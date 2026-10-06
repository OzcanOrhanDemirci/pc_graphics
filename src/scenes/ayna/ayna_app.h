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
    std::vector<AynaLamp> lanterns;   // the brass lanterns, fixed (baked first)
    float frameMs = 0.0f;
    unsigned int lastBad = 0;
    int debug = 0;
    float evScale = 1.0f;   // the exposure keys
    unsigned long long* statsDev = nullptr;   // the tools' counters (debug 3 / 4)
    ayna::BeamBuildStats beamStats;   // the last frame's (all lights)
    double beamMs = 0.0;
    double bakeMs = 0.0;     // the last lamp's bake
    float dynMs = 0.0f;      // our light's bake this frame
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
    // the tools (Ö4)
    const ayna::LightMap& lightMap() const { return lm_; }
    void clearLightMap() { cudaMemset(dLm_, 0, lmTexels_ * sizeof(float4)); baked_ = 0; lampStraight_.clear(); }
    void bakeLampsPublic() { bakeLamps(); }
    bool bakeStraightOnly = false;
    int testMirror = 0;
    float subX = 0.0f, subY = 0.0f;
    float3 tmN{0, 0, 1};
    float tmD = 0.0f;
    // the tools: a reference picture, the mean of four renders shifted by a quarter pixel (16 samples each: 64)
    bool saveReference(const std::string& pathNoExt);
    std::vector<float4> hdrHost() const {
        std::vector<float4> h(size_t(width) * height);
        cudaMemcpy(h.data(), hdr_, h.size() * sizeof(float4), cudaMemcpyDeviceToHost);
        return h;
    }
    size_t lmTexelCount() const { return lmTexels_; }
    unsigned oobCount() const { unsigned h = 0; cudaMemcpy(&h, oob_, 4, cudaMemcpyDeviceToHost); return h; }
    ayna::View makeView() const;
    void computeOptics();
    float3 sphereCentre() const { return cam.pos; }
    float3 forward() const;

private:
    float4* hdr_ = nullptr;
    float4* haze_ = nullptr;
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
    std::vector<int> hazeStart_, hazeNodes_;
    int* dHazeStart_ = nullptr;
    int* dHazeNodes_ = nullptr;
    size_t capHaze_ = 0;
    std::vector<std::vector<ayna::BeamNode>> lampStraight_;   // each baked lamp's straight beams (the mist's glow)
    // the lamps' light map
    std::vector<ayna::LightMapBlock> lmBlocks_;
    std::vector<int> lmFace_, lmRev_, lmCol_;
    ayna::LightMapBlock* dLmBlocks_ = nullptr;
    int* dLmFace_ = nullptr;
    int* dLmRev_ = nullptr;
    int* dLmCol_ = nullptr;
    float4* dLm_ = nullptr;
    float4* dLm2_ = nullptr;   // our light's, every frame
    bool lm2Clear_ = false;
    size_t lmTexels_ = 0;
    int lmMaxW_ = 0, lmMaxH_ = 0;
    ayna::LightMap lm_;
    int baked_ = 0;          // lamps in the map (the first baked_ of `lamps`)
    bool errorShown_ = false;
    void buildLightMap();
    void bakeLamps();
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr;
    unsigned int* bad_ = nullptr;
    unsigned int* oob_ = nullptr;
    void uploadObjects(const ayna::View& v);
};

int runAynaCli(int argc, char** argv);
