// Scene 7 "Ölçek": the deterministic renderer (see olcek_kernels.h). Ö0's placeholders: spheres, discs, clouds,
// points, the ground at the beach (the shore, the grains, the grain's top, its atoms) the density maps and the desert's field.
#include "scenes/olcek/olcek_kernels.h"
#include "shared/display.h"

namespace olcek {

namespace {

// the view in constant memory (as bytes: its members have default values, which constant memory cannot construct)
__constant__ __align__(16) unsigned char cViewBytes[sizeof(OlcekView)];
#define cV (*reinterpret_cast<const OlcekView*>(cViewBytes))

__device__ float3 sunTransmit(const GAtmo& A, float alt, float mu);   // the air (below)

__device__ __forceinline__ unsigned int pcg(unsigned int v) {
    const unsigned int state = v * 747796405u + 2891336453u;
    const unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
__device__ __forceinline__ float hash1(int x, int y, int z, int seed) {
    const unsigned int h = pcg(unsigned(x) * 73856093u ^ unsigned(y) * 19349663u ^ unsigned(z) * 83492791u ^ unsigned(seed) * 2654435761u);
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
// gradient (Perlin) noise in [0, 1]: value noise's flat lattice corners read as squares when magnified (the beach's foam
// was blocky); periodic in z with period pz (> 0) for the clocks
__device__ __forceinline__ float gradDot(int x, int y, int z, int seed, float dx, float dy, float dz) {
    const unsigned int h = pcg(unsigned(x) * 73856093u ^ unsigned(y) * 19349663u ^ unsigned(z) * 83492791u ^ unsigned(seed) * 2654435761u);
    switch (h % 12u) {   // the twelve edge directions of a cube
    case 0: return dx + dy;
    case 1: return -dx + dy;
    case 2: return dx - dy;
    case 3: return -dx - dy;
    case 4: return dx + dz;
    case 5: return -dx + dz;
    case 6: return dx - dz;
    case 7: return -dx - dz;
    case 8: return dy + dz;
    case 9: return -dy + dz;
    case 10: return dy - dz;
    default: return -dy - dz;
    }
}
__device__ float gnoise3(float x, float y, float z, int seed, int pz = 0) {
    const float fx = floorf(x), fy = floorf(y), fz = floorf(z);
    const int ix = int(fx), iy = int(fy), iz = int(fz);
    const float rx = x - fx, ry = y - fy, rz = z - fz;
    const float tx = fade5(rx), ty = fade5(ry), tz = fade5(rz);
    float v[2];
    for (int k = 0; k < 2; ++k) {
        int zz = iz + k;
        if (pz > 0) zz = ((zz % pz) + pz) % pz;
        const float dz = rz - float(k);
        const float a = gradDot(ix, iy, zz, seed, rx, ry, dz), b = gradDot(ix + 1, iy, zz, seed, rx - 1.0f, ry, dz);
        const float c = gradDot(ix, iy + 1, zz, seed, rx, ry - 1.0f, dz), d = gradDot(ix + 1, iy + 1, zz, seed, rx - 1.0f, ry - 1.0f, dz);
        v[k] = lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
    }
    return saturate(0.5f + 0.5f * lerpf(v[0], v[1], tz));
}

// 2D gradient noise with its derivatives (one hash a corner; the surfaces' height fields need the slope for the shading,
// and three evaluations per octave for finite differences were the frame's largest cost). Returns about -1..1.
__device__ __forceinline__ float2 grad2(int x, int y, int seed) {
    const unsigned int h = pcg(unsigned(x) * 73856093u ^ unsigned(y) * 19349663u ^ unsigned(seed) * 2654435761u);
    const float a = float(h >> 8) * (6.2831853f / 16777216.0f);
    return make_float2(cosf(a), sinf(a));
}
__device__ float gnoise2d(float x, float y, int seed, float2& d) {
    const float fx = floorf(x), fy = floorf(y);
    const int ix = int(fx), iy = int(fy);
    const float rx = x - fx, ry = y - fy;
    const float2 ga = grad2(ix, iy, seed), gb = grad2(ix + 1, iy, seed), gc = grad2(ix, iy + 1, seed), gd = grad2(ix + 1, iy + 1, seed);
    const float va = ga.x * rx + ga.y * ry, vb = gb.x * (rx - 1.0f) + gb.y * ry;
    const float vc = gc.x * rx + gc.y * (ry - 1.0f), vd = gd.x * (rx - 1.0f) + gd.y * (ry - 1.0f);
    const float ux = fade5(rx), uy = fade5(ry);
    const float dux = 30.0f * rx * rx * (rx - 1.0f) * (rx - 1.0f), duy = 30.0f * ry * ry * (ry - 1.0f) * (ry - 1.0f);
    const float k = va - vb - vc + vd;
    d.x = ga.x + ux * (gb.x - ga.x) + uy * (gc.x - ga.x) + ux * uy * (ga.x - gb.x - gc.x + gd.x) + dux * (uy * k + (vb - va));
    d.y = ga.y + ux * (gb.y - ga.y) + uy * (gc.y - ga.y) + ux * uy * (ga.y - gb.y - gc.y + gd.y) + duy * (ux * k + (vc - va));
    return (va + ux * (vb - va) + uy * (vc - va) + ux * uy * k) * 1.4f;
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

// ---- the maps (Ö1)
__device__ __forceinline__ float3 tex3(unsigned long long t, float u, float v, float lod) {
    const float4 c = tex2DLod<float4>(cudaTextureObject_t(t), u, v, lod);
    return make_float3(c.x, c.y, c.z);
}
__device__ __forceinline__ float4 tex4(unsigned long long t, float u, float v, float lod) {
    return tex2DLod<float4>(cudaTextureObject_t(t), u, v, lod);
}
// equirectangular: a unit direction in the body's axes (x towards longitude 0, z the north pole)
__device__ __forceinline__ float2 lonLatUV(float3 q) {
    const float lon = atan2f(q.y, q.x), lat = asinf(clampf(q.z, -1.0f, 1.0f));
    return make_float2(lon * (0.5f / M_PIf) + 0.5f, 0.5f - lat / M_PIf);
}

// The Earth's albedo from the maps: the world (averaged along the longitude by the shutter while it spins: taps at a
// level of detail as wide as their spacing, and its zonal mean when it turns more than once), the region around home
// and the home bay over it, each fading out over its outer tenth. fpM: the pixel's footprint on the ground (m).
__device__ float3 earthMaps(float3 q, float spin, float fpM, bool local, float lx, float ly) {
    const GMaps& M = cV.maps;
    const float texRad = 6.2831853f / M.worldTexels, cloudRad = 6.2831853f / 8192.0f;
    const float lodPix = log2f(fmaxf(fpM / 6371000.0f / texRad, 1e-4f));
    const float lodPixC = log2f(fmaxf(fpM / 6371000.0f / cloudRad, 1e-4f));
    const bool clouds = M.cloudWorld && M.cloudFade > 0.0f;
    const float2 uv = lonLatUV(q);
    // the ground and the clouds apart (the clouds of today's map near home replace the world's, not add to them)
    float3 ground, cloud = make_float3(0, 0, 0);
    float cloudA = 0.0f;
    if (spin < 1e-3f) {
        ground = tex3(M.world, uv.x, uv.y, lodPix) * M.worldGain;
        if (clouds) {
            const float4 k = tex4(M.cloudWorld, uv.x, uv.y, lodPixC);
            cloud = make_float3(k.x, k.y, k.z) * k.w;   // premultiplied
            cloudA = k.w;
        }
    } else {
        const float span = fminf(4.0f * spin, 6.2831853f);
        const int N = 16;
        const float spacing = span / N;
        const float lod = fmaxf(lodPix, log2f(spacing / texRad)), lodC = fmaxf(lodPixC, log2f(spacing / cloudRad));
        float3 acc = make_float3(0, 0, 0);
        float ws = 0.0f;
        for (int k = 0; k < N; ++k) {
            const float a = ((k + 0.5f) / N - 0.5f) * span;
            const float w = span >= 6.2831f ? 1.0f : __expf(-0.5f * sqr(a / spin));
            const float u = uv.x + a * (0.5f / M_PIf);
            acc += tex3(M.world, u, uv.y, lod) * w;
            if (clouds) {
                const float4 c4 = tex4(M.cloudWorld, u, uv.y, lodC);
                cloud += make_float3(c4.x, c4.y, c4.z) * (c4.w * w);
                cloudA += c4.w * w;
            }
            ws += w;
        }
        ground = acc * (M.worldGain / ws);
        cloud = cloud * (1.0f / ws);
        cloudA /= ws;
        if (M.zonal) ground = lerp3(ground, tex3(M.zonal, 0.5f, uv.y, 0.0f) * M.worldGain, smoothstepf(3.1416f, 6.2832f, 4.0f * spin));
    }
    if (local) {
        const float still = 1.0f - smoothstepf(0.002f, 0.02f, spin);   // the local maps turn with it, unblurred: only near
        if (M.region && still > 0.0f) {
            const float h = M.regionHalf, e = fmaxf(fabsf(lx), fabsf(ly)) / h;
            if (e < 1.0f) {
                const float w = still * smoothstepf(1.0f, 0.9f, e);
                const float u = (lx + h) / (2.0f * h), v = (h - ly) / (2.0f * h);
                ground = lerp3(ground, tex3(M.region, u, v, log2f(fmaxf(fpM / M.regionTexel, 1e-4f))) * M.worldGain, w);
                if (M.cloudRegion && clouds) {
                    const float4 k = tex4(M.cloudRegion, u, v, log2f(fmaxf(fpM / M.regionTexel, 1e-4f)));
                    cloud = lerp3(cloud, make_float3(k.x, k.y, k.z) * k.w, w);
                    cloudA = lerpf(cloudA, k.w, w);
                }
            }
        }
        if (M.bay && still > 0.0f) {
            const float h = M.bayHalf, e = fmaxf(fabsf(lx), fabsf(ly)) / h;
            if (e < 1.0f) {
                const float4 b = tex4(M.bay, (lx + h) / (2.0f * h), (h - ly) / (2.0f * h), log2f(fmaxf(fpM / M.bayTexel, 1e-4f)));
                const float w = still * b.w * smoothstepf(1.0f, 0.9f, e);
                ground = lerp3(ground, make_float3(b.x, b.y, b.z) * M.bayGain, w);
            }
        }
    }
    // the clouds over the ground (premultiplied), thinning as the camera comes down to them
    const float f = M.cloudFade;
    return ground * (1.0f - cloudA * f) + cloud * (M.cloudGain * f);
}

// ---- the beach (metres from the beach point: x east, y north); fp the pixel's footprint there (m): every edge is at
// least a pixel soft, a band thinner than a pixel fades to its mean (no shimmer)
// how far over the sea a point is (m; + water): the real shore (Sentinel-2's water mask) bent near the beach point so that
// the line passes it at the swash's reach; a placeholder line if there is no map
__device__ float seaDistance(float x, float y) {
    const GMaps& M = cV.maps;
    float d;
    if (M.shore) {
        const float h = M.shoreHalf;
        // bilinear in float on point samples (texel centres at i + 0.5)
        const float fx = (x + h) / (2.0f * h) * M.shoreSize - 0.5f, fy = (h - y) / (2.0f * h) * M.shoreSize - 0.5f;
        const float x0 = floorf(fx), y0 = floorf(fy), tx = fx - x0, ty = fy - y0;
        const cudaTextureObject_t T = cudaTextureObject_t(M.shore);
        const float a = tex2D<float>(T, x0 + 0.5f, y0 + 0.5f), b = tex2D<float>(T, x0 + 1.5f, y0 + 0.5f);
        const float c = tex2D<float>(T, x0 + 0.5f, y0 + 1.5f), e = tex2D<float>(T, x0 + 1.5f, y0 + 1.5f);
        d = lerpf(lerpf(a, b, tx), lerpf(c, e, tx), ty);
        d -= M.shoreCentre * __expf(-(x * x + y * y) / (150.0f * 150.0f));   // through the beach point (the map errs ~10 m)
    } else {
        d = y - 0.75f;
    }
    // the waterline's own small bends, along the shore
    return d + 0.8f * sinf(0.13f * x + 0.07f * y) + 0.35f * sinf(0.41f * x + 1.3f) + 0.15f * sinf(1.7f * x + 0.4f - 0.9f * y) - 0.75f;
}

__device__ float3 beachAlbedo(float x, float y, float fp, float t) {
    // the water's edge wanders at the centimetres too (the swash runs up unevenly), as far as a pixel can show it
    float dsea = seaDistance(x, y);
    dsea += 0.05f * (gnoise3(x * 9.0f, y * 9.0f, 2.0f, 61) - 0.5f) * smoothstepf(2.0f, 4.0f, 1.0f / (fp * 9.0f))
          + 0.02f * (gnoise3(x * 31.0f, y * 31.0f, 3.0f, 62) - 0.5f) * smoothstepf(2.0f, 4.0f, 1.0f / (fp * 31.0f));
    const float run = 0.5f * (0.5f + 0.5f * sinf(t * (6.2831853f / 8.0f)));   // the last wave runs up and back
    const float w = fmaxf(0.004f, fp);
    const float edge = -run;
    // the sand: dry, then darker where the waves wet it; grainy down to the pixel (octaves finer than a pixel fade out)
    // the sand: broad mottling, then nearly white noise down to the grains' clusters (~1 mm): an octave lives while its
    // wavelength is at least 3.5 pixels and fades out by 2 (finer would shimmer as the view moves)
    float grit = 0.0f, amp = 0.5f, f = 0.25f;
    for (int i = 0; i < 13; ++i) {
        const float lim = smoothstepf(2.0f, 3.5f, 1.0f / (fp * f));
        if (lim <= 0.0f) break;
        float2 dn;
        grit += amp * lim * 0.5f * gnoise2d(x * f + 0.37f * i, y * f - 0.21f * i, 40 + i, dn);
        f *= 2.3f;
        amp *= i < 4 ? 0.7f : 0.92f;
    }
    float3 sand = make_float3(0.60f, 0.51f, 0.38f) * (1.0f + 1.1f * grit);
    const float wet = smoothstepf(-1.4f - w, -0.5f + w, dsea);
    sand = lerp3(sand, sand * make_float3(0.55f, 0.55f, 0.58f), wet);
    // the swash: a film of water over the sand, deeper seaward (its colour over the sand, then the sea's)
    const float film = smoothstepf(edge - w, edge + w, dsea);
    const float depth = fmaxf(dsea - edge, 0.0f);
    const float3 seaCol = make_float3(0.035f, 0.10f, 0.11f);
    const float3 water = lerp3(sand * make_float3(0.62f, 0.75f, 0.78f), seaCol, 1.0f - __expf(-depth / 1.2f));
    float3 c = lerp3(sand, water, film);
    // foam: a lace at the edge and a little behind it (the wave's run back), broken by noise; fades below a pixel
    const float fw = 0.04f;
    // the lace: ridges of noise (bubbles gather in lines), finer octaves only while a pixel can hold them
    float lace = 0.0f, lw = 0.0f;
    {
        float fl = 7.0f, al = 0.5f;
        for (int i = 0; i < 4; ++i) {
            const float lim = smoothstepf(2.0f, 3.5f, 1.0f / (fp * fl));
            const float r = 1.0f - fabsf(2.0f * gnoise3(x * fl, y * fl, t * (0.5f + 0.3f * i), 3 + i) - 1.0f);
            lace += al * lim * r * r;
            lw += al * lim;
            fl *= 2.8f;
            al *= 0.75f;
        }
        lace = lw > 0.0f ? lace / lw : 0.0f;
    }
    const float foam = (__expf(-sqr((dsea - edge) / fmaxf(fw, fp))) + 0.3f * __expf(-sqr((dsea - edge - 0.22f) / fmaxf(0.12f, fp))))
                       * fminf(1.0f, fw / fp) * smoothstepf(0.45f, 0.85f, lace);
    c = c + make_float3(0.55f, 0.57f, 0.58f) * (foam * 0.85f);
    return c;
}

// the beach's own pattern only near the waterline: the land behind it and the open water from the maps
__device__ __forceinline__ float beachShare(float x, float y) {
    const float dsea = seaDistance(x, y);
    return smoothstepf(-32.0f, -18.0f, dsea) * (1.0f - smoothstepf(20.0f, 45.0f, dsea));
}

// ---- the sand grains (Ö4): rounded, irregular quartz-sand grains (ellipsoids of random shape and turn), two layers on a
// grid; the home grain's top is the beach point. Kinds by share: clear-to-yellow quartz, iron-stained quartz, feldspar,
// dark rock fragments, shell (a beach on a granite-and-volcanic coast).
struct Grain {
    float3 c, r;      // centre, semi-axes (along its own x, y, z)
    float ca, sa;     // its turn about the vertical
    int kind;         // 0 quartz, 1 iron-stained, 2 feldspar, 3 dark, 4 shell
    float tone;
};
__device__ Grain grainAt(int i, int j, int layer, float R) {
    const float cell = 2.0f * R;
    Grain g;
    const int seed = 101 + 17 * layer;
    const float a = hash1(i, j, 0, seed), b = hash1(i, j, 1, seed), c = hash1(i, j, 2, seed), e = hash1(i, j, 3, seed);
    const float f = hash1(i, j, 5, seed), h = hash1(i, j, 6, seed), k = hash1(i, j, 7, seed);
    const float size = R * (0.7f + 0.55f * a);
    g.r = make_float3(size * (0.95f + 0.25f * f), size * (0.75f + 0.2f * h), size * (0.6f + 0.25f * k));
    const float ang = 6.2831853f * e;
    g.ca = cosf(ang);
    g.sa = sinf(ang);
    const float off = layer ? 0.5f : 0.0f;
    g.c = make_float3((i + off + (b - 0.5f) * 0.55f) * cell, (j + off + (c - 0.5f) * 0.55f) * cell, -g.r.z - (layer ? 0.9f : 0.25f) * R * hash1(i, j, 8, seed) - (layer ? 0.55f * R : 0.0f));
    const float kk = hash1(i, j, 4, seed);
    g.kind = kk < 0.70f ? 0 : kk < 0.82f ? 1 : kk < 0.90f ? 2 : kk < 0.96f ? 3 : 4;
    g.tone = hash1(i, j, 9, seed);
    if (layer == 0 && i == 0 && j == 0) {   // the home grain: its top at the beach point, upright
        g.r = make_float3(R * 1.05f, R * 0.85f, R * 0.7f);
        g.c = make_float3(0, 0, -g.r.z);
        g.kind = 0;
        g.tone = 0.55f;
    }
    return g;
}
// the ray (metres, local axes) into the grains: the nearest hit (t in metres), its normal, kind and tone
__device__ bool hitGrains(float3 o, float3 d, float R, float& t, float3& n, float& tone, int& gi, int& gj, bool homeOnly = false) {
    if (d.z >= 0.0f) return false;
    const float t0 = (0.0f - o.z) / d.z, t1 = (-2.4f * R - o.z) / d.z;
    const float cell = 2.0f * R;
    const float3 a = o + d * fmaxf(t0, 0.0f), b = o + d * t1;
    float best = 1e30f;
    int bestKind = 0;
    float bestTone = 0.5f;
    for (int layer = 0; layer < 2; ++layer) {
        const float off = layer ? 0.5f : 0.0f;
        const int i0 = int(floorf(fminf(a.x, b.x) / cell + 0.5f - off)) - 1, i1 = int(floorf(fmaxf(a.x, b.x) / cell + 0.5f - off)) + 1;
        const int j0 = int(floorf(fminf(a.y, b.y) / cell + 0.5f - off)) - 1, j1 = int(floorf(fmaxf(a.y, b.y) / cell + 0.5f - off)) + 1;
        for (int i = homeOnly ? 0 : i0; i <= (homeOnly ? 0 : min(i1, i0 + 4)); ++i)
            for (int j = homeOnly ? 0 : j0; j <= (homeOnly ? 0 : min(j1, j0 + 4)); ++j) {
                if (homeOnly && layer) continue;
                const Grain g = grainAt(i, j, layer, R);
                // into the grain's unit sphere: turn about z, scale by the semi-axes
                const float3 oc = o - g.c;
                const float3 ou = make_float3((g.ca * oc.x + g.sa * oc.y) / g.r.x, (-g.sa * oc.x + g.ca * oc.y) / g.r.y, oc.z / g.r.z);
                const float3 du = make_float3((g.ca * d.x + g.sa * d.y) / g.r.x, (-g.sa * d.x + g.ca * d.y) / g.r.y, d.z / g.r.z);
                const float aa = dot(du, du);
                const float bq = -dot(ou, du);
                // |ou|^2 - 1 without cancellation for the home grain (its top is the beach point): at 10^-10 m above a
                // 0.25 mm grain the plain form loses every digit (DURUM Ö0)
                const bool home = layer == 0 && i == 0 && j == 0;
                const float cq = home ? ou.x * ou.x + ou.y * ou.y + (o.z / g.r.z) * (o.z / g.r.z + 2.0f) : dot(ou, ou) - 1.0f;
                const float disc = bq * bq - aa * cq;
                if (disc < 0.0f || bq <= 0.0f) continue;
                const float tt = cq / (bq + sqrtf(disc));   // the near root, stable
                if (tt > 0.0f && tt < best) {
                    best = tt;
                    const float3 pu = ou + du * tt;
                    const float3 nl = make_float3(pu.x / g.r.x, pu.y / g.r.y, pu.z / g.r.z);
                    n = normalize(make_float3(g.ca * nl.x - g.sa * nl.y, g.sa * nl.x + g.ca * nl.y, nl.z));
                    bestKind = g.kind;
                    bestTone = g.tone;
                    gi = i + 1000 * layer;
                    gj = j;
                }
            }
    }
    if (best < 1e29f) {
        t = best;
        tone = float(bestKind) + 0.99f * bestTone;   // kind in the integer part
        return true;
    }
    // between the grains: deeper sand, in their shadow
    t = t1;
    n = make_float3(0, 0, 1);
    tone = -1.0f;
    return true;
}

// a grain's colour by kind (albedo, linear)
__device__ float3 grainAlbedo(float tone) {
    const int kind = int(floorf(tone));
    const float v = tone - kind;
    switch (kind) {
    case 1: return lerp3(make_float3(0.62f, 0.38f, 0.20f), make_float3(0.75f, 0.52f, 0.30f), v);   // iron-stained quartz
    case 2: return lerp3(make_float3(0.72f, 0.58f, 0.52f), make_float3(0.82f, 0.74f, 0.70f), v);   // feldspar
    case 3: return lerp3(make_float3(0.10f, 0.10f, 0.10f), make_float3(0.24f, 0.22f, 0.20f), v);   // rock fragments
    case 4: return lerp3(make_float3(0.80f, 0.78f, 0.72f), make_float3(0.90f, 0.88f, 0.84f), v);   // shell
    default: return lerp3(make_float3(0.62f, 0.56f, 0.42f), make_float3(0.80f, 0.76f, 0.66f), v);  // quartz
    }
}

// The home grain's top, below a millimetre: frosted quartz (wind and wave wear: pits and V-shaped impact marks) on
// conchoidal fracture facets; then, under a micrometre, the crystal's terraces, one SiO2 layer high (0.54 nm, the c axis);
// a height field in metres, its slope for the shading. Octaves finer than ~2 pixels are left out (no shimmer).
__device__ float grainTopHeight(float x, float y, float fp, float& hx, float& hy) {
    float h = 0.0f;
    hx = hy = 0.0f;
    // facets: a Voronoi of planes (each cell its own tilt), cells ~40 um
    {
        const float cs = 40e-6f;
        const float u = x / cs, v = y / cs;
        const int ci = int(floorf(u)), cj = int(floorf(v));
        float d1 = 1e9f, d2 = 1e9f;
        int bi = 0, bj = 0;
        for (int i = ci - 1; i <= ci + 1; ++i)
            for (int j = cj - 1; j <= cj + 1; ++j) {
                const float px = i + hash1(i, j, 0, 401), py = j + hash1(i, j, 1, 401);
                const float dd = sqr(u - px) + sqr(v - py);
                if (dd < d1) { d2 = d1; d1 = dd; bi = i; bj = j; }
                else if (dd < d2) d2 = dd;
            }
        const float tx = (hash1(bi, bj, 2, 401) - 0.5f) * 0.6f, ty = (hash1(bi, bj, 3, 401) - 0.5f) * 0.6f;
        const float edge = smoothstepf(0.0f, 0.12f, sqrtf(d2) - sqrtf(d1));   // the ridges between facets, rounded
        const float vis = smoothstepf(2.0f, 4.0f, cs / fp);
        h += (tx * x + ty * y) * edge * vis;
        hx += tx * edge * vis;
        hy += ty * edge * vis;
    }
    // frosting and pits: gradient noise from ~20 um down to the pixel (finite differences of the octaves)
    float f = 1.0f / 20e-6f, amp = 1.0e-6f;
    for (int i = 0; i < 14; ++i) {
        const float lim = smoothstepf(2.0f, 3.5f, 1.0f / (fp * f));
        if (lim <= 0.0f) break;
        float2 dn;
        const float n0 = gnoise2d(x * f, y * f, 430 + i, dn);
        h += amp * lim * n0;
        hx += amp * lim * dn.x * f;
        hy += amp * lim * dn.y * f;
        f *= 2.2f;
        amp *= 0.5f;
    }
    return h;
}

// the crystal's terraces under ~100 nm: a vicinal surface (a few degrees off the basal plane) cut into steps of one layer;
// how near a step edge the point is (for the electron microscope's bright edges) through `edge` (0..1)
__device__ float terraceHeight(float x, float y, float fp, float& edge) {
    const float step = 0.5405e-9f;
    const float H = 0.06f * x + 0.035f * y + 2.0e-9f * (gnoise3(x / 60e-9f, y / 60e-9f, 0.2f, 470) - 0.5f) + 0.8e-9f * (gnoise3(x / 15e-9f, y / 15e-9f, 0.7f, 471) - 0.5f);
    const float u = H / step;
    const float fr = u - floorf(u);
    // distance to the nearest edge across the slope (m): |fr - round| * step / |grad H| (grad ~ the tilt)
    const float g = 0.07f;
    const float dist = fminf(fr, 1.0f - fr) * step / g;
    edge = __expf(-sqr(dist / fmaxf(1.5f * fp, 0.15e-9f)));
    return floorf(u) * step;
}

// the atoms of the top layer as a scanning probe draws them (Gaussian bumps), each vibrating (the averaged swing from the
// host): the top of a silica bilayer, the same net as the density map's (olcek_world.cpp): silicons on a honeycomb
// (Si-Si 3.06 A), a bridging oxygen halfway between each pair
__device__ float atomHeight(float x, float y, const GGround& g, float& hx, float& hy) {
    const float ss = 3.06e-10f, a = ss * 1.7320508f;   // the honeycomb's lattice constant
    // Bravais a1 = (a, 0), a2 = (a/2, a sqrt3/2); the home silicon (A site) at the origin, B at (0, ss) up the north
    const float v = y / (a * 0.8660254f), u = x / a - 0.5f * v;
    const int ci = int(floorf(u)), cj = int(floorf(v));
    float h = 0.0f;
    hx = hy = 0.0f;
    const float sSi = 0.45e-10f, sO = 0.55e-10f;
    for (int i = ci - 1; i <= ci + 1; ++i)
        for (int j = cj - 1; j <= cj + 1; ++j) {
            const float bx = (i + 0.5f * j) * a, by = j * a * 0.8660254f;
            // A at the corner, B at (0, ss); the three bridging oxygens of A: towards B's at 90, 210, 330 degrees
            const float mxs[5] = {0.0f, 0.0f, 0.0f, -0.4330127f * ss, 0.4330127f * ss};
            const float mys[5] = {0.0f, ss, 0.5f * ss, -0.25f * ss, -0.25f * ss};
            for (int m = 0; m < 5; ++m) {
                const bool si = m < 2;
                const float sig = si ? sSi : sO, amp = si ? 0.6f : 1.0f;
                const float p1 = 6.2831853f * hash1(i, j, m, 211), p2 = 6.2831853f * hash1(i, j, m + 5, 211);
                const float jx = g.jitter * sinf(g.vibPhase + p1), jy = g.jitter * sinf(g.vibPhase * 1.37f + p2);
                const float dx = x - (bx + mxs[m] + jx), dy = y - (by + mys[m] + jy);
                const float s2 = 2.0f * sig * sig;
                const float e = amp * __expf(-(dx * dx + dy * dy) / s2);
                h += e;
                hx += -2.0f * dx / s2 * e;
                hy += -2.0f * dy / s2 * e;
            }
        }
    return h;
}

// ---- the ground: its colour at a point (metres), lit by the Sun; tone < 0 between grains
__device__ float3 earthMaps(float3 q, float spin, float fpM, bool local, float lx, float ly);   // above

__device__ float3 groundShade(const GGround& g, float3 lp, float3 nl, float fp, float tone, int gi, int gj, bool onGrain) {
    const float3 sunL = make_float3(dot(g.sunDir, g.east), dot(g.sunDir, g.north), dot(g.sunDir, g.up));
    // the beach's own colour only where nothing below a millimetre covers it
    const bool covered = (onGrain && g.grains >= 1.0f && tone >= 0.0f) || g.surface >= 1.0f;
    float3 alb = make_float3(0.6f, 0.5f, 0.4f);
    if (!covered) {
        alb = beachAlbedo(lp.x, lp.y, fp, cV.beachClock);
        if (cV.maps.bay) {
            const float bs = beachShare(lp.x, lp.y);
            if (bs < 1.0f) alb = lerp3(earthMaps(make_float3(0, 0, 1), 0.0f, fp, true, lp.x, lp.y), alb, bs);
        }
    }
    float3 n = make_float3(0, 0, 1);
    float wrap = 0.0f, occl = 1.0f;
    if (onGrain && g.grains > 0.0f) {
        float3 ga;
        if (tone < 0.0f) {
            ga = make_float3(0.20f, 0.16f, 0.12f) * 0.35f;   // in the dark between them
        } else {
            ga = grainAlbedo(tone);
            // frosting on the grain at its own scale (~20-60 um), while a pixel can hold it
            const float fr = gnoise3(lp.x / 30e-6f, lp.y / 30e-6f, lp.z / 30e-6f, 480);
            ga = ga * (0.88f + 0.24f * fr * smoothstepf(2.0f, 4.0f, 30e-6f / fp));
            wrap = int(floorf(tone)) == 3 ? 0.05f : 0.35f;   // light through the clear grains (dark rock: none)
            occl = 0.55f + 0.45f * saturate(nl.z * 0.7f + 0.5f + lp.z / (2.0f * g.grainR));   // lower and facing down: darker
        }
        alb = lerp3(alb, ga, g.grains);
        n = normalize(lerp3(n, nl, g.grains));
        wrap *= g.grains;
        occl = lerpf(1.0f, occl, g.grains);
    }
    // the home grain's top as a surface: facets, frosting, pits (Ö4); no longer drawn once the microscope's eye has it
    float hx = 0.0f, hy = 0.0f;
    if (g.surface > 0.0f && g.steps < 1.0f) {
        grainTopHeight(lp.x, lp.y, fp, hx, hy);
        const float3 ns = normalize(make_float3(-hx, -hy, 1.0f));
        n = normalize(lerp3(n, ns, g.surface));
        alb = lerp3(alb, grainAlbedo(0.55f), g.surface);
        wrap = lerpf(wrap, 0.35f, g.surface);
        occl = lerpf(occl, 1.0f, g.surface);
    }
    float3 c;
    const float ndl = (dot(n, sunL) + wrap) / (1.0f + wrap);
    if (cV.atmo.on) {
        const GAtmo& A = cV.atmo;
        const float mu = sunL.z;
        const float3 Ts = sunTransmit(A, 0.0f, mu);
        const float3 sky = make_float3(0.10f, 0.13f, 0.20f) * saturate(mu * 1.5f + 0.15f);
        c = alb * (A.E * INV_PIf) * (Ts * fmaxf(ndl, 0.0f) + sky) * occl;
    } else {
        c = alb * (g.sunLit * (fmaxf(ndl, 0.0f) + 0.18f)) * occl;
    }
    // under the light's wavelength: an electron microscope's picture of the terraces (their tilt dims, their edges shine),
    // and on them, once a pixel can hold them, the atoms as a probe draws them
    if (g.steps > 0.0f) {
        float edge;
        terraceHeight(lp.x, lp.y, fp, edge);
        const float tilt = sqrtf(hx * hx + hy * hy);
        float v = 0.24f + 0.10f * fminf(tilt, 1.5f) + 0.42f * edge;
        if (g.atoms > 0.0f) {
            float ax, ay;
            const float h = atomHeight(lp.x, lp.y, g, ax, ay);
            const float A = 0.9f * g.atomSigma;
            const float3 na = normalize(make_float3(-ax * A, -ay * A, 1.0f));
            const float3 L = normalize(make_float3(-0.55f, 0.45f, 0.70f));
            const float va = 0.10f + 0.42f * fmaxf(dot(na, L), 0.0f) + 0.18f * h;
            v = lerpf(v, va * (0.75f + 0.6f * edge) , g.atoms);
        }
        c = lerp3(c, make_float3(v, v, v), g.steps);
    }
    return c;
}

// ---- the Earth's air (see GAtmo)
// Chapman's grazing incidence function, Schüler's approximation (GPU Pro 3): the optical depth along a ray from height h
// (in scale heights) at zenith cosine mu, relative to the vertical column above the ground; X = (Earth radius) / H
__device__ float chapman(float X, float h, float mu) {
    const float c = sqrtf(X + h);
    if (mu >= 0.0f) return c / (c * mu + 1.0f) * __expf(-h);
    const float x0 = sqrtf(fmaxf(1.0f - mu * mu, 0.0f)) * (X + h);
    const float c0 = sqrtf(x0);
    return 2.0f * c0 * __expf(X - x0) - c / (1.0f - c * mu) * __expf(-h);
}
// the sunlight's transmittance from a point at altitude alt (m) whose local vertical makes cosine mu with the Sun
__device__ float3 sunTransmit(const GAtmo& A, float alt, float mu) {
    const float Re = A.r * A.metres;
    alt = fmaxf(alt, 0.0f);
    // below the horizon by more than the Earth's own shadow allows: dark (the formula's far branch grows without bound)
    const float muH = -sqrtf(fmaxf(1.0f - sqr(Re / (Re + alt)), 0.0f));
    if (mu < muH - 0.02f) return make_float3(0, 0, 0);
    const float cR = chapman(Re / A.HR, alt / A.HR, mu) * A.HR, cM = chapman(Re / A.HM, alt / A.HM, mu) * A.HM;
    // ozone: a layer about 25 km up; its column scaled by the slant of the Rayleigh layer (close enough for a thin shell)
    const float cO = 15000.0f * (cR / fmaxf(A.HR * __expf(-alt / A.HR), 1.0f)) * (alt < 35000.0f ? 1.0f : __expf(-(alt - 35000.0f) / 5000.0f));
    const float3 tau = A.betaR * cR + make_float3(1, 1, 1) * ((A.betaM + A.absorbM) * cM) + A.betaO * cO;
    const float s = smoothstepf(muH - 0.02f, muH + 0.01f, mu);
    return make_float3(__expf(-tau.x), __expf(-tau.y), __expf(-tau.z)) * s;
}
// altitude (m) of the ray's point at t (camera units), without cancellation: |p - c|^2 - r^2 = t^2 - 2 t (d.c) + h (2r + h)
__device__ __forceinline__ float altitudeAt(const GAtmo& A, float dc, float t) {
    const float q = A.h * (2.0f * A.r + A.h);
    const float num = t * t - 2.0f * t * dc + q;
    const float len = sqrtf(fmaxf(t * t - 2.0f * t * dc + dot(A.c, A.c), 0.0f));
    return num / (len + A.r) * A.metres;
}
// the light the air scatters towards the camera along the ray up to tEnd, and its transmittance (both through `T`)
__device__ float3 airLight(const GAtmo& A, float3 d, float tEnd, float3& T) {
    T = make_float3(1, 1, 1);
    if (!A.on) return make_float3(0, 0, 0);
    // the ray's part inside the air: the shell of radius r + top
    const float dc = dot(d, A.c);
    const float Rt = A.r + A.top;
    const float ht = A.h - A.top;                       // the camera's height above the top (negative: inside)
    const float qt = ht * (2.0f * Rt + ht);             // |c|^2 - Rt^2
    const float disc = dc * dc - qt;
    if (disc <= 0.0f) return make_float3(0, 0, 0);
    const float sq = sqrtf(disc);
    float t0 = ht > 0.0f ? (dc > 0.0f ? qt / (dc + sq) : 1e30f) : 0.0f;
    const float t1 = fminf(dc + sq, tEnd);
    if (t0 >= t1) return make_float3(0, 0, 0);
    const int N = 20;
    const float cosT = dot(d, A.sun);
    const float pR = 3.0f / (16.0f * M_PIf) * (1.0f + cosT * cosT);
    const float g = A.g, g2 = g * g;
    const float pM = 3.0f / (8.0f * M_PIf) * (1.0f - g2) * (1.0f + cosT * cosT) / ((2.0f + g2) * powf(1.0f + g2 - 2.0f * g * cosT, 1.5f));
    float3 L = make_float3(0, 0, 0), tau = make_float3(0, 0, 0);
    // steps denser where the air is: towards the ray's lowest point (its closest approach to the centre, or its end on the
    // ground), half of them on either side, spaced as u^2
    const float tc = fminf(fmaxf(dc, t0), t1);
    float tPrev = t0;
    for (int i = 0; i < N; ++i) {
        float t;
        if (i < N / 2) {
            const float u = 1.0f - (i + 1.0f) / (N / 2);
            t = tc - (tc - t0) * u * u;
        } else {
            const float u = (i + 1.0f - N / 2) / (N / 2);
            t = tc + (t1 - tc) * u * u;
        }
        const float dtm = (t - tPrev) * A.metres;
        const float tm = 0.5f * (t + tPrev);
        tPrev = t;
        const float alt = fmaxf(altitudeAt(A, dc, tm), 0.0f);
        const float rR = __expf(-alt / A.HR), rM = __expf(-alt / A.HM);
        const float rO = fmaxf(0.0f, 1.0f - fabsf(alt - 25000.0f) / 15000.0f);
        const float3 ext = A.betaR * rR + make_float3(1, 1, 1) * ((A.betaM + A.absorbM) * rM) + A.betaO * rO;
        const float3 tauMid = tau + ext * (0.5f * dtm);
        // the local vertical at that point: (p - c) normalised
        const float3 pc = d * tm - A.c;
        const float mu = dot(pc, A.sun) * rsqrtf(fmaxf(dot(pc, pc), 1e-30f));
        const float3 Ts = sunTransmit(A, alt, mu);
        const float3 Tv = make_float3(__expf(-tauMid.x), __expf(-tauMid.y), __expf(-tauMid.z));
        L += (A.betaR * (rR * pR) + make_float3(1, 1, 1) * (A.betaM * rM * pM)) * Ts * Tv * dtm;
        tau += ext * dtm;
    }
    T = make_float3(__expf(-tau.x), __expf(-tau.y), __expf(-tau.z));
    return L * A.E * A.fade;
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

// ---- the Milky Way (see GGalaxy). R(beta) of an arm: Reid's kinked log-periodic spiral, ln(R/Rk) = -(beta - bk) tan(psi),
// continued past its measured azimuths with a 12-degree pitch (the far side is not measured: a representation)
__device__ float armRadius(const GArm& a, float beta) {
    const float tanExt = 0.2126f;   // tan 12
    const float b = fminf(fmaxf(beta, a.betaMin), a.betaMax);
    const float t = b <= a.betaKink ? a.tanLo : a.tanHi;
    float lnR = logf(a.Rkink) - (b - a.betaKink) * t;
    lnR -= (beta - b) * tanExt;
    return __expf(lnR);
}
// the arm's density at (R, beta): Gaussian across it, on every winding near the point; its distance (kpc, signed: < 0
// inside, towards the centre) to the nearest winding through `side`
__device__ float armDensity(const GArm& a, float R, float beta, float& side) {
    float best = 0.0f;
    side = 1e9f;
    for (int k = -1; k <= 2; ++k) {
        const float bb = beta + 6.2831853f * k;
        // an arm lives from the bar's ends (~3.5 kpc) to the disc's edge, and only so far past its measured azimuths
        const float span = fmaxf(a.betaMin - bb, bb - a.betaMax);
        const float reach = 1.0f - smoothstepf(a.ext * 0.6f, a.ext, span);
        if (reach <= 0.0f) continue;
        const float Ra = armRadius(a, bb);
        if (Ra < 3.3f || Ra > 16.5f) continue;   // one winding: from the bar's end to the disc's edge
        const float w = fmaxf(a.width, 0.336f + 0.036f * (Ra - 8.15f)) * 0.7f;
        const float dr = (R - Ra) * 0.97f;   // ~ across the arm (cos of the pitch)
        const float v = __expf(-0.5f * dr * dr / (w * w)) * reach * smoothstepf(3.3f, 4.3f, Ra) * (1.0f - smoothstepf(12.5f, 16.5f, Ra));
        if (v > best) best = v;
        if (fabsf(dr / w) < fabsf(side)) side = dr / w;
    }
    return best * a.strength;
}

__device__ float3 galaxyLight(const GGalaxy& g, float3 d, float tmax, float pixKpc) {
    const float dn = dot(d, g.n);
    if (fabsf(dn) < 1e-6f) return make_float3(0, 0, 0);
    const float t = dot(g.c, g.n) / dn;
    if (t <= 0.0f || t >= tmax) return make_float3(0, 0, 0);
    const float3 p = (d * t - g.c) * (1.0f / g.kpc);   // kpc, in the plane
    const float x = dot(p, g.e0), y = dot(p, g.e1);
    const float R = sqrtf(x * x + y * y);
    if (R > 25.0f) return make_float3(0, 0, 0);
    const float beta = atan2f(y, x);
    const float fp = pixKpc * t / fabsf(dn) * 0.5f;    // a pixel's footprint there (kpc)
    // the old disc: exponential, its edge softening beyond ~15 kpc
    float3 col = g.disc * (__expf(-R / g.hDisc) * (1.0f - smoothstepf(13.0f, 20.0f, R)) * 0.55f);
    // the bar and the bulge: an elongated exponential along the bar, a round core
    const float ca = cosf(g.barAngle), sa = sinf(g.barAngle);
    const float bx = x * ca + y * sa, by = -x * sa + y * ca;
    const float bar = __expf(-sqrtf(sqr(bx / (g.barHalf * 0.33f)) + sqr(by / 0.55f)));
    const float core = __expf(-R / 0.35f);
    col += g.bulge * (0.6f * bar + 0.4f * core);
    // the arms: young stars and nebulae in clumps; dust on their inner edges
    float young = 0.0f, lane = 0.0f;
    for (int i = 0; i < g.nArms; ++i) {
        float side;
        const float a = armDensity(g.arms[i], R, beta, side);
        young += a;
        lane += g.arms[i].strength * __expf(-0.5f * sqr((side + 0.9f) / 0.45f)) * smoothstepf(0.05f, 0.3f, a + 0.05f);
    }
    // clumps: noise in the plane, stretched along the rotation (star clouds sheared by it); octaves finer than ~2 pixels fade
    float clump = 0.0f, amp = 0.5f, f = 1.6f, wsum = 0.0f;
    const float ux = x * 0.9f + y * 0.43f, uy = -x * 0.43f + y * 0.9f;
    for (int o = 0; o < 11; ++o) {
        const float lim = saturate(1.0f - 2.0f * fp * f);
        if (lim <= 0.0f) break;
        clump += amp * lim * (gnoise3(ux * f, uy * f * 0.55f, 1.7f * o, 900 + o) - 0.5f);
        wsum += amp;
        f *= 2.1f;
        amp *= 0.66f;
    }
    clump = 0.5f + clump / fmaxf(wsum, 1e-3f) * 1.6f;
    const float knots = smoothstepf(0.55f, 0.9f, clump);
    col += g.disc * (__expf(-R / g.hDisc) * 0.35f * fminf(young, 1.0f));   // the old stars crowd into the arms a little too
    col += g.young * (young * (0.12f + 0.5f * clump + 1.1f * knots));   // a band of young light, clumpy, with brighter star clouds
    col += g.nebula * (young * sqr(sqr(knots)) * 3.0f);              // Halpha in the brightest knots only
    // dust: lanes plus a thin general layer, filaments from the same noise; the light passes a mixed layer: (1 - e^-tau)/tau
    float filament = 0.0f;
    {
        float a2 = 0.5f, f2 = 1.2f, w2 = 0.0f;
        for (int o = 0; o < 10; ++o) {
            const float lim = saturate(1.0f - 2.0f * fp * f2);
            if (lim <= 0.0f) break;
            filament += a2 * lim * (gnoise3(ux * f2 * 1.3f, uy * f2 * 0.45f, 5.3f + o, 950 + o) - 0.5f);
            w2 += a2;
            f2 *= 2.2f;
            a2 *= 0.62f;
        }
        filament = 0.5f + filament / fmaxf(w2, 1e-3f) * 1.6f;
    }
    const float tau = g.dust * (lane * (0.2f + 2.2f * smoothstepf(0.45f, 0.8f, filament)) + 0.35f * __expf(-R / 3.2f) * smoothstepf(0.3f, 0.7f, filament));
    const float3 tauRGB = make_float3(tau * 0.75f, tau * 1.0f, tau * 1.3f);   // reddening
    const float3 trans = make_float3(tauRGB.x > 1e-4f ? (1.0f - __expf(-tauRGB.x)) / tauRGB.x : 1.0f,
                                     tauRGB.y > 1e-4f ? (1.0f - __expf(-tauRGB.y)) / tauRGB.y : 1.0f,
                                     tauRGB.z > 1e-4f ? (1.0f - __expf(-tauRGB.z)) / tauRGB.z : 1.0f);
    col = col * trans;
    // seen at a slant the column is longer (a thin, optically thin layer), up to the disc's thickness
    return col * (1.0f / fmaxf(fabsf(dn), 0.08f));
}

// ---- the cosmic web (see GWeb). Seen through gigaparsecs its layers pile up into an even fog (the short form of
// Olbers' paradox: every picture of it, from surveys or simulations, is a slice). So the eye of this scale sees a slice
// through the focus, across the view, a few tens of Mpc thick (Gaussian, sigma 20 Mpc): through us when we are the
// focus, the slice of the universe we stand in. The galaxies' light follows the matter with a bias (rho^1.6: they
// gather more than it does). Steps across the slice, each reading the field prefiltered to the pixel's footprint.
__device__ float3 webLight(const GWeb& w, float3 d, float tmax, float pixAngle) {
    const float sigU = 20.0f * w.mpc;                    // the slice's sigma (camera units)
    if (d.z <= 1e-4f) return make_float3(0, 0, 0);
    const float t0 = fmaxf((1.0f - 3.0f * sigU) / d.z, 0.0f), t1 = fminf((1.0f + 3.0f * sigU) / d.z, tmax);
    if (t1 <= t0) return make_float3(0, 0, 0);
    const int N = 24;
    const float dt = (t1 - t0) / N;
    float3 L = make_float3(0, 0, 0);
    const float invA = 1.0f / (w.boxA * w.mpc), invB = 1.0f / (w.boxB * w.mpc);
    for (int i = 0; i < N; ++i) {
        const float tm = t0 + (i + 0.5f) * dt;
        const float3 p = d * tm - w.c;                      // from us
        const float r = length(p);
        if (r > w.R) continue;
        const float rm = r / w.mpc;                          // Mpc
        // the survey is flux-limited: the light it misses grows with distance (its luminosity-weighted completeness ~0.6 at
        // 100 Mpc, ~0.15 at 200); the web fills in what it misses
        const float inner = 1.0f - __expf(-sqr(rm / (0.7f * w.inner)));
        if (inner <= 0.0f) continue;
        const float slab = __expf(-0.5f * sqr((d.z * tm - 1.0f) / sigU));
        const float lenMpc = tm * pixAngle / w.mpc;
        const float lodA = log2f(fmaxf(lenMpc / w.cell, 1e-3f));
        const float lodB = log2f(fmaxf(lenMpc / (w.cell * 2.55f), 1e-3f));
        const float a = tex3DLod<float>(cudaTextureObject_t(w.field), p.x * invA, p.y * invA, p.z * invA, lodA);
        const float3 q = make_float3(dot(p, w.bx), dot(p, w.by), dot(p, w.bz));
        const float b = tex3DLod<float>(cudaTextureObject_t(w.field), q.x * invB + 0.37f, q.y * invB + 0.11f, q.z * invB + 0.73f, lodB);
        const float rho = powf(fmaxf(a * (0.35f + 0.65f * b), 0.0f), 1.6f);
        const float u = fminf(r / w.R, 1.0f) * 32.0f;
        const int k = min(int(u), 31);
        const float dim = lerpf(w.dim[k], w.dim[k + 1], u - float(k));
        L += w.j0 * (rho * dim * inner * slab * dt);
    }
    return L;
}

// ---- the density map (see GDensity, olDensity). On the plane through the focus, across the view (the crystallographer's picture, then the nuclear and the
// parton physicist's): lines where the density crosses a power of ten, one pixel wide whatever the zoom (their width
// from the gradient across a pixel: crisp as a drawing, no grain; where they would crowd, they fade), on a fill by
// the density over this frame's range, hill-shaded as if log10 of it were height. One evaluation and two neighbours.
// a map's picture from log10 of its quantity at the pixel and its neighbours (x0, and the steps gx, gy): lines at the
// powers of ten, the fill over [lo, hi], the hill-shading
__device__ float mapShade(float x0, float gx, float gy, float lo, float hi, float relief) {
    const float g = sqrtf(gx * gx + gy * gy);
    const float r = x0 - floorf(x0 + 0.5f);                   // from the nearest power of ten
    const float line = __expf(-sqr(r / fmaxf(g * 0.75f, 1e-6f))) * (1.0f - smoothstepf(0.2f, 0.5f, g));
    const float t = saturate((x0 - lo) / fmaxf(hi - lo, 1e-3f));
    const float k = relief * cV.width;                        // height per decade, in pixels
    const float3 n = normalize(make_float3(-gx * k, gy * k, 1.0f));   // screen y runs down
    const float shade = 0.55f + 0.45f * (n.x * -0.5f + n.y * 0.5f + n.z * 0.7071f) / 0.7071f;
    const float fill = (0.004f + 0.75f * t * t) * fmaxf(shade, 0.15f);
    return fill + (0.05f + 0.30f * t) * line;
}
// The silica net's electrons at p (camera space): the AFM's lattice (atomHeight: the same sites, the same thermal jitter),
// each atom its Slater density, the apical oxygens under the silicons in 3D; atoms beyond 4.5 A left out (their tails
// are 10^-9 of what is there). The home silicon is not here: it is a body, carrying the nucleus's swing.
__device__ float latticeDensity(float3 p) {
    const GGround& g = cV.ground;
    const float3 rel = (p - g.o) * g.metres;
    const float x = dot(rel, g.east), y = dot(rel, g.north), z = dot(rel, g.up);
    const float ss = 3.06e-10f, a = ss * 1.7320508f, apical = 1.61e-10f, bo = 0.3e-10f, inva0 = 1.0f / 0.529177e-10f;
    const float v = y / (a * 0.8660254f), u = x / a - 0.5f * v;
    const int ci = int(floorf(u)), cj = int(floorf(v));
    const float mxs[7] = {0.0f, 0.0f, 0.0f, -0.4330127f * ss, 0.4330127f * ss, 0.0f, 0.0f};
    const float mys[7] = {0.0f, ss, 0.5f * ss, -0.25f * ss, -0.25f * ss, 0.0f, ss};
    const float mzs[7] = {0.0f, 0.0f, bo, bo, bo, -apical, -apical};
    const float cut2 = 4.5e-10f * 4.5e-10f, cutJ2 = sqr(4.5e-10f + 1.5f * g.jitter);
    float rho = 0.0f;
    for (int i = ci - 1; i <= ci + 1; ++i)
        for (int j = cj - 1; j <= cj + 1; ++j) {
            const float bx = (i + 0.5f * j) * a, by = j * a * 0.8660254f;
            for (int m = 0; m < 7; ++m) {
                if (i == 0 && j == 0 && m == 0) continue;
                float dx = x - (bx + mxs[m]), dy = y - (by + mys[m]);
                const float dz = z - mzs[m];
                if (dx * dx + dy * dy + dz * dz > cutJ2) continue;   // out of reach even with its jitter
                const int hm = m < 5 ? m : m + 5;   // the AFM's hashes for its five; the apical ones their own
                const float p1 = 6.2831853f * hash1(i, j, hm, 211), p2 = 6.2831853f * hash1(i, j, hm + 5, 211);
                dx -= g.jitter * __sinf(g.vibPhase + p1);
                dy -= g.jitter * __sinf(g.vibPhase * 1.37f + p2);
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > cut2) continue;
                rho += olAtomDensity(m < 2 ? 0 : 1, sqrtf(d2) * inva0);
            }
        }
    return rho;
}
__device__ __forceinline__ float densityAt(const GDensity& D, float3 p) {
    return olDensity(D, p) + (D.lattice ? latticeDensity(p) : 0.0f);
}
__device__ float3 densitySlice(const GDensity& D, float px, float py) {
    const float3 d0 = rayDir(px, py), dx = rayDir(px + 1.0f, py), dy = rayDir(px, py + 1.0f);
    const float3 p0 = d0 * (1.0f / d0.z), p1 = dx * (1.0f / dx.z), p2 = dy * (1.0f / dy.z);
    const float x0 = log10f(densityAt(D, p0));
    const float gx = log10f(densityAt(D, p1)) - x0, gy = log10f(densityAt(D, p2)) - x0;   // decades per pixel
    const float v = D.weight * mapShade(x0, gx, gy, D.lo, D.hi, D.relief);
    return make_float3(v, v, v);
}

// ---- the desert and the bottom (see GField)
// the couplings at distance r (metres, as log10): 1/alpha_i(mu) = 1/alpha_i(M_Z) - b_i / (2 pi) ln(mu / M_Z), b = (41/10,
// -19/6, -7), 1/alpha(M_Z) = (59.0, 29.6, 8.47); mu = hbar c / r (1.97327e-16 GeV m), M_Z = 91.1876 GeV; in lobes
// (strong up, weak lower left, hypercharge lower right), which close in where the quark's smearing is
__device__ float fieldCoupling(float log10r, float theta, float lobes) {
    const float L = -40.6747f - 2.302585f * log10r;
    const float a1 = 1.0f / fmaxf(59.0f - 0.652535f * L, 1.0f);
    const float a2 = 1.0f / fmaxf(29.6f + 0.504049f * L, 1.0f);
    const float a3 = 1.0f / fmaxf(8.47f + 1.114085f * L, 1.0f);
    const float k = 3.0f * lobes;
    const float c3 = __expf(k * __cosf(theta - 1.570796f)), c2 = __expf(k * __cosf(theta - 3.665191f));
    const float c1 = __expf(k * __cosf(theta + 0.523599f));
    return (a1 * c1 + a2 * c2 + a3 * c3) / (c1 + c2 + c3);
}
// the foam's cells: Voronoi cells, their walls bent by a warp (bubbles, not plates), whose points and shifts wander
// with their clock; the shift of the nearest, blended to the pair's mean across a thin wall (the distances jump there:
// the rings crack). wall: how near the wall (0..1). Returns the shift in screen widths.
__device__ float2 foamCells(const GField& F, float x, float y, float& wall) {
    float cx = x * F.cellFreq, cy = y * F.cellFreq;
    cx += 0.7f * (gnoise3(cx * 0.8f, cy * 0.8f, F.cellPhase, 1305, 256) - 0.5f);
    cy += 0.7f * (gnoise3(cx * 0.8f + 5.3f, cy * 0.8f, F.cellPhase, 1306, 256) - 0.5f);
    const int ix = int(floorf(cx)), iy = int(floorf(cy));
    float d1 = 1e9f, d2 = 1e9f;
    int b1x = 0, b1y = 0, b2x = 0, b2y = 0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            const int gx = ix + i, gy = iy + j;
            const float px = gx + 0.5f + 0.8f * (gnoise3(gx * 1.37f, gy * 2.11f, F.cellPhase, 1301, 256) - 0.5f);
            const float py = gy + 0.5f + 0.8f * (gnoise3(gx * 1.37f, gy * 2.11f, F.cellPhase, 1302, 256) - 0.5f);
            const float d = sqrtf(sqr(cx - px) + sqr(cy - py));
            if (d < d1) { d2 = d1; b2x = b1x; b2y = b1y; d1 = d; b1x = gx; b1y = gy; }
            else if (d < d2) { d2 = d; b2x = gx; b2y = gy; }
        }
    auto shift = [&](int gx, int gy) {
        return make_float2(gnoise3(gx * 0.91f, gy * 1.73f, F.cellPhase, 1303, 256) - 0.5f,
                           gnoise3(gx * 0.91f, gy * 1.73f, F.cellPhase, 1304, 256) - 0.5f);
    };
    const float2 s1 = shift(b1x, b1y), s2 = shift(b2x, b2y);
    const float e = d2 - d1;   // ~ twice the distance to the wall (cells)
    const float px = 2.0f * F.cellFreq / cV.width;           // e across one pixel
    const float k = smoothstepf(0.0f, fminf(8.0f * px, 0.06f), e);
    wall = __expf(-sqr(e / (1.5f * px)));
    return make_float2((s1.x * (1.0f + k) + s2.x * (1.0f - k)) * F.cellAmp, (s1.y * (1.0f + k) + s2.y * (1.0f - k)) * F.cellAmp);
}
__device__ float loopDist2(const GField& F, float x, float y) {   // the squared distance to the string's loop
    float m = 1e30f;
    float2 a = F.pts[kLoopPoints - 1];
    for (int k = 0; k < kLoopPoints; ++k) {
        const float2 b = F.pts[k];
        const float ex = b.x - a.x, ey = b.y - a.y, px = x - a.x, py = y - a.y;
        const float t = saturate((px * ex + py * ey) / fmaxf(ex * ex + ey * ey, 1e-20f));
        m = fminf(m, sqr(px - ex * t) + sqr(py - ey * t));
        a = b;
    }
    return m;
}
// cell: the foam cells' shift (found once a pixel: its neighbours, for the slope, take the same)
__device__ float fieldLog(const GField& F, float x, float y, float2 cell, float* toLoop = nullptr) {
    float dx = cell.x, dy = cell.y;
    for (int i = 0; i < F.octaves; ++i) {   // the foam: every distance uncertain by ~ the Planck length
        const float f = F.freq[i];
        dx += F.amp[i] * (gnoise3(x * f, y * f, F.phase[i], 800 + (F.index[i] & 255), 256) - 0.5f);
        dy += F.amp[i] * (gnoise3(x * f + 17.3f, y * f - 9.1f, F.phase[i], 1100 + (F.index[i] & 255), 256) - 0.5f);
    }
    x += dx;
    y += dy;
    const float d2 = F.loop ? loopDist2(F, x, y) : x * x + y * y;
    if (toLoop) *toLoop = sqrtf(d2);
    const float s2 = F.sigma * F.sigma + F.loopBlur * F.loopBlur;
    const float lobes = smoothstepf(0.0f, 3.0f * F.sigma, sqrtf(x * x + y * y));
    const float A = fieldCoupling(0.5f * log10f(fmaxf(d2, s2)) + F.logW, atan2f(y, x), lobes);
    return log10f(A) - 2.0f * log10f(d2 + s2);
}
__device__ float3 fieldSlice(const GField& F, float px, float py) {
    const float3 d0 = rayDir(px, py), dx = rayDir(px + 1.0f, py), dy = rayDir(px, py + 1.0f);
    const float sc = 1.0f / (2.0f * cV.tanHalfX);   // camera units on the focus plane -> screen widths
    float wall = 0.0f, toLoop = 0.0f;
    const float ux = d0.x / d0.z * sc, uy = d0.y / d0.z * sc;
    const float2 cell = F.cellAmp > 0.0f ? foamCells(F, ux, uy, wall) : make_float2(0.0f, 0.0f);
    const float x0 = fieldLog(F, ux, uy, cell, &toLoop);
    const float gx = fieldLog(F, dx.x / dx.z * sc, dx.y / dx.z * sc, cell) - x0;
    const float gy = fieldLog(F, dy.x / dy.z * sc, dy.y / dy.z * sc, cell) - x0;
    float v = mapShade(x0, gx, gy, F.lo, F.hi, F.relief);
    // the foam's walls, as bright as the cells' shift is strong
    v += 0.15f * wall * saturate(F.cellAmp * 8.0f);
    // the string: where the shutter finds it. Its swing's average across the loop is the arcsine law of a sine's
    // positions (bright at the turning points), widened by its own width as a Lorentzian: (1/pi) Re 1/sqrt(A^2 - z^2)
    // at z = delta + i gamma. Normalised so that a still string is 1 at its middle.
    if (F.line > 0.0f) {
        const float A = 1.41421f * F.loopBlur, g = F.lineW;
        const float zr = toLoop, zi = g;
        const float wr = A * A - (zr * zr - zi * zi), wi = -2.0f * zr * zi;   // A^2 - z^2
        const float mod = sqrtf(wr * wr + wi * wi);
        const float sr = sqrtf(fmaxf(0.5f * (mod + wr), 0.0f)), si = copysignf(sqrtf(fmaxf(0.5f * (mod - wr), 0.0f)), wi);
        const float p = sr / fmaxf(sr * sr + si * si, 1e-20f);                // Re 1 / sqrt(w)
        v += F.line * 1.6f * g * p;
    }
    v *= F.weight;
    return make_float3(v, v, v);
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
            if (hitGrains(o, dl, g.grainR, tg, n, tone, gi, gj, g.surface >= 1.0f)) {
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
            if (b.pattern == 1 && cV.maps.world) {
                const float cosI = fmaxf(-dot(n, d), 0.05f);
                const float fpM = tBest * cV.metres * cV.pixAngle / sqrtf(cosI);
                float lx = 0.0f, ly = 0.0f;
                const bool local = g.frame != 0;
                if (local) {
                    const float3 lp3 = p - g.o;
                    lx = dot(lp3, g.east) * g.metres;
                    ly = dot(lp3, g.north) * g.metres;
                }
                alb = earthMaps(q, b.spinBlur, fpM, local, lx, ly);
                if (g.on && g.beach > 0.0f) {
                    const float fp = tBest * g.metres * cV.pixAngle;
                    alb = lerp3(alb, beachAlbedo(lx, ly, fp, cV.beachClock), g.beach * beachShare(lx, ly));
                }
            } else if (b.pattern == 1) {
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
                    alb = lerp3(alb, beachAlbedo(x, y, fp, cV.beachClock), g.beach);
                }
            } else if (b.pattern == 2) {
                if (cV.maps.moon) {
                    const float2 uv = lonLatUV(q);
                    const float fpRad = tBest * cV.pixAngle / fmaxf(b.r, 1e-30f);
                    alb = tex3(cV.maps.moon, uv.x, uv.y, log2f(fmaxf(fpRad * cV.maps.moonTexels / 6.2831853f, 1e-4f))) * cV.maps.moonGain;
                } else {
                    alb = moonAlbedo(q);
                }
            }
            if (b.pattern == 1 && cV.atmo.on) {
                // the sunlight at the ground through the air, and the sky's light (a share of it, by the sun's height)
                const GAtmo& A = cV.atmo;
                const float mu = dot(n, A.sun);
                const float3 Ts = sunTransmit(A, 0.0f, mu);
                const float3 sky = make_float3(0.10f, 0.13f, 0.20f) * saturate(mu * 1.5f + 0.15f);
                col = alb * (A.E * INV_PIf) * (Ts * fmaxf(mu, 0.0f) + sky);
            } else {
                col = alb * (b.lit * fmaxf(dot(n, b.toLight), 0.0f));
            }
        }
        col = col * b.weight;
    }
    // (the air between the camera and the surface is added once a pixel, in kFrame: it is smooth, and dear)
    // light in front of the surface: clouds, discs, shells, the haze. A ground giving way (into the atom) hides what is
    // behind it only as much as it is still there
    for (int i = 0; i < cV.nBlobs; ++i) {
        col += blobLight(cV.blobs[i], d, tBest);
        if (groundHit && g.fade < 1.0f) col += (blobLight(cV.blobs[i], d, 1e30f) - blobLight(cV.blobs[i], d, tBest)) * (1.0f - g.fade);
    }
    for (int i = 0; i < cV.nDiscs; ++i) col += discLight(cV.discs[i], d, tBest);
    // (the Milky Way's light is added once a pixel, in kFrame: it is built of octaves no finer than two pixels)
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
    float3 avg = sum * (1.0f / wsum);
    if (cV.galaxy.on) avg += galaxyLight(cV.galaxy, rayDir(x + 0.5f, y + 0.5f), dCentre, cV.pixAngle / cV.galaxy.kpc);
    if (cV.web.on) avg += webLight(cV.web, rayDir(x + 0.5f, y + 0.5f), dCentre, cV.pixAngle);
    if (cV.dens.on) avg = avg * (1.0f - cV.dens.weight) + densitySlice(cV.dens, x + 0.5f, y + 0.5f);
    if (cV.field.on) avg = avg * (1.0f - cV.field.weight) + fieldSlice(cV.field, x + 0.5f, y + 0.5f);
    if (cV.atmo.on) {   // the air between the camera and what the pixel's centre sees (or space behind the limb)
        float3 T;
        const float3 La = airLight(cV.atmo, rayDir(x + 0.5f, y + 0.5f), dCentre, T);
        avg = avg * T + La;
    }
    accum[y * cV.width + x] = make_float4(avg.x, avg.y, avg.z, 1.0f);
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
