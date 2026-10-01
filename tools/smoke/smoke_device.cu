// Smoke test, device side: one OptiX ray generation program that writes a
// gradient, compiled to OptiX-IR at build time (nvcc -optix-ir).
#include <optix.h>

struct Params {
    unsigned int* image;
    unsigned int width;
    unsigned int height;
};

extern "C" {
__constant__ Params params;
}

extern "C" __global__ void __raygen__gradient() {
    const uint3 idx = optixGetLaunchIndex();
    params.image[idx.y * params.width + idx.x] = (idx.x * 255u / params.width) | ((idx.y * 255u / params.height) << 8);
}
