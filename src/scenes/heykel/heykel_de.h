// Scene 6 "Heykel": the sculptures as distance estimates, shared by the CUDA kernels and the host (picking under the
// cursor, fitting a candidate onto its plinth). A sculpture is a fractal (fractal space, its own units) cut by a simple
// solid, placed in the world (metres) on a plinth over a dark floor.
#pragma once
#include "shared/vec.h"

namespace hk {

enum Fractal { kBox = 0, kKleinian, kApollonian, kCrystal, kBulb, kHybrid, kTetra };

struct M3 {   // rows
    float3 r0{1, 0, 0}, r1{0, 1, 0}, r2{0, 0, 1};
};
HD float3 mul(const M3& m, float3 v) { return make_float3(dot(m.r0, v), dot(m.r1, v), dot(m.r2, v)); }

// one candidate: the fractal and its knobs, the cut, where it stands
struct Shape {
    int fractal = kBox;
    int iters = 12;
    float a = 2.0f, b = 0.25f, c = 1.0f;   // the fractal's own knobs (see each estimate)
    float3 k{1, 1, 1};                     // a vector knob (the Kleinian's box, the Menger's offset)
    M3 fold;                               // a rotation inside the iteration (crystal, hybrid, tetra: before the folds)
    M3 fold2;                              // tetra: after the folds
    int cut = 0;                           // 0 none, 1 sphere (cutSize.x), 2 box (cutSize), 3 upright cylinder (x radius, y half height),
                                           // 4 half space: what lies below the plane through cutSize * cutOffset with normal cutSize
    float3 cutSize{1, 1, 1};
    float cutOffset = 0.0f;
    int julia = 0;                         // box: 1 adds the constant k instead of the point (a Julia set: bounded, not a cube)
    // the floors (Kleinian): from iteration split on, the box k2 and the inversion a2 (a later iteration draws finer
    // structure, so a deeper floor gets its own architecture while the shallower ones stay as they are)
    int split = 99;
    float3 k2{1, 1, 1};
    float a2 = 1.0f;
    float relax = 0.9f;                    // step factor: the estimates of some fractals overshoot a little
    // fractal space -> world: world = center + toWorld(p) * scale
    M3 orient;                             // world -> fractal rotation (rows)
    float3 center{0, 0.2f, 0};
    float scale = 0.1f;
    float boundR = 2.0f;                   // a sphere around it in fractal space (outside it the sphere is the estimate)
    float floorCut = 0.0f;                 // world y below which the sculpture is cut flat (it stands on the plinth)
};

// The deep zoom (10,000 times: a pixel is a few float ulps of the position) by perturbation, as the deep Mandelbrot
// zooms do: one anchor's orbit through the iteration is computed in doubles on the host, every pixel follows it as a
// small difference in floats. The folds and the inversion are piecewise rational, so the difference has an exact
// formula on every branch; where a pixel takes another branch than the anchor, the precise distances to the branch's
// edge (C - A, |A|² - size: from the doubles) keep it exact.
const int kMaxSteps = 24;
struct KStep {          // the anchor, one iteration
    float3 toPos, toNeg;   // C - A and -C - A per axis, A the anchor before the fold
    float3 Af;             // the anchor after the fold
    float r2, r2m;         // |Af|² and |Af|² - size
    float size;            // this iteration's inversion size
    int bx, by, bz;        // the anchor's fold branch per axis: -1, 0, 1
    float3 An;             // the anchor after this iteration (where a point that stops here ends)
};

// The Menger sponge's anchor, one iteration (the same idea for its folds: abs, three sorting swaps, scale, a step on
// z): the anchor's values before the branches and the differences that decide them, from the doubles
struct MStep {
    float3 A;              // the anchor before the iteration (abs: a point crosses only where |A| is as small as it)
    float D1, D2, D3;      // the sort's comparisons for the anchor: x - y, then x - z, then y - z (each after the last swap)
    float E;               // the anchor's z + 1 after the scale (the step on z happens below 0)
    int flags;             // bits 0-2: the anchor's axis was negative; 3-5: it swapped; 6: it took the step
    float3 An;             // the anchor after the iteration
};

struct World {
    Shape s;
    float3 plinthC{0, -0.5f, 0}, plinthH{0.17f, 0.5f, 0.17f};   // the plinth's centre and half size; its top at y = 0
    float floorY = -1.0f;
    // The frame's anchor: every position the estimates see is an offset from it, in metres. Far things use the offsets
    // to them below (from doubles); the Kleinian, when pert, its orbit.
    float3 plinthRel{0, 0.5f, 0};   // anchor - plinth centre
    float floorRel = 1.0f;          // anchor.y - floorY
    float floorCutRel = 0.0f;       // anchor.y - s.floorCut
    float3 centerRel{0, -0.2f, 0};  // anchor - s.center
    int pert = 0, n = 0;
    float3 A0{0, 0, 0}, AN{0, 0, 0};   // the anchor in fractal space, before the first iteration and after the last
    float3 cutRel{0, 0, 0}, cutSign{1, 1, 1};   // |A0| - the cut box's half size, sign(A0), per axis
    const KStep* st = nullptr;      // the orbit: n steps (device memory for the kernels, host memory for the host's
                                    // own estimates; a kernel parameter array indexed in a loop spills to local memory)
    const MStep* mst = nullptr;     // the Menger's orbit (pert with fractal kCrystal)
    float cutPlaneRel = 0.0f;       // dot(A0, plane normal) - offset, from the doubles (the half-space cut)
    // The pixel rule for the formula, point by point (Kleinian): a point r from the camera takes
    // lodBase + lodK log10(lodRef / r) iterations, the fraction blended between two counts (no seam, no popping). A
    // pixel's footprint grows with r, so the far layers get no lace finer than a pixel: calmer, and cheaper.
    int lod = 0;
    float3 camRel{0, 0, 0};         // the camera, as an offset from the anchor
    float lodRef = 1.0f, lodK = 2.0f, lodBase = 10.0f, lodMin = 6.0f;
};

// the iterations a point o (offset from the anchor) takes: its distance from the camera, by the pixel rule
HD float itersAt(float3 o, const World& w, int most) {
    const float3 d = o - w.camRel;
    const float r = fmaxf(sqrtf(dot(d, d)), 1e-20f);
    return clampf(w.lodBase + w.lodK * log10f(w.lodRef / r), w.lodMin, float(most) - 1.001f);
}

HD float fractf(float x) { return x - floorf(x); }
HD float sdBox(float3 p, float3 h) {
    const float3 q = make_float3(fabsf(p.x) - h.x, fabsf(p.y) - h.y, fabsf(p.z) - h.z);
    return length(fmaxf3(q, make_float3(0, 0, 0))) + fminf(fmaxf(q.x, fmaxf(q.y, q.z)), 0.0f);
}

// Mandelbox (Tom Lowe): box fold, sphere fold, scale a; b = min radius², c = fixed radius²
HD float deBox(float3 p, const Shape& s) {
    float3 z = p;
    float dr = 1.0f;
    const float scale = s.a, minR2 = s.b, fixR2 = s.c;
    const float3 add = s.julia ? s.k : p;
    const float addDr = s.julia ? 0.0f : 1.0f;
    for (int i = 0; i < s.iters; ++i) {
        z = make_float3(clampf(z.x, -1.0f, 1.0f) * 2.0f - z.x, clampf(z.y, -1.0f, 1.0f) * 2.0f - z.y, clampf(z.z, -1.0f, 1.0f) * 2.0f - z.z);
        const float r2 = dot(z, z);
        if (r2 < minR2) {
            const float t = fixR2 / minR2;
            z *= t;
            dr *= t;
        } else if (r2 < fixR2) {
            const float t = fixR2 / r2;
            z *= t;
            dr *= t;
        }
        z = z * scale + add;
        dr = dr * fabsf(scale) + addDr;
        if (dot(z, z) > 1e6f) break;
    }
    return length(z) / fabsf(dr);
}

// the hybrid: a Mandelbox whose every step first turns the space (fold), so its rooms do not line up
HD float deHybrid(float3 p, const Shape& s) {
    float3 z = p;
    float dr = 1.0f;
    const float scale = s.a, minR2 = s.b, fixR2 = s.c;
    for (int i = 0; i < s.iters; ++i) {
        z = mul(s.fold, z);
        z = make_float3(clampf(z.x, -1.0f, 1.0f) * 2.0f - z.x, clampf(z.y, -1.0f, 1.0f) * 2.0f - z.y, clampf(z.z, -1.0f, 1.0f) * 2.0f - z.z);
        const float r2 = dot(z, z);
        if (r2 < minR2) {
            const float t = fixR2 / minR2;
            z *= t;
            dr *= t;
        } else if (r2 < fixR2) {
            const float t = fixR2 / r2;
            z *= t;
            dr *= t;
        }
        z = z * scale + p;
        dr = dr * fabsf(scale) + 1.0f;
        if (dot(z, z) > 1e6f) break;
    }
    return length(z) / fabsf(dr);
}

// pseudo-Kleinian (Knighty): box fold against k, inversion of size a, a thin "thingy" at the end (b its radius)
HD float deKleinian(float3 p, const Shape& s) {
    float df = 1.0f;
    for (int i = 0; i < s.iters; ++i) {
        const float3 c = i < s.split ? s.k : s.k2;
        p = make_float3(2.0f * clampf(p.x, -c.x, c.x) - p.x, 2.0f * clampf(p.y, -c.y, c.y) - p.y, 2.0f * clampf(p.z, -c.z, c.z) - p.z);
        const float r2 = dot(p, p);
        const float k = fmaxf((i < s.split ? s.a : s.a2) / r2, 1.0f);
        p *= k;
        df *= k;
    }
    const float rxy = sqrtf(p.x * p.x + p.y * p.y);
    return fmaxf(rxy - s.b, fabsf(rxy * p.z) / length(p)) / df;
}

// the Kleinian's end: the thin disc the last iteration leaves, over the scale the iterations gathered
HD float kleinianEnd(float3 z, float df, const Shape& s) {
    const float rxy = sqrtf(z.x * z.x + z.y * z.y);
    return fmaxf(rxy - s.b, fabsf(rxy * z.z) / length(z)) / df;
}

// the same, stopped after mf iterations (fractional: blended between floor and floor + 1)
HD float deKleinianLod(float3 p, const Shape& s, float mf) {
    const int m0 = int(mf);
    const float f = mf - float(m0);
    float df = 1.0f, d0 = 0.0f;
    for (int i = 0; i <= m0; ++i) {
        if (i == m0) d0 = kleinianEnd(p, df, s);
        const float3 c = i < s.split ? s.k : s.k2;
        p = make_float3(2.0f * clampf(p.x, -c.x, c.x) - p.x, 2.0f * clampf(p.y, -c.y, c.y) - p.y, 2.0f * clampf(p.z, -c.z, c.z) - p.z);
        const float k = fmaxf((i < s.split ? s.a : s.a2) / dot(p, p), 1.0f);
        p *= k;
        df *= k;
    }
    return d0 + (kleinianEnd(p, df, s) - d0) * f;
}

// Apollonian packing (after Inigo Quilez): a lattice of sphere inversions, a its strength
HD float deApollonian(float3 p, const Shape& s) {
    float scale = 1.0f;
    for (int i = 0; i < s.iters; ++i) {
        p = make_float3(-1.0f + 2.0f * fractf(0.5f * p.x + 0.5f), -1.0f + 2.0f * fractf(0.5f * p.y + 0.5f),
                        -1.0f + 2.0f * fractf(0.5f * p.z + 0.5f));
        const float r2 = dot(p, p);
        const float k = s.a / r2;
        p *= k;
        scale *= k;
    }
    return 0.25f * fabsf(p.y) / scale;
}

// Menger sponge as a folding system with a turn in each step (Knighty, Syntopia): scale a (3), offset k
HD float deCrystal(float3 z, const Shape& s) {
    float sc = 1.0f;
    for (int i = 0; i < s.iters; ++i) {
        z = mul(s.fold, z);
        z = make_float3(fabsf(z.x), fabsf(z.y), fabsf(z.z));
        if (z.x < z.y) { const float t = z.x; z.x = z.y; z.y = t; }
        if (z.x < z.z) { const float t = z.x; z.x = z.z; z.z = t; }
        if (z.y < z.z) { const float t = z.y; z.y = z.z; z.z = t; }
        z = z * s.a - s.k * (s.a - 1.0f);
        if (z.z < -0.5f * s.k.z * (s.a - 1.0f)) z.z += s.k.z * (s.a - 1.0f);
        sc *= s.a;
    }
    return sdBox(z, make_float3(1, 1, 1)) / sc;
}

// The Menger sponge (scale 3, offset 1, no turn) to a fractional level mf: floor(mf) whole levels by the folds, then
// in the local cube [-1, 1]³ the next level's cross of tunnels carved with half-width frac(mf) / 3. The newest holes
// open as true square tunnels growing from their centres, so every in-between shape is a Menger sponge (blending the
// two levels' distances instead made stars turn into triangles and filled holes with dents). The pixel rule picks mf:
// no hole smaller than a few pixels is drawn, so the faces stay clean, and none pops in.
HD float mengerCarve(float3 z, float f) {
    const float3 a = make_float3(fabsf(z.x), fabsf(z.y), fabsf(z.z));
    const float dc = fminf(fmaxf(a.x, a.y), fminf(fmaxf(a.y, a.z), fmaxf(a.z, a.x))) - f * (1.0f / 3.0f);
    return fmaxf(sdBox(z, make_float3(1, 1, 1)), -dc);
}
HD float deMengerLod(float3 z, float mf) {
    const int m0 = int(mf);
    float sc = 1.0f;
    for (int i = 0; i < m0; ++i) {
        z = make_float3(fabsf(z.x), fabsf(z.y), fabsf(z.z));
        if (z.x < z.y) { const float t = z.x; z.x = z.y; z.y = t; }
        if (z.x < z.z) { const float t = z.x; z.x = z.z; z.z = t; }
        if (z.y < z.z) { const float t = z.y; z.y = z.z; z.z = t; }
        z = z * 3.0f - make_float3(2, 2, 2);
        if (z.z < -1.0f) z.z += 2.0f;
        sc *= 3.0f;
    }
    return mengerCarve(z, mf - float(m0)) / sc;
}

// one sorting swap of a pair (a, b) as a difference from the anchor's: D the anchor's a - b, sA whether it swapped
HD void swapDelta(float& da, float& db, float D, bool sA) {
    const bool sP = D + (da - db) < 0.0f;
    if (sA == sP) {
        if (sA) { const float t = da; da = db; db = t; }
    } else if (sA) {   // the anchor swapped, the point did not
        const float a = D + da, b = -D + db;
        da = a;
        db = b;
    } else {           // the point swapped, the anchor did not
        const float a = -D + db, b = D + da;
        da = a;
        db = b;
    }
}
// one axis of abs as a difference: a the anchor's value (where they part, |a| is as small as d)
HD float absDelta(float d, float a, bool negA) {
    const bool negP = a + d < 0.0f;
    if (negP == negA) return negA ? -d : d;
    return negA ? (2.0f * a + d) : -(2.0f * a + d);
}

// the Menger by perturbation around the anchor (fractal units), stopped after mf iterations (as deMengerLod)
HD float deMengerPert(float3 d, const World& w, float mf) {
    const int m0 = int(mf);
    float sc = 1.0f;
    for (int i = 0; i < m0; ++i) {
        const MStep& k = w.mst[i];
        d = make_float3(absDelta(d.x, k.A.x, k.flags & 1), absDelta(d.y, k.A.y, k.flags & 2), absDelta(d.z, k.A.z, k.flags & 4));
        swapDelta(d.x, d.y, k.D1, k.flags & 8);
        swapDelta(d.x, d.z, k.D2, k.flags & 16);
        swapDelta(d.y, d.z, k.D3, k.flags & 32);
        d = d * 3.0f;
        const bool stepA = k.flags & 64, stepP = k.E + d.z < 0.0f;
        if (stepA && !stepP) d.z -= 2.0f;
        else if (stepP && !stepA) d.z += 2.0f;
        sc *= 3.0f;
    }
    return mengerCarve(w.mst[m0].A + d, mf - float(m0)) / sc;
}

// Sierpinski tetrahedron as a kaleidoscopic folding system (Knighty) with a turn before and after the folds:
// scale a, offset k
HD float deTetra(float3 z, const Shape& s) {
    float sc = 1.0f;
    for (int i = 0; i < s.iters; ++i) {
        z = mul(s.fold, z);
        if (z.x + z.y < 0.0f) { const float t = -z.y; z.y = -z.x; z.x = t; }
        if (z.x + z.z < 0.0f) { const float t = -z.z; z.z = -z.x; z.x = t; }
        if (z.y + z.z < 0.0f) { const float t = -z.z; z.z = -z.y; z.y = t; }
        z = mul(s.fold2, z);
        z = z * s.a - s.k * (s.a - 1.0f);
        sc *= s.a;
    }
    return (length(z) - 1.5f) / sc;
}

// Mandelbulb (White, Nylander), power a. Far out the iteration's estimate overshoots: the set lies inside r < 1.2
HD float deBulb(float3 p, const Shape& s) {
    if (dot(p, p) > 4.0f) return length(p) - 1.25f;
    float3 z = p;
    float dr = 1.0f, r = length(z);
    for (int i = 0; i < s.iters; ++i) {
        r = length(z);
        if (r > 2.0f) break;
        const float theta = acosf(clampf(z.z / fmaxf(r, 1e-20f), -1.0f, 1.0f)) * s.a;
        const float phi = atan2f(z.y, z.x) * s.a;
        dr = powf(r, s.a - 1.0f) * s.a * dr + 1.0f;
        const float zr = powf(r, s.a);
        z = make_float3(sinf(theta) * cosf(phi), sinf(phi) * sinf(theta), cosf(theta)) * zr + p;
    }
    r = length(z);
    return 0.5f * logf(fmaxf(r, 1e-20f)) * r / dr;
}

HD float deFractal(float3 p, const Shape& s) {
    float d;
    switch (s.fractal) {
    case kKleinian: d = deKleinian(p, s); break;
    case kApollonian: d = deApollonian(p, s); break;
    case kCrystal: d = deCrystal(p, s); break;
    case kBulb: d = deBulb(p, s); break;
    case kHybrid: d = deHybrid(p, s); break;
    case kTetra: d = deTetra(p, s); break;
    default: d = deBox(p, s); break;
    }
    if (s.cut == 1) d = fmaxf(d, length(p) - s.cutSize.x);
    else if (s.cut == 2) d = fmaxf(d, sdBox(p, s.cutSize));
    else if (s.cut == 3) d = fmaxf(d, fmaxf(sqrtf(p.x * p.x + p.z * p.z) - s.cutSize.x, fabsf(p.y) - s.cutSize.y));
    else if (s.cut == 4) d = fmaxf(d, dot(p, normalize(s.cutSize)) - s.cutOffset);
    return d;
}

// one axis of the box fold, as a difference from the anchor's fold (see KStep)
HD float foldDelta(float d, float toPos, float toNeg, int bA) {
    const int bp = d > toPos ? 1 : (d < toNeg ? -1 : 0);
    if (bp == bA) return bA == 0 ? d : -d;
    if (bA == 0) return (bp == 1 ? 2.0f * toPos : 2.0f * toNeg) - d;
    if (bp == 0) return d - (bA == 1 ? 2.0f * toPos : 2.0f * toNeg);
    return (bp == 1 ? 2.0f : -2.0f) * (toPos - toNeg) - d;   // the opposite sides: 4C - d or -4C - d
}

// the pseudo-Kleinian by perturbation around the anchor (fractal units in, fractal units out), stopped after mf
// iterations (fractional, as deKleinianLod)
HD float deKleinianPert(float3 d, const World& w, float mf) {
    const Shape& s = w.s;
    const int m0 = int(mf);
    const float f = mf - float(m0);
    float df = 1.0f, d0 = 0.0f;
    for (int i = 0; i <= m0; ++i) {
        const KStep& k = w.st[i];
        if (i == m0) d0 = kleinianEnd((i == 0 ? w.A0 : w.st[i - 1].An) + d, df, s);
        d = make_float3(foldDelta(d.x, k.toPos.x, k.toNeg.x, k.bx), foldDelta(d.y, k.toPos.y, k.toNeg.y, k.by),
                        foldDelta(d.z, k.toPos.z, k.toNeg.z, k.bz));
        const float ad = 2.0f * dot(k.Af, d) + dot(d, d);   // |z|² - |Af|²
        const float r2z = k.r2 + ad, r2mz = k.r2m + ad;
        const bool invZ = r2mz < 0.0f, invA = k.r2m < 0.0f;
        const float kz = invZ ? k.size / r2z : 1.0f;
        float dk = 0.0f;   // kz - kA, without cancelling
        if (invZ && invA) dk = -k.size * ad / (r2z * k.r2);
        else if (invZ) dk = -r2mz / r2z;
        else if (invA) dk = k.r2m / k.r2;
        d = d * kz + k.Af * dk;
        df *= kz;
    }
    return d0 + (kleinianEnd(w.st[m0].An + d, df, s) - d0) * f;
}

// the sculpture: o is an offset from the frame's anchor (metres); outside its bounding sphere the sphere is the
// estimate (a lower bound)
HD float deSculpture(float3 o, const World& w) {
    const Shape& s = w.s;
    const float3 q = o + w.centerRel;
    const float rw = s.boundR * s.scale;
    const float bd = length(q) - rw;
    float d;
    if (bd > 0.05f * rw) {
        d = bd;
    } else if (w.pert && s.fractal == kCrystal) {
        const float3 d0 = mul(s.orient, o) * (1.0f / s.scale);   // from the anchor, fractal units
        d = deMengerPert(d0, w, w.lod ? itersAt(o, w, w.n) : float(w.n) - 1.001f);
        if (s.cut == 4) d = fmaxf(d, w.cutPlaneRel + dot(d0, normalize(s.cutSize)));
        d *= s.scale;
    } else if (w.pert) {
        const float3 d0 = mul(s.orient, o) * (1.0f / s.scale);   // from the anchor, fractal units
        d = deKleinianPert(d0, w, w.lod ? itersAt(o, w, w.n) : float(w.n) - 1.001f);
        if (s.cut == 2) {   // the box, near a face from |A0| - h exactly
            float3 c;
            c.x = fabsf(w.A0.x) > 0.5f * s.cutSize.x ? w.cutRel.x + w.cutSign.x * d0.x : fabsf(w.A0.x + d0.x) - s.cutSize.x;
            c.y = fabsf(w.A0.y) > 0.5f * s.cutSize.y ? w.cutRel.y + w.cutSign.y * d0.y : fabsf(w.A0.y + d0.y) - s.cutSize.y;
            c.z = fabsf(w.A0.z) > 0.5f * s.cutSize.z ? w.cutRel.z + w.cutSign.z * d0.z : fabsf(w.A0.z + d0.z) - s.cutSize.z;
            d = fmaxf(d, length(fmaxf3(c, make_float3(0, 0, 0))) + fminf(fmaxf(c.x, fmaxf(c.y, c.z)), 0.0f));
        }
        d *= s.scale;
    } else if (w.lod && s.fractal == kCrystal) {
        const float3 pf = mul(s.orient, q) * (1.0f / s.scale);
        d = deMengerLod(pf, itersAt(o, w, kMaxSteps));
        if (s.cut == 4) d = fmaxf(d, dot(pf, normalize(s.cutSize)) - s.cutOffset);
        d *= s.scale;
    } else if (w.lod && s.fractal == kKleinian) {
        const float3 pf = mul(s.orient, q) * (1.0f / s.scale);
        d = deKleinianLod(pf, s, itersAt(o, w, kMaxSteps));
        if (s.cut == 2) d = fmaxf(d, sdBox(pf, s.cutSize));
        d *= s.scale;
    } else {
        d = deFractal(mul(s.orient, q) * (1.0f / s.scale), s) * s.scale;
    }
    return fmaxf(d, -(w.floorCutRel + o.y));
}

// the whole scene at an offset o from the anchor: 0 the sculpture, 1 the plinth, 2 the floor
HD float deScene(float3 o, const World& w, int& id) {
    float d = deSculpture(o, w);
    id = 0;
    const float dp = sdBox(o + w.plinthRel, w.plinthH);
    if (dp < d) { d = dp; id = 1; }
    const float df = o.y + w.floorRel;
    if (df < d) { d = df; id = 2; }
    return d;
}
HD float deScene(float3 p, const World& w) {
    int id;
    return deScene(p, w, id);
}

// Sphere tracing with a surface threshold that grows with the distance: eps0 + slope * t (the pixel's footprint, so
// what is thinner than a pixel is not drawn as geometry). Running out of steps near something counts as a hit (deep
// crevices: the steps shrink without end).
struct Hit {
    float t = 0.0f;
    int id = -1;
    int steps = 0;
};
HD bool march(float3 ro, float3 rd, float tmax, float eps0, float slope, int maxSteps, float relax, const World& w, Hit& h) {
    float t = 0.0f;
    for (int i = 0; i < maxSteps; ++i) {
        const float3 p = ro + rd * t;
        int id;
        const float d = deScene(p, w, id);
        const float eps = eps0 + slope * t;
        if (d < eps) {
            h.t = t;
            h.id = id;
            h.steps = i;
            return true;
        }
        t += d * (id == 0 ? relax : 1.0f);
        if (t > tmax) return false;
    }
    h.t = t;
    h.steps = maxSteps;
    deScene(ro + rd * t, w, h.id);
    return t < tmax;
}

// the normal from the estimate's gradient at the footprint's size (tetrahedron, four taps)
HD float3 normalAt(float3 p, float e, const World& w) {
    const float3 k0 = make_float3(1, -1, -1), k1 = make_float3(-1, -1, 1), k2 = make_float3(-1, 1, -1), k3 = make_float3(1, 1, 1);
    const float3 g = k0 * deScene(p + k0 * e, w) + k1 * deScene(p + k1 * e, w) + k2 * deScene(p + k2 * e, w) + k3 * deScene(p + k3 * e, w);
    const float l = length(g);
    return l > 0.0f ? g * (1.0f / l) : make_float3(0, 1, 0);
}

}   // namespace hk
