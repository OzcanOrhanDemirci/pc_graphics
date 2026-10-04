// Scene 5 "Koy": the trial area's world (see koy_world.h).
#include "scenes/koy/koy_world.h"
#include "scenes/koy/koy_noise.h"

#include "scenes/koy/koy_amphora.h"
#include "scenes/koy/koy_island.h"
#include "scenes/koy/koy_props.h"
#include "scenes/koy/koy_wreck.h"

#include "stb_image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <map>
#include <random>
#include <sstream>
#include <thread>
#include <utility>

namespace {

// ---- gradient noise (Perlin style), deterministic
unsigned int hash3(int x, int y, int z) {
    unsigned int h = unsigned(x) * 73856093u ^ unsigned(y) * 19349663u ^ unsigned(z) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}
float grad3(int x, int y, int z, float fx, float fy, float fz) {
    const unsigned int h = hash3(x, y, z) & 15u;
    const float u = h < 8 ? fx : fy, v = h < 4 ? fy : (h == 12 || h == 14 ? fx : fz);
    return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
}
float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
float noise3(float x, float y, float z) {
    const int X = int(std::floor(x)), Y = int(std::floor(y)), Z = int(std::floor(z));
    const float fx = x - X, fy = y - Y, fz = z - Z;
    const float u = fade(fx), v = fade(fy), w = fade(fz);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    return L(L(L(grad3(X, Y, Z, fx, fy, fz), grad3(X + 1, Y, Z, fx - 1, fy, fz), u),
               L(grad3(X, Y + 1, Z, fx, fy - 1, fz), grad3(X + 1, Y + 1, Z, fx - 1, fy - 1, fz), u), v),
             L(L(grad3(X, Y, Z + 1, fx, fy, fz - 1), grad3(X + 1, Y, Z + 1, fx - 1, fy, fz - 1), u),
               L(grad3(X, Y + 1, Z + 1, fx, fy - 1, fz - 1), grad3(X + 1, Y + 1, Z + 1, fx - 1, fy - 1, fz - 1), u), v),
             w);
}
float fbm3(float x, float y, float z, int oct) {
    float s = 0.0f, a = 0.5f, f = 1.0f;
    for (int k = 0; k < oct; ++k) {
        s += a * noise3(x * f, y * f, z * f);
        f *= 2.03f;
        a *= 0.5f;
    }
    return s;
}
float smooth01(float a, float b, float x) {
    const float t = std::min(std::max((x - a) / (b - a), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// the fine part between lo and hi, growing outwards to [outerLo, outerHi]
std::vector<float> gridAxis2(float lo, float hi, float h0, float outerLo, float outerHi, float hMax) {
    std::vector<float> axis;
    std::vector<float> left;
    for (float p = lo; p > outerLo;) {
        const float h = std::min(hMax, h0 + (lo - p) * 0.03f);
        p -= h;
        left.push_back(std::max(p, outerLo));
    }
    for (size_t k = left.size(); k-- > 0;) axis.push_back(left[k]);
    for (float x = lo; x < hi - 0.5f * h0; x += h0) axis.push_back(x);
    axis.push_back(hi);
    for (float p = hi; p < outerHi;) {
        const float h = std::min(hMax, h0 + (p - hi) * 0.03f);
        p += h;
        axis.push_back(std::min(p, outerHi));
    }
    return axis;
}

}   // namespace

std::vector<float> koyList(const Config& cfg, const std::string& key) {
    std::vector<float> out;
    const auto it = cfg.values.find(key);
    if (it == cfg.values.end()) return out;
    std::string v = it->second;
    const size_t hash = v.find('#');
    if (hash != std::string::npos) v = v.substr(0, hash);
    for (char& ch : v)
        if (ch == ',' || ch == ';' || ch == ':') ch = ' ';
    std::istringstream in(v);
    float x;
    while (in >> x) out.push_back(x);
    return out;
}

// The floor's broad shape (the mesh). The sand ripples are NOT in it: they are analytic in the shading, band-limited by
// the footprint (koy.cu), so they never beat against the grid.
KoyFloorShape KoyFloorShape::from(const Config& cfg) {
    KoyFloorShape f;
    const float3 ic = cfg.v3("island.centre", make_float3(0.0f, 0.0f, 60.0f));
    f.islandX = ic.x;
    f.islandZ = ic.z;
    f.pierR = cfg.f("floor.pier_r", 60.0f);
    f.undulation = cfg.f("floor.undulation", 0.25f);
    f.sandWave = cfg.f("floor.sand_wave", 0.5f);
    f.wander = cfg.f("floor.wander", 6.0f);
    f.shallowest = cfg.f("floor.shallowest", 0.3f);
    // knots: s depth slope, s depth slope, ... (PLAN_ADA §4: the stations sit where the slope eases)
    std::vector<float> k = koyList(cfg, "floor.profile");
    if (k.size() < 6 || k.size() % 3)
        k = {-30, 0.3f, 0.02f, 0, 3.5f, 0.13f, 30, 7.5f, 0.14f, 56, 12.0f, 0.08f, 97, 22.0f, 0.12f, 125, 32.0f, 0.47f, 400, 161.0f, 0.47f};
    for (size_t i = 0; i + 2 < k.size(); i += 3) {
        f.ps.push_back(k[i]);
        f.pd.push_back(k[i + 1]);
        f.pm.push_back(k[i + 2]);
    }
    f.moundH = cfg.i("batik.use", 1) ? cfg.f("batik.mound", 0.8f) : 0.0f;
    f.moundW = cfg.f("batik.mound_width", 4.5f);
    {
        const float3 c = cfg.v3("batik.centre", make_float3(2.0f, 0.0f, -97.0f));
        const float yaw = cfg.f("batik.deg", 35.0f) * 3.14159265f / 180.0f;
        const float2 d = make_float2(std::sin(yaw), -std::cos(yaw));   // towards the bow (koy_wreck.cpp's placement)
        f.moundA = make_float2(c.x + d.x * 17.0f, c.z + d.y * 17.0f);
        f.moundB = make_float2(c.x - d.x * 17.0f, c.z - d.y * 17.0f);
    }
    return f;
}
float KoyFloorShape::depth(float s) const {
    const size_t n = ps.size();
    if (s <= ps[0]) return pd[0] + pm[0] * (s - ps[0]);
    if (s >= ps[n - 1]) return pd[n - 1] + pm[n - 1] * (s - ps[n - 1]);
    size_t i = 0;
    while (i + 2 < n && s > ps[i + 1]) ++i;
    const float h = ps[i + 1] - ps[i], t = (s - ps[i]) / h, t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * pd[i] + (t3 - 2 * t2 + t) * h * pm[i] + (-2 * t3 + 3 * t2) * pd[i + 1] + (t3 - t2) * h * pm[i + 1];
}
float KoyFloorShape::sOf(float x, float z) const {
    const float dx = x - islandX, dz = z - islandZ;
    return std::sqrt(dx * dx + dz * dz) - pierR + wander * fbm3(x * 0.012f, 2.9f, z * 0.012f, 2);
}
float KoyFloorShape::base(float x, float z) const {
    float d = depth(sOf(x, z));
    // long low sand waves (tens of metres) and small undulations, growing a little with depth
    d += sandWave * fbm3(x * 0.035f, 7.1f, z * 0.035f, 2) + undulation * (1.0f + 0.01f * d) * fbm3(x * 0.11f, 0.37f, z * 0.11f, 3);
    return -std::max(d, shallowest);
}
float KoyFloorShape::operator()(float x, float z) const {
    float y = base(x, z);
    if (moundH > 0.0f) {   // the wreck's mound: across the keel a smooth bump (the hold silted, the sand banked against the hull)
        const float2 ab = make_float2(moundB.x - moundA.x, moundB.y - moundA.y), ap = make_float2(x - moundA.x, z - moundA.y);
        const float L2 = std::max(ab.x * ab.x + ab.y * ab.y, 1e-6f);
        const float t = (ap.x * ab.x + ap.y * ab.y) / L2;
        if (t > -0.15f && t < 1.15f) {
            const float tc = std::min(std::max(t, 0.0f), 1.0f);
            const float dx = ap.x - ab.x * tc, dz = ap.y - ab.y * tc;
            const float across = std::sqrt(dx * dx + dz * dz) / moundW;
            const float a = std::max(0.0f, 1.0f - across * across), along = std::sin(3.14159265f * std::min(std::max(t, 0.0f), 1.0f));
            const float edge = std::min(std::max((0.15f - std::fabs(t - 0.5f) + 0.5f) / 0.15f, 0.0f), 1.0f);   // ends fade
            y += moundH * a * a * (0.35f + 0.65f * along) * edge * (0.85f + 0.15f * fbm3(x * 0.4f, 3.3f, z * 0.4f, 2));
        }
    }
    return y;
}
float koyFloor(const Config& cfg, float x, float z) { return KoyFloorShape::from(cfg)(x, z); }

// ---------------------------------------------------------------------------------------------------
namespace {
KMeshCPU buildFloor(const Config& cfg) {
    KMeshCPU m;
    m.name = "floor";
    m.material = 0;
    m.id = 1;
    // fine over the corridor and round the pier (x and z ranges of their own), growing outwards to the world's edge
    const float step = cfg.f("floor.fine_step", 0.12f), grow = cfg.f("floor.coarse_max", 4.0f);
    const float3 lo = cfg.v3("floor.fine_min", make_float3(-45.0f, 0.0f, -190.0f)), hi = cfg.v3("floor.fine_max", make_float3(45.0f, 0.0f, 105.0f));
    const float3 elo = cfg.v3("floor.extent_min", make_float3(-420.0f, 0.0f, -520.0f));
    const float3 ehi = cfg.v3("floor.extent_max", make_float3(420.0f, 0.0f, 480.0f));
    const std::vector<float> axX = gridAxis2(lo.x, hi.x, step, elo.x, ehi.x, grow);
    const std::vector<float> axZ = gridAxis2(lo.z, hi.z, step, elo.z, ehi.z, grow);
    const int nx = int(axX.size()), nz = int(axZ.size());
    const KoyFloorShape fs = KoyFloorShape::from(cfg);
    m.pos.resize(size_t(nx) * nz);
    m.nrm.resize(size_t(nx) * nz);
    std::vector<std::future<void>> jobs;
    const int T = std::max(1, int(std::thread::hardware_concurrency()));
    for (int t = 0; t < T; ++t)
        jobs.push_back(std::async(std::launch::async, [&, t] {
            for (int j = t; j < nz; j += T)
                for (int i = 0; i < nx; ++i) {
                    const float x = axX[i], z = axZ[j];
                    const float e = 0.004f;
                    const float y = fs(x, z);
                    const float gx = (fs(x + e, z) - fs(x - e, z)) / (2 * e);
                    const float gz = (fs(x, z + e) - fs(x, z - e)) / (2 * e);
                    m.pos[size_t(j) * nx + i] = make_float3(x, y, z);
                    m.nrm[size_t(j) * nx + i] = normalize(make_float3(-gx, 1.0f, -gz));
                }
        }));
    for (auto& j : jobs) j.get();
    m.idx.reserve(size_t(nx - 1) * (nz - 1) * 2);
    for (int j = 0; j + 1 < nz; ++j)
        for (int i = 0; i + 1 < nx; ++i) {
            const unsigned a = unsigned(j * nx + i), b = a + 1, c = a + unsigned(nx), d = c + 1;
            m.idx.push_back(make_uint3(a, c, b));
            m.idx.push_back(make_uint3(b, c, d));
        }
    std::printf("koy zemin: %d x %d nokta (%.1f M ucgen)\n", nx, nz, m.idx.size() / 1e6);
    return m;
}

// a boulder until the scanned one arrives: an icosphere displaced by noise, flattened, sunk into the sand
KMeshCPU buildBoulder(const Config& cfg, float3 centre, float radius) {
    KMeshCPU m;
    m.name = "kaya";
    m.material = 1;
    m.id = 2;
    std::vector<float3> v = {{-1, 1.618f, 0}, {1, 1.618f, 0}, {-1, -1.618f, 0}, {1, -1.618f, 0}, {0, -1, 1.618f}, {0, 1, 1.618f},
                             {0, -1, -1.618f}, {0, 1, -1.618f}, {1.618f, 0, -1}, {1.618f, 0, 1}, {-1.618f, 0, -1}, {-1.618f, 0, 1}};
    for (auto& p : v) p = normalize(p);
    std::vector<uint3> f = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                            {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (int lv = 0; lv < cfg.i("rock.subdiv", 6); ++lv) {
        std::map<unsigned long long, unsigned> mid;
        auto midpoint = [&](unsigned a, unsigned b) {
            const unsigned long long key = (unsigned long long)std::min(a, b) << 32 | std::max(a, b);
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            v.push_back(normalize(v[a] + v[b]));
            mid[key] = unsigned(v.size() - 1);
            return unsigned(v.size() - 1);
        };
        std::vector<uint3> nf;
        for (const uint3& t : f) {
            const unsigned a = midpoint(t.x, t.y), b = midpoint(t.y, t.z), c = midpoint(t.z, t.x);
            nf.push_back(make_uint3(t.x, a, c));
            nf.push_back(make_uint3(t.y, b, a));
            nf.push_back(make_uint3(t.z, c, b));
            nf.push_back(make_uint3(a, b, c));
        }
        f.swap(nf);
    }
    m.pos.resize(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        const float3 d = v[i];
        float r = 1.0f + 0.28f * fbm3(d.x * 1.3f + 4.1f, d.y * 1.3f, d.z * 1.3f, 3) + 0.07f * fbm3(d.x * 5.0f, d.y * 5.0f + 2.2f, d.z * 5.0f, 3) +
                  0.018f * fbm3(d.x * 19.0f, d.y * 19.0f, d.z * 19.0f + 9.0f, 3);
        float3 p = d * (radius * r);
        p.y *= 0.72f;
        p.x *= 1.15f;
        m.pos[i] = centre + p;
    }
    m.idx = f;
    m.nrm.assign(m.pos.size(), make_float3(0, 0, 0));
    for (const uint3& t : m.idx) {
        const float3 n = cross(m.pos[t.y] - m.pos[t.x], m.pos[t.z] - m.pos[t.x]);
        m.nrm[t.x] += n;
        m.nrm[t.y] += n;
        m.nrm[t.z] += n;
    }
    for (auto& n : m.nrm) n = normalize(n);
    return m;
}
// A scanned boulder: mesh.bin from assets/koy/fetch.py ('KOYM', counts, positions, normals, uvs, indices), scaled so its
// longest horizontal side is `size` metres, standing on the floor at (x, z) sunk by `sink` of its height.
bool loadScannedRock(const Config& cfg, const std::string& path, float3 at, float size, float sink, float yawDeg, KMeshCPU& m) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[4];
    unsigned int nv = 0, nt = 0;
    bool ok = std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "KOYM", 4) == 0 && std::fread(&nv, 4, 1, f) == 1 && std::fread(&nt, 4, 1, f) == 1;
    if (ok) {
        m.pos.resize(nv);
        m.nrm.resize(nv);
        m.uv.resize(nv);
        m.idx.resize(nt);
        ok = std::fread(m.pos.data(), sizeof(float3), nv, f) == nv && std::fread(m.nrm.data(), sizeof(float3), nv, f) == nv &&
             std::fread(m.uv.data(), sizeof(float2), nv, f) == nv && std::fread(m.idx.data(), sizeof(uint3), nt, f) == nt;
    }
    std::fclose(f);
    if (!ok || nv == 0) return false;
    float3 lo = m.pos[0], hi = m.pos[0];
    for (const float3& p : m.pos) {
        lo = fminf3(lo, p);
        hi = fmaxf3(hi, p);
    }
    const float s = size / std::max(hi.x - lo.x, hi.z - lo.z);
    const float cy = std::cos(yawDeg * 3.14159265f / 180.0f), sy = std::sin(yawDeg * 3.14159265f / 180.0f);
    const float cx = 0.5f * (lo.x + hi.x), cz = 0.5f * (lo.z + hi.z);
    const float ground = koyFloor(cfg, at.x, at.z);
    const float base = ground - sink * (hi.y - lo.y) * s;
    for (size_t i = 0; i < m.pos.size(); ++i) {
        const float3 p = m.pos[i];
        const float x = (p.x - cx) * s, z = (p.z - cz) * s;
        m.pos[i] = make_float3(at.x + cy * x + sy * z, base + (p.y - lo.y) * s, at.z - sy * x + cy * z);
        const float3 n = m.nrm[i];
        m.nrm[i] = normalize(make_float3(cy * n.x + sy * n.z, n.y, -sy * n.x + cy * n.z));
    }
    m.name = "kaya (taranmis)";
    m.material = 1;
    m.id = 2;
    return true;
}
}   // namespace

void KoyWorld::build(const Config& cfg, const std::string& root) {
    meshes.clear();
    instances.clear();
    materials.clear();
    // textures (CC0 scans fetched by assets/koy/fetch.py; without them plain albedos)
    const std::string tdir = root + "/assets/koy/cc0";
    texDir = tdir;
    loadTexturesNow(cfg, cfg.i("tex.mode", 2));
    const int sandSet = setSand, rockSet = setRock;
    KMaterial floorM{};
    floorM.kind = KM_TERRAIN;
    floorM.texSet = sandSet;
    floorM.albedo = sandSet >= 0 ? cfg.v3("floor.tint", make_float3(1.0f, 1.0f, 1.0f)) : cfg.v3("floor.albedo", make_float3(0.50f, 0.45f, 0.36f));
    floorM.uvScale = 1.0f / cfg.f("floor.tile_m", 3.5f);   // ambientCG Ground054: about 3.5 m a tile
    floorM.normalStrength = cfg.f("floor.normal_strength", 1.0f);
    floorM.aoStrength = 1.0f;
    KMaterial rockM{};
    rockM.kind = rockSet >= 0 ? KM_MESH : KM_PLAIN;
    rockM.texSet = rockSet;
    rockM.albedo = rockSet >= 0 ? cfg.v3("rock.tint", make_float3(1.0f, 1.0f, 1.0f)) : cfg.v3("rock.albedo", make_float3(0.34f, 0.32f, 0.28f));
    rockM.uvScale = 1.0f;
    rockM.normalStrength = 1.0f;
    rockM.aoStrength = 1.0f;
    // slot 2 was the seagrass (removed 2026-10-04, Ozcan: the scene is about light and sharpness, not physics); kept
    // empty so the materials after it keep their indices
    KMaterial spareM{};
    spareM.kind = KM_PLAIN;
    spareM.texSet = -1;
    spareM.albedo = make_float3(0.5f, 0.5f, 0.5f);
    // Amfora: the jars' fired clay (the clay scan and the coat below)
    KMaterial jarM{};
    jarM.kind = KM_PLAIN;
    jarM.texSet = -1;
    jarM.albedo = cfg.v3("amfora.albedo", make_float3(0.46f, 0.27f, 0.17f));
    jarM.aoStrength = 1.0f;
    floorM.detailSet = rockM.detailSet = spareM.detailSet = jarM.detailSet = -1;
    floorM.coatSet = rockM.coatSet = spareM.coatSet = jarM.coatSet = -1;   // not 0: that is a texture set
    // the sea's coat (KMaterial) on the jars
    auto coat = [&](KMaterial& M, const char* key, float cover, float scale, float3 tint, float sediment, float film, float mottle) {
        M.coatSet = setCoat;
        M.coatTile = cfg.f("tex.coat_tile", 1.36f);
        M.coatCover = cfg.f((std::string(key) + ".coat_cover").c_str(), cover);
        M.coatScale = cfg.f((std::string(key) + ".coat_scale").c_str(), scale);
        M.coatNormal = cfg.f((std::string(key) + ".coat_normal").c_str(), 1.0f);
        M.coatTint = cfg.v3((std::string(key) + ".coat_tint").c_str(), tint);
        M.sediment = cfg.f((std::string(key) + ".sediment").c_str(), sediment);
        M.sedimentColor = cfg.v3("sediment.color", make_float3(0.40f, 0.37f, 0.31f));
        M.film = cfg.f((std::string(key) + ".film").c_str(), film);
        M.filmColor = cfg.v3("film.color", make_float3(0.78f, 0.95f, 0.72f));
        M.mottle = cfg.f((std::string(key) + ".mottle").c_str(), mottle);
        M.coatRecess = cfg.f((std::string(key) + ".coat_recess").c_str(), 0.9f);
        M.coatSoft = cfg.f((std::string(key) + ".coat_soft").c_str(), 0.1f);
        M.life = cfg.f((std::string(key) + ".life").c_str(), 0.8f);
        M.lifeA = cfg.v3("life.sponge", make_float3(0.55f, 0.16f, 0.06f));   // Crambe crambe: orange-red
        M.lifeB = cfg.v3("life.coralline", make_float3(0.52f, 0.30f, 0.38f));  // Lithophyllum: pale pink-mauve
    };
    if (setCoat >= 0) coat(jarM, "amfora", 0.55f, 0.22f, make_float3(0.42f, 0.40f, 0.36f), 0.75f, 0.35f, 0.22f);
    // Ada: the island's rock, wet (algae turf on what looks up, concretion and sponges in patches) and dry (sunlit
    // limestone, scrub on the gentle slopes); the pier's boards, its concrete wet and dry, the ladder's steel
    auto plain = [](float3 albedo) {
        KMaterial M{};
        M.kind = KM_PLAIN;
        M.texSet = -1;
        M.albedo = albedo;
        M.aoStrength = 1.0f;
        M.detailSet = -1;
        M.coatSet = -1;
        return M;
    };
    KMaterial islandWet = plain(cfg.v3("island.wet_rgb", make_float3(0.30f, 0.29f, 0.25f)));
    KMaterial islandDry = plain(cfg.v3("island.dry_rgb", make_float3(0.62f, 0.58f, 0.50f)));
    if (setCoat >= 0) coat(islandWet, "island", 0.12f, 0.6f, make_float3(0.30f, 0.27f, 0.24f), 1.0f, 0.4f, 0.35f);
    islandWet.sedimentColor = cfg.v3("island.algae_rgb", make_float3(0.11f, 0.12f, 0.05f));   // the turf: Padina, Dictyota, Cystoseira
    islandWet.filmColor = cfg.v3("island.film_rgb", make_float3(0.80f, 0.92f, 0.70f));
    islandDry.sediment = cfg.f("island.scrub", 0.6f);
    islandDry.sedimentColor = cfg.v3("island.scrub_rgb", make_float3(0.16f, 0.19f, 0.09f));   // maquis: lentisk, juniper
    islandDry.mottle = 0.2f;
    KMaterial woodM = plain(cfg.v3("pier.wood_tint", make_float3(0.85f, 0.80f, 0.74f)));
    if (setWood >= 0) {
        woodM.kind = KM_MESH;
        woodM.texSet = setWood;
        woodM.normalStrength = 1.0f;
    }
    woodM.mottle = 0.15f;
    KMaterial concDry = plain(cfg.v3("pier.concrete_tint", make_float3(0.72f, 0.72f, 0.70f)));
    if (setConcrete >= 0) {
        concDry.kind = KM_MESH;
        concDry.texSet = setConcrete;
        concDry.normalStrength = 1.0f;
    } else {
        concDry.albedo = make_float3(0.55f, 0.54f, 0.50f);
    }
    // the tide's dark band on the clean concrete above the crust: algae, barnacles, the wet
    concDry.stainTop = cfg.f("pier.stain_top", 0.75f);
    concDry.stainBand = 0.35f;
    concDry.stainDark = cfg.f("pier.stain_dark", 0.3f);
    KMaterial concWet = concDry;
    concWet.stainDark = 0.0f;
    if (setCoat >= 0) coat(concWet, "pile", 0.6f, 0.25f, make_float3(0.36f, 0.35f, 0.30f), 0.5f, 0.4f, 0.2f);
    KMaterial steelM = plain(cfg.v3("pier.steel_rgb", make_float3(0.55f, 0.56f, 0.57f)));
    KMaterial rustM = plain(cfg.v3("pier.rust_rgb", make_float3(0.30f, 0.13f, 0.05f)));
    rustM.mottle = 0.4f;
    // the floats (yellow plastic, a green film where they sit in the water), the mooring buoy (orange), the ropes
    // (polypropylene gone brown-green), the mussels (blue-black)
    KMaterial floatM = plain(cfg.v3("buoy.rgb", make_float3(0.78f, 0.52f, 0.04f)));
    floatM.film = 0.35f;
    floatM.filmColor = make_float3(0.55f, 0.75f, 0.45f);
    floatM.stainTop = 0.0f;
    floatM.stainBand = 0.25f;
    floatM.stainDark = 0.55f;   // what sits in the water is slimed
    KMaterial bigM = floatM;
    bigM.albedo = cfg.v3("samandira.rgb", make_float3(0.78f, 0.24f, 0.05f));
    KMaterial ropeM = plain(cfg.v3("rope.rgb", make_float3(0.26f, 0.24f, 0.17f)));
    ropeM.film = 0.4f;
    ropeM.filmColor = make_float3(0.7f, 0.85f, 0.6f);
    ropeM.mottle = 0.3f;
    KMaterial musselM = plain(cfg.v3("mussel.rgb", make_float3(0.035f, 0.04f, 0.055f)));
    musselM.mottle = 0.35f;
    musselM.film = 0.25f;
    musselM.filmColor = make_float3(1.6f, 1.7f, 1.4f);   // worn edges lighter
    // the shallows' small things (koy_props.h)
    KMaterial stoneL = plain(cfg.v3("props.stone_light", make_float3(0.44f, 0.42f, 0.37f)));
    stoneL.mottle = 0.3f;
    KMaterial stoneD = plain(cfg.v3("props.stone_dark", make_float3(0.11f, 0.11f, 0.12f)));
    stoneD.mottle = 0.3f;
    KMaterial starM = plain(cfg.v3("props.starfish_rgb", make_float3(0.62f, 0.11f, 0.05f)));   // Echinaster sepositus
    starM.mottle = 0.15f;
    KMaterial cukeM = plain(cfg.v3("props.cucumber_rgb", make_float3(0.09f, 0.065f, 0.045f)));
    cukeM.mottle = 0.25f;
    KMaterial pinnaM = plain(cfg.v3("props.pinna_rgb", make_float3(0.33f, 0.21f, 0.13f)));
    if (setCoat >= 0) coat(pinnaM, "pinna", 0.3f, 0.12f, make_float3(0.36f, 0.33f, 0.28f), 0.4f, 0.3f, 0.25f);
    KMaterial urchinM = plain(cfg.v3("props.urchin_rgb", make_float3(0.045f, 0.03f, 0.045f)));
    // the wreck: oak gone grey-brown and soft, silt on every ledge, concretion and sponges in patches; the iron (guns,
    // anchor) a lumpy concretion, rust showing through
    KMaterial wreckWood = woodM;
    wreckWood.albedo = cfg.v3("batik.wood_tint", make_float3(0.50f, 0.43f, 0.35f));
    wreckWood.mottle = 0.3f;
    // years under water (Ozcan 2026-10-04: "uçan Hollandalı gibi"): the concretion back on the wood in patches, its
    // recess rule nearly off (the hold is all recess: it blotched); turf and silt on every ledge, a green film on the
    // sides, sponges and coralline on the concretion
    if (setCoat >= 0) coat(wreckWood, "batik", 0.7f, 1.4f, make_float3(0.06f, 0.058f, 0.05f), 0.9f, 0.55f, 0.3f);
    wreckWood.coatRecess = cfg.f("batik.coat_recess", 0.12f);
    wreckWood.coatSoft = cfg.f("batik.coat_soft", 0.35f);
    wreckWood.coatNormal = cfg.f("batik.coat_normal", 2.5f);
    wreckWood.streak = cfg.f("batik.streak", 0.55f);
    wreckWood.sedimentColor = cfg.v3("batik.turf_rgb", make_float3(0.055f, 0.058f, 0.035f));
    KMaterial wreckIron = plain(cfg.v3("batik.iron_rgb", make_float3(0.30f, 0.20f, 0.13f)));
    if (setCoat >= 0) coat(wreckIron, "batik_demir", 0.85f, 0.12f, make_float3(0.09f, 0.075f, 0.06f), 0.6f, 0.3f, 0.35f);
    wreckIron.streak = cfg.f("batik_demir.streak", 0.4f);
    // the sea fans: Paramuricea clavata's purple-red, Eunicella cavolini's yellow (true colours under the torch; at 22 m
    // the daylight leaves them dark)
    KMaterial fanRed = plain(cfg.v3("fans.red_rgb", make_float3(0.42f, 0.04f, 0.10f)));
    fanRed.mottle = 0.25f;
    fanRed.aoStrength = 0.4f;
    KMaterial fanYellow = plain(cfg.v3("fans.yellow_rgb", make_float3(0.72f, 0.52f, 0.08f)));
    fanYellow.mottle = 0.25f;
    fanYellow.aoStrength = 0.4f;
    // Ada enriched: the outcrops' limestone (the island's wet rock with more of the sea on it: turf on every ledge,
    // concretion, sponges and coralline crust in patches)
    // Ada enriched: what grows on the wreck (sand scale: the sand's own albedo reads ~0.1 in this shader)
    KMaterial spongeO = plain(cfg.v3("foul.orange_rgb", make_float3(0.30f, 0.07f, 0.02f)));   // Crambe crambe
    spongeO.mottle = 0.25f;
    KMaterial spongeY = plain(cfg.v3("foul.yellow_rgb", make_float3(0.30f, 0.19f, 0.03f)));   // Agelas oroides
    spongeY.mottle = 0.25f;
    KMaterial squirtM = plain(cfg.v3("foul.squirt_rgb", make_float3(0.22f, 0.04f, 0.05f)));   // Halocynthia papillosa
    squirtM.mottle = 0.2f;
    KMaterial crustM = plain(cfg.v3("foul.crust_rgb", make_float3(0.05f, 0.048f, 0.042f)));  // worm tubes, bryozoans, silt
    crustM.mottle = 0.35f;
    crustM.sediment = 0.25f;
    KMaterial hydroM = plain(cfg.v3("foul.hydroid_rgb", make_float3(0.11f, 0.095f, 0.06f)));   // Eudendrium: pale, stiff
    hydroM.mottle = 0.2f;
    crustM.sedimentColor = cfg.v3("sediment.color", make_float3(0.11f, 0.105f, 0.09f));
    // Ada enriched: the sand's and the rocks' small life (sand scale)
    KMaterial shellM = plain(cfg.v3("life.shell_rgb", make_float3(0.14f, 0.13f, 0.11f)));
    shellM.mottle = 0.3f;
    KMaterial shellPinkM = plain(cfg.v3("life.shell_pink_rgb", make_float3(0.14f, 0.095f, 0.085f)));
    shellPinkM.mottle = 0.3f;
    KMaterial sandStarM = plain(cfg.v3("life.sand_star_rgb", make_float3(0.15f, 0.075f, 0.035f)));   // Astropecten aranciacus
    sandStarM.mottle = 0.2f;
    KMaterial padinaM = plain(cfg.v3("life.padina_rgb", make_float3(0.12f, 0.12f, 0.085f)));   // calcified, pale olive
    padinaM.mottle = 0.25f;
    KMaterial reefM = islandWet;
    reefM.albedo = cfg.v3("reef.rgb", make_float3(0.24f, 0.23f, 0.20f));
    if (setCoat >= 0) coat(reefM, "reef", 0.32f, 0.45f, make_float3(0.30f, 0.27f, 0.23f), 1.0f, 0.55f, 0.4f);
    // the turf: a dense, dark olive-brown felt of small algae on everything that looks up (not the sand's grey silt)
    reefM.sedimentColor = cfg.v3("reef.turf_rgb", make_float3(0.12f, 0.13f, 0.06f));
    reefM.filmColor = cfg.v3("reef.film_rgb", make_float3(0.62f, 0.78f, 0.52f));
    reefM.life = cfg.f("reef.life", 1.0f);
    materials = {floorM, rockM, spareM, jarM, islandWet, islandDry, woodM, concWet, concDry, steelM, rustM, floatM, bigM, ropeM, musselM,
                 stoneL, stoneD, starM, cukeM, pinnaM, urchinM, wreckWood, wreckIron, fanRed, fanYellow, reefM,
                 spongeO, spongeY, squirtM, crustM, hydroM, shellM, shellPinkM, sandStarM, padinaM};

    meshes.push_back(buildFloor(cfg));
    rockCentre = cfg.v3("rock.centre", make_float3(-2.4f, 0.0f, -6.0f));
    rockRadius = cfg.f("rock.radius", 1.35f);
    rockCentre.y = koyFloor(cfg, rockCentre.x, rockCentre.z) + rockRadius * 0.45f;
    {
        KMeshCPU scanned;
        if (rockSet >= 0 && loadScannedRock(cfg, tdir + "/kaya_model/mesh.bin", rockCentre, cfg.f("rock.size", 2.8f), cfg.f("rock.sink", 0.12f),
                                            cfg.f("rock.yaw", 35.0f), scanned)) {
            float3 lo = scanned.pos[0], hi = scanned.pos[0];
            for (const float3& p : scanned.pos) { lo = fminf3(lo, p); hi = fmaxf3(hi, p); }
            rockCentre = (lo + hi) * 0.5f;
            rockRadius = 0.5f * std::max(hi.x - lo.x, hi.z - lo.z) * 0.8f;
            meshes.push_back(scanned);
        } else {
            meshes.push_back(buildBoulder(cfg, rockCentre, rockRadius));
            materials[1].kind = KM_PLAIN;
        }
    }
    for (int k = 0; k < int(meshes.size()); ++k) {
        KInstanceCPU in;
        in.mesh = k;
        instances.push_back(in);
    }
    // Ada: the rocks the amphorae gathered among
    const KoyFloorShape fsAll = KoyFloorShape::from(cfg);
    const KoyRockField amphoraRocks = koyAmphoraRocks(cfg, [&](float x, float z) { return fsAll(x, z); });
    int amphoraRockMesh = -1;
    {
        std::string log;
        meshes.push_back(koyBuildRockField(amphoraRocks, cfg.f("amfora_kaya.cell", 0.07f), 25, 6, &log));   // the reef's limestone (Ada enriched)
        amphoraRockMesh = int(meshes.size()) - 1;
        KInstanceCPU in;
        in.mesh = amphoraRockMesh;
        instances.push_back(in);
        std::printf("koy %s\n", log.c_str());
    }
    // the amphorae: one mesh per type, their instances consecutive (each its own baked occlusion); the cargo among its
    // rocks, and a few strays before and after it (PLAN_ADA §3)
    if (cfg.i("amfora.use", 0)) {
        const KoyFloorShape fs = KoyFloorShape::from(cfg);
        auto rocksAt = [&](float3 q) { return amphoraRocks.sdf(q); };
        std::vector<KoyAmphoraPlacement> field = koyPlaceAmphorae(cfg, [&](float x, float z) { return fs(x, z); }, "amfora", rocksAt);
        const std::vector<KoyAmphoraPlacement> strays = koyPlaceAmphorae(cfg, [&](float x, float z) { return fs(x, z); }, "amfora_tek", rocksAt);
        field.insert(field.end(), strays.begin(), strays.end());
        const int firstMesh = int(meshes.size());
        const std::vector<KoyAmphoraPiece> pieces = koyAmphoraPieces();
        for (size_t t = 0; t < pieces.size(); ++t)
            meshes.push_back(koyBuildPiece(pieces[t], cfg.i("amfora.segments", 96), 3, unsigned(10 + t)));
        for (int t = 0; t < int(pieces.size()); ++t)
            for (const KoyAmphoraPlacement& P : field) {
                if (P.piece != t) continue;
                KInstanceCPU in;
                for (int q = 0; q < 12; ++q) in.m[q] = P.m[q];
                in.mesh = firstMesh + t;
                instances.push_back(in);
            }
    }
    // Ada: the island and the pier, one instance each
    {
        const KoyFloorShape fs = KoyFloorShape::from(cfg);
        auto fy = [&](float x, float z) { return fs(x, z); };
        std::string log;
        const size_t first = meshes.size();
        const KoyIslandShape isl = koyIslandShape(cfg, fy);
        koyBuildIsland(isl, cfg.f("island.cell", 0.2f), 4, 5, 3, meshes, &log);
        std::printf("koy %s; %zu kaya\n", log.c_str(), isl.boulders.size());
        koyBuildPier(koyPierSpec(cfg), fy, 6, 7, 8, 9, 10, 4, meshes, &log);
        std::printf("koy %s\n", log.c_str());
        for (size_t k = first; k < meshes.size(); ++k) {
            KInstanceCPU in;
            in.mesh = int(k);
            instances.push_back(in);
        }
        std::string blog;
        buoys.build(cfg, fy, meshes, instances, 11, 12, 13, 7, 14, 5, &blog);
        std::printf("koy dubalar: %s\n", blog.c_str());
        // the wreck
        float3 wreckBow{}, wreckStern{};
        std::vector<std::pair<float3, float3>> wreckGrowth;
        {
            std::string wlog;
            const size_t w0 = meshes.size();
            KoyWreckPlacement wp = koyBuildWreck(cfg, [&](float x, float z) { return fs.base(x, z); }, 21, 22, 16, 8, meshes, &wlog);
            wreckBow = wp.bow;
            wreckStern = wp.stern;
            std::printf("koy %s\n", wlog.c_str());
            {   // the fans' sites come from the hull's plan (the rail every 0.8 m, a rib's broken top at a random height): where
                // the rail is gone or the rib broke lower they stood in the water (Ozcan 2026-10-05). Put each on the wood
                // that is there; none within reach (or only an underside): no fan
                std::vector<const KMeshCPU*> hull;
                for (size_t k = w0; k < meshes.size(); ++k) hull.push_back(&meshes[k]);
                const KoySurfaceSnap snap(hull, 0.25f);
                std::vector<std::pair<float3, float3>> kept;
                int onWood = 0, moved = 0, far = 0, dropped = 0;
                float worst = 0.0f;
                for (const auto& g : wp.growth) {
                    float3 q, n;
                    float d = 0.0f;
                    if (!snap.nearest(g.first, 0.35f, q, n, &d) || n.y < -0.2f) {
                        // the rib broke lower, the rail is gone: the nearest wood that looks up within reach, or none
                        if (!snap.nearest(g.first, 1.5f, q, n, &d) || n.y < 0.3f) {
                            ++dropped;
                            continue;
                        }
                        ++far;
                        kept.push_back({q, normalize(g.second + n)});
                        continue;
                    }
                    if (d > 0.03f) {
                        ++moved;
                        worst = std::max(worst, d);
                    } else {
                        ++onWood;
                    }
                    kept.push_back({q, normalize(g.second + n)});
                }
                std::printf("koy batik yelpaze yerleri: %d, yuzeyde %d, tasinan %d (en uzak %.2f m), 1,5 m icinde yukari bakan tahtaya "
                            "tasinan %d, atilan %d\n",
                            int(wp.growth.size()), onWood, moved, worst, far, dropped);
                wp.growth = kept;
            }
            wreckGrowth = wp.growth;
            std::vector<KMeshCPU> fans;
            std::string flog;
            koyBuildFans(cfg, wp.growth, 23, 24, 9, fans, &flog);
            std::printf("koy %s\n", flog.c_str());
            for (KMeshCPU& m : fans) meshes.push_back(std::move(m));
            if (cfg.i("debris.use", 1)) {   // the debris field: timbers, barrels, round shot, the ballast's mound
                std::vector<KMeshCPU> deb;
                std::string dlog;
                koyWreckDebris(cfg, fy, wp.bow, wp.stern, {21, 22, 16}, 8, deb, &dlog);
                std::printf("koy enkaz: %s\n", dlog.c_str());
                for (KMeshCPU& m : deb) meshes.push_back(std::move(m));
            }
            if (cfg.i("foul.use", 1)) {   // years under water: mussel clumps, sponges, sea squirts, crusts
                std::vector<KMeshCPU> foul;
                std::string olog;
                koyFoulWreck(cfg, meshes[w0], meshes[w0 + 1], {14, 26, 27, 28, 29, 30}, 12, foul, &olog);
                std::printf("koy batik ortusu: %s\n", olog.c_str());
                for (KMeshCPU& m : foul) meshes.push_back(std::move(m));
            }
            for (size_t k = w0; k < meshes.size(); ++k) {
                KInstanceCPU in;
                in.mesh = int(k);
                instances.push_back(in);
            }
        }
        // Ada enriched: the outcrops (lane, near, far), meshed per cluster
        KoyReefRocks reef;
        const size_t reef0 = meshes.size();
        size_t reefEnd = reef0;
        if (cfg.i("reef.use", 1)) {
            KoyReefAvoid av;
            av.island = [&](float3 q) { return isl.sdf(q); };
            av.amphoraCentre = cfg.v3("amfora.centre", make_float3(-3.0f, 0.0f, -52.0f));
            av.wreckBow = wreckBow;
            av.wreckStern = wreckStern;
            reef = koyReefRocks(cfg, fy, av);
            std::string rlog;
            koyBuildReefRocks(reef, 25, 11, meshes, &rlog);
            std::printf("koy kayaliklar: %s\n", rlog.c_str());
            reefEnd = meshes.size();
            for (size_t k = reef0; k < meshes.size(); ++k) {
                KInstanceCPU in;
                in.mesh = int(k);
                instances.push_back(in);
            }
        }
        // sea fans on the deep outcrops (red Paramuricea, yellow Eunicella), as on the wreck
        if (cfg.i("reef.fans", 90) > 0 && reefEnd > reef0) {
            std::vector<const KMeshCPU*> rr;
            for (size_t k = reef0; k < reefEnd; ++k) rr.push_back(&meshes[k]);
            if (amphoraRockMesh >= 0) rr.push_back(&meshes[size_t(amphoraRockMesh)]);
            const auto sites = koyReefFanSites(rr, cfg.f("reef.fan_depth", 13.0f), cfg.i("reef.fans", 90), 1.6f, 911u);
            std::vector<KMeshCPU> rf;
            std::string flog;
            koyBuildFans(cfg, sites, 23, 24, 9, rf, &flog);
            std::printf("koy kayalik mercanlari: %s\n", flog.c_str());
            for (KMeshCPU& m : rf) {
                m.scatter = true;
                meshes.push_back(std::move(m));
                KInstanceCPU in;
                in.mesh = int(meshes.size()) - 1;
                instances.push_back(in);
            }
        }
        // the shallows' small things, and the urchins on the island's wet rock, the amphorae's rocks and the outcrops
        std::vector<const KMeshCPU*> rockMeshes = {&meshes[first]};
        if (amphoraRockMesh >= 0) rockMeshes.push_back(&meshes[size_t(amphoraRockMesh)]);
        for (size_t k = reef0; k < reefEnd; ++k) rockMeshes.push_back(&meshes[k]);
        std::vector<KMeshCPU> props;
        std::string plog;
        koyBuildProps(cfg, fy, [&](float3 q) { return isl.sdf(q) < 0.15f || amphoraRocks.sdf(q) < 0.1f || reef.sdf(q) < 0.1f; }, rockMeshes,
                      {25, 16, 17, 18, 19, 20}, 7, props, &plog);   // the light pebbles in the reef's limestone (turf, grain)
        for (KMeshCPU& m : props) {
            m.scatter = true;
            m.aoRange = 0.6f;
        }
        std::printf("koy susler: %s\n", plog.c_str());
        if (cfg.i("life.use", 1)) {   // Ada enriched: shells, sand stars; Padina and sponges on the rocks
            std::string llog;
            // the shell-strewn patches: the floor map's rule, the same noise (koy_reef.cpp koyBuildFloorMask)
            const float cover = cfg.f("floor2.cover", 0.42f);
            auto shelly = [&](float x, float z) {
                const float big = kn::fbm3(x * 0.045f + 3.1f, 0.7f, z * 0.045f, 3), small = kn::fbm3(x * 0.21f, 4.2f, z * 0.21f + 7.0f, 2);
                const float v = 0.6f * big + 0.28f * small + (cover - 0.42f) * 2.0f;
                return std::min(std::max((v - 0.02f) / 0.3f, 0.0f), 1.0f);
            };
            koyBuildSandLife(cfg, fy, [&](float3 q) { return isl.sdf(q) < 0.15f || amphoraRocks.sdf(q) < 0.1f || reef.sdf(q) < 0.1f; }, shelly, rockMeshes,
                             {31, 32, 33, 34, 26, 27, 29}, 13, props, &llog);
            std::printf("koy canlilar: %s\n", llog.c_str());
        }
        for (KMeshCPU& m : props) {
            meshes.push_back(std::move(m));
            KInstanceCPU in;
            in.mesh = int(meshes.size()) - 1;
            instances.push_back(in);
        }
        // the floor's variety: the shell-strewn sand, silt about the wreck, grit at the rocks' feet
        {
            std::string mlog;
            const std::vector<std::function<float(float3)>> rockSdfs = {[&](float3 q) { return isl.sdf(q); }, [&](float3 q) { return amphoraRocks.sdf(q); },
                                                                         [&](float3 q) { return reef.sdf(q); }};
            floorMask = koyBuildFloorMask(cfg, fy, rockSdfs, wreckBow, wreckStern, &mlog);
            std::printf("koy taban haritasi: %s\n", mlog.c_str());
        }
    }
    if (cfg.i("scatter.tiles", 1)) tileScatter(cfg.f("scatter.tile", 30.0f));
    applyTextureRecipe(cfg);
}

// Everything spread over the whole floor (the outcrops, the pebbles, shells, stars, sponges, the reef's fans) was one mesh
// per kind, each one instance whose bounds covered the scene: every ray entered every one of them (Ada enriched: +2 ms
// where none of them shows). Merged by tile instead (a material per triangle), the bounds are local: a ray meets only the
// tiles it crosses. The big things (rocks, fans: a long occlusion range) and the small (a short one) tile apart. Their
// instances come after the moving floats' (whose indices are kept).
void KoyWorld::tileScatter(float tile) {
    std::vector<KMeshCPU> keep, scat;
    std::vector<int> remap(meshes.size(), -1);
    for (size_t k = 0; k < meshes.size(); ++k) {
        if (meshes[k].scatter) {
            scat.push_back(std::move(meshes[k]));
        } else {
            remap[k] = int(keep.size());
            keep.push_back(std::move(meshes[k]));
        }
    }
    if (scat.empty()) {
        meshes = std::move(keep);
        return;
    }
    std::vector<KInstanceCPU> inst;
    for (const KInstanceCPU& in : instances)
        if (in.mesh >= 0 && size_t(in.mesh) < remap.size() && remap[size_t(in.mesh)] >= 0) {
            KInstanceCPU q = in;
            q.mesh = remap[size_t(in.mesh)];
            inst.push_back(q);
        }
    struct Key { int x, z, big; bool operator<(const Key& o) const { return x != o.x ? x < o.x : z != o.z ? z < o.z : big < o.big; } };
    std::map<Key, KMeshCPU> tiles;
    std::vector<int> stamp, local;
    for (const KMeshCPU& m : scat) {
        const int big = m.aoRange > 1.0f ? 1 : 0;
        stamp.assign(m.pos.size(), -1);
        local.assign(m.pos.size(), 0);
        // the triangles by tile
        std::map<Key, std::vector<size_t>> buckets;
        for (size_t t = 0; t < m.idx.size(); ++t) {
            const uint3 q = m.idx[t];
            const float3 c = (m.pos[q.x] + m.pos[q.y] + m.pos[q.z]) * (1.0f / 3.0f);
            buckets[{int(std::floor(c.x / tile)), int(std::floor(c.z / tile)), big}].push_back(t);
        }
        int tag = 0;
        for (auto& kv : buckets) {
            KMeshCPU& T = tiles[kv.first];
            if (T.idx.empty() && T.pos.empty()) {
                T.name = "dagilim";
                T.material = m.material;
                T.id = m.id;
                T.aoRange = big ? 4.0f : 0.6f;
            }
            ++tag;
            const unsigned base = unsigned(T.pos.size());
            unsigned added = 0;
            auto vtx = [&](unsigned v) {
                if (stamp[v] != tag) {
                    stamp[v] = tag;
                    local[v] = int(base + added++);
                    T.pos.push_back(m.pos[v]);
                    T.nrm.push_back(v < m.nrm.size() ? m.nrm[v] : make_float3(0, 1, 0));
                }
                return unsigned(local[v]);
            };
            for (size_t t : kv.second) {
                const uint3 q = m.idx[t];
                T.idx.push_back(make_uint3(vtx(q.x), vtx(q.y), vtx(q.z)));
                T.triMat.push_back((unsigned short)(m.triMat.empty() ? m.material : m.triMat[t]));
            }
        }
    }
    meshes = std::move(keep);
    for (auto& kv : tiles) {
        kv.second.uv.clear();
        meshes.push_back(std::move(kv.second));
        KInstanceCPU in;
        in.mesh = int(meshes.size()) - 1;
        inst.push_back(in);
    }
    instances = std::move(inst);
    std::printf("koy dagilim: %zu ag -> %zu karo (%.0f m)\n", scat.size(), tiles.size(), tile);
}

void KoyWorld::applyTextureRecipe(const Config& cfg) {
    KMaterial& f = materials[0];
    KMaterial& r = materials[1];
    const int rockDetail = setRockDetail;
    // the detail layers: the sand's own grain at half its tile; the rock a weathered limestone scan, finer
    f.detailSet = f.texSet >= 0 ? setSand : -1;
    f.detailTile = cfg.f("tex.sand_detail_tile", 1.75f);
    f.detailNormal = cfg.f("tex.sand_detail_normal", 0.5f);
    f.detailAlbedo = cfg.f("tex.sand_detail_albedo", 0.25f);
    r.detailSet = r.texSet >= 0 ? rockDetail : -1;
    r.detailTile = cfg.f("tex.rock_detail_tile", 1.8f);
    r.detailNormal = cfg.f("tex.rock_detail_normal", 0.6f);
    r.detailAlbedo = cfg.f("tex.rock_detail_albedo", 0.35f);
    // Amfora: the jars take the clay (2.00 m pressed to a 1.64 m tile: at 8K 0.2 mm a texel, the clay's grain 18 % finer
    // than scanned)
    for (size_t k : {size_t(4), size_t(5), size_t(25)}) {   // Ada: the island's rock (wet, dry), the reef's: the limestone detail scan
        if (k >= materials.size()) continue;
        KMaterial& q = materials[k];
        q.detailSet = rockDetail;
        q.detailTile = cfg.f("island.detail_tile", 2.4f);
        q.detailNormal = cfg.f("island.detail_normal", 0.9f);
        q.detailAlbedo = cfg.f("island.detail_albedo", 0.6f);
    }
    if (materials.size() > 3) {
        KMaterial& j = materials[3];
        j.detailSet = setClay;
        j.detailTile = cfg.f("amfora.detail_tile", 1.64f);
        j.detailNormal = cfg.f("amfora.detail_normal", 0.8f);
        j.detailAlbedo = cfg.f("amfora.detail_albedo", 0.55f);
    }
    // Ada (Ozcan, 2026-10-04: the 16K detail layer on every material it suits): the pier's concrete (7 wet, 8 dry) takes
    // its own 8K scan again at a third of the tile (0.9 m: 0.11 mm a texel, the 16K rock detail's density); the pebbles
    // (15 light, 16 dark) and the wreck's iron concretion (22) the rock detail itself
    if (materials.size() > 22) {
        for (size_t k : {size_t(7), size_t(8)}) {
            KMaterial& q = materials[k];
            q.detailSet = setConcrete >= 0 && cfg.i("pier.concrete_detail", 1) ? setConcrete : -1;
            q.detailTile = cfg.f("pier.concrete_detail_tile", 0.9f);
            q.detailNormal = cfg.f("pier.concrete_detail_normal", 0.5f);
            q.detailAlbedo = cfg.f("pier.concrete_detail_albedo", 0.3f);
        }
        for (size_t k : {size_t(15), size_t(16), size_t(22)}) {
            KMaterial& q = materials[k];
            q.detailSet = rockDetail;
            q.detailTile = cfg.f("props.stone_detail_tile", 1.8f);
            q.detailNormal = cfg.f("props.stone_detail_normal", k == 22 ? 0.7f : 0.8f);
            q.detailAlbedo = cfg.f("props.stone_detail_albedo", k == 22 ? 0.4f : 0.5f);
        }
    }
}


std::vector<KTextureSet> KoyWorld::textureSets() const {
    std::vector<KTextureSet> v;
    for (const auto& t : textures) v.push_back(t.set);
    return v;
}

void KoyWorld::release() {
    for (auto& t : textures) {
        for (auto o : t.textures) cudaDestroyTextureObject(cudaTextureObject_t(o));
        for (void* a : t.arrays) cudaFreeMipmappedArray(static_cast<cudaMipmappedArray_t>(a));
    }
    textures.clear();
    textureBytes = 0;
}

std::string KoyWorld::summary() const {
    size_t tris = 0;
    for (const auto& m : meshes) tris += m.idx.size();
    std::ostringstream s;
    s << meshes.size() << " ag (" << tris << " ucgen), " << textures.size() << " taranmis malzeme";
    return s.str();
}

// ---------------------------------------------------------------------------------------------------
namespace {
float srgbToLin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float linToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

// The CPU half of a texture, made off the CUDA thread: a 16K map is a gigabyte and seconds of work on one core. Level 0
// stays in the buffer it was decoded into (stb_image allocates with malloc: STBI_FREE is not overridden), no copy.
struct HostTexture {
    int w = 0, h = 0, levels = 0;
    bool srgb = false;
    bool found = false;                             // there was a file (base null then: it did not decode)
    unsigned char* base = nullptr;                  // level 0, RGBA8
    std::vector<std::vector<unsigned char>> mips;   // levels 1..
    float3 mean{};                                  // the one-texel level in linear light (sRGB)
    HostTexture() = default;
    HostTexture(const HostTexture&) = delete;
    HostTexture& operator=(const HostTexture&) = delete;
    HostTexture(HostTexture&& o) noexcept { *this = std::move(o); }
    HostTexture& operator=(HostTexture&& o) noexcept {
        if (this != &o) {
            std::free(base);
            w = o.w;
            h = o.h;
            levels = o.levels;
            srgb = o.srgb;
            found = o.found;
            mean = o.mean;
            base = o.base;
            o.base = nullptr;
            mips = std::move(o.mips);
        }
        return *this;
    }
    ~HostTexture() { std::free(base); }
    const unsigned char* level(int l) const { return l == 0 ? base : mips[size_t(l) - 1].data(); }
};

// the mip chain; sRGB: averaged in linear light (the hardware decodes it on fetch)
void buildMips(HostTexture& t) {
    t.levels = 1;
    while ((t.w >> t.levels) > 0 || (t.h >> t.levels) > 0) ++t.levels;
    float lut[256];
    for (int i = 0; i < 256; ++i) lut[i] = t.srgb ? srgbToLin(i / 255.0f) : i / 255.0f;
    std::vector<unsigned char> inv(4096);   // linear 0..1 in 4096 steps -> 8-bit encoded
    for (int i = 0; i < 4096; ++i) {
        const float v = i / 4095.0f;
        inv[i] = (unsigned char)std::min(255.0f, std::max(0.0f, (t.srgb ? linToSrgb(v) : v) * 255.0f + 0.5f));
    }
    t.mips.clear();
    t.mips.reserve(size_t(t.levels));
    const unsigned char* px = t.base;
    int cw = t.w, ch = t.h;
    for (int l = 1; l < t.levels; ++l) {
        const int nw = std::max(1, cw / 2), nh = std::max(1, ch / 2);
        std::vector<unsigned char> nx(size_t(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    float s = 0.0f;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(cw - 1, 2 * x + dx), sy = std::min(ch - 1, 2 * y + dy);
                            const unsigned char v = px[(size_t(sy) * cw + sx) * 4 + c];
                            s += (c < 3) ? lut[v] : v / 255.0f;
                        }
                    s *= 0.25f;
                    nx[(size_t(y) * nw + x) * 4 + c] = c < 3 ? inv[std::min(4095, std::max(0, int(s * 4095.0f + 0.5f)))]
                                                             : (unsigned char)std::min(255.0f, std::max(0.0f, s * 255.0f + 0.5f));
                }
        t.mips.push_back(std::move(nx));
        px = t.mips.back().data();
        cw = nw;
        ch = nh;
    }
    // the last level is one texel: the average
    if (t.srgb) t.mean = make_float3(lut[px[0]], lut[px[1]], lut[px[2]]);
}

// only the levels up to `cap` texels across: level 0 becomes the first that fits (the rest of the chain as it was)
void trimTo(HostTexture& t, int cap) {
    int drop = 0;
    while (drop + 1 < t.levels && std::max(t.w >> drop, t.h >> drop) > cap) ++drop;
    if (drop == 0) return;
    const std::vector<unsigned char>& top = t.mips[size_t(drop) - 1];
    unsigned char* nb = static_cast<unsigned char*>(std::malloc(top.size()));
    std::memcpy(nb, top.data(), top.size());
    std::free(t.base);
    t.base = nb;
    t.mips.erase(t.mips.begin(), t.mips.begin() + drop);
    t.w = std::max(1, t.w >> drop);
    t.h = std::max(1, t.h >> drop);
    t.levels -= drop;
}

// <base>.jpg/.png/.jpeg decoded to RGBA8 and mipmapped, at most cap texels across; an empty texture if there is no file
HostTexture decodeImage(const std::string& base, bool srgb, int cap = 1 << 30) {
    HostTexture t;
    for (const char* ext : {".jpg", ".png", ".jpeg"}) {
        const std::string p = base + ext;
        if (!std::filesystem::exists(p)) continue;
        t.found = true;
        int n = 0;
        t.base = stbi_load(p.c_str(), &t.w, &t.h, &n, 4);
        if (!t.base) {
            std::printf("koy: %s okunamadi (%s)\n", p.c_str(), stbi_failure_reason());
            return t;
        }
        t.srgb = srgb;
        buildMips(t);
        trimTo(t, cap);
        return t;
    }
    return t;
}

// the CUDA half: a mipmapped RGBA8 array and its texture object (sRGB read back decoded by the hardware)
bool uploadTexture(const HostTexture& t, KoyTextureSet& out, unsigned long long& tex, size_t* bytes) {
    if (!t.base) return false;
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<uchar4>();
    cudaMipmappedArray_t arr = nullptr;
    if (cudaMallocMipmappedArray(&arr, &fd, make_cudaExtent(t.w, t.h, 0), t.levels) != cudaSuccess) {
        std::printf("koy: %s icin %dx%d dokuya GPU bellegi yetmedi\n", out.name.c_str(), t.w, t.h);
        cudaGetLastError();
        return false;
    }
    int cw = t.w, ch = t.h;
    size_t total = 0;
    for (int l = 0; l < t.levels; ++l) {
        cudaArray_t a = nullptr;
        cudaGetMipmappedArrayLevel(&a, arr, l);
        if (cudaMemcpy2DToArray(a, 0, 0, t.level(l), size_t(cw) * 4, size_t(cw) * 4, ch, cudaMemcpyHostToDevice) != cudaSuccess) {
            cudaFreeMipmappedArray(arr);
            return false;
        }
        total += size_t(cw) * ch * 4;
        cw = std::max(1, cw / 2);
        ch = std::max(1, ch / 2);
    }
    if (t.srgb) out.set.meanAlbedo = t.mean;
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeMipmappedArray;
    rd.res.mipmap.mipmap = arr;
    cudaTextureDesc td{};
    td.addressMode[0] = td.addressMode[1] = cudaAddressModeWrap;
    td.filterMode = cudaFilterModeLinear;
    td.mipmapFilterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeNormalizedFloat;
    td.normalizedCoords = 1;
    td.sRGB = t.srgb ? 1 : 0;
    td.maxAnisotropy = 16;
    td.maxMipmapLevelClamp = float(t.levels - 1);
    cudaTextureObject_t to = 0;
    if (cudaCreateTextureObject(&to, &rd, &td, nullptr) != cudaSuccess) {
        cudaFreeMipmappedArray(arr);
        return false;
    }
    out.arrays.push_back(arr);
    out.textures.push_back(to);
    tex = to;
    if (bytes) *bytes += total;
    return true;
}

void releaseSet(KoyTextureSet& t) {
    for (auto o : t.textures) cudaDestroyTextureObject(cudaTextureObject_t(o));
    for (void* a : t.arrays) cudaFreeMipmappedArray(static_cast<cudaMipmappedArray_t>(a));
    t.textures.clear();
    t.arrays.clear();
}

// The upload of a decoded set, all or nothing: every map whose file is there must decode and fit on the GPU, or the
// set is dropped whole (a mode never runs on half a set, say the 16K sand without its normal map)
bool uploadSet(const std::string& name, HostTexture albedo, HostTexture normal, HostTexture rough, KoyTextureSet& out, size_t* bytes) {
    out = KoyTextureSet();
    out.name = name;
    out.set.texels = float(std::max(albedo.w, albedo.h));
    size_t added = 0;
    bool ok = albedo.base && uploadTexture(albedo, out, out.set.albedo, &added);
    if (ok && normal.found) {
        ok = uploadTexture(normal, out, out.set.normal, &added);
        out.set.hasNormal = ok ? 1 : 0;
    }
    if (ok && rough.found) ok = uploadTexture(rough, out, out.set.rough, &added);
    if (!ok) {
        releaseSet(out);
        std::printf("koy: malzeme %s yuklenemedi\n", name.c_str());
        return false;
    }
    if (bytes) *bytes += added;
    std::printf("koy: malzeme %s %dx%d%s\n", name.c_str(), albedo.w, albedo.h, out.set.hasNormal ? " (normal haritali)" : "");
    return true;
}
int imageWidth(const std::string& base) {
    for (const char* ext : {".jpg", ".png", ".jpeg"}) {
        const std::string p = base + ext;
        if (!std::filesystem::exists(p)) continue;
        int w = 0, h = 0, n = 0;
        return stbi_info(p.c_str(), &w, &h, &n) ? std::max(w, h) : 0;
    }
    return 0;
}
bool hasImage(const std::string& base) {
    for (const char* ext : {".jpg", ".png", ".jpeg"})
        if (std::filesystem::exists(base + ext)) return true;
    return false;
}
}   // namespace

bool koyLoadTextureSet(const std::string& dir, const std::string& name, KoyTextureSet& out) {
    const std::string base = dir + "/" + name + "/";
    return uploadSet(name, decodeImage(base + "albedo", true), decodeImage(base + "normal", false), decodeImage(base + "rough", false), out, nullptr);
}

// A tier on its way: every role's maps decoding at once, each on its own thread (a 16K map is seconds of work on one
// core); the uploads follow on the CUDA thread, one set a call, each as soon as its maps are ready.
struct KoyWorld::TexLoad {
    struct Job {
        int* index = nullptr;
        int role = 0;
        std::string name, smallest;   // the folder chosen; the role's smallest (should the GPU not take this one)
        int cap = 0;
        double weight = 0.0;          // the source's texels, all maps (the decode's work: an 8K source cut to 1K is still 8K)
        double started = 0.0;         // seconds after t0
        bool done = false;
        std::future<HostTexture> albedo, normal, rough;
    };
    std::vector<Job> jobs;
    std::chrono::steady_clock::time_point t0;
    double total = 0.0;
    double rate = 0.0;                // texels a second one decoding thread managed (measured), 0 before the first
    double seconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
    void start(Job& j, const std::string& dir) {
        const std::string base = dir + "/" + j.name + "/";
        const int cap = j.cap;
        j.started = seconds();
        j.albedo = std::async(std::launch::async, [base, cap] { return decodeImage(base + "albedo", true, cap); });
        j.normal = std::async(std::launch::async, [base, cap] { return decodeImage(base + "normal", false, cap); });
        j.rough = std::async(std::launch::async, [base, cap] { return decodeImage(base + "rough", false, cap); });
    }
};

void KoyWorld::beginTextures(const Config& cfg, int tier) {
    release();   // the tier on screen leaves the GPU first: the next may need all of it
    tier = std::min(std::max(tier, 0), 2);
    texMode = tier;
    textureBytes = 0;
    texLoad.reset();
    setSand = setRock = setRockDetail = setClay = setCoat = setWood = setConcrete = setFloor2 = -1;
    if (cfg.f("tex.use", 1.0f) < 0.5f) return;
    textures.assign(TR_COUNT, KoyTextureSet());
    auto L = std::make_shared<TexLoad>();
    L->t0 = std::chrono::steady_clock::now();
    const int cap = tier == 0 ? cfg.i("tex.low_texels", 1024) : tier == 1 ? cfg.i("tex.normal_texels", 2048) : (1 << 30);
    struct Role {
        int role;
        std::vector<const char*> names;   // the same scan at its sizes
        int* index;
        bool want;
    };
    const std::vector<Role> roles = {{TR_SAND, {"kum", "kum_4k", "kum_8k", "kum_16k"}, &setSand, true},   // ambientCG Ground054
                                     {TR_ROCK, {"kaya_model", "kaya_model_8k"}, &setRock, true},          // boulder_01
                                     {TR_ROCK_DETAIL, {"kaya_ayrinti", "kaya_ayrinti_8k", "kaya_ayrinti_16k"}, &setRockDetail, true},
                                     {TR_CLAY, {"amfora_kil_8k"}, &setClay, cfg.i("amfora.use", 0) != 0},
                                     {TR_COAT, {"kabuk_8k"}, &setCoat, true},
                                     {TR_WOOD, {"ahsap_4k", "ahsap_8k"}, &setWood, true},
                                     {TR_CONCRETE, {"beton_4k", "beton_8k"}, &setConcrete, true},
                                     {TR_FLOOR2, {"kum2_8k"}, &setFloor2, true}};
    for (const Role& r : roles) {
        if (!r.want) continue;
        // the smallest that is at least the cap (the least to decode); none that big: the biggest there is
        std::string pick, smallest;
        int pickW = 0, smallW = 0;
        for (const char* nm : r.names) {
            const int w = imageWidth(texDir + "/" + nm + "/albedo");
            if (w <= 0) continue;
            if (smallest.empty() || w < smallW) { smallest = nm; smallW = w; }
            const bool better = pick.empty() || (pickW < cap ? w > pickW : (w >= cap && w < pickW));
            if (better) { pick = nm; pickW = w; }
        }
        if (pick.empty()) {
            std::printf("koy: %s yok (python assets/koy/fetch.py)\n", r.names[0]);
            continue;
        }
        TexLoad::Job j;
        j.index = r.index;
        j.role = r.role;
        j.name = pick;
        j.smallest = smallest;
        j.cap = cap;
        j.weight = double(pickW) * pickW * (hasImage(texDir + "/" + pick + "/rough") ? 3.0 : 2.0);
        L->total += j.weight;
        *r.index = r.role;   // the slot is the role's (empty until its set is up)
        L->start(j, texDir);
        L->jobs.push_back(std::move(j));
    }
    texLoad = L;
}

bool KoyWorld::pollTextures(float* fraction, std::string* what) {
    if (!texLoad) {
        if (fraction) *fraction = 1.0f;
        return true;
    }
    TexLoad& L = *texLoad;
    auto ready = [](std::future<HostTexture>& f) { return !f.valid() || f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; };
    for (TexLoad::Job& j : L.jobs) {
        if (j.done || !(ready(j.albedo) && ready(j.normal) && ready(j.rough))) continue;
        const double took = std::max(L.seconds() - j.started, 1e-3);
        L.rate = std::max(L.rate, j.weight / took);   // the fastest thread so far (the jobs share the cores)
        KoyTextureSet t;
        if (uploadSet(j.name, j.albedo.get(), j.normal.get(), j.rough.get(), t, &textureBytes)) {
            textures[size_t(j.role)] = t;
            j.done = true;
        } else if (j.cap > 1024 || j.name != j.smallest) {   // the GPU did not take it: the smallest, at 1K, rather than none
            std::printf("koy: %s yerine %s 1K deneniyor\n", j.name.c_str(), j.smallest.c_str());
            j.name = j.smallest;
            j.cap = 1024;
            L.start(j, texDir);
        } else {
            *j.index = -1;
            j.done = true;
        }
        break;   // one upload a call: the bar is drawn between them
    }
    double got = 0.0, biggest = 0.0;
    const double now = L.seconds(), rate = L.rate > 0.0 ? L.rate : 2.5e7;   // before any is done: a guess (an 8K JPEG ~5 s)
    bool all = true;
    for (const TexLoad::Job& j : L.jobs) {
        if (j.done) {
            got += j.weight;
            continue;
        }
        all = false;
        got += j.weight * std::min(0.95, (now - j.started) * rate / j.weight);   // its decode, estimated from the time
        if (j.weight > biggest && what) {
            biggest = j.weight;
            *what = j.name;
        }
    }
    if (fraction) *fraction = L.total > 0.0 ? float(std::min(1.0, got / L.total)) : 1.0f;
    if (!all) return false;
    textureSeconds = now;
    size_t freeB = 0, totalB = 0;
    cudaMemGetInfo(&freeB, &totalB);
    std::printf("koy dokular: %s, %.1f s, dizilerde %.2f GB; kartta %.2f / %.2f GB dolu\n", texMode == 0 ? "dusuk" : texMode == 1 ? "normal" : "yuksek",
                textureSeconds, textureBytes / 1073741824.0, (totalB - freeB) / 1073741824.0, totalB / 1073741824.0);
    texLoad.reset();
    return true;
}

void KoyWorld::loadTexturesNow(const Config& cfg, int tier) {
    beginTextures(cfg, tier);
    while (!pollTextures()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
}
