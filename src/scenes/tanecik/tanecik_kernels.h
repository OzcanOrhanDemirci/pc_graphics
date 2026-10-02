// Scene 4 "Tanecik": small CUDA kernels the renderer runs on the physics' state every frame: the particles it draws
// (gathered from the solver's arrays), the skin mesh riding on the skin particles.
#pragma once
#include "scenes/tanecik/body_graph.h"

#include <cuda_runtime.h>

// out[k] = pos[idx[k]] (and the tint); idx null: the first n as they are. hideLiquid: a particle with tint >= 0.5 gets
// a NaN centre, which OptiX leaves out (the liquid's surface draws it)
void launchGatherParticles(const float3* pos, const float* tint, const uint32_t* idx, int n, float3* outPos, float* outTint, bool hideLiquid,
                           cudaStream_t s);
// liquid soaking into the floor: each listed particle's volume spread over the stain map (a small round footprint,
// litres per m2), the map res^2 cells from mn, size metres on a side
void launchStain(const float3* pos, const uint32_t* idx, int n, float litresEach, float footprint, float* map, int res, float2 mn, float size,
                 cudaStream_t s);
// each skin vertex: its particles' weighted centre now, plus its rest offset and normal turned by their bones' rotations
// (blended by the same weights); boneXf: B_COUNT transforms of 12 floats, particleBone: each particle's bone
// tint (may be null): a vertex any of whose particles turned liquid (>= 0.5) is torn: a NaN position (OptiX leaves its
// triangles out: a hole in the skin)
// tearDistance > 0: so too a vertex one of whose particles is that far from their weighted centre (pulled out of the wound)
void launchSkin(const SkinBind* bind, int vertices, const float3* particles, const uint8_t* particleBone, const float* boneXf, const float* tint,
                float tearDistance, float3* outPos, float3* outNrm, cudaStream_t s);
