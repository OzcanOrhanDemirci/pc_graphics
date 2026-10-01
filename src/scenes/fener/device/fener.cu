// Scene 2 "Fener": OptiX device programs, compiled at run time with NVRTC (F5 recompiles them).
//   __raygen__surface    full res: jittered primary ray, direct light (lamp beam, moon, cabin window) with ray-traced
//                        shadows, sky occlusion, the sea, analytic ambient fog. Writes depth, normal, motion.
//   __raygen__volume     half res: single scattering of the lamp (equi-angular) and the moon (transmittance
//                        sampling) along each view ray, shadowed by everything, blue-noise stratified.
//   __raygen__volresolve half res: temporal accumulation of the volume (reprojected, neighbourhood clamped).
//   __raygen__compose    full res: surface + depth-aware upsampled volume.
//   __raygen__taa        full res: temporal anti-aliasing (YCoCg neighbourhood clamp).
//   __raygen__final      glows, glare, vignette, filmic curve, grade, blue-noise TPDF dither.
// Playdead's recipe (INSIDE, GDC 2016): undersample with blue noise, let the temporal filter integrate.
#include <optix.h>
#include "scenes/fener/fener_params.h"

extern "C" {
__constant__ FenerParams params;
}

// =====================================================================================================
// small helpers
// =====================================================================================================
static __forceinline__ __device__ unsigned int hashU32(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static __forceinline__ __device__ float hashF(unsigned int x) { return (hashU32(x) >> 8) * (1.0f / 16777216.0f); }
static __forceinline__ __device__ float fracf(float x) { return x - floorf(x); }

// Blue noise, decorrelated per dimension k by an offset into the tile, advanced over time by the golden ratio
// (spatio-temporal blue noise in its simplest form: each pixel walks a low-discrepancy sequence).
static __forceinline__ __device__ float blue(unsigned int x, unsigned int y, unsigned int k) {
    const unsigned int ox = hashU32(k * 2u + 1u) & 127u, oy = hashU32(k * 2u + 2u) & 127u;
    const float b = params.blueNoise ? params.blueNoise[((y + oy) & 127u) * 128u + ((x + ox) & 127u)] : 0.5f;
    return fracf(b + 0.61803398875f * float(params.frameIndex % 4096u) + 0.38196601125f * float(k / 64u));
}

static __forceinline__ __device__ float3 sampleCosine(float u1, float u2, float3 n) {
    const float r = sqrtf(u1), phi = 2.0f * M_PIf * u2;
    const Frame f = Frame::from(n);
    return f.toWorld(make_float3(r * cosf(phi), r * sinf(phi), sqrtf(fmaxf(0.0f, 1.0f - u1))));
}
static __forceinline__ __device__ float henyeyGreenstein(float c, float g) {
    const float d = 1.0f + g * g - 2.0f * g * c;
    return (1.0f - g * g) / (4.0f * M_PIf * d * sqrtf(d));
}
// Robust ray origin offset (Waechter & Binder, Ray Tracing Gems ch. 6).
static __forceinline__ __device__ float3 offsetRay(float3 p, float3 n) {
    const float origin = 1.0f / 32.0f, floatScale = 1.0f / 65536.0f, intScale = 256.0f;
    const int3 of = make_int3(int(intScale * n.x), int(intScale * n.y), int(intScale * n.z));
    const float3 pi = make_float3(__int_as_float(__float_as_int(p.x) + (p.x < 0 ? -of.x : of.x)),
                                  __int_as_float(__float_as_int(p.y) + (p.y < 0 ? -of.y : of.y)),
                                  __int_as_float(__float_as_int(p.z) + (p.z < 0 ? -of.z : of.z)));
    return make_float3(fabsf(p.x) < origin ? p.x + floatScale * n.x : pi.x, fabsf(p.y) < origin ? p.y + floatScale * n.y : pi.y,
                       fabsf(p.z) < origin ? p.z + floatScale * n.z : pi.z);
}
static __forceinline__ __device__ float lum(float3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
static __forceinline__ __device__ float3 f3(float4 a) { return make_float3(a.x, a.y, a.z); }

// =====================================================================================================
// camera
// =====================================================================================================
static __forceinline__ __device__ float3 cameraRay(const FCamera& c, float px, float py) {   // px, py in pixels from the top left
    const float u = (2.0f * px / params.width - 1.0f) * c.tanHalfX;
    const float v = (1.0f - 2.0f * py / params.height) * c.tanHalfY;
    return normalize(c.fwd + c.right * u + c.up * v);
}
// Pixel position (unjittered) of a world point; false behind the camera.
static __forceinline__ __device__ bool projectPoint(const FCamera& c, float3 p, float2& px) {
    const float3 v = p - c.pos;
    const float z = dot(v, c.fwd);
    if (z <= 1e-4f) return false;
    px.x = (dot(v, c.right) / (z * c.tanHalfX) + 1.0f) * 0.5f * params.width;
    px.y = (1.0f - dot(v, c.up) / (z * c.tanHalfY)) * 0.5f * params.height;
    return true;
}
static __forceinline__ __device__ bool projectDir(const FCamera& c, float3 d, float2& px) {
    const float z = dot(d, c.fwd);
    if (z <= 1e-4f) return false;
    px.x = (dot(d, c.right) / (z * c.tanHalfX) + 1.0f) * 0.5f * params.width;
    px.y = (1.0f - dot(d, c.up) / (z * c.tanHalfY)) * 0.5f * params.height;
    return true;
}

// =====================================================================================================
// atmosphere: exponential height fog + thin haze, everything analytic
// =====================================================================================================
// two layers: the strait's fog (tens of metres) and a ground mist hugging the forest floor and the water (metres)
static __forceinline__ __device__ float fogSigma(float y) {
    return params.fogRho0 * expf(-fmaxf(y, -20.0f) / params.fogH) + params.fogGround * expf(-fmaxf(y, -5.0f) / params.fogGroundH) + params.fogHaze;
}
static __forceinline__ __device__ float expLayer(float y0, float dy, float t, float H) {   // integral of exp(-y/H) along the ray
    const float k = 1.0f / H;
    const float a = expf(-fmaxf(y0, -20.0f) * k);
    if (fabsf(dy) < 1e-4f) return a * t;
    return (a - expf(-fmaxf(y0 + dy * t, -20.0f) * k)) / (dy * k);
}
// optical depth from height y0 along a ray whose vertical direction component is dy, for distance t
static __forceinline__ __device__ float fogTau(float y0, float dy, float t) {
    const float h = expLayer(y0, dy, t, params.fogH) + params.fogGround / params.fogRho0 * expLayer(y0, dy, t, params.fogGroundH);
    // the haze thins with height too (scale ~2 km): looking up, the night sky shows through
    const float hazePath = fminf(fminf(t, 40000.0f), 2000.0f / fmaxf(dy, 0.05f));
    return params.fogRho0 * h + params.fogHaze * hazePath;
}
// Moonlit fog seen along d: the multiple-scattering glow of an infinitely deep fog (art-directed, unshadowed).
static __forceinline__ __device__ float3 fogAmbientL(float3 d) {
    const float c = dot(d, params.moonDir);
    const float halo = params.fogAmbientHalo * henyeyGreenstein(c, params.fogAmbientG) * 4.0f * M_PIf;
    return params.fogAmbient * ((1.0f - params.fogAmbientHalo) + halo);
}
static __forceinline__ __device__ float3 applyFog(float3 L, float3 o, float3 d, float t) {
    const float T = expf(-fogTau(o.y, d.y, t));
    return L * T + fogAmbientL(d) * (1.0f - T);
}

// Gradient noise for the clouds (cheap value noise is enough at this softness).
static __forceinline__ __device__ float vnoise(float x, float y) {
    const float ix = floorf(x), iy = floorf(y), fx = x - ix, fy = y - iy;
    const float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy);
    const unsigned int X = unsigned(int(ix)), Y = unsigned(int(iy));
    const float a = hashF(X * 73856093u ^ Y * 19349663u), b = hashF((X + 1u) * 73856093u ^ Y * 19349663u);
    const float c = hashF(X * 73856093u ^ (Y + 1u) * 19349663u), e = hashF((X + 1u) * 73856093u ^ (Y + 1u) * 19349663u);
    return lerpf(lerpf(a, b, ux), lerpf(c, e, ux), uy);
}
static __forceinline__ __device__ float fbm(float x, float y, int oct) {
    float s = 0.0f, a = 0.5f, n = 0.0f;
    for (int k = 0; k < oct; ++k) { s += a * vnoise(x, y); n += a; x = x * 2.03f + 17.1f; y = y * 2.03f + 5.3f; a *= 0.5f; }
    return s / n;
}

// The sky above the fog: night, a thin cloud veil lit near the moon, the moon's disc. Then the fog in front of it.
static __forceinline__ __device__ float3 skyL(float3 o, float3 d, bool disc) {
    const float up = fmaxf(d.y, 1e-3f);
    float3 L = params.nightSky;
    const float u = d.x / up, v = d.z / up;
    float cl = fbm(u * 0.9f + 3.0f, v * 0.9f * 0.35f + 1.0f / up * 2.4f, 4);
    cl = smoothstepf(0.45f, 0.85f, cl) * smoothstepf(0.02f, 0.15f, up) * 0.8f;
    const float c = dot(d, params.moonDir);
    const float lit = 0.25f + 3.0f * henyeyGreenstein(c, 0.75f);
    L = L * (1.0f - cl * 0.6f) + params.nightSky * 0.30f * lit * cl * 8.0f;
    if (disc) {
        const float edge = smoothstepf(params.moonCosMax - 0.00004f, params.moonCosMax + 0.00002f, c);
        L += params.moonDisc * (edge * (1.0f - 0.75f * cl));
    }
    const float T = expf(-fogTau(o.y, d.y, 1e6f));
    return L * T + fogAmbientL(d) * (1.0f - T);
}

// =====================================================================================================
// the lamp: two opposite beams (narrow in azimuth, a near-horizontal main lobe plus a weak downward spill) + leak
// =====================================================================================================
static __forceinline__ __device__ float lampIntensity(float3 w, bool leak) {
    const float az = atan2f(-w.x, -w.z), el = asinf(clampf(w.y, -1.0f, 1.0f));
    const float dv = (el - params.beamTilt) / params.beamWv, ds = (el - params.spillTilt) / params.spillWv;
    const float vert = expf(-dv * dv) + params.spillK * expf(-ds * ds);
    float g = 0.0f;
    for (int k = 0; k < 2; ++k) {
        float da = az - params.lampPhi - (k ? M_PIf : 0.0f);
        da = da - 2.0f * M_PIf * floorf((da + M_PIf) / (2.0f * M_PIf));
        const float x = da / params.beamW;
        g += expf(-x * x);
    }
    return params.lampI * g * vert + (leak ? params.lampLeak : 0.0f);
}

// =====================================================================================================
// geometry
// =====================================================================================================
struct Hit {
    bool hit;
    float t;
    float3 p, ng, ns;
    int mat;
};
static __forceinline__ __device__ float3 xformP(const FInstance& in, float3 p) {
    return make_float3(in.r0.x * p.x + in.r0.y * p.y + in.r0.z * p.z + in.r0.w, in.r1.x * p.x + in.r1.y * p.y + in.r1.z * p.z + in.r1.w,
                       in.r2.x * p.x + in.r2.y * p.y + in.r2.z * p.z + in.r2.w);
}
static __forceinline__ __device__ float3 xformV(const FInstance& in, float3 v) {   // rotation + uniform scale: normals transform alike
    return make_float3(in.r0.x * v.x + in.r0.y * v.y + in.r0.z * v.z, in.r1.x * v.x + in.r1.y * v.y + in.r1.z * v.z,
                       in.r2.x * v.x + in.r2.y * v.y + in.r2.z * v.z);
}

// Visibility masks: 1 everything that blocks light, 2 camera-only things (the lantern glass, the window pane).
static __forceinline__ __device__ Hit trace(float3 o, float3 d, float tmax, unsigned int mask, bool reorder) {
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(mask), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0);
    Hit h;
    h.hit = optixHitObjectIsHit();
    h.mat = -1;
    if (!h.hit) {
        if (reorder) optixReorder(0u, 1);
        return h;
    }
    const unsigned int ii = optixHitObjectGetInstanceIndex();
    const unsigned int prim = optixHitObjectGetPrimitiveIndex();
    const FInstance in = params.instances[ii];
    const FMesh& m = params.meshes[in.mesh];
    h.mat = m.mat[prim];
    if (reorder) optixReorder(params.materials[h.mat].kind == FK_DIFFUSE ? 1u : 0u, 1);
    h.t = optixHitObjectGetRayTmax();
    const float2 b = optixHitObjectGetTriangleBarycentrics();
    const uint3 tri = m.idx[prim];
    const float3 v0 = m.pos[tri.x], v1 = m.pos[tri.y], v2 = m.pos[tri.z];
    const float b0 = 1.0f - b.x - b.y;
    h.p = xformP(in, v0 * b0 + v1 * b.x + v2 * b.y);
    h.ng = normalize(xformV(in, cross(v1 - v0, v2 - v0)));
    h.ns = normalize(xformV(in, m.nrm[tri.x] * b0 + m.nrm[tri.y] * b.x + m.nrm[tri.z] * b.y));
    if (dot(h.ng, d) > 0.0f) h.ng = -h.ng;   // two-sided: face the incoming ray
    if (dot(h.ns, h.ng) < 0.0f) h.ns = -h.ns;
    return h;
}
static __forceinline__ __device__ bool visible(float3 o, float3 d, float tmax) {
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(1u),
                  OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, 0, 1, 0);
    return !optixHitObjectIsHit();
}

// =====================================================================================================
// direct light on a diffuse surface
// =====================================================================================================
struct ShadeRng {   // a few blue-noise dimensions per pixel per frame
    unsigned int x, y, k;
    __device__ float next() { return blue(x, y, k++); }
};

static __forceinline__ __device__ float3 directLight(float3 p, float3 n, const FMaterial& m, ShadeRng& rng, bool shadows) {
    const float3 po = offsetRay(p, n);
    const float wrap = m.wrap;
    auto cosTerm = [&](float c) { return fmaxf(0.0f, (c + wrap) / (1.0f + wrap)); };
    float3 L = make_float3(0.0f, 0.0f, 0.0f);
    // moon
    {
        const float c = cosTerm(dot(n, params.moonDir));
        if (c > 0.0f && (!shadows || visible(po, params.moonDir, 1e5f))) {
            const float Tm = expf(-fogTau(p.y, params.moonDir.y, 1e6f));
            L += params.moonE * (c * Tm);
        }
    }
    // lamp
    {
        const float3 v = params.lampPos - p;
        const float r2 = dot(v, v), r = sqrtf(r2);
        const float3 w = v / r;
        const float c = cosTerm(dot(n, w));
        if (c > 0.0f) {
            const float I = lampIntensity(-w, true);
            const float E = I / r2 * c * expf(-fogTau(p.y, w.y, r)) * params.lampSurfK;
            if (E > 1e-7f && (!shadows || visible(po, w, r - 0.6f))) L += params.lampColor * E;
        }
    }
    // cabin window: a rectangle emitting along winN
    {
        const float3 q = params.winCorner + params.winU * rng.next() + params.winV * rng.next();
        const float3 v = q - p;
        const float r2 = dot(v, v), r = sqrtf(r2);
        const float3 w = v / r;
        const float cl = -dot(w, params.winN), c = cosTerm(dot(n, w));
        if (cl > 0.0f && c > 0.0f) {
            const float area = length(cross(params.winU, params.winV));
            const float3 E = params.winRadiance * (cl * c * area / r2);
            if (maxc(E) > 1e-7f && (!shadows || visible(po, w, r - 0.05f))) L += E;
        }
    }
    return L * m.albedo * INV_PIf;
}

// Light that reaches a surface from the fogged sky around it: the fog's own glow over the open hemisphere,
// occluded by one cosine ray (the forest floor stays dark, open slopes catch the moonlit fog).
static __forceinline__ __device__ float3 skyLight(float3 p, float3 n, const FMaterial& m, ShadeRng& rng) {
    const float3 d = sampleCosine(rng.next(), rng.next(), n);
    const float open = visible(offsetRay(p, n), d, 60.0f) ? 1.0f : 0.0f;
    return fogAmbientL(d) * (open * 0.85f + 0.15f) * m.albedo;   // the cosine ray's estimate of irradiance / pi times albedo
}

// =====================================================================================================
// the sea: a plane at y = 0 with a sum of wind waves (2nd-order Stokes crests), filtered by the pixel footprint
// =====================================================================================================
static __forceinline__ __device__ float3 waveNormal(float x, float z, float foot, float& sigU) {
    float gx = 0.0f, gz = 0.0f, res = 0.0f;
    const float steep = 0.050f;
    for (int k = 0; k < 40; ++k) {
        const float h1 = hashF(k * 3u + 101u), h2 = hashF(k * 3u + 102u), h3 = hashF(k * 3u + 103u);
        const float wl = expf(lerpf(-1.05f, 2.64f, h1));                 // 0.35 .. 14 m
        const float ang = 1.5f * M_PIf + (h2 - 0.5f) * 1.1f;             // travelling towards -z (the camera's shore)
        const float kx = cosf(ang), kz = sinf(ang);
        const float kk = 2.0f * M_PIf / wl;
        const float A = steep * wl / (2.0f * M_PIf) * (0.6f + 0.6f * h3);
        const float om = sqrtf(9.81f * kk);
        const float ph = kk * (x * kx + z * kz) - om * params.time + 6.2831853f * hashF(k * 7u + 5u);
        const float fd = expf(-sqr(foot / (wl * 0.22f)));
        const float s = -A * kk * (sinf(ph) + 0.7f * sinf(2.0f * ph)) * fd;
        gx += s * kx;
        gz += s * kz;
        res += sqr(A * kk * fd) * 0.75f;
    }
    sigU = sqrtf(fmaxf(0.13f * 0.13f - res, 0.004f));
    return normalize(make_float3(-gx, 1.0f, -gz));
}
// Smith masking for Beckmann slopes (Walter et al. 2007): the waves hide each other at grazing angles.
static __forceinline__ __device__ float smithG1(float cosT, float sig) {
    const float c = clampf(cosT, 1e-4f, 1.0f);
    const float a = c / (sig * sqrtf(fmaxf(1.0f - c * c, 1e-8f)));
    return a >= 1.6f ? 1.0f : (3.535f * a + 2.181f * a * a) / (1.0f + 2.276f * a + 2.577f * a * a);
}
// Beckmann glint of a small light seen in the sea (unresolved wave slopes): radiance per unit irradiance.
static __forceinline__ __device__ float glint(float3 l, float3 v, float3 n, float sig, float F, float cosv) {
    const float3 h = normalize(l + v);
    const float ch = clampf(dot(h, n), 1e-3f, 1.0f);
    const float tan2 = (1.0f - ch * ch) / (ch * ch);
    const float D = expf(-tan2 / (sig * sig)) / (M_PIf * sig * sig * ch * ch * ch * ch);
    const float G = smithG1(cosv, sig) * smithG1(dot(l, n), sig);
    return D * G * F / (4.0f * cosv);
}

// Shade what a reflection ray from the sea sees (direct light only, no further bounces).
static __forceinline__ __device__ float3 shadeReflected(float3 o, float3 d, ShadeRng& rng) {
    const Hit h = trace(o, d, 2.0e4f, 1u, false);
    if (!h.hit) return skyL(o, d, false);
    const FMaterial m = params.materials[h.mat];
    float3 L = m.emission + directLight(h.p, h.ns, m, rng, true) + fogAmbientL(h.ns) * m.albedo * 0.5f;
    return applyFog(L, o, d, h.t);
}

// =====================================================================================================
// surface pass
// =====================================================================================================
extern "C" __global__ void __raygen__surface() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int i = idx.y * params.width + idx.x;
    const FCamera& cam = params.cam;
    const float px = idx.x + 0.5f + cam.jitter.x, py = idx.y + 0.5f + cam.jitter.y;
    const float3 d = cameraRay(cam, px, py);
    const float3 o = cam.pos;
    ShadeRng rng{idx.x, idx.y, 0u};

    // the sea plane bounds the search: below the horizon nothing is farther than the water
    const float tSea = d.y < -1e-5f ? (o.y / -d.y) : 1e30f;
    const Hit h = trace(o, d, fminf(tSea, 6.0e4f), 3u, true);
    float3 L;
    float depth;
    float3 nOut = make_float3(0.0f, 1.0f, 0.0f);
    float3 P;
    if (h.hit) {
        const FMaterial m = params.materials[h.mat];
        L = m.emission;
        if (m.kind == FK_DIFFUSE) L += directLight(h.p, h.ns, m, rng, true) + skyLight(h.p, h.ns, m, rng);
        else if (m.kind == FK_LAMP_GLASS) L = params.lampColor * (params.lampGlassBase + params.lampGlassK * lampIntensity(-d, false) / params.lampI);
        L = applyFog(L, o, d, h.t);
        depth = h.t;
        nOut = h.ns;
        P = h.p;
    } else if (tSea < 1e29f) {
        // the sea
        P = o + d * tSea;
        const float foot = tSea * (2.0f * cam.tanHalfY / params.height) / fmaxf(-d.y, 0.02f);
        float sigU;
        const float3 n = waveNormal(P.x, P.z, foot, sigU);
        const float3 v = -d;
        const float cosv = clampf(dot(n, v), 1e-3f, 1.0f);
        const float F = 0.02f + 0.98f * powf(1.0f - cosv, 5.0f);
        float3 r = reflect3(d, n);
        r.y = fabsf(r.y) + 1e-4f;
        const float3 refl = shadeReflected(P + make_float3(0.0f, 0.02f, 0.0f), normalize(r), rng);
        L = refl * F + params.waterDeep * (1.0f - F);
        // glints of the small lights: moon, lamp, window
        L += params.moonDisc * (glint(params.moonDir, v, n, sigU, F, cosv) * (1.0f - params.moonCosMax) * 2.0f * M_PIf * params.moonGlintK);
        {
            const float3 vl = params.lampPos - P;
            const float rl = length(vl);
            const float3 wl = vl / rl;
            const float E = lampIntensity(-wl, true) / (rl * rl) * expf(-fogTau(P.y, wl.y, rl)) * params.lampSurfK;
            L += params.lampColor * (E * glint(wl, v, n, sigU, F, cosv));
        }
        {
            const float3 c = params.winCorner + (params.winU + params.winV) * 0.5f;
            const float3 vw = c - P;
            const float rw = length(vw);
            const float3 ww = vw / rw;
            const float area = length(cross(params.winU, params.winV));
            const float E = fmaxf(-dot(ww, params.winN), 0.0f) * area / (rw * rw);
            L += params.winRadiance * (E * glint(ww, v, n, sigU, F, cosv) * expf(-fogTau(P.y, ww.y, rw)));
        }
        L = applyFog(L, o, d, tSea);
        depth = tSea;
        nOut = n;
    } else {
        L = skyL(o, d, true);
        depth = 1e30f;
        P = o + d * 1e6f;
    }
    params.surface[i] = make_float4(L.x, L.y, L.z, 1.0f);
    params.gdepth[i] = make_float4(depth, nOut.x, nOut.y, nOut.z);
    // motion: the same world point on this frame's and the previous frame's screen, both unjittered
    float2 prev, cur;
    const bool ok = depth < 1e29f ? (projectPoint(params.prevCam, P, prev) && projectPoint(cam, P, cur))
                                  : (projectDir(params.prevCam, d, prev) && projectDir(cam, d, cur));
    params.motion[i] = ok ? make_float2(cur.x - prev.x, cur.y - prev.y) : make_float2(0.0f, 0.0f);
}

// =====================================================================================================
// volume pass (full resolution, or half: volScale 2)
// =====================================================================================================
extern "C" __global__ void __raygen__volume() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int vi = idx.y * params.volWidth + idx.x;
    const FCamera& cam = params.cam;
    const unsigned int S = params.volScale;
    const unsigned int fx = min(S * idx.x, params.width - S), fy = min(S * idx.y, params.height - S);
    float tEnd = 0.0f;
    for (unsigned int k = 0; k < S * S; ++k) tEnd = fmaxf(tEnd, params.gdepth[(fy + k / S) * params.width + fx + k % S].x);
    tEnd = fminf(tEnd, 3.0e4f);
    const float3 o = cam.pos;
    const float3 d = cameraRay(cam, fx + 0.5f * S + cam.jitter.x, fy + 0.5f * S + cam.jitter.y);
    float3 L = make_float3(0.0f, 0.0f, 0.0f);
    float wsum = 0.0f, dsum = 0.0f;

    // lamp: equi-angular sampling around the closest approach of the ray to the lamp (Kulla & Fajardo 2012)
    {
        const float3 lp = params.lampPos;
        const float delta = dot(lp - o, d);
        const float D = fmaxf(length(lp - o - d * delta), 0.05f);
        const float ta = atan2f(-delta, D), tb = atan2f(tEnd - delta, D);
        const int N = params.volLampSamples;
        for (int s = 0; s < N; ++s) {
            const float u = (s + blue(idx.x, idx.y, 40u + s)) / N;
            const float th = lerpf(ta, tb, u);
            const float t = delta + D * tanf(th);
            const float pdf = D / ((tb - ta) * (D * D + sqr(t - delta)));
            const float3 P = o + d * t;
            const float3 v = P - lp;
            const float r = fmaxf(length(v), 0.05f);
            const float3 w = v / r;
            const float I = lampIntensity(w, true);
            if (I <= 0.0f) continue;
            const float c = dot(w, -d);
            const float ph = (1.0f - params.beamPhaseHalo) * henyeyGreenstein(c, params.beamPhaseG) + params.beamPhaseHalo * henyeyGreenstein(c, 0.8f);
            const float T = expf(-fogTau(o.y, d.y, t) - fogTau(lp.y, w.y, r));
            float contrib = fogSigma(P.y) * T * I / (r * r) * ph / pdf;
            if (contrib <= 0.0f) continue;
            if (!visible(P, -w, r - 0.6f)) continue;
            contrib /= N;
            L += params.lampColor * (contrib * params.volBeamScale);
            wsum += contrib;
            dsum += contrib * t;
        }
        // art: soft ceiling on the in-scattered beam (INSIDE clamps its fog likewise)
        const float y = lum(L);
        if (y > 0.0f) L = L * (1.0f / (1.0f + y / params.volBeamCap));
    }
    // moon: distances drawn proportional to sigma * transmittance (inverse of the optical depth, Newton)
    {
        const float tauEnd = fogTau(o.y, d.y, tEnd);
        const float scat = 1.0f - expf(-tauEnd);
        const int N = params.volMoonSamples;
        const float c = dot(d, params.moonDir);
        const float ph = henyeyGreenstein(c, 0.7f) * 0.6f + 0.4f / (4.0f * M_PIf);
        for (int s = 0; s < N && scat > 1e-5f; ++s) {
            const float u = (s + blue(idx.x, idx.y, 80u + s)) / N;
            const float tauT = -logf(1.0f - u * scat);
            float t = clampf(tauT / fmaxf(fogSigma(o.y), 1e-7f), 0.0f, tEnd);
            for (int it = 0; it < 8; ++it) {
                const float f = fogTau(o.y, d.y, t) - tauT;
                t = clampf(t - f / fmaxf(fogSigma(o.y + d.y * t), 1e-7f), 0.0f, tEnd);
            }
            const float3 P = o + d * t;
            const float Tm = expf(-fogTau(P.y, params.moonDir.y, 1e6f));
            if (!visible(P, params.moonDir, 1e5f)) continue;
            const float3 add = params.moonE * (scat * ph * Tm * params.volMoonScale / N);
            L += add;
            const float y = lum(add);
            wsum += y;
            dsum += y * t;
        }
    }
    params.volume[vi] = make_float4(L.x, L.y, L.z, wsum > 0.0f ? dsum / wsum : tEnd);
}

// =====================================================================================================
// temporal resolve of the volume
// =====================================================================================================
static __forceinline__ __device__ float4 fetchClamp(const float4* img, int w, int h, int x, int y) {
    x = max(0, min(w - 1, x));
    y = max(0, min(h - 1, y));
    return img[y * w + x];
}
static __forceinline__ __device__ float4 bilinear(const float4* img, int w, int h, float x, float y) {   // pixel units, centres at +0.5
    x -= 0.5f; y -= 0.5f;
    const int x0 = int(floorf(x)), y0 = int(floorf(y));
    const float tx = x - x0, ty = y - y0;
    return (fetchClamp(img, w, h, x0, y0) * (1.0f - tx) + fetchClamp(img, w, h, x0 + 1, y0) * tx) * (1.0f - ty) +
           (fetchClamp(img, w, h, x0, y0 + 1) * (1.0f - tx) + fetchClamp(img, w, h, x0 + 1, y0 + 1) * tx) * ty;
}

extern "C" __global__ void __raygen__volresolve() {
    const uint3 idx = optixGetLaunchIndex();
    const int w = int(params.volWidth), h = int(params.volHeight);
    const unsigned int vi = idx.y * params.volWidth + idx.x;
    const float4 cur = params.volume[vi];
    float4 mn = cur, mx = cur;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const float4 s = fetchClamp(params.volume, w, h, int(idx.x) + dx, int(idx.y) + dy);
            mn = make_float4(fminf(mn.x, s.x), fminf(mn.y, s.y), fminf(mn.z, s.z), 0.0f);
            mx = make_float4(fmaxf(mx.x, s.x), fmaxf(mx.y, s.y), fmaxf(mx.z, s.z), 0.0f);
        }
    float4 out = cur;
    if (params.frame > 0) {
        // reproject by the mean scattering distance
        const FCamera& cam = params.cam;
        const float S = float(params.volScale);
        const float fxp = S * (idx.x + 0.5f), fyp = S * (idx.y + 0.5f);
        const float3 d = cameraRay(cam, fxp, fyp);
        float2 prev;
        const bool ok = projectPoint(params.prevCam, cam.pos + d * fmaxf(cur.w, 1.0f), prev);
        if (ok && prev.x >= 0 && prev.y >= 0 && prev.x < params.width && prev.y < params.height) {
            float4 hist = bilinear(params.volumeHist[params.histRead], w, h, prev.x / S, prev.y / S);
            if (params.taaAlpha > 0.0f) {   // live: clamp; still (progressive) mode: plain average
                hist.x = clampf(hist.x, mn.x, mx.x);
                hist.y = clampf(hist.y, mn.y, mx.y);
                hist.z = clampf(hist.z, mn.z, mx.z);
            }
            const float a = fmaxf(params.taaAlpha * 1.6f, 1.0f / (params.frame + 1.0f));
            out = hist * (1.0f - a) + cur * a;
            out.w = cur.w;
        }
    }
    params.volumeHist[params.histRead ^ 1][vi] = out;
}

// =====================================================================================================
// TAA (full res): surface + depth-aware upsampled volume, YCoCg neighbourhood clamp
// =====================================================================================================
static __forceinline__ __device__ float3 toYCoCg(float3 c) {
    return make_float3(0.25f * c.x + 0.5f * c.y + 0.25f * c.z, 0.5f * c.x - 0.5f * c.z, -0.25f * c.x + 0.5f * c.y - 0.25f * c.z);
}
static __forceinline__ __device__ float3 fromYCoCg(float3 c) {
    return make_float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}
static __forceinline__ __device__ float3 volumeAt(int x, int y) {
    const float4* V = params.volumeHist[params.histRead ^ 1];
    const int w = int(params.volWidth), h = int(params.volHeight);
    if (params.volScale == 1) return f3(V[y * w + x]);
    // half res: the volume pixel whose depth matches this pixel best among the four nearest (joint bilateral)
    const float z = params.gdepth[y * params.width + x].x;
    const float fx = (x + 0.5f) * 0.5f - 0.5f, fy = (y + 0.5f) * 0.5f - 0.5f;
    const int x0 = int(floorf(fx)), y0 = int(floorf(fy));
    const float tx = fx - x0, ty = fy - y0;
    float3 acc = make_float3(0.0f, 0.0f, 0.0f);
    float ws = 0.0f;
    for (int k = 0; k < 4; ++k) {
        const int xx = max(0, min(w - 1, x0 + (k & 1))), yy = max(0, min(h - 1, y0 + (k >> 1)));
        const float4 v = V[yy * w + xx];
        // depth of that half-res pixel's footprint: the farthest of its 2x2 (as the volume pass used)
        float vz = 0.0f;
        for (int j = 0; j < 4; ++j)
            vz = fmaxf(vz, params.gdepth[min(int(params.height) - 1, 2 * yy + (j >> 1)) * params.width + min(int(params.width) - 1, 2 * xx + (j & 1))].x);
        vz = fminf(vz, 3.0e4f);
        const float zz = fminf(z, 3.0e4f);
        const float bw = ((k & 1) ? tx : 1.0f - tx) * ((k >> 1) ? ty : 1.0f - ty);
        const float dw = 1.0f / (1e-3f + fabsf(vz - zz) / fmaxf(zz, 1.0f));
        acc += f3(v) * (bw * dw);
        ws += bw * dw;
    }
    return ws > 0.0f ? acc / ws : make_float3(0.0f, 0.0f, 0.0f);
}

// surface + volume into one image (in place), once per pixel
extern "C" __global__ void __raygen__compose() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int i = idx.y * params.width + idx.x;
    float3 c = f3(params.surface[i]);
    const float3 v = volumeAt(int(idx.x), int(idx.y));
    if (params.view == 2) c = v;
    else if (params.view != 1) c += v;
    params.surface[i] = make_float4(c.x, c.y, c.z, 1.0f);
}

extern "C" __global__ void __raygen__taa() {
    const uint3 idx = optixGetLaunchIndex();
    const int W = int(params.width), H = int(params.height);
    const int x = int(idx.x), y = int(idx.y);
    const unsigned int i = idx.y * params.width + idx.x;
    const float3 c = f3(params.surface[i]);
    // neighbourhood statistics of the current frame (YCoCg mean and deviation)
    float3 m1 = make_float3(0.0f, 0.0f, 0.0f), m2 = make_float3(0.0f, 0.0f, 0.0f);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int xx = max(0, min(W - 1, x + dx)), yy = max(0, min(H - 1, y + dy));
            const float3 q = toYCoCg(f3(params.surface[yy * W + xx]));
            m1 += q;
            m2 += q * q;
        }
    m1 = m1 / 9.0f;
    m2 = m2 / 9.0f;
    const float3 sd = make_float3(sqrtf(fmaxf(m2.x - m1.x * m1.x, 0.0f)), sqrtf(fmaxf(m2.y - m1.y * m1.y, 0.0f)), sqrtf(fmaxf(m2.z - m1.z * m1.z, 0.0f)));
    float3 out = c;
    if (params.frame > 0) {
        const float2 mv = params.motion[i];
        const float hx = x + 0.5f - mv.x, hy = y + 0.5f - mv.y;
        if (hx >= 0 && hy >= 0 && hx < W && hy < H) {
            float3 hist = toYCoCg(f3(bilinear(params.taaHist[params.histRead], W, H, hx, hy)));
            const float g = params.taaAlpha > 0.0f ? 1.25f : 1e6f;   // progressive (still) mode: no clamp
            hist = make_float3(clampf(hist.x, m1.x - g * sd.x, m1.x + g * sd.x), clampf(hist.y, m1.y - g * sd.y, m1.y + g * sd.y),
                               clampf(hist.z, m1.z - g * sd.z, m1.z + g * sd.z));
            const float a = fmaxf(params.taaAlpha, 1.0f / (params.frame + 1.0f));
            out = fromYCoCg(hist) * (1.0f - a) + c * a;
        }
    }
    out = fmaxf3(out, make_float3(0.0f, 0.0f, 0.0f));
    params.taaHist[params.histRead ^ 1][i] = make_float4(out.x, out.y, out.z, 1.0f);
    params.hdr[i] = make_float4(out.x, out.y, out.z, 1.0f);
    // the narrow glow's source: only what is really bright (the lamp, the window, the moon)
    const float Y = lum(out) * params.exposure;
    const float k = smoothstepf(params.glowThreshold, params.glowThreshold * 3.0f, Y);
    params.emissive[i] = make_float4(out.x * k, out.y * k, out.z * k, 1.0f);
}

// =====================================================================================================
// final: glows, glare star, vignette, filmic curve, grade, dither
// =====================================================================================================
static __forceinline__ __device__ float3 acesFit(float3 x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return make_float3(saturate((x.x * (a * x.x + b)) / (x.x * (c * x.x + d) + e)), saturate((x.y * (a * x.y + b)) / (x.y * (c * x.y + d) + e)),
                       saturate((x.z * (a * x.z + b)) / (x.z * (c * x.z + d) + e)));
}
static __forceinline__ __device__ float srgbEncode(float x) {
    x = saturate(x);
    return x <= 0.0031308f ? 12.92f * x : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}

extern "C" __global__ void __raygen__final() {
    const uint3 idx = optixGetLaunchIndex();
    const int W = int(params.width), H = int(params.height);
    const unsigned int i = idx.y * params.width + idx.x;
    float3 c;
    if (params.view == 3) {
        const float z = params.gdepth[i].x;
        const float v = z > 1e29f ? 1.0f : saturate(logf(1.0f + z) / logf(1.0f + 3.0e4f));
        c = make_float3(v, v, v);
    } else if (params.view == 4) {
        const float4 g = params.gdepth[i];
        c = make_float3(g.y * 0.5f + 0.5f, g.z * 0.5f + 0.5f, g.w * 0.5f + 0.5f);
    } else {
        float3 hdr = f3(params.hdr[i]);
        const float hx = (idx.x + 0.5f) * 0.5f, hy = (idx.y + 0.5f) * 0.5f;
        if (params.glowWide) hdr = hdr * (1.0f - params.glowWideK) + f3(bilinear(params.glowWide, W / 2, H / 2, hx, hy)) * params.glowWideK;
        if (params.glowNarrow) hdr += f3(bilinear(params.glowNarrow, W / 2, H / 2, hx, hy)) * params.glowNarrowK;
        const float u = (idx.x + 0.5f) / W - 0.5f, v = (idx.y + 0.5f) / H - 0.5f;
        const float r2 = (u * u) * 4.0f + (v * v) * 4.0f;
        hdr = hdr * (1.0f - params.vignette * smoothstepf(0.3f, 2.0f, r2));
        c = acesFit(hdr * params.exposure);
        c = c + make_float3(0.004f, 0.006f, 0.009f) * (make_float3(1.0f, 1.0f, 1.0f) - c);   // cool lift in the blacks
    }
    // sRGB + TPDF blue-noise dither (Playdead: dither every 8-bit write)
    const float n = blue(idx.x, idx.y, 7u) + blue(idx.x, idx.y, 9u) - 1.0f;
    const float r = fminf(fmaxf(srgbEncode(c.x) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    const float g = fminf(fmaxf(srgbEncode(c.y) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    const float b = fminf(fmaxf(srgbEncode(c.z) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    params.output[i] = make_uchar4((unsigned char)r, (unsigned char)g, (unsigned char)b, 255);
}
