#include "scenes/orman/orman_renderer.h"

#include "core/bloom.h"

#include <nvrtc.h>
#include <optix_stubs.h>

#include <algorithm>
#include <chrono>
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
template <class T> T* toDevice(const std::vector<T>& v, std::vector<void*>& owned, size_t* bytes = nullptr) {
    if (v.empty()) return nullptr;
    T* p = nullptr;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&p), v.size() * sizeof(T)));
    CUDA_CHECK(cudaMemcpy(p, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    owned.push_back(p);
    if (bytes) *bytes += v.size() * sizeof(T);
    return p;
}
double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}   // namespace

bool OrmanRenderer::init(int width, int height, const std::string& root) {
    root_ = root;
    width_ = width;
    height_ = height;
    cudaDeviceProp prop{};
    CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
    deviceName = prop.name;
    CUDA_CHECK(cudaSetDevice(0));
    CUDA_CHECK(cudaFree(nullptr));
    CUDA_CHECK(cudaStreamCreateWithFlags(reinterpret_cast<cudaStream_t*>(&stream_), cudaStreamNonBlocking));
    CUDA_CHECK(cudaEventCreate(&ev0_));
    CUDA_CHECK(cudaEventCreate(&ev1_));
    CUDA_CHECK(cudaEventCreate(&ev2_));
    CUDA_CHECK(cudaEventCreate(&evD_));
    OPTIX_CHECK(optixInit());
    OptixDeviceContextOptions o{};
    o.logCallbackFunction = optixLog;
    o.logCallbackLevel = 4;
    OPTIX_CHECK(optixDeviceContextCreate(nullptr, &o, &context_));
    params = {};
    params.layout = unsigned(sizeof(OrmanParams));
    params.width = unsigned(width);
    params.height = unsigned(height);
    const size_t n = size_t(width) * height;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&params.hdr), n * sizeof(float4)));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&params.output), n * sizeof(uchar4)));
    CUDA_CHECK(cudaMemset(params.output, 0, n * sizeof(uchar4)));
    int lw = width / 2, lh = height / 2;
    for (int k = 0; k < kLevels; ++k) {
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&levels_[k]), size_t(std::max(lw, 1)) * std::max(lh, 1) * sizeof(float4)));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&accum_[k]), size_t(std::max(lw, 1)) * std::max(lh, 1) * sizeof(float4)));
        lw /= 2;
        lh /= 2;
    }
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dParams_), sizeof(OrmanParams)));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&params.badCount), sizeof(unsigned int)));
    CUDA_CHECK(cudaMemset(params.badCount, 0, sizeof(unsigned int)));
    setPattern(4, 0.42f);
    return true;
}

// the pixel's samples drawn from its reconstruction filter (a Gaussian, sigma in pixels) through the inverse of its
// distribution, one axis at a time, on a fixed stratified pattern: every sample weighs the same, nothing is stored
// between pixels, the same every frame
void OrmanRenderer::setPattern(int spp, float sigma) {
    spp = std::clamp(spp, 1, ORMAN_MAX_SPP);
    static const float2 p4[4] = {{0.375f, 0.125f}, {0.875f, 0.375f}, {0.125f, 0.625f}, {0.625f, 0.875f}};
    static const float2 p8[8] = {{0.5625f, 0.3125f}, {0.4375f, 0.6875f}, {0.8125f, 0.5625f}, {0.3125f, 0.1875f},
                                 {0.1875f, 0.8125f}, {0.0625f, 0.4375f}, {0.6875f, 0.9375f}, {0.9375f, 0.0625f}};
    static const float2 p16[16] = {{0.5625f, 0.5625f}, {0.4375f, 0.3125f}, {0.3125f, 0.625f}, {0.75f, 0.4375f},
                                   {0.1875f, 0.375f}, {0.625f, 0.8125f}, {0.8125f, 0.6875f}, {0.6875f, 0.1875f},
                                   {0.375f, 0.875f}, {0.5f, 0.0625f}, {0.25f, 0.125f}, {0.125f, 0.75f},
                                   {0.0f, 0.5f}, {0.9375f, 0.25f}, {0.875f, 0.9375f}, {0.0625f, 0.0f}};
    const float2* src = spp >= 16 ? p16 : spp >= 8 ? p8 : spp >= 4 ? p4 : nullptr;
    const int n = spp >= 16 ? 16 : spp >= 8 ? 8 : spp >= 4 ? 4 : 1;
    // the inverse normal distribution (Acklam's rational approximation)
    auto probit = [](float p) {
        p = std::clamp(p, 1e-4f, 1.0f - 1e-4f);
        const double a[] = {-39.69683028665376, 220.9460984245205, -275.9285104469687, 138.3577518672690, -30.66479806614716, 2.506628277459239};
        const double b[] = {-54.47609879822406, 161.5858368580409, -155.6989798598866, 66.80131188771972, -13.28068155288572};
        const double c[] = {-0.007784894002430293, -0.3223964580411365, -2.400758277161838, -2.549732539343734, 4.374664141464968, 2.938163982698783};
        const double d[] = {0.007784695709041462, 0.3224671290700398, 2.445134137142996, 3.754408661907416};
        const double q0 = p, pl = 0.02425;
        double x;
        if (q0 < pl) {
            const double q = std::sqrt(-2 * std::log(q0));
            x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
        } else if (q0 <= 1 - pl) {
            const double q = q0 - 0.5, r = q * q;
            x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
        } else {
            const double q = std::sqrt(-2 * std::log(1 - q0));
            x = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
        }
        return float(x);
    };
    params.spp = n;
    params.covBias = 0.5f * std::log2(float(n)) + 0.6f;
    for (int k = 0; k < n; ++k) {
        // a stratum's centre (the patterns' points sit on a 1/16 lattice: nudged half a step inwards)
        const float2 u = src ? make_float2(src[k].x + 1.0f / 32.0f, src[k].y + 1.0f / 32.0f) : make_float2(0.5f, 0.5f);
        params.pattern[k] = n == 1 ? make_float2(0, 0) : make_float2(std::clamp(probit(u.x) * sigma, -1.5f, 1.5f), std::clamp(probit(u.y) * sigma, -1.5f, 1.5f));
        // the sun's disk: the same pattern on the unit disk (concentric map)
        const float a = 2.0f * u.x - 1.0f, b = 2.0f * u.y - 1.0f;
        float r, phi;
        if (std::fabs(a) > std::fabs(b)) {
            r = a;
            phi = 0.785398f * (b / a);
        } else {
            r = b;
            phi = std::fabs(b) > 0 ? 1.570796f - 0.785398f * (a / b) : 0.0f;
        }
        params.sunDisk[k] = make_float2(r * std::cos(phi), r * std::sin(phi));
    }
}

bool OrmanRenderer::compile(std::string& log) {
    const std::string srcPath = root_ + "/src/scenes/orman/device/orman.cu";
    const std::string src = readText(srcPath);
    if (src.empty()) {
        log = "cannot read " + srcPath;
        return false;
    }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "orman.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) {
        log = "nvrtcCreateProgram failed";
        return false;
    }
    const std::string inc[] = {"-I" + root_ + "/src", "-I" + root_ + "/third_party/optix/include", "-I" + root_ + "/third_party/cuda/include"};
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
    // two levels: the tile's copies are instances of the tile's own IAS (measured: about a tenth more a frame than one
    // level, the same picture)
    pco.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_ANY;
    pco.numPayloadValues = 4;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "params";
    pco.pipelineLaunchParamsSizeInBytes = sizeof(OrmanParams);
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
    char olog[8192];
    size_t olen = sizeof(olog);
    OptixModule module = nullptr;
    OptixResult r = optixModuleCreate(context_, &mco, &pco, ir.data(), ir.size(), olog, &olen, &module);
    if (r != OPTIX_SUCCESS) {
        log = std::string("optixModuleCreate: ") + optixGetErrorString(r) + "\n" + olog;
        return false;
    }
    const char* rg[5] = {"__raygen__render", "__raygen__final", "__raygen__bake", "__raygen__bakeAO", "__raygen__bakeVis"};
    std::vector<OptixProgramGroupDesc> d(7);
    for (auto& x : d) std::memset(&x, 0, sizeof(x));
    for (int i = 0; i < 5; ++i) {
        d[i].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        d[i].raygen.module = module;
        d[i].raygen.entryFunctionName = rg[i];
    }
    d[5].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;   // traversal only: no programs
    d[6].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    d[6].hitgroup.moduleAH = module;
    d[6].hitgroup.entryFunctionNameAH = "__anyhit__alpha";
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
    OPTIX_CHECK(optixPipelineSetStackSizeFromCallDepths(pipeline, 1, 0, 0, 0, 3));
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
    record(groups_[1], rgFinal_, 1);
    record(groups_[2], rgBake_, 1);
    record(groups_[3], rgBakeAO_, 1);
    record(groups_[4], rgBakeVis_, 1);
    record(groups_[5], miss_, 1);
    record(groups_[6], hits_, ORMAN_MAX_INPUTS);   // a prototype's build inputs: the same any-hit for each
    sbt_ = {};
    sbt_.missRecordBase = miss_;
    sbt_.missRecordStrideInBytes = sizeof(Record);
    sbt_.missRecordCount = 1;
    sbt_.hitgroupRecordBase = hits_;
    sbt_.hitgroupRecordStrideInBytes = sizeof(Record);
    sbt_.hitgroupRecordCount = ORMAN_MAX_INPUTS;
    log = nvlog;
    return true;
}

void OrmanRenderer::upload(const OrmanAssets& assets, const std::vector<OProtoCPU>& extra, const std::vector<OPlace>& places, const OTile& tile) {
    CUDA_CHECK(cudaDeviceSynchronize());
    for (void* p : world_) cudaFree(p);
    world_.clear();
    for (void* p : top_) cudaFree(p);
    top_.clear();
    gasBytes = iasBytes = geomBytes = 0;
    geomsHost_.clear();
    protosHost_.clear();
    geomVerts_.clear();
    gas_.clear();
    std::vector<const OProtoCPU*> all;
    for (const auto& p : assets.protos) all.push_back(&p);
    for (const auto& p : extra) all.push_back(&p);
    std::vector<unsigned long long> protoTris(all.size(), 0);
    for (size_t pi = 0; pi < all.size(); ++pi) {
        const OProtoCPU& P = *all[pi];
        OProto pr{unsigned(geomsHost_.size()), unsigned(P.geoms.size())};
        protosHost_.push_back(pr);
        std::vector<OptixBuildInput> inputs(P.geoms.size());
        std::vector<CUdeviceptr> vbufs(P.geoms.size());
        std::vector<unsigned int> flags(P.geoms.size());
        for (size_t gi = 0; gi < P.geoms.size(); ++gi) {
            const OMeshCPU& m = P.geoms[gi];
            OGeom g{};
            g.pos = toDevice(m.pos, world_, &geomBytes);
            g.nrm = toDevice(m.nrm, world_, &geomBytes);
            g.uv = toDevice(m.uv, world_, &geomBytes);
            g.idx = toDevice(m.idx, world_, &geomBytes);
            g.ao = toDevice(m.ao, world_, &geomBytes);
            g.material = m.material;
            g.uvScale = m.uvScale;
            geomsHost_.push_back(g);
            geomVerts_.push_back(unsigned(m.pos.size()));
            protoTris[pi] += m.idx.size();
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
            const bool alpha = m.material >= 0 && size_t(m.material) < assets.materials.size() && (assets.materials[m.material].flags & OMAT_ALPHA);
            // (a shadow ray multiplies the light each leaf lets through: one call per leaf, or it would count twice)
            flags[gi] = alpha ? OPTIX_GEOMETRY_FLAG_REQUIRE_SINGLE_ANYHIT_CALL : OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT;
            bi.triangleArray.flags = &flags[gi];
            bi.triangleArray.numSbtRecords = 1;
        }
        // built, then compacted (the forest's copies share these: the less each takes the more kinds fit)
        OptixAccelBuildOptions ao{};
        ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE | OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
        ao.operation = OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes sizes{};
        OPTIX_CHECK(optixAccelComputeMemoryUsage(context_, &ao, inputs.data(), unsigned(inputs.size()), &sizes));
        void *temp = nullptr, *out = nullptr;
        size_t* compSize = nullptr;
        CUDA_CHECK(cudaMalloc(&temp, sizes.tempSizeInBytes));
        CUDA_CHECK(cudaMalloc(&out, sizes.outputSizeInBytes));
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&compSize), sizeof(size_t)));
        OptixAccelEmitDesc emit{};
        emit.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
        emit.result = CUdeviceptr(compSize);
        OptixTraversableHandle h = 0;
        OPTIX_CHECK(optixAccelBuild(context_, nullptr, &ao, inputs.data(), unsigned(inputs.size()), CUdeviceptr(temp), sizes.tempSizeInBytes, CUdeviceptr(out),
                                    sizes.outputSizeInBytes, &h, &emit, 1));
        CUDA_CHECK(cudaDeviceSynchronize());
        size_t cs = 0;
        CUDA_CHECK(cudaMemcpy(&cs, compSize, sizeof(size_t), cudaMemcpyDeviceToHost));
        cudaFree(temp);
        cudaFree(compSize);
        if (cs < sizes.outputSizeInBytes) {
            void* small = nullptr;
            CUDA_CHECK(cudaMalloc(&small, cs));
            OPTIX_CHECK(optixAccelCompact(context_, nullptr, h, CUdeviceptr(small), cs, &h));
            CUDA_CHECK(cudaDeviceSynchronize());
            cudaFree(out);
            out = small;
            gasBytes += cs;
        } else {
            gasBytes += sizes.outputSizeInBytes;
        }
        world_.push_back(out);
        gas_.push_back(h);
    }
    // every copy's record by its instance id: those placed in the world (the patch alone), then the groups' (the
    // patch, the stream's row, the cells in the sun's way, the tile), which the cells reach through their IAS
    std::vector<OInstance> oinst;
    auto record = [&](const OPlace& pl) {
        OInstance o{};
        o.proto = unsigned(std::max(pl.proto, 0));
        o.scale = pl.scale;
        o.tint = pl.tint;
        oinst.push_back(o);
    };
    auto valid = [&](const OPlace& pl) { return pl.proto >= 0 && size_t(pl.proto) < gas_.size(); };
    auto instance = [&](const OPlace& pl, unsigned id) {
        OptixInstance oi{};
        std::memcpy(oi.transform, pl.m, sizeof(pl.m));
        oi.instanceId = id;
        oi.sbtOffset = 0;
        oi.visibilityMask = pl.mask;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = gas_[size_t(pl.proto)];
        return oi;
    };
    instancedTriangles = 0;
    unsigned long long copies = 0;
    fixed_.clear();
    cells_.clear();
    for (const OPlace& pl : places) {
        if (!valid(pl)) continue;
        fixed_.push_back(instance(pl, unsigned(oinst.size())));
        record(pl);
        instancedTriangles += protoTris[size_t(pl.proto)];
        ++copies;
    }
    const unsigned tileFirst = unsigned(oinst.size());
    for (const OPlace& pl : tile.places) record(pl);
    iasBytes = 0;
    std::vector<OptixTraversableHandle> ways;
    std::vector<unsigned long long> wayTris, wayCopies;
    for (const auto& way : tile.variants) {
        std::vector<OptixInstance> vi;
        unsigned long long tris = 0;
        for (unsigned k : way) {
            const OPlace& pl = tile.places[k];
            if (!valid(pl)) continue;
            vi.push_back(instance(pl, tileFirst + k));
            tris += protoTris[size_t(pl.proto)];
        }
        ways.push_back(buildIas(vi, world_, iasBytes));
        wayTris.push_back(tris);
        wayCopies.push_back(vi.size());
    }
    for (const OTile::Cell& c : tile.cells) {
        if (c.variant < 0 || size_t(c.variant) >= ways.size()) continue;
        cells_.push_back({ways[size_t(c.variant)], c.offset, c.i, c.j});
        instancedTriangles += wayTris[size_t(c.variant)];
        copies += wayCopies[size_t(c.variant)];
    }
    torusN_ = tile.n;
    torusSide_ = tile.side;
    params.period = tile.n > 0 ? float(tile.n) * tile.side : 0.0f;
    instanceCount = unsigned(copies);
    params.instances = toDevice(oinst, world_);
    params.geoms = toDevice(geomsHost_, world_);
    params.protos = toDevice(protosHost_, world_);
    params.materials = toDevice(assets.materials, world_);
    topBytes_ = 0;
    placeWindow(0, 0);
}

void OrmanRenderer::placeWindow(int ci, int cj) {
    CUDA_CHECK(cudaDeviceSynchronize());
    for (void* p : top_) cudaFree(p);
    top_.clear();
    iasBytes -= topBytes_;
    topBytes_ = 0;
    windowI_ = ci;
    windowJ_ = cj;
    // cell c's copies within `reach` cells of the eye's cell e: the torus sides each is moved by (one each way when the
    // reach is half the torus; a wider reach brings some twice)
    const int reach = torusN_ > 0 ? std::max(windowReach, torusN_ / 2) : 0;
    auto turns = [&](int c, int e) {
        if (torusN_ <= 0) return std::vector<int>{0};
        std::vector<int> t;
        const int lo = int(std::floor(float(e - reach - c) / float(torusN_)));
        for (int k = lo; c + k * torusN_ <= e + reach; ++k)
            if (c + k * torusN_ >= e - reach) t.push_back(k);
        return t;
    };
    const float W = float(torusN_) * torusSide_;
    std::vector<OptixInstance> top = fixed_;
    for (const CellIas& c : cells_)
        for (int tx : turns(c.i, ci))
            for (int tz : turns(c.j, cj)) {
                OptixInstance oi{};
                oi.transform[0] = oi.transform[5] = oi.transform[10] = 1.0f;
                oi.transform[3] = c.offset.x + float(tx) * W;
                oi.transform[7] = c.offset.y;
                oi.transform[11] = c.offset.z + float(tz) * W;
                oi.visibilityMask = 0xFFu;   // (the group's own instances carry the masks)
                oi.traversableHandle = c.handle;
                top.push_back(oi);
            }
    topInstances = unsigned(top.size());
    params.handle = buildIas(top, top_, topBytes_);
    iasBytes += topBytes_;
}

OptixTraversableHandle OrmanRenderer::buildIas(const std::vector<OptixInstance>& inst, std::vector<void*>& keep, size_t& bytes) {
    OptixInstance* dInst = toDevice(inst, keep);
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = CUdeviceptr(dInst);
    bi.instanceArray.numInstances = unsigned(inst.size());
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sizes{};
    OPTIX_CHECK(optixAccelComputeMemoryUsage(context_, &ao, &bi, 1, &sizes));
    void *temp = nullptr, *out = nullptr;
    CUDA_CHECK(cudaMalloc(&temp, sizes.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(&out, sizes.outputSizeInBytes));
    OptixTraversableHandle h = 0;
    OPTIX_CHECK(optixAccelBuild(context_, nullptr, &ao, &bi, 1, CUdeviceptr(temp), sizes.tempSizeInBytes, CUdeviceptr(out), sizes.outputSizeInBytes, &h,
                                nullptr, 0));
    CUDA_CHECK(cudaDeviceSynchronize());
    cudaFree(temp);
    keep.push_back(out);
    bytes += sizes.outputSizeInBytes;
    return h;
}

void OrmanRenderer::launch(CUdeviceptr rg, unsigned int w, unsigned int h, unsigned int d) {
    CUDA_CHECK(cudaMemcpyAsync(dParams_, &params, sizeof(OrmanParams), cudaMemcpyHostToDevice, cudaStream_t(stream_)));
    OptixShaderBindingTable s = sbt_;
    s.raygenRecord = rg;
    OPTIX_CHECK(optixLaunch(pipeline_, stream_, CUdeviceptr(dParams_), sizeof(OrmanParams), &s, w, h, d));
}

void OrmanRenderer::bakeGroundAO(int proto, float3 at, float radius, int rays, OBakeStats& st) {
    const double t0 = nowMs();
    const OProto& pr = protosHost_[size_t(proto)];
    params.aoRadius = radius;
    params.aoRays = rays;
    params.aoOffset = at;
    // every piece of it (the litter: one per leaf set)
    for (unsigned int gi = pr.firstGeom; gi < pr.firstGeom + pr.geomCount; ++gi) {
        const OGeom& g = geomsHost_[gi];
        const unsigned int n = geomVerts_[gi];
        if (!g.ao || !n) continue;
        for (unsigned int first = 0; first < n; first += 65536u * 64u) {
            const unsigned int count = std::min(n - first, 65536u * 64u);
            params.aoPos = g.pos + first;
            params.aoNrm = g.nrm + first;
            params.aoOut = const_cast<float*>(g.ao) + first;
            params.aoCount = count;
            launch(rgBakeAO_, 65536u, (count + 65535u) / 65536u, 1);
            CUDA_CHECK(cudaStreamSynchronize(cudaStream_t(stream_)));
        }
    }
    st.aoMs += nowMs() - t0;
}

void OrmanRenderer::bakeGrid(bool tile, float3 lo, float3 hi, float h, float3 at, int rays, int passes, OBakeStats& st) {
    const double t0 = nowMs();
    std::vector<void*>& owned = tile ? tileGridBufs_ : gridBufs_;
    OGrid& target = tile ? params.tileGrid : params.grid;
    for (void* p : owned) cudaFree(p);
    owned.clear();
    target = OGrid{};
    // the tile first: while it bakes the patch has no grid, so everything reads the tile's (its probes see its copies)
    if (tile) {
        for (void* p : gridBufs_) cudaFree(p);
        gridBufs_.clear();
        params.grid = OGrid{};
    }
    OGrid G{};
    G.origin = lo;
    G.wrap = tile ? 1 : 0;
    if (tile) {
        // whole probes to its side: the last one's neighbour is the first of the next copy
        G.nx = std::max(1, int(std::round((hi.x - lo.x) / h)));
        G.nz = std::max(1, int(std::round((hi.z - lo.z) / h)));
        G.h = (hi.x - lo.x) / G.nx;
    } else {
        G.h = h;
        G.nx = int(std::ceil((hi.x - lo.x) / h)) + 1;
        G.nz = int(std::ceil((hi.z - lo.z) / h)) + 1;
    }
    G.ny = int(std::ceil((hi.y - lo.y) / G.h)) + 1;
    const size_t n = size_t(G.nx) * G.ny * G.nz;
    float4* bufs[2][4] = {};
    for (int b = 0; b < 2; ++b)
        for (int c = 0; c < 4; ++c) {
            CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&bufs[b][c]), n * sizeof(float4)));
            CUDA_CHECK(cudaMemset(bufs[b][c], 0, n * sizeof(float4)));
            owned.push_back(bufs[b][c]);
        }
    (tile ? tileGridBytes : gridBytes) = n * sizeof(float4) * 4;
    params.bakeRays = rays;
    params.bakeGrid = G;
    params.bakeGrid.origin = lo + at;
    int read = -1;
    for (int pass = 0; pass < passes; ++pass) {
        const int write = read == 0 ? 1 : 0;
        OGrid R = G;
        for (int c = 0; c < 4; ++c) R.sh[c] = read >= 0 ? bufs[read][c] : nullptr;
        target = R;
        params.bakePass = pass;
        for (int c = 0; c < 4; ++c) params.bakeOut[c] = bufs[write][c];
        // slabs small enough for the display driver's watchdog (2 s): about 64 million rays a launch
        const int zn = std::max(1, int(6.4e7 / (double(G.nx) * G.ny * rays)));
        for (int z0 = 0; z0 < G.nz; z0 += zn) {
            params.bakeZ0 = z0;
            params.bakeZn = std::min(zn, G.nz - z0);
            launch(rgBake_, unsigned(G.nx), unsigned(G.ny), unsigned(params.bakeZn));
            CUDA_CHECK(cudaStreamSynchronize(cudaStream_t(stream_)));
        }
        read = write;
    }
    // the second buffer set is no longer needed: keep the last written, free the other
    for (int c = 0; c < 4; ++c) G.sh[c] = bufs[read][c];
    const int other = read == 0 ? 1 : 0;
    for (int c = 0; c < 4; ++c) {
        cudaFree(bufs[other][c]);
        owned.erase(std::remove(owned.begin(), owned.end(), static_cast<void*>(bufs[other][c])), owned.end());
    }
    target = G;
    std::vector<float4> s0(n);
    CUDA_CHECK(cudaMemcpy(s0.data(), G.sh[0], n * sizeof(float4), cudaMemcpyDeviceToHost));
    size_t valid = 0;
    for (const float4& v : s0)
        if (v.w > 0.5f) ++valid;
    st.probes += n;
    st.valid += valid;
    st.gridMs += nowMs() - t0;
}

void OrmanRenderer::bakeSunVis(bool tile, float3 lo, float3 hi, float h, float3 at, OBakeStats& st) {
    const auto t0 = std::chrono::steady_clock::now();
    unsigned long long& texOut = tile ? params.tileVis : params.sunVis;
    cudaArray_t& arr = tile ? tileVisArr_ : visArr_;
    if (texOut) cudaDestroyTextureObject(cudaTextureObject_t(texOut));
    if (arr) cudaFreeArray(arr);
    texOut = 0;
    arr = nullptr;
    // the tile's: whole voxels to its side (it wraps)
    if (tile) h = (hi.x - lo.x) / std::max(1.0f, std::round((hi.x - lo.x) / h));
    const int nx = int(std::ceil((hi.x - lo.x) / h - 1e-3f)), ny = int(std::ceil((hi.y - lo.y) / h)), nz = int(std::ceil((hi.z - lo.z) / h - 1e-3f));
    const size_t n = size_t(nx) * ny * nz;
    unsigned char* buf = nullptr;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&buf), n));
    params.visOut = buf;
    params.visN[0] = nx;
    params.visN[1] = ny;
    params.visN[2] = nz;
    params.visH = h;
    params.visOrigin = lo + at;
    const int zn = std::max(1, int(1.6e7 / (double(nx) * ny * 4.0)));
    for (int z0 = 0; z0 < nz; z0 += zn) {
        params.visZ0 = z0;
        launch(rgBakeVis_, unsigned(nx), unsigned(ny), unsigned(std::min(zn, nz - z0)));
        CUDA_CHECK(cudaStreamSynchronize(cudaStream_t(stream_)));
    }
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<unsigned char>();
    CUDA_CHECK(cudaMalloc3DArray(&arr, &fd, make_cudaExtent(nx, ny, nz)));
    cudaMemcpy3DParms cp{};
    cp.srcPtr = make_cudaPitchedPtr(buf, size_t(nx), size_t(nx), size_t(ny));
    cp.dstArray = arr;
    cp.extent = make_cudaExtent(nx, ny, nz);
    cp.kind = cudaMemcpyDeviceToDevice;
    CUDA_CHECK(cudaMemcpy3D(&cp));
    cudaFree(buf);
    params.visOut = nullptr;
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeArray;
    rd.res.array.array = arr;
    cudaTextureDesc td{};
    td.addressMode[0] = td.addressMode[1] = td.addressMode[2] = cudaAddressModeClamp;
    if (tile) td.addressMode[0] = td.addressMode[2] = cudaAddressModeWrap;
    td.filterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeNormalizedFloat;
    td.normalizedCoords = 1;
    cudaTextureObject_t tex = 0;
    CUDA_CHECK(cudaCreateTextureObject(&tex, &rd, &td, nullptr));
    texOut = tex;
    const float3 inv = make_float3(1.0f / (nx * h), 1.0f / (ny * h), 1.0f / (nz * h));
    if (tile) {
        params.tileVisOrigin = lo;
        params.tileVisInvSize = inv;
    } else {
        params.sunVisOrigin = lo;
        params.sunVisInvSize = inv;
    }
    visBytes += n;
    st.visMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void OrmanRenderer::freePlanes() {
    for (Plane& p : planes_) {
        if (p.surf) cudaDestroySurfaceObject(p.surf);
        if (p.tex) cudaDestroyTextureObject(p.tex);
        if (p.arr) cudaFreeArray(p.arr);
        p = Plane();
    }
}

bool OrmanRenderer::enableDlss(bool on) {
    if (on == dlssOn_) return true;
    CUDA_CHECK(cudaStreamSynchronize(cudaStream_t(stream_)));
    dlssOn_ = false;
    params.dlss = 0;
    params.lodBias = 0.0f;
    dlss_.release();
    freePlanes();
    if (!on) return true;
    // the planes as DLSS reads them: CUDA arrays behind a point-sampled texture (inputs) and a surface (written)
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
        td.normalizedCoords = 0;
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
    // textures one level finer than the pixel (the guide's DLAA bias): the frames gather the detail
    params.lodBias = -1.0f;
    dlssOn_ = true;
    return true;
}

void OrmanRenderer::render() {
    CUDA_CHECK(cudaEventRecord(ev0_, cudaStream_t(stream_)));
    launch(rgRender_, params.width, params.height, 1);
    CUDA_CHECK(cudaEventRecord(evD_, cudaStream_t(stream_)));
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
        // NGX does not run in our stream's order: the guides finished before it reads them, its picture before the copy
        CUDA_CHECK(cudaStreamSynchronize(cudaStream_t(stream_)));
        const bool ok = dlss_.evaluate(f);
        CUDA_CHECK(cudaDeviceSynchronize());
        if (ok) {
            // its picture where the fixed samples' would be: the glare and the display read it from there
            CUDA_CHECK(cudaMemcpy2DFromArrayAsync(params.hdr, size_t(width_) * sizeof(float4), planes_[8].arr, 0, 0, size_t(width_) * sizeof(float4),
                                                  size_t(height_), cudaMemcpyDeviceToDevice, cudaStream_t(stream_)));
        } else {
            dlssError = dlss_.error;
        }
        f.reset = false;
    }
    CUDA_CHECK(cudaEventRecord(ev1_, cudaStream_t(stream_)));
    if (params.bloomStrength > 0.0f) {
        launchBloom(params.hdr, width_, height_, levels_, accum_, kLevels, cudaStream_t(stream_));
        params.bloom = accum_[0];
    } else {
        params.bloom = nullptr;
    }
    launch(rgFinal_, params.width, params.height, 1);
    CUDA_CHECK(cudaEventRecord(ev2_, cudaStream_t(stream_)));
    CUDA_CHECK(cudaEventSynchronize(ev2_));
    cudaEventElapsedTime(&renderMs, ev0_, ev1_);
    if (dlssOn_ && params.dlss) cudaEventElapsedTime(&dlssMs, evD_, ev1_);
    cudaEventElapsedTime(&displayMs, ev1_, ev2_);
    cudaMemcpy(&badSamples, params.badCount, sizeof(unsigned int), cudaMemcpyDeviceToHost);
    cudaMemsetAsync(params.badCount, 0, sizeof(unsigned int), cudaStream_t(stream_));
}

void OrmanRenderer::download(std::vector<unsigned char>& rgba) const {
    rgba.resize(size_t(width_) * height_ * 4);
    CUDA_CHECK(cudaMemcpy(rgba.data(), params.output, rgba.size(), cudaMemcpyDeviceToHost));
}

std::vector<float4> OrmanRenderer::hdrHost() const {
    std::vector<float4> h(size_t(width_) * height_);
    CUDA_CHECK(cudaMemcpy(h.data(), params.hdr, h.size() * sizeof(float4), cudaMemcpyDeviceToHost));
    return h;
}

void OrmanRenderer::release() {
    if (context_) cudaDeviceSynchronize();
    dlss_.release();
    freePlanes();
    dlssOn_ = false;
    params.dlss = 0;
    for (void* p : world_) cudaFree(p);
    world_.clear();
    for (void* p : top_) cudaFree(p);
    top_.clear();
    for (void* p : gridBufs_) cudaFree(p);
    gridBufs_.clear();
    for (void* p : tileGridBufs_) cudaFree(p);
    tileGridBufs_.clear();
    params.grid = params.tileGrid = OGrid{};
    for (unsigned long long* t : {&params.sunVis, &params.tileVis})
        if (*t) cudaDestroyTextureObject(cudaTextureObject_t(*t));
    params.sunVis = params.tileVis = 0;
    for (cudaArray_t* a : {&visArr_, &tileVisArr_})
        if (*a) cudaFreeArray(*a);
    visArr_ = tileVisArr_ = nullptr;
    for (int k = 0; k < kLevels; ++k) {
        if (levels_[k]) cudaFree(levels_[k]);
        if (accum_[k]) cudaFree(accum_[k]);
        levels_[k] = accum_[k] = nullptr;
    }
    if (params.badCount) cudaFree(params.badCount);
    params.badCount = nullptr;
    if (params.hdr) cudaFree(params.hdr);
    if (params.output) cudaFree(params.output);
    params.hdr = nullptr;
    params.output = nullptr;
    for (CUdeviceptr* p : {&rgRender_, &rgFinal_, &rgBake_, &rgBakeAO_, &rgBakeVis_, &miss_, &hits_})
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
    for (cudaEvent_t* e : {&ev0_, &ev1_, &ev2_, &evD_})
        if (*e) {
            cudaEventDestroy(*e);
            *e = nullptr;
        }
    if (stream_) cudaStreamDestroy(cudaStream_t(stream_));
    stream_ = nullptr;
}
