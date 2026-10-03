// Scene 4 "Laboratuvar": shapes. Glass ware as solids of revolution (a closed profile in (r, y) turned about the y
// axis: the drawn mesh, with rounded rims), the same profile sampled into particles (the solver's container walls),
// the set (table, backdrop), simple meshes (spheres, boxes, a dipper).
#pragma once
#include "scenes/laboratuvar/laboratuvar_renderer.h"
#include "shared/vec.h"

#include <vector>

// a closed profile in the (r, y) half-plane, counter-clockwise around the solid's cross-section; each point says
// whether the outline is smooth through it (rounded rims) or turns a corner there
struct ProfilePoint {
    float r, y;
    bool smooth;
};
using Profile = std::vector<ProfilePoint>;

// a tumbler or a beaker: inner radius at the top, height, wall, base thickness, taper (outer radius at the bottom / at
// the top), a rounded rim (its radius = half the wall)
Profile glassProfile(float innerRadius, float height, float wall, float base, float taper = 1.0f, float fillet = 0.004f);
// fluid points closer than d to any of the wall points removed (a liquid must not be born inside its glass)
void removeNear(std::vector<float3>& fluid, const std::vector<float3>& walls, float d);
// the mesh: the profile turned about y (segments around), smooth normals where the profile is smooth
HostMesh revolve(const Profile& p, int segments);
// the solid's walls as particles: points inside the solid, half a spacing in from its surface where it is thick
// enough (else on its middle), every `step` along the surface; outward normals per point (unused by the solver)
std::vector<float3> sampleProfileWalls(const Profile& p, float spacing, float step);
// the particles' interior (a cylinder of liquid inside a glass): a jittered lattice inside r < radius, y in [y0, y1]
std::vector<float3> fillCylinder(float radius, float y0, float y1, float spacing, unsigned seed);

// the set: a table top (y = 0, a slab with rounded front edge) and a backdrop (a seamless sweep behind it)
HostMesh tableMesh(float halfWidth, float zFront, float zBack, float thickness);
HostMesh backdropMesh(float halfWidth, float zWall, float sweep, float height, float floorFront);
HostMesh sphereMesh(float radius, int rings, int segments);
HostMesh boxMesh(float3 lo, float3 hi);
// a honey dipper: a handle along +y from the origin, a ridged head at the bottom
HostMesh dipperMesh(float headRadius, float headLength, float handleRadius, float handleLength);
// a funnel (glass): a cone from rTop (at yTop) down to rNeck, then a stem of rNeck down to yStem
Profile funnelProfile(float rTop, float rNeck, float coneHeight, float stemLength, float wall);
// a watermelon's shape: an ellipsoid (radii), a lattice of particles inside it, each tagged rind (within `rind` of the
// surface; its outer part dark) or flesh
// isRind: 0 flesh, 1 the white rind, 2 the green skin (within skin of the surface)
void watermelonFill(float3 radii, float rind, float skin, float spacing, unsigned seed, std::vector<float3>& pos, std::vector<uint8_t>& isRind);

// a 3x4 row-major transform: rotation about z by angle (rad) around pivot, then translated
void xfRotateZ(float out[12], float angle, float3 pivot, float3 translate);
float3 xfApply(const float m[12], float3 p);
