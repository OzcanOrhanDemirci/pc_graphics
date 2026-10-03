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
// separable periodic Gaussian per slice; sigma per slice in texels
__global__ void blurSlices(const float* src, float* dst, int N, int slices, const float* sigma, bool vertical) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y;
    const int s = blockIdx.z;
    if (x >= N || y >= N || s >= slices) return;
    const float sg = sigma[s];
    const int R = min(int(ceilf(3.0f * sg)), N / 2 - 1);
    const float* p = src + size_t(s) * N * N;
    float sum = 0.0f, wsum = 0.0f;
    for (int k = -R; k <= R; ++k) {
        const float wk = expf(-0.5f * k * k / (sg * sg));
        const int xx = vertical ? x : ((x + k) % N + N) % N;
        const int yy = vertical ? ((y + k) % N + N) % N : y;
        sum += p[yy * N + xx] * wk;
        wsum += wk;
    }
    dst[size_t(s) * N * N + y * N + x] = sum / wsum;
}
__global__ void halveSlices(const float* src, float* dst, int n, int slices) {   // n: the destination's size
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y, s = blockIdx.z;
    if (x >= n || y >= n || s >= slices) return;
    const int N = 2 * n;
    const float* p = src + size_t(s) * N * N;
    dst[size_t(s) * n * n + y * n + x] = 0.25f * (p[(2 * y) * N + 2 * x] + p[(2 * y) * N + 2 * x + 1] + p[(2 * y + 1) * N + 2 * x] + p[(2 * y + 1) * N + 2 * x + 1]);
}

// =====================================================================================================
// seagrass: every leaf's control points from its rest shape and the near-bottom current (K1: kinematic; K3: XPBD)
// =====================================================================================================
__global__ void grassKernel(const KoyLeaf* leaves, int n, int ppl, float t, KoyCurrent cur, float4* pts, float3* nrm) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const KoyLeaf L = leaves[i];
    const float3 up = make_float3(0.0f, 1.0f, 0.0f);
    const float3 h = make_float3(cosf(L.yaw), 0.0f, sinf(L.yaw));
    // the current at this leaf: the swell's orbital velocity, its phase travelling along the swell's direction
    const float u = cur.speed * cosf(cur.omega * t - cur.k * (L.base.x * cur.dir.x + L.base.z * cur.dir.z) + 0.15f * L.phase);
    const float flut = cur.flutter * sinf(2.7f * t + 6.2831853f * L.phase) * sinf(0.9f * t + 3.0f * L.phase);
    // the leaf as a polyline Q0 (base) .. Q[ppl-1] (tip), bent along its length
    const int m = ppl - 1;
    const float ds = L.length / float(m);
    float3 Q[8];
    Q[0] = L.base;
    float3 dirLast = up, tiltLast = h;
    for (int k = 1; k <= m; ++k) {
        const float sg = (float(k) - 0.5f) / float(m);
        // tilt in the horizontal plane: the rest lean grows along the leaf, the current bends the upper part more
        const float3 T = h * (L.lean * powf(sg, 0.7f)) + cur.dir * (u * 6.0f * powf(sg, 1.3f) / L.stiffness) + cross(up, h) * (flut * sg);
        const float lt = length(T);
        const float ang = fminf(lt, 1.45f);
        const float3 Th = lt > 1e-5f ? T / lt : h;
        const float3 d = up * cosf(ang) + Th * sinf(ang);
        Q[k] = Q[k - 1] + d * ds;
        dirLast = d;
        tiltLast = Th;
    }
    // quadratic B-spline points that start at the base and end at the tip: the ends mirrored
    float3 P[8];
    for (int k = 0; k <= m; ++k) P[k] = Q[k];
    P[0] = Q[0] * 2.0f - Q[1];
    P[m] = Q[m] * 2.0f - Q[m - 1];
    // the ribbon faces the way it bends (its flat side to the flow)
    float3 nface = tiltLast - dirLast * dot(tiltLast, dirLast);
    nface = length(nface) > 1e-4f ? normalize(nface) : h;
    for (int k = 0; k <= m; ++k) {
        const float sg = float(k) / float(m);
        const float w = L.halfWidth * (1.0f - 0.35f * sg * sg * sg);
        pts[size_t(i) * ppl + k] = make_float4(P[k].x, P[k].y, P[k].z, w);
        nrm[size_t(i) * ppl + k] = nface;
    }
}
void koyGrassPoints(const KoyLeaf* leaves, int n, int pointsPerLeaf, float t, KoyCurrent cur, float4* pts, float3* nrm, cudaStream_t s) {
    if (n <= 0) return;
    grassKernel<<<(n + 127) / 128, 128, 0, s>>>(leaves, n, pointsPerLeaf, t, cur, pts, nrm);
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
            if (k < 1e-6f || k > kCut) { h0[i] = make_float2(0, 0); om[i] = 0.0f; continue; }
            const float w = std::sqrt(9.81f * k);
            const float th = std::atan2(kz, kx);
            const float D = float(std::pow(std::fabs(std::cos(0.5 * (th - wdir))), 2.0 * o.spread) / dnorm);
            const float Sw = jonswap(w, o.wind, o.fetchKm * 1000.0f, o.gamma);
            const float Phi = Sw * (9.81f / (2.0f * w)) * D / k;   // per (kx, kz)
            const float amp = std::sqrt(0.5f * Phi * dk * dk) * o.amplitude;
            h0[i] = make_float2(a * amp, b * amp);
            om[i] = w0 * std::round(w / w0);
            heightVar += 2.0 * double(amp) * amp;
            resolvedVar += 2.0 * double(amp) * amp * 0.5 * k * k;   // per axis
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
    std::printf("deniz: %dx%d / %.0f m, Hs %.3f m, |h| < %.3f m, egim varyansi cozulen %.4f + cozulmeyen %.4f (eksen basina)\n", N, N, o.patch,
                4.0 * std::sqrt(heightVar), hmax_, resolvedVar, varFloor_);
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
    KCHECK(cudaMalloc(&dSig_, c.slices * sizeof(float)));
    int n = c.N;
    for (int l = 0; l < c.levels && n >= 32; ++l, n /= 2) {
        cudaChannelFormatDesc f1 = cudaCreateChannelDesc<float>();
        cudaArray_t arr = nullptr;
        KCHECK(cudaMalloc3DArray(&arr, &f1, make_cudaExtent(n, n, c.slices)));
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
        float* buf = nullptr;
        if (l > 0) KCHECK(cudaMalloc(&buf, size_t(n) * n * c.slices * sizeof(float)));
        causLevelBuf_.push_back(buf);
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
        KCHECK(cudaMemsetAsync(caus_, 0, vox * sizeof(float), s));
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
        causticSplat<<<(c.rays * c.rays + 255) / 256, 256, 0, s>>>(a);
        // the sun's disc blurs every slice by depth (radius r_w z / cos, a uniform disc: sigma = radius / 2)
        std::vector<float> sig(c.slices);
        const float rw = sunRadiusAir_ / waterN_ * (sunAir_.y / std::max(sunWater_.y, 0.05f)) * 0.5f + sunRadiusAir_ / waterN_ * 0.5f;
        for (int k = 0; k < c.slices; ++k) {
            const float z = sliceDepth(k);
            const float radius = rw * z / std::max(sunWater_.y, 0.05f);
            sig[k] = std::max(0.5f * radius * c.N / oset.patch, c.minBlurTexels);
        }
        KCHECK(cudaMemcpyAsync(dSig_, sig.data(), c.slices * sizeof(float), cudaMemcpyHostToDevice, s));
        blurSlices<<<dim3((c.N + 127) / 128, c.N, c.slices), 128, 0, s>>>(caus_, causTmp_, c.N, c.slices, dSig_, false);
        blurSlices<<<dim3((c.N + 127) / 128, c.N, c.slices), 128, 0, s>>>(causTmp_, caus_, c.N, c.slices, dSig_, true);
        // into the 3D textures, level by level
        int n = c.N;
        const float* src = caus_;
        for (size_t l = 0; l < causArr_.size(); ++l, n /= 2) {
            if (l > 0) {
                halveSlices<<<dim3((n + 127) / 128, n, c.slices), 128, 0, s>>>(src, causLevelBuf_[l], n, c.slices);
                src = causLevelBuf_[l];
            }
            cudaMemcpy3DParms cp{};
            cp.srcPtr = make_cudaPitchedPtr(const_cast<float*>(src), size_t(n) * sizeof(float), n, n);
            cp.dstArray = causArr_[l];
            cp.extent = make_cudaExtent(n, n, c.slices);
            cp.kind = cudaMemcpyDeviceToDevice;
            KCHECK(cudaMemcpy3DAsync(&cp, s));
        }
    }
    KCHECK(cudaEventRecord(e2_, s));
    KCHECK(cudaEventSynchronize(e2_));
    cudaEventElapsedTime(&msSurface, e0_, e1_);
    cudaEventElapsedTime(&msCaustic, e1_, e2_);
}

void KoyOcean::readHeights(std::vector<float4>& out, cudaStream_t s) {
    out.resize(size_t(oset.N) * oset.N);
    KCHECK(cudaMemcpyAsync(out.data(), field_, out.size() * sizeof(float4), cudaMemcpyDeviceToHost, s));
    KCHECK(cudaStreamSynchronize(s));
}
void KoyOcean::readCaustic(int slice, std::vector<float>& out, cudaStream_t s) {
    const size_t n = size_t(cset.N) * cset.N;
    out.resize(n);
    KCHECK(cudaMemcpyAsync(out.data(), caus_ + size_t(slice) * n, n * sizeof(float), cudaMemcpyDeviceToHost, s));
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
    for (auto b : causLevelBuf_) if (b) cudaFree(b);
    causLevelBuf_.clear();
    if (surfArr_) cudaFreeMipmappedArray(surfArr_);
    cudaFree(h0_);
    cudaFree(omega_);
    cudaFree(spec_);
    cudaFree(field_);
    cudaFree(caus_);
    cudaFree(causTmp_);
    cudaFree(dSig_);
    dSig_ = nullptr;
    h0_ = nullptr;
    omega_ = nullptr;
    spec_ = nullptr;
    field_ = nullptr;
    caus_ = causTmp_ = nullptr;
    surfArr_ = nullptr;
    surfTex_ = 0;
}
