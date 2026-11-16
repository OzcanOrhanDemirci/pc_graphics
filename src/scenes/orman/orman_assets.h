// Scene 11 "Orman": the open-licence models and materials (assets/orman/cc0/, fetched by assets/orman/fetch.py) read
// into prototypes (one per variant: a glTF node) and GPU textures with their mip chains. A leaf's alpha map keeps its
// coverage down the chain (Castaño 2010): far away a fern stays a fern, not a skeleton.
#pragma once
#include "scenes/orman/orman_shared.h"

#include <cuda_runtime.h>

#include <map>
#include <string>
#include <vector>

struct OMeshCPU {
    std::vector<float3> pos, nrm;
    std::vector<float2> uv;
    std::vector<uint3> idx;
    std::vector<float> ao;    // per vertex, or empty
    int material = 0;
    float uvScale = 1.0f;
};

struct OProtoCPU {
    std::string asset, name;  // the asset id and the glTF node's name
    std::vector<OMeshCPU> geoms;
    float3 bmin{0, 0, 0}, bmax{0, 0, 0};
    bool solid = false;       // rocks, trunks: the eye does not walk through (and the oven's inside test)
    size_t triangles() const {
        size_t n = 0;
        for (const auto& g : geoms) n += g.idx.size();
        return n;
    }
};

struct OTexture {
    cudaMipmappedArray_t arr = nullptr;
    cudaTextureObject_t tex = 0;
    int w = 0, h = 0;
    size_t bytes = 0;
};

class OrmanAssets {
public:
    std::string root;                 // assets/orman/cc0
    std::vector<OProtoCPU> protos;
    std::vector<OMaterial> materials;
    std::vector<std::string> materialNames;
    std::vector<OTexture> textures;
    std::map<std::string, std::vector<int>> byAsset;   // asset id -> its prototypes
    std::vector<std::string> missing;                  // asked for, not on disk

    // a glTF model's variants as prototypes; false (and in missing) if it is not there. whole: every mesh of the file one
    // prototype as the file lays them out (a creature's body, eyes, teeth and claws), left out of byAsset (the world's
    // rules do not place it)
    bool loadModel(const std::string& id, bool solid, bool whole = false);
    // a tiling material (Poly Haven texture or ambientCG set) for the ground; -1 if not there
    int loadMaterial(const std::string& id);
    // a leaf atlas (ambientCG's LeafSet): its material cut by its opacity map, and each leaf's place in it (u0, v0, u1,
    // v1: the opacity's islands); -1 if not there
    int loadAtlas(const std::string& id, std::vector<float4>& leaves);
    int addMaterial(const OMaterial& m, const std::string& name);
    size_t textureBytes() const;
    void release();

private:
    // SpecGloss: a specular-glossiness map (KHR_materials_pbrSpecularGlossiness: glossiness in alpha) read as an ORM map
    // (no occlusion, roughness 1 - glossiness, no metal)
    enum Kind { Albedo, Normal, Linear, Alpha, SpecGloss };
    std::map<std::string, int> texIndex_;
    int texture(const std::string& path, Kind kind, float cutoff = 0.5f, bool alphaChannel = false);
};
