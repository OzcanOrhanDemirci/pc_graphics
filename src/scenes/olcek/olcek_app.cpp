// Scene 7 "Ölçek": the run's state and the command-line tools.
//
//   pc_graphics.exe --scene olcek --headless --s -3,0,3 [--t 2] [--w 1280 --h 720] [--out captures/olcek]
//       frames at those decades (s = log10 of the screen's width in metres), after t seconds of time at each
//   pc_graphics.exe --scene olcek --headless --decades [...]       every whole decade, the bottom and the top
//   pc_graphics.exe --scene olcek --test olcek-hassasiyet          the anchors' places on the screen, floats against
//                                                                  double-doubles, every 0.02 decades (pixels)
//   pc_graphics.exe --scene olcek --test olcek-sure [--step 0.01]  top to bottom and back: the frame's time, the home
//                                                                  in the frame, and every jump (something appearing
//                                                                  or vanishing that the zoom does not explain)
//   pc_graphics.exe --scene olcek --test olcek-zaman               the clocks: home always at today's noon, however
//                                                                  long out there and however fast back
#include "scenes/olcek/olcek_app.h"
#include "core/image_io.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>

using namespace olcek;

namespace {
double nowS() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}   // namespace

void OlcekApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/olcek.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    samples = cfg.i("samples", 4);
    sc.configure(cfg);
}

void OlcekApp::init() {
    sc.init();
    sc.s = cfg.f("start", 0.0f);
    const size_t n = size_t(sc.width) * sc.height;
    cudaMalloc(reinterpret_cast<void**>(&accum), n * sizeof(float4));
    cudaMalloc(reinterpret_cast<void**>(&depth), n * sizeof(float));
    cudaMalloc(reinterpret_cast<void**>(&output), n * sizeof(uchar4));
    splatCap = 1 << 18;
    cudaMalloc(reinterpret_cast<void**>(&dSplats), size_t(splatCap) * sizeof(GSplat));
}

void OlcekApp::release() {
    cudaFree(accum);
    cudaFree(depth);
    cudaFree(output);
    cudaFree(dSplats);
    accum = nullptr;
    depth = nullptr;
    output = nullptr;
    dSplats = nullptr;
}

void OlcekApp::render() {
    const double h0 = nowS();
    sc.place();
    sc.camera();
    sc.build(view, splats);
    view.filterRadius = cfg.f("filter", 1.5f);
    if (int(splats.size()) > splatCap) {
        cudaFree(dSplats);
        splatCap = int(splats.size()) * 2;
        cudaMalloc(reinterpret_cast<void**>(&dSplats), size_t(splatCap) * sizeof(GSplat));
    }
    hostMs = (nowS() - h0) * 1000.0;
    cudaEvent_t a, b;
    cudaEventCreate(&a);
    cudaEventCreate(&b);
    cudaEventRecord(a);
    setView(view);
    if (!splats.empty()) cudaMemcpyAsync(dSplats, splats.data(), splats.size() * sizeof(GSplat), cudaMemcpyHostToDevice, 0);
    renderFrame(accum, depth, sc.width, sc.height, samples, 0);
    splatPoints(dSplats, int(splats.size()), accum, depth, sc.width, sc.height, 0);
    resolve(accum, output, sc.width, sc.height, view.saturation, 0);
    cudaEventRecord(b);
    cudaEventSynchronize(b);
    cudaEventElapsedTime(&frameMs, a, b);
    cudaEventDestroy(a);
    cudaEventDestroy(b);
}

void OlcekApp::save(const std::string& path) {
    std::vector<unsigned char> px(size_t(sc.width) * sc.height * 4);
    cudaMemcpy(px.data(), output, px.size(), cudaMemcpyDeviceToHost);
    writePNG(path + ".png", sc.width, sc.height, px.data());
}

void OlcekApp::luminanceQuarter(std::vector<float>& lum) {
    static float* dq = nullptr;
    static size_t cap = 0;
    const size_t n = size_t(sc.width / 4) * (sc.height / 4);
    if (cap < n) {
        if (dq) cudaFree(dq);
        cudaMalloc(reinterpret_cast<void**>(&dq), n * sizeof(float));
        cap = n;
    }
    quarterLuma(output, dq, sc.width, sc.height, 0);
    lum.resize(n);
    cudaMemcpy(lum.data(), dq, n * sizeof(float), cudaMemcpyDeviceToHost);
}

// ---------------------------------------------------------------------------------------------------------------------

namespace {

double smoothstepD(double a, double b, double x) {
    x = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

std::vector<double> doubles(const std::string& s) {
    std::vector<double> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) out.push_back(std::stod(part));
    return out;
}

// --test olcek-hassasiyet
int testPrecision(OlcekApp& app) {
    Scale& sc = app.sc;
    const int nodes[] = {kLaniakea, kLocalGroup, kGalCentre, kSun, kEarth, kBeach, kNucleus, kProton, kQuark, kMoon, kM31, kLMC};
    const char* names[] = {"laniakea", "yerel grup", "galaksi merkezi", "gunes", "dunya", "sahil", "cekirdek", "proton", "kuark", "ay", "M31", "LMC"};
    double worst = 0.0, worstS = 0.0;
    int worstNode = -1, checks = 0;
    std::printf("hassasiyet: her 0,02 onlukta gorunur capalarin ekrandaki yeri, float (cizimin yolu) ile double-double karsilastirmasi\n");
    std::printf("  onluk   en kotu (piksel)  hangisi\n");
    for (int dec = int(std::floor(sc.sMin)); dec <= int(std::ceil(sc.sMax)); ++dec) {
        double dw = 0.0;
        int dn = -1;
        for (double s = dec; s < dec + 1.0; s += 0.02) {
            if (s < sc.sMin || s > sc.sMax) continue;
            sc.s = s;
            sc.place();
            sc.camera();
            for (int i = 0; i < int(sizeof(nodes) / sizeof(nodes[0])); ++i) {
                // the anchor itself and a point a third of the screen beside it: on the screen, and near enough to be
                // more than a point at infinity (beyond 10^6 D only its direction counts, and that holds anyway)
                for (int side = 0; side < 2; ++side) {
                    const V3 local = side ? sc.cam.right * (0.3 * sc.cam.W) + sc.cam.up * (0.1 * sc.cam.W) : V3{};
                    const V3 p = sc.onScreen(nodes[i], local);
                    if (p.z <= 0.0 || std::fabs(p.x) > 0.6 || std::fabs(p.y) > 0.35) continue;
                    const V3 rel = sc.tree.rel(sc.cam.ref, nodes[i]) + local - sc.cam.pos;
                    if (length(rel) > 1e6 * sc.cam.D) continue;
                    const double e = sc.precisionError(nodes[i], local);
                    ++checks;
                    if (e > dw) { dw = e; dn = i; }
                    if (e > worst) { worst = e; worstS = s; worstNode = i; }
                }
            }
        }
        std::printf("  %4d    %.2e          %s\n", dec, dw, dn >= 0 ? names[dn] : "-");
    }
    std::printf("hassasiyet: %d olcum, en kotu %.3e piksel (s = %.2f, %s); esik 0,01 piksel: %s\n", checks, worst, worstS,
                worstNode >= 0 ? names[worstNode] : "-", worst < 0.01 ? "GECTI" : "KALDI");
    return worst < 0.01 ? 0 : 1;
}

// --test olcek-sure
int testContinuity(OlcekApp& app, double step) {
    Scale& sc = app.sc;
    const int w4 = sc.width / 4, h4 = sc.height / 4;
    std::vector<float> prev, cur;
    double prevW = 0.0;
    V3 prevF;
    int prevRef = -1;
    struct Flag { double s, v; int bx, by; };
    std::vector<Flag> flags;
    std::vector<double> msByDecade(80, 0.0), maxMsByDecade(80, 0.0), hostByDecade(80, 0.0);
    std::vector<int> nByDecade(80, 0);
    double worstHome = 0.0, worstHomeS = 0.0;
    int frames = 0;
    const double threshold = app.cfg.f("test.jump", 0.04f);
    std::printf("sure: ust uctan alta ve geri, %.3f onluk adimla, zaman donuk (yalniz yakinlasma)\n", step);
    std::vector<double> path;
    for (double s = sc.sMax; s > sc.sMin; s -= step) path.push_back(s);
    path.push_back(sc.sMin);
    for (double s = sc.sMin + step; s < sc.sMax; s += step) path.push_back(s);
    const double t0 = nowS();
    for (size_t f = 0; f < path.size(); ++f) {
        sc.s = path[f];
        app.render();
        app.luminanceQuarter(cur);
        const int dec = std::clamp(int(std::floor(sc.s)) + 40, 0, 79);
        msByDecade[dec] += app.frameMs;
        maxMsByDecade[dec] = std::max(maxMsByDecade[dec], double(app.frameMs));
        hostByDecade[dec] += app.hostMs;
        ++nByDecade[dec];
        // the home in the frame: the beach point (and, inside it, what the way in follows)
        {
            // what the way in follows at this depth (the centre's anchors, PLAN §3)
            const int home = sc.s < -14.9 ? kQuark : sc.s < -14.2 ? kProton : sc.s < -10.0 ? kNucleus : kBeach;
            const V3 p = sc.onScreen(home);
            const double m = std::max(std::fabs(p.x) / 0.5, std::fabs(p.y) / (0.5 * sc.height / sc.width));
            if (p.z <= 0.0 || m > worstHome) { worstHome = p.z <= 0.0 ? 99.0 : m; worstHomeS = sc.s; }
        }
        const V3 F = sc.focus();
        if (!prev.empty()) {
            // where frame f's pixels were in frame f-1: the focus plane scales by W and slides by the focus's move
            const V3 dF = sc.tree.rel(prevRef, sc.cam.ref) + F - prevF;
            const double dr = dot(dF, sc.cam.right), du = dot(dF, sc.cam.up);
            const double W1 = sc.cam.W;
            std::vector<float> diff(size_t(w4) * h4, 0.0f);
            for (int y = 0; y < h4; ++y)
                for (int x = 0; x < w4; ++x) {
                    const double u = (x + 0.5) / w4 - 0.5, v = (0.5 - (y + 0.5) / h4) * double(h4) / w4;
                    const double ui = (u * W1 + dr) / prevW, vi = (v * W1 + du) / prevW;
                    const double xi = (ui + 0.5) * w4 - 0.5, yi = (0.5 - vi * double(w4) / h4) * h4 - 0.5;
                    // the smallest difference within 2 pixels (motion the warp does not model: parallax, stars at
                    // infinity), so only something appearing or vanishing counts
                    float best = 1e9f;
                    bool any = false;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int xs = int(std::lround(xi)) + dx, ys = int(std::lround(yi)) + dy;
                            if (xs < 0 || ys < 0 || xs >= w4 || ys >= h4) continue;
                            any = true;
                            best = std::min(best, std::fabs(cur[size_t(y) * w4 + x] - prev[size_t(ys) * w4 + xs]));
                        }
                    diff[size_t(y) * w4 + x] = any ? best : 0.0f;
                }
            // 8x8 blocks
            float worst = 0.0f;
            int wbx = 0, wby = 0;
            // the outer ring of blocks is left out: what the zoom brings in at the border is new by design, and there the
            // warp's error from depth (parallax about the focus plane) is largest
            for (int by = 1; by < h4 / 8 - 1; ++by)
                for (int bx = 1; bx < w4 / 8 - 1; ++bx) {
                    float m = 0.0f;
                    for (int j = 0; j < 8; ++j)
                        for (int i = 0; i < 8; ++i) m += diff[size_t(by * 8 + j) * w4 + bx * 8 + i];
                    m /= 64.0f;
                    if (m > worst) { worst = m; wbx = bx; wby = by; }
                }
            if (worst > threshold) flags.push_back({sc.s, worst, wbx, wby});
        }
        prev.swap(cur);
        prevW = sc.cam.W;
        prevF = F;
        prevRef = sc.cam.ref;
        ++frames;
        if (frames % 1000 == 0) std::printf("  %d / %zu kare (s = %.2f)\n", frames, path.size(), sc.s);
    }
    std::printf("sure: %d kare, %.0f sn\n", frames, nowS() - t0);
    std::printf("  onluk  kare ort / en cok (ms)  ana islemci (ms)\n");
    double worstMs = 0.0;
    for (int d = 0; d < 80; ++d) {
        if (!nByDecade[d]) continue;
        std::printf("  %4d   %6.2f / %6.2f          %5.2f\n", d - 40, msByDecade[d] / nByDecade[d], maxMsByDecade[d], hostByDecade[d] / nByDecade[d]);
        worstMs = std::max(worstMs, maxMsByDecade[d]);
    }
    std::printf("  ev kadrajda: en kotu %.2f (1 = kenar; s = %.2f)\n", worstHome, worstHomeS);
    std::printf("  sicrama (esik %.3f): %zu kare\n", threshold, flags.size());
    std::sort(flags.begin(), flags.end(), [](const Flag& a, const Flag& b) { return a.v > b.v; });
    for (size_t i = 0; i < std::min<size_t>(flags.size(), 40); ++i)
        std::printf("    s = %7.3f  %.3f  blok (%d, %d)\n", flags[i].s, flags[i].v, flags[i].bx, flags[i].by);
    std::printf("sure: en uzun kare %.2f ms (butce 16): %s; sicrama %s\n", worstMs, worstMs <= 16.0 ? "GECTI" : "KALDI", flags.empty() ? "YOK" : "VAR");
    return 0;
}

// --test olcek-zaman: out, a wait, back in at a speed; the home must be at today's noon on arrival
int testTime(OlcekApp& app) {
    Scale& sc = app.sc;
    const double outs[] = {7.4, 8.0, 9.5, 12.5, 14.0, 21.0, 27.0};
    const double waits[] = {0.0, 0.37, 1.3, 5.1, 31.0};
    const double speeds[] = {0.5, 2.0, 8.0, 40.0};
    const double dt = 1.0 / 60.0;
    double worstSpin = 0.0, worstYear = 0.0, worstBeach = 0.0, worstRatio = 0.0;
    double ratioBySpeed[4] = {}, extraBySpeed[4] = {};
    int runs = 0;
    std::printf("zaman: R(0-) = %.6f, R(0+) = %.6f (insan olceginde 1 sn = 1 sn); R(-34,79) = %.3e, Planck zamani %.3e\n",
                Scale::rate(-1e-12), Scale::rate(1e-12), Scale::rate(sc.sMin), 5.391247e-44);
    for (double out : outs)
        for (double wait : waits)
            for (int vi = 0; vi < 4; ++vi) {
                const double v = speeds[vi];
                double extra = 0.0;   // radians turned beyond the Earth's own turning, on the way in
                sc.init();
                sc.s = 0.0;
                double sPrev = 0.0;
                // out at 2 decades a second
                while (sc.s < out) {
                    sPrev = sc.s;
                    sc.s = std::min(out, sc.s + 2.0 * dt);
                    sc.advance(dt, sPrev);
                }
                for (double t = 0.0; t < wait; t += dt) sc.advance(dt, sc.s);
                // in at v decades a second; how fast the Earth turned against its own pace in the zone where it is drawn
                // back (the turn of the frame minus what the clock alone gave)
                while (sc.s > 0.0) {
                    sPrev = sc.s;
                    const double spin0 = sc.clk.spin;
                    sc.s = std::max(0.0, sc.s - v * dt);
                    sc.advance(dt, sPrev);
                    if (sc.s > 6.6 && sc.s < 7.8) {
                        double d = sc.clk.spin - spin0;
                        d = std::remainder(d, 2.0 * 3.14159265358979);
                        const double own = 7.292115146706979e-5 * Scale::rate(sc.s) * dt;
                        worstRatio = std::max(worstRatio, std::fabs(d) / std::max(own, 1e-30));
                        ratioBySpeed[vi] = std::max(ratioBySpeed[vi], std::fabs(d) / std::max(own, 1e-30));
                        extra += std::fabs(d - own * smoothstepD(6.6, 7.8, sc.s));
                    }
                    if (sc.s <= 6.6) worstSpin = std::max(worstSpin, std::fabs(sc.clk.spin));
                    if (sc.s <= 11.9) worstYear = std::max(worstYear, std::fabs(sc.clk.year));
                }
                sc.place();
                worstBeach = std::max(worstBeach, length(sc.tree.rel(kBeachToday, kBeach)));
                extraBySpeed[vi] = std::max(extraBySpeed[vi], extra);
                ++runs;
            }
    std::printf("zaman: %d yolculuk (disari %zu yere, %zu bekleme, %zu donus hizi)\n", runs, sizeof(outs) / sizeof(outs[0]),
                sizeof(waits) / sizeof(waits[0]), sizeof(speeds) / sizeof(speeds[0]));
    std::printf("  10^6,6 altinda Dunya'nin donusu bugunden en cok %.3e rad, 10^11,9 altinda yorungesi %.3e sn\n", worstSpin, worstYear);
    std::printf("  sahile varista sahildeki nokta bugunku yerinden %.3e m\n", worstBeach);
    std::printf("  geri cekilirken Dunya kendi hizinin en cok %.1f kati dondu (gecis bolgesi 10^6,6-10^7,8)\n", worstRatio);
    for (int vi = 0; vi < 4; ++vi)
        std::printf("    saniyede %4.1f onlukla iniste: en cok %.1f kat, kendi donusunden fazla en cok %.2f rad\n", speeds[vi], ratioBySpeed[vi],
                    extraBySpeed[vi]);
    const bool ok = worstSpin < 1e-9 && worstYear < 1e-3 && worstBeach < 1e-3;
    std::printf("zaman: %s\n", ok ? "GECTI" : "KALDI");
    return ok ? 0 : 1;
}

}   // namespace

int runOlcekCli(int argc, char** argv) {
    OlcekApp app;
    std::vector<double> ss;
    std::string out = "captures/olcek", test;
    double t = 0.0, step = 0.01;
    bool decades = false;
    int w = 0, h = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--s") ss = doubles(next());
        else if (a == "--t") t = std::stod(next());
        else if (a == "--out") out = next();
        else if (a == "--w") w = std::stoi(next());
        else if (a == "--h") h = std::stoi(next());
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else if (a == "--test") test = next();
        else if (a == "--step") step = std::stod(next());
        else if (a == "--decades") decades = true;
    }
    if (w > 0) app.overrides.push_back("width=" + std::to_string(w));
    if (h > 0) app.overrides.push_back("height=" + std::to_string(h));
    app.loadConfig();
    app.init();
    int rc = 0;
    if (test == "olcek-hassasiyet") rc = testPrecision(app);
    else if (test == "olcek-sure") rc = testContinuity(app, step);
    else if (test == "olcek-zaman") rc = testTime(app);
    else {
        if (decades) {
            ss.clear();
            ss.push_back(app.sc.sMin);
            for (int d = int(std::ceil(app.sc.sMin)); d <= int(std::floor(app.sc.sMax)); ++d) ss.push_back(d);
            ss.push_back(app.sc.sMax);
        }
        if (ss.empty()) ss = {0.0};
        std::filesystem::create_directories(std::filesystem::path(app.root) / out);
        for (double s : ss) {
            app.sc.init();
            app.sc.s = std::clamp(s, app.sc.sMin, app.sc.sMax);
            for (double tt = 0.0; tt < t; tt += 1.0 / 60.0) app.sc.advance(1.0 / 60.0, app.sc.s);
            app.render();
            if (std::getenv("OLCEK_DEBUG")) {
                const OlcekView& v = app.view;
                std::printf("  ref %d, D %.6g, balls %d, blobs %d, ground %d (o %.4g %.4g %.4g, metres %.6g, beach %.3f)\n", app.sc.cam.ref,
                            app.sc.cam.D, v.nBalls, v.nBlobs, v.ground.on, v.ground.o.x, v.ground.o.y, v.ground.o.z, v.ground.metres, v.ground.beach);
                for (int i = 0; i < v.nBalls; ++i)
                    std::printf("    ball %d: c %.6g %.6g %.6g r %.6g h %.6g pattern %d weight %.3f\n", i, v.balls[i].c.x, v.balls[i].c.y, v.balls[i].c.z,
                                v.balls[i].r, v.balls[i].h, v.balls[i].pattern, v.balls[i].weight);
                for (int i = 0; i < std::min(v.nBlobs, 6); ++i)
                    std::printf("    blob %d: c %.5g %.5g %.5g sigma %.5g emit %.4g\n", i, v.blobs[i].c.x, v.blobs[i].c.y, v.blobs[i].c.z, v.blobs[i].sigma,
                                v.blobs[i].emit.x);
                std::printf("    splats %zu, ground fade %.3f atoms %.3f surface %.3f\n", app.splats.size(), v.ground.fade, v.ground.atoms, v.ground.surface);
            }
            char name[64];
            std::snprintf(name, sizeof(name), "s%+07.3f", app.sc.s);
            app.save(app.root + "/" + out + "/" + name);
            std::printf("s = %+7.2f  %-28s  %-24s  kare %5.2f ms (ana %5.2f), %zu nokta\n", app.sc.s, app.sc.lookingAt().c_str(),
                        app.sc.widthText().c_str(), app.frameMs, app.hostMs, app.splats.size());
        }
    }
    app.release();
    return rc;
}
