// Scene 5 "Koy", Ada, enriched (docs/koy/PLAN_ZENGIN.md, Ozcan 2026-10-04 evening: "dubaların içi dolu dolu, dışı
// serpiştirilmiş, uzağı sakin"). The floor's own variety: a map baked at load that tells the floor shader where the
// second sand (shell-strewn, coarse) lies over the first, where silt has settled (round the wreck, the deep) and where the
// sediment at a rock's foot is coarse and dark.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_props.h"
#include "scenes/koy/koy_renderer.h"

#include <cuda_runtime.h>

#include <functional>
#include <string>
#include <vector>

struct KoyFloorMask {
    int w = 0, h = 0;
    float2 min{}, size{};
    std::vector<uchar4> px;   // r: the second sand's share, g: silt, b: coarse foot of a rock, a: unused
};
// rocks: distance fields (< 0 inside) whose feet gather shells and grit; the wreck's keel from bow to stern (silt about it)
KoyFloorMask koyBuildFloorMask(const Config& cfg, const std::function<float(float, float)>& floorY,
                               const std::vector<std::function<float(float3)>>& rocks, float3 wreckBow, float3 wreckStern,
                               std::string* log = nullptr);

// The rocks of the enriched floor: outcrops (clusters of angular limestone blocks, half sunk) along the lane's sides and
// between its stations, fewer and bigger outside the buoys, none far out. Each cluster meshed in its own box (finer in the
// lane), merged by region.
struct KoyReefRocks {
    struct Cluster {
        KoyRockField field;
        float3 c;
        float r;       // bounding radius
        float cell;    // the mesh's cell
        int region;    // 0 in the lane, 1 just outside, 2 far
    };
    std::vector<Cluster> clusters;
    float sdf(float3 p) const;   // < 0 inside a rock
};
struct KoyReefAvoid {
    std::function<float(float3)> island;   // < 0 inside the island's rock
    float3 amphoraCentre;
    float3 wreckBow, wreckStern;
};
KoyReefRocks koyReefRocks(const Config& cfg, const std::function<float(float, float)>& floorY, const KoyReefAvoid& avoid);
// meshes: one per region (lane, near, far), material and id as given
void koyBuildReefRocks(const KoyReefRocks& r, unsigned int material, unsigned int id, std::vector<KMeshCPU>& out, std::string* log = nullptr);

// The wreck as years under water leave it (Ozcan 2026-10-04: "uçan Hollandalı gibi olmalı [...] bozulma, kirlenme, kabuk
// yosun tutma"): mussel clumps where the current runs (the rails, the gunports' sills, the castles' edges, the broken
// frames' tops, the outward planking), encrusting sponges and sea squirts in patches; on the given wood and iron meshes.
struct KoyFoulMaterials {
    unsigned int mussel, spongeOrange, spongeYellow, squirt, crust, hydroid;
};
void koyFoulWreck(const Config& cfg, const KMeshCPU& wood, const KMeshCPU& iron, const KoyFoulMaterials& mat, unsigned int id,
                  std::vector<KMeshCPU>& out, std::string* log = nullptr);

// The sand's and the rocks' small life over the whole lane (and thinner outside it): empty shells (scallops Pecten
// jacobaeus whole and broken, cockles, tower shells Turritella), sand stars (Astropecten aranciacus) on the sand; on the
// rocks, peacock's tail (Padina pavonica: pale curled fans in tufts, the shallows) and encrusting sponges (orange, yellow).
struct KoyLifeMaterials {
    unsigned int shell, shellPink, sandStar, padina, spongeOrange, spongeYellow, spongeDark;
};
// shelly(x, z): the shell-strewn sand's share there (the floor map): the empty shells gather where it is
void koyBuildSandLife(const Config& cfg, const std::function<float(float, float)>& floorY, const std::function<bool(float3)>& isRock,
                      const std::function<float(float, float)>& shelly,
                      const std::vector<const KMeshCPU*>& rockMeshes, const KoyLifeMaterials& mat, unsigned int id, std::vector<KMeshCPU>& out,
                      std::string* log = nullptr);

// where sea fans stand on the outcrops: the tops and upper sides of the rock deeper than minDepth (gorgonians want the
// current and less light), spread out (no two within `gap` metres)
std::vector<std::pair<float3, float3>> koyReefFanSites(const std::vector<const KMeshCPU*>& rocks, float minDepth, int count, float gap, unsigned seed);
