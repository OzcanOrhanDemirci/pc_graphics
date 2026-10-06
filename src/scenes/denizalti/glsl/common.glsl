// Scene 8 · shared by the tessellation and fragment stages: the frame's constants and the surface features (the
// analytic detail the geometry is displaced by and the normal is bent by, written once so both always agree).
layout(std140, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 camPos;        // xyz; w: the vertical field of view's tan(fov/2)
    vec4 target;        // x: target triangle edge in samples, y: max tess factor, z: the finest triangle (m), w: displacement on (0/1)
    vec4 viewport;      // x, y: the sample grid (width, height in samples), z: 1/sqrt(samples per pixel), w: level (0 low .. 2 high)
    vec4 misc;          // x: time (s), y: exposure (EV), z: debug view, w: normal-detail scale
};

const uint MAT_PAINT_HULL = 0u, MAT_PAINT_PIPE = 1u, MAT_PAINT_DARK = 2u, MAT_BRASS = 3u, MAT_STEEL = 4u, MAT_CHROME = 5u,
           MAT_RUBBER = 6u, MAT_DECK = 7u, MAT_GLASS = 8u, MAT_DIAL = 9u, MAT_RED = 10u, MAT_EMISSIVE = 11u;
const uint FEAT_NONE = 0u, FEAT_PLATE = 1u, FEAT_WELD = 2u, FEAT_RIVETS = 3u, FEAT_THREAD = 4u, FEAT_KNURL = 5u, FEAT_TREAD = 6u,
           FEAT_CAST = 7u, FEAT_BRAID = 8u;
const float PI = 3.14159265358979;

// ---- noise: gradient noise in 3D (deterministic: a hash of the lattice, no random state anywhere)
uint hash3(uvec3 p) {
    p = p * uvec3(1597334673u, 3812015801u, 2798796415u);
    uint n = (p.x ^ p.y ^ p.z) * 1597334673u;
    n ^= n >> 16;
    n *= 2246822519u;
    n ^= n >> 13;
    return n;
}
vec3 grad3(uvec3 c) {
    uint h = hash3(c);
    vec3 g = vec3(float(h & 1023u), float((h >> 10) & 1023u), float((h >> 20) & 1023u)) / 511.5 - 1.0;
    return g;
}
// value and gradient (quintic fade); d receives the derivative
float gnoise(vec3 x, out vec3 d) {
    vec3 i = floor(x), f = x - i;
    uvec3 c = uvec3(ivec3(i) + 65536);
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec3 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    vec3 ga = grad3(c), gb = grad3(c + uvec3(1, 0, 0)), gc = grad3(c + uvec3(0, 1, 0)), gd = grad3(c + uvec3(1, 1, 0));
    vec3 ge = grad3(c + uvec3(0, 0, 1)), gf = grad3(c + uvec3(1, 0, 1)), gg = grad3(c + uvec3(0, 1, 1)), gh = grad3(c + uvec3(1, 1, 1));
    float va = dot(ga, f), vb = dot(gb, f - vec3(1, 0, 0)), vc = dot(gc, f - vec3(0, 1, 0)), vd = dot(gd, f - vec3(1, 1, 0));
    float ve = dot(ge, f - vec3(0, 0, 1)), vf = dot(gf, f - vec3(1, 0, 1)), vg = dot(gg, f - vec3(0, 1, 1)), vh = dot(gh, f - vec3(1, 1, 1));
    float k1 = vb - va, k2 = vc - va, k3 = ve - va, k4 = va - vb - vc + vd, k5 = va - vc - ve + vg, k6 = va - vb - ve + vf,
          k7 = -va + vb + vc - vd + ve - vf - vg + vh;
    d = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.z * (ge - ga) + u.x * u.y * (ga - gb - gc + gd) + u.y * u.z * (ga - gc - ge + gg) +
        u.z * u.x * (ga - gb - ge + gf) + u.x * u.y * u.z * (-ga + gb + gc - gd + ge - gf - gg + gh) +
        du * vec3(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z, k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x, k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y);
    return va + u.x * k1 + u.y * k2 + u.z * k3 + u.x * u.y * k4 + u.y * u.z * k5 + u.z * u.x * k6 + u.x * u.y * u.z * k7;
}
// fade an octave out as its wavelength nears the sampling size (band limit: no octave finer than ~2 samples)
float bandWeight(float wavelength, float size) { return clamp(wavelength / (4.0 * size) - 0.5, 0.0, 1.0); }

// fractal noise with a band limit; returns value, d the 3D gradient
float fbm(vec3 p, float baseWavelength, int oct, float gain, float size, out vec3 d) {
    float v = 0.0, a = 1.0, wl = baseWavelength;
    d = vec3(0);
    for (int i = 0; i < oct; ++i) {
        float w = bandWeight(wl, size);
        if (w <= 0.0) break;
        vec3 g;
        float n = gnoise(p / wl + float(i) * 17.31, g);
        v += a * w * n;
        d += a * w * g / wl;
        a *= gain;
        wl *= 0.5;
    }
    return v;
}

// ---- the features. Height in metres along the displacement normal, and its gradient in the surface's (u, v) metres.
// size: the sampling size here (metres): detail finer than ~2 sizes fades out, so tessellated vertices sliding as the
// factor changes sample a smooth field (no swimming) and the normal map never aliases.
struct Surf {
    vec3 p;      // world position (before displacement)
    vec2 uv;     // surface coordinates (m)
    vec2 fp;     // feature parameters
    uint feat;
    float size;
};

// smooth step profile helpers
float bump(float x) { x = clamp(x, -1.0, 1.0); return (1.0 - x * x) * (1.0 - x * x); }
float dbump(float x) { if (abs(x) >= 1.0) return 0.0; return -4.0 * x * (1.0 - x * x); }

// returns the height; gw receives its gradient along the surface, in world space (T, B: the surface's directions of
// u and v). The gradients are analytic: one evaluation per sample, the same function the tessellation displaces by.
float featureHeight(Surf s, vec3 T, vec3 B, out vec3 gw) {
    gw = vec3(0);
    float h = 0.0;
    if (s.feat == FEAT_PLATE) {
        // rolled plate under enamel: a long soft undulation (rolling, hammering at the frames) and the paint's orange
        // peel (~15 um over ~2 mm: it shows only in the highlights)
        vec3 d1, d2;
        float n1 = fbm(s.p, 0.35, 2, 0.5, s.size, d1);
        float n2 = fbm(s.p, 0.0021, 2, 0.45, s.size, d2);
        h = 0.00035 * n1 + 0.000014 * n2;
        gw = 0.00035 * d1 + 0.000014 * d2;
    } else if (s.feat == FEAT_WELD) {
        // a bead's ripples (the welder's weave, ~3 mm, not quite regular) on the fillet; along u
        vec3 dn;
        float wob = fbm(vec3(s.uv.x * 7.0, s.uv.y * 3.0, 0.0), 0.08, 2, 0.5, s.size * 7.0, dn);
        float ph = 2.0 * PI * (s.uv.x / 0.0031 + 0.8 * wob);
        float bw = bandWeight(0.0031, s.size);
        h = 0.00022 * (0.5 + 0.5 * cos(ph)) * bw;
        float dph = 2.0 * PI * (1.0 / 0.0031 + 0.8 * dn.x * 7.0);
        gw = T * (-0.00022 * 0.5 * sin(ph) * dph * bw);
        vec3 dc;
        float c = fbm(s.p, 0.0012, 2, 0.5, s.size, dc);   // spatter and grain
        h += 0.00004 * c;
        gw += 0.00004 * dc;
    } else if (s.feat == FEAT_THREAD) {
        // a 60 degree thread: the profile repeats every pitch along v, advancing one pitch per turn around u
        float pitch = max(s.fp.x, 0.0005), C = max(s.fp.y, 1e-4);
        float ph = s.uv.y / pitch - s.uv.x / C;
        float f = fract(ph) - 0.5;
        float tri = 0.5 - abs(f);   // 0 at the root, 0.5 at the crest
        float depth = 0.3 * pitch;
        float bw = bandWeight(pitch, s.size);
        h = (tri - 0.25) * 2.0 * depth * bw;
        float dt = -sign(f) * 2.0 * depth * bw;   // d(h)/d(ph)
        gw = T * (-dt / C) + B * (dt / pitch);
    } else if (s.feat == FEAT_TREAD) {
        // diamond tread plate: raised lozenges in a herringbone, ~3 cm pitch, 1.5 mm proud
        float p = max(s.fp.x, 0.01);
        vec2 q = s.uv / p;
        vec2 cell = floor(q);
        vec2 f = q - cell - 0.5;
        float rot = (mod(cell.x + cell.y, 2.0) < 1.0) ? 0.785398 : -0.785398;
        float cr = cos(rot), sr = sin(rot);
        vec2 r = vec2(cr * f.x - sr * f.y, sr * f.x + cr * f.y);
        float ex = r.x / 0.36, ey = r.y / 0.075;
        float bw = bandWeight(0.075 * p, s.size);
        float d2 = ex * ex + ey * ey;
        float t = clamp((d2 - 0.55) / 0.45, 0.0, 1.0);
        h = 0.0015 * (1.0 - t * t * (3.0 - 2.0 * t)) * bw;
        float dpd = (d2 > 0.55 && d2 < 1.0) ? -0.0015 * 6.0 * t * (1.0 - t) / 0.45 * bw : 0.0;
        vec2 dr = vec2(2.0 * ex / 0.36, 2.0 * ey / 0.075) * dpd;      // d(h)/d(r)
        vec2 df = vec2(cr * dr.x + sr * dr.y, -sr * dr.x + cr * dr.y);  // d(h)/d(f) = R^T d(h)/d(r)
        gw = (T * df.x + B * df.y) / p;
    } else if (s.feat == FEAT_CAST) {
        // a sand casting under paint: soft, fine pitting
        vec3 d;
        float n = fbm(s.p, 0.003, 3, 0.5, s.size, d);
        h = 0.00005 * n;
        gw = 0.00005 * d;
    }
    return h;
}
