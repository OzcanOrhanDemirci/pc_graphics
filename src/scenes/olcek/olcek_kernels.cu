// Scene 7 "Ölçek": the deterministic renderer (see olcek_kernels.h). Ö0's placeholders: spheres, discs, clouds,
// points, the ground at the beach (the shore, the grains, the grain's top, its atoms) and the desert's haze.
#include "scenes/olcek/olcek_kernels.h"
#include "shared/display.h"

namespace olcek {

namespace {

// the view in constant memory (as bytes: its members have default values, which constant memory cannot construct)
__constant__ __align__(16) unsigned char cViewBytes[sizeof(OlcekView)];
#define cV (*reinterpret_cast<const OlcekView*>(cViewBytes))

__device__ __forceinline__ unsigned int pcg(unsigned int v) {
    const unsigned int state = v * 747796405u + 2891336453u;
    const unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
__device__ __forceinline__ float hash1(int x, int y, int z, int seed) {
    unsigned int h = pcg(unsigned(x) * 73856093u ^ pcg(unsigned(y) * 19349663u ^ pcg(unsigned(z) * 83492791u ^ unsigned(seed) * 2654435761u)));
    return float(h >> 8) * (1.0f / 16777216.0f);
}
__device__ __forceinline__ float fractf(float x) { return x - floorf(x); }
__device__ __forceinline__ float fade5(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// value noise in [0, 1], 3D, periodic in z with period pz (> 0) for the octaves' clocks
__device__ float vnoise3(float x, float y, float z, int seed, int pz = 0) {
    const float fx = floorf(x), fy = floorf(y), fz = floorf(z);
    const int ix = int(fx), iy = int(fy), iz = int(fz);
    const float tx = fade5(x - fx), ty = fade5(y - fy), tz = fade5(z - fz);
    float v[2];
    for (int k = 0; k < 2; ++k) {
        int zz = iz + k;
        if (pz > 0) zz = ((zz % pz) + pz) % pz;
        const float a = hash1(ix, iy, zz, seed), b = hash1(ix + 1, iy, zz, seed);
        const float c = hash1(ix, iy + 1, zz, seed), d = hash1(ix + 1, iy + 1, zz, seed);
        v[k] = lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
    }
    return lerpf(v[0], v[1], tz);
}
__device__ float fbm3(float3 p, int oct, int seed) {
    float s = 0.0f, a = 0.5f, n = 0.0f;
    for (int i = 0; i < oct; ++i) {
        s += a * (vnoise3(p.x, p.y, p.z, seed + i) * 2.0f - 1.0f);
        n += a;
        p = p * 2.03f;
        a *= 0.5f;
    }
    return s / n;
}

__device__ __forceinline__ float3 rayDir(float px, float py) {
    const float sx = (px / cV.width * 2.0f - 1.0f) * cV.tanHalfX;
    const float sy = (1.0f - py / cV.height * 2.0f) * cV.tanHalfY;
    return normalize(make_float3(sx, sy, 1.0f));
}

// ---- spheres: solved from the camera's altitude (no cancellation when the sphere is 10^12 times the screen)
__device__ bool hitBall(const GBall& b, float3 d, float& t) {
    const float bq = dot(d, b.c);
    const float q = b.h * (2.0f * b.r + b.h);   // |c|^2 - r^2
    const float disc = bq * bq - q;
    if (disc < 0.0f) return false;
    const float sq = sqrtf(disc);
    if (b.h > 0.0f) {
        if (bq <= 0.0f) return false;
        t = q / (bq + sq);
    } else {
        t = bq + sq;   // inside
    }
    return t > 0.0f;
}

__device__ float3 rotZ(float3 q, float a) {
    const float c = cosf(a), s = sinf(a);
    return make_float3(c * q.x - s * q.y, s * q.x + c * q.y, q.z);
}

// The Earth's placeholder (Ö1: the real one): continents of noise, a coast at the beach (land to the south, the gulf
// to the north), ice at the poles. Albedos: sea 0.06, land 0.12-0.2, ice 0.7.
__device__ float3 earthAlbedo(float3 q, float3 home) {
    float n = fbm3(q * 2.3f + make_float3(3.1f, 1.7f, 0.4f), 6, 11) + 0.08f;
    const float3 dz = make_float3(0, 0, 1);
    const float3 nb = normalize(dz - home * home.z);
    const float3 rel = q - home;
    const float near = __expf(-dot(rel, rel) / (0.035f * 0.035f));
    const float side = dot(rel, nb) / 0.004f + 0.4f * fbm3(q * 900.0f, 3, 5);   // + north: the gulf
    n = lerpf(n, -side, near);
    const float land = smoothstepf(-0.015f, 0.015f, n);
    const float lat = fabsf(q.z);
    const float ice = q.z > 0.0f ? smoothstepf(0.935f, 0.955f, lat) : smoothstepf(0.905f, 0.925f, lat);
    const float v = fbm3(q * 14.0f, 4, 23) * 0.5f + 0.5f;
    const float3 sea = make_float3(0.025f, 0.045f, 0.085f);
    const float3 green = make_float3(0.09f, 0.12f, 0.06f), dry = make_float3(0.26f, 0.21f, 0.14f);
    const float desert = smoothstepf(0.45f, 0.75f, v) * (1.0f - smoothstepf(0.4f, 0.6f, lat));
    float3 c = lerp3(sea, lerp3(green, dry, desert), land);
    return lerp3(c, make_float3(0.7f, 0.72f, 0.75f), ice);
}
__device__ float3 moonAlbedo(float3 q) {
    const float m = fbm3(q * 1.7f + make_float3(5.0f, 2.0f, 1.0f), 4, 31);
    const float maria = smoothstepf(0.05f, 0.25f, m) * smoothstepf(-0.2f, 0.4f, q.x);
    const float d = fbm3(q * 12.0f, 3, 37) * 0.04f;
    return make_float3(0.14f, 0.13f, 0.12f) * (1.0f - 0.45f * maria) + make_float3(d, d, d);
}

// ---- the beach (metres from the beach point: x east, y north); fp the pixel's footprint there (m): every edge is at
// least a pixel soft, a band thinner than a pixel fades to its mean (no shimmer)
__device__ float3 beachAlbedo(float x, float y, float fp, float t) {
    const float shore = 0.75f + 0.8f * sinf(0.13f * x) + 0.35f * sinf(0.41f * x + 1.3f) + 0.15f * sinf(1.7f * x + 0.4f) - 0.8f * sinf(0.0f) ;
    const float run = 0.5f * (0.5f + 0.5f * sinf(t * (6.2831853f / 8.0f)));   // the last wave runs up and back
    const float edge = shore - run;
    const float w = fmaxf(0.03f, fp);
    const float sea = smoothstepf(edge - w, edge + w, y);
    const float wet = smoothstepf(shore - 1.4f - w, shore - 0.6f + w, y);
    const float fw = 0.10f;
    const float foam = __expf(-sqr((y - edge) / fmaxf(fw, fp))) * fminf(1.0f, fw / fp) * (0.6f + 0.4f * vnoise3(x * 3.0f, y * 3.0f, t * 0.5f, 3));
    const float deep = smoothstepf(shore + 2.0f, shore + 40.0f, y);
    const float park = 1.0f - smoothstepf(-45.0f - w, -32.0f + w, y);
    const float g = fminf(1.0f, 0.5f / fp);   // the sand's mottling fades where a pixel is wider than it
    const float mott = (vnoise3(x * 2.0f, y * 2.0f, 0.0f, 7) - 0.5f) * 0.12f * g + (vnoise3(x * 0.2f, y * 0.2f, 0.0f, 8) - 0.5f) * 0.10f;
    float3 sand = make_float3(0.56f, 0.47f, 0.35f) * (1.0f + mott);
    sand = lerp3(sand, make_float3(0.30f, 0.25f, 0.19f), wet);
    float3 water = lerp3(make_float3(0.07f, 0.13f, 0.12f), make_float3(0.03f, 0.06f, 0.09f), deep);
    float3 c = lerp3(sand, water, sea);
    c = c + make_float3(0.55f, 0.57f, 0.58f) * (foam * 0.8f);
    return lerp3(c, make_float3(0.08f, 0.11f, 0.05f), park);
}

// ---- the sand grains: a grid of spheres sitting under the beach's plane (the home grain's top is the beach point)
struct Grain {
    float3 c;
    float r;
    float tone;
};
__device__ Grain grainAt(int i, int j, float R) {
    const float cell = 2.3f * R;
    Grain g;
    if (i == 0 && j == 0) {
        g.c = make_float3(0, 0, -R);
        g.r = R;
        g.tone = 0.5f;
        return g;
    }
    const float a = hash1(i, j, 0, 101), b = hash1(i, j, 1, 101), c = hash1(i, j, 2, 101), e = hash1(i, j, 3, 101);
    g.r = R * (0.75f + 0.5f * a);
    g.c = make_float3((i + (b - 0.5f) * 0.5f) * cell, (j + (c - 0.5f) * 0.5f) * cell, -g.r - 0.35f * R * e);
    g.tone = hash1(i, j, 4, 101);
    return g;
}
// the ray (metres, local axes) into the grain layer: the nearest grain hit (t in metres), its normal and tone
__device__ bool hitGrains(float3 o, float3 d, float R, float& t, float3& n, float& tone, int& gi, int& gj) {
    if (d.z >= 0.0f) return false;
    const float t0 = (0.0f - o.z) / d.z, t1 = (-1.7f * R - o.z) / d.z;
    const float cell = 2.3f * R;
    const float3 a = o + d * fmaxf(t0, 0.0f), b = o + d * t1;
    const int i0 = int(floorf(fminf(a.x, b.x) / cell + 0.5f)) - 1, i1 = int(floorf(fmaxf(a.x, b.x) / cell + 0.5f)) + 1;
    const int j0 = int(floorf(fminf(a.y, b.y) / cell + 0.5f)) - 1, j1 = int(floorf(fmaxf(a.y, b.y) / cell + 0.5f)) + 1;
    float best = 1e30f;
    for (int i = i0; i <= min(i1, i0 + 4); ++i)
        for (int j = j0; j <= min(j1, j0 + 4); ++j) {
            const Grain g = grainAt(i, j, R);
            const float3 oc = o - g.c;
            const float bq = -dot(oc, d);
            // |oc|^2 - r^2 without cancellation for the home grain (its top is the beach point): at 10^-10 m above a
            // 0.25 mm sphere the plain form loses every digit (the atom under the camera flickered, DURUM Ö0)
            const float cq = (i == 0 && j == 0) ? o.x * o.x + o.y * o.y + o.z * (o.z + 2.0f * g.r) : dot(oc, oc) - g.r * g.r;
            const float disc = bq * bq - cq;
            if (disc < 0.0f || bq <= 0.0f) continue;
            const float tt = cq / (bq + sqrtf(disc));   // the near root, stable
            if (tt > 0.0f && tt < best) {
                best = tt;
                n = normalize(o + d * tt - g.c);
                tone = g.tone;
                gi = i;
                gj = j;
            }
        }
    if (best < 1e29f) { t = best; return true; }
    // between the grains: the next layer down, in their shadow
    t = t1;
    n = make_float3(0, 0, 1);
    tone = -1.0f;
    return true;
}

// the atoms of the home grain's top (AFM-like: a height field of Gaussian bumps on the lattice, each vibrating)
__device__ float atomHeight(float x, float y, const GGround& g, float& hx, float& hy) {
    const float a = g.lattice, s2 = 2.0f * g.atomSigma * g.atomSigma;
    const int ci = int(floorf(x / a + 0.5f)), cj = int(floorf(y / a + 0.5f));
    float h = 0.0f;
    hx = hy = 0.0f;
    for (int i = ci - 1; i <= ci + 1; ++i)
        for (int j = cj - 1; j <= cj + 1; ++j) {
            const float p1 = 6.2831853f * hash1(i, j, 0, 211), p2 = 6.2831853f * hash1(i, j, 1, 211);
            const float jx = g.jitter * sinf(g.vibPhase + p1), jy = g.jitter * sinf(g.vibPhase * 1.37f + p2);
            const float dx = x - (i * a + jx), dy = y - (j * a + jy);
            const float e = __expf(-(dx * dx + dy * dy) / s2);
            h += e;
            hx += -2.0f * dx / s2 * e;
            hy += -2.0f * dy / s2 * e;
        }
    return h;
}

// ---- the ground: its colour at a point (metres), lit by the Sun; tone < 0 between grains
__device__ float3 groundShade(const GGround& g, float3 lp, float3 nl, float fp, float tone, int gi, int gj, bool onGrain) {
    const float3 sunL = make_float3(dot(g.sunDir, g.east), dot(g.sunDir, g.north), dot(g.sunDir, g.up));
    float3 alb = beachAlbedo(lp.x, lp.y, fp, cV.haze.phase[0]);
    float3 n = make_float3(0, 0, 1);
    if (onGrain && g.grains > 0.0f) {
        float3 ga;
        if (tone < 0.0f) ga = make_float3(0.20f, 0.16f, 0.12f) * 0.35f;   // in the dark between them
        else ga = lerp3(make_float3(0.62f, 0.53f, 0.38f), tone > 0.85f ? make_float3(0.35f, 0.30f, 0.27f) : make_float3(0.72f, 0.64f, 0.50f), tone);
        alb = lerp3(alb, ga, g.grains);
        n = normalize(lerp3(n, nl, g.grains));
    }
    // the home grain's top seen as a surface (Ö4: fractures, then the crystal's steps)
    if (g.surface > 0.0f) {
        float m = 0.0f, amp = 0.5f;
        float f = 1.0f / fmaxf(fp * 512.0f, 1e-12f);
        for (int i = 0; i < 5; ++i) {
            const float lim = fminf(1.0f, 0.25f / (fp * f));
            m += amp * lim * (vnoise3(lp.x * f, lp.y * f, 0.0f, 300 + i) - 0.5f);
            f *= 2.7f;
            amp *= 0.6f;
        }
        alb = lerp3(alb, make_float3(0.64f, 0.58f, 0.48f) * (1.0f + 0.5f * m), g.surface);
    }
    float3 c = alb * (g.sunLit * (fmaxf(dot(n, sunL), 0.0f) + 0.18f));
    // the atoms: an instrument's picture, not the Sun's (below the light's wavelength)
    if (g.atoms > 0.0f) {
        float hx, hy;
        const float h = atomHeight(lp.x, lp.y, g, hx, hy);
        const float A = 0.9f * g.atomSigma;
        const float3 na = normalize(make_float3(-hx * A, -hy * A, 1.0f));
        const float3 L = normalize(make_float3(-0.55f, 0.45f, 0.70f));
        const float v = 0.10f + 0.42f * fmaxf(dot(na, L), 0.0f) + 0.18f * h;
        c = lerp3(c, make_float3(v, v, v), g.atoms);
    }
    return c;
}

// ---- a Gaussian cloud's light along the ray from 0 to tmax
__device__ float3 blobLight(const GBlob& b, float3 d, float tmax) {
    const float tc = dot(b.c, d);
    const float3 pc = d * tc - b.c;
    const float d2 = dot(pc, pc);
    const float s = b.sigma;
    const float e = __expf(-0.5f * d2 / (s * s));
    if (e < 1e-6f) return make_float3(0, 0, 0);
    const float k = 0.70710678f / s;
    const float lo = 0.5f * (1.0f + erff((0.0f - tc) * k)), hi = 0.5f * (1.0f + erff((fminf(tmax, 1e30f) - tc) * k));
    return b.emit * (e * fmaxf(hi - lo, 0.0f));
}

// ---- a galaxy's disc
__device__ float3 discLight(const GDisc& g, float3 d, float tmax) {
    const float dn = dot(d, g.n);
    if (fabsf(dn) < 1e-6f) return make_float3(0, 0, 0);
    const float t = dot(g.c, g.n) / dn;
    if (t <= 0.0f || t >= tmax) return make_float3(0, 0, 0);
    const float3 p = d * t - g.c;
    const float x = dot(p, g.ax) / g.h, y = dot(p, g.ay) / g.h;
    const float r = sqrtf(x * x + y * y);
    if (r > g.rmax / g.h) return make_float3(0, 0, 0);
    float I = __expf(-r) * (1.0f - smoothstepf(0.8f * g.rmax / g.h, g.rmax / g.h, r));
    if (g.arms > 0.0f) {
        const float th = atan2f(y, x);
        const float sp = 0.5f + 0.5f * cosf(2.0f * (th - logf(fmaxf(r, 0.05f)) / g.pitch - g.phase));
        I *= (1.0f - g.arms) + g.arms * 2.2f * sp * sp * sp * smoothstepf(0.2f, 0.8f, r);
    }
    I += g.bulge * 3.0f * __expf(-r * r / 0.05f);
    const float slant = fmaxf(fabsf(dn), g.thick / fmaxf(g.rmax, 1e-20f) * 4.0f);
    return g.emit * (I / slant);
}

// ---- the desert's haze and the foam: octaves on the focus plane (u in screen widths)
__device__ float3 hazeLight(float3 d) {
    const GHaze& hz = cV.haze;
    const float ux = d.x / d.z / (2.0f * cV.tanHalfX), uy = d.y / d.z / (2.0f * cV.tanHalfX);
    float v = 0.0f, wsum = 0.0f, cells = 0.0f;
    for (int i = 0; i < hz.octaves; ++i) {
        const float f = hz.freq[i];
        const float n = vnoise3(ux * f, uy * f, hz.phase[i], 500 + hz.index[i], 256);
        v += hz.weight[i] * (n - 0.5f);
        wsum += hz.weight[i];
        if (hz.foam > 0.0f) {   // bubbles: one minus the distance to the cell's point
            const float cx = floorf(ux * f), cy = floorf(uy * f);
            float m = 1e9f;
            for (int a = -1; a <= 1; ++a)
                for (int b = -1; b <= 1; ++b) {
                    const int ix = int(cx) + a, iy = int(cy) + b;
                    const float px = ix + hash1(ix, iy, int(hz.phase[i]) & 255, 600 + hz.index[i]);
                    const float py = iy + hash1(ix, iy, (int(hz.phase[i]) + 7) & 255, 700 + hz.index[i]);
                    m = fminf(m, sqr(ux * f - px) + sqr(uy * f - py));
                }
            cells += hz.weight[i] * smoothstepf(0.35f, 0.0f, sqrtf(m));
        }
    }
    const float base = 0.035f + 0.06f * (wsum > 0.0f ? v / sqrtf(wsum) : 0.0f);
    const float foam = 0.12f * cells / fmaxf(wsum, 1e-3f);
    return hz.color * (hz.amount * (fmaxf(base, 0.0f) + hz.foam * foam));
}

// One sample: the nearest surface (spheres, the ground), then the light of the clouds and discs in front of it
__device__ float3 sampleAt(float px, float py, float& depth) {
    const float3 d = rayDir(px, py);
    float tBest = 1e30f;
    int which = -1;
    for (int i = 0; i < cV.nBalls; ++i) {
        const GBall& b = cV.balls[i];
        if (b.pattern < 0) continue;   // a shell of light, below
        float t;
        if (hitBall(b, d, t) && t < tBest) {
            tBest = t;
            which = i;
        }
    }
    float3 col = cV.background;
    const GGround& g = cV.ground;
    bool groundHit = false;
    if (g.on && (which < 0 || cV.balls[which].pattern != 1)) {
        // the ground's plane (the Earth's sphere is not drawn this close: its numbers would not hold)
        const float dn = dot(d, g.up);
        if (dn < 0.0f) {
            const float t = dot(g.o, g.up) / dn;
            if (t > 0.0f && t < tBest) {
                tBest = t;
                groundHit = true;
                which = -1;
            }
        }
    }
    if (groundHit) {
        // local axes, metres: the camera from the beach point, and the ray
        const float m = g.metres;
        const float3 oc = g.o * -1.0f;
        float3 o = make_float3(dot(oc, g.east), dot(oc, g.north), dot(oc, g.up)) * m;
        const float3 dl = make_float3(dot(d, g.east), dot(d, g.north), dot(d, g.up));
        float tl = tBest * m;   // metres
        float3 n = make_float3(0, 0, 1);
        float tone = 0.5f;
        int gi = 0, gj = 0;
        bool onGrain = false;
        if (g.grains > 0.0f) {
            float tg;
            if (hitGrains(o, dl, g.grainR, tg, n, tone, gi, gj)) {
                tl = tg;
                onGrain = true;
            }
        }
        const float3 lp = o + dl * tl;
        const float fp = tl * cV.pixAngle;   // a pixel's footprint there (m)
        col = groundShade(g, lp, n, fp, tone, gi, gj, onGrain) * g.fade;
        tBest = tl / m;
    } else if (which >= 0) {
        const GBall& b = cV.balls[which];
        const float3 p = d * tBest;
        const float3 n = normalize(p - b.c);
        if (b.emit.x + b.emit.y + b.emit.z > 0.0f) {
            const float mu = fmaxf(-dot(n, d), 0.0f);
            col = b.emit * (0.4f + 0.6f * mu);   // limb darkening
        } else {
            float3 alb = b.albedo;
            const float3 q = make_float3(dot(n, b.ax), dot(n, b.ay), dot(n, b.az));
            if (b.pattern == 1) {
                // the Earth's turn while the shutter is open: its pattern averaged along the longitude
                const float sb = b.spinBlur;
                if (sb < 0.01f) {
                    alb = earthAlbedo(q, b.home);
                } else {
                    const int nTap = sb * 4.0f >= 6.2831853f ? 12 : min(12, int(sb * 4.0f / 0.05f) + 2);
                    float3 acc = make_float3(0, 0, 0);
                    float ws = 0.0f;
                    for (int k = 0; k < nTap; ++k) {
                        const float u = (k + 0.5f) / nTap;
                        float a, w;
                        if (sb * 4.0f >= 6.2831853f) { a = 6.2831853f * u; w = 1.0f; }
                        else { a = (u - 0.5f) * 4.0f * sb; w = __expf(-0.5f * sqr(a / sb)); }
                        acc += earthAlbedo(rotZ(q, a), b.home) * w;
                        ws += w;
                    }
                    alb = acc * (1.0f / ws);
                }
                // near the ground the beach's own pattern takes over (metres from the beach point)
                if (g.on && g.beach > 0.0f) {
                    const float3 lp3 = p - g.o;
                    const float m = g.metres;
                    const float x = dot(lp3, g.east) * m, y = dot(lp3, g.north) * m;
                    const float fp = tBest * m * cV.pixAngle;
                    alb = lerp3(alb, beachAlbedo(x, y, fp, cV.haze.phase[0]), g.beach);
                }
            } else if (b.pattern == 2) {
                alb = moonAlbedo(q);
            }
            col = alb * (b.lit * fmaxf(dot(n, b.toLight), 0.0f));
        }
        col = col * b.weight;
    }
    // light in front of the surface: clouds, discs, shells, the haze
    for (int i = 0; i < cV.nBlobs; ++i) col += blobLight(cV.blobs[i], d, tBest);
    for (int i = 0; i < cV.nDiscs; ++i) col += discLight(cV.discs[i], d, tBest);
    for (int i = 0; i < cV.nBalls; ++i) {
        const GBall& b = cV.balls[i];
        if (b.pattern >= 0) continue;
        // a thin shell of light (the edge of the observable universe): brighter towards its limb
        const float bq = dot(d, b.c);
        const float p2 = fmaxf(dot(b.c, b.c) - bq * bq, 0.0f);
        const float R = b.r, Ri = 0.99f * R;
        if (p2 < R * R) {
            const float outer = sqrtf(R * R - p2), inner = p2 < Ri * Ri ? sqrtf(Ri * Ri - p2) : 0.0f;
            const float col1 = (outer - inner) / (R - Ri);
            col += b.emit * col1;
        }
    }
    if (cV.haze.amount > 0.0f) col += hazeLight(d);
    depth = tBest;
    return col;
}

__device__ __forceinline__ float blackmanHarris(float x) {
    const float u = 0.5f + 0.5f * saturate(x);
    const float a = 2.0f * M_PIf * u;
    return 0.35875f - 0.48829f * cosf(a) + 0.14128f * cosf(2.0f * a) - 0.01168f * cosf(3.0f * a);
}

__global__ void kFrame(float4* accum, float* depthOut, int samples) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= cV.width || y >= cV.height) return;
    float3 sum = make_float3(0, 0, 0);
    float wsum = 0.0f, dCentre = 1e30f;
    for (int s = 0; s < samples; ++s) {
        float ox = 0.0f, oy = 0.0f, w = 1.0f;
        if (s > 0) {   // R2 over a disc of filterRadius pixels, Blackman-Harris weighted
            const float u1 = fractf(0.5f + 0.7548776662f * s), u2 = fractf(0.5f + 0.5698402910f * s);
            const float r = cV.filterRadius * sqrtf(u1), ph = 6.2831853f * u2;
            ox = r * cosf(ph);
            oy = r * sinf(ph);
            w = blackmanHarris(r / cV.filterRadius);
        }
        float dep;
        const float3 c = sampleAt(x + 0.5f + ox, y + 0.5f + oy, dep);
        if (s == 0) dCentre = dep;
        sum += c * w;
        wsum += w;
    }
    accum[y * cV.width + x] = make_float4(sum.x / wsum, sum.y / wsum, sum.z / wsum, 1.0f);
    depthOut[y * cV.width + x] = dCentre;
}

__global__ void kSplat(const GSplat* sp, int count, float4* accum, const float* depth, int width, int height) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    const GSplat g = sp[i];
    if (g.p.z <= 0.0f) return;
    const float px = (g.p.x / g.p.z / cV.tanHalfX * 0.5f + 0.5f) * width;
    const float py = (0.5f - g.p.y / g.p.z / cV.tanHalfY * 0.5f) * height;
    const float s = fminf(g.sigmaPx, 24.0f);
    const int rad = int(ceilf(3.0f * s));
    const int cx = int(floorf(px)), cy = int(floorf(py));
    if (cx + rad < 0 || cx - rad >= width || cy + rad < 0 || cy - rad >= height) return;
    const float dist = length(g.p);
    const float norm = 1.0f / (6.2831853f * s * s);
    for (int y = max(cy - rad, 0); y <= min(cy + rad, height - 1); ++y)
        for (int x = max(cx - rad, 0); x <= min(cx + rad, width - 1); ++x) {
            const float dx = x + 0.5f - px, dy = y + 0.5f - py;
            const float w = __expf(-(dx * dx + dy * dy) / (2.0f * s * s)) * norm;
            if (w < 1e-7f) continue;
            const int pix = y * width + x;
            if (depth[pix] < dist * 0.9999f) continue;   // behind a surface
            atomicAdd(&accum[pix].x, g.flux.x * w);
            atomicAdd(&accum[pix].y, g.flux.y * w);
            atomicAdd(&accum[pix].z, g.flux.z * w);
        }
}

__global__ void kResolve(const float4* accum, uchar4* out, int width, int height, float sat) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    const int i = y * width + x;
    float3 c = fmaxf3(make_float3(accum[i].x, accum[i].y, accum[i].z), make_float3(0, 0, 0));
    const float Y = luminance709(c);
    c = lerp3(make_float3(Y, Y, Y), c, sat);
    c = agx(c);
    const float n = float(pcg(unsigned(i) * 3u + 1u) >> 8) * (1.0f / 16777216.0f) + float(pcg(unsigned(i) * 3u + 2u) >> 8) * (1.0f / 16777216.0f) - 1.0f;
    const float r = fminf(fmaxf(srgbOetf(c.x) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    const float g = fminf(fmaxf(srgbOetf(c.y) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    const float b = fminf(fmaxf(srgbOetf(c.z) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    out[i] = make_uchar4((unsigned char)r, (unsigned char)g, (unsigned char)b, 255);
}

// the picture's luminance (display values 0..1) box-filtered to a quarter of its size: the jump test's
__global__ void kQuarter(const uchar4* in, float* out, int width, int height) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    const int w4 = width / 4, h4 = height / 4;
    if (x >= w4 || y >= h4) return;
    float s = 0.0f;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            const uchar4 c = in[(y * 4 + j) * width + x * 4 + i];
            s += 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        }
    out[y * w4 + x] = s / (16.0f * 255.0f);
}

}   // namespace

void quarterLuma(const uchar4* in, float* out, int width, int height, cudaStream_t s) {
    const dim3 b(16, 8);
    kQuarter<<<dim3((width / 4 + b.x - 1) / b.x, (height / 4 + b.y - 1) / b.y), b, 0, s>>>(in, out, width, height);
}

void setView(const OlcekView& v) { cudaMemcpyToSymbol(cViewBytes, &v, sizeof(OlcekView)); }

void renderFrame(float4* accum, float* depth, int width, int height, int samples, cudaStream_t s) {
    const dim3 b(16, 8);
    kFrame<<<dim3((width + b.x - 1) / b.x, (height + b.y - 1) / b.y), b, 0, s>>>(accum, depth, samples);
}
void splatPoints(const GSplat* splats, int count, float4* accum, const float* depth, int width, int height, cudaStream_t s) {
    if (count <= 0) return;
    kSplat<<<(count + 127) / 128, 128, 0, s>>>(splats, count, accum, depth, width, height);
}
void resolve(const float4* accum, uchar4* out, int width, int height, float saturation, cudaStream_t s) {
    const dim3 b(16, 8);
    kResolve<<<dim3((width + b.x - 1) / b.x, (height + b.y - 1) / b.y), b, 0, s>>>(accum, out, width, height, saturation);
}

}   // namespace olcek
