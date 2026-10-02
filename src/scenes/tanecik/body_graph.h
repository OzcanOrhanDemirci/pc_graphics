// Scene 4 "Tanecik": the body as the physics will see it, at rest. Every particle is tissue, skin (the outermost
// layer) or bone (inside a bone of the skeleton, and then of that bone: the bone's rigid cluster); neighbours are
// bonded by kind; adjacent bones meet at joints with limits. Plain data: the solver takes it from here (T2).
#pragma once
#include "scenes/tanecik/body.h"

#include <cstdint>
#include <vector>

enum PartKind : uint8_t { PK_TISSUE = 0, PK_SKIN = 1, PK_BONE = 2 };
enum BondKind : uint8_t {
    BK_TISSUE = 0,   // tissue to tissue (or to skin), within a bone or across a joint
    BK_SKIN = 1,     // skin to skin: the membrane
    BK_ATTACH = 2    // tissue or skin to its own bone
};

struct BodyBond {
    uint32_t a, b;
    float rest;
    uint8_t kind;
};

// how two bones meet: a ball (a cone of swing about the child's rest direction, a range of twist about it) or a hinge
// (one axis; positive angles flex: the elbow brings the hand forward, the knee takes the foot back)
struct JointDef {
    int parent = -1, child = -1;
    float3 pivot{};
    bool hinge = false;
    float3 axis{};          // hinge: the axis; ball: the child's rest direction (the twist axis)
    float lo = 0, hi = 0;   // hinge: angle range; ball: twist range (radians)
    float swing = 0;        // ball: the cone's half-angle (radians)
};

struct BodyGraph {
    std::vector<uint8_t> kind;                          // PartKind, per particle
    std::vector<uint8_t> bone;                          // per particle; a bone particle carries the bone it lies in
    std::vector<BodyBond> bonds;
    std::vector<std::vector<uint32_t>> boneParticles;   // per bone: its rigid cluster
    std::vector<JointDef> joints;
    size_t countKind(PartKind k) const;
    size_t countBonds(BondKind k) const;
};

bool bonesAdjacent(int a, int b);   // the same bone, or parent and child
// bonds between particles closer than bondRadius; a bone thinner than minBoneRadius takes the particles within that
// radius of its axis (else a finger bone between lattice points would be empty)
BodyGraph buildBodyGraph(const BodyParticles& body, float bondRadius, float minBoneRadius);
