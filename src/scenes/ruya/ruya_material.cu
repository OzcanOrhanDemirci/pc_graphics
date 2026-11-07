#include "scenes/ruya/ruya_material.h"
#include "scenes/ruya/ruya_shared.h"

namespace ruya {
namespace {
struct Grain { float value, du, dv; };

__device__ Grain grain(float u, float v, int frequency, unsigned seed) {
    const float x = u * frequency;
    const float y = v * frequency;
    const int ix = int(floorf(x));
    const int iy = int(floorf(y));
    const float tx = x - ix;
    const float ty = y - iy;
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sy = ty * ty * (3.0f - 2.0f * ty);
    const auto cell = [=](int a, int b) {
        return randomUnit(unsigned(a & (frequency - 1)) * 0x9e3779b9U ^
                          unsigned(b & (frequency - 1)) * 0x85ebca6bU ^ seed);
    };
    const float a = cell(ix, iy), b = cell(ix + 1, iy);
    const float c = cell(ix, iy + 1), d = cell(ix + 1, iy + 1);
    const float lower = lerpf(a, b, sx), upper = lerpf(c, d, sx);
    return {lerpf(lower, upper, sy), 6.0f * tx * (1.0f - tx) * lerpf(b - a, d - c, sy),
            6.0f * ty * (1.0f - ty) * (upper - lower)};
}

__device__ unsigned char pack(float value) { return static_cast<unsigned char>(saturate(value) * 255.0f + 0.5f); }

__global__ void bakeBase(cudaSurfaceObject_t target) {
    const int x = int(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = int(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= kMaterialResolution || y >= kMaterialResolution) return;
    const float u = (x + 0.5f) / kMaterialResolution;
    const float v = (y + 0.5f) / kMaterialResolution;
    const Grain broad = grain(u, v, 8, 17);
    const Grain erosion = grain(u, v, 32, 61);
    const Grain grains = grain(u, v, 128, 103);
    const Grain pores = grain(u, v, 512, 197);
    const Grain crystals = grain(u, v, 1024, 349);
    const float layerPhase = 2.0f * M_PIf * (9.0f * v + 2.0f * u + 0.47f * broad.value);
    const float bands = sinf(layerPhase);
    const float pit = smoothstepf(0.64f, 0.94f, pores.value) * lerpf(0.22f, 1.0f, erosion.value);
    const float tint = 0.53f + 0.22f * (broad.value - 0.5f) + 0.21f * (erosion.value - 0.5f) +
                       0.15f * (grains.value - 0.5f) - 0.36f * pit + 0.068f * bands +
                       0.09f * (crystals.value - 0.5f);
    const float roughness = 0.56f + 0.22f * erosion.value + 0.16f * pit;
    const float slopeU = 0.09f * broad.du + 0.18f * erosion.du + 0.14f * grains.du -
                         0.31f * pit * pores.du + 0.055f * crystals.du;
    const float slopeV = 0.09f * broad.dv + 0.18f * erosion.dv + 0.14f * grains.dv -
                         0.31f * pit * pores.dv + 0.055f * crystals.dv + 0.066f * cosf(layerPhase);
    surf2Dwrite(make_uchar4(pack(tint), pack(roughness), pack(0.5f + slopeU), pack(0.5f + slopeV)),
                target, x * int(sizeof(uchar4)), y);
}

__global__ void reduce(cudaSurfaceObject_t source, cudaSurfaceObject_t target, int side) {
    const int x = int(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = int(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= side || y >= side) return;
    uint4 sum = make_uint4(0, 0, 0, 0);
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            const auto p = surf2Dread<uchar4>(source, (x * 2 + dx) * int(sizeof(uchar4)), y * 2 + dy);
            sum.x += p.x; sum.y += p.y; sum.z += p.z; sum.w += p.w;
        }
    }
    // Use round-to-even so repeatedly averaging quantized levels does not drift upwards.
    const auto average = [](unsigned value) { return static_cast<unsigned char>((value + 1 + ((value >> 2) & 1U)) / 4); };
    surf2Dwrite(make_uchar4(average(sum.x), average(sum.y), average(sum.z), average(sum.w)),
                target, x * int(sizeof(uchar4)), y);
}
}

cudaError_t bakeMaterialMap(MaterialMap& map, cudaStream_t stream) {
    constexpr int levels = 13;
    static_assert((1 << (levels - 1)) == kMaterialResolution);
    const auto format = cudaCreateChannelDesc<uchar4>();
    cudaError_t result = cudaMallocMipmappedArray(&map.mip, &format, make_cudaExtent(kMaterialResolution, kMaterialResolution, 0),
                                                 levels, cudaArraySurfaceLoadStore);
    if (result != cudaSuccess) return result;
    cudaSurfaceObject_t surfaces[levels]{};
    const dim3 block(16, 16);
    for (int level = 0; level < levels && result == cudaSuccess; ++level) {
        cudaResourceDesc resource{};
        resource.resType = cudaResourceTypeArray;
        result = cudaGetMipmappedArrayLevel(&resource.res.array.array, map.mip, level);
        if (result == cudaSuccess) result = cudaCreateSurfaceObject(&surfaces[level], &resource);
        if (result != cudaSuccess) break;
        const int side = kMaterialResolution >> level;
        const dim3 grid((side + block.x - 1) / block.x, (side + block.y - 1) / block.y);
        if (level == 0) bakeBase<<<grid, block, 0, stream>>>(surfaces[level]);
        else reduce<<<grid, block, 0, stream>>>(surfaces[level - 1], surfaces[level], side);
        result = cudaGetLastError();
    }
    const cudaError_t finished = cudaStreamSynchronize(stream);
    for (cudaSurfaceObject_t surface : surfaces) if (surface) cudaDestroySurfaceObject(surface);
    if (result != cudaSuccess) return result;
    if (finished != cudaSuccess) return finished;
    cudaResourceDesc resource{};
    resource.resType = cudaResourceTypeMipmappedArray;
    resource.res.mipmap.mipmap = map.mip;
    cudaTextureDesc sampler{};
    sampler.addressMode[0] = sampler.addressMode[1] = cudaAddressModeWrap;
    sampler.filterMode = sampler.mipmapFilterMode = cudaFilterModeLinear;
    sampler.readMode = cudaReadModeNormalizedFloat;
    sampler.normalizedCoords = 1;
    sampler.maxMipmapLevelClamp = float(levels - 1);
    return cudaCreateTextureObject(&map.texture, &resource, &sampler, nullptr);
}

void releaseMaterialMap(MaterialMap& map) {
    if (map.texture) cudaDestroyTextureObject(map.texture);
    if (map.mip) cudaFreeMipmappedArray(map.mip);
    map = {};
}
}
