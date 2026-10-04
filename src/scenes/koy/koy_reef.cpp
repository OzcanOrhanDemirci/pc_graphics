// Scene 5 "Koy", Ada, enriched (see koy_reef.h).
#include "scenes/koy/koy_reef.h"

#include "scenes/koy/koy_noise.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <random>
#include <thread>

namespace {
float sstep(float a, float b, float x) {
    const float t = std::min(std::max((x - a) / (b - a), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
float segDist(float3 p, float3 a, float3 b) {   // horizontal distance from p to the segment a-b
    const float2 ab = make_float2(b.x - a.x, b.z - a.z), ap = make_float2(p.x - a.x, p.z - a.z);
    const float t = std::min(std::max((ap.x * ab.x + ap.y * ab.y) / std::max(ab.x * ab.x + ab.y * ab.y, 1e-6f), 0.0f), 1.0f);
    const float dx = ap.x - ab.x * t, dz = ap.y - ab.y * t;
    return std::sqrt(dx * dx + dz * dz);
}
unsigned char u8(float v) { return (unsigned char)std::lround(std::min(std::max(v, 0.0f), 1.0f) * 255.0f); }
}   // namespace

KoyFloorMask koyBuildFloorMask(const Config& cfg, const std::function<float(float, float)>& floorY,
                               const std::vector<std::function<float(float3)>>& rocks, float3 wreckBow, float3 wreckStern,
                               std::string* log) {
    const auto t0 = std::chrono::steady_clock::now();
    KoyFloorMask M;
    const float3 lo = cfg.v3("floor2.min", make_float3(-75.0f, 0.0f, -205.0f)), hi = cfg.v3("floor2.max", make_float3(75.0f, 0.0f, 45.0f));
    const float cell = cfg.f("floor2.cell", 0.25f);
    M.w = int(std::ceil((hi.x - lo.x) / cell));
    M.h = int(std::ceil((hi.z - lo.z) / cell));
    M.min = make_float2(lo.x, lo.z);
    M.size = make_float2(M.w * cell, M.h * cell);
    M.px.assign(size_t(M.w) * M.h, make_uchar4(0, 0, 0, 255));
    const float cover = cfg.f("floor2.cover", 0.42f);   // about this share of the floor is shell-strewn
    // rows in parallel: every texel its own (deterministic)
    const int nThreads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::future<void>> jobs;
    for (int t = 0; t < nThreads; ++t)
        jobs.push_back(std::async(std::launch::async, [&, t] {
            for (int j = t; j < M.h; j += nThreads)
                for (int i = 0; i < M.w; ++i) {
                    const float x = lo.x + (i + 0.5f) * cell, z = lo.z + (j + 0.5f) * cell;
                    const float y = floorY(x, z);
                    const float depth = -y;
                    // the nearest rock's distance at the floor (its foot)
                    float dRock = 1e9f;
                    for (const auto& f : rocks) dRock = std::min(dRock, f(make_float3(x, y + 0.15f, z)));
                    // the shell-strewn sand: big patches the swell and the currents sort, smaller ones within; more at a
                    // rock's foot (shells, broken urchin tests, coralline grit fall there), fewer deep
                    const float big = kn::fbm3(x * 0.045f + 3.1f, 0.7f, z * 0.045f, 3);
                    const float small = kn::fbm3(x * 0.21f, 4.2f, z * 0.21f + 7.0f, 2);
                    float v = 0.6f * big + 0.28f * small + (cover - 0.42f) * 2.0f;
                    v += 0.5f * sstep(3.0f, 0.3f, dRock);
                    v -= 0.25f * sstep(22.0f, 40.0f, depth);
                    const float shell = sstep(0.02f, 0.32f, v);
                    // silt: about the wreck (its hull slows the water, the fine stuff settles in its lee), and the deep
                    const float dW = segDist(make_float3(x, 0, z), wreckBow, wreckStern);
                    float silt = 0.85f * sstep(13.0f, 3.5f, dW) * (0.7f + 0.3f * kn::fbm3(x * 0.3f, 1.9f, z * 0.3f, 2));
                    silt = std::max(silt, 0.35f * sstep(26.0f, 45.0f, depth));
                    // a rock's foot: the coarse, darker grit within a metre or so
                    const float foot = sstep(1.4f, 0.15f, dRock) * (0.75f + 0.25f * kn::noise3(x * 2.0f, 0.3f, z * 2.0f));
                    // the texture's border fades to nothing (outside it the shader reads zero)
                    const float edge = sstep(0.0f, 6.0f, std::min(std::min(x - lo.x, hi.x - x), std::min(z - lo.z, hi.z - z)));
                    M.px[size_t(j) * M.w + i] = make_uchar4(u8(shell * edge), u8(silt * edge), u8(foot * edge), 255);
                }
        }));
    for (auto& j : jobs) j.get();
    if (log) {
        size_t s = 0;
        for (const uchar4& q : M.px) s += q.x > 127 ? 1 : 0;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%d x %d hucre (%.2f m), kabuklu kum %%%.0f, %.2f s", M.w, M.h, cell, 100.0 * double(s) / double(M.px.size()),
                      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        *log = buf;
    }
    return M;
}

float KoyReefRocks::sdf(float3 p) const {
    float d = 1e9f;
    for (const Cluster& C : clusters) {
        const float far = length(p - C.c) - C.r;
        if (far > 0.5f) { d = std::min(d, far); continue; }
        d = std::min(d, C.field.sdf(p));
    }
    return d;
}

KoyReefRocks koyReefRocks(const Config& cfg, const std::function<float(float, float)>& floorY, const KoyReefAvoid& avoid) {
    KoyReefRocks out;
    std::mt19937 rng(unsigned(cfg.i("reef.seed", 41)));
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const float lane = cfg.f("buoy.half", 15.0f);
    const float zTop = cfg.f("reef.z_top", 18.0f), zEnd = cfg.f("reef.z_end", -175.0f);
    // where a cluster may not stand: the island's rock, under and round the pier, the amphorae's own nest, the wreck's
    // hull and its scour, the mooring block, the buoy lines' ropes
    auto blocked = [&](float x, float z, float rad) {
        const float y = floorY(x, z);
        if (avoid.island && avoid.island(make_float3(x, y + 0.3f, z)) < rad + 1.5f) return true;
        if (x > -14.0f - rad && x < 14.0f + rad && z > -7.0f - rad && z < 34.0f + rad) return true;
        const float da = std::hypot(x - avoid.amphoraCentre.x, z - avoid.amphoraCentre.z);
        if (da < 10.0f + rad) return true;
        {   // the wreck
            const float2 ab = make_float2(avoid.wreckStern.x - avoid.wreckBow.x, avoid.wreckStern.z - avoid.wreckBow.z);
            const float2 ap = make_float2(x - avoid.wreckBow.x, z - avoid.wreckBow.z);
            const float t = std::min(std::max((ap.x * ab.x + ap.y * ab.y) / std::max(ab.x * ab.x + ab.y * ab.y, 1e-6f), 0.0f), 1.0f);
            if (std::hypot(ap.x - ab.x * t, ap.y - ab.y * t) < 8.0f + rad) return true;
        }
        if (std::hypot(x, z + 160.0f) < 5.0f + rad) return true;                       // the mooring block
        if (std::fabs(std::fabs(x) - lane) < 1.6f + rad && z < 26.0f) return true;       // the buoy lines' ropes
        if (std::fabs(z + 160.0f) < 1.6f + rad && std::fabs(x) < lane + 1.0f) return true;
        return false;
    };
    auto addCluster = [&](float cx, float cz, float scale, int nBlocks, int region) {
        KoyReefRocks::Cluster C;
        C.region = region;
        C.cell = region == 0 ? cfg.f("reef.cell_lane", 0.07f) : region == 1 ? cfg.f("reef.cell_near", 0.11f) : cfg.f("reef.cell_far", 0.18f);
        float3 lo = make_float3(1e9f, 1e9f, 1e9f), hi = make_float3(-1e9f, -1e9f, -1e9f);
        // a few big blocks in the middle, smaller ones about them (the outcrop broke up where it stood)
        for (int k = 0; k < nBlocks; ++k) {
            KoyRockField::Rock R;
            const bool core = k < std::max(1, nBlocks / 3);
            const float a = U(rng) * 6.2831853f, rad = scale * (core ? 0.6f * U(rng) : 0.8f + 1.4f * U(rng));
            const float x = cx + std::cos(a) * rad * 1.3f, z = cz + std::sin(a) * rad;
            R.r = scale * (core ? 0.55f + 0.45f * U(rng) : 0.18f + 0.35f * U(rng) * U(rng));
            R.seed = U(rng) * 10.0f;
            R.c = make_float3(x, floorY(x, z) + (core ? 0.05f : 0.15f) * R.r, z);   // half sunk: the sand drifted about
            koyMakeBlock(R, rng);
            if (core && U(rng) < 0.65f) {   // limestone breaks in beds: a flat, tilted top (a slab)
                R.n[9] = normalize(make_float3((U(rng) - 0.5f) * 0.35f, 1.0f, (U(rng) - 0.5f) * 0.35f));
                R.d[9] = R.r * (0.32f + 0.25f * U(rng));
            }
            C.field.rocks.push_back(R);
            lo = fminf3(lo, R.c - make_float3(1.6f * R.r, 1.6f * R.r, 1.6f * R.r));
            hi = fmaxf3(hi, R.c + make_float3(1.6f * R.r, 1.6f * R.r, 1.6f * R.r));
        }
        C.c = (lo + hi) * 0.5f;
        C.r = 0.5f * length(hi - lo) + 0.3f;
        out.clusters.push_back(C);
    };
    // the lane: low outcrops along its sides (a rocky edge the swimming area was laid between), a few between the
    // stations; in counts per stretch of the lane
    int tries = 0;
    auto place = [&](int want, float x0, float x1, float z0, float z1, float s0, float s1, int b0, int b1, int region) {
        int got = 0;
        for (int t = 0; t < want * 30 && got < want; ++t, ++tries) {
            const float x = x0 + (x1 - x0) * U(rng), z = z0 + (z1 - z0) * U(rng);
            const float sc = s0 + (s1 - s0) * U(rng) * U(rng);
            if (blocked(x, z, 2.0f * sc)) continue;
            bool near = false;   // clusters keep their distance
            for (const auto& C : out.clusters) near = near || std::hypot(C.c.x - x, C.c.z - z) < C.r + 2.2f * sc + 2.0f;
            if (near) continue;
            addCluster(x, z, sc, b0 + int(U(rng) * float(b1 - b0 + 1)), region);
            ++got;
        }
        return got;
    };
    const int kLaneSide = cfg.i("reef.lane_side", 26), kLaneMid = cfg.i("reef.lane_mid", 9);
    place(kLaneSide / 2, -lane + 1.5f, -6.0f, zEnd + 15.0f, zTop - 10.0f, 0.8f, 2.0f, 6, 13, 0);
    place(kLaneSide - kLaneSide / 2, 6.0f, lane - 1.5f, zEnd + 15.0f, zTop - 10.0f, 0.8f, 2.0f, 6, 13, 0);
    place(kLaneMid, -5.0f, 5.0f, zEnd + 15.0f, -10.0f, 0.5f, 1.2f, 3, 7, 0);
    // outside: up to 50 m out, fewer with distance, bigger (they must read through the haze)
    const int kNear = cfg.i("reef.near", 34), kFar = cfg.i("reef.far", 22);
    place(kNear / 2, -32.0f, -lane - 1.0f, zEnd, zTop + 6.0f, 1.0f, 2.6f, 5, 12, 1);
    place(kNear - kNear / 2, lane + 1.0f, 32.0f, zEnd, zTop + 6.0f, 1.0f, 2.6f, 5, 12, 1);
    place(kFar / 2, -55.0f, -32.0f, zEnd, zTop + 10.0f, 1.5f, 3.5f, 4, 9, 2);
    place(kFar - kFar / 2, 32.0f, 55.0f, zEnd, zTop + 10.0f, 1.5f, 3.5f, 4, 9, 2);
    return out;
}

void koyBuildReefRocks(const KoyReefRocks& r, unsigned int material, unsigned int id, std::vector<KMeshCPU>& out, std::string* log) {
    const auto t0 = std::chrono::steady_clock::now();
    // every cluster meshed in parallel, then merged by region
    std::vector<KMeshCPU> parts(r.clusters.size());
    std::vector<std::future<void>> jobs;
    const int nThreads = std::max(1u, std::thread::hardware_concurrency());
    for (int t = 0; t < nThreads; ++t)
        jobs.push_back(std::async(std::launch::async, [&, t] {
            for (size_t k = size_t(t); k < r.clusters.size(); k += size_t(nThreads)) {
                const auto& C = r.clusters[k];
                parts[k] = koyBuildRockField(C.field, C.cell, material, id, nullptr);
            }
        }));
    for (auto& j : jobs) j.get();
    const char* names[3] = {"kayaliklar (koridor)", "kayaliklar (yakin)", "kayaliklar (uzak)"};
    size_t tris = 0;
    for (int reg = 0; reg < 3; ++reg) {
        KMeshCPU m;
        m.name = names[reg];
        m.material = material;
        m.id = id;
        for (size_t k = 0; k < parts.size(); ++k) {
            if (r.clusters[k].region != reg) continue;
            const KMeshCPU& q = parts[k];
            const unsigned base = unsigned(m.pos.size());
            m.pos.insert(m.pos.end(), q.pos.begin(), q.pos.end());
            m.nrm.insert(m.nrm.end(), q.nrm.begin(), q.nrm.end());
            if (!q.uv.empty()) m.uv.insert(m.uv.end(), q.uv.begin(), q.uv.end());
            for (const uint3& t : q.idx) m.idx.push_back(make_uint3(t.x + base, t.y + base, t.z + base));
        }
        if (m.uv.size() != m.pos.size()) m.uv.clear();
        tris += m.idx.size();
        if (!m.idx.empty()) out.push_back(std::move(m));
    }
    if (log) {
        int n[3] = {0, 0, 0}, blocks = 0;
        for (const auto& C : r.clusters) { ++n[C.region]; blocks += int(C.field.rocks.size()); }
        char buf[200];
        std::snprintf(buf, sizeof(buf), "%d kume (koridor %d, yakin %d, uzak %d), %d blok, %zu ucgen, %.2f s", int(r.clusters.size()), n[0], n[1], n[2], blocks,
                      tris, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        *log = buf;
    }
}
