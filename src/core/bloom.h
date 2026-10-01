// Lens glare (bloom): a pyramid of progressively wider blurs of an HDR image, summed (scenes 1 and 2).
// levels/accum hold nLevels images of halving size; accum[0] (half size) is the result.
#pragma once
#include <cuda_runtime.h>

void launchBloom(const float4* hdr, int w, int h, float4** levels, float4** accum, int nLevels, cudaStream_t stream);
