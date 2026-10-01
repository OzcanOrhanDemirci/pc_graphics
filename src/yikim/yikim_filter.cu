// Scene 3 "Yikim": the live window's denoiser (see yikim_filter.h).
#include "yikim/yikim_filter.h"
#include "shared/display.h"
#include "shared/vec.h"

#include <cstdio>

namespace {

__device__ __forceinline__ float lum(float3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
__device__ __forceinline__ float3 xyz(float4 v) { return make_float3(v.x, v.y, v.z); }

// octahedral normal encoding: a normal in two floats, so distance, body and normal fit one float4 per pixel
__device__ __forceinline__ float2 octEncode(float3 n) {
    const float s = fabsf(n.x) + fabsf(n.y) + fabsf(n.z);
    if (s < 1e-6f) return make_float2(0.0f, 0.0f);
    float2 e = make_float2(n.x / s, n.y / s);
    if (n.z < 0.0f) e = make_float2((1.0f - fabsf(e.y)) * (e.x >= 0.0f ? 1.0f : -1.0f), (1.0f - fabsf(e.x)) * (e.y >= 0.0f ? 1.0f : -1.0f));
    return e;
}
__device__ __forceinline__ float3 octDecode(float2 e) {
    float3 n = make_float3(e.x, e.y, 1.0f - fabsf(e.x) - fabsf(e.y));
    if (n.z < 0.0f) n = make_float3((1.0f - fabsf(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f), (1.0f - fabsf(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f), n.z);
    const float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    return l > 1e-6f ? make_float3(n.x / l, n.y / l, n.z / l) : make_float3(0, 0, 0);
}

constexpr int kMaxHistory = 32;

// (1) temporal: lighting = beauty / albedo, carried from the same point a frame ago
__global__ void kTemporal(YFilterFrame f, const float4* histEPrev, const float4* histMPrev, const float4* geoPrev, float4* histECur, float4* histMCur,
                          float4* geoCur, float4* alb, float4* out, bool history) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= int(f.width) || y >= int(f.height)) return;
    const int i = y * f.width + x;
    const float inv = 1.0f / float(max(f.samples, 1u));
    const float3 L = xyz(f.accum[i]) * inv, A = xyz(f.albedoAcc[i]) * inv;
    float3 N = xyz(f.normalAcc[i]);
    const float nl = sqrtf(dot(N, N));
    N = nl > 1e-6f ? N * (1.0f / nl) : make_float3(0, 0, 0);
    const float4 g = f.gbuf[i];
    const float id = g.y, depth = g.x;
    const bool sky = id < -0.5f;
    const float3 a = sky ? make_float3(1, 1, 1) : make_float3(fmaxf(A.x, 0.03f), fmaxf(A.y, 0.03f), fmaxf(A.z, 0.03f));
    const float3 E = make_float3(L.x / a.x, L.y / a.y, L.z / a.z);
    const float l = lum(E);
    // where this pixel's point was: bilinear over last frame's pixels, each tap checked (same body, normal, distance)
    float3 Eh = make_float3(0, 0, 0);
    float nh = 0.0f, m1 = 0.0f, m2 = 0.0f, ws = 0.0f;
    if (history && !sky) {
        const float4 m = f.motion[i];
        const float px = x + 0.5f - m.x - 0.5f, py = y + 0.5f - m.y - 0.5f;
        const int x0 = int(floorf(px)), y0 = int(floorf(py));
        const float fx = px - x0, fy = py - y0;
        for (int t = 0; t < 4; ++t) {
            const int tx = x0 + (t & 1), ty = y0 + (t >> 1);
            if (tx < 0 || ty < 0 || tx >= int(f.width) || ty >= int(f.height)) continue;
            const int j = ty * f.width + tx;
            const float4 gp = geoPrev[j];
            if (fabsf(gp.y - id) > 0.5f) continue;
            if (dot(octDecode(make_float2(gp.z, gp.w)), N) < 0.9f) continue;
            if (fabsf(gp.x - m.z) > 0.03f * m.z + 0.02f) continue;
            const float w = ((t & 1) ? fx : 1.0f - fx) * ((t >> 1) ? fy : 1.0f - fy);
            const float4 h = histEPrev[j], mm = histMPrev[j];
            Eh = Eh + xyz(h) * w;
            nh += h.w * w;
            m1 += mm.x * w;
            m2 += mm.y * w;
            ws += w;
        }
    }
    float3 Eo;
    float n, M1, M2;
    if (ws > 0.01f) {
        const float r = 1.0f / ws;
        Eh = Eh * r;
        nh *= r;
        m1 *= r;
        m2 *= r;
        // the lighting may have changed under the point (a shadow sweeping over it as the building moves): the carried
        // value is kept within what this frame's neighbourhood allows (mean +- 2 deviations, same body), and a large
        // correction shortens the memory so the new light takes over at once (measured: a falling building's shadow
        // left lit blotches on the ground for half a second)
        float3 mu = make_float3(0, 0, 0), sq = make_float3(0, 0, 0);
        float cnt = 0.0f;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int qx = x + dx, qy = y + dy;
                if (qx < 0 || qy < 0 || qx >= int(f.width) || qy >= int(f.height)) continue;
                const int q = qy * f.width + qx;
                if (fabsf(f.gbuf[q].y - id) > 0.5f) continue;
                const float3 Lq = xyz(f.accum[q]) * inv, Aq = xyz(f.albedoAcc[q]) * inv;
                const float3 Eq = make_float3(Lq.x / fmaxf(Aq.x, 0.03f), Lq.y / fmaxf(Aq.y, 0.03f), Lq.z / fmaxf(Aq.z, 0.03f));
                mu = mu + Eq;
                sq = sq + make_float3(Eq.x * Eq.x, Eq.y * Eq.y, Eq.z * Eq.z);
                cnt += 1.0f;
            }
        mu = mu * (1.0f / cnt);
        const float3 sd = make_float3(sqrtf(fmaxf(sq.x / cnt - mu.x * mu.x, 0.0f)), sqrtf(fmaxf(sq.y / cnt - mu.y * mu.y, 0.0f)),
                                      sqrtf(fmaxf(sq.z / cnt - mu.z * mu.z, 0.0f)));
        const float3 lo = mu - sd * 2.0f, hi = mu + sd * 2.0f;
        const float3 Ec = make_float3(fminf(fmaxf(Eh.x, lo.x), hi.x), fminf(fmaxf(Eh.y, lo.y), hi.y), fminf(fmaxf(Eh.z, lo.z), hi.z));
        const float lh = lum(Eh), lc = lum(Ec);
        if (fabsf(lc - lh) > 0.1f * lh + 0.01f) {
            nh = fminf(nh, 3.0f);
            m1 = lc;
            m2 = lc * lc;
        }
        Eh = Ec;
        n = fminf(nh + 1.0f, float(kMaxHistory));
        const float al = 1.0f / n;
        Eo = Eh + (E - Eh) * al;
        M1 = m1 + (l - m1) * al;
        M2 = m2 + (l * l - m2) * al;
    } else {
        n = 1.0f;
        Eo = E;
        M1 = l;
        M2 = l * l;
    }
    float var = fmaxf(M2 - M1 * M1, 0.0f);
    if (n < 4.0f) var = fmaxf(var, 0.5f * l * l + 0.01f);   // fresh (uncovered, just appeared): no moments yet, blur more
    histECur[i] = make_float4(Eo.x, Eo.y, Eo.z, n);
    histMCur[i] = make_float4(M1, M2, 0.0f, 0.0f);
    const float2 oe = octEncode(N);
    geoCur[i] = make_float4(depth, id, oe.x, oe.y);
    alb[i] = make_float4(A.x, A.y, A.z, sky ? 1.0f : 0.0f);
    out[i] = make_float4(Eo.x, Eo.y, Eo.z, var);
}

// (2) one a-trous pass (5x5 B3 spline, taps `step` pixels apart), stopping at other bodies, normals, depth steps and
// lighting edges larger than the noise. The first pass's result is also the history the next frame starts from.
__global__ void kAtrous(int width, int height, int step, const float4* in, float4* out, const float4* geo, float4* histE) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    const int i = y * width + x;
    const float4 c = in[i];
    const float4 gp = geo[i];
    if (gp.y < -0.5f) { out[i] = c; return; }   // the sky has no noise
    const float3 np = octDecode(make_float2(gp.z, gp.w));
    const float lp = lum(xyz(c));
    // the variance, smoothed over 3x3 (SVGF): one noisy estimate would make the lighting stop erratic
    float vs = 0.0f, vw = 0.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int qx = x + dx, qy = y + dy;
            if (qx < 0 || qy < 0 || qx >= width || qy >= height) continue;
            const float w = (dx == 0 ? 0.5f : 0.25f) * (dy == 0 ? 0.5f : 0.25f);
            vs += in[qy * width + qx].w * w;
            vw += w;
        }
    const float sigL = 4.0f * sqrtf(fmaxf(vs / vw, 0.0f)) + 1e-4f;
    // depth gradient: a plane seen at a grazing angle changes depth fast without an edge
    float grad = 0.0f;
    if (x + 1 < width) { const float4 g1 = geo[i + 1]; if (fabsf(g1.y - gp.y) < 0.5f) grad = fmaxf(grad, fabsf(g1.x - gp.x)); }
    if (y + 1 < height) { const float4 g1 = geo[i + width]; if (fabsf(g1.y - gp.y) < 0.5f) grad = fmaxf(grad, fabsf(g1.x - gp.x)); }
    const float kh[3] = {3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f};
    float3 sum = make_float3(0, 0, 0);
    float sv = 0.0f, sw = 0.0f;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const int qx = x + dx * step, qy = y + dy * step;
            if (qx < 0 || qy < 0 || qx >= width || qy >= height) continue;
            const int j = qy * width + qx;
            const float4 gq = geo[j];
            if (fabsf(gq.y - gp.y) > 0.5f) continue;   // another body (or ground / sky)
            const float4 q = in[j];
            const float3 nq = octDecode(make_float2(gq.z, gq.w));
            const float dn = fmaxf(dot(np, nq), 0.0f);
            const float wn = __powf(dn, 64.0f);
            const float dist = step * sqrtf(float(dx * dx + dy * dy));
            const float wz = __expf(-fabsf(gq.x - gp.x) / (grad * dist * 1.5f + 0.002f * gp.x + 1e-4f));
            const float wl = __expf(-fabsf(lum(xyz(q)) - lp) / sigL);
            const float w = kh[abs(dx)] * kh[abs(dy)] * wn * wz * wl;
            sum = sum + xyz(q) * w;
            sv += w * w * q.w;
            sw += w;
        }
    const float r = 1.0f / fmaxf(sw, 1e-8f);
    const float3 o = sum * r;
    out[i] = make_float4(o.x, o.y, o.z, sv * r * r);
    if (histE) histE[i] = make_float4(o.x, o.y, o.z, histE[i].w);
}

// (3) lighting times albedo, eased into the raw accumulation as a still picture gathers samples; tone mapped
__global__ void kFinal(YFilterFrame f, const float4* in, const float4* alb) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= int(f.width) || y >= int(f.height)) return;
    const int i = y * f.width + x;
    const float4 a = alb[i];
    const float3 E = xyz(in[i]);
    float3 L = a.w > 0.5f ? E : make_float3(E.x * a.x, E.y * a.y, E.z * a.z);
    const float3 raw = xyz(f.accum[i]) * (1.0f / float(max(f.samples, 1u)));
    const float beta = fminf(fmaxf((float(f.samples) - 32.0f) / 96.0f, 0.0f), 1.0f);
    L = L + (raw - L) * beta;
    float3 col = make_float3(fmaxf(L.x, 0.0f), fmaxf(L.y, 0.0f), fmaxf(L.z, 0.0f)) * f.exposure;
    col = agx(col);
    unsigned int h = unsigned(i) * 7919u + f.seed * 104729u + 17u;
    auto next = [&] {
        h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
        return (h >> 8) * (1.0f / 16777216.0f);
    };
    const float dn = (next() + next() - 1.0f) / 255.0f;
    f.output[i] = make_uchar4((unsigned char)(saturate(srgbOetf(col.x) + dn) * 255.0f + 0.5f), (unsigned char)(saturate(srgbOetf(col.y) + dn) * 255.0f + 0.5f),
                              (unsigned char)(saturate(srgbOetf(col.z) + dn) * 255.0f + 0.5f), 255);
}

template <class T> bool dalloc(T*& p, size_t n) {
    if (cudaMalloc(reinterpret_cast<void**>(&p), n * sizeof(T)) != cudaSuccess) return false;
    return cudaMemset(p, 0, n * sizeof(T)) == cudaSuccess;
}

}   // namespace

bool YFilter::create(unsigned int width, unsigned int height) {
    w_ = width;
    h_ = height;
    const size_t n = size_t(width) * height;
    for (int k = 0; k < 2; ++k)
        if (!dalloc(histE_[k], n) || !dalloc(histM_[k], n) || !dalloc(geo_[k], n)) return false;
    return dalloc(alb_, n) && dalloc(a_, n) && dalloc(b_, n);
}

void YFilter::destroy() {
    for (int k = 0; k < 2; ++k) {
        cudaFree(histE_[k]);
        cudaFree(histM_[k]);
        cudaFree(geo_[k]);
        histE_[k] = histM_[k] = geo_[k] = nullptr;
    }
    cudaFree(alb_);
    cudaFree(a_);
    cudaFree(b_);
    alb_ = a_ = b_ = nullptr;
}

void YFilter::run(const YFilterFrame& f, cudaStream_t stream) {
    const dim3 bs(16, 8), gs((w_ + 15) / 16, (h_ + 7) / 8);
    const int prev = cur_ ^ 1;
    kTemporal<<<gs, bs, 0, stream>>>(f, histE_[prev], histM_[prev], geo_[prev], histE_[cur_], histM_[cur_], geo_[cur_], alb_, a_, history_);
    float4 *src = a_, *dst = b_;
    for (int p = 0; p < passes; ++p) {
        kAtrous<<<gs, bs, 0, stream>>>(int(w_), int(h_), 1 << p, src, dst, geo_[cur_], p == 0 ? histE_[cur_] : nullptr);
        float4* t = src;
        src = dst;
        dst = t;
    }
    kFinal<<<gs, bs, 0, stream>>>(f, src, alb_);
    history_ = true;
    cur_ = prev;
}
