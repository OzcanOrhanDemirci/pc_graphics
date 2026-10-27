// Scene 11 "Orman": the OptiX programs (compiled at run time by NVRTC; F5 recompiles).
//   __raygen__render   every pixel: the same fixed sub-pixel samples every frame (the filter's own distribution, equal
//                      weights), each a visibility ray through the instances; its surface lit by the baked light (the
//                      probe grid) and the live sun (one shadow ray through the leaves)
//   __raygen__final    exposure, glare, AgX, sRGB, a fixed (not moving) dither
//   __raygen__bake     the probe grid, one slab: the sky, the sun and (from the second pass) the probes of the pass before,
//                      seen from every probe in fixed directions
//   __raygen__bakeAO   the ground's occlusion per vertex (the plants over it)
//   __raygen__bakeVis  the sun's visibility in the air, per voxel (the mist's shafts)
//   __anyhit__alpha    a leaf's alpha map cuts its triangles (the mip level from the ray's cone)
// Nothing here is random: every frame is the same frame from the same place (grafik netlik kuralı).
#include <optix.h>

#include "scenes/orman/orman_shared.h"
#include "shared/display.h"

extern "C" {
__constant__ OrmanParams params;
}

#define MASK_ALL 0xFFu

static __forceinline__ __device__ float3 xfPoint(const float m[12], float3 p) {
    return make_float3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                       m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
}
static __forceinline__ __device__ float3 xfVector(const float m[12], float3 v) {
    return make_float3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z, m[8] * v.x + m[9] * v.y + m[10] * v.z);
}
static __forceinline__ __device__ float3 lerp3(float3 a, float3 b, float3 c, float2 w) {
    return a * (1.0f - w.x - w.y) + b * w.x + c * w.y;
}
static __forceinline__ __device__ float2 lerp2(float2 a, float2 b, float2 c, float2 w) {
    return a * (1.0f - w.x - w.y) + b * w.x + c * w.y;
}
static __forceinline__ __device__ unsigned int hash32(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// the level of detail of a texture over a triangle for a cone of width w (world metres) at the hit: the ray cones of
// Akenine-Möller et al. (2019), with the triangle's own texel density
static __forceinline__ __device__ float coneLod(float3 p0, float3 p1, float3 p2, float2 t0, float2 t1, float2 t2, float scale,
                                                float texelArea, float width, float cosTheta) {
    const float worldArea = 0.5f * length(cross(p1 - p0, p2 - p0)) * scale * scale;
    const float2 a = t1 - t0, b = t2 - t0;
    const float uvArea = 0.5f * fabsf(a.x * b.y - a.y * b.x) * texelArea;
    if (worldArea <= 0.0f || uvArea <= 0.0f) return 0.0f;
    return 0.5f * log2f(uvArea / worldArea) + log2f(fmaxf(width, 1e-6f) / fmaxf(cosTheta, 0.2f));
}

// ---------------------------------------------------------------------------------------------------------------
// the alpha test
extern "C" __global__ void __anyhit__alpha() {
    const OInstance& in = params.instances[optixGetInstanceId()];
    const OGeom& g = params.geoms[params.protos[in.proto].firstGeom + optixGetSbtGASIndex()];
    const OMaterial& m = params.materials[g.material];
    if (!m.alpha) return;
    const uint3 tri = g.idx[optixGetPrimitiveIndex()];
    const float2 b = optixGetTriangleBarycentrics();
    const float2 t0 = g.uv[tri.x] * g.uvScale, t1 = g.uv[tri.y] * g.uvScale, t2 = g.uv[tri.z] * g.uvScale;
    const float2 uv = lerp2(t0, t1, t2, b);
    const float width = __uint_as_float(optixGetPayload_0()) + optixGetRayTmax() * __uint_as_float(optixGetPayload_1());
    const float lod = coneLod(g.pos[tri.x], g.pos[tri.y], g.pos[tri.z], t0, t1, t2, in.scale, m.texelArea, width, 1.0f);
    const float a = tex2DLod<float>(cudaTextureObject_t(m.alpha), uv.x, uv.y, fmaxf(lod, 0.0f));
    if (a < m.alphaCutoff) optixIgnoreIntersection();
}

// ---------------------------------------------------------------------------------------------------------------
struct Hit {
    bool hit;
    float t;
    unsigned int inst, prim, input;
    float2 b;
    float m[12];
};

static __forceinline__ __device__ Hit trace(float3 o, float3 d, float tmin, float tmax, float width0, float spread,
                                            unsigned int mask = OMASK_SOLID | OMASK_WATER) {
    unsigned int p0 = __float_as_uint(width0), p1 = __float_as_uint(spread);
    optixTraverse(params.handle, o, d, tmin, tmax, 0.0f, OptixVisibilityMask(mask), OPTIX_RAY_FLAG_NONE, 0, 1, 0, p0, p1);
    Hit h;
    h.hit = optixHitObjectIsHit();
    if (!h.hit) return h;
    h.t = optixHitObjectGetRayTmax();
    h.inst = optixHitObjectGetInstanceId();
    h.prim = optixHitObjectGetPrimitiveIndex();
    h.input = optixHitObjectGetSbtGASIndex();
    h.b = optixHitObjectGetTriangleBarycentrics();
    optixHitObjectGetObjectToWorldTransformMatrix(h.m);
    return h;
}

// true if nothing is in the way (leaves cut by their alpha: the dapples)
static __forceinline__ __device__ bool visible(float3 o, float3 d, float tmax, float width0) {
    unsigned int p0 = __float_as_uint(width0), p1 = __float_as_uint(0.0f);
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(OMASK_SOLID), OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, 0, 1, 0,
                  p0, p1);
    return !optixHitObjectIsHit();
}

struct Surf {
    float3 p, ng, n;          // the point, the geometric and the shading normal (both on the side the ray came from)
    float3 albedo;
    float rough, ao, trans;
    unsigned int flags;
    bool back;                // a single-sided surface seen from behind (inside a rock: the oven's validity)
    float lod;
};

static __forceinline__ __device__ Surf surface(const Hit& h, float3 o, float3 d, float width) {
    const OInstance& in = params.instances[h.inst];
    const OGeom& g = params.geoms[params.protos[in.proto].firstGeom + h.input];
    const OMaterial& m = params.materials[g.material];
    const uint3 tri = g.idx[h.prim];
    const float3 q0 = g.pos[tri.x], q1 = g.pos[tri.y], q2 = g.pos[tri.z];
    const float3 p0 = xfPoint(h.m, q0), p1 = xfPoint(h.m, q1), p2 = xfPoint(h.m, q2);
    Surf s;
    s.p = lerp3(p0, p1, p2, h.b);
    s.flags = m.flags;
    // (scans hold degenerate triangles: a zero cross product must not become a NaN that spreads)
    const float3 cr = cross(p1 - p0, p2 - p0);
    const float crl = length(cr);
    float3 n = g.nrm ? xfVector(h.m, lerp3(g.nrm[tri.x], g.nrm[tri.y], g.nrm[tri.z], h.b)) : cr;
    const float nl = length(n);
    n = nl > 1e-12f ? n * (1.0f / nl) : (crl > 1e-20f ? cr * (1.0f / crl) : -d);
    float3 ng = crl > 1e-20f ? cr * (1.0f / crl) : n;
    if (dot(n, ng) < 0.0f) ng = -ng;   // the winding against the normals: the normals win
    s.back = false;
    if (dot(ng, d) > 0.0f) {
        if (m.flags & OMAT_DOUBLE) {
            ng = -ng;
            n = -n;
        } else {
            s.back = true;
        }
    }
    const float2 t0 = g.uv[tri.x] * g.uvScale, t1 = g.uv[tri.y] * g.uvScale, t2 = g.uv[tri.z] * g.uvScale;
    const float2 uv = lerp2(t0, t1, t2, h.b);
    const float cosT = fabsf(dot(ng, d));
    s.lod = fmaxf(0.0f, coneLod(q0, q1, q2, t0, t1, t2, in.scale, m.texelArea, width, cosT));
    if (m.albedo) {
        const float4 a = tex2DLod<float4>(cudaTextureObject_t(m.albedo), uv.x, uv.y, s.lod);
        s.albedo = make_float3(a.x, a.y, a.z) * m.tint * in.tint;
    } else {
        s.albedo = m.tint * in.tint;
    }
    s.ao = 1.0f;
    s.rough = m.roughness;
    if (m.arm) {
        const float4 r = tex2DLod<float4>(cudaTextureObject_t(m.arm), uv.x, uv.y, s.lod);
        s.ao = r.x;
        s.rough = r.y;
    }
    // after the rain a film of water: the rough turns glossy (a wet stone, a leaf's sheen)
    s.rough += (fminf(s.rough, 0.3f) - s.rough) * params.wetness;
    if (g.ao) s.ao *= powf(fmaxf(lerp3(make_float3(g.ao[tri.x], 0, 0), make_float3(g.ao[tri.y], 0, 0), make_float3(g.ao[tri.z], 0, 0), h.b).x, 0.0f),
                           params.aoPower);
    if (m.normal) {
        // the tangent frame from the triangle's texture coordinates; the map's green is towards decreasing v (glTF's v
        // runs down the image, the map is OpenGL's)
        const float2 du1 = t1 - t0, du2 = t2 - t0;
        const float det = du1.x * du2.y - du1.y * du2.x;
        if (fabsf(det) > 1e-12f) {
            const float r = 1.0f / det;
            const float3 e1 = p1 - p0, e2 = p2 - p0;
            float3 T = (e1 * du2.y - e2 * du1.y) * r;
            float3 B = (e2 * du1.x - e1 * du2.x) * r;
            T = T - n * dot(n, T);
            const float tl = length(T);
            if (tl > 1e-12f) {
                T = T * (1.0f / tl);
                // the bitangent's handedness from the triangle (mirrored texture coordinates flip it)
                const float hand = dot(cross(n, T), B) < 0.0f ? -1.0f : 1.0f;
                const float3 Bn = cross(n, T) * hand;
                const float4 nm = tex2DLod<float4>(cudaTextureObject_t(m.normal), uv.x, uv.y, s.lod);
                const float3 tn = make_float3(nm.x * 2.0f - 1.0f, -(nm.y * 2.0f - 1.0f), nm.z * 2.0f - 1.0f);
                const float3 nn = normalize(T * tn.x + Bn * tn.y + n * fmaxf(tn.z, 0.05f));
                n = nn;
            }
        }
    }
    // a shading normal facing away from the eye shades black: bend it back to the horizon
    if (dot(n, d) > -0.02f) n = normalize(n - d * (dot(n, d) + 0.02f));
    s.n = n;
    s.ng = ng;
    s.trans = (m.flags & OMAT_FOLIAGE) ? m.translucency : 0.0f;
    return s;
}

static __forceinline__ __device__ float3 sky(float3 d) {
    if (d.y < 0.0f) return params.skyGround;
    const float t = powf(1.0f - d.y, 3.0f);
    float3 c = params.skyZenith * (1.0f - t) + params.skyHorizon * t;
    // the sun's halo through the morning haze
    const float cs = dot(d, params.sunDir);
    c += params.sunE * (0.02f * powf(fmaxf(cs, 0.0f), 64.0f) + 0.002f * powf(fmaxf(cs, 0.0f), 8.0f));
    return c;
}

// the baked light around p for a surface facing n: the eight probes' L1 radiance coefficients, weighted by their
// validity and by whether they are in front of the surface (a grid's probes with Majercik et al.'s weights, without
// their visibility maps)
struct SH {
    float3 L0;                // L00 per colour
    float3 Lr, Lg, Lb;        // L1 (x, y, z) per colour
    bool ok;
};
static __forceinline__ __device__ SH gridSH(float3 p, float3 n) {
    SH r;
    r.L0 = r.Lr = r.Lg = r.Lb = make_float3(0, 0, 0);
    r.ok = false;
    const OGrid& G = params.grid;
    if (!G.sh[0]) return r;
    const float3 q = (p + n * (0.35f * G.h) - G.origin) * (1.0f / G.h);
    const int ix = int(floorf(q.x)), iy = int(floorf(q.y)), iz = int(floorf(q.z));
    const float fx = q.x - ix, fy = q.y - iy, fz = q.z - iz;
    float wsum = 0.0f;
    for (int c = 0; c < 8; ++c) {
        const int x = ix + (c & 1), y = iy + ((c >> 1) & 1), z = iz + ((c >> 2) & 1);
        if (x < 0 || y < 0 || z < 0 || x >= G.nx || y >= G.ny || z >= G.nz) continue;
        const size_t k = (size_t(z) * G.ny + y) * G.nx + x;
        const float4 s0 = G.sh[0][k];
        if (s0.w <= 0.0f) continue;
        float w = ((c & 1) ? fx : 1.0f - fx) * (((c >> 1) & 1) ? fy : 1.0f - fy) * (((c >> 2) & 1) ? fz : 1.0f - fz);
        const float3 pp = G.origin + make_float3(float(x), float(y), float(z)) * G.h;
        const float3 dir = pp - p;
        const float dl = length(dir);
        const float facing = dl > 1e-4f ? (dot(dir * (1.0f / dl), n) + 1.0f) * 0.5f : 1.0f;
        w *= (facing * facing + 0.05f) * s0.w;
        if (w <= 1e-7f) continue;
        const float4 a = G.sh[1][k], g = G.sh[2][k], b = G.sh[3][k];
        r.L0 += make_float3(s0.x, s0.y, s0.z) * w;
        r.Lr += make_float3(a.x, a.y, a.z) * w;
        r.Lg += make_float3(g.x, g.y, g.z) * w;
        r.Lb += make_float3(b.x, b.y, b.z) * w;
        wsum += w;
    }
    if (wsum > 0.0f) {
        const float iw = 1.0f / wsum;
        r.L0 *= iw;
        r.Lr *= iw;
        r.Lg *= iw;
        r.Lb *= iw;
        r.ok = true;
    }
    return r;
}
// irradiance on a surface facing n: pi c0 L00 + (2 pi / 3) c1 (L1 . n)
static __forceinline__ __device__ float3 shE(const SH& h, float3 n) {
    const float a0 = 3.14159265f * 0.282095f, a1 = 2.0943951f * 0.488603f;
    return fmaxf3(make_float3(a0 * h.L0.x + a1 * dot(h.Lr, n), a0 * h.L0.y + a1 * dot(h.Lg, n), a0 * h.L0.z + a1 * dot(h.Lb, n)), make_float3(0, 0, 0));
}
// radiance arriving from direction w (L1 is a broad lobe: a glossy reflection, not a mirror)
static __forceinline__ __device__ float3 shL(const SH& h, float3 w) {
    const float c0 = 0.282095f, c1 = 0.488603f;
    return fmaxf3(make_float3(c0 * h.L0.x + c1 * dot(h.Lr, w), c0 * h.L0.y + c1 * dot(h.Lg, w), c0 * h.L0.z + c1 * dot(h.Lb, w)), make_float3(0, 0, 0));
}
static __forceinline__ __device__ float3 gridE(float3 p, float3 n) { return shE(gridSH(p, n), n); }

// the sun at s for the eye along d: diffuse and its sheen on a wet surface, and through a leaf from behind
static __forceinline__ __device__ float3 sunLight(const Surf& s, float3 d, int sample, bool trace) {
    const float3 L = params.sunDir;
    const float front = dot(s.ng, L);
    if (front <= 0.0f && s.trans <= 0.0f) return make_float3(0, 0, 0);
    float3 Ls = L;
    if (sample >= 0) {
        // the sun's disk: this sample's own fixed point on it (the half-shadows of the leaves)
        const float2 u = params.sunDisk[sample];
        const float r = sqrtf(fmaxf(0.0f, 1.0f - params.sunCos * params.sunCos)) / params.sunCos;
        const float3 a = normalize(fabsf(L.y) < 0.9f ? cross(L, make_float3(0, 1, 0)) : cross(L, make_float3(1, 0, 0)));
        const float3 b = cross(L, a);
        Ls = normalize(L + (a * u.x + b * u.y) * r);
    }
    const float3 side = front > 0.0f ? s.ng : -s.ng;
    if (trace && !visible(s.p + side * 2e-3f + Ls * 1e-3f, Ls, 1e4f, 0.002f)) return make_float3(0, 0, 0);
    if (front > 0.0f) {
        const float ndl = fmaxf(dot(s.n, Ls), 0.0f);
        float3 c = s.albedo * (INV_PIf * ndl * (1.0f - s.trans));
        // GGX specular, Schlick's Fresnel at 4%
        const float3 v = -d;
        const float3 hv = normalize(v + Ls);
        const float ndh = fmaxf(dot(s.n, hv), 0.0f), ndv = fmaxf(dot(s.n, v), 1e-3f);
        const float a = fmaxf(s.rough * s.rough, 0.02f), a2 = a * a;
        const float den = ndh * ndh * (a2 - 1.0f) + 1.0f;
        const float D = a2 / (3.14159265f * den * den);
        const float k = a * 0.5f;
        const float G = (ndl / (ndl * (1.0f - k) + k)) * (ndv / (ndv * (1.0f - k) + k));
        const float F = 0.04f + 0.96f * powf(1.0f - fmaxf(dot(v, hv), 0.0f), 5.0f);
        const float spec = D * G * F / (4.0f * ndv + 1e-4f);
        c += make_float3(spec, spec, spec);
        return c * params.sunE;
    }
    // from behind: through the leaf (its colour twice: in and out)
    const float ndl = fmaxf(-dot(s.n, Ls), 0.0f);
    return s.albedo * s.albedo * (INV_PIf * ndl * s.trans * 2.2f) * params.sunE;
}

static __forceinline__ __device__ float3 shade(const Surf& s, float3 d, int sample) {
    const int dbg = params.debug;
    if (dbg == 1) return s.albedo;
    if (dbg == 2) return s.n * 0.5f + make_float3(0.5f, 0.5f, 0.5f);
    if (dbg == 5) return make_float3(s.lod / 8.0f, 0.0f, 1.0f - s.lod / 8.0f);
    float3 c = make_float3(0, 0, 0);
    if (dbg != 4) {
        const SH h = gridSH(s.p, s.n);
        const float k = params.giScale * s.ao;
        c += s.albedo * shE(h, s.n) * (k * INV_PIf * (1.0f - s.trans));
        if (s.trans > 0.0f) c += s.albedo * s.albedo * shE(gridSH(s.p, -s.n), -s.n) * (k * INV_PIf * s.trans * 2.2f);
        // the wet sheen: the sky and the forest in the mirror direction, Schlick's Fresnel, fading with roughness
        const float3 v = -d;
        const float ndv = fmaxf(dot(s.n, v), 0.0f);
        const float3 r = s.n * (2.0f * dot(s.n, v)) - v;
        const float F = 0.04f + 0.96f * powf(1.0f - ndv, 5.0f);
        const float gloss = (1.0f - s.rough) * (1.0f - s.rough);
        c += shL(h, r) * (F * gloss * k * params.wetGloss);
    }
    if (dbg != 3) c += sunLight(s, d, sample, true);
    return c;
}

// the mist along a ray from o for tEnd metres: the light it scatters towards the eye (the sun's, where the baked volume
// says the sun reaches, and the forest's own, from the nearest probe) and what it lets through. Steps crowd near the
// eye (t ~ u^2); each of the pixel's samples takes its own place inside the steps: together they fill the gaps
static __forceinline__ __device__ float3 mist(float3 o, float3 d, float tEnd, int sample, float& T) {
    T = 1.0f;
    float3 acc = make_float3(0, 0, 0);
    if (params.fogSteps <= 0 || (params.fogDensity <= 0.0f && params.fogGround <= 0.0f)) return acc;
    const float L = fminf(tEnd, params.fogFar);
    const int N = params.fogSteps;
    const float g = params.fogG, cs = dot(d, params.sunDir);
    const float phase = (1.0f - g * g) / (4.0f * 3.14159265f * powf(fmaxf(1.0f + g * g - 2.0f * g * cs, 1e-4f), 1.5f));
    const float f = (float(sample) + 0.5f) / float(params.spp);
    const OGrid& G = params.grid;
    for (int i = 0; i < N; ++i) {
        const float u0 = float(i) / N, u1 = float(i + 1) / N;
        const float t0 = L * u0 * u0, t1 = L * u1 * u1;
        const float t = t0 + (t1 - t0) * f, dt = t1 - t0;
        const float3 p = o + d * t;
        const float sigma = params.fogDensity + params.fogGround * expf(-fmaxf(p.y, 0.0f) / params.fogHeight);
        float vis = 1.0f;
        if (params.sunVis) {
            const float3 q = (p - params.sunVisOrigin) * params.sunVisInvSize;
            vis = tex3D<float>(cudaTextureObject_t(params.sunVis), q.x, q.y, q.z);
        }
        float3 amb = make_float3(0, 0, 0);
        if (G.sh[0]) {
            const int x = min(max(int(roundf((p.x - G.origin.x) / G.h)), 0), G.nx - 1);
            const int y = min(max(int(roundf((p.y - G.origin.y) / G.h)), 0), G.ny - 1);
            const int z = min(max(int(roundf((p.z - G.origin.z) / G.h)), 0), G.nz - 1);
            const float4 s0 = G.sh[0][(size_t(z) * G.ny + y) * G.nx + x];
            amb = make_float3(s0.x, s0.y, s0.z) * (0.282095f * s0.w * params.fogAmbient);
        }
        const float a = expf(-sigma * dt);
        // the light scattered in this step, times what is left of it on the way to the eye (exact for a constant step)
        acc += (params.sunE * (phase * vis) + amb) * (T * (1.0f - a));
        T *= a;
    }
    return acc;
}

// what a ray off the water sees: the sky, or a surface shaded as any other (no second water)
static __forceinline__ __device__ float3 seen(float3 o, float3 d, float width0, float spread, int sample, float& t) {
    const Hit h = trace(o, d, 0.002f, 1e4f, width0, spread, OMASK_SOLID);
    if (!h.hit) {
        t = 1e4f;
        return sky(d);
    }
    t = h.t;
    const Surf s = surface(h, o, d, width0 + h.t * spread);
    return shade(s, d, sample);
}

// the stream: its surface rippled by the flow (a few waves drifting down the stream, the same every frame at the same
// time), the forest mirrored by Fresnel's share, the bed through the water's tea colour, the sun's glint
static __forceinline__ __device__ float3 water(const Surf& s, float3 d, float t, float spread, int sample) {
    const float tt = params.time * params.waterFlow;
    float dx = 0.0f, dz = 0.0f;
    const float waves[5][4] = {{9.0f, 0.3f, 0.0035f, 0.0f}, {14.0f, -0.6f, 0.0022f, 1.3f}, {23.0f, 0.9f, 0.0013f, 2.1f}, {37.0f, -0.2f, 0.0008f, 0.7f},
                               {61.0f, 0.5f, 0.0004f, 3.3f}};
    for (int i = 0; i < 5; ++i) {
        const float k = waves[i][0], ang = waves[i][1], amp = waves[i][2];
        const float kx = k * cosf(ang), kz = k * sinf(ang);
        const float ph = kx * (s.p.x - tt) + kz * s.p.z + waves[i][3];
        const float c = cosf(ph) * amp;
        dx += kx * c;
        dz += kz * c;
    }
    const float3 n = normalize(make_float3(-dx, 1.0f, -dz));
    const float3 v = -d;
    const float cosi = fmaxf(dot(n, v), 1e-3f);
    const float F = 0.02f + 0.98f * powf(1.0f - cosi, 5.0f);
    const float width0 = t * spread;
    const float3 r = n * (2.0f * dot(n, v)) - v;
    float tr;
    float3 c = seen(s.p + n * 0.002f, r.y > 0.01f ? r : normalize(make_float3(r.x, 0.01f, r.z)), width0, spread, sample, tr) * F;
    // through it: Snell's window, then the tea-coloured water to the bed
    const float eta = 1.0f / 1.333f;
    const float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
    if (k > 0.0f) {
        const float3 tdir = normalize(d * eta + n * (eta * cosi - sqrtf(k)));
        float tb;
        const float3 bed = seen(s.p - n * 0.002f, tdir, width0, spread, sample, tb);
        const float3 att = make_float3(expf(-params.waterAbsorb.x * tb), expf(-params.waterAbsorb.y * tb), expf(-params.waterAbsorb.z * tb));
        c += bed * att * (1.0f - F);
    }
    // the sun's glint on the ripples
    const float3 L = params.sunDir;
    if (dot(n, L) > 0.0f && visible(s.p + n * 0.003f, L, 1e4f, 0.002f)) {
        const float3 hv = normalize(v + L);
        const float a2 = 0.004f;
        const float ndh = fmaxf(dot(n, hv), 0.0f);
        const float den = ndh * ndh * (a2 - 1.0f) + 1.0f;
        const float D = a2 / (3.14159265f * den * den);
        c += params.sunE * (D * F * 0.25f / cosi) * fmaxf(dot(n, L), 0.0f);
    }
    return c;
}

// ---------------------------------------------------------------------------------------------------------------
extern "C" __global__ void __raygen__render() {
    const uint3 li = optixGetLaunchIndex();
    const unsigned int px = li.x, py = li.y;
    if (params.layout != sizeof(OrmanParams)) {
        params.hdr[py * params.width + px] = make_float4(1, 0, 1, 1);
        return;
    }
    const float spread = params.pixelAngle * rsqrtf(float(params.spp));
    float3 acc = make_float3(0, 0, 0);
    for (int k = 0; k < params.spp; ++k) {
        const float2 o = params.pattern[k];
        const float x = ((float(px) + 0.5f + o.x) / float(params.width)) * 2.0f - 1.0f;
        const float y = 1.0f - ((float(py) + 0.5f + o.y) / float(params.height)) * 2.0f;
        const float3 d = normalize(params.W + params.U * x + params.V * y);
        const Hit h = trace(params.eye, d, 0.02f, 1e4f, 0.0f, spread);
        float T;
        const float3 fog = mist(params.eye, d, h.hit ? h.t : 1e4f, k, T);
        if (!h.hit) {
            acc += sky(d) * T + fog;
            continue;
        }
        const Surf s = surface(h, params.eye, d, h.t * spread);
        const float3 c = ((s.flags & OMAT_WATER) ? water(s, d, h.t, spread, k) : shade(s, d, k)) * T + fog;
        // a sample that is not finite would spread over the glare pyramid as a black square: dropped and counted
        if (isfinite(c.x) && isfinite(c.y) && isfinite(c.z)) acc += c;
        else if (params.badCount) atomicAdd(params.badCount, 1u);
    }
    acc *= 1.0f / float(params.spp);
    if (!(isfinite(acc.x) && isfinite(acc.y) && isfinite(acc.z))) {
        acc = make_float3(0, 0, 0);
        if (params.badCount) atomicAdd(params.badCount, 1u << 16);   // (whole pixels: counted in the high half)
    }
    params.hdr[py * params.width + px] = make_float4(acc.x, acc.y, acc.z, 1.0f);
}

extern "C" __global__ void __raygen__final() {
    const uint3 li = optixGetLaunchIndex();
    const unsigned int px = li.x, py = li.y, i = py * params.width + px;
    const float4 h = params.hdr[i];
    float3 c = make_float3(h.x, h.y, h.z);
    if (params.bloom) {
        // the glare pyramid is half size: bilinear by hand (it is a plain buffer)
        const int bw = int(params.width / 2), bh = int(params.height / 2);
        const float fx = fminf(fmaxf((float(px) + 0.5f) * 0.5f - 0.5f, 0.0f), float(bw - 1)), fy = fminf(fmaxf((float(py) + 0.5f) * 0.5f - 0.5f, 0.0f), float(bh - 1));
        const int x0 = int(fx), y0 = int(fy), x1 = min(x0 + 1, bw - 1), y1 = min(y0 + 1, bh - 1);
        const float ax = fx - x0, ay = fy - y0;
        const float4 b = params.bloom[y0 * bw + x0] * ((1 - ax) * (1 - ay)) + params.bloom[y0 * bw + x1] * (ax * (1 - ay)) +
                         params.bloom[y1 * bw + x0] * ((1 - ax) * ay) + params.bloom[y1 * bw + x1] * (ax * ay);
        c = c * (1.0f - params.bloomStrength) + make_float3(b.x, b.y, b.z) * params.bloomStrength;
    }
    c *= params.exposure;
    if (params.debug == 1 || params.debug == 2 || params.debug == 5) c = c * (1.0f / params.exposure);
    else {
        const float u = (float(px) + 0.5f) / params.width - 0.5f, v = (float(py) + 0.5f) / params.height - 0.5f;
        const float r2 = (u * u + v * v) * 2.0f;
        c *= 1.0f - params.vignette * r2 * r2 * 0.5f - params.vignette * r2 * 0.5f;
        c = agx(c);
        const float Y = luminance709(c);
        c = fmaxf3(make_float3(Y, Y, Y) + (c - make_float3(Y, Y, Y)) * params.saturation, make_float3(0, 0, 0));
    }
    // a fixed dither against banding in the dark understory (the same every frame: it does not crawl)
    const float dz = (float(hash32(px * 1973u + py * 9277u) & 1023u) / 1023.0f - 0.5f) / 255.0f;
    params.output[i] = make_uchar4((unsigned char)(srgbOetf(c.x + dz) * 255.0f + 0.5f), (unsigned char)(srgbOetf(c.y + dz) * 255.0f + 0.5f),
                                   (unsigned char)(srgbOetf(c.z + dz) * 255.0f + 0.5f), 255);
}

// ---------------------------------------------------------------------------------------------------------------
// the oven

// a fixed sphere of directions (Fibonacci), turned per probe (a hash of its index) so neighbours do not share their gaps
static __forceinline__ __device__ float3 sphereDir(int i, int n, unsigned int seed) {
    const float z = 1.0f - (2.0f * float(i) + 1.0f) / float(n);
    const float r = sqrtf(fmaxf(0.0f, 1.0f - z * z));
    const float phi = 2.39996323f * float(i) + float(seed & 0xFFFFu) * (6.2831853f / 65536.0f);
    return make_float3(r * cosf(phi), z, r * sinf(phi));
}

extern "C" __global__ void __raygen__bake() {
    const uint3 li = optixGetLaunchIndex();
    const OGrid& G = params.grid;
    const int x = int(li.x), y = int(li.y), z = params.bakeZ0 + int(li.z);
    if (x >= G.nx || y >= G.ny || z >= G.nz) return;
    const size_t k = (size_t(z) * G.ny + y) * G.nx + x;
    const float3 p = G.origin + make_float3(float(x), float(y), float(z)) * G.h;
    const unsigned int seed = hash32(unsigned(k) * 2654435761u + 17u);
    // the turn: a random rotation about y and a tilt, fixed for the probe
    const int n = params.bakeRays;
    float3 L00 = make_float3(0, 0, 0), L1r = make_float3(0, 0, 0), L1g = make_float3(0, 0, 0), L1b = make_float3(0, 0, 0);
    int backs = 0, hits = 0;
    for (int i = 0; i < n; ++i) {
        float3 d = sphereDir(i, n, seed);
        // tilt the whole sphere a little (the poles not always on y)
        const float tilt = float((seed >> 16) & 255u) * (0.5f / 255.0f);
        d = normalize(make_float3(d.x, d.y * cosf(tilt) - d.z * sinf(tilt), d.y * sinf(tilt) + d.z * cosf(tilt)));
        const Hit h = trace(p, d, 0.0f, 1e4f, 0.01f, 0.0f, OMASK_SOLID);
        float3 Lr;
        if (!h.hit) {
            Lr = sky(d);
        } else {
            ++hits;
            const Surf s = surface(h, p, d, 0.01f + h.t * 0.05f);
            if (s.back) {
                ++backs;
                Lr = make_float3(0, 0, 0);
            } else {
                Lr = sunLight(s, d, -1, true);
                if (params.bakePass > 0) {
                    const float3 E = gridE(s.p, s.n) * s.ao;
                    Lr += s.albedo * E * (INV_PIf * (1.0f - s.trans));
                    if (s.trans > 0.0f) Lr += s.albedo * s.albedo * gridE(s.p, -s.n) * (s.ao * INV_PIf * s.trans * 2.2f);
                }
            }
        }
        if (!(isfinite(Lr.x) && isfinite(Lr.y) && isfinite(Lr.z))) Lr = make_float3(0, 0, 0);
        // radiance SH: c0 = 0.282095, c1 = 0.488603 (x, y, z), uniform over the sphere (weight 4 pi / n)
        L00 += Lr * 0.282095f;
        L1r += d * (Lr.x * 0.488603f);
        L1g += d * (Lr.y * 0.488603f);
        L1b += d * (Lr.z * 0.488603f);
    }
    const float w = 4.0f * 3.14159265f / float(n);
    // inside a rock or a trunk (or under the ground): most of its rays meet a back face
    const float bf = float(backs) / float(n);
    const float valid = fminf(fmaxf(1.0f - (bf - 0.08f) / 0.12f, 0.0f), 1.0f);
    params.bakeOut[0][k] = make_float4(L00.x * w, L00.y * w, L00.z * w, valid);
    params.bakeOut[1][k] = make_float4(L1r.x * w, L1r.y * w, L1r.z * w, 0.0f);
    params.bakeOut[2][k] = make_float4(L1g.x * w, L1g.y * w, L1g.z * w, 0.0f);
    params.bakeOut[3][k] = make_float4(L1b.x * w, L1b.y * w, L1b.z * w, 0.0f);
}

extern "C" __global__ void __raygen__bakeAO() {
    const uint3 li = optixGetLaunchIndex();
    const unsigned int v = li.x + li.y * 65536u;
    if (v >= params.aoCount) return;
    const float3 p = params.aoPos[v], n = params.aoNrm[v];
    const float3 a = normalize(fabsf(n.y) < 0.9f ? cross(n, make_float3(0, 1, 0)) : cross(n, make_float3(1, 0, 0)));
    const float3 b = cross(n, a);
    const unsigned int seed = hash32(v * 747796405u + 1u);
    const int N = params.aoRays;
    float occ = 0.0f;
    for (int i = 0; i < N; ++i) {
        // cosine-weighted, Fibonacci on the disk, turned per vertex
        const float r = sqrtf((float(i) + 0.5f) / float(N));
        const float phi = 2.39996323f * float(i) + float(seed & 0xFFFFu) * (6.2831853f / 65536.0f);
        const float3 d = normalize(a * (r * cosf(phi)) + b * (r * sinf(phi)) + n * sqrtf(fmaxf(0.0f, 1.0f - r * r)));
        unsigned int p0 = __float_as_uint(0.003f), p1 = __float_as_uint(0.02f);
        optixTraverse(params.handle, p + n * 0.01f, d, 0.0f, params.aoRadius, 0.0f, OptixVisibilityMask(OMASK_SOLID), OPTIX_RAY_FLAG_NONE, 0, 1,
                      0, p0, p1);
        if (optixHitObjectIsHit()) {
            const float t = optixHitObjectGetRayTmax() / params.aoRadius;
            occ += 1.0f - t * t;   // near things darken more than far ones
        }
    }
    params.aoOut[v] = 1.0f - occ / float(N);
}

// the sun's visibility in the air, per voxel: four shadow rays to the disk (0, 1/4 .. 1): the mist's shafts
extern "C" __global__ void __raygen__bakeVis() {
    const uint3 li = optixGetLaunchIndex();
    const int x = int(li.x), y = int(li.y), z = params.visZ0 + int(li.z);
    if (x >= params.visN[0] || y >= params.visN[1] || z >= params.visN[2]) return;
    const float3 p = params.sunVisOrigin + make_float3(float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f) * params.visH;
    const float3 L = params.sunDir;
    const float3 a = normalize(fabsf(L.y) < 0.9f ? cross(L, make_float3(0, 1, 0)) : cross(L, make_float3(1, 0, 0)));
    const float3 b = cross(L, a);
    const float r = sqrtf(fmaxf(0.0f, 1.0f - params.sunCos * params.sunCos)) / params.sunCos;
    const float2 disk[4] = {{0.5f, 0.2f}, {-0.2f, 0.5f}, {-0.5f, -0.2f}, {0.2f, -0.5f}};
    int seen = 0;
    for (int k = 0; k < 4; ++k) {
        const float3 Ls = normalize(L + (a * disk[k].x + b * disk[k].y) * r);
        if (visible(p, Ls, 1e4f, 0.01f)) ++seen;
    }
    params.visOut[(size_t(z) * params.visN[1] + y) * params.visN[0] + x] = (unsigned char)(seen * 255 / 4);
}
