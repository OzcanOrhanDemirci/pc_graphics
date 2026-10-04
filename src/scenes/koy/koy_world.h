// Scene 5 "Koy": the world. Ada (docs/koy/PLAN_ADA.md): a small island with a pier, the sand deepening slowly all round
// it (hundreds of metres, so the water swallows every edge), along a corridor of buoys: shallow sand, the amphorae, a
// wreck, a buoy's rope down into the blue.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_buoys.h"
#include "scenes/koy/koy_kernels.h"
#include "scenes/koy/koy_reef.h"
#include "scenes/koy/koy_renderer.h"

#include <cuda_runtime.h>

#include <memory>
#include <string>
#include <vector>

// The sea floor's shape, read once from the config (the mesh asks it millions of times). Ada (docs/koy/PLAN_ADA.md §4):
// the sand deepens away from the island along one smooth profile (depth against s, the distance beyond the pier's end
// measured from the island's centre), the same all round; a slow wander of s (noise) keeps the contours natural, small
// undulations and long low sand waves ride on it. The island's rock covers it where it would rise above the water.
struct KoyFloorShape {
    float islandX = 0.0f, islandZ = 60.0f, pierR = 60.0f;   // the pier's end lies pierR from the island's centre
    std::vector<float> ps, pd, pm;                          // profile knots: s, depth, slope (monotone cubic Hermite)
    float undulation = 0.25f, sandWave = 0.5f, wander = 6.0f, shallowest = 0.3f;
    // Ada enriched: the sand drifted into the wreck's hold and against its hull, a low mound along the keel (bow, stern
    // from batik.centre / batik.deg as the wreck itself is placed); the wreck stands on base(), not on its own mound
    float moundH = 0.0f, moundW = 4.5f;
    float2 moundA{}, moundB{};
    float base(float x, float z) const;                     // the floor without the wreck's mound
    static KoyFloorShape from(const Config& cfg);
    float depth(float s) const;                             // the profile alone (metres, positive down)
    float sOf(float x, float z) const;                      // s at (x, z), wander included
    float operator()(float x, float z) const;               // the floor's height y (negative down)
};
// the sea floor's height (y, negative down) at (x, z): the same function for the mesh, the diver's collisions and the map
float koyFloor(const Config& cfg, float x, float z);
// a list of numbers from the config ("1 2 3", commas allowed); empty if the key is missing
std::vector<float> koyList(const Config& cfg, const std::string& key);

struct KoyTextureSet {
    std::string name;
    KTextureSet set{};
    std::vector<void*> arrays;
    std::vector<unsigned long long> textures;
};

struct KoyWorld {
    void tileScatter(float tile);   // the scattered meshes merged by tile (koy_world.cpp)
    std::vector<KMeshCPU> meshes;
    std::vector<KInstanceCPU> instances;
    std::vector<KMaterial> materials;
    std::vector<KoyTextureSet> textures;
    KoyBuoys buoys;                 // Ada: the buoy lines and the mooring buoy (moving instances)
    float3 rockCentre{};
    float rockRadius = 0.0f;
    // The textures (the panel's "Doku"): one set per role, each at its own fixed index (a material points at a role, not
    // at a file), the same recipe in every tier; the tier says only how sharp: 0 Düşük (1K), 1 Normal (2K), 2 Yüksek (the
    // scans' 8K, the sand and the rock's detail layer 16K). Only the tier on screen is on the GPU: another one is loaded in
    // its place (Ozcan 2026-10-05: lowering it must free the memory; before, all of them stayed loaded)
    enum TexRole { TR_SAND, TR_ROCK, TR_ROCK_DETAIL, TR_CLAY, TR_COAT, TR_WOOD, TR_CONCRETE, TR_FLOOR2, TR_COUNT };
    int texMode = 2;                      // the tier loaded (or being loaded)
    int setSand = -1, setRock = -1, setRockDetail = -1;   // the role's index, -1: its files are not there
    int setClay = -1, setCoat = -1;   // Amfora: the jars' clay (a 1.64 m tile) and the concretion (1.36 m)
    int setFloor2 = -1;                   // Ada enriched: the second sand (shell-strewn, 3 m)
    KoyFloorMask floorMask;               // where it lies, where silt and grit are (koy_reef.h)
    int setWood = -1, setConcrete = -1;   // Ada: the pier's and the wreck's boards (0.5 m), the piles' concrete (2.7 m)
    std::string texDir;             // assets/koy/cc0
    double textureSeconds = 0.0;    // decoding, mipmapping and uploading the tier
    size_t textureBytes = 0;        // the sets' GPU arrays, all levels
    struct TexLoad;                 // a tier on its way (koy_world.cpp)
    std::shared_ptr<TexLoad> texLoad;
    // the loaded tier leaves the GPU (its memory free at once), the next one's maps start decoding, each on its own thread
    void beginTextures(const Config& cfg, int tier);
    // on the CUDA thread: uploads one decoded set (the frame shows the bar between them); true once all are up.
    // fraction: how far (decoding estimated from the time it takes), what: the biggest still on its way
    bool pollTextures(float* fraction = nullptr, std::string* what = nullptr);
    bool texturesLoading() const { return texLoad != nullptr; }
    void loadTexturesNow(const Config& cfg, int tier);
    void applyTextureRecipe(const Config& cfg);   // the materials' detail layers (the same in every tier)
    void build(const Config& cfg, const std::string& root);
    std::vector<KTextureSet> textureSets() const;
    void release();
    std::string summary() const;
};

// a scanned material from assets/koy/cc0/<name>/ (albedo, normal, roughness), mipmapped; false if the files are not there
bool koyLoadTextureSet(const std::string& dir, const std::string& name, KoyTextureSet& out);
