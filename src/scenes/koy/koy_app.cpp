// Scene 5 "Koy": the run's state (koy_app.h) and the command-line tools.
//
//   pc_graphics.exe --scene koy --headless --out captures/koy/x [--cam "x y z" --look "x y z"] [--t s] [--ref]
//            one frame as the window draws it (or, --ref, the converged 256-sample reference of the same moment)
//   pc_graphics.exe --scene koy --kararlilik --yol sabit|yuz|don|yukari --kare N --out captures/kararlilik/koy_sabit
//            steadiness and sharpness (PLAN §9): N live frames of a path at 41 fps and the reference of each; then
//            python tools/kararlilik.py <out>
//   pc_graphics.exe --scene koy --zaman N                 N live frames: the passes' timings
//   pc_graphics.exe --scene koy --doku --out captures/koy/doku/x --cam "x y z" --look "x y z" [--cam .. --look ..]
//            each view in the three texture modes with one exposure (only the textures differ); then
//            python tools/doku_kiyas.py captures/koy/doku/x
//   pc_graphics.exe --scene koy --yokla --cam "x y z" [--cam ..]   height above the sand, the nearest surface
//   pc_graphics.exe --scene koy --dalgacik --out captures/koy/dalgacik/x --cam .. --look ..   ripples off and on, one exposure
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
    cs.method = cfg.i("caustic.method", 1);
    cs.beams = cfg.i("caustic.beams", 1024);
    cs.fineZ0 = cfg.f("caustic.fine_z0", 4.2f);
    cs.fineZ1 = cfg.f("caustic.fine_z1", 15.5f);
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
    // The ripples (Ara durak 3): the waves shorter than ocean.min_wave, which the main surface leaves out so that the
    // caustics stay clean, on a small patch of their own and seen only by the view (Snell's window, the mirror, the
    // glints). They take over most of the slope variance that was statistical; what is shorter still (below
    // ripple.min_wave) stays statistical. From afar the footprint averages them (LEAN) back into the same roughness.
    floorVarOff = p.slopeVarFloor;
    floorVarOn = p.slopeVarFloor;
    if (cfg.i("ripple.use", 1)) {
        KoyOceanSettings rs = os;
        rs.name = "dalgacik";
        rs.N = cfg.i("ripple.n", 1024);
        rs.patch = cfg.f("ripple.patch", 4.0f);
        rs.minWave = cfg.f("ripple.min_wave", 0.02f);
        rs.maxWave = os.minWave;
        rs.swellAmplitude = 0.0f;
        rs.unresolvedVar = 0.0f;
        rs.targetVar = p.slopeVarFloor * cfg.f("ripple.share", 0.8f);
        rs.seed = 23;
        KoyCausticSettings none;   // the ripples never make caustics: the smallest volume, never updated
        none.N = 8;
        none.rays = 8;
        none.slices = 2;
        none.levels = 1;
        if (ripples.init(rs, none, optics.sunAir, optics.waterN, optics.sunRadiusAir)) {
            floorVarOn = std::max(p.slopeVarFloor - ripples.slopeVarianceResolved(), 0.0f);
            p.oceanFinePatch = rs.patch;
            p.oceanFineTexels = float(rs.N);
        }
    }
    setRipples(cfg.i("ripple.on", 1) != 0);
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
    size_t freeBefore = 0, freeAfter = 0, totalMem = 0;
    cudaMemGetInfo(&freeBefore, &totalMem);
    world.build(cfg, root);
    cudaMemGetInfo(&freeAfter, &totalMem);
    std::printf("koy dokular: %.1f s (paralel cozme), dizilerde %.2f GB; GPU bellegi dunyayla %.2f GB artti, kartta %.2f / %.2f GB dolu\n",
                world.textureSeconds, world.textureBytes / 1073741824.0, (double(freeBefore) - double(freeAfter)) / 1073741824.0,
                (totalMem - freeAfter) / 1073741824.0, totalMem / 1073741824.0);
    renderer.uploadWorld(world.meshes, world.instances, world.materials, world.textureSets(), &world.ribbons);
    {   // the floor's depth under every (x, z): which light fields a point reads (koy.cu fieldsFor)
        const int B = cfg.i("light.bottom_map", 512);
        const float half = cfg.f("floor.extent", 110.0f);
        const KoyFloorShape fs = KoyFloorShape::from(cfg);
        std::vector<float> m(size_t(B) * B);
        for (int j = 0; j < B; ++j)
            for (int i = 0; i < B; ++i) m[size_t(j) * B + i] = -fs((i + 0.5f) / B * 2.0f * half - half, (j + 0.5f) / B * 2.0f * half - half);
        cudaChannelFormatDesc fd = cudaCreateChannelDesc<float>();
        cudaArray_t arr = nullptr;
        cudaMallocArray(&arr, &fd, B, B);
        cudaMemcpy2DToArray(arr, 0, 0, m.data(), size_t(B) * sizeof(float), size_t(B) * sizeof(float), B, cudaMemcpyHostToDevice);
        cudaResourceDesc rd{};
        rd.resType = cudaResourceTypeArray;
        rd.res.array.array = arr;
        cudaTextureDesc td{};
        td.addressMode[0] = td.addressMode[1] = cudaAddressModeClamp;
        td.filterMode = cudaFilterModeLinear;
        td.readMode = cudaReadModeElementType;
        td.normalizedCoords = 1;
        cudaTextureObject_t t = 0;
        cudaCreateTextureObject(&t, &rd, &td, nullptr);
        bottomArr = arr;
        bottomTex = t;
        p.bottomMap = t;
        p.bottomMapHalf = half;
        std::printf("koy isik alanlari: %d dip derinligi", p.fields);
        for (int f = 0; f < p.fields; ++f) std::printf(" %.1f", p.fieldBottom[f]);
        std::printf(" m\n");
    }
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

void KoyApp::setRipples(bool on) {
    KoyParams& p = renderer.params();
    const bool was = ripplesOn;
    ripplesOn = on && ripples.surfaceTexture() != 0;
    // turned on: their field now, at this moment (paused, no stepWorld comes to fill it: the old or an unset one)
    if (ripplesOn && !was) ripples.update(worldTime, renderer.stream(), false);
    p.oceanFine = ripplesOn ? ripples.surfaceTexture() : 0;
    p.slopeVarFloor = ripplesOn ? floorVarOn : floorVarOff;   // the same roughness in all, only part of it now resolved
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
        // collisions: the floor (the eye at least 0.3 m above it, as near as the boulder lets you), the boulder, the
        // surface's ceiling (1 m under it)
        const float fy = koyFloor(cfg, diver.pos.x, diver.pos.z) + cfg.f("diver.floor_clear", 0.3f);
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
    if (ripplesOn) ripples.update(t, renderer.stream(), false);
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
    if (bottomTex) cudaDestroyTextureObject(cudaTextureObject_t(bottomTex));
    if (bottomArr) cudaFreeArray(static_cast<cudaArray_t>(bottomArr));
    bottomTex = 0;
    bottomArr = nullptr;
    renderer.params().bottomMap = 0;
    ocean.release();
    ripples.release();
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
    bool camGiven = false, ref = false, worker = false, liveOnly = false;
    float3 camPos{}, camLook{};
    std::vector<float3> cams, looks;   // --doku and --yokla take several --cam/--look pairs
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
        else if (a == "--canli") liveOnly = true;   // --kararlilik: the live pass alone (two runs that differ in one setting)
        else if (a == "--zaman") { mode = "time"; timing = std::stoi(next()); }
        else if (a == "--test") { mode = "test"; testName = opt(); }
        else if (a == "--map") { mode = "map"; mapOut = next(); }
        else if (a == "--doku") mode = "tex";
        else if (a == "--dalgacik") mode = "ripple";
        else if (a == "--yokla") mode = "probe";
        else if (a == "--yol") path = next();
        else if (a == "--kare") frames = std::stoi(next());
        else if (a == "--refn") refN = std::stoi(next());
        else if (a == "--hiz") fps = std::stof(next());
        else if (a == "--t") t0 = std::stof(next());
        else if (a == "--cam") { camPos = parse3(next()); camGiven = true; cams.push_back(camPos); }
        else if (a == "--look") { camLook = parse3(next()); looks.push_back(camLook); }
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
        std::printf("koy: --headless, --kararlilik, --zaman N, --doku, --yokla, --test, --map (canli sahne uygulamadan acilir)\n");
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
    auto aim = [&](size_t c) {   // the c-th --cam/--look pair
        app.diver.pos = cams[c];
        const float3 f = normalize((c < looks.size() ? looks[c] : cams[c] + make_float3(0, 0, -1)) - cams[c]);
        app.diver.yaw = std::atan2(f.x, f.z);
        app.diver.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
        app.setCamera();
    };
    auto nearest = [&](float3 p, float3& dir) {   // the probe's nearest static surface (26 directions, up to 2 m)
        float dist[26];
        app.renderer.probe(p, dist);
        int best = 0;
        for (int q = 1; q < 26; ++q)
            if (dist[q] < dist[best]) best = q;
        const int kk = best >= 13 ? best + 1 : best;
        dir = make_float3(float(kk % 3 - 1), float((kk / 3) % 3 - 1), float(kk / 9 - 1));
        return dist[best];
    };
    if (mode == "probe") {
        for (size_t c = 0; c < cams.size(); ++c) {
            float3 dir;
            const float d = nearest(cams[c], dir);
            std::printf("yokla (%.2f %.2f %.2f): kumun %.2f m ustunde, en yakin yuzey %.2f m (yon %+.0f %+.0f %+.0f)\n", cams[c].x, cams[c].y,
                        cams[c].z, cams[c].y - koyFloor(app.cfg, cams[c].x, cams[c].z), d, dir.x, dir.y, dir.z);
        }
    } else if (mode == "ripple") {
        // the same moment with the ripples off and on, one exposure (metered off): python tools/doku_kiyas.py <out>
        // --kipler kapali,acik
        const std::string o = out.empty() ? app.root + "/captures/koy/dalgacik/kiyas" : out;
        std::filesystem::create_directories(std::filesystem::path(o).parent_path());
        for (size_t c = 0; c < std::max<size_t>(1, cams.size()); ++c) {
            if (c < cams.size()) aim(c);
            app.setRipples(false);
            app.stepWorld(t0);
            app.frame();
            app.adaptExposure(0.0f, true);
            const float exposure = app.renderer.params().exposure;
            for (int on = 0; on < 2; ++on) {
                app.setRipples(on != 0);
                if (on && !app.ripplesOn) {
                    std::printf("dalgacik yok: atlandi\n");
                    continue;
                }
                app.stepWorld(t0);
                app.renderer.params().exposure = exposure;
                app.frame();
                char nm[48];
                std::snprintf(nm, sizeof(nm), "_%02d_%s", int(c), on ? "acik" : "kapali");
                app.save(o + nm);
            }
            std::printf("dalgacik kiyas %d: derinlik %.2f m -> %s_%02d_*.png\n", int(c), -app.diver.pos.y, o.c_str(), int(c));
        }
    } else if (mode == "tex") {
        // the same moment in the three texture modes (the panel's "Doku"), one exposure for all three (metered on
        // "Yüksek"): the pictures differ by the textures alone. python tools/doku_kiyas.py <out> measures them
        const std::string o = out.empty() ? app.root + "/captures/koy/doku/kiyas" : out;
        std::filesystem::create_directories(std::filesystem::path(o).parent_path());
        static const char* names[3] = {"standart", "yuksek", "ultra"};
        for (size_t c = 0; c < std::max<size_t>(1, cams.size()); ++c) {
            if (c < cams.size()) aim(c);
            app.setTextureMode(1);
            app.stepWorld(t0);
            app.frame();
            app.adaptExposure(0.0f, true);
            const float exposure = app.renderer.params().exposure;
            for (int m = 0; m < 3; ++m) {
                app.setTextureMode(m);
                if (app.world.texMode != m) {
                    std::printf("doku %s yuklu degil: atlandi\n", names[m]);
                    continue;
                }
                app.renderer.params().exposure = exposure;
                app.frame();
                char nm[48];
                std::snprintf(nm, sizeof(nm), "_%02d_%s", int(c), names[m]);
                app.save(o + nm);
            }
            float3 dir;
            const float d = nearest(app.diver.pos, dir);
            std::printf("doku kiyas %d: kamera kumun %.2f m ustunde, en yakin yuzey %.2f m -> %s_%02d_*.png\n", int(c),
                        app.diver.pos.y - koyFloor(app.cfg, app.diver.pos.x, app.diver.pos.z), d, o.c_str(), int(c));
        }
    } else if (mode == "still") {
        app.stepWorld(t0);
        app.frame();
        app.adaptExposure(0.0f, true);
        // display.exposure_fixed > 0: that exposure, not the metered one (two runs that differ in one setting compare
        // pixel for pixel)
        if (app.cfg.f("display.exposure_fixed", 0.0f) > 0.0f) app.renderer.params().exposure = app.cfg.f("display.exposure_fixed", 0.0f);
        app.frame();
        std::printf("pozlama carpani %.6f\n", app.renderer.params().exposure);
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
        if (path == "yakin" || path == "kum") {
            // yakin: close to the boulder (about a metre), looking at it, gliding sideways along it; kum: 30 cm over the
            // sand, looking down at it, swimming forward (the 16K grain at its nearest: the hardest case for shimmer)
            const bool sand = path == "kum";
            app.diver.pos = sand ? app.cfg.v3("path.sand_pos", make_float3(1.0f, -6.5f, 3.0f)) : app.cfg.v3("path.near_pos", make_float3(-2.2f, -6.1f, -4.1f));
            const float3 look = sand ? app.cfg.v3("path.sand_look", make_float3(1.0f, -6.8f, 2.7f)) : app.cfg.v3("path.near_look", make_float3(-2.4f, -6.6f, -6.0f));
            const float3 f = normalize(look - app.diver.pos);
            app.diver.yaw = std::atan2(f.x, f.z);
            app.diver.pitch = std::asin(f.y);
            app.diver.vel = make_float3(0, 0, 0);
            app.setCamera();
        }
        if (!cams.empty()) aim(0);   // --cam / --look: the path starts there
        app.stepWorld(t0);
        app.frame();
        app.adaptExposure(0.0f, true);
        // display.exposure_fixed > 0: that exposure all the way (two runs that differ in one setting compare pixel for pixel)
        const float fixedExposure = app.cfg.f("display.exposure_fixed", 0.0f);
        if (fixedExposure > 0.0f) app.renderer.params().exposure = fixedExposure;
        double sum = 0.0;
        for (int k = 0; k < frames; ++k) {   // pass 1: live
            const float t = t0 + k * dt;
            // the path: still (the world moves), swimming forward, turning, looking up at the surface, along the boulder
            float fwd = 0, right = 0, up = 0, lookX = 0, lookY = 0;
            if (path == "yuz" || path == "kum") fwd = 1.0f;
            else if (path == "yakin") right = 1.0f;
            else if (path == "don") lookX = -dt * 12.0f / (app.cfg.f("diver.mouse", 0.0022f) * 57.2958f);   // 12 deg/s
            else if (path == "yukari" && k == 0) lookY = -(55.0f / 57.2958f) / app.cfg.f("diver.mouse", 0.0022f);
            app.stepDiver(fwd, right, up, false, lookX, lookY, dt);
            app.stepWorld(t);
            if (fixedExposure > 0.0f) app.renderer.params().exposure = fixedExposure;
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
        for (int k = 0; k < frames && !liveOnly; ++k) {   // pass 2: the reference
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
