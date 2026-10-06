// Scene 7 "Ölçek", second option "Hız" (docs/olcek/hiz/PLAN.md): a rocket from the Earth's orbit to Proxima Centauri
// and on to Andromeda, under special relativity with its full formulas. The screen's clock is the passenger's: a second
// of the screen is a second of the rocket's proper time, nothing is sped up.
//
// The physics is exact, not integrated: the drive's proper acceleration is constant within thin slices of rapidity (see
// the drive below), and under a constant proper acceleration a the rapidity phi grows linearly with the proper time
// (dphi/dtau = a/c) and the rest frame's time and distance over a step have closed forms. Every number is
// formed from the rapidity so that the "nines" survive: 1 - beta = 2 / (e^(2 phi) + 1), never 1 - tanh phi (a double
// rounds 0.999...9 with 21 nines to 1). Events inside a frame (the cap reached, the moment to brake, the stop) are found
// exactly and the frame is split there.
//
// The rest frame is the Sun's (the Sun, Proxima and Andromeda move against each other at hundreds of km/s: neglected,
// PLAN §2). Each leg is a straight line between real positions; the rocket only ever moves along it (no lateral motion):
// its place is a double-double distance from the leg's target, exact to far below a metre at 10^22 m.
#pragma once
#include "scenes/olcek/olcek_world.h"   // V3

#include <map>
#include <string>
#include <vector>

namespace olcek::hiz {

constexpr double kC = 299792458.0;
constexpr double kG0 = 9.80665;         // standard gravity (m/s^2): the drive starts at 1 g
constexpr double kEyeShutter = 1.0 / 60.0;   // the eye's shutter in the side view (the passenger's seconds): what moves
                                             // in it is drawn along its way (PLAN §3.2)
constexpr double kEpsCap = 1e-21;       // 1 - beta at the cap: "21 nines" (PLAN §1, Özcan's choice)
constexpr double kLy = 9.4607304725808e15;
constexpr double kAU = 1.495978707e11;
constexpr double kPc = 3.0856775814913673e16;
constexpr double kYear = 365.25 * 86400.0;   // the Julian year
// Planck's law, from the SI's exact h, c and k: B_lambda = c1 / lambda^5 / (e^(c2 / lambda T) - 1)
constexpr double kPlanckC1 = 2.0 * 6.62607015e-34 * kC * kC;          // 2 h c^2 (W m^2 / sr)
constexpr double kPlanckC2 = 6.62607015e-34 * kC / 1.380649e-23;      // h c / k (m K)
constexpr double kStefan = 5.670374419184429e-8;                      // sigma (W / m^2 K^4)

// ---- double-double: a value as an unevaluated sum hi + lo (|lo| <= ulp(hi)/2); ~32 digits
struct DD {
    double hi = 0.0, lo = 0.0;
    double value() const { return hi + lo; }
};
inline DD twoSum(double a, double b) {
    const double s = a + b, bb = s - a;
    return {s, (a - (s - bb)) + (b - bb)};
}
inline DD quickTwoSum(double a, double b) {   // |a| >= |b|
    const double s = a + b;
    return {s, b - (s - a)};
}
inline DD add(DD a, double b) {
    DD s = twoSum(a.hi, b);
    s.lo += a.lo;
    return quickTwoSum(s.hi, s.lo);
}
inline DD add(DD a, DD b) {
    DD s = twoSum(a.hi, b.hi);
    s.lo += a.lo + b.lo;
    return quickTwoSum(s.hi, s.lo);
}
inline DD neg(DD a) { return {-a.hi, -a.lo}; }
inline DD dd(double v) { return {v, 0.0}; }

// ---- kinematics from the rapidity (phi >= 0)
struct Kin {
    double phi = 0.0;
    double beta = 0.0;    // tanh phi (for display: its nines are in eps)
    double eps = 1.0;     // 1 - beta, exact at any phi
    double gamma = 1.0;   // cosh phi
    double gb = 0.0;      // gamma beta = sinh phi (the proper velocity over c)
    double nines = 0.0;   // -log10(1 - beta): how many nines beta has
};
Kin kin(double phi);
double phiCap();                       // acosh of gamma at 1 - beta = 1e-21: 24.5237...
double coshDiff(double phi0, double phi1);   // cosh phi1 - cosh phi0, without cancellation
double sinhDiff(double phi0, double phi1);   // sinh phi1 - sinh phi0, without cancellation

// ---- the drive (PLAN §2; Özcan 2026-10-06: "başta insani ölçekte çok yavaş hızlanalım ve hızlandıkça hızlanma şiddeti
// artsın", "%1'e 1 dk, %10'a 1 dk daha, %99'a 3-4 dk, 21 dokuza birkaç dk"). The proper acceleration is a function of the
// rapidity alone: 1 g at rest, always rising. Under W from rest: 1% of c at 60 s, 10% at 120 s, 99% at 330 s, 21 nines
// at 480 s (300 km/h at 7.5 s, the escape velocity at 31 s). dphi/dtau = kappa(phi) = max(g/c, phi ln10 / f(log10 phi)),
// f the proper seconds per tenfold of rapidity, a logistic from 11 s to 155 s fitted to those four moments. It is held
// constant in thin slices (each 1% above the one below: not felt), so within a slice the constant acceleration's closed
// forms hold and the physics stays exact at every frame. Braking is the same law backwards (the same power): the brake
// from phi takes the time and the way the thrust to phi took.
struct DriveStep {
    double phi1 = 0.0;     // where the rapidity got to
    double used = 0.0;     // the proper time used (s)
    double dT = 0.0;       // the rest frame's time passed (s)
    double dX = 0.0;       // the rest frame's distance covered (m)
    bool reached = false;  // phiTo (thrust) or rest (brake) reached within the time given
};
double driveRate(double phi);       // kappa(phi) (1/s): the proper acceleration over c; times c / kG0 for g
double driveTime(double phi);       // the proper time from rest to phi under the drive (s)
double driveEarthTime(double phi);  // ... the rest frame's time it takes (s)
double stopDistance(double phi);    // the rest frame's way from rest to phi under the drive: also the way to stop from phi
// the highest rapidity worth reaching from phi0 with r to go and still stop on the target (time-optimal: thrust to it,
// then brake): the way to it and back down, D(phi*) - D(phi0) + D(phi*), is r
double peakFrom(double phi0, double r);
DriveStep driveThrust(double phi0, double h, double phiTo);   // thrust from phi0 for h seconds at most, up to phiTo
DriveStep driveBrake(double phi0, double h);                  // brake from phi0 for h seconds at most, down to rest
// the slices themselves (the tests and the independent check): their starts and rates; the last runs past the cap
void driveSlices(std::vector<double>& phi, std::vector<double>& kappa);

// ---- aberration and Doppler. theta: in the rest frame, the angle between the motion and the direction TO the source;
// thetaP: the same angle as the rocket sees it. Both from their half-angle tangents (stable from 0 to pi).
double aberrate(double theta, double phi);     // tan(theta'/2) = e^-phi tan(theta/2): the sky gathers ahead
double unaberrate(double thetaP, double phi);  // tan(theta/2) = e^phi tan(theta'/2)
double dopplerRest(double theta, double phi);  // D = nu'/nu = gamma (1 + beta cos theta)
double dopplerSeen(double thetaP, double phi); // D = 1 / (gamma (1 - beta cos theta'))
// the same from a direction: n (unit, rest frame, towards the source), m (unit, the motion). Returns the direction as
// the rocket sees it (unit, rest-frame axes) and D. At phi = 0: n and 1.
V3 aberrateDir(V3 n, V3 m, double phi, double& D);
// ... without trigonometry, for every star of every frame: the half-angle tangent t = tan(theta/2) from the direction's
// parts along and across the motion (each form where it does not cancel), t' = e^-phi t, and back;
// D = gamma (eps + 2 beta cos^2(theta/2)) with cos^2(theta/2) = 1 / (1 + t^2). k = kin(phi), emp = e^-phi.
inline V3 aberrateDirFast(V3 n, V3 m, const Kin& k, double emp, double& D) {
    const double c = dot(n, m);
    const V3 perp = n - m * c;
    const double sp = length(perp);
    if (sp <= 0.0) {
        D = c >= 0.0 ? k.gamma * (k.eps + 2.0 * k.beta) : k.gamma * k.eps;
        return c >= 0.0 ? m : m * -1.0;
    }
    const double t = c >= 0.0 ? sp / (1.0 + c) : (1.0 - c) / sp;
    D = k.gamma * (k.eps + 2.0 * k.beta / (1.0 + t * t));
    const double tp = emp * t;
    const double q = 1.0 / (1.0 + tp * tp);
    return m * ((1.0 - tp * tp) * q) + perp * (2.0 * tp * q / sp);
}

// ---- the route (PLAN §1-§3): real positions from the Sun, ICRS axes, metres
enum Leg : int { kLegProxima = 0, kLegAndromeda = 1, kLegDone = 2 };
struct Route {
    V3 earth;          // the Earth "today" (olcek_bugun.h)
    V3 proxima;        // Proxima Centauri (Gaia DR3)
    V3 m31;            // Andromeda's centre (the scene's own: olcek_world.cpp)
    double orbitB = 0.0;          // Proxima b's semi-major axis (m): the rocket stops on its radius (the orbit itself: H5)
    double park = 0.0;            // the start's height above the Earth's centre (m)
    double before = 0.0;          // how far before Andromeda's centre the second leg ends (m)
    V3 start[2], target[2], dir[2];   // each leg: from, to, the unit vector between them
    double length[2] = {};
    double turnDeg = 0.0;         // the angle between the legs (at Proxima)
    double sunPass = 0.0;         // leg 2's closest approach to the Sun (m)
};
Route makeRoute();
const char* legName(int leg);     // "Dünya → Proxima" etc. (Turkish; the scene picks its language)

// ---- the rocket
enum class Drive : int { Rest, Thrust, Coast, Brake, Turn, Done };
struct Ship {
    int leg = kLegProxima;
    double phi = 0.0;       // rapidity, >= 0, along sense * dir[leg]
    int sense = 1;          // +1: moving along the leg's dir (towards its target while before it); -1: against it
    DD x;                   // along the leg's line, from its target (m): < 0 before it, > 0 past it
    double tau = 0.0;       // the passenger's proper time since departure (s)
    DD t;                   // the rest frame's time since "today" (s): the Earth's clock
    V3 heading{0, 0, 1};    // the nose (unit, rest frame): the motion's way while moving; turned at rest
    // a turn at rest (the next leg at Proxima, or back after passing a target): from, to, start (proper time), length
    V3 turnFrom{0, 0, 1}, turnTo{0, 0, 1};
    double turnAt = -1.0, turnDur = 0.0;
    bool autopilot = false; // Space: automatic arrival, the quickest from wherever it is (thrust while the peak it can
                            // still stop from is ahead, coast at the cap, brake on the stopping curve)
    bool handOver = false;  // the hand took over near the target (S, or Space off): no capturing it back until the hand
                            // thrusts again, or the ship leaves the capture zone, or a leg is reached
    Drive drive = Drive::Rest;   // what the last step did (the panel)
    double legTau = -1.0;   // the leg's first thrust: proper time (s); < 0 not yet
    DD legT = {-1.0, 0.0};  // ... and the Earth's time then
    DD departT = {-1.0, 0.0};   // the rest frame's time at the journey's first thrust (< 0: still at the start): the
                                // world runs from "today" from then on (waiting beside the Earth, the rocket goes with it)
};
Ship depart(const Route& r);                   // at rest at the start of leg 1, facing Proxima
V3 position(const Ship& s, const Route& r);    // from the Sun (m); doubles (for the stars and the far world)
V3 motionDir(const Ship& s, const Route& r);   // sense * dir: the way it moves (or would, at rest: towards the target)
bool approaching(const Ship& s);               // moving towards the target (or at rest before/at it)
double remaining(const Ship& s);               // the distance to the leg's target (rest frame, m)

struct Command {
    bool thrust = false;    // W held
    bool brake = false;     // S held
    bool thrustPressed = false, brakePressed = false;   // ... pressed this frame: a hand on them takes over from the
                                                        // automatic arrival (held keys do not: Space pressed with W down)
    bool toggleAuto = false;   // Space pressed
};
// what happened in a step, for the scene's notices
struct Events {
    bool autoOn = false, autoOff = false, autoCancelled = false;
    bool arrived = false;   // a leg's target reached (at rest, exactly)
    int arrivedLeg = -1;
    double legTau = 0.0, legEarth = 0.0;   // ... the leg from its first thrust: the passenger's seconds, the Earth's
    bool capReached = false;
    bool turnStarted = false;
    double turnDeg = 0.0;   // ... by how much (the turn at Proxima is 153 degrees from the way in; after passing it, less)
    bool overshoot = false; // the autopilot found it cannot stop in time: it brakes, will pass and come back
    bool captured = false;  // stopped by hand near the target: the automatic arrival takes the last stretch (no snapping)
    double pinJump = 0.0;   // the largest correction made when the brake was pinned to the stopping curve (m; tests)
};
// advance the ship by dtau seconds of its proper time
Events step(Ship& s, const Route& r, double dtau, const Command& c);

// ---- persistence: "key = value" lines (doubles round-trip: %.17g)
constexpr int kSaveVersion = 2;   // the save's version (2: the drive of H2); parse refuses any other
std::string serialize(const Ship& s);
bool parse(Ship& s, const std::map<std::string, std::string>& kv);

}   // namespace olcek::hiz
