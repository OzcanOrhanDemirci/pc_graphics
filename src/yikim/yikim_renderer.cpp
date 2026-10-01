#include "yikim/yikim_renderer.h"

#include <nvrtc.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

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
    return true;
}

bool YikimRenderer::compileDevice(std::string& log) {
    const std::string path = root_ + "/src/device/yikim.cu";
    const std::string src = readFile(path);
    if (src.empty()) { log = "cannot read " + path; return false; }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "yikim.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) { log = "nvrtcCreateProgram failed"; return false; }
    const std::string inc[] = {"-I" + root_ + "/src", "-I" + root_ + "/src/device", "-I" + root_ + "/third_party/optix/include",
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
    OptixProgramGroupDesc d[3];
    std::memset(d, 0, sizeof(d));
    d[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[0].raygen.module = module;
    d[0].raygen.entryFunctionName = "__raygen__render";
    d[1].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[2].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    std::vector<OptixProgramGroup> groups(3);
    OptixProgramGroupOptions go{};
    olen = sizeof(olog);
    r = optixProgramGroupCreate(ctx_, d, 3, &go, olog, &olen, groups.data());
    if (r != OPTIX_SUCCESS) { log = std::string("optixProgramGroupCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    OptixPipelineLinkOptions lo{};
    lo.maxTraceDepth = 1;
    OptixPipeline pipeline = nullptr;
    olen = sizeof(olog);
    r = optixPipelineCreate(ctx_, &pco, &lo, groups.data(), 3, olog, &olen, &pipeline);
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
    iasTemp_ = iasOut_ = nullptr;
    iasTempSize_ = iasOutSize_ = 0;
    instanceCount_ = 0;
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
        bodies_.push_back(b);
    }
    instanceCount_ = int(inst.size());
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
    w.writeTransforms(dInstances_, sizeof(OptixInstance), 1, instanceCount_, stream_);
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
    YCUDA(cudaMemcpyAsync(reinterpret_cast<void*>(dParams_), &p_, sizeof(YikimParams), cudaMemcpyHostToDevice, stream_));
    YCUDA(cudaEventRecord(e0_, stream_));
    YOPTIX(optixLaunch(pipeline_, stream_, dParams_, sizeof(YikimParams), &sbt_, w_, h_, 1));
    YCUDA(cudaEventRecord(e1_, stream_));
    YCUDA(cudaEventSynchronize(e1_));
    YCUDA(cudaEventElapsedTime(&lastMs, e0_, e1_));
    p_.samples += spp;
    ++p_.seed;
}

void YikimRenderer::download(std::vector<uchar4>& out) {
    out.resize(size_t(w_) * h_);
    YCUDA(cudaMemcpy(out.data(), p_.output, out.size() * sizeof(uchar4), cudaMemcpyDeviceToHost));
}

void YikimRenderer::shutdown() {
    freeWorld();
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    if (ctx_) optixDeviceContextDestroy(ctx_);
    pipeline_ = nullptr;
    module_ = nullptr;
    ctx_ = nullptr;
    groups_.clear();
}
