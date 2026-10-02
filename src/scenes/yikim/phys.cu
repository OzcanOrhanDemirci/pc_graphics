// Scene 3 "Yikim": the GPU solver (see phys.h). One frame: broad phase (sort and sweep along x) with boxes grown by
// the frame's motion, then substeps of: integrate, interfaces colour by colour, contacts (Jacobi), velocities,
// velocity pass (dynamic friction, restitution, interface damping).
#include "scenes/yikim/phys.h"

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
    float depth;          // deepest point of the patch (an overlap being pushed out, or merely touching)
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
    // per body: displacement / velocity (3) and rotation / angular velocity (3) corrections summed in 2^-40 fixed
    // point: integer addition is associative, so the order the atomics land in no longer changes the result and a
    // run repeats exactly (float atomics made the same earthquake bring a building down in one run and not the next)
    long long* acc = nullptr;
    int* cnt = nullptr;
    int* cntPrev = nullptr;   // each body's contact pairs in the last substep (the shares, see finishPair)
    // shapes
    int *sampleStart = nullptr, *sampleCount = nullptr, *planeStart = nullptr, *planeCount = nullptr;
    float3* samples = nullptr;
    float4* planes = nullptr;
    // interfaces
    int nIfc = 0, nFib = 0;
    DInterface* ifc = nullptr;
    DFiber* fib = nullptr;
    int nBars = 0;
    int2* bars = nullptr;            // display: the reinforced fibres (fibre, interface)
    float3* ifcNB = nullptr;         // display: each interface's normal in body b's frame, pointing out of b
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
    int* pairsHost = nullptr;        // page-locked: the pair count read back after a step
    int* frameDev = nullptr;
    float* gravityDev = nullptr;     // this frame's gravity (ramped up while a world settles)
    int maxSubsteps = 0;
    cudaGraph_t graph = nullptr;
    cudaGraphExec_t graphExec = nullptr;
};

// ------------------------------------------------------------------------------------------------------------
// device helpers
// ------------------------------------------------------------------------------------------------------------
#define FIX_SCALE 1099511627776.0   // 2^40: 1e-12 resolution, +-8e6 range
__device__ __forceinline__ void accumFix(long long* p, float v) {
    atomicAdd(reinterpret_cast<unsigned long long*>(p), static_cast<unsigned long long>(__double2ll_rn(double(v) * FIX_SCALE)));
}
__device__ __forceinline__ void accum(DevWorld& W, int b, float3 lin, float3 ang) {
    long long* p = W.acc + 6 * size_t(b);
    accumFix(p + 0, lin.x); accumFix(p + 1, lin.y); accumFix(p + 2, lin.z);
    accumFix(p + 3, ang.x); accumFix(p + 4, ang.y); accumFix(p + 5, ang.z);
}
__device__ __forceinline__ void takeAccum(DevWorld& W, int b, float3& lin, float3& ang) {   // read and clear
    long long* p = W.acc + 6 * size_t(b);
    const double k = 1.0 / FIX_SCALE;
    lin = make_float3(float(p[0] * k), float(p[1] * k), float(p[2] * k));
    ang = make_float3(float(p[3] * k), float(p[4] * k), float(p[5] * k));
    for (int c = 0; c < 6; ++c) p[c] = 0;
}
// generalised inverse mass of a body for a push along unit n at lever r (world)
__device__ __forceinline__ float genInvMass(float im, float3 iI, float4 q, float3 r, float3 n) {
    const float3 rn = cross(r, n);
    return im + dot(rn, invInertiaMul(q, iI, rn));
}

// ------------------------------------------------------------------------------------------------------------
// integrate
// ------------------------------------------------------------------------------------------------------------
// Damping (measured, kept in DURUM): an explicit per-interface velocity damping cannot reach a building's ~5% (it is
// capped at removing the relative velocity of a substep, and a smooth sway leaves neighbours almost none: 0.5-0.6% of
// critical whatever the coefficient); XPBD's implicit stiffness damping softened the structure instead (first period
// 0.45 -> 1.43 s). What is left is Rayleigh's other half: mass-proportional damping of the motion relative to the
// ground, zeta = alpha / (2 omega), for bodies still bonded to something (debris falls free), with its deceleration
// capped (a toppling bonded block is not held back by more than a few percent of g).
__device__ __forceinline__ bool hasLivingInterface(const DevWorld& W, int i) {
    for (int k = W.adjStart[i]; k < W.adjStart[i + 1]; ++k)
        if (W.ifc[W.adjList[k]].alive > 0) return true;
    return false;
}

__global__ void kIntegrate(DevWorld W, float h, int substep, float maxSpeed, float alpha, float capLin, float capAng) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= W.n) return;
    const float g = *W.gravityDev;
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
    float3 wv = W.w[i];
    if (alpha > 0.0f && hasLivingInterface(W, i)) {
        const float3 vr = v - W.v[0];   // relative to the ground (the ground body's velocity is this substep's)
        const float vl = length(vr), wl = length(wv);
        if (vl > 0.0f) v = v - vr * (fminf(alpha * h * vl, capLin * h) / vl);
        if (wl > 0.0f) wv = wv * (1.0f - fminf(alpha * h * wl, capAng * h) / wl);
    }
    v.y -= g * h;
    const float sp = length(v);
    if (sp > maxSpeed) v = v * (maxSpeed / sp);   // guard, never reached by sane motion
    W.v[i] = v;
    W.x[i] = W.x[i] + d3(v * h);
    // rotation: gyroscopic term in the body frame, then q += h/2 [w,0] q
    const float4 q = W.q[i];
    const float3 iI = W.invI[i];
    float3 wl = qrotInv(q, wv);
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

// (Tried and measured: one warp per interface, lanes loading the fibres at once and walking them in lockstep, gave the
// same 11.6 ms: the cost is the 13 colours x 2 passes x 40 substeps of small kernels, each as long as its longest
// interface's serial walk, not memory waiting. 13 colours is the colouring's lower bound, the most interfaces on a body.)
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
    float depth = 0.0f;                  // deepest point (m)
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
__device__ void pointCorrection(PairState& S, bool pIsA, float3 rp, float3 m, float depth, float h, const PhysParams& prm, PatchAcc& acc,
                                float tolerance = 0.0f) {
    const float C = fminf(depth - fmaxf(prm.slop, tolerance), prm.maxDepenetration * h);
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
    acc.depth = fmaxf(acc.depth, depth);
}

// samples of P against the planes of Q (P = a when pIsA)
__device__ void pointsVsHull(const DevWorld& W, PairState& S, int p, int q, bool pIsA, float h, const PhysParams& prm, PatchAcc& acc, int lane,
                             float tolerance) {
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
        // a point deeper than deepOverlap inside an unglued body is past anything a solid does: pushing it out was a force
        // that never ended, and the pair, averaged unevenly over its bodies' other contacts, flew off together (measured:
        // a column piece wedged into the tower's debris rose to 190 m at up to 105 m/s, 1.2 MN between them for 9 s)
        if (prm.deepOverlap > 0.0f && tolerance == 0.0f && -best > prm.deepOverlap) continue;
        pointCorrection(S, pIsA, rp, qrot(qq, make_float3(P.x, P.y, P.z)), -best, h, prm, acc, tolerance);
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

// the pair's net motion into the bodies' accumulators, and the patch record. Jacobi: a body in several pairs takes a share
// of each. The share is the pair's, the same for both bodies: one over the larger of their pair counts (last substep's).
// Each body taking one over its own count instead broke the pair's momentum: two bodies wedged together, one touching
// many others, pushed each other unevenly for seconds and flew off as one (measured in the tower's collapse: a column
// piece rose to 190 m at 105 m/s, another slid along the ground at 53 m/s, 0.8-1.2 MN between them).
#define BOND_OVERLAP 0.02f   // m: how far bonded bodies may overlap before contact steps in

__device__ __forceinline__ float pairShare(const DevWorld& W, int a, int b, bool movesA, bool movesB) {
    const int ca = movesA ? W.cntPrev[a] : 1, cb = movesB ? W.cntPrev[b] : 1;
    return 1.0f / float(max(1, max(ca, cb)));
}

__device__ void finishPair(DevWorld& W, int a, int b, const PairState& S, const PatchAcc& last, float J, bool bonded = false) {
    if (J <= 0.0f) return;
    const float share = pairShare(W, a, b, S.ima > 0.0f, S.imb > 0.0f);
    if (S.ima > 0.0f) {
        const float4 dq = qmul(S.qa, qconj(W.q[a]));
        const float3 dth = make_float3(dq.x, dq.y, dq.z) * (dq.w < 0.0f ? -2.0f : 2.0f);
        accum(W, a, diff3(S.xa, W.x[a]) * share, dth * share);
        atomicAdd(&W.cnt[a], 1);
    }
    if (S.imb > 0.0f) {
        const float4 dq = qmul(S.qb, qconj(W.q[b]));
        const float3 dth = make_float3(dq.x, dq.y, dq.z) * (dq.w < 0.0f ? -2.0f : 2.0f);
        accum(W, b, diff3(S.xb, W.x[b]) * share, dth * share);
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
        c.spent = bonded ? 1e30f : S.lamT;   // a bonded pair gets no friction (its fibres hold it)
        c.radius = sqrtf(fmaxf(0.0f, last.rr / last.lam - dot(ra, ra)));
        c.depth = last.depth;
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
__device__ __forceinline__ float warpMax(float v) {
    for (int o = 16; o > 0; o >>= 1) v = fmaxf(v, __shfl_down_sync(0xffffffffu, v, o));
    return __shfl_sync(0xffffffffu, v, 0);
}
__device__ void warpReduce(PairState& S, PatchAcc& acc) {
    S.dxa = warpSum3(S.dxa); S.dta = warpSum3(S.dta); S.dxb = warpSum3(S.dxb); S.dtb = warpSum3(S.dtb);
    acc.lam = warpSum(acc.lam); acc.r = warpSum3(acc.r); acc.n = warpSum3(acc.n); acc.rr = warpSum(acc.rr);
    acc.points = int(warpSum(float(acc.points)) + 0.5f);
    acc.depth = warpMax(acc.depth);
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
    // a bonded pair touches face to face by design: contact only past BOND_OVERLAP. Skipping it altogether let a
    // crushed, half-broken bond sink one body into the other (a wall block 30 cm into the ground), and the overlap
    // exploded when the last fibre went (measured: 29 m/s).
    const bool bnd = bonded(W, a, b);
    const float tol = bnd ? BOND_OVERLAP : 0.0f;
    PairState S = loadPair(W, a, b);
    PatchAcc last;
    float J = 0.0f;
    for (int it = 0; it < prm.contactIters; ++it) {
        PatchAcc acc;
        pointsVsHull(W, S, a, b, true, h, prm, acc, lane, tol);
        pointsVsHull(W, S, b, a, false, h, prm, acc, lane, tol);
        warpReduce(S, acc);
        acc.J = J;
        if (!applyPoints(S, acc)) { if (it == 0) return; break; }
        J = acc.J;
        if (!bnd) frictionOnLane0(W, a, b, frame, S, acc, prm, it == prm.contactIters - 1, lane);
        last = acc;
    }
    if (lane == 0) finishPair(W, a, b, S, last, J, bnd);
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
        const bool bnd = bonded(W, i, 0);   // held by its foundation: contact only past BOND_OVERLAP (see contactPairWarp)
        const float tol = bnd ? BOND_OVERLAP : 0.0f;
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
                if (y < -tol) pointCorrection(S, true, r, make_float3(0, 1, 0), -y, h, prm, acc, tol);
            }
            warpReduce(S, acc);
            acc.J = J;
            if (!applyPoints(S, acc)) break;
            any = true;
            J = acc.J;
            if (!bnd) frictionOnLane0(W, i, 0, frame, S, acc, prm, it == prm.contactIters - 1, lane);
            last = acc;
        }
        if (any && lane == 0) finishPair(W, i, 0, S, last, J, bnd);
    }
}

__global__ void kApply(DevWorld W, float relax) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= W.n) return;
    const int c = W.cnt[i];
    float3 dx, dth;
    takeAccum(W, i, dx, dth);
    if (c > 0 && !W.kin[i]) {   // the pairs' shares are in already (finishPair)
        const float s = c > 1 ? relax : 1.0f;
        W.x[i] = W.x[i] + d3(dx * s);
        W.q[i] = qaddRot(W.q[i], dth * s);
    }
    W.cntPrev[i] = max(c, 1);
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
        // the other side: separating faster than the bounce, or than the pair was already separating, is velocity the
        // position pass made up pushing an overlap out. It is capped at maxDepenetration; otherwise it piles up over
        // the substeps (measured: a block pressed into the ground by falling debris shot up at 22-52 m/s).
        // an overlap still being pushed out (deeper than touching) separates at maxDepenetration at most: letting it keep
        // last substep's separation ratchets up (measured: 1.5 -> 11.5 -> 29 m/s over two frames)
        const float ceilVn = c.depth > 3.0f * prm.slop ? fmaxf(target, prm.maxDepenetration)
                                                       : fmaxf(fmaxf(target, vnOld), prm.maxDepenetration);
        if (vn < target || vn > ceilVn) {
            const float wn = genInvMass(ima, iIa, qa, c.ra, c.n) + genInvMass(imb, iIb, qb, c.rb, c.n);
            if (wn > 0.0f) P = P + c.n * (((vn < target ? target : ceilVn) - vn) / wn);
        }
        // torsion
        float3 L = make_float3(0, 0, 0);   // angular impulse on a
        const float wn = dot(W.w[a] - W.w[b], c.n);
        if (fabsf(wn) > 1e-6f && c.radius > 0.0f) {
            const float wtor = dot(c.n, invInertiaMul(qa, iIa, c.n)) + dot(c.n, invInertiaMul(qb, iIb, c.n));
            if (wtor > 0.0f) L = c.n * (-copysignf(fminf(prm.muDynamic * fnh * c.radius, fabsf(wn) / wtor), wn));
        }
        const float share = pairShare(W, a, b, ima > 0.0f, imb > 0.0f);   // as in finishPair: the same for both bodies
        P = P * share;
        L = L * share;
        if (ima > 0.0f) {
            accum(W, a, P * ima, invInertiaMul(qa, iIa, cross(c.ra, P) + L));
            atomicAdd(&W.cnt[a], 1);
        }
        if (imb > 0.0f) {
            accum(W, b, P * -imb, invInertiaMul(qb, iIb, cross(c.rb, P * -1.0f) - L));
            atomicAdd(&W.cnt[b], 1);
        }
    }
}

__global__ void kApplyVel(DevWorld W) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= W.n) return;
    const int c = W.cnt[i];
    float3 dv, dw;
    takeAccum(W, i, dv, dw);
    if (c > 0 && !W.kin[i]) {   // shares already taken (kPatchVelocities)
        W.v[i] = W.v[i] + dv;
        W.w[i] = W.w[i] + dw;
    }
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
// Display: the reinforcing bars. A reinforced fibre owns two thin prisms (6 vertices each). While it holds and its crack
// is wider than minGap, one spans the crack (anchor on a to anchor on b). Once it is gone and the faces are apart, a
// stub sticks out of each face, bent a little (the bars of a failed member, stripped of their concrete). A prism not
// shown is NaN: OptiX skips it.
__device__ void barPrism(float3* v, float3 p0, float3 p1, float r) {
    const float3 d = p1 - p0;
    float3 u, w;
    makeBasis(d * (1.0f / fmaxf(length(d), 1e-9f)), u, w);
    for (int k = 0; k < 3; ++k) {
        const float a = k * 2.0943951f;
        const float3 o = (u * cosf(a) + w * sinf(a)) * r;
        v[k] = p0 + o;
        v[k + 3] = p1 + o;
    }
}
__device__ void barNone(float3* v) {
    const float nan = __int_as_float(0x7fc00000);
    for (int k = 0; k < 6; ++k) v[k] = make_float3(nan, nan, nan);
}
__device__ unsigned barHash(unsigned x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
__device__ float3 barBend(float3 n, unsigned h, float most) {
    float3 t, b;
    makeBasis(n, t, b);
    const float a = (h & 0xffffu) * (6.2831853f / 65536.0f), s = most * float((h >> 16) & 0xffu) / 255.0f;
    return normalize(n + (t * cosf(a) + b * sinf(a)) * s);
}
__global__ void kBars(DevWorld W, float3* v, float r, float minGap, float stub) {
    const int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= W.nBars) return;
    const int2 e = W.bars[t];
    const DFiber F = W.fib[e.x];
    const DInterface I = W.ifc[e.y];
    float3* v0 = v + size_t(t) * 12;
    float3* v1 = v0 + 6;
    const float4 qa = W.q[I.a], qb = W.q[I.b];
    const float3 pa = f3(W.x[I.a]) + qrot(qa, F.rA), pb = f3(W.x[I.b]) + qrot(qb, F.rB);
    const float gap = length(pb - pa);
    if (!(F.flags & F_BROKEN)) {
        if (gap > minGap) barPrism(v0, pa, pb, r);
        else barNone(v0);
        barNone(v1);
    } else if (gap > 3.0f * minGap) {
        const unsigned h1 = barHash(unsigned(e.x) * 2u + 1u), h2 = barHash(unsigned(e.x) * 2u + 2u);
        const float3 na = qrot(qmul(qa, I.qI), make_float3(1, 0, 0)), nb = qrot(qb, W.ifcNB[e.y]);
        barPrism(v0, pa, pa + barBend(na, h1, 0.35f) * (stub * (0.6f + 0.4f * float(h2 >> 24) / 255.0f)), r);
        barPrism(v1, pb, pb + barBend(nb, h2, 0.35f) * (stub * (0.6f + 0.4f * float(h1 >> 24) / 255.0f)), r);
    } else {
        barNone(v0);
        barNone(v1);
    }
}

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
                    d_->localC, d_->localH, d_->lo, d_->hi, d_->acc, d_->cnt, d_->cntPrev, d_->sampleStart, d_->sampleCount, d_->planeStart,
                    d_->planeCount, d_->samples, d_->planes, d_->ifc, d_->fib, d_->colourList, d_->adjStart, d_->adjList, d_->mats,
                    d_->keys, d_->keysSorted, d_->ids, d_->idsSorted, d_->sortTemp, d_->pairs, d_->nPairs, d_->patches, d_->nPatches, d_->anchorKey, d_->anchorFrame, d_->anchorSlide, d_->anchorA, d_->anchorB, d_->bars, d_->ifcNB};
    for (void* p : ptrs)
        if (p) cudaFree(p);
    if (d_->graphExec) cudaGraphExecDestroy(d_->graphExec);
    if (d_->graph) cudaGraphDestroy(d_->graph);
    if (d_->groundPath) cudaFree(d_->groundPath);
    if (d_->frameDev) cudaFree(d_->frameDev);
    if (d_->gravityDev) cudaFree(d_->gravityDev);
    if (d_->pairsHost) cudaFreeHost(d_->pairsHost);
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
    invMass.clear(); invInertia.clear(); invMassFree.clear(); invInertiaFree.clear(); kinematic.clear(); materials.clear();
    interfaceDescs.clear();
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
    invMassFree.push_back(0.0f);
    invInertiaFree.push_back(make_float3(0, 0, 0));
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
    invMassFree.push_back(float(1.0 / (mp.volume * density)));
    invInertiaFree.push_back(make_float3(float(1.0 / (vals[0] * density)), float(1.0 / (vals[1] * density)), float(1.0 / (vals[2] * density))));
    invMass.push_back(kin ? 0.0f : invMassFree.back());
    invInertia.push_back(kin ? make_float3(0, 0, 0) : invInertiaFree.back());
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
    W.acc = dmalloc<long long>(size_t(n) * 6);
    W.cnt = dmalloc<int>(n);
    {
        const std::vector<int> ones(n, 1);
        W.cntPrev = dupload(ones);
    }
    // interfaces and fibres
    std::vector<DInterface> ifc;
    std::vector<DFiber> fib;
    std::vector<std::vector<int>> adj(n);
    std::vector<int2> bars;
    std::vector<float3> ifcNB;
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
            if (F.kSteel > 0.0f && F.capSteel > 0.0f) bars.push_back(make_int2(int(fib.size()), int(ifc.size())));
            fib.push_back(F);
            cen = cen + fd.p * (fd.ac + fd.as);
            atot += fd.ac + fd.as;
        }
        if (atot > 0) cen = cen * (1.0f / atot);
        I.cA = qrotInv(rot[d.a], diff3(d3(cen), pos[d.a]));
        I.cB = qrotInv(rot[d.b], diff3(d3(cen), pos[d.b]));
        I.count = int(fib.size()) - I.first;
        I.alive = I.count;
        ifcNB.push_back(qrotInv(rot[d.b], d.n * -1.0f));
        adj[d.a].push_back(int(ifc.size()));
        adj[d.b].push_back(int(ifc.size()));
        ifc.push_back(I);
    }
    W.nIfc = int(ifc.size());
    W.nFib = int(fib.size());
    W.ifc = dupload(ifc);
    W.fib = dupload(fib);
    W.nBars = int(bars.size());
    W.bars = dupload(bars);
    W.ifcNB = dupload(ifcNB);
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
    {   // the lower bound of any colouring: the most interfaces on one moving body
        int maxDeg = 0;
        for (int i = 0; i < n; ++i)
            if (!kinematic[i]) maxDeg = std::max(maxDeg, int(adj[i].size()));
        maxDegree_ = maxDeg;
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
    W.gravityDev = dmalloc<float>(1);
    PCHECK(cudaMallocHost(reinterpret_cast<void**>(&W.pairsHost), sizeof(int)));
    pending_ = false;
    graphKey_.clear();
    PCHECK(cudaEventCreate(&W.e0));
    PCHECK(cudaEventCreate(&W.e1));
    groundPrev_ = groundTarget_ = pos[0];
    // the zeroing memsets above ran on the legacy default stream, which the physics stream (non-blocking) does not wait
    // for: without this a memset could land after the first frame's inputs and zero them (measured: the first frame of
    // a second world fell with no gravity, one frame late on the ground)
    PCHECK(cudaDeviceSynchronize());
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
    stepBegin(dt, nullptr);
    stepEnd();
}

// the step's GPU work is queued and the call returns: a renderer can draw the previous state meanwhile (its own stream).
// after: an event the step waits for first (the renderer's transforms of the state being overwritten).
void PhysWorld::stepBegin(float dt, void* after) {
    if (!uploaded_) upload();
    if (pending_) stepEnd();
    DevWorld& W = *d_;
    const int n = W.n;
    const int S = std::min(std::max(1, params.substeps), W.maxSubsteps);
    const float h = dt / S;
    lastH_ = h;
    cudaStream_t st = W.stream;
    if (after) PCHECK(cudaStreamWaitEvent(st, static_cast<cudaEvent_t>(after), 0));
    // this frame's inputs: where the ground is at the end of each substep, and the frame number (friction anchors)
    groundSteps_.resize(S);
    for (int s = 0; s < S; ++s) groundSteps_[s] = groundPrev_ + (groundTarget_ - groundPrev_) * (double(s + 1) / S);
    PCHECK(cudaMemcpyAsync(W.groundPath, groundSteps_.data(), S * sizeof(double3), cudaMemcpyHostToDevice, st));
    PCHECK(cudaMemcpyAsync(W.frameDev, &frame_, sizeof(int), cudaMemcpyHostToDevice, st));
    gravityNow_ = params.gravity * gravityScale;
    PCHECK(cudaMemcpyAsync(W.gravityDev, &gravityNow_, sizeof(float), cudaMemcpyHostToDevice, st));
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
            kIntegrate<<<blocks(n), 128, 0, st>>>(W, h, s, params.maxSpeed, params.dampAlpha, params.dampCapLin, params.dampCapAng);
            solveInterfaces(h, st);
            PCHECK(cudaMemsetAsync(W.nPatches, 0, sizeof(int), st));
            kContacts<<<contactBlocks, 128, 0, st>>>(W, h, params);
            kGround<<<std::min(blocks(n * 32, 128), 2048), 128, 0, st>>>(W, h, params);
            kApply<<<blocks(n), 128, 0, st>>>(W, params.relax);
            kVelocities<<<blocks(n), 128, 0, st>>>(W, h);
            kPatchVelocities<<<256, 128, 0, st>>>(W, h, params, params.gravity);
            kApplyVel<<<blocks(n), 128, 0, st>>>(W);
        }
        PCHECK(cudaStreamEndCapture(st, &W.graph));
        PCHECK(cudaGraphInstantiate(&W.graphExec, W.graph, 0));
        graphKey_ = key;
    }
    PCHECK(cudaGraphLaunch(W.graphExec, st));
    PCHECK(cudaMemcpyAsync(W.pairsHost, W.nPairs, sizeof(int), cudaMemcpyDeviceToHost, st));
    PCHECK(cudaEventRecord(W.e1, st));
    pending_ = true;
    pendingDt_ = dt;
}

void PhysWorld::stepEnd() {
    if (!pending_) return;
    DevWorld& W = *d_;
    PCHECK(cudaEventSynchronize(W.e1));
    PCHECK(cudaEventElapsedTime(&lastMs, W.e0, W.e1));
    PCHECK(cudaGetLastError());
    lastPairs_ = std::min(*W.pairsHost, W.maxPairs);
    groundPrev_ = groundTarget_;
    time += pendingDt_;
    ++frame_;
    pending_ = false;
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
        timed(1, [&] { kIntegrate<<<blocks(n), 128, 0, st>>>(W, h, s, params.maxSpeed, params.dampAlpha, params.dampCapLin, params.dampCapAng); });
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

// load a freshly built world gently: gravity ramps from zero (smoothstep) over ramp seconds, then the world rests for
// the remaining time. Switching full gravity on at once drops a tall frame like jelly for the first frames (solver
// stiffness is limited) and the jolt broke thin parapets at rest (measured: a balcony fell at half of g at first).
void PhysWorld::settle(float seconds, float ramp, float dt) {
    const int frames = int(seconds / dt + 0.5f);
    for (int k = 0; k < frames; ++k) {
        const float t = std::min(1.0f, (k + 1) * dt / std::max(ramp, 1e-3f));
        gravityScale = t * t * (3.0f - 2.0f * t);
        step(dt);
    }
    gravityScale = 1.0f;
    time = 0;
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

void PhysWorld::removeBody(int b) {
    if (!uploaded_ || b <= 0 || b >= d_->n) return;
    breakBody(b);
    double3 x;
    PCHECK(cudaMemcpy(&x, d_->x + b, sizeof(double3), cudaMemcpyDeviceToHost));
    x.y = -1000.0;
    const unsigned one = 1u;
    const float zero = 0.0f;
    const float3 z3 = make_float3(0, 0, 0);
    PCHECK(cudaMemcpy(d_->x + b, &x, sizeof(double3), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->xPrev + b, &x, sizeof(double3), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->kin + b, &one, sizeof(unsigned), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->invMass + b, &zero, sizeof(float), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->invI + b, &z3, sizeof(float3), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->v + b, &z3, sizeof(float3), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->w + b, &z3, sizeof(float3), cudaMemcpyHostToDevice));
    kinematic[b] = 1u;
    invMass[b] = 0.0f;
}

void PhysWorld::writeBody(int b) {
    const unsigned k = kinematic[b];
    PCHECK(cudaMemcpy(d_->x + b, &pos[b], sizeof(double3), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->xPrev + b, &pos[b], sizeof(double3), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->q + b, &rot[b], sizeof(float4), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->qPrev + b, &rot[b], sizeof(float4), cudaMemcpyHostToDevice));
    for (float3* p : {d_->v, d_->vOld}) PCHECK(cudaMemcpy(p + b, &vel[b], sizeof(float3), cudaMemcpyHostToDevice));
    for (float3* p : {d_->w, d_->wOld}) PCHECK(cudaMemcpy(p + b, &angVel[b], sizeof(float3), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->invMass + b, &invMass[b], sizeof(float), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->invI + b, &invInertia[b], sizeof(float3), cudaMemcpyHostToDevice));
    PCHECK(cudaMemcpy(d_->kin + b, &k, sizeof(unsigned), cudaMemcpyHostToDevice));
}

void PhysWorld::parkBody(int b) {
    if (b <= 0 || b >= int(info.size())) return;
    pos[b].y = -1000.0;
    vel[b] = angVel[b] = make_float3(0, 0, 0);
    kinematic[b] = 1u;
    invMass[b] = 0.0f;
    invInertia[b] = make_float3(0, 0, 0);
    if (uploaded_) writeBody(b);
}

void PhysWorld::launchBody(int b, double3 x, float4 q, float3 v, float3 w) {
    if (b <= 0 || b >= int(info.size())) return;
    pos[b] = x;
    rot[b] = q;
    vel[b] = v;
    angVel[b] = w;
    kinematic[b] = 0u;
    invMass[b] = invMassFree[b];
    invInertia[b] = invInertiaFree[b];
    if (uploaded_) writeBody(b);
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
    s.maxDegree = maxDegree_;
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

std::vector<PhysWorld::PatchInfo> PhysWorld::patchesOf(int b) {
    std::vector<PatchInfo> out;
    if (!uploaded_) return out;
    int np = 0;
    PCHECK(cudaMemcpy(&np, d_->nPatches, sizeof(int), cudaMemcpyDeviceToHost));
    np = std::min(np, d_->maxPatches);
    std::vector<DPatch> P(np);
    if (np) PCHECK(cudaMemcpy(P.data(), d_->patches, np * sizeof(DPatch), cudaMemcpyDeviceToHost));
    for (const DPatch& c : P)
        if (c.a == b || c.b == b) out.push_back({c.a == b ? c.b : c.a, c.J / (lastH_ * lastH_), c.a == b ? c.n : c.n * -1.0f});
    return out;
}

int PhysWorld::barPrisms() const { return uploaded_ ? 2 * d_->nBars : 0; }

void PhysWorld::writeBars(void* verts, float radius, float minGap, float stub, void* stream) const {
    if (!uploaded_ || !d_->nBars) return;
    kBars<<<blocks(d_->nBars), 128, 0, static_cast<cudaStream_t>(stream)>>>(*d_, static_cast<float3*>(verts), radius, minGap, stub);
}

void PhysWorld::writeTransforms(void* instances, size_t stride, int first, int count, void* stream) {
    if (!uploaded_ || count <= 0) return;
    kTransforms<<<blocks(count), 128, 0, static_cast<cudaStream_t>(stream)>>>(*d_, static_cast<char*>(instances), stride, first, count);
}
