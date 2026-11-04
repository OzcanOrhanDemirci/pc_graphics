// Scene 13 "Dağ": the OptiX renderer. One GAS per whole degree of the far ground, compacted one at a time so a build's peak
// stays small; the near ground, the railing; the clip stacks, the air's tables as textures. A frame is two launches.
#pragma once
#include "scenes/dag/dag_atmos.h"
#include "scenes/dag/dag_shared.h"
#include "scenes/dag/dag_world.h"

#include <optix.h>

#include <cuda_runtime.h>

#include <string>
#include <vector>

class DagRenderer {
public:
    DagParams params{};
    float renderMs = 0.0f, displayMs = 0.0f;
    unsigned int badSamples = 0;
    float4 probe{};                     // what the middle of the picture looks at (see DagParams)
    float4 badAt{};                     // where the last sample that was not finite fell
    unsigned long long totalTriangles = 0;
    size_t gasBytes = 0, texBytes = 0, tableBytes = 0, meshBytes = 0;
    double buildGasMs = 0.0;
    std::string deviceName;

    bool init(int width, int height, const std::string& root);
    void setPattern(int spp, float sigma);
    bool compile(std::string& log);
    void uploadGround(DagWorld& w);      // the host's copies are freed as they go up
    void uploadTables(const DagTables& t);
    void uploadRock(int which, const std::string& albedoPath, const std::string& normalPath);
    void uploadCirrus(const std::vector<unsigned char>& tex, int n);
    void render();
    void download(std::vector<unsigned char>& rgba) const;
    std::vector<float4> hdrHost() const;
    void release();

private:
    struct Tex {
        cudaMipmappedArray_t mip = nullptr;
        cudaArray_t arr = nullptr;
        cudaTextureObject_t tex = 0;
    };
    Tex makeMip(const void* base, int w, int h, int elemBytes, cudaChannelFormatDesc fd, bool srgb, bool wrapU, bool wrapV, const std::vector<std::vector<unsigned char>>* levels);
    Tex makeRgba8Mips(std::vector<unsigned char>& rgba, int n, bool srgb, bool wrap);
    Tex makeRgba16Mips(std::vector<uint16_t>& v, int w, int h, bool wrapU, bool wrapV);
    Tex makeFloat2D(const std::vector<float4>& v, int w, int h, bool wrapU);
    Tex makeFloat3D(const std::vector<float4>& v, int w, int h, int d);
    void loadRgbaPair(const std::string& albedoPath, const std::string& normalPath, unsigned long long& albedo, unsigned long long& normal, int& size, float* mean, bool clean = false, bool half = false);
    void launch(CUdeviceptr rg, unsigned int w, unsigned int h, unsigned int d);
    OptixTraversableHandle buildGasTriangles(const float3* verts, size_t nVerts, const uint3* idx, size_t nTri);
    void buildTop();
    void bakePolarAO(DagWorld::Polar& P, int rays, float reach, float reachMax);

    static constexpr int kLevels = 7;
    std::string root_;
    int width_ = 0, height_ = 0;
    OptixDeviceContext context_ = nullptr;
    OptixModule module_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    OptixShaderBindingTable sbt_{};
    CUdeviceptr rgRender_ = 0, rgFinal_ = 0, rgBakeAO_ = 0, miss_ = 0, hits_ = 0;
    void* dParams_ = nullptr;
    void* stream_ = nullptr;
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr, ev2_ = nullptr;
    float4* levels_[kLevels] = {};
    float4* accum_[kLevels] = {};
    std::vector<void*> gasBufs_, meshBufs_;
    std::vector<OptixInstance> instances_;
    void* iasBuf_ = nullptr;
    void* instBuf_ = nullptr;
    std::vector<Tex> textures_, tables_;
    std::vector<DagGeom> geomsHost_;
};
