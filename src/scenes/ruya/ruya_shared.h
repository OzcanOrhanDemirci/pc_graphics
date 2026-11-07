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
inline constexpr float kCellSize = 124.0f;
// Dense fog hides the cut-off: transmittance at kFarDistance is under 4 %.
inline constexpr float kFarDistance = 640.0f;
inline constexpr float kTransportDistance = 320.0f;
// Island stacks rise from this depth, far inside the cloud sea.
inline constexpr float kIslandBase = -230.0f;
inline constexpr float kIslandBoundsY = -60.0f;
inline constexpr float kIslandBoundsRadius = 205.0f;
inline constexpr float kFovY = 48.0f;
inline constexpr float kTurnRate = 0.85f;
inline constexpr int kPrototypeCount = 27;
inline constexpr unsigned kCameraRayMask = 1;
inline constexpr unsigned kTransportRayMask = 2;
inline constexpr float kCellBoundsY = -18.0f;
inline constexpr float kCellBoundsRadius = 84.0f;
inline constexpr float kMaxCellScale = 1.38f;
inline constexpr unsigned kMaterialStone = 0;
inline constexpr unsigned kMaterialDarkStone = 1;
inline constexpr unsigned kMaterialBronze = 2;
inline constexpr unsigned kMaterialLeaf = 3;
inline constexpr unsigned kMaterialWater = 4;
inline constexpr unsigned kMaterialMasonry = 5;
// CC0 texture sets: rock, grass ground, cut stone. Two maps each.
inline constexpr int kTextureSets = 3;
inline constexpr unsigned kIslandSlots = 2048;
inline constexpr unsigned kIslandGraceFrames = 90;

HD unsigned hash(unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}
HD unsigned cellSeed(int x, int z) {
    // Nested so that (x, z) and (-x, -z) do not share a seed, as a plain xor of two odd products does.
    return hash(hash(unsigned(x) * 0x9e3779b9U + 0x52555941U) ^ unsigned(z) * 0x85ebca6bU);
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
    unsigned region, seed;
    float3 offset;
    float scale, yaw;
};
struct Piece {
    float3 offset;
    float3 scale;
    float yaw;
    unsigned prototype, seed, region;
};
struct InstanceData { unsigned prototype, seed, region; };
HD Cell describeCell(int x, int z) {
    Cell c{};
    c.x = x;
    c.z = z;
    c.seed = cellSeed(x, z);
    const int rx = int(floorf((float(x) + 0.8f * randomUnit(c.seed + 17)) / 4.0f));
    const int rz = int(floorf((float(z) + 0.8f * randomUnit(c.seed + 23)) / 4.0f));
    c.region = cellSeed(rx + 91, rz - 73) % 4U;
    const float vacancy = c.region == 2 ? 0.30f : 0.20f;
    c.offset = make_float3(x * kCellSize + (randomUnit(c.seed + 1) - 0.5f) * 28.0f, 0.0f,
                          z * kCellSize + (randomUnit(c.seed + 3) - 0.5f) * 28.0f);
    c.scale = randomUnit(c.seed + 11) < vacancy && (x != 0 || z != 0) ? 0.0f : 1.0f;
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
    const InstanceData* instances;
    const unsigned long long* textures;
    float4* color;
    float4* illumination;
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
static_assert(sizeof(InstanceData) == 12);
static_assert(sizeof(Params) == 184);
static_assert(offsetof(Params, geometries) == 96);
static_assert(std::is_trivially_copyable_v<Params>);
#endif
}
