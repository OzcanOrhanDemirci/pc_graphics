#include "scenes/altinsaat/altinsaat_renderer.h"
#include "shared/spectrum.h"

#include <nvrtc.h>
#include <optix_function_table_definition.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#define CUDA_CHECK(x)                                                                                   \
    do {                                                                                                \
        cudaError_t e_ = (x);                                                                           \
        if (e_ != cudaSuccess) {                                                                        \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                               \
        }                                                                                               \
    } while (0)
#define OPTIX_CHECK(x)                                                                                    \
    do {                                                                                                  \
        OptixResult r_ = (x);                                                                             \
        if (r_ != OPTIX_SUCCESS) {                                                                        \
            std::fprintf(stderr, "OptiX %s:%d %s: %s\n", __FILE__, __LINE__, #x, optixGetErrorString(r_)); \
            std::exit(1);                                                                                 \
        }                                                                                                 \
    } while (0)

struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) Record {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

static void optixLog(unsigned int level, const char* tag, const char* msg, void*) {
    if (level <= 2) std::fprintf(stderr, "[optix %u][%s] %s\n", level, tag, msg);
}

static std::string readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

template <class T> static T* devAlloc(size_t count) {
    void* p = nullptr;
    CUDA_CHECK(cudaMalloc(&p, count * sizeof(T)));
    return static_cast<T*>(p);
}

bool Renderer::init(unsigned int width, unsigned int height, const std::string& root) {
    root_ = root;
    width_ = width;
    height_ = height;
    cudaDeviceProp prop{};
    CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
    deviceName_ = prop.name;
    CUDA_CHECK(cudaSetDevice(0));
    CUDA_CHECK(cudaFree(nullptr));
    CUDA_CHECK(cudaStreamCreate(&stream_));
    CUDA_CHECK(cudaEventCreate(&ev0_));
    CUDA_CHECK(cudaEventCreate(&ev1_));
    OPTIX_CHECK(optixInit());
    OptixDeviceContextOptions o{};
    o.logCallbackFunction = optixLog;
    o.logCallbackLevel = 4;
    OPTIX_CHECK(optixDeviceContextCreate(nullptr, &o, &context_));

    const size_t n = size_t(width) * height;
    params_.width = width;
    params_.height = height;
    params_.renderWidth = width;
    params_.renderHeight = height;
    params_.accum = devAlloc<float4>(n);
    params_.causticAccum = devAlloc<float4>(n);
    params_.aovAlbedo = devAlloc<float4>(n);
    params_.aovNormal = devAlloc<float4>(n);
    params_.heat = devAlloc<float>(n);
    params_.output = devAlloc<uchar4>(n);
    params_.hdr = devAlloc<float4>(n);
    {
        unsigned int w = width, h = height;
        for (int k = 0; k < kBloomLevels; ++k) {
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
            bloomLevels_[k] = devAlloc<float4>(size_t(w) * h);
            bloomAccum_[k] = devAlloc<float4>(size_t(w) * h);
        }
    }
    params_.nanCounter = devAlloc<unsigned int>(1);
    CUDA_CHECK(cudaMemset(params_.nanCounter, 0, sizeof(unsigned int)));
    params_.skyW = 512;
    params_.skyH = 256;
    params_.sky = devAlloc<float>(size_t(params_.skyW) * params_.skyH * SKY_BINS);
    params_.sunTable = devAlloc<float>(SUN_TABLE_N);
    params_.sppPerLaunch = 1;
    params_.exposure = 1.0f;
    params_.maxDepth = 12;
    params_.enableNee = 1;
    params_.enableSunPatch = 1;
    params_.cameraDispersion = 1;
    params_.skyMapFraction = 0.5f;
    params_.gatherDepth = 2;
    photonGrid_.allocate(1u << 22, 1u << 22);
    params_.photonOut = photonGrid_.raw;
    params_.photonCount = photonGrid_.count;
    params_.photonCapacity = photonGrid_.capacity();
    params_.gridSize = photonGrid_.gridSize();
    params_.cellStart = photonGrid_.cellStart;
    params_.cellEnd = photonGrid_.cellEnd;
    params_.photons = photonGrid_.sorted;
    params_.beamCapacity = 1u << 17;
    params_.beamOut = devAlloc<PhotonBeam>(params_.beamCapacity);
    params_.beams = params_.beamOut;
    params_.beamCount = devAlloc<unsigned int>(1);
    params_.enableBeams = 1;
    params_.enableHaze = 1;
    params_.enablePhotons = 0;
    params_.skyScale = 1.0f;
    params_.groundAlbedo = 0.18f;
    params_.sunLimb = 0.6f;
    const float sunRadius = 0.2666f * 3.14159265f / 180.0f;
    params_.sunOneMinusCosMax = 2.0f * std::sin(0.5f * sunRadius) * std::sin(0.5f * sunRadius);
    params_.sunCosMax = 1.0f - params_.sunOneMinusCosMax;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dParams_), sizeof(Params)));
    return true;
}

bool Renderer::compileDevice(std::string& log) {
    const std::string srcPath = root_ + "/src/scenes/altinsaat/device/altinsaat.cu";
    const std::string src = readText(srcPath);
    if (src.empty()) { log = "cannot read " + srcPath; return false; }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "programs.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) { log = "nvrtcCreateProgram failed"; return false; }
    const std::string inc[] = {"-I" + root_ + "/src", "-I" + root_ + "/src/scenes/altinsaat/device", "-I" + root_ + "/third_party/optix/include",
                               "-I" + root_ + "/third_party/cuda/include"};
    std::vector<const char*> opts = {"--std=c++17", "-arch=compute_89", "--use_fast_math", "--relocatable-device-code=true",
                                     "--optix-ir", "-lineinfo", "-D__x86_64"};
    for (const auto& s : inc) opts.push_back(s.c_str());
    const nvrtcResult cr = nvrtcCompileProgram(prog, int(opts.size()), opts.data());
    size_t logSize = 0;
    nvrtcGetProgramLogSize(prog, &logSize);
    std::string nvlog(logSize, '\0');
    if (logSize > 1) nvrtcGetProgramLog(prog, nvlog.data());
    if (cr != NVRTC_SUCCESS) {
        log = "NVRTC: " + nvlog;
        nvrtcDestroyProgram(&prog);
        return false;
    }
    size_t irSize = 0;
    nvrtcGetOptiXIRSize(prog, &irSize);
    std::string ir(irSize, '\0');
    nvrtcGetOptiXIR(prog, ir.data());
    nvrtcDestroyProgram(&prog);

    OptixModuleCompileOptions mco{};
    mco.maxRegisterCount = OPTIX_COMPILE_DEFAULT_MAX_REGISTER_COUNT;
    mco.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    mco.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;
    OptixPipelineCompileOptions pco{};
    pco.usesMotionBlur = 0;
    pco.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;   // the beam GAS is traced directly
    pco.numPayloadValues = 9;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "params";
    pco.pipelineLaunchParamsSizeInBytes = sizeof(Params);
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE | OPTIX_PRIMITIVE_TYPE_FLAGS_CUSTOM;

    char olog[8192];
    size_t olen = sizeof(olog);
    OptixModule module = nullptr;
    OptixResult r = optixModuleCreate(context_, &mco, &pco, ir.data(), ir.size(), olog, &olen, &module);
    if (r != OPTIX_SUCCESS) { log = std::string("optixModuleCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }

    // program groups: 4 raygen, 2 miss, 4 hit groups
    std::vector<OptixProgramGroupDesc> d(13);
    for (auto& x : d) std::memset(&x, 0, sizeof(x));
    const char* rgNames[6] = {"__raygen__render", "__raygen__sky", "__raygen__tonemap", "__raygen__bakenoise", "__raygen__photons",
                              "__raygen__composite"};
    const int RG = 6;
    for (int i = 0; i < RG; ++i) {
        d[i].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        d[i].raygen.module = module;
        d[i].raygen.entryFunctionName = rgNames[i];
    }
    d[RG + 0].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;            // radiance: traversal only, no program
    d[RG + 1].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[RG + 1].miss.module = module;
    d[RG + 1].miss.entryFunctionName = "__miss__shadow";
    d[RG + 2].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;        // triangles, radiance
    d[RG + 3].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;        // triangles, shadow (opaque)
    d[RG + 4].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;        // spheres, radiance
    d[RG + 4].hitgroup.moduleIS = module;
    d[RG + 4].hitgroup.entryFunctionNameIS = "__intersection__sphere";
    d[RG + 5].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;        // spheres, shadow: soap films transmit
    d[RG + 5].hitgroup.moduleIS = module;
    d[RG + 5].hitgroup.entryFunctionNameIS = "__intersection__sphere";
    d[RG + 5].hitgroup.moduleAH = module;
    d[RG + 5].hitgroup.entryFunctionNameAH = "__anyhit__shadow";
    d[RG + 6].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;        // photon beams (own GAS, sbt offset 4)
    d[RG + 6].hitgroup.moduleIS = module;
    d[RG + 6].hitgroup.entryFunctionNameIS = "__intersection__beam";
    d[RG + 6].hitgroup.moduleAH = module;
    d[RG + 6].hitgroup.entryFunctionNameAH = "__anyhit__beam";
    std::vector<OptixProgramGroup> groups(d.size());
    OptixProgramGroupOptions go{};
    olen = sizeof(olog);
    r = optixProgramGroupCreate(context_, d.data(), unsigned(d.size()), &go, olog, &olen, groups.data());
    if (r != OPTIX_SUCCESS) { log = std::string("optixProgramGroupCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }

    OptixPipelineLinkOptions lo{};
    lo.maxTraceDepth = 1;
    OptixPipeline pipeline = nullptr;
    olen = sizeof(olog);
    r = optixPipelineCreate(context_, &pco, &lo, groups.data(), unsigned(groups.size()), olog, &olen, &pipeline);
    if (r != OPTIX_SUCCESS) { log = std::string("optixPipelineCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    OPTIX_CHECK(optixPipelineSetStackSizeFromCallDepths(pipeline, 1, 0, 0, 0, 2));

    // swap in the new pipeline
    CUDA_CHECK(cudaDeviceSynchronize());
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    pipeline_ = pipeline;
    groups_ = groups;
    module_ = module;

    auto record = [&](OptixProgramGroup g, CUdeviceptr& dst) {
        Record rec{};
        OPTIX_CHECK(optixSbtRecordPackHeader(g, &rec));
        if (!dst) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dst), sizeof(Record)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(dst), &rec, sizeof(Record), cudaMemcpyHostToDevice));
    };
    record(groups_[0], rgRender_);
    record(groups_[1], rgSky_);
    record(groups_[2], rgTonemap_);
    record(groups_[3], rgBake_);
    record(groups_[4], rgPhotons_);
    record(groups_[5], rgComposite_);
    Record miss[2], hit[5];
    OPTIX_CHECK(optixSbtRecordPackHeader(groups_[RG + 0], &miss[0]));
    OPTIX_CHECK(optixSbtRecordPackHeader(groups_[RG + 1], &miss[1]));
    for (int i = 0; i < 5; ++i) OPTIX_CHECK(optixSbtRecordPackHeader(groups_[RG + 2 + i], &hit[i]));
    if (!missRecords_) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&missRecords_), sizeof(miss)));
    if (!hitRecords_) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&hitRecords_), sizeof(hit)));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(missRecords_), miss, sizeof(miss), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(hitRecords_), hit, sizeof(hit), cudaMemcpyHostToDevice));
    sbt_ = {};
    sbt_.missRecordBase = missRecords_;
    sbt_.missRecordStrideInBytes = sizeof(Record);
    sbt_.missRecordCount = 2;
    sbt_.hitgroupRecordBase = hitRecords_;
    sbt_.hitgroupRecordStrideInBytes = sizeof(Record);
    sbt_.hitgroupRecordCount = 5;
    log = nvlog;
    frame_ = 0;
    bakeNoise();
    return true;
}

// The procedural noise texture is baked by the device programs themselves (gnoise2), then sampled with
// hardware bilinear filtering: one fetch per octave instead of eight hashed gradients.
void Renderer::bakeNoise() {
    if (!params_.noiseBake) params_.noiseBake = devAlloc<float4>(size_t(NOISE_TEX) * NOISE_TEX);
    launch(rgBake_, NOISE_TEX, NOISE_TEX, nullptr);
    if (noiseTexObj_) { cudaDestroyTextureObject(noiseTexObj_); noiseTexObj_ = 0; }
    if (!noiseArray_) {
        const cudaChannelFormatDesc fd = cudaCreateChannelDesc<float4>();
        CUDA_CHECK(cudaMallocArray(&noiseArray_, &fd, NOISE_TEX, NOISE_TEX));
    }
    CUDA_CHECK(cudaMemcpy2DToArray(noiseArray_, 0, 0, params_.noiseBake, NOISE_TEX * sizeof(float4), NOISE_TEX * sizeof(float4), NOISE_TEX,
                                   cudaMemcpyDeviceToDevice));
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeArray;
    rd.res.array.array = noiseArray_;
    cudaTextureDesc td{};
    td.addressMode[0] = td.addressMode[1] = cudaAddressModeWrap;
    td.filterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeElementType;
    td.normalizedCoords = 1;
    CUDA_CHECK(cudaCreateTextureObject(&noiseTexObj_, &rd, &td, nullptr));
    params_.noiseTex = noiseTexObj_;
}

template <class T> static T* upload(const std::vector<T>& v, std::vector<void*>& owned) {
    if (v.empty()) return nullptr;
    T* p = devAlloc<T>(v.size());
    CUDA_CHECK(cudaMemcpy(p, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    owned.push_back(p);
    return p;
}

static void* buildGAS(OptixDeviceContext ctx, const OptixBuildInput& bi, unsigned int flags, OptixTraversableHandle& handle) {
    OptixAccelBuildOptions ao{};
    ao.buildFlags = flags;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sizes{};
    OPTIX_CHECK(optixAccelComputeMemoryUsage(ctx, &ao, &bi, 1, &sizes));
    void *temp = nullptr, *out = nullptr;
    CUDA_CHECK(cudaMalloc(&temp, sizes.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(&out, sizes.outputSizeInBytes));
    OPTIX_CHECK(optixAccelBuild(ctx, nullptr, &ao, &bi, 1, CUdeviceptr(temp), sizes.tempSizeInBytes, CUdeviceptr(out),
                                sizes.outputSizeInBytes, &handle, nullptr, 0));
    CUDA_CHECK(cudaDeviceSynchronize());
    cudaFree(temp);
    return out;
}

void Renderer::uploadScene(const Scene& s) {
    CUDA_CHECK(cudaDeviceSynchronize());
    for (void* p : sceneBuffers_) cudaFree(p);
    sceneBuffers_.clear();
    params_.vertices = upload(s.vertices, sceneBuffers_);
    params_.normals = upload(s.normals, sceneBuffers_);
    params_.indices = upload(s.indices, sceneBuffers_);
    params_.triMaterial = upload(s.triMaterial, sceneBuffers_);
    params_.spheres = upload(s.spheres, sceneBuffers_);
    params_.sphereMaterial = upload(s.sphereMaterial, sceneBuffers_);
    params_.materials = upload(s.materials, sceneBuffers_);
    params_.numPortals = int(std::min<size_t>(s.portals.size(), MAX_PORTALS));
    for (int i = 0; i < params_.numPortals; ++i) params_.portals[i] = s.portals[i];
    // openings the sun shines through, as seen from the sun: their outer faces and projected areas
    params_.numSunPortals = 0;
    params_.sunPortalAreaTotal = 0.0f;
    for (size_t i = 0; i < s.portals.size() && params_.numSunPortals < MAX_PORTALS; ++i) {
        const float c = -dot(s.portals[i].normal, s.sunDir);
        if (c <= 1e-4f) continue;
        Portal outer = s.portals[i];
        outer.corner = outer.corner - outer.normal * s.portalDepth[i];
        params_.sunPortals[params_.numSunPortals] = outer;
        params_.sunPortalProj[params_.numSunPortals] = outer.area * c;
        params_.sunPortalDepth[params_.numSunPortals] = s.portalDepth[i];
        params_.sunPortalAreaTotal += outer.area * c;
        ++params_.numSunPortals;
    }
    // caustic photon emitters: footprints of the caustic objects in a plane across the sun direction
    {
        float3 U, V;
        makeBasis(s.sunDir, U, V);
        params_.emitBasisU = U;
        params_.emitBasisV = V;
        params_.numEmitters = 0;
        params_.emitAreaTotal = 0.0f;
        double depth = 0.0;
        for (const Emitter& e : s.emitters) {
            if (params_.numEmitters >= MAX_EMITTERS) break;
            float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
            for (int k = 0; k < 8; ++k) {
                const float3 c = make_float3(k & 1 ? e.boundsMax.x : e.boundsMin.x, k & 2 ? e.boundsMax.y : e.boundsMin.y,
                                             k & 4 ? e.boundsMax.z : e.boundsMin.z);
                u0 = std::min(u0, dot(c, U)); u1 = std::max(u1, dot(c, U));
                v0 = std::min(v0, dot(c, V)); v1 = std::max(v1, dot(c, V));
            }
            const float pad = 0.01f;   // the sun's disk widens the bundle slightly
            params_.emitRect[params_.numEmitters] = make_float4(u0 - pad, v0 - pad, u1 + pad, v1 + pad);
            params_.emitArea[params_.numEmitters] = (u1 - u0 + 2 * pad) * (v1 - v0 + 2 * pad);
            params_.emitAreaTotal += params_.emitArea[params_.numEmitters];
            depth += dot((e.boundsMin + e.boundsMax) * 0.5f, s.sunDir);
            ++params_.numEmitters;
        }
        params_.emitDepth = params_.numEmitters ? float(depth / params_.numEmitters) : 0.0f;
    }
    params_.hazeMin = s.hazeMin;
    params_.hazeMax = s.hazeMax;
    params_.sunDir = s.sunDir;
    buildAccel(s);
    frame_ = 0;
}

void Renderer::updateSpheres(const Scene& s) {
    if (s.spheres.empty()) return;
    CUDA_CHECK(cudaMemcpyAsync(const_cast<float4*>(params_.spheres), s.spheres.data(), s.spheres.size() * sizeof(float4), cudaMemcpyHostToDevice,
                               stream_));
    CUDA_CHECK(cudaStreamSynchronize(stream_));
    buildAccel(s);
}

void Renderer::buildAccel(const Scene& s) {
    if (gasTri_) cudaFree(gasTri_);
    if (gasSph_) cudaFree(gasSph_);
    gasSph_ = nullptr;
    if (ias_) cudaFree(ias_);
    if (instances_) cudaFree(instances_);
    gasTri_ = gasSph_ = ias_ = instances_ = nullptr;

    std::vector<OptixInstance> inst;
    {
        OptixBuildInput bi{};
        bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        CUdeviceptr verts = CUdeviceptr(params_.vertices);
        bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        bi.triangleArray.vertexStrideInBytes = sizeof(float3);
        bi.triangleArray.numVertices = unsigned(s.vertices.size());
        bi.triangleArray.vertexBuffers = &verts;
        bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        bi.triangleArray.indexStrideInBytes = sizeof(uint3);
        bi.triangleArray.numIndexTriplets = unsigned(s.indices.size());
        bi.triangleArray.indexBuffer = CUdeviceptr(params_.indices);
        const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
        bi.triangleArray.flags = flags;
        bi.triangleArray.numSbtRecords = 1;
        OptixTraversableHandle h = 0;
        gasTri_ = buildGAS(context_, bi, OPTIX_BUILD_FLAG_PREFER_FAST_TRACE, h);
        OptixInstance in{};
        const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        std::memcpy(in.transform, id, sizeof(id));
        in.instanceId = 0;
        in.sbtOffset = 0;
        in.visibilityMask = 1u;
        in.flags = OPTIX_INSTANCE_FLAG_NONE;
        in.traversableHandle = h;
        inst.push_back(in);
    }
    if (!s.spheres.empty()) {
        // custom primitives: one AABB per sphere, intersected by __intersection__sphere. Two GASes: the glass,
        // water and bubbles (seen by every ray, mask 1) and the dust (camera rays only, mask 2).
        std::vector<OptixAabb> boxes(s.spheres.size());
        for (size_t i = 0; i < s.spheres.size(); ++i) {
            const float4 c = s.spheres[i];
            boxes[i] = {c.x - c.w, c.y - c.w, c.z - c.w, c.x + c.w, c.y + c.w, c.z + c.w};
        }
        if (aabbs_) cudaFree(aabbs_);
        CUDA_CHECK(cudaMalloc(&aabbs_, boxes.size() * sizeof(OptixAabb)));
        CUDA_CHECK(cudaMemcpy(aabbs_, boxes.data(), boxes.size() * sizeof(OptixAabb), cudaMemcpyHostToDevice));
        const unsigned int dustBegin = s.dustEnd > s.dustBegin ? unsigned(s.dustBegin) : unsigned(boxes.size());
        const unsigned int ranges[2][2] = {{0u, dustBegin}, {dustBegin, unsigned(boxes.size())}};
        void** gases[2] = {&gasSph_, &gasDust_};
        if (gasDust_) { cudaFree(gasDust_); gasDust_ = nullptr; }
        for (int g = 0; g < 2; ++g) {
            const unsigned int first = ranges[g][0], count = ranges[g][1] - ranges[g][0];
            if (count == 0) continue;
            OptixBuildInput bi{};
            bi.type = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;
            CUdeviceptr aabbPtr = CUdeviceptr(aabbs_) + first * sizeof(OptixAabb);
            bi.customPrimitiveArray.aabbBuffers = &aabbPtr;
            bi.customPrimitiveArray.numPrimitives = count;
            bi.customPrimitiveArray.strideInBytes = sizeof(OptixAabb);
            bi.customPrimitiveArray.primitiveIndexOffset = first;      // optixGetPrimitiveIndex stays global
            const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_NONE};
            bi.customPrimitiveArray.flags = flags;
            bi.customPrimitiveArray.numSbtRecords = 1;
            OptixTraversableHandle h = 0;
            *gases[g] = buildGAS(context_, bi, OPTIX_BUILD_FLAG_PREFER_FAST_TRACE, h);
            OptixInstance in{};
            const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
            std::memcpy(in.transform, id, sizeof(id));
            in.instanceId = 1;
            in.sbtOffset = 2;
            in.visibilityMask = g == 0 ? 1u : 2u;
            in.flags = OPTIX_INSTANCE_FLAG_NONE;
            in.traversableHandle = h;
            inst.push_back(in);
        }
    }
    CUDA_CHECK(cudaMalloc(&instances_, inst.size() * sizeof(OptixInstance)));
    CUDA_CHECK(cudaMemcpy(instances_, inst.data(), inst.size() * sizeof(OptixInstance), cudaMemcpyHostToDevice));
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = CUdeviceptr(instances_);
    bi.instanceArray.numInstances = unsigned(inst.size());
    OptixTraversableHandle h = 0;
    ias_ = buildGAS(context_, bi, OPTIX_BUILD_FLAG_PREFER_FAST_TRACE, h);
    params_.handle = h;
}

void Renderer::launch(CUdeviceptr raygenRecord, unsigned int w, unsigned int h, float* ms) {
    sbt_.raygenRecord = raygenRecord;
    CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(dParams_), &params_, sizeof(Params), cudaMemcpyHostToDevice, stream_));
    CUDA_CHECK(cudaEventRecord(ev0_, stream_));
    OPTIX_CHECK(optixLaunch(pipeline_, stream_, dParams_, sizeof(Params), &sbt_, w, h, 1));
    CUDA_CHECK(cudaEventRecord(ev1_, stream_));
    CUDA_CHECK(cudaEventSynchronize(ev1_));
    if (ms) cudaEventElapsedTime(ms, ev0_, ev1_);
}

void Renderer::updateSky() {
    launch(rgSky_, params_.skyW, params_.skyH, &times_.sky);
    frame_ = 0;
    // sampling tables for the sky map: probability ~ luminance x solid angle of each texel
    const int W = params_.skyW, H = params_.skyH;
    std::vector<float> sky(size_t(W) * H * SKY_BINS);
    CUDA_CHECK(cudaMemcpy(sky.data(), params_.sky, sky.size() * sizeof(float), cudaMemcpyDeviceToHost));
    float ybar[SKY_BINS];
    for (int b = 0; b < SKY_BINS; ++b) ybar[b] = cieXYZ(LAMBDA_MIN + b * SKY_BIN_NM).y;
    std::vector<double> wgt(size_t(W) * H);
    std::vector<float> rowCdf(H + 1), colCdf(size_t(H) * (W + 1)), pdfUV(size_t(W) * H);
    std::vector<double> rowSum(H, 0.0);
    double total = 0.0;
    for (int y = 0; y < H; ++y) {
        const float v = (y + 0.5f) / H, sv = 2.0f * v - 1.0f;
        const float e = std::copysign(sv * sv, sv) * 1.5707963f;
        const double jac = std::cos(e) * std::fabs(sv);
        for (int x = 0; x < W; ++x) {
            const float* c = &sky[(size_t(y) * W + x) * SKY_BINS];
            double Y = 0.0;
            for (int b = 0; b < SKY_BINS; ++b) Y += c[b] * ybar[b];
            const double w = std::max(Y, 0.0) * jac;
            wgt[size_t(y) * W + x] = w;
            rowSum[y] += w;
        }
        total += rowSum[y];
    }
    if (total <= 0.0) return;
    double acc = 0.0;
    for (int y = 0; y < H; ++y) {
        rowCdf[y] = float(acc / total);
        acc += rowSum[y];
        double ra = 0.0;
        for (int x = 0; x < W; ++x) {
            colCdf[size_t(y) * (W + 1) + x] = rowSum[y] > 0 ? float(ra / rowSum[y]) : float(x) / W;
            ra += wgt[size_t(y) * W + x];
            pdfUV[size_t(y) * W + x] = float(wgt[size_t(y) * W + x] / total * W * H);
        }
        colCdf[size_t(y) * (W + 1) + W] = 1.0f;
    }
    rowCdf[H] = 1.0f;
    auto put = [&](const std::vector<float>& v, const float*& dst) {
        if (!dst) { float* d = nullptr; CUDA_CHECK(cudaMalloc(&d, v.size() * sizeof(float))); dst = d; }
        CUDA_CHECK(cudaMemcpy(const_cast<float*>(dst), v.data(), v.size() * sizeof(float), cudaMemcpyHostToDevice));
    };
    put(pdfUV, params_.skyPdfUV);
    put(rowCdf, params_.skyCdfRow);
    put(colCdf, params_.skyCdfCol);
}

// One caustic photon map per frame. The gather radius shrinks as r0 * n^(-1/6) (progressive photon mapping
// with alpha = 2/3, Knaus & Zwicker 2011): every frame's estimate is averaged, bias and noise both vanish.
void Renderer::tracePhotons() {
    params_.numPhotons = 0;
    if (!params_.enablePhotons || params_.numEmitters == 0) return;
    CUDA_CHECK(cudaMemsetAsync(photonGrid_.count, 0, sizeof(unsigned int), stream_));
    CUDA_CHECK(cudaMemsetAsync(params_.beamCount, 0, sizeof(unsigned int), stream_));
    params_.photonsToEmit = photonsPerFrame;
    params_.numBeams = 0;
    launch(rgPhotons_, photonsPerFrame, 1, &times_.photons);
    buildBeams();
    const float r = liveRadius ? photonRadius0 : photonRadius0 * std::pow(float(params_.photonFrame + 1), -1.0f / 6.0f);
    params_.gatherRadius = r;
    params_.cellSize = 2.0f * r;
    cudaEventRecord(ev0_, stream_);
    params_.numPhotons = photonGrid_.build(params_.cellSize, stream_);
    cudaEventRecord(ev1_, stream_);
    cudaEventSynchronize(ev1_);
    cudaEventElapsedTime(&times_.grid, ev0_, ev1_);
    ++params_.photonFrame;
}

// The caustic beams of this frame's photons get their own acceleration structure (custom primitives: one
// box around each beam's blur cylinder). Radius shrinks as r0 * n^(-1/3) (1D blur, alpha = 2/3).
void Renderer::buildBeams() {
    unsigned int n = 0;
    CUDA_CHECK(cudaMemcpyAsync(&n, params_.beamCount, sizeof(n), cudaMemcpyDeviceToHost, stream_));
    CUDA_CHECK(cudaStreamSynchronize(stream_));
    n = std::min(n, params_.beamCapacity);
    params_.numBeams = 0;
    if (!params_.enableBeams || n == 0) return;
    params_.beamRadius = liveRadius ? beamRadius0 : beamRadius0 * std::pow(float(params_.photonFrame + 1), -1.0f / 3.0f);
    if (!beamBoxes_) {
        CUDA_CHECK(cudaMalloc(&beamBoxes_, size_t(params_.beamCapacity) * BEAM_PIECES * sizeof(OptixAabb)));
        OptixBuildInput bi{};
        bi.type = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;
        CUdeviceptr ptr = CUdeviceptr(beamBoxes_);
        bi.customPrimitiveArray.aabbBuffers = &ptr;
        bi.customPrimitiveArray.numPrimitives = params_.beamCapacity * BEAM_PIECES;
        const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_NONE};
        bi.customPrimitiveArray.flags = flags;
        bi.customPrimitiveArray.numSbtRecords = 1;
        OptixAccelBuildOptions ao{};
        ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
        ao.operation = OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes sizes{};
        OPTIX_CHECK(optixAccelComputeMemoryUsage(context_, &ao, &bi, 1, &sizes));
        beamTempSize_ = sizes.tempSizeInBytes;
        beamOutSize_ = sizes.outputSizeInBytes;
        CUDA_CHECK(cudaMalloc(&beamTemp_, beamTempSize_));
        CUDA_CHECK(cudaMalloc(&beamGas_, beamOutSize_));
    }
    launchBeamBoxes(params_.beams, n, params_.beamRadius, beamBoxes_, stream_);
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_CUSTOM_PRIMITIVES;
    CUdeviceptr ptr = CUdeviceptr(beamBoxes_);
    bi.customPrimitiveArray.aabbBuffers = &ptr;
    bi.customPrimitiveArray.numPrimitives = n * BEAM_PIECES;
    const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_NONE};
    bi.customPrimitiveArray.flags = flags;
    bi.customPrimitiveArray.numSbtRecords = 1;
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OPTIX_CHECK(optixAccelBuild(context_, stream_, &ao, &bi, 1, CUdeviceptr(beamTemp_), beamTempSize_, CUdeviceptr(beamGas_), beamOutSize_,
                                &params_.beamHandle, nullptr, 0));
    params_.numBeams = n;
}

void Renderer::renderFrame() {
    params_.frame = frame_;
    params_.denoised = nullptr;      // stale once more samples arrive
    params_.denoisedCaustic = nullptr;
    tracePhotons();
    launch(rgRender_, params_.renderWidth, params_.renderHeight, &times_.render);
    ++frame_;
}

void Renderer::setHalfResolution(bool half) {
    params_.renderWidth = half ? width_ / 2 : width_;
    params_.renderHeight = half ? height_ / 2 : height_;
    frame_ = 0;
    params_.photonFrame = 0;
}

void Renderer::denoise(float blend, bool caustics) {
    if (halfResolution()) {
        if (!upscaleReady_) {
            upscaleReady_ = upscaleDenoiser_.create(context_, params_.renderWidth, params_.renderHeight, true, true);
            if (!upscaleReady_) { std::fprintf(stderr, "upscaling denoiser unavailable\n"); return; }
        }
        upscaleDenoiser_.run(params_, blend, stream_, caustics);
        params_.denoised = upscaleDenoiser_.output();
        params_.denoisedCaustic = caustics ? upscaleDenoiser_.causticOutput() : nullptr;
        return;
    }
    if (useTemporalDenoiser) {
        if (!temporalReady_) {
            temporalReady_ = temporalDenoiser_.create(context_, width_, height_, true);
            if (!temporalReady_) { std::fprintf(stderr, "temporal denoiser unavailable\n"); useTemporalDenoiser = false; return; }
        }
        temporalDenoiser_.run(params_, blend, stream_, caustics);
        params_.denoised = temporalDenoiser_.output();
        params_.denoisedCaustic = caustics ? temporalDenoiser_.causticOutput() : nullptr;
        return;
    }
    if (!denoiserReady_) {
        denoiserReady_ = denoiser_.create(context_, width_, height_);
        if (!denoiserReady_) { std::fprintf(stderr, "denoiser unavailable\n"); return; }
    }
    denoiser_.run(params_, blend, stream_, caustics);
    params_.denoised = denoiser_.output();
    params_.denoisedCaustic = caustics ? denoiser_.causticOutput() : nullptr;
}

void Renderer::tonemap() {
    launch(rgTonemap_, width_, height_, &times_.tonemap);
    if (params_.view != 0 && params_.view != 3) return;   // debug views were written directly
    params_.bloomStrength = bloomStrength;
    params_.bloom = nullptr;
    if (bloomStrength > 0.0f) {
        launchBloom(params_.hdr, int(width_), int(height_), bloomLevels_, bloomAccum_, kBloomLevels, stream_);
        params_.bloom = bloomAccum_[0];
    }
    float ms = 0.0f;
    launch(rgComposite_, width_, height_, &ms);
    times_.tonemap += ms;
}

void Renderer::downloadOutput(std::vector<uchar4>& out) {
    out.resize(size_t(width_) * height_);
    CUDA_CHECK(cudaMemcpy(out.data(), params_.output, out.size() * sizeof(uchar4), cudaMemcpyDeviceToHost));
}

void Renderer::downloadAccum(std::vector<float4>& out, bool caustic) {
    out.resize(size_t(width_) * height_);
    CUDA_CHECK(cudaMemcpy(out.data(), caustic ? params_.causticAccum : params_.accum, out.size() * sizeof(float4), cudaMemcpyDeviceToHost));
}

unsigned int Renderer::nanCount() {
    unsigned int n = 0;
    CUDA_CHECK(cudaMemcpy(&n, params_.nanCounter, sizeof(n), cudaMemcpyDeviceToHost));
    return n;
}

void Renderer::shutdown() {
    cudaDeviceSynchronize();
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    if (context_) optixDeviceContextDestroy(context_);
    pipeline_ = nullptr;
    groups_.clear();
    module_ = nullptr;
    context_ = nullptr;
}
