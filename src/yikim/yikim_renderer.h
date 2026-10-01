// Scene 3 "Yikim": the OptiX side. Every body is an instance of its own hull (a GAS built once, in the body frame);
// the physics writes the instance transforms straight into the instance buffer on the GPU every frame and the
// top-level structure is rebuilt. Device program src/device/yikim.cu (NVRTC, F5 recompiles).
#pragma once
#include "shared/yikim_params.h"
#include "yikim/phys.h"

#include <cuda_runtime.h>
#include <optix.h>

#include <string>
#include <vector>

class YikimRenderer {
public:
    bool init(unsigned int width, unsigned int height, const std::string& root);
    void shutdown();
    bool compileDevice(std::string& log);
    void uploadWorld(const PhysWorld& w);              // meshes, instances, colours (after the world is built)
    void updateWorld(PhysWorld& w);                    // transforms and bars from the physics, top level rebuilt
    void* transformsDone() const { return evXf_; }     // a cudaEvent_t recorded after updateWorld read the state
    void setDamage(const std::vector<float>& perBody); // display tint, per body (index = body)
    void render(unsigned int spp, bool accumulate);    // adds spp samples (accumulate) or starts afresh
    void download(std::vector<uchar4>& out);
    void downloadTo(uchar4* hostPinned);                 // width * height pixels
    // the body under render pixel (x, y): index, or 0 the ground, -1 the sky; hit point and distance in hit
    int pick(float x, float y, float4& hit);
    YikimParams& params() { return p_; }
    float lastMs = 0.0f;
    float barRadius = 0.01f, barGap = 0.005f, barStub = 0.25f;   // the reinforcing bars (m), see PhysWorld::writeBars
    bool showBars = true;
    unsigned int width() const { return w_; }
    unsigned int height() const { return h_; }

private:
    std::string root_;
    unsigned int w_ = 0, h_ = 0;
    OptixDeviceContext ctx_ = nullptr;
    OptixModule module_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    OptixShaderBindingTable sbt_{};
    CUdeviceptr rg_ = 0, rgPick_ = 0, miss_ = 0, hit_ = 0, dParams_ = 0;
    int* dPickBody_ = nullptr;
    float4* dPickHit_ = nullptr;
    cudaStream_t stream_ = nullptr;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr, evXf_ = nullptr;
    YikimParams p_{};
    std::vector<void*> worldBuffers_;
    OptixInstance* dInstances_ = nullptr;
    int instanceCount_ = 0;        // the bodies, then the bars (one instance, world frame)
    int bodyInstances_ = 0;
    // the bars: one triangle structure rebuilt every frame from vertices the physics writes
    int barPrisms_ = 0;
    float3* barVerts_ = nullptr;
    uint3* barIdx_ = nullptr;
    void *barTemp_ = nullptr, *barOut_ = nullptr;
    size_t barTempSize_ = 0, barOutSize_ = 0;
    OptixTraversableHandle barGas_ = 0;
    bool buildBars();
    void *iasTemp_ = nullptr, *iasOut_ = nullptr;
    size_t iasTempSize_ = 0, iasOutSize_ = 0;
    YBody* dBodies_ = nullptr;
    std::vector<YBody> bodies_;
    void freeWorld();
    void buildIAS();
};
