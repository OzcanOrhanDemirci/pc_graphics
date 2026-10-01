#include "fener/fener_renderer.h"
#include "denoiser.h"   // launchBloom (kernels.cu)

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

struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) FRecord {
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
    CUDA_CHECK(cudaMemset(p, 0, count * sizeof(T)));
    return static_cast<T*>(p);
}

bool FenerRenderer::init(unsigned int width, unsigned int height, const std::string& root, unsigned int volScale) {
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
    params_.volScale = volScale == 2 ? 2u : 1u;
    params_.volWidth = width / params_.volScale;
    params_.volHeight = height / params_.volScale;
    const size_t nv = size_t(params_.volWidth) * params_.volHeight;
    params_.surface = devAlloc<float4>(n);
    params_.gdepth = devAlloc<float4>(n);
    params_.motion = devAlloc<float2>(n);
    params_.volume = devAlloc<float4>(nv);
    params_.volumeHist[0] = devAlloc<float4>(nv);
    params_.volumeHist[1] = devAlloc<float4>(nv);
    params_.taaHist[0] = devAlloc<float4>(n);
    params_.taaHist[1] = devAlloc<float4>(n);
    params_.hdr = devAlloc<float4>(n);
    params_.emissive = devAlloc<float4>(n);
    params_.output = devAlloc<uchar4>(n);
    unsigned int w = width, h = height;
    for (int k = 0; k < kWideLevels; ++k) {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        wideLevels_[k] = devAlloc<float4>(size_t(w) * h);
        wideAccum_[k] = devAlloc<float4>(size_t(w) * h);
        if (k < kNarrowLevels) {
            narrowLevels_[k] = devAlloc<float4>(size_t(w) * h);
            narrowAccum_[k] = devAlloc<float4>(size_t(w) * h);
        }
    }
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dParams_), sizeof(FenerParams)));
    return true;
}

bool FenerRenderer::compileDevice(std::string& log) {
    const std::string srcPath = root_ + "/src/device/fener.cu";
    const std::string src = readText(srcPath);
    if (src.empty()) { log = "cannot read " + srcPath; return false; }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "fener.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) { log = "nvrtcCreateProgram failed"; return false; }
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
    pco.numPayloadValues = 2;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "params";
    pco.pipelineLaunchParamsSizeInBytes = sizeof(FenerParams);
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;

    char olog[8192];
    size_t olen = sizeof(olog);
    OptixModule module = nullptr;
    OptixResult r = optixModuleCreate(context_, &mco, &pco, ir.data(), ir.size(), olog, &olen, &module);
    if (r != OPTIX_SUCCESS) { log = std::string("optixModuleCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }

    const char* rgNames[6] = {"__raygen__surface", "__raygen__volume", "__raygen__volresolve", "__raygen__compose", "__raygen__taa", "__raygen__final"};
    const int RG = 6;
    std::vector<OptixProgramGroupDesc> d(RG + 2);
    for (auto& x : d) std::memset(&x, 0, sizeof(x));
    for (int i = 0; i < RG; ++i) {
        d[i].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        d[i].raygen.module = module;
        d[i].raygen.entryFunctionName = rgNames[i];
    }
    d[RG + 0].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;        // traversal only: no programs
    d[RG + 1].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;    // built-in triangles, no programs
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
    pipeline_ = pipeline;
    groups_ = groups;
    module_ = module;

    auto record = [&](OptixProgramGroup g, CUdeviceptr& dst) {
        FRecord rec{};
        OPTIX_CHECK(optixSbtRecordPackHeader(g, &rec));
        if (!dst) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dst), sizeof(FRecord)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(dst), &rec, sizeof(FRecord), cudaMemcpyHostToDevice));
    };
    record(groups_[0], rgSurface_);
    record(groups_[1], rgVolume_);
    record(groups_[2], rgResolve_);
    record(groups_[3], rgCompose_);
    record(groups_[4], rgTaa_);
    record(groups_[5], rgFinal_);
    record(groups_[RG + 0], missRecord_);
    record(groups_[RG + 1], hitRecord_);
    sbt_ = {};
    sbt_.missRecordBase = missRecord_;
    sbt_.missRecordStrideInBytes = sizeof(FRecord);
    sbt_.missRecordCount = 1;
    sbt_.hitgroupRecordBase = hitRecord_;
    sbt_.hitgroupRecordStrideInBytes = sizeof(FRecord);
    sbt_.hitgroupRecordCount = 1;
    log = nvlog;
    params_.frame = 0;
    return true;
}

template <class T> static T* upload(const std::vector<T>& v, std::vector<void*>& owned) {
    if (v.empty()) return nullptr;
    T* p = nullptr;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&p), v.size() * sizeof(T)));
    CUDA_CHECK(cudaMemcpy(p, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    owned.push_back(p);
    return p;
}

static void* buildAS(OptixDeviceContext ctx, const OptixBuildInput& bi, OptixTraversableHandle& handle) {
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
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

void FenerRenderer::uploadScene(const FenerScene& s) {
    CUDA_CHECK(cudaDeviceSynchronize());
    for (void* p : sceneBuffers_) cudaFree(p);
    sceneBuffers_.clear();
    if (s.meshes.size() > FENER_MAX_MESHES) std::fprintf(stderr, "too many meshes: %zu\n", s.meshes.size());
    std::vector<OptixTraversableHandle> gas(s.meshes.size(), 0);
    for (size_t k = 0; k < s.meshes.size() && k < FENER_MAX_MESHES; ++k) {
        const FMeshCPU& m = s.meshes[k];
        FMesh& dm = params_.meshes[k];
        dm.pos = upload(m.pos, sceneBuffers_);
        dm.nrm = upload(m.nrm, sceneBuffers_);
        dm.idx = upload(m.idx, sceneBuffers_);
        dm.mat = upload(m.mat, sceneBuffers_);
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
        sceneBuffers_.push_back(buildAS(context_, bi, gas[k]));
    }
    std::vector<OptixInstance> inst;
    std::vector<FInstance> finst;
    inst.reserve(s.instances.size());
    for (const FInstanceCPU& in : s.instances) {
        if (in.mesh < 0 || size_t(in.mesh) >= gas.size() || !gas[in.mesh]) continue;
        OptixInstance oi{};
        std::memcpy(oi.transform, in.m, sizeof(in.m));
        oi.instanceId = unsigned(inst.size());
        oi.sbtOffset = 0;
        oi.visibilityMask = in.mask;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = gas[in.mesh];
        inst.push_back(oi);
        FInstance f{};
        f.r0 = make_float4(in.m[0], in.m[1], in.m[2], in.m[3]);
        f.r1 = make_float4(in.m[4], in.m[5], in.m[6], in.m[7]);
        f.r2 = make_float4(in.m[8], in.m[9], in.m[10], in.m[11]);
        f.mesh = in.mesh;
        finst.push_back(f);
    }
    OptixInstance* dInst = upload(inst, sceneBuffers_);
    params_.instances = upload(finst, sceneBuffers_);
    params_.materials = upload(s.materials, sceneBuffers_);
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = CUdeviceptr(dInst);
    bi.instanceArray.numInstances = unsigned(inst.size());
    OptixTraversableHandle h = 0;
    sceneBuffers_.push_back(buildAS(context_, bi, h));
    params_.handle = h;
    params_.lampPos = s.lampPos;
    params_.winCorner = s.winCorner;
    params_.winU = s.winU;
    params_.winV = s.winV;
    params_.winN = s.winN;
    params_.frame = 0;
}

void FenerRenderer::launch(CUdeviceptr raygen, unsigned int w, unsigned int h, float* ms) {
    sbt_.raygenRecord = raygen;
    CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(dParams_), &params_, sizeof(FenerParams), cudaMemcpyHostToDevice, stream_));
    CUDA_CHECK(cudaEventRecord(ev0_, stream_));
    OPTIX_CHECK(optixLaunch(pipeline_, stream_, dParams_, sizeof(FenerParams), &sbt_, w, h, 1));
    CUDA_CHECK(cudaEventRecord(ev1_, stream_));
    CUDA_CHECK(cudaEventSynchronize(ev1_));
    if (ms) cudaEventElapsedTime(ms, ev0_, ev1_);
}

void FenerRenderer::renderFrame() {
    if (params_.frame == 0) params_.prevCam = params_.cam;
    launch(rgSurface_, width_, height_, &times_.surface);
    launch(rgVolume_, params_.volWidth, params_.volHeight, &times_.volume);
    launch(rgResolve_, params_.volWidth, params_.volHeight, &times_.resolve);
    launch(rgCompose_, width_, height_, &times_.compose);
    launch(rgTaa_, width_, height_, &times_.taa);
    CUDA_CHECK(cudaEventRecord(ev0_, stream_));
    launchBloom(params_.hdr, int(width_), int(height_), wideLevels_, wideAccum_, kWideLevels, stream_);
    launchBloom(params_.emissive, int(width_), int(height_), narrowLevels_, narrowAccum_, kNarrowLevels, stream_);
    CUDA_CHECK(cudaEventRecord(ev1_, stream_));
    CUDA_CHECK(cudaEventSynchronize(ev1_));
    cudaEventElapsedTime(&times_.glow, ev0_, ev1_);
    {   // metering for the eye's adaptation: the glow pyramid's 1/64 level is a free, well-filtered thumbnail
        const unsigned int mw = std::max(1u, width_ >> 6), mh = std::max(1u, height_ >> 6);
        meter_.resize(size_t(mw) * mh);
        CUDA_CHECK(cudaMemcpy(meter_.data(), wideLevels_[5], meter_.size() * sizeof(float4), cudaMemcpyDeviceToHost));
        double sum = 0.0;
        for (const float4& c : meter_) sum += std::log2(std::max(0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z, 1e-6f));
        metered_ = float(sum / meter_.size());
    }
    params_.glowWide = wideAccum_[0];
    params_.glowNarrow = narrowAccum_[0];
    launch(rgFinal_, width_, height_, &times_.final);
    params_.histRead ^= 1;
    ++params_.frame;
    ++params_.frameIndex;
    params_.prevCam = params_.cam;
}

void FenerRenderer::download(std::vector<uchar4>& out) {
    out.resize(size_t(width_) * height_);
    CUDA_CHECK(cudaMemcpy(out.data(), params_.output, out.size() * sizeof(uchar4), cudaMemcpyDeviceToHost));
}
void FenerRenderer::downloadHdr(std::vector<float4>& out) {
    out.resize(size_t(width_) * height_);
    CUDA_CHECK(cudaMemcpy(out.data(), params_.hdr, out.size() * sizeof(float4), cudaMemcpyDeviceToHost));
}

void FenerRenderer::shutdown() {
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
