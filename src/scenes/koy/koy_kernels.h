// Scene 5 "Koy": CUDA kernels outside the OptiX programs: metering for the eye's adaptation, the sea surface
// (Tessendorf's FFT waves, looping), its slope pyramid (LEAN: the slope variance a footprint hides) and the caustic volume.
#pragma once
#include "shared/vec.h"

#include <cuda_runtime.h>

#include <vector>

// a seagrass leaf: its base, the direction it leans towards at rest (yaw, lean angle), its size, its own phase and stiffness
struct KoyLeaf {
    float3 base;
    float yaw, lean, length, halfWidth, phase, stiffness;
};
// the near-bottom current that sways the meadow: a swell's orbital velocity (linear wave theory at the floor's depth)
struct KoyCurrent {
    float3 dir;                  // horizontal direction the swell travels
    float speed;                 // orbital velocity amplitude at the floor (m/s)
    float omega, k;              // swell angular frequency, wavenumber
    float flutter;               // small fast fluttering (rad)
};
void koyGrassPoints(const KoyLeaf* leaves, int n, int pointsPerLeaf, float t, KoyCurrent cur, float4* pts, float3* nrm, cudaStream_t s);

// acc = acc * keep + src * w (the reference's average of shifted frames)
void koyBlend(float4* acc, const float4* src, size_t n, float keep, float w, cudaStream_t s);

// mean log2 luminance per 32x32 tile of a linear sRGB image
int koyMeterTiles(int w, int h);
void koyMeter(const float4* hdr, int w, int h, float* tiles, cudaStream_t s);

struct KoyOceanSettings {
    int N = 512;                 // grid (power of two)
    float patch = 16.0f;         // metres, periodic
    float wind = 4.0f;           // m/s at 10 m
    float windDirDeg = 30.0f;    // the direction the wind blows towards (degrees from +x towards +z)
    float fetchKm = 8.0f;        // JONSWAP fetch (a cove: short)
    float gamma = 3.3f;          // JONSWAP peak enhancement
    float spread = 6.0f;         // directional spreading exponent (cos^2s)
    float amplitude = 1.0f;      // art scale on heights (1 = the spectrum)
    float loopSeconds = 24.0f;   // frequencies quantised to multiples of 2 pi / loop: the waves repeat seamlessly
    float minWave = 0.06f;       // metres: shorter waves are left to the unresolved slope variance
    float swellAmplitude = 0.05f;// metres: long waves at the patch's harmonics (8 and 5 m)
    float unresolvedVar = -1.0f; // slope variance per axis of what the grid does not hold; < 0: Cox & Munk minus resolved
    unsigned int seed = 7;
};

struct KoyCausticSettings {
    int N = 1024;                // texels per side of a slice
    int rays = 1024;             // rays per side from the surface
    int slices = 32;
    float z0 = 0.5f, z1 = 20.0f; // depth range (metres), slices spaced logarithmically
    float minBlurTexels = 0.6f;  // the blur never narrower than this (texels)
    int levels = 4;              // pyramid for the shafts' march (each level half as wide in x, z)
};

class KoyOcean {
public:
    bool init(const KoyOceanSettings& o, const KoyCausticSettings& c, float3 sunAir, float waterN, float sunRadiusAir);
    void release();
    // the surface at time t: heights, slopes, the slope pyramid (texture), then the caustic volume
    void update(float t, cudaStream_t s, bool caustics = true);
    unsigned long long surfaceTexture() const { return surfTex_; }
    unsigned long long causticTexture(int level) const { return level < int(causTex_.size()) ? causTex_[level] : 0ull; }
    int causticLevels() const { return int(causTex_.size()); }
    float heightMax() const { return hmax_; }
    float slopeVarianceUnresolved() const { return varFloor_; }
    float msSurface = 0.0f, msCaustic = 0.0f;
    // host copies for tests and maps
    void readHeights(std::vector<float4>& out, cudaStream_t s);           // level 0: h, sx, sz, (sx^2 + sz^2) / 2
    void readCaustic(int slice, std::vector<float>& out, cudaStream_t s);  // level 0
    KoyOceanSettings oset;
    KoyCausticSettings cset;
    float sliceDepth(int k) const;

private:
    float2* h0_ = nullptr;       // spectrum amplitudes h0(k) (N x N)
    float* omega_ = nullptr;     // quantised angular frequency per k
    float2* spec_ = nullptr;     // 3 complex fields: height, slope x, slope z
    float4* field_ = nullptr;    // level 0 packed
    cudaMipmappedArray_t surfArr_ = nullptr;
    unsigned long long surfTex_ = 0;
    std::vector<cudaSurfaceObject_t> surfLevels_;
    int surfLevelCount_ = 0;
    float* caus_ = nullptr;      // N x N x slices accumulation
    float* causTmp_ = nullptr;
    float* dSig_ = nullptr;      // the blur's sigma per slice
    std::vector<cudaArray_t> causArr_;
    std::vector<unsigned long long> causTex_;
    std::vector<float*> causLevelBuf_;
    float hmax_ = 0.3f, varFloor_ = 0.0f;
    float3 sunAir_{}, sunWater_{};
    float waterN_ = 1.333f, sunRadiusAir_ = 0.00465f;
    cudaEvent_t e0_ = nullptr, e1_ = nullptr, e2_ = nullptr;
};
