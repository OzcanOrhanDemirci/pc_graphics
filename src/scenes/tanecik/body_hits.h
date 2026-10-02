// Scene 4 "Tanecik": what the weapons do to the body (T4). A shot: the ray's media read from the particles near it
// (bone, tissue, air), the bullet marched through them (weapons.h: drag and strength, calibrated to 10% gelatin), and
// each step's loss handed to the particles: the ones in its path take its momentum along the ray and lose all their bonds
// (the permanent wound: they turn liquid, the blood), the ones out to the temporary cavity's radius are thrown outward
// (a symmetric flow: no net momentum; the bonds' strain decides what tears). The momentum that reached the body goes to
// the skeleton too, at the bones it was handed to (the flesh is carried by its bone).
#pragma once
#include "scenes/tanecik/body_graph.h"
#include "scenes/tanecik/body_sim.h"
#include "scenes/tanecik/skeleton_sim.h"
#include "scenes/tanecik/weapons.h"

struct HitReport {
    int crushed = 0;              // particles in the bullet's path (bonds broken)
    int thrown = 0;               // particles given the cavity's outward flow
    int bondsBroken = 0;
    double pDeposited = 0;        // what the bullet handed over (N s)
    double pApplied = 0;          // what the particles and bones took along the ray (N s): equals the above, but for what
                                  // the last steps (past the last particle) held
    float tissue = 0;             // m of tissue and bone crossed
    bool exited = false;
    float exitSpeed = 0;
    float3 entry{};               // the first point it touched the body
};

// one projectile from origin along dir (unit); skel may be null (no skeleton to push). cavityShare: the share of each
// step's energy the radial flow carries (weapons::radialSpeed)
HitReport shootBody(BodySim& body, const BodyGraph& g, const weapons::Projectile& p, float3 origin, float3 dir, SkeletonSim* skel,
                    float cavityShare = 0.8f);
