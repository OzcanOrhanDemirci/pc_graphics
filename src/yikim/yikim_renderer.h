// Scene 3 "Yikim": the OptiX side. Every body is an instance of its own hull (a GAS built once, in the body frame);
// the physics writes the instance transforms straight into the instance buffer on the GPU every frame and the
// top-level structure is rebuilt. Device program src/device/yikim.cu (NVRTC, F5 recompiles).
#pragma once
#include "shared/yikim_params.h"
#include "yikim/phys.h"
#include "yikim/yikim_filter.h"

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
    void render(unsigned int spp, bool accumulate);    // adds spp samples (accumulate) or starts afresh; denoises if on
    void download(std::vector<uchar4>& out);
    void downloadTo(uchar4* hostPinned);                 // width * height pixels
    // the body under render pixel (x, y): index, or 0 the ground, -1 the sky; hit point and distance in hit
    int pick(float x, float y, float4& hit);
    YikimParams& params() { return p_; }
    float lastMs = 0.0f;
    float barRadius = 0.01f, barGap = 0.005f, barStub = 0.25f;   // the reinforcing bars (m), see PhysWorld::writeBars
    bool showBars = true;
    // 0 the raw image: grain where the sky light and the sun's disc are sampled with a few rays a frame while things move;
    // 1 our own filter (yikim_filter.h), ~1 ms; 2 the OptiX AI denoiser (temporal AOV with albedo, normal and motion
    // guides): cleaner still, but 9.7 ms at 1080p, and it shares the GPU with the physics (measured: 40 fps).
    int denoiseMode = 0;
    void filterPasses(int n) { filter_.passes = n; }
    float denoiseMs = 0.0f;
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
    CUdeviceptr rg_ = 0, rgPick_ = 0, rgPrep_ = 0, rgTone_ = 0, miss_ = 0, hit_ = 0, dParams_ = 0;
    int* dPickBody_ = nullptr;
    float4* dPickHit_ = nullptr;
    cudaStream_t stream_ = nullptr;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr, evXf_ = nullptr, eMid_ = nullptr;
    YCamera prevCam_{};
    float2 prevShift_{};
    float prevGroundY_ = 0.0f;
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
    // motion and the denoiser
    float* xfPrev_ = nullptr;      // the instances' transforms a frame ago (12 floats each)
    bool havePrev_ = false, havePrevCam_ = false;
    OptixDenoiser dn_ = nullptr;
    void *dnState_ = nullptr, *dnScratch_ = nullptr, *dnGuideA_ = nullptr, *dnGuideB_ = nullptr;
    size_t dnStateSize_ = 0, dnScratchSize_ = 0, dnGuidePixel_ = 0;
    float4 *dnOut_ = nullptr, *dnPrevOut_ = nullptr;
    float* dnIntensity_ = nullptr;
    float* dnAvg_ = nullptr;
    bool dnHistory_ = false;
    YFilter filter_;
    bool createDenoiser();
    void runDenoiser();
    void *iasTemp_ = nullptr, *iasOut_ = nullptr;
    size_t iasTempSize_ = 0, iasOutSize_ = 0;
    YBody* dBodies_ = nullptr;
    std::vector<YBody> bodies_;
    void freeWorld();
    void buildIAS();
};
