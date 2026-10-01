// Device-side helpers: random numbers, sampling, robust ray offsets.
#pragma once
#include <optix.h>
#include "scenes/altinsaat/altinsaat_params.h"
#include "shared/spectrum.h"

extern "C" {
__constant__ Params params;
}

// ---- random numbers: PCG32 seeded per pixel, per frame
struct Rng {
    unsigned long long state;
    __device__ unsigned int next() {
        const unsigned long long old = state;
        state = old * 6364136223846793005ull + 1442695040888963407ull;
        const unsigned int xorshifted = (unsigned int)(((old >> 18u) ^ old) >> 27u);
        const unsigned int rot = (unsigned int)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }
    __device__ float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }
    __device__ float2 uniform2() { const float a = uniform(); return make_float2(a, uniform()); }
};
static __forceinline__ __device__ unsigned int hashU32(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static __forceinline__ __device__ Rng makeRng(unsigned int a, unsigned int b, unsigned int c) {
    Rng r;
    r.state = ((unsigned long long)hashU32(a ^ hashU32(b + 0x9e3779b9u)) << 32) | hashU32(c * 0x85ebca6bu + a);
    r.next();
    return r;
}

// ---- sampling
static __forceinline__ __device__ float3 sampleCosineHemisphere(float2 u) {
    const float r = sqrtf(u.x);
    const float phi = 2.0f * M_PIf * u.y;
    return make_float3(r * cosf(phi), r * sinf(phi), sqrtf(fmaxf(0.0f, 1.0f - u.x)));
}
// Uniform direction in a cone around +z with 1 - cos(theta_max) = oneMinusCosMax.
static __forceinline__ __device__ float3 sampleCone(float2 u, float oneMinusCosMax) {
    const float oneMinusCos = u.x * oneMinusCosMax;
    const float cosT = 1.0f - oneMinusCos;
    const float sinT = sqrtf(fmaxf(0.0f, oneMinusCos * (2.0f - oneMinusCos)));
    const float phi = 2.0f * M_PIf * u.y;
    return make_float3(sinT * cosf(phi), sinT * sinf(phi), cosT);
}
// Uniform point in a regular polygon with n blades (radius 1), for the lens aperture.
static __forceinline__ __device__ float2 samplePolygon(float2 u, int n, float rot) {
    const float k = floorf(u.x * n);
    const float ux = u.x * n - k;            // reuse the remainder
    float a = sqrtf(ux), b = u.y;            // uniform point in triangle (0, v0, v1)
    const float t0 = rot + 2.0f * M_PIf * k / n, t1 = rot + 2.0f * M_PIf * (k + 1.0f) / n;
    const float2 v0 = make_float2(cosf(t0), sinf(t0)), v1 = make_float2(cosf(t1), sinf(t1));
    return v0 * (a * (1.0f - b)) + v1 * (a * b);
}
static __forceinline__ __device__ float powerHeuristic(float a, float b) {
    const float a2 = a * a, b2 = b * b;
    return a2 + b2 > 0.0f ? a2 / (a2 + b2) : 0.0f;
}
static __forceinline__ __device__ float henyeyGreenstein(float cosT, float g) {
    const float d = 1.0f + g * g - 2.0f * g * cosT;
    return (1.0f - g * g) / (4.0f * M_PIf * d * sqrtf(d));
}

// Robust ray origin offset (Waechter & Binder, Ray Tracing Gems ch. 6). n is the geometric normal on
// the side the new ray leaves from.
static __forceinline__ __device__ float3 offsetRay(float3 p, float3 n) {
    const float origin = 1.0f / 32.0f, floatScale = 1.0f / 65536.0f, intScale = 256.0f;
    const int3 of = make_int3(int(intScale * n.x), int(intScale * n.y), int(intScale * n.z));
    const float3 pi = make_float3(__int_as_float(__float_as_int(p.x) + (p.x < 0 ? -of.x : of.x)),
                                  __int_as_float(__float_as_int(p.y) + (p.y < 0 ? -of.y : of.y)),
                                  __int_as_float(__float_as_int(p.z) + (p.z < 0 ? -of.z : of.z)));
    return make_float3(fabsf(p.x) < origin ? p.x + floatScale * n.x : pi.x,
                       fabsf(p.y) < origin ? p.y + floatScale * n.y : pi.y,
                       fabsf(p.z) < origin ? p.z + floatScale * n.z : pi.z);
}

// ---- small spectral helpers on the 4 hero wavelengths
struct Wavelengths {
    float4 lambda;
    float4 pdf;      // 0 for a terminated secondary wavelength
};
static __forceinline__ __device__ Wavelengths sampleWavelengths(float u) {
    Wavelengths w;
    float* l = &w.lambda.x;
    float* p = &w.pdf.x;
    for (int i = 0; i < 4; ++i) {
        float ui = u + 0.25f * i;
        if (ui >= 1.0f) ui -= 1.0f;
        l[i] = sampleVisibleWavelength(ui);
        p[i] = visibleWavelengthPdf(l[i]);
    }
    return w;
}
// The path became wavelength dependent (dispersive refraction): only the hero carries on (pbrt-v4).
static __forceinline__ __device__ void terminateSecondary(Wavelengths& w) {
    if (w.pdf.y == 0.0f && w.pdf.z == 0.0f && w.pdf.w == 0.0f) return;
    w.pdf = make_float4(w.pdf.x * 0.25f, 0.0f, 0.0f, 0.0f);
}
static __forceinline__ __device__ float3 spectrumToXYZ(float4 L, const Wavelengths& w) {
    float3 xyz = make_float3(0.0f, 0.0f, 0.0f);
    const float* l = &w.lambda.x;
    const float* p = &w.pdf.x;
    const float* v = &L.x;
    for (int i = 0; i < 4; ++i)
        if (p[i] > 0.0f) xyz += cieXYZ(l[i]) * (v[i] / p[i]);
    return xyz * (0.25f / CIE_Y_INTEGRAL);
}

// Linear interpolation into the 1 nm sun table.
static __forceinline__ __device__ float sunTableAt(float lambda) {
    const float x = clampf(lambda - LAMBDA_MIN, 0.0f, float(SUN_TABLE_N - 1) - 0.001f);
    const int i = int(x);
    const float f = x - i;
    return lerpf(params.sunTable[i], params.sunTable[i + 1], f);
}
