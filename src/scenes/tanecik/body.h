// Scene 4 "Tanecik": the mannequin at rest. Its shape is a signed distance (smooth unions of ellipsoids and round
// cones, ~1.77 m, A-pose, no face); every part of it belongs to one of 17 bones. From it: the particles that fill the
// body (a lattice inside the skin, each tagged with its bone and whether it lies in the skin layer), the skin as a
// triangle mesh (surface nets on the distance), the skeleton as the glass view shows it, and the studio around it.
#pragma once
#include "shared/vec.h"

#include <cstdint>
#include <vector>

struct TriMesh {
    std::vector<float3> pos, nrm;     // per vertex; nrm unit
    std::vector<uint3> idx;           // counter-clockwise seen from outside (geometric normal = outward)
    void append(const TriMesh& m);
};

enum BoneId : int {
    B_HEAD, B_NECK, B_CHEST, B_ABDOMEN, B_PELVIS,
    B_UARM_L, B_FARM_L, B_HAND_L, B_UARM_R, B_FARM_R, B_HAND_R,
    B_THIGH_L, B_SHIN_L, B_FOOT_L, B_THIGH_R, B_SHIN_R, B_FOOT_R,
    B_COUNT
};
struct BoneInfo {
    const char* name;
    int parent;                       // -1: the root (the pelvis)
};
const BoneInfo& boneInfo(int b);

// the rest pose's joints, left side (+x; the right side is their mirror in x). The skin's parts and the skeleton are
// both laid out from these, and so will the physics' bones be.
struct RestJoints {
    float3 shoulder, elbow, wrist, handTip;
    float3 hip, knee, ankle, heel, toe;
};
const RestJoints& restJoints();
inline float3 mirrorX(float3 p) { return make_float3(-p.x, p.y, p.z); }

// the body's shape; x to the man's left, y up, z forward (he faces +z), feet on y = 0
struct Mannequin {
    float sdf(float3 p) const;                  // < 0 inside
    float sdf(float3 p, int& bone) const;       // and the bone of the nearest part (unblended)
    float3 normal(float3 p) const;              // the distance's gradient, unit
    float3 lo() const { return make_float3(-0.42f, -0.01f, -0.20f); }   // bounds of the shape (with a margin)
    float3 hi() const { return make_float3(0.42f, 1.80f, 0.24f); }
};

struct BodyParticles {
    float spacing = 0.007f;           // lattice spacing (m) = the particles' diameter
    std::vector<float3> pos;
    std::vector<uint8_t> bone;        // BoneId
    std::vector<uint8_t> skin;        // 1: in the outermost layer
    float volume() const { return float(pos.size()) * spacing * spacing * spacing; }
};
// a cubic lattice inside the skin: a particle wherever its sphere fits (distance <= -spacing/2); jitter moves each
// by up to that share of the spacing (a regular lattice reads as graph paper through the glass)
BodyParticles fillBody(const Mannequin& m, float spacing, float jitter = 0.0f);

TriMesh skinMesh(const Mannequin& m, float cell);   // the surface sdf = 0, surface nets on a grid of this cell (m)
TriMesh skeletonMesh();                             // the bones as drawn (ivory): skull, spine, ribs, limbs
// the studio: a white cyclorama, the floor curving up into a back wall at z = wallZ, cove radius r
TriMesh studioMesh(float wallZ, float r, float height, float halfWidth, float frontZ);
