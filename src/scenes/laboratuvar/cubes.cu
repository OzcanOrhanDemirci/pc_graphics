// Scene 4 "Laboratuvar", the Küp experiment's rigid cubes (cubes.h).
#include "scenes/laboratuvar/cubes.h"
#include "shared/vec.h"

#include <cub/device/device_radix_sort.cuh>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kB = 256;
inline int nb(int n) { return (n + kB - 1) / kB; }

#define CCUDA(x)                                                                                    \
    do {                                                                                            \
        cudaError_t e_ = (x);                                                                       \
        if (e_ != cudaSuccess) std::fprintf(stderr, "kup: %s (%s:%d)\n", cudaGetErrorString(e_), __FILE__, __LINE__); \
    } while (0)

template <class T> T* dalloc(size_t n) {
    T* p = nullptr;
    CCUDA(cudaMalloc(reinterpret_cast<void**>(&p), std::max<size_t>(n, 1) * sizeof(T)));
    return p;
}
template <class T> void dfree(T*& p) {
    if (p) cudaFree(p);
    p = nullptr;
}

enum : uint32_t { S_AWAKE = 0, S_ASLEEP = 1, S_GONE = 2 };

// ---- quaternions (x, y, z, w)
__host__ __device__ inline float3 xyz(float4 a) { return make_float3(a.x, a.y, a.z); }
__host__ __device__ inline float4 qmul(float4 a, float4 b) {
    return make_float4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                       a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
__host__ __device__ inline float4 qconj(float4 q) { return make_float4(-q.x, -q.y, -q.z, q.w); }
__host__ __device__ inline float4 qnorm(float4 q) {
    const float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    const float k = l > 0.0f ? 1.0f / l : 0.0f;
    return l > 0.0f ? make_float4(q.x * k, q.y * k, q.z * k, q.w * k) : make_float4(0, 0, 0, 1);
}
// a rotation's three axes (the columns of its matrix)
struct Rot {
    float3 c0, c1, c2;
};
__host__ __device__ inline Rot qrot(float4 q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    Rot r;
    r.c0 = make_float3(1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w));
    r.c1 = make_float3(2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w));
    r.c2 = make_float3(2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y));
    return r;
}
__host__ __device__ inline float3 rmul(const Rot& r, float3 v) { return r.c0 * v.x + r.c1 * v.y + r.c2 * v.z; }
__host__ __device__ inline float3 rtmul(const Rot& r, float3 v) { return make_float3(dot(r.c0, v), dot(r.c1, v), dot(r.c2, v)); }
__host__ __device__ inline float4 qintegrate(float4 q, float3 dtheta) {   // q + 0.5 (dtheta, 0) q, normalised
    const float4 d = qmul(make_float4(dtheta.x, dtheta.y, dtheta.z, 0.0f), q);
    return qnorm(make_float4(q.x + 0.5f * d.x, q.y + 0.5f * d.y, q.z + 0.5f * d.z, q.w + 0.5f * d.w));
}

__host__ __device__ inline uint32_t hashCell(int x, int y, int z, uint32_t mask) {
    return (uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u) & mask;
}
__device__ inline int3 cellOf(float3 p, float inv) { return make_int3(int(floorf(p.x * inv)), int(floorf(p.y * inv)), int(floorf(p.z * inv))); }

__device__ inline unsigned hashU(unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
__device__ inline float hashF(unsigned h) { return (h >> 8) * (1.0f / 16777216.0f); }

struct Consts {
    float a, h;          // edge, half edge
    float invM, invI;    // 1 / mass, 1 / (m a^2 / 6)
    float g, kDrag, angDamp;
    float e, eFloor, muS, muD, muFS, muFD;
    float vRest;         // a contact slower than this (normal) does not bounce
    float maxPush;       // m a substep
    float hs;            // the substep's length
    float tol;           // a point this near a face's edge still counts on it (a lattice's corners)
    float sor;
    float restDamp;
    float maxMargin;
    int debug;
    float sleepV, sleepT;
    float tableHW, tableFront, tableBack, backHW, backWall, backSweep;
};

// ---- the substep

__global__ void kIntegrate(int n, float4* x, float4* xp, float4* v, float4* w, float4* q, float4* qp, const uint32_t* st, float4* dxT, float4* dtT, float4* dvT,
                           float4* dwT, Consts C, float hs) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    dxT[i] = dtT[i] = dvT[i] = dwT[i] = make_float4(0, 0, 0, 0);
    if (st[i] != S_AWAKE) return;
    float3 vel = xyz(v[i]);
    vel.y -= C.g * hs;
    vel = vel * (1.0f / (1.0f + C.kDrag * sqrtf(dot(vel, vel)) * hs));   // quadratic drag, implicit
    float3 om = xyz(w[i]) * (1.0f / (1.0f + C.angDamp * hs));
    v[i] = make_float4(vel.x, vel.y, vel.z, 0.0f);
    w[i] = make_float4(om.x, om.y, om.z, 0.0f);
    const float4 p = x[i];
    xp[i] = p;
    x[i] = make_float4(p.x + vel.x * hs, p.y + vel.y * hs, p.z + vel.z * hs, p.w);
    qp[i] = q[i];
    q[i] = qintegrate(q[i], om * hs);
}

__global__ void kKeys(int n, const float4* x, uint32_t* key, uint32_t* id, float inv, uint32_t mask) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const int3 c = cellOf(xyz(x[i]), inv);
    key[i] = hashCell(c.x, c.y, c.z, mask);
    id[i] = uint32_t(i);
}

__global__ void kRanges(int n, const uint32_t* keySorted, int* start, int* end) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    const uint32_t c = keySorted[k];
    if (k == 0 || keySorted[k - 1] != c) start[c] = k;
    if (k == n - 1 || keySorted[k + 1] != c) end[c] = k + 1;
}

// a body's colour: its cell's coordinates modulo 4, in cells a / sqrt 3 wide (one cube's centre to a cell at most:
// two touching cubes are a apart at least). Two cubes of one colour are 3 cells = sqrt 3 a apart at least, beyond any
// touch, and are solved at once; cells the grid's size (a circumsphere) put touching cubes in one cell, one colour,
// and they read each other half-written (an 8-high stack blew apart)
__global__ void kColours(int n, const float4* x, uint32_t* key, uint32_t* id, float inv) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const int3 c = cellOf(xyz(x[i]), inv);
    key[i] = uint32_t((c.x & 3) + 4 * (c.y & 3) + 16 * (c.z & 3));
    id[i] = uint32_t(i);
}

// a body as the solver sees it now: where it is, how it was moving this substep, what its corrections so far did
struct Body {
    float3 x, v, w;       // v, w: this substep's integrated velocities
    float3 dx, dt;        // the moves so far this substep (overlaps pushed out)
    float3 dv, dw;        // the velocity changes so far (times h): approach stopped, bounce, friction
    Rot R;
};

constexpr int kMaxPts = 8;

// one touching pair's points, Gauss-Seidel over them (a few passes, forwards and backwards); I applies its share at
// once, the other body its own when its colour comes (from where things are then). Split impulses: the MOVE pushes
// the overlap out, a translation, no speed (a stack's leftover overlap would come back as a jitter every substep, and
// a turn would walk it apart); the VELOCITY stops the approach, or separates at e times it (the bounce) when it came
// faster than vRest, and FRICTION cancels the points' slip within Coulomb's cone (the accumulated tangential impulse
// at most mu_s times the normal one, else mu_d of it). Friction too goes point by point: four corners each cancelling
// the whole slip overshoot four times over, and the stack shook itself apart.
__device__ inline void pairStep(Body& I, const Body& J, bool jMoves, int np, const float3* u, const float3* c, const float* d, float e, float muS, float muD,
                                const Consts& C, float3& dxP, float3& dtP, float3& dxV, float3& dtV) {
    float wsum[kMaxPts], needV[kMaxPts];
    for (int k = 0; k < np; ++k) {
        const float3 ri = c[k] - I.x, rj = c[k] - J.x;
        const float3 a = cross(ri, u[k]);
        float w = C.invM + dot(a, a) * C.invI;
        float3 vj = make_float3(0, 0, 0), dj = make_float3(0, 0, 0);
        if (jMoves) {
            const float3 bb = cross(rj, u[k]);
            w += C.invM + dot(bb, bb) * C.invI;
            vj = J.v + cross(J.w, rj);
            dj = J.dv + cross(J.dw, rj);
        }
        wsum[k] = w;
        const float vn = dot(I.v + cross(I.w, ri) - vj, u[k]);                                        // > 0: apart
        const float doneV = dot((I.v + cross(I.w, ri) - vj) * C.hs + I.dv + cross(I.dw, ri) - dj, u[k]);   // apart, times h, so far
        const float allowed = fmaxf(0.0f, -d[k]);   // a gap: the pair may close it this substep (speculative contact)
        float nv = -allowed - doneV;
        if (vn < -C.vRest && vn * C.hs < -allowed) nv = fmaxf(nv, e * -vn * C.hs - doneV);   // they meet: the bounce
        needV[k] = nv;
    }
    float3 pxi = make_float3(0, 0, 0), pxj = make_float3(0, 0, 0);
    float3 vxi = make_float3(0, 0, 0), vti = make_float3(0, 0, 0), vxj = make_float3(0, 0, 0), vtj = make_float3(0, 0, 0);
    float lamP[kMaxPts], lamV[kMaxPts];
    float3 lamT[kMaxPts];
    for (int k = 0; k < np; ++k) {
        lamP[k] = lamV[k] = 0.0f;
        lamT[k] = make_float3(0, 0, 0);
    }
    const float wP = jMoves ? 2.0f * C.invM : C.invM;
    for (int it = 0; it < 3; ++it)
        for (int kk = 0; kk < np; ++kk) {
            const int k = (it & 1) ? np - 1 - kk : kk;
            const float3 ri = c[k] - I.x, rj = c[k] - J.x;
            // the move
            float gp = d[k] - dot(pxi, u[k]);
            if (jMoves) gp += dot(pxj, u[k]);
            const float dl = fmaxf(gp / wP, -lamP[k]);   // pushes, never pulls
            lamP[k] += dl;
            pxi = pxi + u[k] * (dl * C.invM);
            if (jMoves) pxj = pxj - u[k] * (dl * C.invM);
            // the velocity along the normal
            float gv = needV[k] - dot(vxi + cross(vti, ri), u[k]);
            if (jMoves) gv += dot(vxj + cross(vtj, rj), u[k]);
            const float dv = fmaxf(gv / wsum[k], -lamV[k]);
            lamV[k] += dv;
            vxi = vxi + u[k] * (dv * C.invM);
            vti = vti + cross(ri, u[k]) * (dv * C.invI);
            if (jMoves) {
                vxj = vxj - u[k] * (dv * C.invM);
                vtj = vtj - cross(rj, u[k]) * (dv * C.invI);
            }
            // friction at this point: its slip now, against what has been pushed along the normal here
            float3 dp = (I.v + cross(I.w, ri)) * C.hs + I.dv + cross(I.dw, ri) + vxi + cross(vti, ri);
            if (jMoves) dp = dp - (J.v + cross(J.w, rj)) * C.hs - J.dv - cross(J.dw, rj) - vxj - cross(vtj, rj);
            const float3 dpt = dp - u[k] * dot(dp, u[k]);
            const float L = sqrtf(dot(dpt, dpt));
            if (L < 1e-12f || lamV[k] <= 0.0f) continue;
            const float3 t = dpt * (-1.0f / L);
            const float3 rxt = cross(ri, t);
            float wt = C.invM + dot(rxt, rxt) * C.invI;
            if (jMoves) {
                const float3 rjxt = cross(rj, t);
                wt += C.invM + dot(rjxt, rjxt) * C.invI;
            }
            float3 lt = lamT[k] + t * (L / wt);
            const float lm = sqrtf(dot(lt, lt));
            if (lm > muS * lamV[k]) lt = lt * (muD * lamV[k] / lm);   // sliding: kinetic, on the cone's edge
            const float3 dlt = lt - lamT[k];
            lamT[k] = lt;
            vxi = vxi + dlt * C.invM;
            vti = vti + cross(ri, dlt) * C.invI;
            if (jMoves) {
                vxj = vxj - dlt * C.invM;
                vtj = vtj - cross(rj, dlt) * C.invI;
            }
        }
    dxP = pxi;
    dtP = make_float3(0, 0, 0);
    dxV = vxi;
    dtV = vti;
}

// I moves and turns by (dx, dt) and changes its velocity by (vx, vt) (times h), each at most maxPush
__device__ inline void applyTo(Body& I, float4& q, float3 dx, float3 dt, float3 vx, float3 vt, const Consts& C) {
    auto cap = [&](float3& a, float3& b) {
        const float L = sqrtf(dot(a, a));
        if (L > C.maxPush) a = a * (C.maxPush / L);
        const float Lr = sqrtf(dot(b, b)) * C.h;
        if (Lr > C.maxPush) b = b * (C.maxPush / Lr);
    };
    cap(dx, dt);
    cap(vx, vt);
    I.x = I.x + dx;
    I.dx = I.dx + dx;
    I.dt = I.dt + dt;
    I.dv = I.dv + vx;
    I.dw = I.dw + vt;
    q = qintegrate(q, dt);
    I.R = qrot(q);
}

// two cubes of half edge h: where they touch (Gottschalk's separating axes, then the incident face clipped by the
// reference face's sides, as ODE's dBoxBox; an edge across an edge: one point between them). n: from A to B; the
// points lie on the incident box, inside the reference one by dep (negative: a gap under margin, a speculative
// contact: the pair may close it this substep, no more). 0: apart.
__device__ inline int boxBox(const Body& A, const Body& B, float h, float tol, float margin, float3& n, float3* pts, float* dep) {
    const float3 T = B.x - A.x;
    const float3 a[3] = {A.R.c0, A.R.c1, A.R.c2}, b[3] = {B.R.c0, B.R.c1, B.R.c2};
    float Ab[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) Ab[i][j] = fabsf(dot(a[i], b[j])) + 1e-6f;
    float best = 1e30f;
    int code = -1;
    float3 nb = make_float3(0, 1, 0);
    for (int i = 0; i < 3; ++i) {
        const float t = dot(T, a[i]);
        const float pen = h + h * (Ab[i][0] + Ab[i][1] + Ab[i][2]) - fabsf(t);
        if (pen <= -margin) return 0;
        if (pen < best) { best = pen; code = i; nb = a[i] * (t >= 0.0f ? 1.0f : -1.0f); }
    }
    for (int j = 0; j < 3; ++j) {
        const float t = dot(T, b[j]);
        const float pen = h + h * (Ab[0][j] + Ab[1][j] + Ab[2][j]) - fabsf(t);
        if (pen <= -margin) return 0;
        if (pen < best * 0.999f) { best = pen; code = 3 + j; nb = b[j] * (t >= 0.0f ? 1.0f : -1.0f); }
    }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float3 ax = cross(a[i], b[j]);
            const float L = sqrtf(dot(ax, ax));
            if (L < 1e-3f) continue;   // parallel edges: a face axis says it
            ax = ax * (1.0f / L);
            const float rA = h * (fabsf(dot(a[0], ax)) + fabsf(dot(a[1], ax)) + fabsf(dot(a[2], ax)));
            const float rB = h * (fabsf(dot(b[0], ax)) + fabsf(dot(b[1], ax)) + fabsf(dot(b[2], ax)));
            const float t = dot(T, ax);
            const float pen = rA + rB - fabsf(t);
            if (pen <= -margin) return 0;
            if (pen < 0.95f * best - 1e-4f * h) { best = pen; code = 6 + 3 * i + j; nb = ax * (t >= 0.0f ? 1.0f : -1.0f); }
        }
    n = nb;
    if (code >= 6) {   // an edge of A across an edge of B: the nearest points of the two edges
        const int i = (code - 6) / 3, j = (code - 6) % 3;
        float3 pA = A.x, pB = B.x;
        for (int m = 0; m < 3; ++m) {
            if (m != i) pA = pA + a[m] * (dot(n, a[m]) >= 0.0f ? h : -h);
            if (m != j) pB = pB - b[m] * (dot(n, b[m]) >= 0.0f ? h : -h);
        }
        const float3 r = pA - pB;
        const float a12 = dot(a[i], b[j]), b1 = dot(a[i], r), b2 = dot(b[j], r);
        const float den = fmaxf(1.0f - a12 * a12, 1e-6f);
        const float sA = fminf(h, fmaxf(-h, (a12 * b2 - b1) / den));
        const float tB = fminf(h, fmaxf(-h, b2 + sA * a12));
        pts[0] = (pA + a[i] * sA + pB + b[j] * tB) * 0.5f;
        dep[0] = best;
        return 1;
    }
    // a face: the reference box's face along nr, the other box's face most against it, clipped by the reference
    // face's four sides
    const bool refA = code < 3;
    const Body& Rf = refA ? A : B;
    const Body& In = refA ? B : A;
    const float3 nr = refA ? n : n * -1.0f;   // out of the reference face, towards the incident box
    const int k = code % 3;
    const float3 ra[3] = {Rf.R.c0, Rf.R.c1, Rf.R.c2}, ia[3] = {In.R.c0, In.R.c1, In.R.c2};
    const float3 su = ra[(k + 1) % 3], sv = ra[(k + 2) % 3];
    int m = 0;
    float bestDot = -1.0f;
    for (int q = 0; q < 3; ++q) {
        const float dq = fabsf(dot(ia[q], nr));
        if (dq > bestDot) { bestDot = dq; m = q; }
    }
    const float3 ni = ia[m] * (dot(ia[m], nr) > 0.0f ? -1.0f : 1.0f);
    const float3 fc = In.x + ni * h, e1 = ia[(m + 1) % 3] * h, e2 = ia[(m + 2) % 3] * h;
    float3 poly[8], tmp[8];
    int np = 4;
    poly[0] = fc + e1 + e2;
    poly[1] = fc - e1 + e2;
    poly[2] = fc - e1 - e2;
    poly[3] = fc + e1 - e2;
    for (int side = 0; side < 4; ++side) {   // Sutherland-Hodgman against dot(p - Rf.x, ax) <= h
        const float3 ax = side == 0 ? su : side == 1 ? su * -1.0f : side == 2 ? sv : sv * -1.0f;
        int no = 0;
        for (int q = 0; q < np; ++q) {
            const float3 P = poly[q], Q = poly[(q + 1) % np];
            const float dP = dot(P - Rf.x, ax) - h - tol, dQ = dot(Q - Rf.x, ax) - h - tol;   // flush edges (a lattice) kept whole
            if (dP <= 0.0f && no < 8) tmp[no++] = P;
            if ((dP < 0.0f) != (dQ < 0.0f) && no < 8) {
                const float t = dP / (dP - dQ);
                tmp[no++] = P + (Q - P) * t;
            }
        }
        np = no;
        for (int q = 0; q < np; ++q) poly[q] = tmp[q];
        if (np == 0) return 0;
    }
    int out = 0;
    for (int q = 0; q < np; ++q) {
        const float dd = h - dot(poly[q] - Rf.x, nr);
        if (dd > -margin) {
            pts[out] = poly[q];
            dep[out] = dd;
            ++out;
        }
    }
    return out;
}

// the stage: a point below the table top or the backdrop's floor, inside its sweep, behind its wall
__device__ inline bool stageContact(float3 p, bool wasAbove, const Consts& C, float3& n, float& d) {
    const bool onTable = fabsf(p.x) <= C.tableHW && p.z <= C.tableFront && p.z >= C.tableBack;
    const bool onBack = fabsf(p.x) <= C.backHW && p.z < C.tableBack;
    if (!onTable && !onBack) return false;
    const float zc = C.backWall + C.backSweep, yc = C.backSweep;
    if (onBack && p.z < zc && p.y < yc) {   // the sweep: a quarter cylinder, solid outside it
        const float dz = p.z - zc, dy = p.y - yc;
        const float r = sqrtf(dz * dz + dy * dy);
        if (r <= C.backSweep) return false;
        d = r - C.backSweep;
        n = make_float3(0.0f, -dy / r, -dz / r);
        return true;
    }
    if (onBack && p.z < C.backWall) {   // the wall
        d = C.backWall - p.z;
        n = make_float3(0, 0, 1);
        return true;
    }
    if (p.y < 0.0f && wasAbove) {   // the floor, for a cube whose middle was over it (beside the table's edge it falls)
        d = -p.y;
        n = make_float3(0, 1, 0);
        return true;
    }
    return false;
}

constexpr int kPairs = 8;   // touching pairs kept a cube a substep (the deepest)
constexpr int kPts = 4;     // points a pair (the manifold's extremes)

// the narrow phase, every awake cube at once (the colours' passes then read only these): the cubes within reach
// (a circumsphere, and the gap two can close this substep), their contact points (at most four: the manifold's
// extremes across the normal) relative to the cube, how deep, the way it is pushed. A sleeping cube touched by a
// moving one wakes.
__global__ void kNarrow(int n, const float4* x, const float4* q, const float4* v, const float4* w, const uint32_t* st, const uint32_t* idSorted,
                        const int* cellStart, const int* cellEnd, float inv, uint32_t mask, Consts C, uint32_t* pairJ, float4* pairU, float4* pairP,
                        int* pairN, uint32_t* wake) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    pairN[i] = 0;
    if (st[i] != S_AWAKE) return;
    Body I;
    I.x = xyz(x[i]);
    I.R = qrot(q[i]);
    I.v = xyz(v[i]);
    I.w = xyz(w[i]);
    const bool moving = dot(I.v, I.v) > C.sleepV * C.sleepV;
    const float reachMax = 1.7321f * C.a + C.maxMargin;
    const int3 c0 = cellOf(I.x, inv);
    uint32_t seen[27];
    int nSeen = 0, kept = 0;
    float keptDepth[kPairs];
    float3 pc[kMaxPts];
    float pd[kMaxPts];
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dxc = -1; dxc <= 1; ++dxc) {
                const uint32_t hc = hashCell(c0.x + dxc, c0.y + dy, c0.z + dz, mask);
                bool dup = false;
                for (int u = 0; u < nSeen; ++u) dup |= seen[u] == hc;
                if (dup) continue;
                seen[nSeen++] = hc;
                const int a0 = cellStart[hc];
                if (a0 < 0) continue;
                const int a1 = cellEnd[hc];
                for (int m = a0; m < a1; ++m) {
                    const uint32_t j = idSorted[m];
                    if (j == uint32_t(i)) continue;
                    const uint32_t sj = st[j];
                    if (sj == S_GONE) continue;
                    Body J;
                    J.x = xyz(x[j]);
                    const float3 dd = J.x - I.x;
                    if (dot(dd, dd) > reachMax * reachMax) continue;
                    const bool jMoves = sj == S_AWAKE;
                    J.v = jMoves ? xyz(v[j]) : make_float3(0, 0, 0);
                    J.w = jMoves ? xyz(w[j]) : make_float3(0, 0, 0);
                    // how far the two may come together this substep: their relative speed (cubes falling together
                    // are no nearer for it), turning included
                    const float3 dv = I.v - J.v;
                    const float rel = sqrtf(dot(dv, dv)) + (sqrtf(dot(I.w, I.w)) + sqrtf(dot(J.w, J.w))) * C.h * 1.7321f;
                    const float margin = fminf(C.maxMargin, rel * C.hs * 1.2f + 0.001f * C.a);
                    if (dot(dd, dd) > (1.7321f * C.a + margin) * (1.7321f * C.a + margin)) continue;
                    J.R = qrot(q[j]);
                    // the pair in a fixed order (the lower id is A): both its cubes find the same points
                    const bool iIsA = uint32_t(i) < j;
                    float3 nAB;
                    const int np = iIsA ? boxBox(I, J, C.h, C.tol, margin, nAB, pc, pd) : boxBox(J, I, C.h, C.tol, margin, nAB, pc, pd);
                    if (!np) continue;
                    if (!jMoves && moving) wake[j] = 1u;
                    const float3 un = iIsA ? nAB * -1.0f : nAB;   // I moves away from the other
                    // at most four points: the deepest and the extremes across the normal
                    int sel[kPts];
                    int ns = 0;
                    if (np <= kPts) {
                        for (int k = 0; k < np; ++k) sel[ns++] = k;
                    } else {
                        const float3 t1 = normalize(fabsf(un.x) < 0.9f ? cross(un, make_float3(1, 0, 0)) : cross(un, make_float3(0, 1, 0)));
                        const float3 t2 = cross(un, t1);
                        int ext[4] = {0, 0, 0, 0};
                        float ev[4] = {-1e30f, 1e30f, -1e30f, 1e30f};
                        for (int k = 0; k < np; ++k) {
                            const float s1 = dot(pc[k], t1), s2 = dot(pc[k], t2);
                            if (s1 > ev[0]) { ev[0] = s1; ext[0] = k; }
                            if (s1 < ev[1]) { ev[1] = s1; ext[1] = k; }
                            if (s2 > ev[2]) { ev[2] = s2; ext[2] = k; }
                            if (s2 < ev[3]) { ev[3] = s2; ext[3] = k; }
                        }
                        for (int e = 0; e < 4; ++e) {
                            bool have = false;
                            for (int u = 0; u < ns; ++u) have |= sel[u] == ext[e];
                            if (!have) sel[ns++] = ext[e];
                        }
                    }
                    float deepest = -1e30f;
                    for (int k = 0; k < ns; ++k) deepest = fmaxf(deepest, pd[sel[k]]);
                    // the slot: a free one, else the shallowest kept if this one is deeper
                    int slot = -1;
                    if (kept < kPairs) slot = kept++;
                    else {
                        int worst = 0;
                        for (int u = 1; u < kPairs; ++u)
                            if (keptDepth[u] < keptDepth[worst]) worst = u;
                        if (keptDepth[worst] < deepest) slot = worst;
                    }
                    if (slot < 0) continue;
                    keptDepth[slot] = deepest;
                    const int base = i * kPairs + slot;
                    pairJ[base] = j;
                    pairU[base] = make_float4(un.x, un.y, un.z, float(ns));
                    for (int k = 0; k < kPts; ++k) {
                        const int kk = sel[k < ns ? k : 0];
                        const float3 r = pc[kk] - I.x;
                        pairP[base * kPts + k] = make_float4(r.x, r.y, r.z, pd[kk]);
                    }
                }
            }
    pairN[i] = kept;
}

// one colour's cubes, at once: each against the stage and the cubes the narrow phase found it touching, as they are
// now (Gauss-Seidel): a point's depth now is what it was less how far the two have been pushed apart along the
// normal since (the pushes are translations: exactly)
__global__ void kSolveColour(int colour, const int* colStart, const int* colEnd, const uint32_t* idByColour, float4* x, float4* q, const float4* v,
                             const float4* w, const float4* xp, float4* dxT, float4* dtT, float4* dvT, float4* dwT, const uint32_t* st, const uint32_t* pairJ,
                             const float4* pairU, const float4* pairP, const int* pairN, Consts C, bool shock) {
    const int s0 = colStart[colour];
    if (s0 < 0) return;
    const int s1 = colEnd[colour];
    for (int t = s0 + blockIdx.x * blockDim.x + threadIdx.x; t < s1; t += gridDim.x * blockDim.x) {
        const uint32_t i = idByColour[t];
        if (st[i] != S_AWAKE) continue;
        Body I;
        I.x = xyz(x[i]);
        float4 qi = q[i];
        I.R = qrot(qi);
        I.v = xyz(v[i]);
        I.w = xyz(w[i]);
        I.dx = xyz(dxT[i]);
        I.dt = xyz(dtT[i]);
        I.dv = xyz(dvT[i]);
        I.dw = xyz(dwT[i]);
        const bool wasAbove = xp[i].y > 0.0f;
        float3 pu[kMaxPts], pc[kMaxPts];
        float pd[kMaxPts];
        // the stage, at the corners: first, and again last (it does not move: it has the last word)
        int touched = 0;
        auto stage = [&]() {
            int np = 0;
            for (int s = 0; s < 8 && np < kMaxPts; ++s) {
                const float3 lc = make_float3((s & 1) ? C.h : -C.h, (s & 2) ? C.h : -C.h, (s & 4) ? C.h : -C.h);
                const float3 c = I.x + rmul(I.R, lc);
                float3 nn;
                float d;
                if (stageContact(c, wasAbove, C, nn, d)) {
                    pu[np] = nn;
                    pc[np] = c;
                    pd[np] = d;
                    ++np;
                }
            }
            if (np) {
                float3 dx, dt, vx, vt;
                pairStep(I, I, false, np, pu, pc, pd, C.eFloor, C.muFS, C.muFD, C, dx, dt, vx, vt);
                applyTo(I, qi, dx, dt, vx, vt, C);
                touched = 1;
            }
        };
        stage();
        const int np0 = pairN[i];
        for (int sl = 0; sl < np0; ++sl) {
            const int base = int(i) * kPairs + sl;
            const uint32_t j = pairJ[base];
            const uint32_t sj = st[j];
            if (sj == S_GONE) continue;
            const float4 u4 = pairU[base];
            const float3 un = xyz(u4);
            const int np = int(u4.w);
            const bool jMoves = sj == S_AWAKE;
            Body J;
            J.x = xyz(x[j]);
            J.R = qrot(q[j]);
            if (jMoves) {
                J.v = xyz(v[j]);
                J.w = xyz(w[j]);
                J.dx = xyz(dxT[j]);
                J.dt = xyz(dtT[j]);
                J.dv = xyz(dvT[j]);
                J.dw = xyz(dwT[j]);
            } else {
                J.v = J.w = J.dx = J.dt = J.dv = J.dw = make_float3(0, 0, 0);
            }
            // the last pass propagates the support up (shock propagation, Guendelman, Bridson & Fedkiw 2003): a cube
            // under this one is a floor to it, a cube on it is not its business
            bool other = jMoves;
            if (shock && jMoves) {
                if (un.y > 0.5f) other = false;
                else if (un.y < -0.5f) continue;
            }
            const float apart = dot(I.dx - J.dx, un);   // pushed apart since the narrow phase
            for (int k = 0; k < np; ++k) {
                const float4 r4 = pairP[base * kPts + k];
                pu[k] = un;
                pc[k] = I.x + xyz(r4);   // the point moves with I
                pd[k] = r4.w - apart;
            }
            float3 dx, dt, vx, vt;
            pairStep(I, J, other, np, pu, pc, pd, C.e, C.muS, C.muD, C, dx, dt, vx, vt);
            applyTo(I, qi, dx, dt, vx, vt, C);
            touched = 1;
        }
        stage();
        x[i] = make_float4(I.x.x, I.x.y, I.x.z, 0.0f);
        q[i] = qi;
        dxT[i] = make_float4(I.dx.x, I.dx.y, I.dx.z, 0.0f);
        dtT[i] = make_float4(I.dt.x, I.dt.y, I.dt.z, 0.0f);
        dvT[i] = make_float4(I.dv.x, I.dv.y, I.dv.z, touched ? 1.0f : dvT[i].w);
        dwT[i] = make_float4(I.dw.x, I.dw.y, I.dw.z, 0.0f);
    }
}

// the substep's end: velocities (the integrated ones and what the corrections did over the substep: the same as
// (x - x_prev) / h without subtracting two nearly equal floats), sleep, leaving the stage
__global__ void kFinish(int n, float4* x, float4* v, float4* w, uint32_t* st, uint32_t* wake, float* sleepT, const float4* dvT, const float4* dwT, Consts C,
                        float hs) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    uint32_t s = st[i];
    if (s == S_ASLEEP && wake[i]) {
        s = S_AWAKE;
        st[i] = s;
        sleepT[i] = 0.0f;
    }
    wake[i] = 0u;
    if (s != S_AWAKE) return;
    const float3 p = xyz(x[i]);
    float3 vel = xyz(v[i]) + xyz(dvT[i]) * (1.0f / hs);   // the velocity's share only: an overlap pushed out gives no speed
    float3 om = xyz(w[i]) + xyz(dwT[i]) * (1.0f / hs);
    // a cube in contact barely moving: its leftover tremble damped (a pile's contacts spend it so; the solver, which
    // starts each substep afresh, would keep a few mm/s alive)
    if (dvT[i].w > 0.0f && dot(vel, vel) + dot(om, om) * C.h * C.h < 4.0f * C.sleepV * C.sleepV) {
        vel = vel * C.restDamp;
        om = om * C.restDamp;
    }
    if (p.y < -0.6f || fabsf(p.x) > 4.0f || fabsf(p.z) > 4.0f || p.y > 6.0f) {   // gone: past the table's edge, far away
        st[i] = S_GONE;
        return;
    }
    float t = sleepT[i];
    const bool slow = dot(vel, vel) < C.sleepV * C.sleepV && dot(om, om) * C.h * C.h < C.sleepV * C.sleepV;
    t = slow ? t + hs : 0.0f;
    if (t > C.sleepT) {   // asleep: slow for a while
        st[i] = S_ASLEEP;
        v[i] = make_float4(0, 0, 0, 0);
        w[i] = make_float4(0, 0, 0, 0);
        sleepT[i] = 0.0f;
        return;
    }
    sleepT[i] = t;
    v[i] = make_float4(vel.x, vel.y, vel.z, 0.0f);
    w[i] = make_float4(om.x, om.y, om.z, 0.0f);
}

// a sleeping cube's "before" is where it is (its neighbours read it as still)
__global__ void kHoldAsleep(int n, const float4* x, float4* xp, const float4* q, float4* qp, const uint32_t* st) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || st[i] != S_ASLEEP) return;
    xp[i] = x[i];
    qp[i] = q[i];
}

__global__ void kStats(int n, const float4* v, const float4* w, const uint32_t* st, float a, float m, float I, float* red) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const uint32_t s = st[i];
    unsigned* ured = reinterpret_cast<unsigned*>(red);
    if (s == S_AWAKE) {
        const float3 vv = xyz(v[i]), ww = xyz(w[i]);
        const float sp = sqrtf(dot(vv, vv)) + sqrtf(dot(ww, ww)) * 0.5f * a;
        atomicMax(&ured[0], __float_as_uint(sp));
        atomicAdd(&ured[1], 1u);
        atomicAdd(reinterpret_cast<double*>(red + 4), double(0.5f * m * dot(vv, vv) + 0.5f * I * dot(ww, ww)));
    } else if (s == S_ASLEEP) {
        atomicAdd(&ured[2], 1u);
    } else {
        atomicAdd(&ured[3], 1u);
    }
}

__global__ void kExport(int n, const float4* x, const float4* q, const float4* x0, const float4* q0, const uint32_t* st, float alpha, float a, float4* pose) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float3 p1 = xyz(x[i]), p0 = xyz(x0[i]);
    const float3 p = p0 + (p1 - p0) * alpha;
    float4 qa = q0[i], qb = q[i];
    if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0.0f) qb = make_float4(-qb.x, -qb.y, -qb.z, -qb.w);
    const float4 qq = qnorm(make_float4(qa.x + (qb.x - qa.x) * alpha, qa.y + (qb.y - qa.y) * alpha, qa.z + (qb.z - qa.z) * alpha, qa.w + (qb.w - qa.w) * alpha));
    pose[2 * i] = make_float4(p.x, p.y, p.z, st[i] == S_GONE ? 0.0f : a);
    pose[2 * i + 1] = qq;
}

__global__ void kQuantise(int n, const float4* x, const float4* q, const uint32_t* st, float3 lo, float3 inv, ushort4* pos, ushort4* rot) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float3 p = (xyz(x[i]) - lo) * inv;
    auto u16 = [](float f) { return (unsigned short)fminf(65535.0f, fmaxf(0.0f, f + 0.5f)); };
    const bool out = p.x < 0.0f || p.y < 0.0f || p.z < 0.0f || p.x > 65535.0f || p.y > 65535.0f || p.z > 65535.0f;
    pos[i] = make_ushort4(u16(p.x), u16(p.y), u16(p.z), st[i] == S_GONE || out ? 1 : 0);
    const float4 qq = q[i];
    auto s16 = [](float f) { return (unsigned short)fminf(65535.0f, fmaxf(0.0f, (f * 0.5f + 0.5f) * 65535.0f + 0.5f)); };
    rot[i] = make_ushort4(s16(qq.x), s16(qq.y), s16(qq.z), s16(qq.w));
}

__global__ void kCopy4(int n, const float4* a, float4* b) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) b[i] = a[i];
}

// ---- the blast: velocities, then their energy brought to Gurney's
struct BlastArgs {
    float3 c;
    float H;             // the whole cube's half side
    float a, rho;
    float Csr;           // the charge per steradian (kg)
    float G, inner, spread, cone, spin, floorReflect;
    unsigned seed;
};

__device__ inline float gurneyAt(float3 d, const BlastArgs& B) {
    const float m = fmaxf(fabsf(d.x), fmaxf(fabsf(d.y), fabsf(d.z)));
    const float R = B.H / fmaxf(m, 1e-6f);
    const float Msr = B.rho * R * R * R / 3.0f;
    return B.G / sqrtf(Msr / B.Csr + 0.6f);
}

__global__ void kBlast(int n, const float4* x, float4* v, float4* w, uint32_t* st, BlastArgs B, double* sums) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || st[i] == S_GONE) return;
    const float3 r = xyz(x[i]) - B.c;
    const float rl = sqrtf(dot(r, r));
    if (rl < 0.25f * B.a) {   // the middle cube (an odd count): the charge's place
        st[i] = S_GONE;
        return;
    }
    float3 d = r * (1.0f / rl);
    const float V = gurneyAt(d, B);
    const float m = fmaxf(fabsf(d.x), fmaxf(fabsf(d.y), fabsf(d.z)));
    const float R = B.H / fmaxf(m, 1e-6f);
    const float radial = powf(fmaxf(rl, 0.75f * B.a) / R, -B.inner);
    // scatter: speed log-normal, direction in a narrow cone, spin
    unsigned hh = hashU(unsigned(i) * 9781u + B.seed * 6271u);
    auto nrm = [&]() {   // a normal deviate (Box-Muller)
        const float u1 = fmaxf(hashF(hh = hashU(hh + 1u)), 1e-7f), u2 = hashF(hh = hashU(hh + 1u));
        return sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
    };
    const float sp = V * radial * expf(B.spread * nrm() - 0.5f * B.spread * B.spread);
    d = normalize(d + make_float3(nrm(), nrm(), nrm()) * B.cone);
    float3 vel = d * sp;
    float3 ax = normalize(make_float3(nrm(), nrm(), nrm()) + make_float3(0, 0, 1e-6f));
    float3 om = ax * (B.spin * sp / (0.5f * B.a) * hashF(hh = hashU(hh + 1u)));
    v[i] = make_float4(vel.x, vel.y, vel.z, 0.0f);
    w[i] = make_float4(om.x, om.y, om.z, 0.0f);
    st[i] = S_AWAKE;
    atomicAdd(&sums[0], double(V) * V);                  // Gurney's energy (over m/2)
    atomicAdd(&sums[1], double(dot(vel, vel)));          // the scattered field's
}

__global__ void kBlastScale(int n, float4* v, float4* w, const uint32_t* st, float k, float floorReflect, float3 c, float floorY) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || st[i] == S_GONE) return;
    float4 vv = v[i];
    vv.x *= k;
    vv.y *= k;
    vv.z *= k;
    if (vv.y < 0.0f) vv.y = -floorReflect * vv.y;   // the table under the charge: the downward half comes back up
    v[i] = vv;
    float4 ww = w[i];
    w[i] = make_float4(ww.x * k, ww.y * k, ww.z * k, 0.0f);
}

}   // namespace

// ------------------------------------------------------------------------------------------------- the world

void CubeWorld::destroy() {
    dropGraphs();
    for (float4** p : {&x_, &xp_, &v_, &w_, &q_, &qp_, &x0_, &q0_, &accX_, &accW_, &dvT_, &dwT_}) dfree(*p);
    for (uint32_t** p : {&colKey_, &colKeySorted_, &colId_, &idByColour_}) dfree(*p);
    dfree(colStart_);
    dfree(colEnd_);
    dfree(pairJ_);
    dfree(pairU_);
    dfree(pairP_);
    dfree(pairN_);
    dfree(state_);
    dfree(wake_);
    dfree(sleepT_);
    dfree(key_);
    dfree(keySorted_);
    dfree(id_);
    dfree(idSorted_);
    dfree(cellStart_);
    dfree(cellEnd_);
    dfree(dColour_);
    dfree(dRed_);
    if (sortTemp_) cudaFree(sortTemp_);
    sortTemp_ = nullptr;
    if (e0_) cudaEventDestroy(e0_);
    if (e1_) cudaEventDestroy(e1_);
    e0_ = e1_ = nullptr;
    if (stream_) cudaStreamDestroy(stream_);
    stream_ = nullptr;
    n_ = cap_ = 0;
}

void CubeWorld::build(int n, float side, float3 base, unsigned seed) {
    destroy();
    per_ = std::max(1, n);
    n_ = per_ * per_ * per_;
    cap_ = n_;
    side_ = side;
    a_ = side / (per_ * (1.0f + params.gap));   // side by side a gap apart, layer on layer touching
    const float pitch = a_ * (1.0f + params.gap);
    base_ = base;
    time_ = 0.0;
    CCUDA(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking));
    CCUDA(cudaEventCreate(&e0_));
    CCUDA(cudaEventCreate(&e1_));
    for (float4** p : {&x_, &xp_, &v_, &w_, &q_, &qp_, &x0_, &q0_, &accX_, &accW_, &dvT_, &dwT_}) *p = dalloc<float4>(size_t(cap_));
    for (uint32_t** p : {&colKey_, &colKeySorted_, &colId_, &idByColour_}) *p = dalloc<uint32_t>(size_t(cap_));
    colStart_ = dalloc<int>(64);
    colEnd_ = dalloc<int>(64);
    pairJ_ = dalloc<uint32_t>(size_t(cap_) * kPairs);
    pairU_ = dalloc<float4>(size_t(cap_) * kPairs);
    pairP_ = dalloc<float4>(size_t(cap_) * kPairs * kPts);
    pairN_ = dalloc<int>(size_t(cap_));
    state_ = dalloc<uint32_t>(size_t(cap_));
    wake_ = dalloc<uint32_t>(size_t(cap_));
    sleepT_ = dalloc<float>(size_t(cap_));
    key_ = dalloc<uint32_t>(size_t(cap_));
    keySorted_ = dalloc<uint32_t>(size_t(cap_));
    id_ = dalloc<uint32_t>(size_t(cap_));
    idSorted_ = dalloc<uint32_t>(size_t(cap_));
    tableBits_ = 10;
    while ((1 << tableBits_) < 2 * cap_ && tableBits_ < 26) ++tableBits_;
    cellStart_ = dalloc<int>(size_t(1) << tableBits_);
    cellEnd_ = dalloc<int>(size_t(1) << tableBits_);
    sortTempBytes_ = 0;
    cub::DeviceRadixSort::SortPairs(nullptr, sortTempBytes_, key_, keySorted_, id_, idSorted_, cap_, 0, tableBits_);
    size_t colBytes = 0;   // the colours' sort shares the scratch
    cub::DeviceRadixSort::SortPairs(nullptr, colBytes, key_, keySorted_, id_, idSorted_, cap_, 0, 6);
    sortTempBytes_ = std::max(sortTempBytes_, colBytes);
    CCUDA(cudaMalloc(&sortTemp_, std::max<size_t>(sortTempBytes_, 16)));
    dColour_ = dalloc<uchar4>(size_t(cap_));
    dRed_ = dalloc<float>(8);
    // the lattice, at rest, asleep; a colour each from a puzzle cube's six
    std::vector<float4> x(static_cast<size_t>(n_)), q(size_t(n_), make_float4(0, 0, 0, 1)), z(size_t(n_), make_float4(0, 0, 0, 0));
    std::vector<uchar4> col(static_cast<size_t>(n_));
    const uchar4 pal[6] = {{238, 238, 232, 255}, {255, 205, 0, 255}, {190, 18, 40, 255}, {255, 92, 0, 255}, {0, 72, 178, 255}, {0, 150, 70, 255}};
    unsigned rs = seed * 2654435761u + 12345u;
    for (int k = 0; k < per_; ++k)
        for (int j = 0; j < per_; ++j)
            for (int i = 0; i < per_; ++i) {
                const size_t id = (size_t(k) * per_ + j) * per_ + i;
                x[id] = make_float4(base.x - 0.5f * side + pitch * (i + 0.5f), base.y + a_ * (j + 0.5f), base.z - 0.5f * side + pitch * (k + 0.5f), 0.0f);
                rs = rs * 1664525u + 1013904223u;
                col[id] = pal[(rs >> 8) % 6u];
            }
    std::vector<uint32_t> st(size_t(n_), S_ASLEEP);
    std::vector<float> zero(size_t(n_), 0.0f);
    for (float4* p : {x_, xp_, x0_}) CCUDA(cudaMemcpy(p, x.data(), x.size() * sizeof(float4), cudaMemcpyHostToDevice));
    for (float4* p : {q_, qp_, q0_}) CCUDA(cudaMemcpy(p, q.data(), q.size() * sizeof(float4), cudaMemcpyHostToDevice));
    for (float4* p : {v_, w_}) CCUDA(cudaMemcpy(p, z.data(), z.size() * sizeof(float4), cudaMemcpyHostToDevice));
    CCUDA(cudaMemcpy(state_, st.data(), st.size() * sizeof(uint32_t), cudaMemcpyHostToDevice));
    CCUDA(cudaMemset(wake_, 0, size_t(n_) * sizeof(uint32_t)));
    CCUDA(cudaMemcpy(sleepT_, zero.data(), zero.size() * sizeof(float), cudaMemcpyHostToDevice));
    CCUDA(cudaMemcpy(dColour_, col.data(), col.size() * sizeof(uchar4), cudaMemcpyHostToDevice));
}

namespace {
Consts makeConsts(const CubeParams& P, float a, float hs) {
    Consts C;
    C.a = a;
    C.h = 0.5f * a;
    const float m = P.density * a * a * a;
    C.invM = 1.0f / m;
    C.invI = 6.0f / (m * a * a);
    C.g = P.gravity;
    C.kDrag = 0.5f * P.airDensity * P.drag * 1.5f * a * a / m;
    C.angDamp = P.angularDamping;
    C.e = P.restitution;
    C.eFloor = P.restitutionFloor;
    C.muS = P.muStatic;
    C.muD = P.muDynamic;
    C.muFS = P.muFloorStatic;
    C.muFD = P.muFloorDynamic;
    C.vRest = std::max(2.0f * P.gravity * hs, P.restSpeed);
    C.sor = P.sor;
    C.maxMargin = 1.2f * a;   // within the neighbours' cells
    C.restDamp = std::pow(1.0f - P.restDamping, hs * 1000.0f);   // per millisecond
    C.debug = P.debug;
    C.maxPush = P.maxPush * hs;
    C.hs = hs;
    C.tol = 1e-4f * a;
    C.sleepV = P.sleepSpeed;
    C.sleepT = P.sleepTime;
    C.tableHW = P.tableHalfWidth;
    C.tableFront = P.tableFront;
    C.tableBack = P.tableBack;
    C.backHW = P.backHalfWidth;
    C.backWall = P.backWall;
    C.backSweep = P.backSweep;
    return C;
}
}   // namespace

void CubeWorld::dropGraphs() {
    for (Graph& g : graphs_) cudaGraphExecDestroy(g.exec);
    graphs_.clear();
}

void CubeWorld::substep(float hs) {
    if (std::memcmp(&graphParams_, &params, sizeof(CubeParams)) != 0) {   // the settings changed: capture afresh
        dropGraphs();
        std::memcpy(&graphParams_, &params, sizeof(CubeParams));
    }
    for (const Graph& g : graphs_)
        if (g.hs == hs) {
            CCUDA(cudaGraphLaunch(g.exec, stream_));
            return;
        }
    if (graphs_.size() >= 8) {
        cudaGraphExecDestroy(graphs_.front().exec);
        graphs_.erase(graphs_.begin());
    }
    cudaGraph_t gr;
    CCUDA(cudaStreamBeginCapture(stream_, cudaStreamCaptureModeThreadLocal));
    record(hs);
    CCUDA(cudaStreamEndCapture(stream_, &gr));
    Graph g;
    g.hs = hs;
    CCUDA(cudaGraphInstantiate(&g.exec, gr, 0));
    cudaGraphDestroy(gr);
    graphs_.push_back(g);
    CCUDA(cudaGraphLaunch(g.exec, stream_));
}

void CubeWorld::record(float hs) {
    Consts C = makeConsts(params, a_, hs);
    const int B = nb(n_);
    kIntegrate<<<B, kB, 0, stream_>>>(n_, x_, xp_, v_, w_, q_, qp_, state_, accX_, accW_, dvT_, dwT_, C, hs);
    // the neighbours' grid (cells a circumsphere wide), and the colours
    const float cell = a_ * 1.7321f * 1.0005f;
    const float inv = 1.0f / cell;
    const uint32_t mask = (1u << tableBits_) - 1u;
    kKeys<<<B, kB, 0, stream_>>>(n_, x_, key_, id_, inv, mask);
    cub::DeviceRadixSort::SortPairs(sortTemp_, sortTempBytes_, key_, keySorted_, id_, idSorted_, n_, 0, tableBits_, stream_);
    CCUDA(cudaMemsetAsync(cellStart_, 0xff, (size_t(1) << tableBits_) * sizeof(int), stream_));
    kRanges<<<B, kB, 0, stream_>>>(n_, keySorted_, cellStart_, cellEnd_);
    kColours<<<B, kB, 0, stream_>>>(n_, x_, colKey_, colId_, 1.7321f / (a_ * 1.0005f));
    cub::DeviceRadixSort::SortPairs(sortTemp_, sortTempBytes_, colKey_, colKeySorted_, colId_, idByColour_, n_, 0, 6, stream_);
    CCUDA(cudaMemsetAsync(colStart_, 0xff, 64 * sizeof(int), stream_));
    kRanges<<<B, kB, 0, stream_>>>(n_, colKeySorted_, colStart_, colEnd_);
    // the narrow phase at once, then Gauss-Seidel by colour over what it found (a colour's cubes never touch)
    kNarrow<<<B, kB, 0, stream_>>>(n_, x_, q_, v_, w_, state_, idSorted_, cellStart_, cellEnd_, inv, mask, C, pairJ_, pairU_, pairP_, pairN_, wake_);
    const int G = std::max(1, nb(n_ / 48 + 1));
    for (int pass = 0; pass < std::max(1, params.passes); ++pass)
        for (int c = 0; c < 64; ++c)
            kSolveColour<<<G, kB, 0, stream_>>>(c, colStart_, colEnd_, idByColour_, x_, q_, v_, w_, xp_, accX_, accW_, dvT_, dwT_, state_, pairJ_, pairU_, pairP_,
                                                pairN_, C, pass == std::max(1, params.passes) - 1 && params.shock);
    kFinish<<<B, kB, 0, stream_>>>(n_, x_, v_, w_, state_, wake_, sleepT_, dvT_, dwT_, C, hs);
}

CubeStats CubeWorld::stats() const {
    CubeStats s;
    if (!n_) return s;
    CCUDA(cudaMemsetAsync(dRed_, 0, 8 * sizeof(float), stream_));
    const float m = params.density * a_ * a_ * a_;
    kStats<<<nb(n_), kB, 0, stream_>>>(n_, v_, w_, state_, a_, m, m * a_ * a_ / 6.0f, dRed_);
    float r[8];
    CCUDA(cudaMemcpyAsync(r, dRed_, sizeof(r), cudaMemcpyDeviceToHost, stream_));
    CCUDA(cudaStreamSynchronize(stream_));
    unsigned u[4];
    std::memcpy(u, r, sizeof(u));
    std::memcpy(&s.maxSpeed, &u[0], sizeof(float));
    s.awake = int(u[1]);
    s.asleep = int(u[2]);
    s.dead = int(u[3]);
    double ke;
    std::memcpy(&ke, r + 4, sizeof(double));
    s.kinetic = ke;
    return s;
}

void CubeWorld::step(float dt) {
    if (!n_) return;
    CCUDA(cudaEventRecord(e0_, stream_));
    // the drawing interpolates from here
    kCopy4<<<nb(n_), kB, 0, stream_>>>(n_, x_, x0_);
    kCopy4<<<nb(n_), kB, 0, stream_>>>(n_, q_, q0_);
    // whole substeps near the set length, exactly dt together
    const double h0 = 1.0 / (60.0 * baseSubsteps());
    const int base = std::max(1, int(std::lround(dt / h0)));
    const double hb = double(dt) / base;
    const CubeStats s = stats();
    lastStats_ = s;
    lastSubs_ = 0;
    if (s.awake > 0) {
        // a fast cube may move at most half its edge a substep
        const int cut = std::max(1, std::min(params.maxSubdivide, int(std::ceil(s.maxSpeed * hb / (0.5 * a_)))));
        const float hs = float(hb / cut);
        for (int k = 0; k < base * cut; ++k) substep(hs);
        lastSubs_ = base * cut;
    }
    time_ += dt;
    CCUDA(cudaEventRecord(e1_, stream_));
    CCUDA(cudaEventSynchronize(e1_));
    CCUDA(cudaEventElapsedTime(&lastMs_, e0_, e1_));
}

void CubeWorld::blast(const CubeBlast& b, CubeBlastReport* rep) {
    if (!n_) return;
    BlastArgs A;
    A.c = b.centre;
    A.H = 0.5f * side_;
    A.a = a_;
    A.rho = params.density;
    A.Csr = b.charge * 1e-3f / (4.0f * 3.14159265f);
    A.G = b.gurney;
    A.inner = b.inner;
    A.spread = b.spread;
    A.cone = b.cone;
    A.spin = b.spin;
    A.floorReflect = b.floorReflect;
    A.seed = b.seed;
    double* sums = nullptr;
    CCUDA(cudaMalloc(reinterpret_cast<void**>(&sums), 2 * sizeof(double)));
    CCUDA(cudaMemsetAsync(sums, 0, 2 * sizeof(double), stream_));
    kBlast<<<nb(n_), kB, 0, stream_>>>(n_, x_, v_, w_, state_, A, sums);
    double h[2];
    CCUDA(cudaMemcpyAsync(h, sums, sizeof(h), cudaMemcpyDeviceToHost, stream_));
    CCUDA(cudaStreamSynchronize(stream_));
    // the scattered field carries Gurney's energy (along each direction the mass of its cone moves at its speed)
    const float k = h[1] > 0.0 ? float(std::sqrt(h[0] / h[1])) : 1.0f;
    kBlastScale<<<nb(n_), kB, 0, stream_>>>(n_, v_, w_, state_, k, b.floorReflect, b.centre, 0.0f);
    CCUDA(cudaMemsetAsync(wake_, 0, size_t(n_) * sizeof(uint32_t), stream_));
    CCUDA(cudaMemsetAsync(sleepT_, 0, size_t(n_) * sizeof(float), stream_));
    cudaFree(sums);
    if (rep) {
        const float m = params.density * a_ * a_ * a_;
        auto g = [&](float3 d) {
            const float mm = fmaxf(fabsf(d.x), fmaxf(fabsf(d.y), fabsf(d.z)));
            const float R = A.H / fmaxf(mm, 1e-6f);
            return A.G / std::sqrt(A.rho * R * R * R / 3.0f / A.Csr + 0.6f);
        };
        rep->gurneyEnergy = 0.5 * m * h[0];
        rep->faceSpeed = g(make_float3(1, 0, 0));
        rep->cornerSpeed = g(normalize(make_float3(1, 1, 1)));
        const CubeStats s = stats();
        rep->energy = s.kinetic;
        rep->maxSpeed = s.maxSpeed;
        rep->meanSpeed = s.awake > 0 ? float(std::sqrt(2.0 * s.kinetic / (m * s.awake))) : 0.0f;
    }
}

void CubeWorld::exportPoses(float4* pose, float alpha, cudaStream_t st) const {
    if (!n_) return;
    CCUDA(cudaStreamSynchronize(stream_));
    kExport<<<nb(n_), kB, 0, st>>>(n_, x_, q_, x0_, q0_, state_, std::min(1.0f, std::max(0.0f, alpha)), a_, pose);
}

void CubeWorld::quantise(ushort4* pos, ushort4* rot, float3 lo, float3 scale, cudaStream_t st) const {
    if (!n_) return;
    CCUDA(cudaStreamSynchronize(stream_));
    kQuantise<<<nb(n_), kB, 0, st>>>(n_, x_, q_, state_, lo, make_float3(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z), pos, rot);
}

namespace {
__global__ void kCubeDequantise(const ushort4* p0, const ushort4* r0, const ushort4* p1, const ushort4* r1, float a, int n, float3 lo, float3 scale, float edge,
                                float4* pose) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const ushort4 A = p0[i], B = p1 ? p1[i] : A;
    const ushort4 ra = r0[i], rb = r1 ? r1[i] : ra;
    const float3 pa = make_float3(lo.x + A.x * scale.x, lo.y + A.y * scale.y, lo.z + A.z * scale.z);
    const float3 pb = make_float3(lo.x + B.x * scale.x, lo.y + B.y * scale.y, lo.z + B.z * scale.z);
    const float3 p = pa + (pb - pa) * a;
    auto sn = [](unsigned short u) { return u / 65535.0f * 2.0f - 1.0f; };
    float4 qa = make_float4(sn(ra.x), sn(ra.y), sn(ra.z), sn(ra.w)), qb = make_float4(sn(rb.x), sn(rb.y), sn(rb.z), sn(rb.w));
    if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0.0f) qb = make_float4(-qb.x, -qb.y, -qb.z, -qb.w);
    const float4 q = qnorm(make_float4(qa.x + (qb.x - qa.x) * a, qa.y + (qb.y - qa.y) * a, qa.z + (qb.z - qa.z) * a, qa.w + (qb.w - qa.w) * a));
    const bool gone = A.w != 0 || (a > 0.5f && B.w != 0);
    pose[2 * i] = make_float4(p.x, p.y, p.z, gone ? 0.0f : edge);
    pose[2 * i + 1] = q;
}
}   // namespace

void launchCubeDequantise(const ushort4* p0, const ushort4* r0, const ushort4* p1, const ushort4* r1, float a, int n, float3 lo, float3 scale, float edge,
                          float4* pose, cudaStream_t st) {
    if (n > 0) kCubeDequantise<<<nb(n), kB, 0, st>>>(p0, r0, p1, r1, a, n, lo, scale, edge, pose);
}

void CubeWorld::wakeAll(float3 v, float3 w) {
    if (!n_) return;
    CCUDA(cudaStreamSynchronize(stream_));
    std::vector<float4> vv(size_t(n_), make_float4(v.x, v.y, v.z, 0.0f)), ww(size_t(n_), make_float4(w.x, w.y, w.z, 0.0f));
    std::vector<uint32_t> st(static_cast<size_t>(n_));
    CCUDA(cudaMemcpy(st.data(), state_, st.size() * sizeof(uint32_t), cudaMemcpyDeviceToHost));
    for (uint32_t& t : st)
        if (t == S_ASLEEP) t = S_AWAKE;
    CCUDA(cudaMemcpy(state_, st.data(), st.size() * sizeof(uint32_t), cudaMemcpyHostToDevice));
    CCUDA(cudaMemcpy(v_, vv.data(), vv.size() * sizeof(float4), cudaMemcpyHostToDevice));
    CCUDA(cudaMemcpy(w_, ww.data(), ww.size() * sizeof(float4), cudaMemcpyHostToDevice));
    CCUDA(cudaMemset(sleepT_, 0, size_t(n_) * sizeof(float)));
}

void CubeWorld::setPose(int i, float3 x, float4 q) {
    if (i < 0 || i >= n_) return;
    CCUDA(cudaStreamSynchronize(stream_));
    const float4 xx = make_float4(x.x, x.y, x.z, 0.0f), qq = qnorm(q);
    for (float4* p : {x_, xp_, x0_}) CCUDA(cudaMemcpy(p + i, &xx, sizeof(float4), cudaMemcpyHostToDevice));
    for (float4* p : {q_, qp_, q0_}) CCUDA(cudaMemcpy(p + i, &qq, sizeof(float4), cudaMemcpyHostToDevice));
}

void CubeWorld::read(std::vector<float4>& x, std::vector<float4>& q, std::vector<float4>& v, std::vector<uint32_t>& state) const {
    CCUDA(cudaStreamSynchronize(stream_));
    x.resize(size_t(n_));
    q.resize(size_t(n_));
    v.resize(size_t(n_));
    state.resize(size_t(n_));
    if (!n_) return;
    CCUDA(cudaMemcpy(x.data(), x_, x.size() * sizeof(float4), cudaMemcpyDeviceToHost));
    CCUDA(cudaMemcpy(q.data(), q_, q.size() * sizeof(float4), cudaMemcpyDeviceToHost));
    CCUDA(cudaMemcpy(v.data(), v_, v.size() * sizeof(float4), cudaMemcpyDeviceToHost));
    CCUDA(cudaMemcpy(state.data(), state_, state.size() * sizeof(uint32_t), cudaMemcpyDeviceToHost));
}

void CubeWorld::pairStats(int& cubesTouching, int& pairs) const {
    std::vector<int> pn(static_cast<size_t>(n_));
    CCUDA(cudaStreamSynchronize(stream_));
    if (n_) CCUDA(cudaMemcpy(pn.data(), pairN_, pn.size() * sizeof(int), cudaMemcpyDeviceToHost));
    cubesTouching = pairs = 0;
    for (int v : pn) {
        cubesTouching += v > 0;
        pairs += v;
    }
}

std::string CubeWorld::summary() const {
    char b[200];
    std::snprintf(b, sizeof(b), "kup: %d x %d x %d = %d kup, kenar %.2f mm, buyuk kup %.0f mm", per_, per_, per_, n_, a_ * 1000.0f, side_ * 1000.0f);
    return b;
}
