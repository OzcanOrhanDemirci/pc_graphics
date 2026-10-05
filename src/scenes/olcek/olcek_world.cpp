// Scene 7 "Ölçek": the scale engine (see olcek_world.h). Ö0: every body at its true size and in its true place, drawn
// as a placeholder (a sphere, a disc, a point, a cloud); the decades between them empty at their true length.
#include "scenes/olcek/olcek_world.h"
#include "app/lang.h"
#include "app/theme.h"
#include "scenes/olcek/olcek_bugun.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace olcek {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kC = 299792458.0;
constexpr double kAU = 1.495978707e11;
constexpr double kPc = 3.0856775814913673e16;
constexpr double kLy = 9.4607304725808e15;
constexpr double kLsun = 3.828e26;          // W
constexpr double kRsun = 6.957e8;
constexpr double kMuSun = 1.32712440018e20;
constexpr double kMuEarthMoon = 3.986004418e14 + 4.9028e12;
constexpr double kOmegaSun = 230e3 / 2.5061873317e20;   // rad/s: a flat rotation curve, 230 km/s at R0
constexpr double kOmegaArms = 25e3 / kPc * 1e-3 / 1.0;   // the arms' pattern: 25 km/s/kpc

// the bottom (Ö5): the Planck length; the string, a loop of radius ~ it (theory gives an order, not a number: chosen so
// the bottom of the zoom frames it), its modes n = 2..5 (fractions of its radius; a closed string's n-th mode turns at
// n c / r); the foam's displacement per octave, in Planck lengths (Wheeler: every length uncertain by ~ it)
constexpr double kPlanckLength = 1.616255e-35;
constexpr double kLoop = 0.32 * kPlanckLength;
constexpr int kModes = 4;
constexpr double kModeAmp[kModes] = {0.16, 0.10, 0.07, 0.05};
constexpr double kFoamAmp = 0.4;
constexpr double kFoamCell = 0.5;   // a Planck-sized cell's own shift, in Planck lengths

// the density map's fill: log10 of the density (particles per a0^3) at its dark and light ends, by scale (decreasing s).
// Set from what the screen holds (OLCEK_DEBUG): between the atoms -2.3, the atom's edge -0.9 at -10, its L shell 1.0
// at -10.5, the K shell 2.0-3.2 from -11 to -13; the nucleus 13.4; the proton on it 14.0; a quark 16.4. Into the atom
// the light end leaves the electrons for the nucleus over half a decade (the eye's adapting): the cloud goes dim.
struct DensKey {
    double s, lo, hi;
};
constexpr DensKey kDensRange[] = {
    {-8.2, -2.5, 1.5},   {-9.5, -2.5, 1.5},   {-10.0, -1.2, 2.8},  {-10.5, 0.6, 3.4},   {-11.0, 1.6, 4.2},
    {-11.5, 2.6, 13.4},  {-13.5, 2.9, 13.6},  {-14.0, 11.0, 14.5}, {-14.5, 13.2, 16.5}, {-15.0, 13.6, 16.5},
    {-15.5, 14.6, 16.6}, {-16.0, 14.2, 19.0}, {-16.5, 14.0, 21.4}, {-17.5, 14.0, 25.6},
};

// where the camera leaves the Earth's turn (DURUM Ö1: at the plan's pace the globe turns ~1-3 times a second at 10^7-10^8
// and the shutter averaged it into bands like Jupiter's)
constexpr double kSpinLo = 7.0, kSpinHi = 8.6;

double smooth01(double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); }
double smoothstep(double a, double b, double x) { return smooth01((x - a) / (b - a)); }
double smoother(double x) { x = std::clamp(x, 0.0, 1.0); return x * x * x * (x * (6.0 * x - 15.0) + 10.0); }
double wrapPi(double a) { a = std::fmod(a + kPi, 2.0 * kPi); if (a < 0) a += 2.0 * kPi; return a - kPi; }
double wrapTo(double a, double p) { a = std::fmod(a, p); return a < 0 ? a + p : a; }

V3 icrs(double raDeg, double decDeg) {
    const double ra = raDeg * kDeg, dec = decDeg * kDeg;
    return {std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec)};
}

// a blackbody's colour in linear Rec.709, luminance 1 (three wavelengths: enough for star colours)
float3 blackbody(double T) {
    auto planck = [&](double nm) {
        const double l = nm * 1e-9;
        return 1.0 / (std::pow(l, 5.0) * (std::exp(1.4387769e-2 / (l * T)) - 1.0));
    };
    double r = planck(610.0), g = planck(550.0), b = planck(465.0);
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return make_float3(float(r / y), float(g / y), float(b / y));
}

struct Rng {
    unsigned long long s;
    explicit Rng(unsigned long long seed) : s(seed * 2654435761ull + 1442695040888963407ull) {}
    double u() {   // [0, 1)
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return double(s >> 11) * (1.0 / 9007199254740992.0);
    }
    double n() { const double a = std::max(u(), 1e-300), b = u(); return std::sqrt(-2.0 * std::log(a)) * std::cos(2.0 * kPi * b); }
};

// a double-double, for the precision test's reference
struct DD {
    double hi = 0.0, lo = 0.0;
};
DD ddAdd(DD a, double b) {
    const double s = a.hi + b, bb = s - a.hi, e = (a.hi - (s - bb)) + (b - bb);
    const double lo = a.lo + e, hi = s + lo;
    return {hi, lo - (hi - s)};
}
DD ddAdd(DD a, DD b) { return ddAdd(ddAdd(a, b.hi), b.lo); }
DD ddNeg(DD a) { return {-a.hi, -a.lo}; }
struct DV { DD x, y, z; };
DV dvAdd(DV a, V3 b) { return {ddAdd(a.x, b.x), ddAdd(a.y, b.y), ddAdd(a.z, b.z)}; }
DV dvAdd(DV a, DV b) { return {ddAdd(a.x, b.x), ddAdd(a.y, b.y), ddAdd(a.z, b.z)}; }
DV dvNeg(DV a) { return {ddNeg(a.x), ddNeg(a.y), ddNeg(a.z)}; }

// The centre's way (PLAN §3): below s0 at a, above s1 at b, between them a blend linear in W (the screen moves at an
// even pace). Lateral moves are at most ~0.3 of the screen, where the next anchor is (DURUM, Ö0).
struct Seg {
    double s0, s1;
    int a, b;
};
const Seg kSegs[] = {
    {-15.40, -14.90, kQuark, kProton},
    {-14.80, -14.20, kProton, kNucleus},
    {-11.00, -10.00, kNucleus, kBeach},
    {6.00, 7.30, kBeachToday, kEarth},
    {11.00, 11.90, kEarth, kSun},
    {20.20, 21.00, kSun, kGalCentre},
    {22.00, 23.00, kGalCentre, kLocalGroup},
    {24.60, 25.40, kLocalGroup, kLaniakea},
    {25.80, 26.40, kLaniakea, kSun},
};

// what is in the middle, by decades (PLAN §2's table)
struct Name {
    double s;   // from this s upwards (until the next)
    const char *tr, *en;
};
const Name kNames[] = {
    {-99.0, "Planck uzunluğu", "The Planck length"},
    {-34.0, "Köpük ve sicim (kuram)", "Foam and a string (theory)"},
    {-31.5, "Çöl: üç kuvvet yaklaşıyor", "The desert: three forces converge"},
    {-18.5, "Yukarı kuarkın alanı", "The up quark's field"},
    {-16.5, "Yukarı kuark", "An up quark"},
    {-15.5, "Proton", "A proton"},
    {-14.5, "Silisyum-28 çekirdeği", "A silicon-28 nucleus"},
    {-13.5, "Atomun içi", "Inside the atom"},
    {-12.5, "Silisyum atomu", "A silicon atom"},
    {-9.5, "Kuvars kristali", "The quartz crystal"},
    {-8.5, "Kristal basamakları", "Crystal steps"},
    {-7.5, "Kuvars yüzeyi", "The quartz surface"},
    {-5.5, "Kum tanesinin yüzeyi", "The grain's surface"},
    {-3.5, "Kum tanesi (kuvars)", "A grain of sand (quartz)"},
    {-2.5, "Kum", "Sand"},
    {-1.5, "Sahil", "The beach"},
    {2.5, "Çandarlı Körfezi", "The Bay of Çandarlı"},
    {4.5, "Ege", "The Aegean"},
    {6.5, "Dünya", "The Earth"},
    {8.5, "Dünya ve Ay", "The Earth and the Moon"},
    {10.9, "Dünya'nın yörüngesi", "The Earth's orbit"},   // the Sun comes to the middle only from 11 to 11.9
    {11.5, "Güneş sistemi", "The Solar System"},
    {14.5, "Oort bulutu", "The Oort cloud"},
    {16.5, "Güneş'in komşuları", "The Sun's neighbours"},
    {18.5, "Yerel Kabarcık", "The Local Bubble"},
    {19.5, "Orion kolu", "The Orion Arm"},
    {20.5, "Samanyolu", "The Milky Way"},
    {21.5, "Samanyolu ve uyduları", "The Milky Way and its satellites"},
    {22.5, "Yerel Grup", "The Local Group"},
    {23.5, "Laniakea üstkümesi", "The Laniakea Supercluster"},
    {24.5, "Kozmik ağ", "The cosmic web"},
    {26.5, "Gözlemlenebilir evren", "The observable universe"},
};

// the scale window of the inner world's "instruments" (PLAN §5: each scale seen by what sees it): a cloud much larger
// than the screen is a flat veil with no contrast at this scale, and fades (the inside of the atom is honestly empty)
double scaleWindow(double sigma, double W) { return 1.0 - smoothstep(1.5, 6.0, sigma / W); }

}   // namespace

V3 rotate(V3 v, V3 k, double a) {
    const double c = std::cos(a), s = std::sin(a);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1.0 - c));
}

// ---------------------------------------------------------------------------------------------------------------------

void Tree::link(int n, int p) {
    parent[n] = p;
    depth[n] = p < 0 ? 0 : depth[p] + 1;
}

V3 Tree::rel(int from, int to) const {
    V3 a, b;
    int x = from, y = to;
    while (depth[x] > depth[y]) { a += off[x]; x = parent[x]; }
    while (depth[y] > depth[x]) { b += off[y]; y = parent[y]; }
    while (x != y) {
        a += off[x];
        b += off[y];
        x = parent[x];
        y = parent[y];
    }
    return b - a;
}

void Kepler::init(V3 r, V3 v, double mu_) {
    mu = mu_;
    const double rl = length(r);
    const V3 h = cross(r, v);
    const V3 ev = cross(v, h) * (1.0 / mu) - r * (1.0 / rl);
    e = length(ev);
    a = 1.0 / (2.0 / rl - dot(v, v) / mu);
    n = std::sqrt(mu / (a * a * a));
    period = 2.0 * kPi / n;
    P = e > 1e-12 ? ev * (1.0 / e) : r * (1.0 / rl);
    Q = normalize(cross(normalize(h), P));
    const double cosE = (1.0 - rl / a) / e, sinE = dot(r, v) / (e * std::sqrt(mu * a));
    const double E0 = std::atan2(sinE, cosE);
    M0 = E0 - e * std::sin(E0);
}

V3 Kepler::at(double t) const {
    const double M = wrapPi(M0 + n * std::fmod(t, period));
    double E = e < 0.8 ? M : kPi;
    for (int i = 0; i < 30; ++i) {
        const double f = E - e * std::sin(E) - M, d = 1.0 - e * std::cos(E);
        const double dE = f / d;
        E -= dE;
        if (std::fabs(dE) < 1e-15) break;
    }
    return P * (a * (std::cos(E) - e)) + Q * (a * std::sqrt(1.0 - e * e) * std::sin(E));
}

// ---------------------------------------------------------------------------------------------------------------------

struct Body {
    int node;
    double radius;        // m
    float3 albedo;
    double glow = 0.0;    // W/m^2/sr (the Sun)
    float3 glowColor{1, 1, 1};
    int pattern = 0;
    int orbit = -1;       // its Kepler orbit: 0..7 a planet (today::kPlanets' order), 8 the Moon
};

void Scale::configure(const Config& cfg) {
    width = cfg.i("width", width);
    height = cfg.i("height", height);
    fovX = cfg.f("fov", float(fovX));
    shutter = cfg.f("shutter", float(shutter));
    shutterSpin = cfg.f("shutter.spin", float(shutterSpin));
    // the exposure's curve: "s:EV, s:EV, ..."
    evS.clear();
    evV.clear();
    auto it = cfg.values.find("ev");
    const std::string curve = it != cfg.values.end() ? it->second
        : "-35:0, 8.3:0, 9.5:6, 10.5:11, 11.5:16, 12.5:21, 13.5:23.5, 22.6:23.5, 24:28, 25:33.5, 25.6:36, 27.3:36";
    std::stringstream ss(curve);
    std::string part;
    while (std::getline(ss, part, ',')) {
        const size_t c = part.find(':');
        if (c == std::string::npos) continue;
        evS.push_back(std::stod(part.substr(0, c)));
        evV.push_back(std::stod(part.substr(c + 1)));
    }
    // the top: the observable universe fits the screen's height with a margin
    const double tx = std::tan(0.5 * fovX * kDeg), ty = tx * height / width;
    const double D = 4.40e26 / std::sin(0.9 * std::atan(ty));
    sMax = std::log10(2.0 * D * tx);
    sMin = std::log10(kPlanckLength);
}

double Scale::ev(double s) const {
    // the exposure (EV) relative to the sunlit world's, piecewise smooth
    if (evS.empty()) return 0.0;
    if (s <= evS.front()) return evV.front();
    for (size_t i = 1; i < evS.size(); ++i)
        if (s <= evS[i]) return evV[i - 1] + (evV[i] - evV[i - 1]) * smoothstep(evS[i - 1], evS[i], s);
    return evV.back();
}

double Scale::rate(double s) { return std::pow(10.0, s >= 0.0 ? 0.68 * s : 1.24 * s); }

void Scale::init() {
    // ---- the Earth today
    earthPole_ = v3(today::kEarthAxes[0][2], today::kEarthAxes[1][2], today::kEarthAxes[2][2]);
    earthBeach_ = v3(today::kBeach);
    earthUp_ = v3(today::kUp);
    earthNorth_ = v3(today::kNorth);
    earthEast_ = v3(today::kEast);
    // the camera: fixed among the stars, looking down at the beach today (PLAN §3), north up, east right
    cam.fwd = -earthUp_;
    cam.up = earthNorth_;
    cam.right = earthEast_;
    // ---- orbits from today's positions and velocities
    for (int i = 0; i < 8; ++i) planetOrbit_[i].init(v3(today::kPlanets[i].p), v3(today::kPlanets[i].v), kMuSun);
    moonOrbit_.init(v3(today::kMoonFromEarth), v3(today::kMoonVelFromEarth), kMuEarthMoon);
    // ---- the Galaxy, the Local Group, the Laniakea basin (from the Sun; Ö3 replaces these with catalogues)
    galNorth_ = v3(today::kGalNorth);
    galX_ = v3(today::kGalX);
    const V3 gc = v3(today::kGalCentreDir) * today::kGalR0;   // the centre from the Sun
    sunFromGC_ = -gc;
    const V3 m31 = icrs(10.6847, 41.2690) * (765e3 * kPc);   // McConnachie 2012 order of distances
    const V3 m33 = icrs(23.4621, 30.6599) * (840e3 * kPc);
    const V3 lmc = icrs(80.8917, -69.7561) * (49.6e3 * kPc);
    const V3 smc = icrs(13.1867, -72.8286) * (62.4e3 * kPc);
    const V3 lg = gc + (m31 - gc) * 0.55;                     // the barycentre, a little nearer M31 (the heavier)
    // the Laniakea basin's centre: towards the Norma cluster (l = 325.3, b = -7.3), 68 Mpc
    const V3 galY = cross(galNorth_, galX_);
    const double l = 325.3 * kDeg, b = -7.3 * kDeg;
    const V3 lan = (galX_ * (std::cos(b) * std::cos(l)) + galY * (std::cos(b) * std::sin(l)) + galNorth_ * std::sin(b)) * (68e6 * kPc);

    for (int i = 0; i < kNodeCount; ++i) tree.off[i] = V3{};
    tree.link(kLaniakea, -1);
    tree.link(kLocalGroup, kLaniakea);
    tree.link(kGalCentre, kLocalGroup);
    tree.link(kSun, kGalCentre);
    tree.link(kEarth, kSun);
    tree.link(kBeachToday, kEarth);
    tree.link(kBeach, kBeachToday);   // hangs on today's beach point, not on the Earth's centre: what is under it (an
                                      // atom, a proton 10^-15 m off) must not be summed through 6,400 km (DURUM, Ö0)
    tree.link(kNucleus, kBeach);
    tree.link(kProton, kNucleus);
    tree.link(kQuark, kProton);
    tree.link(kMoon, kEarth);
    for (int p = kMercury; p <= kNeptune; ++p) tree.link(p, kSun);
    tree.link(kM31, kLocalGroup);
    tree.link(kM33, kLocalGroup);
    tree.link(kLMC, kGalCentre);
    tree.link(kSMC, kGalCentre);
    tree.link(kRocket, kSun);
    tree.off[kLocalGroup] = lg - lan;
    tree.off[kGalCentre] = gc - lg;
    tree.off[kM31] = m31 - lg;
    tree.off[kM33] = m33 - lg;
    tree.off[kLMC] = lmc - gc;
    tree.off[kSMC] = smc - gc;
    tree.off[kBeachToday] = earthBeach_;

    // ---- the inner world (Ö5 replaces these): a proton near the nucleus's middle, the up quark a little off its centre;
    // lateral to the view so that each step of the way in moves the centre at most ~0.3 of the screen
    protonOff_ = earthEast_ * 0.45e-15 + earthNorth_ * 0.25e-15 + earthUp_ * 0.2e-15;
    quarkOff_ = earthEast_ * -0.22e-15 + earthNorth_ * 0.18e-15 + earthUp_ * 0.1e-15;
    // the thermal vibration of the silicon in quartz at 300 K: u_rms ~ 0.07 A, phonons ~ 10^13 Hz (three modes)
    vibAmp_[0] = earthEast_ * 0.06e-10;
    vibAmp_[1] = earthNorth_ * 0.05e-10;
    vibAmp_[2] = earthUp_ * 0.04e-10;
    vibOmega_[0] = 2.0 * kPi * 1.3e13;
    vibOmega_[1] = 2.0 * kPi * 1.9e13;
    vibOmega_[2] = 2.0 * kPi * 2.7e13;

    // ---- bodies
    for (Body* b : bodies_) delete b;
    bodies_.clear();
    auto add = [&](int node, double r, float3 alb, int orbit, int pattern = 0) {
        Body* b = new Body{node, r, alb};
        b->pattern = pattern;
        b->orbit = orbit;
        bodies_.push_back(b);
        return b;
    };
    Body* sun = add(kSun, kRsun, make_float3(0, 0, 0), -1);
    sun->glow = kLsun / (4.0 * kPi * kRsun * kRsun) / kPi;   // radiance of the disc: 2.0e7 W/m^2/sr
    sun->glowColor = blackbody(5772.0);
    add(kMercury, 2.4397e6, make_float3(0.14f, 0.13f, 0.12f), 0);
    add(kVenus, 6.0518e6, make_float3(0.80f, 0.72f, 0.55f), 1);
    add(kEarth, 6.371e6, make_float3(0.30f, 0.30f, 0.30f), 2, 1);
    add(kMoon, 1.7374e6, make_float3(0.13f, 0.12f, 0.11f), 8, 2);
    add(kMars, 3.3895e6, make_float3(0.32f, 0.17f, 0.09f), 3);
    add(kJupiter, 6.9911e7, make_float3(0.55f, 0.48f, 0.38f), 4);
    add(kSaturn, 5.8232e7, make_float3(0.55f, 0.48f, 0.33f), 5);
    add(kUranus, 2.5362e7, make_float3(0.45f, 0.60f, 0.64f), 6);
    add(kNeptune, 2.4622e7, make_float3(0.30f, 0.42f, 0.62f), 7);

    // ---- the stars around the Sun: the placeholder field (some real neighbours and bright stars, then random ones); the
    // catalogue replaces it (loadStars), and keeps its place across init() (the random numbers still run: the galaxies
    // below draw from the same stream)
    std::vector<V3> keepPos;
    std::vector<float4> keepFlux;
    std::vector<float> keepTemp;
    if (realStars_) {
        keepPos.swap(starPos_);
        keepFlux.swap(starFlux_);
        keepTemp.swap(starTemp_);
    }
    starPos_.clear();
    starFlux_.clear();
    starTemp_.clear();
    struct Star { double ra, dec, pc, lum, T; };
    const Star named[] = {
        {219.9021, -60.8340, 1.34, 1.52, 5790}, {219.8961, -60.8375, 1.34, 0.50, 5260}, {217.4289, -62.6795, 1.30, 0.0017, 3042},
        {269.4521, 4.6934, 1.83, 0.0035, 3134}, {164.1203, 7.0147, 2.41, 0.0011, 2800}, {165.8341, 35.9699, 2.55, 0.021, 3547},
        {101.2872, -16.7161, 2.64, 25.4, 9940}, {101.2890, -16.7130, 2.64, 0.056, 25000}, {53.2327, -9.4583, 3.22, 0.34, 5084},
        {114.8255, 5.2250, 3.51, 6.93, 6530}, {316.7248, 38.7494, 3.49, 0.15, 4526}, {297.6958, 8.8683, 5.13, 10.6, 7700},
        {279.2347, 38.7837, 7.68, 40.1, 9602}, {344.4127, -29.6222, 7.70, 16.6, 8590}, {116.3290, 28.0262, 10.36, 32.7, 4666},
        {213.9153, 19.1824, 11.26, 170.0, 4286}, {79.1723, 45.9980, 13.1, 150.0, 4970}, {113.6500, 31.8880, 15.6, 30.0, 10286},
        {68.9802, 16.5093, 20.0, 439.0, 3910}, {152.0930, 11.9670, 24.3, 316.0, 12460}, {201.2983, -11.1613, 77.0, 20500.0, 25300},
        {95.9880, -52.6957, 95.0, 10700.0, 7350}, {88.7929, 7.4071, 168.0, 126000.0, 3600}, {247.3519, -26.4320, 170.0, 75900.0, 3660},
        {78.6345, -8.2016, 264.0, 120000.0, 12100}, {310.3580, 45.2803, 802.0, 196000.0, 8525}, {37.9546, 89.2641, 133.0, 1260.0, 6015},
        {24.4285, -57.2368, 42.8, 3150.0, 15000}};
    for (const Star& st : named) {
        starPos_.push_back(icrs(st.ra, st.dec) * (st.pc * kPc));
        const float3 c = blackbody(st.T);
        starFlux_.push_back(make_float4(c.x, c.y, c.z, float(st.lum)));
        starTemp_.push_back(float(st.T));
    }
    Rng rng(7);
    for (int i = 0; i < 40000; ++i) {   // the neighbourhood: 60 pc, faint stars mostly
        V3 p;
        do p = v3(rng.u() * 2 - 1, rng.u() * 2 - 1, rng.u() * 2 - 1);
        while (dot(p, p) > 1.0 || dot(p, p) < 1e-4);
        const double lum = std::pow(10.0, std::clamp(-1.3 + 1.0 * rng.n(), -3.5, 1.8));
        const double T = 5772.0 * std::pow(lum, 0.13);
        const float3 c = blackbody(T);
        starPos_.push_back(p * (60.0 * kPc));
        starFlux_.push_back(make_float4(c.x, c.y, c.z, float(lum)));
        starTemp_.push_back(float(T));
    }
    for (int i = 0; i < 60000; ++i) {   // the bright ones of the Orion arm: a thin disc 1.5 kpc around
        const double r = 1500.0 * kPc * std::sqrt(rng.u()), a = 2.0 * kPi * rng.u(), z = 90.0 * kPc * rng.n();
        const double lum = std::pow(10.0, 1.0 + 1.6 * std::pow(rng.u(), 2.0));
        const double T = lum > 1000.0 && rng.u() < 0.5 ? 3800.0 : 7000.0 + 9000.0 * rng.u();
        const float3 c = blackbody(T);
        starPos_.push_back(galX_ * (r * std::cos(a)) + galY * (r * std::sin(a)) + galNorth_ * z);
        starFlux_.push_back(make_float4(c.x, c.y, c.z, float(lum)));
        starTemp_.push_back(float(T));
    }
    if (realStars_) {
        starPos_.swap(keepPos);
        starFlux_.swap(keepFlux);
        starTemp_.swap(keepTemp);
    }

    // ---- galaxies (Ö3 replaces these): a web of nodes and filaments through the observable universe
    galPos_.clear();
    galFlux_.clear();
    const double Ru = 4.40e26;
    std::vector<V3> nodes;
    for (int i = 0; i < 2600; ++i) {
        V3 p;
        do p = v3(rng.u() * 2 - 1, rng.u() * 2 - 1, rng.u() * 2 - 1);
        while (dot(p, p) > 1.0);
        nodes.push_back(p * Ru);
    }
    auto addGal = [&](V3 p) {
        const double lum = std::pow(10.0, 36.3 + 0.5 * rng.n());
        const float3 c = blackbody(5200.0 + 1500.0 * rng.u());
        galPos_.push_back(p - lan);   // from the Laniakea centre
        galFlux_.push_back(make_float4(c.x, c.y, c.z, float(lum / kLsun)));
    };
    for (size_t i = 0; i < nodes.size(); ++i) {
        // a cluster at the node, filaments to its two nearest
        for (int k = 0; k < 14; ++k) addGal(nodes[i] + v3(rng.n(), rng.n(), rng.n()) * 1.0e23);
        double d1 = 1e300, d2 = 1e300;
        int n1 = -1, n2 = -1;
        for (size_t j = 0; j < nodes.size(); ++j) {
            if (j == i) continue;
            const double d = length(nodes[j] - nodes[i]);
            if (d < d1) { d2 = d1; n2 = n1; d1 = d; n1 = int(j); }
            else if (d < d2) { d2 = d; n2 = int(j); }
        }
        for (int f : {n1, n2}) {
            if (f < 0) continue;
            for (int k = 0; k < 10; ++k) addGal(nodes[i] + (nodes[f] - nodes[i]) * rng.u() + v3(rng.n(), rng.n(), rng.n()) * 2.5e23);
        }
    }
    for (int k = 0; k < 6000; ++k) addGal(lan + v3(rng.n(), rng.n(), rng.n()) * 1.6e24);   // the Laniakea basin's own
    lanFromSun_ = lan;
    mergeSurvey();

    clk = Clocks{};
    place();
}

bool Scale::loadStars(const std::string& path, std::string& log) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        log += "  yok: " + path + std::string(1, char(10));
        return false;
    }
    // "OLST": luminosity, r, g, b per star; "OLS2" (the second option's Doppler needs it): and the temperature
    char magic[4];
    unsigned int n = 0;
    if (std::fread(magic, 1, 4, f) != 4 || (std::memcmp(magic, "OLST", 4) != 0 && std::memcmp(magic, "OLS2", 4) != 0) ||
        std::fread(&n, 4, 1, f) != 1) {
        std::fclose(f);
        log += "  bicim: " + path + std::string(1, char(10));
        return false;
    }
    const int na = std::memcmp(magic, "OLS2", 4) == 0 ? 5 : 4;
    std::vector<double> pos(size_t(n) * 3);
    std::vector<float> att(size_t(n) * na);
    const bool ok = std::fread(pos.data(), 8, pos.size(), f) == pos.size() && std::fread(att.data(), 4, att.size(), f) == att.size();
    std::fclose(f);
    if (!ok) {
        log += "  kisa: " + path + std::string(1, char(10));
        return false;
    }
    starPos_.resize(n);
    starFlux_.resize(n);
    starTemp_.resize(n);
    realStars_ = true;
    for (unsigned int i = 0; i < n; ++i) {
        starPos_[i] = v3(pos[3 * i], pos[3 * i + 1], pos[3 * i + 2]);
        const float* a = &att[size_t(na) * i];
        starFlux_[i] = make_float4(a[1], a[2], a[3], a[0]);
        if (na == 5) {
            starTemp_[i] = a[4];
        } else {
            // an older file: the temperature back from the colour (its red over blue rises monotonically with 1/T)
            const double want = double(a[1]) / std::max(double(a[3]), 1e-9);
            double lo = 1000.0, hi = 60000.0;
            for (int it = 0; it < 50; ++it) {
                const double mid = std::sqrt(lo * hi);
                const float3 c = blackbody(mid);
                (c.x / c.z > want ? lo : hi) = mid;
            }
            starTemp_[i] = float(std::sqrt(lo * hi));
        }
    }
    char b[160];
    std::snprintf(b, sizeof(b), "  yildizlar.bin: %u yildiz (HYG v4.1)%s%c", n, na == 5 ? "" : ", sicaklik renkten (eski bicim)", 10);
    log += b;
    return true;
}

int Scale::nearestStar(V3 p) const {
    int best = -1;
    double bd = 1e300;
    for (size_t i = 0; i < starPos_.size(); ++i) {
        const V3 d = starPos_[i] - p;
        const double d2 = dot(d, d);
        if (d2 < bd) {
            bd = d2;
            best = int(i);
        }
    }
    return best;
}

void Scale::moveStar(int i, V3 p) {
    if (i >= 0 && i < int(starPos_.size())) starPos_[size_t(i)] = p;
}

void Scale::hizClocks(double t) {
    // the rest frame's time drives the world (the passenger's clock is the screen's): no designed rate, no way home
    clk.spin = wrapPi(std::fmod(today::kEarthOmega * t, 2.0 * kPi));
    const double yearP = planetOrbit_[2].period;
    clk.year = std::fmod(t, yearP);
    if (clk.year > 0.5 * yearP) clk.year -= yearP;
    if (clk.year < -0.5 * yearP) clk.year += yearP;
    clk.moon = wrapTo(t, moonOrbit_.period);
    for (int i = 0; i < 8; ++i) clk.planets[i] = wrapTo(t, planetOrbit_[i].period);
    // the Galaxy's turn and its arms' held at today's: the route is laid in the rest frame of the Sun, Proxima and
    // Andromeda (PLAN §2), and 2.4 million years of the Sun's orbit would carry it 1,840 ly against Andromeda
    clk.galaxy = 0.0;
    clk.arms = 0.0;
    clk.beach = wrapTo(t, 8000.0);
}

void Scale::hizOff() {   // (the scene puts the first option's clocks back: they were its own)
    hiz.on = false;
    cam.fwd = -earthUp_;
    cam.up = earthNorth_;
    cam.right = earthEast_;
}

// the survey's galaxies in, the placeholder's out where the survey reaches (it is flux-limited: thin far out, but real)
void Scale::mergeSurvey() {
    if (surveyPos_.empty()) return;
    // beyond the survey the volume web is the light (placeholder points kept only when it is missing: volumeWeb_)
    std::vector<V3> pos;
    std::vector<float4> flux;
    for (size_t i = 0; i < galPos_.size(); ++i)
        if (!volumeWeb_ && length(galPos_[i] + lanFromSun_) > 200e6 * kPc) {
            pos.push_back(galPos_[i]);
            flux.push_back(galFlux_[i]);
        }
    for (size_t i = 0; i < surveyPos_.size(); ++i) {
        pos.push_back(surveyPos_[i] - lanFromSun_);
        flux.push_back(surveyFlux_[i]);
    }
    galPos_.swap(pos);
    galFlux_.swap(flux);
}

bool Scale::loadGalaxies(const std::string& path, std::string& log) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        log += "  yok: " + path + std::string(1, char(10));
        return false;
    }
    char magic[4];
    unsigned int n = 0;
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "OLST", 4) != 0 || std::fread(&n, 4, 1, f) != 1) {
        std::fclose(f);
        log += "  bicim: " + path + std::string(1, char(10));
        return false;
    }
    std::vector<double> pos(size_t(n) * 3);
    std::vector<float> att(size_t(n) * 4);
    const bool ok = std::fread(pos.data(), 8, pos.size(), f) == pos.size() && std::fread(att.data(), 4, att.size(), f) == att.size();
    std::fclose(f);
    if (!ok) {
        log += "  kisa: " + path + std::string(1, char(10));
        return false;
    }
    surveyPos_.resize(n);
    surveyFlux_.resize(n);
    for (unsigned int i = 0; i < n; ++i) {
        surveyPos_[i] = v3(pos[3 * i], pos[3 * i + 1], pos[3 * i + 2]);
        surveyFlux_[i] = make_float4(att[4 * i + 1], att[4 * i + 2], att[4 * i + 3], att[4 * i]);
    }
    const size_t before = galPos_.size();
    mergeSurvey();
    char b[160];
    std::snprintf(b, sizeof(b), "  galaksiler.bin: %u galaksi (2MRS), yer tutucu agdan %zu kaldi (200 Mpc otesi)%c", n, galPos_.size() - n, 10);
    (void)before;
    log += b;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Time (PLAN §4)

void Scale::advance(double dt, double sPrev) {
    const double T = rate(s) * dt;   // the world's seconds this frame
    // the Earth's turn: the camera is with the ground below 10^7 m (it turns with it: no sliding, always noon); out from
    // there it leaves it over 1.6 decades and the Earth turns at its own pace (so the whole globe is seen turning slowly
    // first, a blur only once it is small). Coming in, what it turned since is undone in step with the zoom: back to
    // today's turn by 10^7, by the shorter way (PLAN §3 "eve dönüş").
    const double gs = smoothstep(kSpinLo, kSpinHi, s), gsPrev = smoothstep(kSpinLo, kSpinHi, sPrev);
    clk.spin = wrapPi(clk.spin + today::kEarthOmega * T * gs);
    if (s < sPrev) clk.spin = gsPrev > 0.0 ? clk.spin * (gs / gsPrev) : 0.0;
    // the year: the same, once the centre has left the Earth for the Sun (a camera following the Earth round its orbit
    // would swing the whole view; and the season at the beach is today's)
    const double gy = smoothstep(11.9, 12.9, s), gyPrev = smoothstep(11.9, 12.9, sPrev);
    const double yearP = planetOrbit_[2].period;
    clk.year = clk.year + T * gy;
    clk.year = std::fmod(clk.year, yearP);
    if (clk.year > 0.5 * yearP) clk.year -= yearP;
    if (clk.year < -0.5 * yearP) clk.year += yearP;
    if (s < sPrev) clk.year = gyPrev > 0.0 ? clk.year * (gy / gyPrev) : 0.0;
    // the others go their own way and stay where they went (the home does not depend on them)
    clk.moon = wrapTo(clk.moon + T, moonOrbit_.period);
    for (int i = 0; i < 8; ++i) clk.planets[i] = wrapTo(clk.planets[i] + T, planetOrbit_[i].period);
    clk.galaxy = wrapTo(clk.galaxy + kOmegaSun * T, 2.0 * kPi);
    clk.arms = wrapTo(clk.arms + kOmegaArms * T, 2.0 * kPi);
    for (int k = 0; k < 3; ++k) clk.vib[k] = wrapTo(clk.vib[k] + vibOmega_[k] * T, 2.0 * kPi);
    // the foam's octaves: an octave of wavelength 2^-j m lives ~ its light-crossing time; the string's modes
    for (int i = 0; i < 128; ++i) {
        const int j = 40 + i;
        clk.foam[i] = wrapTo(clk.foam[i] + std::fmod(T * kC * std::ldexp(1.0, j), 256.0), 256.0);
    }
    for (int n = 0; n < kModes; ++n) clk.string[n] = wrapTo(clk.string[n] + std::fmod(T * (n + 2) * kC / kLoop, 2.0 * kPi), 2.0 * kPi);
    foamClock_ = wrapTo(foamClock_ + std::fmod(T * kC / kPlanckLength, 4096.0), 4096.0);
    clk.beach = wrapTo(clk.beach + T, 8000.0);
}

// the shutter's average of a sine of angular frequency w (screen time): its amplitude kept, and its variance spread
static double keptBy(double w, double sigma) { return std::exp(-0.5 * w * w * sigma * sigma); }

void Scale::place() {
    // the Sun around the Galaxy's centre (clockwise seen from the north galactic pole)
    tree.off[kSun] = rotate(sunFromGC_, galNorth_, -clk.galaxy);
    tree.off[kEarth] = planetOrbit_[2].at(clk.year);
    const int ids[8] = {kMercury, kVenus, -1, kMars, kJupiter, kSaturn, kUranus, kNeptune};
    for (int i = 0; i < 8; ++i)
        if (ids[i] >= 0) tree.off[ids[i]] = planetOrbit_[i].at(clk.planets[i]);
    tree.off[kMoon] = moonOrbit_.at(clk.moon);
    tree.off[kBeach] = rotate(earthBeach_, earthPole_, clk.spin) - earthBeach_;   // exactly 0 at today's turn
    // the nucleus about the atom's mean place: each mode's swing as the shutter sees it (fast: averaged to the middle)
    const double R = rate(s);
    V3 u;
    for (int k = 0; k < 3; ++k) u += vibAmp_[k] * (std::sin(clk.vib[k]) * keptBy(vibOmega_[k] * R, shutter));
    tree.off[kNucleus] = u;
    tree.off[kProton] = protonOff_;
    tree.off[kQuark] = quarkOff_;
    tree.off[kRocket] = hiz.rocket;
}

double Scale::focusBlend(int& a, int& b) const {
    const int n = int(sizeof(kSegs) / sizeof(kSegs[0]));
    if (s <= kSegs[0].s0) { a = b = kSegs[0].a; return 0.0; }
    for (int i = 0; i < n; ++i) {
        const Seg& g = kSegs[i];
        if (s <= g.s0) { a = b = g.a; return 0.0; }
        if (s < g.s1) {
            a = g.a;
            b = g.b;
            const double W0 = std::pow(10.0, g.s0), W1 = std::pow(10.0, g.s1), W = std::pow(10.0, s);
            return smoother((W - W0) / (W1 - W0));
        }
    }
    a = b = kSegs[n - 1].b;
    return 0.0;
}

void Scale::camera() {
    if (hiz.on) {
        // the rocket from its side: its motion to the screen's right, the Galaxy's north (across the motion) up
        cam.ref = kRocket;
        cam.right = normalize(hiz.motion);
        V3 up = galNorth_ - cam.right * dot(galNorth_, cam.right);
        if (length(up) < 1e-6) up = earthNorth_ - cam.right * dot(earthNorth_, cam.right);
        cam.up = normalize(up);
        cam.fwd = cross(cam.up, cam.right);   // right = fwd x up
        // at the galaxies' scales the camera rises above the Galaxy's plane about the motion (the motion stays across the
        // screen): a disc seen inclined, as on Özcan's reference (edge-on it is a faint line, and from within its breadth
        // the camera sat in a wall of stars); at the rocket's scales a true side view
        const double tilt = 32.0 * kDeg * smoothstep(16.5, 19.0, s);
        if (tilt > 0.0) {
            const V3 f = cam.fwd, u = cam.up;
            cam.fwd = normalize(f * std::cos(tilt) - u * std::sin(tilt));
            cam.up = normalize(u * std::cos(tilt) + f * std::sin(tilt));
        }
        cam.W = std::pow(10.0, s);
        cam.D = cam.W / (2.0 * std::tan(0.5 * fovX * kDeg));
        cam.pos = hiz.focus - cam.fwd * cam.D;
        return;
    }
    int a, b;
    const double u = focusBlend(a, b);
    (void)u;
    cam.ref = tree.depth[a] >= tree.depth[b] ? a : b;
    cam.W = std::pow(10.0, s);
    cam.D = cam.W / (2.0 * std::tan(0.5 * fovX * kDeg));
    cam.pos = focus() - cam.fwd * cam.D;
}

V3 Scale::focus() const {
    if (hiz.on) return hiz.focus;   // from the rocket (the camera's anchor)
    int a, b;
    const double u = focusBlend(a, b);
    return tree.rel(cam.ref, a) * (1.0 - u) + tree.rel(cam.ref, b) * u;
}

V3 Scale::onScreen(int node, V3 local) const {
    const V3 r = tree.rel(cam.ref, node) + local - cam.pos;
    const double z = dot(r, cam.fwd), t = 2.0 * std::tan(0.5 * fovX * kDeg);
    return {dot(r, cam.right) / (z * t), dot(r, cam.up) / (z * t), z};
}

// ---------------------------------------------------------------------------------------------------------------------
// The frame

double Scale::runPeriod(int orbit) const {
    if (orbit == 8) return moonOrbit_.period;
    if (orbit == 2) {
        const double gy = hiz.on ? 1.0 : smoothstep(11.9, 12.9, s);   // (the second option: no home to keep)
        return gy > 1e-9 ? planetOrbit_[2].period / gy : 1e300;
    }
    return orbit >= 0 && orbit < 8 ? planetOrbit_[orbit].period : 1e300;
}

V3 Scale::sunShift(int node, double tau) const {
    const double gy = hiz.on ? 1.0 : smoothstep(11.9, 12.9, s);
    auto earth = [&]() { return gy > 0.0 ? planetOrbit_[2].at(clk.year + tau * gy) - tree.off[kEarth] : V3{}; };
    switch (node) {
    case kEarth: return earth();
    case kMoon: return earth() + moonOrbit_.at(clk.moon + tau) - tree.off[kMoon];
    case kMercury: return planetOrbit_[0].at(clk.planets[0] + tau) - tree.off[node];
    case kVenus: return planetOrbit_[1].at(clk.planets[1] + tau) - tree.off[node];
    case kMars: return planetOrbit_[3].at(clk.planets[3] + tau) - tree.off[node];
    case kJupiter: return planetOrbit_[4].at(clk.planets[4] + tau) - tree.off[node];
    case kSaturn: return planetOrbit_[5].at(clk.planets[5] + tau) - tree.off[node];
    case kUranus: return planetOrbit_[6].at(clk.planets[6] + tau) - tree.off[node];
    case kNeptune: return planetOrbit_[7].at(clk.planets[7] + tau) - tree.off[node];
    default: return {};
    }
}

void Scale::ballOrPoint(OlcekView& v, std::vector<GSplat>& splats, const Body& b, double k) const {
    // The Earth: a sphere through the beach point, its centre below it along the local (geodetic) vertical. The true
    // centre is 21 km off that line (the ellipsoid) and the beach 1 km inside a mean-radius sphere about it: the camera
    // a kilometre up would be under the ground (DURUM, Ö0). The sphere's normal is the geodetic vertical everywhere
    // near home, and its lat/lon are the maps' (Ö1).
    const V3 rel = b.pattern == 1 ? tree.rel(cam.ref, kBeach) - rotate(earthUp_, earthPole_, clk.spin) * b.radius - cam.pos
                                  : tree.rel(cam.ref, b.node) - cam.pos;
    const double dist = length(rel);
    if (dist <= b.radius) return;   // the camera inside: never on the way (DURUM)
    const double z = dot(rel, cam.fwd);
    const double ang = std::asin(std::min(1.0, b.radius / dist));
    const double rpx = std::tan(ang) / v.pixAngle;
    // its light: the Sun's direction and irradiance there
    V3 toSun{0, 0, 1};
    double E = 0.0;
    if (b.glow <= 0.0) {
        const V3 ts = tree.rel(b.node, kSun);
        const double d = length(ts);
        toSun = ts * (1.0 / d);
        E = kLsun / (4.0 * kPi * d * d);
    }
    float wBall = float(smoothstep(1.0, 2.5, rpx));
    // the shutter (PLAN §4): what moves faster than the eye is drawn along its way, weighted by the shutter; an orbit
    // run through several times in it is its ring (light spread by the time spent: slow at aphelion, bright there)
    int K = 1;
    bool ring = false;
    double sigL = 0.0, P = 1e300, reach = 0.0;
    if (b.orbit >= 0) {
        sigL = shutter * worldRate();
        P = runPeriod(b.orbit);
        const V3 d1 = sunShift(b.node, sigL);
        const V3 dp = d1 - cam.fwd * dot(d1, cam.fwd);
        const double blurPx = length(dp) / dist / v.pixAngle;
        if (blurPx > 0.25 && P < 1e299) {
            const Kepler& o = b.orbit == 8 ? moonOrbit_ : planetOrbit_[b.orbit];
            const double circPx = 2.0 * kPi * o.a / dist / v.pixAngle;
            ring = 6.0 * sigL >= 4.0 * P;
            const double pathPx = ring ? circPx : std::max(circPx * 6.0 * sigL / P, 6.0 * blurPx);
            K = int(std::clamp(pathPx / 0.4, 4.0, 8192.0));
            wBall *= float(1.0 - smoothstep(0.3, 1.0, blurPx / std::max(rpx, 0.5)));
            reach = ring ? o.a * (1.0 + o.e) : std::min(o.a * (1.0 + o.e), kPi * o.a * 6.0 * sigL / P + 3.0 * length(dp));
        }
    }
    // in front and on the screen (with its size, and the way it is drawn along, as the margin)
    {
        const double rr = b.radius + reach;
        const double tx = v.tanHalfX, ty = v.tanHalfY;
        const double x = dot(rel, cam.right), y = dot(rel, cam.up);
        const double margin = rr / std::max(z, 1e-300) + 4.0 * v.pixAngle;
        if (z <= -rr) return;
        if (z > 0.0 && (std::fabs(x / z) > tx + margin * 1.5 + 0.01 || std::fabs(y / z) > ty + margin * 1.5 + 0.01) && dist > 3.0 * rr) return;
    }
    // the flux of the whole disc (what its point carries): a Lambert sphere's phase law, or the glowing disc
    float3 flux;
    const V3 toCam = rel * (-1.0 / dist);
    if (b.glow > 0.0) {
        const double f = b.glow * kPi * b.radius * b.radius / (dist * dist) * 0.82;   // 0.82: the limb darkening's mean
        flux = b.glowColor * float(f);
    } else {
        const double ph = std::acos(std::clamp(dot(toSun, toCam), -1.0, 1.0));
        const double f = (2.0 / 3.0) * E * b.radius * b.radius * (std::sin(ph) + (kPi - ph) * std::cos(ph)) / kPi / (dist * dist);
        flux = b.albedo * float(f);
    }
    const double D = cam.D;
    auto toCamSpace = [&](V3 r) { return make_float3(float(dot(r, cam.right) / D), float(dot(r, cam.up) / D), float(dot(r, cam.fwd) / D)); };
    auto dirCam = [&](V3 r) { return make_float3(float(dot(r, cam.right)), float(dot(r, cam.up)), float(dot(r, cam.fwd))); };
    if (wBall > 0.0f && v.nBalls < kMaxBalls) {
        GBall& g = v.balls[v.nBalls++];
        g.c = toCamSpace(rel);
        g.r = float(b.radius / D);
        g.h = float((dist - b.radius) / D);
        g.toLight = dirCam(toSun);
        g.albedo = b.albedo;
        g.lit = float(E / kPi * k);
        g.emit = b.glow > 0.0 ? b.glowColor * float(b.glow * k) : make_float3(0, 0, 0);
        g.weight = wBall;
        g.pattern = b.pattern;
        g.spinBlur = 0.0f;
        if (b.pattern == 1) {   // the Earth's body axes, turned by its clock; its turn while the shutter is open
            const V3 ax = rotate(v3(today::kEarthAxes[0][0], today::kEarthAxes[1][0], today::kEarthAxes[2][0]), earthPole_, clk.spin);
            const V3 ay = rotate(v3(today::kEarthAxes[0][1], today::kEarthAxes[1][1], today::kEarthAxes[2][1]), earthPole_, clk.spin);
            g.ax = dirCam(ax);
            g.ay = dirCam(ay);
            g.az = dirCam(earthPole_);
            g.spinBlur = float(today::kEarthOmega * worldRate() * (hiz.on ? 1.0 : smoothstep(kSpinLo, kSpinHi, s)) * shutterSpin);
            const V3 hb = earthUp_;   // today's beach in the body axes (its vertical: the sphere's normal there)
            const V3 X = v3(today::kEarthAxes[0][0], today::kEarthAxes[1][0], today::kEarthAxes[2][0]);
            const V3 Y = v3(today::kEarthAxes[0][1], today::kEarthAxes[1][1], today::kEarthAxes[2][1]);
            g.home = make_float3(float(dot(hb, X)), float(dot(hb, Y)), float(dot(hb, earthPole_)));
        } else if (b.pattern == 2) {   // the Moon: the same face to the Earth
            const V3 toE = normalize(tree.rel(kMoon, kEarth));
            const V3 nrm = normalize(cross(moonOrbit_.P, moonOrbit_.Q));
            const V3 ay = normalize(cross(nrm, toE));
            g.ax = dirCam(toE);
            g.ay = dirCam(ay);
            g.az = dirCam(nrm);
        }
    }
    if (wBall < 1.0f) {
        const float scale = float(k / (v.pixAngle * v.pixAngle)) * (1.0f - wBall);
        const float3 fl = flux * scale;
        const float sig = float(std::sqrt(0.75 * 0.75 + 0.25 * rpx * rpx));
        if (fl.x + fl.y + fl.z > 1e-5f) {
            if (K == 1) {
                splats.push_back({toCamSpace(rel), fl, sig});
            } else {
                std::vector<double> w(K), tau(K);
                double ws = 0.0;
                for (int i = 0; i < K; ++i) {
                    tau[i] = ring ? P * (i + 0.5) / K : (double(i) / (K - 1) - 0.5) * 6.0 * sigL;
                    w[i] = ring ? 1.0 : std::exp(-0.5 * (tau[i] / sigL) * (tau[i] / sigL));
                    ws += w[i];
                }
                // the flash at the end of the exposure (a representation, the light kept): a third of it where the
                // body is now, so a planet reads as a point on its ring; the rest along its way
                const float flash = 0.35f;
                splats.push_back({toCamSpace(rel), fl * flash, sig});
                for (int i = 0; i < K; ++i)
                    splats.push_back({toCamSpace(rel + sunShift(b.node, tau[i])), fl * float((1.0 - flash) * w[i] / ws), sig});
            }
        }
    }
}

void Scale::build(OlcekView& v, std::vector<GSplat>& splats) {
    const bool densityMap = true;   // the inner world drawn as density maps (Ö4-Ö5); false: the Ö0 placeholders
    splats.clear();
    v.width = width;
    v.height = height;
    v.tanHalfX = float(std::tan(0.5 * fovX * kDeg));
    v.tanHalfY = v.tanHalfX * float(height) / float(width);
    v.pixAngle = 2.0f * v.tanHalfX / float(width);
    v.nBalls = v.nBlobs = v.nDiscs = 0;
    v.background = make_float3(0, 0, 0);
    // colour ends below the light's wavelength (the instruments' grey: the microscopes' pictures), and comes back as a
    // representation from the atom down (Özcan's revision: the particles' palette)
    v.saturation = float(std::max(smoothstep(-7.0, -6.0, s), smoothstep(-8.4, -9.2, s)));
    v.metres = float(cam.D);
    // 3e-3: sunlit sand (~130 W/m^2/sr) at ~0.4 on the display (the second option designs its own exposure)
    const double k = std::exp2(hiz.on ? hiz.ev : ev(s)) * 3.0e-3;
    const double D = cam.D, W = cam.W;
    const float pa = v.pixAngle;
    auto toCamSpace = [&](V3 r) { return make_float3(float(dot(r, cam.right) / D), float(dot(r, cam.up) / D), float(dot(r, cam.fwd) / D)); };
    auto dirCam = [&](V3 r) { return make_float3(float(dot(r, cam.right)), float(dot(r, cam.up)), float(dot(r, cam.fwd))); };
    const double omegaPx = double(pa) * pa;
    auto inFront = [&](V3 rel) {
        const double z = dot(rel, cam.fwd);
        if (z <= 0.0) return false;
        return std::fabs(dot(rel, cam.right)) < z * (v.tanHalfX + 6.0 * pa) && std::fabs(dot(rel, cam.up)) < z * (v.tanHalfY + 6.0 * pa);
    };

    // ---- the outer world: physical light, this scale's exposure (the second option: from the rocket, at every scale)
    if (s > 1.9 || hiz.on) {
        for (const Body* b : bodies_) {
            if (b->node == kEarth && s < 2.0 && !hiz.on) continue;
            ballOrPoint(v, splats, *b, k);
        }
    }
    // stars: points (Ö2: the catalogue)
    if (s > 9.0 || hiz.on) {
        const V3 base = tree.rel(cam.ref, kSun) - cam.pos;
        for (size_t i = 0; i < starPos_.size(); ++i) {
            const V3 rel = base + starPos_[i];
            if (!inFront(rel)) continue;
            const double d2 = dot(rel, rel);
            const float near = float(smoothstep(0.03, 0.25, std::sqrt(d2) / D));
            if (near <= 0.0f) continue;
            const float4 f = starFlux_[i];
            const double fl = double(f.w) * kLsun / (4.0 * kPi * d2) * k / omegaPx;
            if (fl < 2e-4) continue;
            splats.push_back({toCamSpace(rel), make_float3(f.x, f.y, f.z) * (float(fl) * near), 0.75f});
        }
    }
    // galaxies: points
    if (s > 21.0 || hiz.on) {
        const V3 base = tree.rel(cam.ref, kLaniakea) - cam.pos;
        for (size_t i = 0; i < galPos_.size(); ++i) {
            const V3 rel = base + galPos_[i];
            if (!inFront(rel)) continue;
            const double d2 = dot(rel, rel);
            float near = float(smoothstep(0.3, 1.0, std::sqrt(d2) / D));   // wide: nearby galaxies stream past softly
            if (near <= 0.0f) continue;
            // the same slice as the web's (webLight): sigma 20 Mpc about the focus, across the view
            const double dz = (dot(rel, cam.fwd) - D) / (20e6 * kPc);
            near *= float(std::exp(-0.5 * dz * dz));
            if (near < 0.01f) continue;
            const float4 f = galFlux_[i];
            const double fl = double(f.w) * kLsun / (4.0 * kPi * d2) * k / omegaPx;
            if (fl < 2e-4) continue;
            // a galaxy is ~30 kpc across: a disc on the screen when near
            const double rpx = 15e3 * kPc / std::sqrt(d2) / pa;
            splats.push_back({toCamSpace(rel), make_float3(f.x, f.y, f.z) * (float(fl) * near), float(std::sqrt(0.5625 + 0.25 * rpx * rpx))});
        }
    }
    // the Galaxy and its neighbours: discs of light (the unresolved stars), coming in where the stars blend (PLAN §7:
    // a decade of overlap)
    for (GGalaxy& g : v.galaxy) g = GGalaxy{};
    if (s > 18.5) {
        const double fade = smoothstep(18.8, 20.0, s);
        // the Milky Way: today's arms (their pattern turns at its own speed: clk.arms)
        GGalaxy& g = v.galaxy[0];
        const V3 rel = tree.rel(cam.ref, kGalCentre) - cam.pos;
        if (length(rel) < 1e9 * D) {
            g.on = 1;
            g.c = toCamSpace(rel);
            g.n = dirCam(galNorth_);
            const V3 e0 = rotate(-galX_, galNorth_, -clk.arms);           // the centre -> Sun line of today, turned with the arms
            const V3 e1 = cross(e0, galNorth_);                           // the rotation's way (l = 90 degrees today)
            g.e0 = dirCam(e0);
            g.e1 = dirCam(e1);
            g.kpc = float(1e3 * kPc / D);
            // face-on surface brightness (W/m^2/sr at the centre), from luminosities: disc 3.5e10, bulge and bar 1.2e10,
            // young stars in the arms 0.4e10 (L_sun); the nebulae's Halpha a colour of the arms
            const double h = 2.6e3 * kPc;
            const double Idisc = 3.5e10 * kLsun / (2.0 * kPi * h * h) / (4.0 * kPi);
            g.disc = blackbody(4600.0) * float(Idisc * k * fade);
            g.bulge = blackbody(3900.0) * float(Idisc * 9.0 * k * fade);
            g.young = blackbody(11000.0) * float(Idisc * 0.9 * k * fade);
            g.nebula = make_float3(1.0f, 0.36f, 0.52f) * float(Idisc * 0.35 * k * fade);
            g.fade = float(fade);
            const double d2r = kPi / 180.0;
            struct A { double bk, Rk, lo, hi, b0, b1, w, st, ext, gas; };
            // Reid et al. 2019, Table 2 (beta range: where the parallaxes are). The four major arms go on round the far
            // side, one winding from the bar to the edge (extension in degrees, a representation); the spurs stay short.
            // As in the familiar picture (Churchwell et al. 2009, R. Hurt's drawing for NASA/JPL-Caltech): two arms of
            // stars from the bar's ends (Scutum-Centaurus, Perseus), two of gas (Norma, Sagittarius-Carina)
            const A arms[] = {
                {15, 3.52, -4.2, -4.2, 15, 18, 0.18, 0.35, 40, 0.5},      // 3-kpc (near)
                {18, 4.46, -1.0, 19.5, 5, 54, 0.16, 0.5, 170, 0.8},       // Norma (on into the Outer arm)
                {23, 4.91, 14.1, 12.1, 0, 104, 0.34, 1.0, 230, 0.2},      // Scutum-Centaurus (on into the outer Sct-Cen)
                {24, 6.04, 17.1, 1.0, 2, 97, 0.22, 0.5, 150, 0.8},        // Sagittarius-Carina
                {9, 8.26, 11.4, 11.4, -8, 34, 0.28, 0.4, 30, 0.45},       // Local (the Orion spur, the Sun's)
                {40, 8.87, 10.3, 8.7, -23, 115, 0.42, 1.0, 230, 0.2},     // Perseus
                {18, 12.24, 3.0, 9.4, -16, 71, 0.6, 0.4, 60, 0.6}};       // Outer
            g.nArms = 0;
            for (const A& a : arms) {
                GArm& m = g.arms[g.nArms++];
                m.betaKink = float(a.bk * d2r);
                m.Rkink = float(a.Rk);
                m.tanLo = float(std::tan(a.lo * d2r));
                m.tanHi = float(std::tan(a.hi * d2r));
                m.betaMin = float(a.b0 * d2r);
                m.betaMax = float(a.b1 * d2r);
                m.width = float(a.w);
                m.strength = float(a.st);
                m.ext = float(a.ext * d2r);
                m.gas = float(a.gas);
            }
        }
        // M31 and M33 by the same model at their own size and in their measured orientation: the disc's normal from
        // the inclination i and the major axis's position angle (north through east) on the sky; the near side towards
        // the minor axis's NW end. Their arms are a representation (M31: the 10 kpc ring of star formation and two
        // tightly wound arms, Gordon et al. 2006; M33: two open arms and its large HII regions); sizes and light measured
        struct Other { int node, slot; double ra, dec, inc, pa, h, lDisc, bulge, young, nebula, size, barW, barHalf, dust; };
        const Other others[] = {
            {kM31, 1, 10.6847, 41.2690, 77.5, 38.0, 5.3, 2.1e10, 14.0, 0.45, 0.12, 2.0, 0.25, 2.0, 1.8},
            {kM33, 2, 23.4621, 30.6599, 55.0, 22.0, 1.4, 2.8e9, 0.6, 1.6, 0.9, 0.55, 0.0, 0.5, 0.9}};
        for (const Other& o : others) {
            const V3 rel = tree.rel(cam.ref, o.node) - cam.pos;
            if (length(rel) > 1e9 * D) continue;
            GGalaxy& m = v.galaxy[o.slot];
            const V3 los = icrs(o.ra, o.dec);
            const V3 eN = normalize(icrs(o.ra, o.dec + 0.01) - los), eE = normalize(icrs(o.ra + 0.01, o.dec) - los);
            const double pa = o.pa * kDeg, inc = o.inc * kDeg;
            const V3 major = eN * std::cos(pa) + eE * std::sin(pa), minor = eN * -std::sin(pa) + eE * std::cos(pa);
            const V3 n = normalize(los * -std::cos(inc) + minor * std::sin(inc));
            const V3 e0 = normalize(major - n * dot(major, n)), e1 = cross(n, e0);
            m.on = 1;
            m.c = toCamSpace(rel);
            m.n = dirCam(n);
            m.e0 = dirCam(e0);
            m.e1 = dirCam(e1);
            m.kpc = float(1e3 * kPc / D);
            m.size = float(o.size);
            m.hDisc = float(o.h / o.size);
            m.barW = float(o.barW);
            m.barHalf = float(o.barHalf);
            m.barAngle = 0.6f;
            m.dust = float(o.dust);
            const double h = o.h * 1e3 * kPc;
            const double Id = o.lDisc * kLsun / (2.0 * kPi * h * h) / (4.0 * kPi);
            m.disc = blackbody(o.node == kM31 ? 4300.0 : 5600.0) * float(Id * k * fade);
            m.bulge = blackbody(3900.0) * float(Id * o.bulge * k * fade);
            m.young = blackbody(11000.0) * float(Id * o.young * k * fade);
            m.nebula = make_float3(1.0f, 0.36f, 0.52f) * float(Id * o.nebula * k * fade);
            m.fade = float(fade);
            // arms in model kpc: {betaKink, Rkink, tanLo, tanHi, betaMin, betaMax, width, strength, ext} (radians)
            const double d2r = kPi / 180.0;
            struct A { double bk, Rk, plo, phi, b0, b1, w, st, ext, gas; };
            const A m31Arms[] = {
                {0, 5.0, 0.0, 0.0, -180, 180, 0.32, 1.0, 5, 0.55},       // the ring (pitch 0: a circle at 10 kpc)
                {0, 3.6, 7.0, 7.0, -40, 260, 0.22, 0.55, 30, 0.4},       // two tightly wound arms inside and outside it
                {180, 6.6, 7.0, 7.0, -60, 230, 0.26, 0.5, 30, 0.4}};
            const A m33Arms[] = {
                {0, 4.0, 26.0, 26.0, -60, 300, 0.45, 1.0, 40, 0.5},
                {180, 4.0, 26.0, 26.0, -60, 280, 0.42, 0.9, 40, 0.5}};
            const A* arms = o.node == kM31 ? m31Arms : m33Arms;
            const int nA = o.node == kM31 ? 3 : 2;
            m.nArms = 0;
            for (int i = 0; i < nA; ++i) {
                const A& a = arms[i];
                GArm& g2 = m.arms[m.nArms++];
                g2.betaKink = float(a.bk * d2r);
                g2.Rkink = float(a.Rk);
                g2.tanLo = float(std::tan(a.plo * d2r));
                g2.tanHi = float(std::tan(a.phi * d2r));
                g2.betaMin = float(a.b0 * d2r);
                g2.betaMax = float(a.b1 * d2r);
                g2.width = float(a.w);
                g2.strength = float(a.st);
                g2.ext = float(a.ext * d2r);
                g2.gas = float(a.gas);
            }
        }
    }
    auto blob = [&](V3 rel, double sigma, float3 emit) {
        if (v.nBlobs >= kMaxBlobs) return;
        if (emit.x + emit.y + emit.z <= 0.0f) return;
        // its numbers squared must stay floats (a neighbour 10^20 screens away: 10^40 overflows to inf, inf/inf = NaN)
        if (length(rel) > 1e12 * D || sigma > 1e12 * D) return;
        const double spx = sigma / length(rel) / pa;
        const float wB = float(smoothstep(1.0, 2.0, spx));
        if (wB > 0.0f) {
            GBlob& g = v.blobs[v.nBlobs++];
            g.c = toCamSpace(rel);
            g.sigma = float(sigma / D);
            g.emit = emit * wB;
        }
        if (wB < 1.0f && dot(rel, cam.fwd) > 0.0) {   // small: a point with the same light
            const float3 fl = emit * float(2.0 * kPi * spx * spx) * (1.0f - wB);
            if (fl.x + fl.y + fl.z > 1e-5f) splats.push_back({toCamSpace(rel), fl, float(std::sqrt(0.5625 + spx * spx))});
        }
    };
    if (s > 19.0) {
        auto galBlob = [&](int node, double sigma, double lum, double fade) {
            const V3 rel = tree.rel(cam.ref, node) - cam.pos;
            const double col = lum / (2.0 * kPi * sigma * sigma) / (4.0 * kPi);
            blob(rel, sigma, blackbody(4500.0) * float(col * k * fade));
        };
        // (the Galaxy's bulge is in its model: v.galaxy)
        galBlob(kLMC, 1.5e3 * kPc, 1.5e9 * kLsun, 1.0);
        galBlob(kSMC, 0.9e3 * kPc, 4.6e8 * kLsun, 1.0);
    }
    // the cosmic web as a volume (the field's handle is set by the app: v.web.field)
    v.web = GWeb{};
    if (s > 23.8) {
        GWeb& w = v.web;
        const V3 rel = tree.rel(cam.ref, kSun) - cam.pos;
        w.on = 1;
        w.c = toCamSpace(rel);
        const double mpcM = 1e6 * kPc;
        w.R = float(4.40e26 / D);
        w.mpc = float(mpcM / D);
        // the second lookup turned by a fixed odd rotation
        const V3 ax = normalize(v3(0.62, -0.31, 0.72)), ay0 = normalize(cross(ax, v3(0.1, 0.9, 0.2)));
        const V3 az = cross(ax, ay0);
        w.bx = dirCam(ax);
        w.by = dirCam(ay0);
        w.bz = dirCam(az);
        // 1e8 L_sun per Mpc^3 (luminosity density of galaxies, visual), isotropic: per camera unit of path
        const double j = 1e8 * kLsun / (mpcM * mpcM * mpcM) / (4.0 * kPi);
        w.j0 = blackbody(5600.0) * float(j * D * k * smoothstep(23.8, 24.6, s));
        // (1 + z)^-4 against comoving distance: LambdaCDM (H0 70, Omega_m 0.3), D_C(z) by quadrature, inverted
        {
            const double H0 = 70.0, c = 299792.458, Om = 0.3, OL = 0.7;
            std::vector<double> zs, dc;
            double acc = 0.0, zprev = 0.0;
            zs.push_back(0.0);
            dc.push_back(0.0);
            for (int i = 1; i <= 4000; ++i) {
                const double z = std::pow(10.0, -3.0 + 6.0 * i / 4000.0) - 1e-3;   // to ~1000
                const double zm = 0.5 * (z + zprev);
                acc += (z - zprev) / std::sqrt(Om * std::pow(1.0 + zm, 3.0) + OL);
                zs.push_back(z);
                dc.push_back(acc * c / H0);   // Mpc
                zprev = z;
            }
            for (int i = 0; i <= 32; ++i) {
                const double r = 4.40e26 / mpcM * i / 32.0;
                size_t a = 0;
                while (a + 1 < dc.size() && dc[a + 1] < r) ++a;
                const double z = a + 1 < dc.size() ? zs[a] + (zs[a + 1] - zs[a]) * (r - dc[a]) / std::max(dc[a + 1] - dc[a], 1e-9) : zs.back();
                w.dim[i] = float(std::pow(1.0 + z, -4.0));
            }
        }
    }

    // the edge of the observable universe: the first light (CMB), a thin glowing shell around us (Ö3)
    if (s > 26.4 && v.nBalls < kMaxBalls) {
        const V3 rel = tree.rel(cam.ref, kSun) - cam.pos;
        const double Ru = 4.40e26, dist = length(rel);
        GBall& g = v.balls[v.nBalls++];
        g = GBall{};
        g.c = toCamSpace(rel);
        g.r = float(Ru / D);
        g.h = float((dist - Ru) / D);
        const double out = (dist - Ru) / Ru;   // the camera's height above it, in its radius (it crosses at s ~ 26.5)
        g.emit = make_float3(1.0f, 0.55f, 0.3f) * float(0.012 * smoothstep(0.0, 0.6, out));
        g.pattern = -1;   // a shell of light (see the kernel)
        g.weight = 1.0f;
    }

    // ---- the Earth's air: from the sphere through the beach point (the same as its ball), the Sun's direction and light
    v.atmo = GAtmo{};
    if ((hiz.on ? length(tree.rel(cam.ref, kEarth) - cam.pos) < 2e9 : s > 1.5) && s < 9.5) {
        GAtmo& a = v.atmo;
        const double Re = 6.371e6;
        const V3 up = rotate(earthUp_, earthPole_, clk.spin);
        const V3 rel = tree.rel(cam.ref, kBeach) - up * Re - cam.pos;
        const double dist = length(rel);
        a.on = 1;
        a.c = toCamSpace(rel);
        a.r = float(Re / D);
        a.h = float((dist - Re) / D);
        a.top = float(100e3 / D);
        const V3 toSun = tree.rel(kEarth, kSun);
        const double ds = length(toSun);
        a.sun = dirCam(toSun * (1.0 / ds));
        a.E = make_float3(1.0f, 0.96f, 0.92f) * float(kLsun / (4.0 * kPi * ds * ds) * k);   // the Sun's colour above the air (about)
        a.metres = float(D);
        a.fade = hiz.on ? 1.0f : float(smoothstep(1.5, 2.5, s));
    }

    // ---- the ground at the beach: the Earth's surface there as a plane, in metres from the beach point (PLAN §6, 1)
    GGround& gr = v.ground;
    gr = GGround{};
    if (s < 8.5 && s > -13.6 && !hiz.on) {   // the axes: the plane below ~3 km, the local maps on the sphere above, the atoms' net
        gr.frame = 1;
        gr.on = s < 3.5 ? 1 : 0;
        const V3 up = rotate(earthUp_, earthPole_, clk.spin), east = rotate(earthEast_, earthPole_, clk.spin);
        const V3 north = rotate(earthNorth_, earthPole_, clk.spin);
        gr.o = toCamSpace(tree.rel(cam.ref, kBeach) - cam.pos);
        gr.up = dirCam(up);
        gr.east = dirCam(east);
        gr.north = dirCam(north);
        gr.metres = float(D);
        gr.beach = float(1.0 - smoothstep(2.5, 3.5, s));
        const double grainPx = 2.0 * 2.5e-4 / W * width;
        gr.grains = float(smoothstep(4.0, 8.0, grainPx));
        gr.grainR = 2.5e-4f;
        gr.surface = float(smoothstep(-2.8, -3.8, s));
        gr.steps = float(smoothstep(-6.3, -7.3, s));   // below the light's wavelength: the microscope's eye, over a decade
        const double latPx = 4.9e-10 / W * width;
        gr.atoms = float(smoothstep(10.0, 30.0, latPx));   // the atoms once they are well apart (the terraces before)
        const V3 toSun = normalize(tree.rel(kBeach, kSun));
        gr.sunDir = dirCam(toSun);
        gr.sunLit = float(kLsun / (4.0 * kPi * kAU * kAU) * 0.75 / kPi * k);   // 0.75: the air's transmission at noon
        gr.lattice = 4.913e-10f;
        gr.atomSigma = 0.55e-10f;
        // the lattice's vibration as the shutter sees it (the home atom: the nucleus's own swing)
        const double R = rate(s);
        double kept = 0.0;
        for (int i = 0; i < 3; ++i) kept += keptBy(vibOmega_[i] * R, shutter) / 3.0;
        gr.jitter = float(0.06e-10 * kept);
        gr.vibPhase = float(clk.vib[0]);
        // into the atom: the ground gives way to its clouds over a decade
        gr.fade = float(smoothstep(-10.5, -9.5, s));
        // not drawn once the density map covers it (from -9.0) or it has given way; its frame stays: the map's lattice
        // is read in it
        if (gr.fade <= 0.0f || s < -9.0) gr.on = 0;
    }
    // the swash's clock (the beach's own)
    v.beachClock = float(clk.beach);

    // ---- the inner world as density maps (Ö4-Ö5): the silica net's electrons, the nucleus, the proton
    v.dens = GDensity{};
    // the quark as a probe of the scale sees it: 0.07 fm down to the proton, then sharper by 1.6 decades a decade (no
    // size has been found down to ~10^-19 m), to a few pixels by 10^-19 (the field's picture takes it over on the way)
    const double sigQ = 0.07e-15 * (s > -15.5 ? 1.0 : std::pow(10.0, 1.6 * (s + 15.5)));
    if (s < -8.2 && s > -10.1) {
        GDensity& dn = v.dens;
        dn.on = 1;
        // the crystal's map, giving way to the atom's particles (olcek_inner.cpp) over -9.3 .. -10.1
        dn.weight = float(smoothstep(-8.2, -9.0, s) * (1.0 - smoothstep(-9.3, -10.1, s)));
        const double a0 = 0.529177e-10, fm = 1e-15;
        const V3 nuc = tree.rel(cam.ref, kNucleus) - cam.pos;     // the home silicon's nucleus
        const V3 east = earthEast_, north = earthNorth_, up = earthUp_;
        auto add = [&](V3 rel, int type, double unit, float r, float a, float amp) {
            if (dn.n >= kMaxDensity) return;
            GDensityBody& b = dn.b[dn.n++];
            b.c = toCamSpace(rel);
            b.type = type;
            b.scale = float(D / unit);
            b.r = r;
            b.a = a;
            b.amp = amp * float(std::pow(a0 / unit, 3.0));   // per its unit^3 -> per a0^3
        };
        // the top layer of a silica bilayer (the face the camera comes down on): silicons on a honeycomb, Si-Si 3.06 A, a
        // bridging oxygen between each pair (a little raised), an apical oxygen under each silicon. The net is drawn on
        // the GPU from the AFM's lattice (latticeDensity); the home silicon is a body here, at its nucleus (its swing)
        if (s > -13.5) {
            // the net only while it is on the screen: below 10^-11 its share at the home nucleus is ~10^-3 (O's tail at
            // 1.6 A against the K shell), a hundredth of a line's width in log10
            dn.lattice = s > -11.0 ? v.ground.frame : 0;
            add(nuc, 0, a0, 0, 0, 1);
        }
        // the nucleus: silicon-28's 28 nucleons, the time average, Woods-Saxon (R = 3.07 fm, a = 0.52 fm, its charge's
        // fit) with rho0 = 28 / (4/3 pi R^3 (1 + pi^2 a^2 / R^2)) = 0.176 per fm^3
        add(nuc, 2, fm, 3.07f, 0.52f, 0.176f);
        // a proton near its middle and, in a fast probe's snapshot (deep inelastic scattering sees the partons frozen),
        // its three valence quarks joined by a Y of gluon flux
        const float snap = float(smoothstep(-13.3, -14.0, s));   // the snapshot comes in over the nucleus's last decade
        if (snap > 0.0f) {
            const V3 pr = tree.rel(cam.ref, kProton) - cam.pos;
            add(pr, 3, fm, 0, 0.485f, 0.556f * snap);   // one proton: rms radius 0.84 fm (sigma 0.485), 1 / ((2 pi)^1.5 sigma^3)
            const V3 q = tree.rel(cam.ref, kQuark) - cam.pos;
            const V3 qs[3] = {q, pr + east * 0.30e-15 + north * 0.12e-15, pr + north * -0.32e-15 + east * 0.05e-15};
            V3 junction = (qs[0] + qs[1] + qs[2]) * (1.0 / 3.0);
            for (int i = 0; i < 3; ++i) {
                const double sq = i == 0 ? sigQ / fm : 0.07;   // one quark: 1 / ((2 pi)^1.5 sigma^3)
                add(qs[i], 3, fm, 0, float(sq), float(snap / (15.7496 * sq * sq * sq)));
                if (dn.n < kMaxDensity) {
                    add(qs[i], 4, fm, 0, 0.06f, 6.0f * snap);
                    dn.b[dn.n - 1].c2 = toCamSpace(junction);
                }
            }
        }
        // the fill's range: designed for each scale like the exposure (PLAN §5: a function of s, never read from the
        // picture; OLCEK_DEBUG prints what the screen holds, which these were set from)
        const DensKey* k = kDensRange;
        while (k[1].s > s && k + 2 < kDensRange + sizeof(kDensRange) / sizeof(kDensRange[0])) ++k;
        const double u = std::clamp((k[0].s - s) / (k[0].s - k[1].s), 0.0, 1.0);
        dn.lo = float(k[0].lo + (k[1].lo - k[0].lo) * u);
        dn.hi = float(k[0].hi + (k[1].hi - k[0].hi) * u);
        dn.relief = 0.01f;
        // the shells (Laplacian) while atoms are the view: from the lattice down to where only the K shell is left
        dn.shells = float(smoothstep(-8.6, -9.2, s) * (1.0 - smoothstep(-11.2, -11.8, s)));
        dn.pxA0 = float(W / width / a0);
    }

    // ---- the inner world: designed light, seen by "the instrument of its scale" (PLAN §5)
    const float into = float(smoothstep(-9.3, -10.3, s));
    if (s < -9.0 && !densityMap) {
        const V3 nuc = tree.rel(cam.ref, kNucleus) - cam.pos;
        const float3 grey = make_float3(1, 1, 1);
        // the home atom's electrons: the valence cloud, the L and K shells (Ö4: Clementi-Raimondi densities)
        const double shells[3][2] = {{0.62e-10, 0.30}, {0.16e-10, 0.22}, {0.028e-10, 0.16}};
        for (const auto& sh : shells) blob(nuc, sh[0], grey * float(sh[1] * into * scaleWindow(sh[0], W)));
        // the neighbours' valence clouds: the lattice around (a square placeholder; Ö4: the quartz's own)
        const V3 east = earthEast_, north = earthNorth_, up = earthUp_;
        const double a = 4.913e-10;
        const V3 home = tree.rel(cam.ref, kBeach) - cam.pos;
        for (int i = -2; i <= 2; ++i)
            for (int j = -2; j <= 2; ++j)
                for (int l = 0; l >= -1; --l) {
                    if (i == 0 && j == 0 && l == 0) continue;
                    const V3 p = home + east * (i * a) + north * (j * a) + up * (l * a);
                    blob(p, 0.62e-10, grey * float(0.30 * into * scaleWindow(0.62e-10, W)));
                }
        // the nucleus: Si-28, r_rms 3.1 fm (a Gaussian of the same mean square); a point until it resolves
        const double inNuc = smoothstep(-10.8, -11.8, s);
        blob(nuc, 1.8e-15, make_float3(1.0f, 1.0f, 1.0f) * float(0.9 * inNuc * scaleWindow(1.8e-15, W)));
        // a proton (0.84 fm), and in it three quarks (points: no size has ever been measured)
        const double inP = smoothstep(-13.9, -14.7, s);
        const V3 pr = tree.rel(cam.ref, kProton) - cam.pos;
        blob(pr, 0.485e-15, make_float3(1.0f, 1.0f, 1.0f) * float(0.7 * inP * scaleWindow(0.485e-15, W)));
        const double inQ = smoothstep(-14.8, -15.6, s);
        if (inQ > 0.0) {
            const V3 q = tree.rel(cam.ref, kQuark) - cam.pos;
            const V3 others[2] = {pr + earthEast_ * 0.30e-15 + earthNorth_ * 0.12e-15, pr + earthNorth_ * -0.32e-15 + earthEast_ * 0.05e-15};
            for (int i = 0; i < 3; ++i) {
                const V3 p = i == 0 ? q : others[i - 1];
                if (dot(p, cam.fwd) <= 0.0) continue;
                splats.push_back({toCamSpace(p), make_float3(1, 1, 1) * float(90.0 * inQ), 0.8f});
            }
        }
    }
    // ---- the desert and the bottom (Ö5): the field around the home quark (see GField)
    v.field = GField{};
    if (s < -16.3) {
        GField& F = v.field;
        F.on = 1;
        F.weight = float(smoothstep(-16.3, -17.3, s));
        F.logW = float(std::log10(W));
        F.sigma = float(std::max(sigQ / W, 0.0012));
        // the fill's range: from the screen's edge to ten pixels from the quark (or its middle, while the probe's
        // smearing is wider), at the couplings' mean there
        auto meanCoupling = [](double r) {
            const double L = std::log(1.97327e-16 / 91.1876 / r);   // ln(mu / M_Z), mu = hbar c / r
            return (1.0 / (59.0 - 41.0 / 10.0 / (2.0 * kPi) * L) + 1.0 / (29.6 + 19.0 / 6.0 / (2.0 * kPi) * L) +
                    1.0 / (8.47 + 7.0 / (2.0 * kPi) * L)) / 3.0;
        };
        const double A = std::log10(meanCoupling(0.05 * W));
        const double sg2 = double(F.sigma) * F.sigma;
        F.lo = float(A - 2.0 * std::log10(0.55 * 0.55 + sg2));
        F.hi = float(A - 2.0 * std::log10(0.004 * 0.004 + sg2));
        F.relief = 0.01f;
        F.fillGain = 0.07f;   // a dark ground: the vacuum's loops and the quark are the picture (olcek_inner.cpp)
        // the foam: octaves from the screen's width down to the string's size, each displacing by ~ the Planck length
        const double R = rate(s);
        if (kPlanckLength / W > 2e-5) {
            const int jTop = int(std::ceil(-std::log2(2.0 * W)));
            for (int j = jTop; j < jTop + 40 && F.octaves < kMaxOctaves; ++j) {
                const double lam = std::ldexp(1.0, -j);
                const double freq = W / lam;
                if (freq > 400.0 || lam < kLoop) break;
                const double life = lam / kC / R;   // its life on the screen, seconds
                const double kept = std::exp(-2.0 * kPi * kPi * shutter * shutter / (life * life));
                const double amp = kFoamAmp * kPlanckLength / W * kept * smoothstep(kLoop, 3.0 * kLoop, lam);
                if (amp < 1e-4) continue;
                F.index[F.octaves] = j;
                F.freq[F.octaves] = float(freq);
                F.amp[F.octaves] = float(amp);
                F.phase[F.octaves] = float(clk.foam[std::clamp(j - 40, 0, 127)]);
                ++F.octaves;
            }
        }
        // the foam's cells: Planck-sized, living a Planck time (on the screen: t_P / R), averaged away when faster
        {
            const double life = kPlanckLength / kC / R;
            const double kept = std::exp(-2.0 * kPi * kPi * shutter * shutter / (life * life));
            const double amp = kFoamCell * kPlanckLength / W * kept;
            if (amp > 1e-4 && W / kPlanckLength < 400.0) {
                F.cellFreq = float(W / kPlanckLength);
                F.cellAmp = float(amp);
                const int jP = int(std::floor(-std::log2(kPlanckLength)));
                F.cellPhase = float(clk.foam[std::clamp(jP - 40, 0, 127)]);
            }
        }
        // the string: once it is more than a twentieth of the point's smearing
        const double loopR = kLoop / W;
        if (loopR > 0.05 * F.sigma) {
            F.loop = 1;
            double keptA[kModes], blur2 = 0.0;
            for (int n = 0; n < kModes; ++n) {
                const double kept = keptBy((n + 2) * kC / kLoop * R, shutter);
                keptA[n] = kModeAmp[n] * kept;
                blur2 += sqr(kModeAmp[n] * loopR) * (1.0 - kept * kept) * 0.5;
            }
            F.loopBlur = float(std::sqrt(blur2));
            F.line = float(smoothstep(0.5, 3.0, loopR / F.sigma));   // drawn once it is wider than the smear
            F.lineW = 0.0006f;
            for (int k = 0; k < kLoopPoints; ++k) {
                const double ph = 2.0 * kPi * k / kLoopPoints;
                double m = 1.0;
                for (int n = 0; n < kModes; ++n) m += keptA[n] * std::cos((n + 2) * ph + clk.string[n]);
                F.pts[k] = make_float2(float(loopR * m * std::cos(ph)), float(loopR * m * std::sin(ph)));
            }
        }
    }
    buildInner(v);
    // the second option's rocket: a point at the focus (H0; its drawing and the measured world come in H2)
    if (hiz.on && hiz.mark.x + hiz.mark.y + hiz.mark.z > 0.0f) splats.push_back({toCamSpace(cam.pos * -1.0), hiz.mark, 1.6f});
}

// ---------------------------------------------------------------------------------------------------------------------

std::string Scale::lookingAt() const {
    const Name* n = &kNames[0];
    for (const Name& m : kNames)
        if (s >= m.s) n = &m;
    return app::L(n->tr, n->en);
}

namespace {
const char* kSup[10] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
std::string pow10Text(double W) {
    const int e = int(std::floor(std::log10(W)));
    const double m = W / std::pow(10.0, e);
    std::string out = theme::num(m, 1) + " × 10";
    if (e < 0) out += "⁻";
    for (char c : std::to_string(std::abs(e))) out += kSup[c - '0'];
    return out;
}
// 3 significant digits, the language's separators, no trailing ",0" above 10
std::string sig(double v) {
    if (v >= 100.0) return theme::thousands((unsigned long long)std::llround(v));
    if (v >= 10.0) return theme::num(v, 1);
    return theme::num(v, 2);
}
}   // namespace

std::string Scale::widthText() const {
    using app::L;
    const double W = std::pow(10.0, s);
    if (W < 1e-15) return pow10Text(W) + " m";
    if (W < 1e-12) return sig(W * 1e15) + " fm";
    if (W < 1e-9) return sig(W * 1e12) + " pm";
    if (W < 1e-6) return sig(W * 1e9) + " nm";
    if (W < 1e-3) return sig(W * 1e6) + " µm";
    if (W < 1e-2) return sig(W * 1e3) + " mm";
    if (W < 1.0) return sig(W * 1e2) + " cm";
    if (W < 1e3) return sig(W) + " m";
    if (W < 1e9) return sig(W * 1e-3) + " km";
    if (W < 1e12) return sig(W * 1e-9) + L(" milyon km", " million km");
    if (W < 0.1 * kLy) return sig(W * 1e-12) + L(" milyar km", " billion km");
    const double ly = W / kLy;
    if (ly < 1e3) return sig(ly) + L(" ışık yılı", " light years");
    if (ly < 1e6) return sig(ly * 1e-3) + L(" bin ışık yılı", " thousand light years");
    if (ly < 1e9) return sig(ly * 1e-6) + L(" milyon ışık yılı", " million light years");
    return sig(ly * 1e-9) + L(" milyar ışık yılı", " billion light years");
}

// ---------------------------------------------------------------------------------------------------------------------
// --test olcek-hassasiyet: the node's place on the screen as the frame hands it to the GPU (doubles along the tree,
// floats in camera space) against a double-double reference along the same tree. In pixels.

double Scale::precisionError(int node, V3 local) const {
    // the reference: the same walk to the common ancestor, in double-double (absolute positions would not do: 10^24 m
    // and 10^-35 m are 59 digits apart)
    auto relDD = [&](int from, int to) {
        DV a{}, b{};
        int x = from, y = to;
        while (tree.depth[x] > tree.depth[y]) { a = dvAdd(a, tree.off[x]); x = tree.parent[x]; }
        while (tree.depth[y] > tree.depth[x]) { b = dvAdd(b, tree.off[y]); y = tree.parent[y]; }
        while (x != y) {
            a = dvAdd(a, tree.off[x]);
            b = dvAdd(b, tree.off[y]);
            x = tree.parent[x];
            y = tree.parent[y];
        }
        return dvAdd(b, dvNeg(a));
    };
    auto absDD = [&](int n) { return relDD(cam.ref, n); };
    int a, b;
    const double u = focusBlend(a, b);
    // the focus between a and b, and the camera above it (the blend's weights are the same numbers in both)
    const DV pa = absDD(a), pb = absDD(b);
    auto mix = [&](DD x, DD y) {
        // x + u (y - x), with y - x formed in double-double
        DD d = ddAdd(y, ddNeg(x));
        DD t{d.hi * u, d.lo * u};
        return ddAdd(x, t);
    };
    DV F{mix(pa.x, pb.x), mix(pa.y, pb.y), mix(pa.z, pb.z)};
    DV C = dvAdd(F, cam.fwd * -cam.D);
    DV P = dvAdd(absDD(node), local);
    DV r = dvAdd(P, dvNeg(C));
    // the difference is small next to the parts: hi + lo is now exact to ~32 digits of the parts
    const V3 ref{r.x.hi + r.x.lo, r.y.hi + r.y.lo, r.z.hi + r.z.lo};
    // what the frame does: doubles along the tree from the camera's anchor, floats in camera space
    const V3 rel = tree.rel(cam.ref, node) + local - cam.pos;
    const float3 f = make_float3(float(dot(rel, cam.right) / cam.D), float(dot(rel, cam.up) / cam.D), float(dot(rel, cam.fwd) / cam.D));
    const double t = 2.0 * std::tan(0.5 * fovX * kDeg);
    const double fx = double(f.x) / f.z / t * width, fy = double(f.y) / f.z / t * width;
    const double rx = dot(ref, cam.right) / dot(ref, cam.fwd) / t * width, ry = dot(ref, cam.up) / dot(ref, cam.fwd) / t * width;
    return std::hypot(fx - rx, fy - ry);
}

}   // namespace olcek
