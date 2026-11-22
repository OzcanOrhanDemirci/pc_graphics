// Scene 2 "Fener": the command-line tools (the live scene is fener_live.cpp, inside the application).
//
//   pc_graphics.exe --scene fener --headless --frames N --out captures/x    a still: N jittered frames averaged at a
//                                                                            frozen time (offline quality), PNG
//   pc_graphics.exe --scene fener --headless --live N --out captures/x      N live frames at 30 fps: timings + frames
//   pc_graphics.exe --scene fener --headless --journey N --every k --out captures/x   the walk A -> B, N frames at 30 fps
//   pc_graphics.exe --scene fener --headless --player-test                   the player walked by script, frames saved
//   pc_graphics.exe --scene fener --headless --loop N --seconds T --frames K --out captures/x
//            a seamless loop: N stills (K jittered frames each) over T seconds, out/frame_0000.png ...; the waves are
//            rounded to periods that divide T (water.loop), the lamp is left to the settings (two beams: the picture
//            repeats every lamp.period / 2 s); the eye is adapted once, at the first moment
//   pc_graphics.exe --scene fener --map captures/harita.png                  top-down map (islands, forest, A, B, the walk)
//   pc_graphics.exe --scene fener --kararlilik --yol a|yolculuk --kare N --out captures/kararlilik/fener_a
//            steadiness and sharpness (Koy PLAN §9): N live frames of a path as the window draws them (TAA, jitter, the eye's
//            adaptation) and, at the same moments, a converged reference (--ref frames accumulated at a frozen time); then
//            python tools/kararlilik.py <out>. --hiz fps (time step, 41), --bas m (yolculuk: start this far from A, cruising)
//   options: --view A|B, --time t (seconds: beam angle, waves), --at m (a still from the walk), --width W, --height H,
//            --set key=value
#include "scenes/fener/fener_app.h"
#include "core/gpu_stats.h"

#include <ctime>

static std::string stamp() {
    std::time_t t = std::time(nullptr);
    char b[64];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", std::localtime(&t));
    return b;
}

static void printTimes(const FenerTimes& t) {
    std::printf("passes: surface %.2f  volume %.2f  resolve %.2f  compose %.2f  taa %.2f  glow %.2f  final %.2f  = %.2f ms\n", t.surface, t.volume,
                t.resolve, t.compose, t.taa, t.glow, t.final, t.total());
}

int runFenerCli(int argc, char** argv) {
    FenerApp app;
    bool headless = false;
    int frames = 0, live = 0, journeyFrames = 0, journeyEvery = 30;
    float atMeters = -1.0f;
    bool playerTest = false;
    int pview = 0;   // 1 surface only, 2 volume only, 3 depth, 4 normals
    int stab = 0, stabRef = 128;
    int loopFrames = 0;
    float loopSeconds = 0.0f;
    float stabFps = 41.0f, stabStart = 300.0f;
    std::string stabPath = "a";
    float t0 = -1.0f;
    std::string out, viewName, mapOut;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--scene") next();
        else if (a == "--headless") headless = true;
        else if (a == "--frames") frames = std::stoi(next());
        else if (a == "--live") live = std::stoi(next());
        else if (a == "--journey") journeyFrames = std::stoi(next());
        else if (a == "--at") atMeters = std::stof(next());
        else if (a == "--player-test") { playerTest = true; headless = true; }
        else if (a == "--pview") pview = std::stoi(next());
        else if (a == "--every") journeyEvery = std::max(1, std::stoi(next()));
        else if (a == "--out") out = next();
        else if (a == "--view") viewName = next();
        else if (a == "--time") t0 = std::stof(next());
        else if (a == "--width") app.width = unsigned(std::stoi(next()));
        else if (a == "--height") app.height = unsigned(std::stoi(next()));
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else if (a == "--map") mapOut = next();
        else if (a == "--kararlilik") { headless = true; stab = stab > 0 ? stab : 120; }
        else if (a == "--kare") stab = std::stoi(next());
        else if (a == "--ref") stabRef = std::stoi(next());
        else if (a == "--hiz") stabFps = std::stof(next());
        else if (a == "--bas") stabStart = std::stof(next());
        else if (a == "--yol") stabPath = next();
        else if (a == "--loop") { loopFrames = std::stoi(next()); headless = true; }
        else if (a == "--seconds") loopSeconds = std::stof(next());
        else { std::printf("unknown option %s\n", a.c_str()); return 2; }
    }
    if (!headless && mapOut.empty()) {
        std::printf("fener: --headless (--frames, --live, --journey, --player-test) or --map (the live scene opens from the application)\n");
        return 2;
    }
    if (loopFrames > 0 && loopSeconds <= 0.0f) {
        std::printf("fener: --loop needs --seconds\n");
        return 2;
    }
    if (loopFrames > 0) app.overrides.push_back("water.loop=" + std::to_string(loopSeconds));
    const int loopAcc = frames > 0 ? frames : 64;
    if (loopFrames > 0) frames = 0;
    app.still = headless && (frames > 0 || loopFrames > 0);
    if (stab > 0) { frames = 0; live = 0; journeyFrames = 0; }
    app.loadConfig();
    app.renderer.init(app.width, app.height, app.root, unsigned(app.cfg.i("volume.scale", 1)));
    std::printf("GPU: %s\n", app.renderer.deviceName().c_str());
    if (!app.compile()) return 1;
    gpuStatsInit();
    app.loadBlueNoise();
    app.applyConfig();
    if (viewName == "B") app.view = 1;
    app.rebuildScene();
    if (!mapOut.empty()) {
        const bool ok = fenerMap(app.scene, app.cfg, mapOut);
        std::printf("%s %s (journey %zu m)\n", ok ? "map" : "MAP FAILED", mapOut.c_str(), app.scene.journey.size());
        app.renderer.shutdown();
        return ok ? 0 : 1;
    }
    if (t0 < 0.0f) t0 = app.cfg.f("time", 0.0f);
    int rc = 0;
    app.renderer.params().view = pview;
    if (atMeters >= 0.0f) {   // a still from the walk: the camera at this many metres from A
        app.journey.target = 1;
        app.journey.d = std::min(atMeters, app.journey.length());
        app.applyJourney();
    }
    if (headless && loopFrames > 0) {
        const std::string o = out.empty() ? app.root + "/captures/fener_loop_" + stamp() : out;
        std::filesystem::create_directories(o);
        app.setTime(t0);
        app.frame(0);   // OptiX's lazy compilation
        app.renderer.resetHistory();
        for (int k = 0; k < loopAcc; ++k) app.frame(unsigned(k));
        app.adaptExposure(0.0f, true);   // the eye adapted once: the exposure stays the same through the loop
        const double s0 = fnow();
        for (int n = 0; n < loopFrames; ++n) {
            app.setTime(t0 + loopSeconds * float(n) / float(loopFrames));
            app.renderer.resetHistory();
            for (int k = 0; k < loopAcc; ++k) app.frame(unsigned(k));
            char nm[32];
            std::snprintf(nm, sizeof(nm), "/frame_%04d", n);
            app.save(o + nm);
        }
        std::printf("loop: %d frames over %.2f s, %d samples each, %.1f s\n", loopFrames, loopSeconds, loopAcc, fnow() - s0);
    } else if (headless && frames > 0) {
        app.setTime(t0);
        app.frame(0);   // first launch: OptiX finishes its lazy compilation
        app.renderer.resetHistory();
        const double s0 = fnow();
        for (int k = 0; k < frames; ++k) app.frame(unsigned(k));
        app.adaptExposure(0.0f, true);   // the still is shown as the adapted eye would see it: render once more
        app.renderer.resetHistory();
        for (int k = 0; k < frames; ++k) app.frame(unsigned(k));
        std::printf("still: %d frames in %.2f s, metered log2 %.2f, adaptation %+.2f EV\n", frames, fnow() - s0, app.renderer.meteredLog2(), app.evAdapt);
        printTimes(app.renderer.times());
        app.save(out.empty() ? app.root + "/captures/fener_still_" + stamp() : out);
    } else if (headless && playerTest) {
        // player check without a window: walk north from A into the cove, back, jump, run through the forest, swim;
        // then the lighthouse island. Positions printed, a frame saved at each step.
        const std::string o = out.empty() ? app.root + "/captures/fener_oyuncu_" + stamp() : out;
        struct Leg { const char* name; int mode; float f, r, yaw; bool run, jump; float seconds; };
        const Leg legs[] = {{"A'dan koya, suya", 1, 1, 0, 0.0f, false, false, 6.0f},  {"geri, kiyiya", 1, -1, 0, 0.0f, false, false, 8.0f},
                            {"zipla", 1, 0, 0, 0.0f, false, true, 1.2f},               {"guneye kos, ormana", 1, 1, 0, 3.1416f, true, false, 8.0f},
                            {"dogu, agaclar arasi", 1, 1, 0, 1.5708f, true, false, 8.0f}, {"fener adasi", 2, 0, 0, 0.0f, false, false, 0.5f},
                            {"kuleye yuru", 2, 1, 0, -0.18f, false, false, 6.0f},      {"kulubeye", 2, 1, 0, 1.75f, false, false, 14.0f}};
        int curMode = 0, n = 0;
        for (const Leg& L : legs) {
            if (L.mode != curMode) { app.mode = curMode = L.mode; app.placePlayer(L.mode); }
            if (L.f != 0.0f || L.r != 0.0f) app.player.yaw = L.yaw;
            const int steps = int(L.seconds * 60.0f);
            float maxY = app.player.feet.y;
            for (int k = 0; k < steps; ++k) {
                app.stepPlayer(L.f, L.r, L.run, L.jump && k == 0, 1.0f / 60.0f);
                maxY = std::max(maxY, app.player.feet.y);
            }
            const float g = fenerTerrain(app.player.feet.x, app.player.feet.z);
            std::printf("%-22s feet %8.1f %6.2f %8.1f  ground %6.2f  peak %+.2f  %s%s\n", L.name, app.player.feet.x, app.player.feet.y,
                        app.player.feet.z, g, maxY - g, app.player.onGround ? "yerde" : "havada", app.player.swimming ? " yuzuyor" : "");
            app.applyPlayerCamera();
            app.setTime(t0 + n);
            app.renderer.resetHistory();
            for (int k = 0; k < 24; ++k) app.frame(unsigned(k));
            app.adaptExposure(0.0f, true);
            for (int k = 0; k < 24; ++k) app.frame(unsigned(k));
            char nm[32];
            std::snprintf(nm, sizeof(nm), "_%d", n++);
            app.save(o + nm);
        }
    } else if (headless && journeyFrames > 0) {
        const std::string o = out.empty() ? app.root + "/captures/fener_yol_" + stamp() : out;
        app.journey.target = 1;
        double sum = 0;
        for (int k = 0; k < journeyFrames; ++k) {
            app.setTime(t0 + k / 30.0f);
            app.journey.step(1.0f / 30.0f, app.cfg.f("journey.speed", 12.0f), app.cfg.f("journey.accel", 1.0f));
            app.applyJourney();
            const double s = fnow();
            app.frame(unsigned(k));
            app.adaptExposure(1.0f / 30.0f, k == 0);
            sum += fnow() - s;
            if (k % journeyEvery == 0 || k == journeyFrames - 1) {
                char nm[32];
                std::snprintf(nm, sizeof(nm), "_%05d", k);
                app.save(o + nm);
            }
        }
        std::printf("journey: %d frames, %.0f of %.0f m, %.2f ms/frame\n", journeyFrames, app.journey.d, app.journey.length(), 1000.0 * sum / journeyFrames);
        printTimes(app.renderer.times());
    } else if (headless && stab > 0) {
        // Koy PLAN §9: the live frames of a path and, at the same moments, a converged reference (tools/kararlilik.py)
        const std::string o = out.empty() ? app.root + "/captures/kararlilik/fener_" + stabPath : out;
        std::filesystem::create_directories(o);
        const bool walk = stabPath == "yolculuk";
        setPNGCompression(1);   // hundreds of frames: fast zlib, bigger files
        const float dt = 1.0f / stabFps;
        struct Moment { FCamera cam; float t, exposure; };
        std::vector<Moment> at(stab);
        if (walk) {   // already cruising, this far from A towards B
            app.journey.target = 1;
            app.journey.d = std::min(stabStart, app.journey.length());
            app.journey.v = app.cfg.f("journey.speed", 12.0f);
            app.applyJourney();
        }
        app.renderer.resetHistory();
        double sum = 0.0;
        for (int k = 0; k < stab; ++k) {   // pass 1: live, as the window draws it
            const float t = t0 + k * dt;
            app.setTime(t);
            if (walk) {
                app.journey.step(dt, app.cfg.f("journey.speed", 12.0f), app.cfg.f("journey.accel", 1.0f));
                app.applyJourney();
            }
            at[k] = {app.renderer.params().cam, t, app.renderer.params().exposure};
            const double s = fnow();
            app.frame(unsigned(k));
            sum += fnow() - s;
            app.adaptExposure(dt, k == 0);
            char nm[32];
            std::snprintf(nm, sizeof(nm), "/live_%04d", k);
            app.save(o + nm);
        }
        std::printf("kararlilik fener %s: %d canli kare, %.2f ms/kare\n", stabPath.c_str(), stab, 1000.0 * sum / stab);
        app.still = true;   // pass 2: the reference, a progressive average of jittered frames at each frozen moment
        app.applyConfig();
        for (int k = 0; k < stab; ++k) {
            app.renderer.params().cam = at[k].cam;
            app.renderer.params().exposure = at[k].exposure;
            app.setTime(at[k].t);
            app.renderer.resetHistory();
            for (int j = 0; j < stabRef; ++j) app.frame(unsigned(j));
            char nm[32];
            std::snprintf(nm, sizeof(nm), "/ref_%04d", k);
            app.save(o + nm);
        }
        std::printf("kararlilik fener %s: %d referans (%d kare birikim) -> %s\n", stabPath.c_str(), stab, stabRef, o.c_str());
    } else if (headless && live > 0) {
        const std::string o = out.empty() ? app.root + "/captures/fener_live_" + stamp() : out;
        double sum = 0;
        for (int k = 0; k < live; ++k) {
            app.setTime(t0 + k / 30.0f);
            const double s = fnow();
            app.frame(unsigned(k));
            if (k >= 5) sum += fnow() - s;
            if (k == live / 2 || k == live - 1) app.save(o + "_f" + std::to_string(k));
        }
        std::printf("live %ux%u: %.2f ms/frame wall (%.1f fps)\n", app.width, app.height, 1000.0 * sum / std::max(1, live - 5),
                    std::max(1, live - 5) / sum);
        printTimes(app.renderer.times());
    }
    gpuStatsShutdown();
    app.renderer.shutdown();
    return rc;
}
