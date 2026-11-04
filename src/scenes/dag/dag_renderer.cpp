#include "scenes/dag/dag_renderer.h"

#include "core/bloom.h"

#include <stb_image.h>

#include <nvrtc.h>
#include <optix_stubs.h>
#undef far
#undef near

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>

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
double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void parallelFor(int n, const std::function<void(int)>& fn, int chunk = 16) {
    std::atomic<int> next{0};
    const int threads = std::max(1, int(std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (;;) {
                const int i0 = next.fetch_add(chunk);
                if (i0 >= n) return;
                for (int i = i0; i < std::min(i0 + chunk, n); ++i) fn(i);
            }
        });
    for (auto& t : pool) t.join();
}

}   // namespace

bool DagRenderer::init(int width, int height, const std::string& root) {
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
    OPTIX_CHECK(optixInit());
    OptixDeviceContextOptions o{};
    o.logCallbackFunction = optixLog;
    o.logCallbackLevel = 4;
    OPTIX_CHECK(optixDeviceContextCreate(nullptr, &o, &context_));
    params = {};
    params.layout = unsigned(sizeof(DagParams));
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
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dParams_), sizeof(DagParams)));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&params.badCount), sizeof(unsigned int)));
    CUDA_CHECK(cudaMemset(params.badCount, 0, sizeof(unsigned int)));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&params.probe), sizeof(float4)));
    CUDA_CHECK(cudaMemset(params.probe, 0, sizeof(float4)));
    setPattern(16, 0.40f);
    return true;
}

void DagRenderer::setPattern(int spp, float sigma) {
    spp = std::clamp(spp, 1, DAG_MAX_SPP);
    static const float2 p4[4] = {{0.375f, 0.125f}, {0.875f, 0.375f}, {0.125f, 0.625f}, {0.625f, 0.875f}};
    static const float2 p8[8] = {{0.5625f, 0.3125f}, {0.4375f, 0.6875f}, {0.8125f, 0.5625f}, {0.3125f, 0.1875f},
                                 {0.1875f, 0.8125f}, {0.0625f, 0.4375f}, {0.6875f, 0.9375f}, {0.9375f, 0.0625f}};
    static const float2 p16[16] = {{0.5625f, 0.5625f}, {0.4375f, 0.3125f}, {0.3125f, 0.625f}, {0.75f, 0.4375f},
                                   {0.1875f, 0.375f}, {0.625f, 0.8125f}, {0.8125f, 0.6875f}, {0.6875f, 0.1875f},
                                   {0.375f, 0.875f}, {0.5f, 0.0625f}, {0.25f, 0.125f}, {0.125f, 0.75f},
                                   {0.0f, 0.5f}, {0.9375f, 0.25f}, {0.875f, 0.9375f}, {0.0625f, 0.0f}};
    const float2* src = spp >= 16 ? p16 : spp >= 8 ? p8 : spp >= 4 ? p4 : nullptr;
    int n = spp >= 16 ? 16 : spp >= 8 ? 8 : spp >= 4 ? 4 : 1;
    // more than the standard patterns: the R2 sequence (fixed, well spread)
    float2 big[DAG_MAX_SPP];
    if (spp > 16) {
        n = spp;
        for (int k = 0; k < n; ++k) {
            const float g = 1.32471795724f;
            const float ax = std::fmod(0.5f + (k + 1) / g, 1.0f), ay = std::fmod(0.5f + (k + 1) / (g * g), 1.0f);
            big[k] = make_float2(ax, ay);
        }
        src = big;
    }
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
    for (int k = 0; k < n; ++k) {
        const float2 u = src ? make_float2(std::fmod(src[k].x + 1.0f / 32.0f, 1.0f), std::fmod(src[k].y + 1.0f / 32.0f, 1.0f)) : make_float2(0.5f, 0.5f);
        params.pattern[k] = n == 1 ? make_float2(0, 0) : make_float2(std::clamp(probit(u.x) * sigma, -1.5f, 1.5f), std::clamp(probit(u.y) * sigma, -1.5f, 1.5f));
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

bool DagRenderer::compile(std::string& log) {
    const std::string srcPath = root_ + "/src/scenes/dag/device/dag.cu";
    const std::string src = readText(srcPath);
    if (src.empty()) {
        log = "cannot read " + srcPath;
        return false;
    }
    nvrtcProgram prog;
    if (nvrtcCreateProgram(&prog, src.c_str(), "dag.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) {
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
    pco.numPayloadValues = 0;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "params";
    pco.pipelineLaunchParamsSizeInBytes = sizeof(DagParams);
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
    char olog[8192];
    size_t olen = sizeof(olog);
    OptixModule module = nullptr;
    OptixResult r = optixModuleCreate(context_, &mco, &pco, ir.data(), ir.size(), olog, &olen, &module);
    if (r != OPTIX_SUCCESS) {
        log = std::string("optixModuleCreate: ") + optixGetErrorString(r) + "\n" + olog;
        return false;
    }
    std::vector<OptixProgramGroupDesc> d(5);
    for (auto& x : d) std::memset(&x, 0, sizeof(x));
    d[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[0].raygen.module = module;
    d[0].raygen.entryFunctionName = "__raygen__render";
    d[1].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[1].raygen.module = module;
    d[1].raygen.entryFunctionName = "__raygen__final";
    d[2].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;   // traversal only: no programs
    d[3].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    d[4].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[4].raygen.module = module;
    d[4].raygen.entryFunctionName = "__raygen__bakeAO";
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
    auto record = [&](OptixProgramGroup g, CUdeviceptr& dst) {
        Record rec;
        OPTIX_CHECK(optixSbtRecordPackHeader(g, &rec));
        if (!dst) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dst), sizeof(Record)));
        CUDA_CHECK(cudaMemcpy(reinterpret_cast<void*>(dst), &rec, sizeof(Record), cudaMemcpyHostToDevice));
    };
    record(groups_[0], rgRender_);
    record(groups_[1], rgFinal_);
    record(groups_[2], miss_);
    record(groups_[3], hits_);
    record(groups_[4], rgBakeAO_);
    sbt_ = {};
    sbt_.missRecordBase = miss_;
    sbt_.missRecordStrideInBytes = sizeof(Record);
    sbt_.missRecordCount = 1;
    sbt_.hitgroupRecordBase = hits_;
    sbt_.hitgroupRecordStrideInBytes = sizeof(Record);
    sbt_.hitgroupRecordCount = 1;
    log = nvlog;
    return true;
}

OptixTraversableHandle DagRenderer::buildGasTriangles(const float3* verts, size_t nVerts, const uint3* idx, size_t nTri) {
    void* vbuf = nullptr;
    void* ibuf = nullptr;
    CUDA_CHECK(cudaMalloc(&vbuf, nVerts * sizeof(float3)));
    CUDA_CHECK(cudaMemcpy(vbuf, verts, nVerts * sizeof(float3), cudaMemcpyHostToDevice));
    if (idx) {
        CUDA_CHECK(cudaMalloc(&ibuf, nTri * sizeof(uint3)));
        CUDA_CHECK(cudaMemcpy(ibuf, idx, nTri * sizeof(uint3), cudaMemcpyHostToDevice));
    }
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    CUdeviceptr vb = CUdeviceptr(vbuf);
    bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
    bi.triangleArray.vertexStrideInBytes = sizeof(float3);
    bi.triangleArray.numVertices = unsigned(nVerts);
    bi.triangleArray.vertexBuffers = &vb;
    if (idx) {
        bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        bi.triangleArray.indexStrideInBytes = sizeof(uint3);
        bi.triangleArray.numIndexTriplets = unsigned(nTri);
        bi.triangleArray.indexBuffer = CUdeviceptr(ibuf);
    }
    unsigned int flags = OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT;
    bi.triangleArray.flags = &flags;
    bi.triangleArray.numSbtRecords = 1;
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE | OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sizes{};
    OPTIX_CHECK(optixAccelComputeMemoryUsage(context_, &ao, &bi, 1, &sizes));
    void *temp = nullptr, *out = nullptr;
    size_t* compSize = nullptr;
    CUDA_CHECK(cudaMalloc(&temp, sizes.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(&out, sizes.outputSizeInBytes));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&compSize), sizeof(size_t)));
    OptixAccelEmitDesc emit{};
    emit.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
    emit.result = CUdeviceptr(compSize);
    OptixTraversableHandle h = 0;
    OPTIX_CHECK(optixAccelBuild(context_, nullptr, &ao, &bi, 1, CUdeviceptr(temp), sizes.tempSizeInBytes, CUdeviceptr(out), sizes.outputSizeInBytes, &h, &emit, 1));
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
    gasBufs_.push_back(out);
    cudaFree(vbuf);
    if (ibuf) cudaFree(ibuf);
    return h;
}

void DagRenderer::buildTop() {
    if (iasBuf_) cudaFree(iasBuf_);
    if (instBuf_) cudaFree(instBuf_);
    CUDA_CHECK(cudaMalloc(&instBuf_, instances_.size() * sizeof(OptixInstance)));
    CUDA_CHECK(cudaMemcpy(instBuf_, instances_.data(), instances_.size() * sizeof(OptixInstance), cudaMemcpyHostToDevice));
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = CUdeviceptr(instBuf_);
    bi.instanceArray.numInstances = unsigned(instances_.size());
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sizes{};
    OPTIX_CHECK(optixAccelComputeMemoryUsage(context_, &ao, &bi, 1, &sizes));
    void* temp = nullptr;
    CUDA_CHECK(cudaMalloc(&temp, sizes.tempSizeInBytes));
    CUDA_CHECK(cudaMalloc(&iasBuf_, sizes.outputSizeInBytes));
    OptixTraversableHandle h = 0;
    OPTIX_CHECK(optixAccelBuild(context_, nullptr, &ao, &bi, 1, CUdeviceptr(temp), sizes.tempSizeInBytes, CUdeviceptr(iasBuf_), sizes.outputSizeInBytes, &h, nullptr, 0));
    CUDA_CHECK(cudaDeviceSynchronize());
    cudaFree(temp);
    params.handle = h;
}

DagRenderer::Tex DagRenderer::makeMip(const void* base, int w, int h, int elemBytes, cudaChannelFormatDesc fd, bool srgb, bool wrapU, bool wrapV,
                                      const std::vector<std::vector<unsigned char>>* levels) {
    Tex t;
    const unsigned count = unsigned(levels->size()) + 1;
    CUDA_CHECK(cudaMallocMipmappedArray(&t.mip, &fd, make_cudaExtent(w, h, 0), count));
    int lw = w, lh = h;
    for (unsigned k = 0; k < count; ++k) {
        cudaArray_t la = nullptr;
        CUDA_CHECK(cudaGetMipmappedArrayLevel(&la, t.mip, k));
        const void* src = k == 0 ? base : (*levels)[k - 1].data();
        CUDA_CHECK(cudaMemcpy2DToArray(la, 0, 0, src, size_t(lw) * elemBytes, size_t(lw) * elemBytes, size_t(lh), cudaMemcpyHostToDevice));
        texBytes += size_t(lw) * lh * elemBytes;
        lw = std::max(lw / 2, 1);
        lh = std::max(lh / 2, 1);
    }
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeMipmappedArray;
    rd.res.mipmap.mipmap = t.mip;
    cudaTextureDesc td{};
    td.addressMode[0] = wrapU ? cudaAddressModeWrap : cudaAddressModeClamp;
    td.addressMode[1] = wrapV ? cudaAddressModeWrap : cudaAddressModeClamp;
    td.filterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeNormalizedFloat;
    td.normalizedCoords = 1;
    td.mipmapFilterMode = cudaFilterModeLinear;
    td.maxMipmapLevelClamp = float(count - 1);
    td.minMipmapLevelClamp = 0.0f;
    td.maxAnisotropy = 1;
    td.sRGB = srgb ? 1 : 0;
    CUDA_CHECK(cudaCreateTextureObject(&t.tex, &rd, &td, nullptr));
    return t;
}

DagRenderer::Tex DagRenderer::makeRgba8Mips(std::vector<unsigned char>& rgba, int n, bool srgb, bool wrap) {
    // the levels averaged as light (sRGB is decoded, averaged, encoded), the base level kept as it is
    std::vector<float> toLin(256);
    for (int k = 0; k < 256; ++k) {
        const float x = k / 255.0f;
        toLin[k] = srgb ? (x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f)) : x;
    }
    std::vector<unsigned char> toEnc(4096);
    for (int k = 0; k < 4096; ++k) {
        const float x = k / 4095.0f;
        const float e = srgb ? (x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f) : x;
        toEnc[k] = (unsigned char)(std::clamp(e, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    std::vector<std::vector<unsigned char>> levels;
    const unsigned char* prev = rgba.data();
    int pw = n;
    while (pw > 1) {
        const int nw = pw / 2;
        std::vector<unsigned char> cur(size_t(nw) * nw * 4);
        const unsigned char* P = prev;
        const int ppw = pw;
        parallelFor(nw, [&](int y) {
            for (int x = 0; x < nw; ++x) {
                unsigned char* o = &cur[(size_t(y) * nw + x) * 4];
                const unsigned char* a = P + (size_t(y * 2) * ppw + x * 2) * 4;
                const unsigned char* b = a + 4;
                const unsigned char* c = P + (size_t(y * 2 + 1) * ppw + x * 2) * 4;
                const unsigned char* d = c + 4;
                for (int q = 0; q < 3; ++q) {
                    const float v = (toLin[a[q]] + toLin[b[q]] + toLin[c[q]] + toLin[d[q]]) * 0.25f;
                    o[q] = toEnc[int(v * 4095.0f + 0.5f)];
                }
                o[3] = (unsigned char)((a[3] + b[3] + c[3] + d[3]) / 4);
            }
        });
        levels.push_back(std::move(cur));
        prev = levels.back().data();
        pw = nw;
    }
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<uchar4>();
    return makeMip(rgba.data(), n, n, 4, fd, srgb, wrap, wrap, &levels);
}

DagRenderer::Tex DagRenderer::makeRgba16Mips(std::vector<uint16_t>& v, int w, int h, bool wrapU, bool wrapV) {
    std::vector<std::vector<unsigned char>> levels;
    const uint16_t* prev = v.data();
    int pw = w, ph = h;
    while (pw > 1 && ph > 1) {
        const int nw = pw / 2, nh = ph / 2;
        std::vector<unsigned char> cur(size_t(nw) * nh * 8);
        uint16_t* C = reinterpret_cast<uint16_t*>(cur.data());
        const uint16_t* P = prev;
        const int ppw = pw;
        parallelFor(nh, [&](int y) {
            for (int x = 0; x < nw; ++x) {
                uint16_t* o = &C[(size_t(y) * nw + x) * 4];
                const uint16_t* a = P + (size_t(y * 2) * ppw + x * 2) * 4;
                const uint16_t* b = a + 4;
                const uint16_t* c = P + (size_t(y * 2 + 1) * ppw + x * 2) * 4;
                const uint16_t* d = c + 4;
                for (int q = 0; q < 4; ++q) o[q] = uint16_t((unsigned(a[q]) + b[q] + c[q] + d[q] + 2) / 4);
            }
        });
        levels.push_back(std::move(cur));
        prev = reinterpret_cast<const uint16_t*>(levels.back().data());
        pw = nw;
        ph = nh;
    }
    cudaChannelFormatDesc fd = cudaCreateChannelDesc(16, 16, 16, 16, cudaChannelFormatKindUnsigned);
    return makeMip(v.data(), w, h, 8, fd, false, wrapU, wrapV, &levels);
}

DagRenderer::Tex DagRenderer::makeFloat2D(const std::vector<float4>& v, int w, int h, bool wrapU) {
    Tex t;
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<float4>();
    CUDA_CHECK(cudaMallocArray(&t.arr, &fd, size_t(w), size_t(h)));
    CUDA_CHECK(cudaMemcpy2DToArray(t.arr, 0, 0, v.data(), size_t(w) * sizeof(float4), size_t(w) * sizeof(float4), size_t(h), cudaMemcpyHostToDevice));
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeArray;
    rd.res.array.array = t.arr;
    cudaTextureDesc td{};
    td.addressMode[0] = wrapU ? cudaAddressModeWrap : cudaAddressModeClamp;
    td.addressMode[1] = cudaAddressModeClamp;
    td.filterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeElementType;
    td.normalizedCoords = 1;
    CUDA_CHECK(cudaCreateTextureObject(&t.tex, &rd, &td, nullptr));
    tableBytes += size_t(w) * h * sizeof(float4);
    return t;
}

DagRenderer::Tex DagRenderer::makeFloat3D(const std::vector<float4>& v, int w, int h, int d) {
    // the host's order: (y, x, depth); the array's: (depth, y, x): rearranged
    std::vector<float4> o(v.size());
    parallelFor(d, [&](int z) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) o[(size_t(z) * h + y) * w + x] = v[(size_t(y) * w + x) * d + z];
    }, 1);
    Tex t;
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<float4>();
    CUDA_CHECK(cudaMalloc3DArray(&t.arr, &fd, make_cudaExtent(w, h, d)));
    cudaMemcpy3DParms cp{};
    cp.srcPtr = make_cudaPitchedPtr(o.data(), size_t(w) * sizeof(float4), size_t(w), size_t(h));
    cp.dstArray = t.arr;
    cp.extent = make_cudaExtent(w, h, d);
    cp.kind = cudaMemcpyHostToDevice;
    CUDA_CHECK(cudaMemcpy3D(&cp));
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeArray;
    rd.res.array.array = t.arr;
    cudaTextureDesc td{};
    td.addressMode[0] = cudaAddressModeWrap;
    td.addressMode[1] = cudaAddressModeClamp;
    td.addressMode[2] = cudaAddressModeClamp;
    td.filterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeElementType;
    td.normalizedCoords = 1;
    CUDA_CHECK(cudaCreateTextureObject(&t.tex, &rd, &td, nullptr));
    tableBytes += v.size() * sizeof(float4);
    return t;
}

void DagRenderer::uploadGround(DagWorld& w) {
    const double t0 = nowMs();
    params.Re = w.Re;
    params.h0 = w.h0;
    params.nearRadius = w.nearRadius;
    // the clip stacks
    for (int k = 0; k < DAG_CLIPS; ++k) {
        Tex t = makeRgba16Mips(w.clips[k].info, w.clips[k].n, w.clips[k].n, false, false);
        textures_.push_back(t);
        params.info[k] = {(unsigned long long)t.tex, w.clips[k].cell * w.clips[k].n * 0.5f, w.clips[k].cell};
        std::vector<uint16_t>().swap(w.clips[k].info);
    }
    for (int k = 0; k < DAG_CLIPS; ++k) {
        Tex t = makeRgba8Mips(w.albedo[k].rgba, w.albedo[k].n, true, false);
        textures_.push_back(t);
        params.albedo[k] = {(unsigned long long)t.tex, w.albedo[k].cell * w.albedo[k].n * 0.5f, w.albedo[k].cell};
        std::vector<uint8_t>().swap(w.albedo[k].rgba);
    }
    params.slopeScale = 12.0f;
    params.horizonLo = -20.0f * M_PIf / 180.0f;
    params.horizonHi = 70.0f * M_PIf / 180.0f;
    const double t1 = nowMs();
    std::printf("dag: dokular %.1f s, %.2f GB\n", (t1 - t0) * 1e-3, double(texBytes) / 1e9);
    std::fflush(stdout);
    // the far ground's GAS, one whole degree at a time
    instances_.clear();
    unsigned id = 0;
    auto place = [&](OptixTraversableHandle h, unsigned instanceId) {
        OptixInstance oi{};
        oi.transform[0] = oi.transform[5] = oi.transform[10] = 1.0f;
        oi.instanceId = instanceId;
        oi.sbtOffset = 0;
        oi.visibilityMask = 0xFFu;
        oi.flags = OPTIX_INSTANCE_FLAG_NONE;
        oi.traversableHandle = h;
        instances_.push_back(oi);
    };
    for (auto& t : w.far) {
        // big tiles split: a build's peak memory stays well under the card
        const size_t tri = t.tris.size() / 3;
        const size_t maxTri = 12000000;
        for (size_t first = 0; first < tri; first += maxTri) {
            const size_t count = std::min(tri - first, maxTri);
            const OptixTraversableHandle h = buildGasTriangles(t.tris.data() + first * 3, count * 3, nullptr, count);
            place(h, id++);
        }
        std::vector<float3>().swap(t.tris);
    }
    const double t2 = nowMs();
    std::printf("dag: uzak GAS %.1f s, %.2f GB, %u parca\n", (t2 - t1) * 1e-3, double(gasBytes) / 1e9, id);
    std::fflush(stdout);
    // the amplified ground
    for (int k = 0; k < 2; ++k) {
        DagWorld::Polar& P = w.polar[k];
        std::printf("dag: halka %d: %.1f milyon ucgen\n", k, double(P.idx.size()) * 1e-6);
        if (k == 1) {
            place(buildGasTriangles(P.pos.data(), P.pos.size(), P.idx.data(), P.idx.size()), DAG_ID_MID);
        } else {
            place(buildGasTriangles(P.pos.data(), P.pos.size(), P.idx.data(), P.idx.size()), DAG_ID_NEAR);
        }
        std::vector<uint3>().swap(P.idx);
    }
    // the railing and the stones
    unsigned mid = 0;
    for (auto& m : w.meshes) {
        DagGeom g{};
        auto up = [&](auto& v, auto** dst) {
            using T = std::remove_reference_t<decltype(v[0])>;
            if (v.empty()) {
                *dst = nullptr;
                return;
            }
            T* p = nullptr;
            CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&p), v.size() * sizeof(T)));
            CUDA_CHECK(cudaMemcpy(p, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
            meshBufs_.push_back(p);
            meshBytes += v.size() * sizeof(T);
            *dst = p;
        };
        float3* dp = nullptr;
        float3* dn = nullptr;
        float2* du = nullptr;
        uint3* di = nullptr;
        up(m.pos, &dp);
        up(m.nrm, &dn);
        up(m.uv, &du);
        up(m.idx, &di);
        g.pos = dp;
        g.nrm = dn;
        g.uv = du;
        g.idx = di;
        g.tint = m.tint;
        g.rough = m.rough;
        g.material = m.material;
        geomsHost_.push_back(g);
        place(buildGasTriangles(m.pos.data(), m.pos.size(), m.idx.data(), m.idx.size()), DAG_ID_RAIL + mid);
        ++mid;
    }
    if (!geomsHost_.empty()) {
        DagGeom* dg = nullptr;
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dg), geomsHost_.size() * sizeof(DagGeom)));
        CUDA_CHECK(cudaMemcpy(dg, geomsHost_.data(), geomsHost_.size() * sizeof(DagGeom), cudaMemcpyHostToDevice));
        meshBufs_.push_back(dg);
        params.geoms = dg;
    }
    buildTop();
    buildGasMs = nowMs() - t1;
    // how open the polar meshes are to the sky, then their textures
    {
        std::string log;
        const double t3 = nowMs();
        for (int k = 0; k < 2; ++k) {
            DagWorld::Polar& P = w.polar[k];
            if (k == 0) bakePolarAO(P, 32, 0.25f, 60.0f);
            else bakePolarAO(P, 16, 0.03f, 400.0f);
            Tex t = makeRgba16Mips(P.info, P.az, P.rings, true, false);
            textures_.push_back(t);
            params.polar[k] = {(unsigned long long)t.tex, P.r0, P.logQ, 6.2831853f / P.az};
            std::vector<uint16_t>().swap(P.info);
            if (k == 1) std::vector<float3>().swap(P.pos);
        }
        std::printf("dag: halka gok payi %.1f s\n", (nowMs() - t3) * 1e-3);
    }
}

void DagRenderer::bakePolarAO(DagWorld::Polar& P, int rays, float reach, float reachMax) {
    float3* dp = nullptr;
    float* dOut = nullptr;
    const size_t n = size_t(P.az) * P.rings;
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dp), n * sizeof(float3)));
    CUDA_CHECK(cudaMemcpy(dp, P.pos.data(), n * sizeof(float3), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&dOut), n * sizeof(float)));
    CUDA_CHECK(cudaMemset(dOut, 0, n * sizeof(float)));
    params.bakePos = dp;
    params.bakeOut = dOut;
    params.bakeAz = P.az;
    params.bakeRings = P.rings;
    params.bakeRays = rays;
    params.bakeReach = reach;
    params.bakeReachMax = reachMax;
    params.bakeCellAngle = 6.2831853f / P.az;
    // slabs small enough for the display driver's watchdog
    const int perLaunch = std::max(1, int(6.4e7 / (double(P.az) * rays)));
    for (int j0 = 0; j0 < P.rings; j0 += perLaunch) {
        params.bakeRing0 = j0;
        launch(rgBakeAO_, unsigned(P.az), unsigned(std::min(perLaunch, P.rings - j0)), 1);
        CUDA_CHECK(cudaStreamSynchronize(cudaStream_t(stream_)));
    }
    std::vector<float> host(n);
    CUDA_CHECK(cudaMemcpy(host.data(), dOut, n * sizeof(float), cudaMemcpyDeviceToHost));
    cudaFree(dp);
    cudaFree(dOut);
    params.bakePos = nullptr;
    params.bakeOut = nullptr;
    parallelFor(int(n >> 12) + 1, [&](int b) {
        const size_t a0 = size_t(b) << 12, a1 = std::min(n, (size_t(b) + 1) << 12);
        for (size_t i = a0; i < a1; ++i) P.info[i * 4 + 2] = uint16_t(std::clamp(host[i], 0.0f, 1.0f) * 65535.0f + 0.5f);
    });
}

void DagRenderer::uploadTables(const DagTables& t) {
    CUDA_CHECK(cudaDeviceSynchronize());
    for (Tex& x : tables_) {
        if (x.tex) cudaDestroyTextureObject(x.tex);
        if (x.arr) cudaFreeArray(x.arr);
    }
    tables_.clear();
    tableBytes = 0;
    Tex tr = makeFloat2D(t.transmittance, t.trW, t.trH, false);
    Tex sky = makeFloat2D(t.sky, t.skyW, t.skyH, true);
    Tex s = makeFloat3D(t.froxS, t.frW, t.frH, t.frD);
    Tex tt = makeFloat3D(t.froxT, t.frW, t.frH, t.frD);
    tables_ = {tr, sky, s, tt};
    params.transmittance = (unsigned long long)tr.tex;
    params.skyView = (unsigned long long)sky.tex;
    params.froxelS = (unsigned long long)s.tex;
    params.froxelT = (unsigned long long)tt.tex;
    params.eyeDip = t.eyeDip;
    params.froxR0 = t.froxR0;
    params.froxLogK = t.froxLogK;
    params.froxElMax = t.froxElMax;
    params.froxD = t.frD;
    for (int k = 0; k < 4; ++k) params.skySh[k] = t.sh[k];
}

void DagRenderer::uploadRock(int which, const std::string& albedoPath, const std::string& normalPath) {
    auto load = [](const std::string& path, std::vector<unsigned char>& out, int& w) {
        int h = 0, c = 0;
        unsigned char* d = stbi_load(path.c_str(), &w, &h, &c, 4);
        if (!d || w != h) {
            if (d) stbi_image_free(d);
            return false;
        }
        out.assign(d, d + size_t(w) * h * 4);
        stbi_image_free(d);
        return true;
    };
    std::vector<unsigned char> a, n;
    int aw = 0, nw = 0;
    bool oka = false, okn = false;
    std::thread ta([&] { oka = load(albedoPath, a, aw); });
    std::thread tn([&] { okn = load(normalPath, n, nw); });
    ta.join();
    tn.join();
    if (!oka || !okn) {
        std::fprintf(stderr, "dag: tarama okunamadi: %s\n", albedoPath.c_str());
        return;
    }
    // its mean reflectance, as light
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); i += 4 * 64) {
        const float r = a[i] / 255.0f, g = a[i + 1] / 255.0f, b = a[i + 2] / 255.0f;
        auto lin = [](float x) { return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); };
        sum += 0.2126f * lin(r) + 0.7152f * lin(g) + 0.0722f * lin(b);
    }
    params.rockMean[which] = float(sum / (a.size() / (4 * 64)));
    Tex ta2 = makeRgba8Mips(a, aw, true, true);
    Tex tn2 = makeRgba8Mips(n, nw, false, true);
    textures_.push_back(ta2);
    textures_.push_back(tn2);
    params.rockAlbedo[which] = (unsigned long long)ta2.tex;
    params.rockNormal[which] = (unsigned long long)tn2.tex;
}

void DagRenderer::uploadCirrus(const std::vector<unsigned char>& rgba, int n) {
    std::vector<std::vector<unsigned char>> levels;
    const unsigned char* prev = rgba.data();
    int pw = n;
    while (pw > 1) {
        const int nw = pw / 2;
        std::vector<unsigned char> cur(size_t(nw) * nw);
        for (int y = 0; y < nw; ++y)
            for (int x = 0; x < nw; ++x) {
                const unsigned char* a = prev + size_t(y * 2) * pw + x * 2;
                const unsigned char* c = prev + size_t(y * 2 + 1) * pw + x * 2;
                cur[size_t(y) * nw + x] = (unsigned char)((a[0] + a[1] + c[0] + c[1] + 2) / 4);
            }
        levels.push_back(std::move(cur));
        prev = levels.back().data();
        pw = nw;
    }
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<unsigned char>();
    Tex t = makeMip(rgba.data(), n, n, 1, fd, false, true, true, &levels);
    textures_.push_back(t);
    params.cirrus = (unsigned long long)t.tex;
}

void DagRenderer::launch(CUdeviceptr rg, unsigned int w, unsigned int h, unsigned int d) {
    CUDA_CHECK(cudaMemcpyAsync(dParams_, &params, sizeof(DagParams), cudaMemcpyHostToDevice, cudaStream_t(stream_)));
    OptixShaderBindingTable s = sbt_;
    s.raygenRecord = rg;
    OPTIX_CHECK(optixLaunch(pipeline_, reinterpret_cast<cudaStream_t>(stream_), CUdeviceptr(dParams_), sizeof(DagParams), &s, w, h, d));
}

void DagRenderer::render() {
    CUDA_CHECK(cudaEventRecord(ev0_, cudaStream_t(stream_)));
    launch(rgRender_, params.width, params.height, 1);
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
    cudaEventElapsedTime(&displayMs, ev1_, ev2_);
    cudaMemcpy(&badSamples, params.badCount, sizeof(unsigned int), cudaMemcpyDeviceToHost);
    cudaMemcpy(&probe, params.probe, sizeof(float4), cudaMemcpyDeviceToHost);
    cudaMemsetAsync(params.badCount, 0, sizeof(unsigned int), cudaStream_t(stream_));
}

void DagRenderer::download(std::vector<unsigned char>& rgba) const {
    rgba.resize(size_t(width_) * height_ * 4);
    CUDA_CHECK(cudaMemcpy(rgba.data(), params.output, rgba.size(), cudaMemcpyDeviceToHost));
}

std::vector<float4> DagRenderer::hdrHost() const {
    std::vector<float4> h(size_t(width_) * height_);
    CUDA_CHECK(cudaMemcpy(h.data(), params.hdr, h.size() * sizeof(float4), cudaMemcpyDeviceToHost));
    return h;
}

void DagRenderer::release() {
    if (context_) cudaDeviceSynchronize();
    for (void* p : gasBufs_) cudaFree(p);
    gasBufs_.clear();
    for (void* p : meshBufs_) cudaFree(p);
    meshBufs_.clear();
    geomsHost_.clear();
    instances_.clear();
    if (iasBuf_) cudaFree(iasBuf_);
    if (instBuf_) cudaFree(instBuf_);
    iasBuf_ = instBuf_ = nullptr;
    for (auto* list : {&textures_, &tables_}) {
        for (Tex& t : *list) {
            if (t.tex) cudaDestroyTextureObject(t.tex);
            if (t.mip) cudaFreeMipmappedArray(t.mip);
            if (t.arr) cudaFreeArray(t.arr);
        }
        list->clear();
    }
    for (int k = 0; k < kLevels; ++k) {
        if (levels_[k]) cudaFree(levels_[k]);
        if (accum_[k]) cudaFree(accum_[k]);
        levels_[k] = accum_[k] = nullptr;
    }
    if (params.badCount) cudaFree(params.badCount);
    params.badCount = nullptr;
    if (params.probe) cudaFree(params.probe);
    params.probe = nullptr;
    if (params.hdr) cudaFree(params.hdr);
    if (params.output) cudaFree(params.output);
    params.hdr = nullptr;
    params.output = nullptr;
    for (CUdeviceptr* p : {&rgRender_, &rgFinal_, &rgBakeAO_, &miss_, &hits_})
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
    for (cudaEvent_t* e : {&ev0_, &ev1_, &ev2_})
        if (*e) {
            cudaEventDestroy(*e);
            *e = nullptr;
        }
    if (stream_) cudaStreamDestroy(cudaStream_t(stream_));
    stream_ = nullptr;
}
