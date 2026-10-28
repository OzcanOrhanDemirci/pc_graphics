// Scene 12 "Ateş": what the host and the OptiX programs (device/ates.cu) share. The surfaces (the stones, the ground,
// the logs) are triangles traced by OptiX; the fire is the gas's grid (ates_gas.h), marched by the volume pass after
// DLSS: its light is computed, not painted. Units: luminance in cd/m^2 (683 times the radiance weighted by the eye's
// curve), so the night's sky, a lit stone and the flame stand in their true ratios. docs/ates/PLAN.md §3.5.
#pragma once
#include "shared/vec.h"
#include "scenes/orman/orman_shared.h"

#define ATES_PHASES 16        // DLSS: frames before the sub-pixel sequence repeats
#define ATES_MAX_SPP 16
#define ATES_TABLE_T0 300.0f  // the emission tables: temperature of the first entry (K)
#define ATES_TABLE_DT 10.0f   // their step (K)
#define ATES_TABLE_N 300      // entries (300 .. 3290 K)

// a log's patches for shading (ates_wood.h): the state the eye sees on its surface
struct ALogView {
    const float* surfT;       // per patch: the surface's temperature (K)
    const float* charD;       // the char's depth (m)
    const float* ash;         // the ash's thickness (m)
    unsigned int firstPatch;  // this log's first patch in the arrays
    int nTheta, nZ;           // its patches around and along
    float r0, len;            // its first radius and length (m)
    unsigned int seed;        // its own crack pattern
    float prevPose[12];       // where it lay a frame ago (row-major 3x4): its surface's motion for DLSS
};

// per OptiX instance (by its id): its geometries, and the log it is (or -1)
struct AInst {
    unsigned int firstGeom, geomCount;
    int log;
    float scale;
    float3 tint;
};

struct AtesParams {
    OptixTraversableHandle handle;
    unsigned int width, height;
    float4* hdr;              // the surfaces' picture (linear), then with the fire over it
    float4* surf;             // a copy of the surfaces' picture (the heat haze reads around in it)
    float* depth;             // per pixel: the distance to the surface along the ray (m), 1e30 the sky
    uchar4* output;
    const float4* bloom;

    // the eye
    float3 eye, U, V, W;
    float pixelAngle;
    int spp;
    float2 pattern[ATES_MAX_SPP];
    float lodBias;

    // DLSS Ray Reconstruction (the surfaces only; the fire is drawn over its picture)
    int dlss;
    float2 jitter;
    unsigned int phase;
    float3 prevEye, prevU, prevV, prevW;
    unsigned long long gColor, gAlbedo, gSpecular, gNormal, gRough, gDepth, gMotion, gHitT;

    // the world
    const OGeom* geoms;
    const OMaterial* materials;
    const AInst* instances;
    const ALogView* logs;
    int groundMaterial;       // the burnt ground of the pit
    int groundMaterial2;      // the ground away from the pit (blended in by distance), or -1
    float pitRadius;          // the burnt ground's reach (m)

    // the fire's light on the surfaces: the gas's emission summed over blocks (cd per block, RGB), sampled by its
    // cumulative sum (a sample a pixel a frame, DLSS gathers them)
    const float3* lightI;
    const float* lightCdf;    // inclusive cumulative of the blocks' luminous intensity (G)
    int lightN, lnx, lny, lnz;
    float3 lightOrigin;
    float lightCell;          // a block's side (m)

    // the gas for the eye (float4 per cell: T, soot fv * 1e6, smoke kg/m^3 * 1e3, heat release W/m^3 * 1e-6)
    unsigned long long gasTex;
    float3 gasOrigin;
    float3 gasSize;
    float gasH;
    int gnx, gny, gnz;
    const float3* sootEmit;   // [ATES_TABLE_N] soot's emission per metre per unit (fv * 1e6): cd/m^2 per m (RGB)
    const float3* greyEmit;   // [ATES_TABLE_N] a black body's luminance (cd/m^2, RGB): the embers, times emissivity
    float3 sootAbs;           // soot's absorption per metre per unit (fv * 1e6) at the R, G, B wavelengths
    float3 chemEmit;          // the flame front's own light (CH*, C2*) per W/m^3 of heat release: cd/m^2 per m
    float march;              // the volume's step (m)
    float hazeScale;          // the heat haze times this (1: physical)

    // the night
    float3 skyZenith, skyHorizon;   // cd/m^2
    float ambient;            // the night's light on the ground (cd/m^2 of a white surface)
    float time;

    // the display
    float exposure;
    float bloomStrength;
    float vignette;
    float saturation;
    float contrast;           // a power on AgX's display values (Blender's "Punchy" look: 1.35): AgX alone keeps
                              // sixteen stops, so char seven stops under the flame came out mid-grey, not black
    int debug;
    unsigned int* badCount;

    unsigned int layout;
};
