// Scene 7 "Ölçek", second option "Hız": the physics tests and the headless frames (olcek_app.cpp's command line).
//
//   pc_graphics.exe --scene olcek --test olcek-hiz        the formulas, the journeys, the colours, the save file
//   pc_graphics.exe --scene olcek --headless --hiz --phi 0,1,3,6 [--ayak 0] [--yer 0.5] [--gorus 75] [--bakis yaw,pitch]
//                   [--yan 1.3,7,12] [--out captures/olcek_hiz]
//       the rocket's eye at those rapidities (or the side view at those scales), half way along the leg (--yer: the
//       fraction of the leg behind it), at rest when phi = 0
#include "scenes/olcek/olcek_app.h"
#include "scenes/olcek/olcek_bugun.h"

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

// ---- the closed forms against a fine Runge-Kutta integration of dphi/dtau = +-alpha, dt/dtau = cosh, dx/dtau = c sinh
struct Integ { double dt, dx; };
Integ rk4(double phi0, double sign, double tau, int n) {
    // the state (phi, t, x); phi is linear in tau, so the integrand is known exactly at each stage
    double t = 0.0, x = 0.0;
    const double h = tau / n;
    for (int i = 0; i < n; ++i) {
        const double p0 = phi0 + sign * hz::kAlpha * h * i;
        const double pm = p0 + sign * hz::kAlpha * 0.5 * h, p1 = p0 + sign * hz::kAlpha * h;
        t += h / 6.0 * (std::cosh(p0) + 4.0 * std::cosh(pm) + std::cosh(p1));
        x += h / 6.0 * hz::kC * (std::sinh(p0) + 4.0 * std::sinh(pm) + std::sinh(p1));
    }
    return {t, x};
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

    // ---- 1. the closed forms against integration
    {
        double worst = 0.0;
        for (double phi0 : {0.0, 0.5, 3.0, 12.0, 20.0, 23.0})
            for (double tau : {1.0 / 60.0, 0.7, 5.0}) {
                // thrust
                const double p1 = phi0 + hz::kAlpha * tau;
                const Integ a = rk4(phi0, 1.0, tau, 20000);
                worst = std::max(worst, std::fabs(hz::sinhDiff(phi0, p1) / hz::kAlpha - a.dt) / a.dt);
                worst = std::max(worst, std::fabs(kC / hz::kAlpha * hz::coshDiff(phi0, p1) - a.dx) / a.dx);
                // brake (from phi0 + alpha tau down to phi0)
                const Integ b = rk4(p1, -1.0, tau, 20000);
                worst = std::max(worst, std::fabs(hz::sinhDiff(phi0, p1) / hz::kAlpha - b.dt) / b.dt);
                worst = std::max(worst, std::fabs(kC / hz::kAlpha * hz::coshDiff(phi0, p1) - b.dx) / b.dx);
            }
        std::printf("  kapali bicimler / Runge-Kutta (20.000 adim): en buyuk goreli fark %.2e\n", worst);
        check(worst < 1e-10, "itis ve fren: dt = (sinh f1 - sinh f0)/alfa, dx = (c/alfa)(cosh f1 - cosh f0)");
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
            const double phiStar = std::acosh(1.0 + hz::kAlpha * L / (2.0 * kC));
            if (phiStar <= cap) {
                tau = 2.0 * phiStar / hz::kAlpha;
                earth = 2.0 * std::sinh(phiStar) / hz::kAlpha;
            } else {
                const double coast = (L - 2.0 * hz::stopDistance(cap)) / (kC * std::sinh(cap));
                tau = 2.0 * cap / hz::kAlpha + coast;
                earth = 2.0 * std::sinh(cap) / hz::kAlpha + coast * std::cosh(cap);
            }
        };
        double it[2], ie[2];
        ideal(R.length[0], it[0], ie[0]);
        ideal(R.length[1], it[1], ie[1]);
        std::printf("    kusursuz surus (formulden): Proxima %.6f s / %.6f yil; Andromeda %.6f dk / %.6f milyon yil\n", it[0], ie[0] / hz::kYear,
                    it[1] / 60.0, ie[1] / hz::kYear * 1e-6);
        check(std::fabs(it[0] - 89.0) < 0.05 && std::fabs(ie[0] / hz::kYear - 4.25) < 0.01, "Proxima: 89,0 s, Dunya'da 4,25 yil (PLAN §2)");
        check(std::fabs(it[1] / 60.0 - 58.3) < 0.05 && std::fabs(ie[1] / hz::kYear * 1e-6 - 2.395) < 0.001, "Andromeda: 58,3 dk, Dunya'da 2,395 milyon yil (PLAN §2)");
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
            const Journey J = fly(R, c.dt, 4000.0);
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
        for (int i = 0; i < 60 * 46; ++i) hz::step(s, r, 1.0 / 60.0, w);   // 46 s of W: past the middle (44.5 s), not the target
        const bool past = hz::stopDistance(s.phi) > hz::remaining(s);
        hz::Command sp;
        sp.toggleAuto = true;
        hz::Events ev = hz::step(s, r, 0.0, sp);
        bool overshoot = ev.overshoot, turned = false, arrived = false;
        double tau0 = s.tau;
        for (int i = 0; i < 60 * 600 && !arrived; ++i) {
            ev = hz::step(s, r, 1.0 / 60.0, hz::Command{});
            overshoot = overshoot || ev.overshoot;
            turned = turned || ev.turnStarted;
            arrived = ev.arrived;
        }
        std::printf("    elle: 46 s itis (durma mesafesi > kalan: %s), Bosluk: gecti %s, dondu %s, vardi %s (%.1f s sonra)\n", past ? "evet" : "hayir",
                    overshoot ? "evet" : "hayir", turned ? "evet" : "hayir", arrived ? "evet" : "hayir", s.tau - tau0);
        check(past && overshoot && turned && arrived && s.leg == hz::kLegAndromeda, "hedefi gecince: durur, doner, geri gelir, tam hedefte durur");

        hz::Ship c = hz::depart(r);
        for (int i = 0; i < 60 * 100; ++i) hz::step(c, r, 1.0 / 60.0, w);   // 100 s of W from rest: the cap at 61.3 s
        std::printf("    100 s W: phi %.15f (tavan %.15f), %s\n", c.phi, cap, c.drive == hz::Drive::Coast ? "suzuluyor" : "itiyor");
        check(c.phi == cap && c.drive == hz::Drive::Coast, "W tavanda durur: 21 dokuzun otesi yok");
        hz::Command b;
        b.brake = true;
        for (int i = 0; i < 60 * 70; ++i) hz::step(c, r, 1.0 / 60.0, b);   // braking from the cap: 61.3 s
        check(c.phi == 0.0 && c.drive == hz::Drive::Rest, "S durur ve geri gitmez");

        // slow, then Space: the quickest arrival from there (not centuries of coasting at the speed it had)
        hz::Ship q = hz::depart(r);
        hz::Command tap;
        tap.thrust = tap.thrustPressed = true;
        hz::step(q, r, 1.0 / 60.0, tap);
        const double tau0q = q.tau;
        hz::step(q, r, 0.0, sp);
        bool arrivedQ = false;
        for (int i = 0; i < 60 * 200 && !arrivedQ; ++i) arrivedQ = hz::step(q, r, 1.0 / 60.0, hz::Command{}).arrived;
        std::printf("    bir kare W, sonra Bosluk: %s, %.3f s'de (en hizlisi 88,98 s)\n", arrivedQ ? "vardi" : "varmadi", q.tau - tau0q + 1.0 / 60.0);
        check(arrivedQ && q.tau - tau0q < 90.0, "yavasken Bosluk: en kisa yoldan (hizlanir), yuzyillarca suzulmez");

        // stopped by hand close to the target: the automatic arrival flies the last stretch (and nothing jumps)
        hz::Ship n = hz::depart(r);
        n.x = hz::dd(-3e12);   // 20 AU short, at rest
        bool captured = false, arrivedN = false;
        double jump = 0.0;
        for (int i = 0; i < 60 * 120 && !arrivedN; ++i) {
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
        s.sense = 1;
        s.heading = R.dir[s.leg];
        const V3 pos = hz::position(s, R);
        const hz::Kin k = hz::kin(s.phi);
        if (sides.empty()) {
            hz::Eye e = app.eyeAt(s, yaw, pitch, std::tan(0.5 * fovDeg * kPi / 180.0));
            hz::EyeSum sum;
            e.k = std::exp2(app.eyeEv(0.0)) * 3e-3;
            app.renderEye(e, sum);   // a dry run for the sky's light: the adapted exposure
            const double ev = app.eyeEv(sum.visibleView / sum.viewShare);
            e.k = std::exp2(ev) * 3e-3;
            app.renderEye(e, sum);
            char name[96];
            std::snprintf(name, sizeof(name), "goz_ayak%d_phi%06.3f_gorus%g", s.leg, s.phi, fovDeg);
            app.save(app.root + "/" + out + "/" + name);
            std::printf("phi %7.3f (%5.2f dokuz, gamma %.3e)  EV %.2f  gokyuzu %.3e (Gunes %.2e)  gorus alaninda %d yildiz  D %.3e .. %.3e  "
                        "kare %.2f ms (ana %.2f)\n",
                        s.phi, k.nines, k.gamma, ev, sum.visible, sum.visibleSun, sum.inView, sum.minD, sum.maxD, app.frameMs, app.hostMs);
        } else {
            for (double sv : sides) {
                Scale& sc = app.sc;
                sc.hiz.on = true;
                sc.hiz.rocket = pos;
                sc.hiz.motion = s.heading;
                sc.hiz.rate = k.gamma;
                sc.hiz.gamma = k.gamma;
                sc.hiz.ev = app.sideEv(sv, pos);
                sc.hiz.mark = make_float3(1.0f, 0.93f, 0.86f) * 30.0f;
                sc.s = sv;
                sc.hizClocks(s.t.value());
                app.render();
                if (std::getenv("OLCEK_DEBUG")) {   // what the frame drew (the view's primitives)
                    const OlcekView& v = app.view;
                    std::printf("    stretch %.3g, balls %d, blobs %d, atmo %d, web %d, galaxies %d %d %d, ground %d, dens %d, field %d, particles %zu\n",
                                v.stretchX, v.nBalls, v.nBlobs, v.atmo.on, v.web.on, v.galaxy[0].on, v.galaxy[1].on, v.galaxy[2].on, v.ground.on,
                                v.dens.on, v.field.on, app.sc.particles.size());
                    for (int i = 0; i < v.nBalls; ++i)
                        std::printf("      ball %d: c %.4g %.4g %.4g r %.4g h %.4g weight %.3f emit %.3g\n", i, v.balls[i].c.x, v.balls[i].c.y, v.balls[i].c.z,
                                    v.balls[i].r, v.balls[i].h, v.balls[i].weight, v.balls[i].emit.x);
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
