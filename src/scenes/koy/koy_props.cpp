// Scene 5 "Koy", Ada: the rocks among the amphorae and the shallows' small things (see koy_props.h).
#include "scenes/koy/koy_props.h"

#include "scenes/koy/koy_noise.h"
#include "scenes/koy/koy_sdf.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>

namespace {
constexpr float kPi = 3.14159265f;

// an angular block: nine faces about a flat bed, at random azimuths, tilts and distances
void makeBlock(KoyRockField::Rock& R, std::mt19937& rng) {
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    R.n[0] = make_float3(0.0f, -1.0f, 0.0f);
    R.d[0] = 0.4f * R.r;
    for (int f = 1; f < 10; ++f) {
        const float az = (float(f) + 0.6f * U(rng)) / 9.0f * 2.0f * kPi + R.seed, el = (U(rng) - 0.3f) * 1.2f;
        R.n[f] = normalize(make_float3(std::cos(az) * std::cos(el), std::sin(el) + (f == 9 ? 1.4f : 0.0f), std::sin(az) * std::cos(el)));
        R.d[f] = R.r * (0.6f + 0.5f * U(rng)) * (R.n[f].y > 0.5f ? 0.75f : 1.0f);
    }
}

// a small mesh's helpers
struct Frame3 {
    float3 x, y, z, o;
    float3 at(float3 p) const { return o + x * p.x + y * p.y + z * p.z; }
    float3 dir(float3 v) const { return x * v.x + y * v.y + z * v.z; }
};
Frame3 frameOn(float3 o, float3 up, float yaw) {
    up = normalize(up);
    const float3 h = std::fabs(up.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0);
    float3 x = normalize(cross(h, up));
    float3 z = cross(x, up);
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float3 x2 = x * c + z * s, z2 = z * c - x * s;
    return {x2, up, z2, o};
}
// an icosphere of radius 1, subdivided `levels` times (shared by the pebbles and the urchins' bodies)
void icosphere(int levels, std::vector<float3>& v, std::vector<uint3>& f) {
    v = {{-1, 1.618f, 0}, {1, 1.618f, 0}, {-1, -1.618f, 0}, {1, -1.618f, 0}, {0, -1, 1.618f}, {0, 1, 1.618f},
         {0, -1, -1.618f}, {0, 1, -1.618f}, {1.618f, 0, -1}, {1.618f, 0, 1}, {-1.618f, 0, -1}, {-1.618f, 0, 1}};
    for (auto& p : v) p = normalize(p);
    f = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
         {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for (int lv = 0; lv < levels; ++lv) {
        std::map<unsigned long long, unsigned> mid;
        auto midpoint = [&](unsigned a, unsigned b) {
            const unsigned long long key = (unsigned long long)std::min(a, b) << 32 | std::max(a, b);
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            v.push_back(normalize(v[a] + v[b]));
            return mid[key] = unsigned(v.size() - 1);
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
}
// append a deformed copy of (v, f) with smooth normals recomputed
void appendShape(KMeshCPU& m, const std::vector<float3>& pts, const std::vector<uint3>& f) {
    const unsigned base = unsigned(m.pos.size());
    std::vector<float3> nrm(pts.size(), make_float3(0, 0, 0));
    for (const uint3& t : f) {
        const float3 n = cross(pts[t.y] - pts[t.x], pts[t.z] - pts[t.x]);
        nrm[t.x] += n;
        nrm[t.y] += n;
        nrm[t.z] += n;
    }
    for (size_t i = 0; i < pts.size(); ++i) {
        m.pos.push_back(pts[i]);
        const float L = length(nrm[i]);
        m.nrm.push_back(L > 1e-12f ? nrm[i] / L : make_float3(0, 1, 0));
    }
    for (const uint3& t : f) m.idx.push_back(make_uint3(base + t.x, base + t.y, base + t.z));
}
// a tube along a centre line (radius per point, flattened by `flat` along the frame's y), closed at both ends
void appendTube(KMeshCPU& m, const std::vector<float3>& c, const std::vector<float>& r, float3 up, float flat, int around,
                const std::function<float(float, float)>& bump) {
    std::vector<float3> pts;
    std::vector<uint3> f;
    const int n = int(c.size());
    for (int j = 0; j < n; ++j) {
        const float3 w = normalize(c[std::min(j + 1, n - 1)] - c[std::max(j - 1, 0)]);
        float3 e1 = normalize(cross(up, w));
        if (length(cross(up, w)) < 1e-4f) e1 = make_float3(1, 0, 0);
        const float3 e2 = cross(w, e1);   // ~ up
        for (int i = 0; i < around; ++i) {
            const float ph = 2.0f * kPi * float(i) / around;
            const float rr = r[size_t(j)] * (1.0f + bump(float(j) / (n - 1), ph));
            pts.push_back(c[size_t(j)] + e1 * (std::cos(ph) * rr) + e2 * (std::sin(ph) * rr * flat));
        }
    }
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i < around; ++i) {
            const unsigned a = unsigned(j * around + i), b = unsigned(j * around + (i + 1) % around), cc = a + unsigned(around), d = b + unsigned(around);
            f.push_back(make_uint3(a, d, b));
            f.push_back(make_uint3(a, cc, d));
        }
    for (int end = 0; end < 2; ++end) {   // caps: a fan to the end point
        const unsigned ci = unsigned(pts.size());
        pts.push_back(c[end ? size_t(n - 1) : 0]);
        const unsigned ring = end ? unsigned((n - 1) * around) : 0u;
        for (int i = 0; i < around; ++i) {
            const unsigned a = ring + unsigned(i), b = ring + unsigned((i + 1) % around);
            if (end) f.push_back(make_uint3(ci, a, b));
            else f.push_back(make_uint3(ci, b, a));
        }
    }
    appendShape(m, pts, f);
}
}   // namespace

float KoyRockField::sdf(float3 p) const {
    float d = 1e9f;
    for (const Rock& R : rocks) {
        const float3 q = p - R.c;
        const float far = length(q) - 1.6f * R.r - 0.5f;
        if (far > 0.0f) { d = std::min(d, far); continue; }
        float db = -1e9f;
        for (int k = 0; k < 10; ++k) db = std::max(db, dot(R.n[k], q) - R.d[k]);
        if (db < 0.6f) {   // its relief: lumps, eroded creases, joints opened into crevices
            db += 0.10f * R.r * kn::fbm3(q.x * 2.0f / R.r + R.seed * 7.0f, q.y * 2.0f / R.r, q.z * 2.0f / R.r, 4);
            db -= 0.05f * R.r * (kn::ridged3(q.x * 1.5f / R.r, q.y * 1.5f / R.r + R.seed * 3.0f, q.z * 1.5f / R.r, 3) - 0.5f);
            const kn::Cell cc = kn::cellular3(q.x / (0.8f * R.r) + R.seed * 2.0f, q.y / (0.6f * R.r), q.z / (0.8f * R.r), 9u);
            db += 0.04f * R.r * (1.0f - kn::smoothstep(0.0f, 0.05f, cc.f2 - cc.f1));
            db += 0.012f * kn::fbm3(q.x * 14.0f, q.y * 14.0f, q.z * 14.0f + R.seed, 3);   // the fine grain (pitted limestone)
        }
        db = std::max(db, (length(q) - 1.3f * R.r) * 0.8f);   // worn corners
        d = std::min(d, db);
    }
    return d;
}

KoyRockField koyAmphoraRocks(const Config& cfg, const std::function<float(float, float)>& floorY) {
    KoyRockField f;
    const float3 c = cfg.v3("amfora.centre", make_float3(0.0f, 0.0f, -52.0f));
    std::mt19937 rng(unsigned(cfg.i("amfora_kaya.seed", 12)));
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    // the crescent: big blocks on the down-slope side (-z), the cargo against their up-slope faces
    const int big = cfg.i("amfora_kaya.big", 4), small = cfg.i("amfora_kaya.small", 12);
    for (int k = 0; k < big + small; ++k) {
        KoyRockField::Rock R;
        float x, z;
        if (k < big) {
            const float a = -kPi * 0.5f + (float(k) - 0.5f * (big - 1)) * 0.55f + (U(rng) - 0.5f) * 0.2f;   // about -z
            const float rad = 5.2f + U(rng) * 1.2f;
            x = c.x + std::cos(a) * rad;
            z = c.z + std::sin(a) * rad;
            R.r = 1.5f + 1.0f * U(rng);
        } else {
            const float a = U(rng) * 2.0f * kPi, rad = 4.0f + 7.0f * U(rng);
            x = c.x + std::cos(a) * rad * 1.2f;
            z = c.z + std::sin(a) * rad;
            R.r = 0.45f + 0.8f * U(rng) * U(rng);
        }
        R.seed = U(rng) * 10.0f;
        R.c = make_float3(x, floorY(x, z) + 0.2f * R.r, z);
        makeBlock(R, rng);
        f.rocks.push_back(R);
    }
    return f;
}

KMeshCPU koyBuildRockField(const KoyRockField& f, float cell, unsigned int material, unsigned int id, std::string* log) {
    float3 lo = make_float3(1e9f, 1e9f, 1e9f), hi = make_float3(-1e9f, -1e9f, -1e9f);
    for (const auto& R : f.rocks) {
        lo = fminf3(lo, R.c - make_float3(1.5f * R.r + 0.3f, 1.5f * R.r + 0.3f, 1.5f * R.r + 0.3f));
        hi = fmaxf3(hi, R.c + make_float3(1.5f * R.r + 0.3f, 1.5f * R.r + 0.3f, 1.5f * R.r + 0.3f));
    }
    KoySdfBox box;
    box.lo = lo;
    box.hi = hi;
    box.cell = cell;
    box.slope = 1.6f;
    box.reach = 0.6f;
    return koyBuildSdfMesh([&](float3 p) { return f.sdf(p); }, box, "amfora kayalari", material, id, log);
}

void koyBuildFans(const Config& cfg, const std::vector<std::pair<float3, float3>>& sites, unsigned int matRed, unsigned int matYellow, unsigned int id,
                  std::vector<KMeshCPU>& out, std::string* log) {
    std::mt19937 rng(unsigned(cfg.i("fans.seed", 57)));
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    KMeshCPU red, yellow;
    red.name = "yelpaze mercanlari (kizil)";
    yellow.name = "yelpaze mercanlari (sari)";
    red.material = matRed;
    yellow.material = matYellow;
    red.id = yellow.id = id;
    const float3 current = normalize(cfg.v3("fans.current", make_float3(1.0f, 0.0f, 0.4f)));   // the fans open across it
    int fans = 0;
    for (size_t k = 0; k < sites.size(); ++k) {
        const auto& s = sites[k];
        const bool isRed = U(rng) < 0.7f;
        KMeshCPU& m = isRed ? red : yellow;
        // the fan's plane: it holds the growth direction (away from the face, bent up) and is broadside to the current
        const float3 up = normalize(s.second * 0.55f + make_float3(0.0f, 0.75f, 0.0f));
        float3 across = cross(current, up);
        if (length(across) < 1e-3f) across = make_float3(1, 0, 0);
        across = normalize(across);
        const float H = 0.7f + 0.6f * U(rng);
        // branches: a stack of (start, direction in the plane as an angle from up, length, radius, generation)
        struct Br { float3 p; float a; float len, r; int gen; };
        std::vector<Br> stack = {{s.first - s.second * 0.01f, (U(rng) - 0.5f) * 0.3f, H * 0.3f, 0.014f, 0}};
        while (!stack.empty()) {
            const Br b = stack.back();
            stack.pop_back();
            // a branch: a few segments, curving gently, tapering
            std::vector<float3> c;
            std::vector<float> r;
            float a = b.a;
            float3 p = b.p;
            const int nseg = 4;
            for (int j = 0; j <= nseg; ++j) {
                c.push_back(p);
                r.push_back(std::max(b.r * (1.0f - 0.35f * float(j) / nseg), 0.0012f));
                a += (U(rng) - 0.5f) * 0.18f;
                const float3 d = up * std::cos(a) + across * std::sin(a);
                p = p + d * (b.len / nseg) + current * ((U(rng) - 0.5f) * 0.004f);
            }
            appendTube(m, c, r, current, 1.0f, 5, [](float, float) { return 0.0f; });
            if (b.gen < 7) {   // forks, spreading the fan into a dense lace; the outer ones shorter
                const int kids = b.gen < 2 ? 3 : 2;
                for (int q = 0; q < kids; ++q) {
                    const float spread = (q - 0.5f * (kids - 1)) * (0.45f + 0.25f * U(rng));
                    stack.push_back({p, a + spread * (b.gen < 3 ? 1.0f : 0.6f), b.len * (0.68f + 0.12f * U(rng)), r.back() * 0.85f, b.gen + 1});
                }
            }
        }
        ++fans;
    }
    for (KMeshCPU* q : {&red, &yellow})
        if (!q->idx.empty()) out.push_back(std::move(*q));
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%d yelpaze mercani", fans);
    if (log) *log = buf;
}

void koyBuildProps(const Config& cfg, const std::function<float(float, float)>& floorY, const std::function<bool(float3)>& isRock,
                   const std::vector<const KMeshCPU*>& rockMeshes, const KoyPropMaterials& mat, unsigned int id, std::vector<KMeshCPU>& out,
                   std::string* log) {
    std::mt19937 rng(unsigned(cfg.i("props.seed", 31)));
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    std::normal_distribution<float> G(0.0f, 1.0f);
    KMeshCPU light, dark, star, cuke, pinna, urchin;
    light.name = "taslar (acik)";
    dark.name = "taslar (koyu)";
    star.name = "deniz yildizlari";
    cuke.name = "deniz hiyarlari";
    pinna.name = "yelpaze midyeleri";
    urchin.name = "deniz kestaneleri";
    light.material = mat.stoneLight;
    dark.material = mat.stoneDark;
    star.material = mat.starfish;
    cuke.material = mat.cucumber;
    pinna.material = mat.pinna;
    urchin.material = mat.urchin;
    light.id = dark.id = star.id = cuke.id = pinna.id = urchin.id = id;
    // the area: the shallows and the corridor down to the amphorae, not under the pier's head, not in a rock
    const float3 lo = cfg.v3("props.min", make_float3(-24.0f, 0.0f, -46.0f)), hi = cfg.v3("props.max", make_float3(24.0f, 0.0f, 6.0f));
    const float3 hlo = cfg.v3("props.skip_min", make_float3(-12.0f, 0.0f, -5.0f)), hhi = cfg.v3("props.skip_max", make_float3(12.0f, 0.0f, 33.0f));
    auto spot = [&](float& x, float& z) {
        for (int t = 0; t < 50; ++t) {
            x = lo.x + (hi.x - lo.x) * U(rng);
            z = lo.z + (hi.z - lo.z) * U(rng);
            if (x > hlo.x && x < hhi.x && z > hlo.z && z < hhi.z) continue;
            if (isRock(make_float3(x, floorY(x, z) + 0.05f, z))) continue;
            return true;
        }
        return false;
    };
    std::vector<float3> sph;
    std::vector<uint3> sphF;
    icosphere(2, sph, sphF);
    // pebbles: in patches (the swell sorts them), rounded, flattened, each its own lumps
    int pebbles = 0;
    const int patches = cfg.i("props.pebble_patches", 34);
    for (int p = 0; p < patches; ++p) {
        float px, pz;
        if (!spot(px, pz)) continue;
        const int n = 4 + int(U(rng) * 22.0f);
        const float spread = 0.3f + 1.2f * U(rng);
        for (int k = 0; k < n; ++k) {
            const float x = px + G(rng) * spread, z = pz + G(rng) * spread;
            if (isRock(make_float3(x, floorY(x, z) + 0.02f, z))) continue;
            const float s = 0.03f + 0.11f * std::pow(U(rng), 2.2f);
            const float3 sc = make_float3(s * (0.9f + 0.4f * U(rng)), s * (0.45f + 0.25f * U(rng)), s * (0.7f + 0.3f * U(rng)));
            const Frame3 F = frameOn(make_float3(x, floorY(x, z) + sc.y * 0.4f, z), make_float3(G(rng) * 0.1f, 1.0f, G(rng) * 0.1f), U(rng) * 2.0f * kPi);
            std::vector<float3> pts(sph.size());
            const float o = U(rng) * 50.0f;
            for (size_t i = 0; i < sph.size(); ++i) {
                const float3 d = sph[i];
                const float lump = 1.0f + 0.12f * kn::fbm3(d.x * 1.6f + o, d.y * 1.6f, d.z * 1.6f, 2);
                pts[i] = F.at(make_float3(d.x * sc.x, d.y * sc.y, d.z * sc.z) * lump);
            }
            appendShape(U(rng) < 0.6f ? light : dark, pts, sphF);
            ++pebbles;
        }
    }
    // starfish: five arms (tapered, flattened, a little curled), a domed disc, bumps
    int stars = 0;
    for (int k = 0; k < cfg.i("props.starfish", 38); ++k) {
        float x, z;
        if (!spot(x, z)) continue;
        const float R = 0.06f + 0.06f * U(rng);
        const float3 base = make_float3(x, floorY(x, z) + 0.004f, z);
        const float yaw = U(rng) * 2.0f * kPi;
        for (int a = 0; a < 5; ++a) {
            const float ang = yaw + a * 2.0f * kPi / 5.0f + (U(rng) - 0.5f) * 0.25f;
            const float3 dir = make_float3(std::cos(ang), 0.0f, std::sin(ang)), side = make_float3(-dir.z, 0.0f, dir.x);
            const float curl = (U(rng) - 0.5f) * 0.6f;
            std::vector<float3> c;
            std::vector<float> r;
            for (int j = 0; j <= 8; ++j) {
                const float u = float(j) / 8.0f;
                const float rr = R * (0.21f - 0.15f * u);
                c.push_back(base + dir * (R * 0.15f + u * R * 0.85f) + side * (curl * R * u * u) + make_float3(0.0f, rr * 0.55f + 0.012f * u * u * (U(rng) < 0.2f ? 1.0f : 0.0f), 0.0f));
                r.push_back(rr);
            }
            const float o = U(rng) * 9.0f;
            appendTube(star, c, r, make_float3(0, 1, 0), 0.6f, 10,
                       [&](float u, float ph) { return 0.12f * std::max(0.0f, kn::noise3(u * 22.0f + o, std::cos(ph) * 3.0f, std::sin(ph) * 3.0f)); });
        }
        std::vector<float3> pts(sph.size());   // the disc
        for (size_t i = 0; i < sph.size(); ++i) pts[i] = base + make_float3(sph[i].x * R * 0.24f, std::max(sph[i].y, -0.2f) * R * 0.14f + R * 0.08f, sph[i].z * R * 0.24f);
        appendShape(star, pts, sphF);
        ++stars;
    }
    // sea cucumbers: a soft, bent sausage, flat below, its back studded with papillae
    int cukes = 0;
    for (int k = 0; k < cfg.i("props.cucumbers", 46); ++k) {
        float x, z;
        if (!spot(x, z)) continue;
        const float L = 0.14f + 0.12f * U(rng), W = L * (0.18f + 0.05f * U(rng));
        const float yaw = U(rng) * 2.0f * kPi, bend = (U(rng) - 0.5f) * 0.6f;
        const float3 dir = make_float3(std::cos(yaw), 0.0f, std::sin(yaw)), side = make_float3(-dir.z, 0.0f, dir.x);
        std::vector<float3> c;
        std::vector<float> r;
        for (int j = 0; j <= 12; ++j) {
            const float u = float(j) / 12.0f, t = (u - 0.5f) * L;
            const float3 p = make_float3(x, 0.0f, z) + dir * t + side * (bend * L * (u - 0.5f) * (u - 0.5f));
            const float rr = W * std::pow(std::sin(kPi * (0.08f + 0.84f * u)), 0.5f);
            c.push_back(make_float3(p.x, floorY(p.x, p.z) + rr * 0.55f, p.z));
            r.push_back(rr);
        }
        const float o = U(rng) * 9.0f;
        appendTube(cuke, c, r, make_float3(0, 1, 0), 0.7f, 14, [&](float u, float ph) {
            const float back = std::max(0.0f, std::sin(ph));   // papillae on the back
            return 0.15f * back * kn::smoothstep(0.35f, 0.75f, kn::noise3(u * 30.0f + o, std::cos(ph) * 4.0f, std::sin(ph) * 4.0f));
        });
        ++cukes;
    }
    // fan mussel shells: a long wedge, standing on its pointed end in the sand (or fallen), its surface ribbed
    int pinnas = 0;
    for (int k = 0; k < cfg.i("props.pinna", 12); ++k) {
        float x, z;
        if (!spot(x, z)) continue;
        const bool standing = U(rng) < 0.6f;
        const float H = 0.3f + 0.25f * U(rng);
        const float3 up = standing ? normalize(make_float3(G(rng) * 0.25f, 1.0f, G(rng) * 0.25f)) : normalize(make_float3(G(rng), 0.12f, G(rng)));
        const Frame3 F = frameOn(make_float3(x, floorY(x, z) - (standing ? 0.33f * H : -0.02f), z), up, U(rng) * 2.0f * kPi);
        std::vector<float3> pts;
        std::vector<uint3> f;
        const int nu = 14, nv = 16;
        for (int j = 0; j <= nu; ++j)
            for (int i = 0; i < nv; ++i) {
                const float u = float(j) / nu, ph = 2.0f * kPi * float(i) / nv;
                const float w = 0.5f * H * 0.42f * std::pow(u, 0.7f), t = 0.5f * H * 0.14f * std::pow(u, 0.6f) * (1.0f - 0.6f * u * u);
                const float rib = 1.0f + 0.05f * std::sin(ph * 9.0f);
                pts.push_back(F.at(make_float3(std::cos(ph) * w * rib, u * H, std::sin(ph) * t)));
            }
        for (int j = 0; j < nu; ++j)
            for (int i = 0; i < nv; ++i) {
                const unsigned a = unsigned(j * nv + i), b = unsigned(j * nv + (i + 1) % nv), c = a + unsigned(nv), d = b + unsigned(nv);
                f.push_back(make_uint3(a, b, d));
                f.push_back(make_uint3(a, d, c));
            }
        const unsigned top = unsigned(pts.size());   // the open, broken top closed by a fan
        pts.push_back(F.at(make_float3(0.0f, H * 1.01f, 0.0f)));
        for (int i = 0; i < nv; ++i) f.push_back(make_uint3(top, unsigned(nu * nv + (i + 1) % nv), unsigned(nu * nv + i)));
        appendShape(pinna, pts, f);
        ++pinnas;
    }
    // sea urchins on the rock: up-facing triangles in the shallows, a body and its spines
    int urchins = 0;
    std::vector<std::pair<float3, float3>> sites;   // a rock face point and its normal
    for (const KMeshCPU* m : rockMeshes)
        for (size_t t = 0; t < m->idx.size(); t += 7) {
            const uint3 tr = m->idx[t];
            const float3 p = (m->pos[tr.x] + m->pos[tr.y] + m->pos[tr.z]) * (1.0f / 3.0f);
            if (p.y > -0.6f || p.y < -14.0f) continue;
            if (p.x < lo.x - 8.0f || p.x > hi.x + 8.0f || p.z < lo.z - 12.0f || p.z > hi.z + 30.0f) continue;
            const float3 n = normalize(m->nrm[tr.x] + m->nrm[tr.y] + m->nrm[tr.z]);
            if (n.y < 0.15f) continue;
            sites.push_back({p, n});
        }
    const int wantUrchins = cfg.i("props.urchins", 260);
    for (int k = 0; k < wantUrchins && !sites.empty(); ++k) {
        const auto& s = sites[size_t(U(rng) * float(sites.size())) % sites.size()];
        const float R = 0.018f + 0.014f * U(rng);
        const Frame3 F = frameOn(s.first + s.second * (R * 0.6f), s.second, U(rng) * 2.0f * kPi);
        std::vector<float3> pts(sph.size());
        for (size_t i = 0; i < sph.size(); ++i) pts[i] = F.at(make_float3(sph[i].x * R, sph[i].y * R * 0.75f, sph[i].z * R));
        appendShape(urchin, pts, sphF);
        // spines: thin pyramids over the upper body, longer at the equator
        std::vector<float3> sp;
        std::vector<uint3> sf;
        for (int q = 0; q < 46; ++q) {
            const float zq = 1.0f - 1.4f * (q + 0.5f) / 46.0f, rq = std::sqrt(std::max(0.0f, 1.0f - zq * zq)), ph = q * 2.39996f;
            const float3 dl = make_float3(rq * std::cos(ph), zq, rq * std::sin(ph));
            const float3 d = normalize(F.dir(dl));
            const float len = R * (1.1f + 0.7f * rq) * (0.8f + 0.4f * U(rng));
            const float3 root = F.at(make_float3(dl.x * R, dl.y * R * 0.75f, dl.z * R));
            const float3 e1 = normalize(cross(std::fabs(d.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0), d)), e2 = cross(d, e1);
            const float w = 0.0018f;
            const unsigned b = unsigned(sp.size());
            sp.push_back(root + e1 * w);
            sp.push_back(root + e2 * w);
            sp.push_back(root - e1 * w);
            sp.push_back(root - e2 * w);
            sp.push_back(root + d * len);
            for (int e = 0; e < 4; ++e) sf.push_back(make_uint3(b + unsigned(e), b + unsigned((e + 1) % 4), b + 4));
        }
        appendShape(urchin, sp, sf);
        ++urchins;
    }
    for (KMeshCPU* m : {&light, &dark, &star, &cuke, &pinna, &urchin})
        if (!m->idx.empty()) out.push_back(std::move(*m));
    char buf[200];
    std::snprintf(buf, sizeof(buf), "%d tas, %d deniz yildizi, %d deniz hiyari, %d yelpaze midyesi, %d deniz kestanesi", pebbles, stars, cukes, pinnas, urchins);
    if (log) *log = buf;
}
