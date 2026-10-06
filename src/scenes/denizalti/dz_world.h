// Scene 8's world: what is built, where. World axes: y up, z towards the bow, x to starboard; the deck at y = 0.
#pragma once
#include "scenes/denizalti/dz_bake_shared.h"
#include "scenes/denizalti/dz_geom.h"

#include <vector>

namespace dz {

// D0's test wall: a stretch of the port side's plating with ring frames, a valve manifold with its piping, gauges and
// a tread-plate deck. Returns the camera's suggested start.
struct Start {
    float3 pos;
    float yaw, pitch;
};
Start buildTestWall(Mesh& m, float maxEdge, std::vector<DzLight>& lights);

}   // namespace dz
