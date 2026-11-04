// Scene 13 "Dağ": the air. Rayleigh, Mie and ozone over a sphere of radius Re (the air's usual bending built in, so a ray is
// straight), integrated once for the fixed eye into tables: the sky from the eye, the light gathered and let through on
// the way to the ground. The mountains' shadows in the air come from a shadow height map (dag_atmos.cpp): what is seen at
// sunset as dark blue valleys and rays across the sky.
#pragma once
#include "shared/vec.h"

#include <string>
#include <vector>

struct DagAir {
    float Re = 7323000.0f;
    float top = 100000.0f;
    float3 rayleigh = make_float3(5.802e-6f, 13.558e-6f, 33.1e-6f);
    float rayleighH = 8000.0f;
    float3 mieScat = make_float3(3.996e-6f, 3.996e-6f, 3.996e-6f);
    float3 mieAbs = make_float3(0.444e-6f, 0.444e-6f, 0.444e-6f);
    float mieH = 1200.0f;
    float mieG = 0.80f;
    float3 ozone = make_float3(0.650e-6f, 1.881e-6f, 0.085e-6f);
    float ozoneC = 25000.0f, ozoneW = 15000.0f;
    // a second thin layer of haze high up (the summer's dust): colours the horizon at sunset, keeps the far ridges
    float3 hazeScat = make_float3(1.0e-6f, 1.0e-6f, 1.0e-6f);
    float hazeH = 4000.0f;
    // the marine layer: thin, low, it softens the sea's horizon
    float3 lowScat = make_float3(0, 0, 0);
    float lowH = 500.0f;
    float3 groundAlbedo = make_float3(0.12f, 0.12f, 0.12f);
    float3 sunE = make_float3(1.7f, 1.85f, 1.9f);     // the sun's irradiance above the air (arbitrary scale, its colour real)
};

// the shadow height map: over each cell of a square around the eye, the altitude above which the sun is seen
struct DagShadowMap {
    int n = 0;
    float cell = 0.0f, half = 0.0f;
    std::vector<float> s;
};

struct DagTables {
    // float4 RGBA, row by row
    int trW = 0, trH = 0;
    std::vector<float4> transmittance;
    int msW = 0, msH = 0;
    std::vector<float4> multi;
    int skyW = 0, skyH = 0;
    std::vector<float4> sky;
    int frW = 0, frH = 0, frD = 0;
    std::vector<float4> froxS, froxT;
    float3 sh[4];
    float eyeDip = 0.0f;
    float froxR0 = 0.0f, froxLogK = 0.0f, froxElMax = 0.0f;
    double ms = 0.0;
};

// world up and x, z tangents are those of the eye's frame; sun: the direction towards it in that frame
void dagBuildTables(const DagAir& air, float eyeAlt, float3 sun, const DagShadowMap* shadow, float h0, DagTables& out, int skyW, int skyH, int frW, int frH,
                    int frD);
// the tables on disk (they take many seconds to make and change only with the air and the sun)
bool dagTablesRead(const std::string& path, DagTables& t);
void dagTablesWrite(const std::string& path, const DagTables& t);
// sky lookup for the host (the irradiance's check and the gallery)
float3 dagSkyDir(const DagTables& t, float3 d);
