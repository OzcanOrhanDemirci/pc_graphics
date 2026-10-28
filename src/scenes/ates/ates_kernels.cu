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
__global__ void kSampleLum(const float4* hdr, int w, int gx, int gy, int sx, int sy, float* out) {
    const int t = blockIdx.x * blockDim.x + threadIdx.x;
    if (t >= gx * gy) return;
    const int i = t % gx, j = t / gx;
    const float4 c = hdr[size_t(j * sy + sy / 2) * size_t(w) + size_t(i * sx + sx / 2)];
    out[t] = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
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
