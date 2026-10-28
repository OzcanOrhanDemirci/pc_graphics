#include "scenes/ates/ates_renderer.h"

#include "core/bloom.h"
#include "scenes/ates/ates_spectra.h"

#include <nvrtc.h>
#include <optix_stubs.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

namespace {

const int kMaxInputs = 8;   // build inputs a GAS may have (identical hit records)

struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) Record {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

void optixLog(unsigned int level, const char* tag, const char* msg, void*) {
    if (level <= 2) std::fprintf(stderr, "[optix %u][%s] %s\n", level, tag, msg);
}
std::string readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
template <class T> T* toDevice(const std::vector<T>& v, std::vector<void*>& owned) {
    if (v.empty()) return nullptr;
    T* p = nullptr;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&p), v.size() * sizeof(T)));
    CUDA_CHECK(cudaMemcpy(p, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    owned.push_back(p);
    return p;
}
template <class T> void upload(T*& dst, size_t& cap, const std::vector<T>& v) {
    if (v.empty()) return;
    if (v.size() > cap) {
        if (dst) cudaFree(dst);
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dst), v.size() * sizeof(T)));
        cap = v.size();
    }
    CUDA_CHECK(cudaMemcpy(dst, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
}

}   // namespace

bool AtesRenderer::init(int width, int height, const std::string& root) {
    root_ = root;
    width_ = width;
    height_ = height;
    cudaDeviceProp prop{};
    CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
    deviceName = prop.name;
    CUDA_CHECK(cudaSetDevice(0));
    CUDA_CHECK(cudaFree(nullptr));
    CUDA_CHECK(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking));
    for (cudaEvent_t* e : {&ev0_, &ev1_, &ev2_, &ev3_, &evD_}) CUDA_CHECK(cudaEventCreate(e));
    OPTIX_CHECK(optixInit());
    OptixDeviceContextOptions o{};
    o.logCallbackFunction = optixLog;
    o.logCallbackLevel = 4;
    OPTIX_CHECK(optixDeviceContextCreate(nullptr, &o, &context_));
    params = {};
    params.layout = unsigned(sizeof(AtesParams));
    params.width = unsigned(width);
    params.height = unsigned(height);
    const size_t n = size_t(width) * height;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&params.hdr), n * sizeof(float4)));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&params.surf), n * sizeof(float4)));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&params.depth), n * sizeof(float)));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&params.output), n * sizeof(uchar4)));
    CUDA_CHECK(cudaMemset(params.output, 0, n * sizeof(uchar4)));
    int lw = width / 2, lh = height / 2;
    for (int k = 0; k < kLevels; ++k) {
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&levels_[k]), size_t(std::max(lw, 1)) * std::max(lh, 1) * sizeof(float4)));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&accum_[k]), size_t(std::max(lw, 1)) * std::max(lh, 1) * sizeof(float4)));
        lw /= 2;
        lh /= 2;
    }
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dParams_), sizeof(AtesParams)));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&params.badCount), 5 * sizeof(unsigned int)));
    CUDA_CHECK(cudaMemset(params.badCount, 0, 5 * sizeof(unsigned int)));
    // the light of hot things (Planck), once
    std::vector<float3> soot, grey;
    ates_spectra::tables(soot, grey);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&tables_), 2 * ATES_TABLE_N * sizeof(float3)));
    CUDA_CHECK(cudaMemcpy(tables_, soot.data(), ATES_TABLE_N * sizeof(float3), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(tables_ + ATES_TABLE_N, grey.data(), ATES_TABLE_N * sizeof(float3), cudaMemcpyHostToDevice));
    params.sootEmit = tables_;
    params.greyEmit = tables_ + ATES_TABLE_N;
    params.sootAbs = ates_spectra::sootAbsorption();
    params.chemEmit = ates_spectra::chemiluminescence(2e-5);
    params.groundMaterial = -1;
    params.groundMaterial2 = -1;
    setPattern(4, 0.42f);
    return true;
}

void AtesRenderer::setPattern(int spp, float sigma) {
    spp = std::clamp(spp, 1, ATES_MAX_SPP);
    static const float2 p4[4] = {{0.375f, 0.125f}, {0.875f, 0.375f}, {0.125f, 0.625f}, {0.625f, 0.875f}};
    static const float2 p16[16] = {{0.5625f, 0.5625f}, {0.4375f, 0.3125f}, {0.3125f, 0.625f}, {0.75f, 0.4375f},
                                   {0.1875f, 0.375f}, {0.625f, 0.8125f}, {0.8125f, 0.6875f}, {0.6875f, 0.1875f},
                                   {0.375f, 0.875f}, {0.5f, 0.0625f}, {0.25f, 0.125f}, {0.125f, 0.75f},
                                   {0.0f, 0.5f}, {0.9375f, 0.25f}, {0.875f, 0.9375f}, {0.0625f, 0.0f}};
    const float2* src = spp >= 16 ? p16 : spp >= 4 ? p4 : nullptr;
    const int n = spp >= 16 ? 16 : spp >= 4 ? 4 : 1;
    params.spp = n;
    for (int k = 0; k < n; ++k) {
        const float2 u = src ? make_float2(src[k].x + 1.0f / 32.0f - 0.5f, src[k].y + 1.0f / 32.0f - 0.5f) : make_float2(0, 0);
        params.pattern[k] = make_float2(u.x * sigma * 2.0f, u.y * sigma * 2.0f);
    }
}

bool AtesRenderer::compile(std::string& log) {
    const std::string srcPath = root_ + "/src/scenes/ates/device/ates.cu";
    const std::string src = readText(srcPath);
    if (src.empty()) {
        log = "cannot read " + srcPath;
        return false;
    }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "ates.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) {
        log = "nvrtcCreateProgram failed";
        return false;
    }
    const std::string inc[] = {"-I" + root_ + "/src", "-I" + root_ + "/third_party/optix/include", "-I" + root_ + "/third_party/cuda/include"};
    std::vector<const char*> opts = {"--std=c++17", "-arch=compute_89", "--use_fast_math", "--relocatable-device-code=true", "--optix-ir", "-lineinfo", "-D__x86_64"};
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
    pco.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    pco.numPayloadValues = 4;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "params";
    pco.pipelineLaunchParamsSizeInBytes = sizeof(AtesParams);
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
    char olog[8192];
    size_t olen = sizeof(olog);
    OptixModule module = nullptr;
    OptixResult r = optixModuleCreate(context_, &mco, &pco, ir.data(), ir.size(), olog, &olen, &module);
    if (r != OPTIX_SUCCESS) {
        log = std::string("optixModuleCreate: ") + optixGetErrorString(r) + "\n" + olog;
        return false;
    }
    const char* rg[3] = {"__raygen__render", "__raygen__volume", "__raygen__final"};
    std::vector<OptixProgramGroupDesc> d(5);
    for (auto& x : d) std::memset(&x, 0, sizeof(x));
    for (int i = 0; i < 3; ++i) {
        d[i].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        d[i].raygen.module = module;
        d[i].raygen.entryFunctionName = rg[i];
    }
    d[3].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[4].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    d[4].hitgroup.moduleAH = module;
    d[4].hitgroup.entryFunctionNameAH = "__anyhit__none";
    std::vector<OptixProgramGroup> groups(d.size());
    OptixProgramGroupOptions go{};
    olen = sizeof(olog);
    r = optixProgramGroupCreate(context_, d.data(), unsigned(d.size()), &go, olog, &olen, groups.data());
    if (r != OPTIX_SUCCESS) {
        log = std::string("optixProgramGroupCreate: ") + optixGetErrorString(r) + "\n" + olog;
        return false;
    }
    OptixPipelineLinkOptions lo{};
    lo.maxTraceDepth = 1;
    OptixPipeline pipeline = nullptr;
    olen = sizeof(olog);
    r = optixPipelineCreate(context_, &pco, &lo, groups.data(), unsigned(groups.size()), olog, &olen, &pipeline);
    if (r != OPTIX_SUCCESS) {
        log = std::string("optixPipelineCreate: ") + optixGetErrorString(r) + "\n" + olog;
        return false;
    }
    OPTIX_CHECK(optixPipelineSetStackSizeFromCallDepths(pipeline, 1, 0, 0, 0, 2));
    CUDA_CHECK(cudaDeviceSynchronize());
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    pipeline_ = pipeline;
    groups_ = groups;
    module_ = module;
    auto record = [&](OptixProgramGroup g, CUdeviceptr& dst, int count) {
        std::vector<Record> recs(static_cast<size_t>(count));
        for (auto& rec : recs) OPTIX_CHECK(optixSbtRecordPackHeader(g, &rec));
        if (!dst) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dst), sizeof(Record) * size_t(count)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(dst), recs.data(), sizeof(Record) * size_t(count), cudaMemcpyHostToDevice));
    };
    record(groups_[0], rgRender_, 1);
    record(groups_[1], rgVolume_, 1);
    record(groups_[2], rgFinal_, 1);
    record(groups_[3], miss_, 1);
    record(groups_[4], hits_, kMaxInputs);
    sbt_ = {};
    sbt_.missRecordBase = miss_;
    sbt_.missRecordStrideInBytes = sizeof(Record);
    sbt_.missRecordCount = 1;
    sbt_.hitgroupRecordBase = hits_;
    sbt_.hitgroupRecordStrideInBytes = sizeof(Record);
    sbt_.hitgroupRecordCount = kMaxInputs;
    log = nvlog;
    return true;
}

OptixTraversableHandle AtesRenderer::buildGas(const std::vector<const OMeshCPU*>& meshes, std::vector<void*>& keep, std::vector<OGeom>& geoms, bool) {
    std::vector<OptixBuildInput> inputs(meshes.size());
    std::vector<CUdeviceptr> vbufs(meshes.size());
    std::vector<unsigned int> flags(meshes.size(), OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT);
    for (size_t gi = 0; gi < meshes.size(); ++gi) {
        const OMeshCPU& m = *meshes[gi];
        OGeom g{};
        g.pos = toDevice(m.pos, keep);
        g.nrm = toDevice(m.nrm, keep);
        g.uv = toDevice(m.uv, keep);
        g.idx = toDevice(m.idx, keep);
        g.ao = nullptr;
        g.material = m.material;
        g.uvScale = m.uvScale;
        geoms.push_back(g);
        OptixBuildInput& bi = inputs[gi];
        bi = {};
        bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        vbufs[gi] = CUdeviceptr(g.pos);
        bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        bi.triangleArray.vertexStrideInBytes = sizeof(float3);
        bi.triangleArray.numVertices = unsigned(m.pos.size());
        bi.triangleArray.vertexBuffers = &vbufs[gi];
        bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        bi.triangleArray.indexStrideInBytes = sizeof(uint3);
        bi.triangleArray.numIndexTriplets = unsigned(m.idx.size());
        bi.triangleArray.indexBuffer = CUdeviceptr(g.idx);
        bi.triangleArray.flags = &flags[gi];
        bi.triangleArray.numSbtRecords = 1;
    }
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE | OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    OPTIX_CHECK(optixAccelComputeMemoryUsage(context_, &ao, inputs.data(), unsigned(inputs.size()), &sz));
    void *tmp = nullptr, *out = nullptr, *cbuf = nullptr;
    CUDA_CHECK(cudaMalloc(&tmp, sz.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(&out, sz.outputSizeInBytes));
    size_t* dCompact = nullptr;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dCompact), sizeof(size_t)));
    OptixAccelEmitDesc em{};
    em.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
    em.result = CUdeviceptr(dCompact);
    OptixTraversableHandle h = 0;
    OPTIX_CHECK(optixAccelBuild(context_, stream_, &ao, inputs.data(), unsigned(inputs.size()), CUdeviceptr(tmp), sz.tempSizeInBytes, CUdeviceptr(out),
                                sz.outputSizeInBytes, &h, &em, 1));
    CUDA_CHECK(cudaStreamSynchronize(stream_));
    size_t compact = 0;
    CUDA_CHECK(cudaMemcpy(&compact, dCompact, sizeof(size_t), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMalloc(&cbuf, compact));
    OPTIX_CHECK(optixAccelCompact(context_, stream_, h, CUdeviceptr(cbuf), compact, &h));
    CUDA_CHECK(cudaStreamSynchronize(stream_));
    cudaFree(tmp);
    cudaFree(out);
    cudaFree(dCompact);
    keep.push_back(cbuf);
    gasBytes += compact;
    return h;
}

void AtesRenderer::uploadStatic(const OrmanAssets& assets, const std::vector<OProtoCPU>& extra, const std::vector<APlace>& places) {
    CUDA_CHECK(cudaDeviceSynchronize());
    for (void* p : static_) cudaFree(p);
    static_.clear();
    staticGas_.clear();
    staticInst_.clear();
    geomsHost_.clear();
    gasBytes = 0;
    std::vector<const OProtoCPU*> all;
    for (const auto& p : assets.protos) all.push_back(&p);
    for (const auto& p : extra) all.push_back(&p);
    for (const OProtoCPU* P : all) {
        std::vector<const OMeshCPU*> ms;
        for (const auto& g : P->geoms) ms.push_back(&g);
        AInst in{};
        in.firstGeom = unsigned(geomsHost_.size());
        in.geomCount = unsigned(ms.size());
        in.log = -1;
        in.scale = 1.0f;
        in.tint = make_float3(1, 1, 1);
        staticGas_.push_back(buildGas(ms, static_, geomsHost_, false));
        staticInst_.push_back(in);
    }
    staticGeoms_ = unsigned(geomsHost_.size());
    places_ = places;
    upload(dGeoms_, dGeomsCap_, geomsHost_);
    params.geoms = dGeoms_;
    iasDirty_ = true;
}

void AtesRenderer::uploadMaterials(const std::vector<OMaterial>& mats) {
    if (dMats_) cudaFree(dMats_);
    dMats_ = nullptr;
    size_t cap = 0;
    upload(dMats_, cap, mats);
    params.materials = dMats_;
}

void AtesRenderer::setLogs(const std::vector<ALogMesh>& meshes, const std::vector<int>& changed, int sideMaterial, int endMaterial) {
    CUDA_CHECK(cudaStreamSynchronize(stream_));
    logSideMat_ = sideMaterial;
    logEndMat_ = endMaterial;
    // a log thrown on adds its own structure; the others are rebuilt only as asked (their shape drifted). Rebuilding
    // them all at every log, several hundred with the twigs, faulted in OptiX's builder now and then (an illegal
    // address, the application gone: one throw in a few dozen)
    std::vector<int> todo = changed;
    if (meshes.size() < logGas_.size()) {
        for (auto& l : logGas_)
            for (void* p : l.bufs) cudaFree(p);
        logGas_.assign(meshes.size(), LogGas());
        todo.clear();
        for (size_t i = 0; i < meshes.size(); ++i) todo.push_back(int(i));
    } else if (meshes.size() > logGas_.size()) {
        const size_t old = logGas_.size();
        logGas_.resize(meshes.size());
        for (size_t i = old; i < meshes.size(); ++i) todo.push_back(int(i));
    }
    for (int i : todo) {
        LogGas& L = logGas_[size_t(i)];
        for (void* p : L.bufs) cudaFree(p);
        L.bufs.clear();
        OMeshCPU side = meshes[size_t(i)].side, ends = meshes[size_t(i)].ends;
        side.material = sideMaterial;
        ends.material = endMaterial;
        std::vector<OGeom> gs;
        L.handle = buildGas({&side, &ends}, L.bufs, gs, false);
        L.side = gs[0];
        L.ends = gs[1];
    }
    // the geometry table: the static ones, then each log's two
    geomsHost_.resize(staticGeoms_);
    for (const auto& L : logGas_) {
        geomsHost_.push_back(L.side);
        geomsHost_.push_back(L.ends);
    }
    upload(dGeoms_, dGeomsCap_, geomsHost_);
    params.geoms = dGeoms_;
    iasDirty_ = true;
}

void AtesRenderer::placeLogs(const std::vector<std::array<float, 12>>& poses) {
    logPoses_ = poses;
    iasDirty_ = true;
}

void AtesRenderer::setLogViews(const std::vector<ALogView>& views) {
    upload(dLogs_, dLogsCap_, views);
    params.logs = dLogs_;
}

void AtesRenderer::buildIas() {
    std::vector<OptixInstance> inst;
    std::vector<AInst> table;
    for (const APlace& p : places_) {
        OptixInstance oi{};
        std::memcpy(oi.transform, p.m, sizeof(p.m));
        oi.instanceId = unsigned(table.size());
        oi.sbtOffset = 0;
        oi.visibilityMask = 255;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = staticGas_[size_t(p.proto)];
        AInst a = staticInst_[size_t(p.proto)];
        a.tint = p.tint;
        table.push_back(a);
        inst.push_back(oi);
    }
    for (size_t i = 0; i < logGas_.size() && i < logPoses_.size(); ++i) {
        OptixInstance oi{};
        std::memcpy(oi.transform, logPoses_[i].data(), sizeof(oi.transform));
        oi.instanceId = unsigned(table.size());
        oi.visibilityMask = 255;
        oi.traversableHandle = logGas_[i].handle;
        AInst a{};
        a.firstGeom = staticGeoms_ + unsigned(2 * i);
        a.geomCount = 2;
        a.log = int(i);
        a.scale = 1.0f;
        a.tint = make_float3(1, 1, 1);
        table.push_back(a);
        inst.push_back(oi);
    }
    for (void* p : top_) cudaFree(p);
    top_.clear();
    upload(dInst_, dInstCap_, table);
    params.instances = dInst_;
    if (inst.empty()) {
        params.handle = 0;
        return;
    }
    void* dInstances = nullptr;
    CUDA_CHECK(cudaMalloc(&dInstances, inst.size() * sizeof(OptixInstance)));
    CUDA_CHECK(cudaMemcpyAsync(dInstances, inst.data(), inst.size() * sizeof(OptixInstance), cudaMemcpyHostToDevice, stream_));
    top_.push_back(dInstances);
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = CUdeviceptr(dInstances);
    bi.instanceArray.numInstances = unsigned(inst.size());
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    OPTIX_CHECK(optixAccelComputeMemoryUsage(context_, &ao, &bi, 1, &sz));
    void *tmp = nullptr, *out = nullptr;
    CUDA_CHECK(cudaMalloc(&tmp, sz.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(&out, sz.outputSizeInBytes));
    top_.push_back(tmp);
    top_.push_back(out);
    OPTIX_CHECK(optixAccelBuild(context_, stream_, &ao, &bi, 1, CUdeviceptr(tmp), sz.tempSizeInBytes, CUdeviceptr(out), sz.outputSizeInBytes, &params.handle,
                                nullptr, 0));
    iasDirty_ = false;
}

void AtesRenderer::setGas(cudaTextureObject_t tex, const unsigned int* bricks, float3 origin, int nx, int ny, int nz, float h, const float3* lightI, int lnx,
                          int lny, int lnz, float lightCell) {
    params.gasTex = tex;
    params.gasBricks = bricks;
    params.bnx = (nx + ATES_BRICK - 1) / ATES_BRICK;
    params.bny = (ny + ATES_BRICK - 1) / ATES_BRICK;
    params.bnz = (nz + ATES_BRICK - 1) / ATES_BRICK;
    params.gasOrigin = origin;
    params.gasSize = make_float3(nx * h, ny * h, nz * h);
    params.gasH = h;
    params.gnx = nx;
    params.gny = ny;
    params.gnz = nz;
    params.lightI = lightI;
    params.lnx = lnx;
    params.lny = lny;
    params.lnz = lnz;
    params.lightOrigin = origin;
    params.lightCell = lightCell;
    const int n = lnx * lny * lnz;
    if (n != lightN_) {
        if (lum_) cudaFree(lum_);
        if (cdf_) cudaFree(cdf_);
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&lum_), n * sizeof(float)));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&cdf_), n * sizeof(float)));
        CUDA_CHECK(cudaMemset(cdf_, 0, n * sizeof(float)));
        lightN_ = n;
    }
    params.lightCdf = cdf_;
    params.lightN = n;
    // the smoke's light on a grid of two blocks a side (its fluence changes over centimetres, not millimetres)
    const int sx = (lnx + 1) / 2, sy = (lny + 1) / 2, sz = (lnz + 1) / 2;
    if (sx * sy * sz != smokeN_) {
        if (smokeLight_) cudaFree(smokeLight_);
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&smokeLight_), size_t(sx) * sy * sz * sizeof(float4)));
        CUDA_CHECK(cudaMemset(smokeLight_, 0, size_t(sx) * sy * sz * sizeof(float4)));
        if (!smokeList_) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&smokeList_), 2 * kSmokeCap * sizeof(float4)));
        if (!smokeCount_) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&smokeCount_), sizeof(unsigned int)));
        smokeN_ = sx * sy * sz;
    }
    params.smokeLight = smokeLight_;
    params.snx = sx;
    params.sny = sy;
    params.snz = sz;
    params.smokeCell = 2.0f * lightCell;
}

void AtesRenderer::freePlanes() {
    for (Plane& p : planes_) {
        if (p.surf) cudaDestroySurfaceObject(p.surf);
        if (p.tex) cudaDestroyTextureObject(p.tex);
        if (p.arr) cudaFreeArray(p.arr);
        p = Plane();
    }
}

bool AtesRenderer::enableDlss(bool on) {
    if (on == dlssOn_) return true;
    CUDA_CHECK(cudaStreamSynchronize(stream_));
    dlssOn_ = false;
    params.dlss = 0;
    params.lodBias = 0.0f;
    dlss_.release();
    freePlanes();
    if (!on) return true;
    const cudaChannelFormatDesc f4 = cudaCreateChannelDesc<float4>(), f2 = cudaCreateChannelDesc<float2>(), f1 = cudaCreateChannelDesc<float>();
    const cudaChannelFormatDesc fmt[kPlanes] = {f4, f4, f4, f4, f1, f1, f2, f1, f4};
    for (int k = 0; k < kPlanes; ++k) {
        Plane& p = planes_[k];
        if (cudaMallocArray(&p.arr, &fmt[k], size_t(width_), size_t(height_), cudaArraySurfaceLoadStore) != cudaSuccess) {
            cudaGetLastError();
            freePlanes();
            dlssError = "DLSS icin bellek yok";
            return false;
        }
        cudaResourceDesc rd{};
        rd.resType = cudaResourceTypeArray;
        rd.res.array.array = p.arr;
        cudaTextureDesc td{};
        td.addressMode[0] = td.addressMode[1] = cudaAddressModeClamp;
        td.filterMode = cudaFilterModePoint;
        td.readMode = cudaReadModeElementType;
        CUDA_CHECK(cudaCreateTextureObject(&p.tex, &rd, &td, nullptr));
        CUDA_CHECK(cudaCreateSurfaceObject(&p.surf, &rd));
    }
    if (!dlss_.init(width_, height_, stream_)) {
        dlssError = dlss_.error;
        freePlanes();
        return false;
    }
    dlssError.clear();
    params.gColor = planes_[0].surf;
    params.gAlbedo = planes_[1].surf;
    params.gSpecular = planes_[2].surf;
    params.gNormal = planes_[3].surf;
    params.gRough = planes_[4].surf;
    params.gDepth = planes_[5].surf;
    params.gMotion = planes_[6].surf;
    params.gHitT = planes_[7].surf;
    params.dlss = 1;
    params.lodBias = -1.0f;
    dlssOn_ = true;
    return true;
}

void AtesRenderer::launch(CUdeviceptr rg, unsigned int w, unsigned int h) {
    sbt_.raygenRecord = rg;
    CUDA_CHECK(cudaMemcpyAsync(dParams_, &params, sizeof(AtesParams), cudaMemcpyHostToDevice, stream_));
    OPTIX_CHECK(optixLaunch(pipeline_, stream_, CUdeviceptr(dParams_), sizeof(AtesParams), &sbt_, w, h, 1));
}

void AtesRenderer::render() {
    if (iasDirty_) buildIas();
    if (params.lightI && lightN_ > 0) {
        atesLightCdf(params.lightI, lightN_, lum_, cdf_, scanTmp_, scanBytes_, stream_);
        if (params.smokeExt > 0.0f)
            atesSmokeLight(params.lightI, params.lnx, params.lny, params.lnz, params.lightOrigin, params.lightCell, smokeList_, smokeCount_, kSmokeCap, params.snx,
                           params.sny, params.snz, params.smokeCell, smokeLight_, stream_);
    }
    CUDA_CHECK(cudaEventRecord(ev0_, stream_));
    launch(rgRender_, params.width, params.height);
    CUDA_CHECK(cudaEventRecord(evD_, stream_));
    dlssMs = 0.0f;
    if (dlssOn_ && params.dlss) {
        OrmanDlssFrame& f = dlssFrame;
        f.color = planes_[0].tex;
        f.albedo = planes_[1].tex;
        f.specular = planes_[2].tex;
        f.normal = planes_[3].tex;
        f.rough = planes_[4].tex;
        f.depth = planes_[5].tex;
        f.motion = planes_[6].tex;
        f.hitT = planes_[7].tex;
        f.output = planes_[8].surf;
        CUDA_CHECK(cudaStreamSynchronize(stream_));
        const bool ok = dlss_.evaluate(f);
        CUDA_CHECK(cudaDeviceSynchronize());
        if (ok)
            atesFromDlss(planes_[8].surf, params.hdr, width_, height_, 1.0f / params.preExposure, params.badCount + 4, stream_);
        else
            dlssError = dlss_.error;
        f.reset = false;
    }
    CUDA_CHECK(cudaEventRecord(ev1_, stream_));
    // the fire over the surfaces (the heat haze reads around in a copy of them)
    CUDA_CHECK(cudaMemcpyAsync(params.surf, params.hdr, size_t(width_) * height_ * sizeof(float4), cudaMemcpyDeviceToDevice, stream_));
    launch(rgVolume_, params.width, params.height);
    if (overlay) overlay(stream_);
    CUDA_CHECK(cudaEventRecord(ev2_, stream_));
    if (params.bloomStrength > 0.0f) {
        launchBloom(params.hdr, width_, height_, levels_, accum_, kLevels, stream_);
        params.bloom = accum_[0];
    } else {
        params.bloom = nullptr;
    }
    launch(rgFinal_, params.width, params.height);
    CUDA_CHECK(cudaEventRecord(ev3_, stream_));
    CUDA_CHECK(cudaEventSynchronize(ev3_));
    cudaEventElapsedTime(&renderMs, ev0_, ev1_);
    if (dlssOn_ && params.dlss) cudaEventElapsedTime(&dlssMs, evD_, ev1_);
    cudaEventElapsedTime(&volumeMs, ev1_, ev2_);
    cudaEventElapsedTime(&displayMs, ev2_, ev3_);
    cudaMemcpy(badKinds, params.badCount, sizeof(badKinds), cudaMemcpyDeviceToHost);
    cudaMemsetAsync(params.badCount, 0, sizeof(badKinds), stream_);
    badSamples = badKinds[0] + badKinds[1] + badKinds[2] + badKinds[3] + badKinds[4];
    // DLSS keeps what is not finite in its history and moves it with the picture (a black block that follows the
    // fire): drop the history when its output had any
    if (badKinds[4]) dlssFrame.reset = true;
}

void AtesRenderer::redoVolume() {
    launch(rgVolume_, params.width, params.height);
    CUDA_CHECK(cudaStreamSynchronize(stream_));
}

void AtesRenderer::lumSamples(int gx, int gy, std::vector<float>& Y) {
    const int n = 2 * gx * gy;
    if (n > lumN_) {
        if (lumS_) cudaFree(lumS_);
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&lumS_), n * sizeof(float)));
        lumN_ = n;
    }
    atesSampleLum(params.hdr, width_, height_, gx, gy, lumS_, stream_);
    Y.resize(size_t(n));
    CUDA_CHECK(cudaMemcpyAsync(Y.data(), lumS_, n * sizeof(float), cudaMemcpyDeviceToHost, stream_));
    CUDA_CHECK(cudaStreamSynchronize(stream_));
}

void AtesRenderer::download(std::vector<unsigned char>& rgba) const {
    rgba.resize(size_t(width_) * height_ * 4);
    CUDA_CHECK(cudaMemcpy(rgba.data(), params.output, rgba.size(), cudaMemcpyDeviceToHost));
}

std::vector<float4> AtesRenderer::hdrHost() const {
    std::vector<float4> h(size_t(width_) * height_);
    CUDA_CHECK(cudaMemcpy(h.data(), params.hdr, h.size() * sizeof(float4), cudaMemcpyDeviceToHost));
    return h;
}

void AtesRenderer::release() {
    if (context_) cudaDeviceSynchronize();
    dlss_.release();
    freePlanes();
    dlssOn_ = false;
    params.dlss = 0;
    for (void* p : static_) cudaFree(p);
    static_.clear();
    for (auto& l : logGas_)
        for (void* p : l.bufs) cudaFree(p);
    logGas_.clear();
    for (void* p : top_) cudaFree(p);
    top_.clear();
    for (void** p : {reinterpret_cast<void**>(&dGeoms_), reinterpret_cast<void**>(&dInst_), reinterpret_cast<void**>(&dLogs_),
                     reinterpret_cast<void**>(&dMats_), reinterpret_cast<void**>(&lum_), reinterpret_cast<void**>(&cdf_), &scanTmp_,
                     reinterpret_cast<void**>(&tables_), reinterpret_cast<void**>(&lumS_), reinterpret_cast<void**>(&smokeList_),
                     reinterpret_cast<void**>(&smokeCount_), reinterpret_cast<void**>(&smokeLight_)}) {
        if (*p) cudaFree(*p);
        *p = nullptr;
    }
    dGeomsCap_ = dInstCap_ = dLogsCap_ = 0;
    lumN_ = 0;
    scanBytes_ = 0;
    lightN_ = 0;
    smokeN_ = 0;
    for (int k = 0; k < kLevels; ++k) {
        if (levels_[k]) cudaFree(levels_[k]);
        if (accum_[k]) cudaFree(accum_[k]);
        levels_[k] = accum_[k] = nullptr;
    }
    for (void** p : {reinterpret_cast<void**>(&params.badCount), reinterpret_cast<void**>(&params.hdr), reinterpret_cast<void**>(&params.surf),
                     reinterpret_cast<void**>(&params.depth), reinterpret_cast<void**>(&params.output)}) {
        if (*p) cudaFree(*p);
        *p = nullptr;
    }
    for (CUdeviceptr* p : {&rgRender_, &rgVolume_, &rgFinal_, &miss_, &hits_})
        if (*p) {
            cudaFree(reinterpret_cast<void*>(*p));
            *p = 0;
        }
    if (dParams_) cudaFree(dParams_);
    dParams_ = nullptr;
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (auto g : groups_) optixProgramGroupDestroy(g);
    groups_.clear();
    if (module_) optixModuleDestroy(module_);
    if (context_) optixDeviceContextDestroy(context_);
    pipeline_ = nullptr;
    module_ = nullptr;
    context_ = nullptr;
    for (cudaEvent_t* e : {&ev0_, &ev1_, &ev2_, &ev3_, &evD_})
        if (*e) {
            cudaEventDestroy(*e);
            *e = nullptr;
        }
    if (stream_) cudaStreamDestroy(stream_);
    stream_ = nullptr;
}
