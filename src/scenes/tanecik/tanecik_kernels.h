// Scene 4 "Tanecik": small CUDA kernels the renderer runs on the physics' state every frame: the particles it draws
// (gathered from the solver's arrays), the skin mesh riding on the skin particles.
#pragma once
#include "scenes/tanecik/body_graph.h"

#include <cuda_runtime.h>

// out[k] = pos[idx[k]] (and the tint); idx null: the first n as they are. hideLiquid: a particle with tint >= 0.5 gets
// a NaN centre, which OptiX leaves out (the liquid's surface draws it)
void launchGatherParticles(const float3* pos, const float* tint, const uint32_t* idx, int n, float3* outPos, float* outTint, bool hideLiquid,
                           cudaStream_t s);
// each skin vertex: its particles' weighted centre now, plus its rest offset and normal turned by their bones' rotations
// (blended by the same weights); boneXf: B_COUNT transforms of 12 floats, particleBone: each particle's bone
void launchSkin(const SkinBind* bind, int vertices, const float3* particles, const uint8_t* particleBone, const float* boneXf, float3* outPos,
                float3* outNrm, cudaStream_t s);
