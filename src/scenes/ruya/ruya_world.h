// Procedural prototypes are material recipes, not saved world cells. The active
// set has no LRU: a cell leaves it immediately when its bounds leave the view.
#pragma once
#include "scenes/ruya/ruya_shared.h"
#include <vector>

namespace ruya {
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint3> indices;
};
std::vector<Mesh> buildPrototypes(int detail = 2);
// Appends the same composition for a coordinate regardless of camera or lighting.
void assembleCell(const Cell& cell, std::vector<Piece>& pieces);
float3 piecePoint(const Piece& piece, float3 local);
float3 localPoint(const Piece& piece, float3 world);
std::vector<Cell> visibleCells(const Camera& camera, float aspect, float farDistance = kFarDistance,
                               bool fullCircle = false);
void cameraBasis(const Camera& camera, float aspect, float3& U, float3& V, float3& W);
void moveCamera(Camera& camera, float dx, float dy, float forward, float right, float up, float dt);
unsigned long long fingerprint(const Mesh& mesh);
int testWorld();
}
