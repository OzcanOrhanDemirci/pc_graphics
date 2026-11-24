// Scene 3 "Yikim": the command-line tools (the live scene is yikim_live.cpp, inside the application).
//
//   pc_graphics.exe --scene yikim --test [name]      physics tests (dusme, yigin, egim, konsol, mafsal, enerji, determinizm,
//                                                    cerceve, bina, kontrol)
//   pc_graphics.exe --scene yikim --clip kolon|sarsinti --seconds S --out captures/x    frames (30 fps) + an mp4 (ffmpeg)
//   pc_graphics.exe --scene yikim --still T --out captures/x     the scenario simulated to T seconds, one clean frame
//   options: --scenario kolon|sarsinti, --spp N, --width W, --height H (or --w, --h), --set key=value
//            (deprem.magnitude=3..9), --root dir
//   --clip ... --slow f        slow motion: every saved frame (30 a second of video) moves the scenario on by f/30 s
//                              (0.25: four times slower) and every frame is saved; the solver steps at most 1/60 s
//                              at a time (finer where a frame is shorter: the same event, sampled finer). --seconds
//                              is the scenario's time
//   --clip ... --path <file>   the camera per saved frame (src/core/cam_path.h), frames frame_NNNN.png: as many as
//                              the path has poses, each at the 7th column's scenario time if every line has one,
//                              else at frame x f/30 (f from --slow, else 1). A line: "x y z yaw pitch fov [time]",
//                              x y z the camera in metres, yaw 0 along +z growing towards +x and pitch up positive,
//                              radians; fov vertical, degrees. With --slow or --path the scenario's own camera is
//                              printed in that form
#include "scenes/yikim/yikim_app.h"
#include "core/cam_path.h"
#include "core/image_io.h"
#include "scenes/yikim/fly_camera.h"

#include <filesystem>
#include <map>
#include <set>


// a clip's own timing (--slow, --path): the scenario's time at each step and each step's length, and the step each
// saved frame is taken after
struct ClipPlan {
    double slow = 0.0;                        // > 0: a saved frame is slow/30 s of the scenario
    const std::vector<CamPose>* path = nullptr;
    bool timed = false;                       // the path gives each frame's time
    std::vector<double> stepT, stepH;         // per step k: the time after it, its length (step 0: the start, none)
    std::vector<int> frameStep;               // per saved frame: the step it follows
    bool custom() const { return slow > 0.0 || path; }
    bool build(double seconds) {
        const double f = slow > 0.0 ? slow : 1.0, frameDt = f / 30.0;
        std::vector<double> T;
        if (path) {
            for (size_t i = 0; i < path->size(); ++i) T.push_back(timed ? (*path)[i].time : double(i) * frameDt);
        } else {
            const int n = int(std::round(seconds / frameDt));
            for (int i = 0; i <= n; ++i) T.push_back(double(i) * frameDt);
        }
        stepT.assign(1, 0.0);
        stepH.assign(1, 0.0);
        frameStep.clear();
        double cur = 0.0;
        for (double t : T) {
            if (t < cur - 1e-9) {
                std::printf("yikim: yolun zamani geri gidiyor (%.4f s, once %.4f s)\n", t, cur);
                return false;
            }
            const double gap = t - cur;
            const int n = gap <= 1e-9 ? 0 : std::max(1, int(std::ceil(gap * 60.0 - 1e-6)));
            for (int j = 1; j <= n; ++j) {
                stepT.push_back(j == n ? t : cur + gap * j / n);
                stepH.push_back(gap / n);
            }
            if (n > 0) cur = t;
            frameStep.push_back(int(stepT.size()) - 1);
        }
        return true;
    }
};

static int runClip(YikimApp& app, double seconds, double still, unsigned spp, const std::string& outPrefix, const ClipPlan& plan) {
    namespace fs = std::filesystem;
    const bool clip = still < 0.0;
    const bool custom = clip && plan.custom();
    const std::string dir = clip ? outPrefix : fs::path(outPrefix).parent_path().string();
    if (!dir.empty()) fs::create_directories(dir);
    const double dt = 1.0 / 60.0;
    const int frames = custom ? int(plan.stepT.size()) - 1 : int(std::round((clip ? seconds : still) / dt));
    size_t nextFrame = 0;   // custom: the next saved frame
    std::vector<uchar4> px;
    double physMs = 0, rendMs = 0;
    int shots = 0;
    // clips and stills are raw by default (many samples); display.denoise_clip = 1 (filter) or 2 (AI) shows the window's look
    app.renderer.denoiseMode = app.cfg.i("display.denoise_clip", 0);
    app.renderer.filterPasses(app.cfg.i("display.filter_passes", 4));
    // the roof: the structure's pieces whose top is at the top storey's level (not the brick parapet standing on it:
    // measured, a parapet thrown off by the shaking read as the roof falling 9.7 m from a building still standing)
    std::vector<int> roof;
    double roofY = 0;
    for (const Piece& p : app.pieces)
        if (p.kind != PK_WALL) roofY = std::max(roofY, p.hi.y);
    for (const Piece& p : app.pieces)
        if (p.kind != PK_WALL && p.hi.y > roofY - 1e-6) roof.push_back(p.body);
    std::vector<double3> start;
    double maxDrift = 0, maxDrop = 0, tMaxDrift = 0;
    bool caught = false;
    std::vector<float3> prevVel;
    double secMs = 0;
    float secMax = 0;
    float fastest = 0, ballFastest = 0;
    int fastestBody = -1;
    double fastestT = 0, fastestY = 0;
    std::vector<float3> kickPrev;
    std::set<int> kickBodies, kickUpBodies;
    int kicks = 0;
    float kickUp = 0, kickSide = 0;
    for (int k = 0; k <= frames; ++k) {
        const double t = custom ? plan.stepT[k] : k * dt;
        app.events(t);
        if (k > 0) {
            app.world.step(float(custom ? plan.stepH[k] : dt));
            physMs += app.world.lastMs;
            secMs += app.world.lastMs;
            secMax = std::max(secMax, app.world.lastMs);
        }
        // diagnosis (test.iso): every second, how far the base (the pieces whose foot was on the ground: the seismic
        // tower's mat) has moved across relative to the ground, now
        if (app.cfg.i("test.iso", 0) && k > 0 && k % 60 == 0) {
            double sx = 0, sz = 0;
            int nb = 0;
            for (const Piece& p : app.pieces)
                if (p.kind == PK_SLAB && p.lo.y < 1e-6) {
                    sx += app.world.pos[p.body].x - start[p.body].x;
                    sz += app.world.pos[p.body].z - start[p.body].z;
                    ++nb;
                }
            if (nb > 0) {
                const double gx = app.world.pos[0].x - start[0].x, gz = app.world.pos[0].z - start[0].z;
                std::printf("      taban t=%5.1f s: zemine gore %.3f m\n", t, std::sqrt((sx / nb - gx) * (sx / nb - gx) + (sz / nb - gz) * (sz / nb - gz)));
            }
        }
        // diagnosis (test.perf): every second, the physics step's mean and worst time over that second
        if (app.cfg.i("test.perf", 0) && k > 0 && k % 60 == 0) {
            const PhysStats st = app.world.stats();
            std::printf("      hiz t=%5.1f s: fizik ort. %5.2f ms, en kotu %5.2f ms, %6d cift, %5d temas yamasi\n", t, secMs / 60.0, secMax, st.pairs,
                        st.contacts);
            secMs = 0;
            secMax = 0;
        }
        app.world.download();
        if (start.empty()) start = app.world.pos;
        {
            double dx = 0, dz = 0, dy = 0;
            for (int b : roof) {
                dx += app.world.pos[b].x - start[b].x;
                dz += app.world.pos[b].z - start[b].z;
                dy += app.world.pos[b].y - start[b].y;
            }
            dx = dx / roof.size() - (app.world.pos[0].x - start[0].x);
            dz = dz / roof.size() - (app.world.pos[0].z - start[0].z);
            dy = dy / roof.size() - (app.world.pos[0].y - start[0].y);
            const double drift = std::sqrt(dx * dx + dz * dz);
            if (drift > maxDrift) { maxDrift = drift; tMaxDrift = t; }
            maxDrop = std::max(maxDrop, -dy);
        }
        // diagnosis (test.watch = body, test.watch_from = s): the body frame by frame, with its living interfaces
        if (app.cfg.i("test.watch", -1) > 0 && t >= app.cfg.f("test.watch_from", 0.0f) && t <= app.cfg.f("test.watch_from", 0.0f) + 0.4) {
            const int b = app.cfg.i("test.watch", -1);
            std::vector<unsigned> fl;
            std::vector<float3> fo;
            app.world.fibres(fl, fo);
            size_t fi = 0;
            std::string ifs;
            for (const InterfaceDesc& d : app.world.interfaceDescs) {
                int alive = 0;
                float fmax = 0;
                for (size_t q = 0; q < d.fibers.size(); ++q, ++fi)
                    if (!(fl[fi] & 4u)) { ++alive; fmax = std::max(fmax, length(fo[fi])); }
                if ((d.a == b || d.b == b) && alive) {
                    char buf[96];
                    std::snprintf(buf, sizeof(buf), " [%d-%d %d lif, %.0f N]", d.a, d.b, alive, fmax);
                    ifs += buf;
                }
            }
            std::printf("      izle t=%.3f  y %.2f  v (%.1f %.1f %.1f) |v| %.1f  w %.1f  canli arayuz:%s\n", t, app.world.pos[b].y, app.world.vel[b].x,
                        app.world.vel[b].y, app.world.vel[b].z, length(app.world.vel[b]), length(app.world.angVel[b]), ifs.c_str());
        }
        // catcher (test.catch = speed): the first piece that jumps above it (3x its last frame's speed) is reported
        if (app.cfg.f("test.catch", 0.0f) > 0.0f && !caught) {
            if (prevVel.size() == app.world.vel.size())
                for (size_t i = 1; i < app.world.vel.size() && !caught; ++i) {
                    const float sp = length(app.world.vel[i]), sp0 = length(prevVel[i]);
                    if (app.world.kinematic[i] || sp < app.cfg.f("test.catch", 0.0f) || sp < 3.0f * sp0) continue;
                    caught = true;
                    const int b = int(i);
                    std::printf("YAKALANDI t=%.3f govde %d (tur %d, %.0f kg) y %.2f: |v| %.1f -> %.1f m/s, v (%.1f %.1f %.1f)\n", t, b, app.world.info[b].tag,
                                app.world.info[b].mass, app.world.pos[b].y, sp0, sp, app.world.vel[b].x, app.world.vel[b].y, app.world.vel[b].z);
                    std::vector<unsigned> fl;
                    std::vector<float3> fo;
                    app.world.fibres(fl, fo);
                    size_t fi = 0;
                    for (const InterfaceDesc& d : app.world.interfaceDescs) {
                        int alive = 0;
                        float fmax = 0;
                        for (size_t q = 0; q < d.fibers.size(); ++q, ++fi)
                            if (!(fl[fi] & 4u)) { ++alive; fmax = std::max(fmax, length(fo[fi])); }
                        if ((d.a == b || d.b == b) && alive)
                            std::printf("    arayuz %d-%d: %d canli lif, en buyuk kuvvet %.0f N\n", d.a, d.b, alive, fmax);
                    }
                    for (const PhysWorld::PatchInfo& pi : app.world.patchesOf(b))
                        std::printf("    temas: govde %d (tur %d, %.0f kg, |v| %.1f), normal kuvvet %.0f N, n (%.2f %.2f %.2f)\n", pi.other,
                                    pi.other > 0 ? app.world.info[pi.other].tag : -1, app.world.info[pi.other].mass, length(app.world.vel[pi.other]), pi.force,
                                    pi.n.x, pi.n.y, pi.n.z);
                }
            prevVel = app.world.vel;
        }
        // diagnosis (test.kick = dv m/s): brick pieces whose speed relative to the ground jumps by more than dv in one
        // frame (a throw, not a fall: gravity adds 0.16 m/s a frame). The first test.kick_n are shown with what touches them.
        if (app.cfg.f("test.kick", 0.0f) > 0.0f) {
            const float dvMin = app.cfg.f("test.kick", 0.0f);
            if (kickPrev.size() == app.world.vel.size()) {
                const float3 g0 = app.world.vel[0], g0p = kickPrev[0];
                for (size_t i = 1; i < app.world.vel.size(); ++i) {
                    if (app.world.kinematic[i] || app.world.info[i].tag != 3) continue;
                    if (app.world.pos[i].y - app.world.pos[0].y < app.cfg.f("test.kick_min_y", 0.0f)) continue;   // e.g. not the debris on the ground
                    const float3 vr = app.world.vel[i] - g0, vp = kickPrev[i] - g0p;
                    const float gain = length(vr) - length(vp);
                    kickUp = std::max(kickUp, vr.y);
                    kickSide = std::max(kickSide, std::sqrt(vr.x * vr.x + vr.z * vr.z));
                    if (vr.y > 2.0f) kickUpBodies.insert(int(i));
                    if (gain <= dvMin) continue;
                    kickBodies.insert(int(i));
                    ++kicks;
                    if (kicks > app.cfg.i("test.kick_n", 8)) continue;
                    const int b = int(i);
                    std::printf("FIRLAMA t=%.3f govde %d (%.0f kg) y %.2f: |v| %.2f -> %.2f m/s, v (%.2f %.2f %.2f) -> (%.2f %.2f %.2f), |w| %.1f rad/s\n", t, b,
                                app.world.info[b].mass, app.world.pos[b].y, length(vp), length(vr), vp.x, vp.y, vp.z, vr.x, vr.y, vr.z,
                                length(app.world.angVel[b]));
                    std::vector<unsigned> fl;
                    std::vector<float3> fo;
                    app.world.fibres(fl, fo);
                    size_t fi = 0;
                    for (const InterfaceDesc& d : app.world.interfaceDescs) {
                        int alive = 0;
                        float fmax = 0;
                        for (size_t q = 0; q < d.fibers.size(); ++q, ++fi)
                            if (!(fl[fi] & 4u)) { ++alive; fmax = std::max(fmax, length(fo[fi])); }
                        if ((d.a == b || d.b == b) && alive)
                            std::printf("    arayuz %d-%d: %d canli lif, en buyuk kuvvet %.0f N\n", d.a, d.b, alive, fmax);
                    }
                    for (const PhysWorld::PatchInfo& pi : app.world.patchesOf(b))
                        std::printf("    temas: govde %d (tur %d, %.0f kg, v (%.2f %.2f %.2f)), normal kuvvet %.0f N, n (%.2f %.2f %.2f)\n", pi.other,
                                    pi.other > 0 ? app.world.info[pi.other].tag : -1, app.world.info[pi.other].mass, app.world.vel[pi.other].x - g0.x,
                                    app.world.vel[pi.other].y - g0.y, app.world.vel[pi.other].z - g0.z, pi.force, pi.n.x, pi.n.y, pi.n.z);
                }
            }
            kickPrev = app.world.vel;
        }
        // the fastest piece: a fall from the roof is ~17 m/s; much more is energy the solver made up (a thrown ball
        // is counted on its own)
        for (size_t i = 1; i < app.world.vel.size(); ++i) {
            if (app.world.kinematic[i]) continue;
            if (app.world.info[i].sphere > 0.0f) {
                ballFastest = std::max(ballFastest, length(app.world.vel[i]));
                continue;
            }
            const float sp = length(app.world.vel[i]);
            if (sp > fastest) {
                fastest = sp;
                fastestBody = int(i);
                fastestT = t;
                fastestY = app.world.pos[i].y;
            }
        }
        // diagnosis (test.profile_at = s, repeatable as a list "s s s"): one frame kernel by kernel at those times
        {
            const auto it = app.cfg.values.find("test.profile_at");
            if (it != app.cfg.values.end()) {
                const char* c = it->second.c_str();
                char* end = nullptr;
                for (double v = std::strtod(c, &end); end != c; v = std::strtod(c, &end)) {
                    if (std::fabs(t - v) < 0.5 * dt) {
                        const PhysStats st = app.world.stats();
                        std::printf("      t=%.2f s: %d temas yamasi, %d cift\n", t, st.contacts, st.pairs);
                        app.world.profile(float(dt));
                    }
                    c = end;
                }
            }
        }
        // diagnosis (test.track): every second, the fastest piece now, where it is and what touches it
        if (app.cfg.i("test.track", 0) && k % 60 == 0) {
            int b = -1;
            float sp = 0;
            for (size_t i = 1; i < app.world.vel.size(); ++i)
                if (!app.world.kinematic[i] && app.world.info[i].sphere == 0.0f && length(app.world.vel[i]) > sp) { sp = length(app.world.vel[i]); b = int(i); }
            if (b > 0) {
                std::printf("      iz t=%5.1f en hizli govde %d (tur %d, %.0f kg): %.1f m/s, v (%.1f %.1f %.1f), y %.1f m, temas:", t, b, app.world.info[b].tag,
                            app.world.info[b].mass, sp, app.world.vel[b].x, app.world.vel[b].y, app.world.vel[b].z, app.world.pos[b].y);
                for (const PhysWorld::PatchInfo& pi : app.world.patchesOf(b)) std::printf(" [%d %.0f kN]", pi.other, pi.force / 1000.0f);
                std::printf("\n");
            }
        }
        // diagnosis (test.trace = interface index): one interface frame by frame
        if (app.cfg.i("test.trace", -1) >= 0 && k <= 12) {
            const int ti = app.cfg.i("test.trace", -1);
            std::vector<unsigned> fl;
            std::vector<float3> fo;
            app.world.fibres(fl, fo);
            size_t fi = 0;
            for (int q = 0; q < ti; ++q) fi += app.world.interfaceDescs[q].fibers.size();
            const InterfaceDesc& d = app.world.interfaceDescs[ti];
            app.world.download();
            std::printf("      iz t=%.3f  a=%d v(%.3f %.3f %.3f) w(%.2f %.2f %.2f)  b=%d v(%.3f %.3f %.3f) w(%.2f %.2f %.2f)\n", t, d.a, app.world.vel[d.a].x,
                        app.world.vel[d.a].y, app.world.vel[d.a].z, app.world.angVel[d.a].x, app.world.angVel[d.a].y, app.world.angVel[d.a].z, d.b,
                        app.world.vel[d.b].x, app.world.vel[d.b].y, app.world.vel[d.b].z, app.world.angVel[d.b].x, app.world.angVel[d.b].y,
                        app.world.angVel[d.b].z);
            std::printf("         lifler:");
            for (size_t q = 0; q < d.fibers.size(); ++q) std::printf(" [%u %.0f %.0f %.0f]", fl[fi + q], fo[fi + q].x, fo[fi + q].y, fo[fi + q].z);
            std::printf("\n");
        }
        // diagnosis (test.verbose): the cracked and broken fibres as the run starts, by the kinds of piece they join
        if (app.cfg.i("test.verbose", 0) && k == 0) {
            std::vector<unsigned> fl;
            std::vector<float3> fo;
            app.world.fibres(fl, fo);
            const char* kn[] = {"kolon", "kiris", "doseme", "duvar", "diger"};
            std::map<std::string, std::pair<int, int>> tally;   // cracked, broken
            size_t fi = 0;
            for (const InterfaceDesc& d : app.world.interfaceDescs) {
                const int ka = d.a > 0 ? app.world.info[d.a].tag : -1, kb = d.b > 0 ? app.world.info[d.b].tag : -1;
                const std::string key = std::string(ka >= 0 ? kn[ka] : "zemin") + "-" + (kb >= 0 ? kn[kb] : "zemin");
                for (size_t q = 0; q < d.fibers.size(); ++q, ++fi) {
                    if (fl[fi] & 1u) ++tally[key].first;
                    if (fl[fi] & 4u) ++tally[key].second;
                }
            }
            for (const auto& e : tally)
                if (e.second.first || e.second.second) std::printf("      oturmada %-16s catlak %5d  kirik %5d\n", e.first.c_str(), e.second.first, e.second.second);
        }
        // diagnosis (test.verbose): the first interfaces to break, by the kinds they join and where
        static bool reportedBreak = false;
        if (app.cfg.i("test.verbose", 0) && !reportedBreak && k > 0 && k % 3 == 0) {
            std::vector<unsigned> fl;
            std::vector<float3> fo;
            app.world.fibres(fl, fo);
            size_t fi = 0;
            int shown = 0;
            const char* kn[] = {"kolon", "kiris", "doseme", "duvar", "diger"};
            for (const InterfaceDesc& d : app.world.interfaceDescs) {
                int broken = 0, cracked = 0;
                for (size_t q = 0; q < d.fibers.size(); ++q, ++fi) {
                    if (fl[fi] & 4u) ++broken;
                    if (fl[fi] & 1u) ++cracked;
                }
                if (broken > 0 && shown < 12) {
                    const int ka = app.world.info[d.a].tag, kb = app.world.info[d.b].tag;
                    std::printf("      [%d] t=%.2f kirik %d/%zu (catlak %d)  %s-%s  yuz (%.2f %.2f %.2f) normal (%.0f %.0f %.0f)\n", int(&d - app.world.interfaceDescs.data()), t, broken, d.fibers.size(),
                                cracked, ka >= 0 ? kn[ka] : "zemin", kb >= 0 ? kn[kb] : "zemin", d.fibers[0].p.x, d.fibers[0].p.y, d.fibers[0].p.z,
                                d.n.x, d.n.y, d.n.z);
                    ++shown;
                }
            }
            if (shown) reportedBreak = true;
        }
        int want = 0;   // frames saved after this step
        if (custom)
            while (nextFrame + want < plan.frameStep.size() && plan.frameStep[nextFrame + want] == k) ++want;
        else
            want = (clip ? (k % 2 == 0) : (k == frames)) ? 1 : 0;
        if (want == 0) continue;
        app.renderer.updateWorld(app.world);
        app.world.download();
        app.renderer.params().groundY = float(app.world.pos[0].y);
        app.renderer.params().groundShift = make_float2(float(app.world.pos[0].x), float(app.world.pos[0].z));
        for (int w = 0; w < want; ++w) {
            if (plan.path) {
                const CamPose& q = (*plan.path)[size_t(shots)];
                FlyCamera fc;
                fc.pos = make_float3(float(q.x), float(q.y), float(q.z));
                fc.yaw = q.yaw;
                fc.pitch = q.pitch;
                fc.fovDeg = q.fov;
                fc.apply(app.renderer.params().cam, app.width, app.height);
            }
            app.renderer.render(spp, false);
            rendMs += app.renderer.lastMs;
            app.renderer.download(px);
            char name[512];
            if (clip && plan.path) std::snprintf(name, sizeof(name), "%s/%s.png", outPrefix.c_str(), camPathFrameName(shots).c_str());
            else if (clip) std::snprintf(name, sizeof(name), "%s/f_%05d.png", outPrefix.c_str(), shots);
            else std::snprintf(name, sizeof(name), "%s.png", outPrefix.c_str());
            writePNG(name, int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(px.data()));
            ++shots;
        }
        if (custom) nextFrame += size_t(want);
        if (k % 60 == 0 || !clip) {
            const PhysStats st = app.world.stats();
            std::printf("t=%5.2f s  kirik lif %5d  catlak %5d  temas yamasi %4d  fizik %.2f ms  goruntu %.1f ms\n", t, st.broken, st.cracked, st.contacts,
                        app.world.lastMs, app.renderer.lastMs);
        }
    }
    std::printf("%d kare; fizik ort. %.2f ms/kare, goruntu ort. %.1f ms/kare\n", shots, physMs / std::max(1, frames), rendMs / std::max(1, shots));
    std::printf("cati: zemine gore en buyuk yatay oteleme %.3f m (t=%.2f s, yukseklige oran %.2f%%), en buyuk cokme %.3f m\n", maxDrift, tMaxDrift,
                100.0 * maxDrift / roofY, maxDrop);
    if (app.cfg.f("test.kick", 0.0f) > 0.0f)
        std::printf("firlama (tugla, zemine gore, karede > %.1f m/s hizlanma): %d olay, %zu parca; yukari > 2 m/s: %zu parca; en buyuk yukari %.2f m/s, "
                    "en buyuk yatay %.2f m/s\n", app.cfg.f("test.kick", 0.0f), kicks, kickBodies.size(), kickUpBodies.size(), kickUp, kickSide);
    {
        const double fall = std::sqrt(2.0 * 9.81 * (roofY + 1.0));
        std::printf("en hizli parca: %.1f m/s (govde %d, tur %d, t=%.2f s, y=%.1f m); catidan serbest dusus %.1f m/s\n", fastest, fastestBody,
                    fastestBody > 0 ? app.world.info[fastestBody].tag : -1, fastestT, fastestY, fall);
    }
    if (app.struck > 0) {   // the wrecking ball: where it ended, and what the struck column's pieces did
        app.world.download();
        const double3 bp = app.world.pos[app.struck];
        std::printf("yikim topu: en hizli %.1f m/s, son konum (%.2f %.2f %.2f)\n", ballFastest, bp.x, bp.y, bp.z);
        const float3 ij = app.cfg.v3("darbe.kolon", make_float3(0, 0, 0));
        const double x = ij.x * app.spec.spanX, z = ij.y * app.spec.spanZ;
        for (const Piece& p : app.pieces)
            if (p.kind == PK_COLUMN && p.hi.y <= app.spec.level(1) + 1e-6 && std::fabs((p.lo.x + p.hi.x) * 0.5 - x) < 0.01 &&
                std::fabs((p.lo.z + p.hi.z) * 0.5 - z) < 0.01) {
                const double3 d = app.world.pos[p.body] - start[p.body];
                std::printf("    kolon parcasi %d (y %.2f-%.2f m): yer degistirme %.2f m (yatay %.2f, dusey %.2f)\n", p.body, p.lo.y, p.hi.y,
                            std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), std::sqrt(d.x * d.x + d.z * d.z), d.y);
            }
    }
    if (app.cfg.i("test.verbose", 0)) {   // the pieces that sank most (the broken-out ones excluded: they fell on purpose)
        std::vector<int> order;
        for (const Piece& p : app.pieces) order.push_back(p.body);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return start[a].y - app.world.pos[a].y > start[b].y - app.world.pos[b].y; });
        const char* kn[] = {"kolon", "kiris", "doseme", "duvar", "diger"};
        int shown = 0;
        for (int b : order) {
            const Piece* pc = nullptr;
            for (const Piece& p : app.pieces)
                if (p.body == b) pc = &p;
            std::printf("      govde %4d %-7s  baslangic (%.2f %.2f %.2f)  cokme %.3f m\n", b, kn[pc->kind], start[b].x, start[b].y, start[b].z,
                        start[b].y - app.world.pos[b].y);
            if (++shown == 14) break;
        }
    }
    if (clip) {
        const std::string mp4 = outPrefix + ".mp4";
        const std::string cmd = "ffmpeg -loglevel error -y -framerate 30 -i \"" + outPrefix + (plan.path ? "/frame_%04d.png" : "/f_%05d.png") +
                                "\" -c:v libx264 -pix_fmt yuv420p -crf 18 \"" + mp4 + "\"";
        const int rc = std::system(cmd.c_str());
        std::printf("%s %s\n", rc == 0 ? "video:" : "VIDEO YAPILAMADI:", mp4.c_str());
    } else {
        std::printf("kare: %s.png\n", outPrefix.c_str());
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------
// the live window
// ---------------------------------------------------------------------------------------------------------

int runYikimCli(int argc, char** argv) {
    YikimApp app;
    std::string only, out;
    bool test = false;
    double seconds = 6.0, still = -1.0;
    unsigned spp = 16;
    std::string clipName;
    bool sizeGiven = false;
    double slow = 0.0;
    std::string pathFile;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--scene") next();
        else if (a == "--test") { test = true; if (i + 1 < argc && argv[i + 1][0] != '-') only = next(); }
        else if (a == "--clip") clipName = next();
        else if (a == "--scenario") app.scenario = next();
        else if (a == "--still") still = std::stod(next());
        else if (a == "--seconds") seconds = std::stod(next());
        else if (a == "--spp") spp = unsigned(std::stoi(next()));
        else if (a == "--width") { app.width = unsigned(std::stoi(next())); sizeGiven = true; }
        else if (a == "--height") { app.height = unsigned(std::stoi(next())); sizeGiven = true; }
        else if (a == "--w") { app.width = unsigned(std::stoi(next())); sizeGiven = true; }
        else if (a == "--h") { app.height = unsigned(std::stoi(next())); sizeGiven = true; }
        else if (a == "--slow") slow = std::stod(next());
        else if (a == "--path") pathFile = next();
        else if (a == "--out") out = next();
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else { std::printf("unknown option %s\n", a.c_str()); return 2; }
    }
    app.loadConfig();
    if (test) return runYikimTests(app.cfg, only);
    if (!clipName.empty()) app.scenario = clipName;
    if (clipName.empty() && still < 0.0) {
        std::printf("yikim: --test, --clip or --still (the live scene opens from the application)\n");
        return 2;
    }
    (void)sizeGiven;
    std::vector<CamPose> poses;
    ClipPlan plan;
    plan.slow = slow;
    if (!pathFile.empty()) {
        std::string perr;
        if (!readCamPath(pathFile, poses, perr, &plan.timed)) {
            std::printf("yikim: %s\n", perr.c_str());
            return 1;
        }
        plan.path = &poses;
    }
    if (still < 0.0 && plan.custom() && !plan.build(seconds)) return 1;
    app.renderer.init(app.width, app.height, app.root);
    std::string log;
    const double t0 = ynow();
    if (!app.renderer.compileDevice(log)) { std::printf("DERLEME HATASI\n%s\n", log.c_str()); return 1; }
    std::printf("yikim programi derlendi (%.1f s)\n", ynow() - t0);
    app.build();
    app.renderer.uploadWorld(app.world);
    app.applyRender();
    app.setCamera();
    if (plan.custom()) {   // the scenario's camera as a path line
        const YCamera& c = app.renderer.params().cam;
        std::printf("yikim: kamera (yol bicimiyle) %.3f %.3f %.3f %.4f %.4f %.1f\n", c.pos.x, c.pos.y, c.pos.z, std::atan2(c.fwd.x, c.fwd.z),
                    std::asin(std::max(-1.0f, std::min(1.0f, c.fwd.y))), 2.0f * std::atan(c.tanHalfY) * 180.0f / 3.14159265f);
        if (still < 0.0)
            std::printf("yikim: %zu kare, senaryo %.3f s, %zu adim%s\n", plan.frameStep.size(), plan.stepT.back(), plan.stepT.size() - 1,
                        plan.path ? (plan.timed ? ", zaman yoldan" : ", zaman kare x f/30") : "");
    }
    const std::string prefix = out.empty() ? app.root + "/captures/yikim_" + app.scenario : out;
    const int rc = runClip(app, seconds, still, still >= 0.0 ? std::max(spp, 64u) : spp, prefix, plan);
    app.renderer.shutdown();
    return rc;
}
