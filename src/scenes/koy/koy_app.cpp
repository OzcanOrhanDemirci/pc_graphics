// Scene 5 "Koy": the run's state (koy_app.h) and the command-line tools.
//
//   pc_graphics.exe --scene koy --headless --out captures/koy/x [--cam "x y z" --look "x y z"] [--t s] [--ref]
//            one frame as the window draws it (or, --ref, the converged 256-sample reference of the same moment)
//   pc_graphics.exe --scene koy --kararlilik --yol sabit|yuz|don|yukari --kare N --out captures/kararlilik/koy_sabit
//            steadiness and sharpness (PLAN §9): N live frames of a path at 41 fps and the reference of each; then
//            python tools/kararlilik.py <out>
//   pc_graphics.exe --scene koy --zaman N                 N live frames: the passes' timings
//   pc_graphics.exe --scene koy --test [ad]               the physics tests (Snell's window, colour at depth, visibility, ...)
//   pc_graphics.exe --scene koy --map captures/koy/harita.png   the trial area from above (the same floor function)
//   options: --set key=value, --width W, --height H, --spp 1|4|8|16, --edge N, --filter 0..3
#include "scenes/koy/koy_app.h"
#include "core/gpu_stats.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <thread>

namespace {
std::string stamp() {
    std::time_t t = std::time(nullptr);
    char b[64];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", std::localtime(&t));
    return b;
}
}   // namespace

void KoyApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/koy.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    quality.base = cfg.i("sample.base", quality.base);
    quality.edge = cfg.i("sample.edge", quality.edge);
    quality.filter = cfg.i("sample.filter", quality.filter);
    quality.radius = cfg.f("sample.radius", quality.radius);
    quality.contrast = cfg.f("sample.edge_contrast", quality.contrast);
}

bool KoyApp::compile() {
    std::string log;
    const double t0 = know();
    const bool ok = renderer.compileDevice(log);
    std::printf("%s koy programs (%.1f s)\n", ok ? "compiled" : "COMPILE FAILED:", know() - t0);
    if (!log.empty() && (!ok || log.size() > 2)) std::printf("%s\n", log.c_str());
    return ok;
}

void KoyApp::setup() {
    KoyParams& p = renderer.params();
    // optics: spectra, the light field, the tables
    optics.build(cfg);
    optics.upload(p);
    std::printf("koy optik: %s\n", optics.summary().c_str());
    // the sea surface and its caustics
    KoyOceanSettings os;
    os.N = cfg.i("ocean.n", 512);
    os.patch = cfg.f("ocean.patch", 16.0f);
    os.wind = cfg.f("ocean.wind", 4.0f);
    os.windDirDeg = cfg.f("ocean.wind_dir", 30.0f);
    os.fetchKm = cfg.f("ocean.fetch_km", 8.0f);
    os.amplitude = cfg.f("ocean.amplitude", 1.0f);
    os.loopSeconds = cfg.f("ocean.loop", 24.0f);
    os.minWave = cfg.f("ocean.min_wave", 0.06f);
    os.swellAmplitude = cfg.f("ocean.swell", 0.05f);
    os.unresolvedVar = cfg.f("ocean.unresolved_var", -1.0f);
    KoyCausticSettings cs;
    cs.N = cfg.i("caustic.n", 1024);
    cs.rays = cfg.i("caustic.rays", 1024);
    cs.slices = cfg.i("caustic.slices", 32);
    cs.z0 = cfg.f("caustic.z0", 0.5f);
    cs.z1 = cfg.f("caustic.z1", 20.0f);
    cs.minBlurTexels = cfg.f("caustic.min_blur", 0.6f);
    cs.levels = cfg.i("caustic.levels", 4);
    ocean.init(os, cs, optics.sunAir, optics.waterN, optics.sunRadiusAir);
    p.ocean = ocean.surfaceTexture();
    p.oceanPatch = os.patch;
    p.oceanTexels = float(os.N);
    p.oceanHeightMax = ocean.heightMax();
    p.slopeVarFloor = ocean.slopeVarianceUnresolved() * cfg.f("ocean.unresolved_scale", 1.0f);
    for (int l = 0; l < 4; ++l) p.caustic[l] = ocean.causticTexture(std::min(l, ocean.causticLevels() - 1));
    p.causticLevels = ocean.causticLevels();
    p.causticPatch = os.patch;
    p.causticLz0 = std::log(cs.z0);
    p.causticLz1 = std::log(cs.z1);
    p.causticSlices = cs.slices;
    p.causticFlat = cfg.i("caustic.flat", 0);
    p.shaftStep0 = cfg.f("shaft.step0", 0.06f);
    p.shaftStepGrow = cfg.f("shaft.grow", 0.06f);
    p.shaftMax = std::max(cfg.f("shaft.max", 70.0f), 1.0f);
    p.farDistance = cfg.f("view.far", 180.0f);
    // the world
    world.build(cfg, root);
    renderer.uploadWorld(world.meshes, world.instances, world.materials, world.textureSets(), &world.ribbons);
    p.rippleTex = world.ripples.set.normal;
    p.rippleHalf = world.rippleHalf;
    std::printf("koy dunya: %s\n", world.summary().c_str());
    stepWorld(0.0f);
    const double t0 = know();
    renderer.bakeAO();
    const float3 bmin = cfg.v3("sunvis.min", make_float3(-14.0f, -9.5f, -16.0f));
    const float3 bmax = cfg.v3("sunvis.max", make_float3(10.0f, -1.0f, 8.0f));
    const float vs = cfg.f("sunvis.voxel", 0.1f);
    renderer.bakeSunVisibility(bmin, bmax - bmin, make_int3(int((bmax.x - bmin.x) / vs), int((bmax.y - bmin.y) / vs), int((bmax.z - bmin.z) / vs)));
    std::printf("koy firin: golgeleme ve gunes gorunurlugu %.2f s\n", know() - t0);
    // blue noise for the dither
    {
        int w = 0, h = 0;
        std::vector<unsigned char> g;
        if (readPNGGray(root + "/assets/bluenoise128.png", w, h, g) && w == 128 && h == 128) {
            std::vector<float> f(g.size());
            for (size_t i = 0; i < g.size(); ++i) f[i] = (g[i] + 0.5f) / 256.0f;
            float* d = nullptr;
            cudaMalloc(&d, f.size() * sizeof(float));
            cudaMemcpy(d, f.data(), f.size() * sizeof(float), cudaMemcpyHostToDevice);
            p.blueNoise = d;
        }
    }
    diver.pos = cfg.v3("diver.start", make_float3(0.0f, -4.6f, 6.0f));
    diver.yaw = cfg.f("diver.yaw_deg", 180.0f) * 3.14159265f / 180.0f;
    diver.pitch = cfg.f("diver.pitch_deg", -8.0f) * 3.14159265f / 180.0f;
    diver.vel = make_float3(0, 0, 0);
    ev = cfg.f("display.ev", 0.0f);
    applyQuality();
    setCamera();
}

void KoyApp::setTextureMode(int mode) {
    world.applyTextureMode(cfg, mode);
    renderer.updateMaterials(world.materials);
}

void KoyApp::applyQuality() {
    renderer.params().edgeIdShare = cfg.f("sample.edge_id_share", 0.25f);
    renderer.setSampling(quality.base, quality.edge, quality.filter, quality.radius, quality.contrast);
}

void KoyApp::setCamera() {
    KCamera& c = renderer.params().cam;
    const float3 f = diver.forward();
    const float3 r = normalize(cross(f, make_float3(0, 1, 0)));
    const float3 u = cross(r, f);
    c.pos = diver.pos;
    c.fwd = f;
    c.right = r;
    c.up = u;
    // the field of view inside the mask (in air): the water narrows it (flat port)
    const float hfov = cfg.f("camera.hfov_air", 90.0f) * 3.14159265f / 180.0f;
    c.tanHalfX = std::tan(0.5f * hfov);
    c.tanHalfY = c.tanHalfX * float(height) / float(width);
    c.portN = cfg.f("camera.port", 1.0f) > 0.5f ? optics.waterN : 1.0f;
    c.pixelAngle = 2.0f * c.tanHalfX / float(width) / c.portN;
}

void KoyApp::stepDiver(float fwd, float right, float up, bool strong, float lookDX, float lookDY, float dt) {
    const float sens = cfg.f("diver.mouse", 0.0022f);
    diver.yaw -= lookDX * sens;
    diver.pitch = std::max(-1.45f, std::min(1.45f, diver.pitch - lookDY * sens));
    const float3 f = diver.forward();
    const float3 r = normalize(cross(f, make_float3(0, 1, 0)));
    float3 wish = f * fwd + r * right + make_float3(0, 1, 0) * up;
    const float wl = length(wish);
    if (wl > 1e-4f) wish = wish / wl;
    // fin thrust against quadratic drag: F = 1/2 rho CdA v^2; the mass includes the water moved along (added mass)
    const float mass = cfg.f("diver.mass", 90.0f) * 1.5f;
    const float thrust = (strong ? cfg.f("diver.thrust_strong", 85.0f) : cfg.f("diver.thrust", 32.0f)) * (wl > 1e-4f ? 1.0f : 0.0f);
    const float cda = cfg.f("diver.cda", 0.25f), rho = 1025.0f;
    const int sub = 4;
    for (int k = 0; k < sub; ++k) {
        const float h = dt / sub;
        const float v = length(diver.vel);
        const float3 drag = v > 1e-5f ? diver.vel * (-0.5f * rho * cda * v) : make_float3(0, 0, 0);
        diver.vel = diver.vel + (wish * thrust + drag) * (h / mass);
        diver.pos = diver.pos + diver.vel * h;
        // collisions: the floor (the eye at least 0.6 m above it), the boulder, the surface's ceiling (1 m under it)
        const float fy = koyFloor(cfg, diver.pos.x, diver.pos.z) + 0.6f;
        if (diver.pos.y < fy) { diver.pos.y = fy; diver.vel.y = std::max(diver.vel.y, 0.0f); }
        if (k == sub - 1) {   // the rocks: the probe's distances in 26 directions push the head out (0.3 m)
            float dist[26];
            renderer.probe(diver.pos, dist);
            const float rmin = cfg.f("diver.head", 0.3f);
            for (int q = 0; q < 26; ++q) {
                if (dist[q] >= rmin) continue;
                const int kk = q >= 13 ? q + 1 : q;
                const float3 dir = normalize(make_float3(float(kk % 3 - 1), float((kk / 3) % 3 - 1), float(kk / 9 - 1)));
                diver.pos = diver.pos - dir * (rmin - dist[q]);
                const float into = dot(diver.vel, dir);
                if (into > 0.0f) diver.vel = diver.vel - dir * into;
            }
        }
        const float ceil = -cfg.f("diver.ceiling", 1.0f);
        if (diver.pos.y > ceil) { diver.pos.y = ceil; diver.vel.y = std::min(diver.vel.y, 0.0f); }
    }
    setCamera();
}

void KoyApp::stepWorld(float t) {
    worldTime = t;
    KoyParams& p = renderer.params();
    p.time = t;
    const double t0 = know();
    ocean.update(t, renderer.stream(), caustics);
    if ((grassMoves && cfg.i("grass.move", 1)) || t == 0.0f) {
        world.grass.update(t, renderer.ribbonPoints(), renderer.ribbonNormals(), renderer.stream());
        renderer.updateRibbons(nullptr, nullptr);
    }
    cudaStreamSynchronize(renderer.stream());
    renderer.worldMs = float((know() - t0) * 1000.0);
}

void KoyApp::frame() { renderer.renderFrame(); }

// The eye adapts halfway (PLAN §5.9): deeper and darker the exposure opens, but only half the difference.
void KoyApp::adaptExposure(float dt, bool instant) {
    const float key = std::log2(0.18f) + ev;
    const float ref = cfg.f("display.adapt_ref", -99.0f) > -50.0f ? cfg.f("display.adapt_ref", -99.0f) : renderer.meteredLog2();
    if (instant) adaptRef = ref;
    const float a = cfg.f("display.adapt", 0.5f);
    const float target = key - (a * renderer.meteredLog2() + (1.0f - a) * adaptRef);
    evAdapt = instant ? target : evAdapt + (target - evAdapt) * (1.0f - std::exp(-dt / cfg.f("display.adapt_seconds", 1.5f)));
    renderer.params().exposure = std::pow(2.0f, evAdapt);
}

void KoyApp::save(const std::string& prefix) {
    std::filesystem::create_directories(std::filesystem::path(prefix).parent_path());
    renderer.download(pixels);
    writePNG(prefix + ".png", int(width), int(height), reinterpret_cast<const unsigned char*>(pixels.data()));
}

void KoyApp::release() {
    ocean.release();
    world.release();
    optics.release();
}

// ---------------------------------------------------------------------------------------------------
namespace {
void printTimes(const KoyApp& app) {
    const KoyTimes& t = app.renderer.times();
    std::printf("gecisler: dunya %.2f (yuzey %.2f, kostik %.2f)  sutun %.2f  temel %.2f  kenar %.2f (%u piksel)  cozum %.2f  son %.2f  = %.2f ms\n",
                t.world, app.ocean.msSurface, app.ocean.msCaustic, t.shafts, t.base, t.edge, t.edgePixels, t.resolve, t.final, t.total());
}
float3 parse3(const std::string& s) {
    float x = 0, y = 0, z = 0;
    std::sscanf(s.c_str(), "%f %f %f", &x, &y, &z);
    return make_float3(x, y, z);
}
}   // namespace

int runKoyTests(KoyApp& app, const std::string& which);   // koy_tests.cpp

int runKoyCli(int argc, char** argv) {
    KoyApp app;
    std::string mode, out, path = "sabit", testName, mapOut;
    int frames = 120, refN = 4, timing = 0;
    float t0 = 0.0f, fps = 41.0f;
    bool camGiven = false, ref = false, worker = false;
    float3 camPos{}, camLook{};
    int spp = -1, edge = -1, filter = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        auto opt = [&]() { return (i + 1 < argc && argv[i + 1][0] != '-') ? next() : std::string(); };
        if (a == "--scene") next();
        else if (a == "--headless") mode = "still";
        else if (a == "--ref") ref = true;
        else if (a == "--isci") worker = true;
        else if (a == "--kararlilik") mode = "stab";
        else if (a == "--zaman") { mode = "time"; timing = std::stoi(next()); }
        else if (a == "--test") { mode = "test"; testName = opt(); }
        else if (a == "--map") { mode = "map"; mapOut = next(); }
        else if (a == "--yol") path = next();
        else if (a == "--kare") frames = std::stoi(next());
        else if (a == "--refn") refN = std::stoi(next());
        else if (a == "--hiz") fps = std::stof(next());
        else if (a == "--t") t0 = std::stof(next());
        else if (a == "--cam") { camPos = parse3(next()); camGiven = true; }
        else if (a == "--look") camLook = parse3(next());
        else if (a == "--spp") spp = std::stoi(next());
        else if (a == "--edge") edge = std::stoi(next());
        else if (a == "--filter") filter = std::stoi(next());
        else if (a == "--out") out = next();
        else if (a == "--width") app.width = unsigned(std::stoi(next()));
        else if (a == "--height") app.height = unsigned(std::stoi(next()));
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else { std::printf("bilinmeyen secenek %s\n", a.c_str()); return 2; }
    }
    if (mode.empty()) {
        std::printf("koy: --headless, --kararlilik, --zaman N, --test, --map (canli sahne uygulamadan acilir)\n");
        return 2;
    }
    app.loadConfig();
    if (spp > 0) app.quality.base = spp;
    if (edge > 0) app.quality.edge = edge;
    if (filter >= 0) app.quality.filter = filter;
    if (mode == "map") {
        // the trial area from above: the floor's height in grey, the boulder, the meadow, the start
        const int W = 1024;
        const float half = 16.0f;
        std::vector<unsigned char> img(size_t(W) * W * 4);
        KoyWorld w;
        for (int y = 0; y < W; ++y)
            for (int x = 0; x < W; ++x) {
                const float wx = -half + 2 * half * (x + 0.5f) / W, wz = -half + 2 * half * (y + 0.5f) / W;
                const float h = koyFloor(app.cfg, wx, wz);
                const float v = std::min(1.0f, std::max(0.0f, (h + 9.0f) / 4.0f));
                unsigned char* p = &img[(size_t(y) * W + x) * 4];
                p[0] = (unsigned char)(60 + 150 * v);
                p[1] = (unsigned char)(70 + 140 * v);
                p[2] = (unsigned char)(90 + 120 * v);
                p[3] = 255;
            }
        std::filesystem::create_directories(std::filesystem::path(mapOut).parent_path());
        writePNG(mapOut, W, W, img.data());
        std::printf("harita: %s\n", mapOut.c_str());
        return 0;
    }
    app.renderer.init(app.width, app.height, app.root);
    std::printf("GPU: %s\n", app.renderer.deviceName().c_str());
    if (worker) {   // load on another thread, as the application does (load() runs on its worker)
        bool ok = false;
        std::thread th([&] { ok = app.compile(); if (ok) app.setup(); });
        th.join();
        if (!ok) return 1;
    } else {
        if (!app.compile()) return 1;
        app.setup();
    }
    if (mode == "test") {
        const int rc = runKoyTests(app, testName);
        app.release();
        app.renderer.shutdown();
        return rc;
    }
    if (camGiven) {
        app.diver.pos = camPos;
        const float3 f = normalize(camLook - camPos);
        app.diver.yaw = std::atan2(f.x, f.z);
        app.diver.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
        app.setCamera();
    }
    int rc = 0;
    app.renderer.params().view = app.cfg.i("view.mode", 0);
    if (mode == "still") {
        app.stepWorld(t0);
        app.frame();
        app.adaptExposure(0.0f, true);
        app.frame();
        printTimes(app);
        if (ref) app.renderer.renderReference(refN);
        const std::string o = out.empty() ? app.root + "/captures/koy/kare_" + stamp() : out;
        app.save(o);
        std::printf("yazildi: %s.png (pozlama %+.2f EV, olculen log2 Y %.2f)\n", o.c_str(), app.evAdapt, app.renderer.meteredLog2());
    } else if (mode == "time") {
        double sum = 0.0;
        for (int k = 0; k < timing; ++k) {
            const double s = know();
            app.stepWorld(t0 + k / fps);
            app.frame();
            app.adaptExposure(1.0f / fps, k == 0);
            if (k >= 3) sum += know() - s;
        }
        std::printf("canli %ux%u: %.2f ms/kare duvar saati (%.1f fps)\n", app.width, app.height, 1000.0 * sum / std::max(1, timing - 3),
                    std::max(1, timing - 3) / sum);
        printTimes(app);
        if (!out.empty()) {
            app.save(out);
            std::printf("yazildi: %s.png (t %.2f s)\n", out.c_str(), app.worldTime);
        }
    } else if (mode == "stab") {
        // PLAN §9: the live frames of a path (as the window draws them) and the converged reference of each moment
        const std::string o = out.empty() ? app.root + "/captures/kararlilik/koy_" + path : out;
        std::filesystem::create_directories(o);
        setPNGCompression(1);   // hundreds of frames: fast zlib, bigger files
        const float dt = 1.0f / fps;
        struct Moment { Diver d; float t, exposure; };
        std::vector<Moment> at(frames);
        if (path == "yakin") {   // close to the boulder (about a metre), looking at it, gliding sideways along it
            app.diver.pos = app.cfg.v3("path.near_pos", make_float3(-2.2f, -6.1f, -4.1f));
            const float3 f = normalize(app.cfg.v3("path.near_look", make_float3(-2.4f, -6.6f, -6.0f)) - app.diver.pos);
            app.diver.yaw = std::atan2(f.x, f.z);
            app.diver.pitch = std::asin(f.y);
            app.diver.vel = make_float3(0, 0, 0);
            app.setCamera();
        }
        app.stepWorld(t0);
        app.frame();
        app.adaptExposure(0.0f, true);
        double sum = 0.0;
        for (int k = 0; k < frames; ++k) {   // pass 1: live
            const float t = t0 + k * dt;
            // the path: still (the world moves), swimming forward, turning, looking up at the surface, along the boulder
            float fwd = 0, right = 0, up = 0, lookX = 0, lookY = 0;
            if (path == "yuz") fwd = 1.0f;
            else if (path == "yakin") right = 1.0f;
            else if (path == "don") lookX = -dt * 12.0f / (app.cfg.f("diver.mouse", 0.0022f) * 57.2958f);   // 12 deg/s
            else if (path == "yukari" && k == 0) lookY = -(55.0f / 57.2958f) / app.cfg.f("diver.mouse", 0.0022f);
            app.stepDiver(fwd, right, up, false, lookX, lookY, dt);
            app.stepWorld(t);
            at[k] = {app.diver, t, app.renderer.params().exposure};
            const double s = know();
            app.frame();
            sum += know() - s;
            app.adaptExposure(dt, false);
            char nm[32];
            std::snprintf(nm, sizeof(nm), "/live_%04d", k);
            app.save(o + nm);
        }
        std::printf("kararlilik koy %s: %d canli kare, %.2f ms/kare\n", path.c_str(), frames, 1000.0 * sum / frames);
        printTimes(app);
        for (int k = 0; k < frames; ++k) {   // pass 2: the reference
            app.diver = at[k].d;
            app.setCamera();
            app.stepWorld(at[k].t);
            app.renderer.params().exposure = at[k].exposure;
            app.renderer.renderReference(refN);
            char nm[32];
            std::snprintf(nm, sizeof(nm), "/ref_%04d", k);
            app.save(o + nm);
        }
        std::printf("kararlilik koy %s: %d referans (%d ornek) -> %s\n", path.c_str(), frames, 16 * refN * refN, o.c_str());
    }
    app.release();
    app.renderer.shutdown();
    return rc;
}
