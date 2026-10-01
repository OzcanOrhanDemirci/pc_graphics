// Scene 2 "Fener": the world built from code. Two islands across a strait, a lighthouse on a wooded hill, a
// keeper's cabin, spruce forests (a handful of procedural spruces instanced thousands of times), the fjord's
// walls kilometres away. Lengths in metres, y up; the lighthouse stands at the origin, the camera looks north (+z).
#pragma once
#include "scene.h"   // Config
#include "shared/fener_params.h"

#include <string>
#include <vector>

enum FenerMat : int {
    FM_GRASS = 0, FM_ROCK, FM_PATH, FM_BARK, FM_NEEDLE, FM_TOWER_LIGHT, FM_TOWER_DARK, FM_METAL, FM_LAMP_GLASS,
    FM_WOOD, FM_ROOF, FM_WINDOW, FM_FAR, FM_COUNT
};

struct FMeshCPU {
    std::string name;
    std::vector<float3> pos, nrm;
    std::vector<uint3> idx;
    std::vector<unsigned char> mat;

    void tri(float3 a, float3 b, float3 c, int m);                      // flat-shaded triangle
    void quad(float3 a, float3 b, float3 c, float3 d, int m);
    void box(float3 c, float3 half, float3 ax, float3 ay, float3 az, int m);   // oriented box, axes unit
};

struct FInstanceCPU {
    int mesh;
    float m[12];          // object to world, rows
    unsigned int mask;    // visibility: 1 blocks light, 2 camera only
};

struct FenerView {
    std::string name;
    float3 pos, target;
    float vfovDeg;
};

struct FenerScene {
    std::vector<FMeshCPU> meshes;
    std::vector<FInstanceCPU> instances;
    std::vector<FMaterial> materials;
    std::vector<FenerView> views;          // 0 = A "Pencere", 1 = B "Yucelik"
    float3 lampPos;
    float3 winCorner, winU, winV, winN;
    std::vector<float3> journey;           // A -> B through the forest, 1 m steps, y = ground
    float3 cabin;                          // centre of the cabin's floor (8 x 5.2 m, walls along x and z)
    size_t triangles() const;
};

FenerScene buildFenerScene(const Config& cfg);
float fenerGround(float x, float z);    // the camera island's ground height (the journey walks on it)
float fenerTerrain(float x, float z);   // both islands' ground (the player walks on it); below 0 is the sea
// A top-down map of the world (same height functions and instances as the renderer): PNG.
bool fenerMap(const FenerScene& s, const Config& cfg, const std::string& path);
void fenerCamera(const FenerView& v, unsigned int width, unsigned int height, FCamera& cam);
