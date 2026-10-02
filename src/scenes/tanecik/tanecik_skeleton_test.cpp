// Scene 4 "Tanecik": the skeleton's physics on its own (no flesh yet): tests and a clip.
//   --scene tanecik --skeleton-test [asili|serbest]
//       asili    hung by the chest, limbs raised by hand and let go: they fall, swing on their joints and come to rest.
//                The joints hold (the two pivots within 1 mm), no limit is passed by more than 2 degrees, nothing runs
//                away (no body faster than a free fall from its height allows), the energy goes down (joint friction)
//       serbest  no gravity, no pin, spun hard: the joints hold under the fling, the limits too; with no friction the
//                angular momentum stays within 5% (only the joints act: inner forces)
//   --scene tanecik --skeleton-clip asili|serbest [--out path]   frames (30 fps) and an mp4; the flesh rides its bones
#include "scenes/tanecik/skeleton_sim.h"
#include "scenes/tanecik/tanecik_app.h"
#include "core/image_io.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;

struct Setup {
    bool pinChest = false;
    float3 g = make_float3(0, 0, 0);
    float damping = 0.0f;
    float3 spin = make_float3(0, 0, 0);
};

// the scenario's start: limbs raised by hand (forward kinematics), the skeleton placed there
Setup start(TanecikApp& app, SkeletonSim& sim, const std::string& name) {
    float3 turn[B_COUNT];
    for (float3& t : turn) t = make_float3(0, 0, 0);
    Setup s;
    if (name == "asili") {
        turn[B_UARM_L] = make_float3(0, 0, 1) * (90 * kDeg);     // the left arm out sideways, level (within its cone)
        turn[B_UARM_R] = make_float3(-1, 0, 0) * (80 * kDeg);    // the right arm forward
        turn[B_THIGH_L] = make_float3(-1, 0, 0) * (60 * kDeg);   // the left thigh forward
        s.pinChest = true;
        s.g = make_float3(0, -app.cfg.f("iskelet.yercekimi", 9.81f), 0);
        s.damping = 2.0f;
    } else {   // serbest
        turn[B_UARM_L] = make_float3(0, 0, 1) * (40 * kDeg);
        turn[B_THIGH_R] = make_float3(-1, 0, 0) * (30 * kDeg);
        s.spin = make_float3(1.0f, 9.0f, 2.0f);
    }
    Xform xf[B_COUNT];
    forwardKinematics(app.graph, turn, Xform(), xf);
    sim.setPose(xf);
    if (s.pinChest) sim.pin(B_CHEST, true);
    if (s.spin.x != 0 || s.spin.y != 0 || s.spin.z != 0) sim.setVelocity(make_float3(0, 0, 0), s.spin);
    return s;
}

// the flesh moves with its bone here (no flesh physics yet): each segment as heavy as all its particles, at the body's
// mean density
float segmentParticleMass(TanecikApp& app) {
    const float s = app.body.spacing;
    return app.cfg.f("beden.yogunluk", 1000.0f) * s * s * s;
}

int runOne(TanecikApp& app, const std::string& name) {
    SkeletonSim sim;
    if (!sim.init(app.graph, app.body, segmentParticleMass(app), true)) { std::printf("iskelet kurulamadi\n"); return 1; }
    const Setup su = start(app, sim, name);
    const int sub = app.cfg.i("iskelet.alt_adim", 20), iters = app.cfg.i("iskelet.yineleme", 4);
    const float h = 1.0f / (60.0f * sub);
    const double seconds = name == "asili" ? 5.0 : 3.0;
    const SkelStats s0 = sim.stats(su.g);
    float gap = 0, over = 0, speed = 0;
    SkelStats last = s0;
    std::printf("[%s] alt adim %d (h %.3f ms), yineleme %d, eklem surtunmesi %.1f/s\n", name.c_str(), sub, h * 1000.0f, iters, su.damping);
    std::printf("  baslangic: eklem acikligi %.3f mm, sinir asimi %.2f derece\n", s0.maxGap * 1000.0f, s0.maxOver / kDeg);
    const int frames = int(seconds * 60.0);
    for (int f = 1; f <= frames; ++f) {
        for (int k = 0; k < sub; ++k) sim.step(h, su.g, iters, su.damping, nullptr);
        cudaDeviceSynchronize();
        if (f % 1 == 0) {
            last = sim.stats(su.g);
            gap = std::fmax(gap, last.maxGap);
            over = std::fmax(over, last.maxOver);
            if (last.maxSpeed > speed && last.maxSpeed > 5.0f)
                std::printf("  %.2f s: %s %.2f m/s\n", f / 60.0, boneInfo(last.fastest).name, last.maxSpeed);
            speed = std::fmax(speed, last.maxSpeed);
            if (f % 60 == 0)
                std::printf("  %.1f s: eklem acikligi %.3f mm, sinir asimi %.2f derece, en hizli %.2f m/s, enerji %.3f J, |L| %.4f\n", f / 60.0,
                            last.maxGap * 1000.0f, last.maxOver / kDeg, last.maxSpeed, last.energy, std::sqrt(dot(last.L, last.L)));
        }
    }
    bool ok = gap < 1e-3f && over < 2.0f * kDeg && std::isfinite(last.energy);
    std::printf("  en buyuk eklem acikligi %.3f mm (< 1), sinir asimi %.2f derece (< 2)\n", gap * 1000.0f, over / kDeg);
    if (name == "asili") {
        // the fastest a body could go: falling the arm's span (0.75 m) freely, sqrt(2 g h) = 3.8 m/s; a margin for the swing
        ok = ok && speed < 6.0f && last.energy < s0.energy;
        std::printf("  en hizli %.2f m/s (< 6), enerji %.3f -> %.3f J (azalmali)\n", speed, s0.energy, last.energy);
    } else {
        const float L0 = std::sqrt(dot(s0.L, s0.L)), L1 = std::sqrt(dot(last.L, last.L));
        const float dL = std::sqrt(dot(last.L - s0.L, last.L - s0.L)) / std::fmax(L0, 1e-9f);
        ok = ok && dL < 0.05f && last.energy <= s0.energy * 1.02f;
        std::printf("  acisal momentum %.4f -> %.4f (fark %.1f%%, < 5), enerji %.3f -> %.3f J (artmamali)\n", L0, L1, dL * 100.0f, s0.energy,
                    last.energy);
    }
    std::printf("  -> %s: %s\n", name.c_str(), ok ? "GECTI" : "KALDI");
    return ok ? 0 : 1;
}

}   // namespace

int runSkeletonTests(TanecikApp& app, const std::string& only) {
    app.build();
    int failed = 0, ran = 0;
    for (const char* n : {"asili", "serbest"}) {
        if (!only.empty() && only != n) continue;
        ++ran;
        failed += runOne(app, n);
    }
    std::printf("iskelet testleri: %d / %d gecti\n", ran - failed, ran);
    return failed ? 1 : 0;
}

int runSkeletonClip(TanecikApp& app, const std::string& name, const std::string& outPrefix) {
    app.width = 1280;
    app.height = 720;
    app.renderer.init(app.width, app.height, app.root);
    std::string log;
    if (!app.renderer.compileDevice(log)) { std::printf("DERLEME HATASI\n%s\n", log.c_str()); return 1; }
    app.build();
    app.upload();
    app.applyLook(true);
    app.setCamera();
    SkeletonSim sim;
    if (!sim.init(app.graph, app.body, segmentParticleMass(app), true)) return 1;
    const Setup su = start(app, sim, name);
    const int sub = app.cfg.i("iskelet.alt_adim", 20), iters = app.cfg.i("iskelet.yineleme", 4);
    const float h = 1.0f / (60.0f * sub);
    const double seconds = name == "asili" ? 5.0 : 3.0;
    std::filesystem::create_directories(outPrefix);
    const int frames = int(seconds * 30.0);
    Xform xf[B_COUNT];
    for (int f = 0; f < frames; ++f) {
        sim.transforms(xf);
        app.applyTransforms(xf);
        for (unsigned k = 0; k < 64; k += 16) app.renderer.render(16, k > 0);
        std::vector<uchar4> px;
        app.renderer.download(px);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/f_%05d.png", outPrefix.c_str(), f);
        writePNG(path, int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(px.data()));
        for (int k = 0; k < 2 * sub; ++k) sim.step(h, su.g, iters, su.damping, nullptr);   // 1/30 s
    }
    const std::string mp4 = outPrefix + ".mp4";
    const std::string cmd = "ffmpeg -loglevel error -y -framerate 30 -i \"" + outPrefix + "/f_%05d.png\" -c:v libx264 -pix_fmt yuv420p -crf 18 \"" + mp4 + "\"";
    const int rc = std::system(cmd.c_str());
    std::printf("%s %s\n", rc == 0 ? "video:" : "VIDEO YAPILAMADI:", mp4.c_str());
    app.release();
    app.renderer.shutdown();
    return 0;
}
