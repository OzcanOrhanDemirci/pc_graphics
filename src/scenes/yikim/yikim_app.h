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
    QuakePlay quake;
    bool quakeOn = false;
    int quakeMag = 0;            // the magnitude playing (quake.h)
    double quakeT0 = 0;          // scenario time at which it starts
    double3 groundBase = {0, 0, 0};
    bool removed = false;
    // wrecking balls (strike): set aside below the ground until thrown, then the oldest is thrown again
    std::vector<int> balls, heavyBalls;
    int nextBall = 0, nextHeavy = 0;
    int struck = 0;              // the last ball thrown (body), 0: none

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
        {   // the wrecking balls a click throws: cast steel, frozen far below the ground until then
            balls.clear();
            heavyBalls.clear();
            nextBall = nextHeavy = struck = 0;
            auto add = [&](std::vector<int>& list, int count, double r) {
                for (int k = 0; k < count; ++k) {
                    const int b = world.addBody(polySphere(d3(0, 0, 0), r), cfg.f("darbe.yogunluk", 7850.0f), false, PK_OTHER, 0x3b3c3e);
                    world.info[b].sphere = float(r);
                    world.parkBody(b);
                    list.push_back(b);
                }
            };
            add(balls, cfg.i("darbe.toplar", 6), cfg.f("darbe.yaricap", 0.5f));
            add(heavyBalls, cfg.i("darbe.agir_toplar", 3), cfg.f("darbe.agir_yaricap", 0.7f));
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
        if (scenario == "sarsinti" && !quakeOn && quakeMag == 0 && t >= cfg.f("deprem.time", 1.0f))
            startQuake(cfg.i("deprem.magnitude", 7), cfg.f("deprem.seconds", 0.0f), t);
        // darbe: at darbe.time a wrecking ball (darbe.agir: the heavy one) comes along darbe.from (a direction, m) at the
        // ground-storey column at grid point darbe.kolon, half a metre under its middle
        if (scenario == "darbe" && !struck && t >= cfg.f("darbe.time", 1.0f)) {
            const float3 ij = cfg.v3("darbe.kolon", make_float3(0, 0, 0));
            const float3 p = make_float3(float(ij.x * spec.spanX), float(0.5 * spec.level(1) - 0.5), float(ij.y * spec.spanZ));
            const float3 from = p + cfg.v3("darbe.from", make_float3(-8.0f, 1.0f, -4.0f));
            const int b = strike(from, p, cfg.i("darbe.agir", 0) != 0);
            std::printf("t=%.2f s: yikim topu %d (%.0f kg) kolona (%.0f, %.0f), hedef (%.2f %.2f %.2f)\n", t, b, world.info[b].mass, ij.x, ij.y, p.x, p.y,
                        p.z);
        }
        driveQuake(t);
    }
    // a quake of this magnitude (quake.h) starts at scenario time t, for seconds (0: the magnitude's own, < 0: until
    // stopped); the ground moves relative to where it stands
    bool startQuake(int magnitude, double seconds, double t) {
        std::string err;
        if (!quakePlay(magnitude, seconds, root, quake, err)) {
            std::printf("deprem yuklenemedi: %s\n", err.c_str());
            return false;
        }
        quakeOn = true;
        quakeMag = magnitude;
        quakeT0 = t;
        groundBase = world.groundPos();
        if (quake.endless()) std::printf("t=%.2f s: deprem %s, yatay tepe ivme %.3f g, durdurulana kadar\n", t, quake.rec.name.c_str(), quake.rec.pga / 9.81);
        else std::printf("t=%.2f s: deprem %s, yatay tepe ivme %.3f g (%.1f. sn), %.1f sn\n", t, quake.rec.name.c_str(), quake.rec.pga / 9.81, quake.rec.tPeak,
                         quake.length);
        return true;
    }
    // the ground fades back to rest over QuakePlay::kStopFade
    void stopQuake(double t) {
        if (quakeOn) quake.stop(t - quakeT0);
    }
    double quakeTime(double t) const { return quakeOn ? t - quakeT0 : 0.0; }
    void driveQuake(double t) {
        if (!quakeOn) return;
        const double tr = t - quakeT0;
        if (quake.over(tr)) { quakeOn = false; world.setGroundMotion(groundBase); return; }
        world.setGroundMotion(groundBase + quake.displacement(tr));
    }
    // a wrecking ball thrown at p from the direction of `from` (the camera): it appears a few metres short of p and
    // arrives at darbe.hiz, aimed above the line by what gravity takes on the way. What breaks is the solver's
    // business: the ball is a body like the others. Returns the ball's body.
    int strike(float3 from, float3 p, bool heavy) {
        std::vector<int>& pool = heavy ? heavyBalls : balls;
        if (pool.empty()) return 0;
        int& next = heavy ? nextHeavy : nextBall;
        const int b = pool[size_t(next) % pool.size()];
        next = (next + 1) % int(pool.size());
        const float dist = length(p - from);
        const float3 dir = dist > 1e-4f ? (p - from) * (1.0f / dist) : make_float3(0, 0, 1);
        const float speed = cfg.f("darbe.hiz", 14.0f);
        const float s = std::min(cfg.f("darbe.mesafe", 6.0f), std::max(1.5f, dist - 1.0f));   // where it appears, back along the line
        const float T = s / speed;   // on the way: start + v T - g T^2 / 2 = p
        const float3 start = p - dir * s;
        const float3 v = dir * speed + make_float3(0.0f, 0.5f * world.params.gravity * T, 0.0f);
        world.launchBody(b, d3(start), make_float4(0, 0, 0, 1), v, make_float3(0, 0, 0));
        struck = b;
        return b;
    }
};

int runYikimTests(const Config& cfg, const std::string& only);
// yikim's command line: --test [name], --clip kolon|sarsinti, --still T (see yikim_app.cpp)
int runYikimCli(int argc, char** argv);
