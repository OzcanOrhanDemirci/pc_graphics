// Islands are polar height functions: a wall radius per angle and depth below
// the rim, and a top height per point. The plan is a union of convex columns,
// so corners and re-entrant joints stay sharp; the side profile, bedding and
// shelves are read in absolute height so that neighbouring stacks share strata.
#include "scenes/ruya/ruya_island.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ruya {
namespace {
constexpr float kTau = 2.0f * M_PIf;
constexpr float kBedSpacing = 4.2f;
constexpr float kShelfRamp = 0.32f;
constexpr float kDepthRows[] = {0.0f, 0.3f, 0.7f, 1.1f, 1.6f, 2.2f, 2.9f, 3.7f, 4.6f, 5.6f, 6.7f, 8.0f, 9.4f, 11.0f, 12.8f, 14.8f};
constexpr int kDepthRowCount = int(sizeof(kDepthRows) / sizeof(kDepthRows[0]));
// Below this height only cloud can see the rock: the bedding fades out.
constexpr float kBeddingBottom = -120.0f;

float valueNoise(float3 p, unsigned seed) {
    const float fx = std::floor(p.x);
    const float fy = std::floor(p.y);
    const float fz = std::floor(p.z);
    const int x = int(fx);
    const int y = int(fy);
    const int z = int(fz);
    const float tx = p.x - fx;
    const float ty = p.y - fy;
    const float tz = p.z - fz;
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sy = ty * ty * (3.0f - 2.0f * ty);
    const float sz = tz * tz * (3.0f - 2.0f * tz);
    const auto at = [&](int i, int j, int k) {
        return randomUnit(unsigned(x + i) * 73856093U ^ unsigned(y + j) * 19349663U ^ unsigned(z + k) * 83492791U ^ seed);
    };
    const float a = lerpf(lerpf(at(0, 0, 0), at(1, 0, 0), sx), lerpf(at(0, 1, 0), at(1, 1, 0), sx), sy);
    const float b = lerpf(lerpf(at(0, 0, 1), at(1, 0, 1), sx), lerpf(at(0, 1, 1), at(1, 1, 1), sx), sy);
    return lerpf(a, b, sz);
}
float fbm(float3 p, unsigned seed, int octaves) {
    float sum = 0;
    float amplitude = 0.5f;
    float total = 0;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += amplitude * valueNoise(p, seed + unsigned(octave) * 1013U);
        total += amplitude;
        p = p * 2.03f;
        amplitude *= 0.5f;
    }
    return sum / total;
}
float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
float positiveAngle(float angle) {
    const float wrapped = std::fmod(angle, kTau);
    return wrapped < 0 ? wrapped + kTau : wrapped;
}

float columnExit(const Column& column, float c, float s) {
    float exit = 1e9f;
    for (int k = 0; k < column.sides; ++k) {
        const float facing = column.nx[k] * c + column.nz[k] * s;
        if (facing > 1e-4f) exit = std::min(exit, column.d[k] / facing);
    }
    return exit;
}
void columnExits(const Island& island, float c, float s, float* exits) {
    for (int i = 0; i < island.columnCount; ++i) exits[i] = columnExit(island.columns[i], c, s);
}
// Radius of the column union at a height. Shelves are columns with a finite top.
float planRadius(const Island& island, const float* exits, float y) {
    float core = 0;
    for (int i = 0; i < island.coreColumns; ++i) core = std::max(core, exits[i]);
    float radius = core;
    for (int i = island.coreColumns; i < island.columnCount; ++i) {
        const float weight = 1.0f - smooth(island.columns[i].topY - kShelfRamp, island.columns[i].topY, y);
        if (weight > 0) radius = std::max(radius, lerpf(core, std::max(core, exits[i]), weight));
    }
    return radius;
}
float profileAt(const Profile& profile, float depth) {
    if (depth <= profile.depth[0]) return profile.scale[0];
    for (int i = 1; i < profile.count; ++i)
        if (depth <= profile.depth[i]) {
            const float t = (depth - profile.depth[i - 1]) / (profile.depth[i] - profile.depth[i - 1]);
            return lerpf(profile.scale[i - 1], profile.scale[i], t);
        }
    return profile.scale[profile.count - 1];
}

float bedHardness(int k) {
    const float h = randomUnit(hash(unsigned(k) * 0x85ebca6bU + 0x7ab51fU));
    return std::pow(h, 1.4f);
}
struct Bed {
    int index;
    float bottom, top, hardness, hardnessAbove;
};
Bed bedAt(float y) {
    const int k = bedIndexAt(y);
    return {k, bedFloor(k), bedFloor(k + 1), bedHardness(k), bedHardness(k + 1)};
}
// Hash of a block along a bed. The block count is an integer so the seam at 2 pi wraps.
float blockValue(const Island& island, int bed, float angle, float width) {
    const int count = std::max(6, int(std::round(kTau * island.radius / width)));
    const float v = positiveAngle(angle) / kTau * float(count);
    const float f = std::floor(v);
    const int block = int(f) % count;
    const auto at = [&](int b) {
        return randomUnit(hash(unsigned(b) * 0x9e3779b9U ^ hash(unsigned(bed) * 0x85ebca6bU + island.seed)));
    };
    const float edge = std::min(0.16f / width, 0.5f);
    return lerpf(at(block), at((block + 1) % count), smooth(1.0f - edge, 1.0f, v - f));
}
// Radial offset of the bedding at a point: hard beds stand out, a recessed
// parting runs under each bed, and the top of a bed is a narrow flat ledge.
float bedOffset(const Island& island, float angle, float depth, const Bed& bed, float y) {
    if (depth <= 0.2f || y < kBeddingBottom) return 0;
    const float fade = smooth(0.2f, 1.6f, depth) * smooth(kBeddingBottom, kBeddingBottom + 30.0f, y);
    const float width = 2.0f + 3.2f * randomUnit(hash(unsigned(bed.index) + 0x51edU));
    const float standOut = island.strata * bed.hardness * (0.35f + 0.85f * blockValue(island, bed.index, angle, width));
    const float block = 0.38f * (blockValue(island, bed.index + 7777, angle, width * 0.62f) - 0.5f) * (0.5f + bed.hardness);
    const float recess = island.strata * bed.hardness * 0.55f;
    const float recessAbove = island.strata * bed.hardnessAbove * 0.55f;
    const float above = y - bed.bottom;
    const float below = bed.top - y;
    float offset;
    if (above <= 0.3f) offset = -recess;
    else if (above <= 0.42f) offset = lerpf(-recess, standOut, (above - 0.3f) / 0.12f);
    else if (below >= 0.25f) offset = standOut + block;
    else offset = lerpf(standOut + block, -recessAbove, 1.0f - below / 0.25f);
    return offset * fade;
}
float jointOffset(const Island& island, float angle, float y) {
    float offset = 0;
    for (int i = 0; i < island.jointCount; ++i) {
        const Joint& joint = island.joints[i];
        const float across = std::abs(std::remainder(angle - joint.angle, kTau)) * island.radius;
        if (across >= joint.width) continue;
        const float along = smooth(joint.bottomY - 12.0f, joint.bottomY + 10.0f, y);
        offset = std::min(offset, -joint.depth * (1.0f - smooth(joint.width * 0.12f, joint.width, across)) * along);
    }
    return offset;
}
float alcoveOffset(const Island& island, float angle, float y) {
    float offset = 0;
    for (int i = 0; i < island.alcoveCount; ++i) {
        const Alcove& alcove = island.alcoves[i];
        if (y > alcove.topY || y < alcove.bottomY) continue;
        const float across = std::abs(std::remainder(angle - alcove.angle, kTau));
        if (across >= alcove.halfAngle) continue;
        const float roof = 1.0f - smooth(alcove.topY - 1.4f, alcove.topY, y);
        const float floor = smooth(alcove.bottomY, alcove.bottomY + 2.5f, y);
        offset = std::min(offset, -alcove.depth * (1.0f - smooth(alcove.halfAngle * 0.55f, alcove.halfAngle, across)) * roof * floor);
    }
    return offset;
}
// Metre-scale rock independent of the island's size; fades where cloud hides it.
float wallRelief(const Island& island, float c, float s, float virtualY, float depth) {
    if (depth > 140.0f) return 0;
    const float fade = 1.0f - smooth(100.0f, 140.0f, depth);
    const float rx = c * island.radius;
    const float rz = s * island.radius;
    const float flute = fbm(make_float3(rx * 0.14f, virtualY * 0.035f, rz * 0.14f), island.seed + 5U, 2) - 0.5f;
    const float grain = valueNoise(make_float3(rx * 0.55f, virtualY * 0.42f, rz * 0.55f), island.seed + 17U) - 0.5f;
    return (0.8f * flute + 0.6f * grain) * fade;
}
float wallRadiusAt(const Island& island, float angle, float c, float s, const float* exits, float depth, float y, const Bed& bed) {
    const float virtualY = island.top - depth;
    float radius = planRadius(island, exits, y) * profileAt(island.profile, depth);
    radius += (blockValue(island, 0x7fff, angle, 3.4f) - 0.5f) * island.capBreak * (1.0f - smooth(1.0f, 4.0f, depth));
    radius += bedOffset(island, angle, depth, bed, y);
    radius += jointOffset(island, angle, y);
    radius += alcoveOffset(island, angle, y);
    radius += wallRelief(island, c, s, virtualY, depth);
    return radius;
}
// The rim radius does not depend on height, which keeps wall and crown from
// depending on each other; bedding and alcoves vanish at zero depth.
float rimRadiusAt(const Island& island, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    float exits[kIslandMaxColumns];
    columnExits(island, c, s, exits);
    return wallRadiusAt(island, angle, c, s, exits, 0.0f, kColumnFullHeight, Bed{});
}
float wallRadiusBelowRim(const Island& island, float angle, float depth, float y) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    float exits[kIslandMaxColumns];
    columnExits(island, c, s, exits);
    return wallRadiusAt(island, angle, c, s, exits, depth, y, bedAt(y));
}

float forestField(const Island& island, float x, float z) {
    return valueNoise(make_float3(x * 0.021f, 2.7f, z * 0.021f), island.seed + 61U);
}
// Height of the top surface before structure sites are levelled.
float crownHeight(const Island& island, float x, float z, float rim) {
    const float dx = x - island.x;
    const float dz = z - island.z;
    const float reach = std::sqrt(dx * dx + dz * dz);
    const float s = reach / std::max(rim, 1.0f);
    float h = island.top + island.tiltX * dx + island.tiltZ * dz;
    h += island.undulate * (fbm(make_float3(x * 0.05f, 0.0f, z * 0.05f), island.seed + 21U, 3) - 0.5f) * 2.0f;
    h += 0.35f * (valueNoise(make_float3(x * 0.38f, 1.7f, z * 0.38f), island.seed + 33U) - 0.5f);
    if (island.terrace > 0) {
        // Three stepped tiers follow the plan polygon inwards from the rim.
        const float riser = std::min(1.1f / std::max(island.radius, 1.0f), 0.04f);
        const float in = 1.0f - std::min(s, 1.0f);
        for (float boundary : {0.16f, 0.40f, 0.64f}) h += island.terrace * smooth(boundary - riser, boundary + riser, in);
    }
    if (island.plates > 0) {
        // Bare rock plates break the crown; they keep away from the rim, which
        // must stay the crown's lowest line for the wall to meet it.
        const float fade = 1.0f - smooth(0.70f, 0.86f, s);
        if (fade > 0) {
            const float v = fbm(make_float3(x * 0.043f, 5.1f, z * 0.043f), island.seed + 45U, 2) * 5.0f;
            const float step = std::floor(v);
            const float rocky = 1.0f - smooth(0.45f, 0.65f, forestField(island, x, z));
            h += island.plates * fade * (0.35f + 0.65f * rocky) * (step + smooth(0.82f, 1.0f, v - step));
        }
    }
    // The edge is broken caprock: whole blocks stand lower than their neighbours.
    const float edge = std::max(1.0f - s, 0.0f) * rim;
    if (edge < 2.6f) {
        const float angle = std::atan2(dz, dx);
        const float chip = std::max(blockValue(island, 0x7ffe, angle, 4.1f) - 0.45f, 0.0f) * 2.6f * island.capBreak;
        const float t = 1.0f - edge / 2.6f;
        h -= chip * smooth(0.0f, 1.0f, t) + 0.55f * t * t;
    }
    return h;
}
float topAt(const Island& island, float x, float z, float rim) {
    float h = crownHeight(island, x, z, rim);
    for (int i = 0; i < island.siteCount; ++i) {
        const Site& site = island.sites[i];
        const float d = std::hypot(x - site.x, z - site.z);
        h = lerpf(h, site.height, 1.0f - smooth(site.radius, site.radius * 1.55f + 1.5f, d));
    }
    return h;
}

void addColumn(Island& island, float ox, float oz, float radius, int sides, unsigned seed, float topY) {
    Column& column = island.columns[island.columnCount++];
    column.topY = topY;
    column.sides = std::min(sides, kIslandMaxSides);
    const float phase = kTau * randomUnit(seed);
    for (int k = 0; k < column.sides; ++k) {
        const unsigned key = seed + unsigned(k) * 37U;
        const float a = phase + kTau * (float(k) + 0.7f * (randomUnit(key + 1U) - 0.5f)) / float(column.sides);
        column.nx[k] = std::cos(a);
        column.nz[k] = std::sin(a);
        const float distance = radius * lerpf(0.84f, 1.14f, randomUnit(key + 2U));
        column.d[k] = std::max(distance + column.nx[k] * ox + column.nz[k] * oz, 0.18f * radius);
    }
}
void setProfile(Island& island, std::initializer_list<std::pair<float, float>> keys, float jitter) {
    Profile& profile = island.profile;
    profile.count = 0;
    for (const auto& [depth, scale] : keys) {
        const unsigned key = island.seed + 71U * unsigned(profile.count);
        profile.depth[profile.count] = depth;
        profile.scale[profile.count] = scale * (profile.count < 2 ? 1.0f : 1.0f + jitter * (randomUnit(key) - 0.5f));
        ++profile.count;
    }
}
float meanCoreRadius(const Island& island) {
    float mean = 0;
    constexpr int kSamples = 180;
    for (int i = 0; i < kSamples; ++i) {
        const float angle = kTau * float(i) / float(kSamples);
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        float core = 0;
        for (int k = 0; k < island.coreColumns; ++k) core = std::max(core, columnExit(island.columns[k], c, s));
        mean += core;
    }
    return mean / float(kSamples);
}
float maxReach(const Island& island) {
    float reach = 0;
    constexpr int kSamples = 180;
    for (int i = 0; i < kSamples; ++i) {
        const float angle = kTau * float(i) / float(kSamples);
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        for (int k = 0; k < island.columnCount; ++k) reach = std::max(reach, columnExit(island.columns[k], c, s));
    }
    return reach;
}
void scaleColumns(Island& island, float factor) {
    for (int i = 0; i < island.columnCount; ++i)
        for (int k = 0; k < island.columns[i].sides; ++k) island.columns[i].d[k] *= factor;
}
}  // namespace

int bedIndexAt(float y) {
    int k = int(std::floor(y / kBedSpacing));
    while (bedFloor(k) > y) --k;
    while (bedFloor(k + 1) <= y) ++k;
    return k;
}
float bedFloor(int k) {
    return kBedSpacing * float(k) + 3.0f * (randomUnit(hash(unsigned(k) * 0x9e3779b9U + 0x6265U)) - 0.5f);
}

Island makeIsland(float x, float z, unsigned seed, unsigned kind, float radius, float top) {
    Island island{};
    island.x = x;
    island.z = z;
    island.seed = seed;
    island.kind = kind;
    island.radius = radius;
    island.top = top;
    const auto draw = [&](unsigned k) { return randomUnit(seed + 613U * k); };
    const float tiltMagnitude = kind == kKindSpire ? 0.05f : 0.018f;
    island.tiltX = tiltMagnitude * (2.0f * draw(1) - 1.0f);
    island.tiltZ = tiltMagnitude * (2.0f * draw(2) - 1.0f);
    int extraColumns = 0;
    int coreSides = 6;
    switch (kind) {
    case kKindMesa:  // Meteora: sheer blocks and a flat cap
        setProfile(island, {{0.0f, 1.0f}, {1.4f, 1.022f}, {2.0f, 0.965f}, {16.0f, 0.955f}, {60.0f, 1.02f}, {130.0f, 1.14f}, {300.0f, 1.3f}}, 0.08f);
        island.strata = lerpf(1.1f, 2.0f, draw(3));
        island.capBreak = lerpf(0.5f, 0.95f, draw(4));
        island.undulate = lerpf(0.6f, 1.1f, draw(5));
        island.plates = lerpf(0.5f, 0.9f, draw(6));
        extraColumns = 2 + int(draw(7) * 3.0f);
        coreSides = 5 + int(draw(8) * 3.0f);
        break;
    case kKindTerrace:  // stepped garden: wide tiers carved around a smaller core
        setProfile(island, {{0.0f, 1.0f}, {1.2f, 1.018f}, {1.9f, 0.975f}, {26.0f, 1.04f}, {90.0f, 1.2f}, {220.0f, 1.45f}}, 0.08f);
        island.strata = lerpf(1.0f, 1.7f, draw(3));
        island.capBreak = lerpf(0.4f, 0.75f, draw(4));
        island.undulate = lerpf(0.5f, 0.9f, draw(5));
        island.plates = lerpf(0.2f, 0.45f, draw(6));
        island.terrace = lerpf(2.6f, 4.0f, draw(9));
        extraColumns = 1 + int(draw(7) * 3.0f);
        break;
    case kKindSpire:  // Zhangjiajie: slender quartzite with deep joints and a waist
        setProfile(island, {{0.0f, 1.0f}, {1.1f, 1.04f}, {1.7f, 0.97f}, {18.0f, 1.08f}, {34.0f, 0.92f}, {90.0f, 1.32f}, {230.0f, 2.0f}}, 0.12f);
        island.strata = lerpf(1.2f, 2.1f, draw(3));
        island.capBreak = lerpf(0.5f, 1.0f, draw(4));
        island.undulate = lerpf(0.3f, 0.6f, draw(5));
        island.plates = lerpf(0.6f, 1.1f, draw(6));
        extraColumns = int(draw(7) * 2.0f);
        coreSides = 4 + int(draw(8) * 2.0f);
        break;
    default:  // wooded crown: Guilin karst, a steep cone with a soft green cap
        setProfile(island, {{0.0f, 1.0f}, {1.0f, 1.025f}, {1.6f, 0.98f}, {14.0f, 1.03f}, {36.0f, 1.14f}, {96.0f, 1.3f}, {220.0f, 1.5f}}, 0.1f);
        island.strata = lerpf(0.9f, 1.5f, draw(3));
        island.capBreak = lerpf(0.4f, 0.8f, draw(4));
        island.undulate = lerpf(1.2f, 2.0f, draw(5));
        island.plates = lerpf(0.15f, 0.4f, draw(6));
        extraColumns = 2 + int(draw(7) * 2.0f);
        break;
    }
    addColumn(island, radius * 0.12f * (draw(10) - 0.5f), radius * 0.12f * (draw(11) - 0.5f), radius * 0.82f, coreSides, seed + 101U, kColumnFullHeight);
    for (int i = 0; i < extraColumns && island.columnCount < kIslandMaxColumns - 2; ++i) {
        const unsigned key = 20U + unsigned(i) * 5U;
        const float angle = kTau * (float(i) + 0.6f * draw(key)) / float(std::max(extraColumns, 1));
        const float offset = radius * lerpf(0.26f, 0.5f, draw(key + 1U));
        const float columnRadius = radius * lerpf(0.5f, 0.74f, draw(key + 2U));
        addColumn(island, std::cos(angle) * offset, std::sin(angle) * offset, columnRadius, 4 + int(draw(key + 3U) * 3.0f),
                  seed + 151U * unsigned(i + 2), kColumnFullHeight);
    }
    island.coreColumns = island.columnCount;
    scaleColumns(island, radius / std::max(meanCoreRadius(island), 1.0f));
    // Shelves are lower columns, snapped to a bedding plane so they read as beds.
    const int shelves = kind == kKindTerrace ? 3 : kind == kKindMesa ? 1 + int(draw(30) * 2.0f) : kind == kKindWooded ? 1 + int(draw(30) * 2.0f) : int(draw(30) * 1.5f);
    for (int i = 0; i < shelves && island.columnCount < kIslandMaxColumns; ++i) {
        const unsigned key = 40U + unsigned(i) * 5U;
        const float depth = kind == kKindTerrace ? 7.0f + 9.0f * float(i) + 4.0f * draw(key) : lerpf(8.0f, 40.0f, draw(key));
        const float shelfY = bedFloor(bedIndexAt(top - depth));
        const float angle = kTau * (float(i) + draw(key + 1U)) / float(std::max(shelves, 1));
        const float offset = radius * lerpf(0.42f, 0.62f, draw(key + 2U));
        addColumn(island, std::cos(angle) * offset, std::sin(angle) * offset, radius * lerpf(0.6f, 0.82f, draw(key + 3U)),
                  4 + int(draw(key + 4U) * 3.0f), seed + 977U * unsigned(i + 1), shelfY);
    }
    // Nothing may reach past the bound the neighbour spacing was computed for.
    const float reach = maxReach(island);
    if (reach > radius * kIslandMaxReach) scaleColumns(island, radius * kIslandMaxReach / reach);
    const int joints = kind == kKindSpire ? 7 + int(draw(50) * 5.0f) : 9 + int(draw(50) * 7.0f);
    for (int i = 0; i < joints && island.jointCount < kIslandMaxJoints; ++i) {
        const unsigned key = 60U + unsigned(i) * 4U;
        Joint& joint = island.joints[island.jointCount++];
        joint.angle = kTau * (float(i) + 0.8f * draw(key)) / float(joints);
        joint.width = lerpf(0.55f, 1.9f, draw(key + 1U));
        joint.depth = lerpf(1.4f, kind == kKindSpire ? 3.0f : 3.8f, draw(key + 2U));
        joint.bottomY = top - lerpf(18.0f, 150.0f, draw(key + 3U));
    }
    const int alcoves = kind == kKindMesa || kind == kKindSpire ? 2 + int(draw(70) * 3.0f) : 1 + int(draw(70) * 2.0f);
    for (int i = 0; i < alcoves && island.alcoveCount < kIslandMaxAlcoves; ++i) {
        const unsigned key = 90U + unsigned(i) * 5U;
        Alcove& alcove = island.alcoves[island.alcoveCount++];
        alcove.angle = kTau * draw(key);
        alcove.halfAngle = lerpf(0.16f, 0.52f, draw(key + 1U));
        alcove.topY = top - lerpf(6.0f, 46.0f, draw(key + 2U));
        alcove.bottomY = alcove.topY - lerpf(5.0f, 18.0f, draw(key + 3U));
        alcove.depth = lerpf(2.2f, 7.0f, draw(key + 4U)) * std::min(radius / 26.0f, 1.2f);
    }
    return island;
}

void addIslandSite(Island& island, float x, float z, float radius) {
    if (island.siteCount == kIslandMaxSites) return;
    const float rim = rimRadiusAt(island, std::atan2(z - island.z, x - island.x));
    island.sites[island.siteCount++] = {x, z, radius, crownHeight(island, x, z, rim)};
}

float islandTop(const Island& island, float x, float z) {
    return topAt(island, x, z, rimRadiusAt(island, std::atan2(z - island.z, x - island.x)));
}
float islandRimRadius(const Island& island, float angle) { return rimRadiusAt(island, angle); }
float islandRimHeight(const Island& island, float angle) {
    const float rim = rimRadiusAt(island, angle);
    return topAt(island, island.x + std::cos(angle) * rim, island.z + std::sin(angle) * rim, rim);
}
float islandWallRadius(const Island& island, float angle, float y) {
    const float depth = std::max(islandRimHeight(island, angle) - y, 0.0f);
    return wallRadiusBelowRim(island, angle, depth, y);
}
float bedLedgeWidth(const Island& island, float angle, int k, float& radius) {
    const float y = bedFloor(k + 1);
    const float rimY = islandRimHeight(island, angle);
    if (y >= rimY) return 0;
    radius = wallRadiusBelowRim(island, angle, rimY - y + 0.02f, y + 0.02f);
    const float below = wallRadiusBelowRim(island, angle, rimY - y + 0.32f, y - 0.32f);
    return below - radius;
}
bool columnShelf(const Island& island, int column, float angle, float& inner, float& outer) {
    if (column < island.coreColumns || column >= island.columnCount) return false;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float exit = columnExit(island.columns[column], c, s);
    float core = 0;
    for (int i = 0; i < island.coreColumns; ++i) core = std::max(core, columnExit(island.columns[i], c, s));
    if (exit <= core) return false;
    const float rimY = islandRimHeight(island, angle);
    const float scale = profileAt(island.profile, rimY - island.columns[column].topY);
    inner = core * scale;
    outer = exit * scale;
    return true;
}

float islandDistance(const Island& island, float3 p) {
    const float angle = std::atan2(p.z - island.z, p.x - island.x);
    const float rim = rimRadiusAt(island, angle);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float rimY = topAt(island, island.x + c * rim, island.z + s * rim, rim);
    const float r = std::hypot(p.x - island.x, p.z - island.z);
    const float depth = std::max(rimY - p.y, 0.0f);
    const float wall = r - (depth > 0 ? wallRadiusBelowRim(island, angle, depth, p.y) : rim);
    // Outside the rim circle the crown no longer bounds the rock: the wall
    // below the rim may bulge past it.
    const float crown = r <= rim ? p.y - topAt(island, p.x, p.z, rim) : p.y - rimY;
    return std::max(wall, crown);
}

float3 islandGradient(const Island& island, float3 p) {
    constexpr float e = 0.05f;
    const float3 g = make_float3(islandDistance(island, p + make_float3(e, 0, 0)) - islandDistance(island, p - make_float3(e, 0, 0)),
                                 islandDistance(island, p + make_float3(0, e, 0)) - islandDistance(island, p - make_float3(0, e, 0)),
                                 islandDistance(island, p + make_float3(0, 0, e)) - islandDistance(island, p - make_float3(0, 0, e)));
    return g * (0.5f / e);
}

Mesh islandMesh(const Island& island) {
    const int around = std::clamp(int(std::round(kTau * island.radius / 0.42f)) & ~3, 120, 600);
    const int rings = std::clamp(int(island.radius / 0.55f), 16, 58);
    std::vector<float> cosines(around), sines(around), rim(around), rimY(around);
    std::vector<float> exits(size_t(around) * kIslandMaxColumns);
    float lowestRim = 1e9f;
    for (int i = 0; i < around; ++i) {
        const float angle = kTau * float(i) / float(around);
        cosines[i] = std::cos(angle);
        sines[i] = std::sin(angle);
        columnExits(island, cosines[i], sines[i], &exits[size_t(i) * kIslandMaxColumns]);
        rim[i] = wallRadiusAt(island, angle, cosines[i], sines[i], &exits[size_t(i) * kIslandMaxColumns], 0.0f, kColumnFullHeight, Bed{});
        rimY[i] = topAt(island, island.x + cosines[i] * rim[i], island.z + sines[i] * rim[i], rim[i]);
        lowestRim = std::min(lowestRim, rimY[i]);
    }
    // Wall rows: fixed depths under the rim, then world heights aligned with
    // the bedding and shelves, so a sharp bedding plane falls on one row.
    std::vector<float> levels;
    const float start = lowestRim - kDepthRows[kDepthRowCount - 1] - 0.8f;
    constexpr float kVisibleBottom = -105.0f;
    for (int bed = bedIndexAt(start); bedFloor(bed + 1) > kVisibleBottom; --bed) {
        const float top = bedFloor(bed + 1);
        const float bottom = bedFloor(bed);
        if (top < start) {
            levels.push_back(top);
            levels.push_back(top - 0.25f);
        }
        const float spacing = (top > start - 40.0f ? 1.1f : 1.8f) * std::clamp(island.radius / 30.0f, 1.0f, 1.5f);
        for (float y = top - 0.25f - spacing; y > bottom + 0.45f; y -= spacing)
            if (y < start) levels.push_back(y);
        if (bottom < start) {
            levels.push_back(bottom + 0.42f);
            levels.push_back(bottom + 0.3f);
        }
    }
    for (int i = island.coreColumns; i < island.columnCount; ++i) {
        levels.push_back(island.columns[i].topY);
        levels.push_back(island.columns[i].topY - kShelfRamp);
    }
    for (int i = 0; i < island.alcoveCount; ++i) {
        levels.push_back(island.alcoves[i].topY);
        levels.push_back(island.alcoves[i].topY - 1.4f);
        levels.push_back(island.alcoves[i].bottomY + 2.5f);
        levels.push_back(island.alcoves[i].bottomY);
    }
    for (float y = kVisibleBottom - 3.0f; y > kIslandBase + 2.0f; y -= (y < -150.0f ? 14.0f : 6.0f)) levels.push_back(y);
    levels.push_back(kIslandBase);
    std::sort(levels.begin(), levels.end(), std::greater<float>());
    levels.erase(std::remove_if(levels.begin(), levels.end(), [&](float y) { return y > start; }), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end(), [](float a, float b) { return std::abs(a - b) < 0.05f; }), levels.end());
    const int wallRows = kDepthRowCount + int(levels.size());
    const int rows = rings + wallRows;
    Mesh mesh;
    mesh.vertices.reserve(size_t(around) * rows + 1);
    mesh.indices.reserve(size_t(around) * rows * 2);
    mesh.vertices.push_back({make_float3(island.x, topAt(island, island.x, island.z, rimRadiusAt(island, 0.0f)), island.z), {}, kMaterialStone});
    for (int row = 1; row <= rings; ++row)
        for (int i = 0; i < around; ++i) {
            const float radius = rim[i] * float(row) / float(rings);
            float3 p = make_float3(island.x + cosines[i] * radius, 0, island.z + sines[i] * radius);
            p.y = topAt(island, p.x, p.z, rim[i]);
            mesh.vertices.push_back({p, {}, kMaterialStone});
        }
    for (int row = 0; row < wallRows; ++row) {
        const bool byDepth = row < kDepthRowCount;
        const float level = byDepth ? 0.0f : levels[row - kDepthRowCount];
        const Bed levelBed = byDepth ? Bed{} : bedAt(level);
        for (int i = 0; i < around; ++i) {
            const float y = byDepth ? rimY[i] - kDepthRows[row] : level;
            const float depth = byDepth ? kDepthRows[row] : rimY[i] - level;
            const float angle = kTau * float(i) / float(around);
            const float radius = wallRadiusAt(island, angle, cosines[i], sines[i], &exits[size_t(i) * kIslandMaxColumns], depth, y,
                                              byDepth ? bedAt(y) : levelBed);
            mesh.vertices.push_back({make_float3(island.x + cosines[i] * radius, y, island.z + sines[i] * radius), {}, kMaterialStone});
        }
    }
    const auto index = [&](int row, int i) { return row == 0 ? 0U : unsigned(1 + (row - 1) * around + (i % around)); };
    for (int i = 0; i < around; ++i) mesh.indices.push_back(make_uint3(0, index(1, i + 1), index(1, i)));
    for (int row = 1; row < rows; ++row)
        for (int i = 0; i < around; ++i) {
            const unsigned a = index(row, i);
            const unsigned b = index(row, i + 1);
            const unsigned c = index(row + 1, i);
            const unsigned d = index(row + 1, i + 1);
            mesh.indices.push_back(make_uint3(a, b, d));
            mesh.indices.push_back(make_uint3(a, d, c));
        }
    std::vector<float3> normals(mesh.vertices.size(), make_float3(0, 0, 0));
    for (const uint3& t : mesh.indices) {
        const float3 n = cross(mesh.vertices[t.y].position - mesh.vertices[t.x].position,
                               mesh.vertices[t.z].position - mesh.vertices[t.x].position);
        normals[t.x] += n;
        normals[t.y] += n;
        normals[t.z] += n;
    }
    const float sign = normals[0].y < 0 ? -1.0f : 1.0f;
    for (size_t i = 0; i < normals.size(); ++i) {
        const float3 n = normals[i];
        const float l = std::sqrt(dot(n, n));
        mesh.vertices[i].normal = l > 1e-12f ? n * (sign / l) : make_float3(0, 1, 0);
    }
    return mesh;
}
}
