// Scene 5 "Koy": the state of a run (settings, optics, the sea surface and its caustics, the world, the renderer, the diver)
// shared by the live scene (koy_live.cpp) and the command-line tools (koy_app.cpp).
#pragma once
#include "core/config.h"
#include "core/image_io.h"
#include "scenes/koy/koy_kernels.h"
#include "scenes/koy/koy_optics.h"
#include "scenes/koy/koy_renderer.h"
#include "scenes/koy/koy_world.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

inline double know() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// The diver: a body of ~90 kg with its gear, quadratic drag, fin thrust along the view (W/S), sideways (A/D), up (Space),
// down (C); Shift a strong kick. Neutral buoyancy in K1 (the automatic jacket comes in K3). Collides with the floor and
// the boulder.
struct Diver {
    float3 pos{0, -4.5f, 6}, vel{0, 0, 0};
    float yaw = 3.14159265f, pitch = -0.12f;    // yaw 0 looks towards +z; pi towards -z (the cove)
    float3 forward() const {
        return make_float3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
    }
};

struct KoyQuality {   // the panel's knobs (Durak 1)
    int base = 4, edge = 16, filter = 1;
    float radius = 1.5f, contrast = 0.04f;
};

struct KoyApp {
    Config cfg;
    std::string root = PROJECT_ROOT;
    std::vector<std::string> overrides;
    unsigned int width = 2560, height = 1440;
    KoyRenderer renderer;
    KoyOptics optics;
    KoyOcean ocean;
    KoyWorld world;
    Diver diver;
    KoyQuality quality;
    float ev = 0.0f, evAdapt = 0.0f, adaptRef = 0.0f;
    bool caustics = true, grassMoves = true;
    float worldTime = 0.0f;
    std::vector<uchar4> pixels;

    void loadConfig();
    bool compile();
    void setup();                          // optics, ocean, world, bakes (after compile)
    void applyQuality();
    void setCamera();                      // the diver's eye, the mask, the field of view
    void stepDiver(float fwd, float right, float up, bool strong, float lookDX, float lookDY, float dt);
    void stepWorld(float t);               // waves, caustics, seagrass at world time t
    void frame();                          // stepWorld must have run; renders
    void adaptExposure(float dt, bool instant);
    void save(const std::string& prefix);
    void release();
};

int runKoyCli(int argc, char** argv);
