#include "scenes/koy/koy_renderer.h"
#include "scenes/koy/koy_kernels.h"
#include "scenes/koy/koy_sampling.h"

#include <nvrtc.h>
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

struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) KRecord {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

static const unsigned int kRibbonBuildFlags =
    OPTIX_BUILD_FLAG_ALLOW_UPDATE | OPTIX_BUILD_FLAG_ALLOW_RANDOM_VERTEX_ACCESS | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;

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
    CUDA_CHECK(cudaMalloc(&p, std::max<size_t>(count, 1) * sizeof(T)));
    CUDA_CHECK(cudaMemset(p, 0, std::max<size_t>(count, 1) * sizeof(T)));
    return static_cast<T*>(p);
}
template <class T> static T* upload(const std::vector<T>& v, std::vector<void*>& owned) {
    if (v.empty()) return nullptr;
    T* p = nullptr;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&p), v.size() * sizeof(T)));
    CUDA_CHECK(cudaMemcpy(p, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    owned.push_back(p);
    return p;
}

bool KoyRenderer::init(unsigned int width, unsigned int height, const std::string& root) {
    root_ = root;
    width_ = width;
    height_ = height;
    cudaDeviceProp prop{};
    CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
    deviceName_ = prop.name;
    CUDA_CHECK(cudaSetDevice(0));
    CUDA_CHECK(cudaFree(nullptr));
    CUDA_CHECK(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking));
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
    params_.hdr = devAlloc<float4>(n);
    params_.output = devAlloc<uchar4>(n);
    params_.edgeIndex = devAlloc<unsigned int>(n);
    params_.edgeCapacity = unsigned(n / 3);
    params_.edgeList = devAlloc<unsigned int>(params_.edgeCapacity);
    params_.edgeAcc = devAlloc<float4>(size_t(params_.edgeCapacity) * 9);
    params_.edgeCount = devAlloc<unsigned int>(1);
    meterBuf_ = devAlloc<float>(koyMeterTiles(int(width), int(height)));
    {   // the shaft knots: (W/2 + 2) x (H/2 + 2) blocks x knots, float4, written as a surface and read as a texture
        params_.shaftBlocksX = int(width / 2 + 2);
        params_.shaftBlocksY = int(height / 2 + 2);
        params_.shaftKnots = 32;
        cudaChannelFormatDesc fd = cudaCreateChannelDesc<float4>();
        CUDA_CHECK(cudaMalloc3DArray(&shaftArr_, &fd, make_cudaExtent(params_.shaftBlocksX, params_.shaftBlocksY, params_.shaftKnots),
                                     cudaArraySurfaceLoadStore));
        cudaResourceDesc rd{};
        rd.resType = cudaResourceTypeArray;
        rd.res.array.array = shaftArr_;
        cudaSurfaceObject_t so = 0;
        CUDA_CHECK(cudaCreateSurfaceObject(&so, &rd));
        params_.shaftSurf = so;
        cudaTextureDesc td{};
        td.addressMode[0] = td.addressMode[1] = td.addressMode[2] = cudaAddressModeClamp;
        td.filterMode = cudaFilterModeLinear;
        td.readMode = cudaReadModeElementType;
        td.normalizedCoords = 0;
        cudaTextureObject_t to = 0;
        CUDA_CHECK(cudaCreateTextureObject(&to, &rd, &td, nullptr));
        params_.shaftTex = to;
    }
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dParams_), sizeof(KoyParams)));
    setSampling(4, 32, 1, 1.5f, 0.04f);
    return true;
}

void KoyRenderer::allocSamples() {
    if (allocatedBase_ == params_.baseSpp && params_.samples) return;
    if (params_.samples) cudaFree(params_.samples);
    params_.samples = devAlloc<float4>(size_t(width_) * height_ * params_.baseSpp);
    allocatedBase_ = params_.baseSpp;
}

void KoyRenderer::setSampling(int baseSpp, int edgeSpp, int filter, float radius, float edgeContrast) {
    baseSpp = baseSpp <= 1 ? 1 : baseSpp <= 4 ? 4 : baseSpp <= 8 ? 8 : 16;
    edgeSpp = std::min(std::max(edgeSpp, baseSpp), KOY_MAX_EDGE);
    const std::vector<float2> base = koyBasePattern(baseSpp);
    const std::vector<float2> edge = koyEdgePattern(base, KOY_MAX_EDGE);
    params_.baseSpp = baseSpp;
    params_.edgeSpp = edgeSpp;
    for (int k = 0; k < baseSpp; ++k) params_.basePattern[k] = base[k];
    for (int k = 0; k < KOY_MAX_EDGE; ++k) params_.edgePattern[k] = edge[k];
    params_.filter = filter;
    params_.filterRadius = filter == 0 ? 0.5f : std::min(radius, 1.5f);
    params_.edgeContrast = edgeContrast;
    if (params_.edgeIdShare <= 0.0f) params_.edgeIdShare = 0.25f;
    // the extra samples' filter weights towards each neighbour, summed (the resolve divides by the total weight)
    for (int jy = -1; jy <= 1; ++jy)
        for (int jx = -1; jx <= 1; ++jx) {
            float wb = 0.0f, we = 0.0f;
            for (int s = 0; s < baseSpp; ++s)
                wb += koyFilter(filter, params_.filterRadius, base[s].x - 0.5f - jx) * koyFilter(filter, params_.filterRadius, base[s].y - 0.5f - jy);
            for (int s = baseSpp; s < edgeSpp; ++s)
                we += koyFilter(filter, params_.filterRadius, edge[s].x - 0.5f - jx) * koyFilter(filter, params_.filterRadius, edge[s].y - 0.5f - jy);
            params_.filterW[jy + 1][jx + 1] = wb / baseSpp;
            params_.filterWEdge[jy + 1][jx + 1] = we / std::max(edgeSpp, 1);
        }
    allocSamples();
}

bool KoyRenderer::compileDevice(std::string& log) {
    const std::string srcPath = root_ + "/src/scenes/koy/device/koy.cu";
    const std::string src = readText(srcPath);
    if (src.empty()) { log = "cannot read " + srcPath; return false; }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "koy.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) { log = "nvrtcCreateProgram failed"; return false; }
    const std::string inc[] = {"-I" + root_ + "/src", "-I" + root_ + "/src/scenes/koy/device", "-I" + root_ + "/third_party/optix/include",
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
    pco.numPayloadValues = 2;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "params";
    pco.pipelineLaunchParamsSizeInBytes = sizeof(KoyParams);
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE | OPTIX_PRIMITIVE_TYPE_FLAGS_FLAT_QUADRATIC_BSPLINE;

    char olog[8192];
    size_t olen = sizeof(olog);
    OptixModule module = nullptr;
    OptixResult r = optixModuleCreate(context_, &mco, &pco, ir.data(), ir.size(), olog, &olen, &module);
    if (r != OPTIX_SUCCESS) { log = std::string("optixModuleCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    OptixModule curveModule = nullptr;
    OptixBuiltinISOptions bio{};
    bio.builtinISModuleType = OPTIX_PRIMITIVE_TYPE_FLAT_QUADRATIC_BSPLINE;
    bio.usesMotionBlur = 0;
    bio.buildFlags = kRibbonBuildFlags;
    bio.curveEndcapFlags = OPTIX_CURVE_ENDCAP_DEFAULT;
    r = optixBuiltinISModuleGet(context_, &mco, &pco, &bio, &curveModule);
    if (r != OPTIX_SUCCESS) { log = std::string("optixBuiltinISModuleGet: ") + optixGetErrorString(r); return false; }

    const char* rgNames[9] = {"__raygen__base", "__raygen__edge", "__raygen__resolve", "__raygen__final", "__raygen__bakeAO", "__raygen__bakeSun",
                              "__raygen__shafts", "__raygen__edgeFind", "__raygen__probe"};
    const int RG = 9;
    std::vector<OptixProgramGroupDesc> d(RG + 3);
    for (auto& x : d) std::memset(&x, 0, sizeof(x));
    for (int i = 0; i < RG; ++i) {
        d[i].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        d[i].raygen.module = module;
        d[i].raygen.entryFunctionName = rgNames[i];
    }
    d[RG + 0].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;        // traversal only: no programs
    d[RG + 1].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;    // built-in triangles
    d[RG + 2].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;    // ribbons: the built-in intersection
    d[RG + 2].hitgroup.moduleIS = curveModule;
    d[RG + 2].hitgroup.entryFunctionNameIS = nullptr;
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

    CUDA_CHECK(cudaDeviceSynchronize());
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    if (curveModule_) optixModuleDestroy(curveModule_);
    pipeline_ = pipeline;
    groups_ = groups;
    module_ = module;
    curveModule_ = curveModule;

    auto record = [&](OptixProgramGroup g, CUdeviceptr& dst) {
        KRecord rec{};
        OPTIX_CHECK(optixSbtRecordPackHeader(g, &rec));
        if (!dst) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dst), sizeof(KRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(dst), &rec, sizeof(KRecord), cudaMemcpyHostToDevice));
    };
    record(groups_[0], rgBase_);
    record(groups_[1], rgEdge_);
    record(groups_[2], rgResolve_);
    record(groups_[3], rgFinal_);
    record(groups_[4], rgBakeAO_);
    record(groups_[5], rgBakeSun_);
    record(groups_[6], rgShafts_);
    record(groups_[7], rgEdgeFind_);
    record(groups_[8], rgProbe_);
    record(groups_[RG + 0], missRecord_);
    {   // two hit records: [0] triangles, [1] ribbons (instances pick theirs with sbtOffset)
        KRecord recs[2];
        OPTIX_CHECK(optixSbtRecordPackHeader(groups_[RG + 1], &recs[0]));
        OPTIX_CHECK(optixSbtRecordPackHeader(groups_[RG + 2], &recs[1]));
        if (!hitRecords_) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&hitRecords_), sizeof(recs)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(hitRecords_), recs, sizeof(recs), cudaMemcpyHostToDevice));
    }
    sbt_ = {};
    sbt_.missRecordBase = missRecord_;
    sbt_.missRecordStrideInBytes = sizeof(KRecord);
    sbt_.missRecordCount = 1;
    sbt_.hitgroupRecordBase = hitRecords_;
    sbt_.hitgroupRecordStrideInBytes = sizeof(KRecord);
    sbt_.hitgroupRecordCount = 2;
    log = nvlog;
    return true;
}

static void* buildAS(OptixDeviceContext ctx, const OptixBuildInput& bi, unsigned int flags, OptixTraversableHandle& handle) {
    OptixAccelBuildOptions ao{};
    ao.buildFlags = flags;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sizes{};
    OPTIX_CHECK(optixAccelComputeMemoryUsage(ctx, &ao, &bi, 1, &sizes));
    void *temp = nullptr, *out = nullptr;
    CUDA_CHECK(cudaMalloc(&temp, sizes.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(&out, sizes.outputSizeInBytes));
    OPTIX_CHECK(optixAccelBuild(ctx, nullptr, &ao, &bi, 1, CUdeviceptr(temp), sizes.tempSizeInBytes, CUdeviceptr(out), sizes.outputSizeInBytes,
                                &handle, nullptr, 0));
    CUDA_CHECK(cudaDeviceSynchronize());
    cudaFree(temp);
    return out;
}

void KoyRenderer::uploadWorld(const std::vector<KMeshCPU>& meshes, const std::vector<KInstanceCPU>& instances, const std::vector<KMaterial>& materials,
                              const std::vector<KTextureSet>& texSets, const KRibbonsCPU* ribbons) {
    CUDA_CHECK(cudaDeviceSynchronize());
    for (void* p : worldBuffers_) cudaFree(p);
    worldBuffers_.clear();
    meshesCPU_ = meshes;
    instancesCPU_ = instances;
    gas_.assign(meshes.size(), 0);
    for (size_t k = 0; k < meshes.size() && k < KOY_MAX_MESHES; ++k) {
        const KMeshCPU& m = meshes[k];
        KMesh& dm = params_.meshes[k];
        dm = {};
        dm.pos = upload(m.pos, worldBuffers_);
        dm.nrm = upload(m.nrm, worldBuffers_);
        dm.uv = upload(m.uv, worldBuffers_);
        dm.idx = upload(m.idx, worldBuffers_);
        dm.ao = nullptr;
        dm.material = m.material;
        dm.id = m.id;
        if (m.idx.empty()) continue;
        OptixBuildInput bi{};
        bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        CUdeviceptr verts = CUdeviceptr(dm.pos);
        bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        bi.triangleArray.vertexStrideInBytes = sizeof(float3);
        bi.triangleArray.numVertices = unsigned(m.pos.size());
        bi.triangleArray.vertexBuffers = &verts;
        bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        bi.triangleArray.indexStrideInBytes = sizeof(uint3);
        bi.triangleArray.numIndexTriplets = unsigned(m.idx.size());
        bi.triangleArray.indexBuffer = CUdeviceptr(dm.idx);
        const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
        bi.triangleArray.flags = flags;
        bi.triangleArray.numSbtRecords = 1;
        worldBuffers_.push_back(buildAS(context_, bi, OPTIX_BUILD_FLAG_PREFER_FAST_TRACE, gas_[k]));
    }
    // the ribbons
    ribbonGas_ = 0;
    ribbonPts_ = nullptr;
    ribbonNrm_ = nullptr;
    ribbonPointCount_ = ribbonSegCount_ = 0;
    int ribbonMesh = -1;
    if (ribbons && !ribbons->segments.empty() && meshes.size() < KOY_MAX_MESHES) {
        ribbonMesh = int(meshes.size());
        ribbonPointCount_ = unsigned(ribbons->points.size());
        ribbonSegCount_ = unsigned(ribbons->segments.size());
        ribbonPts_ = upload(ribbons->points, worldBuffers_);
        ribbonNrm_ = upload(ribbons->normals, worldBuffers_);
        ribbonSeg_ = upload(ribbons->segments, worldBuffers_);
        KMesh& dm = params_.meshes[ribbonMesh];
        dm = {};
        dm.material = ribbons->material;
        dm.id = 0x10000u;
        OptixBuildInput bi{};
        bi.type = OPTIX_BUILD_INPUT_TYPE_CURVES;
        CUdeviceptr verts = CUdeviceptr(ribbonPts_), widths = CUdeviceptr(reinterpret_cast<char*>(ribbonPts_) + 12),
                    nrms = CUdeviceptr(ribbonNrm_);
        bi.curveArray.curveType = OPTIX_PRIMITIVE_TYPE_FLAT_QUADRATIC_BSPLINE;
        bi.curveArray.numPrimitives = ribbonSegCount_;
        bi.curveArray.vertexBuffers = &verts;
        bi.curveArray.numVertices = ribbonPointCount_;
        bi.curveArray.vertexStrideInBytes = sizeof(float4);
        bi.curveArray.widthBuffers = &widths;
        bi.curveArray.widthStrideInBytes = sizeof(float4);
        bi.curveArray.normalBuffers = &nrms;
        bi.curveArray.normalStrideInBytes = sizeof(float3);
        bi.curveArray.indexBuffer = CUdeviceptr(ribbonSeg_);
        bi.curveArray.indexStrideInBytes = sizeof(unsigned int);
        bi.curveArray.flag = OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT;
        bi.curveArray.primitiveIndexOffset = 0;
        bi.curveArray.endcapFlags = OPTIX_CURVE_ENDCAP_DEFAULT;
        OptixAccelBuildOptions ao{};
        ao.buildFlags = kRibbonBuildFlags;
        ao.operation = OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes sizes{};
        OPTIX_CHECK(optixAccelComputeMemoryUsage(context_, &ao, &bi, 1, &sizes));
        ribbonTempSize_ = std::max(sizes.tempSizeInBytes, sizes.tempUpdateSizeInBytes);
        ribbonGasSize_ = sizes.outputSizeInBytes;
        CUDA_CHECK(cudaMalloc(&ribbonTemp_, ribbonTempSize_));
        CUDA_CHECK(cudaMalloc(&ribbonGasBuf_, ribbonGasSize_));
        worldBuffers_.push_back(ribbonTemp_);
        worldBuffers_.push_back(ribbonGasBuf_);
        OPTIX_CHECK(optixAccelBuild(context_, stream_, &ao, &bi, 1, CUdeviceptr(ribbonTemp_), ribbonTempSize_, CUdeviceptr(ribbonGasBuf_),
                                    ribbonGasSize_, &ribbonGas_, nullptr, 0));
        CUDA_CHECK(cudaStreamSynchronize(stream_));
    }
    // the instances (the ribbons last, identity, the second hit record)
    std::vector<OptixInstance> inst;
    std::vector<KInstance> kinst;
    for (const KInstanceCPU& in : instances) {
        if (in.mesh < 0 || size_t(in.mesh) >= gas_.size() || !gas_[in.mesh]) continue;
        OptixInstance oi{};
        std::memcpy(oi.transform, in.m, sizeof(in.m));
        oi.instanceId = unsigned(inst.size());
        oi.sbtOffset = 0;
        oi.visibilityMask = 1;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = gas_[in.mesh];
        inst.push_back(oi);
        KInstance f{};
        f.r0 = make_float4(in.m[0], in.m[1], in.m[2], in.m[3]);
        f.r1 = make_float4(in.m[4], in.m[5], in.m[6], in.m[7]);
        f.r2 = make_float4(in.m[8], in.m[9], in.m[10], in.m[11]);
        f.mesh = in.mesh;
        kinst.push_back(f);
    }
    if (ribbonGas_) {
        OptixInstance oi{};
        const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        std::memcpy(oi.transform, id, sizeof(id));
        oi.instanceId = unsigned(inst.size());
        oi.sbtOffset = 1;
        oi.visibilityMask = 2;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = ribbonGas_;
        inst.push_back(oi);
        KInstance f{};
        f.r0 = make_float4(1, 0, 0, 0);
        f.r1 = make_float4(0, 1, 0, 0);
        f.r2 = make_float4(0, 0, 1, 0);
        f.mesh = ribbonMesh;
        kinst.push_back(f);
    }
    instanceCount_ = unsigned(inst.size());
    dInstances_ = upload(inst, worldBuffers_);
    params_.instances = upload(kinst, worldBuffers_);
    params_.materials = upload(materials, worldBuffers_);
    params_.texSets = upload(texSets, worldBuffers_);
    iasBuf_ = nullptr;
    iasTemp_ = nullptr;
    buildIAS(false);
}

void KoyRenderer::buildIAS(bool update) {
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = CUdeviceptr(dInstances_);
    bi.instanceArray.numInstances = instanceCount_;
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_ALLOW_UPDATE | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    ao.operation = update ? OPTIX_BUILD_OPERATION_UPDATE : OPTIX_BUILD_OPERATION_BUILD;
    if (!update) {
        OptixAccelBufferSizes sizes{};
        OPTIX_CHECK(optixAccelComputeMemoryUsage(context_, &ao, &bi, 1, &sizes));
        iasTempSize_ = std::max(sizes.tempSizeInBytes, sizes.tempUpdateSizeInBytes);
        iasSize_ = sizes.outputSizeInBytes;
        CUDA_CHECK(cudaMalloc(&iasTemp_, iasTempSize_));
        CUDA_CHECK(cudaMalloc(&iasBuf_, iasSize_));
        worldBuffers_.push_back(iasTemp_);
        worldBuffers_.push_back(iasBuf_);
    }
    OptixTraversableHandle h = 0;
    OPTIX_CHECK(optixAccelBuild(context_, stream_, &ao, &bi, 1, CUdeviceptr(iasTemp_), iasTempSize_, CUdeviceptr(iasBuf_), iasSize_, &h, nullptr, 0));
    params_.handle = h;
}

void KoyRenderer::updateRibbons(const float4* devPoints, const float3* devNormals) {
    if (!ribbonGas_) return;
    if (devPoints && devPoints != ribbonPts_)
        CUDA_CHECK(cudaMemcpyAsync(ribbonPts_, devPoints, size_t(ribbonPointCount_) * sizeof(float4), cudaMemcpyDeviceToDevice, stream_));
    if (devNormals && devNormals != ribbonNrm_)
        CUDA_CHECK(cudaMemcpyAsync(ribbonNrm_, devNormals, size_t(ribbonPointCount_) * sizeof(float3), cudaMemcpyDeviceToDevice, stream_));
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_CURVES;
    CUdeviceptr verts = CUdeviceptr(ribbonPts_), widths = CUdeviceptr(reinterpret_cast<char*>(ribbonPts_) + 12), nrms = CUdeviceptr(ribbonNrm_);
    bi.curveArray.curveType = OPTIX_PRIMITIVE_TYPE_FLAT_QUADRATIC_BSPLINE;
    bi.curveArray.numPrimitives = ribbonSegCount_;
    bi.curveArray.vertexBuffers = &verts;
    bi.curveArray.numVertices = ribbonPointCount_;
    bi.curveArray.vertexStrideInBytes = sizeof(float4);
    bi.curveArray.widthBuffers = &widths;
    bi.curveArray.widthStrideInBytes = sizeof(float4);
    bi.curveArray.normalBuffers = &nrms;
    bi.curveArray.normalStrideInBytes = sizeof(float3);
    bi.curveArray.indexBuffer = CUdeviceptr(ribbonSeg_);
    bi.curveArray.indexStrideInBytes = sizeof(unsigned int);
    bi.curveArray.flag = OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT;
    bi.curveArray.endcapFlags = OPTIX_CURVE_ENDCAP_DEFAULT;
    OptixAccelBuildOptions ao{};
    ao.buildFlags = kRibbonBuildFlags;
    ao.operation = OPTIX_BUILD_OPERATION_UPDATE;
    OPTIX_CHECK(optixAccelBuild(context_, stream_, &ao, &bi, 1, CUdeviceptr(ribbonTemp_), ribbonTempSize_, CUdeviceptr(ribbonGasBuf_), ribbonGasSize_,
                                &ribbonGas_, nullptr, 0));
    buildIAS(true);
}

void KoyRenderer::bakeAO() {
    for (size_t k = 0; k < meshesCPU_.size() && k < KOY_MAX_MESHES; ++k) {
        const KMeshCPU& m = meshesCPU_[k];
        if (!m.bakeAO || m.pos.empty() || m.nrm.empty()) continue;
        int instance = -1;
        for (size_t i = 0; i < instancesCPU_.size(); ++i)
            if (instancesCPU_[i].mesh == int(k)) { instance = int(i); break; }
        if (instance < 0) continue;
        float* ao = devAlloc<float>(m.pos.size());
        worldBuffers_.push_back(ao);
        params_.bakeMesh = int(k);
        params_.bakeInstance = instance;
        params_.bakeOut = ao;
        launch(rgBakeAO_, unsigned(m.pos.size()), 1, 1, nullptr);
        params_.meshes[k].ao = ao;
    }
    CUDA_CHECK(cudaStreamSynchronize(stream_));
}

void KoyRenderer::bakeSunVisibility(float3 boxMin, float3 boxSize, int3 dims) {
    const size_t n = size_t(dims.x) * dims.y * dims.z;
    float* out = devAlloc<float>(n);
    params_.sunVisMin = boxMin;
    params_.sunVisSize = boxSize;
    params_.sunVis = 0;
    params_.bakeOut = out;
    launch(rgBakeSun_, unsigned(dims.x), unsigned(dims.y), unsigned(dims.z), nullptr);
    CUDA_CHECK(cudaStreamSynchronize(stream_));
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<float>();
    cudaArray_t arr = nullptr;
    CUDA_CHECK(cudaMalloc3DArray(&arr, &fd, make_cudaExtent(dims.x, dims.y, dims.z)));
    cudaMemcpy3DParms cp{};
    cp.srcPtr = make_cudaPitchedPtr(out, size_t(dims.x) * sizeof(float), dims.x, dims.y);
    cp.dstArray = arr;
    cp.extent = make_cudaExtent(dims.x, dims.y, dims.z);
    cp.kind = cudaMemcpyDeviceToDevice;
    CUDA_CHECK(cudaMemcpy3D(&cp));
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeArray;
    rd.res.array.array = arr;
    cudaTextureDesc td{};
    td.addressMode[0] = td.addressMode[1] = td.addressMode[2] = cudaAddressModeClamp;
    td.filterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeElementType;
    td.normalizedCoords = 1;
    cudaTextureObject_t tex = 0;
    CUDA_CHECK(cudaCreateTextureObject(&tex, &rd, &td, nullptr));
    params_.sunVis = tex;
    cudaFree(out);
}

void KoyRenderer::launch(CUdeviceptr raygen, unsigned int w, unsigned int h, unsigned int d, float* ms) {
    if (w == 0 || h == 0 || d == 0) {
        if (ms) *ms = 0.0f;
        return;
    }
    sbt_.raygenRecord = raygen;
    CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(dParams_), &params_, sizeof(KoyParams), cudaMemcpyHostToDevice, stream_));
    CUDA_CHECK(cudaEventRecord(ev0_, stream_));
    OPTIX_CHECK(optixLaunch(pipeline_, stream_, dParams_, sizeof(KoyParams), &sbt_, w, h, d));
    CUDA_CHECK(cudaEventRecord(ev1_, stream_));
    CUDA_CHECK(cudaEventSynchronize(ev1_));
    if (ms) cudaEventElapsedTime(ms, ev0_, ev1_);
}

void KoyRenderer::renderFrame() {
    allocSamples();
    times_.world = worldMs;
    CUDA_CHECK(cudaMemsetAsync(params_.edgeCount, 0, sizeof(unsigned int), stream_));
    launch(rgShafts_, unsigned(params_.shaftBlocksX), unsigned(params_.shaftBlocksY), 1, &times_.shafts);
    launch(rgBase_, width_, height_, 1, &times_.base);
    {
        float ms = 0.0f;
        launch(rgEdgeFind_, width_, height_, 1, &ms);
        times_.base += ms;
    }
    unsigned int count = 0;
    CUDA_CHECK(cudaMemcpy(&count, params_.edgeCount, sizeof(unsigned int), cudaMemcpyDeviceToHost));
    times_.edgePixels = std::min(count, params_.edgeCapacity);
    if (count > params_.edgeCapacity) std::fprintf(stderr, "koy: kenar listesi tasti (%u > %u)\n", count, params_.edgeCapacity);
    launch(rgEdge_, times_.edgePixels, 1, 1, &times_.edge);
    launch(rgResolve_, width_, height_, 1, &times_.resolve);
    {   // metering for the eye's adaptation
        const int tiles = koyMeterTiles(int(width_), int(height_));
        koyMeter(params_.hdr, int(width_), int(height_), meterBuf_, stream_);
        meterHost_.resize(tiles);
        CUDA_CHECK(cudaMemcpyAsync(meterHost_.data(), meterBuf_, tiles * sizeof(float), cudaMemcpyDeviceToHost, stream_));
        CUDA_CHECK(cudaStreamSynchronize(stream_));
        double s = 0.0;
        for (float v : meterHost_) s += v;
        metered_ = float(s / std::max(1, tiles));
    }
    launch(rgFinal_, width_, height_, 1, &times_.final);
    ++params_.frameIndex;
}

void KoyRenderer::renderReference(int n) {
    const int base0 = params_.baseSpp, edge0 = params_.edgeSpp, filter0 = params_.filter;
    const float radius0 = params_.filterRadius, contrast0 = params_.edgeContrast;
    float4* live = params_.samples;
    if (!refSamples_) refSamples_ = devAlloc<float4>(size_t(width_) * height_ * 16);
    params_.samples = refSamples_;
    allocatedBase_ = 16;
    setSampling(16, 16, filter0, radius0, contrast0);
    const size_t np = size_t(width_) * height_;
    if (!refAcc_) refAcc_ = devAlloc<float4>(np);
    CUDA_CHECK(cudaMemsetAsync(params_.edgeCount, 0, sizeof(unsigned int), stream_));
    const float spacing = 0.25f;   // the 16-sample pattern's spacing
    launch(rgShafts_, unsigned(params_.shaftBlocksX), unsigned(params_.shaftBlocksY), 1, nullptr);
    int k = 0;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i, ++k) {
            params_.patternShift = make_float2(((i + 0.5f) / n - 0.5f) * spacing, ((j + 0.5f) / n - 0.5f) * spacing);
            launch(rgBase_, width_, height_, 1, nullptr);
            launch(rgEdgeFind_, width_, height_, 1, nullptr);
            launch(rgResolve_, width_, height_, 1, nullptr);
            koyBlend(refAcc_, params_.hdr, np, k == 0 ? 0.0f : 1.0f, 1.0f / float(n * n), stream_);
        }
    CUDA_CHECK(cudaMemcpyAsync(params_.hdr, refAcc_, np * sizeof(float4), cudaMemcpyDeviceToDevice, stream_));
    params_.patternShift = make_float2(0.0f, 0.0f);
    launch(rgFinal_, width_, height_, 1, nullptr);
    params_.samples = live;
    allocatedBase_ = base0;
    setSampling(base0, edge0, filter0, radius0, contrast0);
}

void KoyRenderer::updateMaterials(const std::vector<KMaterial>& materials) {
    if (!params_.materials || materials.empty()) return;
    CUDA_CHECK(cudaMemcpy(const_cast<KMaterial*>(params_.materials), materials.data(), materials.size() * sizeof(KMaterial), cudaMemcpyHostToDevice));
}

void KoyRenderer::probe(float3 p, float out[26]) {
    if (!probeBuf_) probeBuf_ = devAlloc<float>(26);
    float* keep = params_.bakeOut;
    params_.bakeOut = probeBuf_;
    params_.probeOrigin = p;
    launch(rgProbe_, 26, 1, 1, nullptr);
    params_.bakeOut = keep;
    CUDA_CHECK(cudaMemcpy(out, probeBuf_, 26 * sizeof(float), cudaMemcpyDeviceToHost));
}

void KoyRenderer::download(std::vector<uchar4>& out) {
    out.resize(size_t(width_) * height_);
    CUDA_CHECK(cudaMemcpy(out.data(), params_.output, out.size() * sizeof(uchar4), cudaMemcpyDeviceToHost));
}
void KoyRenderer::downloadHdr(std::vector<float4>& out) {
    out.resize(size_t(width_) * height_);
    CUDA_CHECK(cudaMemcpy(out.data(), params_.hdr, out.size() * sizeof(float4), cudaMemcpyDeviceToHost));
}

// The OptiX objects; the device memory goes with the application's device reset after every scene (app.cpp).
void KoyRenderer::shutdown() {
    cudaDeviceSynchronize();
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    if (curveModule_) optixModuleDestroy(curveModule_);
    if (context_) optixDeviceContextDestroy(context_);
    pipeline_ = nullptr;
    groups_.clear();
    module_ = nullptr;
    curveModule_ = nullptr;
    context_ = nullptr;
}
