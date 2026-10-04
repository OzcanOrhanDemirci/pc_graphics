// Scene 5 "Koy": the trial area's world (see koy_world.h).
#include "scenes/koy/koy_world.h"

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
float KoyFloorShape::operator()(float x, float z) const {
    float d = depth(sOf(x, z));
    // long low sand waves (tens of metres) and small undulations, growing a little with depth
    d += sandWave * fbm3(x * 0.035f, 7.1f, z * 0.035f, 2) + undulation * (1.0f + 0.01f * d) * fbm3(x * 0.11f, 0.37f, z * 0.11f, 3);
    return -std::max(d, shallowest);
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
    loadTextures(cfg, tdir);
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
    KMaterial reefM = islandWet;
    reefM.albedo = cfg.v3("reef.rgb", make_float3(0.24f, 0.23f, 0.20f));
    if (setCoat >= 0) coat(reefM, "reef", 0.32f, 0.45f, make_float3(0.30f, 0.27f, 0.23f), 1.0f, 0.55f, 0.4f);
    // the turf: a dense, dark olive-brown felt of small algae on everything that looks up (not the sand's grey silt)
    reefM.sedimentColor = cfg.v3("reef.turf_rgb", make_float3(0.12f, 0.13f, 0.06f));
    reefM.filmColor = cfg.v3("reef.film_rgb", make_float3(0.62f, 0.78f, 0.52f));
    reefM.life = cfg.f("reef.life", 1.0f);
    materials = {floorM, rockM, spareM, jarM, islandWet, islandDry, woodM, concWet, concDry, steelM, rustM, floatM, bigM, ropeM, musselM,
                 stoneL, stoneD, starM, cukeM, pinnaM, urchinM, wreckWood, wreckIron, fanRed, fanYellow, reefM,
                 spongeO, spongeY, squirtM, crustM, hydroM};

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
            const KoyWreckPlacement wp = koyBuildWreck(cfg, fy, 21, 22, 16, 8, meshes, &wlog);
            wreckBow = wp.bow;
            wreckStern = wp.stern;
            wreckGrowth = wp.growth;
            std::printf("koy %s\n", wlog.c_str());
            std::vector<KMeshCPU> fans;
            std::string flog;
            koyBuildFans(cfg, wp.growth, 23, 24, 9, fans, &flog);
            std::printf("koy %s\n", flog.c_str());
            for (KMeshCPU& m : fans) meshes.push_back(std::move(m));
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
            for (size_t k = reef0; k < meshes.size(); ++k) {
                KInstanceCPU in;
                in.mesh = int(k);
                instances.push_back(in);
            }
        }
        // the shallows' small things, and the urchins on the island's wet rock, the amphorae's rocks and the outcrops
        std::vector<const KMeshCPU*> rockMeshes = {&meshes[first]};
        if (amphoraRockMesh >= 0) rockMeshes.push_back(&meshes[size_t(amphoraRockMesh)]);
        for (size_t k = reef0; k < meshes.size(); ++k) rockMeshes.push_back(&meshes[k]);
        std::vector<KMeshCPU> props;
        std::string plog;
        koyBuildProps(cfg, fy, [&](float3 q) { return isl.sdf(q) < 0.15f || amphoraRocks.sdf(q) < 0.1f || reef.sdf(q) < 0.1f; }, rockMeshes,
                      {15, 16, 17, 18, 19, 20}, 7, props, &plog);
        std::printf("koy susler: %s\n", plog.c_str());
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
    applyTextureMode(cfg, cfg.i("tex.mode", 2));
}

bool KoyWorld::hasMode(int mode) const {
    if (mode >= 2) return setSand16k >= 0 && setRockDetail16k >= 0 && setRock8k >= 0;
    if (mode == 1) return setSand4k >= 0 && setRock8k >= 0;
    return true;
}

void KoyWorld::applyTextureMode(const Config& cfg, int mode) {
    mode = std::min(std::max(mode, 0), 2);
    while (mode > 0 && !hasMode(mode)) --mode;   // ultra without its 16K files falls to high, high without 8K to standard
    texMode = mode;
    KMaterial& f = materials[0];
    KMaterial& r = materials[1];
    const bool high = mode >= 1;
    // ultra is high with the 16K sand and the 16K rock detail: the same recipe, only the resolution differs
    const int sandHigh = mode == 2 ? setSand16k : setSand4k;
    const int rockDetail = mode == 2 ? setRockDetail16k : setRockDetail;
    if (f.texSet >= 0) f.texSet = high ? sandHigh : setSand;
    if (r.texSet >= 0) r.texSet = high ? setRock8k : setRock;
    // the detail layers: the sand's own grain at half its tile; the rock a weathered limestone scan, finer
    f.detailSet = high && f.texSet >= 0 ? sandHigh : -1;
    f.detailTile = cfg.f("tex.sand_detail_tile", 1.75f);
    f.detailNormal = cfg.f("tex.sand_detail_normal", 0.5f);
    f.detailAlbedo = cfg.f("tex.sand_detail_albedo", 0.25f);
    r.detailSet = high && r.texSet >= 0 ? rockDetail : -1;
    r.detailTile = cfg.f("tex.rock_detail_tile", 1.8f);
    r.detailNormal = cfg.f("tex.rock_detail_normal", 0.6f);
    r.detailAlbedo = cfg.f("tex.rock_detail_albedo", 0.35f);
    // Amfora: the jars take the clay (8K in both high modes; 2.00 m pressed to a 1.64 m tile: 0.2 mm a texel, the clay's
    // grain 18 % finer than scanned)
    for (size_t k : {size_t(4), size_t(5), size_t(25)}) {   // Ada: the island's rock (wet, dry), the reef's: the limestone detail scan
        if (k >= materials.size()) continue;
        KMaterial& q = materials[k];
        q.detailSet = rockDetail >= 0 ? rockDetail : setRockDetail;
        q.detailTile = cfg.f("island.detail_tile", 2.4f);
        q.detailNormal = cfg.f("island.detail_normal", 0.9f);
        q.detailAlbedo = cfg.f("island.detail_albedo", 0.6f);
    }
    if (materials.size() > 3) {
        KMaterial& j = materials[3];
        j.detailSet = high ? setClay : -1;
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
            q.detailSet = high && setConcrete >= 0 && cfg.i("pier.concrete_detail", 1) ? setConcrete : -1;
            q.detailTile = cfg.f("pier.concrete_detail_tile", 0.9f);
            q.detailNormal = cfg.f("pier.concrete_detail_normal", 0.5f);
            q.detailAlbedo = cfg.f("pier.concrete_detail_albedo", 0.3f);
        }
        for (size_t k : {size_t(15), size_t(16), size_t(22)}) {
            KMaterial& q = materials[k];
            q.detailSet = high ? rockDetail : -1;
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

// <base>.jpg/.png/.jpeg decoded to RGBA8 and mipmapped; an empty texture if there is no such file
HostTexture decodeImage(const std::string& base, bool srgb) {
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

// a set's maps, decoding (each on its own thread) or decoded; `rest`: the slot's other folders, should this one fail
struct PendingSet {
    std::string name;
    std::vector<std::string> rest;
    int* index = nullptr;
    std::future<HostTexture> albedo, normal, rough;
};

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

// Every set's maps are decoded and mipmapped at once, each on its own thread (the 16K maps alone take seconds each on
// one core); the uploads follow here, in order, on the CUDA thread, each as soon as its maps are ready. The slots name
// their fallbacks: the first folder that has an albedo is loaded.
void KoyWorld::loadTextures(const Config& cfg, const std::string& dir) {
    const auto t0 = std::chrono::steady_clock::now();
    textureBytes = 0;
    setSand = setRock = setSand4k = setRock8k = setRockDetail = setSand16k = setRockDetail16k = setClay = setCoat = setWood = setConcrete = -1;
    setFloor2 = -1;
    if (cfg.f("tex.use", 1.0f) < 0.5f) return;
    struct Slot {
        std::vector<const char*> names;
        int* index;
    };
    std::vector<Slot> slots = {{{"kum"}, &setSand}, {{"kaya_model"}, &setRock}};
    if (cfg.i("tex.high", 1)) {   // everything at 8K (panel "Yüksek")
        slots.push_back({{"kum_8k", "kum_4k"}, &setSand4k});
        slots.push_back({{"kaya_model_8k"}, &setRock8k});
        slots.push_back({{"kaya_ayrinti_8k", "kaya_ayrinti"}, &setRockDetail});
        if (cfg.i("amfora.use", 0)) slots.push_back({{"amfora_kil_8k"}, &setClay});   // the jars' clay
        slots.push_back({{"kabuk_8k"}, &setCoat});      // the sea's concretion (jars, rock, piles, the wreck)
        slots.push_back({{"ahsap_8k", "ahsap_4k"}, &setWood});      // the pier's and the wreck's boards
        slots.push_back({{"beton_8k", "beton_4k"}, &setConcrete});  // the pier's concrete
        slots.push_back({{"kum2_8k"}, &setFloor2});                 // the shell-strewn sand's patches
        if (cfg.i("tex.ultra", 1)) {   // the sand and the rock detail at 16K (panel "Ultra")
            slots.push_back({{"kum_16k"}, &setSand16k});
            slots.push_back({{"kaya_ayrinti_16k"}, &setRockDetail16k});
        }
    }
    std::vector<PendingSet> pending;
    for (const Slot& s : slots) {
        std::vector<std::string> found;
        for (const char* nm : s.names)
            if (hasImage(dir + "/" + nm + "/albedo")) found.push_back(nm);
        if (found.empty()) {
            std::printf("koy: %s yok (python assets/koy/fetch.py)\n", s.names[0]);
            continue;
        }
        PendingSet p;
        p.name = found[0];
        p.rest.assign(found.begin() + 1, found.end());
        p.index = s.index;
        const std::string base = dir + "/" + p.name + "/";
        p.albedo = std::async(std::launch::async, [base] { return decodeImage(base + "albedo", true); });
        p.normal = std::async(std::launch::async, [base] { return decodeImage(base + "normal", false); });
        p.rough = std::async(std::launch::async, [base] { return decodeImage(base + "rough", false); });
        pending.push_back(std::move(p));
    }
    for (PendingSet& p : pending) {
        KoyTextureSet t;
        bool ok = uploadSet(p.name, p.albedo.get(), p.normal.get(), p.rough.get(), t, &textureBytes);
        for (size_t k = 0; !ok && k < p.rest.size(); ++k) {   // the slot's next folder, here and now (a failure path)
            const std::string base = dir + "/" + p.rest[k] + "/";
            ok = uploadSet(p.rest[k], decodeImage(base + "albedo", true), decodeImage(base + "normal", false), decodeImage(base + "rough", false), t,
                           &textureBytes);
        }
        if (ok) {
            textures.push_back(t);
            *p.index = int(textures.size()) - 1;
        }
    }
    textureSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
