#include "scenes/tanecik/tanecik_renderer.h"

#include <nvrtc.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#define TCUDA(x)                                                                                        \
    do {                                                                                                \
        cudaError_t e_ = (x);                                                                           \
        if (e_ != cudaSuccess) {                                                                        \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                               \
        }                                                                                               \
    } while (0)
#define TOPTIX(x)                                                                                         \
    do {                                                                                                  \
        OptixResult r_ = (x);                                                                             \
        if (r_ != OPTIX_SUCCESS) {                                                                        \
            std::fprintf(stderr, "OptiX %s:%d %s: %s\n", __FILE__, __LINE__, #x, optixGetErrorString(r_)); \
            std::exit(1);                                                                                 \
        }                                                                                                 \
    } while (0)

namespace {

struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) TRecord {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

void tlog(unsigned int level, const char* tag, const char* msg, void*) {
    if (level <= 2) std::fprintf(stderr, "[optix %u][%s] %s\n", level, tag, msg);
}
std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
template <class T> T* talloc(size_t n) {
    void* p = nullptr;
    TCUDA(cudaMalloc(&p, std::max<size_t>(n, 1) * sizeof(T)));
    TCUDA(cudaMemset(p, 0, std::max<size_t>(n, 1) * sizeof(T)));
    return static_cast<T*>(p);
}

}   // namespace

bool TanecikRenderer::init(unsigned int width, unsigned int height, const std::string& root) {
    root_ = root;
    w_ = width;
    h_ = height;
    TCUDA(cudaFree(nullptr));
    TCUDA(cudaStreamCreate(&stream_));
    TCUDA(cudaEventCreate(&e0_));
    TCUDA(cudaEventCreate(&e1_));
    TOPTIX(optixInit());
    OptixDeviceContextOptions o{};
    o.logCallbackFunction = tlog;
    o.logCallbackLevel = 4;
    TOPTIX(optixDeviceContextCreate(nullptr, &o, &ctx_));
    p_.width = width;
    p_.height = height;
    p_.accum = talloc<float4>(size_t(width) * height);
    p_.output = talloc<uchar4>(size_t(width) * height);
    TCUDA(cudaMalloc(reinterpret_cast<void**>(&dParams_), sizeof(TanecikParams)));
    dInstances_ = talloc<OptixInstance>(4);
    dRadius_ = talloc<float>(1);
    return true;
}

bool TanecikRenderer::compileDevice(std::string& log) {
    const std::string path = root_ + "/src/scenes/tanecik/device/tanecik.cu";
    const std::string src = readFile(path);
    if (src.empty()) { log = "cannot read " + path; return false; }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "tanecik.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) { log = "nvrtcCreateProgram failed"; return false; }
    const std::string inc[] = {"-I" + root_ + "/src", "-I" + root_ + "/third_party/optix/include", "-I" + root_ + "/third_party/cuda/include"};
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
    pco.pipelineLaunchParamsSizeInBytes = sizeof(TanecikParams);
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE | OPTIX_PRIMITIVE_TYPE_FLAGS_SPHERE;
    char olog[8192];
    size_t olen = sizeof(olog);
    OptixModule module = nullptr, sphereModule = nullptr;
    OptixResult r = optixModuleCreate(ctx_, &mco, &pco, ir.data(), ir.size(), olog, &olen, &module);
    if (r != OPTIX_SUCCESS) { log = std::string("optixModuleCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    OptixBuiltinISOptions bo{};
    bo.builtinISModuleType = OPTIX_PRIMITIVE_TYPE_SPHERE;
    r = optixBuiltinISModuleGet(ctx_, &mco, &pco, &bo, &sphereModule);
    if (r != OPTIX_SUCCESS) { log = std::string("optixBuiltinISModuleGet: ") + optixGetErrorString(r); return false; }
    OptixProgramGroupDesc d[4];
    std::memset(d, 0, sizeof(d));
    d[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[0].raygen.module = module;
    d[0].raygen.entryFunctionName = "__raygen__render";
    d[1].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[2].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;   // triangles: nothing runs (the hit object is read in raygen)
    d[3].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;   // spheres: OptiX's own intersection
    d[3].hitgroup.moduleIS = sphereModule;
    d[3].hitgroup.entryFunctionNameIS = nullptr;
    std::vector<OptixProgramGroup> groups(4);
    OptixProgramGroupOptions go{};
    olen = sizeof(olog);
    r = optixProgramGroupCreate(ctx_, d, 4, &go, olog, &olen, groups.data());
    if (r != OPTIX_SUCCESS) { log = std::string("optixProgramGroupCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    OptixPipelineLinkOptions lo{};
    lo.maxTraceDepth = 1;
    OptixPipeline pipeline = nullptr;
    olen = sizeof(olog);
    r = optixPipelineCreate(ctx_, &pco, &lo, groups.data(), 4, olog, &olen, &pipeline);
    if (r != OPTIX_SUCCESS) { log = std::string("optixPipelineCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    TOPTIX(optixPipelineSetStackSizeFromCallDepths(pipeline, 1, 0, 0, 0, 2));
    TCUDA(cudaDeviceSynchronize());
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    pipeline_ = pipeline;
    groups_ = groups;
    module_ = module;
    sphereModule_ = sphereModule;   // owned by the context
    auto record = [&](OptixProgramGroup g, CUdeviceptr dst, size_t slot) {
        TRecord rec{};
        TOPTIX(optixSbtRecordPackHeader(g, &rec));
        TCUDA(cudaMemcpy(reinterpret_cast<void*>(dst + slot * sizeof(TRecord)), &rec, sizeof(TRecord), cudaMemcpyHostToDevice));
    };
    if (!rg_) TCUDA(cudaMalloc(reinterpret_cast<void**>(&rg_), sizeof(TRecord)));
    if (!miss_) TCUDA(cudaMalloc(reinterpret_cast<void**>(&miss_), sizeof(TRecord)));
    if (!hit_) TCUDA(cudaMalloc(reinterpret_cast<void**>(&hit_), 2 * sizeof(TRecord)));
    record(groups_[0], rg_, 0);
    record(groups_[1], miss_, 0);
    record(groups_[2], hit_, 0);
    record(groups_[3], hit_, 1);
    sbt_ = {};
    sbt_.raygenRecord = rg_;
    sbt_.missRecordBase = miss_;
    sbt_.missRecordStrideInBytes = sizeof(TRecord);
    sbt_.missRecordCount = 1;
    sbt_.hitgroupRecordBase = hit_;
    sbt_.hitgroupRecordStrideInBytes = sizeof(TRecord);
    sbt_.hitgroupRecordCount = 2;
    log = nvlog;
    return true;
}

void TanecikRenderer::freeMeshes() {
    TCUDA(cudaDeviceSynchronize());
    for (void* p : meshBuffers_) cudaFree(p);
    meshBuffers_.clear();
    dMeshes_ = nullptr;
    for (auto& g : meshGas_) g = 0;
}

void TanecikRenderer::freeParticles() {
    TCUDA(cudaDeviceSynchronize());
    for (void* p : {static_cast<void*>(dCentres_), static_cast<void*>(dTint_), sphTemp_, sphOut_})
        if (p) cudaFree(p);
    dCentres_ = nullptr;
    dTint_ = nullptr;
    sphTemp_ = sphOut_ = nullptr;
    sphTempSize_ = sphOutSize_ = 0;
    particleCount_ = particleCapacity_ = 0;
    sphereGas_ = 0;
}

void TanecikRenderer::uploadMeshes(const TriMesh& studio, const TriMesh& skin, const TriMesh& bones) {
    freeMeshes();
    const TriMesh* ms[3] = {&studio, &skin, &bones};
    TMesh hm[3];
    for (int k = 0; k < 3; ++k) {
        const TriMesh& m = *ms[k];
        float3* dp = talloc<float3>(m.pos.size());
        float3* dn = talloc<float3>(m.nrm.size());
        uint3* di = talloc<uint3>(m.idx.size());
        TCUDA(cudaMemcpy(dp, m.pos.data(), m.pos.size() * sizeof(float3), cudaMemcpyHostToDevice));
        TCUDA(cudaMemcpy(dn, m.nrm.data(), m.nrm.size() * sizeof(float3), cudaMemcpyHostToDevice));
        TCUDA(cudaMemcpy(di, m.idx.data(), m.idx.size() * sizeof(uint3), cudaMemcpyHostToDevice));
        meshBuffers_.insert(meshBuffers_.end(), {dp, dn, di});
        hm[k] = {dp, dn, di};
        OptixBuildInput bi{};
        bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        CUdeviceptr verts = CUdeviceptr(dp);
        bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        bi.triangleArray.vertexStrideInBytes = sizeof(float3);
        bi.triangleArray.numVertices = unsigned(m.pos.size());
        bi.triangleArray.vertexBuffers = &verts;
        bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        bi.triangleArray.indexStrideInBytes = sizeof(uint3);
        bi.triangleArray.numIndexTriplets = unsigned(m.idx.size());
        bi.triangleArray.indexBuffer = CUdeviceptr(di);
        const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
        bi.triangleArray.flags = flags;
        bi.triangleArray.numSbtRecords = 1;
        OptixAccelBuildOptions ao{};
        ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
        ao.operation = OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes sz{};
        TOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
        void *temp = nullptr, *out = nullptr;
        TCUDA(cudaMalloc(&temp, sz.tempSizeInBytes));
        TCUDA(cudaMalloc(&out, sz.outputSizeInBytes));
        TOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(temp), sz.tempSizeInBytes, CUdeviceptr(out), sz.outputSizeInBytes, &meshGas_[k], nullptr, 0));
        TCUDA(cudaStreamSynchronize(stream_));
        cudaFree(temp);
        meshBuffers_.push_back(out);
    }
    dMeshes_ = talloc<TMesh>(3);
    TCUDA(cudaMemcpy(dMeshes_, hm, sizeof(hm), cudaMemcpyHostToDevice));
    meshBuffers_.push_back(dMeshes_);
    p_.meshes = dMeshes_;
    buildIAS();
}

void TanecikRenderer::buildSpheres() {
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_SPHERES;
    CUdeviceptr vb = CUdeviceptr(dCentres_), rb = CUdeviceptr(dRadius_);
    bi.sphereArray.vertexBuffers = &vb;
    bi.sphereArray.vertexStrideInBytes = sizeof(float3);
    bi.sphereArray.numVertices = unsigned(particleCount_);
    bi.sphereArray.radiusBuffers = &rb;
    bi.sphereArray.singleRadius = 1;
    const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
    bi.sphereArray.flags = flags;
    bi.sphereArray.numSbtRecords = 1;
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    TOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
    if (sz.tempSizeInBytes > sphTempSize_) {
        if (sphTemp_) cudaFree(sphTemp_);
        TCUDA(cudaMalloc(&sphTemp_, sz.tempSizeInBytes));
        sphTempSize_ = sz.tempSizeInBytes;
    }
    if (sz.outputSizeInBytes > sphOutSize_) {
        if (sphOut_) cudaFree(sphOut_);
        TCUDA(cudaMalloc(&sphOut_, sz.outputSizeInBytes));
        sphOutSize_ = sz.outputSizeInBytes;
    }
    TOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(sphTemp_), sphTempSize_, CUdeviceptr(sphOut_), sphOutSize_, &sphereGas_, nullptr, 0));
}

void TanecikRenderer::uploadParticles(const std::vector<float3>& c, const std::vector<float>& tint, float radius) {
    const int n = int(c.size());
    if (n > particleCapacity_) {
        freeParticles();
        dCentres_ = talloc<float3>(n);
        dTint_ = talloc<float>(n);
        particleCapacity_ = n;
    }
    particleCount_ = n;
    if (n > 0) {
        TCUDA(cudaMemcpyAsync(dCentres_, c.data(), size_t(n) * sizeof(float3), cudaMemcpyHostToDevice, stream_));
        if (tint.size() == c.size()) TCUDA(cudaMemcpyAsync(dTint_, tint.data(), size_t(n) * sizeof(float), cudaMemcpyHostToDevice, stream_));
        else TCUDA(cudaMemsetAsync(dTint_, 0, size_t(n) * sizeof(float), stream_));
    }
    TCUDA(cudaMemcpyAsync(dRadius_, &radius, sizeof(float), cudaMemcpyHostToDevice, stream_));
    p_.particles = dCentres_;
    p_.particleTint = dTint_;
    p_.particleRadius = radius;
    if (n > 0) buildSpheres();
    else sphereGas_ = 0;
    TCUDA(cudaStreamSynchronize(stream_));
    buildIAS();
}

void TanecikRenderer::setVisible(TKind k, bool on) {
    if (visible_[k] == on) return;
    visible_[k] = on;
    buildIAS();
}

void TanecikRenderer::buildIAS() {
    std::vector<OptixInstance> inst;
    const OptixTraversableHandle gas[4] = {meshGas_[0], meshGas_[1], meshGas_[2], sphereGas_};
    for (unsigned k = 0; k < 4; ++k) {
        if (!gas[k] || !visible_[k]) continue;
        OptixInstance oi{};
        oi.transform[0] = oi.transform[5] = oi.transform[10] = 1.0f;
        oi.instanceId = k;
        oi.sbtOffset = k == TK_PARTICLES ? 1 : 0;
        oi.visibilityMask = 1;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = gas[k];
        inst.push_back(oi);
    }
    if (inst.empty()) { p_.handle = 0; return; }
    TCUDA(cudaMemcpyAsync(dInstances_, inst.data(), inst.size() * sizeof(OptixInstance), cudaMemcpyHostToDevice, stream_));
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = CUdeviceptr(dInstances_);
    bi.instanceArray.numInstances = unsigned(inst.size());
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    TOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
    if (sz.tempSizeInBytes > iasTempSize_) {
        if (iasTemp_) cudaFree(iasTemp_);
        TCUDA(cudaMalloc(&iasTemp_, sz.tempSizeInBytes));
        iasTempSize_ = sz.tempSizeInBytes;
    }
    if (sz.outputSizeInBytes > iasOutSize_) {
        if (iasOut_) cudaFree(iasOut_);
        TCUDA(cudaMalloc(&iasOut_, sz.outputSizeInBytes));
        iasOutSize_ = sz.outputSizeInBytes;
    }
    TOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(iasTemp_), iasTempSize_, CUdeviceptr(iasOut_), iasOutSize_, &p_.handle, nullptr, 0));
    TCUDA(cudaStreamSynchronize(stream_));
}

void TanecikRenderer::render(unsigned int spp, bool accumulate) {
    if (!accumulate) p_.samples = 0;
    p_.spp = spp;
    TCUDA(cudaMemcpyAsync(reinterpret_cast<void*>(dParams_), &p_, sizeof(TanecikParams), cudaMemcpyHostToDevice, stream_));
    TCUDA(cudaEventRecord(e0_, stream_));
    if (p_.handle) TOPTIX(optixLaunch(pipeline_, stream_, dParams_, sizeof(TanecikParams), &sbt_, w_, h_, 1));
    TCUDA(cudaEventRecord(e1_, stream_));
    TCUDA(cudaEventSynchronize(e1_));
    TCUDA(cudaEventElapsedTime(&lastMs, e0_, e1_));
    p_.samples += spp;
    ++p_.seed;
}

void TanecikRenderer::downloadTo(uchar4* dst) {
    TCUDA(cudaMemcpyAsync(dst, p_.output, size_t(w_) * h_ * sizeof(uchar4), cudaMemcpyDeviceToHost, stream_));
    TCUDA(cudaStreamSynchronize(stream_));
}

void TanecikRenderer::download(std::vector<uchar4>& out) {
    out.resize(size_t(w_) * h_);
    TCUDA(cudaMemcpy(out.data(), p_.output, out.size() * sizeof(uchar4), cudaMemcpyDeviceToHost));
}

void TanecikRenderer::shutdown() {
    if (!ctx_) return;
    TCUDA(cudaDeviceSynchronize());
    freeMeshes();
    freeParticles();
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    optixDeviceContextDestroy(ctx_);
    pipeline_ = nullptr;
    module_ = sphereModule_ = nullptr;
    ctx_ = nullptr;
    groups_.clear();
    for (void* b : {static_cast<void*>(p_.accum), static_cast<void*>(p_.output), static_cast<void*>(dInstances_), static_cast<void*>(dRadius_), iasTemp_, iasOut_,
                    reinterpret_cast<void*>(dParams_), reinterpret_cast<void*>(rg_), reinterpret_cast<void*>(miss_), reinterpret_cast<void*>(hit_)})
        if (b) cudaFree(b);
    p_ = TanecikParams{};
    dInstances_ = nullptr;
    dRadius_ = nullptr;
    iasTemp_ = iasOut_ = nullptr;
    iasTempSize_ = iasOutSize_ = 0;
    dParams_ = rg_ = miss_ = hit_ = 0;
    for (cudaEvent_t* e : {&e0_, &e1_})
        if (*e) { cudaEventDestroy(*e); *e = nullptr; }
    if (stream_) cudaStreamDestroy(stream_);
    stream_ = nullptr;
}
