#include "scenes/laboratuvar/weapons.h"

#include <algorithm>
#include <cmath>

// Sources (read 2026-10-02 unless noted). Every other number is marked an assumption where it is used.
//  [FBI]   FBI ballistic gelatin protocol (via police1.com, "Comparing FBI ballistic gelatin with synthetic ballistic
//          gelatin"): 10% ordnance gelatin is calibrated with a .177 steel BB at 590 +/- 15 ft/s penetrating 8.5 cm.
//  [GAY]   Gaylord, Blair, Courtney, Courtney 2013, "Bullet Retarding Forces in Ballistic Gelatin by Analysis of High
//          Speed Video", arXiv:1305.5215: 9mm NATO 124 gr FMJ peak retarding force 2100 N, E15 = 172 ft lbf (233 J);
//          127 gr Winchester Ranger SXT (impact 1232 ft/s, 425 ft lbf) E15 = 350 ft lbf, ~all its energy by 30 cm.
//  [KEY]   Keys et al. 2015, arXiv:1508.05843: 9mm FMJ temporary cavity max ~10 cm; expanding bullets reach full
//          diameter at ~2 cm.  [COU] Courtney et al. 2016, arXiv:1604.01000: expanding bullets ~8000 N, cavity 12.5 cm.
//  [FAC]   Fackler, "Wounding patterns of military rifle bullets", International Defense Review 1/1989 (archive.org):
//          M193 943 m/s 3.6 g, yaw at ~12 cm, 36% fragmentation, path ~36 cm; M855 925 m/s 4 g -> 2 g, profile as M193;
//          US M80 862 m/s 9.7 g, point-forward ~16 cm, big temporary cavity at 20-35 cm, path drawn to 64 cm;
//          fragments up to 7 cm from the path; West German 7.62 cavity ~22 cm ("M16 profile x 1.6").
//  [GD]    Gun Digest, "Making The Case For .45 ACP 'Hardball'": 230 gr FMJ ~30 in in 10% gelatin.
//  [RG]    Ronin's Grips blog (9mm 147 vs 124 +P): Speer Gold Dot 124 gr +P 13.5 in, 0.58 in expanded (secondary).
//  [LG]    Lucky Gunner Lounge, "Buckshot sizes ...": #00 at 1100-1200 ft/s penetrates 18-20 in of gel.
//  [SI]    Shooting Illustrated, "Buckshot Basics": cylinder bore spreads ~1 in per yard (a conservative rule of thumb).
//  [FED]   federalpremium.com: Power-Shok F127 00 (12 ga 2-3/4 in, 9 pellets, 1325 ft/s); Buckshot P135F 00 (12 ga
//          3-1/2 in, 18 pellets, 1100 ft/s); Personal Defense 410 Handgun PD412JGE 000 (2-1/2 in, 4 pellets, 850 ft/s).
//          American Eagle AE9AP 9 mm 124 gr FMJ: 1150 ft/s, 364 ft lbf (retailer listings of Federal's data).
//  [HOR]   Hornady Custom .50 AE 300 gr XTP (#9245): 1475 ft/s (Wikipedia ".50 Action Express").
//  [TM]    TM 43-0001-27, Army Ammunition Data Sheets, Small Caliber: M80 147 gr 2750 ft/s; M33 .50 ball 2910 ft/s;
//          M855 62 gr. (M855's 3025 ft/s and M33's 661 gr bullet: the standard figures, not re-read in the manual.)
//  [CCI]   CCI Mini-Mag 40 gr CPRN: 1235 ft/s (CCI's catalogue figure, from a rifle barrel; not re-read).
//  [KB]    Kingery and Bulmash 1984, ARBRL-TR-02555, in Swisdak 1994 "Simplified Kingery Airblast Calculations"
//          (NSWC-TR-5081, ADA526744): coefficients as implemented in the 'kingery-bulmash' package (github
//          fcento100/kingery-bulmash); the English-unit incident-pressure set agrees with arXiv:2511.16816 Table S7.
//  [M67]   Wikipedia "M67 grenade": 6.5 oz (180 g) Composition B. [SVJ] Strojniski vestnik 63(2017)11 666-672:
//          Composition B TNT equivalence 1.11 (pressure), 0.98 (impulse).
//  [STEM]  swordstem.com "How Fast Do Swords Move?" (2018): the cutting part of the blade peaks at ~20 m/s.

namespace weapons {

namespace {

constexpr double kFps = 0.3048;                 // ft/s -> m/s
constexpr double kGrain = 64.79891e-6;          // gr -> kg
constexpr double kInch = 0.0254;
constexpr double kLead = 11350.0;               // kg/m^3

// the penetration model's constants, fitted in 10% gelatin (the tissue stand-in; see testJel)
constexpr float kCdSphere = 0.41f;    // with kGelStrength: the FBI BB (8.5 cm) and #00 buck (~45 cm) both
constexpr float kCdRoundNose = 0.50f; // 9mm FMJ: 2100 N at impact and E15 233 J [GAY]; .45 FMJ ~70 cm vs ~76 [GD]
constexpr float kCdSpitzer = 0.20f;   // point-forward rifle bullet
constexpr float kCdSide = 1.20f;      // sideways (a short cylinder broadside)
constexpr float kCdBase = 1.00f;      // base-forward after the yaw
constexpr float kCdHollow = 0.55f;    // opened hollow point: SXT E15 [GAY], Gold Dot 13.5 in [RG]
constexpr float kYawPer90 = 0.15f;    // m of tissue per 90 deg of yaw (fitted to [FAC]'s paths and cavities)
constexpr float kFragmentAtDeg = 60.0f;
constexpr float kFragmentSpread = 0.07f;   // [FAC]
constexpr float kExpandLength = 0.02f;     // [KEY]
// temporary cavity diameter D = max(width, Dref (F / Fref)^b): 10 cm at 2100 N [GAY, KEY]; b through 12.5 cm at
// 8000 N [COU]. It gives 14-16 cm for yawing rifle bullets (Fackler: ~13-20 cm): within ~25%.
constexpr float kCavityRef = 0.10f, kCavityForce = 2100.0f, kCavityExp = 0.167f;

const MediumProps kAir = {1.2f, 0.0f};
const MediumProps kTissue = {1040.0f, 1.46e6f};   // 10% gelatin's density; strength fitted with kCdSphere
const MediumProps kBone = {1900.0f, 80.0e6f};     // assumption: cortical bone density, an effective resistance

Load make(const char* name, double massKg, double diaM, double lenM, double fps, Nose nose, float neck, float frag, double expDiaM,
          float expV, int pellets, float spread, const char* source) {
    Load l;
    l.name = name;
    l.shot.mass = float(massKg);
    l.shot.diameter = float(diaM);
    l.shot.length = float(lenM);
    l.shot.velocity = float(fps * kFps);
    l.shot.nose = nose;
    l.shot.neckLength = neck;
    l.shot.fragmentFraction = frag;
    l.shot.expandedDiameter = float(expDiaM);
    l.shot.expandVelocity = expV;
    l.pellets = pellets;
    l.spreadPerMetre = spread;
    l.source = source;
    return l;
}
double leadBall(double d) { return kLead * 3.14159265358979 / 6.0 * d * d * d; }

const std::vector<Load>& table(Gun g) {
    static const std::vector<Load> pistol = {
        // length 11.6 mm: an assumption (a 40 gr round nose); speed from a rifle barrel (a pistol gives ~10-15% less)
        make(".22 LR 40 gr CPRN (CCI Mini-Mag)", 40 * kGrain, 0.223 * kInch, 0.0116, 1235, Nose::RoundNose, -1, 0, 0, 0, 1, 0,
             "[CCI] 1235 ft/s, 40 gr; length assumed"),
        make("9 mm 124 gr FMJ (Federal AE9AP)", 124 * kGrain, 0.355 * kInch, 0.0155, 1150, Nose::RoundNose, -1, 0, 0, 0, 1, 0,
             "[FED] 1150 ft/s, 124 gr; length 15.5 mm assumed"),
        // opens to 1.5x (0.75 in) above 240 m/s: assumptions
        make(".50 AE 300 gr XTP (Hornady 9245, Desert Eagle)", 300 * kGrain, 0.500 * kInch, 0.019, 1475, Nose::HollowPoint, -1, 0,
             0.75 * kInch, 240.0f, 1, 0, "[HOR] 1475 ft/s, 300 gr; length 19 mm, expansion to 0.75 in assumed")};
    static const std::vector<Load> rifle = {
        make("5.56x45 M855 62 gr", 62 * kGrain, 0.224 * kInch, 0.0230, 3025, Nose::Spitzer, 0.12f, 0.50f, 0, 0, 1, 0,
             "[TM] 62 gr, 3025 ft/s; yaw at 12 cm and 50% fragments [FAC]"),
        make("7.62x51 M80 147 gr", 147 * kGrain, 0.308 * kInch, 0.0288, 2750, Nose::Spitzer, 0.16f, 0.0f, 0, 0, 1, 0,
             "[TM] 147 gr, 2750 ft/s; point-forward 16 cm, no fragments (US) [FAC]"),
        // its yaw depth is an assumption (a long heavy bullet): it crosses a body anyway
        make(".50 BMG M33 ball 661 gr", 661 * kGrain, 0.511 * kInch, 0.0587, 2910, Nose::Spitzer, 0.25f, 0.0f, 0, 0, 1, 0,
             "[TM] 2910 ft/s, 661 gr; yaw at 25 cm assumed")};
    static const std::vector<Load> shotgun = {
        // a short handgun barrel spreads more than a shotgun's: 5 cm per metre is an assumption
        make(".410 2-1/2 in 000 buck x4 (Federal PD412JGE)", leadBall(0.36 * kInch), 0.36 * kInch, 0.36 * kInch, 850, Nose::Sphere, -1,
             0, 0, 0, 4, 0.05f, "[FED] 4 pellets, 850 ft/s; spread assumed"),
        make("12 ga 2-3/4 in 00 buck x9 (Federal F127)", leadBall(0.33 * kInch), 0.33 * kInch, 0.33 * kInch, 1325, Nose::Sphere, -1, 0,
             0, 0, 9, float(kInch / 0.9144), "[FED] 9 pellets, 1325 ft/s; spread 1 in/yd [SI]"),
        make("12 ga 3-1/2 in 00 buck x18 (Federal P135F)", leadBall(0.33 * kInch), 0.33 * kInch, 0.33 * kInch, 1100, Nose::Sphere, -1,
             0, 0, 0, 18, float(kInch / 0.9144), "[FED] 18 pellets, 1100 ft/s; spread 1 in/yd [SI]")};
    return g == Gun::Pistol ? pistol : g == Gun::Rifle ? rifle : shotgun;
}

float geo(float a, float b, float t) { return a > 0 && b > 0 ? a * std::pow(b / a, t) : a + (b - a) * t; }

}   // namespace

Debug& debug() {
    static Debug d;
    return d;
}

const char* gunName(Gun g) { return g == Gun::Pistol ? "tabanca" : g == Gun::Rifle ? "tufek" : "pompali"; }
const Load& namedLoad(Gun g, int which) { return table(g)[std::min(2, std::max(0, which))]; }

Load gunLoad(Gun g, float u) {
    u = std::min(1.0f, std::max(0.0f, u));
    const int lo = u <= 0.5f ? 0 : 1;
    const float t = u <= 0.5f ? u / 0.5f : (u - 0.5f) / 0.5f;
    const Load &A = namedLoad(g, lo), &B = namedLoad(g, lo + 1);
    if (t <= 0.0f) return A;
    if (t >= 1.0f) return B;
    const Load& N = t < 0.5f ? A : B;   // the nearer one gives the name, the nose and the source
    Load l = N;
    l.name = N.name + " (ara)";
    // the payload (all pellets) and the speed geometrically: momentum and energy rise monotonically; the count is
    // rounded and the pellet's mass made to carry the payload exactly
    const float payload = geo(A.shot.mass * A.pellets, B.shot.mass * B.pellets, t);
    l.pellets = std::max(1, int(std::lround(geo(float(A.pellets), float(B.pellets), t))));
    l.shot.mass = payload / float(l.pellets);
    l.shot.velocity = geo(A.shot.velocity, B.shot.velocity, t);
    l.shot.diameter = geo(A.shot.diameter, B.shot.diameter, t);
    l.shot.length = geo(A.shot.length, B.shot.length, t);
    l.shot.neckLength = A.shot.neckLength >= 0 && B.shot.neckLength >= 0 ? A.shot.neckLength + (B.shot.neckLength - A.shot.neckLength) * t
                                                                         : N.shot.neckLength;
    l.shot.fragmentFraction = A.shot.fragmentFraction + (B.shot.fragmentFraction - A.shot.fragmentFraction) * t;
    if (l.shot.nose == Nose::HollowPoint) {   // keep the opened-to-closed ratio of the hollow point
        const Load& H = A.shot.nose == Nose::HollowPoint ? A : B;
        l.shot.expandedDiameter = l.shot.diameter * H.shot.expandedDiameter / H.shot.diameter;
        l.shot.expandVelocity = H.shot.expandVelocity;
    }
    l.spreadPerMetre = A.spreadPerMetre + (B.spreadPerMetre - A.spreadPerMetre) * t;
    return l;
}

const MediumProps& mediumProps(Medium m) { return m == Medium::Bone ? kBone : m == Medium::Tissue ? kTissue : kAir; }

float Path::energyWithin(float depth) const {
    double e = 0;
    for (const Segment& s : segments) {
        if (s.medium == Medium::Air || s.depth0 >= depth) continue;
        const float f = s.depth1 <= depth ? 1.0f : (depth - s.depth0) / std::max(1e-9f, s.depth1 - s.depth0);
        e += s.dE * f;
    }
    return float(e);
}

Path traceShot(const Projectile& pr, const MediumAt& medium, float maxLength, float ds) {
    Path P;
    double m = pr.mass, v = pr.velocity;
    P.p0 = m * v;
    P.e0 = 0.5 * m * v * v;
    const bool expands = pr.nose == Nose::HollowPoint && pr.expandedDiameter > pr.diameter && pr.velocity >= pr.expandVelocity;
    const bool yaws = pr.nose == Nose::Spitzer && pr.neckLength >= 0.0f && !debug().noYaw;
    double length = pr.length;
    bool fragmented = false;
    float s = 0.0f, depth = 0.0f;
    while (s < maxLength - 1e-7f && !P.stopped) {
        float step = std::min(ds, maxLength - s);
        const Medium md = medium(s + 0.5f * step);
        if (md == Medium::Air) {   // nothing is handed over; consecutive air steps are one segment
            if (!P.segments.empty() && P.segments.back().medium == Medium::Air) P.segments.back().s1 = s + step;
            else {
                Segment a;
                a.s0 = s;
                a.s1 = s + step;
                a.depth0 = a.depth1 = depth;
                a.vIn = a.vOut = float(v);
                a.massIn = a.massOut = float(m);
                P.segments.push_back(a);
            }
            s += step;
            continue;
        }
        Segment g;
        g.s0 = s;
        g.depth0 = depth;
        g.medium = md;
        g.vIn = float(v);
        g.massIn = float(m);
        // the shape at mid-step: yaw (spitzers), opening (hollow points)
        const float dm = depth + 0.5f * step;
        const double th = yaws ? std::min(3.14159265358979, std::max(0.0, double(dm - pr.neckLength) / kYawPer90 * 1.5707963267949)) : 0.0;
        double d = pr.diameter;
        if (expands) d += (pr.expandedDiameter - pr.diameter) * std::min(1.0, double(dm) / kExpandLength);
        // shedding fragments (once, as the yaw passes 60 deg): their momentum and energy are handed over here
        if (yaws && pr.fragmentFraction > 0.0f && !fragmented && th >= kFragmentAtDeg * 3.14159265358979 / 180.0) {
            const double shed = m * pr.fragmentFraction;
            if (!debug().dropFragmentMomentum) {
                g.dp += float(shed * v);
                g.dE += float(0.5 * shed * v * v);
            }
            m -= shed;
            length *= 1.0 - pr.fragmentFraction;   // what is left is shorter
            fragmented = true;
            g.fragmentRadius = kFragmentSpread;
        }
        const double Af = 3.14159265358979 * 0.25 * d * d, As = pr.diameter * length;
        const double c2 = std::cos(th) * std::cos(th), s2 = 1.0 - c2;
        double cdf;
        switch (pr.nose) {
        case Nose::Sphere: cdf = kCdSphere; break;
        case Nose::Spitzer: cdf = th <= 1.5707963267949 ? kCdSpitzer : kCdBase; break;
        case Nose::HollowPoint: cdf = expands ? kCdHollow : kCdRoundNose; break;
        default: cdf = kCdRoundNose; break;
        }
        const double CA = cdf * Af * c2 + kCdSide * As * s2, Ares = Af * c2 + As * s2;
        const MediumProps& M = mediumProps(md);
        // exact over the step for constant coefficients: v^2(x) = (v0^2 + c) exp(-k x) - c
        const double k = M.density * CA / m, c = 2.0 * M.strength * Ares / (M.density * CA);
        const double v2 = (v * v + c) * std::exp(-k * step) - c;
        double vOut;
        if (v2 <= 0.0) {
            step = float(std::log((v * v + c) / c) / k);
            vOut = 0.0;
            P.stopped = true;
        } else {
            vOut = std::sqrt(v2);
        }
        step = std::max(step, 1e-9f);
        g.s1 = s + step;
        g.depth1 = depth + step;
        g.vOut = float(vOut);
        g.massOut = float(m);
        const double eLost = 0.5 * m * (v * v - vOut * vOut);
        g.dp += float(m * (v - vOut));
        g.dE += float(eLost);
        g.force = float(eLost / step);
        g.yawDeg = float(th * 180.0 / 3.14159265358979);
        const float width = float(std::sqrt(Ares * 4.0 / 3.14159265358979));   // the presented area's equivalent diameter
        g.crushRadius = 0.5f * width;
        g.cavityRadius = md == Medium::Tissue ? 0.5f * std::max(width, kCavityRef * std::pow(std::max(g.force, 1.0f) / kCavityForce, kCavityExp))
                                              : g.crushRadius;
        if (g.force > P.peakForce) P.peakForce = g.force;
        if (g.cavityRadius > P.maxCavityRadius) {
            P.maxCavityRadius = g.cavityRadius;
            P.maxCavityAt = 0.5f * (g.depth0 + g.depth1);
        }
        P.segments.push_back(g);
        s += step;
        depth += step;
        v = vOut;
        if (P.stopped) P.stopS = s;
    }
    P.tissueLength = depth;
    if (!P.stopped) {
        P.exitSpeed = float(v);
        P.exitMass = float(m);
        P.pOut = m * v;
        P.eOut = 0.5 * m * v * v;
    }
    for (const Segment& g : P.segments) {
        P.pDeposited += g.dp;
        P.eDeposited += g.dE;
    }
    return P;
}

float radialSpeed(const Segment& seg, float r, float r0, float share) {
    const float len = seg.s1 - seg.s0, R = seg.cavityRadius;
    if (seg.medium == Medium::Air || len <= 0.0f || R <= r0 || r > R || seg.dE <= 0.0f) return 0.0f;
    const float rho = mediumProps(seg.medium).density;
    const float V0 = std::sqrt(share * seg.dE / (3.14159265f * rho * len * r0 * r0 * std::log(R / r0)));
    return V0 * r0 / std::max(r, r0);
}

std::vector<float3> pelletDirections(const Load& l, float3 dir, unsigned seed) {
    std::vector<float3> out;
    dir = normalize(dir);
    float3 t, b;
    makeBasis(dir, t, b);
    unsigned h = seed * 747796405u + 2891336453u;
    auto rnd = [&] {
        h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
        return (h & 0xffffff) / 16777216.0f;
    };
    for (int k = 0; k < l.pellets; ++k) {
        if (l.pellets == 1 || l.spreadPerMetre <= 0.0f) { out.push_back(dir); continue; }
        const float r = 0.5f * l.spreadPerMetre * std::sqrt(rnd()), a = 6.2831853f * rnd();
        out.push_back(normalize(dir + t * (r * std::cos(a)) + b * (r * std::sin(a))));
    }
    return out;
}

// ---- Kingery-Bulmash [KB]

namespace {

struct Fit {
    float zlo, zhi;
    float c[7];
};
// exp(sum c_i (ln Z)^i) over the first fit whose range holds Z (Z clamped into the overall range)
double kb(const Fit* fits, int n, double Z, bool& clamped, double nudge = 0.0) {
    const double lo = fits[0].zlo, hi = fits[n - 1].zhi;
    if (Z < lo || Z > hi) clamped = true;
    Z = std::min(hi, std::max(lo, Z));
    for (int i = 0; i < n; ++i)
        if (Z <= fits[i].zhi || i == n - 1) {
            const double L = std::log(Z);
            double sum = 0, Lk = 1;
            for (int k = 0; k < 7; ++k) {
                sum += (fits[i].c[k] + (k == 0 ? nudge : 0.0)) * Lk;
                Lk *= L;
            }
            return std::exp(sum);
        }
    return 0;
}

// metric: Z m/kg^(1/3); pressures kPa; impulses kPa ms per kg^(1/3); arrival ms per kg^(1/3)
const Fit kPso[] = {{0.2f, 2.9f, {7.2106f, -2.1069f, -0.3229f, 0.1117f, 0.0685f, 0, 0}},
                    {2.9f, 23.8f, {7.5938f, -3.0523f, 0.40977f, 0.0261f, -0.01267f, 0, 0}},
                    {23.8f, 198.5f, {6.0536f, -1.4066f, 0, 0, 0, 0, 0}}};
const Fit kPr[] = {{0.06f, 2.0f, {9.006f, -2.6893f, -0.6295f, 0.1011f, 0.29255f, 0.13505f, 0.019736f}},
                   {2.0f, 40.0f, {8.8396f, -1.733f, -2.64f, 2.293f, -0.8232f, 0.14247f, -0.0099f}}};
const Fit kIs[] = {{0.2f, 0.96f, {5.522f, 1.117f, 0.6f, -0.292f, -0.087f, 0, 0}},
                   {0.96f, 2.38f, {5.465f, -0.308f, -1.464f, 1.362f, -0.432f, 0, 0}},
                   {2.38f, 33.7f, {5.2749f, -0.4677f, -0.2499f, 0.0588f, -0.00554f, 0, 0}},
                   {33.7f, 158.7f, {5.9825f, -1.062f, 0, 0, 0, 0, 0}}};
const Fit kIr[] = {{0.06f, 40.0f, {6.7853f, -1.3466f, 0.101f, -0.01123f, 0, 0, 0}}};
const Fit kTa[] = {{0.06f, 1.5f, {-0.7604f, 1.8058f, 0.1257f, -0.0437f, -0.0310f, -0.00669f, 0}},
                   {1.5f, 40.0f, {-0.7137f, 1.5732f, 0.5561f, -0.4213f, 0.1054f, -0.00929f, 0}}};
// English units: Z ft/lb^(1/3); psi; psi ms per lb^(1/3); ms per lb^(1/3)
const Fit kPsoE[] = {{0.5f, 7.25f, {6.9137f, -1.4398f, -0.2815f, -0.1416f, 0.0685f, 0, 0}},
                     {7.25f, 60.0f, {8.8035f, -3.7001f, 0.2709f, 0.0733f, -0.0127f, 0, 0}},
                     {60.0f, 500.0f, {5.4233f, -1.4066f, 0, 0, 0, 0, 0}}};
const Fit kPrE[] = {{0.3f, 4.0f, {9.0795f, -1.7511f, -0.2877f, -0.2199f, -0.0128f, 0.0696f, -0.0118f}},
                    {4.0f, 100.0f, {5.1515f, 9.15826f, -11.85735f, 5.56754f, -1.33455f, 0.16333f, -0.008181f}}};
const Fit kIsE[] = {{0.5f, 2.41f, {2.975f, -0.466f, 0.963f, 0.03f, -0.087f, 0, 0}},
                    {2.41f, 6.0f, {0.911f, 7.26f, -7.459f, 2.960f, -0.432f, 0, 0}},
                    {6.0f, 85.0f, {3.2484f, 0.1633f, -0.4416f, 0.0793f, -0.00554f, 0, 0}},
                    {85.0f, 400.0f, {4.7702f, -1.062f, 0, 0, 0, 0, 0}}};
const Fit kIrE[] = {{0.2f, 100.0f, {5.9313f, -1.5622f, 0.1322f, -0.01123f, 0, 0, 0}}};
const Fit kTaE[] = {{0.2f, 4.5f, {-2.5671f, 1.5348f, 0.1313f, 0.01825f, 0.003656f, -0.008615f, 0}},
                    {4.5f, 100.0f, {-1.79097f, -0.44021f, 2.01409f, -0.78101f, 0.13045f, -0.0081529f, 0}}};

}   // namespace

Blast kingeryBulmash(float W, float R) {
    Blast b;
    W = std::max(W, 1e-6f);
    const double w3 = std::cbrt(double(W));
    b.Z = float(R / w3);
    bool cl = false;
    b.incidentPressure = float(kb(kPso, 3, b.Z, cl, debug().kbCoefficientNudge) * 1e3);
    b.reflectedPressure = float(kb(kPr, 2, b.Z, cl) * 1e3);
    b.incidentImpulse = float(kb(kIs, 4, b.Z, cl) * w3);     // kPa ms = Pa s
    b.reflectedImpulse = float(kb(kIr, 1, b.Z, cl) * w3);
    b.arrival = float(kb(kTa, 2, b.Z, cl) * w3 * 1e-3);
    b.clamped = cl;
    return b;
}

Blast kingeryBulmashImperial(float W, float R) {
    Blast b;
    const double Wlb = std::max(W, 1e-6f) * 2.20462, Rft = R * 3.28084, w3 = std::cbrt(Wlb);
    const double Zen = Rft / w3;
    b.Z = float(R / std::cbrt(std::max(W, 1e-6f)));
    bool cl = false;
    const double psi = 6894.757;
    b.incidentPressure = float(kb(kPsoE, 3, Zen, cl) * psi);
    b.reflectedPressure = float(kb(kPrE, 2, Zen, cl) * psi);
    b.incidentImpulse = float(kb(kIsE, 4, Zen, cl) * w3 * psi * 1e-3);   // psi ms -> Pa s
    b.reflectedImpulse = float(kb(kIrE, 1, Zen, cl) * w3 * psi * 1e-3);
    b.arrival = float(kb(kTaE, 2, Zen, cl) * w3 * 1e-3);
    b.clamped = cl;
    return b;
}

float3 blastImpulse(float W, float3 charge, float3 p, float area, float facing) {
    const float3 d = p - charge;
    const float R = std::max(1e-3f, length(d));
    const Blast b = kingeryBulmash(W, R);
    const float c = std::max(0.0f, std::min(1.0f, facing));
    const float i = b.incidentImpulse + (b.reflectedImpulse - b.incidentImpulse) * c * c;
    return d * (i * area / R);
}

float bombCharge(float u) {
    u = std::min(1.0f, std::max(0.0f, u));
    return u <= 0.5f ? 0.05f * std::pow(kGrenadeTNT / 0.05f, u / 0.5f) : kGrenadeTNT * std::pow(2.0f / kGrenadeTNT, (u - 0.5f) / 0.5f);
}

// a skilled cut: [STEM]'s ~20 m/s plus a tenth (an assumption); 1.2 kg moving with the edge: an assumption (a katana is
// ~1.1 kg, the hands add some, the centre of percussion moves slower than the tip)
float swordSpeedCap() { return 22.0f; }
float swordEffectiveMass() { return 1.2f; }
// assumptions, chosen so that a light 8 m/s swing cuts ~3 cm with a 10 cm edge, a strong 22 m/s one parts a 9 cm limb
// with a 2 cm bone, and a 15 m/s one stops in that bone (bone takes ~19 m/s and up)
float cutCostPerArea(Medium m) { return m == Medium::Bone ? 450.0e3f : m == Medium::Tissue ? 12.0e3f : 0.0f; }

float cubeMass(float u) { return 10.0f * std::pow(500.0f, std::min(1.0f, std::max(0.0f, u))); }

}   // namespace weapons
