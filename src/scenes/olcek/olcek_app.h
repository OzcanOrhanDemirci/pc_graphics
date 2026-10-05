// Scene 7 "Ölçek": the state of a run (settings, the scale engine, the GPU buffers) shared by the live scene
// (olcek_live.cpp) and the command-line tools (olcek_app.cpp).
#pragma once
#include "core/config.h"
#include "scenes/olcek/olcek_kernels.h"
#include "scenes/olcek/olcek_tex.h"
#include "scenes/olcek/olcek_world.h"

#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

struct OlcekApp {
    Config cfg;
    std::string root = PROJECT_ROOT;
    std::vector<std::string> overrides;
    olcek::Scale sc;
    olcek::OlcekView view;
    std::vector<olcek::GSplat> splats;
    int samples = 4;

    // GPU
    float4* accum = nullptr;
    float* depth = nullptr;
    uchar4* output = nullptr;
    olcek::GSplat* dSplats = nullptr;
    int splatCap = 0;
    // the maps (Ö1; tools/olcek_veri.py): missing ones fall back to the placeholders
    olcek::Texture world, zonal, region, bay, moon, cloudWorld, cloudRegion;
    olcek::Field shore;
    std::string mapsLog;             // what was loaded (or not), for the loading screen and the console
    float frameMs = 0.0f;            // the GPU's part of the last frame
    double hostMs = 0.0;             // the host's (the tree, the bodies, the points)

    void loadConfig();
    void init();                     // the engine and the buffers
    void loadMaps();                 // the maps from assets/olcek (seconds: 1.9 GB)
    void release();
    // a frame at the engine's s: the camera, the primitives, the picture (output)
    void render();
    void save(const std::string& path);
    // the picture's luminance at a quarter of the resolution (the jump test's), into lum (w/4 x h/4)
    void luminanceQuarter(std::vector<float>& lum);
};

int runOlcekCli(int argc, char** argv);
