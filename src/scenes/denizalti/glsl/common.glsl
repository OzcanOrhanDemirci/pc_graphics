// Scene 8 · shared by the tessellation and fragment stages: the frame's constants and the surface features (the
// analytic detail the geometry is displaced by and the normal is bent by, written once so both always agree).
layout(std140, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 camPos;        // xyz; w: the vertical field of view's tan(fov/2)
    vec4 target;        // x: target triangle edge in samples, y: max tess factor, z: the finest triangle (m), w: displacement on (0/1)
    vec4 viewport;      // x, y: the sample grid (width, height in samples), z: 1/sqrt(samples per pixel), w: level (0 low .. 2 high)
    vec4 misc;          // x: time (s), y: exposure (EV), z: debug view, w: normal-detail scale
    vec4 lampPos[4];    // the key lamps: xyz, radius
    vec4 lampL[4];      // radiance rgb, w: present
    vec4 camFwd;        // the view axis
    vec4 haze;          // the froxels' near, far, slices, on
    vec4 probeLo;       // the probe grid's first probe; w: mips (0: none)
    vec4 probeStep;     // its spacing; w: probes blended (1 or 8)
    vec4 probeDims;
};

const uint MAT_PAINT_HULL = 0u, MAT_PAINT_PIPE = 1u, MAT_PAINT_DARK = 2u, MAT_BRASS = 3u, MAT_STEEL = 4u, MAT_CHROME = 5u,
           MAT_RUBBER = 6u, MAT_DECK = 7u, MAT_GLASS = 8u, MAT_DIAL = 9u, MAT_RED = 10u, MAT_EMISSIVE = 11u, MAT_CREAM = 12u,
           MAT_GREY = 13u, MAT_DECK_GREEN = 14u, MAT_VINYL = 15u, MAT_ALU = 16u, MAT_WRINKLE = 17u, MAT_RED_GLASS = 18u,
           MAT_TAPE = 19u, MAT_BLACK_PLATE = 20u, MAT_LAMP_SHADE = 21u, MAT_WOOD = 22u, MAT_PAPER = 23u,
           MAT_INDICATOR_RED = 24u, MAT_INDICATOR_GREEN = 25u;
const uint FEAT_NONE = 0u, FEAT_PLATE = 1u, FEAT_WELD = 2u, FEAT_RIVETS = 3u, FEAT_THREAD = 4u, FEAT_KNURL = 5u, FEAT_TREAD = 6u,
           FEAT_CAST = 7u, FEAT_BRAID = 8u, FEAT_CORK = 9u, FEAT_DIAL = 10u, FEAT_LABEL = 11u;
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

// The noise, precomputed: one octave of periodic gradient noise (32 cells, 4 texels a cell, 128^3) with its gradient,
// mipmapped: an octave is one filtered fetch instead of eight hashes, and its mip level is chosen from the sampling size
// (the band limit comes with the filtering). Built by the renderer from the same gnoise as above (dz_render.cpp).
layout(binding = 6) uniform sampler3D noiseTex;   // r: value, gba: d(value)/d(lattice)

// the printed and engraved faces (dz_decal): the atlas and where each face lies in it
layout(binding = 7) uniform sampler2D decalTex;   // sRGB colour, a: ink
layout(std140, binding = 1) uniform DecalTable {
    vec4 decRect[256];   // u0 v0 u1 v1
    vec4 decInfo[256];   // half width, half height (m), mode (0 dial, 1 plate), style (0 black, 1 brass, 2 stencil)
};
// the atlas coordinate of a surface point: a dial's (uv centred, radius r) or a plate's (uv centred, its half sizes)
vec2 decalUV(int id, vec2 uv, float r) {
    vec4 rc = decRect[id], inf = decInfo[id];
    vec2 t = inf.z < 0.5 ? uv / (2.0 * r) + 0.5 : uv / (2.0 * inf.xy) + 0.5;
    t = clamp(t, vec2(0.002), vec2(0.998));
    return vec2(mix(rc.x, rc.z, t.x), mix(rc.y, rc.w, 1.0 - t.y));
}

// fractal noise with a band limit; returns value, d the 3D gradient (per metre)
float fbm(vec3 p, float baseWavelength, int oct, float gain, float size, out vec3 d) {
    float v = 0.0, a = 1.0, wl = baseWavelength;
    d = vec3(0);
    for (int i = 0; i < oct; ++i) {
        float w = bandWeight(wl, size);
        if (w <= 0.0) break;
        vec3 q = p / wl + vec3(float(i) * 17.31, float(i) * 5.17, float(i) * 11.83);
        float lod = log2(max(4.0 * size / wl, 1e-6));
        vec4 s = textureLod(noiseTex, q / 32.0, lod);
        v += a * w * s.r;
        d += a * w * s.gba / wl;
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

// How fine a surface needs cutting, relative to the target: where the feature raises real relief (threads, tread,
// welds, cork granules, braid, knurls, engraving) a triangle per sample on High; where its relief is below a pixel
// anyway (the paint's orange peel, a casting's pitting: the normal carries it) six times coarser; where there is none,
// only enough to round the curves. One function for the control and the evaluation stage (the band limit follows it:
// a shared edge always gets one size).
float featureTessScale(uint feat) {
    if (feat == FEAT_NONE || feat == FEAT_DIAL) return 8.0;
    if (feat == FEAT_PLATE || feat == FEAT_CAST) return 6.0;
    return 1.0;
}

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
    } else if (s.feat == FEAT_BRAID) {
        // braided cable under paint: two families of strands crossing, a diamond every pitch along, a whole number of
        // them round the circumference (the pattern closes on the seam)
        float p = max(s.fp.x, 0.001), C = max(s.fp.y, 1e-4);
        float K = max(1.0, floor(C / p + 0.5));
        float a1 = s.uv.y / p + s.uv.x * K / C, a2 = s.uv.y / p - s.uv.x * K / C;
        float f1 = fract(a1) - 0.5, f2 = fract(a2) - 0.5;
        float bw = bandWeight(p, s.size);
        float b1 = 0.25 - f1 * f1, b2 = 0.25 - f2 * f2;   // a rounded strand profile
        h = 0.0005 * (b1 + b2) * bw;
        vec2 g = 0.0005 * bw * vec2(-2.0 * f1 * K / C + 2.0 * f2 * K / C, -2.0 * f1 / p - 2.0 * f2 / p);
        gw = T * g.x + B * g.y;
    } else if (s.feat == FEAT_CORK) {
        // granulated cork sheet under several coats of enamel: rounded granules 2-5 mm, the paint fills the gaps
        vec3 d1, d2;
        float n1 = fbm(s.p, 0.0045, 2, 0.5, s.size, d1);
        float n2 = fbm(s.p * 1.0 + 3.7, 0.0018, 2, 0.5, s.size, d2);
        float g = n1 + 0.45 * n2;
        // the paint rounds the tops: a soft clamp
        float k = 0.6;
        float sg = g / (1.0 + abs(g) / k);
        float dsg = 1.0 / ((1.0 + abs(g) / k) * (1.0 + abs(g) / k));
        h = 0.0007 * sg;
        gw = 0.0007 * dsg * (d1 + 0.45 * d2);
    } else if (s.feat == FEAT_LABEL) {
        // a brass plate's engraving cut 0.25 mm into the metal (the ink fills it); the mip from the sampling size
        int id = int(s.fp.x + 0.5);
        if (decInfo[id].w > 0.5 && decInfo[id].w < 1.5) {
            vec2 sizeM = 2.0 * decInfo[id].xy;
            vec2 texels = (decRect[id].zw - decRect[id].xy) * vec2(textureSize(decalTex, 0));
            float lod = log2(max(s.size * texels.x / sizeM.x, 1e-4));
            float e = max(s.size, sizeM.x / texels.x);
            float ink = textureLod(decalTex, decalUV(id, s.uv, 0.0), lod).a;
            float ix = textureLod(decalTex, decalUV(id, s.uv + vec2(e, 0), 0.0), lod).a;
            float iy = textureLod(decalTex, decalUV(id, s.uv + vec2(0, e), 0.0), lod).a;
            h = -0.00025 * ink;
            gw = -0.00025 * ((ix - ink) / e * T + (iy - ink) / e * B);
        }
    } else if (s.feat == FEAT_CAST) {
        // a sand casting under paint: soft, fine pitting
        vec3 d;
        float n = fbm(s.p, 0.003, 3, 0.5, s.size, d);
        h = 0.00005 * n;
        gw = 0.00005 * d;
    }
    return h;
}
