// Scene 6 "Heykel": the state of a run (settings, the candidates, the world, the camera and the light, the GPU
// buffers) shared by the live scene (heykel_live.cpp) and the command-line tools (heykel_app.cpp).
#pragma once
#include "core/config.h"
#include "scenes/heykel/heykel_kernels.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

// A world position in doubles (the host): the camera, the focus and the light are kept in it; the GPU sees offsets
// from the frame's anchor in floats (see hk::World).
struct D3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
inline D3 d3(double x, double y, double z) { return D3{x, y, z}; }
inline D3 d3(float3 v) { return D3{v.x, v.y, v.z}; }
inline float3 f3(D3 v) { return make_float3(float(v.x), float(v.y), float(v.z)); }
inline D3 operator+(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline D3 operator-(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline D3 operator*(D3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double length(D3 a) { return std::sqrt(dot(a, a)); }

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
    float3 alignUp{0, 0, 0};        // a fractal-space direction turned to world up first (0: none)
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

    // camera: a position, yaw (0: looking towards -z) and pitch; the focus is what the zoom and the turn go around
    D3 camPos{0, 0, 1};
    float yaw = 0.0f, pitch = 0.0f;
    D3 focus{0, 0, 0};               // also the frame's anchor (the GPU's offsets are from it)
    float level = 1.0f;              // how far zoomed in (1: the first view)
    D3 homePos{0, 0, 1}, homeFocus{0, 0, 0};
    float homeYaw = 0.0f, homePitch = 0.0f;
    float maxLevel = 1000.0f;

    // the light: pointed at a target from a direction (world), at a distance that follows the view
    D3 lightTarget{0, 0, 0};
    float3 lightDir{-0.6f, 0.6f, 0.5f};   // where it was put (the first light, a drag)
    float3 lampDir{-0.6f, 0.6f, 0.5f};    // where it is: turning towards lampGoal (turnLampTowardsGoal)
    float3 lampGoal{-0.6f, 0.6f, 0.5f};   // lightDir, or the nearest direction with a clear line
    float lightDist = 1.5f, lightRadius = 0.06f, litRadius = 0.35f;
    float ev = 0.0f;

    mutable hk::KStep hostSteps[hk::kMaxSteps];   // the anchor's orbit (setAnchor), and its copy on the GPU
    hk::KStep* devSteps = nullptr;
    mutable hk::MStep hostMSteps[hk::kMaxSteps];  // the Menger's
    hk::MStep* devMSteps = nullptr;

    // GPU
    float4* accum = nullptr;         // rgb weighted, w the weights (the fixed pattern's samples)
    uchar4* output = nullptr;
    int samples = 0;                 // the pattern's samples in accum
    bool dof = false;                // the macro lens's depth of field (off: Özcan wants it sharp)
    float sampleMs = 0.0f;           // one sample over the whole picture, measured
    float frameMs = 0.0f;            // the last whole frame (renderFrame), measured

    void loadConfig();
    void buildCandidates();
    void applyOverrides(int i);      // aday<i>.<knob> from the settings
    void init();                     // GPU buffers
    void release();
    void setCandidate(int i);        // fits it onto the plinth, home view, the light at its centre
    int sculpture() const;           // the scene's sculpture: the candidate named by "heykel" in the settings
    void home();
    void cameraBasis(float3& right, float3& up, float3& fwd) const;
    HeykelView view() const;
    void setAnchor(hk::World& w, D3 anchor) const;   // the offsets to the far things, the Kleinian's orbit (doubles,
                                                     // into hostSteps; w.st points there)
    // the perturbed ones, 10,000 times: the Kleinian and the Menger (the crystal without a turn)
    bool deep() const { return world.s.fractal == hk::kKleinian || (world.s.fractal == hk::kCrystal && cands[cand].turn == 0.0f); }
    float zoomCap() const { return deep() ? maxLevel : std::min(maxLevel, cfg.f("zoom.max.float", 1000.0f)); }
    float zoomCapAt(D3 q) const;     // and at a point (flat cut faces and the plinth: shallow)
    double deHost(D3 p) const;       // the scene's estimate at a world point, in doubles where it matters (picking)
    int itersNow() const;            // the anchor orbit's length at this zoom (enough for the nearest points)
    void setLod(hk::World& w) const; // the pixel rule for the iterations, point by point (hk::itersAt)

    bool pick(float px, float py, D3& hit, float3* normal = nullptr, bool* sculpture = nullptr) const;   // render pixels
    bool pickRay(D3 dir, D3& hit, float3* normal = nullptr, bool* sculpture = nullptr) const;         // from the camera
    bool zoomTarget(float px, float py, D3& q) const;   // the wheel's point: on the sculpture, never in the air
    bool zoomAbout(D3 q, float logAmount);       // > 0 in; the point q stays where it is on the screen; false: the
                                                 // camera bumped into the sculpture backing out (it stops there)
    double freePath(D3 a, D3 b, double clearance) const;   // how far from a towards b the camera can go
    double leashRadius() const;                  // the camera stays this near the first view's centre
    double leashFraction(D3 a, D3 b) const;      // how much of the way a to b is inside the leash
    bool homeStep(double& k, D3& q, double maxMove = 1e300);   // a share k of the way back to the first view
                                                 // (false: blocked)
    struct TrailPoint { D3 pos, focus; float yaw, pitch, level; };
    std::vector<TrailPoint> trail;               // the way the camera came from the first view, each leg clear
    void record();                               // the camera's pose onto the trail (after every move)
    bool retrace(float logAmount);               // back along the trail: a backward zoom of -logAmount (< 0)
    bool rewinding = false;
    bool atHome() const { return level == 1.0f && camPos.x == homePos.x && camPos.y == homePos.y && camPos.z == homePos.z; }                      // zooming out goes back along the trail (till a zoom in or a turn)
    bool orbit(float dyaw, float dpitch);        // around the focus; false: it would go into the stone
    void defaultLight();                         // from the upper left, a little towards the viewer (a raking key)
    void aimLight(D3 target, const float3* normal = nullptr);   // onto a point, its size from the view there;
                                                                    // a normal keeps it on the lit side
    void turnLight(float dAzimuth, float dElevation);
    bool lampClear(D3 target, float3 n, float3 dir, double dist) const;
    void clearLamp(const float3* normal);        // lampDir: lightDir if its line is clear, else the nearest clear one
    void settleLight();                          // the zoom stopped: the lamp onto the focus, a clear line
    bool turnLampTowardsGoal(float dt);          // a frame's step of the lamp's turn; false: it is there
    void followLight(float dt);                  // a zoom's frame: the target eases onto the focus, the lamp follows the view
    void keepLampClear();                        // while zooming: a ray a frame; blocked, the nearest clear direction

    void renderFrame();              // the window's frame: frame.base samples, frame.edge more on edges (heykelFrame)
    void resetSamples();             // something changed: the picture starts again from sample 0
    void addSamples(int count, int stripe = 0, int stripes = 1);   // the next samples (rows y = stripe mod stripes)
    void resolve();
    void save(const std::string& prefix);
};

int runHeykelCli(int argc, char** argv);
