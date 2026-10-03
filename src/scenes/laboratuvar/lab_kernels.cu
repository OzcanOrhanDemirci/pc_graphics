#include "scenes/laboratuvar/lab_kernels.h"
#include "shared/display.h"

namespace {

constexpr int kB = 256;
inline int nb(int n) { return (n + kB - 1) / kB; }

struct Xf {
    float m[12];
};

__device__ inline unsigned hashU(unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
__device__ inline float hashF(unsigned x) { return (hashU(x) >> 8) * (1.0f / 16777216.0f); }

__global__ void kTransform(const float3* rest, int n, Xf x, float3* out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float3 p = rest[i];
    out[i] = make_float3(x.m[0] * p.x + x.m[1] * p.y + x.m[2] * p.z + x.m[3], x.m[4] * p.x + x.m[5] * p.y + x.m[6] * p.z + x.m[7],
                         x.m[8] * p.x + x.m[9] * p.y + x.m[10] * p.z + x.m[11]);
}

// codes (lab_world.h DrawCode): 6 flesh -> 7 juice once liquid
__global__ void kCodes(const uint8_t* base, const float* fluid, int n, uint8_t* out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    uint8_t c = base[i];
    if (c == 6 && fluid && fluid[i] > 0.5f) c = 7;
    out[i] = c;
}

__global__ void kWeights(const uint8_t* codes, int n, uint8_t code, float* out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    out[i] = codes[i] == code ? 1.0f : 0.0f;
}

__global__ void kWeightsAdd(const uint8_t* codes, int n, uint8_t code, float* out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && codes[i] == code) out[i] = 1.0f;
}

__global__ void kGrains(const float3* pos, const uint8_t* codes, int n, int per, float spread, float rMin, float rMax, float4* sp, float* tint) {
    const int g = blockIdx.x * blockDim.x + threadIdx.x;
    if (g >= n * per) return;
    const int i = g / per;
    if (codes[i] != 3) { sp[g] = make_float4(0, -10.0f, 0, 0.0f); if (tint) tint[g] = 1.0f; return; }
    const unsigned h = unsigned(g) * 2654435761u + 12345u;
    // a point in the ball of radius spread (rejection-free: cube root of a uniform for the radius)
    const float u = hashF(h), v = hashF(h + 1), w = hashF(h + 2);
    const float r = spread * cbrtf(u), ct = 2.0f * v - 1.0f, st = sqrtf(fmaxf(0.0f, 1.0f - ct * ct)), ph = 6.2831853f * w;
    const float3 p = pos[i];
    const float rad = rMin + (rMax - rMin) * hashF(h + 3);
    sp[g] = make_float4(p.x + r * st * cosf(ph), p.y + r * ct, p.z + r * st * sinf(ph), rad);
    // grains differ: most a little lighter or darker, a few dark (iron minerals) or pale (quartz)
    const float k = hashF(h + 4);
    float t = 0.82f + 0.36f * hashF(h + 5);
    if (k < 0.06f) t = 0.35f;
    else if (k > 0.94f) t = 1.35f;
    if (tint) tint[g] = t;
}

__global__ void kQuantise(const float3* pos, const uint8_t* codes, int n, QuantBox b, ushort4* out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float3 p = pos[i];
    auto q = [](float v, float lo, float sc) { return (unsigned short)fminf(65535.0f, fmaxf(0.0f, (v - lo) / sc + 0.5f)); };
    out[i] = make_ushort4(q(p.x, b.lo.x, b.scale.x), q(p.y, b.lo.y, b.scale.y), q(p.z, b.lo.z, b.scale.z), codes ? codes[i] : 1);
}

__global__ void kDequantise(const ushort4* a, const ushort4* b, float t, int n, QuantBox q, float3* pos, uint8_t* codes) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const ushort4 u = a[i];
    float3 p = make_float3(q.lo.x + u.x * q.scale.x, q.lo.y + u.y * q.scale.y, q.lo.z + u.z * q.scale.z);
    unsigned short c = u.w;
    if (b && t > 0.0f) {
        const ushort4 v = b[i];
        const float3 p1 = make_float3(q.lo.x + v.x * q.scale.x, q.lo.y + v.y * q.scale.y, q.lo.z + v.z * q.scale.z);
        p = p + (p1 - p) * t;
        if (t >= 0.5f) c = v.w;
    }
    pos[i] = p;
    if (codes) codes[i] = uint8_t(c);
}

__global__ void kMembraneFaces(const float3* pos, int first, const uint3* tris, int nTris, float tear2, uint3* outTris, int* count, float3* nrmAcc) {
    const int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= nTris) return;
    const uint3 f = tris[t];
    const float3 a = pos[first + f.x], b = pos[first + f.y], c = pos[first + f.z];
    const float3 ab = b - a, bc = c - b, ca = a - c;
    if (dot(ab, ab) > tear2 || dot(bc, bc) > tear2 || dot(ca, ca) > tear2) return;
    const int k = atomicAdd(count, 1);
    outTris[k] = f;
    const float3 fn = cross(ab, c - a);
    atomicAdd(&nrmAcc[f.x].x, fn.x); atomicAdd(&nrmAcc[f.x].y, fn.y); atomicAdd(&nrmAcc[f.x].z, fn.z);
    atomicAdd(&nrmAcc[f.y].x, fn.x); atomicAdd(&nrmAcc[f.y].y, fn.y); atomicAdd(&nrmAcc[f.y].z, fn.z);
    atomicAdd(&nrmAcc[f.z].x, fn.x); atomicAdd(&nrmAcc[f.z].y, fn.y); atomicAdd(&nrmAcc[f.z].z, fn.z);
}
__global__ void kMembraneVerts(const float3* pos, int first, int nVerts, float3* verts, float3* nrm) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= nVerts) return;
    verts[i] = pos[first + i];
    const float3 n = nrm[i];
    const float l = sqrtf(dot(n, n));
    nrm[i] = l > 1e-20f ? n * (1.0f / l) : make_float3(0, 1, 0);
}

__global__ void kGather(const float3* pos, const uint8_t* codes, const uint32_t* idx, int n, float3* op, uint8_t* oc) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const uint32_t j = idx[i];
    op[i] = pos[j];
    oc[i] = codes[j];
}

__global__ void kCentre(const float3* pos, int first, int count, float r, float4* out) {
    __shared__ float3 acc[kB];
    float3 s = make_float3(0, 0, 0);
    for (int i = threadIdx.x; i < count; i += blockDim.x) s = s + pos[first + i];
    acc[threadIdx.x] = s;
    __syncthreads();
    for (int w = blockDim.x / 2; w > 0; w >>= 1) {
        if (threadIdx.x < w) acc[threadIdx.x] = acc[threadIdx.x] + acc[threadIdx.x + w];
        __syncthreads();
    }
    if (threadIdx.x == 0) {
        const float3 c = acc[0] * (1.0f / float(max(count, 1)));
        out[0] = make_float4(c.x, c.y, c.z, r);
    }
}

__global__ void kDenoisePrep(const float4* acc, float k, int n, float4* out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float4 a = acc[i];
    out[i] = make_float4(a.x * k, a.y * k, a.z * k, 1.0f);
}
__device__ inline float3 toYCoCg(float3 c) { return make_float3(0.25f * c.x + 0.5f * c.y + 0.25f * c.z, 0.5f * c.x - 0.5f * c.z, -0.25f * c.x + 0.5f * c.y - 0.25f * c.z); }
__device__ inline float3 fromYCoCg(float3 y) { return make_float3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z); }

__global__ void kTemporal(const float4* accum, float invSpp, const float4* pos, const float4* ppos, TCam pc, const float4* hin, float4* hout, int w, int h,
                          float alphaMin, float gamma, float maxFrames, bool reset) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= w * h) return;
    const int x = i % w, y = i / w;
    const float4 a = accum[i];
    const float3 cur = make_float3(a.x * invSpp, a.y * invSpp, a.z * invSpp);
    if (reset) { hout[i] = make_float4(cur.x, cur.y, cur.z, 1.0f); return; }
    // where this pixel's surface was on the previous frame's screen
    const float4 P = pos[i];
    const float3 v = make_float3(P.x, P.y, P.z) - pc.pos;
    const float z = dot(v, pc.fwd);
    if (z <= 1e-4f) { hout[i] = make_float4(cur.x, cur.y, cur.z, 1.0f); return; }
    const float sx = dot(v, pc.right) / (z * pc.tanHalfX), sy = dot(v, pc.up) / (z * pc.tanHalfY);
    const float fx = (sx * 0.5f + 0.5f) * w - 0.5f, fy = (0.5f - sy * 0.5f) * h - 0.5f;
    if (fx < 0.0f || fy < 0.0f || fx > w - 1.0f || fy > h - 1.0f) { hout[i] = make_float4(cur.x, cur.y, cur.z, 1.0f); return; }
    const int x0 = int(fx), y0 = int(fy);
    const int x1 = min(x0 + 1, w - 1), y1 = min(y0 + 1, h - 1);
    const float tx = fx - x0, ty = fy - y0;
    // the four taps, each only if it saw this surface (within a hair of the same point, the ray as long)
    // (what is seen through glass shifts as the camera moves: the length may change in proportion; with the camera still
    // the test is strict, and catches liquid moving behind glass)
    const float tolP = 0.0015f + 0.004f * z, tolL = 0.002f + 0.01f * P.w + 8.0f * pc.moved * P.w / fmaxf(z, 0.05f);
    const int tap[4] = {y0 * w + x0, y0 * w + x1, y1 * w + x0, y1 * w + x1};
    const float wt[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
    float4 H = make_float4(0, 0, 0, 1e30f);
    float W = 0.0f;
    for (int k = 0; k < 4; ++k) {
        const float4 q = ppos[tap[k]];
        const float3 dq = make_float3(q.x - P.x, q.y - P.y, q.z - P.z);
        if (dot(dq, dq) > tolP * tolP || fabsf(q.w - P.w) > tolL || wt[k] <= 0.0f) continue;
        const float4 hk = hin[tap[k]];
        H.x += hk.x * wt[k];
        H.y += hk.y * wt[k];
        H.z += hk.z * wt[k];
        H.w = fminf(H.w, hk.w);
        W += wt[k];
    }
    if (W < 0.05f) { hout[i] = make_float4(cur.x, cur.y, cur.z, 1.0f); return; }
    H.x /= W;
    H.y /= W;
    H.z /= W;
    // this frame's neighbourhood: the history kept within its spread
    float3 m1 = make_float3(0, 0, 0), m2 = make_float3(0, 0, 0);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int xx = min(max(x + dx, 0), w - 1), yy = min(max(y + dy, 0), h - 1);
            const float4 b = accum[yy * w + xx];
            const float3 c = toYCoCg(make_float3(b.x * invSpp, b.y * invSpp, b.z * invSpp));
            m1 = m1 + c;
            m2 = m2 + c * c;
        }
    m1 = m1 * (1.0f / 9.0f);
    m2 = m2 * (1.0f / 9.0f);
    const float3 sd = make_float3(sqrtf(fmaxf(0.0f, m2.x - m1.x * m1.x)), sqrtf(fmaxf(0.0f, m2.y - m1.y * m1.y)), sqrtf(fmaxf(0.0f, m2.z - m1.z * m1.z)));
    float3 hy = toYCoCg(make_float3(H.x, H.y, H.z));
    const float3 lo = m1 - sd * gamma, hi = m1 + sd * gamma;
    hy = make_float3(fminf(fmaxf(hy.x, lo.x), hi.x), fminf(fmaxf(hy.y, lo.y), hi.y), fminf(fmaxf(hy.z, lo.z), hi.z));
    const float3 hc = fromYCoCg(hy);
    const float n = fminf(H.w + 1.0f, maxFrames);
    const float al = fmaxf(1.0f / n, alphaMin);
    const float3 o = hc + (cur - hc) * al;
    hout[i] = make_float4(fmaxf(o.x, 0.0f), fmaxf(o.y, 0.0f), fmaxf(o.z, 0.0f), n);
}

__global__ void kTonemap(const float4* lin, int w, int h, unsigned seed, uchar4* out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= w * h) return;
    const float4 a = lin[i];
    float3 c = agx(make_float3(fmaxf(a.x, 0.0f), fmaxf(a.y, 0.0f), fmaxf(a.z, 0.0f)));
    const unsigned hh = hashU(unsigned(i) * 9781u + seed * 6271u);
    const float dn = (hashF(hh) + hashF(hh + 1) - 1.0f) / 255.0f;
    out[i] = make_uchar4((unsigned char)(saturate(srgbOetf(c.x) + dn) * 255.0f + 0.5f), (unsigned char)(saturate(srgbOetf(c.y) + dn) * 255.0f + 0.5f),
                         (unsigned char)(saturate(srgbOetf(c.z) + dn) * 255.0f + 0.5f), 255);
}

}   // namespace

void launchDenoisePrep(const float4* accum, unsigned samples, float exposure, int n, float4* out, cudaStream_t s) {
    if (n > 0) kDenoisePrep<<<nb(n), kB, 0, s>>>(accum, exposure / float(samples > 0 ? samples : 1), n, out);
}
void launchTemporal(const float4* accum, float invSpp, const float4* pos, const float4* prevPos, TCam prev, const float4* histIn, float4* histOut, int w,
                    int h, float alphaMin, float gamma, float maxFrames, bool reset, cudaStream_t s) {
    if (w * h > 0) kTemporal<<<nb(w * h), kB, 0, s>>>(accum, invSpp, pos, prevPos, prev, histIn, histOut, w, h, alphaMin, gamma, maxFrames, reset);
}
void launchTonemap(const float4* lin, int w, int h, unsigned seed, uchar4* out, cudaStream_t s) {
    if (w * h > 0) kTonemap<<<nb(w * h), kB, 0, s>>>(lin, w, h, seed, out);
}

void launchTransform(const float3* rest, int n, const float xf[12], float3* out, cudaStream_t s) {
    if (n <= 0) return;
    Xf x;
    for (int k = 0; k < 12; ++k) x.m[k] = xf[k];
    kTransform<<<nb(n), kB, 0, s>>>(rest, n, x, out);
}
void launchCodes(const uint8_t* base, const float* fluid, int n, uint8_t* out, cudaStream_t s) {
    if (n > 0) kCodes<<<nb(n), kB, 0, s>>>(base, fluid, n, out);
}
void launchWeights(const uint8_t* codes, int n, uint8_t code, float* out, cudaStream_t s) {
    if (n > 0) kWeights<<<nb(n), kB, 0, s>>>(codes, n, code, out);
}
void launchWeightsAdd(const uint8_t* codes, int n, uint8_t code, float* out, cudaStream_t s) {
    if (n > 0) kWeightsAdd<<<nb(n), kB, 0, s>>>(codes, n, code, out);
}
void launchGrains(const float3* pos, const uint8_t* codes, int n, int per, float spread, float rMin, float rMax, float4* spheres, float* tint, cudaStream_t s) {
    if (n > 0 && per > 0) kGrains<<<nb(n * per), kB, 0, s>>>(pos, codes, n, per, spread, rMin, rMax, spheres, tint);
}
void launchQuantise(const float3* pos, const uint8_t* codes, int n, QuantBox b, ushort4* out, cudaStream_t s) {
    if (n > 0) kQuantise<<<nb(n), kB, 0, s>>>(pos, codes, n, b, out);
}
void launchDequantise(const ushort4* a, const ushort4* b, float t, int n, QuantBox q, float3* pos, uint8_t* codes, cudaStream_t s) {
    if (n > 0) kDequantise<<<nb(n), kB, 0, s>>>(a, b, t, n, q, pos, codes);
}
void launchMembrane(const float3* pos, int first, const uint3* tris, int nTris, float tear, uint3* outTris, int* count, float3* verts, float3* nrm, int nVerts,
                    cudaStream_t s) {
    cudaMemsetAsync(count, 0, sizeof(int), s);
    cudaMemsetAsync(nrm, 0, size_t(nVerts) * sizeof(float3), s);
    if (nTris > 0) kMembraneFaces<<<nb(nTris), kB, 0, s>>>(pos, first, tris, nTris, tear * tear, outTris, count, nrm);
    if (nVerts > 0) kMembraneVerts<<<nb(nVerts), kB, 0, s>>>(pos, first, nVerts, verts, nrm);
}
void launchGather(const float3* pos, const uint8_t* codes, const uint32_t* idx, int n, float3* outPos, uint8_t* outCodes, cudaStream_t s) {
    if (n > 0) kGather<<<nb(n), kB, 0, s>>>(pos, codes, idx, n, outPos, outCodes);
}
void launchCentre(const float3* pos, int first, int count, float r, float4* out, cudaStream_t s) {
    kCentre<<<1, kB, 0, s>>>(pos, first, count, r, out);
}
