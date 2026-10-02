// Scene 4 "Tanecik": OptiX device program, compiled at run time with NVRTC.
//   __raygen__render  a small path tracer for a photo studio. Soft boxes (rectangles, sampled directly, seen in
//                     reflections), a white cyclorama, the open studio's ambient light. The figure's skin is a thin
//                     tinted shell in the glass look: at each crossing Fresnel reflection, a frosted share scattered
//                     like skin, the rest passes on, tinted by the shell's optical depth over |cos| (so the silhouette
//                     deepens in colour); bones and particles show inside. In the skin look it is opaque, with a sheen.
//                     The free liquid (a surface rebuilt every frame, and droplets as spheres) is a wet mirror over a
//                     dark body.
#include <optix.h>
#include "scenes/tanecik/tanecik_params.h"
#include "shared/display.h"

extern "C" {
__constant__ TanecikParams params;
}

static __forceinline__ __device__ unsigned int hashU32(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
struct Rng {
    unsigned int s;
    __device__ float next() { s = hashU32(s + 0x9e3779b9u); return (s >> 8) * (1.0f / 16777216.0f); }
};

static __forceinline__ __device__ float3 sampleCosine(float u1, float u2, float3 n) {
    const float r = sqrtf(u1), phi = 2.0f * M_PIf * u2;
    const Frame f = Frame::from(n);
    return f.toWorld(make_float3(r * cosf(phi), r * sinf(phi), sqrtf(fmaxf(0.0f, 1.0f - u1))));
}
static __forceinline__ __device__ float fresnel(float f0, float c) {
    const float m = 1.0f - c, m2 = m * m;
    return f0 + (1.0f - f0) * m2 * m2 * m;
}
static __forceinline__ __device__ float3 exp3(float3 v) { return make_float3(expf(v.x), expf(v.y), expf(v.z)); }

struct Hit {
    bool hit;
    float t;
    float3 n;          // shading normal, outward
    unsigned int kind; // TKind
    unsigned int prim;
};

static __forceinline__ __device__ Hit trace(float3 o, float3 d, float tmin, float tmax) {
    optixTraverse(params.handle, o, d, tmin, tmax, 0.0f, OptixVisibilityMask(1u), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0);
    Hit h;
    h.hit = optixHitObjectIsHit();
    if (!h.hit) return h;
    h.t = optixHitObjectGetRayTmax();
    const unsigned int id = optixHitObjectGetInstanceId();
    h.kind = id & 0xffu;
    h.prim = optixHitObjectGetPrimitiveIndex();
    if (h.kind == TK_PARTICLES) {
        h.n = normalize(o + d * h.t - params.particles[h.prim]);
    } else if (h.kind == TK_DROPS) {
        const float4 s = params.drops[h.prim];
        h.n = normalize(o + d * h.t - make_float3(s.x, s.y, s.z));
    } else {
        // a bone: its rest mesh, placed by its instance; the liquid: this frame's surface
        const TMesh m = h.kind == TK_LIQUID ? params.liquid : params.meshes[h.kind == TK_BONES ? 2u + (id >> 8) : h.kind];
        const uint3 tri = m.idx[h.prim];
        const float2 b = optixHitObjectGetTriangleBarycentrics();
        const float3 ns = optixHitObjectTransformNormalFromObjectToWorldSpace(m.nrm[tri.x] * (1.0f - b.x - b.y) + m.nrm[tri.y] * b.x + m.nrm[tri.z] * b.y);
        const float3 v0 = m.pos[tri.x], v1 = m.pos[tri.y], v2 = m.pos[tri.z];
        const float3 ng = normalize(optixHitObjectTransformNormalFromObjectToWorldSpace(cross(v1 - v0, v2 - v0)));
        h.n = dot(ns, ns) > 1e-12f ? normalize(ns) : ng;
        if (dot(h.n, ng) < 0.0f) h.n = ng;
    }
    return h;
}

// the soft boxes along a ray, nearer than tmax: the first one hit (one-sided), its radiance
static __forceinline__ __device__ bool lightHit(float3 o, float3 d, float tmax, float3& Le) {
    float best = tmax;
    bool any = false;
    for (int i = 0; i < params.lightCount; ++i) {
        const TRectLight& l = params.lights[i];
        const float3 n = normalize(cross(l.u, l.v));
        const float den = dot(d, n);
        if (den >= 0.0f) continue;
        const float t = dot(l.c - o, n) / den;
        if (t <= 1e-4f || t >= best) continue;
        const float3 q = o + d * t - l.c;
        if (fabsf(dot(q, l.u)) > dot(l.u, l.u) || fabsf(dot(q, l.v)) > dot(l.v, l.v)) continue;
        best = t;
        Le = l.Le;
        any = true;
    }
    return any;
}

static __forceinline__ __device__ float3 ambient(float3 d) {
    return params.ambient * (0.8f + 0.2f * fmaxf(d.y, 0.0f));
}

// one crossing of the glass shell, the part that passes straight on
static __forceinline__ __device__ float3 shellPass(float c) {
    return exp3(params.shellOptical * (-1.0f / fmaxf(c, 0.08f))) * ((1.0f - fresnel(params.skinF0, c)) * (1.0f - params.skinFrost));
}

// how much of a light gets from o to o + d * tmax: through glass skin, stopped by anything else
static __forceinline__ __device__ float3 shadow(float3 o, float3 d, float tmax) {
    float3 T = make_float3(1, 1, 1);
    for (int i = 0; i < 16; ++i) {
        const Hit h = trace(o, d, 1e-4f, tmax);
        if (!h.hit) return T;
        if (h.kind != TK_SKIN || !params.glass) return make_float3(0, 0, 0);
        T = T * shellPass(fabsf(dot(h.n, d)));
        if (maxc(T) < 1e-4f) return make_float3(0, 0, 0);
        o = o + d * h.t;
        tmax -= h.t;
    }
    return T;
}

// light arriving at a diffuse point straight from one soft box (one sample), already over pi: times albedo = radiance
static __forceinline__ __device__ float3 direct(float3 p, float3 n, Rng& r) {
    if (params.lightCount <= 0) return make_float3(0, 0, 0);
    const int k = min(int(r.next() * params.lightCount), params.lightCount - 1);
    const TRectLight& l = params.lights[k];
    const float3 q = l.c + l.u * (2.0f * r.next() - 1.0f) + l.v * (2.0f * r.next() - 1.0f);
    float3 w = q - p;
    const float d2 = dot(w, w), d = sqrtf(d2);
    w = w / d;
    const float cs = dot(n, w);
    if (cs <= 0.0f) return make_float3(0, 0, 0);
    const float3 ln = cross(l.u, l.v);
    const float area4 = length(ln);   // |u x v| = a quarter of the box's area
    const float cl = -dot(ln, w) / area4;
    if (cl <= 0.0f) return make_float3(0, 0, 0);
    const float3 T = shadow(p + n * 1e-4f, w, d - 2e-4f);
    return l.Le * T * (cs * cl * 4.0f * area4 * float(params.lightCount) / d2 * INV_PIf);
}

static __forceinline__ __device__ float3 glossy(float3 d, float3 n, float e, Rng& r) {
    const float3 rr = reflect3(d, n);
    const float c = powf(r.next(), 1.0f / (e + 1.0f)), s = sqrtf(fmaxf(0.0f, 1.0f - c * c)), phi = 2.0f * M_PIf * r.next();
    return Frame::from(rr).toWorld(make_float3(s * cosf(phi), s * sinf(phi), c));
}

static __forceinline__ __device__ float3 radiance(float3 o, float3 d, Rng& r) {
    float3 T = make_float3(1, 1, 1), L = make_float3(0, 0, 0);
    // the soft boxes count when seen along a specular chain (else they were sampled directly), but not straight from
    // the camera: a photographer keeps them out of the frame, their reflections are what shows
    bool specular = true;
    int diffuse = 0;
    for (int depth = 0; depth < 40; ++depth) {
        const Hit h = trace(o, d, 0.0f, 1e30f);
        float3 Le;
        if (depth > 0 && lightHit(o, d, h.hit ? h.t : 1e30f, Le)) {
            if (specular) L = L + T * Le;
            break;
        }
        if (!h.hit) {
            L = L + T * ambient(d);
            break;
        }
        const float3 p = o + d * h.t;
        const float3 nn = dot(h.n, d) < 0.0f ? h.n : -h.n;   // the side the ray came from
        const float c = fminf(1.0f, -dot(nn, d));
        float3 alb;
        if (h.kind == TK_SKIN) {
            const float F = fresnel(params.skinF0, c);
            if (r.next() < F) {   // reflection
                d = params.glass ? reflect3(d, nn) : glossy(d, nn, params.skinGloss, r);
                if (dot(d, nn) <= 0.0f) break;
                o = p + nn * 1e-4f;
                continue;
            }
            if (params.glass && r.next() >= params.skinFrost) {   // through the shell
                T = T * exp3(params.shellOptical * (-1.0f / fmaxf(c, 0.08f)));
                o = p + d * 2e-4f;
                continue;
            }
            alb = params.skinAlbedo;
        } else if (h.kind == TK_LIQUID || h.kind == TK_DROPS) {
            // blood: a wet mirror (Fresnel) over a dark, all but opaque body
            if (r.next() < fresnel(params.liquidF0, c)) {
                d = reflect3(d, nn);
                o = p + nn * 1e-4f;
                specular = true;
                continue;
            }
            alb = params.liquidAlbedo;
        } else if (h.kind == TK_PARTICLES) {
            const float k = params.particleTint ? params.particleTint[h.prim] : 0.0f;
            alb = lerp3(params.tissueAlbedo, params.liquidAlbedo, k);
        } else if (h.kind == TK_BONES) {
            alb = params.boneAlbedo;
        } else {
            alb = params.studioAlbedo;
        }
        // a diffuse event: the soft boxes directly, then (a few times) the rest of the room
        L = L + T * alb * direct(p, nn, r);
        if (diffuse >= params.maxDiffuse) break;
        ++diffuse;
        T = T * alb;
        d = sampleCosine(r.next(), r.next(), nn);
        o = p + nn * 1e-4f;
        specular = false;
        if (maxc(T) < 1e-3f) break;
    }
    return L;
}

extern "C" __global__ void __raygen__render() {
    const uint3 ix = optixGetLaunchIndex();
    const unsigned int px = ix.x, py = ix.y;
    const unsigned int pix = py * params.width + px;
    Rng r;
    r.s = hashU32(pix * 9781u + params.seed * 6271u + params.samples * 26699u);
    const TCamera& C = params.cam;
    float3 sum = make_float3(0, 0, 0);
    for (unsigned int s = 0; s < params.spp; ++s) {
        const float jx = r.next(), jy = r.next();
        const float u = ((px + jx) / params.width) * 2.0f - 1.0f, v = 1.0f - ((py + jy) / params.height) * 2.0f;
        const float3 d = normalize(C.fwd + C.right * (u * C.tanHalfX) + C.up * (v * C.tanHalfY));
        float3 L = radiance(C.pos, d, r);
        const float m = maxc(L);
        if (m > 64.0f) L = L * (64.0f / m);   // fireflies: a rare path through the shell to a soft box
        sum = sum + L;
    }
    float4 acc = params.samples > 0 ? params.accum[pix] : make_float4(0, 0, 0, 0);
    acc = acc + make_float4(sum.x, sum.y, sum.z, 0.0f);
    params.accum[pix] = acc;
    const float inv = 1.0f / float(params.samples + params.spp);
    float3 col = make_float3(acc.x, acc.y, acc.z) * (inv * params.exposure);
    col = agx(col);
    const float dn = (r.next() + r.next() - 1.0f) / 255.0f;   // TPDF dither: the cyclorama's gradient does not band
    params.output[pix] = make_uchar4((unsigned char)(saturate(srgbOetf(col.x) + dn) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(srgbOetf(col.y) + dn) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(srgbOetf(col.z) + dn) * 255.0f + 0.5f), 255);
}
