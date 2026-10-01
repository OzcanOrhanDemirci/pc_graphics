// Scene 2 "Fener": OptiX pipeline (NVRTC hot reload of src/device/fener.cu), acceleration structures for the
// instanced world, the per-frame passes and their timings.
#pragma once
#include "fener/fener_scene.h"
#include "shared/fener_params.h"

#include <cuda_runtime.h>
#include <optix.h>

#include <string>
#include <vector>

struct FenerTimes {      // milliseconds of the last frame's passes
    float surface = 0, volume = 0, resolve = 0, compose = 0, taa = 0, glow = 0, final = 0;
    float total() const { return surface + volume + resolve + compose + taa + glow + final; }
};

class FenerRenderer {
public:
    bool init(unsigned int width, unsigned int height, const std::string& root, unsigned int volScale = 1);
    void shutdown();
    bool compileDevice(std::string& log);
    void uploadScene(const FenerScene& scene);
    void renderFrame();                    // all passes; output in params().output
    void resetHistory() { params_.frame = 0; }
    void download(std::vector<uchar4>& out);
    void downloadHdr(std::vector<float4>& out);

    FenerParams& params() { return params_; }
    float meteredLog2() const { return metered_; }   // mean log2 luminance of the last frame (before exposure)
    const FenerTimes& times() const { return times_; }
    std::string deviceName() const { return deviceName_; }

private:
    void launch(CUdeviceptr raygen, unsigned int w, unsigned int h, float* ms);

    std::string root_, deviceName_;
    unsigned int width_ = 0, height_ = 0;
    OptixDeviceContext context_ = nullptr;
    OptixModule module_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    OptixShaderBindingTable sbt_{};
    CUdeviceptr rgSurface_ = 0, rgVolume_ = 0, rgResolve_ = 0, rgCompose_ = 0, rgTaa_ = 0, rgFinal_ = 0, missRecord_ = 0, hitRecord_ = 0;
    CUdeviceptr dParams_ = 0;
    cudaStream_t stream_ = nullptr;
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr;
    FenerParams params_{};
    FenerTimes times_;
    float metered_ = 0.0f;
    std::vector<float4> meter_;
    std::vector<void*> sceneBuffers_;
    static const int kWideLevels = 7, kNarrowLevels = 4;
    float4* wideLevels_[kWideLevels] = {};
    float4* wideAccum_[kWideLevels] = {};
    float4* narrowLevels_[kNarrowLevels] = {};
    float4* narrowAccum_[kNarrowLevels] = {};
};
