// OptiX device programs, compiled at run time with NVRTC (F5 recompiles them).
//   __raygen__sky      spectral sky map + sun table for the current sun position
//   __raygen__render   spectral path tracer (hero wavelengths), one or more samples per pixel
//   __raygen__tonemap  XYZ -> sRGB, AgX, blue-noise dither, debug views
//   __miss__shadow / __anyhit__shadow   visibility with soap-film transmittance
#include "common.h"
#include "noise.h"
#include "sky.h"
#include "bsdf.h"
#include "specular.h"
#include "scenes/altinsaat/photon_grid.h"
#include "shared/display.h"

enum { RAY_RADIANCE = 0, RAY_SHADOW = 1, RAY_TYPES = 2 };
enum { INST_TRIANGLES = 0, INST_SPHERES = 1 };

static __forceinline__ __device__ unsigned int f2u(float f) { return __float_as_uint(f); }
static __forceinline__ __device__ float u2f(unsigned int u) { return __uint_as_float(u); }

// =====================================================================================================
// Sky precomputation
// =====================================================================================================
static __forceinline__ __device__ float rayleighPhase(float c) { return 3.0f / (16.0f * M_PIf) * (1.0f + c * c); }
static __forceinline__ __device__ float miePhase(float c) {     // Cornette-Shanks
    const float g = 0.76f, g2 = g * g;
    return 3.0f / (8.0f * M_PIf) * (1.0f - g2) * (1.0f + c * c) / ((2.0f + g2) * powf(1.0f + g2 - 2.0f * g * c, 1.5f));
}

extern "C" __global__ void __raygen__sky() {
    const uint3 idx = optixGetLaunchIndex();
    float* sky = const_cast<float*>(params.sky);
    const float3 sunD = params.sunDir;
    const float omegaSun = 2.0f * M_PIf * params.sunOneMinusCosMax;
    const float3 o = make_float3(0.0f, ATM_RG + ATM_OBSERVER, 0.0f);

    // the sun table (radiance at the centre of the disk, after the atmosphere) rides along on row 0
    if (idx.y == 0 && idx.x < SUN_TABLE_N) {
        const float l = LAMBDA_MIN + idx.x;
        const float3 depth = opticalDepthToSpace(o, sunD, 512);
        const float T = raySphereNear(o, sunD, ATM_RG) > 0.0f ? 0.0f : transmittanceFromDepth(depth, l);
        const_cast<float*>(params.sunTable)[idx.x] = blackbody(l, 5778.0f) / (1.0f - params.sunLimb / 3.0f) * T;
    }

    const float3 d = skyDir((idx.x + 0.5f) / params.skyW, (idx.y + 0.5f) / params.skyH);
    const float tTop = raySphereFar(o, d, ATM_RT);
    const float tGround = raySphereNear(o, d, ATM_RG);
    const bool ground = tGround > 0.0f;
    const float tEnd = ground ? tGround : tTop;
    const float mu = dot(d, sunD);
    const float pR = rayleighPhase(mu), pM = miePhase(mu);

    float L[SKY_BINS];
    for (int b = 0; b < SKY_BINS; ++b) L[b] = 0.0f;
    float3 depthView = make_float3(0.0f, 0.0f, 0.0f);
    const int N = 96;
    float tPrev = 0.0f;
    for (int i = 0; i < N; ++i) {
        const float s = (i + 1.0f) / N;
        const float t = tEnd * s * s;
        const float dt = t - tPrev;
        const float tm = 0.5f * (t + tPrev);
        tPrev = t;
        const float3 x = o + d * tm;
        const float h = length(x) - ATM_RG;
        const float3 dens = make_float3(expf(-h / ATM_HR), expf(-h / ATM_HM), ozoneDensity(h));
        const float3 depthMid = depthView + dens * (0.5f * dt);
        depthView += dens * dt;
        if (raySphereNear(x, sunD, ATM_RG) > 0.0f) continue;      // the sun is below this point's horizon
        const float3 depthSun = opticalDepthToSpace(x, sunD, 24);
        const float3 depthTot = depthMid + depthSun;
        for (int b = 0; b < SKY_BINS; ++b) {
            const float l = LAMBDA_MIN + b * SKY_BIN_NM;
            const float T = transmittanceFromDepth(depthTot, l);
            L[b] += T * (rayleighScattering(l) * dens.x * pR + mieScattering(l) * dens.y * pM) * dt;
        }
    }
    float* out = sky + (size_t(idx.y) * params.skyW + idx.x) * SKY_BINS;
    float3 groundDepthSun = make_float3(0, 0, 0);
    float groundCos = 0.0f;
    if (ground) {
        const float3 g = o + d * tGround;
        groundCos = fmaxf(0.0f, dot(normalize(g), sunD));
        groundDepthSun = opticalDepthToSpace(g, sunD, 64);
    }
    for (int b = 0; b < SKY_BINS; ++b) {
        const float l = LAMBDA_MIN + b * SKY_BIN_NM;
        const float E0 = blackbody(l, 5778.0f) * omegaSun;
        float v = L[b] * E0;
        if (ground) {
            const float Tsun = transmittanceFromDepth(groundDepthSun, l);
            const float Tview = transmittanceFromDepth(depthView, l);
            v += params.groundAlbedo * INV_PIf * E0 * Tsun * (groundCos + 0.12f) * Tview;
        }
        out[b] = v;
    }
}

// Bakes the tileable 2D noise texture: gnoise2, its gradient, and a second independent noise.
extern "C" __global__ void __raygen__bakenoise() {
    const uint3 idx = optixGetLaunchIndex();
    const float2 uv = make_float2((idx.x + 0.5f) * NOISE_PERIOD / NOISE_TEX, (idx.y + 0.5f) * NOISE_PERIOD / NOISE_TEX);
    const float h = 0.002f;
    const float a = gnoise2(uv, 0x51EDu);
    const float gx = (gnoise2(make_float2(uv.x + h, uv.y), 0x51EDu) - gnoise2(make_float2(uv.x - h, uv.y), 0x51EDu)) / (2.0f * h);
    const float gy = (gnoise2(make_float2(uv.x, uv.y + h), 0x51EDu) - gnoise2(make_float2(uv.x, uv.y - h), 0x51EDu)) / (2.0f * h);
    params.noiseBake[idx.y * NOISE_TEX + idx.x] = make_float4(a, gx, gy, gnoise2(uv, 0xBEEFu));
}

// =====================================================================================================
// Ray casting
// =====================================================================================================
struct Hit {
    bool hit;
    float t;
    int inst, prim, mat;
    float3 p, ngOut, nsOut;   // outward geometric / shading normals
};

static __forceinline__ __device__ void fetchTriangle(int prim, float2 bary, float3& p, float3& ng, float3& ns) {
    const uint3 tri = params.indices[prim];
    const float3 v0 = params.vertices[tri.x], v1 = params.vertices[tri.y], v2 = params.vertices[tri.z];
    const float b1 = bary.x, b2 = bary.y, b0 = 1.0f - b1 - b2;
    p = v0 * b0 + v1 * b1 + v2 * b2;
    ng = normalize(cross(v1 - v0, v2 - v0));
    ns = normalize(params.normals[tri.x] * b0 + params.normals[tri.y] * b1 + params.normals[tri.z] * b2);
    if (dot(ns, ng) < 0.0f) ns = ng;
}

// Visibility masks: 1 everything that shapes the light, 2 dust (only camera rays see it).
static __forceinline__ __device__ Hit traceRadiance(float3 o, float3 d, float tmax, bool reorder = true, unsigned int mask = 1u) {
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(mask), OPTIX_RAY_FLAG_DISABLE_ANYHIT,
                  RAY_RADIANCE, RAY_TYPES, RAY_RADIANCE);
    Hit h;
    h.hit = optixHitObjectIsHit();
    h.mat = -1;
    if (!h.hit) {
        if (reorder && !(params.debugFlags & 8)) optixReorder(15u, 4);
        return h;
    }
    h.t = optixHitObjectGetRayTmax();
    h.inst = optixHitObjectGetInstanceId();
    h.prim = optixHitObjectGetPrimitiveIndex();
    h.mat = h.inst == INST_TRIANGLES ? params.triMaterial[h.prim] : params.sphereMaterial[h.prim];
    if (reorder && !(params.debugFlags & 8)) optixReorder((unsigned int)params.materials[h.mat].type & 15u, 4);
    if (h.inst == INST_TRIANGLES) {
        fetchTriangle(h.prim, optixHitObjectGetTriangleBarycentrics(), h.p, h.ngOut, h.nsOut);
    } else {
        const float4 s = params.spheres[h.prim];
        const float3 c = make_float3(s.x, s.y, s.z);
        const float3 n = normalize(o + d * h.t - c);
        h.p = c + n * s.w;
        h.ngOut = h.nsOut = n;
    }
    return h;
}

static __forceinline__ __device__ bool hazeActive() {
    return params.enableHaze && !params.furnace && params.hazeSigmaS + params.hazeSigmaA > 0.0f;
}

// Length of the ray segment [0, tmax] inside the haze box.
static __forceinline__ __device__ bool hazeSegment(float3 o, float3 d, float tmax, float& t0, float& t1) {
    const float3 inv = make_float3(1.0f / d.x, 1.0f / d.y, 1.0f / d.z);
    const float3 a = (params.hazeMin - o) * inv, b = (params.hazeMax - o) * inv;
    const float3 lo = fminf3(a, b), hi = fmaxf3(a, b);
    t0 = fmaxf(0.0f, fmaxf(lo.x, fmaxf(lo.y, lo.z)));
    t1 = fminf(tmax, fminf(hi.x, fminf(hi.y, hi.z)));
    return t1 > t0;
}

// Visibility towards a light with soap-film transmittance; o must already be offset.
static __forceinline__ __device__ float4 traceShadow(float3 o, float3 d, float tmax, float4 lambda, int medium) {
    if (medium != IOR_AIR) return f4(0.0f);      // light sources are outside; delta interfaces block shadow rays
    unsigned int vis = 0u;
    unsigned int t0 = f2u(1.0f), t1 = f2u(1.0f), t2 = f2u(1.0f), t3 = f2u(1.0f);
    unsigned int l0 = f2u(lambda.x), l1 = f2u(lambda.y), l2 = f2u(lambda.z), l3 = f2u(lambda.w);
    optixTrace(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(1u),
               OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT | OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT, RAY_SHADOW, RAY_TYPES, RAY_SHADOW,
               vis, t0, t1, t2, t3, l0, l1, l2, l3);
    if (!vis) return f4(0.0f);
    float4 T = make_float4(u2f(t0), u2f(t1), u2f(t2), u2f(t3));
    if (hazeActive()) {
        float a, b;
        if (hazeSegment(o, d, tmax, a, b)) T *= expf(-(params.hazeSigmaS + params.hazeSigmaA) * (b - a));
    }
    return T;
}

extern "C" __global__ void __miss__shadow() { optixSetPayload_0(1u); }

// Spheres are custom primitives: OptiX's built-in sphere only reports hits from outside, but glass shells and
// soap bubbles are entered and left. Both roots are reported (closest wins for radiance rays, any-hit sees both).
extern "C" __global__ void __intersection__sphere() {
    const int prim = optixGetPrimitiveIndex();
    const float4 s = params.spheres[prim];
    const float3 o = optixGetObjectRayOrigin(), d = optixGetObjectRayDirection();
    const float3 oc = o - make_float3(s.x, s.y, s.z);
    const float a = dot(d, d), b = dot(oc, d), c = dot(oc, oc) - s.w * s.w;
    const float disc = b * b - a * c;
    if (disc < 0.0f) return;
    const float sq = sqrtf(disc);
    // numerically stable roots
    const float q = -(b + copysignf(sq, b));
    float t0 = q / a, t1 = c / q;
    if (t0 > t1) { const float t = t0; t0 = t1; t1 = t; }
    const float tmin = optixGetRayTmin(), tmax = optixGetRayTmax();
    if (t0 > tmin && t0 < tmax) optixReportIntersection(t0, 0u);
    if (t1 > tmin && t1 < tmax) optixReportIntersection(t1, 1u);
}

// Photon beams: a camera ray "hits" a beam where the two lines pass within the blur radius. Reported at the
// camera ray's closest point, with the line distance and the position along the beam as attributes.
extern "C" __global__ void __intersection__beam() {
    const unsigned int prim = optixGetPrimitiveIndex();
    const PhotonBeam bm = params.beams[prim / BEAM_PIECES];
    const float piece = float(prim % BEAM_PIECES);
    const float3 o = optixGetWorldRayOrigin(), u = optixGetWorldRayDirection();
    const float3 ab = bm.b - bm.a;
    const float len = length(ab);
    if (len <= 1e-6f) return;
    const float3 v = ab * (1.0f / len);
    const float3 w0 = o - bm.a;
    const float uv = dot(u, v), uw = dot(u, w0), vw = dot(v, w0);
    const float den = 1.0f - uv * uv;
    if (den < 1e-6f) return;                           // parallel: no well-defined crossing
    const float sc = (uv * vw - uw) / den;             // along the camera ray
    const float tb = (vw - uv * uw) / den;             // along the beam
    const float seg = len * (1.0f / BEAM_PIECES);
    if (tb < piece * seg || tb >= (piece + 1.0f) * seg || sc <= optixGetRayTmin() || sc >= optixGetRayTmax()) return;
    const float3 dv = (o + u * sc) - (bm.a + v * tb);
    const float dist2 = dot(dv, dv), r = params.beamRadius;
    if (dist2 >= r * r) return;
    optixReportIntersection(sc, 0u, __float_as_uint(dist2), __float_as_uint(tb));
}

extern "C" __global__ void __anyhit__beam() {
    const PhotonBeam bm = params.beams[optixGetPrimitiveIndex() / BEAM_PIECES];
    const float dist2 = __uint_as_float(optixGetAttribute_0()), tb = __uint_as_float(optixGetAttribute_1());
    const float3 u = optixGetWorldRayDirection();
    const float3 v = normalize(bm.b - bm.a);
    const float uv = dot(u, v);
    const float sinT = sqrtf(fmaxf(1e-6f, 1.0f - uv * uv));
    const float r = params.beamRadius;
    const float kernel = 2.0f * sqrtf(fmaxf(0.0f, r * r - dist2)) / (M_PIf * r * r);   // cylinder cross-section
    const float sigT = params.hazeSigmaS + params.hazeSigmaA;
    const float sc = optixGetRayTmax();
    const float val = params.hazeSigmaS * henyeyGreenstein(-uv, params.hazeG) * bm.flux * expf(-sigT * (tb + sc)) * kernel / sinT;
    const float3 c = cieXYZ(bm.lambda) * val;
    optixSetPayload_0(__float_as_uint(__uint_as_float(optixGetPayload_0()) + c.x));
    optixSetPayload_1(__float_as_uint(__uint_as_float(optixGetPayload_1()) + c.y));
    optixSetPayload_2(__float_as_uint(__uint_as_float(optixGetPayload_2()) + c.z));
    optixIgnoreIntersection();
}

// In-scattered caustic light along a camera ray segment (XYZ, before the path throughput).
static __forceinline__ __device__ float3 gatherBeams(float3 o, float3 d, float tmax) {
    unsigned int x = 0u, y = 0u, z = 0u;
    optixTrace(params.beamHandle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(255), OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
               4, 0, 0, x, y, z);
    return make_float3(__uint_as_float(x), __uint_as_float(y), __uint_as_float(z)) * (1.0f / CIE_Y_INTEGRAL);
}

extern "C" __global__ void __anyhit__shadow() {
    // only spheres have an any-hit program: soap films transmit, everything else blocks
    const int prim = optixGetPrimitiveIndex();
    const int mi = params.sphereMaterial[prim];
    const Material& m = params.materials[mi];
    // accept (blocks) unless it is a soap film; the furnace test turns films into opaque white spheres for every ray
    if (m.type != MAT_FILM || params.furnace) return;
    const float4 s = params.spheres[prim];
    const float3 c = make_float3(s.x, s.y, s.z);
    const float3 d = optixGetWorldRayDirection();
    const float3 p = optixGetWorldRayOrigin() + d * optixGetRayTmax();
    const float3 local = (p - c) * (1.0f / s.w);
    const float cosI = fabsf(dot(d, normalize(local)));
    const float th = filmThickness(local, m.p, params.time, float(prim) * 3.17f);
    const float l[4] = {u2f(optixGetPayload_5()), u2f(optixGetPayload_6()), u2f(optixGetPayload_7()), u2f(optixGetPayload_8())};
    optixSetPayload_1(f2u(u2f(optixGetPayload_1()) * (1.0f - thinFilmReflectance(l[0], th, iorOf(IOR_WATER, l[0]), cosI))));
    optixSetPayload_2(f2u(u2f(optixGetPayload_2()) * (1.0f - thinFilmReflectance(l[1], th, iorOf(IOR_WATER, l[1]), cosI))));
    optixSetPayload_3(f2u(u2f(optixGetPayload_3()) * (1.0f - thinFilmReflectance(l[2], th, iorOf(IOR_WATER, l[2]), cosI))));
    optixSetPayload_4(f2u(u2f(optixGetPayload_4()) * (1.0f - thinFilmReflectance(l[3], th, iorOf(IOR_WATER, l[3]), cosI))));
    optixIgnoreIntersection();
}

// =====================================================================================================
// Light sampling
// =====================================================================================================
// Sky light enters through portals (the openings). A portal is chosen with probability proportional to
// a rough solid-angle estimate seen from p, then a uniform point on it.
static __forceinline__ __device__ float portalWeights(float3 p, float* w) {
    float sum = 0.0f;
    for (int i = 0; i < params.numPortals; ++i) {
        const Portal& P = params.portals[i];
        const float3 v = P.corner + (P.edgeU + P.edgeV) * 0.5f - p;
        const float d2 = dot(v, v);
        const float cosL = -dot(v, P.normal) * rsqrtf(fmaxf(d2, 1e-12f));
        w[i] = cosL > 0.0f ? P.area * fmaxf(cosL, 0.1f) / fmaxf(d2, 0.05f) : 0.0f;
        sum += w[i];
    }
    return sum;
}
// Solid-angle pdf of that strategy producing direction d from p.
static __forceinline__ __device__ float portalPdf(float3 p, float3 d) {
    float w[MAX_PORTALS];
    const float sum = portalWeights(p, w);
    if (sum <= 0.0f) return 0.0f;
    float pdf = 0.0f;
    for (int i = 0; i < params.numPortals; ++i) {
        if (w[i] <= 0.0f) continue;
        const Portal& P = params.portals[i];
        const float dn = dot(d, P.normal);
        if (dn >= 0.0f) continue;                  // must leave the room through the portal
        const float t = dot(P.corner - p, P.normal) / dn;
        if (t <= 0.0f) continue;
        const float3 q = p + d * t - P.corner;
        const float a = dot(q, P.edgeU) / dot(P.edgeU, P.edgeU), b = dot(q, P.edgeV) / dot(P.edgeV, P.edgeV);
        if (a < 0.0f || a > 1.0f || b < 0.0f || b > 1.0f) continue;
        pdf += (w[i] / sum) * t * t / (-dn * P.area);
    }
    return pdf;
}

// ---- sun patches: sunlit surfaces as secondary light sources
// The sun enters only through the openings. Tracing the sun's central ray inwards from a uniform point on
// an opening's outer face lands on a sunlit point y with area density cos(theta_y) * crossings / A_proj
// (A_proj: the openings' total area seen from the sun). Connecting a shading point x to y samples the light
// bounced by the bright patches directly. The path x -> y -> sun is also made by BSDF sampling at x followed
// by sun sampling (or BSDF sampling) at y; all three are combined with the power heuristic.
static __forceinline__ __device__ int sunPortalCrossings(float3 p) {
    int n = 0;
    for (int i = 0; i < params.numSunPortals; ++i) {
        const Portal& P = params.sunPortals[i];
        const float dn = dot(params.sunDir, P.normal);
        if (dn >= 0.0f) continue;
        const float t = dot(P.corner - p, P.normal) / dn;
        if (t <= 0.0f) continue;
        const float3 q = p + params.sunDir * t - P.corner;
        const float a = dot(q, P.edgeU) / dot(P.edgeU, P.edgeU), b = dot(q, P.edgeV) / dot(P.edgeV, P.edgeV);
        if (a >= 0.0f && a <= 1.0f && b >= 0.0f && b <= 1.0f) ++n;
    }
    return n;
}
// Area density with which the sun-patch strategy produces the point p (exact: casts the central ray).
static __forceinline__ __device__ float sunPatchPdf(float3 p, float3 ng, float4 lambda) {
    if (!params.enableSunPatch || params.sunPortalAreaTotal <= 0.0f) return 0.0f;
    const float c = dot(ng, params.sunDir);
    if (c <= 0.0f) return 0.0f;
    const int n = sunPortalCrossings(p);
    if (n == 0) return 0.0f;
    if (!(params.debugFlags & 2) && hmax(traceShadow(offsetRay(p, ng), params.sunDir, 1e16f, lambda, IOR_AIR)) <= 0.0f) return 0.0f;
    return c * n / params.sunPortalAreaTotal;
}
// Three-way power heuristic for a path x -> y -> sun: a = patch (p3 * pc), b = BSDF at x then sun sampling
// at y (pIn * pc), c = BSDF at x then BSDF at y (pIn * pBy). Returns the weight of the chosen term.
static __forceinline__ __device__ float weight3(float chosen, float a, float b, float c) {
    const float d = a * a + b * b + c * c;
    return d > 0.0f ? chosen * chosen / d : 0.0f;
}

// Direct sun at s. pIn: area density of reaching s by BSDF sampling from a vertex that also did the patch
// strategy (0 if not), needed for the three-way weight.
static __forceinline__ __device__ float4 neeSun(const Surface& s, float3 wo, const Wavelengths& wl, Rng& rng, int medium, float pIn) {
    const float3 local = sampleCone(rng.uniform2(), params.sunOneMinusCosMax);
    const float3 ld = Frame::from(params.sunDir).toWorld(local);
    if (dot(ld, s.ng) <= 0.0f) return f4(0.0f);
    float bpdf;
    const float4 f = bsdfEval(s, wo, ld, bpdf);
    if (hmax(f) <= 0.0f) return f4(0.0f);
    const float4 Le = sunRadiance(ld, wl.lambda);
    const float4 T = traceShadow(offsetRay(s.p, s.ng), ld, 1e16f, wl.lambda, medium);
    if (hmax(T) <= 0.0f) return f4(0.0f);
    const float lpdf = sunConePdf();
    float w;
    if (pIn > 0.0f) {
        const float p3 = sunPatchPdf(s.p, s.ng, wl.lambda);
        w = weight3(pIn * lpdf, p3 * lpdf, pIn * lpdf, pIn * bpdf);
    } else {
        w = powerHeuristic(lpdf, bpdf);
    }
    return f * Le * T * (w / lpdf);
}

static __forceinline__ __device__ bool isDeltaType(int type) { return type == MAT_DIELECTRIC || type == MAT_WATER || type == MAT_FILM; }

static __forceinline__ __device__ float4 sunPatchConnect(const Surface& x, float3 wo, const Wavelengths& wl, Rng& rng) {
    const float total = params.sunPortalAreaTotal;
    if (params.numSunPortals == 0 || total <= 0.0f) return f4(0.0f);
    float u0 = rng.uniform() * total;
    int k = 0;
    while (k < params.numSunPortals - 1 && u0 >= params.sunPortalProj[k]) { u0 -= params.sunPortalProj[k]; ++k; }
    const Portal& P = params.sunPortals[k];
    const float2 u = rng.uniform2();
    float3 o = P.corner + P.edgeU * u.x + P.edgeV * u.y + params.sunDir * 0.05f;
    const float3 dir = -params.sunDir;
    Hit h;
    h.hit = false;
    for (int i = 0; i < 8; ++i) {                    // soap films let the sun through
        h = traceRadiance(o, dir, 1e16f, false);
        if (!h.hit || params.materials[h.mat].type != MAT_FILM) break;
        o = offsetRay(h.p, dot(dir, h.ngOut) > 0.0f ? h.ngOut : -h.ngOut);
    }
    if (!h.hit || (params.debugFlags & 4)) return f4(0.0f);
    const Material my = params.materials[h.mat];
    if (isDeltaType(my.type)) return f4(0.0f);      // glass and water: the photons carry that light
    const float3 ny = h.ngOut;
    const float cosYs = dot(ny, params.sunDir);
    if (cosYs <= 0.0f) return f4(0.0f);
    const int crossings = sunPortalCrossings(h.p);
    if (crossings == 0) return f4(0.0f);
    const float p3 = cosYs * crossings / total;

    float3 v = h.p - x.p;
    const float d2 = dot(v, v);
    const float dist = sqrtf(d2);
    const float3 w = v * (1.0f / dist);
    if (dot(w, x.ng) <= 0.0f) return f4(0.0f);
    const float cosYc = -dot(w, ny);
    if (cosYc <= 0.0f) return f4(0.0f);
    float pdfX;
    const float4 fx = bsdfEval(x, wo, w, pdfX);
    if (hmax(fx) <= 0.0f) return f4(0.0f);

    Surface y;
    y.p = h.p;
    y.ng = ny;
    y.ns = h.nsOut;
    y.matId = h.mat;
    if (params.debugFlags & 1) {
        y.type = MAT_DIFFUSE; y.albedo = f4(0.5f); y.coat = 0.0f; y.alpha = 1.0f; y.frame = Frame::from(y.ns);
    } else {
        // the patch points land on random materials: regroup threads before the procedural textures
        if (!(params.debugFlags & 8)) optixReorder((unsigned int)my.type & 15u, 4);
        prepareSurface(y, my, wl.lambda);
    }
    const float3 ls = Frame::from(params.sunDir).toWorld(sampleCone(rng.uniform2(), params.sunOneMinusCosMax));
    if (dot(ls, y.ng) <= 0.0f) return f4(0.0f);
    float pdfY;
    const float4 fy = bsdfEval(y, -w, ls, pdfY);
    if (hmax(fy) <= 0.0f) return f4(0.0f);
    const float4 Ts = traceShadow(offsetRay(y.p, y.ng), ls, 1e16f, wl.lambda, IOR_AIR);
    if (hmax(Ts) <= 0.0f) return f4(0.0f);
    const float4 Tc = traceShadow(offsetRay(x.p, x.ng), w, fmaxf(0.0f, dist - fmaxf(1e-3f, dist * 1e-4f)), wl.lambda, IOR_AIR);
    if (hmax(Tc) <= 0.0f) return f4(0.0f);
    const float4 Le = sunRadiance(ls, wl.lambda);
    const float pc = sunConePdf();
    const float pA = pdfX * cosYc / d2;
    const float wgt = weight3(p3 * pc, p3 * pc, pA * pc, pA * pdfY);
    return fx * fy * Le * Ts * Tc * (cosYc / d2 * wgt / (p3 * pc));
}

// Sky light: a one-sample mixture of portal sampling and sky-map sampling; skyPdf is the mixture's density.
static __forceinline__ __device__ float skyPdf(float3 p, float3 d) {
    const float f = params.skyPdfUV ? params.skyMapFraction : 0.0f;
    return (1.0f - f) * portalPdf(p, d) + f * skyMapPdf(d);
}

static __forceinline__ __device__ float4 neeSky(const Surface& s, float3 wo, const Wavelengths& wl, Rng& rng, int medium) {
    const float f = params.skyPdfUV ? params.skyMapFraction : 0.0f;
    float3 ld;
    if (rng.uniform() < f) {
        float pm;
        ld = sampleSkyMap(rng.uniform2(), pm);
        if (pm <= 0.0f) return f4(0.0f);
        rng.uniform();                                // keep the random stream aligned with the portal branch
    } else {
        if (params.numPortals == 0) return f4(0.0f);
        float w[MAX_PORTALS];
        const float sum = portalWeights(s.p, w);
        if (sum <= 0.0f) return f4(0.0f);
        float u0 = rng.uniform() * sum;
        int k = 0;
        while (k < params.numPortals - 1 && (u0 >= w[k] || w[k] <= 0.0f)) { u0 -= w[k]; ++k; }
        if (w[k] <= 0.0f) return f4(0.0f);
        const Portal& P = params.portals[k];
        const float2 u = rng.uniform2();
        const float3 q = P.corner + P.edgeU * u.x + P.edgeV * u.y;
        ld = normalize(q - s.p);
        if (dot(ld, P.normal) >= 0.0f) return f4(0.0f);
    }
    if (dot(ld, s.ng) <= 0.0f) return f4(0.0f);
    float bpdf;
    const float4 fr = bsdfEval(s, wo, ld, bpdf);
    if (hmax(fr) <= 0.0f) return f4(0.0f);
    const float lpdf = skyPdf(s.p, ld);
    if (lpdf <= 0.0f) return f4(0.0f);
    const float4 T = traceShadow(offsetRay(s.p, s.ng), ld, 1e16f, wl.lambda, medium);
    if (hmax(T) <= 0.0f) return f4(0.0f);
    const float4 Le = skyRadiance(ld, wl.lambda);
    return fr * Le * T * (powerHeuristic(lpdf, bpdf) / lpdf);
}

// The sunbeams in the room: points whose sunward line passes an opening's outer and inner faces. For one
// opening that is a convex region; its intersection with a ray is an interval, found by clipping the ray
// against the eight linear constraints (two rectangles, projected along the sun).
static __forceinline__ __device__ void clipRect(float3 o, float3 d, float3 C, float3 eU, float3 eV, float3 n, float margin,
                                               float& lo, float& hi) {
    const float dn = dot(params.sunDir, n);
    if (fabsf(dn) < 1e-6f) { hi = lo - 1.0f; return; }
    // oblique projection along the sun onto the rectangle's plane: P v = v - sunDir * dot(v, n) / dn
    const float3 v0 = o - C;
    const float3 p0 = v0 - params.sunDir * (dot(v0, n) / dn);
    const float3 p1 = d - params.sunDir * (dot(d, n) / dn);
    const float3 axes[2] = {eU, eV};
    for (int k = 0; k < 2; ++k) {
        const float inv = 1.0f / dot(axes[k], axes[k]);
        const float f0 = dot(p0, axes[k]) * inv, f1 = dot(p1, axes[k]) * inv;
        const float m = margin * sqrtf(inv);
        if (fabsf(f1) < 1e-9f) {
            if (f0 < -m || f0 > 1.0f + m) { hi = lo - 1.0f; return; }
            continue;
        }
        const float ta = (-m - f0) / f1, tb = (1.0f + m - f0) / f1;
        lo = fmaxf(lo, fminf(ta, tb));
        hi = fminf(hi, fmaxf(ta, tb));
    }
}
static __forceinline__ __device__ int beamIntervals(float3 o, float3 d, float t0, float t1, float2* iv, float& total) {
    int n = 0;
    total = 0.0f;
    for (int i = 0; i < params.numSunPortals; ++i) {
        const Portal& P = params.sunPortals[i];
        float lo = t0, hi = t1;
        clipRect(o, d, P.corner, P.edgeU, P.edgeV, P.normal, 0.04f, lo, hi);
        if (hi <= lo) continue;
        clipRect(o, d, P.corner + P.normal * params.sunPortalDepth[i], P.edgeU, P.edgeV, P.normal, 0.04f, lo, hi);
        if (hi <= lo) continue;
        iv[n++] = make_float2(lo, hi);
        total += hi - lo;
    }
    return n;
}

// Single scattering of sunlight in the room's haze along [t0, t1] of the ray (one sample). The distance is
// drawn from a mixture: inside the sunbeams (where the light is) or proportional to transmittance.
static __forceinline__ __device__ float4 hazeInscatter(float3 o, float3 d, float t0, float t1, const Wavelengths& wl, Rng& rng) {
    const float sigT = params.hazeSigmaS + params.hazeSigmaA;
    const float len = t1 - t0;
    float2 iv[MAX_PORTALS];
    float beamLen;
    const int nIv = beamIntervals(o, d, t0, t1, iv, beamLen);
    const float pBeam = nIv > 0 ? params.hazeBeamFraction : 0.0f;
    const bool trans = sigT * len > 1e-4f;
    const float norm = trans ? 1.0f - expf(-sigT * len) : 0.0f;
    float t;
    if (rng.uniform() < pBeam) {
        float u = rng.uniform() * beamLen;
        int k = 0;
        while (k < nIv - 1 && u >= iv[k].y - iv[k].x) { u -= iv[k].y - iv[k].x; ++k; }
        t = fminf(iv[k].x + u, iv[k].y);
    } else {
        const float u = rng.uniform();
        t = trans ? t0 - logf(1.0f - u * norm) / sigT : t0 + u * len;
    }
    // mixture density at t
    float pdfBeam = 0.0f;
    for (int k = 0; k < nIv; ++k) pdfBeam += (t >= iv[k].x && t <= iv[k].y) ? 1.0f : 0.0f;
    pdfBeam = beamLen > 0.0f ? pdfBeam / beamLen : 0.0f;
    const float pdfTrans = trans ? sigT * expf(-sigT * (t - t0)) / norm : 1.0f / len;
    const float pdfT = pBeam * pdfBeam + (1.0f - pBeam) * pdfTrans;
    if (pdfT <= 0.0f) return f4(0.0f);
    const float3 x = o + d * t;
    const float3 ld = Frame::from(params.sunDir).toWorld(sampleCone(rng.uniform2(), params.sunOneMinusCosMax));
    const float4 Le = sunRadiance(ld, wl.lambda);
    const float4 T = traceShadow(x, ld, 1e16f, wl.lambda, IOR_AIR);
    if (hmax(T) <= 0.0f) return f4(0.0f);
    const float Tcam = expf(-sigT * (t - t0));
    const float phase = henyeyGreenstein(dot(ld, d), params.hazeG);
    return Le * T * (params.hazeSigmaS * Tcam * phase / (pdfT * sunConePdf()));
}

// =====================================================================================================
// Caustic photons
// =====================================================================================================
// Photons leave the sun through a plane across the sun direction, aimed at the caustic objects' footprints
// in that plane (several rectangles; overlaps are handled by counting how many cover the point). Each photon
// carries one wavelength. Only photons that reach a non-delta surface after at least one glass or water event
// are stored: everything else is direct light, which next-event estimation already computes.
static __forceinline__ __device__ bool photonDeltaEvent(const Material& m, const Hit& h, float3& d, float lambda, int& medium,
                                                        float& flux, Rng& rng, bool& countsAsCaustic) {
    countsAsCaustic = false;
    if (m.type == MAT_FILM) {
        const float4 sp = params.spheres[h.prim];
        const float3 local = (h.p - make_float3(sp.x, sp.y, sp.z)) * (1.0f / sp.w);
        const float3 n = dot(d, h.ngOut) < 0.0f ? h.ngOut : -h.ngOut;
        const float R = thinFilmReflectance(lambda, filmThickness(local, m.p, params.time, float(h.prim) * 3.17f), iorOf(IOR_WATER, lambda),
                                            fabsf(dot(d, n)));
        if (rng.uniform() < R) d = reflect3(d, n);
        return true;
    }
    const float3 nsOut = m.type == MAT_WATER ? waterNormal(h.p, params.time, m.p.x) : h.nsOut;
    const bool front = dot(d, h.ngOut) < 0.0f;
    float3 n = front ? nsOut : -nsOut;
    const float3 ng = front ? h.ngOut : -h.ngOut;
    const int mi = front ? m.iorOutside : m.iorInside, mt = front ? m.iorInside : m.iorOutside;
    float cosI = -dot(d, n);
    if (cosI <= 1e-4f) { n = ng; cosI = -dot(d, n); }
    const float eta = iorOf(mt, lambda) / iorOf(mi, lambda);
    const float F = fresnelDielectric(cosI, eta);
    countsAsCaustic = true;
    if (rng.uniform() < F) {
        d = reflect3(d, n);
        if (dot(d, ng) <= 0.0f) d = reflect3(d, ng);
        medium = mi;
        return true;
    }
    bool ok;
    float3 t = refractDir(d, n, eta, cosI, ok);
    if (!ok || dot(t, ng) >= 0.0f) {
        t = refractDir(d, ng, eta, -dot(d, ng), ok);
        if (!ok) { d = reflect3(d, ng); medium = mi; return true; }
    }
    d = t;
    medium = mt;
    return true;
}

extern "C" __global__ void __raygen__photons() {
    const unsigned int i = optixGetLaunchIndex().x;
    Rng rng = makeRng(i, params.photonFrame, 0x9E0705u ^ params.seed);
    // footprint rectangle, chosen by area
    float ua = rng.uniform() * params.emitAreaTotal;
    int k = 0;
    while (k < params.numEmitters - 1 && ua >= params.emitArea[k]) { ua -= params.emitArea[k]; ++k; }
    const float4 R = params.emitRect[k];
    const float2 u2 = rng.uniform2();
    const float pu = lerpf(R.x, R.z, u2.x), pv = lerpf(R.y, R.w, u2.y);
    int cover = 0;
    for (int j = 0; j < params.numEmitters; ++j) {
        const float4 Q = params.emitRect[j];
        cover += pu >= Q.x && pu <= Q.z && pv >= Q.y && pv <= Q.w;
    }
    // direction inside the sun's disk, wavelength from the visible pdf
    const float3 w = Frame::from(params.sunDir).toWorld(sampleCone(rng.uniform2(), params.sunOneMinusCosMax));
    const float lambda = sampleVisibleWavelength(rng.uniform());
    const float pdfL = visibleWavelengthPdf(lambda);
    const float c = sqrtf(fmaxf(0.0f, 1.0f - sqr(dot(w, params.sunDir))));
    const float mu = sqrtf(fmaxf(0.0f, 1.0f - c * c / (params.sunOneMinusCosMax * (2.0f - params.sunOneMinusCosMax))));
    const float Le = sunTableAt(lambda) * (1.0f - params.sunLimb * (1.0f - mu)) * params.sunScale;
    float flux = Le * dot(w, params.sunDir) / sunConePdf() * (params.emitAreaTotal / fmaxf(1.0f, float(cover))) / pdfL /
                 float(params.photonsToEmit);
    const float3 onPlane = params.emitBasisU * pu + params.emitBasisV * pv + params.sunDir * params.emitDepth;
    float3 o = onPlane + w * 40.0f;
    float3 d = -w;
    int medium = IOR_AIR;
    bool caustic = false;
    for (int bounce = 0; bounce < 12; ++bounce) {
        const Hit h = traceRadiance(o, d, 1e16f, true);
        if (!h.hit) return;
        if (medium == IOR_AIR && hazeActive()) {
            float t0, t1;
            if (hazeSegment(o, d, h.t, t0, t1)) {
                if (caustic && params.enableBeams && rng.uniform() < params.beamFraction) {   // a caustic beam in the haze
                    const unsigned int slot = atomicAdd(params.beamCount, 1u);
                    if (slot < params.beamCapacity) {
                        PhotonBeam bm;
                        bm.a = o + d * t0;
                        bm.b = o + d * t1;
                        bm.lambda = lambda;
                        bm.flux = flux / params.beamFraction;
                        params.beamOut[slot] = bm;
                    }
                }
                flux *= expf(-(params.hazeSigmaS + params.hazeSigmaA) * (t1 - t0));
            }
        } else if (medium == IOR_WATER) {
            flux *= expf(-(waterAbsorption(lambda) + params.waterDepthTint) * h.t);
        }
        const Material m = params.materials[h.mat];
        if (isDeltaType(m.type)) {
            bool counts;
            photonDeltaEvent(m, h, d, lambda, medium, flux, rng, counts);
            caustic = caustic || counts;
            o = offsetRay(h.p, dot(d, h.ngOut) > 0.0f ? h.ngOut : -h.ngOut);
            continue;
        }
        if (!caustic) return;                        // direct light: next-event estimation's job
        const unsigned int slot = atomicAdd(params.photonCount, 1u);
        if (slot < params.photonCapacity) {
            Photon ph;
            ph.pos = h.p;
            ph.lambda = lambda;
            ph.dir = d;
            ph.flux = flux;
            params.photonOut[slot] = ph;
        }
        return;
    }
}

// Caustic radiance leaving s towards wo, as XYZ: Epanechnikov kernel over the photons within the radius.
static __forceinline__ __device__ float3 gatherCaustics(const Surface& s, float3 wo) {
    const float r = params.gatherRadius, r2 = r * r;
    const float h = params.cellSize;
    const int3 base = photonCell(s.p - make_float3(0.5f * h, 0.5f * h, 0.5f * h), h);
    float3 xyz = make_float3(0, 0, 0);
    Surface t = s;
    for (int k = 0; k < 8; ++k) {
        const int3 c = make_int3(base.x + (k & 1), base.y + ((k >> 1) & 1), base.z + (k >> 2));
        const unsigned int key = photonCellHash(c, params.gridSize);
        const unsigned int b = params.cellStart[key];
        if (b == PHOTON_EMPTY_CELL) continue;
        const unsigned int e = params.cellEnd[key];
        for (unsigned int i = b; i < e; ++i) {
            const Photon ph = params.photons[i];
            const float3 v = ph.pos - s.p;
            const float d2 = dot(v, v);
            if (d2 > r2) continue;
            if (fabsf(dot(v, s.ng)) > 0.25f * r || dot(ph.dir, s.ng) >= 0.0f) continue;   // other surface / other side
            const float a = albedoAt(s, ph.lambda);
            t.albedo = f4(a);
            t.f0 = f4(a);
            float pdf;
            const float3 wi = -ph.dir;
            const float cosI = dot(wi, s.ns);
            if (cosI <= 0.0f) continue;
            const float f = bsdfEval(t, wo, wi, pdf).x / cosI;
            xyz += cieXYZ(ph.lambda) * (f * ph.flux * 2.0f * (1.0f - d2 / r2));
        }
    }
    return xyz * (1.0f / (M_PIf * r2 * CIE_Y_INTEGRAL));
}

// Spectral throughput reduced to one number for the photon gather (photons carry their own wavelength).
static __forceinline__ __device__ float throughputGray(float4 beta, const Wavelengths& wl) {
    if (wl.pdf.y == 0.0f && wl.pdf.z == 0.0f && wl.pdf.w == 0.0f) return beta.x;
    return 0.25f * hsum(beta);
}

// =====================================================================================================
// Camera paths
// =====================================================================================================
struct PathOut {
    float3 xyz;
    float3 causticXyz;
    float3 albedo, normal;
    bool aov;
};

static __forceinline__ __device__ bool isDelta(int type) { return type == MAT_DIELECTRIC || type == MAT_WATER || type == MAT_FILM; }

static __forceinline__ __device__ PathOut tracePath(unsigned int px, unsigned int py, Rng& rng, int keepLane = 0) {
    PathOut out;
    out.xyz = out.causticXyz = make_float3(0, 0, 0);
    out.albedo = out.normal = make_float3(0, 0, 0);
    out.aov = false;
    Wavelengths wl = sampleWavelengths(rng.uniform());

    // ---- camera: Gaussian pixel filter, thin lens with a polygonal aperture
    const float2 ug = rng.uniform2();
    const float rad = fminf(0.42f * sqrtf(-2.0f * logf(fmaxf(1e-7f, ug.x))), 1.5f);
    const float jx = 0.5f + rad * cosf(2.0f * M_PIf * ug.y), jy = 0.5f + rad * sinf(2.0f * M_PIf * ug.y);
    const Camera& cam = params.cam;
    const float sx = ((px + jx) / params.renderWidth) * 2.0f - 1.0f;
    const float sy = 1.0f - ((py + jy) / params.renderHeight) * 2.0f;
    float3 d = cam.fwd + cam.right * (sx * cam.tanHalfX) + cam.up * (sy * cam.tanHalfY);
    float3 o = cam.pos;
    if (cam.lensRadius > 0.0f) {
        const float2 lp = samplePolygon(rng.uniform2(), cam.blades, cam.bladeRot) * cam.lensRadius;
        const float3 focus = cam.pos + d * cam.focusDist;
        o = cam.pos + cam.right * lp.x + cam.up * lp.y;
        d = normalize(focus - o);
    } else {
        d = normalize(d);
    }

    float4 beta = f4(1.0f), L = f4(0.0f);
    bool specularBounce = true;   // the previous vertex was delta (the camera counts as one)
    bool causticChain = false;    // non-delta vertex, then only photon-covered delta events since
    float prevPdf = 0.0f;
    float3 prevP = o, prevNg = make_float3(0, 1, 0);
    int medium = IOR_AIR;
    // patch-strategy bookkeeping: did the last non-delta vertex connect to sun patches, the distance
    // travelled since it (soap films pass straight through), and that vertex's own incoming area pdf
    bool prevPatch = false, vtxPrevPatch = false;
    float segDist = 0.0f, vtxPIn = 0.0f;
    const bool patches = params.enableSunPatch && params.enableNee && !params.furnace;
    int nonDelta = 0;
    const int maxDepth = params.furnace ? 4096 : params.maxDepth;

    for (int depth = 0; depth < maxDepth; ++depth) {
        const Hit h = traceRadiance(o, d, 1e16f, true, nonDelta == 0 ? 3u : 1u);
        const float tSeg = h.hit ? h.t : 1e16f;
        segDist += tSeg;

        if (medium == IOR_AIR && hazeActive() && (params.hazeAllSegments || nonDelta == 0)) {
            float t0, t1;
            if (hazeSegment(o, d, tSeg, t0, t1)) {
                if (params.enableBeams && params.numBeams > 0 && nonDelta == 0)
                    out.causticXyz += gatherBeams(o, d, tSeg) * throughputGray(beta, wl);
                L += beta * hazeInscatter(o, d, t0, t1, wl, rng);
                beta *= expf(-(params.hazeSigmaS + params.hazeSigmaA) * (t1 - t0));
            }
        } else if (medium == IOR_WATER && h.hit) {
            beta *= waterTransmittance(wl.lambda, h.t);
        }

        if (!h.hit) {
            const float4 Ls = skyRadiance(d, wl.lambda);
            float4 Lsun = sunRadiance(d, wl.lambda);
            float wSky = 1.0f, wSun = 1.0f;
            if (!specularBounce && params.enableNee) {
                wSky = powerHeuristic(prevPdf, skyPdf(prevP, d));
                if (vtxPrevPatch && vtxPIn > 0.0f && hmax(Lsun) > 0.0f) {
                    const float p3 = sunPatchPdf(prevP, prevNg, wl.lambda);
                    wSun = weight3(vtxPIn * prevPdf, p3 * sunConePdf(), vtxPIn * sunConePdf(), vtxPIn * prevPdf);
                } else {
                    wSun = powerHeuristic(prevPdf, sunConePdf());
                }
            }
            if (causticChain && params.enablePhotons) wSun = 0.0f;   // diffuse -> glass/water -> sun: the photon map carries it
            L += beta * (Ls * wSky + Lsun * wSun);
            break;
        }

        const Material m = params.materials[h.mat];
        if (isDelta(m.type) && !params.furnace) {
            DeltaResult r;
            const float3 ns = m.type == MAT_WATER ? waterNormal(h.p, params.time, m.p.x) : h.nsOut;
            if (m.type == MAT_FILM) {
                const float4 sp = params.spheres[h.prim];
                const float3 local = (h.p - make_float3(sp.x, sp.y, sp.z)) * (1.0f / sp.w);
                r = filmEvent(m, d, h.ngOut, local, float(h.prim) * 3.17f, wl, beta, rng.uniform());
            } else {
                r = dielectricEvent(m, d, h.ngOut, ns, wl, beta, rng.uniform(), false, params.cameraDispersion != 0, keepLane);
            }
            const float3 side = dot(r.dir, h.ngOut) > 0.0f ? h.ngOut : -h.ngOut;
            o = offsetRay(h.p, side);
            d = r.dir;
            medium = r.medium;
            if (!r.passThrough) {
                specularBounce = true;
                prevPatch = false;
            }
            // the photon map holds every sun path diffuse <- (delta)+ <- sun with at least one glass/water event
            if (m.type != MAT_FILM && prevPdf > 0.0f) causticChain = true;
            if (hmax(beta) <= 0.0f) break;
            continue;
        }

        // ---- non-delta surface
        Surface s;
        s.p = h.p;
        const bool front = dot(d, h.ngOut) < 0.0f;
        s.ng = front ? h.ngOut : -h.ngOut;
        s.ns = front ? h.nsOut : -h.nsOut;
        s.matId = h.mat;
        prepareSurface(s, m, wl.lambda);
        const float3 wo = -d;
        if (!out.aov) {
            out.aov = true;
            // albedo guide in linear sRGB: the reflectance spectrum under an equal-energy white, normalised
            const float3 a = xyzToLinearSRGB(spectrumToXYZ(s.albedo, wl));
            out.albedo = make_float3(a.x / 1.2049f, a.y / 0.9483f, a.z / 0.9088f);
            out.normal = s.ng;   // geometric: the bump normals made the denoiser draw streaks on the plaster
        }
        if (params.enablePhotons && params.numPhotons > 0 && nonDelta < params.gatherDepth) {
            const float3 c = gatherCaustics(s, wo) * throughputGray(beta, wl);
            out.causticXyz += c;
        }
        ++nonDelta;
        // area density of having reached s by BSDF sampling from a vertex that also tried the patch strategy
        const float pIn = prevPatch ? prevPdf * fabsf(dot(s.ng, d)) / fmaxf(segDist * segDist, 1e-8f) : 0.0f;
        const bool canPatch = patches && medium == IOR_AIR;
        if (params.enableNee) {
            L += beta * neeSun(s, wo, wl, rng, medium, pIn);
            L += beta * neeSky(s, wo, wl, rng, medium);
            if (canPatch) L += beta * sunPatchConnect(s, wo, wl, rng);
        }
        float3 wi;
        float4 w;
        float pdf;
        if (!bsdfSample(s, wo, make_float3(rng.uniform(), rng.uniform(), rng.uniform()), wi, w, pdf)) break;
        beta *= w;
        prevPdf = pdf;
        prevP = s.p;
        prevNg = s.ng;
        vtxPIn = pIn;
        vtxPrevPatch = prevPatch;
        prevPatch = canPatch;
        segDist = 0.0f;
        specularBounce = false;
        causticChain = false;
        o = offsetRay(s.p, s.ng);
        d = wi;

        // Russian roulette; off in the furnace test so the estimate stays bounded (paths end by escaping)
        if (depth >= 3 && !params.furnace) {
            const float q = fmaxf(0.05f, 1.0f - hmax(beta));
            if (rng.uniform() < q) break;
            beta *= 1.0f / (1.0f - q);
        }
    }
    out.xyz = spectrumToXYZ(L, wl) + out.causticXyz;
    return out;
}

extern "C" __global__ void __raygen__render() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int pix = idx.y * params.renderWidth + idx.x;
    const long long c0 = clock64();
    float3 sum = make_float3(0, 0, 0), sumC = make_float3(0, 0, 0), alb = make_float3(0, 0, 0), nrm = make_float3(0, 0, 0);
    float n = 0.0f;
    // pixels whose primary ray meets dispersive glass (the prism) get more samples: one wavelength per path
    // through it means colour noise there, and they are few
    const unsigned int spp = params.sppPerLaunch;
    bool split = false;
    if (params.dispersionBoost > 1u) {
        const Camera& cam = params.cam;
        const float sx = ((idx.x + 0.5f) / params.renderWidth) * 2.0f - 1.0f, sy = 1.0f - ((idx.y + 0.5f) / params.renderHeight) * 2.0f;
        const float3 d = normalize(cam.fwd + cam.right * (sx * cam.tanHalfX) + cam.up * (sy * cam.tanHalfY));
        const Hit h = traceRadiance(cam.pos, d, 1e16f, false);
        if (h.hit) {
            const Material& m = params.materials[h.mat];
            if (m.type == MAT_DIELECTRIC && (iorIsDispersive(m.iorInside) || iorIsDispersive(m.iorOutside))) split = true;
        }
    }
    for (unsigned int s = 0; s < spp; ++s) {
        Rng rng = makeRng(pix, (params.frame * params.sppPerLaunch) * 64u + s, 0xA511E9B3u ^ (params.seed * 0x9E3779B9u));
        PathOut r;
        if (split) {   // stratified spectral split: same random numbers, a different lane carried through the glass
            const Rng start = rng;
            r = tracePath(idx.x, idx.y, rng, 0);
            for (int k = 1; k < 4; ++k) {
                Rng rk = start;
                const PathOut q = tracePath(idx.x, idx.y, rk, k);
                r.xyz += q.xyz; r.causticXyz += q.causticXyz;
            }
            r.xyz *= 0.25f; r.causticXyz *= 0.25f;
        } else {
            r = tracePath(idx.x, idx.y, rng);
        }
        if (params.sampleClampY > 0.0f) {   // live mode: fireflies would become blotches in the denoiser
            float3 base = r.xyz - r.causticXyz;
            if (base.y > params.sampleClampY) base = base * (params.sampleClampY / base.y);
            r.xyz = base + r.causticXyz;
        }
        const float v = r.xyz.x + r.xyz.y + r.xyz.z + r.causticXyz.x;
        if (!isfinite(v)) {
            atomicAdd(params.nanCounter, 1u);
        } else {
            sum += r.xyz;
            sumC += r.causticXyz;
        }
        alb += r.albedo;
        nrm += r.normal;
        n += 1.0f;
    }
    const bool reset = params.frame == 0;
    if (params.liveAlpha > 0.0f && !reset) {
        // live mode: exponential moving average, normalised to one sample (w stays 1)
        const float k = params.liveAlpha / n, kc = params.liveAlphaCaustic / n;
        auto ema = [](float4 old, float3 s, float wNew, float alpha) {
            return make_float4(old.x * (1.0f - alpha) + s.x * wNew, old.y * (1.0f - alpha) + s.y * wNew, old.z * (1.0f - alpha) + s.z * wNew, 1.0f);
        };
        params.accum[pix] = ema(params.accum[pix], sum, k, params.liveAlpha);
        params.causticAccum[pix] = ema(params.causticAccum[pix], sumC, kc, params.liveAlphaCaustic);
        params.aovAlbedo[pix] = ema(params.aovAlbedo[pix], alb, k, params.liveAlpha);
        params.aovNormal[pix] = ema(params.aovNormal[pix], nrm, k, params.liveAlpha);
        params.heat[pix] = float(clock64() - c0);
        return;
    }
    const float wn = params.liveAlpha > 0.0f ? 1.0f / n : 1.0f;   // live mode keeps w = 1 from the first frame
    const float nn = params.liveAlpha > 0.0f ? 1.0f : n;
    float4 a = reset ? f4(0.0f) : params.accum[pix];
    params.accum[pix] = a + make_float4(sum.x * wn, sum.y * wn, sum.z * wn, nn);
    float4 c = reset ? f4(0.0f) : params.causticAccum[pix];
    params.causticAccum[pix] = c + make_float4(sumC.x * wn, sumC.y * wn, sumC.z * wn, nn);
    float4 aa = reset ? f4(0.0f) : params.aovAlbedo[pix];
    params.aovAlbedo[pix] = aa + make_float4(alb.x * wn, alb.y * wn, alb.z * wn, nn);
    float4 an = reset ? f4(0.0f) : params.aovNormal[pix];
    params.aovNormal[pix] = an + make_float4(nrm.x * wn, nrm.y * wn, nrm.z * wn, nn);
    params.heat[pix] = float(clock64() - c0);
}

// =====================================================================================================
// Display transform
// =====================================================================================================
static __forceinline__ __device__ float3 heatColor(float t) {
    t = saturate(t);
    return make_float3(saturate(1.5f * t * 2.0f - 0.5f), saturate(sinf(t * M_PIf)), saturate(1.0f - 2.0f * t));
}

// Reads a render-resolution buffer at an output pixel: identical sizes index directly, the upscaled live mode
// interpolates bilinearly (sample counts in w are constant there).
static __forceinline__ __device__ float4 renderBuffer(const float4* buf, unsigned int x, unsigned int y) {
    if (params.renderWidth == params.width) return buf[y * params.width + x];
    const float fx = (x + 0.5f) * params.renderWidth / params.width - 0.5f, fy = (y + 0.5f) * params.renderHeight / params.height - 0.5f;
    const int x0 = max(0, min(int(floorf(fx)), int(params.renderWidth) - 1)), y0 = max(0, min(int(floorf(fy)), int(params.renderHeight) - 1));
    const int x1 = min(x0 + 1, int(params.renderWidth) - 1), y1 = min(y0 + 1, int(params.renderHeight) - 1);
    const float tx = saturate(fx - x0), ty = saturate(fy - y0);
    const unsigned int W = params.renderWidth;
    return (buf[y0 * W + x0] * (1.0f - tx) + buf[y0 * W + x1] * tx) * (1.0f - ty) + (buf[y1 * W + x0] * (1.0f - tx) + buf[y1 * W + x1] * tx) * ty;
}

// sRGB encoding (unless the view is already display-referred), blue-noise dither of +-1 LSB, 8-bit output.
static __forceinline__ __device__ void writeDisplay(unsigned int pix, unsigned int x, unsigned int y, float3 rgb, bool linearOut) {
    const float3 enc = linearOut ? make_float3(srgbOetf(rgb.x), srgbOetf(rgb.y), srgbOetf(rgb.z)) : rgb;
    const unsigned int bx = (x + 37u * params.ditherFrame) & 127u, by = (y + 71u * params.ditherFrame) & 127u;
    const float bn = params.blueNoise ? params.blueNoise[by * 128u + bx] : 0.5f;
    const float v = bn * 2.0f - 1.0f;
    const float tri = copysignf(1.0f - sqrtf(1.0f - fabsf(v)), v);
    const float dth = tri / 255.0f;
    params.output[pix] = make_uchar4((unsigned char)(saturate(enc.x + dth) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(enc.y + dth) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(enc.z + dth) * 255.0f + 0.5f), 255);
}

extern "C" __global__ void __raygen__tonemap() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int pix = idx.y * params.width + idx.x;
    float3 rgb;
    bool linearOut = true;
    if (params.view == 1 || params.view == 2) {
        const float4 a = renderBuffer(params.view == 1 ? params.aovAlbedo : params.aovNormal, idx.x, idx.y);
        rgb = make_float3(a.x, a.y, a.z) * (1.0f / fmaxf(a.w, 1.0f));
        if (params.view == 2) rgb = rgb * 0.5f + make_float3(0.5f, 0.5f, 0.5f);
        linearOut = false;
    } else if (params.view == 4) {
        rgb = heatColor(params.heat[(idx.y * params.renderHeight / params.height) * params.renderWidth + idx.x * params.renderWidth / params.width] /
                        (params.exposure * 2.0e6f));
        linearOut = false;
    } else {
        const bool den = params.denoised && params.view == 0;
        const float4 a = renderBuffer(params.view == 3 || den ? params.causticAccum : params.accum, idx.x, idx.y);
        const float3 raw = make_float3(a.x, a.y, a.z) * (1.0f / fmaxf(a.w, 1.0f)) * params.exposure;
        const float* m = params.wbMatrix;
        const float3 xyz = make_float3(m[0] * raw.x + m[1] * raw.y + m[2] * raw.z, m[3] * raw.x + m[4] * raw.y + m[5] * raw.z,
                                       m[6] * raw.x + m[7] * raw.y + m[8] * raw.z);
        float3 lin = xyzToLinearSRGB(xyz);
        float Y = xyz.y;
        if (den && params.denoisedCaustic) {   // live mode: the caustic layer comes denoised as well
            const float4 dc = params.denoisedCaustic[pix];
            lin = make_float3(dc.x, dc.y, dc.z) * params.exposure;
            Y = 0.2126f * lin.x + 0.7152f * lin.y + 0.0722f * lin.z;
        }
        if (den) {   // the denoised diffuse/glossy light plus the caustics, which never went through the denoiser
            const float4 dn = params.denoised[pix];
            const float3 d = make_float3(dn.x, dn.y, dn.z) * params.exposure;
            lin = lin + d;
            Y += 0.2126f * d.x + 0.7152f * d.y + 0.0722f * d.z;
        }
        // beauty and caustic views continue in __raygen__composite (after the glare pass)
        params.hdr[pix] = make_float4(lin.x, lin.y, lin.z, Y);
        return;
    }
    writeDisplay(pix, idx.x, idx.y, rgb, linearOut);
}

// Glare (half-resolution bloom, bilinear) + AgX + sRGB + dither for the beauty and caustic views.
extern "C" __global__ void __raygen__composite() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int pix = idx.y * params.width + idx.x;
    const float4 h = params.hdr[pix];
    float3 lin = make_float3(h.x, h.y, h.z);
    float Y = h.w;
    if (params.bloom && params.bloomStrength > 0.0f) {
        const unsigned int bw = params.width / 2, bh = params.height / 2;
        const float fx = (idx.x + 0.5f) * 0.5f - 0.5f, fy = (idx.y + 0.5f) * 0.5f - 0.5f;
        const int x0 = max(0, min(int(floorf(fx)), int(bw) - 1)), y0 = max(0, min(int(floorf(fy)), int(bh) - 1));
        const int x1 = min(x0 + 1, int(bw) - 1), y1 = min(y0 + 1, int(bh) - 1);
        const float tx = saturate(fx - x0), ty = saturate(fy - y0);
        const float4 b = (params.bloom[y0 * bw + x0] * (1.0f - tx) + params.bloom[y0 * bw + x1] * tx) * (1.0f - ty) +
                         (params.bloom[y1 * bw + x0] * (1.0f - tx) + params.bloom[y1 * bw + x1] * tx) * ty;
        const float s = params.bloomStrength;
        lin = lin * (1.0f - s) + make_float3(b.x, b.y, b.z) * s;
        Y = Y * (1.0f - s) + b.w * s;
    }
    writeDisplay(pix, idx.x, idx.y, agx(gamutCompress(lin, Y)), true);
}

