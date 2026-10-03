// Scene 4 "Laboratuvar": the state of a run (settings, the renderer and what it draws, the camera). Shared by the live
// scene in the application (laboratuvar_live.cpp) and the command-line tools (laboratuvar_app.cpp).
#pragma once
#include "core/config.h"
#include "scenes/laboratuvar/lab_world.h"
#include "scenes/laboratuvar/laboratuvar_renderer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

// a free camera: the mouse turns it (right drag), WASD moves it where it looks, E/Q up and down
struct LabCam {
    float3 pos = make_float3(0.0f, 0.15f, 0.6f);
    float yaw = 3.14159265f, pitch = -0.1f;   // yaw 0 looks along +z
    float fovDeg = 30.0f;
    float aperture = 0.0f, focus = 0.5f;
    float3 forward() const { return make_float3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)); }
    float3 right() const { return normalize(cross(forward(), make_float3(0, 1, 0))); }
    void lookAt(float3 from, float3 to) {
        pos = from;
        const float3 f = normalize(to - from);
        yaw = std::atan2(f.x, f.z);
        pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
    }
    void look(float dx, float dy, float sensitivity) {
        yaw -= dx * sensitivity;
        pitch = std::max(-1.5f, std::min(1.5f, pitch - dy * sensitivity));
    }
    void move(float inF, float inR, float inU, float dist) {
        float3 mv = forward() * inF + right() * inR + make_float3(0.0f, inU, 0.0f);
        if (dot(mv, mv) <= 0.0f) return;
        pos = pos + normalize(mv) * dist;
        pos.y = std::max(pos.y, 0.01f);
    }
    void apply(LCamera& cam, unsigned int width, unsigned int height) const {
        cam.pos = pos;
        cam.fwd = normalize(forward());
        cam.right = normalize(cross(cam.fwd, make_float3(0, 1, 0)));
        cam.up = cross(cam.right, cam.fwd);
        cam.tanHalfY = std::tan(0.5f * fovDeg * 3.14159265f / 180.0f);
        cam.tanHalfX = cam.tanHalfY * float(width) / float(height);
        cam.aperture = aperture;
        cam.focus = focus;
    }
};

// what one picture is drawn from: particles (device), their draw codes, the props' poses, the membrane and the ball
struct DrawInput {
    const float3* pos = nullptr;
    const uint8_t* code = nullptr;
    int n = 0;
    float spacing = 0.0025f;
    const PropPose* poses = nullptr;
    int membraneFirst = -1, membraneCount = 0;
    int ballFirst = -1, ballCount = 0, ballN = 0;   // the balls: the first one's range, as many each after it, how many drawn
    float ballRadius = 0.0f;
};

struct LaboratuvarApp {
    Config cfg;
    std::string root = PROJECT_ROOT;
    std::vector<std::string> overrides;
    LabRenderer renderer;
    unsigned int width = 1920, height = 1080;
    LabCam cam;
    Deney deney = Deney::Su;
    // what the renderer holds for the experiment
    int matGlass = -1, matWood = -1, matSteel = -1;
    int objProp[LabWorld::P_COUNT];
    int surf[kDrawCodes];
    int objGrains = -1, objBall = -1, objMembrane = -1;
    int grainsPer = 0, grainsCap = 0;
    uint3* dMemTris = nullptr;
    int memTris = 0, memTrisCap = 0;
    const std::vector<uint3>* memSource = nullptr;   // whose triangles are uploaded
    int* dMemCount = nullptr;
    float* dWeight = nullptr;
    int weightCap = 0;

    void loadConfig();
    bool initRenderer(std::string& log);
    // the experiment's set, props, surfaces (maxParticles: the most particles a picture will have)
    void setupScene(Deney d, int maxParticles, const std::vector<uint3>* membraneTris = nullptr);
    void draw(const DrawInput& in);
    // the balloon's membrane triangles (the coarse world's, or a recording's fine ones)
    void setMembraneTris(const std::vector<uint3>& tris);
    // is this picked object the experiment's target (the balloon, the melon, the gel), not the table or a stand
    bool isTarget(int obj) const {
        for (int c : {int(DC_RUBBER), int(DC_WATER), int(DC_RIND), int(DC_PITH), int(DC_FLESH), int(DC_GEL)})
            if (renderer.isSurfaceObject(surf[c], obj)) return true;
        return obj >= 0 && obj == objMembrane;
    }
    void applyLook();
    void setCamera(Deney d);
    void release();
};

int runLaboratuvarCli(int argc, char** argv);
