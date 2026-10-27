// Scene 11 "Orman": one run's state, shared by the live scene and the command-line tools (--scene orman --headless).
#pragma once
#include "core/config.h"
#include "scenes/orman/orman_assets.h"
#include "scenes/orman/orman_renderer.h"
#include "scenes/orman/orman_world.h"

#include <string>
#include <vector>

struct OrmanCam {
    float3 feet{0, 0, 0};     // on the ground; the eye is eyeHeight above
    float yaw = 0.0f;         // 0 looks along -z, grows to the left (radians)
    float pitch = 0.0f;       // up positive
    float fovY = 60.0f;       // degrees
    float eyeHeight = 1.7f;
};

class OrmanApp {
public:
    std::string root;
    std::vector<std::string> overrides;
    Config cfg;
    int width = 2560, height = 1440;
    OrmanCam cam;
    OrmanAssets assets;
    OrmanWorld world;
    OrmanRenderer R;
    OBakeStats bake;
    std::vector<std::string> loaded;   // asset ids read
    double assetsMs = 0.0, worldMs = 0.0, uploadMs = 0.0;
    float exposureEv = 0.0f;           // the user's correction (stops)
    float autoExposure = 1.0f;         // the eye's adaptation (follows the scene's brightness)
    bool autoOn = true;

    void loadConfig();
    void loadAssets();                 // every model under assets/orman/cc0 and the ground's material
    void buildWorld();
    bool initRenderer(std::string& err);
    void bakeLight();
    void applyLight();                 // the sun, the sky and the display settings from cfg to the renderer
    void render(float dt = 0.0f);
    // DLSS Ray Reconstruction on or off (false: off, R.dlssError says why when it was asked for)
    bool setDlss(bool on);
    bool save(const std::string& pathNoExt);
    float3 eye() const;
    float3 forward() const;
    static OrmanCam view(int i);
    static int viewCount();
    std::string summary() const;
    void release();

private:
    int groundMaterial_ = -1, waterMaterial_ = -1;
    double clock_ = 0.0;
    float adaptLum_ = -1.0f;
    unsigned int frame_ = 0;            // DLSS: frames since it was turned on (the sub-pixel sequence)
    bool havePrev_ = false;
    float3 prevEye_{0, 0, 0}, prevU_{0, 0, 0}, prevV_{0, 0, 0}, prevW_{0, 0, 1};
};

int runOrmanCli(int argc, char** argv);
