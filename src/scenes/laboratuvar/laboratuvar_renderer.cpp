#include "scenes/laboratuvar/laboratuvar_renderer.h"
#include "scenes/laboratuvar/lab_kernels.h"

#include <nvrtc.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#define LCUDA(x)                                                                                        \
    do {                                                                                                \
        cudaError_t e_ = (x);                                                                           \
        if (e_ != cudaSuccess) {                                                                        \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                               \
        }                                                                                               \
    } while (0)
#define LOPTIX(x)                                                                                         \
    do {                                                                                                  \
        OptixResult r_ = (x);                                                                             \
        if (r_ != OPTIX_SUCCESS) {                                                                        \
            std::fprintf(stderr, "OptiX %s:%d %s: %s\n", __FILE__, __LINE__, #x, optixGetErrorString(r_)); \
            std::exit(1);                                                                                 \
        }                                                                                                 \
    } while (0)

namespace {

struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) LRecord {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

void llog(unsigned int level, const char* tag, const char* msg, void*) {
    if (level <= 2) std::fprintf(stderr, "[optix %u][%s] %s\n", level, tag, msg);
}
std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
template <class T> T* lalloc(size_t n) {
    void* p = nullptr;
    LCUDA(cudaMalloc(&p, std::max<size_t>(n, 1) * sizeof(T)));
    LCUDA(cudaMemset(p, 0, std::max<size_t>(n, 1) * sizeof(T)));
    return static_cast<T*>(p);
}

}   // namespace

bool LabRenderer::init(unsigned int width, unsigned int height, const std::string& root) {
    root_ = root;
    w_ = width;
    h_ = height;
    LCUDA(cudaFree(nullptr));
    LCUDA(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking));
    LCUDA(cudaEventCreate(&e0_));
    LCUDA(cudaEventCreate(&e1_));
    LOPTIX(optixInit());
    OptixDeviceContextOptions o{};
    o.logCallbackFunction = llog;
    o.logCallbackLevel = 4;
    LOPTIX(optixDeviceContextCreate(nullptr, &o, &ctx_));
    p_.width = width;
    p_.height = height;
    p_.accum = lalloc<float4>(size_t(width) * height);
    p_.output = lalloc<uchar4>(size_t(width) * height);
    LCUDA(cudaMalloc(reinterpret_cast<void**>(&dParams_), sizeof(LabParams)));
    dPick_ = lalloc<LPick>(1);
    return true;
}

bool LabRenderer::compileDevice(std::string& log) {
    const std::string path = root_ + "/src/scenes/laboratuvar/device/laboratuvar.cu";
    const std::string src = readFile(path);
    if (src.empty()) { log = "cannot read " + path; return false; }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "laboratuvar.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) { log = "nvrtcCreateProgram failed"; return false; }
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
    pco.pipelineLaunchParamsSizeInBytes = sizeof(LabParams);
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
    OptixProgramGroupDesc d[5];
    std::memset(d, 0, sizeof(d));
    d[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[0].raygen.module = module;
    d[0].raygen.entryFunctionName = "__raygen__render";
    d[1].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[2].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;   // triangles: nothing runs (the hit object is read in raygen)
    d[3].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;   // spheres: OptiX's own intersection
    d[3].hitgroup.moduleIS = sphereModule;
    d[3].hitgroup.entryFunctionNameIS = nullptr;
    d[4].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[4].raygen.module = module;
    d[4].raygen.entryFunctionName = "__raygen__pick";
    std::vector<OptixProgramGroup> groups(5);
    OptixProgramGroupOptions go{};
    olen = sizeof(olog);
    r = optixProgramGroupCreate(ctx_, d, 5, &go, olog, &olen, groups.data());
    if (r != OPTIX_SUCCESS) { log = std::string("optixProgramGroupCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    OptixPipelineLinkOptions lo{};
    lo.maxTraceDepth = 1;
    OptixPipeline pipeline = nullptr;
    olen = sizeof(olog);
    r = optixPipelineCreate(ctx_, &pco, &lo, groups.data(), 5, olog, &olen, &pipeline);
    if (r != OPTIX_SUCCESS) { log = std::string("optixPipelineCreate: ") + optixGetErrorString(r) + "\n" + olog; return false; }
    LOPTIX(optixPipelineSetStackSizeFromCallDepths(pipeline, 1, 0, 0, 0, 2));
    LCUDA(cudaDeviceSynchronize());
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    pipeline_ = pipeline;
    groups_ = groups;
    module_ = module;
    sphereModule_ = sphereModule;
    auto record = [&](OptixProgramGroup g, CUdeviceptr dst, size_t slot) {
        LRecord rec{};
        LOPTIX(optixSbtRecordPackHeader(g, &rec));
        LCUDA(cudaMemcpy(reinterpret_cast<void*>(dst + slot * sizeof(LRecord)), &rec, sizeof(LRecord), cudaMemcpyHostToDevice));
    };
    if (!rg_) LCUDA(cudaMalloc(reinterpret_cast<void**>(&rg_), sizeof(LRecord)));
    if (!miss_) LCUDA(cudaMalloc(reinterpret_cast<void**>(&miss_), sizeof(LRecord)));
    if (!hit_) LCUDA(cudaMalloc(reinterpret_cast<void**>(&hit_), 2 * sizeof(LRecord)));
    if (!rgPick_) LCUDA(cudaMalloc(reinterpret_cast<void**>(&rgPick_), sizeof(LRecord)));
    record(groups_[0], rg_, 0);
    record(groups_[1], miss_, 0);
    record(groups_[2], hit_, 0);
    record(groups_[3], hit_, 1);
    record(groups_[4], rgPick_, 0);
    sbt_ = {};
    sbt_.raygenRecord = rg_;
    sbt_.missRecordBase = miss_;
    sbt_.missRecordStrideInBytes = sizeof(LRecord);
    sbt_.missRecordCount = 1;
    sbt_.hitgroupRecordBase = hit_;
    sbt_.hitgroupRecordStrideInBytes = sizeof(LRecord);
    sbt_.hitgroupRecordCount = 2;
    log = nvlog;
    return true;
}

int LabRenderer::addMaterial(const LMaterial& m) {
    mats_.push_back(m);
    return int(mats_.size()) - 1;
}

void LabRenderer::uploadMaterials() {
    if (int(mats_.size()) > dMatsCap_) {
        if (dMats_) cudaFree(dMats_);
        dMatsCap_ = int(mats_.size()) + 8;
        dMats_ = lalloc<LMaterial>(size_t(dMatsCap_));
    }
    LCUDA(cudaMemcpyAsync(dMats_, mats_.data(), mats_.size() * sizeof(LMaterial), cudaMemcpyHostToDevice, stream_));
    LCUDA(cudaStreamSynchronize(stream_));
    p_.materials = dMats_;
}

void LabRenderer::freeGas(Gas& g) {
    if (g.temp) cudaFree(g.temp);
    if (g.out) cudaFree(g.out);
    g = Gas{};
}

void LabRenderer::ensure(Gas& g, const OptixAccelBufferSizes& sz) {
    if (sz.tempSizeInBytes > g.tempSize) {
        if (g.temp) cudaFree(g.temp);
        LCUDA(cudaMalloc(&g.temp, sz.tempSizeInBytes));
        g.tempSize = sz.tempSizeInBytes;
    }
    if (sz.outputSizeInBytes > g.outSize) {
        if (g.out) cudaFree(g.out);
        LCUDA(cudaMalloc(&g.out, sz.outputSizeInBytes));
        g.outSize = sz.outputSizeInBytes;
    }
}

void LabRenderer::clearObjects() {
    LCUDA(cudaDeviceSynchronize());
    for (Obj& o : objs_) {
        freeGas(o.gas);
        if (!o.external)
            for (void* p : {static_cast<void*>(o.pos), static_cast<void*>(o.nrm), static_cast<void*>(o.idx), static_cast<void*>(o.spheres),
                            static_cast<void*>(o.tint)})
                if (p) cudaFree(p);
    }
    objs_.clear();
    for (Surf& s : surfaces_) s.fs->destroy();
    surfaces_.clear();
    objsDirty_ = iasDirty_ = true;
    p_.handle = 0;
}

void LabRenderer::buildTriangles(Obj& o, bool fast) {
    o.gas.handle = 0;
    if (o.nTris <= 0) return;
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    CUdeviceptr verts = CUdeviceptr(o.pos);
    bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
    bi.triangleArray.vertexStrideInBytes = sizeof(float3);
    bi.triangleArray.numVertices = unsigned(o.nVerts);
    bi.triangleArray.vertexBuffers = &verts;
    bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    bi.triangleArray.indexStrideInBytes = sizeof(uint3);
    bi.triangleArray.numIndexTriplets = unsigned(o.nTris);
    bi.triangleArray.indexBuffer = CUdeviceptr(o.idx);
    const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
    bi.triangleArray.flags = flags;
    bi.triangleArray.numSbtRecords = 1;
    OptixAccelBuildOptions ao{};
    ao.buildFlags = fast ? OPTIX_BUILD_FLAG_PREFER_FAST_BUILD : OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    LOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
    ensure(o.gas, sz);
    LOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(o.gas.temp), o.gas.tempSize, CUdeviceptr(o.gas.out), o.gas.outSize, &o.gas.handle, nullptr, 0));
    iasDirty_ = true;
}

void LabRenderer::buildSpheres(Obj& o, CUdeviceptr centres, CUdeviceptr radii, unsigned stride, int n) {
    o.gas.handle = 0;
    iasDirty_ = true;
    if (n <= 0) return;
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_SPHERES;
    bi.sphereArray.vertexBuffers = &centres;
    bi.sphereArray.vertexStrideInBytes = stride;
    bi.sphereArray.numVertices = unsigned(n);
    bi.sphereArray.radiusBuffers = &radii;
    bi.sphereArray.radiusStrideInBytes = stride;
    bi.sphereArray.singleRadius = 0;
    const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
    bi.sphereArray.flags = flags;
    bi.sphereArray.numSbtRecords = 1;
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    LOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
    ensure(o.gas, sz);
    LOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(o.gas.temp), o.gas.tempSize, CUdeviceptr(o.gas.out), o.gas.outSize, &o.gas.handle, nullptr, 0));
}

int LabRenderer::addMesh(const HostMesh& m, int material) {
    Obj o;
    o.kind = 0;
    o.material = material;
    o.nVerts = o.maxVerts = int(m.pos.size());
    o.nTris = o.maxTris = int(m.idx.size());
    o.pos = lalloc<float3>(m.pos.size());
    o.nrm = lalloc<float3>(m.nrm.size());
    o.idx = lalloc<uint3>(m.idx.size());
    LCUDA(cudaMemcpy(o.pos, m.pos.data(), m.pos.size() * sizeof(float3), cudaMemcpyHostToDevice));
    LCUDA(cudaMemcpy(o.nrm, m.nrm.data(), m.nrm.size() * sizeof(float3), cudaMemcpyHostToDevice));
    LCUDA(cudaMemcpy(o.idx, m.idx.data(), m.idx.size() * sizeof(uint3), cudaMemcpyHostToDevice));
    buildTriangles(o, false);
    LCUDA(cudaStreamSynchronize(stream_));
    objs_.push_back(o);
    objsDirty_ = true;
    return int(objs_.size()) - 1;
}

int LabRenderer::addDynamicMesh(int maxVertices, int maxTriangles, int material) {
    Obj o;
    o.kind = 0;
    o.material = material;
    o.maxVerts = maxVertices;
    o.maxTris = maxTriangles;
    o.pos = lalloc<float3>(size_t(maxVertices));
    o.nrm = lalloc<float3>(size_t(maxVertices));
    o.idx = lalloc<uint3>(size_t(maxTriangles));
    objs_.push_back(o);
    objsDirty_ = true;
    return int(objs_.size()) - 1;
}

float3* LabRenderer::meshPositions(int obj) { return objs_[size_t(obj)].pos; }
float3* LabRenderer::meshNormals(int obj) { return objs_[size_t(obj)].nrm; }
uint3* LabRenderer::meshIndices(int obj) { return objs_[size_t(obj)].idx; }

void LabRenderer::rebuildMesh(int obj, int nVerts, int nTris) {
    Obj& o = objs_[size_t(obj)];
    o.nVerts = std::min(nVerts, o.maxVerts);
    o.nTris = std::min(nTris, o.maxTris);
    buildTriangles(o, true);
}

void LabRenderer::updateMesh(int obj, const float3* dPos, const float3* dNrm, int nVerts, const uint3* dIdx, int nTris) {
    Obj& o = objs_[size_t(obj)];
    nVerts = std::min(nVerts, o.maxVerts);
    nTris = std::min(nTris, o.maxTris);
    LCUDA(cudaMemcpyAsync(o.pos, dPos, size_t(nVerts) * sizeof(float3), cudaMemcpyDeviceToDevice, stream_));
    if (dNrm) LCUDA(cudaMemcpyAsync(o.nrm, dNrm, size_t(nVerts) * sizeof(float3), cudaMemcpyDeviceToDevice, stream_));
    if (dIdx) LCUDA(cudaMemcpyAsync(o.idx, dIdx, size_t(nTris) * sizeof(uint3), cudaMemcpyDeviceToDevice, stream_));
    rebuildMesh(obj, nVerts, nTris);
}

int LabRenderer::addSpheres(int capacity, int material, bool tinted) {
    Obj o;
    o.kind = 1;
    o.material = material;
    o.capacity = capacity;
    o.spheres = lalloc<float4>(size_t(capacity));
    if (tinted) o.tint = lalloc<float>(size_t(capacity));
    objs_.push_back(o);
    objsDirty_ = true;
    return int(objs_.size()) - 1;
}

float4* LabRenderer::spheresBuffer(int obj) { return objs_[size_t(obj)].spheres; }
float* LabRenderer::tintBuffer(int obj) { return objs_[size_t(obj)].tint; }

void LabRenderer::setSphereCount(int obj, int n) {
    Obj& o = objs_[size_t(obj)];
    o.count = std::min(n, o.capacity);
    buildSpheres(o, CUdeviceptr(o.spheres), CUdeviceptr(reinterpret_cast<char*>(o.spheres) + 12), sizeof(float4), o.count);
}

int LabRenderer::addSurface(int material, int maxParticles, int maxVertices, int maxTriangles, int maxBlocks, const FluidSurfaceSettings& s) {
    Surf sf;
    sf.fs = std::make_unique<FluidSurface>();
    sf.fs->settings = s;
    if (!sf.fs->init(maxParticles, maxVertices, maxTriangles, maxBlocks)) return -1;
    Obj mesh;
    mesh.kind = 0;
    mesh.material = material;
    mesh.external = true;
    mesh.pos = const_cast<float3*>(sf.fs->vertices());
    mesh.nrm = const_cast<float3*>(sf.fs->normals());
    mesh.idx = const_cast<uint3*>(sf.fs->indices());
    mesh.maxVerts = maxVertices;
    mesh.maxTris = maxTriangles;
    objs_.push_back(mesh);
    sf.meshObj = int(objs_.size()) - 1;
    Obj drops;
    drops.kind = 1;
    drops.material = material;
    drops.external = true;
    drops.spheres = const_cast<float4*>(sf.fs->droplets());
    objs_.push_back(drops);
    sf.dropObj = int(objs_.size()) - 1;
    surfaces_.push_back(std::move(sf));
    objsDirty_ = true;
    return int(surfaces_.size()) - 1;
}

void LabRenderer::buildSurface(int surf, const float3* dPos, const float* dWeight, int n, float radius) {
    Surf& s = surfaces_[size_t(surf)];
    Obj& mesh = objs_[size_t(s.meshObj)];
    Obj& drops = objs_[size_t(s.dropObj)];
    if (n <= 0 || !s.visible) {
        mesh.gas.handle = drops.gas.handle = 0;
        iasDirty_ = true;
        return;
    }
    s.fs->build(dPos, dWeight, n, radius, stream_);
    surfaceMs += s.fs->lastMs;
    mesh.nVerts = s.fs->vertexCount();
    mesh.nTris = s.fs->triangleCount();
    buildTriangles(mesh, true);
    // droplets: centre and radius interleaved
    drops.spheres = const_cast<float4*>(s.fs->droplets());
    drops.count = s.fs->dropletCount();
    buildSpheres(drops, CUdeviceptr(drops.spheres), CUdeviceptr(reinterpret_cast<char*>(drops.spheres) + 12), sizeof(float4), drops.count);
    objsDirty_ = true;   // the droplets' pointer is fixed, but keep the table honest
}

void LabRenderer::setSurfaceVisible(int surf, bool on) {
    Surf& s = surfaces_[size_t(surf)];
    s.visible = on;
    objs_[size_t(s.meshObj)].visible = on;
    objs_[size_t(s.dropObj)].visible = on;
    iasDirty_ = true;
}

void LabRenderer::setDropletsVisible(int surf, bool on) {
    objs_[size_t(surfaces_[size_t(surf)].dropObj)].visible = on;
    iasDirty_ = true;
}

void LabRenderer::setTransform(int obj, const float xf[12]) {
    std::memcpy(objs_[size_t(obj)].xf, xf, 12 * sizeof(float));
    iasDirty_ = true;
}

void LabRenderer::setVisible(int obj, bool on) {
    if (objs_[size_t(obj)].visible == on) return;
    objs_[size_t(obj)].visible = on;
    iasDirty_ = true;
}

void LabRenderer::uploadObjects() {
    std::vector<LObject> t(objs_.size());
    for (size_t i = 0; i < objs_.size(); ++i) {
        const Obj& o = objs_[i];
        t[i].kind = o.kind;
        t[i].material = o.material;
        t[i].pos = o.pos;
        t[i].nrm = o.nrm;
        t[i].idx = o.idx;
        t[i].spheres = o.spheres;
        t[i].tint = o.tint;
    }
    if (int(t.size()) > dObjsCap_) {
        if (dObjs_) cudaFree(dObjs_);
        dObjsCap_ = int(t.size()) + 16;
        dObjs_ = lalloc<LObject>(size_t(dObjsCap_));
    }
    if (!t.empty()) LCUDA(cudaMemcpyAsync(dObjs_, t.data(), t.size() * sizeof(LObject), cudaMemcpyHostToDevice, stream_));
    p_.objects = dObjs_;
    objsDirty_ = false;
}

void LabRenderer::buildIAS() {
    std::vector<OptixInstance> inst;
    for (size_t i = 0; i < objs_.size(); ++i) {
        const Obj& o = objs_[i];
        if (!o.gas.handle || !o.visible) continue;
        OptixInstance oi{};
        std::memcpy(oi.transform, o.xf, 12 * sizeof(float));
        oi.instanceId = unsigned(i);
        oi.sbtOffset = o.kind == 1 ? 1 : 0;
        oi.visibilityMask = 1;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = o.gas.handle;
        inst.push_back(oi);
    }
    iasDirty_ = false;
    if (inst.empty()) { p_.handle = 0; return; }
    if (int(inst.size()) > dInstancesCap_) {
        if (dInstances_) cudaFree(dInstances_);
        dInstancesCap_ = int(inst.size()) + 16;
        dInstances_ = lalloc<OptixInstance>(size_t(dInstancesCap_));
    }
    LCUDA(cudaMemcpyAsync(dInstances_, inst.data(), inst.size() * sizeof(OptixInstance), cudaMemcpyHostToDevice, stream_));
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = CUdeviceptr(dInstances_);
    bi.instanceArray.numInstances = unsigned(inst.size());
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    LOPTIX(optixAccelComputeMemoryUsage(ctx_, &ao, &bi, 1, &sz));
    ensure(ias_, sz);
    LOPTIX(optixAccelBuild(ctx_, stream_, &ao, &bi, 1, CUdeviceptr(ias_.temp), ias_.tempSize, CUdeviceptr(ias_.out), ias_.outSize, &p_.handle, nullptr, 0));
}

namespace {
OptixImage2D limage(void* d, unsigned w, unsigned h) {
    OptixImage2D im{};
    im.data = CUdeviceptr(d);
    im.width = w;
    im.height = h;
    im.pixelStrideInBytes = sizeof(float4);
    im.rowStrideInBytes = w * sizeof(float4);
    im.format = OPTIX_PIXEL_FORMAT_FLOAT4;
    return im;
}
}   // namespace

void LabRenderer::createDenoiser() {
    OptixDenoiserOptions o{};
    o.guideAlbedo = 1;
    o.guideNormal = 1;
    LOPTIX(optixDenoiserCreate(ctx_, OPTIX_DENOISER_MODEL_KIND_AOV, &o, &dn_));
    OptixDenoiserSizes sz{};
    LOPTIX(optixDenoiserComputeMemoryResources(dn_, w_, h_, &sz));
    dnStateSize_ = sz.stateSizeInBytes;
    dnScratchSize_ = sz.withoutOverlapScratchSizeInBytes;
    LCUDA(cudaMalloc(&dnState_, dnStateSize_));
    LCUDA(cudaMalloc(&dnScratch_, dnScratchSize_));
    dnIn_ = lalloc<float4>(size_t(w_) * h_);
    dnOut_ = lalloc<float4>(size_t(w_) * h_);
    dnIntensity_ = lalloc<float>(1);
    dnAvg_ = lalloc<float>(3);
    if (!p_.albedoOut) p_.albedoOut = lalloc<float4>(size_t(w_) * h_);
    if (!p_.normalOut) p_.normalOut = lalloc<float4>(size_t(w_) * h_);
    for (float4*& pb : pos_) if (!pb) pb = lalloc<float4>(size_t(w_) * h_);
    p_.posOut = pos_[posCur_];
    for (float4*& hb : hist_) if (!hb) hb = lalloc<float4>(size_t(w_) * h_);
    LOPTIX(optixDenoiserSetup(dn_, stream_, w_, h_, CUdeviceptr(dnState_), dnStateSize_, CUdeviceptr(dnScratch_), dnScratchSize_));
    LCUDA(cudaStreamSynchronize(stream_));
}

void LabRenderer::freeDenoiser() {
    if (dn_) optixDenoiserDestroy(dn_);
    dn_ = nullptr;
    for (void* p : {dnState_, dnScratch_, static_cast<void*>(dnIn_), static_cast<void*>(dnOut_), static_cast<void*>(dnIntensity_), static_cast<void*>(dnAvg_),
                    static_cast<void*>(hist_[0]), static_cast<void*>(hist_[1]), static_cast<void*>(pos_[0]), static_cast<void*>(pos_[1])})
        if (p) cudaFree(p);
    hist_[0] = hist_[1] = pos_[0] = pos_[1] = nullptr;
    p_.posOut = nullptr;
    histValid_ = false;
    dnState_ = dnScratch_ = nullptr;
    dnIn_ = dnOut_ = nullptr;
    dnIntensity_ = dnAvg_ = nullptr;
}

void LabRenderer::runTemporal() {
    // this frame's samples (accum holds only them) into the history, the result to the denoiser's input
    TCam pc;
    pc.pos = prevCam_.pos;
    pc.right = prevCam_.right;
    pc.up = prevCam_.up;
    pc.fwd = prevCam_.fwd;
    pc.tanHalfX = prevCam_.tanHalfX;
    pc.tanHalfY = prevCam_.tanHalfY;
    pc.moved = length(p_.cam.pos - prevCam_.pos);
    float4* in = hist_[histCur_];
    float4* out = hist_[histCur_ ^ 1];
    // still: a plain mean, unclamped (a converged history clamped to a noisy 4-sample neighbourhood would flicker where
    // glass shows a bright path only now and then)
    launchTemporal(p_.accum, 1.0f / float(std::max(1u, p_.samples)), pos_[posCur_], pos_[posCur_ ^ 1], pc, in, out, int(w_), int(h_),
                   still ? 0.0f : temporalAlpha, still ? 1e30f : temporalGamma, still ? 4096.0f : 1.0f / temporalAlpha, !histValid_, stream_);
    histCur_ ^= 1;
    histValid_ = true;
    prevCam_ = p_.cam;
    posCur_ ^= 1;   // the next frame writes the other one (this one is its history's)
    p_.posOut = pos_[posCur_];
}

void LabRenderer::runDenoiser() {
    const int n = int(w_ * h_);
    if (useTemporal_) launchDenoisePrep(hist_[histCur_], 1u, p_.exposure, n, dnIn_, stream_);
    else launchDenoisePrep(p_.accum, p_.samples, p_.exposure, n, dnIn_, stream_);
    OptixDenoiserLayer layer{};
    layer.input = limage(dnIn_, w_, h_);
    layer.output = limage(dnOut_, w_, h_);
    layer.type = OPTIX_DENOISER_AOV_TYPE_BEAUTY;
    OptixDenoiserGuideLayer g{};
    g.albedo = limage(p_.albedoOut, w_, h_);
    g.normal = limage(p_.normalOut, w_, h_);
    LOPTIX(optixDenoiserComputeIntensity(dn_, stream_, &layer.input, CUdeviceptr(dnIntensity_), CUdeviceptr(dnScratch_), dnScratchSize_));
    LOPTIX(optixDenoiserComputeAverageColor(dn_, stream_, &layer.input, CUdeviceptr(dnAvg_), CUdeviceptr(dnScratch_), dnScratchSize_));
    OptixDenoiserParams dp{};
    dp.hdrIntensity = CUdeviceptr(dnIntensity_);
    dp.hdrAverageColor = CUdeviceptr(dnAvg_);
    dp.blendFactor = 0.0f;
    LOPTIX(optixDenoiserInvoke(dn_, stream_, &dp, CUdeviceptr(dnState_), dnStateSize_, &g, &layer, 1, 0, 0, CUdeviceptr(dnScratch_), dnScratchSize_));
    launchTonemap(dnOut_, int(w_), int(h_), 0u, p_.output, stream_);   // a fixed dither: breaks bands, does not shimmer
}

void LabRenderer::render(unsigned int spp, bool accumulate) {
    if (objsDirty_) uploadObjects();
    if (iasDirty_) buildIAS();
    if ((denoise || temporal) && !dn_) createDenoiser();
    useTemporal_ = temporal && !accumulate;   // stills accumulate as they always did
    if (accumulate) histValid_ = false;
    if (!accumulate) p_.samples = 0;
    p_.spp = spp;
    LCUDA(cudaMemcpyAsync(reinterpret_cast<void*>(dParams_), &p_, sizeof(LabParams), cudaMemcpyHostToDevice, stream_));
    LCUDA(cudaEventRecord(e0_, stream_));
    if (p_.handle) LOPTIX(optixLaunch(pipeline_, stream_, dParams_, sizeof(LabParams), &sbt_, w_, h_, 1));
    p_.samples += spp;
    if (useTemporal_ && p_.handle) runTemporal();
    if (denoise && p_.handle) runDenoiser();
    else if (useTemporal_ && p_.handle) {   // no denoiser: the accumulation itself, tone mapped
        launchDenoisePrep(hist_[histCur_], 1u, p_.exposure, int(w_ * h_), dnIn_, stream_);
        launchTonemap(dnIn_, int(w_), int(h_), 0u, p_.output, stream_);
    }
    p_.samples -= spp;
    LCUDA(cudaEventRecord(e1_, stream_));
    LCUDA(cudaEventSynchronize(e1_));
    LCUDA(cudaEventElapsedTime(&lastMs, e0_, e1_));
    p_.samples += spp;
    ++p_.seed;
    surfaceMs = 0.0f;
}

LPick LabRenderer::pick(float x, float y) {
    if (objsDirty_) uploadObjects();
    if (iasDirty_) buildIAS();
    p_.pickPixel = make_float2(x, y);
    p_.pickOut = dPick_;
    LCUDA(cudaMemcpyAsync(reinterpret_cast<void*>(dParams_), &p_, sizeof(LabParams), cudaMemcpyHostToDevice, stream_));
    OptixShaderBindingTable sbt = sbt_;
    sbt.raygenRecord = rgPick_;
    LPick out{};
    out.object = -1;
    if (p_.handle) {
        LOPTIX(optixLaunch(pipeline_, stream_, dParams_, sizeof(LabParams), &sbt, 1, 1, 1));
        LCUDA(cudaMemcpyAsync(&out, dPick_, sizeof(LPick), cudaMemcpyDeviceToHost, stream_));
    }
    LCUDA(cudaStreamSynchronize(stream_));
    return out;
}

void LabRenderer::downloadTo(uchar4* dst) {
    LCUDA(cudaMemcpyAsync(dst, p_.output, size_t(w_) * h_ * sizeof(uchar4), cudaMemcpyDeviceToHost, stream_));
    LCUDA(cudaStreamSynchronize(stream_));
}

void LabRenderer::download(std::vector<uchar4>& out) {
    out.resize(size_t(w_) * h_);
    LCUDA(cudaMemcpyAsync(out.data(), p_.output, out.size() * sizeof(uchar4), cudaMemcpyDeviceToHost, stream_));
    LCUDA(cudaStreamSynchronize(stream_));
}

void LabRenderer::shutdown() {
    if (!ctx_) return;
    LCUDA(cudaDeviceSynchronize());
    clearObjects();
    freeGas(ias_);
    freeDenoiser();
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    optixDeviceContextDestroy(ctx_);
    pipeline_ = nullptr;
    module_ = sphereModule_ = nullptr;
    ctx_ = nullptr;
    groups_.clear();
    for (void* b : {static_cast<void*>(p_.accum), static_cast<void*>(p_.output), static_cast<void*>(p_.albedoOut), static_cast<void*>(p_.normalOut),
                    static_cast<void*>(dInstances_), static_cast<void*>(dPick_), static_cast<void*>(dMats_), static_cast<void*>(dObjs_),
                    reinterpret_cast<void*>(dParams_), reinterpret_cast<void*>(rg_), reinterpret_cast<void*>(rgPick_), reinterpret_cast<void*>(miss_),
                    reinterpret_cast<void*>(hit_)})
        if (b) cudaFree(b);
    p_ = LabParams{};
    dInstances_ = nullptr;
    dInstancesCap_ = 0;
    dPick_ = nullptr;
    dMats_ = nullptr;
    dMatsCap_ = 0;
    dObjs_ = nullptr;
    dObjsCap_ = 0;
    dParams_ = rg_ = rgPick_ = miss_ = hit_ = 0;
    mats_.clear();
    for (cudaEvent_t* e : {&e0_, &e1_})
        if (*e) { cudaEventDestroy(*e); *e = nullptr; }
    if (stream_) cudaStreamDestroy(stream_);
    stream_ = nullptr;
}
