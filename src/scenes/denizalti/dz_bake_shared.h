// Scene 8's oven: what the host and the device program (NVRTC) share.
#pragma once
#include "shared/vec.h"

#if !defined(__CUDACC_RTC__)
#include <optix_types.h>
#endif

#define DZ_MAX_MATS 32

// a lamp: a glowing sphere (a bulb's filament glow seen through its glass, spread over the bulb)
struct DzLight {
    float3 p;
    float r;
    float3 L;      // radiance (the scene's units: cd/m^2 / 1000)
    float pad;
};

struct DzBakeParams {
    OptixTraversableHandle handle;
    const float4* vtxN;        // per vertex: shading normal
    const uint3* idx;          // per triangle
    const unsigned int* triMat;
    const float4* texP;        // per texel of this batch: position (w unused)
    const float4* texN;        // normal
    float4* acc0;              // sums: L0 rgb (w: samples)
    float4* acc1;              // L1 of red (xyz), green x
    float4* acc2;              // green yz, blue x... see dz_bake.cpp
    float4* acc3;
    float4* acc4;              // the visibility of lamps 0..3 seen from the texel (their specular's shadow)
    unsigned int count;        // texels in the batch
    unsigned int sampleBase;   // the first sample's index (the sequence continues across launches)
    unsigned int samples;      // samples per texel in this launch
    unsigned int maxBounces;
    const DzLight* lights;
    unsigned int lightCount;
    unsigned int texelBase;    // the batch's first texel (its global index seeds the sequence)
    float3 albedo[DZ_MAX_MATS];
    unsigned int emissiveMat, glassMat;
    // the haze volume: its lower corner, cell size, cells (the voxel pass)
    float3 volLo;
    float volCell;
    uint3 volDim;
};
