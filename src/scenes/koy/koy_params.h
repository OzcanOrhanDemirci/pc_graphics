// Scene 5 "Koy" (under water): launch parameters and records shared by the host and the device programs (koy.cu).
// Not a path tracer: visibility by ray tracing, light as deterministic as it can be (docs/koy/PLAN.md §5). Sharpness and
// steadiness come first: every pixel gets the same fixed sub-pixel samples every frame (no jitter, no history), edges get
// more, and a reconstruction filter a little wider than a pixel resolves them.
//
// Spectral, but not at run time: all the wavelength work (the water's absorption and scattering, the sun's and the sky's
// spectra, the eye's colour matching) is done on the host at 5 nm and baked into tables of CIE XYZ indexed by the length
// of the light's path through the water (koy_optics.cpp). A surface's colour enters through three smooth reflectance
// spectra (a linear sRGB albedo is their weights), so the tables carry one XYZ per basis spectrum.
#pragma once
#include "shared/vec.h"

#if !defined(__CUDACC_RTC__)
#include <optix_types.h>
#endif

typedef unsigned long long KTex;   // cudaTextureObject_t (NVRTC sees it as this too)

#define KOY_MAX_MESHES 32
#define KOY_MAX_BASE 16          // base samples per pixel (1, 4, 8 or 16)
#define KOY_MAX_EDGE 32          // samples of a pixel that sits on an edge, base included

// ---- the baked tables (koy_optics.cpp builds them, koy.cu reads them)
// pathTab: 2D, PT_W columns over the path length L in [0, PT_LMAX] m, one row per quantity, XYZ in .xyz:
#define PT_W 1024
#define PT_LMAX 200.0f
#define PT_SURF0 0               // 3 rows: Lambert reflectance basis k lit by the direct sun after L metres of water (/pi)
#define PT_SHAFT_W 3             // in-scattering source of the direct sun by the water itself (b_w E e^{-cL}), per unit phase
#define PT_SHAFT_P 4             // ... by the particles (b_p E e^{-cL})
#define PT_SKY0 5                // 3 rows: the sky's spectral basis j entering the water (n^2), after L metres
#define PT_SUN_AIR 8             // the sun's irradiance in air (on a plane facing it) after L metres of water (glints)
#define PT_TRANS 9               // the transmission of a flat white spectrum (grey reference), x: luminance-weighted e^{-cL}
#define PT_ROWS 10
// ambSurf[k]: 3D (path length t: AS_T, normal's vertical component: AS_N, depth: AS_Z): diffuse light of the water on a
// Lambert surface of basis k, seen after t metres; ambPath: 3D (t, view direction's mu: AP_MU, depth of the ray's start):
// the in-scattered diffuse light along a view ray; reflPath: 3D (t2, mu2, ts): the same for a ray that starts at the
// surface (a total internal reflection) and is seen after a further ts metres.
#define AS_T 64
#define AS_N 17
#define AS_Z 64
#define AP_T 64
#define AP_MU 48
#define AP_Z 64
#define RP_T 64
#define RP_MU 48
#define RP_TS 32
#define AMB_TMAX 200.0f          // t spans 0 .. AMB_TMAX (squared spacing: fine near)
#define AMB_ZMAX 40.0f           // depth spans 0 .. AMB_ZMAX
#define RP_TSMAX 60.0f

enum KoyMaterialKind : int {
    KM_TERRAIN = 0,      // the sea floor: a scanned texture set mapped in world x, z
    KM_MESH = 1,         // a mesh with its own uv (a scanned rock)
    KM_PLAIN = 2,        // a plain albedo
    KM_LEAF = 3,         // a seagrass ribbon
};

struct KMaterial {
    float3 albedo;       // linear sRGB: the colour without a texture, a tint with one
    int kind;
    int texSet;          // index into texSets, -1 none
    float uvScale;       // terrain: texture tiles per metre
    float normalStrength;
    float aoStrength;
};

struct KMesh {
    const float3* pos;
    const float3* nrm;
    const float2* uv;
    const uint3* idx;
    const float* ao;     // per vertex, baked (1 open .. 0 closed), may be null
    unsigned int material;
    unsigned int id;     // edge detection: a different surface (a different rock, the floor)
};

struct KInstance {       // object-to-world 3x4 rows, and the mesh
    float4 r0, r1, r2;
    int mesh;
    int pad[3];
};

struct KTextureSet {     // a scanned material: albedo (sRGB, decoded on fetch), normal (OpenGL), roughness; mipmapped
    KTex albedo, normal, rough;
    float texels;        // texels across the texture (LOD from the ray cone)
    int hasNormal;
};

struct KCamera {
    float3 pos, right, up, fwd;   // orthonormal
    float tanHalfX, tanHalfY;     // the pinhole inside the mask (in air), or in water when portN == 1
    float portN;                  // flat mask port: water's index over air's (1.333); 1: no port
    float pixelAngle;             // radians a pixel subtends in water at the centre (ray cones)
};

struct KoyParams {
    // ---- frames
    unsigned int width, height;
    int baseSpp, edgeSpp;               // samples a pixel (base), and an edge pixel in all (base included)
    float2 basePattern[KOY_MAX_BASE];   // fixed sub-pixel positions in [0,1)^2, the same every frame
    float2 edgePattern[KOY_MAX_EDGE];   // the first baseSpp equal basePattern
    float2 patternShift;                // the reference: the whole pattern shifted (pixels), frames averaged; live: 0
    float4* samples;                    // base samples [pixel * baseSpp + s]: XYZ, w: the surface's id
    float4* edgeAcc;                    // edge pixels: the extra samples already filtered towards the 3x3 neighbours [list * 9 + j]
    unsigned int* edgeIndex;            // pixel -> its place in the edge list + 1 (0: not an edge)
    unsigned int* edgeList;
    unsigned int* edgeCount;
    unsigned int edgeCapacity;
    float edgeContrast;                 // base samples differing this much (in display terms) make an edge pixel
    float edgeIdShare;                  // ... or this share of it when they hit different surfaces
    float filterW[3][3];                // filter weight sums per neighbour offset: base samples (per sample normalised) ...
    float filterWEdge[3][3];            // ... and the extra samples of an edge pixel (host computed from the patterns)
    int filter;                         // 0 box (inside the pixel), 1 Blackman-Harris, 2 Mitchell-Netravali, 3 Gaussian
    float filterRadius;                 // pixels (<= 1.5: the 3x3 neighbourhood holds every sample that counts)
    float4* hdr;                        // resolved scene-referred linear sRGB
    uchar4* output;
    unsigned int frameIndex;

    // ---- scene
    OptixTraversableHandle handle;
    KMesh meshes[KOY_MAX_MESHES];
    const KInstance* instances;
    const KMaterial* materials;
    const KTextureSet* texSets;
    KTex rippleTex;                     // the sand ripples' normal map in world x, z over +-rippleHalf (0: none)
    float rippleHalf;
    KCamera cam;
    float time;
    float farDistance;                  // a ray that meets nothing ends here (the water has swallowed it)

    // ---- light (tables built by koy_optics.cpp)
    KTex pathTab;
    KTex ambSurf[3];
    KTex ambPath;
    KTex reflPath;
    float basisFix[3][3];               // linear sRGB albedo -> weights of the three basis spectra (exact round trip in daylight)
    float3 sunWater;                    // towards the sun, refracted (in water)
    float3 sunAir;                      // towards the sun, in air
    float sunCosW;                      // cosine of the refracted sun with the vertical
    float sunRadiusAir;                 // angular radius of the sun's disc (rad)
    float waterN;                       // 1.333
    float phaseB;                       // backscatter fraction of the particles (Fournier-Forand)
    float ffNu, ffDelta180;             // Fournier-Forand parameters derived from phaseB (host)
    float ffScale;                      // delta-M renormalisation of the phase outside the sun-disc cone
    float skyY;                         // sky luminance scale of the analytic sky (the basis rows carry the spectra)
    float perez[5];                     // Perez sky shape A..E
    float perezNorm;                    // 1 / F(zenith)
    float coastHeight;                  // sine of the cove's rim's elevation above the horizon (the rocky shore in Snell's window)

    // ---- the surface (Tessendorf FFT, periodic) and its caustics
    KTex ocean;                         // float4: height, d/dx, d/dz, slope variance; mipmapped, periodic over oceanPatch
    float oceanPatch;                   // metres
    float oceanTexels;
    float oceanHeightMax;               // bound of |height|
    float slopeVarFloor;                // unresolved capillary roughness (slope variance)
    KTex caustic[4];                    // 3D: x, z (periodic over causticPatch), depth; focusing factor (mean 1), pyramid
    int causticLevels;
    float causticPatch;
    float causticLz0, causticLz1;       // slices spaced in log depth: ln z0 .. ln z1
    int causticSlices;
    int causticFlat;                    // debug: F = 1 everywhere
    KTex sunVis;                        // 3D sun visibility in the test area (1 lit .. 0 shadowed), world box below
    float3 sunVisMin, sunVisSize;
    float shaftStep0, shaftStepGrow;    // in-scattering march: first step (m), growth per metre travelled
    float shaftMax;                     // metres
    // The sun's single scattering along view rays, marched once per 2x2 pixels at fixed distances ("knots", spaced
    // quadratically up to shaftMax) into a 3D texture: (block x, block y, knot). A sample reads its own distance with one
    // filtered fetch (bilinear across blocks, linear between knots): every sample, base or edge, at its exact distance.
    unsigned long long shaftSurf;       // cudaSurfaceObject_t, written by __raygen__shafts
    KTex shaftTex;                      // the same array, read (unnormalised coordinates)
    int shaftBlocksX, shaftBlocksY, shaftKnots;

    // ---- display
    float exposure;                     // scene luminance (Y in our units) -> display
    int view;                           // 0 final, 1 edge pixels, 2 depth, 3 normal, 4 caustic factor, 5 ambient only
    float3 xyzToRgb0, xyzToRgb1, xyzToRgb2;   // rows: CIE XYZ -> linear sRGB, white balanced to daylight above the water
    const float* blueNoise;             // 128 x 128, fixed: the dither does not move from frame to frame

    // ---- load-time bakes
    int bakeMesh, bakeInstance;
    float* bakeOut;
};
