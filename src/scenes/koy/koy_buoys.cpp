// Scene 5 "Koy", Ada: the buoy lines, the mooring buoy and its rope, the mussels (see koy_buoys.h).
#include "scenes/koy/koy_buoys.h"

#include "scenes/koy/koy_island.h"
#include "scenes/koy/koy_noise.h"
#include "scenes/koy/koy_world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace {
constexpr float kPi = 3.14159265f;

KMeshCPU unitSphere(const char* name, unsigned int material, unsigned int id, int around, int rings) {
    KMeshCPU m;
    m.name = name;
    m.material = material;
    m.id = id;
    m.bakeAO = false;   // it moves
    for (int j = 0; j <= rings; ++j)
        for (int i = 0; i <= around; ++i) {
            const float th = kPi * float(j) / rings, ph = 2.0f * kPi * float(i) / around;
            const float3 p = make_float3(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
            m.pos.push_back(p);
            m.nrm.push_back(p);
        }
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < around; ++i) {
            const unsigned a = unsigned(j * (around + 1) + i), b = a + 1, c = a + unsigned(around + 1), d = c + 1;
            m.idx.push_back(make_uint3(a, b, d));
            m.idx.push_back(make_uint3(a, d, c));
        }
    return m;
}
// a tube of radius 1 along +y from 0 to 1 (a rope's segment, scaled per instance)
KMeshCPU unitTube(const char* name, unsigned int material, unsigned int id, int around) {
    KMeshCPU m;
    m.name = name;
    m.material = material;
    m.id = id;
    m.bakeAO = false;
    for (int j = 0; j <= 1; ++j)
        for (int i = 0; i <= around; ++i) {
            const float ph = 2.0f * kPi * float(i) / around;
            m.pos.push_back(make_float3(std::cos(ph), float(j), std::sin(ph)));
            m.nrm.push_back(make_float3(std::cos(ph), 0.0f, std::sin(ph)));
        }
    for (int i = 0; i < around; ++i) {
        const unsigned a = unsigned(i), b = a + 1, c = a + unsigned(around + 1), d = c + 1;
        m.idx.push_back(make_uint3(a, d, b));
        m.idx.push_back(make_uint3(a, c, d));
    }
    return m;
}
void addBlock(KMeshCPU& m, float3 c, float3 h, float yaw) {
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float3 ax = make_float3(cy, 0.0f, -sy), ay = make_float3(0, 1, 0), az = make_float3(sy, 0.0f, cy);
    const float3 A[3] = {ax, ay, az};
    const float H[3] = {h.x, h.y, h.z};
    for (int f = 0; f < 6; ++f) {
        const int n = f / 2, u = (n + 1) % 3, v = (n + 2) % 3;
        const float s = (f & 1) ? 1.0f : -1.0f;
        const float3 N = A[n] * s;
        const unsigned base = unsigned(m.pos.size());
        const int seg = 3;
        for (int j = 0; j <= seg; ++j)
            for (int i = 0; i <= seg; ++i) {
                const float su = -H[u] + 2.0f * H[u] * i / seg, sv = -H[v] + 2.0f * H[v] * j / seg;
                const float3 p = c + N * H[n] + A[u] * su + A[v] * sv;
                m.pos.push_back(p);
                m.nrm.push_back(N);
                m.uv.push_back(make_float2((su + 7.0f * c.x) / 2.7f, (sv + 3.0f * c.z) / 2.7f));
            }
        for (int j = 0; j < seg; ++j)
            for (int i = 0; i < seg; ++i) {
                const unsigned a = base + unsigned(j * (seg + 1) + i), b = a + 1, cc = a + unsigned(seg + 1), d = cc + 1;
                if (dot(cross(A[u], A[v]), N) > 0.0f) {
                    m.idx.push_back(make_uint3(a, b, d));
                    m.idx.push_back(make_uint3(a, d, cc));
                } else {
                    m.idx.push_back(make_uint3(a, d, b));
                    m.idx.push_back(make_uint3(a, cc, d));
                }
            }
    }
}
// A mussel (Mytilus galloprovincialis): a pointed, slightly bent shell of `len` metres, its hinge (the pointed end) at
// `at`, lying along `axis`, its flat side (where the two valves meet) facing `side`
void addMussel(KMeshCPU& m, float3 at, float3 axis, float3 side, float len, unsigned seed) {
    const int around = 10, along = 7;
    axis = normalize(axis);
    side = normalize(side - axis * dot(side, axis));
    const float3 up = cross(axis, side);
    const float wid = len * (0.42f + 0.06f * kn::hash01(int(seed), 1, 3)), thick = len * 0.36f;
    const float bend = 0.12f * len;   // the shell curves a little towards its ventral side
    const unsigned base = unsigned(m.pos.size());
    for (int j = 0; j <= along; ++j) {
        const float u = float(j) / along;
        const float prof = std::pow(std::sin(kPi * std::pow(u, 0.75f)), 0.8f);   // pointed at the hinge, round at the end
        const float3 c = at + axis * (u * len) + side * (bend * std::sin(kPi * u));
        for (int i = 0; i <= around; ++i) {
            const float ph = 2.0f * kPi * float(i) / around;
            const float cx = std::cos(ph), cy = std::sin(ph);
            const float3 off = side * (cx * 0.5f * wid * prof) + up * (cy * 0.5f * thick * prof);
            m.pos.push_back(c + off);
            // the normal of the ellipse's section (good enough at a few cm)
            m.nrm.push_back(normalize(side * (cx / std::max(wid, 1e-4f)) + up * (cy / std::max(thick, 1e-4f)) + axis * ((u - 0.5f) * 0.6f)));
        }
    }
    for (int j = 0; j < along; ++j)
        for (int i = 0; i < around; ++i) {
            const unsigned a = base + unsigned(j * (around + 1) + i), b = a + 1, c = a + unsigned(around + 1), d = c + 1;
            m.idx.push_back(make_uint3(a, b, d));
            m.idx.push_back(make_uint3(a, d, c));
        }
}
}   // namespace

void koyAddMussel(KMeshCPU& m, float3 at, float3 axis, float3 side, float len, unsigned seed) { addMussel(m, at, axis, side, len, seed); }

void KoyBuoys::build(const Config& cfg, const std::function<float(float, float)>& floorY, std::vector<KMeshCPU>& meshes, std::vector<KInstanceCPU>& instances,
                     unsigned int matFloat, unsigned int matBig, unsigned int matRope, unsigned int matBlock, unsigned int matMussel, unsigned int id,
                     std::string* log) {
    floaters.clear();
    ropes.clear();
    std::mt19937 rng(77u);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const int meshFloat = int(meshes.size());
    meshes.push_back(unitSphere("duba", matFloat, id, 20, 12));
    const int meshBig = int(meshes.size());
    meshes.push_back(unitSphere("samandira", matBig, id, 36, 20));
    const int meshTube = int(meshes.size());
    meshes.push_back(unitTube("ip", matRope, id, 8));
    KMeshCPU blocks, rope, mussels;
    blocks.name = "bloklar";
    blocks.material = matBlock;
    blocks.id = id;
    rope.name = "samandira halati";
    rope.material = matRope;
    rope.id = id;
    mussels.name = "midyeler";
    mussels.material = matMussel;
    mussels.id = id;
    auto addInstance = [&](int mesh) {
        KInstanceCPU in;
        in.mesh = mesh;
        instances.push_back(in);
        return int(instances.size()) - 1;
    };
    // the lines: floats every `step` metres on both sides and across the far end; a down-line every `anchorEvery`-th
    const float half = cfg.f("buoy.half", 15.0f), z0 = cfg.f("buoy.z0", 24.0f), z1 = cfg.f("buoy.z1", -160.0f), step = cfg.f("buoy.step", 2.5f);
    const float fr = cfg.f("buoy.radius", 0.15f);
    const int anchorEvery = cfg.i("buoy.anchor_every", 10);
    const float3 sm = cfg.v3("samandira.pos", make_float3(0.0f, 0.0f, -160.0f));
    auto line = [&](float xa, float za, float xb, float zb, bool skipMiddle) {
        const float L = std::sqrt((xb - xa) * (xb - xa) + (zb - za) * (zb - za));
        const int n = std::max(1, int(std::round(L / step)));
        int prev = -1;
        for (int k = 0; k <= n; ++k) {
            const float t = float(k) / n;
            const float x = xa + (xb - xa) * t, z = za + (zb - za) * t;
            if (skipMiddle && std::fabs(x - sm.x) < 0.6f * step && std::fabs(z - sm.z) < 0.6f * step) { prev = -2; continue; }   // the big buoy's place
            Floater f{x, z, fr * (0.95f + 0.1f * U(rng)), 0.25f, addInstance(meshFloat)};
            floaters.push_back(f);
            const int me = int(floaters.size()) - 1;
            if (prev >= 0) ropes.push_back({prev, me, make_float3(0, 0, 0), 0.006f, addInstance(meshTube)});
            if (prev == -2 && me > 0) {}   // across the big buoy: the rope runs through its ring (left out)
            prev = me;
            if (k % anchorEvery == 0) {   // a down-line to a block on the sand
                const float g = floorY(x, z);
                addBlock(blocks, make_float3(x, g + 0.12f, z), make_float3(0.25f, 0.18f, 0.25f), U(rng) * kPi);
                ropes.push_back({me, -1, make_float3(x, g + 0.3f, z), 0.007f, addInstance(meshTube)});
            }
        }
    };
    line(-half, z0, -half, z1, false);
    line(half, z0, half, z1, false);
    line(-half, z1, half, z1, true);
    // the big mooring buoy and its rope down to a block on the sand
    {
        Floater f{sm.x, sm.z, cfg.f("samandira.radius", 0.55f), 0.15f, addInstance(meshBig)};
        floaters.push_back(f);
        const float g = floorY(sm.x, sm.z);
        addBlock(blocks, make_float3(sm.x + 0.4f, g + 0.3f, sm.z - 0.3f), make_float3(0.6f, 0.4f, 0.6f), 0.4f);
        // the rope: a 3-strand lay, a slight bow (a current), from just under the buoy to the block's ring
        const float3 top = make_float3(sm.x, -0.75f, sm.z), bot = make_float3(sm.x + 0.4f, g + 0.75f, sm.z - 0.3f);
        const float R = cfg.f("samandira.rope_r", 0.022f);
        const int around = 16;
        const float L = length(bot - top);
        const int rings = int(L / 0.03f);
        const float3 drift = normalize(make_float3(1.0f, 0.0f, 0.6f));
        auto centre = [&](float u) { return top + (bot - top) * u + drift * (0.9f * std::sin(kPi * u)); };
        for (int j = 0; j <= rings; ++j) {
            const float u = float(j) / rings;
            const float3 c = centre(u), c2 = centre(std::min(u + 0.001f, 1.0f)), c1 = centre(std::max(u - 0.001f, 0.0f));
            const float3 w = normalize(c2 - c1);
            const float3 e1 = normalize(cross(make_float3(0, 0, 1), w)), e2 = cross(w, e1);
            const float s = u * L;
            for (int i = 0; i <= around; ++i) {
                const float ph = 2.0f * kPi * float(i) / around;
                const float rr = R * (1.0f + 0.14f * std::cos(3.0f * ph - s * 2.0f * kPi / 0.11f));
                const float3 d = e1 * std::cos(ph) + e2 * std::sin(ph);
                rope.pos.push_back(c + d * rr);
                rope.nrm.push_back(d);
            }
        }
        for (int j = 0; j < rings; ++j)
            for (int i = 0; i < around; ++i) {
                const unsigned a = unsigned(j * (around + 1) + i), b = a + 1, c = a + unsigned(around + 1), d = c + 1;
                rope.idx.push_back(make_uint3(a, d, b));
                rope.idx.push_back(make_uint3(a, c, d));
            }
        // mussels: clumps all round the rope, thick in the upper metres (food and light), thinning to nothing by ~20 m;
        // in the top two metres the clumps run together into one sleeve
        int nm = 0;
        for (float s = 0.05f; s < std::min(L, 22.0f); s += 0.035f) {
            const float depth = 0.75f + s;
            const float dens = depth < 4.0f ? 1.0f : depth < 12.0f ? 0.95f - 0.06f * (depth - 4.0f) : std::max(0.0f, 0.45f - 0.05f * (depth - 12.0f));
            if (U(rng) > dens) continue;
            const float u = s / L;
            const float3 c = centre(u), w = normalize(centre(std::min(u + 0.002f, 1.0f)) - c);
            const float3 e1 = normalize(cross(make_float3(0, 0, 1), w)), e2 = cross(w, e1);
            const int k = depth < 4.0f ? 7 : 2 + int(U(rng) * 3.0f);
            for (int q = 0; q < k; ++q) {
                const float ph = U(rng) * 2.0f * kPi;
                const float3 out = e1 * std::cos(ph) + e2 * std::sin(ph);
                const float layer = depth < 4.0f ? U(rng) * 0.07f : U(rng) * 0.02f;   // mussels on mussels
                const float len = 0.035f + 0.04f * U(rng);
                // hinge on the rope (or the clump), the shell pointing out and down (they hang)
                const float3 axis = normalize(out * (0.7f + 0.3f * U(rng)) - w * (0.4f + 0.5f * U(rng)) + make_float3(0, -0.2f, 0));
                addMussel(mussels, c + out * (R * 0.8f + layer), axis, cross(axis, w) + out * 0.3f, len, unsigned(nm * 31 + q));
                ++nm;
            }
        }
        if (log) {
            char b[160];
            std::snprintf(b, sizeof(b), "samandira halati %.1f m, %d midye; ", L, nm);
            *log += b;
        }
    }
    // mussels on the piles: a band from just above the mean water down to ~1.4 m, on the crust
    {
        const KoyPierSpec P = koyPierSpec(cfg);
        int nm = 0;
        for (const KoyPierSection& S : P.sections)
            for (float pz : S.pileZ)
                for (float px : S.pileX) {
                    if (floorY(px, pz) > -0.9f) continue;   // on the beach
                    const int count = 160 + int(U(rng) * 120.0f);
                    for (int q = 0; q < count; ++q) {
                        const float y = 0.12f - 1.5f * std::pow(U(rng), 1.6f);
                        const float ph = U(rng) * 2.0f * kPi;
                        const float3 out = make_float3(std::cos(ph), 0.0f, std::sin(ph));
                        const float rr = P.pileR + 0.045f + 0.03f * U(rng);
                        const float len = 0.035f + 0.045f * U(rng);
                        const float3 axis = normalize(out * (0.4f + 0.5f * U(rng)) + make_float3((U(rng) - 0.5f) * 0.6f, -0.6f - 0.4f * U(rng), (U(rng) - 0.5f) * 0.6f));
                        addMussel(mussels, make_float3(px + out.x * rr, y, pz + out.z * rr), axis, out + make_float3(0, 0.3f, 0), len, unsigned(nm * 17 + 5));
                        ++nm;
                    }
                }
        if (log) {
            char b[96];
            std::snprintf(b, sizeof(b), "ayaklarda %d midye; ", nm);
            *log += b;
        }
    }
    for (KMeshCPU* m : {&blocks, &rope, &mussels})
        if (!m->idx.empty()) {
            meshes.push_back(std::move(*m));
            addInstance(int(meshes.size()) - 1);
        }
    if (log) {
        char b[96];
        std::snprintf(b, sizeof(b), "%zu duba, %zu ip parcasi", floaters.size(), ropes.size());
        *log += b;
    }
}

std::vector<float2> KoyBuoys::samplePoints() const {
    std::vector<float2> p;
    for (const Floater& f : floaters) p.push_back(make_float2(f.x, f.z));
    return p;
}

void KoyBuoys::transforms(const std::vector<float>& heights, std::vector<int>& idx, std::vector<std::array<float, 12>>& m) const {
    idx.clear();
    m.clear();
    std::vector<float3> centre(floaters.size());
    for (size_t i = 0; i < floaters.size(); ++i) {
        const Floater& f = floaters[i];
        const float h = i < heights.size() ? heights[i] : 0.0f;
        centre[i] = make_float3(f.x, h + f.lift * f.r, f.z);
        idx.push_back(f.instance);
        m.push_back({f.r, 0, 0, f.x, 0, f.r, 0, centre[i].y, 0, 0, f.r, f.z});
    }
    for (const Rope& r : ropes) {
        const float3 a = centre[size_t(r.a)] - make_float3(0.0f, r.b < 0 ? floaters[size_t(r.a)].r : 0.0f, 0.0f);
        const float3 b = r.b >= 0 ? centre[size_t(r.b)] : r.fixedEnd;
        const float3 axis = b - a;
        const float3 w = normalize(axis);
        const float3 e1 = normalize(cross(std::fabs(w.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0), w)), e2 = cross(w, e1);
        idx.push_back(r.instance);
        m.push_back({e1.x * r.r, axis.x, e2.x * r.r, a.x, e1.y * r.r, axis.y, e2.y * r.r, a.y, e1.z * r.r, axis.z, e2.z * r.r, a.z});
    }
}
