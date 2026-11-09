// Path tracing uses one frame-local IAS with separate camera and transport masks.
#include <optix.h>
#include <cuda_runtime.h>
#include "scenes/ruya/ruya_shared.h"
#include "shared/display.h"

extern "C" { __constant__ ruya::Params params; }
using namespace ruya;

namespace {
struct Hit {
    float t;
    float3 position, normal, geometric;
    unsigned material, seed, region, meta;
    // Leaf cards only: atlas-independent coordinates and world edge vectors.
    float cardU, cardV;
    float3 cardTangent, cardBitangent;
};
struct Surface {
    float3 color, f0, normal;
    float roughness, metal, specProbability;
};
__forceinline__ __device__ float next(unsigned& state) {
    state = hash(state + 0x9e3779b9U);
    return float(state >> 8) * (1.0f / 16777216.0f);
}
__forceinline__ __device__ float3 toFloat3(float4 p) { return make_float3(p.x, p.y, p.z); }
__forceinline__ __device__ bool finite3(float3 p) { return isfinite(p.x) && isfinite(p.y) && isfinite(p.z); }
__forceinline__ __device__ float3 safeNormal(float3 p, float3 fallback) {
    return dot(p, p) > 1e-16f ? p * rsqrtf(dot(p, p)) : fallback;
}
__forceinline__ __device__ float3 sunDirection() { return normalize(make_float3(-0.53f, 0.36f, -0.76f)); }
__forceinline__ __device__ float3 sunRadiance() { return make_float3(42000.0f, 29400.0f, 16500.0f); }
inline constexpr float kSunRadius = 0.0062f;
inline constexpr float kSunCosine = 0.99998078006f;
inline constexpr float kSunSolidAngle = 2.0f * M_PIf * (1.0f - kSunCosine);

__forceinline__ __device__ float3 environment(float3 d) {
    const float up = smoothstepf(-0.15f, 0.9f, d.y);
    float3 c = lerp3(make_float3(0.22f, 0.34f, 0.40f), make_float3(0.045f, 0.11f, 0.23f), up);
    const float glow = powf(fmaxf(dot(d, sunDirection()), 0.0f), 18.0f);
    c += make_float3(1.0f, 0.55f, 0.21f) * (glow * 0.35f);
    if (d.y < -0.1f) c = lerp3(c, make_float3(0.075f, 0.16f, 0.19f), smoothstepf(-0.1f, -0.8f, d.y));
    return c;
}
__forceinline__ __device__ bool isSun(float3 d) { return dot(d, sunDirection()) >= kSunCosine; }
__forceinline__ __device__ float powerHeuristic(float a, float b) { return a * a / fmaxf(a * a + b * b, 1e-20f); }

__forceinline__ __device__ Hit traceTriangles(float3 origin, float3 direction, float tmax, unsigned mask) {
    Hit h{};
    h.t = tmax;
    if (!params.handle) return h;
    const unsigned long long address = reinterpret_cast<unsigned long long>(&h);
    unsigned lo = unsigned(address);
    unsigned hi = unsigned(address >> 32);
    unsigned mode = 0;
    optixTrace(params.handle, origin, direction, 0.003f, tmax, 0.0f, mask,
               OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0, lo, hi, mode);
    return h;
}
__forceinline__ __device__ bool shadowed(float3 origin, float3 direction, float limit) {
    if (!params.handle) return false;
    unsigned lo = 0;
    unsigned hi = 0;
    unsigned mode = 2;
    optixTrace(params.handle, origin, direction, 0.003f, limit, 0.0f, kTransportRayMask,
               OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT,
               0, 1, 0, lo, hi, mode);
    return mode == 2;
}

__forceinline__ __device__ float4 noiseGradient(float3 p) {
    const int x = int(floorf(p.x));
    const int y = int(floorf(p.y));
    const int z = int(floorf(p.z));
    const float3 t = make_float3(p.x - x, p.y - y, p.z - z);
    const float3 f = t * t * (make_float3(3, 3, 3) - t * 2.0f);
    const float3 df = t * (make_float3(1, 1, 1) - t) * 6.0f;
    float values[4];
    float slopes[4];
    for (int i = 0; i < 4; ++i) {
        const unsigned seed = unsigned(x) * 73856093U ^ unsigned(y + (i & 1)) * 19349663U ^ unsigned(z + (i >> 1)) * 83492791U;
        const float a = randomUnit(seed);
        const float b = randomUnit(seed ^ (unsigned(x) * 73856093U) ^ (unsigned(x + 1) * 73856093U));
        values[i] = lerpf(a, b, f.x);
        slopes[i] = b - a;
    }
    const float low = lerpf(values[0], values[1], f.y);
    const float high = lerpf(values[2], values[3], f.y);
    return make_float4(lerpf(low, high, f.z),
                       lerpf(lerpf(slopes[0], slopes[1], f.y), lerpf(slopes[2], slopes[3], f.y), f.z) * df.x,
                       lerpf(values[1] - values[0], values[3] - values[2], f.z) * df.y,
                       (high - low) * df.z);
}
__forceinline__ __device__ float noise(float3 p) { return noiseGradient(p).x; }

// Two horizontal decks give the optical background structure with no
// generated meshes or volume march. They are stationary in world space.
__forceinline__ __device__ float3 cloudDeck(float3 eye, float3 direction, float3 sky) {
    const float deck = direction.y < 0 ? -108.0f : 310.0f;
    const float t = (deck - eye.y) / direction.y;
    if (t <= 0.0f || t > 3600.0f || !isfinite(t)) return sky;
    const float3 p = (eye + direction * t) * 0.009f;
    const float wide = noise(p);
    const float density = wide * 0.67f + noise(p * 2.26f) * 0.23f + noise(p * 5.12f) * 0.10f;
    const float horizon = 1.0f - smoothstepf(650.0f, 3100.0f, t);
    const float mask = smoothstepf(direction.y < 0 ? 0.37f : 0.50f, 0.72f, density) * horizon;
    const float light = smoothstepf(0.32f, 0.77f, wide);
    const float3 cloud = lerp3(make_float3(0.13f, 0.24f, 0.33f), make_float3(0.82f, 0.76f, 0.65f), light);
    return lerp3(sky, cloud, mask * (direction.y < 0 ? 0.78f : 0.57f));
}

// CC0 scans. Analytic ray-footprint gradients keep the hardware filter anisotropic at
// grazing angles. Where projections or layers meet, a height mask lets one texel win
// instead of averaging two scans, so a blend zone keeps the contrast of a single scan.
struct Footprint { float3 major, minor; float width; };
struct Texel { float3 color, slope; float roughness, occlusion, height; };
__forceinline__ __device__ Footprint rayFootprint(float3 direction, float3 normal, float width) {
    const float along = dot(direction, normal);
    const float3 axis = safeNormal(direction - normal * along, safeNormal(cross(normal, make_float3(0.31f, 0.87f, 0.38f)), make_float3(1, 0, 0)));
    const float stretch = width / fmaxf(fabsf(along), 0.12f);
    Footprint f;
    f.major = axis * stretch;
    f.minor = cross(normal, axis) * width;
    f.width = stretch;
    return f;
}
__forceinline__ __device__ Footprint isotropicFootprint(float3 normal, float width) {
    return rayFootprint(normal, normal, width);
}
__forceinline__ __device__ Texel texelLerp(const Texel& a, const Texel& b, float k) {
    Texel t;
    t.color = lerp3(a.color, b.color, k);
    t.slope = lerp3(a.slope, b.slope, k);
    t.roughness = lerpf(a.roughness, b.roughness, k);
    t.occlusion = lerpf(a.occlusion, b.occlusion, k);
    t.height = lerpf(a.height, b.height, k);
    return t;
}
// mask 0 keeps a, mask 1 keeps b; in between the taller texel takes the point.
__forceinline__ __device__ Texel heightPick(const Texel& a, const Texel& b, float mask, float band) {
    const float scoreA = a.height + (1.0f - mask);
    const float scoreB = b.height + mask;
    const float floorScore = fmaxf(scoreA, scoreB) - band;
    const float weightA = fmaxf(scoreA - floorScore, 0.0f);
    const float weightB = fmaxf(scoreB - floorScore, 0.0f);
    return texelLerp(a, b, weightB / fmaxf(weightA + weightB, 1e-6f));
}
__forceinline__ __device__ Texel sampleAxis(int set, float u, float v, float3 tangent, float3 bitangent, float tile,
                                          float cosine, float sine, const Footprint& f) {
    const float scale = 1.0f / tile;
    const float majorU = dot(f.major, tangent) * scale, majorV = -dot(f.major, bitangent) * scale;
    const float minorU = dot(f.minor, tangent) * scale, minorV = -dot(f.minor, bitangent) * scale;
    const float ru = u * cosine - v * sine;
    const float rv = u * sine + v * cosine;
    const float2 dx = make_float2(majorU * cosine - majorV * sine, majorU * sine + majorV * cosine);
    const float2 dy = make_float2(minorU * cosine - minorV * sine, minorU * sine + minorV * cosine);
    const float4 a = tex2DGrad<float4>(params.textures[set * 2], ru, rv, dx, dy);
    const float4 n = tex2DGrad<float4>(params.textures[set * 2 + 1], ru, rv, dx, dy);
    const float nx = n.x * 2.0f - 1.0f;
    const float ny = n.y * 2.0f - 1.0f;
    Texel t;
    t.color = make_float3(a.x, a.y, a.z);
    t.roughness = a.w;
    t.occlusion = n.z;
    t.height = n.w;
    t.slope = (tangent * cosine + bitangent * sine) * nx + (bitangent * cosine - tangent * sine) * ny;
    return t;
}
__forceinline__ __device__ Texel scan(int set, float3 p, float3 normal, float tile, float angle, const Footprint& f, bool topOnly = false) {
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    const float3 uv = p * (1.0f / tile);
    float3 w = make_float3(fabsf(normal.x), fabsf(normal.y), fabsf(normal.z));
    w = w * w;
    w = w * w;
    if (topOnly) w = make_float3(0, 1, 0);
    w = w * (1.0f / (w.x + w.y + w.z));
    const float sx = normal.x < 0 ? -1.0f : 1.0f;
    const float sy = normal.y < 0 ? -1.0f : 1.0f;
    const float sz = normal.z < 0 ? -1.0f : 1.0f;
    constexpr float kCut = 0.12f;
    constexpr float kBand = 0.18f;
    Texel axes[3] = {};
    float score[3] = {-1.0f, -1.0f, -1.0f};
    if (w.x > kCut) { axes[0] = sampleAxis(set, uv.z * sx, -uv.y, make_float3(0, 0, sx), make_float3(0, 1, 0), tile, cosine, sine, f); score[0] = w.x + axes[0].height * kBand; }
    if (w.y > kCut) { axes[1] = sampleAxis(set, uv.x * sy, uv.z + 0.37f, make_float3(sy, 0, 0), make_float3(0, 0, -1), tile, cosine, sine, f); score[1] = w.y + axes[1].height * kBand; }
    if (w.z > kCut) { axes[2] = sampleAxis(set, -uv.x * sz, -uv.y + 0.22f, make_float3(-sz, 0, 0), make_float3(0, 1, 0), tile, cosine, sine, f); score[2] = w.z + axes[2].height * kBand; }
    const float top = fmaxf(fmaxf(score[0], score[1]), score[2]) - kBand;
    float total = 0.0f;
    Texel out{};
    for (int i = 0; i < 3; ++i) {
        const float weight = fmaxf(score[i] - top, 0.0f);
        if (weight <= 0.0f) continue;
        total += weight;
        out.color += axes[i].color * weight;
        out.slope += axes[i].slope * weight;
        out.roughness += axes[i].roughness * weight;
        out.occlusion += axes[i].occlusion * weight;
        out.height += axes[i].height * weight;
    }
    const float inverse = 1.0f / fmaxf(total, 1e-6f);
    out.color *= inverse;
    out.slope *= inverse;
    out.roughness *= inverse;
    out.occlusion *= inverse;
    out.height *= inverse;
    return out;
}
__forceinline__ __device__ Texel groundSurface(const Hit& h, float3 p, const Footprint& f) {
    Texel near = scan(kSetGrass, h.position, h.normal, 3.0f, 0.0f, f, true);
    const Texel aerial = scan(kSetGroundFar, h.position, h.normal, 15.0f, 0.7f, f, true);
    // The aerial scan owns the ground once a pixel is wider than a grass blade, and shows in patches nearer.
    const float range = smoothstepf(0.06f, 0.5f, f.width);
    near = heightPick(near, aerial, saturate(0.08f + 0.75f * range + (noise(p * 0.09f) - 0.5f) * 0.7f), 0.18f);
    return near;
}
__forceinline__ __device__ Texel rockSurface(const Hit& h, float3 p, const Footprint& f) {
    // Masks are mostly 0 or 1, so a pixel usually pays for a single scan. The rotated
    // copy breaks the repeat of the base layer; the strata scan carries the large forms.
    const float turn = smoothstepf(0.40f, 0.60f, noise(p * 0.045f));
    const float layer = smoothstepf(0.47f, 0.60f, noise(p * 0.021f));
    Texel rock;
    if (layer >= 0.98f) rock = scan(kSetCliff, h.position + make_float3(11.7f, 5.3f, 37.9f), h.normal, 13.0f, 0.0f, f);
    else {
        if (turn < 0.98f) rock = scan(kSetRock, h.position, h.normal, 3.2f, 0.0f, f);
        if (turn > 0.02f) {
            const Texel turned = scan(kSetRock, h.position + make_float3(41.3f, 17.9f, 23.1f), h.normal, 5.1f, 1.93f, f);
            rock = turn >= 0.98f ? turned : heightPick(rock, turned, turn, 0.16f);
        }
        if (layer > 0.02f) rock = heightPick(rock, scan(kSetCliff, h.position + make_float3(11.7f, 5.3f, 37.9f), h.normal, 13.0f, 0.0f, f), layer, 0.2f);
    }
    return rock;
}
struct CardLookup { float u, v; float2 dx, dy; int set; };
__forceinline__ __device__ CardLookup cardLookup(const Hit& h, const Footprint& f) {
    const unsigned atlas = (h.meta >> kCardAtlasShift) & 1U;
    const float4 r = cardRect(atlas, (h.meta >> kCardCellShift) & 3U);
    const float rangeU = r.z - r.x;
    const float rangeV = r.w - r.y;
    const float lengthU = fmaxf(length(h.cardTangent), 1e-5f);
    const float lengthV = fmaxf(length(h.cardBitangent), 1e-5f);
    const float3 axisU = h.cardTangent * (1.0f / lengthU);
    const float3 axisV = h.cardBitangent * (1.0f / lengthV);
    CardLookup c;
    c.set = atlas ? kSetNeedle : kSetBroadleaf;
    c.u = r.x + h.cardU * rangeU;
    c.v = r.y + h.cardV * rangeV;
    c.dx = make_float2(dot(f.major, axisU) * rangeU / lengthU, dot(f.major, axisV) * rangeV / lengthV);
    c.dy = make_float2(dot(f.minor, axisU) * rangeU / lengthU, dot(f.minor, axisV) * rangeV / lengthV);
    return c;
}
// The mip keeps the mean alpha, which is the leaf's share of the footprint. A pixel's samples
// test it against cut values spread evenly over (0, 1), so the share of samples that stop on the
// card equals that coverage at any distance, and a leaf edge crossing a sample changes the pixel
// by one sample's weight instead of switching it.
__forceinline__ __device__ bool cardOpaque(const Hit& h, const Footprint& f, float cut) {
    const CardLookup c = cardLookup(h, f);
    const float4 n = tex2DGrad<float4>(params.textures[c.set * 2 + 1], c.u, c.v, c.dx, c.dy);
    return n.w > cut;
}

__forceinline__ __device__ void applyTexel(Surface& s, const Hit& h, const Texel& t, float relief, float minOcclusion) {
    s.color = t.color * lerpf(minOcclusion, 1.0f, t.occlusion);
    s.roughness = clampf(t.roughness, 0.3f, 0.97f);
    const float3 tangentSlope = t.slope - h.normal * dot(t.slope, h.normal);
    s.normal = normalize(h.normal + tangentSlope * relief);
}

__forceinline__ __device__ Surface material(const Hit& h, const Footprint& f) {
    Surface s{};
    s.normal = h.normal;
    const float footprint = f.width;
    const float3 p = h.position + make_float3(randomUnit(h.seed) * 19.0f, 0, randomUnit(h.seed + 7) * 11.0f);
    // Regions differ by a trace of hue; the scan's own colour stays in front.
    const float3 tint = h.region == 0 ? make_float3(1.015f, 1.0f, 0.985f)
                      : h.region == 1 ? make_float3(0.99f, 1.0f, 1.01f)
                      : h.region == 2 ? make_float3(1.02f, 0.99f, 0.97f)
                                      : make_float3(0.995f, 1.005f, 0.985f);
    const float macro = 0.94f + 0.12f * (noise(p * 0.035f) * 0.6f + noise(p * 0.11f) * 0.4f);
    if (h.material == kMaterialStone || h.material == kMaterialDarkStone) {
        Texel t = rockSurface(h, p, f);
        t.color = t.color * tint * macro;
        // Soil holds on ledges and the crown, never on the sheer walls.
        const float grass = smoothstepf(0.70f, 0.88f, h.normal.y + (noise(p * 0.21f) - 0.5f) * 0.3f);
        if (grass > 0.001f) t = heightPick(t, groundSurface(h, p, f), grass, 0.2f);
        if (h.material == kMaterialDarkStone) t.color *= 0.35f;
        applyTexel(s, h, t, 1.35f, 0.35f);
    } else if (h.material == kMaterialMasonry) {
        Texel t = scan(kSetMasonry, h.position, h.normal, 2.6f, 0.0f, f);
        t.color = t.color * tint * macro;
        applyTexel(s, h, t, 1.0f, 0.5f);
    } else if (h.material == kMaterialBark) {
        Texel t = scan(kSetBark, h.position, h.normal, 1.5f, 0.0f, f);
        t.color = t.color * tint;
        applyTexel(s, h, t, 1.2f, 0.4f);
    } else if (h.material == kMaterialRoof) {
        Texel t = scan(kSetRoof, h.position, h.normal, 2.4f, 0.0f, f);
        t.color = t.color * macro;
        applyTexel(s, h, t, 1.0f, 0.5f);
    } else if (h.material == kMaterialWood) {
        Texel t = scan(kSetWood, h.position, h.normal, 2.0f, 0.0f, f);
        applyTexel(s, h, t, 1.0f, 0.5f);
    } else if (h.material == kMaterialBronze) {
        // The rust scan drives where bronze has weathered to verdigris and where it still shines.
        Texel t = scan(kSetMetal, h.position, h.normal, 0.9f, 0.0f, f);
        const float luminance = dot(t.color, make_float3(0.2126f, 0.7152f, 0.0722f));
        const float rust = saturate((t.color.x - t.color.z) * 2.5f);
        const float patina = smoothstepf(0.35f, 0.75f, rust + (noise(p * 1.7f) - 0.5f) * 0.4f);
        t.color = lerp3(make_float3(0.60f, 0.36f, 0.12f) * (0.55f + 0.9f * luminance),
                        make_float3(0.05f, 0.25f, 0.20f) * (0.6f + 0.8f * luminance), patina);
        applyTexel(s, h, t, 1.0f, 0.6f);
        s.metal = 1.0f - patina * 0.75f;
        s.roughness = lerpf(s.roughness, 0.3f + 0.4f * patina, 0.5f);
    } else if (h.material == kMaterialLeafCard) {
        const CardLookup c = cardLookup(h, f);
        const float4 a = tex2DGrad<float4>(params.textures[c.set * 2], c.u, c.v, c.dx, c.dy);
        const float4 n = tex2DGrad<float4>(params.textures[c.set * 2 + 1], c.u, c.v, c.dx, c.dy);
        // A per-card shift towards yellow or deep green gives the crown its depth.
        const float tone = float((h.meta >> kCardTintShift) & 255U) / 255.0f;
        const float3 cast = lerp3(make_float3(0.70f, 0.82f, 0.55f), make_float3(1.30f, 1.14f, 0.70f), tone);
        s.color = make_float3(a.x, a.y, a.z) * cast * tint;
        s.roughness = clampf(a.w, 0.35f, 0.9f);
        const float3 axisU = safeNormal(h.cardTangent, make_float3(1, 0, 0));
        const float3 axisV = safeNormal(h.cardBitangent, make_float3(0, 0, 1));
        const float3 slope = axisU * (n.x * 2.0f - 1.0f) - axisV * (n.y * 2.0f - 1.0f);
        s.normal = normalize(h.normal + (slope - h.normal * dot(slope, h.normal)) * 0.8f);
    } else if (h.material == kMaterialLeaf) {
        // Inner crown volume: only seen through gaps between the sprigs, so it stays dark.
        const float shade = noise(p * 1.8f);
        s.color = lerp3(make_float3(0.006f, 0.016f, 0.006f), make_float3(0.016f, 0.034f, 0.012f), shade);
        s.roughness = 0.8f;
    } else if (h.material == kMaterialWater) {
        s.color = make_float3(0.018f, 0.14f, 0.12f);
        s.roughness = 0.092f + 0.040f * smoothstepf(0.035f, 0.30f, footprint);
        const float weight = 1.0f - smoothstepf(0.026f, 0.24f, footprint);
        s.normal = normalize(h.normal + make_float3(sinf(p.x * 2.8f + p.z * 1.1f), 0,
                                                   cosf(p.z * 2.4f - p.x * 0.9f)) * (0.015f * weight));
    }
    s.f0 = lerp3(make_float3(0.04f, 0.04f, 0.04f), s.color, s.metal);
    if (h.material == kMaterialWater) s.f0 = make_float3(0.0204f, 0.0204f, 0.0204f);
    // Mostly diffuse stone rarely benefits from a GGX draw. Base the mixture
    // on the reflected energy and keep both lobes represented in the PDF.
    const float brightness = dot(s.color, make_float3(0.2126f, 0.7152f, 0.0722f));
    s.specProbability = lerpf(clampf(0.05f / (0.07f + brightness), 0.08f, 0.39f), 0.92f, s.metal);
    if (h.material == kMaterialWater) s.specProbability = 0.8f;
    return s;
}

// Camera rays pass through the cut-out parts of leaf cards. Light transport keeps
// them opaque, which is what gives a crown its dark interior.
__forceinline__ __device__ Hit traceVisible(float3 origin, float3 direction, float tmax, float cone, float cut) {
    float travelled = 0.0f;
    Hit h{};
    for (int step = 0; step < 3; ++step) {
        const float remaining = tmax - travelled;
        h = traceTriangles(origin, direction, remaining, kCameraRayMask);
        if (h.t >= remaining) { h.t = tmax; return h; }
        h.t += travelled;
        if (h.material != kMaterialLeafCard || step == 2) return h;
        if (cardOpaque(h, rayFootprint(direction, h.geometric, h.t * cone), cut)) return h;
        travelled = h.t + 0.004f;
        origin = h.position + direction * 0.004f;
    }
    return h;
}
inline constexpr float kLeafTransmission = 0.65f;
__forceinline__ __device__ float3 leafTransmission(const Surface& s, float cosine) {
    return s.color * make_float3(1.25f, 1.3f, 0.75f) * (kLeafTransmission * INV_PIf * cosine);
}

__forceinline__ __device__ float3 fresnel(float3 f0, float cosine) {
    const float f = powf(1.0f - saturate(cosine), 5.0f);
    return f0 + (make_float3(1, 1, 1) - f0) * f;
}
__forceinline__ __device__ float smithG1(float cosine, float a2) {
    return 2.0f * cosine / fmaxf(cosine + sqrtf(a2 + (1.0f - a2) * cosine * cosine), 1e-7f);
}
// Walter et al. GGX NDF, Smith masking, and a diffuse/specular mixture PDF.
__forceinline__ __device__ float3 evaluateBsdf(const Surface& s, float3 view, float3 light, float& pdf) {
    const float nv = dot(s.normal, view);
    const float nl = dot(s.normal, light);
    pdf = 0;
    if (nv <= 0 || nl <= 0) return make_float3(0, 0, 0);
    const float3 half = safeNormal(view + light, s.normal);
    const float nh = fmaxf(dot(s.normal, half), 0.0f);
    const float vh = fmaxf(dot(view, half), 1e-7f);
    const float alpha = fmaxf(s.roughness * s.roughness, 0.006f);
    const float a2 = alpha * alpha;
    const float denominator = nh * nh * (a2 - 1.0f) + 1.0f;
    const float D = a2 / fmaxf(M_PIf * denominator * denominator, 1e-16f);
    const float G = smithG1(nv, a2) * smithG1(nl, a2);
    const float3 F = fresnel(s.f0, vh);
    const float3 specular = F * (D * G / fmaxf(4.0f * nv * nl, 1e-8f));
    const float3 diffuse = s.color * (make_float3(1, 1, 1) - F) * ((1.0f - s.metal) * INV_PIf);
    pdf = (1.0f - s.specProbability) * nl * INV_PIf + s.specProbability * D * nh / (4.0f * vh);
    return diffuse + specular;
}
__forceinline__ __device__ float3 sampleBsdf(const Surface& s, float3 view, unsigned& state, int stratum, unsigned scramble) {
    float selector = next(state);
    float u = next(state);
    float v = next(state);
    if (stratum >= 0) {
        // Independent per-pixel permutations keep every interval covered
        // without correlating the lobe, azimuth and elevation dimensions.
        const auto stratify = [stratum](float value, unsigned order) {
            const int count = params.spp;
            const int direction = order & 1U ? 1 : count - 1;
            return (float((stratum * direction + int((order >> 1) % count)) % count) + value) / float(count);
        };
        selector = stratify(selector, scramble);
        u = stratify(u, scramble >> 8);
        v = stratify(v, scramble >> 16);
    }
    const Frame frame = Frame::from(s.normal);
    const float phi = 2.0f * M_PIf * u;
    if (selector < s.specProbability) {
        const float alpha = fmaxf(s.roughness * s.roughness, 0.006f);
        const float cosine = sqrtf((1.0f - v) / fmaxf(1.0f + (alpha * alpha - 1.0f) * v, 1e-8f));
        const float sine = sqrtf(fmaxf(1.0f - cosine * cosine, 0.0f));
        const float3 half = frame.toWorld(make_float3(cosf(phi) * sine, sinf(phi) * sine, cosine));
        return reflect3(-view, half);
    }
    const float r = sqrtf(v);
    return frame.toWorld(make_float3(cosf(phi) * r, sinf(phi) * r, sqrtf(1.0f - v)));
}
__forceinline__ __device__ float3 sampleSun(unsigned& state) {
    const float u = next(state);
    const float v = next(state);
    const float cosine = lerpf(1.0f, kSunCosine, u);
    const float sine = sqrtf(fmaxf(1.0f - cosine * cosine, 0.0f));
    return Frame::from(sunDirection()).toWorld(make_float3(cosf(2.0f * M_PIf * v) * sine, sinf(2.0f * M_PIf * v) * sine, cosine));
}
__forceinline__ __device__ float heightIntegral(float density, float falloff, float y, float dy, float distance) {
    const float base = density * expf(clampf(-falloff * y, -30.0f, 8.0f));
    const float v = falloff * dy * distance;
    return base * distance * (fabsf(v) > 1e-4f ? (1.0f - expf(clampf(-v, -30.0f, 30.0f))) / v : 1.0f);
}
__forceinline__ __device__ float fogTransmittance(float3 origin, float3 direction, float distance) {
    // The nearer fog is thinned so islands within a few hundred metres keep their contrast;
    // from kFarDistance outwards it is at full strength and still hides the far cut-off.
    const float thin = lerpf(0.62f, 1.0f, smoothstepf(120.0f, 560.0f, distance));
    const float opticalDepth = (heightIntegral(0.0040f, 0.028f, origin.y, direction.y, distance) +
                                heightIntegral(0.0019f, 0.005f, origin.y, direction.y, distance)) * thin;
    return expf(-fmaxf(opticalDepth, 0.0f));
}
__forceinline__ __device__ float3 atmospheric(float3 radiance, float3 origin, float3 direction, float distance) {
    const float transmittance = fogTransmittance(origin, direction, distance);
    const float forward = powf(fmaxf(dot(direction, sunDirection()), 0.0f), 8.0f);
    const float3 source = make_float3(0.26f, 0.34f, 0.40f) + make_float3(0.9f, 0.5f, 0.2f) * forward;
    return radiance * transmittance + source * (1.0f - transmittance);
}

__forceinline__ __device__ float3 mineralModulation(float3 albedo) {
    return make_float3(sqrtf(0.31f + 0.69f * saturate(albedo.x)),
                       sqrtf(0.31f + 0.69f * saturate(albedo.y)),
                       sqrtf(0.31f + 0.69f * saturate(albedo.z)));
}

__forceinline__ __device__ float3 integrate(float3 origin, float3 direction, unsigned& random,
                                          Hit& primary, float3& primaryAlbedo, int stratum, unsigned scramble,
                                          bool indirect, float indirectWeight) {
    float3 result = make_float3(0, 0, 0);
    float3 throughput = make_float3(1, 1, 1);
    const float3 cameraOrigin = origin;
    const float3 cameraDirection = direction;
    float previousPdf = 0;
    float footprint = 0;
    const float cone = 2.0f * length(params.V) / float(params.height);
    for (int bounce = 0; bounce <= params.bounces; ++bounce) {
        const Hit h = bounce == 0 ? traceVisible(origin, direction, kFarDistance, cone, next(random))
                                  : traceTriangles(origin, direction, 1200.0f, kTransportRayMask);
        if (bounce == 0) primary = h;
        if (h.t >= (bounce == 0 ? kFarDistance : 1200.0f)) {
            if (bounce == 0) primary.t = 1200.0f;
            float3 light = environment(direction);
            if (bounce == 0) light = cloudDeck(cameraOrigin, cameraDirection, light);
            if (isSun(direction)) light += sunRadiance() * (bounce == 0 ? 1.0f : powerHeuristic(previousPdf, 1.0f / kSunSolidAngle));
            result += throughput * light;
            break;
        }
        if (bounce == params.bounces) break;
        footprint += h.t * cone;
        const Surface s = material(h, rayFootprint(direction, h.geometric, footprint));
        if (bounce == 0) {
            primaryAlbedo = s.color;
            primary.normal = s.normal;
        }
        const float3 view = -direction;
        const float3 light = sampleSun(random);
        const float nl = dot(s.normal, light);
        if (nl > 0 && dot(h.geometric, light) > 0) {
            float bsdfPdf = 0;
            const float3 bsdf = evaluateBsdf(s, view, light, bsdfPdf);
            if (!shadowed(h.position + h.geometric * 0.006f, light, 1000.0f)) {
                const float sunPdf = 1.0f / kSunSolidAngle;
                result += throughput * bsdf * sunRadiance() * (nl * powerHeuristic(sunPdf, bsdfPdf) / sunPdf);
            }
        } else if (h.material == kMaterialLeafCard && !shadowed(h.position - h.geometric * 0.006f, light, 1000.0f)) {
            result += throughput * leafTransmission(s, fabsf(nl)) * sunRadiance() * kSunSolidAngle;
        }
        if (bounce == 0) {
            if (!indirect) break;
            throughput *= indirectWeight;
        }
        direction = sampleBsdf(s, view, random, bounce == 0 ? stratum : -1, scramble);
        if (dot(direction, h.geometric) <= 0) break;
        float pdf = 0;
        const float3 f = evaluateBsdf(s, view, direction, pdf);
        if (pdf <= 1e-10f) break;
        throughput *= f * (fmaxf(dot(s.normal, direction), 0.0f) / pdf);
        if (!finite3(throughput)) { atomicAdd(params.badCount, 1U); break; }
        previousPdf = pdf;
        origin = h.position + h.geometric * 0.006f;
    }
    return atmospheric(result, cameraOrigin, cameraDirection, primary.t < 1200.0f ? primary.t : 4000.0f);
}
inline constexpr int kAmbientRays = 2;
inline constexpr float kAmbientReach = 10.0f;
__forceinline__ __device__ float3 ambientDirection(int index, int count) {
    // Cosine-weighted Fibonacci spiral, identical for every pixel and frame.
    const float u = (float(index) + 0.5f) / float(count);
    const float phi = float(index) * 2.39996323f;
    const float r = sqrtf(u);
    return make_float3(cosf(phi) * r, sinf(phi) * r, sqrtf(fmaxf(1.0f - u, 0.0f)));
}
__forceinline__ __device__ float3 shadeDirect(const Hit& h, const Surface& s, float3 view) {
    const float3 light = sunDirection();
    const float nl = dot(s.normal, light);
    if (nl <= 0 || dot(h.geometric, light) <= 0) {
        if (h.material != kMaterialLeafCard || shadowed(h.position - h.geometric * 0.006f, light, 1000.0f)) return make_float3(0, 0, 0);
        return leafTransmission(s, fabsf(nl)) * sunRadiance() * kSunSolidAngle;
    }
    if (shadowed(h.position + h.geometric * 0.006f, light, 1000.0f)) return make_float3(0, 0, 0);
    float pdf = 0;
    return evaluateBsdf(s, view, light, pdf) * sunRadiance() * (kSunSolidAngle * nl);
}
__forceinline__ __device__ float3 shadeAmbient(const Hit& h, const Surface& s, int sample, int rays) {
    if (h.material == kMaterialLeafCard || h.material == kMaterialLeaf) {
        // Rays through a crown are the costliest in the scene and mostly hit more leaves,
        // so foliage takes its sky exposure from where the card sits in the crown.
        const float open = h.material == kMaterialLeaf ? 0.22f : 0.3f + 0.7f * float((h.meta >> kCardOpenShift) & 15U) / 15.0f;
        return s.color * environment(s.normal) * open;
    }
    const Frame frame = Frame::from(s.normal);
    const int count = rays * params.spp;
    float3 sum = make_float3(0, 0, 0);
    for (int k = 0; k < rays; ++k) {
        const float3 d = frame.toWorld(ambientDirection(sample + k * params.spp, count));
        if (dot(d, h.geometric) <= 0.0f) { sum += make_float3(0.10f, 0.09f, 0.075f); continue; }
        sum += shadowed(h.position + h.geometric * 0.006f, d, kAmbientReach) ? make_float3(0.10f, 0.09f, 0.075f)
                                                                            : environment(d);
    }
    return s.color * (1.0f - s.metal) * sum * (1.0f / float(rays));
}
// A pixel's samples nearly always land on one surface, and its scans are filtered to a
// footprint, so the surface is evaluated once per quarter of the pixel, filtered to that
// quarter, and reused by the quarter's samples. Leaf cards are shaded per sample.
inline constexpr unsigned kNoSurface = 0xFFFFFFFFU;
// Where the fog has left less than this share of the contrast, one evaluation serves the
// pixel and one ambient ray per sample is enough.
inline constexpr float kQuarterFog = 0.55f;
__forceinline__ __device__ unsigned surfaceKey(const Hit& h) {
    return h.material == kMaterialLeafCard ? kNoSurface : hash(h.seed ^ (h.material << 24) ^ (h.region << 28));
}
// quarter is the sample's quarter of the pixel, or -1 when the pixel is not split.
__forceinline__ __device__ float3 shadeLive(float3 direction, int sample, float cut, int quarter,
                                           Surface& cached, unsigned& cachedKey, Hit& primary, float3& albedo) {
    // A surface shaded for one sample is filtered to that sample's share of the pixel, as brute-force
    // supersampling does; the reference shades every sample so.
    const float pixelCone = 2.0f * length(params.V) / float(params.height);
    const float sampleCone = pixelCone * rsqrtf(float(params.spp));
    const Hit h = traceVisible(params.eye, direction, kFarDistance, sampleCone, cut);
    primary = h;
    if (h.t >= kFarDistance) {
        float3 sky = cloudDeck(params.eye, direction, environment(direction));
        if (isSun(direction)) sky += sunRadiance();
        primary.t = 1200.0f;
        return atmospheric(sky, params.eye, direction, 4000.0f);
    }
    const unsigned surfaceId = surfaceKey(h);
    const bool clear = params.reference || fogTransmittance(params.eye, direction, h.t) > kQuarterFog;
    const bool split = quarter >= 0 && clear;
    const unsigned key = surfaceId == kNoSurface ? kNoSurface : split ? hash(surfaceId + unsigned(quarter)) : surfaceId;
    if (params.reference || key == kNoSurface || key != cachedKey) {
        const float cone = params.reference || key == kNoSurface ? sampleCone : split ? pixelCone * 0.5f : pixelCone;
        cached = material(h, rayFootprint(direction, h.geometric, h.t * cone));
        cachedKey = key;
    }
    const Surface s = cached;
    albedo = s.color;
    primary.normal = s.normal;
    const float3 view = -direction;
    float3 color = shadeDirect(h, s, view) + shadeAmbient(h, s, sample, clear ? kAmbientRays : 1);
    if (h.material == kMaterialWater) {
        const float3 mirror = reflect3(direction, s.normal);
        const Hit r = traceTriangles(h.position + h.geometric * 0.006f, mirror, kFarDistance, kTransportRayMask);
        float3 reflected;
        if (r.t >= kFarDistance) reflected = atmospheric(environment(mirror), h.position, mirror, 4000.0f);
        else {
            const Surface rs = material(r, isotropicFootprint(r.normal, 0.05f));
            reflected = atmospheric(shadeDirect(r, rs, -mirror) + rs.color * 0.2f, h.position, mirror, r.t);
        }
        color += fresnel(s.f0, fmaxf(dot(s.normal, view), 0.0f)) * reflected;
    }
    return atmospheric(color, params.eye, direction, h.t);
}
}

extern "C" __global__ void __closesthit__surface() {
    const unsigned long long address = static_cast<unsigned long long>(optixGetPayload_0()) |
                                       (static_cast<unsigned long long>(optixGetPayload_1()) << 32);
    Hit* hit = reinterpret_cast<Hit*>(address);
    const InstanceData& cell = params.instances[optixGetInstanceId()];
    const Geometry& geometry = params.geometries[cell.prototype];
    const uint3 triangle = geometry.indices[optixGetPrimitiveIndex()];
    const Vertex& a = geometry.vertices[triangle.x];
    const Vertex& b = geometry.vertices[triangle.y];
    const Vertex& c = geometry.vertices[triangle.z];
    const float2 uv = optixGetTriangleBarycentrics();
    const float3 localNormal = a.normal * (1.0f - uv.x - uv.y) + b.normal * uv.x + c.normal * uv.y;
    const float3 localGeometric = safeNormal(cross(b.position - a.position, c.position - a.position), make_float3(0, 1, 0));
    hit->t = optixGetRayTmax();
    const float3 worldDirection = optixGetWorldRayDirection();
    hit->position = optixGetWorldRayOrigin() + worldDirection * hit->t;
    hit->normal = safeNormal(optixTransformNormalFromObjectToWorldSpace(localNormal), make_float3(0, 1, 0));
    hit->geometric = safeNormal(optixTransformNormalFromObjectToWorldSpace(localGeometric), hit->normal);
    if (dot(hit->geometric, worldDirection) > 0) hit->geometric = -hit->geometric;
    if (dot(hit->normal, hit->geometric) < 0) hit->normal = -hit->normal;
    hit->material = materialId(a.material);
    hit->meta = a.material;
    if (hit->material == kMaterialLeafCard) {
        const auto corner = [](unsigned material, unsigned bit) { return (material & bit) ? 1.0f : 0.0f; };
        const float ua = corner(a.material, kCardCornerU), ub = corner(b.material, kCardCornerU), uc = corner(c.material, kCardCornerU);
        const float va = corner(a.material, kCardCornerV), vb = corner(b.material, kCardCornerV), vc = corner(c.material, kCardCornerV);
        const float w = 1.0f - uv.x - uv.y;
        hit->cardU = ua * w + ub * uv.x + uc * uv.y;
        hit->cardV = va * w + vb * uv.x + vc * uv.y;
        const float3 edge1 = b.position - a.position;
        const float3 edge2 = c.position - a.position;
        const float du1 = ub - ua, dv1 = vb - va, du2 = uc - ua, dv2 = vc - va;
        const float inverse = 1.0f / (du1 * dv2 - dv1 * du2);
        hit->cardTangent = optixTransformVectorFromObjectToWorldSpace((edge1 * dv2 - edge2 * dv1) * inverse);
        hit->cardBitangent = optixTransformVectorFromObjectToWorldSpace((edge2 * du1 - edge1 * du2) * inverse);
    }
    hit->seed = cell.seed;
    hit->region = cell.region;
}
extern "C" __global__ void __miss__surface() { optixSetPayload_2(0); }
extern "C" __global__ void __raygen__render() {
    const uint3 pixel = optixGetLaunchIndex();
    const unsigned index = pixel.y * params.width + pixel.x;
    if (params.layout != sizeof(Params)) {
        params.color[index] = make_float4(100, 0, 100, 1);
        atomicAdd(params.badCount, 1U);
        return;
    }
    float3 color = make_float3(0, 0, 0);
    float3 albedo = make_float3(0, 0, 0);
    float3 normal = make_float3(0, 0, 0);
    Hit center{};
    center.t = 1200.0f;
    if (params.bounces == 0) {
        Surface surface{};
        unsigned surfaceId = kNoSurface;
        // Samples are visited a quarter at a time so that each quarter's surface is evaluated once.
        const int quarters = params.spp >= 8 ? 4 : 1;
        for (int quarter = 0; quarter < quarters; ++quarter)
        for (int sample = 0; sample < params.spp; ++sample) {
            // Rotated grid for four samples, the pixel centre for one.
            const float ox = params.spp == 4 ? (float(sample) + 0.5f) * 0.25f : fmodf(0.5f + float(sample) * 0.754877666f, 1.0f);
            const float oy = params.spp == 4 ? (float((sample * 3 + 1) & 3) + 0.5f) * 0.25f : fmodf(0.5f + float(sample) * 0.569840296f, 1.0f);
            if (quarters == 4 && int(ox * 2.0f) + 2 * int(oy * 2.0f) != quarter) continue;
            const float x = 2.0f * (float(pixel.x) + ox) / float(params.width) - 1.0f;
            const float y = 1.0f - 2.0f * (float(pixel.y) + oy) / float(params.height);
            Hit primary{};
            float3 sampleAlbedo = make_float3(0, 0, 0);
            // Stratified alpha cuts, rotated by a fixed per-pixel value that never changes between frames.
            const float cut = fmodf((float(sample) + 0.5f) / float(params.spp) + randomUnit(index ^ 0x5C3A9E1DU), 1.0f);
            float3 sampleColor = shadeLive(normalize(params.W + params.U * x + params.V * y), sample, cut,
                                           quarters == 4 ? quarter : -1, surface, surfaceId, primary, sampleAlbedo);
            if (!finite3(sampleColor)) { atomicAdd(params.badCount, 1U); sampleColor = make_float3(0, 0, 0); }
            color += sampleColor;
            albedo += sampleAlbedo;
            normal += primary.t < 1200.0f ? primary.normal : make_float3(0, 0, 0);
            if (sample == 0) center = primary;
        }
        color *= 1.0f / float(params.spp);
        params.color[index] = make_float4(color.x, color.y, color.z, 1);
        params.albedo[index] = make_float4(albedo.x, albedo.y, albedo.z, 1);
        params.depth[index] = center.t < 1200.0f ? make_float4(center.position.x, center.position.y, center.position.z, center.t) : make_float4(0, 0, 0, -1);
        return;
    }
    const unsigned scramble = hash(index);
    const bool reducedIndirect = params.spp == 4 && params.sampleOffset == 0;
    for (int sample = 0; sample < params.spp; ++sample) {
        const unsigned sequence = unsigned(sample) + unsigned(params.sampleOffset);
        unsigned random = hash(index ^ hash(sequence + 0xabcdefU));
        // The R2 positions do not change with frame index or camera stillness.
        float ox = params.spp == 1 && params.sampleOffset == 0 ? 0.5f : fmodf(0.5f + float(sequence + 1) * 0.754877666f, 1.0f);
        float oy = params.spp == 1 && params.sampleOffset == 0 ? 0.5f : fmodf(0.5f + float(sequence + 1) * 0.569840296f, 1.0f);
        if (params.spp == 3 && params.sampleOffset == 0) {
            // Centroid at (0.5, 0.5), unlike the first three R2 points.
            ox = sample == 0 ? 0.155f : sample == 1 ? 0.62f : 0.725f;
            oy = sample == 0 ? 0.425f : sample == 1 ? 0.155f : 0.92f;
        }
        if (params.spp == 4 && params.sampleOffset == 0) {
            // Rotated grid covers four distinct rows and columns without frame jitter.
            ox = (float(sample) + 0.5f) * 0.25f;
            oy = (float((sample * 3 + 1) & 3) + 0.5f) * 0.25f;
        }
        const float x = 2.0f * (float(pixel.x) + ox) / float(params.width) - 1.0f;
        const float y = 1.0f - 2.0f * (float(pixel.y) + oy) / float(params.height);
        const float3 direction = normalize(params.W + params.U * x + params.V * y);
        Hit primary{};
        float3 sampleAlbedo = make_float3(0, 0, 0);
        // Four direct/visibility samples still resolve thin silhouettes. Each pixel
        // omits one uniformly chosen indirect continuation and weights the other three.
        const bool indirect = !reducedIndirect || sample != int(hash(index ^ 0x4ae1c3U) & 3U);
        float3 sampleColor = integrate(params.eye, direction, random, primary, sampleAlbedo,
                                       (params.spp == 3 || params.spp == 4) && params.sampleOffset == 0 ? sample : -1,
                                       scramble, indirect, reducedIndirect ? 4.0f / 3.0f : 1.0f);
        if (!finite3(sampleColor)) { atomicAdd(params.badCount, 1U); sampleColor = make_float3(0, 0, 0); }
        color += sampleColor;
        albedo += sampleAlbedo;
        normal += primary.t < 1200.0f ? primary.normal : make_float3(0, 0, 0);
        if (sample == 0) center = primary;
    }
    const float reciprocal = 1.0f / float(params.spp);
    color *= reciprocal;
    albedo *= reciprocal;
    normal = safeNormal(normal, make_float3(0, 0, 0));
    params.color[index] = make_float4(color.x, color.y, color.z, 1);
    if (!params.raw) {
        const float3 factor = mineralModulation(albedo);
        params.illumination[index] = make_float4(color.x / factor.x, color.y / factor.y, color.z / factor.z, 1);
    }
    params.albedo[index] = make_float4(albedo.x, albedo.y, albedo.z, 1);
    params.normal[index] = make_float4(normal.x, normal.y, normal.z, 0);
    params.depth[index] = center.t < 1200.0f ? make_float4(center.position.x, center.position.y, center.position.z, center.t) : make_float4(0, 0, 0, -1);
}
extern "C" __global__ void __raygen__final() {
    const uint3 pixel = optixGetLaunchIndex();
    const unsigned index = pixel.y * params.width + pixel.x;
    float3 color = toFloat3(params.raw ? params.color[index] : params.filtered[index]);
    if (!params.raw) color *= mineralModulation(toFloat3(params.albedo[index]));
    const float3 display = agx(color * exp2f(params.exposure));
    const float dither = (randomUnit(index + 0x12345U) - 0.5f) / 255.0f;
    params.output[index] = make_uchar4(static_cast<unsigned char>(saturate(srgbOetf(display.x) + dither) * 255.0f + 0.5f),
                                     static_cast<unsigned char>(saturate(srgbOetf(display.y) + dither) * 255.0f + 0.5f),
                                     static_cast<unsigned char>(saturate(srgbOetf(display.z) + dither) * 255.0f + 0.5f), 255);
}
