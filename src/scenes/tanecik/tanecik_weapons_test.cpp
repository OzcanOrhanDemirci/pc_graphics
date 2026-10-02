// Scene 4 "Tanecik": tests of the weapons and blast module (weapons.h). Each prints measured against reference and
// GECTI / KALDI with its tolerance; the references and their sources are in weapons.cpp's header comment.
//   --scene tanecik --weapons-test [yukler|jel|korunum|patlama|kilic|kirmizi]   (none: all)
#include "scenes/tanecik/weapons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace weapons;

namespace {

constexpr double kFps = 0.3048, kGrain = 64.79891e-6, kInch = 0.0254;

const char* verdict(bool ok) { return ok ? "GECTI" : "KALDI"; }
bool within(double got, double ref, double tol) { return std::fabs(got - ref) <= tol * std::fabs(ref); }

// ---- yukler: the presets, their momentum and energy, the realistic limits, the slider's monotonicity

bool testYukler() {
    std::printf("[yukler] silah basina uc adli yuk, kayma cubugu\n");
    bool ok = true;
    const Gun guns[3] = {Gun::Pistol, Gun::Rifle, Gun::Shotgun};
    const double limit[3] = {0.15, 0.6, 0.35};   // PLAN §3: the most a body of 70 kg could be given (m/s)
    for (int gi = 0; gi < 3; ++gi) {
        const Gun g = guns[gi];
        std::printf("  %s:\n", gunName(g));
        for (int k = 0; k < 3; ++k) {
            const Load& l = namedLoad(g, k);
            std::printf("    %s %-46s %6.2f g x%-2d %5.2f mm %6.1f m/s  p %6.2f N s  E %7.0f J  p/70 kg %.3f m/s\n", k == 1 ? "*" : " ", l.name.c_str(),
                        l.shot.mass * 1e3, l.pellets, l.shot.diameter * 1e3, l.shot.velocity, l.momentum(), l.energy(), maxPush(l));
            std::printf("        kaynak: %s\n", l.source.c_str());
        }
        const double push = maxPush(namedLoad(g, 2));
        const bool lim = push <= limit[gi];
        std::printf("    en guclu: 70 kg'a en cok %.3f m/s (sinir %.2f): %s\n", push, limit[gi], verdict(lim));
        ok &= lim;
        // the slider: momentum and energy never fall as it moves right
        double lastP = 0, lastE = 0;
        bool mono = true;
        for (int i = 0; i <= 200; ++i) {
            const Load l = gunLoad(g, i / 200.0f);
            if (l.momentum() < lastP * (1 - 1e-6) || l.energy() < lastE * (1 - 1e-6)) mono = false;
            lastP = l.momentum();
            lastE = l.energy();
        }
        const bool ends = std::fabs(gunLoad(g, 0.0f).momentum() - namedLoad(g, 0).momentum()) < 1e-9 &&
                          std::fabs(gunLoad(g, 0.5f).momentum() - namedLoad(g, 1).momentum()) < 1e-6 &&
                          std::fabs(gunLoad(g, 1.0f).momentum() - namedLoad(g, 2).momentum()) < 1e-9;
        std::printf("    cubuk: momentum ve enerji tekduze artiyor %s, uclar ve centik adli yuklerde %s\n", verdict(mono), verdict(ends));
        ok &= mono && ends;
    }
    // the plan's approximate table (docs/tanecik/PLAN.md §3) against these
    std::printf("  plandaki yaklasik tabloyla fark (kutle g, hiz m/s):\n");
    struct PlanRow { Gun g; int k; double mass, v; };
    const PlanRow plan[] = {{Gun::Pistol, 0, 2.6, 330}, {Gun::Pistol, 1, 8, 360}, {Gun::Pistol, 2, 19, 450}, {Gun::Rifle, 0, 4, 940},
                            {Gun::Rifle, 1, 9.5, 840}, {Gun::Rifle, 2, 42, 900}, {Gun::Shotgun, 0, 14, 370}, {Gun::Shotgun, 1, 31, 400},
                            {Gun::Shotgun, 2, 60, 370}};
    for (const PlanRow& r : plan) {
        const Load& l = namedLoad(r.g, r.k);
        std::printf("    %-46s plan %5.1f g %4.0f m/s -> %5.1f g %4.0f m/s\n", l.name.c_str(), r.mass, r.v, l.shot.mass * l.pellets * 1e3, l.shot.velocity);
    }
    std::printf("  -> yukler: %s\n", verdict(ok));
    return ok;
}

// ---- jel: penetration, energy in the first 15 cm, peak force, temporary cavity in 10% gelatin vs the cited data

struct GelCase {
    const char* name;
    Projectile p;
    double pen, penTol;       // m, share (pen < 0: not checked)
    double e15, e15Tol;       // J
    double force, forceTol;   // N
    double cavity, cavityTol; // diameter, m
    double cavLo, cavHi;      // depth range of the widest cavity, m (lo < 0: not checked)
    double stopsBy;           // m: must stop within (< 0: not checked)
    const char* ref;
};
Projectile proj(double massKg, double diaM, double lenM, double v, Nose n, float neck = -1, float frag = 0, double exp = 0, float expV = 0) {
    Projectile p;
    p.mass = float(massKg);
    p.diameter = float(diaM);
    p.length = float(lenM);
    p.velocity = float(v);
    p.nose = n;
    p.neckLength = neck;
    p.fragmentFraction = frag;
    p.expandedDiameter = float(exp);
    p.expandVelocity = expV;
    return p;
}
std::vector<GelCase> gelCases() {
    const double buck00 = 11350.0 * 3.14159265 / 6 * std::pow(0.33 * kInch, 3);
    return {
        // the FBI's calibration shot: a .177 steel BB, 5.1 gr (Daisy's figure; 4.5 mm), 590 ft/s -> 8.5 +/- 1 cm
        {"BB .177 (FBI kalibrasyon)", proj(0.330e-3, 4.5e-3, 4.5e-3, 590 * kFps, Nose::Sphere), 0.085, 0.12, -1, 0, -1, 0, -1, 0, -1, 0, -1,
         "[FBI] 8.5 +/- 1 cm"},
        {"00 sacma tanesi 1150 ft/s", proj(buck00, 0.33 * kInch, 0.33 * kInch, 1150 * kFps, Nose::Sphere), 0.48, 0.20, -1, 0, -1, 0, -1, 0, -1, 0, -1,
         "[LG] 18-20 in at 1100-1200 ft/s"},
        {"9 mm 124 gr FMJ", proj(124 * kGrain, 0.355 * kInch, 0.0155, 1150 * kFps, Nose::RoundNose), -1, 0, 233, 0.15, 2100, 0.15, 0.10, 0.25, -1, 0, -1,
         "[GAY] E15 233 J, tepe 2100 N; [KEY] gecici bosluk ~10 cm"},
        // 230 gr hardball at ~830 ft/s (a typical factory figure: an assumption)
        {".45 ACP 230 gr FMJ", proj(230 * kGrain, 0.452 * kInch, 0.0170, 830 * kFps, Nose::RoundNose), 0.76, 0.20, -1, 0, -1, 0, -1, 0, -1, 0, -1,
         "[GD] ~30 in"},
        {"9 mm 127 gr SXT +P+", proj(127 * kGrain, 0.355 * kInch, 0.0155, 1232 * kFps, Nose::HollowPoint, -1, 0, 0.550 * kInch, 240), -1, 0, 475, 0.15, -1, 0,
         -1, 0, -1, 0, 0.33, "[GAY] E15 350 ft lbf (475 J), ~tumu 30 cm icinde; [KEY] 0.550 in"},
        // Gold Dot 124 gr +P at 1220 ft/s (Speer's catalogue figure: not re-read, an assumption)
        {"9 mm 124 gr Gold Dot +P", proj(124 * kGrain, 0.355 * kInch, 0.0155, 1220 * kFps, Nose::HollowPoint, -1, 0, 0.58 * kInch, 240), 0.343, 0.20, -1, 0,
         -1, 0, -1, 0, -1, 0, -1, "[RG] 13.5 in, 0.58 in"},
        // rifle bullet lengths: 18.8 / 23.0 / 28.8 mm (the standard drawings' figures; assumptions)
        {"5.56 M193 (Fackler)", proj(3.6e-3, 0.224 * kInch, 0.0188, 943, Nose::Spitzer, 0.12f, 0.36f), 0.36, 0.20, -1, 0, -1, 0, 0.13, 0.30, -1, 0, -1,
         "[FAC] yol ~36 cm, bosluk ~12-14 cm"},
        {"5.56 M855 (Fackler)", proj(4.0e-3, 0.224 * kInch, 0.0230, 925, Nose::Spitzer, 0.12f, 0.50f), 0.34, 0.20, -1, 0, -1, 0, 0.126, 0.30, -1, 0, -1,
         "[FAC] yol ~34 cm, bosluk ~12.6 cm"},
        {"7.62 M80 (Fackler)", proj(9.7e-3, 0.308 * kInch, 0.0288, 862, Nose::Spitzer, 0.16f, 0.0f), 0.64, 0.20, -1, 0, -1, 0, 0.20, 0.30, 0.20, 0.35, -1,
         "[FAC] yol ~64 cm, bosluk ~20 cm, 20-35 cm'de"},
    };
}

bool runGel(bool quiet, int& failures) {
    failures = 0;
    for (const GelCase& c : gelCases()) {
        const Path P = traceThroughTissue(c.p, 2.0f, 0.0005f);
        const double pen = P.stopped ? P.stopS : P.tissueLength;
        const double e15 = P.energyWithin(0.15f), D = 2.0 * P.maxCavityRadius;
        bool ok = true;
        std::string line;
        char b[256];
        if (c.pen > 0) {
            const bool k = within(pen, c.pen, c.penTol);
            std::snprintf(b, sizeof(b), " yol %.1f cm (ref %.1f +/-%.0f%%)%s", pen * 100, c.pen * 100, c.penTol * 100, k ? "" : " !");
            line += b;
            ok &= k;
        } else {
            std::snprintf(b, sizeof(b), " yol %.1f cm", pen * 100);
            line += b;
        }
        if (c.e15 > 0) {
            const bool k = within(e15, c.e15, c.e15Tol);
            std::snprintf(b, sizeof(b), " E15 %.0f J (ref %.0f +/-%.0f%%)%s", e15, c.e15, c.e15Tol * 100, k ? "" : " !");
            line += b;
            ok &= k;
        }
        if (c.force > 0) {
            const bool k = within(P.peakForce, c.force, c.forceTol);
            std::snprintf(b, sizeof(b), " tepe kuvvet %.0f N (ref %.0f +/-%.0f%%)%s", P.peakForce, c.force, c.forceTol * 100, k ? "" : " !");
            line += b;
            ok &= k;
        }
        if (c.cavity > 0) {
            const bool k = within(D, c.cavity, c.cavityTol);
            std::snprintf(b, sizeof(b), " gecici bosluk %.1f cm (ref %.1f +/-%.0f%%)%s", D * 100, c.cavity * 100, c.cavityTol * 100, k ? "" : " !");
            line += b;
            ok &= k;
        }
        if (c.cavLo > 0) {
            const bool k = P.maxCavityAt >= c.cavLo && P.maxCavityAt <= c.cavHi;
            std::snprintf(b, sizeof(b), " en genis %.0f cm'de (ref %.0f-%.0f)%s", P.maxCavityAt * 100, c.cavLo * 100, c.cavHi * 100, k ? "" : " !");
            line += b;
            ok &= k;
        }
        if (c.stopsBy > 0) {
            const bool k = P.stopped && pen <= c.stopsBy;
            std::snprintf(b, sizeof(b), " %.0f cm icinde durur%s", c.stopsBy * 100, k ? "" : " !");
            line += b;
            ok &= k;
        }
        if (!quiet) std::printf("  %-28s%s  [%s] %s\n", c.name, line.c_str(), c.ref, verdict(ok));
        failures += !ok;
    }
    return failures == 0;
}

bool testJel() {
    std::printf("[jel] %%10 jelatinde yol, ilk 15 cm'de birakilan enerji, tepe kuvvet, gecici bosluk\n");
    int f = 0;
    const bool ok = runGel(false, f);
    // what the presets do in a body: through 30 cm of tissue (a torso), how much stays, with what speed it leaves
    std::printf("  adli yukler 30 cm dokuda (bilgi):\n");
    for (Gun g : {Gun::Pistol, Gun::Rifle, Gun::Shotgun})
        for (int k = 0; k < 3; ++k) {
            const Load& l = namedLoad(g, k);
            const Path P = traceThroughTissue(l.shot, 0.30f, 0.0005f);
            std::printf("    %-46s %s, birakilan %.0f / %.0f J (tane basina), momentum %.2f / %.2f N s%s\n", l.name.c_str(),
                        P.stopped ? "durdu" : "cikti", P.eDeposited, P.e0, P.pDeposited, P.p0,
                        P.stopped ? "" : (" cikis " + std::to_string(int(P.exitSpeed)) + " m/s").c_str());
        }
    std::printf("  -> jel: %s (%d kalan)\n", verdict(ok), f);
    return ok;
}

// ---- korunum: what goes in = what was handed over + what leaves, through air, tissue and bone

bool testKorunum() {
    std::printf("[korunum] hava, doku, kemik, doku, kemik, doku, hava, doku boyunca momentum ve enerji defteri\n");
    // 0.5 m air, 4 cm tissue, 2 cm bone, 20 cm tissue, 2 cm bone, 5 cm tissue, 1 m air, 30 cm tissue
    const MediumAt body = [](float s) {
        const float e[] = {0.5f, 0.54f, 0.56f, 0.76f, 0.78f, 0.83f, 1.83f, 2.13f};
        const Medium m[] = {Medium::Air, Medium::Tissue, Medium::Bone, Medium::Tissue, Medium::Bone, Medium::Tissue, Medium::Air, Medium::Tissue};
        for (int i = 0; i < 8; ++i)
            if (s < e[i]) return m[i];
        return Medium::Air;
    };
    bool ok = true;
    double worstP = 0, worstE = 0;
    for (Gun g : {Gun::Pistol, Gun::Rifle, Gun::Shotgun})
        for (int k = 0; k <= 10; ++k) {
            const Load l = gunLoad(g, k / 10.0f);
            const Path P = traceShot(l.shot, body, 2.5f);
            const double eP = std::fabs(P.p0 - P.pDeposited - P.pOut) / P.p0, eE = std::fabs(P.e0 - P.eDeposited - P.eOut) / P.e0;
            bool neg = false;
            for (const Segment& s : P.segments) neg |= s.dp < -1e-9f || s.dE < -1e-9f;
            worstP = std::max(worstP, eP);
            worstE = std::max(worstE, eE);
            const bool k2 = eP < 1e-5 && eE < 1e-5 && !neg;
            if (k % 5 == 0)
                std::printf("  %-50s p %.3f = %.3f + %.3f, E %.0f = %.0f + %.0f %s\n", l.name.c_str(), P.p0, P.pDeposited, P.pOut, P.e0, P.eDeposited, P.eOut,
                            verdict(k2));
            ok &= k2;
        }
    std::printf("  en buyuk goreli acik: momentum %.1e, enerji %.1e (sinir 1e-5)\n", worstP, worstE);
    std::printf("  -> korunum: %s\n", verdict(ok));
    return ok;
}

// ---- patlama: the metric fits against the English-unit ones (two transcriptions of the same report), a grenade on a person

bool kbAgree(bool quiet) {
    bool ok = true;
    const float Zs[] = {0.3f, 0.5f, 1.0f, 2.0f, 3.0f, 5.0f, 10.0f, 20.0f, 30.0f};
    for (float Z : Zs) {
        const Blast a = kingeryBulmash(1.0f, Z), b = kingeryBulmashImperial(1.0f, Z);
        const double q[5][2] = {{a.incidentPressure, b.incidentPressure}, {a.reflectedPressure, b.reflectedPressure}, {a.incidentImpulse, b.incidentImpulse},
                                {a.reflectedImpulse, b.reflectedImpulse}, {a.arrival, b.arrival}};
        bool k = true;
        double worst = 0;
        for (auto& v : q) {
            const double d = std::fabs(v[0] - v[1]) / std::max(1e-12, std::fabs(v[1]));
            worst = std::max(worst, d);
            k &= d <= 0.05;
        }
        if (!quiet)
            std::printf("  Z %5.1f: Pso %8.1f / %8.1f kPa  Pr %9.1f / %9.1f kPa  is %6.1f / %6.1f  ir %7.1f / %7.1f Pa s  ta %.2f / %.2f ms  en buyuk fark %.1f%% %s\n",
                        Z, a.incidentPressure / 1e3, b.incidentPressure / 1e3, a.reflectedPressure / 1e3, b.reflectedPressure / 1e3, a.incidentImpulse,
                        b.incidentImpulse, a.reflectedImpulse, b.reflectedImpulse, a.arrival * 1e3, b.arrival * 1e3, worst * 100, verdict(k));
        ok &= k;
    }
    return ok;
}

bool testPatlama() {
    std::printf("[patlama] Kingery-Bulmash (Swisdak 1994): metrik ve Ingiliz birimli katsayilar ayni noktada (W 1 kg, sinir %%5)\n");
    bool ok = kbAgree(false);
    // a grenade on a standing person: 0.5 m^2 face-on (1.75 x 0.3 m: an assumption), all of it reflected (the most)
    std::printf("  el bombasi (%.0f g TNT esdegeri) ayakta bir insana, 0.5 m^2 yuz yuze, tumu yansimis (ust sinir):\n", kGrenadeTNT * 1e3);
    for (float R : {1.0f, 2.0f, 5.0f}) {
        const float3 J = blastImpulse(kGrenadeTNT, make_float3(0, 0, 0), make_float3(R, 0, 0), 0.5f, 1.0f);
        const float j = length(J);
        std::printf("    %.0f m: Z %.2f, darbe %.1f N s -> 70 kg'a %.2f m/s\n", R, kingeryBulmash(kGrenadeTNT, R).Z, j, j / 70.0f);
        if (R == 1.0f) ok &= j / 70.0f < 5.0f && j > 0;
        if (R == 5.0f) ok &= j / 70.0f < 0.5f;
    }
    {
        const float j = length(blastImpulse(2.0f, make_float3(0, 0, 0), make_float3(1, 0, 0), 0.5f, 1.0f));
        std::printf("    2 kg TNT, 1 m: %.0f N s -> %.1f m/s (cubugun en ucu)\n", j, j / 70.0f);
    }
    std::printf("  cubuk: 0 -> %.0f g, 0.5 -> %.0f g, 1 -> %.0f g TNT\n", bombCharge(0) * 1e3, bombCharge(0.5f) * 1e3, bombCharge(1) * 1e3);
    std::printf("  -> patlama: %s\n", verdict(ok));
    return ok;
}

// ---- kilic: a light, a medium and a strong swing; a 9 cm limb with a 2 cm bone

bool testKilic() {
    std::printf("[kilic] etkin kutle %.1f kg, usta vurusu %.0f m/s; kesme bedeli doku %.0f kJ/m^2, kemik %.0f kJ/m^2 (varsayim)\n", swordEffectiveMass(),
                swordSpeedCap(), cutCostPerArea(Medium::Tissue) / 1e3, cutCostPerArea(Medium::Bone) / 1e3);
    const float limbR = 0.045f, boneR = 0.01f;
    const float tissueArea = 3.14159265f * (limbR * limbR - boneR * boneR), boneArea = 3.14159265f * boneR * boneR;
    bool ok = true;
    for (float v : {8.0f, 15.0f, swordSpeedCap()}) {
        const float E = swordEnergy(v);
        const float depth = swordDepthInTissue(E, 0.10f);
        // through the limb: the tissue before the bone (half), the bone, the rest
        const float before = 0.5f * tissueArea * cutCostPerArea(Medium::Tissue), bone = boneArea * cutCostPerArea(Medium::Bone);
        const bool parts = E >= before + bone + 0.5f * tissueArea * cutCostPerArea(Medium::Tissue);
        const bool intoBone = E > before;
        std::printf("  %4.1f m/s: %5.0f J, 10 cm kenarla dokuda %.1f cm; kol (9 cm, kemik 2 cm): %s\n", v, E, depth * 100,
                    parts ? "kopar" : intoBone ? "kemikte durur" : "kemige varmaz");
        if (v == 8.0f) ok &= depth >= 0.02f && depth <= 0.05f && !parts;   // a light cut: a few cm, no bone parted
        if (v == 15.0f) ok &= !parts;
        if (v == swordSpeedCap()) ok &= parts;
    }
    std::printf("  kup cubugu: 0 -> %.0f kg, 0.5 -> %.0f kg, 1 -> %.0f kg\n", cubeMass(0), cubeMass(0.5f), cubeMass(1));
    std::printf("  -> kilic: %s\n", verdict(ok));
    return ok;
}

// ---- kirmizi: each test turns red when its mechanism is broken

bool testKirmizi() {
    std::printf("[kirmizi] mekanizma bozulunca testler kirmiziya donmeli\n");
    bool ok = true;
    {
        debug().noYaw = true;
        int f = 0;
        const bool r = runGel(true, f);
        debug().noYaw = false;
        std::printf("  tufek mermisi hic yalpalamazsa jel testi: %s (%d kalan) %s\n", r ? "GECTI" : "KALDI", f, r ? "! beklenmedi" : "(beklenen)");
        ok &= !r;
    }
    {
        debug().dropFragmentMomentum = true;
        const Path P = traceThroughTissue(namedLoad(Gun::Rifle, 0).shot, 0.6f);
        debug().dropFragmentMomentum = false;
        const double eP = std::fabs(P.p0 - P.pDeposited - P.pOut) / P.p0;
        const bool red = eP > 1e-5;
        std::printf("  parcalar momentumunu birakmazsa M855'in defteri %.1e acik: %s\n", eP, red ? "KALDI (beklenen)" : "GECTI ! beklenmedi");
        ok &= red;
    }
    {
        debug().kbCoefficientNudge = 0.1f;
        const bool r = kbAgree(true);
        debug().kbCoefficientNudge = 0;
        std::printf("  bir Kingery-Bulmash katsayisi 0.1 kayarsa iki takim: %s\n", r ? "GECTI ! beklenmedi" : "KALDI (beklenen)");
        ok &= !r;
    }
    std::printf("  -> kirmizi: %s\n", verdict(ok));
    return ok;
}

}   // namespace

int runWeaponsTests(const std::string& only) {
    int failed = 0, run = 0;
    auto t = [&](const char* name, auto f) {
        if (!only.empty() && only != name) return;
        ++run;
        if (!f()) ++failed;
    };
    t("yukler", testYukler);
    t("jel", testJel);
    t("korunum", testKorunum);
    t("patlama", testPatlama);
    t("kilic", testKilic);
    t("kirmizi", testKirmizi);
    if (!run) { std::printf("bilinmeyen test: %s\n", only.c_str()); return 2; }
    std::printf("silah testleri: %d / %d gecti\n", run - failed, run);
    return failed ? 1 : 0;
}
