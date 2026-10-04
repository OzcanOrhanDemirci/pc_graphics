// Scene 5 "Koy", Amfora (docs/koy/PLAN_AMFORA.md §3, §5.3): the cargo of a Roman merchantman of the 1st century BC,
// Dressel 1B wine amphorae mostly, lying where they fell when the hull rotted away: a mound of rows, half sunk in the sand.
// A jar is a lathe of its type's profile (outside, the rim, the inside of the neck down to the sediment that fills it)
// and two handles swept along their arcs; the field is instances of a few such meshes.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_renderer.h"

#include <functional>
#include <string>
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
KMeshCPU koyBuildAmphora(const KoyAmphoraType& t, int segments, unsigned int material, unsigned int id, bool handles = true);

// A whole jar or what is left of it: a stretch of its wall between two heights and two angles, the boundaries jagged
// where it broke (a whole jar: the full height, all round). The break shows the wall's thickness between the outside and
// the cavity's wall.
struct KoyAmphoraPiece {
    int type;                     // which profile
    int kind;                     // 0 whole, 1 body (the neck broken off), 2 neck with its handles, 3 half body, 4 sherd
    float z0, z1;                 // heights (metres): a break where it is not the jar's own end
    float a0, a1;                 // angles (radians); a1 - a0 >= 2 pi: all round
    float jag;                    // metres the break wanders
    unsigned seed;
};
std::vector<KoyAmphoraPiece> koyAmphoraPieces();   // the pieces the field draws from (one mesh each)
KMeshCPU koyBuildPiece(const KoyAmphoraPiece& p, int segments, unsigned int material, unsigned int id);

struct KoyAmphoraPlacement {
    int piece;                    // index into koyAmphoraPieces()
    float m[12];                  // object to world, 3x4 rows
    float burial;                 // 0 lying on the sand .. 1 its width under it
};
// the wreck's field on its mound: positions, turns, how deep each jar sank (floorY: the floor's height)
// key: the config prefix of this field (centre, count, sherds, ship_deg, half_length, half_width, seed); obstacle: the
// distance to rocks they must not pass into (empty: none)
std::vector<KoyAmphoraPlacement> koyPlaceAmphorae(const Config& cfg, const std::function<float(float, float)>& floorY, const std::string& key = "amfora",
                                                 const std::function<float(float3)>& obstacle = {});
