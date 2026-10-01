#include "renderer.h"

#include <nvrtc.h>
#include <optix_function_table_definition.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>

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
    params_.accum = devAlloc<float4>(n);
    params_.causticAccum = devAlloc<float4>(n);
    params_.aovAlbedo = devAlloc<float4>(n);
    params_.aovNormal = devAlloc<float4>(n);
    params_.heat = devAlloc<float>(n);
    params_.output = devAlloc<uchar4>(n);
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
    const std::string srcPath = root_ + "/src/device/programs.cu";
    const std::string src = readText(srcPath);
    if (src.empty()) { log = "cannot read " + srcPath; return false; }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "programs.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) { log = "nvrtcCreateProgram failed"; return false; }
    const std::string inc[] = {"-I" + root_ + "/src", "-I" + root_ + "/src/device", "-I" + root_ + "/third_party/optix/include",
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
    pco.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    pco.numPayloadValues = 9;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "params";
    pco.pipelineLaunchParamsSizeInBytes = sizeof(Params);
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE | OPTIX_PRIMITIVE_TYPE_FLAGS_SPHERE;

    char olog[8192];
    size_t olen = sizeof(olog);
    OptixModule module = nullptr;
    OptixResult r = optixModuleCreate(context_, &mco, &pco, ir.data(), ir.size(), olog, &olen, &module);
    if (r != OPTIX_SUCCESS) { log = std::string("optixModuleCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    OptixModule sphereModule = nullptr;
    OptixBuiltinISOptions bis{};
    bis.builtinISModuleType = OPTIX_PRIMITIVE_TYPE_SPHERE;
    bis.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE | OPTIX_BUILD_FLAG_ALLOW_UPDATE;
    OPTIX_CHECK(optixBuiltinISModuleGet(context_, &mco, &pco, &bis, &sphereModule));

    // program groups: 3 raygen, 2 miss, 4 hit groups
    std::vector<OptixProgramGroupDesc> d(9);
    for (auto& x : d) std::memset(&x, 0, sizeof(x));
    const char* rgNames[3] = {"__raygen__render", "__raygen__sky", "__raygen__tonemap"};
    for (int i = 0; i < 3; ++i) {
        d[i].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        d[i].raygen.module = module;
        d[i].raygen.entryFunctionName = rgNames[i];
    }
    d[3].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;                 // radiance: traversal only, no program
    d[4].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[4].miss.module = module;
    d[4].miss.entryFunctionName = "__miss__shadow";
    d[5].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;             // triangles, radiance
    d[6].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;             // triangles, shadow (opaque)
    d[7].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;             // spheres, radiance
    d[7].hitgroup.moduleIS = sphereModule;
    d[8].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;             // spheres, shadow: soap films transmit
    d[8].hitgroup.moduleIS = sphereModule;
    d[8].hitgroup.moduleAH = module;
    d[8].hitgroup.entryFunctionNameAH = "__anyhit__shadow";
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
    sphereModule_ = sphereModule;

    auto record = [&](OptixProgramGroup g, CUdeviceptr& dst) {
        Record rec{};
        OPTIX_CHECK(optixSbtRecordPackHeader(g, &rec));
        if (!dst) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dst), sizeof(Record)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(dst), &rec, sizeof(Record), cudaMemcpyHostToDevice));
    };
    record(groups_[0], rgRender_);
    record(groups_[1], rgSky_);
    record(groups_[2], rgTonemap_);
    Record miss[2], hit[4];
    OPTIX_CHECK(optixSbtRecordPackHeader(groups_[3], &miss[0]));
    OPTIX_CHECK(optixSbtRecordPackHeader(groups_[4], &miss[1]));
    for (int i = 0; i < 4; ++i) OPTIX_CHECK(optixSbtRecordPackHeader(groups_[5 + i], &hit[i]));
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
    sbt_.hitgroupRecordCount = 4;
    log = nvlog;
    frame_ = 0;
    return true;
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
    params_.hazeMin = s.hazeMin;
    params_.hazeMax = s.hazeMax;
    params_.sunDir = s.sunDir;
    buildAccel(s);
    frame_ = 0;
}

void Renderer::buildAccel(const Scene& s) {
    if (gasTri_) cudaFree(gasTri_);
    if (gasSph_) cudaFree(gasSph_);
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
        in.visibilityMask = 255;
        in.flags = OPTIX_INSTANCE_FLAG_NONE;
        in.traversableHandle = h;
        inst.push_back(in);
    }
    if (!s.spheres.empty()) {
        OptixBuildInput bi{};
        bi.type = OPTIX_BUILD_INPUT_TYPE_SPHERES;
        CUdeviceptr centers = CUdeviceptr(params_.spheres);
        CUdeviceptr radii = CUdeviceptr(reinterpret_cast<const char*>(params_.spheres) + 12);
        bi.sphereArray.vertexBuffers = &centers;
        bi.sphereArray.vertexStrideInBytes = sizeof(float4);
        bi.sphereArray.numVertices = unsigned(s.spheres.size());
        bi.sphereArray.radiusBuffers = &radii;
        bi.sphereArray.radiusStrideInBytes = sizeof(float4);
        const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_NONE};
        bi.sphereArray.flags = flags;
        bi.sphereArray.numSbtRecords = 1;
        OptixTraversableHandle h = 0;
        gasSph_ = buildGAS(context_, bi, OPTIX_BUILD_FLAG_PREFER_FAST_TRACE | OPTIX_BUILD_FLAG_ALLOW_UPDATE, h);
        OptixInstance in{};
        const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        std::memcpy(in.transform, id, sizeof(id));
        in.instanceId = 1;
        in.sbtOffset = 2;
        in.visibilityMask = 255;
        in.flags = OPTIX_INSTANCE_FLAG_NONE;
        in.traversableHandle = h;
        inst.push_back(in);
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
}

void Renderer::renderFrame() {
    params_.frame = frame_;
    launch(rgRender_, width_, height_, &times_.render);
    ++frame_;
}

void Renderer::tonemap() {
    launch(rgTonemap_, width_, height_, &times_.tonemap);
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
