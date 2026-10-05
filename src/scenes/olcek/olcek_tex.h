// Scene 7 "Ölçek": the maps (tools/olcek_veri.py writes them as .tex: a 16-byte header "OLTX", width, height, levels,
// then every mip level from the finest, RGBA8 sRGB). Loaded as they are into a CUDA mipmapped array: no decoding at
// the scene's start. Read with sRGB -> linear conversion and trilinear filtering (tex2DLod).
#pragma once
#include <cuda_runtime.h>

#include <string>

namespace olcek {

struct Texture {
    cudaMipmappedArray_t arr = nullptr;
    cudaTextureObject_t tex = 0;
    int width = 0, height = 0, levels = 0;
    size_t bytes = 0;
    // wrapU: the longitude of a world map wraps; everything else clamps
    bool load(const std::string& path, bool wrapU, std::string& err);
    void release();
};

}   // namespace olcek
