// Device buffers of the caustic photon map and its hash grid (built by CUDA kernels in photons.cu).
#pragma once
#include "shared/params.h"

#include <cuda_runtime.h>
#include <optix_types.h>

struct PhotonGrid {
    Photon* raw = nullptr;           // written by the photon raygen (unsorted)
    Photon* sorted = nullptr;        // grid order, read by the gather
    unsigned int* count = nullptr;   // atomic counter of deposited photons
    unsigned int* cellStart = nullptr;
    unsigned int* cellEnd = nullptr;
    unsigned int numPhotons = 0;

    void allocate(unsigned int capacity, unsigned int gridSize);
    // Reads the photon count, sorts and indexes; returns the number of photons in the map.
    unsigned int build(float cellSize, cudaStream_t stream);
    unsigned int capacity() const { return capacity_; }
    unsigned int gridSize() const { return gridSize_; }

private:
    unsigned int capacity_ = 0, gridSize_ = 0;
    unsigned int *keys_[2] = {nullptr, nullptr}, *vals_[2] = {nullptr, nullptr};
    void* temp_ = nullptr;
    size_t tempBytes_ = 0;
};

// One AABB per photon beam: the segment grown by the blur radius.
void launchBeamBoxes(const PhotonBeam* beams, unsigned int n, float radius, void* boxes, cudaStream_t stream);
