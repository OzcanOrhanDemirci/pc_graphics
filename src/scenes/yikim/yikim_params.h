// Scene 3 "Yikim": launch parameters shared by the host and the device program (src/device/yikim.cu).
// A plain daylight renderer for now (Y0): sun with soft shadows, sky light with one bounce off the ground, flat
// shaded pieces, a ground plane with a metre grid that moves with the earthquake. Y5 replaces it.
#pragma once
#include "shared/vec.h"

#if !defined(__CUDACC_RTC__)
#include <optix_types.h>
#endif

struct YMesh {                 // one body's hull, in its body frame
    const float3* pos;
    const uint3* idx;
};

struct YBody {                 // per instance (instance index = body index - 1)
    float3 albedo;
    float damage;              // 0..1: share of the body's fibres cracked or broken (display only)
    float sphere;              // a ball's radius: shaded with the sphere's normal; 0: the faces' own
    float glass;               // 1: shaded as glass (a reflection over a dark tint, glassShade)
};

struct YCamera {
    float3 pos, right, up, fwd;    // orthonormal
    float tanHalfX, tanHalfY;
};

struct YikimParams {
    unsigned int width, height;
    float4* accum;                 // linear RGB, summed over the samples of this image
    uchar4* output;
    unsigned int samples;          // samples already in accum
    unsigned int spp;              // samples to add in this launch
    unsigned int seed;
    OptixTraversableHandle handle;
    const YMesh* meshes;
    const YBody* bodies;
    YCamera cam;
    float3 sunDir;                 // towards the sun
    float sunCosMax;               // cos of the sun's angular radius
    float3 sunE;                   // sun irradiance on a surface facing it
    float3 skyZenith, skyHorizon;  // sky radiance
    float3 groundAlbedo;
    float groundY;                 // the ground plane (moves with the earthquake)
    float2 groundShift;            // horizontal ground displacement (the grid rides along)
    float exposure;
    float damageTint;              // display: how much a cracked body darkens
    // picking (__raygen__pick, one thread): the body under a pixel
    float2 pickPixel;              // in render pixels
    int* pickBody;                 // out: body index (instance + 1), 0 the ground, -1 the sky
    float4* pickHit;               // out: hit point xyz, distance w
    // the AI denoiser (temporal AOV model): guides summed like accum, then means (__raygen__prep), then the display
    float4* albedoAcc;             // first-hit albedo, summed over the samples (null: no guides)
    float4* normalAcc;             // first-hit world normal, summed
    float4* motion;                // xy: pixel movement from the previous frame (render pixels); z: the point's distance
                                   // from last frame's camera (first, pixel-centre ray)
    float4* gbuf;                  // first ray: x distance, y what is seen (body + 1, 0 the ground, -1 the sky)
    float4 *dnIn, *dnAlbedo, *dnNormal;   // the denoiser's input and guides (means)
    const float4* denoised;        // its output, linear (tone mapped by __raygen__tonemap)
    // motion: each instance's 3x4 transform now (inside the OptixInstance array) and a frame ago (12 floats each)
    const float* xfCur;
    unsigned int xfStride;         // floats between instances in xfCur
    const float* xfPrev;
    unsigned int barInstance;      // the bars' instance (world space, rebuilt every frame: taken as still)
    YCamera prevCam;
    float2 groundShiftPrev;
    float groundYPrev;
};
