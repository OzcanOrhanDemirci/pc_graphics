// Scene 5 "Koy", Ada (docs/koy/PLAN_ADA.md §3): the wreck. A 17th-century three-masted galleon of ~38 m ("Kara İnci tarzı
// korsan gemisi", Ozcan), half fallen in: built timber by timber from one hull function (a deadrise floor, tumblehome
// sides, a sheer rising fore and aft, a flat transom), frames, strakes of planking with butt joints, two decks on beams
// with hatches, a quarterdeck and poop with the great cabin's stern windows and quarter galleries, a forecastle, gunports
// with a few lids hanging, broken masts (the main's upper part fallen on the sand), guns, an anchor, the rudder, ballast.
// It lies on the slope bow down, listing to starboard; the starboard side has fallen in (its frames standing like ribs),
// the port side and the stern castle stand. One can swim in: through the hatches, the broken side, the cabin's door.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_renderer.h"

#include <functional>
#include <string>
#include <vector>

struct KoyWreckPlacement {
    float m[12];          // ship (x to the bow, y up from the keel, z to port) -> world, rows
    float3 bow, stern;    // world points (the keel's ends), for the map and the props
    std::vector<std::pair<float3, float3>> growth;   // where sea fans grow: the rails, the castles' edges, the ribs' tops (world)
};

// meshes in world coordinates: wood, iron, ballast stone (with those materials); the placement used
KoyWreckPlacement koyBuildWreck(const Config& cfg, const std::function<float(float, float)>& floorY, unsigned int matWood, unsigned int matIron,
                                unsigned int matStone, unsigned int id, std::vector<KMeshCPU>& out, std::string* log = nullptr);
