#include "scenes/ruya/ruya_world.h"

#include <cmath>

namespace ruya {
namespace { constexpr float kTau = 2.0f * M_PIf; }

float3 piecePoint(const Piece& piece, float3 local) {
    const float3 point = local * piece.scale;
    const float cosine = std::cos(piece.yaw);
    const float sine = std::sin(piece.yaw);
    return piece.offset + make_float3(cosine * point.x + sine * point.z, point.y, -sine * point.x + cosine * point.z);
}

float3 localPoint(const Piece& piece, float3 world) {
    const float3 point = world - piece.offset;
    const float cosine = std::cos(piece.yaw);
    const float sine = std::sin(piece.yaw);
    return make_float3((cosine * point.x - sine * point.z) / piece.scale.x, point.y / piece.scale.y,
                       (sine * point.x + cosine * point.z) / piece.scale.z);
}

void assembleCell(const Cell& cell, std::vector<Piece>& pieces) {
    if (cell.scale == 0) return;
    const auto draw = [&](unsigned channel) { return randomUnit(cell.seed + 877U * channel); };
    const float sine = std::sin(cell.yaw);
    const float cosine = std::cos(cell.yaw);
    const auto add = [&](unsigned prototype, float x, float y, float z, float3 size, float yaw) {
        const auto point = cell.offset + make_float3(cosine * x + sine * z, y, -sine * x + cosine * z) * cell.scale;
        pieces.push_back({point, size * cell.scale, cell.yaw + yaw, prototype,
                          hash(cell.seed + unsigned(prototype) * 167U), cell.region});
    };
    const auto size = [](float value) { return make_float3(value, value, value); };
    const unsigned region = cell.region;
    if (region == 2 && draw(1) < 0.68f) {
        // A broken field of upright stone splinters, with an open passage through its centre.
        const int needles = 2 + int(draw(2) * 6);
        for (int needle = 0; needle < needles; ++needle) {
            const unsigned key = 30U + unsigned(needle) * 5U;
            const float angle = kTau * draw(key);
            const float radius = 5.5f + 21.0f * draw(key + 1);
            add(18, radius * std::cos(angle), 10.0f + 17.0f * draw(key + 2), radius * std::sin(angle),
                make_float3(0.48f + draw(key + 3), 0.95f + 0.65f * draw(key + 4), 0.62f + 0.45f * draw(key)), angle);
        }
        if (draw(3) < 0.32f) {
            add(21, 0, -1, 0, size(1.2f), 0);
            add(19, 0, 0, 0, size(0.72f), 0.75f);
        }
        return;
    }
    const unsigned terrain = region == 0 ? (draw(4) < 0.58f ? 24U : unsigned(draw(5) * 3))
                           : region == 1 ? 3U + unsigned(draw(4) * 3)
                           : region == 2 ? 7U : (draw(4) < 0.56f ? 26U : 6U);
    const float width = lerpf(0.84f, 1.30f, draw(5));
    const float depth = terrain == 26 ? lerpf(1.02f, 1.37f, draw(6)) : lerpf(0.68f, 1.21f, draw(6));
    const float thickness = region == 1 ? lerpf(0.31f, 0.63f, draw(7)) : lerpf(0.60f, 1.02f, draw(7));
    add(terrain, 0, 0, 0, make_float3(width, thickness, depth), 0);
    if (region == 0) {
        const unsigned design = unsigned(draw(8) * 5);
        if (design < 2) add(8, 0, 0, 0, size(0.58f + 0.22f * draw(9)), 1.5f * draw(10));
        else if (design < 4) add(19, 0, 0, 0, make_float3(0.74f, 0.86f + 0.24f * draw(9), 0.73f), 1.6f * draw(10));
        else add(16, 0, 0, 0, size(0.66f), 0);
        add(draw(14) < 0.5f ? 9 : 20, -2, 0, -5, size(0.65f + 0.24f * draw(15)), 2.7f * draw(16));
        if (draw(17) < 0.4f) add(10, 3, 0, 2, size(0.60f), 0.42f);
        if (draw(18) < 0.46f)
            add(25, 0, -3 - 7 * draw(19), -13, make_float3(0.85f + 0.55f * draw(17), 0.87f, 1.0f), 1.5f);
    } else if (region == 1) {
        add(11, 0, 0, 0, make_float3(1.13f + 0.35f * draw(8), 1.0f, 0.88f), 0.8f * draw(9));
        if (draw(10) < 0.65f) add(13, -3, 0, -3, size(0.55f + 0.27f * draw(13)), 2.5f * draw(14));
        add(23, 0, -0.22f, 0, size(0.92f), 1.8f + 2.4f * draw(10));
    } else if (region == 2) {
        add(10, 1.0f, 0, 1.0f, size(0.77f + 0.19f * draw(12)), 1.8f * draw(13));
    } else {
        add(draw(10) < 0.4f ? 12 : 17, 0, 0, 0, size(0.58f + 0.14f * draw(11)), draw(12) * 2.8f);
    }
    const int trees = region == 3 ? 9 + int(draw(15) * 6) : 2 + int(draw(15) * 5);
    for (int tree = 0; tree < trees; ++tree) {
        const unsigned key = 72U + unsigned(tree) * 5U;
        const float angle = draw(key) * kTau;
        const float r = (terrain == 26 ? 3.4f : region == 3 ? 4.8f : 7.0f) + 4.0f * draw(key + 1);
        const float treeDepth = terrain == 24 || terrain == 26 ? depth * 0.54f : depth;
        add(draw(key + 2) < 0.44f ? 15 : 14, std::cos(angle) * r * width, -0.15f, std::sin(angle) * r * treeDepth,
            size(0.57f + 0.92f * draw(key + 3)), draw(key + 4) * kTau);
    }
    if (draw(20) < 0.68f) {
        const float angle = draw(21) * kTau;
        const float reach = 22.5f + 5.0f * draw(22);
        add(21, std::cos(angle) * reach, -3 - 8 * draw(23), std::sin(angle) * reach,
            make_float3(0.63f + 0.29f * draw(24), 0.67f, 0.74f), angle);
    }
    for (int rock = 0; rock < 3; ++rock) {
        const unsigned key = 155U + unsigned(rock) * 4U;
        const float angle = draw(key) * kTau;
        add(22, cosf(angle) * 15 * width, 0.18f, sinf(angle) * 15 * depth, size(0.27f + draw(key + 1) * 0.5f), angle);
    }
}

}
