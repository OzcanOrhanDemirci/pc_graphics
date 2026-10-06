// Scene 9 "Ayna": the maze's geometry, shared by the host (the maze, walking, the light's beams) and the kernels.
//
// A 60° triangular lattice in plan (x, z), y up, the floor y = 0. Every lattice edge inside the maze is a wall
// module: a slab `t` thick from the floor to the ceiling, columns at the lattice vertices, and in the slab a
// horseshoe-arched window. A window is either open (a passage) or holds a two-sided mirror: silver in the slab's
// middle plane, 6 mm of float glass on each side. Mirrors stand on lattice lines, so a reflection maps the lattice
// onto itself: seen in the mirrors the maze goes on as the same lattice of columns and arches, and a mirror and a
// passage look alike (the Luzern labyrinth's trick, 1896).
//
// Cells are the lattice triangles: up U(i,j) = V(i,j) V(i+1,j) V(i,j+1), down D(i,j) = V(i+1,j) V(i+1,j+1) V(i,j+1),
// with V(i,j) = (i + j/2) L x + j h z, h = L sqrt(3)/2. Edge families: 0 along x (line q0 = z/h integer), 1 along
// V(i,j)->V(i,j+1) (q1 integer), 2 along V(i+1,j)->V(i,j+1) (q2 = q0 + q1 integer). A cell owns the half of each of
// its three slabs on its side of the lattice line (the window's reveal up to the glass or the middle plane).
#pragma once
#include "shared/vec.h"

namespace ayna {

constexpr float kSqrt3Half = 0.86602540378f;

enum : unsigned char { kEdgeOut = 0, kEdgeOpen = 1, kEdgeMirror = 2 };

struct Lattice {
    float L = 1.6f, h = 1.6f * kSqrt3Half;   // edge length, the lattice lines' spacing
    float t = 0.12f;                          // slab thickness
    float rc = 0.125f;                        // column radius (>= t covers the slabs' meeting at a vertex)
    float wHalf = 0.615f;                     // the window's half width between the jambs
    float ra = 0.66f, yc = 2.05f, yk = 1.81f; // horseshoe arch: radius, centre height, the jambs' top (where they meet the arc)
    float Hc = 3.4f;                          // ceiling
    float glass = 0.006f;                     // a mirror's glass on each side of the silver
    int i0 = 0, j0 = 0, ni = 1, nj = 1;       // the index box of the maze's cells
};

HD float2 vtx(const Lattice& g, int i, int j) { return make_float2((float(i) + 0.5f * float(j)) * g.L, float(j) * g.h); }

// the cell holding a plan point
HD void locate(const Lattice& g, float x, float z, int& i, int& j, int& up) {
    const float q0 = z / g.h, q1 = (x * kSqrt3Half - z * 0.5f) / g.h;
    j = int(floorf(q0));
    i = int(floorf(q1));
    up = int(floorf(q0 + q1)) == i + j ? 1 : 0;
}

HD float2 famNormal(int fam) {
    return fam == 0 ? make_float2(0.0f, 1.0f) : fam == 1 ? make_float2(kSqrt3Half, -0.5f) : make_float2(kSqrt3Half, 0.5f);
}
HD float2 famDir(int fam) {
    return fam == 0 ? make_float2(1.0f, 0.0f) : fam == 1 ? make_float2(0.5f, kSqrt3Half) : make_float2(-0.5f, kSqrt3Half);
}

// a cell's k-th edge: family, index and the side the cell is on (+1: where the line coordinate grows)
struct EdgeRef {
    int fam, i, j, side;
};
HD EdgeRef cellEdge(int i, int j, int up, int k) {
    if (up) {
        if (k == 0) return {0, i, j, +1};
        if (k == 1) return {1, i, j, +1};
        return {2, i, j, -1};
    }
    if (k == 0) return {0, i, j + 1, -1};
    if (k == 1) return {1, i + 1, j, -1};
    return {2, i, j, +1};
}
// the cell across a cell's k-th edge
HD void cellAcross(int i, int j, int up, int k, int& ni, int& nj, int& nup) {
    if (up) {
        nup = 0;
        if (k == 0) { ni = i; nj = j - 1; }
        else if (k == 1) { ni = i - 1; nj = j; }
        else { ni = i; nj = j; }
    } else {
        nup = 1;
        if (k == 0) { ni = i; nj = j + 1; }
        else if (k == 1) { ni = i + 1; nj = j; }
        else { ni = i; nj = j; }
    }
}
// an edge's first and second vertex (u grows from A to B along famDir)
HD void edgeEnds(const Lattice& g, int fam, int i, int j, float2& A, float2& B) {
    if (fam == 0) { A = vtx(g, i, j); B = vtx(g, i + 1, j); }
    else if (fam == 1) { A = vtx(g, i, j); B = vtx(g, i, j + 1); }
    else { A = vtx(g, i + 1, j); B = vtx(g, i, j + 1); }
}
// a cell's three corners
HD void cellCorners(const Lattice& g, int i, int j, int up, float2 c[3]) {
    if (up) { c[0] = vtx(g, i, j); c[1] = vtx(g, i + 1, j); c[2] = vtx(g, i, j + 1); }
    else { c[0] = vtx(g, i + 1, j); c[1] = vtx(g, i + 1, j + 1); c[2] = vtx(g, i, j + 1); }
}
HD void cellCornerIdx(int i, int j, int up, int ci[3], int cj[3]) {
    if (up) { ci[0] = i; cj[0] = j; ci[1] = i + 1; cj[1] = j; ci[2] = i; cj[2] = j + 1; }
    else { ci[0] = i + 1; cj[0] = j; ci[1] = i + 1; cj[1] = j + 1; ci[2] = i; cj[2] = j + 1; }
}

// the window in the slab's own coordinates: du from the edge's middle, y up
HD bool inWindow(const Lattice& g, float du, float y) {
    if (y < -1e-4f) return false;   // (a ray leaving the floor starts at y = 0 within rounding)
    if (fabsf(du) <= g.wHalf && y <= g.yk) return true;
    return y >= g.yk && du * du + sqr(y - g.yc) <= g.ra * g.ra;
}
// the window's widest half extent (the horseshoe's bulge)
HD float windowHalfMax(const Lattice& g) { return fmaxf(g.wHalf, g.ra); }

// storage: edge types and cells inside the maze, by index box
HD int edgeIndex(const Lattice& g, int fam, int i, int j) {
    const int ii = i - g.i0, jj = j - g.j0;
    if (ii < 0 || jj < 0 || ii > g.ni || jj > g.nj) return -1;
    return (fam * (g.nj + 1) + jj) * (g.ni + 1) + ii;
}
HD int edgeCount(const Lattice& g) { return 3 * (g.nj + 1) * (g.ni + 1); }
HD int cellIndex(const Lattice& g, int i, int j, int up) {
    const int ii = i - g.i0, jj = j - g.j0;
    if (ii < 0 || jj < 0 || ii >= g.ni || jj >= g.nj) return -1;
    return (jj * g.ni + ii) * 2 + (up ? 0 : 1);
}
HD int cellCount(const Lattice& g) { return g.ni * g.nj * 2; }
HD int vertexIndex(const Lattice& g, int i, int j) {
    const int ii = i - g.i0, jj = j - g.j0;
    if (ii < 0 || jj < 0 || ii > g.ni + 1 || jj > g.nj + 1) return -1;
    return jj * (g.ni + 2) + ii;
}
HD int vertexCount(const Lattice& g) { return (g.ni + 2) * (g.nj + 2); }

}   // namespace ayna
