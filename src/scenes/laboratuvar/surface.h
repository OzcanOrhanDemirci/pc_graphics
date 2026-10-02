// Scene 4 "Tanecik": the liquid's surface, built on the GPU from particle positions every frame, as a triangle mesh
// the OptiX renderer can trace (vertices, smooth normals, indices, all in device memory).
//
// Method (Yu & Turk 2013, "Reconstructing Surfaces of Particle-Based Fluids Using Anisotropic Kernels"):
//   1. neighbours on a hashed grid (cells sorted with CUB; only occupied cells exist, however far the splash flies)
//   2. per particle: its neighbours' weighted mean and covariance (radius 3 spacings). The centre is pulled towards
//      the mean (smooths the disorder of a simulation) less the pull a flat free surface always has, so the liquid
//      keeps its volume; the kernel becomes an ellipsoid of the same volume, flattened along the surface's normal (a
//      flat surface stays flat, a thin sheet holds together). Few neighbours: a droplet, drawn by the renderer as a
//      sphere (droplets()), not meshed.
//   3. the density field is splatted (fixed-point atomics: the same input gives the same mesh) into sparse 8^3-voxel
//      blocks, allocated only where kernels reach (a hash of block coordinates). With a floor, kernels reaching under
//      it are splatted again as their mirror images.
//   4. surface nets at the level `iso` (bulk density = 1): a vertex per crossed cell (the mean of its edge crossings,
//      then pulled onto the level set), a quad per crossed grid edge; normals are the field's gradient (central
//      differences at the cell's corners, interpolated), so the shading is smooth across cells.
// Two small readbacks per build (the number of blocks and droplets, then of vertices and triangles); everything else
// stays on the GPU, on the caller's stream. Measurements and pictures: --scene tanecik --surface-test.
#pragma once
#include <cuda_runtime.h>

#include <cstddef>

struct FluidSurfaceSettings {
    float cellScale = 0.5f;          // voxel edge / particle spacing (spacing = 2 x radius)
    float supportScale = 2.0f;       // the bulk kernel's support radius / spacing
    float neighbourScale = 3.0f;     // radius of the neighbourhood the covariance is taken over / spacing
    float smoothing = 0.9f;          // lambda: how far a centre moves towards its neighbours' weighted mean (less the
                                     // pull a flat free surface always has: the liquid does not shrink)
    int smoothingPasses = 1;         // the smoothing repeated over the smoothed centres (same neighbours and weights)
    float maxStretch = 4.0f;         // k_r: the longest axis of a kernel at most this many times its shortest
    int minNeighbours = 6;           // fewer (itself included): a droplet (a sphere of its own, or a round kernel)
    bool dropletSpheres = true;      // droplets stay out of the mesh: the renderer draws them as spheres (droplets())
    float iso = 0.5f;                // the surface's level, in units of the bulk density
    float dropletRadius = 1.0f;      // with dropletSpheres off: a lone particle's meshed droplet radius / particle radius
    bool anisotropic = true;         // false: every kernel round (the bulk size), for comparison
    int projectSteps = 2;            // Newton steps pulling a vertex onto the level set inside its cell (0: none)
    float floorY = -1e30f;           // a floor: the liquid is mirrored in it (it meets the floor upright, no rounded lip,
                                     // and the floor side is no free surface); nothing under it is meshed (the mesh
                                     // opens onto the floor: a puddle has no underside)
};

class FluidSurface {
public:
    FluidSurfaceSettings settings;

    // capacities: particles, output vertices and triangles, field blocks (8^3 voxels each; ~4.6 KB with what goes
    // with them). Over capacity a build keeps what fits, the same part every time, and says so (truncated()).
    bool init(int maxParticles, int maxVertices, int maxTriangles, int maxBlocks = 131072);
    // device positions; an optional per-particle weight (null: all liquid; <= 0: not liquid, 0..1 scales its share);
    // the particles' radius (m). The mesh is ready when this returns (the stream is synchronised).
    void build(const float3* dPos, const float* dWeight, int n, float radius, cudaStream_t s);

    const float3* vertices() const { return vtx_; }
    const float3* normals() const { return nrm_; }
    const uint3* indices() const { return tri_; }
    // the particles left out of the mesh as droplets (dropletSpheres), as spheres: xyz the centre, w the radius. Each
    // is pulled to the centre of the droplet particles touching it and grown to their joint volume, so a small cluster
    // draws as one round drop. Grid order, the same every time.
    const float4* droplets() const { return drop_; }
    int dropletCount() const { return nDrops_; }
    int vertexCount() const { return nVerts_; }
    int triangleCount() const { return nTris_; }
    bool truncated() const { return truncated_; }
    int blockCount() const { return nBlocks_; }        // field blocks used in the last build
    size_t memoryBytes() const { return bytes_; }      // everything init allocated
    float lastMs = 0.0f;
    // where the time went in the last build (ms): neighbours (sort + cell table), kernels (covariance, eigen),
    // blocks (allocation, sort), splat, extraction (classify, scan, vertices, triangles)
    float msNeighbours = 0, msKernels = 0, msBlocks = 0, msSplat = 0, msExtract = 0;
    void destroy();
    ~FluidSurface() { destroy(); }

private:
    int maxP_ = 0, maxV_ = 0, maxT_ = 0, maxB_ = 0;
    size_t bytes_ = 0;
    // particles
    unsigned long long *pKey_ = nullptr, *pKeySorted_ = nullptr;
    int *pIdx_ = nullptr, *pIdxSorted_ = nullptr;
    float4* sPos_ = nullptr;               // sorted positions, w = weight
    float4 *kCen_ = nullptr, *kG0_ = nullptr, *kG1_ = nullptr, *kExt_ = nullptr;   // kernels (see surface.cu)
    float4 *kNrm_ = nullptr, *kCen2_ = nullptr;   // the surface's outward normal and how flat (smoothing passes)
    // the cell table (neighbours) and the block table (field)
    unsigned long long* cellKey_ = nullptr;
    int2* cellRange_ = nullptr;            // start, count in the sorted order
    unsigned cellCap_ = 0;
    unsigned long long* blkKey_ = nullptr;
    int* blkIdx_ = nullptr;
    unsigned blkCap_ = 0;
    unsigned long long *blkList_ = nullptr, *blkListSorted_ = nullptr;   // the blocks' coordinates, compacted, sorted
    int* field_ = nullptr;                 // fixed point, 512 per block
    int* cellVert_ = nullptr;              // a cell's vertex, -1 none, 512 per block
    unsigned char* cellBits_ = nullptr;    // what a cell holds (surface.cu), 512 per block
    unsigned long long *blkCount_ = nullptr, *blkOffset_ = nullptr;   // vertices | quads << 32, per block, and their scan
    int* counters_ = nullptr;              // device: blocks, overflow flags
    int* hostCounters_ = nullptr;          // pinned
    void* temp_ = nullptr;                 // CUB
    size_t tempBytes_ = 0;
    float3 *vtx_ = nullptr, *nrm_ = nullptr;
    uint3* tri_ = nullptr;
    int nVerts_ = 0, nTris_ = 0, nBlocks_ = 0, nDrops_ = 0;
    float4* drop_ = nullptr;
    unsigned char* dropFlag_ = nullptr;    // per sorted particle
    float4* dropIn_ = nullptr;             // per sorted particle: its sphere (compacted into drop_)
    bool truncated_ = false;
    cudaEvent_t ev_[6] = {};
};
