#include "scenes/ates/ates_app.h"

#include "core/config.h"
#include "core/image_io.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// the middle x-y slice of the gas as a picture: temperature, volatiles, oxygen, heat release, soot side by side
void writeSlice(const AtesGas& g, const std::string& path) {
    const int nx = g.nx, ny = g.ny, k = g.nz / 2;
    const int panels = 5;
    std::vector<unsigned char> img(size_t(nx) * panels * ny * 4, 255);
    const std::vector<float> T = g.download(0), F = g.download(1), O = g.download(3), Q = g.download(6), S = g.download(4);
    auto put = [&](int p, int i, int j, float v, int map) {
        v = std::clamp(v, 0.0f, 1.0f);
        float r, gg, b;
        if (map == 0) { r = std::min(1.0f, 3 * v); gg = std::clamp(3 * v - 1, 0.0f, 1.0f); b = std::clamp(3 * v - 2, 0.0f, 1.0f); }
        else { r = gg = b = v; }
        const size_t o = (size_t(ny - 1 - j) * nx * panels + size_t(p) * nx + i) * 4;
        img[o] = (unsigned char)(r * 255);
        img[o + 1] = (unsigned char)(gg * 255);
        img[o + 2] = (unsigned char)(b * 255);
    };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const size_t c = i + size_t(nx) * (j + size_t(ny) * k);
            put(0, i, j, (T[c] - 288.0f) / 2000.0f, 0);
            put(1, i, j, F[c], 1);
            put(2, i, j, O[c] / 0.232f, 1);
            put(3, i, j, std::sqrt(Q[c] / 2e7f), 0);
            put(4, i, j, std::sqrt(S[c] / 0.01f), 1);
        }
    writePNG(path, nx * panels, ny, img.data());
}

// the gas alone over a burner on the floor (PLAN §2 and F1): the fire science's measures against the correlations.
// Writes <out>.csv (the time series) and <out>_eksen.csv (the centreline's mean temperature and speed by height).
int gasTest(Config& cfg, const std::string& root, const std::string& out, int steps) {
    cudaStream_t st;
    cudaStreamCreateWithFlags(&st, cudaStreamNonBlocking);
    AtesGas g;
    const int nx = cfg.i("gas.nx", 128), ny = cfg.i("gas.ny", 192), nz = cfg.i("gas.nz", 128);
    const float h = cfg.f("gas.h", 0.0075f);
    g.vcycles = cfg.i("gas.vcycles", 2);
    g.k.mixC = cfg.f("gas.mixC", g.k.mixC);
    g.k.vortEps = cfg.f("gas.vort", g.k.vortEps);
    g.k.sootA = cfg.f("gas.sootA", g.k.sootA);
    g.k.Tcft = cfg.f("gas.Tcft", g.k.Tcft);
    g.k.buoy = cfg.f("gas.buoy", g.k.buoy);
    g.k.buoyLocal = cfg.f("gas.buoyLocal", g.k.buoyLocal);
    g.k.maxExpand = cfg.f("gas.maxExpand", g.k.maxExpand);
    g.k.maccormack = cfg.f("gas.mc", g.k.maccormack);
    g.k.conserve = cfg.i("gas.conserve", 1) != 0;
    g.k.smag = cfg.f("gas.smag", g.k.smag);
    g.k.smagMomentum = cfg.f("gas.smagV", g.k.smagMomentum);
    g.k.nbMargin = cfg.f("gas.nbMargin", g.k.nbMargin);
    g.k.TignF = cfg.f("gas.TignF", g.k.TignF);
    g.k.Tcond = cfg.f("gas.Tcond", g.k.Tcond);
    g.init(nx, ny, nz, h, make_float3(-0.5f * nx * h, 0.0f, -0.5f * nz * h), st);
    g.profile = cfg.i("test.prof", 0) != 0;
    const float D = cfg.f("test.D", 0.4f), Q = cfg.f("test.Q", 20.0f) * 1000.0f;
    const float dt = cfg.f("gas.dt", 1.0f / 120.0f);
    g.burner.centre = make_float3(0, 0, 0);
    g.burner.radius = 0.5f * D;
    g.burner.power = Q;
    std::printf("ates gaz: %dx%dx%d, h %.1f mm (%.2f x %.2f x %.2f m), %zu MB, %d duzey; brulor D %.2f m, Q %.1f kW, dt %.4f s\n", nx, ny, nz, h * 1e3f,
                nx * h, ny * h, nz * h, g.bytes >> 20, g.levels, D, Q / 1000.0f, dt);
    const std::string csv = (fs::path(root) / (out + ".csv")).generic_string();
    fs::create_directories(fs::path(csv).parent_path());
    FILE* f = std::fopen(csv.c_str(), "w");
    if (!f) return 1;
    std::fprintf(f, "t,hrr_kW,rad_kW,fuel_gs,maxT,tip_m,slab_W,resid,step_ms\n");
    const int warm = int(cfg.f("test.warm", 3.0f) / dt);
    std::vector<double> meanT(ny, 0.0), meanV(ny, 0.0), meanQ(ny, 0.0);   // (meanQ: the heat released per height, W/m)
    int samples = 0;
    double ms = 0.0, injected = 0.0, burnt = 0.0, leftGrid = 0.0, made = 0.0;
    const double t0 = nowMs();
    for (int s = 0; s < steps; ++s) {
        const float t = s * dt;
        // a pilot for the first half second (the fuel leaves the burner at 450 K and would not light by itself)
        g.burner.temp = t < 0.5f ? cfg.f("test.pilotT", 1200.0f) : 450.0f;
        g.step(dt);
        ms += g.stats.stepMs;
        // every other step: the flame's tip (the highest reacting cell) and the light of the slab at a third of the
        // expected flame height (its flicker: the puffing)
        if (s % 2 == 0) {
            const std::vector<float> q = g.download(6);
            const std::vector<float> rad = g.download(7);
            int tipJ = -1;
            double slab = 0.0;
            const int js = std::max(1, int(0.15f / h));
            for (int j = 0; j < ny; ++j) {
                float rowMax = 0.0f;
                double rowRad = 0.0, rowQ = 0.0;
                for (int k = 0; k < nz; ++k)
                    for (int i = 0; i < nx; ++i) {
                        const size_t c = i + size_t(nx) * (j + size_t(ny) * k);
                        rowMax = std::max(rowMax, q[c]);
                        rowRad += rad[c];
                        rowQ += q[c];
                    }
                if (rowMax > 2e5f) tipJ = j;   // 0.2 MW/m^3: a burning cell
                if (std::abs(j - js) <= 2) slab += rowRad * h * h * h;
                if (s >= warm) meanQ[j] += rowQ * h * h;
            }
            std::fprintf(f, "%.4f,%.3f,%.3f,%.4f,%.1f,%.4f,%.3f,%.2e,%.3f\n", t, g.stats.hrr / 1000.0f, g.stats.rad / 1000.0f, g.stats.fuelIn * 1000.0f,
                         g.stats.maxT, tipJ >= 0 ? (tipJ + 1) * h : 0.0f, slab, g.stats.residual, g.stats.stepMs);
            if (s >= warm) {
                const std::vector<float> T = g.download(0);
                const std::vector<float> V = g.downloadV();
                for (int j = 0; j < ny; ++j) {
                    double tsum = 0.0, vsum = 0.0;
                    for (int dk = -1; dk <= 0; ++dk)
                        for (int di = -1; di <= 0; ++di) {
                            const size_t c = (nx / 2 + di) + size_t(nx) * (j + size_t(ny) * (nz / 2 + dk));
                            tsum += T[c];
                            vsum += V[c];
                        }
                    meanT[j] += tsum / 4.0;
                    meanV[j] += vsum / 4.0;
                }
                ++samples;
            }
        }
        if (cfg.i("test.debug", 0) && s % 10 == 0 && s > 300) {
            const std::vector<float> V = g.downloadV();
            const std::vector<float> T = g.download(0);
            float vm = 0.0f, tm = 0.0f;
            size_t at = 0;
            for (size_t c = 0; c < V.size(); ++c) {
                if (!(std::fabs(V[c]) <= vm)) { vm = std::fabs(V[c]); at = c; }
                tm = std::max(tm, T[c]);
            }
            {
                const std::vector<float> D = g.download(8), P = g.download(9), Fu = g.download(1), Ox = g.download(3);
                float dm = 0.0f, pm = 0.0f, fm = 0.0f, om = 0.0f;
                size_t dat = 0;
                for (size_t c = 0; c < D.size(); ++c) {
                    if (std::fabs(D[c]) > dm) { dm = std::fabs(D[c]); dat = c; }
                    pm = std::max(pm, std::fabs(P[c]));
                    fm = std::max(fm, Fu[c]);
                    om = std::max(om, Ox[c]);
                }
                std::printf("    s %d: max|div| %.1f at (%zu %zu %zu) T %.0f F %.3f O %.3f, max|phi| %.3e, max F %.3f, max O %.3f\n", s, dm, dat % nx,
                            (dat / nx) % ny, dat / (size_t(nx) * ny), T[dat], Fu[dat], Ox[dat], pm, fm, om);
            }
            std::printf("    s %d: max|v| %.2f at (%zu %zu %zu), max T %.0f\n", s, vm, at % nx, (at / nx) % ny, at / (size_t(nx) * ny), tm);
        }
        const int sliceEvery = cfg.i("test.slice", 0);
        if (sliceEvery > 0 && s % sliceEvery == 0) {
            char nm[64];
            std::snprintf(nm, sizeof(nm), "_dilim_%05d.png", s);
            writeSlice(g, (fs::path(root) / (out + nm)).generic_string());
        }
        injected += g.stats.fuelIn * dt;
        leftGrid += g.stats.fuelOut * dt;
        made += g.stats.fuelMade * dt;
        if (s % 120 == 0) {
            std::printf("  t %.2f s: HRR %.1f kW, isinim %.1f kW (%%%.0f), en sicak %.0f K, artik %.1e, adim %.2f ms\n", t, g.stats.hrr / 1000.0f,
                        g.stats.rad / 1000.0f, 100.0f * g.stats.rad / std::max(g.stats.hrr, 1.0f), g.stats.maxT, g.stats.residual, g.stats.stepMs);
            if (cfg.i("test.budget", 0)) {
                // the fuel's books: what is in the grid (as volatiles, soot, smoke) against what came in and burnt
                const std::vector<float> T = g.download(0), Fu = g.download(1), So = g.download(4), Sm = g.download(5);
                double inGrid = 0.0;
                for (size_t c = 0; c < T.size(); ++c) inGrid += (g.k.rhoT / T[c]) * (Fu[c] + So[c] + Sm[c]) * h * h * h;
                std::printf("    yakit: girdi %.3f g, izgarada %.3f g, yanan %.3f g (HRR'den), cikan %.3f g, tasimanin yaptigi %.3f g\n", injected * 1e3,
                            inGrid * 1e3, burnt * 1e3, leftGrid * 1e3, made * 1e3);
            }
        }
        burnt += g.stats.hrr / g.k.hcF * dt;
    }
    std::fclose(f);
    const std::string ax = (fs::path(root) / (out + "_eksen.csv")).generic_string();
    if (FILE* e = std::fopen(ax.c_str(), "w")) {
        std::fprintf(e, "z_m,T_K,v_ms,q_Wm\n");
        for (int j = 0; j < ny; ++j)
            std::fprintf(e, "%.4f,%.1f,%.3f,%.1f\n", (j + 0.5f) * h, meanT[j] / std::max(samples, 1), meanV[j] / std::max(samples, 1),
                         meanQ[j] / std::max(samples, 1));
        std::fclose(e);
    }
    if (g.profile)
        std::printf("ates gaz: adim parcalari (ms): hucreler %.2f, hiz tasima %.2f, kuvvetler %.2f, izdusum %.2f, skaler tasima %.2f, karisma %.2f\n",
                    g.sectionMs[0] / steps, g.sectionMs[1] / steps, g.sectionMs[2] / steps, g.sectionMs[3] / steps, g.sectionMs[4] / steps, g.sectionMs[5] / steps);
    std::printf("ates gaz: %d adim, ortalama %.2f ms/adim (GPU), duvar saati %.0f ms -> %s\n", steps, ms / steps, nowMs() - t0, csv.c_str());
    g.release();
    cudaStreamDestroy(st);
    return 0;
}

// each log's surface (debug.wood): mean char depth, ash, surface temperature, and its heat balance
void printLogs(const AtesApp& a) {
    std::printf("  kul yatagi %.0f K (%.2f m)\n", a.wood.bedT, a.wood.bedR);
    std::vector<float> cd(a.wood.patches), as(a.wood.patches), st(a.wood.patches), pr;
    if (a.wood.probe) {
        pr.resize(size_t(a.wood.patches) * 8);
        cudaMemcpy(pr.data(), a.wood.probe, pr.size() * 4, cudaMemcpyDeviceToHost);
    }
    cudaMemcpy(cd.data(), a.wood.charD, cd.size() * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(as.data(), a.wood.ash, as.size() * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(st.data(), a.wood.surfT, st.size() * 4, cudaMemcpyDeviceToHost);
    for (size_t l = 0; l < a.wood.logs.size(); ++l) {
        const ALog& L = a.wood.logs[l];
        double c = 0, s2 = 0, t = 0, tmax = 0;
        const int n = L.nTheta * L.nZ;
        for (int k = 0; k < n; ++k) {
            c += cd[L.firstPatch + k];
            s2 += as[L.firstPatch + k];
            t += st[L.firstPatch + k];
            tmax = std::max(tmax, double(st[L.firstPatch + k]));
        }
        std::printf("  kutuk %zu: r %.1f mm (simdi %.1f), komur %.2f mm, kul %.2f mm, yuzey %.0f K (en %.0f), kutle %.0f g, yer (%.3f %.3f %.3f)%s\n", l,
                    L.r0 * 1e3f, L.radius * 1e3f, c / n * 1e3, s2 / n * 1e3, t / n, tmax, L.mass * 1e3f, L.pos.x, L.pos.y, L.pos.z, L.sleeping ? " uyuyor" : "");
        if (pr.empty()) continue;
        // its surface's balance, the mean over its patches (W/m^2): what it absorbs and keeps of its char's heat
        // against what it radiates and gives the gas
        double m[8] = {};
        for (int k = 0; k < n; ++k)
            for (int j = 0; j < 8; ++j) m[j] += pr[size_t(L.firstPatch + k) * 8 + j] / n;
        std::printf("      O2 %.3f, gaz %.0f K, h %.1f W/m2K, hiz %.2f m/s | emilen %.1f, komur isisi %.1f | isinan %.1f, tasinan %.1f kW/m2\n", m[0], m[1],
                    m[2], m[7], m[3] * 1e-3, m[4] * 1e-3, m[5] * 1e-3, m[6] * 1e-3);
    }
}

// the fire left alone in real time, as scripted runs (PLAN §2.1; the others, the hand's, went with the hand when the
// scene was closed): pictures at fixed times of the gas's clock and nothing else. One crouches before the pit, looking
// at the foot of the fire.
struct ScenarioMove {
    float t;
    enum { Picture } what;
};
struct Scenario {
    const char* name;
    const char* layout;
    float length;   // seconds of the gas
    std::vector<ScenarioMove> moves;
    float3 aim = make_float3(0.06f, 0.03f, 0.0f);   // where the eye looks: the coals between the logs
};

std::vector<Scenario> scenarios() {
    using M = ScenarioMove;
    std::vector<Scenario> s;
    // left alone in real time: it keeps burning
    s.push_back({"kendi", "ates", 180.0f, {{60.0f, M::Picture}, {180.0f, M::Picture}}});
    return s;
}

// one run: <out>.csv holds a row per half second of the gas's clock (the step's sums averaged over it), the pictures
// <out>_<t>s.png; the summary goes to stdout
int runScenario(AtesApp& a, const Scenario& sc, const std::string& root, const std::string& out) {
    // from the side the laid logs leave open (they lie along x), at the scenario's aim
    AtesCam hand;
    const float3 at = sc.aim;
    hand.feet = make_float3(at.x + 1.3f, 0.0f, at.z);
    hand.eyeHeight = 0.71f;
    hand.yaw = 1.5707963f;
    hand.pitch = -std::atan2(hand.eyeHeight - at.y, 1.3f);
    a.cam = hand;
    const float dtGas = a.cfg.f("gas.dt", 1.0f / 120.0f);
    const std::string base = (fs::path(root) / out).generic_string();
    std::error_code ec;
    fs::create_directories(fs::path(base).parent_path(), ec);
    FILE* csv = std::fopen((base + ".csv").c_str(), "w");
    if (!csv) {
        std::fprintf(stderr, "ates: %s.csv yazilamadi\n", base.c_str());
        return 1;
    }
    std::fprintf(csv, "t_gaz_s,t_odun_s,hrr_kW,isinim_kW,gaz_yakit_gs,piroliz_gs,kor_yanmasi_gs,kutle_kg,kor_cm2,en_sicak_yuzey_K,en_sicak_gaz_K,kivilcim_dogan,odun_sayisi\n");
    std::vector<ScenarioMove> moves = sc.moves;
    std::stable_sort(moves.begin(), moves.end(), [](const ScenarioMove& x, const ScenarioMove& y) { return x.t < y.t; });
    size_t next = 0;
    const float length = a.cfg.f("senaryo.sure", sc.length);   // (a shorter run for a quick look)
    double hrr = 0, rad = 0, fuel = 0, pyro = 0, charB = 0, maxT = 0;
    int n = 0;
    float peak = 0.0f, peakAt = 0.0f;
    const double t0 = nowMs();
    const int steps = int(std::ceil(length / dtGas));
    for (int s = 0; s <= steps; ++s) {
        const float t = s * dtGas;
        while (next < moves.size() && moves[next].t <= t + 0.5f * dtGas) {
            const ScenarioMove& m = moves[next++];
            switch (m.what) {
            case ScenarioMove::Picture: {
                // DLSS's frames through first; the fire's clock goes on by them
                for (int k = 0; k < (a.R.dlssOn() ? 3 * ATES_PHASES : 4); ++k) {
                    a.update(1.0f / 60.0f);
                    a.render(1.0f / 60.0f);
                }
                char nm[32];
                std::snprintf(nm, sizeof(nm), "_%03ds", int(std::lround(m.t)));
                a.save(base + nm);
                if (a.wood.probe) {
                    std::printf("ates: %.1f s\n", t);
                    printLogs(a);
                }
                break;
            }
            }
        }
        if (s == steps) break;
        a.update(dtGas);
        hrr += a.gas.stats.hrr;
        rad += a.gas.stats.rad;
        fuel += a.gas.stats.fuelIn;
        pyro += a.wood.stats.pyrolysis;
        charB += a.wood.stats.charBurn;
        maxT = std::max(maxT, double(a.gas.stats.maxT));
        ++n;
        if (n * dtGas >= 0.5f - 1e-4f) {
            const float h = float(hrr / n);
            if (h > peak) {
                peak = h;
                peakAt = t;
            }
            a.sparks.readStats();
            int alive = 0;
            for (const ALog& L : a.wood.logs) alive += L.alive ? 1 : 0;
            std::fprintf(csv, "%.2f,%.1f,%.2f,%.2f,%.3f,%.3f,%.3f,%.4f,%.0f,%.0f,%.0f,%u,%d\n", t + dtGas, a.woodTime, h / 1000.0f, rad / n / 1000.0, fuel / n * 1e3, pyro / n * 1e3,
                         charB / n * 1e3, a.wood.stats.mass, a.wood.stats.emberArea * 1e4f, a.wood.stats.hottest, maxT, a.sparks.stats.born, alive);
            hrr = rad = fuel = pyro = charB = maxT = 0;
            n = 0;
        }
    }
    std::fclose(csv);
    std::printf("ates: senaryo %s, %.0f s gaz (odun %.0f s) %.0f s'de; en yuksek HRR %.1f kW (%.1f s), son: HRR %.2f kW, kutle %.3f kg, kor %.0f cm2 -> %s.csv\n",
                sc.name, length, a.woodTime, (nowMs() - t0) / 1000.0, peak / 1000.0f, peakAt, a.gas.stats.hrr / 1000.0f, a.wood.stats.mass,
                a.wood.stats.emberArea * 1e4f, base.c_str());
    return 0;
}

}   // namespace

int runAtesCli(int argc, char** argv) {
    std::string root = PROJECT_ROOT, out;
    std::vector<std::string> overrides;
    std::vector<int> views;
    std::vector<AtesCam> freeCams;
    int gas = 0, bench = 0, seq = 0, relay = 0;
    float settle = 4.0f, seqFps = 30.0f, turn = 0.0f;
    bool live = false, freeze = false;
    std::string scenario;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (s == "--set") overrides.push_back(next());
        else if (s == "--root") root = next();
        else if (s == "--out") out = next();
        else if (s == "--gaz") gas = std::stoi(next());
        else if (s == "--view") views.push_back(std::stoi(next()));
        else if (s == "--sure") settle = std::stof(next());
        else if (s == "--bench") bench = std::stoi(next());
        else if (s == "--yeniden") relay = std::stoi(next());
        else if (s == "--seri") seq = std::stoi(next());
        else if (s == "--fps") seqFps = std::stof(next());
        else if (s == "--canli") live = true;
        else if (s == "--dondur") freeze = true;
        else if (s == "--donus") turn = std::stof(next());
        else if (s == "--senaryo") scenario = next();
        else if (s == "--cam") {
            AtesCam c;
            if (std::sscanf(next().c_str(), "%f %f %f %f %f %f", &c.feet.x, &c.eyeHeight, &c.feet.z, &c.yaw, &c.pitch, &c.fovY) >= 5) freeCams.push_back(c);
        }
    }
    if (gas > 0) {
        Config cfg;
        cfg.load(root + "/scenes/ates.cfg");
        for (const auto& o : overrides) {
            const size_t eq = o.find('=');
            if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
        }
        return gasTest(cfg, root, out.empty() ? "captures/ates/gaz" : out, gas);
    }
    // the scene without a window: views after `settle` seconds of fire, a series of frames, the frame's cost
    const Scenario* sc = nullptr;
    static const std::vector<Scenario> all = scenarios();
    if (!scenario.empty()) {
        for (const Scenario& c : all)
            if (scenario == c.name) sc = &c;
        if (!sc) {
            std::fprintf(stderr, "ates: senaryo yok: %s (", scenario.c_str());
            for (const Scenario& c : all) std::fprintf(stderr, " %s", c.name);
            std::fprintf(stderr, " )\n");
            return 1;
        }
        overrides.insert(overrides.begin(), std::string("layout=") + sc->layout);
    }
    AtesApp a;
    a.root = root;
    a.overrides = overrides;
    a.loadConfig();
    a.loadAssets();
    std::string err;
    if (!a.initRenderer(err)) {
        std::fprintf(stderr, "ates: %s\n", err.c_str());
        return 1;
    }
    a.initSim();
    if (a.cfg.i("dlss", 1) != 0) {
        if (a.setDlss(true)) std::printf("ates: DLSS-RR acik\n");
        else std::printf("ates: DLSS-RR kapali: %s\n", a.R.dlssError.c_str());
    }
    if (out.empty()) out = "captures/ates/ates";
    std::error_code ec;
    fs::create_directories(fs::path(root) / fs::path(out).parent_path(), ec);
    std::printf("%s\n", a.summary().c_str());
    if (sc) {
        if (a.cfg.i("debug.wood", 0)) cudaMalloc(reinterpret_cast<void**>(&a.wood.probe), size_t(a.wood.capacity) * 8 * sizeof(float));
        const int rc = runScenario(a, *sc, root, out == "captures/ates/ates" ? std::string("captures/ates/senaryo_") + sc->name : out);
        a.release();
        return rc;
    }
    // the fire's first seconds (the gas fills, the flames stand up): simulated at the gas's own step
    const double t0 = nowMs();
    const float dtGas = a.cfg.f("gas.dt", 1.0f / 120.0f);
    a.cam = AtesApp::view(views.empty() ? 0 : views[0]);
    if (a.cfg.i("debug.wood", 0)) cudaMalloc(reinterpret_cast<void**>(&a.wood.probe), size_t(a.wood.capacity) * 8 * sizeof(float));
    for (float t = 0.0f; t < settle; t += dtGas) a.update(dtGas);
    std::printf("ates: %.1f s ates %.0f ms'de (HRR %.1f kW, piroliz %.2f g/s, kor %.0f cm2, en sicak yuzey %.0f K)\n", settle, nowMs() - t0, a.gas.stats.hrr / 1000.0f,
                a.wood.stats.pyrolysis * 1e3f, a.wood.stats.emberArea * 1e4f, a.wood.stats.hottest);
    std::printf("ates: odun %.3f kg, kor yanmasi %.3f g/s, gaz yakit girdisi %.3f g/s, isinim %.1f kW, en sicak gaz %.0f K\n", a.wood.stats.mass,
                a.wood.stats.charBurn * 1e3f, a.gas.stats.fuelIn * 1e3f, a.gas.stats.rad / 1000.0f, a.gas.stats.maxT);
    if (a.cfg.i("debug.wood", 0)) printLogs(a);
    if (a.cfg.i("debug.slice", 0)) {
        // the middle slice, and the flame's reach: the highest burning cell (0.2 MW/m^3) and the highest glowing soot
        // (over 1000 K), and how many cells are over 2300 K (the stoichiometric flame's adiabatic temperature)
        writeSlice(a.gas, (fs::path(root) / (out + "_dilim.png")).generic_string());
        const std::vector<float> T = a.gas.download(0), S = a.gas.download(4), q = a.gas.download(6);
        int tipQ = -1, tipS = -1;
        size_t over = 0;
        for (size_t c = 0; c < T.size(); ++c) {
            const int j = int((c / size_t(a.gas.nx)) % size_t(a.gas.ny));
            if (q[c] > 2e5f) tipQ = std::max(tipQ, j);
            if (T[c] > 1000.0f && (a.gas.k.rhoT / T[c]) * S[c] / a.gas.k.rhoSoot > 1e-7f) tipS = std::max(tipS, j);
            if (T[c] > 2300.0f) ++over;
        }
        std::printf("ates: alev ucu %.2f m (yanan), %.2f m (parlayan is), 2300 K ustu %zu hucre\n", (tipQ + 1) * a.gas.h, (tipS + 1) * a.gas.h, over);
        // the smoke and the unburnt volatiles the grid holds (g), the densest smoke (g/m^3)
        {
            const std::vector<float> F = a.gas.download(1), M = a.gas.download(5);
            const double V = double(a.gas.h) * a.gas.h * a.gas.h;
            double mM = 0, mF = 0, top = 0;
            size_t at = 0;
            for (size_t c = 0; c < T.size(); ++c) {
                const double rho = a.gas.k.rhoT / T[c];
                mM += rho * M[c] * V;
                mF += rho * F[c] * V;
                if (rho * M[c] * 1e3 > top) {
                    top = rho * M[c] * 1e3;
                    at = c;
                }
            }
            const int ci = int(at % a.gas.nx), cj = int((at / a.gas.nx) % a.gas.ny), ck = int(at / (size_t(a.gas.nx) * a.gas.ny));
            std::printf("ates: duman %.2f g (en yogun %.2f g/m3: %.3f %.3f %.3f m, %.0f K, F %.3f), yanmamis ucucu %.2f g\n", mM * 1e3, top,
                        a.gas.origin.x + (ci + 0.5f) * a.gas.h, a.gas.origin.y + (cj + 0.5f) * a.gas.h, a.gas.origin.z + (ck + 0.5f) * a.gas.h, T[at], F[at],
                        mF * 1e3);
        }
    }
    if (views.empty() && freeCams.empty()) views = {0, 1, 2, 3};
    auto shoot = [&](const AtesCam& c, const std::string& name) {
        a.cam = c;
        const int warm = a.R.dlssOn() ? 3 * ATES_PHASES : 4;
        for (int k = 0; k < warm; ++k) {
            a.update(1.0f / 60.0f);
            a.render(1.0f / 60.0f);
        }
        const std::string path = (fs::path(root) / (out + "_" + name)).generic_string();
        {
            // the picture's luminances (cd/m^2, before the exposure): what the eye meets
            std::vector<float4> hd = a.R.hdrHost();
            std::vector<float> Y;
            Y.reserve(hd.size());
            for (const float4& c : hd) Y.push_back(0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z);
            std::sort(Y.begin(), Y.end());
            auto pc = [&](double q) { return Y[size_t(q * (Y.size() - 1))]; };
            std::printf("ates: parlaklik (cd/m2): %%50 %.3g, %%90 %.3g, %%99 %.3g, %%99.9 %.3g, en %.3g\n", pc(0.5), pc(0.9), pc(0.99), pc(0.999), Y.back());
        }
        if (a.cfg.i("debug.skipab", 0)) {
            // the march with and without its empty-space skip over the same frame: their times, the largest difference
            const std::vector<float4> A = a.R.hdrHost();
            const unsigned int* keep = a.R.params.gasBricks;
            auto timed = [&]() {
                const double q0 = nowMs();
                for (int r = 0; r < 10; ++r) a.R.redoVolume();
                return (nowMs() - q0) / 10.0;
            };
            const double msA = timed();
            a.R.params.gasBricks = nullptr;
            const double msB = timed();
            const std::vector<float4> B = a.R.hdrHost();
            a.R.params.gasBricks = keep;
            a.R.redoVolume();
            double worst = 0.0;
            size_t at = 0, differ = 0;
            for (size_t p = 0; p < A.size(); ++p) {
                const double dA = std::fabs(double(A[p].x) - B[p].x) + std::fabs(double(A[p].y) - B[p].y) + std::fabs(double(A[p].z) - B[p].z);
                const double ref = std::fabs(double(B[p].x)) + std::fabs(double(B[p].y)) + std::fabs(double(B[p].z));
                const double rel = dA / std::max(ref, 1e-6);
                if (dA > 0.0) ++differ;
                if (rel > worst) {
                    worst = rel;
                    at = p;
                }
            }
            std::printf("ates: atlama A/B: %.2f ms, atlamasiz %.2f ms; %zu piksel farkli, en buyuk goreli fark %.2e (piksel %zu %zu)\n", msA, msB, differ, worst,
                        at % size_t(a.width), at / size_t(a.width));
        }
        a.save(path);
        if (a.cfg.i("debug.exr", 0)) {
            const std::vector<float4> hd = a.R.hdrHost();
            writeEXR(path + ".exr", a.width, a.height, 4, reinterpret_cast<const float*>(hd.data()));
        }
        std::printf("ates: %s (yuzey %.2f ms, DLSS %.2f, ates %.2f ms, goruntu %.2f ms, gaz adimi %.2f ms, odun %.2f ms, pozlama x%.3g, sonlu olmayan %u)\n",
                    path.c_str(), a.R.renderMs, a.R.dlssMs, a.R.volumeMs, a.R.displayMs, a.gas.stats.stepMs, a.wood.stats.stepMs, a.autoExposure, a.R.badSamples);
        a.sparks.readStats();
        if (a.cfg.i("debug.sparks", 0)) {
            // where the live ones are: by height, on the ground or in the air, and how hot
            std::vector<float4> P, V;
            a.sparks.snapshot(P, V);
            const float edges[5] = {0.0f, 0.0035f, 0.3f, 1.44f, 100.0f};
            for (int b = 0; b < 4; ++b) {
                int n = 0;
                double T = 0.0, d = 0.0, vy = 0.0;
                for (size_t k = 0; k < P.size(); ++k)
                    if (P[k].y >= edges[b] && P[k].y < edges[b + 1]) {
                        ++n;
                        T += V[k].w;
                        d += P[k].w;
                        vy += V[k].y;
                    }
                if (n) std::printf("  kivilcim y %.3f-%.2f m: %d, ortalama %.0f K, %.2f mm, vy %.2f m/s\n", edges[b], edges[b + 1], n, T / n, d / n * 1e3, vy / n);
            }
        }
        std::printf("ates: kivilcim %u canli (simdiye dek %u; olen: katiya %u, yanip biten %u, sonen %u, yere %u, uzaga %u), adim + cizim %.2f ms\n",
                    a.sparks.stats.alive, a.sparks.stats.born, a.sparks.stats.deaths[0], a.sparks.stats.deaths[1], a.sparks.stats.deaths[2],
                    a.sparks.stats.deaths[3], a.sparks.stats.deaths[4], a.sparks.stats.ms);
        if (bench > 0) {
            // the frame's cost, each part's mean: the wall clock of the update and of the picture, the GPU's time of
            // their parts, the bodies' CPU time. --canli runs each frame with the last frame's own time, as the live
            // scene does (the gas's steps then follow the frame rate: 40 FPS takes three); otherwise frames of 1/60 s
            double sum = 0.0, worst = 0.0, upd = 0.0, ren = 0.0, sim0 = a.simTime;
            double gasMs = 0.0, woodMs = 0.0, surfMs = 0.0, dlssMs = 0.0, volMs = 0.0, dispMs = 0.0, bodyMs = 0.0;
            unsigned long long bad[5] = {0, 0, 0, 0, 0};
            int badFrames = 0;
            int steps = 0;
            float dt = 1.0f / 60.0f;
            for (int k = 0; k < bench; ++k) {
                const double s0 = nowMs();
                // (--yeniden n: the fire laid again every n frames, as R does live: every log's structure built anew)
                if (relay > 0 && k % relay == relay - 1) a.lay(a.layout);
                a.update(dt);
                cudaDeviceSynchronize();
                const double s1 = nowMs();
                a.render(dt);
                const double ms = nowMs() - s0;
                if (live) dt = std::min(float(ms) * 1e-3f, 0.1f);
                sum += ms;
                worst = std::max(worst, ms);
                upd += s1 - s0;
                ren += ms - (s1 - s0);
                steps += a.cost.steps;
                gasMs += a.cost.gasMs;
                woodMs += a.cost.woodMs;
                surfMs += a.R.renderMs - a.R.dlssMs;
                dlssMs += a.R.dlssMs;
                volMs += a.R.volumeMs;
                dispMs += a.R.displayMs;
                bodyMs += a.bodies.stats.ms;
                for (int q = 0; q < 5; ++q) bad[q] += a.R.badKinds[q];
                if (a.R.badSamples) ++badFrames;
            }
            const double n = bench;
            std::printf("ates: olcum %d kare%s (duvar saati): ortalama %.2f ms (%.1f FPS), en kotu %.2f ms; guncelleme %.2f, goruntu %.2f ms\n", bench,
                        live ? " canli" : "", sum / n, 1000.0 * n / sum, worst, upd / n, ren / n);
            std::printf("ates:   GPU: gaz %.2f ms (%.2f adim x %.2f), odun %.2f, yuzey %.2f, DLSS %.2f, ates %.2f, goruntu %.2f ms; cisimler (CPU) %.2f ms; gaz "
                        "gercek zamanin %%%.0f'i\n",
                        gasMs / n, steps / n, steps ? gasMs / steps : 0.0, woodMs / n, surfMs / n, dlssMs / n, volMs / n, dispMs / n, bodyMs / n,
                        100.0 * (a.simTime - sim0) / (sum * 1e-3));
            std::printf("ates:   sonlu olmayan (%d karede): yuzey rengi %llu, DLSS rehberleri %llu, ates %llu, kivilcimdan sonra %llu, DLSS ciktisi %llu\n",
                        badFrames, bad[0], bad[1], bad[2], bad[3], bad[4]);
        }
    };
    if (seq > 0) {
        // a clip's frames at its own rate (the fire's clock advances 1/fps a frame, as the eye would see it)
        const AtesCam c = !freeCams.empty() ? freeCams[0] : AtesApp::view(views.empty() ? 0 : views[0]);
        a.cam = c;
        for (int k = 0; k < 3 * ATES_PHASES; ++k) {
            a.update(1.0f / seqFps);
            a.render(1.0f / seqFps);
        }
        a.autoOn = false;
        const std::string dir = (fs::path(root) / out).generic_string();
        fs::create_directories(dir, ec);
        setPNGCompression(1);
        // (--dondur: the fire stands still, only the picture's samples move: what flickers then is the sampling's;
        // --donus: the look turns this many degrees a second)
        const float yaw0 = a.cam.yaw;
        for (int f = 0; f < seq; ++f) {
            if (!freeze) a.update(1.0f / seqFps);
            a.cam.yaw += turn * 0.0174533f / seqFps;
            a.render(1.0f / seqFps);
            char nm[64];
            std::snprintf(nm, sizeof(nm), "/kare_%05d", f);
            a.save(dir + nm);
        }
        if (freeze) {
            // the same still fire without DLSS, 16 sub-pixel samples, its light from 64 samples each, the textures as
            // sharp as DLSS takes them: the frames' reference
            const int keep = a.R.params.lightSamples;
            a.setDlss(false);
            a.R.setPattern(16, 0.42f);
            a.R.params.lodBias = -1.0f;
            a.R.params.lightSamples = 64;
            a.render(1.0f / seqFps);
            a.save(dir + "/ref");
            // turning, the whole path again, every frame drawn so (ref_*: tools/titreme.py's reference)
            if (turn != 0.0f) {
                a.cam.yaw = yaw0;
                for (int f = 0; f < seq; ++f) {
                    a.cam.yaw += turn * 0.0174533f / seqFps;
                    a.render(1.0f / seqFps);
                    char nm[64];
                    std::snprintf(nm, sizeof(nm), "/ref_%05d", f);
                    a.save(dir + nm);
                }
            }
            a.R.params.lightSamples = keep;
        }
        std::printf("ates: %d kare -> %s\n", seq, dir.c_str());
        a.release();
        return 0;
    }
    for (size_t k = 0; k < freeCams.size(); ++k) shoot(freeCams[k], "serbest" + std::to_string(k));
    for (int v : views) shoot(AtesApp::view(v), "bakis" + std::to_string(v));
    a.release();
    return 0;
}
