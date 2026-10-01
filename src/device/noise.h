// Procedural noise for materials and water: gradient noise, fbm, cell hashing. Pure functions of position.
#pragma once
#include "shared/vec.h"

static __forceinline__ __device__ unsigned int pcgHash(unsigned int v) {
    const unsigned int state = v * 747796405u + 2891336453u;
    const unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
static __forceinline__ __device__ unsigned int hash3i(int x, int y, int z) {
    return pcgHash((unsigned int)x * 73856093u ^ pcgHash((unsigned int)y * 19349663u ^ pcgHash((unsigned int)z * 83492791u)));
}
static __forceinline__ __device__ float hashToFloat(unsigned int h) { return (h >> 8) * (1.0f / 16777216.0f); }
static __forceinline__ __device__ float hash3f(int x, int y, int z) { return hashToFloat(hash3i(x, y, z)); }

static __forceinline__ __device__ float3 gradAt(int x, int y, int z) {
    const unsigned int h = hash3i(x, y, z);
    // 12 edge directions of a cube, as in improved Perlin noise
    const float u = (h & 1u) ? 1.0f : -1.0f, v = (h & 2u) ? 1.0f : -1.0f;
    switch ((h >> 2) % 3u) {
    case 0: return make_float3(u, v, 0.0f);
    case 1: return make_float3(u, 0.0f, v);
    default: return make_float3(0.0f, u, v);
    }
}
static __forceinline__ __device__ float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Gradient noise in [-1, 1] (roughly).
static __forceinline__ __device__ float gnoise(float3 p) {
    const float fx = floorf(p.x), fy = floorf(p.y), fz = floorf(p.z);
    const int ix = int(fx), iy = int(fy), iz = int(fz);
    const float3 f = make_float3(p.x - fx, p.y - fy, p.z - fz);
    const float u = fade(f.x), v = fade(f.y), w = fade(f.z);
    float n[8];
    for (int k = 0; k < 8; ++k) {
        const int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
        n[k] = dot(gradAt(ix + dx, iy + dy, iz + dz), make_float3(f.x - dx, f.y - dy, f.z - dz));
    }
    const float x00 = lerpf(n[0], n[1], u), x10 = lerpf(n[2], n[3], u);
    const float x01 = lerpf(n[4], n[5], u), x11 = lerpf(n[6], n[7], u);
    return lerpf(lerpf(x00, x10, v), lerpf(x01, x11, v), w);
}
static __forceinline__ __device__ float fbm(float3 p, int octaves, float lacunarity = 2.03f, float gain = 0.5f) {
    float sum = 0.0f, amp = 0.5f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * gnoise(p);
        p = p * lacunarity + make_float3(17.13f, 31.71f, 7.37f);
        amp *= gain;
    }
    return sum;
}
// Numerical gradient of fbm, for bump mapping.
static __forceinline__ __device__ float3 fbmGrad(float3 p, int octaves, float eps) {
    const float c = fbm(p, octaves);
    return make_float3(fbm(p + make_float3(eps, 0, 0), octaves) - c, fbm(p + make_float3(0, eps, 0), octaves) - c,
                       fbm(p + make_float3(0, 0, eps), octaves) - c) * (1.0f / eps);
}

// ---- 2D noise for planar surfaces, read from a texture baked from gnoise2 below (same source)
static __forceinline__ __device__ unsigned int hash2i(int x, int y) {
    return pcgHash((unsigned int)x * 1597334677u ^ (unsigned int)y * 3812015801u);
}
static __forceinline__ __device__ float2 grad2At(int x, int y, unsigned int seed) {
    const unsigned int h = pcgHash(hash2i(x, y) ^ seed);
    const float a = (h >> 8) * (6.2831853f / 16777216.0f);
    return make_float2(cosf(a), sinf(a));
}
// Periodic 2D gradient noise, period NOISE_PERIOD cells.
static __forceinline__ __device__ float gnoise2(float2 p, unsigned int seed) {
    const float fx = floorf(p.x), fy = floorf(p.y);
    const int ix = int(fx), iy = int(fy);
    const float ux = p.x - fx, uy = p.y - fy;
    float n[4];
    for (int k = 0; k < 4; ++k) {
        const int dx = k & 1, dy = k >> 1;
        const int cx = ((ix + dx) % NOISE_PERIOD + NOISE_PERIOD) % NOISE_PERIOD, cy = ((iy + dy) % NOISE_PERIOD + NOISE_PERIOD) % NOISE_PERIOD;
        const float2 g = grad2At(cx, cy, seed);
        n[k] = g.x * (ux - dx) + g.y * (uy - dy);
    }
    const float a = fade(ux), b = fade(uy);
    return 1.4142f * lerpf(lerpf(n[0], n[1], a), lerpf(n[2], n[3], a), b);
}
// One lookup: x = noise, y/z = its gradient (per cell), w = an independent noise. uv in cells.
static __forceinline__ __device__ float4 noiseTex(float2 uv) {
    return tex2D<float4>((cudaTextureObject_t)params.noiseTex, uv.x * (1.0f / NOISE_PERIOD), uv.y * (1.0f / NOISE_PERIOD));
}
// fbm of the texture noise with its gradient (value, d/du, d/dv); each octave rotated to hide the tiling.
static __forceinline__ __device__ float3 fbm2(float2 uv, int octaves) {
    float v = 0.0f, gu = 0.0f, gv = 0.0f, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        const float c = cosf(0.83f * i), s = sinf(0.83f * i);
        const float2 r = make_float2((c * uv.x - s * uv.y) * freq + 13.7f * i, (s * uv.x + c * uv.y) * freq + 7.3f * i);
        const float4 t = noiseTex(r);
        v += amp * t.x;
        // gradient back to uv: d/duv = freq * R^T * grad
        gu += amp * freq * (c * t.y + s * t.z);
        gv += amp * freq * (-s * t.y + c * t.z);
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return make_float3(v, gu, gv);
}
