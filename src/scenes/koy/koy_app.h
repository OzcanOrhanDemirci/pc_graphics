// Scene 5 "Koy": the state of a run (settings, optics, the sea surface and its caustics, the world, the renderer, the diver)
// shared by the live scene (koy_live.cpp) and the command-line tools (koy_app.cpp).
#pragma once
#include "core/config.h"
#include "core/image_io.h"
#include "scenes/koy/koy_kernels.h"
#include "scenes/koy/koy_optics.h"
#include "scenes/koy/koy_renderer.h"
#include "scenes/koy/koy_world.h"

#include <algorithm>
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
    KoyOcean ripples;                      // the short waves the main surface leaves out, for the view alone (panel "Dalgacık")
    bool ripplesOn = false;
    float floorVarOff = 0.0f, floorVarOn = 0.0f;   // the unresolved slope variance without and with the ripples
    KoyWorld world;
    Diver diver;
    KoyQuality quality;
    float ev = 0.0f, evAdapt = 0.0f, adaptRef = 0.0f;
    bool caustics = true, grassMoves = true;
    bool torchOn = false;                  // the diver's torch (panel, F)
    float worldTime = 0.0f;
    std::vector<uchar4> pixels;
    void* bottomArr = nullptr;             // the floor's depth map (the light fields' choice), cudaArray_t
    unsigned long long bottomTex = 0;
    // Ada (docs/koy/PLAN_ADA.md §6): the pressure, the oxygen's partial pressure, the toxicity dose (0..1) it builds
    // above the limit; full: the convulsion, the blackout, the restart at the ladder
    float o2Dose = 0.0f, blackTimer = 0.0f;
    int restarts = 0;
    float pressureBar() const { return 1.013f + 0.1005f * std::max(0.0f, -diver.pos.y); }   // sea water: rho g = 1025 * 9.81
    float ppO2() const { return 0.2095f * pressureBar(); }
    bool blackedOut() const { return blackTimer > 0.0f; }

    void loadConfig();
    bool compile();
    void setup();                          // optics, ocean, world, bakes (after compile)
    void applyQuality();
    void setTextureMode(int mode);         // 0 standard, 1 high, 2 ultra (panel "Doku")
    void setRipples(bool on);              // stays off if there are none
    void setCamera();                      // the diver's eye, the mask, the field of view
    void stepDiver(float fwd, float right, float up, bool strong, float lookDX, float lookDY, float dt);
    void restartDiver();                   // at the ladder, as at the start
    void stepBody(float dt);               // pressure and oxygen: the dose, the tunnel, the blackout and the restart
    void stepWorld(float t);               // waves, caustics, seagrass at world time t
    void frame();                          // stepWorld must have run; renders
    void adaptExposure(float dt, bool instant);
    // the eye's reference adaptation: metered on open water (from the start, looking out to sea), whatever the start
    // view holds; then exposure set for the current view from there (display.adapt of the way)
    void calibrateEye();
    void save(const std::string& prefix);
    void release();
};

int runKoyCli(int argc, char** argv);
