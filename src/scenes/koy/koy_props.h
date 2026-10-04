// Scene 5 "Koy", Ada (docs/koy/PLAN_ADA.md §3): what lies about on the sand. The rocks the amphorae gathered among (angular
// limestone blocks as a signed distance field, meshed by surface nets); and, sparse and random as Ozcan asked ("deniz
// taşları, deniz yıldızı gibi ufak süslemeler [...] sık olmasına da gerek yok"), the shallows' small things: rounded
// pebbles in patches, red Mediterranean starfish (Echinaster sepositus), sea cucumbers (Holothuria tubulosa), dead fan
// mussel shells (Pinna nobilis) standing half buried, black sea urchins (Arbacia lixula) on the rock.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_renderer.h"

#include <functional>
#include <random>
#include <string>
#include <vector>

struct KoyRockField {
    struct Rock {
        float3 c;
        float r;
        float3 n[10];
        float d[10];
        float seed;
    };
    std::vector<Rock> rocks;
    float sdf(float3 p) const;   // < 0 inside a rock
};
// an angular limestone block about R.c of radius R.r (its faces from rng)
void koyMakeBlock(KoyRockField::Rock& R, std::mt19937& rng);
// the amphorae's rocks about amfora.centre: a crescent of big blocks on the down-slope side (the trap the cargo slid
// against) and smaller ones about
KoyRockField koyAmphoraRocks(const Config& cfg, const std::function<float(float, float)>& floorY);
KMeshCPU koyBuildRockField(const KoyRockField& f, float cell, unsigned int material, unsigned int id, std::string* log = nullptr);

// sea fans (red Paramuricea clavata, yellow Eunicella cavolini): flat branching colonies across the current, on the up-
// and side-facing faces of the host meshes deeper than minDepth (the wreck)
void koyBuildFans(const Config& cfg, const std::vector<std::pair<float3, float3>>& sites, unsigned int matRed, unsigned int matYellow, unsigned int id,
                  std::vector<KMeshCPU>& out, std::string* log = nullptr);

struct KoyPropMaterials {
    unsigned int stoneLight, stoneDark, starfish, cucumber, pinna, urchin;
};
// the shallows' small things; isRock(p): inside some rock (nothing is placed there); urchins sit on the given rock
// meshes (their up-facing triangles in the shallows)
void koyBuildProps(const Config& cfg, const std::function<float(float, float)>& floorY, const std::function<bool(float3)>& isRock,
                   const std::vector<const KMeshCPU*>& rockMeshes, const KoyPropMaterials& mat, unsigned int id, std::vector<KMeshCPU>& out,
                   std::string* log = nullptr);
