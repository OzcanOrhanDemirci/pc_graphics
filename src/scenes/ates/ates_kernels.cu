// Scene 12 "Ateş": small CUDA kernels of the renderer: the fire light's cumulative sum (for the surfaces' samples), the
// picture's luminance on a sparse grid (the eye's adaptation).
#include "scenes/ates/ates_renderer.h"

#include <cub/device/device_scan.cuh>

namespace {
__global__ void kLum(const float3* light, int n, float* lum) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float3 c = light[i];
    lum[i] = fmaxf(0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z, 0.0f);
}
// each cell's mean luminance and mean squared luminance over all its pixels (a point sample a cell lands on a thin
// flame or misses it from frame to frame)
__global__ void kSampleLum(const float4* hdr, int w, int gx, int gy, int sx, int sy, float* out) {
    const int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= gx * gy) return;
    const int i = t % gx, j = t / gx;
    float s = 0.0f, s2 = 0.0f;
    for (int y = j * sy; y < (j + 1) * sy; ++y)
        for (int x = i * sx; x < (i + 1) * sx; ++x) {
            const float4 c = hdr[size_t(y) * size_t(w) + size_t(x)];
            const float Y = fmaxf(0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z, 0.0f);
            s += Y;
            s2 += Y * Y;
        }
    const float inv = 1.0f / float(sx * sy);
    out[t] = s * inv;
    out[gx * gy + t] = s2 * inv;
}
}   // namespace

void atesSampleLum(const float4* hdr, int w, int h, int gx, int gy, float* out, cudaStream_t st) {
    kSampleLum<<<(gx * gy + 255) / 256, 256, 0, st>>>(hdr, w, gx, gy, w / gx, h / gy, out);
}

void atesLightCdf(const float3* light, int n, float* lum, float* cdf, void*& tmp, size_t& tmpBytes, cudaStream_t st) {
    kLum<<<(n + 255) / 256, 256, 0, st>>>(light, n, lum);
    size_t need = 0;
    cub::DeviceScan::InclusiveSum(nullptr, need, lum, cdf, n, st);
    if (need > tmpBytes) {
        if (tmp) cudaFree(tmp);
        cudaMalloc(&tmp, need);
        tmpBytes = need;
    }
    cub::DeviceScan::InclusiveSum(tmp, tmpBytes, lum, cdf, n, st);
}
