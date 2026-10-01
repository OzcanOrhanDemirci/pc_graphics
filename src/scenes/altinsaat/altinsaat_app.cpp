// Scene 1 "Altin saatte cam ve tas": the command-line tools (the live scene is altinsaat_live.cpp, in the application).
//
//   pc_graphics.exe [--scene altinsaat] --headless --spp N --out captures/name     N samples per pixel, PNG + EXR
//   pc_graphics.exe [--scene altinsaat] --headless --live N --out captures/name    N live frames: timings, flicker, frames
//   pc_graphics.exe [--scene altinsaat] --selftest       spectral tests (CMF, D65, Sellmeier, thin film, Newton colours)
//   options: --camera k, --width W, --height H, --set key=value, --view v, --furnace, --spp-per-launch k,
//            --denoise (also write _dn.png), --auto-ev target, --seed s, --compile-only
#include "scenes/altinsaat/altinsaat_app.h"
#include "core/gpu_stats.h"

#include <ctime>

static std::string timestamp() {
    std::time_t t = std::time(nullptr);
    char b[64];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", std::localtime(&t));
    return b;
}

// ---------------------------------------------------------------------------------------------------
static int runHeadless(AltinSaat& app, int spp, const std::string& out) {
    app.renderer.renderFrame();          // the first launch finishes OptiX's lazy compilation: keep it out of the timing
    app.renderer.resetAccumulation();
    const double t0 = nowSec();
    double renderMs = 0;
    while (int(app.renderer.samplesPerPixel()) < spp) {
        app.renderer.renderFrame();
        renderMs += app.renderer.times().render;
    }
    const double wall = nowSec() - t0;
    const unsigned int s = app.renderer.samplesPerPixel();
    {   // light diagnostics: sun radiance at the disk centre and the sky map's range
        const Params& p = app.renderer.params();
        std::vector<float> sun(SUN_TABLE_N), sky(size_t(p.skyW) * p.skyH * SKY_BINS);
        cudaMemcpy(sun.data(), p.sunTable, sun.size() * sizeof(float), cudaMemcpyDeviceToHost);
        cudaMemcpy(sky.data(), p.sky, sky.size() * sizeof(float), cudaMemcpyDeviceToHost);
        double mx = 0, mean = 0;
        for (float v : sky) { mx = std::max<double>(mx, v); mean += v; }
        double X = 0, Y = 0, Z = 0;
        for (int i = 0; i < SUN_TABLE_N; ++i) {
            const float3 c = cieXYZ(LAMBDA_MIN + i);
            X += sun[i] * c.x; Y += sun[i] * c.y; Z += sun[i] * c.z;
        }
        const double x = X / (X + Y + Z), y = Y / (X + Y + Z), n = (x - 0.3320) / (0.1858 - y);
        const double cct = 449.0 * n * n * n + 3525.0 * n * n + 6823.3 * n + 5520.33;   // McCamy
        std::printf("sun L(450/550/650) = %.4g %.4g %.4g   xy %.4f %.4f  CCT ~%.0f K   sky mean %.4g max %.4g   sunDir %.3f %.3f %.3f\n",
                    sun[90], sun[190], sun[290], x, y, cct, mean / sky.size(), mx, p.sunDir.x, p.sunDir.y, p.sunDir.z);
    }
    {
        const Params& p = app.renderer.params();
        const PassTimes& t = app.renderer.times();
        if (p.enablePhotons)
            std::printf("photons: %u stored of %u emitted per frame, %u beams (r %.4f m), %d emitters (%.3f m2), radius now %.4f m, trace %.2f ms, grid %.2f ms\n",
                        p.numPhotons, p.photonsToEmit, p.numBeams, p.beamRadius, p.numEmitters, p.emitAreaTotal, p.gatherRadius, t.photons, t.grid);
    }
    std::printf("rendered %u spp in %.2f s (gpu %.1f ms/launch, %.2f Mpaths/s), NaN/Inf: %u\n", s, wall,
                renderMs / app.renderer.frame(), double(app.width) * app.height * s / (renderMs * 1e-3) / 1e6, app.renderer.nanCount());
    if (app.autoEvTarget > 0.0f) {   // exposure that puts the median luminance at the target (scene-referred)
        std::vector<float4> acc;
        app.renderer.downloadAccum(acc);
        std::vector<float> Y;
        Y.reserve(acc.size());
        for (const float4& a : acc) if (a.w > 0) Y.push_back(a.y / a.w);
        std::nth_element(Y.begin(), Y.begin() + Y.size() / 2, Y.end());
        const float med = std::max(Y[Y.size() / 2], 1e-9f);
        app.ev = std::log2(app.autoEvTarget / med);
        app.renderer.params().exposure = std::pow(2.0f, app.ev);
        std::printf("auto exposure: median Y %.4g -> EV %.2f\n", med, app.ev);
    }
    app.saveCapture(out);
    if (app.headlessDenoise) {
        app.renderer.denoise(0.0f);
        app.renderer.tonemap();
        app.renderer.downloadOutput(app.pixels);
        writePNG(out + "_dn.png", int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(app.pixels.data()));
        std::printf("denoised %s_dn.png (%.1f ms)\n", out.c_str(), app.renderer.denoiseMs());
        app.renderer.clearDenoised();
    }
    return 0;
}

// Live mode without a window: frames at a fixed rate, per-pass timings, flicker = mean absolute difference of
// consecutive displayed frames (8-bit), and a few frames written for inspection.
static int runLiveHeadless(AltinSaat& app, int frames, float fps, const std::string& out) {
    app.setLive(true);
    std::vector<uchar4> prev, cur;
    double sumRender = 0, sumPhot = 0, sumDn = 0, sumTone = 0, sumWall = 0, sumFlicker = 0, sumFlickerStatic = 0;
    int counted = 0;
    std::filesystem::create_directories(std::filesystem::path(out).parent_path());
    for (int f = 0; f < frames; ++f) {
        const double t0 = nowSec();
        app.liveStep(float(f) / fps);
        app.renderer.params().ditherFrame = f;
        if (app.denoise) app.renderer.denoise(0.0f, app.liveDenoiseCaustics);
        app.renderer.tonemap();
        app.renderer.downloadOutput(cur);
        const double wall = nowSec() - t0;
        const PassTimes& t = app.renderer.times();
        if (f >= 10) {   // after the exponential average settles
            sumRender += t.render; sumPhot += t.photons + t.grid; sumDn += app.renderer.denoiseMs(); sumTone += t.tonemap; sumWall += wall;
            if (!prev.empty()) {
                double d = 0, ds = 0;
                size_t ns = 0;
                for (size_t i = 0; i < cur.size(); ++i) {
                    const double e = std::abs(int(cur[i].x) - int(prev[i].x)) + std::abs(int(cur[i].y) - int(prev[i].y)) + std::abs(int(cur[i].z) - int(prev[i].z));
                    d += e;
                    // a region with nothing moving: the upper middle of the frame (walls, above the dust beams)
                    const size_t x = i % app.width, y = i / app.width;
                    if (x > app.width / 2 && x < app.width * 7 / 8 && y > app.height / 12 && y < app.height / 3) { ds += e; ++ns; }
                }
                sumFlicker += d / (3.0 * cur.size());
                sumFlickerStatic += ns ? ds / (3.0 * ns) : 0.0;
            }
            ++counted;
        }
        if (f == 10 || f == frames / 2 || f == frames - 1) {
            char name[64];
            std::snprintf(name, sizeof(name), "_f%03d.png", f);
            writePNG(out + name, int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(cur.data()));
        }
        prev = cur;
    }
    if (counted > 1)
        std::printf("live %ux%u: frame %.1f ms wall (%.1f fps) = path %.1f + photons %.1f + denoise %.1f + tonemap %.2f ms; flicker %.3f / 255 (static region %.3f)\n",
                    app.width, app.height, 1000.0 * sumWall / counted, counted / sumWall, sumRender / counted, sumPhot / counted, sumDn / counted,
                    sumTone / counted, sumFlicker / (counted - 1), sumFlickerStatic / (counted - 1));
    return 0;
}

// ---------------------------------------------------------------------------------------------------
int runAltinSaatCli(int argc, char** argv) {
    AltinSaat app;
    bool headless = false, selftest = false, compileOnly = false, furnace = false;
    int spp = 64, view = 0, sppPerLaunch = 1;
    unsigned int seed = 0;
    int liveFrames = 0;
    std::string out;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--scene") next();
        else if (a == "--headless") headless = true;
        else if (a == "--selftest") selftest = true;
        else if (a == "--compile-only") compileOnly = true;
        else if (a == "--furnace") furnace = true;
        else if (a == "--auto-ev") app.autoEvTarget = std::stof(next());
        else if (a == "--seed") seed = unsigned(std::stoul(next()));
        else if (a == "--denoise") app.headlessDenoise = true;
        else if (a == "--live") liveFrames = std::stoi(next());
        else if (a == "--spp") spp = std::stoi(next());
        else if (a == "--spp-per-launch") sppPerLaunch = std::stoi(next());
        else if (a == "--out") out = next();
        else if (a == "--camera") app.camera = std::stoi(next());
        else if (a == "--width") app.width = unsigned(std::stoi(next()));
        else if (a == "--height") app.height = unsigned(std::stoi(next()));
        else if (a == "--view") view = std::stoi(next());
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else { std::printf("unknown option %s\n", a.c_str()); return 2; }
    }
    if (!headless && !selftest && !compileOnly) {
        std::printf("altinsaat: --headless, --selftest or --compile-only (the live scene opens from the application)\n");
        return 2;
    }
    if (selftest) {
        std::string report;
        const bool ok = spectralSelfTest(report);
        std::printf("%s%s\n", report.c_str(), ok ? "SELFTEST OK" : "SELFTEST HATA");
        return ok ? 0 : 1;
    }
    app.start = nowSec();
    app.loadConfig();
    app.ev = app.cfg.f("exposure.ev", 0.0f);
    app.renderer.init(app.width, app.height, app.root);
    std::printf("GPU: %s\n", app.renderer.deviceName().c_str());
    if (!app.compile()) return 1;
    if (compileOnly) return 0;
    gpuStatsInit();
    app.loadBlueNoise();
    app.applyConfig();
    Params& p = app.renderer.params();
    p.view = view;
    p.sppPerLaunch = unsigned(sppPerLaunch);
    p.seed = seed;
    p.furnace = furnace ? 1 : 0;
    app.rebuildScene();
    std::printf("scene: %zu triangles, %zu spheres, %zu materials, %zu portals\n", app.scene.indices.size(), app.scene.spheres.size(),
                app.scene.materials.size(), app.scene.portals.size());
    int rc = 0;
    if (headless && liveFrames > 0) rc = runLiveHeadless(app, liveFrames, 30.0f, out.empty() ? app.root + "/captures/live_" + timestamp() : out);
    else if (headless) rc = runHeadless(app, spp, out.empty() ? app.root + "/captures/headless_" + timestamp() : out);

    gpuStatsShutdown();
    app.renderer.shutdown();
    return rc;
}
