// Launch parameters and scene records shared by the host and the OptiX device programs.
// Every struct here is plain data: the host fills it, cudaMemcpy's it, the device reads it.
#pragma once
#include "shared/vec.h"

#if !defined(__CUDACC_RTC__)
#include <optix_types.h>
#endif

// Wavelength domain of the whole renderer (nm). Paths sample it with pbrt-v4's visible-wavelength pdf.
#define LAMBDA_MIN 360.0f
#define LAMBDA_MAX 830.0f
#define SUN_TABLE_N 471               // 1 nm steps, LAMBDA_MIN..LAMBDA_MAX
#define SKY_BINS 48                   // spectral bins of the sky map, 10 nm apart starting at LAMBDA_MIN
#define SKY_BIN_NM 10.0f
#define MAX_PORTALS 8
#define MAX_EMITTERS 24
#define BEAM_PIECES 16                 // each photon beam is split into this many boxes for a tight BVH
#define NOISE_TEX 1024                // baked noise texture size; NOISE_PERIOD gradient cells across it
#define NOISE_PERIOD 128

enum MaterialType : int {
    MAT_DIFFUSE = 0,     // plaster: matte, faint normal noise
    MAT_CONCRETE = 1,    // Ando concrete: form-panel joints, tie holes, mottling, soft sheen
    MAT_STONE = 2,       // polished terrazzo floor: diffuse base + glossy coat
    MAT_DIELECTRIC = 3,  // smooth interface between two optical media (glass, water)
    MAT_FILM = 4,        // soap film: thin-film interference, transmits without bending
    MAT_METAL = 5,       // GGX conductor (brass)
    MAT_WATER = 6,       // pool surface: dielectric air|water with procedural ripple normal
    MAT_DUST = 7,        // airborne dust mote: bright diffuse speck
    MAT_POOL = 8,        // stone under water (pool floor and sides)
};

enum IorModel : int { IOR_AIR = 0, IOR_WATER = 1, IOR_BK7 = 2, IOR_SF11 = 3 };

struct Material {
    int type;
    int iorInside;      // dielectric: medium behind the surface normal
    int iorOutside;     // dielectric: medium the normal points into
    int flags;
    float3 sigA;        // reflectance spectrum, sigmoid-polynomial coefficients (Jakob & Hanika 2019)
    float roughness;    // GGX alpha of the coat / conductor
    float3 sigB;        // second reflectance spectrum (terrazzo chips, concrete tone variation)
    float coat;         // coat weight
    float4 p;           // per-type parameters
};

struct Portal {         // a rectangular opening the sky is seen through, used for sky light sampling
    float3 corner, edgeU, edgeV, normal;  // normal points into the room
    float area;
    float pad[3];
};

struct Camera {
    float3 pos, right, up, fwd;  // right/up/fwd orthonormal
    float tanHalfX, tanHalfY;
    float lensRadius;            // m
    float focusDist;             // m
    int blades;
    float bladeRot;
    float pad[2];
};

struct Photon {                  // a caustic photon resting on a non-specular surface
    float3 pos;
    float lambda;                // nm
    float3 dir;                  // direction of travel (into the surface)
    float flux;                  // spectral flux / wavelength pdf, so it integrates to XYZ via the CMF
};

struct PhotonBeam {              // a caustic photon's straight flight through the hazy air
    float3 a;                    // start (leaves the glass or water here)
    float lambda;
    float3 b;                    // end (next surface)
    float flux;                  // at a, before the haze's attenuation along the beam
};

struct Params {
    // ---- frame / output
    float4* accum;               // xyz = summed XYZ, w = sample count
    float4* causticAccum;        // the caustic (photon) part of accum, kept apart for the denoiser
    float4* aovAlbedo;
    float4* aovNormal;
    float* heat;                 // per-pixel GPU clock cycles of the last launch
    uchar4* output;
    float4* hdr;                 // exposure-scaled linear sRGB of the displayed image (before glare and AgX)
    const float4* bloom;         // lens glare at half resolution (null: none)
    float bloomStrength;         // share of the light spread by the glare point spread function
    unsigned int width, height;  // output (display) size
    unsigned int renderWidth, renderHeight;  // path tracing size: equal in still mode, half in upscaled live mode
    unsigned int frame;          // frames accumulated so far (seeds the sampler)
    unsigned int sppPerLaunch;
    unsigned int seed;           // decorrelates independent renders of the same frame (noise measurements)
    float liveAlpha;             // live mode: exponential blend weight of the new frame (0: progressive average)
    float liveAlphaCaustic;      // same for the caustic buffer
    int cameraDispersion;        // camera paths split colours in glass (1, still mode) or refract along the hero only (0, live)
    int hazeAllSegments;         // single scattering on every path segment (1) or only before the first surface (0)
    float sampleClampY;
    float denoiseClampY;
    unsigned int dispersionBoost;// > 1: pixels looking into dispersive glass trace a stratified spectral split (4 runs)         // the denoiser's input only: luminance ceiling against early fireflies (accumulation untouched)          // live mode: a path sample's luminance is clamped to this (0: no clamp, unbiased)
    int view;                    // 0 beauty, 1 albedo, 2 normal, 3 caustics, 4 heat, 5 relative noise
    float exposure;              // linear multiplier before tone mapping
    const float* blueNoise;      // 128 x 128, values in [0,1)
    unsigned long long noiseTex; // cudaTextureObject_t: baked tileable 2D gradient noise (value, d/du, d/dv, second noise)
    float4* noiseBake;           // bake target, NOISE_TEX x NOISE_TEX
    int ditherFrame;
    float wbMatrix[9];
    const float4* denoised;      // denoised linear sRGB without caustics (null: show the raw accumulation)
    const float4* denoisedCaustic;// live mode: the caustic layer denoised too (null: raw caustics on top)           // XYZ -> XYZ chromatic adaptation (Bradford), row major: camera white balance

    // ---- scene
    OptixTraversableHandle handle;
    const float3* vertices;
    const uint3* indices;
    const float3* normals;       // per vertex
    const int* triMaterial;      // per triangle
    const float4* spheres;       // xyz center, w radius
    const int* sphereMaterial;   // per sphere
    const Material* materials;
    Camera cam;
    float time;                  // seconds, drives water ripples and drifting things

    // ---- light
    float3 sunDir;               // unit vector towards the sun
    float sunCosMax;             // cos of the sun's angular radius
    float sunOneMinusCosMax;     // 1 - sunCosMax, kept separately for precision
    float sunLimb;               // limb darkening coefficient u in 1 - u(1 - mu)
    float sunScale;              // multiplier on the sun's radiance (1 physical; 0 isolates the sky)
    const float* sunTable;       // spectral radiance at disk centre, SUN_TABLE_N entries
    const float* sky;            // [y][x][SKY_BINS] spectral radiance, sun disk excluded
    int skyW, skyH;
    float skyScale;
    const float* skyPdfUV;       // [skyH][skyW] probability density of the sky-map sampler in (u, v)
    const float* skyCdfRow;      // skyH + 1 entries
    const float* skyCdfCol;      // skyH rows of skyW + 1 entries
    float skyMapFraction;        // share of sky light samples drawn from the map (the rest through portals)
    Portal portals[MAX_PORTALS];
    int numPortals;
    Portal sunPortals[MAX_PORTALS];  // outer faces of the openings the sun shines through
    float sunPortalProj[MAX_PORTALS];// their areas projected along the sun direction
    float sunPortalDepth[MAX_PORTALS];// wall thickness: the inner face is outer + normal * depth
    int numSunPortals;
    float sunPortalAreaTotal;
    float groundAlbedo;

    // ---- participating media
    float3 hazeMin, hazeMax;     // the room's air volume
    float hazeSigmaS;            // 1/m, wavelength independent
    float hazeSigmaA;
    float hazeG;                 // Henyey-Greenstein asymmetry
    float hazeBeamFraction;      // share of in-scatter samples placed inside the sunbeams
    float waterDepthTint;

    // ---- caustic photons (progressive photon mapping, radius shrinks over frames)
    const Photon* photons;
    unsigned int numPhotons;
    const unsigned int* cellStart;   // hash grid: first photon of each cell (sorted order)
    const unsigned int* cellEnd;
    unsigned int gridSize;           // number of hash cells (power of two)
    float gatherRadius;
    float cellSize;
    float photonScale;               // 1 / photons emitted in this frame's map

    // ---- photon emission (written by the photon raygen)
    Photon* photonOut;
    unsigned int* photonCount;
    unsigned int photonCapacity;
    unsigned int photonsToEmit;
    float4 emitRect[MAX_EMITTERS];   // per caustic target: (u0, v0, u1, v1) in the plane across the sun direction
    float emitArea[MAX_EMITTERS];
    float emitAreaTotal;
    float3 emitBasisU, emitBasisV;   // the plane's axes, both perpendicular to sunDir
    float emitDepth;                 // the plane's distance along sunDir
    int numEmitters;
    unsigned int photonFrame;        // photon maps traced so far (radius schedule, seeds)
    // ---- photon beams (volumetric caustics): the beams' own acceleration structure, gathered by camera rays
    OptixTraversableHandle beamHandle;
    const PhotonBeam* beams;
    unsigned int numBeams;
    float beamRadius;
    PhotonBeam* beamOut;
    unsigned int* beamCount;
    unsigned int beamCapacity;
    int enableBeams;
    float beamFraction;          // share of caustic photons whose flight is kept as a beam (flux scaled up)
    int gatherDepth;                 // gather caustics at the first n non-delta vertices

    // ---- toggles (debug / measurement)
    int enablePhotons;
    int enableHaze;
    int enableNee;
    int enableSunPatch;              // connect shading points to sunlit patches (secondary light sampling)
    int maxDepth;
    int furnace;
    int debugFlags;                  // cost experiments: 1 plain y surface, 2 no central ray, 4 patch trace only, 8 no SER                     // white furnace test: every surface white diffuse, uniform sky = 1
    unsigned int nanCount_pad;
    unsigned int* nanCounter;
};
