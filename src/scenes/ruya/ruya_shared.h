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
inline constexpr unsigned kMaterialBark = 6;
inline constexpr unsigned kMaterialRoof = 7;
inline constexpr unsigned kMaterialWood = 8;
inline constexpr unsigned kMaterialLeafCard = 9;
// A vertex keeps the material id in the low byte. Leaf cards use the rest:
// corner u, corner v, atlas, atlas cell, and a per-card colour value.
inline constexpr unsigned kMaterialIdMask = 0xFFU;
inline constexpr unsigned kCardCornerU = 1U << 8;
inline constexpr unsigned kCardCornerV = 1U << 9;
inline constexpr unsigned kCardAtlasShift = 10;
inline constexpr unsigned kCardCellShift = 11;
inline constexpr unsigned kCardTintShift = 16;
inline constexpr unsigned kCardOpenShift = 24;
HD unsigned materialId(unsigned material) { return material & kMaterialIdMask; }
// Atlas cells holding whole sprigs (u0, v0, u1, v1); atlas 0 is broadleaf, 1 is needles.
HD float4 cardRect(unsigned atlas, unsigned cell) {
    if (atlas == 0) {
        if (cell == 0) return make_float4(0.215f, 0.12f, 0.56f, 0.68f);
        if (cell == 1) return make_float4(0.52f, 0.02f, 0.84f, 0.55f);
        if (cell == 2) return make_float4(0.00f, 0.30f, 0.255f, 0.60f);
        return make_float4(0.00f, 0.00f, 0.39f, 0.30f);
    }
    if (cell == 0) return make_float4(0.309f, 0.406f, 0.641f, 0.775f);
    if (cell == 1) return make_float4(0.645f, 0.457f, 0.951f, 0.83f);
    if (cell == 2) return make_float4(0.654f, 0.053f, 0.936f, 0.369f);
    return make_float4(0.189f, 0.053f, 0.408f, 0.311f);
}
HD bool isFoliage(unsigned material) { return materialId(material) == kMaterialLeaf || materialId(material) == kMaterialLeafCard; }
// CC0 scan sets, two GPU maps each: colour + roughness, and normal + AO + height (alpha cut for leaf sets).
inline constexpr int kSetRock = 0, kSetGroundFar = 1, kSetMasonry = 2, kSetCliff = 3, kSetGrass = 4, kSetBark = 5,
                     kSetRoof = 6, kSetWood = 7, kSetMetal = 8, kSetBroadleaf = 9, kSetNeedle = 10;
inline constexpr int kTextureSets = 11;
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
// The world is laid out in massifs of kMassifCells cells square. Each massif
// has one hero stack; winding empty corridors separate the massifs.
inline constexpr int kMassifCells = 5;
inline constexpr float kCorridorHalfWidth = 62.0f;

HD int floorDivide(int a, int b) {
    const int q = a / b;
    return ((a % b) != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
HD void cellJitter(int x, int z, unsigned seed, float& wx, float& wz) {
    wx = x * kCellSize + (randomUnit(seed + 1) - 0.5f) * 42.0f;
    wz = z * kCellSize + (randomUnit(seed + 3) - 0.5f) * 42.0f;
}
HD void massifHeroCell(int mx, int mz, int& cx, int& cz) {
    const unsigned h = hash(hash(unsigned(mx) * 0x9e3779b9U + 0x48455230U) ^ unsigned(mz) * 0x85ebca6bU);
    cx = mx * kMassifCells + 1 + int(h % 3U);
    cz = mz * kMassifCells + 1 + int((h >> 11) % 3U);
}
struct MassifInfo {
    float heroX, heroZ, distance;
    bool isHero;
};
// Closest hero stack to a cell, searched over the neighbouring massifs.
HD MassifInfo nearestHero(int x, int z) {
    const int mx = floorDivide(x, kMassifCells);
    const int mz = floorDivide(z, kMassifCells);
    float wx, wz;
    cellJitter(x, z, cellSeed(x, z), wx, wz);
    MassifInfo best{};
    best.distance = 1e9f;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            int hx, hz;
            massifHeroCell(mx + i, mz + j, hx, hz);
            float hwx, hwz;
            cellJitter(hx, hz, cellSeed(hx, hz), hwx, hwz);
            const float d = sqrtf((hwx - wx) * (hwx - wx) + (hwz - wz) * (hwz - wz));
            if (d < best.distance) {
                best.distance = d;
                best.heroX = hwx;
                best.heroZ = hwz;
                best.isHero = hx == x && hz == z;
            }
        }
    return best;
}
// Distance from a world point to the centre line of the nearest corridor.
HD float corridorDistance(float wx, float wz) {
    const float period = float(kMassifCells) * kCellSize;
    const auto line = [](float along, float phase) {
        return 60.0f * (0.62f * sinf(along * 0.00505f + phase) + 0.38f * sinf(along * 0.0123f + 2.1f * phase));
    };
    const auto nearest = [&](float across, float along, unsigned salt) {
        const float start = floorf((across + 0.5f * kCellSize) / period);
        float distance = 1e9f;
        for (int k = 0; k <= 1; ++k) {
            const float index = start + float(k);
            const float phase = 6.2831853f * randomUnit(unsigned(int(index)) * 0x9e3779b9U + salt);
            const float center = index * period - 0.5f * kCellSize + line(along, phase);
            distance = fminf(distance, fabsf(across - center));
        }
        return distance;
    };
    return fminf(nearest(wz, wx, 0x434fU), nearest(wx, wz, 0x5252U));
}
HD Cell describeCell(int x, int z) {
    Cell c{};
    c.x = x;
    c.z = z;
    c.seed = cellSeed(x, z);
    const int rx = int(floorf((float(x) + 0.8f * randomUnit(c.seed + 17)) / 4.0f));
    const int rz = int(floorf((float(z) + 0.8f * randomUnit(c.seed + 23)) / 4.0f));
    c.region = cellSeed(rx + 91, rz - 73) % 4U;
    float wx, wz;
    cellJitter(x, z, c.seed, wx, wz);
    const MassifInfo hero = nearestHero(x, z);
    // Cells lean towards their hero, so a massif tightens at its core.
    const float closeness = expf(-(hero.distance * hero.distance) / (170.0f * 170.0f));
    if (!hero.isHero && hero.distance > 1.0f) {
        const float pull = 24.0f * closeness / hero.distance;
        wx += (hero.heroX - wx) * pull;
        wz += (hero.heroZ - wz) * pull;
    }
    c.offset = make_float3(wx, 0.0f, wz);
    const bool corridor = corridorDistance(float(x) * kCellSize, float(z) * kCellSize) < kCorridorHalfWidth;
    const bool pocket = !hero.isHero && closeness < 0.6f && randomUnit(c.seed + 11) < 0.07f;
    c.scale = (corridor || pocket) && !hero.isHero && (x != 0 || z != 0) ? 0.0f : 1.0f;
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
    float textureSide[kTextureSets];
};
#if !defined(__CUDACC_RTC__)
static_assert(sizeof(Vertex) == 28);
static_assert(sizeof(Geometry) == 16);
static_assert(sizeof(Cell) == 36);
static_assert(sizeof(InstanceData) == 12);
static_assert(sizeof(Params) == 232);
static_assert(offsetof(Params, geometries) == 96);
static_assert(std::is_trivially_copyable_v<Params>);
#endif
}
