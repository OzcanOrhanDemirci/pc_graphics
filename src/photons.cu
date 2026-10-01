// Caustic photon grid build: hash every photon's cell, radix-sort by hash, reorder the photons, mark each
// hash bucket's range. The gather in programs.cu walks the 2x2x2 cells around a point.
#include "photons.h"
#include "shared/photon_grid.h"

#include <cub/device/device_radix_sort.cuh>

#include <cstdio>

__global__ void keysKernel(const Photon* ph, unsigned int n, float cellSize, unsigned int gridSize, unsigned int* keys, unsigned int* vals) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    keys[i] = photonCellHash(photonCell(ph[i].pos, cellSize), gridSize);
    vals[i] = i;
}

__global__ void reorderKernel(const Photon* in, const unsigned int* order, unsigned int n, Photon* out) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = in[order[i]];
}

__global__ void rangesKernel(const unsigned int* keys, unsigned int n, unsigned int* start, unsigned int* end) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const unsigned int k = keys[i];
    if (i == 0 || keys[i - 1] != k) start[k] = i;
    if (i == n - 1 || keys[i + 1] != k) end[k] = i + 1;
}

void PhotonGrid::allocate(unsigned int capacity, unsigned int gridSize) {
    capacity_ = capacity;
    gridSize_ = gridSize;
    cudaMalloc(&raw, capacity * sizeof(Photon));
    cudaMalloc(&sorted, capacity * sizeof(Photon));
    cudaMalloc(&count, sizeof(unsigned int));
    cudaMalloc(&keys_[0], capacity * sizeof(unsigned int));
    cudaMalloc(&keys_[1], capacity * sizeof(unsigned int));
    cudaMalloc(&vals_[0], capacity * sizeof(unsigned int));
    cudaMalloc(&vals_[1], capacity * sizeof(unsigned int));
    cudaMalloc(&cellStart, gridSize * sizeof(unsigned int));
    cudaMalloc(&cellEnd, gridSize * sizeof(unsigned int));
    cub::DeviceRadixSort::SortPairs(nullptr, tempBytes_, keys_[0], keys_[1], vals_[0], vals_[1], int(capacity));
    cudaMalloc(&temp_, tempBytes_);
}

unsigned int PhotonGrid::build(float cellSize, cudaStream_t stream) {
    unsigned int n = 0;
    cudaMemcpyAsync(&n, count, sizeof(unsigned int), cudaMemcpyDeviceToHost, stream);
    cudaStreamSynchronize(stream);
    if (n > capacity_) n = capacity_;
    numPhotons = n;
    cudaMemsetAsync(cellStart, 0xFF, gridSize_ * sizeof(unsigned int), stream);
    cudaMemsetAsync(cellEnd, 0xFF, gridSize_ * sizeof(unsigned int), stream);
    if (n == 0) return 0;
    const unsigned int threads = 256, blocks = (n + threads - 1) / threads;
    keysKernel<<<blocks, threads, 0, stream>>>(raw, n, cellSize, gridSize_, keys_[0], vals_[0]);
    int bits = 0;
    while ((1u << bits) < gridSize_) ++bits;
    size_t tb = tempBytes_;
    cub::DeviceRadixSort::SortPairs(temp_, tb, keys_[0], keys_[1], vals_[0], vals_[1], int(n), 0, bits, stream);
    reorderKernel<<<blocks, threads, 0, stream>>>(raw, vals_[1], n, sorted);
    rangesKernel<<<blocks, threads, 0, stream>>>(keys_[1], n, cellStart, cellEnd);
    return n;
}

__global__ void beamBoxesKernel(const PhotonBeam* beams, unsigned int n, float r, OptixAabb* boxes) {
    const unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n * BEAM_PIECES) return;
    const PhotonBeam b = beams[i / BEAM_PIECES];
    const float k = float(i % BEAM_PIECES);
    const float3 d = (b.b - b.a) * (1.0f / BEAM_PIECES);
    const float3 p = b.a + d * k, q = b.a + d * (k + 1.0f);
    boxes[i] = {fminf(p.x, q.x) - r, fminf(p.y, q.y) - r, fminf(p.z, q.z) - r, fmaxf(p.x, q.x) + r, fmaxf(p.y, q.y) + r, fmaxf(p.z, q.z) + r};
}

void launchBeamBoxes(const PhotonBeam* beams, unsigned int n, float radius, void* boxes, cudaStream_t stream) {
    const unsigned int threads = 256, blocks = (n * BEAM_PIECES + threads - 1) / threads;
    beamBoxesKernel<<<blocks, threads, 0, stream>>>(beams, n, radius, static_cast<OptixAabb*>(boxes));
}
