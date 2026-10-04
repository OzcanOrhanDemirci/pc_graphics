// Scene 6 "Heykel": preview, path tracer and resolve (see heykel_kernels.h).
#include "scenes/heykel/heykel_kernels.h"
#include "shared/display.h"

namespace {

using namespace hk;

__device__ __forceinline__ unsigned int pcgHash(unsigned int v) {
    const unsigned int state = v * 747796405u + 2891336453u;
    const unsigned int word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
struct Rng {
    unsigned int s;
    __device__ float next() {
        s = pcgHash(s);
        return float(s >> 8) * (1.0f / 16777216.0f);
    }
};

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

__device__ __forceinline__ float spotOf(const HeykelView& v, float3 fromLight) {
    return smoothstepf(v.coneOuter, v.coneInner, dot(fromLight, v.lightAxis));
}

// soft shadow estimate from the distances along the ray (Quilez): k ~ the light's distance over its radius
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

__device__ float3 shadePreview(const HeykelView& v, float3 p, float3 n, int id, float eps, float t) {
    const float3 alb = v.albedo[id < 0 ? 0 : id];
    float3 L = v.lightPos - p;
    const float dist = length(L);
    L = L * (1.0f / dist);
    const float cosT = dot(n, L);
    float direct = 0.0f;
    if (cosT > 0.0f) {
        const float spot = spotOf(v, -L);
        if (spot > 0.0f) {
            const float E = v.lightLe * M_PIf * (v.lightR * v.lightR) / (dist * dist) * cosT * spot;
            const float sh = softShadow(v, p + n * (eps * 2.0f), L, dist - v.lightR, eps, v.pixAngle * v.detail * 2.0f, dist / v.lightR);
            direct = E * sh;
        }
    }
    const float ao = occlusion(v, p, n, t * v.tanHalf);
    const float fill = v.fillLe * M_PIf * (0.35f + 0.65f * saturate(n.y * 0.5f + 0.5f)) * ao;
    return alb * INV_PIf * (v.lightColor * direct + v.fillColor * fill);
}

__global__ void kPreview(HeykelView v, float4* out) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= v.width || y >= v.height) return;
    const float3 rd = rayDir(v, x + 0.5f, y + 0.5f);
    const float slope = v.pixAngle * v.detail;
    Hit h;
    float3 c;
    float tHit = 0.0f;
    if (march(v.camPos, rd, v.tmax, 0.0f, slope, v.maxSteps, v.w.s.relax, v.w, h)) {
        const float3 p = v.camPos + rd * h.t;
        const float eps = slope * h.t;
        const float3 n = normalAt(p, eps, v.w);
        c = shadePreview(v, p, n, h.id, eps, h.t);
        tHit = h.t;
    } else {
        c = env(v, rd);
    }
    out[y * v.width + x] = make_float4(c.x, c.y, c.z, tHit);
}

// the key light by sampling the cone its sphere fills (uniform in solid angle), the spot by the leaving direction
__device__ float3 lightSample(const HeykelView& v, float3 p, float3 n, float eps, float slope, Rng& rng) {
    const float3 L = v.lightPos - p;
    const float d2 = dot(L, L), d = sqrtf(d2);
    const float sin2 = fminf(v.lightR * v.lightR / d2, 1.0f);
    const float cosMax = sqrtf(1.0f - sin2);
    const float oneMinus = sin2 / (1.0f + cosMax);   // 1 - cosMax without cancelling
    const float u1 = rng.next(), u2 = rng.next();
    const float cosT = 1.0f - u1 * oneMinus;
    const float sinT = sqrtf(fmaxf(0.0f, 1.0f - cosT * cosT));
    const float phi = 2.0f * M_PIf * u2;
    const Frame f = Frame::from(L * (1.0f / d));
    const float3 dir = f.toWorld(make_float3(cosf(phi) * sinT, sinf(phi) * sinT, cosT));
    const float cosN = dot(n, dir);
    if (cosN <= 0.0f) return make_float3(0, 0, 0);
    const float b = dot(L, dir);
    const float tS = b - sqrtf(fmaxf(b * b - (d2 - v.lightR * v.lightR), 0.0f));
    const float spot = spotOf(v, -dir);
    if (spot <= 0.0f) return make_float3(0, 0, 0);
    Hit h;
    if (march(p + n * (eps * 2.0f), dir, tS, eps, slope, v.maxSteps, v.w.s.relax, v.w, h)) return make_float3(0, 0, 0);
    return v.lightColor * (v.lightLe * spot * cosN * 2.0f * M_PIf * oneMinus);
}

__device__ float3 trace(const HeykelView& v, float3 ro, float3 rd, Rng& rng) {
    float3 thr = make_float3(1, 1, 1), sum = make_float3(0, 0, 0);
    float eps0 = 0.0f;
    float slope = v.pixAngle * v.detail;
    for (int b = 0; b <= v.bounces; ++b) {
        Hit h;
        if (!march(ro, rd, v.tmax, eps0, slope, v.maxSteps, v.w.s.relax, v.w, h)) {
            sum += thr * env(v, rd);
            break;
        }
        const float3 p = ro + rd * h.t;
        const float eps = eps0 + slope * h.t;
        float3 n = normalAt(p, eps, v.w);
        if (dot(n, rd) > 0.0f) n = normalize(n - rd * (2.0f * dot(n, rd)));
        const float3 alb = v.albedo[h.id < 0 ? 0 : h.id];
        sum += thr * alb * INV_PIf * lightSample(v, p, n, eps, slope * 2.0f, rng);
        if (b == v.bounces) break;
        // cosine-weighted bounce
        const float u1 = rng.next(), u2 = rng.next();
        const float r = sqrtf(u1), phi = 2.0f * M_PIf * u2;
        rd = Frame::from(n).toWorld(make_float3(r * cosf(phi), r * sinf(phi), sqrtf(fmaxf(0.0f, 1.0f - u1))));
        ro = p + n * (eps * 2.0f);
        thr *= alb;
        eps0 = eps;
        slope = v.pixAngle * v.detail * 2.0f;
        if (b >= 1) {   // Russian roulette
            const float q = fminf(0.95f, maxc(thr) * 1.5f);
            if (rng.next() > q) break;
            thr = thr * (1.0f / q);
        }
    }
    return sum;
}

__global__ void kPath(HeykelView v, float4* accum, int samples, int stripe, int stripes) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = stripe + (blockIdx.y * blockDim.y + threadIdx.y) * stripes;
    if (x >= v.width || y >= v.height) return;
    const unsigned int pix = unsigned(y * v.width + x);
    // the sub-pixel positions: the R2 sequence, shifted per pixel (no pattern across the screen)
    const float g1 = 0.7548776662466927f, g2 = 0.5698402909980532f;
    const float ox = float(pcgHash(pix * 2u + 1u) >> 8) * (1.0f / 16777216.0f);
    const float oy = float(pcgHash(pix * 2u + 7919u) >> 8) * (1.0f / 16777216.0f);
    float3 c = make_float3(0, 0, 0);
    for (int s = 0; s < samples; ++s) {
        const int k = v.sample + s;
        Rng rng{pcgHash(pix ^ pcgHash(unsigned(k) * 9781u + 17u))};
        const float jx = fractf(ox + g1 * k), jy = fractf(oy + g2 * k);
        c += trace(v, v.camPos, rayDir(v, x + jx, y + jy), rng);
    }
    float4 a = accum[pix];
    a.x += c.x;
    a.y += c.y;
    a.z += c.z;
    a.w += float(samples);
    accum[pix] = a;
}

__device__ __forceinline__ float3 blended(const HeykelView& v, const float4* preview, const float4* accum, int i) {
    const float4 p = preview[i];
    float3 c = make_float3(p.x, p.y, p.z);
    if (accum) {
        const float4 a = accum[i];
        if (a.w > 0.0f) {
            const float3 m = make_float3(a.x, a.y, a.z) * (1.0f / a.w);
            c = lerp3(c, m, saturate(a.w / 6.0f));
        }
    }
    return c;
}

__global__ void kResolve(HeykelView v, const float4* preview, const float4* accum, uchar4* out) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= v.width || y >= v.height) return;
    const int i = y * v.width + x;
    float3 c = blended(v, preview, accum, i) * v.exposure;
    c = fmaxf3(c, make_float3(0, 0, 0));
    c = agx(c);
    // a fixed triangular dither (the same every frame: nothing moves on a still picture)
    const float n = float(pcgHash(unsigned(i) * 3u + 1u) >> 8) * (1.0f / 16777216.0f) + float(pcgHash(unsigned(i) * 3u + 2u) >> 8) * (1.0f / 16777216.0f) - 1.0f;
    const float r = fminf(fmaxf(srgbOetf(c.x) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    const float g = fminf(fmaxf(srgbOetf(c.y) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    const float b = fminf(fmaxf(srgbOetf(c.z) * 255.0f + 0.5f + n, 0.0f), 255.0f);
    out[i] = make_uchar4((unsigned char)r, (unsigned char)g, (unsigned char)b, 255);
}

__global__ void kLinear(HeykelView v, const float4* preview, const float4* accum, float4* out) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= v.width || y >= v.height) return;
    const int i = y * v.width + x;
    const float3 c = blended(v, preview, accum, i);
    out[i] = make_float4(c.x, c.y, c.z, 1.0f);
}

dim3 grid(const HeykelView& v, dim3 b) { return dim3((v.width + b.x - 1) / b.x, (v.height + b.y - 1) / b.y); }

}   // namespace

void heykelPreview(const HeykelView& v, float4* preview, cudaStream_t s) {
    const dim3 b(16, 8);
    kPreview<<<grid(v, b), b, 0, s>>>(v, preview);
}
void heykelPath(const HeykelView& v, float4* accum, int samples, int stripe, int stripes, cudaStream_t s) {
    const dim3 b(16, 8);
    const int rows = (v.height - stripe + stripes - 1) / stripes;
    kPath<<<dim3((v.width + b.x - 1) / b.x, (rows + b.y - 1) / b.y), b, 0, s>>>(v, accum, samples, stripe, stripes);
}
void heykelResolve(const HeykelView& v, const float4* preview, const float4* accum, uchar4* out, cudaStream_t s) {
    const dim3 b(16, 8);
    kResolve<<<grid(v, b), b, 0, s>>>(v, preview, accum, out);
}
void heykelLinear(const HeykelView& v, const float4* preview, const float4* accum, float4* out, cudaStream_t s) {
    const dim3 b(16, 8);
    kLinear<<<grid(v, b), b, 0, s>>>(v, preview, accum, out);
}
