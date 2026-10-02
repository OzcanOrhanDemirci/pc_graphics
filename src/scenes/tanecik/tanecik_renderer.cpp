#include "scenes/tanecik/tanecik_renderer.h"
#include "scenes/tanecik/tanecik_kernels.h"

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
    dInstances_ = talloc<OptixInstance>(TK_KINDS + B_COUNT);
    dRadius_ = talloc<float>(1);
    dBoneXf_ = talloc<float>(12 * B_COUNT);
    setBoneXforms(boneXf_);
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

void TanecikRenderer::freeGas(Gas& g) {
    if (g.temp) cudaFree(g.temp);
    if (g.out) cudaFree(g.out);
    g = Gas{};
}

void TanecikRenderer::freeMeshes() {
    TCUDA(cudaDeviceSynchronize());
    for (void* p : meshBuffers_) cudaFree(p);
    meshBuffers_.clear();
    for (Gas& g : gas_) freeGas(g);
    gas_.clear();
    meshes_.clear();
    meshVerts_.clear();
    meshTris_.clear();
    dMeshes_ = nullptr;
    for (void* p : {static_cast<void*>(dBind_), static_cast<void*>(dParticleBone_)})
        if (p) cudaFree(p);
    dBind_ = nullptr;
    dParticleBone_ = nullptr;
    bindCount_ = 0;
}

void TanecikRenderer::freeParticles() {
    TCUDA(cudaDeviceSynchronize());
    for (void* p : {static_cast<void*>(dCentres_), static_cast<void*>(dTint_), static_cast<void*>(dSubset_)})
        if (p) cudaFree(p);
    dCentres_ = nullptr;
    dTint_ = nullptr;
    dSubset_ = nullptr;
    freeGas(spheres_);
    particleCount_ = particleCapacity_ = 0;
}

// a mesh's structure: built for tracing once (fast: false), or rebuilt quickly every frame it deforms (the skin)
void TanecikRenderer::buildMeshGas(int k, bool fast) {
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    CUdeviceptr verts = CUdeviceptr(meshes_[k].pos);
    bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
    bi.triangleArray.vertexStrideInBytes = sizeof(float3);
    bi.triangleArray.numVertices = unsigned(meshVerts_[k]);
    bi.triangleArray.vertexBuffers = &verts;
    bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    bi.triangleArray.indexStrideInBytes = sizeof(uint3);
    bi.triangleArray.numIndexTriplets = unsigned(meshTris_[k]);
    bi.triangleArray.indexBuffer = CUdeviceptr(meshes_[k].idx);
    const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
    bi.triangleArray.flags = flags;
    bi.triangleArray.numSbtRecords = 1;
    OptixAccelBuildOptions ao{};
    ao.buildFlags = fast ? OPTIX_BUILD_FLAG_PREFER_FAST_BUILD : OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    TOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
    Gas& g = gas_[k];
    if (sz.tempSizeInBytes > g.tempSize) {
        if (g.temp) cudaFree(g.temp);
        TCUDA(cudaMalloc(&g.temp, sz.tempSizeInBytes));
        g.tempSize = sz.tempSizeInBytes;
    }
    if (sz.outputSizeInBytes > g.outSize) {
        if (g.out) cudaFree(g.out);
        TCUDA(cudaMalloc(&g.out, sz.outputSizeInBytes));
        g.outSize = sz.outputSizeInBytes;
    }
    TOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(g.temp), g.tempSize, CUdeviceptr(g.out), g.outSize, &g.handle, nullptr, 0));
}

void TanecikRenderer::uploadMeshes(const TriMesh& studio, const TriMesh& skin, const std::vector<TriMesh>& bones) {
    freeMeshes();
    std::vector<const TriMesh*> ms = {&studio, &skin};
    for (const TriMesh& b : bones) ms.push_back(&b);
    gas_.resize(ms.size());
    for (const TriMesh* m : ms) {
        float3* dp = talloc<float3>(m->pos.size());
        float3* dn = talloc<float3>(m->nrm.size());
        uint3* di = talloc<uint3>(m->idx.size());
        TCUDA(cudaMemcpy(dp, m->pos.data(), m->pos.size() * sizeof(float3), cudaMemcpyHostToDevice));
        TCUDA(cudaMemcpy(dn, m->nrm.data(), m->nrm.size() * sizeof(float3), cudaMemcpyHostToDevice));
        TCUDA(cudaMemcpy(di, m->idx.data(), m->idx.size() * sizeof(uint3), cudaMemcpyHostToDevice));
        meshBuffers_.insert(meshBuffers_.end(), {dp, dn, di});
        meshes_.push_back({dp, dn, di});
        meshVerts_.push_back(int(m->pos.size()));
        meshTris_.push_back(int(m->idx.size()));
    }
    for (int k = 0; k < int(ms.size()); ++k)
        if (meshTris_[k] > 0) buildMeshGas(k, k == TK_SKIN);
    TCUDA(cudaStreamSynchronize(stream_));
    dMeshes_ = talloc<TMesh>(meshes_.size());
    TCUDA(cudaMemcpy(dMeshes_, meshes_.data(), meshes_.size() * sizeof(TMesh), cudaMemcpyHostToDevice));
    meshBuffers_.push_back(dMeshes_);
    p_.meshes = dMeshes_;
    buildIAS();
}

void TanecikRenderer::setBoneXforms(const Xform* xf) {
    for (int b = 0; b < B_COUNT; ++b) boneXf_[b] = xf[b];
    TCUDA(cudaMemcpyAsync(dBoneXf_, boneXf_, sizeof(boneXf_), cudaMemcpyHostToDevice, stream_));
    if (!gas_.empty()) buildIAS();
}

void TanecikRenderer::setSkinBinding(const std::vector<SkinBind>& bind, const std::vector<uint8_t>& particleBone) {
    for (void* p : {static_cast<void*>(dBind_), static_cast<void*>(dParticleBone_)})
        if (p) cudaFree(p);
    dBind_ = talloc<SkinBind>(bind.size());
    dParticleBone_ = talloc<uint8_t>(particleBone.size());
    TCUDA(cudaMemcpy(dBind_, bind.data(), bind.size() * sizeof(SkinBind), cudaMemcpyHostToDevice));
    TCUDA(cudaMemcpy(dParticleBone_, particleBone.data(), particleBone.size(), cudaMemcpyHostToDevice));
    bindCount_ = int(bind.size());
}

void TanecikRenderer::deformSkin(const float3* dParticles) {
    if (!bindCount_ || gas_.size() <= TK_SKIN || bindCount_ != meshVerts_[TK_SKIN]) return;
    launchSkin(dBind_, bindCount_, dParticles, dParticleBone_, dBoneXf_, const_cast<float3*>(meshes_[TK_SKIN].pos),
               const_cast<float3*>(meshes_[TK_SKIN].nrm), stream_);
    buildMeshGas(TK_SKIN, true);
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
    Gas& g = spheres_;
    if (sz.tempSizeInBytes > g.tempSize) {
        if (g.temp) cudaFree(g.temp);
        TCUDA(cudaMalloc(&g.temp, sz.tempSizeInBytes));
        g.tempSize = sz.tempSizeInBytes;
    }
    if (sz.outputSizeInBytes > g.outSize) {
        if (g.out) cudaFree(g.out);
        TCUDA(cudaMalloc(&g.out, sz.outputSizeInBytes));
        g.outSize = sz.outputSizeInBytes;
    }
    TOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(g.temp), g.tempSize, CUdeviceptr(g.out), g.outSize, &g.handle, nullptr, 0));
}

void TanecikRenderer::setParticleSubset(const std::vector<uint32_t>& idx, float radius) {
    const int n = int(idx.size());
    if (n > particleCapacity_) {
        freeParticles();
        dCentres_ = talloc<float3>(n);
        dTint_ = talloc<float>(n);
        dSubset_ = talloc<uint32_t>(n);
        particleCapacity_ = n;
    }
    particleCount_ = n;
    if (n > 0) TCUDA(cudaMemcpyAsync(dSubset_, idx.data(), size_t(n) * sizeof(uint32_t), cudaMemcpyHostToDevice, stream_));
    TCUDA(cudaMemcpyAsync(dRadius_, &radius, sizeof(float), cudaMemcpyHostToDevice, stream_));
    p_.particles = dCentres_;
    p_.particleTint = dTint_;
    p_.particleRadius = radius;
    if (n == 0) {
        spheres_.handle = 0;
        buildIAS();
    }
}

void TanecikRenderer::updateParticles(const float3* dPos, const float* dTint) {
    if (particleCount_ <= 0) return;
    launchGatherParticles(dPos, dTint, dSubset_, particleCount_, dCentres_, dTint_, hideLiquidParticles, stream_);
    buildSpheres();
    buildIAS();
}

bool TanecikRenderer::initLiquid(int maxParticles, int maxVertices, int maxTriangles, int maxBlocks, float floorY) {
    fluid_.settings.floorY = floorY;
    liquidReady_ = fluid_.init(maxParticles, maxVertices, maxTriangles, maxBlocks);
    return liquidReady_;
}

void TanecikRenderer::updateLiquid(const float3* dPos, const float* dWeight, int n, float radius) {
    if (!liquidReady_) return;
    fluid_.build(dPos, dWeight, n, radius, stream_);
    // the surface: rebuilt (its topology changes every frame)
    liquidGas_.handle = 0;
    if (fluid_.triangleCount() > 0) {
        OptixBuildInput bi{};
        bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        CUdeviceptr verts = CUdeviceptr(fluid_.vertices());
        bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        bi.triangleArray.vertexStrideInBytes = sizeof(float3);
        bi.triangleArray.numVertices = unsigned(fluid_.vertexCount());
        bi.triangleArray.vertexBuffers = &verts;
        bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        bi.triangleArray.indexStrideInBytes = sizeof(uint3);
        bi.triangleArray.numIndexTriplets = unsigned(fluid_.triangleCount());
        bi.triangleArray.indexBuffer = CUdeviceptr(fluid_.indices());
        const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
        bi.triangleArray.flags = flags;
        bi.triangleArray.numSbtRecords = 1;
        OptixAccelBuildOptions ao{};
        ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
        ao.operation = OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes sz{};
        TOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
        Gas& g = liquidGas_;
        if (sz.tempSizeInBytes > g.tempSize) {
            if (g.temp) cudaFree(g.temp);
            TCUDA(cudaMalloc(&g.temp, sz.tempSizeInBytes));
            g.tempSize = sz.tempSizeInBytes;
        }
        if (sz.outputSizeInBytes > g.outSize) {
            if (g.out) cudaFree(g.out);
            TCUDA(cudaMalloc(&g.out, sz.outputSizeInBytes));
            g.outSize = sz.outputSizeInBytes;
        }
        TOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(g.temp), g.tempSize, CUdeviceptr(g.out), g.outSize, &g.handle, nullptr, 0));
    }
    p_.liquid = {fluid_.vertices(), fluid_.normals(), fluid_.indices()};
    // the droplets: spheres of their own radius (centre and radius interleaved, 16 bytes each)
    dropsGas_.handle = 0;
    if (fluid_.dropletCount() > 0) {
        OptixBuildInput bi{};
        bi.type = OPTIX_BUILD_INPUT_TYPE_SPHERES;
        CUdeviceptr vb = CUdeviceptr(fluid_.droplets()), rb = CUdeviceptr(reinterpret_cast<const char*>(fluid_.droplets()) + 12);
        bi.sphereArray.vertexBuffers = &vb;
        bi.sphereArray.vertexStrideInBytes = sizeof(float4);
        bi.sphereArray.numVertices = unsigned(fluid_.dropletCount());
        bi.sphereArray.radiusBuffers = &rb;
        bi.sphereArray.radiusStrideInBytes = sizeof(float4);
        bi.sphereArray.singleRadius = 0;
        const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
        bi.sphereArray.flags = flags;
        bi.sphereArray.numSbtRecords = 1;
        OptixAccelBuildOptions ao{};
        ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
        ao.operation = OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes sz{};
        TOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
        Gas& g = dropsGas_;
        if (sz.tempSizeInBytes > g.tempSize) {
            if (g.temp) cudaFree(g.temp);
            TCUDA(cudaMalloc(&g.temp, sz.tempSizeInBytes));
            g.tempSize = sz.tempSizeInBytes;
        }
        if (sz.outputSizeInBytes > g.outSize) {
            if (g.out) cudaFree(g.out);
            TCUDA(cudaMalloc(&g.out, sz.outputSizeInBytes));
            g.outSize = sz.outputSizeInBytes;
        }
        TOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(g.temp), g.tempSize, CUdeviceptr(g.out), g.outSize, &g.handle, nullptr, 0));
    }
    p_.drops = fluid_.droplets();
    buildIAS();
}

void TanecikRenderer::initStain(int res, float2 mn, float size) {
    if (dStain_) cudaFree(dStain_);
    dStain_ = talloc<float>(size_t(res) * res);
    p_.stain = dStain_;
    p_.stainRes = res;
    p_.stainMin = mn;
    p_.stainSize = size;
}

void TanecikRenderer::soak(const float3* dPos, const uint32_t* dIdx, int n, float litresEach, float footprint) {
    if (!dStain_) return;
    launchStain(dPos, dIdx, n, litresEach, footprint, dStain_, p_.stainRes, p_.stainMin, p_.stainSize, stream_);
}

void TanecikRenderer::wipeStain() {
    if (dStain_) TCUDA(cudaMemsetAsync(dStain_, 0, size_t(p_.stainRes) * p_.stainRes * sizeof(float), stream_));
}

void TanecikRenderer::setVisible(TKind k, bool on) {
    if (visible_[k] == on) return;
    visible_[k] = on;
    buildIAS();
}

void TanecikRenderer::buildIAS() {
    std::vector<OptixInstance> inst;
    auto add = [&](OptixTraversableHandle h, unsigned kind, unsigned sub, const float* xf) {
        if (!h || !visible_[kind]) return;
        OptixInstance oi{};
        if (xf) std::memcpy(oi.transform, xf, 12 * sizeof(float));
        else oi.transform[0] = oi.transform[5] = oi.transform[10] = 1.0f;
        oi.instanceId = kind | (sub << 8);
        oi.sbtOffset = kind == TK_PARTICLES || kind == TK_DROPS ? 1 : 0;   // OptiX's sphere intersection
        oi.visibilityMask = 1;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = h;
        inst.push_back(oi);
    };
    if (gas_.size() >= 2) {
        add(gas_[TK_STUDIO].handle, TK_STUDIO, 0, nullptr);
        add(gas_[TK_SKIN].handle, TK_SKIN, 0, nullptr);
        for (int b = 0; b < B_COUNT && 2 + b < int(gas_.size()); ++b) add(gas_[2 + b].handle, TK_BONES, unsigned(b), boneXf_[b].m);
    }
    add(spheres_.handle, TK_PARTICLES, 0, nullptr);
    add(liquidGas_.handle, TK_LIQUID, 0, nullptr);
    add(dropsGas_.handle, TK_DROPS, 0, nullptr);
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
    if (sz.tempSizeInBytes > ias_.tempSize) {
        if (ias_.temp) cudaFree(ias_.temp);
        TCUDA(cudaMalloc(&ias_.temp, sz.tempSizeInBytes));
        ias_.tempSize = sz.tempSizeInBytes;
    }
    if (sz.outputSizeInBytes > ias_.outSize) {
        if (ias_.out) cudaFree(ias_.out);
        TCUDA(cudaMalloc(&ias_.out, sz.outputSizeInBytes));
        ias_.outSize = sz.outputSizeInBytes;
    }
    TOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(ias_.temp), ias_.tempSize, CUdeviceptr(ias_.out), ias_.outSize, &p_.handle, nullptr, 0));
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
    freeGas(ias_);
    if (dStain_) cudaFree(dStain_);
    dStain_ = nullptr;
    freeGas(liquidGas_);
    freeGas(dropsGas_);
    fluid_.destroy();
    liquidReady_ = false;
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    optixDeviceContextDestroy(ctx_);
    pipeline_ = nullptr;
    module_ = sphereModule_ = nullptr;
    ctx_ = nullptr;
    groups_.clear();
    for (void* b : {static_cast<void*>(p_.accum), static_cast<void*>(p_.output), static_cast<void*>(dInstances_), static_cast<void*>(dRadius_),
                    static_cast<void*>(dBoneXf_), reinterpret_cast<void*>(dParams_), reinterpret_cast<void*>(rg_), reinterpret_cast<void*>(miss_),
                    reinterpret_cast<void*>(hit_)})
        if (b) cudaFree(b);
    p_ = TanecikParams{};
    dInstances_ = nullptr;
    dRadius_ = nullptr;
    dBoneXf_ = nullptr;
    dParams_ = rg_ = miss_ = hit_ = 0;
    for (cudaEvent_t* e : {&e0_, &e1_})
        if (*e) { cudaEventDestroy(*e); *e = nullptr; }
    if (stream_) cudaStreamDestroy(stream_);
    stream_ = nullptr;
}
