// Scene 3 "Yikim": the free camera of the live window (fly with the keys, look with the mouse).
// The renderer builds the camera as right = fwd x up, which is the screen's right; moving and turning are taken
// from that same basis, so D goes to the screen's right and the mouse dragged right turns the view right.
// (They were mirrored: the keys used (cos yaw, 0, -sin yaw), the screen's left, and yaw grew to the left.)
#pragma once
#include "shared/vec.h"
#include "shared/yikim_params.h"

#include <algorithm>
#include <cmath>

struct FlyCamera {
    float3 pos = make_float3(0, 0, 0);
    float yaw = 0.0f, pitch = 0.0f;   // yaw 0 looks along +z
    float fovDeg = 50.0f;

    float3 forward() const { return make_float3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)); }
    // the screen's right, level
    float3 right() const { return normalize(cross(make_float3(std::sin(yaw), 0.0f, std::cos(yaw)), make_float3(0, 1, 0))); }

    void lookAt(float3 from, float3 to) {
        pos = from;
        const float3 f = normalize(to - from);
        yaw = std::atan2(f.x, f.z);
        pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
    }
    // mouse moved by (dx, dy) pixels: right turns right, down looks down
    void look(float dx, float dy, float sensitivity) {
        yaw -= dx * sensitivity;
        pitch = std::max(-1.5f, std::min(1.5f, pitch - dy * sensitivity));
    }
    // inputs in -1..1: forward (W/S), right (D/A), up (E/Q); dist in metres
    void move(float inForward, float inRight, float inUp, float dist) {
        const float3 f = make_float3(std::sin(yaw), 0.0f, std::cos(yaw));
        float3 mv = f * inForward + right() * inRight + make_float3(0.0f, inUp, 0.0f);
        if (dot(mv, mv) <= 0.0f) return;
        pos = pos + normalize(mv) * dist;
        pos.y = std::max(pos.y, 0.3f);
    }
    void apply(YCamera& cam, unsigned int width, unsigned int height) const {
        cam.pos = pos;
        cam.fwd = normalize(forward());
        cam.right = normalize(cross(cam.fwd, make_float3(0, 1, 0)));
        cam.up = cross(cam.right, cam.fwd);
        cam.tanHalfY = std::tan(0.5f * fovDeg * 3.14159265f / 180.0f);
        cam.tanHalfX = cam.tanHalfY * float(width) / float(height);
    }
};

// where a world point lands on the screen: x in -1 (left) .. 1 (right), as the renderer's rays are made
inline float screenX(const YCamera& c, float3 p) {
    const float3 v = p - c.pos;
    return dot(v, c.right) / (dot(v, c.fwd) * c.tanHalfX);
}
