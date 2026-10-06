// Scene 8's geometry kit: the parts a submarine's control room is made of, built in code with their real measures.
// Every edge is rounded (a hard surface reads as real by the light its edges catch). A part is made in its own frame and
// placed with an Xform; the mesh it adds to carries what the raster path and the oven need:
//   p, n      position and shading normal (metres, world after placement)
//   dn        the direction the surface is displaced along: the normals of every vertex at one position averaged, so
//             tessellated displacement never opens a crack where faces meet
//   t         tangent (xyz) along u, w the bitangent's sign
//   uv        the surface's own coordinates in metres (around x along a tube, across a plate...): the analytic detail
//             (rivets, weld ripples, threads, knurls, tread) is written in them, periodic where the surface closes
//   uv2       the light map's coordinates (filled by the atlas, dz_atlas)
//   mat       material (Mat), feat: which feature program shapes the surface (Feat), and its parameter
#pragma once
#include "shared/vec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dz {

enum Mat : uint32_t {
    MAT_PAINT_HULL = 0,    // the hull's and frames' paint (light enamel)
    MAT_PAINT_PIPE,        // pipes' paint
    MAT_PAINT_DARK,        // black crackle / dark grey instrument paint
    MAT_BRASS,             // bare brass (handwheel hubs, plates, gauge bezels)
    MAT_STEEL,             // bare worked steel (stems, bolts, ladder treads)
    MAT_CHROME,            // chrome plate
    MAT_RUBBER,            // rubber, cable jackets
    MAT_DECK,              // deck plates (painted tread)
    MAT_GLASS,             // gauge glass
    MAT_DIAL,              // gauge faces (printed)
    MAT_RED,               // red handwheels (the vents' colour code)
    MAT_EMISSIVE,          // lamp bulbs
    MAT_COUNT
};

enum Feat : uint32_t {
    FEAT_NONE = 0,
    FEAT_PLATE,     // painted steel plate: orange peel, faint mill scale under paint
    FEAT_WELD,      // weld bead along u (param: bead width, m)
    FEAT_RIVETS,    // domed rivet rows on a plate (param: pitch, m)
    FEAT_THREAD,    // a screw thread around a cylinder (param: pitch, m)
    FEAT_KNURL,     // diamond knurl (handwheel grips)
    FEAT_TREAD,     // diamond tread plate (deck)
    FEAT_CAST,      // sand-cast metal (valve bodies)
    FEAT_BRAID,     // braided / served cable
    FEAT_COUNT
};

struct Vtx {
    float3 p, n, dn;
    float4 t;
    float2 uv, uv2;
    uint32_t mat;
    uint32_t feat;    // Feat
    float2 fp;        // the feature's parameters (pitch, size; a closed surface's circumference in fp.y)
    float edge;       // 0 on a face, 1 on an edge's rounding: where paint wears
    float fmask;      // 1 where the feature shapes the surface, 0 where it does not (a thread stops at the stud's end)
    uint32_t chart;   // the light map chart (dz_atlas)
};

// a placement: origin and three axes (orthonormal, or uniformly scaled)
struct Xform {
    float3 o{0, 0, 0}, x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};
    float3 point(float3 p) const { return o + x * p.x + y * p.y + z * p.z; }
    float3 dir(float3 d) const { return x * d.x + y * d.y + z * d.z; }
    Xform operator*(const Xform& b) const;   // this after b
    static Xform at(float3 o) { Xform t; t.o = o; return t; }
    static Xform rotX(float a), rotY(float a), rotZ(float a);
    static Xform frame(float3 o, float3 z, float3 up);   // z axis along z, x as near the up's perpendicular as can be
};

struct Mesh {
    std::vector<Vtx> v;
    std::vector<uint32_t> idx;
    // a surface piece the light map treats as one chart (ids are per mesh, renumbered on append)
    uint32_t charts = 0;
    void append(const Mesh& m, const Xform& t);
    size_t tris() const { return idx.size() / 3; }
};

// what the parts are made of: a builder that knows the material and feature of what it adds now
struct Kit {
    Mesh& m;
    Xform t;                 // where what is added goes
    uint32_t mat = MAT_PAINT_HULL;
    uint32_t feat = FEAT_NONE;
    float2 fp{0, 0};
    float maxEdge = 0.015f;  // the base mesh's longest edge (m): tessellation divides from here
    explicit Kit(Mesh& mesh) : m(mesh) {}

    // a grid patch: f(i, j) gives vertex (i, j) of an (nu+1) x (nv+1) grid (position, normal, surface coordinates in
    // metres, edge weight in the kit's frame); nu x nv quads, one light map chart
    struct GridPt {
        float3 p, n;
        float2 s;
        float edge = 0.0f;
        float fmask = 1.0f;
    };
    template <class F>
    void grid(int nu, int nv, F f);
    // segments for a length at the kit's maxEdge (at least lo)
    int segs(float length, int lo = 1) const;

    // ---- primitives (in the kit's frame t; z is an axis where there is one)
    // a straight tube from z0 to z1, radius r, ends open
    void tube(float r, float z0, float z1, float uOffset = 0.0f);
    // a bend: a torus quarter etc. Centre line radius R in the xz plane turning from +z towards +x by angle a, tube r
    void bend(float r, float R, float a, float vOffset = 0.0f);
    // a disc ring at z from radius r0 to r1 facing +z (n = +z) or -z
    void ring(float r0, float r1, float z, bool up);
    // a flat-ended cylinder with rounded edges (radius r, from z0 to z1, edge rounding e): bolt heads, hubs, bodies
    void roundedCylinder(float r, float z0, float z1, float e, bool cap0 = true, bool cap1 = true);
    // a hexagonal prism with rounded edges (nuts, bolt heads), across flats s, height h, from z0
    void hexPrism(float s, float z0, float h, float e);
    // a box with rounded edges and corners: half sizes h, rounding e
    void roundedBox(float3 h, float e);
    // a torus (handwheel rims) in the xy plane, centre radius R, tube r
    void torus(float R, float r, float a0 = 0.0f, float a1 = 6.2831853f);
    // a sphere cap / dome: radius r, height above base hb (rivet heads, globes)
    void dome(float r, float hb);
    // a sphere
    void sphere(float r);
    // a part of a cylinder's inside or outside wall (hull plating): radius r, angles a0..a1 about z, z0..z1;
    // inside = normals towards the axis
    void cylinderWall(float r, float a0, float a1, float z0, float z1, bool inside);
    // a flat rectangle in the xy plane (w x h centred), normal +z
    void quad(float w, float h);
    // a swept profile along z: a closed 2D outline (xy, counter-clockwise) extruded from z0 to z1 with rounded long
    // edges already in the outline; ends capped flat (T-bars, angle irons)
    void extrude(const std::vector<float2>& outline, float z0, float z1, bool caps = true);
    // a lathe: a profile in the (r, z) plane, as a polyline from bottom to top (r >= 0; r = 0 at an end closes it),
    // turned about z from a0 to a1. Normals from the profile's direction (outward when walking it with the solid on the
    // left... i.e. walk up the outside). edgeW: per point edge weight (or empty)
    void lathe(const std::vector<float2>& rz, float a0 = 0.0f, float a1 = 6.2831853f, const std::vector<float>& edgeW = {});
    // a 2D outline with rounded corners (radius e) from a polygon (counter-clockwise), n segments per corner
    static std::vector<float2> roundOutline(const std::vector<float2>& poly, float e, int n = 4);
};

// After building: the displacement normals (average over coincident positions) and the tangents
void finishMesh(Mesh& m);

}   // namespace dz
