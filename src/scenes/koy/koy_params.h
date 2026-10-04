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

#define KOY_MAX_MESHES 128
#define KOY_FIELDS 8             // the light field for up to this many floor depths
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
#define PT_TORCH0 10             // 3 rows: Lambert basis k lit by the diver's torch at 1 m, after L metres of water (/pi)
#define PT_AIRSUN0 13            // 3 rows: a Lambert surface in AIR (basis k) lit by the sun at normal incidence, its light
                                 // entering the water (n^2) and crossing L metres of it (/pi): the pier, the island seen
                                 // through Snell's window
#define PT_AIRSKY0 16            // 3 rows: the same lit by the sky's diffuse light on a horizontal plane
#define PT_ROWS 19
// ambSurf[k]: 3D (path length t: AS_T, normal's vertical component: AS_N, depth: AS_Z): diffuse light of the water on a
// Lambert surface of basis k, seen after t metres; ambPath: 3D (t, view direction's mu: AP_MU, depth of the ray's start):
// the in-scattered diffuse light along a view ray; reflPath: 3D (t2, mu2, ts): the same for a ray that starts at the
// surface (a total internal reflection) and is seen after a further ts metres.
#define AS_T 64
#define AS_N 17
#define AS_Z 96
#define AP_T 64
#define AP_MU 48
#define AP_Z 96
#define RP_T 64
#define RP_MU 48
#define RP_TS 32
#define AMB_TMAX 200.0f          // t spans 0 .. AMB_TMAX (squared spacing: fine near)
#define AMB_ZMAX 120.0f          // depth spans 0 .. AMB_ZMAX (Ada: the slope goes on past the oxygen limit, ~66 m)
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
    int detailSet;       // a tiling detail layer laid over in world space (close-up grain), -1 none
    float detailTile;    // metres a tile of it covers
    float detailNormal;  // how strongly its normal map bends the surface
    float detailAlbedo;  // how strongly its colour variation (around its own mean) modulates the albedo
    // Amfora: what the sea laid over it (docs/koy/AMFORA_KAYNAK.md §3.3): a concretion scan in patches (a noise mask in
    // world space, denser in recesses), a felt of fine sediment on the faces looking up, a thin greenish film on the
    // sides, and a slow mottling of the colour (no two jars, no two metres of rock alike)
    int coatSet;         // -1 none
    float coatTile;      // metres a tile of it covers
    float coatCover;     // share of the surface it covers (0..1), before recesses and facing
    float coatScale;     // metres: the size of its patches
    float coatNormal;    // how strongly its normal map bends the surface
    float3 coatTint;     // its colour (linear sRGB; the scan's variation around its mean on top)
    float sediment;      // the felt on upward faces (0..1)
    float3 sedimentColor;
    float film;          // the film on the sides (0..1), towards filmColor
    float3 filmColor;
    float mottle;        // slow colour variation (0..1)
    float life;          // small living patches on the concretion (0..1): sponge (lifeA), coralline crust (lifeB)
    float3 lifeA, lifeB;
    // Ada: the tide's stain on what stands in the water: below stainTop (metres, world y) the albedo times stainDark,
    // blending over stainBand (the wet, dark band of algae and barnacles at the waterline); stainDark 0: none
    float stainTop, stainBand, stainDark;
};

struct KMesh {
    const float3* pos;
    const float3* nrm;
    const float2* uv;
    const uint3* idx;
    const float* ao;     // per vertex, baked (1 open .. 0 closed), may be null; per instance when instanced:
    unsigned int aoFirst, aoVerts;   // instance i's values at ao + (i - aoFirst) * aoVerts (its instances consecutive)
    unsigned int material;
    unsigned int id;     // edge detection: a different surface (a different rock, the floor); per instance in the hit
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
    float3 meanAlbedo;   // linear, the whole texture's average (a detail layer modulates around it)
};

// A sun map (Ada), over a rectangle of the water's surface: (1) how far the refracted sun's light gets down from each
// point before something under the water stops it (the piles), point sampled, the comparison filtered by hand; (2) how
// much of it the things above the water let through (the deck's boards and gaps), at half the texel, mipmapped: a point
// reads it blurred as wide as the sun's disc is at its depth (the stripes under the pier are as soft as the real ones).
#define KOY_SUNMAPS 4
struct KSunMap {
    KTex tex;            // depth: float, unnormalised coordinates, point sampled
    KTex air;            // air transmission: uchar normalised, mipmapped, normalised coordinates
    float2 min;          // x, z of the rectangle's corner
    float texel;         // metres (depth); the air map's texel is half of it
    int w, h;            // depth texels
    float airLevels;
    // (3) how open the sky is straight above (the deck's footprint), at twice the texel, mipmapped: the water's diffuse
    // light comes down through Snell's window, a cone of 48.6 degrees, so a point d metres down reads it averaged over a
    // disc about d wide: under the head the light dims, a hall of shade (Ozcan's reference)
    KTex sky;
    float skyLevels;
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
    // the host's sizeof(KoyParams) and sizeof(KMaterial): the device program is compiled from source at run time
    // (NVRTC), so a stale executable with a changed struct reads every field shifted, silently. The final pass compares
    // them with its own and paints the frame magenta if they differ (2026-10-04: a measurement copy did just that)
    unsigned int layoutParams, layoutMaterial;

    // ---- scene
    OptixTraversableHandle handle;
    KMesh meshes[KOY_MAX_MESHES];
    const KInstance* instances;
    const KMaterial* materials;
    const KTextureSet* texSets;
    // the sand ripples (analytic, band-limited by the footprint): crests along the depth contours round the island,
    // wavelength rippleLength, height rippleHeight in the shallows, gone by rippleDepth1 (waves do not stir the deep sand)
    int ripples;
    float rippleLength, rippleHeight, rippleDepth0, rippleDepth1;
    float2 islandCentre;                // x, z: the ripples' crests circle it
    KCamera cam;
    float time;
    float farDistance;                  // a ray that meets nothing ends here (the water has swallowed it)

    // ---- light (tables built by koy_optics.cpp)
    KTex pathTab;
    // The water's diffuse light depends on the floor below it (bright sand sends light back up: over the wreck's sand the
    // water column is ~30 % brighter than over the deep). One set of tables per floor depth; a point reads the two that
    // bracket the floor under it, each only where it is lit (above its own floor).
    KTex ambSurf[KOY_FIELDS][3];
    KTex ambPath[KOY_FIELDS];
    KTex reflPath[KOY_FIELDS];
    int fields;
    float fieldBottom[KOY_FIELDS];      // ascending
    KTex bottomMap;                     // the floor's depth under (x, z) over the rectangle bottomMapMin + [0, bottomMapSize]
    float2 bottomMapMin, bottomMapSize; // (0: no map, the deepest field)
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
    // Ada: what stands above the water (the pier, the island) seen through Snell's window and shading the water: the
    // shadow ray goes on through the air; it leaves the water along the sun refracted by the waves there (the shadows
    // sway with them); a face looking down in the air gets upwell x the light falling on the sea
    int airGeometry, shadowSway;
    float upwell;
    // the diver's torch (PLAN_AMFORA §3: the jars' true fired-clay colour close up): on the hand beside the eye, a cone,
    // inverse square, its light crossing the water there and back (the PT_TORCH0 rows: red is gone within a few metres)
    int torch;
    float3 torchPos, torchDir;
    float torchCos0, torchCos1;         // full inside cos0, nothing outside cos1

    // ---- the surface (Tessendorf FFT, periodic) and its caustics
    KTex ocean;                         // float4: height, d/dx, d/dz, slope variance; mipmapped, periodic over oceanPatch
    float oceanPatch;                   // metres
    float oceanTexels;
    float oceanHeightMax;               // bound of |height|
    float slopeVarFloor;                // unresolved capillary roughness (slope variance)
    KTex oceanFine;                     // the ripples: like ocean, the short waves on a small patch of their own (0: off)
    float oceanFinePatch;               // metres
    float oceanFineTexels;
    KTex caustic[4];                    // 3D: x, z (periodic over causticPatch), depth; focusing factor (mean 1), pyramid
    int causticLevels;
    float causticPatch;
    float causticLz0, causticLz1;       // slices spaced in log depth: ln z0 .. ln z1
    int causticSlices;
    int causticFlat;                    // debug: F = 1 everywhere
    KSunMap sunMap[KOY_SUNMAPS];
    int sunMaps;
    float swayMax;                      // metres below the surface the shafts still sway with the waves (deeper: the mean sun)
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
    float tunnel;                       // oxygen toxicity: the field of view closing in (0 none .. 1 a pinhole)
    // the eye's colour adaptation (Ada): the camera is white balanced for daylight above the water; under it the eye
    // adapts part of the way to the light around it (a white plane at the eye's depth), as it does to its brightness
    // (exposure: half). 0 the camera's daylight balance, 1 full (von Kries, in linear sRGB, luminance kept)
    float chromaAdapt;
    // AgX's look (Sobotka's 'punchy': in the log space after its contrast curve, a power and a saturation): the water's
    // long veils flatten a picture; 1, 1: plain AgX
    float lookPower, lookSat;
    float blackout;                     // 0 seeing .. 1 black (the convulsion, the restart)
    float3 xyzToRgb0, xyzToRgb1, xyzToRgb2;   // rows: CIE XYZ -> linear sRGB, white balanced to daylight above the water
    const float* blueNoise;             // 128 x 128, fixed: the dither does not move from frame to frame

    // ---- load-time bakes, and the diver's collision probe
    int bakeMesh, bakeInstance;
    float bakeAORange;
    float* bakeOut;
    float2 bakeMapMin;                  // __raygen__bakeSun: the map being baked
    float bakeMapTexel;
    int bakeMapW, bakeMapAir;           // bakeMapAir: 0 the depth pass, 1 the air pass (to the sun), 2 the sky pass (straight up)
    float3 probeOrigin;                 // __raygen__probe: distances to static geometry in 26 directions from here
};
