// Scene 8's room: a Tench/Balao fleet submarine's control room as TCG Uluçalireis (ex-USS Thornback, GUPPY IIA) had it
// in Turkish service, at night under its red lamps. Measures from docs/denizalti/REFERANS.md (§0-§3).
// Built with x to starboard, y up, z towards the bow, then mirrored in x: that naming is left-handed for someone facing
// the bow (in a right-handed world looking along +z, +x is on the left), so the finished world has x to PORT. The deck
// at y = 0 on the centre line, the after bulkhead at z = 0 (frame 58 + 3"), the forward one at z = 8.23 m (frame 47 +
// 9"). Frames every 30" (0.762 m).
#pragma once
#include "scenes/denizalti/dz_bake_shared.h"
#include "scenes/denizalti/dz_geom.h"
#include "scenes/denizalti/dz_world.h"

#include <vector>

namespace dz {

namespace room {
constexpr float kFrame = 0.762f;          // frame spacing (m)
constexpr float kAxisY = 0.12f;           // the hull's axis above the deck
constexpr float kRPlate = 2.421f;         // the pressure hull plating's inside radius
constexpr float kRCork = 2.396f;          // the cork lining's face (1" of cork)
constexpr float kLength = 8.23f;          // bulkhead to bulkhead
inline float frameZ(float f) { return (58.1f - f) * kFrame; }   // frame number -> z
}   // namespace room

Start buildRoom(Mesh& m, float maxEdge, std::vector<DzLight>& lights);

}   // namespace dz
