#include "scenes/tanecik/tanecik_kernels.h"

namespace {

__global__ void kGather(const float3* pos, const float* tint, const uint32_t* idx, int n, float3* outPos, float* outTint, bool hideLiquid) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    const uint32_t i = idx ? idx[k] : uint32_t(k);
    const float t = tint ? tint[i] : 0.0f;
    outPos[k] = hideLiquid && t >= 0.5f ? make_float3(__int_as_float(0x7fc00000), 0.0f, 0.0f) : pos[i];
    outTint[k] = t;
}

__device__ float3 turn(const float* m, float3 v) {
    return make_float3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z, m[8] * v.x + m[9] * v.y + m[10] * v.z);
}

__global__ void kSkin(const SkinBind* bind, int nv, const float3* particles, const uint8_t* particleBone, const float* boneXf, const float* tint,
                      float tearDistance, float3* outPos, float3* outNrm) {
    const int v = blockIdx.x * blockDim.x + threadIdx.x;
    if (v >= nv) return;
    const SkinBind b = bind[v];
    if (tint)
        for (int k = 0; k < 4; ++k)
            if (b.w[k] > 0.0f && tint[b.p[k]] >= 0.5f) {
                outPos[v] = make_float3(__int_as_float(0x7fc00000), 0.0f, 0.0f);
                return;
            }
    float3 c = make_float3(0, 0, 0), off = make_float3(0, 0, 0), n = make_float3(0, 0, 0);
    for (int k = 0; k < 4; ++k) {
        if (b.w[k] <= 0.0f) continue;
        const float3 p = particles[b.p[k]];
        const float* m = boneXf + 12 * particleBone[b.p[k]];
        c = c + p * b.w[k];
        off = off + turn(m, b.offset) * b.w[k];
        n = n + turn(m, b.normal) * b.w[k];
    }
    if (tearDistance > 0.0f)
        for (int k = 0; k < 4; ++k) {
            if (b.w[k] <= 0.0f) continue;
            const float3 d = particles[b.p[k]] - c;
            if (dot(d, d) > tearDistance * tearDistance) {
                outPos[v] = make_float3(__int_as_float(0x7fc00000), 0.0f, 0.0f);
                return;
            }
        }
    outPos[v] = c + off;
    const float l = sqrtf(dot(n, n));
    outNrm[v] = l > 1e-12f ? n / l : b.normal;
}

__global__ void kStain(const float3* pos, const uint32_t* idx, int n, float litresEach, float footprint, float* map, int res, float2 mn, float size) {
    const int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;
    const float3 p = pos[idx ? idx[k] : uint32_t(k)];
    const float cell = size / res;
    const int r = max(1, int(ceilf(footprint / cell)));
    const float cu = (p.x - mn.x) / cell - 0.5f, cv = (p.z - mn.y) / cell - 0.5f;
    // a cone footprint (radius 'footprint'), normalised over the cells it touches: the litres arrive whole
    float wsum = 0.0f;
    for (int dj = -r; dj <= r; ++dj)
        for (int di = -r; di <= r; ++di) {
            const int i = int(roundf(cu)) + di, j = int(roundf(cv)) + dj;
            const float dx = (i - cu) * cell, dz = (j - cv) * cell;
            wsum += fmaxf(0.0f, 1.0f - sqrtf(dx * dx + dz * dz) / footprint);
        }
    if (wsum <= 0.0f) return;
    for (int dj = -r; dj <= r; ++dj)
        for (int di = -r; di <= r; ++di) {
            const int i = int(roundf(cu)) + di, j = int(roundf(cv)) + dj;
            if (i < 0 || j < 0 || i >= res || j >= res) continue;
            const float dx = (i - cu) * cell, dz = (j - cv) * cell;
            const float w = fmaxf(0.0f, 1.0f - sqrtf(dx * dx + dz * dz) / footprint) / wsum;
            if (w > 0.0f) atomicAdd(&map[j * res + i], litresEach * w / (cell * cell));
        }
}

}   // namespace

void launchStain(const float3* pos, const uint32_t* idx, int n, float litresEach, float footprint, float* map, int res, float2 mn, float size,
                 cudaStream_t s) {
    if (n <= 0) return;
    kStain<<<(n + 127) / 128, 128, 0, s>>>(pos, idx, n, litresEach, footprint, map, res, mn, size);
}

void launchGatherParticles(const float3* pos, const float* tint, const uint32_t* idx, int n, float3* outPos, float* outTint, bool hideLiquid,
                           cudaStream_t s) {
    if (n <= 0) return;
    kGather<<<(n + 255) / 256, 256, 0, s>>>(pos, tint, idx, n, outPos, outTint, hideLiquid);
}

void launchSkin(const SkinBind* bind, int vertices, const float3* particles, const uint8_t* particleBone, const float* boneXf, const float* tint,
                float tearDistance, float3* outPos, float3* outNrm, cudaStream_t s) {
    if (vertices <= 0) return;
    kSkin<<<(vertices + 255) / 256, 256, 0, s>>>(bind, vertices, particles, particleBone, boneXf, tint, tearDistance, outPos, outNrm);
}
