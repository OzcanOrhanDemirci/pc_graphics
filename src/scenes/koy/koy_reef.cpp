// Scene 5 "Koy", Ada, enriched (see koy_reef.h).
#include "scenes/koy/koy_reef.h"

#include "scenes/koy/koy_buoys.h"
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

namespace {
// an icosphere of radius 1 (level 1: 80 faces), for the encrusting blobs
void ico1(std::vector<float3>& v, std::vector<uint3>& f) {
    const float t = 1.6180340f;
    v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& p : v) p = normalize(p);
    f = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
         {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    std::vector<uint3> g;
    std::vector<float3> w = v;
    auto mid = [&](unsigned a, unsigned b) { w.push_back(normalize((w[a] + w[b]) * 0.5f)); return unsigned(w.size() - 1); };
    for (const uint3& q : f) {
        const unsigned a = mid(q.x, q.y), b = mid(q.y, q.z), c = mid(q.z, q.x);
        g.push_back(make_uint3(q.x, a, c));
        g.push_back(make_uint3(q.y, b, a));
        g.push_back(make_uint3(q.z, c, b));
        g.push_back(make_uint3(a, b, c));
    }
    v = w;
    f = g;
}
}   // namespace

void koyFoulWreck(const Config& cfg, const KMeshCPU& wood, const KMeshCPU& iron, const KoyFoulMaterials& mat, unsigned int id,
                  std::vector<KMeshCPU>& out, std::string* log) {
    std::mt19937 rng(unsigned(cfg.i("foul.seed", 77)));
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    KMeshCPU mussels, orange, yellow, squirt, crust, hydro;
    mussels.name = "batik (midyeler)";
    orange.name = "batik (turuncu sungerler)";
    yellow.name = "batik (sari sungerler)";
    squirt.name = "batik (deniz kadehleri)";
    crust.name = "batik (kabuk topaklari)";
    hydro.name = "batik (hidroidler)";
    hydro.material = mat.hydroid;
    hydro.id = id;
    mussels.material = mat.mussel;
    orange.material = mat.spongeOrange;
    yellow.material = mat.spongeYellow;
    squirt.material = mat.squirt;
    crust.material = mat.crust;
    mussels.id = orange.id = yellow.id = squirt.id = crust.id = id;
    // the surfaces, by area: a triangle's chance of a site goes with its area and with where the current runs over it
    // (edges and tops: a facing up or out; less on faces looking down, none inside the sand)
    struct Site { float3 p, n; };
    auto collect = [&](const KMeshCPU& m, std::vector<float>& cdf, std::vector<Site>& tri) {
        float acc = 0.0f;
        for (const uint3& t : m.idx) {
            const float3 a = m.pos[t.x], b = m.pos[t.y], c = m.pos[t.z];
            float3 n = cross(b - a, c - a);
            const float area = 0.5f * length(n);
            if (area < 1e-6f) continue;
            n = n * (1.0f / (2.0f * area));
            if (!m.nrm.empty()) {   // the mesh's own outward normal
                const float3 sn = m.nrm[t.x] + m.nrm[t.y] + m.nrm[t.z];
                if (dot(sn, n) < 0.0f) n = n * -1.0f;
            }
            const float w = area * (n.y > -0.3f ? 1.0f : 0.25f) * (0.6f + 0.4f * std::max(n.y, 0.0f));
            acc += w;
            cdf.push_back(acc);
            tri.push_back({(a + b + c) * (1.0f / 3.0f), n});
        }
    };
    std::vector<float> cdfW, cdfI, cdfT;
    std::vector<Site> triW, triI, triT;
    collect(wood, cdfW, triW);
    collect(iron, cdfI, triI);
    {   // the tops of the wood: what faces up and stands clear (the current runs over it: mussels, hydroids)
        float acc = 0.0f;
        for (size_t k = 0; k < triW.size(); ++k) {
            if (triW[k].n.y < 0.55f) continue;
            const float w = (cdfW[k] - (k ? cdfW[k - 1] : 0.0f));
            acc += w;
            cdfT.push_back(acc);
            triT.push_back(triW[k]);
        }
    }
    auto pick = [&](const std::vector<float>& cdf, const std::vector<Site>& tri) -> Site {
        const float r = U(rng) * cdf.back();
        const size_t k = size_t(std::lower_bound(cdf.begin(), cdf.end(), r) - cdf.begin());
        return tri[std::min(k, tri.size() - 1)];
    };
    std::vector<float3> sph;
    std::vector<uint3> sphF;
    ico1(sph, sphF);
    auto blob = [&](KMeshCPU& m, float3 p, float3 n, float r, float flat, float lump, unsigned seed) {
        // a low dome on the surface (an encrusting sponge, a crust of tube worms and coralline), lumpy
        const float3 t = normalize(cross(std::fabs(n.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0), n)), b = cross(n, t);
        const unsigned base = unsigned(m.pos.size());
        const float o = float(seed % 97) * 1.7f;
        for (size_t i = 0; i < sph.size(); ++i) {
            const float3 d = sph[i];
            const float l = 1.0f + lump * kn::fbm3(d.x * 2.2f + o, d.y * 2.2f, d.z * 2.2f, 2);
            const float h = std::max(d.y, -0.15f) * flat;   // flattened against the wood, a little sunk into it
            m.pos.push_back(p + (t * (d.x * r * l)) + (b * (d.z * r * l)) + n * (h * r * l));
            m.nrm.push_back(normalize(t * d.x + b * d.z + n * (d.y / std::max(flat, 0.2f))));
        }
        for (const uint3& f : sphF) m.idx.push_back(make_uint3(base + f.x, base + f.y, base + f.z));
    };
    // mussel clumps: a site, then a clump of 6-30 about it, each its own axis (hinge to the wood, the shell standing out)
    int nMussel = 0, clumps = 0;
    const int wantClumps = cfg.i("foul.mussel_clumps", 480);
    for (int c = 0; c < wantClumps && !triW.empty(); ++c) {
        const Site S = (!triT.empty() && U(rng) < 0.6f) ? pick(cdfT, triT) : pick(cdfW, triW);
        if (S.n.y < -0.5f) continue;
        const bool beard = U(rng) < 0.35f;   // some clumps grow into beards: dozens, layered, hanging off the edge
        const int n = beard ? 30 + int(U(rng) * 40.0f) : 6 + int(U(rng) * 18.0f);
        const float rad = beard ? 0.12f + 0.14f * U(rng) : 0.05f + 0.1f * U(rng);
        const float3 t = normalize(cross(std::fabs(S.n.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0), S.n)), b = cross(S.n, t);
        for (int k = 0; k < n; ++k) {
            const float a = U(rng) * 6.2831853f, r = rad * std::sqrt(U(rng));
            const float3 at = S.p + t * (std::cos(a) * r) + b * (std::sin(a) * r) - S.n * 0.005f + S.n * (beard ? 0.03f * U(rng) : 0.0f);
            const float3 axis = normalize(S.n * (0.6f + 0.6f * U(rng)) + t * (U(rng) - 0.5f) + b * (U(rng) - 0.5f));
            const float len = 0.035f + 0.04f * U(rng);
            koyAddMussel(mussels, at, axis, t * (U(rng) - 0.5f) + b, len, unsigned(c * 131 + k));
            ++nMussel;
        }
        ++clumps;
    }
    // encrusting life: sponges (orange Crambe crambe, yellow Agelas oroides), red sea squirts (Halocynthia papillosa),
    // grey crusts (worm tubes, bryozoans, coralline under silt); on wood and on iron
    int nBlobs = 0;
    const int wantBlobs = cfg.i("foul.blobs", 900);
    for (int k = 0; k < wantBlobs; ++k) {
        const bool onIron = !triI.empty() && U(rng) < 0.22f;
        if (!onIron && triW.empty()) break;
        const Site S = onIron ? pick(cdfI, triI) : ((!triT.empty() && U(rng) < 0.5f) ? pick(cdfT, triT) : pick(cdfW, triW));
        const float kind = U(rng);
        if (kind < 0.18f) blob(orange, S.p, S.n, 0.03f + 0.07f * U(rng), 0.3f, 0.55f, unsigned(k));
        else if (kind < 0.30f) blob(yellow, S.p, S.n, 0.03f + 0.07f * U(rng), 0.5f, 0.4f, unsigned(k));
        else if (kind < 0.40f) blob(squirt, S.p, S.n, 0.025f + 0.03f * U(rng), 1.6f, 0.25f, unsigned(k));   // a squat barrel standing out
        else blob(crust, S.p, S.n, 0.03f + 0.07f * U(rng), 0.35f, 0.8f, unsigned(k));
        ++nBlobs;
    }
    int nHydro = 0;
    const int wantTufts = cfg.i("foul.hydroid_tufts", 220);
    for (int c = 0; c < wantTufts && !triT.empty(); ++c) {
        const Site S = pick(cdfT, triT);
        const int n = 2 + int(U(rng) * 5.0f);
        const float3 t = normalize(cross(std::fabs(S.n.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0), S.n)), bb = cross(S.n, t);
        for (int k = 0; k < n; ++k) {
            const float a = U(rng) * 6.2831853f, r = 0.12f * std::sqrt(U(rng));
            const float3 base = S.p + t * (std::cos(a) * r) + bb * (std::sin(a) * r) - S.n * 0.004f;
            const float H = 0.10f + 0.18f * U(rng);
            struct Br { float3 p, d; float len, r; int gen; };
            std::vector<Br> st = {{base, normalize(S.n + make_float3(0, 0.6f, 0)), H * 0.45f, 0.006f, 0}};
            while (!st.empty()) {
                const Br q = st.back();
                st.pop_back();
                const float3 e = q.p + q.d * q.len;
                // a short tapered tube (6 sides)
                const float3 u = normalize(cross(std::fabs(q.d.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0), q.d)), v = cross(q.d, u);
                const unsigned b0 = unsigned(hydro.pos.size());
                for (int ring = 0; ring < 2; ++ring)
                    for (int i = 0; i < 6; ++i) {
                        const float ph = 6.2831853f * float(i) / 6.0f;
                        const float rr = ring ? q.r * 0.7f : q.r;
                        const float3 off = u * (std::cos(ph) * rr) + v * (std::sin(ph) * rr);
                        hydro.pos.push_back((ring ? e : q.p) + off);
                        hydro.nrm.push_back(normalize(off));
                    }
                for (int i = 0; i < 6; ++i) {
                    const unsigned a0 = b0 + unsigned(i), a1 = b0 + unsigned((i + 1) % 6), c0 = a0 + 6, c1 = a1 + 6;
                    hydro.idx.push_back(make_uint3(a0, a1, c1));
                    hydro.idx.push_back(make_uint3(a0, c1, c0));
                }
                if (q.gen < 3) {
                    const int kids = 2 + (U(rng) < 0.4f ? 1 : 0);
                    for (int j = 0; j < kids; ++j) {
                        const float3 dj = normalize(q.d * 1.2f + u * ((U(rng) - 0.5f) * 1.3f) + v * ((U(rng) - 0.5f) * 1.3f));
                        st.push_back({e, dj, q.len * (0.62f + 0.15f * U(rng)), q.r * 0.75f, q.gen + 1});
                    }
                }
            }
            ++nHydro;
        }
    }
    for (KMeshCPU* m : {&mussels, &orange, &yellow, &squirt, &crust, &hydro}) {
        m->aoRange = 0.6f;
        if (!m->idx.empty()) out.push_back(std::move(*m));
    }
    if (log) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%d midye (%d kume), %d sunger ve kabuk topagi, %d hidroid", nMussel, clumps, nBlobs, nHydro);
        *log = buf;
    }
}
