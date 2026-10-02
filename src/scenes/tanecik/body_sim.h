// Scene 4 "Tanecik": the body in the particle solver (T2). The bone particles are kinematic: they follow the skeleton
// (SkeletonSim's bones, their rigid transforms), nothing pushes them. Tissue and skin are bonded as the body graph says
// (to each other within a bone and across its joints, to their own bone; the skin's membrane), each bone's flesh a group
// of its own (so an arm's flesh meets the chest's instead of passing through it), and a particle that has lost all its
// bonds turns liquid: the blood. What a shot or a blast gives the flesh, the skeleton gets too (the parent adds it to the
// bone: the flesh here is carried by its bone, its mass counted in the bone's).
#pragma once
#include "core/config.h"
#include "scenes/tanecik/body_graph.h"
#include "scenes/tanecik/particles.h"

#include <cuda_runtime.h>

#include <vector>

class BodySim {
public:
    ParticleWorld world;
    // the body and its graph are reordered in place (the bones' particles first, then each bone's flesh): the solver's
    // groups are contiguous runs, and every index the renderer and the skin use stays the solver's
    bool build(BodyParticles& body, BodyGraph& graph, const Config& cfg);
    // the bone particles towards where the skeleton's bones put them, over the next step of dt
    void drive(const Xform xf[B_COUNT], float dt);
    void step(float dt) { world.step(dt); }
    int boneParticles() const { return nBone_; }
    float fleshParticleMass() const { return fleshMass_; }
    void destroy();
    ~BodySim() { destroy(); }

private:
    int nBone_ = 0;
    float fleshMass_ = 0.0f;
    std::vector<float3> rest_;          // the bone particles at rest
    std::vector<uint8_t> restBone_;
    std::vector<float3> target_;
    uint32_t* dIdx_ = nullptr;
    float3* dTarget_ = nullptr;
};
