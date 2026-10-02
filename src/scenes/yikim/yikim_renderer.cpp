#include "scenes/yikim/yikim_renderer.h"

#include <nvrtc.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

#define YCUDA(x)                                                                                        \
    do {                                                                                                \
        cudaError_t e_ = (x);                                                                           \
        if (e_ != cudaSuccess) {                                                                        \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                               \
        }                                                                                               \
    } while (0)
#define YOPTIX(x)                                                                                         \
    do {                                                                                                  \
        OptixResult r_ = (x);                                                                             \
        if (r_ != OPTIX_SUCCESS) {                                                                        \
            std::fprintf(stderr, "OptiX %s:%d %s: %s\n", __FILE__, __LINE__, #x, optixGetErrorString(r_)); \
            std::exit(1);                                                                                 \
        }                                                                                                 \
    } while (0)

struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) YRecord {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

static void ylog(unsigned int level, const char* tag, const char* msg, void*) {
    if (level <= 2) std::fprintf(stderr, "[optix %u][%s] %s\n", level, tag, msg);
}
static std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
template <class T> static T* ualloc(size_t n) {
    void* p = nullptr;
    YCUDA(cudaMalloc(&p, std::max<size_t>(n, 1) * sizeof(T)));
    YCUDA(cudaMemset(p, 0, std::max<size_t>(n, 1) * sizeof(T)));
    return static_cast<T*>(p);
}

bool YikimRenderer::init(unsigned int width, unsigned int height, const std::string& root) {
    root_ = root;
    w_ = width;
    h_ = height;
    YCUDA(cudaFree(nullptr));
    YCUDA(cudaStreamCreate(&stream_));
    YCUDA(cudaEventCreate(&e0_));
    YCUDA(cudaEventCreate(&e1_));
    YCUDA(cudaEventCreateWithFlags(&evXf_, cudaEventDisableTiming));
    YCUDA(cudaEventCreate(&eMid_));
    YOPTIX(optixInit());
    OptixDeviceContextOptions o{};
    o.logCallbackFunction = ylog;
    o.logCallbackLevel = 4;
    YOPTIX(optixDeviceContextCreate(nullptr, &o, &ctx_));
    p_.width = width;
    p_.height = height;
    p_.accum = ualloc<float4>(size_t(width) * height);
    p_.output = ualloc<uchar4>(size_t(width) * height);
    YCUDA(cudaMalloc(reinterpret_cast<void**>(&dParams_), sizeof(YikimParams)));
    dPickBody_ = ualloc<int>(1);
    dPickHit_ = ualloc<float4>(1);
    p_.pickBody = dPickBody_;
    p_.pickHit = dPickHit_;
    const size_t px = size_t(width) * height;
    p_.albedoAcc = ualloc<float4>(px);
    p_.normalAcc = ualloc<float4>(px);
    p_.motion = ualloc<float4>(px);
    p_.gbuf = ualloc<float4>(px);
    if (!filter_.create(width, height)) return false;
    p_.dnIn = ualloc<float4>(px);
    p_.dnAlbedo = ualloc<float4>(px);
    p_.dnNormal = ualloc<float4>(px);
    return createDenoiser();
}

static OptixImage2D yimage(void* data, unsigned int w, unsigned int h, OptixPixelFormat f = OPTIX_PIXEL_FORMAT_FLOAT4, unsigned int px = sizeof(float4)) {
    OptixImage2D im{};
    im.data = CUdeviceptr(data);
    im.width = w;
    im.height = h;
    im.rowStrideInBytes = w * px;
    im.pixelStrideInBytes = px;
    im.format = f;
    return im;
}

bool YikimRenderer::createDenoiser() {
    OptixDenoiserOptions o{};
    o.guideAlbedo = 1;
    o.guideNormal = 1;
    YOPTIX(optixDenoiserCreate(ctx_, OPTIX_DENOISER_MODEL_KIND_TEMPORAL_AOV, &o, &dn_));
    OptixDenoiserSizes sz{};
    YOPTIX(optixDenoiserComputeMemoryResources(dn_, w_, h_, &sz));
    dnStateSize_ = sz.stateSizeInBytes;
    dnScratchSize_ = sz.withoutOverlapScratchSizeInBytes;
    dnGuidePixel_ = sz.internalGuideLayerPixelSizeInBytes;
    YCUDA(cudaMalloc(&dnState_, dnStateSize_));
    YCUDA(cudaMalloc(&dnScratch_, dnScratchSize_));
    YCUDA(cudaMalloc(&dnGuideA_, dnGuidePixel_ * w_ * h_));
    YCUDA(cudaMalloc(&dnGuideB_, dnGuidePixel_ * w_ * h_));
    YCUDA(cudaMemset(dnGuideA_, 0, dnGuidePixel_ * w_ * h_));
    YCUDA(cudaMemset(dnGuideB_, 0, dnGuidePixel_ * w_ * h_));
    dnOut_ = ualloc<float4>(size_t(w_) * h_);
    dnPrevOut_ = ualloc<float4>(size_t(w_) * h_);
    dnIntensity_ = ualloc<float>(1);
    dnAvg_ = ualloc<float>(3);
    p_.denoised = dnOut_;
    YOPTIX(optixDenoiserSetup(dn_, stream_, w_, h_, CUdeviceptr(dnState_), dnStateSize_, CUdeviceptr(dnScratch_), dnScratchSize_));
    YCUDA(cudaStreamSynchronize(stream_));
    return true;
}

// the accumulation's means -> denoiser (with last frame's output and the motion guide) -> the display
void YikimRenderer::runDenoiser() {
    OptixShaderBindingTable sbt = sbt_;
    sbt.raygenRecord = rgPrep_;
    YCUDA(cudaMemcpyAsync(reinterpret_cast<void*>(dParams_), &p_, sizeof(YikimParams), cudaMemcpyHostToDevice, stream_));
    YOPTIX(optixLaunch(pipeline_, stream_, dParams_, sizeof(YikimParams), &sbt, w_, h_, 1));
    OptixDenoiserLayer layer{};
    layer.input = yimage(p_.dnIn, w_, h_);
    layer.output = yimage(dnOut_, w_, h_);
    layer.previousOutput = yimage(dnPrevOut_, w_, h_);
    layer.type = OPTIX_DENOISER_AOV_TYPE_BEAUTY;
    OptixDenoiserGuideLayer g{};
    g.albedo = yimage(p_.dnAlbedo, w_, h_);
    g.normal = yimage(p_.dnNormal, w_, h_);
    g.flow = yimage(p_.motion, w_, h_, OPTIX_PIXEL_FORMAT_FLOAT2, sizeof(float4));
    auto internal = [&](void* d) {
        OptixImage2D im{};
        im.data = CUdeviceptr(d);
        im.width = w_;
        im.height = h_;
        im.pixelStrideInBytes = unsigned(dnGuidePixel_);
        im.rowStrideInBytes = unsigned(dnGuidePixel_ * w_);
        im.format = OPTIX_PIXEL_FORMAT_INTERNAL_GUIDE_LAYER;
        return im;
    };
    g.previousOutputInternalGuideLayer = internal(dnGuideA_);
    g.outputInternalGuideLayer = internal(dnGuideB_);
    if (!dnHistory_) YCUDA(cudaMemcpyAsync(dnPrevOut_, p_.dnIn, size_t(w_) * h_ * sizeof(float4), cudaMemcpyDeviceToDevice, stream_));
    YOPTIX(optixDenoiserComputeIntensity(dn_, stream_, &layer.input, CUdeviceptr(dnIntensity_), CUdeviceptr(dnScratch_), dnScratchSize_));
    YOPTIX(optixDenoiserComputeAverageColor(dn_, stream_, &layer.input, CUdeviceptr(dnAvg_), CUdeviceptr(dnScratch_), dnScratchSize_));
    OptixDenoiserParams dp{};
    dp.hdrIntensity = CUdeviceptr(dnIntensity_);
    dp.hdrAverageColor = CUdeviceptr(dnAvg_);
    dp.blendFactor = 0.0f;
    dp.temporalModeUsePreviousLayers = dnHistory_ ? 1u : 0u;
    YOPTIX(optixDenoiserInvoke(dn_, stream_, &dp, CUdeviceptr(dnState_), dnStateSize_, &g, &layer, 1, 0, 0, CUdeviceptr(dnScratch_), dnScratchSize_));
    // this output and its internal guide are the next frame's history
    YCUDA(cudaMemcpyAsync(dnPrevOut_, dnOut_, size_t(w_) * h_ * sizeof(float4), cudaMemcpyDeviceToDevice, stream_));
    std::swap(dnGuideA_, dnGuideB_);
    dnHistory_ = true;
    sbt.raygenRecord = rgTone_;
    YOPTIX(optixLaunch(pipeline_, stream_, dParams_, sizeof(YikimParams), &sbt, w_, h_, 1));
}

bool YikimRenderer::compileDevice(std::string& log) {
    const std::string path = root_ + "/src/scenes/yikim/device/yikim.cu";
    const std::string src = readFile(path);
    if (src.empty()) { log = "cannot read " + path; return false; }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "yikim.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) { log = "nvrtcCreateProgram failed"; return false; }
    const std::string inc[] = {"-I" + root_ + "/src", "-I" + root_ + "/src/scenes/yikim/device", "-I" + root_ + "/third_party/optix/include",
                               "-I" + root_ + "/third_party/cuda/include"};
    std::vector<const char*> opts = {"--std=c++17", "-arch=compute_89", "--use_fast_math", "--relocatable-device-code=true", "--optix-ir", "-lineinfo",
                                     "-D__x86_64"};
    for (const auto& s : inc) opts.push_back(s.c_str());
    const nvrtcResult cr = nvrtcCompileProgram(prog, int(opts.size()), opts.data());
    size_t ls = 0;
    nvrtcGetProgramLogSize(prog, &ls);
    std::string nvlog(ls, '\0');
    if (ls > 1) nvrtcGetProgramLog(prog, nvlog.data());
    if (cr != NVRTC_SUCCESS) { log = "NVRTC: " + nvlog; nvrtcDestroyProgram(&prog); return false; }
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
    pco.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    pco.numPayloadValues = 2;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "params";
    pco.pipelineLaunchParamsSizeInBytes = sizeof(YikimParams);
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
    char olog[8192];
    size_t olen = sizeof(olog);
    OptixModule module = nullptr;
    OptixResult r = optixModuleCreate(ctx_, &mco, &pco, ir.data(), ir.size(), olog, &olen, &module);
    if (r != OPTIX_SUCCESS) { log = std::string("optixModuleCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    OptixProgramGroupDesc d[6];
    std::memset(d, 0, sizeof(d));
    d[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[0].raygen.module = module;
    d[0].raygen.entryFunctionName = "__raygen__render";
    d[1].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[2].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    d[3].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[3].raygen.module = module;
    d[3].raygen.entryFunctionName = "__raygen__pick";
    d[4].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[4].raygen.module = module;
    d[4].raygen.entryFunctionName = "__raygen__prep";
    d[5].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[5].raygen.module = module;
    d[5].raygen.entryFunctionName = "__raygen__tonemap";
    std::vector<OptixProgramGroup> groups(6);
    OptixProgramGroupOptions go{};
    olen = sizeof(olog);
    r = optixProgramGroupCreate(ctx_, d, 6, &go, olog, &olen, groups.data());
    if (r != OPTIX_SUCCESS) { log = std::string("optixProgramGroupCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    OptixPipelineLinkOptions lo{};
    lo.maxTraceDepth = 1;
    OptixPipeline pipeline = nullptr;
    olen = sizeof(olog);
    r = optixPipelineCreate(ctx_, &pco, &lo, groups.data(), 6, olog, &olen, &pipeline);
    if (r != OPTIX_SUCCESS) { log = std::string("optixPipelineCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    YOPTIX(optixPipelineSetStackSizeFromCallDepths(pipeline, 1, 0, 0, 0, 2));
    YCUDA(cudaDeviceSynchronize());
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    pipeline_ = pipeline;
    groups_ = groups;
    module_ = module;
    auto record = [&](OptixProgramGroup g, CUdeviceptr& dst) {
        YRecord rec{};
        YOPTIX(optixSbtRecordPackHeader(g, &rec));
        if (!dst) YCUDA(cudaMalloc(reinterpret_cast<void**>(&dst), sizeof(YRecord)));
        YCUDA(cudaMemcpy(reinterpret_cast<void*>(dst), &rec, sizeof(YRecord), cudaMemcpyHostToDevice));
    };
    record(groups_[0], rg_);
    record(groups_[1], miss_);
    record(groups_[2], hit_);
    record(groups_[3], rgPick_);
    record(groups_[4], rgPrep_);
    record(groups_[5], rgTone_);
    sbt_ = {};
    sbt_.raygenRecord = rg_;
    sbt_.missRecordBase = miss_;
    sbt_.missRecordStrideInBytes = sizeof(YRecord);
    sbt_.missRecordCount = 1;
    sbt_.hitgroupRecordBase = hit_;
    sbt_.hitgroupRecordStrideInBytes = sizeof(YRecord);
    sbt_.hitgroupRecordCount = 1;
    log = nvlog;
    return true;
}

void YikimRenderer::freeWorld() {
    YCUDA(cudaDeviceSynchronize());
    for (void* p : worldBuffers_) cudaFree(p);
    worldBuffers_.clear();
    dInstances_ = nullptr;
    dBodies_ = nullptr;
    if (iasTemp_) cudaFree(iasTemp_);
    if (iasOut_) cudaFree(iasOut_);
    if (barTemp_) cudaFree(barTemp_);
    if (barOut_) cudaFree(barOut_);
    iasTemp_ = iasOut_ = nullptr;
    iasTempSize_ = iasOutSize_ = 0;
    instanceCount_ = bodyInstances_ = barPrisms_ = 0;
    barVerts_ = nullptr;
    barIdx_ = nullptr;
    barTemp_ = barOut_ = nullptr;
    barTempSize_ = barOutSize_ = 0;
    barGas_ = 0;
}

// the bars' triangle structure, from the vertices the physics wrote (NaN prisms are skipped)
bool YikimRenderer::buildBars() {
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    CUdeviceptr verts = CUdeviceptr(barVerts_);
    bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
    bi.triangleArray.vertexStrideInBytes = sizeof(float3);
    bi.triangleArray.numVertices = unsigned(barPrisms_ * 6);
    bi.triangleArray.vertexBuffers = &verts;
    bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    bi.triangleArray.indexStrideInBytes = sizeof(uint3);
    bi.triangleArray.numIndexTriplets = unsigned(barPrisms_ * 6);
    bi.triangleArray.indexBuffer = CUdeviceptr(barIdx_);
    const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
    bi.triangleArray.flags = flags;
    bi.triangleArray.numSbtRecords = 1;
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    if (!barOut_) {
        OptixAccelBufferSizes sz{};
        YOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
        YCUDA(cudaMalloc(&barTemp_, sz.tempSizeInBytes));
        YCUDA(cudaMalloc(&barOut_, sz.outputSizeInBytes));
        barTempSize_ = sz.tempSizeInBytes;
        barOutSize_ = sz.outputSizeInBytes;
    }
    OptixTraversableHandle h = 0;
    YOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(barTemp_), barTempSize_, CUdeviceptr(barOut_), barOutSize_, &h, nullptr, 0));
    const bool changed = h != barGas_;
    barGas_ = h;
    return changed;
}

void YikimRenderer::uploadWorld(const PhysWorld& w) {
    freeWorld();
    const int n = int(w.info.size());
    std::vector<YMesh> meshes;
    std::vector<OptixInstance> inst;
    bodies_.clear();
    for (int i = 1; i < n; ++i) {
        // the hull as triangles (fans over its faces), in the body frame
        std::vector<float3> pos;
        std::vector<uint3> idx;
        for (const PolyFace& f : w.shapes[i].faces) {
            const unsigned base = unsigned(pos.size());
            for (const double3& v : f.v) pos.push_back(f3(v));
            for (size_t k = 1; k + 1 < f.v.size(); ++k) idx.push_back(make_uint3(base, base + unsigned(k), base + unsigned(k) + 1));
        }
        YMesh m;
        float3* dp = ualloc<float3>(pos.size());
        uint3* di = ualloc<uint3>(idx.size());
        YCUDA(cudaMemcpy(dp, pos.data(), pos.size() * sizeof(float3), cudaMemcpyHostToDevice));
        YCUDA(cudaMemcpy(di, idx.data(), idx.size() * sizeof(uint3), cudaMemcpyHostToDevice));
        worldBuffers_.push_back(dp);
        worldBuffers_.push_back(di);
        m.pos = dp;
        m.idx = di;
        meshes.push_back(m);
        OptixBuildInput bi{};
        bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        CUdeviceptr verts = CUdeviceptr(dp);
        bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        bi.triangleArray.vertexStrideInBytes = sizeof(float3);
        bi.triangleArray.numVertices = unsigned(pos.size());
        bi.triangleArray.vertexBuffers = &verts;
        bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        bi.triangleArray.indexStrideInBytes = sizeof(uint3);
        bi.triangleArray.numIndexTriplets = unsigned(idx.size());
        bi.triangleArray.indexBuffer = CUdeviceptr(di);
        const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
        bi.triangleArray.flags = flags;
        bi.triangleArray.numSbtRecords = 1;
        OptixAccelBuildOptions ao{};
        ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
        ao.operation = OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes sz{};
        YOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
        void *temp = nullptr, *out = nullptr;
        YCUDA(cudaMalloc(&temp, sz.tempSizeInBytes));
        YCUDA(cudaMalloc(&out, sz.outputSizeInBytes));
        OptixTraversableHandle gas = 0;
        YOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(temp), sz.tempSizeInBytes, CUdeviceptr(out), sz.outputSizeInBytes, &gas, nullptr, 0));
        YCUDA(cudaStreamSynchronize(stream_));
        cudaFree(temp);
        worldBuffers_.push_back(out);
        OptixInstance oi{};
        oi.transform[0] = oi.transform[5] = oi.transform[10] = 1.0f;
        oi.instanceId = unsigned(i - 1);
        oi.sbtOffset = 0;
        oi.visibilityMask = 1;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = gas;
        inst.push_back(oi);
        const unsigned c = w.info[i].color;
        YBody b;
        // sRGB-ish hex to linear albedo
        auto lin = [](unsigned v) { const float x = v / 255.0f; return x <= 0.04045f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f); };
        b.albedo = make_float3(lin((c >> 16) & 255), lin((c >> 8) & 255), lin(c & 255));
        b.damage = 0.0f;
        b.sphere = w.info[i].sphere;
        b.glass = w.info[i].glass ? 1.0f : 0.0f;
        bodies_.push_back(b);
    }
    bodyInstances_ = int(inst.size());
    // the bars: one more instance in the world frame, its own mesh and colour (rusty steel)
    barPrisms_ = w.barPrisms();
    if (barPrisms_ > 0) {
        barVerts_ = ualloc<float3>(size_t(barPrisms_) * 6);
        barIdx_ = ualloc<uint3>(size_t(barPrisms_) * 6);
        worldBuffers_.push_back(barVerts_);
        worldBuffers_.push_back(barIdx_);
        std::vector<uint3> idx;
        idx.reserve(size_t(barPrisms_) * 6);
        for (unsigned p = 0; p < unsigned(barPrisms_); ++p) {   // the three sides of a prism: ends 0 1 2 and 3 4 5
            const unsigned b = p * 6;
            for (unsigned k = 0; k < 3; ++k) {
                const unsigned k1 = (k + 1) % 3;
                idx.push_back(make_uint3(b + k, b + k1, b + k1 + 3));
                idx.push_back(make_uint3(b + k, b + k1 + 3, b + k + 3));
            }
        }
        YCUDA(cudaMemcpy(barIdx_, idx.data(), idx.size() * sizeof(uint3), cudaMemcpyHostToDevice));
        w.writeBars(barVerts_, barRadius, barGap, barStub, stream_);
        buildBars();
        YCUDA(cudaStreamSynchronize(stream_));
        YMesh m;
        m.pos = barVerts_;
        m.idx = barIdx_;
        meshes.push_back(m);
        OptixInstance oi{};
        oi.transform[0] = oi.transform[5] = oi.transform[10] = 1.0f;
        oi.instanceId = unsigned(bodyInstances_);
        oi.visibilityMask = 1;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = barGas_;
        inst.push_back(oi);
        YBody b;
        b.albedo = make_float3(0.11f, 0.075f, 0.055f);
        b.damage = 0.0f;
        b.sphere = 0.0f;
        b.glass = 0.0f;
        bodies_.push_back(b);
    }
    instanceCount_ = int(inst.size());
    xfPrev_ = ualloc<float>(size_t(std::max(bodyInstances_, 1)) * 12);
    worldBuffers_.push_back(xfPrev_);
    havePrev_ = false;
    dnHistory_ = false;
    filter_.reset();
    YMesh* dm = ualloc<YMesh>(meshes.size());
    YCUDA(cudaMemcpy(dm, meshes.data(), meshes.size() * sizeof(YMesh), cudaMemcpyHostToDevice));
    worldBuffers_.push_back(dm);
    dInstances_ = ualloc<OptixInstance>(inst.size());
    YCUDA(cudaMemcpy(dInstances_, inst.data(), inst.size() * sizeof(OptixInstance), cudaMemcpyHostToDevice));
    worldBuffers_.push_back(dInstances_);
    dBodies_ = ualloc<YBody>(bodies_.size());
    YCUDA(cudaMemcpy(dBodies_, bodies_.data(), bodies_.size() * sizeof(YBody), cudaMemcpyHostToDevice));
    worldBuffers_.push_back(dBodies_);
    p_.meshes = dm;
    p_.bodies = dBodies_;
    p_.samples = 0;
    p_.xfCur = reinterpret_cast<const float*>(dInstances_);
    p_.xfStride = unsigned(sizeof(OptixInstance) / sizeof(float));
    p_.xfPrev = xfPrev_;
    p_.barInstance = unsigned(bodyInstances_);
}

void YikimRenderer::buildIAS() {
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = CUdeviceptr(dInstances_);
    bi.instanceArray.numInstances = unsigned(instanceCount_);
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    YOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
    if (sz.tempSizeInBytes > iasTempSize_) {
        if (iasTemp_) cudaFree(iasTemp_);
        YCUDA(cudaMalloc(&iasTemp_, sz.tempSizeInBytes));
        iasTempSize_ = sz.tempSizeInBytes;
    }
    if (sz.outputSizeInBytes > iasOutSize_) {
        if (iasOut_) cudaFree(iasOut_);
        YCUDA(cudaMalloc(&iasOut_, sz.outputSizeInBytes));
        iasOutSize_ = sz.outputSizeInBytes;
    }
    OptixTraversableHandle h = 0;
    YOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(iasTemp_), iasTempSize_, CUdeviceptr(iasOut_), iasOutSize_, &h, nullptr, 0));
    p_.handle = h;
}

void YikimRenderer::updateWorld(PhysWorld& w) {
    if (!instanceCount_) return;
    // the transforms the last frame was drawn with become "a frame ago" (the denoiser's motion guide)
    auto keep = [&] {
        YCUDA(cudaMemcpy2DAsync(xfPrev_, 12 * sizeof(float), dInstances_, sizeof(OptixInstance), 12 * sizeof(float), size_t(bodyInstances_),
                                cudaMemcpyDeviceToDevice, stream_));
    };
    if (havePrev_) keep();
    w.writeTransforms(dInstances_, sizeof(OptixInstance), 1, bodyInstances_, stream_);
    if (!havePrev_) keep();
    havePrev_ = true;
    if (barPrisms_) w.writeBars(barVerts_, showBars ? barRadius : 0.0f, showBars ? barGap : 1e30f, barStub, stream_);
    YCUDA(cudaEventRecord(evXf_, stream_));   // the state has been read: a queued physics step may overwrite it now
    if (barPrisms_ && buildBars())
        YCUDA(cudaMemcpyAsync(&dInstances_[bodyInstances_].traversableHandle, &barGas_, sizeof(barGas_), cudaMemcpyHostToDevice, stream_));
    buildIAS();
}

void YikimRenderer::setDamage(const std::vector<float>& d) {
    if (bodies_.empty()) return;
    for (size_t i = 0; i < bodies_.size(); ++i) bodies_[i].damage = i + 1 < d.size() ? d[i + 1] : 0.0f;
    YCUDA(cudaMemcpyAsync(dBodies_, bodies_.data(), bodies_.size() * sizeof(YBody), cudaMemcpyHostToDevice, stream_));
}

void YikimRenderer::render(unsigned int spp, bool accumulate) {
    if (!accumulate) p_.samples = 0;
    p_.spp = spp;
    if (!havePrevCam_) {   // the first frame: nothing moved yet
        prevCam_ = p_.cam;
        prevShift_ = p_.groundShift;
        prevGroundY_ = p_.groundY;
        havePrevCam_ = true;
    }
    p_.prevCam = prevCam_;
    p_.groundShiftPrev = prevShift_;
    p_.groundYPrev = prevGroundY_;
    YCUDA(cudaMemcpyAsync(reinterpret_cast<void*>(dParams_), &p_, sizeof(YikimParams), cudaMemcpyHostToDevice, stream_));
    YCUDA(cudaEventRecord(e0_, stream_));
    YOPTIX(optixLaunch(pipeline_, stream_, dParams_, sizeof(YikimParams), &sbt_, w_, h_, 1));
    YCUDA(cudaEventRecord(eMid_, stream_));
    p_.samples += spp;
    if (denoiseMode == 1) {
        YFilterFrame f{};
        f.width = w_;
        f.height = h_;
        f.accum = p_.accum;
        f.albedoAcc = p_.albedoAcc;
        f.normalAcc = p_.normalAcc;
        f.samples = p_.samples;
        f.frameSpp = spp;
        f.motion = p_.motion;
        f.gbuf = p_.gbuf;
        f.exposure = p_.exposure;
        f.seed = p_.seed;
        f.output = p_.output;
        filter_.run(f, stream_);
    } else if (denoiseMode == 2) {
        runDenoiser();
    }
    YCUDA(cudaEventRecord(e1_, stream_));
    YCUDA(cudaEventSynchronize(e1_));
    YCUDA(cudaEventElapsedTime(&lastMs, e0_, e1_));
    float tr = 0.0f;
    YCUDA(cudaEventElapsedTime(&tr, e0_, eMid_));
    denoiseMs = denoiseMode ? lastMs - tr : 0.0f;
    if (denoiseMode != 2) dnHistory_ = false;   // the AI denoiser's history is stale once it has been off
    prevCam_ = p_.cam;
    prevShift_ = p_.groundShift;
    prevGroundY_ = p_.groundY;
    ++p_.seed;
}

int YikimRenderer::pick(float x, float y, float4& hit) {
    p_.pickPixel = make_float2(x, y);
    YCUDA(cudaMemcpyAsync(reinterpret_cast<void*>(dParams_), &p_, sizeof(YikimParams), cudaMemcpyHostToDevice, stream_));
    OptixShaderBindingTable sbt = sbt_;
    sbt.raygenRecord = rgPick_;
    YOPTIX(optixLaunch(pipeline_, stream_, dParams_, sizeof(YikimParams), &sbt, 1, 1, 1));
    int body = -1;
    YCUDA(cudaMemcpyAsync(&body, dPickBody_, sizeof(int), cudaMemcpyDeviceToHost, stream_));
    YCUDA(cudaMemcpyAsync(&hit, dPickHit_, sizeof(float4), cudaMemcpyDeviceToHost, stream_));
    YCUDA(cudaStreamSynchronize(stream_));
    return body;
}

void YikimRenderer::downloadTo(uchar4* dst) {
    YCUDA(cudaMemcpyAsync(dst, p_.output, size_t(w_) * h_ * sizeof(uchar4), cudaMemcpyDeviceToHost, stream_));
    YCUDA(cudaStreamSynchronize(stream_));
}

void YikimRenderer::download(std::vector<uchar4>& out) {
    out.resize(size_t(w_) * h_);
    YCUDA(cudaMemcpy(out.data(), p_.output, out.size() * sizeof(uchar4), cudaMemcpyDeviceToHost));
}

// everything init, the denoiser and the programs allocated: the application opens and closes scenes many times
void YikimRenderer::shutdown() {
    if (!ctx_) return;
    YCUDA(cudaDeviceSynchronize());
    freeWorld();
    if (dn_) optixDenoiserDestroy(dn_);
    dn_ = nullptr;
    filter_.destroy();
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    if (ctx_) optixDeviceContextDestroy(ctx_);
    pipeline_ = nullptr;
    module_ = nullptr;
    ctx_ = nullptr;
    groups_.clear();
    void* bufs[] = {p_.accum, p_.output, p_.albedoAcc, p_.normalAcc, p_.motion, p_.gbuf, p_.dnIn, p_.dnAlbedo, p_.dnNormal, dPickBody_, dPickHit_,
                    dnState_, dnScratch_, dnGuideA_, dnGuideB_, dnOut_, dnPrevOut_, dnIntensity_, dnAvg_,
                    reinterpret_cast<void*>(dParams_), reinterpret_cast<void*>(rg_), reinterpret_cast<void*>(rgPick_), reinterpret_cast<void*>(rgPrep_),
                    reinterpret_cast<void*>(rgTone_), reinterpret_cast<void*>(miss_), reinterpret_cast<void*>(hit_)};
    for (void* b : bufs)
        if (b) cudaFree(b);
    p_ = YikimParams{};
    dPickBody_ = nullptr;
    dPickHit_ = nullptr;
    dnState_ = dnScratch_ = dnGuideA_ = dnGuideB_ = nullptr;
    dnOut_ = dnPrevOut_ = nullptr;
    dnIntensity_ = dnAvg_ = nullptr;
    dParams_ = rg_ = rgPick_ = rgPrep_ = rgTone_ = miss_ = hit_ = 0;
    for (cudaEvent_t* e : {&e0_, &e1_, &evXf_, &eMid_})
        if (*e) { cudaEventDestroy(*e); *e = nullptr; }
    if (stream_) cudaStreamDestroy(stream_);
    stream_ = nullptr;
}
