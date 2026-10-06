// Scene 9 "Ayna": one run's state, shared by the live scene and the command-line tools.
#pragma once
#include "core/config.h"
#include "scenes/ayna/ayna_beams.h"
#include "scenes/ayna/ayna_kernels.h"
#include "scenes/ayna/ayna_maze.h"

#include <string>
#include <vector>

struct AynaCam {
    float3 pos{0.0f, 1.3f, 0.0f};   // our sphere's centre (the eye)
    float yaw = 0.0f, pitch = 0.0f; // yaw 0 looks along -z, grows to the left; pitch up positive (radians)
    float fovY = 62.0f;             // degrees
};

// a lamp left behind: a small glass ball glowing in the colour and strength our light had
struct AynaLamp {
    float3 pos{0, 0, 0};
    float3 color{1, 1, 1};
    float level = 1.0f;
};

class AynaApp {
public:
    std::string root;
    std::vector<std::string> overrides;
    Config cfg;
    int width = 2560, height = 1440;
    AynaCam cam;
    ayna::Maze maze;
    ayna::Optics optics;
    // our light: its colour (the palette), its strength (steps on a log scale), on or off
    int colorIndex = 0;
    int levelIndex = 4;
    bool lightOn = true;
    std::vector<AynaLamp> lamps;
    float frameMs = 0.0f;
    unsigned int lastBad = 0;
    int debug = 0;
    float evScale = 1.0f;   // the exposure keys
    unsigned long long* statsDev = nullptr;   // the tools' counters (debug 3 / 4)
    ayna::BeamBuildStats beamStats;   // the last frame's (all lights)
    double beamMs = 0.0;
    uchar4* output = nullptr;   // device: the last picture

    static int paletteCount();
    static float3 paletteColor(int i);       // linear Rec.709, luminance 1
    static const char* paletteName(int i, bool turkish);
    static int levelCount();
    static float levelValue(int i);          // our light's peak intensity at that step (cd)

    static int viewCount();
    static AynaCam view(int i);              // the composed views (tools, autotest)
    static const char* viewName(int i, bool turkish);

    void loadConfig();
    void init();
    void release();
    void render();
    bool save(const std::string& pathNoExt);
    void probe(float x, float y);
    ayna::View makeView() const;
    void computeOptics();
    float3 sphereCentre() const { return cam.pos; }
    float3 forward() const;

private:
    float4* hdr_ = nullptr;
    static const int kLevels = 6;
    float4* levels_[kLevels] = {};
    float4* accum_[kLevels] = {};
    unsigned char* dEdges_ = nullptr;
    unsigned char* dColumns_ = nullptr;
    signed char* dCellObj_ = nullptr;
    std::vector<signed char> cellObj_;
    // the beams (rebuilt every frame: our light moves with us)
    std::vector<ayna::BeamNode> nodes_;
    std::vector<int> cellStart_, cellNodes_;
    ayna::BeamNode* dNodes_ = nullptr;
    int* dCellStart_ = nullptr;
    int* dCellNodes_ = nullptr;
    size_t capNodes_ = 0, capCellNodes_ = 0;
    void uploadBeams(ayna::View& v);
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr;
    unsigned int* bad_ = nullptr;
    void uploadObjects(const ayna::View& v);
};

int runAynaCli(int argc, char** argv);
