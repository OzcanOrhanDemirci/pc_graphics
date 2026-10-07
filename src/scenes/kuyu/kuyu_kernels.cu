// Scene 10 "Kuyu": the tracer, the light and the picture (see kuyu_kernels.h). Units: metres, y up.
#include "scenes/kuyu/kuyu_kernels.h"
#include "shared/display.h"

#include <algorithm>
#include <cstdio>

namespace kuyu {

__constant__ Optics cOpt;

namespace {

__device__ __forceinline__ float tabLerp(const float* tab, float c) {
    const float x = saturate(c) * float(kOpticsN - 1);
    const int i = min(int(x), kOpticsN - 2);
    const float f = x - float(i);
    return tab[i] + (tab[i + 1] - tab[i]) * f;
}
__device__ __forceinline__ float3 tab3(const float (*tab)[kOpticsN], float c) {
    return make_float3(tabLerp(tab[0], c), tabLerp(tab[1], c), tabLerp(tab[2], c));
}

// the overlap of two discs (radii a, b, centres d apart), its area
__device__ float discOverlap(float a, float b, float d) {
    if (d >= a + b) return 0.0f;
    if (d <= fabsf(a - b)) return M_PIf * sqr(fminf(a, b));
    const float c1 = clampf((d * d + a * a - b * b) / (2.0f * d * a), -1.0f, 1.0f), c2 = clampf((d * d + b * b - a * a) / (2.0f * d * b), -1.0f, 1.0f);
    const float a1 = acosf(c1), a2 = acosf(c2);
    return a * a * (a1 - sinf(2.0f * a1) * 0.5f) + b * b * (a2 - sinf(2.0f * a2) * 0.5f);
}

__device__ __forceinline__ unsigned int pcg(unsigned int v) {
    const unsigned int state = v * 747796405u + 2891336453u;
    const unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// the small tables every ray reads, copied into each block's shared memory at its start (loadShared): the rooms' mix,
// their furniture, the glow leaving their faces, and which face of a variant a world face is under each symmetry.
// Read straight from global memory they made chains of dependent loads (room -> variant -> glow) the warps could not hide.
__shared__ unsigned char sVariantOf[256];
__shared__ float4 sFaceGlow[kMaxVariants * 6];
__shared__ unsigned char sSymFace[48 * 6];
__shared__ RoomVariant sVariants[kMaxVariants];

__device__ void loadShared(const View& v) {
    const int tid = threadIdx.x + threadIdx.y * blockDim.x, nt = blockDim.x * blockDim.y;
    for (int i = tid; i < 256; i += nt) sVariantOf[i] = v.variantOf[i];
    const int nv = min(v.nVariants, kMaxVariants);
    for (int i = tid; i < nv * 6; i += nt) sFaceGlow[i] = v.faceGlow ? v.faceGlow[i] : make_float4(0, 0, 0, 0);
    for (int i = tid; i < 48 * 6; i += nt) {
        const int g = i / 6, face = i % 6;
        const float3 vn = symApply(g, axisVec(face >> 1, (face & 1) ? -1.0f : 1.0f));
        sSymFace[i] = (unsigned char)(fabsf(vn.x) > 0.5f ? (vn.x > 0.0f ? 0 : 1) : fabsf(vn.y) > 0.5f ? (vn.y > 0.0f ? 2 : 3) : (vn.z > 0.0f ? 4 : 5));
    }
    const int* src = reinterpret_cast<const int*>(v.variants);
    int* dst = reinterpret_cast<int*>(sVariants);
    const int words = nv * int(sizeof(RoomVariant) / 4);
    for (int i = tid; i < words; i += nt) dst[i] = src[i];
    __syncthreads();
}

// ---------------------------------------------------------------------------------------------------------------
// the rooms: which furniture (variant), turned how (one of the cube's 48 symmetries), its pendulum's phase
struct RoomRef {
    int var, sym;
    float phase;
};
__device__ __forceinline__ RoomRef roomAt(const View& v, long long i, long long j, long long k) {
    const unsigned h = cellKey(i, j, k);
    RoomRef r;
    r.var = sVariantOf[h & 255u];
    r.sym = int((h >> 8) % 48u);
    r.phase = unit(h, 3) * 6.2831853f;
    return r;
}

// a per-variant octahedral table (the glow, the tail), bilinear
__device__ float3 octLookup(const float4* table, int var, float3 w) {
    const float2 e = octEncode(w);
    const int N = kGlowN;
    const float x = fminf(fmaxf(e.x * N - 0.5f, 0.0f), N - 1.001f), y = fminf(fmaxf(e.y * N - 0.5f, 0.0f), N - 1.001f);
    const int x0 = int(x), y0 = int(y);
    const float fx = x - x0, fy = y - y0;
    const float4* t = table + size_t(var) * N * N;
    const float4 a = __ldg(t + y0 * N + x0), b = __ldg(t + y0 * N + x0 + 1), c = __ldg(t + (y0 + 1) * N + x0), d = __ldg(t + (y0 + 1) * N + x0 + 1);
    const float4 m = (a * (1.0f - fx) + b * fx) * (1.0f - fy) + (c * (1.0f - fx) + d * fx) * fy;
    return make_float3(m.x, m.y, m.z);
}

// a variant's mean glow looking into it along w (its own frame): bilinear on the octahedral table
__device__ float3 glowLookup(const View& v, int var, float3 w) {
    const float2 e = octEncode(w);
    const int N = kGlowN;
    const float x = fminf(fmaxf(e.x * N - 0.5f, 0.0f), N - 1.001f), y = fminf(fmaxf(e.y * N - 0.5f, 0.0f), N - 1.001f);
    const int x0 = int(x), y0 = int(y);
    const float fx = x - x0, fy = y - y0;
    const float4* t = v.glow + size_t(var) * N * N;
    const float4 a = __ldg(t + y0 * N + x0), b = __ldg(t + y0 * N + x0 + 1), c = __ldg(t + (y0 + 1) * N + x0), d = __ldg(t + (y0 + 1) * N + x0 + 1);
    const float4 m = (a * (1.0f - fx) + b * fx) * (1.0f - fy) + (c * (1.0f - fx) + d * fx) * fy;
    return make_float3(m.x, m.y, m.z);
}

// a polygon's projected solid angle (the integral of cos over it) from a point with normal N; the vertices relative to
// the point, the part under the point's horizon clipped away (Lambert's formula, exact for a uniform emitter)
__device__ float projectedSolidAngle(const float3* p, int n, float3 N) {
    float3 c[8];
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const float3 a = p[i], b = p[(i + 1) % n];
        const float da = dot(N, a), db = dot(N, b);
        if (da >= 0.0f) c[m++] = a;
        if ((da >= 0.0f) != (db >= 0.0f)) c[m++] = a + (b - a) * (da / (da - db));
    }
    if (m < 3) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < m; ++i) {
        const float3 a = normalize(c[i]), b = normalize(c[(i + 1) % m]);
        const float3 cr = cross(a, b);
        const float l = length(cr);
        if (l < 1e-9f) continue;
        sum += acosf(clampf(dot(a, b), -1.0f, 1.0f)) * dot(N, cr) / l;
    }
    return 0.5f * fabsf(sum);
}

// the light on the structure at point q (relative to the centre of cell (I, J, K)) with normal n: the glass windows of
// the rooms round it, each a uniform emitter with its variant's mean glow towards q (K1: no occlusion yet)
__device__ float3 structureLight(const View& v, long long I, long long J, long long K, float3 q, float3 n) {
    const Lattice& g = v.g;
    const float H = g.h + g.glass;
    float3 E = make_float3(0, 0, 0);
    if (v.lightRooms == 0) return make_float3(0.05f, 0.03f, 0.015f);   // (the tools: the light's cost)
    const int R = v.lightRooms == 8 ? 0 : 1;
    int lo[3] = {-R, -R, -R}, hi[3] = {R, R, R};
    if (R == 0) {   // the eight nearest: the cells towards q's side
        for (int a = 0; a < 3; ++a) {
            if (comp(q, a) >= 0.0f) hi[a] = 1; else lo[a] = -1;
        }
    }
    for (int ox = lo[0]; ox <= hi[0]; ++ox)
        for (int oy = lo[1]; oy <= hi[1]; ++oy)
            for (int oz = lo[2]; oz <= hi[2]; ++oz) {
                const RoomRef rr = roomAt(v, I + ox, J + oy, K + oz);
                if (sVariants[rr.var].nL == 0) continue;
                const float3 rc = make_float3(ox * g.C, oy * g.C, oz * g.C) - q;   // the room's centre from q
                for (int fa = 0; fa < 3; ++fa) {
                    const float x = -comp(rc, fa);   // q from the room's centre along fa
                    if (fabsf(x) <= H) continue;
                    const float s = x > 0.0f ? 1.0f : -1.0f;
                    const int a1 = (fa + 1) % 3, a2 = (fa + 2) % 3;
                    const float e1 = g.h - g.b, e2 = g.h - g.frame;   // the window: bundles lie on the a1 edges
                    float3 fc = rc;
                    setComp(fc, fa, comp(rc, fa) + s * H);
                    float3 p[4];
                    const float sg[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
                    for (int k = 0; k < 4; ++k) {
                        p[k] = fc;
                        setComp(p[k], a1, comp(fc, a1) + sg[k][0] * e1);
                        setComp(p[k], a2, comp(fc, a2) + sg[k][1] * e2);
                    }
                    const float om = projectedSolidAngle(p, 4, n);
                    if (om <= 0.0f) continue;
                    const float3 w = normalize(fc);   // looking into the room
                    E += glowLookup(v, rr.var, symApply(rr.sym, w)) * om;
                }
            }
    return E;
}

// ---------------------------------------------------------------------------------------------------------------
// inside a room: the walls (silver film, copper frame along the edges), the free-standing mirrors, the metal spheres,
// the bulbs and their wires. p on the inner face of a wall, d going in, both in the variant's frame; w the path's
// weight, fp the footprint's radius at p, coneS its spread a metre
__device__ float3 pendulumCentre(const RoomSphere& s, float time, float phase) {
    if (s.swing == 0.0f) return s.c;
    const float th = s.swing * sinf(6.2831853f * time / s.period + phase);
    return make_float3(s.c.x + s.len * sinf(th), s.c.y + s.len * (1.0f - cosf(th)), s.c.z);
}

// the share of a footprint (radius R, centre dist from the wire's axis) a wire of radius rw covers: the strip's width
// across the disc, a plain linear overlap
__device__ __forceinline__ float wireCover(float rw, float R, float dist) {
    const float lo = fmaxf(dist - R, -rw), hi = fminf(dist + R, rw);
    return hi > lo ? fminf(1.0f, (hi - lo) / (2.0f * R)) : 0.0f;
}
// closest approach of the segment p + d t (t in [0, tEnd]) to the segment A..B: the ray's t and the distance
__device__ bool segClosest(float3 p, float3 d, float tEnd, float3 A, float3 B, float& t, float& dist) {
    const float3 u = B - A;
    const float uu = dot(u, u), du = dot(d, u);
    const float3 w0 = p - A;
    const float den = uu - du * du;   // |d| = 1
    if (den < 1e-10f) return false;
    const float dw = dot(d, w0), uw = dot(u, w0);
    t = (du * uw - uu * dw) / den;
    const float s = (uw + du * t) / uu;
    if (t <= 0.0f || t >= tEnd || s < 0.0f || s > 1.0f) return false;
    dist = length(w0 + d * t - u * s);
    return true;
}

__device__ float3 roomInterior(const View& v, int var, float phase, float3 p, float3 d, float3 w, float fp, float coneS, int& bounces,
                             int maxBounces, bool tail) {
    const RoomVariant& rv = sVariants[var];
    const float h = v.g.h, fr = h - v.g.frame;
    float3 L = make_float3(0, 0, 0);
    const float wEntry = maxc(w);
    if (rv.nL == 0) return L;   // a dark room: nothing of its own to show
    // the cut-off: no bulb can give more than w I / (pi max(r, R)^2) (its share of the footprint R), and R only grows
    const float cutK = rv.Imax * v.exposure * v.cutEncounters / (M_PIf * v.cutEps);
    float3 sc[kMaxRoomSpheres];
    for (int s = 0; s < rv.nS; ++s) sc[s] = pendulumCentre(rv.S[s], v.time, phase);
    const float rw = 0.0008f;   // the wires
    for (bounces = 0;; ++bounces) {
        if (bounces >= maxBounces) {
            // the rest of the way by its expectation: past the exact bounces the bulbs' images are dots under a pixel,
            // ever denser; their mean along d is what a pixel would gather (deterministic, smooth)
            if (tail && v.tail) L += w * octLookup(v.tail, var, d);
            break;
        }
        // the walls ahead
        float tHit = 1e30f;
        int kind = 0, idx = 0;
        for (int a = 0; a < 3; ++a) {
            const float da = comp(d, a);
            if (da == 0.0f) continue;
            const float t = ((da > 0.0f ? h : -h) - comp(p, a)) / da;
            if (t < tHit) {
                tHit = t;
                idx = a;
            }
        }
        tHit = fmaxf(tHit, 0.0f);
        // the spheres (entering only)
        for (int s = 0; s < rv.nS; ++s) {
            const float3 o = p - sc[s];
            const float b = dot(o, d);
            if (b >= 0.0f) continue;
            const float cc = dot(o, o) - rv.S[s].r * rv.S[s].r;
            if (cc <= 0.0f) continue;
            const float disc = b * b - cc;
            if (disc <= 0.0f) continue;
            const float t = -b - sqrtf(disc);
            if (t > 1e-6f && t < tHit) {
                tHit = t;
                kind = 1;
                idx = s;
            }
        }
        // the free-standing mirrors
        for (int q = 0; q < rv.nP; ++q) {
            const RoomPanel& P = rv.P[q];
            const float dn = dot(d, P.n);
            if (fabsf(dn) < 1e-8f) continue;
            const float t = dot(P.c - p, P.n) / dn;
            if (t <= 1e-6f || t >= tHit) continue;
            const float3 x = p + d * t - P.c;
            if (fabsf(dot(x, P.u)) <= P.hu && fabsf(dot(x, P.w)) <= P.hw) {
                tHit = t;
                kind = 2;
                idx = q;
            }
        }
        // the bulbs by the footprint (a bulb below a pixel stays a steady dot), their wires
        for (int l = 0; l < rv.nL; ++l) {
            const RoomLight& Lt = rv.L[l];
            const float tc = dot(Lt.p - p, d);
            if (tc > 0.0f && tc < tHit) {
                const float R = fmaxf(fp + coneS * tc, 1e-5f);
                const float3 q = p + d * tc - Lt.p;
                const float dist = length(q);
                if (dist < Lt.r + R) {
                    const float cover = fminf(1.0f, discOverlap(Lt.r, R, dist) / (M_PIf * R * R));
                    L += w * Lt.I * (cover / (M_PIf * Lt.r * Lt.r));
                    w *= (1.0f - cover);
                }
            }
            if (Lt.wire) {
                float t, dist;
                if (segClosest(p, d, tHit, make_float3(Lt.p.x, Lt.p.y + Lt.r, Lt.p.z), make_float3(Lt.p.x, h, Lt.p.z), t, dist)) {
                    const float R = fmaxf(fp + coneS * t, 1e-5f);
                    w *= (1.0f - wireCover(rw, R, dist));
                }
            }
        }
        for (int s = 0; s < rv.nS; ++s) {
            if (rv.S[s].len <= 0.0f) continue;
            float t, dist;
            const float3 pivot = make_float3(rv.S[s].c.x, rv.S[s].c.y + rv.S[s].len, rv.S[s].c.z);
            if (segClosest(p, d, tHit, pivot, sc[s], t, dist)) {
                const float R = fmaxf(fp + coneS * t, 1e-5f);
                w *= (1.0f - wireCover(0.0015f, R, dist));
            }
        }
        p = p + d * tHit;
        fp += coneS * tHit;
        if (kind == 0) {
            const float da = comp(d, idx);
            const float ci = fabsf(da);
            setComp(p, idx, da > 0.0f ? h : -h);
            const int a1 = (idx + 1) % 3, a2 = (idx + 2) % 3;
            // the chromed frame along the edges, its border filtered by the footprint
            const float m = fmaxf(fabsf(comp(p, a1)), fabsf(comp(p, a2)));
            const float fw = fp / fmaxf(ci, v.grazeClamp);
            const float cover = smoothstepf(fr - fw, fr + fw, m);
            w *= lerp3(tab3(cOpt.inR, ci), tab3(cOpt.chrome, ci), cover);
            setComp(d, idx, -da);
        } else if (kind == 1) {
            const float3 n = (p - sc[idx]) * (1.0f / rv.S[idx].r);
            const float ci = -dot(d, n);
            w *= rv.S[idx].metal == 1 ? tab3(cOpt.copper, ci) : tab3(cOpt.chrome, ci);
            d = d + n * (2.0f * ci);
            coneS += 2.0f * fp / rv.S[idx].r;   // a convex mirror spreads the footprint
        } else {
            const float3 n = rv.P[idx].n;
            const float dn = dot(d, n);
            w *= tab3(cOpt.inR, fabsf(dn));
            d = d - n * (2.0f * dn);
        }
        if (maxc(w) < wEntry * v.minWeight) break;
        if (maxc(w) * cutK < fmaxf(fp * fp, 4e-4f)) break;   // (r of the bulbs: 2 cm)
    }
    return L;
}

// ---------------------------------------------------------------------------------------------------------------
// outside: a cell's nearest surface along ol + t d (ol relative to the cell's centre), t in (t0, t1)
struct Hit {
    float t;
    int kind;     // 0 none, 1 a bundle, 2 a room's glass
    int q;        // the bundle, or the room's face axis
    int face;     // the bundle's face entered: 0 +u, 1 -u, 2 +v, 3 -v; the room's: the sign (+1 / -1)
};

__device__ void cellNearest(const View& v, float3 ol, float3 d, float t0, float t1, Hit& h) {
    const Lattice& g = v.g;
    h.kind = 0;
    h.t = t1;
    // the room's glass (its outer face), entering
    {
        const float H = g.h + g.glass;
        float tn = -1e30f, tf = 1e30f;
        int ax = 0;
        for (int a = 0; a < 3; ++a) {
            const float o = comp(ol, a), da = comp(d, a);
            if (fabsf(da) < 1e-12f) {
                if (fabsf(o) > H) tn = 1e30f;
                continue;
            }
            const float inv = 1.0f / da;
            float ta = (-H - o) * inv, tb = (H - o) * inv;
            if (ta > tb) { const float x = ta; ta = tb; tb = x; }
            if (ta > tn) { tn = ta; ax = a; }
            tf = fminf(tf, tb);
        }
        if (tn < tf && tn > t0 && tn < h.t) {
            h.t = tn;
            h.kind = 2;
            h.q = ax;
            h.face = comp(d, ax) > 0.0f ? -1 : 1;
        }
    }
    // the bundles: squares in their crosswise planes, endless along their axes
    const float hb = 0.5f * g.b;
    for (int q = 0; q < 12; ++q) {
        const BundleShape s = bundleShape(g, q);
        const float ou = comp(ol, s.u) - s.cu, ov = comp(ol, s.v) - s.cv;
        const float du = comp(d, s.u), dv = comp(d, s.v);
        float un = -1e30f, uf = 1e30f, vn = -1e30f, vf = 1e30f;
        if (fabsf(du) < 1e-12f) {
            if (fabsf(ou) > hb) continue;
        } else {
            const float inv = 1.0f / du;
            un = (-hb - ou) * inv;
            uf = (hb - ou) * inv;
            if (un > uf) { const float x = un; un = uf; uf = x; }
        }
        if (fabsf(dv) < 1e-12f) {
            if (fabsf(ov) > hb) continue;
        } else {
            const float inv = 1.0f / dv;
            vn = (-hb - ov) * inv;
            vf = (hb - ov) * inv;
            if (vn > vf) { const float x = vn; vn = vf; vf = x; }
        }
        const float tn = fmaxf(un, vn), tf = fminf(uf, vf);
        if (tn < tf && tn > t0 && tn < h.t) {
            h.t = tn;
            h.kind = 1;
            h.q = q;
            if (un >= vn) h.face = du < 0.0f ? 0 : 1;
            else h.face = dv < 0.0f ? 2 : 3;
        }
    }
}

// the first surface along o + t d, t < tMax (o relative to the centre of cell (0, 0, 0) of the walk); the cell it
// is in comes back in (cx, cy, cz)
__device__ void walkFirst(const View& v, float3 o, float3 d, float tMax, int maxCells, Hit& h, int& cx, int& cy, int& cz) {
    const float C = v.g.C;
    cx = int(floorf(o.x / C + 0.5f));
    cy = int(floorf(o.y / C + 0.5f));
    cz = int(floorf(o.z / C + 0.5f));
    const int sx = d.x > 0.0f ? 1 : -1, sy = d.y > 0.0f ? 1 : -1, sz = d.z > 0.0f ? 1 : -1;
    const float big = 1e30f;
    float tmx = fabsf(d.x) > 1e-12f ? ((float(cx) + 0.5f * sx) * C - o.x) / d.x : big;
    float tmy = fabsf(d.y) > 1e-12f ? ((float(cy) + 0.5f * sy) * C - o.y) / d.y : big;
    float tmz = fabsf(d.z) > 1e-12f ? ((float(cz) + 0.5f * sz) * C - o.z) / d.z : big;
    const float tdx = fabsf(d.x) > 1e-12f ? C / fabsf(d.x) : big, tdy = fabsf(d.y) > 1e-12f ? C / fabsf(d.y) : big,
                tdz = fabsf(d.z) > 1e-12f ? C / fabsf(d.z) : big;
    float t = 0.0f;
    h.kind = 0;
    for (int step = 0; step < maxCells && t < tMax; ++step) {
        const float tNext = fminf(fminf(tmx, fminf(tmy, tmz)), tMax);
        const float3 ol = o - make_float3(float(cx) * C, float(cy) * C, float(cz) * C);
        cellNearest(v, ol, d, t, tNext, h);
        if (h.kind) return;
        t = tNext;
        if (tmx <= tmy && tmx <= tmz) { cx += sx; tmx += tdx; }
        else if (tmy <= tmz) { cy += sy; tmy += tdy; }
        else { cz += sz; tmz += tdz; }
    }
    h.kind = 0;
    h.t = tMax;
}

// a bundle face's frame: its normal axis and sign, its crosswise axis and the bundle square's centre on both
__device__ __forceinline__ void faceFrame(const BundleShape& s, int f, int& nAx, float& sN, int& wAx, float& cN, float& cW) {
    nAx = f < 2 ? s.u : s.v;
    wAx = f < 2 ? s.v : s.u;
    sN = (f & 1) ? -1.0f : 1.0f;
    cN = f < 2 ? s.cu : s.cv;
    cW = f < 2 ? s.cv : s.cu;
}

// the structure's light from the table at point q (relative to the centre of cell (I, J, K)) on bundle bq's face f
__device__ float3 tableLight(const View& v, long long I, long long J, long long K, int bq, int f, float3 q) {
    const Lattice& g = v.g;
    const BundleShape s = bundleShape(g, bq);
    int nAx, wAx;
    float sN, cN, cW;
    faceFrame(s, f, nAx, sN, wAx, cN, cW);
    if (v.prof & 1) return make_float3(0.05f, 0.03f, 0.015f);
    const float hb = 0.5f * g.b;
    const int ia = min(v.ltNa - 1, max(0, int((comp(q, s.a) + 0.5f * g.C) * (float(v.ltNa) / g.C))));
    const int iw = min(v.ltNw - 1, max(0, int((comp(q, wAx) - cW + hb) * (float(v.ltNw) / g.b))));
    const int texel = ((bq * 4 + f) * v.ltNa + ia) * v.ltNw + iw;
    float3 E = make_float3(0, 0, 0);
    const uint4 ids = __ldg(reinterpret_cast<const uint4*>(v.ltIdx) + texel);
    const float4* gp = reinterpret_cast<const float4*>(v.ltG) + size_t(texel) * (kLtK / 4);
    const float4 g4[4] = {__ldg(gp), __ldg(gp + 1), __ldg(gp + 2), __ldg(gp + 3)};
    const unsigned words[4] = {ids.x, ids.y, ids.z, ids.w};
#pragma unroll
    for (int k = 0; k < kLtK; ++k) {
        const int idx = (words[k >> 2] >> ((k & 3) * 8)) & 255;
        if (idx == 255) continue;
        const float4 gq = g4[k >> 2];
        const float G = (k & 3) == 0 ? gq.x : (k & 3) == 1 ? gq.y : (k & 3) == 2 ? gq.z : gq.w;
        const int r = idx / 6, face = idx % 6;
        const RoomRef rr = roomAt(v, I + r / 9 - 1, J + (r / 3) % 3 - 1, K + r % 3 - 1);
        const float4 Lf = sFaceGlow[rr.var * 6 + sSymFace[rr.sym * 6 + face]];
        E += make_float3(Lf.x, Lf.y, Lf.z) * G;
    }
    const float2 far = __ldg(v.ltFar + texel);
    const float sigT = v.hazeSigma + v.hazeAbsorb;
    E += v.meanFace * far.x + v.hazeAmbient * (sigT > 0.0f ? v.hazeSigma / sigT : 0.0f) * far.y;
    return E;
}

__device__ __forceinline__ float glowShapeAt(const View& v, float c) {
    const float x = saturate(c) * float(kShapeN) - 0.5f;
    const int i = min(kShapeN - 2, max(0, int(floorf(x))));
    const float f = saturate(x - float(i));
    return v.glowShape[i] + (v.glowShape[i + 1] - v.glowShape[i]) * f;
}

// the table, one texel a thread: for every room face round the cell, its exact projected solid angle times the share
// of it the texel sees (shadow rays to a grid of points on its window, weighted by cos cos / d^2 and the glow's angular
// shape); the rooms beyond and the mist by cosine-weighted rays (a Fibonacci spiral: no random numbers)
__global__ void kLightTable(View v, unsigned char* outIdx, float* outG, float2* outFar, int ns, int farRays) {
    const int total = 48 * v.ltNa * v.ltNw;
    const int texel = blockIdx.x * blockDim.x + threadIdx.x;
    if (texel >= total) return;
    const Lattice& g = v.g;
    const int iw = texel % v.ltNw, ia = (texel / v.ltNw) % v.ltNa, f = (texel / (v.ltNw * v.ltNa)) % 4, bq = texel / (v.ltNw * v.ltNa * 4);
    const BundleShape s = bundleShape(g, bq);
    int nAx, wAx;
    float sN, cN, cW;
    faceFrame(s, f, nAx, sN, wAx, cN, cW);
    const float hb = 0.5f * g.b, H = g.h + g.glass;
    float3 P;
    setComp(P, s.a, -0.5f * g.C + (float(ia) + 0.5f) * g.C / float(v.ltNa));
    setComp(P, nAx, cN + sN * hb);
    setComp(P, wAx, cW - hb + (float(iw) + 0.5f) * g.b / float(v.ltNw));
    const float3 n = axisVec(nAx, sN);
    const float3 o = P + n * 2e-4f;
    const float sigT = v.hazeSigma + v.hazeAbsorb;
    unsigned char ki[kLtK];
    float kg[kLtK];
    int nk = 0;
    float spill = 0.0f;
    for (int r = 0; r < 27; ++r) {
        const int ox = r / 9 - 1, oy = (r / 3) % 3 - 1, oz = r % 3 - 1;
        const float3 rc = make_float3(ox * g.C, oy * g.C, oz * g.C) - P;
        for (int fa = 0; fa < 3; ++fa) {
            const float x = -comp(rc, fa);
            if (fabsf(x) <= H) continue;
            const float sg = x > 0.0f ? 1.0f : -1.0f;
            const int a1 = (fa + 1) % 3, a2 = (fa + 2) % 3;
            const float e1 = g.h - g.b, e2 = g.h - g.frame;
            float3 fc = rc;
            setComp(fc, fa, comp(rc, fa) + sg * H);
            float3 poly[4];
            const float sgn[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int k = 0; k < 4; ++k) {
                poly[k] = fc;
                setComp(poly[k], a1, comp(fc, a1) + sgn[k][0] * e1);
                setComp(poly[k], a2, comp(fc, a2) + sgn[k][1] * e2);
            }
            const float om = projectedSolidAngle(poly, 4, n);
            if (om <= 1e-7f) continue;
            float sumAll = 0.0f, sumVis = 0.0f;
            for (int py = 0; py < ns; ++py)
                for (int px = 0; px < ns; ++px) {
                    float3 Q = fc;
                    setComp(Q, a1, comp(fc, a1) + e1 * (2.0f * (px + 0.5f) / ns - 1.0f));
                    setComp(Q, a2, comp(fc, a2) + e2 * (2.0f * (py + 0.5f) / ns - 1.0f));
                    const float dist = length(Q);
                    const float3 dir = Q * (1.0f / dist);
                    const float cp = dot(n, dir);
                    if (cp <= 0.0f) continue;
                    const float cf = fabsf(comp(dir, fa));
                    const float w = cp * cf / (dist * dist);
                    sumAll += w;
                    Hit hh;
                    int cx, cy, cz;
                    walkFirst(v, o, dir, dist - 2e-3f, 8, hh, cx, cy, cz);
                    if (hh.kind == 0) sumVis += w * glowShapeAt(v, cf) * __expf(-sigT * dist);
                }
            if (sumAll <= 0.0f) continue;
            const float G = om * sumVis / sumAll;
            if (G <= 0.0f) continue;
            const unsigned char id = (unsigned char)(r * 6 + fa * 2 + (sg > 0.0f ? 0 : 1));
            // keep the kLtK largest, the rest to the rooms beyond (at the mean glow)
            if (nk < kLtK) {
                ki[nk] = id;
                kg[nk] = G;
                ++nk;
            } else {
                int mi = 0;
                for (int k = 1; k < kLtK; ++k)
                    if (kg[k] < kg[mi]) mi = k;
                if (kg[mi] < G) {
                    spill += kg[mi];
                    ki[mi] = id;
                    kg[mi] = G;
                } else {
                    spill += G;
                }
            }
        }
    }
    // the rooms beyond the 27 and the mist: cosine-weighted directions round n
    const float3 t1 = fabsf(n.x) < 0.9f ? normalize(cross(n, make_float3(1, 0, 0))) : normalize(cross(n, make_float3(0, 1, 0)));
    const float3 t2 = cross(n, t1);
    float farRoom = 0.0f, mist = 0.0f;
    for (int k = 0; k < farRays; ++k) {
        const float u = (float(k) + 0.5f) / float(farRays);
        const float ph = float(k) * 2.39996323f;
        const float rr = sqrtf(u);
        const float3 dir = normalize(t1 * (rr * cosf(ph)) + t2 * (rr * sinf(ph)) + n * sqrtf(fmaxf(0.0f, 1.0f - u)));
        Hit hh;
        int cx, cy, cz;
        walkFirst(v, o, dir, 1200.0f, 150, hh, cx, cy, cz);
        const float tr = __expf(-sigT * hh.t);
        mist += 1.0f - tr;
        if (hh.kind == 2 && (abs(cx) > 1 || abs(cy) > 1 || abs(cz) > 1)) {
            const float cf = fabsf(comp(dir, hh.q));
            const float3 qq = o + dir * hh.t - make_float3(cx * g.C, cy * g.C, cz * g.C);
            const int b1 = (hh.q + 1) % 3, b2 = (hh.q + 2) % 3;
            if (fmaxf(fabsf(comp(qq, b1)), fabsf(comp(qq, b2))) <= H - g.frame) farRoom += tr * glowShapeAt(v, cf);
        }
    }
    const float scale = M_PIf / float(farRays);
    for (int k = 0; k < kLtK; ++k) {
        outIdx[size_t(texel) * kLtK + k] = k < nk ? ki[k] : 255;
        outG[size_t(texel) * kLtK + k] = k < nk ? kg[k] : 0.0f;
    }
    outFar[texel] = make_float2(farRoom * scale + spill, mist * scale);
}

// the far terms are low-frequency light: a box blur along the bundle and across the face (rays, not a field)
__global__ void kBlurFar(View v, const float2* in, float2* out, int ra, int rw) {
    const int total = 48 * v.ltNa * v.ltNw;
    const int texel = blockIdx.x * blockDim.x + threadIdx.x;
    if (texel >= total) return;
    const int iw = texel % v.ltNw, ia = (texel / v.ltNw) % v.ltNa, face = texel / (v.ltNw * v.ltNa);
    float2 acc = make_float2(0, 0);
    float n = 0.0f;
    for (int da = -ra; da <= ra; ++da)
        for (int dw = -rw; dw <= rw; ++dw) {
            const int a = (ia + da + v.ltNa) % v.ltNa, w = iw + dw;   // along: the cell repeats
            if (w < 0 || w >= v.ltNw) continue;
            const float2 q = in[(face * v.ltNa + a) * v.ltNw + w];
            acc.x += q.x;
            acc.y += q.y;
            n += 1.0f;
        }
    out[texel] = make_float2(acc.x / n, acc.y / n);
}

// a slat: how far it stands out (0..1 of the relief), constant along its whole line
__device__ __forceinline__ float slatOut(unsigned key, int face, int j) {
    return float((hash32(key + (unsigned)(face * 64 + j) * 0x9E3779B9u) >> 20) & 3u) * (1.0f / 3.0f);
}

// a thread's look (v.threads to a slat; the bundle's fine lines, the tesseract's stretched book edges): its colour and
// brightness, its finish (matte, varnished, gold leaf), constant along its line. jt: the thread across the face; the
// corner posts are thread 4095
struct Finish {
    float3 alb;      // the diffuse colour (none for gold)
    float varn;      // share varnished (a clear coat: Fresnel's reflection on top)
    float metal;     // share gold leaf
};
__device__ __forceinline__ Finish threadLook(const View& v, unsigned key, int face, int jt) {
    const unsigned h = hash32(key ^ (unsigned)(face * 4096 + jt + 1) * 0x9E3779B9u);
    const int c = v.threadMap[h & 15u];
    Finish f;
    const int fin = v.slatFinish[c];
    f.varn = fin == 1 ? 1.0f : 0.0f;
    f.metal = fin == 2 ? 1.0f : 0.0f;
    f.alb = fin == 2 ? make_float3(0, 0, 0) : v.slatAlbedo[c] * (0.7f + 0.6f * float((h >> 8) & 255u) * (1.0f / 255.0f));
    return f;
}
__device__ __forceinline__ void addLook(Finish& a, const Finish& b, float w) {
    a.alb += b.alb * w;
    a.varn += b.varn * w;
    a.metal += b.metal * w;
}

// the looks averaged over [lo, hi] across a face (a box filter of the footprint): thread by thread while it spans
// few, slat by slat (each its threads' mean) when it spans many
__device__ Finish lookOver(const View& v, unsigned key, int face, float lo, float hi, float hb, float inner, float sw, int k) {
    Finish acc;
    acc.alb = make_float3(0, 0, 0);
    acc.varn = acc.metal = 0.0f;
    lo = fmaxf(lo, -hb);
    hi = fminf(hi, hb);
    if (hi - lo < 1e-5f) {
        const float m = 0.5f * (lo + hi);
        lo = m - 5e-6f;
        hi = m + 5e-6f;
    }
    float wsum = 0.0f;
    const Finish post = threadLook(v, key, face, 4095);
    if (lo < -inner) { const float l = fminf(hi, -inner) - lo; if (l > 0.0f) { addLook(acc, post, l); wsum += l; } }
    if (hi > inner) { const float l = hi - fmaxf(lo, inner); if (l > 0.0f) { addLook(acc, post, l); wsum += l; } }
    const int nt = v.threads;
    const float tw = sw / float(nt);
    const float a = fmaxf(lo, -inner), b = fminf(hi, inner);
    if (b > a) {
        if (b - a <= 12.0f * tw) {
            const int i0 = max(0, int(floorf((a + inner) / tw))), i1 = min(k * nt - 1, int(floorf((b + inner) / tw)));
            for (int i = i0; i <= i1; ++i) {
                const float l = fminf(b, -inner + float(i + 1) * tw) - fmaxf(a, -inner + float(i) * tw);
                if (l <= 0.0f) continue;
                addLook(acc, threadLook(v, key, face, i), l);
                wsum += l;
            }
        } else {
            const int j0 = max(0, int(floorf((a + inner) / sw))), j1 = min(k - 1, int(floorf((b + inner) / sw)));
            for (int j = j0; j <= j1; ++j) {
                const float l = fminf(b, -inner + float(j + 1) * sw) - fmaxf(a, -inner + float(j) * sw);
                if (l <= 0.0f) continue;
                Finish m;
                m.alb = make_float3(0, 0, 0);
                m.varn = m.metal = 0.0f;
                for (int t = 0; t < nt; ++t) addLook(m, threadLook(v, key, face, j * nt + t), 1.0f / float(nt));
                addLook(acc, m, l);
                wsum += l;
            }
        }
    }
    const float inv = 1.0f / fmaxf(wsum, 1e-12f);
    acc.alb *= inv;
    acc.varn *= inv;
    acc.metal *= inv;
    return acc;
}

// a bundle's face: the slats' relief walked exactly (a height field across the face, constant along the axis), or,
// once the footprint covers a slat, the flat face; the threads' looks filtered by the footprint either way. t and n
// are set.
__device__ Finish bundleSurface(const View& v, const Hit& hh, float3 ol, float3 d, float fpAt, unsigned key, float& t, float3& n) {
    const Lattice& g = v.g;
    const BundleShape s = bundleShape(g, hh.q);
    const int nAx = hh.face < 2 ? s.u : s.v, wAx = hh.face < 2 ? s.v : s.u;
    const float sN = (hh.face & 1) ? -1.0f : 1.0f;
    const float cW = hh.face < 2 ? s.cv : s.cu;
    const float hb = 0.5f * g.b, inner = hb - g.P;
    const int k = min(g.slats, kStripMax);
    const float sw = 2.0f * inner / float(k);
    const float dn = sN * comp(d, nAx);   // < 0: entering
    const float dw = comp(d, wAx);
    const float t0 = hh.t;
    const float w0 = comp(ol, wAx) - cW + dw * t0;
    const float3 N = axisVec(nAx, sN);
    // the footprint on the face, across the slats
    const float fw = fpAt / fmaxf(-dn, v.grazeClamp);
    const float far = smoothstepf(0.25f * sw, 0.6f * sw, fw);
    Finish out;
    out.alb = make_float3(0, 0, 0);
    out.varn = out.metal = 0.0f;
    t = t0;
    n = N;
    if (far < 1.0f && !(v.prof & 4)) {
        // the relief: slat j's top lies (1 - out_j) P under the bounding face; corner posts at full height
        float tt = t0;
        float3 nn = N;
        Finish a;
        bool done = false;
        if (fabsf(w0) < inner) {
            int j = min(k - 1, max(0, int(floorf((w0 + inner) / sw))));
            for (int guard = 0; guard < kStripMax + 2; ++guard) {
                const float depth = (1.0f - slatOut(key, hh.face, j)) * g.P;
                const float tD = t0 + depth / fmaxf(-dn, 1e-7f);
                float tW = 1e30f;
                if (dw > 0.0f) tW = t0 + ((-inner + float(j + 1) * sw) - w0) / dw;
                else if (dw < 0.0f) tW = t0 + ((-inner + float(j) * sw) - w0) / dw;
                if (tD <= tW) {   // its top
                    tt = tD;
                    const float wh = w0 + dw * (tD - t0);
                    a = lookOver(v, key, hh.face, wh - fw, wh + fw, hb, inner, sw, k);
                    done = true;
                    break;
                }
                const int j2 = j + (dw > 0.0f ? 1 : -1);
                const float depthNow = (tW - t0) * (-dn);
                const float3 side = axisVec(wAx, dw > 0.0f ? -1.0f : 1.0f);
                if (j2 < 0 || j2 >= k) {   // the corner post's side
                    tt = tW;
                    nn = side;
                    a = threadLook(v, key, hh.face, 4095);
                    done = true;
                    break;
                }
                if ((1.0f - slatOut(key, hh.face, j2)) * g.P <= depthNow) {   // the next slat stands higher: its side
                    tt = tW;
                    nn = side;
                    a = threadLook(v, key, hh.face, j2 * v.threads + (dw > 0.0f ? 0 : v.threads - 1));
                    a.alb *= 0.85f;
                    done = true;
                    break;
                }
                j = j2;
            }
        }
        if (!done) a = threadLook(v, key, hh.face, 4095);   // entered over a corner post
        addLook(out, a, 1.0f - far);
        t = tt;
        n = nn;
    }
    if (far > 0.0f) {
        addLook(out, lookOver(v, key, hh.face, w0 - fw, w0 + fw, hb, inner, sw, k), far);
        if (far >= 1.0f) {
            t = t0;
            n = N;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
__device__ float3 glassRoom(const View& v, long long I, long long J, long long K, float3 p, float3 d, int fa, float fs, float3 w, float fp,
                            float coneS, int& bounces) {
    const Lattice& g = v.g;
    // through the half-inch slab: refracted inside, out of the inner face parallel to where it came from
    const float3 nOut = axisVec(fa, fs);
    const float ci = -dot(d, nOut);
    const float eta = 1.0f / cOpt.glassN;
    const float ct = sqrtf(fmaxf(1.0f - eta * eta * (1.0f - ci * ci), 1e-6f));
    const float3 dt = d * eta + nOut * (eta * ci - ct);
    float3 pin = p + dt * (g.glass / ct);
    setComp(pin, fa, fs * g.h);
    const RoomRef rr = roomAt(v, I, J, K);
    const float3 pv = symApply(rr.sym, pin), dv = symApply(rr.sym, d);
    return roomInterior(v, rr.var, rr.phase, pv, dv, w, fp, coneS, bounces, v.exactBounces, true);
}

// a sample's way into a room, left for kFrame: the samples of a pixel entering the same room face share its interior
struct RoomReq {
    int has;
    int cx, cy, cz, fa;
    float fs;
    float3 p, d, w;
    float fp, coneS;
};

// a ray from o (relative to the camera's cell's centre) along d: outside from cell to cell, into a room when it meets
// one; the room's glass reflects a little too, and that ray is followed after (once). With req the room's inside is
// not traced here but handed back.
__device__ float3 tracePath(const View& v, float3 o, float3 d, int& cellsOut, int& bouncesOut, float* firstT = nullptr, RoomReq* req = nullptr) {
    const Lattice& g = v.g;
    const float C = g.C;
    float3 L = make_float3(0, 0, 0), thr = make_float3(1, 1, 1);
    float coneS = v.coneScale * v.pixAngle;
    float fp0 = 0.0f;
    bool pending = false;
    float3 po = o, pd = d, pthr = thr;
    float pfp = 0.0f;
    cellsOut = 0;
    bouncesOut = 0;
    const float sigT = v.hazeSigma + v.hazeAbsorb;
    const float3 hazeSrc = v.hazeAmbient * (sigT > 0.0f ? v.hazeSigma / sigT : 0.0f);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            if (!pending) break;
            o = po;
            d = pd;
            thr = pthr;
            fp0 = pfp;
            pending = false;
        }
        int cx = int(floorf(o.x / C + 0.5f)), cy = int(floorf(o.y / C + 0.5f)), cz = int(floorf(o.z / C + 0.5f));
        const int sx = d.x > 0.0f ? 1 : -1, sy = d.y > 0.0f ? 1 : -1, sz = d.z > 0.0f ? 1 : -1;
        const float big = 1e30f;
        float tmx = fabsf(d.x) > 1e-12f ? ((float(cx) + 0.5f * sx) * C - o.x) / d.x : big;
        float tmy = fabsf(d.y) > 1e-12f ? ((float(cy) + 0.5f * sy) * C - o.y) / d.y : big;
        float tmz = fabsf(d.z) > 1e-12f ? ((float(cz) + 0.5f * sz) * C - o.z) / d.z : big;
        const float tdx = fabsf(d.x) > 1e-12f ? C / fabsf(d.x) : big, tdy = fabsf(d.y) > 1e-12f ? C / fabsf(d.y) : big,
                    tdz = fabsf(d.z) > 1e-12f ? C / fabsf(d.z) : big;
        float t = 0.0f;
        bool ended = false;
        for (int step = 0; step < v.maxCells; ++step) {
            ++cellsOut;
            const float tNext = fminf(tmx, fminf(tmy, tmz));
            const float3 ctr = make_float3(float(cx) * C, float(cy) * C, float(cz) * C);
            const float3 ol = o - ctr;
            Hit h;
            cellNearest(v, ol, d, t, tNext, h);
            const float tEnd = h.kind ? h.t : tNext;
            if (sigT > 0.0f) {
                const float tr = __expf(-sigT * (tEnd - t));
                L += thr * hazeSrc * (1.0f - tr);
                thr *= tr;
            }
            const long long I = v.camI + cx, J = v.camJ + cy, K = v.camK + cz;
            if (h.kind == 1) {
                const unsigned key = lineKey(h.q, I, J, K);
                float th;
                float3 n;
                const float fpAt = fp0 + coneS * h.t;
                const Finish fin = bundleSurface(v, h, ol, d, fpAt, key, th, n);
                const float3 q = ol + d * th;
                if (firstT && pass == 0) *firstT = th;
                float3 E;
                if (v.useTable && v.ltIdx) {
                    const BundleShape bs = bundleShape(g, h.q);
                    int f = h.face;
                    float share = 1.0f;
                    if (fabsf(dot(n, axisVec(h.face < 2 ? bs.u : bs.v, 1.0f))) < 0.5f) {   // a slat's side: the face it looks along
                        const int wAx = h.face < 2 ? bs.v : bs.u;
                        const float sw = comp(n, wAx);
                        f = (wAx == bs.u ? 0 : 2) + (sw > 0.0f ? 0 : 1);
                        share = 0.6f;
                    }
                    E = tableLight(v, I, J, K, h.q, f, q) * share;
                } else {
                    E = structureLight(v, I, J, K, q, n);
                }
                // a clear coat reflects as glass does (Fresnel), gold leaf as gold; the rest is the thread's colour
                const float cn = fmaxf(-dot(d, n), 0.0f);
                const float Fv = tabLerp(cOpt.varnish, cn) * fin.varn;
                L += thr * fin.alb * ((1.0f - Fv) * INV_PIf) * E;
                const float3 spec = make_float3(Fv, Fv, Fv) + tab3(cOpt.gold, cn) * fin.metal;
                if (pass == 0 && v.reflections && maxc(spec) > 0.0f) {
                    pending = true;
                    po = ctr + q + n * 1e-4f;
                    pd = d + n * (2.0f * cn);
                    pthr = thr * spec;
                    pfp = fpAt;
                }
                ended = true;
                break;
            }
            if (h.kind == 2) {
                const float3 p = ol + d * h.t;
                const int fa = h.q;
                const float fs = float(h.face);
                if (firstT && pass == 0) *firstT = h.t;
                const int a1 = (fa + 1) % 3, a2 = (fa + 2) % 3;
                const float H = g.h + g.glass;
                const float3 nOut = axisVec(fa, fs);
                const float ci = -dot(d, nOut);
                if (fmaxf(fabsf(comp(p, a1)), fabsf(comp(p, a2))) > H - g.frame) {
                    // the frame, dark outside (its chrome reflects the dark round it)
                    L += thr * v.frameAlbedo * INV_PIf * ((v.prof & 2) ? make_float3(0.05f, 0.03f, 0.015f) : structureLight(v, I, J, K, p, nOut));
                    ended = true;
                    break;
                }
                if (pass == 1) {   // the reflection: the room's mean glow (4% of it; its detail is lost there)
                    const RoomRef rr = roomAt(v, I, J, K);
                    L += thr * glowLookup(v, rr.var, symApply(rr.sym, d));
                    ended = true;
                    break;
                }
                if (pass == 0 && v.reflections) {
                    pending = true;
                    po = ctr + p + nOut * 1e-4f;
                    pd = d + nOut * (2.0f * ci);
                    pthr = thr * tab3(cOpt.outR, ci);
                    pfp = fp0 + coneS * h.t;
                }
                if (req) {
                    req->has = 1;
                    req->cx = cx;
                    req->cy = cy;
                    req->cz = cz;
                    req->fa = fa;
                    req->fs = fs;
                    req->p = p;
                    req->d = d;
                    req->w = thr * tab3(cOpt.T, ci);
                    req->fp = fp0 + coneS * h.t;
                    req->coneS = coneS;
                } else {
                    int b = 0;
                    L += glassRoom(v, I, J, K, p, d, fa, fs, thr * tab3(cOpt.T, ci), fp0 + coneS * h.t, coneS, b);
                    bouncesOut += b;
                }
                ended = true;
                break;
            }
            t = tNext;
            if (tmx <= tmy && tmx <= tmz) { cx += sx; tmx += tdx; }
            else if (tmy <= tmz) { cy += sy; tmy += tdy; }
            else { cz += sz; tmz += tdz; }
            if (maxc(thr) < 2e-4f) { ended = true; break; }
        }
        if (!ended) L += thr * hazeSrc;   // the mist on to the end of the shaft
    }
    return L;
}

__device__ float3 rayDir(const View& v, float px, float py) {
    const float aspect = float(v.width) / float(v.height);
    const float sx = (px / v.width * 2.0f - 1.0f) * v.tanHalf * aspect;
    const float sy = (1.0f - py / v.height * 2.0f) * v.tanHalf;
    return normalize(v.camFwd + v.camRight * sx + v.camUp * sy);
}

// the standard multisample positions (Direct3D 11), in sixteenths of a pixel; six rooks (Ayna)
__constant__ signed char kPat4[4][2] = {{-2, -6}, {6, -2}, {-6, 2}, {2, 6}};
__constant__ signed char kPat8[8][2] = {{1, -3}, {-1, 3}, {5, 1}, {-3, -5}, {-5, 5}, {-7, -1}, {3, 7}, {7, -7}};
__constant__ signed char kPat6[6][2] = {{-7, 1}, {-4, -7}, {-1, 4}, {1, -4}, {4, 7}, {7, -1}};
__constant__ signed char kPat16[16][2] = {{1, 1}, {-1, -3}, {-3, 2}, {4, -1}, {-5, -2}, {2, 5}, {5, 3}, {3, -5},
                                          {-2, 6}, {0, -7}, {-4, -6}, {-6, 4}, {-8, 0}, {7, -4}, {6, 7}, {-7, -8}};

__global__ void kFrame(View v, float4* hdr) {
    loadShared(v);
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= v.width || y >= v.height) return;
    const int n = v.samples >= 16 ? 16 : v.samples >= 8 ? 8 : v.samples >= 6 ? 6 : v.samples >= 4 ? 4 : 1;
    float3 sum = make_float3(0, 0, 0);
    float csum = 0.0f, bsum = 0.0f;
    RoomReq rq[16];
    float off[16];
    const bool share = v.interiorReps > 0;
    for (int s = 0; s < n; ++s) {
        float ox = 0.0f, oy = 0.0f;
        if (n == 4) { ox = kPat4[s][0]; oy = kPat4[s][1]; }
        else if (n == 6) { ox = kPat6[s][0]; oy = kPat6[s][1]; }
        else if (n == 8) { ox = kPat8[s][0]; oy = kPat8[s][1]; }
        else if (n == 16) { ox = kPat16[s][0]; oy = kPat16[s][1]; }
        const float3 d = rayDir(v, x + 0.5f + ox / 16.0f + v.subX, y + 0.5f + oy / 16.0f + v.subY);
        int cells, bounces;
        rq[s].has = 0;
        off[s] = ox * ox + oy * oy;
        sum += tracePath(v, v.camPos, d, cells, bounces, nullptr, share ? &rq[s] : nullptr);
        csum += float(cells);
        bsum += float(bounces);
    }
    if (share) {
        // the samples that entered the same room face: their interior traced by the few nearest the pixel's centre, the
        // footprint widened to the share of the pixel each stands for, the light given back to all by their weights
        unsigned done = 0;
        for (int s = 0; s < n; ++s) {
            if (!rq[s].has || ((done >> s) & 1u)) continue;
            unsigned grp = 0;
            int k = 0;
            float3 wsum = make_float3(0, 0, 0);
            for (int m = s; m < n; ++m) {
                if (!rq[m].has || rq[m].cx != rq[s].cx || rq[m].cy != rq[s].cy || rq[m].cz != rq[s].cz || rq[m].fa != rq[s].fa || rq[m].fs != rq[s].fs) continue;
                grp |= 1u << m;
                ++k;
                wsum += rq[m].w;
            }
            done |= grp;
            // a room with spheres or free mirrors has hard edges inside (silhouettes): more of its own rays
            const RoomRef rr0 = roomAt(v, v.camI + rq[s].cx, v.camJ + rq[s].cy, v.camK + rq[s].cz);
            const bool edges = sVariants[rr0.var].nS + sVariants[rr0.var].nP > 0;
            const int reps = min(edges ? v.interiorRepsEdges : v.interiorReps, k);
            const float sc = powf(float(k) / float(reps), 0.5f * v.interiorWiden);
            float3 acc = make_float3(0, 0, 0), wr = make_float3(0, 0, 0);
            unsigned used = 0;
            for (int r = 0; r < reps; ++r) {
                int best = -1;
                for (int m = 0; m < n; ++m)
                    if (((grp >> m) & 1u) && !((used >> m) & 1u) && (best < 0 || off[m] < off[best])) best = m;
                used |= 1u << best;
                const RoomReq& q = rq[best];
                int b = 0;
                acc += glassRoom(v, v.camI + q.cx, v.camJ + q.cy, v.camK + q.cz, q.p, q.d, q.fa, q.fs, q.w, q.fp * sc, q.coneS * sc, b);
                wr += q.w;
                bsum += float(b) * float(k) / float(reps);
            }
            sum += make_float3(acc.x * (wsum.x / fmaxf(wr.x, 1e-20f)), acc.y * (wsum.y / fmaxf(wr.y, 1e-20f)), acc.z * (wsum.z / fmaxf(wr.z, 1e-20f)));
        }
    }
    sum = sum * (1.0f / float(n));
    if (v.debug == 1) {
        const float b = fminf(bsum / float(n) / 120.0f, 1.0f);
        sum = make_float3(b, b * b, 1.0f - b) * (1.0f / v.exposure) * 0.5f;
    } else if (v.debug == 2) {
        const float b = fminf(csum / float(n) / 60.0f, 1.0f);
        sum = make_float3(b, b * b, 1.0f - b) * (1.0f / v.exposure) * 0.5f;
    }
    if (v.stats) {
        atomicAdd(&v.stats[0], (unsigned long long)csum);
        atomicAdd(&v.stats[1], (unsigned long long)bsum);
    }
    if (!(isfinite(sum.x) && isfinite(sum.y) && isfinite(sum.z))) {
        sum = make_float3(0, 0, 0);
        if (v.badCount) atomicAdd(v.badCount, 1u);
    }
    hdr[y * v.width + x] = make_float4(sum.x, sum.y, sum.z, 1.0f);
}

__device__ float4 tapHalf(const float4* img, int w, int h, float x, float y) {
    x = fminf(fmaxf(x - 0.5f, 0.0f), w - 1.001f);
    y = fminf(fmaxf(y - 0.5f, 0.0f), h - 1.001f);
    const int x0 = int(x), y0 = int(y);
    const float tx = x - x0, ty = y - y0;
    const int x1 = min(x0 + 1, w - 1), y1 = min(y0 + 1, h - 1);
    return (img[y0 * w + x0] * (1.0f - tx) + img[y0 * w + x1] * tx) * (1.0f - ty) + (img[y1 * w + x0] * (1.0f - tx) + img[y1 * w + x1] * tx) * ty;
}

// Ayna's resolve: the reconstruction (a pixel's light leaks a little into its neighbours), the glare, the vignette,
// the exposure, AgX, the same triangular dither every frame
__global__ void kResolve(View v, const float4* hdr, const float4* bloomHalf, uchar4* out) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= v.width || y >= v.height) return;
    const int i = y * v.width + x;
    float3 c;
    if (v.recon > 0.0f) {
        const float wsd = v.recon, wc = 1.0f - 2.0f * v.recon;
        float3 acc = make_float3(0, 0, 0);
        for (int dy = -1; dy <= 1; ++dy) {
            const int yy = min(max(y + dy, 0), v.height - 1);
            const float wy = dy == 0 ? wc : wsd;
            for (int dx = -1; dx <= 1; ++dx) {
                const int xx = min(max(x + dx, 0), v.width - 1);
                const float wx = dx == 0 ? wc : wsd;
                const float4 q = hdr[yy * v.width + xx];
                acc += make_float3(q.x, q.y, q.z) * (wx * wy);
            }
        }
        c = acc;
    } else {
        const float4 a = hdr[i];
        c = make_float3(a.x, a.y, a.z);
    }
    if (bloomHalf) {
        const int hw = max(1, v.width / 2), hh = max(1, v.height / 2);
        const float4 b = tapHalf(bloomHalf, hw, hh, (x + 0.5f) * 0.5f, (y + 0.5f) * 0.5f);
        c += make_float3(b.x, b.y, b.z) * v.bloom;
    }
    const float nx = (x + 0.5f) / v.width * 2.0f - 1.0f, ny = (y + 0.5f) / v.height * 2.0f - 1.0f;
    c = c * ((1.0f - v.vignette * (nx * nx * 0.6f + ny * ny * 0.4f)) * v.exposure);
    c = fmaxf3(c, make_float3(0, 0, 0));
    c = agx(c);
    const float nd = float(pcg(unsigned(i) * 3u + 1u) >> 8) * (1.0f / 16777216.0f) + float(pcg(unsigned(i) * 3u + 2u) >> 8) * (1.0f / 16777216.0f) - 1.0f;
    const float r = fminf(fmaxf(srgbOetf(c.x) * 255.0f + 0.5f + nd, 0.0f), 255.0f);
    const float gg = fminf(fmaxf(srgbOetf(c.y) * 255.0f + 0.5f + nd, 0.0f), 255.0f);
    const float b = fminf(fmaxf(srgbOetf(c.z) * 255.0f + 0.5f + nd, 0.0f), 255.0f);
    out[i] = make_uchar4((unsigned char)r, (unsigned char)gg, (unsigned char)b, 255);
}

// a variant's mean glow, looking into it along each of the table's directions: parallel rays on a grid across the
// room's outline, each with a footprint the size of its grid square (so every bulb's image is counted by its share,
// not hit or missed), through the glass (not the frame) and into the room; the mean over the glass
__global__ void kGlow(View v, float4* out, int perSide) {
    loadShared(v);
    const int var = blockIdx.y, dirIdx = blockIdx.x;
    const int N = kGlowN;
    const float3 w = octDecode(make_float2((float(dirIdx % N) + 0.5f) / N, (float(dirIdx / N) + 0.5f) / N));
    const Lattice& g = v.g;
    const float H = g.h + g.glass;
    float3 e1 = fabsf(w.y) < 0.9f ? normalize(cross(w, make_float3(0, 1, 0))) : normalize(cross(w, make_float3(1, 0, 0)));
    const float3 e2 = cross(e1, w);
    const float R = 1.7321f * H;
    const float step = 2.0f * R / float(perSide);
    __shared__ float3 acc[256];
    __shared__ float cnt[256];
    float3 a = make_float3(0, 0, 0);
    float c = 0.0f;
    for (int k = threadIdx.x; k < perSide * perSide; k += blockDim.x) {
        const float gx = -R + (float(k % perSide) + 0.5f) * step, gy = -R + (float(k / perSide) + 0.5f) * step;
        const float3 o = e1 * gx + e2 * gy - w * (4.0f * H);
        // the box, entering
        float tn = -1e30f, tf = 1e30f;
        int ax = 0;
        for (int q = 0; q < 3; ++q) {
            const float oq = comp(o, q), dq = comp(w, q);
            if (fabsf(dq) < 1e-12f) {
                if (fabsf(oq) > H) tn = 1e30f;
                continue;
            }
            float ta = (-H - oq) / dq, tb = (H - oq) / dq;
            if (ta > tb) { const float x = ta; ta = tb; tb = x; }
            if (ta > tn) { tn = ta; ax = q; }
            tf = fminf(tf, tb);
        }
        if (!(tn < tf)) continue;
        const float3 p = o + w * tn;
        const int a1 = (ax + 1) % 3, a2 = (ax + 2) % 3;
        if (fmaxf(fabsf(comp(p, a1)), fabsf(comp(p, a2))) > H - g.frame) continue;
        const float fs = comp(w, ax) > 0.0f ? -1.0f : 1.0f;
        const float3 nOut = axisVec(ax, fs);
        const float ci = -dot(w, nOut);
        const float eta = 1.0f / cOpt.glassN;
        const float ct = sqrtf(fmaxf(1.0f - eta * eta * (1.0f - ci * ci), 1e-6f));
        const float3 dt = w * eta + nOut * (eta * ci - ct);
        float3 pin = p + dt * (g.glass / ct);
        setComp(pin, ax, fs * g.h);
        int b = 0;
        a += roomInterior(v, var, 0.0f, pin, w, tab3(cOpt.T, ci), step * 0.5642f, 0.0f, b, v.maxBounces, false);   // a disc of the square's area
        c += 1.0f;
    }
    acc[threadIdx.x] = a;
    cnt[threadIdx.x] = c;
    __syncthreads();
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (threadIdx.x < s) {
            acc[threadIdx.x] += acc[threadIdx.x + s];
            cnt[threadIdx.x] += cnt[threadIdx.x + s];
        }
        __syncthreads();
    }
    if (threadIdx.x == 0) {
        const float3 m = cnt[0] > 0.0f ? acc[0] * (1.0f / cnt[0]) : make_float3(0, 0, 0);
        out[size_t(var) * N * N + dirIdx] = make_float4(m.x, m.y, m.z, cnt[0]);
    }
}

// the tail: the mean radiance a ray collects inside a variant going on along w from anywhere in it: rays from a grid of
// points through the room (not inside the spheres), each with a footprint the size of its share of the grid, full depth
__global__ void kTail(View v, float4* out, int perSide) {
    loadShared(v);
    const int var = blockIdx.y, dirIdx = blockIdx.x;
    const int N = kGlowN;
    const float3 w = octDecode(make_float2((float(dirIdx % N) + 0.5f) / N, (float(dirIdx / N) + 0.5f) / N));
    const float h = v.g.h;
    const float step = 2.0f * h / float(perSide);
    __shared__ float3 acc[256];
    __shared__ float cnt[256];
    float3 a = make_float3(0, 0, 0);
    float c = 0.0f;
    const RoomVariant& rv = sVariants[var];
    for (int k = threadIdx.x; k < perSide * perSide * perSide; k += blockDim.x) {
        const float3 p = make_float3(-h + (float(k % perSide) + 0.5f) * step, -h + (float((k / perSide) % perSide) + 0.5f) * step,
                                     -h + (float(k / (perSide * perSide)) + 0.5f) * step);
        bool inside = false;
        for (int s2 = 0; s2 < rv.nS; ++s2) inside |= length(p - rv.S[s2].c) < rv.S[s2].r;
        if (inside) continue;
        int b = 0;
        a += roomInterior(v, var, 0.0f, p, w, make_float3(1, 1, 1), step * 0.62f, 0.0f, b, v.maxBounces, false);   // a disc of the cube's cross-section
        c += 1.0f;
    }
    acc[threadIdx.x] = a;
    cnt[threadIdx.x] = c;
    __syncthreads();
    for (int s2 = blockDim.x / 2; s2 > 0; s2 >>= 1) {
        if (threadIdx.x < s2) {
            acc[threadIdx.x] += acc[threadIdx.x + s2];
            cnt[threadIdx.x] += cnt[threadIdx.x + s2];
        }
        __syncthreads();
    }
    if (threadIdx.x == 0) {
        const float3 m = cnt[0] > 0.0f ? acc[0] * (1.0f / cnt[0]) : make_float3(0, 0, 0);
        out[size_t(var) * N * N + dirIdx] = make_float4(m.x, m.y, m.z, cnt[0]);
    }
}

__global__ void kProbe(View v, float px, float py) {
    loadShared(v);
    const float3 d = rayDir(v, px, py);
    int cells, bounces;
    float t = -1.0f;
    const float3 L = tracePath(v, v.camPos, d, cells, bounces, &t);
    printf("probe (%.1f %.1f): L %.6f %.6f %.6f  hucre %d  sekme %d  ilk carpma %.3f m\n", px, py, L.x, L.y, L.z, cells, bounces, t);
}

}   // namespace

void setOptics(const Optics& o) { cudaMemcpyToSymbol(cOpt, &o, sizeof(Optics)); }

void buildGlowTable(const View& v, float4* glowOut, int perSide) {
    kGlow<<<dim3(kGlowN * kGlowN, v.nVariants), 256>>>(v, glowOut, perSide);
}

void buildLightTable(const View& v, unsigned char* idx, float* G, float2* far, int samplesSide, int farRays) {
    const int total = 48 * v.ltNa * v.ltNw;
    float2* tmp = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&tmp), sizeof(float2) * total);
    kLightTable<<<(total + 63) / 64, 64>>>(v, idx, G, tmp, samplesSide, farRays);
    kBlurFar<<<(total + 255) / 256, 256>>>(v, tmp, far, 4, 2);
    cudaDeviceSynchronize();
    cudaFree(tmp);
}

void buildTailTable(const View& v, float4* tailOut, int perSide) {
    kTail<<<dim3(kGlowN * kGlowN, v.nVariants), 256>>>(v, tailOut, perSide);
}

void renderFrame(const View& v, float4* hdr, cudaStream_t s) {
    const dim3 b(16, 8);
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

size_t viewSizeCu() { return sizeof(View); }

}   // namespace kuyu
