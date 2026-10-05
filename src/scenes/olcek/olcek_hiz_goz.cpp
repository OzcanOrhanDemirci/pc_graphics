// Scene 7 "Ölçek", second option "Hız": the rocket's eye and the colour of light at any speed (see olcek_hiz_goz.h).
#include "scenes/olcek/olcek_hiz_goz.h"
#include "shared/display.h"

#include <algorithm>
#include <cmath>
#include <execution>
#include <fstream>
#include <numeric>
#include <sstream>

namespace olcek::hiz {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kLsun = 3.828e26;
constexpr double kTsun = 5772.0;

// when the CIE table is missing: the multi-lobe fit of the same functions (Wyman, Sloan, Shirley 2013; the one in
// src/shared/spectrum.h, which needs scene 1's constants)
double lobe(double x, double mu, double s1, double s2) {
    const double t = (x - mu) / (x < mu ? s1 : s2);
    return std::exp(-0.5 * t * t);
}
void cmfFit(double l, double& x, double& y, double& z) {
    x = 1.056 * lobe(l, 599.8, 37.9, 31.0) + 0.362 * lobe(l, 442.0, 16.0, 26.7) - 0.065 * lobe(l, 501.1, 20.4, 26.2);
    y = 0.821 * lobe(l, 568.8, 46.9, 40.5) + 0.286 * lobe(l, 530.9, 16.3, 31.1);
    z = 1.217 * lobe(l, 437.0, 11.8, 36.0) + 0.681 * lobe(l, 459.0, 26.0, 13.8);
}
// CIE XYZ to linear Rec.709 (sRGB primaries, D65)
float3 xyzTo709(double X, double Y, double Z) {
    return make_float3(float(3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z), float(-0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z),
                       float(0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z));
}
}   // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The blackbody table

void Blackbody::build(const std::string& cmfCsv) {
    std::vector<double> lam, xb, yb, zb;
    {
        std::ifstream f(cmfCsv);
        std::string line;
        while (std::getline(f, line)) {
            for (char& ch : line)
                if (ch == ',' || ch == ';' || ch == '\t') ch = ' ';
            std::istringstream ss(line);
            double l, x, y, z;
            if (ss >> l >> x >> y >> z && l >= 300.0 && l <= 900.0) {
                lam.push_back(l);
                xb.push_back(x);
                yb.push_back(y);
                zb.push_back(z);
            }
        }
    }
    if (lam.size() >= 80) {
        source = "CIE 1931 2°";
    } else {   // the multi-lobe fit of the same functions, 1 nm
        lam.clear();
        xb.clear();
        yb.clear();
        zb.clear();
        for (int l = 360; l <= 830; ++l) {
            double x, y, z;
            cmfFit(l, x, y, z);
            lam.push_back(l);
            xb.push_back(x);
            yb.push_back(y);
            zb.push_back(z);
        }
        source = "CIE 1931 2° (Wyman-Sloan-Shirley 2013 yaklaşımı)";
    }
    samples = int(lam.size());
    // trapezoid weights (nm)
    std::vector<double> w(lam.size(), 0.0);
    for (size_t j = 0; j + 1 < lam.size(); ++j) {
        const double h = lam[j + 1] - lam[j];
        w[j] += 0.5 * h;
        w[j + 1] += 0.5 * h;
    }
    // Planck in logs (10 K: e^-1700 at 830 nm, far below a double; 10^12 K: Rayleigh-Jeans, expm1 keeps it exact)
    const double c1 = kPlanckC1, c2 = kPlanckC2, sigma = kStefan;
    const double lnC1 = std::log(c1), ln10 = std::log(10.0);
    rgb_.assign(kN, make_float3(1, 1, 1));
    logShare_.assign(kN, 0.0);
    cx_.assign(kN, 0.0);
    cy_.assign(kN, 0.0);
    std::vector<double> lb(lam.size());
    for (int i = 0; i < kN; ++i) {
        const double logT = kLogT0 + (kLogT1 - kLogT0) * i / (kN - 1);
        const double T = std::pow(10.0, logT);
        double mx = -1e300;
        for (size_t j = 0; j < lam.size(); ++j) {
            const double l = lam[j] * 1e-9;
            const double z = c2 / (l * T);
            const double lnE = z > 40.0 ? z + std::log1p(-std::exp(-z)) : std::log(std::expm1(z));
            lb[j] = lnC1 - 5.0 * std::log(l) - lnE;
            mx = std::max(mx, lb[j]);
        }
        double X = 0.0, Y = 0.0, Z = 0.0;
        for (size_t j = 0; j < lam.size(); ++j) {
            const double e = w[j] * std::exp(lb[j] - mx);
            X += xb[j] * e;
            Y += yb[j] * e;
            Z += zb[j] * e;
        }
        // Y in W/m^2/sr per unit of y-bar (nm -> m: 1e-9), against the whole of Planck's sigma T^4 / pi
        logShare_[i] = std::log10(Y) + mx / ln10 - 9.0 - (std::log10(sigma / kPi) + 4.0 * logT);
        const double s = X + Y + Z;
        cx_[i] = X / s;
        cy_[i] = Y / s;
        const float3 rgb = xyzTo709(X / Y, 1.0, Z / Y);
        rgb_[i] = gamutCompress(rgb, 1.0f);
    }
    // against the Sun's
    const double f = (std::log10(kTsun) - kLogT0) / (kLogT1 - kLogT0) * (kN - 1);
    const int i0 = std::clamp(int(f), 0, kN - 2);
    const double sun = logShare_[i0] + (logShare_[i0 + 1] - logShare_[i0]) * (f - i0);
    for (double& v : logShare_) v -= sun;
}

float3 Blackbody::colour(double T) const {
    if (rgb_.empty()) return make_float3(1, 1, 1);
    const double lt = std::clamp(std::log10(std::max(T, 1e-30)), kLogT0, kLogT1);
    const double f = (lt - kLogT0) / (kLogT1 - kLogT0) * (kN - 1);
    const int i = std::clamp(int(f), 0, kN - 2);
    const float u = float(f - i);
    return rgb_[i] + (rgb_[i + 1] - rgb_[i]) * u;
}

double Blackbody::logShare(double T) const {
    if (logShare_.empty()) return 0.0;
    const double lt = std::log10(std::max(T, 1e-30));
    if (lt >= kLogT1) return logShare_.back() - 3.0 * (lt - kLogT1);   // Rayleigh-Jeans: the visible ~T, the whole ~T^4
    if (lt <= kLogT0) return logShare_.front() - 1e3;                  // Wien: nothing visible
    const double f = (lt - kLogT0) / (kLogT1 - kLogT0) * (kN - 1);
    const int i = std::clamp(int(f), 0, kN - 2);
    return logShare_[i] + (logShare_[i + 1] - logShare_[i]) * (f - i);
}

double Blackbody::chromaticity(double T, double& y) const {
    const double lt = std::clamp(std::log10(T), kLogT0, kLogT1);
    const double f = (lt - kLogT0) / (kLogT1 - kLogT0) * (kN - 1);
    const int i = std::clamp(int(f), 0, kN - 2);
    const double u = f - i;
    y = cy_[i] + (cy_[i + 1] - cy_[i]) * u;
    return cx_[i] + (cx_[i + 1] - cx_[i]) * u;
}

// ---------------------------------------------------------------------------------------------------------------------
// The eye

void buildEye(const StarSet& st, const Blackbody& bb, const Eye& e, std::vector<GSplat>& out, EyeSum& sum, const std::vector<Light>* extra) {
    out.clear();
    sum = EyeSum{};
    const Kin k = kin(e.phi);
    const double emp = std::exp(-e.phi);
    const double toDisplay = e.k / e.omegaPx;
    const double mx = e.tanHalfX * (1.0 + 8.0 / e.width), my = e.tanHalfY * (1.0 + 8.0 / e.height);
    const double logShareSun = bb.logShare(kTsun);
    // the field's soft cone: full inside the screen's half diagonal, gone a tenth beyond it (the eye adapts to what it
    // sees; the Sun 61 degrees off the bow at the start, just outside the window, does not dim the stars in it). Angles
    // off the axis from tangents and sines (a cosine of 1e-11 rad is 1 in a double: the deepest binoculars)
    const double diag = std::clamp(e.adaptDiag, 1e-6, 1.4);
    const double aIn = diag, aOut = std::min(1.1 * diag, 0.5 * kPi);
    const double sh = std::sin(0.525 * std::min(diag, kPi / 1.05));
    const double viewShare = std::max(sh * sh, 1e-300);   // 0.5 (1 - cos(1.05 diag)) without the cancellation
    const size_t n = st.pos ? st.pos->size() : 0;
    // one more "star": the Sun, at the origin (the catalogue has no Sun); then the other lights
    const size_t m = extra ? extra->size() : 0;
    const size_t total = n + 1 + m;
    constexpr int kChunks = 64;
    std::vector<std::vector<GSplat>> part(kChunks);
    std::vector<EyeSum> sums(kChunks);
    std::vector<int> chunks(kChunks);
    std::iota(chunks.begin(), chunks.end(), 0);
    std::for_each(std::execution::par, chunks.begin(), chunks.end(), [&](int w) {
        const size_t a = total * size_t(w) / kChunks, b = total * size_t(w + 1) / kChunks;
        EyeSum& S = sums[size_t(w)];
        S.minD = 1e300;
        std::vector<GSplat>& P = part[size_t(w)];
        for (size_t i = a; i < b; ++i) {
            V3 p;
            double LV, T;
            float weight = 1.0f, sigma = 0.75f;
            if (i == n) {
                p = V3{};
                LV = 1.0;
                T = kTsun;
            } else if (i > n) {
                const Light& l = (*extra)[i - n - 1];
                p = l.pos;
                LV = l.LV;
                T = l.T;
                weight = l.weight;
                sigma = l.sigma;
                if (!(LV > 0.0)) continue;
            } else {
                p = (*st.pos)[i];
                LV = (*st.flux)[i].w;
                T = (*st.temp)[i];
            }
            const V3 rel = p - e.pos;
            const double d2 = dot(rel, rel);
            if (!(d2 > 0.0)) continue;
            const V3 nr = rel * (1.0 / std::sqrt(d2));
            double D = 1.0;
            const V3 np = e.phi > 0.0 ? aberrateDirFast(nr, e.motion, k, emp, D) : nr;
            const double Tp = D * T;
            // the visible flux: the rest frame's (L_V / 4 pi d^2, the Sun's spectrum's watts) x D^2 x the visible share
            // at D T against its own at T
            const double ls = i == n ? logShareSun : bb.logShare(T);
            const double lsP = bb.logShare(Tp);
            const double vis = LV * kLsun / (4.0 * kPi * d2) * D * D * std::pow(10.0, lsP - ls);
            const double bol = LV * kLsun / (4.0 * kPi * d2) * D * D * std::pow(10.0, -ls);   // in every band
            S.visible += vis;
            S.bolometric += bol;
            S.energy += bol;
            if (i == n) S.visibleSun = vis;
            S.maxD = std::max(S.maxD, D);
            S.minD = std::min(S.minD, D);
            const double z = dot(np, e.fwd);
            if (z > 0.0) {
                const double xr = dot(np, e.right), yu = dot(np, e.up);
                const double off = std::atan2(std::sqrt(xr * xr + yu * yu), z);   // the angle off the eye's axis
                if (off < aOut) {
                    const double u = std::clamp((aOut - off) / (aOut - aIn), 0.0, 1.0);
                    const double w = u * u * (3.0 - 2.0 * u);
                    S.visibleView += vis * w;
                    S.bolometricView += bol * w;
                    S.energyView += bol * w;
                }
            }
            if (z <= 0.0) continue;
            const double x = dot(np, e.right) / z, y = dot(np, e.up) / z;
            if (std::fabs(x) > mx || std::fabs(y) > my) continue;
            const double f = (e.energy ? bol : vis) * toDisplay * weight;
            if (!(f > 1e-6)) continue;
            ++S.inView;
            const float3 c = e.energy ? energyColour(float(std::log10(Tp)), bb.colour(Tp)) : bb.colour(Tp);
            P.push_back({make_float3(float(x), float(y), 1.0f), c * float(f), sigma});
        }
    });
    sum.minD = 1e300;
    sum.viewShare = viewShare;
    for (int w = 0; w < kChunks; ++w) {
        out.insert(out.end(), part[size_t(w)].begin(), part[size_t(w)].end());
        sum.visible += sums[size_t(w)].visible;
        sum.visibleView += sums[size_t(w)].visibleView;
        sum.bolometric += sums[size_t(w)].bolometric;
        sum.bolometricView += sums[size_t(w)].bolometricView;
        sum.energy += sums[size_t(w)].energy;
        sum.energyView += sums[size_t(w)].energyView;
        sum.visibleSun += sums[size_t(w)].visibleSun;
        sum.inView += sums[size_t(w)].inView;
        sum.maxD = std::max(sum.maxD, sums[size_t(w)].maxD);
        sum.minD = std::min(sum.minD, sums[size_t(w)].minD);
    }
}


void skySums(const SkyCoarse& sky, const Blackbody& bb, const Eye& e, double panoWeight, V3 galX, V3 galY, V3 galN, EyeSum& sum) {
    const Kin k = kin(e.phi);
    const double emp = std::exp(-e.phi);
    const double diag = std::clamp(e.adaptDiag, 1e-6, 1.4);
    const double aIn = diag, aOut = std::min(1.1 * diag, 0.5 * kPi);
    // the background's temperature with the Sun's dipole (Planck 2018: 3.362 mK towards l 264.02, b 48.25)
    const double T0 = 2.72548, dip = 3.362e-3 / T0;
    const double dl = 264.021 * kPi / 180.0, db = 48.253 * kPi / 180.0;
    const V3 dipDir = galX * (std::cos(db) * std::cos(dl)) + galY * (std::cos(db) * std::sin(dl)) + galN * std::sin(db);
    const int W = std::max(sky.w, 256), H = std::max(sky.h, 128);   // the grid (the panorama's when there is one)
    const bool pano = sky.w > 0 && panoWeight > 0.0;
    std::vector<double> vis(size_t(H), 0.0), visV(size_t(H), 0.0), bol(size_t(H), 0.0), bolV(size_t(H), 0.0);
    std::vector<double> fV(size_t(H), 0.0), fB(size_t(H), 0.0), fT(size_t(H), 0.0);
    std::vector<double> en(size_t(H), 0.0), enV(size_t(H), 0.0), fE(size_t(H), 0.0);
    const double cmbRest = kStefan / kPi * T0 * T0 * T0 * T0;   // its radiance at rest (W/m^2/sr), the energy eye's zero
    const double cos5 = std::cos(5.0 * kPi / 180.0);
    std::vector<int> rows(static_cast<size_t>(H));
    std::iota(rows.begin(), rows.end(), 0);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int r) {
        const double b = 0.5 * kPi - (r + 0.5) * kPi / H;
        const double dOmega = std::cos(b) * (2.0 * kPi / W) * (kPi / H);
        for (int c = 0; c < W; ++c) {
            const double l = (0.5 - (c + 0.5) / W) * 2.0 * kPi;   // the panorama's: l grows to the left, 0 at the middle
            const V3 n = galX * (std::cos(b) * std::cos(l)) + galY * (std::cos(b) * std::sin(l)) + galN * std::sin(b);
            double D = 1.0;
            const V3 np = e.phi > 0.0 ? aberrateDirFast(n, e.motion, k, emp, D) : n;
            // the CMB: its rest-frame light from this cell, D^2 at the rocket, the visible share at D T
            const double Tc = T0 * (1.0 + dip * dot(n, dipDir)) * D;
            const double cmbBol = kStefan / kPi * std::pow(T0 * (1.0 + dip * dot(n, dipDir)), 4.0) * D * D * dOmega;
            double v = cmbBol * std::pow(10.0, bb.logShare(Tc)), bo = cmbBol;
            // the energy eye: the cell's radiance in the rocket's sky over the rest glow (none where it is less), over
            // its solid angle there (dOmega / D^2)
            const double q4 = std::pow(1.0 + dip * dot(n, dipDir), 4.0) * D * D * D * D;
            double ex = q4 > 1.0 ? cmbRest * (q4 - 1.0) * dOmega / (D * D) : 0.0;
            if (pano) {
                const float4 p = sky.px[size_t(r) * sky.w + size_t(c)];
                const double lum = (0.2126 * p.x + 0.7152 * p.y + 0.0722 * p.z) * panoWeight;
                if (lum > 0.0) {
                    const double T = double(p.w) * 1e4;
                    const double l0 = bb.logShare(T);
                    v += lum * D * D * std::pow(10.0, bb.logShare(T * D) - l0) * dOmega;
                    bo += lum * std::pow(10.0, -l0) * D * D * dOmega;
                    ex += lum * std::pow(10.0, -l0) * D * D * dOmega;
                }
            }
            vis[size_t(r)] += v;
            bol[size_t(r)] += bo;
            en[size_t(r)] += ex;
            if (dot(np, e.motion) > cos5) {
                fV[size_t(r)] += v;
                fB[size_t(r)] += bo;
                fE[size_t(r)] += ex;
                fT[size_t(r)] += v * Tc;
            }
            const double z = dot(np, e.fwd);
            if (z > 0.0) {
                const double xr = dot(np, e.right), yu = dot(np, e.up);
                const double off = std::atan2(std::sqrt(xr * xr + yu * yu), z);
                if (off < aOut) {
                    const double u = std::clamp((aOut - off) / (aOut - aIn), 0.0, 1.0);
                    const double w = u * u * (3.0 - 2.0 * u);
                    visV[size_t(r)] += v * w;
                    bolV[size_t(r)] += bo * w;
                    enV[size_t(r)] += ex * w;
                }
            }
        }
    });
    for (int r = 0; r < H; ++r) {
        sum.visible += vis[size_t(r)];
        sum.visibleView += visV[size_t(r)];
        sum.bolometric += bol[size_t(r)];
        sum.bolometricView += bolV[size_t(r)];
        sum.fwdVisible += fV[size_t(r)];
        sum.fwdBolometric += fB[size_t(r)];
        sum.energy += en[size_t(r)];
        sum.energyView += enV[size_t(r)];
        sum.fwdEnergy += fE[size_t(r)];
        sum.fwdT += fT[size_t(r)];
    }
    sum.fwdT = sum.fwdVisible > 0.0 ? sum.fwdT / sum.fwdVisible : T0;
}

}   // namespace olcek::hiz
