// Scene 3 "Yikim": the state of a run (the building, its physics, the renderer, the earthquake, the scenario's
// events). Shared by the live scene in the application (yikim_live.cpp) and the command-line tools (yikim_app.cpp).
#pragma once
#include "core/config.h"
#include "scenes/yikim/building.h"
#include "scenes/yikim/quake.h"
#include "scenes/yikim/yikim_renderer.h"
#include "scenes/yikim/yikim_scenes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

inline double ynow() {
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
    int quakeMag = 0;            // the magnitude playing (quake.h)
    double quakeT0 = 0;          // scenario time at which it starts
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
        if (scenario == "sarsinti" && !quakeOn && quakeMag == 0 && t >= cfg.f("deprem.time", 1.0f)) startQuake(cfg.i("deprem.magnitude", 7), t);
        driveQuake(t);
    }
    // a quake of this magnitude (quake.h) starts at scenario time t; the ground moves relative to where it stands
    bool startQuake(int magnitude, double t) {
        std::string err;
        if (!quakeLoadMagnitude(magnitude, root, quake, err)) {
            std::printf("deprem yuklenemedi: %s\n", err.c_str());
            return false;
        }
        quakeOn = true;
        quakeMag = magnitude;
        quakeT0 = t;
        groundBase = world.groundPos();
        std::printf("t=%.2f s: deprem %s, yatay tepe ivme %.3f g (%.1f. sn), %.1f sn\n", t, quake.name.c_str(), quake.pga / 9.81, quake.tPeak,
                    quake.duration());
        return true;
    }
    void stopQuake() { quakeOn = false; }   // the ground stays where it was
    double quakeProgress(double t) const { return quakeOn ? std::min(1.0, (t - quakeT0) / std::max(quake.duration(), 1e-6)) : 0.0; }
    void driveQuake(double t) {
        if (!quakeOn) return;
        const double tr = t - quakeT0;
        if (tr > quake.duration()) { quakeOn = false; world.setGroundMotion(groundBase); return; }
        world.setGroundMotion(groundBase + quake.displacement(tr));
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

int runYikimTests(const Config& cfg, const std::string& only);
// yikim's command line: --test [name], --clip kolon|sarsinti, --still T (see yikim_app.cpp)
int runYikimCli(int argc, char** argv);
