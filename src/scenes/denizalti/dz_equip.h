// Scene 8's equipment: the control room's stations (REFERANS §3) built into the room's mesh.
#pragma once
#include "scenes/denizalti/dz_bake_shared.h"
#include "scenes/denizalti/dz_geom.h"
#include "scenes/denizalti/dz_room.h"

namespace dz {

// everything at its frame; chartTop: where the chart table's top is (the chart lamp hangs over it)
// minor: the small glows (the Christmas tree's lenses) that light their surroundings but have no highlight of their own
void buildEquipment(Kit& k, float3& chartTop, std::vector<DzLight>& minor);

}   // namespace dz
