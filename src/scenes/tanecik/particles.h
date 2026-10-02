// Scene 4 "Tanecik": the particle world on the GPU (T1). Everything is a particle of one radius (rest distance =
// spacing = 2r), in the manner of NVIDIA FleX (Macklin, Mueller, Chentanez, Kim 2014, "Unified Particle Physics for
// Real-Time Applications"):
//   fluid    Position Based Fluids (Macklin & Mueller 2013): a density constraint per particle, poly6 density and
//            spiky gradient, one-sided (a free surface does not pull), XSPH viscosity, an optional cohesion force
//            (Akinci et al. 2013). Solid particles count in a fluid particle's density and feel its reaction, so
//            water presses on a balloon's skin and the skin holds it (two-way, momentum-conserving).
//   solid    particles of a group keep their partners through BONDS: XPBD distance constraints (compliance, a
//            breaking strain, a crushing strain); unbonded solids of different groups (or of a self-colliding one)
//            touch: kept 2r apart, with friction. A RIGID group is shape-matched (one body; Mueller et al. 2005, the
//            rotation by Mueller et al. 2016). A LIQUEFY group's particle turns liquid once its last bond breaks.
//   planes   the ground (y = 0) and an optional box (an aquarium), with friction.
//   time     many small substeps with one iteration each (Macklin et al. 2019, "Small Steps in Physics Simulation").
// Repeatable: neighbour lists come from a stable sort, every correction is GATHERED by the particle it moves (each
// pair is evaluated from both ends with the same formula; no floating-point atomics), the rigid sums are block
// reductions in a fixed order. The same input gives the same positions bit for bit.
#pragma once
#include "shared/vec.h"

#include <cuda_runtime.h>

#include <cstdint>
#include <vector>

// a group's kind
enum PGroupFlags : uint32_t {
    PG_RIGID = 1u << 0,      // shape-matched: moves as one rigid body
    PG_SELF = 1u << 1,       // its unbonded particles collide with each other
    PG_LIQUEFY = 1u << 2,    // a particle that has lost all its bonds becomes liquid
};

struct PParams {
    float gravity = 9.81f;
    float gravityRamp = 0.0f;    // s: gravity grows from 0 over this long after the start (settling); 0: at once
    int substeps = 20;           // per step
    int iterations = 1;          // per substep
    int divergenceIters = 2;     // velocity divergence passes per substep (0: off)
    int bondIterations = 1;      // of the iterations, how many solve the bonds (XPBD with small steps: one is the recipe)
    float psiMax = 4.0f;         // the largest boundary weight (a thin skin's particle stands for at most this many)
    float kernel = 2.0f;         // the fluid kernel's radius h, in spacings
    float relax = 0.01f;
    float sor = 1.0f;            // the density correction's scale (Jacobi relaxation)         // the density constraint's relaxation, a share of the full lattice's denominator
    bool unilateral = true;      // a fluid particle is pushed apart only when compressed (no pull at a free surface)
    float scorrK = 0.0f;         // tensile-instability correction (Macklin & Mueller 2013): k, n, dq (in h)
    float scorrN = 4.0f, scorrDq = 0.2f;
    float xsph = 0.01f;          // XSPH viscosity per 1/60 s (scaled to the substep)
    float cohesion = 0.0f;       // Akinci et al. 2013 cohesion, m/s^2 at its peak (0: off)
    float tension = 0.0f;        // and its curvature term (surface tension proper), m/s^2 per unit of normal difference
    float muStatic = 0.6f, muDynamic = 0.5f;     // particle-particle (solid contacts)
    float planeMuStatic = 0.6f, planeMuDynamic = 0.5f;   // solids on the ground and the box
    float fluidPlaneMu = 0.3f;   // fluid on the ground and the box (a one-particle film stops; see the puddle test)
    float fluidSolidMu = 0.0f;   // fluid along solid particles (water slips along rubber, skin, a cube)
    float maxSpeed = 60.0f;      // m/s guard
    bool density = true;         // the fluid's density constraint (off: a test shows the volume is not kept)
    bool wallDensity = true;     // walls count in a fluid particle's density as resting particles would
    bool bonds = true;           // the bonds (off: a test shows the balloon does not hold)
    bool box = false;            // an aquarium: the particles stay inside [boxLo, boxHi]
    float3 boxLo = {0, 0, 0}, boxHi = {0, 0, 0};
};

struct PStats {
    int particles = 0, fluid = 0, bonds = 0, broken = 0;
    float densityMean = 0.0f;    // fluid particles with a full neighbourhood: mean of |rho / rho0 - 1|
    float densityMax = 0.0f;     // and the largest compression rho / rho0 - 1 among all fluid particles
    float maxSpeed = 0.0f;
    int neighbourOverflow = 0;   // particles whose neighbour list was full (some neighbours ignored)
    float ms = 0.0f;             // GPU time of the last step
};

class ParticleWorld {
public:
    PParams params;

    // ---- building (host), then commit() uploads it all
    void begin(float spacing);                 // empties the world; spacing = 2 * radius (m)
    int addGroup(uint32_t flags);              // a new group (1, 2, ...); group 0 is free fluid
    // particles at rest: density (kg/m^3) gives each a mass of density * spacing^3; group 0 = fluid. A group's
    // particles must be added in one run (rigid groups are reduced over their contiguous range).
    int addParticles(const std::vector<float3>& pos, float density, int group, const std::vector<float3>* vel = nullptr);
    // bonds between the particles in [first, first + count) closer than maxDist (rest length = their distance now)
    int bondWithin(int first, int count, float maxDist, float compliance, float breakStrain, float crushStrain);
    void commit();
    // ---- running
    void step(float dt, cudaStream_t s = 0);   // one frame: substeps * iterations
    void appendFluid(const std::vector<float3>& pos, const std::vector<float3>& vel, float density);   // pouring (no bonds)
    void placeGroup(int group, float3 offset, float3 velocity);   // a rigid group: its rest shape moved by offset, moving
    int breakBondsInSphere(float3 c, float r);  // every bond with an end inside: broken (a shot, for the tests); returns how many
    // ---- reading
    int count() const { return n_; }
    float spacing() const { return s_; }
    double time() const { return time_; }
    const float4* devicePositions() const { return x_; }   // xyz; device, in particle order
    void exportPositions(float3* dst, cudaStream_t s = 0) const;   // device float3 array (the renderer's spheres)
    void exportDisplay(float* dst, cudaStream_t s = 0) const;      // 0 bonded or solid, 1 free liquid
    void readPositions(std::vector<float3>& out) const;
    void readVelocities(std::vector<float3>& out) const;
    void readDensity(std::vector<float>& out) const;       // rho / rho0 of each fluid particle (0 for solids)
    void readFluid(std::vector<uint8_t>& out) const;       // 1: liquid now
    std::vector<uint8_t> readBroken() const;
    std::vector<int2> readBrokenEnds() const;            // the two particles of every broken bond
    PStats stats();
    float restDensityLattice() const { return S0_; }   // the kernel sum of a full lattice (density units, see .cu)
    void destroy();
    ~ParticleWorld() { destroy(); }

private:
    struct HostBond {
        int a, b;
        float rest, compliance, breakStrain, crushStrain;
    };
    struct HostGroup {
        int first = 0, count = 0;
        uint32_t flags = 0;
    };
    float s_ = 0.007f;
    int n_ = 0, cap_ = 0, nb_ = 0;
    double time_ = 0.0;
    float S0_ = 1.0f, D0_ = 1.0f, fluidInvMass_ = 1.0f;
    // host mirror
    std::vector<float4> hx_, hv_;
    std::vector<float> hw_;
    std::vector<uint32_t> hphase_;
    std::vector<HostBond> hbonds_;
    std::vector<HostGroup> hgroups_;
    // device state, particle order
    float4 *x_ = nullptr, *x0_ = nullptr, *v_ = nullptr, *dx_ = nullptr, *q_ = nullptr;   // q: offset in its rigid body
    float* w_ = nullptr;           // inverse mass
    uint32_t* phase_ = nullptr;    // group (low 24 bits) | PF_FLUID
    float* rho_ = nullptr;         // rho / rho0 (stats, tests)
    uint8_t *contacts_ = nullptr, *contactsNext_ = nullptr;   // contact counts (Jacobi sharing), last substep / this one
    // neighbours
    uint32_t *key_ = nullptr, *keySorted_ = nullptr, *id_ = nullptr, *idSorted_ = nullptr;
    float4* xs_ = nullptr;         // positions in sorted order (w: inverse mass)
    float4* vs_ = nullptr;         // velocities in sorted order (the divergence and viscosity passes)
    float4* ns_ = nullptr;         // fluid surface normals, sorted order (surface tension)
    uint32_t* phs_ = nullptr;      // phases in sorted order
    float *psi_ = nullptr, *psis_ = nullptr;   // boundary weights (Akinci et al. 2012), particle and sorted order
    unsigned long long* code_ = nullptr;   // each sorted slot's cell (hash collisions are told apart by it)
    float* lam_ = nullptr;         // density multipliers, sorted order
    int *cellStart_ = nullptr, *cellEnd_ = nullptr;
    int tableBits_ = 10;
    int* nbr_ = nullptr;           // neighbour slots, kMaxNbr per slot
    int* nbrCount_ = nullptr;
    void* sortTemp_ = nullptr;
    size_t sortTempBytes_ = 0;
    // bonds
    int2* bEnds_ = nullptr;
    float4* bProp_ = nullptr;      // rest, compliance, breakStrain, crushStrain
    float* bLam_ = nullptr;
    float4* bCorr_ = nullptr;      // this pass's correction of end a (b gets the opposite, weighted)
    uint8_t* bBroken_ = nullptr;
    int *csrStart_ = nullptr, *csrBond_ = nullptr;   // per particle: its bonds (bond index, high bit: it is end b)
    int* degree_ = nullptr;        // live bonds now
    // groups
    int ng_ = 0, nRigid_ = 0;
    int* rigidList_ = nullptr;     // the rigid groups
    std::vector<int> colourOffset_;   // bonds by Gauss-Seidel colour: offsets, colours + 1
    int4* groups_ = nullptr;       // first, count, flags, -
    float4* groupFrame_ = nullptr; // per group: centre of mass (xyz), then the rotation as a quaternion (next float4)
    // stats
    float* stat_ = nullptr;
    float2* wallTable_ = nullptr;  // the walls' kernel sum and gradient by distance (see Walls in the .cu)
    cudaEvent_t e0_ = nullptr, e1_ = nullptr;
    float lastMs_ = 0.0f;
    void allocate(int cap, int bondCap);
    void record(float dt, cudaStream_t st);   // one frame's launches (captured into graph_)
    cudaStream_t stream_ = nullptr;
    cudaGraphExec_t graph_ = nullptr;
    float graphDt_ = -1.0f;
    int graphN_ = -1;
    PParams graphP_{};
    float* gSub_ = nullptr;        // gravity per substep (device; the graph reads it)
    void freeAll();
};
