// Scene 4 "Tanecik": the man standing, and pushed (T3), with the flesh still riding its bones (a plain ragdoll):
//   --scene tanecik --balance-test [ayakta|merdiven]
//       ayakta    6 s on his feet with no push: still up (head above 1.55 m), the centre of mass within 3 cm of where
//                 it started
//       merdiven  a push at the chest, forward, at 1 s; a ladder of impulses (N s), each run from a fresh start: how far
//                 the centre of mass went, whether he is up 4 s later. The same ladder with the ankle strategy off (a
//                 statue on stiff ankles) for comparison. PASS: a pistol's and a Desert Eagle's momentum (3, 9 N s) do
//                 not move him more than 3 cm, the strongest push (120 N s, 2 m/s on 60 kg) puts him down.
//   --scene tanecik --balance-clip <impulse N s> [--out path]   frames (30 fps) and an mp4 of one push
#include "scenes/tanecik/balance.h"
#include "scenes/tanecik/tanecik_app.h"
#include "core/image_io.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

struct Run {
    float maxShift = 0.0f;   // the centre of mass's greatest horizontal distance from its start (m)
    float headEnd = 0.0f;    // the head's height at the end (m)
    bool up = false;
    const char* phase = "";
};

struct Bench {
    TanecikApp& app;
    SkeletonSim sim;
    BalanceController bal;
    int sub = 20, iters = 4;
    float h = 0.0f;
    explicit Bench(TanecikApp& a) : app(a) {}
    bool reset() {
        const float s = app.body.spacing;
        if (!sim.init(app.graph, app.body, app.cfg.f("beden.yogunluk", 1000.0f) * s * s * s, true)) return false;
        SkelGround gr;
        gr.radius = 0.5f * s;
        gr.muStatic = app.cfg.f("denge.zemin_mus", 0.9f);
        gr.muDynamic = app.cfg.f("denge.zemin_mud", 0.7f);
        sim.setGround(app.graph, app.body, gr, app.cfg.i("denge.temas_seyrek", 6));
        bal.init(sim);
        bal.ankleKp = app.cfg.f("denge.bilek_kp", bal.ankleKp);
        bal.ankleKd = app.cfg.f("denge.bilek_kd", bal.ankleKd);
        sub = app.cfg.i("iskelet.alt_adim", 20);
        iters = app.cfg.i("iskelet.yineleme", 4);
        h = 1.0f / (60.0f * sub);
        return true;
    }
    // one frame (1/60 s): the controller reads, sets the muscles, the skeleton steps
    void frame() {
        std::vector<SkelBody> b;
        sim.readBodies(b);
        bal.update(b, 1.0f / 60.0f);
        bal.apply(sim);
        for (int k = 0; k < sub; ++k) sim.step(h, make_float3(0, -9.81f, 0), iters, app.cfg.f("iskelet.surtunme", 2.0f), nullptr);
    }
    void pushChest(float J) {
        std::vector<SkelBody> b;
        sim.readBodies(b);
        sim.push(B_CHEST, make_float3(0, 0, J), b[B_CHEST].x);
    }
    Run run(float J, double seconds, bool ankles) {
        Run r;
        if (!reset()) return r;
        bal.ankleStrategy = ankles;
        float3 c0{};
        bool haveC0 = false;
        const int frames = int(seconds * 60.0);
        for (int f = 0; f < frames; ++f) {
            if (f == 60 && J > 0.0f) pushChest(J);
            frame();
            if (app.cfg.i("denge.izle", 0) && f % 6 == 0) {   // a trace, to see how he falls
                std::vector<SkelBody> b;
                sim.readBodies(b);
                std::printf("    %.2f s: km (%.3f %.3f %.3f) v (%.2f %.2f %.2f) govde egik %.1f der, bas %.3f, diz L %.3f, ayak L y %.4f R y %.4f, "
                            "leğen %.3f, evre %s\n",
                            f / 60.0, bal.com.x, bal.com.y, bal.com.z, bal.comVel.x, bal.comVel.y, bal.comVel.z, bal.trunkTilt * 57.2958f, b[B_HEAD].x.y,
                            b[B_SHIN_L].x.y, b[B_FOOT_L].x.y, b[B_FOOT_R].x.y, b[B_PELVIS].x.y, bal.phaseName());
            }
            if (f == 59) { c0 = bal.com; haveC0 = true; }
            if (haveC0) {
                const float dx = bal.com.x - c0.x, dz = bal.com.z - c0.z;
                r.maxShift = std::fmax(r.maxShift, std::sqrt(dx * dx + dz * dz));
            }
        }
        std::vector<SkelBody> b;
        sim.readBodies(b);
        r.headEnd = b[B_HEAD].x.y;
        r.up = r.headEnd > 1.4f;
        r.phase = bal.phaseName();
        return r;
    }
};

}   // namespace

int runBalanceTests(TanecikApp& app, const std::string& only) {
    app.build();
    Bench B(app);
    int failed = 0, ran = 0;
    if (only.empty() || only == "ayakta") {
        ++ran;
        const Run r = B.run(0.0f, 6.0, true);
        const bool ok = r.up && r.headEnd > 1.55f && r.maxShift < 0.03f;
        std::printf("[ayakta] 6 sn: bas %.3f m (> 1,55), kutle merkezi en cok %.1f mm kaydi (< 30), evre %s\n  -> ayakta: %s\n", r.headEnd,
                    r.maxShift * 1000.0f, r.phase, ok ? "GECTI" : "KALDI");
        failed += !ok;
    }
    if (only.empty() || only == "merdiven") {
        ++ran;
        const float ladder[] = {3, 9, 20, 30, 40, 50, 60, 80, 100, 120};
        B.reset();
        std::printf("[merdiven] gogse one itki (1. sn), 5 sn sonra; kutle %.1f kg\n", B.sim.totalMass());
        std::printf("  itki N s   hiz m/s   bilek stratejisi: kayma / bas / sonuc      heykel: kayma / bas / sonuc\n");
        bool small = true, bigFalls = false;
        for (float J : ladder) {
            const Run a = B.run(J, 5.0, true), s = B.run(J, 5.0, false);
            const float v = J / B.sim.totalMass();
            auto word = [](const Run& r) { return !r.up ? "dustu" : r.maxShift < 0.03f ? "kipirdamadi" : "sallandi"; };
            std::printf("  %7.0f   %7.2f   %8.1f cm / %.2f m / %-12s   %6.1f cm / %.2f m / %s\n", J, v, a.maxShift * 100.0f, a.headEnd, word(a),
                        s.maxShift * 100.0f, s.headEnd, word(s));
            if (J <= 9.0f && (!a.up || a.maxShift >= 0.03f)) small = false;
            if (J >= 120.0f && !a.up) bigFalls = true;
        }
        const bool ok = small && bigFalls;
        std::printf("  tabanca ve Desert Eagle (3, 9 N s) 3 cm'den az: %s; 120 N s dusurur: %s\n  -> merdiven: %s\n", small ? "evet" : "hayir",
                    bigFalls ? "evet" : "hayir", ok ? "GECTI" : "KALDI");
        failed += !ok;
    }
    std::printf("denge testleri: %d / %d gecti\n", ran - failed, ran);
    return failed ? 1 : 0;
}

int runBalanceClip(TanecikApp& app, float J, const std::string& outPrefix) {
    app.width = 1280;
    app.height = 720;
    app.renderer.init(app.width, app.height, app.root);
    std::string log;
    if (!app.renderer.compileDevice(log)) { std::printf("DERLEME HATASI\n%s\n", log.c_str()); return 1; }
    app.build();
    app.upload();
    app.applyLook(app.cfg.i("gorunum.cam", 1) != 0);
    app.cfg.set("kamera.yaw_deg", "70");
    app.setCamera();
    Bench B(app);
    if (!B.reset()) return 1;
    std::filesystem::create_directories(outPrefix);
    const int frames = 30 * 5;
    Xform xf[B_COUNT];
    for (int f = 0; f < frames; ++f) {
        B.sim.transforms(xf);
        app.applyTransforms(xf);
        for (unsigned k = 0; k < 48; k += 16) app.renderer.render(16, k > 0);
        std::vector<uchar4> px;
        app.renderer.download(px);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/f_%05d.png", outPrefix.c_str(), f);
        writePNG(path, int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(px.data()));
        for (int k = 0; k < 2; ++k) {   // 1/30 s
            if (f * 2 + k == 60 && J > 0.0f) B.pushChest(J);
            B.frame();
        }
    }
    const std::string mp4 = outPrefix + ".mp4";
    const std::string cmd = "ffmpeg -loglevel error -y -framerate 30 -i \"" + outPrefix + "/f_%05d.png\" -c:v libx264 -pix_fmt yuv420p -crf 18 \"" + mp4 + "\"";
    const int rc = std::system(cmd.c_str());
    std::printf("%s %s\n", rc == 0 ? "video:" : "VIDEO YAPILAMADI:", mp4.c_str());
    app.release();
    app.renderer.shutdown();
    return 0;
}
