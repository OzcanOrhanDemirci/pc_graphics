// Scene 4 "Tanecik": command-line tools.
//   --scene tanecik --still [--look cam|deri] [--particles hepsi|seyrek|yok] [--spp N] [--yaw deg] [--pitch deg]
//                   [--dist m] [--target x,y,z] [--fov deg] [--width W --height H] [--out path] [--set key=value]
//       one picture of the figure in the studio, accumulated (no window)
//   --scene tanecik --stats      the body's numbers: particles, volume, mass, per bone, skin layer, the meshes
//   --scene tanecik --surface-test [ad]   the liquid's surface (tanecik_surface_test.cpp)
#include "scenes/tanecik/tanecik_app.h"
#include "core/image_io.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {

double tnow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

int stats(TanecikApp& app) {
    const double t0 = tnow();
    app.build();
    std::printf("beden kuruldu (%.2f s)\n", tnow() - t0);
    const BodyParticles& b = app.body;
    const float density = app.cfg.f("beden.yogunluk", 1000.0f);
    size_t skinN = 0;
    std::vector<size_t> perBone(B_COUNT, 0);
    for (size_t i = 0; i < b.pos.size(); ++i) {
        skinN += b.skin[i];
        ++perBone[b.bone[i]];
    }
    std::printf("aralik %.1f mm: %zu tanecik, hacim %.1f L, kutle %.1f kg (yogunluk %.0f), deri katmani %zu (%.0f%%)\n", b.spacing * 1000.0f,
                b.pos.size(), b.volume() * 1000.0f, b.volume() * density, density, skinN, 100.0 * skinN / std::max<size_t>(1, b.pos.size()));
    for (int k = 0; k < B_COUNT; ++k)
        std::printf("  %-12s %7zu  %.2f kg\n", boneInfo(k).name, perBone[k], perBone[k] * b.spacing * b.spacing * b.spacing * density);
    float top = 0.0f, lo = 1e9f;
    for (const float3& p : app.skin.pos) { top = std::max(top, p.y); lo = std::min(lo, p.y); }
    std::printf("deri: %zu kose, %zu ucgen, boy %.3f m (taban %.3f)\n", app.skin.pos.size(), app.skin.idx.size(), top, lo);
    std::printf("kemik: %zu kose, %zu ucgen\n", app.bones.pos.size(), app.bones.idx.size());
    return 0;
}

}   // namespace

int runSurfaceTests(TanecikApp& app, const std::string& only);   // tanecik_surface_test.cpp

int runTanecikCli(int argc, char** argv) {
    TanecikApp app;
    std::string surfaceTest;
    bool surface = false;
    std::string out, look = "cam", parts = "hepsi";
    bool still = false, doStats = false;
    unsigned spp = 512;
    std::vector<std::pair<std::string, std::string>> camSet;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--scene") next();
        else if (a == "--still") still = true;
        else if (a == "--stats") doStats = true;
        else if (a == "--surface-test") { surface = true; if (i + 1 < argc && argv[i + 1][0] != '-') surfaceTest = next(); }
        else if (a == "--look") look = next();
        else if (a == "--particles") parts = next();
        else if (a == "--spp") spp = unsigned(std::stoi(next()));
        else if (a == "--width") app.width = unsigned(std::stoi(next()));
        else if (a == "--height") app.height = unsigned(std::stoi(next()));
        else if (a == "--yaw") camSet.push_back({"kamera.yaw_deg", next()});
        else if (a == "--pitch") camSet.push_back({"kamera.pitch_deg", next()});
        else if (a == "--dist") camSet.push_back({"kamera.uzaklik", next()});
        else if (a == "--target") camSet.push_back({"kamera.hedef", next()});
        else if (a == "--fov") camSet.push_back({"kamera.fov", next()});
        else if (a == "--out") out = next();
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else { std::printf("unknown option %s\n", a.c_str()); return 2; }
    }
    app.loadConfig();
    for (const auto& kv : camSet) app.cfg.set(kv.first, kv.second);
    if (doStats) return stats(app);
    if (surface) return runSurfaceTests(app, surfaceTest);
    if (!still) {
        std::printf("tanecik: --still or --stats (the live scene opens from the application)\n");
        return 2;
    }
    app.particleView = parts == "seyrek" ? 1 : parts == "yok" ? 2 : 0;
    app.renderer.init(app.width, app.height, app.root);
    std::string log;
    double t0 = tnow();
    if (!app.renderer.compileDevice(log)) { std::printf("DERLEME HATASI\n%s\n", log.c_str()); return 1; }
    std::printf("tanecik programi derlendi (%.1f s)\n", tnow() - t0);
    t0 = tnow();
    app.build();
    app.upload();
    std::printf("beden ve sahne (%.2f s): %zu tanecik\n", tnow() - t0, app.body.pos.size());
    app.applyLook(look != "deri");
    app.setCamera();
    t0 = tnow();
    const unsigned batch = 16;
    for (unsigned s = 0; s < spp; s += batch) app.renderer.render(std::min(batch, spp - s), s > 0);
    std::printf("%u ornek (%.1f s)\n", spp, tnow() - t0);
    std::vector<uchar4> px;
    app.renderer.download(px);
    const std::string path = (out.empty() ? app.root + "/captures/tanecik_" + look : out) + ".png";
    writePNG(path, int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(px.data()));
    std::printf("yazildi: %s\n", path.c_str());
    app.renderer.shutdown();
    return 0;
}
