// OptiX renderer: NVRTC-compiled device programs (hot reload), acceleration structures,
// accumulation buffers, timed launches.
#pragma once
#include "scenes/altinsaat/denoiser.h"
#include "scenes/altinsaat/photons.h"
#include "scenes/altinsaat/altinsaat_world.h"
#include "scenes/altinsaat/altinsaat_params.h"

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
    void updateSpheres(const Scene& scene);   // moved spheres: new centres, sphere GAS and IAS rebuilt
    void updateSky();                     // after the sun moved
    void resetAccumulation() { frame_ = 0; params_.photonFrame = 0; }
    float bloomStrength = 0.04f;
    float photonRadius0 = 0.01f;          // m, first photon map's gather radius; shrinks as frames accumulate
    unsigned int photonsPerFrame = 1u << 20;
    float beamRadius0 = 0.02f;            // m, photon beam blur radius of the first frame
    bool liveRadius = false;              // live mode: radii stay fixed (no progressive shrinking)

    void renderFrame();                   // one launch: sppPerLaunch samples per pixel
    void tonemap();                       // accum -> output (uchar4, top row first)
    // Runs the AI denoiser on the current accumulation; the next tonemap() shows it. blend 0: fully denoised.
    void denoise(float blend, bool caustics = false);
    void clearDenoised() { params_.denoised = nullptr; params_.denoisedCaustic = nullptr; }
    void reuseDenoised() { if (denoiserReady_) params_.denoised = denoiser_.output(); }
    float denoiseMs() const {
        return halfResolution() ? upscaleDenoiser_.lastMs : (useTemporalDenoiser ? temporalDenoiser_.lastMs : denoiser_.lastMs);
    }
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
    void bakeNoise();

    std::string root_;
    std::string deviceName_;
    unsigned int width_ = 0, height_ = 0;
    unsigned int frame_ = 0;
    OptixDeviceContext context_ = nullptr;
    OptixModule module_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    OptixShaderBindingTable sbt_{};
    CUdeviceptr rgRender_ = 0, rgSky_ = 0, rgTonemap_ = 0, rgBake_ = 0, rgPhotons_ = 0, rgComposite_ = 0, missRecords_ = 0, hitRecords_ = 0;
    static const int kBloomLevels = 7;
    float4* bloomLevels_[kBloomLevels] = {};
    float4* bloomAccum_[kBloomLevels] = {};
    PhotonGrid photonGrid_;
    Denoiser denoiser_, temporalDenoiser_, upscaleDenoiser_;
    bool denoiserReady_ = false, temporalReady_ = false, upscaleReady_ = false;
public:
    bool useTemporalDenoiser = false;     // live mode
    // live mode at half resolution, upscaled 2x by the AI denoiser (TEMPORAL_UPSCALE2X)
    void setHalfResolution(bool half);
    bool halfResolution() const { return params_.renderWidth != width_; }
    void resetDenoiseHistory() { temporalDenoiser_.resetHistory(); upscaleDenoiser_.resetHistory(); }
private:
    void tracePhotons();
    void buildBeams();
    void *beamBoxes_ = nullptr, *beamTemp_ = nullptr, *beamGas_ = nullptr;
    size_t beamTempSize_ = 0, beamOutSize_ = 0;
    cudaArray_t noiseArray_ = nullptr;
    cudaTextureObject_t noiseTexObj_ = 0;
    CUdeviceptr dParams_ = 0;
    cudaStream_t stream_ = nullptr;
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr;
    Params params_{};
    PassTimes times_;

    // device buffers
    std::vector<void*> sceneBuffers_;
    void *gasDust_ = nullptr;
    void *gasTri_ = nullptr, *gasSph_ = nullptr, *ias_ = nullptr, *instances_ = nullptr, *aabbs_ = nullptr;
};
