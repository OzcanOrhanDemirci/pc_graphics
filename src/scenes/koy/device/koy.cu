// Scene 5 "Koy": OptiX device programs, compiled at run time with NVRTC (F5 recompiles them).
//   __raygen__base     every pixel: the same fixed sub-pixel samples every frame, each fully shaded; the sun's light
//                      scattered by the water is marched once along the pixel's centre and read at each sample's distance.
//                      A pixel whose samples disagree (another surface, a strong contrast) joins the edge list.
//   __raygen__edge     every edge pixel: the rest of the 32-sample pattern, filtered at once towards its 3x3 neighbours.
//   __raygen__resolve  every pixel: the reconstruction filter over the samples of its 3x3 neighbourhood, XYZ -> sRGB.
//   __raygen__final    exposure, AgX, sRGB, a fixed (not moving) blue-noise TPDF dither.
//   __raygen__bakeAO   the meshes' ambient occlusion per vertex (load time).
//   __raygen__bakeSun  the sun's visibility through the water of the test area, per voxel (load time).
// No random numbers anywhere in a frame: the picture is a function of the camera and the world's time.
#include <optix.h>
#include "scenes/koy/koy_params.h"
#include "scenes/koy/koy_sampling.h"
#include "shared/display.h"

extern "C" {
__constant__ KoyParams params;
}

#define ID_WATER 0xFFFF0001u
#define ID_OPEN 0xFFFF0002u

// =====================================================================================================
// small helpers
// =====================================================================================================
static __forceinline__ __device__ float3 f3(float4 a) { return make_float3(a.x, a.y, a.z); }
static __forceinline__ __device__ unsigned int hashU32(unsigned int x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static __forceinline__ __device__ float fracf(float x) { return x - floorf(x); }
static __forceinline__ __device__ float3 offsetRay(float3 p, float3 n) {   // Waechter & Binder, Ray Tracing Gems ch. 6
    const float origin = 1.0f / 32.0f, floatScale = 1.0f / 65536.0f, intScale = 256.0f;
    const int3 of = make_int3(int(intScale * n.x), int(intScale * n.y), int(intScale * n.z));
    const float3 pi = make_float3(__int_as_float(__float_as_int(p.x) + (p.x < 0 ? -of.x : of.x)),
                                  __int_as_float(__float_as_int(p.y) + (p.y < 0 ? -of.y : of.y)),
                                  __int_as_float(__float_as_int(p.z) + (p.z < 0 ? -of.z : of.z)));
    return make_float3(fabsf(p.x) < origin ? p.x + floatScale * n.x : pi.x, fabsf(p.y) < origin ? p.y + floatScale * n.y : pi.y,
                       fabsf(p.z) < origin ? p.z + floatScale * n.z : pi.z);
}
static __forceinline__ __device__ float3 xyzToRgb(float3 c) {
    return make_float3(dot(params.xyzToRgb0, c), dot(params.xyzToRgb1, c), dot(params.xyzToRgb2, c));
}

// =====================================================================================================
// the baked tables (koy_optics.cpp)
// =====================================================================================================
static __forceinline__ __device__ float4 tex2Dt(KTex t, float u, float v) { return tex2D<float4>((cudaTextureObject_t)t, u, v); }
static __forceinline__ __device__ float4 tex3Dt(KTex t, float u, float v, float w) { return tex3D<float4>((cudaTextureObject_t)t, u, v, w); }
// texel-centre coordinate of x in [0, 1] over n samples placed at the ends and between
static __forceinline__ __device__ float tc(float x, int n) { return (saturate(x) * float(n - 1) + 0.5f) / float(n); }
static __forceinline__ __device__ float tcT(float t, float tmax, int n) { return tc(sqrtf(fmaxf(t, 0.0f) / tmax), n); }   // squared spacing

static __forceinline__ __device__ float3 pathRow(int row, float L) {
    return f3(tex2Dt(params.pathTab, tc(L * (1.0f / PT_LMAX), PT_W), (row + 0.5f) / float(PT_ROWS)));
}
// a linear sRGB albedo -> the weights of the three basis spectra
static __forceinline__ __device__ float3 basisWeights(float3 rgb) {
    return make_float3(params.basisFix[0][0] * rgb.x + params.basisFix[0][1] * rgb.y + params.basisFix[0][2] * rgb.z,
                       params.basisFix[1][0] * rgb.x + params.basisFix[1][1] * rgb.y + params.basisFix[1][2] * rgb.z,
                       params.basisFix[2][0] * rgb.x + params.basisFix[2][1] * rgb.y + params.basisFix[2][2] * rgb.z);
}
// Lambert surface of albedo weights wk at depth z lit by the direct sun with cosine cs, seen after t metres (XYZ)
static __forceinline__ __device__ float3 sunOnSurface(float3 wk, float z, float t) {
    const float L = fmaxf(z, 0.0f) / params.sunCosW + t;
    return pathRow(PT_SURF0 + 0, L) * wk.x + pathRow(PT_SURF0 + 1, L) * wk.y + pathRow(PT_SURF0 + 2, L) * wk.z;
}
// which light fields: the floor's depth under a point (the bottom map) picks the two that bracket it; a field is dark
// below its own floor, so none shallower than zmax (the deepest point that must be lit) is used
struct FW {
    int a, b;
    float w;   // b's weight
};
static __forceinline__ __device__ float bottomAt(float x, float z) {
    if (!params.bottomMap) return params.fieldBottom[params.fields - 1];
    return tex2D<float>((cudaTextureObject_t)params.bottomMap, (x - params.bottomMapMin.x) / params.bottomMapSize.x,
                        (z - params.bottomMapMin.y) / params.bottomMapSize.y);
}
// a surface: the two fields that bracket the floor under it (each read no deeper than its own floor: continuous where the
// floor crosses a field's depth)
static __forceinline__ __device__ FW fieldsFor(float hf) {
    const int n = params.fields;
    int a = 0;
    while (a + 1 < n && params.fieldBottom[a + 1] <= hf) ++a;
    const int b = min(a + 1, n - 1);
    FW f;
    f.a = a;
    f.b = b;
    f.w = (b > a && hf > params.fieldBottom[a]) ? saturate((hf - params.fieldBottom[a]) / (params.fieldBottom[b] - params.fieldBottom[a])) : 0.0f;
    return f;
}
// Ada: a view path over a floor that keeps getting deeper: the fields that bracket the floor under the path (where its
// light comes from: the first ~15 m), never shallower than the path's own deepest point within ~30 m (c ~ 0.1/m)
static __forceinline__ __device__ FW pathFields(float3 o, float3 d, float tEnd) {
    const float zmax = fmaxf(-o.y, -o.y - d.y * fminf(tEnd, 30.0f));
    const float3 m = o + d * fminf(tEnd, 15.0f);
    return fieldsFor(fmaxf(zmax, bottomAt(m.x, m.z)));
}
static __forceinline__ __device__ FW deepestField() {
    FW f;
    f.a = f.b = params.fields - 1;
    f.w = 0.0f;
    return f;
}
// the water's diffuse light on a Lambert surface (normal's vertical component ny) at depth z, seen after t metres
static __forceinline__ __device__ float3 ambientOnSurface(float3 wk, float z, float ny, float t, FW f) {
    const float u = tcT(t, AMB_TMAX, AS_T), v = tc(ny * 0.5f + 0.5f, AS_N);
    float3 r = make_float3(0.0f, 0.0f, 0.0f);
    for (int q = 0; q < 2; ++q) {
        const int k = q == 0 ? f.a : f.b;
        const float wq = q == 0 ? 1.0f - f.w : f.w;
        if (wq <= 0.0f) continue;
        const float w = tc(fminf(z, params.fieldBottom[k]) / AMB_ZMAX, AS_Z);
        r += (f3(tex3Dt(params.ambSurf[k][0], u, v, w)) * wk.x + f3(tex3Dt(params.ambSurf[k][1], u, v, w)) * wk.y +
              f3(tex3Dt(params.ambSurf[k][2], u, v, w)) * wk.z) * wq;
    }
    return r;
}
// the diffuse light scattered into a view ray (direction's vertical component muUp, from depth z0) over its first t metres
static __forceinline__ __device__ float3 ambientPath(float z0, float muUp, float t, FW f) {
    const float u = tcT(t, AMB_TMAX, AP_T), v = tc(muUp * 0.5f + 0.5f, AP_MU), w = tc(z0 / AMB_ZMAX, AP_Z);
    const float3 A = f3(tex3Dt(params.ambPath[f.a], u, v, w));
    return f.w > 0.0f ? A + (f3(tex3Dt(params.ambPath[f.b], u, v, w)) - A) * f.w : A;
}
// the same for a ray leaving the surface downwards (a total internal reflection), seen through a further ts metres
static __forceinline__ __device__ float3 reflectedPath(float muUp, float t2, float ts, FW f) {
    const float u = tcT(t2, AMB_TMAX, RP_T), v = tc(muUp * 0.5f + 0.5f, RP_MU), w = tc(ts / RP_TSMAX, RP_TS);
    const float3 A = f3(tex3Dt(params.reflPath[f.a], u, v, w));
    return f.w > 0.0f ? A + (f3(tex3Dt(params.reflPath[f.b], u, v, w)) - A) * f.w : A;
}

// =====================================================================================================
// phase functions (normalised over the sphere)
// =====================================================================================================
static __forceinline__ __device__ float phaseWater(float c) {   // pure water: 1 + 0.835 cos^2 (depolarisation 0.09)
    return (1.0f + 0.835f * c * c) * (1.0f / (4.0f * M_PIf * (1.0f + 0.835f / 3.0f)));
}
// Fournier-Forand (Fournier & Forand 1994, as written by Mobley et al. 2002); nu and delta180 from the backscatter fraction
static __forceinline__ __device__ float phaseFF(float c) {
    const float th = fmaxf(acosf(clampf(c, -1.0f, 1.0f)), 0.0035f);   // not sharper than the sun's disc seen under water
    const float s2 = sqr(sinf(0.5f * th));
    const float nu = params.ffNu, d180 = params.ffDelta180;
    const float d = d180 * s2;                                      // delta = 4 / (3 (n-1)^2) sin^2(theta/2)
    const float dn = powf(d, nu), d180n = powf(d180, nu);
    const float a = 1.0f / (4.0f * M_PIf * sqr(1.0f - d) * dn) * (nu * (1.0f - d) - (1.0f - dn) + (d * (1.0f - dn) - nu * (1.0f - d)) / s2);
    const float b = (1.0f - d180n) / (16.0f * M_PIf * (d180 - 1.0f) * d180n) * (3.0f * c * c - 1.0f);
    return fmaxf(a + b, 0.0f) * params.ffScale;
}

// =====================================================================================================
// textures of the scanned materials (mipmapped, filtered by the ray cone)
// =====================================================================================================
static __forceinline__ __device__ float3 srgbDecode(float3 c) {
    auto f = [](float x) { return x <= 0.04045f ? x * (1.0f / 12.92f) : powf((x + 0.055f) * (1.0f / 1.055f), 2.4f); };
    return make_float3(f(c.x), f(c.y), f(c.z));
}
static __forceinline__ __device__ float4 texGrad(KTex t, float2 uv, float2 du, float2 dv) {
    return tex2DGrad<float4>((cudaTextureObject_t)t, uv.x, uv.y, du, dv);
}

// =====================================================================================================
// the camera: a pinhole behind a flat mask port (rays refract at the glass: everything 4/3 nearer, a narrower view)
// =====================================================================================================
static __forceinline__ __device__ float3 cameraRay(const KCamera& c, float px, float py) {
    const float u = (2.0f * px / params.width - 1.0f) * c.tanHalfX;
    const float v = (1.0f - 2.0f * py / params.height) * c.tanHalfY;
    float3 d = normalize(c.fwd + c.right * u + c.up * v);
    if (c.portN > 1.0f) {   // Snell at a plane facing fwd: the tangential part shrinks by 1/n
        const float ci = dot(d, c.fwd);
        const float3 tang = (d - c.fwd * ci) * (1.0f / c.portN);
        d = tang + c.fwd * sqrtf(fmaxf(0.0f, 1.0f - dot(tang, tang)));
    }
    return d;
}

// =====================================================================================================
// geometry
// =====================================================================================================
struct Hit {
    bool hit;
    float t;
    float3 p, ng, ns;
    float2 uv;
    float3 dpdu, dpdv;          // world-space surface derivatives along uv (texture footprints, normal maps)
    float ao;
    unsigned int mat, id;
    bool leaf;
};
static __forceinline__ __device__ float3 xformP(const KInstance& in, float3 p) {
    return make_float3(in.r0.x * p.x + in.r0.y * p.y + in.r0.z * p.z + in.r0.w, in.r1.x * p.x + in.r1.y * p.y + in.r1.z * p.z + in.r1.w,
                       in.r2.x * p.x + in.r2.y * p.y + in.r2.z * p.z + in.r2.w);
}
static __forceinline__ __device__ float3 xformV(const KInstance& in, float3 v) {
    return make_float3(in.r0.x * v.x + in.r0.y * v.y + in.r0.z * v.z, in.r1.x * v.x + in.r1.y * v.y + in.r1.z * v.z,
                       in.r2.x * v.x + in.r2.y * v.y + in.r2.z * v.z);
}
static __forceinline__ __device__ Hit trace(float3 o, float3 d, float tmax) {
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(3u), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0);
    Hit h;
    h.hit = optixHitObjectIsHit();
    h.leaf = false;
    if (!h.hit) return h;
    const unsigned int ii = optixHitObjectGetInstanceIndex();
    const unsigned int prim = optixHitObjectGetPrimitiveIndex();
    const KInstance in = params.instances[ii];
    const KMesh& m = params.meshes[in.mesh];
    h.t = optixHitObjectGetRayTmax();
    h.mat = m.material;
    if (optixGetPrimitiveType(optixHitObjectGetHitKind()) == OPTIX_PRIMITIVE_TYPE_FLAT_QUADRATIC_BSPLINE) {
        // a seagrass ribbon: its normal from OptiX, its id per leaf (every leaf a different surface)
        const float2 rp = optixHitObjectGetRibbonParameters();
        h.leaf = true;
        h.p = o + d * h.t;
        h.ng = normalize(xformV(in, optixHitObjectGetRibbonNormal(rp)));
        if (dot(h.ng, d) > 0.0f) h.ng = -h.ng;
        h.ns = h.ng;
        h.uv = make_float2(rp.x, rp.y);
        h.dpdu = make_float3(0, 0, 0);
        h.dpdv = make_float3(0, 0, 0);
        h.ao = m.ao ? m.ao[prim] : 1.0f;
        h.id = 0x10000u + prim / 4u;
        return h;
    }
    const float2 b = optixHitObjectGetTriangleBarycentrics();
    const uint3 tri = m.idx[prim];
    const float3 v0 = m.pos[tri.x], v1 = m.pos[tri.y], v2 = m.pos[tri.z];
    const float b0 = 1.0f - b.x - b.y;
    h.p = xformP(in, v0 * b0 + v1 * b.x + v2 * b.y);
    const float3 e1 = xformV(in, v1 - v0), e2 = xformV(in, v2 - v0);
    h.ng = normalize(cross(e1, e2));
    h.ns = m.nrm ? normalize(xformV(in, m.nrm[tri.x] * b0 + m.nrm[tri.y] * b.x + m.nrm[tri.z] * b.y)) : h.ng;
    if (dot(h.ng, d) > 0.0f) h.ng = -h.ng;
    if (dot(h.ns, h.ng) < 0.0f) h.ns = -h.ns;
    const float* ao = m.ao ? m.ao + size_t(ii - m.aoFirst) * m.aoVerts : nullptr;   // this instance's own
    h.ao = ao ? (ao[tri.x] * b0 + ao[tri.y] * b.x + ao[tri.z] * b.y) : 1.0f;
    h.id = (m.id << 20) | ii;   // every instance a surface of its own (two amphorae side by side make an edge)
    if (m.uv) {
        const float2 t0 = m.uv[tri.x], t1 = m.uv[tri.y], t2 = m.uv[tri.z];
        h.uv = make_float2(t0.x * b0 + t1.x * b.x + t2.x * b.y, t0.y * b0 + t1.y * b.x + t2.y * b.y);
        const float du1 = t1.x - t0.x, dv1 = t1.y - t0.y, du2 = t2.x - t0.x, dv2 = t2.y - t0.y;
        const float det = du1 * dv2 - dv1 * du2;
        if (fabsf(det) > 1e-12f) {
            const float r = 1.0f / det;
            h.dpdu = (e1 * dv2 - e2 * dv1) * r;
            h.dpdv = (e2 * du1 - e1 * du2) * r;
        } else {
            h.dpdu = make_float3(0, 0, 0);
            h.dpdv = make_float3(0, 0, 0);
        }
    } else {
        h.uv = make_float2(h.p.x, h.p.z);
        h.dpdu = make_float3(1, 0, 0);
        h.dpdv = make_float3(0, 0, 1);
    }
    return h;
}
static __forceinline__ __device__ bool visible(float3 o, float3 d, float tmax) {
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(3u),
                  OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, 0, 1, 0);
    return !optixHitObjectIsHit();
}

// =====================================================================================================
// the sea surface seen from below: a periodic height field (FFT), filtered by the pixel's footprint (LEAN slopes)
// =====================================================================================================
struct Wave {
    float h, sx, sz, var;       // height, slopes d/dx d/dz, slope variance inside the footprint
};
static __forceinline__ __device__ Wave waveAt(float x, float z, float footprint, bool ripples) {
    const float inv = 1.0f / params.oceanPatch;
    const float lod = log2f(fmaxf(footprint * inv * params.oceanTexels, 1.0f));
    const float4 w = tex2DLod<float4>((cudaTextureObject_t)params.ocean, x * inv, z * inv, lod);
    Wave r;
    r.h = w.x;
    r.sx = w.y;
    r.sz = w.z;
    r.var = fmaxf(w.w - 0.5f * (w.y * w.y + w.z * w.z), 0.0f) + params.slopeVarFloor;   // w: mean of (sx^2 + sz^2)/2 ... per axis
    // the ripples: the waves shorter than the main surface holds, on their own small patch (an odd offset against its
    // tiles). Their slopes add to the surface's; what of them the footprint averages (LEAN) adds to the variance. Their
    // millimetre heights stay out of the hit, and the caustics never see them
    if (ripples && params.oceanFine) {
        const float invF = 1.0f / params.oceanFinePatch;
        const float lodF = log2f(fmaxf(footprint * invF * params.oceanFineTexels, 1.0f));
        const float4 f = tex2DLod<float4>((cudaTextureObject_t)params.oceanFine, x * invF + 0.3719f, z * invF + 0.6127f, lodF);
        r.sx += f.y;
        r.sz += f.z;
        r.var += fmaxf(f.w - 0.5f * (f.y * f.y + f.z * f.z), 0.0f);
    }
    return r;
}
// where an upward ray meets the surface y = h(x, z): regula falsi inside the band |h| <= hmax (robust at grazing angles)
static __forceinline__ __device__ float surfaceHit(float3 o, float3 d, float coneAngle) {
    const float hm = params.oceanHeightMax;
    float ta = fmaxf((-hm - o.y) / d.y, 0.0f), tb = (hm - o.y) / d.y;
    auto g = [&](float t) {
        const float3 p = o + d * t;
        return p.y - waveAt(p.x, p.z, coneAngle * t / fmaxf(d.y, 0.1f), false).h;
    };
    float ga = g(ta), gb = g(tb);
    if (ga >= 0.0f) return ta;
    float t = tb;
    for (int k = 0; k < 6; ++k) {
        t = ta - ga * (tb - ta) / (gb - ga);
        const float gt = g(t);
        if (gt < 0.0f) { ta = t; ga = gt; } else { tb = t; gb = gt; }
    }
    return t;
}

// The sky above the water, as weights of the three basis spectra of pathTab's sky rows: clear blue (Rayleigh-like),
// white (haze, the sun's aureole, near the horizon) and the sunlit rocky rim of the cove. Perez shape (Preetham clear sky).
static __forceinline__ __device__ float3 skyWeights(float3 w) {
    const float cz = fmaxf(w.y, 0.0f);
    if (cz < params.coastHeight * 0.9f) {   // the cove's rim: a band of sunlit rock above the horizon
        const float rim = 1.0f - smoothstepf(params.coastHeight * 0.8f, params.coastHeight * 0.9f, cz);
        return make_float3(0.0f, 0.0f, rim);
    }
    const float cg = clampf(dot(w, params.sunAir), -1.0f, 1.0f);
    const float g = acosf(cg);
    const float F = (1.0f + params.perez[0] * expf(params.perez[1] / fmaxf(cz, 0.02f))) *
                    (1.0f + params.perez[2] * expf(params.perez[3] * g) + params.perez[4] * cg * cg);
    const float Y = params.skyY * F * params.perezNorm;
    const float white = saturate(0.25f + 0.55f * expf(-3.0f * cz) + 0.35f * expf(-4.0f * g));
    return make_float3(Y * (1.0f - white), Y * white, 0.0f);
}

// =====================================================================================================
// shading
// =====================================================================================================
struct Lit {
    float3 L;           // XYZ arriving at the camera, without the in-scattering of the first segment's sun light
    float t;            // where the first segment ends (the sun's in-scattering is marched up to here)
    unsigned int id;
    float3 n;           // for the views
    float caustic;
};

static __forceinline__ __device__ float causticAt(float3 p, int level) {
    // slices spaced in log depth (a caustic's lines move laterally ~ in proportion to depth); above the first slice the
    // focusing has not happened yet: fade to 1 towards the surface
    const float z = -p.y;
    if (z <= 0.0f || params.causticFlat) return 1.0f;
    const float lz = logf(z);
    const float w = (lz - params.causticLz0) / (params.causticLz1 - params.causticLz0);
    const float inv = 1.0f / params.causticPatch;
    const float F = tex3D<float>((cudaTextureObject_t)params.caustic[level], p.x * inv, p.z * inv, tc(w, params.causticSlices));
    // below the last slice the sun's disc has blurred the focusing (its width grows with depth) and the light that is
    // left comes more and more from scattering: the pattern fades to 1 over the next half of the volume's depth
    if (w > 1.0f) return lerpf(F, 1.0f, saturate((lz - params.causticLz1) / 0.405f));   // ln 1.5
    if (w >= 0.0f) return F;
    return lerpf(1.0f, F, saturate(z / expf(params.causticLz0)));
}
// The sun's light under water through a sun map: the entry point c on the surface (swayed by the waves near it), the
// air's transmission there blurred over the sun's disc as seen from dist metres down (and the deck's own height), and,
// with depth, whether something under the water stops it first. -1 where no map covers c.
static __forceinline__ __device__ float3 swayedEntry(float3 p, float dist) {
    float3 c = p + params.sunWater * dist;
    if (params.shadowSway && dist < params.swayMax) {
        const Wave w = waveAt(c.x, c.z, 0.05f, false);
        const float3 m = normalize(make_float3(-w.sx, 1.0f, -w.sz));
        const float3 i = params.sunAir * -1.0f;
        const float eta = 1.0f / params.waterN, ci = -dot(i, m);
        const float kk = 1.0f - eta * eta * (1.0f - ci * ci);
        if (kk > 0.0f) {
            const float3 t = normalize(i * eta + m * (eta * ci - sqrtf(kk)));
            if (t.y < -0.05f) c = p - t * (p.y / t.y);   // up, against the light, to y = 0
        }
    }
    return c;
}
static __forceinline__ __device__ int sunMapAt(float3 c) {
    for (int k = 0; k < params.sunMaps; ++k) {
        const KSunMap& M = params.sunMap[k];
        if (c.x >= M.min.x && c.z >= M.min.y && c.x <= M.min.x + M.w * M.texel && c.z <= M.min.y + M.h * M.texel) return k;
    }
    return -1;
}
static __forceinline__ __device__ float airSun(const KSunMap& M, float3 c, float dist) {
    // the disc's radius: 0.27 degrees in the air over the deck's ~1.3 m, 0.2 degrees in the water over dist
    const float blur = 0.006f + dist * 0.0035f;
    const float at = 0.5f * M.texel;
    const float lod = fminf(log2f(fmaxf(blur / at, 1.0f)), M.airLevels - 1.0f);
    return tex2DLod<float>((cudaTextureObject_t)M.air, (c.x - M.min.x) / (M.w * M.texel), (c.z - M.min.y) / (M.h * M.texel), lod);
}
// how open the sky is above p under water (1 where no map covers it): the sky map averaged over Snell's window's disc
static __forceinline__ __device__ float skyVisAt(float3 p) {
    if (params.sunMaps == 0 || p.y >= 0.0f) return 1.0f;
    const int k = sunMapAt(make_float3(p.x, 0.0f, p.z));
    if (k < 0) return 1.0f;
    const KSunMap& M = params.sunMap[k];
    const float radius = 0.3f + 1.0f * (-p.y);
    const float lod = fminf(log2f(fmaxf(radius / (2.0f * M.texel), 1.0f)), M.skyLevels - 1.0f);
    return tex2DLod<float>((cudaTextureObject_t)M.sky, (p.x - M.min.x) / (M.w * M.texel), (p.z - M.min.y) / (M.h * M.texel), lod);
}
// the same along a view ray, weighted as its in-scattered light is: each sample stands for a stretch of the ray, worth
// e^(-c a) - e^(-c b) of it (c ~ 0.1/m: 60 % of what reaches the eye comes from beyond 5 m), cut where the ray ends
static __forceinline__ __device__ float pathSky(float3 o, float3 d, float tEnd) {
    if (params.sunMaps == 0) return 1.0f;
    const float ts[5] = {0.5f, 2.0f, 5.0f, 10.0f, 20.0f}, edge[6] = {0.0f, 1.0f, 3.5f, 7.5f, 15.0f, 60.0f};
    float s = 0.0f, w = 0.0f;
    for (int k = 0; k < 5 && edge[k] < tEnd; ++k) {
        const float b = fminf(edge[k + 1], tEnd);
        const float wk = expf(-0.1f * edge[k]) - expf(-0.1f * b);
        s += wk * skyVisAt(o + d * fminf(ts[k], b));
        w += wk;
    }
    return w > 0.0f ? s / w : 1.0f;
}
static __forceinline__ __device__ float sunMapVis(float3 p) {
    if (params.sunMaps == 0 || p.y >= 0.0f) return 1.0f;
    const float dist = -p.y / params.sunWater.y;
    const float3 c = swayedEntry(p, dist);
    const int k = sunMapAt(c);
    if (k < 0) return 1.0f;
    const KSunMap& M = params.sunMap[k];
    const float T = airSun(M, c, dist);
    if (T <= 0.0f) return 0.0f;
    const float u = (c.x - M.min.x) / M.texel - 0.5f, v = (c.z - M.min.y) / M.texel - 0.5f;
    const float fu = floorf(u), fv = floorf(v), au = u - fu, av = v - fv;
    const float bias = 0.02f + 0.5f * M.texel;
    float vis = 0.0f;
    for (int q = 0; q < 4; ++q) {
        const float x = fminf(fmaxf(fu + float(q & 1), 0.0f), float(M.w - 1)), y = fminf(fmaxf(fv + float(q >> 1), 0.0f), float(M.h - 1));
        const float depth = tex2D<float>((cudaTextureObject_t)M.tex, x + 0.5f, y + 0.5f);
        const float wq = ((q & 1) ? au : 1.0f - au) * ((q >> 1) ? av : 1.0f - av);
        if (dist < depth - bias) vis += wq;
    }
    return vis * T;
}

// gradient noise (the load-time builders' kn::noise3, same hash): masks for the concretion and the mottling
static __forceinline__ __device__ unsigned int nHash(int x, int y, int z) {
    unsigned int h = unsigned(x) * 73856093u ^ unsigned(y) * 19349663u ^ unsigned(z) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}
static __forceinline__ __device__ float nGrad(int x, int y, int z, float fx, float fy, float fz) {
    const unsigned int h = nHash(x, y, z) & 15u;
    const float u = h < 8 ? fx : fy, v = h < 4 ? fy : (h == 12 || h == 14 ? fx : fz);
    return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
}
static __forceinline__ __device__ float nFade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
static __device__ float noise3(float3 p) {
    const int X = int(floorf(p.x)), Y = int(floorf(p.y)), Z = int(floorf(p.z));
    const float fx = p.x - X, fy = p.y - Y, fz = p.z - Z;
    const float u = nFade(fx), v = nFade(fy), w = nFade(fz);
    return lerpf(lerpf(lerpf(nGrad(X, Y, Z, fx, fy, fz), nGrad(X + 1, Y, Z, fx - 1, fy, fz), u),
                       lerpf(nGrad(X, Y + 1, Z, fx, fy - 1, fz), nGrad(X + 1, Y + 1, Z, fx - 1, fy - 1, fz), u), v),
                 lerpf(lerpf(nGrad(X, Y, Z + 1, fx, fy, fz - 1), nGrad(X + 1, Y, Z + 1, fx - 1, fy, fz - 1), u),
                       lerpf(nGrad(X, Y + 1, Z + 1, fx, fy - 1, fz - 1), nGrad(X + 1, Y + 1, Z + 1, fx - 1, fy - 1, fz - 1), u), v),
                 w);
}
static __device__ float fbm3(float3 p, int oct) {
    float s = 0.0f, a = 0.5f;
    for (int k = 0; k < oct; ++k) {
        s += a * noise3(p);
        p = p * 2.03f + make_float3(17.3f, 3.1f, 7.7f);
        a *= 0.5f;
    }
    return s;
}

// A tiling scan laid over in world space by three planar projections (on the floor only the horizontal one), filtered by
// the ray cone. Returns its colour (the scan's own, linear) and the tilt its normal map gives, in world axes.
struct Tri {
    float3 albedo, tilt;
    bool ok;
};
static __device__ Tri triplanar(int set, float tile, float3 p, float3 ng, float width, bool floorOnly) {
    Tri r;
    r.albedo = make_float3(0.0f, 0.0f, 0.0f);
    r.tilt = make_float3(0.0f, 0.0f, 0.0f);
    r.ok = false;
    const KTextureSet ds = params.texSets[set];
    const float inv = 1.0f / tile;
    const float lodBase = log2f(fmaxf(width * inv * ds.texels, 1e-6f));
    const float3 an = make_float3(fabsf(ng.x), fabsf(ng.y), fabsf(ng.z));
    float3 w3 = floorOnly ? make_float3(0.0f, 1.0f, 0.0f) : make_float3(an.x * an.x * an.x * an.x, an.y * an.y * an.y * an.y, an.z * an.z * an.z * an.z);
    w3 = w3 / fmaxf(w3.x + w3.y + w3.z, 1e-6f);
    float wsum = 0.0f;
    for (int ax = 0; ax < 3; ++ax) {
        const float wa = ax == 0 ? w3.x : ax == 1 ? w3.y : w3.z;
        if (wa < 0.02f) continue;
        // the plane's uv, rotated by a fixed angle per axis so the tiles never line up with the scan's own
        float2 uv = ax == 0 ? make_float2(p.z, p.y) : ax == 1 ? make_float2(p.x, p.z) : make_float2(p.x, p.y);
        const float ca = 0.8253f, sa = 0.5646f;
        uv = make_float2((ca * uv.x - sa * uv.y) * inv, (sa * uv.x + ca * uv.y) * inv);
        const float axisCos = ax == 0 ? an.x : ax == 1 ? an.y : an.z;
        const float lod = lodBase - log2f(fmaxf(axisCos, 0.25f));
        r.albedo += f3(tex2DLod<float4>((cudaTextureObject_t)ds.albedo, uv.x, uv.y, lod)) * wa;
        if (ds.hasNormal) {
            const float3 t = f3(tex2DLod<float4>((cudaTextureObject_t)ds.normal, uv.x, uv.y, lod)) * 2.0f - make_float3(1.0f, 1.0f, 1.0f);
            // the tangent-space tilt back into the plane's world axes (undo the fixed rotation)
            const float tx = ca * t.x + sa * t.y, ty = -sa * t.x + ca * t.y;
            const float s = (ax == 0 ? ng.x : ax == 1 ? ng.y : ng.z) < 0.0f ? -1.0f : 1.0f;
            r.tilt += (ax == 0 ? make_float3(0.0f, ty, tx * s) : ax == 1 ? make_float3(tx * s, 0.0f, ty) : make_float3(tx * s, ty, 0.0f)) * wa;
        }
        wsum += wa;
    }
    if (wsum > 0.0f) {
        r.albedo = r.albedo / wsum;
        r.tilt = r.tilt / wsum;
        r.ok = true;
    }
    return r;
}

// A surface: the scanned material (ray-cone filtered), the direct sun (caustic factor, a shadow ray) and the water's
// diffuse light (baked occlusion), all seen through tPath metres of water.
// cheap: a surface seen in the wavy mirror of total internal reflection (blurred and distorted there): the costly
// layers (the detail scan, the noise masks of the coat) give way to their averages
static __device__ void surfaceLook(const Hit& h, float3 d, float coneAngle, bool cheap, float3& albedoOut, float3& nOut) {
    const KMaterial m = params.materials[h.mat];
    float3 albedo = m.albedo;
    float3 n = h.ns;
    const float width = coneAngle * h.t;   // the cone's width at the hit
    if (m.texSet >= 0 && (m.kind == KM_TERRAIN || m.kind == KM_MESH)) {
        const KTextureSet ts = params.texSets[m.texSet];
        float2 uv = h.uv;
        float3 dpdu = h.dpdu, dpdv = h.dpdv;
        if (m.kind == KM_TERRAIN) {
            uv = make_float2(h.p.x * m.uvScale, h.p.z * m.uvScale);
            dpdu = make_float3(1.0f / m.uvScale, 0.0f, 0.0f);
            dpdv = make_float3(0.0f, 0.0f, 1.0f / m.uvScale);
        }
        // the cone's footprint on the surface: an ellipse stretched along the view's projection (anisotropic filtering)
        const float cosv = fmaxf(fabsf(dot(d, h.ng)), 0.05f);
        float3 ax = d - h.ng * dot(d, h.ng);
        const float axl = length(ax);
        ax = axl > 1e-6f ? ax / axl : normalize(dpdu);
        const float3 ay = cross(h.ng, ax);
        const float3 A = ax * (width / cosv), B = ay * width;
        // to uv: solve with the (non-orthogonal) tangent frame by projection
        const float uu = dot(dpdu, dpdu), vv = dot(dpdv, dpdv), uv_ = dot(dpdu, dpdv);
        const float det = fmaxf(uu * vv - uv_ * uv_, 1e-20f);
        auto toUV = [&](float3 v) {
            const float a = dot(v, dpdu), b = dot(v, dpdv);
            return make_float2((a * vv - b * uv_) / det, (b * uu - a * uv_) / det);
        };
        const float2 du = toUV(A), dv = toUV(B);
        albedo = albedo * srgbDecode(f3(texGrad(ts.albedo, uv, du, dv)));
        if (ts.hasNormal) {
            const float3 tn = f3(texGrad(ts.normal, uv, du, dv)) * 2.0f - make_float3(1.0f, 1.0f, 1.0f);
            const float3 T = normalize(dpdu - n * dot(n, dpdu));
            const float3 Bt = cross(n, T) * (dot(cross(n, T), dpdv) < 0.0f ? -1.0f : 1.0f);
            const float3 nn = normalize(T * (tn.x * m.normalStrength) + Bt * (tn.y * m.normalStrength) + n * fmaxf(tn.z, 0.05f));
            n = dot(nn, h.ng) > 0.02f ? nn : n;
        }
    }
    // the detail layer: a tiling scan laid over in world space (planar on the floor, triplanar on rocks and jars), its
    // colour modulating around its own mean, its normal map bending the surface: grain that holds up at 30 cm
    if (m.detailSet >= 0 && !cheap) {
        const Tri t = triplanar(m.detailSet, m.detailTile, h.p, h.ng, width, m.kind == KM_TERRAIN);
        if (t.ok) {
            const float3 mean = params.texSets[m.detailSet].meanAlbedo;
            const float3 rel = make_float3(t.albedo.x / fmaxf(mean.x, 1e-3f), t.albedo.y / fmaxf(mean.y, 1e-3f), t.albedo.z / fmaxf(mean.z, 1e-3f));
            albedo = albedo * (make_float3(1.0f, 1.0f, 1.0f) + (rel - make_float3(1.0f, 1.0f, 1.0f)) * m.detailAlbedo);
            const float3 nn = normalize(n + t.tilt * m.detailNormal);
            n = dot(nn, h.ng) > 0.05f ? nn : n;
        }
    }
    // Amfora: what the sea laid over it. A slow mottling first (no two jars, no two metres of rock alike), the greenish
    // film on the sides, the concretion in patches (recesses gather more, faces down in the sand's reach less: the sand
    // scours them), and the felt of fine sediment on everything that looks up (docs/koy/AMFORA_KAYNAK.md §3.3)
    auto sstep = [](float a, float b, float x) {
        const float t = fminf(fmaxf((x - a) / (b - a), 0.0f), 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    if (cheap) {   // the layers' averages: the coat over about its cover, the felt on what looks up
        if (m.coatSet >= 0) albedo = albedo + (m.coatTint - albedo) * fminf(fmaxf(m.coatCover, 0.0f), 1.0f);
        if (m.sediment > 0.0f) albedo = albedo + (m.sedimentColor - albedo) * (0.75f * m.sediment * sstep(0.30f, 0.85f, n.y));
    }
    if (m.mottle > 0.0f && !cheap) albedo = albedo * (1.0f + m.mottle * 2.0f * fbm3(h.p * 1.7f + make_float3(5.0f, 1.0f, 3.0f), 3));
    if (m.film > 0.0f && !cheap) {
        const float side = 1.0f - sstep(0.2f, 0.7f, n.y);
        albedo = albedo + (albedo * m.filmColor - albedo) * (m.film * side);
    }
    float coatMask = 0.0f;   // where the concretion lies (the living patches grow only there)
    if (m.coatSet >= 0 && !cheap) {
        const float nv = fbm3(h.p * (1.0f / m.coatScale), 4);
        const float recess = (1.0f - h.ao) * sstep(-0.4f, 0.0f, n.y);
        const float scour = sstep(-0.2f, -0.7f, n.y);
        const float mask = sstep(-0.10f, 0.10f, nv + 1.2f * (m.coatCover - 0.5f) + 0.9f * recess - 0.5f * scour);
        coatMask = mask;
        if (mask > 0.01f) {
            const Tri c = triplanar(m.coatSet, m.coatTile, h.p, h.ng, width, false);
            if (c.ok) {
                const float3 mean = params.texSets[m.coatSet].meanAlbedo;
                const float3 rel = make_float3(c.albedo.x / fmaxf(mean.x, 1e-3f), c.albedo.y / fmaxf(mean.y, 1e-3f), c.albedo.z / fmaxf(mean.z, 1e-3f));
                const float3 ca = m.coatTint * rel;
                albedo = albedo + (ca - albedo) * mask;
                const float3 nn = normalize(n + c.tilt * (m.coatNormal * mask));
                n = dot(nn, h.ng) > 0.05f ? nn : n;
            }
        }
    }
    if (m.life > 0.0f && !cheap && coatMask > 0.01f) {   // a few cm each on the concretion: orange-red sponge, pink coralline crust (dark at depth)
        const float3 q = h.p * 9.0f;
        const float a = sstep(0.40f, 0.52f, noise3(q + make_float3(3.0f, 1.0f, 7.0f))) * m.life * coatMask;
        const float b = sstep(0.38f, 0.48f, noise3(q * 0.7f + make_float3(11.0f, 5.0f, 2.0f))) * m.life * 0.8f * coatMask;
        albedo = albedo + (m.lifeA - albedo) * a;
        albedo = albedo + (m.lifeB - albedo) * b;
    }
    if (m.sediment > 0.0f && !cheap) {
        const float up = sstep(0.30f, 0.85f, n.y);
        const float s = m.sediment * up * (0.55f + 0.45f * sstep(-0.25f, 0.25f, fbm3(h.p * 3.1f + make_float3(2.0f, 7.0f, 1.0f), 3)));
        albedo = albedo + (m.sedimentColor - albedo) * s;
        const float3 nn = normalize(n + (h.ng - n) * (0.6f * s));   // the felt fills the fine relief
        n = dot(nn, h.ng) > 0.05f ? nn : n;
    }
    // the sand ripples, analytic: crests along the depth contours (circles round the island, the waves' fronts bend to the
    // shore), wandering slowly; the height fades with depth (the swell no longer stirs the sand there). Band-limited by the
    // footprint: a Gaussian of the cone's width filters each harmonic exactly (sharp near, gone far, never aliased)
    if (m.kind == KM_TERRAIN && params.ripples) {
        const float fade = 1.0f - sstep(params.rippleDepth0, params.rippleDepth1, -h.p.y);
        const float cosv = fmaxf(fabsf(dot(d, h.ng)), 0.05f);
        const float sigma = 0.5f * width / cosv;   // the footprint's spread along the view (its long axis)
        const float k = 2.0f * M_PIf / params.rippleLength;
        const float g1 = expf(-0.5f * sqr(k * sigma)), g2 = expf(-0.5f * sqr(2.0f * k * sigma));
        if (fade > 0.0f && g1 > 1e-3f) {
            const float2 rel = make_float2(h.p.x - params.islandCentre.x, h.p.z - params.islandCentre.y);
            const float r = fmaxf(sqrtf(rel.x * rel.x + rel.y * rel.y), 1e-3f);
            auto warp = [&](float x, float z) { return 1.3f * noise3(make_float3(x * 0.06f, 1.7f, z * 0.06f)); };
            const float e = 0.5f;
            const float wx = (warp(h.p.x + e, h.p.z) - warp(h.p.x - e, h.p.z)) / (2.0f * e);
            const float wz = (warp(h.p.x, h.p.z + e) - warp(h.p.x, h.p.z - e)) / (2.0f * e);
            // the phase reduced to whole cycles first: k r is thousands of radians and the fast cosine is only good near 0
            const float cyc = (r + warp(h.p.x, h.p.z)) / params.rippleLength;
            const float ph = 2.0f * M_PIf * (cyc - floorf(cyc)) + 2.2f * noise3(make_float3(h.p.x * 0.45f, 3.1f, h.p.z * 0.45f));
            const float A = params.rippleHeight * fade * (0.65f + 0.35f * noise3(make_float3(h.p.x * 0.3f, 5.3f, h.p.z * 0.3f)));
            const float dh = A * k * (0.55f * g1 * cosf(ph) + 0.44f * g2 * cosf(2.0f * ph + 0.7f));   // dh / d(r + warp)
            const float sx = dh * (rel.x / r + wx), sz = dh * (rel.y / r + wz);
            const float3 nn = normalize(n - make_float3(sx, 0.0f, sz));
            n = dot(nn, h.ng) > 0.05f ? nn : n;
        }
    }
    if (m.stainDark > 0.0f) {   // the tide's dark band
        const float t = fminf(fmaxf((h.p.y - (m.stainTop - m.stainBand)) / fmaxf(m.stainBand, 1e-3f), 0.0f), 1.0f);
        albedo = albedo * (m.stainDark + (1.0f - m.stainDark) * t * t * (3.0f - 2.0f * t));
    }
    albedoOut = albedo;
    nOut = n;
}

// How much of the sun reaches p under water: the beam enters where the mean refracted sun meets the surface, the waves'
// slope there bends it (the shadows sway with the waves, as the caustics do); it must cross the water unblocked (a shadow
// ray), and the air: through a sun map's air transmission where one covers the entry (soft as the sun's disc), else a ray
static __device__ float sunReaches(float3 p, float3 ng) {
    const float dist = -p.y / params.sunWater.y;
    const float3 c = swayedEntry(p, dist);
    const float3 dir = normalize(c - p);
    const float tUp = (params.oceanHeightMax - p.y) / fmaxf(dir.y, 0.05f);
    if (!visible(offsetRay(p, ng), dir, tUp)) return 0.0f;
    if (!params.airGeometry) return 1.0f;
    const int k = sunMapAt(c);
    if (k >= 0) return airSun(params.sunMap[k], c, dist);
    return visible(c, params.sunAir, 400.0f) ? 1.0f : 0.0f;
}

static __forceinline__ __device__ float3 airRow(int row, float3 wk, float L) {
    return pathRow(row + 0, L) * wk.x + pathRow(row + 1, L) * wk.y + pathRow(row + 2, L) * wk.z;
}
// Something above the water (the pier, the island's rock, the beach) seen from below through Snell's window, ts metres of
// water after its light enters: the sun with a shadow ray, the sky on what looks up, the sea's glow on what looks down
static __device__ float3 shadeAir(const Hit& h, float3 d, float ts, float coneAngle) {
    const KMaterial m = params.materials[h.mat];
    float3 albedo, n;
    surfaceLook(h, d, coneAngle, true, albedo, n);
    const float3 wk = basisWeights(albedo);
    float3 L = make_float3(0.0f, 0.0f, 0.0f);
    const float cs = dot(n, params.sunAir);
    const float3 sun = airRow(PT_AIRSUN0, wk, ts), sky = airRow(PT_AIRSKY0, wk, ts);
    if (cs > 0.0f && dot(h.ng, params.sunAir) > -0.05f && visible(offsetRay(h.p, h.ng), params.sunAir, 400.0f)) L += sun * cs;
    // under the head's slab the sky is hidden too (the sky map read about a metre wide at the point)
    const float skyOpen = h.p.y < 1.5f ? skyVisAt(make_float3(h.p.x, -0.7f, h.p.z)) : 1.0f;
    const float occ = lerpf(1.0f, h.ao, m.aoStrength) * skyOpen;
    const float up = 0.5f + 0.5f * n.y;
    const float glow = params.upwell * skyVisAt(make_float3(h.p.x, -1.0f, h.p.z));   // under the head the water below is dark
    L += (sky * up + (sun * params.sunAir.y + sky) * (glow * (1.0f - up))) * occ;
    return L;
}

static __forceinline__ __device__ float3 shadeSurface(const Hit& h, float3 d, float tPath, float coneAngle, float& causticOut, float3& nOut,
                                                     bool cheap = false) {
    const KMaterial m = params.materials[h.mat];
    float3 albedo, n;
    surfaceLook(h, d, coneAngle, cheap, albedo, n);
    auto sstep = [](float a, float b, float x) {
        const float t = fminf(fmaxf((x - a) / (b - a), 0.0f), 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    nOut = n;
    const float3 wk = basisWeights(albedo);
    const float z = -h.p.y;
    float3 L = make_float3(0.0f, 0.0f, 0.0f);
    // the sun
    const float cs = dot(n, params.sunWater);
    causticOut = 1.0f;
    if (cs > 0.0f && dot(h.ng, params.sunWater) > -0.05f) {
        const float F = causticAt(h.p, 0);
        causticOut = F;
        const float V = sunReaches(h.p, h.ng);
        if (V > 0.0f) L += sunOnSurface(wk, z, tPath) * (cs * F * V);
    }
    // the water's diffuse light
    const float occ = lerpf(1.0f, h.ao, m.aoStrength);
    L += ambientOnSurface(wk, z, n.y, tPath, fieldsFor(bottomAt(h.p.x, h.p.z))) * (occ * skyVisAt(h.p));
    // the diver's torch: inverse square, a cone, a shadow ray to the hand; its light crosses the water to the surface and
    // on to the eye (tPath), red gone within a few metres
    if (params.torch) {
        const float3 tv = params.torchPos - h.p;
        const float d2 = fmaxf(dot(tv, tv), 1e-4f), dt = sqrtf(d2);
        const float3 tl = tv * (1.0f / dt);
        const float cs = dot(n, tl);
        const float spot = sstep(params.torchCos1, params.torchCos0, dot(tl * -1.0f, params.torchDir));
        if (cs > 0.0f && spot > 0.0f && dot(h.ng, tl) > -0.05f && visible(offsetRay(h.p, h.ng), tl, dt - 0.02f)) {
            const float Lt = dt + tPath;
            L += (pathRow(PT_TORCH0 + 0, Lt) * wk.x + pathRow(PT_TORCH0 + 1, Lt) * wk.y + pathRow(PT_TORCH0 + 2, Lt) * wk.z) * (cs * spot / d2);
        }
    }
    // debug views: grey levels in display terms (5 occlusion, 6 the sun's shadow ray, 7 the normal's height)
    if (params.view >= 5 && params.view <= 7) {
        const float tUp = (params.oceanHeightMax - h.p.y) / fmaxf(params.sunWater.y, 0.05f);
        const float v = params.view == 5 ? h.ao : params.view == 6 ? sunReaches(h.p, h.ng) : n.y;
        const float g = 0.18f * v / fmaxf(params.exposure, 1e-9f);
        return make_float3(0.9505f * g, g, 1.089f * g);
    }
    return L;
}

// What a ray leaving the surface downwards sees (a total internal reflection): one more surface or the water's depth.
// tBefore: the metres the light still travels afterwards (to the camera).
static __forceinline__ __device__ float3 shadeReflected(float3 q, float3 r, float tBefore, float coneAngle) {
    const Hit h = trace(q, r, params.farDistance);
    float3 L;
    if (h.hit) {
        float c;
        float3 n;
        L = shadeSurface(h, r, h.t + tBefore, coneAngle, c, n, true);   // seen in the wavy mirror: the cheap layers
        L += reflectedPath(r.y, h.t, tBefore, deepestField()) * pathSky(q, r, h.t);   // reflected rays go down: the deep field is lit all the way
    } else {
        L = reflectedPath(r.y, params.farDistance, tBefore, deepestField()) * pathSky(q, r, params.farDistance);
    }
    return L;
}

// The surface from below at q (reached after ts metres along d): Snell's window (the sky, the rim, the sun's glints,
// all refracted, Fresnel and n^2), and outside it the mirror of total internal reflection. The footprint's unresolved
// slopes soften the window's edge and spread the glints: the sharper the footprint, the sharper both.
static __forceinline__ __device__ float3 shadeFromBelow(float3 q, float3 d, float ts, float coneAngle, float3& nOut) {
    const float foot = coneAngle * ts / fmaxf(d.y, 0.1f);
    const Wave w = waveAt(q.x, q.z, foot, true);
    const float3 m = normalize(make_float3(-w.sx, 1.0f, -w.sz));
    nOut = m;
    const float n1 = params.waterN;
    const float ci = clampf(dot(d, m), 0.0f, 1.0f);
    const float si = sqrtf(fmaxf(0.0f, 1.0f - ci * ci));
    const float thetaI = acosf(ci), thetaC = asinf(1.0f / n1);
    const float sigma = sqrtf(w.var);                         // slope spread ~ normal angle spread (rad), small slopes
    const float spread = fmaxf(sigma * 1.4142f, 1e-4f);
    // the share of the footprint's normals that let the ray out (incidence below the critical angle)
    const float pass = 0.5f * erfcf((thetaI - thetaC) / spread);
    float3 L = make_float3(0.0f, 0.0f, 0.0f);
    float fresnel = 1.0f;
    if (pass > 1e-4f) {
        const float st = fminf(n1 * si, 0.9999f);
        const float ct = sqrtf(1.0f - st * st);
        // transmitted direction (into air)
        const float3 tdir = normalize(d * n1 + m * (ct - n1 * ci));
        // unpolarised Fresnel water -> air
        const float rs = (n1 * ci - ct) / (n1 * ci + ct), rp = (ci - n1 * ct) / (ci + n1 * ct);
        fresnel = 0.5f * (rs * rs + rp * rp);
        const float T = (1.0f - fresnel) * pass;
        // what stands above the water there (the pier, the island), or the sky
        Hit ha;
        ha.hit = false;
        if (params.airGeometry) ha = trace(q + make_float3(0.0f, 0.002f, 0.0f), tdir, 2000.0f);
        if (ha.hit) {
            L += shadeAir(ha, tdir, ts, coneAngle) * T;
        } else {
            const float3 sw = skyWeights(tdir);
            L += (pathRow(PT_SKY0 + 0, ts) * sw.x + pathRow(PT_SKY0 + 1, ts) * sw.y + pathRow(PT_SKY0 + 2, ts) * sw.z) * T;
        }
        // the sun's glint: microfacet transmission (Walter et al. 2007) with Beckmann slopes of the footprint's spread
        // plus the sun's own disc mapped to slopes, evaluated for the one normal that refracts d into the sun
        const float3 s = params.sunAir;
        float3 hm = d * n1 - s;                                // the refraction half vector (towards the air)
        if (hm.y > 1e-4f) {
            hm = normalize(hm);
            const float im = dot(s, hm), om = dot(d, hm);      // the light's and the view's cosines with that normal
            if (im > 0.0f && om > 0.0f) {
                const float px = -hm.x / hm.y, pz = -hm.z / hm.y;   // its slopes
                const float discSlope = params.sunRadiusAir / fmaxf(length(d * n1 - s), 0.05f);
                const float v = w.var + 0.25f * discSlope * discSlope;
                const float dx = px - w.sx, dz = pz - w.sz;
                const float cm = hm.y;
                const float D = expf(-(dx * dx + dz * dz) / (2.0f * v)) / (2.0f * M_PIf * v * cm * cm * cm * cm);
                const float denom = im - n1 * om;               // n_i (i.m) + n_o (o.m) with o = -d
                const float J = n1 * n1 * om * im / fmaxf(denom * denom, 1e-6f);   // |i.m||o.m| n_o^2 / (...)^2
                const float cosO = fmaxf(d.y, 0.05f), cosI = fmaxf(s.y, 0.05f);
                const float fs = (1.0f - fresnel) * D * J / (cosO * cosI);   // BTDF
                L += pathRow(PT_SUN_AIR, ts) * (fs * cosI);
            }
        }
    }
    // the mirror: total internal reflection outside the window, Fresnel inside it
    const float R = (1.0f - pass) + pass * fresnel;
    if (R > 1e-3f) {
        const float3 r = normalize(reflect3(d, m));
        if (r.y < -0.02f) L += shadeReflected(q + make_float3(0.0f, -0.01f, 0.0f), r, ts, coneAngle) * R;
    }
    return L;
}

// One sample: trace, shade what is hit, carry it through the water to the camera (the diffuse in-scattering included;
// the sun's single scattering of the first segment is added by the march).
static __forceinline__ __device__ Lit shadeSample(float3 o, float3 d, float coneAngle) {
    Lit r;
    r.n = make_float3(0, 0, 0);
    r.caustic = 1.0f;
    const float z0 = -o.y;
    float tmax = params.farDistance;
    if (d.y > 1e-4f) tmax = fminf(tmax, (params.oceanHeightMax - o.y) / d.y);
    Hit h = trace(o, d, tmax);
    if (h.hit && d.y > 1e-4f && h.p.y > -params.oceanHeightMax) {
        // something standing through the surface (a pile) hit near the water line: if the wave there is lower than the
        // hit, the ray left the water before it (it meets that from the air, through the surface)
        const Wave w = waveAt(h.p.x, h.p.z, coneAngle * h.t, false);
        if (h.p.y > w.h) h.hit = false;
    }
    if (h.hit) {
        // the path's light field: the floor under where it ends, and lit down to its deepest point
        // (the depth the path reaches within ~30 m: the water's in-scattering comes almost all from there, c ~ 0.1/m)
        const FW fp = pathFields(o, d, h.t);
        r.L = shadeSurface(h, d, h.t, coneAngle, r.caustic, r.n) + (params.view >= 5 ? make_float3(0, 0, 0) : ambientPath(z0, d.y, h.t, fp) * pathSky(o, d, h.t));
        r.t = h.t;
        r.id = h.id;
        return r;
    }
    if (d.y > 1e-4f) {
        const float ts = surfaceHit(o, d, coneAngle);
        const float3 q = o + d * ts;
        r.L = shadeFromBelow(q, d, ts, coneAngle, r.n) + ambientPath(z0, d.y, ts, pathFields(o, d, ts)) * pathSky(o, d, ts);
        r.t = ts;
        r.id = ID_WATER;
        return r;
    }
    // open water: the field lit down to the depth the ray reaches within ~30 m (its light comes almost all from there)
    r.L = ambientPath(z0, d.y, params.farDistance, pathFields(o, d, params.farDistance)) * pathSky(o, d, params.farDistance);
    r.t = params.farDistance;
    r.id = ID_OPEN;
    return r;
}

// ---- the sun's light scattered once by the water along a ray (shafts): marched per 2x2 pixel block, read per sample
// knot k of K sits at t = shaftMax (k / (K-1))^2: fine near the eye
static __forceinline__ __device__ float knotT(int k) {
    const float u = float(k) / float(params.shaftKnots - 1);
    return params.shaftMax * u * u;
}
static __forceinline__ __device__ float knotCoord(float t) {
    return sqrtf(fminf(fmaxf(t, 0.0f), params.shaftMax) / params.shaftMax) * float(params.shaftKnots - 1);
}
// the shafts' light reaching the camera from the first t metres of a sample's ray (px, py: its position on the screen)
static __forceinline__ __device__ float3 shaftAt(float px, float py, float t) {
    // block i's centre sits at pixel 2i - 1 (texel centres at i + 0.5, unnormalised coordinates)
    const float bx = (px + 1.0f) * 0.5f + 0.5f, by = (py + 1.0f) * 0.5f + 0.5f, bz = knotCoord(t) + 0.5f;
    return f3(tex3D<float4>((cudaTextureObject_t)params.shaftTex, bx, by, bz));
}

// =====================================================================================================
// frame passes
// =====================================================================================================
static __forceinline__ __device__ float displayValue(float3 xyz) {   // a rough display brightness: for edge detection only
    const float Y = fmaxf(xyz.y, 0.0f) * params.exposure;
    return Y / (1.0f + Y);
}

extern "C" __global__ void __raygen__base() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int pix = idx.y * params.width + idx.x;
    const KCamera& cam = params.cam;
    const int N = params.baseSpp;
    const float cone = cam.pixelAngle / sqrtf(float(N));
    for (int s = 0; s < N; ++s) {
        const float2 sp = params.basePattern[s] + params.patternShift;
        const float3 d = cameraRay(cam, idx.x + sp.x, idx.y + sp.y);
        const Lit l = shadeSample(cam.pos, d, cone);
        const float3 c = l.L + shaftAt(idx.x + sp.x, idx.y + sp.y, l.t);
        params.samples[size_t(pix) * N + s] = make_float4(c.x, c.y, c.z, __uint_as_float(l.id));
    }
}

// Which pixels sit on an edge: a strong contrast between their samples, or another surface with at least a little
// contrast (two leaves of the same green, the far floor meeting the far water, need no more). With one sample a pixel
// has nothing to compare inside it: it is compared with its four neighbours instead (both sides of an edge get flagged).
extern "C" __global__ void __raygen__edgeFind() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int pix = idx.y * params.width + idx.x;
    const int N = params.baseSpp;
    float lo = 1e30f, hi = -1e30f;
    bool other = false;
    unsigned int id0 = 0;
    for (int s = 0; s < N; ++s) {
        const float4 c = params.samples[size_t(pix) * N + s];
        const float v = displayValue(f3(c));
        lo = fminf(lo, v);
        hi = fmaxf(hi, v);
        const unsigned int id = __float_as_uint(c.w);
        if (s == 0) id0 = id;
        other = other || id != id0;
    }
    if (N == 1) {
        const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
        for (int k = 0; k < 4; ++k) {
            const int qx = int(idx.x) + dx[k], qy = int(idx.y) + dy[k];
            if (qx < 0 || qy < 0 || qx >= int(params.width) || qy >= int(params.height)) continue;
            const float4 c = params.samples[size_t(qy) * params.width + qx];
            const float v = displayValue(f3(c));
            lo = fminf(lo, v);
            hi = fmaxf(hi, v);
            other = other || __float_as_uint(c.w) != id0;
        }
    }
    unsigned int e = 0;
    if (params.edgeSpp > N && (hi - lo > params.edgeContrast || (other && hi - lo > params.edgeContrast * params.edgeIdShare))) {
        const unsigned int k = atomicAdd(params.edgeCount, 1u);
        if (k < params.edgeCapacity) {
            params.edgeList[k] = pix;
            e = k + 1;
        }
    }
    params.edgeIndex[pix] = e;
}

// The diver's collision probe: distance to the static geometry (rocks, floor; not the seagrass, which bends) in 26
// directions from probeOrigin, up to 2 m.
extern "C" __global__ void __raygen__probe() {
    const unsigned int k = optixGetLaunchIndex().x;
    const int ix = int(k % 3) - 1, iy = int((k / 3) % 3) - 1, iz = int(k / 9) - 1;
    const int kk = k >= 13 ? int(k) + 1 : int(k);   // skip (0, 0, 0)
    const int jx = kk % 3 - 1, jy = (kk / 3) % 3 - 1, jz = kk / 9 - 1;
    (void)ix; (void)iy; (void)iz;
    const float3 d = normalize(make_float3(float(jx), float(jy), float(jz)));
    optixTraverse(params.handle, params.probeOrigin, d, 0.0f, 2.0f, 0.0f, OptixVisibilityMask(1u), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0);
    params.bakeOut[k] = optixHitObjectIsHit() ? optixHitObjectGetRayTmax() : 2.0f;
}

// the reconstruction filter (separable), x in pixels from the output pixel's centre: the same function as the host's
static __forceinline__ __device__ float filter1(float x) { return koyFilter(params.filter, params.filterRadius, x); }

extern "C" __global__ void __raygen__edge() {
    const unsigned int k = optixGetLaunchIndex().x;
    if (k >= min(*params.edgeCount, params.edgeCapacity)) return;
    const unsigned int pix = params.edgeList[k];
    const unsigned int x = pix % params.width, y = pix / params.width;
    const KCamera& cam = params.cam;
    const int N = params.baseSpp, M = params.edgeSpp - params.baseSpp;
    const float cone = cam.pixelAngle / sqrtf(float(params.edgeSpp));
    float3 acc[9];
    for (int j = 0; j < 9; ++j) acc[j] = make_float3(0.0f, 0.0f, 0.0f);
    const float invN = 1.0f / float(params.edgeSpp);
    for (int s = 0; s < M; ++s) {
        const float2 sp = params.edgePattern[N + s] + params.patternShift;
        const float3 d = cameraRay(cam, x + sp.x, y + sp.y);
        const Lit l = shadeSample(cam.pos, d, cone);
        const float3 c = (l.L + shaftAt(x + sp.x, y + sp.y, l.t)) * invN;
        // towards the output pixel at (x + jx, y + jy): neighbour j = (jy + 1) * 3 + (jx + 1)
        const float wx[3] = {filter1(sp.x + 0.5f), filter1(sp.x - 0.5f), filter1(sp.x - 1.5f)};
        const float wy[3] = {filter1(sp.y + 0.5f), filter1(sp.y - 0.5f), filter1(sp.y - 1.5f)};
        for (int jy = 0; jy < 3; ++jy)
            for (int jx = 0; jx < 3; ++jx) acc[jy * 3 + jx] += c * (wx[jx] * wy[jy]);
    }
    for (int j = 0; j < 9; ++j) params.edgeAcc[size_t(k) * 9 + j] = make_float4(acc[j].x, acc[j].y, acc[j].z, 0.0f);
}

extern "C" __global__ void __raygen__resolve() {
    const uint3 idx = optixGetLaunchIndex();
    const int W = int(params.width), H = int(params.height);
    const int x = int(idx.x), y = int(idx.y);
    const int N = params.baseSpp;
    float3 sum = make_float3(0.0f, 0.0f, 0.0f);
    float wsum = 0.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int qx = x + dx, qy = y + dy;
            if (qx < 0 || qy < 0 || qx >= W || qy >= H) continue;
            const unsigned int q = unsigned(qy) * params.width + unsigned(qx);
            const unsigned int e = params.edgeIndex[q];
            const float inv = 1.0f / float(e ? params.edgeSpp : N);
            for (int s = 0; s < N; ++s) {
                const float2 sp = params.basePattern[s] + params.patternShift;
                const float w = filter1(dx + sp.x - 0.5f) * filter1(dy + sp.y - 0.5f) * inv;
                const float4 c = params.samples[size_t(q) * N + s];
                sum += f3(c) * w;
                wsum += w;
            }
            if (e) {
                // the edge pixel q filtered its extra samples towards the output pixel at q + (jx, jy): here (-dx, -dy)
                const int j = (-dy + 1) * 3 + (-dx + 1);
                sum += f3(params.edgeAcc[size_t(e - 1) * 9 + j]);
                wsum += params.filterWEdge[-dy + 1][-dx + 1];
            }
        }
    const float3 xyz = wsum > 0.0f ? sum / wsum : make_float3(0.0f, 0.0f, 0.0f);
    float3 rgb = xyzToRgb(xyz);
    if (params.view == 1 && params.edgeIndex[unsigned(y) * params.width + unsigned(x)]) rgb = rgb * 0.3f + make_float3(1.0f, 0.15f, 0.05f) * 0.7f / fmaxf(params.exposure, 1e-6f);
    params.hdr[unsigned(y) * params.width + unsigned(x)] = make_float4(rgb.x, rgb.y, rgb.z, 1.0f);
}

// the light a white Lambert plane facing up gets at the eye's depth (the sun on it and the water's diffuse light), as linear
// sRGB normalised to unit luminance: what the eye adapts its colour to
static __forceinline__ __device__ float3 adaptingWhite() {
    const float z = fmaxf(-params.cam.pos.y, 0.0f);
    const float3 wk = basisWeights(make_float3(1.0f, 1.0f, 1.0f));
    const float3 xyz = sunOnSurface(wk, z, 0.0f) * params.sunCosW + ambientOnSurface(wk, z, 1.0f, 0.0f, fieldsFor(bottomAt(params.cam.pos.x, params.cam.pos.z)));
    const float3 rgb = xyzToRgb(xyz);
    const float Y = fmaxf(0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z, 1e-12f);
    // deep blue light leaves sRGB's gamut (red below zero): no channel under a tenth of the luminance
    return make_float3(fmaxf(rgb.x / Y, 0.1f), fmaxf(rgb.y / Y, 0.1f), fmaxf(rgb.z / Y, 0.1f));
}

extern "C" __global__ void __raygen__final() {
    const uint3 idx = optixGetLaunchIndex();
    const unsigned int i = idx.y * params.width + idx.x;
    float3 c = f3(params.hdr[i]) * params.exposure;
    if (params.chromaAdapt > 0.0f) {   // von Kries towards the light around the eye, part of the way
        const float3 w = adaptingWhite();
        const float a = params.chromaAdapt;
        const float3 g = make_float3(fminf(powf(w.x, -a), 4.0f), fminf(powf(w.y, -a), 4.0f), fminf(powf(w.z, -a), 4.0f));
        const float gy = 0.2126f * g.x * w.x + 0.7152f * g.y * w.y + 0.0722f * g.z * w.z;   // the white's luminance after
        const float Yw = 0.2126f * w.x + 0.7152f * w.y + 0.0722f * w.z;
        c = make_float3(c.x * g.x, c.y * g.y, c.z * g.z) * (Yw / fmaxf(gy, 1e-6f));
    }
    // oxygen toxicity (Ada, PLAN_ADA §6): the field of view closes in from the edges (tunnel vision), then everything
    // goes black; the restart opens it again
    if (params.tunnel > 0.0f || params.blackout > 0.0f) {
        const float ax = (idx.x + 0.5f) / params.width * 2.0f - 1.0f, ay = ((idx.y + 0.5f) / params.height * 2.0f - 1.0f) * float(params.height) / float(params.width);
        const float rr = sqrtf(ax * ax + ay * ay);
        const float r1 = lerpf(1.25f, 0.18f, params.tunnel), r0 = r1 * lerpf(0.55f, 0.35f, params.tunnel);
        const float t = saturate((rr - r0) / fmaxf(r1 - r0, 1e-3f));
        c = c * ((1.0f - t * t * (3.0f - 2.0f * t)) * (1.0f - params.blackout));
    }
    c = gamutCompress(c, luminance709(c));
    c = agx(c);
    // a fixed TPDF dither from two decorrelated blue-noise tiles: no banding, and nothing that moves between frames
    float n = 0.0f;
    if (params.blueNoise) {
        const float a = params.blueNoise[((idx.y) & 127u) * 128u + ((idx.x) & 127u)];
        const float b = params.blueNoise[((idx.y + 61u) & 127u) * 128u + ((idx.x + 37u) & 127u)];
        n = a + b - 1.0f;
    }
    const float r = fminf(fmaxf(srgbOetf(c.x) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    const float g = fminf(fmaxf(srgbOetf(c.y) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    const float b = fminf(fmaxf(srgbOetf(c.z) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    if (params.layoutParams != sizeof(KoyParams) || params.layoutMaterial != sizeof(KMaterial)) {
        params.output[i] = make_uchar4(255, 0, 255, 255);   // host and device disagree on the structs: rebuild
        return;
    }
    params.output[i] = make_uchar4((unsigned char)r, (unsigned char)g, (unsigned char)b, 255);
}

extern "C" __global__ void __raygen__shafts() {
    const uint3 idx = optixGetLaunchIndex();
    const KCamera& cam = params.cam;
    // the block's centre on the screen: pixel 2i - 1 (blocks reach a pixel beyond each border)
    const float px = 2.0f * float(idx.x) - 1.0f, py = 2.0f * float(idx.y) - 1.0f;
    const float3 d = cameraRay(cam, px, py);
    // the sun's light travels along -sunWater and leaves towards the camera along -d: cos(theta) = dot(sunWater, d)
    const float cth = dot(d, params.sunWater);
    const float pW = phaseWater(cth), pP = phaseFF(cth);
    const float3 o = cam.pos;
    float3 acc = make_float3(0.0f, 0.0f, 0.0f);
    const int K = params.shaftKnots;
    surf3Dwrite(make_float4(0.0f, 0.0f, 0.0f, 0.0f), (cudaSurfaceObject_t)params.shaftSurf, int(idx.x * sizeof(float4)), int(idx.y), 0);
    float t0 = 0.0f;
    for (int k = 1; k < K; ++k) {
        const float t1 = knotT(k);
        const float step = params.shaftStep0 + params.shaftStepGrow * t0;
        const int n = min(8, max(1, int(ceilf((t1 - t0) / step))));
        const float ds = (t1 - t0) / float(n);
        const int level = ds < 0.08f ? 0 : ds < 0.16f ? 1 : ds < 0.32f ? 2 : 3;
        for (int j = 0; j < n; ++j) {
            const float sm = t0 + (j + 0.5f) * ds;
            const float3 p = o + d * sm;
            const float z = -p.y;
            if (z <= 0.0f) continue;   // above the surface: no water
            const float L = z / params.sunCosW + sm;
            const float3 src = pathRow(PT_SHAFT_W, L) * pW + pathRow(PT_SHAFT_P, L) * pP;
            const float F = causticAt(p, min(level, params.causticLevels - 1));
            const float V = sunMapVis(p);
            acc += src * (F * V * ds);
        }
        surf3Dwrite(make_float4(acc.x, acc.y, acc.z, 0.0f), (cudaSurfaceObject_t)params.shaftSurf, int(idx.x * sizeof(float4)), int(idx.y), k);
        t0 = t1;
    }
}

// =====================================================================================================
// load-time bakes
// =====================================================================================================
// Hammersley point k of n, cosine-weighted around n
static __forceinline__ __device__ float3 cosineDir(unsigned int k, unsigned int n, float3 nrm, float rot) {
    unsigned int b = k;
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    const float u1 = (k + 0.5f) / float(n), u2 = fracf(float(b) * 2.3283064365386963e-10f + rot);
    const float r = sqrtf(u1), phi = 2.0f * M_PIf * u2;
    const Frame f = Frame::from(nrm);
    return f.toWorld(make_float3(r * cosf(phi), r * sinf(phi), sqrtf(fmaxf(0.0f, 1.0f - u1))));
}

extern "C" __global__ void __raygen__bakeAO() {
    const unsigned int v = optixGetLaunchIndex().x;
    const KMesh& m = params.meshes[params.bakeMesh];
    const KInstance in = params.instances[params.bakeInstance];
    const float3 p = xformP(in, m.pos[v]);
    const float3 n = normalize(xformV(in, m.nrm[v]));
    const unsigned int R = 128;
    float open = 0.0f;
    const float rot = (hashU32(v) & 0xFFFF) * (1.0f / 65536.0f);
    for (unsigned int k = 0; k < R; ++k) {
        const float3 d = cosineDir(k, R, n, rot);
        if (visible(offsetRay(p, n), d, 4.0f)) open += 1.0f;
    }
    params.bakeOut[v] = open / float(R);
}

// a sun map's texel, from its point on the still surface: the air pass, 1 if the sun is in sight through the air (the
// pier's deck); the depth pass, how far down the refracted sun goes before something under the water stops it (or 400 m)
extern "C" __global__ void __raygen__bakeSun() {
    const uint3 idx = optixGetLaunchIndex();
    const float3 s = make_float3(params.bakeMapMin.x + (idx.x + 0.5f) * params.bakeMapTexel, 0.0f, params.bakeMapMin.y + (idx.y + 0.5f) * params.bakeMapTexel);
    float v;
    if (params.bakeMapAir) {
        v = visible(s, params.bakeMapAir == 2 ? make_float3(0.0f, 1.0f, 0.0f) : params.sunAir, 400.0f) ? 1.0f : 0.0f;
    } else {
        const Hit h = trace(s + make_float3(0.0f, -0.001f, 0.0f), params.sunWater * -1.0f, 400.0f);
        v = h.hit ? h.t : 400.0f;
    }
    params.bakeOut[size_t(idx.y) * params.bakeMapW + idx.x] = v;
}
