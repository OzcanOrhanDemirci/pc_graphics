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

static __forceinline__ __device__ Hit traceRadiance(float3 o, float3 d, float tmax) {
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(255), OPTIX_RAY_FLAG_DISABLE_ANYHIT,
                  RAY_RADIANCE, RAY_TYPES, RAY_RADIANCE);
    Hit h;
    h.hit = optixHitObjectIsHit();
    h.mat = -1;
    if (!h.hit) {
        optixReorder(15u, 4);
        return h;
    }
    h.t = optixHitObjectGetRayTmax();
    h.inst = optixHitObjectGetInstanceId();
    h.prim = optixHitObjectGetPrimitiveIndex();
    h.mat = h.inst == INST_TRIANGLES ? params.triMaterial[h.prim] : params.sphereMaterial[h.prim];
    optixReorder((unsigned int)params.materials[h.mat].type & 15u, 4);
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
    optixTrace(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(255),
               OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT | OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT, RAY_SHADOW, RAY_TYPES, RAY_SHADOW,
               vis, t0, t1, t2, t3, l0, l1, l2, l3);
    if (!vis) return f4(0.0f);
    float4 T = make_float4(u2f(t0), u2f(t1), u2f(t2), u2f(t3));
    if (params.enableHaze && params.hazeSigmaS + params.hazeSigmaA > 0.0f) {
        float a, b;
        if (hazeSegment(o, d, tmax, a, b)) T *= expf(-(params.hazeSigmaS + params.hazeSigmaA) * (b - a));
    }
    return T;
}

extern "C" __global__ void __miss__shadow() { optixSetPayload_0(1u); }

extern "C" __global__ void __anyhit__shadow() {
    // only spheres have an any-hit program: soap films transmit, everything else blocks
    const int prim = optixGetPrimitiveIndex();
    const int mi = params.sphereMaterial[prim];
    const Material& m = params.materials[mi];
    if (m.type != MAT_FILM) return;                       // accept: blocks (terminate on first hit)
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
// Solid-angle pdf of reaching the sky through the portals in direction d from p (one portal chosen
// uniformly, a uniform point on it).
static __forceinline__ __device__ float portalPdf(float3 p, float3 d) {
    float pdf = 0.0f;
    for (int i = 0; i < params.numPortals; ++i) {
        const Portal& P = params.portals[i];
        const float dn = dot(d, P.normal);
        if (dn >= 0.0f) continue;                  // must leave the room through the portal
        const float t = dot(P.corner - p, P.normal) / dn;
        if (t <= 0.0f) continue;
        const float3 q = p + d * t - P.corner;
        const float a = dot(q, P.edgeU) / dot(P.edgeU, P.edgeU), b = dot(q, P.edgeV) / dot(P.edgeV, P.edgeV);
        if (a < 0.0f || a > 1.0f || b < 0.0f || b > 1.0f) continue;
        pdf += t * t / (-dn * P.area);
    }
    return params.numPortals > 0 ? pdf / params.numPortals : 0.0f;
}

static __forceinline__ __device__ float4 neeSun(const Surface& s, float3 wo, const Wavelengths& wl, Rng& rng, int medium) {
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
    return f * Le * T * (powerHeuristic(lpdf, bpdf) / lpdf);
}

static __forceinline__ __device__ float4 neeSky(const Surface& s, float3 wo, const Wavelengths& wl, Rng& rng, int medium) {
    if (params.numPortals == 0) return f4(0.0f);
    const float u0 = rng.uniform();
    const int k = min(int(u0 * params.numPortals), params.numPortals - 1);
    const Portal& P = params.portals[k];
    const float2 u = rng.uniform2();
    const float3 q = P.corner + P.edgeU * u.x + P.edgeV * u.y;
    float3 ld = q - s.p;
    const float dist = length(ld);
    ld = ld * (1.0f / dist);
    if (dot(ld, P.normal) >= 0.0f || dot(ld, s.ng) <= 0.0f) return f4(0.0f);
    float bpdf;
    const float4 f = bsdfEval(s, wo, ld, bpdf);
    if (hmax(f) <= 0.0f) return f4(0.0f);
    const float lpdf = portalPdf(s.p, ld);
    if (lpdf <= 0.0f) return f4(0.0f);
    const float4 T = traceShadow(offsetRay(s.p, s.ng), ld, 1e16f, wl.lambda, medium);
    if (hmax(T) <= 0.0f) return f4(0.0f);
    const float4 Le = skyRadiance(ld, wl.lambda);
    return f * Le * T * (powerHeuristic(lpdf, bpdf) / lpdf);
}

// Single scattering of sunlight in the room's haze along [t0, t1] of the ray (one sample).
static __forceinline__ __device__ float4 hazeInscatter(float3 o, float3 d, float t0, float t1, const Wavelengths& wl, Rng& rng) {
    const float sigT = params.hazeSigmaS + params.hazeSigmaA;
    const float len = t1 - t0;
    // sample distance proportionally to transmittance within the segment
    const float u = rng.uniform();
    float t, pdfT;
    if (sigT * len > 1e-4f) {
        const float norm = 1.0f - expf(-sigT * len);
        const float dt = -logf(1.0f - u * norm) / sigT;
        t = t0 + dt;
        pdfT = sigT * expf(-sigT * dt) / norm;
    } else {
        t = t0 + u * len;
        pdfT = 1.0f / len;
    }
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
// Camera paths
// =====================================================================================================
struct PathOut {
    float3 xyz;
    float3 causticXyz;
    float3 albedo, normal;
    bool aov;
};

static __forceinline__ __device__ bool isDelta(int type) { return type == MAT_DIELECTRIC || type == MAT_WATER || type == MAT_FILM; }

static __forceinline__ __device__ PathOut tracePath(unsigned int px, unsigned int py, Rng& rng) {
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
    const float sx = ((px + jx) / params.width) * 2.0f - 1.0f;
    const float sy = 1.0f - ((py + jy) / params.height) * 2.0f;
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
    float3 prevP = o;
    int medium = IOR_AIR;
    const int maxDepth = params.furnace ? 4096 : params.maxDepth;

    for (int depth = 0; depth < maxDepth; ++depth) {
        const Hit h = traceRadiance(o, d, 1e16f);
        const float tSeg = h.hit ? h.t : 1e16f;

        if (medium == IOR_AIR && params.enableHaze && !params.furnace) {
            float t0, t1;
            if (hazeSegment(o, d, tSeg, t0, t1)) {
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
                wSky = powerHeuristic(prevPdf, portalPdf(prevP, d));
                wSun = powerHeuristic(prevPdf, sunConePdf());
            }
            if (causticChain && params.enablePhotons) wSun = 0.0f;   // the photon map carries this light
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
                r = dielectricEvent(m, d, h.ngOut, ns, wl, beta, rng.uniform(), false);
            }
            const float3 side = dot(r.dir, h.ngOut) > 0.0f ? h.ngOut : -h.ngOut;
            o = offsetRay(h.p, side);
            d = r.dir;
            medium = r.medium;
            if (!r.passThrough) {
                specularBounce = true;
                if (m.type != MAT_FILM) causticChain = causticChain || prevPdf > 0.0f;
                else causticChain = false;
            }
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
            out.albedo = make_float3(1, 1, 1) * (0.25f * hsum(s.albedo));
            out.normal = s.ns;
        }
        if (params.enableNee) {
            L += beta * neeSun(s, wo, wl, rng, medium);
            L += beta * neeSky(s, wo, wl, rng, medium);
        }
        float3 wi;
        float4 w;
        float pdf;
        if (!bsdfSample(s, wo, make_float3(rng.uniform(), rng.uniform(), rng.uniform()), wi, w, pdf)) break;
        beta *= w;
        prevPdf = pdf;
        prevP = s.p;
        specularBounce = false;
        causticChain = false;
        o = offsetRay(s.p, s.ng);
        d = wi;

        if (depth >= 3) {
            const float q = fmaxf(0.05f, 1.0f - hmax(beta));
            if (rng.uniform() < q) break;
            beta *= 1.0f / (1.0f - q);
        }
    }
    out.xyz = spectrumToXYZ(L, wl);
    return out;
}

extern "C" __global__ void __raygen__render() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int pix = idx.y * params.width + idx.x;
    const long long c0 = clock64();
    float3 sum = make_float3(0, 0, 0), sumC = make_float3(0, 0, 0), alb = make_float3(0, 0, 0), nrm = make_float3(0, 0, 0);
    float n = 0.0f;
    for (unsigned int s = 0; s < params.sppPerLaunch; ++s) {
        Rng rng = makeRng(pix, params.frame * params.sppPerLaunch + s, 0xA511E9B3u);
        const PathOut r = tracePath(idx.x, idx.y, rng);
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
    float4 a = reset ? f4(0.0f) : params.accum[pix];
    params.accum[pix] = a + make_float4(sum.x, sum.y, sum.z, n);
    float4 c = reset ? f4(0.0f) : params.causticAccum[pix];
    params.causticAccum[pix] = c + make_float4(sumC.x, sumC.y, sumC.z, n);
    float4 aa = reset ? f4(0.0f) : params.aovAlbedo[pix];
    params.aovAlbedo[pix] = aa + make_float4(alb.x, alb.y, alb.z, n);
    float4 an = reset ? f4(0.0f) : params.aovNormal[pix];
    params.aovNormal[pix] = an + make_float4(nrm.x, nrm.y, nrm.z, n);
    params.heat[pix] = float(clock64() - c0);
}

// =====================================================================================================
// Display transform
// =====================================================================================================
static __forceinline__ __device__ float3 agxContrast(float3 x) {
    auto p = [](float v) {
        const float v2 = v * v, v4 = v2 * v2;
        return 15.5f * v4 * v2 - 40.14f * v4 * v + 31.96f * v4 - 6.868f * v2 * v + 0.4298f * v2 + 0.1191f * v - 0.00232f;
    };
    return make_float3(p(x.x), p(x.y), p(x.z));
}
// AgX (Troy Sobotka; minimal fit by Benjamin Wrensch). Input linear Rec.709, output display-linear.
static __forceinline__ __device__ float3 agx(float3 c) {
    float3 v = make_float3(0.842479062253094f * c.x + 0.0784335999999992f * c.y + 0.0792237451477643f * c.z,
                           0.0423282422610123f * c.x + 0.878468636469772f * c.y + 0.0791661274605434f * c.z,
                           0.0423756549057051f * c.x + 0.0784336f * c.y + 0.879142973793104f * c.z);
    const float minEv = -12.47393f, maxEv = 4.026069f;
    v = make_float3(clampf(log2f(fmaxf(v.x, 1e-10f)), minEv, maxEv), clampf(log2f(fmaxf(v.y, 1e-10f)), minEv, maxEv),
                    clampf(log2f(fmaxf(v.z, 1e-10f)), minEv, maxEv));
    v = (v - make_float3(minEv, minEv, minEv)) * (1.0f / (maxEv - minEv));
    v = agxContrast(v);
    float3 o = make_float3(1.19687900512017f * v.x - 0.0980208811401368f * v.y - 0.0990297440797205f * v.z,
                           -0.0528968517574562f * v.x + 1.15190312990417f * v.y - 0.0989611768448433f * v.z,
                           -0.0529716355144438f * v.x - 0.0980434501171241f * v.y + 1.15107367264116f * v.z);
    return make_float3(powf(fmaxf(o.x, 0.0f), 2.2f), powf(fmaxf(o.y, 0.0f), 2.2f), powf(fmaxf(o.z, 0.0f), 2.2f));
}
static __forceinline__ __device__ float srgbOetf(float x) {
    x = saturate(x);
    return x <= 0.0031308f ? 12.92f * x : 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}
// Out-of-gamut spectral colours: pull towards the luminance until no channel is negative.
static __forceinline__ __device__ float3 gamutCompress(float3 rgb, float Y) {
    const float mn = fminf(rgb.x, fminf(rgb.y, rgb.z));
    if (mn >= 0.0f || Y <= 0.0f) return fmaxf3(rgb, make_float3(0, 0, 0));
    const float t = Y / (Y - mn);
    return make_float3(Y, Y, Y) + (rgb - make_float3(Y, Y, Y)) * t;
}
static __forceinline__ __device__ float3 heatColor(float t) {
    t = saturate(t);
    return make_float3(saturate(1.5f * t * 2.0f - 0.5f), saturate(sinf(t * M_PIf)), saturate(1.0f - 2.0f * t));
}

extern "C" __global__ void __raygen__tonemap() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int pix = idx.y * params.width + idx.x;
    float3 rgb;
    bool linearOut = true;
    if (params.view == 1 || params.view == 2) {
        const float4 a = params.view == 1 ? params.aovAlbedo[pix] : params.aovNormal[pix];
        rgb = make_float3(a.x, a.y, a.z) * (1.0f / fmaxf(a.w, 1.0f));
        if (params.view == 2) rgb = rgb * 0.5f + make_float3(0.5f, 0.5f, 0.5f);
        linearOut = false;
    } else if (params.view == 4) {
        rgb = heatColor(params.heat[pix] / (params.exposure * 2.0e6f));
        linearOut = false;
    } else {
        const float4 a = params.view == 3 ? params.causticAccum[pix] : params.accum[pix];
        const float3 xyz = make_float3(a.x, a.y, a.z) * (1.0f / fmaxf(a.w, 1.0f)) * params.exposure;
        rgb = agx(gamutCompress(xyzToLinearSRGB(xyz), xyz.y));
    }
    float3 enc = linearOut ? make_float3(srgbOetf(rgb.x), srgbOetf(rgb.y), srgbOetf(rgb.z)) : rgb;
    // blue-noise dither with a triangular distribution of +-1 LSB
    const unsigned int bx = (idx.x + 37u * params.ditherFrame) & 127u, by = (idx.y + 71u * params.ditherFrame) & 127u;
    const float bn = params.blueNoise ? params.blueNoise[by * 128u + bx] : 0.5f;
    const float v = bn * 2.0f - 1.0f;
    const float tri = copysignf(1.0f - sqrtf(1.0f - fabsf(v)), v);
    const float dth = tri / 255.0f;
    params.output[pix] = make_uchar4((unsigned char)(saturate(enc.x + dth) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(enc.y + dth) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(enc.z + dth) * 255.0f + 0.5f), 255);
}
