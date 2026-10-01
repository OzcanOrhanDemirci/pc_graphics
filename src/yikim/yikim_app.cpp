// Scene 3 "Yikim": an earthquake and a reinforced-concrete building, the physics on the GPU.
//
//   yikim.exe --test [name]            physics tests (dusme, yigin, egim, konsol, enerji, cerceve)
//   yikim.exe --clip kolon|sarsinti --seconds S --out captures/x     frames (30 fps) + an mp4 (ffmpeg)
//   yikim.exe --still T --out captures/x          the scenario simulated to T seconds, one clean frame
//   options: --scenario kolon|sarsinti, --spp N, --width W, --height H, --set key=value, --root dir
#include "image_io.h"
#include "scene.h"   // Config
#include "yikim/building.h"
#include "yikim/quake.h"
#include "yikim/yikim_renderer.h"
#include "yikim/yikim_scenes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
        if (scenario == "sarsinti" && !quakeOn && t >= cfg.f("deprem.time", 1.0f)) {
            std::string err;
            if (quakeLoad(cfg.i("deprem.record", 0), root, quake, err)) {
                quakeOn = true;
                quakeT0 = t;
                // where the record starts playing: deprem.offset seconds, or (negative) 15 s before its strongest moment
                quakeOffset = cfg.f("deprem.offset", -1.0f);
                if (quakeOffset < 0) quakeOffset = std::max(0.0, quake.tPeak - 15.0);
                quakeScale = cfg.f("deprem.scale", 1.0f);
                std::printf("t=%.2f s: deprem %s, yatay tepe ivme %.2f g (kaydin %.1f. sn), x%.2f, kaydin %.1f. saniyesinden\n", t, quake.name.c_str(),
                            quake.pga / 9.81, quake.tPeak, quakeScale, quakeOffset);
            } else {
                std::printf("deprem kaydi yuklenemedi: %s\n", err.c_str());
            }
        }
        if (quakeOn) {
            const double tr = t - quakeT0 + quakeOffset;
            // fade in over a second so the ground does not jump to the record's displacement at the offset
            const double3 d = quake.displacement(tr) - quake.displacement(quakeOffset);
            const double fade = std::min(1.0, (t - quakeT0) / 1.0);
            world.setGroundMotion(d * (quakeScale * fade));
        }
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
    // the roof: pieces whose top is at the top storey's level
    std::vector<int> roof;
    double roofY = 0;
    for (const Piece& p : app.pieces) roofY = std::max(roofY, p.hi.y);
    for (const Piece& p : app.pieces)
        if (p.hi.y > roofY - 1e-6) roof.push_back(p.body);
    std::vector<double3> start;
    double maxDrift = 0, maxDrop = 0, tMaxDrift = 0;
    bool caught = false;
    std::vector<float3> prevVel;
    float fastest = 0;
    int fastestBody = -1;
    double fastestT = 0, fastestY = 0;
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

int runYikim(int argc, char** argv) {
    YikimApp app;
    std::string only, out;
    bool test = false;
    double seconds = 6.0, still = -1.0;
    unsigned spp = 16;
    std::string clipName;
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
        else if (a == "--width") app.width = unsigned(std::stoi(next()));
        else if (a == "--height") app.height = unsigned(std::stoi(next()));
        else if (a == "--out") out = next();
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else { std::printf("unknown option %s\n", a.c_str()); return 2; }
    }
    app.loadConfig();
    if (test) return runYikimTests(app.cfg, only);
    if (!clipName.empty()) app.scenario = clipName;
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
    std::printf("pencere henuz yok: --test, --clip, --still\n");
    app.renderer.shutdown();
    return 0;
}
