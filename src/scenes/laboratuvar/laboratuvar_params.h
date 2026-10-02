// Scene 4 "Laboratuvar": launch parameters shared by the host and the device program (device/laboratuvar.cu).
// A small path tracer for a high-speed photography set: a dark backdrop with a large soft panel behind the experiment
// (liquids and glass read by their refracted edges, the classic liquid-photography light), a key light, strips.
// Everything drawn is an OBJECT (an OptiX instance; its instance id is its index here) of one MATERIAL: triangle meshes
// (the set, glass ware, the liquids' surfaces rebuilt every frame) or spheres (droplets, sand grains).
#pragma once
#include "shared/vec.h"

#if !defined(__CUDACC_RTC__)
#include <optix_types.h>
#endif

enum LMatType : int {
    LM_DIFFUSE = 0,   // Lambert, with an optional glossy coat (f0 > 0)
    LM_GLASS = 1,     // a dielectric solid (ior), absorbing inside (sigma); glass ware has two walls
    LM_LIQUID = 2,    // the same, for a liquid's surface (water, honey, juice): a ray inside absorbs on the way
    LM_WET = 3,       // an opaque, wet body: a mirror coat (Fresnel f0) over a diffuse colour (watermelon flesh)
    LM_SHELL = 4,     // a thin tinted membrane (balloon rubber): coat, a frosted share, the rest passes tinted
    LM_METAL = 5,     // a polished metal (albedo = its reflectance), glossy (gloss)
};

enum LPattern : int { LP_NONE = 0, LP_SLATE = 1, LP_BACKDROP = 2, LP_RIND = 3, LP_WOOD = 4, LP_SAND = 5 };

struct LMaterial {
    int type;
    int pattern;          // a procedural variation of its colour (LPattern)
    float ior;            // dielectrics
    float f0;             // a coat's Fresnel at normal incidence (diffuse, wet, shell)
    float gloss;          // the coat's (or metal's) Phong exponent; >= 5000: a perfect mirror
    float frost;          // shell: the share that scatters (gives it form)
    float3 albedo;        // diffuse colour; metal: reflectance
    float3 sigma;         // dielectrics: absorption per metre; shell: optical depth per crossing
};

struct LObject {
    int kind;             // 0 triangles, 1 spheres
    int material;
    const float3* pos;    // triangles: object space
    const float3* nrm;
    const uint3* idx;
    const float4* spheres;   // spheres: centre (world), radius
    const float* tint;       // spheres: an optional brightness per sphere (sand grains), null: 1
};

struct LCamera {
    float3 pos, right, up, fwd;   // orthonormal
    float tanHalfX, tanHalfY;
};

struct LRectLight {              // a soft box: centre, half-extents (its two edges), emitting towards cross(u, v)
    float3 c, u, v;
    float3 Le;                   // radiance
    int visible;                 // seen straight from the camera (the panel behind the experiment)
    float fall;                  // how much darker its edges are than its middle (a diffuser's falloff), 0..1
};

#define L_MAX_LIGHTS 6

// what is under a pixel (__raygen__pick): the first opaque or liquid thing the camera's ray meets
struct LPick {
    int object;                  // -1 nothing
    int prim;
    float t;
    float3 pos, normal;          // world; the normal faces the camera
    float3 dir;
};

struct LabParams {
    unsigned int width, height;
    float4* accum;               // linear RGB summed over the samples of this image
    uchar4* output;
    float4* albedoOut;           // the first diffuse-like hit's albedo and normal (the denoiser's guides; may be null)
    float4* normalOut;
    unsigned int samples, spp, seed;
    OptixTraversableHandle handle;
    const LObject* objects;
    const LMaterial* materials;
    LCamera cam;
    LRectLight lights[L_MAX_LIGHTS];
    int lightCount;
    float3 ambient;              // what escapes the set sees (a dark studio)
    int maxDiffuse;
    float exposure;
    float gapMax;                // m: a dielectric left and another entered within this: one interface (water in glass)
    float2 pickPixel;
    LPick* pickOut;
};
