// Scene 3 "Yikim": an earthquake and a reinforced-concrete building, the physics on the GPU.
//
//   yikim.exe                          the live window (full screen; --windowed: 1280x720; --autotest: keys on a timeline)
//   yikim.exe --test [name]            physics tests (dusme, yigin, egim, konsol, mafsal, enerji, determinizm, cerceve, bina)
//   yikim.exe --clip kolon|sarsinti --seconds S --out captures/x     frames (30 fps) + an mp4 (ffmpeg)
//   yikim.exe --still T --out captures/x          the scenario simulated to T seconds, one clean frame
//   options: --scenario kolon|sarsinti, --spp N, --width W, --height H, --set key=value, --root dir
#include "image_io.h"
#include "scene.h"   // Config
#include "yikim/building.h"
#include "yikim/fly_camera.h"
#include "yikim/quake.h"
#include "yikim/yikim_renderer.h"
#include "yikim/yikim_scenes.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <windows.h>
#include <GL/gl.h>
#include "stb_easy_font.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

int runYikimTests(const Config& cfg, const std::string& only);

static double ynow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
static double fnow2() { return ynow(); }

struct YikimApp {
    Config cfg;
    std::string root = PROJECT_ROOT;
    std::vector<std::string> overrides;
    PhysWorld world;
    std::vector<Piece> pieces;
    std::vector<int> loose;      // unglued bodies (roof tanks)
    FrameSpec spec;
    YikimRenderer renderer;
    unsigned int width = 1280, height = 720;
    std::string scenario = "kolon";
    QuakeRecord quake;
    bool quakeOn = false;
    double quakeT0 = 0;          // scenario time at which the record starts playing
    double quakeOffset = 0;      // seconds of the record skipped (its quiet start)
    float quakeScale = 1.0f;
    double3 groundBase = {0, 0, 0};
    bool removed = false;

    void loadConfig() {
        cfg = Config();
        cfg.load(root + "/scenes/yikim.cfg");
        for (const auto& o : overrides) {
            const size_t eq = o.find('=');
            if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
        }
    }
    void applyPhysics() {
        world.params.substeps = cfg.i("phys.substeps", world.params.substeps);
        world.params.interfaceIters = cfg.i("phys.interface_iters", world.params.interfaceIters);
        world.params.contactIters = cfg.i("phys.contact_iters", world.params.contactIters);
        world.params.muStatic = cfg.f("phys.mu_static", world.params.muStatic);
        world.params.muDynamic = cfg.f("phys.mu_dynamic", world.params.muDynamic);
        world.params.restitution = cfg.f("phys.restitution", world.params.restitution);
        world.params.dampAlpha = cfg.f("phys.damp_alpha", world.params.dampAlpha);
        world.params.maxDepenetration = cfg.f("phys.max_depenetration", world.params.maxDepenetration);
    }
    void build() {
        if (cfg.values.count("scene") && cfg.values.at("scene") == "cerceve") {
            spec = frameSpecFromConfig(cfg);
            pieces = buildFrame(world, spec, concreteFromConfig(cfg));
            loose.clear();
        } else {
            const ApartmentSpec as = apartmentSpecFromConfig(cfg);
            PhysMaterial mc = concreteFromConfig(cfg), mb = brickFromConfig(cfg);
            if (cfg.i("test.elastic", 0))   // diagnosis: nothing can break; what still falls is not glued
                for (PhysMaterial* m : {&mc, &mb}) m->ft = m->fc = m->cohesion = m->fy = 1e12f;
            Apartment ap = buildApartment(world, as, mc, mb);
            spec = as.frame;
            pieces = std::move(ap.pieces);
            loose = std::move(ap.loose);
        }
        applyPhysics();
        world.upload();
        {   // the building settles under its own weight before anything happens (hidden)
            const double t0 = ynow();
            world.settle(cfg.f("phys.settle_seconds", 2.0f), cfg.f("phys.settle_ramp", 0.5f), 1.0f / 60.0f);
            const PhysStats st = world.stats();
            std::printf("oturma: %.1f s, kirik lif %d, catlak %d (%.1f s surdu)\n", cfg.f("phys.settle_seconds", 2.0f), st.broken, st.cracked, ynow() - t0);
        }
        removed = false;
        quakeOn = false;
        std::printf("dunya: %zu govde, %zu arayuz\n", world.info.size(), world.interfaceDescs.size());
    }
    void applyRender() {
        YikimParams& p = renderer.params();
        const float D2R = 3.14159265f / 180.0f;
        const float az = cfg.f("sun.azimuth_deg", 215.0f) * D2R, el = cfg.f("sun.elevation_deg", 42.0f) * D2R;
        p.sunDir = make_float3(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
        p.sunCosMax = std::cos(cfg.f("sun.radius_deg", 0.27f) * D2R);
        p.sunE = cfg.v3("sun.E", make_float3(3.1f, 2.95f, 2.7f));
        p.skyZenith = cfg.v3("sky.zenith", make_float3(0.11f, 0.20f, 0.42f));
        p.skyHorizon = cfg.v3("sky.horizon", make_float3(0.42f, 0.48f, 0.56f));
        p.groundAlbedo = cfg.v3("ground.albedo", make_float3(0.30f, 0.29f, 0.27f));
        p.exposure = std::pow(2.0f, cfg.f("display.ev", 0.0f));
        p.damageTint = cfg.f("display.damage_tint", 0.35f);
        renderer.barRadius = cfg.f("bar.radius", 0.01f);
        renderer.barGap = cfg.f("bar.gap", 0.005f);
        renderer.barStub = cfg.f("bar.stub", 0.25f);
    }
    void setCamera() {
        const float3 pos = cfg.v3("camera.pos", make_float3(-9.0f, 4.5f, -11.0f));
        const float3 tgt = cfg.v3("camera.target", make_float3(4.0f, 2.6f, 4.0f));
        const float fov = cfg.f("camera.fov", 50.0f) * 3.14159265f / 180.0f;
        YCamera& c = renderer.params().cam;
        c.pos = pos;
        c.fwd = normalize(tgt - pos);
        c.right = normalize(cross(c.fwd, make_float3(0, 1, 0)));
        c.up = cross(c.right, c.fwd);
        c.tanHalfY = std::tan(0.5f * fov);
        c.tanHalfX = c.tanHalfY * float(width) / float(height);
    }
    // the scenario's events at time t (seconds since the start)
    void events(double t) {
        if (scenario == "kolon" && !removed && t >= cfg.f("kolon.time", 1.0f)) {
            // ground-storey columns at grid points "i j i j ...": their three pieces break loose and are shoved out
            std::vector<int> ij;
            {
                const auto it = cfg.values.find("kolon.list");
                const std::string list = it == cfg.values.end() ? std::string("0 0") : it->second;
                size_t pos = 0;
                while (pos < list.size()) {
                    char* end = nullptr;
                    const long v = std::strtol(list.c_str() + pos, &end, 10);
                    if (end == list.c_str() + pos) { ++pos; continue; }
                    ij.push_back(int(v));
                    pos = size_t(end - list.c_str());
                }
            }
            const float3 push = cfg.v3("kolon.push", make_float3(-2.5f, 0.0f, -1.0f));
            for (size_t c = 0; c + 1 < ij.size(); c += 2) {
                const double x = ij[c] * spec.spanX, z = ij[c + 1] * spec.spanZ;
                int n = 0;
                for (const Piece& p : pieces)
                    if (p.kind == PK_COLUMN && p.hi.y <= spec.level(1) + 1e-6 && std::fabs((p.lo.x + p.hi.x) * 0.5 - x) < 0.01 &&
                        std::fabs((p.lo.z + p.hi.z) * 0.5 - z) < 0.01) {
                        if (cfg.i("kolon.remove", 1)) {
                            world.removeBody(p.body);   // gone: an explosive charge
                        } else {                        // cut and shoved (a light frame lets go; a loaded one does not)
                            world.breakBody(p.body);
                            world.setVelocity(p.body, push, make_float3(0.0f, 0.0f, 1.5f));
                        }
                        ++n;
                    }
                std::printf("t=%.2f s: kolon (%d, %d) %s (%d parca)\n", t, ij[c], ij[c + 1], cfg.i("kolon.remove", 1) ? "kaldirildi" : "kirildi", n);
            }
            removed = true;
        }
        if (scenario == "sarsinti" && !quakeOn && t >= cfg.f("deprem.time", 1.0f)) startQuake(cfg.i("deprem.record", 0), cfg.f("deprem.scale", 1.0f), t);
        driveQuake(t);
    }
    // the record starts playing at scenario time t, from deprem.offset s of the record, or (negative) 15 s before its
    // strongest moment; the ground keeps where it stands when a quake begins (the record is played relative to it)
    bool startQuake(int record, float scale, double t) {
        std::string err;
        if (!quakeLoad(record, root, quake, err)) {
            std::printf("deprem kaydi yuklenemedi: %s\n", err.c_str());
            return false;
        }
        quakeOn = true;
        quakeT0 = t;
        quakeOffset = cfg.f("deprem.offset", -1.0f);
        if (quakeOffset < 0) quakeOffset = std::max(0.0, quake.tPeak - 15.0);
        quakeScale = scale;
        groundBase = world.groundPos();
        std::printf("t=%.2f s: deprem %s, yatay tepe ivme %.2f g (kaydin %.1f. sn), x%.2f, kaydin %.1f. saniyesinden\n", t, quake.name.c_str(),
                    quake.pga / 9.81, quake.tPeak, quakeScale, quakeOffset);
        return true;
    }
    void stopQuake() { quakeOn = false; }   // the ground stays where the record left it
    void driveQuake(double t) {
        if (!quakeOn) return;
        const double tr = t - quakeT0 + quakeOffset;
        if (tr > quake.duration()) { quakeOn = false; return; }
        // fade in over a second so the ground does not jump to the record's displacement at the offset
        const double3 d = quake.displacement(tr) - quake.displacement(quakeOffset);
        const double fade = std::min(1.0, (t - quakeT0) / 1.0);
        world.setGroundMotion(groundBase + d * (quakeScale * fade));
    }
    // an explosive charge at p: bodies whose centre is within r are gone, those within 3r are thrown outwards
    int blast(float3 p, float r) {
        world.download();
        int n = 0;
        for (size_t i = 1; i < world.pos.size(); ++i) {
            if (world.kinematic[i]) continue;
            const float3 d = f3(world.pos[i]) - p;
            const float dl = length(d);
            if (dl < r) {
                world.removeBody(int(i));
                ++n;
            } else if (dl < 3.0f * r) {
                const float k = cfg.f("patlama.hiz", 6.0f) * (1.0f - dl / (3.0f * r));
                world.setVelocity(int(i), world.vel[i] + d * (k / dl), world.angVel[i]);
            }
        }
        return n;
    }
};

static int runClip(YikimApp& app, double seconds, double still, unsigned spp, const std::string& outPrefix) {
    namespace fs = std::filesystem;
    const bool clip = still < 0.0;
    const std::string dir = clip ? outPrefix : fs::path(outPrefix).parent_path().string();
    if (!dir.empty()) fs::create_directories(dir);
    const double dt = 1.0 / 60.0;
    const int frames = int(std::round((clip ? seconds : still) / dt));
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
    float fastest = 0;
    int fastestBody = -1;
    double fastestT = 0, fastestY = 0;
    std::vector<float3> kickPrev;
    std::set<int> kickBodies, kickUpBodies;
    int kicks = 0;
    float kickUp = 0, kickSide = 0;
    for (int k = 0; k <= frames; ++k) {
        const double t = k * dt;
        app.events(t);
        if (k > 0) {
            app.world.step(float(dt));
            physMs += app.world.lastMs;
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
        // the fastest piece: a fall from the roof is ~17 m/s; much more is energy the solver made up
        for (size_t i = 1; i < app.world.vel.size(); ++i) {
            if (app.world.kinematic[i]) continue;
            const float sp = length(app.world.vel[i]);
            if (sp > fastest) {
                fastest = sp;
                fastestBody = int(i);
                fastestT = t;
                fastestY = app.world.pos[i].y;
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
        const bool shoot = clip ? (k % 2 == 0) : (k == frames);
        if (!shoot) continue;
        app.renderer.updateWorld(app.world);
        app.renderer.setDamage(app.world.damage());
        app.world.download();
        app.renderer.params().groundY = float(app.world.pos[0].y);
        app.renderer.params().groundShift = make_float2(float(app.world.pos[0].x), float(app.world.pos[0].z));
        app.renderer.render(spp, false);
        rendMs += app.renderer.lastMs;
        app.renderer.download(px);
        char name[512];
        if (clip) std::snprintf(name, sizeof(name), "%s/f_%05d.png", outPrefix.c_str(), shots);
        else std::snprintf(name, sizeof(name), "%s.png", outPrefix.c_str());
        writePNG(name, int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(px.data()));
        ++shots;
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
        const std::string cmd = "ffmpeg -loglevel error -y -framerate 30 -i \"" + outPrefix + "/f_%05d.png\" -c:v libx264 -pix_fmt yuv420p -crf 18 \"" + mp4 + "\"";
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
static bool g_ykeys[GLFW_KEY_LAST + 1] = {};
static bool g_yclick = false, g_yclickCtrl = false;
static double g_yscroll = 0.0;
static void yKey(GLFWwindow*, int key, int, int action, int) {
    if (key >= 0 && key <= GLFW_KEY_LAST && action == GLFW_PRESS) g_ykeys[key] = true;
}
static void yMouse(GLFWwindow* w, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        g_yclick = true;
        g_yclickCtrl = (mods & GLFW_MOD_CONTROL) != 0;
    }
    (void)w;
}
static void yScroll(GLFWwindow*, double, double dy) { g_yscroll += dy; }
static bool ypressed(int key) {
    const bool p = g_ykeys[key];
    g_ykeys[key] = false;
    return p;
}

static void yDrawText(float x, float y, const std::string& text, float scale) {
    static char buffer[200000];
    const int quads = stb_easy_font_print(0, 0, const_cast<char*>(text.c_str()), nullptr, buffer, sizeof(buffer));
    glEnableClientState(GL_VERTEX_ARRAY);
    for (int pass = 0; pass < 2; ++pass) {
        glPushMatrix();
        glTranslatef(x + (pass == 0 ? scale : 0), y + (pass == 0 ? scale : 0), 0);
        glScalef(scale, scale, 1);
        if (pass == 0) glColor4f(0, 0, 0, 0.85f);
        else glColor4f(1.0f, 0.97f, 0.9f, 1.0f);
        glVertexPointer(2, GL_FLOAT, 16, buffer);
        glDrawArrays(GL_QUADS, 0, quads * 4);
        glPopMatrix();
    }
    glDisableClientState(GL_VERTEX_ARRAY);
}

static std::string ystamp() {
    std::time_t t = std::time(nullptr);
    char b[64];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", std::localtime(&t));
    return b;
}

// --autotest: keys and clicks on a timeline, through the same paths as real input (the window cannot be watched)
struct YKeyAt { double t; int key; };   // key -1: a click at the centre, -2: a ctrl-click at the centre
static const YKeyAt kYAutotest[] = {{3.0, GLFW_KEY_F12}, {4.0, -1}, {6.0, GLFW_KEY_F12}, {7.0, GLFW_KEY_SPACE}, {16.0, GLFW_KEY_F12},
                                    {17.0, GLFW_KEY_T}, {20.0, GLFW_KEY_F12}, {21.0, GLFW_KEY_R}, {26.0, GLFW_KEY_F12}, {27.0, GLFW_KEY_ESCAPE}};

static int runWindow(YikimApp& app, bool windowed, bool autotest) {
    if (!glfwInit()) { std::printf("glfwInit failed\n"); return 1; }
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(mon);
    GLFWwindow* win = nullptr;
    if (windowed) {
        win = glfwCreateWindow(1280, 720, "pc_graphics - Yikim", nullptr, nullptr);
    } else {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
        win = glfwCreateWindow(mode->width, mode->height, "pc_graphics - Yikim", mon, nullptr);
    }
    if (!win) { std::printf("window creation failed\n"); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(0);
    glfwSetKeyCallback(win, yKey);
    glfwSetMouseButtonCallback(win, yMouse);
    glfwSetScrollCallback(win, yScroll);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, app.width, app.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    uchar4* pinned = nullptr;   // page-locked: the frame comes down at full PCIe speed
    cudaMallocHost(reinterpret_cast<void**>(&pinned), size_t(app.width) * app.height * sizeof(uchar4));
    double frameMs = 0.0;

    // the free camera (fly_camera.h)
    YCamera& cam = app.renderer.params().cam;
    FlyCamera fly;
    fly.lookAt(cam.pos, cam.pos + cam.fwd);
    fly.fovDeg = app.cfg.f("camera.fov", 50.0f);
    auto applyCam = [&]() { fly.apply(cam, app.width, app.height); };
    applyCam();

    const float speeds[3] = {1.0f, 0.25f, 0.1f};
    int speedIdx = 0;
    bool paused = false, hud = true;
    int record = app.cfg.i("deprem.record", 0);
    float scale = app.cfg.f("deprem.scale", 1.0f);
    const unsigned spp = unsigned(app.cfg.i("display.spp", 2));
    app.renderer.denoiseMode = app.cfg.i("display.denoise", 1);
    app.renderer.filterPasses(app.cfg.i("display.filter_passes", 4));
    double tick = 0.0, last = fnow2(), fpsT = fnow2(), msg = 0.0;
    int fpsFrames = 0, frameNo = 0;
    float fps = 0.0f;
    std::string message;
    auto say = [&](const std::string& m) { message = m; msg = fnow2() + 3.0; std::printf("%s\n", m.c_str()); std::fflush(stdout); };
    double mouseX = 0, mouseY = 0;
    glfwGetCursorPos(win, &mouseX, &mouseY);
    bool looking = false, camMoved = true;
    PhysStats st = app.world.stats();
    double lastStats = 0.0;
    const double winStart = fnow2();
    size_t autoNext = 0;
    say("YIKIM: sol tik patlat (Ctrl: buyuk), Bosluk deprem, 1-5 kayit, +/- siddet, T agir cekim, P dur, R bastan, H yardim");

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        const float sx = float(fbw) / app.width, sy = float(fbh) / app.height, sc = std::min(sx, sy);
        const float qw = app.width * sc, qh = app.height * sc, qx = 0.5f * (fbw - qw), qy = 0.5f * (fbh - qh);
        bool click = false, clickCtrl = false;
        double clickX = 0, clickY = 0;
        if (autotest && autoNext < sizeof(kYAutotest) / sizeof(YKeyAt) && fnow2() - winStart > kYAutotest[autoNext].t) {
            const int k = kYAutotest[autoNext].key;
            std::printf("autotest: %d at %.1f s (%.1f fps, kare %.1f ms, fizik %.1f ms, goruntu %.1f ms, gurultu giderici %.1f ms)\n", k,
                        fnow2() - winStart, fps, frameMs, app.world.lastMs, app.renderer.lastMs, app.renderer.denoiseMs);
            if (k < 0) { click = true; clickCtrl = k == -2; clickX = qx + qw * 0.5; clickY = qy + qh * 0.62; }
            else g_ykeys[k] = true;
            ++autoNext;
        }
        if (ypressed(GLFW_KEY_ESCAPE)) break;
        if (glfwGetWindowAttrib(win, GLFW_ICONIFIED)) { glfwWaitEventsTimeout(0.05); last = fnow2(); continue; }
        if (ypressed(GLFW_KEY_H)) hud = !hud;
        if (ypressed(GLFW_KEY_G)) {
            app.renderer.denoiseMode = (app.renderer.denoiseMode + 1) % 3;
            say(app.renderer.denoiseMode == 0 ? "ham goruntu (gurultu giderici kapali)"
                : app.renderer.denoiseMode == 1 ? "gurultu giderici: hizli suzgec" : "gurultu giderici: yapay zeka (en temiz, fizigi yavaslatir)");
        }
        if (ypressed(GLFW_KEY_P)) { paused = !paused; say(paused ? "durdu" : "devam"); }
        if (ypressed(GLFW_KEY_B)) {
            app.renderer.showBars = !app.renderer.showBars;
            camMoved = true;
            say(app.renderer.showBars ? "donati gorunuyor" : "donati gizli");
        }
        if (ypressed(GLFW_KEY_T)) {
            speedIdx = (speedIdx + 1) % 3;
            say(speedIdx == 0 ? "normal hiz" : speedIdx == 1 ? "agir cekim x0.25" : "agir cekim x0.1");
        }
        for (int r = 0; r < quakeCount() && r < 9; ++r)
            if (ypressed(GLFW_KEY_1 + r)) {
                record = r;
                say("kayit: " + quakeName(r) + (app.quakeOn ? " (Bosluk ile baslat)" : ""));
                if (app.quakeOn) app.stopQuake();
            }
        if (ypressed(GLFW_KEY_EQUAL) || ypressed(GLFW_KEY_KP_ADD)) { scale = std::min(4.0f, scale + 0.25f); app.quakeScale = scale; say("siddet x" + std::to_string(scale).substr(0, 4)); }
        if (ypressed(GLFW_KEY_MINUS) || ypressed(GLFW_KEY_KP_SUBTRACT)) { scale = std::max(0.25f, scale - 0.25f); app.quakeScale = scale; say("siddet x" + std::to_string(scale).substr(0, 4)); }
        if (ypressed(GLFW_KEY_SPACE)) {
            if (app.quakeOn) { app.stopQuake(); say("deprem durdu"); }
            else if (app.startQuake(record, scale, app.world.time)) say("DEPREM: " + quakeName(record) + "  x" + std::to_string(scale).substr(0, 4));
        }
        if (ypressed(GLFW_KEY_R)) {
            say("bina yeniden kuruluyor...");
            app.build();
            app.renderer.uploadWorld(app.world);
            camMoved = true;
            say("bina hazir");
        }
        const bool shot = ypressed(GLFW_KEY_F12);
        // mouse: right button looks, left button blasts, the wheel zooms
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        const bool rmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (rmb && looking) {
            fly.look(float(mx - mouseX), float(my - mouseY), app.cfg.f("camera.mouse", 0.0025f));
            if (mx != mouseX || my != mouseY) camMoved = true;
        }
        looking = rmb;
        mouseX = mx;
        mouseY = my;
        if (g_yscroll != 0.0) { fly.fovDeg = std::max(15.0f, std::min(90.0f, fly.fovDeg - float(g_yscroll) * 3.0f)); g_yscroll = 0.0; camMoved = true; }
        if (g_yclick) { click = true; clickCtrl = g_yclickCtrl; clickX = mx; clickY = my; g_yclick = false; }
        const double now = fnow2();
        const float realDt = float(std::min(0.1, now - last));
        last = now;
        {   // WASD + E/Q, Shift faster
            auto key = [&](int k) { return glfwGetKey(win, k) == GLFW_PRESS ? 1.0f : 0.0f; };
            const float inF = key(GLFW_KEY_W) - key(GLFW_KEY_S), inR = key(GLFW_KEY_D) - key(GLFW_KEY_A), inU = key(GLFW_KEY_E) - key(GLFW_KEY_Q);
            if (inF != 0.0f || inR != 0.0f || inU != 0.0f) {
                fly.move(inF, inR, inU, (key(GLFW_KEY_LEFT_SHIFT) > 0.0f ? 24.0f : 7.0f) * realDt);
                camMoved = true;
            }
        }
        applyCam();
        if (click) {   // the cursor in render pixels
            const float rx = float((clickX - qx) / qw * app.width), ry = float((clickY - qy) / qh * app.height);
            if (rx >= 0 && ry >= 0 && rx < app.width && ry < app.height) {
                float4 hit;
                const int body = app.renderer.pick(rx, ry, hit);
                if (body > 0) {
                    const int n = app.blast(make_float3(hit.x, hit.y, hit.z), clickCtrl ? 1.3f : 0.55f);
                    say("patlatildi: " + std::to_string(n) + " parca");
                } else {
                    say(body == 0 ? "zemin" : "bos");
                }
            }
        }
        // draw the current state while the GPU computes the next one: the transforms are read first (an event), the
        // step waits for them, the render runs on its own stream meanwhile. One step a frame at most, on a 60 Hz tick:
        // when a step costs more than a tick the world slows down a little instead of the frame rate collapsing (two
        // steps a frame locked the window at 30 fps, measured).
        if (++frameNo % 10 == 0) app.renderer.setDamage(app.world.damage());
        app.renderer.updateWorld(app.world);
        {
            YikimParams& P = app.renderer.params();
            const double3 g = app.world.groundPos();
            P.groundY = float(g.y);
            P.groundShift = make_float2(float(g.x), float(g.z));
        }
        bool stepped = false;
        if (!paused) {
            tick += realDt;
            if (tick >= 1.0 / 60.0) {
                tick = std::min(tick - 1.0 / 60.0, 1.0 / 60.0);   // no backlog
                const double dt = speeds[speedIdx] / 60.0;
                app.driveQuake(app.world.time + dt);
                app.world.stepBegin(float(dt), app.renderer.transformsDone());
                stepped = true;
                camMoved = true;
            }
        }
        // a still world under a still camera gathers samples; anything moving starts afresh
        app.renderer.render(camMoved ? spp : spp * 2, !camMoved && app.renderer.params().samples < 4096);
        camMoved = false;
        app.renderer.downloadTo(pinned);
        if (stepped) app.world.stepEnd();
        if (shot) {
            std::filesystem::create_directories(app.root + "/captures");
            const std::string name = app.root + "/captures/yikim_" + ystamp() + ".png";
            writePNG(name, int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(pinned));
            say("ekran goruntusu: " + name);
        }

        glViewport(0, 0, fbw, fbh);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, fbw, fbh, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, app.width, app.height, GL_RGBA, GL_UNSIGNED_BYTE, pinned);
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(qx, qy);
        glTexCoord2f(1, 0); glVertex2f(qx + qw, qy);
        glTexCoord2f(1, 1); glVertex2f(qx + qw, qy + qh);
        glTexCoord2f(0, 1); glVertex2f(qx, qy + qh);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        frameMs = (fnow2() - now) * 1000.0;
        ++fpsFrames;
        if (fnow2() - fpsT > 0.5) { fps = float(fpsFrames / (fnow2() - fpsT)); fpsFrames = 0; fpsT = fnow2(); }
        if (fnow2() - lastStats > 0.5) { st = app.world.stats(); lastStats = fnow2(); }
        if (hud) {
            char b[640];
            std::snprintf(b, sizeof(b),
                          "YIKIM  %.0f fps   fizik %.1f ms   goruntu %.1f ms%s   t %.1f s%s%s\n"
                          "deprem: %s  x%.2f  %s\n"
                          "parca %d   kirik lif %d / %d   catlak %d   temas yamasi %d",
                          fps, app.world.lastMs, app.renderer.lastMs, app.renderer.denoiseMode == 0 ? " (ham)" : app.renderer.denoiseMode == 1 ? " (suzgec dahil)" : " (yapay zeka dahil)", app.world.time,
                          paused ? "   DURDU" : "",
                          speedIdx ? (speedIdx == 1 ? "   AGIR CEKIM x0.25" : "   AGIR CEKIM x0.1") : "", quakeName(record).c_str(), scale,
                          app.quakeOn ? "OYNUYOR (Bosluk: durdur)" : "(Bosluk: baslat)", st.bodies, st.broken, st.fibres, st.cracked, st.contacts);
            yDrawText(24, float(fbh) - 90.0f, b, 2.0f);
            yDrawText(24, 24, "Sol tik: patlat (Ctrl: buyuk)   Sag tik + fare: bak   WASD, Q/E, Shift: uc   Tekerlek: yakinlas\n"
                              "Bosluk: deprem   1-5: kayit   +/-: siddet   T: agir cekim   P: dur   B: donati   G: gurultu giderici   R: bastan   F12: ekran goruntusu   H: gizle   Esc: cik",
                      2.0f);
            if (fnow2() < msg) yDrawText(24, float(fbh) - 130.0f, message, 2.0f);
        }
        glfwSwapBuffers(win);
    }
    cudaFreeHost(pinned);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}

int runYikim(int argc, char** argv) {
    YikimApp app;
    std::string only, out;
    bool test = false;
    double seconds = 6.0, still = -1.0;
    unsigned spp = 16;
    std::string clipName;
    bool windowed = false, autotest = false, sizeGiven = false;
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
        else if (a == "--windowed") windowed = true;
        else if (a == "--autotest") autotest = true;
        else if (a == "--out") out = next();
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else { std::printf("unknown option %s\n", a.c_str()); return 2; }
    }
    app.loadConfig();
    if (test) return runYikimTests(app.cfg, only);
    if (!clipName.empty()) app.scenario = clipName;
    const bool live = clipName.empty() && still < 0.0;
    if (live) {
        app.scenario = "none";
        if (!sizeGiven) {   // the window renders at display.width x display.height, scaled to the screen
            app.width = unsigned(app.cfg.i("display.width", 1920));
            app.height = unsigned(app.cfg.i("display.height", 1080));
        }
    }
    app.renderer.init(app.width, app.height, app.root);
    std::string log;
    const double t0 = ynow();
    if (!app.renderer.compileDevice(log)) { std::printf("DERLEME HATASI\n%s\n", log.c_str()); return 1; }
    std::printf("yikim programi derlendi (%.1f s)\n", ynow() - t0);
    app.build();
    app.renderer.uploadWorld(app.world);
    app.applyRender();
    app.setCamera();
    const std::string prefix = out.empty() ? app.root + "/captures/yikim_" + app.scenario : out;
    if (!clipName.empty() || still >= 0.0) {
        const int rc = runClip(app, seconds, still, still >= 0.0 ? std::max(spp, 64u) : spp, prefix);
        app.renderer.shutdown();
        return rc;
    }
    const int rc = runWindow(app, windowed, autotest);
    app.renderer.shutdown();
    return rc;
}
