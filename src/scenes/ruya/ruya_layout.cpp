#include "scenes/ruya/ruya_island.h"

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
    const CellPlan plan = planCell(cell);
    pieces.insert(pieces.end(), plan.pieces.begin(), plan.pieces.end());
}

}
