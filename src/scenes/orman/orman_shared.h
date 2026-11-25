// Scene 11 "Orman": what the host and the OptiX programs (device/orman.cu) share. The world is instances of open-licence
// models (docs/orman/VARLIK.md): a prototype (one variant of a model) is one GAS whose build inputs are its glTF
// primitives; an instance points at its prototype, or (a copy of the forest's tile around the stream's patch) at the
// tile's own IAS: two levels, the instance id the inner one's. The cells are a torus (period): what the patch baked is
// read where the eye's copy of it stands. The light is baked (a probe grid of the sky's and the bounces' light,
// the ground's own occlusion per vertex); the sun is traced live, one shadow ray a sample (the dapples through the leaves
// are the forest's life). docs/orman/PLAN.md §3.
#pragma once
#include "shared/vec.h"

#if !defined(__CUDACC_RTC__)
#include <optix_types.h>
#endif

#define ORMAN_MAX_SPP 16
#define ORMAN_PHASES 16       // DLSS: frames before the sub-pixel sequence repeats (the guide asks 8 or more for DLAA)
#define ORMAN_MAX_INPUTS 16   // primitives a prototype may have (its GAS's build inputs; that many identical hit records)

// one glTF primitive (a build input of its prototype's GAS), in the prototype's own space
struct OGeom {
    const float3* pos;
    const float3* nrm;
    const float2* uv;
    const uint3* idx;
    const float* ao;          // per vertex (the ground: the plants' occlusion over it, baked), or null
    int material;
    float uvScale;            // texture coordinates times this (the ground: metres to tiles)
};

#define OMAT_DOUBLE 1u        // both sides drawn, the normal faces the ray
#define OMAT_FOLIAGE 2u       // thin and translucent: the light through it from behind
#define OMAT_ALPHA 4u         // cut by its alpha map (the any-hit program)
#define OMAT_WATER 8u         // the stream's surface: a mirror and a window (one ray each)

// what a ray is for (the any-hit's third payload): a shadow ray (the fourth payload carries the light it lets through), or
// a sample of a set: its index in the low byte, the set's size in the next (0: one fixed cutoff)
#define ORAY_SHADOW 0x80000000u
#define ORAY_SAMPLE(k, n) ((unsigned int)(k) & 0xFFu) | (((unsigned int)(n) & 0xFFu) << 8)

#define OMASK_SOLID 1u        // everything that stops light
#define OMASK_WATER 2u        // the water's surface: seen, but casting no shadow (the oven and the sun look through it)

struct OMaterial {
    unsigned long long albedo;   // cudaTextureObject_t: sRGB, read linear
    unsigned long long normal;   // tangent space, OpenGL convention (y up); 0 none
    unsigned long long arm;      // occlusion, roughness, metal (glTF's ORM); 0 none
    unsigned long long alpha;    // one channel; 0 none
    float alphaCutoff;
    float translucency;          // share of the light passing through a leaf
    float roughness;             // without an arm map
    unsigned int flags;
    float3 tint;                 // the albedo times this (one source's colours brought to the others')
    float texelArea;             // texels of the albedo's level 0 (w * h): the ray cone's level of detail
};

struct OProto {
    unsigned int firstGeom, geomCount;
};

// per OptiX instance, by its instance id
struct OInstance {
    unsigned int proto;
    float scale;              // uniform: the ray cone into the prototype's space
    float3 tint;              // this copy's colour variation (times the material's)
};

// the baked light: a regular grid of probes, each the irradiance of every direction as L1 spherical harmonics per
// colour (sh0 = L00 r g b + validity, sh1..3 = L1 for r, g, b: (x, y, z) and 0)
struct OGrid {
    float4* sh[4];
    int nx, ny, nz;
    float3 origin;            // probe (0, 0, 0)
    float h;                  // spacing (m)
    int wrap;                 // the forest's tile: x and z go round (nx h and nz h its sides), its copies read it in turn
};

struct OrmanParams {
    OptixTraversableHandle handle;
    unsigned int width, height;
    float4* hdr;              // linear radiance, the pixel's samples filtered
    uchar4* output;           // the picture
    const float4* bloom;      // the glare pyramid's sum (half size), or null

    // the eye
    float3 eye, U, V, W;      // a pixel's ray: W + U * x + V * y, x and y in [-1, 1]
    float pixelAngle;         // radians a pixel (the ray cone's spread)
    int spp;
    float2 pattern[ORMAN_MAX_SPP];   // sub-pixel offsets from the pixel's centre (pixels), the filter's own distribution
    float covBias;            // a leaf's coverage read this many levels above the sample's own (the pixel's reach)
    float sampleFrac;         // where in its 1/spp step a sample's alpha threshold and mist step sit (0.5; the capture's
                              // passes move it, so their union fills the steps)
    float lodBias;            // texture levels finer by this (DLSS gathers the frames: it samples finer)

    // DLSS Ray Reconstruction (orman_dlss.h): one sample a pixel at this frame's place in it, the guides it reads
    int dlss;
    float2 jitter;            // this frame's sample in the pixel (pixels from its centre)
    unsigned int phase;       // its place in the sequence (0 .. ORMAN_PHASES - 1): the sun's disk point, the mist's step,
                              // the alpha threshold turn with it
    float3 prevEye, prevU, prevV, prevW;   // the last frame's eye (the motion vectors)
    unsigned long long gColor, gAlbedo, gSpecular, gNormal, gRough, gDepth, gMotion, gHitT;   // cudaSurfaceObject_t

    // the world
    const OGeom* geoms;
    const OMaterial* materials;
    const OProto* protos;
    const OInstance* instances;

    // the light
    float3 sunDir;            // towards the sun
    float3 sunE;              // the sun's irradiance on a surface facing it
    float sunCos;             // cos of the sun's angular radius
    float2 sunDisk[ORMAN_MAX_SPP];   // per sample: where on the sun's disk its shadow ray aims (unit disk)
    float3 skyZenith, skyHorizon, skyGround;   // the sky's radiance (and the light from below the horizon)
    OGrid grid;               // the stream's patch in the middle
    OGrid tileGrid;           // the forest's tile around it (wrap: every copy reads the same probes), or empty
    float patchHalf;          // the patch's half side (m): within it its own grid, outside the tile's
    float period;             // the torus's side (m): a place and its copies a side apart are one; 0 none
    float giScale;            // the baked light times this (an artistic dial, 1 = physical)
    float aoPower;
    float wetness;            // 0 dry .. 1 just rained: rougher surfaces turn glossy (leaves, stones, bark)
    float time;               // seconds: the water's flow
    float3 waterAbsorb;       // the stream's absorption (1/m, per colour): tea-coloured forest water
    float waterFlow;          // its speed (m/s) along +x

    // the morning's mist: denser near the ground, the sun's shafts through it from a baked visibility volume
    unsigned long long sunVis;   // cudaTextureObject_t, 3D, 0..1 (four shadow rays a voxel), or 0: the patch's
    float3 sunVisOrigin;      // the volume's corner
    float3 sunVisInvSize;     // 1 / the volume's size (m): normalized coordinates
    unsigned long long tileVis;  // the same over the tile, wrapping in x and z, or 0
    float3 tileVisOrigin, tileVisInvSize;
    float fogDensity;         // extinction everywhere (1/m)
    float fogGround;          // more of it at the ground, falling off over fogHeight
    float fogHeight;
    float fogG;               // Henyey-Greenstein's forward scattering
    float fogFar;             // how far it is gathered (m)
    float fogAmbient;         // the forest's own light in the mist (its scattering albedo for the probes' light)
    float wetGloss;           // the wet sheen's strength
    float saturation;         // the display's colour (AgX alone is pale)
    int fogSteps;

    // the display
    float exposure;
    float bloomStrength;
    float vignette;
    int debug;                // 0 the picture; 1 albedo, 2 normals, 3 baked light only, 4 sun only, 5 level of detail
    unsigned int* badCount;   // samples that were not finite (dropped)

    // the oven (load time)
    int bakePass;             // 0 the sky and the sun, then the probes of the pass before
    int bakeRays;
    OGrid bakeGrid;           // the probes being baked (where they stand: the tile's in one of its copies)
    float4* bakeOut[4];       // one probe slab
    int bakeZ0, bakeZn;       // the slab: probes z0 .. z0 + zn - 1
    const float3* aoPos;      // a ground's vertices and normals, for its occlusion
    const float3* aoNrm;
    float3 aoOffset;          // where those vertices stand in the world (the tile's: in one of its copies)
    float* aoOut;
    unsigned int aoCount;
    float aoRadius;
    int aoRays;
    unsigned char* visOut;    // the sun's visibility volume, one slab
    float3 visOrigin;         // its corner where it is baked
    int visN[3];
    float visH;
    int visZ0;

    unsigned int layout;      // sizeof(OrmanParams) on the host: the device program checks it agrees
};
