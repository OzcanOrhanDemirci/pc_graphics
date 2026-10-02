#include "scenes/tanecik/balance.h"

#include <algorithm>
#include <cmath>

namespace {

float4 quatAxisAngle(float3 axis, float angle) {
    const float l = std::sqrt(dot(axis, axis));
    if (l < 1e-9f || std::fabs(angle) < 1e-9f) return make_float4(0, 0, 0, 1);
    const float3 k = axis / l;
    const float s = std::sin(0.5f * angle);
    return make_float4(k.x * s, k.y * s, k.z * s, std::cos(0.5f * angle));
}
float3 rotateQ(float4 q, float3 v) {
    const float3 u = make_float3(q.x, q.y, q.z);
    const float3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

// a plain adult's muscles at each joint (by the child bone): stiffness (N m per rad) and strength (N m); rough
// figures of the order the literature gives for peak joint torques (ankle plantar flexion ~150, knee and hip ~200)
struct Tone {
    float stiffness, strength;
};
Tone toneOf(int child) {
    switch (child) {
    case B_HEAD: return {100, 40};
    case B_NECK: return {150, 60};
    case B_CHEST: return {800, 200};
    case B_ABDOMEN: return {800, 200};
    case B_UARM_L: case B_UARM_R: return {120, 60};
    case B_FARM_L: case B_FARM_R: return {60, 40};
    case B_HAND_L: case B_HAND_R: return {15, 10};
    case B_THIGH_L: case B_THIGH_R: return {800, 200};
    case B_SHIN_L: case B_SHIN_R: return {800, 200};
    case B_FOOT_L: case B_FOOT_R: return {600, 120};
    default: return {100, 50};
    }
}

}   // namespace

const char* BalanceController::phaseName() const { return phase_ == Phase::Stand ? "ayakta" : "dusuyor"; }

void BalanceController::init(const SkeletonSim& sim) {
    const std::vector<SkelJoint>& J = sim.joints();
    jointOfChild_.assign(B_COUNT, -1);
    for (int k = 0; k < int(J.size()); ++k) jointOfChild_[J[k].b] = k;
    mass_.resize(B_COUNT);
    totalMass_ = 0.0f;
    for (int b = 0; b < B_COUNT; ++b) { mass_[b] = sim.mass(b); totalMass_ += mass_[b]; }
    const int nj = int(J.size());
    target_.assign(nj, make_float4(0, 0, 0, 1));
    compliance_.assign(nj, 0.0f);
    strength_.assign(nj, 0.0f);
    baseStiff_.assign(nj, 0.0f);
    baseStrength_.assign(nj, 0.0f);
    for (int k = 0; k < nj; ++k) {
        const Tone t = toneOf(J[k].b);
        baseStiff_[k] = t.stiffness;
        baseStrength_[k] = t.strength;
    }
    phase_ = Phase::Stand;
    tone = 1.0f;
    fallTime_ = 0.0f;
    haveCom_ = false;
    // standing at rest is the balance to keep: the centre of mass where it is then, relative to the feet's middle (it is
    // not over the feet's own centre of mass: the feet reach forward of the ankles, the body's weight is near the heels)
    const std::vector<SkelBody>& r = sim.restBodies();
    float3 c = make_float3(0, 0, 0);
    for (int b = 0; b < B_COUNT; ++b) c = c + r[b].x0 * mass_[b];
    c = c / totalMass_;
    const float3 s = (r[B_FOOT_L].x0 + r[B_FOOT_R].x0) * 0.5f;
    restOffset_ = make_float3(c.x - s.x, 0.0f, c.z - s.z);
}

void BalanceController::update(const std::vector<SkelBody>& b, float dt) {
    // where the body is: the centre of mass, its speed, the trunk's tilt, the middle of the feet
    float3 c = make_float3(0, 0, 0), v = make_float3(0, 0, 0);
    for (int i = 0; i < B_COUNT; ++i) { c = c + b[i].x * mass_[i]; v = v + b[i].v * mass_[i]; }
    c = c / totalMass_;
    v = v / totalMass_;
    com = c;
    comVel = v;
    support = (b[B_FOOT_L].x + b[B_FOOT_R].x) * 0.5f;
    support.y = 0.0f;
    const float3 up = rotateQ(b[B_CHEST].q, make_float3(0, 1, 0));
    trunkTilt = std::acos(std::fmax(-1.0f, std::fmin(1.0f, up.y)));
    // the capture point: where he must stand to come to rest (linear inverted pendulum, omega = sqrt(g / height))
    const float omega = std::sqrt(9.81f / std::fmax(0.3f, c.y));
    capture = make_float3(c.x + v.x / omega, 0.0f, c.z + v.z / omega);

    if (phase_ == Phase::Stand && trunkTilt > fallTilt) {
        phase_ = Phase::Fall;
        fallTime_ = 0.0f;
        fallDir_ = make_float3(up.x, 0.0f, up.z);
        const float l = std::sqrt(dot(fallDir_, fallDir_));
        fallDir_ = l > 1e-6f ? fallDir_ / l : make_float3(0, 0, 1);
    }
    if (phase_ == Phase::Fall) {
        fallTime_ += dt;
        tone = std::fmax(0.12f, 1.0f - fallTime_ / fadeTime);
    }

    const int nj = int(target_.size());
    for (int k = 0; k < nj; ++k) {
        target_[k] = make_float4(0, 0, 0, 1);
        compliance_[k] = 1.0f / (baseStiff_[k] * tone);
        strength_[k] = baseStrength_[k] * tone;
    }
    if (phase_ == Phase::Stand && ankleStrategy) {
        // lean the body back over the feet: the feet turn relative to the shins (toes down when he leans forward)
        float3 e = make_float3(c.x - support.x - restOffset_.x, 0.0f, c.z - support.z - restOffset_.z);
        float3 a = e * ankleKp + make_float3(v.x, 0.0f, v.z) * ankleKd;
        float mag = std::sqrt(dot(a, a));
        if (mag > 1e-6f) {
            const float ang = std::fmin(mag, ankleMax);
            const float3 axis = cross(make_float3(0, 1, 0), a / mag);
            for (int foot : {B_FOOT_L, B_FOOT_R}) {
                const int k = jointOfChild_[foot];
                const int shin = foot == B_FOOT_L ? B_SHIN_L : B_SHIN_R;
                // the axis in the shin's frame (the target is relative to the parent)
                const float4 qs = b[shin].q;
                const float3 al = rotateQ(make_float4(-qs.x, -qs.y, -qs.z, qs.w), axis);
                target_[k] = quatAxisAngle(al, ang);
            }
        }
    }
    if (phase_ == Phase::Fall) {
        // the arms reach towards the fall (forward and down: a hand to catch the ground)
        for (int arm : {B_UARM_L, B_UARM_R}) {
            const int k = jointOfChild_[arm];
            const float3 axis = cross(make_float3(0, -1, 0), fallDir_);
            const float4 qc = b[B_CHEST].q;
            const float3 al = rotateQ(make_float4(-qc.x, -qc.y, -qc.z, qc.w), axis);
            target_[k] = quatAxisAngle(al, 1.1f);
            strength_[k] = baseStrength_[k];   // the reflex is strong while the rest goes limp
            compliance_[k] = 1.0f / baseStiff_[k];
        }
    }
    haveCom_ = true;
}
