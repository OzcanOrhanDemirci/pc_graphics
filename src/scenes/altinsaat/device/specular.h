// Delta scattering events: smooth dielectric interfaces (glass, water) and the soap film. Shared by
// camera paths (radiance transport) and photon paths (importance transport): only the radiance
// scaling at refraction differs.
#pragma once
#include "common.h"
#include "noise.h"

// ---- pool surface: a sum of capillary-gravity ripples, normal only (the geometry stays flat)
#define WATER_WAVES 28
static __forceinline__ __device__ float3 waterNormal(float3 p, float time, float amplitude) {
    float dhdx = 0.0f, dhdz = 0.0f;
    for (int i = 0; i < WATER_WAVES; ++i) {
        const unsigned int h = pcgHash(0xC0FFEEu + i * 7919u);
        const float r0 = hashToFloat(h), r1 = hashToFloat(h * 31u + 7u), r2 = hashToFloat(h * 131u + 3u);
        const float wavelength = 0.025f * powf(24.0f, (i + r0) / WATER_WAVES);          // 2.5 cm .. 60 cm
        const float k = 2.0f * M_PIf / wavelength;
        const float omega = sqrtf(9.81f * k + 7.4e-5f * k * k * k);                     // capillary-gravity
        const float ang = 0.6f + (r1 - 0.5f) * 2.4f;                                     // around a light draft
        const float2 dir = make_float2(cosf(ang), sinf(ang));
        const float steep = 0.010f * powf(wavelength / 0.2f, 0.25f);                     // slope a*k
        const float a = steep / k;
        const float ph = k * (dir.x * p.x + dir.y * p.z) - omega * time + r2 * 6.2831853f;
        const float c = cosf(ph);
        dhdx += a * k * dir.x * c;
        dhdz += a * k * dir.y * c;
    }
    return normalize(make_float3(-dhdx * amplitude, 1.0f, -dhdz * amplitude));
}

// Pure water absorption (1/m), smoothed Pope & Fry 1997.
static __forceinline__ __device__ float waterAbsorption(float l) {
    const float xs[10] = {360, 420, 480, 520, 560, 590, 610, 650, 700, 830};
    const float ys[10] = {0.012f, 0.005f, 0.015f, 0.035f, 0.065f, 0.20f, 0.29f, 0.36f, 0.65f, 3.0f};
    if (l <= xs[0]) return ys[0];
    for (int i = 1; i < 10; ++i)
        if (l < xs[i]) return lerpf(ys[i - 1], ys[i], (l - xs[i - 1]) / (xs[i] - xs[i - 1]));
    return ys[9];
}
static __forceinline__ __device__ float4 waterTransmittance(float4 l, float dist) {
    const float k = params.waterDepthTint;
    return make_float4(expf(-(waterAbsorption(l.x) + k) * dist), expf(-(waterAbsorption(l.y) + k) * dist),
                       expf(-(waterAbsorption(l.z) + k) * dist), expf(-(waterAbsorption(l.w) + k) * dist));
}

// Soap film thickness (nm) on a bubble: thin at the top where it drains, thick below, with swirls.
static __forceinline__ __device__ float filmThickness(float3 local, float4 prm, float time, float seed) {
    const float h = local.y;                                   // -1 bottom .. 1 top
    const float drain = powf(saturate(0.5f * (1.0f - h)), 1.4f);
    float d = lerpf(prm.y, prm.x, drain);
    const float3 q = local * 2.2f + make_float3(seed, 0.37f * time + seed * 1.7f, -0.21f * time);
    d += prm.z * fbm(q, 4);
    return fmaxf(d, 8.0f);
}

static __forceinline__ __device__ float3 refractDir(float3 d, float3 n, float eta, float cosI, bool& ok) {
    // eta = n_t / n_i, n faces the incoming side, cosI = -dot(d, n) > 0
    const float inv = 1.0f / eta;
    const float k = 1.0f - inv * inv * (1.0f - cosI * cosI);
    ok = k > 0.0f;
    if (!ok) return d;
    return normalize(d * inv + n * (inv * cosI - sqrtf(k)));
}

struct DeltaResult {
    float3 dir;
    bool transmitted;   // went through to the other side of the surface
    bool passThrough;   // film transmission: direction unchanged (shadow rays model it too)
    int medium;         // optical medium after the event
};

// Smooth interface. ngOut: outward geometric normal; nsOut: outward shading normal (ripples).
// beta is the spectral throughput, updated in place. importance = photon transport (no 1/eta^2).
static __forceinline__ __device__ DeltaResult dielectricEvent(const Material& m, float3 d, float3 ngOut, float3 nsOut,
                                                              Wavelengths& wl, float4& beta, float u, bool importance,
                                                              bool allowDispersion = true, int keep = 0) {
    DeltaResult r;
    r.passThrough = false;
    const bool front = dot(d, ngOut) < 0.0f;
    float3 n = front ? nsOut : -nsOut;
    const float3 ng = front ? ngOut : -ngOut;
    const int mi = front ? m.iorOutside : m.iorInside;
    const int mt = front ? m.iorInside : m.iorOutside;
    float cosI = -dot(d, n);
    if (cosI <= 1e-4f) { n = ng; cosI = -dot(d, n); }
    // stratified spectral split: the caller traces the same path once per lane; at the dispersive event lane `keep`
    // becomes the hero (moved to lane 0), so the four runs follow the four wavelengths' own refractions
    if (keep > 0 && allowDispersion && (iorIsDispersive(mt) || iorIsDispersive(mi)) && wl.pdf.y + wl.pdf.z + wl.pdf.w > 0.0f) {
        float* L = &wl.lambda.x;
        float* P = &wl.pdf.x;
        float* B = &beta.x;
        float t = L[0]; L[0] = L[keep]; L[keep] = t;
        t = P[0]; P[0] = P[keep]; P[keep] = t;
        t = B[0]; B[0] = B[keep]; B[keep] = t;
    }
    float eta[4], F[4];
    const float* l = &wl.lambda.x;
    for (int k = 0; k < 4; ++k) {
        eta[k] = iorOf(mt, l[k]) / iorOf(mi, l[k]);
        F[k] = fresnelDielectric(cosI, eta[k]);
    }
    float* b = &beta.x;
    if (u < F[0]) {
        r.dir = reflect3(d, n);
        if (dot(r.dir, ng) <= 0.0f) r.dir = reflect3(d, ng);       // ripple normal pushed it below
        for (int k = 0; k < 4; ++k) b[k] *= F[k] / F[0];
        r.transmitted = false;
        r.medium = mi;
        return r;
    }
    bool ok;
    r.dir = refractDir(d, n, eta[0], cosI, ok);
    if (!ok || dot(r.dir, ng) >= 0.0f) {                          // grazing ripple: fall back to the flat surface
        const float c = -dot(d, ng);
        r.dir = refractDir(d, ng, eta[0], c, ok);
        if (!ok) { r.dir = reflect3(d, ng); r.transmitted = false; r.medium = mi; return r; }
    }
    const bool dispersive = allowDispersion && (iorIsDispersive(mt) || iorIsDispersive(mi));
    if (dispersive) {
        terminateSecondary(wl);
        b[1] = b[2] = b[3] = 0.0f;
        if (!importance) b[0] *= 1.0f / (eta[0] * eta[0]);
    } else {
        for (int k = 0; k < 4; ++k) b[k] *= (1.0f - F[k]) / (1.0f - F[0]) * (importance ? 1.0f : 1.0f / (eta[k] * eta[k]));
    }
    r.transmitted = true;
    r.medium = mt;
    return r;
}

static __forceinline__ __device__ DeltaResult filmEvent(const Material& m, float3 d, float3 ngOut, float3 local, float seed,
                                                        const Wavelengths& wl, float4& beta, float u) {
    DeltaResult r;
    const float3 n = dot(d, ngOut) < 0.0f ? ngOut : -ngOut;
    const float cosI = fabsf(dot(d, n));
    const float th = filmThickness(local, m.p, params.time, seed);
    float R[4];
    const float* l = &wl.lambda.x;
    const float* p = &wl.pdf.x;
    float sum = 0.0f;
    int cnt = 0;
    for (int k = 0; k < 4; ++k) {
        R[k] = thinFilmReflectance(l[k], th, iorOf(IOR_WATER, l[k]), cosI);
        if (p[k] > 0.0f) { sum += R[k]; ++cnt; }
    }
    const float P = clampf(sum / cnt, 0.0f, 0.999f);
    float* b = &beta.x;
    r.medium = IOR_AIR;
    if (u < P) {
        r.dir = reflect3(d, n);
        for (int k = 0; k < 4; ++k) b[k] *= R[k] / P;
        r.transmitted = false;
        r.passThrough = false;
    } else {
        r.dir = d;
        for (int k = 0; k < 4; ++k) b[k] *= (1.0f - R[k]) / (1.0f - P);
        r.transmitted = true;
        r.passThrough = true;
    }
    return r;
}
