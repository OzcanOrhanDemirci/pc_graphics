#include "scenes/denizalti/dz_world.h"
#include "scenes/denizalti/dz_parts.h"

#include <cmath>

namespace dz {

namespace {
const float kPi = 3.14159265358979f;
float3 f3(float x, float y, float z) { return make_float3(x, y, z); }
}   // namespace

Start buildTestWall(Mesh& m, float maxEdge) {
    Kit k(m);
    k.maxEdge = maxEdge;
    const float R = 2.44f;          // the pressure hull's inside radius (16 ft diameter)
    const float axisY = 0.95f;      // the hull's axis above the deck
    const Xform hull = Xform::at(f3(0, axisY, 0));
    // the port side: angles about the axis (0 = starboard, pi/2 = up), from overhead round to the deck's edge
    const float a0 = 1.95f, a1 = 3.55f, z0 = -1.25f, z1 = 1.25f;
    k.t = hull;
    hullWall(k, R, a0, a1, z0, z1);
    for (int i = 0; i < 4; ++i) ringFrame(k, R, a0, a1, -1.14f + 0.76f * i, 0.16f, 0.012f, 0.10f, 0.014f);

    // the deck: tread plates, 1.2 m x 0.6 m, from the wall's foot inwards
    for (int ix = 0; ix < 3; ++ix)
        for (int iz = 0; iz < 4; ++iz) {
            k.t = Xform::at(f3(-2.05f + 0.61f * ix, 0.0f, -0.92f + 0.61f * iz));
            deckPlate(k, 0.6f, 0.6f, 0.008f);
        }

    // the manifold: a 2" header along z, five globe valves on risers, their pipes going up the wall
    const float hx = -2.02f, hy = 1.25f;
    k.t = Xform();
    k.mat = MAT_PAINT_PIPE;
    k.feat = FEAT_NONE;
    pipeRun(k, {f3(hx, hy, -1.0f), f3(hx, hy, 1.0f)}, 0.030f, 0.09f);
    // the header's end caps
    k.t = Xform::frame(f3(hx, hy, 1.0f), f3(0, 0, 1), f3(0, 1, 0));
    k.roundedCylinder(0.036f, 0.0f, 0.025f, 0.004f);
    k.t = Xform::frame(f3(hx, hy, -1.0f), f3(0, 0, -1), f3(0, 1, 0));
    k.roundedCylinder(0.036f, 0.0f, 0.025f, 0.004f);
    for (int v = 0; v < 5; ++v) {
        const float z = -0.72f + 0.36f * v;
        // the riser from the header's top to the valve
        k.t = Xform();
        k.mat = MAT_PAINT_PIPE;
        pipeRun(k, {f3(hx, hy + 0.02f, z), f3(hx, hy + 0.165f, z)}, 0.017f, 0.05f);
        // the valve: its pipe axis vertical, the stem towards the room (+x)
        Xform vt;
        vt.o = f3(hx, hy + 0.2f, z);
        vt.x = f3(0, 1, 0);
        vt.y = f3(0, 0, 1);
        vt.z = f3(1, 0, 0);
        k.t = vt;
        k.mat = MAT_PAINT_PIPE;
        globeValve(k, 0.017f, 0.15f, v == 2 ? MAT_RED : MAT_BRASS);
        // the outlet: up, a bend towards the wall, up along it
        k.t = Xform();
        k.mat = MAT_PAINT_PIPE;
        pipeRun(k, {f3(hx, hy + 0.235f, z), f3(hx, hy + 0.55f, z), f3(-2.3f, hy + 0.75f, z), f3(-2.3f, 2.6f, z)}, 0.017f, 0.06f);
        // a flange pair on the outlet's first straight
        k.t = Xform::frame(f3(hx, hy + 0.4f, z), f3(0, 1, 0), f3(1, 0, 0));
        k.mat = MAT_PAINT_PIPE;
        flangePair(k, 0.017f, 0.048f, 0.012f, 4);
    }
    // the header's supply from below, with a flange
    k.t = Xform();
    k.mat = MAT_PAINT_PIPE;
    pipeRun(k, {f3(hx, hy - 0.03f, 0.9f), f3(hx, 0.6f, 0.9f), f3(-2.2f, 0.35f, 0.9f), f3(-2.2f, 0.02f, 0.9f)}, 0.030f, 0.1f);
    k.t = Xform::frame(f3(hx, 0.95f, 0.9f), f3(0, -1, 0), f3(1, 0, 0));
    flangePair(k, 0.030f, 0.076f, 0.016f, 6);

    // a gauge board above the manifold, two gauges
    k.t = Xform::frame(f3(-2.24f, 2.05f, -0.2f), f3(1, 0, 0), f3(0, 1, 0));
    k.mat = MAT_PAINT_HULL;
    k.feat = FEAT_PLATE;
    k.roundedBox(f3(0.34f, 0.16f, 0.008f), 0.004f);
    k.feat = FEAT_NONE;
    for (int g = 0; g < 2; ++g) {
        Xform gt = Xform::frame(f3(-2.24f + 0.012f + 0.035f, 2.05f, -0.38f + 0.36f * g), f3(1, 0, 0), f3(0, 1, 0));
        k.t = gt;
        gauge(k, 0.062f, 0.045f);
    }

    // a big 3" line low along the wall, threaded through the frames
    k.t = Xform();
    k.mat = MAT_PAINT_PIPE;
    pipeRun(k, {f3(-2.12f, 0.42f, -1.25f), f3(-2.12f, 0.42f, 1.25f)}, 0.0445f, 0.15f);
    (void)kPi;
    finishMesh(m);
    return Start{f3(-0.9f, 1.62f, -0.15f), -1.35f, -0.08f};
}

}   // namespace dz
