// Scene 2 "Fener": launch parameters and scene records shared by the host and the device programs (fener.cu).
// A real-time renderer in Playdead's manner: blue-noise undersampled effects integrated over time (TAA),
// RGB (no spectral rendering: nothing in this scene disperses light).
#pragma once
#include "vec.h"

#if !defined(__CUDACC_RTC__)
#include <optix_types.h>
#endif

#define FENER_MAX_MESHES 64
#define FENER_GLOW_LEVELS 7

enum FenerMaterialKind : int {
    FK_DIFFUSE = 0,      // Lambert: terrain, bark, needles, wood, stone
    FK_LAMP_GLASS = 1,   // the lantern glazing: emits the lamp as seen through it, invisible to shadow rays
    FK_WINDOW = 2,       // the cabin's window pane: warm emitter, invisible to shadow rays
};

struct FMaterial {
    float3 albedo;
    int kind;
    float3 emission;     // linear RGB radiance added on top (window, lantern glow)
    float wrap;          // 0 Lambert, 1 fully wrapped (needles: light leaks through thin foliage)
};

struct FMesh {           // one bottom-level geometry (an island, a tree variant, the lighthouse ...)
    const float3* pos;
    const float3* nrm;   // per vertex
    const uint3* idx;
    const unsigned char* mat;   // material per triangle
};

struct FInstance {       // object-to-world 3x4 (rows) and the mesh it places
    float4 r0, r1, r2;
    int mesh;
    int pad[3];
};

struct FCamera {
    float3 pos, right, up, fwd;  // orthonormal
    float tanHalfX, tanHalfY;
    float2 jitter;               // sub-pixel offset of this frame (pixels), TAA
};

struct FenerParams {
    // ---- frame buffers
    unsigned int width, height;          // display = surface pass size
    unsigned int volWidth, volHeight;    // volumetric pass size
    unsigned int volScale;               // 1 full resolution, 2 half (bilateral upsampling)
    float4* surface;                     // linear RGB of this frame: surfaces * transmittance + ambient fog
    float4* gdepth;                      // x: distance along the primary ray (1e30 sky), yzw: world normal
    float2* motion;                      // pixels to subtract to find this pixel in the previous frame
    float4* volume;                      // half res: lamp + moon single scattering (rgb), w: mean scattering distance
    float4* volumeHist[2];               // temporal accumulation of the volume, ping-pong
    float4* taaHist[2];                  // full-res TAA history, ping-pong
    float4* hdr;                         // resolved linear image (input to glow and the final pass)
    float4* emissive;                    // the bright bits only (narrow glow source)
    const float4* glowWide;              // half-res results of the glow pyramids
    const float4* glowNarrow;
    uchar4* output;
    unsigned int frame;                  // frames since the history was reset
    unsigned int frameIndex;             // ever increasing: noise decorrelation
    int histRead;                        // which ping-pong buffer holds the history
    float time;                          // seconds: waves, beam rotation, drizzle

    // ---- scene
    OptixTraversableHandle handle;
    FMesh meshes[FENER_MAX_MESHES];
    const FInstance* instances;          // indexed by optixGetInstanceIndex
    const FMaterial* materials;
    FCamera cam, prevCam;

    // ---- lights
    float3 moonDir;                      // towards the moon
    float moonCosMax;                    // cos of the drawn disc radius (art: larger than the real 0.26 deg)
    float3 moonE;                        // moonlight irradiance on a perpendicular surface (RGB)
    float3 moonDisc;                     // radiance of the disc as drawn in the sky
    float3 lampPos;
    float lampPhi;                       // beam azimuth (rad): 0 = towards -z
    float lampI;                         // peak intensity of a beam
    float beamW, beamWv, beamTilt;       // azimuth width, elevation width, elevation tilt (rad)
    float spillK, spillWv, spillTilt;    // weak wide downward lobe (art)
    float lampLeak;                      // omnidirectional light of the lantern
    float lampSurfK;                     // art: gain of the lamp's light on surfaces (the fog has its own, volBeamScale)
    float lampGlassBase, lampGlassK;     // lantern glazing radiance: base + K * (intensity towards the viewer / peak)
    float3 lampColor;
    float3 winCorner, winU, winV, winN;  // cabin window: emits along winN
    float3 winRadiance;

    // ---- atmosphere
    float fogRho0, fogH, fogHaze;        // height fog: sigma(y) = rho0 * exp(-y / H) + haze  (1/m)
    float fogGround, fogGroundH;         // ground mist: + ground * exp(-y / groundH)
    float3 fogAmbient;                   // moonlit multiple scattering, radiance of an infinitely deep fog (RGB)
    float fogAmbientHalo, fogAmbientG;   // forward-scattering share and asymmetry of the ambient around the moon
    float3 nightSky;                     // radiance of the clear sky above the fog
    float3 waterDeep;                    // radiance of the water body seen through the surface
    float moonGlintK;                    // art: the moon's path on the sea

    // ---- volume pass
    int volLampSamples, volMoonSamples;
    float volBeamScale, volBeamCap;      // art: in-scattered beam light gain and soft ceiling
    float volMoonScale;
    float beamPhaseG, beamPhaseHalo;     // beam phase: HG(g) mixed with a sharper forward lobe (share)

    // ---- display
    float exposure;
    float glowWideK, glowNarrowK;
    float glowThreshold;                 // exposed luminance where the narrow glow starts
    float vignette;
    float taaAlpha;                      // weight of the new frame in the TAA blend
    const float* blueNoise;              // 128 x 128
    int view;                            // 0 final, 1 surface only, 2 volume only, 3 depth, 4 normals
};
