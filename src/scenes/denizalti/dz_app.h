// Scene 8 "Denizaltı": what the live scene and the command-line tools share (the world, the camera, the settings).
#pragma once
#include "core/config.h"
#include "scenes/denizalti/dz_atlas.h"
#include "scenes/denizalti/dz_bake.h"
#include "scenes/denizalti/dz_geom.h"
#include "scenes/denizalti/dz_render.h"
#include "scenes/denizalti/dz_room.h"
#include "scenes/denizalti/dz_world.h"

#include <functional>
#include <string>
#include <vector>

namespace dz {

// the panel's choices as the names it shows
extern const char* const kResNames[5];
extern const int kResW[5], kResH[5];
extern const int kSsaa[5];

struct DzApp {
    std::string root;
    std::vector<std::string> overrides;
    Config cfg;
    Mesh mesh;
    std::vector<DzLight> lights;
    Atlas atlas;
    LightMap light;
    BakeOptions bakeOpt;
    Start start{};
    Camera cam;
    Settings set;
    int resIndex = 2, ssaaIndex = 2;
    Renderer renderer;
    bool glReady = false;

    void loadConfig();
    void build();                               // the world (CPU, any thread)
    std::string lightPath() const;
    // the light map from the cache, or baked now (and cached): any thread with CUDA
    bool ensureLight(std::string& err, const std::function<void(float, const std::string&)>& progress = {}, bool forceBake = false);
    bool initGL(std::string& err);              // the GL side (the thread with the context)
    void applyChoices();                        // resIndex/ssaaIndex -> settings
    void home();
};

int runCli(int argc, char** argv);

}   // namespace dz
