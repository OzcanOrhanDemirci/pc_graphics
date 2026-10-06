// Scene 8 · the oven (OptiX, compiled at run time by NVRTC). One thread per texel of a batch: `samples` paths each,
// the light arriving at the texel projected onto L1 spherical harmonics (its amount and the direction it comes from).
//   direct light: next-event estimation to the lamps (spheres), shadow rays through the room (cages cast shadows)
//   the rest: cosine-weighted paths, every bounce lit by the lamps again, Russian roulette after the third
// Deterministic: the sequence is a hash of the texel's global index and the sample's; no state anywhere.
#include <optix.h>

#include "scenes/denizalti/dz_bake_shared.h"

extern "C" {
__constant__ DzBakeParams params;
}

static __forceinline__ __device__ unsigned int pcg(unsigned int v) {
    unsigned int s = v * 747796405u + 2891336453u;
    unsigned int w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
struct Rng {
    unsigned int s;
    __device__ float next() {
        s = pcg(s);
        return float(s >> 8) * (1.0f / 16777216.0f);
    }
};

static __forceinline__ __device__ void frame(float3 n, float3& t, float3& b) {
    const float sgn = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (sgn + n.z), c = n.x * n.y * a;
    t = make_float3(1.0f + sgn * n.x * n.x * a, sgn * c, -sgn * n.x);
    b = make_float3(c, sgn + n.y * n.y * a, -n.y);
}
static __forceinline__ __device__ float3 cosineDir(float3 n, float u1, float u2) {
    float3 t, b;
    frame(n, t, b);
    const float r = sqrtf(u1), ph = 2.0f * M_PIf * u2;
    return normalize(t * (r * cosf(ph)) + b * (r * sinf(ph)) + n * sqrtf(fmaxf(0.0f, 1.0f - u1)));
}

struct Hit {
    float t;
    float3 p, n, ng;
    unsigned int mat;
    bool front;
};

static __forceinline__ __device__ bool trace(float3 o, float3 d, float tmax, Hit& h) {
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(1u), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0);
    if (!optixHitObjectIsHit()) return false;
    const unsigned int prim = optixHitObjectGetPrimitiveIndex();
    const float2 bc = optixHitObjectGetTriangleBarycentrics();
    h.t = optixHitObjectGetRayTmax();
    const uint3 tri = params.idx[prim];
    float3 v[3];
    optixHitObjectGetTriangleVertexData(v);
    h.ng = normalize(cross(v[1] - v[0], v[2] - v[0]));
    const float4 n0 = params.vtxN[tri.x], n1 = params.vtxN[tri.y], n2 = params.vtxN[tri.z];
    const float w0 = 1.0f - bc.x - bc.y;
    h.n = normalize(make_float3(n0.x * w0 + n1.x * bc.x + n2.x * bc.y, n0.y * w0 + n1.y * bc.x + n2.y * bc.y,
                                n0.z * w0 + n1.z * bc.x + n2.z * bc.y));
    h.p = o + d * h.t;
    h.mat = params.triMat[prim];
    h.front = dot(d, h.ng) < 0.0f;
    return true;
}

static __forceinline__ __device__ bool visible(float3 o, float3 d, float tmax) {
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(1u),
                  OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, 0, 1, 0);
    return !optixHitObjectIsHit();
}

// one lamp sample from point x: direction, radiance, solid-angle pdf; false if the lamp is behind or occluded
static __forceinline__ __device__ bool sampleLamp(const DzLight& l, float3 x, float u1, float u2, float3& w, float3& L, float& pdf, float& dist) {
    const float3 dc = l.p - x;
    const float d2 = dot(dc, dc);
    if (d2 <= l.r * l.r * 1.0001f) return false;
    const float dl = sqrtf(d2);
    const float3 wc = dc / dl;
    const float sinMax = l.r / dl, cosMax = sqrtf(fmaxf(0.0f, 1.0f - sinMax * sinMax));
    const float cosT = 1.0f - u1 * (1.0f - cosMax), sinT = sqrtf(fmaxf(0.0f, 1.0f - cosT * cosT));
    const float ph = 2.0f * M_PIf * u2;
    float3 t, b;
    frame(wc, t, b);
    w = normalize(t * (sinT * cosf(ph)) + b * (sinT * sinf(ph)) + wc * cosT);
    // where the ray meets the sphere
    const float proj = dl * cosT;
    dist = proj - sqrtf(fmaxf(0.0f, l.r * l.r - (d2 - proj * proj)));
    pdf = 1.0f / (2.0f * M_PIf * fmaxf(1.0f - cosMax, 1e-7f));
    L = l.L;
    return true;
}

// direct light leaving a diffuse point x (normal n, albedo a) towards wherever: sum over the lamps, one sample each
static __forceinline__ __device__ float3 directDiffuse(float3 x, float3 n, float3 a, Rng& rng) {
    float3 sum = make_float3(0, 0, 0);
    const float3 o = x + n * 2e-4f;
    for (unsigned int k = 0; k < params.lightCount; ++k) {
        float3 w, L;
        float pdf, dist;
        if (!sampleLamp(params.lights[k], o, rng.next(), rng.next(), w, L, pdf, dist)) continue;
        const float c = dot(n, w);
        if (c <= 0.0f) continue;
        if (!visible(o, w, dist * 0.999f)) continue;
        sum += L * a * (c * INV_PIf / pdf);
    }
    return sum;
}

// the radiance arriving along -d at a point from direction d (o: where the ray starts), not counting lamps seen
// directly (the direct estimator has them)
static __forceinline__ __device__ float3 incoming(float3 o, float3 d, Rng& rng) {
    float3 sum = make_float3(0, 0, 0), beta = make_float3(1, 1, 1);
    for (unsigned int bounce = 0; bounce < params.maxBounces; ++bounce) {
        Hit h;
        if (!trace(o, d, 1e4f, h)) break;
        if (!h.front || h.mat == params.emissiveMat) break;   // inside a part, or a lamp (counted directly)
        const float3 n = dot(h.n, h.ng) > 0.0f ? h.n : h.ng;
        const float3 a = params.albedo[min(h.mat, (unsigned)(DZ_MAX_MATS - 1))];
        sum += beta * directDiffuse(h.p, n, a, rng);
        beta *= a;
        if (bounce >= 2) {
            const float q = fminf(0.95f, maxc(beta) * 1.5f);
            if (rng.next() >= q) break;
            beta *= 1.0f / q;
        }
        o = h.p + h.ng * 2e-4f;
        d = cosineDir(n, rng.next(), rng.next());
        if (dot(d, h.ng) <= 0.0f) break;
    }
    return sum;
}

// The haze: the light arriving at a point of the air from every direction (its mean, and the luminance's L1 for a
// little forward scattering), lamps sampled directly with their shadows (the shafts), the rest by paths.
extern "C" __global__ void __raygen__volume() {
    const unsigned int i = optixGetLaunchIndex().x;
    if (i >= params.count) return;
    const unsigned int gi = params.texelBase + i;
    const unsigned int nx = params.volDim.x, ny = params.volDim.y;
    const unsigned int ux = gi % nx, uy = (gi / nx) % ny, uz = gi / (nx * ny);
    const float3 p = params.volLo + make_float3(ux + 0.5f, uy + 0.5f, uz + 0.5f) * params.volCell;
    float3 L0 = make_float3(0, 0, 0), L1 = make_float3(0, 0, 0);
    const float Y0 = 0.282095f, Y1 = 0.488603f;
    for (unsigned int s = 0; s < params.samples; ++s) {
        const unsigned int k = params.sampleBase + s;
        Rng rng{pcg(gi * 7919u + k * 3301u + 91u)};
        for (unsigned int l = 0; l < params.lightCount; ++l) {
            float3 w, L;
            float pdf, dist;
            if (!sampleLamp(params.lights[l], p, rng.next(), rng.next(), w, L, pdf, dist)) continue;
            if (!visible(p, w, dist * 0.999f)) continue;
            const float3 c = L * (1.0f / pdf);
            L0 += c * Y0;
            L1 += w * ((0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z) * Y1);
        }
        // indirect: a uniform direction over the sphere (pdf 1/4pi)
        const float z = 1.0f - 2.0f * rng.next(), ph = 2.0f * M_PIf * rng.next(), r = sqrtf(fmaxf(0.0f, 1.0f - z * z));
        const float3 w = make_float3(r * cosf(ph), r * sinf(ph), z);
        float3 Li = incoming(p, w, rng);
        const float lum = 0.2126f * Li.x + 0.7152f * Li.y + 0.0722f * Li.z;
        if (lum > 0.5f) Li *= 0.5f / lum;
        const float3 c = Li * (4.0f * M_PIf);
        L0 += c * Y0;
        L1 += w * ((0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z) * Y1);
    }
    const float4 a0 = params.acc0[i], a1 = params.acc1[i];
    params.acc0[i] = make_float4(a0.x + L0.x, a0.y + L0.y, a0.z + L0.z, a0.w + float(params.samples));
    params.acc1[i] = make_float4(a1.x + L1.x, a1.y + L1.y, a1.z + L1.z, 0.0f);
}

extern "C" __global__ void __raygen__bake() {
    const unsigned int i = optixGetLaunchIndex().x;
    if (i >= params.count) return;
    const float4 P = params.texP[i], N = params.texN[i];
    const float3 p = make_float3(P.x, P.y, P.z), n = normalize(make_float3(N.x, N.y, N.z));
    const float3 o = p + n * 2e-4f;
    const unsigned int gi = params.texelBase + i;
    float3 L0 = make_float3(0, 0, 0), L1r = L0, L1g = L0, L1b = L0;
    float vis[4] = {0, 0, 0, 0};
    float aoVis = 0.0f, aoW = 0.0f;   // cosine-weighted openness within 25 cm (where grime gathers, where reflections dim)
    const float Y0 = 0.282095f, Y1 = 0.488603f;
    // the first direction of every sample from a 2D low-discrepancy sequence (R2), shifted per texel
    const float sx = float(pcg(gi * 2u + 1u) >> 8) * (1.0f / 16777216.0f), sy = float(pcg(gi * 2u + 2u) >> 8) * (1.0f / 16777216.0f);
    for (unsigned int s = 0; s < params.samples; ++s) {
        const unsigned int k = params.sampleBase + s;
        Rng rng{pcg(gi * 9781u + k * 6271u + 17u)};
        // direct: every lamp
        for (unsigned int l = 0; l < params.lightCount; ++l) {
            float3 w, L;
            float pdf, dist;
            if (!sampleLamp(params.lights[l], o, rng.next(), rng.next(), w, L, pdf, dist)) continue;
            if (dot(w, n) <= 0.0f) continue;
            if (!visible(o, w, dist * 0.999f)) continue;
            if (l < 4) vis[l] += 1.0f;
            const float3 c = L * (1.0f / pdf);
            L0 += c * Y0;
            L1r += w * (c.x * Y1);
            L1g += w * (c.y * Y1);
            L1b += w * (c.z * Y1);
        }
        // indirect: a direction uniform over the hemisphere (pdf 1/2pi). The harmonics hold radiance, not irradiance:
        // with cosine sampling the weight pi/cos explodes at grazing angles (single bright texels)
        float u1 = sx + 0.7548776662f * float(k), u2 = sy + 0.5698402910f * float(k);
        u1 -= floorf(u1);
        u2 -= floorf(u2);
        float3 tt, bb;
        frame(n, tt, bb);
        const float cz = u1, sz = sqrtf(fmaxf(0.0f, 1.0f - cz * cz)), ph = 2.0f * M_PIf * u2;
        const float3 w = normalize(tt * (sz * cosf(ph)) + bb * (sz * sinf(ph)) + n * cz);
        const float cw = fmaxf(dot(w, n), 0.0f);
        aoW += cw;
        if (visible(o, w, 0.25f)) aoVis += cw;
        float3 Li = incoming(o, w, rng);
        // a firefly clamp on the indirect light: 0.5 is ~20 times the brightest surface in the room (the chart under its
        // lamp ~0.025); a path that found a hot spot no longer speckles the room
        const float lum = 0.2126f * Li.x + 0.7152f * Li.y + 0.0722f * Li.z;
        if (lum > 0.5f) Li *= 0.5f / lum;
        const float3 c = Li * (2.0f * M_PIf);
        L0 += c * Y0;
        L1r += w * (c.x * Y1);
        L1g += w * (c.y * Y1);
        L1b += w * (c.z * Y1);
    }
    float4 a0 = params.acc0[i], a1 = params.acc1[i], a2 = params.acc2[i], a3 = params.acc3[i];
    a0 = make_float4(a0.x + L0.x, a0.y + L0.y, a0.z + L0.z, a0.w + float(params.samples));
    a1 = make_float4(a1.x + L1r.x, a1.y + L1r.y, a1.z + L1r.z, a1.w + aoVis);
    a2 = make_float4(a2.x + L1g.x, a2.y + L1g.y, a2.z + L1g.z, a2.w + aoW);
    a3 = make_float4(a3.x + L1b.x, a3.y + L1b.y, a3.z + L1b.z, 0.0f);
    params.acc0[i] = a0;
    params.acc1[i] = a1;
    params.acc2[i] = a2;
    params.acc3[i] = a3;
    const float4 a4 = params.acc4[i];
    params.acc4[i] = make_float4(a4.x + vis[0], a4.y + vis[1], a4.z + vis[2], a4.w + vis[3]);
}
