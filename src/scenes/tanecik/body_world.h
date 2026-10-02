// Scene 4 "Tanecik": the world the live window and the clips run: the skeleton with its muscles (for now on a hidden
// stand: the pelvis held, as a shop mannequin's pole holds it, until the balance comes), the body in the particle solver
// following it, the renderer fed from both, and the weapons applied to them.
#pragma once
#include "scenes/tanecik/balance.h"
#include "scenes/tanecik/body_hits.h"
#include "scenes/tanecik/body_sim.h"
#include "scenes/tanecik/tanecik_app.h"
#include "scenes/tanecik/weapons.h"

class BodyWorld {
public:
    explicit BodyWorld(TanecikApp& app) : app_(app) {}
    ~BodyWorld();
    // the mannequin, its graph (reordered for the solver), the solver's body, the skin bound again, the skeleton on its
    // stand; the renderer's upload follows (app.upload) once the renderer is up
    bool build();
    void step(float dt);          // world time: the skeleton, then the flesh it drives
    void feed();                  // the renderer: particles, liquid, skin, bones
    // a gun's shot from origin towards aim (a shotgun: its pellets): the summed report
    HitReport shoot(weapons::Gun g, float power, float3 origin, float3 aim);
    int liquidCount();            // particles turned liquid (the blood out), read back
    struct Wound {
        float3 at, out;            // the hole, and outward
        float age = 0.0f;
    };
    std::vector<Wound> wounds;
    SkeletonSim skel;
    BalanceController muscles;
    BodySim body;
    float physicsMs = 0.0f;

private:
    TanecikApp& app_;
    float* dTint_ = nullptr;
    int sub_ = 20, iters_ = 4;
    unsigned shotSeed_ = 1;
};
