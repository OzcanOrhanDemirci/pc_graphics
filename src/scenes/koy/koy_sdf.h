// Scene 5 "Koy": a mesh from a signed distance field by surface nets (one vertex per cell the surface crosses, one quad
// per grid edge it crosses: watertight, welded by construction), evaluated in full only in a narrow band about the
// surface (the field first on a grid 8 cells coarse). The Amfora arch's mesher (2026-10-04), made general for Ada's island
// and rocks. The fine grain is the textures' (triplanar), not the mesh's.
#pragma once
#include "scenes/koy/koy_renderer.h"

#include <functional>
#include <string>

struct KoySdfBox {
    float3 lo, hi;       // world, axis aligned
    float cell;          // metres
    float slope = 2.2f;  // the field's steepest slope against a true distance (warps stretch it): the band's safety
    float reach = 1.1f;  // metres the relief can move the surface past the base field (also the band's safety)
};

// f(p) < 0 inside. Normals from the field's gradient.
KMeshCPU koyBuildSdfMesh(const std::function<float(float3)>& f, const KoySdfBox& box, const char* name, unsigned int material,
                         unsigned int id, std::string* log = nullptr);
