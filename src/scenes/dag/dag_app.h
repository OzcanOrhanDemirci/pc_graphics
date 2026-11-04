// Scene 13 "Dağ": one run's state, shared by the live scene and the command-line tools (--scene dag --headless).
#pragma once
#include "core/config.h"
#include "scenes/dag/dag_atmos.h"
#include "scenes/dag/dag_renderer.h"
#include "scenes/dag/dag_world.h"

#include <functional>
#include <string>
#include <vector>

struct DagCam {
    float3 feet{0, 0, 0};      // on the ground; the eye is eyeHeight above
    float yaw = 0.0f;          // 0 looks along -z (north), grows to the left (radians)
    float pitch = 0.0f;        // up positive
    float fovY = 45.0f;        // degrees
    float eyeHeight = 1.7f;
};

class DagApp {
public:
    std::string root;
    std::vector<std::string> overrides;
    Config cfg;
    int width = 2560, height = 1440;
    DagCam cam;
    DagWorld world;
    DagRenderer R;
    DagAir air;
    DagTables tables;
    DagShadowMap shadow;
    float exposureEv = 0.0f;
    float eyeAlt = 0.0f;
    std::function<void(float, const std::string&)> progress;

    void loadConfig();
    bool prepareWorld(std::string& err);
    bool initRenderer(std::string& err);
    void buildAir();                     // the sun's shadow height map and the air's tables for the sun of now
    void applyDisplay();
    void render();
    // the farthest ground the picture really draws: a whole circle of frames, measured, not the bake's list (metres)
    float measureFarthest();
    float farthestDrawn = 0.0f;
    bool save(const std::string& pathNoExt);
    float3 eye() const;
    static DagCam view(int i);
    static int viewCount();
    std::string summary() const;
    void release();
    // what the middle of the picture looks at, in words; empty for the sky
    std::string lookingAt() const;
    float3 sunDir() const;
};

int runDagCli(int argc, char** argv);
