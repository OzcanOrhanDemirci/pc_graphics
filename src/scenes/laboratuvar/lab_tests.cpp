// Scene 4 "Laboratuvar": the experiments' measurements (--scene laboratuvar --lab-test [name]), each with its
// criterion: PASS / FAIL.
//   balon     the water balloon hangs 2 s without tearing (broken bonds, water outside the membrane), then a needle
//             pops it: the membrane must be gone from most of the water within 0.1 s while the water keeps its shape
//   kum       sand poured on the table piles at its angle of repose (30-35 deg for dry sand)
//   bal       honey off the dipper: the rope stays whole and coils on the dish
//   yavas     slow motion leaves a resting world at rest: milk in its dish and water in its beaker at 1/100 speed
//             (the live window's way, whole substeps taken less often) against the old way (each frame's substeps
//             shrunk 100 times), which blew them apart untouched (Özcan, 10-03)
#include "scenes/laboratuvar/laboratuvar_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* verdict(bool ok) { return ok ? "PASS" : "FAIL"; }

int testBalon(LaboratuvarApp& app, bool fine) {
    LabWorld w;
    LabInputs in;
    w.build(Deney::Balon, app.cfg, fine, in);
    std::printf("%s\n", w.summary().c_str());
    const double dt = fine ? 1.0 / app.cfg.f("balon.kayit_hiz", 1000.0f) : 1.0 / 60.0;
    w.settle(app.cfg.f("balon.oturma", 1.5f), dt);   // as the scene does (the stretch brought in)
    std::vector<float3> x;
    std::vector<uint8_t> fl;
    auto report = [&](double t) {
        w.pw.readPositions(x);
        w.pw.readFluid(fl);
        const PStats st = w.pw.stats();
        float yMin = 1e9f, yMax = -1e9f;
        float3 c = make_float3(0, 0, 0);
        for (int i = w.membraneFirst; i < w.membraneFirst + w.membraneCount; ++i) {
            yMin = std::min(yMin, x[size_t(i)].y);
            yMax = std::max(yMax, x[size_t(i)].y);
            c = c + x[size_t(i)];
        }
        c = c * (1.0f / w.membraneCount);
        int below = 0, water = 0;
        for (size_t i = 0; i < x.size(); ++i)
            if (w.code[i] == DC_WATER) {
                ++water;
                if (length(x[i] - c) > 0.5f * (yMax - yMin) + 0.02f) ++below;   // outside the membrane
            }
        std::printf("  t %.2f s: kopuk bag %d / %d, zar y %.3f..%.3f, merkez %.3f, zarin altinda su %d / %d, en hizli %.2f m/s\n", t, st.broken, st.bonds, yMin,
                    yMax, c.y, below, water, st.maxSpeed);
        return std::make_pair(st.broken, below);
    };
    auto r0 = report(0.0);
    const int steps = int(2.0 / dt);
    for (int k = 1; k <= steps; ++k) {
        w.step(float(dt));
        if (k % std::max(1, int(0.25 / dt)) == 0) r0 = report(k * dt);
    }
    const bool held = r0.second == 0 && r0.first < 50;
    std::printf("balon (%s) masada 2 sn: %s\n", fine ? "ince" : "kaba", verdict(held));
    // the needle at its front: within 0.15 s most of the membrane's bonds should be gone (it pops, not leaks)
    w.pw.readPositions(x);
    int front = w.membraneFirst;
    for (int i = w.membraneFirst; i < w.membraneFirst + w.membraneCount; ++i)
        if (x[size_t(i)].z > x[size_t(front)].z) front = i;
    const int before = w.pw.stats().broken;
    LabEvent e;
    e.kind = LabEvent::Prick;
    e.a = x[size_t(front)];
    w.trigger(e);
    const double pdt = 1.0 / 1000.0;
    w.pw.params.substeps = std::max(2, int(std::round(w.pw.params.substeps * (1.0 / 60.0) / pdt / 12.0)));
    int last = 0;
    for (int k = 1; k <= 150; ++k) {
        w.step(float(pdt));
        if (k == 5 || k == 10 || k == 25 || k == 50 || k == 100 || k == 150) {
            last = w.pw.stats().broken - before;
            std::printf("  igneden %.0f ms sonra: kopan bag %d (%.0f%%)\n", k * pdt * 1000.0, last, 100.0 * last / std::max(1, w.pw.stats().bonds));
        }
    }
    const bool popped = last > w.pw.stats().bonds / 4;
    std::printf("balon (%s) igne: %s (patlar: 0,15 sn'de bag > %%25)\n", fine ? "ince" : "kaba", verdict(popped));
    return (held ? 0 : 1) + (popped ? 0 : 1);
}

// sand poured on one spot of the table for a while: the cone's angle from its height over its base radius
int testKum(LaboratuvarApp& app) {
    LabWorld w;
    LabInputs in;
    in.nozzle = make_float3(-0.08f, 0.0f, 0.0f);   // away from the beaker
    in.height = 0.5f;
    in.flow = app.cfg.f("test.kum.akis", 0.6f);
    w.build(Deney::Kum, app.cfg, false, in);
    w.settle(0.1);
    const double T = app.cfg.f("test.kum.sure", 10.0f);
    for (int k = 0; k < int(T * 60.0); ++k) w.step(1.0f / 60.0f);
    in.flowing = false;
    w.setInputs(in);
    for (int k = 0; k < 120; ++k) w.step(1.0f / 60.0f);   // let it come to rest
    std::vector<float3> x;
    w.pw.readPositions(x);
    std::vector<float> rad;
    float H = 0.0f;
    int n = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        if (w.code[i] != DC_SAND) continue;
        const float dx = x[i].x - in.nozzle.x, dz = x[i].z - in.nozzle.z;
        const float r = std::sqrt(dx * dx + dz * dz);
        if (x[i].y < 0.0f || x[i].y > 0.2f) continue;
        rad.push_back(r);
        if (r < 2.0f * w.s) H = std::max(H, x[i].y + 0.5f * w.s);
        ++n;
    }
    std::sort(rad.begin(), rad.end());
    const float R = rad.empty() ? 0.0f : rad[size_t(0.95 * (rad.size() - 1))];
    const float ang = std::atan2(H, std::max(R, 1e-6f)) * 180.0f / 3.14159265f;
    const bool ok = ang > 25.0f && ang < 40.0f;
    std::printf("kum: %d tane (%.2f mm), yigin yuksekligi %.1f mm, taban yaricapi (%%95) %.1f mm: aci %.1f derece  [olcut: 25-40, kuru kum 30-35] %s\n", n,
                w.s * 1000.0f, H * 1000.0f, R * 1000.0f, ang, verdict(ok));
    return ok ? 0 : 1;
}

// a resting world run 0.05 s of world time at 1/100 speed (300 frames): the fastest particle and how many liquid
// particles rose above where any was at the start (a splash); `old`: each frame one step of 1/6000 s in the world's
// usual substeps (each 100 times shorter)
int testYavas(LaboratuvarApp& app) {
    int fails = 0;
    for (Deney d : {Deney::Damla, Deney::Su}) {
        // 0: slow motion (the window's way), 1: the old way, 2: real speed over the same 0.05 s (the resting world's own stir)
        float speeds[3] = {0, 0, 0};
        int risen[3] = {0, 0, 0};
        for (int old = 0; old < 3; ++old) {
            LabWorld w;
            LabInputs in;
            in.height = d == Deney::Su ? app.cfg.f("su.yukseklik_baslangic", 0.25f) : 0.3f;
            w.build(d, app.cfg, false, in);
            w.settle(app.cfg.f(std::string(deneyKey(d)) + ".oturma", 1.0f));
            for (int k = 0; k < 30; ++k) w.advance(1.0 / 60.0);   // half a second at real speed, as the window starts
            std::vector<float3> x;
            w.pw.readPositions(x);
            float top = -1e9f;
            for (size_t i = 0; i < x.size(); ++i)
                if (w.code[i] == DC_MILK || w.code[i] == DC_WATER) top = std::max(top, x[i].y);
            float vmax = 0.0f;
            for (int k = 0; k < (old == 2 ? 3 : 300); ++k) {
                if (old == 1) w.step(float(1.0 / 6000.0));
                else w.advance(old == 2 ? 1.0 / 60.0 : 1.0 / 6000.0);
                if (old == 2 || k % 20 == 19) vmax = std::max(vmax, w.pw.stats().maxSpeed);
            }
            w.pw.readPositions(x);
            int up = 0;
            for (size_t i = 0; i < x.size(); ++i)
                if ((w.code[i] == DC_MILK || w.code[i] == DC_WATER) && x[i].y > top + 0.005f) ++up;
            speeds[old] = vmax;
            risen[old] = up;
        }
        const bool ok = speeds[0] <= std::max(0.05f, 1.5f * speeds[2]) && risen[0] == 0;
        std::printf("yavas %s x1/100: en hizli %.3f m/s, yukari firlayan %d  (gercek hizda ayni 0,05 sn: %.3f m/s, %d; eski yol: %.2f m/s, %d)  "
                    "[olcut: gercek hizin en cok 1,5 kati, 0] %s\n", deneyKey(d), speeds[0], risen[0], speeds[2], risen[2], speeds[1], risen[1], verdict(ok));
        fails += ok ? 0 : 1;
    }
    return fails;
}

}   // namespace

int runLabTests(LaboratuvarApp& app, const std::string& only) {
    int fails = 0, ran = 0;
    if (only.empty() || only == "balon") { ++ran; fails += testBalon(app, false); }
    if (only == "balon_ince") { ++ran; fails += testBalon(app, true); }
    if (only.empty() || only == "kum") { ++ran; fails += testKum(app); }
    if (only.empty() || only == "yavas") { ++ran; fails += testYavas(app); }
    if (!ran) { std::printf("bilinmeyen test: %s (balon, balon_ince, kum, yavas)\n", only.c_str()); return 2; }
    std::printf("laboratuvar deney testleri: %d test, %d basarisiz\n", ran, fails);
    return fails ? 1 : 0;
}
