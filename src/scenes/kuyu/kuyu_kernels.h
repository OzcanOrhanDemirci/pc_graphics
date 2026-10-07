// Scene 10 "Kuyu": the CUDA kernels. An analytic tracer on the lattice: a ray walks from cell to cell (3D DDA),
// meeting in each only that cell's room (a glass box) and its twelve bundles (square prisms along the axes, their
// slats a relief on the faces). A ray that meets a room's glass splits as the wall does: its outer reflection goes on
// outside, the rest goes in and bounces between the silver walls, the mirrors and the metal inside until its weight
// is spent. Every surface is exact; every pixel takes the same fixed sub-pixel samples every frame.
#pragma once
#include "scenes/kuyu/kuyu_geom.h"

#include <cuda_runtime.h>

namespace kuyu {

constexpr int kOpticsN = 128;       // the optics tables' samples over cos(theta)
constexpr int kMaxRoomLights = 8;
constexpr int kMaxRoomSpheres = 2;
constexpr int kMaxRoomPanels = 3;
constexpr int kGlowN = 16;          // a variant's mean glow: kGlowN x kGlowN octahedral directions
constexpr int kStripMax = 12;
constexpr int kLtK = 16;            // the structure's light table: room faces kept per texel
constexpr int kShapeN = 16;         // the glow's angular shape over cos
constexpr int kMaxVariants = 24;    // room variants (kept in each block's shared memory while drawing)

// a room's furniture, in the variant's own frame (the room's centre at the origin, walls at +-h)
struct RoomLight {
    float3 p{0, 0, 0};
    float r = 0.02f;                 // the glowing glass bulb
    float3 I{0, 0, 0};               // intensity per channel (W/sr; with the exposure, "candela")
    int wire = 1;                    // hangs from the ceiling on a thin wire
};
struct RoomSphere {
    float3 c{0, 0, 0};               // centre (a pendulum: its rest point)
    float r = 0.2f;
    int metal = 0;                   // 0 chrome, 1 copper
    float swing = 0.0f;              // a pendulum's amplitude (radians), 0: it stands still
    float len = 0.0f;                // its wire's length (the pivot len above the rest point)
    float period = 0.0f;             // seconds
};
struct RoomPanel {                   // a free-standing two-sided mirror (silver on both faces of a thin plate)
    float3 c{0, 0, 0}, n{0, 0, 1}, u{1, 0, 0}, w{0, 1, 0};
    float hu = 0.5f, hw = 0.5f;
};
struct RoomVariant {
    int nL = 0, nS = 0, nP = 0;
    int kind = 0;
    float Imax = 0.0f;               // its brightest bulb's intensity over its radius squared (the cut-off's bound)
    RoomLight L[kMaxRoomLights];
    RoomSphere S[kMaxRoomSpheres];
    RoomPanel P[kMaxRoomPanels];
};

// optics from measured constants (filled on the host, see kuyu_app.cpp), per channel over cos(theta) in air
struct Optics {
    float inR[3][kOpticsN];          // a wall seen from inside: air | silver film | glass
    float outR[3][kOpticsN];         // a wall seen from outside: the antireflection coat, the grey glass both ways, the film
    float T[3][kOpticsN];            // through the wall, either way (the same both ways: reciprocity)
    float copper[3][kOpticsN];       // the frame and the pendulums
    float chrome[3][kOpticsN];
    float gold[3][kOpticsN];         // gold leaf on the threads
    float varnish[kOpticsN];         // a clear coat (n 1.5) on the threads
    float glassN;
};

struct View {
    int width = 0, height = 0;
    float3 camPos{0, 0, 0};          // relative to the centre of the camera's cell
    float3 camRight{1, 0, 0}, camUp{0, 1, 0}, camFwd{0, 0, -1};
    long long camI = 0, camJ = 0, camK = 0;   // the camera's cell
    float tanHalf = 0.6f, pixAngle = 0.0f;
    Lattice g;
    float time = 0.0f;
    const RoomVariant* variants = nullptr;
    int nVariants = 0;
    const float4* glow = nullptr;    // per variant kGlowN^2: the mean radiance looking into it (rgb), in its frame
    const float4* tail = nullptr;    // per variant kGlowN^2: the mean radiance a ray inside collects going on along a
                                     // direction (its frame): the rest of a path past the exact bounces, by its expectation
    int exactBounces = 400;          // bounces traced exactly inside a room before the rest is taken by its mean (tail)
    float interiorWiden = 1.0f;      // the shared rays' footprint widened by (k / reps)^(widen / 2): 1 their share of the pixel, 0 none
    int interiorReps = 1;
    int interiorRepsEdges = 3;       // ... in rooms with spheres or free mirrors (hard edges inside)            // a pixel's samples entering the same room face share this many interior rays, each
                                     // with the footprint widened to their share of the pixel (0: every sample its own)
    const unsigned char* variantOf = nullptr;   // the rooms' variants by hash: a 256-entry table (the mix)
    float exposure = 1.0f, bloom = 0.04f, vignette = 0.2f;
    int samples = 6;
    float coneScale = 0.6f;
    float recon = 0.12f;
    float grazeClamp = 0.05f;
    float minWeight = 1.0f / 255.0f; // inside a room: a path ends when its weight falls under this share of its entry weight
    int maxBounces = 256;
    float cutEps = 1.86e-4f;         // inside a room a path ends once no bulb could still add this much to the exposed
                                     // picture (half a code value at AgX's steepest, see DURUM): bound = w I / (pi R^2)
    float cutEncounters = 4.0f;      // ... times this many bulbs met on the rest of the way
    int maxCells = 160;
    int reflections = 1;             // the glass's outer reflection followed (1) or not (0)
    int lightRooms = 27;             // structure light: rooms around the cell (27, or 8 nearest)
    float3 slatAlbedo[8];            // the threads' colours (book spines)
    int slatFinish[8];               // 0 matte, 1 varnished, 2 gold leaf
    unsigned char threadMap[16];     // a thread's hash -> its colour (the mix)
    int threads = 5;                 // threads to a slat
    int nSlatColors = 6;
    float hazeSigma = 0.0f, hazeAbsorb = 0.0f;
    float3 hazeAmbient{0, 0, 0};     // the mean radiance the mist sees round it (the lattice's own glow)
    float3 frameAlbedo{0.08f, 0.05f, 0.03f};
    // the structure's light, baked once over the bundles of one cell (the lattice repeats; the rooms' colours do not,
    // so each texel keeps the share every room face round it gives, shadows and the glow's angular shape in, and the
    // rooms' glow is multiplied in as the picture is drawn)
    const unsigned char* ltIdx = nullptr;   // per texel kLtK entries: room (27 round the cell) x 6 + face, 255 none
    const float* ltG = nullptr;             // per texel kLtK: that face's share
    const float2* ltFar = nullptr;          // per texel: the rooms beyond (x meanFace), the mist (x its source)
    int ltNa = 0, ltNw = 0;                 // texels along a bundle in one cell, across a face
    const float4* faceGlow = nullptr;       // per variant, its 6 faces (its frame, +x -x +y -y +z -z): the glow leaving
    const float4* faceGlowRest = nullptr;   // the same without the bulbs seen straight (the own room's: exact)
    int ownDirect = 1;                      // the own room's bulbs straight, exactly (0: all in the table, the tools)
    float glowShape[kShapeN];               // the glow's angular shape over cos (the cosine-weighted mean is 1)
    float3 meanFace{0, 0, 0};
    int useTable = 1;
    int prof = 0;                    // the tools: parts switched off to time them (1 table, 2 frame light, 4 relief)
    unsigned int* badCount = nullptr;
    unsigned long long* stats = nullptr;
    int debug = 0;
    float subX = 0.0f, subY = 0.0f;  // the tools: every sample shifted (pixels)
};

void setOptics(const Optics& o);
void buildGlowTable(const View& v, float4* glowOut, int perSide, int skipDirect);
void buildTailTable(const View& v, float4* tailOut, int perSide);
void buildLightTable(const View& v, unsigned char* idx, float* G, float2* far, int samplesSide, int farRays);
void renderFrame(const View& v, float4* hdr, cudaStream_t s);
void resolveFrame(const View& v, const float4* hdr, const float4* bloomHalf, uchar4* out, cudaStream_t s);
void probePixel(const View& v, float px, float py);
// Ö4: the structure's light at P (cell (0, 0, 0), bundle bq's face f, normal n) from the table and by brute force
void o4Point(const View& v, int bq, int f, float3 P, float3 n, int rays, float3& table, float3& brute, int model);
// Ö1: a ray into a room through face (fa, fs); per bounce its closest approach to bulb 0 (x) and where along that piece (y)
void imageTest(const View& v, int var, float3 E, float3 d, int fa, float fs, int maxB, float2* traceHost, float* entryHost);
size_t viewSizeCu();

}   // namespace kuyu
