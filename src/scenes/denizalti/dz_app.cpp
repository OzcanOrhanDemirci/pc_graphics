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
    decals = Decals();
    activeDecals = &decals;
    if (cfg.i("world.testWall", 0)) start = buildTestWall(mesh, cfg.f("geom.maxEdge", 0.012f), lights);
    else start = buildRoom(mesh, cfg.f("geom.maxEdge", 0.012f), lights);
    bakeOpt.samples = cfg.i("bake.samples", 1024);
    bakeOpt.bounces = cfg.i("bake.bounces", 12);
    bakeOpt.texel = cfg.f("bake.texel", 0.002f);
    bakeOpt.clamp = cfg.f("bake.clamp", 0.06f);
    bakeOpt.filter = cfg.i("bake.filter", 2);
    bakeOpt.volCell = cfg.f("bake.volCell", 0.04f);
    bakeOpt.volSamples = cfg.i("bake.volSamples", 256);
    bakeOpt.probeRes = cfg.i("bake.probeRes", 128);
    bakeOpt.probeSamples = cfg.i("bake.probeSamples", 128);
    activeDecals = nullptr;
    std::string derr;
    if (!decals.draw(root, derr)) std::fprintf(stderr, "denizalti: %s\n", derr.c_str());
    atlas = buildAtlas(mesh, bakeOpt.texel);
    buildCollision(cfg.f("cam.radius", 0.1f));
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

void DzApp::buildCollision(float radius) {
    const float hi[3] = {2.5f, 2.7f, 8.3f};
    for (int a = 0; a < 3; ++a) solidDim[a] = int(std::ceil((hi[a] - solidLo[a]) / solidCell));
    std::vector<uint8_t> occ(size_t(solidDim[0]) * solidDim[1] * solidDim[2], 0);
    auto mark = [&](float3 p) {
        const int x = int((p.x - solidLo[0]) / solidCell), y = int((p.y - solidLo[1]) / solidCell), z = int((p.z - solidLo[2]) / solidCell);
        if (x < 0 || y < 0 || z < 0 || x >= solidDim[0] || y >= solidDim[1] || z >= solidDim[2]) return;
        occ[(size_t(z) * solidDim[1] + y) * solidDim[0] + x] = 1;
    };
    // every triangle sampled densely enough that no 5 cm cell it crosses is missed
    for (size_t k = 0; k + 2 < mesh.idx.size(); k += 3) {
        const float3 a = mesh.v[mesh.idx[k]].p, b = mesh.v[mesh.idx[k + 1]].p, c = mesh.v[mesh.idx[k + 2]].p;
        if (mesh.v[mesh.idx[k]].mat == MAT_GLASS) continue;
        const float L = std::max(length(b - a), std::max(length(c - b), length(a - c)));
        const int n = std::max(1, int(std::ceil(L / (0.5f * solidCell))));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n - i; ++j) {
                const float u = float(i) / n, v = float(j) / n;
                mark(a + (b - a) * u + (c - a) * v);
            }
    }
    // grown by the radius (a sphere of cells)
    const int R = int(std::ceil(radius / solidCell));
    solid.assign(occ.size(), 0);
    for (int z = 0; z < solidDim[2]; ++z)
        for (int y = 0; y < solidDim[1]; ++y)
            for (int x = 0; x < solidDim[0]; ++x) {
                if (!occ[(size_t(z) * solidDim[1] + y) * solidDim[0] + x]) continue;
                for (int dz = -R; dz <= R; ++dz)
                    for (int dy = -R; dy <= R; ++dy)
                        for (int dx = -R; dx <= R; ++dx) {
                            if (dx * dx + dy * dy + dz * dz > R * R) continue;
                            const int xx = x + dx, yy = y + dy, zz = z + dz;
                            if (xx < 0 || yy < 0 || zz < 0 || xx >= solidDim[0] || yy >= solidDim[1] || zz >= solidDim[2]) continue;
                            solid[(size_t(zz) * solidDim[1] + yy) * solidDim[0] + xx] = 1;
                        }
            }
}

bool DzApp::blocked(float3 p) const {
    if (solid.empty()) return false;
    const int x = int(std::floor((p.x - solidLo[0]) / solidCell)), y = int(std::floor((p.y - solidLo[1]) / solidCell)),
              z = int(std::floor((p.z - solidLo[2]) / solidCell));
    if (x < 0 || y < 0 || z < 0 || x >= solidDim[0] || y >= solidDim[1] || z >= solidDim[2]) return true;
    return solid[(size_t(z) * solidDim[1] + y) * solidDim[0] + x] != 0;
}

void DzApp::moveCamera(float3 d) {
    // in small steps (a cell is 5 cm: never jump one), each axis on its own so the camera slides along walls
    const float len = length(d);
    const int n = std::max(1, int(std::ceil(len / 0.01f)));
    const float3 s = d * (1.0f / n);
    for (int i = 0; i < n; ++i) {
        const float3 p = cam.pos;
        float3 q = make_float3(p.x + s.x, p.y, p.z);
        if (!blocked(q)) cam.pos = q;
        q = make_float3(cam.pos.x, cam.pos.y, cam.pos.z + s.z);
        if (!blocked(q)) cam.pos = q;
        q = make_float3(cam.pos.x, cam.pos.y + s.y, cam.pos.z);
        if (!blocked(q)) cam.pos = q;
    }
}

void DzApp::home() {
    cam.pos = start.pos;
    cam.yaw = start.yaw;
    cam.pitch = start.pitch;
    cam.fovY = cfg.f("cam.fov", 60.0f) * 3.14159265f / 180.0f;
    set.exposure = cfg.f("cam.ev", 4.7f);
    set.hazeDensity = cfg.f("fx.haze", 1.0f);
}

bool DzApp::initGL(std::string& err) {
    if (!renderer.init(root, err)) return false;
    renderer.upload(mesh);
    renderer.uploadDecals(decals);
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
    a.set.gfx = std::stoi(argValue(argc, argv, "--grafik", "2"));
    a.set.light = std::stoi(argValue(argc, argv, "--isik", "2"));
    a.set.fx = std::stoi(argValue(argc, argv, "--efekt", "2"));
    a.set.debug = std::stoi(argValue(argc, argv, "--debug", "0"));
    a.set.displacement = argValue(argc, argv, "--yerdegistirme", "1") != "0";
    a.set.tessOff = hasArg(argc, argv, "--bolmesiz");
    a.set.normalDetail = argValue(argc, argv, "--detay", "1") != "0";
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
    if (hasArg(argc, argv, "--kararlilik")) {
        // a slow walk with a turn from the start (the live settings), the same frames as a reference at 16x: the
        // frames for tools/kararlilik.py (live_i.png, ref_i.png)
        const std::string dir = out + "kararlilik_" + argValue(argc, argv, "--kararlilik") + "/";
        std::filesystem::create_directories(dir);
        const int n = std::stoi(argValue(argc, argv, "--kare-sayisi", "48"));
        const Camera c0 = a.cam;
        const float3 fw0 = c0.forward();
        Settings live = a.set, ref = a.set;
        ref.ssaa = 16;
        setPNGCompression(1);
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < n; ++i) {
                a.cam = c0;
                a.cam.pos = c0.pos + make_float3(fw0.x, 0.0f, fw0.z) * (0.008f * i);
                a.cam.yaw = c0.yaw + 0.0025f * i;
                a.cam.pitch = c0.pitch + 0.0008f * i;
                const Settings& s = pass ? ref : live;
                a.renderer.draw(a.cam, s, s.width, s.height, 0.0);
                std::vector<uint8_t> px;
                a.renderer.readOutput(px);
                char nm[32];
                std::snprintf(nm, sizeof(nm), "%s_%04d.png", pass ? "ref" : "live", i);
                writePNG(dir + nm, a.renderer.outW(), a.renderer.outH(), px.data());
            }
        std::printf("kararlilik kareleri: %s (%d canli + %d referans, canli %dx, referans 16x)\n", dir.c_str(), n, n, live.ssaa);
        return 0;
    }
    if (hasArg(argc, argv, "--foto")) {
        // the photo as F12 makes it: tiles of 1920 x 1080 with a 192 px guard band
        const std::string name = argValue(argc, argv, "--foto");
        const int W = a.set.width, H = a.set.height, nx = (W + 1919) / 1920, ny = (H + 1079) / 1080, guard = nx * ny > 1 ? 192 : 0;
        std::vector<uint8_t> img(size_t(W) * H * 4);
        const auto t0 = std::chrono::steady_clock::now();
        for (int t = 0; t < nx * ny; ++t) {
            const int tx = t % nx, ty = t / nx;
            const int x0 = tx * W / nx, x1 = (tx + 1) * W / nx, y0 = ty * H / ny, y1 = (ty + 1) * H / ny;
            Settings s = a.set;
            s.fullW = W;
            s.fullH = H;
            s.tileX = x0 - guard;
            s.tileY = y0 - guard;
            s.width = (x1 - x0) + 2 * guard;
            s.height = (y1 - y0) + 2 * guard;
            a.renderer.draw(a.cam, s, s.width, s.height, 0.0);
            std::vector<uint8_t> px;
            a.renderer.readOutput(px);
            for (int y = y0; y < y1; ++y)
                std::memcpy(&img[(size_t(y) * W + x0) * 4], &px[(size_t(y - y0 + guard) * s.width + guard) * 4], size_t(x1 - x0) * 4);
        }
        glFinish();
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::filesystem::create_directories(out);
        setPNGCompression(1);
        writePNG(out + name + ".png", W, H, img.data());
        std::printf("foto %s: %dx%d %dx, %d parca, %.1f sn\n", name.c_str(), W, H, a.set.ssaa, nx * ny, sec);
        return 0;
    }
    if (hasArg(argc, argv, "--kare")) {
        const std::string name = argValue(argc, argv, "--kare");
        const GpuTimes g = timeFrames(a, 3);
        savePng(out + name + ".png", a.renderer);
        if (hasArg(argc, argv, "--siyah-maske")) {   // the pixels where no surface was drawn (HDR exactly zero), cyan
            std::vector<float> rgb;
            int w = 0, h = 0;
            a.renderer.readHdr(rgb, w, h);
            std::vector<uint8_t> m(size_t(w) * h * 4, 0);
            int n = 0;
            for (size_t i = 0; i < size_t(w) * h; ++i) {
                const bool z = rgb[i * 3] == 0.0f && rgb[i * 3 + 1] == 0.0f && rgb[i * 3 + 2] == 0.0f;
                n += z;
                m[i * 4 + 0] = z ? 0 : uint8_t(std::min(255.0f, rgb[i * 3] * 4000.0f));
                m[i * 4 + 1] = z ? 255 : uint8_t(std::min(255.0f, rgb[i * 3 + 1] * 4000.0f));
                m[i * 4 + 2] = z ? 255 : uint8_t(std::min(255.0f, rgb[i * 3 + 2] * 4000.0f));
                m[i * 4 + 3] = 255;
            }
            writePNG(out + name + "_maske.png", w, h, m.data());
            std::printf("siyah piksel: %d\n", n);
        }
        std::printf("kare %s: %s %dx tess %d, %.2f ms\n", name.c_str(), kResNames[a.resIndex], kSsaa[a.ssaaIndex], a.set.tess, g.total);
        return 0;
    }
    if (argValue(argc, argv, "--test") == "oda-catlak") {
        // the room is a closed shell: a crack shows the clear colour (exactly black; every surface gets at least the
        // haze). The same views drawn as High and with every factor 1 (the base mesh as it is): gaps that are real
        // (between deck plates) are black in both; what High adds is cracks
        const float cams[][5] = {{0.95f, 1.62f, 2.45f, -0.2f, -0.06f}, {1.3f, 1.2f, 2.2f, 0.69f, -0.25f}, {-0.9f, 1.62f, 6.9f, 3.3f, -0.1f},
                                 {0.2f, 1.7f, 4.6f, 1.57f, 0.5f}, {-0.5f, 1.4f, 1.0f, -0.9f, -0.4f}, {1.15f, 1.4f, 5.3f, 1.25f, 0.02f}};
        auto black = [&](Renderer& r) {
            std::vector<float> rgb;
            int w = 0, h = 0;
            r.readHdr(rgb, w, h);
            int n = 0;
            for (size_t i = 0; i < size_t(w) * h; ++i)
                if (rgb[i * 3] == 0.0f && rgb[i * 3 + 1] == 0.0f && rgb[i * 3 + 2] == 0.0f) ++n;
            return n;
        };
        int worst = 0;
        for (const auto& c : cams) {
            a.cam.pos = make_float3(c[0], c[1], c[2]);
            a.cam.yaw = c[3];
            a.cam.pitch = c[4];
            for (int ss : {0, 2}) {
                a.ssaaIndex = ss;
                a.applyChoices();
                a.set.tess = 2;
                a.set.tessOff = false;
                timeFrames(a, 1);
                const int hi = black(a.renderer);
                a.set.tessOff = true;
                timeFrames(a, 1);
                const int base = black(a.renderer);
                a.set.tessOff = false;
                std::printf("  kamera (%.2f, %.2f, %.2f) %2dx: siyah piksel High %d, bolmesiz %d, fark %d\n", c[0], c[1], c[2], kSsaa[ss], hi, base, hi - base);
                worst = std::max(worst, hi - base);
            }
        }
        std::printf("oda catlak testi: en kotu fark %d piksel -> %s\n", worst, worst > 0 ? "KALDI" : "GECTI");
        return worst > 0 ? 1 : 0;
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
