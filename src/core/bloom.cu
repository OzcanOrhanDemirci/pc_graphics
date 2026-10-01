#include "core/bloom.h"
#include "shared/vec.h"

#include <algorithm>

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
