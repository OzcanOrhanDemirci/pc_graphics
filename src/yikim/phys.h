// Scene 3 "Yikim": rigid-element physics on the GPU.
//
// Bodies are convex polyhedra. Neighbouring bodies are glued by INTERFACES, each a handful of FIBRES spread over the
// shared face (concrete area + steel area), in the manner of the Applied Element Method: concrete cracks in tension
// and crushes in compression, steel yields and snaps, shear holds by cohesion and then by friction. Bending capacity,
// plastic hinges and the axial-moment interplay come out of the fibre layout instead of being coded separately.
// Broken interfaces hand the bodies over to contact: points of one hull against the planes of the other.
// Solver: XPBD with small steps (Mueller et al. 2020, Macklin et al. 2019): many substeps, one iteration each;
// interfaces graph-coloured and solved colour by colour (Gauss-Seidel), contacts Jacobi with averaging.
// A kinematic ground (body 0) can shake: the building's foundation fibres hold on to it.
#pragma once
#include "yikim/geom.h"

#include <string>
#include <vector>

struct PhysMaterial {
    float E = 25e9f, nu = 0.2f;       // concrete (Pa)
    float ft = 1.6e6f, fc = 16e6f;    // tensile, compressive strength
    float cohesion = 2.0e6f;          // shear strength of uncracked concrete (Pa)
    float mu = 0.7f;                  // friction across a crack
    float Es = 200e9f, fy = 220e6f;   // steel
    float dowel = 0.5f;               // shear capacity of the bars, share of fy * As
    float openBreak = 0.02f;          // m: an unreinforced crack this wide has separated
    float steelBreak = 0.06f;         // m: plastic elongation at which the bars snap or pull out
    float slipBreak = 0.08f;          // m: plastic slip at which the fibre is gone
    float crushStart = 0.0035f;       // shortening / length at which the concrete is crushed
    float crushBreak = 0.25f;         // shortening / length at which the crushed fibre is gone
    float residual = 0.3f;            // compressive capacity left after crushing
    float damping = 0.0f;             // 1/s: relative velocity across the interface removed per second
};

struct FiberDesc {
    float3 p;          // world point on the shared face
    float ac, as;      // concrete and steel area (m^2)
};

struct InterfaceDesc {
    int a = -1, b = -1;
    float3 n;          // world unit normal from a to b
    float len = 0.5f;  // spring length (m): distance between the two centroids along n
    int mat = 0;
    std::vector<FiberDesc> fibers;
};

struct PhysParams {
    float gravity = 9.81f;
    int substeps = 40;
    float muStatic = 0.7f, muDynamic = 0.6f;
    float restitution = 0.15f;
    float slop = 0.0005f;          // m of penetration tolerated (resting contacts do not chatter)
    float maxDepenetration = 2.0f; // m/s: a deep overlap is resolved at most this fast (spawned or tunnelled bodies)
    float relax = 1.0f;            // relaxation of a body's pairs (Jacobi between pairs)
    int interfaceIters = 2;        // colour passes over the interfaces per substep
    int contactIters = 4;          // per pair: averaged normal pass + patch static friction, repeated
    float margin = 0.05f;          // m: broad-phase box growth
    float maxSpeed = 120.0f;       // m/s guard
};

struct BodyInfo {     // host copy of what the renderer and the tests need
    int tag = 0;
    unsigned color = 0xffffff;
    float mass = 0;
    float3 localLo, localHi;
};

struct PhysStats {
    int bodies = 0, interfaces = 0, fibres = 0, alive = 0, broken = 0, cracked = 0, pairs = 0, contacts = 0, colours = 0;
    float ms = 0;              // GPU time of the last step
};

struct DevWorld;   // device arrays (phys.cu)

class PhysWorld {
public:
    PhysWorld();
    ~PhysWorld();
    void clear();
    // body 0 is always the ground: kinematic, the plane y = its position's y. Returns the new body's index.
    int addBody(const Poly& worldShape, float density, bool kinematic, int tag = 0, unsigned color = 0xb0b0b0);
    int addMaterial(const PhysMaterial& m);
    int addInterface(const InterfaceDesc& d);
    void upload();                 // to the GPU; colours the interface graph
    void step(float dt);           // one frame: broad phase once, then params.substeps substeps
    void download();               // positions, rotations, velocities to the host arrays below
    void settle(float seconds, float ramp, float dt);   // gravity ramped up, then rest; time restarts at 0
    void profile(float dt);        // one frame kernel by kernel (events), printed
    void setGroundMotion(double3 pos) { groundTarget_ = pos; }   // where the ground is at the end of the next step
    void breakBody(int b);         // every interface of the body breaks (interaction)
    void setVelocity(int b, float3 v, float3 w);   // a push (interaction, scenarios)
    // the body is gone (an explosive charge, "notional removal"): its interfaces break, it is parked far below the
    // ground and frozen. Cutting a loaded column's bonds is not enough: wedged under the floors it still carries them
    // by contact and friction (measured: eight cut and kicked columns stayed put under four storeys).
    void removeBody(int b);
    std::vector<float> damage();   // per body: share of its interfaces' fibres cracked or broken (display)
    PhysStats stats();             // counts (reads the fibres back: not every frame)
    // per fibre, in interfaceDescs order: flags (1 cracked, 2 crushed, 4 broken) and last force (interface frame:
    // x normal, >0 tension; y, z shear), N
    void fibres(std::vector<unsigned>& flags, std::vector<float3>& force);
    // contact patches of the last substep: normal force J / h^2 summed over the patches touching body b (N)
    float patchForce(int b);
    struct PatchInfo { int other; float force; float3 n; };
    std::vector<PatchInfo> patchesOf(int b);   // the last substep's contact patches touching body b

    // write the 3x4 object-to-world transform of body (first + i) into an array of OptixInstance (stride bytes)
    void writeTransforms(void* instances, size_t strideBytes, int first, int count, void* stream = nullptr);

    PhysParams params;
    float gravityScale = 1.0f;     // share of params.gravity applied (settle ramps it)
    double time = 0;
    // host state
    std::vector<BodyInfo> info;
    std::vector<Poly> shapes;      // in the body frame (centre of mass at the origin, principal axes)
    std::vector<double3> pos;
    std::vector<float4> rot;
    std::vector<float3> vel, angVel;
    std::vector<float> invMass;
    std::vector<float3> invInertia;
    std::vector<unsigned> kinematic;
    std::vector<PhysMaterial> materials;
    std::vector<InterfaceDesc> interfaceDescs;
    float lastMs = 0;

private:
    DevWorld* d_ = nullptr;
    bool uploaded_ = false;
    int lastPairs_ = 0;
    int frame_ = 0;
    float gravityNow_ = 9.81f;
    float lastH_ = 0.0f;
    std::vector<char> graphKey_;
    std::vector<double3> groundSteps_;
    double3 groundTarget_ = {0, 0, 0}, groundPrev_ = {0, 0, 0};
    std::vector<std::vector<int>> colours_;
    std::vector<int> colourOffset_;
    void solveInterfaces(float h, void* stream);
    void freeDevice();
};
