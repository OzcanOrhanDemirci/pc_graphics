// Smoke test, host side: proves the whole chain before the real work starts.
//   1. a CUDA kernel compiles (nvcc + MSVC) and runs on the RTX 4080
//   2. OptiX initialises from the driver, reports its RT core version
//   3. an OptiX-IR module (built by nvcc) loads, a pipeline links, a raygen launch writes pixels
//   4. NVML reads the GPU's power draw and limit
#include <cuda.h>
#include <cuda_runtime.h>
#include <nvml.h>
#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define CHECK_CUDA(x) do { cudaError_t e = (x); if (e != cudaSuccess) { std::printf("CUDA %s: %s\n", #x, cudaGetErrorString(e)); std::exit(1); } } while (0)
#define CHECK_OPTIX(x) do { OptixResult r = (x); if (r != OPTIX_SUCCESS) { std::printf("OptiX %s: %d\n", #x, (int)r); std::exit(1); } } while (0)

__global__ void square(float* v, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) v[i] = v[i] * v[i];
}

struct Params {
    unsigned int* image;
    unsigned int width;
    unsigned int height;
};

struct __align__(OPTIX_SBT_RECORD_ALIGNMENT) Record {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};

int main(int argc, char** argv) {
    // 1. CUDA
    cudaDeviceProp prop{};
    CHECK_CUDA(cudaGetDeviceProperties(&prop, 0));
    int driver = 0;
    CHECK_CUDA(cudaDriverGetVersion(&driver));
    std::printf("GPU %s, SM %d.%d, %d SMs, %.1f GB, CUDA driver %d\n", prop.name, prop.major, prop.minor,
                prop.multiProcessorCount, prop.totalGlobalMem / 1073741824.0, driver);
    std::vector<float> host(1024);
    for (int i = 0; i < 1024; ++i) host[i] = float(i);
    float* dev = nullptr;
    CHECK_CUDA(cudaMalloc(&dev, 1024 * sizeof(float)));
    CHECK_CUDA(cudaMemcpy(dev, host.data(), 1024 * sizeof(float), cudaMemcpyHostToDevice));
    square<<<4, 256>>>(dev, 1024);
    CHECK_CUDA(cudaMemcpy(host.data(), dev, 1024 * sizeof(float), cudaMemcpyDeviceToHost));
    std::printf("cuda kernel: 31^2 = %.0f %s\n", host[31], host[31] == 961.0f ? "OK" : "WRONG");

    // 2. OptiX
    CHECK_OPTIX(optixInit());
    OptixDeviceContext context = nullptr;
    OptixDeviceContextOptions options{};
    CHECK_OPTIX(optixDeviceContextCreate(0, &options, &context));
    unsigned int rtcore = 0, maxDepth = 0;
    CHECK_OPTIX(optixDeviceContextGetProperty(context, OPTIX_DEVICE_PROPERTY_RTCORE_VERSION, &rtcore, sizeof(rtcore)));
    CHECK_OPTIX(optixDeviceContextGetProperty(context, OPTIX_DEVICE_PROPERTY_LIMIT_MAX_TRACE_DEPTH, &maxDepth, sizeof(maxDepth)));
    std::printf("optix %d.%d.%d: RT core version %u, max trace depth %u\n", OPTIX_VERSION / 10000,
                (OPTIX_VERSION % 10000) / 100, OPTIX_VERSION % 100, rtcore, maxDepth);

    // 3. OptiX-IR module -> pipeline -> launch
    const char* irPath = argc > 1 ? argv[1] : "smoke_device.optixir";
    std::ifstream in(irPath, std::ios::binary);
    std::string ir((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (ir.empty()) { std::printf("cannot read %s\n", irPath); return 1; }
    OptixModuleCompileOptions moduleOptions{};
    OptixPipelineCompileOptions pipelineOptions{};
    pipelineOptions.pipelineLaunchParamsVariableName = "params";
    pipelineOptions.numPayloadValues = 0;
    pipelineOptions.numAttributeValues = 2;
    char log[2048];
    size_t logSize = sizeof(log);
    OptixModule module = nullptr;
    CHECK_OPTIX(optixModuleCreate(context, &moduleOptions, &pipelineOptions, ir.data(), ir.size(), log, &logSize, &module));
    OptixProgramGroupDesc raygenDesc{};
    raygenDesc.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    raygenDesc.raygen.module = module;
    raygenDesc.raygen.entryFunctionName = "__raygen__gradient";
    OptixProgramGroupDesc missDesc{};
    missDesc.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    OptixProgramGroupOptions groupOptions{};
    OptixProgramGroup groups[2];
    logSize = sizeof(log);
    CHECK_OPTIX(optixProgramGroupCreate(context, &raygenDesc, 1, &groupOptions, log, &logSize, &groups[0]));
    logSize = sizeof(log);
    CHECK_OPTIX(optixProgramGroupCreate(context, &missDesc, 1, &groupOptions, log, &logSize, &groups[1]));
    OptixPipelineLinkOptions linkOptions{};
    linkOptions.maxTraceDepth = 1;
    OptixPipeline pipeline = nullptr;
    logSize = sizeof(log);
    CHECK_OPTIX(optixPipelineCreate(context, &pipelineOptions, &linkOptions, groups, 2, log, &logSize, &pipeline));

    Record raygenRecord{}, missRecord{};
    CHECK_OPTIX(optixSbtRecordPackHeader(groups[0], &raygenRecord));
    CHECK_OPTIX(optixSbtRecordPackHeader(groups[1], &missRecord));
    CUdeviceptr dRaygen = 0, dMiss = 0;
    CHECK_CUDA(cudaMalloc(reinterpret_cast<void**>(&dRaygen), sizeof(Record)));
    CHECK_CUDA(cudaMalloc(reinterpret_cast<void**>(&dMiss), sizeof(Record)));
    CHECK_CUDA(cudaMemcpy(reinterpret_cast<void*>(dRaygen), &raygenRecord, sizeof(Record), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(reinterpret_cast<void*>(dMiss), &missRecord, sizeof(Record), cudaMemcpyHostToDevice));
    OptixShaderBindingTable sbt{};
    sbt.raygenRecord = dRaygen;
    sbt.missRecordBase = dMiss;
    sbt.missRecordStrideInBytes = sizeof(Record);
    sbt.missRecordCount = 1;

    const unsigned int W = 2560, H = 1440;
    Params params{};
    params.width = W;
    params.height = H;
    CHECK_CUDA(cudaMalloc(reinterpret_cast<void**>(&params.image), W * H * sizeof(unsigned int)));
    CUdeviceptr dParams = 0;
    CHECK_CUDA(cudaMalloc(reinterpret_cast<void**>(&dParams), sizeof(Params)));
    CHECK_CUDA(cudaMemcpy(reinterpret_cast<void*>(dParams), &params, sizeof(Params), cudaMemcpyHostToDevice));
    CHECK_OPTIX(optixLaunch(pipeline, 0, dParams, sizeof(Params), &sbt, W, H, 1));
    CHECK_CUDA(cudaDeviceSynchronize());
    std::vector<unsigned int> image(W * H);
    CHECK_CUDA(cudaMemcpy(image.data(), params.image, W * H * sizeof(unsigned int), cudaMemcpyDeviceToHost));
    const unsigned int probe = image[1000 * W + 1280];
    std::printf("optix launch 2560x1440: pixel(1280,1000) = (%u, %u) %s\n", probe & 255u, (probe >> 8) & 255u,
                (probe & 255u) == 127u && ((probe >> 8) & 255u) == 177u ? "OK" : "WRONG");

    // 3b. The AI denoiser ships with the driver: each model the renderer may use must be creatable.
    struct { OptixDenoiserModelKind kind; const char* name; } models[] = {
        {OPTIX_DENOISER_MODEL_KIND_HDR, "HDR"},
        {OPTIX_DENOISER_MODEL_KIND_AOV, "AOV"},
        {OPTIX_DENOISER_MODEL_KIND_TEMPORAL, "TEMPORAL"},
        {OPTIX_DENOISER_MODEL_KIND_TEMPORAL_AOV, "TEMPORAL_AOV"},
        {OPTIX_DENOISER_MODEL_KIND_UPSCALE2X, "UPSCALE2X"},
    };
    for (const auto& m : models) {
        OptixDenoiserOptions denoiserOptions{};
        denoiserOptions.guideAlbedo = 1;
        denoiserOptions.guideNormal = 1;
        OptixDenoiser denoiser = nullptr;
        OptixResult r = optixDenoiserCreate(context, m.kind, &denoiserOptions, &denoiser);
        OptixDenoiserSizes sizes{};
        if (r == OPTIX_SUCCESS) r = optixDenoiserComputeMemoryResources(denoiser, W, H, &sizes);
        std::printf("denoiser %-12s %s  state %.0f MB, scratch %.0f MB\n", m.name, r == OPTIX_SUCCESS ? "OK" : "FAILED",
                    sizes.stateSizeInBytes / 1048576.0, sizes.withoutOverlapScratchSizeInBytes / 1048576.0);
        if (denoiser) optixDenoiserDestroy(denoiser);
    }

    // 4. NVML
    if (nvmlInit_v2() == NVML_SUCCESS) {
        nvmlDevice_t gpu;
        nvmlDeviceGetHandleByIndex_v2(0, &gpu);
        unsigned int mw = 0, limit = 0;
        nvmlDeviceGetPowerUsage(gpu, &mw);
        nvmlDeviceGetEnforcedPowerLimit(gpu, &limit);
        std::printf("nvml: drawing %.1f W, limit %.0f W\n", mw / 1000.0, limit / 1000.0);
        nvmlShutdown();
    } else {
        std::printf("nvml: init failed\n");
    }
    std::printf("SMOKE OK\n");
    return 0;
}
