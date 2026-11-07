// Path tracing uses one frame-local IAS with separate camera and transport masks.
#include <optix.h>
#include "scenes/ruya/ruya_shared.h"
#include "shared/display.h"

extern "C" { __constant__ ruya::Params params; }
using namespace ruya;

namespace {
struct Hit {
    float t;
    float yaw;
    float3 position, normal, geometric, local;
    unsigned material, seed;
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
__forceinline__ __device__ Surface material(const Hit& h, float footprint) {
    Surface s{};
    s.normal = h.normal;
    s.color = make_float3(0.66f, 0.57f, 0.43f);
    s.roughness = 0.48f;
    const float3 p = h.local + make_float3(randomUnit(h.seed) * 7.0f, 0, 0);
    const float coarse = noise(p * 0.27f);
    const float3 weathered = make_float3(p.x * 0.8f + p.z * 0.6f, p.y * 0.93f + p.z * 0.23f, p.z * 0.8f - p.x * 0.6f);
    const float4 grain = params.materialDetail ? noiseGradient(weathered * 2.1f + make_float3(coarse, coarse, coarse)) : make_float4(0.5f, 0, 0, 0);
    const float medium = grain.x;
    const float fineWeight = params.materialDetail ? 1.0f - smoothstepf(0.0006f, 0.006f, footprint) : 0;
    const float pores = fineWeight > 0 ? noise(p * 280.0f) : 0.5f;
    const float bandWeight = 1.0f - smoothstepf(0.02f, 0.28f, footprint);
    const float bands = noise(make_float3(p.x * 0.16f, p.y * 3.4f + coarse, p.z * 0.16f));
    if (h.material == kMaterialStone || h.material == kMaterialDarkStone) {
        const float density = 0.62f + coarse * 0.48f + (medium - 0.5f) * 0.24f + (bands - 0.5f) * 0.14f * bandWeight;
        s.color *= density;
        s.color *= 1.0f - fineWeight * smoothstepf(0.65f, 0.83f, pores) * 0.18f;
        const float moss = smoothstepf(0.48f, 0.72f, coarse) * smoothstepf(0.25f, 0.8f, h.normal.y);
        s.color = lerp3(s.color, make_float3(0.075f, 0.15f, 0.065f), moss * 0.72f);
        if (h.material == kMaterialDarkStone) s.color *= 0.25f;
        const float seepage = smoothstepf(0.43f, 0.76f, noise(make_float3(p.x * 0.56f, p.y * 0.12f, p.z * 0.56f)));
        s.color *= 1.0f - 0.24f * seepage;
        s.roughness = 0.49f + 0.23f * medium - 0.22f * seepage;
        const float reliefWeight = params.materialDetail ? 1.0f - smoothstepf(0.01f, 0.08f, footprint) : 0;
        if (reliefWeight > 0) {
            const float3 local = make_float3(grain.y * 0.8f - grain.w * 0.6f, grain.z * 0.93f,
                                             grain.y * 0.6f + grain.z * 0.23f + grain.w * 0.8f);
            float sine, cosine;
            sincosf(h.yaw, &sine, &cosine);
            const float3 gradient = make_float3(cosine * local.x + sine * local.z, local.y,
                                                 -sine * local.x + cosine * local.z) * (0.17f * reliefWeight);
            s.normal = normalize(h.normal + gradient - h.normal * dot(gradient, h.normal));
        }
    } else if (h.material == kMaterialBronze) {
        const float patina = smoothstepf(0.53f, 0.72f, coarse + (medium - 0.5f) * 0.28f);
        s.color = lerp3(make_float3(0.63f, 0.35f, 0.10f), make_float3(0.045f, 0.24f, 0.19f), patina);
        s.metal = 1.0f - patina * 0.7f;
        s.roughness = 0.22f + patina * 0.36f;
    } else if (h.material == kMaterialLeaf) {
        s.color = lerp3(make_float3(0.025f, 0.09f, 0.035f), make_float3(0.19f, 0.34f, 0.045f), coarse);
        s.roughness = 0.44f;
    } else if (h.material == kMaterialWater) {
        s.color = make_float3(0.015f, 0.10f, 0.095f);
        s.roughness = 0.085f;
        // An opaque reflecting pool: transmission and caustics are outside this material.
        s.normal = normalize(h.normal + make_float3(sinf(p.x * 2.8f + p.z * 1.1f), 0, cosf(p.z * 2.4f - p.x * 0.9f)) * (0.012f * bandWeight));
    }
    s.f0 = lerp3(make_float3(0.04f, 0.04f, 0.04f), s.color, s.metal);
    if (h.material == kMaterialWater) s.f0 = make_float3(0.0204f, 0.0204f, 0.0204f);
    s.specProbability = lerpf(0.35f, 0.92f, s.metal);
    if (h.material == kMaterialWater) s.specProbability = 0.8f;
    return s;
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
        // Independent per-pixel permutations keep all three intervals covered
        // without correlating the lobe, azimuth and elevation dimensions.
        const auto stratify = [stratum](float value, unsigned order) {
            const int direction = order & 1U ? 1 : 2;
            return (float((stratum * direction + int((order >> 1) % 3)) % 3) + value) * (1.0f / 3.0f);
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
__forceinline__ __device__ float3 atmospheric(float3 radiance, float3 origin, float3 direction, float distance) {
    // Analytic height fog with an approximate, unshadowed single-scatter source.
    const float density = 0.0010f * expf(clampf(-origin.y * 0.035f, -5.0f, 3.0f));
    const float v = direction.y * distance * 0.035f;
    const float opticalDepth = density * distance * (fabsf(v) > 0.001f ? (1.0f - expf(clampf(-v, -20.0f, 10.0f))) / v : 1.0f);
    const float transmittance = expf(-fmaxf(opticalDepth, 0.0f));
    const float forward = powf(fmaxf(dot(direction, sunDirection()), 0.0f), 9.0f);
    const float3 source = make_float3(0.12f, 0.24f, 0.28f) + make_float3(0.8f, 0.43f, 0.16f) * forward;
    return radiance * transmittance + source * (1.0f - transmittance);
}

__forceinline__ __device__ float3 integrate(float3 origin, float3 direction, unsigned& random,
                                          Hit& primary, float3& primaryAlbedo, int stratum, unsigned scramble) {
    float3 result = make_float3(0, 0, 0);
    float3 throughput = make_float3(1, 1, 1);
    const float3 cameraOrigin = origin;
    const float3 cameraDirection = direction;
    float previousPdf = 0;
    float footprint = 0;
    const float cone = 2.0f * length(params.V) / float(params.height);
    for (int bounce = 0; bounce <= params.bounces; ++bounce) {
        const Hit h = traceTriangles(origin, direction, 1200.0f, bounce == 0 ? kCameraRayMask : kTransportRayMask);
        if (bounce == 0) primary = h;
        if (h.t >= 1200.0f) {
            float3 light = environment(direction);
            if (isSun(direction)) light += sunRadiance() * (bounce == 0 ? 1.0f : powerHeuristic(previousPdf, 1.0f / kSunSolidAngle));
            result += throughput * light;
            break;
        }
        if (bounce == params.bounces) break;
        footprint += h.t * cone;
        const Surface s = material(h, footprint);
        if (bounce == 0) primaryAlbedo = s.color;
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
    return atmospheric(result, cameraOrigin, cameraDirection, fminf(primary.t, 1100.0f));
}
}

extern "C" __global__ void __closesthit__surface() {
    const unsigned long long address = static_cast<unsigned long long>(optixGetPayload_0()) |
                                       (static_cast<unsigned long long>(optixGetPayload_1()) << 32);
    Hit* hit = reinterpret_cast<Hit*>(address);
    const Cell& cell = params.cells[optixGetInstanceId()];
    const Geometry& geometry = params.geometries[cell.prototype];
    const uint3 triangle = geometry.indices[optixGetPrimitiveIndex()];
    const Vertex& a = geometry.vertices[triangle.x];
    const Vertex& b = geometry.vertices[triangle.y];
    const Vertex& c = geometry.vertices[triangle.z];
    const float2 uv = optixGetTriangleBarycentrics();
    const float3 localNormal = a.normal * (1.0f - uv.x - uv.y) + b.normal * uv.x + c.normal * uv.y;
    const float3 localGeometric = safeNormal(cross(b.position - a.position, c.position - a.position), make_float3(0, 1, 0));
    hit->t = optixGetRayTmax();
    hit->local = a.position * (1.0f - uv.x - uv.y) + b.position * uv.x + c.position * uv.y;
    const float3 worldDirection = optixGetWorldRayDirection();
    hit->position = optixGetWorldRayOrigin() + worldDirection * hit->t;
    hit->normal = safeNormal(optixTransformNormalFromObjectToWorldSpace(localNormal), make_float3(0, 1, 0));
    hit->geometric = safeNormal(optixTransformNormalFromObjectToWorldSpace(localGeometric), hit->normal);
    if (dot(hit->geometric, worldDirection) > 0) hit->geometric = -hit->geometric;
    if (dot(hit->normal, hit->geometric) < 0) hit->normal = -hit->normal;
    hit->material = a.material;
    hit->seed = cell.seed;
    hit->yaw = cell.yaw;
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
        const float x = 2.0f * (float(pixel.x) + ox) / float(params.width) - 1.0f;
        const float y = 1.0f - 2.0f * (float(pixel.y) + oy) / float(params.height);
        const float3 direction = normalize(params.W + params.U * x + params.V * y);
        Hit primary{};
        float3 sampleAlbedo = make_float3(0, 0, 0);
        float3 sampleColor = integrate(params.eye, direction, random, primary, sampleAlbedo,
                                       params.spp == 3 && params.sampleOffset == 0 ? sample : -1, hash(index));
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
    params.albedo[index] = make_float4(albedo.x, albedo.y, albedo.z, 1);
    params.normal[index] = make_float4(normal.x, normal.y, normal.z, 0);
    params.depth[index] = center.t < 1200.0f ? make_float4(center.position.x, center.position.y, center.position.z, center.t) : make_float4(0, 0, 0, -1);
}
extern "C" __global__ void __raygen__final() {
    const uint3 pixel = optixGetLaunchIndex();
    const unsigned index = pixel.y * params.width + pixel.x;
    const float3 color = toFloat3(params.raw ? params.color[index] : params.filtered[index]);
    const float3 display = agx(color * exp2f(params.exposure));
    const float dither = (randomUnit(index + 0x12345U) - 0.5f) / 255.0f;
    params.output[index] = make_uchar4(static_cast<unsigned char>(saturate(srgbOetf(display.x) + dither) * 255.0f + 0.5f),
                                     static_cast<unsigned char>(saturate(srgbOetf(display.y) + dither) * 255.0f + 0.5f),
                                     static_cast<unsigned char>(saturate(srgbOetf(display.z) + dither) * 255.0f + 0.5f), 255);
}
