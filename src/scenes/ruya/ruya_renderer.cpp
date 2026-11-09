// Owns the OptiX pipeline and the frame-local optical instance table.
// Its buffers are wiped as soon as the ray launch has finished.
#include "scenes/ruya/ruya_renderer.h"
#include "scenes/ruya/ruya_island.h"
#include "scenes/ruya/ruya_material.h"
#include "stb_image.h"

#include <nvrtc.h>
#include <optix_stubs.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <map>
#include <tuple>
#include <sstream>

#define RUYA_CUDA(call) do { const cudaError_t result_ = (call); if (result_ != cudaSuccess) { \
    std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #call, cudaGetErrorString(result_)); std::exit(1); } } while (0)
#define RUYA_OPTIX(call) do { const OptixResult result_ = (call); if (result_ != OPTIX_SUCCESS) { \
    std::fprintf(stderr, "OptiX %s:%d %s: %s\n", __FILE__, __LINE__, #call, optixGetErrorString(result_)); std::exit(1); } } while (0)

namespace ruya {
namespace {
struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) Record { char header[OPTIX_SBT_RECORD_HEADER_SIZE]; };
static_assert(sizeof(Record) == OPTIX_SBT_RECORD_HEADER_SIZE);
double milliseconds() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void logOptix(unsigned level, const char* tag, const char* message, void*) {
    if (level <= 2) std::fprintf(stderr, "[ruya optix %u][%s] %s\n", level, tag, message);
}
void freeCuda(void* pointer) {
    if (!pointer) return;
    const cudaError_t result = cudaFree(pointer);
    if (result != cudaSuccess && result != cudaErrorCudartUnloading)
        std::fprintf(stderr, "Rüya CUDA release: %s\n", cudaGetErrorString(result));
}
void logOptixRelease(OptixResult result) {
    if (result != OPTIX_SUCCESS) std::fprintf(stderr, "Rüya OptiX release: %s\n", optixGetErrorString(result));
}
bool cellLess(const Cell& a, const Cell& b) {
    return a.z < b.z || (a.z == b.z && a.x < b.x);
}
OptixImage2D image(float4* pointer, unsigned width, unsigned height) {
    OptixImage2D result{};
    result.data = CUdeviceptr(pointer);
    result.width = width;
    result.height = height;
    result.rowStrideInBytes = width * unsigned(sizeof(float4));
    result.pixelStrideInBytes = unsigned(sizeof(float4));
    result.format = OPTIX_PIXEL_FORMAT_FLOAT4;
    return result;
}
}

struct Renderer::State {
    struct Prototype {
        Vertex* vertices = nullptr;
        uint3* indices = nullptr;
        void* gas = nullptr;
        OptixTraversableHandle handle = 0;
        unsigned long long triangleCount = 0;
        size_t bytes = 0;
        unsigned lastFrame = 0;
    };
    Params params{};
    FrameStats stats{};
    std::string device;
    cudaStream_t stream = nullptr;
    std::array<cudaEvent_t, 4> events{};
    OptixDeviceContext context = nullptr;
    OptixModule module = nullptr;
    OptixPipeline pipeline = nullptr;
    std::array<OptixProgramGroup, 4> groups{};
    OptixShaderBindingTable sbt{};
    Record* records = nullptr;
    Params* deviceParams = nullptr;
    Geometry* geometries = nullptr;
    std::vector<Prototype> prototypes;
    std::array<MaterialMap, kTextureSets * 2> textures{};
    unsigned long long* textureHandles = nullptr;
    // Island meshes live while their cell stays in the transport set, and a
    // short grace period after, so a cell at the boundary is not rebuilt.
    std::vector<Prototype> islands;
    std::vector<unsigned> freeIslands;
    std::map<std::tuple<int, int, int>, unsigned> islandSlots;
    unsigned frame = 0;
    size_t allocatedBytes = 0;
    OptixInstance* instances = nullptr;
    size_t instanceCapacity = 0;
    void* top = nullptr;
    size_t topCapacity = 0;
    void* scratch = nullptr;
    size_t scratchCapacity = 0;
    OptixDenoiser denoiser = nullptr;
    void* denoiserState = nullptr;
    size_t denoiserStateSize = 0;
    void* denoiserScratch = nullptr;
    size_t denoiserScratchSize = 0;
    float* intensity = nullptr;

    void allocate(void** pointer, size_t bytes) {
        if (!bytes) return;
        size_t freeBytes = 0;
        size_t totalBytes = 0;
        RUYA_CUDA(cudaMemGetInfo(&freeBytes, &totalBytes));
        if (bytes + 1024ULL * 1024ULL * 1024ULL > freeBytes) {
            std::fprintf(stderr, "Rüya GPU memory margin exhausted: requested %zu, free %zu\n", bytes, freeBytes);
            std::exit(1);
        }
        RUYA_CUDA(cudaMalloc(pointer, bytes));
        allocatedBytes += bytes;
    }
    void reserve(void** pointer, size_t& capacity, size_t requested) {
        if (capacity >= requested) return;
        if (*pointer) { freeCuda(*pointer); allocatedBytes -= capacity; }
        capacity = requested;
        allocate(pointer, capacity);
    }
    bool compile(const std::string& root, std::string& error) {
        const std::string path = root + "/src/scenes/ruya/device/ruya.cu";
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string source = buffer.str();
        if (source.empty()) { error = "Cannot read " + path; return false; }
        nvrtcProgram program = nullptr;
        nvrtcResult result = nvrtcCreateProgram(&program, source.c_str(), "ruya.cu", 0, nullptr, nullptr);
        if (result != NVRTC_SUCCESS) { error = nvrtcGetErrorString(result); return false; }
        const std::array<std::string, 3> includes{"-I" + root + "/src", "-I" + root + "/third_party/optix/include", "-I" + root + "/third_party/cuda/include"};
        // The SM89 A/B retained fast math after comparing both raw HDR and displayed pixels.
        std::vector<const char*> options{"--std=c++17", "-arch=compute_89", "--relocatable-device-code=true",
                                         "--optix-ir", "-lineinfo", "-D__x86_64", "--use_fast_math"};
        for (const std::string& include : includes) options.push_back(include.c_str());
        result = nvrtcCompileProgram(program, int(options.size()), options.data());
        size_t logSize = 0;
        const nvrtcResult logResult = nvrtcGetProgramLogSize(program, &logSize);
        std::string log(logSize, '\0');
        if (logResult == NVRTC_SUCCESS && logSize > 1) nvrtcGetProgramLog(program, log.data());
        if (result != NVRTC_SUCCESS) {
            error = "NVRTC ruya.cu: " + std::string(nvrtcGetErrorString(result)) + "\n" + log;
            nvrtcDestroyProgram(&program);
            return false;
        }
        size_t size = 0;
        result = nvrtcGetOptiXIRSize(program, &size);
        if (result != NVRTC_SUCCESS) { error = nvrtcGetErrorString(result); nvrtcDestroyProgram(&program); return false; }
        std::string ir(size, '\0');
        result = nvrtcGetOptiXIR(program, ir.data());
        nvrtcDestroyProgram(&program);
        if (result != NVRTC_SUCCESS) { error = nvrtcGetErrorString(result); return false; }

        OptixModuleCompileOptions moduleOptions{};
        moduleOptions.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
        moduleOptions.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;
        OptixPipelineCompileOptions pipelineOptions{};
        pipelineOptions.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
        pipelineOptions.numPayloadValues = 3;
        pipelineOptions.numAttributeValues = 2;
        pipelineOptions.pipelineLaunchParamsVariableName = "params";
        pipelineOptions.pipelineLaunchParamsSizeInBytes = sizeof(Params);
        pipelineOptions.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
        char optixLog[8192]{};
        size_t optixLogSize = sizeof(optixLog);
        OptixResult optixResult = optixModuleCreate(context, &moduleOptions, &pipelineOptions, ir.data(), ir.size(), optixLog, &optixLogSize, &module);
        if (optixResult != OPTIX_SUCCESS) { error = std::string(optixGetErrorString(optixResult)) + "\n" + optixLog; return false; }
        std::array<OptixProgramGroupDesc, 4> descriptions{};
        descriptions[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        descriptions[0].raygen.module = module;
        descriptions[0].raygen.entryFunctionName = "__raygen__render";
        descriptions[1].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        descriptions[1].raygen.module = module;
        descriptions[1].raygen.entryFunctionName = "__raygen__final";
        descriptions[2].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
        descriptions[2].miss.module = module;
        descriptions[2].miss.entryFunctionName = "__miss__surface";
        descriptions[3].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        descriptions[3].hitgroup.moduleCH = module;
        descriptions[3].hitgroup.entryFunctionNameCH = "__closesthit__surface";
        OptixProgramGroupOptions groupOptions{};
        optixLogSize = sizeof(optixLog);
        optixResult = optixProgramGroupCreate(context, descriptions.data(), unsigned(descriptions.size()), &groupOptions, optixLog, &optixLogSize, groups.data());
        if (optixResult != OPTIX_SUCCESS) { error = std::string(optixGetErrorString(optixResult)) + "\n" + optixLog; return false; }
        OptixPipelineLinkOptions linkOptions{};
        linkOptions.maxTraceDepth = 1;
        optixLogSize = sizeof(optixLog);
        optixResult = optixPipelineCreate(context, &pipelineOptions, &linkOptions, groups.data(), unsigned(groups.size()), optixLog, &optixLogSize, &pipeline);
        if (optixResult != OPTIX_SUCCESS) { error = std::string(optixGetErrorString(optixResult)) + "\n" + optixLog; return false; }
        RUYA_OPTIX(optixPipelineSetStackSizeFromCallDepths(pipeline, 1, 0, 0, 0, 2));
        std::array<Record, 4> hostRecords{};
        for (size_t i = 0; i < groups.size(); ++i) RUYA_OPTIX(optixSbtRecordPackHeader(groups[i], &hostRecords[i]));
        allocate(reinterpret_cast<void**>(&records), sizeof(hostRecords));
        RUYA_CUDA(cudaMemcpyAsync(records, hostRecords.data(), sizeof(hostRecords), cudaMemcpyHostToDevice, stream));
        RUYA_CUDA(cudaStreamSynchronize(stream));
        sbt.missRecordBase = CUdeviceptr(records + 2);
        sbt.missRecordStrideInBytes = sizeof(Record);
        sbt.missRecordCount = 1;
        sbt.hitgroupRecordBase = CUdeviceptr(records + 3);
        sbt.hitgroupRecordStrideInBytes = sizeof(Record);
        sbt.hitgroupRecordCount = 1;
        return true;
    }
    void uploadPrototype(const Mesh& mesh, Prototype& prototype) {
        allocate(reinterpret_cast<void**>(&prototype.vertices), mesh.vertices.size() * sizeof(Vertex));
        allocate(reinterpret_cast<void**>(&prototype.indices), mesh.indices.size() * sizeof(uint3));
        RUYA_CUDA(cudaMemcpyAsync(prototype.vertices, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex), cudaMemcpyHostToDevice, stream));
        RUYA_CUDA(cudaMemcpyAsync(prototype.indices, mesh.indices.data(), mesh.indices.size() * sizeof(uint3), cudaMemcpyHostToDevice, stream));
        CUdeviceptr vertices = CUdeviceptr(prototype.vertices);
        unsigned flag = OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT;
        OptixBuildInput input{};
        input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        input.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        input.triangleArray.vertexStrideInBytes = sizeof(Vertex);
        input.triangleArray.numVertices = unsigned(mesh.vertices.size());
        input.triangleArray.vertexBuffers = &vertices;
        input.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        input.triangleArray.indexStrideInBytes = sizeof(uint3);
        input.triangleArray.numIndexTriplets = unsigned(mesh.indices.size());
        input.triangleArray.indexBuffer = CUdeviceptr(prototype.indices);
        input.triangleArray.flags = &flag;
        input.triangleArray.numSbtRecords = 1;
        OptixAccelBuildOptions options{};
        options.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE | OPTIX_BUILD_FLAG_ALLOW_COMPACTION;
        options.operation = OPTIX_BUILD_OPERATION_BUILD;
        OptixAccelBufferSizes sizes{};
        RUYA_OPTIX(optixAccelComputeMemoryUsage(context, &options, &input, 1, &sizes));
        void* temporary = nullptr;
        void* output = nullptr;
        size_t* compactedSize = nullptr;
        allocate(&temporary, sizes.tempSizeInBytes);
        allocate(&output, sizes.outputSizeInBytes);
        allocate(reinterpret_cast<void**>(&compactedSize), sizeof(size_t));
        OptixAccelEmitDesc emit{};
        emit.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
        emit.result = CUdeviceptr(compactedSize);
        RUYA_OPTIX(optixAccelBuild(context, stream, &options, &input, 1, CUdeviceptr(temporary), sizes.tempSizeInBytes,
                                  CUdeviceptr(output), sizes.outputSizeInBytes, &prototype.handle, &emit, 1));
        size_t compactedBytes = 0;
        RUYA_CUDA(cudaMemcpyAsync(&compactedBytes, compactedSize, sizeof(size_t), cudaMemcpyDeviceToHost, stream));
        RUYA_CUDA(cudaStreamSynchronize(stream));
        freeCuda(temporary);
        allocatedBytes -= sizes.tempSizeInBytes;
        freeCuda(compactedSize);
        allocatedBytes -= sizeof(size_t);
        if (compactedBytes < sizes.outputSizeInBytes && compactedBytes > 0) {
            allocate(&prototype.gas, compactedBytes);
            RUYA_OPTIX(optixAccelCompact(context, stream, prototype.handle, CUdeviceptr(prototype.gas), compactedBytes, &prototype.handle));
            RUYA_CUDA(cudaStreamSynchronize(stream));
            freeCuda(output);
            allocatedBytes -= sizes.outputSizeInBytes;
        } else prototype.gas = output;
        prototype.triangleCount = mesh.indices.size();
    }
    void setupDenoiser() {
        OptixDenoiserOptions options{};
        options.guideAlbedo = 1;
        options.guideNormal = 1;
        RUYA_OPTIX(optixDenoiserCreate(context, OPTIX_DENOISER_MODEL_KIND_HDR, &options, &denoiser));
        OptixDenoiserSizes sizes{};
        RUYA_OPTIX(optixDenoiserComputeMemoryResources(denoiser, params.width, params.height, &sizes));
        denoiserStateSize = sizes.stateSizeInBytes;
        denoiserScratchSize = std::max(sizes.withoutOverlapScratchSizeInBytes, sizes.computeIntensitySizeInBytes);
        allocate(&denoiserState, denoiserStateSize);
        allocate(&denoiserScratch, denoiserScratchSize);
        allocate(reinterpret_cast<void**>(&intensity), sizeof(float));
        RUYA_OPTIX(optixDenoiserSetup(denoiser, stream, params.width, params.height, CUdeviceptr(denoiserState), denoiserStateSize,
                                    CUdeviceptr(denoiserScratch), denoiserScratchSize));
    }
    void denoise() {
        OptixDenoiserGuideLayer guide{};
        guide.albedo = image(params.albedo, params.width, params.height);
        guide.normal = image(params.normal, params.width, params.height);
        OptixDenoiserLayer layer{};
        layer.input = image(params.illumination, params.width, params.height);
        layer.output = image(params.filtered, params.width, params.height);
        RUYA_OPTIX(optixDenoiserComputeIntensity(denoiser, stream, &layer.input, CUdeviceptr(intensity), CUdeviceptr(denoiserScratch), denoiserScratchSize));
        OptixDenoiserParams options{};
        options.hdrIntensity = CUdeviceptr(intensity);
        options.blendFactor = 0;
        RUYA_OPTIX(optixDenoiserInvoke(denoiser, stream, &options, CUdeviceptr(denoiserState), denoiserStateSize,
                                     &guide, &layer, 1, 0, 0, CUdeviceptr(denoiserScratch), denoiserScratchSize));
    }
    bool loadTextures(const std::string& root, std::string& error) {
        static const char* names[kTextureSets] = {"rock_face_03", "aerial_grass_rock", "large_sandstone_blocks_01"};
        struct Image { int width = 0, height = 0; unsigned char* pixels = nullptr; };
        std::vector<std::future<Image>> jobs;
        for (const char* name : names)
            for (const char* map : {"diff", "nor_gl", "arm"})
                jobs.push_back(std::async(std::launch::async, [path = root + "/assets/ruya/cc0/" + name + "_" + map + ".jpg"] {
                    Image image;
                    int channels = 0;
                    image.pixels = stbi_load(path.c_str(), &image.width, &image.height, &channels, 4);
                    return image;
                }));
        std::vector<Image> images;
        for (auto& job : jobs) images.push_back(job.get());
        bool ok = true;
        for (const Image& image : images) ok = ok && image.pixels && image.width == image.height && image.width == images[0].width;
        if (ok) {
            const int side = images[0].width;
            std::vector<unsigned char> packed(size_t(side) * side * 4);
            for (int set = 0; set < kTextureSets && ok; ++set) {
                const Image& color = images[set * 3];
                const Image& normal = images[set * 3 + 1];
                const Image& arm = images[set * 3 + 2];
                // Colour with roughness in alpha; normal xy with occlusion in z.
                for (size_t i = 0; i < packed.size(); i += 4) {
                    packed[i] = color.pixels[i];
                    packed[i + 1] = color.pixels[i + 1];
                    packed[i + 2] = color.pixels[i + 2];
                    packed[i + 3] = arm.pixels[i + 1];
                }
                ok = uploadMaterialMap(textures[set * 2], packed.data(), side, true, stream) == cudaSuccess;
                for (size_t i = 0; i < packed.size(); i += 4) {
                    packed[i] = normal.pixels[i];
                    packed[i + 1] = normal.pixels[i + 1];
                    packed[i + 2] = arm.pixels[i];
                    packed[i + 3] = 255;
                }
                ok = ok && uploadMaterialMap(textures[set * 2 + 1], packed.data(), side, false, stream) == cudaSuccess;
                allocatedBytes += 2 * size_t(side) * side * 4 * 4 / 3;
            }
        }
        for (const Image& image : images) stbi_image_free(image.pixels);
        if (!ok) { error = "Rüya textures missing or unreadable: run python assets/ruya/fetch.py"; return false; }
        unsigned long long handles[kTextureSets * 2];
        for (int i = 0; i < kTextureSets * 2; ++i) handles[i] = textures[i].texture;
        allocate(reinterpret_cast<void**>(&textureHandles), sizeof(handles));
        RUYA_CUDA(cudaMemcpyAsync(textureHandles, handles, sizeof(handles), cudaMemcpyHostToDevice, stream));
        RUYA_CUDA(cudaStreamSynchronize(stream));
        params.textures = textureHandles;
        return true;
    }
    void releaseIsland(unsigned slot) {
        Prototype& island = islands[slot];
        freeCuda(island.vertices);
        freeCuda(island.indices);
        freeCuda(island.gas);
        allocatedBytes -= island.bytes;
        island = {};
        freeIslands.push_back(slot);
    }
    // Builds the missing island meshes of the plans in parallel and drops
    // islands no plan has used for the grace period.
    void ensureIslands(const std::vector<std::pair<const Cell*, std::shared_ptr<const CellPlan>>>& plans) {
        std::vector<std::tuple<int, int, int>> missingKeys;
        std::vector<const Island*> missing;
        for (const auto& [cell, plan] : plans)
            for (int i = 0; i < plan->islandCount; ++i) {
                const auto key = std::make_tuple(cell->x, cell->z, i);
                const auto found = islandSlots.find(key);
                if (found != islandSlots.end()) islands[found->second].lastFrame = frame;
                else { missingKeys.push_back(key); missing.push_back(&plan->islands[i]); }
            }
        std::vector<std::future<Mesh>> jobs;
        for (const Island* island : missing) jobs.push_back(std::async(std::launch::async, [island] { return islandMesh(*island); }));
        for (size_t i = 0; i < jobs.size(); ++i) {
            const Mesh mesh = jobs[i].get();
            if (freeIslands.empty()) {
                if (islands.size() >= kIslandSlots) { std::fprintf(stderr, "Rüya island slots exhausted\n"); std::exit(1); }
                freeIslands.push_back(unsigned(islands.size()));
                islands.emplace_back();
            }
            const unsigned slot = freeIslands.back();
            freeIslands.pop_back();
            const size_t before = allocatedBytes;
            uploadPrototype(mesh, islands[slot]);
            islands[slot].bytes = allocatedBytes - before;
            islands[slot].lastFrame = frame;
            islandSlots[missingKeys[i]] = slot;
            const Geometry geometry{islands[slot].vertices, islands[slot].indices};
            RUYA_CUDA(cudaMemcpyAsync(geometries + kPrototypeCount + slot, &geometry, sizeof(Geometry), cudaMemcpyHostToDevice, stream));
            RUYA_CUDA(cudaStreamSynchronize(stream));
        }
        for (auto it = islandSlots.begin(); it != islandSlots.end();) {
            if (frame - islands[it->second].lastFrame > kIslandGraceFrames) { releaseIsland(it->second); it = islandSlots.erase(it); }
            else ++it;
        }
    }
    void launch(unsigned record) {
        sbt.raygenRecord = CUdeviceptr(records + record);
        RUYA_OPTIX(optixLaunch(pipeline, stream, CUdeviceptr(deviceParams), sizeof(Params), &sbt, params.width, params.height, 1));
    }
};

Renderer::Renderer() = default;
Renderer::~Renderer() { release(); }
bool Renderer::init(int width, int height, const std::string& root, const std::vector<Mesh>& meshes, std::string& error) {
    release();
    if (width <= 0 || height <= 0 || meshes.empty() || meshes.size() > kPrototypeCount) { error = "Invalid Rüya dimensions or prototypes"; return false; }
    for (const Mesh& mesh : meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty()) { error = "Empty Ruya prototype"; return false; }
        for (const Vertex& vertex : mesh.vertices) {
            const float3 relative = vertex.position - make_float3(0, kCellBoundsY, 0);
            if (!(dot(relative, relative) <= sqr(kCellBoundsRadius))) { error = "Ruya prototype exceeds its visibility bounds"; return false; }
        }
    }
    state_ = std::make_unique<State>();
    State& state = *state_;
    RUYA_CUDA(cudaSetDevice(0));
    RUYA_CUDA(cudaFree(nullptr));
    cudaDeviceProp device{};
    RUYA_CUDA(cudaGetDeviceProperties(&device, 0));
    state.device = device.name;
    if (device.major != 8 || device.minor != 9) { error = "Rüya targets the local Ada SM89 device"; release(); return false; }
    RUYA_CUDA(cudaStreamCreateWithFlags(&state.stream, cudaStreamNonBlocking));
    for (cudaEvent_t& event : state.events) RUYA_CUDA(cudaEventCreate(&event));
    RUYA_OPTIX(optixInit());
    OptixDeviceContextOptions contextOptions{};
    contextOptions.logCallbackFunction = logOptix;
    contextOptions.logCallbackLevel = 3;
    RUYA_OPTIX(optixDeviceContextCreate(nullptr, &contextOptions, &state.context));
    state.params.width = unsigned(width);
    state.params.height = unsigned(height);
    state.params.layout = sizeof(Params);
    const size_t count = size_t(width) * size_t(height);
    state.allocate(reinterpret_cast<void**>(&state.params.color), count * sizeof(float4));
    state.allocate(reinterpret_cast<void**>(&state.params.illumination), count * sizeof(float4));
    state.allocate(reinterpret_cast<void**>(&state.params.albedo), count * sizeof(float4));
    state.allocate(reinterpret_cast<void**>(&state.params.normal), count * sizeof(float4));
    state.allocate(reinterpret_cast<void**>(&state.params.filtered), count * sizeof(float4));
    state.allocate(reinterpret_cast<void**>(&state.params.depth), count * sizeof(float4));
    state.allocate(reinterpret_cast<void**>(&state.params.output), count * sizeof(uchar4));
    state.allocate(reinterpret_cast<void**>(&state.params.badCount), sizeof(unsigned));
    state.allocate(reinterpret_cast<void**>(&state.deviceParams), sizeof(Params));
    if (!state.compile(root, error)) { release(); return false; }
    if (!state.loadTextures(root, error)) { release(); return false; }
    state.prototypes.resize(meshes.size());
    std::vector<Geometry> geometries;
    for (size_t i = 0; i < meshes.size(); ++i) {
        state.uploadPrototype(meshes[i], state.prototypes[i]);
        geometries.push_back({state.prototypes[i].vertices, state.prototypes[i].indices});
    }
    geometries.resize(kPrototypeCount + kIslandSlots, Geometry{});
    state.allocate(reinterpret_cast<void**>(&state.geometries), geometries.size() * sizeof(Geometry));
    RUYA_CUDA(cudaMemcpyAsync(state.geometries, geometries.data(), geometries.size() * sizeof(Geometry), cudaMemcpyHostToDevice, state.stream));
    state.params.geometries = state.geometries;
    state.setupDenoiser();
    RUYA_CUDA(cudaStreamSynchronize(state.stream));
    state.stats.residentBytes = state.allocatedBytes;
    error.clear();
    return true;
}

void Renderer::setCells(const std::vector<Cell>& visible, const std::vector<Cell>& transport) {
    if (!state_) return;
    State& state = *state_;
    const double start = milliseconds();
    const auto& cells = transport.empty() ? visible : transport;
    state.params.handle = 0;
    state.stats.cells = unsigned(visible.size());
    state.stats.transportCells = unsigned(cells.size());
    state.stats.instancedTriangles = 0;
    if (cells.empty()) {
        state.stats.uploadMs = 0;
        return;
    }
    std::vector<OptixInstance> instances;
    std::vector<InstanceData> data;
    instances.reserve(cells.size() * 24);
    data.reserve(instances.capacity());
    ++state.frame;
    std::vector<std::pair<const Cell*, std::shared_ptr<const CellPlan>>> plans;
    plans.reserve(cells.size());
    for (const Cell& cell : cells) plans.emplace_back(&cell, cachedPlan(cell));
    state.ensureIslands(plans);
    auto nextVisible = visible.begin();
    for (const auto& [cellPointer, planPointer] : plans) {
        const Cell& cell = *cellPointer;
        const CellPlan& plan = *planPointer;
        while (nextVisible != visible.end() && cellLess(*nextVisible, cell)) ++nextVisible;
        const bool inView = nextVisible != visible.end() && nextVisible->x == cell.x && nextVisible->z == cell.z;
        for (int i = 0; i < plan.islandCount; ++i) {
            const unsigned slot = state.islandSlots.at(std::make_tuple(cell.x, cell.z, i));
            OptixInstance instance{};
            const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
            std::memcpy(instance.transform, identity, sizeof(identity));
            instance.instanceId = unsigned(data.size());
            instance.visibilityMask = kTransportRayMask | (inView ? kCameraRayMask : 0);
            instance.flags = OPTIX_INSTANCE_FLAG_DISABLE_ANYHIT;
            instance.traversableHandle = state.islands[slot].handle;
            if (inView) state.stats.instancedTriangles += state.islands[slot].triangleCount;
            instances.push_back(instance);
            data.push_back({kPrototypeCount + slot, plan.islands[i].seed, cell.region});
        }
        for (const Piece& piece : plan.pieces) {
            OptixInstance instance{};
            const float cosine = std::cos(piece.yaw);
            const float sine = std::sin(piece.yaw);
            const auto [x, y, z] = piece.scale;
            const float transform[12] = {cosine*x, 0, sine*z, piece.offset.x, 0, y, 0, piece.offset.y,
                                         -sine*x, 0, cosine*z, piece.offset.z};
            std::memcpy(instance.transform, transform, sizeof(transform));
            instance.instanceId = unsigned(data.size());
            instance.visibilityMask = kTransportRayMask | (inView ? kCameraRayMask : 0);
            instance.flags = OPTIX_INSTANCE_FLAG_DISABLE_ANYHIT;
            instance.traversableHandle = state.prototypes[piece.prototype].handle;
            if (inView) state.stats.instancedTriangles += state.prototypes[piece.prototype].triangleCount;
            instances.push_back(instance);
            data.push_back({piece.prototype, piece.seed, piece.region});
        }
    }
    if (data.empty()) return;
    if (data.size() > state.instanceCapacity) {
        if (state.params.instances) {
            freeCuda(const_cast<InstanceData*>(state.params.instances));
            freeCuda(state.instances);
            state.allocatedBytes -= state.instanceCapacity * (sizeof(InstanceData) + sizeof(OptixInstance));
        }
        state.instanceCapacity = std::max(size_t(64), state.instanceCapacity);
        while (state.instanceCapacity < data.size()) state.instanceCapacity *= 2;
        InstanceData* deviceData = nullptr;
        state.allocate(reinterpret_cast<void**>(&deviceData), state.instanceCapacity * sizeof(InstanceData));
        state.params.instances = deviceData;
        state.allocate(reinterpret_cast<void**>(&state.instances), state.instanceCapacity * sizeof(OptixInstance));
    }
    RUYA_CUDA(cudaMemcpyAsync(const_cast<InstanceData*>(state.params.instances), data.data(),
                              data.size() * sizeof(InstanceData), cudaMemcpyHostToDevice, state.stream));
    RUYA_CUDA(cudaMemcpyAsync(state.instances, instances.data(), instances.size() * sizeof(OptixInstance),
                              cudaMemcpyHostToDevice, state.stream));
    OptixBuildInput input{};
    input.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    input.instanceArray.instances = CUdeviceptr(state.instances);
    input.instanceArray.numInstances = unsigned(instances.size());
    OptixAccelBuildOptions options{};
    options.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
    options.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sizes{};
    RUYA_OPTIX(optixAccelComputeMemoryUsage(state.context, &options, &input, 1, &sizes));
    state.reserve(&state.scratch, state.scratchCapacity, sizes.tempSizeInBytes);
    state.reserve(&state.top, state.topCapacity, sizes.outputSizeInBytes);
    RUYA_OPTIX(optixAccelBuild(state.context, state.stream, &options, &input, 1,
                              CUdeviceptr(state.scratch), state.scratchCapacity, CUdeviceptr(state.top), state.topCapacity,
                              &state.params.handle, nullptr, 0));
    RUYA_CUDA(cudaStreamSynchronize(state.stream));
    state.stats.residentBytes = state.allocatedBytes;
    state.stats.opticalPoolBytes = state.instanceCapacity * (sizeof(InstanceData) + sizeof(OptixInstance)) +
                                   state.topCapacity + state.scratchCapacity;
    state.stats.uploadMs = float(milliseconds() - start);
}

void Renderer::render(const Camera& camera, int spp, int bounces, int sampleOffset, bool denoise, float exposure, int materialDetail) {
    if (!state_) return;
    State& state = *state_;
    state.params.eye = camera.eye;
    cameraBasis(camera, float(state.params.width) / float(state.params.height), state.params.U, state.params.V, state.params.W);
    state.params.spp = std::clamp(spp, 1, 4096);
    state.params.bounces = std::clamp(bounces, 0, 16);
    denoise = denoise && bounces > 0;
    state.params.sampleOffset = std::max(sampleOffset, 0);
    state.params.raw = denoise ? 0 : 1;
    state.params.exposure = exposure;
    state.params.materialDetail = materialDetail;
    RUYA_CUDA(cudaEventRecord(state.events[0], state.stream));
    RUYA_CUDA(cudaMemsetAsync(state.params.badCount, 0, sizeof(unsigned), state.stream));
    RUYA_CUDA(cudaMemcpyAsync(state.deviceParams, &state.params, sizeof(Params), cudaMemcpyHostToDevice, state.stream));
    state.launch(0);
    RUYA_CUDA(cudaEventRecord(state.events[1], state.stream));
    if (state.params.handle) {
        RUYA_CUDA(cudaMemsetAsync(const_cast<InstanceData*>(state.params.instances), 0, state.instanceCapacity * sizeof(InstanceData), state.stream));
        RUYA_CUDA(cudaMemsetAsync(state.instances, 0, state.instanceCapacity * sizeof(OptixInstance), state.stream));
        RUYA_CUDA(cudaMemsetAsync(state.top, 0, state.topCapacity, state.stream));
        RUYA_CUDA(cudaMemsetAsync(state.scratch, 0, state.scratchCapacity, state.stream));
    }
    if (denoise) state.denoise();
    RUYA_CUDA(cudaEventRecord(state.events[2], state.stream));
    state.launch(1);
    RUYA_CUDA(cudaEventRecord(state.events[3], state.stream));
    RUYA_CUDA(cudaMemcpyAsync(&state.stats.badSamples, state.params.badCount, sizeof(unsigned), cudaMemcpyDeviceToHost, state.stream));
    RUYA_CUDA(cudaStreamSynchronize(state.stream));
    state.params.handle = 0;
    RUYA_CUDA(cudaEventElapsedTime(&state.stats.traceMs, state.events[0], state.events[1]));
    RUYA_CUDA(cudaEventElapsedTime(&state.stats.denoiseMs, state.events[1], state.events[2]));
    RUYA_CUDA(cudaEventElapsedTime(&state.stats.displayMs, state.events[2], state.events[3]));
    size_t total = 0;
    RUYA_CUDA(cudaMemGetInfo(&state.stats.freeBytes, &total));
}
void Renderer::download(std::vector<unsigned char>& rgba) const {
    if (!state_) { rgba.clear(); return; }
    const State& state = *state_;
    rgba.resize(size_t(state.params.width) * state.params.height * sizeof(uchar4));
    RUYA_CUDA(cudaMemcpyAsync(rgba.data(), state.params.output, rgba.size(), cudaMemcpyDeviceToHost, state.stream));
    RUYA_CUDA(cudaStreamSynchronize(state.stream));
}
const uchar4* Renderer::devicePixels() const { return state_ ? state_->params.output : nullptr; }
std::vector<float4> Renderer::hdr() const {
    if (!state_) return {};
    const State& state = *state_;
    std::vector<float4> result(size_t(state.params.width) * state.params.height);
    RUYA_CUDA(cudaMemcpyAsync(result.data(), state.params.color, result.size() * sizeof(float4), cudaMemcpyDeviceToHost, state.stream));
    RUYA_CUDA(cudaStreamSynchronize(state.stream));
    return result;
}
std::vector<float4> Renderer::depth() const {
    if (!state_) return {};
    const State& state = *state_;
    std::vector<float4> result(size_t(state.params.width) * state.params.height);
    RUYA_CUDA(cudaMemcpyAsync(result.data(), state.params.depth, result.size() * sizeof(float4), cudaMemcpyDeviceToHost, state.stream));
    RUYA_CUDA(cudaStreamSynchronize(state.stream));
    return result;
}
bool Renderer::opticalPoolIsClear() const {
    if (!state_) return true;
    const State& state = *state_;
    const auto isZero = [&](const void* data, size_t bytes) {
        if (!bytes) return true;
        std::vector<unsigned char> contents(bytes);
        RUYA_CUDA(cudaMemcpyAsync(contents.data(), data, bytes, cudaMemcpyDeviceToHost, state.stream));
        RUYA_CUDA(cudaStreamSynchronize(state.stream));
        return std::all_of(contents.begin(), contents.end(), [](unsigned char byte) { return byte == 0; });
    };
    return isZero(state.params.instances, state.instanceCapacity * sizeof(InstanceData)) &&
           isZero(state.instances, state.instanceCapacity * sizeof(OptixInstance)) &&
           isZero(state.top, state.topCapacity) && isZero(state.scratch, state.scratchCapacity);
}
const FrameStats& Renderer::stats() const {
    static const FrameStats empty{};
    return state_ ? state_->stats : empty;
}
const std::string& Renderer::deviceName() const {
    static const std::string empty;
    return state_ ? state_->device : empty;
}
void Renderer::release() {
    if (!state_) return;
    State& state = *state_;
    if (state.stream) {
        const cudaError_t result = cudaStreamSynchronize(state.stream);
        if (result != cudaSuccess && result != cudaErrorCudartUnloading) std::fprintf(stderr, "Rüya stream release: %s\n", cudaGetErrorString(result));
    }
    if (state.denoiser) logOptixRelease(optixDenoiserDestroy(state.denoiser));
    for (MaterialMap& map : state.textures) releaseMaterialMap(map);
    freeCuda(state.textureHandles);
    for (const State::Prototype& island : state.islands) { freeCuda(island.vertices); freeCuda(island.indices); freeCuda(island.gas); }
    freeCuda(state.denoiserState);
    freeCuda(state.denoiserScratch);
    freeCuda(state.intensity);
    freeCuda(state.params.color);
    freeCuda(state.params.illumination);
    freeCuda(state.params.albedo);
    freeCuda(state.params.normal);
    freeCuda(state.params.filtered);
    freeCuda(state.params.depth);
    freeCuda(state.params.output);
    freeCuda(state.params.badCount);
    freeCuda(state.deviceParams);
    freeCuda(const_cast<InstanceData*>(state.params.instances));
    freeCuda(state.instances);
    freeCuda(state.top);
    freeCuda(state.scratch);
    freeCuda(state.geometries);
    freeCuda(state.records);
    for (const State::Prototype& prototype : state.prototypes) { freeCuda(prototype.vertices); freeCuda(prototype.indices); freeCuda(prototype.gas); }
    if (state.pipeline) logOptixRelease(optixPipelineDestroy(state.pipeline));
    for (OptixProgramGroup group : state.groups) if (group) logOptixRelease(optixProgramGroupDestroy(group));
    if (state.module) logOptixRelease(optixModuleDestroy(state.module));
    if (state.context) logOptixRelease(optixDeviceContextDestroy(state.context));
    for (cudaEvent_t event : state.events) if (event) {
        const cudaError_t result = cudaEventDestroy(event);
        if (result != cudaSuccess && result != cudaErrorCudartUnloading) std::fprintf(stderr, "Rüya event release: %s\n", cudaGetErrorString(result));
    }
    if (state.stream) {
        const cudaError_t result = cudaStreamDestroy(state.stream);
        if (result != cudaSuccess && result != cudaErrorCudartUnloading) std::fprintf(stderr, "Rüya stream destroy: %s\n", cudaGetErrorString(result));
    }
    state_.reset();
}
}
