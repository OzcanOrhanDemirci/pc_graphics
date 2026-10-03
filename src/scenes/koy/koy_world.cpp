// Scene 5 "Koy": the trial area's world (see koy_world.h).
#include "scenes/koy/koy_world.h"

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <random>
#include <sstream>

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

// grid coordinates: fine near the centre, growing outwards (the camera stays in the trial area)
std::vector<float> gridAxis(float inner, float h0, float outer, float hMax) {
    std::vector<float> pos{0.0f};
    float x = 0.0f;
    while (x < outer) {
        const float h = x < inner ? h0 : std::min(hMax, h0 + (x - inner) * 0.012f);
        x += h;
        pos.push_back(std::min(x, outer));
    }
    std::vector<float> axis;
    for (size_t k = pos.size(); k-- > 1;) axis.push_back(-pos[k]);
    for (float p : pos) axis.push_back(p);
    return axis;
}

}   // namespace

// The floor's broad shape (the mesh). Its sand ripples are NOT in the mesh: sampled at the vertices they beat against the
// grid (a 9 cm ripple on a 3 cm grid) and the shading shows wide moire bands. They live in a mipmapped normal map that
// the ray cone filters (koyRipple, KoyWorld::buildRipples): sharp near, averaged out far, never aliased.
float koyFloor(const Config& cfg, float x, float z) {
    float y = cfg.f("floor.depth", -7.0f) + cfg.f("floor.slope", 0.05f) * z;
    y += cfg.f("floor.undulation", 0.45f) * fbm3(x * 0.11f, 0.37f, z * 0.11f, 3);
    return y;
}
float koyRipple(const Config& cfg, float x, float z) {
    const float th = 0.35f + 0.5f * noise3(x * 0.07f, 1.7f, z * 0.07f);
    const float wl = cfg.f("floor.ripple_length", 0.09f);
    const float ph = (x * std::cos(th) + z * std::sin(th)) * 6.2831853f / wl + 2.2f * noise3(x * 0.45f, 3.1f, z * 0.45f);
    const float A = cfg.f("floor.ripple_height", 0.011f) * (0.65f + 0.35f * noise3(x * 0.3f, 5.3f, z * 0.3f));
    return A * (0.55f * std::sin(ph) + 0.22f * std::sin(2.0f * ph + 0.7f));
}

// ---------------------------------------------------------------------------------------------------
void KoySeagrass::build(const Config& cfg) {
    leaves.clear();
    std::mt19937 rng(cfg.i("grass.seed", 11));
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const float3 c = cfg.v3("grass.centre", make_float3(3.4f, 0.0f, -4.0f));
    const float R = cfg.f("grass.radius", 2.6f);
    const float spacing = 1.0f / std::sqrt(cfg.f("grass.shoots_per_m2", 400.0f));
    for (float x = c.x - R - 0.6f; x <= c.x + R + 0.6f; x += spacing)
        for (float z = c.z - R - 0.6f; z <= c.z + R + 0.6f; z += spacing) {
            const float jx = x + (U(rng) - 0.5f) * spacing, jz = z + (U(rng) - 0.5f) * spacing;
            const float d = std::sqrt((jx - c.x) * (jx - c.x) + (jz - c.z) * (jz - c.z));
            const float edge = R * (1.0f + 0.25f * noise3(jx * 0.9f, 7.7f, jz * 0.9f));
            if (d > edge) continue;
            const float dens = smooth01(edge, edge - 0.5f, d);
            if (U(rng) > 0.35f + 0.65f * dens) continue;
            const float shootYaw = U(rng) * 6.2831853f;
            const int nl = 4 + int(U(rng) * 3.0f);
            const float y = koyFloor(cfg, jx, jz) - 0.01f;
            for (int k = 0; k < nl; ++k) {
                KoyLeaf L;
                L.base = make_float3(jx + (U(rng) - 0.5f) * 0.012f, y, jz + (U(rng) - 0.5f) * 0.012f);
                L.yaw = shootYaw + (k - 0.5f * nl) * 0.35f + (U(rng) - 0.5f) * 0.3f;
                L.lean = 0.08f + 0.3f * U(rng) * (k == 0 || k == nl - 1 ? 1.5f : 1.0f);
                L.length = (cfg.f("grass.length_min", 0.32f) + (cfg.f("grass.length_max", 0.75f) - cfg.f("grass.length_min", 0.32f)) * U(rng)) *
                           (0.7f + 0.3f * dens);
                L.halfWidth = cfg.f("grass.half_width", 0.0048f) * (0.85f + 0.3f * U(rng));
                L.phase = U(rng);
                L.stiffness = 0.8f + 0.5f * U(rng);
                leaves.push_back(L);
            }
        }
    // the swell's current at the floor (linear wave theory): amplitude a, period T, depth d -> u_b = a w / sinh(k d)
    const float a = cfg.f("swell.amplitude", 0.12f), T = cfg.f("swell.period", 7.0f), d = -cfg.f("floor.depth", -7.0f);
    const float w = 2.0f * 3.14159265f / T;
    float k = w * w / 9.81f;
    for (int it = 0; it < 50; ++it) k = w * w / (9.81f * std::tanh(k * d));
    const float dirDeg = cfg.f("swell.direction", 250.0f) * 3.14159265f / 180.0f;
    current.dir = make_float3(std::cos(dirDeg), 0.0f, std::sin(dirDeg));
    current.speed = a * w / std::sinh(k * d);
    current.omega = w;
    current.k = k;
    current.flutter = cfg.f("grass.flutter", 0.04f);
    cudaMalloc(reinterpret_cast<void**>(&dLeaves), std::max<size_t>(1, leaves.size()) * sizeof(KoyLeaf));
    cudaMemcpy(dLeaves, leaves.data(), leaves.size() * sizeof(KoyLeaf), cudaMemcpyHostToDevice);
}

void KoySeagrass::toRibbons(KRibbonsCPU& r, unsigned int material) const {
    r = KRibbonsCPU();
    r.material = material;
    const int ppl = pointsPerLeaf;
    r.points.resize(leaves.size() * ppl);
    r.normals.resize(leaves.size() * ppl);
    for (size_t i = 0; i < leaves.size(); ++i) {
        const KoyLeaf& L = leaves[i];
        for (int k = 0; k < ppl; ++k) {
            const float s = L.length * (float(k) - 0.5f) / float(ppl - 2);
            r.points[i * ppl + k] = make_float4(L.base.x, L.base.y + s, L.base.z, L.halfWidth);
            r.normals[i * ppl + k] = make_float3(std::cos(L.yaw), 0.0f, std::sin(L.yaw));
        }
        for (int s = 0; s < ppl - 2; ++s) r.segments.push_back(unsigned(i * ppl + s));
    }
}

void KoySeagrass::update(float t, float4* pts, float3* nrm, cudaStream_t s) const {
    koyGrassPoints(dLeaves, int(leaves.size()), pointsPerLeaf, t, current, pts, nrm, s);
}

void KoySeagrass::release() {
    if (dLeaves) cudaFree(dLeaves);
    dLeaves = nullptr;
}

// ---------------------------------------------------------------------------------------------------
namespace {
KMeshCPU buildFloor(const Config& cfg) {
    KMeshCPU m;
    m.name = "floor";
    m.material = 0;
    m.id = 1;
    const std::vector<float> ax = gridAxis(cfg.f("floor.fine_reach", 12.0f), cfg.f("floor.fine_step", 0.03f), cfg.f("floor.extent", 110.0f), 1.5f);
    const int n = int(ax.size());
    m.pos.resize(size_t(n) * n);
    m.nrm.resize(size_t(n) * n);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float x = ax[i], z = ax[j];
            const float e = 0.004f;
            const float y = koyFloor(cfg, x, z);
            const float gx = (koyFloor(cfg, x + e, z) - koyFloor(cfg, x - e, z)) / (2 * e);
            const float gz = (koyFloor(cfg, x, z + e) - koyFloor(cfg, x, z - e)) / (2 * e);
            m.pos[size_t(j) * n + i] = make_float3(x, y, z);
            m.nrm[size_t(j) * n + i] = normalize(make_float3(-gx, 1.0f, -gz));
        }
    m.idx.reserve(size_t(n - 1) * (n - 1) * 2);
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            const unsigned a = unsigned(j * n + i), b = a + 1, c = a + unsigned(n), d = c + 1;
            m.idx.push_back(make_uint3(a, c, b));
            m.idx.push_back(make_uint3(b, c, d));
        }
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
    // textures (CC0 scans fetched by assets/koy/fetch.sh; without them plain albedos)
    const std::string tdir = root + "/assets/koy/cc0";
    auto load = [&](const char* name) {
        KoyTextureSet t;
        if (cfg.f("tex.use", 1.0f) < 0.5f || !koyLoadTextureSet(tdir, name, t)) return -1;
        textures.push_back(t);
        return int(textures.size()) - 1;
    };
    setSand = load("kum");
    setRock = load("kaya_model");
    if (cfg.i("tex.high", 1)) {   // the high-resolution set (the panel can switch between them live)
        setSand4k = load("kum_4k");
        setRock8k = load("kaya_model_8k");
        setRockDetail = load("kaya_ayrinti");
    }
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
    KMaterial leafM{};
    leafM.kind = KM_LEAF;
    leafM.texSet = -1;
    leafM.albedo = cfg.v3("grass.albedo", make_float3(0.09f, 0.20f, 0.06f));
    leafM.aoStrength = 0.0f;
    floorM.detailSet = rockM.detailSet = leafM.detailSet = -1;
    materials = {floorM, rockM, leafM};

    meshes.push_back(buildFloor(cfg));
    buildRipples(cfg);
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
    grass.build(cfg);
    grass.toRibbons(ribbons, 2);
    applyTextureMode(cfg, cfg.i("tex.mode", 1));
}

void KoyWorld::applyTextureMode(const Config& cfg, int mode) {
    texMode = mode;
    KMaterial& f = materials[0];
    KMaterial& r = materials[1];
    const bool high = mode == 1 && setSand4k >= 0 && setRock8k >= 0;
    if (f.texSet >= 0) f.texSet = high ? setSand4k : setSand;
    if (r.texSet >= 0) r.texSet = high ? setRock8k : setRock;
    // the detail layers: the sand's own grain at a quarter of its tile; the rock a weathered limestone scan, finer
    f.detailSet = high && f.texSet >= 0 ? setSand4k : -1;
    f.detailTile = cfg.f("tex.sand_detail_tile", 0.9f);
    f.detailNormal = cfg.f("tex.sand_detail_normal", 0.5f);
    f.detailAlbedo = cfg.f("tex.sand_detail_albedo", 0.25f);
    r.detailSet = high && r.texSet >= 0 ? setRockDetail : -1;
    r.detailTile = cfg.f("tex.rock_detail_tile", 0.9f);
    r.detailNormal = cfg.f("tex.rock_detail_normal", 0.6f);
    r.detailAlbedo = cfg.f("tex.rock_detail_albedo", 0.35f);
}

namespace {
bool makeMipTexture(std::vector<unsigned char> px, int w, int h, bool srgb, KoyTextureSet& out, unsigned long long& tex);
}

void KoyWorld::buildRipples(const Config& cfg) {
    const int N = cfg.i("floor.ripple_texels", 2048);
    const float half = cfg.f("floor.ripple_half", 16.0f);
    const float reach = cfg.f("floor.ripple_reach", 13.0f);
    const float texel = 2.0f * half / N;
    std::vector<float> h(size_t(N) * N);
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const float x = -half + (i + 0.5f) * texel, z = -half + (j + 0.5f) * texel;
            const float r = std::max(std::fabs(x), std::fabs(z));
            const float fadeIn = 1.0f - smooth01(reach, std::min(reach + 3.0f, half - 0.2f), r);
            h[size_t(j) * N + i] = fadeIn * koyRipple(cfg, x, z);
        }
    std::vector<unsigned char> px(size_t(N) * N * 4);
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            auto H = [&](int a, int b) { return h[size_t(std::min(std::max(b, 0), N - 1)) * N + std::min(std::max(a, 0), N - 1)]; };
            const float gx = (H(i + 1, j) - H(i - 1, j)) / (2.0f * texel), gz = (H(i, j + 1) - H(i, j - 1)) / (2.0f * texel);
            const float3 n = normalize(make_float3(-gx, 1.0f, -gz));
            unsigned char* p = &px[(size_t(j) * N + i) * 4];
            p[0] = (unsigned char)std::lround((n.x * 0.5f + 0.5f) * 255.0f);
            p[1] = (unsigned char)std::lround((n.z * 0.5f + 0.5f) * 255.0f);
            p[2] = (unsigned char)std::lround((n.y * 0.5f + 0.5f) * 255.0f);
            p[3] = 255;
        }
    ripples = KoyTextureSet();
    ripples.name = "dalgacik";
    unsigned long long t = 0;
    if (makeMipTexture(px, N, N, false, ripples, t)) {
        ripples.set.normal = t;
        ripples.set.hasNormal = 1;
        ripples.set.texels = float(N);
    }
    rippleHalf = half;
}

std::vector<KTextureSet> KoyWorld::textureSets() const {
    std::vector<KTextureSet> v;
    for (const auto& t : textures) v.push_back(t.set);
    return v;
}

void KoyWorld::release() {
    grass.release();
    for (auto o : ripples.textures) cudaDestroyTextureObject(cudaTextureObject_t(o));
    for (void* a : ripples.arrays) cudaFreeMipmappedArray(static_cast<cudaMipmappedArray_t>(a));
    ripples = KoyTextureSet();
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
    s << meshes.size() << " ag (" << tris << " ucgen), " << grass.leaves.size() << " yaprak (" << ribbons.segments.size() << " serit parcasi), "
      << textures.size() << " taranmis malzeme";
    return s.str();
}

// ---------------------------------------------------------------------------------------------------
namespace {
float srgbToLin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float linToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

// a mipmapped RGBA8 texture from pixels; sRGB: averaged in linear light, read back decoded by the hardware
bool makeMipTexture(std::vector<unsigned char> px, int w, int h, bool srgb, KoyTextureSet& out, unsigned long long& tex) {
    int levels = 1;
    while ((w >> levels) > 0 || (h >> levels) > 0) ++levels;
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<uchar4>();
    cudaMipmappedArray_t arr = nullptr;
    if (cudaMallocMipmappedArray(&arr, &fd, make_cudaExtent(w, h, 0), levels) != cudaSuccess) return false;
    std::vector<float> lut(256);
    for (int i = 0; i < 256; ++i) lut[i] = srgb ? srgbToLin(i / 255.0f) : i / 255.0f;
    std::vector<unsigned char> inv(4096);   // linear 0..1 in 4096 steps -> 8-bit encoded
    for (int i = 0; i < 4096; ++i) {
        const float v = i / 4095.0f;
        inv[i] = (unsigned char)std::min(255.0f, std::max(0.0f, (srgb ? linToSrgb(v) : v) * 255.0f + 0.5f));
    }
    int cw = w, ch = h;
    for (int l = 0; l < levels; ++l) {
        cudaArray_t a = nullptr;
        cudaGetMipmappedArrayLevel(&a, arr, l);
        cudaMemcpy2DToArray(a, 0, 0, px.data(), size_t(cw) * 4, size_t(cw) * 4, ch, cudaMemcpyHostToDevice);
        if (l + 1 == levels) break;
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
        px.swap(nx);
        cw = nw;
        ch = nh;
    }
    // the last level is one texel: the average (in linear light for sRGB)
    if (srgb) out.set.meanAlbedo = make_float3(lut[px[0]], lut[px[1]], lut[px[2]]);
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeMipmappedArray;
    rd.res.mipmap.mipmap = arr;
    cudaTextureDesc td{};
    td.addressMode[0] = td.addressMode[1] = cudaAddressModeWrap;
    td.filterMode = cudaFilterModeLinear;
    td.mipmapFilterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeNormalizedFloat;
    td.normalizedCoords = 1;
    td.sRGB = srgb ? 1 : 0;
    td.maxAnisotropy = 16;
    td.maxMipmapLevelClamp = float(levels - 1);
    cudaTextureObject_t t = 0;
    if (cudaCreateTextureObject(&t, &rd, &td, nullptr) != cudaSuccess) return false;
    out.arrays.push_back(arr);
    out.textures.push_back(t);
    tex = t;
    return true;
}
bool loadImage(const std::string& base, std::vector<unsigned char>& px, int& w, int& h) {
    for (const char* ext : {".jpg", ".png", ".jpeg"}) {
        const std::string p = base + ext;
        if (!std::filesystem::exists(p)) continue;
        int n = 0;
        unsigned char* d = stbi_load(p.c_str(), &w, &h, &n, 4);
        if (!d) return false;
        px.assign(d, d + size_t(w) * h * 4);
        stbi_image_free(d);
        return true;
    }
    return false;
}
}   // namespace

bool koyLoadTextureSet(const std::string& dir, const std::string& name, KoyTextureSet& out) {
    const std::string base = dir + "/" + name + "/";
    std::vector<unsigned char> px;
    int w = 0, h = 0;
    if (!loadImage(base + "albedo", px, w, h)) return false;
    out.name = name;
    out.set = {};
    out.set.texels = float(std::max(w, h));
    if (!makeMipTexture(px, w, h, true, out, out.set.albedo)) return false;
    if (loadImage(base + "normal", px, w, h) && makeMipTexture(px, w, h, false, out, out.set.normal)) out.set.hasNormal = 1;
    if (loadImage(base + "rough", px, w, h)) makeMipTexture(px, w, h, false, out, out.set.rough);
    std::printf("koy: malzeme %s %dx%d%s\n", name.c_str(), w, h, out.set.hasNormal ? " (normal haritali)" : "");
    return true;
}
