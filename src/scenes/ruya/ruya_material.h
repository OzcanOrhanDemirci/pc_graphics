#pragma once
#include "scenes/ruya/ruya_shared.h"
#include <cstddef>

namespace ruya {
struct MaterialMap {
    cudaMipmappedArray_t mip = nullptr;
    cudaTextureObject_t texture = 0;
};

inline constexpr std::size_t kMaterialBytes = sizeof(uchar4) * ((std::size_t(kMaterialResolution) * kMaterialResolution * 4 - 1) / 3);
cudaError_t bakeMaterialMap(MaterialMap& map, cudaStream_t stream);
void releaseMaterialMap(MaterialMap& map);
}
