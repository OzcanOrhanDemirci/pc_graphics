// Scene 6 "Heykel": the CUDA kernels. A frame is either a preview (moving: one ray a pixel through its centre, direct
// light with an estimated soft shadow and occlusion, no noise) or samples of a path tracer that accumulate while
// everything is still (the area light's true shadow, light bouncing into the hollows, sub-pixel samples); the
// resolve blends the preview into the accumulation over the first samples and tone maps.
#pragma once
#include "scenes/heykel/heykel_de.h"

#include <cuda_runtime.h>

// a sphere light behind a spot's cone (coneOuter < -1: no cone)
struct HeykelLight {
    float3 pos{0, 1, 1}, axis{0, -1, 0};   // the axis: from the light towards its target
    float r = 0.05f, le = 0.0f;            // radius (m), radiance; le 0: off
    float coneOuter = -2.0f, coneInner = -1.0f;
    float3 color{1, 1, 1};
};

struct HeykelView {   // one frame's world, camera and light, by value to the kernels
    hk::World w;
    int width = 0, height = 0;
    float3 camPos{0, 0, 0}, camRight{1, 0, 0}, camUp{0, 1, 0}, camFwd{0, 0, -1};
    float tanHalf = 0.27f;        // tan(vertical field of view / 2)
    float pixAngle = 0.0f;        // 2 tanHalf / height: a pixel's angle
    float detail = 0.5f;          // the surface threshold, in pixels
    int maxSteps = 400;           // the camera's rays
    int maxSteps2 = 200;          // the shadow and bounce rays (out of steps near something: a hit, dark)
    float shadowSlope = 2.0f, bounceSlope = 4.0f;   // their thresholds, times the camera's (they need no pixel's precision)
    float filterSigma = 0.45f;    // the reconstruction's Gaussian, pixels
    float tmax = 30.0f;
    // 0 the key: a spot pointed at what was clicked; 1 the fill on the camera (a macro photographer's ring light: it
    // reaches every surface the camera sees, deep in the lattice too, and fades with the square of the distance)
    HeykelLight light[2];
    // the fill: a dim dome, brighter above
    float fillLe = 0.02f;
    float3 fillColor{1, 1, 1};
    float3 albedo[3] = {{0.8f, 0.8f, 0.8f}, {0.2f, 0.2f, 0.2f}, {0.04f, 0.04f, 0.04f}};   // sculpture, plinth, floor
    float exposure = 1.0f;
    int sample = 0;               // the accumulation's first sample index this frame
    int bounces = 2;
};

// one ray a pixel: linear colour, w = the hit's distance (0: none)
void heykelPreview(const HeykelView& v, float4* preview, cudaStream_t s);
// adds `samples` path-traced samples a pixel to accum (w counts them), on the rows y = stripe (mod stripes) only (a
// sample too slow for one frame is spread over several)
void heykelPath(const HeykelView& v, float4* accum, int samples, int stripe, int stripes, cudaStream_t s);
// the picture: the preview blended into the accumulation's mean, exposure, AgX, sRGB, a fixed dither
void heykelResolve(const HeykelView& v, const float4* preview, const float4* accum, uchar4* out, cudaStream_t s);
// the mean of accum into a linear float RGBA image (for EXR and measurements)
void heykelLinear(const HeykelView& v, const float4* preview, const float4* accum, float4* out, cudaStream_t s);
