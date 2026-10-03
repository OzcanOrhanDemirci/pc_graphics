// Scene 5 "Koy", Amfora (docs/koy/PLAN_AMFORA.md §3, §5.3): the cargo of a Roman merchantman of the 1st century BC,
// Dressel 1B wine amphorae mostly, lying where they fell when the hull rotted away: a mound of rows, half sunk in the sand.
// A jar is a lathe of its type's profile (outside, the rim, the inside of the neck down to the sediment that fills it)
// and two handles swept along their arcs; the field is instances of a few such meshes.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_renderer.h"

#include <functional>
#include <vector>

struct KoyAmphoraType {
    const char* name;
    std::vector<float2> outer;    // (radius, height) in metres, from the foot (0) up to the top of the rim
    std::vector<float2> inner;    // the cavity's wall (radius, height), from its bottom up to the rim
    std::vector<float2> handle;   // a handle's centre line (radius, height): from inside the neck out and down into the shoulder
    float handleR, handleT;       // its section: radial and tangential (metres)
    bool doubleRod;               // Dressel 2-4: two rods side by side (the bifid handle)
    float fillDepth;              // how far below the rim the sediment fills it
};

const KoyAmphoraType& koyAmphoraType(int k);   // 0 Dressel 1B, 1 Dressel 1A, 2 Lamboglia 2, 3 Dressel 2-4
int koyAmphoraTypes();

// the jar's mesh in its own frame: the axis +y, the toe's tip at the origin. UVs: u around (metres of circumference,
// a whole number of texture tiles), v along the profile (metres).
KMeshCPU koyBuildAmphora(const KoyAmphoraType& t, int segments, unsigned int material, unsigned int id);

struct KoyAmphoraPlacement {
    int type;
    float m[12];                  // object to world, 3x4 rows
    float burial;                 // 0 lying on the sand .. 1 its width under it
};
// the wreck's field on its mound: positions, turns, how deep each jar sank (floorY: the floor's height)
std::vector<KoyAmphoraPlacement> koyPlaceAmphorae(const Config& cfg, const std::function<float(float, float)>& floorY);
