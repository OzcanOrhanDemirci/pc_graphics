// Small vector math shared by host (MSVC), nvcc and NVRTC (device programs).
#pragma once

#if defined(__CUDACC_RTC__)
// NVRTC: vector types and math are built in.
#else
#include <cuda_runtime.h>
#include <cmath>
#endif

#define HD __host__ __device__ __forceinline__

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif
#define INV_PIf 0.31830988618379067154f

HD float3 operator+(float3 a, float3 b) { return make_float3(a.x + b.x, a.y + b.y, a.z + b.z); }
HD float3 operator-(float3 a, float3 b) { return make_float3(a.x - b.x, a.y - b.y, a.z - b.z); }
HD float3 operator-(float3 a) { return make_float3(-a.x, -a.y, -a.z); }
HD float3 operator*(float3 a, float3 b) { return make_float3(a.x * b.x, a.y * b.y, a.z * b.z); }
HD float3 operator*(float3 a, float s) { return make_float3(a.x * s, a.y * s, a.z * s); }
HD float3 operator*(float s, float3 a) { return make_float3(a.x * s, a.y * s, a.z * s); }
HD float3 operator/(float3 a, float s) { float r = 1.0f / s; return a * r; }
HD float3& operator+=(float3& a, float3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
HD float3& operator-=(float3& a, float3 b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
HD float3& operator*=(float3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }
HD float3& operator*=(float3& a, float3 b) { a.x *= b.x; a.y *= b.y; a.z *= b.z; return a; }

HD float4 operator+(float4 a, float4 b) { return make_float4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
HD float4 operator-(float4 a, float4 b) { return make_float4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
HD float4 operator*(float4 a, float4 b) { return make_float4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); }
HD float4 operator*(float4 a, float s) { return make_float4(a.x * s, a.y * s, a.z * s, a.w * s); }
HD float4 operator*(float s, float4 a) { return a * s; }
HD float4 operator/(float4 a, float4 b) { return make_float4(a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w); }
HD float4& operator+=(float4& a, float4 b) { a.x += b.x; a.y += b.y; a.z += b.z; a.w += b.w; return a; }
HD float4& operator*=(float4& a, float4 b) { a.x *= b.x; a.y *= b.y; a.z *= b.z; a.w *= b.w; return a; }
HD float4& operator*=(float4& a, float s) { a.x *= s; a.y *= s; a.z *= s; a.w *= s; return a; }
HD float4 f4(float s) { return make_float4(s, s, s, s); }
HD float hsum(float4 a) { return a.x + a.y + a.z + a.w; }
HD float hmax(float4 a) { return fmaxf(fmaxf(a.x, a.y), fmaxf(a.z, a.w)); }

HD float2 operator+(float2 a, float2 b) { return make_float2(a.x + b.x, a.y + b.y); }
HD float2 operator-(float2 a, float2 b) { return make_float2(a.x - b.x, a.y - b.y); }
HD float2 operator*(float2 a, float s) { return make_float2(a.x * s, a.y * s); }

HD float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
HD float3 cross(float3 a, float3 b) { return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
HD float length(float3 a) { return sqrtf(dot(a, a)); }
HD float3 normalize(float3 a) { return a * (1.0f / sqrtf(dot(a, a))); }
HD float3 fminf3(float3 a, float3 b) { return make_float3(fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z)); }
HD float3 fmaxf3(float3 a, float3 b) { return make_float3(fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z)); }
HD float clampf(float x, float a, float b) { return fminf(fmaxf(x, a), b); }
HD float saturate(float x) { return clampf(x, 0.0f, 1.0f); }
HD float lerpf(float a, float b, float t) { return a + (b - a) * t; }
HD float3 lerp3(float3 a, float3 b, float t) { return a + (b - a) * t; }
HD float smoothstepf(float a, float b, float x) { float t = saturate((x - a) / (b - a)); return t * t * (3.0f - 2.0f * t); }
HD float sqr(float x) { return x * x; }
HD float3 reflect3(float3 d, float3 n) { return d - n * (2.0f * dot(d, n)); }
HD float maxc(float3 a) { return fmaxf(a.x, fmaxf(a.y, a.z)); }

// Orthonormal basis from a unit normal (Duff et al. 2017).
HD void makeBasis(float3 n, float3& t, float3& b) {
    const float s = copysignf(1.0f, n.z);
    const float a = -1.0f / (s + n.z);
    const float c = n.x * n.y * a;
    t = make_float3(1.0f + s * n.x * n.x * a, s * c, -s * n.x);
    b = make_float3(c, s + n.y * n.y * a, -n.y);
}

struct Frame {
    float3 t, b, n;
    HD static Frame from(float3 n) { Frame f; f.n = n; makeBasis(n, f.t, f.b); return f; }
    HD float3 toLocal(float3 v) const { return make_float3(dot(v, t), dot(v, b), dot(v, n)); }
    HD float3 toWorld(float3 v) const { return t * v.x + b * v.y + n * v.z; }
};
