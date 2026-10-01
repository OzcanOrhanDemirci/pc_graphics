// Small CUDA kernels outside the OptiX pipeline.
#include "denoiser.h"
#include "shared/spectrum.h"

// Denoiser input: white-balanced linear sRGB of the accumulated image without its caustic part; guides:
// first-hit albedo (RGB) and world-space normal.
__global__ void denoisePrepKernel(Params p, float4* rgb, float4* albedo, float4* normal, float4* caustic) {
    const unsigned int x = blockIdx.x * blockDim.x + threadIdx.x, y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p.renderWidth || y >= p.renderHeight) return;
    const unsigned int i = y * p.renderWidth + x;
    const float4 a = p.accum[i], c = p.causticAccum[i];
    const float inv = 1.0f / fmaxf(a.w, 1.0f);
    const float3 raw = make_float3(a.x - c.x, a.y - c.y, a.z - c.z) * inv;
    const float* m = p.wbMatrix;
    const float3 xyz = make_float3(m[0] * raw.x + m[1] * raw.y + m[2] * raw.z, m[3] * raw.x + m[4] * raw.y + m[5] * raw.z,
                                   m[6] * raw.x + m[7] * raw.y + m[8] * raw.z);
    const float3 s = xyzToLinearSRGB(xyz);
    rgb[i] = make_float4(fmaxf(s.x, 0.0f), fmaxf(s.y, 0.0f), fmaxf(s.z, 0.0f), 1.0f);
    {   // caustic layer, white balanced linear sRGB (may be slightly negative for spectral colours: kept)
        const float3 cr = make_float3(c.x, c.y, c.z) * inv;
        const float3 cx = make_float3(m[0] * cr.x + m[1] * cr.y + m[2] * cr.z, m[3] * cr.x + m[4] * cr.y + m[5] * cr.z,
                                      m[6] * cr.x + m[7] * cr.y + m[8] * cr.z);
        const float3 cs = xyzToLinearSRGB(cx);
        caustic[i] = make_float4(cs.x, cs.y, cs.z, 1.0f);
    }
    const float4 al = p.aovAlbedo[i], nr = p.aovNormal[i];
    const float ia = 1.0f / fmaxf(al.w, 1.0f);
    albedo[i] = make_float4(fminf(fmaxf(al.x * ia, 0.0f), 1.0f), fminf(fmaxf(al.y * ia, 0.0f), 1.0f), fminf(fmaxf(al.z * ia, 0.0f), 1.0f), 1.0f);
    float3 n = make_float3(nr.x, nr.y, nr.z);
    const float l = sqrtf(dot(n, n));
    n = l > 1e-6f ? n * (1.0f / l) : make_float3(0.0f, 0.0f, 0.0f);
    // the temporal model wants camera space (optixDenoiserInvoke docs): x right, y up, z towards the camera
    const float3 nc = make_float3(dot(n, p.cam.right), dot(n, p.cam.up), -dot(n, p.cam.fwd));
    normal[i] = make_float4(nc.x, nc.y, nc.z, 0.0f);
}

void launchDenoisePrep(const Params& p, float4* rgb, float4* albedo, float4* normal, float4* caustic, cudaStream_t stream) {
    const dim3 b(16, 16), g((p.renderWidth + 15) / 16, (p.renderHeight + 15) / 16);
    denoisePrepKernel<<<g, b, 0, stream>>>(p, rgb, albedo, normal, caustic);
}
