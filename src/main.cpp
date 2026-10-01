// stress_test_pc: "Altin saatte cam ve tas". A spectral path tracer on OptiX 9.1 for one scene.
//
//   stress.exe                      borderless full screen on the primary monitor
//   stress.exe --windowed           1280x720 window (still renders at --width x --height)
//   stress.exe --headless --spp N --out captures/name     render N samples per pixel, write PNG + EXR
//   stress.exe --selftest           spectral tests (CMF, D65, Sellmeier, thin film, sigmoid fit)
//   options: --camera k, --width W, --height H, --set key=value, --view v, --furnace, --spp-per-launch k
//
// Keys: H hud, Space still/live, S screenshot, R reset, F5 reload shaders + scene.cfg, 1-6 views,
//       Up/Down exposure, Left/Right focus, C next camera, P photons, V haze, N NEE, Esc quit.
#include "gpu_stats.h"
#include "image_io.h"
#include "renderer.h"
#include "scene.h"
#include "shared/spectrum.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <windows.h>
#include <GL/gl.h>
#include "stb_easy_font.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

// Ask hybrid-graphics systems for the discrete NVIDIA GPU.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0;
}

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

static double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct App {
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

    void loadConfig() {
        cfg = Config();
        cfg.load(root + "/scene.cfg");
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
        p.enablePhotons = cfg.i("photon.enable", 1);
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
        std::vector<float> rgba(acc.size() * 4);
        for (size_t i = 0; i < acc.size(); ++i) {
            const float n = acc[i].w > 0 ? 1.0f / acc[i].w : 0.0f;
            const float3 rgb = xyzToLinearSRGB(make_float3(acc[i].x * n, acc[i].y * n, acc[i].z * n));
            rgba[i * 4 + 0] = rgb.x; rgba[i * 4 + 1] = rgb.y; rgba[i * 4 + 2] = rgb.z; rgba[i * 4 + 3] = 1.0f;
        }
        writeEXR(prefix + ".exr", int(width), int(height), 4, rgba.data());
        std::printf("saved %s.png / .exr (%u spp)\n", prefix.c_str(), renderer.samplesPerPixel());
    }
};

static std::string timestamp() {
    std::time_t t = std::time(nullptr);
    char b[64];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", std::localtime(&t));
    return b;
}

// ---------------------------------------------------------------------------------------------------
static int runHeadless(App& app, int spp, const std::string& out) {
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
    return 0;
}

// ---------------------------------------------------------------------------------------------------
static void drawText(float x, float y, const std::string& text, float scale) {
    static char buffer[200000];
    const int quads = stb_easy_font_print(0, 0, const_cast<char*>(text.c_str()), nullptr, buffer, sizeof(buffer));
    glEnableClientState(GL_VERTEX_ARRAY);
    for (int pass = 0; pass < 2; ++pass) {
        glPushMatrix();
        glTranslatef(x + (pass == 0 ? scale : 0), y + (pass == 0 ? scale : 0), 0);
        glScalef(scale, scale, 1);
        if (pass == 0) glColor4f(0, 0, 0, 0.8f);
        else glColor4f(1.0f, 0.95f, 0.85f, 1.0f);
        glVertexPointer(2, GL_FLOAT, 16, buffer);
        glDrawArrays(GL_QUADS, 0, quads * 4);
        glPopMatrix();
    }
    glDisableClientState(GL_VERTEX_ARRAY);
}

static App* g_app = nullptr;
static bool g_keys[GLFW_KEY_LAST + 1] = {};
static void keyCallback(GLFWwindow*, int key, int, int action, int) {
    if (key >= 0 && key <= GLFW_KEY_LAST && action == GLFW_PRESS) g_keys[key] = true;
}
static bool pressed(int key) {
    const bool p = g_keys[key];
    g_keys[key] = false;
    return p;
}

// --autotest: presses keys on a timeline through the same path real key presses take (F0 gate).
static const struct { double t; int key; } kAutotest[] = {
    {3.0, GLFW_KEY_H}, {3.5, GLFW_KEY_H}, {4.0, GLFW_KEY_F5}, {7.0, GLFW_KEY_S}, {9.0, GLFW_KEY_ESCAPE}};

static int runWindow(App& app, bool windowed, bool autotest) {
    if (!glfwInit()) { std::printf("glfwInit failed\n"); return 1; }
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(mon);
    GLFWwindow* win = nullptr;
    if (windowed) {
        win = glfwCreateWindow(1280, 720, "stress_test_pc", nullptr, nullptr);
    } else {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        win = glfwCreateWindow(mode->width, mode->height, "stress_test_pc", mon, nullptr);
    }
    if (!win) { std::printf("window creation failed\n"); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(0);
    glfwSetKeyCallback(win, keyCallback);
    std::printf("GL_RENDERER: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    std::printf("monitor: %dx%d @ %d Hz\n", mode->width, mode->height, mode->refreshRate);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, app.width, app.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    uchar4* pinned = nullptr;
    cudaMallocHost(&pinned, size_t(app.width) * app.height * sizeof(uchar4));

    double lastStats = 0, fpsT = nowSec();
    int fpsFrames = 0;
    float fps = 0;
    GpuStats stats;
    Params& p = app.renderer.params();
    std::string message;
    double messageUntil = 0;
    auto say = [&](const std::string& m) { message = m; messageUntil = nowSec() + 3.0; std::printf("%s\n", m.c_str()); };

    const double windowStart = nowSec();
    size_t autoNext = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (autotest && autoNext < sizeof(kAutotest) / sizeof(kAutotest[0]) && nowSec() - windowStart > kAutotest[autoNext].t) {
            std::printf("autotest: key %d at %.1f s\n", kAutotest[autoNext].key, nowSec() - windowStart);
            g_keys[kAutotest[autoNext].key] = true;
            ++autoNext;
        }
        if (pressed(GLFW_KEY_ESCAPE)) break;
        if (pressed(GLFW_KEY_H)) app.hud = !app.hud;
        if (pressed(GLFW_KEY_SPACE)) { app.live = !app.live; app.renderer.resetAccumulation(); }
        if (pressed(GLFW_KEY_R)) app.renderer.resetAccumulation();
        if (pressed(GLFW_KEY_C)) app.setCamera(app.camera + 1);
        if (pressed(GLFW_KEY_P)) { p.enablePhotons = !p.enablePhotons; app.renderer.resetAccumulation(); say(std::string("fotonlar ") + (p.enablePhotons ? "acik" : "kapali")); }
        if (pressed(GLFW_KEY_V)) { p.enableHaze = !p.enableHaze; app.renderer.resetAccumulation(); say(std::string("pus ") + (p.enableHaze ? "acik" : "kapali")); }
        if (pressed(GLFW_KEY_G)) { p.enableSunPatch = !p.enableSunPatch; app.renderer.resetAccumulation(); say(std::string("gunes lekesi baglantisi ") + (p.enableSunPatch ? "acik" : "kapali")); }
        if (pressed(GLFW_KEY_N)) { p.enableNee = !p.enableNee; app.renderer.resetAccumulation(); say(std::string("NEE ") + (p.enableNee ? "acik" : "kapali")); }
        for (int k = 0; k < 6; ++k)
            if (pressed(GLFW_KEY_1 + k)) p.view = k == 0 ? 0 : k;
        if (pressed(GLFW_KEY_UP)) { app.ev += 0.25f; p.exposure = std::pow(2.0f, app.ev); }
        if (pressed(GLFW_KEY_DOWN)) { app.ev -= 0.25f; p.exposure = std::pow(2.0f, app.ev); }
        if (pressed(GLFW_KEY_RIGHT)) { p.cam.focusDist *= 1.05f; app.renderer.resetAccumulation(); }
        if (pressed(GLFW_KEY_LEFT)) { p.cam.focusDist /= 1.05f; app.renderer.resetAccumulation(); }
        if (pressed(GLFW_KEY_F5)) {
            app.loadConfig();
            if (app.compile()) {
                app.applyConfig();
                app.rebuildScene();
                say("yeniden derlendi");
            } else {
                say("DERLEME HATASI: konsola bak");
            }
        }
        if (pressed(GLFW_KEY_S)) {
            app.saveCapture(app.root + "/captures/shot_" + timestamp());
            say("ekran goruntusu kaydedildi");
        }

        // render: in still mode keep accumulating for ~30 ms per displayed frame
        const double t0 = nowSec();
        if (app.live) {
            p.time = float(nowSec() - app.start);
            app.renderer.resetAccumulation();
            app.renderer.renderFrame();
        } else {
            do { app.renderer.renderFrame(); } while (nowSec() - t0 < 0.030);
        }
        p.ditherFrame = app.live ? int(app.renderer.frame()) : 0;
        app.renderer.tonemap();
        cudaMemcpy(pinned, p.output, size_t(app.width) * app.height * sizeof(uchar4), cudaMemcpyDeviceToHost);

        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, fbw, fbh, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, app.width, app.height, GL_RGBA, GL_UNSIGNED_BYTE, pinned);
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
        // letterbox to the framebuffer, image rows are top first
        const float sx = float(fbw) / app.width, sy = float(fbh) / app.height, s = std::min(sx, sy);
        const float w = app.width * s, h = app.height * s, x0 = 0.5f * (fbw - w), y0 = 0.5f * (fbh - h);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(x0, y0);
        glTexCoord2f(1, 0); glVertex2f(x0 + w, y0);
        glTexCoord2f(1, 1); glVertex2f(x0 + w, y0 + h);
        glTexCoord2f(0, 1); glVertex2f(x0, y0 + h);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        ++fpsFrames;
        if (nowSec() - fpsT > 0.5) { fps = float(fpsFrames / (nowSec() - fpsT)); fpsFrames = 0; fpsT = nowSec(); }
        if (nowSec() - lastStats > 0.25) { stats = gpuStatsRead(); lastStats = nowSec(); }
        if (app.hud) {
            char b[512];
            const PassTimes& t = app.renderer.times();
            std::snprintf(b, sizeof(b),
                          "%s  %.1f fps   path %.1f ms   ton %.2f ms   %u spp   EV %+.2f\n"
                          "%.0f W / %.0f W   %u MHz   %u C   kamera %d   gorunum %d   foton %s  pus %s  NEE %s",
                          app.live ? "CANLI" : "DURAGAN", fps, t.render, t.tonemap, app.renderer.samplesPerPixel(), app.ev,
                          stats.watts, stats.limitWatts, stats.smClockMHz, stats.tempC, app.camera, p.view,
                          p.enablePhotons ? "+" : "-", p.enableHaze ? "+" : "-", p.enableNee ? "+" : "-");
            // bottom left: the top left corner belongs to Ozcan's RTSS overlay
            drawText(24, float(fbh) - 64.0f, b, 2.0f);
            if (nowSec() < messageUntil) drawText(24, float(fbh) - 100.0f, message, 2.0f);
        }
        glfwSwapBuffers(win);
    }
    cudaFreeHost(pinned);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}

int main(int argc, char** argv) {
    App app;
    g_app = &app;
    bool headless = false, windowed = false, selftest = false, compileOnly = false, furnace = false, autotest = false;
    int spp = 64, view = 0, sppPerLaunch = 1;
    unsigned int seed = 0;
    std::string out;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--headless") headless = true;
        else if (a == "--windowed") windowed = true;
        else if (a == "--selftest") selftest = true;
        else if (a == "--compile-only") compileOnly = true;
        else if (a == "--furnace") furnace = true;
        else if (a == "--autotest") autotest = true;
        else if (a == "--auto-ev") app.autoEvTarget = std::stof(next());
        else if (a == "--seed") seed = unsigned(std::stoul(next()));
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
    if (headless) rc = runHeadless(app, spp, out.empty() ? app.root + "/captures/headless_" + timestamp() : out);
    else rc = runWindow(app, windowed, autotest);
    gpuStatsShutdown();
    app.renderer.shutdown();
    return rc;
}
