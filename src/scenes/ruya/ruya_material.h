#pragma once
#include "scenes/ruya/ruya_shared.h"
#include <cstddef>

namespace ruya {
struct MaterialMap {
    cudaMipmappedArray_t mip = nullptr;
    cudaTextureObject_t texture = 0;
};

// Uploads a square power-of-two RGBA8 image and builds its full mip chain on the GPU.
cudaError_t uploadMaterialMap(MaterialMap& map, const unsigned char* rgba, int side, bool srgb, cudaStream_t stream);
void releaseMaterialMap(MaterialMap& map);
}
