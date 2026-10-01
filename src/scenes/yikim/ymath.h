// Scene 3 "Yikim": quaternion and double-vector helpers shared by the host and the CUDA physics kernels.
// Body positions are doubles: a substep moves a resting body by micrometres, a float at 20 m resolves 2 um.
#pragma once
#include "shared/vec.h"

HD double3 d3(double x, double y, double z) { return make_double3(x, y, z); }
HD double3 d3(float3 a) { return make_double3(a.x, a.y, a.z); }
HD float3 f3(double3 a) { return make_float3(float(a.x), float(a.y), float(a.z)); }
HD double3 operator+(double3 a, double3 b) { return make_double3(a.x + b.x, a.y + b.y, a.z + b.z); }
HD double3 operator-(double3 a, double3 b) { return make_double3(a.x - b.x, a.y - b.y, a.z - b.z); }
HD double3 operator+(double3 a, float3 b) { return make_double3(a.x + b.x, a.y + b.y, a.z + b.z); }
HD double3 operator*(double3 a, double s) { return make_double3(a.x * s, a.y * s, a.z * s); }
HD float3 diff3(double3 a, double3 b) { return make_float3(float(a.x - b.x), float(a.y - b.y), float(a.z - b.z)); }   // a - b

// Quaternions (x, y, z, w), unit, rotating body frame to world.
HD float4 qmul(float4 a, float4 b) {
    return make_float4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                       a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
HD float4 qconj(float4 q) { return make_float4(-q.x, -q.y, -q.z, q.w); }
HD float4 qnormalize(float4 q) {
    const float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 0.0f ? q * (1.0f / n) : make_float4(0, 0, 0, 1);
}
HD float3 qrot(float4 q, float3 v) {   // q v q*
    const float3 u = make_float3(q.x, q.y, q.z);
    const float3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
HD float3 qrotInv(float4 q, float3 v) { return qrot(qconj(q), v); }
// q += 0.5 [w, 0] q (small rotation by the world-space vector w), normalised
HD float4 qaddRot(float4 q, float3 w) {
    const float4 d = qmul(make_float4(w.x, w.y, w.z, 0.0f), q) * 0.5f;
    return qnormalize(q + d);
}
HD float4 qaxis(float3 axis, float angle) {   // axis unit
    const float s = sinf(0.5f * angle);
    return make_float4(axis.x * s, axis.y * s, axis.z * s, cosf(0.5f * angle));
}
// rotation matrix rows from a quaternion
HD void qmat(float4 q, float3& r0, float3& r1, float3& r2) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    r0 = make_float3(1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w));
    r1 = make_float3(2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w));
    r2 = make_float3(2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y));
}
// world-space inverse inertia applied to v: R diag(invI) R^T v
HD float3 invInertiaMul(float4 q, float3 invI, float3 v) { return qrot(q, qrotInv(q, v) * invI); }
HD float lengthSq(float3 a) { return dot(a, a); }
