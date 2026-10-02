// Scene 4 "Tanecik": the man's muscles and his balance (T3). Every frame it reads the bones and sets each joint's
// muscle: a target (the child's rotation relative to the parent), a stiffness and a strength. Standing, every joint
// holds the rest pose; the ankles lean the body back over the feet (the ankle strategy: the centre of mass is pulled
// over the middle of the support by its offset and its speed). Fallen (the trunk past a tilt, or the capture point out
// of reach), the tone fades and the arms reach towards the fall.
#pragma once
#include "scenes/tanecik/skeleton_sim.h"

#include <string>
#include <vector>

class BalanceController {
public:
    enum class Phase { Stand, Fall };
    void init(const SkeletonSim& sim);
    void update(const std::vector<SkelBody>& bodies, float dt);
    void apply(SkeletonSim& sim) const { sim.setMuscles(target_, compliance_, strength_); }
    Phase phase() const { return phase_; }
    const char* phaseName() const;
    // knobs (measured, see docs/tanecik/DURUM.md)
    bool ankleStrategy = true;
    float ankleKp = 3.0f;      // rad of ankle lean per metre of the centre of mass off the middle of the feet
    float ankleKd = 1.2f;      // and per m/s of its speed
    float ankleMax = 0.35f;    // rad
    float fallTilt = 0.6f;     // rad: the trunk this far off upright: he is falling
    float fadeTime = 0.6f;     // s: the tone fades over this once falling
    float tone = 1.0f;         // the muscles' share of their strength now
    // what it saw last (telemetry)
    float3 com{}, comVel{}, capture{}, support{};
    float trunkTilt = 0.0f;

private:
    std::vector<int> jointOfChild_;            // per bone: the joint where it hangs from its parent (-1 the pelvis)
    std::vector<float> mass_;
    float totalMass_ = 0.0f;
    std::vector<float4> target_;
    std::vector<float> compliance_, strength_, baseStiff_, baseStrength_;
    Phase phase_ = Phase::Stand;
    float fallTime_ = 0.0f;
    float3 fallDir_{};
    bool haveCom_ = false;
    float3 restOffset_{};                      // the centre of mass relative to the feet's middle at rest (he stands so)
};
