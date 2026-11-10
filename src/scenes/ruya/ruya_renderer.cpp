// Owns the OptiX pipeline and the frame-local optical instance table.
// Its buffers are wiped as soon as the ray launch has finished.
#include "scenes/ruya/ruya_renderer.h"
#include "scenes/ruya/ruya_island.h"
#include "scenes/ruya/ruya_material.h"
#include "scenes/ruya/ruya_streaming.h"
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
#include <sstream>
#include <thread>

#define RUYA_CUDA(call) do { const cudaError_t result_ = (call); if (result_ != cudaSuccess) { \
    std::fprintf(stderr, "CUDA %s:%d %s: %s\n", __FILE__, __LINE__, #call, cudaGetErrorString(result_)); std::exit(1); } } while (0)
#define RUYA_OPTIX(call) do { const OptixResult result_ = (call); if (result_ != OPTIX_SUCCESS) { \
    std::fprintf(stderr, "OptiX %s:%d %s: %s\n", __FILE__, __LINE__, #call, optixGetErrorString(result_)); std::exit(1); } } while (0)

namespace ruya {
namespace {
struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) Record { char header[OPTIX_SBT_RECORD_HEADER_SIZE]; };
static_assert(sizeof(Record) == OPTIX_SBT_RECORD_HEADER_SIZE);
struct Image { int width = 0, height = 0; unsigned char* pixels = nullptr; };
// Fetched maps are JPG when the source offers one, PNG otherwise.
Image loadImage(const std::string& path, int components) {
    Image image;
    int channels = 0;
    for (const char* extension : {".jpg", ".png"}) {
        image.pixels = stbi_load((path + extension).c_str(), &image.width, &image.height, &channels, components);
        if (image.pixels) break;
    }
    return image;
}
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
// Island meshes resident around the camera peaked at 1.44 GiB in 36 m/s flights; the pool
// starts above that so that flight never waits for device memory to grow.
constexpr size_t kIslandPoolReserve = size_t(2048) << 20;
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
    cudaMemPool_t islandPool = nullptr;
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
    // A cell joins as soon as a worker has built it; its islands are uploaded one by one
    // and the cell is drawn once all of them are on the GPU. It stays while it is in the
    // ahead set, and a short grace period after, so a cell at the boundary is not rebuilt.
    struct ResidentCell {
        std::shared_ptr<const CellPlan> plan;
        std::vector<Mesh> pending;
        std::vector<unsigned> slots;
        unsigned lastFrame = 0;
        bool complete() const { return pending.empty() || slots.size() == pending.size(); }
    };
    std::vector<Prototype> islands;
    std::vector<unsigned> freeIslands;
    std::map<std::pair<int, int>, ResidentCell> residentCells;
    std::unique_ptr<IslandBuilder> builder;
    // Measured upload cost, milliseconds per triangle, for the frame budget.
    double uploadRate = 1e-5;
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

    // Pooled memory comes from the island pool in stream order; the rest from cudaMalloc.
    void allocate(void** pointer, size_t bytes, bool pooled = false) {
        if (!bytes) return;
        size_t freeBytes = 0;
        size_t totalBytes = 0;
        RUYA_CUDA(cudaMemGetInfo(&freeBytes, &totalBytes));
        if (bytes + 1024ULL * 1024ULL * 1024ULL > freeBytes) {
            std::fprintf(stderr, "Rüya GPU memory margin exhausted: requested %zu, free %zu\n", bytes, freeBytes);
            std::exit(1);
        }
        if (pooled) RUYA_CUDA(cudaMallocFromPoolAsync(pointer, bytes, islandPool, stream));
        else RUYA_CUDA(cudaMalloc(pointer, bytes));
        if (reinterpret_cast<uintptr_t>(*pointer) % OPTIX_ACCEL_BUFFER_BYTE_ALIGNMENT) {
            std::fprintf(stderr, "Rüya GPU allocation is not aligned for OptiX\n");
            std::exit(1);
        }
        allocatedBytes += bytes;
    }
    void freeDevice(void* pointer, bool pooled) {
        if (!pooled) { freeCuda(pointer); return; }
        if (!pointer) return;
        const cudaError_t result = cudaFreeAsync(pointer, stream);
        if (result != cudaSuccess && result != cudaErrorCudartUnloading)
            std::fprintf(stderr, "Rüya CUDA pool release: %s\n", cudaGetErrorString(result));
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
    void uploadPrototype(const Mesh& mesh, Prototype& prototype, bool pooled = false) {
        allocate(reinterpret_cast<void**>(&prototype.vertices), mesh.vertices.size() * sizeof(Vertex), pooled);
        allocate(reinterpret_cast<void**>(&prototype.indices), mesh.indices.size() * sizeof(uint3), pooled);
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
        allocate(&temporary, sizes.tempSizeInBytes, pooled);
        allocate(&output, sizes.outputSizeInBytes, pooled);
        allocate(reinterpret_cast<void**>(&compactedSize), sizeof(size_t), pooled);
        OptixAccelEmitDesc emit{};
        emit.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
        emit.result = CUdeviceptr(compactedSize);
        RUYA_OPTIX(optixAccelBuild(context, stream, &options, &input, 1, CUdeviceptr(temporary), sizes.tempSizeInBytes,
                                  CUdeviceptr(output), sizes.outputSizeInBytes, &prototype.handle, &emit, 1));
        size_t compactedBytes = 0;
        RUYA_CUDA(cudaMemcpyAsync(&compactedBytes, compactedSize, sizeof(size_t), cudaMemcpyDeviceToHost, stream));
        RUYA_CUDA(cudaStreamSynchronize(stream));
        freeDevice(temporary, pooled);
        allocatedBytes -= sizes.tempSizeInBytes;
        freeDevice(compactedSize, pooled);
        allocatedBytes -= sizeof(size_t);
        if (compactedBytes < sizes.outputSizeInBytes && compactedBytes > 0) {
            allocate(&prototype.gas, compactedBytes, pooled);
            RUYA_OPTIX(optixAccelCompact(context, stream, prototype.handle, CUdeviceptr(prototype.gas), compactedBytes, &prototype.handle));
            RUYA_CUDA(cudaStreamSynchronize(stream));
            freeDevice(output, pooled);
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
    // Sets may differ in resolution; the maps of one set must match. Two sets are in hand at
    // a time: they decode side by side, and the set after them starts once the first is
    // uploaded and freed, so host memory stays near two 8K sets instead of the whole library.
    bool loadTextures(const std::string& root, std::string& error) {
        // Order follows the kSet* indices in ruya_shared.h; keep in step with assets/ruya/fetch.py.
        static const char* const names[kTextureSets] = {
            "rock_face_03", "aerial_grass_rock", "large_sandstone_blocks_01", "cliff_side", "leafy_grass", "pine_bark",
            "clay_roof_tiles", "weathered_planks", "rust_coarse_01", "tree_small_02_leaves", "fir_tree_01_twig"};
        struct Decoded { Image color, normal, arm, extra; };
        const auto isLeaves = [](int set) { return set == kSetBroadleaf || set == kSetNeedle; };
        const auto decodeSet = [&](int set) {
            const std::string base = root + "/assets/ruya/cc0/" + names[set] + "_";
            const char* extra = isLeaves(set) ? "alpha" : "disp";
            return std::async(std::launch::async, [base, extra] {
                const auto decode = [&](const char* suffix, int components) {
                    return std::async(std::launch::async, [path = base + suffix, components] { return loadImage(path, components); });
                };
                auto colorJob = decode("diff", 4);
                auto normalJob = decode("nor_gl", 4);
                auto armJob = decode("arm", 4);
                auto extraJob = decode(extra, 1);
                return Decoded{colorJob.get(), normalJob.get(), armJob.get(), extraJob.get()};
            });
        };
        const auto freeImage = [](Image& image) { stbi_image_free(image.pixels); image.pixels = nullptr; };
        const double started = milliseconds();
        constexpr int kSetsInHand = 2;
        std::array<size_t, kTextureSets> decodedBytes{};
        std::array<std::future<Decoded>, kTextureSets> pending;
        for (int set = 0; set < kSetsInHand; ++set) pending[set] = decodeSet(set);
        for (int set = 0; set < kTextureSets; ++set) {
            Decoded decoded = pending[set].get();
            Image& color = decoded.color;
            Image& normal = decoded.normal;
            Image& arm = decoded.arm;
            Image& extra = decoded.extra;
            const int side = color.width;
            const bool valid = color.pixels && normal.pixels && arm.pixels && extra.pixels && color.height == side &&
                               normal.width == side && normal.height == side && arm.width == side && arm.height == side &&
                               extra.width == side && extra.height == side && (side & (side - 1)) == 0;
            bool ok = valid;
            if (ok) {
                const size_t count = size_t(side) * side;
                decodedBytes[set] = count * (4 + 4 + 4 + 1);
                std::vector<unsigned char> packed(count * 4);
                // Colour with roughness in alpha; normal xy with occlusion in z, height or alpha cut in w.
                for (size_t i = 0; i < count; ++i) {
                    packed[i * 4] = color.pixels[i * 4];
                    packed[i * 4 + 1] = color.pixels[i * 4 + 1];
                    packed[i * 4 + 2] = color.pixels[i * 4 + 2];
                    packed[i * 4 + 3] = arm.pixels[i * 4 + 1];
                }
                ok = uploadMaterialMap(textures[set * 2], packed.data(), side, true, stream) == cudaSuccess;
                for (size_t i = 0; i < count; ++i) {
                    packed[i * 4] = normal.pixels[i * 4];
                    packed[i * 4 + 1] = normal.pixels[i * 4 + 1];
                    packed[i * 4 + 2] = arm.pixels[i * 4];
                    packed[i * 4 + 3] = extra.pixels[i];
                }
                ok = ok && uploadMaterialMap(textures[set * 2 + 1], packed.data(), side, false, stream) == cudaSuccess;
                allocatedBytes += 2 * count * 4 * 4 / 3;
                params.textureSide[set] = float(side);
            }
            for (Image* image : {&color, &normal, &arm, &extra}) freeImage(*image);
            if (!ok) {
                for (int later = set + 1; later < std::min(set + kSetsInHand, kTextureSets); ++later) {
                    Decoded unused = pending[later].get();
                    for (Image* image : {&unused.color, &unused.normal, &unused.arm, &unused.extra}) freeImage(*image);
                }
                error = std::string("Rüya texture set missing or unreadable: ") + names[set] + "; run python assets/ruya/fetch.py";
                return false;
            }
            if (set + kSetsInHand < kTextureSets) pending[set + kSetsInHand] = decodeSet(set + kSetsInHand);
        }
        size_t decodedPeak = 0;
        for (int set = 0; set + 1 < kTextureSets; ++set) decodedPeak = std::max(decodedPeak, decodedBytes[set] + decodedBytes[set + 1]);
        std::printf("Ruya dokular: %.1f s, ayni anda cozulen iki set en cok %.0f MiB\n", (milliseconds() - started) / 1000.0,
                    decodedPeak / 1048576.0);
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
        freeDevice(island.vertices, true);
        freeDevice(island.indices, true);
        freeDevice(island.gas, true);
        allocatedBytes -= island.bytes;
        island = {};
        freeIslands.push_back(slot);
    }
    void uploadIsland(ResidentCell& cell) {
        if (freeIslands.empty()) {
            if (islands.size() >= kIslandSlots) { std::fprintf(stderr, "Rüya island slots exhausted\n"); std::exit(1); }
            freeIslands.push_back(unsigned(islands.size()));
            islands.emplace_back();
        }
        const unsigned slot = freeIslands.back();
        freeIslands.pop_back();
        const Mesh& mesh = cell.pending[cell.slots.size()];
        const double begin = milliseconds();
        const size_t before = allocatedBytes;
        uploadPrototype(mesh, islands[slot], true);
        islands[slot].bytes = allocatedBytes - before;
        const Geometry geometry{islands[slot].vertices, islands[slot].indices};
        RUYA_CUDA(cudaMemcpyAsync(geometries + kPrototypeCount + slot, &geometry, sizeof(Geometry), cudaMemcpyHostToDevice, stream));
        RUYA_CUDA(cudaStreamSynchronize(stream));
        cell.slots.push_back(slot);
        uploadRate = 0.8 * uploadRate + 0.2 * (milliseconds() - begin) / double(std::max<size_t>(mesh.indices.size(), 1));
        if (cell.slots.size() == cell.pending.size()) {
            cell.pending.clear();
            cell.pending.shrink_to_fit();
            ++stats.streamedCells;
        }
    }
    void admit(IslandBuilder::Result& result) {
        ResidentCell& cell = residentCells[{result.cell.x, result.cell.z}];
        cell.plan = std::move(result.plan);
        cell.pending = std::move(result.meshes);
        cell.lastFrame = frame;
    }
    // Makes every required cell resident, waiting for it if it is not built yet, then
    // uploads islands built ahead, nearest first, within the frame budget.
    void streamCells(const std::vector<Cell>& required, const std::vector<Cell>& ahead) {
        stats.streamedCells = 0;
        stats.waitedCells = 0;
        std::vector<Cell> wanted;
        for (const std::vector<Cell>* list : {&required, &ahead})
            for (const Cell& cell : *list) {
                const auto found = residentCells.find({cell.x, cell.z});
                if (found != residentCells.end()) found->second.lastFrame = frame;
            }
        for (const Cell& cell : ahead)
            if (!residentCells.count({cell.x, cell.z})) wanted.push_back(cell);
        builder->request(wanted);
        for (IslandBuilder::Result& result : builder->collect()) admit(result);
        for (const Cell& cell : required) {
            auto found = residentCells.find({cell.x, cell.z});
            if (found == residentCells.end()) {
                IslandBuilder::Result result = builder->wait(cell);
                admit(result);
                found = residentCells.find({cell.x, cell.z});
                ++stats.waitedCells;
            }
            while (!found->second.complete()) uploadIsland(found->second);
        }
        // A frame that had to wait (the first one, or a jump to a view) has already
        // stalled, so it settles the whole ahead ring and later frames start warm.
        if (stats.waitedCells > 0)
            for (const Cell& cell : ahead) {
                auto found = residentCells.find({cell.x, cell.z});
                if (found == residentCells.end()) {
                    IslandBuilder::Result result = builder->wait(cell);
                    admit(result);
                    found = residentCells.find({cell.x, cell.z});
                }
                while (!found->second.complete()) uploadIsland(found->second);
            }
        // An island is started only while its predicted cost fits the budget, except
        // the first of a frame, so one frame pays for at most one large island.
        const double start = milliseconds();
        for (const Cell& cell : ahead) {
            const auto found = residentCells.find({cell.x, cell.z});
            if (found == residentCells.end()) continue;
            ResidentCell& resident = found->second;
            bool spent = false;
            while (!resident.complete()) {
                const double used = milliseconds() - start;
                const double cost = uploadRate * double(resident.pending[resident.slots.size()].indices.size());
                if (used > 0.05 && used + cost > kStreamUploadMs) { spent = true; break; }
                uploadIsland(resident);
            }
            if (spent) break;
        }
        for (auto it = residentCells.begin(); it != residentCells.end();) {
            if (frame - it->second.lastFrame > kIslandGraceFrames) {
                for (unsigned slot : it->second.slots) releaseIsland(slot);
                it = residentCells.erase(it);
            } else ++it;
        }
        stats.residentCells = unsigned(residentCells.size());
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
    {
        // Growing device memory can stall a cudaMalloc for tens of milliseconds, so island
        // meshes come from a pool that is grown once here and keeps what it is given back.
        cudaMemPoolProps pool{};
        pool.allocType = cudaMemAllocationTypePinned;
        pool.location.type = cudaMemLocationTypeDevice;
        pool.location.id = 0;
        RUYA_CUDA(cudaMemPoolCreate(&state.islandPool, &pool));
        cuuint64_t keep = ~cuuint64_t(0);
        RUYA_CUDA(cudaMemPoolSetAttribute(state.islandPool, cudaMemPoolAttrReleaseThreshold, &keep));
        void* reserve = nullptr;
        RUYA_CUDA(cudaMallocFromPoolAsync(&reserve, kIslandPoolReserve, state.islandPool, state.stream));
        RUYA_CUDA(cudaFreeAsync(reserve, state.stream));
        RUYA_CUDA(cudaStreamSynchronize(state.stream));
    }
    for (cudaEvent_t& event : state.events) RUYA_CUDA(cudaEventCreate(&event));
    RUYA_OPTIX(optixInit());
    OptixDeviceContextOptions contextOptions{};
    contextOptions.logCallbackFunction = logOptix;
    contextOptions.logCallbackLevel = 3;
    RUYA_OPTIX(optixDeviceContextCreate(nullptr, &contextOptions, &state.context));
    state.params.width = unsigned(width);
    state.params.height = unsigned(height);
    state.params.layout = sizeof(Params);
    state.params.ambientReach = ShadingOptions{}.ambientReach;
    state.params.perSampleShading = 0;
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
    state.builder = std::make_unique<IslandBuilder>(std::clamp(std::thread::hardware_concurrency() / 3, 2U, 8U));
    RUYA_CUDA(cudaStreamSynchronize(state.stream));
    state.stats.residentBytes = state.allocatedBytes;
    error.clear();
    return true;
}

void Renderer::setCells(const std::vector<Cell>& visible, const std::vector<Cell>& transport, const std::vector<Cell>& ahead) {
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
    state.streamCells(cells, ahead.empty() ? cells : ahead);
    auto nextVisible = visible.begin();
    for (const Cell& cell : cells) {
        const State::ResidentCell& resident = state.residentCells.at({cell.x, cell.z});
        const CellPlan& plan = *resident.plan;
        while (nextVisible != visible.end() && cellLess(*nextVisible, cell)) ++nextVisible;
        const bool inView = nextVisible != visible.end() && nextVisible->x == cell.x && nextVisible->z == cell.z;
        for (int i = 0; i < plan.islandCount; ++i) {
            const unsigned slot = resident.slots[size_t(i)];
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

void Renderer::setShading(const ShadingOptions& options) {
    if (!state_) return;
    state_->params.ambientReach = options.ambientReach;
    state_->params.perSampleShading = options.perSampleShading ? 1 : 0;
}

void Renderer::render(const Camera& camera, int spp, int bounces, int sampleOffset, bool denoise, float exposure, int materialDetail, bool reference) {
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
    state.params.reference = reference ? 1 : 0;
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
    state.builder.reset();
    if (state.stream) {
        const cudaError_t result = cudaStreamSynchronize(state.stream);
        if (result != cudaSuccess && result != cudaErrorCudartUnloading) std::fprintf(stderr, "Rüya stream release: %s\n", cudaGetErrorString(result));
    }
    if (state.denoiser) logOptixRelease(optixDenoiserDestroy(state.denoiser));
    for (MaterialMap& map : state.textures) releaseMaterialMap(map);
    freeCuda(state.textureHandles);
    for (const State::Prototype& island : state.islands) {
        state.freeDevice(island.vertices, true);
        state.freeDevice(island.indices, true);
        state.freeDevice(island.gas, true);
    }
    if (state.stream) {
        const cudaError_t result = cudaStreamSynchronize(state.stream);
        if (result != cudaSuccess && result != cudaErrorCudartUnloading) std::fprintf(stderr, "Rüya pool release: %s\n", cudaGetErrorString(result));
    }
    if (state.islandPool) {
        const cudaError_t result = cudaMemPoolDestroy(state.islandPool);
        if (result != cudaSuccess && result != cudaErrorCudartUnloading) std::fprintf(stderr, "Rüya pool destroy: %s\n", cudaGetErrorString(result));
    }
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
