// Scene 8's equipment: the stations of the control room (REFERANS §3), each at its frame.
#include "scenes/denizalti/dz_equip.h"
#include "scenes/denizalti/dz_decal.h"
#include "scenes/denizalti/dz_parts.h"

#include <algorithm>
#include <cmath>

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
float hullX(float y, int side, float in = 0.0f) {
    const float r = kRCork - in;
    const float dy = y - kAxisY;
    return side * std::sqrt(std::max(0.0f, r * r - dy * dy));
}
// a frame facing inboard from a side (side -1 port: facing +x), z along the room, y up: x = along z, y = up, z = out
Xform facing(float3 o, int side) {
    const float s = float(side);
    return axes(o, f3(0, 0, s), f3(0, 1, 0), f3(-s, 0, 0));
}
// a box with rounded edges given its centre and half sizes in the frame t
void box(Kit& k, const Xform& t, float3 c, float3 h, float e, uint32_t mat, uint32_t feat = FEAT_PLATE) {
    with(k, t * Xform::at(c), mat, feat, [&] { k.roundedBox(h, e); });
}
// a round gauge in frame t at local point c, facing the frame's +z
void gaugeAt(Kit& k, const Xform& t, float3 c, float rc, float depth, uint32_t bezel = MAT_BRASS, DialKind kind = DIAL_WHITE) {
    with(k, t * Xform::at(c), MAT_PAINT_DARK, FEAT_NONE, [&] {
        k.roundedCylinder(rc, -depth, -0.002f, std::min(0.006f, rc * 0.1f), true, false);
        k.mat = bezel;
        k.lathe({{rc * 0.86f, 0.006f}, {rc * 0.9f, 0.0085f}, {rc * 0.97f, 0.008f}, {rc * 1.03f, 0.004f}, {rc * 1.035f, -0.004f}, {rc * 0.995f, -0.006f}});
        k.mat = MAT_DIAL;
        k.feat = FEAT_DIAL;
        k.fp = make_float2(activeDecals ? float(activeDecals->dial(kind)) : 0.0f, rc * 0.87f);
        k.ring(0.0f, rc * 0.87f, 0.0f, true);
        k.feat = FEAT_NONE;
        k.mat = MAT_PAINT_DARK;
        const Xform t0 = k.t;
        k.t = t0 * Xform::at(f3(0.3f * rc, 0.08f * rc, 0.0026f)) * Xform::rotZ(0.35f);
        k.roundedBox(f3(0.42f * rc, 0.0012f + rc * 0.008f, 0.0006f), 0.0005f);
        k.t = t0 * Xform::at(f3(0, 0, 0.0018f));
        k.dome(std::max(0.0045f, rc * 0.05f), 0.003f);
        k.t = t0;
    });
}
// a label: a thin plate (black or brass) and its engraved face, or a stencil straight on the surface; in frame t at c
void plateAt(Kit& k, const Xform& t, float3 c, float w, float h, const std::string& text, int style) {
    const int id = activeDecals ? activeDecals->plate(text, w, h, style) : 0;
    const uint32_t mat = style == PLATE_BRASS ? MAT_BRASS : MAT_BLACK_PLATE;
    if (style != PLATE_STENCIL) {
        with(k, t * Xform::at(c), mat, FEAT_NONE, [&] { k.slab(Kit::roundOutline({{-0.5f * w, -0.5f * h}, {0.5f * w, -0.5f * h}, {0.5f * w, 0.5f * h}, {-0.5f * w, 0.5f * h}}, std::min(w, h) * 0.08f, 3), 0.0015f, 0.0004f); });
    }
    with(k, t * Xform::at(c + f3(0, 0, style == PLATE_STENCIL ? 0.0006f : 0.0016f)), style == PLATE_STENCIL ? MAT_CREAM : mat, FEAT_LABEL, [&] {
        k.fp = make_float2(float(id), 0.0f);
        k.quad(w * 0.985f, h * 0.985f);
    });
}

// a big handwheel facing +z of frame t: rim radius R, n straight spokes, a grip handle on the rim
void bigWheel(Kit& k, const Xform& t, float R, float r, int n, uint32_t rimMat, uint32_t spokeMat) {
    with(k, t, rimMat, FEAT_CAST, [&] { k.torus(R, r); });
    for (int i = 0; i < n; ++i) {
        const float a = (i + 0.5f) * 2.0f * kPi / n;
        const float3 dir = f3(std::cos(a), std::sin(a), 0);
        const float3 p0 = dir * 0.05f + f3(0, 0, 0.03f), p1 = dir * (R - 0.6f * r);
        with(k, t * Xform::frame(p0, p1 - p0, f3(0, 0, 1)), spokeMat, FEAT_CAST, [&] { k.roundedCylinder(0.55f * r, 0.0f, length(p1 - p0), 0.2f * r, false, false); });
    }
    with(k, t, spokeMat, FEAT_CAST, [&] {
        k.roundedCylinder(0.065f, -0.02f, 0.05f, 0.008f);
        k.t = k.t * Xform::at(f3(0, 0, 0.05f));
        k.hexPrism(0.05f, 0.0f, 0.02f, 0.002f);
    });
    // the spinner grip on the rim
    with(k, t * Xform::at(f3(0, R, 0)), MAT_WOOD, FEAT_NONE, [&] { k.roundedCylinder(0.018f, 0.0f, 0.11f, 0.008f); });
}

// ---------------------------------------------------------------------------------------------------
// port side

void divingStation(Kit& k) {
    const int side = -1;
    const float zS = frameZ(52.8f), zB = frameZ(50.9f);   // stern and bow plane wheels
    const float z0 = frameZ(53.3f), z1 = frameZ(49.8f);
    // the panel: a flat board leaning back with the hull, from 0.55 m to 2.05 m, its face 0.42 m inboard of the hull at
    // the bottom
    const float yb = 0.55f, yt = 2.05f;
    const float xb = hullX(yb, side, 0.30f), xt = hullX(yt, side, 0.08f);
    const float3 pb = f3(xb, yb, 0.5f * (z0 + z1)), pt = f3(xt, yt, 0.5f * (z0 + z1));
    const float3 up = normalize(pt - pb);
    const float3 along = f3(0, 0, -1);                // the board's x runs aft (right-handed with z into the room)
    const float3 out = normalize(cross(along, up));   // into the room (+x on port)
    const float hh = 0.5f * length(pt - pb);
    const Xform panel = axes(0.5f * (pb + pt), along, up, out);
    box(k, panel, f3(0, 0, -0.03f), f3(0.5f * (z1 - z0), hh, 0.03f), 0.006f, MAT_CREAM);
    // the board's frame of flat bar and its bolts
    for (int s = -1; s <= 1; s += 2) box(k, panel, f3(s * (0.5f * (z1 - z0) - 0.02f), 0, 0.006f), f3(0.02f, hh, 0.006f), 0.002f, MAT_CREAM);
    // depth gauges: two 16" shallow gauges, the 8.5" deep gauge between them, all on the board's upper half
    const float zc = 0.5f * (z0 + z1);
    gaugeAt(k, panel, f3(zc - zS, 0.32f, 0.01f), 0.203f, 0.09f, MAT_PAINT_DARK, DIAL_DEPTH16);
    gaugeAt(k, panel, f3(zc - zB, 0.32f, 0.01f), 0.203f, 0.09f, MAT_PAINT_DARK, DIAL_DEPTH16);
    gaugeAt(k, panel, f3(zc - 0.5f * (zS + zB), 0.42f, 0.01f), 0.108f, 0.07f, MAT_BRASS, DIAL_DEPTH_DEEP);
    plateAt(k, panel, f3(zc - zS, 0.32f + 0.25f, 0.004f), 0.2f, 0.035f, "STERN PLANES", PLATE_BRASS);
    plateAt(k, panel, f3(zc - zB, 0.32f + 0.25f, 0.004f), 0.2f, 0.035f, "BOW PLANES", PLATE_BRASS);
    plateAt(k, panel, f3(zc - 0.5f * (zS + zB), 0.42f + 0.14f, 0.004f), 0.17f, 0.03f, "DEEP DEPTH GAUGE", PLATE_BLACK);
    // plane angle indicators (chrome boxes, cream faces) beside each shallow gauge, the rudder angle indicator between
    for (float zz : {zS, zB}) {
        box(k, panel, f3(zc - zz + (zz == zS ? 0.32f : -0.32f), 0.2f, 0.05f), f3(0.07f, 0.11f, 0.045f), 0.008f, MAT_CHROME, FEAT_NONE);
        with(k, panel * Xform::at(f3(zc - zz + (zz == zS ? 0.32f : -0.32f), 0.2f, 0.096f)), MAT_DIAL, FEAT_NONE, [&] { k.roundedBox(f3(0.055f, 0.09f, 0.001f), 0.002f); });
    }
    gaugeAt(k, panel, f3(zc - 0.5f * (zS + zB), 0.08f, 0.01f), 0.075f, 0.06f, MAT_CHROME, DIAL_COMPASS);
    plateAt(k, panel, f3(zc - 0.5f * (zS + zB), -0.03f, 0.004f), 0.15f, 0.026f, "RUDDER ANGLE", PLATE_BLACK);
    // the bubble inclinometers: a curved glass tube in a brass case, high in the middle
    with(k, panel * Xform::at(f3(zc - 0.5f * (zS + zB), 0.66f, 0.03f)), MAT_BRASS, FEAT_NONE, [&] { k.roundedBox(f3(0.3f, 0.05f, 0.03f), 0.01f); });
    with(k, panel * Xform::at(f3(zc - 0.5f * (zS + zB), 0.66f, 0.062f)), MAT_DIAL, FEAT_NONE, [&] { k.roundedBox(f3(0.26f, 0.025f, 0.002f), 0.003f); });
    // the two plane wheels: 70 cm, eight straight spokes, bare steel, on shaft housings out of the board
    for (float zz : {zS, zB}) {
        const float3 hub = f3(xb + 0.62f, 0.98f, zz);
        // the shaft housing from the board to the wheel
        with(k, Xform::frame(f3(hullX(0.98f, side, 0.30f) + 0.02f, 0.98f, zz), f3(1, 0, 0), f3(0, 1, 0)), MAT_CREAM, FEAT_CAST, [&] {
            k.roundedCylinder(0.07f, 0.0f, 0.42f, 0.01f);
            k.t = k.t * Xform::at(f3(0, 0, 0.42f));
            k.roundedCylinder(0.045f, 0.0f, 0.12f, 0.006f);
        });
        bigWheel(k, Xform::frame(hub, f3(1, 0, 0), f3(0, 1, 0)), 0.35f, 0.018f, 8, MAT_STEEL, MAT_STEEL);
        // the change valve lever beside it, with its chrome plate
        box(k, panel, f3(zc - zz - 0.22f, -0.45f, 0.03f), f3(0.06f, 0.08f, 0.004f), 0.003f, MAT_CHROME, FEAT_NONE);
        with(k, panel * Xform::at(f3(zc - zz - 0.22f, -0.45f, 0.04f)), MAT_STEEL, FEAT_NONE, [&] {
            k.roundedCylinder(0.012f, 0.0f, 0.03f, 0.003f);
            k.t = k.t * Xform::at(f3(0, 0, 0.03f)) * Xform::rotX(-0.9f);
            k.roundedCylinder(0.007f, 0.0f, 0.12f, 0.003f);
            k.t = k.t * Xform::at(f3(0, 0, 0.125f));
            k.sphere(0.016f);
        });
        // the planesman's stool: a steel post, a turquoise vinyl seat
        const float3 st = f3(xb + 1.25f, 0.0f, zz);
        with(k, Xform::frame(st, f3(0, 1, 0), f3(1, 0, 0)), MAT_GREY, FEAT_NONE, [&] {
            k.roundedCylinder(0.16f, 0.0f, 0.015f, 0.006f);
            k.roundedCylinder(0.025f, 0.0f, 0.5f, 0.004f);
        });
        with(k, Xform::frame(st + f3(0, 0.5f, 0), f3(0, 1, 0), f3(1, 0, 0)), MAT_VINYL, FEAT_NONE, [&] { k.roundedCylinder(0.19f, 0.0f, 0.07f, 0.03f); });
    }
}

void christmasTreeAndManifold(Kit& k, std::vector<DzLight>& minor) {
    const int side = -1;
    const float z0 = frameZ(49.7f), z1 = frameZ(47.6f);
    const float zc = 0.5f * (z0 + z1);
    // the Christmas tree: a grey box leaning with the hull, a grid of red and green lenses behind white rules
    const float y = 1.78f;
    const float x = hullX(y, side, 0.12f);
    const Xform tree = Xform::frame(f3(x, y, zc), f3(0.9f, -0.44f, 0), f3(0, 1, 0));
    const float3 hs = f3(0.36f, 0.42f, 0.06f);
    box(k, tree, f3(0, 0, -0.06f), hs, 0.008f, MAT_GREY, FEAT_NONE);
    box(k, tree, f3(0, 0, 0.002f), f3(hs.x - 0.02f, hs.y - 0.02f, 0.002f), 0.002f, MAT_PAINT_DARK, FEAT_NONE);
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 7; ++c) {
            const bool green = !((r == 2 && c == 5) || (r == 6 && c == 1));   // a green board, two openings still red
            const float3 p = f3(-0.27f + c * 0.09f, -0.33f + r * 0.08f, 0.006f);
            box(k, tree, p, f3(0.025f, 0.014f, 0.004f), 0.002f, green ? MAT_INDICATOR_GREEN : MAT_INDICATOR_RED, FEAT_NONE);
        }
    plateAt(k, tree, f3(0, 0.385f, 0.004f), 0.6f, 0.05f, "INDICATOR FOR HULL OPENINGS\nNAVY YARD, PORTSMOUTH, N.H.", PLATE_BLACK);
    // the board's glow: three small green lights just off the lenses (the panel's green light on the manifold below)
    for (int g = 0; g < 3; ++g) {
        // a soft glow off the board (the lamps sit behind the lenses: they light the room a little, not the panel hot)
        const float3 lp = tree.point(f3(-0.2f + g * 0.2f, -0.05f, 0.16f));
        minor.push_back(DzLight{lp, 0.09f, make_float3(0.02f, 1.0f, 0.18f) * 0.03f, 0.0f});
    }
    // the main vent control manifold: six bronze valve bodies on a cast base, chrome levers with ball ends
    const float yv = 1.02f;
    const float xv = hullX(yv, side, 0.38f);
    box(k, Xform(), f3(xv, yv - 0.08f, zc), f3(0.11f, 0.06f, 0.46f), 0.01f, MAT_CREAM, FEAT_CAST);
    for (int v = 0; v < 6; ++v) {
        const float zz = zc - 0.38f + v * 0.152f;
        with(k, Xform::frame(f3(xv, yv - 0.02f, zz), f3(0, 1, 0), f3(1, 0, 0)), MAT_BRASS, FEAT_CAST, [&] {
            k.lathe({{0.0f, -0.01f}, {0.045f, -0.01f}, {0.05f, 0.0f}, {0.05f, 0.03f}, {0.04f, 0.05f}, {0.03f, 0.08f}, {0.034f, 0.09f}, {0.0f, 0.095f}});
        });
        // the lever: a pivot and a 30 cm arm thrown forward or aft, ball end
        const float tilt = (v % 2) ? 0.22f : -0.15f;
        with(k, Xform::at(f3(xv, yv + 0.075f, zz)) * Xform::rotX(tilt - 0.5f * kPi), MAT_CHROME, FEAT_NONE, [&] {
            k.roundedCylinder(0.011f, 0.0f, 0.3f, 0.004f);
            k.t = k.t * Xform::at(f3(0, 0, 0.31f));
            k.sphere(0.022f);
        });
    }
    // the label strip under the levers, and the manifold's name
    {
        const Xform face = axes(f3(xv + 0.112f, yv - 0.08f, zc), f3(0, 0, -1), f3(0, 1, 0), f3(1, 0, 0));
        plateAt(k, face, f3(0, 0, 0), 0.86f, 0.04f, "SAFE    6C&D    6A&B    VENT 3&5    VENT 1&2    B.B.", PLATE_BLACK);
        plateAt(k, face, f3(0, -0.075f, 0), 0.5f, 0.035f, "MAIN VENT CONTROL MANIFOLD", PLATE_BLACK);
    }
    // the hydraulic supply tank: a grey bolted box with sight glasses, against the forward bulkhead
    const float3 tc = f3(hullX(0.65f, side, 0.30f) + 0.05f, 0.62f, kLength - 0.22f);
    box(k, Xform(), tc, f3(0.22f, 0.6f, 0.12f), 0.012f, MAT_GREY, FEAT_PLATE);
    for (int s = 0; s < 2; ++s)
        with(k, Xform::frame(tc + f3(-0.08f + s * 0.16f, -0.2f, -0.125f), f3(0, 1, 0), f3(1, 0, 0)), MAT_GLASS, FEAT_NONE, [&] { k.tube(0.011f, 0.0f, 0.5f); });
    // the main oil pressure and accumulator gauges above the manifold
    const Xform gb = Xform::frame(f3(hullX(1.45f, side, 0.10f), 1.45f, zc - 0.62f), f3(0.95f, -0.3f, 0), f3(0, 1, 0));
    box(k, gb, f3(0, 0, -0.02f), f3(0.16f, 0.22f, 0.02f), 0.005f, MAT_CREAM, FEAT_PLATE);
    gaugeAt(k, gb, f3(0, 0.1f, 0.01f), 0.065f, 0.05f, MAT_BRASS, DIAL_WHITE);
    gaugeAt(k, gb, f3(0, -0.08f, 0.01f), 0.065f, 0.05f, MAT_BRASS, DIAL_WHITE);
}

void trimManifold(Kit& k) {
    const int side = -1;
    const float z0 = frameZ(54.8f), z1 = frameZ(53.4f), zc = 0.5f * (z0 + z1);
    const float y = 0.92f;
    const float x = hullX(y, side, 0.40f);
    // the two-compartment casting, hip high
    box(k, Xform(), f3(x, y, zc), f3(0.15f, 0.12f, 0.5f * (z1 - z0) - 0.03f), 0.015f, MAT_CREAM, FEAT_CAST);
    // eight suction and eight discharge valves: rising stems, small cast handwheels in two rows
    for (int row = 0; row < 2; ++row)
        for (int v = 0; v < 8; ++v) {
            const float zz = z0 + 0.08f + v * (z1 - z0 - 0.16f) / 7.0f;
            const float yy = y + (row ? 0.06f : -0.06f);
            const Xform t = Xform::frame(f3(x + 0.15f, yy, zz), f3(1, 0, 0), f3(0, 1, 0));
            with(k, t, MAT_CREAM, FEAT_CAST, [&] {
                k.roundedCylinder(0.022f, 0.0f, 0.035f, 0.004f);
                k.mat = MAT_STEEL;
                k.feat = FEAT_THREAD;
                k.fp = make_float2(0.0018f, 0.0f);
                k.roundedCylinder(0.006f, 0.03f, 0.09f + 0.02f * float((v + row) % 3), 0.0015f, false, true);
            });
            const float zw = 0.075f + 0.02f * float((v + row) % 3);
            with(k, t * Xform::at(f3(0, 0, zw)), MAT_RED, FEAT_NONE, [&] { handwheel(k, 0.038f, 0.0055f, 3, 0.012f, MAT_RED, MAT_RED); });
        }
    // the gauge board directly above
    const float yb = 1.55f;
    const Xform gb = Xform::frame(f3(hullX(yb, side, 0.10f), yb, zc), f3(0.92f, -0.38f, 0), f3(0, 1, 0));
    box(k, gb, f3(0, 0, -0.02f), f3(0.5f * (z1 - z0) - 0.04f, 0.24f, 0.02f), 0.006f, MAT_CREAM, FEAT_PLATE);
    for (int g = 0; g < 4; ++g) gaugeAt(k, gb, f3(-0.36f + g * 0.24f, 0.07f, 0.01f), 0.07f, 0.05f, MAT_BRASS, DIAL_SMALL);
    gaugeAt(k, gb, f3(-0.2f, -0.14f, 0.01f), 0.05f, 0.04f, MAT_CHROME, DIAL_SMALL);
    gaugeAt(k, gb, f3(0.2f, -0.14f, 0.01f), 0.05f, 0.04f, MAT_CHROME, DIAL_SMALL);
    plateAt(k, gb, f3(0, 0.205f, 0.002f), 0.4f, 0.035f, "TRIM MANIFOLD", PLATE_BRASS);
}

void radioRoom(Kit& k) {
    // the partition: aluminium panels on a frame, from the port hull to x = -0.35, the after port corner (frames
    // 55.3 to the after bulkhead); a door in its forward wall
    const float zf = frameZ(55.3f), xw = -0.35f;
    const float top = kAxisY + std::sqrt(kRCork * kRCork - xw * xw) - 0.02f;
    // the side wall (along z, facing starboard)
    for (int p = 0; p < 3; ++p) {
        const float za = p * zf / 3.0f + 0.01f, zb = (p + 1) * zf / 3.0f - 0.01f;
        box(k, Xform(), f3(xw, 0.5f * top, 0.5f * (za + zb)), f3(0.006f, 0.5f * top - 0.01f, 0.5f * (zb - za)), 0.003f, MAT_ALU, FEAT_NONE);
    }
    box(k, Xform(), f3(xw + 0.01f, 0.5f * top, zf), f3(0.02f, 0.5f * top, 0.02f), 0.004f, MAT_CREAM, FEAT_PLATE);
    // the forward wall (across, facing the bow) with a door
    const float xh = hullX(1.0f, -1, 0.0f);
    const float doorX0 = xw - 0.72f, doorX1 = xw - 0.06f;
    auto wallPart = [&](float x0, float x1, float y0, float y1) {
        box(k, Xform(), f3(0.5f * (x0 + x1), 0.5f * (y0 + y1), zf), f3(0.5f * (x1 - x0), 0.5f * (y1 - y0), 0.006f), 0.003f, MAT_ALU, FEAT_NONE);
    };
    wallPart(xh + 0.25f, doorX0, 0.0f, 1.9f);
    wallPart(doorX1, xw, 0.0f, 1.9f);
    wallPart(doorX0, doorX1, 1.88f, 2.1f);
    // the door: a sliding aluminium leaf half open, a handle
    box(k, Xform(), f3(doorX0 + 0.12f, 0.94f, zf + 0.03f), f3(0.33f, 0.92f, 0.008f), 0.004f, MAT_ALU, FEAT_NONE);
    with(k, Xform::frame(f3(doorX0 + 0.38f, 1.0f, zf + 0.045f), f3(0, 1, 0), f3(1, 0, 0)), MAT_CHROME, FEAT_NONE, [&] { k.roundedCylinder(0.01f, -0.08f, 0.08f, 0.004f); });
}

// ---------------------------------------------------------------------------------------------------
// the centre line

void chartTable(Kit& k, float3& top) {
    const float z0 = frameZ(50.9f), z1 = frameZ(49.6f);
    const float x0 = -0.78f, x1 = -0.02f;
    const float h = 0.95f;
    // the cabinet (the master gyro inside), drawers with brass pulls on its after face
    box(k, Xform(), f3(0.5f * (x0 + x1), 0.5f * h - 0.02f, 0.5f * (z0 + z1)), f3(0.5f * (x1 - x0), 0.5f * h - 0.02f, 0.5f * (z1 - z0)), 0.008f, MAT_GREY, FEAT_PLATE);
    for (int d = 0; d < 3; ++d) {
        const float y = 0.72f - d * 0.24f;
        box(k, Xform(), f3(0.5f * (x0 + x1), y, z0 - 0.004f), f3(0.5f * (x1 - x0) - 0.03f, 0.105f, 0.006f), 0.004f, MAT_GREY, FEAT_PLATE);
        with(k, Xform::frame(f3(0.5f * (x0 + x1), y + 0.04f, z0 - 0.012f), f3(1, 0, 0), f3(0, 1, 0)), MAT_BRASS, FEAT_NONE, [&] {
            k.roundedCylinder(0.007f, -0.06f, 0.06f, 0.003f);
        });
    }
    // the top: laminate in a steel band, a chart on it, the gyro's window
    box(k, Xform(), f3(0.5f * (x0 + x1), h + 0.015f, 0.5f * (z0 + z1)), f3(0.5f * (x1 - x0) + 0.02f, 0.015f, 0.5f * (z1 - z0) + 0.02f), 0.004f, MAT_WOOD, FEAT_NONE);
    with(k, Xform::at(f3(0.5f * (x0 + x1) - 0.04f, h + 0.0305f, 0.5f * (z0 + z1) + 0.05f)) * Xform::rotX(-0.5f * kPi) * Xform::rotZ(0.06f), MAT_PAPER, FEAT_NONE,
         [&] { k.quad(0.56f, 0.78f); });
    top = f3(0.5f * (x0 + x1), h + 0.03f, 0.5f * (z0 + z1));
    // dividers on the chart: two thin legs
    for (int s = -1; s <= 1; s += 2)
        with(k, Xform::frame(top + f3(0.08f, 0.004f, -0.12f), f3(s * 0.25f, 0.0f, 0.97f), f3(0, 1, 0)), MAT_STEEL, FEAT_NONE, [&] { k.roundedCylinder(0.0025f, 0.0f, 0.14f, 0.001f); });
    // a coffee mug (cream china) by the chart
    with(k, Xform::frame(top + f3(0.24f, 0.0f, 0.2f), f3(0, 1, 0), f3(1, 0, 0)), MAT_CREAM, FEAT_NONE, [&] {
        k.lathe({{0.0f, 0.0f}, {0.036f, 0.0f}, {0.04f, 0.004f}, {0.042f, 0.09f}, {0.039f, 0.092f}, {0.037f, 0.012f}, {0.0f, 0.012f}});
    });
}

void steeringStand(Kit& k) {
    const float zc = frameZ(49.2f), xc = 0.28f;
    // the stand: a cream pedestal with its instrument head
    box(k, Xform(), f3(xc, 0.55f, zc), f3(0.2f, 0.55f, 0.16f), 0.02f, MAT_CREAM, FEAT_CAST);
    box(k, Xform(), f3(xc, 1.18f, zc + 0.02f), f3(0.24f, 0.1f, 0.2f), 0.025f, MAT_CREAM, FEAT_CAST);
    // the gyro repeater (black square box) and the rudder angle indicator (chrome) on the head, facing aft
    const Xform aft = axes(f3(xc, 1.18f, zc - 0.18f), f3(-1, 0, 0), f3(0, 1, 0), f3(0, 0, -1));
    box(k, aft, f3(-0.1f, 0.03f, 0.02f), f3(0.085f, 0.085f, 0.04f), 0.008f, MAT_WRINKLE, FEAT_NONE);
    gaugeAt(k, aft, f3(-0.1f, 0.03f, 0.065f), 0.065f, 0.02f, MAT_CHROME, DIAL_COMPASS);
    box(k, aft, f3(0.11f, 0.03f, 0.02f), f3(0.07f, 0.06f, 0.04f), 0.01f, MAT_CHROME, FEAT_NONE);
    with(k, aft * Xform::at(f3(0.11f, 0.03f, 0.062f)), MAT_DIAL, FEAT_NONE, [&] { k.roundedBox(f3(0.055f, 0.045f, 0.001f), 0.003f); });
    // the wheel: 65 cm, four bronze spokes, its rim wound with red tape, on the stand's after face
    const Xform w = Xform::frame(f3(xc, 0.98f, zc - 0.28f), f3(0, 0, -1), f3(0, 1, 0));
    with(k, Xform::frame(f3(xc, 0.98f, zc - 0.16f), f3(0, 0, -1), f3(0, 1, 0)), MAT_CREAM, FEAT_CAST, [&] { k.roundedCylinder(0.05f, 0.0f, 0.12f, 0.008f); });
    bigWheel(k, w, 0.325f, 0.02f, 4, MAT_TAPE, MAT_BRASS);
}

void ladderAndHatch(Kit& k) {
    const float zc = frameZ(51.6f), xc = -0.12f;
    const float top = kAxisY + std::sqrt(kRCork * kRCork - xc * xc);
    // the trunk up through the overhead: an oval collar (53 x 69 cm opening)
    const std::vector<float2> ov = Kit::superellipse(0.31f, 0.39f, 2.4f, 72);
    with(k, Xform::at(f3(xc, top - 0.14f, zc + 0.05f)) * Xform::rotX(-0.5f * kPi), MAT_CREAM, FEAT_CAST, [&] {
        const int n = int(ov.size());
        // the trunk's inside (facing in) and outside, from the collar up 0.3 m
        for (int pass = 0; pass < 2; ++pass) {
            const float sc = pass ? 1.08f : 1.0f;
            k.grid(n, 6, [&](int i, int j) {
                Kit::GridPt g;
                const int kk = (pass ? i : n - i) % n;
                const float2 q = ov[kk];
                const float2 qa = ov[(kk + n - 1) % n], qb = ov[(kk + 1) % n];
                float3 nn = normalize(f3(qb.y - qa.y, -(qb.x - qa.x), 0));
                if (!pass) nn = -nn;
                g.p = f3(q.x * sc, q.y * sc, 0.4f * float(j) / 6);
                g.n = nn;
                g.s = make_float2(float(i) * 0.03f, g.p.z);
                return g;
            });
        }
        // the collar's lower face
        k.grid(n, 1, [&](int i, int j) {
            Kit::GridPt g;
            const int kk = (n - i) % n;
            const float sc = j ? 1.18f : 1.0f;
            g.p = f3(ov[kk].x * sc, ov[kk].y * sc, 0.0f);
            g.n = f3(0, 0, -1);
            g.s = make_float2(g.p.x, g.p.y);
            return g;
        });
    });
    // the ladder: two rails 40 cm apart, round rungs every 30 cm, from the deck to the collar
    const float yTop = top - 0.12f;
    for (int s = -1; s <= 1; s += 2)
        with(k, Xform::frame(f3(xc + s * 0.2f, 0.0f, zc - 0.22f), f3(0, 1, 0), f3(1, 0, 0)), MAT_STEEL, FEAT_NONE, [&] { k.roundedCylinder(0.018f, 0.0f, yTop, 0.004f); });
    for (float y = 0.3f; y < yTop - 0.1f; y += 0.3f)
        with(k, Xform::frame(f3(xc - 0.2f, y, zc - 0.22f), f3(1, 0, 0), f3(0, 1, 0)), MAT_STEEL, FEAT_KNURL, [&] { k.roundedCylinder(0.0125f, 0.0f, 0.4f, 0.002f, false, false); });
}

void periscopeWells(Kit& k) {
    struct W { float f, x, r; };
    const W wells[] = {{52.7f, 0.05f, 0.24f}, {53.9f, 0.18f, 0.21f}};
    for (const W& w : wells) {
        const float z = frameZ(w.f);
        const float top = kAxisY + std::sqrt(kRCork * kRCork - w.x * w.x);
        with(k, Xform::frame(f3(w.x, 0.0f, z), f3(0, 1, 0), f3(1, 0, 0)), MAT_CREAM, FEAT_PLATE, [&] { k.tube(w.r, 0.0f, top); });
        // bands at the deck, mid height and the overhead
        for (float y : {0.04f, 1.1f, top - 0.05f})
            with(k, Xform::frame(f3(w.x, y, z), f3(0, 1, 0), f3(1, 0, 0)), MAT_BRASS, FEAT_NONE, [&] { k.roundedCylinder(w.r + 0.012f, -0.03f, 0.03f, 0.006f, false, false); });
    }
}

// ---------------------------------------------------------------------------------------------------
// starboard side

void airManifold(Kit& k) {
    const int side = 1;
    const float z0 = frameZ(54.0f), z1 = frameZ(52.0f), zc = 0.5f * (z0 + z1);
    // the gauge board (polished aluminium) leaning with the hull, two rows of gauges
    const float y = 1.55f;
    const Xform gb = Xform::frame(f3(hullX(y, side, 0.12f), y, zc), f3(-0.92f, -0.38f, 0), f3(0, 1, 0));
    box(k, gb, f3(0, 0, -0.02f), f3(0.5f * (z1 - z0) - 0.03f, 0.32f, 0.02f), 0.006f, MAT_ALU, FEAT_NONE);
    for (int row = 0; row < 2; ++row)
        for (int g = 0; g < 9; ++g) gaugeAt(k, gb, f3(-0.62f + g * 0.155f, 0.13f - row * 0.2f, 0.01f), 0.062f, 0.05f, MAT_BRASS, DIAL_AIR);
    plateAt(k, gb, f3(0, 0.285f, 0.002f), 0.5f, 0.035f, "H.P. AIR MANIFOLD   3000 LBS.", PLATE_BRASS);
    // the manifold: three horizontal tiers of valves, facing inboard, white bodies, chrome T handles
    for (int tier = 0; tier < 3; ++tier) {
        const float yy = 0.72f + tier * 0.16f;
        const float xx = hullX(yy, side, 0.32f);
        with(k, Xform(), MAT_CREAM, FEAT_NONE, [&] { pipeRun(k, {f3(xx, yy, z0 + 0.05f), f3(xx, yy, z1 - 0.05f)}, 0.022f, 0.05f); });
        const int nv = tier == 0 ? 7 : (tier == 1 ? 5 : 7);
        for (int v = 0; v < nv; ++v) {
            const float zz = z0 + 0.15f + v * (z1 - z0 - 0.3f) / (nv - 1);
            Xform vt;
            vt.o = f3(xx - 0.08f, yy, zz);
            vt.x = f3(0, 1, 0);
            vt.y = f3(0, 0, -1);
            vt.z = f3(-1, 0, 0);
            with(k, vt, MAT_BRASS, FEAT_NONE, [&] {
                k.lathe({{0.0f, 0.06f}, {0.028f, 0.06f}, {0.03f, 0.03f}, {0.026f, 0.0f}, {0.02f, -0.02f}, {0.0f, -0.02f}});
                k.t = k.t * Xform::at(f3(0, 0, 0.06f));
                k.mat = MAT_CHROME;
                k.roundedCylinder(0.006f, 0.0f, 0.05f, 0.002f);
                k.t = k.t * Xform::at(f3(0, 0, 0.055f)) * Xform::rotY(0.5f * kPi);
                k.roundedCylinder(0.007f, -0.05f, 0.05f, 0.003f);
            });
        }
    }
}

void electricalPanels(Kit& k) {
    const int side = 1;
    // grey switchboards and cabinets standing on the deck against the hull: forward (aux gyro, IC board, gyro control)
    // and aft (DC distribution, external switchboard)
    struct C { float f0, f1, h, d; };
    const C cabs[] = {{51.9f, 51.0f, 1.75f, 0.42f}, {51.0f, 49.7f, 1.8f, 0.4f}, {49.6f, 48.0f, 1.6f, 0.45f}, {57.2f, 56.3f, 1.8f, 0.4f},
                      {56.2f, 55.5f, 1.75f, 0.38f}};
    for (const C& c : cabs) {
        const float z0 = frameZ(c.f0), z1 = frameZ(c.f1);
        const float xBack = hullX(c.h, side, 0.02f);   // the top is limited by the hull's curve
        const float xFront = std::min(xBack, hullX(0.3f, side, 0.0f)) - c.d;
        const float x0 = xFront, x1 = xBack;
        box(k, Xform(), f3(0.5f * (x0 + x1), 0.5f * c.h, 0.5f * (z0 + z1)), f3(0.5f * (x1 - x0), 0.5f * c.h, 0.5f * (z1 - z0) - 0.01f), 0.01f, MAT_GREY, FEAT_PLATE);
        // the face: switch handles and small meters in rows
        const Xform face = axes(f3(x0, 0.0f, 0.5f * (z0 + z1)), f3(0, 0, 1), f3(0, 1, 0), f3(-1, 0, 0));
        const float w = 0.5f * (z1 - z0) - 0.08f;
        for (int r = 0; r < 3; ++r) {
            const float yy = 1.45f - r * 0.32f;
            if (yy > c.h - 0.1f) continue;
            for (int s = 0; s < 4; ++s) {
                const float xx = -w + s * (2.0f * w / 3.0f);
                if (r == 0) gaugeAt(k, face, f3(xx, yy, 0.01f), 0.045f, 0.03f, MAT_CHROME, DIAL_METER);
                else {
                    with(k, face * Xform::at(f3(xx, yy, 0.0f)), MAT_PAINT_DARK, FEAT_NONE, [&] {
                        k.roundedCylinder(0.022f, 0.0f, 0.02f, 0.004f);
                        k.t = k.t * Xform::at(f3(0, 0, 0.02f)) * Xform::rotX(-0.35f);
                        k.roundedCylinder(0.006f, 0.0f, 0.06f, 0.002f);
                        k.t = k.t * Xform::at(f3(0, 0, 0.06f));
                        k.mat = (s + r) % 2 ? MAT_RED : MAT_PAINT_DARK;
                        k.sphere(0.012f);
                    });
                }
            }
        }
        static const char* names[] = {"GYRO CONTROL", "I.C. SWITCHBOARD", "AUX. GYRO", "FWD. D.C. DISTRIBUTION", "EXT. SWITCHBOARD"};
        plateAt(k, face, f3(0, c.h - 0.12f, 0.002f), 2.0f * w * 0.9f, 0.04f, names[&c - cabs], PLATE_BLACK);
    }
}

}   // namespace

void buildEquipment(Kit& k, float3& chartTop, std::vector<DzLight>& minor) {
    divingStation(k);
    christmasTreeAndManifold(k, minor);
    trimManifold(k);
    radioRoom(k);
    chartTable(k, chartTop);
    steeringStand(k);
    ladderAndHatch(k);
    periscopeWells(k);
    airManifold(k);
    electricalPanels(k);
}

}   // namespace dz
