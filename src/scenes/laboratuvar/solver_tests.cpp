// Scene 4 "Laboratuvar": the particle solver's tests (built in T1 of the earlier "Tanecik").
//   --scene laboratuvar --test [dusus|hacim|baraj|balon|birikinti|dokme|sivilasma|tekrar|butce]
//                                                                      measurements, each with its criterion (PASS / FAIL)
//   --scene laboratuvar --clip baraj|balon [--seconds S] [--out dir]     30 fps frames and an mp4 (ffmpeg), particles as spheres
// Settings: scenes/laboratuvar.cfg, fizik.* (the solver) and test.* (the scenarios); --set key=value overrides them.
// Proving a test can fail: --set fizik.yogunluk=0 (no density constraint: hacim fails), --set fizik.baglar=0 (no
// bonds: balon fails), --set fizik.yapisma=0 (no cohesion: birikinti fails).
#include "scenes/laboratuvar/particles.h"
#include "core/config.h"
#include "core/image_io.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// the solver's settings from scenes/laboratuvar.cfg (fizik.*); the body (body_sim.cpp) uses it too
void applyPhysics(ParticleWorld& w, const Config& c) {
    PParams& p = w.params;
    p.substeps = c.i("fizik.alt_adim", p.substeps);
    p.iterations = c.i("fizik.yineleme", p.iterations);
    p.divergenceIters = c.i("fizik.diverjans", p.divergenceIters);
    p.bondIterations = c.i("fizik.bag_yineleme", p.bondIterations);
    p.psiMax = c.f("fizik.sinir_agirlik", p.psiMax);
    p.kernel = c.f("fizik.cekirdek", p.kernel);
    p.relax = c.f("fizik.gevseme", p.relax);
    p.sor = c.f("fizik.sor", p.sor);
    p.unilateral = c.i("fizik.tek_yonlu", p.unilateral ? 1 : 0) != 0;
    p.scorrK = c.f("fizik.scorr_k", p.scorrK);
    p.xsph = c.f("fizik.xsph", p.xsph);
    p.cohesion = c.f("fizik.yapisma", p.cohesion);
    p.tension = c.f("fizik.yuzey_gerilimi", p.tension);
    p.muStatic = c.f("fizik.mu_statik", p.muStatic);
    p.muDynamic = c.f("fizik.mu_dinamik", p.muDynamic);
    p.planeMuStatic = c.f("fizik.zemin_mu_statik", p.planeMuStatic);
    p.planeMuDynamic = c.f("fizik.zemin_mu_dinamik", p.planeMuDynamic);
    p.fluidPlaneMu = c.f("fizik.sivi_zemin_mu", p.fluidPlaneMu);
    p.fluidSolidMu = c.f("fizik.sivi_kati_mu", p.fluidSolidMu);
    p.maxSpeed = c.f("fizik.en_hiz", p.maxSpeed);
    p.density = c.i("fizik.yogunluk", 1) != 0;
    p.bonds = c.i("fizik.baglar", 1) != 0;
    p.bondDamping = c.f("fizik.bag_sonum", p.bondDamping);
    p.tearTip = c.f("fizik.yirtilma_ucu", p.tearTip);
    p.bleedFade = c.f("yara.pihtilasma", p.bleedFade);
    p.adhesion = c.f("fizik.islatma", p.adhesion);
    p.wetDrag = c.f("fizik.islak_surtunme", p.wetDrag);
}

namespace {

double tnow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// the solver's settings from the cfg (fizik.*)

std::vector<float3> lattice(float3 lo, int nx, int ny, int nz, float s) {
    std::vector<float3> v;
    v.reserve(size_t(nx) * ny * nz);
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) v.push_back(lo + make_float3((x + 0.5f) * s, (y + 0.5f) * s, (z + 0.5f) * s));
    return v;
}

const char* verdict(bool ok) { return ok ? "PASS" : "FAIL"; }

float spacingOf(const Config& c) { return c.f("fizik.aralik", c.f("beden.aralik", 0.007f)); }

// ---- the water balloon: a rubber membrane (one layer of particles spread evenly over a sphere, a Fibonacci lattice,
// bonded to their nearest neighbours only: it stretches but does not resist bending) around water (the cubic lattice
// inside, a spacing in from the membrane)
struct Balloon {
    int skinGroup = 0;
    int first = 0, skinFirst = 0, skinCount = 0, fluidFirst = 0, fluidCount = 0;
    float3 centre;
    float radius = 0.1f;
};
Balloon addBalloon(ParticleWorld& w, const Config& c, float3 centre, float R) {
    const float s = w.spacing();
    Balloon b;
    b.centre = centre;
    b.radius = R;
    std::vector<float3> skin, fluid;
    // as many points as a triangular lattice of spacing s needs to cover the sphere (each holds sqrt(3)/2 s^2)
    const int m = int(4.0f * 3.14159265f * R * R / (0.8660254f * s * s));
    const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
    for (int q = 0; q < m; ++q) {
        const float y = 1.0f - 2.0f * (q + 0.5f) / m, r = std::sqrt(std::max(0.0f, 1.0f - y * y)), ph = golden * q;
        skin.push_back(centre + make_float3(r * std::cos(ph), y, r * std::sin(ph)) * R);
    }
    const int k = int(std::ceil(R / s)) + 1;
    for (int z = -k; z <= k; ++z)
        for (int y = -k; y <= k; ++y)
            for (int x = -k; x <= k; ++x) {
                const float3 p = make_float3(x * s, y * s, z * s);
                if (length(p) <= R - s) fluid.push_back(centre + p);
            }
    b.skinGroup = w.addGroup(0);
    b.skinFirst = w.addParticles(skin, c.f("test.balon.deri_yogunluk", 200.0f), b.skinGroup);
    b.skinCount = int(skin.size());
    b.fluidFirst = w.addParticles(fluid, 1000.0f, 0);
    b.fluidCount = int(fluid.size());
    w.bondWithin(b.skinFirst, b.skinCount, c.f("test.balon.bag_uzaklik", 1.5f) * s, c.f("test.balon.uyum", 2.5e-3f), c.f("test.balon.kopma", 0.6f),
                 c.f("test.balon.ezilme", 0.6f));
    return b;
}
// a rigid cube (a shape-matched cluster) of side a and density rho, parked far away until dropped
struct Cube {
    int group = 0, first = 0, count = 0;
    float side = 0.1f, mass = 0.0f;
};
Cube addCube(ParticleWorld& w, float a, float rho, float3 park) {
    const float s = w.spacing();
    const int m = std::max(2, int(std::round(a / s)));
    Cube c;
    c.side = m * s;
    c.group = w.addGroup(PG_RIGID);
    const std::vector<float3> p = lattice(park - make_float3(0.5f * c.side, 0.5f * c.side, 0.5f * c.side), m, m, m, s);
    c.first = w.addParticles(p, rho, c.group);
    c.count = int(p.size());
    c.mass = rho * c.count * s * s * s;
    return c;
}

// what has left a balloon: its fluid farther from the skin's centre than the skin's farthest particle (plus a spacing)
struct BalloonState {
    float leaked = 0.0f;    // share of its fluid outside
    float skinTop = 0.0f, skinBottom = 0.0f;
    float3 skinCentre;
};
// where a shot from the side (+x or -x) meets the membrane: its outermost particle that way (on a sagging balloon, the
// bulge half way up)
float3 balloonSide(const Balloon& b, const std::vector<float3>& x, float dir) {
    int best = b.skinFirst;
    for (int i = b.skinFirst; i < b.skinFirst + b.skinCount; ++i)
        if (x[i].x * dir > x[best].x * dir) best = i;
    return x[best];
}
BalloonState balloonState(const Balloon& b, const std::vector<float3>& x, float s) {
    BalloonState st;
    double cx = 0, cy = 0, cz = 0;
    for (int i = b.skinFirst; i < b.skinFirst + b.skinCount; ++i) { cx += x[i].x; cy += x[i].y; cz += x[i].z; }
    st.skinCentre = make_float3(float(cx / b.skinCount), float(cy / b.skinCount), float(cz / b.skinCount));
    // the skin's extent from its centre: the 98th percentile (torn-off bits of skin that wash out with the water would
    // otherwise widen it, and the water around them would not count as out)
    std::vector<float> rs;
    st.skinTop = -1e9f;
    st.skinBottom = 1e9f;
    for (int i = b.skinFirst; i < b.skinFirst + b.skinCount; ++i) {
        rs.push_back(length(x[i] - st.skinCentre));
        st.skinTop = std::max(st.skinTop, x[i].y);
        st.skinBottom = std::min(st.skinBottom, x[i].y);
    }
    std::sort(rs.begin(), rs.end());
    const float rmax = rs[size_t(0.98 * (rs.size() - 1))];
    int out = 0;
    for (int i = b.fluidFirst; i < b.fluidFirst + b.fluidCount; ++i)
        if (length(x[i] - st.skinCentre) > rmax + s) ++out;
    st.leaked = float(out) / float(std::max(1, b.fluidCount));
    return st;
}

// ---- tests

int testDusus(const Config& c) {
    ParticleWorld w;
    const float s = spacingOf(c);
    w.begin(s);
    applyPhysics(w, c);
    const float h = c.f("test.dusus.yukseklik", 1.0f);
    w.addParticles(lattice(make_float3(-2 * s, h, -2 * s), 4, 4, 4, s), 1000.0f, 0);
    w.commit();
    const float dt = 1.0f / 60.0f;
    std::vector<float3> x, v;
    double prevT = 0.0;
    float prevY = h + 0.5f * s, prevV = 0.0f, hit = -1.0f;
    for (int f = 0; f < 120 && hit < 0.0f; ++f) {
        w.step(dt);
        w.readPositions(x);
        w.readVelocities(v);
        float y = 1e9f, vy = 0.0f;
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i].y < y) { y = x[i].y; vy = v[i].y; }
        // the bottom's lowest point reaches the ground: between this frame and the last, by its fall (y = y0 + v t - g t^2 / 2)
        const float bottom = y - 0.5f * s;
        if (bottom <= 1e-4f) {
            const float b0 = prevY - 0.5f * s, v0 = prevV;   // the last frame's height and speed (downward negative)
            float tau = dt;
            // solve b0 + v0 tau - g tau^2 / 2 = 0 in [0, dt]
            const float A = -0.5f * 9.81f, B = v0, C = b0;
            const float disc = B * B - 4 * A * C;
            if (disc >= 0.0f) tau = (-B - std::sqrt(disc)) / (2 * A);
            hit = float(prevT) + std::min(std::max(tau, 0.0f), dt);
        }
        prevT = w.time();
        prevY = y;
        prevV = vy;
    }
    const float drop = h;   // the cluster's bottom starts at y = h
    const float ideal = std::sqrt(2.0f * drop / 9.81f);
    const float err = std::fabs(hit - ideal) / ideal;
    std::printf("dusus: %.2f m, zemine %.4f s, sqrt(2h/g) = %.4f s, fark %.2f%%  [olcut < 1%%] %s\n", drop, hit, ideal, 100.0f * err, verdict(err < 0.01f));
    return err < 0.01f ? 0 : 1;
}

int testHacim(const Config& c) {
    ParticleWorld w;
    const float s = spacingOf(c);
    w.begin(s);
    applyPhysics(w, c);
    const int nx = c.i("test.hacim.n", 40), ny = c.i("test.hacim.yukseklik", 40);
    w.params.box = true;
    w.params.boxLo = make_float3(0, 0, 0);
    w.params.boxHi = make_float3(nx * s, 10.0f, nx * s);
    w.params.gravityRamp = 0.3f;
    w.addParticles(lattice(make_float3(0, 0, 0), nx, ny, nx, s), 1000.0f, 0);
    w.commit();
    const float seconds = c.f("test.hacim.sure", 3.0f);
    const bool trace = c.i("test.iz", 0) != 0;
    std::vector<float3> xv, vv;
    for (int f = 0; f < int(seconds * 60); ++f) {
        w.step(1.0f / 60.0f);
        if (trace && f % 15 == 14) {   // the fastest particle: where it is
            const PStats t = w.stats();
            w.readPositions(xv);
            w.readVelocities(vv);
            size_t m = 0, over1 = 0, over3 = 0;
            for (size_t i = 0; i < vv.size(); ++i) {
                if (length(vv[i]) > length(vv[m])) m = i;
                over1 += length(vv[i]) > 0.1f;
                over3 += length(vv[i]) > 0.3f;
            }
            std::printf("  > 0.1 m/s: %zu, > 0.3 m/s: %zu", over1, over3);
            std::printf("  t=%.2f  ort %.2f%%  en buyuk %.2f%%  en hizli %.3f m/s  (%.3f %.3f %.3f) v (%.2f %.2f %.2f)  %.2f ms\n", w.time(), 100 * t.densityMean,
                        100 * t.densityMax, t.maxSpeed, xv[m].x, xv[m].y, xv[m].z, vv[m].x, vv[m].y, vv[m].z, t.ms);
        }
    }
    const PStats st = w.stats();
    std::vector<float3> x;
    w.readPositions(x);
    // the settled height: the top layer, as the 99.5th percentile of the particles' tops
    std::vector<float> ys;
    for (const float3& p : x) ys.push_back(p.y + 0.5f * s);
    std::sort(ys.begin(), ys.end());
    const float top = ys[size_t(0.995 * (ys.size() - 1))];
    const float expect = ny * s;
    const float herr = (top - expect) / expect;
    const bool ok = st.densityMean < 0.02f && std::fabs(herr) < 0.02f;
    std::printf("hacim: %d tanecik, %.1f s; yogunluk sapmasi ort %.2f%%, en buyuk sikisma %.2f%%; yukseklik %.4f m (beklenen %.4f, %+.2f%%); en hizli "
                "%.3f m/s; adim %.2f ms  [olcut: ort < 2%%, yukseklik +-2%%] %s\n",
                st.particles, seconds, 100 * st.densityMean, 100 * st.densityMax, top, expect, 100 * herr, st.maxSpeed, st.ms, verdict(ok));
    return ok ? 0 : 1;
}

// Martin & Moyce 1952 (Phil. Trans. R. Soc. A 244, 312-324, doi:10.1098/rsta.1952.0006), column of width a and height 2a
// (n^2 = 2): the surge front Z = x/a against T = t sqrt(2g/a). The points as tabulated in the Lethe project's dam-break
// example (github.com/chaos-polymtl/lethe, examples/multiphysics/dam-break/dam-break-2d.py), which cites the paper.
const float kMMT[] = {0.0f, 0.41f, 0.84f, 1.19f, 1.43f, 1.63f, 1.82f, 1.97f, 2.2f, 2.32f, 2.5f, 2.64f, 2.82f, 2.96f};
const float kMMZ[] = {1.0f, 1.11f, 1.23f, 1.44f, 1.67f, 1.89f, 2.11f, 2.33f, 2.56f, 2.78f, 3.0f, 3.22f, 3.44f, 3.67f};
constexpr int kMMN = int(sizeof(kMMT) / sizeof(kMMT[0]));

struct DamBreak {
    float a = 0.3f, depth = 0.07f;
};
DamBreak setupDam(ParticleWorld& w, const Config& c) {
    const float s = spacingOf(c);
    DamBreak d;
    d.a = c.f("test.baraj.a", 0.3f);
    const int nx = int(std::round(d.a / s)), ny = 2 * nx, nz = c.i("test.baraj.derinlik", 10);
    d.a = nx * s;
    d.depth = nz * s;
    w.begin(s);
    applyPhysics(w, c);
    w.params.box = true;
    w.params.boxLo = make_float3(0, 0, 0);
    w.params.boxHi = make_float3(c.f("test.baraj.tank", 6.0f) * d.a, 10.0f, d.depth);
    w.addParticles(lattice(make_float3(0, 0, 0), nx, ny, nz, s), 1000.0f, 0);
    w.commit();
    return d;
}
// the surge's leading edge: the largest x among the particles near the floor (the 99.9th percentile: a lone droplet
// ahead does not count)
float damFront(const std::vector<float3>& x, float s) {
    std::vector<float> xs;
    for (const float3& p : x)
        if (p.y < 3.0f * s) xs.push_back(p.x + 0.5f * s);
    if (xs.empty()) return 0.0f;
    std::sort(xs.begin(), xs.end());
    return xs[size_t(0.999 * (xs.size() - 1))];
}

int testBaraj(const Config& c) {
    ParticleWorld w;
    const DamBreak d = setupDam(w, c);
    const float s = w.spacing();
    const float Tscale = std::sqrt(2.0f * 9.81f / d.a);
    std::vector<float> T, Z;
    std::vector<float3> x;
    const float dt = 1.0f / 60.0f / float(std::max(1, c.i("test.baraj.kare_bolme", 2)));   // finer frames for the curve
    while (true) {
        w.readPositions(x);
        T.push_back(float(w.time()) * Tscale);
        Z.push_back(damFront(x, s) / d.a);
        if (T.back() > 3.3f) break;
        w.step(dt);
    }
    auto simZ = [&](float t) {
        if (t <= T.front()) return Z.front();
        for (size_t k = 1; k < T.size(); ++k)
            if (T[k] >= t) return Z[k - 1] + (Z[k] - Z[k - 1]) * (t - T[k - 1]) / (T[k] - T[k - 1]);
        return Z.back();
    };
    auto rms = [&](float shift) {
        double e = 0;
        for (int k = 1; k < kMMN; ++k) {
            const float dz = simZ(kMMT[k] - shift) - kMMZ[k];
            e += dz * dz;
        }
        return float(std::sqrt(e / (kMMN - 1)));
    };
    float best = 0.0f, bestE = rms(0.0f);
    for (float sh = 0.0f; sh <= 0.3001f; sh += 0.01f)
        if (rms(sh) < bestE) { bestE = rms(sh); best = sh; }
    std::printf("baraj: a = %.3f m (%d tanecik genislik), derinlik %.3f m, %d tanecik; adim %.2f ms\n", d.a, int(std::round(d.a / s)), d.depth, w.count(),
                w.stats().ms);
    std::printf("   T      deney Z   benzetim Z (kaydirmasiz)   benzetim Z (kaydirma %.2f)\n", best);
    for (int k = 0; k < kMMN; ++k) std::printf("  %4.2f    %5.2f      %5.2f                     %5.2f\n", kMMT[k], kMMZ[k], simZ(kMMT[k]), simZ(kMMT[k] - best));
    const float raw = rms(0.0f);
    const bool ok = bestE < 0.15f;
    std::printf("baraj: Z farkinin kok ortalama karesi %.3f (kaydirmasiz %.3f; kaydirma %.2f, deneyde kapak acilisi aninda degil)  [olcut < 0.15] %s\n",
                bestE, raw, best, verdict(ok));
    return ok ? 0 : 1;
}

int testBalon(const Config& c) {
    const float s = spacingOf(c);
    const float R = c.f("test.balon.yaricap", 0.1f);
    const float3 at = make_float3(0, R + 0.5f * s + 0.002f, 0);
    int fails = 0;
    // 1: it rests on the ground and holds
    {
        ParticleWorld w;
        w.begin(s);
        applyPhysics(w, c);
        w.params.gravityRamp = 0.3f;
        const Balloon b = addBalloon(w, c, at, R);
        w.commit();
        std::vector<float3> x;
        for (int f = 0; f < 120; ++f) w.step(1.0f / 60.0f);
        PStats st = w.stats();
        w.readPositions(x);
        BalloonState bs = balloonState(b, x, s);
        const bool hold = bs.leaked < 0.01f && st.broken == 0 && st.densityMean < 0.03f && bs.skinTop - bs.skinBottom > R;
        std::printf("balon 1 (yerde 2 sn): %d deri + %d su tanecigi, %d bag; disari cikan su %.2f%%, kopan bag %d, su yogunluk sapmasi ort %.2f%%; "
                    "boy %.3f m (ilk %.3f), adim %.2f ms  [olcut: <1%% su, 0 bag, <3%%] %s\n",
                    b.skinCount, b.fluidCount, st.bonds, 100 * bs.leaked, st.broken, 100 * st.densityMean, bs.skinTop - bs.skinBottom, 2 * R, st.ms,
                    verdict(hold));
        fails += !hold;
        if (c.i("test.iz", 0))
            for (const int2& e : w.readBrokenEnds())
                std::printf("    kopan bag: (%.3f %.3f %.3f) - (%.3f %.3f %.3f)\n", x[e.x].x, x[e.x].y, x[e.x].z, x[e.y].x, x[e.y].y, x[e.y].z);
        // 2: a shot breaks the bonds in a small sphere at its side, half way up: the water runs out
        const float3 hole = balloonSide(b, x, 1.0f);
        std::printf("balon 2: delik (%.3f %.3f %.3f), su ustu %.3f m\n", hole.x, hole.y, hole.z, bs.skinTop);
        const int cut = w.breakBondsInSphere(hole, c.f("test.balon.delik", 2.5f) * s);
        std::printf("balon 2: yandan delik (yaricap %.1f mm): %d bag koptu\n", 1000 * c.f("test.balon.delik", 2.5f) * s, cut);
        float leak1 = 0.0f, leak3 = 0.0f;
        for (int f = 1; f <= 180; ++f) {
            w.step(1.0f / 60.0f);
            if (f == 60 || f == 180) {
                w.readPositions(x);
                (f == 60 ? leak1 : leak3) = balloonState(b, x, s).leaked;
            }
        }
        st = w.stats();
        const bool leaks = leak3 > 0.25f && st.broken < st.bonds / 10;
        std::printf("balon 2: akan su 1 sn %.1f%%, 3 sn %.1f%%; kopan bag %d / %d  [olcut: 3 sn'de > %%25, deri butun (< %%10 bag)] %s\n", 100 * leak1,
                    100 * leak3, st.broken, st.bonds, verdict(leaks));
        fails += !leaks;
    }
    // 3: a cube dropped on a fresh balloon: a light one bounces off, a heavy one bursts it
    for (int heavy = 0; heavy < 2; ++heavy) {
        ParticleWorld w;
        w.begin(s);
        applyPhysics(w, c);
        w.params.gravityRamp = 0.3f;
        const Balloon b = addBalloon(w, c, at, R);
        const float rho = heavy ? c.f("test.balon.agir_kup", 7850.0f) : c.f("test.balon.hafif_kup", 100.0f);
        const Cube k = addCube(w, c.f("test.balon.kup", 0.12f), rho, make_float3(5, 5, 5));
        w.commit();
        std::vector<float3> x;
        for (int f = 0; f < 90; ++f) w.step(1.0f / 60.0f);
        const float drop = c.f("test.balon.kup_yukseklik", 0.3f);
        w.readPositions(x);
        const BalloonState b0 = balloonState(b, x, s);
        w.placeGroup(k.group, make_float3(b0.skinCentre.x, b0.skinTop + 0.5f * k.side + drop, b0.skinCentre.z) - make_float3(5, 5, 5),
                     make_float3(0, 0, 0));
        float lowest = 1e9f;
        for (int f = 0; f < 150; ++f) {
            w.step(1.0f / 60.0f);
            w.readPositions(x);
            float cy = 0;
            for (int i = k.first; i < k.first + k.count; ++i) cy += x[i].y;
            lowest = std::min(lowest, cy / k.count);
        }
        const PStats st = w.stats();
        const BalloonState bs = balloonState(b, x, s);
        const float brokenShare = float(st.broken) / float(std::max(1, st.bonds));
        const bool ok = heavy ? (brokenShare > 0.05f || bs.leaked > 0.3f) : (st.broken < st.bonds / 100 && bs.leaked < 0.02f);
        std::printf("balon 3 (%s kup, %.2f kg, %.0f cm'den): kopan bag %.1f%%, disari cikan su %.1f%%, kupun en alcak merkezi %.3f m; en hizli %.2f m/s  "
                    "[olcut: %s] %s\n",
                    heavy ? "agir" : "hafif", k.mass, 100 * drop, 100 * brokenShare, 100 * bs.leaked, lowest, st.maxSpeed,
                    heavy ? "patlar (> %5 bag ya da > %30 su)" : "patlamaz (< %1 bag, < %2 su)", verdict(ok));
        fails += !ok;
    }
    std::printf("balon: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

// a third of a litre of water falls 10 cm onto the floor: it must come to rest as a puddle (one or two particles thick,
// the particles touching each other), not spread for ever as loose drops. 0.34 L one spacing (7 mm) thick would cover
// a disc of ~12 cm radius.
int testBirikinti(const Config& c) {
    ParticleWorld w;
    const float s = spacingOf(c);
    w.begin(s);
    applyPhysics(w, c);
    const int m = c.i("test.birikinti.n", 10);
    w.addParticles(lattice(make_float3(-0.5f * m * s, c.f("test.birikinti.yukseklik", 0.1f), -0.5f * m * s), m, m, m, s), 1000.0f, 0);
    w.commit();
    const float seconds = c.f("test.birikinti.sure", 4.0f);
    for (int f = 0; f < int(seconds * 60); ++f) w.step(1.0f / 60.0f);
    std::vector<float3> x, v;
    w.readPositions(x);
    w.readVelocities(v);
    int moving = 0, lonely = 0;
    std::vector<float> rad;
    float top = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        moving += length(v[i]) > 0.02f;
        rad.push_back(std::sqrt(x[i].x * x[i].x + x[i].z * x[i].z));
        top = std::max(top, x[i].y + 0.5f * s);
        bool near = false;
        for (size_t j = 0; j < x.size() && !near; ++j)
            if (j != i && length(x[j] - x[i]) < 1.5f * s) near = true;
        lonely += !near;
    }
    std::sort(rad.begin(), rad.end());
    const float r95 = rad[size_t(0.95 * (rad.size() - 1))];
    // the floor it covers: cells of one spacing with a particle over them, as a disc
    std::vector<long long> cells;
    for (const float3& p : x) cells.push_back((long long)(std::floor(p.x / s)) * 1000003LL + (long long)(std::floor(p.z / s)));
    std::sort(cells.begin(), cells.end());
    const size_t covered = size_t(std::unique(cells.begin(), cells.end()) - cells.begin());
    const float rCover = std::sqrt(covered * s * s / 3.14159265f), rMono = std::sqrt(x.size() * 0.8660254f * s * s / 3.14159265f);
    const float fm = float(moving) / x.size(), fl = float(lonely) / x.size();
    const bool ok = fm < 0.05f && fl < 0.05f && rCover < 2.0f * rMono;
    std::printf("birikinti: %zu tanecik (%.2f L), %.0f sn sonra: hareketli (> 2 cm/s) %.1f%%, yalniz %.1f%%, kapladigi alanin yaricapi %.3f m (tek kat "
                "sikisik %.3f m), en uzak (%%95) %.3f m, en ust %.1f mm  [olcut: < %%5, < %%5, < 2 x tek kat] %s\n",
                x.size(), x.size() * s * s * s * 1000.0f, seconds, 100 * fm, 100 * fl, rCover, rMono, r95, 1000 * top, verdict(ok));
    return ok ? 0 : 1;
}

// pouring: a tap 30 cm up feeds a box 20 x 20 cm for two seconds (appendFluid, a 4 x 4 particle stream at 1 m/s);
// everything added stays and settles, the level is the poured volume over the box's floor
int testDokme(const Config& c) {
    ParticleWorld w;
    const float s = spacingOf(c);
    w.begin(s);
    applyPhysics(w, c);
    const float side = 0.2f;
    w.params.box = true;
    w.params.boxLo = make_float3(-0.5f * side, 0, -0.5f * side);
    w.params.boxHi = make_float3(0.5f * side, 10.0f, 0.5f * side);
    w.addParticles(lattice(make_float3(-2 * s, 0, -2 * s), 4, 1, 4, s), 1000.0f, 0);   // a first layer on the floor
    w.reserve(8000);
    w.commit();
    const float speed = 1.0f;
    int poured = 16;
    double carry = 0.0;
    for (int f = 0; f < 180; ++f) {
        if (f < 120) {   // a column of layers, each a spacing below the last, at the tap's speed
            carry += speed / 60.0 / s;
            std::vector<float3> p, v;
            while (carry >= 1.0) {
                carry -= 1.0;
                for (int z = 0; z < 4; ++z)
                    for (int x = 0; x < 4; ++x) {
                        p.push_back(make_float3((x - 1.5f) * s, 0.3f - float(carry) * s, (z - 1.5f) * s));
                        v.push_back(make_float3(0, -speed, 0));
                    }
            }
            poured += w.appendFluid(p, v, 1000.0f);
        }
        w.step(1.0f / 60.0f);
    }
    for (int f = 0; f < 120; ++f) w.step(1.0f / 60.0f);
    const PStats st = w.stats();
    std::vector<float3> x;
    w.readPositions(x);
    std::vector<float> ys;
    for (const float3& p : x) ys.push_back(p.y + 0.5f * s);
    std::sort(ys.begin(), ys.end());
    const float level = ys[size_t(0.99 * (ys.size() - 1))];
    const float expect = poured * s * s * s / (side * side);
    const bool ok = w.count() == poured && st.maxSpeed < 0.5f && std::fabs(level - expect) < 1.5f * s;
    std::printf("dokme: %d tanecik eklendi (dunyada %d), seviye %.1f mm (beklenen %.1f), en hizli %.2f m/s, adim %.2f ms  "
                "[olcut: hepsi, dinlenir (< 0.5 m/s), seviye +-1.5 aralik] %s\n",
                poured, w.count(), 1000 * level, 1000 * expect, st.maxSpeed, st.ms, verdict(ok));
    return ok ? 0 : 1;
}

// liquefying: a bonded block of a PG_LIQUEFY group; the bonds in a sphere are cut: the particles that lost them all
// turn liquid and run out, the rest stays solid
int testSivilasma(const Config& c) {
    ParticleWorld w;
    const float s = spacingOf(c);
    w.begin(s);
    applyPhysics(w, c);
    const int m = 16;
    const int g = w.addGroup(PG_LIQUEFY);
    const int first = w.addParticles(lattice(make_float3(-0.5f * m * s, 0, -0.5f * m * s), m, m, m, s), 1000.0f, g);
    w.bondWithin(first, m * m * m, 1.75f * s, 1e-6f, 0.5f, 0.5f);
    w.commit();
    for (int f = 0; f < 30; ++f) w.step(1.0f / 60.0f);
    const float3 centre = make_float3(0, (m - 0.5f) * s, 0);   // the top face's middle: a crater
    const float r = 4.0f * s;
    const int cut = w.breakBondsInSphere(centre, r);
    for (int f = 0; f < 90; ++f) w.step(1.0f / 60.0f);
    std::vector<uint8_t> fl;
    w.readFluid(fl);
    int liquid = 0;
    for (uint8_t b : fl) liquid += b;
    // a particle loses all its bonds when it, or every partner it had, is inside the sphere: about the sphere's
    // particles that lie inside the block
    const float inside = 0.5f * (4.0f / 3.0f) * 3.14159265f * r * r * r / (s * s * s);
    const bool ok = liquid > 0.3f * inside && liquid < 2.0f * inside;
    std::printf("sivilasma: %d bag kesildi, %d tanecik sivilasti (kesilen kurenin bloktaki hacmi ~%.0f tanecik)  [olcut: 0.3x - 2x] %s\n", cut, liquid,
                inside, verdict(ok));
    return ok ? 0 : 1;
}

int testButce(const Config& c) {
    const float s = spacingOf(c);
    const int sizes[3] = {46, 58, 82};   // ~97k, ~195k, ~551k
    std::printf("butce (ekran karti baska islerle paylasiliyor: 3 tekrarin en kucugu):\n");
    for (int n : sizes) {
        float best = 1e9f;
        int count = 0;
        for (int rep = 0; rep < 3; ++rep) {
            ParticleWorld w;
            w.begin(s);
            applyPhysics(w, c);
            w.params.box = true;
            w.params.boxLo = make_float3(0, 0, 0);
            w.params.boxHi = make_float3(n * s * 1.5f, 10.0f, n * s * 1.5f);
            w.addParticles(lattice(make_float3(0, 0, 0), n, n, n, s), 1000.0f, 0);
            w.commit();
            for (int f = 0; f < 20; ++f) w.step(1.0f / 60.0f);   // the block starts to spread
            float sum = 0.0f;
            for (int f = 0; f < 10; ++f) {
                w.step(1.0f / 60.0f);
                sum += w.stats().ms;
            }
            best = std::min(best, sum / 10.0f);
            count = w.count();
        }
        std::printf("  %7d tanecik, %d alt adim x %d yineleme: %.2f ms/kare\n", count, std::max(1, c.i("fizik.alt_adim", 20)), std::max(1, c.i("fizik.yineleme", 1)),
                    best);
    }
    return 0;
}

// what a body costs (for T2's planning, not a physics test): the mannequin's particles (body.h), one bonded group per
// bone (bonds within 1.75 spacings, stiff tissue), no joints between bones; it falls to the floor in pieces while timed
int testTekrar(const Config& c) {
    const float s = spacingOf(c);
    std::vector<float3> run[2];
    for (int r = 0; r < 2; ++r) {
        ParticleWorld w;
        w.begin(s);
        applyPhysics(w, c);
        w.params.gravityRamp = 0.3f;
        const float R = 0.1f;
        addBalloon(w, c, make_float3(0, R + 0.5f * s + 0.002f, 0), R);
        const Cube k = addCube(w, 0.12f, 7850.0f, make_float3(5, 5, 5));
        w.commit();
        for (int f = 0; f < 60; ++f) w.step(1.0f / 60.0f);
        w.placeGroup(k.group, make_float3(0, 0.45f, 0) - make_float3(5, 5, 5), make_float3(0, 0, 0));
        for (int f = 0; f < 60; ++f) w.step(1.0f / 60.0f);
        w.readPositions(run[r]);
    }
    const bool same = run[0].size() == run[1].size() && std::memcmp(run[0].data(), run[1].data(), run[0].size() * sizeof(float3)) == 0;
    size_t diff = 0;
    for (size_t i = 0; i < std::min(run[0].size(), run[1].size()); ++i) diff += std::memcmp(&run[0][i], &run[1][i], sizeof(float3)) != 0;
    std::printf("tekrar: balon + agir kup, 2 sn, iki kosu: %zu tanecikten %zu farkli  [olcut: bit bit ayni] %s\n", run[0].size(), diff, verdict(same));
    return same ? 0 : 1;
}

}   // namespace

int runSolverTests(const Config& c, const std::string& only) {
    const double t0 = tnow();
    int fails = 0, ran = 0;
    auto run = [&](const char* name, int (*fn)(const Config&)) {
        if (!only.empty() && only != name) return;
        ++ran;
        fails += fn(c);
    };
    run("dusus", testDusus);
    run("hacim", testHacim);
    run("baraj", testBaraj);
    run("balon", testBalon);
    run("birikinti", testBirikinti);
    run("dokme", testDokme);
    run("sivilasma", testSivilasma);
    run("tekrar", testTekrar);
    run("butce", testButce);
    if (!ran) { std::printf("bilinmeyen test: %s (dusus, hacim, baraj, balon, birikinti, dokme, sivilasma, tekrar, butce)\n", only.c_str()); return 2; }
    std::printf("cozucu testleri: %d test, %d basarisiz (%.1f s)\n", ran, fails, tnow() - t0);
    return fails ? 1 : 0;
}

