// Scene 3 "Yikim": OptiX device program, compiled at run time with NVRTC.
//   __raygen__render  jittered primary ray per sample; Lambert pieces and ground: sun with a ray-traced soft shadow,
//                     sky light from one cosine-sampled ray (sky, or the ground plane below the horizon, lit by the
//                     sun and the sky without occlusion: one cheap bounce). Accumulates; writes the tone-mapped image.
#include <optix.h>
#include "shared/yikim_params.h"
#include "shared/display.h"

extern "C" {
__constant__ YikimParams params;
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

static __forceinline__ __device__ float3 sky(float3 d) {
    const float t = sqrtf(fmaxf(d.y, 0.0f));
    return params.skyHorizon + (params.skyZenith - params.skyHorizon) * t;
}

struct Hit {
    bool hit;
    float t;
    float3 n;          // geometric, facing the ray
    unsigned int body; // instance index
};

static __forceinline__ __device__ float3 xformN(float3 n) { return optixHitObjectTransformNormalFromObjectToWorldSpace(n); }

static __forceinline__ __device__ Hit trace(float3 o, float3 d, float tmax) {
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(1u), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0);
    Hit h;
    h.hit = optixHitObjectIsHit();
    if (!h.hit) return h;
    h.body = optixHitObjectGetInstanceIndex();
    const unsigned int prim = optixHitObjectGetPrimitiveIndex();
    const YMesh m = params.meshes[h.body];
    const uint3 tri = m.idx[prim];
    const float3 v0 = m.pos[tri.x], v1 = m.pos[tri.y], v2 = m.pos[tri.z];
    h.n = normalize(xformN(cross(v1 - v0, v2 - v0)));
    if (dot(h.n, d) > 0.0f) h.n = -h.n;
    h.t = optixHitObjectGetRayTmax();
    return h;
}
static __forceinline__ __device__ bool visible(float3 o, float3 d, float tmax) {
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(1u),
                  OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, 0, 1, 0);
    return !optixHitObjectIsHit();
}

// a direction inside the sun's disc
static __forceinline__ __device__ float3 sunSample(Rng& r) {
    const float c = 1.0f - r.next() * (1.0f - params.sunCosMax), s = sqrtf(fmaxf(0.0f, 1.0f - c * c)), phi = 2.0f * M_PIf * r.next();
    const Frame f = Frame::from(params.sunDir);
    return f.toWorld(make_float3(s * cosf(phi), s * sinf(phi), c));
}

// the ground's albedo: concrete-grey yard with a 1 m grid and 5 m lines (scale and speed are readable in a clip)
static __forceinline__ __device__ float3 groundAlbedo(float3 p) {
    const float x = p.x - params.groundShift.x, z = p.z - params.groundShift.y;
    const float fx = fabsf(x - roundf(x)), fz = fabsf(z - roundf(z));
    const float f5x = fabsf(x * 0.2f - roundf(x * 0.2f)) * 5.0f, f5z = fabsf(z * 0.2f - roundf(z * 0.2f)) * 5.0f;
    float k = 1.0f;
    if (fx < 0.015f || fz < 0.015f) k = 0.82f;
    if (f5x < 0.03f || f5z < 0.03f) k = 0.7f;
    return params.groundAlbedo * k;
}

// radiance arriving along d from a surface point: the sky, or the open ground (sunlit, unoccluded: one cheap bounce)
static __forceinline__ __device__ float3 skyOrGround(float3 o, float3 d) {
    if (d.y < 0.0f && o.y > params.groundY) {
        const float t = (params.groundY - o.y) / d.y;
        const float3 p = o + d * t;
        const float3 E = params.sunE * fmaxf(params.sunDir.y, 0.0f) + (params.skyZenith + params.skyHorizon) * (0.5f * M_PIf);
        return groundAlbedo(p) * E * INV_PIf;
    }
    return sky(d);
}

static __forceinline__ __device__ float3 shade(float3 p, float3 n, float3 albedo, Rng& r) {
    const float3 o = p + n * 1e-3f;
    float3 L = make_float3(0, 0, 0);
    const float3 ls = sunSample(r);
    const float c = dot(n, ls);
    if (c > 0.0f && visible(o, ls, 1e30f)) L = L + params.sunE * (c * INV_PIf);
    const float3 d = sampleCosine(r.next(), r.next(), n);
    if (visible(o, d, 1e30f)) L = L + skyOrGround(o, d);   // cosine-sampled: E/pi = mean radiance
    return albedo * L;
}

extern "C" __global__ void __raygen__render() {
    const uint3 ix = optixGetLaunchIndex();
    const unsigned int px = ix.x, py = ix.y;
    const unsigned int pix = py * params.width + px;
    Rng r;
    r.s = hashU32(pix * 9781u + params.seed * 6271u + params.samples * 26699u);
    float3 sum = make_float3(0, 0, 0);
    const YCamera& C = params.cam;
    for (unsigned int s = 0; s < params.spp; ++s) {
        const float u = ((px + r.next()) / params.width) * 2.0f - 1.0f, v = 1.0f - ((py + r.next()) / params.height) * 2.0f;
        const float3 d = normalize(C.fwd + C.right * (u * C.tanHalfX) + C.up * (v * C.tanHalfY));
        const Hit h = trace(C.pos, d, 1e30f);
        const float tg = d.y < 0.0f ? (params.groundY - C.pos.y) / d.y : 1e30f;
        float3 L;
        if (h.hit && h.t < tg) {
            const YBody b = params.bodies[h.body];
            const float3 alb = b.albedo * (1.0f - params.damageTint * b.damage);
            L = shade(C.pos + d * h.t, h.n, alb, r);
        } else if (tg < 1e29f) {
            const float3 p = C.pos + d * tg;
            L = shade(p, make_float3(0, 1, 0), groundAlbedo(p), r);
            // haze towards the horizon (a hint of distance)
            const float f = 1.0f - expf(-tg * 0.0015f);
            L = L + (sky(make_float3(0, 0.05f, 1.0f)) - L) * f;
        } else {
            L = sky(d);
            if (dot(d, params.sunDir) > params.sunCosMax) L = L + params.sunE * (1.0f / (2.0f * M_PIf * (1.0f - params.sunCosMax)));
        }
        sum = sum + L;
    }
    float4 acc = params.samples > 0 ? params.accum[pix] : make_float4(0, 0, 0, 0);
    acc = acc + make_float4(sum.x, sum.y, sum.z, 0.0f);
    params.accum[pix] = acc;
    const float inv = 1.0f / float(params.samples + params.spp);
    float3 col = make_float3(acc.x, acc.y, acc.z) * (inv * params.exposure);
    col = agx(col);
    // TPDF dither so the sky does not band
    const float dn = (r.next() + r.next() - 1.0f) / 255.0f;
    params.output[pix] = make_uchar4((unsigned char)(saturate(srgbOetf(col.x) + dn) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(srgbOetf(col.y) + dn) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(srgbOetf(col.z) + dn) * 255.0f + 0.5f), 255);
}
