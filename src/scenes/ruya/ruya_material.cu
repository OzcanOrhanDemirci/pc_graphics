#include "scenes/ruya/ruya_material.h"
#include <vector>
#include "scenes/ruya/ruya_shared.h"

namespace ruya {
namespace {
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

cudaError_t uploadMaterialMap(MaterialMap& map, const unsigned char* rgba, int side, bool srgb, cudaStream_t stream) {
    int levels = 1;
    while ((1 << (levels - 1)) < side) ++levels;
    const auto format = cudaCreateChannelDesc<uchar4>();
    cudaError_t result = cudaMallocMipmappedArray(&map.mip, &format, make_cudaExtent(side, side, 0), levels,
                                                 cudaArraySurfaceLoadStore);
    if (result != cudaSuccess) return result;
    cudaArray_t base = nullptr;
    result = cudaGetMipmappedArrayLevel(&base, map.mip, 0);
    if (result == cudaSuccess)
        result = cudaMemcpy2DToArrayAsync(base, 0, 0, rgba, size_t(side) * 4, size_t(side) * 4, side,
                                          cudaMemcpyHostToDevice, stream);
    std::vector<cudaSurfaceObject_t> surfaces(levels, 0);
    const dim3 block(16, 16);
    for (int level = 0; level < levels && result == cudaSuccess; ++level) {
        cudaResourceDesc resource{};
        resource.resType = cudaResourceTypeArray;
        result = cudaGetMipmappedArrayLevel(&resource.res.array.array, map.mip, level);
        if (result == cudaSuccess) result = cudaCreateSurfaceObject(&surfaces[level], &resource);
        if (result != cudaSuccess || level == 0) continue;
        const int extent = side >> level;
        const dim3 grid((extent + block.x - 1) / block.x, (extent + block.y - 1) / block.y);
        reduce<<<grid, block, 0, stream>>>(surfaces[level - 1], surfaces[level], extent);
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
    sampler.sRGB = srgb ? 1 : 0;
    sampler.maxAnisotropy = 16;
    sampler.maxMipmapLevelClamp = float(levels - 1);
    return cudaCreateTextureObject(&map.texture, &resource, &sampler, nullptr);
}

void releaseMaterialMap(MaterialMap& map) {
    if (map.texture) cudaDestroyTextureObject(map.texture);
    if (map.mip) cudaFreeMipmappedArray(map.mip);
    map = {};
}
}
