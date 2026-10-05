// Scene 7 "Ölçek": the scale engine. One number drives the scene: s = log10 W, W the screen's width in metres at the
// thing looked at, from the Planck length (s = -34.8) to the observable universe (s = 27.25). The camera only moves
// away or closer along one fixed direction (Izmir's zenith at "today", PLAN §3): what the wheel changes is s.
//
// Precision (PLAN §7): there is no absolute position. Everything hangs on a tree of anchors (the Laniakea basin, the
// Local Group, the Galaxy's centre, the Sun, the Earth, the beach point, the atom under it, its nucleus, a proton, a
// quark), each an offset from its parent in metres (doubles). The offset between two anchors is summed up to their
// lowest common ancestor only, so two neighbours deep down (a proton and its quark, 0.35 fm apart) are as exact as
// two neighbours far out; only the ~5 decades on the screen are ever close in scale, and doubles hold 16.
//
// Time (PLAN §4): it flows at R(s) seconds a second (W^0.68 above a metre, W^1.24 below), one rule, many clocks: each
// layer keeps its own phase so that the numbers stay local. What repeats or lives faster than the eye's shutter is
// drawn as its time average (a Gaussian shutter: a body becomes a ring, a spinning Earth bands, a vibrating lattice a
// blur), smoothly. Going home, the Earth's turn and its place on its orbit are drawn back to "today" while zooming in,
// so the beach is always reached at the same noon.
#pragma once
#include <cstdint>
#include "core/config.h"
#include "scenes/olcek/olcek_types.h"

#include <cmath>
#include <string>
#include <vector>

namespace olcek {

struct V3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
inline V3 v3(double x, double y, double z) { return V3{x, y, z}; }
inline V3 v3(const double* a) { return V3{a[0], a[1], a[2]}; }
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator-(V3 a) { return {-a.x, -a.y, -a.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator*(double s, V3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline V3& operator+=(V3& a, V3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) { return a * (1.0 / length(a)); }
inline float3 f3(V3 a) { return make_float3(float(a.x), float(a.y), float(a.z)); }
V3 rotate(V3 v, V3 axis, double angle);   // Rodrigues, axis unit

// the anchors (PLAN §3: the centre's way out and in), and the bodies that hang on them
enum Node : int {
    kLaniakea,     // the root: the Laniakea basin's centre (towards the Norma cluster)
    kLocalGroup,   // its barycentre
    kGalCentre,    // Sgr A*
    kSun,
    kEarth,
    kBeachToday,   // the beach point where it is "today" (fixed to the Earth's centre, not turning): the way in
    kBeach,        // the beach point where it is now (turns with the Earth; hung on kBeachToday, 0 at today's turn)
    kNucleus,      // the home silicon atom's nucleus, vibrating about the beach point (the atom's mean place)
    kProton,
    kQuark,
    kMoon,
    kMercury, kVenus, kMars, kJupiter, kSaturn, kUranus, kNeptune,
    kM31, kM33, kLMC, kSMC,
    kRocket,       // the second option "Hız": the rocket, hung on the Sun (its place from olcek_hiz.h's route)
    kNodeCount
};

struct Tree {
    int parent[kNodeCount];
    int depth[kNodeCount];
    V3 off[kNodeCount];   // from the parent, metres
    void link(int n, int p);
    V3 rel(int from, int to) const;   // to - from, summed to their common ancestor only
};

// a Kepler orbit from a position and a velocity at "today" (two bodies; enough for the eye over any span of time)
struct Kepler {
    double mu = 0.0, a = 0.0, e = 0.0, n = 0.0, M0 = 0.0, period = 0.0;
    V3 P, Q;              // the orbit's plane: towards the periapsis, and 90 degrees on
    void init(V3 r, V3 v, double mu);
    V3 at(double t) const;   // position t seconds after "today"
};

// the clocks (PLAN §4): one rule R(s), many local numbers
struct Clocks {
    double spin = 0.0;            // the Earth's turn, radians away from today's (wrapped to -pi..pi)
    double year = 0.0;            // the Earth's orbit, seconds away from today (wrapped to half a year)
    double moon = 0.0;            // seconds since today, wrapped to the Moon's period
    double planets[8] = {};       // the same for each planet (index as today::kPlanets)
    double galaxy = 0.0;          // the Sun around the Galaxy's centre, radians
    double arms = 0.0;            // the arms' pattern, radians
    double vib[3] = {};           // the lattice's vibration, radians (three modes)
    double foam[128] = {};        // the foam's octaves, each its own phase (wraps at 256)
    double string[4] = {};        // the string's modes, radians
    double beach = 0.0;           // the swash, seconds (wraps)
};

struct Body;   // olcek_world.cpp

// one frame's camera
struct Camera {
    int ref = kBeach;             // the anchor the camera is kept against
    V3 pos;                       // from ref, metres
    V3 right, up, fwd;            // fixed (ICRS): east, north, down at the beach "today"
    double D = 1.0;               // distance to the focus, metres
    double W = 1.0;               // the screen's width there
};

class Scale {
public:
    // settings
    int width = 2560, height = 1440;
    double fovX = 40.0;           // degrees, across
    double sMin = -34.79, sMax = 27.25;
    double shutter = 0.15;        // the eye's Gaussian shutter, seconds of screen time (PLAN §4: faster than ~0.5 s: averaged)
    double shutterSpin = 1.0 / 60.0;   // the Earth's turn: a frame's motion blur only (its continents stay readable)
    std::vector<double> evS, evV; // the exposure's curve: EV at s (PLAN §5: designed)

    double s = 0.0;               // where we are
    Clocks clk;
    Tree tree;
    Camera cam;

    // The second option "Hız" (docs/olcek/hiz/PLAN.md, olcek_hiz.h): the side view follows the rocket. The camera looks at
    // it from its side (the motion is the screen's right), the wheel is the same, the world's clocks are the rest frame's
    // time (the scene sets them: hizClocks), the exposure is the scene's, the beach and the home are off.
    struct Hiz {
        bool on = false;
        V3 rocket;                // from the Sun (m)
        V3 motion{1, 0, 0};       // unit: the screen's right
        double rate = 1.0;        // the world's seconds per screen second (gamma): what the shutter averages
        double gamma = 1.0;       // the measured world: lengths along the motion gamma times shorter about the rocket
        double ev = 23.5;         // the exposure (EV against the sunlit world's)
        float3 mark{0, 0, 0};     // the rocket's point (display-referred light, after the exposure); 0: none
    } hiz;
    void hizClocks(double t);     // every clock at the rest frame's time t (s since today): planets, Moon, spin, Galaxy
    void hizOff();                // back to the first option: the fixed camera, today's clocks
    // the catalogue's stars (from the Sun), their luminosity and colour, their temperature
    const std::vector<V3>& stars() const { return starPos_; }
    const std::vector<float4>& starLight() const { return starFlux_; }
    const std::vector<float>& starTemp() const { return starTemp_; }
    int nearestStar(V3 p) const;  // the index of the catalogue star nearest to p (from the Sun), -1 none
    void moveStar(int i, V3 p);   // a better measured place (Proxima: Gaia DR3)

    void configure(const Config& cfg);
    void init();                  // the catalogue, the tree, today's orbits
    // the real stars (tools/olcek_veri.py yildiz: HYG v4.1) in place of the placeholder field; false: kept the field
    bool loadStars(const std::string& path, std::string& log);
    // the real galaxies (tools/olcek_veri.py galaksi: 2MRS) in place of the placeholder web where the survey reaches
    bool loadGalaxies(const std::string& path, std::string& log);
    // the belts (tools/olcek_veri.py asteroit: MPCORB): the main and Kuiper belts as rings, Jupiter's Trojan swarms
    bool loadBelts(const std::string& path, std::string& log);
    struct BeltRing {
        double r0 = 0.0, dr = 0.0;            // AU
        std::vector<float> sigma, zrms;       // per AU^2; AU
    };
    BeltRing beltMain, beltKuiper;
    double trojanN[2] = {}, trojanLon[2] = {}, trojanSig[2] = {};   // L4, L5: count, degrees from Jupiter
    // time: dt seconds of screen time at scale s (sPrev: where the last frame was; zooming in pulls the home back)
    void advance(double dt, double sPrev);
    void place();                 // the tree's offsets from the clocks
    void camera();                // the camera at s (the focus's way between the anchors)
    void build(OlcekView& v, std::vector<GSplat>& splats);   // the frame's primitives
    std::vector<GParticle> particles;   // below the light: the frame's particles (build fills them, far to near)
    // the test's hold on them (olcek-sure): each tracked particle's identity, its light, its place (pixels)
    struct PartTrack {
        uint64_t id;
        float light, x, y;
    };
    std::vector<PartTrack> particleTrack;

    // what is in the middle (the panel), its name in the language in use; the screen's width in words
    std::string lookingAt() const;
    std::string widthText() const;
    static double rate(double s);   // R(s): seconds of the world a second of the screen
    double ev(double s) const;
    // the home's place on the screen (x across, y up, in screen widths from the middle; z > 0 in front): the test's
    V3 onScreen(int node, V3 local = {}) const;
    V3 focus() const;             // the focus, from cam.ref (metres)

    // tests
    double precisionError(int node, V3 local) const;   // pixels: float camera space vs a double-double reference

private:
    std::vector<Body*> bodies_;
    std::vector<V3> starPos_;     // the stars' field around the Sun (from the Sun, metres)
    bool realStars_ = false;      // the catalogue is in starPos_ (loadStars)
    std::vector<float4> starFlux_;   // luminosity (W) and colour
    std::vector<float> starTemp_;    // temperature (K): the eye's Doppler shifts it (olcek_hiz_goz.h)
    std::vector<V3> galPos_;      // galaxies (from the Laniakea centre)
    std::vector<V3> surveyPos_;   // the survey's (from the Sun), kept across init(); the placeholder web stays beyond it
    std::vector<float4> surveyFlux_;
    V3 lanFromSun_;
    void mergeSurvey();
public:
    bool volumeWeb_ = false;      // the volume web is loaded: no placeholder points beyond the survey
private:
    std::vector<float4> galFlux_;
    Kepler moonOrbit_, planetOrbit_[8];
    V3 earthPole_, earthBeach_, earthUp_, earthNorth_, earthEast_;   // today
    V3 sunFromGC_, galNorth_, galX_;
    V3 protonOff_, quarkOff_;
    V3 vibAmp_[3];
    double vibOmega_[3] = {};
    double focusBlend(int& a, int& b) const;   // the centre's way: the anchors between which it is, and how far
    double worldRate() const { return hiz.on ? hiz.rate : rate(s); }   // the world's seconds per screen second
    // the second option's side view: rel (from the camera) where the rocket measures it, contracted along the motion
    // about the rocket (the camera is straight across from it: the same as about the camera); others: rel
    V3 measured(V3 rel) const {
        if (!hiz.on || hiz.gamma <= 1.0) return rel;
        const double a = dot(rel, cam.right);
        return rel + cam.right * (a / hiz.gamma - a);
    }
    // how far a body moves against the Sun if its clocks ran tau seconds more (the shutter's samples), and its orbit's
    // period as it runs at this scale (the Earth's year is held near home: longer, or infinite)
    V3 sunShift(int node, double tau) const;
    double runPeriod(int orbit) const;
    void ballOrPoint(OlcekView& v, std::vector<GSplat>& splats, const Body& b, double k) const;
    void buildInner(OlcekView& v);   // olcek_inner.cpp: the particles below the light
    double foamClock_ = 0.0;         // the Planck foam's bubbles: cycles of a Planck time, wrapped
};

}   // namespace olcek
