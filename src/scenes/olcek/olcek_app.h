// Scene 7 "Ölçek": the state of a run (settings, the scale engine, the GPU buffers) shared by the live scene
// (olcek_live.cpp) and the command-line tools (olcek_app.cpp).
#pragma once
#include "core/config.h"
#include "scenes/olcek/olcek_hiz.h"
#include "scenes/olcek/olcek_hiz_goz.h"
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
    // the particles below the light: the list, its tiles (host, rebuilt every frame), and their GPU copies
    std::vector<int> tileStart, tileIdx;
    olcek::GParticle* dParts = nullptr;
    int* dTileStart = nullptr;
    int* dTileIdx = nullptr;
    int partCap = 0, tileIdxCap = 0;
    void binParticles();
    // the maps (Ö1; tools/olcek_veri.py): missing ones fall back to the placeholders
    olcek::Texture world, zonal, region, bay, moon, cloudWorld, cloudRegion;
    olcek::Field shore;
    olcek::Volume web;
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

    // ---- the second option "Hız" (docs/olcek/hiz/PLAN.md): the route, the rocket, the colour of light
    olcek::hiz::Route route;
    olcek::hiz::Ship ship;
    olcek::hiz::Blackbody bb;
    int proximaStar = -1;            // the catalogue's Proxima, moved to its Gaia DR3 place (the route's)
    std::string hizLog;
    void prepareHiz();               // after loadMaps: the route, Proxima's place, the blackbody table
    // the rocket's eye: the stars where and how it sees them, on black (H0); sum: the sky's light
    void renderEye(const olcek::hiz::Eye& e, olcek::hiz::EyeSum& sum);
    // The designed exposures (PLAN §3.1: a known function of where and how fast, never read from the picture). The eye
    // adapts to the visible light in its field, as if the whole sky were like it (visibleView / viewShare), against the
    // night sky's at the Sun (nightSky, the Sun left out), part of the way (hiz.uyum); the side view takes the first
    // option's curve near the Sun, deep space's beyond.
    double nightSky = 1e-12;         // W/m^2 (visible, the Sun's spectrum's watts): set by prepareHiz
    double eyeEv(double skyVisible) const;
    double sideEv(double s, olcek::V3 rocket) const;
    olcek::hiz::Eye eyeAt(const olcek::hiz::Ship& s, double yaw, double pitch, double tanHalfX) const;   // the camera

private:
    void draw(int samplesThis);      // the GPU half of a frame: view, splats, particles -> output
};

int runOlcekCli(int argc, char** argv);
// the second option's (olcek_hiz_test.cpp)
int runHizTests(OlcekApp& app);
int runHizFrames(OlcekApp& app, const std::vector<double>& phis, int leg, double frac, double fovDeg, double yaw, double pitch,
                 const std::vector<double>& sides, const std::string& out);
