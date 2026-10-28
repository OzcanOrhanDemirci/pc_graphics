// Scene 12 "Ateş": small CUDA kernels of the renderer: the fire light's cumulative sum (for the surfaces' samples), the
// picture's luminance on a sparse grid (the eye's adaptation), the smoke's light.
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
// the light blocks worth their light to the smoke, in a list (their centre and intensity)
__global__ void kSmokeList(const float3* light, int lnx, int lny, int lnz, float3 origin, float cell, float floorCd, float4* list, unsigned int* count,
                           unsigned int cap) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= lnx * lny * lnz) return;
    const float3 c = light[i];
    if (0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z < floorCd) return;
    const unsigned int at = atomicAdd(count, 1u);
    if (at >= cap) return;
    const int x = i % lnx, y = (i / lnx) % lny, z = i / (lnx * lny);
    list[2 * at] = make_float4(origin.x + (x + 0.5f) * cell, origin.y + (y + 0.5f) * cell, origin.z + (z + 0.5f) * cell, 0.0f);
    list[2 * at + 1] = make_float4(c.x, c.y, c.z, 0.0f);
}
// each smoke cell's fluence (lux): the listed blocks' intensities over the squared distance, a block a ball (no point
// nearer than half its side)
__global__ void kSmokeLight(const float4* list, const unsigned int* count, unsigned int cap, int sx, int sy, int sz, float3 origin, float scell, float soft,
                            float4* out) {
    __shared__ float4 tile[2 * 128];
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    const int x = i % sx, y = (i / sx) % sy, z = i / (sx * sy);
    const float3 p = make_float3(origin.x + (x + 0.5f) * scell, origin.y + (y + 0.5f) * scell, origin.z + (z + 0.5f) * scell);
    const unsigned int n = min(*count, cap);
    float3 E = make_float3(0, 0, 0);
    for (unsigned int base = 0; base < n; base += 128) {
        const unsigned int k = base + threadIdx.x;
        if (k < n) {
            tile[2 * threadIdx.x] = list[2 * k];
            tile[2 * threadIdx.x + 1] = list[2 * k + 1];
        }
        __syncthreads();
        const unsigned int m = min(128u, n - base);
        for (unsigned int t = 0; t < m; ++t) {
            const float4 b = tile[2 * t], I = tile[2 * t + 1];
            const float dx = b.x - p.x, dy = b.y - p.y, dz = b.z - p.z;
            const float w = 1.0f / (dx * dx + dy * dy + dz * dz + soft);
            E.x += I.x * w;
            E.y += I.y * w;
            E.z += I.z * w;
        }
        __syncthreads();
    }
    if (i < sx * sy * sz) out[i] = make_float4(E.x, E.y, E.z, 0.0f);
}
}   // namespace

void atesSmokeLight(const float3* light, int lnx, int lny, int lnz, float3 origin, float lightCell, float4* list, unsigned int* count, unsigned int cap,
                    int sx, int sy, int sz, float smokeCell, float4* out, cudaStream_t st) {
    cudaMemsetAsync(count, 0, sizeof(unsigned int), st);
    const int n = lnx * lny * lnz;
    // (a block under a thousandth of a candela gives a tenth of a lux at 10 cm: nothing beside the flame's thousands)
    kSmokeList<<<(n + 255) / 256, 256, 0, st>>>(light, lnx, lny, lnz, origin, lightCell, 1e-3f, list, count, cap);
    const int m = sx * sy * sz;
    kSmokeLight<<<(m + 127) / 128, 128, 0, st>>>(list, count, cap, sx, sy, sz, origin, smokeCell, 0.25f * lightCell * lightCell, out);
}

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
