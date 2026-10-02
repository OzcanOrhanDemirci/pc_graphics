// Scene 4 "Laboratuvar": the OptiX side. A list of objects (instances), each one material: static meshes (the set,
// glass ware; an object's transform moves it: the beaker tilts), spheres (droplets, sand grains; rebuilt when they
// move) and liquid surfaces (FluidSurface: a mesh rebuilt from particles every frame, and its droplets as spheres).
// Device program device/laboratuvar.cu (NVRTC, F5 recompiles).
#pragma once
#include "scenes/laboratuvar/laboratuvar_params.h"
#include "scenes/laboratuvar/surface.h"

#include <cuda_runtime.h>
#include <optix.h>

#include <memory>
#include <string>
#include <vector>

struct HostMesh {
    std::vector<float3> pos, nrm;
    std::vector<uint3> idx;
    void append(const HostMesh& o) {
        const unsigned base = unsigned(pos.size());
        pos.insert(pos.end(), o.pos.begin(), o.pos.end());
        nrm.insert(nrm.end(), o.nrm.begin(), o.nrm.end());
        for (uint3 t : o.idx) idx.push_back(make_uint3(t.x + base, t.y + base, t.z + base));
    }
};

class LabRenderer {
public:
    bool init(unsigned int width, unsigned int height, const std::string& root);
    void shutdown();
    bool compileDevice(std::string& log);
    // materials: added, changed in place, then uploaded
    int addMaterial(const LMaterial& m);
    LMaterial& material(int i) { return mats_[size_t(i)]; }
    void uploadMaterials();
    void clearMaterials() { mats_.clear(); }
    // objects
    void clearObjects();
    int addMesh(const HostMesh& m, int material);             // a fixed mesh (it moves only by its transform)
    // a mesh whose vertices change (a membrane): its buffers on the device, rebuilt by updateMesh
    int addDynamicMesh(int maxVertices, int maxTriangles, int material);
    void updateMesh(int obj, const float3* dPos, const float3* dNrm, int nVerts, const uint3* dIdx, int nTris);   // device arrays, copied
    float3* meshPositions(int obj);
    float3* meshNormals(int obj);
    uint3* meshIndices(int obj);
    void rebuildMesh(int obj, int nVerts, int nTris);         // after writing into its buffers
    int addSpheres(int capacity, int material, bool tinted);  // the caller writes spheresBuffer(obj) (centre, radius)
    float4* spheresBuffer(int obj);
    float* tintBuffer(int obj);
    void setSphereCount(int obj, int n);                      // and builds them
    // a liquid's surface (two objects: the mesh and its droplets); returns the surface's index
    int addSurface(int material, int maxParticles, int maxVertices, int maxTriangles, int maxBlocks, const FluidSurfaceSettings& s);
    void buildSurface(int surf, const float3* dPos, const float* dWeight, int n, float radius);
    FluidSurface& surface(int surf) { return *surfaces_[size_t(surf)].fs; }
    void setSurfaceVisible(int surf, bool on);
    void setTransform(int obj, const float xf[12]);           // row-major 3x4, object to world
    void setVisible(int obj, bool on);
    // drawing
    void render(unsigned int spp, bool accumulate);
    LPick pick(float x, float y);
    void download(std::vector<uchar4>& out);
    void downloadTo(uchar4* hostPinned);
    LabParams& params() { return p_; }
    cudaStream_t stream() const { return stream_; }
    unsigned int width() const { return w_; }
    unsigned int height() const { return h_; }
    float lastMs = 0.0f;       // the last render
    float surfaceMs = 0.0f;    // the surfaces built since the last render

private:
    struct Gas {
        OptixTraversableHandle handle = 0;
        void *temp = nullptr, *out = nullptr;
        size_t tempSize = 0, outSize = 0;
    };
    struct Obj {
        int kind = 0, material = 0;
        Gas gas;
        float xf[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        bool visible = true;
        float3* pos = nullptr;
        float3* nrm = nullptr;
        uint3* idx = nullptr;
        int nVerts = 0, nTris = 0, maxVerts = 0, maxTris = 0;
        float4* spheres = nullptr;
        float* tint = nullptr;
        int count = 0, capacity = 0;
        bool external = false;   // buffers owned by a FluidSurface
    };
    struct Surf {
        std::unique_ptr<FluidSurface> fs;
        int meshObj = -1, dropObj = -1;
        bool visible = true;
    };
    std::string root_;
    unsigned int w_ = 0, h_ = 0;
    OptixDeviceContext ctx_ = nullptr;
    OptixModule module_ = nullptr, sphereModule_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
    std::vector<OptixProgramGroup> groups_;
    OptixShaderBindingTable sbt_{};
    CUdeviceptr rg_ = 0, rgPick_ = 0, miss_ = 0, hit_ = 0, dParams_ = 0;
    LPick* dPick_ = nullptr;
    cudaStream_t stream_ = nullptr;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr;
    LabParams p_{};
    std::vector<LMaterial> mats_;
    LMaterial* dMats_ = nullptr;
    int dMatsCap_ = 0;
    std::vector<Obj> objs_;
    std::vector<Surf> surfaces_;
    LObject* dObjs_ = nullptr;
    int dObjsCap_ = 0;
    bool objsDirty_ = true, iasDirty_ = true;
    OptixInstance* dInstances_ = nullptr;
    int dInstancesCap_ = 0;
    Gas ias_;
    void buildTriangles(Obj& o, bool fast);
    void buildSpheres(Obj& o, CUdeviceptr centres, CUdeviceptr radii, unsigned stride, int n);
    void ensure(Gas& g, const OptixAccelBufferSizes& sz);
    void freeGas(Gas& g);
    void uploadObjects();
    void buildIAS();
};
