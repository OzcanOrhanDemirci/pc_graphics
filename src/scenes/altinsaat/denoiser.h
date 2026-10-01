// OptiX AI denoiser (AOV model, albedo + normal guides). The caustic buffer stays out of it: photons are
// added after denoising, so the denoiser cannot smear the rainbow or the water patterns.
#pragma once
#include "core/bloom.h"
#include "scenes/altinsaat/altinsaat_params.h"

#include <cuda_runtime.h>
#include <optix.h>

class Denoiser {
public:
    // temporal: OPTIX_DENOISER_MODEL_KIND_TEMPORAL_AOV (live mode, static camera: zero flow)
    // width/height: the input; with upscale the output is twice as large (TEMPORAL_UPSCALE2X when temporal)
    bool create(OptixDeviceContext ctx, unsigned int width, unsigned int height, bool temporal = false, bool upscale = false);
    void resetHistory() { history_ = false; }
    void destroy();   // the OptiX denoiser and every buffer (before its device context goes)
    bool temporal() const { return temporal_; }
    // accum/caustic/albedo/normal as accumulated by __raygen__render; output: linear sRGB (white balanced),
    // caustics excluded. blend: 0 fully denoised, 1 untouched.
    void run(const Params& p, float blend, cudaStream_t stream, bool caustics = false);
    float4* output() const { return out_; }
    float4* causticOutput() const { return outC_; }
    float lastMs = 0.0f;

private:
    OptixDenoiser denoiser_ = nullptr;
    unsigned int w_ = 0, h_ = 0;
    void *state_ = nullptr, *scratch_ = nullptr;
    size_t stateSize_ = 0, scratchSize_ = 0;
    float4 *in_ = nullptr, *albedo_ = nullptr, *normal_ = nullptr, *out_ = nullptr, *inC_ = nullptr, *outC_ = nullptr;
    float* intensity_ = nullptr;
    float* avgColor_ = nullptr;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr;
    bool temporal_ = false, history_ = false, upscale_ = false;
    unsigned int ow_ = 0, oh_ = 0;
    float4 *prevOut_ = nullptr, *flow_ = nullptr;
    void *guideA_ = nullptr, *guideB_ = nullptr;
    size_t guidePixel_ = 0;
};

// CUDA kernel launcher (kernels.cu): builds the denoiser's input and guide images from the accumulation.
void launchDenoisePrep(const Params& p, float4* rgb, float4* albedo, float4* normal, float4* caustic, cudaStream_t stream);
