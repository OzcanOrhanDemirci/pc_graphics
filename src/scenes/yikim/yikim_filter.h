// Scene 3 "Yikim": the live window's own denoiser, a light SVGF (Schied et al. 2017) for a scene of rigid bodies.
// The render writes, per pixel, the sums of beauty, albedo and normal, and from its first (pixel-centre) ray the
// distance, the body seen and where that point was a frame ago. Here: (1) temporal: the lighting (beauty divided by
// albedo) is carried along each point from the last frame, checked by body, normal and distance, and averaged with the
// new samples; its luminance moments give a variance; (2) spatial: a few a-trous passes that stop at other bodies,
// other normals, depth steps and lighting edges the variance cannot explain; (3) times albedo, tone mapped.
// The OptiX AI denoiser looks better still but took 9.7 ms at 1080p and slowed the physics by 9 ms (measured).
#pragma once
#include <cuda_runtime.h>

struct YFilterFrame {
    unsigned int width, height;
    const float4* accum;      // sums over `samples` samples
    const float4* albedoAcc;
    const float4* normalAcc;
    unsigned int samples;
    unsigned int frameSpp;    // samples added this frame (fewer than `samples`: the world stood still, they accumulate)
    const float4* motion;     // xy: pixel movement from the last frame; z: the point's distance from last frame's camera
    const float4* gbuf;       // x: distance; y: what is seen (body + 1, 0 ground, -1 sky)
    float exposure;
    unsigned int seed;
    uchar4* output;
};

class YFilter {
public:
    bool create(unsigned int width, unsigned int height);
    void destroy();
    void reset() { history_ = false; }   // a new world: nothing to carry over
    void run(const YFilterFrame& f, cudaStream_t stream);
    int passes = 4;                      // a-trous passes (steps 1, 2, 4, 8)

private:
    unsigned int w_ = 0, h_ = 0;
    int cur_ = 0;
    bool history_ = false;
    float4 *histE_[2] = {}, *histM_[2] = {}, *geo_[2] = {};   // per frame parity; geo: distance, body, normal (octahedral)
    float4 *alb_ = nullptr, *a_ = nullptr, *b_ = nullptr;
};
