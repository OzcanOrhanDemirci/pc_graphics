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
    BK_ATTACH = 2,   // tissue or skin to its own bone
    BK_LOOSE = 3     // any other pair closer than the contact distance at rest (the jittered lattice puts some there): weak
                     // and quick to break, else the solver's contact would push them apart against their neighbours' bonds
                     // (measured: 132 particles shaking at up to 0.4 m/s in a body at rest)
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

// a bone's placement: 3x4 row-major (rotation | translation), OptiX's instance layout; at rest the identity
struct Xform {
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float3 apply(float3 p) const {
        return make_float3(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
    }
    float3 rotate(float3 v) const {
        return make_float3(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z, m[8] * v.x + m[9] * v.y + m[10] * v.z);
    }
    Xform operator*(const Xform& b) const;                     // this after b
    static Xform turnAbout(float3 pivot, float3 axisAngle);   // a rotation (axis * angle, radians) about a point
};

// forward kinematics from the rest pose: each bone turned about its joint's pivot by an axis-angle (rest-world axes),
// relative to its parent; the pelvis placed by root. Parents before children.
void forwardKinematics(const BodyGraph& g, const float3 turn[B_COUNT], const Xform& root, Xform out[B_COUNT]);

// the skin mesh rides on the particles: each vertex on (up to) four skin particles near it, weighted by closeness, and
// an offset (the vertex minus their weighted centre at rest) that turns with their bones
struct SkinBind {
    uint32_t p[4];
    float w[4];
    float3 offset, normal;            // at rest
};
std::vector<SkinBind> bindSkin(const TriMesh& skin, const BodyParticles& body, const BodyGraph& g);

bool bonesAdjacent(int a, int b);   // the same bone, or parent and child
// bonds between particles closer than bondRadius; a bone thinner than minBoneRadius takes the particles within that
// radius of its axis (else a finger bone between lattice points would be empty)
BodyGraph buildBodyGraph(const BodyParticles& body, float bondRadius, float minBoneRadius, float contactDistance = 0.0f);
