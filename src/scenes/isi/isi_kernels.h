// Scene 9 "Isı" (draft): the CUDA kernels. A computer seen from inside, grown to a landscape: the graphics card a wall of
// three fans, the memory a city of towers, the processor's pump a temple, the power supply a far mountain, the case's
// perforated panel the sky. Rough on purpose: the composition and the feel of walking in it, not the detail (Özcan
// 2026-10-06: "taslak olduğu için her şey kaba ve detaysız olur. Ama kompozisyonu görürüz").
// Deterministic like scenes 5 and 6: no random numbers; every pixel takes the same fixed samples every frame. The haze,
// the lights' glow in it and the beams from the sky are closed-form integrals along the ray, not marched: no grain.
#pragma once
#include "shared/vec.h"

#include <cuda_runtime.h>

// a beam of light from a hole in the sky: from `top` along `dir` (unit, downwards) for `len` metres
struct IsiBeam {
    float3 top{0, 0, 0}, dir{0, -1, 0};
    float len = 0.0f, sigma = 1.0f, le = 1.0f;
};
constexpr int kIsiMaxBeams = 320;

// a light in the haze: a hot part's glow (its colour times its strength; 0: off)
struct IsiLight {
    float3 pos{0, 0, 0}, color{0, 0, 0};
    float soft = 4.0f;   // the core's radius (m): the falloff 1 / (d^2 + soft^2), and the glow's
};
constexpr int kIsiLights = 8;

struct IsiView {
    int width = 0, height = 0;
    float3 camPos{0, 0, 0}, camRight{1, 0, 0}, camUp{0, 1, 0}, camFwd{0, 0, -1};
    float tanHalf = 0.45f, pixAngle = 0.0f;
    float time = 0.0f;
    float fanAngle = 0.0f;   // the fans' turn (radians)
    float flow = 0.0f;       // the lava's travel along the pipes (m)
    float gpu = 0.7f, cpu = 0.0f, ram = 0.0f, ssd = 0.0f;   // how awake each part is, 0..1 (eased)
    float energy = 0.0f;     // the energy spent so far, as the world's tint: 0 cold .. 1 amber
    float gauge[7] = {};     // the console's needles, 0..1: GPU, CPU, RAM, SSD, MOSFETs, power supply, total
    float exposure = 1.0f;
    int base = 2, edge = 4;  // fixed samples a pixel, and more where they disagree
    float contrast = 0.06f;
    int maxSteps = 320;
    float tmax = 4000.0f;
    int beams = 0;
    float fogDensity = 0.0016f, fogHeight = 130.0f;
    float beamLe = 1.0f, glowLe = 1.0f, bloom = 0.05f, vignette = 0.25f;
    float skyHole = 30.0f;   // the panel's holes: their spacing (m)
    float fogLe = 1.0f, skyLe = 1.0f;   // the haze's own colour and the sky's light on surfaces, scaled
    IsiLight light[kIsiLights];
    IsiBeam* beam = nullptr;   // device array, `beams` long
    unsigned int* badCount = nullptr;   // device: pixels that came out not finite (zeroed), for the tools
};

void isiFindBeamLengths(IsiBeam* beams, int n, cudaStream_t s);   // each beam down to the first surface it meets
void isiFrame(const IsiView& v, float4* hdr, cudaStream_t s);
void isiResolve(const IsiView& v, const float4* hdr, const float4* bloomHalf, uchar4* out, cudaStream_t s);
