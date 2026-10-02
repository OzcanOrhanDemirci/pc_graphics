// Scene 4 "Tanecik": command-line tools.
//   --scene tanecik --still [--look cam|deri] [--particles hepsi|seyrek|yok] [--spp N] [--yaw deg] [--pitch deg]
//                   [--dist m] [--target x,y,z] [--fov deg] [--width W --height H] [--out path] [--set key=value]
//       one picture of the figure in the studio, accumulated (no window)
//   --scene tanecik --stats      the body's numbers: particles, volume, mass, per bone, skin layer, the meshes, the graph
//   --still ... --poz dirsek|kol|diz|egil|karma   the bones turned by hand (no physics): the skin and bones follow
//   --scene tanecik --surface-test [ad]   the liquid's surface (tanecik_surface_test.cpp)
//   --scene tanecik --skeleton-test [ad] / --skeleton-clip ad   the skeleton's physics alone (tanecik_skeleton_test.cpp)
//   --still ... --kan-deneme      a still pool and a trickle of blood placed by hand (no physics): the liquid's look
//   --scene tanecik --weapons-test [ad]   weapons and blast (tanecik_weapons_test.cpp)
//   --scene tanecik --balance-test [ad] / --balance-clip <N s>   standing and pushed (tanecik_balance_test.cpp)
//   --scene tanecik --test [ad]  the particle solver's tests; --clip <ad> [--seconds S]: its clips (tanecik_tests.cpp)
#include "scenes/tanecik/tanecik_app.h"
#include "scenes/tanecik/body_graph.h"
#include "core/image_io.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

int runTanecikTests(TanecikApp& app, const std::string& only);
int runTanecikClip(TanecikApp& app, const std::string& name, const std::string& out, double seconds);

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
    size_t bv = 0, bt = 0;
    for (const TriMesh& m : app.boneMeshes) { bv += m.pos.size(); bt += m.idx.size(); }
    std::printf("kemik: %zu kose, %zu ucgen, %zu kemik\n", bv, bt, app.boneMeshes.size());
    // the body as the physics will see it: kinds, bonds, bone clusters, joints
    const BodyGraph& g = app.graph;
    std::printf("cizge: doku %zu, deri %zu, kemik %zu tanecik\n", g.countKind(PK_TISSUE), g.countKind(PK_SKIN), g.countKind(PK_BONE));
    std::printf("  bag: %zu (doku %zu, deri zari %zu, kemige %zu), tanecik basina %.1f\n", g.bonds.size(), g.countBonds(BK_TISSUE), g.countBonds(BK_SKIN),
                g.countBonds(BK_ATTACH), 2.0 * g.bonds.size() / std::max<size_t>(1, b.pos.size()));
    std::vector<int> deg(b.pos.size(), 0);
    for (const BodyBond& bd : g.bonds) { ++deg[bd.a]; ++deg[bd.b]; }
    size_t orphans = 0, weak = 0;
    for (size_t i = 0; i < deg.size(); ++i) {
        if (g.kind[i] == PK_BONE) continue;   // a bone particle is held by its cluster
        orphans += deg[i] == 0;
        weak += deg[i] > 0 && deg[i] < 4;
    }
    std::printf("  bagsiz doku/deri tanecigi: %zu, 4'ten az bagli: %zu\n", orphans, weak);
    std::printf("  kemik kumeleri:");
    for (int k = 0; k < B_COUNT; ++k) std::printf(" %s %zu%s", boneInfo(k).name, g.boneParticles[k].size(), k + 1 < B_COUNT ? "," : "\n");
    std::printf("  eklem: %zu\n", g.joints.size());
    return 0;
}

}   // namespace

int runSurfaceTests(TanecikApp& app, const std::string& only);   // tanecik_surface_test.cpp
int runSkeletonTests(TanecikApp& app, const std::string& only);  // tanecik_skeleton_test.cpp
int runSkeletonClip(TanecikApp& app, const std::string& name, const std::string& outPrefix);
int runWeaponsTests(const std::string& only);                      // tanecik_weapons_test.cpp
int runBalanceTests(TanecikApp& app, const std::string& only);   // tanecik_balance_test.cpp
int runBalanceClip(TanecikApp& app, float J, const std::string& outPrefix);

int runTanecikCli(int argc, char** argv) {
    TanecikApp app;
    std::string surfaceTest;
    bool surface = false;
    std::string out, look = "cam", parts = "hepsi", poseName;
    bool still = false, doStats = false, bloodTest = false, skelTest = false;
    std::string skelOnly, skelClip, balOnly, balClip;
    bool balTest = false;
    bool test = false;
    std::string only, clip;
    double seconds = -1.0;
    unsigned spp = 512;
    std::vector<std::pair<std::string, std::string>> camSet;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--scene") next();
        else if (a == "--still") still = true;
        else if (a == "--stats") doStats = true;
        else if (a == "--surface-test") { surface = true; if (i + 1 < argc && argv[i + 1][0] != '-') surfaceTest = next(); }
        else if (a == "--weapons-test") return runWeaponsTests(i + 1 < argc && argv[i + 1][0] != '-' ? std::string(argv[i + 1]) : std::string());
        else if (a == "--test") { test = true; if (i + 1 < argc && argv[i + 1][0] != '-') only = next(); }
        else if (a == "--clip") clip = next();
        else if (a == "--seconds") seconds = std::stod(next());
        else if (a == "--look") look = next();
        else if (a == "--particles") parts = next();
        else if (a == "--poz") poseName = next();
        else if (a == "--kan-deneme") bloodTest = true;
        else if (a == "--skeleton-test") { skelTest = true; if (i + 1 < argc && argv[i + 1][0] != '-') skelOnly = next(); }
        else if (a == "--skeleton-clip") skelClip = next();
        else if (a == "--balance-test") { balTest = true; if (i + 1 < argc && argv[i + 1][0] != '-') balOnly = next(); }
        else if (a == "--balance-clip") balClip = next();
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
    if (skelTest) return runSkeletonTests(app, skelOnly);
    if (balTest) return runBalanceTests(app, balOnly);
    if (!balClip.empty()) return runBalanceClip(app, std::stof(balClip), out.empty() ? app.root + "/captures/tanecik/denge_" + balClip : out);
    if (!skelClip.empty()) return runSkeletonClip(app, skelClip, out.empty() ? app.root + "/captures/tanecik/iskelet_" + skelClip : out);
    if (surface) return runSurfaceTests(app, surfaceTest);
    if (test) return runTanecikTests(app, only);
    if (!clip.empty()) return runTanecikClip(app, clip, out, seconds);
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
    if (!poseName.empty()) {
        float3 turn[B_COUNT];
        for (float3& t : turn) t = make_float3(0, 0, 0);
        auto hingeAxis = [&](int child) {
            for (const JointDef& j : app.graph.joints)
                if (j.child == child) return j.axis;
            return make_float3(1, 0, 0);
        };
        const float d = 3.14159265f / 180.0f;
        const bool all = poseName == "karma";
        if (all || poseName == "dirsek") turn[B_FARM_L] = hingeAxis(B_FARM_L) * (90 * d);
        if (all || poseName == "kol") {   // the left arm raised sideways, the elbow a little bent
            turn[B_UARM_L] = make_float3(0, 0, 1) * (70 * d);
            if (!all) turn[B_FARM_L] = hingeAxis(B_FARM_L) * (60 * d);
        }
        if (all || poseName == "diz") {   // the right thigh forward, the knee bent
            turn[B_THIGH_R] = make_float3(-1, 0, 0) * (60 * d);
            turn[B_SHIN_R] = hingeAxis(B_SHIN_R) * (90 * d);
        }
        if (all || poseName == "egil") turn[B_CHEST] = make_float3(1, 0, 0) * (25 * d);
        app.applyPose(turn);
        std::printf("poz: %s\n", poseName.c_str());
    }
    if (bloodTest) {   // a pool by his left foot, a trickle from his left hand into it: placed, not simulated
        const float s = app.body.spacing, r = 0.5f * s;
        std::vector<float3> p;
        unsigned h = 12345u;
        auto rnd = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (h & 0xffffff) / 16777216.0f - 0.5f; };
        const float3 c = make_float3(0.36f, 0.0f, 0.32f);
        for (int layer = 0; layer < 2; ++layer)
            for (float x = -0.26f; x <= 0.26f; x += s)
                for (float z = -0.2f; z <= 0.2f; z += s) {
                    const float e = (x * x) / (0.26f * 0.26f) + (z * z) / (0.2f * 0.2f);   // an uneven oval, thinner at the rim
                    if (e > 1.0f || (layer == 1 && e > 0.55f)) continue;
                    p.push_back(c + make_float3(x + 0.15f * s * rnd(), r + layer * s * 0.95f, z + 0.15f * s * rnd()));
                }
        const float3 tip = restJoints().handTip;
        for (float y = tip.y - 0.01f; y > 2.0f * s; y -= s * (y > 0.4f ? 1.0f : 1.8f))   // a thread, breaking into drops lower down
            p.push_back(make_float3(tip.x + 0.02f + 0.1f * s * rnd(), y, tip.z + 0.1f * s * rnd()));
        float3* dp = nullptr;
        cudaMalloc(reinterpret_cast<void**>(&dp), p.size() * sizeof(float3));
        cudaMemcpy(dp, p.data(), p.size() * sizeof(float3), cudaMemcpyHostToDevice);
        app.renderer.updateLiquid(dp, nullptr, int(p.size()), r);
        {   // and the floor as a canvas: a trail of drops soaked in, leading to the pool (as if he had walked there)
            std::vector<float3> t;
            for (int k = 0; k < 60; ++k) {
                const float u = k / 60.0f;
                const float3 q = make_float3(0.36f - 0.9f * u + 0.04f * rnd(), 0.0f, 0.32f + 0.7f * u + 0.04f * rnd());
                for (int m = 0; m < 3 + int(4 * (rnd() + 0.5f)); ++m) t.push_back(q + make_float3(0.012f * rnd(), 0, 0.012f * rnd()));
            }
            float3* dt = nullptr;
            cudaMalloc(reinterpret_cast<void**>(&dt), t.size() * sizeof(float3));
            cudaMemcpy(dt, t.data(), t.size() * sizeof(float3), cudaMemcpyHostToDevice);
            const float litres = 1000.0f * s * s * s;   // one particle's volume
            app.renderer.soak(dt, nullptr, int(t.size()), litres, 2.5f * s);
            cudaDeviceSynchronize();
            cudaFree(dt);
        }
        cudaDeviceSynchronize();
        std::printf("kan denemesi: %zu tanecik, %d ucgen, %d damla, %.2f ms\n", p.size(), app.renderer.liquid().triangleCount(),
                    app.renderer.liquid().dropletCount(), app.renderer.liquid().lastMs);
        cudaFree(dp);   // the surface keeps its own buffers
    }
    t0 = tnow();
    const unsigned batch = 16;
    for (unsigned s = 0; s < spp; s += batch) app.renderer.render(std::min(batch, spp - s), s > 0);
    std::printf("%u ornek (%.1f s)\n", spp, tnow() - t0);
    std::vector<uchar4> px;
    app.renderer.download(px);
    const std::string path = (out.empty() ? app.root + "/captures/tanecik_" + look : out) + ".png";
    writePNG(path, int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(px.data()));
    std::printf("yazildi: %s\n", path.c_str());
    app.release();
    app.renderer.shutdown();
    return 0;
}
