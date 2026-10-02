// Scene 4 "Laboratuvar": the drawing of an experiment (materials, the set, the props, the surfaces) and the
// command-line tools:
//   --scene laboratuvar --still --deney su|bal|kum|balon|karpuz [--tetik] [--t S] [--kayit] [--spp N] [--out file.png]
//        one picture: the coarse world (or with --kayit the fine recording) S seconds after the trigger
//   --scene laboratuvar --seq --deney D [--tetik] --t0 A --t1 B --kare N [--kayit] --out dir
//        N pictures from A to B (my own checking: no video is made)
//   --scene laboratuvar --test [name]          the solver's tests (solver_tests.cpp)
//   --scene laboratuvar --surface-test [name]  the liquid surface's tests
//   --scene laboratuvar --weapons-test [name]  the loads and the gelatin calibration
//   --scene laboratuvar --lab-test [name]      the experiments' measurements (lab_tests.cpp)
// Common: --width --height, --set key=value, --cam "x y z" --look "x y z", --fov.
#include "scenes/laboratuvar/laboratuvar_app.h"
#include "scenes/laboratuvar/lab_kernels.h"
#include "scenes/laboratuvar/lab_record.h"
#include "core/image_io.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

int runSolverTests(const Config& c, const std::string& only);
int runSurfaceTests(const Config& cfg, const std::string& only);
int runWeaponsTests(const std::string& only);
int runLabTests(LaboratuvarApp& app, const std::string& only);

void LaboratuvarApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/laboratuvar.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
}

bool LaboratuvarApp::initRenderer(std::string& log) {
    renderer.init(width, height, root);
    return renderer.compileDevice(log);
}

void LaboratuvarApp::release() {
    for (void* p : {static_cast<void*>(dMemTris), static_cast<void*>(dMemCount), static_cast<void*>(dWeight)})
        if (p) cudaFree(p);
    dMemTris = nullptr;
    dMemCount = nullptr;
    dWeight = nullptr;
    weightCap = 0;
    memTris = memTrisCap = 0;
    memSource = nullptr;
}

namespace {

LMaterial mat(int type, float3 albedo, float f0 = 0.0f, float gloss = 0.0f) {
    LMaterial m{};
    m.type = type;
    m.albedo = albedo;
    m.f0 = f0;
    m.gloss = gloss;
    m.ior = 1.5f;
    return m;
}

}   // namespace

void LaboratuvarApp::applyLook() {
    LabParams& p = renderer.params();
    p.ambient = cfg.v3("isik.ortam", make_float3(0.012f, 0.012f, 0.014f));
    p.maxDiffuse = cfg.i("isik.sekme", 2);
    p.exposure = std::pow(2.0f, cfg.f("goruntu.ev", 0.0f));
    p.gapMax = cfg.f("goruntu.bosluk", 0.004f);
    auto box = [&](const std::string& k, float3 c, float3 aim, float w, float h, float3 Le, int visible) {
        LRectLight l;
        l.c = cfg.v3(k + ".konum", c);
        const float3 n = normalize(cfg.v3(k + ".hedef", aim) - l.c);
        float3 u = cross(make_float3(0, 1, 0), n);
        if (dot(u, u) < 1e-6f) u = make_float3(1, 0, 0);
        u = normalize(u);
        const float3 v = cross(n, u);
        l.u = u * (0.5f * cfg.f(k + ".en", w));
        l.v = v * (0.5f * cfg.f(k + ".boy", h));
        l.Le = cfg.v3(k + ".parlaklik", Le);
        l.visible = cfg.i(k + ".gorunur", visible);
        l.fall = cfg.f(k + ".kenar", visible ? 0.75f : 0.2f);
        return l;
    };
    // the panel behind the experiment (seen: the background liquids read against), a key light front left, a strip
    // on the right for a bright edge, a weak top
    p.lights[0] = box("isik.panel", make_float3(0.0f, 0.26f, -0.45f), make_float3(0.0f, 0.26f, 1.0f), 0.55f, 0.50f, make_float3(1.6f, 1.6f, 1.7f), 1);
    p.lights[1] = box("isik.ana", make_float3(-0.55f, 0.55f, 0.45f), make_float3(0.0f, 0.08f, 0.0f), 0.5f, 0.5f, make_float3(9.0f, 8.6f, 8.0f), 0);
    p.lights[2] = box("isik.serit", make_float3(0.55f, 0.25f, -0.10f), make_float3(0.0f, 0.10f, 0.0f), 0.10f, 0.7f, make_float3(14.0f, 14.0f, 15.0f), 0);
    p.lights[3] = box("isik.tepe", make_float3(0.0f, 0.9f, 0.05f), make_float3(0.0f, 0.0f, 0.0f), 0.6f, 0.4f, make_float3(1.5f, 1.5f, 1.6f), 0);
    p.lightCount = std::min(L_MAX_LIGHTS, cfg.i("isik.sayi", 4));
}

void LaboratuvarApp::setCamera(Deney d) {
    const std::string k = std::string("kamera.") + deneyKey(d);
    const float3 defPos[kDeneyCount] = {{0.12f, 0.24f, 0.95f}, {0.0f, 0.16f, 0.50f}, {0.02f, 0.17f, 0.58f}, {0.0f, 0.15f, 0.50f}, {0.0f, 0.16f, 0.62f}};
    const float3 defAim[kDeneyCount] = {{-0.03f, 0.20f, 0.0f}, {0.0f, 0.06f, 0.0f}, {0.03f, 0.07f, 0.0f}, {0.0f, 0.06f, 0.0f}, {0.0f, 0.12f, 0.0f}};
    cam.lookAt(cfg.v3(k + ".konum", defPos[int(d)]), cfg.v3(k + ".hedef", defAim[int(d)]));
    cam.fovDeg = cfg.f(k + ".fov", cfg.f("kamera.fov", 30.0f));
    cam.apply(renderer.params().cam, width, height);
}

void LaboratuvarApp::setupScene(Deney d, int maxParticles, const std::vector<uint3>* membraneTris) {
    deney = d;
    renderer.clearObjects();
    // materials: rebuilt with the scene (their numbers are the objects')
    LabRenderer& R = renderer;
    R.clearMaterials();
    auto add = [&](LMaterial m) { return R.addMaterial(m); };
    LMaterial slate = mat(LM_DIFFUSE, cfg.v3("masa.renk", make_float3(0.05f, 0.05f, 0.055f)), cfg.f("masa.f0", 0.04f), cfg.f("masa.parlaklik", 400.0f));
    slate.pattern = LP_SLATE;
    const int mSlate = add(slate);
    LMaterial back = mat(LM_DIFFUSE, cfg.v3("fon.renk", make_float3(0.035f, 0.035f, 0.04f)));
    back.pattern = LP_BACKDROP;
    const int mBack = add(back);
    LMaterial glass = mat(LM_GLASS, make_float3(1, 1, 1));
    glass.ior = cfg.f("cam.kirilma", 1.5f);
    glass.sigma = cfg.v3("cam.sogurma", make_float3(1.2f, 0.3f, 0.6f));
    matGlass = add(glass);
    LMaterial wood = mat(LM_DIFFUSE, cfg.v3("tahta.renk", make_float3(0.40f, 0.24f, 0.12f)), 0.04f, 60.0f);
    wood.pattern = LP_WOOD;
    matWood = add(wood);
    matSteel = add(mat(LM_METAL, cfg.v3("celik.renk", make_float3(0.62f, 0.62f, 0.63f)), 0.0f, cfg.f("celik.parlaklik", 4000.0f)));
    LMaterial water = mat(LM_LIQUID, make_float3(1, 1, 1));
    water.ior = cfg.f("su.kirilma", 1.333f);
    water.sigma = cfg.v3("su.sogurma", make_float3(0.35f, 0.06f, 0.03f));
    const int mWater = add(water);
    LMaterial honey = mat(LM_LIQUID, make_float3(1, 1, 1));
    honey.ior = cfg.f("bal.kirilma", 1.49f);
    honey.sigma = cfg.v3("bal.sogurma", make_float3(23.0f, 77.0f, 260.0f));
    const int mHoney = add(honey);
    LMaterial sandM = mat(LM_DIFFUSE, cfg.v3("kum.renk", make_float3(0.62f, 0.47f, 0.29f)));
    sandM.pattern = LP_SAND;
    const int mSand = add(sandM);
    LMaterial rubber = mat(LM_SHELL, cfg.v3("balon.renk", make_float3(0.70f, 0.05f, 0.07f)), cfg.f("balon.f0", 0.05f), cfg.f("balon.parlaklik", 150.0f));
    rubber.frost = cfg.f("balon.buzlu", 0.35f);
    rubber.sigma = cfg.v3("balon.optik", make_float3(0.15f, 2.2f, 2.0f));
    const int mRubber = add(rubber);
    LMaterial rind = mat(LM_DIFFUSE, cfg.v3("karpuz.kabuk_renk", make_float3(0.03f, 0.085f, 0.02f)), 0.05f, 90.0f);
    rind.pattern = LP_RIND;
    const int mRind = add(rind);
    const int mFlesh = add(mat(LM_WET, cfg.v3("karpuz.et_renk", make_float3(0.70f, 0.07f, 0.08f)), 0.03f, 5000.0f));
    LMaterial juice = mat(LM_LIQUID, make_float3(1, 1, 1));
    juice.ior = 1.34f;
    juice.sigma = cfg.v3("karpuz.su_sogurma", make_float3(6.0f, 110.0f, 80.0f));
    const int mJuice = add(juice);
    R.uploadMaterials();
    // the set
    R.addMesh(tableMesh(0.75f, 0.32f, -0.40f, 0.03f), mSlate);
    R.addMesh(backdropMesh(1.2f, -0.62f, 0.18f, 1.4f, -0.40f), mBack);
    // the props
    for (int p = 0; p < LabWorld::P_COUNT; ++p) {
        objProp[p] = -1;
        const HostMesh m = labPropMesh(d, p);
        if (m.idx.empty()) continue;
        const int mt = labPropMaterial(d, p);
        objProp[p] = R.addMesh(m, mt == 0 ? matGlass : mt == 1 ? matWood : matSteel);
    }
    // the surfaces
    for (int c = 0; c < kDrawCodes; ++c) surf[c] = -1;
    FluidSurfaceSettings S;
    S.floorY = 0.0f;
    S.smoothing = cfg.f("yuzey.yumusatma", 0.9f);
    S.minNeighbours = cfg.i("yuzey.en_az_komsu", 6);
    const int verts = cfg.i("yuzey.kose", 1500000), tris = cfg.i("yuzey.ucgen", 3000000), blocks = cfg.i("yuzey.blok", 49152);
    auto surface = [&](int code, int material, FluidSurfaceSettings s) { surf[code] = R.addSurface(material, maxParticles, verts, tris, blocks, s); };
    if (d == Deney::Su || d == Deney::Balon) surface(DC_WATER, mWater, S);
    if (d == Deney::Bal) surface(DC_HONEY, mHoney, S);
    if (d == Deney::Kum) {
        FluidSurfaceSettings k = S;
        k.smoothing = cfg.f("kum.yuzey_yumusatma", 0.5f);
        k.dropletSpheres = true;
        surface(DC_SAND, mSand, k);
        R.setDropletsVisible(surf[DC_SAND], false);   // a lone grain in the air: its fine grains only, no bead
        grainsPer = cfg.i("kum.tane_sayisi", 4);
        grainsCap = maxParticles * grainsPer;
        objGrains = R.addSpheres(grainsCap, mSand, true);
        objBall = R.addSpheres(1, matSteel, false);
    } else {
        objGrains = objBall = -1;
    }
    if (d == Deney::Karpuz) {
        FluidSurfaceSettings k = S;
        k.smoothing = cfg.f("karpuz.yuzey_yumusatma", 0.6f);
        surface(DC_RIND, mRind, k);
        surface(DC_FLESH, mFlesh, k);
        surface(DC_JUICE, mJuice, S);
    }
    objMembrane = -1;
    release();
    if (d == Deney::Balon) {
        // room for the fine recording's membrane too (it has more particles and faces than the live one)
        memTrisCap = std::max(cfg.i("balon.ucgen_kapasite", 160000), membraneTris ? int(membraneTris->size()) : 0);
        cudaMalloc(reinterpret_cast<void**>(&dMemTris), size_t(memTrisCap) * sizeof(uint3));
        cudaMalloc(reinterpret_cast<void**>(&dMemCount), sizeof(int));
        objMembrane = R.addDynamicMesh(maxParticles, memTrisCap, mRubber);
        if (membraneTris) setMembraneTris(*membraneTris);
    }
    weightCap = maxParticles;
    cudaMalloc(reinterpret_cast<void**>(&dWeight), size_t(weightCap) * sizeof(float));
    applyLook();
    setCamera(d);
}

void LaboratuvarApp::setMembraneTris(const std::vector<uint3>& tris) {
    if (!dMemTris) return;
    memTris = std::min(int(tris.size()), memTrisCap);
    if (memTris > 0) cudaMemcpy(dMemTris, tris.data(), size_t(memTris) * sizeof(uint3), cudaMemcpyHostToDevice);
    memSource = &tris;
}

void LaboratuvarApp::draw(const DrawInput& in) {
    LabRenderer& R = renderer;
    const cudaStream_t st = R.stream();
    const int n = std::min(in.n, weightCap);
    for (int c = 0; c < kDrawCodes; ++c) {
        if (surf[c] < 0) continue;
        launchWeights(in.code, n, uint8_t(c), dWeight, st);
        R.buildSurface(surf[c], in.pos, dWeight, n, 0.5f * in.spacing);
    }
    if (in.poses)
        for (int p = 0; p < LabWorld::P_COUNT; ++p)
            if (objProp[p] >= 0) R.setTransform(objProp[p], in.poses[p].xf);
    if (objGrains >= 0) {
        const int m = std::min(n, grainsCap / std::max(1, grainsPer));
        launchGrains(in.pos, in.code, m, grainsPer, cfg.f("kum.tane_yayilma", 0.55f) * in.spacing, cfg.f("kum.tane_en_kucuk", 0.10f) * in.spacing,
                     cfg.f("kum.tane_en_buyuk", 0.17f) * in.spacing, R.spheresBuffer(objGrains), R.tintBuffer(objGrains), st);
        R.setSphereCount(objGrains, m * grainsPer);
    }
    if (objBall >= 0) {
        if (in.ballFirst >= 0) {
            launchCentre(in.pos, in.ballFirst, in.ballCount, in.ballRadius, R.spheresBuffer(objBall), st);
            R.setSphereCount(objBall, 1);
        } else {
            R.setSphereCount(objBall, 0);
        }
    }
    if (objMembrane >= 0 && in.membraneFirst >= 0) {
        launchMembrane(in.pos, in.membraneFirst, dMemTris, memTris, cfg.f("balon.yirtilma", 2.6f) * in.spacing, R.meshIndices(objMembrane), dMemCount,
                       R.meshPositions(objMembrane), R.meshNormals(objMembrane), in.membraneCount, st);
        int kept = 0;
        cudaMemcpyAsync(&kept, dMemCount, sizeof(int), cudaMemcpyDeviceToHost, st);
        cudaStreamSynchronize(st);
        R.rebuildMesh(objMembrane, in.membraneCount, kept);
    }
}

// ---------------------------------------------------------------------------------------------------- tools

namespace {

double nowS() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

Deney parseDeney(const std::string& s) {
    for (int k = 0; k < kDeneyCount; ++k)
        if (s == deneyKey(Deney(k))) return Deney(k);
    return Deney::Su;
}

// the experiment's default trigger (the command line's stand-in for a click)
LabEvent defaultEvent(Deney d, const Config& c) {
    LabEvent e;
    switch (d) {
    case Deney::Su: e.kind = LabEvent::Pour; break;
    case Deney::Balon: {
        e.kind = LabEvent::Prick;
        const float R = c.f("balon.yaricap", 0.06f);
        const float3 m = c.v3("balon.merkez", make_float3(0.0f, 0.20f, 0.0f));
        e.a = c.v3("test.igne", m + make_float3(0.0f, -0.25f * R, 0.97f * R));
        break;
    }
    case Deney::Karpuz: {
        e.kind = LabEvent::Shot;
        e.a = c.v3("test.namlu", make_float3(-1.5f, 0.135f, 0.0f));
        e.b = c.v3("test.hedef", make_float3(0.0f, 0.135f, 0.0f));
        e.power = c.f("test.guc", 0.5f);
        break;
    }
    case Deney::Kum: e.kind = LabEvent::Ball; e.a = c.v3("test.bilye", make_float3(0.0f, 0.25f, 0.0f)); break;
    default: break;
    }
    return e;
}

void save(LaboratuvarApp& app, const std::string& path) {
    std::vector<uchar4> px;
    app.renderer.download(px);
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    writePNG(path, int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(px.data()));
}

}   // namespace

int runLaboratuvarCli(int argc, char** argv) {
    LaboratuvarApp app;
    std::string deneyS = "su", out, testName, mode;
    bool trig = false, recorded = false;
    double t = 0.0, t0 = 0.0, t1 = 1.0, live = 0.0;
    int frames = 10;
    unsigned spp = 64;
    std::vector<std::pair<std::string, std::string>> camSet;
    float3 camPos{}, camAim{};
    bool camGiven = false;
    LabInputs inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        auto opt = [&]() { return (i + 1 < argc && argv[i + 1][0] != '-') ? next() : std::string(); };
        auto v3 = [&](const std::string& s) {
            float x = 0, y = 0, z = 0;
            std::sscanf(s.c_str(), "%f %f %f", &x, &y, &z);
            return make_float3(x, y, z);
        };
        if (a == "--scene") next();
        else if (a == "--still") mode = "still";
        else if (a == "--seq") mode = "seq";
        else if (a == "--test") { mode = "test"; testName = opt(); }
        else if (a == "--surface-test") { mode = "surface"; testName = opt(); }
        else if (a == "--weapons-test") { mode = "weapons"; testName = opt(); }
        else if (a == "--lab-test") { mode = "lab"; testName = opt(); }
        else if (a == "--deney") deneyS = next();
        else if (a == "--tetik") trig = true;
        else if (a == "--kayit") recorded = true;
        else if (a == "--t") t = std::stod(next());
        else if (a == "--t0") t0 = std::stod(next());
        else if (a == "--t1") t1 = std::stod(next());
        else if (a == "--kare") frames = std::stoi(next());
        else if (a == "--once") live = std::stod(next());   // seconds of the coarse world before the trigger
        else if (a == "--yukseklik") inputs.height = std::stof(next());
        else if (a == "--akis") inputs.flow = std::stof(next());
        else if (a == "--guc") inputs.power = std::stof(next());
        else if (a == "--spp") spp = unsigned(std::stoi(next()));
        else if (a == "--width") app.width = unsigned(std::stoi(next()));
        else if (a == "--height") app.height = unsigned(std::stoi(next()));
        else if (a == "--cam") { camPos = v3(next()); camGiven = true; }
        else if (a == "--look") camAim = v3(next());
        else if (a == "--fov") camSet.push_back({"kamera.fov", next()});
        else if (a == "--out") out = next();
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
    }
    app.loadConfig();
    for (auto& kv : camSet) app.cfg.set(kv.first, kv.second);
    if (mode == "test") return runSolverTests(app.cfg, testName);
    if (mode == "surface") return runSurfaceTests(app.cfg, testName);
    if (mode == "weapons") return runWeaponsTests(testName);
    if (mode == "lab") return runLabTests(app, testName);
    if (mode != "still" && mode != "seq") {
        std::printf("laboratuvar: --still, --seq, --test, --surface-test, --weapons-test, --lab-test\n");
        return 2;
    }
    const Deney d = parseDeney(deneyS);
    if (d == Deney::Bal || d == Deney::Kum) trig = trig || false;
    std::string log;
    // the world: coarse (live), or a recording's fine pass
    LabWorld w;
    LabRecording rec;
    const double tStart = nowS();
    std::vector<uint3> memTris;
    int maxN = 0;
    const LabEvent ev = defaultEvent(d, app.cfg);
    if (recorded) {
        const std::string pre = std::string(deneyKey(d)) + ".";
        rec.start(app.cfg, d, inputs, ev, std::max(t, t1) + 0.01, 1.0 / app.cfg.f(pre + "kayit_hiz", 600.0f), size_t(app.cfg.f("kayit.butce_mb", 4096.0f)) << 20);
        while (rec.computing()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (rec.failed()) { std::printf("kayit basarisiz\n"); return 1; }
        maxN = rec.drawnCount();
        memTris = rec.membraneTris;
        std::printf("kayit: %d kare, %.1f s\n", rec.framesDone(), nowS() - tStart);
    } else {
        w.build(d, app.cfg, false, inputs);
        w.settle(app.cfg.f(std::string(deneyKey(d)) + ".oturma", 1.0f));
        std::printf("%s (kurma + oturma %.1f s)\n", w.summary().c_str(), nowS() - tStart);
        for (int k = 0; k < int(live * 60.0); ++k) w.step(1.0f / 60.0f);
        if (trig && ev.kind != LabEvent::None) w.trigger(ev);
        maxN = w.count() + app.cfg.i(std::string(deneyKey(d)) + ".ek_kapasite", 0);
        memTris = w.membraneTris;
    }
    if (!app.initRenderer(log)) { std::printf("DERLEME HATASI\n%s\n", log.c_str()); return 1; }
    app.setupScene(d, std::max(maxN, 1024), &memTris);
    if (camGiven) {
        app.cam.lookAt(camPos, camAim);
        app.cam.apply(app.renderer.params().cam, app.width, app.height);
    }
    float3* dPos = nullptr;
    uint8_t* dCode = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&dPos), size_t(maxN + 1) * sizeof(float3));
    cudaMalloc(reinterpret_cast<void**>(&dCode), size_t(maxN + 1));
    PropPose poses[LabWorld::P_COUNT];
    double simT = 0.0;
    auto drawAt = [&](double tt) {
        DrawInput in;
        if (recorded) {
            rec.frameAt(tt, dPos, dCode, poses, app.renderer.stream());
            in.n = rec.drawnCount();
            in.spacing = rec.spacing();
            in.membraneFirst = rec.membraneFirst;
            in.membraneCount = rec.membraneCount;
            in.ballFirst = rec.ballFirst;
            in.ballCount = rec.ballCount;
            in.ballRadius = rec.ballRadius;
        } else {
            while (simT + 1e-9 < tt) {
                w.step(1.0f / 60.0f);
                simT += 1.0 / 60.0;
            }
            w.exportDraw(dPos, dCode, app.renderer.stream());
            for (int p = 0; p < LabWorld::P_COUNT; ++p) poses[p] = w.prop[p];
            in.n = w.count();
            in.spacing = w.s;
            in.membraneFirst = w.membraneFirst;
            in.membraneCount = w.membraneCount;
            in.ballFirst = w.ballFirst;
            in.ballCount = w.ballCount;
            in.ballRadius = w.ballRadius;
        }
        in.pos = dPos;
        in.code = dCode;
        in.poses = poses;
        app.draw(in);
        const double r0 = nowS();
        for (unsigned s = 0; s < spp; s += 4) app.renderer.render(std::min(4u, spp - s), s > 0);
        std::printf("  t %.3f s: %d tanecik, cizim %.0f ms (%u ornek)\n", tt, in.n, (nowS() - r0) * 1000.0, spp);
    };
    if (mode == "still") {
        drawAt(t);
        const std::string path = out.empty() ? app.root + "/captures/laboratuvar/" + deneyS + ".png" : out;
        save(app, path);
        std::printf("yazildi: %s\n", path.c_str());
    } else {
        const std::string dir = out.empty() ? app.root + "/captures/laboratuvar/" + deneyS : out;
        for (int k = 0; k < frames; ++k) {
            const double tt = frames > 1 ? t0 + (t1 - t0) * k / (frames - 1) : t0;
            drawAt(tt);
            char name[64];
            std::snprintf(name, sizeof(name), "/f_%03d.png", k);
            save(app, dir + name);
        }
        std::printf("yazildi: %s (%d kare)\n", dir.c_str(), frames);
    }
    cudaFree(dPos);
    cudaFree(dCode);
    rec.release();
    w.destroy();
    app.release();
    app.renderer.shutdown();
    return 0;
}
