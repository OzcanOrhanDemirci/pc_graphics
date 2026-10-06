// Scene 8's oven (host side): builds the ray-tracing structure of the base mesh, traces every texel of the light map
// with OptiX in short launches (Windows resets a GPU that is busy for 2 s), and returns the light per texel as L1
// spherical harmonics. The result is cached on disk (cache/denizalti/, not committed) keyed by what it depends on.
#pragma once
#include "scenes/denizalti/dz_atlas.h"
#include "scenes/denizalti/dz_bake_shared.h"
#include "scenes/denizalti/dz_geom.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dz {

struct BakeOptions {
    int samples = 1024;      // paths per texel
    int bounces = 12;
    float texel = 0.002f;    // metres per texel (the base density)
};

// what the live picture reads: five RGBA16F planes over the atlas (L0 rgb; L1 of red, green, blue as xyz; the
// visibility of the four key lamps, their specular highlight's baked shadow)
struct LightMap {
    Atlas atlas;
    std::vector<uint16_t> plane[5];   // half floats, 4 per texel, row 0 = atlas row 0
    uint64_t key = 0;
    double bakeSeconds = 0.0;
    size_t texels = 0;
    bool valid() const { return atlas.width > 0 && !plane[0].empty(); }
};

// a hash of what the light depends on: the geometry, the lamps, the options
uint64_t bakeKey(const Mesh& m, const std::vector<DzLight>& lights, const BakeOptions& o);

// bakes (progress: 0..1 and a line). The mesh's uv2 must already be set by buildAtlas with the same atlas.
bool bake(const Mesh& m, const Atlas& a, const std::vector<DzLight>& lights, const BakeOptions& o, const std::string& root, LightMap& out,
          std::string& err, const std::function<void(float, const std::string&)>& progress = {});

bool saveLightMap(const std::string& path, const LightMap& lm);
bool loadLightMap(const std::string& path, uint64_t key, LightMap& lm);

uint16_t toHalf(float f);
float fromHalf(uint16_t h);

}   // namespace dz
