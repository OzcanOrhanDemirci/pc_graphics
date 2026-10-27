// Scene 11 "Orman": the OptiX side. One GAS per prototype (its primitives as build inputs, compacted), an IAS of each
// group a cell of the forest's torus is made of (the tile, the patch, a band of the stream, a cell in the sun's way)
// and the top IAS of the cells around the eye, each once (rebuilt as the eye goes from cell to cell); the probe grid
// and the ground's occlusion baked at load; a frame is the render launch (visibility + shading, the sun's shadow
// rays), the glare pyramid and the display launch.
#pragma once
#include "scenes/orman/orman_assets.h"
#include "scenes/orman/orman_dlss.h"
#include "scenes/orman/orman_shared.h"

#include <cuda_runtime.h>
#include <optix.h>

#include <string>
#include <vector>

struct OPlace {
    int proto = 0;
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};   // row-major 3x4, the prototype's space to the world
    float scale = 1.0f;
    float3 tint{1, 1, 1};
    unsigned int mask = OMASK_SOLID;
};

// the forest around the stream's patch, a torus of n x n cells: the groups its cells are made of (each a list of
// places, an IAS: the tile whole or without the stream's band, its middle at the origin; the patch, a band, a cell in
// the sun's way, where they stand) and which group each cell holds where
struct OTile {
    std::vector<OPlace> places;
    std::vector<std::vector<unsigned>> variants;
    struct Cell {
        float3 offset;
        int variant;
        int i = 0, j = 0;     // the torus's cell it is in (the middle one 0, 0)
    };
    std::vector<Cell> cells;
    int n = 0;                // cells to the torus's side (0: no torus, the world's own copies alone)
    float side = 0.0f;        // a cell's (m)
};

struct OBakeStats {
    double aoMs = 0.0, gridMs = 0.0, visMs = 0.0;
    size_t probes = 0, valid = 0;   // the probes baked (the patch's and the tile's), how many of them in the open
};

class OrmanRenderer {
public:
    OrmanParams params{};
    std::string deviceName;
    size_t gasBytes = 0, iasBytes = 0, geomBytes = 0, gridBytes = 0, tileGridBytes = 0, visBytes = 0;
    unsigned int instanceCount = 0;              // every copy, the tile's in each of its cells
    unsigned int topInstances = 0;               // the top level's: the world's own and one a cell's group
    unsigned long long instancedTriangles = 0;   // every copy's triangles: the density the eye sees
    float renderMs = 0.0f, displayMs = 0.0f, dlssMs = 0.0f;
    unsigned int badSamples = 0;                 // the last frame's samples that were not finite

    bool init(int width, int height, const std::string& root);
    bool compile(std::string& log);
    // the prototypes (the ground among them), the copies placed in the world and the tile with its cells
    void upload(const OrmanAssets& assets, const std::vector<OProtoCPU>& extra, const std::vector<OPlace>& places, const OTile& tile);
    // the torus's cells around the eye's cell (i, j): each once, its copy nearest the eye (the top IAS, rebuilt when
    // the eye goes into another cell); no look reaches past them
    void placeWindow(int i, int j);
    int windowReach = 0;                         // cells each way around the eye's (at least half the torus)
    int windowI() const { return windowI_; }
    int windowJ() const { return windowJ_; }
    // a ground's occlusion per vertex (every geometry of prototype `proto` that has one), its vertices standing at `at`
    void bakeGroundAO(int proto, float3 at, float radius, int rays, OBakeStats& st);
    // the probe grid over [lo, hi]: the patch's, or the tile's (whole probes to its side, wrapping; baked first, in its
    // copy at `at`)
    void bakeGrid(bool tile, float3 lo, float3 hi, float h, float3 at, int rays, int passes, OBakeStats& st);
    // the sun's visibility in the air (the mist's shafts), a 3D texture of h-metre voxels over [lo, hi] (the tile's
    // wraps, baked in its copy at `at`)
    void bakeSunVis(bool tile, float3 lo, float3 hi, float h, float3 at, OBakeStats& st);
    void setPattern(int spp, float sigma);
    // DLSS Ray Reconstruction instead of the fixed samples (false and dlssError when it cannot be set up); the app fills
    // dlssFrame's jitter, matrices and reset before each render
    bool enableDlss(bool on);
    bool dlssOn() const { return dlssOn_; }
    std::string dlssError;
    OrmanDlssFrame dlssFrame{};
    void render();
    void download(std::vector<unsigned char>& rgba) const;
    std::vector<float4> hdrHost() const;
    void release();

private:
    std::string root_;
    CUstream stream_ = nullptr;
    OptixDeviceContext context_ = nullptr;
    OptixModule module_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    CUdeviceptr rgRender_ = 0, rgFinal_ = 0, rgBake_ = 0, rgBakeAO_ = 0, rgBakeVis_ = 0, miss_ = 0, hits_ = 0;
    cudaArray_t visArr_ = nullptr, tileVisArr_ = nullptr;
    OptixShaderBindingTable sbt_{};
    OrmanParams* dParams_ = nullptr;
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr, ev2_ = nullptr;
    std::vector<void*> world_;           // every world buffer, freed together
    std::vector<void*> gridBufs_, tileGridBufs_;
    std::vector<OptixTraversableHandle> gas_;
    std::vector<OGeom> geomsHost_;
    std::vector<OProto> protosHost_;
    std::vector<unsigned int> geomVerts_;   // per geometry: its vertex count
    static const int kLevels = 6;
    float4* levels_[kLevels] = {};
    float4* accum_[kLevels] = {};
    int width_ = 0, height_ = 0;
    // DLSS's planes: color, diffuse albedo, specular albedo, normal, roughness, depth, motion, the reflection's length,
    // and its output
    struct Plane {
        cudaArray_t arr = nullptr;
        cudaSurfaceObject_t surf = 0;
        cudaTextureObject_t tex = 0;
    };
    static const int kPlanes = 9;
    Plane planes_[kPlanes];
    OrmanDlss dlss_;
    bool dlssOn_ = false;
    cudaEvent_t evD_ = nullptr;
    void freePlanes();
    void launch(CUdeviceptr rg, unsigned int w, unsigned int h, unsigned int d);
    OptixTraversableHandle buildIas(const std::vector<OptixInstance>& inst, std::vector<void*>& keep, size_t& bytes);
    // the top level: the world's own copies and each cell's group, moved round the torus into the window
    struct CellIas {
        OptixTraversableHandle handle;
        float3 offset;
        int i, j;
    };
    std::vector<OptixInstance> fixed_;
    std::vector<CellIas> cells_;
    int torusN_ = 0;
    float torusSide_ = 0.0f;
    int windowI_ = 0, windowJ_ = 0;
    std::vector<void*> top_;             // the top IAS's buffers (rebuilt with the window)
    size_t topBytes_ = 0;
};
