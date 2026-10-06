// Scene 9 "Ayna": the tracer, the materials and the picture (see ayna_kernels.h). Units: metres, y up.
#include "scenes/ayna/ayna_kernels.h"
#include "shared/display.h"

#include <cstddef>
#include <cstdio>

namespace ayna {

__constant__ Optics cOpt;
constexpr unsigned kTexelValid = 1u << 30;   // a light map texel on a real surface

namespace {

enum HitKind { HNone = 0, HFloor, HCeil, HColumn, HFace, HJamb, HArc, HMirror, HPortal, HObject };

struct Cell {
    int i, j, up;
    float2 A[3], e[3], n[3];   // each edge's first vertex, its direction, the cell's inward normal
    int type[3];
    float2 corner[3];
    bool col[3];
    int idx;
};

struct Hit {
    float t;
    int kind, k;
    float3 n;
};

__device__ __forceinline__ float2 f2(float x, float y) { return make_float2(x, y); }
__device__ __forceinline__ float dot2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }
__device__ __forceinline__ float3 lift(float2 a) { return make_float3(a.x, 0.0f, a.y); }

__device__ void loadCell(const View& v, int i, int j, int up, Cell& c) {
    c.i = i;
    c.j = j;
    c.up = up;
    for (int k = 0; k < 3; ++k) {
        const EdgeRef e = cellEdge(i, j, up, k);
        float2 A, B;
        edgeEnds(v.g, e.fam, e.i, e.j, A, B);
        c.A[k] = A;
        c.e[k] = famDir(e.fam);
        const float2 n = famNormal(e.fam);
        c.n[k] = f2(n.x * float(e.side), n.y * float(e.side));
        const int ei = edgeIndex(v.g, e.fam, e.i, e.j);
        c.type[k] = ei < 0 ? kEdgeOut : __ldg(v.edges + ei);
    }
    int ci[3], cj[3];
    cellCornerIdx(i, j, up, ci, cj);
    for (int k = 0; k < 3; ++k) {
        c.corner[k] = vtx(v.g, ci[k], cj[k]);
        const int vi = vertexIndex(v.g, ci[k], cj[k]);
        c.col[k] = vi >= 0 && __ldg(v.columns + vi) != 0;
    }
    c.idx = cellIndex(v.g, i, j, up);
}

__device__ __forceinline__ void take(Hit& h, float t, int kind, int k, float3 n) {
    h.t = t;
    h.kind = kind;
    h.k = k;
    h.n = n;
}

// the nearest surface along o + t d inside cell c (t > 0)
__device__ void nearest(const View& v, const Cell& c, float3 o, float3 d, Hit& h) {
    const Lattice& g = v.g;
    h.t = 1e30f;
    h.kind = HNone;
    h.k = 0;
    if (d.y < 0.0f) take(h, -o.y / d.y, HFloor, 0, make_float3(0, 1, 0));
    else if (d.y > 0.0f) take(h, (g.Hc - o.y) / d.y, HCeil, 0, make_float3(0, -1, 0));
    const float2 o2 = f2(o.x, o.z), d2 = f2(d.x, d.z);
    const float a2 = dot2(d2, d2);
    // the columns at the corners (entering only)
    if (a2 > 1e-14f) {
        for (int k = 0; k < 3; ++k) {
            if (!c.col[k]) continue;
            const float2 w = f2(o2.x - c.corner[k].x, o2.y - c.corner[k].y);
            const float b = dot2(w, d2);
            if (b >= 0.0f) continue;
            const float cc = dot2(w, w) - g.rc * g.rc;
            const float disc = b * b - a2 * cc;
            if (disc <= 0.0f) continue;
            const float t = (-b - sqrtf(disc)) / a2;
            if (t > 0.0f && t < h.t) {
                const float2 q = f2(w.x + d2.x * t, w.y + d2.y * t);
                take(h, t, HColumn, k, make_float3(q.x / g.rc, 0.0f, q.y / g.rc));
            }
        }
    }
    // the three slabs
    const float ht = 0.5f * g.t;
    for (int k = 0; k < 3; ++k) {
        const float2 rel = f2(o2.x - c.A[k].x, o2.y - c.A[k].y);
        const float v0 = dot2(rel, c.n[k]), dv = dot2(d2, c.n[k]);
        const float du0 = dot2(rel, c.e[k]) - 0.5f * g.L, dud = dot2(d2, c.e[k]);
        float ts = 0.0f;
        if (v0 > ht) {
            if (dv >= 0.0f) continue;   // not in its window and going away
            const float tf = (ht - v0) / dv;
            if (tf >= h.t) continue;
            const float du = du0 + dud * tf, y = o.y + d.y * tf;
            if (!inWindow(g, du, y)) {
                if (fabsf(du) <= 0.5f * g.L) take(h, tf, HFace, k, lift(c.n[k]));
                continue;
            }
            ts = tf;
        }
        // in the window's reveal (or entering it at ts): v between the middle (glass or passage plane) and the face
        const float vmin = c.type[k] == kEdgeMirror ? g.glass : 0.0f;
        float tl = 1e30f;   // where the ray leaves the reveal
        if (dv < 0.0f) {
            const float tm = (vmin - v0) / dv;
            tl = tm;
            if (tm > 0.0f && tm < h.t) {
                const float du = du0 + dud * tm, y = o.y + d.y * tm;
                if (inWindow(g, du, y)) take(h, tm, c.type[k] == kEdgeMirror ? HMirror : HPortal, k, lift(c.n[k]));
            }
        } else if (dv > 0.0f) {
            tl = (ht - v0) / dv;
        }
        // the jambs
        if (dud != 0.0f) {
            const float side = dud > 0.0f ? 1.0f : -1.0f;
            const float tj = (side * g.wHalf - du0) / dud;
            if (tj > ts && tj > 0.0f && tj < tl && tj < h.t) {
                const float y = o.y + d.y * tj;
                if (y >= 0.0f && y <= g.yk) take(h, tj, HJamb, k, lift(c.e[k]) * (-side));
            }
        }
        // the arch: where the ray leaves the horseshoe's circle (du, y) round (0, yc)
        const float aa = dud * dud + d.y * d.y;
        if (aa > 1e-14f) {
            const float py = o.y - g.yc;
            const float b = du0 * dud + py * d.y;
            const float cc = du0 * du0 + py * py - g.ra * g.ra;
            const float disc = b * b - aa * cc;
            if (disc > 0.0f) {
                const float t2 = (-b + sqrtf(disc)) / aa;
                if (t2 > ts && t2 > 0.0f && t2 < tl && t2 < h.t) {
                    const float y = o.y + d.y * t2;
                    if (y >= g.yk) {
                        const float du = du0 + dud * t2;
                        take(h, t2, HArc, k, (lift(c.e[k]) * du + make_float3(0.0f, y - g.yc, 0.0f)) * (-1.0f / g.ra));
                    }
                }
            }
        }
    }
    // the objects standing in this cell (entering only: the primary rays start inside our own sphere)
    if (c.idx >= 0) {
        for (int s = 0; s < kCellObjects; ++s) {
            const int id = v.cellObj[c.idx * kCellObjects + s];
            if (id < 0) break;
            const Object& ob = v.obj[id];
            const float3 w = o - ob.c;
            const float b = dot(w, d);
            if (b >= 0.0f) continue;
            const float cc = dot(w, w) - ob.r * ob.r;
            if (cc <= 0.0f) continue;
            const float disc = b * b - cc;
            if (disc <= 0.0f) continue;
            const float t = -b - sqrtf(disc);
            if (t > 0.0f && t < h.t) take(h, t, HObject, id, (w + d * t) * (1.0f / ob.r));
        }
    }
}

// the optics tables by cos(theta)
__device__ __forceinline__ float tabLerp(const float* tab, float c) {
    const float x = saturate(c) * float(kOpticsN - 1);
    const int i = min(int(x), kOpticsN - 2);
    const float f = x - float(i);
    return tab[i] + (tab[i + 1] - tab[i]) * f;
}
__device__ __forceinline__ float3 mirrorW(float c) {
    return make_float3(tabLerp(cOpt.mirror[0], c), tabLerp(cOpt.mirror[1], c), tabLerp(cOpt.mirror[2], c));
}
__device__ __forceinline__ float3 chromeW(float c) {
    return make_float3(tabLerp(cOpt.chrome[0], c), tabLerp(cOpt.chrome[1], c), tabLerp(cOpt.chrome[2], c));
}

// ---------------------------------------------------------------------------------------------------------------
// shadows: a straight ray towards a light; passages let it through, anything else stops it
__device__ bool visible(const View& v, float3 o, float3 d, float dist, int skipObj) {
    int i, j, up;
    locate(v.g, o.x, o.z, i, j, up);
    Cell c;
    loadCell(v, i, j, up, c);
    for (int step = 0; step < 64; ++step) {
        Hit h;
        nearest(v, c, o, d, h);
        if (h.t >= dist) return true;
        if (h.kind == HObject && h.k == skipObj) return true;
        if (h.kind != HPortal) return false;
        int ni, nj, nup;
        cellAcross(c.i, c.j, c.up, h.k, ni, nj, nup);
        loadCell(v, ni, nj, nup, c);
        o = o + d * h.t;
        dist -= h.t;
    }
    return false;
}

// the light falling on a surface point straight from the lights (the mirrors' light comes with the beams, A2)
__device__ float3 directLight(const View& v, float3 p, float3 n) {
    float3 E = make_float3(0, 0, 0);
    for (int l = 0; l < v.nLights; ++l) {
        const Light& L = v.light[l];
        float3 w = L.pos - p;
        const float d2 = dot(w, w);
        const float dist = sqrtf(d2);
        w = w * (1.0f / dist);
        const float cn = dot(n, w);
        if (cn <= 0.0f) continue;
        float cone = 1.0f;
        if (L.cosOuter > -1.0f) {
            const float ca = -dot(w, L.axis);
            cone = smoothstepf(L.cosOuter, L.cosInner, ca);
            if (cone <= 0.0f) continue;
        }
        if (!visible(v, p + n * 1e-4f, w, dist - L.radius, L.object)) continue;
        E += L.color * (cn * cone / d2);
    }
    return E;
}

// the light by way of the beams (ayna_beams.h), from the nodes of cell c, at the point q with normal n.
// throughK >= 0: q stands in the window of c's edge throughK, on the far side (in the neighbour's half of a passage,
// or q is a point's image behind mirror throughK): the way must come out through that window into c; mirrorK adds that
// mirror's reflectance. skipK: nodes that came in through that edge are not counted here (the window's reveal is
// lit by them through the other call).
__device__ int gTested, gPassed;   // (the tools' counts; per thread they are not meaningful, only as a heat map)
__device__ float3 evalCell(const View& v, const Cell& c, float3 q, float3 n, float pathW, int throughK, bool mirrorK, int skipK,
                           int onlyStraight, int* counts, int* straight = nullptr) {
    float3 E = make_float3(0, 0, 0);
    if (c.idx < 0) return E;
    const Lattice& g = v.g;
    const float2 p2 = f2(q.x, q.z);
    const float ht = 0.5f * g.t;
    float vk[3];
    for (int k = 0; k < 3; ++k) vk[k] = dot2(f2(p2.x - c.A[k].x, p2.y - c.A[k].y), c.n[k]);
    bool sphereHere = false;
    for (int s = 0; s < kCellObjects && !v.bake; ++s) {
        const int id = v.cellObj[c.idx * kCellObjects + s];
        if (id < 0) break;
        if (id == 0) sphereHere = true;
    }
    // the point's bin in the cell's bounding box
    const float bx = fminf(c.corner[0].x, fminf(c.corner[1].x, c.corner[2].x)), bz = fminf(c.corner[0].y, fminf(c.corner[1].y, c.corner[2].y));
    const int bi = min(kBins - 1, max(0, int((p2.x - bx) * (float(kBins) / g.L))));
    const int bj = min(kBins - 1, max(0, int((p2.y - bz) * (float(kBins) / g.h))));
    const int list = c.idx * kBins * kBins + bj * kBins + bi;
    const int s0 = v.cellStart[list], s1 = v.cellStart[list + 1];
    if (s0 < 0 || s1 > v.nEntries || s0 > s1) {
        if (v.oob) atomicAdd(v.oob, 100u);
        return E;
    }
    const bool high = fmaxf(q.y, 0.0f) > g.yk;
    const float4* hot = reinterpret_cast<const float4*>(v.cellNodes);
    for (int s = s0; s < s1; ++s) {
        const float4 h0 = __ldg(hot + 2 * s), h1 = __ldg(hot + 2 * s + 1);
        if (h1.z * pathW < v.beamEps) break;
        if (counts) ++counts[0];
        const float2 d = f2(p2.x - h0.x, p2.y - h0.y);
        if (h0.z * d.y - h0.w * d.x < 0.0f || d.x * h1.y - d.y * h1.x < 0.0f) continue;   // (a root's a = b = 0 passes)
        const int nodeIdx = __float_as_int(h1.w);
        if (nodeIdx < 0 || nodeIdx >= v.nNodes) {
            if (v.oob) atomicAdd(v.oob, 1u);
            continue;
        }
        const BeamNode& nd = v.nodes[nodeIdx];
        if (nd.high && !high && v.light[nd.light].pos.y <= g.yk) continue;   // a bulge's light never comes this low
        if (int(nd.entryK) == skipK || (throughK >= 0 && int(nd.entryK) == throughK)) continue;
        const bool root = nd.entryK == 3;
        if (onlyStraight && (nd.m[0] | nd.m[1] | nd.m[2] | (mirrorK ? 1 : 0))) continue;
        const Light& L = v.light[nd.light];
        float3 to = make_float3(-d.x, L.pos.y - q.y, -d.y);
        const float d2 = dot(to, to);
        const float dist = sqrtf(d2);
        const float3 w = to * (1.0f / dist);
        const float cn = dot(n, w);
        if (cn <= 0.0f) continue;
        // the way leaves the light in the light's own frame: the plan part through the node's transform
        const float2 hl = f2(-(nd.m00 * w.x + nd.m01 * w.z), -(nd.m10 * w.x + nd.m11 * w.z));
        float cone = 1.0f;
        if (L.cosOuter > -1.0f) {
            const float ca = hl.x * L.axis.x - w.y * L.axis.y + hl.y * L.axis.z;
            cone = smoothstepf(L.cosOuter, L.cosInner, ca);
            if (cone <= 0.0f) continue;
        }
        // how far the way runs inside this cell: to the entry window's line, or to the light itself
        float te = dist;
        if (!root) {
            const int k = nd.entryK;
            const float rate = -(w.x * c.n[k].x + w.z * c.n[k].y);
            if (rate <= 1e-6f) continue;
            te = fminf(dist, vk[k] / rate);
        }
        bool blocked = false;
        // out of the far half of the window: through its middle plane and the near face, both inside the window
        if (throughK >= 0) {
            const int k = throughK;
            const float rate = w.x * c.n[k].x + w.z * c.n[k].y;
            if (rate <= 1e-6f) continue;
            for (int f = 0; f < 2 && !blocked; ++f) {
                const float tf = ((f == 0 ? 0.0f : ht) - vk[k]) / rate;
                if (tf >= te) { blocked = true; break; }
                const float2 x = f2(p2.x + w.x * tf - c.A[k].x, p2.y + w.z * tf - c.A[k].y);
                if (!inWindow(g, dot2(x, c.e[k]) - 0.5f * g.L, q.y + w.y * tf)) blocked = true;
            }
            if (blocked) continue;
        }
        // the cell's columns
        const float2 w2 = f2(w.x, w.z);
        const float a2 = dot2(w2, w2);
        if (a2 > 1e-12f) {
            for (int k = 0; k < 3 && !blocked; ++k) {
                if (!c.col[k]) continue;
                const float2 qq = f2(p2.x - c.corner[k].x, p2.y - c.corner[k].y);
                const float b = dot2(qq, w2);
                if (b >= 0.0f) continue;
                const float cc = dot2(qq, qq) - g.rc * g.rc;
                const float disc = b * b - a2 * cc;
                if (disc <= 0.0f) continue;
                const float t = (-b - sqrtf(disc)) / a2;
                if (t > 0.0f && t < te) blocked = true;
            }
        }
        if (blocked) continue;
        // a point inside one of this cell's slabs sees out only through its window
        if (throughK < 0) {
            for (int k = 0; k < 3 && !blocked; ++k) {
                if (vk[k] >= ht - 1e-4f) continue;   // (a point on the face itself is not inside)
                const float rate = w.x * c.n[k].x + w.z * c.n[k].y;
                if (rate <= 0.0f) {   // deeper into the slab: only through the window the node came in by
                    if (root || k != nd.entryK) blocked = true;
                    continue;
                }
                const float tf = (ht - vk[k]) / rate;
                if (tf >= te) continue;
                const float2 x = f2(p2.x + w.x * tf - c.A[k].x, p2.y + w.z * tf - c.A[k].y);
                if (!inWindow(g, dot2(x, c.e[k]) - 0.5f * g.L, q.y + w.y * tf)) blocked = true;
            }
            if (blocked) continue;
        }
        // our sphere, in this cell
        if (sphereHere) {
            const float3 oc = q - v.obj[0].c;
            const float b = dot(oc, w), cc = dot(oc, oc) - v.obj[0].r * v.obj[0].r;
            const float disc = b * b - cc;
            if (disc > 0.0f) {
                const float t = -b - sqrtf(disc);
                if (t > 0.0f && t < te) continue;
            }
        }
        // the arches: where the way runs above the jambs' top, each window it came through must hold it
        if (!root && (nd.high || fmaxf(L.pos.y, q.y) > g.yk)) {
            const float2 SL = f2(L.pos.x, L.pos.z);
            const float2 PL = f2(nd.m00 * p2.x + nd.m01 * p2.y + nd.tx, nd.m10 * p2.x + nd.m11 * p2.y + nd.tz);
            const float2 dl = f2(PL.x - SL.x, PL.y - SL.y);
            int at = nodeIdx;
            for (int guard = 0; guard < 256 && !blocked; ++guard) {
                const BeamNode& an = v.nodes[at];
                if (an.entryK == 3) break;
                const float den = dl.x * an.ed.y - dl.y * an.ed.x;
                if (fabsf(den) > 1e-12f) {
                    // the window through the slab's depth: at both faces
                    for (int f = -1; f <= 1 && !blocked; f += 2) {
                        const float2 em = f2(an.em.x - an.ed.y * ht * float(f), an.em.y + an.ed.x * ht * float(f));
                        const float2 e0 = f2(em.x - SL.x, em.y - SL.y);
                        const float sc = (e0.x * an.ed.y - e0.y * an.ed.x) / den;
                        const float2 X = f2(SL.x + dl.x * sc - em.x, SL.y + dl.y * sc - em.y);
                        const float y = L.pos.y + (q.y - L.pos.y) * sc;
                        if (!inWindow(g, X.x * an.ed.x + X.y * an.ed.y, y)) blocked = true;
                    }
                }
                at = an.parent;
                if (at < 0) break;
                if (at >= v.nNodes) {
                    if (v.oob) atomicAdd(v.oob, 1000u);
                    break;
                }
            }
            if (blocked) continue;
        }
        // the mirrors' reflectance at this way's own angles (in the light's frame each family is met at one angle)
        float3 W = make_float3(1, 1, 1);
        for (int f = 0; f < 3; ++f) {
            if (!nd.m[f]) continue;
            const float2 fn = famNormal(f);
            const float ci = fabsf(hl.x * fn.x + hl.y * fn.y);
            const float3 wf = mirrorW(ci);
            const float mf = float(nd.m[f]);
            W *= make_float3(__powf(wf.x, mf), __powf(wf.y, mf), __powf(wf.z, mf));
        }
        if (mirrorK) W *= mirrorW(fabsf(w.x * c.n[throughK].x + w.z * c.n[throughK].y));
        E += L.color * W * (cone * cn / d2);
        if (counts) ++counts[1];
        if (straight && !mirrorK && !(nd.m[0] | nd.m[1] | nd.m[2])) *straight = 1;
    }
    return E;
}

__device__ float3 beamLight(const View& v, const Cell& c, float3 p, float3 n, float pathW, int onlyStraight, int* counts = nullptr,
                            int* straight = nullptr) {
    if (c.idx < 0 || !v.nodes) return make_float3(0, 0, 0);
    const float ht = 0.5f * v.g.t;
    // a point in one of the cell's windows (on a jamb, the arch's underside, the floor between the faces) also takes
    // the light that comes through the window's other half: from the neighbour (a passage) or off the mirror
    int inK = -1;
    for (int k = 0; k < 3; ++k)
        if (dot2(f2(p.x - c.A[k].x, p.z - c.A[k].y), c.n[k]) < ht - 1e-4f) inK = k;
    float3 E = evalCell(v, c, p, n, pathW, -1, false, inK, onlyStraight, counts, straight);
    if (inK >= 0 && c.type[inK] != kEdgeOut) {
        if (c.type[inK] == kEdgeOpen) {
            int ni, nj, nup;
            cellAcross(c.i, c.j, c.up, inK, ni, nj, nup);
            Cell t;
            loadCell(v, ni, nj, nup, t);
            E += evalCell(v, t, p, n, pathW, inK, false, -1, onlyStraight, counts, straight);
        } else {
            const float2 nk = c.n[inK];
            const float vp = dot2(f2(p.x - c.A[inK].x, p.z - c.A[inK].y), nk);
            const float3 q = make_float3(p.x - 2.0f * vp * nk.x, p.y, p.z - 2.0f * vp * nk.y);
            const float vn = n.x * nk.x + n.z * nk.y;
            const float3 nq = make_float3(n.x - 2.0f * vn * nk.x, n.y, n.z - 2.0f * vn * nk.y);
            E += evalCell(v, c, q, nq, pathW, inK, true, -1, onlyStraight, counts);
        }
    }
    return E;
}

// ---------------------------------------------------------------------------------------------------------------
// the light map (the lamps' baked light)
__device__ __forceinline__ float revealS(const Lattice& g, float du, float y, float perim) {
    // round the window from the left jamb's foot: up the jamb, over the arch, down the right jamb
    if (y <= g.yk) return du < 0.0f ? y : perim - y;
    // the arc runs from the left junction (thL, past pi) down over the top to the right junction (thR)
    const float thL = atan2f(g.yk - g.yc, -g.wHalf) + 2.0f * M_PIf, thR = atan2f(g.yk - g.yc, g.wHalf);
    float th = atan2f(y - g.yc, du);
    if (th < thR) th += 2.0f * M_PIf;
    return g.yk + g.ra * (thL - th);
}

// a surface point's block and its coordinates in texels (-1: no light map for it)
__device__ int texelOf(const View& v, const Cell& c, int kind, int k, float3 p, float& tx, float& ty) {
    const LightMap& lm = v.lm;
    const Lattice& g = v.g;
    const float inv = 1.0f / lm.texel;
    switch (kind) {
    case HFloor:
    case HCeil:
        tx = (p.x - lm.x0) * inv - 0.5f;
        ty = (p.z - lm.z0) * inv - 0.5f;
        return kind == HFloor ? -2 : -3;
    case HFace:
    case HJamb:
    case HArc: {
        const EdgeRef e = cellEdge(c.i, c.j, c.up, k);
        const int ei = edgeIndex(g, e.fam, e.i, e.j);
        if (ei < 0) return -1;
        const float2 rel = f2(p.x - c.A[k].x, p.z - c.A[k].y);
        const float u = dot2(rel, c.e[k]);
        if (kind == HFace) {
            tx = u * inv - 0.5f;
            ty = p.y * inv - 0.5f;
            return lm.faceBlock[ei * 2 + (e.side > 0 ? 0 : 1)];
        }
        const float vv = dot2(rel, c.n[k]);
        tx = revealS(g, u - 0.5f * g.L, p.y, lm.perimeter) * inv - 0.5f;
        ty = (0.5f * g.t - vv) * inv - 0.5f;
        return lm.revBlock[ei * 2 + (e.side > 0 ? 0 : 1)];
    }
    case HColumn: {
        int ci[3], cj[3];
        cellCornerIdx(c.i, c.j, c.up, ci, cj);
        const int vi = vertexIndex(g, ci[k], cj[k]);
        if (vi < 0) return -1;
        float phi = atan2f(p.z - c.corner[k].y, p.x - c.corner[k].x);
        if (phi < 0.0f) phi += 2.0f * M_PIf;
        tx = phi * (g.rc * inv) - 0.5f;
        ty = p.y * inv - 0.5f;
        return lm.colBlock[vi];
    }
    default: return -1;
    }
}

__device__ float4 fetchMap(const View& v, int block, float tx, float ty) {
    const LightMap& lm = v.lm;
    int off, w, h;
    bool wrapX = false;
    if (block == -2 || block == -3) {
        off = block == -2 ? lm.floorOff : lm.ceilOff;
        w = lm.gw;
        h = lm.gh;
    } else {
        const LightMapBlock& b = lm.blocks[block];
        off = b.offset;
        w = b.w;
        h = b.h;
        wrapX = b.type == 4;
    }
    tx = wrapX ? tx : fminf(fmaxf(tx, 0.0f), float(w) - 1.001f);
    ty = fminf(fmaxf(ty, 0.0f), float(h) - 1.001f);
    int x0 = int(floorf(tx)), y0 = int(ty);
    const float fx = tx - floorf(tx), fy = ty - float(y0);
    int x1 = x0 + 1;
    if (wrapX) {
        x0 = (x0 % w + w) % w;
        x1 = (x1 % w + w) % w;
    }
    const int y1 = min(y0 + 1, h - 1);
    const float4 a = lm.E[off + y0 * w + x0], b = lm.E[off + y0 * w + x1], c = lm.E[off + y1 * w + x0], d = lm.E[off + y1 * w + x1];
    float4 r;
    r.x = (a.x * (1 - fx) + b.x * fx) * (1 - fy) + (c.x * (1 - fx) + d.x * fx) * fy;
    r.y = (a.y * (1 - fx) + b.y * fx) * (1 - fy) + (c.y * (1 - fx) + d.y * fx) * fy;
    r.z = (a.z * (1 - fx) + b.z * fx) * (1 - fy) + (c.z * (1 - fx) + d.z * fx) * fy;
    // the bits: the nearest texel's
    const float4& nn = fx < 0.5f ? (fy < 0.5f ? a : c) : (fy < 0.5f ? b : d);
    r.w = nn.w;
    return r;
}

// how much of a lamp's glowing ball our sphere hides, seen from p (two discs on the sky, their overlap)
__device__ float sphereHides(const View& v, float3 p, float3 lp, float lr) {
    const float3 toL = lp - p, toS = v.obj[0].c - p;
    const float dl = length(toL), ds = length(toS);
    if (ds >= dl || ds <= v.obj[0].r) return 0.0f;
    const float al = asinf(fminf(1.0f, lr / dl)), as = asinf(fminf(1.0f, v.obj[0].r / ds));
    const float cb = dot(toL, toS) / (dl * ds);
    const float beta = acosf(fminf(1.0f, fmaxf(-1.0f, cb)));
    if (beta >= al + as) return 0.0f;
    if (beta <= as - al) return 1.0f;
    if (beta <= al - as) return (as * as) / (al * al);
    // the lens of two circles (radii al, as, centres beta apart), over the lamp's disc
    const float r1 = al, r2 = as, dd = beta;
    const float c1 = (dd * dd + r1 * r1 - r2 * r2) / (2.0f * dd * r1), c2 = (dd * dd + r2 * r2 - r1 * r1) / (2.0f * dd * r2);
    const float a1 = acosf(fminf(1.0f, fmaxf(-1.0f, c1))), a2 = acosf(fminf(1.0f, fmaxf(-1.0f, c2)));
    const float lens = r1 * r1 * (a1 - sinf(2.0f * a1) * 0.5f) + r2 * r2 * (a2 - sinf(2.0f * a2) * 0.5f);
    return fminf(1.0f, lens / (M_PIf * r1 * r1));
}

__device__ float3 lightAt(const View& v, const Cell& c, int kind, int k, float3 p, float3 n, float pathW, int* counts = nullptr) {
    float3 E = make_float3(0, 0, 0);
    if (v.beams > 0) E = beamLight(v, c, p, n, pathW, v.beams == 2, counts);
    else E = directLight(v, p, n);
    if (v.lm.on && !v.bake) {
        float tx, ty;
        const int blk = texelOf(v, c, kind, k, p, tx, ty);
        if (blk != -1) {
            const float4 m = fetchMap(v, blk, tx, ty);
            E += make_float3(m.x, m.y, m.z);
            // our sphere's shadow on the lamps this point sees straight
            const unsigned bits = __float_as_uint(m.w) & 0xFFFFu;
            for (int i = 0; i < v.nLamps; ++i) {
                if (!(bits & (1u << i))) continue;
                const float3 lp = make_float3(v.lamp[i].x, v.lamp[i].y, v.lamp[i].z);
                const float hide = sphereHides(v, p, lp, v.lamp[i].w);
                if (hide <= 0.0f) continue;
                const float3 w = lp - p;
                const float d2 = dot(w, w);
                const float cn = dot(n, w) * rsqrtf(d2);
                if (cn > 0.0f) E -= v.lampI[i] * (hide * cn / d2);
            }
            E = fmaxf3(E, make_float3(0, 0, 0));
        }
    }
    return E;
}

// the bake: one texel's surface point, its normal and its cell; false if the texel is on no surface
__device__ bool texelPoint(const View& v, const LightMapBlock& b, int x, int y, float3& p, float3& n) {
    const Lattice& g = v.g;
    const float ts = v.lm.texel;
    const float fx = (float(x) + 0.5f) * ts, fy = (float(y) + 0.5f) * ts;
    switch (b.type) {
    case 0:
    case 1: {
        p = make_float3(v.lm.x0 + fx, b.type == 0 ? 0.0f : g.Hc, v.lm.z0 + fy);
        n = make_float3(0.0f, b.type == 0 ? 1.0f : -1.0f, 0.0f);
        // not under a slab's solid part (the floor shows only in the windows; the ceiling never over a slab), nor in a column
        int i, j, up;
        locate(g, p.x, p.z, i, j, up);
        if (cellIndex(g, i, j, up) < 0) return false;
        float2 cc[3];
        cellCorners(g, i, j, up, cc);
        for (int k = 0; k < 3; ++k) {
            if (sqr(p.x - cc[k].x) + sqr(p.z - cc[k].y) < g.rc * g.rc) return false;
            const EdgeRef e = cellEdge(i, j, up, k);
            float2 A, B;
            edgeEnds(g, e.fam, e.i, e.j, A, B);
            const float2 ed = famDir(e.fam), nn = famNormal(e.fam);
            const float vv = fabsf((p.x - A.x) * nn.x + (p.z - A.y) * nn.y);
            if (vv >= 0.5f * g.t) continue;
            if (b.type == 1) return false;
            const float du = (p.x - A.x) * ed.x + (p.z - A.y) * ed.y - 0.5f * g.L;
            if (fabsf(du) > g.wHalf) return false;
        }
        return true;
    }
    case 2:
    case 3: {
        float2 A, B;
        edgeEnds(g, b.fam, b.i, b.j, A, B);
        const float2 e = famDir(b.fam), nn = famNormal(b.fam);
        const float2 nin = f2(nn.x * float(b.side), nn.y * float(b.side));
        if (b.type == 2) {
            if (fx > g.L || fy > g.Hc) return false;
            if (inWindow(g, fx - 0.5f * g.L, fy)) return false;
            const float off = 0.5f * g.t;
            p = make_float3(A.x + e.x * fx + nin.x * off, fy, A.y + e.y * fx + nin.y * off);
            n = lift(nin);
            return true;
        }
        // a reveal: s round the window, depth from the face
        const float perim = v.lm.perimeter;
        if (fx > perim) return false;
        const float depth = fminf(fy, 0.5f * g.t - 0.001f);
        float du, yy;
        float3 nl;   // (du, y) normal
        if (fx <= g.yk) {
            du = -g.wHalf; yy = fx; nl = make_float3(1, 0, 0);
        } else if (fx >= perim - g.yk) {
            du = g.wHalf; yy = perim - fx; nl = make_float3(-1, 0, 0);
        } else {
            const float thL = atan2f(g.yk - g.yc, -g.wHalf) + 2.0f * M_PIf;
            const float th = thL - (fx - g.yk) / g.ra;
            du = g.ra * cosf(th);
            yy = g.yc + g.ra * sinf(th);
            nl = make_float3(-cosf(th), -sinf(th), 0);
        }
        const float vv = 0.5f * g.t - depth;
        p = make_float3(A.x + e.x * (0.5f * g.L + du) + nin.x * vv, yy, A.y + e.y * (0.5f * g.L + du) + nin.y * vv);
        n = make_float3(e.x * nl.x, nl.y, e.y * nl.x);
        return true;
    }
    case 4: {
        const float2 V = vtx(g, b.i, b.j);
        const float phi = fx / g.rc;
        if (phi > 2.0f * M_PIf || fy > g.Hc) return false;
        n = make_float3(cosf(phi), 0.0f, sinf(phi));
        p = make_float3(V.x + n.x * g.rc, fy, V.y + n.z * g.rc);
        return true;
    }
    }
    return false;
}

__global__ void kBake(View v, const LightMapBlock* blocks, unsigned bit) {
    const LightMapBlock& b = blocks[blockIdx.z];
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= b.w || y >= b.h) return;
    float3 p, n;
    if (!texelPoint(v, b, x, y, p, n)) return;
    // the point's cell: a hair off the surface, along its normal
    const float3 q = p + n * 0.002f;
    int i, j, up;
    locate(v.g, q.x, q.z, i, j, up);
    if (cellIndex(v.g, i, j, up) < 0) return;
    Cell c;
    loadCell(v, i, j, up, c);
    if (c.idx < 0) return;
    int straight = 0;
    const float3 E = beamLight(v, c, p, n, 1.0f, 0, nullptr, &straight);
    float4& t = v.lm.E[b.offset + y * b.w + x];
    t.x += E.x;
    t.y += E.y;
    t.z += E.z;
    t.w = __uint_as_float(__float_as_uint(t.w) | (straight ? bit : 0u) | kTexelValid);
}

// texels on no surface (in a window, under a slab, inside a column) take their valid neighbours' mean: the bilinear
// fetch at a surface's rim then reads its own light, not the dark beyond it (reads only valid texels: no order)
__global__ void kDilate(View v, const LightMapBlock* blocks) {
    const LightMapBlock& b = blocks[blockIdx.z];
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= b.w || y >= b.h) return;
    float4* E = v.lm.E + b.offset;
    if (__float_as_uint(E[y * b.w + x].w) & kTexelValid) return;
    float3 sum = make_float3(0, 0, 0);
    float wsum = 0.0f;
    unsigned bits = 0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= b.w || yy >= b.h) continue;
            const float4 q = E[yy * b.w + xx];
            const unsigned qb = __float_as_uint(q.w);
            if (!(qb & kTexelValid)) continue;
            const float w = 1.0f / float(1 + dx * dx + dy * dy);
            sum += make_float3(q.x, q.y, q.z) * w;
            wsum += w;
            bits |= qb & 0xFFFFu;
        }
    if (wsum > 0.0f) {
        sum = sum * (1.0f / wsum);
        E[y * b.w + x] = make_float4(sum.x, sum.y, sum.z, __uint_as_float(bits));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// the haze's glow along o + t d, t in [ta, tb], from one node (its image is a point light): the segment cut to the
// node's wedge, then the point light's in-scattering in closed form, I / (h^2 + (t - t0)^2) integrated as an angle
// (atan); a spot's cone and the mirrors' reflectance vary along it, so their factor is taken at four Gauss points of
// that angle
__device__ float3 hazeNode(const View& v, const BeamNode& nd, float3 o, float3 d, float ta, float tb) {
    const Light& L = v.light[nd.light];
    const float2 o2 = f2(o.x, o.z), d2 = f2(d.x, d.z);
    if (nd.entryK != 3) {
        // the two sides: cross(a, P - S) >= 0, cross(P - S, b) >= 0 with P = o2 + d2 t
        const float2 r = f2(o2.x - nd.S.x, o2.y - nd.S.y);
        const float ca0 = nd.a.x * r.y - nd.a.y * r.x, ca1 = nd.a.x * d2.y - nd.a.y * d2.x;
        const float cb0 = r.x * nd.b.y - r.y * nd.b.x, cb1 = d2.x * nd.b.y - d2.y * nd.b.x;
        if (fabsf(ca1) > 1e-9f) {
            const float tc = -ca0 / ca1;
            if (ca1 > 0.0f) ta = fmaxf(ta, tc); else tb = fminf(tb, tc);
        } else if (ca0 < 0.0f) return make_float3(0, 0, 0);
        if (fabsf(cb1) > 1e-9f) {
            const float tc = -cb0 / cb1;
            if (cb1 > 0.0f) ta = fmaxf(ta, tc); else tb = fminf(tb, tc);
        } else if (cb0 < 0.0f) return make_float3(0, 0, 0);
        if (tb <= ta) return make_float3(0, 0, 0);
    }
    const float3 S3 = make_float3(nd.S.x, L.pos.y, nd.S.y);
    const float t0 = dot(S3 - o, d);
    const float3 cp = o + d * t0;
    const float h = fmaxf(length(S3 - cp), fmaxf(L.radius, 0.01f));
    const float pa = atanf((ta - t0) / h), pb = atanf((tb - t0) / h);
    const float trans = __expf(-(v.hazeSigma + v.hazeAbsorb) * length(S3 - o - d * clampf(t0, ta, tb)));
    const float k = v.hazeSigma * (0.25f * INV_PIf) / h * trans;
    const bool plain = L.cosOuter <= -1.0f && !(nd.m[0] | nd.m[1] | nd.m[2]);
    if (plain) return L.color * (k * (pb - pa));
    // four Gauss-Legendre points over the angle
    const float gx[4] = {-0.8611363116f, -0.3399810436f, 0.3399810436f, 0.8611363116f};
    const float gw[4] = {0.3478548451f, 0.6521451549f, 0.6521451549f, 0.3478548451f};
    float3 acc = make_float3(0, 0, 0);
    const float mid = 0.5f * (pa + pb), half = 0.5f * (pb - pa);
    for (int q = 0; q < 4; ++q) {
        const float ph = mid + half * gx[q];
        const float3 x = o + d * (t0 + h * tanf(ph));
        float3 w = x - S3;   // from the image to the point
        w = w * rsqrtf(fmaxf(dot(w, w), 1e-12f));
        const float2 hl = f2(nd.m00 * w.x + nd.m01 * w.z, nd.m10 * w.x + nd.m11 * w.z);
        float c = 1.0f;
        if (L.cosOuter > -1.0f) c = smoothstepf(L.cosOuter, L.cosInner, hl.x * L.axis.x + w.y * L.axis.y + hl.y * L.axis.z);
        float3 W = make_float3(c, c, c);
        for (int f = 0; f < 3; ++f) {
            if (!nd.m[f]) continue;
            const float2 fn = famNormal(f);
            const float3 wf = mirrorW(fabsf(hl.x * fn.x + hl.y * fn.y));
            const float mf = float(nd.m[f]);
            W *= make_float3(__powf(wf.x, mf), __powf(wf.y, mf), __powf(wf.z, mf));
        }
        acc += W * gw[q];
    }
    return L.color * acc * (k * half);
}

// the haze's glow along a ray's piece inside cell c
__device__ float3 hazeSegment(const View& v, const Cell& c, float3 o, float3 d, float len) {
    float3 g = make_float3(0, 0, 0);
    if (c.idx < 0 || !v.hazeStart) return g;
    const int s0 = v.hazeStart[c.idx], s1 = v.hazeStart[c.idx + 1];
    for (int s = s0; s < s1; ++s) {
        const int q = v.hazeNodes[s];
        if (q < 0 || q >= v.nNodes) {
            if (v.oob) atomicAdd(v.oob, 1000000u);
            continue;
        }
        g += hazeNode(v, v.nodes[q], o, d, 0.0f, len);
    }
    return g;
}

// ---------------------------------------------------------------------------------------------------------------
// the materials (A0: plain; the art comes in A6)
__device__ float3 albedoAt(const View& v, int kind, float3 p) {
    switch (kind) {
    case HFloor: return make_float3(0.035f, 0.033f, 0.032f);
    case HCeil: return make_float3(0.09f, 0.055f, 0.032f);
    case HColumn: return make_float3(0.22f, 0.25f, 0.23f);
    default: return v.albedoPlaster;
    }
}

__device__ float3 tracePath(const View& v, float3 o, float3 d, int& bounces, int& steps, int* counts, bool haze = false, bool glowOnly = false) {
    int i, j, up;
    locate(v.g, o.x, o.z, i, j, up);
    Cell c;
    loadCell(v, i, j, up, c);
    float3 L = make_float3(0, 0, 0), thr = make_float3(1, 1, 1);
    bounces = 0;
    for (steps = 0; steps < v.maxSteps; ++steps) {
        Hit h;
        nearest(v, c, o, d, h);
        if (h.kind == HNone) break;
        float3 p = o + d * h.t;
        if (v.hazeSigma > 0.0f && v.hazeOnly != 3) {
            if (haze && maxc(thr) > v.hazeMinWeight) L += thr * hazeSegment(v, c, o, d, h.t);
            thr *= __expf(-(v.hazeSigma + v.hazeAbsorb) * h.t);
        }
        if (glowOnly && maxc(thr) < v.hazeMinWeight) break;
        if (h.kind == HPortal) {
            int ni, nj, nup;
            cellAcross(c.i, c.j, c.up, h.k, ni, nj, nup);
            loadCell(v, ni, nj, nup, c);
            o = p;
            continue;
        }
        if (h.kind == HMirror) {
            // through the glass's front surface, off the silver, back out: the ray leaves parallel to the front
            // surface's own reflection, shifted along the glass by 2 g tan(theta_t)
            const float ci = -dot(d, h.n);
            const float si = sqrtf(fmaxf(1.0f - ci * ci, 0.0f));
            const float st = si / cOpt.glassN, ct = sqrtf(fmaxf(1.0f - st * st, 0.0f));
            const float3 tang = d + h.n * ci;
            const float tl = length(tang);
            if (tl > 1e-7f) {
                float3 q = p + tang * (2.0f * v.g.glass * st / (ct * tl));
                q.y = fmaxf(q.y, 0.0f);   // the glass stands on the floor: below it the light meets the floor
                // the shifted exit must still be in the window (at its very rim it would be in the frame's groove)
                const float2 rel = f2(q.x - c.A[h.k].x, q.z - c.A[h.k].y);
                if (!inWindow(v.g, dot2(rel, c.e[h.k]) - 0.5f * v.g.L, q.y)) {
                    if (!glowOnly) L += thr * albedoAt(v, HJamb, p) * INV_PIf * lightAt(v, c, HJamb, h.k, p, h.n, maxc(thr), counts);
                    break;
                }
                p = q;
            }
            thr *= mirrorW(ci);
            d = d + h.n * (2.0f * ci);
            o = p;
            ++bounces;
            if (maxc(thr) < v.minWeight) break;
            continue;
        }
        if (h.kind == HObject) {
            const Object& ob = v.obj[h.k];
            if (ob.kind == 1) {
                const Light& Lt = v.light[ob.light];
                if (!glowOnly) L += thr * Lt.color * (1.0f / (M_PIf * ob.r * ob.r));
                break;
            }
            const float ci = -dot(d, h.n);
            thr *= chromeW(ci);
            d = d + h.n * (2.0f * ci);
            o = p;
            ++bounces;
            if (maxc(thr) < v.minWeight) break;
            continue;
        }
        if (h.kind == HFloor) p.y = 0.0f;   // exactly on it: the windows start at y = 0
        const float3 alb = albedoAt(v, h.kind, p);
        if (h.kind == HFloor || h.kind == HColumn) {
            // polished stone: the Fresnel part reflects, the rest is diffuse
            const float ci = -dot(d, h.n);
            const float F = tabLerp(cOpt.stone, ci);
            if (!glowOnly) L += thr * alb * ((1.0f - F) * INV_PIf) * lightAt(v, c, h.kind, h.k, p, h.n, maxc(thr) * (1.0f - F), counts);
            thr *= F;
            d = d + h.n * (2.0f * ci);
            o = p;
            ++bounces;
            if (maxc(thr) < v.minWeight) break;
            continue;
        }
        if (!glowOnly) L += thr * alb * INV_PIf * lightAt(v, c, h.kind, h.k, p, h.n, maxc(thr), counts);
        break;
    }
    return L;
}

__device__ float3 rayDir(const View& v, float px, float py) {
    const float aspect = float(v.width) / float(v.height);
    const float sx = (px / v.width * 2.0f - 1.0f) * v.tanHalf * aspect;
    const float sy = (1.0f - py / v.height * 2.0f) * v.tanHalf;
    return normalize(v.camFwd + v.camRight * sx + v.camUp * sy);
}

// the standard multisample positions (Direct3D 11), in sixteenths of a pixel
__constant__ signed char kPat4[4][2] = {{-2, -6}, {6, -2}, {-6, 2}, {2, 6}};
__constant__ signed char kPat8[8][2] = {{1, -3}, {-1, 3}, {5, 1}, {-3, -5}, {-5, 5}, {-7, -1}, {3, 7}, {7, -7}};
__constant__ signed char kPat16[16][2] = {{1, 1}, {-1, -3}, {-3, 2}, {4, -1}, {-5, -2}, {2, 5}, {5, 3}, {3, -5},
                                          {-2, 6}, {0, -7}, {-4, -6}, {-6, 4}, {-8, 0}, {7, -4}, {6, 7}, {-7, -8}};

__global__ void kFrame(View v, float4* hdr) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= v.width || y >= v.height) return;
    const int n = v.samples >= 16 ? 16 : v.samples >= 8 ? 8 : v.samples >= 4 ? 4 : 1;
    float3 sum = make_float3(0, 0, 0);
    float bsum = 0.0f, ssum = 0.0f, tsum = 0.0f, psum = 0.0f;
    for (int s = 0; s < n; ++s) {
        float ox = 0.0f, oy = 0.0f;
        if (n == 4) { ox = kPat4[s][0]; oy = kPat4[s][1]; }
        else if (n == 8) { ox = kPat8[s][0]; oy = kPat8[s][1]; }
        else if (n == 16) { ox = kPat16[s][0]; oy = kPat16[s][1]; }
        const float3 d = rayDir(v, x + 0.5f + ox / 16.0f, y + 0.5f + oy / 16.0f);
        int b, st;
        int counts[2] = {0, 0};
        sum += tracePath(v, v.camPos, d, b, st, counts);
        bsum += float(b);
        ssum += float(st);
        tsum += float(counts[0]);
        psum += float(counts[1]);
    }
    sum = sum * (1.0f / float(n));
    if (v.hazeSigma > 0.0f && v.debug == 0 && v.hazeBuf && v.hazeOnly != 2) {
        // the mist's glow, worked out at half resolution (one ray through each 2 x 2 block's centre): bilinear
        const int hw = (v.width + 1) / 2, hh = (v.height + 1) / 2;
        float gx = (x + 0.5f) * 0.5f - 0.5f, gy = (y + 0.5f) * 0.5f - 0.5f;
        gx = fminf(fmaxf(gx, 0.0f), hw - 1.001f);
        gy = fminf(fmaxf(gy, 0.0f), hh - 1.001f);
        const int x0 = int(gx), y0 = int(gy);
        const float fx = gx - x0, fy = gy - y0;
        const int x1 = min(x0 + 1, hw - 1), y1 = min(y0 + 1, hh - 1);
        const float4 a = v.hazeBuf[y0 * hw + x0], b = v.hazeBuf[y0 * hw + x1], c = v.hazeBuf[y1 * hw + x0], d = v.hazeBuf[y1 * hw + x1];
        const float4 m = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
        sum += make_float3(m.x, m.y, m.z);
    }
    if (v.debug == 1) {
        const float b = bsum / float(n) / 60.0f;
        sum = make_float3(b, b * b, 1.0f - b) * (1.0f / v.exposure) * 0.5f;
    } else if (v.debug == 2) {
        const float b = ssum / float(n) / 200.0f;
        sum = make_float3(b, b * b, 1.0f - b) * (1.0f / v.exposure) * 0.5f;
    } else if (v.debug == 3 || v.debug == 4) {   // beam nodes looked at (3) or that gave light (4), per sample, 0..200
        const float b = fminf((v.debug == 3 ? tsum : psum) / float(n) / 200.0f, 1.0f);
        sum = make_float3(b, b * b, 1.0f - b) * (1.0f / v.exposure) * 0.5f;
        if (v.badCount) { atomicAdd(v.badCount, 0u); }
        if (v.stats) { atomicAdd(&v.stats[0], (unsigned long long)tsum); atomicAdd(&v.stats[1], (unsigned long long)psum); }
    }
    // a NaN in one pixel would spread through the glare: zero it, and count it
    if (!(isfinite(sum.x) && isfinite(sum.y) && isfinite(sum.z))) {
        sum = make_float3(0, 0, 0);
        if (v.badCount) atomicAdd(v.badCount, 1u);
    }
    hdr[y * v.width + x] = make_float4(sum.x, sum.y, sum.z, 1.0f);
}

__global__ void kHaze(View v, float4* out) {
    const int hw = (v.width + 1) / 2, hh = (v.height + 1) / 2;
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= hw || y >= hh) return;
    int b, st;
    float3 g = tracePath(v, v.camPos, rayDir(v, 2.0f * x + 1.0f, 2.0f * y + 1.0f), b, st, nullptr, true, true);
    if (!(isfinite(g.x) && isfinite(g.y) && isfinite(g.z))) g = make_float3(0, 0, 0);
    out[y * hw + x] = make_float4(g.x, g.y, g.z, 0.0f);
}

__device__ __forceinline__ unsigned int pcg(unsigned int v) {
    const unsigned int state = v * 747796405u + 2891336453u;
    const unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

__device__ float4 tapHalf(const float4* img, int w, int h, float x, float y) {
    x = fminf(fmaxf(x - 0.5f, 0.0f), w - 1.001f);
    y = fminf(fmaxf(y - 0.5f, 0.0f), h - 1.001f);
    const int x0 = int(x), y0 = int(y);
    const float tx = x - x0, ty = y - y0;
    const int x1 = min(x0 + 1, w - 1), y1 = min(y0 + 1, h - 1);
    return (img[y0 * w + x0] * (1.0f - tx) + img[y0 * w + x1] * tx) * (1.0f - ty) + (img[y1 * w + x0] * (1.0f - tx) + img[y1 * w + x1] * tx) * ty;
}

__global__ void kResolve(View v, const float4* hdr, const float4* bloomHalf, uchar4* out) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= v.width || y >= v.height) return;
    const int i = y * v.width + x;
    const float4 a = hdr[i];
    float3 c = make_float3(a.x, a.y, a.z);
    if (bloomHalf) {
        const int hw = max(1, v.width / 2), hh = max(1, v.height / 2);
        const float4 b = tapHalf(bloomHalf, hw, hh, (x + 0.5f) * 0.5f, (y + 0.5f) * 0.5f);
        c += make_float3(b.x, b.y, b.z) * v.bloom;
    }
    const float nx = (x + 0.5f) / v.width * 2.0f - 1.0f, ny = (y + 0.5f) / v.height * 2.0f - 1.0f;
    c = c * ((1.0f - v.vignette * (nx * nx * 0.6f + ny * ny * 0.4f)) * v.exposure);
    c = fmaxf3(c, make_float3(0, 0, 0));
    c = agx(c);
    // the same triangular dither every frame (a fixed pattern: no grain that moves)
    const float nd = float(pcg(unsigned(i) * 3u + 1u) >> 8) * (1.0f / 16777216.0f) + float(pcg(unsigned(i) * 3u + 2u) >> 8) * (1.0f / 16777216.0f) - 1.0f;
    const float r = fminf(fmaxf(srgbOetf(c.x) * 255.0f + 0.5f + nd, 0.0f), 255.0f);
    const float gg = fminf(fmaxf(srgbOetf(c.y) * 255.0f + 0.5f + nd, 0.0f), 255.0f);
    const float b = fminf(fmaxf(srgbOetf(c.z) * 255.0f + 0.5f + nd, 0.0f), 255.0f);
    out[i] = make_uchar4((unsigned char)r, (unsigned char)gg, (unsigned char)b, 255);
}

// the tools: one pixel's first surface, and the light there by the beams and by shadow rays
__global__ void kProbe(View v, float px, float py) {
    const float3 d = rayDir(v, px, py);
    float3 o = v.camPos;
    int i, j, up;
    locate(v.g, o.x, o.z, i, j, up);
    Cell c;
    loadCell(v, i, j, up, c);
    for (int step = 0; step < 200; ++step) {
        Hit h;
        nearest(v, c, o, d, h);
        const float3 p = o + d * h.t;
        if (h.kind == HPortal) {
            int ni, nj, nup;
            cellAcross(c.i, c.j, c.up, h.k, ni, nj, nup);
            loadCell(v, ni, nj, nup, c);
            o = p;
            continue;
        }
        float vk[3];
        for (int k = 0; k < 3; ++k) vk[k] = dot2(f2(p.x - c.A[k].x, p.z - c.A[k].y), c.n[k]);
        const float3 eb = beamLight(v, c, p, h.n, 1.0f, 1);
        const float3 es = directLight(v, p, h.n);
        printf("probe (%.1f %.1f): kind %d k %d cell (%d %d %d) p (%.4f %.4f %.4f) n (%.3f %.3f %.3f) v %.4f %.4f %.4f types %d %d %d\n", px, py, h.kind, h.k,
               c.i, c.j, c.up, p.x, p.y, p.z, h.n.x, h.n.y, h.n.z, vk[0], vk[1], vk[2], c.type[0], c.type[1], c.type[2]);
        printf("   beams %.5f %.5f %.5f   shadow %.5f %.5f %.5f\n", eb.x, eb.y, eb.z, es.x, es.y, es.z);
        return;
    }
}

// one ray, its mirror points (the tools' Ö2)
__global__ void kMirrorPoints(View v, float3 o, float3 d, int maxHits, float3* hits, int* count) {
    int i, j, up;
    locate(v.g, o.x, o.z, i, j, up);
    Cell c;
    loadCell(v, i, j, up, c);
    int n = 0;
    for (int step = 0; step < 4000 && n < maxHits; ++step) {
        Hit h;
        nearest(v, c, o, d, h);
        if (h.kind == HNone) break;
        float3 p = o + d * h.t;
        if (h.kind == HPortal) {
            int ni, nj, nup;
            cellAcross(c.i, c.j, c.up, h.k, ni, nj, nup);
            loadCell(v, ni, nj, nup, c);
            o = p;
            continue;
        }
        if (h.kind != HMirror) break;
        const float ci = -dot(d, h.n);
        const float si = sqrtf(fmaxf(1.0f - ci * ci, 0.0f));
        const float st = si / cOpt.glassN, ct = sqrtf(fmaxf(1.0f - st * st, 0.0f));
        const float3 tang = d + h.n * ci;
        const float tl = length(tang);
        hits[n++] = p;
        if (tl > 1e-7f) p = p + tang * (2.0f * v.g.glass * st / (ct * tl));
        p.y = fmaxf(p.y, 0.0f);
        d = d + h.n * (2.0f * ci);
        o = p;
    }
    *count = n;
}

}   // namespace

void setOptics(const Optics& o) { cudaMemcpyToSymbol(cOpt, &o, sizeof(Optics)); }

void bakeLightMap(const View& v, const LightMapBlock* blocksDev, int nBlocks, int maxW, int maxH, unsigned bit, cudaStream_t s) {
    const dim3 b(16, 16);
    kBake<<<dim3((maxW + 15) / 16, (maxH + 15) / 16, nBlocks), b, 0, s>>>(v, blocksDev, bit);
    kDilate<<<dim3((maxW + 15) / 16, (maxH + 15) / 16, nBlocks), b, 0, s>>>(v, blocksDev);
}

void renderFrame(const View& v, float4* hdr, cudaStream_t s) {
    const dim3 b(16, 8);
    if (v.hazeSigma > 0.0f && v.hazeBuf && v.debug == 0) {
        const int hw = (v.width + 1) / 2, hh = (v.height + 1) / 2;
        kHaze<<<dim3((hw + b.x - 1) / b.x, (hh + b.y - 1) / b.y), b, 0, s>>>(v, v.hazeBuf);
    }
    if (v.hazeOnly == 1) return;   // (the tools)
    kFrame<<<dim3((v.width + b.x - 1) / b.x, (v.height + b.y - 1) / b.y), b, 0, s>>>(v, hdr);
}

void resolveFrame(const View& v, const float4* hdr, const float4* bloomHalf, uchar4* out, cudaStream_t s) {
    const dim3 b(16, 8);
    kResolve<<<dim3((v.width + b.x - 1) / b.x, (v.height + b.y - 1) / b.y), b, 0, s>>>(v, hdr, bloomHalf, out);
}

void probePixel(const View& v, float px, float py) {
    kProbe<<<1, 1>>>(v, px, py);
    cudaDeviceSynchronize();
}

int traceMirrorPoints(const View& v, float3 o, float3 d, int maxHits, float3* hitsHost) {
    float3* dh = nullptr;
    int* dc = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&dh), sizeof(float3) * maxHits);
    cudaMalloc(reinterpret_cast<void**>(&dc), sizeof(int));
    kMirrorPoints<<<1, 1>>>(v, o, d, maxHits, dh, dc);
    int n = 0;
    cudaMemcpy(&n, dc, sizeof(int), cudaMemcpyDeviceToHost);
    cudaMemcpy(hitsHost, dh, sizeof(float3) * n, cudaMemcpyDeviceToHost);
    cudaFree(dh);
    cudaFree(dc);
    return n;
}

}   // namespace ayna

namespace ayna {
size_t viewSizeCu() { return sizeof(View); }
size_t viewHazeOffsetCu() { return offsetof(View, hazeBuf); }
}
