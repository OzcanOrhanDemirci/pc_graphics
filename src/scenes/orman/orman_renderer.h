// Scene 11 "Orman": the OptiX side. One GAS per prototype (its primitives as build inputs, compacted), one IAS of every
// placed copy; the probe grid and the ground's occlusion baked at load; a frame is the render launch (visibility +
// shading, the sun's shadow rays), the glare pyramid and the display launch.
#pragma once
#include "scenes/orman/orman_assets.h"
#include "scenes/orman/orman_shared.h"

#include <cuda_runtime.h>
#include <optix.h>

#include <string>
#include <vector>

struct OPlace {
    int proto = 0;
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};   // row-major 3x4, the prototype's space to the world
    float scale = 1.0f;
    float3 tint{1, 1, 1};
    unsigned int mask = OMASK_SOLID;
};

struct OBakeStats {
    double aoMs = 0.0, gridMs = 0.0, visMs = 0.0;
    size_t probes = 0;
    float validShare = 0.0f;
};

class OrmanRenderer {
public:
    OrmanParams params{};
    std::string deviceName;
    size_t gasBytes = 0, iasBytes = 0, geomBytes = 0, gridBytes = 0, visBytes = 0;
    unsigned int instanceCount = 0;
    unsigned long long instancedTriangles = 0;   // every copy's triangles: the density the eye sees
    float renderMs = 0.0f, displayMs = 0.0f;
    unsigned int badSamples = 0;                 // the last frame's samples that were not finite

    bool init(int width, int height, const std::string& root);
    bool compile(std::string& log);
    // the prototypes (the ground among them) and the placed copies
    void upload(const OrmanAssets& assets, const std::vector<OProtoCPU>& extra, const std::vector<OPlace>& places);
    // the ground's occlusion per vertex: the extra prototype `proto`'s geometry 0, placed by the identity
    void bakeGroundAO(int proto, float radius, int rays, OBakeStats& st);
    void bakeGrid(float3 lo, float3 hi, float h, int rays, int passes, OBakeStats& st);
    // the sun's visibility in the air (the mist's shafts), a 3D texture of h-metre voxels over [lo, hi]
    void bakeSunVis(float3 lo, float3 hi, float h, OBakeStats& st);
    void setPattern(int spp, float sigma);
    void render();
    void download(std::vector<unsigned char>& rgba) const;
    std::vector<float4> hdrHost() const;
    void release();

private:
    std::string root_;
    CUstream stream_ = nullptr;
    OptixDeviceContext context_ = nullptr;
    OptixModule module_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    CUdeviceptr rgRender_ = 0, rgFinal_ = 0, rgBake_ = 0, rgBakeAO_ = 0, rgBakeVis_ = 0, miss_ = 0, hits_ = 0;
    cudaArray_t visArr_ = nullptr;
    OptixShaderBindingTable sbt_{};
    OrmanParams* dParams_ = nullptr;
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr, ev2_ = nullptr;
    std::vector<void*> world_;           // every world buffer, freed together
    std::vector<void*> gridBufs_;
    std::vector<OptixTraversableHandle> gas_;
    std::vector<OGeom> geomsHost_;
    std::vector<OProto> protosHost_;
    std::vector<unsigned int> geomVerts_;   // per geometry: its vertex count
    static const int kLevels = 6;
    float4* levels_[kLevels] = {};
    float4* accum_[kLevels] = {};
    int width_ = 0, height_ = 0;
    void launch(CUdeviceptr rg, unsigned int w, unsigned int h, unsigned int d);
};
