// Scene 8's live picture: OpenGL 4.6 on the application's context (main thread). The base mesh is tessellated and
// displaced every frame (nothing kept), every sample of a multisampled target is shaded on its own (supersampling),
// the samples are resolved to pixels, tone mapped, dithered and fitted to the window.
#pragma once
#include "scenes/denizalti/dz_bake_shared.h"
#include "scenes/denizalti/dz_geom.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dz {

struct Camera {
    float3 pos{0, 1.6f, 0};
    float yaw = 0.0f, pitch = 0.0f;   // yaw 0 looks along +z, positive turns towards +x; pitch up positive
    float fovY = 1.0472f;             // radians (60 degrees)
    float3 forward() const;
};

// the panel's choices
struct Settings {
    int width = 2560, height = 1440;  // render resolution
    int ssaa = 4;                     // 1, 2, 4, 8, 16
    int tess = 2, gfx = 2, light = 2, fx = 2;   // 0 low, 1 medium, 2 high
    float exposure = 0.0f;            // EV
    int debug = 0;                    // 0 off, 1 normals, 2 surface coordinates
    bool normalDetail = true;
    bool displacement = true;
    bool tessOff = false;             // measurement: every factor 1 (the base mesh as it is)
};

struct GpuTimes {   // ms, from the GPU's timestamps (the frame before last: no stall)
    double scene = 0, resolve = 0, post = 0, final = 0, total = 0;
};

class Renderer {
public:
    // compile the programs, make the fixed objects. root: the project (shaders under src/scenes/denizalti/glsl)
    bool init(const std::string& root, std::string& err);
    bool reloadShaders(std::string& err);
    void upload(const Mesh& m);
    void uploadLight(const struct LightMap& lm);
    void setLamps(const ::DzLight* l, int n);   // the key lamps whose highlights are drawn (shadowed by the bake)
    // one frame: the picture at outW x outH (the window), drawn at the settings' resolution and supersampling
    void draw(const Camera& cam, const Settings& s, int outW, int outH, double time);
    unsigned output() const { return outTex_; }
    int outW() const { return outW_; }
    int outH() const { return outH_; }
    // the output's pixels, top row first (RGBA8)
    void readOutput(std::vector<uint8_t>& rgba);
    // the resolved HDR picture at render resolution (RGB float, top row first): measurements
    void readHdr(std::vector<float>& rgb, int& w, int& h);
    const GpuTimes& times() const { return times_; }
    void release();

    // what the card offers (logged once)
    int maxSamples = 0, maxColorSamples = 0, maxDepthSamples = 0, maxTessLevel = 0;
    std::string glVersion, glRenderer;
    // the supersampling actually used: m hardware samples per texel, sx x sy texels per pixel
    int msaa = 1, sx = 1, sy = 1;
    size_t baseTris = 0;
    float finestTriangle = 0.0f;   // m: the base mesh's longest edge over the largest factor

private:
    bool buildPrograms(std::string& err);
    void makeNoise();
    void targets(int w, int h, int ssaa, int ow, int oh);
    void freeTargets();

    std::string root_;
    unsigned progScene_ = 0, progResolve_ = 0, progPost_ = 0, progFinal_ = 0;
    unsigned vao_ = 0, vbo_ = 0, ibo_ = 0, ubo_ = 0, emptyVao_ = 0;
    size_t indexCount_ = 0;
    // clusters: the triangles sorted by a 0.5 m grid cell, each cell a contiguous range with its bounds; culled
    // against the view every frame and drawn with one multi-draw
    struct Cluster {
        float lo[3], hi[3];
        uint32_t first, count;
    };
    std::vector<Cluster> clusters_;
    std::vector<int> drawCounts_;
    std::vector<const void*> drawOffsets_;
public:
    size_t drawnClusters = 0, drawnTris = 0;
private:
    // targets
    unsigned msColor_ = 0, msDepth_ = 0, msFbo_ = 0;
    unsigned lightTex_[5] = {};
    unsigned noiseTex_ = 0;
    float lampPos_[4][4] = {}, lampL_[4][4] = {};
    unsigned hdrTex_ = 0, ldrTex_ = 0, ldrFbo_ = 0, outTex_ = 0, outFbo_ = 0;
    int rw_ = 0, rh_ = 0, ssaa_ = 0, outW_ = 0, outH_ = 0;
    // timing: a ring of timestamp queries (5 per frame)
    unsigned queries_[3][5] = {};
    int qFrame_ = 0;
    bool qValid_[3] = {};
    GpuTimes times_;
};

}   // namespace dz
