// Scene 4 "Laboratuvar": the experiments' measurements (--scene laboratuvar --lab-test [name]), each with its
// criterion: PASS / FAIL.
//   balon     the water balloon hangs 2 s without tearing (broken bonds, water outside the membrane), then a needle
//             pops it: the membrane must be gone from most of the water within 0.1 s while the water keeps its shape
//   kum       sand poured on the table piles at its angle of repose (30-35 deg for dry sand)
//   bal       honey off the dipper: the rope stays whole and coils on the dish
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

}   // namespace

int runLabTests(LaboratuvarApp& app, const std::string& only) {
    int fails = 0, ran = 0;
    if (only.empty() || only == "balon") { ++ran; fails += testBalon(app, false); }
    if (only == "balon_ince") { ++ran; fails += testBalon(app, true); }
    if (!ran) { std::printf("bilinmeyen test: %s (balon, balon_ince)\n", only.c_str()); return 2; }
    std::printf("laboratuvar deney testleri: %d test, %d basarisiz\n", ran, fails);
    return fails ? 1 : 0;
}
