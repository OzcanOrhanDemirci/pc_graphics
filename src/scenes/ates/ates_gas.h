// Scene 12 "Ateş": the gas. A low-Mach reacting flow on a uniform grid over the fire pit, on the GPU: the velocity on
// a staggered (MAC) grid, six scalars at the cells (temperature, the wood's volatiles, carbon monoxide, oxygen, soot and
// the condensed smoke), advected by MacCormack with the extrema clamp, the pressure by multigrid. The flame is not
// drawn by a rule: the volatiles burn where they meet oxygen as fast as they mix (eddy dissipation, NIST FDS's
// engineering model) and only if the mixture can reach the critical flame temperature (FDS's extinction test: smothered
// air does not burn); soot forms on the fuel side and burns at the flame sheet (Lee's oxidation), its light leaves the
// gas as the radiation it is (4 kappa sigma T^4); the heat expands the gas (the divergence the projection takes) and
// lifts it. docs/ates/PLAN.md §3.2.
#pragma once
#include "shared/vec.h"

#include <cuda_runtime.h>

#include <vector>

// the gas's constants (SI); docs/ates/DURUM.md gives each one's source
struct AGasConst {
    float T0 = 288.0f;          // the night's air (K)
    float rhoT = 352.8f;        // rho * T of air at 1 atm (kg K / m^3): rho = rhoT / T
    float Yo2 = 0.232f;         // oxygen in air (mass fraction)
    float g = 9.81f;
    // the wood's volatiles (the pyrolysis gas and tar): heat of combustion, oxygen per kg (Thornton: 13.1 MJ per kg O2)
    float hcF = 14.7e6f, sF = 1.12f;
    // carbon monoxide (the embers' gas): sootless, its own ignition
    float hcC = 10.1e6f, sC = 0.571f;
    float Tcft = 1700.0f;       // critical flame temperature (FDS's default 1427 C)
    float TignF = 700.0f;       // the volatiles burn above this (piloted by the flame's own heat)
    float TignC = 880.0f;       // carbon monoxide's auto-ignition
    float TignSurf = 900.0f;    // a glowing surface this hot lights the volatiles at it (hot-surface ignition)
    float nbMargin = 400.0f;    // a neighbour this much over the ignition temperature lights a cell (the flame front)
    // soot: formation on the fuel side (Arrhenius in T, first order in the volatiles), oxidation by Lee's rate
    float sootA = 1.5e5f, sootTa = 20000.0f;   // 1/s, K (A set so the radiant fraction comes out ~0.2-0.3)
    float sootYield = 1.0f;     // kg soot per kg volatiles consumed by formation (the carbon goes over whole)
    float rhoSoot = 1800.0f;    // kg/m^3
    float sootArea = 1.1e5f;    // m^2 per kg (30 nm spheres)
    // the tar that does not burn condenses into the white smoke under this (its share of the volatiles)
    float Tcond = 480.0f, tarShare = 0.45f, tauCond = 0.15f;
    float cp = 1150.0f;         // J/(kg K), the mixture at flame temperatures
    float mixC = 0.4f;          // tau_mix = min(sqrt(2h/g), mixC / |S|) (the buoyant and the resolved strain's time)
    float wallMix = 1.0f;       // a cell the wood breathes its volatiles into mixes no faster than wallMix h^2 / D (the
                                // laminar boundary layer's molecular diffusion, FDS's tau_d); 0: as any other cell
    float vortEps = 0.6f;       // vorticity confinement (the advection's lost swirl; 0: none)
    float buoy = 1.0f;          // the buoyancy times this (1: physical)
    float buoyLocal = 1.0f;     // 1: g (rho0 - rho) / rho (a free parcel); 0: g (rho0 - rho) / rho0 (Boussinesq, the
                                // constant-density projection's own)
    float smag = 0.2f;          // Smagorinsky's constant (FDS); 0: no sub-grid mixing
    float smagMomentum = 0.0f;  // the eddy viscosity on the velocity too (0: on the scalars only: measured, it laminarized)
    float Sct = 0.5f;           // turbulent Schmidt and Prandtl numbers (FDS)
    float maccormack = 1.0f;    // the scalars' MacCormack correction (0: plain semi-Lagrangian)
    float maxExpand = 2.5f;     // a cell's temperature may grow by this factor a step from its reactions
    float Tad = 2350.0f;        // nor beyond this: the volatiles' adiabatic flame temperature in the night's air
                                // (T0 + hcF Yst / cp: 288 + 14.7 MJ x 0.17 / 1250 J/kg K, ~2290 K; a little room)
};

// what the wood and the hand put into the gas this step, per cell (atomics; zeroed after each step)
struct AGasSources {
    float* fuel;                // kg/s of volatiles
    float* co;                  // kg/s of carbon monoxide
    float* o2;                  // kg/s of oxygen taken (the embers' surface)
    float* heat;                // W into the gas (convection from the surfaces; negative: the gas heats them)
    float* massT;               // kg/s * K of the mass injected (its temperature: the surface's)
    float* ignite;              // the hottest surface touching the cell (K, as int bits: atomicMax)
};

// a jet of air (the person blowing) or a little burner (a match, the test burner): applied each step
struct AGasJet {
    float3 origin{0, 0, 0}, dir{0, 0, 1};
    float speed = 0.0f;         // m/s at the mouth
    float radius = 0.01f;       // the mouth's (m)
    float length = 0.6f;        // how far it reaches before it spreads out (m)
    bool on = false;
};
struct AGasBurner {
    float3 centre{0, 0, 0};     // on the floor (or a point: the match's head)
    float radius = 0.0f;        // m (a disk on the floor; 0: off)
    float power = 0.0f;         // W of the volatiles' heat of combustion (the fuel's mass rate times hcF)
    float temp = 300.0f;        // the gas's temperature as it leaves
    bool point = false;         // a sphere of `radius` at `centre`, not a disk on the floor
};

struct AGasStats {
    float hrr = 0.0f;           // heat release rate (W), last step
    float rad = 0.0f;           // radiated by the soot (W)
    float fuelIn = 0.0f;        // kg/s of fuel injected
    float maxT = 0.0f;
    float residual = 0.0f;      // the projection's last relative residual
    float stepMs = 0.0f;        // GPU time of one step
    int steps = 0;
};

class AtesGas {
public:
    int nx = 0, ny = 0, nz = 0, levels = 0;
    float h = 0.0f;
    float3 origin{0, 0, 0};    // the grid's lower corner (world, m)
    AGasConst k;
    AGasSources src{};
    AGasJet jet;
    AGasBurner burner;
    AGasStats stats;
    int vcycles = 2;
    bool profile = false;       // time each part of the step (sectionMs: cells, velocity, forces, projection, scalars, mixing)
    float sectionMs[6] = {};
    unsigned char* solid = nullptr;   // per cell, 1: inside a log or a stone (written by the wood each step)

    bool init(int nx, int ny, int nz, float h, float3 origin, cudaStream_t stream);
    void reset();               // still air at T0
    // the sources are rates (kg/s, W per cell): written by the wood, they hold for every step until cleared
    void clearSources();
    void step(float dt);
    // the drawing's view of the gas: a half4 3D texture per cell (T, soot volume fraction * 1e6, smoke kg/m^3 * 1e3,
    // the reactions' heat release W/m^3 * 1e-6), refreshed by pack(); and per brick of ATES_BRICK^3 cells what it
    // holds for the eye (0 nothing, 1 hot air only, 2 soot or a flame front; a cell's mark reaches the bricks its
    // trilinear reads touch), so the march skips the empty ones
    void pack(const float3* sootEmit);
    cudaTextureObject_t texture() const { return tex_; }
    const unsigned int* bricks() const { return bricks_; }
    int bnx = 0, bny = 0, bnz = 0;
    // the soot's luminous intensity summed over blocks of `lightDiv`^3 cells (cd, RGB; from the emission table given to
    // pack), for the surfaces' light: refreshed by pack()
    int lightDiv = 4, lnx = 0, lny = 0, lnz = 0;
    const float3* lightIntensity() const { return light_; }
    // the gas's state at the cells (device): T, F, C, O, S, M and the velocity's faces
    float *T = nullptr, *F = nullptr, *C = nullptr, *O = nullptr, *S = nullptr, *M = nullptr;
    float *u = nullptr, *v = nullptr, *w = nullptr;
    float* q = nullptr;         // the reactions' heat release this step (W/m^3)
    float* rad = nullptr;       // the soot's radiated power (W/m^3)
    size_t bytes = 0;
    void release();
    // host copies for the tools: a scalar field (0 T .. 5 M, 6 q, 7 rad), and the vertical velocity at the cells
    std::vector<float> download(int field) const;
    std::vector<float> downloadV() const;

private:
    cudaStream_t st_ = nullptr;
    float *T2_ = nullptr, *F2_ = nullptr, *C2_ = nullptr, *O2_ = nullptr, *S2_ = nullptr, *M2_ = nullptr;   // the hats
    float *T3_ = nullptr, *F3_ = nullptr, *C3_ = nullptr, *O3_ = nullptr, *S3_ = nullptr, *M3_ = nullptr;   // the results
    float4* omega_ = nullptr;   // the vorticity at the cells (confinement)
    float* nut_ = nullptr;      // the eddy viscosity at the cells
    float *u2_ = nullptr, *v2_ = nullptr, *w2_ = nullptr, *u3_ = nullptr, *v3_ = nullptr, *w3_ = nullptr;
    float* div_ = nullptr;      // the expansion (1/s) the projection must leave
    float* wc_ = nullptr;       // per cell T0 / T (+inf in a solid): the finest smoother's faces, 2 / (w_a + w_b)
    std::vector<float*> phi_, rhs_, res_, bx_, by_, bz_;   // the multigrid's levels (bx..: the faces' rho0 / rho)
    std::vector<unsigned char*> msk_;
    std::vector<int3> dims_;
    int bottom_ = 0;            // the first level the one-block bottom solve takes
    float* red_ = nullptr;      // reductions: hrr, rad, fuel, maxT, residual parts
    float3* light_ = nullptr;
    unsigned int* bricks_ = nullptr;
    cudaArray_t arr_ = nullptr;
    cudaTextureObject_t tex_ = 0;
    cudaSurfaceObject_t surf_ = 0;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr;
    cudaEvent_t pe_[7] = {};
    std::vector<void*> owned_;
    float* alloc(size_t n);
    void project(float dt);
    void vcycle(int l);
    void smooth(int l, int iters);
};
