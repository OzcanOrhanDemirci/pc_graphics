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
#include <thread>

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
    float3 al[DZ_MAX_MATS];
    albedos(al);
    h = fnv(h, al, sizeof(al));
    const int version = 13;   // 13: stencils are not occluders   // 9: samples spread over the texel's footprint; 10-11: rough probe mips sampled more, a mip's probes in one launch;
                              // 12: the probes' own clamp, a launch no more paths than a probe's mip once had
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
    OptixProgramGroupDesc d[5] = {};
    d[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[0].raygen.module = module;
    d[0].raygen.entryFunctionName = "__raygen__bake";
    d[1].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[2].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    d[3].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[3].raygen.module = module;
    d[3].raygen.entryFunctionName = "__raygen__volume";
    d[4].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[4].raygen.module = module;
    d[4].raygen.entryFunctionName = "__raygen__probe";
    OptixProgramGroup groups[5];
    OptixProgramGroupOptions go{};
    olen = sizeof(olog);
    OK(optixProgramGroupCreate(ctx, d, 5, &go, olog, &olen, groups));
    OptixPipelineLinkOptions lo{};
    lo.maxTraceDepth = 1;
    OptixPipeline pipe = nullptr;
    olen = sizeof(olog);
    OK(optixPipelineCreate(ctx, &pco, &lo, groups, 5, olog, &olen, &pipe));
    OK(optixPipelineSetStackSizeFromCallDepths(pipe, 1, 0, 0, 0, 1));
    CUdeviceptr rec[5] = {};
    for (int i = 0; i < 5; ++i) {
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
        // a stencil (a cream label quad 0.6 mm off its surface) is paint, not a sheet: as an occluder it baked the
        // surface under it black, which showed once the quad was drawn only where its letters are
        if (mt == MAT_CREAM && m.v[m.idx[k]].feat == FEAT_LABEL) continue;
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
    prm.clampLum = o.clamp;
    prm.probeClampLum = 1.0f;
    prm.emissiveMat = MAT_EMISSIVE;
    prm.glassMat = MAT_GLASS;
    albedos(prm.albedo);
    DzBakeParams* dPrm = nullptr;
    CK(cudaMalloc(reinterpret_cast<void**>(&dPrm), sizeof(DzBakeParams)));
    cudaStream_t stream;
    CK(cudaStreamCreate(&stream));

    // the result goes straight into the five half-float planes (no full-atlas float copy: the room's atlas is ~10^8
    // texels); a chart id per texel for the filter
    const size_t atlasN = size_t(a.width) * a.height;
    out.atlas = a;
    out.texels = nt;
    for (int k = 0; k < 5; ++k) out.plane[k].assign(atlasN * 4, toHalf(0.0f));
    std::vector<int32_t> chartOf(atlasN, -1);
    std::vector<float4> hp(batch), hn(batch), h0(batch), h1(batch), h2(batch), h3(batch), h4(batch);
    unsigned int perLaunch = 8;   // samples per launch: grown while launches stay short
    size_t done = 0;
    for (size_t b0 = 0; b0 < nt; b0 += batch) {
        const size_t cnt = std::min(batch, nt - b0);
        for (size_t i = 0; i < cnt; ++i) {
            const Texel& t = ras.texels[b0 + i];
            // the texel's footprint on the surface (m): its triangle's area in the world over its area in the atlas
            float ts = 0.0f;
            {
                const uint32_t* id = &m.idx[size_t(t.tri) * 3];
                const Vtx &va = m.v[id[0]], &vb = m.v[id[1]], &vc = m.v[id[2]];
                const float aw = length(cross(vb.p - va.p, vc.p - va.p));
                const float d1x = (vb.uv2.x - va.uv2.x) * a.width, d1y = (vb.uv2.y - va.uv2.y) * a.height;
                const float d2x = (vc.uv2.x - va.uv2.x) * a.width, d2y = (vc.uv2.y - va.uv2.y) * a.height;
                const float aT = std::fabs(d1x * d2y - d1y * d2x);
                if (aT > 1e-9f) ts = std::sqrt(aw / aT);
            }
            hp[i] = make_float4(t.p.x, t.p.y, t.p.z, ts);
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
            say(0.05f + 0.85f * f, line);
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
            const float4 v[5] = {h0[i], h1[i], h2[i], h3[i], h4[i]};
            for (int k = 0; k < 5; ++k) {
                uint16_t* d = &out.plane[k][ai * 4];
                d[0] = toHalf(v[k].x * inv);
                d[1] = toHalf(v[k].y * inv);
                d[2] = toHalf(v[k].z * inv);
                d[3] = toHalf(k == 4 ? v[k].w * inv : 0.0f);
            }
            out.plane[0][ai * 4 + 3] = toHalf(h2[i].w > 0.0f ? h1[i].w / h2[i].w : 1.0f);   // the occlusion within 25 cm
            chartOf[ai] = int32_t(m.v[m.idx[size_t(t.tri) * 3]].chart);
        }
        done += cnt;
    }
    // the Monte Carlo noise left: an edge-keeping filter in the atlas, inside each chart only (5 x 5 texels, weighted by
    // distance and by how alike the light is: a shadow's edge stays), then the padding filled; plane by plane
    say(0.92f, "gurultu suzuluyor");
    std::vector<float> guide(atlasN, 0.0f);
    for (size_t i = 0; i < atlasN; ++i)
        if (chartOf[i] >= 0)
            guide[i] = 0.2126f * fromHalf(out.plane[0][i * 4]) + 0.7152f * fromHalf(out.plane[0][i * 4 + 1]) + 0.0722f * fromHalf(out.plane[0][i * 4 + 2]);
    const int radius = o.filter;
    std::vector<float> fsrc, dst;
    for (int k = 0; k < 5; ++k) {
        fsrc.assign(atlasN * 4, 0.0f);
        for (size_t i = 0; i < atlasN * 4; ++i) fsrc[i] = fromHalf(out.plane[k][i]);
        dst = fsrc;
        if (radius > 0) {
            const int W = a.width, Hh = a.height;
            auto rows = [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < W; ++x) {
                        const size_t i = size_t(y) * W + x;
                        const int c = chartOf[i];
                        if (c < 0) continue;
                        const float g0 = guide[i];
                        float acc4[4] = {0, 0, 0, 0}, ws = 0.0f;
                        for (int dy = -radius; dy <= radius; ++dy) {
                            const int yy = y + dy;
                            if (yy < 0 || yy >= Hh) continue;
                            for (int dx = -radius; dx <= radius; ++dx) {
                                const int xx = x + dx;
                                if (xx < 0 || xx >= W) continue;
                                const size_t j = size_t(yy) * W + xx;
                                if (chartOf[j] != c) continue;
                                const float rel = (guide[j] - g0) / (0.25f * (guide[j] + g0) + 1e-4f);
                                const float w = std::exp(-0.35f * float(dx * dx + dy * dy) - rel * rel);
                                for (int q = 0; q < 4; ++q) acc4[q] += w * fsrc[j * 4 + q];
                                ws += w;
                            }
                        }
                        for (int q = 0; q < 4; ++q) dst[i * 4 + q] = acc4[q] / ws;
                    }
            };
            const int nth = std::max(1u, std::thread::hardware_concurrency());
            std::vector<std::thread> th;
            for (int t = 0; t < nth; ++t) th.emplace_back(rows, Hh * t / nth, Hh * (t + 1) / nth);
            for (auto& t : th) t.join();
        }
        dilate(dst, 4, a, ras.index);
        for (size_t i = 0; i < atlasN * 4; ++i) out.plane[k][i] = toHalf(dst[i]);
    }
    say(0.96f, "kenarlar dolduruldu");

    // ---- the haze volume: the room's bounds in 4 cm cells
    {
        const float cell = o.volCell;
        const float3 lo = make_float3(-2.46f, 0.0f, 0.0f), hi = make_float3(2.46f, 2.62f, 8.23f);
        const uint3 dim = make_uint3(unsigned(std::ceil((hi.x - lo.x) / cell)), unsigned(std::ceil((hi.y - lo.y) / cell)),
                                     unsigned(std::ceil((hi.z - lo.z) / cell)));
        const size_t nv = size_t(dim.x) * dim.y * dim.z;
        out.volDim[0] = int(dim.x);
        out.volDim[1] = int(dim.y);
        out.volDim[2] = int(dim.z);
        out.volLo[0] = lo.x;
        out.volLo[1] = lo.y;
        out.volLo[2] = lo.z;
        out.volCell = cell;
        out.vol[0].assign(nv * 4, toHalf(0.0f));
        out.vol[1].assign(nv * 4, toHalf(0.0f));
        prm.volLo = lo;
        prm.volCell = cell;
        prm.volDim = dim;
        sbt.raygenRecord = rec[3];
        unsigned int perV = 4;
        for (size_t b0 = 0; b0 < nv; b0 += batch) {
            const size_t cnt = std::min(batch, nv - b0);
            CK(cudaMemset(acc[0], 0, cnt * sizeof(float4)));
            CK(cudaMemset(acc[1], 0, cnt * sizeof(float4)));
            prm.acc0 = acc[0];
            prm.acc1 = acc[1];
            prm.count = unsigned(cnt);
            prm.texelBase = unsigned(b0);
            unsigned int s = 0;
            while (s < unsigned(o.volSamples)) {
                prm.sampleBase = s;
                prm.samples = std::min(perV, unsigned(o.volSamples) - s);
                CK(cudaMemcpyAsync(dPrm, &prm, sizeof(prm), cudaMemcpyHostToDevice, stream));
                const auto l0 = std::chrono::steady_clock::now();
                OK(optixLaunch(pipe, stream, CUdeviceptr(dPrm), sizeof(DzBakeParams), &sbt, unsigned(cnt), 1, 1));
                CK(cudaStreamSynchronize(stream));
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l0).count();
                s += prm.samples;
                if (ms < 120.0 && perV < 1024) perV *= 2;
                else if (ms > 500.0 && perV > 1) perV /= 2;
            }
            CK(cudaMemcpy(h0.data(), acc[0], cnt * sizeof(float4), cudaMemcpyDeviceToHost));
            CK(cudaMemcpy(h1.data(), acc[1], cnt * sizeof(float4), cudaMemcpyDeviceToHost));
            for (size_t i = 0; i < cnt; ++i) {
                const float inv = 1.0f / std::max(1.0f, h0[i].w);
                uint16_t* a0 = &out.vol[0][(b0 + i) * 4];
                uint16_t* a1 = &out.vol[1][(b0 + i) * 4];
                a0[0] = toHalf(h0[i].x * inv);
                a0[1] = toHalf(h0[i].y * inv);
                a0[2] = toHalf(h0[i].z * inv);
                a0[3] = toHalf(1.0f);
                a1[0] = toHalf(h1[i].x * inv);
                a1[1] = toHalf(h1[i].y * inv);
                a1[2] = toHalf(h1[i].z * inv);
                a1[3] = toHalf(0.0f);
            }
            say(0.96f + 0.03f * float(b0 + cnt) / float(nv), "pus pisiriliyor");
        }
        std::printf("pus: %ux%ux%u hucre (%.0f mm), %d ornek\n", dim.x, dim.y, dim.z, cell * 1000.0f, o.volSamples);
    }

    // ---- reflection probes: a 3 x 2 x 9 grid in the room's air (x across, y two heights, z along), cube maps with a
    // GGX-filtered mip per roughness, traced here (no filtering pass after)
    {
        const int nx = 3, ny = 2, nz = 9, res = o.probeRes, mips = 6;
        out.probeDims[0] = nx;
        out.probeDims[1] = ny;
        out.probeDims[2] = nz;
        out.probeLo[0] = -0.9f;
        out.probeLo[1] = 0.55f;
        out.probeLo[2] = 0.45f;
        out.probeStep[0] = 0.9f;
        out.probeStep[1] = 0.9f;
        out.probeStep[2] = 0.92f;
        out.probeRes = res;
        out.probeMips = mips;
        size_t perProbe = 0;
        for (int m = 0; m < mips; ++m) perProbe += size_t(6) * std::max(1, res >> m) * std::max(1, res >> m);
        const int np = nx * ny * nz;
        out.probes.assign(perProbe * np * 4, toHalf(0.0f));
        // one launch a mip for every probe (z = probe * 6 + face): a probe's small mips alone were a few hundred
        // threads, the card idle; samples a launch so a launch stays well under the driver's 2 s watchdog
        float4* dOut = nullptr;
        CK(cudaMalloc(reinterpret_cast<void**>(&dOut), size_t(6) * np * res * res * sizeof(float4)));
        std::vector<float4> hOut(size_t(6) * np * res * res);
        sbt.raygenRecord = rec[4];
        prm.probePos = make_float3(out.probeLo[0], out.probeLo[1], out.probeLo[2]);
        prm.probeStep = make_float3(out.probeStep[0], out.probeStep[1], out.probeStep[2]);
        prm.probeDims = make_uint3(unsigned(nx), unsigned(ny), unsigned(nz));
        std::vector<float> valid(np, 1.0f);
        size_t mipOff = 0;
        for (int m = 0; m < mips; ++m) {
            const int r = std::max(1, res >> m);
            const size_t n = size_t(6) * r * r, all = n * np;
            CK(cudaMemset(dOut, 0, all * sizeof(float4)));
            prm.probeOut = dOut;
            prm.probeRes = unsigned(r);
            prm.probeRough = float(m) / float(mips - 1);
            // the sharp mip shows its noise on glass; the rough ones are few texels but each a wide lobe (at 128 a
            // satin aluminium door wore blotches): 2x, 4x, 8x
            const unsigned int total = unsigned(o.probeSamples * (m == 0 ? 2 : (m == 1 ? 4 : 8)));
            const unsigned int per = unsigned(std::clamp(64.0 * 98304.0 / double(all), 1.0, 64.0));
            for (unsigned int s = 0; s < total;) {
                prm.sampleBase = s;
                prm.samples = std::min(total - s, per);
                CK(cudaMemcpyAsync(dPrm, &prm, sizeof(prm), cudaMemcpyHostToDevice, stream));
                OK(optixLaunch(pipe, stream, CUdeviceptr(dPrm), sizeof(DzBakeParams), &sbt, unsigned(r), unsigned(r), unsigned(6 * np)));
                CK(cudaStreamSynchronize(stream));
                s += prm.samples;
            }
            CK(cudaMemcpy(hOut.data(), dOut, all * sizeof(float4), cudaMemcpyDeviceToHost));
            for (int p = 0; p < np; ++p) {
                const float4* src = &hOut[n * p];
                if (m == 0) {   // a probe inside a part sees mostly back faces: it is not used
                    double backs = 0.0;
                    for (size_t i = 0; i < n; ++i) backs += src[i].w;
                    if (backs / (double(n) * total) > 0.2) valid[p] = 0.0f;
                }
                for (size_t i = 0; i < n; ++i) {
                    const float inv = m == 0 ? 1.0f / float(total) : (src[i].w > 0.0f ? 1.0f / src[i].w : 0.0f);
                    uint16_t* d = &out.probes[(perProbe * p + mipOff + i) * 4];
                    d[0] = toHalf(src[i].x * inv);
                    d[1] = toHalf(src[i].y * inv);
                    d[2] = toHalf(src[i].z * inv);
                    d[3] = toHalf(valid[p]);
                }
            }
            mipOff += n;
            say(0.97f + 0.02f * float(m + 1) / mips, "yansima yoklamalari");
        }
        int invalid = 0;
        for (float v : valid) invalid += v == 0.0f ? 1 : 0;
        cudaFree(dOut);
        std::printf("yoklama: %d (%d gecersiz: bir parcanin icinde), %d px, %d mip, %d ornek\n", np, invalid, res, mips, o.probeSamples);
    }

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
    f.write(reinterpret_cast<const char*>(lm.volDim), sizeof(lm.volDim));
    f.write(reinterpret_cast<const char*>(lm.volLo), sizeof(lm.volLo));
    f.write(reinterpret_cast<const char*>(&lm.volCell), sizeof(lm.volCell));
    for (int k = 0; k < 2; ++k) f.write(reinterpret_cast<const char*>(lm.vol[k].data()), std::streamsize(lm.vol[k].size() * 2));
    f.write(reinterpret_cast<const char*>(lm.probeDims), sizeof(lm.probeDims));
    f.write(reinterpret_cast<const char*>(lm.probeLo), sizeof(lm.probeLo));
    f.write(reinterpret_cast<const char*>(lm.probeStep), sizeof(lm.probeStep));
    f.write(reinterpret_cast<const char*>(&lm.probeRes), sizeof(lm.probeRes));
    f.write(reinterpret_cast<const char*>(&lm.probeMips), sizeof(lm.probeMips));
    const uint64_t pn = lm.probes.size();
    f.write(reinterpret_cast<const char*>(&pn), 8);
    f.write(reinterpret_cast<const char*>(lm.probes.data()), std::streamsize(pn * 2));
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
    f.read(reinterpret_cast<char*>(lm.volDim), sizeof(lm.volDim));
    f.read(reinterpret_cast<char*>(lm.volLo), sizeof(lm.volLo));
    f.read(reinterpret_cast<char*>(&lm.volCell), sizeof(lm.volCell));
    const size_t nv = size_t(lm.volDim[0]) * lm.volDim[1] * lm.volDim[2];
    for (int k = 0; k < 2; ++k) {
        lm.vol[k].resize(nv * 4);
        f.read(reinterpret_cast<char*>(lm.vol[k].data()), std::streamsize(lm.vol[k].size() * 2));
    }
    f.read(reinterpret_cast<char*>(lm.probeDims), sizeof(lm.probeDims));
    f.read(reinterpret_cast<char*>(lm.probeLo), sizeof(lm.probeLo));
    f.read(reinterpret_cast<char*>(lm.probeStep), sizeof(lm.probeStep));
    f.read(reinterpret_cast<char*>(&lm.probeRes), sizeof(lm.probeRes));
    f.read(reinterpret_cast<char*>(&lm.probeMips), sizeof(lm.probeMips));
    uint64_t pn = 0;
    f.read(reinterpret_cast<char*>(&pn), 8);
    lm.probes.resize(size_t(pn));
    f.read(reinterpret_cast<char*>(lm.probes.data()), std::streamsize(pn * 2));
    return bool(f);
}

}   // namespace dz
