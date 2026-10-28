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
    double ms = 0.0, injected = 0.0, burnt = 0.0;
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
        if (s % 120 == 0) {
            std::printf("  t %.2f s: HRR %.1f kW, isinim %.1f kW (%%%.0f), en sicak %.0f K, artik %.1e, adim %.2f ms\n", t, g.stats.hrr / 1000.0f,
                        g.stats.rad / 1000.0f, 100.0f * g.stats.rad / std::max(g.stats.hrr, 1.0f), g.stats.maxT, g.stats.residual, g.stats.stepMs);
            if (cfg.i("test.budget", 0)) {
                // the fuel's books: what is in the grid (as volatiles, soot, smoke) against what came in and burnt
                const std::vector<float> T = g.download(0), Fu = g.download(1), So = g.download(4), Sm = g.download(5);
                double inGrid = 0.0;
                for (size_t c = 0; c < T.size(); ++c) inGrid += (g.k.rhoT / T[c]) * (Fu[c] + So[c] + Sm[c]) * h * h * h;
                std::printf("    yakit: girdi %.3f g, izgarada %.3f g, yanan %.3f g (HRR'den)\n", injected * 1e3, inGrid * 1e3, burnt * 1e3);
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

}   // namespace

int runAtesCli(int argc, char** argv) {
    std::string root = PROJECT_ROOT, out;
    std::vector<std::string> overrides;
    std::vector<int> views;
    std::vector<AtesCam> freeCams;
    int gas = 0, bench = 0, seq = 0;
    float settle = 4.0f, seqFps = 30.0f;
    bool blow = false;
    std::vector<std::pair<float, int>> throws;
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
        else if (s == "--seri") seq = std::stoi(next());
        else if (s == "--fps") seqFps = std::stof(next());
        else if (s == "--ufle") blow = true;
        else if (s == "--odun") {
            const float at = std::stof(next());
            throws.push_back({at, std::stoi(next())});
        }
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
    // the fire's first seconds (the gas fills, the flames stand up): simulated at the gas's own step
    const double t0 = nowMs();
    const float dtGas = a.cfg.f("gas.dt", 1.0f / 120.0f);
    a.cam = AtesApp::view(views.empty() ? 0 : views[0]);
    for (float t = 0.0f; t < settle; t += dtGas) {
        for (const auto& th : throws)
            if (th.first >= t && th.first < t + dtGas) a.addFuel(th.second);
        a.update(dtGas);
    }
    std::printf("ates: %.1f s ates %.0f ms'de (HRR %.1f kW, piroliz %.2f g/s, kor %.0f cm2, en sicak yuzey %.0f K)\n", settle, nowMs() - t0, a.gas.stats.hrr / 1000.0f,
                a.wood.stats.pyrolysis * 1e3f, a.wood.stats.emberArea * 1e4f, a.wood.stats.hottest);
    std::printf("ates: odun %.3f kg, kor yanmasi %.3f g/s, gaz yakit girdisi %.3f g/s, isinim %.1f kW, en sicak gaz %.0f K\n", a.wood.stats.mass,
                a.wood.stats.charBurn * 1e3f, a.gas.stats.fuelIn * 1e3f, a.gas.stats.rad / 1000.0f, a.gas.stats.maxT);
    if (a.cfg.i("debug.wood", 0)) {
        // each log's surface: mean char depth, ash, surface temperature
        std::vector<float> cd(a.wood.patches), as(a.wood.patches), st(a.wood.patches);
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
        }
    }
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
    }
    if (views.empty() && freeCams.empty()) views = {0, 1, 2, 3};
    auto shoot = [&](const AtesCam& c, const std::string& name) {
        a.cam = c;
        a.blowing = blow;
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
        a.save(path);
        if (a.cfg.i("debug.exr", 0)) {
            const std::vector<float4> hd = a.R.hdrHost();
            writeEXR(path + ".exr", a.width, a.height, 4, reinterpret_cast<const float*>(hd.data()));
        }
        std::printf("ates: %s (yuzey %.2f ms, DLSS %.2f, ates %.2f ms, goruntu %.2f ms, gaz adimi %.2f ms, odun %.2f ms, pozlama x%.3g, sonlu olmayan %u)\n",
                    path.c_str(), a.R.renderMs, a.R.dlssMs, a.R.volumeMs, a.R.displayMs, a.gas.stats.stepMs, a.wood.stats.stepMs, a.autoExposure, a.R.badSamples);
        if (bench > 0) {
            double sum = 0.0, worst = 0.0;
            for (int k = 0; k < bench; ++k) {
                const double s0 = nowMs();
                a.update(1.0f / 60.0f);
                a.render(1.0f / 60.0f);
                const double ms = nowMs() - s0;
                sum += ms;
                worst = std::max(worst, ms);
            }
            std::printf("ates: olcum %d kare (gaz + odun + goruntu, duvar saati): ortalama %.2f ms, en kotu %.2f ms\n", bench, sum / bench, worst);
        }
    };
    if (seq > 0) {
        // a clip's frames at its own rate (the fire's clock advances 1/fps a frame, as the eye would see it)
        const AtesCam c = !freeCams.empty() ? freeCams[0] : AtesApp::view(views.empty() ? 0 : views[0]);
        a.cam = c;
        a.blowing = blow;
        for (int k = 0; k < 3 * ATES_PHASES; ++k) {
            a.update(1.0f / seqFps);
            a.render(1.0f / seqFps);
        }
        a.autoOn = false;
        const std::string dir = (fs::path(root) / out).generic_string();
        fs::create_directories(dir, ec);
        setPNGCompression(1);
        for (int f = 0; f < seq; ++f) {
            a.update(1.0f / seqFps);
            a.render(1.0f / seqFps);
            char nm[64];
            std::snprintf(nm, sizeof(nm), "/kare_%05d", f);
            a.save(dir + nm);
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
