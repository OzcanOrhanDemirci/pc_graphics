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
// a target is reached only by stopping on it: the automatic arrival lands exactly (x = 0); a hand on the brake never
// does, and is never snapped there (no teleport: PLAN §1)
constexpr double kArriveTol = 1e-3;
// stopped by hand within this share of a classic leg from the target, the automatic arrival takes over the last stretch
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
    k.nines = k.eps < 1.0 ? -std::log10(k.eps) : 0.0;   // (eps 1, up to phi ~3e-17: -log10(1) is -0, "-0,00" on the panel)
    return k;
}

double phiCap() {
    // gamma = 1 / sqrt((1 - beta)(1 + beta)) with 1 - beta = 1e-21 exactly
    static const double v = std::acosh(1.0 / std::sqrt(kEpsCap * (2.0 - kEpsCap)));
    return v;
}

double coshDiff(double phi0, double phi1) { return 2.0 * std::sinh(0.5 * (phi1 + phi0)) * std::sinh(0.5 * (phi1 - phi0)); }
double sinhDiff(double phi0, double phi1) { return 2.0 * std::cosh(0.5 * (phi1 + phi0)) * std::sinh(0.5 * (phi1 - phi0)); }

// ---------------------------------------------------------------------------------------------------------------------
// the drive

namespace {

// f(y), the proper seconds per tenfold of rapidity at y = log10 phi: a logistic from kFlo to kFhi around kYm, kW wide. The
// four numbers are fitted so that W from rest reaches 1% of c at 60 s, 10% at 120 s, 99% at 330 s and the cap at 480 s
// (Özcan's schedule). kappa = phi ln10 / f rises while d ln f / dy < ln 10: it peaks at 2.264 here (always rising).
constexpr double kFlo = 11.045826948166013, kFhi = 155.1844369732429, kYm = -1.2793262021112877, kW = 0.25562973259046673;
constexpr double kSliceStep = 1.01;   // each slice's rate 1% above the one below

double rateTarget(double phi) {
    const double k0 = kG0 / kC;
    if (phi <= 0.0) return k0;
    const double f = kFlo + (kFhi - kFlo) / (1.0 + std::exp(-(std::log10(phi) - kYm) / kW));
    return std::max(k0, phi * 2.302585092994045684 / f);
}

struct Slices {
    std::vector<double> phi, k;    // each slice's start and rate (phi[0] = 0: the 1 g floor)
    std::vector<double> T, C, D;   // at each slice's start: the proper time, the rest frame's time and way from rest
};

// where the target rate reaches `want` (it rises monotonically): bisection in log phi
double phiAtRate(double want, double lo, double hi) {
    double a = std::log(lo), b = std::log(hi);
    for (int i = 0; i < 200 && b - a > 1e-15 * std::fabs(b); ++i) {
        const double m = 0.5 * (a + b);
        (rateTarget(std::exp(m)) < want ? a : b) = m;
    }
    return std::exp(b);
}

const Slices& slices() {
    static const Slices S = [] {
        Slices s;
        const double k0 = kG0 / kC, top = phiCap() * 1.05;
        // the floor: 1 g until the rate the law asks for passes it
        s.phi.push_back(0.0);
        s.k.push_back(k0);
        double p = phiAtRate(k0 * 1.0000001, 1e-15, 1.0);
        while (true) {
            s.phi.push_back(p);
            if (p > top) break;
            const double r0 = rateTarget(p), r1 = r0 * kSliceStep;
            const double q = phiAtRate(r1, p, top * 2.0);
            s.k.push_back(std::sqrt(r0 * r1));   // the slice's rate: the middle of its ends' (in ratio)
            p = q;
        }
        s.k.push_back(rateTarget(p));   // (the last slice, past the cap: never reached)
        const size_t n = s.phi.size();
        s.T.assign(n, 0.0);
        s.C.assign(n, 0.0);
        s.D.assign(n, 0.0);
        for (size_t i = 0; i + 1 < n; ++i) {
            s.T[i + 1] = s.T[i] + (s.phi[i + 1] - s.phi[i]) / s.k[i];
            s.C[i + 1] = s.C[i] + sinhDiff(s.phi[i], s.phi[i + 1]) / s.k[i];
            s.D[i + 1] = s.D[i] + kC / s.k[i] * coshDiff(s.phi[i], s.phi[i + 1]);
        }
        return s;
    }();
    return S;
}

// the slice holding phi: thrust from phi uses the one starting at or below it; brake from phi the one ending at or above it
size_t sliceUp(double phi) {
    const Slices& s = slices();
    const size_t i = size_t(std::upper_bound(s.phi.begin(), s.phi.end(), phi) - s.phi.begin());
    return i == 0 ? 0 : std::min(i - 1, s.phi.size() - 1);
}
size_t sliceDown(double phi) {
    const Slices& s = slices();
    const size_t i = size_t(std::lower_bound(s.phi.begin(), s.phi.end(), phi) - s.phi.begin());
    return i == 0 ? 0 : i - 1;
}

}   // namespace

double driveRate(double phi) {
    const Slices& s = slices();
    return s.k[sliceUp(std::max(phi, 0.0))];
}

double driveTime(double phi) {
    phi = std::max(phi, 0.0);
    const Slices& s = slices();
    const size_t i = sliceUp(phi);
    return s.T[i] + (phi - s.phi[i]) / s.k[i];
}

double driveEarthTime(double phi) {
    phi = std::max(phi, 0.0);
    const Slices& s = slices();
    const size_t i = sliceUp(phi);
    return s.C[i] + sinhDiff(s.phi[i], phi) / s.k[i];
}

double stopDistance(double phi) {
    phi = std::max(phi, 0.0);
    const Slices& s = slices();
    const size_t i = sliceUp(phi);
    return s.D[i] + kC / s.k[i] * coshDiff(s.phi[i], phi);
}

double peakFrom(double phi0, double r) {
    // the way from rest to phi*: D(phi*) = (r + D(phi0)) / 2. Inverted in its slice: (c/k)(cosh phi - cosh phi_i) = d - D_i,
    // as sinh^2(phi/2) = sinh^2(phi_i/2) + k (d - D_i) / (2c) (the acosh form loses its digits near rest)
    const Slices& s = slices();
    const double d = 0.5 * (std::max(r, 0.0) + stopDistance(phi0));
    size_t i = size_t(std::upper_bound(s.D.begin(), s.D.end(), d) - s.D.begin());
    i = i == 0 ? 0 : i - 1;
    const double sh = std::sinh(0.5 * s.phi[i]);
    return 2.0 * std::asinh(std::sqrt(sh * sh + s.k[i] * (d - s.D[i]) / (2.0 * kC)));
}

DriveStep driveThrust(double phi0, double h, double phiTo) {
    const Slices& s = slices();
    DriveStep st;
    double phi = std::max(phi0, 0.0), left = std::max(h, 0.0);
    for (int guard = 0; guard < 100000 && phi < phiTo; ++guard) {
        const size_t i = sliceUp(phi);
        const double end = i + 1 < s.phi.size() ? std::min(s.phi[i + 1], phiTo) : phiTo;
        const double need = (end - phi) / s.k[i];
        double next = end, dt = need;
        if (need > left) {
            next = phi + s.k[i] * left;
            dt = left;
        }
        if (dt <= 0.0 && next <= phi) break;
        st.dT += sinhDiff(phi, next) / s.k[i];
        st.dX += kC / s.k[i] * coshDiff(phi, next);
        st.used += dt;
        left -= dt;
        phi = next;
        if (need > dt || left <= 0.0) break;
    }
    st.phi1 = phi;
    st.reached = phi >= phiTo;
    return st;
}

DriveStep driveBrake(double phi0, double h) {
    const Slices& s = slices();
    DriveStep st;
    double phi = std::max(phi0, 0.0), left = std::max(h, 0.0);
    for (int guard = 0; guard < 100000 && phi > 0.0; ++guard) {
        const size_t i = sliceDown(phi);
        const double end = s.phi[i];
        const double need = (phi - end) / s.k[i];
        double next = end, dt = need;
        if (need > left) {
            next = std::max(phi - s.k[i] * left, end);
            dt = left;
        }
        st.dT += sinhDiff(next, phi) / s.k[i];
        st.dX += kC / s.k[i] * coshDiff(next, phi);
        st.used += dt;
        left -= dt;
        phi = next;
        if (need > dt || left <= 0.0) break;
    }
    st.phi1 = std::max(phi, 0.0);
    st.reached = phi <= 0.0;
    return st;
}

void driveSlices(std::vector<double>& phi, std::vector<double>& kappa) {
    phi = slices().phi;
    kappa = slices().k;
}

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

const char* targetName(int target) { return target == kTargetAndromeda ? "Andromeda" : "Proxima Centauri"; }

namespace {
V3 centreOf(const Route& r, int target) { return target == kTargetAndromeda ? r.m31 : r.proxima; }
double standoff(const Route& r, int target) { return target == kTargetAndromeda ? r.before : r.orbitB; }
// how near a place a double can tell from the Sun's origin: ~10^-15 of its distance (at Andromeda's stop 2.4e22 m, a
// few 10^7 m), never under the arrival's millimetre
double resolution(V3 p) { return std::max(kArriveTol, 1e-15 * length(p)); }
}   // namespace

V3 stopPoint(const Route& r, int target, V3 from) {
    const V3 c = centreOf(r, target);
    const V3 d = c - from;
    const double n = length(d);
    // (from its centre itself: in front as seen from the Earth)
    const V3 u = n > 0.0 ? d * (1.0 / n) : normalize(c - r.earth);
    return c - u * standoff(r, target);
}

double captureRadius(const Route& r, int target) { return kCapture * r.length[target == kTargetAndromeda ? 1 : 0]; }

Course course(const Route& r, int target, V3 from) {
    Course c;
    c.from = from;
    c.to = stopPoint(r, target, from);
    c.target = target;
    const V3 d = c.to - c.from;   // (as makeRoute's legs: the classic ones come out the same, bit for bit)
    c.length = length(d);
    c.dir = c.length > 0.0 ? d * (1.0 / c.length) : normalize(centreOf(r, target) - r.earth);
    return c;
}

Course legCourse(const Route& r, int leg) {
    Course c;
    c.from = r.start[leg];
    c.to = r.target[leg];
    c.dir = r.dir[leg];
    c.length = r.length[leg];
    c.target = leg == kLegAndromeda ? kTargetAndromeda : kTargetProxima;
    return c;
}

Ship depart(const Route& r) {
    Ship s;
    s.target = kTargetProxima;
    s.course = legCourse(r, kLegProxima);
    s.x = dd(-r.length[0]);
    s.heading = r.dir[0];
    s.turnFrom = s.turnTo = s.heading;
    return s;
}

V3 position(const Ship& s) {
    const Course& c = s.course;
    // from whichever end is nearer: the far end's rounding (~ulp of 10^22 m) never shows next to the near one
    const DD fromStart = add(s.x, c.length);
    if (fromStart.value() < 0.5 * c.length) return c.from + c.dir * fromStart.value();
    return c.to + c.dir * s.x.value();
}

V3 motionDir(const Ship& s) { return s.course.dir; }

bool approaching(const Ship& s) { return s.phi <= 0.0 || s.x.value() < 0.0; }

double remaining(const Ship& s) { return std::fabs(s.x.value()); }

double toTarget(const Ship& s, const Route& r) {
    // along the course while it leads there with its end ahead; else straight from here (past the end and beyond the
    // target's centre, the stop is the far side's: where the rocket would set off to)
    if (s.course.target == s.target && s.x.value() <= 0.0) return -s.x.value();
    const V3 here = position(s);
    return length(stopPoint(r, s.target, here) - here);
}

double toTargetMeasured(const Ship& s, const Route& r) {
    const Kin k = kin(s.phi);
    if (s.course.target == s.target && s.x.value() <= 0.0) return -s.x.value() / k.gamma;
    const V3 here = position(s);
    const V3 d = stopPoint(r, s.target, here) - here;
    // (only the share along the motion is contracted)
    const double par = dot(d, s.course.dir), perp2 = std::max(dot(d, d) - par * par, 0.0);
    return std::sqrt(par * par / (k.gamma * k.gamma) + perp2);
}

bool atTarget(const Ship& s) { return s.at >= 0 && s.at == s.target && s.phi <= 0.0; }

// ---------------------------------------------------------------------------------------------------------------------

Events step(Ship& s, const Route& r, double dtau, const Command& c) {
    Events ev;
    const double cap = phiCap();

    auto startTurn = [&](V3 to) {
        s.turnFrom = s.heading;
        s.turnTo = to;
        const double ang = std::acos(std::clamp(dot(s.heading, to), -1.0, 1.0));
        s.turnDur = turnDuration(ang);
        s.turnAt = s.tau;
        ev.turnStarted = true;
        ev.turnDeg = ang / kDeg;
    };
    auto rest = [&](double h) {   // time flows at rest: the two clocks together
        s.tau += h;
        s.t = add(s.t, h);
    };
    // the course from here to the chosen target (at rest: it sets off there, or turns to it). One laid for another target
    // than the last is a new trip (its clocks from its first thrust). Back from a short way past the same target's stop
    // (still on its side of the target): along the same line reversed, to the same stop (its place not worked out again
    // from a rounded position: near Andromeda a double's step is ~10^6 m)
    auto layCourse = [&]() {
        if (s.course.target != s.target) {
            s.legTau = -1.0;
            s.legT = dd(-1.0);
        }
        const double past = s.x.value();
        if (s.course.target == s.target && past > 0.0 && past < standoff(r, s.target)) {
            s.course.dir = s.course.dir * -1.0;
            s.course.length = past;
            s.course.from = s.course.to - s.course.dir * past;
            s.x = neg(s.x);
            return;
        }
        s.course = course(r, s.target, position(s));
        s.x = dd(-s.course.length);
    };
    // the course's end where the rocket already is, within what a double can tell there: it has arrived (no turn, no
    // flight of a few rounding steps; nothing seen moves)
    auto within = [&]() { return std::fabs(s.x.value()) <= resolution(s.course.to); };
    // a turn under way at rest called off: back to where it began (or stopped where it is, if it had not moved)
    auto undoTurn = [&]() {
        if (s.turnAt < 0.0) return;
        if (dot(s.heading, s.turnFrom) < 1.0 - 1e-12) startTurn(s.turnFrom);
        else s.turnAt = -1.0;
    };
    auto arrive = [&]() {
        ev.arrived = true;
        ev.arrivedAt = s.course.target;
        if (s.legTau >= 0.0) {
            ev.legTau = s.tau - s.legTau;
            ev.legEarth = add(s.t, neg(s.legT)).value();
        }
        s.autopilot = false;
        s.handOver = false;
        s.phi = 0.0;
        s.at = s.course.target;
        s.legTau = -1.0;   // the next trip's clocks from its first thrust
        s.legT = dd(-1.0);
    };

    // another target chosen (the hand's claim gone). At rest the rocket lays its course there and turns to it at once (the
    // choice answered on the screen); moving, it keeps to its line (no turning under way: the automatic arrival stops
    // first). Choosing back the target it rests at: the turn towards the other undone, its own course again
    if (c.choose >= 0 && c.choose <= 1 && c.choose != s.target) {
        s.target = c.choose;
        ev.chosen = true;
        s.handOver = false;
        if (s.phi <= 0.0) {
            const V3 here = position(s);
            if (s.at == s.target) {
                undoTurn();
                s.course = Course{here, here, s.course.dir, 0.0, s.target};
                s.x = dd(0.0);
            } else {
                layCourse();
                if (within()) {   // already there
                    undoTurn();
                    s.x = dd(0.0);
                    s.at = s.target;
                } else if (dot(s.heading, s.course.dir) < 1.0 - 1e-12) {
                    startTurn(s.course.dir);   // (from where a turn under way has got to)
                } else {
                    s.turnAt = -1.0;           // already facing it: a turn under way towards the other is called off
                }
            }
        }
    }
    if (c.toggleAuto) {
        if (!s.autopilot && atTarget(s)) {
            ev.alreadyThere = true;   // nothing to fly
        } else {
            s.autopilot = !s.autopilot;
            (s.autopilot ? ev.autoOn : ev.autoOff) = true;
            s.handOver = !s.autopilot;
        }
    } else if (s.autopilot && (c.thrustPressed || c.brakePressed)) {   // a hand on W or S takes over
        s.autopilot = false;
        s.handOver = true;
        ev.autoCancelled = true;
    }
    if (c.thrustPressed && atTarget(s)) ev.alreadyThere = true;
    // the keys only count by hand (the automatic arrival flies alone, whatever is held)
    const bool thrust = !s.autopilot && c.thrust && !c.brake, brake = !s.autopilot && c.brake;

    double left = std::max(dtau, 0.0);
    for (int iter = 0; iter < 64 && left > 0.0; ++iter) {
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
            const bool onCourse = s.course.target == s.target;
            if (onCourse && s.at != s.target && std::fabs(s.x.value()) <= kArriveTol) {
                arrive();
                continue;
            }
            if (atTarget(s)) {   // resting at the chosen target: nothing to fly (another target is chosen on the panel)
                s.autopilot = false;
                rest(left);
                left = 0.0;
                s.drive = Drive::Rest;
                break;
            }
            // stopped by hand close to the target: the last stretch by the automatic arrival, flown (never snapped)
            const double toGo = toTarget(s, r), zone = captureRadius(r, s.target);
            if (toGo > zone) s.handOver = false;   // (outside the zone: forgotten)
            if (!s.autopilot && !s.handOver && !c.thrust && !c.brake && toGo <= zone) {
                s.autopilot = true;
                ev.captured = true;
            }
            const bool go = s.autopilot || thrust;
            if (!go) {
                rest(left);
                left = 0.0;
                s.drive = Drive::Rest;
                break;
            }
            // setting off: on a course to the chosen target from here (laid anew unless the one it rests on still leads
            // there, its end ahead), facing it (no reverse gear: it turns first)
            if (!onCourse || s.x.value() > -kArriveTol) layCourse();
            if (within()) {
                s.x = dd(0.0);
                arrive();
                continue;
            }
            if (dot(s.heading, s.course.dir) < 1.0 - 1e-12) {
                startTurn(s.course.dir);
                continue;
            }
            s.heading = s.course.dir;
        }

        // moving, or about to: which drive, and until when
        enum Mode { Thrust, Coast, Brake } mode = Coast;
        double phiTo = 0.0;
        bool pinned = false;
        double hCoast = left;
        const double rem = std::fabs(s.x.value());
        if (s.autopilot) {
            // the quickest arrival from wherever it is (one rule for every state): going away, or along a line laid for
            // another target, stop first (then it turns and sets off from there); then thrust while the peak it can
            // still stop from is higher (and under the cap), coast at the cap, brake on the stopping curve. Already as
            // fast as the peak: it only coasts, then brakes at the right moment (PLAN §2).
            if (s.course.target != s.target || !approaching(s)) {
                mode = Brake;
                ev.stopFirst = true;
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
            const DriveStep d = driveThrust(phi0, left, phiTo);
            h = d.used;
            phi1 = d.reached ? phiTo : d.phi1;
            dT = d.dT;
            dX = d.dX;
            if (s.legTau < 0.0) {
                s.legTau = s.tau;
                s.legT = s.t;
            }
            if (s.departT.hi < 0.0) s.departT = s.t;
            if (!s.autopilot) s.handOver = false;   // the hand thrusting again: it may be captured later
            s.at = -1;                              // (it has left the stop it rested at)
            if (phi1 >= cap) ev.capReached = true;
        } else if (mode == Brake) {
            const DriveStep d = driveBrake(phi0, left);
            h = d.used;
            phi1 = d.reached ? 0.0 : d.phi1;
            dT = d.dT;
            dX = d.dX;
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
        s.x = add(s.x, dX);
        if (pinned) {   // the brake that lands: on the stopping curve, so the stop is at the target exactly
            const DD on = dd(-stopDistance(phi1));
            ev.pinJump = std::max(ev.pinJump, std::fabs(add(s.x, neg(on)).value()));
            s.x = on;
        }
        s.t = add(s.t, dT);
        s.tau += h;
        s.phi = phi1;
        left -= h;
        if (s.phi > 0.0) s.heading = s.course.dir;
        s.drive = mode == Thrust ? Drive::Thrust : mode == Brake ? Drive::Brake : Drive::Coast;
        if (mode == Brake && s.phi <= 0.0) {
            s.phi = 0.0;
            if (s.course.target == s.target && std::fabs(s.x.value()) <= kArriveTol) arrive();
        }
    }
    return ev;
}

// ---------------------------------------------------------------------------------------------------------------------

std::string serialize(const Ship& s) {
    const Course& c = s.course;
    char b[2048];
    std::snprintf(b, sizeof(b),
                  "hiz.surum = %d\nhiz.hedef = %d\nhiz.rota = %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %d\n"
                  "hiz.phi = %.17g\nhiz.x = %.17g %.17g\nhiz.tau = %.17g\nhiz.t = %.17g %.17g\n"
                  "hiz.burun = %.17g %.17g %.17g\nhiz.donus = %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n"
                  "hiz.oto = %d %d\nhiz.ayak_basi = %.17g %.17g %.17g\nhiz.kalkis = %.17g %.17g\nhiz.varis = %d\n",
                  kSaveVersion, s.target, c.from.x, c.from.y, c.from.z, c.to.x, c.to.y, c.to.z, c.dir.x, c.dir.y, c.dir.z, c.length,
                  c.target, s.phi, s.x.hi, s.x.lo, s.tau, s.t.hi, s.t.lo, s.heading.x, s.heading.y, s.heading.z, s.turnFrom.x,
                  s.turnFrom.y, s.turnFrom.z, s.turnTo.x, s.turnTo.y, s.turnTo.z, s.turnAt, s.turnDur, s.autopilot ? 1 : 0,
                  s.handOver ? 1 : 0, s.legTau, s.legT.hi, s.legT.lo, s.departT.hi, s.departT.lo, s.at);
    return b;
}

bool parse(Ship& out, const std::map<std::string, std::string>& kv, const Route& r) {
    auto nums = [&](const char* key, int n, double* v) {
        const auto it = kv.find(key);
        if (it == kv.end()) return false;
        std::istringstream ss(it->second);
        for (int i = 0; i < n; ++i)
            if (!(ss >> v[i]) || !std::isfinite(v[i])) return false;
        return true;
    };
    Ship s;
    double a[11];
    // version 2: the drive of H2 (1 g at rest, always rising; 2026-10-06). A journey flown under the old drive (0.4 c/s)
    // is not continued: its times and places came from another schedule. Version 3: the chosen target and the course
    if (!nums("hiz.surum", 1, a) || (a[0] != 2.0 && a[0] != double(kSaveVersion))) return false;
    const bool v2 = a[0] == 2.0;
    if (!nums("hiz.phi", 1, a) || a[0] < 0.0 || a[0] > phiCap() + 1e-9) return false;
    s.phi = std::min(a[0], phiCap());
    if (!nums("hiz.x", 2, a)) return false;
    s.x = {a[0], a[1]};
    if (v2) {
        // the two legs' journey carried over as it was: its place, speed and way. At rest, or moving along its leg: the
        // leg itself (Andromeda's leg begins at Proxima's stop). Moving back against it: the line it flies, to the stop
        // on the side it comes from when that lies ahead (back from beyond Proxima: its far side), else past the one
        // behind (going away from it)
        if (!nums("hiz.ayak", 1, a) || a[0] < 0 || a[0] > 2) return false;
        const int leg = int(a[0]);
        if (!nums("hiz.yon", 1, a) || std::fabs(std::fabs(a[0]) - 1.0) > 0.0) return false;
        const int sense = int(a[0]);
        s.target = leg == 0 ? kTargetProxima : kTargetAndromeda;
        s.course = legCourse(r, std::min(leg, 1));
        if (leg >= 2) {   // at Andromeda
            s.x = dd(0.0);
            s.phi = 0.0;
            s.at = kTargetAndromeda;
        } else if (s.phi > 0.0 && sense < 0) {
            const Course L = s.course;
            const V3 p = position(s);
            const V3 m = L.dir * -1.0;
            Course c = course(r, s.target, p);
            const double toOld = dot(L.to - p, m);   // its old stop: ahead along the way it goes when > 0
            if (dot(c.dir, m) > 0.0 && stopDistance(s.phi) <= c.length) {
                s.course = c;
                s.x = dd(-c.length);
            } else if (toOld > 0.0) {
                // too fast for that nearer stop (saved during the old brake on to its own): on to the old stop, as it flew
                c.to = L.to;
                c.dir = m;
                c.length = toOld;
                c.from = L.to - m * toOld;
                s.course = c;
                s.x = dd(-toOld);
            } else {
                c.dir = m;
                c.from = c.to - m * c.length;
                s.course = c;
                s.x = dd(dot(p - c.to, m));
            }
        }
    } else {
        if (!nums("hiz.hedef", 1, a) || (a[0] != 0.0 && a[0] != 1.0)) return false;
        s.target = int(a[0]);
        if (!nums("hiz.rota", 11, a) || a[9] < 0.0 || (a[10] != 0.0 && a[10] != 1.0)) return false;
        s.course.from = v3(a[0], a[1], a[2]);
        s.course.to = v3(a[3], a[4], a[5]);
        s.course.dir = v3(a[6], a[7], a[8]);
        s.course.length = a[9];
        s.course.target = int(a[10]);
        if (std::fabs(length(s.course.dir) - 1.0) > 1e-6) return false;
        if (!nums("hiz.varis", 1, a) || a[0] < -1.0 || a[0] > 1.0) return false;
        s.at = int(a[0]);
    }
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
    if (!nums("hiz.oto", 2, a)) return false;
    s.autopilot = a[0] != 0.0;
    s.handOver = a[1] != 0.0;
    if (!nums("hiz.ayak_basi", 3, a)) return false;
    s.legTau = a[0];
    s.legT = {a[1], a[2]};
    if (!nums("hiz.kalkis", 2, a)) return false;
    s.departT = {a[0], a[1]};
    out = s;
    return true;
}

}   // namespace olcek::hiz
