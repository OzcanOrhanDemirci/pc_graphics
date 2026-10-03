// Scene 5 "Koy", Amfora: the jars and their field (see koy_amphora.h).
#include "scenes/koy/koy_amphora.h"

#include "scenes/koy/koy_noise.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace {

// Profiles (radius, height) in metres from the toe's tip. First pass (A0, composition): proportions after the published
// typology (Dressel 1B ~1.13 m: a tall cylindrical neck, a sharply carinated shoulder, a long tapering body, a solid
// toe; Peacock & Williams class 4, 3), refined in A3 against the drawings gathered in docs/koy/AMFORA_KAYNAK.md.
const KoyAmphoraType kTypes[] = {
    {"Dressel 1B",
     {{0.000f, 0.000f}, {0.018f, 0.008f}, {0.025f, 0.035f}, {0.027f, 0.085f}, {0.033f, 0.125f}, {0.052f, 0.155f}, {0.086f, 0.195f},
      {0.112f, 0.255f}, {0.130f, 0.335f}, {0.141f, 0.425f}, {0.148f, 0.515f}, {0.150f, 0.595f}, {0.149f, 0.655f}, {0.144f, 0.695f},
      {0.126f, 0.716f}, {0.098f, 0.740f}, {0.074f, 0.766f}, {0.060f, 0.797f}, {0.056f, 0.848f}, {0.054f, 0.948f}, {0.055f, 1.018f},
      {0.059f, 1.048f}, {0.069f, 1.064f}, {0.073f, 1.080f}, {0.073f, 1.118f}, {0.068f, 1.128f}},
     0.016f, 0.10f, 1.030f, 0.735f, 0.104f, 0.036f, 0.024f},
    {"Dressel 1A",
     {{0.000f, 0.000f}, {0.017f, 0.008f}, {0.024f, 0.032f}, {0.026f, 0.075f}, {0.033f, 0.110f}, {0.058f, 0.140f}, {0.094f, 0.180f},
      {0.120f, 0.240f}, {0.136f, 0.320f}, {0.145f, 0.410f}, {0.150f, 0.500f}, {0.150f, 0.570f}, {0.146f, 0.625f}, {0.132f, 0.655f},
      {0.104f, 0.680f}, {0.078f, 0.705f}, {0.062f, 0.735f}, {0.057f, 0.790f}, {0.055f, 0.880f}, {0.056f, 0.930f}, {0.062f, 0.950f},
      {0.072f, 0.962f}, {0.074f, 0.990f}, {0.069f, 1.000f}},
     0.015f, 0.09f, 0.915f, 0.680f, 0.100f, 0.034f, 0.023f},
    {"Lamboglia 2",
     {{0.000f, 0.000f}, {0.020f, 0.008f}, {0.030f, 0.030f}, {0.034f, 0.060f}, {0.050f, 0.090f}, {0.085f, 0.130f}, {0.125f, 0.200f},
      {0.150f, 0.290f}, {0.163f, 0.390f}, {0.166f, 0.480f}, {0.160f, 0.560f}, {0.140f, 0.620f}, {0.110f, 0.660f}, {0.080f, 0.690f},
      {0.064f, 0.720f}, {0.060f, 0.780f}, {0.061f, 0.840f}, {0.066f, 0.860f}, {0.080f, 0.872f}, {0.082f, 0.910f}, {0.076f, 0.920f}},
     0.017f, 0.09f, 0.835f, 0.660f, 0.108f, 0.040f, 0.026f},
    {"Dressel 2-4",
     {{0.000f, 0.000f}, {0.016f, 0.010f}, {0.022f, 0.050f}, {0.024f, 0.100f}, {0.030f, 0.140f}, {0.060f, 0.170f}, {0.098f, 0.215f},
      {0.118f, 0.290f}, {0.128f, 0.380f}, {0.131f, 0.480f}, {0.130f, 0.560f}, {0.124f, 0.610f}, {0.108f, 0.650f}, {0.084f, 0.680f},
      {0.064f, 0.705f}, {0.054f, 0.740f}, {0.050f, 0.820f}, {0.050f, 0.880f}, {0.055f, 0.905f}, {0.062f, 0.915f}, {0.062f, 0.940f},
      {0.057f, 0.948f}},
     0.014f, 0.08f, 0.860f, 0.665f, 0.096f, 0.040f, 0.022f},
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
    // the profile walked from the toe up the outside, over the rim and down the inside of the neck to the fill
    std::vector<float2> prof = t.outer;   // (r, h)
    std::vector<int> inward;               // 1 where the surface faces the axis (the inside)
    inward.assign(prof.size(), 0);
    const float hTop = t.outer.back().y, hFill = hTop - t.fillDepth;
    const float rimIn = std::max(t.outer.back().x - t.wall * 1.1f, 0.01f);
    for (int k = 0; k <= 8; ++k) {   // the inside of the mouth and neck, the wall following the outside
        const float h = hTop - 0.004f - (hTop - 0.004f - hFill) * float(k) / 8.0f;
        const float r = std::max(std::min(radiusAt(t.outer, h) - t.wall, rimIn), 0.01f);
        prof.push_back(make_float2(r, h));
        inward.push_back(1);
    }
    prof.push_back(make_float2(0.0f, hFill));   // the sediment's face across the neck (a disc, facing up)
    inward.push_back(2);
    const int R = int(prof.size());
    // arc length along the profile (v), and the normals: the profile's own perpendicular turned about the axis
    std::vector<float> arc(R, 0.0f);
    for (int k = 1; k < R; ++k) arc[k] = arc[k - 1] + std::hypot(prof[k].x - prof[k - 1].x, prof[k].y - prof[k - 1].y);
    const float uLen = twoPi * 0.15f;   // u in metres of the widest girth: the same texel size on every jar
    for (int k = 0; k < R; ++k) {
        const float2 a = prof[std::max(k - 1, 0)], b = prof[std::min(k + 1, R - 1)];
        float nr = (b.y - a.y), nh = -(b.x - a.x);   // outward for a profile going up the outside
        if (inward[k] == 1) { nr = -std::fabs(nr) - 1e-4f; nh = -nh; }
        if (inward[k] == 2) { nr = 0.0f; nh = 1.0f; }
        const float L = std::max(std::hypot(nr, nh), 1e-6f);
        nr /= L;
        nh /= L;
        for (int s = 0; s <= S; ++s) {
            const float ph = twoPi * float(s) / float(S);
            const float c = std::cos(ph), sn = std::sin(ph);
            m.pos.push_back(make_float3(prof[k].x * c, prof[k].y, prof[k].x * sn));
            m.nrm.push_back(make_float3(nr * c, nh, nr * sn));
            m.uv.push_back(make_float2(uLen * float(s) / float(S), arc[k]));
        }
    }
    for (int k = 0; k + 1 < R; ++k)
        for (int s = 0; s < S; ++s) {
            const unsigned a = unsigned(k * (S + 1) + s), b = a + 1, c = a + unsigned(S + 1), d = c + 1;
            m.idx.push_back(make_uint3(a, c, b));
            m.idx.push_back(make_uint3(b, c, d));
        }
    // two handles: a tube of elliptic section swept along an arc from the neck out and down to the shoulder; their ends
    // sink into the jar (the ray tracer needs no seam)
    const int HS = 24, HC = 12;
    for (int side = 0; side < 2; ++side) {
        const float ang = side == 0 ? 0.0f : 3.14159265f;
        const float3 out = make_float3(std::cos(ang), 0.0f, std::sin(ang)), tan = make_float3(-std::sin(ang), 0.0f, std::cos(ang));
        const float rN = radiusAt(t.outer, t.handleTop), rS = radiusAt(t.outer, t.handleBottom);
        auto path = [&](float u) {   // u 0 at the neck .. 1 at the shoulder: radius and height
            const float r0 = rN - 0.01f, r3 = rS - 0.012f;
            const float h0 = t.handleTop, h3 = t.handleBottom;
            const float r1 = t.handleOut, h1 = t.handleTop + 0.012f, r2 = t.handleOut + 0.004f, h2 = t.handleBottom + 0.10f;
            const float v = 1.0f - u;
            const float r = v * v * v * r0 + 3 * v * v * u * r1 + 3 * v * u * u * r2 + u * u * u * r3;
            const float h = v * v * v * h0 + 3 * v * v * u * h1 + 3 * v * u * u * h2 + u * u * u * h3;
            return make_float2(r, h);
        };
        const unsigned base = unsigned(m.pos.size());
        for (int i = 0; i <= HS; ++i) {
            const float u = float(i) / HS;
            const float2 p = path(u), q = path(std::min(u + 0.01f, 1.0f)), o = path(std::max(u - 0.01f, 0.0f));
            const float dr = q.x - o.x, dh = q.y - o.y, L = std::max(std::hypot(dr, dh), 1e-6f);
            // the section's frame: along the jar's circumference (tan) and across in the profile plane
            const float3 T = make_float3(out.x * dr / L, dh / L, out.z * dr / L);
            const float3 Nn = normalize(cross(tan, T));
            for (int c = 0; c <= HC; ++c) {
                const float ph = twoPi * float(c) / float(HC);
                const float3 dir = tan * (0.5f * t.handleW * std::cos(ph)) + Nn * (0.5f * t.handleT * std::sin(ph));
                const float3 nrm = normalize(tan * (std::cos(ph) / (0.5f * t.handleW)) + Nn * (std::sin(ph) / (0.5f * t.handleT)));
                m.pos.push_back(make_float3(out.x * p.x, p.y, out.z * p.x) + dir);
                m.nrm.push_back(nrm);
                m.uv.push_back(make_float2(0.5f * (t.handleW + t.handleT) * 3.14159265f * float(c) / float(HC), 0.6f * u));
            }
        }
        for (int i = 0; i < HS; ++i)
            for (int c = 0; c < HC; ++c) {
                const unsigned a = base + unsigned(i * (HC + 1) + c), b = a + 1, cc = a + unsigned(HC + 1), d = cc + 1;
                m.idx.push_back(make_uint3(a, cc, b));
                m.idx.push_back(make_uint3(b, cc, d));
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
