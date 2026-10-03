// Scene 4 "Laboratuvar": OptiX device program, compiled at run time with NVRTC.
//   __raygen__render  a small path tracer for a high-speed photography set. Soft boxes (rectangles, sampled directly;
//                     the panel behind the experiment is seen straight on), a dark studio around. Dielectrics (glass,
//                     water, honey, juice) refract with exact Fresnel and absorb inside: the ray carries a small stack
//                     of the media it is in. A liquid resting in a glass is drawn a hair inside the glass wall (its
//                     surface comes from its particles); a ray leaving one dielectric and entering another within
//                     gapMax crosses ONE interface (glass to water), else the thin air gap would mirror (total
//                     internal reflection) where a real liquid touches its glass.
//   __raygen__pick    what is under a pixel.
#include <optix.h>
#include "scenes/laboratuvar/laboratuvar_params.h"
#include "shared/display.h"

extern "C" {
__constant__ LabParams params;
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
static __forceinline__ __device__ float schlick(float f0, float c) {
    const float m = 1.0f - fminf(fmaxf(c, 0.0f), 1.0f), m2 = m * m;
    return f0 + (1.0f - f0) * m2 * m2 * m;
}
static __forceinline__ __device__ float3 exp3(float3 v) { return make_float3(expf(v.x), expf(v.y), expf(v.z)); }

// exact Fresnel for a dielectric (unpolarised); cosI > 0, eta = n_from / n_to; 1 at total internal reflection
static __forceinline__ __device__ float fresnelDielectric(float cosI, float eta, float& cosT) {
    const float s2 = eta * eta * fmaxf(0.0f, 1.0f - cosI * cosI);
    if (s2 >= 1.0f) { cosT = 0.0f; return 1.0f; }
    cosT = sqrtf(1.0f - s2);
    const float rs = (eta * cosI - cosT) / (eta * cosI + cosT), rp = (cosI - eta * cosT) / (cosI + eta * cosT);
    return 0.5f * (rs * rs + rp * rp);
}

// value noise, 3D (a few octaves make slate, wood, rind)
static __forceinline__ __device__ float hash3(int x, int y, int z) {
    return (hashU32(unsigned(x) * 73856093u ^ unsigned(y) * 19349663u ^ unsigned(z) * 83492791u) >> 8) * (1.0f / 16777216.0f);
}
static __device__ float vnoise(float3 p) {
    const float fx = floorf(p.x), fy = floorf(p.y), fz = floorf(p.z);
    const int ix = int(fx), iy = int(fy), iz = int(fz);
    float ux = p.x - fx, uy = p.y - fy, uz = p.z - fz;
    ux = ux * ux * (3 - 2 * ux); uy = uy * uy * (3 - 2 * uy); uz = uz * uz * (3 - 2 * uz);
    float a = 0.0f;
    for (int k = 0; k < 8; ++k) {
        const int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
        a += hash3(ix + dx, iy + dy, iz + dz) * (dx ? ux : 1 - ux) * (dy ? uy : 1 - uy) * (dz ? uz : 1 - uz);
    }
    return a;
}
static __device__ float fbm(float3 p) { return 0.5f * vnoise(p) + 0.25f * vnoise(p * 2.03f) + 0.125f * vnoise(p * 4.11f) + 0.0625f * vnoise(p * 8.07f); }

struct Hit {
    bool hit;
    float t;
    float3 n;            // shading normal, outward
    int object;
    unsigned int prim;
    float3 local;        // the hit in the object's own frame (patterns move with it)
};

static __forceinline__ __device__ Hit trace(float3 o, float3 d, float tmin, float tmax) {
    optixTraverse(params.handle, o, d, tmin, tmax, 0.0f, OptixVisibilityMask(1u), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0);
    Hit h;
    h.hit = optixHitObjectIsHit();
    if (!h.hit) return h;
    h.t = optixHitObjectGetRayTmax();
    h.object = int(optixHitObjectGetInstanceId());
    h.prim = optixHitObjectGetPrimitiveIndex();
    const LObject& ob = params.objects[h.object];
    const float3 p = o + d * h.t;
    if (ob.kind == 1) {
        const float4 s = ob.spheres[h.prim];
        h.n = normalize(p - make_float3(s.x, s.y, s.z));
        h.local = p;
    } else {
        if (ob.kind == 2) h.prim = optixHitObjectGetInstanceIndex() - unsigned(ob.first);   // which box
        const uint3 tri = ob.idx[ob.kind == 2 ? optixHitObjectGetPrimitiveIndex() : h.prim];
        const float2 b = optixHitObjectGetTriangleBarycentrics();
        const float3 v0 = ob.pos[tri.x], v1 = ob.pos[tri.y], v2 = ob.pos[tri.z];
        h.local = v0 * (1.0f - b.x - b.y) + v1 * b.x + v2 * b.y;
        const float3 ng = normalize(optixHitObjectTransformNormalFromObjectToWorldSpace(cross(v1 - v0, v2 - v0)));
        float3 ns = ob.nrm ? optixHitObjectTransformNormalFromObjectToWorldSpace(ob.nrm[tri.x] * (1.0f - b.x - b.y) + ob.nrm[tri.y] * b.x + ob.nrm[tri.z] * b.y)
                           : ng;
        h.n = dot(ns, ns) > 1e-12f ? normalize(ns) : ng;
        if (dot(h.n, ng) < 0.0f) h.n = ng;
        if (ob.kind == 2) {
            // a moulded plastic cube's edges are rounded: within a few hundredths of the edge of one, the normal
            // turns towards the neighbouring face (the edge catches the light as a real cube's does)
            const float3 l = h.local;
            const float al[3] = {fabsf(l.x), fabsf(l.y), fabsf(l.z)};
            const int k = (al[0] >= al[1] && al[0] >= al[2]) ? 0 : (al[1] >= al[2] ? 1 : 2);
            float3 no = make_float3(k == 0 ? (l.x >= 0 ? 1.0f : -1.0f) : 0.0f, k == 1 ? (l.y >= 0 ? 1.0f : -1.0f) : 0.0f, k == 2 ? (l.z >= 0 ? 1.0f : -1.0f) : 0.0f);
            const float r = 0.07f;
            for (int ax = 0; ax < 3; ++ax) {
                if (ax == k) continue;
                const float e = 0.5f - al[ax];
                if (e < r) {
                    const float t = 1.0f - e / r;
                    const float sgn = (ax == 0 ? l.x : ax == 1 ? l.y : l.z) >= 0.0f ? 1.0f : -1.0f;
                    if (ax == 0) no.x += sgn * t;
                    else if (ax == 1) no.y += sgn * t;
                    else no.z += sgn * t;
                }
            }
            const float3 nw = normalize(optixHitObjectTransformNormalFromObjectToWorldSpace(no));
            h.n = dot(nw, ng) > 0.05f ? nw : ng;
        }
    }
    return h;
}

static __forceinline__ __device__ bool dielectric(int m) {
    const int t = params.materials[m].type;
    return t == LM_GLASS || t == LM_LIQUID;
}

// the soft boxes along a ray, nearer than tmax: the first one hit (one-sided), its radiance
static __forceinline__ __device__ bool lightHit(float3 o, float3 d, float tmax, bool onlyVisible, float3& Le) {
    float best = tmax;
    bool any = false;
    for (int i = 0; i < params.lightCount; ++i) {
        const LRectLight& l = params.lights[i];
        if (onlyVisible && !l.visible) continue;
        const float3 n = normalize(cross(l.u, l.v));
        const float den = dot(d, n);
        if (den >= 0.0f) continue;
        const float t = dot(l.c - o, n) / den;
        if (t <= 1e-4f || t >= best) continue;
        const float3 q = o + d * t - l.c;
        const float su = dot(q, l.u) / dot(l.u, l.u), sv = dot(q, l.v) / dot(l.v, l.v);
        if (fabsf(su) > 1.0f || fabsf(sv) > 1.0f) continue;
        best = t;
        // a soft box is a little brighter in its middle (its diffuser), as real ones are
        const float fall = 1.0f - l.fall * smoothstepf(0.0f, 1.0f, su * su + sv * sv);
        Le = l.Le * fall;
        any = true;
    }
    return any;
}

static __forceinline__ __device__ float3 ambient(float3 d) { return params.ambient * (0.6f + 0.4f * fmaxf(d.y, 0.0f)); }

// the colour of a diffuse-like hit, with its pattern
static __device__ float3 albedoAt(const LMaterial& m, const Hit& h, float3 p) {
    float3 a = m.albedo;
    if (m.pattern == LP_SLATE) {
        const float n = fbm(make_float3(p.x * 9.0f, p.z * 9.0f, 0.3f)), f = fbm(make_float3(p.x * 70.0f, p.z * 70.0f, 4.1f));
        a = a * (0.80f + 0.30f * n + 0.12f * f);
    } else if (m.pattern == LP_BACKDROP) {
        a = a * (0.9f + 0.2f * fbm(p * 3.0f));
    } else if (m.pattern == LP_WOOD) {
        const float3 q = h.local;
        const float r = sqrtf(q.x * q.x + q.z * q.z) * 160.0f + 4.0f * fbm(q * 20.0f);
        a = a * (0.75f + 0.25f * (0.5f + 0.5f * sinf(r)));
    } else if (m.pattern == LP_SAND) {
        // grains a third of a millimetre across: each cell its own brightness, a few dark (iron minerals), a few pale (quartz)
        const float g = 0.00035f;
        const int cx = int(floorf(p.x / g)), cy = int(floorf(p.y / g)), cz = int(floorf(p.z / g));
        const float r = hash3(cx, cy, cz);
        float k = 0.72f + 0.5f * hash3(cx + 7, cy - 3, cz + 11);
        if (r < 0.07f) k = 0.32f;
        else if (r > 0.94f) k = 1.35f;
        a = a * k;
    } else if (m.pattern == LP_FLESH) {
        // a melon's flesh is big juice-filled cells, a millimetre or two: each its own shade of red, paler towards the rind
        const float g = 0.0016f;
        const int cx = int(floorf(p.x / g)), cy = int(floorf(p.y / g)), cz = int(floorf(p.z / g));
        a = a * (0.8f + 0.4f * hash3(cx, cy, cz));
    } else if (m.pattern == LP_RIND) {
        // dark green with lighter stripes running pole to pole, mottled (from the object's own frame: a piece of rind
        // keeps its stripes as it flies)
        const float3 q = h.local;
        const float ang = atan2f(q.z, q.x);
        const float st = 0.5f + 0.5f * sinf(ang * 16.0f + 2.5f * fbm(q * 18.0f));
        const float mot = fbm(q * 60.0f);
        a = lerp3(a, make_float3(0.20f, 0.33f, 0.09f), smoothstepf(0.45f, 0.75f, st) * (0.6f + 0.4f * mot));
    }
    if (h.object >= 0 && params.objects[h.object].kind == 1 && params.objects[h.object].tint) a = a * params.objects[h.object].tint[h.prim];
    if (h.object >= 0 && params.objects[h.object].kind == 2 && params.objects[h.object].colour) {
        const uchar4 c = params.objects[h.object].colour[h.prim];
        a = a * make_float3(powf(c.x / 255.0f, 2.2f), powf(c.y / 255.0f, 2.2f), powf(c.z / 255.0f, 2.2f));
    }
    return a;
}

// how much of a light gets from o to o + d * tmax: straight through dielectrics and membranes (their caustics are not
// traced), stopped by anything else
static __device__ float3 shadow(float3 o, float3 d, float tmax) {
    float3 T = make_float3(1, 1, 1);
    for (int i = 0; i < 24; ++i) {
        const Hit h = trace(o, d, 1e-4f, tmax);
        if (!h.hit) return T;
        const LMaterial& m = params.materials[params.objects[h.object].material];
        const float c = fabsf(dot(h.n, d));
        if (m.type == LM_GLASS || m.type == LM_LIQUID) {
            T = T * (1.0f - schlick(0.04f, c));
            if (dot(h.n, d) > 0.0f) T = T * exp3(m.sigma * (-h.t));   // leaving it: absorbed over the stretch inside
        } else if (m.type == LM_SHELL) {
            T = T * exp3(m.sigma * (-1.0f / fmaxf(c, 0.1f))) * ((1.0f - schlick(m.f0, c)) * (1.0f - m.frost));
        } else {
            return make_float3(0, 0, 0);
        }
        if (maxc(T) < 1e-4f) return make_float3(0, 0, 0);
        o = o + d * h.t;
        tmax -= h.t;
    }
    return T;
}

// light arriving at a diffuse point straight from one soft box (one sample), over pi: times albedo = radiance
static __device__ float3 direct(float3 p, float3 n, Rng& r) {
    if (params.lightCount <= 0) return make_float3(0, 0, 0);
    const int k = min(int(r.next() * params.lightCount), params.lightCount - 1);
    const LRectLight& l = params.lights[k];
    const float su = 2.0f * r.next() - 1.0f, sv = 2.0f * r.next() - 1.0f;
    const float3 q = l.c + l.u * su + l.v * sv;
    float3 w = q - p;
    const float d2 = dot(w, w), d = sqrtf(d2);
    w = w / d;
    const float cs = dot(n, w);
    if (cs <= 0.0f) return make_float3(0, 0, 0);
    const float3 ln = cross(l.u, l.v);
    const float area4 = length(ln);
    const float cl = -dot(ln, w) / area4;
    if (cl <= 0.0f) return make_float3(0, 0, 0);
    const float3 T = shadow(p + n * 2e-5f, w, d - 4e-5f);
    const float fall = 1.0f - l.fall * smoothstepf(0.0f, 1.0f, su * su + sv * sv);
    return l.Le * T * (fall * cs * cl * 4.0f * area4 * float(params.lightCount) / d2 * INV_PIf);
}

static __forceinline__ __device__ float3 glossy(float3 d, float3 n, float e, Rng& r) {
    const float3 rr = reflect3(d, n);
    if (e >= 5000.0f) return rr;
    const float c = powf(r.next(), 1.0f / (e + 1.0f)), s = sqrtf(fmaxf(0.0f, 1.0f - c * c)), phi = 2.0f * M_PIf * r.next();
    return Frame::from(rr).toWorld(make_float3(s * cosf(phi), s * sinf(phi), c));
}

struct Media {   // the dielectrics a ray is inside, innermost last
    int m[4];
    int n;
    __device__ float ior() const { return n > 0 ? params.materials[m[n - 1]].ior : 1.0f; }
    __device__ float iorBelow(int mat) const {   // what is outside mat
        for (int i = n - 1; i >= 0; --i)
            if (m[i] == mat) return i > 0 ? params.materials[m[i - 1]].ior : 1.0f;
        return ior();
    }
    __device__ void push(int mat) { if (n < 4) m[n++] = mat; }
    __device__ void remove(int mat) {
        for (int i = n - 1; i >= 0; --i)
            if (m[i] == mat) { for (int j = i; j + 1 < n; ++j) m[j] = m[j + 1]; --n; return; }
    }
    __device__ float3 sigma() const { return n > 0 ? params.materials[m[n - 1]].sigma : make_float3(0, 0, 0); }
};

static __device__ float3 radiance(float3 o, float3 d, Rng& r, float3* aovAlb, float3* aovNrm) {
    float3 T = make_float3(1, 1, 1), L = make_float3(0, 0, 0);
    Media md;
    md.n = 0;
    bool specular = true, aovDone = false;
    bool lightCounted = false;   // this segment's soft box was already counted where it was reflected
    int diffuse = 0;
    for (int depth = 0; depth < 48; ++depth) {
        const Hit h = trace(o, d, 0.0f, 1e30f);
        if (md.n > 0) T = T * exp3(md.sigma() * (-(h.hit ? h.t : 0.0f)));
        float3 Le;
        const bool counted = lightCounted;
        lightCounted = false;
        if (specular && lightHit(o, d, h.hit ? h.t : 1e30f, depth == 0, Le)) {
            if (!counted) L = L + T * Le;
            if (!aovDone) { *aovAlb = make_float3(1, 1, 1); *aovNrm = -d; aovDone = true; }
            break;
        }
        if (!h.hit) {
            L = L + T * ambient(d);
            if (!aovDone) { *aovAlb = make_float3(0, 0, 0); *aovNrm = -d; aovDone = true; }
            break;
        }
        const float3 p = o + d * h.t;
        const int mi = params.objects[h.object].material;
        const LMaterial& m = params.materials[mi];
        float3 nn = dot(h.n, d) < 0.0f ? h.n : -h.n;   // the side the ray came from
        if (m.pattern == LP_FLESH) {   // the cells glisten each at its own tilt
            const float g = 0.0016f;
            const int cx = int(floorf(p.x / g)), cy = int(floorf(p.y / g)), cz = int(floorf(p.z / g));
            const float3 t = make_float3(hash3(cx, cy, cz + 5) - 0.5f, hash3(cx + 3, cy, cz) - 0.5f, hash3(cx, cy + 9, cz) - 0.5f);
            const float3 b = normalize(nn + t * 0.5f);
            if (dot(b, -d) > 0.05f) nn = b;
        }
        if (m.pattern == LP_SAND) {
            // each grain tilts the surface its own way: sand's matte, sparkling roughness
            const float g = 0.00035f;
            const int cx = int(floorf(p.x / g)), cy = int(floorf(p.y / g)), cz = int(floorf(p.z / g));
            const float3 t = make_float3(hash3(cx, cy, cz + 5) - 0.5f, hash3(cx + 3, cy, cz) - 0.5f, hash3(cx, cy + 9, cz) - 0.5f);
            const float3 b = normalize(nn + t * 0.9f);
            if (dot(b, -d) > 0.05f) nn = b;
        }
        const float c = fminf(1.0f, -dot(nn, d));
        if (m.type == LM_GLASS || m.type == LM_LIQUID) {
            const bool entering = dot(h.n, d) < 0.0f;
            float n1, n2;
            int into = -1;            // the medium a refracted ray ends up in (-1: out of mi, into what is around)
            float3 across = p;        // where a refracted ray continues from
            if (entering) {
                n1 = md.ior();
                n2 = m.ior;
                into = mi;
            } else {
                n1 = m.ior;
                n2 = md.iorBelow(mi);
                // leaving it: does another dielectric begin right behind (the liquid in its glass)?
                if (params.gapMax > 0.0f) {
                    const Hit g = trace(p, d, 1e-5f, params.gapMax);
                    if (g.hit && dot(g.n, d) < 0.0f && dielectric(params.objects[g.object].material) && params.objects[g.object].material != mi) {
                        into = params.objects[g.object].material;
                        n2 = params.materials[into].ior;
                        across = p + d * g.t;
                    }
                }
            }
            float cosT;
            const float F = fresnelDielectric(c, n1 / n2, cosT);
            // a soft box in this surface's mirror, met from the air on the way from the camera: counted here, weighted by F,
            // in every sample (left to the few samples that happen to reflect, a highlight on glass or liquid speckles and
            // never settles); the sample that does reflect then does not count it again
            bool mirrored = false;
            if (specular && entering && md.n == 0 && F < 1.0f) {
                const float3 rr = reflect3(d, nn), ro = p + nn * 1e-5f;
                const Hit hr = trace(ro, rr, 0.0f, 1e30f);
                float3 Lr;
                if (lightHit(ro, rr, hr.hit ? hr.t : 1e30f, false, Lr)) L = L + T * Lr * F;
                mirrored = true;
            }
            if (r.next() < F) {
                d = reflect3(d, nn);
                o = p + nn * 1e-5f;
                lightCounted = mirrored;
            } else {
                const float eta = n1 / n2;
                d = normalize(d * eta + nn * (eta * c - cosT));
                o = across - nn * 1e-5f;
                if (!entering) md.remove(mi);
                if (into >= 0) md.push(into);
            }
            continue;
        }
        if (!aovDone) {
            *aovAlb = m.type == LM_METAL ? m.albedo : albedoAt(m, h, p);
            *aovNrm = nn;
            aovDone = true;
        }
        if (m.type == LM_METAL) {
            const float3 F = m.albedo + (make_float3(1, 1, 1) - m.albedo) * schlick(0.0f, c);
            T = T * F;
            d = glossy(d, nn, m.gloss, r);
            if (dot(d, nn) <= 0.0f) break;
            o = p + nn * 1e-5f;
            continue;
        }
        if (m.type == LM_SHELL) {
            if (r.next() < schlick(m.f0, c)) {
                d = glossy(d, nn, m.gloss, r);
                if (dot(d, nn) <= 0.0f) break;
                o = p + nn * 1e-5f;
                continue;
            }
            if (r.next() >= m.frost) {   // through the membrane, tinted
                T = T * exp3(m.sigma * (-1.0f / fmaxf(c, 0.1f)));
                o = p + d * 1e-5f;
                continue;
            }
        } else if (m.f0 > 0.0f && r.next() < schlick(m.f0, c)) {   // a coat: wet flesh, a polished table
            d = glossy(d, nn, m.gloss, r);
            if (dot(d, nn) <= 0.0f) break;
            o = p + nn * 1e-5f;
            specular = m.gloss >= 2000.0f;
            continue;
        }
        // a diffuse event: the soft boxes directly, then (a few times) the rest of the room
        const float3 alb = albedoAt(m, h, p);
        L = L + T * alb * direct(p, nn, r);
        if (diffuse >= params.maxDiffuse) break;
        ++diffuse;
        T = T * alb;
        d = sampleCosine(r.next(), r.next(), nn);
        o = p + nn * 1e-5f;
        specular = false;
        if (maxc(T) < 2e-3f) break;
    }
    if (!aovDone) { *aovAlb = make_float3(0, 0, 0); *aovNrm = -d; }
    return L;
}

// the denoiser's guides and the reprojection's position, from one ray through the pixel's centre that takes the same
// way every frame: through glass and liquids it refracts (reflects only at total internal reflection), off metal it
// reflects, and it stops at the first surface that scatters. (The guides from a light-transport sample flip between
// what a glass reflects and what it shows through, at random, frame to frame: the denoiser then redraws glass and
// water differently every frame, and the picture swims.)
static __device__ void guidePath(float3 o, float3 d, float3& alb, float3& nrm, float4& firstPos) {
    firstPos = make_float4(o.x + d.x * 50.0f, o.y + d.y * 50.0f, o.z + d.z * 50.0f, 50.0f);
    Media md;
    md.n = 0;
    float len = 0.0f;
    for (int depth = 0; depth < 24; ++depth) {
        const Hit h = trace(o, d, 0.0f, 1e30f);
        float3 Le;
        if (lightHit(o, d, h.hit ? h.t : 1e30f, depth == 0, Le)) {
            alb = make_float3(1, 1, 1);
            nrm = -d;
            if (depth > 0) firstPos.w = fminf(len + (h.hit ? h.t : 50.0f), 50.0f);
            return;
        }
        if (!h.hit) {
            alb = make_float3(0, 0, 0);
            nrm = -d;
            if (depth > 0) firstPos.w = 50.0f;
            return;
        }
        const float3 p = o + d * h.t;
        len += h.t;
        if (depth == 0) firstPos = make_float4(p.x, p.y, p.z, len);
        firstPos.w = fminf(len, 50.0f);
        const int mi = params.objects[h.object].material;
        const LMaterial& m = params.materials[mi];
        const float3 nn = dot(h.n, d) < 0.0f ? h.n : -h.n;
        const float c = fminf(1.0f, -dot(nn, d));
        if (m.type == LM_GLASS || m.type == LM_LIQUID) {   // as radiance() does, refracting unless it cannot
            const bool entering = dot(h.n, d) < 0.0f;
            float n1, n2;
            int into = -1;
            float3 across = p;
            if (entering) {
                n1 = md.ior();
                n2 = m.ior;
                into = mi;
            } else {
                n1 = m.ior;
                n2 = md.iorBelow(mi);
                if (params.gapMax > 0.0f) {
                    const Hit g = trace(p, d, 1e-5f, params.gapMax);
                    if (g.hit && dot(g.n, d) < 0.0f && dielectric(params.objects[g.object].material) && params.objects[g.object].material != mi) {
                        into = params.objects[g.object].material;
                        n2 = params.materials[into].ior;
                        across = p + d * g.t;
                    }
                }
            }
            float cosT;
            const float F = fresnelDielectric(c, n1 / n2, cosT);
            if (F >= 1.0f) {
                d = reflect3(d, nn);
                o = p + nn * 1e-5f;
            } else {
                const float eta = n1 / n2;
                d = normalize(d * eta + nn * (eta * c - cosT));
                o = across - nn * 1e-5f;
                if (!entering) md.remove(mi);
                if (into >= 0) md.push(into);
            }
            continue;
        }
        alb = m.type == LM_METAL ? m.albedo : albedoAt(m, h, p);   // where radiance() takes its guides
        nrm = nn;
        return;
    }
    alb = make_float3(0, 0, 0);
    nrm = -d;
}

extern "C" __global__ void __raygen__pick() {
    const LCamera& C = params.cam;
    const float u = (params.pickPixel.x / params.width) * 2.0f - 1.0f, v = 1.0f - (params.pickPixel.y / params.height) * 2.0f;
    const float3 d = normalize(C.fwd + C.right * (u * C.tanHalfX) + C.up * (v * C.tanHalfY));
    const Hit h = trace(C.pos, d, 0.0f, 1e30f);
    LPick p;
    p.dir = d;
    if (!h.hit) {
        p.object = -1;
        p.prim = -1;
        p.t = 1e30f;
        p.pos = C.pos;
        p.normal = -d;
    } else {
        p.object = h.object;
        p.prim = int(h.prim);
        p.t = h.t;
        p.pos = C.pos + d * h.t;
        p.normal = dot(h.n, d) < 0.0f ? h.n : -h.n;
    }
    *params.pickOut = p;
}

extern "C" __global__ void __raygen__render() {
    const uint3 ix = optixGetLaunchIndex();
    const unsigned int px = ix.x, py = ix.y;
    const unsigned int pix = py * params.width + px;
    Rng r;
    r.s = hashU32(pix * 9781u + params.seed * 6271u + params.samples * 26699u);
    const LCamera& C = params.cam;
    float3 sum = make_float3(0, 0, 0);
    if (params.albedoOut || params.posOut) {   // the guides and the reprojection's position: one steady ray
        const float u = ((px + 0.5f) / params.width) * 2.0f - 1.0f, v = 1.0f - ((py + 0.5f) / params.height) * 2.0f;
        const float3 d = normalize(C.fwd + C.right * (u * C.tanHalfX) + C.up * (v * C.tanHalfY));
        float3 ga, gn;
        float4 gp;
        guidePath(C.pos, d, ga, gn, gp);
        if (params.albedoOut) {
            params.albedoOut[pix] = make_float4(ga.x, ga.y, ga.z, 1.0f);
            params.normalOut[pix] = make_float4(gn.x, gn.y, gn.z, 0.0f);
        }
        if (params.posOut) params.posOut[pix] = gp;
    }
    for (unsigned int s = 0; s < params.spp; ++s) {
        const float jx = r.next(), jy = r.next();
        const float u = ((px + jx) / params.width) * 2.0f - 1.0f, v = 1.0f - ((py + jy) / params.height) * 2.0f;
        float3 d = normalize(C.fwd + C.right * (u * C.tanHalfX) + C.up * (v * C.tanHalfY));
        float3 o = C.pos;
        if (C.aperture > 0.0f) {   // a thin lens
            const float3 fp = C.pos + d * (C.focus / fmaxf(dot(d, C.fwd), 1e-3f));
            const float lr = C.aperture * sqrtf(r.next()), la = 2.0f * M_PIf * r.next();
            o = C.pos + C.right * (lr * cosf(la)) + C.up * (lr * sinf(la));
            d = normalize(fp - o);
        }
        float3 a, n;   // (the per-sample guides; the steady guide path writes them)
        float3 L = radiance(o, d, r, &a, &n);
        const float mx = maxc(L);
        if (!(mx == mx)) L = make_float3(0, 0, 0);
        else if (mx > 40.0f) L = L * (40.0f / mx);   // fireflies: a rare path through glass to a soft box
        sum = sum + L;
    }
    float4 acc = params.samples > 0 ? params.accum[pix] : make_float4(0, 0, 0, 0);
    acc = acc + make_float4(sum.x, sum.y, sum.z, 0.0f);
    params.accum[pix] = acc;
    const float inv = 1.0f / float(params.samples + params.spp);
    float3 col = make_float3(acc.x, acc.y, acc.z) * (inv * params.exposure);
    col = agx(col);
    const float dn = (r.next() + r.next() - 1.0f) / 255.0f;
    params.output[pix] = make_uchar4((unsigned char)(saturate(srgbOetf(col.x) + dn) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(srgbOetf(col.y) + dn) * 255.0f + 0.5f),
                                     (unsigned char)(saturate(srgbOetf(col.z) + dn) * 255.0f + 0.5f), 255);
}
