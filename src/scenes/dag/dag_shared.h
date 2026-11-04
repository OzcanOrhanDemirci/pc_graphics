// Scene 13 "Dağ": what the host and the OptiX programs (device/dag.cu) share. The world is the real ground around
// Kaçkar's summit out to the air's limit of sight, in one frame around the eye (x east, y up, z south, the origin on the
// terrace), the Earth's curve built into the geometry. docs/dag/PLAN.md.
#pragma once
#include "shared/vec.h"

#if !defined(__CUDACC_RTC__)
#include <optix_types.h>
#endif

#define DAG_MAX_SPP 64
#define DAG_CLIPS 3

// what an instance is (its id): the far ground's tiles are below DAG_ID_NEAR
#define DAG_ID_MID 99999u      // the ground out to some tens of kilometres (a polar mesh, like the near one)
#define DAG_ID_NEAR 100000u    // the ground around the terrace (a polar mesh, its normals in a polar texture)
#define DAG_ID_RAIL 100001u    // the railing: its own vertices and normals
#define DAG_ID_STONE 100002u   // first of the stones around the terrace (id - DAG_ID_STONE: which DagGeom)

// where the terrace's scree is large stones and where bare rock shows: the same in the geometry (dag_near.cpp) and the colour
// (device/dag.cu). Smooth noise in 0 .. 1
HD float dagPatchNoise(float x, float y, unsigned int seed) {
    const float xf = floorf(x), yf = floorf(y);
    const int xi = int(xf), yi = int(yf);
    const float fx = x - xf, fy = y - yf;
    const float u = fx * fx * (3.0f - 2.0f * fx), v = fy * fy * (3.0f - 2.0f * fy);
    const auto h = [&](int a, int b) {
        unsigned int q = unsigned(a) * 0x8DA6B343u ^ unsigned(b) * 0xD8163841u ^ seed * 0xCB1AB31Fu;
        q ^= q >> 16;
        q *= 0x7feb352dU;
        q ^= q >> 15;
        q *= 0x846ca68bU;
        q ^= q >> 16;
        return float(q & 0xFFFFu) / 65535.0f;
    };
    return (1.0f - v) * ((1.0f - u) * h(xi, yi) + u * h(xi + 1, yi)) + v * ((1.0f - u) * h(xi, yi + 1) + u * h(xi + 1, yi + 1));
}
// 0 the common scree, 1 large stones
HD float dagBigStones(float x, float z) {
    const float n = 0.65f * dagPatchNoise(x / 3.7f + 11.0f, z / 3.7f, 3u) + 0.35f * dagPatchNoise(x / 1.3f, z / 1.3f + 5.0f, 4u);
    return smoothstepf(0.50f, 0.58f, n);
}
// 1 bare rock
HD float dagBedrock(float x, float z) {
    const float n = 0.7f * dagPatchNoise(x / 5.3f - 3.0f, z / 5.3f + 2.0f, 7u) + 0.3f * dagPatchNoise(x / 1.7f + 9.0f, z / 1.7f, 8u);
    return smoothstepf(0.66f, 0.71f, n);
}

// one level of a clip stack: a square of the azimuthal equidistant plane around the summit
struct DagClip {
    unsigned long long tex;    // cudaTextureObject_t, mipmapped
    float half;                // half its side (m)
    float cell;                // a cell's side (m)
};

// a polar texture around the eye
struct DagPolarTex {
    unsigned long long tex;    // normal x, z (coded), the sky's share
    float r0, logQ;            // r = r0 * exp(v * logQ), v 0..1 along the rings
    float cellAngle;           // a cell's width over the radius
};

struct DagGeom {
    const float3* pos;
    const float3* nrm;
    const float2* uv;
    const uint3* idx;
    float3 tint;
    float rough;
    int material;              // 0 metal, 1 a scanned rock (its textures), 2 galvanized steel
    unsigned long long albedo, normal;   // the scan's (material 1)
    float texelArea;           // texels of the albedo's level 0
};

struct DagParams {
    OptixTraversableHandle handle;
    unsigned int width, height;
    float4* hdr;               // linear radiance, the pixel's samples resolved
    uchar4* output;
    const float4* bloom;       // the glare pyramid's sum (half size), or null

    float3 eye, U, V, W;       // a pixel's ray: W + U * x + V * y, x and y in [-1, 1]
    float pixelAngle;          // radians a pixel (the footprint)
    int spp;
    float2 pattern[DAG_MAX_SPP];
    float2 sunDisk[DAG_MAX_SPP];   // per sample: where on the sun's disk its shadow ray aims (unit disk)

    // the planet: a sphere of radius Re (the air's usual bending built in), its centre under the origin; h0 the origin's altitude
    float Re, h0;
    float nearRadius;          // how far the near mesh reaches (m)

    // the sun
    float3 sunDir;             // towards the sun
    float3 sunE;               // its irradiance above the air
    float sunCos;              // cos of its angular radius
    float sunRad;              // its angular radius (radians)
    float sunFlat;             // how flattened the air makes it, as it sets
    float sunZoomGain;         // the disk's light over the zoom's square

    // the ground
    DagClip albedo[DAG_CLIPS];     // the ground's colour, sRGB read linear (de-lit)
    DagClip info[DAG_CLIPS];       // slope along the plane's x and z, the sky's share, the horizon towards the sun
    float slopeScale;              // the slope's code value 0..1 to slope: (v - 0.5) * this
    float horizonLo, horizonHi;    // the horizon's code value 0..1 to radians
    float albedoGain;              // the mosaic's display values to reflectance
    float albedoGainSnow;          // the snow's reflectance over the rest
    float saturation;              // the display's colour
    float shadowLift;              // the eye's adaptation in the dark: gain 1 + this at black, fading over liftKnee
    float liftKnee;
    float3 white;                  // the display's white balance
    float zoomK;                   // through the binoculars the eye settles on the little contrast there is: display contrast around zoomPivot
    float zoomPivot;

    // the amplified ground
    DagPolarTex polar[2];          // near, mid
    unsigned long long rockAlbedo[3], rockNormal[3];   // the scans: 0 the cliffs from the air (large), 1 scree (close, on the flats), 2 rock face (close, on the steep)
    float rockTile[4];             // metres a tile: the grain's; the cliffs'; the cliffs' again at a larger scale; the grain's as a boulder field
    float rockMean[3];             // the scan's mean reflectance: its colour over this is what it adds to the satellite's
    float rockGain;

    // the air
    unsigned long long transmittance;   // Bruneton's parameterization, the planet at Re
    unsigned long long skyView;         // from the eye: azimuth, elevation (the horizon's dip at the middle)
    unsigned long long froxelS, froxelT;    // from the terrace: azimuth, elevation, log range: light gathered, light let through
    float atmTop;              // the air's top above the sphere (m)
    float eyeDip;              // the horizon's dip from the terrace (radians, positive)
    float froxR0, froxLogK;    // range = froxR0 * exp(w * froxLogK)
    int froxD;                 // slices
    float froxElMax;           // the froxels' elevation span around the dip (radians)
    float3 skySh[4];           // the sky's irradiance as L1 spherical harmonics (L00, then x, y, z), radiance units

    // the sea
    float seaSlope2;           // mean square slope of the waves (Cox-Munk)
    float3 seaColour;          // water-leaving reflectance
    float time;

    // the high thin cloud
    unsigned long long cirrus;  // tiled, one channel, mipmapped
    float cirrusAlt, cirrusTile, cirrusCover, cirrusThick;

    const DagGeom* geoms;       // the railing and the stones
    float4* probe;              // [0] what the middle of the picture looks at: x the distance, y 0 sky, 1 ground, 2 sea; [1] the last sample not finite: pixel x, y, instance, sample

    // the display
    float exposure;
    float bloomStrength;
    float vignette;
    int debug;                 // 0 the picture; 1 albedo, 2 normals, 3 range, 4 no air
    float debugA;
    unsigned int* badCount;

    // the oven: how open the polar meshes are to the sky
    const float3* bakePos;
    float* bakeOut;
    int bakeAz, bakeRings, bakeRing0, bakeRays;
    float bakeReach, bakeReachMax, bakeCellAngle;

    unsigned int layout;       // sizeof(DagParams) on the host
};
