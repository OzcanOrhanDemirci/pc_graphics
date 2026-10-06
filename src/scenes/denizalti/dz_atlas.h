// Scene 8's light map atlas: every chart of the mesh (one per grid the kit made) gets a rectangle of texels at a
// density in metres per texel, packed on shelves; the chart's own surface coordinates (metres) map into it. Then the
// texels are rasterised back onto the surface: for each texel inside a triangle, its point and normal (the oven's
// starting points).
#pragma once
#include "scenes/denizalti/dz_geom.h"

#include <cstdint>
#include <vector>

namespace dz {

struct Atlas {
    int width = 0, height = 0;   // texels
    float texel = 0.002f;        // metres per texel (the base density; small charts finer, see build)
    int pad = 2;                 // texels around every chart (bilinear reads never leak)
    size_t usedTexels = 0;       // inside triangles
};

// one texel the oven lights: where it is and what it faces
struct Texel {
    float3 p;      // on the base surface
    float3 n;      // the shading normal there
    uint32_t xy;   // x | y << 16 in the atlas
    uint32_t tri;  // the triangle it lies in
};

// Packs the charts, writes every vertex's uv2 (0..1). texel: the base density; maxWidth: the atlas' width
Atlas buildAtlas(Mesh& m, float texel, int maxWidth = 8192);

struct Rasterised {
    std::vector<Texel> texels;
    std::vector<int32_t> index;   // atlas texel -> its index in texels (-1: none)
};
// The texels covered by the mesh (centres inside a triangle, or within half a texel of one: the chart's rim is lit
// too), at most one per atlas texel
Rasterised rasterise(const Mesh& m, const Atlas& a);

// After baking: every empty texel within the padding takes the average of its lit neighbours (repeated pad times), so
// bilinear reads at a chart's rim never mix in black. data: channels floats per texel
void dilate(std::vector<float>& data, int channels, const Atlas& a, const std::vector<int32_t>& index);

}   // namespace dz
