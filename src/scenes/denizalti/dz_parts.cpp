#include "scenes/denizalti/dz_parts.h"

#include <algorithm>
#include <cmath>

namespace dz {

namespace {
const float kPi = 3.14159265358979f;
float3 f3(float x, float y, float z) { return make_float3(x, y, z); }

// runs f with the kit placed at local (in its current frame) and its material/feature set, then restores them
template <class F>
void with(Kit& k, const Xform& local, uint32_t mat, uint32_t feat, float2 fp, F f) {
    const Xform t0 = k.t;
    const uint32_t m0 = k.mat, f0 = k.feat;
    const float2 p0 = k.fp;
    k.t = t0 * local;
    k.mat = mat;
    k.feat = feat;
    k.fp = fp;
    f();
    k.t = t0;
    k.mat = m0;
    k.feat = f0;
    k.fp = p0;
}
Xform axes(float3 o, float3 x, float3 y, float3 z) {
    Xform t;
    t.o = o;
    t.x = x;
    t.y = y;
    t.z = z;
    return t;
}
}   // namespace

void pipeRun(Kit& k, const std::vector<float3>& pts, float r, float bendR) {
    const int n = int(pts.size());
    if (n < 2) return;
    std::vector<float3> s(n - 1), e(n - 1);   // each straight's start and end after the bends take their share
    for (int i = 0; i + 1 < n; ++i) {
        s[i] = pts[i];
        e[i] = pts[i + 1];
    }
    std::vector<float> ang(n, 0.0f);
    for (int i = 1; i + 1 < n; ++i) {
        const float3 d0 = normalize(pts[i] - pts[i - 1]), d1 = normalize(pts[i + 1] - pts[i]);
        const float a = std::acos(std::max(-1.0f, std::min(1.0f, dot(d0, d1))));
        ang[i] = a;
        const float cut = bendR * std::tan(0.5f * a);
        e[i - 1] = pts[i] - d0 * cut;
        s[i] = pts[i] + d1 * cut;
    }
    float acc = 0.0f;   // the run's length so far: the surface coordinate along it
    for (int i = 0; i + 1 < n; ++i) {
        const float3 d = normalize(pts[i + 1] - pts[i]);
        const float len = length(e[i] - s[i]);
        if (len > 1e-5f) {
            Xform t = Xform::frame(s[i] - d * acc, d, std::fabs(d.y) < 0.95f ? f3(0, 1, 0) : f3(1, 0, 0));
            const Xform t0 = k.t;
            k.t = t0 * t;
            k.tube(r, acc, acc + len);
            k.t = t0;
            acc += len;
        }
        if (i + 2 < n && ang[i + 1] > 1e-4f) {
            const float3 d0 = d, d1 = normalize(pts[i + 2] - pts[i + 1]);
            const float3 x = normalize(d1 - d0 * dot(d0, d1));
            const float3 y = cross(d0, x);
            const Xform t0 = k.t;
            k.t = t0 * axes(e[i], x, y, d0);
            k.bend(r, bendR, ang[i + 1], acc);
            k.t = t0;
            acc += bendR * ang[i + 1];
        }
    }
}

void studAndNut(Kit& k, float s, float rs, float len) {
    const float hn = 0.8f * 2.0f * rs + 0.002f;   // a heavy hex nut's height ~ the thread's diameter
    with(k, Xform(), k.mat, FEAT_NONE, make_float2(0, 0), [&] { k.hexPrism(s, 0.0f, hn, 0.0012f); });
    // the stud: threaded (11 threads to the inch on 5/8", ~2.3 mm pitch), a chamfered end
    with(k, Xform(), MAT_STEEL, FEAT_THREAD, make_float2(0.0023f, 0.0f), [&] { k.roundedCylinder(rs, hn - 0.002f, hn + len, 0.0012f, false, true); });
}

void flangePair(Kit& k, float r, float rf, float tf, int n) {
    with(k, Xform(), k.mat, FEAT_CAST, make_float2(0, 0), [&] {
        k.roundedCylinder(rf, -tf, 0.0f, 0.0025f);
        k.roundedCylinder(rf, 0.0f, tf, 0.0025f);
        // the hubs where the pipe enters (a tapered neck)
        k.lathe({{r * 1.25f, tf}, {r * 1.32f, tf + 0.004f}, {r * 1.06f, tf + 0.03f}, {r * 1.0f, tf + 0.034f}});
        k.lathe({{r * 1.0f, -tf - 0.034f}, {r * 1.06f, -tf - 0.03f}, {r * 1.32f, -tf - 0.004f}, {r * 1.25f, -tf}});
    });
    const float rb = 0.5f * (r * 1.35f + rf) + 0.002f;   // the bolt circle
    const float rs = std::max(0.005f, 0.11f * rf);         // the bolts: 5/8" on a 6" flange
    const float s = 2.0f * rs * 1.6f + 0.002f;
    for (int i = 0; i < n; ++i) {
        const float a = (i + 0.5f) * 2.0f * kPi / n;
        const float3 o = f3(rb * std::cos(a), rb * std::sin(a), 0);
        Xform up = Xform::at(o + f3(0, 0, tf));
        up = up * Xform::rotZ(a + 0.37f * i);   // nuts not all turned alike
        Xform dn = Xform::at(o + f3(0, 0, -tf)) * Xform::rotX(kPi) * Xform::rotZ(-a + 0.21f * i);
        with(k, up, k.mat, FEAT_NONE, make_float2(0, 0), [&] { studAndNut(k, s, rs, 0.004f + 0.002f * (i % 3)); });
        with(k, dn, k.mat, FEAT_NONE, make_float2(0, 0), [&] { studAndNut(k, s, rs, 0.003f + 0.002f * ((i + 1) % 3)); });
    }
}

void handwheel(Kit& k, float R, float r, int n, float rh, uint32_t rimMat, uint32_t hubMat) {
    with(k, Xform(), rimMat, FEAT_CAST, make_float2(0, 0), [&] { k.torus(R, r); });
    // spokes: slightly dished (the hub sits a little proud of the rim's plane), oval-ish round bars
    const float rs = 0.55f * r;
    for (int i = 0; i < n; ++i) {
        const float a = (i + 0.25f) * 2.0f * kPi / n;
        const float3 dir = f3(std::cos(a), std::sin(a), 0);
        const float3 p0 = dir * (rh * 0.8f) + f3(0, 0, 0.010f), p1 = dir * (R - 0.4f * r);
        const float3 d = p1 - p0;
        Xform t = Xform::frame(p0, d, f3(0, 0, 1));
        with(k, t, rimMat, FEAT_CAST, make_float2(0, 0), [&] { k.roundedCylinder(rs, 0.0f, length(d), 0.25f * rs, false, false); });
    }
    with(k, Xform(), hubMat, FEAT_CAST, make_float2(0, 0), [&] {
        k.roundedCylinder(rh, -0.004f, 0.022f, 0.004f);
    });
    // the nut holding it on the stem
    with(k, Xform::at(f3(0, 0, 0.022f)), MAT_STEEL, FEAT_NONE, make_float2(0, 0), [&] { k.hexPrism(0.6f * rh * 2.0f * 0.55f, 0.0f, 0.007f, 0.001f); });
}

void globeValve(Kit& k, float rp, float D, uint32_t wheelMat) {
    // the body: a cast bulb along x with collars where the pipes enter (a lathe about the pipe's axis)
    const float rb = 1.75f * rp, L = 5.0f * rp;
    Xform alongX = axes(f3(0, 0, 0), f3(0, 1, 0), f3(0, 0, 1), f3(1, 0, 0));   // the lathe's z along the pipe
    with(k, alongX, k.mat, FEAT_CAST, make_float2(0, 0), [&] {
        k.lathe({{rp * 1.0f, -0.5f * L - 0.004f},
                 {rp * 1.22f, -0.5f * L},
                 {rp * 1.26f, -0.5f * L + 0.006f},
                 {rp * 1.26f, -0.5f * L + 0.18f * L},
                 {rp * 1.12f, -0.5f * L + 0.22f * L},
                 {rb * 0.92f, -0.18f * L},
                 {rb, 0.0f},
                 {rb * 0.92f, 0.18f * L},
                 {rp * 1.12f, 0.5f * L - 0.22f * L},
                 {rp * 1.26f, 0.5f * L - 0.18f * L},
                 {rp * 1.26f, 0.5f * L - 0.006f},
                 {rp * 1.22f, 0.5f * L},
                 {rp * 1.0f, 0.5f * L + 0.004f}});
    });
    // bonnet, union ring and packing nut up +z; the stem; the wheel
    const float zb = rb * 0.82f;
    with(k, Xform(), k.mat, FEAT_CAST, make_float2(0, 0), [&] {
        k.lathe({{rb * 0.62f, zb - 0.01f}, {rb * 0.66f, zb}, {rb * 0.66f, zb + 0.012f}, {rb * 0.5f, zb + 0.016f},
                 {rp * 0.75f, zb + 0.045f}, {rp * 0.72f, zb + 0.08f}, {rp * 0.6f, zb + 0.085f}});
    });
    with(k, Xform::at(f3(0, 0, zb + 0.004f)), k.mat, FEAT_NONE, make_float2(0, 0), [&] { k.hexPrism(rb * 1.25f, 0.0f, 0.014f, 0.0015f); });
    with(k, Xform::at(f3(0, 0, zb + 0.085f)), MAT_BRASS, FEAT_NONE, make_float2(0, 0), [&] { k.hexPrism(rp * 1.1f, 0.0f, 0.016f, 0.0012f); });
    const float zs0 = zb + 0.1f, zs1 = zb + 0.1f + 0.06f + 0.4f * D;
    with(k, Xform(), MAT_STEEL, FEAT_THREAD, make_float2(0.0018f, 0.0f), [&] { k.roundedCylinder(0.0065f, zs0 - 0.002f, zs1 + 0.012f, 0.0012f, false, true); });
    with(k, Xform::at(f3(0, 0, zs1 - 0.01f)), k.mat, FEAT_NONE, make_float2(0, 0), [&] {
        handwheel(k, 0.5f * D - 0.008f, 0.0085f, 4, 0.016f, wheelMat, wheelMat);
    });
}

void gauge(Kit& k, float rc, float d) {
    // the case: dark paint, rounded back
    with(k, Xform(), MAT_PAINT_DARK, FEAT_NONE, make_float2(0, 0), [&] { k.roundedCylinder(rc, -d, -0.002f, 0.006f, true, false); });
    // the bezel ring (polished brass), a rounded profile
    with(k, Xform(), MAT_BRASS, FEAT_NONE, make_float2(0, 0), [&] {
        k.lathe({{rc * 0.86f, 0.006f}, {rc * 0.9f, 0.0085f}, {rc * 0.97f, 0.008f}, {rc * 1.03f, 0.004f}, {rc * 1.035f, -0.004f}, {rc * 0.995f, -0.006f}});
    });
    with(k, Xform(), MAT_DIAL, FEAT_NONE, make_float2(rc, 0), [&] { k.ring(0.0f, rc * 0.87f, 0.0f, true); });
    // the needle and its cap
    with(k, Xform::at(f3(0.32f * rc, 0, 0.0025f)) * Xform::rotZ(0.6f), MAT_PAINT_DARK, FEAT_NONE, make_float2(0, 0),
         [&] { k.roundedBox(f3(0.5f * rc * 0.8f, 0.0012f, 0.0006f), 0.0005f); });
    with(k, Xform::at(f3(0, 0, 0.0018f)), MAT_PAINT_DARK, FEAT_NONE, make_float2(0, 0), [&] { k.dome(0.0045f, 0.003f); });
    // the glass: a very flat dome
    with(k, Xform::at(f3(0, 0, 0.0055f)), MAT_GLASS, FEAT_NONE, make_float2(0, 0), [&] { k.dome(rc * 0.88f, 0.0025f); });
    // the stem from below (the gauge line's fitting)
    with(k, Xform::frame(f3(0, -rc * 0.95f, -0.5f * d), f3(0, -1, 0), f3(0, 0, 1)), MAT_BRASS, FEAT_NONE, make_float2(0, 0), [&] {
        k.roundedCylinder(0.007f, 0.0f, 0.03f, 0.0015f);
        k.hexPrism(0.019f, 0.03f, 0.012f, 0.001f);
    });
}

void hullWall(Kit& k, float r, float a0, float a1, float z0, float z1) {
    with(k, Xform(), MAT_PAINT_HULL, FEAT_PLATE, make_float2(0, 0), [&] { k.cylinderWall(r, a0, a1, z0, z1, true); });
}

void ringFrame(Kit& k, float r, float a0, float a1, float z, float hw, float tw, float bf, float tf) {
    // the T in the (radius, z) plane, counter-clockwise: web from the plating inwards, the flange on its inner end
    const float rw = r + 0.002f;   // into the plating a little: the weld hides the joint
    std::vector<float2> poly = {{rw, z - 0.5f * tw},
                                {r - hw + tf, z - 0.5f * tw},
                                {r - hw + tf, z - 0.5f * bf},
                                {r - hw, z - 0.5f * bf},
                                {r - hw, z + 0.5f * bf},
                                {r - hw + tf, z + 0.5f * bf},
                                {r - hw + tf, z + 0.5f * tw},
                                {rw, z + 0.5f * tw}};
    // walk it counter-clockwise in (r, z) with r to the right: reverse if needed
    float area = 0.0f;
    for (size_t i = 0; i < poly.size(); ++i) {
        const float2 a = poly[i], b = poly[(i + 1) % poly.size()];
        area += a.x * b.y - b.x * a.y;
    }
    if (area < 0.0f) std::reverse(poly.begin(), poly.end());
    std::vector<float2> out = Kit::roundOutline(poly, 0.003f, 3);
    out.push_back(out.front());
    with(k, Xform(), MAT_PAINT_HULL, FEAT_PLATE, make_float2(0, 0), [&] { k.lathe(out, a0, a1); });
    // the weld fillets along both sides of the web at the plating: a convex bead, 6 mm legs, round the corner
    for (int side = 0; side < 2; ++side) {
        const float zs = side ? z + 0.5f * tw : z - 0.5f * tw;
        const float sg = side ? 1.0f : -1.0f;
        std::vector<float2> pr;   // from the web to the plating
        for (int i = 0; i <= 6; ++i) {
            const float th = 0.5f * kPi * float(i) / 6;
            pr.push_back({r - 0.006f * std::cos(th), zs + sg * 0.006f * std::sin(th)});
        }
        if (side) std::reverse(pr.begin(), pr.end());   // walked so the outward normal faces away from the corner
        with(k, Xform(), MAT_PAINT_HULL, FEAT_WELD, make_float2(0.006f, 0.0f), [&] { k.lathe(pr, a0, a1); });
    }
}

void deckPlate(Kit& k, float w, float l, float t) {
    with(k, Xform::at(f3(0, -0.5f * t - 0.0005f, 0)), MAT_DECK, FEAT_NONE, make_float2(0, 0), [&] { k.roundedBox(f3(0.5f * w, 0.5f * t, 0.5f * l), 0.002f); });
    with(k, Xform::rotX(-0.5f * kPi), MAT_DECK, FEAT_TREAD, make_float2(0.03f, 0.0f), [&] { k.quad(w - 0.004f, l - 0.004f); });
}

}   // namespace dz
