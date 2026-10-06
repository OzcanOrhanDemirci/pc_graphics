// Scene 8 "Denizaltı": the shared state and the command-line tools.
//
//   pc_graphics.exe --scene denizalti --headless --olc                 the card's limits and a frame-time table
//                                                                      (tessellation x supersampling, 1440p)
//   pc_graphics.exe --scene denizalti --headless --kare <ad> [--res 0..4] [--ssaa 1..16] [--tess 0..2]
//                   [--cam x,y,z,yaw,pitch] [--fov derece] [--debug 0..2] [--yerdegistirme 0|1]
//                                                                      one frame to captures/denizalti/<ad>.png
//   pc_graphics.exe --scene denizalti --headless --test catlak         tessellation cracks: pinholes counted
#include "scenes/denizalti/dz_app.h"
#include "core/image_io.h"
#include "scenes/denizalti/dz_gl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace dz {

const char* const kResNames[5] = {"720p", "1080p", "1440p", "4K", "8K"};
const int kResW[5] = {1280, 1920, 2560, 3840, 7680};
const int kResH[5] = {720, 1080, 1440, 2160, 4320};
const int kSsaa[5] = {1, 2, 4, 8, 16};

void DzApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/denizalti.cfg");
    for (const std::string& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
}

void DzApp::build() {
    mesh = Mesh();
    lights.clear();
    start = buildTestWall(mesh, cfg.f("geom.maxEdge", 0.012f), lights);
    bakeOpt.samples = cfg.i("bake.samples", 1024);
    bakeOpt.bounces = cfg.i("bake.bounces", 12);
    bakeOpt.texel = cfg.f("bake.texel", 0.002f);
    atlas = buildAtlas(mesh, bakeOpt.texel);
    std::fprintf(stderr, "denizalti: %zu ucgen, %u parca (chart), atlas %dx%d\n", mesh.tris(), mesh.charts, atlas.width, atlas.height);
    home();
}

std::string DzApp::lightPath() const { return root + "/cache/denizalti/isik.bin"; }

bool DzApp::ensureLight(std::string& err, const std::function<void(float, const std::string&)>& progress, bool forceBake) {
    const uint64_t key = bakeKey(mesh, lights, bakeOpt);
    if (!forceBake && loadLightMap(lightPath(), key, light)) {
        std::printf("isik haritasi onbellekten: %dx%d, %zu teksel (pisirme %.0f sn)\n", light.atlas.width, light.atlas.height, light.texels,
                    light.bakeSeconds);
        return true;
    }
    if (!bake(mesh, atlas, lights, bakeOpt, root, light, err, progress)) return false;
    light.key = key;
    std::printf("isik pisirildi: %dx%d, %zu teksel, %d ornek, %.1f sn\n", light.atlas.width, light.atlas.height, light.texels, bakeOpt.samples,
                light.bakeSeconds);
    if (!saveLightMap(lightPath(), light)) std::printf("isik haritasi yazilamadi: %s\n", lightPath().c_str());
    return true;
}

void DzApp::home() {
    cam.pos = start.pos;
    cam.yaw = start.yaw;
    cam.pitch = start.pitch;
    cam.fovY = cfg.f("cam.fov", 60.0f) * 3.14159265f / 180.0f;
    set.exposure = cfg.f("cam.ev", 4.7f);
}

bool DzApp::initGL(std::string& err) {
    if (!renderer.init(root, err)) return false;
    renderer.upload(mesh);
    if (light.valid()) renderer.uploadLight(light);
    renderer.setLamps(lights.data(), int(lights.size()));
    glReady = true;
    return true;
}

void DzApp::applyChoices() {
    set.width = kResW[resIndex];
    set.height = kResH[resIndex];
    set.ssaa = kSsaa[ssaaIndex];
}

// ---------------------------------------------------------------------------------------------------
namespace {

std::string argValue(int argc, char** argv, const char* name, const std::string& def = "") {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return def;
}
bool hasArg(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

GLFWwindow* hiddenContext() {
    if (!glfwInit()) return nullptr;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* w = glfwCreateWindow(64, 64, "denizalti", nullptr, nullptr);
    if (!w) return nullptr;
    glfwMakeContextCurrent(w);
    glfwSwapInterval(0);
    return w;
}

void savePng(const std::string& path, Renderer& r) {
    std::vector<uint8_t> px;
    r.readOutput(px);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    setPNGCompression(1);
    writePNG(path, r.outW(), r.outH(), px.data());
}

// renders n frames, returns the median of the GPU's total frame time (ms) and the parts' medians
GpuTimes timeFrames(DzApp& a, int n) {
    std::vector<GpuTimes> t;
    for (int i = 0; i < n + 3; ++i) {
        a.renderer.draw(a.cam, a.set, a.set.width, a.set.height, 0.0);
        glFinish();
        if (i >= 3) t.push_back(a.renderer.times());
    }
    auto med = [&](double GpuTimes::*f) {
        std::vector<double> v;
        for (const GpuTimes& x : t) v.push_back(x.*f);
        std::sort(v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    };
    GpuTimes r;
    r.scene = med(&GpuTimes::scene);
    r.resolve = med(&GpuTimes::resolve);
    r.post = med(&GpuTimes::post);
    r.final = med(&GpuTimes::final);
    r.total = med(&GpuTimes::total);
    return r;
}

// cracks: with back faces drawn magenta (debug view 3, no culling), a crack shows the inside of the surface it opens:
// pixels where magenta dominates are counted
int crackPixels(Renderer& r) {
    std::vector<float> rgb;
    int w = 0, h = 0;
    r.readHdr(rgb, w, h);
    int n = 0;
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        const float R = rgb[i * 3], G = rgb[i * 3 + 1], B = rgb[i * 3 + 2];
        if (R > 0.25f && B > 0.25f && G < 0.5f * std::min(R, B)) ++n;
    }
    return n;
}

}   // namespace

int runCli(int argc, char** argv) {
    DzApp a;
    a.root = argValue(argc, argv, "--root", PROJECT_ROOT);
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--set") == 0) a.overrides.push_back(argv[i + 1]);
    a.loadConfig();
    const auto t0 = std::chrono::steady_clock::now();
    a.build();
    const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("denizalti: dunya %zu ucgen, %zu kose, %.0f ms\n", a.mesh.tris(), a.mesh.v.size(), buildMs);
    {
        std::string berr;
        int lastPct = -1;
        if (!a.ensureLight(berr, [&](float f, const std::string& s) {
                const int pct = int(f * 100.0f);
                if (pct != lastPct && pct % 5 == 0) {
                    std::printf("  %3d%% %s\n", pct, s.c_str());
                    lastPct = pct;
                }
            }, hasArg(argc, argv, "--firin"))) {
            std::printf("firin: %s\n", berr.c_str());
            return 1;
        }
    }
    GLFWwindow* win = hiddenContext();
    if (!win) {
        std::printf("GL baglami acilamadi\n");
        return 1;
    }
    std::string err;
    if (!a.initGL(err)) {
        std::printf("%s\n", err.c_str());
        return 1;
    }
    dzgl::debugOutput(hasArg(argc, argv, "--gldebug"));
    a.resIndex = std::stoi(argValue(argc, argv, "--res", "2"));
    a.ssaaIndex = 2;
    {
        const int n = std::stoi(argValue(argc, argv, "--ssaa", "4"));
        for (int k = 0; k < 5; ++k)
            if (kSsaa[k] == n) a.ssaaIndex = k;
    }
    a.set.tess = std::stoi(argValue(argc, argv, "--tess", "2"));
    a.set.debug = std::stoi(argValue(argc, argv, "--debug", "0"));
    a.set.displacement = argValue(argc, argv, "--yerdegistirme", "1") != "0";
    a.set.tessOff = hasArg(argc, argv, "--bolmesiz");
    a.applyChoices();
    const std::string camArg = argValue(argc, argv, "--cam");
    if (!camArg.empty()) {
        float v[5] = {0, 0, 0, 0, 0};
        std::sscanf(camArg.c_str(), "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]);
        a.cam.pos = make_float3(v[0], v[1], v[2]);
        a.cam.yaw = v[3];
        a.cam.pitch = v[4];
    }
    const std::string fov = argValue(argc, argv, "--fov");
    if (!fov.empty()) a.cam.fovY = std::stof(fov) * 3.14159265f / 180.0f;
    const std::string out = a.root + "/captures/denizalti/";

    if (hasArg(argc, argv, "--olc")) {
        std::printf("taban agi: %zu ucgen, en ince ucgen %.3f mm\n", a.renderer.baseTris, a.renderer.finestTriangle * 1000.0f);
        std::printf("1440p | tessellation x SSAA | GPU ms (sahne / cozumleme / son) \n");
        const char* tn[3] = {"Low", "Medium", "High"};
        for (int t = 0; t < 3; ++t)
            for (int s = 0; s < 5; ++s) {
                a.set.tess = t;
                a.ssaaIndex = s;
                a.applyChoices();
                const GpuTimes g = timeFrames(a, 8);
                std::printf("  %-6s %2dx (%d ornek x %dx%d) : %7.2f ms  (%6.2f / %5.2f / %5.2f)\n", tn[t], kSsaa[s], a.renderer.msaa, a.renderer.sx,
                            a.renderer.sy, g.total, g.scene, g.resolve, g.post + g.final);
            }
        return 0;
    }
    if (hasArg(argc, argv, "--kare")) {
        const std::string name = argValue(argc, argv, "--kare");
        const GpuTimes g = timeFrames(a, 3);
        savePng(out + name + ".png", a.renderer);
        std::printf("kare %s: %s %dx tess %d, %.2f ms\n", name.c_str(), kResNames[a.resIndex], kSsaa[a.ssaaIndex], a.set.tess, g.total);
        return 0;
    }
    if (argValue(argc, argv, "--test") == "catlak") {
        int fails = 0;
        // close views onto the manifold, the frames and the deck, every tessellation level, displacement on
        const float cams[][5] = {{-1.7f, 1.45f, -0.35f, -1.57f, -0.1f}, {-1.4f, 0.5f, 0.2f, -1.9f, -0.5f}, {-1.9f, 2.2f, -0.9f, -1.2f, 0.35f},
                                 {-1.95f, 1.25f, 0.18f, -1.57f, -0.3f}};
        for (const auto& c : cams)
            for (int t = 0; t < 3; ++t)
                for (int s : {0, 2}) {
                    a.cam.pos = make_float3(c[0], c[1], c[2]);
                    a.cam.yaw = c[3];
                    a.cam.pitch = c[4];
                    a.set.tess = t;
                    a.ssaaIndex = s;
                    a.resIndex = 2;
                    a.applyChoices();
                    a.set.debug = 3;
                    timeFrames(a, 1);
                    const int h = crackPixels(a.renderer);
                    char nm[96];
                    std::snprintf(nm, sizeof(nm), "catlak_%d_t%d_s%d.png", int(&c - &cams[0]), t, kSsaa[s]);
                    savePng(out + nm, a.renderer);
                    std::printf("  kamera %d tess %d ssaa %2d: macenta piksel %d\n", int(&c - &cams[0]), t, kSsaa[s], h);
                    if (h > 0) ++fails;
                }
        std::printf("catlak testi: %s\n", fails ? "KALDI" : "GECTI");
        return fails ? 1 : 0;
    }
    std::printf("secenek yok: --olc | --kare <ad> | --test catlak\n");
    return 2;
}

}   // namespace dz
