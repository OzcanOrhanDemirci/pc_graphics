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
    // wrecking balls (strike): a pool per size, set aside below the ground until thrown; the oldest is thrown again
    std::vector<float> ballSizes;              // radii, m
    std::vector<std::vector<int>> ballPools;   // bodies, per size
    std::vector<int> ballNext;
    int struck = 0;              // the last ball thrown (body), 0: none
    struct BallThrow {
        int size = -1;           // index into ballSizes
        float radius = 0, speed = 0, mass = 0;
    };

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
        world.params.deepOverlap = cfg.f("phys.deep_overlap", world.params.deepOverlap);
    }
    // which building stands: "apartman" (the apartment block), "gokdelen" (the tower) or "ev" (the house); setting yapi
    std::string building() const {
        const auto it = cfg.values.find("yapi");
        return it == cfg.values.end() || (it->second != "gokdelen" && it->second != "ev") ? std::string("apartman") : it->second;
    }
    void build() {
        if (cfg.values.count("scene") && cfg.values.at("scene") == "cerceve") {
            spec = frameSpecFromConfig(cfg);
            pieces = buildFrame(world, spec, concreteFromConfig(cfg));
            loose.clear();
        } else {
            PhysMaterial mc = concreteFromConfig(cfg), mb = brickFromConfig(cfg);
            if (cfg.i("test.elastic", 0))   // diagnosis: nothing can break; what still falls is not glued
                for (PhysMaterial* m : {&mc, &mb}) m->ft = m->fc = m->cohesion = m->fy = 1e12f;
            Building b;
            const std::string y = building();
            if (y == "gokdelen") {
                const TowerSpec ts = towerSpecFromConfig(cfg);
                b = buildTowerBlock(world, ts, mc, mb);
                spec = ts.frame;
            } else if (y == "ev") {
                const HouseSpec hs = houseSpecFromConfig(cfg);
                b = buildHouse(world, hs, mc, mb);
                spec = hs.frame;
            } else {
                const ApartmentSpec as = apartmentSpecFromConfig(cfg);
                b = buildApartment(world, as, mc, mb);
                spec = as.frame;
            }
            pieces = std::move(b.pieces);
            loose = std::move(b.loose);
        }
        {   // the wrecking balls a click throws: cast steel in a few sizes (the power picks one), frozen far below the
            // ground until then
            ballSizes.clear();
            ballPools.clear();
            ballNext.clear();
            struck = 0;
            const auto it = cfg.values.find("darbe.boylar");
            const std::string list = it == cfg.values.end() ? std::string("0.25 0.3 0.35 0.42 0.5 0.6 0.7") : it->second;
            for (const char* c = list.c_str(); *c;) {
                char* end = nullptr;
                const double r = std::strtod(c, &end);
                if (end == c) { ++c; continue; }
                c = end;
                if (r > 0.05) ballSizes.push_back(float(r));
            }
            std::sort(ballSizes.begin(), ballSizes.end());
            for (float r : ballSizes) {
                std::vector<int> pool;
                for (int k = 0; k < std::max(1, cfg.i("darbe.toplar", 4)); ++k) {
                    const int b = world.addBody(polySphere(d3(0, 0, 0), r), cfg.f("darbe.yogunluk", 7850.0f), false, PK_OTHER, 0x3b3c3e);
                    world.info[b].sphere = r;
                    world.parkBody(b);
                    pool.push_back(b);
                }
                ballPools.push_back(pool);
                ballNext.push_back(0);
            }
        }
        applyPhysics();
        world.upload();
        {   // the building settles under its own weight before anything happens (hidden)
            const double t0 = ynow();
            const std::vector<double3> built = world.pos;
            world.settle(cfg.f("phys.settle_seconds", 2.0f), cfg.f("phys.settle_ramp", 0.5f), 1.0f / 60.0f);
            const PhysStats st = world.stats();
            std::printf("oturma: %.1f s, kirik lif %d, catlak %d (%.1f s surdu)\n", cfg.f("phys.settle_seconds", 2.0f), st.broken, st.cracked, ynow() - t0);
            if (cfg.i("test.verbose", 0)) {   // diagnosis: the pieces that moved most while settling
                world.download();
                std::vector<int> order;
                for (const Piece& p : pieces) order.push_back(p.body);
                auto moved = [&](int b) { return dlength(world.pos[b] - built[b]); };
                std::sort(order.begin(), order.end(), [&](int a, int b) { return moved(a) > moved(b); });
                const char* kn[] = {"kolon", "kiris", "doseme", "duvar", "diger"};
                for (size_t k = 0; k < std::min<size_t>(10, order.size()); ++k) {
                    const int b = order[k];
                    const double3 d = world.pos[b] - built[b];
                    std::printf("      oturmada en cok kayan %4d %-6s (%.2f %.2f %.2f): %.1f mm (%.1f %.1f %.1f)\n", b, kn[world.info[b].tag], built[b].x,
                                built[b].y, built[b].z, 1000 * moved(b), 1000 * d.x, 1000 * d.y, 1000 * d.z);
                }
            }
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
    void setCamera() {   // the building's own view: camera.* for the apartment, <yapi>.camera_* for the others
        const std::string b = building(), pre = b == "apartman" ? std::string("camera.") : b + ".camera_";
        const float3 pos = cfg.v3(pre + "pos", cfg.v3("camera.pos", make_float3(-9.0f, 4.5f, -11.0f)));
        const float3 tgt = cfg.v3(pre + "target", cfg.v3("camera.target", make_float3(4.0f, 2.6f, 4.0f)));
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
        // darbe: at darbe.time a wrecking ball of power darbe.guc comes along darbe.from (a direction, m) at the
        // ground-storey column at grid point darbe.kolon, half a metre under its middle
        if (scenario == "darbe" && !struck && t >= cfg.f("darbe.time", 1.0f)) {
            const float3 ij = cfg.v3("darbe.kolon", make_float3(0, 0, 0));
            float3 p = make_float3(float(ij.x * spec.spanX), float(0.5 * spec.level(1) - 0.5), float(ij.y * spec.spanZ));
            if (cfg.values.count("darbe.hedef")) p = cfg.v3("darbe.hedef", p);   // a point instead of a column
            const float3 from = p + cfg.v3("darbe.from", make_float3(-8.0f, 1.0f, -4.0f));
            const BallThrow bt = ballThrow(cfg.f("darbe.guc", 0.5f));
            const int b = strike(from, p, cfg.f("darbe.guc", 0.5f));
            std::printf("t=%.2f s: yikim topu %d (r %.2f m, %.0f kg, %.0f m/s), hedef (%.2f %.2f %.2f)\n", t, b, bt.radius, bt.mass, bt.speed, p.x, p.y,
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
    // The power of a throw, 0..1: size (cast steel, so mass) and speed together. 0.5 is the balanced throw (darbe.yaricap
    // at darbe.hiz); towards 0 smaller and slower (darbe.min_*), towards 1 bigger and far faster (darbe.max_*), each half
    // on a log scale. The size is the nearest of the balls that exist.
    BallThrow ballThrow(float u) const {
        BallThrow t;
        if (ballSizes.empty()) return t;
        u = std::min(1.0f, std::max(0.0f, u));
        auto between = [](float a, float b, float x) { return a * std::pow(b / a, x); };
        const float r0 = cfg.f("darbe.min_yaricap", 0.25f), r1 = cfg.f("darbe.yaricap", 0.5f), r2 = cfg.f("darbe.max_yaricap", 0.7f);
        const float v0 = cfg.f("darbe.min_hiz", 8.0f), v1 = cfg.f("darbe.hiz", 14.0f), v2 = cfg.f("darbe.max_hiz", 70.0f);
        const float r = u <= 0.5f ? between(r0, r1, u / 0.5f) : between(r1, r2, (u - 0.5f) / 0.5f);
        t.speed = u <= 0.5f ? between(v0, v1, u / 0.5f) : between(v1, v2, (u - 0.5f) / 0.5f);
        t.size = 0;
        for (int k = 1; k < int(ballSizes.size()); ++k)
            if (std::fabs(std::log(ballSizes[k] / r)) < std::fabs(std::log(ballSizes[t.size] / r))) t.size = k;
        t.radius = ballSizes[t.size];
        t.mass = world.info[ballPools[t.size][0]].mass;
        return t;
    }
    // a wrecking ball of this power thrown at p from the direction of `from` (the camera): it appears a few metres short
    // of p (further back when fast, so that it is seen) and arrives at its speed, aimed above the line by what gravity
    // takes on the way. What breaks is the solver's business: the ball is a body like the others. Returns the body.
    int strike(float3 from, float3 p, float power) {
        const BallThrow bt = ballThrow(power);
        if (bt.size < 0) return 0;
        std::vector<int>& pool = ballPools[bt.size];
        int& next = ballNext[bt.size];
        const int b = pool[size_t(next) % pool.size()];
        next = (next + 1) % int(pool.size());
        const float dist = length(p - from);
        const float3 dir = dist > 1e-4f ? (p - from) * (1.0f / dist) : make_float3(0, 0, 1);
        const float speed = bt.speed;
        // where it appears, back along the line: darbe.mesafe, or a quarter second of flight when that is more
        const float s = std::min(std::max(cfg.f("darbe.mesafe", 6.0f), 0.25f * speed), std::max(1.5f, dist - 1.0f));
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
