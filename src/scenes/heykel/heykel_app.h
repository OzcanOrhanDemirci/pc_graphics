// Scene 6 "Heykel": the state of a run (settings, the candidates, the world, the camera and the light, the GPU
// buffers) shared by the live scene (heykel_live.cpp) and the command-line tools (heykel_app.cpp).
#pragma once
#include "core/config.h"
#include "scenes/heykel/heykel_kernels.h"

#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

// one candidate of the study (G0): a fractal, its knobs, how it stands and how it is first seen
struct HeykelCandidate {
    std::string tr, en;             // its name on the panel
    std::string id;                 // in file names (ASCII: Windows' narrow file names are not UTF-8)
    hk::Shape shape;                // in fractal space; the fit places it (scale, centre, bound)
    float3 euler{0, 0, 0};          // degrees: the fractal turned in the world (x, then y, then z)
    float sizeM = 0.42f;            // the sculpture's height, metres
    float cutDepth = 0.04f;         // how much of its height is cut flat at the bottom (it stands on that)
    float homeYaw = 35.0f, homePitch = -14.0f;   // degrees, the first view
    float frame = 1.0f;             // more room around it in the first view (a wide one)
    float2 dive{0.5f, 0.45f};       // where the tools zoom in, as a fraction of the screen
    float turn = 0.0f, turn2 = 0.0f;   // radians: the folds' turns (crystal, hybrid, tetra), about these axes
    float3 turnAxis{1, 0, 0}, turnAxis2{0, 1, 0};
};

struct HeykelApp {
    Config cfg;
    std::string root = PROJECT_ROOT;
    std::vector<std::string> overrides;
    int width = 2560, height = 1440;
    float fovDeg = 30.0f;
    std::vector<HeykelCandidate> cands;
    int cand = 0;
    hk::World world;
    float fitR = 1.0f, fitMinY = -1.0f;   // the fit: the outer radius and the lowest point, fractal units (turned)

    // camera: a position, yaw (0: looking towards -z) and pitch; the focus is what the zoom and the turn go around
    float3 camPos{0, 0, 1};
    float yaw = 0.0f, pitch = 0.0f;
    float3 focus{0, 0, 0};
    float level = 1.0f;              // how far zoomed in (1: the first view)
    float3 homePos{0, 0, 1}, homeFocus{0, 0, 0};
    float homeYaw = 0.0f, homePitch = 0.0f;
    float maxLevel = 1000.0f;

    // the light: pointed at a target from a direction (world), at a distance that follows the view
    float3 lightTarget{0, 0, 0}, lightDir{-0.6f, 0.6f, 0.5f};
    float lightDist = 1.5f, lightRadius = 0.06f, litRadius = 0.35f;
    float ev = 0.0f;

    // GPU
    float4* preview = nullptr;
    float4* accum = nullptr;
    uchar4* output = nullptr;
    int samples = 0;                 // accumulated a pixel
    float previewMs = 0.0f, pathMs = 0.0f;   // the last preview; a whole sample a pixel of the path tracer

    void loadConfig();
    void buildCandidates();
    void applyOverrides(int i);      // aday<i>.<knob> from the settings
    void init();                     // GPU buffers
    void release();
    void setCandidate(int i);        // fits it onto the plinth, home view, the light at its centre
    void home();
    void cameraBasis(float3& right, float3& up, float3& fwd) const;
    HeykelView view() const;

    bool pick(float px, float py, float3& hit, float3* normal = nullptr) const;   // render pixels
    void zoomAbout(float3 q, float logAmount);   // > 0 in; the point q stays where it is on the screen
    bool orbit(float dyaw, float dpitch);        // around the focus; false: it would go into the stone
    void defaultLight();                         // from the upper left, a little towards the viewer (a raking key)
    void aimLight(float3 target, const float3* normal = nullptr);   // onto a point, its size from the view there;
                                                                    // a normal keeps it on the lit side
    void turnLight(float dAzimuth, float dElevation);

    void renderPreview();            // and resets the accumulation
    void accumulate(int spp);
    void pathPass(int spp, int stripe, int stripes);   // one part of a sample (rows y = stripe mod stripes); pathMs a whole one
    void resolve();
    void save(const std::string& prefix);
};

int runHeykelCli(int argc, char** argv);
