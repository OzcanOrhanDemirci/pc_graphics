// Every island is a limestone stack that rises from the cloud sea. Its shape
// is a closed function of the cell seed, so the render mesh, the collision
// query and structure placement all read the same surface.
#pragma once
#include "scenes/ruya/ruya_world.h"

namespace ruya {
struct Site {
    float x, z, radius, height;
};
struct Island {
    float x, z;          // centre
    float radius;        // nominal top radius
    float top;           // nominal top height
    float flare;         // base widening, 0 = vertical
    float gully;         // vertical fluting depth relative to radius
    float strata;        // horizontal ledge depth relative to radius
    float dome;          // top crown relative to radius
    float terrace;       // terrace step height, 0 = none
    float lobes[6];      // rim harmonics
    float phases[6];
    unsigned seed;
    int siteCount;
    Site sites[4];
};
struct CellPlan {
    int islandCount = 0;
    Island islands[5];
    std::vector<Piece> pieces;
};
CellPlan planCell(const Cell& cell);
// Height of the island's top surface at a world point (extrapolated off the rim).
float islandTop(const Island& island, float x, float z);
// First-order signed distance estimate: negative inside the rock.
float islandDistance(const Island& island, float3 p);
float3 islandGradient(const Island& island, float3 p);
Mesh islandMesh(const Island& island);
}
