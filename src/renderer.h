// OptiX renderer: NVRTC-compiled device programs (hot reload), acceleration structures,
// accumulation buffers, timed launches.
#pragma once
#include "scene.h"
#include "shared/params.h"

#include <cuda_runtime.h>
#include <optix.h>

#include <string>
#include <vector>

struct PassTimes {      // milliseconds of the last launch of each pass
    float render = 0, photons = 0, grid = 0, tonemap = 0, sky = 0;
};

class Renderer {
public:
    bool init(unsigned int width, unsigned int height, const std::string& root);
    void shutdown();

    // Compiles src/device/programs.cu with NVRTC and rebuilds the pipeline. On failure the previous
    // pipeline stays and `log` says why.
    bool compileDevice(std::string& log);
    void uploadScene(const Scene& scene);
    void updateSky();                     // after the sun moved
    void resetAccumulation() { frame_ = 0; }

    void renderFrame();                   // one launch: sppPerLaunch samples per pixel
    void tonemap();                       // accum -> output (uchar4, top row first)
    void downloadOutput(std::vector<uchar4>& out);
    void downloadAccum(std::vector<float4>& out, bool caustic = false);
    unsigned int nanCount();

    Params& params() { return params_; }
    unsigned int frame() const { return frame_; }
    unsigned int samplesPerPixel() const { return frame_ * params_.sppPerLaunch; }
    const PassTimes& times() const { return times_; }
    std::string deviceName() const { return deviceName_; }

private:
    void launch(CUdeviceptr raygenRecord, unsigned int w, unsigned int h, float* ms);
    void buildAccel(const Scene& scene);

    std::string root_;
    std::string deviceName_;
    unsigned int width_ = 0, height_ = 0;
    unsigned int frame_ = 0;
    OptixDeviceContext context_ = nullptr;
    OptixModule module_ = nullptr, sphereModule_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    OptixShaderBindingTable sbt_{};
    CUdeviceptr rgRender_ = 0, rgSky_ = 0, rgTonemap_ = 0, missRecords_ = 0, hitRecords_ = 0;
    CUdeviceptr dParams_ = 0;
    cudaStream_t stream_ = nullptr;
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr;
    Params params_{};
    PassTimes times_;

    // device buffers
    std::vector<void*> sceneBuffers_;
    void *gasTri_ = nullptr, *gasSph_ = nullptr, *ias_ = nullptr, *instances_ = nullptr;
};
