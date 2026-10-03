// Scene 5 "Koy", Amfora (docs/koy/PLAN_AMFORA.md §3, §5.4): a natural limestone arch ~30 m from the wreck. Its rock is a
// signed distance field (a thick tube bent over a half-ellipse, its feet in the sand, warped, layered like bedded
// limestone, eroded), turned into a mesh by surface nets. The fine grain is the textures' (triplanar), not the mesh's.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_renderer.h"

struct KoyArchShape {
    float3 base{};          // world: the middle of the line between its feet, on the floor
    float yaw = 0.0f;       // radians: the span (local x) turned from world x about y
    float span = 15.0f;     // metres between the feet's centre lines
    float rise = 12.6f;     // the centre line's height over the base at the top
    float footR = 3.0f;     // the tube's half-thickness in the arch's plane: at the feet, at the top
    float topR = 2.6f;
    float footD = 4.2f;     // its half-depth (through the opening): at the feet, at the top
    float topD = 2.8f;
    float warp = 1.1f;      // metres the shape is bent about by low noise (it should not look drawn)
    float bump = 0.32f;     // metres of mid-scale relief
    float bed = 0.7f;       // metres a limestone bed is thick; beds stand out or recede by bedStep (patchily)
    float bedStep = 0.22f;
    float asym = 0.15f;     // the left foot this much thicker, the right this much thinner
    float lean = 1.2f;      // metres the top sits off the middle (an arch is no drawing)
    float flat = 0.8f;      // < 1: the top flatter than a half-ellipse (a bedding plane caps it)
    float spurLen = 11.0f;  // the eroded ridge the arch is left of: its runs out from each foot along the span, metres
    float spurH = 6.0f;     // ... and their height at the foot (falling to nothing at their ends)
    int blocks = 7;         // fallen blocks about its feet
    unsigned seed = 3;
    float sdf(float3 world) const;   // < 0 inside the rock
    float bound() const;             // radius of a sphere about base + (0, rise / 2, 0) holding all of it
};

KoyArchShape koyArchShape(const Config& cfg, float floorY);
// the rock's mesh in world coordinates (normals from the field's gradient), cells of `cell` metres
KMeshCPU koyBuildArch(const KoyArchShape& a, float cell, unsigned int material, unsigned int id, std::string* log = nullptr);
