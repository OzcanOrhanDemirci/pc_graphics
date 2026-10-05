// Scene 6 "Heykel": the deterministic renderer and the resolve (see heykel_kernels.h).
#include "scenes/heykel/heykel_kernels.h"
#include "shared/display.h"

namespace {

using namespace hk;

__device__ __forceinline__ unsigned int pcgHash(unsigned int v) {
    const unsigned int state = v * 747796405u + 2891336453u;
    const unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// The fixed pattern: the R4 sequence (Roberts' generalised golden ratio, d = 4), the same for every pixel. Dimensions
// 0-1 the sub-pixel position, 2-3 the point on the lamp's disc and on the lens. Sample 0 is the centre of all three.
__device__ __forceinline__ float r4(int s, int dim) {
    const float a[4] = {0.8566748838545029f, 0.7338918566271260f, 0.6287067210378086f, 0.5385972572236101f};
    return fractf(0.5f + a[dim] * float(s));
}

__device__ __forceinline__ float3 rayDir(const HeykelView& v, float px, float py) {
    const float aspect = float(v.width) / float(v.height);
    const float sx = (px / v.width * 2.0f - 1.0f) * v.tanHalf * aspect;
    const float sy = (1.0f - py / v.height * 2.0f) * v.tanHalf;
    return normalize(v.camFwd + v.camRight * sx + v.camUp * sy);
}

// the room: dark all round, a soft light from high above (no horizon to see)
__device__ __forceinline__ float3 env(const HeykelView& v, float3 d) {
    const float up = saturate(d.y);
    return v.fillColor * (v.fillLe * (0.015f + 2.2f * up * up));
}

__device__ __forceinline__ float spotOf(const HeykelLight& l, float3 fromLight) {
    return l.coneOuter < -1.0f ? 1.0f : smoothstepf(l.coneOuter, l.coneInner, dot(fromLight, l.axis));
}

// soft shadow estimate from the distances along the ray (Quilez): k ~ the light's distance over its radius. Used only
// for the room's faint light from above (wide, soft); the lamp's shadow is traced.
__device__ float softShadow(const HeykelView& v, float3 ro, float3 rd, float tmax, float eps0, float slope, float k) {
    float res = 1.0f, t = eps0 * 2.0f, ph = 1e20f;
    for (int i = 0; i < 160; ++i) {
        const float h = deScene(ro + rd * t, v.w);
        if (h < eps0 + slope * t) return 0.0f;
        const float y = h * h / (2.0f * ph);
        const float d = sqrtf(fmaxf(h * h - y * y, 0.0f));
        res = fminf(res, k * d / fmaxf(t - y, 1e-9f));
        ph = h;
        t += h * v.w.s.relax;
        if (t > tmax) break;
    }
    res = saturate(res);
    return res * res * (3.0f - 2.0f * res);
}

// occlusion from five taps along the normal, scaled to what the view shows (the same look at every zoom)
__device__ float occlusion(const HeykelView& v, float3 p, float3 n, float scale) {
    float occ = 0.0f, w = 1.0f;
    for (int i = 0; i < 5; ++i) {
        const float h = scale * (0.004f + 0.05f * float(i) / 4.0f);
        const float d = deScene(p + n * h, v.w);
        occ += (h - d) / scale * w;
        w *= 0.85f;
    }
    return saturate(1.0f - 12.0f * occ);
}

// the eye light's irradiance at p (see HeykelView)
__device__ __forceinline__ float3 eyeLight(const HeykelView& v, float3 p, float3 n) {
    if (v.eyeE <= 0.0f) return make_float3(0, 0, 0);
    float3 L = v.eyePos - p;
    const float r2 = dot(L, L);
    L = L * rsqrtf(r2);
    const float c2 = v.eyeC * v.eyeC;
    return v.eyeColor * (v.eyeE * (v.eyeD * v.eyeD + c2) / (r2 + c2) * fmaxf(dot(n, L), 0.0f));
}

// One sample's shading, no randomness: the lamp seen from a fixed point of its disc (this sample's), traced; the eye
// light; the room's light from above by the occlusion and how open the point is upwards; ambient by the occlusion.
__device__ float3 shade(const HeykelView& v, float3 p, float3 n, int id, float eps, float t, int s) {
    const float3 alb = v.albedo[id < 0 ? 0 : id];
    float3 direct = make_float3(0, 0, 0);
    const HeykelLight& l = v.key;
    if (l.le > 0.0f) {
        // the disc's point: uniform in area, in the plane across the lamp's axis (the same basis for every pixel)
        float3 t1, t2;
        makeBasis(l.axis, t1, t2);
        const float rr = s == 0 ? 0.0f : l.r * sqrtf(r4(s, 2)), ph = 6.2831853f * r4(s, 3);
        const float3 lp = l.pos + (t1 * cosf(ph) + t2 * sinf(ph)) * rr;
        float3 L = lp - p;
        const float dist = length(L);
        L = L * (1.0f / dist);
        const float cosT = dot(n, L);
        const float spot = spotOf(l, -L);
        if (cosT > 0.0f && spot > 0.0f) {
            float vis;
            if (v.softKey) {   // the penumbra estimated from the distances along one ray to the lamp's centre: it moves
                               // smoothly with the lamp (a traced edge jumps a pixel at a time while the lamp is dragged)
                vis = softShadow(v, p + n * (eps * 2.0f), L, fminf(dist - l.r, v.shadowReach), eps, v.pixAngle * v.detail * v.shadowSlope, dist / l.r);
            } else {
                Hit h;
                vis = march(p + n * (eps * 2.0f), L, dist, eps, v.pixAngle * v.detail * v.shadowSlope, v.maxSteps2, v.w.s.relax, v.w, h) ? 0.0f : 1.0f;
            }
            const float lc = length(l.pos - p);
            direct += l.color * (l.le * M_PIf * (l.r * l.r) / (lc * lc) * cosT * spot * vis);
        }
    }
    direct += eyeLight(v, p, n);
    const float ao = occlusion(v, p, n, t * v.tanHalf);
    const float3 upDir = normalize(make_float3(0.0f, 1.0f, 0.0f) + n * 0.35f);
    // the room's light from above: how open the point is upwards (a wide soft shadow towards the ceiling). It fades out
    // with the zoom (deep in the lattice it is shut out anyway), and so does this march: the longest of a sample's rays
    const float sky = v.fillLe > 0.0f ? softShadow(v, p + n * (eps * 2.0f), upDir, 2.0f, eps, v.pixAngle * v.detail * 4.0f, 1.2f) : 0.0f;
    const float fill = v.fillLe * M_PIf * (0.35f + 0.65f * saturate(n.y * 0.5f + 0.5f)) * ao * (0.15f + 0.85f * sky);
    const float amb = v.ambient * M_PIf * ao;
    return alb * INV_PIf * (direct + v.fillColor * fill + v.eyeColor * amb);
}

// Blackman-Harris over [0, 1]: 1 at the centre, 0 at the edge
__device__ __forceinline__ float blackmanHarris(float x) {
    const float u = 0.5f + 0.5f * saturate(x);   // the window's second half
    const float a = 2.0f * M_PIf * u;
    return 0.35875f - 0.48829f * cosf(a) + 0.14128f * cosf(2.0f * a) - 0.01168f * cosf(3.0f * a);
}

__global__ void kSamples(HeykelView v, float4* accum, int first, int count, int stripe, int stripes) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = stripe + (blockIdx.y * blockDim.y + threadIdx.y) * stripes;
    if (x >= v.width || y >= v.height) return;
    const int pix = y * v.width + x;
    const float slope = v.pixAngle * v.detail;
    float4 a = accum[pix];
    for (int s = first; s < first + count; ++s) {
        // the sub-pixel position: the centre first, then uniform over a disc of filterRadius pixels, weighted
        float ox = 0.0f, oy = 0.0f, w = 1.0f;
        if (s > 0) {
            const float r = v.filterRadius * sqrtf(r4(s, 0)), ph = 6.2831853f * r4(s, 1);
            ox = r * cosf(ph);
            oy = r * sinf(ph);
            w = blackmanHarris(r / v.filterRadius);
        }
        float3 ro = v.camPos, rd = rayDir(v, x + 0.5f + ox, y + 0.5f + oy);
        if (v.aperture > 0.0f && s > 0) {   // a thin lens focused on the plane through the zoom's point
            const float3 fp = ro + rd * (v.focusDist / dot(rd, v.camFwd));
            const float lr = v.aperture * sqrtf(r4(s, 3)), la = 6.2831853f * r4(s, 2) + 1.3f;
            ro = ro + v.camRight * (lr * cosf(la)) + v.camUp * (lr * sinf(la));
            rd = normalize(fp - ro);
        }
        Hit h;
        float3 c;
        if (march(ro, rd, v.tmax, 0.0f, slope, v.maxSteps, v.w.s.relax, v.w, h)) {
            const float3 p = ro + rd * h.t;
            const float eps = slope * h.t;
            float3 n = normalAt(p, eps * v.normalScale, v.w);
            if (dot(n, rd) > 0.0f) n = normalize(n - rd * (2.0f * dot(n, rd)));
            c = shade(v, p, n, h.id, eps, h.t, s);
        } else {
            c = env(v, rd);
        }
        a.x += c.x * w;
        a.y += c.y * w;
        a.z += c.z * w;
        a.w += w;
    }
    accum[pix] = a;
}

__global__ void kResolve(HeykelView v, const float4* accum, uchar4* out) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= v.width || y >= v.height) return;
    const int i = y * v.width + x;
    const float4 a = accum[i];
    float3 c = a.w > 0.0f ? make_float3(a.x, a.y, a.z) * (v.exposure / a.w) : make_float3(0, 0, 0);
    c = fmaxf3(c, make_float3(0, 0, 0));
    c = agx(c);
    // a fixed triangular dither (the same every frame: nothing moves on a still picture)
    const float n = float(pcgHash(unsigned(i) * 3u + 1u) >> 8) * (1.0f / 16777216.0f) + float(pcgHash(unsigned(i) * 3u + 2u) >> 8) * (1.0f / 16777216.0f) - 1.0f;
    const float r = fminf(fmaxf(srgbOetf(c.x) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    const float g = fminf(fmaxf(srgbOetf(c.y) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    const float b = fminf(fmaxf(srgbOetf(c.z) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    out[i] = make_uchar4((unsigned char)r, (unsigned char)g, (unsigned char)b, 255);
}

}   // namespace

void heykelSamples(const HeykelView& v, float4* accum, int first, int count, int stripe, int stripes, cudaStream_t s) {
    const dim3 b(16, 8);
    const int rows = (v.height - stripe + stripes - 1) / stripes;
    kSamples<<<dim3((v.width + b.x - 1) / b.x, (rows + b.y - 1) / b.y), b, 0, s>>>(v, accum, first, count, stripe, stripes);
}
void heykelResolve(const HeykelView& v, const float4* accum, uchar4* out, cudaStream_t s) {
    const dim3 b(16, 8);
    kResolve<<<dim3((v.width + b.x - 1) / b.x, (v.height + b.y - 1) / b.y), b, 0, s>>>(v, accum, out);
}
