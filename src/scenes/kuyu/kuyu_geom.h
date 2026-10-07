// Scene 10 "Kuyu": the lattice, shared by the host and the kernels. Units: metres, y up.
//
// An endless cubic lattice of cells C metres wide, centred on multiples of C. In the middle of every cell stands a
// room: a closed cube of grey glass (inside half size h) whose walls are one-way mirrors, a silver film on their
// inner face. Every edge of every room runs on as an endless bundle of slats, the tesseract's stretched lines: along
// each axis four lines through every row of rooms. The three families pass one another without touching: a bundle
// along axis a lies outside the room in axis a+1 and inside its span in axis a+2 (a pinwheel round each corner of a
// room), so every face of a room carries two bundles along two of its opposite edges. Between the rooms the lattice
// is open: shafts run down without end, and we fall through one of them.
#pragma once
#include "shared/vec.h"

namespace kuyu {

struct Lattice {
    float C = 8.0f;        // the cell
    float h = 1.8f;        // the room's half size, inside the glass (the silver film)
    float glass = 0.0127f; // the wall: half-inch grey float glass
    float b = 0.6f;        // a bundle's width
    float gap = 0.03f;     // between a bundle and the glass's outer face
    float P = 0.03f;       // the slats' relief: how far a slat may stand out of the bundle's core
    int slats = 7;         // slats across a bundle's face
    float frame = 0.04f;   // the copper frame along the glass's free edges
};

HD float comp(float3 v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
HD void setComp(float3& v, int i, float x) {
    if (i == 0) v.x = x;
    else if (i == 1) v.y = x;
    else v.z = x;
}
HD float3 axisVec(int i, float s) { return make_float3(i == 0 ? s : 0.0f, i == 1 ? s : 0.0f, i == 2 ? s : 0.0f); }

// bundle q (0..11) of a cell: its axis a, the crosswise axes u (where it lies outside the room) and v (inside the
// room's span), the centre of its square in (u, v)
struct BundleShape {
    int a, u, v;
    float cu, cv;
};
HD BundleShape bundleShape(const Lattice& g, int q) {
    BundleShape s;
    s.a = q >> 2;
    s.u = (s.a + 1) % 3;
    s.v = (s.a + 2) % 3;
    const float su = (q & 1) ? -1.0f : 1.0f, sv = (q & 2) ? -1.0f : 1.0f;
    s.cu = su * (g.h + g.glass + g.gap + 0.5f * g.b);
    s.cv = sv * (g.h - 0.5f * g.b);
    return s;
}

// hashing the lattice: the same answer for the same cell, forever. 32-bit arithmetic (64-bit multiplies are slow on
// the card); the cell indices are 64-bit and wrap here only after four billion cells (32 million kilometres of fall)
HD unsigned hash32(unsigned x) {   // lowbias32 (C. Wellons)
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
HD unsigned cellKey(long long i, long long j, long long k) {
    return hash32((unsigned)i * 0x9E3779B1u ^ hash32((unsigned)j * 0x85EBCA77u ^ hash32((unsigned)k * 0xC2B2AE3Du + 0x27D4EB2Fu)));
}
// a bundle's line: the same in every cell along its axis
HD unsigned lineKey(int q, long long i, long long j, long long k) {
    const int a = q >> 2;
    const long long p0 = a == 0 ? j : i, p1 = a == 2 ? j : k;
    return hash32(cellKey(p0, p1, 0x5EED + a) ^ (unsigned)(q & 3) * 0xA24BAED5u);
}
HD float unit(unsigned h, int slot) { return float(hash32(h + (unsigned)slot * 0x9E3779B9u) >> 8) * (1.0f / 16777216.0f); }

// the 48 symmetries of the cube as signed permutations: a room's furniture (a variant) turned and mirrored, so that
// rooms sharing a variant still differ
HD int symPerm(int g, int i) {
    const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    return perms[(g >> 3) % 6][i];
}
HD float3 symApply(int g, float3 w) {   // world (room-local) -> the variant's frame
    float3 r;
    for (int i = 0; i < 3; ++i) setComp(r, i, comp(w, symPerm(g, i)) * (((g >> i) & 1) ? -1.0f : 1.0f));
    return r;
}
HD float3 symInverse(int g, float3 r) {   // the variant's frame -> world
    float3 w = make_float3(0, 0, 0);
    for (int i = 0; i < 3; ++i) setComp(w, symPerm(g, i), comp(r, i) * (((g >> i) & 1) ? -1.0f : 1.0f));
    return w;
}

// the octahedral map of directions (the rooms' mean glow tables)
HD float2 octEncode(float3 n) {
    const float s = fabsf(n.x) + fabsf(n.y) + fabsf(n.z);
    float x = n.x / s, y = n.z / s;
    if (n.y < 0.0f) {
        const float ox = (1.0f - fabsf(y)) * (x >= 0.0f ? 1.0f : -1.0f), oy = (1.0f - fabsf(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = ox;
        y = oy;
    }
    return make_float2(x * 0.5f + 0.5f, y * 0.5f + 0.5f);
}
HD float3 octDecode(float2 e) {
    float x = e.x * 2.0f - 1.0f, z = e.y * 2.0f - 1.0f;
    const float y = 1.0f - fabsf(x) - fabsf(z);
    if (y < 0.0f) {
        const float ox = (1.0f - fabsf(z)) * (x >= 0.0f ? 1.0f : -1.0f), oz = (1.0f - fabsf(x)) * (z >= 0.0f ? 1.0f : -1.0f);
        x = ox;
        z = oz;
    }
    return normalize(make_float3(x, y, z));
}

}   // namespace kuyu
