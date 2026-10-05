// Scene 7 "Ölçek": below the light, as particles (Özcan's revision of 2026-10-05: "art and representation"; colour is
// representational there, PLAN §5's grey overruled by his choice). The sizes, the numbers and the places are true:
// the electron shells' radii from their Slater exponents, the silicon's four bonds to its oxygens, a silicon-28's
// 28 nucleons, a proton's three valence quarks and their gluon flux, the vacuum's loops at every scale with the forces'
// couplings as their mix, the Planck foam. What moves faster than the eye is shown as a snapshot (a fast probe's), the
// only motion the bottom's own (PLAN §4: there the world's time is the screen's). Every particle is fixed by a seed:
// the same picture each frame, nothing random at run time.
#include "scenes/olcek/olcek_world.h"
#include "scenes/olcek/olcek_bugun.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace olcek {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kC = 299792458.0;
constexpr double kA0 = 0.529177e-10;
constexpr double kFm = 1e-15;
constexpr double kPlanck = 1.616255e-35;

double ss01(double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); }
double sstep(double a, double b, double x) { return ss01((x - a) / (b - a)); }

// a fixed sequence (splitmix64): the same numbers on every run
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double u() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)
    double u01() { return std::max(u(), 1e-12); }
    V3 dir() {
        const double z = 2.0 * u() - 1.0, ph = 2.0 * kPi * u(), r = std::sqrt(std::max(1.0 - z * z, 0.0));
        return {r * std::cos(ph), r * std::sin(ph), z};
    }
    double gamma(int k, double theta) {   // integer shape: a sum of exponentials
        double a = 0.0;
        for (int i = 0; i < k; ++i) a -= std::log(u01());
        return a * theta;
    }
};
uint64_t hash3(int64_t i, int64_t j, int64_t k, uint64_t seed) {
    uint64_t h = seed ^ 0x51ED270B27C3A1F5ull;
    for (int64_t v : {i, j, k}) {
        h ^= uint64_t(v) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 29;
    }
    return h;
}

float3 f3(double r, double g, double b) { return make_float3(float(r), float(g), float(b)); }
float3 mix3(float3 a, float3 b, double t) { return a * float(1.0 - t) + b * float(t); }

// the palette (representational): the electrons' shells cool, the nucleons warm and cold, the quarks' colour charges
const float3 kShellK = f3(0.78, 0.62, 1.00), kShellL = f3(0.30, 0.50, 1.00), kShellM = f3(0.18, 0.86, 0.78);
const float3 kOxL = f3(0.38, 0.68, 1.00);
const float3 kSiCore = f3(1.00, 0.76, 0.36), kOCore = f3(1.00, 0.36, 0.26);
const float3 kProtonCol = f3(0.92, 0.38, 0.24), kNeutronCol = f3(0.40, 0.50, 0.66);
const float3 kRed = f3(1.00, 0.20, 0.16), kGreen = f3(0.22, 0.92, 0.34), kBlue = f3(0.24, 0.44, 1.00);
const float3 kCyan = f3(0.20, 0.90, 1.00), kMagenta = f3(1.00, 0.30, 0.90), kYellow = f3(1.00, 0.88, 0.22);
const float3 kWeakA = f3(0.62, 0.36, 1.00), kWeakB = f3(1.00, 0.62, 0.20);
const float3 kEmA = f3(1.00, 0.95, 0.86), kEmB = f3(1.00, 0.78, 0.48);

// the frame's projection and its list (depth, particle), sorted far to near at the end
struct Canvas {
    float tx, ty, pa;
    int W, H;
    // identities (the test's): the caller names a group (setId); each call after it takes the next number in the
    // group, drawn or not, so a particle keeps its number from frame to frame whatever else is culled
    uint64_t id = 0, idSub = 0;
    void setId(uint64_t g) { id = g; idSub = 0; }
    uint64_t take() { return id ? (id << 6) + idSub++ : 0; }
    struct Item {
        float z;
        GParticle g;
        uint64_t id;
    };
    std::vector<Item> list;
    bool screen(float3 c, float2& p, float margin) const {
        if (c.z <= 1e-6f) return false;
        p.x = (c.x / c.z / tx * 0.5f + 0.5f) * W;
        p.y = (0.5f - c.y / c.z / ty * 0.5f) * H;
        return p.x > -margin && p.x < W + margin && p.y > -margin && p.y < H + margin;
    }
    float px(float rCam, float z) const { return rCam / (z * pa); }
    // a Gaussian cloud of sigma rCam (camera units), its peak col (surface brightness: the same at every distance);
    // below 0.7 pixel it keeps its light
    void glow(float3 c, float rCam, float3 col, float a) {
        const uint64_t myId = take();
        if (a <= 1e-4f || c.z <= 1e-6f) return;
        float s = px(rCam, c.z);
        if (s < 0.7f) {
            a *= (s / 0.7f) * (s / 0.7f);
            s = 0.7f;
        }
        a *= float(1.0 - sstep(0.08 * W, 0.22 * W, s));   // a cloud filling the screen would only wash it
        if (a * fmaxf(col.x, fmaxf(col.y, col.z)) < 2e-4f) return;
        float2 p;
        if (!screen(c, p, 3.5f * s)) return;
        GParticle g;
        g.kind = kPartGlow;
        g.c = p;
        g.r = s;
        g.col = col;
        g.alpha = a;
        list.push_back({c.z, g, myId});
    }
    // a point of fixed size on the screen (an electron's place): sigma in pixels
    void dot(float3 c, float sigmaPx, float3 col, float a) {
        const uint64_t myId = take();
        if (a <= 1e-4f) return;
        float2 p;
        if (!screen(c, p, 4.0f * sigmaPx)) return;
        GParticle g;
        g.kind = kPartGlow;
        g.c = p;
        g.r = sigmaPx;
        g.col = col;
        g.alpha = a;
        list.push_back({c.z, g, myId});
    }
    void sphere(float3 c, float rCam, float3 col, float3 rim, float a) {
        const uint64_t myId = take();
        if (a <= 1e-4f || c.z <= 1e-6f) return;
        const float r = px(rCam, c.z);
        if (r < 1.2f) {   // below a pixel or so: its light as a glow (under its own number)
            const uint64_t keep = idSub;
            idSub = myId ? (myId & 63) : idSub;
            glow(c, rCam * 0.6f, col * 0.75f, a);
            idSub = keep;
            return;
        }
        a *= float(1.0 - sstep(0.6 * W, 1.3 * W, r));
        if (a <= 1e-4f) return;
        float2 p;
        if (!screen(c, p, r + 2.0f)) return;
        GParticle g;
        g.kind = kPartSphere;
        g.c = p;
        g.r = r;
        g.col = col;
        g.col2 = rim;
        g.alpha = a;
        list.push_back({c.z, g, myId});
    }
    // a circle of radius rCam with normal n (camera axes): an ellipse on the screen; width in pixels
    void ring(float3 c, float rCam, float3 n, float3 colA, float3 colB, float widthPx, float a, float phase, float maxW = 0.25f) {
        const uint64_t myId = take();
        if (a <= 1e-4f || c.z <= 1e-6f) return;
        const float r = px(rCam, c.z);
        if (r < 1.0f) {
            const uint64_t keep = idSub;
            idSub = myId ? (myId & 63) : idSub;
            glow(c, rCam, mix3(colA, colB, 0.5) * 0.5f, a);
            idSub = keep;
            return;
        }
        a *= float(1.0 - sstep(maxW * W, 2.2 * maxW * W, r));
        if (a <= 1e-4f) return;
        float2 p;
        if (!screen(c, p, r + 4.0f * widthPx)) return;
        float mx = -n.y, my = n.x;
        const float ml = std::sqrt(mx * mx + my * my);
        if (ml < 1e-4f) { mx = 1.0f; my = 0.0f; } else { mx /= ml; my /= ml; }
        GParticle g;
        g.kind = kPartRing;
        g.c = p;
        g.r = r;
        g.col = colA;
        g.col2 = colB;
        g.w = widthPx;
        g.ax = make_float2(mx, -my);   // screen y runs down
        g.q = std::fabs(n.z);
        g.alpha = a;
        g.phase = phase;
        list.push_back({c.z, g, myId});
    }
    void segment(float3 c0, float3 c1, float wCam, float3 col0, float3 col1, float a) {
        const uint64_t myId = take();
        if (a <= 1e-4f || c0.z <= 1e-6f || c1.z <= 1e-6f) return;
        float2 p0, p1;
        const bool on0 = screen(c0, p0, 1e9f), on1 = screen(c1, p1, 1e9f);
        if (!on0 || !on1) return;
        const float z = 0.5f * (c0.z + c1.z);
        float w = px(wCam, z);
        if (w < 0.7f) {
            a *= w / 0.7f;
            w = 0.7f;
        }
        a *= float(1.0 - sstep(0.03 * W, 0.1 * W, w));
        if (a <= 1e-4f) return;
        const float m = 4.0f * w;
        if (std::max(p0.x, p1.x) < -m || std::min(p0.x, p1.x) > W + m || std::max(p0.y, p1.y) < -m || std::min(p0.y, p1.y) > H + m) return;
        GParticle g;
        g.kind = kPartSegment;
        g.c = p0;
        g.ax = make_float2(p1.x - p0.x, p1.y - p0.y);
        g.w = w;
        g.col = col0;
        g.col2 = col1;
        g.alpha = a;
        list.push_back({z, g, myId});
    }
};

// ---- the silicon atom and its four oxygens, as electrons: the shells' radial distributions r^2n e^(-2 zeta r)
// (Clementi-Raimondi zeta, the same as the density map's) sampled as points; the valence shell along the four bonds
// (sp3: three bridging oxygens, the apical one below). Offsets in the local frame (east, north, up), metres.
struct Dot {
    V3 off;
    int atom;    // 0 the home silicon (about its nucleus), 1-4 its oxygens, 5-7 the neighbouring silicons
    int shell;   // 0 K, 1 L, 2 M
};
struct Atoms {
    V3 at[8];                // the atoms' places from the home's mean place (local frame)
    bool oxygen[8];
    std::vector<Dot> dots;
    V3 bonds[4];             // the home silicon's four bond directions (local frame)
};
const Atoms& atoms() {
    static Atoms A = [] {
        Atoms a;
        const double ss = 3.06e-10, bo = 0.3e-10, apical = 1.61e-10;
        a.at[0] = {0, 0, 0};
        a.oxygen[0] = false;
        const double ang[3] = {90.0, 210.0, 330.0};
        for (int i = 0; i < 3; ++i) {
            const double g = ang[i] * kPi / 180.0;
            a.at[1 + i] = {0.5 * ss * std::cos(g), 0.5 * ss * std::sin(g), bo};
            a.oxygen[1 + i] = true;
            a.at[5 + i] = {ss * std::cos(g), ss * std::sin(g), 0.0};
            a.oxygen[5 + i] = false;
        }
        a.at[4] = {0, 0, -apical};
        a.oxygen[4] = true;
        for (int i = 0; i < 4; ++i) a.bonds[i] = normalize(a.at[1 + i]);
        Rng rng(0x5110C0DEull);
        auto shell = [&](int atom, int sh, int n, double zeta, int count, const V3* lobes, int nl) {
            for (int k = 0; k < count; ++k) {
                const double r = rng.gamma(2 * n + 1, 1.0 / (2.0 * zeta)) * kA0;
                V3 d = rng.dir();
                if (nl > 0) d = normalize(lobes[rng.next() % uint64_t(nl)] + d * 0.62);
                a.dots.push_back({d * r, atom, sh});
            }
        };
        // the home silicon: K (2 electrons), L (8), M (4, along its bonds)
        shell(0, 0, 1, 13.5745, 260, nullptr, 0);
        shell(0, 1, 2, 4.94, 900, nullptr, 0);
        shell(0, 2, 3, 1.53, 900, a.bonds, 4);
        for (int o = 1; o <= 4; ++o) {
            shell(o, 0, 1, 7.6579, 120, nullptr, 0);
            const V3 back = normalize(a.at[0] - a.at[o]);   // the oxygen's pair towards the silicon
            const V3 lobes[3] = {back, normalize(V3{0, 0, 1} - back * 0.3), normalize(V3{0, 0, -1} - back * 0.3)};
            shell(o, 1, 2, 2.236, 520, lobes, 3);
        }
        for (int n = 5; n <= 7; ++n) {
            shell(n, 1, 2, 4.94, 260, nullptr, 0);
            shell(n, 2, 3, 1.53, 420, nullptr, 0);
        }
        return a;
    }();
    return A;
}

// ---- silicon-28: 14 protons and 14 neutrons packed about the centre (a snapshot: they cross it in ~10^-23 s), the
// one the camera goes into among them at its place; nucleon radius 0.84 fm (the proton's charge radius), centres at
// least 1.7 fm apart, within 3.0 fm (the nucleus's 3.07 fm half-density radius)
struct Nucleon {
    V3 off;          // from the nucleus's centre (local frame), metres
    bool proton;
    float3 quark[3];   // its quarks' colours (a snapshot of their colour charges)
    V3 qoff[3];       // and their places inside it
};
const std::vector<Nucleon>& nucleons(V3 target) {
    static std::vector<Nucleon> N;
    if (!N.empty()) return N;
    Rng rng(0x28A28A28ull);
    auto quarks = [&](Nucleon& n) {
        const float3 perm[6][3] = {{kRed, kGreen, kBlue}, {kGreen, kBlue, kRed}, {kBlue, kRed, kGreen},
                                   {kRed, kBlue, kGreen}, {kGreen, kRed, kBlue}, {kBlue, kGreen, kRed}};
        const int k = int(rng.next() % 6);
        const V3 a = rng.dir();
        V3 b = normalize(cross(a, rng.dir()));
        const V3 c = cross(a, b);
        for (int i = 0; i < 3; ++i) {
            n.quark[i] = perm[k][i];
            const double th = 2.0 * kPi * i / 3.0 + 0.3 * rng.u();
            n.qoff[i] = (b * std::cos(th) + c * std::sin(th)) * (0.32e-15 * (0.8 + 0.4 * rng.u()));
        }
    };
    Nucleon t;
    t.off = target;
    t.proton = true;
    quarks(t);
    N.push_back(t);
    int protons = 1;
    for (int tries = 0; tries < 200000 && N.size() < 28; ++tries) {
        const V3 p = rng.dir() * (3.0e-15 * std::cbrt(rng.u()));
        bool ok = true;
        for (const Nucleon& m : N)
            if (length(p - m.off) < 1.7e-15) { ok = false; break; }
        if (!ok) continue;
        Nucleon n;
        n.off = p;
        n.proton = protons < 14 && N.size() % 2 == 0;   // 13 more: 14 protons, 14 neutrons
        if (n.proton) ++protons;
        quarks(n);
        N.push_back(n);
    }
    return N;
}

// the couplings at length r (metres): one loop, the Standard Model's (as the field's)
void couplings(double r, double& a1, double& a2, double& a3) {
    const double L = std::log(1.97327e-16 / 91.1876 / r);
    a1 = 1.0 / std::max(59.0 - 41.0 / 10.0 / (2.0 * kPi) * L, 1.0);
    a2 = 1.0 / std::max(29.6 + 19.0 / 6.0 / (2.0 * kPi) * L, 1.0);
    a3 = 1.0 / std::max(8.47 + 7.0 / (2.0 * kPi) * L, 1.0);
}

}   // namespace

bool Scale::loadBelts(const std::string& path, std::string& log) {
    std::ifstream f(path);
    if (!f) {
        log += "  kusaklar.txt yok (tools/olcek_veri.py asteroit): kusaklarsiz\n";
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string kind, name;
        in >> kind >> name;
        if (kind == "ring") {
            BeltRing& b = name == "main" ? beltMain : beltKuiper;
            int n = 0;
            in >> b.r0 >> b.dr >> n;
            b.sigma.resize(size_t(n));
            b.zrms.resize(size_t(n));
            for (int i = 0; i < n && std::getline(f, line); ++i) {
                std::istringstream v(line);
                v >> b.sigma[size_t(i)] >> b.zrms[size_t(i)];
            }
        } else if (kind == "trojan") {
            const int k = name == "L4" ? 0 : 1;
            in >> trojanN[k] >> trojanLon[k] >> trojanSig[k];
        }
    }
    char b[160];
    std::snprintf(b, sizeof(b), "  kusaklar.txt: ana kusak %zu halka, Kuiper %zu, Truvalilar %.0f + %.0f%c", beltMain.sigma.size(),
                  beltKuiper.sigma.size(), trojanN[0], trojanN[1], 10);
    log += b;
    return !beltMain.sigma.empty();
}

void Scale::buildInner(OlcekView& v) {
    particles.clear();
    particleTrack.clear();
    const bool belts = s > 10.2 && s < 15.6;
    if (s > -8.6 && !belts) return;
    Canvas cv;
    cv.tx = v.tanHalfX;
    cv.ty = v.tanHalfY;
    cv.pa = v.pixAngle;
    cv.W = width;
    cv.H = height;
    const double D = cam.D, W = cam.W;
    auto camOf = [&](V3 r) { return make_float3(float(dot(r, cam.right) / D), float(dot(r, cam.up) / D), float(dot(r, cam.fwd) / D)); };
    auto dirOf = [&](V3 r) { return make_float3(float(dot(r, cam.right)), float(dot(r, cam.up)), float(dot(r, cam.fwd))); };
    auto local = [&](V3 o) { return earthEast_ * o.x + earthNorth_ * o.y + earthUp_ * o.z; };
    // a particle near the camera fades (it passes it); camera units
    auto nearFade = [&](float z, float r) { return float(sstep(0.04, 0.2, z) * sstep(2.0, 5.0, z / std::max(r, 1e-9f))); };

    // ---- the Solar System's belts and the Sun's halo (10.2 .. 15.6). At these scales an asteroid goes round faster than
    // the eye's shutter: the belts are their time average, rings of the surface density of 1.5 million orbits (MPCORB),
    // the Kuiper belt's 6,000 the same; Jupiter's Trojans two swarms 60 degrees before and behind it (they go round with
    // it). Their light designed (a representation: dust and rocks, far fainter in truth), their places measured.
    if (belts) {
        const double AU = 1.495978707e11;
        const V3 sunRel = tree.rel(cam.ref, kSun) - cam.pos;
        const float3 cs = camOf(sunRel);
        const V3 eclN = normalize(V3{today::kEclipticNorth[0], today::kEclipticNorth[1], today::kEclipticNorth[2]});
        const float3 nCam = dirOf(eclN);
        auto beltRings = [&](const BeltRing& b, double sLo, double sHi, float3 colA, float3 colB, float gain, uint64_t idBase) {
            const double w = sstep(sLo, sLo + 0.5, s) * (1.0 - sstep(sHi - 0.5, sHi, s));
            if (w <= 0.0 || b.sigma.empty()) return;
            float peak = 0.0f;
            for (float x : b.sigma) peak = std::max(peak, x);
            for (size_t i = 0; i < b.sigma.size(); ++i) {
                const double r = (b.r0 + b.dr * (i + 0.5)) * AU;
                const float rCam = float(r / D);
                const float rPx = cv.px(rCam, std::max(cs.z, 1e-6f));
                if (rPx < 4.0f) continue;
                const float binPx = cv.px(float(b.dr * AU / D), std::max(cs.z, 1e-6f));
                const float thickPx = cv.px(float(b.zrms[i] * AU / D), std::max(cs.z, 1e-6f)) * std::sqrt(std::max(0.0f, 1.0f - nCam.z * nCam.z));
                const float wPx = std::max(0.8f, std::sqrt(sqr(0.85f * binPx) + sqr(0.15f * thickPx)));
                const float dens = std::pow(b.sigma[i] / std::max(peak, 1e-6f), 0.7f);
                // neighbouring rings overlap: each carries its bin's share so the band's light is gain x density
                const float a = float(w) * gain * dens * std::min(1.0f, binPx / (2.507f * wPx));
                if (a < 1e-4f) continue;
                cv.setId(idBase + i);
                cv.ring(cs, rCam, nCam, colA, colB, wPx, a, float(i) * 0.37f, 0.6f);
            }
        };
        beltRings(beltMain, 10.6, 13.9, f3(0.95, 0.82, 0.62), f3(0.80, 0.74, 0.66), 0.045f, 0x6000000ull);
        beltRings(beltKuiper, 12.5, 15.6, f3(0.62, 0.74, 0.95), f3(0.70, 0.70, 0.80), 0.04f, 0x6100000ull);
        // the Trojans: a swarm's clouds along Jupiter's orbit about L4 / L5
        {
            const double w = sstep(11.1, 11.6, s) * (1.0 - sstep(13.4, 13.9, s));
            const int jup = kMercury + 4;
            const V3 jr = tree.rel(kSun, jup);
            const double rj = length(jr);
            if (w > 0.0 && rj > 0.0) {
                const V3 u = normalize(jr), t = normalize(cross(eclN, u));
                const double nMax = std::max(trojanN[0], trojanN[1]);
                for (int k = 0; k < 2; ++k) {
                    for (int m = -4; m <= 4; ++m) {
                        const double lon = (trojanLon[k] + m * 0.55 * trojanSig[k]) * kPi / 180.0;
                        const V3 pos = sunRel + (u * std::cos(lon) + t * std::sin(lon)) * rj;
                        const float3 c = camOf(pos);
                        const float g = float(std::exp(-0.5 * sqr(m * 0.55)) * trojanN[k] / std::max(nMax, 1.0));
                        cv.setId(0x6200000ull + uint64_t(k * 16 + m + 8));
                        cv.glow(c, float(rj * 0.35 * trojanSig[k] * kPi / 180.0 * 0.55 / D), f3(0.95, 0.80, 0.58), float(w) * 0.035f * g);
                    }
                }
            }
        }
        // the Sun's halo: the eye's glare about the brightest thing there is (three widths, faint)
        {
            const double w = sstep(10.2, 10.7, s) * (1.0 - sstep(15.0, 15.6, s));
            cv.setId(0x6300000ull);
            cv.dot(cs, 5.0f, f3(1.0, 0.93, 0.80), float(w) * 0.9f);
            cv.dot(cs, 26.0f, f3(1.0, 0.86, 0.66), float(w) * 0.10f);
            cv.dot(cs, 110.0f, f3(1.0, 0.80, 0.58), float(w) * 0.018f);
        }
    }

    // ---- the atom: its electrons and its orbitals (from -8.8, the crystal map giving way, to -12.6)
    const double wAtom = s > -8.6 ? 0.0 : sstep(-8.8, -9.7, s) * (1.0 - sstep(-12.1, -12.7, s));
    if (wAtom > 0.0) {
        const Atoms& A = atoms();
        const V3 nuc = tree.rel(cam.ref, kNucleus) - cam.pos, home = tree.rel(cam.ref, kBeach) - cam.pos;
        V3 at[8];
        at[0] = nuc;
        for (int i = 1; i < 8; ++i) at[i] = home + local(A.at[i]);
        // the electrons' places: points of fixed size, their shell's colour, fading where they pass the camera
        const float3 shellCol[3] = {kShellK, kShellL, kShellM};
        const float shellB[3] = {1.1f, 0.85f, 0.8f};
        uint64_t di = 0;
        for (const Dot& d : A.dots) {
            cv.setId(0x1000000ull + di++);
            const float3 c = camOf(at[d.atom] + local(d.off));
            if (c.z <= 0.0f) continue;
            const float fade = float(sstep(0.03, 0.18, c.z)) * std::min(1.0f, 1.6f / std::sqrt(std::max(c.z, 0.05f)));
            const float3 col = (A.oxygen[d.atom] && d.shell == 1) ? kOxL : shellCol[d.shell];
            cv.dot(c, 1.25f, col, float(wAtom) * shellB[d.shell] * fade);
        }
        // the orbitals as clouds (their light the density's, projected): K, L (with its limb), the bonds' lobes
        for (int i = 0; i < 8; ++i) {
            cv.setId(0x2000000ull + uint64_t(i) * 16);
            const bool ox = A.oxygen[i];
            const float3 c = camOf(at[i]);
            if (c.z <= 0.0f) continue;
            const double zK = ox ? 7.6579 : 13.5745, zL = ox ? 2.236 : 4.94;
            const float rK = float(0.75 / zK * kA0 / D), rL = float(2.0 / zL * kA0 / D);
            cv.glow(c, rK, ox ? kOCore * 0.6f + kShellK * 0.4f : kShellK, float(wAtom) * (ox ? 0.18f : 0.35f) * nearFade(c.z, rK));
            cv.glow(c, rL * 0.7f, ox ? kOxL : kShellL, float(wAtom) * 0.06f * nearFade(c.z, rL));
            // the nucleus itself: a point of its element's colour
            cv.glow(c, float(3e-15 / D), ox ? kOCore : kSiCore, float(wAtom) * 2.0f);
        }
        // the home silicon's four bonds: two clouds along each (to the oxygens)
        cv.setId(0x2100000ull);
        for (int b = 0; b < 4; ++b) {
            for (int k = 0; k < 2; ++k) {
                const double t = k == 0 ? 0.42e-10 : 0.88e-10, sg = k == 0 ? 0.22e-10 : 0.3e-10;
                const float3 c = camOf(nuc + local(A.bonds[b] * t));
                cv.glow(c, float(sg / D), kShellM, float(wAtom) * 0.05f * nearFade(c.z, float(sg / D)));
            }
        }
    }

    // ---- the nucleus: 28 nucleons (from -11.2, a point, to -15, the last ones passing)
    const double wNuc = s > -8.6 ? 0.0 : sstep(-11.2, -11.8, s) * (1.0 - sstep(-14.6, -15.2, s));
    if (wNuc > 0.0) {
        const V3 nuc = tree.rel(cam.ref, kNucleus) - cam.pos;
        const std::vector<Nucleon>& N = nucleons(tree.off[kProton]);
        const float rN = float(0.84e-15 / D);
        for (size_t i = 0; i < N.size(); ++i) {
            cv.setId(0x3000000ull + uint64_t(i) * 16);
            const Nucleon& n = N[i];
            const V3 pos = i == 0 ? tree.rel(cam.ref, kProton) - cam.pos : nuc + n.off;
            const float3 c = camOf(pos);
            if (c.z <= 0.0f) continue;
            const float rPx = cv.px(rN, c.z);
            // a nucleon opens as the camera nears it (the one it enters becomes the proton's inside)
            const float open = float(1.0 - sstep(0.18 * width, 0.55 * width, rPx));
            const float a = float(wNuc) * nearFade(c.z, rN) * (i == 0 ? 0.9f * open : 0.25f + 0.65f * open);
            const float3 base = n.proton ? kProtonCol : kNeutronCol;
            cv.sphere(c, rN, base * 0.55f, base * 0.35f, a);
            cv.glow(c, rN * 1.3f, base, a * 0.10f * open);
            // its quarks inside, a little in front of its middle so they show through
            if (rPx > 6.0f && i != 0) {
                for (int q = 0; q < 3; ++q) {
                    float3 cq = camOf(pos + n.qoff[q]);
                    cq.z -= rN * 0.9f;
                    cv.glow(cq, float(0.12e-15 / D), n.quark[q], a * 0.9f * float(sstep(6.0, 30.0, rPx)));
                }
            }
        }
    }

    // ---- the proton's inside (from -13.8): three valence quarks (u, u, d: colour charges red, green, blue), their gluon
    // flux as a Y, and its sea: gluons and quark-antiquark pairs (a deep inelastic snapshot)
    const double wPro = sstep(-13.7, -14.4, s) * (1.0 - sstep(-16.2, -16.9, s));
    if (wPro > 0.0) {
        const V3 pr = tree.rel(cam.ref, kProton) - cam.pos, q0 = tree.rel(cam.ref, kQuark) - cam.pos;
        const V3 qs[3] = {q0, pr + local(V3{0.30e-15, 0.12e-15, -0.05e-15}), pr + local(V3{0.05e-15, -0.32e-15, 0.06e-15})};
        const float3 qc[3] = {kRed, kGreen, kBlue};
        const V3 junction = (qs[0] + qs[1] + qs[2]) * (1.0 / 3.0);
        const float3 cj = camOf(junction);
        const double sharpen = s > -15.0 ? 1.0 : std::pow(10.0, 1.6 * (s + 15.0));   // the probe's resolution
        for (int i = 0; i < 3; ++i) {
            cv.setId(0x4100000ull + uint64_t(i));
            const float3 c = camOf(qs[i]);
            const float rq = float(0.09e-15 * sharpen / D);
            const float f = float(wPro) * nearFade(c.z, rq);
            const float own = i > 0 ? 1.0f : float(1.0 - sstep(-14.85, -15.25, s));   // the up quark: then its point
            cv.glow(c, rq, qc[i], f * 1.6f * own);
            cv.glow(c, rq * 3.5f, qc[i], f * 0.22f * own);
            const float tube = f * float(1.0 - sstep(-14.85, -15.3, s));
            cv.segment(c, cj, float(0.05e-15 * sharpen / D), qc[i] * 0.8f, f3(0.9, 0.9, 0.95) * 0.6f, tube * 0.55f);
            cv.segment(c, cj, float(0.12e-15 * sharpen / D), qc[i] * 0.4f, f3(0.5, 0.5, 0.55) * 0.3f, tube * 0.35f);
        }
        // the sea: within the proton (Gaussian, sigma 0.45 fm): 70 gluons (two-coloured loops), 30 pairs
        Rng rng(0xC0FFEE11ull);
        const float3 cols[3] = {kRed, kGreen, kBlue}, anti[3] = {kCyan, kMagenta, kYellow};
        for (int k = 0; k < 100; ++k) {
            cv.setId(0x4000000ull + uint64_t(k) * 4);
            V3 p{0, 0, 0};
            for (int a = 0; a < 3; ++a) {
                const double g = std::sqrt(-2.0 * std::log(rng.u01())) * std::cos(2.0 * kPi * rng.u());
                (a == 0 ? p.x : a == 1 ? p.y : p.z) = g * 0.42e-15;
            }
            const V3 pos = pr + p;
            const float3 c = camOf(pos);
            const double size = (0.04 + 0.08 * rng.u()) * 1e-15 * sharpen;
            const float fade = float(wPro) * nearFade(c.z, float(size / D)) * float(1.0 - sstep(0.06 * width, 0.14 * width, cv.px(float(size / D), c.z)));
            const int a = int(rng.next() % 3), b = (a + 1 + int(rng.next() % 2)) % 3;
            if (k < 70) {
                cv.ring(c, float(size / D), dirOf(rng.dir()), cols[a], cols[b], std::clamp(cv.px(float(size * 0.1 / D), c.z), 0.9f, 3.0f),
                        fade * 0.6f, float(2.0 * kPi * rng.u()));
            } else {
                const V3 d = rng.dir() * size;
                cv.glow(camOf(pos + d), float(size * 0.35 / D), cols[a], fade * 0.8f);
                cv.glow(camOf(pos - d), float(size * 0.35 / D), anti[a], fade * 0.8f);
            }
        }
    }

    // ---- the up quark and the vacuum about it, at every scale (from -15.4 to the bottom): loops of virtual pairs, a
    // Poisson field in octaves anchored to the quark (each octave's loops a size 2^-j m, as many to a volume as to its
    // own size cubed: the same picture at every scale, "nothing new"). Their kind by the forces' couplings at that size
    // (strong: two of red, green, blue; weak: violet and amber; electromagnetic: warm white), so the mix evens out on the
    // way down. A snapshot: they live ~ their size / c, far below the eye's shutter.
    const double wVac = sstep(-15.2, -16.2, s);
    {   // the quark itself, from where it leaves its proton: a point (no size has been found), a halo of its colour
        const double wq = sstep(-14.85, -15.25, s);
        if (wq > 0.0) {
            cv.setId(0x5000000ull);
            const float3 cq = camOf(tree.rel(cam.ref, kQuark) - cam.pos);
            cv.dot(cq, 2.0f, f3(1.0, 0.75, 0.7) * 1.6f, float(wq));
            cv.dot(cq, 7.0f, kRed, float(wq) * 0.35f);
            cv.dot(cq, 26.0f, kRed, float(wq) * 0.06f);
        }
    }
    if (wVac > 0.0) {
        const V3 qRel = tree.rel(cam.ref, kQuark) - cam.pos;   // camera -> quark (metres)
        const double pa = v.pixAngle;
        const V3 qCam{dot(qRel, cam.right), dot(qRel, cam.up), dot(qRel, cam.fwd)};
        const int jLo = int(std::floor(-std::log2(W / 4.0))), jHi = int(std::ceil(-std::log2(W / 600.0)));
        int count = 0;
        for (int j = jLo; j <= jHi; ++j) {
            const double lam = std::ldexp(1.0, -j);
            if (lam < 0.5 * kPlanck) break;
            // its depth band: loops 5..260 pixels across, within the frustum between 0.06 D and 4 D
            const double z0 = std::max(0.15 * D, lam / (260.0 * pa)), z1 = std::min(4.0 * D, lam / (5.0 * pa));
            if (z1 <= z0) continue;
            // the octave itself comes in and goes out gently at the ends of its range (W/500 .. W/6 of the screen)
            const double xo = std::log2(W / lam);
            const double wOct = sstep(2.4, 3.4, xo) * (1.0 - sstep(8.3, 9.1, xo));
            if (wOct <= 0.0) continue;
            const double L = 14.0 * lam;   // a super-cell, ~0.15 loops in it
            const double hx = z1 * v.tanHalfX, hy = z1 * v.tanHalfY;
            // the cells in the quark's frame (the camera's axes, which never turn): their range over the slab
            const int64_t i0 = int64_t(std::floor((-hx - qCam.x) / L)), i1 = int64_t(std::floor((hx - qCam.x) / L));
            const int64_t k0 = int64_t(std::floor((-hy - qCam.y) / L)), k1 = int64_t(std::floor((hy - qCam.y) / L));
            const int64_t m0 = int64_t(std::floor((z0 - qCam.z) / L)), m1 = int64_t(std::floor((z1 - qCam.z) / L));
            if ((i1 - i0 + 1) * (k1 - k0 + 1) * (m1 - m0 + 1) > 400000) continue;
            double a1, a2, a3;
            couplings(lam, a1, a2, a3);
            const double sum = a1 + a2 + a3;
            for (int64_t i = i0; i <= i1; ++i)
                for (int64_t k = k0; k <= k1; ++k)
                    for (int64_t m = m0; m <= m1; ++m) {
                        Rng r(hash3(i, k, m, uint64_t(j) * 0x9E37ull + 77));
                        if (r.u() > 0.15) continue;
                        const V3 pq{(double(i) + r.u()) * L, (double(k) + r.u()) * L, (double(m) + r.u()) * L};
                        const V3 pc = qCam + pq;   // camera axes, metres
                        if (pc.z < z0 || pc.z > z1) continue;
                        const float3 c = make_float3(float(pc.x / D), float(pc.y / D), float(pc.z / D));
                        const double size = lam * (0.35 + 0.3 * r.u());
                        const float rPx = float(size / (pc.z * pa));
                        // fade in as it grows past a few pixels, out before it leaves its band (90 px at the least)
                        // and before it is so near the camera that it would cross the screen in a few frames
                        // and before the band's far edge (4 D: it comes nearer four times as fast as the camera does)
                        const float f = float(wVac * wOct * sstep(4.0, 12.0, rPx) * (1.0 - sstep(55.0, 85.0, rPx)) * sstep(0.15, 0.45, c.z) *
                                              (1.0 - sstep(2.6, 3.9, c.z)));
                        if (f <= 0.0f) continue;
                        cv.setId(hash3(i, k, m, uint64_t(j) * 0x9E37ull + 77) | 1ull);
                        const double pick = r.u() * sum;
                        float3 ca, cb;
                        if (pick < a3) {
                            const float3 cols[3] = {kRed, kGreen, kBlue};
                            const int a = int(r.next() % 3);
                            ca = cols[a];
                            cb = cols[(a + 1 + int(r.next() % 2)) % 3];
                        } else if (pick < a3 + a2) {
                            ca = kWeakA;
                            cb = kWeakB;
                        } else {
                            ca = kEmA;
                            cb = kEmB;
                        }
                        const V3 nrm = r.dir();
                        cv.ring(c, float(size / D), make_float3(float(nrm.x), float(nrm.y), float(nrm.z)), ca, cb,
                                std::clamp(rPx * 0.05f, 0.8f, 3.0f), f * 0.55f, float(2.0 * kPi * r.u()));
                        if (++count > 60000) break;   // a guard only (~4-8 thousand are drawn)
                    }
        }
    }

    // ---- the bottom: spacetime foam, bubbles a Planck length across about the quark (Wheeler: there the geometry itself
    // fluctuates by order one). Each lives a Planck time: on the screen t_P / R, so they boil only at the very bottom;
    // before that the probe's snapshot holds them still. Their walls in thin-film colours (a representation). In 3D
    // their number on the screen grows as (W / L_P)^3: hundreds half a decade up. So a layer about the focus, from
    // where the screen is a few Planck lengths (above it the field's foam cells are the texture).
    const double wFoam = sstep(-33.9, -34.5, s);
    if (wFoam > 0.0) {
        const V3 qRel = tree.rel(cam.ref, kQuark) - cam.pos;
        const V3 qCam{dot(qRel, cam.right), dot(qRel, cam.up), dot(qRel, cam.fwd)};
        const double pa = v.pixAngle, R = rate(s);
        const double life = kPlanck / kC / R;   // screen seconds
        const double kept = std::exp(-2.0 * kPi * kPi * shutter * shutter / (life * life));
        const double clock = foamClock_ * std::min(kept * 4.0, 1.0);   // still while faster than the eye
        for (int o = 0; o < 2; ++o) {
            const double lam = kPlanck * (o == 0 ? 0.6 : 1.2);
            const double L = 0.9 * lam;   // cells across the view (a layer: each bubble's depth near the focus's)
            const double hx = 1.2 * D * v.tanHalfX, hy = 1.2 * D * v.tanHalfY;
            const int64_t i0 = int64_t(std::floor((-hx - qCam.x) / L)), i1 = int64_t(std::floor((hx - qCam.x) / L));
            const int64_t k0 = int64_t(std::floor((-hy - qCam.y) / L)), k1 = int64_t(std::floor((hy - qCam.y) / L));
            if ((i1 - i0 + 1) * (k1 - k0 + 1) > 4000) continue;
            for (int64_t i = i0; i <= i1; ++i)
                for (int64_t k = k0; k <= k1; ++k) {
                        Rng r(hash3(i, k, 0, 0xF0A7ull + uint64_t(o)));
                        if (r.u() > 0.8) continue;
                        const V3 pc{qCam.x + (double(i) + r.u()) * L, qCam.y + (double(k) + r.u()) * L, qCam.z + (r.u() - 0.5) * 0.5 * D};
                        // its life: grows from nothing, holds, bursts (a fraction of its cycle, its own offset)
                        const double ph = clock * (0.6 + 0.8 * r.u()) + r.u();
                        const double f = ph - std::floor(ph);
                        const double grow = sstep(0.0, 0.45, f) * (1.0 - sstep(0.85, 1.0, f));
                        const double size = lam * (0.35 + 0.4 * r.u()) * (0.4 + 0.6 * grow);
                        const float3 c = make_float3(float(pc.x / D), float(pc.y / D), float(pc.z / D));
                        const float rPx = float(size / (pc.z * pa));
                        const float a = float(wFoam * grow * sstep(10.0, 30.0, rPx));
                        if (a <= 0.0f) continue;
                        // thin-film colours: a hue from its thickness (its own number), two around the wall
                        const double hue = r.u();
                        auto film = [](double h) {
                            return f3(0.55 + 0.45 * std::cos(2.0 * kPi * (h + 0.00)), 0.55 + 0.45 * std::cos(2.0 * kPi * (h + 0.33)),
                                      0.55 + 0.45 * std::cos(2.0 * kPi * (h + 0.67)));
                        };
                        const V3 nrm = r.dir();
                        cv.setId(hash3(i, k, 0, 0xF0A7ull + uint64_t(o)) | 1ull);
                        cv.ring(c, float(size / D), make_float3(float(nrm.x), float(nrm.y), float(nrm.z)), film(hue), film(hue + 0.35),
                                std::clamp(rPx * 0.03f, 1.0f, 4.0f), a * 0.7f, float(2.0 * kPi * r.u()), 0.42f);
                        cv.glow(c, float(size * 0.55 / D), film(hue + 0.15) * 0.5f, a * 0.06f);
                    }
        }
    }

    std::sort(cv.list.begin(), cv.list.end(), [](const auto& a, const auto& b) { return a.z > b.z; });
    particles.reserve(cv.list.size());
    particleTrack.clear();
    for (const auto& e : cv.list) {
        particles.push_back(e.g);
        if (e.id) {
            const float3 m = e.g.kind == kPartRing ? make_float3(fmaxf(e.g.col.x, e.g.col2.x), fmaxf(e.g.col.y, e.g.col2.y), fmaxf(e.g.col.z, e.g.col2.z)) : e.g.col;
            particleTrack.push_back({e.id, e.g.alpha * fmaxf(m.x, fmaxf(m.y, m.z)), e.g.c.x, e.g.c.y});
        }
    }
}

}   // namespace olcek
