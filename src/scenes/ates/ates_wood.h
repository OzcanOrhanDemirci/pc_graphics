// Scene 12 "Ateş": the wood. Each log is a cylinder whose side is cut into patches (around x along); each patch is a
// column of layers from its surface to the axis (cylindrical shells), each layer holding its temperature, its wood,
// its char and its water. Heat flows along the column (implicit conduction); the wood dries at 100 C, then pyrolyses by
// three competing reactions (Di Blasi and Branca 2001: gas, tar, char); the volatiles leave through the surface into
// the gas as its fuel; the char at the surface burns with the gas's oxygen as fast as the oxygen can reach it (the
// kinetic rate and the boundary layer's mass transfer in series, the ash's diffusion in front of them), to CO or CO2 by
// Arthur's ratio; the ash it leaves insulates the ember until a draught lifts it. The flame's and the embers' radiation
// reach each patch (from the gas's blocks), the gas's heat by convection (a cylinder's Nusselt number at the local
// speed). Nothing here says "burn", "smother" or "glow": each comes out of these. docs/ates/PLAN.md §3.3.
#pragma once
#include "scenes/ates/ates_gas.h"
#include "scenes/ates/ates_shared.h"
#include "scenes/ates/ates_sparks.h"

#include <array>
#include <cuda_runtime.h>
#include <vector>

#define WOOD_LAYERS 12

// the wood's constants (SI); docs/ates/DURUM.md gives the sources
struct AWoodConst {
    float rhoWood = 550.0f;        // dry wood (kg/m^3: oak 650-750, pine 450-550, beech 700)
    float moisture = 0.10f;        // water per dry wood (air-dried firewood 0.1-0.2)
    float ashInChar = 0.03f;       // ash per char (wood ash ~0.6% of the wood, char a fifth of it)
    float rhoAsh = 40.0f;          // the ash skin on a coal: the char's mineral skeleton (3% of 140 kg/m^3), its
                                   // cells fallen in to a tenth of their volume (settled ash 250-700)
    float ashFall = 90.0f;         // s: the skin flakes off at its thickness over this (thicker, sooner)
    float emissivity = 0.9f;       // char and wood surfaces
    float hPyro = 3.0e5f;          // J/kg, the primary pyrolysis endothermic
    float latent = 2.26e6f;        // J/kg, water
    float charHeatShare = 0.7f;    // the share of the char's oxidation heat that stays in the surface
    float ashDiff = 1.5e-5f;       // oxygen's effective diffusivity through the ash (m^2/s)
    float ashLift = 1.2f;          // gas speed (m/s) over which the draught lifts ash
    float kinA = 3.0e8f, kinTa = 19000.0f;   // the char's apparent surface rate k = A exp(-Ta/T), m/s
    float ambientIn = 390.0f;      // W/m^2 the night (ground, sky) sends to a surface
};

// a log on the host: what it is and where it lies
struct ALog {
    int kind = 1;                  // 0 a twig (kindling), 1 a log
    float r0 = 0.05f, len = 0.4f;  // its first radius and length (m)
    int nTheta = 24, nZ = 32;
    unsigned int firstPatch = 0;
    unsigned int seed = 1;
    // the body (ates_body.h): centre, orientation (a quaternion w x y z), velocities
    float3 pos{0, 0, 0};
    float4 q{1, 0, 0, 0};
    float3 vel{0, 0, 0}, angVel{0, 0, 0};
    bool sleeping = false;
    float sleepTime = 0.0f;        // seconds it has hardly moved
    float wakeRadius = 0.0f;       // its mean radius when the bodies last woke (a log burnt thinner wakes them)
    float mass = 1.0f;             // kg, refreshed from the patches
    float radius = 0.05f;          // the mean radius now (m)
    std::vector<float> ringRadius; // per z ring: the mean surface radius now (m)
    bool alive = true;
    float meshRadiusSum = 0.0f;    // the radii the mesh was built with (it is rebuilt when they drift)
    std::array<float, 12> pose() const;
};

struct AWoodStats {
    float pyrolysis = 0.0f;        // kg/s of volatiles
    float charBurn = 0.0f;         // kg/s of char burnt
    float mass = 0.0f;             // kg of wood and char left
    float hottest = 0.0f;          // the hottest surface (K)
    float emberArea = 0.0f;        // m^2 of surface over 800 K
    float stepMs = 0.0f;
};

class AtesWood {
public:
    AWoodConst k;
    std::vector<ALog> logs;
    AWoodStats stats;
    float bedT = 288.0f;           // the ash bed under the pile (K): a share of the embers' mean (set by the run)
    float bedR = 0.25f;            // its reach (m)
    ASparkSink sparks;             // where its crackles and flakes leave their sparks (ates_sparks.h; none by default)
    unsigned int patches = 0, capacity = 0;
    // device: per patch (surface temperature, char depth, ash thickness: for the eye), per layer
    float *surfT = nullptr, *charD = nullptr, *ash = nullptr, *qrad = nullptr;
    float *T = nullptr, *wood = nullptr, *charM = nullptr, *water = nullptr;   // [patch * WOOD_LAYERS + layer]
    int *outer = nullptr;          // per patch: the first layer still there (the surface recedes inward)
    float* massOut = nullptr;      // per patch: kg left (for the bodies)
    float* radOut = nullptr;       // per patch: its surface radius now (m)
    float* probe = nullptr;        // per patch, when set (debug.wood): its surface's balance, 8 terms (kWoodStep)

    bool init(unsigned int capacity, cudaStream_t st);
    // a new log (its patches set to the night's air temperature, or charred and hot: `burnt` depth of char, surface at
    // `surfaceT`); returns its index
    int add(const ALog& proto, float burnt = 0.0f, float surfaceT = 288.0f);
    // the wood's step: dtWood of its own time (the time scale's), the gas read and written at the gas's real rates;
    // radiation from the gas's blocks (W per block, the soot's) and the hot surfaces themselves
    void step(float dtWood, float timeScale, AtesGas& gas);
    // the logs' cells in the gas as solids (logs thicker than a cell; twigs are porous to it)
    void markSolids(AtesGas& gas);
    // the embers' light into the gas's light blocks (cd, RGB; `grey` the black body's table)
    void emberLight(const AtesGas& gas, float3* light, int lx, int ly, int lz, float lightCell, const float3* grey);
    // the per-log radii and masses back to the host (for the bodies and the meshes)
    void readBack();
    std::vector<ALogView> views() const;
    void release();

private:
    cudaStream_t st_ = nullptr;
    float* logPose_ = nullptr;     // per log: 3x4 (device)
    int* patchLog_ = nullptr;      // per patch: its log
    float* logGeom_ = nullptr;     // per log: r0, len, nTheta, nZ, kind
    float* radBlocks_ = nullptr;   // the radiation's blocks (W)
    int rbx_ = 0, rby_ = 0, rbz_ = 0;
    std::vector<void*> owned_;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr;
};

// a log's mesh in its own space (the axis along x, centred): the side follows each patch's surface radius
struct ALogMesh;
void buildLogMesh(const ALog& L, const std::vector<float>& patchRadius, ALogMesh& out);
