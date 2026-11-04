// Scene 13 "Dağ": the ground. The real Earth around Kaçkar's summit from assets/dag (tools/dag_veri.py), made once and kept in
// cache/dag: every cell of the 1 arc second elevation model the terrace can see, as triangles of the far mesh (no level of
// detail: the eye never leaves the terrace); the clip stacks of slope, sky share and sun horizon, and of the ground's
// colour with the satellite's own sunlight taken out; the amplified ground around the terrace.
#pragma once
#include "core/config.h"
#include "shared/vec.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct DagClipCPU {
    int n = 0;
    float cell = 0.0f;
    std::vector<uint16_t> info;   // RGBA16: slope x, slope z, sky share, sun horizon
};
struct DagAlbedoCPU {
    int n = 0;
    float cell = 0.0f;
    std::vector<uint8_t> rgba;    // sRGB
};
struct DagFarTile {
    int lat = 0, lon = 0;
    std::vector<float3> tris;     // three vertices a triangle, no index
};
struct DagMeshCPU {
    std::vector<float3> pos, nrm;
    std::vector<float2> uv;
    std::vector<uint3> idx;
    float3 tint = make_float3(1, 1, 1);
    float rough = 0.5f;
    int material = 0;
    int id = 0;
};

// a copy of a scanned rock
struct DagStonePlace {
    int proto = 0;
    float m[12];               // object to world
};

struct DagBand {
    float from = 0.0f, to = 0.0f;
    uint64_t cells = 0;
};

// a known place on the plane: what the binoculars can name
struct DagPlace {
    std::string nameTr, nameEn;
    double mx = 0.0, mz = 0.0;
    float altitude = 0.0f;     // 0: not a peak
    float reach = 0.0f;        // metres around it that count as it
};

class DagWorld {
public:
    // settings
    double lat0 = 0.0, lon0 = 0.0;     // the summit (the plane's centre)
    float Re = 7323000.0f;
    float h0 = 3900.0f;                // the terrace's altitude
    float sunAzimuth = 299.0f;         // degrees from north, clockwise
    float sunElevation = 2.0f;         // degrees at the terrace
    float nearRadius = 1500.0f;

    std::vector<DagFarTile> far;
    std::vector<DagBand> bands;
    float farthestM = 0.0f;
    uint64_t farTriangles = 0;

    DagClipCPU clips[3];
    DagAlbedoCPU albedo[3];

    // the amplified ground around the terrace: two polar meshes (the terrace's surroundings, then out to some tens of km), their
    // normals as polar textures. A cell grows with the distance: about the same picture everywhere
    struct Polar {
        int az = 0, rings = 0;
        float r0 = 0.0f, rOut = 0.0f, logQ = 0.0f;   // r = r0 * exp(v * logQ), v 0..1 along the rings
        std::vector<float3> pos;
        std::vector<uint3> idx;
        std::vector<uint16_t> info;    // RGBA16: normal x, z, sky share, unused
    };
    Polar polar[2];
    float midRadius = 30000.0f;

    std::vector<DagMeshCPU> meshes;    // 0 the railing; from 1 the scanned rocks (with stoneTex: their textures' paths)
    std::vector<std::pair<std::string, std::string>> stoneTex;
    std::vector<DagStonePlace> stones;
    std::vector<DagPlace> places;

    // the terrace: the walkable polygon (x, z; the railing stands on it)
    std::vector<float2> terrace;
    float terraceRadius = 12.0f;

    std::vector<float> shadowBase;     // the max ground over 500 m cells, for the air's shadow map (square around the summit)
    int shadowN = 0;
    float shadowCell = 500.0f;

    std::string error;
    std::function<void(float, const std::string&)> progress;
    double buildMs = 0.0;

    bool prepare(const std::string& root, const Config& cfg);
    void release();

    // the ground under (x, z), metres from the terrace's centre: feet on it, gravity
    float groundY(float x, float z) const;
    bool inside(float x, float z, float margin) const;
    // the point nearest to (x, z) that the railing keeps you in
    void keepIn(float& x, float& z, float margin) const;
    std::string summary() const;
    const std::string& rootPath() const { return root_; }
    // the place at a point of the plane seen from this far, or null
    const DagPlace* placeAt(double mx, double mz, double range) const;
    // the plane's coordinates and altitude of a point of the world
    void toPlane(float3 p, double& mx, double& mz, double& alt) const;
    // the plane to world
    float3 worldPoint(double mx, double mz, double h) const;

private:
    std::string root_;
};
