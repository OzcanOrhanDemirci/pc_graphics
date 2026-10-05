// Scene 7 "Ölçek", second option "Hız": the rocket's physics (see olcek_hiz.h).
#include "scenes/olcek/olcek_hiz.h"
#include "scenes/olcek/olcek_bugun.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace olcek::hiz {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
// a leg's target is reached only by stopping on it: the automatic arrival lands exactly (x = 0); a hand on the brake
// never does, and is never snapped there (no teleport: PLAN §1)
constexpr double kArriveTol = 1e-3;
// stopped by hand within this share of the leg from its target, the automatic arrival takes over the last stretch
constexpr double kCapture = 1e-3;

V3 icrsDir(double raDeg, double decDeg) {
    const double ra = raDeg * kDeg, dec = decDeg * kDeg;
    return {std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec)};
}

double smoother(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return u * u * u * (u * (6.0 * u - 15.0) + 10.0);
}

// a turn at rest: ~8 s for the 153 degrees at Proxima (PLAN §2), shorter turns quicker
double turnDuration(double angle) { return std::max(1.5, 8.0 * std::sqrt(angle / (153.0 * kDeg))); }

// from a to b along the great circle, u in 0..1; a half turn goes about the galaxy's north (any axis across would do)
V3 slerp(V3 a, V3 b, double u) {
    const double c = std::clamp(dot(a, b), -1.0, 1.0);
    const double ang = std::acos(c);
    if (ang < 1e-12) return b;
    V3 axis = cross(a, b);
    const double al = length(axis);
    if (al < 1e-9) {
        V3 g = v3(today::kGalNorth);
        g = g - a * dot(g, a);
        if (length(g) < 1e-6) g = std::fabs(a.x) < 0.9 ? v3(1, 0, 0) - a * a.x : v3(0, 1, 0) - a * a.y;
        axis = normalize(g);
    } else {
        axis = axis * (1.0 / al);
    }
    return normalize(rotate(a, axis, ang * u));
}

}   // namespace

// ---------------------------------------------------------------------------------------------------------------------

Kin kin(double phi) {
    Kin k;
    phi = std::max(phi, 0.0);
    k.phi = phi;
    const double e2 = std::exp(-2.0 * phi);
    k.eps = 2.0 * e2 / (1.0 + e2);   // 1 - tanh phi = 2 / (e^(2 phi) + 1)
    k.beta = std::tanh(phi);
    k.gamma = std::cosh(phi);
    k.gb = std::sinh(phi);
    k.nines = -std::log10(k.eps);
    return k;
}

double phiCap() {
    // gamma = 1 / sqrt((1 - beta)(1 + beta)) with 1 - beta = 1e-21 exactly
    static const double v = std::acosh(1.0 / std::sqrt(kEpsCap * (2.0 - kEpsCap)));
    return v;
}

double stopDistance(double phi) {
    const double s = std::sinh(0.5 * std::max(phi, 0.0));
    return 2.0 * (kC / kAlpha) * s * s;   // (c/alpha)(cosh phi - 1)
}

double peakFrom(double phi0, double r) {
    // sinh^2(phi*/2) = alpha r / (4c) + sinh^2(phi0/2) / 2 (the acosh form loses digits near 1: 8e-5 at r = 1 mm)
    const double sh = std::sinh(0.5 * std::max(phi0, 0.0));
    return 2.0 * std::asinh(std::sqrt(kAlpha * std::max(r, 0.0) / (4.0 * kC) + 0.5 * sh * sh));
}

double coshDiff(double phi0, double phi1) { return 2.0 * std::sinh(0.5 * (phi1 + phi0)) * std::sinh(0.5 * (phi1 - phi0)); }
double sinhDiff(double phi0, double phi1) { return 2.0 * std::cosh(0.5 * (phi1 + phi0)) * std::sinh(0.5 * (phi1 - phi0)); }

double aberrate(double theta, double phi) {
    return 2.0 * std::atan2(std::exp(-phi) * std::sin(0.5 * theta), std::cos(0.5 * theta));
}
double unaberrate(double thetaP, double phi) {
    return 2.0 * std::atan2(std::exp(phi) * std::sin(0.5 * thetaP), std::cos(0.5 * thetaP));
}
double dopplerRest(double theta, double phi) {
    // 1 + beta cos theta = (1 - beta) + 2 beta cos^2(theta/2): nothing cancels, also straight behind at 21 nines
    const Kin k = kin(phi);
    const double ch = std::cos(0.5 * theta);
    return k.gamma * (k.eps + 2.0 * k.beta * ch * ch);
}
double dopplerSeen(double thetaP, double phi) {
    // 1 - beta cos theta' = (1 - beta) + 2 beta sin^2(theta'/2)
    const Kin k = kin(phi);
    const double sh = std::sin(0.5 * thetaP);
    return 1.0 / (k.gamma * (k.eps + 2.0 * k.beta * sh * sh));
}

V3 aberrateDir(V3 n, V3 m, double phi, double& D) {
    if (phi <= 0.0) {
        D = 1.0;
        return n;
    }
    const double c = dot(n, m);
    const V3 perp = n - m * c;
    const double sp = length(perp);
    const double half = 0.5 * std::atan2(sp, c);   // theta / 2
    const double ch = std::cos(half), sh = std::sin(half);
    const Kin k = kin(phi);
    D = k.gamma * (k.eps + 2.0 * k.beta * ch * ch);
    const double thP = 2.0 * std::atan2(std::exp(-phi) * sh, ch);
    if (sp <= 0.0) return c >= 0.0 ? m : m * -1.0;   // straight ahead or behind: stays there
    const V3 e = perp * (1.0 / sp);
    return m * std::cos(thP) + e * std::sin(thP);
}

// ---------------------------------------------------------------------------------------------------------------------

Route makeRoute() {
    Route r;
    r.earth = v3(today::kPlanets[2].p);
    // Proxima Centauri today: Gaia DR3 5853498713190525696 (J2016.0: RA 217.39232147, Dec -62.67607512, parallax
    // 768.0665 mas, proper motion -3781.741 / +769.465 mas/yr, radial velocity -21.94 km/s) carried to 2026-10-05 10:00 UTC
    // with astropy: RA 217.36769529, Dec -62.67377279, 1.301729 pc = 4.2457 ly (it has come 50 AU nearer since 2016).
    // From then on the rest frame holds it still (its 32 km/s against the Sun: PLAN §2). The scene's catalogue (HYG,
    // Hipparcos 2007) has it at 1.2958 pc: that Proxima is moved here, so the star the rocket stops by is the one it sees.
    r.proxima = icrsDir(217.36769529, -62.67377279) * (1.301729 * kPc);
    // Andromeda's centre: the scene's own (olcek_world.cpp: 765 kpc, Riess, Fliri & Valls-Gabaud 2012), so that the model
    // the side view draws is where the rocket goes
    r.m31 = icrsDir(10.6847, 41.2690) * (765e3 * kPc);
    r.orbitB = 0.0486 * kAU;   // Proxima b: a = 0.04857 AU (Faria et al. 2022): it stops on that radius (the orbit: H5)
    r.park = 42164e3;          // the start: beside the Earth, at the geostationary radius from its centre
    r.before = 1e5 * kLy;      // the second leg ends 100,000 ly in front of Andromeda's centre: its disc ~70 deg wide
    const V3 u1 = normalize(r.proxima - r.earth);
    r.start[0] = r.earth + u1 * r.park;
    r.target[0] = r.proxima - u1 * r.orbitB;
    r.start[1] = r.target[0];
    const V3 u2 = normalize(r.m31 - r.start[1]);
    r.target[1] = r.m31 - u2 * r.before;
    for (int i = 0; i < 2; ++i) {
        const V3 d = r.target[i] - r.start[i];
        r.length[i] = length(d);
        r.dir[i] = d * (1.0 / r.length[i]);
    }
    r.turnDeg = std::acos(std::clamp(dot(r.dir[0], r.dir[1]), -1.0, 1.0)) / kDeg;
    const double along = std::clamp(-dot(r.start[1], r.dir[1]), 0.0, r.length[1]);
    r.sunPass = length(r.start[1] + r.dir[1] * along);
    return r;
}

const char* legName(int leg) {
    switch (leg) {
    case kLegProxima: return "Dünya → Proxima";
    case kLegAndromeda: return "Proxima → Andromeda";
    default: return "Andromeda";
    }
}

Ship depart(const Route& r) {
    Ship s;
    s.leg = kLegProxima;
    s.x = dd(-r.length[0]);
    s.heading = r.dir[0];
    s.turnFrom = s.turnTo = s.heading;
    return s;
}

V3 position(const Ship& s, const Route& r) {
    const int L = std::min(s.leg, 1);
    if (s.leg >= kLegDone) return r.target[1];
    // from whichever end is nearer: the far end's rounding (~ulp of 10^22 m) never shows next to the near one
    const DD fromStart = add(s.x, r.length[L]);
    if (fromStart.value() < 0.5 * r.length[L]) return r.start[L] + r.dir[L] * fromStart.value();
    return r.target[L] + r.dir[L] * s.x.value();
}

V3 motionDir(const Ship& s, const Route& r) {
    const int L = std::min(s.leg, 1);
    if (s.phi > 0.0) return r.dir[L] * double(s.sense);
    return r.dir[L] * (s.x.value() <= 0.0 ? 1.0 : -1.0);
}

bool approaching(const Ship& s) {
    if (s.phi <= 0.0) return true;
    const double x = s.x.value();
    return (x < 0.0 && s.sense > 0) || (x > 0.0 && s.sense < 0);
}

double remaining(const Ship& s) { return std::fabs(s.x.value()); }

// ---------------------------------------------------------------------------------------------------------------------

Events step(Ship& s, const Route& r, double dtau, const Command& c) {
    Events ev;
    const double cap = phiCap();
    const double cA = kC / kAlpha;   // c / alpha: the rest frame's metres per unit of cosh under the drive

    if (c.toggleAuto && s.leg < kLegDone) {
        s.autopilot = !s.autopilot;
        (s.autopilot ? ev.autoOn : ev.autoOff) = true;
    } else if (s.autopilot && (c.thrustPressed || c.brakePressed)) {   // a hand on W or S takes over
        s.autopilot = false;
        ev.autoCancelled = true;
    }
    // the keys only count by hand (the automatic arrival flies alone, whatever is held)
    const bool thrust = !s.autopilot && c.thrust && !c.brake, brake = !s.autopilot && c.brake;

    auto startTurn = [&](V3 to) {
        s.turnFrom = s.heading;
        s.turnTo = to;
        const double ang = std::acos(std::clamp(dot(s.heading, to), -1.0, 1.0));
        s.turnDur = turnDuration(ang);
        s.turnAt = s.tau;
        ev.turnStarted = true;
    };
    auto rest = [&](double h) {   // time flows at rest: the two clocks together
        s.tau += h;
        s.t = add(s.t, h);
    };
    auto arrive = [&]() {
        ev.arrived = true;
        ev.arrivedLeg = s.leg;
        if (s.legTau >= 0.0) {
            ev.legTau = s.tau - s.legTau;
            ev.legEarth = add(s.t, neg(s.legT)).value();
        }
        s.autopilot = false;
        s.phi = 0.0;
        if (s.leg == kLegProxima) {
            s.leg = kLegAndromeda;
            s.x = dd(-r.length[1]);
            s.sense = 1;
            s.legTau = -1.0;
            s.legT = dd(-1.0);
            startTurn(r.dir[1]);   // it turns towards Andromeda by itself
        } else {
            s.leg = kLegDone;
            s.x = dd(0.0);
        }
    };

    double left = std::max(dtau, 0.0);
    for (int iter = 0; iter < 64 && left > 0.0; ++iter) {
        if (s.leg >= kLegDone) {
            rest(left);
            left = 0.0;
            s.drive = Drive::Done;
            break;
        }
        const int L = s.leg;
        // a turn under way (at rest)
        if (s.turnAt >= 0.0) {
            const double end = s.turnAt + s.turnDur;
            const double h = std::min(left, std::max(end - s.tau, 0.0));
            rest(h);
            left -= h;
            if (s.tau >= end || h <= 0.0) {
                s.heading = s.turnTo;
                s.turnAt = -1.0;
            } else {
                s.heading = slerp(s.turnFrom, s.turnTo, smoother((s.tau - s.turnAt) / s.turnDur));
            }
            s.drive = Drive::Turn;
            continue;
        }
        // at rest
        if (s.phi <= 0.0) {
            s.phi = 0.0;
            if (std::fabs(s.x.value()) <= kArriveTol) {
                arrive();
                continue;
            }
            // stopped by hand close to the target: the last stretch by the automatic arrival, flown (never snapped)
            if (!s.autopilot && !c.thrust && !c.brake && std::fabs(s.x.value()) <= kCapture * r.length[L]) {
                s.autopilot = true;
                ev.captured = true;
            }
            const V3 want = r.dir[L] * (s.x.value() < 0.0 ? 1.0 : -1.0);
            const bool go = s.autopilot || thrust;
            if (!go) {
                rest(left);
                left = 0.0;
                s.drive = Drive::Rest;
                break;
            }
            if (dot(s.heading, want) < 1.0 - 1e-12) {   // facing elsewhere: turn first (no reverse gear)
                startTurn(want);
                continue;
            }
            s.sense = s.x.value() < 0.0 ? 1 : -1;
            s.heading = want;
        }

        // moving, or about to: which drive, and until when
        enum Mode { Thrust, Coast, Brake } mode = Coast;
        double phiTo = 0.0;
        bool pinned = false;
        double hCoast = left;
        const double rem = std::fabs(s.x.value());
        if (s.autopilot) {
            // the quickest arrival from wherever it is (one rule for every state): going away, stop first; then thrust
            // while the peak it can still stop from is higher (and under the cap), coast at the cap, brake on the
            // stopping curve. Already as fast as the peak: it only coasts, then brakes at the right moment (PLAN §2).
            if (!approaching(s)) {
                mode = Brake;
            } else {
                // within the rounding of rem - d (a few ulps of ~10^22 m): on the curve
                const double slack = rem - stopDistance(s.phi);
                const double tol = 1e-14 * rem + 1e-6;
                if (slack <= tol) {
                    mode = Brake;
                    pinned = slack >= -tol;   // on the stopping curve: it lands exactly; inside it: too fast, it passes
                    if (!pinned) ev.overshoot = true;
                } else {
                    const double phiTop = std::min(cap, peakFrom(s.phi, rem));
                    if (s.phi < phiTop - 1e-12) {
                        mode = Thrust;
                        phiTo = phiTop;
                    } else {
                        mode = Coast;   // until the remaining way is the stopping distance
                        hCoast = std::min(left, slack / (kC * std::sinh(s.phi)));
                    }
                }
            }
        } else if (brake) {
            mode = Brake;
        } else if (thrust && s.phi < cap) {
            mode = Thrust;
            phiTo = cap;
        }

        const double phi0 = s.phi;
        double phi1 = phi0, h = left, dT = 0.0, dX = 0.0;
        if (mode == Thrust) {
            const double hEv = (phiTo - phi0) / kAlpha;
            if (hEv <= left) {
                h = hEv;
                phi1 = phiTo;
            } else {
                phi1 = phi0 + kAlpha * h;
            }
            dT = sinhDiff(phi0, phi1) / kAlpha;
            dX = cA * coshDiff(phi0, phi1);
            if (s.legTau < 0.0) {
                s.legTau = s.tau;
                s.legT = s.t;
            }
            if (s.departT.hi < 0.0) s.departT = s.t;
            if (phi1 >= cap) ev.capReached = true;
        } else if (mode == Brake) {
            const double hEv = phi0 / kAlpha;
            if (hEv <= left) {
                h = hEv;
                phi1 = 0.0;
            } else {
                phi1 = phi0 - kAlpha * h;
            }
            dT = sinhDiff(phi1, phi0) / kAlpha;
            dX = cA * coshDiff(phi1, phi0);
        } else {
            if (phi0 <= 0.0) {   // nothing moves it
                rest(left);
                left = 0.0;
                s.drive = Drive::Rest;
                break;
            }
            h = hCoast;
            dT = std::cosh(phi0) * h;
            dX = kC * std::sinh(phi0) * h;
        }
        s.x = add(s.x, double(s.sense) * dX);
        if (pinned) {   // the brake that lands: on the stopping curve, so the stop is at the target exactly
            const DD on = dd(-double(s.sense) * stopDistance(phi1));
            ev.pinJump = std::max(ev.pinJump, std::fabs(add(s.x, neg(on)).value()));
            s.x = on;
        }
        s.t = add(s.t, dT);
        s.tau += h;
        s.phi = phi1;
        left -= h;
        if (s.phi > 0.0) s.heading = r.dir[L] * double(s.sense);
        s.drive = mode == Thrust ? Drive::Thrust : mode == Brake ? Drive::Brake : Drive::Coast;
        if (mode == Brake && s.phi <= 0.0) {
            s.phi = 0.0;
            if (std::fabs(s.x.value()) <= kArriveTol) arrive();
        }
    }
    return ev;
}

// ---------------------------------------------------------------------------------------------------------------------

std::string serialize(const Ship& s) {
    char b[1024];
    std::snprintf(b, sizeof(b),
                  "hiz.surum = 1\nhiz.ayak = %d\nhiz.phi = %.17g\nhiz.yon = %d\nhiz.x = %.17g %.17g\nhiz.tau = %.17g\nhiz.t = %.17g %.17g\n"
                  "hiz.burun = %.17g %.17g %.17g\nhiz.donus = %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n"
                  "hiz.oto = %d\nhiz.ayak_basi = %.17g %.17g %.17g\nhiz.kalkis = %.17g %.17g\n",
                  s.leg, s.phi, s.sense, s.x.hi, s.x.lo, s.tau, s.t.hi, s.t.lo, s.heading.x, s.heading.y, s.heading.z,
                  s.turnFrom.x, s.turnFrom.y, s.turnFrom.z, s.turnTo.x, s.turnTo.y, s.turnTo.z, s.turnAt, s.turnDur,
                  s.autopilot ? 1 : 0, s.legTau, s.legT.hi, s.legT.lo, s.departT.hi, s.departT.lo);
    return b;
}

bool parse(Ship& out, const std::map<std::string, std::string>& kv) {
    auto nums = [&](const char* key, int n, double* v) {
        const auto it = kv.find(key);
        if (it == kv.end()) return false;
        std::istringstream ss(it->second);
        for (int i = 0; i < n; ++i)
            if (!(ss >> v[i]) || !std::isfinite(v[i])) return false;
        return true;
    };
    Ship s;
    double a[8];
    if (!nums("hiz.surum", 1, a) || a[0] != 1.0) return false;
    if (!nums("hiz.ayak", 1, a) || a[0] < 0 || a[0] > 2) return false;
    s.leg = int(a[0]);
    if (!nums("hiz.phi", 1, a) || a[0] < 0.0 || a[0] > phiCap() + 1e-9) return false;
    s.phi = std::min(a[0], phiCap());
    if (!nums("hiz.yon", 1, a) || std::fabs(std::fabs(a[0]) - 1.0) > 0.0) return false;
    s.sense = int(a[0]);
    if (!nums("hiz.x", 2, a)) return false;
    s.x = {a[0], a[1]};
    if (!nums("hiz.tau", 1, a) || a[0] < 0.0) return false;
    s.tau = a[0];
    if (!nums("hiz.t", 2, a)) return false;
    s.t = {a[0], a[1]};
    if (!nums("hiz.burun", 3, a)) return false;
    s.heading = v3(a[0], a[1], a[2]);
    if (std::fabs(length(s.heading) - 1.0) > 1e-6) return false;
    s.heading = normalize(s.heading);
    if (!nums("hiz.donus", 8, a)) return false;
    s.turnFrom = v3(a[0], a[1], a[2]);
    s.turnTo = v3(a[3], a[4], a[5]);
    s.turnAt = a[6];
    s.turnDur = a[7];
    if (s.turnAt >= 0.0 && (std::fabs(length(s.turnFrom) - 1.0) > 1e-6 || std::fabs(length(s.turnTo) - 1.0) > 1e-6)) return false;
    if (!nums("hiz.oto", 1, a)) return false;
    s.autopilot = a[0] != 0.0;
    if (!nums("hiz.ayak_basi", 3, a)) return false;
    s.legTau = a[0];
    s.legT = {a[1], a[2]};
    if (!nums("hiz.kalkis", 2, a)) return false;
    s.departT = {a[0], a[1]};
    out = s;
    return true;
}

}   // namespace olcek::hiz
