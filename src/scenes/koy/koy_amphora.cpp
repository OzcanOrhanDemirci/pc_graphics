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
// (both ends inside the wall, between the cavity's and the outside's radius at that height: the tapered, capped ends
// are buried; first written with the lower ends 0.8-1.5 cm outside the shoulder and two upper ends in the mouth's cavity)
const float2 h1B[] = {{5.9f, 107.8f}, {7.5f, 108.2f}, {9.6f, 107.0f}, {10.05f, 103.0f}, {10.1f, 95.0f}, {10.1f, 87.0f}, {10.2f, 81.0f}, {10.9f, 77.6f}, {10.9f, 75.0f}};
const float2 h1A[] = {{6.3f, 91.5f}, {7.4f, 92.3f}, {8.9f, 91.2f}, {9.35f, 87.5f}, {9.3f, 78.0f}, {9.4f, 68.0f}, {9.6f, 62.5f}, {10.6f, 60.0f}, {11.0f, 58.8f}};
const float2 h24[] = {{5.8f, 90.0f}, {7.6f, 90.6f}, {9.9f, 89.0f}, {10.2f, 86.0f}, {9.7f, 81.0f}, {9.3f, 75.5f}, {9.4f, 72.5f}, {9.5f, 70.8f}, {8.6f, 69.6f}};
const float2 hL2[] = {{4.8f, 78.0f}, {8.0f, 79.0f}, {10.8f, 78.0f}, {11.4f, 75.0f}, {11.4f, 68.0f}, {11.4f, 61.5f}, {11.6f, 58.4f}, {10.3f, 56.3f}};

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

KMeshCPU koyBuildAmphora(const KoyAmphoraType& t, int segments, unsigned int material, unsigned int id, bool handles) {
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
    if (!handles) return m;
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
            const unsigned firstRing = unsigned(m.pos.size());
            for (int i = 0; i <= HS; ++i) {
                const float u = float(i) / HS;
                // the section narrows to 65 % towards both ends (they merge into the wall instead of standing out of it)
                const float taper = 1.0f - 0.35f * kn::smoothstep(0.85f, 1.0f, u) - 0.35f * (1.0f - kn::smoothstep(0.0f, 0.15f, u));
                const float2 p = curve(u), q = curve(std::min(u + 0.01f, 1.0f)), o = curve(std::max(u - 0.01f, 0.0f));
                const float dr = q.x - o.x, dh = q.y - o.y, L = std::max(std::hypot(dr, dh), 1e-6f);
                const float3 T = make_float3(out.x * dr / L, dh / L, out.z * dr / L);   // along the handle
                const float3 Nn = normalize(cross(tan, T));                                 // radial-ish, in the profile plane
                for (int c = 0; c <= HC; ++c) {
                    const float ph = twoPi * float(c) / float(HC);
                    const float3 dir = Nn * (taper * halfR * std::cos(ph)) + tan * (taper * halfT * std::sin(ph) + shift);
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
            // caps on both ends: no open tube ever shows
            for (int end = 0; end < 2; ++end) {
                const unsigned ring = firstRing + unsigned(end == 0 ? 0 : HS * (HC + 1));
                float3 ctr = make_float3(0.0f, 0.0f, 0.0f);
                for (int c = 0; c < HC; ++c) ctr = ctr + m.pos[ring + unsigned(c)];
                ctr = ctr * (1.0f / float(HC));
                const float3 along = m.pos[ring + unsigned(end == 0 ? HC + 1 : 0) + 0u] - m.pos[ring];   // towards the tube's inside
                const unsigned ci = unsigned(m.pos.size());
                m.pos.push_back(ctr);
                m.nrm.push_back(normalize(end == 0 ? -along : along));
                m.uv.push_back(make_float2(0.0f, end == 0 ? 0.0f : 0.6f));
                for (int c = 0; c < HC; ++c) m.idx.push_back(make_uint3(ci, ring + unsigned(c), ring + unsigned(c + 1)));
            }
        }
    }
    return m;
}

// ---- broken pieces ----------------------------------------------------------------------------------------------------
namespace {

// a profile walked by arc length (so the rim's steps and the shoulder's break keep their shape when resampled)
struct Walk {
    std::vector<float2> p;   // (r, z)
    std::vector<float> s;    // arc length at each point
    float length() const { return s.back(); }
    float2 at(float t) const {   // t in metres along it
        if (t <= 0.0f) return p.front();
        for (size_t k = 1; k < p.size(); ++k)
            if (t <= s[k]) {
                const float u = (t - s[k - 1]) / std::max(s[k] - s[k - 1], 1e-7f);
                return make_float2(p[k - 1].x + (p[k].x - p[k - 1].x) * u, p[k - 1].y + (p[k].y - p[k - 1].y) * u);
            }
        return p.back();
    }
    float arcAt(float z) const {   // where the walk first reaches height z
        if (z <= p.front().y) return 0.0f;
        for (size_t k = 1; k < p.size(); ++k)
            if (z <= p[k].y) {
                const float u = (z - p[k - 1].y) / std::max(p[k].y - p[k - 1].y, 1e-7f);
                return s[k - 1] + (s[k] - s[k - 1]) * u;
            }
        return s.back();
    }
};
Walk makeWalk(const std::vector<float2>& prof) {
    Walk w;
    w.p = prof;
    w.s.assign(prof.size(), 0.0f);
    for (size_t k = 1; k < prof.size(); ++k) w.s[k] = w.s[k - 1] + std::hypot(prof[k].x - prof[k - 1].x, prof[k].y - prof[k - 1].y);
    return w;
}
// the break's wander: smooth noise along a closed circle (heights at an angle) or along a line (angles at a height)
float jagAt(float a, unsigned seed, float amp) {
    const float o = float(seed) * 3.71f;
    return amp * (kn::noise3(std::cos(a) * 1.8f + o, std::sin(a) * 1.8f, 0.37f * o) + 0.22f * kn::noise3(std::cos(a) * 4.3f, std::sin(a) * 4.3f + o, 1.3f));
}
float jagLine(float z, unsigned seed, float amp) {
    const float o = float(seed) * 5.13f;
    return amp * (kn::noise3(z * 14.0f + o, 0.5f, 2.0f) + 0.45f * kn::noise3(z * 41.0f, o, 3.0f));
}

}   // namespace

std::vector<KoyAmphoraPiece> koyAmphoraPieces() {
    const float twoPi = 6.2831853f, pi = 3.14159265f;
    std::vector<KoyAmphoraPiece> v;
    // every type whole (0..3), then the broken ones: bodies whose neck snapped (the commonest find), necks with their
    // handles, half bodies split down their length, sherds
    for (int t = 0; t < koyAmphoraTypes(); ++t) v.push_back({t, 0, 0.0f, 9.0f, 0.0f, twoPi, 0.0f, 0u});
    const KoyAmphoraType& B = koyAmphoraType(0);
    const float sh = 0.79f, top = B.outer.back().y;   // Dressel 1B: the shoulder's top, the rim
    v.push_back({0, 1, 0.0f, 0.86f, 0.0f, twoPi, 0.035f, 11u});   // body, the neck gone a little above the shoulder
    v.push_back({0, 1, 0.0f, 0.76f, 0.0f, twoPi, 0.05f, 12u});    // body broken through the shoulder
    v.push_back({1, 1, 0.0f, 0.66f, 0.0f, twoPi, 0.04f, 13u});    // a Dressel 1A body
    v.push_back({2, 1, 0.0f, 0.62f, 0.0f, twoPi, 0.04f, 14u});    // a Lamboglia 2 body
    v.push_back({0, 2, sh - 0.09f, top, 0.0f, twoPi, 0.03f, 21u});   // a Dressel 1B neck with its handles (broken below their feet)
    v.push_back({1, 2, 0.55f, 0.978f, 0.0f, twoPi, 0.03f, 22u});     // a Dressel 1A neck
    v.push_back({0, 3, 0.24f, 0.74f, -0.15f, pi + 0.2f, 0.05f, 31u});    // half bodies (above the solid toe: the break shows the wall)
    v.push_back({0, 3, 0.26f, 0.70f, 0.3f, pi + 0.1f, 0.06f, 32u});
    v.push_back({0, 4, 0.40f, 0.62f, 0.0f, 0.9f, 0.03f, 41u});   // sherds: hand-sized pieces of the wall
    v.push_back({0, 4, 0.30f, 0.46f, 1.0f, 1.65f, 0.025f, 42u});
    v.push_back({0, 4, 0.50f, 0.74f, 2.0f, 2.55f, 0.03f, 43u});
    return v;
}

KMeshCPU koyBuildPiece(const KoyAmphoraPiece& pc, int segments, unsigned int material, unsigned int id) {
    const KoyAmphoraType& t = koyAmphoraType(pc.type);
    if (pc.kind == 0) return koyBuildAmphora(t, segments, material, id);
    KMeshCPU m;
    m.name = std::string(t.name) + (pc.kind == 1 ? " govde" : pc.kind == 2 ? " boyun" : pc.kind == 3 ? " yarim" : " kirik");
    m.material = material;
    m.id = id;
    const float twoPi = 6.2831853f;
    const bool round = pc.a1 - pc.a0 >= twoPi - 1e-4f;
    const Walk wo = makeWalk(t.outer), wi = makeWalk(t.inner);
    const float hTop = t.outer.back().y;
    const bool cutTop = pc.z1 < hTop - 1e-3f, cutBottom = pc.z0 > 1e-3f;
    // columns over the angle range, rows over the height range; each column's own (jagged) ends
    const int S = round ? std::max(8, segments) : std::max(6, int(float(segments) * (pc.a1 - pc.a0) / twoPi) + 2);
    const int R = std::max(24, int((pc.z1 - pc.z0) * 110.0f));
    const float aSpan = pc.a1 - pc.a0;
    auto columnAngle = [&](int s, float z) {   // the side breaks wander with height, eased over four columns (no folds)
        float a = pc.a0 + aSpan * float(s) / float(S);
        if (!round) {
            a += std::max(0.0f, 1.0f - float(s) / 4.0f) * jagLine(z, pc.seed, 0.12f);
            a += std::max(0.0f, 1.0f - float(S - s) / 4.0f) * jagLine(z, pc.seed + 7u, 0.12f);
        }
        return a;
    };
    auto zEnds = [&](float a, float& zb, float& zt) {
        zb = cutBottom ? pc.z0 + jagAt(a, pc.seed + 3u, pc.jag) : 0.0f;
        zt = cutTop ? pc.z1 + jagAt(a, pc.seed, pc.jag) : hTop;
    };
    const float innerLo = t.inner.front().y;
    // one wall surface: rows by arc length between the column's two ends (the outside: the outer profile; the cavity's
    // wall: the inner one, its normals towards the axis)
    auto surface = [&](const Walk& w, bool cavity, std::vector<unsigned>& bottomRow, std::vector<unsigned>& topRow,
                       std::vector<unsigned>& firstCol, std::vector<unsigned>& lastCol) {
        const unsigned base = unsigned(m.pos.size());
        for (int k = 0; k <= R; ++k)
            for (int s = 0; s <= S; ++s) {
                const float u = float(k) / float(R);
                // the column's angle at its middle height decides its ends; the side breaks bend with height
                float a = pc.a0 + aSpan * float(s) / float(S), zb, zt;
                zEnds(a, zb, zt);
                if (cavity) zb = std::max(zb, innerLo + 0.002f), zt = std::min(zt, t.inner.back().y);
                const float sb = w.arcAt(zb), st = w.arcAt(zt);
                const float2 p = w.at(sb + (st - sb) * u);
                a = columnAngle(s, p.y);
                const float c = std::cos(a), sn = std::sin(a);
                m.pos.push_back(make_float3(p.x * c, p.y, p.x * sn));
                // the normal from the profile's slope there
                const float2 q0 = w.at(sb + (st - sb) * std::max(u - 0.01f, 0.0f)), q1 = w.at(sb + (st - sb) * std::min(u + 0.01f, 1.0f));
                float nr = q1.y - q0.y, nh = -(q1.x - q0.x);
                const float L = std::max(std::hypot(nr, nh), 1e-7f);
                nr /= L;
                nh /= L;
                if (cavity) { nr = -nr; nh = -nh; }
                m.nrm.push_back(make_float3(nr * c, nh, nr * sn));
                m.uv.push_back(make_float2(0.15f * a, p.y));
            }
        for (int k = 0; k < R; ++k)
            for (int s = 0; s < S; ++s) {
                const unsigned a = base + unsigned(k * (S + 1) + s), b = a + 1, c = a + unsigned(S + 1), d = c + 1;
                if (!cavity) {
                    m.idx.push_back(make_uint3(a, c, b));
                    m.idx.push_back(make_uint3(b, c, d));
                } else {
                    m.idx.push_back(make_uint3(a, b, c));
                    m.idx.push_back(make_uint3(b, d, c));
                }
            }
        bottomRow.clear();
        topRow.clear();
        firstCol.clear();
        lastCol.clear();
        for (int s = 0; s <= S; ++s) {
            bottomRow.push_back(base + unsigned(s));
            topRow.push_back(base + unsigned(R * (S + 1) + s));
        }
        for (int k = 0; k <= R; ++k) {
            firstCol.push_back(base + unsigned(k * (S + 1)));
            lastCol.push_back(base + unsigned(k * (S + 1) + S));
        }
    };
    std::vector<unsigned> ob, ot, of, ol, ib, it, iff, il;
    surface(wo, false, ob, ot, of, ol);
    const bool hasCavity = pc.z1 > innerLo + 0.01f;
    if (hasCavity) surface(wi, true, ib, it, iff, il);
    // the breaks: strips across the wall between the outside's edge and the cavity's (fresh clay there; the coat finds it
    // anyway, as on the old breaks of the real ones)
    // ref: the side the break faces (its normal turned that way: the baked occlusion looks out of the break, not into it)
    auto strip = [&](const std::vector<unsigned>& A, const std::vector<unsigned>& B, float3 ref) {
        const size_t n = std::min(A.size(), B.size());
        const unsigned base = unsigned(m.pos.size());
        for (size_t q = 0; q < n; ++q) {   // duplicated vertices: the break's own normals (across the wall's edge)
            const float3 pa = m.pos[A[q]], pb = m.pos[B[q]];
            const float3 along = q + 1 < n ? m.pos[A[q + 1]] - pa : pa - m.pos[A[q - 1]];
            float3 nn = cross(along, pb - pa);
            const float L = length(nn);
            nn = L > 1e-9f ? nn / L : ref;
            if (dot(nn, ref) < 0.0f) nn = -nn;
            m.pos.push_back(pa);
            m.pos.push_back(pb);
            m.nrm.push_back(nn);
            m.nrm.push_back(nn);
            m.uv.push_back(make_float2(0.0f, 0.0f));
            m.uv.push_back(make_float2(0.0f, 0.02f));
        }
        for (size_t q = 0; q + 1 < n; ++q) {
            const unsigned a = base + unsigned(2 * q), b = a + 1, c = a + 2, d = a + 3;
            m.idx.push_back(make_uint3(a, b, c));
            m.idx.push_back(make_uint3(b, d, c));
        }
    };
    if (hasCavity) {
        if (cutTop) strip(ot, it, make_float3(0.0f, 1.0f, 0.0f));
        if (cutBottom && pc.z0 > innerLo) strip(ob, ib, make_float3(0.0f, -1.0f, 0.0f));
        if (!round) {
            strip(of, iff, make_float3(std::sin(pc.a0), 0.0f, -std::cos(pc.a0)));    // towards decreasing angle
            strip(ol, il, make_float3(-std::sin(pc.a1), 0.0f, std::cos(pc.a1)));     // towards increasing angle
        }
    }
    // a body: sediment fills it to a little below the break (a disc, facing up)
    if (pc.kind == 1) {
        float zmin = 1e9f;
        for (int s = 0; s <= S; ++s) {
            float zb, zt;
            zEnds(pc.a0 + aSpan * float(s) / float(S), zb, zt);
            zmin = std::min(zmin, zt);
        }
        const float hf = zmin - 0.06f;
        const float r = radiusAt(t.inner, hf);
        const unsigned c0 = unsigned(m.pos.size());
        m.pos.push_back(make_float3(0.0f, hf, 0.0f));
        m.nrm.push_back(make_float3(0.0f, 1.0f, 0.0f));
        m.uv.push_back(make_float2(0.0f, 0.0f));
        for (int s = 0; s <= S; ++s) {
            const float ph = twoPi * float(s) / float(S);
            m.pos.push_back(make_float3(r * std::cos(ph), hf, r * std::sin(ph)));
            m.nrm.push_back(make_float3(0.0f, 1.0f, 0.0f));
            m.uv.push_back(make_float2(r * std::cos(ph), r * std::sin(ph)));
        }
        for (int s = 0; s < S; ++s) m.idx.push_back(make_uint3(c0, c0 + 1 + unsigned(s) + 1, c0 + 1 + unsigned(s)));
    }
    // a neck keeps its handles: the whole jar's, cut to the piece's heights by taking only those inside it
    if (pc.kind == 2) {
        KMeshCPU whole = koyBuildAmphora(t, segments, material, id);
        // the handles are the whole jar's last vertices: everything after the body built without them
        const size_t handleStart = koyBuildAmphora(t, segments, material, id, false).pos.size();
        const unsigned off = unsigned(m.pos.size());
        for (size_t v = handleStart; v < whole.pos.size(); ++v) {
            m.pos.push_back(whole.pos[v]);
            m.nrm.push_back(whole.nrm[v]);
            m.uv.push_back(whole.uv[v]);
        }
        for (const uint3& tri : whole.idx)
            if (tri.x >= handleStart && tri.y >= handleStart && tri.z >= handleStart)
                m.idx.push_back(make_uint3(off + unsigned(tri.x - handleStart), off + unsigned(tri.y - handleStart), off + unsigned(tri.z - handleStart)));
    }
    return m;
}

// The field. The hull lay along `ship` (degrees from +x towards +z); its cargo stood in rows across it, toes between the
// necks below. When the wood went, the rows fell over across the hull, mostly the same way, some the other, slid and
// rolled a little; sand drifted over the lower ones and the edges; many broke (bodies whose necks snapped, necks with their
// handles, halves), and sherds lie everywhere about. Every piece is placed so it does not pass through another (its body
// as a capsule), lying on the floor or sunk into it.
std::vector<KoyAmphoraPlacement> koyPlaceAmphorae(const Config& cfg, const std::function<float(float, float)>& floorY, const std::string& key,
                                                 const std::function<float(float3)>& obstacle) {
    std::vector<KoyAmphoraPlacement> out;
    const std::vector<KoyAmphoraPiece> pieces = koyAmphoraPieces();
    // the shells and sherds are placed by their own lowest point (their mesh's vertices): their edges touch the sand
    std::vector<std::vector<float3>> verts(pieces.size());
    for (size_t i = 0; i < pieces.size(); ++i)
        if (pieces[i].kind >= 3) verts[i] = koyBuildPiece(pieces[i], 48, 0, 0).pos;
    auto lowest = [&](int piece, float3 rx, float3 ax, float3 rz) {   // the lowest vertex's height over the piece's origin
        float lo = 1e9f;
        for (const float3& v : verts[size_t(piece)]) lo = std::min(lo, rx.y * v.x + ax.y * v.y + rz.y * v.z);
        return lo;
    };
    const int n = cfg.i(key + ".count", 96), nSherds = cfg.i(key + ".sherds", 40);
    const float3 c = cfg.v3(key + ".centre", make_float3(0.0f, 0.0f, 0.0f));
    const float ship = cfg.f(key + ".ship_deg", -75.0f) * 3.14159265f / 180.0f;
    const float halfL = cfg.f(key + ".half_length", 4.4f), halfW = cfg.f(key + ".half_width", 1.9f);
    const float2 U = make_float2(std::cos(ship), std::sin(ship)), V = make_float2(-U.y, U.x);
    std::mt19937 rng(unsigned(cfg.i(key + ".seed", 5)));
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
    // which piece: types by their share among the whole jars; broken pieces by theirs
    const float typeShare[4] = {cfg.f("amfora.share_1b", 0.62f), cfg.f("amfora.share_1a", 0.2f), cfg.f("amfora.share_l2", 0.12f), cfg.f("amfora.share_24", 0.06f)};
    const float pBody = cfg.f("amfora.broken_body", 0.24f), pNeck = cfg.f("amfora.broken_neck", 0.08f), pHalf = cfg.f("amfora.broken_half", 0.07f);
    auto pick = [&](int kind) {   // a random piece of that kind (whole: by type share)
        if (kind == 0) {
            float r = uni(rng), acc = 0.0f;
            for (int k = 0; k < 4; ++k) {
                acc += typeShare[k];
                if (r <= acc) return k;
            }
            return 0;
        }
        std::vector<int> of;
        for (int i = 0; i < int(pieces.size()); ++i)
            if (pieces[i].kind == kind) of.push_back(i);
        return of[std::min(int(uni(rng) * of.size()), int(of.size()) - 1)];
    };
    auto frame = [&](float yaw, float pitch, float roll, float3& rx, float3& ax, float3& rz) {
        ax = make_float3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
        const float3 helper = std::fabs(ax.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0);
        const float3 bx = normalize(cross(helper, ax)), bz = cross(bx, ax);
        const float cr = std::cos(roll), sr = std::sin(roll);
        rx = bx * cr + bz * sr;
        rz = bz * cr - bx * sr;
    };
    auto emit = [&](int piece, float3 rx, float3 ax, float3 rz, float3 tip, float burial) {
        KoyAmphoraPlacement P;
        P.piece = piece;
        P.burial = burial;
        P.m[0] = rx.x; P.m[1] = ax.x; P.m[2] = rz.x; P.m[3] = tip.x;
        P.m[4] = rx.y; P.m[5] = ax.y; P.m[6] = rz.y; P.m[7] = tip.y;
        P.m[8] = rx.z; P.m[9] = ax.z; P.m[10] = rz.z; P.m[11] = tip.z;
        out.push_back(P);
    };
    int tries = 0;
    while (int(out.size()) < n && tries < n * 400) {
        ++tries;
        // where along and across the hull (the middle fuller)
        const float u = halfL * (2.0f * uni(rng) - 1.0f);
        const float vmax = halfW * std::sqrt(std::max(0.0f, 1.0f - (u / (halfL * 1.05f)) * (u / (halfL * 1.05f))));
        const float v = std::min(std::max(0.55f * vmax * gauss(rng), -vmax), vmax);
        const float rk = uni(rng);
        const int kind = rk < pHalf ? 3 : rk < pHalf + pNeck ? 2 : rk < pHalf + pNeck + pBody ? 1 : 0;
        const int piece = pick(kind);
        const KoyAmphoraPiece& PC = pieces[size_t(piece)];
        const KoyAmphoraType& T = koyAmphoraType(PC.type);
        const float H = T.outer.back().y;
        const float z0 = PC.kind == 0 ? 0.0f : PC.z0, z1 = PC.kind == 0 ? H : PC.z1;
        float rmax = 0.0f;
        for (const float2& p : T.outer)
            if (p.y >= z0 - 0.02f && p.y <= z1 + 0.02f) rmax = std::max(rmax, p.x);
        // its lie: across the hull mostly (one way or the other), a few along it; a few whole ones still upright, sunk
        // to the shoulder
        const float r = uni(rng);
        float yaw = std::atan2(V.y, V.x) + 0.30f * gauss(rng);
        if (uni(rng) < 0.5f) yaw += 3.14159265f;
        if (r < 0.12f) yaw = std::atan2(U.y, U.x) + 0.3f * gauss(rng) + (uni(rng) < 0.5f ? 3.14159265f : 0.0f);
        const bool upright = PC.kind == 0 && r > 0.95f;
        const float pitch = upright ? (1.25f + 0.25f * uni(rng)) : 0.10f * gauss(rng);
        // half bodies lie hollow up or down (their opening along the roll), the rest any way round
        float roll = 6.2831853f * uni(rng);
        if (PC.kind == 3) roll = (uni(rng) < 0.5f ? 0.0f : 3.14159265f) - 0.5f * (PC.a0 + PC.a1) + 1.5708f + 0.3f * gauss(rng);
        float3 rx, ax, rz;
        frame(yaw, pitch, roll, rx, ax, rz);
        const float px = c.x + U.x * u + V.x * v, pz = c.z + U.y * u + V.y * v;
        // sunk: the edges and the ends more, the heart of the mound less; plus chance
        const float edge = std::sqrt((u / halfL) * (u / halfL) + (v / std::max(vmax, 0.3f)) * (v / std::max(vmax, 0.3f)) * 0.6f);
        float burial = std::min(std::max(0.15f + 0.45f * edge + 0.18f * gauss(rng), 0.0f), 0.85f);
        const float mid = 0.5f * (z0 + z1);
        const float fy = floorY(px, pz);
        float3 centre;
        if (upright) {
            // the toe deep in the sand, the sand reaching up to about the shoulder
            const float shoulder = 0.62f * H;
            centre = make_float3(px, fy - shoulder * (0.75f + 0.25f * uni(rng)) + ax.y * mid, pz);
            burial = 0.9f;
        } else if (PC.kind == 3) {
            // a shell: its lowest point (an edge, or its back) a few cm in the sand
            const float sink = 0.01f + 0.04f * burial;
            centre = make_float3(px, fy - sink - lowest(piece, rx, ax, rz) + ax.y * mid, pz);
        } else {
            centre = make_float3(px, fy + rmax * (1.0f - 2.0f * burial) * 0.98f, pz);
        }
        const float3 tip = centre - ax * mid;   // the jar's own origin (its foot) for this piece's mid height
        const Cap me{tip + ax * (z0 + 0.14f * (z1 - z0)), tip + ax * (z0 + 0.62f * (z1 - z0)), rmax * 0.95f};
        bool clash = false;
        for (const Cap& o : caps)
            if (segDist(me.a, me.b, o.a, o.b) < me.r + o.r) { clash = true; break; }
        // the rocks they lie among: not into them
        if (!clash && obstacle)
            for (float t : {0.0f, 0.5f, 1.0f})
                if (obstacle(me.a + (me.b - me.a) * t) < me.r + 0.04f) { clash = true; break; }
        if (clash) continue;
        caps.push_back(me);
        emit(piece, rx, ax, rz, tip, burial);
    }
    // sherds: on and about the mound, lying flat-ish, a little sunk
    int placed = 0, stries = 0;
    while (placed < nSherds && stries < nSherds * 200) {
        ++stries;
        const float u = halfL * 1.25f * (2.0f * uni(rng) - 1.0f), v = halfW * 1.5f * gauss(rng) * 0.6f;
        const int piece = pick(4);
        const KoyAmphoraPiece& PC = pieces[size_t(piece)];
        const KoyAmphoraType& T = koyAmphoraType(PC.type);
        const float mid = 0.5f * (PC.z0 + PC.z1), r = radiusAt(T.outer, mid);
        // the sherd's own normal (outward at its middle angle) turned up or down: it lies on its curve
        const float am = 0.5f * (PC.a0 + PC.a1);
        float3 rx, ax, rz;
        const bool down = uni(rng) < 0.5f;
        frame(6.2831853f * uni(rng), 0.0f, -am + (down ? -1.5708f : 1.5708f) + 0.25f * gauss(rng), rx, ax, rz);
        const float px = c.x + U.x * u + V.x * v, pz = c.z + U.y * u + V.y * v;
        const float fy = floorY(px, pz);
        // the sherd's middle point sits at the floor: its offset from the axis is r along the rolled normal
        const float3 outward = rx * std::cos(am) + rz * std::sin(am);
        float3 tip = make_float3(px, 0.0f, pz) - ax * mid - outward * r;
        tip.y = fy - (0.005f + 0.02f * uni(rng)) - lowest(piece, rx, ax, rz);   // its lowest point just in the sand
        const Cap me{make_float3(px, fy, pz), make_float3(px, fy + 0.01f, pz), 0.10f};
        bool clash = obstacle && obstacle(me.a) < 0.12f;
        for (const Cap& o : caps)
            if (segDist(me.a, me.b, o.a, o.b) < me.r + o.r) { clash = true; break; }
        if (clash) continue;
        caps.push_back(me);
        emit(piece, rx, ax, rz, tip, 0.2f);
        ++placed;
    }
    std::printf("amfora tarlasi (%s): %zu parca (%d butun ya da kirik, %d kirik parcacik; %d deneme)\n", key.c_str(), out.size(), int(out.size()) - placed, placed, tries + stries);
    return out;
}
