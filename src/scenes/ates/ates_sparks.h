// Scene 12 "Ateş": the sparks (PLAN §3.7). Each is a glowing fragment of char: thrown off by a crackle (steam and
// volatiles bursting a pocket under the char, more often where the wood pyrolyses faster), lifted off a glowing surface
// by the draught (more as it blows harder), or knocked out of the embers when wood lands on them. It is carried by
// the gas (its drag at the particle's Reynolds number, gravity), burns with the oxygen that reaches it (the char's
// kinetic rate and its film's diffusion in series, to CO or CO2 by Arthur's ratio) and is heated by that and cooled by
// convection and radiation; its light is its own temperature's. It dies burnt away, gone dark, or on the ground.
// Above the gas's grid the plume goes on as a buoyant plume does (its speed falling as z^-1/3, its width growing with
// z), from what leaves the grid's top. Drawn after DLSS: the path it took during the frame, its energy spread along it.
#pragma once
#include "scenes/ates/ates_gas.h"

#include <cuda_runtime.h>
#include <vector>

// a spark to be born (the wood's kernel writes them, the host adds bursts)
struct ASparkSeed {
    float3 p, v;
    float d, t;         // a flake's breadth and thickness (m)
    float T;            // K
};

// where the wood leaves its sparks (set by the run; seeds null: none)
struct ASparkSink {
    ASparkSeed* seeds = nullptr;
    unsigned int* count = nullptr;
    unsigned int cap = 0;
    float popPerKg = 0.0f;       // crackles per kg of volatiles
    float flakePerKg = 0.0f;     // flakes per kg of char burnt (at still air; a draught multiplies it)
    unsigned int frame = 0;
};

struct ASparkStats {
    unsigned int alive = 0, born = 0;
    float ms = 0.0f;             // GPU time of the step and the drawing
    unsigned int deaths[5] = {};  // in all: into a solid, burnt away, gone dark in the air, on the ground, far off
};

class AtesSparks {
public:
    unsigned int cap = 0;
    float rhoChar = 250.0f;      // kg/m^3, a char fragment
    float emissivity = 0.9f;
    float groundH = 400.0f;      // W/(m^2 K) into the ground from a spark lying on it (over its lower face)
    float kinA = 3.0e10f;        // the char's apparent rate k = A exp(-19000 / T) m/s (the logs': 3e8)
    ASparkStats stats;

    bool init(unsigned int cap, cudaStream_t st);
    // the wood's side: where its seeds go this frame
    ASparkSink sink(float popPerKg, float flakePerKg);
    // a burst from the host (wood landing on the embers): seeds added to this frame's
    void burst(const std::vector<ASparkSeed>& seeds);
    // the seeds born, then every spark moved, burnt and cooled over dt (real seconds)
    void step(float dt, const AtesGas& gas);
    // the sparks' paths of the last step added to the picture (linear, cd/m^2), behind what the depth hides
    void draw(float4* hdr, const float* depth, int width, int height, float3 eye, float3 U, float3 V, float3 W, float pixelAngle, const float3* grey,
              cudaStream_t st);
    // read the counts back (the tools)
    void readStats();
    // the live sparks' places and states (the tools): xyz and breadth, velocity and temperature
    void snapshot(std::vector<float4>& pos, std::vector<float4>& vel) const;
    void clear();
    void release();

private:
    cudaStream_t st_ = nullptr;
    float4 *pos_ = nullptr, *prev_ = nullptr;   // xyz, w: breadth (m); prev: where it was when the step began, w: its T then
    float4* vel_ = nullptr;                     // xyz, w: T (K)
    float* thick_ = nullptr;                    // the flake's thickness (m)
    ASparkSeed* seeds_ = nullptr;
    unsigned int* counters_ = nullptr;          // [0] seeds this frame, [1] the ring's head, [2] alive (drawn), [3] born in all
    float* plume_ = nullptr;                    // what leaves the grid's top: [0] mass flux weight, [1..2] centre x z, [3] r^2, [4] v, [5] T excess
    unsigned int frame_ = 0;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr, e2_ = nullptr, e3_ = nullptr;
    bool timed_ = false;
};
