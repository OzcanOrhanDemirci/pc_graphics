// Scene description built on the host: architecture as triangle meshes, round things as spheres,
// materials, light portals, camera presets. Everything is generated from code and scenes/altinsaat.cfg.
#pragma once
#include "core/config.h"
#include "scenes/altinsaat/altinsaat_params.h"

#include <map>
#include <string>
#include <vector>


struct CameraPreset {
    std::string name;
    float3 pos, target;
    float focalMm;      // full-frame equivalent, 36 mm sensor width
    float fNumber;
    float focusDist;    // 0: distance to target
};

struct Emitter {          // a caustic-casting object the photons are aimed at
    float3 boundsMin, boundsMax;
};

struct Scene {
    std::vector<float3> vertices, normals;
    std::vector<uint3> indices;
    std::vector<int> triMaterial;
    std::vector<float4> spheres;
    std::vector<int> sphereMaterial;
    std::vector<Material> materials;
    std::vector<Portal> portals;
    std::vector<float> portalDepth;   // wall thickness behind each portal: its outer face is portal - normal * depth
    std::vector<CameraPreset> cameras;
    std::vector<Emitter> emitters;
    float3 hazeMin, hazeMax;
    float3 sunDir;
    // live mode animates these sphere ranges around their rest positions
    int dustBegin = 0, dustEnd = 0, bubbleBegin = 0, bubbleEnd = 0;
    std::vector<float4> restSpheres;

    int addMaterial(const Material& m) { materials.push_back(m); return int(materials.size()) - 1; }
    void addTriangle(float3 a, float3 b, float3 c, int mat);
    void addQuad(float3 a, float3 b, float3 c, float3 d, int mat);   // counter-clockwise seen from the front
    void addBox(float3 lo, float3 hi, int mat);
    // A solid slab spanning origin + u*[0,w] + v*[0,h], extruded by -n*thickness, with rectangular
    // openings given in (u, v) metres as {u0, v0, u1, v1}. The front face looks along n.
    void addSlab(float3 origin, float3 u, float3 v, float3 n, float w, float h, float thickness,
                 const std::vector<float4>& openings, int mat, int revealMat);
};

Scene buildScene(const Config& cfg);
// Positions of the drifting things (dust, bubbles) at time t, into s.spheres.
void animateScene(Scene& s, float t);
void cameraFromPreset(const CameraPreset& p, unsigned int width, unsigned int height, Camera& cam);

// Spectral helpers on the host
float3 fitSigmoid(float3 linearSRGB);          // reflectance spectrum coefficients for an sRGB albedo
bool spectralSelfTest(std::string& report);    // D65 white point, sRGB white, Sellmeier, thin film order
// Bradford adaptation that renders a blackbody of `kelvin` as neutral (D65) white; kelvin <= 0: identity.
void whiteBalanceMatrix(float kelvin, float out[9]);
