// Scene 5 "Koy", Ada (docs/koy/PLAN_ADA.md §4): the small island and its pier. The island is a limestone mass as a signed
// distance field (a height profile about its waterline: a low cliff, a gentle crown, the rock sloping into the sand
// under water; jointed blocks, beds, eroded relief, boulders along its foot), meshed by surface nets, cut in two at the
// waterline (wet rock with algae and concretion, dry sunlit rock with scrub). It is seen above the water only through
// Snell's window. A sandy cove opens where the pier starts.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_renderer.h"

#include <functional>
#include <string>
#include <vector>

struct KoyIslandShape {
    float cx = 0.0f, cz = 60.0f;   // centre (x, z)
    float R0 = 35.0f;              // mean waterline radius
    float H = 14.0f;               // crown height over the water
    float cliff = 3.0f;            // the low cliff at the waterline
    float slope = 1.1f;            // the rock's fall under water (metres down per metre out)
    float coveDepth = 9.0f;        // the cove of the pier: the waterline drawn back this far ...
    float coveHalf = 0.30f;        // ... over this half angle (radians) about coveDir
    float2 coveDir{0.0f, -1.0f};   // from the centre towards the pier
    unsigned seed = 5;
    // the two reef arms: the headlands either side of the cove go on under the water as rocky ridges along the pier,
    // lower and more broken seaward (the pier stands in a sandy channel between them)
    struct Reef { float2 a, b; float topA, topB, halfW; };
    Reef reefs[2];
    int reefCount = 0;
    // a boulder: a block split off the cliff, angular (an intersection of planes about its centre), worn a little
    struct Boulder { float3 c; float r; float ang; float3 n[9]; float d[9]; };
    std::vector<Boulder> boulders;                 // along the foot, sorted by angle
    std::vector<std::vector<int>> bins;            // angle bins (2 degrees) -> boulders whose reach touches them
    float sdf(float3 p) const;                     // < 0 inside the rock
    float waterline(float ux, float uz) const;     // the waterline radius towards (ux, uz)
};

KoyIslandShape koyIslandShape(const Config& cfg, const std::function<float(float, float)>& floorY);
// two meshes: [0] wet (below waterline + splash), [1] dry
void koyBuildIsland(const KoyIslandShape& s, float cell, unsigned int matWet, unsigned int matDry, unsigned int id, std::vector<KMeshCPU>& out,
                    std::string* log = nullptr);

// The pier (PLAN_ADA §3, Ozcan's reference photograph and words, 2026-10-04: "under it dark against its surroundings,
// light leaking between its boards", "real darkness, shadowy, a little frightening: going under it should unsettle, as
// a cave or a wreck does"): a timber walkway from the beach (boards along it with gaps: stripes of light below) and at
// its seaward end a wide, low T-head of concrete: a solid slab on a grid of beams on a forest of fouled piles, broken
// through in a few places (rusted reinforcing bars sticking out), so that under it stands a dark hall with a few shafts
// of sun. A railing; a steel ladder on the head's seaward edge. Every board its own box with its own grain offset.
struct KoyPierSection {
    float x0, x1, z0, z1;                 // the deck's extent
    std::vector<float> pileX, pileZ;      // the pile grid
    bool concrete = false;                // a slab on beams (the head) instead of boards on joists (the walkway)
};
struct KoyPierSpec {
    std::vector<KoyPierSection> sections; // [0] the walkway, [1] the head
    float deck = 1.1f;                    // the deck's top over the water
    float pileR = 0.25f;
    float plankW = 0.14f, plankT = 0.045f, gap = 0.025f, missing = 0.05f;
    float3 ladder{2.6f, 0.0f, -2.0f};     // the ladder's centre (x, z) on the head's seaward edge
};
KoyPierSpec koyPierSpec(const Config& cfg);
// meshes: wood, wet concrete, dry concrete, steel, rust (in that order, with those materials)
void koyBuildPier(const KoyPierSpec& p, const std::function<float(float, float)>& floorY, unsigned int matWood, unsigned int matWet,
                  unsigned int matDry, unsigned int matSteel, unsigned int matRust, unsigned int id, std::vector<KMeshCPU>& out,
                  std::string* log = nullptr);
