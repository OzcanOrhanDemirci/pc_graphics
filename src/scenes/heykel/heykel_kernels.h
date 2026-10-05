// Scene 6 "Heykel": the CUDA kernels. Deterministic, as scene 5's are: no random numbers anywhere. A sample index s is
// the same sub-pixel position and the same point on the lamp's disc (and on the lens) for every pixel, every frame:
// a still view is the same picture bit for bit, a moving light changes the shading smoothly, and while the view is
// still the samples add only what more of them can add (edges, the penumbra), never grain. Sample 0 is the pixel's
// centre and the lamp's centre. Özcan 2026-10-05: "görüntüyü taneciksiz [...] ve pır pırsız yapman. Ada da 1 er örnekle
// bile bunu başarmıştın" (the path tracer's grain and its restart on every change made the geometry unreadable).
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
    int maxSteps2 = 200;          // the shadow rays (out of steps near something: a hit, dark)
    float shadowSlope = 2.0f;     // their threshold, times the camera's
    float shadowReach = 1e30f;    // how far the lamp's shadow ray looks for an occluder (m; the view scales it)
    int softKey = 1;              // the lamp's shadow: 1 estimated (smooth as it moves), 0 traced from the disc's points
    float normalScale = 1.0f;     // the normal's differences, times the surface threshold (wider: calmer shading)
    float filterRadius = 1.5f;    // the reconstruction: Blackman-Harris over this many pixels (scene 5's)
    float aperture = 0.0f, focusDist = 1.0f;   // the lens's radius (m; 0: a pinhole) and its focus along the view
    float tmax = 30.0f;
    HeykelLight key;              // the spot pointed at what was clicked
    // the eye light: a ring light on the lens. Every point the camera sees sees it, so it casts no shadow the camera
    // could see and needs no shadow ray. Its fall with distance softened near the lens (an art setting, by name):
    // E = eyeE (d² + c²) / (r² + c²), d the focus's distance
    float3 eyePos{0, 0, 0}, eyeColor{1, 1, 1};
    float eyeE = 0.0f, eyeD = 1.0f, eyeC = 0.5f;
    // the room: a dim light from high above; ambient: what the white surfaces throw at each other, by the occlusion
    float fillLe = 0.02f;
    float3 fillColor{1, 1, 1};
    float ambient = 0.0f;
    float3 albedo[3] = {{0.8f, 0.8f, 0.8f}, {0.2f, 0.2f, 0.2f}, {0.04f, 0.04f, 0.04f}};   // sculpture, plinth, floor
    float exposure = 1.0f;
};

// adds samples s = first .. first+count-1 (the fixed pattern) to accum (rgb weighted, w the weights), on the rows
// y = stripe (mod stripes) only (a sample too slow for one frame is spread over several)
void heykelSamples(const HeykelView& v, float4* accum, int first, int count, int stripe, int stripes, cudaStream_t s);
// the picture: accum's weighted mean, exposure, AgX, sRGB, a fixed dither
void heykelResolve(const HeykelView& v, const float4* accum, uchar4* out, cudaStream_t s);
