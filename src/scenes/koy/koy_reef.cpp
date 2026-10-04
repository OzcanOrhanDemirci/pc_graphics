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
uint64_t cellKey(int x, int y, int z) {
    return (uint64_t(uint32_t(x + (1 << 20)) & 0x1fffff) << 42) | (uint64_t(uint32_t(y + (1 << 20)) & 0x1fffff) << 21) |
           uint64_t(uint32_t(z + (1 << 20)) & 0x1fffff);
}
// the point of triangle abc nearest p (Ericson, Real-Time Collision Detection 5.1.5)
float3 closestOnTri(float3 p, float3 a, float3 b, float3 c) {
    const float3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    const float3 bp = p - b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));
    const float3 cp = p - c;
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float den = 1.0f / (va + vb + vc);
    return a + ab * (vb * den) + ac * (vc * den);
}
}   // namespace

int KoySurfaceSnap::cellOf(float v) const { return int(std::floor(v / cell_)); }

KoySurfaceSnap::KoySurfaceSnap(const std::vector<const KMeshCPU*>& meshes, float cell, const std::function<bool(float3)>& keep) : cell_(cell) {
    for (const KMeshCPU* m : meshes)
        for (const uint3& t : m->idx) {
            const float3 a = m->pos[t.x], b = m->pos[t.y], c = m->pos[t.z];
            float3 n = cross(b - a, c - a);
            const float l = length(n);
            if (l < 1e-9f) continue;
            if (keep && !keep((a + b + c) * (1.0f / 3.0f))) continue;
            n = n * (1.0f / l);
            if (!m->nrm.empty() && dot(m->nrm[t.x] + m->nrm[t.y] + m->nrm[t.z], n) < 0.0f) n = n * -1.0f;
            const unsigned k = unsigned(a_.size());
            a_.push_back(a);
            b_.push_back(b);
            c_.push_back(c);
            n_.push_back(n);
            const float3 lo = fminf3(a, fminf3(b, c)), hi = fmaxf3(a, fmaxf3(b, c));
            for (int z = cellOf(lo.z); z <= cellOf(hi.z); ++z)
                for (int y = cellOf(lo.y); y <= cellOf(hi.y); ++y)
                    for (int x = cellOf(lo.x); x <= cellOf(hi.x); ++x) grid_[cellKey(x, y, z)].push_back(k);
        }
}

bool KoySurfaceSnap::nearest(float3 p, float maxD, float3& q, float3& n, float* dist) const {
    float best = maxD * maxD;
    bool found = false;
    const int r = int(std::ceil(maxD / cell_));
    const int cx = cellOf(p.x), cy = cellOf(p.y), cz = cellOf(p.z);
    for (int z = cz - r; z <= cz + r; ++z)
        for (int y = cy - r; y <= cy + r; ++y)
            for (int x = cx - r; x <= cx + r; ++x) {
                const auto it = grid_.find(cellKey(x, y, z));
                if (it == grid_.end()) continue;
                for (const unsigned k : it->second) {
                    const float3 c = closestOnTri(p, a_[k], b_[k], c_[k]);
                    const float d2 = dot(c - p, c - p);
                    if (d2 < best) {
                        best = d2;
                        q = c;
                        n = n_[k];
                        found = true;
                    }
                }
            }
    if (found && dist) *dist = std::sqrt(best);
    return found;
}

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
            for (const auto& C : out.clusters) near = near || std::hypot(C.c.x - x, C.c.z - z) < C.r + 2.0f * sc + 1.0f;
            if (near) continue;
            addCluster(x, z, sc, b0 + int(U(rng) * float(b1 - b0 + 1)), region);
            ++got;
        }
        return got;
    };
    const int kLaneSide = cfg.i("reef.lane_side", 40), kLaneMid = cfg.i("reef.lane_mid", 14);
    place(kLaneSide / 2, -lane + 1.5f, -6.0f, zEnd + 15.0f, zTop - 10.0f, 0.8f, 2.0f, 6, 13, 0);
    place(kLaneSide - kLaneSide / 2, 6.0f, lane - 1.5f, zEnd + 15.0f, zTop - 10.0f, 0.8f, 2.0f, 6, 13, 0);
    place(kLaneMid, -5.0f, 5.0f, zEnd + 15.0f, -10.0f, 0.5f, 1.2f, 3, 7, 0);
    place(cfg.i("reef.lane_boulders", 16), -lane + 2.0f, lane - 2.0f, zEnd + 15.0f, -8.0f, 0.7f, 1.3f, 1, 2, 0);   // lone boulders
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
        m.scatter = true;
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
    // the wood and the iron as they are (a tuft or a clump spreads about its site: each piece is put down on what is there)
    const KoySurfaceSnap snap({&wood, &iron}, 0.25f);
    int nMussel = 0, clumps = 0, musselOff = 0;
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
            const float lift = beard ? 0.03f * U(rng) : 0.0f;   // a beard's shells layered on the others
            float3 mq, sn;
            if (!snap.nearest(S.p + t * (std::cos(a) * r) + b * (std::sin(a) * r), 0.04f, mq, sn)) {
                ++musselOff;
                continue;
            }
            const float3 at = mq - sn * 0.005f + sn * lift;
            const float3 axis = normalize(sn * (0.6f + 0.6f * U(rng)) + t * (U(rng) - 0.5f) + b * (U(rng) - 0.5f));
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
    int nHydro = 0, hydroOff = 0;
    const int wantTufts = cfg.i("foul.hydroid_tufts", 220);
    for (int c = 0; c < wantTufts && !triT.empty(); ++c) {
        const Site S = pick(cdfT, triT);
        const int n = 2 + int(U(rng) * 5.0f);
        const float3 t = normalize(cross(std::fabs(S.n.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0), S.n)), bb = cross(S.n, t);
        for (int k = 0; k < n; ++k) {
            const float a = U(rng) * 6.2831853f, r = 0.12f * std::sqrt(U(rng));
            const float H = 0.10f + 0.18f * U(rng);
            float3 hq, sn;
            if (!snap.nearest(S.p + t * (std::cos(a) * r) + bb * (std::sin(a) * r), 0.04f, hq, sn) || sn.y < -0.2f) {
                ++hydroOff;   // past the plank's edge: no wood under it
                continue;
            }
            const float3 base = hq - sn * 0.004f;
            struct Br { float3 p, d; float len, r; int gen; };
            std::vector<Br> st = {{base, normalize(sn + make_float3(0, 0.6f, 0)), H * 0.45f, 0.006f, 0}};
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
        char buf[240];
        std::snprintf(buf, sizeof(buf), "%d midye (%d kume), %d sunger ve kabuk topagi, %d hidroid; yuzeyde yer bulamayip atilan %d midye, %d hidroid",
                      nMussel, clumps, nBlobs, nHydro, musselOff, hydroOff);
        *log = buf;
    }
}

namespace {
struct LifeFrame { float3 o, x, y, z; float3 at(float3 q) const { return o + x * q.x + y * q.y + z * q.z; } float3 dir(float3 q) const { return x * q.x + y * q.y + z * q.z; } };
LifeFrame lifeFrameAt(float3 o, float3 up, float yaw) {
    LifeFrame F;
    F.o = o;
    F.y = normalize(up);
    const float3 ref = std::fabs(F.y.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0);
    const float3 a = normalize(cross(ref, F.y)), c = cross(F.y, a);
    F.x = a * std::cos(yaw) + c * std::sin(yaw);
    F.z = cross(F.x, F.y);
    return F;
}
// a grid surface (nu x nv points) appended with smooth normals from its own triangles
void appendGrid(KMeshCPU& m, const std::vector<float3>& pts, int nu, int nv, bool both) {
    const unsigned base = unsigned(m.pos.size());
    std::vector<float3> nrm(pts.size(), make_float3(0, 0, 0));
    std::vector<uint3> tris;
    for (int j = 0; j + 1 < nv; ++j)
        for (int i = 0; i + 1 < nu; ++i) {
            const unsigned a = unsigned(j * nu + i), b = a + 1, c = a + unsigned(nu), d = c + 1;
            tris.push_back(make_uint3(a, b, d));
            tris.push_back(make_uint3(a, d, c));
        }
    for (const uint3& t : tris) {
        const float3 n = cross(pts[t.y] - pts[t.x], pts[t.z] - pts[t.x]);
        nrm[t.x] = nrm[t.x] + n; nrm[t.y] = nrm[t.y] + n; nrm[t.z] = nrm[t.z] + n;
    }
    for (size_t i = 0; i < pts.size(); ++i) {
        m.pos.push_back(pts[i]);
        const float l = length(nrm[i]);
        m.nrm.push_back(l > 1e-12f ? nrm[i] * (1.0f / l) : make_float3(0, 1, 0));
    }
    for (const uint3& t : tris) m.idx.push_back(make_uint3(base + t.x, base + t.y, base + t.z));
    if (both) {   // the underside: the same surface, a hair below, wound the other way
        const unsigned b2 = unsigned(m.pos.size());
        for (size_t i = 0; i < pts.size(); ++i) {
            m.pos.push_back(pts[i] - m.nrm[base + i] * 0.0015f);
            m.nrm.push_back(m.nrm[base + i] * -1.0f);
        }
        for (const uint3& t : tris) m.idx.push_back(make_uint3(b2 + t.x, b2 + t.z, b2 + t.y));
    }
}
}   // namespace

void koyBuildSandLife(const Config& cfg, const std::function<float(float, float)>& floorY, const std::function<bool(float3)>& isRock,
                      const std::function<float(float, float)>& shelly,
                      const std::vector<const KMeshCPU*>& rockMeshes, const KoyLifeMaterials& mat, unsigned int id, std::vector<KMeshCPU>& out,
                      std::string* log) {
    std::mt19937 rng(unsigned(cfg.i("life.seed", 53)));
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    KMeshCPU shells, pink, stars, padina, orange, yellow, darkS;
    shells.name = "kabuklar";
    pink.name = "kabuklar (pembe)";
    stars.name = "kum yildizlari";
    padina.name = "padina";
    orange.name = "kaya sungerleri (turuncu)";
    yellow.name = "kaya sungerleri (sari)";
    darkS.name = "kaya sungerleri (koyu)";
    darkS.material = mat.spongeDark;
    darkS.id = id;
    shells.material = mat.shell;
    pink.material = mat.shellPink;
    stars.material = mat.sandStar;
    padina.material = mat.padina;
    orange.material = mat.spongeOrange;
    yellow.material = mat.spongeYellow;
    shells.id = pink.id = stars.id = padina.id = orange.id = yellow.id = id;
    const float lane = cfg.f("buoy.half", 15.0f);
    // a spot on the sand: in the lane mostly, thinner outside it up to `out` metres (the eye's field), none under the pier
    auto spot = [&](float& x, float& z) {
        for (int t = 0; t < 60; ++t) {
            const bool inLane = U(rng) < 0.78f;
            x = inLane ? (U(rng) * 2.0f - 1.0f) * (lane - 0.5f) : (U(rng) < 0.5f ? -1.0f : 1.0f) * (lane + 0.5f + 22.0f * U(rng) * U(rng));
            z = 20.0f - 192.0f * U(rng);
            if (x > -13.0f && x < 13.0f && z > -6.0f && z < 34.0f) continue;   // the pier's hall: its own floor
            if (isRock(make_float3(x, floorY(x, z) + 0.03f, z))) continue;
            return true;
        }
        return false;
    };
    // the empty shells gather: clusters on the shell-strewn patches (the swell sorts them there), a few strays anywhere
    std::vector<float2> centres;
    for (int t = 0; t < 6000 && int(centres.size()) < cfg.i("life.shell_clusters", 260); ++t) {
        float x, z;
        if (!spot(x, z)) continue;
        if (U(rng) > 0.15f + 0.85f * shelly(x, z)) continue;
        centres.push_back(make_float2(x, z));
    }
    auto shellSpot = [&](float& x, float& z) {
        if (centres.empty() || U(rng) < 0.15f) return spot(x, z);
        const float2 c = centres[size_t(U(rng) * float(centres.size())) % centres.size()];
        for (int t = 0; t < 10; ++t) {
            const float a = U(rng) * 6.2831853f, r = 0.9f * std::sqrt(U(rng));
            x = c.x + std::cos(a) * r;
            z = c.y + std::sin(a) * r;
            if (!isRock(make_float3(x, floorY(x, z) + 0.03f, z))) return true;
        }
        return false;
    };
    // scallops: a ribbed fan, cupped, ears at the hinge; whole or broken; lying either way up
    int nScallop = 0;
    for (int k = 0; k < cfg.i("life.scallops", 900); ++k) {
        float x, z;
        if (!shellSpot(x, z)) continue;
        const float R = 0.04f + 0.06f * U(rng);
        const bool broken = U(rng) < 0.4f;
        const float th0 = broken ? -0.2f - 0.9f * U(rng) : -1.35f, th1 = broken ? 0.2f + 0.9f * U(rng) : 1.35f;
        const bool upside = U(rng) < 0.3f;
        const LifeFrame F = lifeFrameAt(make_float3(x, floorY(x, z) + 0.004f, z), make_float3((U(rng) - 0.5f) * 0.3f, 1.0f, (U(rng) - 0.5f) * 0.3f), U(rng) * 6.2831853f);
        const int nu = 18, nv = 9;
        std::vector<float3> pts;
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                const float r = R * (0.08f + 0.92f * float(j) / (nv - 1)), th = th0 + (th1 - th0) * float(i) / (nu - 1);
                const float rib = 0.035f * R * std::cos(th * 18.0f) * (r / R);
                const float cup = 0.22f * R * (1.0f - (r / R) * (r / R)) * (upside ? -1.0f : 1.0f);
                pts.push_back(F.at(make_float3(std::sin(th) * r, cup + rib + 0.01f * R, -std::cos(th) * r + 0.45f * R)));
            }
        appendGrid(U(rng) < 0.25f ? pink : shells, pts, nu, nv, true);
        ++nScallop;
    }
    // cockles: a ribbed dome, half sunk
    int nCockle = 0;
    for (int k = 0; k < cfg.i("life.cockles", 700); ++k) {
        float x, z;
        if (!shellSpot(x, z)) continue;
        const float R = 0.015f + 0.02f * U(rng);
        const LifeFrame F = lifeFrameAt(make_float3(x, floorY(x, z) - 0.25f * R, z), make_float3((U(rng) - 0.5f) * 0.6f, 1.0f, (U(rng) - 0.5f) * 0.6f), U(rng) * 6.2831853f);
        const int nu = 16, nv = 7;
        std::vector<float3> pts;
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                const float el = 1.5707963f * float(j) / (nv - 1), az = 6.2831853f * float(i) / (nu - 1);
                const float rib = 1.0f + 0.06f * std::cos(az * 11.0f) * std::cos(el);
                pts.push_back(F.at(make_float3(std::cos(az) * std::cos(el) * R * rib, std::sin(el) * R * 0.75f, std::sin(az) * std::cos(el) * R * 1.15f * rib)));
            }
        appendGrid(U(rng) < 0.3f ? pink : shells, pts, nu, nv, false);
        ++nCockle;
    }
    // tower shells: a tapering tube wound on a cone, lying on the sand
    int nTower = 0;
    for (int k = 0; k < cfg.i("life.towers", 420); ++k) {
        float x, z;
        if (!shellSpot(x, z)) continue;
        const float L = 0.04f + 0.05f * U(rng);
        const float3 axis = normalize(make_float3(std::cos(U(rng) * 6.283f), 0.15f, std::sin(U(rng) * 6.283f)));
        const LifeFrame F = lifeFrameAt(make_float3(x, floorY(x, z) + 0.12f * L, z), axis, U(rng) * 6.2831853f);
        const int nu = 10, nv = 40;
        std::vector<float3> pts;
        for (int j = 0; j < nv; ++j) {
            const float u = float(j) / (nv - 1);   // base to tip
            const float coneR = 0.14f * L * (1.0f - 0.92f * u), tubeR = 0.09f * L * (1.0f - 0.85f * u) + 0.001f;
            const float turn = u * 6.2831853f * 10.0f;
            const float3 c = make_float3(std::cos(turn) * coneR, u * L, std::sin(turn) * coneR);
            const float3 tdir = normalize(make_float3(-std::sin(turn) * coneR * 62.8f, L, std::cos(turn) * coneR * 62.8f));
            const float3 nA = normalize(make_float3(std::cos(turn), 0.0f, std::sin(turn))), nB = cross(tdir, nA);
            for (int i = 0; i < nu; ++i) {
                const float ph = 6.2831853f * float(i) / (nu - 1);
                pts.push_back(F.at(c + nA * (std::cos(ph) * tubeR) + nB * (std::sin(ph) * tubeR)));
            }
        }
        appendGrid(shells, pts, nu, nv, false);
        ++nTower;
    }
    // sand stars: flat, five straight tapering arms, a fringe of marginal plates (the outline a little scalloped)
    int nStar = 0;
    for (int k = 0; k < cfg.i("life.sand_stars", 140); ++k) {
        float x, z;
        if (!spot(x, z)) continue;
        const float R = 0.06f + 0.07f * U(rng);
        const float yaw = U(rng) * 6.2831853f;
        const float3 c0 = make_float3(x, floorY(x, z) + 0.003f, z);
        const int nRing = 80, nr = 5;
        std::vector<float3> pts;
        for (int j = 0; j < nr; ++j)
            for (int i = 0; i < nRing; ++i) {
                const float a = yaw + 6.2831853f * float(i) / (nRing - 1);
                const float arm = std::pow(std::fabs(std::cos(2.5f * (a - yaw))), 6.0f);   // five arms
                const float edge = R * (0.22f + 0.78f * arm) * (1.0f + 0.03f * std::cos(a * 60.0f));
                const float r = edge * float(j) / (nr - 1);
                const float h = 0.13f * R * (1.0f - float(j) / (nr - 1)) * (0.4f + 0.6f * arm) + 0.002f;
                pts.push_back(c0 + make_float3(std::cos(a) * r, h, std::sin(a) * r));
            }
        // rows run centre -> edge: transpose to (nu = ring, nv = rows)
        appendGrid(stars, pts, nRing, nr, false);
        ++nStar;
    }
    // the rocks: their faces by area, in the shallows Padina on what looks up, sponges on the sides and under the ledges
    struct Site { float3 p, n; };
    std::vector<float> cdf;
    std::vector<Site> tri;
    float acc = 0.0f;
    for (const KMeshCPU* m : rockMeshes)
        for (size_t t = 0; t < m->idx.size(); t += 3) {
            const uint3 q = m->idx[t];
            const float3 a = m->pos[q.x], bb = m->pos[q.y], cc = m->pos[q.z];
            float3 n = cross(bb - a, cc - a);
            const float area = 0.5f * length(n);
            if (area < 1e-7f) continue;
            n = n * (1.0f / (2.0f * area));
            const float3 sn = m->nrm[q.x] + m->nrm[q.y] + m->nrm[q.z];
            if (dot(sn, n) < 0.0f) n = n * -1.0f;
            const float3 c = (a + bb + cc) * (1.0f / 3.0f);
            if (c.y > -0.8f || c.y < -60.0f) continue;
            if (std::fabs(c.x) > lane + 26.0f || c.z > 24.0f || c.z < -178.0f) continue;
            acc += area;
            cdf.push_back(acc);
            tri.push_back({c, n});
        }
    auto pick = [&]() -> Site {
        const size_t k = size_t(std::lower_bound(cdf.begin(), cdf.end(), U(rng) * acc) - cdf.begin());
        return tri[std::min(k, tri.size() - 1)];
    };
    int nPadina = 0, nSponge = 0, padinaOff = 0;
    const KoySurfaceSnap rockSnap(rockMeshes, 0.25f, [&](float3 c) {
        return c.y < -0.5f && c.y > -16.6f && std::fabs(c.x) <= lane + 26.5f && c.z <= 24.5f && c.z >= -178.5f;
    });
    for (int k = 0; k < cfg.i("life.padina_tufts", 420) && !tri.empty(); ++k) {
        const Site S = pick();
        if (S.n.y < 0.35f || S.p.y < -16.0f) continue;   // light-loving: the shallow tops
        const int n = 4 + int(U(rng) * 9.0f);
        const float3 t = normalize(cross(std::fabs(S.n.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0), S.n)), bb = cross(S.n, t);
        for (int q = 0; q < n; ++q) {
            const float R = 0.025f + 0.035f * U(rng);
            float3 at, sn;
            if (!rockSnap.nearest(S.p + t * ((U(rng) - 0.5f) * 0.12f) + bb * ((U(rng) - 0.5f) * 0.12f), 0.04f, at, sn) || sn.y < 0.2f) {
                ++padinaOff;   // over a block's edge, or onto its side
                continue;
            }
            const LifeFrame F = lifeFrameAt(at - sn * 0.004f, normalize(sn + make_float3((U(rng) - 0.5f) * 0.8f, 0.4f, (U(rng) - 0.5f) * 0.8f)), U(rng) * 6.2831853f);
            // a fan rolled into a funnel: angle across, height up; the rim curls outward
            const int nu = 12, nv = 5;
            std::vector<float3> pts;
            for (int j = 0; j < nv; ++j)
                for (int i = 0; i < nu; ++i) {
                    const float u = float(i) / (nu - 1), v = float(j) / (nv - 1);
                    const float ang = (u - 0.5f) * 3.6f;
                    const float rr = R * (0.15f + 0.85f * v);
                    const float curl = 0.25f * R * v * v;
                    pts.push_back(F.at(make_float3(std::sin(ang) * rr * 0.6f, v * R * 1.1f, std::cos(ang) * rr * 0.6f - 0.3f * R + curl)));
                }
            appendGrid(padina, pts, nu, nv, true);
        }
        ++nPadina;
    }
    std::vector<float3> sph;
    std::vector<uint3> sphF;
    ico1(sph, sphF);
    for (int k = 0; k < cfg.i("life.rock_sponges", 900) && !tri.empty(); ++k) {
        const Site S = pick();
        if (S.n.y > 0.5f) continue;   // the sides and the ledges' undersides: shade
        const float kind = U(rng);
        KMeshCPU& m = kind < 0.45f ? orange : kind < 0.75f ? yellow : darkS;
        const float r = (&m == &darkS) ? 0.07f + 0.13f * U(rng) : 0.03f + 0.08f * U(rng);
        const float3 t = normalize(cross(std::fabs(S.n.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0), S.n)), bb = cross(S.n, t);
        const unsigned base = unsigned(m.pos.size());
        const float o = float(k % 89) * 1.3f;
        for (size_t i = 0; i < sph.size(); ++i) {
            const float3 d = sph[i];
            const float l = 1.0f + 0.5f * kn::fbm3(d.x * 2.2f + o, d.y * 2.2f, d.z * 2.2f, 2);
            const float fl = (&m == &darkS) ? 0.7f : 0.3f;   // the massive ones stand out, the encrusting ones lie flat
            const float h = std::max(d.y, -0.15f) * fl;
            m.pos.push_back(S.p + t * (d.x * r * l) + bb * (d.z * r * l) + S.n * (h * r * l));
            m.nrm.push_back(normalize(t * d.x + bb * d.z + S.n * (d.y / fl)));
        }
        for (const uint3& f : sphF) m.idx.push_back(make_uint3(base + f.x, base + f.y, base + f.z));
        ++nSponge;
    }
    for (KMeshCPU* m : {&shells, &pink, &stars, &padina, &orange, &yellow, &darkS}) {
        m->aoRange = 0.5f;
        m->scatter = true;
        if (!m->idx.empty()) out.push_back(std::move(*m));
    }
    if (log) {
        char buf[260];
        std::snprintf(buf, sizeof(buf), "%d tarak, %d kalp kabugu, %d burgu, %d kum yildizi; kayalarda %d padina tutami (%d yaprak kayada yer bulamayip atildi), %d sunger",
                      nScallop, nCockle, nTower, nStar, nPadina, padinaOff, nSponge);
        *log = buf;
    }
}

std::vector<std::pair<float3, float3>> koyReefFanSites(const std::vector<const KMeshCPU*>& rocks, float minDepth, int count, float gap, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    std::vector<std::pair<float3, float3>> cand, out;
    for (const KMeshCPU* m : rocks)
        for (size_t t = 0; t < m->idx.size(); t += 5) {
            const uint3 q = m->idx[t];
            const float3 c = (m->pos[q.x] + m->pos[q.y] + m->pos[q.z]) * (1.0f / 3.0f);
            if (-c.y < minDepth) continue;
            const float3 n = normalize(m->nrm[q.x] + m->nrm[q.y] + m->nrm[q.z]);
            if (n.y < 0.2f) continue;
            cand.push_back({c, n});
        }
    for (int t = 0; t < count * 40 && int(out.size()) < count && !cand.empty(); ++t) {
        const auto& s = cand[size_t(U(rng) * float(cand.size())) % cand.size()];
        bool near = false;
        for (const auto& o : out) near = near || length(o.first - s.first) < gap;
        if (!near) out.push_back(s);
    }
    return out;
}

namespace {
void addBoxW(KMeshCPU& m, float3 c, float3 ax, float3 ay, float3 az, float3 h) {   // a box, half sizes h along unit axes
    const float3 X = ax * h.x, Y = ay * h.y, Z = az * h.z;
    const float3 n6[6] = {ax, ax * -1.0f, ay, ay * -1.0f, az, az * -1.0f};
    const float3 cs[6][4] = {{c + X - Y - Z, c + X + Y - Z, c + X + Y + Z, c + X - Y + Z}, {c - X - Y + Z, c - X + Y + Z, c - X + Y - Z, c - X - Y - Z},
                             {c - X + Y - Z, c - X + Y + Z, c + X + Y + Z, c + X + Y - Z}, {c - X - Y + Z, c - X - Y - Z, c + X - Y - Z, c + X - Y + Z},
                             {c + X - Y + Z, c + X + Y + Z, c - X + Y + Z, c - X - Y + Z}, {c - X - Y - Z, c - X + Y - Z, c + X + Y - Z, c + X - Y - Z}};
    for (int f = 0; f < 6; ++f) {
        const unsigned b0 = unsigned(m.pos.size());
        for (int k = 0; k < 4; ++k) { m.pos.push_back(cs[f][k]); m.nrm.push_back(n6[f]); }
        m.idx.push_back(make_uint3(b0, b0 + 1, b0 + 2));
        m.idx.push_back(make_uint3(b0, b0 + 2, b0 + 3));
    }
}
void addSphereW(KMeshCPU& m, float3 c, float r, float3 squash, unsigned seed, float lump) {
    std::vector<float3> v;
    std::vector<uint3> f;
    ico1(v, f);
    const unsigned b0 = unsigned(m.pos.size());
    const float o = float(seed % 97) * 1.9f;
    for (const float3& d : v) {
        const float l = 1.0f + lump * kn::fbm3(d.x * 2.0f + o, d.y * 2.0f, d.z * 2.0f, 2);
        m.pos.push_back(c + make_float3(d.x * squash.x, d.y * squash.y, d.z * squash.z) * (r * l));
        m.nrm.push_back(normalize(make_float3(d.x / squash.x, d.y / squash.y, d.z / squash.z)));
    }
    for (const uint3& t : f) m.idx.push_back(make_uint3(b0 + t.x, b0 + t.y, b0 + t.z));
}
}   // namespace

void koyWreckDebris(const Config& cfg, const std::function<float(float, float)>& floorY, float3 bow, float3 stern, const KoyDebrisMaterials& mat,
                    unsigned int id, std::vector<KMeshCPU>& out, std::string* log) {
    std::mt19937 rng(unsigned(cfg.i("debris.seed", 61)));
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    std::normal_distribution<float> G(0.0f, 1.0f);
    KMeshCPU wood, iron, stone;
    wood.name = "enkaz (ahsap)";
    iron.name = "enkaz (demir)";
    stone.name = "enkaz (safra)";
    wood.material = mat.wood;
    iron.material = mat.iron;
    stone.material = mat.stone;
    wood.id = iron.id = stone.id = id;
    // the ship's frame on the sand: along the keel (bow-ward), across to starboard (the side that fell), the middle
    float3 X = bow - stern;
    X.y = 0.0f;
    const float L = length(X);
    X = X * (1.0f / std::max(L, 1e-3f));
    const float3 S = make_float3(-X.z, 0.0f, X.x) * -1.0f;   // starboard: X cross up, sign checked against the list (port up)
    const float3 mid = (bow + stern) * 0.5f;
    auto onSand = [&](float a, float c) {   // a along the keel from the middle, c out to starboard (negative: port)
        const float3 p = mid + X * a + S * c;
        return make_float3(p.x, floorY(p.x, p.z), p.z);
    };
    // where debris lies: mostly to starboard (out from the hull, 5-16 m), a train behind the stern, a little to port
    auto spot = [&](float& a, float& c) {
        const float r = U(rng);
        if (r < 0.62f) { a = (U(rng) - 0.5f) * L * 1.1f; c = 5.5f + 11.0f * std::pow(U(rng), 1.5f); }
        else if (r < 0.85f) { a = -L * 0.5f - 2.0f - 12.0f * U(rng); c = (U(rng) - 0.3f) * 10.0f; }
        else { a = (U(rng) - 0.5f) * L; c = -(5.5f + 6.0f * U(rng)); }
    };
    // timbers and planks: long, half sunk, tilted, some crossed
    int nTimber = 0;
    for (int k = 0; k < cfg.i("debris.timbers", 46); ++k) {
        float a, c;
        spot(a, c);
        const float3 p = onSand(a, c);
        const float len = 0.8f + 3.2f * U(rng) * U(rng);
        const bool plank = U(rng) < 0.6f;
        const float w = plank ? 0.22f + 0.1f * U(rng) : 0.18f + 0.12f * U(rng), t = plank ? 0.06f + 0.03f * U(rng) : w * (0.7f + 0.3f * U(rng));
        const float yaw = U(rng) * 6.2831853f, tilt = (U(rng) - 0.5f) * 0.25f;
        const float3 ax = normalize(make_float3(std::cos(yaw), tilt, std::sin(yaw)));
        const float3 az0 = normalize(cross(ax, make_float3(0, 1, 0)));
        const float roll = (U(rng) - 0.5f) * 0.6f;
        const float3 ay0 = cross(az0, ax);
        const float3 ay = ay0 * std::cos(roll) + az0 * std::sin(roll), az = az0 * std::cos(roll) - ay0 * std::sin(roll);
        addBoxW(wood, p + make_float3(0.0f, t * (0.1f + 0.3f * U(rng)), 0.0f), ax, ay, az, make_float3(0.5f * len, 0.5f * t, 0.5f * w));
        ++nTimber;
    }
    // barrels: a bulging stave cylinder with hoops; burst ones: their staves splayed about, a hoop or two lying flat
    int nBarrel = 0;
    for (int k = 0; k < cfg.i("debris.barrels", 12); ++k) {
        float a, c;
        spot(a, c);
        const float3 p = onSand(a, c);
        const float H = 0.75f + 0.25f * U(rng), R = 0.27f + 0.06f * U(rng);
        if (U(rng) < 0.55f) {   // whole, on its side, a third sunk
            const float yaw = U(rng) * 6.2831853f;
            const float3 ax = make_float3(std::cos(yaw), 0.0f, std::sin(yaw)), up = make_float3(0, 1, 0), sd = cross(ax, up);
            const float3 c0 = p + up * (R * 0.55f);
            const int nu = 18, nv = 10;
            const unsigned b0 = unsigned(wood.pos.size());
            for (int j = 0; j <= nv; ++j) {
                const float u = float(j) / nv - 0.5f;
                const float rr = R * (0.86f + 0.14f * std::cos(u * 3.14159f));   // the bilge
                for (int i = 0; i <= nu; ++i) {
                    const float ph = 6.2831853f * float(i) / nu;
                    const float3 rad = up * std::cos(ph) + sd * std::sin(ph);
                    wood.pos.push_back(c0 + ax * (u * H) + rad * rr * (1.0f - 0.025f * float(i % 2)));   // the staves' seams
                    wood.nrm.push_back(rad);
                }
            }
            for (int j = 0; j < nv; ++j)
                for (int i = 0; i < nu; ++i) {
                    const unsigned q0 = b0 + unsigned(j * (nu + 1) + i), q1 = q0 + 1, q2 = q0 + unsigned(nu + 1), q3 = q2 + 1;
                    wood.idx.push_back(make_uint3(q0, q2, q3));
                    wood.idx.push_back(make_uint3(q0, q3, q1));
                }
            for (float hu : {-0.38f, -0.2f, 0.2f, 0.38f}) {   // the hoops: thin iron bands proud of the staves
                const float rr = R * (0.86f + 0.14f * std::cos(hu * 3.14159f)) + 0.008f;
                for (int i = 0; i < 16; ++i) {
                    const float p0 = 6.2831853f * float(i) / 16.0f, p1 = 6.2831853f * float(i + 1) / 16.0f;
                    const float3 r0 = up * std::cos(p0) + sd * std::sin(p0), r1 = up * std::cos(p1) + sd * std::sin(p1);
                    const float3 m0 = c0 + ax * (hu * H) + (r0 + r1) * (0.5f * rr);
                    addBoxW(iron, m0, normalize(r1 - r0), normalize(r0 + r1), ax, make_float3(0.5f * length(r1 - r0) * rr, 0.006f, 0.022f));
                }
            }
        } else {   // burst: staves splayed, two hoops flat on the sand
            const int nStave = 10 + int(U(rng) * 8.0f);
            for (int q = 0; q < nStave; ++q) {
                const float ang = U(rng) * 6.2831853f, rr = 0.2f + 1.0f * U(rng);
                const float3 sp = onSand(a + std::cos(ang) * rr, c + std::sin(ang) * rr);
                const float yaw = U(rng) * 6.2831853f;
                const float3 ax = make_float3(std::cos(yaw), 0.05f * G(rng), std::sin(yaw));
                const float3 az = normalize(cross(ax, make_float3(0, 1, 0))), ay = cross(az, normalize(ax));
                addBoxW(wood, sp + make_float3(0, 0.012f, 0), normalize(ax), ay, az, make_float3(0.5f * H * (0.6f + 0.4f * U(rng)), 0.012f, 0.045f));
            }
            for (int hh = 0; hh < 2; ++hh) {
                const float3 hc = onSand(a + G(rng) * 0.5f, c + G(rng) * 0.5f);
                const float rr = R * (0.9f + 0.1f * U(rng));
                for (int i = 0; i < 16; ++i) {
                    const float p0 = 6.2831853f * float(i) / 16.0f, p1 = 6.2831853f * float(i + 1) / 16.0f;
                    const float3 r0 = make_float3(std::cos(p0), 0, std::sin(p0)), r1 = make_float3(std::cos(p1), 0, std::sin(p1));
                    addBoxW(iron, hc + (r0 + r1) * (0.5f * rr) + make_float3(0, 0.01f, 0), normalize(r1 - r0), make_float3(0, 1, 0), normalize(r0 + r1),
                            make_float3(0.5f * length(r1 - r0) * rr, 0.008f, 0.02f));
                }
            }
        }
        ++nBarrel;
    }
    // round shot: piles (pyramids gone slumped) and strays, concreted (lumpy)
    int nShot = 0;
    for (int k = 0; k < cfg.i("debris.shot_piles", 7); ++k) {
        float a, c;
        spot(a, c);
        const float3 p = onSand(a, c);
        const float r = 0.06f + 0.015f * U(rng);
        const int layers = 2 + int(U(rng) * 2.0f);
        for (int l = 0; l < layers; ++l) {
            const int n = (layers - l) * (layers - l) + 2;
            for (int q = 0; q < n; ++q) {
                const float ang = U(rng) * 6.2831853f, rr = r * 2.0f * float(layers - l) * std::sqrt(U(rng));
                const float3 sp = p + make_float3(std::cos(ang) * rr, r * (0.6f + 1.6f * float(l)), std::sin(ang) * rr);
                addSphereW(iron, sp, r, make_float3(1, 1, 1), unsigned(nShot * 7 + 1), 0.08f);
                ++nShot;
            }
        }
    }
    for (int k = 0; k < cfg.i("debris.shot_loose", 70); ++k) {
        float a, c;
        spot(a, c);
        const float3 p = onSand(a, c);
        const float r = 0.06f + 0.015f * U(rng);
        addSphereW(iron, p + make_float3(0, r * 0.45f, 0), r, make_float3(1, 1, 1), unsigned(nShot * 7 + 3), 0.1f);
        ++nShot;
    }
    // the ballast: a low mound of river stones under the breach (starboard, amidships), thinning out
    int nBallast = 0;
    for (int k = 0; k < cfg.i("debris.ballast", 320); ++k) {
        const float a = G(rng) * L * 0.18f, c = 4.5f + std::fabs(G(rng)) * 3.5f;
        const float3 p = onSand(a, c);
        const float r = 0.08f + 0.14f * U(rng) * U(rng);
        const float heap = 0.35f * std::exp(-(a * a) / (2.0f * 16.0f)) * std::exp(-((c - 4.5f) * (c - 4.5f)) / (2.0f * 6.0f));
        addSphereW(stone, p + make_float3(0, r * 0.3f + heap * U(rng), 0), r, make_float3(1.0f + 0.3f * U(rng), 0.6f + 0.2f * U(rng), 0.9f + 0.2f * U(rng)),
                   unsigned(k * 13 + 5), 0.18f);
        ++nBallast;
    }
    for (KMeshCPU* m : {&wood, &iron, &stone}) {
        m->aoRange = 1.2f;
        if (!m->idx.empty()) out.push_back(std::move(*m));
    }
    if (log) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%d kalas, %d fici, %d gulle, %d safra tasi", nTimber, nBarrel, nShot, nBallast);
        *log = buf;
    }
}
