// Scene 9 "Ayna": the CUDA kernels. An analytic ray tracer on the maze's lattice: a ray walks from cell to cell,
// meeting in each only that cell's floor, ceiling, three columns, three slabs (face, window reveal, the mirror's
// glass or the passage's middle plane) and the few objects standing in it. Every surface is exact (planes, circles,
// cylinders): no marching, no bounding volumes, no self-intersection offsets (a surface just left can not be met
// again by construction). Deterministic: every pixel takes the same fixed sub-pixel samples every frame.
#pragma once
#include "scenes/ayna/ayna_geom.h"

#include <cuda_runtime.h>

namespace ayna {

constexpr int kMaxLights = 16;
constexpr int kMaxObjects = 16;     // our sphere (0), the lanterns and the lamps
constexpr int kCellObjects = 4;     // objects listed per cell
constexpr int kOpticsN = 128;       // the optics tables' samples over cos(theta)

// a light: a small glowing glass ball (a lamp) or a spot (our own light, from the slit in the sphere)
struct Light {
    float3 pos{0, 0, 0};
    float radius = 0.03f;            // the glowing ball's radius (the lamp's own size, also its soft shadows)
    float3 color{0, 0, 0};           // intensity per channel (W/sr; with the exposure, "candela")
    float3 axis{0, 0, -1};           // a spot's axis
    float cosOuter = -2.0f, cosInner = -2.0f;   // the spot's cone (cosOuter <= -1: all round)
    int object = -1;                 // the object that is its glass (-1: none, e.g. our spot)
};

// a sphere standing in the maze: ours (chrome) or a lamp's glass
struct Object {
    float3 c{0, 0, 0};
    float r = 0.0f;
    int kind = 0;                    // 0 chrome (us), 1 a lamp (emits: light index in `light`), 2 a brass lantern on its chain
    int light = -1;
};

// a beam fragment in one cell (see ayna_beams.h): the light's image in the cell's own frame and the wedge of
// directions from it that came through the windows
struct BeamNode {
    float2 S{0, 0};                  // the light's image, in the cell's frame (plan)
    float2 a{1, 0}, b{0, 1};         // the wedge's sides from S (unit, counter-clockwise a -> b); unused for a root
    float m00 = 1, m01 = 0, m10 = 0, m11 = 1, tx = 0, tz = 0;   // the cell's frame -> the light's (plan)
    float2 em{0, 0}, ed{1, 0};       // the entry window's middle and direction in the light's frame (the arch check)
    float power = 0.0f;              // a bound on the irradiance it can give (sorting, cutting)
    float iw = 0.0f;                 // its intensity times the mirrors' best reflectance (power = iw / dmin^2)
    int parent = -1;
    int cell = -1;                   // cellIndex
    short light = 0;                 // the light's index in View::light
    unsigned char entryK = 3;        // the cell's edge it came through (3: the light's own cell)
    unsigned char m[3] = {0, 0, 0};  // mirrors crossed, by edge family in the light's frame
    unsigned char high = 0;          // 1: came through a horseshoe's bulge somewhere (only ways above the jambs' top)
};
constexpr int kBins = 5;             // each cell's bounding box in kBins x kBins bins, each with its own node list

// The lamps' light, baked: a lamp stands still, so its light (straight and by every mirror, the same beams) is
// worked out once when it is left, into texels on every surface: the floor and the ceiling (one grid each), each
// slab face (u along the edge, y up), each window's reveal (s round the jambs and the arch, depth into the slab) and
// each column (angle, y). A texel keeps the irradiance and, in w's bits, which lamps it sees straight (for our
// sphere's moving shadow, worked out per pixel).
struct LightMapBlock {
    int type = 0;        // 0 floor, 1 ceiling, 2 face, 3 reveal, 4 column
    int offset = 0;      // first texel
    int w = 0, h = 0;    // texels
    int fam = 0, i = 0, j = 0, side = 1;   // faces and reveals: the edge and the side's cell; columns: i, j the vertex
    float ts = 0.01f;    // its texel's size (m): finer on the reveals and columns, seen from a hand's breadth away
};
struct LightMap {
    float texel = 0.01f;
    float x0 = 0, z0 = 0;            // the floor's and ceiling's grid origin
    int gw = 0, gh = 0;              // their size
    int floorOff = 0, ceilOff = 0;
    const int* faceBlock = nullptr;  // per edgeIndex * 2 + (side > 0 ? 0 : 1): block index or -1
    const int* revBlock = nullptr;
    const int* colBlock = nullptr;   // per vertexIndex
    const LightMapBlock* blocks = nullptr;
    float4* E = nullptr;             // rgb irradiance, w the straight-visible lamps' bits
    float4* E2 = nullptr;            // our own light's, baked again every frame (it moves with us), at half resolution:
    const LightMapBlock* blocks2 = nullptr;   //   its blocks (each texel twice the size; the same order)
    int gw2 = 0, gh2 = 0, floorOff2 = 0, ceilOff2 = 0;
    float perimeter = 1.0f;          // a reveal's length round the window
    int on = 0;
};
constexpr int kMaxLamps = 12;       // the brass lanterns first, then the lamps left behind

// optics from measured constants (filled on the host, see ayna_app.cpp): per channel, over cos(theta) at the
// glass's front surface: the mirror's main reflection (Fresnel in and out of the glass, silver behind it, the glass's
// absorption both ways), the front surface's own (the first ghost); our chrome; the polished stone's Fresnel
struct Optics {
    float mirror[3][kOpticsN];
    float ghost[kOpticsN];
    float chrome[3][kOpticsN];
    float stone[kOpticsN];
    float glassN;
};

struct View {
    int width = 0, height = 0;
    float3 camPos{0, 0, 0}, camRight{1, 0, 0}, camUp{0, 1, 0}, camFwd{0, 0, -1};
    float tanHalf = 0.6f, pixAngle = 0.0f;
    Lattice g;
    const unsigned char* edges = nullptr;     // device: edge types (edgeIndex)
    const unsigned char* columns = nullptr;   // device: columns (vertexIndex)
    const signed char* cellObj = nullptr;     // device: per cell kCellObjects object ids (-1 empty)
    int nLights = 0;
    Light light[kMaxLights];
    int nObjects = 0;
    Object obj[kMaxObjects];
    float exposure = 0.12f, bloom = 0.04f, vignette = 0.2f;
    int samples = 4;                 // fixed samples a pixel (1, 4, 8 or 16)
    float minWeight = 1.0f / 1024.0f;   // a path ends when its weight falls under this (and the light it could still carry)
    int maxSteps = 600;              // cells a path may cross
    float3 albedoPlaster{0.62f, 0.56f, 0.46f};
    // the beams: nodes, and per cell the indices of its nodes, strongest first
    const BeamNode* nodes = nullptr;
    const int* cellStart = nullptr;  // cellCount * kBins * kBins + 1 (per cell, per bin)
    const int* cellNodes = nullptr;
    float beamEps = 1e-3f;           // irradiance under which a node's light is not looked for (times the path's weight)
    // the haze: a thin, even mist (scattering and absorption per metre; isotropic). Its glow along a ray comes in
    // closed form from the haze nodes of each cell the ray crosses: all of our light's beams, and the lamps' straight
    // ones (their light by the mirrors lights the mist too: not yet, see DURUM)
    float hazeSigma = 0.0f, hazeAbsorb = 0.0f;
    const int* hazeStart = nullptr;  // cellCount + 1
    const int* hazeNodes = nullptr;  // node indices
    float hazeMinWeight = 0.03f;     // a path's mist glow stops being looked for under this weight
    float4* hazeBuf = nullptr;       // device: the glow at half resolution
    int hazeOnly = 0;                // the tools: only the glow pass
    int plainMat = 0;                // the tools: plain colours instead of the materials (their cost)
    float coneScale = 0.5f;          // the footprint: this many pixels a metre (half: four samples share a pixel)
    float subX = 0.0f, subY = 0.0f;  // the tools: every sample shifted by this (pixels), for references of many renders
    // our light straight in its own cell (and the doorways' far halves) is worked out per pixel, sharp; the rest of its
    // light is in the light map E2
    int spotLight = -1, spotCell = -1;
    int skipRoot = 0;                // the bake of E2: our light's straight light in its own cell is not baked
    int ghosts = 1;                  // the mirrors' front-surface ghosts of the lights, for the path's first this many mirrors
    int ghostCells = 10;             // how far a ghost's ray is followed
    // the tools (Ö1): a mirror plane in front of the eye, met before anything else; 1 ideal (all reflected, no glass),
    // 2 the maze's own mirror (glass and silver); the samples are mirrored with it
    int testMirror = 0;
    float3 tmN{0, 0, 1};
    float tmD = 0.0f;
    LightMap lm;                     // the lamps' baked light
    int nLamps = 0;
    float4 lamp[kMaxLamps];          // xyz the lamp, w its glass's radius (for our sphere's shadow)
    float3 lampI[kMaxLamps];
    int bake = 0;                    // 1 while baking the lamps: no sphere, no light map (2: our light's map: the sphere counts)
    int firstLamp = 0;               // light[firstLamp ..] are the lamps (their glass is an object too)
    int beams = 1;                   // 0: straight light only (shadow rays), the tools' comparison
    unsigned int* badCount = nullptr;
    unsigned long long* stats = nullptr;   // the tools: beam nodes looked at, and lit by (debug 3 / 4)
    unsigned int* oob = nullptr;     // the tools: out-of-range indices met (and skipped)
    int nNodes = 0, nEntries = 0;
    // tools: draw the bounce count instead of the picture (1), or the cell steps (2)
    int debug = 0;
};

void setOptics(const Optics& o);
// bakes the lights in v (their nodes in v.nodes) into v.lm.E, adding; `bit` is or-ed into the texels that see light 0
// straight. nBlocks blocks, the largest maxW x maxH texels.
void bakeLightMap(const View& v, const LightMapBlock* blocksHost, int nBlocks, int maxW, int maxH, unsigned bit, cudaStream_t s);
// our light into v.lm.E2, every frame: written (not added), our sphere's shadows in it
void bakeDynamicMap(const View& v, const LightMapBlock* blocksDev, int nBlocks, int maxW, int maxH, cudaStream_t s);
void renderFrame(const View& v, float4* hdr, cudaStream_t s);
void resolveFrame(const View& v, const float4* hdr, const float4* bloomHalf, uchar4* out, cudaStream_t s);

// one ray through the maze for the tools (Ö2: where the n-th copy lands): the plan points where it met mirrors
int traceMirrorPoints(const View& v, float3 o, float3 d, int maxHits, float3* hitsHost);
void probePixel(const View& v, float px, float py);   // prints the first surface and its light, both ways
// Ö4: photons from a point light into floor bins (device grids gw x gh, bin metres, origin x0 z0), straight and by mirrors
void tracePhotons(const View& v, float3 lp, float3 I, long long total, float* gridDirect, float* gridMirror, float x0, float z0, float bin, int gw,
                  int gh);
size_t viewSizeCu();
size_t viewHazeOffsetCu();

}   // namespace ayna
