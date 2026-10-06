#include "scenes/denizalti/dz_bake.h"

#include <cuda_runtime.h>
#include <nvrtc.h>
#include <optix.h>
#include <optix_stack_size.h>
#include <optix_stubs.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace dz {

namespace {

struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) Record {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

std::string readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void optixLog(unsigned int level, const char* tag, const char* msg, void*) {
    if (level <= 2) std::fprintf(stderr, "[optix %u][%s] %s\n", level, tag, msg);
}

#define CK(x)                                                                                              \
    do {                                                                                                   \
        cudaError_t e_ = (x);                                                                              \
        if (e_ != cudaSuccess) {                                                                           \
            err = std::string("CUDA ") + #x + ": " + cudaGetErrorString(e_);                                \
            return false;                                                                                  \
        }                                                                                                  \
    } while (0)
#define OK(x)                                                                                              \
    do {                                                                                                   \
        OptixResult r_ = (x);                                                                              \
        if (r_ != OPTIX_SUCCESS) {                                                                         \
            err = std::string("OptiX ") + #x + ": " + optixGetErrorString(r_);                              \
            return false;                                                                                  \
        }                                                                                                  \
    } while (0)

uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// the albedo the light bounces with (the shader's base colours; metals as a dull diffuse share of their F0)
void albedos(float3* a) {
    for (int i = 0; i < DZ_MAX_MATS; ++i) a[i] = make_float3(0.5f, 0.5f, 0.5f);
    a[MAT_PAINT_HULL] = make_float3(0.62f, 0.66f, 0.60f);
    a[MAT_PAINT_PIPE] = make_float3(0.52f, 0.53f, 0.50f);
    a[MAT_PAINT_DARK] = make_float3(0.035f, 0.035f, 0.035f);
    a[MAT_BRASS] = make_float3(0.87f, 0.72f, 0.42f) * 0.5f;
    a[MAT_STEEL] = make_float3(0.56f, 0.57f, 0.58f) * 0.5f;
    a[MAT_CHROME] = make_float3(0.55f, 0.56f, 0.57f) * 0.5f;
    a[MAT_RUBBER] = make_float3(0.025f, 0.025f, 0.025f);
    a[MAT_DECK] = make_float3(0.16f, 0.16f, 0.15f);
    a[MAT_GLASS] = make_float3(0.0f, 0.0f, 0.0f);
    a[MAT_DIAL] = make_float3(0.80f, 0.79f, 0.74f);
    a[MAT_RED] = make_float3(0.45f, 0.035f, 0.025f);
    a[MAT_EMISSIVE] = make_float3(0.8f, 0.8f, 0.8f);
    a[MAT_CREAM] = make_float3(0.72f, 0.68f, 0.56f);
    a[MAT_GREY] = make_float3(0.40f, 0.42f, 0.42f);
    a[MAT_DECK_GREEN] = make_float3(0.10f, 0.20f, 0.13f);
    a[MAT_VINYL] = make_float3(0.08f, 0.38f, 0.40f);
    a[MAT_ALU] = make_float3(0.91f, 0.92f, 0.92f) * 0.5f;
    a[MAT_WRINKLE] = make_float3(0.03f, 0.03f, 0.03f);
    a[MAT_RED_GLASS] = make_float3(0.5f, 0.02f, 0.01f);
    a[MAT_TAPE] = make_float3(0.42f, 0.04f, 0.03f);
    a[MAT_BLACK_PLATE] = make_float3(0.03f, 0.03f, 0.03f);
    a[MAT_LAMP_SHADE] = make_float3(0.05f, 0.11f, 0.07f);
    a[MAT_WOOD] = make_float3(0.36f, 0.22f, 0.12f);
    a[MAT_PAPER] = make_float3(0.80f, 0.78f, 0.70f);
    a[MAT_INDICATOR_RED] = make_float3(0.3f, 0.02f, 0.02f);
    a[MAT_INDICATOR_GREEN] = make_float3(0.02f, 0.3f, 0.05f);
}

}   // namespace

uint16_t toHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t e = int32_t((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFF) == 0xFF) return uint16_t(sign | 0x7C00u | (m ? 0x200u : 0));
    if (e >= 31) return uint16_t(sign | 0x7C00u);
    if (e <= 0) {
        if (e < -10) return uint16_t(sign);
        m |= 0x800000u;
        const uint32_t shift = uint32_t(14 - e);
        uint32_t h = m >> shift;
        if ((m >> (shift - 1)) & 1u) ++h;   // round half up
        return uint16_t(sign | h);
    }
    uint32_t h = sign | (uint32_t(e) << 10) | (m >> 13);
    if (m & 0x1000u) ++h;   // round to nearest (carry into the exponent is right)
    return uint16_t(h);
}
float fromHalf(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    uint32_t e = (h >> 10) & 0x1F, m = h & 0x3FFu;
    uint32_t x;
    if (e == 0) {
        if (!m) x = sign;
        else {
            e = 1;
            while (!(m & 0x400u)) { m <<= 1; --e; }
            m &= 0x3FFu;
            x = sign | ((e + 127 - 15) << 23) | (m << 13);
        }
    } else if (e == 31) x = sign | 0x7F800000u | (m << 13);
    else x = sign | ((e + 127 - 15) << 23) | (m << 13);
    float f;
    std::memcpy(&f, &x, 4);
    return f;
}

uint64_t bakeKey(const Mesh& m, const std::vector<DzLight>& lights, const BakeOptions& o) {
    uint64_t h = 1469598103934665603ULL;
    for (const Vtx& v : m.v) {
        h = fnv(h, &v.p, sizeof(v.p));
        h = fnv(h, &v.n, sizeof(v.n));
        h = fnv(h, &v.uv2, sizeof(v.uv2));
        h = fnv(h, &v.mat, sizeof(v.mat));
    }
    h = fnv(h, m.idx.data(), m.idx.size() * sizeof(uint32_t));
    h = fnv(h, lights.data(), lights.size() * sizeof(DzLight));
    h = fnv(h, &o, sizeof(o));
    const int version = 4;
    h = fnv(h, &version, sizeof(version));
    return h;
}

bool bake(const Mesh& m, const Atlas& a, const std::vector<DzLight>& lights, const BakeOptions& o, const std::string& root, LightMap& out,
          std::string& err, const std::function<void(float, const std::string&)>& progress) {
    const auto t0 = std::chrono::steady_clock::now();
    auto say = [&](float f, const std::string& s) {
        if (progress) progress(f, s);
    };
    say(0.0f, "teksel yuzeye yerlestiriliyor");
    Rasterised ras = rasterise(m, a);
    const size_t nt = ras.texels.size();
    std::fprintf(stderr, "firin: atlas %dx%d, %zu teksel (%.0f%% dolu)\n", a.width, a.height, nt, 100.0 * nt / (double(a.width) * a.height));

    CK(cudaFree(nullptr));
    OK(optixInit());
    OptixDeviceContext ctx = nullptr;
    OptixDeviceContextOptions co{};
    co.logCallbackFunction = optixLog;
    co.logCallbackLevel = 4;
    OK(optixDeviceContextCreate(nullptr, &co, &ctx));

    // ---- the device program
    const std::string src = readText(root + "/src/scenes/denizalti/device/dz_bake.cu");
    if (src.empty()) {
        err = "dz_bake.cu okunamadi";
        return false;
    }
    nvrtcProgram prog;
    nvrtcCreateProgram(&prog, src.c_str(), "dz_bake.cu", 0, nullptr, nullptr);
    const std::string inc[] = {"-I" + root + "/src", "-I" + root + "/third_party/optix/include", "-I" + root + "/third_party/cuda/include"};
    std::vector<const char*> opts = {"--std=c++17", "-arch=compute_89", "--use_fast_math", "--relocatable-device-code=true", "--optix-ir",
                                     "-lineinfo", "-D__x86_64"};
    for (const auto& s : inc) opts.push_back(s.c_str());
    const nvrtcResult cr = nvrtcCompileProgram(prog, int(opts.size()), opts.data());
    size_t ls = 0;
    nvrtcGetProgramLogSize(prog, &ls);
    std::string nvlog(ls, '\0');
    if (ls > 1) nvrtcGetProgramLog(prog, nvlog.data());
    if (cr != NVRTC_SUCCESS) {
        err = "NVRTC: " + nvlog;
        nvrtcDestroyProgram(&prog);
        return false;
    }
    size_t irSize = 0;
    nvrtcGetOptiXIRSize(prog, &irSize);
    std::string ir(irSize, '\0');
    nvrtcGetOptiXIR(prog, ir.data());
    nvrtcDestroyProgram(&prog);
    OptixModuleCompileOptions mco{};
    mco.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    mco.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;
    OptixPipelineCompileOptions pco{};
    pco.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
    pco.numPayloadValues = 2;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "params";
    pco.pipelineLaunchParamsSizeInBytes = sizeof(DzBakeParams);
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
    char olog[8192];
    size_t olen = sizeof(olog);
    OptixModule module = nullptr;
    if (optixModuleCreate(ctx, &mco, &pco, ir.data(), ir.size(), olog, &olen, &module) != OPTIX_SUCCESS) {
        err = std::string("optixModuleCreate: ") + olog;
        return false;
    }
    OptixProgramGroupDesc d[3] = {};
    d[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[0].raygen.module = module;
    d[0].raygen.entryFunctionName = "__raygen__bake";
    d[1].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[2].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    OptixProgramGroup groups[3];
    OptixProgramGroupOptions go{};
    olen = sizeof(olog);
    OK(optixProgramGroupCreate(ctx, d, 3, &go, olog, &olen, groups));
    OptixPipelineLinkOptions lo{};
    lo.maxTraceDepth = 1;
    OptixPipeline pipe = nullptr;
    olen = sizeof(olog);
    OK(optixPipelineCreate(ctx, &pco, &lo, groups, 3, olog, &olen, &pipe));
    OK(optixPipelineSetStackSizeFromCallDepths(pipe, 1, 0, 0, 0, 1));
    CUdeviceptr rec[3] = {};
    for (int i = 0; i < 3; ++i) {
        Record r{};
        OK(optixSbtRecordPackHeader(groups[i], &r));
        CK(cudaMalloc(reinterpret_cast<void**>(&rec[i]), sizeof(Record)));
        CK(cudaMemcpy(reinterpret_cast<void*>(rec[i]), &r, sizeof(Record), cudaMemcpyHostToDevice));
    }
    OptixShaderBindingTable sbt{};
    sbt.raygenRecord = rec[0];
    sbt.missRecordBase = rec[1];
    sbt.missRecordStrideInBytes = sizeof(Record);
    sbt.missRecordCount = 1;
    sbt.hitgroupRecordBase = rec[2];
    sbt.hitgroupRecordStrideInBytes = sizeof(Record);
    sbt.hitgroupRecordCount = 1;

    // ---- the geometry: positions and normals, triangles without the glass (light goes through it)
    std::vector<float3> pos(m.v.size());
    std::vector<float4> nrm(m.v.size());
    for (size_t i = 0; i < m.v.size(); ++i) {
        pos[i] = m.v[i].p;
        nrm[i] = make_float4(m.v[i].n.x, m.v[i].n.y, m.v[i].n.z, 0.0f);
    }
    std::vector<uint3> tri;
    std::vector<unsigned int> tmat;
    tri.reserve(m.tris());
    for (size_t k = 0; k + 2 < m.idx.size(); k += 3) {
        const uint32_t mt = m.v[m.idx[k]].mat;
        if (mt == MAT_GLASS) continue;
        tri.push_back(make_uint3(m.idx[k], m.idx[k + 1], m.idx[k + 2]));
        tmat.push_back(mt);
    }
    float3* dPos = nullptr;
    float4* dNrm = nullptr;
    uint3* dTri = nullptr;
    unsigned int* dMat = nullptr;
    CK(cudaMalloc(reinterpret_cast<void**>(&dPos), pos.size() * sizeof(float3)));
    CK(cudaMemcpy(dPos, pos.data(), pos.size() * sizeof(float3), cudaMemcpyHostToDevice));
    CK(cudaMalloc(reinterpret_cast<void**>(&dNrm), nrm.size() * sizeof(float4)));
    CK(cudaMemcpy(dNrm, nrm.data(), nrm.size() * sizeof(float4), cudaMemcpyHostToDevice));
    CK(cudaMalloc(reinterpret_cast<void**>(&dTri), tri.size() * sizeof(uint3)));
    CK(cudaMemcpy(dTri, tri.data(), tri.size() * sizeof(uint3), cudaMemcpyHostToDevice));
    CK(cudaMalloc(reinterpret_cast<void**>(&dMat), tmat.size() * sizeof(unsigned int)));
    CK(cudaMemcpy(dMat, tmat.data(), tmat.size() * sizeof(unsigned int), cudaMemcpyHostToDevice));
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    CUdeviceptr vb = CUdeviceptr(dPos);
    bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
    bi.triangleArray.vertexStrideInBytes = sizeof(float3);
    bi.triangleArray.numVertices = unsigned(pos.size());
    bi.triangleArray.vertexBuffers = &vb;
    bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    bi.triangleArray.indexStrideInBytes = sizeof(uint3);
    bi.triangleArray.numIndexTriplets = unsigned(tri.size());
    bi.triangleArray.indexBuffer = CUdeviceptr(dTri);
    const unsigned int flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
    bi.triangleArray.flags = flags;
    bi.triangleArray.numSbtRecords = 1;
    OptixAccelBuildOptions ao{};
    ao.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE | OPTIX_BUILD_FLAG_ALLOW_RANDOM_VERTEX_ACCESS;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sizes{};
    OK(optixAccelComputeMemoryUsage(ctx, &ao, &bi, 1, &sizes));
    void *temp = nullptr, *gasBuf = nullptr;
    CK(cudaMalloc(&temp, sizes.tempSizeInBytes));
    CK(cudaMalloc(&gasBuf, sizes.outputSizeInBytes));
    OptixTraversableHandle handle = 0;
    OK(optixAccelBuild(ctx, nullptr, &ao, &bi, 1, CUdeviceptr(temp), sizes.tempSizeInBytes, CUdeviceptr(gasBuf), sizes.outputSizeInBytes, &handle,
                       nullptr, 0));
    CK(cudaDeviceSynchronize());
    cudaFree(temp);

    DzLight* dLights = nullptr;
    CK(cudaMalloc(reinterpret_cast<void**>(&dLights), std::max<size_t>(1, lights.size()) * sizeof(DzLight)));
    if (!lights.empty()) CK(cudaMemcpy(dLights, lights.data(), lights.size() * sizeof(DzLight), cudaMemcpyHostToDevice));

    // ---- the texels in batches; every batch traced in short launches until it has all its samples
    const size_t batch = std::min<size_t>(nt, 1u << 21);
    float4 *dP = nullptr, *dN = nullptr, *acc[5] = {};
    CK(cudaMalloc(reinterpret_cast<void**>(&dP), batch * sizeof(float4)));
    CK(cudaMalloc(reinterpret_cast<void**>(&dN), batch * sizeof(float4)));
    for (auto& p : acc) CK(cudaMalloc(reinterpret_cast<void**>(&p), batch * sizeof(float4)));
    DzBakeParams prm{};
    prm.handle = handle;
    prm.vtxN = dNrm;
    prm.idx = dTri;
    prm.triMat = dMat;
    prm.lights = dLights;
    prm.lightCount = unsigned(lights.size());
    prm.maxBounces = unsigned(o.bounces);
    prm.emissiveMat = MAT_EMISSIVE;
    prm.glassMat = MAT_GLASS;
    albedos(prm.albedo);
    DzBakeParams* dPrm = nullptr;
    CK(cudaMalloc(reinterpret_cast<void**>(&dPrm), sizeof(DzBakeParams)));
    cudaStream_t stream;
    CK(cudaStreamCreate(&stream));

    const int CH = 20;   // L0 rgb + covered, L1 r, L1 g, L1 b (xyz + 0), lamp visibility 0..3
    std::vector<float> data(size_t(a.width) * a.height * CH, 0.0f);
    std::vector<float4> hp(batch), hn(batch), h0(batch), h1(batch), h2(batch), h3(batch), h4(batch);
    unsigned int perLaunch = 8;   // samples per launch: grown while launches stay short
    size_t done = 0;
    for (size_t b0 = 0; b0 < nt; b0 += batch) {
        const size_t cnt = std::min(batch, nt - b0);
        for (size_t i = 0; i < cnt; ++i) {
            const Texel& t = ras.texels[b0 + i];
            hp[i] = make_float4(t.p.x, t.p.y, t.p.z, 0.0f);
            hn[i] = make_float4(t.n.x, t.n.y, t.n.z, 0.0f);
        }
        CK(cudaMemcpy(dP, hp.data(), cnt * sizeof(float4), cudaMemcpyHostToDevice));
        CK(cudaMemcpy(dN, hn.data(), cnt * sizeof(float4), cudaMemcpyHostToDevice));
        for (auto& p : acc) CK(cudaMemset(p, 0, cnt * sizeof(float4)));
        prm.texP = dP;
        prm.texN = dN;
        prm.acc0 = acc[0];
        prm.acc1 = acc[1];
        prm.acc2 = acc[2];
        prm.acc3 = acc[3];
        prm.acc4 = acc[4];
        prm.count = unsigned(cnt);
        prm.texelBase = unsigned(b0);
        unsigned int s = 0;
        while (s < unsigned(o.samples)) {
            prm.sampleBase = s;
            prm.samples = std::min(perLaunch, unsigned(o.samples) - s);
            CK(cudaMemcpyAsync(dPrm, &prm, sizeof(prm), cudaMemcpyHostToDevice, stream));
            const auto l0 = std::chrono::steady_clock::now();
            OK(optixLaunch(pipe, stream, CUdeviceptr(dPrm), sizeof(DzBakeParams), &sbt, unsigned(cnt), 1, 1));
            CK(cudaStreamSynchronize(stream));
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l0).count();
            s += prm.samples;
            // keep a launch near 250 ms (far under the driver's 2 s watchdog)
            if (ms < 120.0 && perLaunch < 1024) perLaunch *= 2;
            else if (ms > 500.0 && perLaunch > 1) perLaunch /= 2;
            const float f = float((double(done) + double(cnt) * s / o.samples) / double(nt));
            char line[160];
            std::snprintf(line, sizeof(line), "isik izleniyor: %zu/%zu teksel, %u/%d ornek", b0 + cnt, nt, s, o.samples);
            say(0.05f + 0.9f * f, line);
        }
        CK(cudaMemcpy(h0.data(), acc[0], cnt * sizeof(float4), cudaMemcpyDeviceToHost));
        CK(cudaMemcpy(h1.data(), acc[1], cnt * sizeof(float4), cudaMemcpyDeviceToHost));
        CK(cudaMemcpy(h2.data(), acc[2], cnt * sizeof(float4), cudaMemcpyDeviceToHost));
        CK(cudaMemcpy(h3.data(), acc[3], cnt * sizeof(float4), cudaMemcpyDeviceToHost));
        CK(cudaMemcpy(h4.data(), acc[4], cnt * sizeof(float4), cudaMemcpyDeviceToHost));
        for (size_t i = 0; i < cnt; ++i) {
            const Texel& t = ras.texels[b0 + i];
            const size_t ai = size_t(t.xy & 0xFFFFu) + size_t(t.xy >> 16) * a.width;
            const float inv = 1.0f / std::max(1.0f, h0[i].w);
            float* dd = &data[ai * CH];
            const float4 v[5] = {h0[i], h1[i], h2[i], h3[i], h4[i]};
            for (int k = 0; k < 4; ++k) {
                dd[k * 4 + 0] = v[k].x * inv;
                dd[k * 4 + 1] = v[k].y * inv;
                dd[k * 4 + 2] = v[k].z * inv;
                dd[k * 4 + 3] = 0.0f;
            }
            dd[3] = h2[i].w > 0.0f ? h1[i].w / h2[i].w : 1.0f;   // the occlusion within 25 cm (1 open)
            dd[16] = v[4].x * inv;
            dd[17] = v[4].y * inv;
            dd[18] = v[4].z * inv;
            dd[19] = v[4].w * inv;
        }
        done += cnt;
    }
    say(0.96f, "kenarlar dolduruluyor");
    dilate(data, CH, a, ras.index);
    out.atlas = a;
    out.texels = nt;
    for (int k = 0; k < 5; ++k) out.plane[k].resize(size_t(a.width) * a.height * 4);
    for (size_t i = 0; i < size_t(a.width) * a.height; ++i)
        for (int k = 0; k < 5; ++k)
            for (int c = 0; c < 4; ++c) out.plane[k][i * 4 + c] = toHalf(data[i * CH + k * 4 + c]);

    cudaFree(dP);
    cudaFree(dN);
    for (auto p : acc) cudaFree(p);
    cudaFree(dPrm);
    cudaFree(dLights);
    cudaFree(gasBuf);
    cudaFree(dPos);
    cudaFree(dNrm);
    cudaFree(dTri);
    cudaFree(dMat);
    for (auto r : rec) cudaFree(reinterpret_cast<void*>(r));
    cudaStreamDestroy(stream);
    optixPipelineDestroy(pipe);
    for (auto g : groups) optixProgramGroupDestroy(g);
    optixModuleDestroy(module);
    optixDeviceContextDestroy(ctx);
    out.bakeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    say(1.0f, "bitti");
    return true;
}

bool saveLightMap(const std::string& path, const LightMap& lm) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t magic = 0x315A4449;   // "IDZ1"
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&lm.key), 8);
    f.write(reinterpret_cast<const char*>(&lm.atlas), sizeof(Atlas));
    f.write(reinterpret_cast<const char*>(&lm.bakeSeconds), 8);
    const uint64_t texels = lm.texels;
    f.write(reinterpret_cast<const char*>(&texels), 8);
    for (int k = 0; k < 5; ++k) f.write(reinterpret_cast<const char*>(lm.plane[k].data()), std::streamsize(lm.plane[k].size() * 2));
    return bool(f);
}

bool loadLightMap(const std::string& path, uint64_t key, LightMap& lm) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t magic = 0;
    uint64_t k = 0, texels = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&k), 8);
    if (magic != 0x315A4449 || k != key) return false;
    f.read(reinterpret_cast<char*>(&lm.atlas), sizeof(Atlas));
    f.read(reinterpret_cast<char*>(&lm.bakeSeconds), 8);
    f.read(reinterpret_cast<char*>(&texels), 8);
    lm.texels = size_t(texels);
    lm.key = k;
    for (int p = 0; p < 5; ++p) {
        lm.plane[p].resize(size_t(lm.atlas.width) * lm.atlas.height * 4);
        f.read(reinterpret_cast<char*>(lm.plane[p].data()), std::streamsize(lm.plane[p].size() * 2));
    }
    return bool(f);
}

}   // namespace dz
