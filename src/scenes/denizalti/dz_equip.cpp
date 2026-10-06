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
// a board on the hull at height y (side), its face `in` inboard of the cork, square to the hull there (facing the
// axis; x along the room as Xform::frame makes it): a flat board tangent to the curve keeps clear of the frames, whose
// inner flanges stand 12.5 cm in from the cork
Xform hullBoard(float y, int side, float in, float z) {
    const float r = kRCork - in;
    const float a = std::asin(std::clamp((y - kAxisY) / r, -1.0f, 1.0f));   // at the board's own radius: its centre at y
    return Xform::frame(f3(side * r * std::cos(a), kAxisY + r * std::sin(a), z), f3(-side * std::cos(a), -std::sin(a), 0), f3(0, 1, 0));
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
// the hull's service runs along the room (hullServices builds them; the boards stand in front of them, 15-25 cm in
// from the cork, and their brackets keep clear): angle from starboard (degrees), in from the cork, radius
struct HullRun { float aDeg, in, r; uint32_t mat; bool valve; };
const HullRun kHighRuns[] = {{132.0f, 0.18f, 0.016f, MAT_CREAM, true},  {128.0f, 0.2f, 0.011f, MAT_BRASS, false}, {137.0f, 0.19f, 0.0095f, MAT_CREAM, false},
                             {124.0f, 0.22f, 0.022f, MAT_RED, true},    {48.0f, 0.18f, 0.016f, MAT_CREAM, true},  {52.0f, 0.2f, 0.011f, MAT_BRASS, false},
                             {43.0f, 0.19f, 0.0095f, MAT_BRASS, false}, {56.0f, 0.22f, 0.019f, MAT_CREAM, true}};
const float kConduitDeg[] = {140.0f, 40.0f, 118.0f, 62.0f};
const float kConduitIn = 0.165f;

// the board's brackets: flat bars from its back (local z = back, half sizes hw along the room, hh up the hull) out to
// the frames' inner flanges where a frame crosses it, else to the cork at its ends. in: the board's face from the cork
void boardBrackets(Kit& k, const Xform& b, float hw, float hh, float back, float in) {
    const float r0 = kRCork - in;
    std::vector<float> xs;
    float reach = kRPlate - 0.15f + 0.004f;   // into the flange
    for (int f = 47; f <= 59; ++f) {
        const float xl = (frameZ(float(f)) - b.o.z) / b.x.z;
        if (std::fabs(xl) < hw - 0.02f) xs.push_back(xl);
    }
    if (xs.empty()) {
        xs = {-(hw - 0.05f), hw - 0.05f};
        reach = kRCork + 0.004f;
    }
    // the runs in the board's frame: their offset up the board (u) and depth (w, negative outboard); a bracket at u = t
    // must pass them by
    struct Near { float u, w, r; };
    std::vector<Near> near;
    auto addRun = [&](float aDeg, float inn, float r) {
        const float a = aDeg * kPi / 180.0f, rr = kRCork - inn;
        const float3 p = f3(rr * std::cos(a), kAxisY + rr * std::sin(a), b.o.z);
        const float3 d = p - b.o;
        near.push_back({dot(d, b.y), dot(d, b.z), r});
    };
    for (const HullRun& h : kHighRuns) addRun(h.aDeg, h.in, h.r);
    for (float a : kConduitDeg) addRun(a, kConduitIn, 0.035f);   // and its junction boxes
    for (float xl : xs)
        for (int s = -1; s <= 1; s += 2) {
            float t = s * (hh - 0.06f);
            for (int tries = 0; tries < 40; ++tries) {
                bool clear = true;
                for (const Near& n : near)
                    if (n.w < back + n.r && std::fabs(n.u - t) < n.r + 0.012f) clear = false;
                if (clear) break;
                t -= s * 0.01f;
            }
            const float L = std::sqrt(std::max(0.0f, reach * reach - t * t)) - (r0 - back);
            if (L <= 0.0f) continue;
            box(k, b, f3(xl, t, back - 0.5f * L), f3(0.016f, 0.003f, 0.5f * L), 0.0012f, MAT_CREAM, FEAT_PLATE);
            box(k, b, f3(xl, t + 0.012f * s, back - 0.004f), f3(0.016f, 0.012f, 0.004f), 0.0012f, MAT_CREAM, FEAT_PLATE);   // the foot
        }
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

// a valve's tag: a brass disc stamped with its name, on a split ring through the handwheel's rim at the bottom (frame
// t: the wheel's, its y up, rim radius R)
void valveTag(Kit& k, const Xform& t, float R, float rr, const std::string& name, float swing) {
    with(k, t * Xform::frame(f3(0, -R - 0.004f, 0), f3(1, 0, 0), f3(0, 1, 0)), MAT_STEEL, FEAT_NONE, [&] { k.torus(rr + 0.0045f, 0.0009f); });
    const Xform tag = t * Xform::at(f3(0, -R - rr - 0.009f, 0.0f)) * Xform::rotZ(swing) * Xform::at(f3(0, -0.013f, 0.0f));
    plateAt(k, tag, f3(0, 0, -0.0008f), 0.044f, 0.026f, name, PLATE_BRASS);
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
    // the panel: a flat board leaning back with the hull, from 0.55 m to 2.05 m, its back clear of the frames' inner
    // flanges (12.5 cm in from the cork) all the way up
    const float yb = 0.55f, yt = 1.85f;
    const float xb = hullX(yb, side, 0.31f), xt = hullX(yt, side, 0.31f);   // its back 25 cm in: the high runs pass behind
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
        // a ribbed rubber mat on the deck where he stands, between the board and the stool
        with(k, Xform::at(f3(xb + 0.8f, 0.0045f, zz)), MAT_RUBBER, FEAT_TREAD, [&] {
            k.fp = make_float2(0.025f, 0.0f);
            k.roundedBox(f3(0.27f, 0.0045f, 0.48f), 0.003f);
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
    const Xform tree = hullBoard(1.74f, side, 0.33f, zc);   // its back 27 cm in: the high runs pass behind
    const float3 hs = f3(0.36f, 0.42f, 0.03f);
    box(k, tree, f3(0, 0, -0.03f), hs, 0.008f, MAT_GREY, FEAT_NONE);
    boardBrackets(k, tree, hs.x, hs.y, -0.06f, 0.33f);
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
        plateAt(k, face, f3(0, -0.043f, 0), 0.46f, 0.028f, "MAIN VENT CONTROL MANIFOLD", PLATE_BLACK);
    }
    // the hydraulic supply tank: a grey bolted box with sight glasses, against the forward bulkhead
    const float3 tc = f3(hullX(0.65f, side, 0.30f) + 0.05f, 0.62f, kLength - 0.22f);
    box(k, Xform(), tc, f3(0.22f, 0.6f, 0.12f), 0.012f, MAT_GREY, FEAT_PLATE);
    for (int s = 0; s < 2; ++s)
        with(k, Xform::frame(tc + f3(-0.08f + s * 0.16f, -0.2f, -0.125f), f3(0, 1, 0), f3(1, 0, 0)), MAT_GLASS, FEAT_NONE, [&] { k.tube(0.011f, 0.0f, 0.5f); });
    // the main oil pressure and accumulator gauges above the manifold
    const Xform gb = hullBoard(1.45f, side, 0.29f, zc - 0.62f);
    box(k, gb, f3(0, 0, -0.02f), f3(0.16f, 0.22f, 0.02f), 0.005f, MAT_CREAM, FEAT_PLATE);
    boardBrackets(k, gb, 0.16f, 0.22f, -0.04f, 0.29f);
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
            // the lower row's tags: the tank or line each valve serves
            static const char* tags[8] = {"FWD\nTRIM", "AFT\nTRIM", "AUX\nNo.1", "AUX\nNo.2", "NEG.\nTANK", "SAFETY\nTANK", "SEA", "DRAIN"};
            if (row == 0) valveTag(k, t * Xform::at(f3(0, 0, zw)), 0.038f, 0.0055f, tags[v], 0.06f * float(int(v % 3) - 1));
        }
    // the gauge board directly above
    const float yb = 1.55f;
    const Xform gb = hullBoard(yb, side, 0.29f, zc);
    box(k, gb, f3(0, 0, -0.02f), f3(0.5f * (z1 - z0) - 0.04f, 0.24f, 0.02f), 0.006f, MAT_CREAM, FEAT_PLATE);
    boardBrackets(k, gb, 0.5f * (z1 - z0) - 0.04f, 0.24f, -0.04f, 0.29f);
    for (int g = 0; g < 4; ++g) gaugeAt(k, gb, f3(-0.36f + g * 0.24f, 0.07f, 0.01f), 0.07f, 0.05f, MAT_BRASS, DIAL_SMALL);
    gaugeAt(k, gb, f3(-0.2f, -0.14f, 0.01f), 0.05f, 0.04f, MAT_CHROME, DIAL_SMALL);
    gaugeAt(k, gb, f3(0.2f, -0.14f, 0.01f), 0.05f, 0.04f, MAT_CHROME, DIAL_SMALL);
    plateAt(k, gb, f3(0, 0.205f, 0.002f), 0.4f, 0.035f, "TRIM MANIFOLD", PLATE_BRASS);
}

void radioRoom(Kit& k) {
    // the partition: painted sheet panels on a frame (cream as everything aboard), from the port hull to x = -0.35, the
    // after port corner (frames 55.3 to the after bulkhead); a sliding door in its forward wall. Rows of screws hold
    // the panels to the frame; the door has a louvre low down, a small window, the room's name stencilled
    const float zf = frameZ(55.3f), xw = -0.35f;
    const float top = kAxisY + std::sqrt(kRCork * kRCork - xw * xw) - 0.02f;
    auto screws = [&](float3 a, float3 b, float3 out, float pitch) {
        const int n = std::max(1, int(length(b - a) / pitch));
        for (int i = 0; i <= n; ++i)
            with(k, Xform::frame(a + (b - a) * (float(i) / n), out, f3(0, 1, 0)), MAT_CREAM, FEAT_NONE, [&] { k.dome(0.0045f, 0.0016f); });
    };
    // the side wall (along z, facing starboard)
    for (int p = 0; p < 3; ++p) {
        const float za = p * zf / 3.0f + 0.01f, zb = (p + 1) * zf / 3.0f - 0.01f;
        box(k, Xform(), f3(xw, 0.5f * top, 0.5f * (za + zb)), f3(0.006f, 0.5f * top - 0.01f, 0.5f * (zb - za)), 0.003f, MAT_CREAM, FEAT_PLATE);
        const float xs = xw + 0.006f;
        screws(f3(xs, 0.04f, za + 0.03f), f3(xs, top - 0.06f, za + 0.03f), f3(1, 0, 0), 0.16f);
        screws(f3(xs, 0.04f, zb - 0.03f), f3(xs, top - 0.06f, zb - 0.03f), f3(1, 0, 0), 0.16f);
        screws(f3(xs, 0.04f, za + 0.03f), f3(xs, 0.04f, zb - 0.03f), f3(1, 0, 0), 0.16f);
    }
    box(k, Xform(), f3(xw + 0.01f, 0.5f * top, zf), f3(0.02f, 0.5f * top, 0.02f), 0.004f, MAT_CREAM, FEAT_PLATE);
    // the forward wall (across, facing the bow) with a door
    const float xh = hullX(1.0f, -1, 0.0f);
    const float doorX0 = xw - 0.72f, doorX1 = xw - 0.06f;
    auto wallPart = [&](float x0, float x1, float y0, float y1) {
        box(k, Xform(), f3(0.5f * (x0 + x1), 0.5f * (y0 + y1), zf), f3(0.5f * (x1 - x0), 0.5f * (y1 - y0), 0.006f), 0.003f, MAT_CREAM, FEAT_PLATE);
    };
    wallPart(xh + 0.25f, doorX0, 0.0f, 1.9f);
    wallPart(doorX1, xw, 0.0f, 1.9f);
    wallPart(doorX0, doorX1, 1.88f, 2.1f);
    const float zs = zf + 0.006f;
    screws(f3(doorX0 - 0.03f, 0.04f, zs), f3(doorX0 - 0.03f, 1.84f, zs), f3(0, 0, 1), 0.16f);
    screws(f3(doorX1 + 0.025f, 0.04f, zs), f3(doorX1 + 0.025f, 1.84f, zs), f3(0, 0, 1), 0.16f);
    screws(f3(xh + 0.4f, 1.84f, zs), f3(doorX0 - 0.03f, 1.84f, zs), f3(0, 0, 1), 0.16f);
    // the door: a sliding leaf half open; its louvre, window, name, handle
    const float dx = doorX0 + 0.12f, dzz = zf + 0.03f;
    box(k, Xform(), f3(dx, 0.94f, dzz), f3(0.33f, 0.92f, 0.008f), 0.004f, MAT_CREAM, FEAT_PLATE);
    const Xform door = Xform::at(f3(dx, 0.0f, dzz + 0.008f));
    box(k, door, f3(0, 0.27f, 0.0005f), f3(0.22f, 0.12f, 0.0005f), 0.0004f, MAT_PAINT_DARK, FEAT_NONE);
    for (int sl = 0; sl < 9; ++sl)
        with(k, door * Xform::at(f3(0, 0.165f + sl * 0.026f, 0.0005f)) * Xform::rotX(0.75f), MAT_CREAM, FEAT_NONE, [&] { k.roundedBox(f3(0.215f, 0.012f, 0.0011f), 0.0006f); });
    for (int sd = -1; sd <= 1; sd += 2) box(k, door, f3(sd * 0.228f, 0.27f, 0.004f), f3(0.008f, 0.128f, 0.004f), 0.002f, MAT_CREAM, FEAT_NONE);
    for (int sd = -1; sd <= 1; sd += 2) box(k, door, f3(0, 0.27f + sd * 0.128f, 0.004f), f3(0.236f, 0.008f, 0.004f), 0.002f, MAT_CREAM, FEAT_NONE);
    with(k, door * Xform::at(f3(0, 1.5f, 0.0f)), MAT_CHROME, FEAT_NONE, [&] {
        k.lathe({{0.075f, -0.001f}, {0.092f, 0.0f}, {0.094f, 0.006f}, {0.084f, 0.009f}, {0.075f, 0.007f}});
    });
    with(k, door * Xform::at(f3(0, 1.5f, 0.0012f)), MAT_PAINT_DARK, FEAT_NONE, [&] { k.ring(0.0f, 0.077f, 0.0f, true); });
    with(k, door * Xform::at(f3(0, 1.5f, 0.0035f)), MAT_GLASS, FEAT_NONE, [&] { k.ring(0.0f, 0.077f, 0.0f, true); });
    plateAt(k, door, f3(0, 1.22f, 0.0f), 0.44f, 0.1f, "TELS\xC4\xB0Z ODASI", PLATE_STENCIL);
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
    // the chart: printed, worked in pencil (the decal atlas draws it), north away from the navigator at the table's
    // after side. Paper once rolled lies in soft waves a millimetre or two high, its far corner lifting; a back face
    // under it (the lifted corner is seen from below too)
    {
        const int id = activeDecals ? activeDecals->plate("CHART", 0.56f, 0.78f, PLATE_CHART) : 0;
        const float cw = 0.56f, ch = 0.78f;
        auto hgt = [&](float x, float y) {
            float hh = 0.0011f * (0.5f + 0.5f * std::sin(x * 11.0f + 0.7f) * std::sin(y * 7.0f + 1.3f)) + 0.0006f * (0.5f + 0.5f * std::sin(y * 17.0f + x * 3.0f));
            const float cx = std::max(0.0f, x - (0.5f * cw - 0.1f)), cy = std::max(0.0f, y - (0.5f * ch - 0.13f));
            return hh + 0.55f * (cx * cx + cy * cy);
        };
        const Xform ct = Xform::at(f3(0.5f * (x0 + x1) - 0.04f, h + 0.0302f, 0.5f * (z0 + z1) + 0.05f)) * Xform::rotX(-0.5f * kPi) * Xform::rotZ(0.06f + kPi);
        const int nu = k.segs(cw), nv = k.segs(ch);
        for (int side = 0; side < 2; ++side)
            with(k, ct, MAT_PAPER, side ? FEAT_NONE : FEAT_LABEL, [&] {
                k.fp = make_float2(float(id), 0.0f);
                k.grid(nu, nv, [&](int i, int j) {
                    Kit::GridPt g;
                    const float x = -0.5f * cw + cw * float(side ? nu - i : i) / nu, y = -0.5f * ch + ch * float(j) / nv;
                    const float e = 1e-4f;
                    const float3 n = normalize(f3(-(hgt(x + e, y) - hgt(x - e, y)) / (2.0f * e), -(hgt(x, y + e) - hgt(x, y - e)) / (2.0f * e), 1.0f));
                    g.p = f3(x, y, hgt(x, y) - (side ? 0.0003f : 0.0f));
                    g.n = side ? -n : n;
                    g.s = make_float2(x, y);
                    return g;
                });
            });
    }
    top = f3(0.5f * (x0 + x1), h + 0.03f, 0.5f * (z0 + z1));
    // dividers on the chart: two legs from a brass hinge, raised there, their points on the paper
    {
        const float3 apex = top + f3(0.08f, 0.0135f, -0.12f);
        for (int s = -1; s <= 1; s += 2) {
            const float3 tip = top + f3(0.08f + s * 0.034f, 0.0024f, 0.016f);
            with(k, Xform::frame(apex, tip - apex, f3(0, 1, 0)), MAT_STEEL, FEAT_NONE, [&] {
                k.roundedCylinder(0.0024f, 0.0f, length(tip - apex) - 0.008f, 0.001f, true, false);
                k.lathe({{0.0024f, length(tip - apex) - 0.008f}, {0.0006f, length(tip - apex)}, {0.0f, length(tip - apex) + 0.0002f}});
            });
        }
        with(k, Xform::frame(apex, f3(0, 1, 0), f3(1, 0, 0)), MAT_BRASS, FEAT_NONE, [&] {
            k.roundedCylinder(0.0068f, -0.0035f, 0.0035f, 0.0012f);
            k.roundedCylinder(0.0028f, 0.0035f, 0.014f, 0.0008f);
        });
    }
    // a parallel rule on the chart: two black rules on brass links, brass knobs to walk it
    {
        const Xform pr = Xform::at(top + f3(-0.12f, 0.0022f, -0.04f)) * Xform::rotY(0.55f);
        for (int s = 0; s < 2; ++s) box(k, pr, f3(0, 0.003f, s * 0.035f), f3(0.19f, 0.003f, 0.016f), 0.0015f, MAT_WRINKLE, FEAT_NONE);
        for (int l = -1; l <= 1; l += 2) {
            box(k, pr, f3(l * 0.12f, 0.0068f, 0.0175f), f3(0.006f, 0.0008f, 0.025f), 0.0005f, MAT_BRASS, FEAT_NONE);
            for (int s = 0; s < 2; ++s)
                with(k, pr * Xform::at(f3(l * 0.12f, 0.0076f, s * 0.035f)) * Xform::rotX(-0.5f * kPi), MAT_BRASS, FEAT_NONE, [&] { k.dome(0.004f, 0.0015f); });
            for (int s = 0; s < 2; ++s)
                with(k, pr * Xform::frame(f3(l * 0.06f, 0.006f, s * 0.035f), f3(0, 1, 0), f3(1, 0, 0)), MAT_BRASS, FEAT_NONE,
                     [&] { k.lathe({{0.0f, 0.0f}, {0.0062f, 0.0f}, {0.0066f, 0.004f}, {0.0048f, 0.011f}, {0.0058f, 0.016f}, {0.0f, 0.0185f}}); });
        }
    }
    // a pencil across the track: red lacquer on six flats (the tape's red: no chipped enamel on a pencil), the cut
    // wood, the lead
    with(k, Xform::frame(top + f3(0.17f, 0.0062f, -0.31f), f3(std::cos(1.77f), 0.0f, std::sin(1.77f)), f3(0, 1, 0)), MAT_TAPE, FEAT_NONE, [&] {
        k.hexPrism(0.0072f, 0.0f, 0.15f, 0.0006f);
        k.mat = MAT_WOOD;
        k.lathe({{0.0036f, 0.15f}, {0.0011f, 0.168f}, {0.0f, 0.1683f}});
        k.mat = MAT_PAINT_DARK;
        k.t = k.t * Xform::at(f3(0, 0, 0.1675f));
        k.lathe({{0.00105f, 0.0f}, {0.0002f, 0.0045f}, {0.0f, 0.0046f}});
    });
    // a coffee mug (cream china) by the chart, its handle
    with(k, Xform::frame(top + f3(0.31f, 0.0f, 0.3f), f3(0, 1, 0), f3(1, 0, 0)), MAT_CREAM, FEAT_NONE, [&] {
        k.lathe({{0.0f, 0.0f}, {0.036f, 0.0f}, {0.04f, 0.004f}, {0.042f, 0.09f}, {0.039f, 0.092f}, {0.037f, 0.012f}, {0.0f, 0.012f}});
    });
    with(k, Xform::frame(top + f3(0.31f + 0.041f, 0.048f, 0.3f), f3(0, 0, 1), f3(0, 1, 0)), MAT_CREAM, FEAT_NONE, [&] { k.torus(0.022f, 0.0055f, -0.5f * kPi, 0.5f * kPi); });
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
    const Xform gb = hullBoard(y, side, 0.3f, zc);
    box(k, gb, f3(0, 0, -0.02f), f3(0.5f * (z1 - z0) - 0.03f, 0.32f, 0.02f), 0.006f, MAT_ALU, FEAT_NONE);
    boardBrackets(k, gb, 0.5f * (z1 - z0) - 0.03f, 0.32f, -0.04f, 0.3f);
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

// a rotary snap switch on a face (frame f, its +z out) at c: a chrome escutcheon with its OFF and ON marks, a
// bakelite boss and the bar handle turned to `angle` (0: up), a white line along the handle's pointing end
void rotarySwitch(Kit& k, const Xform& f, float3 c, float angle) {
    with(k, f * Xform::at(c), MAT_CHROME, FEAT_NONE, [&] {
        k.roundedCylinder(0.024f, 0.0f, 0.0025f, 0.001f, false, true);
        const Xform t0 = k.t;
        k.mat = MAT_PAINT_DARK;
        for (int m = -1; m <= 1; m += 2) {
            k.t = t0 * Xform::rotZ(m * 0.785f) * Xform::at(f3(0, 0.019f, 0.0025f));
            k.roundedBox(f3(0.0008f, 0.0035f, 0.0003f), 0.0002f);
        }
        k.mat = MAT_WRINKLE;
        k.t = t0;
        k.roundedCylinder(0.0105f, 0.0025f, 0.011f, 0.002f, false, true);
        k.t = t0 * Xform::rotZ(angle) * Xform::at(f3(0, 0, 0.0165f));
        k.roundedBox(f3(0.0065f, 0.021f, 0.0062f), 0.0028f);
        k.mat = MAT_CREAM;
        k.t = k.t * Xform::at(f3(0, 0.011f, 0.0061f));
        k.roundedBox(f3(0.0011f, 0.0075f, 0.0004f), 0.0003f);
        k.t = t0;
    });
}

// the circuits on each board's labels (REFERANS §3: a fleet boat's control room switchboards)
const std::vector<std::vector<const char*>> kCircuits = {
    {"GYRO MOTOR", "FOLLOW UP", "REPEATERS 1", "REPEATERS 2", "COMPENSATOR", "GYRO ALARM", "SPEED UNIT", "LATITUDE CORR.", "D.R. TRACER",
     "SUPPLY No.1", "SUPPLY No.2", "HEATERS", "STARTING", "RUNNING"},
    {"1MC ANNOUNCING", "GENERAL ALARM", "COLLISION ALARM", "DIVING ALARM", "RUDDER ANGLE IND.", "BOW PLANE IND.", "STERN PLANE IND.",
     "ENGINE ORDER TEL.", "SHAFT REV. IND.", "HULL OPENING IND.", "DEPTH GAUGE LTS.", "SOUND POWERED TEL.", "TORPEDO READY",
     "FIRING CIRCUITS", "WHISTLE", "UNDERWATER LOG", "STEERING REP.", "MAGNETIC COMPASS LT.", "SALINITY IND.", "BATTERY VENT IND.",
     "MOTOR ORDER", "EMERG. LIGHTS", "AIR ALARM", "FLOOD ALARM"},
    {"AUX. GYRO MOTOR", "AUX. FOLLOW UP", "AUX. REPEATERS", "TRANSFER", "AUX. ALARM", "INVERTER", "INVERTER FIELD", "SUPPLY", "TEST"},
    {"LIGHTING FWD.", "LIGHTING AFT", "EMERG. LIGHTING", "HYDRAULIC PUMP", "TRIM PUMP", "DRAIN PUMP", "AIR COMPRESSOR No.1",
     "AIR COMPRESSOR No.2", "VENT FAN SUPPLY", "VENT FAN EXHAUST", "GALLEY", "RADIO", "RADAR", "SONAR", "PERISCOPE HOIST No.1",
     "PERISCOPE HOIST No.2", "ANCHOR WINDLASS", "CAPSTAN", "BOW PLANE TILT", "REFRIGERATION", "AIR CONDITIONING", "DISTILLER",
     "BATTERY FANS", "SPARE"},
    {"SHORE POWER", "BATTERY CHARGE", "GROUND DETECTOR", "BUS TIE", "FWD. BATTERY", "AFT BATTERY", "SPARE"},
};

// a switchboard's front (frame f: x across, y up from the deck, z out of the face; half width hw, height h, board ci):
// a frame round hinged doors 4 mm proud, the meter panel at the top under the board's name, rows of rotary snap
// switches each over its circuit's label, fuses on the lowest row, a louvre low down, a dark kick plate at the deck
void cabinetFace(Kit& k, const Xform& f, float hw, float h, int ci) {
    static const char* names[] = {"GYRO CONTROL", "I.C. SWITCHBOARD", "AUX. GYRO", "FWD. D.C. DISTRIBUTION", "EXT. SWITCHBOARD"};
    const float dz = 0.004f;   // the doors' face
    const float m = 0.025f;    // the frame round them
    const float gap = 0.0025f;
    auto door = [&](float xa, float xb, float ya, float yb) {
        box(k, f, f3(0.5f * (xa + xb), 0.5f * (ya + yb), 0.5f * dz), f3(0.5f * (xb - xa) - gap, 0.5f * (yb - ya) - gap, 0.5f * dz), 0.0015f, MAT_GREY, FEAT_PLATE);
        // three hinge knuckles on the left, a quarter-turn latch (or two) on the right
        for (int i = 0; i < 3; ++i) {
            const float yk = ya + 0.1f + (yb - ya - 0.2f) * 0.5f * i;
            with(k, f * Xform::frame(f3(xa + 0.002f, yk, dz), f3(0, 1, 0), f3(1, 0, 0)), MAT_GREY, FEAT_NONE, [&] { k.roundedCylinder(0.0055f, -0.028f, 0.028f, 0.0015f); });
        }
        const int nl = yb - ya > 0.8f ? 2 : 1;
        for (int i = 0; i < nl; ++i) {
            const float yl = nl == 1 ? 0.5f * (ya + yb) : ya + (yb - ya) * (i ? 0.72f : 0.28f);
            with(k, f * Xform::at(f3(xb - 0.03f, yl, dz)), MAT_CHROME, FEAT_NONE, [&] {
                k.roundedCylinder(0.014f, 0.0f, 0.003f, 0.001f, false, true);
                k.roundedCylinder(0.0045f, 0.003f, 0.011f, 0.001f, false, false);
                k.t = k.t * Xform::at(f3(0, 0, 0.0145f));
                k.roundedBox(f3(0.0055f, 0.02f, 0.0045f), 0.002f);
            });
        }
    };
    // the kick plate
    box(k, f, f3(0, 0.05f, 0.002f), f3(hw - 0.012f, 0.045f, 0.003f), 0.0015f, MAT_PAINT_DARK, FEAT_PLATE);
    // the meter panel: the name, meters in their bakelite cases, each over its label
    const float yt0 = h - 0.4f, yt1 = h - m;
    door(-hw + m, hw - m, yt0, yt1);
    plateAt(k, f, f3(0, h - 0.07f, dz), std::min(2.0f * hw * 0.75f, 0.46f), 0.036f, names[ci], PLATE_BLACK);
    const int nm = std::max(2, int((2.0f * hw - 0.12f) / 0.16f));
    static const char* meters[] = {"D.C. VOLTS", "AMPERES", "A.C. VOLTS", "FREQUENCY", "FIELD AMP.", "AMPERES"};
    for (int i = 0; i < nm; ++i) {
        const float xm = -hw + 0.06f + (2.0f * hw - 0.12f) * (i + 0.5f) / nm;
        box(k, f, f3(xm, h - 0.215f, dz + 0.012f), f3(0.052f, 0.052f, 0.012f), 0.004f, MAT_WRINKLE, FEAT_NONE);
        gaugeAt(k, f, f3(xm, h - 0.215f, dz + 0.0245f), 0.041f, 0.008f, MAT_CHROME, DIAL_METER);
        plateAt(k, f, f3(xm, h - 0.292f, dz), 0.07f, 0.014f, meters[(i + ci) % 6], PLATE_BLACK);
    }
    // the switch doors
    const float ya = 0.12f, yb = h - 0.4f - 0.005f;
    const float W = 2.0f * (hw - m);
    const int nd = W > 0.75f ? 2 : 1;
    const std::vector<const char*>& circ = kCircuits[ci];
    int label = 0;
    uint32_t hs = 0x9e3779b9u * uint32_t(ci + 1);
    for (int d = 0; d < nd; ++d) {
        const float xa = -hw + m + W * d / nd, xb = xa + W / nd;
        door(xa, xb, ya, yb);
        const int cols = std::max(1, int((xb - xa - 0.05f) / 0.105f));
        const float sp = (xb - xa - 0.05f) / cols;
        int rows = 0;
        while (yb - 0.085f - rows * 0.13f > 0.47f) ++rows;
        for (int r = 0; r < rows; ++r)
            for (int cc = 0; cc < cols; ++cc) {
                const float x = xa + 0.02f + sp * (cc + 0.5f), y = yb - 0.085f - r * 0.13f;
                hs = hs * 1664525u + 1013904223u;
                if (r == rows - 1) {
                    // a pair of cartridge fuses in their clips on a bakelite base
                    box(k, f, f3(x, y, dz + 0.005f), f3(0.034f, 0.024f, 0.005f), 0.002f, MAT_WRINKLE, FEAT_NONE);
                    for (int fz = -1; fz <= 1; fz += 2) {
                        const Xform ft = f * Xform::frame(f3(x - 0.026f, y + fz * 0.011f, dz + 0.019f), f3(1, 0, 0), f3(0, 0, 1));
                        with(k, ft, MAT_WRINKLE, FEAT_NONE, [&] { k.roundedCylinder(0.0062f, 0.008f, 0.044f, 0.001f); });
                        for (float e : {0.0f, 0.044f})
                            with(k, ft, MAT_BRASS, FEAT_NONE, [&] { k.roundedCylinder(0.007f, e, e + 0.008f, 0.0012f); });
                    }
                    plateAt(k, f, f3(x, y - 0.042f, dz), 0.075f, 0.015f, "FUSES", PLATE_BLACK);
                    continue;
                }
                const bool on = (hs >> 24) % 7 != 0;
                rotarySwitch(k, f, f3(x, y, dz), on ? -0.785f : 0.785f);
                const size_t li = label++, nc = circ.size();
                std::string nm = circ[li % nc];
                if (li >= nc) nm = (li - nc) % 2 == 0 ? std::string("SPARE") : nm + " No.2";
                if (li >= 2 * nc) nm = "SPARE";
                plateAt(k, f, f3(x, y - 0.042f, dz), 0.08f, 0.016f, nm, PLATE_BLACK);
            }
        // the louvre: a dark opening behind slats turned down
        const float lw = 0.5f * (xb - xa) - 0.06f;
        with(k, f * Xform::at(f3(0.5f * (xa + xb), 0.27f, dz + 0.0005f)), MAT_PAINT_DARK, FEAT_NONE, [&] { k.quad(2.0f * lw, 0.18f); });
        for (int s = 0; s < 7; ++s)
            with(k, f * Xform::at(f3(0.5f * (xa + xb), 0.195f + s * 0.025f, dz + 0.0005f)) * Xform::rotX(0.75f), MAT_GREY, FEAT_NONE,
                 [&] { k.roundedBox(f3(lw, 0.011f, 0.0011f), 0.0006f); });
        for (int s = -1; s <= 1; s += 2)
            box(k, f, f3(0.5f * (xa + xb) + s * (lw + 0.004f), 0.27f, dz + 0.004f), f3(0.004f, 0.095f, 0.004f), 0.0015f, MAT_GREY, FEAT_NONE);
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
        // the body: its back corner at the top cut along the hull, clear of the frames' inner flanges (a cabinet stands on
        // its foundation, the frames run on behind it)
        std::vector<float2> prof = {{x0, 0.0f}, {x1, 0.0f}};
        const float rf = kRCork - 0.255f;   // clear of the frames' flanges and the high runs (from the hull's axis)
        const float yc = kAxisY + std::sqrt(std::max(0.0f, rf * rf - x1 * x1));
        if (yc < c.h) {
            const float xc = std::sqrt(std::max(0.0f, rf * rf - (c.h - kAxisY) * (c.h - kAxisY)));
            prof.push_back(make_float2(x1, yc));
            prof.push_back(make_float2(std::max(xc, x0 + 0.1f), c.h));
        } else {
            prof.push_back(make_float2(x1, c.h));
        }
        prof.push_back(make_float2(x0, c.h));
        float area = 0.0f;
        for (size_t i = 0; i < prof.size(); ++i) area += prof[i].x * prof[(i + 1) % prof.size()].y - prof[(i + 1) % prof.size()].x * prof[i].y;
        if (area < 0.0f) std::reverse(prof.begin(), prof.end());
        with(k, Xform::at(f3(0, 0, z0 + 0.01f)), MAT_GREY, FEAT_PLATE, [&] { k.slab(Kit::roundOutline(prof, 0.01f, 3), z1 - z0 - 0.02f, 0.008f); });
        const int ci = int(&c - cabs);
        cabinetFace(k, axes(f3(x0, 0.0f, 0.5f * (z0 + z1)), f3(0, 0, 1), f3(0, 1, 0), f3(-1, 0, 0)), 0.5f * (z1 - z0) - 0.01f, c.h, ci);
    }
}

// ---------------------------------------------------------------------------------------------------
// the hull's services: the piping and conduit that line a submarine's compartment, run fore and aft inboard of the
// frames, clamped at every frame, with flanged joints, unions, the odd valve and drops to the stations

struct Rng {
    uint32_t s;
    float next() {
        s = s * 747796405u + 2891336453u;
        uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
        return float((w >> 22u) ^ w) * (1.0f / 4294967296.0f);
    }
};

// a point on the run: at angle a on the hull (0 starboard, pi/2 up), moved `in` inboard of the cork, at z
float3 runAt(float a, float in, float z) {
    const float r = kRCork - in;
    return f3(r * std::cos(a), kAxisY + r * std::sin(a), z);
}

void serviceRun(Kit& k, Rng& rng, float a, float in, float r, uint32_t mat, float z0, float z1, bool valves) {
    const float3 p0 = runAt(a, in, z0), p1 = runAt(a, in, z1);
    with(k, Xform(), mat, FEAT_NONE, [&] { pipeRun(k, {p0, p1}, r, 0.1f); });
    const float3 radial = normalize(f3(std::cos(a), std::sin(a), 0));   // outwards
    const float3 tang = f3(-std::sin(a), std::cos(a), 0);
    // a clamp at every frame the run crosses: a band round the pipe and a strap out to the frame's flange
    for (int f = 48; f <= 58; ++f) {
        const float z = frameZ(float(f));
        if (z < z0 + 0.05f || z > z1 - 0.05f) continue;
        const float3 c = runAt(a, in, z + 0.06f);
        with(k, Xform::frame(c, f3(0, 0, 1), radial), MAT_CREAM, FEAT_NONE, [&] { k.roundedCylinder(r + 0.004f, -0.012f, 0.012f, 0.002f, false, false); });
        const float strap = std::max(0.0f, in - 0.15f) + r + 0.01f;   // to the frame's inner flange (15 cm inboard)
        with(k, axes(c + radial * (0.5f * strap), tang, radial, f3(0, 0, 1)), MAT_CREAM, FEAT_NONE, [&] { k.roundedBox(f3(0.012f, 0.5f * strap, 0.004f), 0.0015f); });
    }
    // joints: a flanged joint or a union every couple of metres
    for (float z = z0 + 0.9f + rng.next() * 0.8f; z < z1 - 0.4f; z += 1.6f + rng.next() * 1.2f) {
        const float3 c = runAt(a, in, z);
        if (r > 0.014f) {
            with(k, Xform::frame(c, f3(0, 0, 1), radial), mat, FEAT_NONE, [&] { flangePair(k, r, r * 2.4f, 0.008f, 4); });
        } else {
            with(k, Xform::frame(c, f3(0, 0, 1), radial), MAT_BRASS, FEAT_NONE, [&] { k.hexPrism(r * 2.9f, -0.015f, 0.03f, 0.0012f); });
        }
    }
    if (valves) {
        // an in-line valve, its stem inboard
        const float z = z0 + 0.3f + rng.next() * (z1 - z0 - 0.6f);
        Xform vt;
        vt.o = runAt(a, in, z);
        vt.x = f3(0, 0, 1);            // the pipe's axis
        vt.z = -radial;                // the stem inboard
        vt.y = cross(vt.z, vt.x);
        with(k, vt, mat, FEAT_NONE, [&] { globeValve(k, r, std::max(0.07f, r * 6.0f), rng.next() < 0.5f ? MAT_RED : MAT_BRASS); });
    }
}

void conduitRun(Kit& k, Rng& rng, float a, float in, float z0, float z1) {
    const float3 radial = normalize(f3(std::cos(a), std::sin(a), 0));
    const float3 tang = f3(-std::sin(a), std::cos(a), 0);
    with(k, Xform(), MAT_STEEL, FEAT_NONE, [&] { pipeRun(k, {runAt(a, in, z0), runAt(a, in, z1)}, 0.009f, 0.05f); });
    for (float z = z0 + 0.5f + rng.next(); z < z1 - 0.2f; z += 1.8f + rng.next() * 1.5f) {
        // a junction box: grey, a screwed lid
        const float3 c = runAt(a, in - 0.005f, z);
        const Xform t = axes(c, f3(0, 0, 1), tang, -radial);
        with(k, t, MAT_GREY, FEAT_NONE, [&] { k.roundedBox(f3(0.05f, 0.045f, 0.03f), 0.004f); });
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
                with(k, t * Xform::at(f3(sx * 0.038f, sy * 0.033f, 0.03f)), MAT_STEEL, FEAT_NONE, [&] { k.dome(0.004f, 0.0018f); });
    }
}

void hullServices(Kit& k) {
    Rng rng{20260606u};
    const float kPiF = kPi;
    // the high band, both sides: behind the boards, below the overhead's cable runs
    for (const HullRun& r : kHighRuns) serviceRun(k, rng, r.aDeg * kPiF / 180.0f, r.in, r.r, r.mat, 0.06f, kLength - 0.06f, r.valve);
    // the low band at the deck's edge, both sides: the 3" trim lines and drains
    const HullRun low[] = {{186.0f, 0.17f, 0.0445f, MAT_CREAM, true}, {190.0f, 0.2f, 0.016f, MAT_CREAM, false}, {-6.0f, 0.17f, 0.0445f, MAT_CREAM, true},
                           {-10.0f, 0.19f, 0.019f, MAT_CREAM, false}};
    for (const HullRun& r : low) serviceRun(k, rng, r.aDeg * kPiF / 180.0f, r.in, r.r, r.mat, 0.06f, kLength - 0.06f, r.valve);
    // conduits with junction boxes
    for (float aDeg : kConduitDeg) conduitRun(k, rng, aDeg * kPiF / 180.0f, kConduitIn, 0.08f, kLength - 0.08f);
}

// ---------------------------------------------------------------------------------------------------
// the things a crew leaves about

void props(Kit& k) {
    // a CO2 extinguisher on the after bulkhead, starboard of the door (built with x to starboard)
    {
        const float3 b = f3(1.68f, 0.0f, 0.07f + 0.09f);
        with(k, Xform::frame(b + f3(0, 0.62f, 0), f3(0, 1, 0), f3(0, 0, 1)), MAT_RED, FEAT_NONE, [&] {
            k.lathe({{0.0f, 0.0f}, {0.075f, 0.0f}, {0.08f, 0.01f}, {0.08f, 0.52f}, {0.06f, 0.58f}, {0.022f, 0.6f}, {0.0f, 0.6f}});
        });
        with(k, Xform::frame(b + f3(0, 1.22f, 0), f3(0, 1, 0), f3(0, 0, 1)), MAT_BRASS, FEAT_CAST, [&] {
            k.roundedCylinder(0.022f, 0.0f, 0.06f, 0.005f);
            k.t = k.t * Xform::at(f3(0, 0, 0.06f)) * Xform::rotX(0.5f * kPi);
            k.mat = MAT_STEEL;
            k.roundedBox(f3(0.012f, 0.004f, 0.06f), 0.003f);
        });
        with(k, Xform(), MAT_RUBBER, FEAT_NONE, [&] { pipeRun(k, {b + f3(0.02f, 1.26f, 0), b + f3(0.09f, 1.22f, 0.0f), b + f3(0.1f, 0.85f, 0.0f), b + f3(0.06f, 0.75f, 0.0f)}, 0.008f, 0.04f); });
        // the bracket band
        with(k, Xform::frame(b + f3(0, 1.0f, 0), f3(0, 1, 0), f3(0, 0, 1)), MAT_STEEL, FEAT_NONE, [&] { k.roundedCylinder(0.084f, -0.02f, 0.02f, 0.003f, false, false); });
    }
    // a battle lantern (navy grey box, a round lens) on a bracket over the diving station's aft end
    {
        const float3 c = f3(-hullX(1.95f, 1, 0.22f), 1.95f, frameZ(53.4f));
        with(k, Xform::frame(c, f3(1, -0.2f, 0), f3(0, 1, 0)), MAT_GREY, FEAT_PLATE, [&] {
            k.roundedBox(f3(0.07f, 0.09f, 0.08f), 0.01f);
            k.t = k.t * Xform::at(f3(0, 0.02f, 0.08f));
            k.mat = MAT_CHROME;
            k.lathe({{0.055f, -0.004f}, {0.06f, 0.0f}, {0.06f, 0.012f}, {0.05f, 0.014f}});
            k.mat = MAT_DIAL;
            k.ring(0.0f, 0.05f, 0.006f, true);
        });
    }
    // headsets hanging on hooks by the diving station: a band, two earphones, the cord in a loop
    for (int h = 0; h < 2; ++h) {
        const float z = frameZ(51.85f) + h * 0.16f;
        const float3 hook = f3(-hullX(1.72f, 1, 0.31f) + 0.02f, 1.72f, z);
        with(k, Xform::frame(hook, f3(1, 0, 0), f3(0, 1, 0)), MAT_STEEL, FEAT_NONE, [&] { k.roundedCylinder(0.004f, 0.0f, 0.04f, 0.002f, false, true); });
        const float3 c = hook + f3(0.03f, -0.1f, 0);
        with(k, Xform::frame(c, f3(0, 0, 1), f3(0, 1, 0)), MAT_PAINT_DARK, FEAT_NONE, [&] { k.torus(0.085f, 0.006f, 0.15f, kPi - 0.15f); });
        for (int s = -1; s <= 1; s += 2)
            with(k, Xform::frame(c + f3(0, -0.01f, s * 0.085f), f3(0, 0, float(s)), f3(0, 1, 0)), MAT_PAINT_DARK, FEAT_NONE, [&] { k.roundedCylinder(0.035f, 0.0f, 0.03f, 0.008f); });
        std::vector<float3> cord;
        for (int i = 0; i <= 16; ++i) {
            const float t = float(i) / 16.0f;
            cord.push_back(c + f3(0.01f * std::sin(t * 9.0f), -0.04f - 0.42f * std::sin(t * kPi) , 0.085f - 0.12f * t));
        }
        with(k, Xform(), MAT_RUBBER, FEAT_NONE, [&] { pipeRun(k, cord, 0.0035f, 0.02f); });
    }
    // a sound-powered telephone on the forward bulkhead, starboard of the door: a grey box, the black handset on its
    // hooks, the circuit selector, the circuit's plate, the cord hanging in a loop
    {
        const Xform t = Xform::frame(f3(1.72f, 1.32f, kLength - 0.004f), f3(0, 0, -1), f3(0, 1, 0));
        box(k, t, f3(0, 0, 0.045f), f3(0.085f, 0.11f, 0.045f), 0.006f, MAT_GREY, FEAT_PLATE);
        const float hx = -0.022f, hz = 0.104f, hy = 0.015f;
        with(k, t * Xform::frame(f3(hx, hy - 0.085f, hz), f3(0, 1, 0), f3(1, 0, 0)), MAT_WRINKLE, FEAT_NONE, [&] { k.roundedCylinder(0.0105f, 0.0f, 0.17f, 0.004f); });
        for (int e = 0; e < 2; ++e)   // the ear and mouth pieces, facing the box
            with(k, t * Xform::at(f3(hx, hy + (e ? 0.092f : -0.092f), 0.094f)), MAT_WRINKLE, FEAT_NONE, [&] {
                k.lathe({{0.0f, 0.0f}, {0.022f, 0.0f}, {0.025f, 0.006f}, {0.024f, 0.02f}, {0.012f, 0.03f}, {0.0f, 0.031f}});
            });
        for (int s = -1; s <= 1; s += 2)   // the hooks
            box(k, t, f3(hx + s * 0.017f, hy + 0.03f * s, 0.098f), f3(0.004f, 0.008f, 0.009f), 0.0015f, MAT_CHROME, FEAT_NONE);
        rotarySwitch(k, t, f3(0.05f, -0.07f, 0.09f), -0.785f);
        plateAt(k, t, f3(0.05f, 0.065f, 0.09f), 0.05f, 0.02f, "1JV", PLATE_BRASS);
        // the cord: from the handset's foot down in a loop and back into the box's underside
        with(k, t * Xform::at(f3(hx + 0.03f, -0.11f, 0.075f)), MAT_RUBBER, FEAT_NONE, [&] { k.torus(0.03f, 0.0032f, kPi, 2.0f * kPi); });
    }
    // the ship's clock on the forward bulkhead (port of the door), a brass case
    {
        const Xform t = Xform::frame(f3(-0.55f, 1.85f, kLength - 0.03f), f3(0, 0, -1), f3(0, 1, 0));
        gaugeAt(k, t, f3(0, 0, 0.0f), 0.11f, 0.05f, MAT_BRASS, DIAL_COMPASS);
    }
    // the compartment bill holder: a chrome frame with its typed card, on the forward bulkhead
    {
        const Xform t = Xform::frame(f3(-1.35f, 1.35f, kLength - 0.008f), f3(0, 0, -1), f3(0, 1, 0));
        with(k, t, MAT_CHROME, FEAT_NONE, [&] { k.slab(Kit::roundOutline({{-0.11f, -0.15f}, {0.11f, -0.15f}, {0.11f, 0.15f}, {-0.11f, 0.15f}}, 0.006f, 2), 0.004f, 0.0015f); });
        plateAt(k, t, f3(0, 0.0f, 0.0042f), 0.2f, 0.28f, "KONTROL DAİRESİ\nTATBİKAT KARTI\n\nYANGIN · SU ALMA\nTERK · GÖREV YERLERİ", PLATE_STENCIL);
    }
    // an oscillating fan in its wire cage, on a bracket off the overhead over the chart table
    {
        const float3 base = f3(0.25f, 2.28f, frameZ(50.6f));
        with(k, Xform(), MAT_GREY, FEAT_NONE, [&] { pipeRun(k, {base, base + f3(0, -0.18f, 0), base + f3(-0.06f, -0.24f, -0.02f)}, 0.008f, 0.04f); });
        const Xform fan = Xform::frame(base + f3(-0.08f, -0.27f, -0.03f), f3(-0.6f, -0.5f, -0.6f), f3(0, 1, 0));
        with(k, fan, MAT_GREY, FEAT_CAST, [&] { k.roundedCylinder(0.05f, -0.1f, 0.0f, 0.01f); });
        for (int b = 0; b < 4; ++b)
            with(k, fan * Xform::at(f3(0, 0, 0.035f)) * Xform::rotZ(b * 0.5f * kPi + 0.3f) * Xform::rotX(0.35f), MAT_ALU, FEAT_NONE,
                 [&] { k.roundedBox(f3(0.045f, 0.022f, 0.0015f), 0.0012f); k.t = k.t; });
        for (int w = 0; w < 10; ++w) {   // the cage's wires
            const float ang = w * kPi / 10.0f;
            std::vector<float3> pts;
            for (int i = 0; i <= 10; ++i) {
                const float t = -0.5f * kPi + kPi * float(i) / 10.0f;
                pts.push_back(fan.point(f3(0.13f * std::cos(t) * std::cos(ang), 0.13f * std::cos(t) * std::sin(ang), 0.04f + 0.05f * std::sin(t))));
            }
            with(k, Xform(), MAT_CHROME, FEAT_NONE, [&] { pipeRun(k, pts, 0.0013f, 0.004f); });
        }
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
    hullServices(k);
    props(k);
}

}   // namespace dz
