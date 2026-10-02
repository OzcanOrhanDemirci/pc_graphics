// Scene 4 "Tanecik": the skeleton's physics. The 17 bones are rigid bodies (mass and inertia from their bone particles,
// rest orientation the identity), the 16 joints XPBD constraints (Müller et al. 2020, "Detailed Rigid Body Simulation
// with Extended Position Based Dynamics"): the pivot shared, a hinge's axes aligned, angle limits (a hinge's range; a
// ball's swing cone and twist range). On the GPU, every substep: predict, solve the joints (Gauss-Seidel over the 16,
// one thread: they are few), velocities from positions. The particles' solver couples to it (T2): bone particles
// written from the bodies, what moved them handed back.
#pragma once
#include "scenes/tanecik/body_graph.h"

#include <cuda_runtime.h>

#include <vector>

struct SkelBody {
    float3 x, v, w;            // centre of mass, velocity, angular velocity (world)
    float4 q;                  // orientation (x, y, z, w); rest = identity
    float3 xPrev;
    float4 qPrev;
    float invMass;             // 0: pinned
    float invI[9];             // inverse inertia at rest, world axes (row-major); 0 when pinned
    float I[9];                // the inertia itself (the gyroscopic term)
    float3 x0;                 // the centre of mass at rest
};

struct SkelJoint {
    int a, b;                  // parent, child
    float3 la, lb;             // the pivot in each body's frame (rest: pivot minus centre of mass)
    int hinge;
    float3 axis;               // rest, the same in both frames: hinge axis, or the ball's twist axis (the child's direction)
    float3 ref;                // rest, a unit vector perpendicular to axis (angles are measured from it)
    float lo, hi, swing;       // hinge: range; ball: twist range and swing cone (radians)
    // the muscle: pulls the child's rotation relative to the parent (in the parent's frame; rest = identity) towards
    // target, softly (compliance, rad per N m) and at most this strong (N m); 0: limp
    float4 target;
    float compliance;
    float maxTorque;
};

// the ground under the bones: each body's contact points (rest offsets from its centre of mass), friction
struct SkelGround {
    float radius = 0.0035f;    // a contact point is a particle's centre: it touches at this height
    float muStatic = 0.8f, muDynamic = 0.6f;
    bool on = false;
};

struct SkelStats {
    float maxGap = 0;          // the largest distance between a joint's two pivots (m)
    float maxOver = 0;         // the largest angle past a limit (radians)
    float maxSpeed = 0;        // the fastest body (m/s)
    int fastest = -1;          // which
    float3 L{};                // angular momentum about the centre of mass of the whole (kg m^2/s)
    float energy = 0;          // kinetic + potential (J)
};

class SkeletonSim {
public:
    // bodies from the bone particles (each particleMass kg), joints from the graph's; wholeSegments: every particle of the
    // bone's segment counts (the flesh moving with it: a plain ragdoll, for tests before the flesh has its own physics)
    bool init(const BodyGraph& g, const BodyParticles& body, float particleMass, bool wholeSegments = false);
    void destroy();
    ~SkeletonSim() { destroy(); }
    void pin(int bone, bool on);                         // a pinned bone does not move (infinite mass)
    void setPose(const Xform xf[B_COUNT]);               // every body placed (e.g. from forward kinematics), at rest
    void setVelocity(float3 v, float3 w);                // every body: a linear velocity and a spin about the whole's centre
    // one substep of length h: predict under gravity, solve the joints (iterations), velocities; damping: a share of
    // each joint's relative angular velocity taken out per second (a ragdoll's joints are not frictionless)
    void step(float h, float3 gravity, int iterations, float jointDamping, cudaStream_t s);
    // the ground: contact points from each segment's skin particles (every n-th, the soles all), then on
    void setGround(const BodyGraph& g, const BodyParticles& body, const SkelGround& ground, int every);
    // the muscles: per joint (in the graph's order) a target, a compliance and a strength; copied before the next step
    void setMuscles(const std::vector<float4>& target, const std::vector<float>& compliance, const std::vector<float>& maxTorque);
    void readBodies(std::vector<SkelBody>& out) const;     // the bodies now (a controller reads them every frame)
    void push(int bone, float3 impulse, float3 at);         // an impulse (N s) at a world point of a bone
    float mass(int bone) const { return mass_[bone]; }
    float totalMass() const;
    void transforms(Xform out[B_COUNT]);                  // where each bone is (p = R (p0 - x0) + x): readback
    SkelStats stats(float3 gravity);                      // readback and measure
    const std::vector<SkelJoint>& joints() const { return hJoints_; }
    const std::vector<SkelBody>& restBodies() const { return hRest_; }

private:
    SkelBody* dBodies_ = nullptr;
    SkelJoint* dJoints_ = nullptr;
    std::vector<SkelBody> hRest_;
    std::vector<SkelJoint> hJoints_;
    std::vector<float> mass_;
    int nb_ = 0, nj_ = 0;
    float3* dPts_ = nullptr;        // contact points, rest offsets, by body
    int2* dRange_ = nullptr;        // per body: first, count
    SkelGround ground_;
};
