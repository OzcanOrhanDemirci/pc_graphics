#include "scenes/ruya/ruya_app.h"
#include "core/image_io.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

namespace ruya {
int testLive(const std::string& root);
namespace {
template<class T> bool parseNumber(const std::string& text, T& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}
bool ensureParent(const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) return true;
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) std::fprintf(stderr, "Klasor olusturulamadi: %s: %s\n", path.c_str(), error.message().c_str());
    return !error;
}
bool sameCell(const Cell& a, const Cell& b) { return a.x == b.x && a.z == b.z; }
}

bool Run::init(const std::string& root, const std::vector<std::string>& overrides, std::string& error) {
    Config config;
    if (!config.load(root + "/scenes/ruya.cfg")) {
        error = "Ruya ayar dosyasi okunamadi";
        return false;
    }
    for (const auto& item : overrides) {
        const auto equal = item.find('=');
        if (equal == std::string::npos) {
            error = "Ayar anahtar=deger biciminde olmali: " + item;
            return false;
        }
        config.set(item.substr(0, equal), item.substr(equal + 1));
    }
    for (auto setting : {std::pair{"width", &width}, {"height", &height}, {"spp", &spp},
                         {"bounces", &bounces}, {"detail", &detail}}) {
        const auto entry = config.values.find(setting.first);
        if (entry != config.values.end() && !parseNumber(entry->second, *setting.second)) {
            error = "Gecersiz sayisal ayar: " + entry->first;
            return false;
        }
    }
    if (width < 64 || width > 7680 || height < 64 || height > 4320 || spp < 1 || spp > 4096 ||
        bounces < 1 || bounces > 12 || detail < 0 || detail > 3) {
        error = "Ruya ayarlari izin verilen araligin disinda";
        return false;
    }
    auto prototypes = buildPrototypes(detail);
    geometryTriangles = 0;
    for (const auto& mesh : prototypes) geometryTriangles += mesh.indices.size();
    std::printf("Ruya: %zu uretim prototipi, %llu benzersiz ucgen, %dx%d, %d spp, %d sekme\n",
                prototypes.size(), geometryTriangles, width, height, spp, bounces);
    if (!renderer.init(width, height, root, prototypes, error)) return false;
    camera = view(0);
    return true;
}

void Run::render() {
    const auto start = std::chrono::steady_clock::now();
    auto selected = visibleCells(camera, float(width) / height, kFarDistance, fullWorld);
    added = 0;
    evicted = 0;
    for (const auto& cell : selected)
        if (std::none_of(cells.begin(), cells.end(), [&](const Cell& previous) { return sameCell(previous, cell); })) ++added;
    for (const auto& cell : cells)
        if (std::none_of(selected.begin(), selected.end(), [&](const Cell& next) { return sameCell(next, cell); })) ++evicted;
    cells = std::move(selected);
    const auto transport = offscreen && !fullWorld ? visibleCells(camera, float(width) / height, kFarDistance, true)
                                                  : std::vector<Cell>{};
    renderer.setCells(cells, transport);
    renderer.render(camera, spp, bounces, sampleOffset, denoise, exposure, materialDetail);
    frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

bool Run::save(const std::string& prefix) const {
    if (!ensureParent(prefix)) return false;
    std::vector<unsigned char> rgba;
    renderer.download(rgba);
    const auto linear = renderer.hdr();
    return writePNG(prefix + ".png", width, height, rgba.data()) &&
           writeEXR(prefix + ".exr", width, height, 4, reinterpret_cast<const float*>(linear.data()));
}

void Run::release() {
    cells.clear();
    cells.shrink_to_fit();
    renderer.release();
}

Camera Run::view(int index) {
    Camera camera;
    switch (index) {
    case 1: camera.eye = make_float3(18, 9, 28); camera.yaw = -0.45f; camera.pitch = 0.12f; break;
    case 2: camera.eye = make_float3(-26, 4, -12); camera.yaw = 1.35f; camera.pitch = 0.08f; break;
    case 3: camera.eye = make_float3(14, -35, 65); camera.yaw = -0.15f; camera.pitch = 0.3f; break;
    case 4: camera.eye = make_float3(84, 50, 142); camera.yaw = -0.38f; camera.pitch = -0.22f; break;
    case 5: camera.eye = make_float3(-80, 14, 45); camera.yaw = 0.7f; camera.pitch = 0.08f; break;
    default: break;
    }
    return camera;
}

int cli(int argc, char** argv) {
    Run run;
    std::string root = PROJECT_ROOT;
    std::string out;
    std::string csv;
    std::string dumpDirectory;
    std::vector<std::string> overrides;
    int frames = 1;
    int warmup = 3;
    int view = 0;
    bool route = false;
    bool lifecycle = false;
    bool test = false;
    bool liveTest = false;
    bool customYaw = false;
    bool customPitch = false;
    float yaw = 0, pitch = 0;
    auto failUsage = [](const std::string& message) {
        std::fprintf(stderr, "Ruya: %s\n", message.c_str());
        return 2;
    };
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        auto value = [&]() { return index + 1 < argc ? std::string(argv[++index]) : std::string(); };
        auto number = [&](auto& target) { return parseNumber(value(), target); };
        if (option == "--scene") { if (value() != "ruya") return failUsage("Yanlis sahne"); }
        else if (option == "--headless") {}
        else if (option == "--root") root = value();
        else if (option == "--set") overrides.push_back(value());
        else if (option == "--out") out = value();
        else if (option == "--csv") csv = value();
        else if (option == "--dump-dir") dumpDirectory = value();
        else if (option == "--frames") { if (!number(frames)) return failUsage("Gecersiz kare sayisi"); }
        else if (option == "--warmup") { if (!number(warmup)) return failUsage("Gecersiz isinma sayisi"); }
        else if (option == "--view") { if (!number(view)) return failUsage("Gecersiz bakis"); }
        else if (option == "--sample-offset") { if (!number(run.sampleOffset)) return failUsage("Gecersiz ornek ofseti"); }
        else if (option == "--exposure") { if (!number(run.exposure) || !std::isfinite(run.exposure)) return failUsage("Gecersiz pozlama"); }
        else if (option == "--yaw") { customYaw = true; if (!number(yaw) || !std::isfinite(yaw)) return failUsage("Gecersiz yaw"); }
        else if (option == "--pitch") { customPitch = true; if (!number(pitch) || !std::isfinite(pitch)) return failUsage("Gecersiz pitch"); }
        else if (option == "--width" || option == "--height" || option == "--spp" || option == "--bounces" || option == "--detail") {
            int parsed = 0;
            const auto text = value();
            if (!parseNumber(text, parsed)) return failUsage("Gecersiz deger: " + option);
            overrides.push_back(option.substr(2) + "=" + text);
        }
        else if (option == "--raw") run.denoise = false;
        else if (option == "--no-offscreen" || option == "--no-proxy") run.offscreen = false;
        else if (option == "--flat-material") run.materialDetail = 0;
        else if (option == "--full-world") run.fullWorld = true;
        else if (option == "--route") route = true;
        else if (option == "--lifecycle") lifecycle = true;
        else if (option == "--test") test = true;
        else if (option == "--test-live") liveTest = true;
        else if (option == "--lang") value();
        else if (option == "--help") {
            std::puts("Ruya: --headless [--width 2560 --height 1440 --spp 3 --bounces 3 --detail 2]\n"
                      "  --frames N --warmup N --view 0..5 --out UZANTISIZ_YOL --csv YOL\n"
                      "  --route --raw --sample-offset N --no-offscreen --full-world --flat-material\n"
                      "  --yaw RAD --pitch RAD --exposure EV --lifecycle --test --test-live --dump-dir DOSYA\n"
                      "  --detail 0 en dusuk yogunluktur (1 ile ayni)");
            return 0;
        }
        else return failUsage("Bilinmeyen secenek: " + option);
    }
    if (test) return testWorld();
    if (liveTest) return testLive(root);
    if (frames < 1 || frames > 100000 || warmup < 0 || warmup > 1000 || view < 0 || view > 5 || run.sampleOffset < 0)
        return failUsage("Parametre izin verilen araligin disinda");
    std::string error;
    if (!run.init(root, overrides, error)) {
        std::fprintf(stderr, "Ruya baslatilamadi: %s\n", error.c_str());
        return 1;
    }
    run.camera = Run::view(view);
    if (customYaw) run.camera.yaw = yaw;
    if (customPitch) run.camera.pitch = std::clamp(pitch, -1.45f, 1.45f);
    std::ofstream log;
    if (!csv.empty()) {
        if (!ensureParent(csv)) return 1;
        log.open(csv);
        if (!log) return failUsage("CSV acilamadi: " + csv);
        log << "view,frame,gpu_trace_ms,gpu_denoise_ms,gpu_display_ms,upload_ms,frame_ms,cells,triangles,resident_bytes,free_bytes,bad_samples,added,evicted,x,y,z,yaw,pitch,width,height,spp,bounces,transport_cells,optical_pool_bytes\n";
    }
    for (int index = -warmup; index < frames; ++index) {
        if (route && index >= 0) {
            const float mouse = index < frames / 2 ? 50.0f : -50.0f;
            moveCamera(run.camera, mouse, 4.0f * std::sin(float(index) * 0.017f), 6.0f, 2.0f, 0.0f, 1.0f / 30.0f);
        }
        run.render();
        const auto& stats = run.renderer.stats();
        if (stats.badSamples != 0) {
            std::fprintf(stderr, "Ruya: %u sonlu olmayan ornek, kare %d\n", stats.badSamples, index);
            return 1;
        }
        if (index < 0) continue;
        if (log.is_open()) {
            log << view << ',' << index << ',' << stats.traceMs << ',' << stats.denoiseMs << ',' << stats.displayMs << ','
                << stats.uploadMs << ',' << run.frameMs << ',' << stats.cells << ',' << stats.instancedTriangles << ','
                << stats.residentBytes << ',' << stats.freeBytes << ',' << stats.badSamples << ',' << run.added << ',' << run.evicted << ','
                << run.camera.eye.x << ',' << run.camera.eye.y << ',' << run.camera.eye.z << ',' << run.camera.yaw << ',' << run.camera.pitch << ','
                << run.width << ',' << run.height << ',' << run.spp << ',' << run.bounces << ',' << stats.transportCells << ',' << stats.opticalPoolBytes << '\n';
            if (!log) { std::fprintf(stderr, "CSV yazma hatasi\n"); return 1; }
        }
        if (index == frames - 1)
            std::printf("Ruya kare: %.3f ms; GPU %.3f + %.3f + %.3f ms; %u hucre; %llu orneklenmis ucgen; %.1f MiB tahsisli; %u hatali ornek\n",
                        run.frameMs, stats.traceMs, stats.denoiseMs, stats.displayMs, stats.cells,
                        stats.instancedTriangles, stats.residentBytes / 1048576.0, stats.badSamples);
        if (!dumpDirectory.empty()) {
            char name[32];
            std::snprintf(name, sizeof(name), "/frame_%04d", index);
            if (!run.save(dumpDirectory + name)) { std::fprintf(stderr, "Dizi karesi yazilamadi\n"); return 1; }
        }
    }
    if (!out.empty() && !run.save(out)) { std::fprintf(stderr, "Ruya kare kaydedilemedi\n"); return 1; }
    if (lifecycle) {
        const auto before = run.renderer.stats();
        const auto original = run.renderer.hdr();
        if (!run.renderer.opticalPoolIsClear()) { std::fprintf(stderr, "Isik verisi kare sonunda kaldi\n"); return 1; }
        for (int cycle = 0; cycle < 20; ++cycle) {
            run.renderer.setCells({});
            if (run.renderer.stats().cells != 0) { std::fprintf(stderr, "Bosaltma basarisiz\n"); return 1; }
            if (cycle == 0) {
                run.renderer.render(run.camera, run.spp, run.bounces, run.sampleOffset, run.denoise, run.exposure, run.materialDetail);
                const auto empty = run.renderer.depth();
                if (std::any_of(empty.begin(), empty.end(), [](float4 hit) { return hit.w >= 0; })) {
                    std::fprintf(stderr, "Bos dunyada kalan geometri var\n"); return 1;
                }
            }
            run.renderer.setCells(run.cells);
            if (run.renderer.stats().cells != before.cells) { std::fprintf(stderr, "Yeniden uretme basarisiz\n"); return 1; }
            if (cycle == 0 && run.renderer.opticalPoolIsClear()) {
                std::fprintf(stderr, "Optik tampon denetimi dolu tamponu algilamadi\n"); return 1;
            }
            run.render();
            const auto regenerated = run.renderer.hdr();
            if (regenerated.size() != original.size() ||
                std::memcmp(original.data(), regenerated.data(), original.size() * sizeof(float4)) != 0 ||
                run.renderer.stats().residentBytes != before.residentBytes || !run.renderer.opticalPoolIsClear()) {
                std::fprintf(stderr, "Dongu %d: yeniden uretim veya bellek ayni degil\n", cycle); return 1;
            }
        }
        std::puts("Ruya: 20 bosaltma/yeniden uretme dongusu; HDR baytlari ve GPU tahsisi ayni; optik tamponlar sifir");
    }
    run.release();
    return 0;
}
}
