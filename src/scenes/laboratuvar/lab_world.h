// Scene 4 "Laboratuvar": the experiments' physics. One world of particles (particles.h) set up for the chosen
// experiment, at a spacing given by its settings: coarse for the live window, fine for a recording's careful pass
// (the same setup, smaller particles). Glass ware held still or moved by a hand is KINEMATIC particles: one layer
// over the glass's surface, a liquid's density counts them (Akinci's psi), so a thin wall holds water.
//   Su      a beaker of water tilts over a tumbler (its height is set): the water pours, splashes, overflows
//   Bal     a dipper drips honey (very viscous, sticking to glass) on a glass dish; the dipper follows the mouse
//   Kum     a funnel pours sand (grains with friction) on the table and into a beaker; a steel ball can be dropped
//   Balon   a water balloon (a stretched rubber membrane round water) hangs; a needle pricks it where clicked
//   Karpuz  a small watermelon (a brittle rind, a soft flesh that turns to juice) on a stand; a rifle shot
#pragma once
#include "core/config.h"
#include "scenes/laboratuvar/lab_geom.h"
#include "scenes/laboratuvar/particles.h"

#include <cuda_runtime.h>

#include <string>
#include <vector>

enum class Deney : int { Su = 0, Bal = 1, Kum = 2, Balon = 3, Karpuz = 4 };
constexpr int kDeneyCount = 5;
const char* deneyKey(Deney d);            // the settings' prefix: "su", "bal", "kum", "balon", "karpuz"
inline bool deneyRecorded(Deney d) { return d == Deney::Su || d == Deney::Balon || d == Deney::Karpuz; }

// what a particle is drawn as (the renderer: a surface each; DC_NONE not drawn: glass walls, a stand)
enum DrawCode : uint8_t { DC_NONE = 0, DC_WATER = 1, DC_HONEY = 2, DC_SAND = 3, DC_RUBBER = 4, DC_RIND = 5, DC_FLESH = 6, DC_JUICE = 7, DC_STEEL = 8 };
constexpr int kDrawCodes = 9;

// what the panel sets: the inputs a recording keeps with its trigger (0..1 each, read per experiment)
struct LabInputs {
    float height = 0.3f;    // Su: the beaker's lip over the tumbler; Bal: the dipper's height; Kum: the funnel's
    float flow = 0.5f;      // Bal, Kum: the flow
    float power = 0.5f;     // Karpuz: the load (weapons.h gunLoad rifle, u)
    float3 nozzle = make_float3(0, 0, 0);   // Bal, Kum: where over the table (x, z) the stream falls
    bool flowing = true;    // Bal, Kum: the stream runs
};

// a trigger (what a recording replays at its time zero)
struct LabEvent {
    enum Kind : int { None = 0, Pour = 1, Prick = 2, Shot = 3, Ball = 4 } kind = None;
    float3 a = make_float3(0, 0, 0);   // Prick: the point; Shot: the muzzle; Ball: where it is let go
    float3 b = make_float3(0, 0, 0);   // Shot: the aim point
    float power = 0.5f;
};

// what one held object (a prop: the beaker, the dipper, the funnel) is: its mesh's object frame placed by xf
struct PropPose {
    float xf[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};

// the props' meshes (the same profiles the walls' particles come from), in their own frames; empty: not used
HostMesh labPropMesh(Deney d, int propId);
// which material a prop is drawn in: 0 glass, 1 wood, 2 steel
int labPropMaterial(Deney d, int propId);

class LabWorld {
public:
    Deney deney = Deney::Su;
    bool fine = false;
    float s = 0.0025f;            // the spacing (m)
    ParticleWorld pw;
    LabInputs in;
    Config cfg;                   // the experiment's settings (its own keys over the shared ones)
    std::vector<uint8_t> code;    // per particle: DrawCode at build (appended particles too)
    // the props (fixed and moving glass ware, the dipper...): their poses now; the renderer has a mesh for each
    enum PropId { P_TUMBLER = 0, P_BEAKER = 1, P_DIPPER = 2, P_FUNNEL = 3, P_DISH = 4, P_STAND = 5, P_HOOK = 6, P_COUNT = 7 };
    PropPose prop[P_COUNT];
    bool propUsed[P_COUNT] = {};
    // building
    bool build(Deney d, const Config& sceneCfg, bool fine, const LabInputs& in);
    void settle(double seconds, double dt = 1.0 / 60.0);  // rest under gravity (ramped in), before anything is triggered
    // running
    void step(float dt);
    void trigger(const LabEvent& e);
    void setInputs(const LabInputs& i);   // live changes (the dipper moved, the flow set)
    double time() const { return pw.time() - t0_; }   // since the world settled
    double sinceTrigger() const { return triggered_ ? time() - tTrigger_ : -1.0; }
    bool triggered() const { return triggered_; }
    int count() const { return pw.count(); }
    // the drawing: every particle's position and draw code, on the device (the caller's arrays, count() long)
    void exportDraw(float3* dPos, uint8_t* dCode, cudaStream_t st);
    // the balloon's membrane: its triangles (indices relative to its first particle) and range
    int membraneFirst = -1, membraneCount = 0;
    std::vector<uint3> membraneTris;
    // the steel ball (Kum): its particles' range, its radius
    int ballFirst = -1, ballCount = 0, ballGroup = 0;
    float ballRadius = 0.0f;
    // how far the recording's box must reach (the particles that matter stay inside)
    float3 boxLo = make_float3(-0.6f, -0.02f, -0.45f), boxHi = make_float3(0.6f, 0.9f, 0.45f);
    std::string summary() const;   // a line for the console: counts
    void destroy();
    ~LabWorld() { destroy(); }

private:
    double t0_ = 0.0, tTrigger_ = 0.0;
    float restScale_ = 1.0f, restTarget_ = 1.0f;   // the balloon's membrane: its rest length now and when settled
    bool triggered_ = false;
    LabEvent ev_;
    // kinematic glass (one range per held prop): rest positions in its frame, the device copies
    struct Held {
        int first = -1, count = 0, propId = -1;
        float3* dRest = nullptr;
        float3* dTarget = nullptr;
        uint32_t* dIdx = nullptr;
    };
    std::vector<Held> held_;
    int addHeld(const std::vector<float3>& restLocal, int propId, int group);   // before commit
    void driveHeld(float dt);    // every held prop to its pose at the end of the step
    // emitters (Bal, Kum)
    double emitCarry_ = 0.0;
    unsigned emitSeed_ = 1;
    int sandGroup_ = 0;
    void emit(float dt);
    // per experiment
    void buildSu();
    void buildBal();
    void buildKum();
    void buildBalon();
    void buildKarpuz();
    float3 suPivot_ = make_float3(0, 0, 0), suBeakerBase_ = make_float3(0, 0, 0);
    float suAngle(double t) const;   // the beaker's tilt (rad, clockwise negative) t seconds after the pour began
    void placeProps();
    uint8_t* dBase_ = nullptr;   // code on the device
    float* dFluid_ = nullptr;
    int dCap_ = 0, baseUploaded_ = 0;
};
