// Scene 3 "Yikim": convex polyhedra on the host. Every physical piece (a column segment, a slab panel, a Voronoi
// fragment) is one of these: faces as convex polygons with their planes, clipping by a plane, mass properties.
#pragma once
#include "scenes/yikim/ymath.h"

#include <vector>

struct PolyFace {
    std::vector<double3> v;   // counter-clockwise seen from outside
    double3 n;                // outward unit normal
    double d;                 // plane: dot(n, p) = d
    int tag;                  // 0 original surface, 1 a cut (fracture) face
};

struct Poly {
    std::vector<PolyFace> faces;
    bool empty() const { return faces.size() < 4; }
    std::vector<double3> vertices(double eps = 1e-7) const;   // unique corners
    void bounds(double3& lo, double3& hi) const;
};

// oriented box: centre, half extents along the unit axes ax, ay, az (right-handed)
Poly polyBox(double3 c, double3 half, double3 ax = d3(1, 0, 0), double3 ay = d3(0, 1, 0), double3 az = d3(0, 0, 1));
inline Poly polyAabb(double3 lo, double3 hi) { return polyBox((lo + hi) * 0.5, (hi - lo) * 0.5); }
// keep the half-space dot(n, p) <= d; the new cap face gets tag. Returns false when nothing is left.
bool polyClip(Poly& p, double3 n, double d, int tag = 1);
// a ball of radius r about c: a cube clipped by planes tangent to the sphere in `planes` evenly spread directions
// (its corners stand a few millimetres outside the sphere at r = 0.35 m and 256 planes)
Poly polySphere(double3 c, double r, int planes = 256);

struct MassProps {
    double volume = 0;
    double3 centroid = {0, 0, 0};
    double inertia[3][3] = {};   // about the centroid, per unit density
};
MassProps polyMass(const Poly& p);
// symmetric 3x3 eigen decomposition (Jacobi): vecs columns are the eigenvectors, right-handed
void eigenSym3(const double a[3][3], double vals[3], double vecs[3][3]);
float4 quatFromMatrixColumns(const double m[3][3]);

// helpers
HD double ddot(double3 a, double3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
HD double3 dcross(double3 a, double3 b) { return make_double3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
HD double dlength(double3 a) { return sqrt(ddot(a, a)); }
HD double3 dnormalize(double3 a) { return a * (1.0 / dlength(a)); }
