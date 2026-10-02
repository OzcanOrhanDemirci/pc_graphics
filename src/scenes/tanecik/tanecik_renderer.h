// Scene 4 "Tanecik": the OptiX side. Instances: the studio, the skin (rebuilt when it deforms), one per bone (its rest
// mesh under the bone's transform) and the particles (OptiX's built-in spheres, rebuilt when they move). Device program
// device/tanecik.cu (NVRTC, F5 recompiles).
#pragma once
#include "scenes/tanecik/body.h"
#include "scenes/tanecik/body_graph.h"
#include "scenes/tanecik/tanecik_params.h"
#include "scenes/tanecik/surface.h"

#include <cuda_runtime.h>
#include <optix.h>

#include <string>
#include <vector>

class TanecikRenderer {
public:
    bool init(unsigned int width, unsigned int height, const std::string& root);
    void shutdown();
    bool compileDevice(std::string& log);
    // the meshes at rest; bones: one mesh per bone (B_COUNT)
    void uploadMeshes(const TriMesh& studio, const TriMesh& skin, const std::vector<TriMesh>& bones);
    void setBoneXforms(const Xform* xf);                    // B_COUNT: the bones' instances, and the skin's rotations
    // the skin rides on particles (deformSkin): its binding and every particle's bone
    void setSkinBinding(const std::vector<SkinBind>& bind, const std::vector<uint8_t>& particleBone);
    void deformSkin(const float3* dParticles);              // device positions of every particle
    // which particles are drawn (indices into the solver's arrays) and how large; then their positions each frame
    void setParticleSubset(const std::vector<uint32_t>& idx, float radius);
    void updateParticles(const float3* dPos, const float* dTint);   // device; dTint may be null
    bool hideLiquidParticles = true;                        // a particle that is liquid (tint >= 0.5) is drawn by the surface
    // the free liquid: its surface built from the particles every frame (FluidSurface), droplets as spheres
    bool initLiquid(int maxParticles, int maxVertices, int maxTriangles, int maxBlocks, float floorY);
    void updateLiquid(const float3* dPos, const float* dWeight, int n, float radius);   // weight <= 0: not liquid
    const FluidSurface& liquid() const { return fluid_; }
    void setVisible(TKind k, bool on);                      // hidden instances are skipped by every ray
    bool visible(TKind k) const { return visible_[k]; }
    void render(unsigned int spp, bool accumulate);         // adds spp samples (accumulate) or starts afresh
    void download(std::vector<uchar4>& out);
    void downloadTo(uchar4* hostPinned);
    TanecikParams& params() { return p_; }
    cudaStream_t stream() const { return stream_; }
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
    // the meshes: [0] the studio, [1] the skin, [2 + b] bone b; their buffers and structures
    struct Gas {
        OptixTraversableHandle handle = 0;
        void *temp = nullptr, *out = nullptr;
        size_t tempSize = 0, outSize = 0;
    };
    std::vector<TMesh> meshes_;
    std::vector<int> meshVerts_, meshTris_;
    std::vector<Gas> gas_;
    std::vector<void*> meshBuffers_;
    TMesh* dMeshes_ = nullptr;
    void buildMeshGas(int k, bool fast);
    // the skin's deformation
    SkinBind* dBind_ = nullptr;
    uint8_t* dParticleBone_ = nullptr;
    int bindCount_ = 0;
    Xform boneXf_[B_COUNT];
    float* dBoneXf_ = nullptr;
    // the particles drawn
    uint32_t* dSubset_ = nullptr;
    float3* dCentres_ = nullptr;
    float* dTint_ = nullptr;
    float* dRadius_ = nullptr;
    int particleCount_ = 0, particleCapacity_ = 0;
    Gas spheres_;
    void buildSpheres();
    // the top level
    bool visible_[TK_KINDS] = {true, true, true, true, true, true};
    FluidSurface fluid_;
    Gas liquidGas_, dropsGas_;
    bool liquidReady_ = false;
    OptixInstance* dInstances_ = nullptr;
    Gas ias_;
    void buildIAS();
    void freeMeshes();
    void freeParticles();
    void freeGas(Gas& g);
};
