// Hash grid of caustic photons, shared by the grid build (nvcc, photons.cu) and the gather (NVRTC,
// programs.cu): both must agree on cells and hashes, so this is the single definition.
#pragma once
#include "shared/vec.h"

HD int3 photonCell(float3 p, float cellSize) {
    const float inv = 1.0f / cellSize;
    return make_int3(int(floorf(p.x * inv)), int(floorf(p.y * inv)), int(floorf(p.z * inv)));
}
HD unsigned int photonCellHash(int3 c, unsigned int gridSize) {
    const unsigned int h = (unsigned int)c.x * 73856093u ^ (unsigned int)c.y * 19349663u ^ (unsigned int)c.z * 83492791u;
    return h & (gridSize - 1u);
}
#define PHOTON_EMPTY_CELL 0xFFFFFFFFu
