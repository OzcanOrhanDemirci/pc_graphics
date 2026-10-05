// Scene 7 "Ölçek": the CUDA kernels. Deterministic, as scenes 5 and 6's are (Özcan 2026-10-05: no grain, no flicker):
// every pixel takes the same four fixed sub-pixel samples every frame (the R2 sequence, the first at the centre,
// Blackman-Harris weights), nothing accumulates, a still view is the same picture bit for bit. Points of light are
// Gaussians on the screen whose sum is their flux, so a body that shrinks below a pixel keeps its light (PLAN §7).
#pragma once
#include "scenes/olcek/olcek_types.h"

#include <cuda_runtime.h>

namespace olcek {

// the frame's view into constant memory
void setView(const OlcekView& v);
// the picture: rgb (display-referred, before the tone curve) into accum, the centre sample's depth into depth
void renderFrame(float4* accum, float* depth, int width, int height, int samples, cudaStream_t s);
// points of light added onto accum, hidden where a solid surface is nearer than they are
void splatPoints(const GSplat* splats, int count, float4* accum, const float* depth, int width, int height, cudaStream_t s);
// the particles over accum, each pixel through its tile's list (tileStart: tiles + 1 offsets into tileIdx)
void drawParticles(const GParticle* parts, const int* tileStart, const int* tileIdx, float4* accum, int width, int height, cudaStream_t s);
// accum -> RGBA8: the colour's fade (saturation), AgX, sRGB, a fixed dither
void resolve(const float4* accum, uchar4* out, int width, int height, float saturation, cudaStream_t s);
// the picture's luminance box-filtered to a quarter of its size (the jump test)
void quarterLuma(const uchar4* in, float* out, int width, int height, cudaStream_t s);

}   // namespace olcek
