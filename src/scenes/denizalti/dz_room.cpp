#include "scenes/denizalti/dz_room.h"
#include "scenes/denizalti/dz_decal.h"
#include "scenes/denizalti/dz_equip.h"
#include "scenes/denizalti/dz_parts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dz {

using namespace room;

namespace {

const float kPi = 3.14159265358979f;
float3 f3(float x, float y, float z) { return make_float3(x, y, z); }

template <class F>
void with(Kit& k, const Xform& t, uint32_t mat, uint32_t feat, F f) {
    const Xform t0 = k.t;
    const uint32_t m0 = k.mat, f0 = k.feat;
    const float2 p0 = k.fp;
    k.t = t;
    k.mat = mat;
    k.feat = feat;
    k.fp = make_float2(0, 0);
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
// a point on the cork lining at angle a (0 = starboard, pi/2 = up) and z, moved inwards by `in`
float3 onHull(float a, float z, float in = 0.0f) {
    const float r = kRCork - in;
    return f3(r * std::cos(a), kAxisY + r * std::sin(a), z);
}
// the angle on the lining at height y on a side (side +1 starboard, -1 port)
float hullAngleAt(float y, int side) {
    const float s = std::clamp((y - kAxisY) / kRCork, -1.0f, 1.0f);
    const float a = std::asin(s);
    return side > 0 ? a : kPi - a;
}
// the lining's x at height y (inner face), side +1 / -1
float hullX(float y, int side, float in = 0.0f) {
    const float r = kRCork - in;
    const float dy = y - kAxisY;
    return side * std::sqrt(std::max(0.0f, r * r - dy * dy));
}

// ---------------------------------------------------------------------------------------------------
// the shell

// a bulkhead at z with the door's oval opening centred at (cx, cy), half sizes a x b; facing +z (the after one, its
// room side forward) or -z (the forward one)
void bulkhead(Kit& k, float z, bool facePlus, float cx, float cy, float a, float b) {
    const int nu = 360, nv = 110;
    const float R = kRPlate;
    with(k, Xform(), MAT_CREAM, FEAT_PLATE, [&] {
        k.grid(nu, nv, [&](int i, int j) {
            Kit::GridPt g;
            const int ii = facePlus ? nu - i : i;   // the winding faces the room
            const float th = 2.0f * kPi * float(ii % nu) / nu;
            const float c = std::cos(th), s = std::sin(th);
            const float ro = std::pow(std::pow(std::fabs(c) / a, 4.0f) + std::pow(std::fabs(s) / b, 4.0f), -0.25f);
            // the ray to the hull's circle and to the deck
            const float ox = cx, oy = cy - kAxisY;
            const float B = c * ox + s * oy, C = ox * ox + oy * oy - R * R;
            float tb = -B + std::sqrt(std::max(0.0f, B * B - C));
            if (s < -1e-6f) tb = std::min(tb, -cy / s);
            const float t = float(j) / nv;
            // spacing finer near the door (its rim is looked at), coarser outwards
            const float tt = t * t * 0.6f + t * 0.4f;
            const float r = ro + (tb - ro) * tt;
            g.p = f3(cx + c * r, cy + s * r, z);
            g.n = f3(0, 0, facePlus ? 1.0f : -1.0f);
            g.s = make_float2(g.p.x, g.p.y);
            return g;
        });
    });
    const float dir = facePlus ? 1.0f : -1.0f;
    // the door's cast frame (coaming): an oval ring standing 5 cm out of the plate on both sides, 6 cm wide
    const std::vector<float2> inner = Kit::superellipse(a, b, 4.0f, 120);
    const std::vector<float2> outer = Kit::superellipse(a + 0.06f, b + 0.06f, 4.0f, 120);
    with(k, Xform::at(f3(cx, cy, z)) * (facePlus ? Xform() : Xform::rotY(kPi)), MAT_CREAM, FEAT_CAST, [&] {
        const int n = int(inner.size());
        // the inside of the opening, the outside rim, the front and back faces (rounded with small rows)
        auto ring = [&](const std::vector<float2>& o, float z0, float z1, bool inward) {
            k.grid(n, 4, [&](int i, int j) {
                Kit::GridPt g;
                const int kk = (inward ? n - i : i) % n;
                const float2 q = o[kk];
                const float2 qa = o[(kk + n - 1) % n], qb = o[(kk + 1) % n];
                float3 nn = normalize(f3(qb.y - qa.y, -(qb.x - qa.x), 0));
                if (inward) nn = -nn;
                g.p = f3(q.x, q.y, z0 + (z1 - z0) * float(j) / 4);
                g.n = nn;
                g.s = make_float2(float(i) * 0.02f, g.p.z);
                return g;
            });
        };
        ring(inner, -0.05f, 0.05f, true);
        ring(outer, -0.05f, 0.05f, false);
        for (int side = 0; side < 2; ++side) {
            const float zz = side ? 0.05f : -0.05f;
            k.grid(n, 1, [&](int i, int j) {
                Kit::GridPt g;
                const int kk = (side ? i : n - i) % n;
                g.p = j ? f3(outer[kk].x, outer[kk].y, zz) : f3(inner[kk].x, inner[kk].y, zz);
                g.n = f3(0, 0, side ? 1.0f : -1.0f);
                g.s = make_float2(g.p.x, g.p.y);
                return g;
            });
        }
    });
    // the door, closed, on the room side: an oval leaf 4 cm thick, its window, the dogs and the operating gear
    const Xform room = Xform::at(f3(cx, cy, z + dir * 0.05f)) * (facePlus ? Xform() : Xform::rotY(kPi));
    with(k, room, MAT_CREAM, FEAT_PLATE, [&] { k.slab(Kit::superellipse(a + 0.035f, b + 0.035f, 4.0f, 96), 0.04f, 0.008f); });
    // the window: a brass ring and dark glass
    with(k, room * Xform::at(f3(0, b * 0.55f, 0.04f)), MAT_BRASS, FEAT_NONE, [&] {
        k.lathe({{0.055f, -0.002f}, {0.07f, 0.0f}, {0.072f, 0.008f}, {0.064f, 0.012f}, {0.055f, 0.01f}, {0.052f, 0.004f}});
    });
    with(k, room * Xform::at(f3(0, b * 0.55f, 0.044f)), MAT_BLACK_PLATE, FEAT_NONE, [&] { k.ring(0.0f, 0.056f, 0.0f, true); });
    // the operating gear: a hub and a two-knobbed lever across
    with(k, room * Xform::at(f3(0, -0.02f, 0.04f)), MAT_STEEL, FEAT_CAST, [&] {
        k.roundedCylinder(0.05f, 0.0f, 0.035f, 0.006f);
        k.t = k.t * Xform::at(f3(0, 0, 0.045f)) * Xform::rotZ(0.4f) * Xform::rotY(0.5f * kPi);
        k.roundedCylinder(0.012f, -0.17f, 0.17f, 0.004f);
    });
    for (int s = -1; s <= 1; s += 2) {
        const float aa = 0.4f;
        with(k, room * Xform::at(f3(s * 0.17f * std::cos(aa), s * 0.17f * std::sin(aa), 0.085f)), MAT_STEEL, FEAT_NONE, [&] { k.sphere(0.022f); });
    }
    // eight dogs round the leaf: a wedge lever on a spindle at the coaming
    for (int d = 0; d < 8; ++d) {
        const float th = (d + 0.5f) * 2.0f * kPi / 8.0f;
        const float c = std::cos(th), s = std::sin(th);
        const float ro = std::pow(std::pow(std::fabs(c) / (a + 0.06f), 4.0f) + std::pow(std::fabs(s) / (b + 0.06f), 4.0f), -0.25f);
        const Xform at = room * Xform::at(f3(c * (ro + 0.01f), s * (ro + 0.01f), 0.0f)) * Xform::rotZ(th);
        with(k, at, MAT_STEEL, FEAT_CAST, [&] {
            k.roundedCylinder(0.016f, -0.04f, 0.03f, 0.004f);
            k.t = k.t * Xform::at(f3(-0.03f, 0, 0.035f));
            k.roundedBox(f3(0.045f, 0.012f, 0.008f), 0.004f);
        });
    }
    // stiffeners: vertical flat bars on the room side, port and starboard of the door
    for (float x : {-1.6f, -0.7f, 0.2f, 1.9f}) {
        if (std::fabs(x - cx) < a + 0.15f) continue;
        const float top = kAxisY + std::sqrt(std::max(0.0f, R * R - x * x)) - 0.05f;
        with(k, Xform::at(f3(x, 0.0f, z + dir * 0.0f)) * (facePlus ? Xform() : Xform::rotY(kPi)), MAT_CREAM, FEAT_PLATE, [&] {
            k.t = k.t * Xform::at(f3(0, 0.5f * top, 0.05f));
            k.roundedBox(f3(0.006f, 0.5f * top, 0.05f), 0.002f);
        });
    }
}

void frames(Kit& k) {
    const float a0 = hullAngleAt(0.0f, 1), a1 = hullAngleAt(0.0f, -1);
    for (int f = 48; f <= 58; ++f) {
        const float z = frameZ(float(f));
        const float r = kRPlate, hw = 0.15f, tw = 0.012f, bf = 0.10f, tf = 0.014f;
        std::vector<float2> poly = {{r, z - 0.5f * tw},          {r - hw + tf, z - 0.5f * tw}, {r - hw + tf, z - 0.5f * bf},
                                    {r - hw, z - 0.5f * bf},     {r - hw, z + 0.5f * bf},      {r - hw + tf, z + 0.5f * bf},
                                    {r - hw + tf, z + 0.5f * tw}, {r, z + 0.5f * tw}};
        float area = 0.0f;
        for (size_t i = 0; i < poly.size(); ++i) {
            const float2 p = poly[i], q = poly[(i + 1) % poly.size()];
            area += p.x * q.y - q.x * p.y;
        }
        if (area < 0.0f) std::reverse(poly.begin(), poly.end());
        std::vector<float2> out = Kit::roundOutline(poly, 0.003f, 3);
        out.push_back(out.front());
        with(k, Xform::at(f3(0, kAxisY, 0)), MAT_CREAM, FEAT_PLATE, [&] { k.lathe(out, a0, a1); });
    }
}

void deck(Kit& k) {
    const float xs[6] = {-2.37f, -1.42f, -0.47f, 0.47f, 1.42f, 2.37f};
    for (int c = 0; c < 5; ++c)
        for (int r = 0; r < 11; ++r) {
            const float z0 = std::max(0.0f, r * kFrame - 0.06f), z1 = std::min(kLength, (r + 1) * kFrame - 0.06f);
            const float x0 = xs[c], x1 = xs[c + 1];
            const float3 cen = f3(0.5f * (x0 + x1), -0.003f, 0.5f * (z0 + z1));
            with(k, Xform::at(cen), MAT_DECK_GREEN, FEAT_PLATE, [&] { k.roundedBox(f3(0.5f * (x1 - x0) - 0.0015f, 0.003f, 0.5f * (z1 - z0) - 0.0015f), 0.0012f); });
            // four countersunk screws, painted over
            for (int sx = 0; sx < 2; ++sx)
                for (int sz = 0; sz < 2; ++sz) {
                    const float3 p = f3(sx ? x1 - 0.035f : x0 + 0.035f, 0.0f, sz ? z1 - 0.035f : z0 + 0.035f);
                    with(k, Xform::at(p) * Xform::rotX(-0.5f * kPi), MAT_DECK_GREEN, FEAT_NONE, [&] { k.dome(0.007f, 0.0012f); });
                }
        }
}

// ---------------------------------------------------------------------------------------------------
// the overhead: cable runs, the ventilation duct, piping

void cableRun(Kit& k, float a, float in, int count, float r, float z0, float z1) {
    // count cables side by side along z, laid on the lining at angle a, the bundle tangent to the hull
    const float3 t = f3(-std::sin(a), std::cos(a), 0);   // across the bundle
    const float3 inw = f3(-std::cos(a), -std::sin(a), 0);  // inwards
    const float3 base = onHull(a, 0.0f, in);
    const float w = count * 2.1f * r;
    for (int c = 0; c < count; ++c) {
        const float off = -0.5f * w + (c + 0.5f) * 2.1f * r;
        const float rr = r * (0.85f + 0.3f * float((c * 7) % 5) / 4.0f);
        const float3 o = base + t * off + inw * (r - rr);
        with(k, Xform::frame(f3(o.x, o.y, 0.0f), f3(0, 0, 1), inw * -1.0f), MAT_CREAM, FEAT_BRAID, [&] {
            k.fp = make_float2(0.0035f, 0.0f);
            k.tube(rr, z0, z1, 0.37f * c);
        });
    }
    // hangers: a strap round the bundle every 0.45 m, bolted to the frames' flanges
    for (float z = z0 + 0.3f; z < z1 - 0.1f; z += 0.4572f) {
        const float3 o = base + inw * (2.0f * r + 0.004f);
        const Xform at = axes(f3(o.x, o.y, z), t, inw * -1.0f, f3(0, 0, 1));
        with(k, at, MAT_CREAM, FEAT_NONE, [&] { k.roundedBox(f3(0.5f * w + 0.012f, 0.0025f, 0.012f), 0.0012f); });
    }
}

void overhead(Kit& k) {
    // cable runs across the overhead (white-painted braided cable, strapped)
    const float maxEdge0 = k.maxEdge;
    k.maxEdge = 0.03f;   // long straight runs: tessellation divides along them
    cableRun(k, 0.5f * kPi + 0.32f, 0.03f, 9, 0.011f, 0.03f, kLength - 0.03f);
    cableRun(k, 0.5f * kPi + 0.12f, 0.03f, 7, 0.009f, 0.03f, kLength - 0.03f);
    cableRun(k, 0.5f * kPi - 0.12f, 0.03f, 8, 0.012f, 0.03f, kLength - 0.03f);
    cableRun(k, 0.5f * kPi - 0.36f, 0.03f, 6, 0.008f, 0.03f, kLength - 0.03f);
    cableRun(k, 0.5f * kPi + 0.62f, 0.03f, 5, 0.010f, 0.03f, kLength - 0.03f);
    cableRun(k, 0.5f * kPi - 0.66f, 0.03f, 5, 0.010f, 0.03f, kLength - 0.03f);
    // the ventilation supply duct, starboard of the centre line, flanged every 1.5 m, on hangers
    const float dx = 0.72f, dy = 2.12f, dr = 0.12f;
    with(k, Xform(), MAT_CREAM, FEAT_PLATE, [&] { pipeRun(k, {f3(dx, dy, 0.05f), f3(dx, dy, kLength - 0.05f)}, dr, 0.3f); });
    for (float z = 0.6f; z < kLength - 0.3f; z += 1.52f) {
        with(k, Xform::frame(f3(dx, dy, z), f3(0, 0, 1), f3(0, 1, 0)), MAT_CREAM, FEAT_CAST, [&] { k.roundedCylinder(dr + 0.018f, -0.008f, 0.008f, 0.003f); });
        // the hanger: two flat straps up to the overhead
        const float top = kAxisY + std::sqrt(kRCork * kRCork - dx * dx) - 0.02f;
        for (int s = -1; s <= 1; s += 2) {
            const float x = dx + s * (dr + 0.01f);
            const float y0 = dy, y1 = kAxisY + std::sqrt(kRCork * kRCork - x * x) - 0.01f;
            with(k, Xform::at(f3(x, 0.5f * (y0 + y1), z + 0.12f)), MAT_CREAM, FEAT_NONE, [&] { k.roundedBox(f3(0.003f, 0.5f * (y1 - y0), 0.015f), 0.0012f); });
        }
        (void)top;
    }
    // piping along the overhead: hydraulic (white), air (copper), a 3" trim line
    struct P { float a, in, r; uint32_t mat; };
    const P pipes[] = {{0.5f * kPi + 0.85f, 0.06f, 0.0127f, MAT_CREAM}, {0.5f * kPi + 0.9f, 0.05f, 0.0095f, MAT_CREAM},
                       {0.5f * kPi - 0.88f, 0.05f, 0.0127f, MAT_BRASS}, {0.5f * kPi - 0.94f, 0.05f, 0.0095f, MAT_BRASS},
                       {0.5f * kPi + 1.05f, 0.07f, 0.038f, MAT_CREAM}, {0.5f * kPi - 1.12f, 0.06f, 0.016f, MAT_CREAM}};
    for (const P& p : pipes) {
        const float3 o = onHull(p.a, 0.0f, p.in + p.r);
        with(k, Xform(), p.mat, FEAT_NONE, [&] { pipeRun(k, {f3(o.x, o.y, 0.04f), f3(o.x, o.y, kLength - 0.04f)}, p.r, 0.1f); });
    }
    k.maxEdge = maxEdge0;
}

// ---------------------------------------------------------------------------------------------------
// lamps

// a steam-tight night lamp: a cast guard ring on a short neck, a red glass globe, four guard bars
void redLamp(Kit& k, std::vector<DzLight>& lights, float3 p) {
    with(k, Xform::at(p), MAT_CREAM, FEAT_CAST, [&] {
        // the base on the overhead and the neck down to the globe's collar (lathe along -y)
        k.t = k.t * Xform::rotX(0.5f * kPi);   // the lathe's z points down
        k.lathe({{0.0f, -0.09f}, {0.075f, -0.09f}, {0.08f, -0.085f}, {0.08f, -0.072f}, {0.05f, -0.065f}, {0.032f, -0.05f},
                 {0.034f, -0.02f}, {0.05f, -0.012f}, {0.052f, 0.0f}, {0.0f, 0.0f}});
    });
    const float rg = 0.055f;
    const float3 c = p + f3(0, -rg - 0.004f, 0);
    with(k, Xform::at(c), MAT_RED_GLASS, FEAT_NONE, [&] { k.sphere(rg); });
    for (int i = 0; i < 4; ++i) {
        const float th = (i + 0.5f) * 0.5f * kPi;
        std::vector<float3> pts;
        for (int s = 0; s <= 8; ++s) {
            const float ph = -0.05f + (kPi * 0.62f) * float(s) / 8;   // from the collar round the globe
            const float rr = rg + 0.012f;
            pts.push_back(c + f3(std::cos(th) * rr * std::sin(ph), rr * std::cos(ph), std::sin(th) * rr * std::sin(ph)));
        }
        with(k, Xform(), MAT_CREAM, FEAT_NONE, [&] { pipeRun(k, pts, 0.0035f, 0.01f); });
    }
    // the light: the globe glows red (a filtered bulb inside)
    lights.push_back(DzLight{c, rg * 0.98f, make_float3(1.0f, 0.02f, 0.006f) * (0.55f / 0.2126f), 0.0f});
}

// the chart lamp: a short arm off the overhead, an enamelled cone shade, a bulb inside
void chartLamp(Kit& k, std::vector<DzLight>& lights, float3 mount, float3 bulb) {
    const float3 d = bulb + f3(0, 0.07f, 0) - mount;
    with(k, Xform(), MAT_CREAM, FEAT_NONE, [&] { pipeRun(k, {mount, mount + f3(0, -0.15f, 0), bulb + f3(0, 0.16f, 0)}, 0.008f, 0.04f); });
    (void)d;
    // the shade: a lathe about -y (lathe z down)
    with(k, Xform::at(bulb + f3(0, 0.16f, 0)) * Xform::rotX(0.5f * kPi), MAT_LAMP_SHADE, FEAT_NONE, [&] {
        k.lathe({{0.0f, -0.01f}, {0.022f, -0.01f}, {0.025f, 0.0f}, {0.03f, 0.05f}, {0.075f, 0.13f}, {0.105f, 0.185f}, {0.108f, 0.19f},
                 {0.104f, 0.192f}, {0.1f, 0.187f}, {0.07f, 0.135f}, {0.026f, 0.056f}, {0.02f, 0.01f}, {0.0f, 0.005f}});
    });
    with(k, Xform::at(bulb), MAT_EMISSIVE, FEAT_NONE, [&] { k.sphere(0.024f); });
    const float3 warm = make_float3(1.0f, 0.72f, 0.42f) * (1.0f / 0.758f);
    lights.push_back(DzLight{bulb, 0.025f, warm * 40.0f, 0.0f});
}

}   // namespace

Start buildRoom(Mesh& m, float maxEdge, std::vector<DzLight>& lights) {
    Kit k(m);
    k.maxEdge = maxEdge;
    // the lining: cork under cream enamel, from the deck round the overhead to the deck
    with(k, Xform::at(f3(0, kAxisY, 0)), MAT_CREAM, FEAT_CORK, [&] {
        const float a0 = hullAngleAt(0.0f, 1), a1 = hullAngleAt(0.0f, -1);
        const float maxEdge0 = k.maxEdge;
        k.maxEdge = 0.025f;
        k.cylinderWall(kRCork, a0, a1, 0.0f, kLength, true);
        k.maxEdge = maxEdge0;
    });
    frames(k);
    // the bulkheads: both doors 51 x 97 cm, the sill 48 cm up, 1.1 m to starboard
    bulkhead(k, 0.0f, true, 1.1f, 0.48f + 0.485f, 0.255f, 0.485f);
    bulkhead(k, kLength, false, 1.1f, 0.48f + 0.485f, 0.255f, 0.485f);
    deck(k);
    overhead(k);
    // stencils: the forward door's order in Turkish (the boat's own hand, 1970s-2000), frame numbers on the frames
    if (activeDecals) {
        const int id = activeDecals->plate("SU GEÇMEZ KAPI\nSEYİRDE KAPALI TUTULACAK", 0.42f, 0.11f, PLATE_STENCIL);
        with(k, Xform::at(f3(1.1f, 1.68f, kLength - 0.0005f)) * Xform::rotY(kPi), MAT_CREAM, FEAT_LABEL, [&] {
            k.fp = make_float2(float(id), 0.0f);
            k.quad(0.42f, 0.11f);
        });
        for (int f = 48; f <= 58; f += 2) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", f);
            const int fid = activeDecals->plate(buf, 0.07f, 0.045f, PLATE_STENCIL);
            for (int side = -1; side <= 1; side += 2) {
                // on the frame's after face of the web, a hand's height above the deck
                const float y = 0.42f;
                const float x = side * (std::sqrt(kRCork * kRCork - (y - kAxisY) * (y - kAxisY)) - 0.075f);
                with(k, Xform::at(f3(x, y, frameZ(float(f)) - 0.0065f)) * Xform::rotY(kPi), MAT_CREAM, FEAT_LABEL, [&] {
                    k.fp = make_float2(float(fid), 0.0f);
                    k.quad(0.07f, 0.045f);
                });
            }
        }
    }
    float3 chartTop = f3(-0.4f, 0.98f, frameZ(50.25f));
    std::vector<DzLight> minor;
    buildEquipment(k, chartTop, minor);
    // the night lamps over the port side's stations and the passage
    redLamp(k, lights, onHull(0.5f * kPi + 0.42f, frameZ(52.0f), 0.08f));
    redLamp(k, lights, onHull(0.5f * kPi + 0.42f, frameZ(49.4f), 0.08f));
    redLamp(k, lights, onHull(0.5f * kPi - 0.5f, frameZ(53.0f), 0.08f));
    chartLamp(k, lights, onHull(0.5f * kPi + 0.16f, chartTop.z, 0.12f), chartTop + f3(0.0f, 0.62f, 0.0f));
    // the key lamps first (their highlights are drawn, dz_render), then the small glows
    lights.insert(lights.end(), minor.begin(), minor.end());
    // built with x to starboard: mirrored so starboard is on the right of someone facing the bow (dz_room.h)
    mirrorX(m);
    for (DzLight& l : lights) l.p.x = -l.p.x;
    finishMesh(m);
    // standing on the port side by the trim manifold, looking forward along the diving station to the Christmas tree
    return Start{f3(0.95f, 1.62f, 2.45f), -0.2f, -0.06f};
}

}   // namespace dz
