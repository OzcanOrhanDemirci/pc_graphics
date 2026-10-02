#include "scenes/tanecik/surface.h"
#include "shared/vec.h"

#include <cub/block/block_reduce.cuh>
#include <cub/block/block_scan.cuh>
#include <cub/device/device_radix_sort.cuh>
#include <cub/device/device_scan.cuh>
#include <cub/device/device_select.cuh>

#include <algorithm>
#include <cmath>
#include <cstdio>

#define SCHECK(x)                                                                                       \
    do {                                                                                                \
        cudaError_t e_ = (x);                                                                           \
        if (e_ != cudaSuccess) {                                                                        \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                               \
        }                                                                                               \
    } while (0)

namespace {

typedef unsigned long long u64;

constexpr u64 kEmpty = ~0ull;
constexpr int kOff = 1 << 20;            // coordinates are stored offset, 21 bits each
constexpr u64 kMask = (1ull << 21) - 1;
constexpr int kBlk3 = 512;               // a field block: 8^3 voxels
constexpr float kScale = 1048576.0f;     // the field in fixed point (2^20 per unit of bulk density)
constexpr int kProbes = 128;
constexpr float kPoly6 = 315.0f / (64.0f * 3.14159265358979f);   // (1 - q^2)^3 over the unit ball, normalised

__device__ __forceinline__ u64 mix64(u64 x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull; x ^= x >> 27; x *= 0x94d049bb133111ebull; x ^= x >> 31;
    return x;
}
__device__ __forceinline__ u64 packKey(int x, int y, int z) {
    return u64(x + kOff) | (u64(y + kOff) << 21) | (u64(z + kOff) << 42);
}
__device__ __forceinline__ int3 unpackKey(u64 k) {
    return make_int3(int(k & kMask) - kOff, int((k >> 21) & kMask) - kOff, int((k >> 42) & kMask) - kOff);
}
// open addressing, linear probing; insert returns the key's slot (new or found), -1 when the table is full
__device__ int tableInsert(u64* keys, unsigned cap, u64 key) {
    unsigned s = unsigned(mix64(key)) & (cap - 1);
    for (int i = 0; i < kProbes; ++i) {
        const u64 prev = atomicCAS(&keys[s], kEmpty, key);
        if (prev == kEmpty || prev == key) return int(s);
        s = (s + 1) & (cap - 1);
    }
    return -1;
}
__device__ int tableFind(const u64* keys, unsigned cap, u64 key) {
    unsigned s = unsigned(mix64(key)) & (cap - 1);
    for (int i = 0; i < kProbes; ++i) {
        const u64 k = keys[s];
        if (k == key) return int(s);
        if (k == kEmpty) return -1;
        s = (s + 1) & (cap - 1);
    }
    return -1;
}

struct Params {
    float spacing, cell, invCell, rn, h, hLone, smoothing, maxStretch, isoF;
    int iso;               // fixed point
    int minNeighbours, anisotropic, projectSteps, dropletSpheres;
    float vol;             // a particle's volume (spacing^3)
    float floorY;          // the mirror (mirror != 0)
    int mirror;
    float bias;            // how far a flat free surface's particle sits from its neighbours' weighted mean (m)
    float flatRatio;       // and how flat its neighbourhood's spread is (shortest / longest axis)
};

// ---- 1. neighbours

__global__ void kKeys(const float3* pos, const float* weight, int n, float invRn, u64* key, int* idx) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float w = weight ? weight[i] : 1.0f;
    const float3 p = pos[i];
    const bool ok = w > 0.0f && isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
    key[i] = ok ? packKey(int(floorf(p.x * invRn)), int(floorf(p.y * invRn)), int(floorf(p.z * invRn))) : kEmpty;
    idx[i] = i;
}
__global__ void kGather(const float3* pos, const float* weight, const u64* key, const int* idx, int n, float4* sp) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const int j = idx[i];
    const float3 p = pos[j];
    const float w = key[i] == kEmpty ? 0.0f : weight ? fminf(weight[j], 1.0f) : 1.0f;
    sp[i] = make_float4(p.x, p.y, p.z, w);
}
// a cell's range in the sorted order, written by its first particle
__global__ void kCells(const u64* key, int n, u64* cellKey, int2* cellRange, unsigned cap) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const u64 k = key[i];
    if (k == kEmpty || (i > 0 && key[i - 1] == k)) return;
    int c = 1;
    while (i + c < n && key[i + c] == k) ++c;
    const int s = tableInsert(cellKey, cap, k);
    if (s >= 0) cellRange[s] = make_int2(i, c);
}

// ---- 2. kernels: centre, shape (G maps an offset to the unit ball), reach (half-extents of its box), amplitude

// eigen decomposition of a symmetric 3x3 (cyclic Jacobi): A becomes diagonal (the eigenvalues), V's columns the vectors
__device__ void jacobi3(float A[3][3], float V[3][3]) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) V[i][j] = i == j ? 1.0f : 0.0f;
    for (int sweep = 0; sweep < 6; ++sweep) {
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                const float apq = A[p][q];
                if (fabsf(apq) < 1e-30f) continue;
                const float theta = (A[q][q] - A[p][p]) / (2.0f * apq);
                const float t = copysignf(1.0f, theta) / (fabsf(theta) + sqrtf(theta * theta + 1.0f));
                const float c = rsqrtf(t * t + 1.0f), s = t * c;
                for (int k = 0; k < 3; ++k) {   // A = J^T A J
                    const float akp = A[k][p], akq = A[k][q];
                    A[k][p] = c * akp - s * akq;
                    A[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {
                    const float apk = A[p][k], aqk = A[q][k];
                    A[p][k] = c * apk - s * aqk;
                    A[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) {
                    const float vkp = V[k][p], vkq = V[k][q];
                    V[k][p] = c * vkp - s * vkq;
                    V[k][q] = s * vkp + c * vkq;
                }
            }
    }
}

__global__ void kKernels(const float4* sp, int n, const u64* cellKey, const int2* cellRange, unsigned cap, Params P, float4* cen, float4* g0,
                         float4* g1, float4* ext, unsigned char* dropFlag, float4* dropIn, float4* nrmOut) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float4 me = sp[i];
    dropFlag[i] = 0;
    if (me.w <= 0.0f) {
        cen[i] = make_float4(0, 0, 0, 0);
        return;
    }
    const float3 p = make_float3(me.x, me.y, me.z);
    const float rn2 = P.rn * P.rn, invRn = 1.0f / P.rn;
    const int cx = int(floorf(p.x * invRn)), cy = int(floorf(p.y * invRn)), cz = int(floorf(p.z * invRn));
    const float touch2 = sqr(1.5f * P.spacing);
    float sw = 0.0f, mx = 0, my = 0, mz = 0, xx = 0, yy = 0, zz = 0, xy = 0, xz = 0, yz = 0;
    float3 touching = make_float3(0, 0, 0);   // the particles touching it (itself included): a droplet's own cluster
    int cnt = 0, nNear = 0;
    // its neighbours, and near the floor their mirror images too (found round its own mirror image)
    const int passes = P.mirror && p.y - P.floorY < P.rn ? 2 : 1;
    for (int pass = 0; pass < passes; ++pass)
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int qy = pass ? int(floorf((2.0f * P.floorY - p.y) * invRn)) : cy;
                const int s = tableFind(cellKey, cap, packKey(cx + dx, qy + dy, cz + dz));
                if (s < 0) continue;
                const int2 r = cellRange[s];
                for (int j = r.x; j < r.x + r.y; ++j) {
                    const float4 q = sp[j];
                    if (q.w <= 0.0f) continue;
                    const float ox = q.x - p.x, oy = (pass ? 2.0f * P.floorY - q.y : q.y) - p.y, oz = q.z - p.z;
                    const float d2 = ox * ox + oy * oy + oz * oz;
                    if (d2 >= rn2) continue;
                    if (d2 < touch2 && !pass) {
                        touching = touching + make_float3(ox, oy, oz);
                        ++nNear;
                    }
                    const float d = sqrtf(d2) * invRn;
                    const float w = 1.0f - d * d * d;
                    sw += w;
                    mx += w * ox; my += w * oy; mz += w * oz;
                    xx += w * ox * ox; yy += w * oy * oy; zz += w * oz * oz;
                    xy += w * ox * oy; xz += w * ox * oz; yz += w * oy * oz;
                    ++cnt;
                }
            }
    const float inv = 1.0f / sw;   // sw >= 1: the particle itself
    mx *= inv; my *= inv; mz *= inv;
    const float3 mean = make_float3(mx, my, mz);
    nrmOut[i] = make_float4(0, 0, 0, 0);
    if (cnt < P.minNeighbours && P.dropletSpheres && P.anisotropic) {   // a droplet: drawn as a sphere, not meshed
        const float3 c = p + touching / float(nNear);
        cen[i] = make_float4(p.x, p.y, p.z, 0.0f);
        dropFlag[i] = 1;
        dropIn[i] = make_float4(c.x, c.y, c.z, 0.5f * P.spacing * cbrtf(float(nNear)));
        return;
    }
    float G[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    float3 e, c;
    float a3;
    if (!P.anisotropic || cnt < P.minNeighbours) {
        const float a = cnt < P.minNeighbours && P.anisotropic ? P.hLone : P.h;
        G[0][0] = G[1][1] = G[2][2] = 1.0f / a;
        e = make_float3(a, a, a);
        a3 = a * a * a;
        c = p + mean * P.smoothing;
    } else {
        float A[3][3] = {{xx * inv - mx * mx, xy * inv - mx * my, xz * inv - mx * mz},
                         {xy * inv - mx * my, yy * inv - my * my, yz * inv - my * mz},
                         {xz * inv - mx * mz, yz * inv - my * mz, zz * inv - mz * mz}};
        float V[3][3];
        jacobi3(A, V);
        float t[3];
        for (int k = 0; k < 3; ++k) t[k] = sqrtf(fmaxf(A[k][k], 1e-14f));
        const int kmin = t[0] <= t[1] && t[0] <= t[2] ? 0 : t[1] <= t[2] ? 1 : 2;
        const float tmax = fmaxf(t[0], fmaxf(t[1], t[2]));
        // the smoothing (Yu & Turk): towards the neighbours' mean, less the pull every particle of a flat free surface
        // has (its neighbours are all on one side): the noise goes, the liquid keeps its volume. How much of that pull
        // to take back: by how flat the neighbourhood is (a surface's is, the bulk's is round) and by how much of the
        // mean points inwards (a thin sheet's mean lies in its plane: nothing to take back)
        float3 nrm = make_float3(V[0][kmin], V[1][kmin], V[2][kmin]);
        if (dot(nrm, mean) > 0.0f) nrm = -nrm;   // outwards
        const float flat = saturate((1.0f - t[kmin] / tmax) / (1.0f - P.flatRatio));
        const float pull = saturate(-dot(mean, nrm) / (0.5f * P.bias));
        c = p + (mean + nrm * (flat * pull * P.bias)) * P.smoothing;
        nrmOut[i] = make_float4(nrm.x, nrm.y, nrm.z, flat);
        // the kernel: stretches from the spreads, the longest at most maxStretch times the shortest, then the volume
        // of a round kernel (the product 1): a flat neighbourhood gives a flat kernel of the same mass
        for (int k = 0; k < 3; ++k) t[k] = fmaxf(t[k], tmax / P.maxStretch);
        const float g = cbrtf(t[0] * t[1] * t[2]);
        float a[3];
        for (int k = 0; k < 3; ++k) a[k] = P.h * t[k] / g;
        for (int r = 0; r < 3; ++r)
            for (int s = 0; s < 3; ++s) G[r][s] = V[r][0] * V[s][0] / a[0] + V[r][1] * V[s][1] / a[1] + V[r][2] * V[s][2] / a[2];
        e = make_float3(sqrtf(sqr(V[0][0] * a[0]) + sqr(V[0][1] * a[1]) + sqr(V[0][2] * a[2])),
                        sqrtf(sqr(V[1][0] * a[0]) + sqr(V[1][1] * a[1]) + sqr(V[1][2] * a[2])),
                        sqrtf(sqr(V[2][0] * a[0]) + sqr(V[2][1] * a[1]) + sqr(V[2][2] * a[2])));
        a3 = P.h * P.h * P.h;
    }
    cen[i] = make_float4(c.x, c.y, c.z, P.vol * me.w * kPoly6 / a3);
    g0[i] = make_float4(G[0][0], G[1][1], G[2][2], G[0][1]);
    g1[i] = make_float4(G[0][2], G[1][2], 0.0f, 0.0f);
    ext[i] = make_float4(e.x, e.y, e.z, 0.0f);
}

// another smoothing pass over the smoothed centres: the same neighbours (by the original positions) and weights, the
// same pull taken back along the normal found from the original positions
__global__ void kSmooth(const float4* sp, int n, const u64* cellKey, const int2* cellRange, unsigned cap, Params P, const float4* nrmFlat,
                        const float4* cenIn, float4* cenOut) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float4 me = sp[i], ci = cenIn[i];
    cenOut[i] = ci;
    if (me.w <= 0.0f || ci.w <= 0.0f) return;
    const float rn2 = P.rn * P.rn, invRn = 1.0f / P.rn;
    const int cx = int(floorf(me.x * invRn)), cy = int(floorf(me.y * invRn)), cz = int(floorf(me.z * invRn));
    float sw = 0.0f;
    float3 m = make_float3(0, 0, 0);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int s = tableFind(cellKey, cap, packKey(cx + dx, cy + dy, cz + dz));
                if (s < 0) continue;
                const int2 r = cellRange[s];
                for (int j = r.x; j < r.x + r.y; ++j) {
                    const float4 q = sp[j];
                    if (q.w <= 0.0f) continue;
                    const float d2 = sqr(q.x - me.x) + sqr(q.y - me.y) + sqr(q.z - me.z);
                    if (d2 >= rn2) continue;
                    const float4 cj = cenIn[j];
                    if (cj.w <= 0.0f) continue;   // a droplet: not part of the body
                    const float d = sqrtf(d2) * invRn;
                    const float w = 1.0f - d * d * d;
                    sw += w;
                    m = m + make_float3(cj.x - ci.x, cj.y - ci.y, cj.z - ci.z) * w;
                }
            }
    m = m / sw;
    const float4 nf = nrmFlat[i];
    const float3 nrm = make_float3(nf.x, nf.y, nf.z);
    const float pull = saturate(-dot(m, nrm) / (0.5f * P.bias));
    const float3 c = make_float3(ci.x, ci.y, ci.z) + (m + nrm * (nf.w * pull * P.bias)) * P.smoothing;
    cenOut[i] = make_float4(c.x, c.y, c.z, ci.w);
}

// ---- 3. blocks and the splat

__device__ __forceinline__ void reach(const float4 c, const float4 e, float invCell, int3& lo, int3& hi) {
    lo = make_int3(int(ceilf((c.x - e.x) * invCell)), int(ceilf((c.y - e.y) * invCell)), int(ceilf((c.z - e.z) * invCell)));
    hi = make_int3(int(floorf((c.x + e.x) * invCell)), int(floorf((c.y + e.y) * invCell)), int(floorf((c.z + e.z) * invCell)));
}

// every block a kernel touches, one voxel more all round: a voxel with any density has all its neighbours allocated,
// so every crossed cell and every quad's four cells exist
// a kernel's mirror image in the floor, when it reaches below it
__device__ __forceinline__ bool mirrored(const Params& P, float4& c, float4 e) {
    if (!P.mirror || c.y - e.y >= P.floorY) return false;
    c.y = 2.0f * P.floorY - c.y;
    return true;
}
__global__ void kAllocBlocks(const float4* cen, const float4* ext, int n, Params P, u64* blkKey, unsigned cap, int* counters) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    float4 c = cen[i];
    if (c.w <= 0.0f) return;
    const float4 e = ext[i];
    for (int pass = 0; pass < 2; ++pass) {
        if (pass && !mirrored(P, c, e)) break;
        int3 lo, hi;
        reach(c, e, P.invCell, lo, hi);
        for (int bz = (lo.z - 1) >> 3; bz <= (hi.z + 1) >> 3; ++bz)
            for (int by = (lo.y - 1) >> 3; by <= (hi.y + 1) >> 3; ++by)
                for (int bx = (lo.x - 1) >> 3; bx <= (hi.x + 1) >> 3; ++bx)
                    if (tableInsert(blkKey, cap, packKey(bx, by, bz)) < 0) counters[1] = 1;
    }
}
__global__ void kCompactBlocks(const u64* blkKey, unsigned cap, u64* list, int* counters) {
    const unsigned s = blockIdx.x * blockDim.x + threadIdx.x;
    if (s >= cap) return;
    const u64 k = blkKey[s];
    if (k != kEmpty) list[atomicAdd(&counters[0], 1)] = k;
}
// a block's index is its rank in coordinate order (the same field and mesh every time); over capacity: -1
__global__ void kAssignBlocks(const u64* sorted, int count, int maxB, const u64* blkKey, unsigned cap, int* blkIdx) {
    const int r = blockIdx.x * blockDim.x + threadIdx.x;
    if (r >= count) return;
    const int s = tableFind(blkKey, cap, sorted[r]);
    if (s >= 0) blkIdx[s] = r < maxB ? r : -1;
}

// one warp per kernel: the (at most 4^3) blocks it touches are looked up once, then its lanes walk its voxels
constexpr int kSplatWarps = 4;
__device__ void splatOne(float4 c, float4 a, float4 b, float4 e, float cell, float invCell, const u64* blkKey, const int* blkIdx, unsigned cap,
                         int* field, int* blk, int lane) {
    int3 lo, hi;
    reach(c, e, invCell, lo, hi);
    const int3 blo = make_int3(lo.x >> 3, lo.y >> 3, lo.z >> 3);
    const int nbx = min((hi.x >> 3) - blo.x + 1, 4), nby = min((hi.y >> 3) - blo.y + 1, 4), nbz = min((hi.z >> 3) - blo.z + 1, 4);
    for (int k = lane; k < nbx * nby * nbz; k += 32) {
        const int bx = blo.x + k % nbx, by = blo.y + (k / nbx) % nby, bz = blo.z + k / (nbx * nby);
        const int s = tableFind(blkKey, cap, packKey(bx, by, bz));
        blk[k] = s >= 0 ? blkIdx[s] : -1;
    }
    __syncwarp();
    const int dx = hi.x - lo.x + 1, dy = hi.y - lo.y + 1, dz = hi.z - lo.z + 1;
    const int total = dx * dy * dz;
    for (int v = lane; v < total; v += 32) {
        const int vx = lo.x + v % dx, vy = lo.y + (v / dx) % dy, vz = lo.z + v / (dx * dy);
        const float ox = vx * cell - c.x, oy = vy * cell - c.y, oz = vz * cell - c.z;
        const float qx = a.x * ox + a.w * oy + b.x * oz, qy = a.w * ox + a.y * oy + b.y * oz, qz = b.x * ox + b.y * oy + a.z * oz;
        const float q2 = qx * qx + qy * qy + qz * qz;
        if (q2 >= 1.0f) continue;
        const float u = 1.0f - q2;
        const int iv = __float2int_rn(c.w * u * u * u * kScale);
        if (iv <= 0) continue;
        const int bxi = (vx >> 3) - blo.x, byi = (vy >> 3) - blo.y, bzi = (vz >> 3) - blo.z;
        if (bxi >= nbx || byi >= nby || bzi >= nbz) continue;
        const int bi = blk[bxi + nbx * (byi + nby * bzi)];
        if (bi < 0) continue;
        atomicAdd(&field[bi * kBlk3 + ((vz & 7) * 8 + (vy & 7)) * 8 + (vx & 7)], iv);
    }
    __syncwarp();
}
__global__ void kSplat(const float4* cen, const float4* g0, const float4* g1, const float4* ext, int n, Params P, const u64* blkKey, const int* blkIdx,
                       unsigned cap, int* field) {
    __shared__ int blk[kSplatWarps][64];
    const int warp = threadIdx.x >> 5, lane = threadIdx.x & 31;
    const int i = blockIdx.x * kSplatWarps + warp;
    if (i >= n) return;
    float4 c = cen[i];
    if (c.w <= 0.0f) return;
    float4 a = g0[i], b = g1[i];   // G: xx yy zz xy | xz yz
    const float4 e = ext[i];
    splatOne(c, a, b, e, P.cell, P.invCell, blkKey, blkIdx, cap, field, blk[warp], lane);
    if (mirrored(P, c, e)) {   // the mirror image: G's xy and yz change sign
        a.w = -a.w;
        b.y = -b.y;
        splatOne(c, a, b, e, P.cell, P.invCell, blkKey, blkIdx, cap, field, blk[warp], lane);
    }
}

// ---- 4. surface nets over the blocks (a CUDA block of 512 threads per field block, a thread per cell)

// the block's 26 neighbours (and itself), by offset -1..1
__device__ void loadNeighbours(u64 key, const u64* blkKey, const int* blkIdx, unsigned cap, int* nb) {
    const int t = threadIdx.x;
    if (t < 27) {
        const int3 b = unpackKey(key);
        const int s = tableFind(blkKey, cap, packKey(b.x + t % 3 - 1, b.y + (t / 3) % 3 - 1, b.z + t / 9 - 1));
        nb[t] = s >= 0 ? blkIdx[s] : -1;
    }
}
// the field on voxels -1..9 of the block (11^3)
constexpr int kA = 11;
__device__ void loadApron(const int* nb, const int* field, int* ap) {
    for (int v = threadIdx.x; v < kA * kA * kA; v += blockDim.x) {
        const int x = v % kA - 1, y = (v / kA) % kA - 1, z = v / (kA * kA) - 1;
        const int bx = x < 0 ? -1 : x > 7 ? 1 : 0, by = y < 0 ? -1 : y > 7 ? 1 : 0, bz = z < 0 ? -1 : z > 7 ? 1 : 0;
        const int n = nb[(bz + 1) * 9 + (by + 1) * 3 + (bx + 1)];
        ap[v] = n >= 0 ? field[n * kBlk3 + ((z - bz * 8) * 8 + (y - by * 8)) * 8 + (x - bx * 8)] : 0;
    }
}
__device__ __forceinline__ int A(const int* ap, int x, int y, int z) { return ap[((z + 1) * kA + (y + 1)) * kA + (x + 1)]; }

// what a cell holds: bit 0 a vertex (its corners straddle the level), bits 1-3 a quad around its corner's +x, +y, +z
// edge, bit 4 its corner inside. Quads wholly under the floor are left out (their four cells reach a cell above the
// corner at most): the liquid is mirrored in the floor, so its side meets the floor upright and what is left is open
// only there
__global__ void __launch_bounds__(512) kClassify(const u64* blocks, const u64* blkKey, const int* blkIdx, unsigned cap, const int* field, int iso,
                                                 float cell, float floorY, unsigned char* bits, u64* blkCount) {
    __shared__ int nb[27];
    __shared__ int ap[kA * kA * kA];
    typedef cub::BlockReduce<u64, 512> Reduce;
    __shared__ typename Reduce::TempStorage tmp;
    const int b = blockIdx.x;
    loadNeighbours(blocks[b], blkKey, blkIdx, cap, nb);
    __syncthreads();
    loadApron(nb, field, ap);
    __syncthreads();
    const int t = threadIdx.x, i = t & 7, j = (t >> 3) & 7, k = t >> 6;
    int in = 0;
    for (int q = 0; q < 8; ++q) in |= (A(ap, i + (q & 1), j + ((q >> 1) & 1), k + (q >> 2)) > iso) << q;
    unsigned char m = 0;
    const float y = float(unpackKey(blocks[b]).y * 8 + j) * cell;   // the cell's lower corner
    const bool under = y + cell <= floorY;                           // its quads: their cells reach y + cell at most
    if (in != 0 && in != 255 && y + 2.0f * cell > floorY) m |= 1;    // a vertex, unless no kept quad can use it
    const bool c0 = in & 1;
    if (c0 != bool(in & 2) && !under) m |= 2;
    if (c0 != bool(in & 4) && !under) m |= 4;
    if (c0 != bool(in & 16) && !under) m |= 8;
    if (c0) m |= 16;
    bits[b * kBlk3 + t] = m;
    const u64 v = u64(m & 1) | (u64(__popc(m & 14)) << 32);
    const u64 sum = Reduce(tmp).Sum(v);
    if (t == 0) blkCount[b] = sum;
}
__global__ void kTotals(const u64* count, const u64* offset, int nb, int* counters) {
    const u64 tot = nb > 0 ? offset[nb - 1] + count[nb - 1] : 0;
    counters[3] = int(tot & 0xffffffffu);
    counters[4] = int(tot >> 32);
}

__device__ __forceinline__ float trilerp(const float f[8], float x, float y, float z) {
    const float a = f[0] + (f[1] - f[0]) * x, bq = f[2] + (f[3] - f[2]) * x, c = f[4] + (f[5] - f[4]) * x, d = f[6] + (f[7] - f[6]) * x;
    const float e = a + (bq - a) * y, g = c + (d - c) * y;
    return e + (g - e) * z;
}
__device__ __forceinline__ float3 trigrad(const float f[8], float x, float y, float z) {
    const float gx = ((f[1] - f[0]) * (1 - y) + (f[3] - f[2]) * y) * (1 - z) + ((f[5] - f[4]) * (1 - y) + (f[7] - f[6]) * y) * z;
    const float gy = ((f[2] - f[0]) * (1 - x) + (f[3] - f[1]) * x) * (1 - z) + ((f[6] - f[4]) * (1 - x) + (f[7] - f[5]) * x) * z;
    const float gz = ((f[4] - f[0]) * (1 - x) + (f[5] - f[1]) * x) * (1 - y) + ((f[6] - f[2]) * (1 - x) + (f[7] - f[3]) * x) * y;
    return make_float3(gx, gy, gz);
}

__global__ void __launch_bounds__(512) kVertices(const u64* blocks, const u64* blkKey, const int* blkIdx, unsigned cap, const int* field, Params P,
                                                 const unsigned char* bits, const u64* blkOffset, int maxV, int* cellVert, float3* vtx, float3* nrm) {
    __shared__ int nb[27];
    __shared__ int ap[kA * kA * kA];
    typedef cub::BlockScan<int, 512> Scan;
    __shared__ typename Scan::TempStorage tmp;
    const int b = blockIdx.x;
    const u64 key = blocks[b];
    loadNeighbours(key, blkKey, blkIdx, cap, nb);
    __syncthreads();
    loadApron(nb, field, ap);
    __syncthreads();
    const int t = threadIdx.x, i = t & 7, j = (t >> 3) & 7, k = t >> 6;
    const unsigned char m = bits[b * kBlk3 + t];
    int local;
    Scan(tmp).ExclusiveSum(int(m & 1), local);
    const int vi = int(blkOffset[b] & 0xffffffffu) + local;
    cellVert[b * kBlk3 + t] = (m & 1) && vi < maxV ? vi : -1;
    if (!(m & 1) || vi >= maxV) return;
    const float inv = 1.0f / kScale;
    float f[8];
    for (int q = 0; q < 8; ++q) f[q] = A(ap, i + (q & 1), j + ((q >> 1) & 1), k + (q >> 2)) * inv;
    // the mean of the crossings on the cell's 12 edges
    const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    float3 s = make_float3(0, 0, 0);
    int n = 0;
    for (int e = 0; e < 12; ++e) {
        const float fa = f[E[e][0]], fb = f[E[e][1]];
        if ((fa > P.isoF) == (fb > P.isoF)) continue;
        const float u = (P.isoF - fa) / (fb - fa);
        const float3 pa = make_float3(float(E[e][0] & 1), float((E[e][0] >> 1) & 1), float(E[e][0] >> 2));
        const float3 pb = make_float3(float(E[e][1] & 1), float((E[e][1] >> 1) & 1), float(E[e][1] >> 2));
        s = s + pa + (pb - pa) * u;
        ++n;
    }
    float3 x = s / float(max(n, 1));
    // pulled onto the level set of the cell's trilinear field
    for (int it = 0; it < P.projectSteps; ++it) {
        const float3 g = trigrad(f, x.x, x.y, x.z);
        const float g2 = dot(g, g);
        if (g2 < 1e-12f) break;
        x = x - g * ((trilerp(f, x.x, x.y, x.z) - P.isoF) / g2);
        x = make_float3(saturate(x.x), saturate(x.y), saturate(x.z));
    }
    // the normal: central differences at the 8 corners, interpolated (smooth across cells); the field falls outwards
    float gx[8], gy[8], gz[8];
    for (int q = 0; q < 8; ++q) {
        const int cx = i + (q & 1), cy = j + ((q >> 1) & 1), cz = k + (q >> 2);
        gx[q] = float(A(ap, cx + 1, cy, cz) - A(ap, cx - 1, cy, cz));
        gy[q] = float(A(ap, cx, cy + 1, cz) - A(ap, cx, cy - 1, cz));
        gz[q] = float(A(ap, cx, cy, cz + 1) - A(ap, cx, cy, cz - 1));
    }
    float3 g = make_float3(trilerp(gx, x.x, x.y, x.z), trilerp(gy, x.x, x.y, x.z), trilerp(gz, x.x, x.y, x.z));
    if (dot(g, g) < 1e-12f) g = trigrad(f, x.x, x.y, x.z);
    const float gl = sqrtf(dot(g, g));
    const int3 B = unpackKey(key);
    vtx[vi] = make_float3((B.x * 8 + i + x.x) * P.cell, (B.y * 8 + j + x.y) * P.cell, (B.z * 8 + k + x.z) * P.cell);
    nrm[vi] = gl > 0.0f ? g * (-1.0f / gl) : make_float3(0, 1, 0);
}

// the vertex of the cell at (i, j, k) + d, d in -1..0 (a neighbouring block's when it falls outside)
__device__ __forceinline__ int cellVertex(const int* nb, const int* cellVert, int i, int j, int k) {
    const int bx = i < 0 ? -1 : 0, by = j < 0 ? -1 : 0, bz = k < 0 ? -1 : 0;
    const int n = nb[(bz + 1) * 9 + (by + 1) * 3 + (bx + 1)];
    return n >= 0 ? cellVert[n * kBlk3 + ((k - bz * 8) * 8 + (j - by * 8)) * 8 + (i - bx * 8)] : -1;
}

__global__ void __launch_bounds__(512) kTriangles(const u64* blocks, const u64* blkKey, const int* blkIdx, unsigned cap, const unsigned char* bits,
                                                  const u64* blkOffset, const int* cellVert, const float3* vtx, int maxT, uint3* tri, int* counters) {
    __shared__ int nb[27];
    typedef cub::BlockScan<int, 512> Scan;
    __shared__ typename Scan::TempStorage tmp;
    const int b = blockIdx.x;
    loadNeighbours(blocks[b], blkKey, blkIdx, cap, nb);
    __syncthreads();
    const int t = threadIdx.x, i = t & 7, j = (t >> 3) & 7, k = t >> 6;
    const unsigned char m = bits[b * kBlk3 + t];
    int local;
    Scan(tmp).ExclusiveSum(__popc(m & 14), local);
    int q = int(blkOffset[b] >> 32) + local;
    // the four cells round each edge, counter-clockwise about the axis (the quad faces +axis): x (y, z), y (z, x), z (x, y)
    const int C[3][4][3] = {{{0, -1, -1}, {0, 0, -1}, {0, 0, 0}, {0, -1, 0}},
                            {{-1, 0, -1}, {-1, 0, 0}, {0, 0, 0}, {0, 0, -1}},
                            {{-1, -1, 0}, {0, -1, 0}, {0, 0, 0}, {-1, 0, 0}}};
    for (int axis = 0; axis < 3; ++axis) {
        if (!(m & (2 << axis))) continue;
        int v[4];
        bool ok = true;
        for (int c = 0; c < 4; ++c) {
            v[c] = cellVertex(nb, cellVert, i + C[axis][c][0], j + C[axis][c][1], k + C[axis][c][2]);
            ok = ok && v[c] >= 0;
        }
        const int at = q++;
        if (!ok || 2 * at + 1 >= maxT) {
            if (2 * at + 1 < maxT) {   // keep the count honest: a degenerate pair
                tri[2 * at] = make_uint3(0, 0, 0);
                tri[2 * at + 1] = make_uint3(0, 0, 0);
            }
            atomicOr(&counters[2], 1);
            continue;
        }
        if (!(m & 16)) { const int s = v[1]; v[1] = v[3]; v[3] = s; }   // inside beyond the edge: the quad faces -axis
        const float3 p0 = vtx[v[0]], p1 = vtx[v[1]], p2 = vtx[v[2]], p3 = vtx[v[3]];
        const float3 d02 = p2 - p0, d13 = p3 - p1;
        if (dot(d13, d13) < dot(d02, d02)) {   // the shorter diagonal
            tri[2 * at] = make_uint3(v[0], v[1], v[3]);
            tri[2 * at + 1] = make_uint3(v[1], v[2], v[3]);
        } else {
            tri[2 * at] = make_uint3(v[0], v[1], v[2]);
            tri[2 * at + 1] = make_uint3(v[0], v[2], v[3]);
        }
    }
}

unsigned nextPow2(unsigned v) {
    unsigned p = 1;
    while (p < v) p <<= 1;
    return p;
}
template <class T> T* salloc(size_t n, size_t& bytes) {
    void* p = nullptr;
    SCHECK(cudaMalloc(&p, std::max<size_t>(n, 1) * sizeof(T)));
    bytes += std::max<size_t>(n, 1) * sizeof(T);
    return static_cast<T*>(p);
}
int blocksFor(long long n, int t) { return int((n + t - 1) / t); }

// a lone particle's round kernel: its droplet (the level set around it) has the radius rho. Of the two kernel sizes
// that do it, the wider (the field falls gently at the droplet's edge, so a few voxels resolve it)
float loneSupport(float vol, float rho, float iso) {
    const float k = vol * kPoly6 / (rho * rho * rho);   // field = k * x^3 (1 - x^2)^3 with x = rho / h
    float lo = 1e-4f, hi = 0.57735f;                     // the field rises with x up to 1/sqrt(3)
    if (k * std::pow(hi, 3.0f) * std::pow(1.0f - hi * hi, 3.0f) < iso) return rho * std::sqrt(3.0f);   // unreachable: its peak
    for (int it = 0; it < 60; ++it) {
        const float x = 0.5f * (lo + hi);
        const float f = k * x * x * x * std::pow(1.0f - x * x, 3.0f);
        (f < iso ? lo : hi) = x;
    }
    return rho / (0.5f * (lo + hi));
}

// a particle in the top layer of a lattice's flat free surface: the weighted mean of its neighbours (within rn, in
// spacings) lies `bias` spacings below it; and the spread of their positions across the surface / along it
void flatSurface(float rn, float& bias, float& ratio) {
    const int R = int(std::ceil(rn)) + 1;
    double sw = 0, sz = 0, sxx = 0, szz = 0;
    for (int k = -R; k <= 0; ++k)
        for (int j = -R; j <= R; ++j)
            for (int i = -R; i <= R; ++i) {
                const double d = std::sqrt(double(i * i + j * j + k * k)) / rn;
                if (d >= 1.0) continue;
                const double w = 1.0 - d * d * d;
                sw += w;
                sz += w * k;
                sxx += w * i * i;
                szz += w * k * k;
            }
    const double mz = sz / sw;
    bias = float(-mz);
    ratio = float(std::sqrt((szz / sw - mz * mz) / (sxx / sw)));
}

}   // namespace

bool FluidSurface::init(int maxParticles, int maxVertices, int maxTriangles, int maxBlocks) {
    destroy();
    maxP_ = std::max(1, maxParticles);
    maxV_ = std::max(1, maxVertices);
    maxT_ = std::max(1, maxTriangles);
    maxB_ = std::max(1, maxBlocks);
    bytes_ = 0;
    pKey_ = salloc<u64>(maxP_, bytes_);
    pKeySorted_ = salloc<u64>(maxP_, bytes_);
    pIdx_ = salloc<int>(maxP_, bytes_);
    pIdxSorted_ = salloc<int>(maxP_, bytes_);
    sPos_ = salloc<float4>(maxP_, bytes_);
    kCen_ = salloc<float4>(maxP_, bytes_);
    kCen2_ = salloc<float4>(maxP_, bytes_);
    kNrm_ = salloc<float4>(maxP_, bytes_);
    kG0_ = salloc<float4>(maxP_, bytes_);
    kG1_ = salloc<float4>(maxP_, bytes_);
    kExt_ = salloc<float4>(maxP_, bytes_);
    cellCap_ = nextPow2(unsigned(2 * maxP_));
    cellKey_ = salloc<u64>(cellCap_, bytes_);
    cellRange_ = salloc<int2>(cellCap_, bytes_);
    blkCap_ = nextPow2(unsigned(4 * maxB_));
    blkKey_ = salloc<u64>(blkCap_, bytes_);
    blkIdx_ = salloc<int>(blkCap_, bytes_);
    blkList_ = salloc<u64>(blkCap_, bytes_);
    blkListSorted_ = salloc<u64>(blkCap_, bytes_);
    field_ = salloc<int>(size_t(maxB_) * kBlk3, bytes_);
    cellVert_ = salloc<int>(size_t(maxB_) * kBlk3, bytes_);
    cellBits_ = salloc<unsigned char>(size_t(maxB_) * kBlk3, bytes_);
    blkCount_ = salloc<u64>(maxB_, bytes_);
    blkOffset_ = salloc<u64>(maxB_, bytes_);
    counters_ = salloc<int>(8, bytes_);
    SCHECK(cudaMallocHost(&hostCounters_, 8 * sizeof(int)));
    drop_ = salloc<float4>(maxP_, bytes_);
    dropIn_ = salloc<float4>(maxP_, bytes_);
    dropFlag_ = salloc<unsigned char>(maxP_, bytes_);
    vtx_ = salloc<float3>(maxV_, bytes_);
    nrm_ = salloc<float3>(maxV_, bytes_);
    tri_ = salloc<uint3>(maxT_, bytes_);
    size_t a = 0, b = 0, c = 0, d = 0;
    cub::DeviceRadixSort::SortPairs(nullptr, a, pKey_, pKeySorted_, pIdx_, pIdxSorted_, maxP_, 0, 64);
    cub::DeviceRadixSort::SortKeys(nullptr, b, blkList_, blkListSorted_, int(blkCap_), 0, 64);
    cub::DeviceScan::ExclusiveSum(nullptr, c, blkCount_, blkOffset_, maxB_);
    cub::DeviceSelect::Flagged(nullptr, d, dropIn_, dropFlag_, drop_, counters_ + 5, maxP_);
    tempBytes_ = std::max(std::max(a, b), std::max(c, d));
    temp_ = salloc<char>(tempBytes_, bytes_);
    for (cudaEvent_t& e : ev_) SCHECK(cudaEventCreate(&e));
    return true;
}

void FluidSurface::destroy() {
    void* bufs[] = {pKey_, pKeySorted_, pIdx_, pIdxSorted_, sPos_, kCen_, kCen2_, kNrm_, kG0_, kG1_, kExt_, cellKey_, cellRange_, blkKey_, blkIdx_, blkList_,
                    blkListSorted_, field_, cellVert_, cellBits_, blkCount_, blkOffset_, counters_, vtx_, nrm_, tri_, temp_, drop_, dropIn_,
                    dropFlag_};
    for (void* p : bufs)
        if (p) cudaFree(p);
    if (hostCounters_) cudaFreeHost(hostCounters_);
    for (cudaEvent_t& e : ev_)
        if (e) { cudaEventDestroy(e); e = nullptr; }
    pKey_ = pKeySorted_ = nullptr;
    pIdx_ = pIdxSorted_ = nullptr;
    sPos_ = kCen_ = kCen2_ = kNrm_ = kG0_ = kG1_ = kExt_ = nullptr;
    cellKey_ = blkKey_ = blkList_ = blkListSorted_ = nullptr;
    cellRange_ = nullptr;
    blkIdx_ = field_ = cellVert_ = counters_ = hostCounters_ = nullptr;
    cellBits_ = nullptr;
    blkCount_ = blkOffset_ = nullptr;
    vtx_ = nrm_ = nullptr;
    tri_ = nullptr;
    temp_ = nullptr;
    drop_ = dropIn_ = nullptr;
    dropFlag_ = nullptr;
    bytes_ = 0;
    nVerts_ = nTris_ = nBlocks_ = nDrops_ = 0;
}

void FluidSurface::build(const float3* dPos, const float* dWeight, int n, float radius, cudaStream_t st) {
    nVerts_ = nTris_ = nBlocks_ = nDrops_ = 0;
    truncated_ = n > maxP_;
    n = std::min(n, maxP_);
    if (n <= 0 || !vtx_) { lastMs = 0; return; }
    const FluidSurfaceSettings& S = settings;
    Params P;
    P.spacing = 2.0f * radius;
    P.cell = S.cellScale * P.spacing;
    P.invCell = 1.0f / P.cell;
    P.rn = S.neighbourScale * P.spacing;
    P.h = S.supportScale * P.spacing;
    P.vol = P.spacing * P.spacing * P.spacing;
    P.hLone = loneSupport(P.vol, S.dropletRadius * radius, S.iso);
    P.smoothing = S.smoothing;
    P.maxStretch = std::max(1.0f, S.maxStretch);
    P.isoF = S.iso;
    P.iso = int(S.iso * kScale);
    P.minNeighbours = S.minNeighbours;
    P.anisotropic = S.anisotropic ? 1 : 0;
    P.projectSteps = S.projectSteps;
    P.dropletSpheres = S.dropletSpheres ? 1 : 0;
    P.mirror = S.floorY > -1e29f ? 1 : 0;
    P.floorY = S.floorY;
    flatSurface(S.neighbourScale, P.bias, P.flatRatio);
    P.bias *= P.spacing;
    const int T = 256;
    SCHECK(cudaEventRecord(ev_[0], st));
    // 1. neighbours
    kKeys<<<blocksFor(n, T), T, 0, st>>>(dPos, dWeight, n, 1.0f / P.rn, pKey_, pIdx_);
    cub::DeviceRadixSort::SortPairs(temp_, tempBytes_, pKey_, pKeySorted_, pIdx_, pIdxSorted_, n, 0, 64, st);
    kGather<<<blocksFor(n, T), T, 0, st>>>(dPos, dWeight, pKeySorted_, pIdxSorted_, n, sPos_);
    SCHECK(cudaMemsetAsync(cellKey_, 0xff, size_t(cellCap_) * sizeof(u64), st));
    kCells<<<blocksFor(n, T), T, 0, st>>>(pKeySorted_, n, cellKey_, cellRange_, cellCap_);
    SCHECK(cudaEventRecord(ev_[1], st));
    // 2. kernels
    kKernels<<<blocksFor(n, 128), 128, 0, st>>>(sPos_, n, cellKey_, cellRange_, cellCap_, P, kCen_, kG0_, kG1_, kExt_, dropFlag_, dropIn_, kNrm_);
    for (int pass = 1; pass < S.smoothingPasses && S.anisotropic; ++pass) {
        kSmooth<<<blocksFor(n, 128), 128, 0, st>>>(sPos_, n, cellKey_, cellRange_, cellCap_, P, kNrm_, kCen_, kCen2_);
        std::swap(kCen_, kCen2_);
    }
    SCHECK(cudaEventRecord(ev_[2], st));
    // 3. blocks: allocated where kernels reach, compacted, ranked by coordinates
    SCHECK(cudaMemsetAsync(blkKey_, 0xff, size_t(blkCap_) * sizeof(u64), st));
    SCHECK(cudaMemsetAsync(counters_, 0, 8 * sizeof(int), st));
    kAllocBlocks<<<blocksFor(n, T), T, 0, st>>>(kCen_, kExt_, n, P, blkKey_, blkCap_, counters_);
    kCompactBlocks<<<blocksFor(blkCap_, T), T, 0, st>>>(blkKey_, blkCap_, blkList_, counters_);
    cub::DeviceSelect::Flagged(temp_, tempBytes_, dropIn_, dropFlag_, drop_, counters_ + 5, n, st);
    SCHECK(cudaMemcpyAsync(hostCounters_, counters_, 8 * sizeof(int), cudaMemcpyDeviceToHost, st));
    SCHECK(cudaStreamSynchronize(st));
    const int allBlocks = hostCounters_[0];
    nDrops_ = hostCounters_[5];
    if (hostCounters_[1]) truncated_ = true;
    cub::DeviceRadixSort::SortKeys(temp_, tempBytes_, blkList_, blkListSorted_, allBlocks, 0, 64, st);
    kAssignBlocks<<<blocksFor(std::max(allBlocks, 1), T), T, 0, st>>>(blkListSorted_, allBlocks, maxB_, blkKey_, blkCap_, blkIdx_);
    const int nb = std::min(allBlocks, maxB_);
    if (allBlocks > maxB_) truncated_ = true;
    nBlocks_ = nb;
    SCHECK(cudaEventRecord(ev_[3], st));
    // the splat
    SCHECK(cudaMemsetAsync(field_, 0, size_t(nb) * kBlk3 * sizeof(int), st));
    kSplat<<<blocksFor(n, kSplatWarps), 32 * kSplatWarps, 0, st>>>(kCen_, kG0_, kG1_, kExt_, n, P, blkKey_, blkIdx_, blkCap_, field_);
    SCHECK(cudaEventRecord(ev_[4], st));
    // 4. extraction
    if (nb > 0) {
        kClassify<<<nb, 512, 0, st>>>(blkListSorted_, blkKey_, blkIdx_, blkCap_, field_, P.iso, P.cell, S.floorY, cellBits_, blkCount_);
        cub::DeviceScan::ExclusiveSum(temp_, tempBytes_, blkCount_, blkOffset_, nb, st);
        kTotals<<<1, 1, 0, st>>>(blkCount_, blkOffset_, nb, counters_);
        SCHECK(cudaMemcpyAsync(hostCounters_, counters_, 8 * sizeof(int), cudaMemcpyDeviceToHost, st));
        SCHECK(cudaStreamSynchronize(st));
        const int verts = hostCounters_[3], quads = hostCounters_[4];
        if (verts > maxV_ || 2LL * quads > maxT_) truncated_ = true;
        kVertices<<<nb, 512, 0, st>>>(blkListSorted_, blkKey_, blkIdx_, blkCap_, field_, P, cellBits_, blkOffset_, maxV_, cellVert_, vtx_, nrm_);
        kTriangles<<<nb, 512, 0, st>>>(blkListSorted_, blkKey_, blkIdx_, blkCap_, cellBits_, blkOffset_, cellVert_, vtx_, maxT_, tri_, counters_);
        nVerts_ = std::min(verts, maxV_);
        nTris_ = int(std::min<long long>(2LL * quads, maxT_ & ~1));
    }
    SCHECK(cudaEventRecord(ev_[5], st));
    SCHECK(cudaEventSynchronize(ev_[5]));
    SCHECK(cudaMemcpy(hostCounters_ + 2, counters_ + 2, sizeof(int), cudaMemcpyDeviceToHost));
    if (hostCounters_[2]) truncated_ = true;
    SCHECK(cudaEventElapsedTime(&lastMs, ev_[0], ev_[5]));
    SCHECK(cudaEventElapsedTime(&msNeighbours, ev_[0], ev_[1]));
    SCHECK(cudaEventElapsedTime(&msKernels, ev_[1], ev_[2]));
    SCHECK(cudaEventElapsedTime(&msBlocks, ev_[2], ev_[3]));
    SCHECK(cudaEventElapsedTime(&msSplat, ev_[3], ev_[4]));
    SCHECK(cudaEventElapsedTime(&msExtract, ev_[4], ev_[5]));
}
