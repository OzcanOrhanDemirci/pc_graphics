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

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

int runSolverTests(const Config& c, const std::string& only);
int runSurfaceTests(const Config& cfg, const std::string& only);
int runWeaponsTests(const std::string& only);
int runLabTests(LaboratuvarApp& app, const std::string& only);
int runCubeTests(const std::string& only);

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
    renderer.temporal = cfg.i("goruntu.zamansal", 1) != 0;
    renderer.temporalAlpha = cfg.f("goruntu.zamansal_alfa", 0.1f);
    renderer.temporalGamma = cfg.f("goruntu.zamansal_gama", 2.5f);
    renderer.denoise = cfg.i("goruntu.gurultu_giderici", 1) != 0;
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
    const float3 defPos[kDeneyCount] = {{0.12f, 0.24f, 0.95f}, {0.0f, 0.16f, 0.50f}, {0.02f, 0.17f, 0.58f}, {0.0f, 0.2f, 0.78f}, {0.10f, 0.26f, 1.25f}, {0.06f, 0.13f, 0.55f}, {0.0f, 0.035f, 0.13f}, {0.10f, 0.22f, 0.70f}};
    const float3 defAim[kDeneyCount] = {{-0.03f, 0.20f, 0.0f}, {0.0f, 0.06f, 0.0f}, {0.03f, 0.07f, 0.0f}, {0.0f, 0.06f, 0.0f}, {0.0f, 0.17f, 0.0f}, {0.0f, 0.05f, 0.0f}, {0.0f, 0.012f, 0.0f}, {0.0f, 0.06f, -0.02f}};
    cam.lookAt(cfg.v3(k + ".konum", defPos[int(d)]), cfg.v3(k + ".hedef", defAim[int(d)]));
    cam.fovDeg = cfg.f(k + ".fov", cfg.f("kamera.fov", 30.0f));
    // a lens: a little depth of field, in focus at the experiment (the live window refocuses on the picture's middle)
    cam.aperture = cfg.f("kamera.diyafram", 0.0f);
    cam.focus = length(cfg.v3(k + ".hedef", defAim[int(d)]) - cam.pos);
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
    LMaterial flesh = mat(LM_WET, cfg.v3("karpuz.et_renk", make_float3(0.86f, 0.12f, 0.15f)), 0.035f, 5000.0f);
    flesh.pattern = LP_FLESH;
    const int mFlesh = add(flesh);
    const int mPith = add(mat(LM_WET, cfg.v3("karpuz.ic_kabuk_renk", make_float3(0.62f, 0.72f, 0.46f)), 0.03f, 3000.0f));
    LMaterial juice = mat(LM_LIQUID, make_float3(1, 1, 1));
    juice.ior = 1.34f;
    juice.sigma = cfg.v3("karpuz.su_sogurma", make_float3(6.0f, 110.0f, 80.0f));
    const int mJuice = add(juice);
    LMaterial gel = mat(LM_LIQUID, make_float3(1, 1, 1));
    gel.ior = cfg.f("jel.kirilma", 1.345f);
    gel.sigma = cfg.v3("jel.sogurma", make_float3(2.5f, 3.5f, 10.0f));
    const int mGel = add(gel);
    const int mMilk = add(mat(LM_WET, cfg.v3("damla.renk", make_float3(0.86f, 0.86f, 0.84f)), 0.03f, 5000.0f));
    // moulded ABS (the Küp experiment's cubes): a glossy coat over its colour (each cube's own), edges rounded in the shading
    const int mPlastic = add(mat(LM_DIFFUSE, cfg.v3("kup.renk", make_float3(1.0f, 1.0f, 1.0f)), cfg.f("kup.f0", 0.045f), cfg.f("kup.parlaklik", 700.0f)));
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
    if (d == Deney::Bal) {
        FluidSurfaceSettings k = S;
        k.smoothingPasses = cfg.i("bal.yuzey_gecis", 2);   // the thread is one to three particles thick: smooth it twice
        surface(DC_HONEY, mHoney, k);
    }
    if (d == Deney::Kum) {
        FluidSurfaceSettings k = S;
        k.smoothing = cfg.f("kum.yuzey_yumusatma", 0.5f);
        k.dropletSpheres = true;
        surface(DC_SAND, mSand, k);
        R.setDropletsVisible(surf[DC_SAND], false);   // a lone grain in the air: its fine grains only, no bead
        grainsPer = cfg.i("kum.tane_sayisi", 4);
        grainsCap = maxParticles * grainsPer;
        objGrains = R.addSpheres(grainsCap, mSand, true);
    } else {
        objGrains = -1;
    }
    if (d == Deney::Karpuz) {
        FluidSurfaceSettings k = S;
        k.smoothing = cfg.f("karpuz.yuzey_yumusatma", 0.6f);
        k.supportScale = cfg.f("karpuz.yuzey_destek", 2.0f);   // tighter kernels: crisper fragment edges
        k.iso = cfg.f("karpuz.yuzey_seviye", 0.5f);
        surface(DC_RIND, mRind, k);
        // the white rind its own surface, or drawn with the green (karpuz.ic_kabuk_ayri 0): two shells of different
        // particles interleave where the rind breaks into a camouflage of small blobs
        if (cfg.i("karpuz.ic_kabuk_ayri", 0)) surface(DC_PITH, mPith, k);
        surface(DC_FLESH, mFlesh, k);
        surface(DC_JUICE, mJuice, S);
    }
    if (d == Deney::Damla) surface(DC_MILK, mMilk, S);
    if (d == Deney::Jel) {
        FluidSurfaceSettings k = S;
        k.smoothing = cfg.f("jel.yuzey_yumusatma", 0.5f);
        surface(DC_GEL, mGel, k);
        R.setDropletsVisible(surf[DC_GEL], cfg.i("jel.damla", 0) != 0);   // the crumbs a track spits out read as beads
    }
    objMembrane = -1;
    objCubes = -1;
    if (d == Deney::Kup) objCubes = R.addBoxes(maxParticles, mPlastic);
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

void LaboratuvarApp::setCubeColours(const uchar4* dColours, int n) {
    if (objCubes < 0 || !dColours) return;
    cudaMemcpy(renderer.boxColours(objCubes), dColours, size_t(n) * sizeof(uchar4), cudaMemcpyDeviceToDevice);
    if (std::getenv("KUP_TANI")) {
        uchar4 c[3];
        cudaMemcpy(c, renderer.boxColours(objCubes), sizeof(c), cudaMemcpyDeviceToHost);
        std::printf("kup renkleri: (%d %d %d) (%d %d %d) (%d %d %d), %d kup, nesne %d\n", c[0].x, c[0].y, c[0].z, c[1].x, c[1].y, c[1].z, c[2].x, c[2].y, c[2].z, n, objCubes);
    }
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
    if (objCubes >= 0 && in.cubePose) R.setBoxes(objCubes, in.cubePose, in.cubeCount);
    const int n = std::min(in.n, weightCap);
    for (int c = 0; c < kDrawCodes; ++c) {
        if (surf[c] < 0) continue;
        launchWeights(in.code, n, uint8_t(c), dWeight, st);
        if (c == DC_RIND && surf[DC_PITH] < 0) launchWeightsAdd(in.code, n, uint8_t(DC_PITH), dWeight, st);   // the white rind with it
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
        e.gun = c.i("test.silah", 1);
        break;
    }
    case Deney::Jel: {
        e.kind = LabEvent::Shot;
        e.a = c.v3("test.jel_namlu", make_float3(-1.5f, 0.05f, 0.0f));
        e.b = c.v3("test.jel_hedef", make_float3(0.0f, 0.05f, 0.0f));
        e.power = c.f("test.jel_guc", 0.5f);
        break;
    }
    case Deney::Damla: e.kind = LabEvent::Drop; break;
    case Deney::Kup: e.kind = LabEvent::Blast; e.power = c.f("test.kup_guc", 0.5f); break;
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
    bool trig = false, recorded = false, frozen = false;
    float pan = 0.0f;
    int shots = 1;
    int kupN = 0;
    float kupPower = -1.0f;
    double shotGap = 0.5;
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
        else if (a == "--kararlilik") mode = "stab";
        else if (a == "--dondur") frozen = true;
        else if (a == "--kaydir") pan = std::stof(next());   // --kararlilik: the camera slides sideways, metres a frame
        else if (a == "--test") { mode = "test"; testName = opt(); }
        else if (a == "--surface-test") { mode = "surface"; testName = opt(); }
        else if (a == "--weapons-test") { mode = "weapons"; testName = opt(); }
        else if (a == "--lab-test") { mode = "lab"; testName = opt(); }
        else if (a == "--kup-test") { mode = "kup"; testName = opt(); }
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
        else if (a == "--atis") shots = std::max(1, std::stoi(next()));        // the gel: so many shots (side by side)
        else if (a == "--kup") kupN = std::max(1, std::stoi(next()));          // Kup: so many cubes along an edge
        else if (a == "--siddet") kupPower = std::stof(next());                // Kup: the charge (0..1)
        else if (a == "--atis_ara") shotGap = std::stod(next());               // seconds between them
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
    if (mode == "kup") return runCubeTests(testName);
    if (mode != "still" && mode != "seq" && mode != "stab") {
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
    if (d == Deney::Kup && kupN > 0) {   // so many cubes an edge (the panel's nearest), allowed
        app.cfg.set("kup.en_cok_kenar", std::to_string(std::max(kupN, app.cfg.i("kup.en_cok_kenar", 100))));
        inputs.fineness = kupFineness(kupN, app.cfg.i("kup.en_cok_kenar", 100));
    }
    if (d == Deney::Kup && kupPower >= 0.0f) app.cfg.set("test.kup_guc", std::to_string(kupPower));
    const LabEvent ev = defaultEvent(d, app.cfg);
    // the triggers: the default one, and for the gel more shots after it, each a little higher and further back
    std::vector<LabEvent> evs{ev};
    for (int k = 1; k < shots && ev.kind == LabEvent::Shot; ++k) {
        LabEvent e = ev;
        const float3 off = make_float3(0.0f, 0.018f * float((k + 1) / 2) * (k % 2 ? 1.0f : -1.0f), 0.012f * float(k % 3) - 0.012f);
        e.a = e.a + off;
        e.b = e.b + off;
        e.at = k * shotGap;
        evs.push_back(e);
    }
    if (recorded) {
        const std::string pre = std::string(deneyKey(d)) + ".";
        const double cover = std::max(t, t1) + 0.01;
        rec.start(app.cfg, d, inputs, evs, std::max(0.05, cover - evs.back().at), 1.0 / app.cfg.f(pre + "kayit_hiz", 600.0f),
                  1.0 / app.cfg.f(pre + "kayit_seyrek_hiz", app.cfg.f("kayit.seyrek_hiz", 120.0f)), 0.0, size_t(app.cfg.f("kayit.butce_mb", 4096.0f)) << 20,
                  app.cfg.f(pre + "kayit_yogun", -1.0f));
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
    const int nCubes = d == Deney::Kup ? (recorded ? rec.cubeCount() : w.cubes.count()) : 0;
    if (d == Deney::Kup) maxN = std::max(maxN, nCubes);
    if (!app.initRenderer(log)) { std::printf("DERLEME HATASI\n%s\n", log.c_str()); return 1; }
    app.setupScene(d, std::max(maxN, 1024), &memTris);
    if (camGiven) {
        app.cam.lookAt(camPos, camAim);
        app.cam.focus = length(camAim - camPos);   // in focus where it looks
        app.cam.apply(app.renderer.params().cam, app.width, app.height);
    }
    float3* dPos = nullptr;
    uint8_t* dCode = nullptr;
    float4* dCubes = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&dPos), size_t(maxN + 1) * sizeof(float3));
    cudaMalloc(reinterpret_cast<void**>(&dCode), size_t(maxN + 1));
    if (nCubes > 0) {
        cudaMalloc(reinterpret_cast<void**>(&dCubes), size_t(nCubes) * 2 * sizeof(float4));
        // their colours: the world's (a recording's fine world built the same cubes from the same seed)
        LabWorld cw;
        if (recorded) cw.build(d, app.cfg, true, inputs);
        app.setCubeColours(recorded ? cw.cubes.deviceColours() : w.cubes.deviceColours(), nCubes);
    }
    PropPose poses[LabWorld::P_COUNT];
    double simT = 0.0;
    size_t nextEv = 1;   // the coarse world: the later shots as its time reaches them
    auto drawAt = [&](double tt) {
        DrawInput in;
        if (recorded) {
            if (nCubes) rec.cubesAt(tt, dCubes, app.renderer.stream());
            else rec.frameAt(tt, dPos, dCode, poses, app.renderer.stream());
            in.n = nCubes ? 0 : rec.drawnCount();
            in.spacing = rec.spacing();
            in.membraneFirst = rec.membraneFirst;
            in.membraneCount = rec.membraneCount;
        } else {
            while (simT + 1e-9 < tt) {
                w.step(1.0f / 60.0f);
                simT += 1.0 / 60.0;
                while (nextEv < evs.size() && trig && evs[nextEv].at <= simT + 1e-9) w.trigger(evs[nextEv++]);
            }
            w.exportDraw(dPos, dCode, app.renderer.stream());
            if (nCubes) w.exportCubes(dCubes, 1.0f, app.renderer.stream());
            for (int p = 0; p < LabWorld::P_COUNT; ++p) poses[p] = w.prop[p];
            in.n = w.count();
            in.spacing = w.s;
            in.membraneFirst = w.membraneFirst;
            in.membraneCount = w.membraneCount;
        }
        in.cubePose = dCubes;
        in.cubeCount = nCubes;
        in.pos = dPos;
        in.code = dCode;
        in.poses = poses;
        app.draw(in);
        const double r0 = nowS();
        for (unsigned s = 0; s < spp; s += 4) app.renderer.render(std::min(4u, spp - s), s > 0);
        std::printf("  t %.3f s: %d tanecik, cizim %.0f ms (%u ornek)\n", tt, in.n, (nowS() - r0) * 1000.0, spp);
    };
    if (mode == "stab") {
        // how steady the live picture is: the camera still, frames drawn as the live window draws them (a fresh
        // render each frame, the world stepping unless --dondur); every pixel's brightness over the frames after a
        // warm-up, its standard deviation (0..1 of the display's range) averaged and at the 95th percentile, and the
        // mean change from one frame to the next. A heat map of the deviation is written too.
        const int warm = 8, N = std::max(frames, warm + 4);
        const size_t np = size_t(app.width) * app.height;
        std::vector<double> sum(np, 0.0), sum2(np, 0.0);
        // swimming: 8x8 blocks' mean brightness (fine grain averages out in a block, a drifting blotch does not)
        const int bw = int(app.width) / 8, bh = int(app.height) / 8;
        std::vector<double> bsum(size_t(bw) * bh, 0.0), bsum2(size_t(bw) * bh, 0.0);
        std::vector<float> blk(size_t(bw) * bh);
        std::vector<float> prev(np, 0.0f);
        double diff = 0.0, msSum = 0.0;
        int used = 0;
        std::vector<uchar4> px;
        for (int k = 0; k < N; ++k) {
            if (!frozen) w.step(1.0f / 60.0f);
            if (pan != 0.0f) {
                app.cam.move(0.0f, 1.0f, 0.0f, pan);
                app.cam.apply(app.renderer.params().cam, app.width, app.height);
            }
            w.exportDraw(dPos, dCode, app.renderer.stream());
            if (nCubes) w.exportCubes(dCubes, 1.0f, app.renderer.stream());
            for (int q = 0; q < LabWorld::P_COUNT; ++q) poses[q] = w.prop[q];
            DrawInput in;
            in.pos = dPos;
            in.code = dCode;
            in.n = w.count();
            in.spacing = w.s;
            in.poses = poses;
            in.membraneFirst = w.membraneFirst;
            in.membraneCount = w.membraneCount;
            in.cubePose = dCubes;
            in.cubeCount = nCubes;
            app.draw(in);
            app.renderer.render(spp, false);
            app.renderer.download(px);
            if (k < warm) continue;
            msSum += app.renderer.lastMs;
            std::fill(blk.begin(), blk.end(), 0.0f);
            for (size_t i = 0; i < np; ++i) {
                const float l = (0.2126f * px[i].x + 0.7152f * px[i].y + 0.0722f * px[i].z) / 255.0f;
                const int bx = int(i % app.width) / 8, by = int(i / app.width) / 8;
                if (bx < bw && by < bh) blk[size_t(by) * bw + bx] += l * (1.0f / 64.0f);
                sum[i] += l;
                sum2[i] += double(l) * l;
                if (used > 0) diff += std::fabs(l - prev[i]);
                prev[i] = l;
            }
            for (size_t b = 0; b < blk.size(); ++b) {
                bsum[b] += blk[b];
                bsum2[b] += double(blk[b]) * blk[b];
            }
            ++used;
        }
        std::vector<float> bsd(blk.size());
        double bmean = 0.0;
        for (size_t b = 0; b < blk.size(); ++b) {
            const double m = bsum[b] / used;
            bsd[b] = float(std::sqrt(std::max(0.0, bsum2[b] / used - m * m)));
            bmean += bsd[b];
        }
        bmean /= double(std::max<size_t>(1, bsd.size()));
        std::sort(bsd.begin(), bsd.end());
        std::printf("kararlilik %s: cizim %.2f ms/kare\n", deneyS.c_str(), msSum / std::max(1, used));
        std::printf("kararlilik %s: dalgalanma (8x8 blok) ort %.5f, %%95 %.5f, %%99 %.5f\n", deneyS.c_str(), bmean, bsd[size_t(0.95 * (bsd.size() - 1))],
                    bsd[size_t(0.99 * (bsd.size() - 1))]);
        std::vector<float> sd(np);
        for (size_t i = 0; i < np; ++i) {
            const double m = sum[i] / used;
            sd[i] = float(std::sqrt(std::max(0.0, sum2[i] / used - m * m)));
        }
        std::vector<float> sorted = sd;
        std::sort(sorted.begin(), sorted.end());
        double mean = 0.0;
        for (float v : sd) mean += v;
        mean /= double(np);
        std::printf("kararlilik %s (%s, %u ornek, %d kare): sapma ort %.4f, %%95 %.4f, %%99 %.4f; kareden kareye ort fark %.4f\n", deneyS.c_str(),
                    frozen ? "dunya donuk" : "dunya akiyor", spp, used, mean, sorted[size_t(0.95 * (np - 1))], sorted[size_t(0.99 * (np - 1))],
                    diff / (double(np) * std::max(1, used - 1)));
        std::vector<unsigned char> heat(np * 4);
        for (size_t i = 0; i < np; ++i) {
            const float v = std::min(1.0f, sd[i] * 10.0f);
            heat[4 * i] = (unsigned char)(255.0f * std::min(1.0f, 2.0f * v));
            heat[4 * i + 1] = (unsigned char)(255.0f * std::max(0.0f, 2.0f * v - 1.0f));
            heat[4 * i + 2] = (unsigned char)(80.0f * (1.0f - v));
            heat[4 * i + 3] = 255;
        }
        const std::string base = out.empty() ? app.root + "/captures/laboratuvar/kararlilik_" + deneyS : out;
        std::filesystem::create_directories(std::filesystem::path(base).parent_path());
        writePNG(base + "_isi.png", int(app.width), int(app.height), heat.data());
        writePNG(base + "_kare.png", int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(px.data()));
    } else if (mode == "still") {
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
