// Scene 5 "Koy": metering, the FFT sea surface and its caustics (see koy_kernels.h).
#include "scenes/koy/koy_kernels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

#define KCHECK(x)                                                                                       \
    do {                                                                                                \
        cudaError_t e_ = (x);                                                                           \
        if (e_ != cudaSuccess) {                                                                        \
            std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_)); \
            std::exit(1);                                                                               \
        }                                                                                               \
    } while (0)

// =====================================================================================================
// metering
// =====================================================================================================
int koyMeterTiles(int w, int h) { return ((w + 31) / 32) * ((h + 31) / 32); }

__global__ void meterKernel(const float4* hdr, int w, int h, float* tiles) {
    __shared__ float part[256];
    const int tx = blockIdx.x, ty = blockIdx.y;
    float s = 0.0f;
    int n = 0;
    for (int k = threadIdx.x; k < 1024; k += 256) {
        const int x = tx * 32 + (k & 31), y = ty * 32 + (k >> 5);
        if (x >= w || y >= h) continue;
        const float4 c = hdr[size_t(y) * w + x];
        s += log2f(fmaxf(0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z, 1e-9f));
        ++n;
    }
    part[threadIdx.x] = n ? s / n : 0.0f;
    __syncthreads();
    for (int st = 128; st > 0; st >>= 1) {   // fixed order: deterministic
        if (threadIdx.x < st) part[threadIdx.x] += part[threadIdx.x + st];
        __syncthreads();
    }
    if (threadIdx.x == 0) tiles[ty * gridDim.x + tx] = part[0] / 256.0f;
}
void koyMeter(const float4* hdr, int w, int h, float* tiles, cudaStream_t s) {
    meterKernel<<<dim3((w + 31) / 32, (h + 31) / 32), 256, 0, s>>>(hdr, w, h, tiles);
}

__global__ void sampleHeightsKernel(cudaTextureObject_t tex, float inv, const float2* pts, int n, float* out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    out[i] = tex2DLod<float4>(tex, pts[i].x * inv, pts[i].y * inv, 0.0f).x;
}
void koySampleHeights(unsigned long long surface, float patch, const float2* pts, int n, float* out, cudaStream_t s) {
    if (n <= 0 || !surface) return;
    sampleHeightsKernel<<<(n + 127) / 128, 128, 0, s>>>(cudaTextureObject_t(surface), 1.0f / patch, pts, n, out);
}
__global__ void blendKernel(float4* acc, const float4* src, size_t n, float keep, float w) {
    const size_t i = size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float4 a = acc[i], b = src[i];
    acc[i] = make_float4(a.x * keep + b.x * w, a.y * keep + b.y * w, a.z * keep + b.z * w, a.w * keep + b.w * w);
}
void koyBlend(float4* acc, const float4* src, size_t n, float keep, float w, cudaStream_t s) {
    blendKernel<<<unsigned((n + 255) / 256), 256, 0, s>>>(acc, src, n, keep, w);
}

// =====================================================================================================
// the FFT: radix-2 in shared memory, one row (or column) per block; inverse transform without the 1/N
// =====================================================================================================
__device__ __forceinline__ float2 cmul(float2 a, float2 b) { return make_float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
__device__ __forceinline__ unsigned int bitrev(unsigned int x, int bits) { return __brev(x) >> (32 - bits); }

// data: fields of N*N complex; blockIdx.x = line, blockIdx.y = field; stride 1 (rows) or N (columns)
__global__ void fftLines(float2* data, int N, int bits, bool columns) {
    extern __shared__ float2 sh[];
    float2* base = data + size_t(blockIdx.y) * N * N;
    const int line = blockIdx.x;
    for (int i = threadIdx.x; i < N; i += blockDim.x) {
        const int src = columns ? i * N + line : line * N + i;
        sh[bitrev(i, bits)] = base[src];
    }
    __syncthreads();
    for (int len = 2; len <= N; len <<= 1) {
        const int half = len >> 1;
        for (int b = threadIdx.x; b < N / 2; b += blockDim.x) {
            const int group = b / half, j = b % half;
            const int i0 = group * len + j, i1 = i0 + half;
            float sn, cs;
            sincospif(2.0f * j / len, &sn, &cs);   // inverse: e^{+2 pi i j / len}
            const float2 t = cmul(make_float2(cs, sn), sh[i1]);
            const float2 u = sh[i0];
            sh[i0] = make_float2(u.x + t.x, u.y + t.y);
            sh[i1] = make_float2(u.x - t.x, u.y - t.y);
        }
        __syncthreads();
    }
    for (int i = threadIdx.x; i < N; i += blockDim.x) {
        const int dst = columns ? i * N + line : line * N + i;
        base[dst] = sh[i];
    }
}

// the spectra at time t: height, and its slopes (i kx H, i kz H)
__global__ void oceanSpectrum(const float2* h0, const float* omega, float2* spec, int N, float patch, float t) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N * N) return;
    const int m = i % N, n = i / N;
    const int mm = (N - m) % N, nn = (N - n) % N;   // -k
    const float kx = 2.0f * M_PIf / patch * float(m < N / 2 ? m : m - N);
    const float kz = 2.0f * M_PIf / patch * float(n < N / 2 ? n : n - N);
    const float w = omega[i] * t;
    float sn, cs;
    sincosf(w, &sn, &cs);
    const float2 a = h0[i], b = h0[nn * N + mm];
    // H = h0(k) e^{i w t} + conj(h0(-k)) e^{-i w t}
    const float2 H = make_float2(a.x * cs - a.y * sn + b.x * cs - b.y * sn, a.x * sn + a.y * cs - (b.x * sn + b.y * cs));
    spec[i] = H;
    spec[size_t(N) * N + i] = make_float2(-kx * H.y, kx * H.x);
    spec[size_t(2) * N * N + i] = make_float2(-kz * H.y, kz * H.x);
}

__global__ void oceanPack(const float2* spec, float4* field, int N, cudaSurfaceObject_t level0) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N * N) return;
    const float h = spec[i].x, sx = spec[size_t(N) * N + i].x, sz = spec[size_t(2) * N * N + i].x;
    const float4 v = make_float4(h, sx, sz, 0.5f * (sx * sx + sz * sz));
    field[i] = v;
    surf2Dwrite(v, level0, (i % N) * int(sizeof(float4)), i / N);
}
__global__ void mipDown(cudaSurfaceObject_t src, cudaSurfaceObject_t dst, int n) {   // n: the destination's size
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= n || y >= n) return;
    float4 a = surf2Dread<float4>(src, (2 * x) * int(sizeof(float4)), 2 * y), b = surf2Dread<float4>(src, (2 * x + 1) * int(sizeof(float4)), 2 * y);
    float4 c = surf2Dread<float4>(src, (2 * x) * int(sizeof(float4)), 2 * y + 1), d = surf2Dread<float4>(src, (2 * x + 1) * int(sizeof(float4)), 2 * y + 1);
    const float4 m = make_float4(0.25f * (a.x + b.x + c.x + d.x), 0.25f * (a.y + b.y + c.y + d.y), 0.25f * (a.z + b.z + c.z + d.z),
                                 0.25f * (a.w + b.w + c.w + d.w));
    surf2Dwrite(m, dst, x * int(sizeof(float4)), y);
}

// =====================================================================================================
// caustics: refract a dense grid of the sun's rays through the surface, splat where they cross each depth slice
// =====================================================================================================
__device__ __forceinline__ float fresnelT(float ci, float eta) {   // air -> water transmission, eta = n_t / n_i
    ci = fminf(fmaxf(ci, 0.0f), 1.0f);
    const float s2 = (1.0f - ci * ci) / (eta * eta);
    const float ct = sqrtf(fmaxf(0.0f, 1.0f - s2));
    const float rs = (ci - eta * ct) / (ci + eta * ct), rp = (eta * ci - ct) / (eta * ci + ct);
    return 1.0f - 0.5f * (rs * rs + rp * rp);
}
__device__ __forceinline__ float4 fieldBilinear(const float4* f, int N, float u, float v) {   // u, v in grid units, periodic
    const float fu = floorf(u), fv = floorf(v);
    const float a = u - fu, b = v - fv;
    const int x0 = ((int(fu) % N) + N) % N, y0 = ((int(fv) % N) + N) % N, x1 = (x0 + 1) % N, y1 = (y0 + 1) % N;
    const float4 p = f[y0 * N + x0], q = f[y0 * N + x1], r = f[y1 * N + x0], s = f[y1 * N + x1];
    const float w00 = (1 - a) * (1 - b), w10 = a * (1 - b), w01 = (1 - a) * b, w11 = a * b;
    return make_float4(p.x * w00 + q.x * w10 + r.x * w01 + s.x * w11, p.y * w00 + q.y * w10 + r.y * w01 + s.y * w11,
                       p.z * w00 + q.z * w10 + r.z * w01 + s.z * w11, 0.0f);
}
struct CausticArgs {
    const float4* field;
    int fieldN;
    float patch;
    float* acc;
    int N, rays, slices;
    float lz0, lz1;              // log depth range
    float3 sunAir;
    float eta;
    float tFlat, sunY;
};
__device__ __forceinline__ float sliceDepthD(const CausticArgs& a, int k) {
    return expf(a.lz0 + (a.lz1 - a.lz0) * float(k) / float(a.slices - 1));
}
__global__ void causticSplat(CausticArgs a) {
    const int r = blockIdx.x * blockDim.x + threadIdx.x;
    if (r >= a.rays * a.rays) return;
    const int rx = r % a.rays, rz = r / a.rays;
    // a fixed jitter inside the ray's cell (the same every frame): the regular grid's moire becomes grain
    unsigned int hs = unsigned(r) * 0x9E3779B1u;
    hs ^= hs >> 15; hs *= 0x2C1B3C6Du; hs ^= hs >> 12; hs *= 0x297A2D39u; hs ^= hs >> 15;
    const float jx = float(hs & 0xFFFFu) / 65536.0f, jz = float(hs >> 16) / 65536.0f;
    const float x0 = (rx + jx) * a.patch / a.rays, z0 = (rz + jz) * a.patch / a.rays;
    const float g = float(a.fieldN) / a.patch;
    const float4 w = fieldBilinear(a.field, a.fieldN, x0 * g - 0.5f, z0 * g - 0.5f);
    const float3 m = normalize(make_float3(-w.y, 1.0f, -w.z));
    const float3 din = make_float3(-a.sunAir.x, -a.sunAir.y, -a.sunAir.z);   // the sunlight, going down
    const float ci = -dot(din, m);
    if (ci <= 0.0f) return;
    // refract into the water (eta = 1/n relative index for the formula below)
    const float e = 1.0f / a.eta;
    const float k = 1.0f - e * e * (1.0f - ci * ci);
    const float3 t = din * e + m * (e * ci - sqrtf(fmaxf(k, 0.0f)));
    if (t.y >= -1e-4f) return;
    // flux of this ray's cell relative to a flat surface: the tilted patch's projected area towards the sun and its transmission
    const float wgt = (dot(a.sunAir, m) / m.y) * fresnelT(ci, a.eta) / (a.sunY * a.tFlat) * (float(a.N) / a.rays) * (float(a.N) / a.rays);
    const float inv = float(a.N) / a.patch;
    for (int s = 0; s < a.slices; ++s) {
        const float z = sliceDepthD(a, s);
        const float tt = z / -t.y;
        const float px = (x0 + t.x * tt) * inv - 0.5f, pz = (z0 + t.z * tt) * inv - 0.5f;
        const float fx = floorf(px), fz = floorf(pz);
        const float ax = px - fx, az = pz - fz;
        const int ix = ((int(fx) % a.N) + a.N) % a.N, iz = ((int(fz) % a.N) + a.N) % a.N;
        const int jx = (ix + 1) % a.N, jz = (iz + 1) % a.N;
        float* slice = a.acc + size_t(s) * a.N * a.N;
        atomicAdd(slice + iz * a.N + ix, wgt * (1 - ax) * (1 - az));
        atomicAdd(slice + iz * a.N + jx, wgt * ax * (1 - az));
        atomicAdd(slice + jz * a.N + ix, wgt * (1 - ax) * az);
        atomicAdd(slice + jz * a.N + jx, wgt * ax * az);
    }
}
// =====================================================================================================
// caustics by beams (A1, docs/koy/PLAN_AMFORA.md §5.1): every cell of a regular grid on the surface is a beam of
// sunlight. At each depth slice the refracted rays through its four corners land somewhere; the cell's flux spreads
// evenly over the quad they span (two triangles, each with its own density = flux / area), and every texel receives the
// part of each triangle's area it holds, exactly (box coverage). Nothing is counted, so there is no noise: a beam that
// focuses is a small, bright triangle, one that spreads a large, dim one, and where the beams fold over each other the
// line is sharp by itself. The flux of the whole slice is conserved to rounding.
// =====================================================================================================
// Catmull-Rom weights at fraction t
__device__ __forceinline__ void crWeights(float t, float w[4]) {
    const float t2 = t * t, t3 = t2 * t;
    w[0] = 0.5f * (-t3 + 2.0f * t2 - t);
    w[1] = 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
    w[2] = 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
    w[3] = 0.5f * (t3 - t2);
}
// the surface's slopes at (u, v) in grid units (periodic), bicubic: the slopes stay smooth between the samples, so the
// beams' densities do not step at the field's own cells
__device__ __forceinline__ float2 slopeBicubic(const float4* f, int N, float u, float v) {
    const float fu = floorf(u), fv = floorf(v);
    float wu[4], wv[4];
    crWeights(u - fu, wu);
    crWeights(v - fv, wv);
    const int x0 = int(fu) - 1, y0 = int(fv) - 1;
    float2 s = make_float2(0.0f, 0.0f);
    for (int j = 0; j < 4; ++j) {
        const int y = ((y0 + j) % N + N) % N;
        float2 r = make_float2(0.0f, 0.0f);
        for (int i = 0; i < 4; ++i) {
            const int x = ((x0 + i) % N + N) % N;
            const float4 q = f[y * N + x];
            r.x += wu[i] * q.y;
            r.y += wu[i] * q.z;
        }
        s.x += wv[j] * r.x;
        s.y += wv[j] * r.y;
    }
    return s;
}
struct BeamPrepArgs {
    const float4* field;
    int fieldN;
    float patch;
    int M;
    float4* vtx;
    float3 sunAir;
    float eta, tFlat, sunY;
};
// per grid vertex: the refracted ray's horizontal travel per metre of depth, and the flux per unit of horizontal area
// relative to a flat surface (the tilted patch's projected area towards the sun and its Fresnel transmission)
__global__ void beamPrep(BeamPrepArgs a) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= a.M * a.M) return;
    const int vx = i % a.M, vz = i / a.M;
    const float g = float(a.fieldN) / float(a.M);
    const float2 sl = slopeBicubic(a.field, a.fieldN, vx * g - 0.5f, vz * g - 0.5f);
    const float3 m = normalize(make_float3(-sl.x, 1.0f, -sl.y));
    const float3 din = make_float3(-a.sunAir.x, -a.sunAir.y, -a.sunAir.z);
    const float ci = fmaxf(-dot(din, m), 0.0f);
    const float e = 1.0f / a.eta;
    const float k = 1.0f - e * e * (1.0f - ci * ci);
    float3 t = din * e + m * (e * ci - sqrtf(fmaxf(k, 0.0f)));
    t.y = fminf(t.y, -1e-3f);
    const float w = ci > 0.0f ? (dot(a.sunAir, m) / m.y) * fresnelT(ci, a.eta) / (a.sunY * a.tFlat) : 0.0f;
    a.vtx[i] = make_float4(t.x / -t.y, t.z / -t.y, w, 0.0f);
}
// Green: the signed area of a polygon left of x = X and below y = Y is the sum over its edges of the integral of
// min(x, X) dy along the part of the edge below Y (counter-clockwise positive). One edge p -> q of it:
__device__ __forceinline__ float edgeQ(float2 p, float2 q, float X, float Y) {
    if (p.y >= Y && q.y >= Y) return 0.0f;
    if (p.y > Y) p = make_float2(p.x + (Y - p.y) / (q.y - p.y) * (q.x - p.x), Y);
    else if (q.y > Y) q = make_float2(p.x + (Y - p.y) / (q.y - p.y) * (q.x - p.x), Y);
    const float dy = q.y - p.y;
    if (p.x <= X && q.x <= X) return 0.5f * (p.x + q.x) * dy;
    if (p.x >= X && q.x >= X) return X * dy;
    const float ym = p.y + (X - p.x) / (q.x - p.x) * dy;   // where the edge crosses x = X
    return p.x < X ? 0.5f * (p.x + X) * (ym - p.y) + X * (q.y - ym) : X * (ym - p.y) + 0.5f * (X + q.x) * (q.y - ym);
}
__device__ __forceinline__ float cross2(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }
__device__ __forceinline__ float dot2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }
__device__ __forceinline__ void depositTexel(float* slice, int N, int x, int y, float v) {
    atomicAdd(slice + size_t(y & (N - 1)) * N + (x & (N - 1)), v);
}
// a triangle too thin to divide its area reliably (where beams fold): its flux along its longest side, a few bilinear
// splats (the thin triangle is that line)
__device__ void depositLine(float* slice, int N, int ox, int oy, float2 a, float2 b, float2 c, float f) {
    const float lab = dot2(b - a, b - a), lbc = dot2(c - b, c - b), lca = dot2(a - c, a - c);
    float2 p = a, q = b;
    if (lbc >= lab && lbc >= lca) { p = b; q = c; }
    else if (lca >= lab && lca >= lbc) { p = c; q = a; }
    const int n = min(16, 1 + int(ceilf(sqrtf(dot2(q - p, q - p)))));
    const float fs = f / float(n);
    for (int k = 0; k < n; ++k) {
        const float2 s = p + (q - p) * ((k + 0.5f) / float(n)) - make_float2(0.5f, 0.5f);   // texel centres at +0.5
        const float fx = floorf(s.x), fy = floorf(s.y);
        const float ax = s.x - fx, ay = s.y - fy;
        const int ix = ox + int(fx), iy = oy + int(fy);
        depositTexel(slice, N, ix, iy, fs * (1.0f - ax) * (1.0f - ay));
        depositTexel(slice, N, ix + 1, iy, fs * ax * (1.0f - ay));
        depositTexel(slice, N, ix, iy + 1, fs * (1.0f - ax) * ay);
        depositTexel(slice, N, ix + 1, iy + 1, fs * ax * ay);
    }
}
// the integral of min(x, X) dy along a segment (xa, ya) -> (xb, yb)
__device__ __forceinline__ float segI(float xa, float ya, float xb, float yb, float X) {
    const float dy = yb - ya;
    if (xa <= X && xb <= X) return 0.5f * (xa + xb) * dy;
    if (xa >= X && xb >= X) return X * dy;
    const float ym = ya + (X - xa) / (xb - xa) * dy;   // where it crosses x = X
    return xa < X ? 0.5f * (xa + X) * (ym - ya) + X * (yb - ym) : X * (ym - ya) + 0.5f * (X + xb) * (yb - ym);
}
// One quad P0 -> P1 -> P2 -> P3 in local texel coordinates (the lowest x and y in [0, 1); the local texel (i, j) is the
// slice's (ox + i, oy + j)), split into T1 = (P0, P1, P2) with flux f1 and T2 = (P0, P2, P3) with f2. By Green, the flux
// of a row left of a line x = X is G(X) = sum over the edges of w * integral of min(x, X) dy along the edge's part in the
// row, where the weights are r1 = f1 / A1 on T1's own edges, r2 on T2's, r2 - r1 on the diagonal they share (signed
// areas: a folded triangle works the same). A texel gets G's difference at its two sides. Row by row, only the columns
// the row's edges span are visited: the cost goes with the quad's area and perimeter, not its bounding box.
__device__ void rasterQuad(float* slice, int N, int ox, int oy, float2 P0, float2 P1, float2 P2, float2 P3, float f1, float f2) {
    const float A1 = 0.5f * cross2(P1 - P0, P2 - P0), A2 = 0.5f * cross2(P2 - P0, P3 - P0);
    // a triangle too thin for its coverages to be divided by its area reliably goes to the line deposit
    const float thin = 1e-3f;
    float r1 = 0.0f, r2 = 0.0f;
    if (fabsf(A1) > thin) r1 = f1 / A1; else depositLine(slice, N, ox, oy, P0, P1, P2, f1);
    if (fabsf(A2) > thin) r2 = f2 / A2; else depositLine(slice, N, ox, oy, P0, P2, P3, f2);
    if (r1 == 0.0f && r2 == 0.0f) return;
    const float2 ea[5] = {P0, P1, P2, P3, P0};
    const float2 eb[5] = {P1, P2, P3, P0, P2};
    float ew[5] = {r1, r1, r2, r2, r2 - r1};
    float es[5];
#pragma unroll
    for (int e = 0; e < 5; ++e) {   // dx / dy per edge; a level edge adds nothing to any row
        const float dy = eb[e].y - ea[e].y;
        es[e] = dy != 0.0f ? (eb[e].x - ea[e].x) / dy : 0.0f;
        if (dy == 0.0f) ew[e] = 0.0f;
    }
    const float ymax = fmaxf(fmaxf(P0.y, P1.y), fmaxf(P2.y, P3.y));
    const int ny = max(1, int(ceilf(ymax)));
    for (int j = 0; j < ny; ++j) {
        const float y0 = float(j), y1 = float(j + 1);
        float sxa[5], sya[5], sxb[5], syb[5];
        float xlo = 3.0e38f, xhi = -3.0e38f, tot = 0.0f;
#pragma unroll
        for (int e = 0; e < 5; ++e) {
            const float ya = fminf(fmaxf(ea[e].y, y0), y1), yb = fminf(fmaxf(eb[e].y, y0), y1);
            const bool in = ya != yb && ew[e] != 0.0f;
            const float xa = ea[e].x + (ya - ea[e].y) * es[e], xb = ea[e].x + (yb - ea[e].y) * es[e];
            sxa[e] = in ? xa : 0.0f;
            sxb[e] = in ? xb : 0.0f;
            sya[e] = in ? ya : 0.0f;
            syb[e] = in ? yb : 0.0f;
            if (in) {
                xlo = fminf(xlo, fminf(xa, xb));
                xhi = fmaxf(xhi, fmaxf(xa, xb));
                tot += ew[e] * 0.5f * (xa + xb) * (yb - ya);
            }
        }
        if (xlo > xhi) continue;
        const int c0 = int(floorf(xlo)), c1 = max(c0 + 1, int(ceilf(xhi)));
        float Gp = 0.0f;
        for (int c = c0; c < c1; ++c) {
            float G = tot;
            if (c + 1 < c1) {
                const float X = float(c + 1);
                G = 0.0f;
#pragma unroll
                for (int e = 0; e < 5; ++e) G += ew[e] * segI(sxa[e], sya[e], sxb[e], syb[e], X);
            }
            const float v = G - Gp;
            Gp = G;
            if (v != 0.0f) depositTexel(slice, N, ox + c, oy + j, v);
        }
    }
}
struct BeamArgs {
    const float4* vtx;
    int M;                       // the vertex grid's side (the finest beams)
    int stride;                  // this launch's beams: every stride-th vertex (a coarser grid of M / stride cells)
    float* acc;                  // the slices at this launch's resolution: slice k at acc + k Ns^2
    int Ns;
    int slices, k0, k1;          // k0 .. k1 - 1: the slices this launch fills (consecutive, one resolution)
    float lz0, lz1;
    float texPerCell;            // a cell's side in this resolution's texels at the surface
    float texPerMetre;           // Ns / patch
    unsigned long long* stats;   // optional, per slice: cells, texels of their windows, split beams, thin triangles
    int debug;                   // tests: 1 the quad's centroid splatted bilinearly (the raster's cost isolated)
};
__global__ void __launch_bounds__(128, 8) causticBeams(BeamArgs a) {
    const int Mc = a.M / a.stride;
    const int c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= Mc * Mc) return;
    const int cx = c % Mc, cz = c / Mc;
    const int vx0 = cx * a.stride, vz0 = cz * a.stride;
    const int vx1 = ((cx + 1) % Mc) * a.stride, vz1 = ((cz + 1) % Mc) * a.stride;
    const float4 v00 = a.vtx[vz0 * a.M + vx0], v10 = a.vtx[vz0 * a.M + vx1], v11 = a.vtx[vz1 * a.M + vx1], v01 = a.vtx[vz1 * a.M + vx0];
    const float h = a.texPerCell;
    // each triangle's flux in texel^2 (1 per texel of a flat surface): half the cell at its corners' mean weight. The
    // diagonal alternates like a chessboard, so the split has no preferred direction
    const bool alt = ((cx ^ cz) & 1) != 0;
    const float base = h * h * 0.5f / 3.0f;
    float f1, f2;
    if (!alt) { f1 = base * (v00.z + v10.z + v11.z); f2 = base * (v00.z + v11.z + v01.z); }
    else { f1 = base * (v10.z + v11.z + v01.z); f2 = base * (v10.z + v01.z + v00.z); }
    const float X0 = float(cx) * h, Z0 = float(cz) * h;   // the cell's corner in texels (exact: h is a power of two)
    {   // one slice per block row: a slice (4 MB) is filled while it sits in the L2 cache (a thread looping over all the
        // slices spread its writes over all of them and ran ~30 % slower)
        const int k = a.k0 + int(blockIdx.y);
        if (k >= a.k1) return;
        // the slice's depth times texels per metre: the corners' travel in texels
        const float zt = expf(a.lz0 + (a.lz1 - a.lz0) * float(k) / float(a.slices - 1)) * a.texPerMetre;
        // the corners relative to v00's landing point: differences first (precision), the cell's own size added
        const float2 o = make_float2(X0 + zt * v00.x, Z0 + zt * v00.y);
        const float2 q10 = make_float2(h + zt * (v10.x - v00.x), zt * (v10.y - v00.y));
        const float2 q11 = make_float2(h + zt * (v11.x - v00.x), h + zt * (v11.y - v00.y));
        const float2 q01 = make_float2(zt * (v01.x - v00.x), h + zt * (v01.y - v00.y));
        const float mx = fminf(fminf(0.0f, q10.x), fminf(q11.x, q01.x)), mz = fminf(fminf(0.0f, q10.y), fminf(q11.y, q01.y));
        const float bx = floorf(o.x + mx), bz = floorf(o.y + mz);
        const float2 l = make_float2(o.x - bx, o.y - bz);   // v00's landing point in the local frame
        const int ox = int(bx), oz = int(bz);
        float* slice = a.acc + size_t(k) * a.Ns * a.Ns;
        const float2 p00 = l, p10 = l + q10, p11 = l + q11, p01 = l + q01;
        if (a.stats) {
            const float spanX = fmaxf(fmaxf(p00.x, p10.x), fmaxf(p11.x, p01.x)), spanZ = fmaxf(fmaxf(p00.y, p10.y), fmaxf(p11.y, p01.y));
            const int nx = max(1, int(ceilf(spanX))), ny = max(1, int(ceilf(spanZ)));
            const float A1 = 0.5f * cross2(p10 - p00, p11 - p00), A2 = 0.5f * cross2(p11 - p00, p01 - p00);
            atomicAdd(a.stats + 6 * k + 0, 1ull);
            atomicAdd(a.stats + 6 * k + 1, (unsigned long long)(nx * ny));
            if (fabsf(A1) <= 1e-3f || fabsf(A2) <= 1e-3f) atomicAdd(a.stats + 6 * k + 3, 1ull);
            atomicAdd(a.stats + 6 * k + 4, (unsigned long long)((fabsf(A1) + fabsf(A2)) * 1024.0f));
            const float per = sqrtf(dot2(p10 - p00, p10 - p00)) + sqrtf(dot2(p11 - p10, p11 - p10)) + sqrtf(dot2(p01 - p11, p01 - p11)) +
                              sqrtf(dot2(p00 - p01, p00 - p01));
            atomicAdd(a.stats + 6 * k + 5, (unsigned long long)(per * 1024.0f));
        }
        if (a.debug == 1) {
            const float2 cc = (p00 + p10 + p11 + p01) * 0.25f - make_float2(0.5f, 0.5f);
            const float fx = floorf(cc.x), fy = floorf(cc.y), ax = cc.x - fx, ay = cc.y - fy, f = f1 + f2;
            depositTexel(slice, a.Ns, ox + int(fx), oz + int(fy), f * (1 - ax) * (1 - ay));
            depositTexel(slice, a.Ns, ox + int(fx) + 1, oz + int(fy), f * ax * (1 - ay));
            depositTexel(slice, a.Ns, ox + int(fx), oz + int(fy) + 1, f * (1 - ax) * ay);
            depositTexel(slice, a.Ns, ox + int(fx) + 1, oz + int(fy) + 1, f * ax * ay);
            return;
        }
        // one call site, the chessboard's vertex order by selects (two call sites diverged every warp in half)
        const float2 qa = alt ? p10 : p00, qb = alt ? p11 : p10, qc = alt ? p01 : p11, qd = alt ? p00 : p01;
        rasterQuad(slice, a.Ns, ox, oz, qa, qb, qc, qd, f1, f2);
    }
}
// a half-resolution slice brought up to the full one (bilinear, periodic): the shallow slices that only the light shafts
// read, and the deepest, where the sun's disc blurs wider than two texels anyway
__global__ void upsampleSlices(const float* half, cudaSurfaceObject_t full, int N, int k0, int k1) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y, k = k0 + blockIdx.z;
    if (x >= N || y >= N || k >= k1) return;
    const int n = N / 2;
    const float u = (x + 0.5f) * 0.5f - 0.5f, v = (y + 0.5f) * 0.5f - 0.5f;
    const float fu = floorf(u), fv = floorf(v), au = u - fu, av = v - fv;
    const int x0 = (int(fu) + n) & (n - 1), y0 = (int(fv) + n) & (n - 1), x1 = (x0 + 1) & (n - 1), y1 = (y0 + 1) & (n - 1);
    const float* p = half + size_t(k) * n * n;
    surf3Dwrite((p[y0 * n + x0] * (1 - au) + p[y0 * n + x1] * au) * (1 - av) + (p[y1 * n + x0] * (1 - au) + p[y1 * n + x1] * au) * av, full,
                x * int(sizeof(float)), y, k);
}

// separable periodic Gaussian per slice; sigma per slice in texels
__global__ void blurSlices(const float* src, float* dst, int N, int slices, const float* sigma, bool vertical, cudaSurfaceObject_t out = 0) {
    // N a power of two (the wrap is a mask); the kernel's weights once per block, normalised, in shared memory
    __shared__ float w[2 * 64 + 2];   // the taps, then the normalisation
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y;
    const int s = blockIdx.z;
    if (s >= slices) return;
    const float sg = sigma[s];
    if (sg <= 0.0f) return;   // not this buffer's slice: left as it is
    const int R = min(min(int(ceilf(3.0f * sg)), N / 2 - 1), 64);
    for (int k = threadIdx.x; k <= 2 * R; k += blockDim.x) w[k] = expf(-0.5f * float((k - R) * (k - R)) / (sg * sg));
    __syncthreads();
    if (threadIdx.x == 0) {
        float t = 0.0f;
        for (int k = 0; k <= 2 * R; ++k) t += w[k];
        w[2 * R + 1] = 1.0f / t;
    }
    __syncthreads();
    if (x >= N || y >= N) return;
    const float* p = src + size_t(s) * N * N;
    const int m = N - 1;
    float sum = 0.0f;
    if (vertical)
        for (int k = -R; k <= R; ++k) sum += p[size_t((y + k) & m) * N + x] * w[k + R];
    else
        for (int k = -R; k <= R; ++k) sum += p[size_t(y) * N + ((x + k) & m)] * w[k + R];
    if (out) surf3Dwrite(sum * w[2 * R + 1], out, x * int(sizeof(float)), y, s);   // straight into the 3D texture
    else dst[size_t(s) * N * N + size_t(y) * N + x] = sum * w[2 * R + 1];
}
// the pyramid for the shafts' march: a level from the one above it (2 x 2 mean), surface to surface, slices k0 .. k1 - 1
__global__ void halveSurf(cudaSurfaceObject_t src, cudaSurfaceObject_t dst, int n, int k0, int k1) {   // n: the destination's size
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y, k = k0 + int(blockIdx.z);
    if (x >= n || y >= n || k >= k1) return;
    const int b = int(sizeof(float));
    const float v = surf3Dread<float>(src, 2 * x * b, 2 * y, k) + surf3Dread<float>(src, (2 * x + 1) * b, 2 * y, k) +
                    surf3Dread<float>(src, 2 * x * b, 2 * y + 1, k) + surf3Dread<float>(src, (2 * x + 1) * b, 2 * y + 1, k);
    surf3Dwrite(0.25f * v, dst, x * b, y, k);
}
// a half-resolution slice as it is into the pyramid's level 1 (no detour through the full resolution)
__global__ void bufferToSurf(const float* src, cudaSurfaceObject_t dst, int n, int k0, int k1) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y, k = k0 + int(blockIdx.z);
    if (x >= n || y >= n || k >= k1) return;
    surf3Dwrite(src[size_t(k) * n * n + size_t(y) * n + x], dst, x * int(sizeof(float)), y, k);
}

// =====================================================================================================
// host
// =====================================================================================================
static float jonswap(float w, float wind, float fetchM, float gamma) {
    const float g = 9.81f;
    const float X = g * fetchM / (wind * wind);
    const float alpha = 0.076f * powf(X, -0.22f);
    const float wp = 22.0f * (g / wind) * powf(X, -0.33f);   // peak angular frequency, rad/s (Hasselmann et al. 1973)
    const float sig = w <= wp ? 0.07f : 0.09f;
    const float r = expf(-sqr(w - wp) / (2.0f * sig * sig * wp * wp));
    return alpha * g * g / powf(w, 5.0f) * expf(-1.25f * powf(wp / w, 4.0f)) * powf(gamma, r);
}

float KoyOcean::sliceDepth(int k) const {
    return std::exp(std::log(cset.z0) + (std::log(cset.z1) - std::log(cset.z0)) * float(k) / float(cset.slices - 1));
}

bool KoyOcean::init(const KoyOceanSettings& o, const KoyCausticSettings& c, float3 sunAir, float waterN, float sunRadiusAir) {
    oset = o;
    cset = c;
    sunAir_ = sunAir;
    waterN_ = waterN;
    sunRadiusAir_ = sunRadiusAir;
    const int N = o.N;
    // ---- the spectrum (host, fixed seed: the same sea every time)
    std::vector<float2> h0(size_t(N) * N);
    std::vector<float> om(size_t(N) * N);
    std::mt19937 rng(o.seed);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    const float dk = 2.0f * 3.14159265f / o.patch;
    const float w0 = 2.0f * 3.14159265f / o.loopSeconds;
    const float kCut = 2.0f * 3.14159265f / o.minWave;
    const float kLow = o.maxWave > 0.0f ? 2.0f * 3.14159265f / o.maxWave : 0.0f;
    const float wdir = o.windDirDeg * 3.14159265f / 180.0f;
    // directional spreading cos^(2s)((th - wdir)/2), normalised over the circle
    double dnorm = 0.0;
    for (int k = 0; k < 3600; ++k) dnorm += std::pow(std::fabs(std::cos(0.5 * (k * 2.0 * 3.14159265 / 3600.0))), 2.0 * o.spread) * (2.0 * 3.14159265 / 3600.0);
    double resolvedVar = 0.0, heightVar = 0.0;
    for (int n = 0; n < N; ++n)
        for (int m = 0; m < N; ++m) {
            const size_t i = size_t(n) * N + m;
            const float kx = dk * float(m < N / 2 ? m : m - N), kz = dk * float(n < N / 2 ? n : n - N);
            const float k = std::sqrt(kx * kx + kz * kz);
            const float a = nd(rng), b = nd(rng);
            if (k < 1e-6f || k > kCut || k < kLow) { h0[i] = make_float2(0, 0); om[i] = 0.0f; continue; }
            const float w = std::sqrt(9.81f * k);
            const float th = std::atan2(kz, kx);
            const float D = float(std::pow(std::fabs(std::cos(0.5 * (th - wdir))), 2.0 * o.spread) / dnorm);
            const float Sw = jonswap(w, o.wind, o.fetchKm * 1000.0f, o.gamma);
            const float Phi = Sw * (9.81f / (2.0f * w)) * D / k;   // per (kx, kz)
            const float amp = std::sqrt(0.5f * Phi * dk * dk) * o.amplitude;
            h0[i] = make_float2(a * amp, b * amp);
            om[i] = w0 * std::round(w / w0);
            // the field is Hermitian, H(k) = h0(k) e^{iwt} + conj(h0(-k)) e^{-iwt}: every h0 shows up twice (at k and at
            // -k), so the field holds twice the sum of |h0|^2 (2 amp^2 each). The ripples' strength is set from these
            // sums; booked once, they came out at twice their slope variance (koy --test dalga measures the field)
            heightVar += 4.0 * double(amp) * amp;
            resolvedVar += 4.0 * double(amp) * amp * 0.5 * k * k;   // per axis
        }
    // the ripple layer: the spectrum's shape kept, its strength set by the slope variance it is to carry
    if (o.targetVar > 0.0f && resolvedVar > 0.0) {
        const float s = float(std::sqrt(double(o.targetVar) / resolvedVar));
        for (auto& v : h0) v = make_float2(v.x * s, v.y * s);
        heightVar *= double(s) * s;
        resolvedVar = o.targetVar;
    }
    // the swell: long waves at the patch's own harmonics (so the patch stays periodic), deterministic
    {
        const int harm[3][2] = {{0, 2}, {1, 3}, {-1, 3}};   // (m, n): wavelengths 8, 5.1, 5.1 m along ~z
        const float amp[3] = {o.swellAmplitude, 0.6f * o.swellAmplitude, 0.5f * o.swellAmplitude};
        for (int s = 0; s < 3 && o.swellAmplitude > 0.0f; ++s) {
            const int m = (harm[s][0] + N) % N, n = (harm[s][1] + N) % N;
            const size_t i = size_t(n) * N + m;
            const float kx = dk * float(harm[s][0]), kz = dk * float(harm[s][1]);
            const float k = std::sqrt(kx * kx + kz * kz);
            const float w = std::sqrt(9.81f * k);
            const float ph = 1.7f + 2.3f * s;
            h0[i] = make_float2(h0[i].x + 0.5f * amp[s] * std::cos(ph), h0[i].y + 0.5f * amp[s] * std::sin(ph));
            om[i] = w0 * std::round(w / w0);
            heightVar += 0.5 * double(amp[s]) * amp[s];
            resolvedVar += 0.5 * double(amp[s]) * amp[s] * 0.5 * k * k;
        }
    }
    hmax_ = float(4.5 * std::sqrt(heightVar)) + 0.02f;
    // the slopes the grid does not hold (Cox & Munk 1954: total mean square slope 0.003 + 5.12e-3 U, both axes)
    const float coxMunk = 0.003f + 0.00512f * o.wind;
    varFloor_ = o.unresolvedVar >= 0.0f ? o.unresolvedVar : std::max(0.5f * coxMunk - float(resolvedVar), 0.0004f);
    varResolved_ = float(resolvedVar);
    std::printf("%s: %dx%d / %.0f m, Hs %.3f m, |h| < %.3f m, egim varyansi cozulen %.4f + cozulmeyen %.4f (eksen basina)\n", o.name, N, N,
                o.patch, 4.0 * std::sqrt(heightVar), hmax_, resolvedVar, varFloor_);
    KCHECK(cudaMalloc(&h0_, h0.size() * sizeof(float2)));
    KCHECK(cudaMemcpy(h0_, h0.data(), h0.size() * sizeof(float2), cudaMemcpyHostToDevice));
    KCHECK(cudaMalloc(&omega_, om.size() * sizeof(float)));
    KCHECK(cudaMemcpy(omega_, om.data(), om.size() * sizeof(float), cudaMemcpyHostToDevice));
    KCHECK(cudaMalloc(&spec_, size_t(3) * N * N * sizeof(float2)));
    KCHECK(cudaMalloc(&field_, size_t(N) * N * sizeof(float4)));
    // ---- the surface texture: a mip pyramid of (h, sx, sz, mean square slope)
    surfLevelCount_ = int(std::log2(float(N))) + 1;
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<float4>();
    KCHECK(cudaMallocMipmappedArray(&surfArr_, &fd, make_cudaExtent(N, N, 0), surfLevelCount_, cudaArraySurfaceLoadStore));
    for (int l = 0; l < surfLevelCount_; ++l) {
        cudaArray_t a = nullptr;
        KCHECK(cudaGetMipmappedArrayLevel(&a, surfArr_, l));
        cudaResourceDesc rd{};
        rd.resType = cudaResourceTypeArray;
        rd.res.array.array = a;
        cudaSurfaceObject_t so = 0;
        KCHECK(cudaCreateSurfaceObject(&so, &rd));
        surfLevels_.push_back(so);
    }
    {
        cudaResourceDesc rd{};
        rd.resType = cudaResourceTypeMipmappedArray;
        rd.res.mipmap.mipmap = surfArr_;
        cudaTextureDesc td{};
        td.addressMode[0] = td.addressMode[1] = cudaAddressModeWrap;
        td.filterMode = cudaFilterModeLinear;
        td.mipmapFilterMode = cudaFilterModeLinear;
        td.readMode = cudaReadModeElementType;
        td.normalizedCoords = 1;
        td.maxMipmapLevelClamp = float(surfLevelCount_ - 1);
        td.maxAnisotropy = 1;
        cudaTextureObject_t t = 0;
        KCHECK(cudaCreateTextureObject(&t, &rd, &td, nullptr));
        surfTex_ = t;
    }
    // ---- the caustic volume: level 0 and the pyramid, each a 3D array (x, z periodic; depth clamped)
    const size_t vox = size_t(c.N) * c.N * c.slices;
    KCHECK(cudaMalloc(&caus_, vox * sizeof(float)));
    KCHECK(cudaMalloc(&causTmp_, vox * sizeof(float)));
    KCHECK(cudaMalloc(&dSig_, size_t(2) * c.slices * sizeof(float)));
    {   // the beams' buffers always: the panel switches between the two methods live
        KCHECK(cudaMalloc(&beamVtx_, size_t(c.beams) * c.beams * sizeof(float4)));
        KCHECK(cudaMalloc(&causHalf_, size_t(c.N / 2) * (c.N / 2) * c.slices * sizeof(float)));
    }
    int n = c.N;
    for (int l = 0; l < c.levels && n >= 32; ++l, n /= 2) {
        cudaChannelFormatDesc f1 = cudaCreateChannelDesc<float>();
        cudaArray_t arr = nullptr;
        KCHECK(cudaMalloc3DArray(&arr, &f1, make_cudaExtent(n, n, c.slices), cudaArraySurfaceLoadStore));
        causArr_.push_back(arr);
        cudaResourceDesc rd{};
        rd.resType = cudaResourceTypeArray;
        rd.res.array.array = arr;
        cudaTextureDesc td{};
        td.addressMode[0] = td.addressMode[1] = cudaAddressModeWrap;
        td.addressMode[2] = cudaAddressModeClamp;
        td.filterMode = cudaFilterModeLinear;
        td.readMode = cudaReadModeElementType;
        td.normalizedCoords = 1;
        cudaTextureObject_t t = 0;
        KCHECK(cudaCreateTextureObject(&t, &rd, &td, nullptr));
        causTex_.push_back(t);
        cudaSurfaceObject_t so = 0;
        KCHECK(cudaCreateSurfaceObject(&so, &rd));
        causSurf_.push_back(so);
    }
    // the refracted sun
    {
        const float ci = sunAir.y, e = 1.0f / waterN;
        const float k = 1.0f - e * e * (1.0f - ci * ci);
        const float3 din = make_float3(-sunAir.x, -sunAir.y, -sunAir.z);
        const float3 m = make_float3(0, 1, 0);
        const float3 t = din * e + m * (e * ci - std::sqrt(std::max(k, 0.0f)));
        sunWater_ = make_float3(-t.x, -t.y, -t.z);
    }
    KCHECK(cudaEventCreate(&e0_));
    KCHECK(cudaEventCreate(&e1_));
    KCHECK(cudaEventCreate(&e2_));
    KCHECK(cudaEventCreate(&eb0_));
    KCHECK(cudaEventCreate(&eb1_));
    KCHECK(cudaEventCreate(&e1b_));
    return true;
}

void KoyOcean::update(float t, cudaStream_t s, bool caustics) {
    const int N = oset.N;
    const int bits = int(std::log2(float(N)));
    KCHECK(cudaEventRecord(e0_, s));
    oceanSpectrum<<<(N * N + 255) / 256, 256, 0, s>>>(h0_, omega_, spec_, N, oset.patch, t);
    fftLines<<<dim3(N, 3), 256, N * sizeof(float2), s>>>(spec_, N, bits, false);
    fftLines<<<dim3(N, 3), 256, N * sizeof(float2), s>>>(spec_, N, bits, true);
    oceanPack<<<(N * N + 255) / 256, 256, 0, s>>>(spec_, field_, N, surfLevels_[0]);
    for (int l = 1; l < surfLevelCount_; ++l) {
        const int n = std::max(1, N >> l);
        mipDown<<<dim3((n + 15) / 16, (n + 15) / 16), dim3(16, 16), 0, s>>>(surfLevels_[l - 1], surfLevels_[l], n);
    }
    KCHECK(cudaEventRecord(e1_, s));
    if (caustics) {
        const KoyCausticSettings& c = cset;
        const size_t vox = size_t(c.N) * c.N * c.slices;
        if (c.method != 1 || !beamVtx_) KCHECK(cudaMemsetAsync(caus_, 0, vox * sizeof(float), s));   // beams: their slices below
        CausticArgs a{};
        a.field = field_;
        a.fieldN = N;
        a.patch = oset.patch;
        a.acc = caus_;
        a.N = c.N;
        a.rays = c.rays;
        a.slices = c.slices;
        a.lz0 = std::log(c.z0);
        a.lz1 = std::log(c.z1);
        a.sunAir = sunAir_;
        a.eta = waterN_;
        {
            const float ci = sunAir_.y, e = waterN_;
            const float s2 = (1.0f - ci * ci) / (e * e), ct = std::sqrt(std::max(0.0f, 1.0f - s2));
            const float rs = (ci - e * ct) / (ci + e * ct), rp = (e * ci - ct) / (e * ci + ct);
            a.tFlat = 1.0f - 0.5f * (rs * rs + rp * rp);
        }
        a.sunY = sunAir_.y;
        // the sun's disc blurs every slice by depth (radius r_w z / cos, a uniform disc: sigma = radius / 2), in texels of
        // the slice's own resolution
        const float rw = sunRadiusAir_ / waterN_ * (sunAir_.y / std::max(sunWater_.y, 0.05f)) * 0.5f + sunRadiusAir_ / waterN_ * 0.5f;
        auto sigmaAt = [&](int k, int level) {
            const float radius = rw * sliceDepth(k) / std::max(sunWater_.y, 0.05f);
            return std::max(0.5f * radius * float(c.N >> level) / oset.patch, c.minBlurTexels);
        };
        // beams: three groups of consecutive slices. Above fineZ0 only the light shafts read the caustics (nothing stands
        // that high): half resolution from every other beam. Below fineZ1 the sun's disc blurs wider than two texels:
        // half resolution, all the beams (the folds there come from fine detail). Between them: full resolution
        int groupK[4] = {0, 0, c.slices, c.slices};
        if (c.method == 1 && beamVtx_) {
            while (groupK[1] < c.slices && sliceDepth(groupK[1]) < c.fineZ0) ++groupK[1];
            groupK[2] = groupK[1];
            while (groupK[2] < c.slices && sliceDepth(groupK[2]) <= c.fineZ1) ++groupK[2];
        }
        const int groupLevel[3] = {1, 0, 1}, groupStride[3] = {2, 1, 1};
        std::vector<float> sig(size_t(2) * c.slices, -1.0f);   // [0, slices): the full buffer's blur, then the half's; < 0 skip
        for (int g = 0; g < 3; ++g)
            for (int k = groupK[g]; k < groupK[g + 1]; ++k) sig[size_t(groupLevel[g]) * c.slices + k] = sigmaAt(k, groupLevel[g]);
        if (c.method != 1 || !beamVtx_)
            for (int k = 0; k < c.slices; ++k) sig[k] = sigmaAt(k, 0), sig[size_t(c.slices) + k] = -1.0f;
        if (c.method == 1 && beamVtx_) {
            const int nh = c.N / 2;
            // only the slices the beams fill: the full-resolution group in caus_, the half ones in causHalf_
            const size_t full = size_t(c.N) * c.N, half = size_t(nh) * nh;
            if (groupK[2] > groupK[1]) KCHECK(cudaMemsetAsync(caus_ + full * groupK[1], 0, full * (groupK[2] - groupK[1]) * sizeof(float), s));
            if (groupK[1] > 0) KCHECK(cudaMemsetAsync(causHalf_, 0, half * groupK[1] * sizeof(float), s));
            if (groupK[3] > groupK[2]) KCHECK(cudaMemsetAsync(causHalf_ + half * groupK[2], 0, half * (groupK[3] - groupK[2]) * sizeof(float), s));
            BeamPrepArgs pa{};
            pa.field = field_;
            pa.fieldN = N;
            pa.patch = oset.patch;
            pa.M = c.beams;
            pa.vtx = beamVtx_;
            pa.sunAir = sunAir_;
            pa.eta = waterN_;
            pa.tFlat = a.tFlat;
            pa.sunY = a.sunY;
            beamPrep<<<(c.beams * c.beams + 255) / 256, 256, 0, s>>>(pa);
            BeamArgs ba{};
            ba.vtx = beamVtx_;
            ba.M = c.beams;
            ba.slices = c.slices;
            ba.lz0 = a.lz0;
            ba.lz1 = a.lz1;
            ba.debug = beamDebug;
            if (beamStatsOn) {
                if (!dStats_) KCHECK(cudaMalloc(&dStats_, size_t(c.slices) * 6 * sizeof(unsigned long long)));
                KCHECK(cudaMemsetAsync(dStats_, 0, size_t(c.slices) * 6 * sizeof(unsigned long long), s));
                ba.stats = beamStatsOn == 1 ? dStats_ : nullptr;   // 2: per-slice timing alone (the counters serialise)
                beamSliceMs.assign(c.slices, 0.0f);
            }
            KCHECK(cudaEventRecord(eb0_, s));
            for (int g = 0; g < 3; ++g) {
                if (groupK[g] >= groupK[g + 1]) continue;
                ba.stride = groupStride[g];
                ba.Ns = c.N >> groupLevel[g];
                ba.acc = groupLevel[g] ? causHalf_ : caus_;
                ba.texPerCell = float(ba.Ns) / float(c.beams / ba.stride);
                ba.texPerMetre = float(ba.Ns) / oset.patch;
                const int cells = (c.beams / ba.stride) * (c.beams / ba.stride);
                if (beamStatsOn) {   // tests: one launch per slice, timed
                    for (int k = groupK[g]; k < groupK[g + 1]; ++k) {
                        ba.k0 = k;
                        ba.k1 = k + 1;
                        KCHECK(cudaEventRecord(e1b_, s));
                        causticBeams<<<dim3((cells + 127) / 128, 1), 128, 0, s>>>(ba);
                        KCHECK(cudaEventRecord(e2_, s));
                        KCHECK(cudaEventSynchronize(e2_));
                        cudaEventElapsedTime(&beamSliceMs[k], e1b_, e2_);
                    }
                } else {
                    ba.k0 = groupK[g];
                    ba.k1 = groupK[g + 1];
                    causticBeams<<<dim3((cells + 127) / 128, groupK[g + 1] - groupK[g]), 128, 0, s>>>(ba);
                }
            }
            KCHECK(cudaEventRecord(eb1_, s));
        } else {
            KCHECK(cudaEventRecord(eb0_, s));
            causticSplat<<<(c.rays * c.rays + 255) / 256, 256, 0, s>>>(a);
            KCHECK(cudaEventRecord(eb1_, s));
        }
        KCHECK(cudaMemcpyAsync(dSig_, sig.data(), sig.size() * sizeof(float), cudaMemcpyHostToDevice, s));
        // blurred straight into the 3D textures: the full-resolution slices into level 0, the half ones kept for level 1
        // and brought up into level 0
        const cudaSurfaceObject_t L0 = cudaSurfaceObject_t(causSurf_[0]);
        blurSlices<<<dim3((c.N + 127) / 128, c.N, c.slices), 128, 0, s>>>(caus_, causTmp_, c.N, c.slices, dSig_, false);
        blurSlices<<<dim3((c.N + 127) / 128, c.N, c.slices), 128, 0, s>>>(causTmp_, nullptr, c.N, c.slices, dSig_, true, L0);
        const bool beams = c.method == 1 && beamVtx_;
        if (beams) {
            const int nh = c.N / 2;
            blurSlices<<<dim3((nh + 127) / 128, nh, c.slices), 128, 0, s>>>(causHalf_, causTmp_, nh, c.slices, dSig_ + c.slices, false);
            blurSlices<<<dim3((nh + 127) / 128, nh, c.slices), 128, 0, s>>>(causTmp_, causHalf_, nh, c.slices, dSig_ + c.slices, true);
            for (int g = 0; g < 3; g += 2)
                if (groupK[g] < groupK[g + 1])
                    upsampleSlices<<<dim3((c.N + 127) / 128, c.N, groupK[g + 1] - groupK[g]), 128, 0, s>>>(causHalf_, L0, c.N, groupK[g], groupK[g + 1]);
        }
        KCHECK(cudaEventRecord(e1b_, s));
        // the pyramid: level 1 of a half-resolution slice is that slice itself, of a full one its 2 x 2 mean; then halving
        int n = c.N / 2;
        for (size_t l = 1; l < causSurf_.size(); ++l, n /= 2) {
            const cudaSurfaceObject_t src = cudaSurfaceObject_t(causSurf_[l - 1]), dst = cudaSurfaceObject_t(causSurf_[l]);
            const dim3 grid((n + 127) / 128, n, 1);
            if (l == 1 && beams) {
                for (int g = 0; g < 3; ++g) {
                    if (groupK[g] >= groupK[g + 1]) continue;
                    const dim3 gg(grid.x, grid.y, groupK[g + 1] - groupK[g]);
                    if (groupLevel[g] == 1) bufferToSurf<<<gg, 128, 0, s>>>(causHalf_, dst, n, groupK[g], groupK[g + 1]);
                    else halveSurf<<<gg, 128, 0, s>>>(src, dst, n, groupK[g], groupK[g + 1]);
                }
            } else {
                halveSurf<<<dim3(grid.x, grid.y, c.slices), 128, 0, s>>>(src, dst, n, 0, c.slices);
            }
        }
    }
    KCHECK(cudaEventRecord(e2_, s));
    KCHECK(cudaEventSynchronize(e2_));
    cudaEventElapsedTime(&msSurface, e0_, e1_);
    cudaEventElapsedTime(&msCaustic, e1_, e2_);
    if (caustics) {
        cudaEventElapsedTime(&msBeams, eb0_, eb1_);
        cudaEventElapsedTime(&msBlur, eb1_, e1b_);
        if (beamStatsOn && dStats_) {
            beamStats.resize(size_t(cset.slices) * 6);
            KCHECK(cudaMemcpy(beamStats.data(), dStats_, beamStats.size() * sizeof(unsigned long long), cudaMemcpyDeviceToHost));
        }
    }
}

void KoyOcean::readHeights(std::vector<float4>& out, cudaStream_t s) {
    out.resize(size_t(oset.N) * oset.N);
    KCHECK(cudaMemcpyAsync(out.data(), field_, out.size() * sizeof(float4), cudaMemcpyDeviceToHost, s));
    KCHECK(cudaStreamSynchronize(s));
}
void KoyOcean::readCaustic(int slice, std::vector<float>& out, cudaStream_t s) {
    // level 0 of the 3D texture itself: what the renderer reads (blurred, the half-resolution slices brought up)
    const size_t n = size_t(cset.N) * cset.N;
    out.resize(n);
    cudaMemcpy3DParms cp{};
    cp.srcArray = causArr_[0];
    cp.srcPos = make_cudaPos(0, 0, size_t(slice));
    cp.dstPtr = make_cudaPitchedPtr(out.data(), size_t(cset.N) * sizeof(float), cset.N, cset.N);
    cp.extent = make_cudaExtent(cset.N, cset.N, 1);
    cp.kind = cudaMemcpyDeviceToHost;
    KCHECK(cudaMemcpy3DAsync(&cp, s));
    KCHECK(cudaStreamSynchronize(s));
}
void KoyOcean::release() {
    cudaDeviceSynchronize();
    for (auto so : surfLevels_) cudaDestroySurfaceObject(so);
    surfLevels_.clear();
    if (surfTex_) cudaDestroyTextureObject(cudaTextureObject_t(surfTex_));
    for (auto t : causTex_) cudaDestroyTextureObject(cudaTextureObject_t(t));
    causTex_.clear();
    for (auto a : causArr_) cudaFreeArray(a);
    causArr_.clear();
    for (auto so : causSurf_) cudaDestroySurfaceObject(cudaSurfaceObject_t(so));
    causSurf_.clear();
    if (surfArr_) cudaFreeMipmappedArray(surfArr_);
    cudaFree(h0_);
    cudaFree(omega_);
    cudaFree(spec_);
    cudaFree(field_);
    cudaFree(caus_);
    cudaFree(causTmp_);
    cudaFree(dSig_);
    dSig_ = nullptr;
    cudaFree(beamVtx_);
    beamVtx_ = nullptr;
    cudaFree(causHalf_);
    causHalf_ = nullptr;
    cudaFree(dStats_);
    dStats_ = nullptr;
    for (cudaEvent_t* ev : {&e0_, &e1_, &e2_, &eb0_, &eb1_, &e1b_})
        if (*ev) {
            cudaEventDestroy(*ev);
            *ev = nullptr;
        }
    h0_ = nullptr;
    omega_ = nullptr;
    spec_ = nullptr;
    field_ = nullptr;
    caus_ = causTmp_ = nullptr;
    surfArr_ = nullptr;
    surfTex_ = 0;
}
