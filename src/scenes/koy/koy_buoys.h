// Scene 5 "Koy", Ada (docs/koy/PLAN_ADA.md §3-§4): the swimming area's buoy lines (the corridor's walls: floats on a rope
// along both sides and across the far end, down-lines to concrete blocks on the sand), the big mooring buoy at the far
// end with its rope straight down into the blue to a block at ~47 m, mussels thick on that rope's upper metres and in a
// band on the pier's piles at the water line. The floats ride the waves (their heights sampled on the GPU every frame,
// their instances moved); everything else is static.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_renderer.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

// one Mediterranean mussel (Mytilus galloprovincialis): hinge at `at`, along `axis`, its flat side towards `side`
void koyAddMussel(KMeshCPU& m, float3 at, float3 axis, float3 side, float len, unsigned seed);

struct KoyBuoys {
    struct Floater {
        float x, z, r;
        float lift;          // the centre's height over the wave, in radii (a float rides high, the moored buoy low)
        int instance;
    };
    struct Rope {            // between floaters a and b, or from floater a down to a fixed point
        int a, b;
        float3 fixedEnd;
        float r;
        int instance;
    };
    std::vector<Floater> floaters;
    std::vector<Rope> ropes;
    // adds meshes and instances to the world's lists; the moving instances' indices are kept here
    void build(const Config& cfg, const std::function<float(float, float)>& floorY, std::vector<KMeshCPU>& meshes, std::vector<KInstanceCPU>& instances,
               unsigned int matFloat, unsigned int matBig, unsigned int matRope, unsigned int matBlock, unsigned int matMussel, unsigned int id,
               std::string* log = nullptr);
    std::vector<float2> samplePoints() const;   // x, z of every floater (the waves' heights are wanted there)
    // the moving instances' transforms for the sampled wave heights (one per floater, in samplePoints' order)
    void transforms(const std::vector<float>& heights, std::vector<int>& idx, std::vector<std::array<float, 12>>& m) const;
};
