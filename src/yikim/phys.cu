// Scene 3 "Yikim": the GPU solver (see phys.h). One frame: broad phase (sort and sweep along x) with boxes grown by
// the frame's motion, then substeps of: integrate, interfaces colour by colour, contacts (Jacobi), velocities,
// velocity pass (dynamic friction, restitution, interface damping).
#include "yikim/phys.h"

#include <cub/device/device_radix_sort.cuh>
#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define PCHECK(x)                                                                                        \
    do {                                                                                                 \
        cudaError_t e_ = (x);                                                                            \
        if (e_ != cudaSuccess) {                                                                         \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                                \
        }                                                                                                \
    } while (0)

enum : unsigned { F_CRACKED = 1u, F_CRUSHED = 2u, F_BROKEN = 4u };

struct DFiber {
    float3 rA, rB;        // anchors in the body frames
    float3 plastic;       // permanent offset in the interface frame (normal, t1, t2)
    float3 force;         // N, last substep's force in the interface frame (normal: >0 tension), for display
    float kn, ks, kSteel; // N/m: axial (concrete + steel), shear, axial steel only (after cracking)
    float crackF;         // N: tension that cracks the concrete
    float capC;           // N: compression capacity
    float capSteel;       // N: tension capacity of the bars
    float capCoh;         // N: shear capacity of uncracked concrete
    float capDowel;       // N: shear capacity of the bars
    float len;            // m
    unsigned flags;
};

struct DInterface {
    int a, b, first, count, mat, alive;
    float4 qI;            // interface frame (x = normal a->b, y, z tangents) relative to body a
    float3 cA, cB;        // centre of the face in the body frames (damping)
};

struct DPatch {           // the contact points of one pair of bodies, for friction
    int a, b;
    float3 ra, rb;        // world levers of the patch centre
    float3 n;             // pushes a away from b
    float J;              // normal impulse of the position pass (J / h^2 is the force)
    float spent;          // friction impulse already spent at position level
    float radius;         // spread of the points about the centre (torsional friction)
};

struct DevWorld {
    int n = 0;
    double3 *x = nullptr, *xPrev = nullptr;
    float4 *q = nullptr, *qPrev = nullptr;
    float3 *v = nullptr, *w = nullptr, *vOld = nullptr, *wOld = nullptr;
    float* invMass = nullptr;
    float3* invI = nullptr;
    unsigned* kin = nullptr;
    int* shape = nullptr;
    float3 *localC = nullptr, *localH = nullptr;    // local box (centre, half extents)
    float3 *lo = nullptr, *hi = nullptr;            // world boxes, grown
    // accumulators (Jacobi)
    float3 *dx = nullptr, *dth = nullptr;
    int* cnt = nullptr;
    // shapes
    int *sampleStart = nullptr, *sampleCount = nullptr, *planeStart = nullptr, *planeCount = nullptr;
    float3* samples = nullptr;
    float4* planes = nullptr;
    // interfaces
    int nIfc = 0, nFib = 0;
    DInterface* ifc = nullptr;
    DFiber* fib = nullptr;
    int* colourList = nullptr;
    int *adjStart = nullptr, *adjList = nullptr;
    PhysMaterial* mats = nullptr;
    // broad phase
    float *keys = nullptr, *keysSorted = nullptr;
    int *ids = nullptr, *idsSorted = nullptr;
    void* sortTemp = nullptr;
    size_t sortTempBytes = 0;
    int2* pairs = nullptr;
    int* nPairs = nullptr;
    int maxPairs = 0;
    DPatch* patches = nullptr;
    int* nPatches = nullptr;
    int maxPatches = 0;
    unsigned long long* anchorKey = nullptr;   // friction anchors, by pair
    int* anchorFrame = nullptr;
    int* anchorSlide = nullptr;
    float3 *anchorA = nullptr, *anchorB = nullptr;
    int anchorCap = 0;
    cudaEvent_t e0 = nullptr, e1 = nullptr;
    // the substep loop as a CUDA graph (hundreds of small kernels a frame: launch overhead dominated)
    cudaStream_t stream = nullptr;
    double3* groundPath = nullptr;   // the ground's position at the end of each substep of this frame
    int* frameDev = nullptr;
    int maxSubsteps = 0;
    cudaGraph_t graph = nullptr;
    cudaGraphExec_t graphExec = nullptr;
};

// ------------------------------------------------------------------------------------------------------------
// device helpers
// ------------------------------------------------------------------------------------------------------------
__device__ __forceinline__ void atomicAdd3(float3* p, float3 v) {
    atomicAdd(&p->x, v.x);
    atomicAdd(&p->y, v.y);
    atomicAdd(&p->z, v.z);
}
// generalised inverse mass of a body for a push along unit n at lever r (world)
__device__ __forceinline__ float genInvMass(float im, float3 iI, float4 q, float3 r, float3 n) {
    const float3 rn = cross(r, n);
    return im + dot(rn, invInertiaMul(q, iI, rn));
}

// ------------------------------------------------------------------------------------------------------------
// integrate
// ------------------------------------------------------------------------------------------------------------
__global__ void kIntegrate(DevWorld W, float h, float g, int substep, float maxSpeed) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= W.n) return;
    const double3 groundPos = W.groundPath[substep];
    W.vOld[i] = W.v[i];
    W.wOld[i] = W.w[i];
    W.xPrev[i] = W.x[i];
    W.qPrev[i] = W.q[i];
    if (W.kin[i]) {
        if (i == 0) {
            W.x[0] = groundPos;
            W.v[0] = diff3(groundPos, W.xPrev[0]) * (1.0f / h);
        }
        return;
    }
    float3 v = W.v[i];
    v.y -= g * h;
    const float sp = length(v);
    if (sp > maxSpeed) v = v * (maxSpeed / sp);   // guard, never reached by sane motion
    W.v[i] = v;
    W.x[i] = W.x[i] + d3(v * h);
    // rotation: gyroscopic term in the body frame, then q += h/2 [w,0] q
    const float4 q = W.q[i];
    const float3 iI = W.invI[i];
    float3 wl = qrotInv(q, W.w[i]);
    const float3 Iw = make_float3(iI.x > 0 ? wl.x / iI.x : 0.0f, iI.y > 0 ? wl.y / iI.y : 0.0f, iI.z > 0 ? wl.z / iI.z : 0.0f);
    wl = wl - cross(wl, Iw) * iI * h;
    float3 w = qrot(q, wl);
    const float ws = length(w);
    if (ws > 200.0f) w = w * (200.0f / ws);
    W.w[i] = w;
    W.q[i] = qaddRot(q, w * h);
}

// ------------------------------------------------------------------------------------------------------------
// interfaces (one thread per interface of a colour: no two share a moving body)
// ------------------------------------------------------------------------------------------------------------
// One interface per thread (a colour's interfaces share no moving body). The fibres are solved in order
// (Gauss-Seidel), each XPBD: dl = -(C + at lambda) / (w + at), lambda accumulated over the substep's passes. Capacities clamp it; what the
// clamp leaves unresolved becomes permanent (plastic) offset, so a yielding fibre keeps carrying its capacity.
// The two bodies' rotations, world inverse inertias and the interface frame are taken once per interface; within
// the interface the corrections are tracked as small displacements and rotation vectors (first order) and applied
// at the end (the poses move by micrometres per substep). Doubles stay out of the fibre loop.
// Measured: a warm start (last substep's forces applied first) integrates the residual of the unconverged
// Gauss-Seidel pass and blew up a cantilever at every share from 0.5 to 1; removed.
struct Mat3 {
    float3 r0, r1, r2;   // rows
    __device__ float3 mul(float3 v) const { return make_float3(dot(r0, v), dot(r1, v), dot(r2, v)); }
};
__device__ __forceinline__ Mat3 rotMat(float4 q) {
    Mat3 m;
    qmat(q, m.r0, m.r1, m.r2);
    return m;
}
__device__ __forceinline__ Mat3 invInertiaWorld(const Mat3& R, float3 iI) {   // R diag(iI) R^T
    Mat3 m;
    m.r0 = make_float3(R.r0.x * iI.x * R.r0.x + R.r0.y * iI.y * R.r0.y + R.r0.z * iI.z * R.r0.z,
                       R.r0.x * iI.x * R.r1.x + R.r0.y * iI.y * R.r1.y + R.r0.z * iI.z * R.r1.z,
                       R.r0.x * iI.x * R.r2.x + R.r0.y * iI.y * R.r2.y + R.r0.z * iI.z * R.r2.z);
    m.r1 = make_float3(m.r0.y, R.r1.x * iI.x * R.r1.x + R.r1.y * iI.y * R.r1.y + R.r1.z * iI.z * R.r1.z,
                       R.r1.x * iI.x * R.r2.x + R.r1.y * iI.y * R.r2.y + R.r1.z * iI.z * R.r2.z);
    m.r2 = make_float3(m.r0.z, m.r1.z, R.r2.x * iI.x * R.r2.x + R.r2.y * iI.y * R.r2.y + R.r2.z * iI.z * R.r2.z);
    return m;
}

struct IfcBody {         // one side of an interface during its solve
    float im;
    Mat3 R, Iinv;
    float3 dx, dth;      // accumulated correction (world)
    __device__ float w(float3 r, float3 n) const { const float3 rn = cross(r, n); return im + dot(rn, Iinv.mul(rn)); }
    __device__ void push(float3 r, float3 p) {   // impulse p at lever r
        if (im <= 0.0f) return;
        dx = dx + p * im;
        dth = dth + Iinv.mul(cross(r, p));
    }
    __device__ float3 moved(float3 r) const { return dx + cross(dth, r); }
};

__global__ void kSolveInterfaces(DevWorld W, const int* list, int count, float h, int iter) {
    const int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= count) return;
    const int k = list[t];
    const DInterface I = W.ifc[k];
    if (I.alive <= 0) return;
    const int a = I.a, b = I.b;
    const float4 qa = W.q[a], qb = W.q[b];
    IfcBody A, B;
    A.im = W.invMass[a]; B.im = W.invMass[b];
    A.R = rotMat(qa); B.R = rotMat(qb);
    A.Iinv = invInertiaWorld(A.R, W.invI[a]); B.Iinv = invInertiaWorld(B.R, W.invI[b]);
    A.dx = A.dth = B.dx = B.dth = make_float3(0, 0, 0);
    const float3 xab = diff3(W.x[b], W.x[a]);   // b's centre from a's
    const float4 qI = qmul(qa, I.qI);
    const float3 n = qrot(qI, make_float3(1, 0, 0)), t1 = qrot(qI, make_float3(0, 1, 0)), t2 = qrot(qI, make_float3(0, 0, 1));
    const PhysMaterial M = W.mats[I.mat];
    const float hh = h * h;
    int alive = 0;
    for (int f = I.first; f < I.first + I.count; ++f) {
        DFiber F = W.fib[f];
        if (F.flags & F_BROKEN) continue;
        // the multipliers the earlier passes of this substep applied (XPBD accumulates them; capacities bound the
        // total: measured, bounding each pass alone let two passes carry twice the yield moment)
        const float lam0 = iter == 0 ? 0.0f : -F.force.x * hh;
        const float l10 = iter == 0 ? 0.0f : -F.force.y * hh, l20 = iter == 0 ? 0.0f : -F.force.z * hh;
        const float3 ra = A.R.mul(F.rA), rb = B.R.mul(F.rB);
        const float wn = A.w(ra, n) + B.w(rb, n);
        // ---- normal (axial) part
        float3 sep = xab + rb + B.moved(rb) - ra - A.moved(ra);
        const float C = dot(sep, n) - F.plastic.x;
        float fn = 0.0f;   // total normal force this substep, >0 tension
        if (wn > 0.0f) {
            float at = 1.0f / (F.kn * hh);
            float dl = -(C + at * lam0) / (wn + at);
            float lamT = lam0 + dl;
            fn = -lamT / hh;
            if (fn > 0.0f && !(F.flags & F_CRACKED) && fn > F.crackF) F.flags |= F_CRACKED;
            if (fn > 0.0f && (F.flags & F_CRACKED)) {
                if (F.kSteel <= 0.0f) {
                    // an open crack without bars: no tension, and an opening is a gap, not a permanent offset
                    lamT = 0.0f;
                    fn = 0.0f;
                    if (C > M.openBreak) F.flags |= F_BROKEN;
                } else {
                    at = 1.0f / (F.kSteel * hh);
                    lamT = lam0 - (C + at * lam0) / (wn + at);
                    fn = -lamT / hh;
                    if (fn > F.capSteel) {   // the bars yield
                        fn = F.capSteel;
                        lamT = -fn * hh;
                        F.plastic.x += C + wn * (lamT - lam0) + at * lamT;
                    }
                }
            } else if (fn < 0.0f) {
                const float cap = (F.flags & F_CRUSHED) ? F.capC * M.residual : F.capC;
                if (-fn > cap) {   // the concrete crushes
                    fn = -cap;
                    lamT = cap * hh;
                    F.plastic.x += C + wn * (lamT - lam0) + at * lamT;
                    if (F.plastic.x < -M.crushStart * F.len) F.flags |= F_CRUSHED;
                }
            }
            dl = lamT - lam0;
            const float3 p = n * dl;   // on b; -p on a
            B.push(rb, p);
            A.push(ra, p * -1.0f);
        }
        if (F.plastic.x > M.steelBreak || F.plastic.x < -M.crushBreak * F.len) F.flags |= F_BROKEN;
        F.force = make_float3(fn, 0.0f, 0.0f);
        // ---- shear part (two tangents), after the normal correction
        if (!(F.flags & F_BROKEN)) {
            sep = xab + rb + B.moved(rb) - ra - A.moved(ra);
            const float c1 = dot(sep, t1) - F.plastic.y, c2 = dot(sep, t2) - F.plastic.z;
            const float at = 1.0f / (F.ks * hh);
            const float g1 = c1 + at * l10, g2 = c2 + at * l20;
            const float gl = sqrtf(g1 * g1 + g2 * g2);
            if (gl > 1e-14f) {
                const float3 dir = (t1 * g1 + t2 * g2) * (1.0f / gl);
                const float ws = A.w(ra, dir) + B.w(rb, dir);
                if (ws > 0.0f) {
                    float n1 = l10 - g1 / (ws + at), n2 = l20 - g2 / (ws + at);   // total multipliers
                    const float S = sqrtf(n1 * n1 + n2 * n2) / hh;
                    const float cap = ((F.flags & F_CRACKED) ? 0.0f : F.capCoh) + M.mu * fmaxf(0.0f, -fn) + F.capDowel;
                    if (S > cap) {   // slips: cohesion is gone, friction and dowels hold what they can
                        const float sc = cap / S;
                        n1 *= sc;
                        n2 *= sc;
                        F.plastic.y += c1 + ws * (n1 - l10) + at * n1;
                        F.plastic.z += c2 + ws * (n2 - l20) + at * n2;
                        F.flags |= F_CRACKED;
                    }
                    const float3 p = t1 * (n1 - l10) + t2 * (n2 - l20);
                    B.push(rb, p);
                    A.push(ra, p * -1.0f);
                    F.force.y = -n1 / hh;
                    F.force.z = -n2 / hh;
                }
            }
            if (F.plastic.y * F.plastic.y + F.plastic.z * F.plastic.z > M.slipBreak * M.slipBreak) F.flags |= F_BROKEN;
        }
        if (F.flags & F_BROKEN) F.force = make_float3(0, 0, 0);
        W.fib[f].plastic = F.plastic;
        W.fib[f].force = F.force;
        W.fib[f].flags = F.flags;
        if (!(F.flags & F_BROKEN)) ++alive;
    }
    if (A.im > 0.0f) { W.x[a] = W.x[a] + d3(A.dx); W.q[a] = qaddRot(qa, A.dth); }
    if (B.im > 0.0f) { W.x[b] = W.x[b] + d3(B.dx); W.q[b] = qaddRot(qb, B.dth); }
    if (alive != I.alive) W.ifc[k].alive = alive;
}

// ------------------------------------------------------------------------------------------------------------
// contacts
// ------------------------------------------------------------------------------------------------------------
// is there a living interface between a and b?
__device__ __forceinline__ bool bonded(const DevWorld& W, int a, int b) {
    const int s = W.adjStart[a], e = W.adjStart[a + 1];
    for (int k = s; k < e; ++k) {
        const DInterface& I = W.ifc[W.adjList[k]];
        if ((I.a == b || I.b == b) && I.alive > 0) return true;
    }
    return false;
}

// Contacts are solved PAIR BY PAIR: one thread owns a pair of bodies (or a body and the ground) and keeps private
// copies of both poses. Each iteration: every contact point computes its normal correction from the same poses and
// the pair applies their average (symmetric: a box landing flat stays flat), then static friction acts ONCE for the
// whole patch at its centre against the normal impulse applied so far. Iterating lets friction and the normal
// contacts settle together. The pair's net motion is then added to each body and averaged over the body's pairs.
// Measured on the way (25 degree slope, mu 0.7, 1 m block): per-point friction (Mueller et al. 2020, sec. 3.5, fine
// for a few points) fails with twenty, since the first point must stop the whole body with 1/20 of the load; a single
// pass of patch friction loses ~60% of its correction to rotation that the normal contacts undo: both crept at
// 3-5 mm/s. Points walked in order (Gauss-Seidel) instead of averaged spun a flat-landing box (1.7 rad/s).
struct PatchAcc {
    float lam = 0.0f;                    // sum of the points' normal multipliers (last iteration)
    float3 r = make_float3(0, 0, 0);     // sum lambda * lever from body a
    float3 n = make_float3(0, 0, 0);     // sum lambda * normal pushing a away from b
    float rr = 0.0f;                     // sum lambda * |lever|^2 (patch radius)
    int points = 0;
    float J = 0.0f;                      // normal impulse applied to the pair, all iterations
};

struct PairState {       // private copies of the two bodies
    double3 xa, xb;
    float4 qa, qb;
    float ima, imb;
    float3 iIa, iIb;
    double3 xpa, xpb;    // poses at the start of the substep (static friction)
    float4 qpa, qpb;
    // this iteration's sum of point corrections
    float3 dxa, dta, dxb, dtb;
    float lamT;          // friction impulse spent at position level in this substep (all iterations)
    int slot;            // anchor slot: -2 not looked up yet, -1 none (or fresh)
};

// body P's point (lever rp from P, world) is inside Q by depth along Q's outward normal m: P's correction is a push
// along m. pIsA: P is the pair's body a. Accumulates into the iteration's sums (applied averaged).
__device__ void pointCorrection(PairState& S, bool pIsA, float3 rp, float3 m, float depth, float h, const PhysParams& prm, PatchAcc& acc) {
    const float C = fminf(depth - prm.slop, prm.maxDepenetration * h);
    if (C <= 0.0f) return;
    const double3 xp = pIsA ? S.xa : S.xb, xq = pIsA ? S.xb : S.xa;
    const float4 qp = pIsA ? S.qa : S.qb, qq = pIsA ? S.qb : S.qa;
    const float imp = pIsA ? S.ima : S.imb, imq = pIsA ? S.imb : S.ima;
    const float3 iIp = pIsA ? S.iIa : S.iIb, iIq = pIsA ? S.iIb : S.iIa;
    const float3 rq = diff3(xp, xq) + rp + m * depth;   // the matching point on Q, from Q's centre
    const float wsum = genInvMass(imp, iIp, qp, rp, m) + genInvMass(imq, iIq, qq, rq, m);
    if (wsum <= 0.0f) return;
    const float lam = C / wsum;
    const float3 Pp = m * lam;   // impulse on P (on Q: -Pp)
    const float3 dxp = Pp * imp, dtp = invInertiaMul(qp, iIp, cross(rp, Pp));
    const float3 dxq = Pp * -imq, dtq = invInertiaMul(qq, iIq, cross(rq, Pp * -1.0f));
    if (pIsA) { S.dxa += dxp; S.dta += dtp; S.dxb += dxq; S.dtb += dtq; }
    else      { S.dxb += dxp; S.dtb += dtp; S.dxa += dxq; S.dta += dtq; }
    const float3 ra = pIsA ? rp : rq;
    const float3 na = pIsA ? m : m * -1.0f;
    acc.lam += lam;
    acc.r = acc.r + ra * lam;
    acc.n = acc.n + na * lam;
    acc.rr += lam * dot(ra, ra);
    acc.points++;
}

// samples of P against the planes of Q (P = a when pIsA)
__device__ void pointsVsHull(const DevWorld& W, PairState& S, int p, int q, bool pIsA, float h, const PhysParams& prm, PatchAcc& acc, int lane) {
    const int s0 = W.sampleStart[W.shape[p]], sn = W.sampleCount[W.shape[p]];
    const int p0 = W.planeStart[W.shape[q]], pn = W.planeCount[W.shape[q]];
    const float3 hq = W.localH[q], cq = W.localC[q];
    const double3 xp = pIsA ? S.xa : S.xb, xq = pIsA ? S.xb : S.xa;
    const float4 qp = pIsA ? S.qa : S.qb, qq = pIsA ? S.qb : S.qa;
    const float3 rel = diff3(xp, xq);
    for (int s = lane; s < sn; s += 32) {
        const float3 rp = qrot(qp, W.samples[s0 + s]);
        const float3 pl = qrotInv(qq, rel + rp);    // in Q's frame
        if (fabsf(pl.x - cq.x) > hq.x || fabsf(pl.y - cq.y) > hq.y || fabsf(pl.z - cq.z) > hq.z) continue;
        float best = -1e30f;
        int bi = -1;
        for (int k = 0; k < pn; ++k) {
            const float4 P = W.planes[p0 + k];
            const float sd = P.x * pl.x + P.y * pl.y + P.z * pl.z - P.w;
            if (sd > 0.0f) { bi = -1; break; }
            if (sd > best) { best = sd; bi = k; }
        }
        if (bi < 0) continue;
        const float4 P = W.planes[p0 + bi];
        pointCorrection(S, pIsA, rp, qrot(qq, make_float3(P.x, P.y, P.z)), -best, h, prm, acc);
    }
}

// apply the iteration's averaged point corrections; returns false when there were none
__device__ bool applyPoints(PairState& S, PatchAcc& acc) {
    if (acc.points == 0) return false;
    const float s = 1.0f / float(acc.points);
    if (S.ima > 0.0f) { S.xa = S.xa + d3(S.dxa * s); S.qa = qaddRot(S.qa, S.dta * s); }
    if (S.imb > 0.0f) { S.xb = S.xb + d3(S.dxb * s); S.qb = qaddRot(S.qb, S.dtb * s); }
    acc.J += acc.lam * s;
    S.dxa = S.dta = S.dxb = S.dtb = make_float3(0, 0, 0);
    return true;
}

// Friction anchors (as PhysX does): when two bodies first touch, the patch centre is pinned in both bodies' frames.
// Static friction then pulls the two pins back together, however long ago they drifted apart, as long as the grip
// (mu * normal impulse) holds; when it does not, the pins are reset where the patch now is and the bodies slide
// (dynamic friction, velocity pass). Correcting only the slide within the current substep instead let every small
// push from a neighbour pair (Jacobi between pairs) stay forever: a pyramid of 15 boxes spread by 0.65 m in 30 s.
// The anchors live in an open-addressing table keyed by the pair, stamped with the frame; stale entries are reused.
#define ANCHOR_EMPTY 0xffffffffffffffffull

__device__ int anchorSlot(DevWorld& W, int a, int b, int frame, bool& fresh) {
    const unsigned long long key = (unsigned long long)(unsigned)a << 32 | (unsigned)b;
    unsigned long long hsh = key * 0x9E3779B97F4A7C15ull;
    const unsigned mask = unsigned(W.anchorCap - 1);
    unsigned s = unsigned(hsh >> 40) & mask;
    for (int probe = 0; probe < 64; ++probe, s = (s + 1) & mask) {
        const unsigned long long k = W.anchorKey[s];
        if (k == key) {
            fresh = W.anchorFrame[s] < frame - 1;   // not touched in the last frame: the contact is new
            return int(s);
        }
        if (k == ANCHOR_EMPTY || W.anchorFrame[s] < frame - 2) {
            if (atomicCAS(&W.anchorKey[s], k, key) == k) {
                fresh = true;
                return int(s);
            }
        }
    }
    return -1;
}

// Friction of a patch, at position level against its anchor. A sticking patch may spend mu_s times the normal impulse
// to pull the pins together; a sliding one mu_d (kinetic friction at position level, as in position-based dynamics).
// The verdict comes at the last iteration, when the normal impulse has built up: if even then the pins could not be
// closed the patch slides, and its pins move to where the patch is now. The velocity pass only spends what is left of
// mu_d times the normal impulse (dynamic friction counted twice held a block on a 40 degree slope).
__device__ void patchFriction(DevWorld& W, int a, int b, int frame, PairState& S, const PatchAcc& acc, const PhysParams& prm, bool last) {
    if (acc.lam <= 0.0f || acc.J <= 0.0f) return;
    float3 n = acc.n;
    const float nl = length(n);
    if (nl < 1e-12f) return;
    n = n * (1.0f / nl);
    const float3 rc = acc.r * (1.0f / acc.lam);   // the patch centre, lever from a
    if (S.slot == -2) {   // first call this substep: find the pair's anchor
        bool fresh = false;
        S.slot = anchorSlot(W, a, b, frame, fresh);
        if (S.slot >= 0) {
            W.anchorFrame[S.slot] = frame;
            if (fresh) {   // pin the centre in both frames; no friction until the next substep
                W.anchorA[S.slot] = qrotInv(S.qa, rc);
                W.anchorB[S.slot] = qrotInv(S.qb, diff3(S.xa, S.xb) + rc);
                W.anchorSlide[S.slot] = 0;
                S.slot = -1;
            }
        }
    }
    if (S.slot < 0) return;
    const int slot = S.slot;
    const bool sliding = W.anchorSlide[slot] != 0;
    const float3 ra = qrot(S.qa, W.anchorA[slot]);
    const float3 rb = qrot(S.qb, W.anchorB[slot]);
    float3 d = (ra + diff3(S.xa, S.xb)) - rb;     // pin on a minus pin on b (world)
    d = d - n * dot(d, n);
    const float dl = length(d);
    bool clamped = false;
    if (dl > 1e-10f) {
        const float3 t = d * (1.0f / dl);
        const float wt = genInvMass(S.ima, S.iIa, S.qa, ra, t) + genInvMass(S.imb, S.iIb, S.qb, rb, t);
        if (wt > 0.0f) {
            float lt = dl / wt;
            const float budget = (sliding ? prm.muDynamic : prm.muStatic) * acc.J - S.lamT;
            if (lt > budget) {
                lt = fmaxf(budget, 0.0f);
                clamped = true;
            }
            S.lamT += lt;
            const float3 Pa = t * -lt;   // impulse on a
            if (S.ima > 0.0f) {
                S.xa = S.xa + d3(Pa * S.ima);
                S.qa = qaddRot(S.qa, invInertiaMul(S.qa, S.iIa, cross(ra, Pa)));
            }
            if (S.imb > 0.0f) {
                S.xb = S.xb + d3(Pa * -S.imb);
                S.qb = qaddRot(S.qb, invInertiaMul(S.qb, S.iIb, cross(rb, Pa * -1.0f)));
            }
        }
    }
    if (last) {
        W.anchorSlide[slot] = clamped ? 1 : 0;
        if (clamped) {   // sliding: pin again where the patch is now
            W.anchorA[slot] = qrotInv(S.qa, rc);
            W.anchorB[slot] = qrotInv(S.qb, diff3(S.xa, S.xb) + rc);
        }
    }
}

__device__ PairState loadPair(const DevWorld& W, int a, int b) {
    PairState S;
    S.xa = W.x[a]; S.xb = W.x[b];
    S.qa = W.q[a]; S.qb = W.q[b];
    S.ima = W.invMass[a]; S.imb = W.invMass[b];
    S.iIa = W.invI[a]; S.iIb = W.invI[b];
    S.xpa = W.xPrev[a]; S.xpb = W.xPrev[b];
    S.qpa = W.qPrev[a]; S.qpb = W.qPrev[b];
    S.dxa = S.dta = S.dxb = S.dtb = make_float3(0, 0, 0);
    S.lamT = 0.0f;
    S.slot = -2;
    return S;
}

// the pair's net motion into the bodies' accumulators (averaged over each body's pairs), and the patch record
__device__ void finishPair(DevWorld& W, int a, int b, const PairState& S, const PatchAcc& last, float J) {
    if (J <= 0.0f) return;
    if (S.ima > 0.0f) {
        const float4 dq = qmul(S.qa, qconj(W.q[a]));
        const float3 dth = make_float3(dq.x, dq.y, dq.z) * (dq.w < 0.0f ? -2.0f : 2.0f);
        atomicAdd3(&W.dx[a], diff3(S.xa, W.x[a]));
        atomicAdd3(&W.dth[a], dth);
        atomicAdd(&W.cnt[a], 1);
    }
    if (S.imb > 0.0f) {
        const float4 dq = qmul(S.qb, qconj(W.q[b]));
        const float3 dth = make_float3(dq.x, dq.y, dq.z) * (dq.w < 0.0f ? -2.0f : 2.0f);
        atomicAdd3(&W.dx[b], diff3(S.xb, W.x[b]));
        atomicAdd3(&W.dth[b], dth);
        atomicAdd(&W.cnt[b], 1);
    }
    if (last.lam <= 0.0f) return;
    float3 n = last.n;
    const float nl = length(n);
    if (nl < 1e-12f) return;
    n = n * (1.0f / nl);
    const float3 ra = last.r * (1.0f / last.lam);
    const int slot = atomicAdd(W.nPatches, 1);
    if (slot < W.maxPatches) {
        DPatch c;
        c.a = a; c.b = b; c.ra = ra; c.rb = diff3(W.x[a], W.x[b]) + ra; c.n = n;
        c.J = J;
        c.spent = S.lamT;
        c.radius = sqrtf(fmaxf(0.0f, last.rr / last.lam - dot(ra, ra)));
        W.patches[slot] = c;
    }
}

// ---- one warp per pair: the lanes share the contact points, sums are reduced to lane 0 and broadcast, so every
// lane holds the same private poses; anchor bookkeeping and the bodies' accumulators are lane 0's.
// (One thread per pair walked ~640 sample tests in series and waited on memory: 3.4 of 7.3 ms a frame, measured.)
__device__ __forceinline__ float warpSum(float v) {
    for (int o = 16; o > 0; o >>= 1) v += __shfl_down_sync(0xffffffffu, v, o);
    return __shfl_sync(0xffffffffu, v, 0);
}
__device__ __forceinline__ float3 warpSum3(float3 v) { return make_float3(warpSum(v.x), warpSum(v.y), warpSum(v.z)); }
__device__ void warpReduce(PairState& S, PatchAcc& acc) {
    S.dxa = warpSum3(S.dxa); S.dta = warpSum3(S.dta); S.dxb = warpSum3(S.dxb); S.dtb = warpSum3(S.dtb);
    acc.lam = warpSum(acc.lam); acc.r = warpSum3(acc.r); acc.n = warpSum3(acc.n); acc.rr = warpSum(acc.rr);
    acc.points = int(warpSum(float(acc.points)) + 0.5f);
}
__device__ __forceinline__ double3 shflD3(double3 v) {
    return make_double3(__shfl_sync(0xffffffffu, v.x, 0), __shfl_sync(0xffffffffu, v.y, 0), __shfl_sync(0xffffffffu, v.z, 0));
}
__device__ __forceinline__ float4 shflF4(float4 v) {
    return make_float4(__shfl_sync(0xffffffffu, v.x, 0), __shfl_sync(0xffffffffu, v.y, 0), __shfl_sync(0xffffffffu, v.z, 0),
                       __shfl_sync(0xffffffffu, v.w, 0));
}
__device__ void frictionOnLane0(DevWorld& W, int a, int b, int frame, PairState& S, const PatchAcc& acc, const PhysParams& prm, bool last, int lane) {
    if (lane == 0) patchFriction(W, a, b, frame, S, acc, prm, last);
    S.xa = shflD3(S.xa); S.xb = shflD3(S.xb);
    S.qa = shflF4(S.qa); S.qb = shflF4(S.qb);
    S.lamT = __shfl_sync(0xffffffffu, S.lamT, 0);
    S.slot = __shfl_sync(0xffffffffu, S.slot, 0);
}

__device__ void contactPairWarp(DevWorld& W, int i, int lane, float h, const PhysParams& prm, int frame) {
    const int2 pr = W.pairs[i];
    const int a = pr.x, b = pr.y;
    if (bonded(W, a, b)) return;
    PairState S = loadPair(W, a, b);
    PatchAcc last;
    float J = 0.0f;
    for (int it = 0; it < prm.contactIters; ++it) {
        PatchAcc acc;
        pointsVsHull(W, S, a, b, true, h, prm, acc, lane);
        pointsVsHull(W, S, b, a, false, h, prm, acc, lane);
        warpReduce(S, acc);
        acc.J = J;
        if (!applyPoints(S, acc)) { if (it == 0) return; break; }
        J = acc.J;
        frictionOnLane0(W, a, b, frame, S, acc, prm, it == prm.contactIters - 1, lane);
        last = acc;
    }
    if (lane == 0) finishPair(W, a, b, S, last, J);
}

__global__ void kContacts(DevWorld W, float h, PhysParams prm) {
    const int frame = *W.frameDev;
    const int np = min(*W.nPairs, W.maxPairs);
    const int lane = threadIdx.x & 31;
    const int warps = (gridDim.x * blockDim.x) >> 5;
    for (int i = (blockIdx.x * blockDim.x + threadIdx.x) >> 5; i < np; i += warps) contactPairWarp(W, i, lane, h, prm, frame);
}

__global__ void kGround(DevWorld W, float h, PhysParams prm) {
    const int frame = *W.frameDev;
    const int lane = threadIdx.x & 31;
    const int warps = (gridDim.x * blockDim.x) >> 5;
    const float gy = float(W.x[0].y);
    for (int i = (blockIdx.x * blockDim.x + threadIdx.x) >> 5; i < W.n; i += warps) {
        if (i == 0 || W.kin[i]) continue;
        const float3 c = W.localC[i], hh = W.localH[i];
        {   // the lowest point of the local box decides whether to look at the samples at all
            float3 r0, r1, r2;
            qmat(W.q[i], r0, r1, r2);
            const float yc = float(W.x[i].y) + dot(r1, c);
            const float ext = fabsf(r1.x) * hh.x + fabsf(r1.y) * hh.y + fabsf(r1.z) * hh.z;
            if (yc - ext > gy + 0.01f) continue;
        }
        if (bonded(W, i, 0)) continue;   // still held by its foundation
        PairState S = loadPair(W, i, 0);
        const int s0 = W.sampleStart[W.shape[i]], sn = W.sampleCount[W.shape[i]];
        PatchAcc last;
        float J = 0.0f;
        bool any = false;
        for (int it = 0; it < prm.contactIters; ++it) {
            PatchAcc acc;
            for (int s = lane; s < sn; s += 32) {
                const float3 r = qrot(S.qa, W.samples[s0 + s]);
                const float y = float(S.xa.y - double(gy)) + r.y;
                if (y < 0.0f) pointCorrection(S, true, r, make_float3(0, 1, 0), -y, h, prm, acc);
            }
            warpReduce(S, acc);
            acc.J = J;
            if (!applyPoints(S, acc)) break;
            any = true;
            J = acc.J;
            frictionOnLane0(W, i, 0, frame, S, acc, prm, it == prm.contactIters - 1, lane);
            last = acc;
        }
        if (any && lane == 0) finishPair(W, i, 0, S, last, J);
    }
}

__global__ void kApply(DevWorld W, float relax) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= W.n) return;
    const int c = W.cnt[i];
    if (c > 0 && !W.kin[i]) {
        const float s = (c > 1 ? relax : 1.0f) / float(c);
        W.x[i] = W.x[i] + d3(W.dx[i] * s);
        W.q[i] = qaddRot(W.q[i], W.dth[i] * s);
    }
    W.dx[i] = make_float3(0, 0, 0);
    W.dth[i] = make_float3(0, 0, 0);
    W.cnt[i] = 0;
}

__global__ void kVelocities(DevWorld W, float h) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= W.n || W.kin[i]) return;
    W.v[i] = diff3(W.x[i], W.xPrev[i]) * (1.0f / h);
    float4 dq = qmul(W.q[i], qconj(W.qPrev[i]));
    float3 w = make_float3(dq.x, dq.y, dq.z) * (2.0f / h);
    if (dq.w < 0.0f) w = -w;
    W.w[i] = w;
}

// velocity pass per patch (Mueller et al. 2020, eq. 30-34, at the patch centre): dynamic friction bounded by
// mu * normal force, restitution against the normal velocity before the substep, and torsional friction (a patch
// resists spinning about its normal: bounded by mu * normal force * patch radius). Jacobi over a body's patches.
__global__ void kPatchVelocities(DevWorld W, float h, PhysParams prm, float g) {
    const int np = min(*W.nPatches, W.maxPatches);
    for (int i = blockIdx.x * blockDim.x + threadIdx.x; i < np; i += gridDim.x * blockDim.x) {
        const DPatch c = W.patches[i];
        const int a = c.a, b = c.b;
        const float ima = W.invMass[a], imb = W.invMass[b];
        const float3 iIa = W.invI[a], iIb = W.invI[b];
        const float4 qa = W.q[a], qb = W.q[b];
        const float3 vr = (W.v[a] + cross(W.w[a], c.ra)) - (W.v[b] + cross(W.w[b], c.rb));
        const float vn = dot(vr, c.n);
        const float3 vt = vr - c.n * vn;
        const float vtl = length(vt);
        const float fnh = c.J / h;   // normal force * h: the impulse a substep's friction may spend
        float3 P = make_float3(0, 0, 0);   // impulse on a
        if (vtl > 1e-6f) {
            const float3 t = vt * (1.0f / vtl);
            const float wt = genInvMass(ima, iIa, qa, c.ra, t) + genInvMass(imb, iIb, qb, c.rb, t);
            const float left = fmaxf(0.0f, prm.muDynamic * c.J - c.spent) / h;
            if (wt > 0.0f) P = P - t * fminf(left, vtl / wt);
        }
        const float3 vao = W.vOld[a] + cross(W.wOld[a], c.ra), vbo = W.vOld[b] + cross(W.wOld[b], c.rb);
        const float vnOld = dot(vao - vbo, c.n);
        const float e = fabsf(vn) <= 2.0f * g * h ? 0.0f : prm.restitution;
        const float target = fmaxf(-e * vnOld, 0.0f);
        if (vn < target) {
            const float wn = genInvMass(ima, iIa, qa, c.ra, c.n) + genInvMass(imb, iIb, qb, c.rb, c.n);
            if (wn > 0.0f) P = P + c.n * ((target - vn) / wn);
        }
        // torsion
        float3 L = make_float3(0, 0, 0);   // angular impulse on a
        const float wn = dot(W.w[a] - W.w[b], c.n);
        if (fabsf(wn) > 1e-6f && c.radius > 0.0f) {
            const float wtor = dot(c.n, invInertiaMul(qa, iIa, c.n)) + dot(c.n, invInertiaMul(qb, iIb, c.n));
            if (wtor > 0.0f) L = c.n * (-copysignf(fminf(prm.muDynamic * fnh * c.radius, fabsf(wn) / wtor), wn));
        }
        if (ima > 0.0f) {
            atomicAdd3(&W.dx[a], P * ima);
            atomicAdd3(&W.dth[a], invInertiaMul(qa, iIa, cross(c.ra, P) + L));
            atomicAdd(&W.cnt[a], 1);
        }
        if (imb > 0.0f) {
            atomicAdd3(&W.dx[b], P * -imb);
            atomicAdd3(&W.dth[b], invInertiaMul(qb, iIb, cross(c.rb, P * -1.0f) - L));
            atomicAdd(&W.cnt[b], 1);
        }
    }
}

// interface damping: remove a share of the relative velocity across each living interface (Jacobi)
__global__ void kInterfaceDamping(DevWorld W, float h) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= W.nIfc) return;
    const DInterface I = W.ifc[k];
    if (I.alive <= 0) return;
    const float c = W.mats[I.mat].damping;
    if (c <= 0.0f) return;
    const float eta = fminf(1.0f, c * h);
    const int a = I.a, b = I.b;
    const float ima = W.invMass[a], imb = W.invMass[b];
    const float3 iIa = W.invI[a], iIb = W.invI[b];
    const float4 qa = W.q[a], qb = W.q[b];
    const float3 ra = qrot(qa, I.cA), rb = qrot(qb, I.cB);
    const float3 vr = (W.v[b] + cross(W.w[b], rb)) - (W.v[a] + cross(W.w[a], ra));
    const float vl = length(vr);
    float3 Pl = make_float3(0, 0, 0), L = make_float3(0, 0, 0);
    if (vl > 1e-7f) {
        const float3 d = vr * (1.0f / vl);
        const float wsum = genInvMass(ima, iIa, qa, ra, d) + genInvMass(imb, iIb, qb, rb, d);
        if (wsum > 0.0f) Pl = d * (-eta * vl / wsum);   // impulse on b
    }
    const float3 wr = W.w[b] - W.w[a];
    const float wl = length(wr);
    if (wl > 1e-7f) {
        const float3 ax = wr * (1.0f / wl);
        const float wsum = dot(ax, invInertiaMul(qa, iIa, ax)) + dot(ax, invInertiaMul(qb, iIb, ax));
        if (wsum > 0.0f) L = ax * (-eta * wl / wsum);   // angular impulse on b
    }
    if (imb > 0.0f) {
        atomicAdd3(&W.dx[b], Pl * imb);
        atomicAdd3(&W.dth[b], invInertiaMul(qb, iIb, cross(rb, Pl) + L));
        atomicAdd(&W.cnt[b], 1);
    }
    if (ima > 0.0f) {
        atomicAdd3(&W.dx[a], Pl * -ima);
        atomicAdd3(&W.dth[a], invInertiaMul(qa, iIa, cross(ra, Pl * -1.0f) - L));
        atomicAdd(&W.cnt[a], 1);
    }
}

__global__ void kApplyVel(DevWorld W) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= W.n) return;
    const int c = W.cnt[i];
    if (c > 0 && !W.kin[i]) {
        const float s = 1.0f / float(c);
        W.v[i] = W.v[i] + W.dx[i] * s;
        W.w[i] = W.w[i] + W.dth[i] * s;
    }
    W.dx[i] = make_float3(0, 0, 0);
    W.dth[i] = make_float3(0, 0, 0);
    W.cnt[i] = 0;
}

// ------------------------------------------------------------------------------------------------------------
// broad phase
// ------------------------------------------------------------------------------------------------------------
__global__ void kBoxes(DevWorld W, float dt, float margin) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= W.n) return;
    if (i == 0) {   // the ground plane is not in the sweep
        W.lo[0] = make_float3(1e30f, 1e30f, 1e30f);
        W.hi[0] = make_float3(-1e30f, -1e30f, -1e30f);
        W.keys[0] = 3e30f;
        W.ids[0] = 0;
        return;
    }
    float3 r0, r1, r2;
    qmat(W.q[i], r0, r1, r2);
    const float3 c = W.localC[i], hh = W.localH[i];
    const float3 x = f3(W.x[i]);
    const float3 wc = x + make_float3(dot(r0, c), dot(r1, c), dot(r2, c));
    float3 e = make_float3(fabsf(r0.x) * hh.x + fabsf(r0.y) * hh.y + fabsf(r0.z) * hh.z, fabsf(r1.x) * hh.x + fabsf(r1.y) * hh.y + fabsf(r1.z) * hh.z,
                           fabsf(r2.x) * hh.x + fabsf(r2.y) * hh.y + fabsf(r2.z) * hh.z);
    const float grow = margin + length(W.v[i]) * dt + length(W.w[i]) * dt * length(hh);
    e = e + make_float3(grow, grow, grow);
    W.lo[i] = wc - e;
    W.hi[i] = wc + e;
    W.keys[i] = wc.x - e.x;
    W.ids[i] = i;
}

__global__ void kSweep(DevWorld W) {
    const int s = blockIdx.x * blockDim.x + threadIdx.x;
    if (s >= W.n - 1) return;   // the ground sorts last
    const int i = W.idsSorted[s];
    const float3 lo = W.lo[i], hi = W.hi[i];
    const bool ki = W.kin[i] != 0;
    for (int u = s + 1; u < W.n; ++u) {
        if (W.keysSorted[u] > hi.x) break;
        const int j = W.idsSorted[u];
        if (j == 0) break;
        if (ki && W.kin[j]) continue;
        const float3 l2 = W.lo[j], h2 = W.hi[j];
        if (l2.y > hi.y || h2.y < lo.y || l2.z > hi.z || h2.z < lo.z) continue;
        const int slot = atomicAdd(W.nPairs, 1);
        if (slot < W.maxPairs) W.pairs[slot] = make_int2(i, j);
    }
}

// transforms for the renderer: OptixInstance.transform is the first 12 floats (3x4 rows)
__global__ void kTransforms(DevWorld W, char* base, size_t stride, int first, int count) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= count) return;
    const int i = first + k;
    float3 r0, r1, r2;
    qmat(W.q[i], r0, r1, r2);
    const float3 x = f3(W.x[i]);
    float* m = reinterpret_cast<float*>(base + stride * size_t(k));
    m[0] = r0.x; m[1] = r0.y; m[2] = r0.z; m[3] = x.x;
    m[4] = r1.x; m[5] = r1.y; m[6] = r1.z; m[7] = x.y;
    m[8] = r2.x; m[9] = r2.y; m[10] = r2.z; m[11] = x.z;
}

// ------------------------------------------------------------------------------------------------------------
// host
// ------------------------------------------------------------------------------------------------------------
static inline int blocks(int n, int t = 128) { return (n + t - 1) / t; }

template <class T> static T* dmalloc(size_t n) {
    void* p = nullptr;
    PCHECK(cudaMalloc(&p, std::max<size_t>(1, n) * sizeof(T)));
    PCHECK(cudaMemset(p, 0, std::max<size_t>(1, n) * sizeof(T)));
    return static_cast<T*>(p);
}
template <class T> static T* dupload(const std::vector<T>& v) {
    T* p = dmalloc<T>(v.size());
    if (!v.empty()) PCHECK(cudaMemcpy(p, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    return p;
}

PhysWorld::PhysWorld() { clear(); }
PhysWorld::~PhysWorld() { freeDevice(); }

void PhysWorld::freeDevice() {
    if (!d_) return;
    void* ptrs[] = {d_->x, d_->xPrev, d_->q, d_->qPrev, d_->v, d_->w, d_->vOld, d_->wOld, d_->invMass, d_->invI, d_->kin, d_->shape,
                    d_->localC, d_->localH, d_->lo, d_->hi, d_->dx, d_->dth, d_->cnt, d_->sampleStart, d_->sampleCount, d_->planeStart,
                    d_->planeCount, d_->samples, d_->planes, d_->ifc, d_->fib, d_->colourList, d_->adjStart, d_->adjList, d_->mats,
                    d_->keys, d_->keysSorted, d_->ids, d_->idsSorted, d_->sortTemp, d_->pairs, d_->nPairs, d_->patches, d_->nPatches, d_->anchorKey, d_->anchorFrame, d_->anchorSlide, d_->anchorA, d_->anchorB};
    for (void* p : ptrs)
        if (p) cudaFree(p);
    if (d_->graphExec) cudaGraphExecDestroy(d_->graphExec);
    if (d_->graph) cudaGraphDestroy(d_->graph);
    if (d_->groundPath) cudaFree(d_->groundPath);
    if (d_->frameDev) cudaFree(d_->frameDev);
    if (d_->stream) cudaStreamDestroy(d_->stream);
    if (d_->e0) cudaEventDestroy(d_->e0);
    if (d_->e1) cudaEventDestroy(d_->e1);
    delete d_;
    d_ = nullptr;
    uploaded_ = false;
}

void PhysWorld::clear() {
    freeDevice();
    info.clear(); shapes.clear(); pos.clear(); rot.clear(); vel.clear(); angVel.clear();
    invMass.clear(); invInertia.clear(); kinematic.clear(); materials.clear(); interfaceDescs.clear();
    colours_.clear();
    time = 0;
    groundTarget_ = groundPrev_ = d3(0, 0, 0);
    // body 0: the ground
    BodyInfo g;
    g.tag = -1;
    g.localLo = g.localHi = make_float3(0, 0, 0);
    info.push_back(g);
    shapes.push_back(Poly());
    pos.push_back(d3(0, 0, 0));
    rot.push_back(make_float4(0, 0, 0, 1));
    vel.push_back(make_float3(0, 0, 0));
    angVel.push_back(make_float3(0, 0, 0));
    invMass.push_back(0.0f);
    invInertia.push_back(make_float3(0, 0, 0));
    kinematic.push_back(1u);
}

int PhysWorld::addMaterial(const PhysMaterial& m) {
    materials.push_back(m);
    return int(materials.size()) - 1;
}

int PhysWorld::addBody(const Poly& ws, float density, bool kin, int tag, unsigned color) {
    const MassProps mp = polyMass(ws);
    double vals[3], vecs[3][3];
    eigenSym3(mp.inertia, vals, vecs);
    const float4 q = quatFromMatrixColumns(vecs);
    const double3 c = mp.centroid;
    // shape in the body frame: p_local = R^T (p - c)
    Poly local = ws;
    const double3 ex = d3(vecs[0][0], vecs[1][0], vecs[2][0]), ey = d3(vecs[0][1], vecs[1][1], vecs[2][1]), ez = d3(vecs[0][2], vecs[1][2], vecs[2][2]);
    auto toLocal = [&](double3 p) { const double3 d = p - c; return d3(ddot(d, ex), ddot(d, ey), ddot(d, ez)); };
    auto dirLocal = [&](double3 d) { return d3(ddot(d, ex), ddot(d, ey), ddot(d, ez)); };
    for (PolyFace& f : local.faces) {
        for (double3& p : f.v) p = toLocal(p);
        f.n = dirLocal(f.n);
        f.d = ddot(f.n, f.v[0]);
    }
    BodyInfo bi;
    bi.tag = tag;
    bi.color = color;
    bi.mass = float(mp.volume * density);
    double3 lo, hi;
    local.bounds(lo, hi);
    bi.localLo = f3(lo);
    bi.localHi = f3(hi);
    info.push_back(bi);
    shapes.push_back(local);
    pos.push_back(c);
    rot.push_back(q);
    vel.push_back(make_float3(0, 0, 0));
    angVel.push_back(make_float3(0, 0, 0));
    if (kin) {
        invMass.push_back(0.0f);
        invInertia.push_back(make_float3(0, 0, 0));
    } else {
        invMass.push_back(float(1.0 / (mp.volume * density)));
        invInertia.push_back(make_float3(float(1.0 / (vals[0] * density)), float(1.0 / (vals[1] * density)), float(1.0 / (vals[2] * density))));
    }
    kinematic.push_back(kin ? 1u : 0u);
    uploaded_ = false;
    return int(info.size()) - 1;
}

int PhysWorld::addInterface(const InterfaceDesc& d) {
    interfaceDescs.push_back(d);
    uploaded_ = false;
    return int(interfaceDescs.size()) - 1;
}

void PhysWorld::upload() {
    freeDevice();
    d_ = new DevWorld();
    DevWorld& W = *d_;
    const int n = int(info.size());
    W.n = n;
    W.x = dupload(pos);
    W.xPrev = dupload(pos);
    W.q = dupload(rot);
    W.qPrev = dupload(rot);
    W.v = dupload(vel);
    W.w = dupload(angVel);
    W.vOld = dupload(vel);
    W.wOld = dupload(angVel);
    W.invMass = dupload(invMass);
    W.invI = dupload(invInertia);
    W.kin = dupload(kinematic);
    // shapes: one per body; contact samples = corners + points along long edges + a grid on big faces
    std::vector<int> shapeIdx(n), ss(n), sc(n), ps(n), pc(n);
    std::vector<float3> samples, lc(n), lh(n);
    std::vector<float4> planes;
    for (int i = 0; i < n; ++i) {
        shapeIdx[i] = i;
        const Poly& P = shapes[i];
        ss[i] = int(samples.size());
        ps[i] = int(planes.size());
        // per face: corners and edge points pulled a few millimetres into the face, so that a corner standing
        // exactly on another box's edge (a stack of equal boxes) never reads that box's side as the nearest face
        const double step = 0.25;   // m between samples along edges and over faces
        for (const PolyFace& f : P.faces) {
            const size_t m = f.v.size();
            if (m < 3) continue;
            double3 fc = d3(0, 0, 0);
            for (const double3& p : f.v) fc = fc + p;
            fc = fc * (1.0 / m);
            double minR = 1e30;
            for (const double3& p : f.v) minR = std::min(minR, dlength(p - fc));
            const double inset = std::min(0.005, 0.2 * minR);
            for (size_t k = 0; k < m; ++k) {
                const double3 a = f.v[k], b = f.v[(k + 1) % m];
                const double3 e = b - a;
                const double L = dlength(e);
                if (L < 1e-9) continue;
                const double3 in = dnormalize(dcross(f.n, e));   // into the face (counter-clockwise: interior on the left)
                // the corner: pulled in along both of its edges' inward directions
                const double3 ep = a - f.v[(k + m - 1) % m];
                const double3 inPrev = dlength(ep) > 1e-9 ? dnormalize(dcross(f.n, ep)) : in;
                samples.push_back(f3(a + (in + inPrev) * inset));
                const int segs = int(std::floor(L / step));
                for (int s = 1; s < segs; ++s) samples.push_back(f3(a + e * (double(s) / segs) + in * inset));
            }
            {   // interior grid on faces wider than two steps (a big slab resting on a column)
                const double3 t = std::fabs(f.n.x) < 0.9 ? dnormalize(dcross(f.n, d3(1, 0, 0))) : dnormalize(dcross(f.n, d3(0, 1, 0)));
                const double3 bt = dcross(f.n, t);
                double u0 = 1e30, u1 = -1e30, v0 = 1e30, v1 = -1e30;
                for (const double3& p : f.v) {
                    u0 = std::min(u0, ddot(p, t)); u1 = std::max(u1, ddot(p, t));
                    v0 = std::min(v0, ddot(p, bt)); v1 = std::max(v1, ddot(p, bt));
                }
                const int nu = int((u1 - u0) / (2 * step)), nv = int((v1 - v0) / (2 * step));
                for (int iu = 1; iu < nu * 2; iu += 2)
                    for (int iv = 1; iv < nv * 2; iv += 2) {
                        const double uu = u0 + (u1 - u0) * iu / (2.0 * nu), vv = v0 + (v1 - v0) * iv / (2.0 * nv);
                        const double3 p = f.n * f.d + t * uu + bt * vv;
                        bool inside = true;
                        for (size_t k = 0; k < m && inside; ++k) {
                            const double3 e = f.v[(k + 1) % m] - f.v[k];
                            if (ddot(dcross(e, p - f.v[k]), f.n) < -1e-9) inside = false;
                        }
                        if (inside) samples.push_back(f3(p));
                    }
            }
            planes.push_back(make_float4(float(f.n.x), float(f.n.y), float(f.n.z), float(f.d)));
        }
        sc[i] = int(samples.size()) - ss[i];
        pc[i] = int(planes.size()) - ps[i];
        lc[i] = (info[i].localLo + info[i].localHi) * 0.5f;
        lh[i] = (info[i].localHi - info[i].localLo) * 0.5f;
    }
    W.shape = dupload(shapeIdx);
    W.sampleStart = dupload(ss);
    W.sampleCount = dupload(sc);
    W.planeStart = dupload(ps);
    W.planeCount = dupload(pc);
    W.samples = dupload(samples);
    W.planes = dupload(planes);
    W.localC = dupload(lc);
    W.localH = dupload(lh);
    W.lo = dmalloc<float3>(n);
    W.hi = dmalloc<float3>(n);
    W.dx = dmalloc<float3>(n);
    W.dth = dmalloc<float3>(n);
    W.cnt = dmalloc<int>(n);
    // interfaces and fibres
    std::vector<DInterface> ifc;
    std::vector<DFiber> fib;
    std::vector<std::vector<int>> adj(n);
    for (const InterfaceDesc& d : interfaceDescs) {
        const PhysMaterial& M = materials[d.mat];
        DInterface I{};
        I.a = d.a;
        I.b = d.b;
        I.mat = d.mat;
        I.first = int(fib.size());
        // interface frame: x = normal (a's frame), y, z tangents
        const float3 nA = qrotInv(rot[d.a], d.n);
        float3 t1, t2;
        makeBasis(nA, t1, t2);
        const double m[3][3] = {{nA.x, t1.x, t2.x}, {nA.y, t1.y, t2.y}, {nA.z, t1.z, t2.z}};
        I.qI = quatFromMatrixColumns(m);
        float3 cen = make_float3(0, 0, 0);
        float atot = 0;
        for (const FiberDesc& fd : d.fibers) {
            DFiber F{};
            F.rA = qrotInv(rot[d.a], diff3(d3(fd.p), pos[d.a]));
            F.rB = qrotInv(rot[d.b], diff3(d3(fd.p), pos[d.b]));
            F.plastic = make_float3(0, 0, 0);
            F.force = make_float3(0, 0, 0);
            const float L = std::max(d.len, 0.02f);
            const float G = M.E / (2.0f * (1.0f + M.nu));
            F.kn = (M.E * fd.ac + M.Es * fd.as) / L;
            F.kSteel = M.Es * fd.as / L;
            F.ks = (G * fd.ac + 0.4f * M.Es * fd.as) / L;
            const float kc = M.E * fd.ac / L;
            F.crackF = kc > 0 ? M.ft * fd.ac * (F.kn / kc) : 0.0f;
            F.capC = M.fc * fd.ac + M.fy * fd.as;
            F.capSteel = M.fy * fd.as;
            F.capCoh = M.cohesion * fd.ac;
            F.capDowel = M.dowel * M.fy * fd.as;
            F.len = L;
            F.flags = 0;
            fib.push_back(F);
            cen = cen + fd.p * (fd.ac + fd.as);
            atot += fd.ac + fd.as;
        }
        if (atot > 0) cen = cen * (1.0f / atot);
        I.cA = qrotInv(rot[d.a], diff3(d3(cen), pos[d.a]));
        I.cB = qrotInv(rot[d.b], diff3(d3(cen), pos[d.b]));
        I.count = int(fib.size()) - I.first;
        I.alive = I.count;
        adj[d.a].push_back(int(ifc.size()));
        adj[d.b].push_back(int(ifc.size()));
        ifc.push_back(I);
    }
    W.nIfc = int(ifc.size());
    W.nFib = int(fib.size());
    W.ifc = dupload(ifc);
    W.fib = dupload(fib);
    W.mats = dupload(materials);
    std::vector<int> adjStart(n + 1, 0), adjList;
    for (int i = 0; i < n; ++i) {
        adjStart[i] = int(adjList.size());
        for (int k : adj[i]) adjList.push_back(k);
    }
    adjStart[n] = int(adjList.size());
    W.adjStart = dupload(adjStart);
    W.adjList = dupload(adjList);
    // greedy colouring: interfaces sharing a moving body get different colours
    colours_.clear();
    std::vector<int> colourOf(ifc.size(), -1);
    std::vector<std::vector<int>> bodyColours(n);
    for (size_t k = 0; k < ifc.size(); ++k) {
        const int a = ifc[k].a, b = ifc[k].b;
        int c = 0;
        for (;; ++c) {
            bool used = false;
            if (!kinematic[a])
                for (int u : bodyColours[a]) if (u == c) { used = true; break; }
            if (!used && !kinematic[b])
                for (int u : bodyColours[b]) if (u == c) { used = true; break; }
            if (!used) break;
        }
        colourOf[k] = c;
        if (!kinematic[a]) bodyColours[a].push_back(c);
        if (!kinematic[b]) bodyColours[b].push_back(c);
        if (int(colours_.size()) <= c) colours_.resize(c + 1);
        colours_[c].push_back(int(k));
    }
    std::vector<int> flat;
    colourOffset_.clear();
    for (auto& c : colours_) {
        colourOffset_.push_back(int(flat.size()));
        flat.insert(flat.end(), c.begin(), c.end());
    }
    W.colourList = dupload(flat);
    // broad phase buffers
    W.keys = dmalloc<float>(n);
    W.keysSorted = dmalloc<float>(n);
    W.ids = dmalloc<int>(n);
    W.idsSorted = dmalloc<int>(n);
    cub::DeviceRadixSort::SortPairs(nullptr, W.sortTempBytes, W.keys, W.keysSorted, W.ids, W.idsSorted, n);
    PCHECK(cudaMalloc(&W.sortTemp, std::max<size_t>(W.sortTempBytes, 16)));
    W.maxPairs = std::max(1 << 16, n * 64);
    W.pairs = dmalloc<int2>(W.maxPairs);
    W.nPairs = dmalloc<int>(1);
    W.maxPatches = std::max(1 << 16, n * 32);
    W.patches = dmalloc<DPatch>(W.maxPatches);
    W.nPatches = dmalloc<int>(1);
    W.anchorCap = 1;
    while (W.anchorCap < W.maxPatches * 2) W.anchorCap <<= 1;
    W.anchorKey = dmalloc<unsigned long long>(W.anchorCap);
    PCHECK(cudaMemset(W.anchorKey, 0xff, W.anchorCap * sizeof(unsigned long long)));
    W.anchorFrame = dmalloc<int>(W.anchorCap);
    {
        std::vector<int> old(W.anchorCap, -1000);
        PCHECK(cudaMemcpy(W.anchorFrame, old.data(), old.size() * sizeof(int), cudaMemcpyHostToDevice));
    }
    W.anchorSlide = dmalloc<int>(W.anchorCap);
    W.anchorA = dmalloc<float3>(W.anchorCap);
    W.anchorB = dmalloc<float3>(W.anchorCap);
    frame_ = 0;
    PCHECK(cudaStreamCreateWithFlags(&W.stream, cudaStreamNonBlocking));
    W.maxSubsteps = 1024;
    W.groundPath = dmalloc<double3>(W.maxSubsteps);
    W.frameDev = dmalloc<int>(1);
    graphKey_.clear();
    PCHECK(cudaEventCreate(&W.e0));
    PCHECK(cudaEventCreate(&W.e1));
    groundPrev_ = groundTarget_ = pos[0];
    uploaded_ = true;
}

// the colour passes, always in the same order (measured: running odd passes backwards, symmetric Gauss-Seidel,
// left a cantilever softer, 13.3 mm against 9.4 mm at 40 substeps x 2 passes)
void PhysWorld::solveInterfaces(float h, void* stream) {
    DevWorld& W = *d_;
    const int C = int(colours_.size());
    for (int it = 0; it < std::max(1, params.interfaceIters); ++it)
        for (int k = 0; k < C; ++k) {
            const int c = k;
            kSolveInterfaces<<<blocks(int(colours_[c].size()), 64), 64, 0, static_cast<cudaStream_t>(stream)>>>(W, W.colourList + colourOffset_[c],
                                                                                                             int(colours_[c].size()), h, it);
        }
}

void PhysWorld::step(float dt) {
    if (!uploaded_) upload();
    DevWorld& W = *d_;
    const int n = W.n;
    const int S = std::min(std::max(1, params.substeps), W.maxSubsteps);
    const float h = dt / S;
    lastH_ = h;
    cudaStream_t st = W.stream;
    // this frame's inputs: where the ground is at the end of each substep, and the frame number (friction anchors)
    groundSteps_.resize(S);
    for (int s = 0; s < S; ++s) groundSteps_[s] = groundPrev_ + (groundTarget_ - groundPrev_) * (double(s + 1) / S);
    PCHECK(cudaMemcpyAsync(W.groundPath, groundSteps_.data(), S * sizeof(double3), cudaMemcpyHostToDevice, st));
    PCHECK(cudaMemcpyAsync(W.frameDev, &frame_, sizeof(int), cudaMemcpyHostToDevice, st));
    PCHECK(cudaEventRecord(W.e0, st));
    // broad phase, once per frame
    kBoxes<<<blocks(n), 128, 0, st>>>(W, dt, params.margin);
    cub::DeviceRadixSort::SortPairs(W.sortTemp, W.sortTempBytes, W.keys, W.keysSorted, W.ids, W.idsSorted, n, 0, sizeof(float) * 8, st);
    PCHECK(cudaMemsetAsync(W.nPairs, 0, sizeof(int), st));
    kSweep<<<blocks(n), 128, 0, st>>>(W);
    // the substeps: captured once as a graph, replayed every frame (recaptured when a setting or dt changes)
    std::vector<char> key(sizeof(PhysParams) + sizeof(float));
    std::memcpy(key.data(), &params, sizeof(PhysParams));
    std::memcpy(key.data() + sizeof(PhysParams), &h, sizeof(float));
    if (key != graphKey_ || !W.graphExec) {
        if (W.graphExec) { cudaGraphExecDestroy(W.graphExec); W.graphExec = nullptr; }
        if (W.graph) { cudaGraphDestroy(W.graph); W.graph = nullptr; }
        PCHECK(cudaStreamBeginCapture(st, cudaStreamCaptureModeThreadLocal));
        const int contactBlocks = std::max(1, std::min(blocks(W.maxPairs * 32, 128), 2048));
        for (int s = 0; s < S; ++s) {
            kIntegrate<<<blocks(n), 128, 0, st>>>(W, h, params.gravity, s, params.maxSpeed);
            solveInterfaces(h, st);
            PCHECK(cudaMemsetAsync(W.nPatches, 0, sizeof(int), st));
            kContacts<<<contactBlocks, 128, 0, st>>>(W, h, params);
            kGround<<<std::min(blocks(n * 32, 128), 2048), 128, 0, st>>>(W, h, params);
            kApply<<<blocks(n), 128, 0, st>>>(W, params.relax);
            kVelocities<<<blocks(n), 128, 0, st>>>(W, h);
            kPatchVelocities<<<256, 128, 0, st>>>(W, h, params, params.gravity);
            if (W.nIfc) kInterfaceDamping<<<blocks(W.nIfc), 128, 0, st>>>(W, h);
            kApplyVel<<<blocks(n), 128, 0, st>>>(W);
        }
        PCHECK(cudaStreamEndCapture(st, &W.graph));
        PCHECK(cudaGraphInstantiate(&W.graphExec, W.graph, 0));
        graphKey_ = key;
    }
    PCHECK(cudaGraphLaunch(W.graphExec, st));
    PCHECK(cudaEventRecord(W.e1, st));
    PCHECK(cudaEventSynchronize(W.e1));
    PCHECK(cudaEventElapsedTime(&lastMs, W.e0, W.e1));
    PCHECK(cudaGetLastError());
    PCHECK(cudaMemcpy(&lastPairs_, W.nPairs, sizeof(int), cudaMemcpyDeviceToHost));
    lastPairs_ = std::min(lastPairs_, W.maxPairs);
    groundPrev_ = groundTarget_;
    time += dt;
    ++frame_;
}

// one frame without the graph, every kernel group timed with events (profile before optimising)
void PhysWorld::profile(float dt) {
    if (!uploaded_) upload();
    DevWorld& W = *d_;
    const int n = W.n;
    const int S = std::min(std::max(1, params.substeps), W.maxSubsteps);
    const float h = dt / S;
    cudaStream_t st = W.stream;
    groundSteps_.assign(S, groundPrev_);
    PCHECK(cudaMemcpyAsync(W.groundPath, groundSteps_.data(), S * sizeof(double3), cudaMemcpyHostToDevice, st));
    const char* names[] = {"genis faz", "integrate", "arayuz renkleri", "temas ciftleri", "zemin", "uygula+hiz", "yama hizlari",
                           "sonum+uygula"};
    const int G = 8;
    float ms[G] = {};
    cudaEvent_t ev[2];
    cudaEventCreate(&ev[0]);
    cudaEventCreate(&ev[1]);
    auto timed = [&](int g, auto fn) {
        cudaEventRecord(ev[0], st);
        fn();
        cudaEventRecord(ev[1], st);
        cudaEventSynchronize(ev[1]);
        float t = 0;
        cudaEventElapsedTime(&t, ev[0], ev[1]);
        ms[g] += t;
    };
    timed(0, [&] {
        kBoxes<<<blocks(n), 128, 0, st>>>(W, dt, params.margin);
        cub::DeviceRadixSort::SortPairs(W.sortTemp, W.sortTempBytes, W.keys, W.keysSorted, W.ids, W.idsSorted, n, 0, sizeof(float) * 8, st);
        cudaMemsetAsync(W.nPairs, 0, sizeof(int), st);
        kSweep<<<blocks(n), 128, 0, st>>>(W);
    });
    const int contactBlocks = std::max(1, std::min(blocks(W.maxPairs * 32, 128), 2048));
    for (int s = 0; s < S; ++s) {
        timed(1, [&] { kIntegrate<<<blocks(n), 128, 0, st>>>(W, h, params.gravity, s, params.maxSpeed); });
        timed(2, [&] {
            solveInterfaces(h, st);
        });
        timed(3, [&] {
            cudaMemsetAsync(W.nPatches, 0, sizeof(int), st);
            kContacts<<<contactBlocks, 128, 0, st>>>(W, h, params);
        });
        timed(4, [&] { kGround<<<std::min(blocks(n * 32, 128), 2048), 128, 0, st>>>(W, h, params); });
        timed(5, [&] {
            kApply<<<blocks(n), 128, 0, st>>>(W, params.relax);
            kVelocities<<<blocks(n), 128, 0, st>>>(W, h);
        });
        timed(6, [&] { kPatchVelocities<<<256, 128, 0, st>>>(W, h, params, params.gravity); });
        timed(7, [&] {
            if (W.nIfc) kInterfaceDamping<<<blocks(W.nIfc), 128, 0, st>>>(W, h);
            kApplyVel<<<blocks(n), 128, 0, st>>>(W);
        });
    }
    float tot = 0;
    for (int g = 0; g < G; ++g) tot += ms[g];
    std::printf("      profil (%d alt adim, %d govde, %d cift): ", S, n, lastPairs_);
    for (int g = 0; g < G; ++g) std::printf("%s %.2f  ", names[g], ms[g]);
    std::printf("= %.2f ms\n", tot);
    cudaEventDestroy(ev[0]);
    cudaEventDestroy(ev[1]);
}

void PhysWorld::download() {
    if (!uploaded_) return;
    const int n = d_->n;
    PCHECK(cudaMemcpy(pos.data(), d_->x, n * sizeof(double3), cudaMemcpyDeviceToHost));
    PCHECK(cudaMemcpy(rot.data(), d_->q, n * sizeof(float4), cudaMemcpyDeviceToHost));
    PCHECK(cudaMemcpy(vel.data(), d_->v, n * sizeof(float3), cudaMemcpyDeviceToHost));
    PCHECK(cudaMemcpy(angVel.data(), d_->w, n * sizeof(float3), cudaMemcpyDeviceToHost));
}

void PhysWorld::breakBody(int b) {
    if (!uploaded_) return;
    std::vector<DInterface> ifc(d_->nIfc);
    PCHECK(cudaMemcpy(ifc.data(), d_->ifc, ifc.size() * sizeof(DInterface), cudaMemcpyDeviceToHost));
    for (size_t k = 0; k < ifc.size(); ++k)
        if (ifc[k].a == b || ifc[k].b == b) {
            ifc[k].alive = 0;
            std::vector<DFiber> f(ifc[k].count);
            PCHECK(cudaMemcpy(f.data(), d_->fib + ifc[k].first, f.size() * sizeof(DFiber), cudaMemcpyDeviceToHost));
            for (DFiber& x : f) x.flags |= F_BROKEN | F_CRACKED;
            PCHECK(cudaMemcpy(d_->fib + ifc[k].first, f.data(), f.size() * sizeof(DFiber), cudaMemcpyHostToDevice));
        }
    PCHECK(cudaMemcpy(d_->ifc, ifc.data(), ifc.size() * sizeof(DInterface), cudaMemcpyHostToDevice));
}

void PhysWorld::setVelocity(int b, float3 v, float3 w) {
    if (!uploaded_ || b <= 0 || b >= d_->n) return;
    PCHECK(cudaMemcpy(d_->v + b, &v, sizeof(float3), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->w + b, &w, sizeof(float3), cudaMemcpyHostToDevice));
}

std::vector<float> PhysWorld::damage() {
    std::vector<float> dmg(info.size(), 0.0f), cnt(info.size(), 0.0f);
    if (!uploaded_ || !d_->nFib) return dmg;
    std::vector<DFiber> fib(d_->nFib);
    PCHECK(cudaMemcpy(fib.data(), d_->fib, fib.size() * sizeof(DFiber), cudaMemcpyDeviceToHost));
    size_t f = 0;
    for (const InterfaceDesc& d : interfaceDescs)
        for (size_t k = 0; k < d.fibers.size(); ++k, ++f) {
            const float x = (fib[f].flags & F_BROKEN) ? 1.0f : (fib[f].flags & (F_CRACKED | F_CRUSHED)) ? 0.5f : 0.0f;
            dmg[d.a] += x; dmg[d.b] += x;
            cnt[d.a] += 1.0f; cnt[d.b] += 1.0f;
        }
    for (size_t i = 0; i < dmg.size(); ++i) dmg[i] = cnt[i] > 0 ? dmg[i] / cnt[i] : 0.0f;
    return dmg;
}

PhysStats PhysWorld::stats() {
    PhysStats s;
    s.bodies = int(info.size());
    s.interfaces = int(interfaceDescs.size());
    s.colours = int(colours_.size());
    s.ms = lastMs;
    s.pairs = lastPairs_;
    if (!uploaded_) return s;
    std::vector<DInterface> ifc(d_->nIfc);
    std::vector<DFiber> fib(d_->nFib);
    if (!ifc.empty()) PCHECK(cudaMemcpy(ifc.data(), d_->ifc, ifc.size() * sizeof(DInterface), cudaMemcpyDeviceToHost));
    if (!fib.empty()) PCHECK(cudaMemcpy(fib.data(), d_->fib, fib.size() * sizeof(DFiber), cudaMemcpyDeviceToHost));
    s.fibres = int(fib.size());
    for (const DInterface& I : ifc) s.alive += I.alive > 0;
    for (const DFiber& F : fib) {
        if (F.flags & F_BROKEN) ++s.broken;
        if (F.flags & F_CRACKED) ++s.cracked;
    }
    int nc = 0;
    PCHECK(cudaMemcpy(&nc, d_->nPatches, sizeof(int), cudaMemcpyDeviceToHost));
    s.contacts = nc;
    return s;
}

void PhysWorld::fibres(std::vector<unsigned>& flags, std::vector<float3>& force) {
    flags.clear();
    force.clear();
    if (!uploaded_ || !d_->nFib) return;
    std::vector<DFiber> fib(d_->nFib);
    PCHECK(cudaMemcpy(fib.data(), d_->fib, fib.size() * sizeof(DFiber), cudaMemcpyDeviceToHost));
    for (const DFiber& F : fib) {
        flags.push_back(F.flags);
        force.push_back(F.force);
    }
}

float PhysWorld::patchForce(int b) {
    if (!uploaded_) return 0.0f;
    int np = 0;
    PCHECK(cudaMemcpy(&np, d_->nPatches, sizeof(int), cudaMemcpyDeviceToHost));
    np = std::min(np, d_->maxPatches);
    std::vector<DPatch> P(np);
    if (np) PCHECK(cudaMemcpy(P.data(), d_->patches, np * sizeof(DPatch), cudaMemcpyDeviceToHost));
    const float h = lastH_;
    float f = 0.0f;
    for (const DPatch& c : P)
        if (c.a == b || c.b == b) f += c.J / (h * h);
    return f;
}

void PhysWorld::writeTransforms(void* instances, size_t stride, int first, int count, void* stream) {
    if (!uploaded_ || count <= 0) return;
    kTransforms<<<blocks(count), 128, 0, static_cast<cudaStream_t>(stream)>>>(*d_, static_cast<char*>(instances), stride, first, count);
}
