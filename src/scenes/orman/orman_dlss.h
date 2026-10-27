// Scene 11 "Orman": NVIDIA DLSS Ray Reconstruction (DLSS-RR) through NGX's CUDA path, in DLAA mode (no upscaling).
// The forest's leaves and twigs are finer than a pixel: a few fixed samples a pixel cannot hold them still while the eye
// moves. Each frame instead takes one sample a pixel at the next point of a Halton sequence inside it, and DLSS-RR
// gathers the frames along the motion vectors (and the alpha thresholds, the sun's disk and the mist's steps that turn
// with the sequence). Built only when third_party/dlss is there (third_party/fetch.sh); without it the scene keeps its
// fixed samples. This software contains source code provided by NVIDIA Corporation (the SDK's helper headers).
#pragma once
#include <cuda.h>
#include <cuda_runtime.h>

#include <string>

struct OrmanDlssFrame {
    cudaTextureObject_t color, albedo, specular, normal, rough, depth, motion, hitT;   // the guides (point sampled)
    cudaSurfaceObject_t output;
    float jitterX, jitterY;            // DLSS's sign: the image's shift, pixels
    bool reset;                        // the history is wrong (a jump of the eye, new settings)
    float worldToView[16], viewToClip[16];   // row-major, row vectors (v' = v M)
    float frameMs;
};

class OrmanDlss {
public:
    std::string error;                 // why it is off (empty when on)
    bool init(int width, int height, CUstream stream);
    bool ready() const { return handle_ != nullptr; }
    bool evaluate(const OrmanDlssFrame& f);
    void release();

private:
    void* handle_ = nullptr;
    void* params_ = nullptr;
    bool ngx_ = false;
    unsigned int w_ = 0, h_ = 0;
};
