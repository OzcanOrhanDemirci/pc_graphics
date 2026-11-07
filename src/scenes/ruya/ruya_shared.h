// Immutable recipes keep regenerated cells identical. The optical instance
// table is frame-local and distinguishes camera rays from light transport.
#pragma once
#include "shared/vec.h"

#if !defined(__CUDACC_RTC__)
#include <optix_types.h>
#include <type_traits>
#include <cstddef>
#endif

namespace ruya {
inline constexpr float kCellSize = 72.0f;
inline constexpr float kFarDistance = 1100.0f;
inline constexpr float kFovY = 48.0f;
inline constexpr float kTurnRate = 0.85f;
inline constexpr int kPrototypeCount = 16;
inline constexpr unsigned kCameraRayMask = 1;
inline constexpr unsigned kTransportRayMask = 2;
inline constexpr float kCellBoundsY = -20.0f;
inline constexpr float kCellBoundsRadius = 64.0f;
inline constexpr unsigned kMaterialStone = 0;
inline constexpr unsigned kMaterialDarkStone = 1;
inline constexpr unsigned kMaterialBronze = 2;
inline constexpr unsigned kMaterialLeaf = 3;
inline constexpr unsigned kMaterialWater = 4;

HD unsigned hash(unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}
HD unsigned cellSeed(int x, int z) {
    return hash(unsigned(x) * 0x9e3779b9U ^ unsigned(z) * 0x85ebca6bU ^ 0x52555941U);
}
HD float randomUnit(unsigned x) { return float(hash(x) >> 8) * (1.0f / 16777216.0f); }

struct Vertex {
    float3 position;
    float3 normal;
    unsigned material;
};
struct Geometry {
    const Vertex* vertices;
    const uint3* indices;
};
struct Cell {
    int x, z;
    unsigned prototype, seed;
    float3 offset;
    float scale, yaw;
};
HD Cell describeCell(int x, int z) {
    Cell c{};
    c.x = x;
    c.z = z;
    c.seed = cellSeed(x, z);
    c.prototype = c.seed % kPrototypeCount;
    c.offset = make_float3(x * kCellSize + (randomUnit(c.seed + 1) - 0.5f) * 17.0f,
                          (randomUnit(c.seed + 2) - 0.5f) * 76.0f,
                          z * kCellSize + (randomUnit(c.seed + 3) - 0.5f) * 17.0f);
    c.scale = 0.80f + 0.38f * randomUnit(c.seed + 4);
    c.yaw = randomUnit(c.seed + 5) * (2.0f * M_PIf);
    return c;
}
struct Camera {
    float3 eye{26.0f, 20.0f, 96.0f};
    float yaw = -0.12f, pitch = -0.13f;
    float fovY = kFovY;
};
struct FrameStats {
    float traceMs = 0, denoiseMs = 0, displayMs = 0, uploadMs = 0;
    unsigned badSamples = 0;
    size_t residentBytes = 0, freeBytes = 0;
    unsigned cells = 0, transportCells = 0;
    size_t opticalPoolBytes = 0;
    unsigned long long instancedTriangles = 0;
};
struct Params {
    OptixTraversableHandle handle;
    unsigned width, height, layout;
    int spp, bounces, sampleOffset;
    int raw, materialDetail;
    float exposure;
    float3 eye, U, V, W;
    const Geometry* geometries;
    const Cell* cells;
    float4* color;
    float4* albedo;
    float4* normal;
    float4* filtered;
    float4* depth;
    uchar4* output;
    unsigned* badCount;
};
#if !defined(__CUDACC_RTC__)
static_assert(sizeof(Vertex) == 28);
static_assert(sizeof(Geometry) == 16);
static_assert(sizeof(Cell) == 36);
static_assert(sizeof(Params) == 168);
static_assert(offsetof(Params, geometries) == 96);
static_assert(std::is_trivially_copyable_v<Params>);
#endif
}
