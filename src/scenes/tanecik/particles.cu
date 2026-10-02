// Scene 4 "Tanecik": the particle world on the GPU (see particles.h for the model).
//
// Density units: every particle stands for one spacing^3 of matter, so a fluid particle's density is the kernel sum
// over its neighbours (solids included: they fill space too), and the rest density S0 is that sum over a full cubic
// lattice. C_i = sum_j W_ij / S0 - 1; grad_j C_i = gradW(x_j - x_i) / S0. With unequal masses the multiplier is
//   lambda_i = -C_i / (sum_k (w_k / w_f) |grad_k C_i|^2 + eps),   dx_k = (w_k / w_f) lambda_i grad_k C_i
// (w_f a water particle's inverse mass), which keeps momentum: a heavy solid moves less than the water it holds.
// Each particle gathers its own correction: a fluid particle from its constraint and its fluid neighbours', a solid
// one from the constraints of the fluid around it.
#include "scenes/tanecik/particles.h"

#include <cub/device/device_radix_sort.cuh>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>

#define PCUDA(x)                                                                                        \
    do {                                                                                                \
        cudaError_t e_ = (x);                                                                           \
        if (e_ != cudaSuccess) {                                                                        \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                               \
        }                                                                                               \
    } while (0)

namespace {

constexpr int kMaxSubsteps = 256;
constexpr uint32_t PF_FLUID = 1u << 31;
constexpr uint32_t PF_GROUP = 0x00ffffffu;
constexpr int kMaxNbr = 96;
constexpr int kBlock = 256;
constexpr float kWaterDensity = 1000.0f;

inline int blocks(int n) { return (n + kBlock - 1) / kBlock; }

template <class T> T* dalloc(size_t n) {
    void* p = nullptr;
    PCUDA(cudaMalloc(&p, std::max<size_t>(n, 1) * sizeof(T)));
    PCUDA(cudaMemset(p, 0, std::max<size_t>(n, 1) * sizeof(T)));
    return static_cast<T*>(p);
}
template <class T> void dfree(T*& p) {
    if (p) cudaFree(p);
    p = nullptr;
}

// the kernels: poly6 for density, spiky for its gradient (Mueller et al. 2003)
struct Kern {
    float h, h2, poly6, spiky;   // poly6 = 315 / (64 pi h^9), spiky = -45 / (pi h^6)
    float cohesion;              // Akinci's spline: 32 / (pi h^9), normalised by its peak (see cohesionSpline)
};
__host__ __device__ inline float W(const Kern& k, float r2) {
    const float d = k.h2 - r2;
    return d > 0.0f ? k.poly6 * d * d * d : 0.0f;
}
__host__ __device__ inline float3 gradW(const Kern& k, float3 r, float d) {   // grad wrt the first point of r = a - b
    if (d <= 1e-9f || d >= k.h) return make_float3(0, 0, 0);
    const float t = k.h - d;
    return r * (k.spiky * t * t / d);
}
// Akinci et al. 2013 cohesion spline, without its constant (the caller scales by the spline's peak)
__host__ __device__ inline float cohesionSpline(float r, float h) {
    if (r <= 0.0f || r >= h) return 0.0f;
    const float a = (h - r) * (h - r) * (h - r) * r * r * r;
    return 2.0f * r > h ? a : 2.0f * a - h * h * h * h * h * h / 64.0f;
}

__host__ __device__ inline uint32_t hashCell(int x, int y, int z, uint32_t mask) {
    return (uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u) & mask;
}
__host__ __device__ inline unsigned long long packCell(int x, int y, int z) {
    return (unsigned long long)(x + (1 << 20)) | ((unsigned long long)(y + (1 << 20)) << 21) | ((unsigned long long)(z + (1 << 20)) << 42);
}
__device__ inline int3 cellOf(float3 p, float invH) { return make_int3(int(floorf(p.x * invH)), int(floorf(p.y * invH)), int(floorf(p.z * invH))); }
__device__ inline float3 xyz(float4 a) { return make_float3(a.x, a.y, a.z); }

// ---- the substep

__global__ void kPredict(int n, float4* x, float4* x0, float4* v, const float* w, const float* gSub, float dt) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float g = *gSub;
    float4 vi = v[i];
    const float4 xi = x[i];
    x0[i] = xi;
    if (w[i] > 0.0f) vi.y -= g * dt;
    else vi = make_float4(0, 0, 0, 0);
    v[i] = vi;
    x[i] = make_float4(xi.x + vi.x * dt, xi.y + vi.y * dt, xi.z + vi.z * dt, 0.0f);
}

__global__ void kKeys(int n, const float4* x, uint32_t* key, uint32_t* id, float invH, uint32_t mask) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const int3 c = cellOf(xyz(x[i]), invH);
    key[i] = hashCell(c.x, c.y, c.z, mask);
    id[i] = uint32_t(i);
}

__global__ void kReorder(int n, const uint32_t* idSorted, const float4* x, const float* w, const uint32_t* phase, const float* psi, float4* xs, uint32_t* phs,
                         float* psis, unsigned long long* code, float invH) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    const uint32_t i = idSorted[k];
    const float4 p = x[i];
    xs[k] = make_float4(p.x, p.y, p.z, w[i]);
    if (phs) phs[k] = phase[i];
    if (psis) psis[k] = psi[i];
    if (code) {
        const int3 c = cellOf(xyz(p), invH);
        code[k] = packCell(c.x, c.y, c.z);
    }
}

__global__ void kCellRanges(int n, const uint32_t* keySorted, int* start, int* end) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    const uint32_t c = keySorted[k];
    if (k == 0 || keySorted[k - 1] != c) start[c] = k;
    if (k == n - 1 || keySorted[k + 1] != c) end[c] = k + 1;
}

// every particle's neighbours within h, in a fixed order (cells in order, slots ascending)
// flags (per slot): NB_FLUID some fluid within h, NB_OTHER a solid of another group within h (a possible contact). A
// solid particle with neither (the inside of a bone, of a cube) skips the density and contact loops.
// Sleeping (SleepArgs): a bonded particle that still has every bond it started with, in a group where that is the most anyone has
// (the inside of a body part, shielded by its outer layers), skips its own search too, unless last substep someone of
// another group, or fluid, found it within h (mark). Both sides of any contact or coupling then search: the marks see
// to it one substep ahead (h - 2r of approach a substep is ~7 m/s at 20 substeps).
constexpr uint8_t NB_FLUID = 1, NB_OTHER = 2;
struct SleepArgs {
    const uint32_t* idSorted;
    const uint8_t* ok;            // per particle: may sleep (full bonds at commit)
    const int* degree;            // live bonds now
    const int* initial;           // bonds at commit
    const uint8_t* markPrev;      // found by another group or fluid last substep
    uint8_t* markNext;
};
__global__ void kNeighbours(int n, int stride, const float4* xs, const uint32_t* phs, const unsigned long long* code, const int* start, const int* end,
                            float h2, float invH, uint32_t mask, int* nbr, int* nbrCount, uint8_t* flags, float* stat, SleepArgs sl) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    const uint32_t pk = phs[k];
    {
        const uint32_t i = sl.idSorted[k];
        if (!(pk & PF_FLUID) && sl.ok[i] && sl.degree[i] == sl.initial[i] && !sl.markPrev[i]) {
            nbrCount[k] = 0;
            flags[k] = 0;
            return;
        }
    }
    const float3 p = xyz(xs[k]);
    const int3 c = cellOf(p, invH);
    int cnt = 0;
    uint8_t fl = 0;
    bool overflow = false;
    int* out = nbr + k;   // column-major: neighbour q of slot k at q * stride + k (a warp's reads are coalesced)
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = c.x + dx, cy = c.y + dy, cz = c.z + dz;
                const uint32_t b = hashCell(cx, cy, cz, mask);
                const int s = start[b];
                if (s < 0) continue;
                const int e = end[b];
                const unsigned long long want = packCell(cx, cy, cz);
                for (int j = s; j < e; ++j) {
                    if (j == k || code[j] != want) continue;
                    const float3 r = p - xyz(xs[j]);
                    if (dot(r, r) >= h2) continue;
                    if (cnt < kMaxNbr) out[size_t(cnt++) * stride] = j;
                    else overflow = true;
                    const uint32_t pj = phs[j];
                    if (pj & PF_FLUID) fl |= NB_FLUID;
                    else if ((pj & PF_GROUP) != (pk & PF_GROUP) || (pk & PF_FLUID)) fl |= NB_OTHER;
                    // a solid found by fluid or by another group must search next substep (the same value from every writer)
                    if (!(pj & PF_FLUID) && ((pk & PF_FLUID) || (pj & PF_GROUP) != (pk & PF_GROUP))) sl.markNext[sl.idSorted[j]] = 1;
                }
            }
    nbrCount[k] = cnt;
    flags[k] = fl;
    if (overflow) atomicAdd(&stat[4], 1.0f);
}

// the walls (the ground and the box's sides) as resting particles: their kernel sum and gradient at a distance d from
// the wall, tabulated at commit. A fluid particle beside a wall then has a full neighbourhood.
constexpr int kWallTable = 256;
struct Walls {
    const float2* table;        // x: kernel sum, y: gradient magnitude (towards the wall), at d = k / (kWallTable - 1) * h
    float h;
    bool box;
    float3 lo, hi;
};
__device__ inline void wallOne(const Walls& w, float d, float3 n, float& sum, float3& g) {
    if (d >= w.h) return;
    const float u = fmaxf(d, 0.0f) / w.h * (kWallTable - 1);
    const int i = min(int(u), kWallTable - 2);
    const float f = u - i;
    const float2 a = w.table[i], b = w.table[i + 1];
    sum += a.x + (b.x - a.x) * f;
    g += n * -(a.y + (b.y - a.y) * f);   // towards the wall: against its inward normal
}
__device__ inline void wallDensity(const Walls& w, float3 p, float& sum, float3& g) {
    sum = 0.0f;
    g = make_float3(0, 0, 0);
    if (!w.table) return;
    wallOne(w, p.y, make_float3(0, 1, 0), sum, g);
    if (w.box) {
        wallOne(w, p.x - w.lo.x, make_float3(1, 0, 0), sum, g);
        wallOne(w, w.hi.x - p.x, make_float3(-1, 0, 0), sum, g);
        wallOne(w, p.z - w.lo.z, make_float3(0, 0, 1), sum, g);
        wallOne(w, w.hi.z - p.z, make_float3(0, 0, -1), sum, g);
    }
}

struct DensityArgs {
    Walls walls;
    Kern K;
    float S0, eps, wf, scorrK, scorrN, scorrWdq;
    bool unilateral, on;
};

// the density multiplier of every fluid particle (sorted order); its density for the stats
__global__ void kLambda(int n, int stride, const float4* xs, const uint32_t* phs, const float* psis, const int* nbr, const int* nbrCount,
                        const uint32_t* idSorted, float* lam, float* rho, float* rhos, DensityArgs a) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    if (!(phs[k] & PF_FLUID) || !a.on) {
        lam[k] = 0.0f;
        rhos[k] = 1.0f;
        if (!(phs[k] & PF_FLUID)) rho[idSorted[k]] = 0.0f;
        return;
    }
    const float3 p = xyz(xs[k]);
    float sum = W(a.K, 0.0f);
    float3 gi = make_float3(0, 0, 0);
    float g2 = 0.0f;
    const int m = nbrCount[k];
    for (int q = 0; q < m; ++q) {
        const int j = nbr[size_t(q) * stride + k];
        const float4 xj = xs[j];
        const float3 r = p - xyz(xj);
        const float r2 = dot(r, r);
        const bool fj = (phs[j] & PF_FLUID) != 0;
        const float pj = fj ? 1.0f : psis[j];
        sum += pj * W(a.K, r2);
        const float3 g = gradW(a.K, r, sqrtf(r2)) * (pj / a.S0);
        gi += g;
        const float ratio = fj ? 1.0f : xj.w / a.wf;
        g2 += ratio * dot(g, g);
    }
    {   // the walls, as if filled with resting particles (they cannot move: no term in the denominator)
        float wsum;
        float3 wg;
        wallDensity(a.walls, p, wsum, wg);
        sum += wsum;
        gi += wg * (1.0f / a.S0);
    }
    float C = sum / a.S0 - 1.0f;
    rho[idSorted[k]] = rhos[k] = sum / a.S0;
    if (a.unilateral) C = fmaxf(C, 0.0f);
    lam[k] = -C / (dot(gi, gi) + g2 + a.eps);
}

struct DeltaArgs {
    Walls walls;
    Kern K;
    float S0, wf, scorrK, scorrN, scorrWdq, D0;
    float spacing;              // contact distance
    float muS, muD, fluidSolidMu;
    bool density;
    float sor;                  // the density correction's scale (successive over/under-relaxation)
};

// every particle's correction: density (its own constraint and its fluid neighbours') and contacts with the solids
// of other groups, shared in Jacobi fashion so that a pair's two ends get equal and opposite impulses
__global__ void kDelta(int n, int stride, const float4* xs, const uint32_t* phs, const float* psis, const uint8_t* flags, const uint32_t* idSorted, const int* nbr,
                       const int* nbrCount, const float* lam,
                       const float4* x0, const uint8_t* contacts, uint8_t* contactsNext, float4* dx, DeltaArgs a) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    const uint32_t i = idSorted[k];
    const float4 xi4 = xs[k];
    const float3 p = xyz(xi4);
    const float wi = xi4.w;
    const uint32_t ph = phs[k];
    const bool fi = (ph & PF_FLUID) != 0;
    const uint32_t gi = ph & PF_GROUP;
    float3 acc = make_float3(0, 0, 0);
    if (wi <= 0.0f || (!fi && !flags[k])) {   // kinematic, or a solid inside its own group: nothing to do
        dx[i] = make_float4(0, 0, 0, 0);
        contactsNext[i] = 0;
        return;
    }
    const float3 mi = make_float3(x0[i].x, x0[i].y, x0[i].z);
    const float3 movedI = p - mi;
    const float lamI = lam[k];
    float3 accD = make_float3(0, 0, 0);
    const int m = nbrCount[k];
    const int ci = contacts[i];
    int nc = 0;
    if (fi && a.density) {   // the walls push back on a compressed particle (its own constraint)
        float wsum;
        float3 wg;
        wallDensity(a.walls, p, wsum, wg);
        accD += wg * (lamI / a.S0);
    }
    for (int q = 0; q < m; ++q) {
        const int j = nbr[size_t(q) * stride + k];
        const float4 xj4 = xs[j];
        const float3 r = p - xyz(xj4);
        const float d = sqrtf(dot(r, r));
        const uint32_t pj = phs[j];
        const bool fj = (pj & PF_FLUID) != 0;
        if (a.density) {
            if (fi) {
                float l = lamI;
                if (fj) {
                    l += lam[j];
                    if (a.scorrK > 0.0f) l += -a.scorrK * powf(W(a.K, d * d) / a.scorrWdq, a.scorrN) / a.D0;   // in the multipliers' units (lambda ~ C / D0)
                } else {
                    l *= psis[j];
                }
                accD += gradW(a.K, r, d) * (l / a.S0);
            } else if (fj) {
                accD += gradW(a.K, r, d) * ((wi / a.wf) * lam[j] * psis[k] / a.S0);
            }
        }
        // contact: at least one solid, from different groups
        if ((fi && fj) || (!fi && !fj && (pj & PF_GROUP) == gi)) continue;
        if (d >= a.spacing || d <= 1e-9f) continue;
        const float wj = xj4.w;
        const float ws = wi + wj;
        if (ws <= 0.0f) continue;
        const uint32_t idj = idSorted[j];
        const int share = max(1, max(ci, int(contacts[idj])));
        const float pen = a.spacing - d;
        const float3 nrm = r * (1.0f / d);
        const float wr = wi / ws / float(share);
        acc += nrm * (pen * wr);
        ++nc;
        // friction: the pair's relative sliding over this substep
        const float mu_s = (fi || fj) ? a.fluidSolidMu : a.muS, mu_d = (fi || fj) ? a.fluidSolidMu : a.muD;
        if (mu_s > 0.0f) {
            const float3 mj = make_float3(x0[idj].x, x0[idj].y, x0[idj].z);
            const float3 rel = movedI - (xyz(xj4) - mj);
            const float3 t = rel - nrm * dot(rel, nrm);
            const float tl = sqrtf(dot(t, t));
            if (tl > 1e-12f) {
                const float f = tl < mu_s * pen ? 1.0f : fminf(mu_d * pen / tl, 1.0f);
                acc += t * (-f * wr);
            }
        }
    }
    acc += accD * a.sor;
    dx[i] = make_float4(acc.x, acc.y, acc.z, 0.0f);
    contactsNext[i] = uint8_t(min(nc, 255));
}

__global__ void kApply(int n, float4* x, const float4* dx) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float4 d = dx[i];
    float4 p = x[i];
    p.x += d.x;
    p.y += d.y;
    p.z += d.z;
    x[i] = p;
}

// one colour of bonds: no two share a particle, so each moves its two ends directly (XPBD)
__global__ void kBonds(int first, int count, const int2* ends, const float4* prop, float* lamB, const uint8_t* broken, float4* x, const float* w,
                       float invDt2) {
    const int b = first + blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= first + count || broken[b]) return;
    const int2 e = ends[b];
    const float wa = w[e.x], wb = w[e.y];
    const float ws = wa + wb;
    if (ws <= 0.0f) return;
    float4 pa = x[e.x], pb = x[e.y];
    const float3 r = make_float3(pa.x - pb.x, pa.y - pb.y, pa.z - pb.z);
    const float d = sqrtf(dot(r, r));
    if (d < 1e-9f) return;
    const float4 pr = prop[b];
    const float C = d - pr.x;
    const float at = pr.y * invDt2;
    const float dl = (-C - at * lamB[b]) / (ws + at);
    lamB[b] += dl;
    const float3 g = r * (dl / d);
    pa.x += g.x * wa;
    pa.y += g.y * wa;
    pa.z += g.z * wa;
    pb.x -= g.x * wb;
    pb.y -= g.y * wb;
    pb.z -= g.z * wb;
    x[e.x] = pa;
    x[e.y] = pb;
}

// a bond stretched past its breaking strain, or squeezed past its crushing strain, is gone
__global__ void kBondBreak(int nb, const int2* ends, const float4* prop, uint8_t* broken, const float4* x) {
    const int b = blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= nb || broken[b]) return;
    const int2 e = ends[b];
    const float4 pa = x[e.x], pb = x[e.y];
    const float3 r = make_float3(pa.x - pb.x, pa.y - pb.y, pa.z - pb.z);
    const float4 pr = prop[b];
    const float strain = (sqrtf(dot(r, r)) - pr.x) / pr.x;
    if (strain > pr.z || -strain > pr.w) broken[b] = 1;
}

// live bonds per particle; a liquefying group's particle with none left becomes liquid
__global__ void kDegree(int n, const int* csrStart, const int* csrBond, const uint8_t* broken, int* degree, uint32_t* phase, const int4* groups) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const int s = csrStart[i], e = csrStart[i + 1];
    int d = 0;
    for (int k = s; k < e; ++k) d += broken[csrBond[k] & 0x7fffffff] ? 0 : 1;
    degree[i] = d;
    const uint32_t ph = phase[i];
    if (d == 0 && e > s && !(ph & PF_FLUID)) {
        const int g = int(ph & PF_GROUP);
        if (g > 0 && (groups[g].z & PG_LIQUEFY)) phase[i] = PF_FLUID;   // its mass stays
    }
}

// rigid groups: centre of mass and A = sum x q^T (sum q = 0 at rest, so the centre drops out), one block per group in
// a fixed order (doubles: a bone has tens of thousands of particles); the rotation by Mueller et al. 2016, warm-started
__global__ void kRigidReduce(const int4* groups, const int* rigidList, const float4* x, const float4* q, float4* frame) {
    const int g = rigidList[blockIdx.x];
    const int4 G = groups[g];
    __shared__ double sh[kBlock][12];
    double acc[12] = {0};
    for (int k = G.x + threadIdx.x; k < G.x + G.y; k += blockDim.x) {
        const float4 p = x[k], o = q[k];
        acc[0] += p.x;
        acc[1] += p.y;
        acc[2] += p.z;
        const double pp[3] = {p.x, p.y, p.z}, oo[3] = {o.x, o.y, o.z};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) acc[3 + r * 3 + c] += pp[r] * oo[c];
    }
    for (int k = 0; k < 12; ++k) sh[threadIdx.x][k] = acc[k];
    __syncthreads();
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (threadIdx.x < s)
            for (int k = 0; k < 12; ++k) sh[threadIdx.x][k] += sh[threadIdx.x + s][k];
        __syncthreads();
    }
    if (threadIdx.x != 0) return;
    const double inv = 1.0 / double(G.y);
    float A[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) A[r][c] = float(sh[0][3 + r * 3 + c]);
    float4 qt = frame[2 * g + 1];   // the rotation last time
    for (int it = 0; it < 20; ++it) {
        const float xx = qt.x, yy = qt.y, zz = qt.z, ww = qt.w;
        const float R[3][3] = {{1 - 2 * (yy * yy + zz * zz), 2 * (xx * yy - zz * ww), 2 * (xx * zz + yy * ww)},
                               {2 * (xx * yy + zz * ww), 1 - 2 * (xx * xx + zz * zz), 2 * (yy * zz - xx * ww)},
                               {2 * (xx * zz - yy * ww), 2 * (yy * zz + xx * ww), 1 - 2 * (xx * xx + yy * yy)}};
        float3 om = make_float3(0, 0, 0);
        float den = 0.0f;
        for (int c = 0; c < 3; ++c) {
            const float3 rc = make_float3(R[0][c], R[1][c], R[2][c]), ac = make_float3(A[0][c], A[1][c], A[2][c]);
            om += cross(rc, ac);
            den += dot(rc, ac);
        }
        om = om * (1.0f / (fabsf(den) + 1e-9f));
        const float wl = sqrtf(dot(om, om));
        if (wl < 1e-9f) break;
        const float3 ax = om * (1.0f / wl);
        const float s = sinf(0.5f * wl), co = cosf(0.5f * wl);
        const float4 dq = make_float4(ax.x * s, ax.y * s, ax.z * s, co);
        // dq * qt
        float4 nq = make_float4(dq.w * qt.x + dq.x * qt.w + dq.y * qt.z - dq.z * qt.y, dq.w * qt.y - dq.x * qt.z + dq.y * qt.w + dq.z * qt.x,
                                dq.w * qt.z + dq.x * qt.y - dq.y * qt.x + dq.z * qt.w, dq.w * qt.w - dq.x * qt.x - dq.y * qt.y - dq.z * qt.z);
        const float l = rsqrtf(nq.x * nq.x + nq.y * nq.y + nq.z * nq.z + nq.w * nq.w);
        qt = make_float4(nq.x * l, nq.y * l, nq.z * l, nq.w * l);
    }
    frame[2 * g] = make_float4(float(sh[0][0] * inv), float(sh[0][1] * inv), float(sh[0][2] * inv), 0.0f);
    frame[2 * g + 1] = qt;
}

__device__ inline float3 rotate(float4 q, float3 v) {
    const float3 u = make_float3(q.x, q.y, q.z);
    const float3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

__global__ void kRigidProject(int n, const uint32_t* phase, const int4* groups, const float4* q, const float4* frame, float4* x) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const uint32_t ph = phase[i];
    if (ph & PF_FLUID) return;
    const int g = int(ph & PF_GROUP);
    if (g == 0 || !(groups[g].z & PG_RIGID)) return;
    const float4 c = frame[2 * g];
    const float3 o = rotate(frame[2 * g + 1], make_float3(q[i].x, q[i].y, q[i].z));
    x[i] = make_float4(c.x + o.x, c.y + o.y, c.z + o.z, 0.0f);
}

struct PlaneArgs {
    float r;                    // particle radius
    float muS, muD, fluidMu;
    bool box;
    float3 lo, hi;
};
// a particle against one plane (inside of n . x >= d): pushed out, friction against what it slid this substep
__device__ inline void planeContact(float3& p, float3 p0, float3 n, float d, float muS, float muD) {
    const float pen = d - dot(n, p);
    if (pen <= 0.0f) return;
    p += n * pen;
    if (muS <= 0.0f) return;
    const float3 mv = p - p0;
    const float3 t = mv - n * dot(mv, n);
    const float tl = sqrtf(dot(t, t));
    if (tl <= 1e-12f) return;
    const float f = tl < muS * pen ? 1.0f : fminf(muD * pen / tl, 1.0f);
    p -= t * f;
}
__global__ void kPlanes(int n, float4* x, const float4* x0, const uint32_t* phase, const float* w, PlaneArgs a) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || w[i] <= 0.0f) return;
    float3 p = xyz(x[i]);
    const float3 p0 = xyz(x0[i]);
    const bool f = (phase[i] & PF_FLUID) != 0;
    const float ms = f ? a.fluidMu : a.muS, md = f ? a.fluidMu : a.muD;
    planeContact(p, p0, make_float3(0, 1, 0), a.r, ms, md);
    if (a.box) {
        planeContact(p, p0, make_float3(1, 0, 0), a.lo.x + a.r, ms, md);
        planeContact(p, p0, make_float3(-1, 0, 0), -(a.hi.x - a.r), ms, md);
        planeContact(p, p0, make_float3(0, 0, 1), a.lo.z + a.r, ms, md);
        planeContact(p, p0, make_float3(0, 0, -1), -(a.hi.z - a.r), ms, md);
    }
    x[i] = make_float4(p.x, p.y, p.z, 0.0f);
}

__global__ void kVelocity(int n, const float4* x, const float4* x0, float4* v, const float* w, float invDt, float maxSpeed) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    if (w[i] <= 0.0f) { v[i] = make_float4(0, 0, 0, 0); return; }
    float3 vi = (xyz(x[i]) - xyz(x0[i])) * invDt;
    const float s = sqrtf(dot(vi, vi));
    if (s > maxSpeed) vi = vi * (maxSpeed / s);
    v[i] = make_float4(vi.x, vi.y, vi.z, 0.0f);
}

// The velocities made divergence-free (as DFSPH's second solver does, Bender & Koschier 2015), in the same
// multiplier form as the density: D_i = sum_j (v_i - v_j) . gradW_ij / S0 is how fast i's density changes; the
// velocities are corrected so that it does not. Position corrections leave a residual that, divided by a small
// substep, is velocity noise (a resting tank boiled at 0.1-0.7 m/s); this removes it at the velocity level.
// A particle at a free surface (density below rest) may still spread: only its compressing rate is removed.
__global__ void kGatherVel(int n, const uint32_t* idSorted, const float4* v, float4* vs) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k < n) vs[k] = v[idSorted[k]];
}
__global__ void kScatterVel(int n, const uint32_t* idSorted, const float4* vs, float4* v) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k < n) v[idSorted[k]] = vs[k];
}

// vs: velocities in sorted order
__global__ void kDivLambda(int n, int stride, const float4* xs, const uint32_t* phs, const float* psis, const int* nbr, const int* nbrCount,
                           const float4* vs, const float* rhos, float* lam, DensityArgs a) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    if (!(phs[k] & PF_FLUID)) { lam[k] = 0.0f; return; }
    const float3 p = xyz(xs[k]);
    const float3 vi = xyz(vs[k]);
    float D = 0.0f, g2 = 0.0f;
    float3 gi = make_float3(0, 0, 0);
    const int m = nbrCount[k];
    for (int q = 0; q < m; ++q) {
        const int j = nbr[size_t(q) * stride + k];
        const float4 xj = xs[j];
        const float3 r = p - xyz(xj);
        const bool fj = (phs[j] & PF_FLUID) != 0;
        const float3 g = gradW(a.K, r, sqrtf(dot(r, r))) * ((fj ? 1.0f : psis[j]) / a.S0);
        D += dot(vi - xyz(vs[j]), g);
        gi += g;
        const float ratio = fj ? 1.0f : xj.w / a.wf;
        g2 += ratio * dot(g, g);
    }
    {   // the walls stand still
        float wsum;
        float3 wg;
        wallDensity(a.walls, p, wsum, wg);
        wg = wg * (1.0f / a.S0);
        D += dot(vi, wg);
        gi += wg;
    }
    if (rhos[k] < 1.0f) D = fmaxf(D, 0.0f);
    lam[k] = -D / (dot(gi, gi) + g2 + a.eps);
}

// adds the correction to vs (sorted) directly: every multiplier was computed before, nothing here reads velocities
__global__ void kDivDelta(int n, int stride, const float4* xs, const uint32_t* phs, const float* psis, const uint8_t* flags, const int* nbr, const int* nbrCount,
                          const float* lam, float4* vs, DensityArgs a) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    const float4 xi = xs[k];
    const float3 p = xyz(xi);
    const bool fi = (phs[k] & PF_FLUID) != 0;
    if (!fi && !(flags[k] & NB_FLUID)) return;
    float3 acc = make_float3(0, 0, 0);
    if (xi.w > 0.0f) {
        const float lamI = lam[k];
        if (fi) {
            float wsum;
            float3 wg;
            wallDensity(a.walls, p, wsum, wg);
            acc += wg * (lamI / a.S0);
        }
        const int m = nbrCount[k];
        for (int q = 0; q < m; ++q) {
            const int j = nbr[size_t(q) * stride + k];
            const bool fj = (phs[j] & PF_FLUID) != 0;
            if (!fi && !fj) continue;
            const float3 r = p - xyz(xs[j]);
            const float3 g = gradW(a.K, r, sqrtf(dot(r, r))) * (1.0f / a.S0);
            if (fi) acc += g * (fj ? lamI + lam[j] : lamI * psis[j]);
            else acc += g * ((xi.w / a.wf) * lam[j] * psis[k]);
        }
    }
    vs[k] = vs[k] + make_float4(acc.x, acc.y, acc.z, 0.0f);
}

// XSPH viscosity and cohesion, fluid on fluid: velocity changes gathered into dv
// Akinci et al. 2013's surface normal (h times the gradient of the fluid's colour field): about zero inside, pointing
// into the fluid at its surface. Sorted order.
__global__ void kNormals(int n, int stride, const float4* xs, const uint32_t* phs, const int* nbr, const int* nbrCount, float4* ns, Kern K, float S0) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    float3 g = make_float3(0, 0, 0);
    if (phs[k] & PF_FLUID) {
        const float3 p = xyz(xs[k]);
        const int m = nbrCount[k];
        for (int q = 0; q < m; ++q) {
            const int j = nbr[size_t(q) * stride + k];
            if (!(phs[j] & PF_FLUID)) continue;
            const float3 r = p - xyz(xs[j]);
            g += gradW(K, r, sqrtf(dot(r, r)));
        }
        g = g * (K.h / S0);
    }
    ns[k] = make_float4(g.x, g.y, g.z, 0.0f);
}

// ns: the normals (kNormals), or null when there is no surface tension
__global__ void kFluidForces(int n, int stride, const float4* xs, const uint32_t* phs, const int* nbr, const int* nbrCount, const float4* vs,
                             const float* rhos, const float4* ns, float4* out, Kern K, float S0, float xsph, float cohesion, float cohPeak, float tension,
                             float dt) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    if (!(phs[k] & PF_FLUID) || xs[k].w <= 0.0f) { out[k] = vs[k]; return; }
    const float3 p = xyz(xs[k]);
    const float3 vi = xyz(vs[k]);
    float3 visc = make_float3(0, 0, 0), coh = make_float3(0, 0, 0);
    const float ri = fmaxf(rhos[k], 0.5f);
    const int m = nbrCount[k];
    for (int q = 0; q < m; ++q) {
        const int j = nbr[size_t(q) * stride + k];
        if (!(phs[j] & PF_FLUID)) continue;
        const float3 r = p - xyz(xs[j]);
        const float r2 = dot(r, r);
        visc += (xyz(vs[j]) - vi) * (W(K, r2) / S0);
        if (cohesion > 0.0f || tension > 0.0f) {
            const float d = sqrtf(r2);
            const float kij = 2.0f / (ri + fmaxf(rhos[j], 0.5f));   // Akinci's correction: less pull where the fluid is full
            if (cohesion > 0.0f && d > 1e-9f) coh += r * (-cohesion * kij * cohesionSpline(d, K.h) / (cohPeak * d));
            if (ns) coh += (xyz(ns[k]) - xyz(ns[j])) * (-tension * kij);   // curvature: the surface pulls itself flat
        }
    }
    const float3 d = visc * xsph + coh * dt;
    out[k] = make_float4(vi.x + d.x, vi.y + d.y, vi.z + d.z, 0.0f);
}

__global__ void kAddVelocity(int n, float4* v, const float4* dv) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    v[i] = v[i] + dv[i];
}

__global__ void kStats(int n, const float4* v, const uint32_t* phase, const float* rho, const uint32_t* idSorted, const int* nbrCount, int fullNbr,
                       float* stat) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    const uint32_t i = idSorted[k];
    const float4 vi = v[i];
    const float s = sqrtf(vi.x * vi.x + vi.y * vi.y + vi.z * vi.z);
    atomicMax(reinterpret_cast<int*>(&stat[3]), __float_as_int(s));
    if (!(phase[i] & PF_FLUID)) return;
    atomicAdd(&stat[5], 1.0f);
    const float e = rho[i] - 1.0f;
    if (e > 0.0f) atomicMax(reinterpret_cast<int*>(&stat[2]), __float_as_int(e));
    if (nbrCount[k] >= fullNbr) {
        atomicAdd(&stat[0], fabsf(e));
        atomicAdd(&stat[1], 1.0f);
    }
}

__global__ void kPlace(int first, int count, float4* x, float4* x0, float4* v, const float4* q, float3 c, float3 vel) {
    const int i = first + blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= first + count) return;
    const float4 o = q[i];
    x[i] = x0[i] = make_float4(c.x + o.x, c.y + o.y, c.z + o.z, 0.0f);
    v[i] = make_float4(vel.x, vel.y, vel.z, 0.0f);
}

__global__ void kBreakSphere(int nb, const int2* ends, const float4* x, uint8_t* broken, float3 c, float r2, int* counter) {
    const int b = blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= nb || broken[b]) return;
    const int2 e = ends[b];
    const float3 a = xyz(x[e.x]) - c, d = xyz(x[e.y]) - c;
    if (dot(a, a) < r2 || dot(d, d) < r2) {
        broken[b] = 1;
        atomicAdd(counter, 1);
    }
}

__global__ void kPad(int first, int count, float4* x, float4* x0, float4* v, float* w, uint32_t* phase, float* psi, int* csrStart, int csrTotal,
                     uint8_t* sleepOk, int* degree, int* initDeg, uint8_t* markPrev) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= count) return;
    const int i = first + k;
    const float4 p = make_float4(1.0e4f + float(i % 256), 1.0e4f, float(i / 256), 0.0f);   // a metre apart: no neighbours
    x[i] = x0[i] = p;
    v[i] = make_float4(0, 0, 0, 0);
    w[i] = 0.0f;
    phase[i] = 0;   // solid, of no group (group 0 is otherwise only fluid), kinematic
    psi[i] = 1.0f;
    csrStart[i + 1] = csrTotal;
    sleepOk[i] = 1;   // never searches (no bonds to lose, nobody near to mark it)
    degree[i] = initDeg[i] = 0;
    markPrev[i] = 0;
}

__global__ void kExport(int n, const float4* x, const uint32_t* phase, float3* pos, float* disp) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    if (pos) pos[i] = xyz(x[i]);
    if (disp) disp[i] = (phase[i] & PF_FLUID) ? 1.0f : 0.0f;
}

}   // namespace

// ---- host

// phases timed with events when the environment variable TANECIK_PROFIL is set (a diagnosis: it serialises nothing,
// but every event costs a little)
struct Profiler {
    bool on = std::getenv("TANECIK_PROFIL") != nullptr;
    cudaStream_t st = nullptr;
    std::vector<std::pair<const char*, cudaEvent_t>> marks;
    std::map<std::string, double> total;
    int frames = 0;
    void begin(cudaStream_t s) {
        if (!on) return;
        st = s;
        mark("start");
    }
    void mark(const char* name) {
        if (!on) return;
        cudaEvent_t e;
        cudaEventCreate(&e);
        cudaEventRecord(e, st);
        marks.push_back({name, e});
    }
    void end() {
        if (!on || marks.empty()) return;
        cudaEventSynchronize(marks.back().second);
        for (size_t k = 1; k < marks.size(); ++k) {
            float ms = 0.0f;
            cudaEventElapsedTime(&ms, marks[k - 1].second, marks[k].second);
            total[marks[k].first] += ms;
        }
        for (auto& m : marks) cudaEventDestroy(m.second);
        marks.clear();
        if (++frames % 60 == 0) {
            std::printf("profil (%d kare, kare basina ms):", frames);
            for (auto& t : total) std::printf(" %s %.3f", t.first.c_str(), t.second / frames);
            std::printf("\n");
        }
    }
};
static Profiler g_prof;

void ParticleWorld::begin(float spacing) {
    destroy();
    s_ = spacing;
    n_ = nb_ = reserve_ = 0;
    time_ = 0.0;
    hx_.clear();
    hv_.clear();
    hw_.clear();
    hphase_.clear();
    hbonds_.clear();
    hgroups_.assign(1, HostGroup{});   // group 0: free fluid
}

int ParticleWorld::addGroup(uint32_t flags) {
    HostGroup g;
    g.first = int(hx_.size());
    g.flags = flags;
    hgroups_.push_back(g);
    return int(hgroups_.size()) - 1;
}

int ParticleWorld::addParticles(const std::vector<float3>& pos, float density, int group, const std::vector<float3>* vel) {
    const int first = int(hx_.size());
    if (group > 0) {
        HostGroup& g = hgroups_[group];
        if (g.count == 0) g.first = first;
        else if (g.first + g.count != first) std::fprintf(stderr, "tanecik: grup %d ardisik degil\n", group);
        g.count += int(pos.size());
    }
    const float w = 1.0f / (density * s_ * s_ * s_);
    for (size_t k = 0; k < pos.size(); ++k) {
        hx_.push_back(make_float4(pos[k].x, pos[k].y, pos[k].z, 0.0f));
        const float3 v = vel ? (*vel)[k] : make_float3(0, 0, 0);
        hv_.push_back(make_float4(v.x, v.y, v.z, 0.0f));
        hw_.push_back(w);
        hphase_.push_back(group == 0 ? PF_FLUID : uint32_t(group));
    }
    return first;
}

int ParticleWorld::bondWithin(int first, int count, float maxDist, float compliance, float breakStrain, float crushStrain) {
    // a grid of maxDist cells over the range; pairs i < j
    const float inv = 1.0f / maxDist;
    std::unordered_map<unsigned long long, std::vector<int>> cells;
    auto key = [&](int x, int y, int z) { return packCell(x, y, z); };
    for (int i = first; i < first + count; ++i) {
        const float4 p = hx_[i];
        cells[key(int(std::floor(p.x * inv)), int(std::floor(p.y * inv)), int(std::floor(p.z * inv)))].push_back(i);
    }
    const int before = int(hbonds_.size());
    const float md2 = maxDist * maxDist;
    for (int i = first; i < first + count; ++i) {
        const float4 p = hx_[i];
        const int cx = int(std::floor(p.x * inv)), cy = int(std::floor(p.y * inv)), cz = int(std::floor(p.z * inv));
        std::vector<int> found;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto it = cells.find(key(cx + dx, cy + dy, cz + dz));
                    if (it == cells.end()) continue;
                    for (int j : it->second)
                        if (j > i) found.push_back(j);
                }
        std::sort(found.begin(), found.end());
        for (int j : found) {
            const float4 q = hx_[j];
            const float d2 = (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z);
            if (d2 > md2 || d2 < 1e-12f) continue;
            hbonds_.push_back({i, j, std::sqrt(d2), compliance, breakStrain, crushStrain});
        }
    }
    return int(hbonds_.size()) - before;
}

void ParticleWorld::addBond(int a, int b, float compliance, float breakStrain, float crushStrain) {
    const float4 p = hx_[a], q = hx_[b];
    const float d = std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z));
    hbonds_.push_back({a, b, d, compliance, breakStrain, crushStrain});
}

void ParticleWorld::allocate(int cap, int bondCap) {
    cap_ = cap;
    x_ = dalloc<float4>(cap);
    x0_ = dalloc<float4>(cap);
    v_ = dalloc<float4>(cap);
    dx_ = dalloc<float4>(cap);
    q_ = dalloc<float4>(cap);
    w_ = dalloc<float>(cap);
    phase_ = dalloc<uint32_t>(cap);
    rho_ = dalloc<float>(cap);
    contacts_ = dalloc<uint8_t>(cap);
    contactsNext_ = dalloc<uint8_t>(cap);
    key_ = dalloc<uint32_t>(cap);
    keySorted_ = dalloc<uint32_t>(cap);
    id_ = dalloc<uint32_t>(cap);
    idSorted_ = dalloc<uint32_t>(cap);
    xs_ = dalloc<float4>(cap);
    vs_ = dalloc<float4>(cap);
    ns_ = dalloc<float4>(cap);
    phs_ = dalloc<uint32_t>(cap);
    psi_ = dalloc<float>(cap);
    psis_ = dalloc<float>(cap);
    code_ = dalloc<unsigned long long>(cap);
    lam_ = dalloc<float>(cap);
    rhos_ = dalloc<float>(cap);
    nbrFlag_ = dalloc<uint8_t>(cap);
    nbr_ = dalloc<int>(size_t(cap) * kMaxNbr);
    nbrCount_ = dalloc<int>(cap);
    tableBits_ = 10;
    while ((1 << tableBits_) < 2 * cap && tableBits_ < 24) ++tableBits_;
    cellStart_ = dalloc<int>(size_t(1) << tableBits_);
    cellEnd_ = dalloc<int>(size_t(1) << tableBits_);
    sortTempBytes_ = 0;
    cub::DeviceRadixSort::SortPairs(nullptr, sortTempBytes_, key_, keySorted_, id_, idSorted_, cap, 0, tableBits_);
    PCUDA(cudaMalloc(&sortTemp_, std::max<size_t>(sortTempBytes_, 16)));
    bEnds_ = dalloc<int2>(bondCap);
    bProp_ = dalloc<float4>(bondCap);
    bLam_ = dalloc<float>(bondCap);
    bCorr_ = dalloc<float4>(1);
    bBroken_ = dalloc<uint8_t>(bondCap);
    csrStart_ = dalloc<int>(size_t(cap) + 1);
    csrBond_ = dalloc<int>(size_t(bondCap) * 2);
    degree_ = dalloc<int>(cap);
    initDeg_ = dalloc<int>(cap);
    sleepOk_ = dalloc<uint8_t>(cap);
    markPrev_ = dalloc<uint8_t>(cap);
    markNext_ = dalloc<uint8_t>(cap);
    stat_ = dalloc<float>(8);
    wallTable_ = dalloc<float2>(kWallTable);
    PCUDA(cudaEventCreate(&e0_));
    PCUDA(cudaEventCreate(&e1_));
    PCUDA(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking));
    gSub_ = dalloc<float>(kMaxSubsteps);
    graph_ = nullptr;
}

void ParticleWorld::freeAll() {
    if (!x_) return;
    cudaDeviceSynchronize();
    dfree(x_), dfree(x0_), dfree(v_), dfree(dx_), dfree(q_), dfree(w_), dfree(phase_), dfree(rho_), dfree(contacts_), dfree(contactsNext_);
    dfree(key_), dfree(keySorted_), dfree(id_), dfree(idSorted_), dfree(xs_), dfree(vs_), dfree(ns_), dfree(phs_), dfree(psi_), dfree(psis_), dfree(code_), dfree(lam_), dfree(rhos_), dfree(nbrFlag_), dfree(nbr_), dfree(nbrCount_);
    dfree(cellStart_), dfree(cellEnd_);
    if (sortTemp_) cudaFree(sortTemp_);
    sortTemp_ = nullptr;
    dfree(bEnds_), dfree(bProp_), dfree(bLam_), dfree(bCorr_), dfree(bBroken_), dfree(csrStart_), dfree(csrBond_), dfree(degree_), dfree(initDeg_), dfree(sleepOk_), dfree(markPrev_), dfree(markNext_);
    dfree(groups_), dfree(groupFrame_), dfree(rigidList_), dfree(stat_), dfree(wallTable_);
    nRigid_ = 0;
    if (e0_) cudaEventDestroy(e0_);
    if (e1_) cudaEventDestroy(e1_);
    e0_ = e1_ = nullptr;
    if (graph_) cudaGraphExecDestroy(graph_);
    graph_ = nullptr;
    dfree(gSub_);
    if (stream_) cudaStreamDestroy(stream_);
    stream_ = nullptr;
    cap_ = 0;
}

void ParticleWorld::destroy() {
    freeAll();
    n_ = nb_ = ng_ = 0;
}

// the GS colours of the bonds (no two bonds of a colour share a particle), greedy in bond order
static std::vector<int> colourBonds(int n, const std::vector<std::pair<int, int>>& ends, std::vector<int>& order, int& colours) {
    std::vector<unsigned long long> used(size_t(n), 0ull);
    std::vector<int> colour(ends.size());
    colours = 0;
    for (size_t b = 0; b < ends.size(); ++b) {
        const unsigned long long u = used[ends[b].first] | used[ends[b].second];
        int c = 0;
        while (c < 64 && (u >> c & 1ull)) ++c;
        if (c == 64) { std::fprintf(stderr, "tanecik: bag renkleri 64'u asti\n"); c = 63; }
        colour[b] = c;
        used[ends[b].first] |= 1ull << c;
        used[ends[b].second] |= 1ull << c;
        colours = std::max(colours, c + 1);
    }
    order.resize(ends.size());
    std::vector<int> cnt(size_t(colours) + 1, 0);
    for (int c : colour) ++cnt[c + 1];
    for (int c = 0; c < colours; ++c) cnt[c + 1] += cnt[c];
    std::vector<int> fill(cnt.begin(), cnt.end() - 1);
    for (size_t b = 0; b < ends.size(); ++b) order[fill[colour[b]]++] = int(b);
    return std::vector<int>(cnt.begin(), cnt.end());   // offsets, colours + 1
}

void ParticleWorld::commit() {
    const int n = int(hx_.size());
    const int nb = int(hbonds_.size());
    freeAll();
    allocate(std::max(std::max(n + n / 4, 1024), reserve_), std::max(nb, 1));
    n_ = n;
    nb_ = nb;
    // kernel constants on the lattice: S0 (density of a full lattice) and D0 (the multiplier's denominator there)
    {
        const float h = params.kernel * s_;
        Kern K{h, h * h, 315.0f / (64.0f * 3.14159265f * std::pow(h, 9.0f)), -45.0f / (3.14159265f * std::pow(h, 6.0f)), 0.0f};
        const int R = int(std::ceil(params.kernel)) + 1;
        double S0 = 0.0, g2 = 0.0;
        for (int z = -R; z <= R; ++z)
            for (int y = -R; y <= R; ++y)
                for (int x = -R; x <= R; ++x) {
                    const float3 r = make_float3(x * s_, y * s_, z * s_);
                    const float d2 = dot(r, r);
                    S0 += W(K, d2);
                    if (d2 > 0.0f) {
                        const float3 g = gradW(K, r, std::sqrt(d2));
                        g2 += dot(g, g);
                    }
                }
        S0_ = float(S0);
        D0_ = float(g2 / (S0 * S0));
        // the walls: the lattice continued beyond them (layers at -(k + 1/2) s), averaged over the particle's sideways
        // offset against it
        std::vector<float2> tab(kWallTable);
        const int Rl = int(std::ceil(params.kernel)) + 2, offs = 4;
        for (int t = 0; t < kWallTable; ++t) {
            const float d = h * t / float(kWallTable - 1);
            double sum = 0.0, grad = 0.0;
            for (int oy = 0; oy < offs; ++oy)
                for (int ox = 0; ox < offs; ++ox)
                    for (int k = 0; k <= Rl; ++k)
                        for (int j = -Rl; j <= Rl; ++j)
                            for (int i = -Rl; i <= Rl; ++i) {
                                const float3 r = make_float3((i + ox / float(offs)) * s_, (j + oy / float(offs)) * s_, d + (k + 0.5f) * s_);
                                const float r2 = dot(r, r);
                                sum += W(K, r2);
                                const float rl = std::sqrt(r2);
                                if (rl < h) grad += -K.spiky * (h - rl) * (h - rl) * r.z / rl;
                            }
            tab[t] = make_float2(float(sum / (offs * offs)), float(grad / (offs * offs)));
        }
        PCUDA(cudaMemcpy(wallTable_, tab.data(), tab.size() * sizeof(float2), cudaMemcpyHostToDevice));
        if (std::getenv("TANECIK_VERBOSE")) {
            const int t = int((kWallTable - 1) * 0.5f / params.kernel);
            std::printf("duvar: d = s/2'de toplam %.4g (S0'in %.1f%%), egim %.4g\n", tab[t].x, 100.0 * tab[t].x / S0, tab[t].y);
        }
    }
    fluidInvMass_ = 1.0f / (kWaterDensity * s_ * s_ * s_);
    // rigid groups: offsets from their centres at rest
    std::vector<float4> q(size_t(n), make_float4(0, 0, 0, 0));
    std::vector<int4> groups;
    std::vector<float4> frames;
    std::vector<int> rigid;
    for (size_t g = 0; g < hgroups_.size(); ++g) {
        const HostGroup& G = hgroups_[g];
        groups.push_back(make_int4(G.first, G.count, int(G.flags), 0));
        float3 c = make_float3(0, 0, 0);
        if ((G.flags & PG_RIGID) && G.count > 0) {
            double cx = 0, cy = 0, cz = 0;
            for (int i = G.first; i < G.first + G.count; ++i) { cx += hx_[i].x; cy += hx_[i].y; cz += hx_[i].z; }
            c = make_float3(float(cx / G.count), float(cy / G.count), float(cz / G.count));
            for (int i = G.first; i < G.first + G.count; ++i) q[i] = make_float4(hx_[i].x - c.x, hx_[i].y - c.y, hx_[i].z - c.z, 0.0f);
            rigid.push_back(int(g));
        }
        frames.push_back(make_float4(c.x, c.y, c.z, 0.0f));
        frames.push_back(make_float4(0, 0, 0, 1));
    }
    ng_ = int(groups.size());
    nRigid_ = int(rigid.size());
    rigidList_ = dalloc<int>(rigid.size());
    if (nRigid_) PCUDA(cudaMemcpy(rigidList_, rigid.data(), rigid.size() * sizeof(int), cudaMemcpyHostToDevice));
    groups_ = dalloc<int4>(groups.size());
    groupFrame_ = dalloc<float4>(frames.size());
    PCUDA(cudaMemcpy(groups_, groups.data(), groups.size() * sizeof(int4), cudaMemcpyHostToDevice));
    PCUDA(cudaMemcpy(groupFrame_, frames.data(), frames.size() * sizeof(float4), cudaMemcpyHostToDevice));
    // boundary weights: S0 over the kernel sum of the particle's own group at rest, clamped (a fluid particle: 1)
    {
        const float h = params.kernel * s_;
        Kern K{h, h * h, 315.0f / (64.0f * 3.14159265f * std::pow(h, 9.0f)), -45.0f / (3.14159265f * std::pow(h, 6.0f)), 0.0f};
        std::vector<float> psi(size_t(n), 1.0f);
        const float inv = 1.0f / h;
        std::unordered_map<unsigned long long, std::vector<int>> cells;
        for (int i = 0; i < n; ++i)
            if (!(hphase_[i] & PF_FLUID))
                cells[packCell(int(std::floor(hx_[i].x * inv)), int(std::floor(hx_[i].y * inv)), int(std::floor(hx_[i].z * inv)))].push_back(i);
        for (int i = 0; i < n; ++i) {
            if (hphase_[i] & PF_FLUID) continue;
            const float4 p = hx_[i];
            const int cx = int(std::floor(p.x * inv)), cy = int(std::floor(p.y * inv)), cz = int(std::floor(p.z * inv));
            double sum = 0.0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const auto it = cells.find(packCell(cx + dx, cy + dy, cz + dz));
                        if (it == cells.end()) continue;
                        for (int j : it->second) {
                            if ((hphase_[j] & PF_GROUP) != (hphase_[i] & PF_GROUP)) continue;
                            const float3 r = make_float3(p.x - hx_[j].x, p.y - hx_[j].y, p.z - hx_[j].z);
                            sum += W(K, dot(r, r));
                        }
                    }
            psi[i] = float(std::min(std::max(double(S0_) / std::max(sum, 1e-30), 1.0), double(params.psiMax)));
        }
        PCUDA(cudaMemcpy(psi_, psi.data(), size_t(n) * sizeof(float), cudaMemcpyHostToDevice));
        if (std::getenv("TANECIK_VERBOSE")) {
            double mean = 0.0;
            int cnt = 0;
            float mx = 0.0f;
            for (int i = 0; i < n; ++i)
                if (!(hphase_[i] & PF_FLUID)) { mean += psi[i]; ++cnt; mx = std::max(mx, psi[i]); }
            if (cnt) std::printf("sinir agirligi: kati taneciklerde ort %.2f, en buyuk %.2f\n", mean / cnt, mx);
        }
    }
    // particles
    PCUDA(cudaMemcpy(x_, hx_.data(), size_t(n) * sizeof(float4), cudaMemcpyHostToDevice));
    PCUDA(cudaMemcpy(x0_, hx_.data(), size_t(n) * sizeof(float4), cudaMemcpyHostToDevice));
    PCUDA(cudaMemcpy(v_, hv_.data(), size_t(n) * sizeof(float4), cudaMemcpyHostToDevice));
    PCUDA(cudaMemcpy(q_, q.data(), size_t(n) * sizeof(float4), cudaMemcpyHostToDevice));
    PCUDA(cudaMemcpy(w_, hw_.data(), size_t(n) * sizeof(float), cudaMemcpyHostToDevice));
    PCUDA(cudaMemcpy(phase_, hphase_.data(), size_t(n) * sizeof(uint32_t), cudaMemcpyHostToDevice));
    // bonds, ordered by colour; each particle's list of them
    std::vector<std::pair<int, int>> ends;
    for (const HostBond& b : hbonds_) ends.push_back({b.a, b.b});
    std::vector<int> order;
    int colours = 0;
    colourOffset_ = nb ? colourBonds(n, ends, order, colours) : std::vector<int>{0};
    std::vector<int2> e(static_cast<size_t>(nb));
    std::vector<float4> pr(static_cast<size_t>(nb));
    std::vector<std::vector<int>> per(static_cast<size_t>(n));
    for (int k = 0; k < nb; ++k) {
        const HostBond& b = hbonds_[order[k]];
        e[k] = make_int2(b.a, b.b);
        pr[k] = make_float4(b.rest, b.compliance, b.breakStrain, b.crushStrain);
        per[b.a].push_back(k);
        per[b.b].push_back(k | int(0x80000000u));
    }
    std::vector<int> cs(size_t(n) + 1, 0), cb;
    for (int i = 0; i < n; ++i) {
        cs[i + 1] = cs[i] + int(per[i].size());
        cb.insert(cb.end(), per[i].begin(), per[i].end());
    }
    if (nb) {
        PCUDA(cudaMemcpy(bEnds_, e.data(), size_t(nb) * sizeof(int2), cudaMemcpyHostToDevice));
        PCUDA(cudaMemcpy(bProp_, pr.data(), size_t(nb) * sizeof(float4), cudaMemcpyHostToDevice));
        PCUDA(cudaMemcpy(csrBond_, cb.data(), cb.size() * sizeof(int), cudaMemcpyHostToDevice));
    }
    PCUDA(cudaMemcpy(csrStart_, cs.data(), cs.size() * sizeof(int), cudaMemcpyHostToDevice));
    {   // who may sleep: a bonded particle with as many bonds as anyone in its group has
        std::vector<int> deg(static_cast<size_t>(n));
        std::vector<int> gmax(hgroups_.size(), 0);
        for (int i = 0; i < n; ++i) {
            deg[i] = cs[i + 1] - cs[i];
            const uint32_t ph = hphase_[i];
            if (!(ph & PF_FLUID)) gmax[ph & PF_GROUP] = std::max(gmax[ph & PF_GROUP], deg[i]);
        }
        std::vector<uint8_t> ok(static_cast<size_t>(n), 0), one(static_cast<size_t>(n), 1);
        for (int i = 0; i < n; ++i) {
            const uint32_t ph = hphase_[i];
            ok[i] = !(ph & PF_FLUID) && (ph & PF_GROUP) > 0 && gmax[ph & PF_GROUP] > 0 && deg[i] == gmax[ph & PF_GROUP] && !std::getenv("TANECIK_UYUMA_YOK");
        }
        PCUDA(cudaMemcpy(initDeg_, deg.data(), size_t(n) * sizeof(int), cudaMemcpyHostToDevice));
        PCUDA(cudaMemcpy(degree_, deg.data(), size_t(n) * sizeof(int), cudaMemcpyHostToDevice));
        PCUDA(cudaMemcpy(sleepOk_, ok.data(), size_t(n), cudaMemcpyHostToDevice));
        PCUDA(cudaMemcpy(markPrev_, one.data(), size_t(n), cudaMemcpyHostToDevice));   // the first substep: everyone searches
    }
    nPad_ = n;
    pad(n);
    if (std::getenv("TANECIK_VERBOSE"))
        std::printf("dunya: %d tanecik, %d bag (%d renk), %d grup, S0 %.4g, D0 %.4g\n", n, nb, colours, ng_, S0_, D0_);
}

// One frame: the substeps' launches are captured once into a CUDA graph and replayed (a few hundred small launches a
// frame; a 12 thousand particle balloon took 9.8 ms, most of it launching). Recaptured when the frame's length, the
// settings or the particle count change. Not with TANECIK_PROFIL (its events would be captured too).
void ParticleWorld::step(float dt, cudaStream_t st) {
    if (n_ == 0) return;
    if (!st) st = stream_;
    const int subs = std::max(1, params.substeps);
    std::vector<float> g(static_cast<size_t>(subs));
    for (int k = 0; k < subs; ++k) {
        const double t = time_ + double(dt) / subs * k;
        g[k] = params.gravity * (params.gravityRamp > 0.0f ? float(std::min(1.0, t / params.gravityRamp)) : 1.0f);
    }
    if (subs > kMaxSubsteps) { std::fprintf(stderr, "tanecik: en fazla %d alt adim\n", kMaxSubsteps); return; }
    PCUDA(cudaMemcpyAsync(gSub_, g.data(), g.size() * sizeof(float), cudaMemcpyHostToDevice, st));
    PCUDA(cudaEventRecord(e0_, st));
    const bool useGraph = !g_prof.on && !std::getenv("TANECIK_GRAFIKSIZ");
    if (useGraph) {
        if (!graph_ || graphDt_ != dt || graphN_ != nPad_ || std::memcmp(&graphP_, &params, sizeof(PParams)) != 0) {
            if (graph_) cudaGraphExecDestroy(graph_);
            graph_ = nullptr;
            cudaGraph_t gr;
            PCUDA(cudaStreamBeginCapture(st, cudaStreamCaptureModeThreadLocal));
            record(dt, st);
            PCUDA(cudaStreamEndCapture(st, &gr));
            PCUDA(cudaGraphInstantiate(&graph_, gr, 0));
            cudaGraphDestroy(gr);
            graphDt_ = dt;
            graphN_ = nPad_;
            std::memcpy(&graphP_, &params, sizeof(PParams));
        }
        PCUDA(cudaGraphLaunch(graph_, st));
    } else {
        record(dt, st);
    }
    time_ += dt;
    PCUDA(cudaEventRecord(e1_, st));
    PCUDA(cudaEventSynchronize(e1_));
    PCUDA(cudaEventElapsedTime(&lastMs_, e0_, e1_));
}

void ParticleWorld::record(float dt, cudaStream_t st) {
    // every launch covers the padded count: appended particles take padding slots without a new graph
    const int n_ = nPad_;
    const PParams& P = params;
    const int subs = std::max(1, P.substeps);
    const float h = P.kernel * s_;
    const float invH = 1.0f / h;
    const uint32_t mask = (1u << tableBits_) - 1u;
    Kern K{h, h * h, 315.0f / (64.0f * 3.14159265f * std::pow(h, 9.0f)), -45.0f / (3.14159265f * std::pow(h, 6.0f)), 0.0f};
    float cohPeak = 0.0f;
    for (int k = 1; k < 100; ++k) cohPeak = std::max(cohPeak, cohesionSpline(h * k / 100.0f, h));
    const Walls walls{P.density && P.wallDensity ? wallTable_ : nullptr, h, P.box, P.boxLo, P.boxHi};
    DensityArgs da{walls, K, S0_, P.relax * D0_, fluidInvMass_, P.scorrK, P.scorrN, W(K, P.scorrDq * P.scorrDq * h * h), P.unilateral, P.density};
    DeltaArgs dl{walls, K, S0_, fluidInvMass_, P.scorrK, P.scorrN, W(K, P.scorrDq * P.scorrDq * h * h), D0_ * (1.0f + P.relax), s_, P.muStatic, P.muDynamic, P.fluidSolidMu, P.density, P.sor};
    PlaneArgs pa{0.5f * s_, P.planeMuStatic, P.planeMuDynamic, P.fluidPlaneMu, P.box, P.boxLo, P.boxHi};
    const int nB = blocks(n_);
    PCUDA(cudaMemsetAsync(stat_, 0, 8 * sizeof(float), st));
    g_prof.begin(st);
    for (int s = 0; s < subs; ++s) {
        const float sdt = dt / float(subs);
        kPredict<<<nB, kBlock, 0, st>>>(n_, x_, x0_, v_, w_, gSub_ + s, sdt);
        g_prof.mark("tahmin");
        // neighbours
        kKeys<<<nB, kBlock, 0, st>>>(n_, x_, key_, id_, invH, mask);
        cub::DeviceRadixSort::SortPairs(sortTemp_, sortTempBytes_, key_, keySorted_, id_, idSorted_, n_, 0, tableBits_, st);
        g_prof.mark("siralama");
        kReorder<<<nB, kBlock, 0, st>>>(n_, idSorted_, x_, w_, phase_, psi_, xs_, phs_, psis_, code_, invH);
        PCUDA(cudaMemsetAsync(cellStart_, 0xff, (size_t(1) << tableBits_) * sizeof(int), st));
        kCellRanges<<<nB, kBlock, 0, st>>>(n_, keySorted_, cellStart_, cellEnd_);
        g_prof.mark("hucre");
        PCUDA(cudaMemsetAsync(markNext_, 0, size_t(n_), st));
        kNeighbours<<<nB, kBlock, 0, st>>>(n_, cap_, xs_, phs_, code_, cellStart_, cellEnd_, h * h, invH, mask, nbr_, nbrCount_, nbrFlag_, stat_,
                                           SleepArgs{idSorted_, sleepOk_, degree_, initDeg_, markPrev_, markNext_});
        PCUDA(cudaMemcpyAsync(markPrev_, markNext_, size_t(n_), cudaMemcpyDeviceToDevice, st));
        g_prof.mark("komsu");
        if (nb_) PCUDA(cudaMemsetAsync(bLam_, 0, size_t(nb_) * sizeof(float), st));
        const int iters = std::max(1, P.iterations), bondIters = std::max(1, P.bondIterations);
        for (int it = 0; it < std::max(iters, bondIters); ++it) {
            if (it < iters) {   // density and contacts
                if (it > 0) kReorder<<<nB, kBlock, 0, st>>>(n_, idSorted_, x_, w_, phase_, psi_, xs_, nullptr, nullptr, nullptr, invH);
                kLambda<<<nB, kBlock, 0, st>>>(n_, cap_, xs_, phs_, psis_, nbr_, nbrCount_, idSorted_, lam_, rho_, rhos_, da);
                g_prof.mark("lambda");
                kDelta<<<nB, kBlock, 0, st>>>(n_, cap_, xs_, phs_, psis_, nbrFlag_, idSorted_, nbr_, nbrCount_, lam_, x0_, contacts_, contactsNext_, dx_, dl);
                kApply<<<nB, kBlock, 0, st>>>(n_, x_, dx_);
                g_prof.mark("delta");
            }
            if (nb_ && P.bonds && it < bondIters) {
                const float invDt2 = 1.0f / (sdt * sdt);
                for (size_t c = 0; c + 1 < colourOffset_.size(); ++c) {
                    const int f = colourOffset_[c], cnt = colourOffset_[c + 1] - f;
                    if (cnt > 0) kBonds<<<blocks(cnt), kBlock, 0, st>>>(f, cnt, bEnds_, bProp_, bLam_, bBroken_, x_, w_, invDt2);
                }
            }
            g_prof.mark("bag");
            kPlanes<<<nB, kBlock, 0, st>>>(n_, x_, x0_, phase_, w_, pa);
            if (nRigid_) {   // one block per rigid group, then every rigid particle to its place in its body
                kRigidReduce<<<nRigid_, kBlock, 0, st>>>(groups_, rigidList_, x_, q_, groupFrame_);
                kRigidProject<<<nB, kBlock, 0, st>>>(n_, phase_, groups_, q_, groupFrame_, x_);
            }
            g_prof.mark("duzlem_kati");
        }
        kVelocity<<<nB, kBlock, 0, st>>>(n_, x_, x0_, v_, w_, 1.0f / sdt, P.maxSpeed);
        g_prof.mark("hiz");
        const bool div = P.density && P.divergenceIters > 0, visc = P.xsph > 0.0f || P.cohesion > 0.0f || P.tension > 0.0f;
        if (div || visc) {
            kReorder<<<nB, kBlock, 0, st>>>(n_, idSorted_, x_, w_, phase_, psi_, xs_, nullptr, nullptr, nullptr, invH);
            kGatherVel<<<nB, kBlock, 0, st>>>(n_, idSorted_, v_, vs_);
            if (div)
                for (int it = 0; it < P.divergenceIters; ++it) {
                    kDivLambda<<<nB, kBlock, 0, st>>>(n_, cap_, xs_, phs_, psis_, nbr_, nbrCount_, vs_, rhos_, lam_, da);
                    kDivDelta<<<nB, kBlock, 0, st>>>(n_, cap_, xs_, phs_, psis_, nbrFlag_, nbr_, nbrCount_, lam_, vs_, da);
                }
            g_prof.mark("diverjans");
            if (visc) {   // every substep: once a frame (with the frame's step) was measured impulsive (a puddle never settled)
                const float xs = 1.0f - std::pow(1.0f - std::min(P.xsph, 0.99f), sdt * 60.0f);
                if (P.tension > 0.0f) kNormals<<<nB, kBlock, 0, st>>>(n_, cap_, xs_, phs_, nbr_, nbrCount_, ns_, K, S0_);
                kFluidForces<<<nB, kBlock, 0, st>>>(n_, cap_, xs_, phs_, nbr_, nbrCount_, vs_, rhos_, P.tension > 0.0f ? ns_ : nullptr, dx_, K, S0_, xs,
                                                    P.cohesion, cohPeak, P.tension, sdt);
                kScatterVel<<<nB, kBlock, 0, st>>>(n_, idSorted_, dx_, v_);
            } else {
                kScatterVel<<<nB, kBlock, 0, st>>>(n_, idSorted_, vs_, v_);
            }
            g_prof.mark("xsph");
        }
        if (nb_ && P.bonds) {
            kBondBreak<<<blocks(nb_), kBlock, 0, st>>>(nb_, bEnds_, bProp_, bBroken_, x_);
            kDegree<<<nB, kBlock, 0, st>>>(n_, csrStart_, csrBond_, bBroken_, degree_, phase_, groups_);
        }
        std::swap(contacts_, contactsNext_);
    }
    if (subs % 2) std::swap(contacts_, contactsNext_);   // a replayed graph starts where the capture did: keep it even
    if (subs % 2) PCUDA(cudaMemcpyAsync(contacts_, contactsNext_, size_t(n_), cudaMemcpyDeviceToDevice, st));
    // a full neighbourhood: the lattice's count within h, less a little
    const int R = int(std::ceil(P.kernel));
    int full = 0;
    for (int z = -R; z <= R; ++z)
        for (int y = -R; y <= R; ++y)
            for (int x = -R; x <= R; ++x)
                if ((x || y || z) && float(x * x + y * y + z * z) < P.kernel * P.kernel) ++full;
    kStats<<<nB, kBlock, 0, st>>>(n_, v_, phase_, rho_, idSorted_, nbrCount_, int(full * 0.95f), stat_);
    g_prof.mark("istatistik");
    g_prof.end();
}

// the padded count: n rounded up to a block of 4096 (capped by the capacity), the slots past n made inert
void ParticleWorld::pad(int n) {
    const int want = std::min(cap_, (n + 4095) / 4096 * 4096);
    if (want <= nPad_ && nPad_ > n) return;
    const int from = n, count = std::max(want, n) - n;
    nPad_ = std::max(want, n);
    if (count <= 0) return;
    int total = 0;
    PCUDA(cudaMemcpy(&total, csrStart_ + n, sizeof(int), cudaMemcpyDeviceToHost));
    kPad<<<blocks(count), kBlock, 0, stream_>>>(from, count, x_, x0_, v_, w_, phase_, psi_, csrStart_, total, sleepOk_, degree_, initDeg_, markPrev_);
    PCUDA(cudaStreamSynchronize(stream_));
}

int ParticleWorld::appendFluid(const std::vector<float3>& pos, const std::vector<float3>& vel, float density) {
    PCUDA(cudaStreamSynchronize(stream_));
    const int k = int(std::min<size_t>(pos.size(), size_t(cap_ - n_)));
    if (k <= 0) return 0;
    std::vector<float4> x(k), v(k);
    std::vector<float> w(size_t(k), 1.0f / (density * s_ * s_ * s_));
    std::vector<uint32_t> ph(size_t(k), PF_FLUID);
    for (int i = 0; i < k; ++i) {
        x[i] = make_float4(pos[i].x, pos[i].y, pos[i].z, 0.0f);
        v[i] = i < int(vel.size()) ? make_float4(vel[i].x, vel[i].y, vel[i].z, 0.0f) : make_float4(0, 0, 0, 0);
    }
    PCUDA(cudaMemcpy(x_ + n_, x.data(), k * sizeof(float4), cudaMemcpyHostToDevice));
    PCUDA(cudaMemcpy(x0_ + n_, x.data(), k * sizeof(float4), cudaMemcpyHostToDevice));
    PCUDA(cudaMemcpy(v_ + n_, v.data(), k * sizeof(float4), cudaMemcpyHostToDevice));
    PCUDA(cudaMemcpy(w_ + n_, w.data(), k * sizeof(float), cudaMemcpyHostToDevice));
    PCUDA(cudaMemcpy(phase_ + n_, ph.data(), k * sizeof(uint32_t), cudaMemcpyHostToDevice));
    const std::vector<float> one(size_t(k), 1.0f);
    PCUDA(cudaMemcpy(psi_ + n_, one.data(), k * sizeof(float), cudaMemcpyHostToDevice));
    PCUDA(cudaMemset(sleepOk_ + n_, 0, size_t(k)));   // liquid never sleeps
    PCUDA(cudaMemset(degree_ + n_, 0, size_t(k) * sizeof(int)));
    PCUDA(cudaMemset(initDeg_ + n_, 0, size_t(k) * sizeof(int)));
    // the bond lists of the new particles are empty
    std::vector<int> cs(size_t(k), 0);
    int last = 0;
    PCUDA(cudaMemcpy(&last, csrStart_ + n_, sizeof(int), cudaMemcpyDeviceToHost));
    std::fill(cs.begin(), cs.end(), last);
    PCUDA(cudaMemcpy(csrStart_ + n_ + 1, cs.data(), k * sizeof(int), cudaMemcpyHostToDevice));
    n_ += k;
    if (n_ > nPad_ || n_ == nPad_) pad(n_);   // past the padding: a new block (the graph is captured again)
    return k;
}

void ParticleWorld::placeGroup(int group, float3 offset, float3 velocity) {
    if (group <= 0 || group >= int(hgroups_.size())) return;
    const HostGroup& G = hgroups_[group];
    // the rest centre: the mean of the host positions
    double cx = 0, cy = 0, cz = 0;
    for (int i = G.first; i < G.first + G.count; ++i) { cx += hx_[i].x; cy += hx_[i].y; cz += hx_[i].z; }
    const float3 c0 = make_float3(float(cx / G.count), float(cy / G.count), float(cz / G.count));
    kPlace<<<blocks(G.count), kBlock, 0, stream_>>>(G.first, G.count, x_, x0_, v_, q_, c0 + offset, velocity);
    const float4 fr[2] = {make_float4(c0.x + offset.x, c0.y + offset.y, c0.z + offset.z, 0.0f), make_float4(0, 0, 0, 1)};
    PCUDA(cudaMemcpyAsync(groupFrame_ + 2 * group, fr, sizeof(fr), cudaMemcpyHostToDevice, stream_));
    PCUDA(cudaStreamSynchronize(stream_));
}

int ParticleWorld::breakBondsInSphere(float3 c, float r) {
    if (!nb_) return 0;
    int* cnt = dalloc<int>(1);
    kBreakSphere<<<blocks(nb_), kBlock, 0, stream_>>>(nb_, bEnds_, x_, bBroken_, c, r * r, cnt);
    PCUDA(cudaStreamSynchronize(stream_));
    int h = 0;
    PCUDA(cudaMemcpy(&h, cnt, sizeof(int), cudaMemcpyDeviceToHost));
    cudaFree(cnt);
    return h;
}

void ParticleWorld::exportPositions(float3* dst, cudaStream_t s) const { kExport<<<blocks(n_), kBlock, 0, s>>>(n_, x_, phase_, dst, nullptr); }
void ParticleWorld::exportDisplay(float* dst, cudaStream_t s) const { kExport<<<blocks(n_), kBlock, 0, s>>>(n_, x_, phase_, nullptr, dst); }

void ParticleWorld::readPositions(std::vector<float3>& out) const {
    std::vector<float4> t(static_cast<size_t>(n_));
    PCUDA(cudaMemcpy(t.data(), x_, size_t(n_) * sizeof(float4), cudaMemcpyDeviceToHost));
    out.resize(size_t(n_));
    for (int i = 0; i < n_; ++i) out[i] = make_float3(t[i].x, t[i].y, t[i].z);
}
void ParticleWorld::readVelocities(std::vector<float3>& out) const {
    std::vector<float4> t(static_cast<size_t>(n_));
    PCUDA(cudaMemcpy(t.data(), v_, size_t(n_) * sizeof(float4), cudaMemcpyDeviceToHost));
    out.resize(size_t(n_));
    for (int i = 0; i < n_; ++i) out[i] = make_float3(t[i].x, t[i].y, t[i].z);
}
void ParticleWorld::readDensity(std::vector<float>& out) const {
    out.resize(size_t(n_));
    PCUDA(cudaMemcpy(out.data(), rho_, size_t(n_) * sizeof(float), cudaMemcpyDeviceToHost));
}
void ParticleWorld::readFluid(std::vector<uint8_t>& out) const {
    std::vector<uint32_t> p(static_cast<size_t>(n_));
    PCUDA(cudaMemcpy(p.data(), phase_, size_t(n_) * sizeof(uint32_t), cudaMemcpyDeviceToHost));
    out.resize(size_t(n_));
    for (int i = 0; i < n_; ++i) out[i] = (p[i] & PF_FLUID) ? 1 : 0;
}
std::vector<uint8_t> ParticleWorld::readBroken() const {
    std::vector<uint8_t> b(static_cast<size_t>(nb_));
    if (nb_) PCUDA(cudaMemcpy(b.data(), bBroken_, size_t(nb_), cudaMemcpyDeviceToHost));
    return b;
}

std::vector<int2> ParticleWorld::readBrokenEnds() const {
    std::vector<int2> out;
    if (!nb_) return out;
    const std::vector<uint8_t> b = readBroken();
    std::vector<int2> e(static_cast<size_t>(nb_));
    PCUDA(cudaMemcpy(e.data(), bEnds_, size_t(nb_) * sizeof(int2), cudaMemcpyDeviceToHost));
    for (int k = 0; k < nb_; ++k)
        if (b[k]) out.push_back(e[k]);
    return out;
}

PStats ParticleWorld::stats() {
    PStats s;
    s.particles = n_;
    s.bonds = nb_;
    s.ms = lastMs_;
    if (!n_) return s;
    float h[8];
    PCUDA(cudaMemcpy(h, stat_, sizeof(h), cudaMemcpyDeviceToHost));
    s.densityMean = h[1] > 0.0f ? h[0] / h[1] : 0.0f;
    s.densityMax = h[2];
    s.maxSpeed = h[3];
    s.neighbourOverflow = int(h[4]);
    s.fluid = int(h[5]);
    if (nb_) {
        const std::vector<uint8_t> b = readBroken();
        for (uint8_t x : b) s.broken += x;
    }
    return s;
}
