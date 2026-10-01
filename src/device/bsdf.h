// Materials: procedural surface description (textures) and scattering functions.
// Non-delta lobes (diffuse, coated, conductor) are evaluated and sampled here; delta events
// (dielectric interfaces, soap film) are in specular.h.
#pragma once
#include "common.h"
#include "noise.h"

#define COAT_IOR 1.5f

// Everything shading needs at one hit, after textures are applied.
struct Surface {
    float3 p;          // world position
    float3 ng;         // geometric normal, facing the incoming ray
    float3 ns;         // shading normal, same hemisphere as ng
    Frame frame;       // around ns
    int type;
    int matId;
    float4 albedo;     // diffuse reflectance at the 4 wavelengths
    float4 f0;         // conductor reflectance at normal incidence
    float alpha;       // GGX alpha
    float coat;        // coat weight (0: plain diffuse)
    float cavity;      // ambient-occlusion-like darkening already folded into albedo (for AOV)
};

// ---- GGX microfacet distribution (isotropic)
static __forceinline__ __device__ float ggxD(float cosH, float a) {
    const float a2 = a * a;
    const float d = cosH * cosH * (a2 - 1.0f) + 1.0f;
    return a2 / (M_PIf * d * d);
}
static __forceinline__ __device__ float ggxLambdaTerm(float cosV, float a) {
    const float a2 = a * a;
    return sqrtf(a2 + (1.0f - a2) * cosV * cosV);
}
static __forceinline__ __device__ float ggxG1(float cosV, float a) {
    return 2.0f * cosV / (cosV + ggxLambdaTerm(cosV, a));
}
// Height-correlated masking-shadowing divided by (4 cosO cosI): the "visibility" term.
static __forceinline__ __device__ float ggxVis(float cosO, float cosI, float a) {
    return 0.5f / (cosO * ggxLambdaTerm(cosI, a) + cosI * ggxLambdaTerm(cosO, a));
}
// Visible normal sampling (Heitz 2018), wo in the local frame (z up).
static __forceinline__ __device__ float3 sampleGGXVNDF(float3 wo, float a, float2 u) {
    const float3 vh = normalize(make_float3(a * wo.x, a * wo.y, wo.z));
    const float lensq = vh.x * vh.x + vh.y * vh.y;
    const float3 t1 = lensq > 0.0f ? make_float3(-vh.y, vh.x, 0.0f) * (1.0f / sqrtf(lensq)) : make_float3(1.0f, 0.0f, 0.0f);
    const float3 t2 = cross(vh, t1);
    const float r = sqrtf(u.x), phi = 2.0f * M_PIf * u.y;
    const float p1 = r * cosf(phi);
    float p2 = r * sinf(phi);
    const float s = 0.5f * (1.0f + vh.z);
    p2 = (1.0f - s) * sqrtf(fmaxf(0.0f, 1.0f - p1 * p1)) + s * p2;
    const float3 nh = t1 * p1 + t2 * p2 + vh * sqrtf(fmaxf(0.0f, 1.0f - p1 * p1 - p2 * p2));
    return normalize(make_float3(a * nh.x, a * nh.y, fmaxf(1e-6f, nh.z)));
}
static __forceinline__ __device__ float ggxReflectPdf(float3 wo, float3 h, float a) {
    // pdf of wi = reflect(wo, h) under VNDF sampling: G1(wo) D(h) / (4 cosO)
    return ggxG1(wo.z, a) * ggxD(h.z, a) / (4.0f * wo.z);
}

// ---- textures
// Planar coordinates on an axis-aligned architectural surface: (along wall, up) for walls, (x, z) for floors.
// du/dv receive the world directions of the two coordinates (for bump mapping).
static __forceinline__ __device__ float2 archUV(float3 p, float3 n, float3& du, float3& dv) {
    const float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);
    if (ay >= ax && ay >= az) { du = make_float3(1, 0, 0); dv = make_float3(0, 0, 1); return make_float2(p.x, p.z); }
    if (ax >= az) { du = make_float3(0, 0, 1); dv = make_float3(0, 1, 0); return make_float2(p.z, p.y); }
    du = make_float3(1, 0, 0); dv = make_float3(0, 1, 0);
    return make_float2(p.x, p.y);
}
// Tilts the shading normal by a height gradient given along the surface's u and v directions.
static __forceinline__ __device__ float3 bumpNormal(float3 n, float3 du, float3 dv, float gu, float gv) {
    float3 t = du * gu + dv * gv;
    t = t - n * dot(t, n);
    return normalize(n - t);
}

// Ando concrete: 1820 x 910 mm form panels, six tie holes each, V-joints, mottled tone.
static __forceinline__ __device__ void textureConcrete(const Material& m, Surface& s, float4 lambda) {
    const float pw = m.p.x, ph = m.p.y, holeR = m.p.z;
    float3 du3, dv3;
    const float2 uv = archUV(s.p, s.ng, du3, dv3);
    const float iu = floorf(uv.x / pw), iv = floorf(uv.y / ph);
    const float lu = uv.x - iu * pw, lv = uv.y - iv * ph;         // metres inside the panel
    const unsigned int panel = pcgHash(hash2i(int(iu), int(iv)) ^ (fabsf(s.ng.x) > 0.5f ? 11u : (fabsf(s.ng.z) > 0.5f ? 23u : 37u)));
    float tone = (hashToFloat(panel) - 0.5f) * 0.10f;              // panel-to-panel pour variation
    // mottling at several scales, with a slight vertical streaking of the pour
    tone += 0.07f * fbm2(make_float2(uv.x * 1.3f, uv.y * 0.6f), 4).x;
    tone += 0.035f * fbm2(make_float2(uv.x * 9.0f + 3.1f, uv.y * 9.0f), 3).x;
    const float mixB = saturate(0.5f + 2.5f * fbm2(make_float2(uv.x * 0.7f + 5.1f, uv.y * 0.7f + 2.3f), 3).x);
    float cavity = 1.0f;
    float bu = 0.0f, bv = 0.0f;
    // V-joints between panels: 3 mm wide
    const float du = fminf(lu, pw - lu), dv = fminf(lv, ph - lv);
    const float jw = 0.003f;
    if (du < jw || dv < jw) {
        cavity *= 0.72f;
        if (du < dv) bu = (lu < pw * 0.5f ? 1.0f : -1.0f) * 0.6f;   // the groove's walls face its centre line
        else bv = (lv < ph * 0.5f ? 1.0f : -1.0f) * 0.6f;
    }
    // tie holes: 3 x 2 per panel
    const float hu[3] = {0.30f, 0.91f, 1.52f};
    const float hv[2] = {0.2275f, 0.6825f};
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 2; ++b) {
            const float dx = lu - hu[a] * (pw / 1.82f), dy = lv - hv[b] * (ph / 0.91f);
            const float d = sqrtf(dx * dx + dy * dy);
            if (d < holeR) {               // recessed conical plug: darker, normal facing the centre
                const float k = d / holeR;
                cavity *= 0.35f + 0.35f * k;
                bu += dx / fmaxf(d, 1e-5f) * 0.5f * k;
                bv += dy / fmaxf(d, 1e-5f) * 0.5f * k;
            } else if (d < holeR * 2.2f) { // a faint ring left by the form cone
                tone -= 0.03f * (1.0f - (d - holeR) / (holeR * 1.2f));
            }
        }
    // sparse bug holes (air pockets), 1-3 mm
    {
        const float cs = 0.02f;
        const float gx = floorf(uv.x / cs), gy = floorf(uv.y / cs);
        const unsigned int h = pcgHash(hash2i(int(gx), int(gy)) ^ 913u);
        if ((h & 15u) == 0u) {
            const float cx = (gx + 0.2f + 0.6f * hashToFloat(h * 31u)) * cs;
            const float cy = (gy + 0.2f + 0.6f * hashToFloat(h * 57u)) * cs;
            const float r = 0.0006f + 0.0012f * hashToFloat(h * 101u);
            const float d = sqrtf(sqr(uv.x - cx) + sqr(uv.y - cy));
            if (d < r) cavity *= 0.45f;
        }
    }
    const float t = 1.0f + tone * m.p.w;
    const float4 a = sigmoidSpectrum4(m.sigA, lambda), b = sigmoidSpectrum4(m.sigB, lambda);
    s.albedo = (a * (1.0f - mixB) + b * mixB) * (t * cavity);
    s.cavity = cavity;
    // micro-relief of the cast surface
    const float3 g = fbm2(uv * 60.0f, 2) * 0.012f;
    s.ns = bumpNormal(s.ns, du3, dv3, g.y + bu, g.z + bv);
    s.alpha = m.roughness;
    s.coat = m.coat;
}

// Polished terrazzo: light matrix with marble chips, brass divider strips on the 1820 x 910 grid.
static __forceinline__ __device__ void textureStone(const Material& m, Surface& s, float4 lambda, bool& isBrass) {
    float3 du3, dv3;
    const float2 uv = archUV(s.p, s.ng, du3, dv3);
    const float4 base = sigmoidSpectrum4(m.sigA, lambda);
    // chips: one per cell, check the 3x3 neighbourhood
    const float cs = m.p.y;
    const float gx = floorf(uv.x / cs), gy = floorf(uv.y / cs);
    float best = 1e9f;
    unsigned int bestH = 0u;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            const unsigned int h = pcgHash(hash2i(int(gx) + i, int(gy) + j) ^ 77u);
            const float cx = (gx + i + hashToFloat(h)) * cs, cy = (gy + j + hashToFloat(h * 7919u)) * cs;
            const float r = cs * (0.25f + 0.45f * hashToFloat(h * 104729u));
            const float d = sqrtf(sqr(uv.x - cx) + sqr(uv.y - cy)) / r;
            if (d < best) { best = d; bestH = h; }
        }
    const float edge = best + 0.25f * noiseTex(uv * 400.0f).w;
    float4 alb = base;
    if (edge < 1.0f && hashToFloat(bestH * 31337u) < m.p.x) {
        const float c = hashToFloat(bestH * 2654435761u);
        const float4 chip = sigmoidSpectrum4(m.sigB, lambda);
        const float shade = c < 0.55f ? 1.08f : (c < 0.85f ? 0.62f : 0.30f);   // white marble, grey, dark
        alb = chip * shade;
        if (c > 0.97f) alb = alb * make_float4(1.0f, 1.0f, 1.05f, 1.15f);
    }
    alb *= 1.0f + 0.05f * fbm2(uv * 2.0f, 3).x;
    s.albedo = alb;
    s.cavity = 1.0f;
    // brass strips
    const float pw = 1.82f, ph = 0.91f;
    const float lu = uv.x - floorf(uv.x / pw) * pw, lv = uv.y - floorf(uv.y / ph) * ph;
    const float d = fminf(fminf(lu, pw - lu), fminf(lv, ph - lv));
    isBrass = d < 0.002f && m.flags == 1;
    // polish: roughness varies slightly (wax, wear)
    const float wear = fbm2(make_float2(uv.x * 1.5f + 40.0f, uv.y * 1.5f), 3).x;
    s.alpha = m.roughness * (1.0f + 0.8f * fmaxf(0.0f, wear));
    s.coat = m.coat;
    const float3 g = fbm2(uv * 8.0f, 2) * 0.002f;
    s.ns = bumpNormal(s.ns, du3, dv3, g.y, g.z);
}

static __forceinline__ __device__ void texturePlaster(const Material& m, Surface& s, float4 lambda) {
    float3 du3, dv3;
    const float2 uv = archUV(s.p, s.ng, du3, dv3);
    const float v = fbm2(uv * 3.0f, 3).x;
    s.albedo = sigmoidSpectrum4(m.sigA, lambda) * (1.0f + 0.025f * v);
    s.cavity = 1.0f;
    const float3 g = fbm2(uv * 25.0f + make_float2(17.0f, 3.0f), 3) * (0.004f * m.p.x);
    s.ns = bumpNormal(s.ns, du3, dv3, g.y, g.z);
    s.alpha = 1.0f;
    s.coat = 0.0f;
}

// Brass: F0 as a smooth spectrum (sigmoid fit of the measured reflectance).
static __forceinline__ __device__ float4 schlick4(float4 f0, float cosT) {
    const float m = 1.0f - saturate(cosT);
    const float m5 = m * m * m * m * m;
    return f0 + (f4(1.0f) - f0) * m5;
}

// Fills type, albedo, normals for a non-delta surface.
static __forceinline__ __device__ void prepareSurface(Surface& s, const Material& m, float4 lambda) {
    s.type = m.type;
    s.f0 = f4(0.0f);
    s.alpha = 1.0f;
    s.coat = 0.0f;
    s.cavity = 1.0f;
    if (params.furnace || (params.debugFlags & 16)) {
        s.type = MAT_DIFFUSE;
        s.albedo = f4(params.furnace ? 1.0f : 0.5f);
        s.frame = Frame::from(s.ns);
        return;
    }
    switch (m.type) {
    case MAT_CONCRETE: textureConcrete(m, s, lambda); break;
    case MAT_STONE:
    case MAT_POOL: {
        bool brass = false;
        textureStone(m, s, lambda, brass);
        if (brass) {
            s.type = MAT_METAL;
            s.f0 = sigmoidSpectrum4(make_float3(-6.0f, 9.0f, -1.2f), lambda);
            s.alpha = 0.12f;
        }
        break;
    }
    case MAT_METAL:
        s.f0 = sigmoidSpectrum4(m.sigA, lambda);
        s.albedo = s.f0;
        s.alpha = m.roughness;
        break;
    case MAT_DUST:
        s.albedo = sigmoidSpectrum4(m.sigA, lambda);
        break;
    default: texturePlaster(m, s, lambda); break;
    }
    // the shading normal must stay on the incoming side
    if (dot(s.ns, s.ng) < 0.05f) s.ns = normalize(s.ns + s.ng * (0.05f - dot(s.ns, s.ng)));
    s.frame = Frame::from(s.ns);
}

// ---- evaluation: f(wo, wi) * |cos(wi)| and the sampling pdf, both in world space
static __forceinline__ __device__ float4 bsdfEval(const Surface& s, float3 woW, float3 wiW, float& pdf) {
    pdf = 0.0f;
    // geometric-normal side test: no light leaking through the surface
    if (dot(wiW, s.ng) <= 0.0f || dot(woW, s.ng) <= 0.0f) return f4(0.0f);
    const float3 wo = s.frame.toLocal(woW), wi = s.frame.toLocal(wiW);
    if (wi.z <= 0.0f || wo.z <= 0.0f) return f4(0.0f);
    if (s.type == MAT_METAL) {
        const float3 h = normalize(wo + wi);
        const float D = ggxD(h.z, s.alpha), V = ggxVis(wo.z, wi.z, s.alpha);
        pdf = ggxReflectPdf(wo, h, s.alpha);
        return schlick4(s.f0, dot(wo, h)) * (D * V * wi.z);
    }
    const float4 diffuse = s.albedo * (INV_PIf * wi.z);
    if (s.coat <= 0.0f) {
        pdf = wi.z * INV_PIf;
        return diffuse;
    }
    const float Fo = fresnelDielectric(wo.z, COAT_IOR) * s.coat;
    const float Fi = fresnelDielectric(wi.z, COAT_IOR) * s.coat;
    const float3 h = normalize(wo + wi);
    const float D = ggxD(h.z, s.alpha), V = ggxVis(wo.z, wi.z, s.alpha);
    const float Fh = fresnelDielectric(dot(wo, h), COAT_IOR) * s.coat;
    const float spec = D * V * Fh * wi.z;
    const float avgAlb = 0.25f * hsum(s.albedo);
    const float ps = clampf(Fo / (Fo + (1.0f - Fo) * avgAlb + 1e-4f), 0.05f, 0.95f);
    pdf = ps * ggxReflectPdf(wo, h, s.alpha) + (1.0f - ps) * wi.z * INV_PIf;
    return diffuse * ((1.0f - Fo) * (1.0f - Fi)) + f4(spec);
}

// Samples wi; returns false if the sample is invalid. weight = f * cos / pdf.
static __forceinline__ __device__ bool bsdfSample(const Surface& s, float3 woW, float3 u, float3& wiW, float4& weight, float& pdf) {
    const float3 wo = s.frame.toLocal(woW);
    if (wo.z <= 0.0f) return false;
    float3 wi;
    if (s.type == MAT_METAL) {
        const float3 h = sampleGGXVNDF(wo, s.alpha, make_float2(u.x, u.y));
        wi = reflect3(-wo, h);
    } else if (s.coat <= 0.0f) {
        wi = sampleCosineHemisphere(make_float2(u.x, u.y));
    } else {
        const float Fo = fresnelDielectric(wo.z, COAT_IOR) * s.coat;
        const float avgAlb = 0.25f * hsum(s.albedo);
        const float ps = clampf(Fo / (Fo + (1.0f - Fo) * avgAlb + 1e-4f), 0.05f, 0.95f);
        if (u.z < ps) {
            const float3 h = sampleGGXVNDF(wo, s.alpha, make_float2(u.x, u.y));
            wi = reflect3(-wo, h);
        } else {
            wi = sampleCosineHemisphere(make_float2(u.x, u.y));
        }
    }
    if (wi.z <= 0.0f) return false;
    wiW = s.frame.toWorld(wi);
    const float4 f = bsdfEval(s, woW, wiW, pdf);
    if (pdf <= 0.0f) return false;
    weight = f * (1.0f / pdf);
    return true;
}
