// Scene 5 "Koy": the OptiX pipeline (NVRTC hot reload of device/koy.cu), the acceleration structures (triangles; the
// moving floats' instances refitted every frame), the frame's passes and their timings.
#pragma once
#include "scenes/koy/koy_params.h"

#include <cuda_runtime.h>
#include <optix.h>

#include <array>
#include <string>
#include <vector>

struct KoyTimes {        // milliseconds of the last frame's passes
    float world = 0, shafts = 0, base = 0, edge = 0, resolve = 0, final = 0;
    unsigned int edgePixels = 0;
    float total() const { return world + shafts + base + edge + resolve + final; }
};

struct KMeshCPU {
    std::string name;
    std::vector<float3> pos, nrm;
    std::vector<float2> uv;
    std::vector<uint3> idx;
    std::vector<unsigned short> triMat;   // optional: a material per triangle
    unsigned int material = 0, id = 1;
    bool scatter = false;   // spread over the whole floor: merged by tile at the end of the build (local bounds for the rays)
    bool bakeAO = true;
    float aoRange = 4.0f;   // metres the occlusion rays look (a wreck's hold wants more)
};
struct KInstanceCPU {
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    int mesh = 0;
};
class KoyRenderer {
public:
    bool init(unsigned int width, unsigned int height, const std::string& root);
    void shutdown();
    bool compileDevice(std::string& log);
    // the world's geometry and materials
    void uploadWorld(const std::vector<KMeshCPU>& meshes, const std::vector<KInstanceCPU>& instances, const std::vector<KMaterial>& materials,
                     const std::vector<KTextureSet>& texSets);
    void bakeAO();                                                // every mesh's per-vertex occlusion (once, after upload)
    // a sun depth map over the surface rectangle [min, max] (x, z), texel metres (once, after upload); false if full
    bool bakeSunMap(float2 mn, float2 mx, float texel);
    // the light under the pier: a box of cells, `dirs` directions each (after the sun maps: it reads them)
    bool bakeHall(float3 mn, float3 mx, float cell, int dirs, int bounces);
    float hallStats_[2] = {0.0f, 0.0f};   // the baked share's least and most
    void setSampling(int baseSpp, int edgeSpp, int filter, float radius, float edgeContrast);
    void updateMaterials(const std::vector<KMaterial>& materials);   // the same count as uploaded (texture choice)
    // new object-to-world transforms for some of the world's instances (their indices as uploaded); refits the IAS
    void setTransforms(const std::vector<int>& idx, const std::vector<std::array<float, 12>>& m);
    // distances to the static geometry from p in 26 directions (index = 9 (z+1) + 3 (y+1) + (x+1), the centre skipped)
    void probe(float3 p, float out[26]);
    void renderFrame();                                           // base -> edge -> resolve -> meter -> final
    // the converged reference of the same moment: the base pattern (16) shifted over an n x n grid inside its spacing,
    // edges off, the resolved frames averaged (n = 4: 256 samples a pixel), then the same final pass
    void renderReference(int n);
    void download(std::vector<uchar4>& out);
    void downloadHdr(std::vector<float4>& out);

    KoyParams& params() { return params_; }
    const KoyTimes& times() const { return times_; }
    float meteredLog2() const { return metered_; }   // mean log2 luminance (Y) of the last frame, before exposure
    cudaStream_t stream() const { return stream_; }
    std::string deviceName() const { return deviceName_; }
    float worldMs = 0.0f;                             // set by the owner: the time spent on the world (waves, caustics, floats)

private:
    void launch(CUdeviceptr raygen, unsigned int w, unsigned int h, unsigned int d, float* ms);
    void allocSamples();

    std::string root_, deviceName_;
    unsigned int width_ = 0, height_ = 0;
    OptixDeviceContext context_ = nullptr;
    OptixModule module_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    OptixShaderBindingTable sbt_{};
    CUdeviceptr rgBase_ = 0, rgEdge_ = 0, rgResolve_ = 0, rgFinal_ = 0, rgBakeAO_ = 0, rgBakeSun_ = 0, rgShafts_ = 0, rgEdgeFind_ = 0, rgProbe_ = 0, rgBakeHall_ = 0,
                missRecord_ = 0, hitRecords_ = 0;
    float* probeBuf_ = nullptr;
    cudaArray_t shaftArr_ = nullptr;
    CUdeviceptr dParams_ = 0;
    cudaStream_t stream_ = nullptr;
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr;
    KoyParams params_{};
    KoyTimes times_;
    float metered_ = 0.0f;
    int allocatedBase_ = 0;
    std::vector<void*> worldBuffers_;
    std::vector<KMeshCPU> meshesCPU_;
    std::vector<KInstanceCPU> instancesCPU_;
    // acceleration structures: the meshes' GASes, the IAS over all (updatable: the floats move)
    std::vector<OptixTraversableHandle> gas_;
    OptixInstance* dInstances_ = nullptr;
    std::vector<OptixInstance> instHost_;
    std::vector<KInstance> kinstHost_;
    std::vector<int> instMap_;          // the world's instance index -> its place in the arrays (-1: none)
    unsigned int instanceCount_ = 0;
    void* iasBuf_ = nullptr;
    void* iasTemp_ = nullptr;
    size_t iasTempSize_ = 0, iasSize_ = 0;
    void buildIAS(bool update);
    float* meterBuf_ = nullptr;
    float4* meanBuf_ = nullptr;
    std::vector<float4> meanHost_;
    float4* refAcc_ = nullptr;
    float4* refSamples_ = nullptr;
    std::vector<float> meterHost_;
};
