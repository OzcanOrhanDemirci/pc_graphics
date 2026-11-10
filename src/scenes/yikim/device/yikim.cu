// Scene 3 "Yikim": OptiX device program, compiled at run time with NVRTC.
//   __raygen__render  jittered primary ray per sample; Lambert pieces and ground: sun with a ray-traced soft shadow,
//                     sky light from one cosine-sampled ray (sky, or the ground plane below the horizon, lit by the
//                     sun and the sky without occlusion: one cheap bounce). Accumulates; writes the tone-mapped image,
//                     and the denoiser's guides: first-hit albedo and normal, and where each pixel's point was a frame ago.
//   __raygen__tonemap the denoised image to the display (AgX, sRGB, dither)
#include <optix.h>
#include "scenes/yikim/yikim_params.h"
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
    h.t = optixHitObjectGetRayTmax();
    if (h.body < params.barInstance && params.bodies[h.body].sphere > 0.0f) {   // a ball: round, not faceted
        const float* c = params.xfCur + size_t(h.body) * params.xfStride;
        h.n = normalize(o + d * h.t - make_float3(c[3], c[7], c[11]));
    } else {
        h.n = normalize(xformN(cross(v1 - v0, v2 - v0)));
    }
    if (dot(h.n, d) > 0.0f) h.n = -h.n;
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

// glass: a dark tint under a mirror reflection, its share by Schlick's Fresnel with a coated pane's F0 (a curtain wall
// reflects: that is its look). The reflection is traced once: another piece or the ground, lit as usual, or the sky
// and the sun's disc.
#define GLASS_F0 0.25f
static __forceinline__ __device__ float3 glassShade(float3 p, float3 n, float3 d, float3 tint, Rng& r) {
    const float c = fminf(1.0f, fmaxf(0.0f, -dot(d, n))), m = 1.0f - c, m2 = m * m;
    const float F = GLASS_F0 + (1.0f - GLASS_F0) * m2 * m2 * m;
    const float3 rd = d - n * (2.0f * dot(d, n));
    const float3 o = p + n * 1e-3f;
    float3 R;
    const Hit h = trace(o, rd, 1e30f);
    const float tg = rd.y < 0.0f ? (params.groundY - o.y) / rd.y : 1e30f;
    if (h.hit && h.t < tg) {
        const YBody b = params.bodies[h.body];
        R = shade(o + rd * h.t, h.n, b.albedo * (1.0f - params.damageTint * b.damage), r);
    } else if (tg < 1e29f) {
        const float3 q = o + rd * tg;
        R = shade(q, make_float3(0, 1, 0), groundAlbedo(q), r);
    } else {
        R = sky(rd);
        if (dot(rd, params.sunDir) > params.sunCosMax) R = R + params.sunE * (1.0f / (2.0f * M_PIf * (1.0f - params.sunCosMax)));
    }
    return shade(p, n, tint, r) * (1.0f - F) + R * F;
}

// the body under a pixel (the window's mouse): one thread
extern "C" __global__ void __raygen__pick() {
    const YCamera& C = params.cam;
    const float u = (params.pickPixel.x / params.width) * 2.0f - 1.0f, v = 1.0f - (params.pickPixel.y / params.height) * 2.0f;
    const float3 d = normalize(C.fwd + C.right * (u * C.tanHalfX) + C.up * (v * C.tanHalfY));
    const Hit h = trace(C.pos, d, 1e30f);
    const float tg = d.y < 0.0f ? (params.groundY - C.pos.y) / d.y : 1e30f;
    int body = -1;
    float t = 1e30f;
    if (h.hit && h.t < tg) { body = int(h.body) + 1; t = h.t; }
    else if (tg < 1e29f) { body = 0; t = tg; }
    *params.pickBody = body;
    const float3 p = C.pos + d * (t < 1e29f ? t : 0.0f);
    *params.pickHit = make_float4(p.x, p.y, p.z, t);
}

// where a world point was on the previous frame's screen (render pixels); far behind the camera: no match
static __forceinline__ __device__ float2 projectPrev(float3 p) {
    const YCamera& c = params.prevCam;
    const float3 v = p - c.pos;
    const float z = dot(v, c.fwd);
    if (z < 1e-4f) return make_float2(-1e6f, -1e6f);
    const float u = dot(v, c.right) / (z * c.tanHalfX), w = dot(v, c.up) / (z * c.tanHalfY);
    return make_float2((u + 1.0f) * 0.5f * params.width, (1.0f - w) * 0.5f * params.height);
}

// the same point of body instance k a frame ago: into the body frame with this frame's transform, out with the last
static __forceinline__ __device__ float3 prevPoint(unsigned int k, float3 p) {
    const float* c = params.xfCur + size_t(k) * params.xfStride;
    const float* q = params.xfPrev + size_t(k) * 12;
    const float3 r = p - make_float3(c[3], c[7], c[11]);
    const float3 l = make_float3(c[0] * r.x + c[4] * r.y + c[8] * r.z, c[1] * r.x + c[5] * r.y + c[9] * r.z, c[2] * r.x + c[6] * r.y + c[10] * r.z);
    return make_float3(q[0] * l.x + q[1] * l.y + q[2] * l.z + q[3], q[4] * l.x + q[5] * l.y + q[6] * l.z + q[7], q[8] * l.x + q[9] * l.y + q[10] * l.z + q[11]);
}

extern "C" __global__ void __raygen__render() {
    const uint3 ix = optixGetLaunchIndex();
    const unsigned int px = ix.x, py = ix.y;
    const unsigned int pix = py * params.width + px;
    Rng r;
    r.s = hashU32(pix * 9781u + params.seed * 6271u + params.samples * 26699u);
    float3 sum = make_float3(0, 0, 0), sumAlb = make_float3(0, 0, 0), sumN = make_float3(0, 0, 0);
    const YCamera& C = params.cam;
    for (unsigned int s = 0; s < params.spp; ++s) {
        // the first sample goes through the pixel centre: the guides (what is seen, where it was) stay put from frame to frame
        const float jx = s == 0 ? 0.5f : r.next(), jy = s == 0 ? 0.5f : r.next();
        const float u = ((px + jx) / params.width) * 2.0f - 1.0f, v = 1.0f - ((py + jy) / params.height) * 2.0f;
        const float3 d = normalize(C.fwd + C.right * (u * C.tanHalfX) + C.up * (v * C.tanHalfY));
        const Hit h = trace(C.pos, d, 1e30f);
        const float tg = d.y < 0.0f ? (params.groundY - C.pos.y) / d.y : 1e30f;
        float3 L, alb, n = make_float3(0, 0, 0);
        float3 pp = make_float3(0, 0, 0);   // first sample only: this point a frame ago (the denoisers' motion guide)
        float dist = 1e9f, seen = -1.0f;
        bool dir = false;
        if (h.hit && h.t < tg) {
            const YBody b = params.bodies[h.body];
            alb = b.albedo * (1.0f - params.damageTint * b.damage);
            const float3 p = C.pos + d * h.t;
            L = b.glass > 0.0f ? glassShade(p, h.n, d, alb, r) : shade(p, h.n, alb, r);
            n = h.n;
            if (s == 0) { pp = h.body < params.barInstance ? prevPoint(h.body, p) : p; dist = h.t; seen = float(h.body) + 1.0f; }
        } else if (tg < 1e29f) {
            const float3 p = C.pos + d * tg;
            alb = groundAlbedo(p);
            L = shade(p, make_float3(0, 1, 0), alb, r);
            n = make_float3(0, 1, 0);
            // haze towards the horizon (a hint of distance)
            const float f = 1.0f - expf(-tg * 0.0015f);
            L = L + (sky(make_float3(0, 0.05f, 1.0f)) - L) * f;
            if (s == 0) {   // the ground rides the earthquake
                pp = p - make_float3(params.groundShift.x - params.groundShiftPrev.x, params.groundY - params.groundYPrev,
                                     params.groundShift.y - params.groundShiftPrev.y);
                dist = tg;
                seen = 0.0f;
            }
        } else {
            L = sky(d);
            alb = make_float3(fminf(L.x, 1.0f), fminf(L.y, 1.0f), fminf(L.z, 1.0f));
            if (dot(d, params.sunDir) > params.sunCosMax) L = L + params.sunE * (1.0f / (2.0f * M_PIf * (1.0f - params.sunCosMax)));
            if (s == 0) { pp = params.prevCam.pos + d; dir = true; }   // a direction: only the camera's turn moves it
        }
        if (s == 0 && params.motion) {
            const float2 prev = projectPrev(pp);
            const float3 dv = pp - params.prevCam.pos;
            params.motion[pix] = make_float4(px + jx - prev.x, py + jy - prev.y, dir ? 1e9f : sqrtf(dot(dv, dv)), 0.0f);
            params.gbuf[pix] = make_float4(dist, seen, 0.0f, 0.0f);
        }
        sum = sum + L;
        sumAlb = sumAlb + alb;
        sumN = sumN + n;
    }
    float4 acc = params.samples > 0 ? params.accum[pix] : make_float4(0, 0, 0, 0);
    acc = acc + make_float4(sum.x, sum.y, sum.z, 0.0f);
    params.accum[pix] = acc;
    const float4 albedo = (params.samples > 0 ? params.albedoAcc[pix] : make_float4(0, 0, 0, 0)) +
                          make_float4(sumAlb.x, sumAlb.y, sumAlb.z, 0.0f);
    const float4 normal = (params.samples > 0 ? params.normalAcc[pix] : make_float4(0, 0, 0, 0)) +
                          make_float4(sumN.x, sumN.y, sumN.z, 0.0f);
    params.albedoAcc[pix] = albedo;
    params.normalAcc[pix] = normal;
    const float inv = 1.0f / float(max(params.samples + params.spp, 1u));
    if (params.denoised) {
        // Give OptiX the means and camera-space normal while the sums are still in registers.
        params.dnIn[pix] = make_float4(acc.x * inv, acc.y * inv, acc.z * inv, 1.0f);
        params.dnAlbedo[pix] = make_float4(albedo.x * inv, albedo.y * inv, albedo.z * inv, 1.0f);
        const float3 nm = make_float3(normal.x, normal.y, normal.z);
        const float l = sqrtf(dot(nm, nm));
        const float3 nw = l > 1e-6f ? nm * (1.0f / l) : make_float3(0, 0, 0);
        params.dnNormal[pix] = make_float4(dot(nw, C.right), dot(nw, C.up), -dot(nw, C.fwd), 0.0f);
        return;
    }
    float3 col = make_float3(acc.x, acc.y, acc.z) * (inv * params.exposure);
    col = agx(col);
    // TPDF dither so the sky does not band
    const float dn = (r.next() + r.next() - 1.0f) / 255.0f;
    params.output[pix] = make_uchar4((unsigned char)(saturate(srgbOetf(col.x) + dn) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(srgbOetf(col.y) + dn) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(srgbOetf(col.z) + dn) * 255.0f + 0.5f), 255);
}

// the denoised image to the display, as __raygen__render does with the raw one
extern "C" __global__ void __raygen__tonemap() {
    const uint3 ix = optixGetLaunchIndex();
    const unsigned int pix = ix.y * params.width + ix.x;
    Rng r;
    r.s = hashU32(pix * 7919u + params.seed * 104729u + 17u);
    const float4 c = params.denoised[pix];
    float3 col = make_float3(fmaxf(c.x, 0.0f), fmaxf(c.y, 0.0f), fmaxf(c.z, 0.0f)) * params.exposure;
    col = agx(col);
    const float dn = (r.next() + r.next() - 1.0f) / 255.0f;
    params.output[pix] = make_uchar4((unsigned char)(saturate(srgbOetf(col.x) + dn) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(srgbOetf(col.y) + dn) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(srgbOetf(col.z) + dn) * 255.0f + 0.5f), 255);
}
