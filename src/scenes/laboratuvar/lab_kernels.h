// Scene 4 "Laboratuvar": small GPU kernels between the solver, the recording and the renderer.
#pragma once
#include "shared/vec.h"

#include <cuda_runtime.h>

#include <cstdint>

// rest positions moved by a 3x4 row-major transform (a held glass's walls: the solver's kinematic targets)
void launchTransform(const float3* rest, int n, const float xf[12], float3* out, cudaStream_t s);
// what each particle is drawn as now: its code at build, a flesh particle turned liquid drawn as juice
void launchCodes(const uint8_t* base, const float* fluid, int n, uint8_t* out, cudaStream_t s);
// a surface's weights: 1 where the code is `code`, else 0 (FluidSurface leaves out weights <= 0)
void launchWeights(const uint8_t* codes, int n, uint8_t code, float* out, cudaStream_t s);
void launchWeightsAdd(const uint8_t* codes, int n, uint8_t code, float* out, cudaStream_t s);   // and 1 where it is code too
// sand: `per` grains around each sand particle (fixed offsets by a hash of its index, so a grain rides with it),
// radius from rMin to rMax, a brightness each; returns nothing, writes n * per spheres (non-sand: radius 0)
void launchGrains(const float3* pos, const uint8_t* codes, int n, int per, float spread, float rMin, float rMax, float4* spheres, float* tint, cudaStream_t s);
// a recording: positions quantised into a box (16 bits an axis) with the code in w; and back, between two frames
struct QuantBox {
    float3 lo;
    float3 scale;    // metres per unit
};
void launchQuantise(const float3* pos, const uint8_t* codes, int n, QuantBox b, ushort4* out, cudaStream_t s);
void launchDequantise(const ushort4* a, const ushort4* b, float t, int n, QuantBox q, float3* pos, uint8_t* codes, cudaStream_t s);
// the listed particles' positions and codes, packed
void launchGather(const float3* pos, const uint8_t* codes, const uint32_t* idx, int n, float3* outPos, uint8_t* outCodes, cudaStream_t s);
// a membrane drawn by its own triangles: those whose edges are all shorter than tear (m) kept, compacted; smooth
// normals accumulated from the kept faces. Returns the kept count through *count (device int).
void launchMembrane(const float3* pos, int first, const uint3* tris, int nTris, float tear, uint3* outTris, int* count, float3* verts, float3* nrm,
                    int nVerts, cudaStream_t s);
// the denoiser: the accumulation's mean (times exposure) as its input; its output tone-mapped to the display
void launchDenoisePrep(const float4* accum, unsigned samples, float exposure, int n, float4* out, cudaStream_t s);
void launchTonemap(const float4* lin, int w, int h, unsigned seed, uchar4* out, cudaStream_t s);
// temporal accumulation (live): this frame's mean (accum / spp) blended with last frame's result, found where this
// pixel's surface was in the previous camera, clamped to this frame's neighbourhood (YCoCg mean +- gamma sigma) so a
// moved thing leaves no ghost; blend max(1 / frames, alphaMin). Out: linear RGB, w = frames accumulated.
struct TCam {
    float3 pos, right, up, fwd;
    float tanHalfX, tanHalfY;
    float moved;   // how far the camera went since (m)
};
// pos / prevPos: the steady ray's first hit and length, this frame and the last (history from a tap whose surface or
// length differs is not used: what moved, and what it uncovered, start over instead of leaving a trail)
void launchTemporal(const float4* accum, float invSpp, const float4* pos, const float4* prevPos, TCam prev, const float4* histIn, float4* histOut, int w,
                    int h, float alphaMin, float gamma, float maxFrames, bool reset, cudaStream_t s);
// the mean of a range of positions as a sphere of radius r (a steel ball drawn whole)
// slow motion's drawing between two substeps: b = a + (b - a) t for the first n (newer particles stay as they are)
void launchLerp(const float3* a, float3* b, int n, float t, cudaStream_t s);
void launchCentre(const float3* pos, int first, int count, float r, float4* out, cudaStream_t s);
