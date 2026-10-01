// Spectral sky and sun. The sky map is precomputed once per sun position (__raygen__sky) by single
// scattering through a spherical atmosphere (Rayleigh + Mie aerosol + ozone); the sun disk is analytic.
#pragma once
#include "common.h"

// ---- atmosphere constants (metres)
#define ATM_RG 6360e3f
#define ATM_RT 6460e3f
#define ATM_OBSERVER 60.0f
#define ATM_HR 8000.0f
#define ATM_HM 1200.0f

static __forceinline__ __device__ float rayleighScattering(float l) { const float r = 440.0f / l; return 33.1e-6f * r * r * r * r; }
// Aerosol optical depth ~0.15 at 550 nm (warm golden-hour haze), Angstrom exponent 1.3, albedo 0.9.
static __forceinline__ __device__ float mieScattering(float l) { return 1.12e-4f * powf(l / 550.0f, -1.3f); }
static __forceinline__ __device__ float mieExtinction(float l) { return mieScattering(l) / 0.9f; }
// Chappuis band of ozone, column optical depth ~0.039 at 600 nm, spread over a 15 km equivalent layer.
static __forceinline__ __device__ float ozoneAbsorption(float l) { const float t = (l - 600.0f) / 65.0f; return 0.039f * expf(-0.5f * t * t) / 15000.0f; }
static __forceinline__ __device__ float ozoneDensity(float h) { return fmaxf(0.0f, 1.0f - fabsf(h - 25000.0f) / 15000.0f); }

static __forceinline__ __device__ float raySphereFar(float3 o, float3 d, float R) {
    const float b = dot(o, d), c = dot(o, o) - R * R;
    const float disc = b * b - c;
    if (disc < 0.0f) return -1.0f;
    return -b + sqrtf(disc);
}
static __forceinline__ __device__ float raySphereNear(float3 o, float3 d, float R) {
    const float b = dot(o, d), c = dot(o, o) - R * R;
    const float disc = b * b - c;
    if (disc < 0.0f) return -1.0f;
    const float t = -b - sqrtf(disc);
    return t;
}
// Integrated densities (Rayleigh, Mie, ozone) from o along d to the top of the atmosphere.
static __forceinline__ __device__ float3 opticalDepthToSpace(float3 o, float3 d, int steps) {
    const float tMax = raySphereFar(o, d, ATM_RT);
    float3 acc = make_float3(0.0f, 0.0f, 0.0f);
    if (tMax <= 0.0f) return acc;
    const float dt = tMax / steps;
    for (int i = 0; i < steps; ++i) {
        const float3 p = o + d * ((i + 0.5f) * dt);
        const float h = length(p) - ATM_RG;
        acc += make_float3(expf(-h / ATM_HR), expf(-h / ATM_HM), ozoneDensity(h)) * dt;
    }
    return acc;
}
static __forceinline__ __device__ float transmittanceFromDepth(float3 depth, float l) {
    return expf(-(rayleighScattering(l) * depth.x + mieExtinction(l) * depth.y + ozoneAbsorption(l) * depth.z));
}

// ---- sky map parameterisation: longitude in u, elevation in v with extra resolution at the horizon
static __forceinline__ __device__ float2 skyUV(float3 d) {
    const float u = (atan2f(d.z, d.x) + M_PIf) * (0.5f * INV_PIf);
    const float e = asinf(clampf(d.y, -1.0f, 1.0f));
    const float v = 0.5f + 0.5f * copysignf(sqrtf(fabsf(e) * (2.0f * INV_PIf)), e);
    return make_float2(u, v);
}
static __forceinline__ __device__ float3 skyDir(float u, float v) {
    const float s = (v - 0.5f) * 2.0f;
    const float e = copysignf(s * s, s) * (0.5f * M_PIf);
    const float phi = u * 2.0f * M_PIf - M_PIf;
    return make_float3(cosf(e) * cosf(phi), sinf(e), cosf(e) * sinf(phi));
}

static __forceinline__ __device__ float4 skyRadiance(float3 d, float4 lambda) {
    if (params.furnace) return f4(1.0f);
    const float2 uv = skyUV(d);
    const int W = params.skyW, H = params.skyH;
    const float fx = uv.x * W - 0.5f, fy = clampf(uv.y * H - 0.5f, 0.0f, H - 1.001f);
    const int x0 = int(floorf(fx)), y0 = int(fy);
    const float tx = fx - floorf(fx), ty = fy - y0;
    const int xa = (x0 % W + W) % W, xb = (xa + 1) % W;
    const int y1 = min(y0 + 1, H - 1);
    float4 out;
    float* o = &out.x;
    const float* l = &lambda.x;
    for (int k = 0; k < 4; ++k) {
        const float fb = clampf((l[k] - LAMBDA_MIN) / SKY_BIN_NM, 0.0f, SKY_BINS - 1.001f);
        const int b = int(fb);
        const float tb = fb - b;
        auto at = [&](int x, int y) {
            const float* c = params.sky + (size_t(y) * W + x) * SKY_BINS;
            return lerpf(c[b], c[b + 1], tb);
        };
        o[k] = lerpf(lerpf(at(xa, y0), at(xb, y0), tx), lerpf(at(xa, y1), at(xb, y1), tx), ty);
    }
    return out * params.skyScale;
}

// Sun disk radiance in direction d (0 outside the disk), with limb darkening.
static __forceinline__ __device__ float4 sunRadiance(float3 d, float4 lambda) {
    if (params.furnace) return f4(0.0f);
    const float c = dot(d, params.sunDir);
    if (c < params.sunCosMax) return f4(0.0f);
    // mu = cos of the angle on the solar surface: 1 at the centre, 0 at the limb
    const float sin2 = fmaxf(0.0f, 1.0f - c * c);
    const float sinMax2 = params.sunOneMinusCosMax * (2.0f - params.sunOneMinusCosMax);
    const float mu = sqrtf(fmaxf(0.0f, 1.0f - sin2 / sinMax2));
    const float limb = 1.0f - params.sunLimb * (1.0f - mu);
    return make_float4(sunTableAt(lambda.x), sunTableAt(lambda.y), sunTableAt(lambda.z), sunTableAt(lambda.w)) * (limb * params.sunScale);
}
static __forceinline__ __device__ float sunConePdf() {
    return 1.0f / (2.0f * M_PIf * params.sunOneMinusCosMax);
}

// ---- importance sampling of the sky map (luminance x solid angle), for the bright aureole around the sun
// Jacobian of the (u, v) parameterisation: d(omega) = 4 pi^2 cos(e) |2v - 1| du dv.
static __forceinline__ __device__ float skyJacobian(float v) {
    const float s = 2.0f * v - 1.0f;
    const float e = copysignf(s * s, s) * (0.5f * M_PIf);
    return 4.0f * M_PIf * M_PIf * cosf(e) * fabsf(s);
}
static __forceinline__ __device__ float skyMapPdf(float3 d) {
    if (!params.skyPdfUV) return 0.0f;
    const float2 uv = skyUV(d);
    const int x = min(int(uv.x * params.skyW), params.skyW - 1), y = min(int(uv.y * params.skyH), params.skyH - 1);
    return params.skyPdfUV[y * params.skyW + x] / fmaxf(skyJacobian(uv.y), 1e-7f);
}
// Largest i with cdf[i] <= u in a monotone table of n + 1 entries; remainder in [0,1) within the bin.
static __forceinline__ __device__ int searchCdf(const float* cdf, int n, float u, float& rem) {
    int lo = 0, hi = n;
    while (hi - lo > 1) {
        const int mid = (lo + hi) >> 1;
        if (cdf[mid] <= u) lo = mid; else hi = mid;
    }
    const float w = cdf[lo + 1] - cdf[lo];
    rem = w > 0.0f ? clampf((u - cdf[lo]) / w, 0.0f, 0.99999f) : 0.5f;
    return lo;
}
static __forceinline__ __device__ float3 sampleSkyMap(float2 u, float& pdf) {
    float ry, rx;
    const int y = searchCdf(params.skyCdfRow, params.skyH, u.x, ry);
    const int x = searchCdf(params.skyCdfCol + size_t(y) * (params.skyW + 1), params.skyW, u.y, rx);
    const float uu = (x + rx) / params.skyW, vv = (y + ry) / params.skyH;
    pdf = params.skyPdfUV[y * params.skyW + x] / fmaxf(skyJacobian(vv), 1e-7f);
    return skyDir(uu, vv);
}
