// Scene 5 "Koy", Amfora: the jars and their field (see koy_amphora.h).
#include "scenes/koy/koy_amphora.h"

#include "scenes/koy/koy_noise.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace {

// The profiles: ArchAIDE consortium (2019), vector profiles of the University of Southampton's drawings (P. Copeland,
// "Roman Amphorae: a digital resource", 2014), CC BY 4.0 (assets/koy/LICENSES.md): Dressel 1B DR105, Dressel 1A DR104,
// Dressel 2-4 Italian DR139, Lamboglia 2 DR136. Simplified to 28 points (at most 2.2 mm off the full outline) and read
// from the drawings in docs/koy/AMFORA_KAYNAK.md §2.4; the handles from §2.5 (centre line between their outer and inner
// edges, the section's radial and tangential size). Centimetres here, metres in the mesh.
struct Row { float z, ro, ri; };   // height, outer radius, inner radius (< 0: solid there)
const Row k1B[] = {{0.0f, 0.00f, -1}, {0.0f, 3.05f, -1}, {1.0f, 3.73f, -1}, {6.8f, 2.84f, -1}, {12.7f, 3.56f, -1}, {21.6f, 6.18f, 1.52f},
                   {26.7f, 8.21f, 6.05f}, {29.4f, 9.65f, 7.41f}, {37.4f, 12.70f, 10.33f}, {42.4f, 13.93f, 11.73f}, {46.7f, 14.44f, 12.32f},
                   {53.0f, 14.52f, 12.07f}, {62.7f, 13.89f, 11.43f}, {74.3f, 13.67f, 9.48f}, {79.1f, 7.11f, 4.87f}, {80.7f, 6.10f, 4.40f},
                   {82.3f, 5.63f, 4.36f}, {85.7f, 5.59f, 4.57f}, {92.2f, 6.27f, 4.61f}, {101.2f, 5.93f, 4.40f}, {104.2f, 6.82f, 4.70f},
                   {109.6f, 6.77f, 5.21f}, {111.0f, 7.41f, 5.59f}, {112.1f, 8.89f, 6.22f}, {114.9f, 8.34f, 6.77f}, {117.2f, 8.85f, 6.69f},
                   {117.8f, 8.47f, 7.03f}, {117.9f, 7.45f, 7.45f}};
const Row k1A[] = {{0.0f, 0.00f, -1}, {0.1f, 2.50f, -1}, {0.5f, 3.05f, -1}, {4.5f, 3.18f, -1}, {9.7f, 4.78f, -1}, {11.0f, 5.50f, -1},
                   {15.5f, 8.89f, 5.80f}, {19.1f, 10.71f, 8.09f}, {26.0f, 12.70f, 10.67f}, {34.4f, 13.67f, 12.11f}, {42.6f, 14.10f, 12.74f},
                   {52.7f, 14.06f, 12.62f}, {57.6f, 13.80f, 10.33f}, {58.5f, 13.25f, 8.17f}, {61.0f, 8.55f, 5.88f}, {62.0f, 7.54f, 5.46f},
                   {64.7f, 6.22f, 4.87f}, {67.9f, 5.59f, 4.61f}, {70.7f, 5.38f, 4.40f}, {79.0f, 5.42f, 4.11f}, {88.1f, 6.05f, 5.00f},
                   {88.7f, 6.35f, 5.08f}, {93.2f, 7.49f, 5.84f}, {93.3f, 8.38f, 5.84f}, {93.9f, 8.81f, 6.05f}, {97.2f, 8.38f, 7.28f},
                   {97.7f, 7.96f, 7.58f}, {97.8f, 7.62f, 7.62f}};
const Row k24[] = {{0.0f, 0.00f, -1}, {0.3f, 1.91f, -1}, {0.7f, 2.58f, -1}, {1.6f, 3.30f, -1}, {4.1f, 3.18f, -1}, {5.9f, 2.71f, 0.97f},
                   {9.0f, 2.96f, 1.57f}, {12.3f, 3.98f, 2.75f}, {21.1f, 8.04f, 6.60f}, {27.8f, 10.41f, 9.19f}, {35.7f, 12.19f, 11.05f},
                   {44.0f, 13.08f, 11.81f}, {51.3f, 13.34f, 12.11f}, {60.5f, 13.29f, 12.02f}, {64.2f, 13.17f, 11.81f}, {67.7f, 12.70f, 10.03f},
                   {69.3f, 11.09f, 7.07f}, {72.0f, 6.14f, 4.36f}, {72.8f, 5.42f, 3.94f}, {74.9f, 4.61f, 3.60f}, {80.2f, 4.83f, 3.68f},
                   {84.4f, 5.38f, 4.32f}, {87.3f, 6.14f, 4.78f}, {92.0f, 6.65f, 5.55f}, {93.1f, 7.45f, 5.50f}, {93.7f, 7.37f, 5.55f},
                   {94.1f, 7.03f, 5.80f}, {94.3f, 6.05f, 6.05f}};
const Row kL2[] = {{0.0f, 0.00f, -1}, {0.5f, 2.16f, -1}, {3.7f, 3.90f, -1}, {6.0f, 5.80f, 0.76f}, {6.8f, 6.90f, 3.09f}, {9.5f, 12.28f, 8.76f},
                   {13.1f, 15.92f, 13.42f}, {15.3f, 17.49f, 15.28f}, {17.8f, 18.63f, 16.81f}, {21.2f, 19.69f, 18.16f}, {24.5f, 20.19f, 18.84f},
                   {31.2f, 20.41f, 19.05f}, {37.5f, 19.81f, 18.37f}, {46.0f, 18.25f, 16.77f}, {53.7f, 16.13f, 13.42f}, {54.8f, 15.41f, 10.84f},
                   {55.7f, 12.83f, 9.40f}, {58.1f, 8.76f, 6.60f}, {59.9f, 7.11f, 5.50f}, {62.4f, 6.14f, 4.91f}, {69.3f, 5.12f, 4.02f},
                   {75.1f, 5.00f, 3.73f}, {80.1f, 5.76f, 4.45f}, {81.2f, 6.73f, 4.95f}, {81.5f, 7.37f, 5.08f}, {82.3f, 7.66f, 5.42f},
                   {84.9f, 7.96f, 6.69f}, {85.5f, 7.28f, 7.28f}};
// handle centre lines (radius, height) in cm: inside the neck under the rim, out, down the free part (between the
// handle's outer and inner edges of §2.5), into the shoulder
const float2 h1B[] = {{5.0f, 107.8f}, {7.5f, 108.2f}, {9.6f, 107.0f}, {10.05f, 103.0f}, {10.1f, 95.0f}, {10.1f, 87.0f}, {10.2f, 81.0f}, {10.9f, 77.6f}, {11.4f, 75.4f}};
const float2 h1A[] = {{4.8f, 91.5f}, {7.0f, 92.3f}, {8.9f, 91.2f}, {9.35f, 87.5f}, {9.3f, 78.0f}, {9.4f, 68.0f}, {9.6f, 62.5f}, {10.6f, 60.0f}, {11.4f, 59.0f}};
const float2 h24[] = {{4.5f, 90.0f}, {7.5f, 90.6f}, {9.9f, 89.0f}, {10.2f, 86.0f}, {9.7f, 81.0f}, {9.3f, 75.5f}, {9.4f, 72.5f}, {9.9f, 70.6f}, {10.0f, 69.9f}};
const float2 hL2[] = {{4.6f, 78.0f}, {8.0f, 79.0f}, {10.8f, 78.0f}, {11.4f, 75.0f}, {11.4f, 68.0f}, {11.4f, 61.5f}, {12.0f, 58.5f}, {12.2f, 56.6f}};

template <size_t N, size_t H>
KoyAmphoraType makeType(const char* name, const Row (&rows)[N], const float2 (&hp)[H], float hr, float ht, bool dbl, float fill) {
    KoyAmphoraType t;
    t.name = name;
    for (const Row& r : rows) {
        t.outer.push_back(make_float2(r.ro * 0.01f, r.z * 0.01f));
        if (r.ri >= 0.0f) t.inner.push_back(make_float2(r.ri * 0.01f, r.z * 0.01f));
    }
    for (const float2& p : hp) t.handle.push_back(make_float2(p.x * 0.01f, p.y * 0.01f));
    t.handleR = hr;
    t.handleT = ht;
    t.doubleRod = dbl;
    t.fillDepth = fill;
    return t;
}
const KoyAmphoraType kTypes[] = {
    makeType("Dressel 1B", k1B, h1B, 0.033f, 0.054f, false, 0.12f),
    makeType("Dressel 1A", k1A, h1A, 0.031f, 0.054f, false, 0.10f),
    makeType("Lamboglia 2", kL2, hL2, 0.035f, 0.033f, false, 0.08f),
    makeType("Dressel 2-4", k24, h24, 0.033f, 0.052f, true, 0.09f),
};

float radiusAt(const std::vector<float2>& p, float h) {
    if (h <= p.front().y) return p.front().x;
    for (size_t k = 1; k < p.size(); ++k)
        if (h <= p[k].y) {
            const float t = (h - p[k - 1].y) / std::max(p[k].y - p[k - 1].y, 1e-6f);
            return p[k - 1].x + (p[k].x - p[k - 1].x) * t;
        }
    return p.back().x;
}

// a polyline (radius, height) turned about the axis: rings of S + 1 vertices, normals from the profile's own
// perpendicular (flipped for the cavity's wall, which faces the axis)
void lathe(KMeshCPU& m, const std::vector<float2>& prof, int S, bool facingAxis, float vOffset) {
    const float twoPi = 6.2831853f, uLen = twoPi * 0.15f;
    const int R = int(prof.size());
    const unsigned base = unsigned(m.pos.size());
    float arc = vOffset;
    for (int k = 0; k < R; ++k) {
        if (k > 0) arc += std::hypot(prof[k].x - prof[k - 1].x, prof[k].y - prof[k - 1].y);
        const float2 a = prof[std::max(k - 1, 0)], b = prof[std::min(k + 1, R - 1)];
        float nr = (b.y - a.y), nh = -(b.x - a.x);   // outward for a profile going up the outside
        if (facingAxis) { nr = -nr; nh = -nh; }
        const float L = std::max(std::hypot(nr, nh), 1e-6f);
        nr /= L;
        nh /= L;
        for (int s = 0; s <= S; ++s) {
            const float ph = twoPi * float(s) / float(S);
            const float c = std::cos(ph), sn = std::sin(ph);
            m.pos.push_back(make_float3(prof[k].x * c, prof[k].y, prof[k].x * sn));
            m.nrm.push_back(make_float3(nr * c, nh, nr * sn));
            m.uv.push_back(make_float2(uLen * float(s) / float(S), arc));
        }
    }
    for (int k = 0; k + 1 < R; ++k)
        for (int s = 0; s < S; ++s) {
            const unsigned a = base + unsigned(k * (S + 1) + s), b = a + 1, c = a + unsigned(S + 1), d = c + 1;
            if (prof[k].x < 1e-5f && prof[k + 1].x < 1e-5f) continue;   // a degenerate band on the axis
            m.idx.push_back(make_uint3(a, c, b));
            m.idx.push_back(make_uint3(b, c, d));
        }
}

}   // namespace

int koyAmphoraTypes() { return int(sizeof(kTypes) / sizeof(kTypes[0])); }
const KoyAmphoraType& koyAmphoraType(int k) { return kTypes[std::min(std::max(k, 0), koyAmphoraTypes() - 1)]; }

KMeshCPU koyBuildAmphora(const KoyAmphoraType& t, int segments, unsigned int material, unsigned int id) {
    KMeshCPU m;
    m.name = t.name;
    m.material = material;
    m.id = id;
    const int S = std::max(8, segments);
    const float twoPi = 6.2831853f;
    // the outside, from the foot up over the rim's top (the profile closes onto the inner radius at the top)
    lathe(m, t.outer, S, false, 0.0f);
    // the inside of the mouth and neck, from the rim down to the sediment that fills the jar
    const float hTop = t.outer.back().y, hFill = hTop - t.fillDepth;
    std::vector<float2> in;
    for (size_t k = t.inner.size(); k-- > 0;) {
        if (t.inner[k].y < hFill) break;
        in.push_back(make_float2(t.inner[k].x, t.inner[k].y));
    }
    in.push_back(make_float2(radiusAt(t.inner, hFill), hFill));
    // walking down the inside: the profile goes downwards, so its outward perpendicular already faces the axis
    lathe(m, in, S, false, 0.0f);
    // the sediment's face across the neck (a disc, facing up)
    {
        const float r = in.back().x;
        const unsigned c0 = unsigned(m.pos.size());
        m.pos.push_back(make_float3(0.0f, hFill, 0.0f));
        m.nrm.push_back(make_float3(0.0f, 1.0f, 0.0f));
        m.uv.push_back(make_float2(0.0f, 0.0f));
        for (int s = 0; s <= S; ++s) {
            const float ph = twoPi * float(s) / float(S);
            m.pos.push_back(make_float3(r * std::cos(ph), hFill, r * std::sin(ph)));
            m.nrm.push_back(make_float3(0.0f, 1.0f, 0.0f));
            m.uv.push_back(make_float2(r * std::cos(ph), r * std::sin(ph)));
        }
        for (int s = 0; s < S; ++s) m.idx.push_back(make_uint3(c0, c0 + 1 + unsigned(s) + 1, c0 + 1 + unsigned(s)));
    }
    // two handles: a section swept along the centre line (Catmull-Rom between its points), elliptic (radial x
    // tangential), or two round rods side by side for the bifid Dressel 2-4 handle; their ends sink into the jar
    const int HC = 12;
    auto curve = [&](float u) {   // u in 0..1 along the centre line
        const int n = int(t.handle.size());
        const float f = u * float(n - 1);
        const int i = std::min(int(f), n - 2);
        const float s = f - float(i);
        const float2 p0 = t.handle[std::max(i - 1, 0)], p1 = t.handle[i], p2 = t.handle[i + 1], p3 = t.handle[std::min(i + 2, n - 1)];
        const float s2 = s * s, s3 = s2 * s;
        const float a0 = -0.5f * s3 + s2 - 0.5f * s, a1 = 1.5f * s3 - 2.5f * s2 + 1.0f, a2 = -1.5f * s3 + 2.0f * s2 + 0.5f * s, a3 = 0.5f * s3 - 0.5f * s2;
        return make_float2(p0.x * a0 + p1.x * a1 + p2.x * a2 + p3.x * a3, p0.y * a0 + p1.y * a1 + p2.y * a2 + p3.y * a3);
    };
    const int HS = 40;
    for (int side = 0; side < 2; ++side) {
        const float ang = side == 0 ? 0.0f : 3.14159265f;
        const float3 out = make_float3(std::cos(ang), 0.0f, std::sin(ang)), tan = make_float3(-std::sin(ang), 0.0f, std::cos(ang));
        const int rods = t.doubleRod ? 2 : 1;
        for (int rod = 0; rod < rods; ++rod) {
            // a single handle: the full section; a bifid one: two rods of the radial size, side by side tangentially
            const float halfR = 0.5f * t.handleR, halfT = t.doubleRod ? 0.5f * t.handleR : 0.5f * t.handleT;
            const float shift = t.doubleRod ? (rod == 0 ? -1.0f : 1.0f) * (0.5f * t.handleT - halfT) : 0.0f;
            const unsigned base = unsigned(m.pos.size());
            for (int i = 0; i <= HS; ++i) {
                const float u = float(i) / HS;
                const float2 p = curve(u), q = curve(std::min(u + 0.01f, 1.0f)), o = curve(std::max(u - 0.01f, 0.0f));
                const float dr = q.x - o.x, dh = q.y - o.y, L = std::max(std::hypot(dr, dh), 1e-6f);
                const float3 T = make_float3(out.x * dr / L, dh / L, out.z * dr / L);   // along the handle
                const float3 Nn = normalize(cross(tan, T));                                 // radial-ish, in the profile plane
                for (int c = 0; c <= HC; ++c) {
                    const float ph = twoPi * float(c) / float(HC);
                    const float3 dir = Nn * (halfR * std::cos(ph)) + tan * (halfT * std::sin(ph) + shift);
                    const float3 nrm = normalize(Nn * (std::cos(ph) / halfR) + tan * (std::sin(ph) / halfT));
                    m.pos.push_back(make_float3(out.x * p.x, p.y, out.z * p.x) + dir);
                    m.nrm.push_back(nrm);
                    m.uv.push_back(make_float2(float(c) / HC * 0.15f, 0.6f * u));
                }
            }
            for (int i = 0; i < HS; ++i)
                for (int c = 0; c < HC; ++c) {
                    const unsigned a = base + unsigned(i * (HC + 1) + c), b = a + 1, cc = a + unsigned(HC + 1), d = cc + 1;
                    m.idx.push_back(make_uint3(a, cc, b));
                    m.idx.push_back(make_uint3(b, cc, d));
                }
        }
    }
    return m;
}

// The field. The hull lay along `ship` (degrees from +x towards +z); its cargo stood in rows across it, toes between the
// necks below. When the wood went, the rows fell over across the hull, mostly the same way, some the other, slid and
// rolled a little; sand drifted over the lower ones and the edges. Every jar is placed so it does not pass through
// another (its body as a capsule), lying on the floor or sunk into it.
std::vector<KoyAmphoraPlacement> koyPlaceAmphorae(const Config& cfg, const std::function<float(float, float)>& floorY) {
    std::vector<KoyAmphoraPlacement> out;
    const int n = cfg.i("amfora.count", 64);
    const float3 c = cfg.v3("amfora.centre", make_float3(0.0f, 0.0f, 0.0f));
    const float ship = cfg.f("amfora.ship_deg", -75.0f) * 3.14159265f / 180.0f;
    const float halfL = cfg.f("amfora.half_length", 4.4f), halfW = cfg.f("amfora.half_width", 1.9f);
    const float2 U = make_float2(std::cos(ship), std::sin(ship)), V = make_float2(-U.y, U.x);
    std::mt19937 rng(unsigned(cfg.i("amfora.seed", 5)));
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    struct Cap { float3 a, b; float r; };
    std::vector<Cap> caps;
    auto segDist = [](float3 p1, float3 q1, float3 p2, float3 q2) {   // closest distance between two segments
        const float3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
        const float a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r), cc = dot(d1, r), b = dot(d1, d2);
        const float den = a * e - b * b;
        float s = den > 1e-8f ? std::min(std::max((b * f - cc * e) / den, 0.0f), 1.0f) : 0.0f;
        float t = (b * s + f) / std::max(e, 1e-8f);
        if (t < 0.0f) { t = 0.0f; s = std::min(std::max(-cc / std::max(a, 1e-8f), 0.0f), 1.0f); }
        else if (t > 1.0f) { t = 1.0f; s = std::min(std::max((b - cc) / std::max(a, 1e-8f), 0.0f), 1.0f); }
        return length((p1 + d1 * s) - (p2 + d2 * t));
    };
    const float typeShare[4] = {cfg.f("amfora.share_1b", 0.62f), cfg.f("amfora.share_1a", 0.2f), cfg.f("amfora.share_l2", 0.12f), cfg.f("amfora.share_24", 0.06f)};
    int tries = 0;
    while (int(out.size()) < n && tries < n * 400) {
        ++tries;
        // where along and across the hull (the middle fuller), which type
        const float u = halfL * (2.0f * uni(rng) - 1.0f);
        const float vmax = halfW * std::sqrt(std::max(0.0f, 1.0f - (u / (halfL * 1.05f)) * (u / (halfL * 1.05f))));
        const float v = std::min(std::max(0.55f * vmax * gauss(rng), -vmax), vmax);
        float pick = uni(rng), acc = 0.0f;
        int type = 0;
        for (int k = 0; k < 4; ++k) {
            acc += typeShare[k];
            if (pick <= acc) { type = k; break; }
        }
        const KoyAmphoraType& T = kTypes[type];
        const float H = T.outer.back().y;
        float rmax = 0.0f;
        for (const float2& p : T.outer) rmax = std::max(rmax, p.x);
        // its lie: across the hull mostly (one way or the other), a few along it; a few still half upright
        const float r = uni(rng);
        float yaw = std::atan2(V.y, V.x) + 0.30f * gauss(rng);
        if (uni(rng) < 0.5f) yaw += 3.14159265f;
        if (r < 0.12f) yaw = std::atan2(U.y, U.x) + 0.3f * gauss(rng) + (uni(rng) < 0.5f ? 3.14159265f : 0.0f);
        const bool upright = r > 0.95f;
        const float pitch = upright ? (0.9f + 0.4f * uni(rng)) : 0.12f * gauss(rng);   // radians off the horizontal, toe down > 0
        const float roll = 6.2831853f * uni(rng);
        // the frame: the jar's axis (y) laid along (yaw, pitch); its middle at mid-height on the floor
        const float3 ax = make_float3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
        const float3 helper = std::fabs(ax.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0);
        float3 bx = normalize(cross(helper, ax));
        float3 bz = cross(bx, ax);
        const float cr = std::cos(roll), sr = std::sin(roll);
        const float3 rx = bx * cr + bz * sr, rz = bz * cr - bx * sr;
        const float px = c.x + U.x * u + V.x * v, pz = c.z + U.y * u + V.y * v;
        // sunk: the edges and the ends more, the heart of the mound less; plus chance
        const float edge = std::sqrt((u / halfL) * (u / halfL) + (v / std::max(vmax, 0.3f)) * (v / std::max(vmax, 0.3f)) * 0.6f);
        float burial = std::min(std::max(0.15f + 0.45f * edge + 0.18f * gauss(rng), 0.0f), 0.85f);
        if (upright) burial = 0.0f;
        const float mid = 0.5f * H;
        const float fy = floorY(px, pz);
        // lying: the axis rmax above the floor (resting), sunk by burial of its width; upright: the toe sunk deep
        float3 centre;
        if (upright) {
            const float sink = 0.35f + 0.25f * uni(rng);
            centre = make_float3(px, fy - sink + ax.y * mid, pz);
        } else {
            centre = make_float3(px, fy + rmax * (1.0f - 2.0f * burial) * 0.98f, pz);
        }
        const float3 tip = centre - ax * mid;
        // capsule: the body from the toe's top to the shoulder
        const Cap me{tip + ax * (0.16f * H), tip + ax * (0.70f * H), rmax * 0.95f};
        bool clash = false;
        for (const Cap& o : caps)
            if (segDist(me.a, me.b, o.a, o.b) < me.r + o.r) { clash = true; break; }
        if (clash) continue;
        caps.push_back(me);
        KoyAmphoraPlacement P;
        P.type = type;
        P.burial = burial;
        // columns: rx (object x), ax (object y), rz (object z); translation: the tip
        P.m[0] = rx.x; P.m[1] = ax.x; P.m[2] = rz.x; P.m[3] = tip.x;
        P.m[4] = rx.y; P.m[5] = ax.y; P.m[6] = rz.y; P.m[7] = tip.y;
        P.m[8] = rx.z; P.m[9] = ax.z; P.m[10] = rz.z; P.m[11] = tip.z;
        out.push_back(P);
    }
    std::printf("amfora tarlasi: %zu amfora (%d deneme)\n", out.size(), tries);
    return out;
}
