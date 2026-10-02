// Scene 4 "Tanecik": the OptiX side. Four instances: the studio, the skin, the bones (triangle meshes) and the
// particles (OptiX's built-in spheres, one structure rebuilt when they move). Device program device/tanecik.cu
// (NVRTC, F5 recompiles).
#pragma once
#include "scenes/tanecik/body.h"
#include "scenes/tanecik/tanecik_params.h"

#include <cuda_runtime.h>
#include <optix.h>

#include <string>
#include <vector>

class TanecikRenderer {
public:
    bool init(unsigned int width, unsigned int height, const std::string& root);
    void shutdown();
    bool compileDevice(std::string& log);
    void uploadMeshes(const TriMesh& studio, const TriMesh& skin, const TriMesh& bones);
    // the spheres as drawn: centres, a display tint each (0 tissue .. 1 liquid), one radius
    void uploadParticles(const std::vector<float3>& centres, const std::vector<float>& tint, float radius);
    void setVisible(TKind k, bool on);                 // hidden instances are skipped by every ray
    bool visible(TKind k) const { return visible_[k]; }
    void render(unsigned int spp, bool accumulate);    // adds spp samples (accumulate) or starts afresh
    void download(std::vector<uchar4>& out);
    void downloadTo(uchar4* hostPinned);
    TanecikParams& params() { return p_; }
    unsigned int width() const { return w_; }
    unsigned int height() const { return h_; }
    float lastMs = 0.0f;

private:
    std::string root_;
    unsigned int w_ = 0, h_ = 0;
    OptixDeviceContext ctx_ = nullptr;
    OptixModule module_ = nullptr, sphereModule_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    OptixShaderBindingTable sbt_{};
    CUdeviceptr rg_ = 0, miss_ = 0, hit_ = 0, dParams_ = 0;
    cudaStream_t stream_ = nullptr;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr;
    TanecikParams p_{};
    // the meshes (studio, skin, bones): their buffers and structures
    std::vector<void*> meshBuffers_;
    TMesh* dMeshes_ = nullptr;
    OptixTraversableHandle meshGas_[3] = {0, 0, 0};
    // the particles
    float3* dCentres_ = nullptr;
    float* dTint_ = nullptr;
    float* dRadius_ = nullptr;
    int particleCount_ = 0, particleCapacity_ = 0;
    void *sphTemp_ = nullptr, *sphOut_ = nullptr;
    size_t sphTempSize_ = 0, sphOutSize_ = 0;
    OptixTraversableHandle sphereGas_ = 0;
    void buildSpheres();
    // the top level
    bool visible_[4] = {true, true, true, true};
    OptixInstance* dInstances_ = nullptr;
    void *iasTemp_ = nullptr, *iasOut_ = nullptr;
    size_t iasTempSize_ = 0, iasOutSize_ = 0;
    void buildIAS();
    void freeMeshes();
    void freeParticles();
};
