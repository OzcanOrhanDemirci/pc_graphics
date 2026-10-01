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
    void updateWorld(PhysWorld& w);                    // transforms from the physics, top level rebuilt
    void setDamage(const std::vector<float>& perBody); // display tint, per body (index = body)
    void render(unsigned int spp, bool accumulate);    // adds spp samples (accumulate) or starts afresh
    void download(std::vector<uchar4>& out);
    YikimParams& params() { return p_; }
    float lastMs = 0.0f;
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
    CUdeviceptr rg_ = 0, miss_ = 0, hit_ = 0, dParams_ = 0;
    cudaStream_t stream_ = nullptr;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr;
    YikimParams p_{};
    std::vector<void*> worldBuffers_;
    OptixInstance* dInstances_ = nullptr;
    int instanceCount_ = 0;
    void *iasTemp_ = nullptr, *iasOut_ = nullptr;
    size_t iasTempSize_ = 0, iasOutSize_ = 0;
    YBody* dBodies_ = nullptr;
    std::vector<YBody> bodies_;
    void freeWorld();
    void buildIAS();
};
