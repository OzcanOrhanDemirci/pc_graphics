// Scene 7 "Ölçek", second option "Hız": the physics tests and the headless frames (olcek_app.cpp's command line).
//
//   pc_graphics.exe --scene olcek --test olcek-hiz        the formulas, the journeys, the colours, the save file
//   pc_graphics.exe --scene olcek --headless --hiz --phi 0,1,3,6 [--ayak 0] [--yer 0.5] [--gorus 75] [--bakis yaw,pitch]
//                   [--yan 1.3,7,12] [--out captures/olcek_hiz]
//       the rocket's eye at those rapidities (or the side view at those scales), half way along the leg (--yer: the
//       fraction of the leg behind it), at rest when phi = 0
#include "scenes/olcek/olcek_app.h"
#include "scenes/olcek/olcek_bugun.h"
#include "scenes/olcek/olcek_hiz_pano.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace olcek;
namespace hz = olcek::hiz;

namespace {

constexpr double kPi = 3.14159265358979323846;

// ---- double-double arithmetic for the long-precision reference of 1 - beta (~32 digits)
hz::DD ddMul(hz::DD a, hz::DD b) {
    const double p = a.hi * b.hi;
    double e = std::fma(a.hi, b.hi, -p);
    e += a.hi * b.lo + a.lo * b.hi;
    return hz::quickTwoSum(p, e);
}
hz::DD ddMul(hz::DD a, double b) { return ddMul(a, hz::dd(b)); }
hz::DD ddDiv(hz::DD a, hz::DD b) {
    const double q1 = a.hi / b.hi;
    hz::DD r = hz::add(a, hz::neg(ddMul(b, q1)));
    const double q2 = r.hi / b.hi;
    r = hz::add(r, hz::neg(ddMul(b, q2)));
    const double q3 = r.hi / b.hi;
    return hz::add(hz::quickTwoSum(q1, q2), q3);
}
hz::DD ddExp(hz::DD x) {
    const hz::DD ln2{0.6931471805599453094, 2.3190468138462996154e-17};
    const double k = std::nearbyint(x.hi / ln2.hi);
    const hz::DD r = hz::add(x, hz::neg(ddMul(ln2, k)));   // |r| <= 0.35
    hz::DD sum = hz::dd(1.0), term = hz::dd(1.0);
    for (int n = 1; n < 40; ++n) {
        term = ddDiv(ddMul(term, r), hz::dd(double(n)));
        sum = hz::add(sum, term);
        if (std::fabs(term.hi) < 1e-34 * std::fabs(sum.hi)) break;
    }
    return {std::ldexp(sum.hi, int(k)), std::ldexp(sum.lo, int(k))};
}
// 1 - tanh(phi) = 2 e^(-2 phi) / (1 + e^(-2 phi)), in double-double
double epsLong(double phi) {
    const hz::DD e = ddExp(hz::dd(-2.0 * phi));
    return ddDiv(ddMul(e, 2.0), hz::add(e, 1.0)).value();
}

// ---- the drive's closed forms against a fine integration of dphi/dtau = +-kappa(phi), dt/dtau = cosh, dx/dtau = c sinh:
// the slices' rates only (not their sums), phi linear in tau within each, Simpson's rule on n steps per slice crossed
struct Integ { double dt, dx, phi; };
Integ integrate(double phi0, double sign, double tau, int n) {
    std::vector<double> sp, sk;
    hz::driveSlices(sp, sk);
    double t = 0.0, x = 0.0, phi = phi0, left = tau;
    while (left > 0.0) {
        if (sign < 0.0 && phi <= 0.0) break;
        // the slice: up from phi (thrust) or down from it (brake)
        size_t i;
        if (sign > 0.0) i = size_t(std::upper_bound(sp.begin(), sp.end(), phi) - sp.begin()) - 1;
        else i = size_t(std::lower_bound(sp.begin(), sp.end(), phi) - sp.begin()) - 1;
        const double end = sign > 0.0 ? (i + 1 < sp.size() ? sp[i + 1] : 1e300) : sp[i];
        const double seg = std::min(left, std::fabs(end - phi) / sk[i]);
        const double h = seg / n;
        for (int j = 0; j < n; ++j) {
            const double p0 = phi + sign * sk[i] * h * j;
            const double pm = p0 + sign * sk[i] * 0.5 * h, p1 = p0 + sign * sk[i] * h;
            t += h / 6.0 * (std::cosh(p0) + 4.0 * std::cosh(pm) + std::cosh(p1));
            x += h / 6.0 * hz::kC * (std::sinh(p0) + 4.0 * std::sinh(pm) + std::sinh(p1));
        }
        phi = seg < left ? end : phi + sign * sk[i] * seg;
        left -= seg;
        if (seg <= 0.0) break;
    }
    return {t, x, phi};
}

// Gauss-Legendre nodes on [-1, 1]
void gaussLegendre(int n, std::vector<double>& x, std::vector<double>& w) {
    x.resize(n);
    w.resize(n);
    for (int i = 0; i < n; ++i) {
        double z = std::cos(kPi * (i + 0.75) / (n + 0.5)), pp = 0.0;
        for (int it = 0; it < 100; ++it) {
            double p1 = 1.0, p2 = 0.0;
            for (int j = 1; j <= n; ++j) {
                const double p3 = p2;
                p2 = p1;
                p1 = ((2.0 * j - 1.0) * z * p2 - (j - 1.0) * p3) / j;
            }
            pp = n * (z * p1 - p2) / (z * z - 1.0);
            const double z1 = z;
            z = z1 - p1 / pp;
            if (std::fabs(z - z1) < 1e-16) break;
        }
        x[i] = z;
        w[i] = 2.0 / ((1.0 - z * z) * pp * pp);
    }
}

// a whole journey by the automatic arrival: Space at the start and again on reaching Proxima
struct Journey {
    double legTau[2] = {}, legEarth[2] = {};
    double tau = 0.0, earth = 0.0;
    bool arrived[2] = {};
    double pin = 0.0;
    long long steps = 0;
};
Journey fly(const hz::Route& r, const std::function<double(long long)>& dtOf, double maxTau) {
    Journey J;
    hz::Ship s = hz::depart(r);
    hz::Command go;
    go.toggleAuto = true;
    hz::step(s, r, 0.0, go);
    while (s.tau < maxTau && s.leg < hz::kLegDone) {
        const hz::Events ev = hz::step(s, r, dtOf(J.steps), hz::Command{});
        ++J.steps;
        J.pin = std::max(J.pin, ev.pinJump);
        if (ev.arrived) {
            J.arrived[ev.arrivedLeg] = true;
            J.legTau[ev.arrivedLeg] = ev.legTau;
            J.legEarth[ev.arrivedLeg] = ev.legEarth;
            if (s.leg == hz::kLegAndromeda) hz::step(s, r, 0.0, go);   // on to Andromeda (it turns first)
        }
    }
    J.tau = s.tau;
    J.earth = s.t.value();
    return J;
}

}   // namespace

int runHizTests(OlcekApp& app) {
    using hz::kC;
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "GECTI" : "KALDI", what);
        if (!ok) ++fails;
    };
    const hz::Route& R = app.route;
    const double cap = hz::phiCap();
    std::printf("hiz: ozel gorelilik sinamalari\n");

    // ---- 1. the drive: its law (Özcan's schedule, 1 g at rest, always rising) and its closed forms against integration
    {
        std::vector<double> sp, sk;
        hz::driveSlices(sp, sk);
        bool rising = true;
        double ratio = 0.0;
        for (size_t i = 0; i + 1 < sk.size(); ++i) {
            rising = rising && sk[i + 1] > sk[i] && sp[i + 1] > sp[i];
            ratio = std::max(ratio, sk[i + 1] / sk[i]);
        }
        const double g = hz::kG0 / kC;
        std::printf("    ivme: %zu dilim, komsu dilimler en cok %%%.2f farkli; durusta %.6f g, tavanda %.4g g\n", sk.size(), (ratio - 1.0) * 100.0,
                    hz::driveRate(0.0) / g, hz::driveRate(cap) / g);
        check(rising && ratio < 1.0101 && std::fabs(hz::driveRate(0.0) / g - 1.0) < 1e-12, "ivme durusta 1 g, hep artiyor, dilimler %1 adimla (hissedilmez)");
        const double t1 = hz::driveTime(std::atanh(0.01)), t10 = hz::driveTime(std::atanh(0.1)), t99 = hz::driveTime(std::atanh(0.99));
        const double tCap = hz::driveTime(cap);
        std::printf("    W ile durustan: %%1 c %.3f s, %%10 c %.3f s, %%99 c %.3f s, 21 dokuz %.3f s; 300 km/sa %.2f s, kacis hizi %.2f s\n", t1, t10, t99,
                    tCap, hz::driveTime(std::atanh(83.3333333 / kC)), hz::driveTime(std::atanh(11186.0 / kC)));
        check(std::fabs(t1 - 60.0) < 0.5 && std::fabs(t10 - 120.0) < 0.5 && std::fabs(t99 - 330.0) < 0.5 && std::fabs(tCap - 480.0) < 0.5,
              "Ozcan'in takvimi: %1 c 1 dk, %10 c 2 dk, %99 c 5,5 dk, 21 dokuz 8 dk");
        double worst = 0.0, worstPhi = 0.0;
        for (double phi0 : {0.0, 1e-7, 3e-4, 0.01, 0.5, 3.0, 12.0, 20.0, 23.0})
            for (double tau : {1.0 / 60.0, 0.7, 5.0}) {
                const hz::DriveStep a = hz::driveThrust(phi0, tau, 1e9);
                const Integ ia = integrate(phi0, 1.0, tau, 4000);
                worst = std::max({worst, std::fabs(a.dT - ia.dt) / ia.dt, std::fabs(a.dX - ia.dx) / ia.dx});
                worstPhi = std::max(worstPhi, std::fabs(a.phi1 - ia.phi) / std::max(ia.phi, 1e-300));
                // brake back down from there for the same time: the same way and time (the law runs backwards)
                const hz::DriveStep b = hz::driveBrake(a.phi1, tau);
                const Integ ib = integrate(a.phi1, -1.0, tau, 4000);
                worst = std::max({worst, std::fabs(b.dT - ib.dt) / ib.dt, std::fabs(b.dX - ib.dx) / ib.dx});
                worstPhi = std::max(worstPhi, std::fabs(b.phi1 - phi0) / std::max(phi0, 1e-12));
            }
        std::printf("  kapali bicimler / Simpson (dilim basina 4.000 adim): en buyuk goreli fark %.2e; hiz acisi %.2e\n", worst, worstPhi);
        check(worst < 1e-10 && worstPhi < 1e-9, "itis ve fren dilim dilim: dt = (sinh f1 - sinh f0)/k, dx = (c/k)(cosh f1 - cosh f0); fren itisin aynasi");
        // the way, the time and the peak: one law (D(phi*) = (r + D(phi0)) / 2 lands on r)
        double worstPeak = 0.0;
        for (double phi0 : {0.0, 0.003, 1.0, 9.0})
            for (double r : {1e3, 1e9, 4e16, 2.2e22}) {
                const double ps = hz::peakFrom(phi0, r);
                if (ps < phi0) continue;
                const double way = hz::stopDistance(ps) - hz::stopDistance(phi0) + hz::stopDistance(ps);
                worstPeak = std::max(worstPeak, std::fabs(way - r) / r);
            }
        check(worstPeak < 1e-9, "tepe hiz: oraya itis + oradan fren tam kalan yol");
        // the slices for the independent check (tools: an mpmath oracle reads them)
        std::filesystem::create_directories(std::filesystem::path(app.root) / "captures/olcek_hiz");
        if (FILE* f = std::fopen((std::filesystem::path(app.root) / "captures/olcek_hiz/ivme_dilimleri.csv").string().c_str(), "wb")) {
            std::fprintf(f, "phi_start,kappa_per_s\n");
            for (size_t i = 0; i < sp.size(); ++i) std::fprintf(f, "%.17g,%.17g\n", sp[i], sk[i]);
            std::fclose(f);
        }
        // ... and what this code makes of them, for the oracle to compare (the route's legs, the drive's sums, the journeys)
        if (FILE* f = std::fopen((std::filesystem::path(app.root) / "captures/olcek_hiz/ivme_sonuclar.txt").string().c_str(), "wb")) {
            std::fprintf(f, "leg0_m %.17g\nleg1_m %.17g\nphi_cap %.17g\n", R.length[0], R.length[1], cap);
            for (double phi : {1e-7, 1e-3, 0.0100003333466675, 0.100335347731076, 0.5, 2.6466524123622462, 10.0, 17.0, cap})
                std::fprintf(f, "phi %.17g T %.17g C %.17g D %.17g\n", phi, hz::driveTime(phi), hz::driveEarthTime(phi), hz::stopDistance(phi));
            for (int L = 0; L < 2; ++L) {
                const double ps = hz::peakFrom(0.0, R.length[L]);
                std::fprintf(f, "peak%d %.17g\n", L, ps);
            }
            std::fclose(f);
        }
    }

    // ---- 2. 1 - beta against a double-double evaluation
    {
        double worst = 0.0;
        for (double phi : {0.3, 1.0, 5.0, 10.0, 17.797, 24.0, cap}) {
            const double ref = epsLong(phi), got = hz::kin(phi).eps;
            const double e = std::fabs(got - ref) / ref;
            worst = std::max(worst, e);
            std::printf("    phi %8.4f  1 - beta %.17e  (cift-double %.17e, fark %.1e)\n", phi, got, ref, e);
        }
        check(worst < 4e-15, "1 - beta = 2 / (e^(2 phi) + 1): her hizda 15 basamak");
        const hz::Kin k = hz::kin(cap);
        std::printf("    tavan: phi %.15f, gamma %.10e, 1 - beta %.6e, %.6f dokuz\n", cap, k.gamma, k.eps, k.nines);
        check(std::fabs(k.eps / 1e-21 - 1.0) < 1e-13, "tavan: 21 dokuz (1 - beta = 1e-21)");
        // the identity (1 - beta)(1 + beta) = 1 / gamma^2, its two sides from independent formulas
        double wid = 0.0;
        for (double phi : {0.01, 2.0, 9.0, 17.0, cap}) {
            const hz::Kin q = hz::kin(phi);
            wid = std::max(wid, std::fabs(q.eps * (2.0 - q.eps) * q.gamma * q.gamma - 1.0));
        }
        check(wid < 1e-13, "(1 - beta)(1 + beta) gamma^2 = 1");
    }

    // ---- 3. the route
    {
        std::printf("    1. ayak %.6f isik yili, 2. ayak %.6f milyon isik yili, donus %.2f derece, Gunes'e en yakin %.3f isik yili\n",
                    R.length[0] / hz::kLy, R.length[1] / hz::kLy * 1e-6, R.turnDeg, R.sunPass / hz::kLy);
        check(std::fabs(R.length[0] / hz::kLy - 4.2457) < 0.0005, "Dunya -> Proxima 4,2457 isik yili (Gaia DR3, bugune tasinmis)");
        check(std::fabs(R.length[1] / hz::kLy * 1e-6 - 2.395) < 0.001, "Proxima -> Andromeda 2,395 milyon isik yili");
        check(std::fabs(R.turnDeg - 153.0) < 1.0 && std::fabs(R.sunPass / hz::kLy - 1.9) < 0.1, "Proxima'da ~153 derece donus, Gunes'in ~1,9 isik yili yanindan");
    }

    // ---- 4. the journeys: the automatic arrival, at 60 frames a second, at uneven frames, at long frames
    {
        // what the perfect drive takes, from the formulas alone (from rest to rest: thrust to the peak, coast at the cap,
        // brake on the stopping curve)
        auto ideal = [&](double L, double& tau, double& earth) {
            const double phiStar = hz::peakFrom(0.0, L);
            if (phiStar <= cap) {
                tau = 2.0 * hz::driveTime(phiStar);
                earth = 2.0 * hz::driveEarthTime(phiStar);
            } else {
                const double coast = (L - 2.0 * hz::stopDistance(cap)) / (kC * std::sinh(cap));
                tau = 2.0 * hz::driveTime(cap) + coast;
                earth = 2.0 * hz::driveEarthTime(cap) + coast * std::cosh(cap);
            }
        };
        double it[2], ie[2];
        ideal(R.length[0], it[0], ie[0]);
        ideal(R.length[1], it[1], ie[1]);
        std::printf("    kusursuz surus (formulden): Proxima %.6f s (%.3f dk) / %.6f yil, tepe %.4f (%.2f dokuz); Andromeda %.6f dk / %.6f milyon yil\n", it[0],
                    it[0] / 60.0, ie[0] / hz::kYear, hz::peakFrom(0.0, R.length[0]), hz::kin(hz::peakFrom(0.0, R.length[0])).nines, it[1] / 60.0,
                    ie[1] / hz::kYear * 1e-6);
        check(std::fabs(ie[0] / hz::kYear - 4.25) < 0.01 && std::fabs(ie[1] / hz::kYear * 1e-6 - 2.395) < 0.001, "Dunya'da 4,25 yil ve 2,395 milyon yil");
        struct Case { const char* name; std::function<double(long long)> dt; };
        unsigned long long lcg = 12345;
        const Case cases[] = {
            {"60 kare/s", [](long long) { return 1.0 / 60.0; }},
            {"duzensiz kareler (1/300 - 1/12 s)", [&](long long) {
                 lcg = lcg * 6364136223846793005ull + 1442695040888963407ull;
                 return 1.0 / 300.0 + (1.0 / 12.0 - 1.0 / 300.0) * double(lcg >> 11) / 9007199254740992.0;
             }},
            {"uzun kareler (0,5 s)", [](long long) { return 0.5; }},
        };
        double worstT = 0.0, worstE = 0.0, worstPin = 0.0;
        for (const Case& c : cases) {
            const Journey J = fly(R, c.dt, 9000.0);
            const double dT0 = std::fabs(J.legTau[0] - it[0]), dT1 = std::fabs(J.legTau[1] - it[1]);
            const double dE0 = std::fabs(J.legEarth[0] - ie[0]) / ie[0], dE1 = std::fabs(J.legEarth[1] - ie[1]) / ie[1];
            std::printf("    %-34s Proxima %.9f s (fark %.1e), Andromeda %.9f s (fark %.1e); Dunya'nin saati farki %.1e / %.1e; "
                        "egriye oturma en cok %.3g m; %lld adim\n",
                        c.name, J.legTau[0], dT0, J.legTau[1], dT1, dE0, dE1, J.pin, J.steps);
            check(J.arrived[0] && J.arrived[1], "iki varis da tam hedefte (isinlanma yok: fren egrisinin ucu)");
            worstT = std::max({worstT, dT0, dT1});
            worstE = std::max({worstE, dE0, dE1});
            worstPin = std::max(worstPin, J.pin);
        }
        check(worstT < 1e-6, "yolcunun saati kare uzunlugundan bagimsiz (olaylar kare icinde bolunuyor)");
        check(worstE < 1e-12, "Dunya'nin saati formulle ayni");
        std::printf("    egriye oturtma: en buyuk duzeltme %.3g m (o anda roket karede ~10^17 m yol aliyor)\n", worstPin);
    }

    // ---- 5. by hand: thrust past the middle, then Space (too fast to stop: it passes, stops, turns and comes back);
    // and the cap (W held: phi stops at 21 nines)
    {
        const hz::Route& r = R;
        hz::Ship s = hz::depart(r);
        hz::Command w;
        w.thrust = true;
        const double mid = hz::driveTime(hz::peakFrom(0.0, r.length[0]));   // W this long from rest: the middle of the way
        for (int i = 0; i < int(60.0 * (mid + 2.0)); ++i) hz::step(s, r, 1.0 / 60.0, w);   // 2 s past the middle: too fast to stop
        const bool past = hz::stopDistance(s.phi) > hz::remaining(s);
        hz::Command sp;
        sp.toggleAuto = true;
        hz::Events ev = hz::step(s, r, 0.0, sp);
        bool overshoot = ev.overshoot, turned = false, arrived = false;
        double tau0 = s.tau;
        for (int i = 0; i < 60 * 3000 && !arrived; ++i) {
            ev = hz::step(s, r, 1.0 / 60.0, hz::Command{});
            overshoot = overshoot || ev.overshoot;
            turned = turned || ev.turnStarted;
            arrived = ev.arrived;
        }
        std::printf("    elle: %.1f s itis (durma mesafesi > kalan: %s), Bosluk: gecti %s, dondu %s, vardi %s (%.1f s sonra)\n", mid + 2.0, past ? "evet" : "hayir",
                    overshoot ? "evet" : "hayir", turned ? "evet" : "hayir", arrived ? "evet" : "hayir", s.tau - tau0);
        check(past && overshoot && turned && arrived && s.leg == hz::kLegAndromeda, "hedefi gecince: durur, doner, geri gelir, tam hedefte durur");

        hz::Ship c = hz::depart(r);
        for (int i = 0; i < 60 * 500; ++i) hz::step(c, r, 1.0 / 60.0, w);   // 500 s of W from rest: the cap at 480 s
        std::printf("    500 s W: phi %.15f (tavan %.15f), %s\n", c.phi, cap, c.drive == hz::Drive::Coast ? "suzuluyor" : "itiyor");
        check(c.phi == cap && c.drive == hz::Drive::Coast, "W tavanda durur: 21 dokuzun otesi yok");
        hz::Command b;
        b.brake = true;
        for (int i = 0; i < 60 * 500; ++i) hz::step(c, r, 1.0 / 60.0, b);   // braking from the cap: 480 s
        check(c.phi == 0.0 && c.drive == hz::Drive::Rest, "S durur ve geri gitmez");

        // slow, then Space: the quickest arrival from there (not centuries of coasting at the speed it had)
        hz::Ship q = hz::depart(r);
        hz::Command tap;
        tap.thrust = tap.thrustPressed = true;
        hz::step(q, r, 1.0 / 60.0, tap);
        const double tau0q = q.tau;
        hz::step(q, r, 0.0, sp);
        bool arrivedQ = false;
        const double bestQ = 2.0 * hz::driveTime(hz::peakFrom(0.0, r.length[0]));   // the quickest from rest (its peak is under the cap)
        for (int i = 0; i < 60 * 1200 && !arrivedQ; ++i) arrivedQ = hz::step(q, r, 1.0 / 60.0, hz::Command{}).arrived;
        std::printf("    bir kare W, sonra Bosluk: %s, %.3f s'de (en hizlisi %.3f s)\n", arrivedQ ? "vardi" : "varmadi", q.tau - tau0q + 1.0 / 60.0, bestQ);
        check(arrivedQ && q.tau - tau0q < bestQ + 1.0, "yavasken Bosluk: en kisa yoldan (hizlanir), yuzyillarca suzulmez");

        // stopped by hand close to the target: the automatic arrival flies the last stretch (and nothing jumps)
        hz::Ship n = hz::depart(r);
        n.x = hz::dd(-3e12);   // 20 AU short, at rest
        bool captured = false, arrivedN = false;
        double jump = 0.0;
        for (int i = 0; i < 60 * 1500 && !arrivedN; ++i) {
            const double x0 = n.x.value(), phi0 = n.phi;
            const hz::Events e2 = hz::step(n, r, 1.0 / 60.0, hz::Command{});
            captured = captured || e2.captured;
            arrivedN = e2.arrived;
            if (!arrivedN) {   // no step longer than the motion allows (the fastest it could have gone this frame)
                const double most = hz::kC * std::sinh(std::max(phi0, n.phi)) / 60.0 + 1e-6;
                jump = std::max(jump, std::fabs(n.x.value() - x0) / most);
            }
        }
        std::printf("    20 AU kala durmus: yakalandi %s, vardi %s (%.2f s); adim / izin verilen en cok %.3f\n", captured ? "evet" : "hayir",
                    arrivedN ? "evet" : "hayir", n.tau, jump);
        check(captured && arrivedN && jump <= 1.0 + 1e-9, "elle hedefe yakin durunca son parcayi otomatik varis ucar (isinlanmadan)");

        // ... but a hand that takes over there keeps it: S during that last stretch, the ship stops and stays
        hz::Ship hv = hz::depart(r);
        hv.x = hz::dd(-3e12);
        hz::step(hv, r, 1.0 / 60.0, hz::Command{});   // captured: it starts the last stretch
        for (int i = 0; i < 60; ++i) hz::step(hv, r, 1.0 / 60.0, hz::Command{});
        hz::Command sb;
        sb.brake = sb.brakePressed = true;
        hz::step(hv, r, 1.0 / 60.0, sb);
        sb.brakePressed = false;
        for (int i = 0; i < 60 * 10 && hv.phi > 0.0; ++i) hz::step(hv, r, 1.0 / 60.0, sb);
        bool recaptured = false;
        for (int i = 0; i < 60 * 20; ++i) recaptured = recaptured || hz::step(hv, r, 1.0 / 60.0, hz::Command{}).captured;
        check(!recaptured && !hv.autopilot && hv.phi == 0.0, "hedefin yakininda S ile devralinca durur ve oyle kalir (geri yakalanmaz)");
        // and the turn the notice names is the one made (after passing the target it is not Proxima's 153 degrees)
        check(std::fabs(R.turnDeg - 153.4) < 0.2, "ayaklar arasi donus 153,4 derece");

        // a held key does not take over (Space pressed with W down), a pressed one does
        hz::Ship h = hz::depart(r);
        hz::Command held;
        held.thrust = true;
        held.toggleAuto = true;
        hz::step(h, r, 1.0 / 60.0, held);
        held.toggleAuto = false;
        hz::step(h, r, 1.0 / 60.0, held);
        const bool stillOn = h.autopilot;
        held.thrustPressed = true;
        const hz::Events e3 = hz::step(h, r, 1.0 / 60.0, held);
        check(stillOn && !h.autopilot && e3.autoCancelled, "basili W otomatik varisi bozmaz, W'ye basmak devralir");
    }

    // ---- 6. aberration and Doppler
    {
        double worstInv = 0.0, worstD = 0.0, worstFast = 0.0, worstDir = 0.0, worstDirNear = 0.0;
        for (double phi : {0.01, 0.5, 2.0, 7.0, 17.8, cap})
            for (double th : {0.0, 1e-9, 1e-4, 0.3, 1.0, kPi / 2.0, 2.2, 3.0, kPi - 1e-6, kPi}) {
                const double thp = hz::aberrate(th, phi);
                const double back = hz::unaberrate(thp, phi);
                worstInv = std::max(worstInv, std::fabs(back - th) / std::max(th, 1e-300) * (th > 0.0 ? 1.0 : 0.0));
                // the rest frame's D and the rocket frame's D of the same light: one number
                const double d1 = hz::dopplerRest(th, phi), d2 = hz::dopplerSeen(thp, phi);
                worstD = std::max(worstD, std::fabs(d1 / d2 - 1.0));
                // the fast (algebraic) form against the trigonometric one, on directions
                const V3 m = normalize(v3(0.3, -0.5, 0.81));
                V3 a = normalize(cross(m, v3(0.0, 0.0, 1.0)));
                const V3 n = m * std::cos(th) + a * std::sin(th);
                double Da, Db;
                const V3 x1 = hz::aberrateDir(n, m, phi, Da);
                const V3 x2 = hz::aberrateDirFast(n, m, hz::kin(phi), std::exp(-phi), Db);
                // Straight behind the map is ill-conditioned: a unit vector's ~1e-16 of noise across the motion comes out
                // e^phi times larger (tan(theta'/2) = e^-phi tan(theta/2) near pi: the stern magnifies by e^phi), so
                // the two forms agree there only to that noise (~1e-5 rad at 21 nines). Compared where the input is
                // well defined: at least 0.1 rad off the stern.
                if (th > 3.05) continue;
                worstFast = std::max(worstFast, std::fabs(Da / Db - 1.0));
                // the two directions apart (radians: a unit vector holds ~1e-16 absolute, so an angle of 1e-12 off the bow
                // is good to ~1e-4 of itself in either form; the screen's smallest field, 1e-11 rad, needs ~1e-14)
                worstDir = std::max(worstDir, length(x1 - x2));
                if (th < 1.0) worstDirNear = std::max(worstDirNear, length(x1 - x2));
            }
        std::printf("    sapinc tersi %.1e, D(teta) = D'(teta') %.1e, hizli bicim D %.1e, yon %.1e rad (onde %.1e rad)\n", worstInv, worstD, worstFast,
                    worstDir, worstDirNear);
        check(worstInv < 1e-12, "ters sapinc: tan(teta/2) = e^phi tan(teta'/2) geri getirir");
        check(worstD < 1e-12, "D = gamma (1 + beta cos teta) = 1 / (gamma (1 - beta cos teta')), D(teta) D'(teta') = 1");
        check(worstFast < 1e-13 && worstDir < 1e-14, "trigonometrisiz bicim (her yildiz, her kare) trigonometrik olanla ayni");
        // the isotropic energy density seen in motion: (1/4 pi) integral of D'^4 over the rocket's sky = gamma^2 (1 + beta^2/3)
        // (over the rest frame's sky with dOmega' = dOmega / D^2: each node through aberrate and dopplerSeen)
        std::vector<double> gx, gw;
        gaussLegendre(48, gx, gw);
        double worstU = 0.0;
        for (double phi : {0.2, 1.0, 3.0, 10.0, cap}) {
            const hz::Kin k = hz::kin(phi);
            double u = 0.0;
            for (size_t i = 0; i < gx.size(); ++i) {
                const double th = std::acos(gx[i]);
                const double Dp = hz::dopplerSeen(hz::aberrate(th, phi), phi), D = hz::dopplerRest(th, phi);
                u += 0.5 * gw[i] * Dp * Dp * Dp * Dp / (D * D);
            }
            const double want = k.gamma * k.gamma * (1.0 + k.beta * k.beta / 3.0);
            worstU = std::max(worstU, std::fabs(u / want - 1.0));
            std::printf("    phi %6.3f: enerji yogunlugu %.10e u (beklenen %.10e)\n", phi, u, want);
        }
        check(worstU < 1e-10, "es yonlu isimanin enerji yogunlugu gamma^2 (1 + beta^2/3) u (sapinc + Doppler + aci olcegi birlikte)");
    }

    // ---- 7. the colour of light: the table against explicit spectra
    {
        const hz::Blackbody& bb = app.bb;
        std::printf("    renk tablosu: %s, %d dalga boyu\n", bb.source.c_str(), bb.samples);
        // the Planck locus (CIE 1931 2-degree): published chromaticities
        struct Ref { double T, x, y; };
        const Ref refs[] = {{1000.0, 0.6528, 0.3444}, {2000.0, 0.5267, 0.4133}, {3000.0, 0.4369, 0.4041}, {5000.0, 0.3451, 0.3516},
                            {6500.0, 0.3135, 0.3237}, {10000.0, 0.2807, 0.2884}, {1e12, 0.2399, 0.2342}};
        double worstXY = 0.0;
        for (const Ref& r : refs) {
            double y;
            const double x = bb.chromaticity(r.T, y);
            worstXY = std::max({worstXY, std::fabs(x - r.x), std::fabs(y - r.y)});
            std::printf("    %8.0f K: x %.4f y %.4f (yayimlanan %.4f %.4f)\n", r.T, x, y, r.x, r.y);
        }
        check(worstXY < 2e-3, "Planck egrisi: kromatiklik yayimlanan degerlerle");
        // a Doppler-shifted blackbody, integrated wavelength by wavelength (I'(nu') = D^3 B(nu'/D, T)), against the
        // table at D T: the visible light and its colour
        auto planckLam = [](double lamM, double T) { return hz::kPlanckC1 / std::pow(lamM, 5.0) / std::expm1(hz::kPlanckC2 / (lamM * T)); };
        double worstY = 0.0;
        for (double T : {3042.0, 5772.0, 9940.0})
            for (double D : {0.25, 0.5, 2.0, 5.0}) {
                // in wavelength: lambda = lambda'_obs * D (the source frame's), I'_lam'(lam') = I'_nu' |dnu'/dlam'|:
                // B_lam'(lam', D T) by the invariance; computed here the long way, from the source's B at lam' D
                double Y0 = 0.0, Y1 = 0.0;
                for (int l = 380; l <= 780; ++l) {
                    const double lam = l * 1e-9;
                    const double nu = hz::kC / lam;
                    const double Bnu = [&](double nuS) { return 2.0 * 6.62607015e-34 * nuS * nuS * nuS / (hz::kC * hz::kC) / std::expm1(6.62607015e-34 * nuS / (1.380649e-23 * T)); }(nu / D);
                    const double Inu = D * D * D * Bnu;                    // observed specific intensity at nu
                    const double Ilam = Inu * hz::kC / (lam * lam);        // per metre of wavelength
                    // y-bar from the same table the scene uses (here: the published 5-nm CIE values via the fit)
                    const double ybar = std::exp(-0.5 * std::pow((l - 568.8) / (l < 568.8 ? 46.9 : 40.5), 2)) * 0.821 +
                                        std::exp(-0.5 * std::pow((l - 530.9) / (l < 530.9 ? 16.3 : 31.1), 2)) * 0.286;
                    Y1 += ybar * Ilam * 1e-9;
                    Y0 += ybar * planckLam(lam, D * T) * 1e-9;
                }
                worstY = std::max(worstY, std::fabs(Y1 / Y0 - 1.0));
            }
        std::printf("    kaymis kara cisim: D^3 B(nu/D, T) ile B(nu, D T) gorunur bantta en buyuk fark %.1e\n", worstY);
        check(worstY < 1e-12, "Doppler'de T sicakligindaki kara cisim D T sicakliginda kara cisimdir (I_nu / nu^3 degismez)");
        // the table's visible share against a direct integration at a few temperatures (its interpolation)
        const double s1 = bb.share(5772.0), s2 = bb.share(11544.0), s3 = bb.share(3042.0);
        std::printf("    gorunur pay (Gunes'e gore): 3042 K %.4f, 5772 K %.4f, 11544 K %.4f, 1e5 K %.3e, 1e8 K %.3e, 1e12 K %.3e\n", s3, s1, s2, bb.share(1e5),
                    bb.share(1e8), bb.share(1e12));
        check(std::fabs(s1 - 1.0) < 1e-12, "Gunes'in kendi payi 1");
        check(bb.share(1e9) / bb.share(1e8) < 1.1e-3 && bb.share(1e9) / bb.share(1e8) > 0.9e-3, "cok sicakta gorunur pay T^-3 (Rayleigh-Jeans)");
    }

    // ---- 8. the save file: the journey round-trips bit for bit
    {
        hz::Ship s = hz::depart(R);
        hz::Command w;
        w.thrust = true;
        for (int i = 0; i < 1000; ++i) hz::step(s, R, 1.0 / 61.0, w);
        hz::Command sp;
        sp.toggleAuto = true;
        hz::step(s, R, 0.0, sp);
        for (int i = 0; i < 300; ++i) hz::step(s, R, 1.0 / 59.0, hz::Command{});
        const std::string a = hz::serialize(s);
        std::map<std::string, std::string> kv;
        std::istringstream ss(a);
        std::string line;
        while (std::getline(ss, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            k.erase(k.find_last_not_of(' ') + 1);
            v.erase(0, v.find_first_not_of(' '));
            kv[k] = v;
        }
        hz::Ship b;
        const bool ok = hz::parse(b, kv);
        const bool same = ok && hz::serialize(b) == a && b.phi == s.phi && b.x.hi == s.x.hi && b.x.lo == s.x.lo && b.t.hi == s.t.hi && b.tau == s.tau;
        // and the two then fly on identically
        bool still = same;
        for (int i = 0; i < 600 && still; ++i) {
            hz::step(s, R, 1.0 / 60.0, hz::Command{});
            hz::step(b, R, 1.0 / 60.0, hz::Command{});
            still = s.phi == b.phi && s.x.hi == b.x.hi && s.x.lo == b.x.lo && s.t.hi == b.t.hi;
        }
        check(same && still, "kayit dosyasi: yolculuk bit bit geri gelir ve ayni surer");
    }

    {   // BEDEL: the plan's numbers (PLAN §4), and the plate's light against the boosted stress-energy tensor
        const hz::Cost c = hz::costOf(hz::phiCap());
        const double sunS = 1.0 / hz::kSunLuminosity;
        const double h = c.kinetic * sunS / 3600.0, ph = c.photon * sunS / 3600.0, pst = c.photonStop * sunS / hz::kYear * 1e-6;
        std::printf("    21 dokuzda: hareket enerjisi Gunes'in %.3f saati, foton roketi %.3f saat, durmakla %.2f milyon yil\n", h, ph, pst);
        std::printf("    kutle orani %.3e (durmakla %.3e), ivme %.2f milyon g, H atomu %.3e eV, onde fon %.3e K\n", c.massRatio, c.massRatioStop,
                    c.gees * 1e-6, c.hydrogen, c.cmbAhead);
        check(std::fabs(h - 1.46) < 0.01 && std::fabs(ph - 2.9) < 0.05 && std::fabs(pst - 15.0) < 0.5,
              "BEDEL, 21 dokuz: Gunes'in 1,46 saati; foton roketi 2,9 saat, durmakla 15 milyon yil (PLAN §4)");
        check(std::fabs(c.massRatio / 4.5e10 - 1.0) < 0.01 && std::fabs(c.massRatioStop / 2e21 - 1.0) < 0.05 && std::fabs(c.gees * 1e-6 - 11.1) < 0.05 &&
                  std::fabs(c.hydrogen / 2e19 - 1.0) < 0.1,
              "BEDEL, 21 dokuz: kutle orani 4,5e10 (durmakla 2e21), tavanda 11 milyon g (yeni ivme), H atomu 2e19 eV (PLAN §4)");
        const double p17 = hz::costOf(0.5 * std::log(2.0 / 1e-17 - 1.0)).plate / hz::kSolarConstant;   // (1 - beta = 1e-17)
        const double p21 = c.plate / hz::kSolarConstant;
        std::printf("    onden gelen isima: 17 dokuzda Gunes isiginin %.3e, 21 dokuzda %.3e kati\n", p17, p21);
        check(std::fabs(p17 / 6e8 - 1.0) < 0.05 && std::fabs(p21 / 6e12 - 1.0) < 0.05, "onden gelen isima: 17 dokuzda 6e8, 21 dokuzda 6e12 Gunes isigi (PLAN §4)");
        // the plate takes the forward hemisphere; its back would take the rear one. Their difference is the net flux of
        // the isotropic field seen moving, T'^{0z} = (4/3) gamma^2 beta u c = (16/3) gamma^2 beta sigma T0^4: a check
        // from another route (the tensor's boost, not the hemisphere's integral)
        double worst = 0.0;
        const double s4 = hz::kStefan * std::pow(hz::kCmbToday, 4.0);
        for (double phi : {0.01, 0.1, 0.5, 1.0, 3.0, 8.0, 15.0, hz::phiCap()}) {
            const hz::Kin k = hz::kin(phi);
            const double front = hz::cmbOnPlate(k);
            const double ep = 1.0 + k.beta;   // the rear hemisphere: beta -> -beta
            const double back = 2.0 * s4 / (std::pow(k.gamma, 4.0) * k.beta * k.beta) * (1.0 / 6.0 + 1.0 / (3.0 * ep * ep * ep) - 1.0 / (2.0 * ep * ep));
            const double net = 16.0 / 3.0 * k.gamma * k.gamma * k.beta * s4;
            worst = std::max(worst, std::fabs((front - back) / net - 1.0));
        }
        // the series near rest meets the closed form where they hand over (beta = 1e-3)
        const double cont = std::fabs(hz::cmbOnPlate(hz::kin(std::atanh(1.001e-3))) / hz::cmbOnPlate(hz::kin(std::atanh(0.999e-3))) - 1.0);
        std::printf("    plaka: on - arka yarikure = (16/3) gamma^2 beta sigma T0^4, en buyuk fark %.1e; seriden kapali bicime gecis %.1e\n", worst, cont);
        check(worst < 1e-9 && cont < 1e-5, "onden gelen isima: yarikure integrali, gerilim-enerji tensorunun donusumuyle ayni");
    }

    // ---- 8. the eye's sky (H3): the sums the exposure is made of, the eye's adaptation, the energy eye's colours
    {
        // the cosmic background alone through the renderer's own sums (skySums: the rest frame's grid, each cell's light
        // D^2 times): its light at the rocket from every direction is 4 sigma T0^4 gamma^2 (1 + beta^2/3) (the dipole
        // moves it by at most 0.25%); at speed nearly all of it within 5 degrees of the bow (the glare's source)
        double worst = 0.0, fwdShare10 = 0.0;
        const double s4 = hz::kStefan * std::pow(hz::kCmbToday, 4.0);
        for (double phi : {0.0, 0.5, 2.0, 7.0, 10.0, 15.0, hz::phiCap()}) {
            hz::Eye e;
            e.phi = phi;
            e.motion = V3{1.0, 0.0, 0.0};
            e.fwd = V3{1.0, 0.0, 0.0};
            e.right = V3{0.0, -1.0, 0.0};
            e.up = V3{0.0, 0.0, 1.0};
            hz::EyeSum sum;
            hz::skySums(hz::SkyCoarse{}, app.bb, e, 0.0, V3{1.0, 0.0, 0.0}, V3{0.0, 1.0, 0.0}, V3{0.0, 0.0, 1.0}, sum);
            const hz::Kin k = hz::kin(phi);
            const double want = 4.0 * s4 * k.gamma * k.gamma * (1.0 + k.beta * k.beta / 3.0);
            worst = std::max(worst, std::fabs(sum.bolometric / want - 1.0));
            if (phi == 10.0) fwdShare10 = sum.fwdBolometric / sum.bolometric;
            std::printf("    phi %6.3f: fon isimasi butun bantlarda %.6e W/m^2 (beklenen %.6e), gorunur %.3e, burnun 5 derecesinde %%%.2f\n", phi,
                        sum.bolometric, want, sum.visible, 100.0 * sum.fwdBolometric / sum.bolometric);
        }
        check(worst < 5e-3, "goz: fon isimasi her hizda 4 sigma T0^4 gamma^2 (1 + beta^2/3) (gokyuzu toplamlari)");
        {   // the energy eye's zero: the glow at rest taken out (none left at rest but the dipole's; at speed the sky's sum
            // over it, no less than the whole sky's gain 4 sigma T0^4 (gamma^2 (1 + beta^2/3) - 1), no more than all of it)
            bool ok = true;
            for (double phi : {0.0, 0.5, 2.0, 10.0}) {
                hz::Eye e;
                e.phi = phi;
                e.motion = V3{1.0, 0.0, 0.0};
                e.fwd = V3{1.0, 0.0, 0.0};
                e.right = V3{0.0, -1.0, 0.0};
                e.up = V3{0.0, 0.0, 1.0};
                hz::EyeSum sum;
                hz::skySums(hz::SkyCoarse{}, app.bb, e, 0.0, V3{1.0, 0.0, 0.0}, V3{0.0, 1.0, 0.0}, V3{0.0, 0.0, 1.0}, sum);
                const hz::Kin k = hz::kin(phi);
                const double gain = 4.0 * s4 * (k.gamma * k.gamma * (1.0 + k.beta * k.beta / 3.0) - 1.0);
                std::printf("    phi %6.3f: enerji gozunde fon %.4e W/m^2 (butun gokun kazanci %.4e, butunu %.4e)\n", phi, sum.energy, gain, sum.bolometric);
                if (phi == 0.0) ok = ok && sum.energy < 3e-3 * 4.0 * s4;
                else ok = ok && sum.energy >= gain * (1.0 - 5e-3) && sum.energy <= sum.bolometric;
            }
            check(ok, "goz: enerji gozunde fonun durustaki isimasi sifir (durusta yalniz dipol), hizda kazanci gorunur");
        }
        check(fwdShare10 > 0.99, "goz: phi 10'da fon isimasinin %99'dan fazlasi burnun 5 derecesinde (parlamanin kaynagi)");

        // the night sky at the Sun's place against the one measured: starlight alone ~1e-4 lux on a moonless night (on a
        // plane: a quarter of the whole sky's light for an even sky), the Sun's spectrum's 93 lm/W
        const double lux = app.nightSky * 93.0 / 4.0;
        std::printf("    gece goku: gorunur %.3e W/m^2 (yildizlar + Samanyolu), bir duzleme ~%.1e lux; butun bantlar %.3e W/m^2 (fon %%%.0f)\n",
                    app.nightSky, lux, app.nightSkyBol, 100.0 * 4.0 * s4 / app.nightSkyBol);
        check(lux > 5e-5 && lux < 1e-3, "goz: gece goku yildiz isigi mertebesinde (~1e-4 lux, aysiz gece)");
        if (app.skyCoarse.w == 0) std::printf("    (Samanyolu panoramasi yok: tools/olcek_veri.py samanyolu; panorama denetimleri atlandi)\n");
        else {
            // the panorama: its light at rest, all of it seen from the Sun, its colour temperatures those of starlight
            hz::Eye e;
            hz::EyeSum withPano, without;
            const V3 gx = app.sc.galCentreDir(), gn = app.sc.galNorth(), gy = cross(gn, gx);
            hz::skySums(app.skyCoarse, app.bb, e, 1.0, gx, gy, gn, withPano);
            hz::skySums(hz::SkyCoarse{}, app.bb, e, 1.0, gx, gy, gn, without);
            const double pano = withPano.visible - without.visible;
            double tMin = 1e30, tMax = 0.0;
            for (const float4& p : app.skyCoarse.px)
                if (0.2126 * p.x + 0.7152 * p.y + 0.0722 * p.z > 0.0) {
                    tMin = std::min(tMin, double(p.w) * 1e4);
                    tMax = std::max(tMax, double(p.w) * 1e4);
                }
            std::printf("    Samanyolu panoramasi: gorunur %.3e W/m^2 (gece gokunun %%%.0f), renk sicakliklari %.0f .. %.0f K\n", pano,
                        100.0 * pano / app.nightSky, tMin, tMax);
            check(pano > 0.1 * app.nightSky && pano < 0.9 * app.nightSky && tMin > 1500.0 && tMax < 20000.0,
                  "goz: panorama gece gokunun bir parcasi, renkleri yildiz isigi sicakliginda");
        }

        // the eye's adaptation: at rest hiz.ev; in the dark half an EV per doubling of the light, in daylight a whole one
        const double base = app.cfg.f("hiz.ev", 17.5f), n0 = app.nightSky;
        const double dark = (app.eyeEv(n0 * 32.0) - app.eyeEv(n0 * 8.0)) / 2.0, day = (app.eyeEv(n0 * std::exp2(32.0)) - app.eyeEv(n0 * std::exp2(30.0))) / 2.0;
        std::printf("    gozun uyumu: durusta EV %.5f (hiz.ev %.2f); karanlikta %.3f, gunduzde %.3f EV / iki kat isik; gunduz payi %.4f .. %.4f\n",
                    app.eyeEv(n0), base, dark, day, app.eyeDay(n0, false), app.eyeDay(n0 * std::exp2(30.0), false));
        check(std::fabs(app.eyeEv(n0) - base) < 1e-4 && std::fabs(dark + 0.5) < 0.01 && std::fabs(day + 1.0) < 0.01 &&
                  app.eyeDay(n0, false) < 1e-3 && app.eyeDay(n0 * std::exp2(30.0), false) > 0.999,
              "goz: karanlikta yari uyum (parlak gok parlak gorunur), gunduzde tam uyum (Weber)");

        // the energy eye's colours: luminance 1 all the way, no jump anywhere (the colour moves with the speed)
        double lumErr = 0.0, jump = 0.0;
        float3 last{};
        for (int i = 0; i <= 13000; ++i) {
            const float lt = -1.0f + 0.001f * float(i);
            const float3 c = energyColour(lt, app.bb.colour(std::pow(10.0, std::clamp(double(lt), -1.0, 12.0))));
            lumErr = std::max(lumErr, std::fabs(0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z - 1.0));
            if (i > 0) jump = std::max({jump, double(std::fabs(c.x - last.x)), double(std::fabs(c.y - last.y)), double(std::fabs(c.z - last.z))});
            last = c;
        }
        std::printf("    enerji gozunun renkleri: parlaklik 1'den en cok %.1e sapiyor, 0,001 onluk adimda en buyuk sicrama %.4f\n", lumErr, jump);
        check(lumErr < 1e-3 && jump < 0.05, "goz: enerji gozunun renkleri parlaklik 1, sicramasiz");
    }

    // ---- 9. the galaxies in the eye (H4): the models where they are wide, points where they are small, in the sums
    {
        const hz::Route& R = app.route;
        auto eyeAt = [&](double frac, double phi, hz::EyeSum& sum) {
            hz::Ship s = hz::depart(R);
            s.leg = 1;
            s.x = hz::dd(-R.length[1] * (1.0 - frac));
            s.phi = phi;
            s.sense = 1;
            s.heading = R.dir[1];
            hz::Eye e = app.eyeAt(s, 0.0, 0.0, std::tan(37.5 * kPi / 180.0));
            e.k = std::exp2(17.0) * 3e-3;
            app.renderEye(e, sum);
            return hz::position(s, R);
        };
        // at the arrival (100,000 ly before Andromeda's centre): its light is the sky's, L / 4 pi d^2 (M_V -21.5)
        hz::EyeSum a;
        const V3 pa = eyeAt(1.0, 0.0, a);
        const double dM = length(app.sc.nodeFromSun(olcek::kM31) - pa);
        const double want = 3.4e10 * 3.828e26 / (4.0 * kPi * dM * dM);
        const bool modelArr = app.view.galaxy[1].on != 0;
        // half way: Andromeda a model at rest; at 21 nines a point ahead (smaller than a pixel: 1 / D of its angle)
        hz::EyeSum m0, m21;
        eyeAt(0.5, 0.0, m0);
        const bool modelMid = app.view.galaxy[1].on != 0;
        eyeAt(0.5, hz::phiCap(), m21);
        const bool pointFast = app.view.galaxy[1].on == 0;
        std::printf("    varista Andromeda %.0f ly uzakta: gokyuzu %.4e W/m^2, L/4 pi d^2 %.4e; model: varista %d, yolun ortasinda %d, "
                    "21 dokuzda %d\n", dM / hz::kLy, a.visible, want, int(modelArr), int(modelMid), int(!pointFast));
        check(a.visible > want && a.visible < 1.1 * want && modelArr && modelMid && pointFast,
              "goz: Andromeda varista gokyuzunun isigi, yakinda model, 21 dokuzda bir nokta");
    }

    // ---- 10. the stops (H5): Alpha Centauri from Proxima, Proxima's disc and b from the stop, the Earth at the start
    {
        const hz::Route& R = app.route;
        const V3 stop = R.target[0];
        const double AU = 1.495978707e11;
        // A and B at their measured places: 12,016 and 12,010 AU from Proxima, -6.8 and -5.5 there (PLAN §3.3)
        bool ab = app.alphaCenStar[0] >= 0 && app.alphaCenStar[1] >= 0;
        double dA = 0.0, dB = 0.0, mA = 0.0, mB = 0.0;
        if (ab) {
            auto mag = [&](int i, double& d) {
                const V3 p = app.sc.stars()[size_t(i)];
                d = length(p - R.proxima) / AU;
                const double r = length(p - stop);
                const double f = double(app.sc.starLight()[size_t(i)].w) * 3.828e26 / (4.0 * kPi * r * r);
                return -26.74 - 2.5 * std::log10(f / 1361.0);
            };
            mA = mag(app.alphaCenStar[0], dA);
            mB = mag(app.alphaCenStar[1], dB);
        }
        std::printf("    Alfa Erboga Proxima'dan: A %.0f AU, %.2f kadir; B %.0f AU, %.2f kadir\n", dA, mA, dB, mB);
        check(ab && std::fabs(dA - 12016.0) < 30.0 && std::fabs(dB - 12010.0) < 30.0 && std::fabs(mA + 6.79) < 0.12 && std::fabs(mB + 5.47) < 0.12,
              "duraklar: Alfa Erboga A ve B Proxima'dan 12.000 AU'da, -6,8 ve -5,5 kadir");
        // Proxima from the stop (b's orbit): 1.69 degrees; b 60 degrees off it, 0.0486 AU away, at the automatic arrival
        hz::Ship s = hz::depart(R);
        s.x = hz::dd(0.0);
        hz::Eye e = app.eyeAt(s, 0.0, 0.0, std::tan(37.5 * kPi / 180.0));
        e.worldT = 2.0 * hz::driveEarthTime(hz::peakFrom(0.0, R.length[0]));
        e.k = std::exp2(10.0) * 3e-3;
        hz::EyeSum sum;
        app.renderEye(e, sum);
        const double dP = length(R.proxima - stop);
        const double discDeg = 2.0 * std::asin(0.1542 * 6.957e8 / dP) * 180.0 / kPi;
        const V3 bPos = app.sc.nodeFromSun(olcek::kProximaB);
        const double bAng = std::acos(std::clamp(dot(normalize(bPos - stop), normalize(R.proxima - stop)), -1.0, 1.0)) * 180.0 / kPi;
        const double bDist = length(bPos - stop) / AU;
        std::printf("    duraktan Proxima %.3f derece; b Proxima'dan %.1f derece, %.4f AU uzakta; gozun olcusu %.3e (alanin ortalamasi %.3e)\n",
                    discDeg, bAng, bDist, sum.adaptVisible, sum.visibleView / sum.viewShare);
        check(std::fabs(discDeg - 1.69) < 0.02 && std::fabs(bAng - 60.0) < 1.0 && std::fabs(bDist - 0.0486) < 0.001,
              "duraklar: duraktan Proxima 1,69 derece, b ondan 60 derece ve 0,049 AU otede");
        check(sum.adaptVisible > 100.0 * sum.visibleView / sum.viewShare,
              "duraklar: Proxima'ya bakan goz onun yuzune uyar (alanin ortalamasina degil)");
        // the start: looking back, the Earth fills 17 degrees and the eye adapts to its sunlit face
        hz::Ship s0 = hz::depart(R);
        hz::Eye e0 = app.eyeAt(s0, kPi, 0.0, std::tan(37.5 * kPi / 180.0));
        e0.k = std::exp2(10.0) * 3e-3;
        hz::EyeSum sum0;
        app.renderEye(e0, sum0);
        bool earthBall = false;
        for (int i = 0; i < app.view.nBalls; ++i) earthBall = earthBall || app.view.balls[i].pattern == 1;
        const double ev0 = app.eyeEv(sum0.adaptVisible);
        std::printf("    kalkista arkaya: Dunya top %d, goz EV %.2f (gece goku %.2f)\n", int(earthBall), ev0, app.eyeEv(app.nightSky));
        check(earthBall && ev0 < 2.0, "duraklar: kalkista arkada Dunya (fotogercekci top), goz gunduze uyar");
    }

    std::printf("hiz: %s (%d kalan)\n", fails == 0 ? "GECTI" : "KALDI", fails);
    return fails == 0 ? 0 : 1;
}

// the headless frames of the second option
int runHizFrames(OlcekApp& app, const std::vector<double>& phis, int leg, double frac, double fovDeg, double yaw, double pitch,
                 const std::vector<double>& sides, const std::string& out) {
    std::filesystem::create_directories(std::filesystem::path(app.root) / out);
    const hz::Route& R = app.route;
    std::printf("hiz kareleri: gece goku %.3e W/m^2 (gorunur, Gunes'in tayfinin wattlari)\n", app.nightSky);
    for (double phi : phis) {
        hz::Ship s = hz::depart(R);
        s.leg = std::clamp(leg, 0, 1);
        s.x = hz::dd(-R.length[s.leg] * (1.0 - frac));
        s.phi = std::clamp(phi, 0.0, hz::phiCap());
        // (--set hiz.test.fren=1) on the stopping curve: as far from the target as it takes to stop from that speed
        if (app.cfg.f("hiz.test.fren", 0.0f) > 0.5f) s.x = hz::dd(-std::min(hz::stopDistance(s.phi), R.length[s.leg]));
        s.sense = 1;
        s.heading = R.dir[s.leg];
        const V3 pos = hz::position(s, R);
        const hz::Kin k = hz::kin(s.phi);
        if (sides.empty()) {
            hz::Eye e = app.eyeAt(s, yaw, pitch, std::tan(0.5 * fovDeg * kPi / 180.0));
            e.energy = app.cfg.f("hiz.enerji", 1.0f) > 0.5f;
            // the world's clock: the automatic arrival's at Proxima (the leg's end), otherwise the start's
            if (s.leg == 0 && frac >= 1.0) e.worldT = 2.0 * hz::driveEarthTime(hz::peakFrom(0.0, R.length[0]));
            hz::EyeSum sum;
            e.k = std::exp2(app.eyeEv(0.0)) * 3e-3;
            app.renderEye(e, sum);   // a dry run for the sky's light: the adapted exposure
            const double ev = e.energy ? app.eyeEvEnergy(sum.adaptEnergy) : app.eyeEv(sum.adaptVisible);
            e.k = std::exp2(ev) * 3e-3;
            app.renderEye(e, sum);
            char name[96];
            std::snprintf(name, sizeof(name), "goz_ayak%d_phi%06.3f_gorus%g%s", s.leg, s.phi, fovDeg, e.energy ? "_enerji" : "");
            app.save(app.root + "/" + out + "/" + name);
            std::printf("phi %7.3f (%5.2f dokuz, gamma %.3e)  EV %.2f  gokyuzu %.3e (Gunes %.2e, butun bantlar %.3e)  gorus alaninda %d yildiz  "
                        "D %.3e .. %.3e  gun %.2f  kare %.2f ms (ana %.2f)\n",
                        s.phi, k.nines, k.gamma, ev, sum.visible, sum.visibleSun, sum.bolometric, sum.inView, sum.minD, sum.maxD,
                        app.eyeDay(e.energy ? sum.adaptEnergy : sum.adaptVisible, e.energy), app.frameMs, app.hostMs);
        } else {
            for (double sv : sides) {
                Scale& sc = app.sc;
                sc.hiz.on = true;
                sc.hiz.rocket = pos;
                sc.hiz.motion = s.heading;
                sc.hiz.rate = k.gamma;
                sc.hiz.shutterMove = hz::kC * k.gb * hz::kEyeShutter;
                {   // the scene's framing: at the Local Group's scale the leg's middle (olcek_live.cpp)
                    const int Lg = std::min(s.leg, 1);
                    const double u = std::clamp((sv - 21.6) / 0.8, 0.0, 1.0);
                    sc.hiz.focus = R.dir[Lg] * ((-s.x.value() - 0.5 * R.length[Lg]) * u * u * (3.0 - 2.0 * u));
                }
                sc.hiz.ev = app.sideEv(sv, pos);
                sc.hiz.mark = make_float3(1.0f, 0.93f, 0.86f) * 30.0f;
                sc.s = sv;
                sc.hizClocks(s.t.value());
                app.render();
                if (std::getenv("OLCEK_DEBUG")) {   // what the frame drew (the view's primitives)
                    const OlcekView& v = app.view;
                    std::printf("    balls %d, blobs %d, atmo %d, web %d, galaxies %d %d %d, ground %d, dens %d, field %d, particles %zu\n",
                                v.nBalls, v.nBlobs, v.atmo.on, v.web.on, v.galaxy[0].on, v.galaxy[1].on, v.galaxy[2].on, v.ground.on,
                                v.dens.on, v.field.on, app.sc.particles.size());
                    for (int i = 0; i < v.nBalls; ++i)
                        std::printf("      ball %d: c %.4g %.4g %.4g r %.4g h %.4g weight %.3f emit %.3g\n", i, v.balls[i].c.x, v.balls[i].c.y, v.balls[i].c.z,
                                    v.balls[i].r, v.balls[i].h, v.balls[i].weight, v.balls[i].emit.x);
                    for (int i = 0; i < 3; ++i)   // the galaxies' centres in camera units (x right, y up, z ahead; z = 1 at the focus)
                        if (v.galaxy[i].on)
                            std::printf("      galaxy %d: c %.4g %.4g %.4g, kpc %.4g\n", i, v.galaxy[i].c.x, v.galaxy[i].c.y, v.galaxy[i].c.z, v.galaxy[i].kpc);
                    double fmax = 0.0;
                    for (const GSplat& g : app.splats) fmax = std::max(fmax, double(g.flux.x + g.flux.y + g.flux.z));
                    std::printf("      splats %zu, the brightest %.3g\n", app.splats.size(), fmax);
                }
                char name[96];
                std::snprintf(name, sizeof(name), "yan_ayak%d_yer%.3f_s%+06.2f", s.leg, frac, sv);
                app.save(app.root + "/" + out + "/" + name);
                std::printf("yan s %+6.2f  ev %.2f  kare %.2f ms  %zu nokta\n", sv, sc.hiz.ev, app.frameMs, app.splats.size());
            }
        }
    }
    return 0;
}
