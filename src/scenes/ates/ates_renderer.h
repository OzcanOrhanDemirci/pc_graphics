// Scene 12 "Ateş": the OptiX side. One GAS per static prototype (the ground, the stones), one per log (rebuilt when
// its burning changes its shape), an IAS of them all rebuilt each frame (the logs move: they settle, roll, are thrown
// on). A frame: the surfaces (DLSS-RR's guides, or fixed samples), DLSS, the fire marched over them (the volume pass),
// the glare, the display. docs/ates/PLAN.md §3.5.
#pragma once
#include "scenes/ates/ates_shared.h"
#include "scenes/orman/orman_assets.h"
#include "scenes/orman/orman_dlss.h"

#include <cuda_runtime.h>
#include <optix.h>

#include <array>

#include <string>
#include <vector>

struct APlace {
    int proto = 0;
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};   // row-major 3x4, the prototype's space to the world
    float3 tint{1, 1, 1};
};

// a log's mesh in its own space (the axis along x, centred): its side (u around, v along: the patches' grid) and
// its two end faces
struct ALogMesh {
    OMeshCPU side, ends;
};

class AtesRenderer {
public:
    AtesParams params{};
    std::string deviceName;
    float renderMs = 0.0f, volumeMs = 0.0f, displayMs = 0.0f, dlssMs = 0.0f;
    size_t gasBytes = 0;
    unsigned int badSamples = 0;
    std::string dlssError;
    OrmanDlssFrame dlssFrame{};

    bool init(int width, int height, const std::string& root);
    bool compile(std::string& log);
    // the static world: prototypes (the stones' scans and the ground) and where their copies stand
    void uploadStatic(const OrmanAssets& assets, const std::vector<OProtoCPU>& extra, const std::vector<APlace>& places);
    // the logs: their meshes (rebuilt GAS for each index in `changed`, or all when it is empty and the count changed),
    // their materials (side, ends), their poses, and their views for shading
    void setLogs(const std::vector<ALogMesh>& meshes, const std::vector<int>& changed, int sideMaterial, int endMaterial);
    void placeLogs(const std::vector<std::array<float, 12>>& poses);
    void setLogViews(const std::vector<ALogView>& views);
    // the materials as uploaded (the assets' and any added after)
    void uploadMaterials(const std::vector<OMaterial>& mats);
    void setPattern(int spp, float sigma);
    bool enableDlss(bool on);
    bool dlssOn() const { return dlssOn_; }
    // the fire: the gas's texture, its grid and its light blocks (the cumulative sum is built here each frame)
    void setGas(cudaTextureObject_t tex, const unsigned int* bricks, float3 origin, int nx, int ny, int nz, float h, const float3* lightI, int lnx, int lny, int lnz,
                float lightCell);
    void render();
    // the fire's pass alone again over the last frame's surfaces (the tools' A/B of the march), its picture in hdr
    void redoVolume();
    void download(std::vector<unsigned char>& rgba) const;
    std::vector<float4> hdrHost() const;
    void release();
    cudaStream_t stream() const { return stream_; }

private:
    std::string root_;
    cudaStream_t stream_ = nullptr;
    OptixDeviceContext context_ = nullptr;
    OptixModule module_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    CUdeviceptr rgRender_ = 0, rgVolume_ = 0, rgFinal_ = 0, miss_ = 0, hits_ = 0;
    OptixShaderBindingTable sbt_{};
    AtesParams* dParams_ = nullptr;
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr, ev2_ = nullptr, ev3_ = nullptr, evD_ = nullptr;
    std::vector<void*> static_;          // the static world's buffers
    std::vector<OptixTraversableHandle> staticGas_;
    std::vector<OGeom> geomsHost_;       // the static prototypes' geometries, then the logs'
    std::vector<AInst> staticInst_;      // per static prototype: its geometries
    std::vector<APlace> places_;
    unsigned int staticGeoms_ = 0;
    struct LogGas {
        std::vector<void*> bufs;
        OptixTraversableHandle handle = 0;
        OGeom side{}, ends{};
    };
    std::vector<LogGas> logGas_;
    std::vector<std::array<float, 12>> logPoses_;
    int logSideMat_ = 0, logEndMat_ = 0;
    std::vector<void*> top_;             // the IAS's buffers and the per-frame tables
    OGeom* dGeoms_ = nullptr;
    AInst* dInst_ = nullptr;
    ALogView* dLogs_ = nullptr;
    size_t dGeomsCap_ = 0, dInstCap_ = 0, dLogsCap_ = 0;
    OMaterial* dMats_ = nullptr;
    float* lum_ = nullptr;               // the light blocks' luminance and its cumulative sum
    float* cdf_ = nullptr;
    void* scanTmp_ = nullptr;
    size_t scanBytes_ = 0;
    int lightN_ = 0;
    static const int kLevels = 6;
    float4* levels_[kLevels] = {};
    float4* accum_[kLevels] = {};
    int width_ = 0, height_ = 0;
    struct Plane {
        cudaArray_t arr = nullptr;
        cudaSurfaceObject_t surf = 0;
        cudaTextureObject_t tex = 0;
    };
    static const int kPlanes = 9;
    Plane planes_[kPlanes];
    OrmanDlss dlss_;
    bool dlssOn_ = false;
    bool iasDirty_ = true;
    float3* tables_ = nullptr;           // the soot's and the black body's emission tables (2 x ATES_TABLE_N)
    void freePlanes();
    void launch(CUdeviceptr rg, unsigned int w, unsigned int h);
    void buildIas();
    OptixTraversableHandle buildGas(const std::vector<const OMeshCPU*>& meshes, std::vector<void*>& keep, std::vector<OGeom>& geoms, bool update);
};

// the cumulative sum of the light blocks' luminance (ates_kernels.cu)
void atesLightCdf(const float3* light, int n, float* lum, float* cdf, void*& tmp, size_t& tmpBytes, cudaStream_t st);
