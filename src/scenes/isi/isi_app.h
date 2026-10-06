// Scene 9 "Isı" (draft): one run's state, shared by the live scene and the command-line tools. The world is rough on
// purpose: the composition and how it feels to stand and walk in it, before any detail (Özcan 2026-10-06).
#pragma once
#include "core/config.h"
#include "scenes/isi/isi_kernels.h"

#include <string>
#include <vector>

struct IsiCam {
    float3 pos{0.0f, 25.7f, 60.5f};
    float yaw = 0.0f, pitch = 0.0f;   // yaw 0 looks along -z, grows to the left; pitch up positive (radians)
    float fovY = 50.0f;               // degrees
};

class IsiApp {
public:
    std::string root;
    std::vector<std::string> overrides;
    Config cfg;
    int width = 2560, height = 1440;
    IsiCam cam;
    // what is awake: the targets (the panel's switches) and the eased values the picture shows
    float gpu = 0.8f, cpuOn = 0.0f, ramOn = 0.0f, ssdOn = 0.0f;
    float cpu = 0.0f, ram = 0.0f, ssd = 0.0f;
    float energy = 0.0f;              // the draft's slider: the world's tint for the energy spent
    float gpuWatts = 0.0f, gpuLimit = 0.0f, gpuTemp = 0.0f;   // the card's own readings (NVML), when there are any
    float ev = 0.0f;
    double time = 0.0, fanAngle = 0.0, flow = 0.0;
    float frameMs = 0.0f;
    unsigned int lastBad = 0;         // pixels zeroed for not being finite in the last frame
    uchar4* output = nullptr;         // device: the last picture

    static int viewCount();
    static IsiCam view(int i);        // the composed views: 0 the balcony, 1 under the wall, 2 the tower's top, 3 the pump
    static const char* viewName(int i, bool turkish);

    void loadConfig();
    void init();                      // buffers and the beams
    void release();
    void step(float dt);              // the switches eased, the fans and the lava moved on
    void snapAwake();                 // the eased values straight to their targets (stills)
    void render();
    bool save(const std::string& pathNoExt);

private:
    IsiView makeView() const;
    float4* hdr_ = nullptr;
    static const int kLevels = 6;
    float4* levels_[kLevels] = {};
    float4* accum_[kLevels] = {};
    IsiBeam* beams_ = nullptr;
    int beamCount_ = 0;
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr;
    unsigned int* bad_ = nullptr;
};

int runIsiCli(int argc, char** argv);
