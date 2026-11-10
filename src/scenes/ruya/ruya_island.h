// Every island is a quartzite stack that rises from the cloud sea. Its shape is
// a closed function of the cell seed, so the render mesh, the collision query
// and placement of structures and plants all read the same surface.
#pragma once
#include "scenes/ruya/ruya_world.h"

#include <memory>

namespace ruya {
inline constexpr int kIslandMaxColumns = 7;
inline constexpr int kIslandMaxSides = 8;
inline constexpr int kIslandMaxJoints = 16;
inline constexpr int kIslandMaxAlcoves = 4;
inline constexpr int kIslandMaxProfile = 8;
inline constexpr int kIslandMaxSites = 4;
// A column with this top height runs the full height of the stack.
inline constexpr float kColumnFullHeight = 1e9f;
// Plan polygons reach at most this multiple of the nominal radius.
inline constexpr float kIslandMaxReach = 1.4f;

enum IslandKind : unsigned { kKindMesa = 0, kKindTerrace = 1, kKindSpire = 2, kKindWooded = 3 };

struct Site {
    float x, z, radius, height;
};
// A convex plan polygon. Half planes n.p <= d are measured from the island
// centre and always contain it, so the union stays a polar function.
struct Column {
    float topY;  // the column exists only below this height
    int sides;
    float nx[kIslandMaxSides], nz[kIslandMaxSides], d[kIslandMaxSides];
};
struct Joint {
    float angle, width, depth, bottomY;
};
struct Alcove {
    float angle, halfAngle, topY, bottomY, depth;
};
// Side profile: radius multiplier at depths below the rim, linear between keys.
struct Profile {
    int count;
    float depth[kIslandMaxProfile], scale[kIslandMaxProfile];
};
struct Island {
    float x, z;     // centre
    float radius;   // nominal top radius (mean plan radius)
    float top;      // nominal top height
    float strata;   // ledge depth of a hard bed, metres
    float capBreak; // block-to-block jog of the caprock rim, metres
    float undulate; // crown relief amplitude, metres
    float plates;   // height of the rock plates on the crown, metres
    float terrace;  // terrace step height, 0 = none
    float tiltX, tiltZ;
    Profile profile;
    Column columns[kIslandMaxColumns];
    int columnCount, coreColumns;  // full-height columns come first
    Joint joints[kIslandMaxJoints];
    int jointCount;
    Alcove alcoves[kIslandMaxAlcoves];
    int alcoveCount;
    unsigned seed, kind;
    int siteCount;
    Site sites[kIslandMaxSites];
};
struct CellPlan {
    int islandCount = 0;
    Island islands[5];
    std::vector<Piece> pieces;
};
// Built structures (walls, towers, paving) as opposed to plants and rocks.
bool isStructurePrototype(unsigned prototype);
// Ground cells, kFootprintGrid metres square, under the solid part of a placed
// structure: sorted and unique. Empty for any other prototype.
inline constexpr float kFootprintGrid = 0.5f;
std::vector<std::pair<int, int>> structureFootprint(const Piece& piece);

// planCell is pure; cachedPlan keeps its results for cells in use.
CellPlan planCell(const Cell& cell);
std::shared_ptr<const CellPlan> cachedPlan(const Cell& cell);

Island makeIsland(float x, float z, unsigned seed, unsigned kind, float radius, float top);
void addIslandSite(Island& island, float x, float z, float radius);

// Height of the island's top surface at a world point (extrapolated off the rim).
float islandTop(const Island& island, float x, float z);
float islandRimRadius(const Island& island, float angle);
// Height of the rim at an angle.
float islandRimHeight(const Island& island, float angle);
// Wall radius at a world height below the rim.
float islandWallRadius(const Island& island, float angle, float y);

// Bedding is global: bed k spans bedFloor(k) to bedFloor(k + 1) on every island.
int bedIndexAt(float y);
float bedFloor(int k);
// Radial width of the flat top of bed k at an angle; radius is where it starts.
float bedLedgeWidth(const Island& island, float angle, int k, float& radius);
// Inner and outer radius of the shelf a lower column forms at an angle.
bool columnShelf(const Island& island, int column, float angle, float& inner, float& outer);

// First-order signed distance estimate: negative inside the rock.
float islandDistance(const Island& island, float3 p);
float3 islandGradient(const Island& island, float3 p);
Mesh islandMesh(const Island& island);
}
