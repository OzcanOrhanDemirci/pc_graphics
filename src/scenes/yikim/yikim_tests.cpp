// Scene 3 "Yikim": physics tests (yikim.exe --test). Each prints a number against its expectation; the thresholds
// were written before the first run. Exit code 0 only if all pass.
#include "scenes/yikim/building.h"
#include "scenes/yikim/fly_camera.h"
#include "scenes/yikim/quake.h"
#include "scenes/yikim/yikim_scenes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

struct Result {
    std::string name, measured, expected;
    bool pass;
};
std::vector<Result> g_results;

void report(const std::string& name, bool pass, const char* fmtMeasured, double m, const char* fmtExpected, double e) {
    char a[128], b[128];
    std::snprintf(a, sizeof(a), fmtMeasured, m);
    std::snprintf(b, sizeof(b), fmtExpected, e);
    g_results.push_back({name, a, b, pass});
    std::printf("  %-44s %-24s %-28s %s\n", name.c_str(), a, b, pass ? "GECTI" : "KALDI");
    std::fflush(stdout);
}

const float kDt = 1.0f / 60.0f;
const Config* g_cfg = nullptr;
void applyParams(PhysWorld& w) {   // solver settings from scenes/yikim.cfg (and --set)
    if (!g_cfg) return;
    w.params.substeps = g_cfg->i("phys.substeps", w.params.substeps);
    w.params.relax = g_cfg->f("phys.relax", w.params.relax);
    w.params.slop = g_cfg->f("phys.slop", w.params.slop);
    w.params.contactIters = g_cfg->i("phys.contact_iters", w.params.contactIters);
    w.params.interfaceIters = g_cfg->i("phys.interface_iters", w.params.interfaceIters);
    w.params.dampAlpha = g_cfg->f("phys.damp_alpha", w.params.dampAlpha);
    w.params.maxDepenetration = g_cfg->f("phys.max_depenetration", w.params.maxDepenetration);
}

double kinetic(const PhysWorld& w) {
    double e = 0;
    for (size_t i = 1; i < w.info.size(); ++i) {
        if (w.kinematic[i]) continue;
        const double m = w.info[i].mass;
        e += 0.5 * m * dot(w.vel[i], w.vel[i]);
        const float3 wl = qrotInv(w.rot[i], w.angVel[i]);
        const float3 iI = w.invInertia[i];
        e += 0.5 * ((iI.x > 0 ? wl.x * wl.x / iI.x : 0) + (iI.y > 0 ? wl.y * wl.y / iI.y : 0) + (iI.z > 0 ? wl.z * wl.z / iI.z : 0));
    }
    return e;
}
double potential(const PhysWorld& w, double g) {
    double e = 0;
    for (size_t i = 1; i < w.info.size(); ++i)
        if (!w.kinematic[i]) e += w.info[i].mass * g * w.pos[i].y;
    return e;
}
float maxSpeed(const PhysWorld& w) {
    float m = 0;
    for (size_t i = 1; i < w.info.size(); ++i)
        if (!w.kinematic[i]) m = std::max(m, length(w.vel[i]));
    return m;
}

void testFreeFall(const Config&) {
    PhysWorld w;
    const float drop = 3.0f;
    buildFreeFall(w, drop);
    applyParams(w);
    w.upload();
    const double g = w.params.gravity;
    const double T = std::sqrt(2.0 * drop / g);
    double tTouch = -1;
    double prevBottom = drop, prevVdown = 0, prevT = 0;
    for (int k = 0; k < 60 * 5; ++k) {
        w.step(kDt);
        w.download();
        const double bottom = w.pos[1].y - 0.5;
        // the impact frame: the fall stops (the downward speed drops by more than a frame of gravity could add back).
        // "Bottom within 1 mm at the frame's end" missed it when the box had already bounced off by then.
        if (tTouch < 0 && k > 0 && -w.vel[1].y < prevVdown - 1.0) {
            // the bottom crossed y = 0 inside this frame: from the measured state of the previous frame, under g
            tTouch = prevT + (-prevVdown + std::sqrt(prevVdown * prevVdown + 2.0 * g * prevBottom)) / g;
        }
        if (g_cfg && g_cfg->i("test.verbose", 0) && (k <= 3 || (k >= 44 && k <= 48)))
            std::printf("      t=%.3f  alt %.4f m  v (%.3f %.3f %.3f)  w (%.3f %.3f %.3f)\n", w.time, bottom, w.vel[1].x, w.vel[1].y, w.vel[1].z, w.angVel[1].x,
                        w.angVel[1].y, w.angVel[1].z);
        prevBottom = bottom;
        prevVdown = -w.vel[1].y;
        prevT = w.time;
    }
    report("serbest dusme: 3 m'den yere varis (s)", std::fabs(tTouch - T) < 0.005, "%.4f", tTouch, "%.4f (+-0.005)", T);
    w.download();
    const double rest = w.pos[1].y - 0.5;
    report("serbest dusme: 5 sn sonra yere gomulme (mm)", rest > -0.003 && rest < 0.002, "%.2f", rest * 1000.0, "-3..+2 mm (%g)", 0.0);
    report("serbest dusme: 5 sn sonra hiz (m/s)", maxSpeed(w) < 0.01f, "%.4f", maxSpeed(w), "< %.2f", 0.01);
}

void testFallTiming(const Config&) {
    // the free-fall phase itself: position at 0.5 s against y0 - g t^2 / 2 (no contact yet)
    PhysWorld w;
    buildFreeFall(w, 3.0f);
    applyParams(w);
    w.upload();
    for (int k = 0; k < 30; ++k) w.step(kDt);
    w.download();
    const double t = w.time, g = w.params.gravity;
    const double expect = 3.5 - 0.5 * g * t * t;
    report("serbest dusme: 0,5 sn'de yukseklik (m)", std::fabs(w.pos[1].y - expect) < 0.01, "%.4f", w.pos[1].y, "%.4f (+-0.01)", expect);
}

void testStacks(const Config&) {
    PhysWorld w;
    w.clear();
    buildTower(w, 10, 0.5f);
    buildPyramid(w, 5, 1.0f, 4.0f);
    applyParams(w);
    w.upload();
    std::vector<double3> start = w.pos;
    float vmaxLate = 0;
    for (int k = 0; k < 60 * 30; ++k) {
        w.step(kDt);
        if (k >= 60 * 29) { w.download(); vmaxLate = std::max(vmaxLate, maxSpeed(w)); }
    }
    w.download();
    double drift = 0, drop = 0;
    for (size_t i = 1; i < w.pos.size(); ++i) {
        drift = std::max(drift, double(std::sqrt(sqr(float(w.pos[i].x - start[i].x)) + sqr(float(w.pos[i].z - start[i].z)))));
        drop = std::max(drop, start[i].y - w.pos[i].y);
    }
    if (g_cfg && g_cfg->i("test.verbose", 0))
        for (size_t i = 1; i < w.pos.size(); ++i) {
            const double dxz = std::sqrt(sqr(float(w.pos[i].x - start[i].x)) + sqr(float(w.pos[i].z - start[i].z)));
            if (dxz > 0.005 || length(w.vel[i]) > 0.005f)
                std::printf("      govde %2zu  baslangic (%.2f %.2f %.2f)  simdi (%.2f %.2f %.2f)  hiz %.3f\n", i, start[i].x, start[i].y, start[i].z,
                            w.pos[i].x, w.pos[i].y, w.pos[i].z, length(w.vel[i]));
        }
    report("kule (10) + piramit (15): son 1 sn en buyuk hiz", vmaxLate < 0.01f, "%.4f m/s", vmaxLate, "< %.2f m/s", 0.01);
    report("kule + piramit: 30 sn'de yatay kayma", drift < 0.01, "%.4f m", drift, "< %.2f m", 0.01);
    report("kule + piramit: 30 sn'de cokme", drop < 0.01, "%.4f m", drop, "< %.2f m", 0.01);
}

void testSlope(const Config&) {
    {
        PhysWorld w;
        const int b = buildSlope(w, 25.0f);
        applyParams(w);
    w.upload();
        const double3 p0 = w.pos[b];
        for (int k = 0; k < 180; ++k) {
            w.step(kDt);
            if (g_cfg && g_cfg->i("test.verbose", 0) && k % 15 == 0) {
                w.download();
                const PhysStats st = w.stats();
                // the block's lowest corner against the ramp's top plane, and its tilt against the ramp
                const float4 q = w.rot[b];
                const double a = 25.0 * 3.14159265358979 / 180.0;
                const float3 ny = make_float3(float(-std::sin(a)), float(std::cos(a)), 0.0f);   // ramp normal
                float lowest = 1e9f;
                for (const double3& v : w.shapes[b].vertices()) {
                    const float3 pw = f3(w.pos[b]) + qrot(q, f3(v));
                    lowest = std::min(lowest, dot(pw - make_float3(0, 5, 0), ny));
                }
                const float3 up = qrot(q, qrotInv(w.rot[b], make_float3(0, 0, 0)) + make_float3(0, 0, 0));
                (void)up;
                std::printf("      t=%.2f  yol %.4f m  v (%.4f %.4f %.4f)  w (%.4f %.4f %.4f)  temas %d  en alt kose %.2f mm  q (%.5f %.5f %.5f %.5f)\n",
                            w.time, dlength(w.pos[b] - p0), w.vel[b].x, w.vel[b].y, w.vel[b].z, w.angVel[b].x, w.angVel[b].y, w.angVel[b].z,
                            st.contacts, lowest * 1000.0f, q.x, q.y, q.z, q.w);
            }
        }
        w.download();
        const double d = dlength(w.pos[b] - p0);
        report("egik duzlem 25 derece (tan 0,47 < mu 0,7): kayma", d < 0.01, "%.4f m", d, "< %.2f m", 0.01);
    }
    {
        PhysWorld w;
        const int b = buildSlope(w, 40.0f);
        applyParams(w);
        w.upload();
        const double3 p0 = w.pos[b];
        for (int k = 0; k < 60; ++k) {
            w.step(kDt);
            if (g_cfg && g_cfg->i("test.verbose", 0) && k % 6 == 5) {
                w.download();
                const double a40 = 3.14159265358979 * 40.0 / 180.0;
                std::printf("      40 derece t=%.2f  yol %.4f m  yama normal kuvveti / (m g cos) = %.3f\n", w.time, dlength(w.pos[b] - p0),
                            w.patchForce(b) / (w.info[b].mass * w.params.gravity * std::cos(a40)));
            }
        }
        w.download();
        const double d = dlength(w.pos[b] - p0);
        const double a = 3.14159265358979 * 40.0 / 180.0;
        const double expect = 0.5 * w.params.gravity * (std::sin(a) - w.params.muDynamic * std::cos(a)) * 1.0;
        report("egik duzlem 40 derece: 1 sn'de kayma", std::fabs(d / expect - 1.0) < 0.15, "%.3f m", d, "%.3f m (+-15%%)", expect);
    }
}

void testCantilever(const Config&) {
    PhysMaterial m;
    m.ft = m.fc = m.cohesion = 1e12f;   // elastic: the test measures the solver's stiffness, not the strength
    PhysWorld w;
    const int segs = 10;
    const float L = 0.5f, b = 0.3f, h = 0.5f;
    const int tip = buildCantilever(w, segs, L, b, h, m);
    applyParams(w);
    w.upload();
    const double y0 = w.pos[tip].y;
    const double E = m.E, I = b * h * h * h / 12.0, q = 2500.0 * 9.81 * b * h, Ltot = segs * L;
    // tip of a uniformly loaded cantilever: q L^4 / (8 E I); the tip body's centre sits L/2 from the end:
    // deflection there w(x) = q x^2 (6 L^2 - 4 L x + x^2) / (24 E I)
    const double x = Ltot - L * 0.5;
    const double wStatic = q * x * x * (6 * Ltot * Ltot - 4 * Ltot * x + x * x) / (24.0 * E * I);
    const double f1 = 1.875104 * 1.875104 / (2.0 * 3.14159265358979) * std::sqrt(E * I / (2500.0 * b * h * Ltot * Ltot * Ltot * Ltot));
    std::vector<double> ys, ts;
    for (int k = 0; k < 60 * 6; ++k) {
        w.step(kDt);   // the real frame rate; the 10 Hz mode gets 6 samples a period (crossings interpolated)
        w.download();
        ys.push_back(w.pos[tip].y - y0);
        ts.push_back(w.time);
    }
    // the tip oscillates about its static deflection: mean over whole periods, frequency from upward crossings
    double mean = 0;
    for (double y : ys) mean += y;
    mean /= ys.size();
    std::vector<double> ups;
    for (size_t k = 1; k < ys.size(); ++k)
        if (ys[k - 1] < mean && ys[k] >= mean) ups.push_back(ts[k - 1] + (ts[k] - ts[k - 1]) * (mean - ys[k - 1]) / (ys[k] - ys[k - 1]));
    double freq = 0;
    if (ups.size() >= 3) freq = (ups.size() - 1) / (ups.back() - ups.front());
    double amp = 0;
    for (size_t k = ys.size() / 2; k < ys.size(); ++k) amp = std::max(amp, std::fabs(ys[k] - mean));
    report("konsol kiris: uc sehim / analitik", -mean / wStatic > 0.7 && -mean / wStatic < 1.5, "%.3f", -mean / wStatic, "0,7..1,5 (%.2f mm)",
           wStatic * 1000.0);
    report("konsol kiris: 1. frekans / analitik", freq / f1 > 0.7 && freq / f1 < 1.3, "%.3f", freq / f1, "0,7..1,3 (%.2f Hz)", f1);
    std::printf("      (sehim %.2f mm, frekans %.2f Hz, son 2 sn genlik %.2f mm)\n", -mean * 1000.0, freq, amp * 1000.0);
}

// a plastic hinge: a 6 m cantilever (0.3 x 0.5, C16, S220, 0.8% steel) under its own weight. Root moment
// q L^2 / 2 = 66 kN m against the fibre section's yield moment ~38 kN m: the root must yield, the tip sag on and on,
// and the bars snap. A 3 m one (16.5 kN m) must hold with only elastic sag.
void testHinge(const Config& cfg) {
    const PhysMaterial mat = concreteFromConfig(cfg);
    for (int run = 0; run < 2; ++run) {
        PhysWorld w;
        const int segs = run == 0 ? 12 : 6;
        const int tip = buildCantilever(w, segs, 0.5f, 0.3f, 0.5f, mat, 0.008f);
        applyParams(w);
        w.upload();
        const double y0 = w.pos[tip].y;
        double sag1 = 0, sag3 = 0;
        for (int k = 0; k < 60 * 3; ++k) {
            w.step(kDt);
            if (k == 59) { w.download(); sag1 = y0 - w.pos[tip].y; }
        }
        w.download();
        sag3 = y0 - w.pos[tip].y;
        const PhysStats st = w.stats();
        const double L = segs * 0.5, q = 2500.0 * 9.81 * 0.15;
        std::printf("      konsol %.0f m: kok momenti %.1f kN m; uc cokmesi 1 sn %.3f m, 3 sn %.3f m; catlak %d, kirik %d lif\n", L, q * L * L / 2000.0, sag1,
                    sag3, st.cracked, st.broken);
        if (run == 0) report("mafsal: 6 m konsol (66 kN m > ~38) 3 sn'de duser", sag3 > 1.0, "%.3f m", sag3, "> %.1f m", 1.0);
        else report("mafsal: 3 m konsol (16,5 kN m < ~38) durur", sag3 < 0.02, "%.3f m", sag3, "< %.2f m", 0.02);
    }    // a slab strip 1 m wide, 0.12 m thick, 0.4% steel, 4500 kg/m^3 (with finishes and contents), cantilevered 2 m:
    // 10.6 kN m at the root against ~3.7 kN m of the fibre section: it must fall. 0.6 m (1.0 kN m) must hold.
    for (int run = 0; run < 2; ++run) {
        PhysWorld w;
        const int segs = run == 0 ? 4 : 1;
        const float L = run == 0 ? 0.5f : 0.6f;
        const int tip = buildCantilever(w, segs, L, 1.0f, 0.12f, mat, 0.004f, 4500.0f);
        applyParams(w);
        w.upload();
        const double y0 = w.pos[tip].y;
        for (int k = 0; k < 60 * 3; ++k) w.step(kDt);
        w.download();
        const double sag = y0 - w.pos[tip].y;
        const PhysStats st = w.stats();
        const double Lt = segs * L, q = 4500.0 * 9.81 * 0.12;
        std::printf("      doseme konsolu %.1f m: kok momenti %.1f kN m; uc cokmesi 3 sn %.3f m; catlak %d, kirik %d lif\n", Lt, q * Lt * Lt / 2000.0, sag,
                    st.cracked, st.broken);
        if (run == 0) report("mafsal: 2 m doseme konsolu (10,6 kN m > ~3,7) duser", sag > 1.0, "%.3f m", sag, "> %.1f m", 1.0);
        else report("mafsal: 0,6 m doseme konsolu (1 kN m) durur", sag < 0.02, "%.3f m", sag, "< %.2f m", 0.02);
    }
}

void testEnergy(const Config&) {
    PhysWorld w;
    w.clear();
    buildPile(w, 40, 12345u);
    applyParams(w);
    w.upload();
    w.download();
    const double g = w.params.gravity;
    const double E0 = kinetic(w) + potential(w, g);
    double hMax = 0;
    for (size_t i = 1; i < w.pos.size(); ++i) hMax = std::max(hMax, w.pos[i].y);
    double worstRise = 0, Emax = E0;
    float vmax = 0;
    std::vector<double> hist;
    for (int k = 0; k < 60 * 15; ++k) {
        w.step(kDt);
        w.download();
        const double E = kinetic(w) + potential(w, g);
        hist.push_back(E);
        Emax = std::max(Emax, E);
        vmax = std::max(vmax, maxSpeed(w));
        if (hist.size() > 60) worstRise = std::max(worstRise, E - hist[hist.size() - 61]);
    }
    const double scale = E0 - potential(w, g) + 1e-9;   // energy released by the fall
    report("enerji: hic bir an baslangictan fazla", Emax <= E0 * 1.0 + 0.01 * scale, "%.3e J", Emax - E0, "<= %.2e J (%%1)", 0.01 * scale);
    report("enerji: 1 sn'de en buyuk artis / dusus enerjisi", worstRise / scale < 0.01, "%.4f", worstRise / scale, "< %.2f", 0.01);
    report("enerji: en buyuk hiz / serbest dusus hizi", vmax / std::sqrt(2 * g * hMax) < 1.1, "%.3f", vmax / std::sqrt(2 * g * hMax), "< %.1f", 1.1);
}

// determinism: the same pile run twice must end bit for bit in the same place (the accumulators are fixed point)
void testDeterminism(const Config&) {
    std::vector<double3> end[2];
    for (int run = 0; run < 2; ++run) {
        PhysWorld w;
        w.clear();
        buildPile(w, 40, 777u);
        applyParams(w);
        w.upload();
        for (int k = 0; k < 60 * 6; ++k) w.step(kDt);
        w.download();
        end[run] = w.pos;
    }
    double worst = 0;
    for (size_t i = 1; i < end[0].size(); ++i) worst = std::max(worst, dlength(end[0][i] - end[1][i]));
    report("determinizm: ayni yigin iki kez, 6 sn sonra en buyuk fark", worst == 0.0, "%.3g m", worst, "%.0f m", 0.0);
}

void testFrame(const Config& cfg) {
    PhysWorld w;
    const FrameSpec spec = frameSpecFromConfig(cfg);
    PhysMaterial mat = concreteFromConfig(cfg);
    if (cfg.i("test.elastic", 0)) mat.ft = mat.fc = mat.cohesion = mat.fy = 1e12f;
    std::vector<Piece> P = buildFrame(w, spec, mat);
    applyParams(w);
    w.upload();
    double weight = 0;
    for (size_t i = 1; i < w.info.size(); ++i) weight += w.info[i].mass * w.params.gravity;
    const std::vector<double3> start = w.pos;
    std::vector<int> kindOf(w.info.size(), -1);
    for (const Piece& p : P) kindOf[p.body] = p.kind;
    const char* kn[] = {"kolon", "kiris", "doseme", "duvar", "diger"};
    auto pairName = [&](int a, int b) {
        if (a == 0 || b == 0) return std::string("zemin-") + kn[kindOf[a ? a : b]];
        int ka = kindOf[a], kb = kindOf[b];
        if (ka > kb) std::swap(ka, kb);
        return std::string(kn[ka]) + "-" + kn[kb];
    };
    float vLate = 0;
    bool reported = false;
    for (int k = 0; k < 60 * 10; ++k) {
        w.step(kDt);
        if (k >= 60 * 9) { w.download(); vLate = std::max(vLate, maxSpeed(w)); }
        // the first cracks: where, and the ground reaction against the weight
        if (!reported && (k % 6 == 5 || k == 0)) {
            std::vector<unsigned> fl;
            std::vector<float3> fo;
            w.fibres(fl, fo);
            size_t fi = 0;
            double ground = 0;
            std::map<std::string, int> cracked, total;
            for (const InterfaceDesc& d : w.interfaceDescs) {
                const std::string nm = pairName(d.a, d.b);
                for (size_t q = 0; q < d.fibers.size(); ++q, ++fi) {
                    if (d.b == 0 || d.a == 0) ground += -fo[fi].x;   // compression carries the weight
                    total[nm]++;
                    if (fl[fi] & 1u) cracked[nm]++;
                }
            }
            int nc = 0;
            for (auto& c : cracked) nc += c.second;
            if (nc > 0 || k == 0 || k == 5) {
                std::printf("      t=%.2f s  zemin tepkisi / agirlik = %.3f  catlak:", w.time, ground / weight);
                for (auto& t : total) std::printf(" %s %d/%d", t.first.c_str(), cracked[t.first], t.second);
                std::printf("\n");
                if (nc > 0) reported = true;
            }
        }
    }
    w.download();
    double sag = 0;
    for (size_t i = 1; i < w.pos.size(); ++i) sag = std::max(sag, start[i].y - w.pos[i].y);
    if (cfg.i("test.verbose", 0)) {   // the bodies that sank most, with their interface count
        std::vector<int> order;
        for (size_t i = 1; i < w.pos.size(); ++i) order.push_back(int(i));
        std::sort(order.begin(), order.end(), [&](int a, int b) { return start[a].y - w.pos[a].y > start[b].y - w.pos[b].y; });
        std::vector<int> nIfc(w.info.size(), 0);
        for (const InterfaceDesc& d : w.interfaceDescs) { nIfc[d.a]++; nIfc[d.b]++; }
        for (int r = 0; r < 12 && r < int(order.size()); ++r) {
            const int i = order[r];
            std::printf("      govde %4d %-7s  baslangic (%.2f %.2f %.2f)  cokme %.3f m  arayuz %d\n", i, kn[kindOf[i]], start[i].x, start[i].y, start[i].z,
                        start[i].y - w.pos[i].y, nIfc[i]);
        }
    }
    if (cfg.i("test.profile", 0)) w.profile(kDt);
    PhysStats st = w.stats();
    std::printf("      cerceve: %d govde, %d arayuz, %d lif, %d renk, fizik %.2f ms/kare, agirlik %.0f kN\n", st.bodies, st.interfaces, st.fibres,
                st.colours, st.ms, weight / 1000.0);
    report("cerceve: oz agirlikla 10 sn, kirik lif", st.broken == 0, "%.0f", double(st.broken), "%.0f", 0.0);
    report("cerceve: oz agirlikla en buyuk sehim", sag < 0.02, "%.4f m", sag, "< %.2f m", 0.02);
    report("cerceve: 10. sn en buyuk hiz", vLate < 0.01f, "%.4f m/s", vLate, "< %.2f m/s", 0.01);
    std::printf("      (catlak lif %d)\n", st.cracked);
}

// the apartment block: settles (gravity ramped) and stands 10 s with no fibre broken; then a push shaped like the
// first mode (v = 0.05 m/s * y / H) and the roof's free swing gives the first period. Thresholds written before the
// first run: no broken fibre, sag < 2 cm, T1 between 0.2 and 1.0 s (code formula 0.07 H^0.75 = 0.54 s for 15 m;
// infills stiffen a real one).
void testBuilding(const Config& cfg) {
    PhysWorld w;
    const ApartmentSpec as = apartmentSpecFromConfig(cfg);
    Building ap = buildApartment(w, as, concreteFromConfig(cfg), brickFromConfig(cfg));
    applyParams(w);
    w.upload();
    w.settle(cfg.f("phys.settle_seconds", 2.0f), cfg.f("phys.settle_ramp", 0.5f), kDt);
    if (cfg.i("test.profile", 0)) w.profile(kDt);
    w.download();
    const std::vector<double3> start = w.pos;
    for (int k = 0; k < 60 * 10; ++k) w.step(kDt);
    w.download();
    double sag = 0;
    for (size_t i = 1; i < w.pos.size(); ++i) sag = std::max(sag, start[i].y - w.pos[i].y);
    PhysStats st = w.stats();
    std::printf("      bina: %d govde, %d arayuz, %d lif, %d renk (alt sinir %d), fizik %.2f ms/kare, cati %.1f m\n", st.bodies, st.interfaces, st.fibres,
                st.colours, st.maxDegree, st.ms, ap.height);
    report("bina: oturduktan sonra 10 sn, kirik lif", st.broken == 0, "%.0f", double(st.broken), "%.0f", 0.0);
    report("bina: 10 sn'de en buyuk cokme", sag < 0.02, "%.4f m", sag, "< %.2f m", 0.02);
    std::printf("      (catlak lif %d)\n", st.cracked);
    // the first period: a first-mode-shaped push along x, the roof's swing about its mean
    std::vector<int> roof;
    for (const Piece& p : ap.pieces)
        if (p.kind != PK_WALL && p.hi.y > ap.height - 1e-6) roof.push_back(p.body);
    for (size_t i = 1; i < w.info.size(); ++i)
        if (!w.kinematic[i]) w.setVelocity(int(i), make_float3(float(0.05 * w.pos[i].y / ap.height), 0.0f, 0.0f), make_float3(0, 0, 0));
    std::vector<double> xs, ts;
    for (int k = 0; k < 60 * 4; ++k) {
        w.step(kDt);
        w.download();
        double x = 0;
        for (int b : roof) x += w.pos[b].x - start[b].x;
        xs.push_back(x / roof.size());
        ts.push_back(w.time);
    }
    double mean = 0;
    for (double x : xs) mean += x;
    mean /= xs.size();
    std::vector<double> ups;
    for (size_t k = 1; k < xs.size(); ++k)
        if (xs[k - 1] < mean && xs[k] >= mean) ups.push_back(ts[k - 1] + (ts[k] - ts[k - 1]) * (mean - xs[k - 1]) / (xs[k] - xs[k - 1]));
    const double T = ups.size() >= 3 ? (ups.back() - ups.front()) / (ups.size() - 1) : 0.0;
    double amp = 0;
    for (double x : xs) amp = std::max(amp, std::fabs(x - mean));
    report("bina: 1. dogal periyot (x)", T > 0.2 && T < 1.0, "%.3f s", T, "0,2..1,0 s (formul %.2f)", 0.07 * std::pow(ap.height, 0.75));
    std::printf("      (cati genligi %.1f mm, %zu gecis)\n", amp * 1000.0, ups.size());
    // damping: the logarithmic decrement of the swing's peaks (one peak per period, between upward crossings).
    // Threshold written before the first run: 2..7% of critical (reinforced concrete buildings ~5%).
    std::vector<double> peaks;
    for (size_t c = 0; c + 1 < ups.size(); ++c) {
        double pk = 0;
        for (size_t k = 0; k < xs.size(); ++k)
            if (ts[k] >= ups[c] && ts[k] < ups[c + 1]) pk = std::max(pk, xs[k] - mean);
        peaks.push_back(pk);
    }
    // least-squares slope of ln(peak) per cycle: modes beating against each other make single peaks unreliable
    double zeta = 0;
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        int m = 0;
        for (size_t c = 0; c < peaks.size(); ++c)
            if (peaks[c] > 0) {
                const double x = double(c), y = std::log(peaks[c]);
                sx += x; sy += y; sxx += x * x; sxy += x * y; ++m;
            }
        if (m >= 3) zeta = -((m * sxy - sx * sy) / (m * sxx - sx * sx)) / (2.0 * 3.14159265358979);
    }
    report("bina: sonum orani (serbest titresim)", zeta > 0.02 && zeta < 0.07, "%.3f", zeta, "0,02..0,07 (beton ~%.2f)", 0.05);
    std::printf("      (tepeler:");
    for (double pk : peaks) std::printf(" %.2f", pk * 1000.0);
    std::printf(" mm)\n");
}

// The free camera's directions against the picture: a point straight ahead must move to the screen's left when the
// camera steps right (D) and when the view turns right (mouse dragged right). Özcan found both mirrored (10-02).
void testControls(const Config&) {
    FlyCamera c;
    c.lookAt(make_float3(-15.0f, 9.0f, -17.0f), make_float3(6.0f, 6.5f, 7.0f));
    YCamera cam{};
    c.apply(cam, 1920, 1080);
    const float3 ahead = cam.pos + cam.fwd * 20.0f;
    FlyCamera m = c;
    m.move(0.0f, 1.0f, 0.0f, 1.0f);   // D
    YCamera cm{};
    m.apply(cm, 1920, 1080);
    report("kontrol: D ile saga adim, onumuzdeki nokta", screenX(cm, ahead) < -0.01f, "x %.3f", screenX(cm, ahead), "< 0 (%.0f: sola kayar)", 0.0);
    FlyCamera l = c;
    l.look(100.0f, 0.0f, 0.0025f);    // mouse right
    YCamera cl{};
    l.apply(cl, 1920, 1080);
    report("kontrol: fare saga, onumuzdeki nokta", screenX(cl, ahead) < -0.01f, "x %.3f", screenX(cl, ahead), "< 0 (%.0f: sola kayar)", 0.0);
    FlyCamera w = c;
    w.move(1.0f, 0.0f, 0.0f, 1.0f);   // W: nearer, still ahead
    YCamera cw{};
    w.apply(cw, 1920, 1080);
    report("kontrol: W ileri, nokta yaklasir", length(ahead - cw.pos) < length(ahead - cam.pos), "%.2f m", length(ahead - cw.pos), "< %.2f m", length(ahead - cam.pos));
    FlyCamera u = c;
    u.look(0.0f, 100.0f, 0.0025f);    // mouse down: look down
    report("kontrol: fare asagi, bakis asagi", u.forward().y < c.forward().y, "%.3f", u.forward().y, "< %.3f", c.forward().y);
    FlyCamera v = c;
    v.look(0.0f, -300.0f, 0.0025f);   // look up, then W: it climbs
    const float y0 = v.pos.y;
    v.move(1.0f, 0.0f, 0.0f, 1.0f);
    report("kontrol: yukari bakip W, kamera yukselir", v.pos.y > y0 + 0.1f, "%.2f m", v.pos.y - y0, "> %.2f m", 0.1);
}

// The quake for any length (quake.h). Longer than a magnitude's own it loops the record's strong part: where the
// loop wraps, the ground's position, speed and acceleration must not step more than they do from one sample to the
// next inside it (a step there is a jolt the building would feel every period), and it must not wander off: it may
// reach about as far from where it started as the magnitude's own record does.
void testQuakeLoop(const Config&) {
#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif
    double seam[3] = {0, 0, 0}, reach = 0;
    int worstReach = 0;
    auto hlen = [](double3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
    for (int m = quakeMagnitudeMin(); m <= quakeMagnitudeMax(); ++m) {
        QuakePlay loop, own;
        std::string err;
        if (!quakePlay(m, -1.0, PROJECT_ROOT, loop, err) || !quakePlay(m, 0.0, PROJECT_ROOT, own, err)) {
            report("deprem dongusu: kayit yuklenir", false, "%.0f", 0.0, "%.0f", 1.0);
            std::printf("    %s\n", err.c_str());
            return;
        }
        const std::vector<double3>* series[3] = {&loop.rec.disp, &loop.rec.vel, &loop.rec.acc};
        for (int k = 0; k < 3; ++k) {
            const std::vector<double3>& x = *series[k];
            double inside = 0.0;
            for (size_t i = 0; i + 1 < x.size(); ++i) inside = std::max(inside, hlen(x[i + 1] - x[i]));
            seam[k] = std::max(seam[k], hlen(x.front() - x.back()) / std::max(inside, 1e-12));
        }
        double far = 0.0, farOwn = 0.0;
        for (const double3& d : loop.rec.disp) far = std::max(far, hlen(d));
        for (const double3& d : own.rec.disp) farOwn = std::max(farOwn, hlen(d));
        if (far / std::max(farOwn, 1e-9) > reach) { reach = far / std::max(farOwn, 1e-9); worstReach = m; }
        std::printf("      buyukluk %d: dongu %.1f sn, zeminin en uzak gittigi %.3f m (kendi kaydi %.3f m)\n", m, loop.rec.dt * loop.rec.disp.size(), far,
                    farOwn);
    }
    report("deprem dongusu: dikiste yer degistirme / icteki en buyuk adim", seam[0] <= 1.0, "%.2f", seam[0], "<= %.1f", 1.0);
    report("deprem dongusu: dikiste hiz / icteki en buyuk adim", seam[1] <= 1.0, "%.2f", seam[1], "<= %.1f", 1.0);
    report("deprem dongusu: dikiste ivme / icteki en buyuk adim", seam[2] <= 1.0, "%.2f", seam[2], "<= %.1f", 1.0);
    report("deprem dongusu: en uzak / kendi kaydininki (en kotusu " + std::to_string(worstReach) + ")", reach <= 2.0, "%.2f", reach, "<= %.1f", 2.0);
}

}   // namespace

int runYikimTests(const Config& cfg, const std::string& only) {
    g_cfg = &cfg;
    std::printf("YIKIM fizik testleri (alt adim %d)\n", cfg.i("phys.substeps", 20));
    struct T { const char* name; void (*fn)(const Config&); };
    const T tests[] = {{"dusme", testFallTiming}, {"dusme", testFreeFall}, {"yigin", testStacks}, {"egim", testSlope},
                       {"konsol", testCantilever}, {"mafsal", testHinge}, {"enerji", testEnergy}, {"determinizm", testDeterminism}, {"cerceve", testFrame}, {"bina", testBuilding}, {"kontrol", testControls},
                       {"dongu", testQuakeLoop}};
    for (const T& t : tests)
        if (only.empty() || only == t.name) t.fn(cfg);
    int fails = 0;
    for (const Result& r : g_results) fails += !r.pass;
    std::printf("%d / %zu gecti\n", int(g_results.size()) - fails, g_results.size());
    return fails ? 1 : 0;
}
