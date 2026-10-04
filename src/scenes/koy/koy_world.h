// Scene 5 "Koy": the world. Ada (docs/koy/PLAN_ADA.md): a small island with a pier, the sand deepening slowly all round
// it (hundreds of metres, so the water swallows every edge), along a corridor of buoys: shallow sand, the amphorae, a
// wreck, a buoy's rope down into the blue.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_kernels.h"
#include "scenes/koy/koy_renderer.h"

#include <cuda_runtime.h>

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
    static KoyFloorShape from(const Config& cfg);
    float depth(float s) const;                             // the profile alone (metres, positive down)
    float sOf(float x, float z) const;                      // s at (x, z), wander included
    float operator()(float x, float z) const;               // the floor's height y (negative down)
};
// the sea floor's height (y, negative down) at (x, z): the same function for the mesh, the diver's collisions and the map
float koyFloor(const Config& cfg, float x, float z);
// a list of numbers from the config ("1 2 3", commas allowed); empty if the key is missing
std::vector<float> koyList(const Config& cfg, const std::string& key);

struct KoySeagrass {
    std::vector<KoyLeaf> leaves;
    int pointsPerLeaf = 5;          // quadratic B-spline control points: 3 segments a leaf
    KoyLeaf* dLeaves = nullptr;
    KoyCurrent current{};
    void build(const Config& cfg);
    void toRibbons(KRibbonsCPU& r, unsigned int material) const;
    // the leaves sway with the swell's orbital current at the floor (K1 kinematic; K3: XPBD with contact)
    void update(float t, float4* pts, float3* nrm, cudaStream_t s) const;
    void release();
};

struct KoyTextureSet {
    std::string name;
    KTextureSet set{};
    std::vector<void*> arrays;
    std::vector<unsigned long long> textures;
};

struct KoyWorld {
    std::vector<KMeshCPU> meshes;
    std::vector<KInstanceCPU> instances;
    std::vector<KMaterial> materials;
    std::vector<KoyTextureSet> textures;
    KoySeagrass grass;
    KRibbonsCPU ribbons;
    float3 rockCentre{};
    float rockRadius = 0.0f;
    // the texture choice (the panel's "Doku"): 0 standard (2K scans), 1 high (everything 8K, detail layers), 2 ultra (the
    // sand and the rock's detail layer at 16K; the boulder's own scan ends at 8K). All of them stay loaded: live switching
    int texMode = 2;
    int setSand = -1, setSand4k = -1, setRock = -1, setRock8k = -1, setRockDetail = -1;
    int setSand16k = -1, setRockDetail16k = -1;
    int setClay = -1, setCoat = -1;   // Amfora: the jars' clay (8K, a 1.64 m tile) and the concretion (8K, 1.36 m)
    int setWood = -1, setConcrete = -1;   // Ada: the pier's and the wreck's boards (4K, 0.5 m), the piles' concrete (4K, 2.7 m)
    double textureSeconds = 0.0;    // decoding, mipmapping and uploading every set
    size_t textureBytes = 0;        // the sets' GPU arrays, all levels
    bool hasMode(int mode) const;
    void applyTextureMode(const Config& cfg, int mode);   // takes the best mode that is there, at most `mode`
    void loadTextures(const Config& cfg, const std::string& dir);   // every set decoded in parallel, uploaded in order
    void build(const Config& cfg, const std::string& root);
    std::vector<KTextureSet> textureSets() const;
    void release();
    std::string summary() const;
};

// a scanned material from assets/koy/cc0/<name>/ (albedo, normal, roughness), mipmapped; false if the files are not there
bool koyLoadTextureSet(const std::string& dir, const std::string& name, KoyTextureSet& out);
