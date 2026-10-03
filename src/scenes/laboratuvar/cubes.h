// Scene 4 "Laboratuvar", the Küp experiment: a cube built of small, hard, loose plastic cubes (all one size), a charge
// at its centre. Each small cube is a rigid body (a position and an orientation), not a particle:
//   contact    a point of one cube (its corners) inside the other, pushed out through the face it came in by (the
//              face it crossed since the substep began: a lattice's corners lie on its neighbours' edges, and the
//              nearest face would be the wrong one); the table top, the backdrop's sweep and wall the same way
//   solver     position-based rigid bodies (XPBD, Müller et al. 2020, "Detailed rigid body simulation with extended
//              position based dynamics"), one pass a substep, Jacobi with averaging; restitution as an extra push
//              (the separation the bounce needs over the substep), friction from the contact points' slip (static
//              below mu_s times the push, else kinetic)
//   air        quadratic drag on the mean projected area of a tumbling cube (1.5 a^2): a 1 mm cube falls at ~3 m/s,
//              a 25 mm one at ~16 m/s
//   rest       a cube slow for a while sleeps (out of the solver, an obstacle) until something moving touches it;
//              the built cube starts asleep (a stack of cubes stands by itself)
//   blast      Gurney along each direction: the mass in that direction's cone over the charge's (a face's middle
//              flies faster than a corner), the inner cubes faster than the outer, the downward half reflected by
//              the table (the image charge of a burst on a hard floor), a little scatter in speed, direction, spin
#pragma once
#include <cuda_runtime.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

struct CubeParams {
    float density = 1050.0f;            // ABS (kg/m^3)
    float restitution = 0.55f;          // cube on cube
    float restitutionFloor = 0.45f;     // on the table, the backdrop
    float muStatic = 0.45f, muDynamic = 0.35f;
    float muFloorStatic = 0.40f, muFloorDynamic = 0.30f;
    float gravity = 9.81f;
    float airDensity = 1.2f, drag = 1.05f;   // drag coefficient over 1.5 a^2
    float angularDamping = 0.2f;        // 1/s (the air on a spinning cube, roughly)
    float sleepSpeed = 0.02f, sleepTime = 0.3f;
    float restDamping = 0.1f;           // a cube in contact slower than 2 sleepSpeed loses this share a millisecond
    float maxPush = 2.0f;               // m/s: the most a correction may move a cube in a substep (over its length)
    float restSpeed = 0.15f;            // m/s: a contact closing slower does not bounce (a stack's settling)
    float sor = 1.0f;                   // Jacobi over several touching bodies: their mean, over-relaxed by this
    float gap = 0.004f;                 // between neighbours side by side in the built cube (of the edge): real stacked
                                        // cubes are never flush, and flush faces make the solver's noise a push
    int substeps = 20;                  // per 1/60 s: the substep's length
    int passes = 2;                     // Gauss-Seidel passes a substep (the last one-way upwards: shock)
    bool shock = true;                  // the last pass one-way upwards (a stack stands)
    int debug = 0;                      // print the contacts of cube 1 (a test)
    int maxSubdivide = 1;               // a fast step cut finer (a cube moving at most half its edge a substep); contacts
                                        // are speculative (a gap closing this substep is one), so it need not be
    // the stage: the table's top (y = 0), the backdrop's floor, sweep and wall (as lab_geom draws them)
    float tableHalfWidth = 0.75f, tableFront = 0.32f, tableBack = -0.40f;
    float backHalfWidth = 1.2f, backWall = -0.62f, backSweep = 0.18f;
};

struct CubeBlast {
    float3 centre = make_float3(0, 0, 0);
    float charge = 1e-3f;               // grams of TNT-equivalent
    float gurney = 2440.0f;             // m/s: sqrt(2E) of TNT
    float inner = 0.5f;                 // in a direction, speed ~ (r / R)^-inner (the inner cubes pushed hardest)
    float spread = 0.25f;               // log-normal sigma of the speed
    float cone = 0.12f;                 // the direction's scatter (radians, roughly)
    float spin = 0.3f;                  // spin up to this share of the speed over the half edge
    float floorReflect = 0.6f;          // the downward half: its vertical speed reflected, this share of it
    unsigned seed = 7;
};

struct CubeBlastReport {
    double energy = 0.0;                // J in the cubes after the blast (the table's reflection taken)
    double gurneyEnergy = 0.0;          // J: every cube at its direction's Gurney speed
    float meanSpeed = 0.0f, maxSpeed = 0.0f, faceSpeed = 0.0f, cornerSpeed = 0.0f;   // Gurney at a face, a corner
};

struct CubeStats {
    int awake = 0, asleep = 0, dead = 0;
    float maxSpeed = 0.0f;
    double kinetic = 0.0;
};

// a recording's cubes at a time between two stored frames: positions lerped, orientations nlerped, into 2 float4 a
// cube (centre + edge, orientation); a gone cube's edge 0
void launchCubeDequantise(const ushort4* p0, const ushort4* r0, const ushort4* p1, const ushort4* r1, float a, int n, float3 lo, float3 scale, float edge,
                          float4* pose, cudaStream_t st);

class CubeWorld {
public:
    CubeParams params;
    // a cube of n x n x n cubes, side `side`, its bottom face's middle at `base`, colours from the six of a puzzle cube
    void build(int n, float side, float3 base, unsigned seed);
    void destroy();
    ~CubeWorld() { destroy(); }
    int count() const { return n_; }
    int perEdge() const { return per_; }
    float edge() const { return a_; }     // a small cube's
    float side() const { return side_; }  // the whole cube's
    float3 centre() const { return make_float3(base_.x, base_.y + 0.5f * side_, base_.z); }
    double time() const { return time_; }
    int baseSubsteps() const { return std::max(1, params.substeps); }
    void blast(const CubeBlast& b, CubeBlastReport* rep = nullptr);
    // dt of world time in whole substeps of 1/(60 substeps) (rounded, at least one), each cut finer when the fastest
    // cube would move more than half its edge
    void step(float dt);
    float lastStepMs() const { return lastMs_; }
    const CubeStats& lastStats() const { return lastStats_; }   // as the last step began
    int lastSubsteps() const { return lastSubs_; }
    // the drawing: 2 float4 a cube (centre + edge, orientation xyzw), between the state before the last step and now
    void exportPoses(float4* pose, float alpha, cudaStream_t st) const;
    const uchar4* deviceColours() const { return dColour_; }   // sRGB, one a cube
    CubeStats stats() const;
    // a recording's frame: positions in [lo, lo + 65535 scale] (w: 1 gone), orientation snorm16
    void quantise(ushort4* pos, ushort4* rot, float3 lo, float3 scale, cudaStream_t st) const;
    cudaStream_t stream() const { return stream_; }
    // for the tests: every cube awake with this velocity and spin; one cube's pose; read the state back
    void wakeAll(float3 v, float3 w);
    void setPose(int i, float3 x, float4 q);
    void read(std::vector<float4>& x, std::vector<float4>& q, std::vector<float4>& v, std::vector<uint32_t>& state) const;
    // the last substep's narrow phase: how many cubes touched something, how many pairs in all
    void pairStats(int& cubesTouching, int& pairs) const;
    std::string summary() const;

private:
    int n_ = 0, per_ = 0, cap_ = 0, tableBits_ = 10;
    float a_ = 0.0f, side_ = 0.0f;
    float3 base_ = make_float3(0, 0, 0);
    double time_ = 0.0;
    float lastMs_ = 0.0f;
    CubeStats lastStats_;
    int lastSubs_ = 0;
    cudaStream_t stream_ = nullptr;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr;
    // state (by cube), the sorted copies the contact pass reads
    float4 *x_ = nullptr, *xp_ = nullptr, *v_ = nullptr, *w_ = nullptr, *q_ = nullptr, *qp_ = nullptr;
    float4 *x0_ = nullptr, *q0_ = nullptr;   // before the last step (the drawing interpolates from here)
    uint32_t* state_ = nullptr;              // 0 awake, 1 asleep, 2 gone
    uint32_t* wake_ = nullptr;
    float* sleepT_ = nullptr;
    float4 *accX_ = nullptr, *accW_ = nullptr;   // a substep's corrections so far: the move, the turn
    float4 *dvT_ = nullptr, *dwT_ = nullptr;     // and what of them is velocity (times h)
    uint32_t *colKey_ = nullptr, *colKeySorted_ = nullptr, *colId_ = nullptr, *idByColour_ = nullptr;   // Gauss-Seidel's colours
    int *colStart_ = nullptr, *colEnd_ = nullptr;
    uint32_t* pairJ_ = nullptr;                  // the narrow phase's records: kPairs a cube
    float4 *pairU_ = nullptr, *pairP_ = nullptr;
    int* pairN_ = nullptr;
    uint32_t *key_ = nullptr, *keySorted_ = nullptr, *id_ = nullptr, *idSorted_ = nullptr;
    int *cellStart_ = nullptr, *cellEnd_ = nullptr;
    void* sortTemp_ = nullptr;
    size_t sortTempBytes_ = 0;
    uchar4* dColour_ = nullptr;
    float* dRed_ = nullptr;                  // reductions: [0] max speed bits, [1] awake, [2] asleep, [3] gone, [4..5] kinetic
    void substep(float h);
    void record(float h);
    // a substep's launches (a few hundred: the colours) captured once a length and replayed
    struct Graph {
        float hs;
        cudaGraphExec_t exec;
    };
    std::vector<Graph> graphs_;
    CubeParams graphParams_{};
    void dropGraphs();
};
