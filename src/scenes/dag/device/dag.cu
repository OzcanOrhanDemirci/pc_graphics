// Scene 13 "Dağ": the OptiX programs (compiled at run time by NVRTC; F5 recompiles).
//   __raygen__render   every pixel: the same fixed sub-pixel samples every frame; each a visibility ray through the real
//                      ground (no level of detail: every cell the terrace can see is a triangle), its surface lit by the
//                      sun through the air, the air's light on the way read from the tables
//   __raygen__final    exposure, glare, AgX, sRGB, a fixed dither
// Nothing here is random: every frame is the same frame from the same place.
#include <optix.h>

#include "scenes/dag/dag_shared.h"
#include "shared/display.h"

extern "C" {
__constant__ DagParams params;
}

extern "C" __global__ void __miss__none() {}

static __forceinline__ __device__ unsigned int hash32(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
static __forceinline__ __device__ float3 baryLerp(float3 a, float3 b, float3 c, float2 w) { return a * (1.0f - w.x - w.y) + b * w.x + c * w.y; }

// the point of the sphere's surface under p: its plane coordinates, its altitude, the local up
struct Under {
    float mx, mz, alt;
    float3 up;
};
static __forceinline__ __device__ Under underOf(float3 p) {
    const float rho2 = p.x * p.x + p.z * p.z;
    const float Y = p.y + params.h0 + params.Re;
    const float q2 = rho2 / (Y * Y);
    const float k = (params.Re / Y) * (1.0f - q2 / 3.0f + q2 * q2 * 0.2f);
    Under u;
    u.mx = p.x * k;
    u.mz = p.z * k;
    u.alt = p.y + params.h0 + rho2 / (2.0f * Y) * (1.0f - q2 * 0.25f);
    u.up = normalize(make_float3(p.x, Y, p.z));
    return u;
}

// the clip stack at a point: the finest level holding it, its mip level from the footprint; blended across a level's edge
static __forceinline__ __device__ float4 clipRead(const DagClip* stack, float mx, float mz, float foot) {
    const float e = fmaxf(fabsf(mx), fabsf(mz));
    int k = 0;
    while (k < DAG_CLIPS - 1 && e >= stack[k].half * 0.98f) ++k;
    const DagClip& C = stack[k];
    const float s = 0.5f / C.half;
    const float lod = log2f(fmaxf(foot / C.cell, 1e-3f));
    float4 a = tex2DLod<float4>(cudaTextureObject_t(C.tex), (mx + C.half) * s, (mz + C.half) * s, fmaxf(lod, 0.0f));
    if (k + 1 < DAG_CLIPS) {
        const float f = (e / C.half - 0.90f) / 0.08f;
        if (f > 0.0f) {
            const DagClip& D = stack[k + 1];
            const float sd = 0.5f / D.half;
            const float lodd = log2f(fmaxf(foot / D.cell, 1e-3f));
            const float4 b = tex2DLod<float4>(cudaTextureObject_t(D.tex), (mx + D.half) * sd, (mz + D.half) * sd, fmaxf(lodd, 0.0f));
            const float w = fminf(f, 1.0f);
            a = a * (1.0f - w) + b * w;
        }
    }
    return a;
}

// ---------------------------------------------------------------------------------------------------------------
// the air

// the light let through towards the sun from altitude alt at cosine mu
static __forceinline__ __device__ float3 sunThroughAir(float alt, float mu) {
    const float Rg = params.Re, top = params.atmTop;
    alt = fmaxf(alt, 0.5f);
    const float r = Rg + alt;
    const float rho = sqrtf(alt * (2.0f * Rg + alt));
    if (mu < -rho / r) return make_float3(0, 0, 0);
    const float H2 = top * (2.0f * Rg + top);
    const float H = sqrtf(H2);
    const float rmu = r * mu;
    const float disc = rmu * rmu + (top - alt) * (2.0f * Rg + top + alt);
    const float d = -rmu + sqrtf(fmaxf(disc, 0.0f));
    const float dmin = top - alt, dmax = rho + H;
    const float u = (d - dmin) / (dmax - dmin), v = rho / H;
    const float4 t = tex2DLod<float4>(cudaTextureObject_t(params.transmittance), u, v, 0.0f);
    return make_float3(t.x, t.y, t.z);
}

static __forceinline__ __device__ float3 skyLookup(float3 d) {
    const float az = atan2f(d.x, -d.z);
    const float el = asinf(clampf(d.y, -1.0f, 1.0f));
    const float l = el + params.eyeDip;
    const float lmax = 1.5707963f + params.eyeDip, lmin = 1.5707963f - params.eyeDip;
    const float v = l >= 0.0f ? 0.5f + 0.5f * sqrtf(l / lmax) : 0.5f - 0.5f * sqrtf(-l / lmin);
    const float u = az * 0.15915494f + 0.5f;
    const float4 c = tex2DLod<float4>(cudaTextureObject_t(params.skyView), u, v, 0.0f);
    return make_float3(c.x, c.y, c.z);
}

// the air between the terrace and a point: what it adds, what it lets through
static __forceinline__ __device__ void airTo(float3 p, float3& S, float3& T) {
    const float r = length(p);
    const float k = 1.0f / fmaxf(r, 1e-3f);
    const float az = atan2f(p.x, -p.z);
    const float el = asinf(clampf(p.y * k, -1.0f, 1.0f));
    const float l = (el + params.eyeDip) / params.froxElMax;
    const float q = l >= 0.0f ? sqrtf(fminf(l, 1.0f)) : -sqrtf(fminf(-l, 1.0f));
    const float u = az * 0.15915494f + 0.5f, v = 0.5f + 0.5f * q;
    const float w = logf(fmaxf(r, params.froxR0) / params.froxR0) / params.froxLogK;
    const float wn = (clampf(w, 0.0f, 1.0f) * float(params.froxD - 1) + 0.5f) / float(params.froxD);
    const float4 s = tex3D<float4>(cudaTextureObject_t(params.froxelS), u, v, wn);
    const float4 t = tex3D<float4>(cudaTextureObject_t(params.froxelT), u, v, wn);
    S = make_float3(s.x, s.y, s.z);
    T = make_float3(t.x, t.y, t.z);
    if (r < params.froxR0) {
        const float a = r / params.froxR0;
        S = S * a;
        T = make_float3(1.0f, 1.0f, 1.0f) + (T - make_float3(1.0f, 1.0f, 1.0f)) * a;
    }
}

static __forceinline__ __device__ float3 skyIrradiance(float3 n) {
    return fmaxf3(params.skySh[0] + params.skySh[1] * n.x + params.skySh[2] * n.y + params.skySh[3] * n.z, make_float3(0, 0, 0));
}

// ---------------------------------------------------------------------------------------------------------------
// noise for what is drawn, not random: the same at the same place
static __forceinline__ __device__ float vnoise(float x, float y) {
    const float xf = floorf(x), yf = floorf(y);
    const int xi = int(xf), yi = int(yf);
    const float fx = x - xf, fy = y - yf;
    const float u = fx * fx * (3.0f - 2.0f * fx), v = fy * fy * (3.0f - 2.0f * fy);
    const auto h = [](int a, int b) { return float(hash32(unsigned(a) * 0x8DA6B343u ^ unsigned(b) * 0xD8163841u) & 0xFFFFu) / 65535.0f; };
    return (1.0f - v) * ((1.0f - u) * h(xi, yi) + u * h(xi + 1, yi)) + v * ((1.0f - u) * h(xi, yi + 1) + u * h(xi + 1, yi + 1));
}

// the distance to the sea (the sphere), or < 0
static __forceinline__ __device__ float seaHit(float3 o, float3 d) {
    const double Re = double(params.Re);
    const double ox = o.x, oy = double(o.y) + double(params.h0) + Re, oz = o.z;
    const double b = double(d.x) * ox + double(d.y) * oy + double(d.z) * oz;
    if (b >= 0.0) return -1.0f;
    const double c = ox * ox + oz * oz + (double(o.y) + double(params.h0)) * (double(o.y) + double(params.h0) + 2.0 * Re);
    const double disc = b * b - c;
    if (disc < 0.0) return -1.0f;
    const double t = -b - sqrt(disc);
    return t > 0.0 ? float(t) : -1.0f;
}

// the high cloud between the eye and the sky: its share and light
static __forceinline__ __device__ float3 cirrusOver(float3 skyL, float3 o, float3 d) {
    if (!params.cirrus || d.y < -0.05f) return skyL;
    const double Re = double(params.Re);
    const double ox = o.x, oy = double(o.y) + double(params.h0) + Re, oz = o.z;
    const double R = Re + double(params.cirrusAlt);
    const double b = double(d.x) * ox + double(d.y) * oy + double(d.z) * oz;
    const double c = ox * ox + oy * oy - R * R;
    const double disc = b * b - c;
    if (disc < 0.0) return skyL;
    const float t = float(-b + sqrt(disc));
    if (t <= 0.0f) return skyL;
    const float3 p = o + d * t;
    const float foot = t * params.pixelAngle / fmaxf(fabsf(d.y) + 0.02f, 0.02f);
    const float lod = log2f(fmaxf(foot / (params.cirrusTile / 4096.0f), 1e-3f));
    const float m0 = tex2DLod<float>(cudaTextureObject_t(params.cirrus), p.x / params.cirrusTile + 0.37f, p.z / params.cirrusTile + 0.11f, fmaxf(lod, 0.0f));
    const float m1 = tex2DLod<float>(cudaTextureObject_t(params.cirrus), p.x / (params.cirrusTile * 0.37f) + 0.71f, p.z / (params.cirrusTile * 0.37f) - 0.23f,
                                     fmaxf(lod + 1.43f, 0.0f));
    const float cover0 = saturate(((m0 * 0.7f + m1 * 0.3f) - (1.0f - params.cirrusCover)) / 0.3f);
    const float cover = cover0 * cover0;
    if (cover <= 0.0f) return skyL;
    // along the slanted path the thin sheet thickens, up to the sheet's breadth
    const float sec = 1.0f / fmaxf(fabsf(d.y) + 0.05f, 0.05f);
    const float tau = cover * params.cirrusThick * fminf(sec, 4.0f);
    const float a = 1.0f - expf(-tau);
    const Under u = underOf(p);
    const float muS = dot(params.sunDir, u.up);
    const float3 sunT = sunThroughAir(u.alt, muS);
    // the path up to the cloud takes as much of the cloud's light as of the sun's along the same air, in ratio
    const float muV = dot(d, u.up);
    const float3 Te = sunThroughAir(fmaxf(o.y + params.h0, 1.0f), d.y);
    const float3 Tc = sunThroughAir(u.alt, muV);
    const float3 Tv = make_float3(Te.x / fmaxf(Tc.x, 1e-4f), Te.y / fmaxf(Tc.y, 1e-4f), Te.z / fmaxf(Tc.z, 1e-4f));
    const float cs = dot(d, params.sunDir);
    const float g = 0.75f;
    const float hg = (1.0f - g * g) / (12.566371f * powf(fmaxf(1.0f + g * g - 2.0f * g * cs, 1e-3f), 1.5f));
    const float phase = 0.35f * hg + 0.65f * 0.0795775f;
    const float3 lit = params.sunE * sunT * (phase * 1.0f) + skyIrradiance(make_float3(0, 1, 0)) * (0.0795775f * 0.6f);
    return skyL * (1.0f - a) + fminf3(Tv, make_float3(1, 1, 1)) * lit * a;
}

static __forceinline__ __device__ float3 skyColour(float3 o, float3 d, int k) {
    float3 c = skyLookup(d);
    c = cirrusOver(c, o, d);
    const float cs = dot(d, params.sunDir);
    if (cs > params.sunCos) {
        const float sinA = sqrtf(fmaxf(1.0f - cs * cs, 0.0f));
        const float mu = sqrtf(fmaxf(1.0f - sqr(sinA / sinf(params.sunRad)), 0.0f));
        const float limb = 0.3f + 0.93f * mu - 0.23f * mu * mu;
        const float area = M_PIf * params.sunRad * params.sunRad;
        c += params.sunE * sunThroughAir(o.y + params.h0, d.y) * (limb / area);
    }
    return c;
}

// the sea
static __forceinline__ __device__ float3 shadeSea(float3 o, float3 d, float t) {
    const float3 p = o + d * t;
    const Under u = underOf(p);
    const float3 n = u.up;
    const float3 v = -d;
    const float cv = fmaxf(dot(n, v), 1e-3f);
    // large calm and rippled patches of the sea: the wind's slicks, seen as the sun's path breaking up
    const float nA = vnoise(u.mx / 9000.0f, u.mz / 9000.0f) * 0.55f + vnoise(u.mx / 2600.0f, u.mz / 2600.0f) * 0.3f + vnoise(u.mx / 800.0f, u.mz / 800.0f) * 0.15f;
    const float s2 = params.seaSlope2 * (0.55f + 0.9f * nA);
    const float cl = dot(n, params.sunDir);
    // the sun's glitter: Beckmann with the waves' slopes, Smith's masking: over the footprint the waves are never seen one by one
    const float3 hv = normalize(v + params.sunDir);
    const float ch = fmaxf(dot(n, hv), 1e-3f);
    const float vh = fmaxf(dot(v, hv), 0.0f);
    const float tan2 = (1.0f - ch * ch) / (ch * ch);
    const float D = expf(-tan2 / s2) / (M_PIf * s2 * ch * ch * ch * ch);
    const float F0 = 0.02f;
    const float Fh = F0 + (1.0f - F0) * powf(1.0f - vh, 5.0f);
    // Smith's G for the Beckmann slopes, rational fit
    const auto lambda = [&](float c) {
        const float a = c / sqrtf(fmaxf((1.0f - c * c), 1e-6f) * s2);
        return a < 1.6f ? (1.0f - 1.259f * a + 0.396f * a * a) / (3.535f * a + 2.181f * a * a) : 0.0f;
    };
    const float cll = fmaxf(cl, 1e-3f);
    const float G = 1.0f / (1.0f + lambda(cv) + lambda(cll));
    float3 c = make_float3(0, 0, 0);
    if (cl > -0.02f) {
        const float3 sunT = sunThroughAir(u.alt + 2.0f, cl);
        c += params.sunE * sunT * (Fh * D * G / (4.0f * cv));
    }
    // the sky in the waves: at this grazing angle a wave's far side is what is seen, tilted towards the eye. Three slopes
    const float sg = sqrtf(s2 * 0.5f);
    const float3 wt[3] = {make_float3(0.25f, 0.5f, 0.25f), make_float3(0.0f, 0.0f, 0.0f), make_float3(0.0f, 0.0f, 0.0f)};
    const float slopes[3] = {0.3f * sg, 1.0f * sg, 2.0f * sg};
    const float3 side = normalize(cross(v, n));
    const float3 towards = normalize(cross(n, side));
    float3 refl = make_float3(0, 0, 0);
    for (int i = 0; i < 3; ++i) {
        const float3 nn = normalize(n + towards * slopes[i]);
        const float cvn = fmaxf(dot(v, nn), 0.0f);
        const float F = F0 + (1.0f - F0) * powf(1.0f - cvn, 5.0f);
        float3 r = d - nn * (2.0f * dot(d, nn));
        // the planet's up differs at the point from the eye's up: the table is by the eye's
        if (r.y < 0.0f) r.y = -r.y;
        refl += skyLookup(normalize(r)) * (F * wt[0].x * (i == 1 ? 2.0f : 1.0f));
    }
    c += refl;
    // what comes up out of the water
    c += params.seaColour * skyIrradiance(n) * INV_PIf;
    return c;
}

// the rock's grain over the satellite's colour: a scan on each axis by the normal's share (no stretching on steep faces)
static __forceinline__ __device__ void triplanar(int which, float tile, float rot, float3 p, float3 bw, float foot, float3& colour, float3& normal) {
    const float s = 1.0f / tile;
    const float L = fmaxf(log2f(fmaxf(foot * s * 8192.0f, 1e-3f)), 0.0f);
    const float cr = cosf(rot), sr = sinf(rot);
    float3 c = make_float3(0, 0, 0), nb = make_float3(0, 0, 0);
    const auto one = [&](float u, float v, float w, float3 axisU, float3 axisV) {
        if (w < 0.03f) return;
        const float uu = (cr * u - sr * v) * s, vv = (sr * u + cr * v) * s;
        const float4 a = tex2DLod<float4>(cudaTextureObject_t(params.rockAlbedo[which]), uu, vv, L);
        const float4 m = tex2DLod<float4>(cudaTextureObject_t(params.rockNormal[which]), uu, vv, L);
        c += make_float3(a.x, a.y, a.z) * w;
        const float tu = m.x * 2.0f - 1.0f, tv = -(m.y * 2.0f - 1.0f);
        // back from the turned tile to the axes
        const float bu = cr * tu + sr * tv, bv = -sr * tu + cr * tv;
        nb += (axisU * bu + axisV * bv) * w;
    };
    const float3 X = make_float3(1, 0, 0), Y = make_float3(0, 1, 0), Z = make_float3(0, 0, 1);
    one(p.z, p.y, bw.x, Z, Y);
    one(p.x, p.z, bw.y, X, Z);
    one(p.x, p.y, bw.z, X, Y);
    colour = c;
    normal = nb;
}

// the sun on the ground: what the air lets through, the share of its disk over the horizon. The light of the surface: sun and sky
static __forceinline__ __device__ float3 groundLight(const Under& u, float3 n, float3 ngIn, float3 p, float ao, float horizon, bool traced, float offset, int k) {
    const float muS = dot(params.sunDir, u.up);
    const float e = asinf(clampf(muS, -1.0f, 1.0f));
    // the sun's disk behind the horizon: the share of it seen, linear in the height of the disk over the horizon
    float vis = saturate((e - horizon + params.sunRad) / (2.0f * params.sunRad));
    vis = vis * vis * (3.0f - 2.0f * vis);
    const float cl = dot(n, params.sunDir);
    float3 E = make_float3(0, 0, 0);
    if (cl > 0.0f && vis > 0.0f) {
        const float3 sunT = sunThroughAir(u.alt + 5.0f, muS);
        // the rock's own shadows: one shadow ray a sample, each to its own part of the sun's disk
        float rayVis = 1.0f;
        if (traced) {
            const float3 sd = normalize(params.sunDir + (make_float3(1, 0, 0) * params.sunDisk[k].x + make_float3(0, 0, 1) * params.sunDisk[k].y) * params.sunRad);
            optixTraverse(params.handle, p + ngIn * offset + sd * offset, sd, 0.0f, 6000.0f, 0.0f, OptixVisibilityMask(255),
                          OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, 0, 1, 0);
            rayVis = optixHitObjectIsHit() ? 0.0f : 1.0f;
        }
        E += params.sunE * sunT * (cl * vis * rayVis);
    }
    E += skyIrradiance(n) * ao;
    return E;
}

// a surface seen in the water: the satellite's colour and the sun, no grain
static __forceinline__ __device__ float3 shadeGroundPlain(float3 p, float foot) {
    const Under u = underOf(p);
    const float4 info = clipRead(params.info, u.mx, u.mz, foot);
    const float sx = (info.x - 0.5f) * params.slopeScale, sz = (info.y - 0.5f) * params.slopeScale;
    const float horizon = params.horizonLo + (params.horizonHi - params.horizonLo) * info.w;
    const float3 ex = normalize(make_float3(1, 0, 0) - u.up * u.up.x), ez = normalize(make_float3(0, 0, 1) - u.up * u.up.z);
    const float3 n = normalize(u.up - ex * sx - ez * sz);
    const float4 al = clipRead(params.albedo, u.mx, u.mz, foot);
    const float3 albedo = make_float3(al.x, al.y, al.z) * params.albedoGain;
    return albedo * groundLight(u, n, n, p, info.z, horizon, false, 0.0f, 0) * INV_PIf;
}

// the ground's own light. polar: the amplified mesh (its normal given, its rock traced); otherwise the far mesh
static __forceinline__ __device__ float3 shadeGround(float3 p, float3 d, float t, float foot, bool polar, int k, float3 polarN, float polarAo, float cellSize) {
    const Under u = underOf(p);
    const float4 info = clipRead(params.info, u.mx, u.mz, foot);
    const float sx = (info.x - 0.5f) * params.slopeScale, sz = (info.y - 0.5f) * params.slopeScale;
    const float share = info.z;
    const float horizon = params.horizonLo + (params.horizonHi - params.horizonLo) * info.w;
    const float3 ex = normalize(make_float3(1, 0, 0) - u.up * u.up.x), ez = normalize(make_float3(0, 0, 1) - u.up * u.up.z);
    const float3 ngIn = polar ? polarN : normalize(u.up - ex * sx - ez * sz);
    float3 n = ngIn;
    const float4 al = clipRead(params.albedo, u.mx, u.mz, foot);
    float3 macro = make_float3(al.x, al.y, al.z) * params.albedoGain;
    float3 albedo = macro;
    const float macroLum = luminance709(macro);
    float ao = polar ? polarAo * fminf(share * 1.3f, 1.0f) : share;

    // the rock's grain: the scans at the scales that show at this footprint
    if (foot < 10.0f && params.rockAlbedo[0]) {
        float3 bw = make_float3(fabsf(n.x), fabsf(n.y), fabsf(n.z));
        bw = make_float3(bw.x * bw.x * bw.x * bw.x, bw.y * bw.y * bw.y * bw.y, bw.z * bw.z * bw.z * bw.z);
        bw = bw * (1.0f / fmaxf(bw.x + bw.y + bw.z, 1e-4f));
        // large: the cliffs from the air at a scale of a kilometre; middle: the same at their own scale; fine: the rock in hand
        const float wLarge = saturate(log2f(10.0f / fmaxf(foot, 0.01f)) * 0.5f) * (1.0f - saturate(log2f(0.5f / fmaxf(foot, 0.01f)) * 0.5f));
        const float wMid = saturate(log2f(1.5f / fmaxf(foot, 0.001f)) * 0.5f) * (1.0f - saturate(log2f(0.02f / fmaxf(foot, 0.001f)) * 0.5f));
        const float wField = saturate(log2f(0.6f / fmaxf(foot, 0.001f)) * 0.5f) * (1.0f - saturate(log2f(0.04f / fmaxf(foot, 0.001f)) * 0.5f));
        const float wFine = saturate(log2f(0.05f / fmaxf(foot, 0.0005f)) * 0.5f);
        float3 cSum = make_float3(0, 0, 0), nSum = make_float3(0, 0, 0);
        float wSum = 0.0f;
        float3 c, nn;
        // (the terrace and its shoulders are scree whatever a single stone's side says)
        const float steepMix = saturate((0.90f - dot(ngIn, u.up)) / 0.12f) * saturate((sqrtf(p.x * p.x + p.z * p.z) - 12.0f) / 10.0f);
        if (wLarge > 0.01f) {
            triplanar(0, params.rockTile[2], 0.61f, p, bw, foot, c, nn);
            cSum += c * (wLarge / params.rockMean[0]);
            nSum += nn * wLarge;
            wSum += wLarge;
        }
        if (wMid > 0.01f) {
            triplanar(0, params.rockTile[1], 0.0f, p, bw, foot, c, nn);
            cSum += c * (wMid / params.rockMean[0]);
            nSum += nn * wMid;
            wSum += wMid;
        }
        if (wField > 0.01f) {
            // the grain again at eight times its size: a field of boulders; on the steep, rock faces
            triplanar(1, params.rockTile[3], 1.13f, p, bw, foot, c, nn);
            c = c * (1.0f / params.rockMean[1]);
            if (steepMix > 0.02f && params.rockAlbedo[2]) {
                float3 c2, n2;
                triplanar(2, params.rockTile[3] * 1.3f, 0.47f, p, bw, foot, c2, n2);
                c = c + (c2 * (1.0f / params.rockMean[2]) - c) * steepMix;
                nn = nn + (n2 - nn) * steepMix;
            }
            cSum += c * wField;
            nSum += nn * (wField * 0.6f);
            wSum += wField;
        }
        // the cliffs' scan is meadow and rock from the air: only its light and dark go over the satellite's colour
        float3 g = make_float3(1, 1, 1);
        if (wSum > 0.0f) {
            const float iw = 1.0f / wSum;
            const float gl = luminance709(cSum * iw);
            g = make_float3(gl, gl, gl);
        }
        // on bare rock the grain carries its contrast; forest, meadow and snow keep the satellite's colour
        const float green = saturate((macro.y - fmaxf(macro.x, macro.z)) * 40.0f);
        const float snow = saturate((macroLum - 0.32f) / 0.16f);
        const float bare = (1.0f - green * 0.8f) * (1.0f - snow);
        const float wide = saturate(wSum);
        albedo = macro * (make_float3(1, 1, 1) + (g - make_float3(1, 1, 1)) * (bare * wide));
        float3 nTotal = wSum > 0.0f ? nSum * (wide / wSum) * (0.3f + 0.7f * bare) : make_float3(0, 0, 0);
        // close up the scan is the ground itself: its own colour (the satellite only tints it)
        if (wFine > 0.01f) {
            triplanar(1, params.rockTile[0], 0.33f, p, bw, foot, c, nn);
            if (steepMix > 0.02f && params.rockAlbedo[2]) {
                float3 c2, n2;
                triplanar(2, params.rockTile[0] * 1.6f, 0.91f, p, bw, foot, c2, n2);
                const float cl2 = luminance709(c2);
                c2 = (make_float3(cl2, cl2, cl2) * 0.6f + c2 * 0.4f) * 1.5f;
                c = c + (c2 - c) * steepMix;
                nn = nn + (n2 - nn) * steepMix;
            }
            const float3 scan = c * params.rockGain;
            const float tintBack = saturate(macroLum / 0.12f);
            albedo = albedo * (1.0f - wFine) + scan * (0.75f + 0.25f * tintBack) * wFine;
            nTotal = nTotal * (1.0f - wFine) + nn * wFine;
        }
        n = normalize(n + nTotal);
    }
    // the snow close by: the satellite's blur over ten metres becomes a sharp, torn edge against the rock
    if (foot < 6.0f) {
        const float near = saturate(log2f(6.0f / fmaxf(foot, 0.01f)) * 0.5f);
        const float tear = (vnoise(p.x * 0.31f + 3.0f, p.z * 0.31f) - 0.5f) * 0.5f + (vnoise(p.x * 1.3f, p.z * 1.3f + 9.0f) - 0.5f) * 0.3f + (vnoise(p.x * 5.1f, p.z * 5.1f) - 0.5f) * 0.2f;
        const float amount = saturate((macroLum - 0.22f) / 0.25f + tear * 0.9f);
        const float cover = amount * amount * (3.0f - 2.0f * amount);
        // where the satellite mixed snow and rock the rock takes back its own brightness
        const float cap = macroLum > 0.13f ? 0.13f / macroLum : 1.0f;
        const float3 rock = albedo * (1.0f + (cap - 1.0f) * near);
        const float3 snowA = make_float3(0.86f, 0.88f, 0.92f);
        const float c2 = cover * near + saturate((macroLum - 0.22f) / 0.25f) * (1.0f - near);
        albedo = rock + (snowA * params.albedoGainSnow - rock) * c2;
        n = normalize(n + (ngIn - n) * c2);
    }
    float3 E = groundLight(u, n, ngIn, p, ao, horizon, polar, 0.02f + cellSize * 0.2f, k);
    float3 surf = albedo * E * INV_PIf;

    // water: the elevation model's flat cells; a mirror, slightly rippled
    const float water = 1.0f - al.w;
    if (water > 0.02f) {
        const float rip = 0.002f + 0.006f * vnoise(u.mx / 40.0f, u.mz / 40.0f);
        const float3 wn = normalize(u.up + ex * ((vnoise(u.mx / 3.0f, u.mz / 3.0f) - 0.5f) * rip) + ez * ((vnoise(u.mx / 3.0f + 17.0f, u.mz / 3.0f) - 0.5f) * rip));
        const float cv = fmaxf(-dot(d, wn), 0.0f);
        const float F = 0.02f + 0.98f * powf(1.0f - cv, 5.0f);
        float3 r = d - wn * (2.0f * dot(d, wn));
        if (r.y < 0.0f) r.y = -r.y * 0.2f + 1e-3f;
        r = normalize(r);
        float3 mirror;
        optixTraverse(params.handle, p + u.up * 0.5f, r, 0.0f, 80000.0f, 0.0f, OptixVisibilityMask(255), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0);
        if (optixHitObjectIsHit()) {
            const float tr = optixHitObjectGetRayTmax();
            const float3 q = p + u.up * 0.5f + r * tr;
            float3 S, T;
            airTo(q, S, T);
            // (the air on the way from here to the far side, read as if from the terrace: close enough for a lake's reflection)
            mirror = shadeGroundPlain(q, (t + tr) * params.pixelAngle) * T + S;
        } else {
            mirror = skyLookup(r);
        }
        const float3 w = mirror * F + params.seaColour * skyIrradiance(u.up) * INV_PIf;
        surf = surf * (1.0f - water) + w * water;
    }
    if (params.debug == 1) return albedo;
    if (params.debug == 2) return n * 0.5f + make_float3(0.5f, 0.5f, 0.5f);
    return surf;
}

// a scanned rock: its colour brought to the summit's grey, its normal map; the sun and the sky like the ground's
static __forceinline__ __device__ float3 shadeStone(const DagGeom& g, unsigned int prim, float2 b, const float m[12], float3 p, float3 d, float t, float foot, int k) {
    const uint3 tri = g.idx[prim];
    const float3 q0 = g.pos[tri.x], q1 = g.pos[tri.y], q2 = g.pos[tri.z];
    const auto xfP = [&](float3 q) { return make_float3(m[0] * q.x + m[1] * q.y + m[2] * q.z + m[3], m[4] * q.x + m[5] * q.y + m[6] * q.z + m[7], m[8] * q.x + m[9] * q.y + m[10] * q.z + m[11]); };
    const auto xfV = [&](float3 q) { return make_float3(m[0] * q.x + m[1] * q.y + m[2] * q.z, m[4] * q.x + m[5] * q.y + m[6] * q.z, m[8] * q.x + m[9] * q.y + m[10] * q.z); };
    const float3 p0 = xfP(q0), p1 = xfP(q1), p2 = xfP(q2);
    float3 n = normalize(xfV(baryLerp(g.nrm[tri.x], g.nrm[tri.y], g.nrm[tri.z], b)));
    float3 ng = cross(p1 - p0, p2 - p0);
    const float ngl = length(ng);
    ng = ngl > 1e-12f ? ng * (1.0f / ngl) : n;
    if (dot(ng, n) < 0.0f) ng = -ng;
    if (dot(ng, d) > 0.0f) {
        ng = -ng;
        n = -n;
    }
    const float2 t0 = g.uv[tri.x], t1 = g.uv[tri.y], t2 = g.uv[tri.z];
    const float2 uv = t0 * (1.0f - b.x - b.y) + t1 * b.x + t2 * b.y;
    const float2 du1 = t1 - t0, du2 = t2 - t0;
    const float uvArea = 0.5f * fabsf(du1.x * du2.y - du1.y * du2.x) * g.texelArea;
    const float cosT = fmaxf(fabsf(dot(ng, d)), 0.2f);
    const float lod = fmaxf(0.5f * log2f(fmaxf(uvArea, 1e-9f) / fmaxf(ngl * 0.5f, 1e-12f)) + log2f(fmaxf(foot, 1e-6f) / cosT), 0.0f);
    const float4 a = tex2DLod<float4>(cudaTextureObject_t(g.albedo), uv.x, uv.y, lod);
    float3 albedo = make_float3(a.x, a.y, a.z);
    const float gl = luminance709(albedo);
    albedo = (make_float3(gl, gl, gl) * 0.65f + albedo * 0.35f) * (params.rockGain * 1.5f);
    const float det = du1.x * du2.y - du1.y * du2.x;
    if (fabsf(det) > 1e-12f && g.normal) {
        const float r = 1.0f / det;
        const float3 e1 = p1 - p0, e2 = p2 - p0;
        float3 T = (e1 * du2.y - e2 * du1.y) * r;
        const float3 B = (e2 * du1.x - e1 * du2.x) * r;
        T = T - n * dot(n, T);
        const float tl = length(T);
        if (tl > 1e-12f) {
            T = T * (1.0f / tl);
            const float hand = dot(cross(n, T), B) < 0.0f ? -1.0f : 1.0f;
            const float3 Bn = cross(n, T) * hand;
            const float4 nm = tex2DLod<float4>(cudaTextureObject_t(g.normal), uv.x, uv.y, lod);
            n = normalize(T * (nm.x * 2.0f - 1.0f) + Bn * (-(nm.y * 2.0f - 1.0f)) + n * fmaxf(nm.z * 2.0f - 1.0f, 0.05f));
        }
    }
    if (dot(n, d) > -0.02f) n = normalize(n - d * (dot(n, d) + 0.02f));
    const Under u = underOf(p);
    const float4 info = clipRead(params.info, u.mx, u.mz, foot);
    const float horizon = params.horizonLo + (params.horizonHi - params.horizonLo) * info.w;
    return albedo * groundLight(u, n, ng, p, 0.85f, horizon, true, 0.003f + foot, k) * INV_PIf;
}

static __forceinline__ __device__ float3 shadeMesh(unsigned int id, unsigned int prim, float2 b, float3 p, float3 d, float t, int k) {
    const DagGeom& g = params.geoms[id - DAG_ID_RAIL];
    const uint3 tri = g.idx[prim];
    float3 n = normalize(baryLerp(g.nrm[tri.x], g.nrm[tri.y], g.nrm[tri.z], b));
    const float3 ngw = normalize(cross(g.pos[tri.y] - g.pos[tri.x], g.pos[tri.z] - g.pos[tri.x]));
    float3 off = ngw;
    if (dot(off, d) > 0.0f) off = -off;
    if (dot(n, d) > 0.0f) n = -n;
    const float3 sd = normalize(params.sunDir + (make_float3(1, 0, 0) * params.sunDisk[k].x + make_float3(0, 0, 1) * params.sunDisk[k].y) * params.sunRad);
    optixTraverse(params.handle, p + off * 0.003f, sd, 0.001f, 3000.0f, 0.0f, OptixVisibilityMask(255),
                  OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, 0, 1, 0);
    const float rayVis = optixHitObjectIsHit() ? 0.0f : 1.0f;
    const float3 sunT = sunThroughAir(p.y + params.h0, params.sunDir.y);
    const float3 v = -d;
    const float3 h = normalize(v + params.sunDir);
    const float cl = fmaxf(dot(n, params.sunDir), 0.0f), cv = fmaxf(dot(n, v), 1e-3f), ch = fmaxf(dot(n, h), 0.0f), vh = fmaxf(dot(v, h), 0.0f);
    const float a2 = g.rough * g.rough * g.rough * g.rough;
    const float dn = ch * ch * (a2 - 1.0f) + 1.0f;
    const float D = a2 / (M_PIf * dn * dn);
    const float kk = (g.rough + 1.0f) * (g.rough + 1.0f) * 0.125f;
    const float G = (cl / (cl * (1.0f - kk) + kk)) * (cv / (cv * (1.0f - kk) + kk));
    // the paint: a dielectric coat over its colour, worn by the weather in patches
    const float wear = g.material == 2 ? vnoise(p.x * 7.0f + p.y * 3.0f, p.z * 7.0f - p.y * 3.0f) * 0.5f + vnoise(p.x * 41.0f + p.y * 17.0f, p.z * 41.0f - p.y * 23.0f) * 0.5f : 0.5f;
    const float3 F0 = g.material == 2 ? g.tint * (0.7f + 0.6f * wear) : g.tint;
    const float3 F = F0 + (make_float3(1, 1, 1) - F0) * powf(1.0f - vh, 5.0f);
    const float3 spec = F * (D * G / (4.0f * cv * fmaxf(cl, 1e-3f)));
    // the sky in the metal, blurred by its roughness: a look along the reflection (the table is smooth enough)
    float3 r = d - n * (2.0f * dot(d, n));
    r = normalize(r + n * (g.rough * 0.5f));
    if (r.y < 0.0f) r.y *= -0.3f;
    r = normalize(r + (make_float3(1, 0, 0) * params.sunDisk[k].x + make_float3(0, 0, 1) * params.sunDisk[k].y + make_float3(0, 1, 0) * params.sunDisk[k].x * params.sunDisk[k].y) * (g.rough * g.rough * 0.5f));
    float3 seen;
    optixTraverse(params.handle, p + off * 0.003f, r, 0.001f, 2.0e6f, 0.0f, OptixVisibilityMask(255), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0);
    if (optixHitObjectIsHit()) {
        const float tr = optixHitObjectGetRayTmax();
        const float3 q = p + r * tr;
        float3 S2, T2;
        airTo(q, S2, T2);
        seen = shadeGroundPlain(q, tr * params.pixelAngle * 8.0f) * T2 + S2;
    } else {
        seen = skyLookup(r);
    }
    float3 c = seen * (F0 + (make_float3(1, 1, 1) - F0) * powf(1.0f - cv, 5.0f));
    c += params.sunE * sunT * spec * (cl * rayVis);
    if (g.material == 1) c = c + g.tint * INV_PIf * (params.sunE * sunT * (cl * rayVis) + skyIrradiance(n));
    if (g.material == 2) c = c + make_float3(0.05f, 0.05f, 0.052f) * ((1.4f - wear) * INV_PIf) * (params.sunE * sunT * (cl * rayVis) + skyIrradiance(n));
    return c;
}

// ---------------------------------------------------------------------------------------------------------------
extern "C" __global__ void __raygen__render() {
    const uint3 li = optixGetLaunchIndex();
    const unsigned int px = li.x, py = li.y;
    if (params.layout != sizeof(DagParams)) {
        params.hdr[py * params.width + px] = make_float4(1, 0, 1, 1);
        return;
    }
    const float spread = params.pixelAngle / sqrtf(float(params.spp));
    float3 acc = make_float3(0, 0, 0);
    float wsum = 0.0f;
    float farthest = -1.0f;   // of the ground this pixel's samples hit (the far distance's evidence, in the picture's fourth channel)
    for (int k = 0; k < params.spp; ++k) {
        const float2 o = params.pattern[k];
        const float x = ((float(px) + 0.5f + o.x) / float(params.width)) * 2.0f - 1.0f;
        const float y = 1.0f - ((float(py) + 0.5f + o.y) / float(params.height)) * 2.0f;
        const float3 d = normalize(params.W + params.U * x + params.V * y);
        optixTraverse(params.handle, params.eye, d, 0.02f, 2.0e6f, 0.0f, OptixVisibilityMask(255), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0);
        const bool hit = optixHitObjectIsHit();
        float t = hit ? optixHitObjectGetRayTmax() : -1.0f;
        const float tSea = d.y < 0.0f ? seaHit(params.eye, d) : -1.0f;
        if (k == 0 && params.probe && px == params.width / 2 && py == params.height / 2) {
            const bool ground = hit && (tSea < 0.0f || t < tSea);
            params.probe[0] = make_float4(ground ? t : tSea, ground ? 1.0f : tSea > 0.0f ? 2.0f : 0.0f, 0.0f, 0.0f);
        }
        if (hit && (tSea < 0.0f || t < tSea)) farthest = fmaxf(farthest, t);
        float3 c;
        if (!hit && tSea < 0.0f) {
            c = skyColour(params.eye, d, k);
        } else if (!hit || (tSea > 0.0f && tSea < t)) {
            const float3 p = params.eye + d * tSea;
            float3 S, T;
            airTo(p, S, T);
            c = params.debug == 4 ? shadeSea(params.eye, d, tSea) : shadeSea(params.eye, d, tSea) * T + S;
            if (params.debug == 3) c = make_float3(tSea * 1e-6f, tSea * 1e-6f, 1.0f);
        } else {
            const unsigned int id = optixHitObjectGetInstanceId();
            const unsigned int prim = optixHitObjectGetPrimitiveIndex();
            const float2 bary = optixHitObjectGetTriangleBarycentrics();
            float3 ng = make_float3(0, 1, 0);
            const float3 p = params.eye + d * t;
            float3 surf;
            if (id >= DAG_ID_STONE) {
                float mtx[12];
                optixHitObjectGetObjectToWorldTransformMatrix(mtx);
                surf = shadeStone(params.geoms[id - DAG_ID_RAIL], prim, bary, mtx, p, d, t, t * spread, k);
            } else if (id == DAG_ID_RAIL) {
                surf = shadeMesh(id, prim, bary, p, d, t, k);
            } else if (id == DAG_ID_NEAR || id == DAG_ID_MID) {
                // the polar normal map
                const DagPolarTex& P = params.polar[id == DAG_ID_NEAR ? 0 : 1];
                const float r = sqrtf(p.x * p.x + p.z * p.z);
                const float az = atan2f(p.x, -p.z);
                const float u = az * 0.15915494f + (az < 0.0f ? 1.0f : 0.0f);
                const float v = logf(fmaxf(r, P.r0) / P.r0) / P.logQ;
                const float foot = t * spread;
                const float cell = fmaxf(r, P.r0) * P.cellAngle;
                const float lod = log2f(fmaxf(foot / cell, 1e-3f));
                const float4 ni = tex2DLod<float4>(cudaTextureObject_t(P.tex), u, v, fmaxf(lod, 0.0f));
                const float nx = ni.x * 2.0f - 1.0f, nz = ni.y * 2.0f - 1.0f;
                const float3 n = normalize(make_float3(nx, sqrtf(fmaxf(1.0f - nx * nx - nz * nz, 0.0f)), nz));
                surf = shadeGround(p, d, t, foot, true, k, n, ni.z, cell);
            } else {
                surf = shadeGround(p, d, t, t * spread, false, k, ng, 1.0f, 0.0f);
            }
            float3 S, T;
            airTo(p, S, T);
            c = params.debug == 4 ? surf : surf * T + S;
            if (params.debug == 3) c = make_float3(t * 1e-6f, t * 1e-5f, t * 1e-4f);
        }
        if (!(isfinite(c.x) && isfinite(c.y) && isfinite(c.z))) {
            if (params.badCount) atomicAdd(params.badCount, 1u);
            continue;
        }
        // resolved as the eye will see it: a bright sky beside a dark ridge stays a clean edge after the display's curve
        const float w = 1.0f / (1.0f + luminance709(c) * params.exposure);
        acc += c * w;
        wsum += w;
    }
    if (wsum > 0.0f) acc = acc * (1.0f / wsum);
    params.hdr[py * params.width + px] = make_float4(acc.x, acc.y, acc.z, farthest);
}

extern "C" __global__ void __raygen__final() {
    const uint3 li = optixGetLaunchIndex();
    const unsigned int px = li.x, py = li.y, i = py * params.width + px;
    const float4 h = params.hdr[i];
    float3 c = make_float3(h.x, h.y, h.z);
    if (params.bloom) {
        const int bw = int(params.width / 2), bh = int(params.height / 2);
        const float fx = fminf(fmaxf((float(px) + 0.5f) * 0.5f - 0.5f, 0.0f), float(bw - 1)), fy = fminf(fmaxf((float(py) + 0.5f) * 0.5f - 0.5f, 0.0f), float(bh - 1));
        const int x0 = int(fx), y0 = int(fy), x1 = min(x0 + 1, bw - 1), y1 = min(y0 + 1, bh - 1);
        const float ax = fx - x0, ay = fy - y0;
        const float4 b = params.bloom[y0 * bw + x0] * ((1 - ax) * (1 - ay)) + params.bloom[y0 * bw + x1] * (ax * (1 - ay)) +
                         params.bloom[y1 * bw + x0] * ((1 - ax) * ay) + params.bloom[y1 * bw + x1] * (ax * ay);
        c = c * (1.0f - params.bloomStrength) + make_float3(b.x, b.y, b.z) * params.bloomStrength;
    }
    if (params.debug == 1 || params.debug == 2 || params.debug == 3) {
        // plain
    } else {
        c *= params.exposure;
        // the eye opens up in the dark parts of a scene that holds a bright sky: a gain on the dark, none on the bright
        const float Ls = luminance709(c);
        c *= 1.0f + params.shadowLift / (1.0f + Ls / params.liftKnee);
        const float u = (float(px) + 0.5f) / params.width - 0.5f, v = (float(py) + 0.5f) / params.height - 0.5f;
        const float r2 = (u * u + v * v) * 2.0f;
        c *= 1.0f - params.vignette * r2 * r2 * 0.5f - params.vignette * r2 * 0.5f;
        c = agx(c * params.white);
        const float Y = luminance709(c);
        c = fmaxf3(make_float3(Y, Y, Y) + (c - make_float3(Y, Y, Y)) * params.saturation, make_float3(0, 0, 0));
    }
    const float dz = (float(hash32(px * 1973u + py * 9277u) & 1023u) / 1023.0f - 0.5f) / 255.0f;
    params.output[i] = make_uchar4((unsigned char)(srgbOetf(c.x + dz) * 255.0f + 0.5f), (unsigned char)(srgbOetf(c.y + dz) * 255.0f + 0.5f),
                                   (unsigned char)(srgbOetf(c.z + dz) * 255.0f + 0.5f), 255);
}

// ---------------------------------------------------------------------------------------------------------------
// the oven: how open each vertex of a polar mesh is to the sky (the rock's own crevices), fixed directions turned per vertex
extern "C" __global__ void __raygen__bakeAO() {
    const uint3 li = optixGetLaunchIndex();
    const int a = int(li.x), j = int(li.y) + params.bakeRing0;
    const int nA = params.bakeAz, nR = params.bakeRings;
    const float3 p = params.bakePos[size_t(j) * nA + a];
    const float3 pa = params.bakePos[size_t(j) * nA + (a + nA - 1) % nA], pb = params.bakePos[size_t(j) * nA + (a + 1) % nA];
    const float3 pc = params.bakePos[size_t(max(j - 1, 0)) * nA + a], pd = params.bakePos[size_t(min(j + 1, nR - 1)) * nA + a];
    float3 n = normalize(cross(pb - pa, pc - pd));
    if (n.y < 0.0f) n = -n;
    const float r = sqrtf(p.x * p.x + p.z * p.z);
    const float cell = r * params.bakeCellAngle;
    const float3 t = normalize(fabsf(n.y) < 0.9f ? cross(n, make_float3(0, 1, 0)) : cross(n, make_float3(1, 0, 0)));
    const float3 b = cross(n, t);
    const unsigned int h = hash32(unsigned(j) * 7919u + unsigned(a));
    const float turn = float(h & 0xFFFFu) / 65535.0f * 6.2831853f;
    const float tmax = fminf(params.bakeReach * fmaxf(r, 1.0f), params.bakeReachMax);
    int open = 0;
    for (int k = 0; k < params.bakeRays; ++k) {
        // cosine weighted, on a Fibonacci spiral
        const float u = (float(k) + 0.5f) / float(params.bakeRays);
        const float rad = sqrtf(u), ph = float(k) * 2.39996323f + turn;
        const float3 d = t * (rad * cosf(ph)) + b * (rad * sinf(ph)) + n * sqrtf(1.0f - u);
        optixTraverse(params.handle, p + n * (0.003f + cell * 0.3f), d, 0.0f, tmax, 0.0f, OptixVisibilityMask(255),
                      OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, 0, 1, 0);
        if (!optixHitObjectIsHit()) ++open;
    }
    params.bakeOut[size_t(j) * nA + a] = float(open) / float(params.bakeRays);
}
