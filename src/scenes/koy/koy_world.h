// Scene 5 "Koy": the world. K1 (docs/koy/PLAN.md §10) is a trial area of about 20 x 20 m: rippled sand, a boulder and a
// patch of seagrass under the FFT surface; the sand reaches far beyond it so the water swallows its edge.
#pragma once
#include "core/config.h"
#include "scenes/koy/koy_kernels.h"
#include "scenes/koy/koy_renderer.h"

#include <cuda_runtime.h>

#include <string>
#include <vector>

// the sea floor's height (y, negative down) at (x, z): the same function for the mesh, the diver's collisions and the map
float koyFloor(const Config& cfg, float x, float z);
float koyRipple(const Config& cfg, float x, float z);   // the sand ripples' height (not in the mesh: a filtered normal map)

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
    KoyTextureSet ripples;          // the sand ripples: a world-space normal map (x, z over +-rippleHalf), mipmapped
    float rippleHalf = 16.0f;
    void build(const Config& cfg, const std::string& root);
    void buildRipples(const Config& cfg);
    std::vector<KTextureSet> textureSets() const;
    void release();
    std::string summary() const;
};

// a scanned material from assets/koy/cc0/<name>/ (albedo, normal, roughness), mipmapped; false if the files are not there
bool koyLoadTextureSet(const std::string& dir, const std::string& name, KoyTextureSet& out);
