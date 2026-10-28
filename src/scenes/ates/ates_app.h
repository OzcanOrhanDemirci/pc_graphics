// Scene 12 "Ateş": one run's state, shared by the live scene and the command-line tools (--scene ates ...): the world
// (the stones' scans, the ground), the gas, the wood, the eye, and the hand (blowing, a match, the logs added).
#pragma once
#include "core/config.h"
#include "scenes/ates/ates_gas.h"
#include "scenes/ates/ates_renderer.h"
#include "scenes/ates/ates_wood.h"
#include "scenes/orman/orman_assets.h"

#include <string>
#include <vector>

struct AtesCam {
    float3 feet{0, 0, 0};
    float yaw = 0.0f;         // 0 looks along -z
    float pitch = 0.0f;
    float fovY = 55.0f;
    float eyeHeight = 1.65f;
};

class AtesApp {
public:
    std::string root;
    std::vector<std::string> overrides;
    Config cfg;
    int width = 2560, height = 1440;
    AtesCam cam;
    OrmanAssets assets;
    AtesRenderer R;
    AtesGas gas;
    AtesWood wood;
    float exposureEv = 0.0f;
    float autoExposure = 1.0f;
    bool autoOn = true;
    float timeScale = 1.0f;          // the wood's time against the real (the flame always runs real time)
    double simTime = 0.0;            // seconds of the gas
    double woodTime = 0.0;           // seconds of the wood (scaled)
    bool blowing = false;            // the person blows along the look (held)
    float matchLeft = 0.0f;          // seconds the match still burns
    float3 matchAt{0, 0, 0};
    std::string layout;              // the fire laid at the start (ates.cfg: layout)
    double loadMs = 0.0;

    void loadConfig();
    void loadAssets();
    void buildWorld();
    bool initRenderer(std::string& err);
    void initSim();
    void lay(const std::string& name);   // the fire's wood: "ates" burning, "soguk" laid and unlit, "kor" embers only
    // a piece of wood thrown on where the eye looks (kind 0 twigs, 1 a log)
    void addFuel(int kind);
    void strikeMatch();
    void update(float dt);           // the gas (fixed steps) and the wood
    void render(float dt);
    bool setDlss(bool on);
    bool save(const std::string& pathNoExt);
    float3 eye() const;
    float3 forward() const;
    static AtesCam view(int i);
    static int viewCount();
    std::string summary() const;
    void release();

private:
    int groundMat_ = -1, groundMat2_ = -1, barkMat_ = -1, endMat_ = -1;
    float gasAcc_ = 0.0f;
    float starterLeft_ = 0.0f;   // the kindling's last seconds (a burner under the logs at the start)
    float meshAge_ = 0.0f;
    float adaptLum_ = -1.0f;
    unsigned int frame_ = 0;
    bool havePrev_ = false;
    float3 prevEye_{0, 0, 0}, prevU_{0, 0, 0}, prevV_{0, 0, 0}, prevW_{0, 0, 1};
    std::vector<float> patchRad_;
    void syncLogs(bool rebuild);
    void placeLog(ALog& L, float3 at, float3 axis);
};

int runAtesCli(int argc, char** argv);
