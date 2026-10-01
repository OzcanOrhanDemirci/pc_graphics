// Small CUDA kernels outside the OptiX pipeline.
#include "denoiser.h"

#include <algorithm>
#include "shared/spectrum.h"

// Denoiser input: white-balanced linear sRGB of the accumulated image without its caustic part; guides:
// first-hit albedo (RGB) and world-space normal.
__global__ void denoisePrepKernel(Params p, float4* rgb, float4* albedo, float4* normal, float4* caustic) {
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.renderWidth || y >= p.renderHeight) return;
    const unsigned int i = y * p.renderWidth + x;
    const float4 a = p.accum[i], c = p.causticAccum[i];
    const float inv = 1.0f / fmaxf(a.w, 1.0f);
    const float3 raw = make_float3(a.x - c.x, a.y - c.y, a.z - c.z) * inv;
    const float* m = p.wbMatrix;
    const float3 xyz = make_float3(m[0] * raw.x + m[1] * raw.y + m[2] * raw.z, m[3] * raw.x + m[4] * raw.y + m[5] * raw.z,
                                   m[6] * raw.x + m[7] * raw.y + m[8] * raw.z);
    const float3 s = xyzToLinearSRGB(xyz);
    rgb[i] = make_float4(fmaxf(s.x, 0.0f), fmaxf(s.y, 0.0f), fmaxf(s.z, 0.0f), 1.0f);
    {   // caustic layer, white balanced linear sRGB (may be slightly negative for spectral colours: kept)
        const float3 cr = make_float3(c.x, c.y, c.z) * inv;
        const float3 cx = make_float3(m[0] * cr.x + m[1] * cr.y + m[2] * cr.z, m[3] * cr.x + m[4] * cr.y + m[5] * cr.z,
                                      m[6] * cr.x + m[7] * cr.y + m[8] * cr.z);
        const float3 cs = xyzToLinearSRGB(cx);
        caustic[i] = make_float4(cs.x, cs.y, cs.z, 1.0f);
    }
    const float4 al = p.aovAlbedo[i], nr = p.aovNormal[i];
    const float ia = 1.0f / fmaxf(al.w, 1.0f);
    albedo[i] = make_float4(fminf(fmaxf(al.x * ia, 0.0f), 1.0f), fminf(fmaxf(al.y * ia, 0.0f), 1.0f), fminf(fmaxf(al.z * ia, 0.0f), 1.0f), 1.0f);
    float3 n = make_float3(nr.x, nr.y, nr.z);
    const float l = sqrtf(dot(n, n));
    n = l > 1e-6f ? n * (1.0f / l) : make_float3(0.0f, 0.0f, 0.0f);
    // the temporal model wants camera space (optixDenoiserInvoke docs): x right, y up, z towards the camera
    const float3 nc = make_float3(dot(n, p.cam.right), dot(n, p.cam.up), -dot(n, p.cam.fwd));
    normal[i] = make_float4(nc.x, nc.y, nc.z, 0.0f);
}

void launchDenoisePrep(const Params& p, float4* rgb, float4* albedo, float4* normal, float4* caustic, cudaStream_t stream) {
    const dim3 b(16, 16), g((p.renderWidth + 15) / 16, (p.renderHeight + 15) / 16);
    denoisePrepKernel<<<g, b, 0, stream>>>(p, rgb, albedo, normal, caustic);
}

// ---- lens glare: a sum of progressively wider blurs (a pyramid), like a camera's veiling-glare spread.
// Down: 13-tap filter (Jimenez 2014) from the level above. Up: tent-filtered upsample added to the level.
__device__ float4 tap(const float4* img, int w, int h, float x, float y) {   // bilinear, clamped, pixel units
    x = fminf(fmaxf(x - 0.5f, 0.0f), w - 1.001f);
    y = fminf(fmaxf(y - 0.5f, 0.0f), h - 1.001f);
    const int x0 = int(x), y0 = int(y);
    const float tx = x - x0, ty = y - y0;
    const int x1 = min(x0 + 1, w - 1), y1 = min(y0 + 1, h - 1);
    return (img[y0 * w + x0] * (1.0f - tx) + img[y0 * w + x1] * tx) * (1.0f - ty) + (img[y1 * w + x0] * (1.0f - tx) + img[y1 * w + x1] * tx) * ty;
}

__global__ void bloomDown(const float4* src, int sw, int sh, float4* dst, int dw, int dh) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= dw || y >= dh) return;
    const float cx = (x + 0.5f) * 2.0f, cy = (y + 0.5f) * 2.0f;   // centre in source pixels
    const float4 a = tap(src, sw, sh, cx - 2, cy - 2), b = tap(src, sw, sh, cx, cy - 2), c = tap(src, sw, sh, cx + 2, cy - 2);
    const float4 d = tap(src, sw, sh, cx - 1, cy - 1), e = tap(src, sw, sh, cx + 1, cy - 1);
    const float4 f = tap(src, sw, sh, cx - 2, cy), g = tap(src, sw, sh, cx, cy), hh = tap(src, sw, sh, cx + 2, cy);
    const float4 i = tap(src, sw, sh, cx - 1, cy + 1), j = tap(src, sw, sh, cx + 1, cy + 1);
    const float4 k = tap(src, sw, sh, cx - 2, cy + 2), l = tap(src, sw, sh, cx, cy + 2), m = tap(src, sw, sh, cx + 2, cy + 2);
    dst[y * dw + x] = (d + e + i + j) * 0.125f + (a + c + k + m) * 0.03125f + (b + f + hh + l) * 0.0625f + g * 0.125f;
}

__global__ void bloomUp(const float4* small, int sw, int sh, const float4* level, float4* dst, int dw, int dh) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= dw || y >= dh) return;
    const float sx = (x + 0.5f) * 0.5f, sy = (y + 0.5f) * 0.5f;   // centre in the smaller level's pixels
    const float r = 1.0f;
    const float4 t = (tap(small, sw, sh, sx - r, sy - r) + tap(small, sw, sh, sx + r, sy - r) + tap(small, sw, sh, sx - r, sy + r) +
                      tap(small, sw, sh, sx + r, sy + r)) * 0.0625f +
                     (tap(small, sw, sh, sx, sy - r) + tap(small, sw, sh, sx - r, sy) + tap(small, sw, sh, sx + r, sy) + tap(small, sw, sh, sx, sy + r)) * 0.125f +
                     tap(small, sw, sh, sx, sy) * 0.25f;
    dst[y * dw + x] = level[y * dw + x] + t;
}

__global__ void bloomScale(float4* img, int n, float s) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) img[i] = img[i] * s;
}

void launchBloom(const float4* hdr, int w, int h, float4** levels, float4** accum, int nLevels, cudaStream_t stream) {
    const dim3 b(16, 16);
    int lw[8], lh[8];
    const float4* src = hdr;
    int sw = w, sh = h;
    for (int k = 0; k < nLevels; ++k) {
        lw[k] = std::max(1, sw / 2);
        lh[k] = std::max(1, sh / 2);
        bloomDown<<<dim3((lw[k] + 15) / 16, (lh[k] + 15) / 16), b, 0, stream>>>(src, sw, sh, levels[k], lw[k], lh[k]);
        src = levels[k];
        sw = lw[k];
        sh = lh[k];
    }
    // accumulate from the smallest level up: acc_k = level_k + up(acc_{k+1})
    cudaMemcpyAsync(accum[nLevels - 1], levels[nLevels - 1], size_t(lw[nLevels - 1]) * lh[nLevels - 1] * sizeof(float4), cudaMemcpyDeviceToDevice, stream);
    for (int k = nLevels - 2; k >= 0; --k)
        bloomUp<<<dim3((lw[k] + 15) / 16, (lh[k] + 15) / 16), b, 0, stream>>>(accum[k + 1], lw[k + 1], lh[k + 1], levels[k], accum[k], lw[k], lh[k]);
    const int n = lw[0] * lh[0];
    bloomScale<<<(n + 255) / 256, 256, 0, stream>>>(accum[0], n, 1.0f / nLevels);
}
