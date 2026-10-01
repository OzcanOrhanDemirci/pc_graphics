// Scene 1 "Altin saatte cam ve tas": the state of a run (the spectral path tracer, its settings, the room, the camera,
// still and live mode). Shared by the live scene in the application (altinsaat_live.cpp) and the command-line tools
// (altinsaat_app.cpp).
#pragma once
#include "core/image_io.h"
#include "scenes/altinsaat/altinsaat_renderer.h"
#include "scenes/altinsaat/altinsaat_world.h"
#include "shared/spectrum.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

inline double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct AltinSaat {
    Renderer renderer;
    Config cfg;
    Scene scene;
    std::string root = PROJECT_ROOT;
    unsigned int width = 2560, height = 1440;
    int camera = 0;
    float ev = 0.0f;
    bool hud = true, live = false;
    std::vector<std::string> overrides;
    std::vector<uchar4> pixels;
    double start = 0;
    unsigned int photonsDefault = 0;
    float autoEvTarget = 0.0f;
    bool headlessDenoise = false;
    bool denoise = true;
    bool liveDenoiseCaustics = true;
    unsigned int denoiseUntil = 8192;

    void loadConfig() {
        cfg = Config();
        cfg.load(root + "/scenes/altinsaat.cfg");
        for (const auto& o : overrides) {
            const size_t eq = o.find('=');
            if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
        }
    }
    void applyConfig() {
        Params& p = renderer.params();
        ev = cfg.f("exposure.ev", ev);
        p.exposure = std::pow(2.0f, ev);
        p.hazeSigmaS = cfg.f("haze.sigma_s", 0.02f);
        p.hazeSigmaA = cfg.f("haze.sigma_a", 0.002f);
        p.hazeG = cfg.f("haze.g", 0.65f);
        p.hazeBeamFraction = cfg.f("haze.beam_fraction", 0.75f);
        p.hazeAllSegments = 1;
        p.dispersionBoost = unsigned(cfg.i("path.spectral_split", 1) * 4);
        p.maxDepth = cfg.i("path.max_depth", 12);
        p.enableSunPatch = cfg.i("path.sun_patch", 0);
        p.waterDepthTint = cfg.f("water.tint", 0.05f);
        p.groundAlbedo = cfg.f("ground.albedo", 0.18f);
        p.skyScale = cfg.f("sky.scale", 1.0f);
        p.sunScale = cfg.f("sun.scale", 1.0f);
        {   // the sun's angular radius (validation tests enlarge it so path tracing can find caustics)
            const float r = cfg.f("sun.radius_deg", 0.2666f) * 3.14159265f / 180.0f;
            p.sunOneMinusCosMax = 2.0f * std::sin(0.5f * r) * std::sin(0.5f * r);
            p.sunCosMax = 1.0f - p.sunOneMinusCosMax;
        }
        p.skyMapFraction = cfg.f("sky.map_fraction", 0.0f);
        p.debugFlags = cfg.i("debug.flags", 0);
        renderer.bloomStrength = cfg.f("bloom.strength", 0.04f);
        p.denoiseClampY = cfg.f("denoise.clamp", 0.5f);
        p.enablePhotons = cfg.i("photon.enable", 1);
        denoise = cfg.i("denoise.enable", 1) != 0;
        liveDenoiseCaustics = cfg.i("live.denoise_caustics", 0) != 0;
        denoiseUntil = unsigned(cfg.f("denoise.until", 8192.0f));
        p.gatherDepth = cfg.i("photon.gather_depth", 2);
        renderer.photonRadius0 = cfg.f("photon.radius", 0.01f);
        renderer.photonsPerFrame = unsigned(cfg.f("photon.count", 1048576.0f));
        renderer.beamRadius0 = cfg.f("photon.beam_radius", 0.02f);
        p.enableBeams = cfg.i("photon.beams", 1);
        p.beamFraction = cfg.f("photon.beam_fraction", 0.125f);
        p.time = cfg.f("time", 0.0f);
        whiteBalanceMatrix(cfg.f("wb.kelvin", 0.0f), p.wbMatrix);
    }
    void rebuildScene() {
        scene = buildScene(cfg);
        renderer.uploadScene(scene);
        setCamera(camera);
        renderer.updateSky();
    }
    // live mode: the scene moves (water, dust, bubbles); the accumulation becomes an exponential average
    void setLive(bool on) {
        Params& p = renderer.params();
        p.liveAlpha = on ? cfg.f("live.alpha", 0.12f) : 0.0f;
        p.liveAlphaCaustic = on ? cfg.f("live.alpha_caustic", 0.06f) : 0.0f;
        p.sampleClampY = on ? cfg.f("live.clamp", 0.25f) : 0.0f;
        p.maxDepth = on ? cfg.i("live.max_depth", 6) : cfg.i("path.max_depth", 12);
        p.hazeAllSegments = on ? cfg.i("live.haze_all", 0) : 1;
        p.cameraDispersion = on ? cfg.i("live.camera_dispersion", 1) : 1;
        p.dispersionBoost = unsigned(on ? cfg.i("live.spectral_split", 1) * 4 : cfg.i("path.spectral_split", 1) * 4);
        renderer.useTemporalDenoiser = on && cfg.i("live.temporal", 1) != 0;
        renderer.setHalfResolution(on && renderer.useTemporalDenoiser && cfg.i("live.upscale", 1) != 0);
        if (renderer.useTemporalDenoiser) p.liveAlpha = cfg.f("live.alpha_temporal", 1.0f);   // the denoiser keeps the history
        renderer.resetDenoiseHistory();
        renderer.photonRadius0 = on ? cfg.f("live.photon_radius", 0.016f) : cfg.f("photon.radius", 0.01f);
        renderer.photonsPerFrame = unsigned(on ? cfg.f("live.photon_count", 4194304.0f) : cfg.f("photon.count", 1048576.0f));
        p.beamFraction = on ? cfg.f("live.beam_fraction", 0.03f) : cfg.f("photon.beam_fraction", 0.125f);
        renderer.liveRadius = on;
        if (!on) { animateScene(scene, p.time = cfg.f("time", 0.0f)); renderer.updateSpheres(scene); }
        renderer.resetAccumulation();
    }
    void liveStep(float t) {
        Params& p = renderer.params();
        if (cfg.i("live.animate", 1) == 0) t = cfg.f("time", 0.0f);   // frozen: measurements against a still reference
        p.time = t;
        animateScene(scene, t);
        renderer.updateSpheres(scene);
        renderer.renderFrame();
    }
    void setCamera(int k) {
        camera = (k % int(scene.cameras.size()) + int(scene.cameras.size())) % int(scene.cameras.size());
        CameraPreset p = scene.cameras[camera];
        const std::string pre = "camera." + std::to_string(camera) + ".";
        p.pos = cfg.v3(pre + "pos", p.pos);
        p.target = cfg.v3(pre + "target", p.target);
        p.focalMm = cfg.f(pre + "focal", p.focalMm);
        p.fNumber = cfg.f(pre + "fnumber", p.fNumber);
        p.focusDist = cfg.f(pre + "focus", p.focusDist);
        cameraFromPreset(p, width, height, renderer.params().cam);
        renderer.resetAccumulation();
    }
    bool compile() {
        std::string log;
        const double t0 = nowSec();
        const bool ok = renderer.compileDevice(log);
        std::printf("%s device programs (%.1f s)\n", ok ? "compiled" : "COMPILE FAILED:", nowSec() - t0);
        if (!log.empty() && (!ok || log.size() > 2)) std::printf("%s\n", log.c_str());
        return ok;
    }
    void loadBlueNoise() {
        int w = 0, h = 0;
        std::vector<unsigned char> g;
        if (readPNGGray(root + "/assets/bluenoise128.png", w, h, g) && w == 128 && h == 128) {
            std::vector<float> f(g.size());
            for (size_t i = 0; i < g.size(); ++i) f[i] = (g[i] + 0.5f) / 256.0f;
            float* d = nullptr;
            cudaMalloc(&d, f.size() * sizeof(float));
            cudaMemcpy(d, f.data(), f.size() * sizeof(float), cudaMemcpyHostToDevice);
            renderer.params().blueNoise = d;
        } else {
            std::printf("blue noise texture missing, dithering with a constant\n");
        }
    }
    void saveCapture(const std::string& prefix) {
        std::filesystem::create_directories(std::filesystem::path(prefix).parent_path());
        renderer.tonemap();
        renderer.downloadOutput(pixels);
        writePNG(prefix + ".png", int(width), int(height), reinterpret_cast<const unsigned char*>(pixels.data()));
        std::vector<float4> acc;
        renderer.downloadAccum(acc);
        // the accumulation is at the render size (half in the upscaled live mode)
        const unsigned int rw = renderer.params().renderWidth, rh = renderer.params().renderHeight;
        acc.resize(size_t(rw) * rh);
        std::vector<float> rgba(acc.size() * 4);
        for (size_t i = 0; i < acc.size(); ++i) {
            const float n = acc[i].w > 0 ? 1.0f / acc[i].w : 0.0f;
            const float3 rgb = xyzToLinearSRGB(make_float3(acc[i].x * n, acc[i].y * n, acc[i].z * n));
            rgba[i * 4 + 0] = rgb.x; rgba[i * 4 + 1] = rgb.y; rgba[i * 4 + 2] = rgb.z; rgba[i * 4 + 3] = 1.0f;
        }
        writeEXR(prefix + ".exr", int(rw), int(rh), 4, rgba.data());
        std::printf("saved %s.png / .exr (%u spp)\n", prefix.c_str(), renderer.samplesPerPixel());
    }
};

// the command line: --headless, --live N, --selftest, --furnace ... (see altinsaat_app.cpp)
int runAltinSaatCli(int argc, char** argv);
