#include "denoiser.h"

#include <optix_stubs.h>

#include <cstdio>
#include <utility>

#define OPTIX_TRY(x)                                                                                    \
    do {                                                                                                \
        OptixResult r_ = (x);                                                                           \
        if (r_ != OPTIX_SUCCESS) {                                                                      \
            std::fprintf(stderr, "OptiX %s:%d %s: %s\n", __FILE__, __LINE__, #x, optixGetErrorString(r_)); \
            return false;                                                                               \
        }                                                                                               \
    } while (0)

static OptixImage2D image(float4* data, unsigned int w, unsigned int h) {
    OptixImage2D im{};
    im.data = CUdeviceptr(data);
    im.width = w;
    im.height = h;
    im.rowStrideInBytes = w * sizeof(float4);
    im.pixelStrideInBytes = sizeof(float4);
    im.format = OPTIX_PIXEL_FORMAT_FLOAT4;
    return im;
}

bool Denoiser::create(OptixDeviceContext ctx, unsigned int width, unsigned int height, bool temporal, bool upscale) {
    w_ = width;
    h_ = height;
    temporal_ = temporal;
    upscale_ = upscale;
    ow_ = upscale ? 2 * w_ : w_;
    oh_ = upscale ? 2 * h_ : h_;
    OptixDenoiserOptions o{};
    o.guideAlbedo = 1;
    o.guideNormal = 1;
    const OptixDenoiserModelKind kind = upscale ? (temporal ? OPTIX_DENOISER_MODEL_KIND_TEMPORAL_UPSCALE2X : OPTIX_DENOISER_MODEL_KIND_UPSCALE2X)
                                                : (temporal ? OPTIX_DENOISER_MODEL_KIND_TEMPORAL_AOV : OPTIX_DENOISER_MODEL_KIND_AOV);
    OPTIX_TRY(optixDenoiserCreate(ctx, kind, &o, &denoiser_));
    OptixDenoiserSizes sizes{};
    OPTIX_TRY(optixDenoiserComputeMemoryResources(denoiser_, w_, h_, &sizes));
    std::printf("denoiser %s%s: input %ux%u -> output %ux%u, state %.0f MB, scratch %.0f MB, guide px %zu B\n", temporal ? "temporal " : "",
                upscale ? "upscale2x" : "aov", w_, h_, ow_, oh_, sizes.stateSizeInBytes / 1048576.0, sizes.withoutOverlapScratchSizeInBytes / 1048576.0,
                size_t(sizes.internalGuideLayerPixelSizeInBytes));
    stateSize_ = sizes.stateSizeInBytes;
    scratchSize_ = sizes.withoutOverlapScratchSizeInBytes;
    const size_t px = size_t(w_) * h_ * sizeof(float4), opx = size_t(ow_) * oh_ * sizeof(float4);
    if (cudaMalloc(&state_, stateSize_) || cudaMalloc(&scratch_, scratchSize_) || cudaMalloc(&in_, px) || cudaMalloc(&albedo_, px) ||
        cudaMalloc(&normal_, px) || cudaMalloc(&out_, opx) || cudaMalloc(&inC_, px) || cudaMalloc(&outC_, opx) || cudaMalloc(&intensity_, sizeof(float)) ||
        cudaMalloc(&avgColor_, 3 * sizeof(float)))
        return false;
    if (temporal_) {
        guidePixel_ = sizes.internalGuideLayerPixelSizeInBytes;
        const size_t gsz = guidePixel_ * ow_ * oh_;   // internal guides live at the output size
        if (cudaMalloc(&prevOut_, opx) || cudaMalloc(&flow_, px) || cudaMalloc(&guideA_, gsz) || cudaMalloc(&guideB_, gsz))
            return false;
        cudaMemset(flow_, 0, px);              // static camera: nothing moves on screen except the scene itself
        cudaMemset(guideA_, 0, gsz);
        cudaMemset(guideB_, 0, gsz);
    }
    OPTIX_TRY(optixDenoiserSetup(denoiser_, nullptr, w_, h_, CUdeviceptr(state_), stateSize_, CUdeviceptr(scratch_), scratchSize_));
    cudaEventCreate(&e0_);
    cudaEventCreate(&e1_);
    return true;
}

void Denoiser::run(const Params& p, float blend, cudaStream_t stream, bool caustics) {
    cudaEventRecord(e0_, stream);
    launchDenoisePrep(p, in_, albedo_, normal_, inC_, stream);
    OptixDenoiserLayer layers[2] = {};
    OptixDenoiserLayer& layer = layers[0];
    layer.input = image(in_, w_, h_);
    layer.output = image(out_, ow_, oh_);
    layer.type = OPTIX_DENOISER_AOV_TYPE_BEAUTY;
    layers[1].input = image(inC_, w_, h_);       // the caustics as a second layer, same guides (live mode)
    layers[1].output = image(outC_, ow_, oh_);
    layers[1].type = OPTIX_DENOISER_AOV_TYPE_SPECULAR;
    OptixDenoiserGuideLayer guide{};
    guide.albedo = image(albedo_, w_, h_);
    guide.normal = image(normal_, w_, h_);
    if (temporal_) {
        if (!history_) cudaMemsetAsync(prevOut_, 0, size_t(ow_) * oh_ * sizeof(float4), stream);
        layer.previousOutput = image(prevOut_, ow_, oh_);
        guide.flow = image(flow_, w_, h_);
        guide.flow.format = OPTIX_PIXEL_FORMAT_FLOAT2;
        guide.flow.pixelStrideInBytes = sizeof(float4);
        auto internal = [&](void* p) {
            OptixImage2D im{};
            im.data = CUdeviceptr(p);
            im.width = ow_;
            im.height = oh_;
            im.pixelStrideInBytes = unsigned(guidePixel_);
            im.rowStrideInBytes = unsigned(guidePixel_ * ow_);
            im.format = OPTIX_PIXEL_FORMAT_INTERNAL_GUIDE_LAYER;
            return im;
        };
        guide.previousOutputInternalGuideLayer = internal(guideA_);
        guide.outputInternalGuideLayer = internal(guideB_);
    }
    optixDenoiserComputeIntensity(denoiser_, stream, &layer.input, CUdeviceptr(intensity_), CUdeviceptr(scratch_), scratchSize_);
    optixDenoiserComputeAverageColor(denoiser_, stream, &layer.input, CUdeviceptr(avgColor_), CUdeviceptr(scratch_), scratchSize_);
    OptixDenoiserParams dp{};
    dp.hdrIntensity = CUdeviceptr(intensity_);
    dp.hdrAverageColor = CUdeviceptr(avgColor_);
    dp.blendFactor = blend;
    dp.temporalModeUsePreviousLayers = temporal_ && history_ ? 1u : 0u;
    const OptixResult r = optixDenoiserInvoke(denoiser_, stream, &dp, CUdeviceptr(state_), stateSize_, &guide, layers, caustics ? 2u : 1u, 0, 0,
                                              CUdeviceptr(scratch_), scratchSize_);
    if (r != OPTIX_SUCCESS) std::fprintf(stderr, "optixDenoiserInvoke: %s\n", optixGetErrorString(r));
    if (temporal_) {   // this output and its internal guide become the next frame's history
        cudaMemcpyAsync(prevOut_, out_, size_t(ow_) * oh_ * sizeof(float4), cudaMemcpyDeviceToDevice, stream);
        std::swap(guideA_, guideB_);
        history_ = true;
    }
    cudaEventRecord(e1_, stream);
    cudaEventSynchronize(e1_);
    cudaEventElapsedTime(&lastMs, e0_, e1_);
}
