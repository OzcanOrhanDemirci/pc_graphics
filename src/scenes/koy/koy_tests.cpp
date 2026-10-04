// Scene 5 "Koy": physics tests against published numbers (docs/koy/PLAN.md §9). Each test renders or computes with the
// scene's own code (the same tables, the same programs) and compares with the physics; PASS/FAIL per test.
//   snell     Snell's window: looking straight up through a flat surface, the edge of the bright disc at 48.6 degrees
//   optik     the optics' self-checks: phase functions integrate to 1, the light field's Kd per wavelength
//   kostik    the caustic volume's mean is 1 at every depth (energy), its sharpness against the sun's disc
//   dalga     each surface's slope variance as booked by its spectrum against the field the FFT really makes
//   oksijen   Ada (PLAN_ADA §6): pressure and ppO2 at depth against the formula; at 70 m the dose builds, the view
//             narrows, goes black and the diver starts again at the ladder; at 50 m nothing happens
#include "scenes/koy/koy_app.h"
#include "scenes/koy/koy_data.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
int fails = 0;
void report(const char* name, bool ok, const char* fmt, double a = 0.0, double b = 0.0) {
    char msg[256];
    std::snprintf(msg, sizeof(msg), fmt, a, b);
    std::printf("[%s] %-10s %s\n", ok ? "GECTI" : "KALDI", name, msg);
    if (!ok) ++fails;
}

int testSnell(KoyApp& app) {
    // a flat sea (no waves), the camera 5 m deep looking straight up, a pinhole in water (no mask) with a wide view
    app.overrides.push_back("ocean.amplitude=0");
    app.overrides.push_back("camera.port=0");
    app.overrides.push_back("camera.hfov_air=150");
    app.loadConfig();
    app.release();
    app.setup();
    app.diver.pos = make_float3(0.0f, -5.0f, -30.0f);   // open water in the corridor (Ada: the pier's head stands over z ~ 0)
    app.diver.yaw = 0.0f;
    app.diver.pitch = 1.5707f;
    app.setCamera();
    app.stepWorld(0.0f);
    app.frame();
    app.adaptExposure(0.0f, true);
    app.frame();
    std::vector<float4> hdr;
    app.renderer.downloadHdr(hdr);
    const int W = int(app.width), H = int(app.height), y = H / 2;
    // the brightness along the middle row: the window's edge is where it falls fastest
    std::vector<float> Y(W);
    for (int x = 0; x < W; ++x) {
        const float4 c = hdr[size_t(y) * W + x];
        Y[x] = std::log(std::max(1e-9f, 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z));
    }
    int best = W / 2;
    float drop = 0.0f;
    for (int x = W / 2; x + 3 < W; ++x) {
        const float d = Y[x - 2] - Y[x + 2];
        if (d > drop) { drop = d; best = x; }
    }
    const KCamera& c = app.renderer.params().cam;
    const float u = (2.0f * (best + 0.5f) / W - 1.0f) * c.tanHalfX;
    const float angle = std::atan(u) * 57.29578f;
    const float expect = std::asin(1.0f / app.optics.waterN) * 57.29578f;
    report("snell", std::fabs(angle - expect) < 0.5f, "pencere kenari %.2f derece, beklenen %.2f (+-0.5)", angle, expect);
    app.save(app.root + "/captures/koy/test_snell");
    return 0;
}

int testOptics(KoyApp& app) {
    const KoyOptics& o = app.optics;
    // phase functions
    double sw = 0, sp = 0;
    const int N = 200000;
    for (int k = 0; k < N; ++k) {
        const double u = (k + 0.5) / N;
        const double th = 3.14159265358979 * u * u;
        const double dth = 3.14159265358979 * 2.0 * u / N;
        sw += KoyOptics::phaseWater(float(std::cos(th))) * std::sin(th) * 2.0 * 3.14159265358979 * dth;
        sp += o.phaseFF(float(std::cos(th))) * std::sin(th) * 2.0 * 3.14159265358979 * dth;
    }
    report("faz_su", std::fabs(sw - 1.0) < 0.01, "su faz integrali %.4f (1 +- 0.01)", sw);
    (void)sp;
    const double ip = o.phaseIntegral();
    report("faz_ff", std::fabs(ip - 1.0) < 0.01, "Fournier-Forand integrali %.4f (1 +- 0.01), kesilen pay %.4f", ip, o.ffTruncated);
    // backscatter fraction of the FF used
    double bb = 0, tot = 0;
    for (int k = 0; k < N; ++k) {
        const double th = 3.14159265358979 * (k + 0.5) / N;
        const double w = o.phaseFF(float(std::cos(th))) * std::sin(th);
        tot += w;
        if (th > 0.5 * 3.14159265358979) bb += w;
    }
    report("ff_geri", std::fabs(bb / tot - o.Bp / (1.0 - o.ffTruncated)) < 0.002, "kesik fazin geri sacilim payi %.4f, beklenen B/(1-kesilen) %.4f", bb / tot,
           o.Bp / (1.0 - o.ffTruncated));
    // the light field's downwelling irradiance with depth at 490 nm: direct beam and diffuse separately
    {
        const int li = int(std::round((490.0f - KoyOptics::L0) / KoyOptics::DL));
        const int NZ = AP_Z, NM = AP_MU;
        const float dz = AMB_ZMAX / float(NZ - 1);
        for (float z : {0.0f, 0.65f, 1.3f, 2.5f, 5.0f, 7.5f, 8.2f}) {
            const int j = std::min(NZ - 1, std::max(0, int(std::round(z / dz))));
            const float* L = o.field.data() + size_t(li) * NZ * NM + size_t(j) * NM;
            double ed = 0, eu = 0;
            for (int m = 0; m < NM; ++m) {
                const float mu = -1.0f + (m + 0.5f) * 2.0f / NM;
                const double v = L[m] * std::fabs(mu) * 2.0 * 3.14159265358979 * (2.0 / NM);
                if (mu < 0) ed += v; else eu += v;
            }
            const double dir = o.sunWaterPerp[li] * o.sunCosW * std::exp(-o.c[li] * j * dz / o.sunCosW);
            std::printf("         z %.2f m: Ed dogrudan %.4f + daginik %.4f = %.4f, Eu %.4f\n", j * dz, dir, ed, dir + ed, eu);
        }
    }
    // the radiance distribution at 5 m: horizontal and upward-looking radiance against Ed / pi (a white plate's radiance)
    for (float nm : {490.0f, 550.0f}) {
        const int li = int(std::round((nm - KoyOptics::L0) / KoyOptics::DL));
        const int NZ = AP_Z, NM = AP_MU;
        const float dz = AMB_ZMAX / float(NZ - 1);
        const int j = int(std::round(5.0f / dz));
        const float* L = o.field.data() + size_t(li) * NZ * NM + size_t(j) * NM;
        double ed = o.sunWaterPerp[li] * o.sunCosW * std::exp(-o.c[li] * j * dz / o.sunCosW);
        for (int m = 0; m < NM; ++m) {
            const float mu = -1.0f + (m + 0.5f) * 2.0f / NM;
            if (mu < 0) ed += L[m] * (-mu) * 2.0 * 3.14159265358979 * (2.0 / NM);
        }
        const double lh = 0.5 * (L[NM / 2 - 1] + L[NM / 2]), lup = L[0], ldown = L[NM - 1];
        std::printf("         %.0f nm, 5 m: Ed/pi %.4f; yatay isinim %.4f (oran %.3f), asagidan gelen (yukari giden) %.4f (oran %.3f), yukaridan gelen daginik %.4f\n", nm,
                    ed / 3.14159265, lh, lh / (ed / 3.14159265), ldown, ldown / (ed / 3.14159265), lup);
    }
    // Kd of the light field against Solonenko & Mobley's Kd for the same Jerlov type (PLAN §9 "derinlikte renk"). Their Kd
    // is for deep water: the same optics with the sand far below (no light coming back up from a bright floor)
    {
        Config deep = app.cfg;
        deep.set("light.bottom", "39");
        KoyOptics od;
        od.build(deep);
        const KoyOptics& o = od;
        const KoyJerlov* J = &kJerlov[0];
        for (const KoyJerlov& k : kJerlov)
            if (o.jerlov == k.name) J = &k;
        double worst = 0.0;
        for (int k = 0; k < J->n; ++k) {
            const float nm = J->nm[k];
            if (nm < 400.0f || nm > 600.0f) continue;
            const int li = int(std::round((nm - KoyOptics::L0) / KoyOptics::DL));
            const double kd = o.kdDown(li, 10.0f, 35.0f), kdNear = o.kdDown(li, 1.0f, 5.0f);
            const double rel = std::fabs(kd - J->kd[k]) / J->kd[k];
            worst = std::max(worst, rel);
            std::printf("         Kd(%.0f nm): isik alani 10-35 m %.4f (1-5 m %.4f), Solonenko-Mobley %s %.4f (%+.0f%%)\n", nm, kd, kdNear, J->name, J->kd[k],
                        100.0 * (kd - J->kd[k]) / J->kd[k]);
        }
        report("kd", worst < 0.15, "isik alaninin derindeki Kd'si (10-35 m, 400-600 nm) yayimlanan tipe en cok %%%.0f uzak (sinir %%15)", 100.0 * worst);
    }
    // Kd of the light field at a few wavelengths (printed; the comparison with the Jerlov type comes with the data)
    for (float nm : {450.0f, 490.0f, 530.0f, 570.0f, 600.0f}) {
        const int li = int(std::round((nm - KoyOptics::L0) / KoyOptics::DL));
        std::printf("         Kd(%.0f nm, 1-6 m) = %.4f 1/m, a %.4f, b %.4f, c %.4f\n", nm, o.kdDown(li, 1.0f, 6.0f), o.a[li], o.b[li], o.c[li]);
    }
    return 0;
}

int testCaustic(KoyApp& app) {
    app.ocean.beamStatsOn = app.cfg.i("test.beam_stats", 0);
    app.ocean.beamDebug = app.cfg.i("test.beam_debug", 0);
    app.stepWorld(app.cfg.f("test.t", 3.0f));
    const KoyOcean& oc = app.ocean;
    if (!oc.beamStats.empty())
        for (int k = 0; k < oc.cset.slices; ++k) {
            const unsigned long long* st = oc.beamStats.data() + 6 * k;
            const double nc = double(std::max(st[0], 1ull));
            std::printf("         huzme dilim %2d (%5.2f m): pencere %.2f, alan %.2f, cevre %.2f doku/hucre, bolunen %llu, ince %llu, %.3f ms\n", k,
                        oc.sliceDepth(k), double(st[1]) / nc, double(st[4]) / 1024.0 / nc, double(st[5]) / 1024.0 / nc, st[2], st[3],
                        k < int(oc.beamSliceMs.size()) ? oc.beamSliceMs[k] : 0.0f);
        }
    std::vector<float> sl;
    bool ok = true;
    for (int k = 0; k < oc.cset.slices; k += 4) {
        const_cast<KoyOcean&>(oc).readCaustic(k, sl, app.renderer.stream());
        double m = 0, mx = 0;
        for (float v : sl) {
            m += v;
            mx = std::max(mx, double(v));
        }
        m /= double(sl.size());
        std::printf("         dilim %2d (%.2f m): ortalama %.4f, en buyuk %.2f\n", k, oc.sliceDepth(k), m, mx);
        {   // the slice as an image: 1 (flat sun) at mid grey, 4x brighter at white
            const int N = oc.cset.N;
            std::vector<unsigned char> img(size_t(N) * N * 4);
            for (size_t i = 0; i < sl.size(); ++i) {
                const float v = std::min(1.0f, sl[i] / 4.0f);
                const unsigned char g = (unsigned char)(255.0f * std::sqrt(v));
                img[4 * i] = img[4 * i + 1] = img[4 * i + 2] = g;
                img[4 * i + 3] = 255;
            }
            char nm[128];
            std::snprintf(nm, sizeof(nm), "/captures/koy/kostik_dilim_%02d.png", k);
            writePNG(app.root + nm, N, N, img.data());
        }
        ok = ok && std::fabs(m - 1.0) < 0.02;
    }
    report("kostik", ok, "her dilimin ortalamasi 1 +- 0.02 (enerji korunur)");
    std::printf("         yuzey %.2f ms, kostik %.2f ms (%s %.2f, bulaniklik %.2f)\n", oc.msSurface, oc.msCaustic,
                oc.cset.method == 1 ? "huzme" : "isin", oc.msBeams, oc.msBlur);
    return 0;
}
}   // namespace

// the tables along their axes near the trial area's numbers (a jump marks a bad cell)
int testTables(KoyApp& app) {
    const KoyOptics& o = app.optics;
    auto t3 = [](const std::vector<float4>& v, int nx, int ny, int nz, float fx, float fy, float fz) {   // trilinear, texel units
        auto at = [&](int x, int y, int z) {
            x = std::min(std::max(x, 0), nx - 1);
            y = std::min(std::max(y, 0), ny - 1);
            z = std::min(std::max(z, 0), nz - 1);
            return v[(size_t(z) * ny + y) * nx + x].y;
        };
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy)), z0 = int(std::floor(fz));
        const float ax = fx - x0, ay = fy - y0, az = fz - z0;
        float r = 0.0f;
        for (int k = 0; k < 8; ++k) {
            const float w = ((k & 1) ? ax : 1 - ax) * ((k & 2) ? ay : 1 - ay) * ((k & 4) ? az : 1 - az);
            r += w * at(x0 + (k & 1), y0 + ((k >> 1) & 1), z0 + ((k >> 2) & 1));
        }
        return r;
    };
    auto ix = [](float x, int n) { return std::min(std::max(x, 0.0f), 1.0f) * float(n - 1); };
    auto it = [&](float t, float tmax, int n) { return ix(std::sqrt(std::max(t, 0.0f) / tmax), n); };
    std::printf("ambSurf[1].Y (yukari bakan, t 6 m) derinlige gore:\n  ");
    for (float z = 5.5f; z <= 8.6f; z += 0.25f) std::printf("%.2f:%.4f ", z, t3(o.ambSurf[1], AS_T, AS_N, AS_Z, it(6.0f, AMB_TMAX, AS_T), ix(1.0f, AS_N), ix(z / AMB_ZMAX, AS_Z)));
    std::printf("\nambSurf[1].Y (z 7 m) yol uzunluguna gore:\n  ");
    for (float t = 3.0f; t <= 12.0f; t += 0.5f) std::printf("%.1f:%.4f ", t, t3(o.ambSurf[1], AS_T, AS_N, AS_Z, it(t, AMB_TMAX, AS_T), ix(1.0f, AS_N), ix(7.0f / AMB_ZMAX, AS_Z)));
    std::printf("\nambPath.Y (z0 4.6, mu -0.4) yol uzunluguna gore:\n  ");
    for (float t = 3.0f; t <= 12.0f; t += 0.5f)
        std::printf("%.1f:%.4f ", t, t3(o.ambPath, AP_T, AP_MU, AP_Z, it(t, AMB_TMAX, AP_T), ix(-0.4f * 0.5f + 0.5f, AP_MU), ix(4.6f / AMB_ZMAX, AP_Z)));
    std::printf("\npathTab SURF1.Y yol uzunluguna gore:\n  ");
    for (float L = 8.0f; L <= 20.0f; L += 1.0f) {
        const float u = std::min(std::max(L / PT_LMAX, 0.0f), 1.0f) * (PT_W - 1);
        const int x0 = int(u);
        const float a = u - x0;
        std::printf("%.0f:%.4f ", L, o.pathTab[size_t(PT_SURF0 + 1) * PT_W + x0].y * (1 - a) + o.pathTab[size_t(PT_SURF0 + 1) * PT_W + x0 + 1].y * a);
    }
    std::printf("\n");
    return 0;
}

// The slope variance a surface's spectrum books (it sets the ripples' strength, and what of the statistical roughness
// they take over) against the field the FFT makes: the mean of (sx^2 + sz^2) / 2 over level 0, at two moments
int testWaves(KoyApp& app) {
    const bool wasOn = app.ripplesOn;
    app.setRipples(true);
    for (int s = 0; s < 2; ++s) {
        KoyOcean& o = s == 0 ? app.ocean : app.ripples;
        if (!o.surfaceTexture()) continue;
        for (float t : {0.0f, 7.3f}) {
            app.stepWorld(t);
            std::vector<float4> f;
            o.readHeights(f, app.renderer.stream());
            double ms = 0.0, h2 = 0.0;
            for (const float4& v : f) {
                ms += v.w;
                h2 += double(v.x) * v.x;
            }
            ms /= double(f.size());
            h2 /= double(f.size());
            const double booked = o.slopeVarianceResolved(), ratio = ms / std::max(booked, 1e-12);
            char name[32];
            std::snprintf(name, sizeof(name), "%s t%.0f", o.oset.name, t);
            std::printf("  %s: egim varyansi defterde %.6f, alanda %.6f; yukseklik std %.4f m\n", name, booked, ms, std::sqrt(h2));
            report(name, std::fabs(ratio - 1.0) < 0.1, "alan / defter %.3f (1 +- 0.1)", ratio);
        }
    }
    app.setRipples(wasOn);
    return 0;
}

int testOxygen(KoyApp& app) {
    // the formula: P = 1.013 + 0.1005 d, ppO2 = 0.2095 P; 1.6 bar at ~65.9 m
    app.diver.pos = make_float3(0.0f, -65.9f, -200.0f);
    report("ppo2", std::fabs(app.ppO2() - 1.6f) < 0.01f, "65,9 m'de ppO2 %.3f bar (1,6 +- 0,01)", app.ppO2());
    const float dt = 1.0f / 41.0f;
    // 50 m for 20 s: below the limit, no dose, no tunnel
    app.restartDiver();
    app.diver.pos = make_float3(0.0f, -50.0f, -175.0f);
    for (int k = 0; k < int(20.0f / dt); ++k) app.stepBody(dt);
    report("50 m", app.o2Dose == 0.0f && app.renderer.params().tunnel == 0.0f && app.restarts == 0, "doz %.3f, tunel %.3f (0 olmali)",
           app.o2Dose, app.renderer.params().tunnel);
    // 70 m: the dose builds (ppO2 ~1.69: ~5 s), the tunnel closes, black, the restart at the ladder
    app.restartDiver();
    const int r0 = app.restarts;
    float tTunnel = -1.0f, tBlack = -1.0f, tRestart = -1.0f, maxTunnel = 0.0f;
    bool saved = false;
    for (int k = 0; k < int(16.0f / dt); ++k) {
        if (app.restarts == r0) app.diver.pos = make_float3(0.0f, -70.0f, -215.0f);   // held there until the restart
        app.stepBody(dt);
        const float t = k * dt;
        const KoyParams& p = app.renderer.params();
        maxTunnel = std::max(maxTunnel, p.tunnel);
        if (tTunnel < 0.0f && p.tunnel > 0.3f) tTunnel = t;
        if (tBlack < 0.0f && p.blackout > 0.99f) tBlack = t;
        if (tRestart < 0.0f && app.restarts > r0) tRestart = t;
        if (!saved && p.tunnel > 0.6f && p.blackout < 0.05f) {   // a frame with the view narrowed
            app.setCamera();
            app.stepWorld(0.0f);
            app.frame();
            app.adaptExposure(0.0f, true);
            app.frame();
            app.save(app.root + "/captures/koy/test_oksijen");
            saved = true;
        }
    }
    const float3 st = app.cfg.v3("diver.start", make_float3(0, 0, 0));
    const bool home = length(app.diver.pos - st) < 0.01f;
    std::printf("  70 m: tunel %.2f s'de, kararma %.2f s'de, yeniden baslama %.2f s'de; en dar tunel %.2f\n", tTunnel, tBlack, tRestart, maxTunnel);
    report("70 m", tTunnel > 0.0f && tBlack > tTunnel && tRestart > tBlack && home && !app.blackedOut(),
           "tunel -> kararma -> merdiven (%.1f s'de yeniden basladi)", tRestart);
    return 0;
}

int runKoyTests(KoyApp& app, const std::string& which) {
    fails = 0;
    const bool all = which.empty();
    if (all || which == "optik") testOptics(app);
    if (which == "tablo") testTables(app);
    if (all || which == "kostik") testCaustic(app);
    if (all || which == "dalga") testWaves(app);
    if (all || which == "oksijen") testOxygen(app);
    // last: it flattens the sea and sets the app up again with that (a wave test after it measured a flat sea)
    if (all || which == "snell") testSnell(app);
    std::printf("koy testleri: %d kalan\n", fails);
    return fails ? 1 : 0;
}
