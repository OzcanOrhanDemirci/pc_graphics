// Scene 8 "Denizaltı": what the live scene and the command-line tools share (the world, the camera, the settings).
#pragma once
#include "core/config.h"
#include "scenes/denizalti/dz_atlas.h"
#include "scenes/denizalti/dz_bake.h"
#include "scenes/denizalti/dz_decal.h"
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
    Decals decals;
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
    // the camera's collision: every 5 cm cell a surface passes through, grown by the camera's radius
    std::vector<uint8_t> solid;
    int solidDim[3] = {0, 0, 0};
    float solidLo[3] = {-2.5f, -0.05f, -0.05f}, solidCell = 0.05f;
    void buildCollision(float radius);
    bool blocked(float3 p) const;
    // moves the camera by d, sliding along what it meets (axis by axis)
    void moveCamera(float3 d);
    // the walker (the live view): a person, feet on the deck, gravity; Space jumps, C crouches a little while held.
    // The body is three points (knee, waist, eye) against the solid cells, so a table stops it as a wall does
    struct Walker {
        float3 feet{0, 0, 0};
        float vy = 0.0f, crouch = 0.0f;   // crouch 0 standing .. 1 down
        bool grounded = true, placed = false;
    } walker;
    float eyeStand() const { return start.pos.y; }
    float eyeHeight() const;
    bool bodyBlocked(float3 feet, float eye) const;
    void walk(float fwd, float right, bool slow, bool jump, bool crouch, float dt);
};

int runCli(int argc, char** argv);

}   // namespace dz
