// Scene 12 "Ateş": small CUDA kernels of the renderer: the fire light's cumulative sum (for the surfaces' samples).
#include "scenes/ates/ates_renderer.h"

#include <cub/device/device_scan.cuh>

namespace {
__global__ void kLum(const float3* light, int n, float* lum) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const float3 c = light[i];
    lum[i] = fmaxf(0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z, 0.0f);
}
}   // namespace

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
