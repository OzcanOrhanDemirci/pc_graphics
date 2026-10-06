// Scene 9 "Ayna": the surfaces' look, from formulas (device only; included by ayna_kernels.cu).
//
// The labyrinth is dressed after the Mezquita of Córdoba and the Alhambra: horseshoe arches of alternating red brick
// and cream stone voussoirs, a gilded alfiz framing each arch with carved stucco in the spandrels, an azure frieze of
// eight-pointed stars under a dark cedar ceiling latticed with gold, columns of green-veined marble with gilded
// capitals, a floor of polished black marble with white veins.
//
// Every pattern is filtered by the ray's footprint `fw` (metres on the surface): a line is drawn with its coverage over
// the footprint, fine detail fades to its mean once it is smaller than the footprint. The footprint grows with the
// whole way the ray has come (a flat mirror does not change it, our sphere widens it), so behind a mirror a pattern is
// exactly as sharp, and as still, as seen straight at the same distance.
#pragma once

namespace ayna {
namespace mat {

// a hash and value noise (no random numbers: the same answer for the same place, every frame)
__device__ __forceinline__ float hash2(int x, int y) {
    unsigned h = unsigned(x) * 374761393u + unsigned(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xFFFFFFu) * (1.0f / 16777216.0f);
}
__device__ __forceinline__ float vnoise(float x, float y) {
    const float fx = floorf(x), fy = floorf(y);
    const int ix = int(fx), iy = int(fy);
    float u = x - fx, v = y - fy;
    u = u * u * (3.0f - 2.0f * u);
    v = v * v * (3.0f - 2.0f * v);
    const float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}
// fractal noise whose octaves fade out once their wavelength is under twice the footprint (in noise units)
__device__ float fbm(float x, float y, float fw, int oct) {
    float s = 0.0f, a = 0.5f, f = 1.0f, norm = 0.0f;
    for (int i = 0; i < oct; ++i) {
        const float k = saturate(1.0f - (fw * f - 0.25f) * 2.0f);   // 1 while the wavelength 1/f is above 4 fw
        if (k <= 0.0f) {   // this octave and the finer ones are under the footprint: their mean
            for (int j = i; j < oct; ++j) {
                s += a * 0.5f;
                norm += a;
                a *= 0.5f;
            }
            break;
        }
        s += a * (k * vnoise(x * f + 17.3f * i, y * f - 9.1f * i) + (1.0f - k) * 0.5f);
        norm += a;
        a *= 0.5f;
        f *= 2.03f;
    }
    return s / norm;
}

// the coverage of a line of half width hw at distance d, over a footprint fw (box filtered, smooth)
__device__ __forceinline__ float line(float d, float hw, float fw) {
    const float w = fmaxf(fw, 1e-5f);
    const float a = fabsf(d);
    return saturate((a + hw) / w + 0.5f) - saturate((a - hw) / w + 0.5f);
}
// a periodic line (period p, half width hw) of coordinate x, faded to its mean when the period gets small
__device__ __forceinline__ float lines(float x, float p, float hw, float fw) {
    const float d = x - p * floorf(x / p + 0.5f);
    const float mean = fminf(1.0f, 2.0f * hw / p);
    const float k = saturate((p / fmaxf(fw, 1e-6f) - 2.0f) * 0.5f);   // 1 while the period is above 4 footprints
    return line(d, hw, fw) * k + mean * (1.0f - k);
}

// an eight-pointed star lattice (two square grids, one turned 45 degrees): the coverage of its gold lines
__device__ float starLattice(float x, float y, float p, float hw, float fw) {
    const float a = fmaxf(lines(x, p, hw, fw), lines(y, p, hw, fw));
    const float r = 0.70710678f;
    const float b = fmaxf(lines((x + y) * r, p * r, hw, fw), lines((x - y) * r, p * r, hw, fw));
    return fmaxf(a, b);
}

struct Surface {
    float3 albedo;
    float glossy = 0.0f;   // 1: polished dielectric (Fresnel reflection continues the path)
    float3 bump{0, 0, 0};  // a tilt to the normal (relief), in world space
};

__device__ __forceinline__ float3 rgb(float r, float g, float b) { return make_float3(r, g, b); }

__device__ __forceinline__ float3 gold() { return make_float3(0.78f, 0.56f, 0.22f); }

// the voussoirs: index and position across one, from the angle round the arch's centre
__device__ float3 voussoir(const Lattice& g, float du, float y, float fw, float& joint) {
    const float thL = atan2f(g.yk - g.yc, -g.wHalf) + 2.0f * M_PIf, thR = atan2f(g.yk - g.yc, g.wHalf);
    float th = atan2f(y - g.yc, du);
    if (th < thR) th += 2.0f * M_PIf;
    const float span = thL - thR;
    const int N = 25;
    const float t = (thL - th) / span * float(N);   // 0 .. N round the arch
    const float r = sqrtf(du * du + sqr(y - g.yc));
    const float fa = fw / fmaxf(r, 0.1f) / span * float(N);   // the footprint in voussoir units
    const int idx = int(floorf(t));
    joint = line(t - floorf(t + 0.5f), 0.04f, fa);
    const float red = (idx & 1) ? 1.0f : 0.0f;
    // brick courses across the red ones (thin lines along the radius), faint
    const float courses = lines(r, 0.045f, 0.003f, fw) * red;
    const float3 brick = rgb(0.42f, 0.12f, 0.07f), stone = rgb(0.74f, 0.66f, 0.52f);
    float3 c = brick * red + stone * (1.0f - red);
    c = c * (1.0f - 0.35f * courses);
    // a little weathering
    const float wv = fbm(du * 6.0f, y * 6.0f, fw * 6.0f, 3);
    return c * (0.85f + 0.3f * wv);
}

// carved stucco (ataurique): an interlace of leaves as a height field, its coverage and slope
__device__ float stucco(float x, float y, float fw, float& gx, float& gy) {
    const float k = 2.0f * M_PIf / 0.11f;   // 11 cm repeat
    const float fade = saturate(1.0f - (fw * k / (2.0f * M_PIf) - 0.15f) * 3.0f);
    const float a = __sinf(k * x) * __cosf(k * y * 0.8660254f), b = __sinf(k * (0.5f * x + 0.8660254f * y));
    const float h = (a + b) * 0.5f;
    gx = (k * __cosf(k * x) * __cosf(k * y * 0.8660254f) + 0.5f * k * __cosf(k * (0.5f * x + 0.8660254f * y))) * 0.5f * fade;
    gy = (-0.8660254f * k * __sinf(k * x) * __sinf(k * y * 0.8660254f) + 0.8660254f * k * __cosf(k * (0.5f * x + 0.8660254f * y))) * 0.5f * fade;
    return h * fade;
}

// dark walnut, its grain along `a` (the frames round the windows below the arches)
__device__ float3 walnut(float a, float b, float fw) {
    const float g = fbm(a * 2.0f, b * 40.0f, fw * 40.0f, 3);
    return rgb(0.070f, 0.040f, 0.024f) * (0.85f + 0.3f * g);
}

// a slab's face (the side of the wall the cell sees): du from the window's middle, y up, e the edge's direction
__device__ Surface faceSurface(const Lattice& g, float du, float y, float3 e, float fw) {
    Surface s;
    const float3 plaster = rgb(0.70f, 0.64f, 0.53f);
    s.albedo = plaster;
    const float r = sqrtf(du * du + sqr(y - g.yc));
    const float ringW = 0.15f;
    const float ax = g.ra + ringW + 0.05f;                    // the alfiz's sides
    const float atop = g.yc + g.ra + ringW + 0.07f;           // its top
    // beside the window, below the arch: the frame's wood, with a gold fillet at the window's edge
    if (y < g.yk - 0.02f && fabsf(du) > g.wHalf - 0.001f) {
        s.albedo = walnut(y, du, fw);
        return s;
    }
    // the voussoir ring round the arch
    if (r <= g.ra + ringW && y >= g.yk - 0.02f) {
        float joint;
        s.albedo = voussoir(g, du, y, fw, joint);
        s.albedo = s.albedo * (1.0f - 0.55f * joint);
        // its outer rim, a thin gold fillet
        const float rim = line(r - (g.ra + ringW), 0.008f, fw);
        s.albedo = lerp3(s.albedo, gold(), rim);
        return s;
    }
    // the alfiz: a gilded frame round the arch
    const float frame = fmaxf(line(fabsf(du) - ax, 0.013f, fw) * (y > g.yk - 0.15f && y < atop + 0.013f ? 1.0f : 0.0f),
                              line(y - atop, 0.013f, fw) * (fabsf(du) < ax + 0.013f ? 1.0f : 0.0f));
    if (fabsf(du) < ax && y < atop && y > g.yk - 0.15f) {
        // the spandrels: carved stucco, a warm cream, the relief tilting the normal
        float gx, gy;
        const float h = stucco(du, y, fw, gx, gy);
        s.albedo = plaster * (0.92f + 0.10f * h);
        s.bump = e * (-0.035f * gx) + make_float3(0.0f, -0.035f * gy, 0.0f);
    }
    // the frieze under the ceiling: gold stars on azure
    const float f0 = g.Hc - 0.42f, f1 = g.Hc - 0.16f;
    if (y > f0 && y < f1) {
        const float st = starLattice(du + 7.0f, y - f0, 0.26f, 0.006f, fw);
        s.albedo = lerp3(rgb(0.05f, 0.11f, 0.28f), gold(), st);
    }
    const float bands = fmaxf(line(y - f0, 0.006f, fw), line(y - f1, 0.006f, fw));
    s.albedo = lerp3(s.albedo, gold(), fmaxf(frame, bands));
    // plaster's own faint unevenness
    s.albedo = s.albedo * (0.93f + 0.14f * fbm(du * 3.0f + 11.0f, y * 3.0f, fw * 3.0f, 3));
    return s;
}

// a window's reveal: the jambs are plaster, the arch's underside carries the voussoirs on
__device__ Surface revealSurface(const Lattice& g, float du, float y, float depthFromFace, bool arc, float fw) {
    Surface s;
    if (arc) {
        float joint;
        s.albedo = voussoir(g, du, y, fw, joint) * (1.0f - 0.55f * joint);
    } else {
        s.albedo = walnut(y, depthFromFace, fw);
    }
    return s;
}

// a column: base, green-veined marble shaft (polished), gilded capital, then the pier's plaster up to the ceiling
__device__ Surface columnSurface(const Lattice& g, float phi, float y, float fw, float seed) {
    Surface s;
    const float cap1 = g.yk, cap0 = g.yk - 0.24f, base = 0.16f;
    const float3 gld = gold();
    if (y > cap1) {
        s.albedo = rgb(0.70f, 0.64f, 0.53f);
        return s;
    }
    if (y > cap0) {
        // the capital: gilded, with carved leaves (vertical flutes) and a band at each end
        const float flutes = lines(phi * g.rc, 0.035f, 0.006f, fw);
        const float bands = fmaxf(line(y - cap0 - 0.02f, 0.008f, fw), line(y - cap1 + 0.03f, 0.012f, fw));
        s.albedo = gld * (0.75f + 0.25f * (1.0f - flutes)) * (1.0f - 0.3f * bands);
        return s;
    }
    if (y < base) {
        const float ring = line(y - base + 0.025f, 0.01f, fw);
        s.albedo = lerp3(rgb(0.10f, 0.13f, 0.11f), gld, ring);
        s.glossy = 1.0f - ring;
        return s;
    }
    // verde antico: dark green with pale veins, polished
    const float x = phi * g.rc * 4.0f + seed * 13.0f, z = y * 4.0f;
    const float w = fbm(x * 0.7f, z * 0.7f, fw * 2.8f, 4);
    const float vein = 1.0f - saturate(fabsf(__sinf((x + 3.0f * w) * 2.3f + z * 1.1f)) * 9.0f - fw * 40.0f);
    s.albedo = lerp3(rgb(0.04f, 0.09f, 0.07f), rgb(0.55f, 0.62f, 0.56f), 0.6f * vein * saturate(1.0f - fw * 30.0f) + 0.0f);
    s.albedo = s.albedo * (0.8f + 0.4f * w);
    s.glossy = 1.0f;
    return s;
}

// the ceiling: dark cedar, its grain along x, latticed with gilded eight-pointed stars
__device__ Surface ceilingSurface(float x, float z, float fw) {
    Surface s;
    const float grain = fbm(x * 40.0f, z * 2.0f, fw * 40.0f, 4);
    s.albedo = rgb(0.11f, 0.062f, 0.034f) * (0.75f + 0.5f * grain);
    const float st = starLattice(x, z, 0.52f, 0.009f, fw);
    s.albedo = lerp3(s.albedo, gold() * 0.9f, st);
    return s;
}

// the floor: polished black marble (Nero Marquina): a few long white veins running slant, a net of fine ones, in
// square slabs
__device__ Surface floorSurface(float x, float z, float fw) {
    Surface s;
    // turned 35 degrees, stretched along the veins: their iso-lines run long and gently bent
    const float u = x * 0.819f + z * 0.574f, w = -x * 0.574f + z * 0.819f;
    const float warp = fbm(u * 0.25f, w * 0.25f, fw * 0.25f, 2);
    const float n1 = fbm(u * 0.18f + 3.0f * warp, w * 1.1f, fw * 1.1f, 3);
    const float n2 = fbm(u * 0.5f - 7.0f, w * 2.6f + 2.0f * warp, fw * 2.6f, 3);
    const float big = line(n1 - 0.5f, 0.006f, fw * 1.1f * 0.25f);
    const float fine = line(n2 - 0.52f, 0.004f, fw * 2.6f * 0.25f);
    s.albedo = rgb(0.020f, 0.019f, 0.021f) + rgb(0.62f, 0.61f, 0.58f) * (0.75f * big + 0.28f * fine);
    const float joint = fmaxf(lines(x, 0.8f, 0.0015f, fw), lines(z, 0.8f, 0.0015f, fw));
    s.albedo = s.albedo * (1.0f - 0.6f * joint);
    s.glossy = 1.0f - 0.8f * joint;
    return s;
}

}   // namespace mat
}   // namespace ayna
