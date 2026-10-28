// Scene 12 "Ateş": the logs as rigid bodies (PLAN §3.3, §3.9). A log thrown on falls, strikes, rolls and settles on
// the others; as the fire eats the logs underneath, the ones above sink with them. Each log is its burning shape: a
// cylinder whose radius along its length is the wood's rings' (ates_wood.h), so a log thinned in the middle rests
// lower there. The solver is XPBD with small steps (Mueller et al. 2020, "Detailed rigid body simulation with extended
// position based dynamics"): contacts as position corrections with static friction, then the velocities' dynamic
// friction and restitution. The pit's stones are fixed capsules, the ground its height. A body still for a while
// sleeps (its pose stops changing: no new frames for the eye's structures); a log that has burnt thinner wakes them all.
#pragma once
#include "scenes/ates/ates_wood.h"

#include <vector>

struct ABodyObstacle {
    float3 a{0, 0, 0}, b{0, 0, 0};   // a fixed capsule: its segment and radius (m)
    float r = 0.0f;
};

struct ABodyStats {
    int awake = 0;
    int contacts = 0;
    float ms = 0.0f;
};

class AtesBodies {
public:
    std::vector<ABodyObstacle> stones;
    float (*ground)(float x, float z) = nullptr;
    float muStatic = 0.6f, muDynamic = 0.5f;   // charred wood on charred wood and on earth
    float restitution = 0.15f;
    float maxDepenetration = 0.3f; // m/s
    float rollResist = 0.5f;       // rolling resistance (a share of g)
    float maxSpeed = 6.0f;         // m/s
    int substeps = 16;
    ABodyStats stats;

    // advance the awake bodies by dt (the clock's: the bodies fall in real time whatever the wood's time scale)
    void step(std::vector<ALog>& logs, float dt);
    void wakeAll(std::vector<ALog>& logs);
    // let a new log down from where it is to just over what is under it (the ground, the stones, the logs laid
    // before it): laid so, nothing starts inside anything
    void lower(const std::vector<ALog>& logs, ALog& L) const;
};
