// Scene 12 "Ateş": the OptiX programs. Compiled at run time by NVRTC (F5 recompiles).
//   __raygen__render   the surfaces: the stones, the ground, the logs (their char, ash and glow from the wood's state),
//                      lit by the fire (one sample of its light a pixel a frame, from the gas's emission, with a shadow
//                      ray: DLSS-RR gathers the frames) and by the night; the DLSS guides
//   __raygen__volume   the fire over the surfaces' picture: the gas marched along each pixel's ray, deterministic (no
//                      random step), the soot's emission and absorption (Planck's law at its temperature), the flame
//                      front's own blue, the heat haze (the hot air's refraction bends the ray to what is behind)
//   __raygen__final    exposure, glare, AgX, sRGB
#include <optix.h>

#include "scenes/ates/ates_shared.h"
#include "shared/display.h"

extern "C" {
__constant__ AtesParams params;
}

static __forceinline__ __device__ float3 xfPoint(const float m[12], float3 p) {
    return make_float3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                       m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
}
static __forceinline__ __device__ float3 xfVector(const float m[12], float3 v) {
    return make_float3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z, m[8] * v.x + m[9] * v.y + m[10] * v.z);
}
static __forceinline__ __device__ float3 lerp3b(float3 a, float3 b, float3 c, float2 w) { return a * (1.0f - w.x - w.y) + b * w.x + c * w.y; }
static __forceinline__ __device__ float2 lerp2b(float2 a, float2 b, float2 c, float2 w) { return a * (1.0f - w.x - w.y) + b * w.x + c * w.y; }
static __forceinline__ __device__ unsigned int hash32(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
static __forceinline__ __device__ float u01(unsigned int h) { return float(h >> 8) * (1.0f / 16777216.0f); }

extern "C" __global__ void __anyhit__none() {}

// ---------------------------------------------------------------------------------------------------------------
struct Hit {
    bool hit;
    float t;
    unsigned int inst, prim, input;
    float2 b;
    float m[12];
};

static __forceinline__ __device__ Hit trace(float3 o, float3 d, float tmin, float tmax) {
    unsigned int p0 = 0, p1 = 0, p2 = 0, p3 = 0;
    optixTraverse(params.handle, o, d, tmin, tmax, 0.0f, OptixVisibilityMask(255), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0, p0, p1, p2, p3);
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

static __forceinline__ __device__ bool occluded(float3 o, float3 d, float tmax) {
    unsigned int p0 = 0, p1 = 0, p2 = 0, p3 = 0;
    optixTraverse(params.handle, o, d, 0.0f, tmax, 0.0f, OptixVisibilityMask(255),
                  OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT, 0, 1, 0, p0, p1, p2, p3);
    return optixHitObjectIsHit();
}

// the tables (ates_spectra.h) at a temperature, linear between entries
static __forceinline__ __device__ float3 table(const float3* t, float T) {
    const float x = fminf(fmaxf((T - ATES_TABLE_T0) / ATES_TABLE_DT, 0.0f), float(ATES_TABLE_N - 1) - 0.001f);
    const int i = int(x);
    const float f = x - i;
    return t[i] * (1.0f - f) + t[i + 1] * f;
}

// ---------------------------------------------------------------------------------------------------------------
struct Surf {
    float3 p, ng, n;
    float3 albedo;
    float3 emit;              // its own light (an ember): cd/m^2
    float rough;
};

// value noise in [0, 1): a hash at the lattice's corners, blended smoothly between them
static __forceinline__ __device__ float vnoise(float3 p, unsigned int seed) {
    const float fx = floorf(p.x), fy = floorf(p.y), fz = floorf(p.z);
    const int ix = int(fx), iy = int(fy), iz = int(fz);
    const float tx = p.x - fx, ty = p.y - fy, tz = p.z - fz;
    const float wx = tx * tx * (3.0f - 2.0f * tx), wy = ty * ty * (3.0f - 2.0f * ty), wz = tz * tz * (3.0f - 2.0f * tz);
    float c[8];
    for (int k = 0; k < 8; ++k)
        c[k] = u01(hash32(unsigned(ix + (k & 1)) * 73856093u ^ unsigned(iy + ((k >> 1) & 1)) * 19349663u ^ unsigned(iz + (k >> 2)) * 83492791u ^ seed));
    const float x0 = c[0] + (c[1] - c[0]) * wx, x1 = c[2] + (c[3] - c[2]) * wx, x2 = c[4] + (c[5] - c[4]) * wx, x3 = c[6] + (c[7] - c[6]) * wx;
    const float y0 = x0 + (x1 - x0) * wy, y1 = x2 + (x3 - x2) * wy;
    return y0 + (y1 - y0) * wz;
}

// a block's slope at d from a crack's lip: its top is a pillow, flat at its middle (R in from the lip) and falling
// into the crack at ~50 degrees
static __forceinline__ __device__ float bevel(float d, float R) {
    const float x = fmaxf(1.0f - fmaxf(d, 0.0f) / R, 0.0f);
    return 1.2f * x * x;
}

// a log's surface: the char's checks, its grain, the ash, the glow of its temperature. `uv` the side's (metres around
// at the first radius, metres along from the middle), `lp` the point in the log's own space (its axis along x), `axis`
// that axis in the world, `width` the pixel's footprint (m)
static __forceinline__ __device__ void logSurface(const ALogView& L, float2 uv, float3 lp, bool end, float3 axis, float width, Surf& s) {
    // the place around: the side's angle (stable as the surface burns in), on an end the point's own
    float th = uv.x / L.r0;
    float rho = L.r0;
    if (end) {
        th = atan2f(lp.z, lp.y);
        if (th < 0.0f) th += 6.2831853f;
        rho = sqrtf(lp.y * lp.y + lp.z * lp.z);
    }
    const float v = end ? (lp.x > 0.0f ? 0.5f : -0.5f) * L.len : uv.y;
    // the patches' state, bilinear between their centres (around: wrapped; along: clamped)
    const float gu = th / 6.2831853f * L.nTheta - 0.5f, gv = fminf(fmaxf((v / L.len + 0.5f) * L.nZ - 0.5f, 0.0f), float(L.nZ - 1));
    const int u0 = int(floorf(gu)), v0 = min(int(gv), L.nZ - 1);
    const float au = gu - floorf(gu), av = gv - float(v0);
    const int ua = (u0 % L.nTheta + L.nTheta) % L.nTheta, ub = (ua + 1) % L.nTheta, v1 = min(v0 + 1, L.nZ - 1);
    const unsigned int p00 = L.firstPatch + unsigned(v0 * L.nTheta + ua), p01 = L.firstPatch + unsigned(v0 * L.nTheta + ub),
                       p10 = L.firstPatch + unsigned(v1 * L.nTheta + ua), p11 = L.firstPatch + unsigned(v1 * L.nTheta + ub);
    auto bil = [&](const float* f) { return (f[p00] * (1.0f - au) + f[p01] * au) * (1.0f - av) + (f[p10] * (1.0f - au) + f[p11] * au) * av; };
    const float T = bil(L.surfT), charD = bil(L.charD), ash = bil(L.ash);
    // char: black where it is deeper than the bark's first fraction of a millimetre
    const float charred = smoothstepf(0.0002f, 0.0015f, charD);
    // the checks (the alligator pattern): the char shrinks most along the grain, so its widest cracks run across it,
    // rings around the log; between two rings shorter fissures along the grain cut the band into blocks, staggered
    // from band to band, a fifth of them missing (two blocks as one); the lines wander. Their size is the log's (set
    // when the char first split, it does not crawl as it deepens), their width grows with the char's depth. A coal (a
    // short piece of the bed) breaks into few, broad facets
    const bool coal = L.len < 0.15f;
    const float cell = coal ? fminf(fmaxf(0.75f * L.r0, 0.009f), 0.018f) : fminf(fmaxf(0.3f * L.r0, 0.007f), 0.014f);
    const float pillow = coal ? 0.45f : 1.0f;
    const int ncol = max(6, int(6.2831853f * L.r0 / cell + 0.5f));
    const float wU = 6.2831853f * L.r0 / float(ncol), H = 1.1f * cell;
    // (the noise's place: around on the first radius's circle, so it closes on itself)
    const float cs = cosf(th), sn = sinf(th);
    const float3 pn = make_float3(v, L.r0 * cs, L.r0 * sn);
    const float ic = 1.0f / cell;
    // (a coal's lines wander further: its cracks run every way, not in rings)
    const float wander = coal ? 1.6f : 0.7f;
    const float u = th * L.r0 + (vnoise(pn * ic, L.seed) - 0.5f) * wander * cell;
    const float vw = v + (vnoise(pn * (coal ? 0.6f * ic : ic), L.seed ^ 0x5bd1e995u) - 0.5f) * wander * cell;
    const float aa = fmaxf(0.5f * width, 0.00015f);
    const float hw0 = fminf(0.0003f + 0.07f * charD, 0.12f * cell) * charred;
    const float wob = 0.6f + 0.8f * vnoise(pn * (3.0f * ic), L.seed ^ 0x27d4eb2fu);
    float crack, edgeD, dhdu = 0.0f, dhdv = 0.0f;   // (edgeD: the distance out from the nearest crack's lip, m)
    float pit = 1.0f;                                // the light the char's relief lets reach the point
    unsigned int blockId;
    if (!end) {
        // the band between two rings (each ring a little off its place, the same all around)
        const int k = int(floorf(vw / H));
        float lo = H * float(k), hi = H * float(k + 1);
        int kb = k;
        {
            const float jl = 0.3f * H * (u01(hash32(unsigned(k) * 0x9e3779b9u ^ L.seed)) - 0.5f);
            const float jh = 0.3f * H * (u01(hash32(unsigned(k + 1) * 0x9e3779b9u ^ L.seed)) - 0.5f);
            if (vw < lo + jl) {
                hi = lo + jl;
                lo = H * float(k - 1) + 0.3f * H * (u01(hash32(unsigned(k - 1) * 0x9e3779b9u ^ L.seed)) - 0.5f);
                kb = k - 1;
            } else if (vw >= hi + jh) {
                lo = hi + jh;
                hi = H * float(k + 2) + 0.3f * H * (u01(hash32(unsigned(k + 2) * 0x9e3779b9u ^ L.seed)) - 0.5f);
                kb = k + 1;
            } else {
                lo += jl;
                hi += jh;
            }
        }
        // the band's fissures along the grain, staggered from band to band; their index wraps around the log
        const unsigned int hb = hash32(unsigned(kb) * 2654435761u ^ L.seed);
        const float off = u01(hb) * wU;
        const int c0 = int(floorf((u - off) / wU));
        float ul = -1e9f, ur = 1e9f;
        int clo = c0 - 3;
        for (int c = c0 - 2; c <= c0 + 3; ++c) {
            const unsigned int hc = hash32(hb ^ unsigned((c % ncol + ncol) % ncol) * 0x85ebca6bu);
            if (u01(hash32(hc)) < 0.2f) continue;
            const float b = off + wU * (float(c) + 0.35f * (u01(hc) - 0.5f));
            if (b <= u && b > ul) {
                ul = b;
                clo = c;
            }
            if (b > u && b < ur) ur = b;
        }
        const float du0 = fminf(u - ul, 2.0f * wU), du1 = fminf(ur - u, 2.0f * wU);
        const float dv0 = vw - lo, dv1 = hi - vw;
        const float hwT = hw0 * wob * (coal ? 1.0f : 1.25f), hwL = hw0 * wob * (coal ? 1.0f : 0.7f);
        const float e = fminf(fminf(du0, du1) - hwL, fminf(dv0, dv1) - hwT);
        edgeD = e;
        // (a crack narrower than the pixel covers its share of it)
        crack = (1.0f - smoothstepf(-aa, aa, e)) * fminf((fminf(du0, du1) - hwL < fminf(dv0, dv1) - hwT ? hwL : hwT) / aa, 1.0f);
        // the block's top: flat, rounded off at its lips; each block leans its own way a little
        const float Ru = 0.3f * (du0 + du1), Rv = 0.3f * (dv0 + dv1);
        blockId = hash32(hb ^ unsigned((clo % ncol + ncol) % ncol) * 0x27d4eb2fu);
        dhdu = ((bevel(du0 - hwL, Ru) - bevel(du1 - hwL, Ru)) * pillow + 0.3f * (u01(blockId) - 0.5f)) * charred;
        dhdv = ((bevel(dv0 - hwT, Rv) - bevel(dv1 - hwT, Rv)) * pillow + 0.3f * (u01(hash32(blockId)) - 0.5f)) * charred;
        // its face is not smooth: the char's own relief, a few millimetres across (two octaves of noise, its slope by
        // differences over a fifth of the finer one's wavelength)
        const float f1 = 3.0f * ic, f2 = 7.0f * ic, dd = 0.2f / f2;
        auto relief = [&](float a, float b) {
            const float t2 = th + b / L.r0;
            const float3 q = make_float3(v + a, L.r0 * cosf(t2), L.r0 * sinf(t2));
            return vnoise(q * f1, L.seed ^ 0x51ed270bu) / f1 + 0.5f * vnoise(q * f2, L.seed ^ 0x2f4a7c15u) / f2;
        };
        const float h0 = relief(0.0f, 0.0f), A = 0.8f * charred;
        dhdu += A * (relief(0.0f, dd) - h0) / dd;
        dhdv += A * (relief(dd, 0.0f) - h0) / dd;
        // (its hollows see less of the fire than its crests: the relief's height, in the coarser octave's wavelengths 0
        // to ~1.2, sets the share of the light that reaches in, 0.45 down in a hollow to 0.9 on a crest)
        pit = 1.0f - 0.55f * charred * (1.0f - fminf(h0 * f1 / 1.5f, 1.0f));
    } else {
        // an end: cracks out from the heart (every few strips' lines, each from its own depth in) and along the rings
        const int c = int(floorf(u / wU));
        const unsigned int cw = unsigned((c % ncol + ncol) % ncol);
        const float duL = (u - float(c) * wU) * rho / L.r0, duR = (float(c + 1) * wU - u) * rho / L.r0;
        const bool radL = u01(hash32(cw * 2654435761u ^ L.seed ^ 0x1234567u)) < 0.4f, radR = u01(hash32(((cw + 1) % unsigned(ncol)) * 2654435761u ^ L.seed ^ 0x1234567u)) < 0.4f;
        const float inL = L.r0 * (0.2f + 0.6f * u01(hash32(cw ^ L.seed))), inR = L.r0 * (0.2f + 0.6f * u01(hash32(((cw + 1) % unsigned(ncol)) ^ L.seed)));
        float e = 1e9f;
        if (radL && rho > inL) e = fminf(e, duL - hw0 * wob);
        if (radR && rho > inR) e = fminf(e, duR - hw0 * wob);
        const float ring = 0.35f * L.r0;
        const float rr = rho + (vnoise(pn * ic, L.seed ^ 0x2545f491u) - 0.5f) * 0.3f * ring;
        const float dr = fabsf(rr - ring * floorf(rr / ring + 0.5f));
        if (u01(hash32(unsigned(rr / ring + 0.5f) ^ cw ^ L.seed)) < 0.5f) e = fminf(e, dr - 0.7f * hw0 * wob);
        edgeD = e;
        crack = (1.0f - smoothstepf(-aa, aa, e)) * fminf(0.7f * hw0 * wob / aa, 1.0f);
        blockId = hash32(cw ^ unsigned(rr / ring) * 0x85ebca6bu ^ L.seed);
    }
    // the grain: fibres along the log (fine across it, long along it); on an end its rings
    const float grain = end ? 0.5f + 0.5f * sinf(rho * (6.2831853f / 0.003f) + 4.0f * vnoise(pn * ic, L.seed ^ 0x68e31da4u))
                            : vnoise(make_float3(v * (1.0f / 0.03f), pn.y * (1.0f / 0.0012f), pn.z * (1.0f / 0.0012f)), L.seed ^ 0x68e31da4u);
    if (!end && (dhdu != 0.0f || dhdv != 0.0f)) {
        float3 tu = cross(axis, s.n);
        const float tl = length(tu);
        if (tl > 1e-4f) {
            tu = tu * (1.0f / tl);
            const float3 tv = cross(s.n, tu);
            const float3 nb = normalize(s.n - tu * dhdu - tv * dhdv);
            if (dot(nb, s.ng) > 0.05f) s.n = nb;
        }
    }
    // the char's tone: block to block a little, the grain in it, the cracks' depths dark
    const float tone = 0.75f + 0.5f * u01(hash32(blockId ^ 0x3c6ef372u));
    const float3 charA = make_float3(0.038f, 0.035f, 0.033f) * (tone * (0.85f + 0.3f * grain) * pit);
    s.albedo = s.albedo * (1.0f - charred) + charA * charred;
    s.albedo = s.albedo * (1.0f - 0.75f * crack);
    // the ash: a pale skin on the blocks' tops first (none down in the cracks), in patches
    const float ashCover = smoothstepf(0.00002f, 0.00012f, ash * (0.3f + 1.4f * vnoise(pn * (0.7f * ic), L.seed ^ 0x165667b1u))) * (1.0f - crack);
    s.albedo = s.albedo * (1.0f - ashCover) + make_float3(0.5f, 0.48f, 0.45f) * (ashCover * (0.8f + 0.4f * grain));
    // (char's microsurface is very rough: its whole reflectance, gloss and all, is a few per cent, so the gloss is a
    // faint broad sheen, not a highlight; ash is matt)
    s.rough = (s.rough * (1.0f - charred) + 0.85f * charred) * (1.0f - ashCover) + 0.95f * ashCover;
    // the glow: a grey body at its temperature (emissivity 0.9). A crack is a cavity: its walls see mostly each other,
    // their loss to the open is the opening's share F of a flat face's, so under the same heat they run hotter by
    // F^(-1/4) (F ~ 0.53 for a crack as deep as it is wide: x1.17) and radiate as a black body. The face's temperature
    // wanders a little within a patch (the patch is a centimetre; the ash, the draught vary inside it)
    // The lips share the walls' heat (it conducts out over about the crack's own width), and a crack is as deep as
    // the char let it split: some run deep, some are shallow
    const float lip = __expf(-fmaxf(edgeD, 0.0f) / (1.5f * hw0 + aa));
    const float deep = 0.35f + 0.65f * smoothstepf(0.3f, 0.7f, vnoise(pn * (1.3f * ic), L.seed ^ 0x2c1b3c6du));
    const float cavity = fmaxf(crack, 0.6f * lip * charred) * deep * smoothstepf(0.001f, 0.004f, charD);
    const float mottle = 1.0f + 0.05f * (vnoise(pn * (1.6f * ic), L.seed ^ 0x7f4a7c15u) - 0.5f);
    const float Te = T * mottle * (1.0f + 0.17f * cavity);
    const float through = 1.0f - 0.8f * ashCover;
    if (Te > 650.0f) s.emit = table(params.greyEmit, Te) * ((0.9f + 0.08f * cavity) * through);
}

static __forceinline__ __device__ Surf surface(const Hit& h, float3 d, float width) {
    const AInst& in = params.instances[h.inst];
    const OGeom& g = params.geoms[in.firstGeom + h.input];
    const OMaterial& m = params.materials[g.material];
    const uint3 tri = g.idx[h.prim];
    const float3 q0 = g.pos[tri.x], q1 = g.pos[tri.y], q2 = g.pos[tri.z];
    const float3 p0 = xfPoint(h.m, q0), p1 = xfPoint(h.m, q1), p2 = xfPoint(h.m, q2);
    Surf s;
    s.p = lerp3b(p0, p1, p2, h.b);
    s.emit = make_float3(0, 0, 0);
    const float3 cr = cross(p1 - p0, p2 - p0);
    const float crl = length(cr);
    float3 n = g.nrm ? xfVector(h.m, lerp3b(g.nrm[tri.x], g.nrm[tri.y], g.nrm[tri.z], h.b)) : cr;
    const float nl = length(n);
    n = nl > 1e-12f ? n * (1.0f / nl) : (crl > 1e-20f ? cr * (1.0f / crl) : -d);
    float3 ng = crl > 1e-20f ? cr * (1.0f / crl) : n;
    if (dot(n, ng) < 0.0f) ng = -ng;
    if (dot(ng, d) > 0.0f) {
        ng = -ng;
        n = -n;
    }
    const float2 t0 = g.uv[tri.x], t1 = g.uv[tri.y], t2 = g.uv[tri.z];
    const float2 uvRaw = lerp2b(t0, t1, t2, h.b);
    const float2 uv = uvRaw * g.uvScale;
    // the texture's level from the footprint over the triangle's texel density
    const float uvArea = fabsf((t1.x - t0.x) * (t2.y - t0.y) - (t2.x - t0.x) * (t1.y - t0.y)) * g.uvScale * g.uvScale;
    const float wArea = crl * 0.5f;
    const float texels = uvArea * m.texelArea / fmaxf(wArea, 1e-12f);
    const float cosT = fmaxf(fabsf(dot(ng, d)), 0.1f);
    const float lod = fmaxf(0.0f, 0.5f * log2f(fmaxf(texels * width * width / cosT, 1e-12f)) + params.lodBias);
    float3 albedo = m.tint * in.tint;
    if (m.albedo) {
        const float4 a = tex2DLod<float4>(cudaTextureObject_t(m.albedo), uv.x, uv.y, lod);
        albedo = make_float3(a.x, a.y, a.z) * m.tint * in.tint;
    }
    s.rough = m.roughness;
    if (m.arm) s.rough = tex2DLod<float4>(cudaTextureObject_t(m.arm), uv.x, uv.y, lod).y;
    // the ground away from the pit: the second material, blended in by distance (the burnt ground is the pit's)
    if (in.log < 0 && g.material == params.groundMaterial && params.groundMaterial2 >= 0) {
        const float r = sqrtf(s.p.x * s.p.x + s.p.z * s.p.z);
        const float w = smoothstepf(params.pitRadius, params.pitRadius * 1.8f, r);
        if (w > 0.0f) {
            const OMaterial& m2 = params.materials[params.groundMaterial2];
            if (m2.albedo) {
                const float4 a = tex2DLod<float4>(cudaTextureObject_t(m2.albedo), uv.x, uv.y, lod);
                albedo = albedo * (1.0f - w) + make_float3(a.x, a.y, a.z) * m2.tint * w;
            }
        }
    }
    float relief = 1.0f;
    if (in.log >= 0) {
        const ALogView& Lv = params.logs[in.log];
        const float fu = uvRaw.x / (6.2831853f * Lv.r0), fv = uvRaw.y / Lv.len + 0.5f;
        const int it = min(max(int(fu * Lv.nTheta), 0), Lv.nTheta - 1), iz = min(max(int(fv * Lv.nZ), 0), Lv.nZ - 1);
        relief = 1.0f - 0.85f * smoothstepf(0.0002f, 0.0015f, Lv.charD[Lv.firstPatch + unsigned(iz * Lv.nTheta + it)]);
    }
    if (m.normal) {
        const float2 du1 = t1 - t0, du2 = t2 - t0;
        const float det = du1.x * du2.y - du1.y * du2.x;
        if (fabsf(det) > 1e-12f) {
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
                const float4 nm = tex2DLod<float4>(cudaTextureObject_t(m.normal), uv.x, uv.y, lod);
                const float3 tn = make_float3((nm.x * 2.0f - 1.0f) * relief, -(nm.y * 2.0f - 1.0f) * relief, nm.z * 2.0f - 1.0f);
                n = normalize(T * tn.x + Bn * tn.y + n * fmaxf(tn.z, 0.05f));
            }
        }
    }
    if (dot(n, d) > -0.02f) n = normalize(n - d * (dot(n, d) + 0.02f));
    s.n = n;
    s.ng = ng;
    s.albedo = albedo;
    if (in.log >= 0) {
        const float3 axis = normalize(make_float3(h.m[0], h.m[4], h.m[8]));
        logSurface(params.logs[in.log], uvRaw, lerp3b(q0, q1, q2, h.b), h.input == 1, axis, width, s);
    }
    return s;
}

// ---------------------------------------------------------------------------------------------------------------
// the fire's light: a block of the gas picked by its share of the light (the cumulative sum, binary search), a point in
// it, a shadow ray. Returns the illuminance (lux, RGB) times the cosine at the surface, divided by the pick's chance
static __forceinline__ __device__ float3 fireLight(const Surf& s, float u1, float2 u2, float3& Ldir) {
    if (params.lightN <= 0) return make_float3(0, 0, 0);
    const float total = params.lightCdf[params.lightN - 1];
    if (total <= 0.0f) return make_float3(0, 0, 0);
    const float target = u1 * total;
    int lo = 0, hi = params.lightN - 1;
    while (lo < hi) {
        const int mid = (lo + hi) >> 1;
        if (params.lightCdf[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    const float prev = lo > 0 ? params.lightCdf[lo - 1] : 0.0f;
    const float pick = (params.lightCdf[lo] - prev) / total;
    if (pick <= 0.0f) return make_float3(0, 0, 0);
    const int ix = lo % params.lnx, iy = (lo / params.lnx) % params.lny, iz = lo / (params.lnx * params.lny);
    const float3 cell = params.lightOrigin + make_float3(ix, iy, iz) * params.lightCell;
    const float3 lp = cell + make_float3(u2.x, 0.5f, u2.y) * params.lightCell;
    float3 L = lp - s.p;
    const float r2 = dot(L, L);
    const float r = sqrtf(r2);
    L = L * (1.0f / fmaxf(r, 1e-6f));
    Ldir = L;
    const float cosn = dot(s.n, L), cosg = dot(s.ng, L);
    if (cosn <= 0.0f || cosg <= 0.0f) return make_float3(0, 0, 0);
    if (occluded(s.p + s.ng * 0.002f, L, r * 0.999f)) return make_float3(0, 0, 0);
    // the block as a small ball: no point nearer than half its side (an ember inside the flame is lit by it, not by a
    // singular point)
    const float soft = 0.25f * params.lightCell * params.lightCell;
    return params.lightI[lo] * (cosn / ((r2 + soft) * pick));
}

static __forceinline__ __device__ float3 sky(float3 d) {
    const float up = fmaxf(d.y, 0.0f);
    return params.skyHorizon + (params.skyZenith - params.skyHorizon) * sqrtf(up);
}

// a dielectric's gloss (F0 0.04) under one light: GGX, Smith's height-correlated visibility, Schlick's Fresnel
static __forceinline__ __device__ float gloss(float3 n, float3 wo, float3 wi, float rough) {
    const float NoL = dot(n, wi), NoV = dot(n, wo);
    if (NoL <= 0.0f || NoV <= 0.0f) return 0.0f;
    const float3 hv = normalize(wi + wo);
    const float NoH = fmaxf(dot(n, hv), 0.0f), VoH = fmaxf(dot(wo, hv), 0.0f);
    const float a = fmaxf(rough * rough, 0.002f), a2 = a * a;
    const float dd = NoH * NoH * (a2 - 1.0f) + 1.0f;
    const float D = a2 / (3.14159265f * dd * dd);
    const float Vis = 0.5f / (NoL * sqrtf(NoV * NoV * (1.0f - a2) + a2) + NoV * sqrtf(NoL * NoL * (1.0f - a2) + a2));
    const float f = 1.0f - VoH, f2 = f * f;
    return D * Vis * (0.04f + 0.96f * f2 * f2 * f);
}

static __forceinline__ __device__ float3 shade(const Surf& s, float3 wo, float u1, float2 u2) {
    if (params.debug == 1) return s.albedo;
    if (params.debug == 2) return s.emit;
    float3 Ld = make_float3(0, 1, 0);
    const float3 E = fireLight(s, u1, u2, Ld);
    // the night's light: a faint sky over everything open (no shadow: it is a few thousandths of a candela)
    const float3 amb = make_float3(params.ambient, params.ambient, params.ambient) * (0.5f + 0.5f * s.n.y);
    return s.albedo * (E * INV_PIf + amb) + E * gloss(s.n, wo, Ld, s.rough) + s.emit;
}

// ---------------------------------------------------------------------------------------------------------------
static __forceinline__ __device__ float specAlbedo(float NoV, float rough) {
    const float rx = -rough + 1.0f, ry = -0.0275f * rough + 0.0425f, rz = -0.572f * rough + 1.04f, rw = 0.022f * rough - 0.04f;
    const float a004 = fminf(rx * rx, exp2f(-9.28f * NoV)) * rx + ry;
    return 0.04f * (-1.04f * a004 + rz) + (1.04f * a004 + rw);
}
static __forceinline__ __device__ bool prevPixel(float3 q, float2& pix) {
    const float z = dot(q, params.prevW);
    if (z <= 1e-6f) return false;
    const float x = dot(q, params.prevU) / (dot(params.prevU, params.prevU) * z);
    const float y = dot(q, params.prevV) / (dot(params.prevV, params.prevV) * z);
    pix = make_float2((x + 1.0f) * 0.5f * float(params.width), (1.0f - y) * 0.5f * float(params.height));
    return true;
}

// the sample's place in the light's sequence: this frame's, turned by the pixel's place in a 4 x 4 Bayer tile (every
// tile holds all sixteen each frame: noise DLSS gathers, not a flicker of the whole picture)
static __forceinline__ __device__ void lightSample(unsigned int px, unsigned int py, unsigned int k, float& u1, float2& u2) {
    const unsigned int h = hash32(px * 1973u + py * 9277u);
    // a rank-1 lattice over the sixteen phases, scrambled per pixel by a fixed shift (the same every frame: no crawl)
    const float g1 = 0.7548776662f, g2 = 0.5698402910f;
    u1 = fmodf(u01(h) + float(k) * 0.6180339887f, 1.0f);
    u2 = make_float2(fmodf(u01(hash32(h)) + float(k) * g1, 1.0f), fmodf(u01(hash32(h ^ 0x9e3779b9u)) + float(k) * g2, 1.0f));
}

extern "C" __global__ void __raygen__render() {
    const uint3 li = optixGetLaunchIndex();
    const unsigned int px = li.x, py = li.y, i = py * params.width + px;
    if (params.layout != sizeof(AtesParams)) {
        params.hdr[i] = make_float4(1, 0, 1, 1);
        return;
    }
    if (params.dlss) {
        const float cx = float(px) + 0.5f + params.jitter.x, cy = float(py) + 0.5f + params.jitter.y;
        const float x = cx / float(params.width) * 2.0f - 1.0f, y = 1.0f - cy / float(params.height) * 2.0f;
        const float3 d = normalize(params.W + params.U * x + params.V * y);
        const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
        const unsigned int k = (params.phase + unsigned(bayer[(py & 3u) * 4u + (px & 3u)])) % ATES_PHASES;
        const Hit h = trace(params.eye, d, 0.01f, 1e4f);
        float3 c, albedo = make_float3(0.5f, 0.5f, 0.5f), n = make_float3(0, 0, 0);
        float spec = 0.0f, rough = 0.0f, depth = 1e4f, hitT = 65504.0f;
        float3 q = d;
        if (!h.hit) {
            c = sky(d);
            params.depth[i] = 1e30f;
        } else {
            const Surf s = surface(h, d, h.t * params.pixelAngle);
            float u1;
            float2 u2;
            lightSample(px, py, k, u1, u2);
            c = shade(s, -d, u1, u2);
            albedo = s.albedo;
            // the emitted light is not lit by anything: DLSS sees it as part of the albedo's response
            if (s.emit.x + s.emit.y + s.emit.z > 0.0f) albedo = albedo + make_float3(0.02f, 0.02f, 0.02f);
            const float ndv = fmaxf(dot(s.n, -d), 0.0f);
            spec = specAlbedo(ndv, s.rough);
            rough = s.rough;
            hitT = 0.0f;
            n = s.n;
            depth = h.t * dot(d, params.W);
            q = s.p - params.prevEye;
            // a log moves (it falls, it settles): its point where it was a frame ago
            const AInst& in = params.instances[h.inst];
            if (in.log >= 0) {
                const float3 dd = s.p - make_float3(h.m[3], h.m[7], h.m[11]);
                const float3 lp = make_float3(h.m[0] * dd.x + h.m[4] * dd.y + h.m[8] * dd.z, h.m[1] * dd.x + h.m[5] * dd.y + h.m[9] * dd.z,
                                              h.m[2] * dd.x + h.m[6] * dd.y + h.m[10] * dd.z);
                q = xfPoint(params.logs[in.log].prevPose, lp) - params.prevEye;
            }
            params.depth[i] = h.t;
        }
        if (!(isfinite(c.x) && isfinite(c.y) && isfinite(c.z))) {
            c = make_float3(0, 0, 0);
            if (params.badCount) atomicAdd(params.badCount, 1u);
        }
        float2 mv = make_float2(0, 0), pp;
        if (prevPixel(q, pp)) mv = make_float2(pp.x - cx, pp.y - cy);
        // DLSS keeps what it is given in its history and moves it with the motion: one value not finite among its
        // guides stays on screen as a black block that follows the surface (counted in badCount[1])
        {
            bool bad = false;
            if (!(isfinite(n.x) && isfinite(n.y) && isfinite(n.z))) { n = -d; bad = true; }
            if (!(isfinite(albedo.x) && isfinite(albedo.y) && isfinite(albedo.z))) { albedo = make_float3(0.5f, 0.5f, 0.5f); bad = true; }
            if (!isfinite(spec)) { spec = 0.04f; bad = true; }
            if (!isfinite(rough)) { rough = 0.8f; bad = true; }
            if (!(isfinite(mv.x) && isfinite(mv.y))) { mv = make_float2(0, 0); bad = true; }
            if (!isfinite(depth)) { depth = 1e4f; bad = true; }
            if (bad && params.badCount) atomicAdd(params.badCount + 1, 1u);
        }
        c = fminf3(c * params.preExposure, make_float3(6.0e4f, 6.0e4f, 6.0e4f));
        surf2Dwrite(make_float4(c.x, c.y, c.z, 1.0f), cudaSurfaceObject_t(params.gColor), int(px * sizeof(float4)), int(py));
        surf2Dwrite(make_float4(albedo.x, albedo.y, albedo.z, 1.0f), cudaSurfaceObject_t(params.gAlbedo), int(px * sizeof(float4)), int(py));
        surf2Dwrite(make_float4(spec, spec, spec, 1.0f), cudaSurfaceObject_t(params.gSpecular), int(px * sizeof(float4)), int(py));
        surf2Dwrite(make_float4(n.x, n.y, n.z, 0.0f), cudaSurfaceObject_t(params.gNormal), int(px * sizeof(float4)), int(py));
        surf2Dwrite(rough, cudaSurfaceObject_t(params.gRough), int(px * sizeof(float)), int(py));
        surf2Dwrite(depth, cudaSurfaceObject_t(params.gDepth), int(px * sizeof(float)), int(py));
        surf2Dwrite(mv, cudaSurfaceObject_t(params.gMotion), int(px * sizeof(float2)), int(py));
        surf2Dwrite(hitT, cudaSurfaceObject_t(params.gHitT), int(px * sizeof(float)), int(py));
        return;
    }
    // without DLSS: the fixed sub-pixel samples, each with its own light sample (the same every frame)
    float3 acc = make_float3(0, 0, 0);
    float firstT = 1e30f;
    for (int k = 0; k < params.spp; ++k) {
        const float2 o = params.pattern[k];
        const float x = ((float(px) + 0.5f + o.x) / float(params.width)) * 2.0f - 1.0f;
        const float y = 1.0f - ((float(py) + 0.5f + o.y) / float(params.height)) * 2.0f;
        const float3 d = normalize(params.W + params.U * x + params.V * y);
        const Hit h = trace(params.eye, d, 0.01f, 1e4f);
        if (!h.hit) {
            acc += sky(d);
            continue;
        }
        if (k == 0) firstT = h.t;
        const Surf s = surface(h, d, h.t * params.pixelAngle);
        float u1;
        float2 u2;
        lightSample(px, py, unsigned(k), u1, u2);
        const float3 c = shade(s, -d, u1, u2);
        if (isfinite(c.x) && isfinite(c.y) && isfinite(c.z)) acc += c;
    }
    acc *= 1.0f / float(params.spp);
    params.hdr[i] = make_float4(acc.x, acc.y, acc.z, 1.0f);
    params.depth[i] = firstT;
}

// ---------------------------------------------------------------------------------------------------------------
// the fire over the picture

static __forceinline__ __device__ bool boxHit(float3 o, float3 d, float3 lo, float3 hi, float& t0, float& t1) {
    const float3 inv = make_float3(1.0f / d.x, 1.0f / d.y, 1.0f / d.z);
    const float3 a = (lo - o) * inv, b = (hi - o) * inv;
    const float3 mn = fminf3(a, b), mx = fmaxf3(a, b);
    t0 = fmaxf(fmaxf(mn.x, mn.y), fmaxf(mn.z, 0.0f));
    t1 = fminf(fminf(mx.x, mx.y), mx.z);
    return t1 > t0;
}

static __forceinline__ __device__ float4 gasAt(float3 p) {
    const float3 g = (p - params.gasOrigin) * (1.0f / params.gasH);
    return tex3D<float4>(cudaTextureObject_t(params.gasTex), g.x, g.y, g.z);
}

// the smoke's fluence at a point (lux, RGB): its coarse grid, trilinear
static __forceinline__ __device__ float3 smokeLightAt(float3 p) {
    const float3 c = (p - params.gasOrigin) * (1.0f / params.smokeCell) - make_float3(0.5f, 0.5f, 0.5f);
    const float x = fminf(fmaxf(c.x, 0.0f), float(params.snx - 1) - 1e-3f), y = fminf(fmaxf(c.y, 0.0f), float(params.sny - 1) - 1e-3f),
                z = fminf(fmaxf(c.z, 0.0f), float(params.snz - 1) - 1e-3f);
    const int i = int(x), j = int(y), k = int(z);
    const float a = x - i, b = y - j, e = z - k;
    auto at = [&](int ii, int jj, int kk) {
        const float4 v = params.smokeLight[ii + params.snx * (jj + params.sny * kk)];
        return make_float3(v.x, v.y, v.z);
    };
    const float3 c00 = at(i, j, k) * (1 - a) + at(i + 1, j, k) * a, c10 = at(i, j + 1, k) * (1 - a) + at(i + 1, j + 1, k) * a;
    const float3 c01 = at(i, j, k + 1) * (1 - a) + at(i + 1, j, k + 1) * a, c11 = at(i, j + 1, k + 1) * (1 - a) + at(i + 1, j + 1, k + 1) * a;
    return (c00 * (1 - b) + c10 * b) * (1 - e) + (c01 * (1 - b) + c11 * b) * e;
}

extern "C" __global__ void __raygen__volume() {
    const uint3 li = optixGetLaunchIndex();
    const unsigned int px = li.x, py = li.y, i = py * params.width + px;
    const float x = (float(px) + 0.5f) / float(params.width) * 2.0f - 1.0f, y = 1.0f - (float(py) + 0.5f) / float(params.height) * 2.0f;
    const float3 d = normalize(params.W + params.U * x + params.V * y);
    float t0, t1;
    const float4 base = params.surf[i];
    if (!params.gasTex || !boxHit(params.eye, d, params.gasOrigin, params.gasOrigin + params.gasSize, t0, t1)) {
        params.hdr[i] = base;
        return;
    }
    const float depth = params.depth[i];
    t1 = fminf(t1, depth);
    float3 L = make_float3(0, 0, 0), Tr = make_float3(1, 1, 1);
    // the heat haze: the ray's bend, the integral of the refractive index's gradient across it (n - 1 = 2.93e-4 T0/T)
    float3 bend = make_float3(0, 0, 0);
    const float ds = params.march;
    // a fixed grid of steps along the ray from its entry (the same each frame for a still eye: no crawl)
    const float tA = t0 + 0.5f * ds;
    const float eh = params.gasH, bs = ATES_BRICK * eh, ibs = 1.0f / bs;
    const float3 id = make_float3(1.0f / d.x, 1.0f / d.y, 1.0f / d.z);
    int n = 0;
    for (int it = 0; it < 4096; ++it, ++n) {
        const float t = tA + float(n) * ds;
        if (t >= t1) break;
        const float3 p = params.eye + d * t;
        // an empty brick (nothing hot, no soot, no flame front in reach of its samples) is passed in one go, to the
        // first step of the ray's grid beyond it: the same samples are taken as without the skip
        const float3 gb = (p - params.gasOrigin) * ibs;
        const int bx = min(max(int(gb.x), 0), params.bnx - 1), by = min(max(int(gb.y), 0), params.bny - 1), bz = min(max(int(gb.z), 0), params.bnz - 1);
        if (params.gasBricks && params.gasBricks[bx + params.bnx * (by + params.bny * bz)] == 0u) {
            const float3 lo = params.gasOrigin + make_float3(float(bx), float(by), float(bz)) * bs;
            const float tx = ((d.x > 0.0f ? lo.x + bs : lo.x) - params.eye.x) * id.x, ty = ((d.y > 0.0f ? lo.y + bs : lo.y) - params.eye.y) * id.y,
                        tz = ((d.z > 0.0f ? lo.z + bs : lo.z) - params.eye.z) * id.z;
            const float te = fminf(fminf(tx, ty), tz);
            n = max(n, int(ceilf((te - tA) / ds)) - 1);   // (the loop's ++n steps to the first sample past te)
            continue;
        }
        const float4 g = gasAt(p);
        const float T = g.x, fv = g.y, sm = g.z, q = g.w;
        if (T < 330.0f && fv < 1e-4f && sm < 1e-3f) continue;
        if (fv > 1e-5f) {
            const float3 jE = table(params.sootEmit, T) * fv;
            const float3 a = params.sootAbs * fv;
            const float3 att = make_float3(__expf(-a.x * ds), __expf(-a.y * ds), __expf(-a.z * ds));
            // emission over the step with its own absorption: j (1 - e^{-a ds}) / a
            const float3 em = make_float3(a.x > 1e-6f ? jE.x * (1.0f - att.x) / a.x : jE.x * ds, a.y > 1e-6f ? jE.y * (1.0f - att.y) / a.y : jE.y * ds,
                                          a.z > 1e-6f ? jE.z * (1.0f - att.z) / a.z : jE.z * ds);
            L += Tr * em;
            Tr *= att;
        }
        if (q > 0.01f) L += Tr * params.chemEmit * (q * 1e6f * ds);
        if (sm > 1e-3f && params.smokeExt > 0.0f) {
            // the smoke: it scatters the fire's light (evenly every way: J = albedo sigma E / 4 pi) and dims what is
            // behind it; it thins out over the grid's top 10 cm, where the plume leaves the grid (a flat lid else)
            const float top = params.gasOrigin.y + params.gasSize.y;
            const float sig = params.smokeExt * sm * fminf(fmaxf((top - p.y) * 10.0f, 0.0f), 1.0f);
            if (sig > 1e-6f) {
                const float att = __expf(-sig * ds);
                L += Tr * smokeLightAt(p) * (params.smokeAlbedo * (1.0f / 12.566371f) * (1.0f - att));
                Tr *= att;
            }
        }
        if (T > 340.0f && params.hazeScale > 0.0f) {
            const float Tx = gasAt(p + make_float3(eh, 0, 0)).x, Ty = gasAt(p + make_float3(0, eh, 0)).x, Tz = gasAt(p + make_float3(0, 0, eh)).x;
            const float3 gT = make_float3(Tx - T, Ty - T, Tz - T) * (1.0f / eh);
            const float3 gn = gT * (-2.93e-4f * 288.0f / (T * T));
            bend += (gn - d * dot(gn, d)) * ds;
        }
        if (Tr.x + Tr.y + Tr.z < 3e-3f) break;
    }
    // what is behind, seen through the bend: the surfaces' picture at the shifted place (bilinear)
    float4 behind = base;
    if (params.hazeScale > 0.0f && (bend.x != 0.0f || bend.y != 0.0f || bend.z != 0.0f)) {
        const float ox = dot(bend, normalize(params.U)) / params.pixelAngle * params.hazeScale;
        const float oy = -dot(bend, normalize(params.V)) / params.pixelAngle * params.hazeScale;
        const float sx = fminf(fmaxf(float(px) + ox, 0.0f), float(params.width - 1)), sy = fminf(fmaxf(float(py) + oy, 0.0f), float(params.height - 1));
        const int x0 = int(sx), y0 = int(sy), x1 = min(x0 + 1, int(params.width) - 1), y1 = min(y0 + 1, int(params.height) - 1);
        const float ax = sx - x0, ay = sy - y0;
        behind = params.surf[y0 * params.width + x0] * ((1 - ax) * (1 - ay)) + params.surf[y0 * params.width + x1] * (ax * (1 - ay)) +
                 params.surf[y1 * params.width + x0] * ((1 - ax) * ay) + params.surf[y1 * params.width + x1] * (ax * ay);
    }
    float3 c = make_float3(behind.x, behind.y, behind.z) * Tr + L;
    // (debug 3: the flame's opacity along the ray, 1 - its transmittance at green, as a grey)
    if (params.debug == 3) c = make_float3(1.0f - Tr.y, 1.0f - Tr.y, 1.0f - Tr.y);
    // nothing not finite goes on to the bloom, which would spread it into a black square (counted in badCount[2])
    if (!(isfinite(c.x) && isfinite(c.y) && isfinite(c.z))) {
        c = isfinite(base.x) && isfinite(base.y) && isfinite(base.z) ? make_float3(base.x, base.y, base.z) : make_float3(0, 0, 0);
        if (params.badCount) atomicAdd(params.badCount + 2, 1u);
    }
    params.hdr[i] = make_float4(c.x, c.y, c.z, 1.0f);
}

extern "C" __global__ void __raygen__final() {
    const uint3 li = optixGetLaunchIndex();
    const unsigned int px = li.x, py = li.y, i = py * params.width + px;
    const float4 h = params.hdr[i];
    float3 c = make_float3(h.x, h.y, h.z);
    // (what the sparks added after the fire's pass: counted in badCount[3])
    if (!(isfinite(c.x) && isfinite(c.y) && isfinite(c.z))) {
        c = make_float3(0, 0, 0);
        if (params.badCount) atomicAdd(params.badCount + 3, 1u);
    }
    if (params.bloom) {
        const int bw = int(params.width / 2), bh = int(params.height / 2);
        const float fx = fminf(fmaxf((float(px) + 0.5f) * 0.5f - 0.5f, 0.0f), float(bw - 1)), fy = fminf(fmaxf((float(py) + 0.5f) * 0.5f - 0.5f, 0.0f), float(bh - 1));
        const int x0 = int(fx), y0 = int(fy), x1 = min(x0 + 1, bw - 1), y1 = min(y0 + 1, bh - 1);
        const float ax = fx - x0, ay = fy - y0;
        const float4 b = params.bloom[y0 * bw + x0] * ((1 - ax) * (1 - ay)) + params.bloom[y0 * bw + x1] * (ax * (1 - ay)) +
                         params.bloom[y1 * bw + x0] * ((1 - ax) * ay) + params.bloom[y1 * bw + x1] * (ax * ay);
        c = c * (1.0f - params.bloomStrength) + make_float3(b.x, b.y, b.z) * params.bloomStrength;
    }
    c *= params.exposure;
    const float u = (float(px) + 0.5f) / params.width - 0.5f, v = (float(py) + 0.5f) / params.height - 0.5f;
    const float r2 = (u * u + v * v) * 2.0f;
    c *= 1.0f - params.vignette * r2 * r2 * 0.5f - params.vignette * r2 * 0.5f;
    c = agx(fmaxf3(c, make_float3(0, 0, 0)));
    if (params.contrast != 1.0f) c = make_float3(powf(c.x, params.contrast), powf(c.y, params.contrast), powf(c.z, params.contrast));
    const float Y = luminance709(c);
    c = fmaxf3(make_float3(Y, Y, Y) + (c - make_float3(Y, Y, Y)) * params.saturation, make_float3(0, 0, 0));
    const float dz = (float(hash32(px * 1973u + py * 9277u) & 1023u) / 1023.0f - 0.5f) / 255.0f;
    params.output[i] = make_uchar4((unsigned char)(srgbOetf(c.x + dz) * 255.0f + 0.5f), (unsigned char)(srgbOetf(c.y + dz) * 255.0f + 0.5f),
                                   (unsigned char)(srgbOetf(c.z + dz) * 255.0f + 0.5f), 255);
}
