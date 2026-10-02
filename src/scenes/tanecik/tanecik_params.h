// Scene 4 "Tanecik": launch parameters shared by the host and the device program (device/tanecik.cu).
// A small path tracer for a photo studio: a white cyclorama, rectangular soft boxes, a figure whose skin is either a
// tinted glass shell (the inside shows: bones and particles) or opaque skin.
#pragma once
#include "shared/vec.h"

#if !defined(__CUDACC_RTC__)
#include <optix_types.h>
#endif

// what an instance is: the low byte of its instance id; the bones carry their bone in the next byte
enum TKind : unsigned int { TK_STUDIO = 0, TK_SKIN = 1, TK_BONES = 2, TK_PARTICLES = 3, TK_LIQUID = 4, TK_DROPS = 5, TK_KINDS = 6 };

struct TMesh {
    const float3* pos;
    const float3* nrm;
    const uint3* idx;
};

struct TCamera {
    float3 pos, right, up, fwd;    // orthonormal
    float tanHalfX, tanHalfY;
};

struct TRectLight {                // a soft box: centre, half-extents (its two edges), emitting towards cross(u, v)
    float3 c, u, v;
    float3 Le;                     // radiance
};

#define T_MAX_LIGHTS 4

// what is under a pixel (__raygen__pick): the first thing the camera's ray meets, glass skin included
struct TPick {
    int kind;                      // TKind, -1 nothing
    int index;                     // the bone (TK_BONES), the drawn sphere (TK_PARTICLES, TK_DROPS), the triangle (meshes)
    float t;                       // distance along the ray
    float3 pos, normal;            // world; the normal faces the camera
    float3 dir;                    // the ray's direction
};

struct TanecikParams {
    unsigned int width, height;
    float4* accum;                 // linear RGB summed over the samples of this image
    uchar4* output;
    unsigned int samples, spp, seed;
    OptixTraversableHandle handle;
    const TMesh* meshes;           // [0] the studio, [1] the skin, [2 + b] bone b (at rest; its instance places it)
    const float3* particles;       // sphere centres (the instance TK_PARTICLES), its primitive index
    const float* particleTint;     // per sphere: 0 tissue .. 1 freed liquid (display)
    float particleRadius;          // as drawn (m)
    TMesh liquid;                  // the free liquid's surface (rebuilt every frame from its particles)
    const float4* drops;           // its droplets: centre, radius (the instance TK_DROPS)
    // the floor as a canvas: liquid that settled was taken out of the simulation and soaked in here (litres per m2,
    // a square map of stainRes^2 cells from stainMin, stainSize metres on a side; null: none)
    const float* stain;
    int stainRes;
    float2 stainMin;
    float stainSize;
    float3 stainAlbedo;            // a soaked-in stain's colour (dried blood on white: a brown red)
    float stainDepth;              // litres per m2 that make a stain all but opaque
    TCamera cam;
    // the look
    int glass;                     // 1 the glass figure, 0 opaque skin
    float3 skinAlbedo;             // opaque skin, and the glass shell's frosted part
    float3 shellOptical;           // glass: the shell's optical depth at normal incidence (sigma * thickness), per channel
    float skinFrost;               // glass: the share of light the shell scatters (gives it form)
    float skinF0;                  // Fresnel at normal incidence
    float skinGloss;               // opaque skin: Phong exponent of its sheen
    float liquidF0;                // the liquid's Fresnel at normal incidence (a wet mirror over its colour)
    float3 boneAlbedo, tissueAlbedo, liquidAlbedo, studioAlbedo;
    // light
    TRectLight lights[T_MAX_LIGHTS];
    int lightCount;
    float3 ambient;                // what escapes the studio sees (a large white tent)
    int maxDiffuse;                // diffuse bounces
    float exposure;
    float2 pickPixel;              // render pixels
    TPick* pickOut;
};
