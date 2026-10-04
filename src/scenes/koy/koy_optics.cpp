// Scene 5 "Koy": optics on the host (see koy_optics.h).
#include "scenes/koy/koy_optics.h"
#include "scenes/koy/koy_data.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <sstream>
#include <thread>

namespace {

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
void parallelFor(int n, const std::function<void(int)>& f) {
    const int T = std::max(1, int(std::thread::hardware_concurrency()));
    std::atomic<int> next{0};
    std::vector<std::thread> th;
    for (int t = 0; t < T; ++t)
        th.emplace_back([&] {
            for (int i = next++; i < n; i = next++) f(i);
        });
    for (auto& x : th) x.join();
}
template <size_t N> float interp(const KoyPoint (&t)[N], float nm) {
    if (nm <= t[0].nm) return t[0].v;
    for (size_t k = 1; k < N; ++k)
        if (nm <= t[k].nm) {
            const float a = (nm - t[k - 1].nm) / (t[k].nm - t[k - 1].nm);
            return t[k - 1].v + (t[k].v - t[k - 1].v) * a;
        }
    return t[N - 1].v;
}
float logistic(float x) { return 1.0f / (1.0f + std::exp(-x)); }
std::string cfgString(const Config& cfg, const std::string& key, const std::string& def) {
    const auto it = cfg.values.find(key);
    if (it == cfg.values.end()) return def;
    std::string v = it->second;
    while (!v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\t')) v.pop_back();
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    const size_t hash = v.find('#');
    if (hash != std::string::npos) { v = v.substr(0, hash); while (!v.empty() && v.back() == ' ') v.pop_back(); }
    return v.empty() ? def : v;
}
// CIE 1931 2-degree colour matching functions, multi-lobe Gaussian fit (Wyman, Sloan & Shirley 2013), as in shared/spectrum.h
float cmfLobe(float x, float mu, float s1, float s2) {
    const float t = (x - mu) / (x < mu ? s1 : s2);
    return std::exp(-0.5f * t * t);
}
float3 cieXYZ(float l) {
    const float x = 1.056f * cmfLobe(l, 599.8f, 37.9f, 31.0f) + 0.362f * cmfLobe(l, 442.0f, 16.0f, 26.7f) - 0.065f * cmfLobe(l, 501.1f, 20.4f, 26.2f);
    const float y = 0.821f * cmfLobe(l, 568.8f, 46.9f, 40.5f) + 0.286f * cmfLobe(l, 530.9f, 16.3f, 31.1f);
    const float z = 1.217f * cmfLobe(l, 437.0f, 11.8f, 36.0f) + 0.681f * cmfLobe(l, 459.0f, 26.0f, 13.8f);
    return make_float3(x, y, z);
}
// 3x3 helpers
void inv3(const double m[3][3], double r[3][3]) {
    const double d = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                     m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    r[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / d;
    r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / d;
    r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / d;
    r[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / d;
    r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / d;
    r[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / d;
    r[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / d;
    r[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / d;
    r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / d;
}
void mul3(const double a[3][3], const double b[3][3], double r[3][3]) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
}
const double kXyzToSrgb[3][3] = {{3.2404542, -1.5371385, -0.4985314}, {-0.9692660, 1.8760108, 0.0415560}, {0.0556434, -0.2040259, 1.0572252}};
const double kBradford[3][3] = {{0.8951, 0.2664, -0.1614}, {-0.7502, 1.7135, 0.0367}, {0.0389, -0.0685, 1.0296}};

// table coordinates (the inverse of the device's tc/tcT)
float tAt(int i, int n, float tmax) { const float u = float(i) / float(n - 1); return u * u * tmax; }
float linAt(int i, int n, float lo, float hi) { return lo + (hi - lo) * float(i) / float(n - 1); }

}   // namespace

// ---------------------------------------------------------------------------------------------------
float KoyOptics::phaseWater(float c) { return (1.0f + 0.835f * c * c) / (4.0f * 3.14159265f * (1.0f + 0.835f / 3.0f)); }

float KoyOptics::phaseFF(float c) const {
    const float th = std::max(std::acos(std::min(std::max(c, -1.0f), 1.0f)), 0.0035f);
    const float s2 = std::sin(0.5f * th) * std::sin(0.5f * th);
    const float nu = ffNu, d180 = ffD180;
    const float d = d180 * s2;
    const float dn = std::pow(d, nu), d180n = std::pow(d180, nu);
    const float A = 1.0f / (4.0f * 3.14159265f * (1.0f - d) * (1.0f - d) * dn) * (nu * (1.0f - d) - (1.0f - dn) + (d * (1.0f - dn) - nu * (1.0f - d)) / s2);
    const float B = (1.0f - d180n) / (16.0f * 3.14159265f * (d180 - 1.0f) * d180n) * (3.0f * c * c - 1.0f);
    return std::max(A + B, 0.0f) * ffScale;
}
// The integral of phaseFF over the sphere: the plateau inside the clamp cone analytically, the rest on a log-spaced grid
// (the function falls like theta^-1.5 there). Midpoint rule in theta with sin(theta).
double KoyOptics::phaseIntegral() const {
    const double thc = 0.0035, PI = 3.14159265358979;
    double I = phaseFF(float(std::cos(thc))) * 2.0 * PI * (1.0 - std::cos(thc));
    const int N = 200000;
    const double l0 = std::log(thc), l1 = std::log(PI);
    for (int k = 0; k < N; ++k) {
        const double a = std::exp(l0 + (l1 - l0) * k / N), b = std::exp(l0 + (l1 - l0) * (k + 1) / N), th = 0.5 * (a + b);
        I += phaseFF(float(std::cos(th))) * std::sin(th) * 2.0 * PI * (b - a);
    }
    return I;
}

float KoyOptics::fresnelWaterToAir(float ci) const {
    ci = std::min(std::max(ci, 0.0f), 1.0f);
    const float n = waterN;
    const float st2 = n * n * (1.0f - ci * ci);
    if (st2 >= 1.0f) return 1.0f;
    const float ct = std::sqrt(1.0f - st2);
    const float rs = (n * ci - ct) / (n * ci + ct), rp = (ci - n * ct) / (ci + n * ct);
    return 0.5f * (rs * rs + rp * rp);
}
float KoyOptics::fresnelAirToWater(float ci) const {
    ci = std::min(std::max(ci, 0.0f), 1.0f);
    const float n = waterN;
    const float st2 = (1.0f - ci * ci) / (n * n);
    const float ct = std::sqrt(std::max(0.0f, 1.0f - st2));
    const float rs = (ci - n * ct) / (ci + n * ct), rp = (n * ci - ct) / (n * ci + ct);
    return 0.5f * (rs * rs + rp * rp);
}
float3 KoyOptics::xyzOf(const float* s) const {
    double x = 0, y = 0, z = 0;
    for (int i = 0; i < NL; ++i) {
        x += cmf[0][i] * s[i] * DL;
        y += cmf[1][i] * s[i] * DL;
        z += cmf[2][i] * s[i] * DL;
    }
    return make_float3(float(x), float(y), float(z));
}

// ---------------------------------------------------------------------------------------------------
void KoyOptics::spectra(const Config& cfg) {
    waterN = cfg.f("water.n", 1.333f);
    Bp = cfg.f("water.bp_fraction", 0.0183f);   // Petzold's average particle backscatter fraction
    bottomDepth = cfg.f("light.bottom", 8.0f);
    {   // the floors the light field is built for: light.bottoms (up to three, metres) or light.bottom alone
        const float3 b = cfg.v3("light.bottoms", make_float3(0.0f, 0.0f, 0.0f));
        bottoms.clear();
        for (float v : {b.x, b.y, b.z})
            if (v > 0.0f) bottoms.push_back(v);
        if (bottoms.empty()) bottoms.push_back(bottomDepth);
        std::sort(bottoms.begin(), bottoms.end());
        if (bottoms.size() > size_t(KOY_FIELDS)) bottoms.resize(KOY_FIELDS);
    }
    photons = cfg.i("light.photons", 400000);
    sunElevDeg = cfg.f("sun.elevation", 70.0f);
    sunAzimDeg = cfg.f("sun.azimuth", 180.0f);
    const float D2R = 3.14159265f / 180.0f;
    // ---- absorption and scattering: a Jerlov water type as Solonenko & Mobley 2015 tabulate it. Absorption: pure water at
    //      2.5 nm (Pope & Fry 1997) plus the type's non-water part (its tabulated a minus pure water, smooth, clamped at 0:
    //      their pure-water column differs slightly from Pope & Fry in the red); scattering: their Haltrin two-component
    //      formula (water + small + large particles).
    const std::string type = cfgString(cfg, "water.jerlov", "IB");
    const KoyJerlov* J = &kJerlov[0];
    for (const KoyJerlov& k : kJerlov)
        if (type == k.name) J = &k;
    jerlov = J->name;
    chl = J->chl;
    auto nonWater = [&](float l) {
        float prev = 0.0f;
        for (int k = 0; k < J->n; ++k) {
            const float v = std::max(0.0f, J->a[k] - interp(kWaterAbsorption, J->nm[k]));
            if (l <= J->nm[k]) {
                if (k == 0) return v;
                const float t = (l - J->nm[k - 1]) / (J->nm[k] - J->nm[k - 1]);
                return prev + (v - prev) * t;
            }
            prev = v;
        }
        return prev;
    };
    for (int i = 0; i < NL; ++i) {
        const float l = lambdaOf(i);
        // pure water beyond Pope & Fry's last row (727.5 nm): held at its rising end (the eye barely sees there and a metre
        // of water swallows it)
        aw[i] = l <= 727.5f ? interp(kWaterAbsorption, l) : 1.678f + (l - 727.5f) * 0.02f;
        aph[i] = nonWater(l);
        acdom[i] = 0.0f;
        a[i] = aw[i] + aph[i];
        const float r = 400.0f / l;
        bw[i] = 0.00583f * std::pow(r, 4.322f);
        bp[i] = (1.513f * J->Bs * std::pow(r, 1.7f) + 0.3411f * J->Bl * std::pow(r, 0.3f)) * cfg.f("water.particle_scale", 1.0f);
        b[i] = bw[i] + bp[i];
        c[i] = a[i] + b[i];
    }
    // ---- the eye
    for (int i = 0; i < NL; ++i) {
        const float3 x = cieXYZ(lambdaOf(i));
        cmf[0][i] = x.x;
        cmf[1][i] = x.y;
        cmf[2][i] = x.z;
    }
    // ---- the sun: the ASTM G173 beam at air mass 1.5, carried to the scene's air mass with Rayleigh + aerosol depths
    const float am = 1.0f / std::sin(sunElevDeg * D2R);
    for (int i = 0; i < NL; ++i) {
        const float l = lambdaOf(i), lu = l / 1000.0f;
        const float tauR = 0.008569f * std::pow(lu, -4.0f) * (1.0f + 0.0113f * std::pow(lu, -2.0f) + 0.00013f * std::pow(lu, -4.0f));
        const float tauA = cfg.f("sky.aerosol", 0.08f) * std::pow(lu / 0.5f, -1.3f);
        const float dir = interp(kSunDirect, std::min(l, 730.0f)), glob = interp(kSunGlobal, std::min(l, 730.0f));
        sunPerp[i] = dir * std::exp((tauR + tauA) * (1.5f - am));
        // the sky's diffuse light on a horizontal plane: the G173 global on its 37 degree plane minus the beam on it
        // (incidence 11.2 degrees), scaled by the sun's height; a clear sky's diffuse share
        const float diffuse = std::max(glob - dir * std::cos(11.2f * D2R), 0.0f);
        skyHoriz[i] = diffuse * cfg.f("sky.diffuse_scale", 0.85f);
    }
    sunAir = make_float3(std::cos(sunElevDeg * D2R) * std::sin(sunAzimDeg * D2R), std::sin(sunElevDeg * D2R),
                         std::cos(sunElevDeg * D2R) * std::cos(sunAzimDeg * D2R));
    {
        const float ci = sunAir.y, e = 1.0f / waterN;
        const float k = 1.0f - e * e * (1.0f - ci * ci);
        const float3 din = make_float3(-sunAir.x, -sunAir.y, -sunAir.z);
        const float3 t = din * e + make_float3(0, 1, 0) * (e * ci - std::sqrt(std::max(k, 0.0f)));
        sunWater = normalize(make_float3(-t.x, -t.y, -t.z));
        sunCosW = sunWater.y;
    }
    const float Tsun = 1.0f - fresnelAirToWater(sunAir.y);
    for (int i = 0; i < NL; ++i) sunWaterPerp[i] = sunPerp[i] * Tsun * sunAir.y / sunCosW;
    // ---- the white point: daylight on a horizontal plane above the water (the camera is white balanced for the sun)
    std::vector<float> day(NL);
    for (int i = 0; i < NL; ++i) day[i] = sunPerp[i] * sunAir.y + skyHoriz[i];
    const float3 W = xyzOf(day.data());
    whiteY = W.y;
    {
        // Bradford adaptation from the daylight's white to D65, then XYZ -> linear sRGB
        const double wd65[3] = {0.95047, 1.0, 1.08883};
        const double ws[3] = {W.x / W.y, 1.0, W.z / W.y};
        double cs[3], cd[3];
        for (int r = 0; r < 3; ++r) {
            cs[r] = kBradford[r][0] * ws[0] + kBradford[r][1] * ws[1] + kBradford[r][2] * ws[2];
            cd[r] = kBradford[r][0] * wd65[0] + kBradford[r][1] * wd65[1] + kBradford[r][2] * wd65[2];
        }
        double D[3][3] = {{cd[0] / cs[0], 0, 0}, {0, cd[1] / cs[1], 0}, {0, 0, cd[2] / cs[2]}};
        double binv[3][3], t1[3][3], t2[3][3], m[3][3];
        inv3(kBradford, binv);
        mul3(D, kBradford, t1);
        mul3(binv, t1, t2);
        mul3(kXyzToSrgb, t2, m);
        // and normalise so that the daylight's Y maps to 1 on the white
        for (int r = 0; r < 3; ++r) xyzToRgb[r] = make_float3(float(m[r][0]), float(m[r][1]), float(m[r][2]));
    }
    // ---- three smooth reflectance spectra (blue, green, red regions) that sum to 1; basisFix makes the round trip exact
    for (int i = 0; i < NL; ++i) {
        const float l = lambdaOf(i);
        const float bl = 1.0f - logistic((l - 490.0f) / 11.0f), rd = logistic((l - 588.0f) / 11.0f);
        basis[0][i] = bl;   // NOTE: basis index 0 = red, 1 = green, 2 = blue in basisFix's rows below
        basis[2][i] = rd;
        basis[1][i] = std::max(0.0f, 1.0f - bl - rd);
    }
    std::swap(basis[0], basis[2]);   // 0 red, 1 green, 2 blue
    {
        // the linear sRGB each basis spectrum shows in daylight (white balanced: the white plane is (1, 1, 1))
        double M[3][3];
        const float3 Wrgb = make_float3(dot(xyzToRgb[0], W), dot(xyzToRgb[1], W), dot(xyzToRgb[2], W));
        for (int k = 0; k < 3; ++k) {
            std::vector<float> s(NL);
            for (int i = 0; i < NL; ++i) s[i] = day[i] * basis[k][i];
            const float3 X = xyzOf(s.data());
            M[0][k] = dot(xyzToRgb[0], X) / Wrgb.x;
            M[1][k] = dot(xyzToRgb[1], X) / Wrgb.y;
            M[2][k] = dot(xyzToRgb[2], X) / Wrgb.z;
        }
        double Mi[3][3];
        inv3(M, Mi);
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 3; ++k) basisFix[r][k] = float(Mi[r][k]);
        // fold the white normalisation into xyzToRgb: a white Lambert plane in daylight shows rgb = Y-scale (1,1,1) * Y
        for (int r = 0; r < 3; ++r) {
            const float s = (r == 0 ? W.y / Wrgb.x : r == 1 ? W.y / Wrgb.y : W.y / Wrgb.z);
            xyzToRgb[r] = xyzToRgb[r] * s;
        }
    }
    auto reflectance = [&](float3 rgb, float* out) {
        const float w0 = basisFix[0][0] * rgb.x + basisFix[0][1] * rgb.y + basisFix[0][2] * rgb.z;
        const float w1 = basisFix[1][0] * rgb.x + basisFix[1][1] * rgb.y + basisFix[1][2] * rgb.z;
        const float w2 = basisFix[2][0] * rgb.x + basisFix[2][1] * rgb.y + basisFix[2][2] * rgb.z;
        for (int i = 0; i < NL; ++i) out[i] = std::min(std::max(w0 * basis[0][i] + w1 * basis[1][i] + w2 * basis[2][i], 0.0f), 1.0f);
    };
    reflectance(cfg.v3("light.sand_rgb", make_float3(0.50f, 0.45f, 0.36f)), sand);
    {
        float rr[NL];
        reflectance(cfg.v3("sky.rim_rgb", make_float3(0.42f, 0.37f, 0.30f)), rr);
        for (int i = 0; i < NL; ++i) rim[i] = day[i] * rr[i] / 3.14159265f;
    }
    // ---- the sky's two shapes: Rayleigh blue and white haze, each normalised to unit Y
    {
        double yb = 0, yw = 0;
        for (int i = 0; i < NL; ++i) {
            const float lu = lambdaOf(i) / 1000.0f;
            skyBlue[i] = sunPerp[i] * std::pow(lu, -4.0f);
            skyWhite[i] = sunPerp[i];
            yb += cmf[1][i] * skyBlue[i] * DL;
            yw += cmf[1][i] * skyWhite[i] * DL;
        }
        for (int i = 0; i < NL; ++i) {
            skyBlue[i] /= float(yb);
            skyWhite[i] /= float(yw);
        }
    }
    // ---- the Perez sky (Preetham, Shirley & Smits 1999, luminance coefficients for turbidity T), scaled so that the
    //      sky's illuminance on a horizontal plane matches the diffuse light above
    {
        const float T = cfg.f("sky.turbidity", 2.2f);
        perez[0] = 0.1787f * T - 1.4630f;
        perez[1] = -0.3554f * T + 0.4275f;
        perez[2] = -0.0227f * T + 5.3251f;
        perez[3] = 0.1206f * T - 2.5771f;
        perez[4] = -0.0670f * T + 0.3703f;
        const float thS = (90.0f - sunElevDeg) * D2R;
        auto F = [&](float cz, float g) {
            return (1.0f + perez[0] * std::exp(perez[1] / std::max(cz, 0.02f))) * (1.0f + perez[2] * std::exp(perez[3] * g) + perez[4] * std::cos(g) * std::cos(g));
        };
        perezNorm = 1.0f / F(1.0f, thS);
        coastSin = std::sin(cfg.f("sky.rim_deg", 10.0f) * D2R);
        double e = 0.0;
        const int NT = 180, NP = 360;
        for (int it = 0; it < NT; ++it)
            for (int ip = 0; ip < NP; ++ip) {
                const float th = (it + 0.5f) / NT * 0.5f * 3.14159265f, ph = (ip + 0.5f) / NP * 2.0f * 3.14159265f;
                const float3 w = make_float3(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
                if (w.y < coastSin * 0.9f) continue;
                const float g = std::acos(std::min(std::max(dot(w, sunAir), -1.0f), 1.0f));
                e += F(w.y, g) * perezNorm * w.y * std::sin(th) * (0.5 * 3.14159265 / NT) * (2.0 * 3.14159265 / NP);
            }
        double ySky = 0;
        for (int i = 0; i < NL; ++i) ySky += cmf[1][i] * skyHoriz[i] * DL;
        skyY = float(ySky / e);
    }
    // ---- Fournier-Forand: n = 1.10 and the Junge slope that gives the particles' backscatter fraction (Mobley et al. 2002)
    {
        // Mobley, Sundman & Boss 2002: on the line n = 1.01 + 0.1542 (mu - 3), find the Junge slope mu whose backscatter
        // fraction is B_p (their Eqs. 3 and 7)
        auto backFrac = [&](float mu) {
            const float n = 1.01f + 0.1542f * (mu - 3.0f);
            const float d90 = 4.0f / (3.0f * (n - 1.0f) * (n - 1.0f)) * 0.5f;
            const float nu = 0.5f * (3.0f - mu);
            return 1.0f - (1.0f - std::pow(d90, nu + 1.0f) - 0.5f * (1.0f - std::pow(d90, nu))) / ((1.0f - d90) * std::pow(d90, nu));
        };
        float lo = 3.0001f, hi = 5.0f;
        const bool rising = backFrac(4.5f) > backFrac(3.2f);
        for (int k = 0; k < 80; ++k) {
            const float mid = 0.5f * (lo + hi);
            if ((backFrac(mid) < Bp) == rising) lo = mid; else hi = mid;
        }
        ffMu = 0.5f * (lo + hi);
        ffN = 1.01f + 0.1542f * (ffMu - 3.0f);
        ffD180 = 4.0f / (3.0f * (ffN - 1.0f) * (ffN - 1.0f));
        ffNu = 0.5f * (3.0f - ffMu);
        // delta-M truncation: the phase is read no sharper than the sun's disc under water (0.2 degrees); what the
        // particles scatter inside that cone stays in the beam (it cannot be told apart from it). So: the phase is
        // renormalised over the rest and b_p keeps only the share scattered outside the cone (Wiscombe 1977).
        ffScale = 1.0f;
        const double I = phaseIntegral();
        ffScale = float(1.0 / I);
        ffTruncated = float(1.0 - I);
        for (int i = 0; i < NL; ++i) {
            bp[i] *= float(I);
            b[i] = bw[i] + bp[i];
            c[i] = a[i] + b[i];
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// The light field: photons through a plane-parallel water column (the surface above, Lambert sand below at bottomDepth).
// Tallies the radiance crossing horizontal planes, binned by direction, excluding the sun's unscattered beam.
void KoyOptics::lightField() {
    const int NZ = AP_Z, NM = AP_MU;
    field.assign(size_t(NL) * NZ * NM, 0.0f);
    const float zmax = AMB_ZMAX, dz = zmax / float(NZ - 1);
    // phase sampling tables (inverse CDF over cos theta), FF on a fine angular grid
    const int NC = 4096;
    std::vector<float> cdfFF(NC + 1), cosFF(NC + 1), cdfW(NC + 1), cosW(NC + 1);
    {
        // theta from 0 to pi on a grid dense near 0 (FF's forward peak)
        std::vector<double> th(NC + 1), pf(NC + 1), pw(NC + 1);
        for (int k = 0; k <= NC; ++k) {
            const double u = double(k) / NC;
            th[k] = 3.14159265358979 * u * u * u;   // cubic spacing
            pf[k] = phaseFF(float(std::cos(th[k]))) * std::sin(th[k]) * 2.0 * 3.14159265358979;
            pw[k] = phaseWater(float(std::cos(th[k]))) * std::sin(th[k]) * 2.0 * 3.14159265358979;
        }
        double sf = 0, sw = 0;
        cdfFF[0] = cdfW[0] = 0.0f;
        cosFF[0] = cosW[0] = 1.0f;
        for (int k = 1; k <= NC; ++k) {
            const double d = th[k] - th[k - 1];
            sf += 0.5 * (pf[k] + pf[k - 1]) * d;
            sw += 0.5 * (pw[k] + pw[k - 1]) * d;
            cdfFF[k] = float(sf);
            cdfW[k] = float(sw);
            cosFF[k] = cosW[k] = float(std::cos(th[k]));
        }
        for (int k = 0; k <= NC; ++k) {
            cdfFF[k] /= float(sf);
            cdfW[k] /= float(sw);
        }
        std::printf("koy: Fournier-Forand B %.4f (n %.2f, Junge %.3f), integral %.4f (1 olmali)\n", Bp, ffN, ffMu, sf);
    }
    auto sampleCos = [&](const std::vector<float>& cdf, const std::vector<float>& cs, float u) {
        const size_t k = size_t(std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin());
        if (k == 0) return cs[0];
        if (k > size_t(NC)) return cs[NC];
        const float a = (u - cdf[k - 1]) / std::max(cdf[k] - cdf[k - 1], 1e-12f);
        return cs[k - 1] + (cs[k] - cs[k - 1]) * a;
    };
    const double t0 = nowMs();
    const float twoPi = 2.0f * 3.14159265f;
    parallelFor(NL, [&](int li) {
        std::mt19937 rng(1234u + 7919u * unsigned(li));
        std::uniform_real_distribution<float> U(0.0f, 1.0f);
        float* L = field.data() + size_t(li) * NZ * NM;
        std::vector<double> acc(size_t(NZ) * NM, 0.0);
        const float cl = c[li], al = b[li] / c[li], shareW = bw[li] / b[li];
        const float H = bottomDepth;
        const int Nsun = photons, Nsky = photons;
        const double wSun = double(sunWaterPerp[li]) * sunCosW / Nsun;   // flux per horizontal area per photon
        const double wSky = double(skyHoriz[li]) / Nsky;
        for (int src = 0; src < 2; ++src) {
            const int count = src == 0 ? Nsun : Nsky;
            for (int pk = 0; pk < count; ++pk) {
                float3 d;
                double w;
                if (src == 0) {
                    d = make_float3(-sunWater.x, -sunWater.y, -sunWater.z);
                    w = wSun;
                } else {   // a cosine-weighted sky direction in air, refracted, weighted by the transmission
                    const float u1 = U(rng), u2 = U(rng);
                    const float ct = std::sqrt(1.0f - u1), st = std::sqrt(u1), ph = twoPi * u2;
                    const float ci = ct;
                    const float T = 1.0f - fresnelAirToWater(ci);
                    const float sw = st / waterN, cw = std::sqrt(1.0f - sw * sw);
                    d = make_float3(sw * std::cos(ph), -cw, sw * std::sin(ph));
                    w = wSky * T;
                }
                const double w0 = w;
                float z = 1e-4f;      // depth (down positive)
                bool scattered = src == 1;   // the sky's light counts from the start; the sun's beam only once scattered
                for (int ev = 0; ev < 200 && w > 0.0; ++ev) {
                    const float s = -std::log(std::max(U(rng), 1e-12f)) / cl;
                    const float dzv = -d.y;   // depth change per metre
                    float z1 = z + s * dzv;
                    bool atBottom = false, atTop = false;
                    if (z1 >= H) { z1 = H; atBottom = true; }
                    if (z1 <= 0.0f) { z1 = 0.0f; atTop = true; }
                    if (scattered && std::fabs(dzv) > 1e-6f) {   // planes crossed between z and z1
                        const float mu = d.y;
                        const int bin = std::min(NM - 1, std::max(0, int((mu * 0.5f + 0.5f) * NM)));
                        const double add = w / std::max(std::fabs(mu), 0.02f);
                        const float za = std::min(z, z1), zb = std::max(z, z1);
                        int j0 = int(std::ceil(za / dz)), j1 = int(std::floor(zb / dz));
                        j0 = std::max(j0, 0);
                        if (za < 0.01f && zb >= 0.01f) j0 = 0;   // plane 0 is at 0.01 m
                        j1 = std::min(j1, NZ - 1);
                        for (int j = j0; j <= j1; ++j) {
                            const float zp = j == 0 ? 0.01f : j * dz;
                            if (zp > za && zp <= zb) acc[size_t(j) * NM + bin] += add;
                        }
                    }
                    z = z1;
                    if (atBottom) {   // Lambert sand
                        w *= sand[li];
                        const float u1 = U(rng), u2 = U(rng);
                        const float st = std::sqrt(u1), ct = std::sqrt(1.0f - u1), ph = twoPi * u2;
                        d = make_float3(st * std::cos(ph), ct, st * std::sin(ph));
                        scattered = true;
                        z = H - 1e-4f;
                        if (w < 1e-12) break;
                        continue;
                    }
                    if (atTop) {   // the surface from below: Fresnel, total internal reflection outside the window
                        const float R = fresnelWaterToAir(d.y);
                        if (U(rng) < R) {
                            d.y = -d.y;
                            z = 1e-4f;
                            continue;
                        }
                        break;
                    }
                    // a collision: scatter with the single-scattering albedo as weight
                    w *= al;
                    if (w < 0.02 * w0) {
                        if (U(rng) < 0.25f) w *= 4.0; else break;   // Russian roulette
                    }
                    const bool water = U(rng) < shareW;
                    const float ct = water ? sampleCos(cdfW, cosW, U(rng)) : sampleCos(cdfFF, cosFF, U(rng));
                    const float st = std::sqrt(std::max(0.0f, 1.0f - ct * ct)), ph = twoPi * U(rng);
                    const Frame f = Frame::from(d);
                    d = normalize(f.toWorld(make_float3(st * std::cos(ph), st * std::sin(ph), ct)));
                    scattered = true;
                }
            }
        }
        const double dOmega = 2.0 * 3.14159265358979 * (2.0 / NM);
        for (size_t k = 0; k < acc.size(); ++k) L[k] = float(acc[k] / dOmega);
        // the planes below the floor: no photon crosses them, so they stayed zero and a lookup near the floor blended
        // towards it (at the floor itself 15 % of the light; found by the independent review, 2026-10-04): they take the
        // last lit plane's value (a field read a little below its floor stays near right)
        const int jLast = std::min(NZ - 1, int(std::floor(H / dz + 1e-4f)));
        for (int j = jLast + 1; j < NZ; ++j)
            for (int m = 0; m < NM; ++m) L[size_t(j) * NM + m] = L[size_t(jLast) * NM + m];
    });
    msField = nowMs() - t0;
}

double KoyOptics::kdDown(int li, float z0, float z1) const {
    // downwelling irradiance (diffuse field + the direct beam) at two depths -> Kd
    const int NZ = AP_Z, NM = AP_MU;
    const float dz = AMB_ZMAX / float(NZ - 1);
    auto Ed = [&](float z) {
        const int j = std::min(NZ - 1, std::max(0, int(std::round(z / dz))));
        const float* L = field.data() + size_t(li) * NZ * NM + size_t(j) * NM;
        double e = 0.0;
        for (int m = 0; m < NM; ++m) {
            const float mu = -1.0f + (m + 0.5f) * 2.0f / NM;
            if (mu < 0.0f) e += L[m] * (-mu) * 2.0 * 3.14159265358979 * (2.0 / NM);
        }
        const float zz = j * dz;
        e += sunWaterPerp[li] * sunCosW * std::exp(-c[li] * zz / sunCosW);
        return e;
    };
    return std::log(Ed(z0) / Ed(z1)) / double(z1 - z0);
}

// ---------------------------------------------------------------------------------------------------
void KoyOptics::tables() {
    const int NZ = AP_Z, NM = AP_MU;
    const float dz = AMB_ZMAX / float(NZ - 1);
    const double PI = 3.14159265358979;
    auto muOf = [&](int m) { return -1.0f + (m + 0.5f) * 2.0f / NM; };
    // ---- the phase redistribution: Q[in][out] = integral over the in-bin and the azimuth of p(cos theta)
    std::vector<double> Qw(size_t(NM) * NM, 0.0), Qp(size_t(NM) * NM, 0.0);
    parallelFor(NM, [&](int out) {
        const double mo = muOf(out);
        const double so = std::sqrt(std::max(0.0, 1.0 - mo * mo));
        for (int in = 0; in < NM; ++in) {
            double sw = 0, sp = 0;
            const int SM = 24, SP = 96;
            for (int a = 0; a < SM; ++a) {
                const double mi = -1.0 + (in + (a + 0.5) / SM) * 2.0 / NM;
                const double si = std::sqrt(std::max(0.0, 1.0 - mi * mi));
                for (int k = 0; k < SP; ++k) {
                    const double ph = (k + 0.5) / SP * 2.0 * PI;
                    const double ct = mi * mo + si * so * std::cos(ph);
                    sw += phaseWater(float(ct));
                    sp += phaseFF(float(ct));
                }
            }
            const double dA = (2.0 / NM / SM) * (2.0 * PI / SP);
            Qw[size_t(in) * NM + out] = sw * dA;
            Qp[size_t(in) * NM + out] = sp * dA;
        }
    });
    // ---- J[lambda][z][out]: in-scattered radiance per metre towards direction out (diffuse field only)
    std::vector<float> J(size_t(NL) * NZ * NM, 0.0f), Js(size_t(NL) * NZ * NM, 0.0f);
    const int sunIn = std::min(NM - 1, std::max(0, int((-sunCosW * 0.5f + 0.5f) * NM)));
    parallelFor(NL, [&](int li) {
        for (int j = 0; j < NZ; ++j) {
            const float* L = field.data() + (size_t(li) * NZ + j) * NM;
            const float z = j * dz;
            const double Esun = sunWaterPerp[li] * std::exp(-c[li] * z / sunCosW);
            for (int out = 0; out < NM; ++out) {
                double s = 0.0;
                for (int in = 0; in < NM; ++in) s += L[in] * (bw[li] * Qw[size_t(in) * NM + out] + bp[li] * Qp[size_t(in) * NM + out]);
                J[(size_t(li) * NZ + j) * NM + out] = float(s);
                // the sun's beam scattered once, averaged over azimuth (used where the march is not: reflections)
                const double qa = (bw[li] * Qw[size_t(sunIn) * NM + out] + bp[li] * Qp[size_t(sunIn) * NM + out]) / (2.0 / NM);
                Js[(size_t(li) * NZ + j) * NM + out] = float(Esun * qa / (2.0 * PI));
            }
        }
    });
    auto Jat = [&](const std::vector<float>& tab, int li, float z, int out) {
        const float u = std::min(std::max(z / dz, 0.0f), float(NZ - 1) - 1e-3f);
        const int j = int(u);
        const float a = u - j;
        const float* p = tab.data() + (size_t(li) * NZ + j) * NM + out;
        return p[0] * (1.0f - a) + p[NM] * a;
    };
    // ---- irradiance on a tilted Lambert surface from the diffuse field: E[lambda][z][n]
    std::vector<float> E(size_t(NL) * NZ * AS_N, 0.0f);
    parallelFor(NL, [&](int li) {
        for (int j = 0; j < NZ; ++j) {
            const float* L = field.data() + (size_t(li) * NZ + j) * NM;
            for (int n = 0; n < AS_N; ++n) {
                const double ny = linAt(n, AS_N, -1.0f, 1.0f), nx = std::sqrt(std::max(0.0, 1.0 - ny * ny));
                double e = 0.0;
                for (int m = 0; m < NM; ++m) {
                    const double mu = muOf(m), sm = std::sqrt(std::max(0.0, 1.0 - mu * mu));
                    const int SP = 64;
                    double f = 0.0;
                    for (int k = 0; k < SP; ++k) {
                        const double ph = (k + 0.5) / SP * 2.0 * PI;
                        // light travelling along (sm cos, mu, sm sin) arrives on the face whose normal is opposite
                        const double cosArr = -(ny * mu + nx * sm * std::cos(ph));
                        if (cosArr > 0.0) f += cosArr;
                    }
                    e += L[m] * f * (2.0 * PI / SP) * (2.0 / NM);
                }
                E[(size_t(li) * NZ + j) * AS_N + n] = float(e);
            }
        }
    });
    // ---- ambSurf[k][t][n][z]
    for (int k = 0; k < 3; ++k) ambSurf[k].assign(size_t(AS_T) * AS_N * AS_Z, make_float4(0, 0, 0, 0));
    parallelFor(AS_Z, [&](int zi) {
        const float z = linAt(zi, AS_Z, 0.0f, AMB_ZMAX);
        for (int n = 0; n < AS_N; ++n)
            for (int ti = 0; ti < AS_T; ++ti) {
                const float t = tAt(ti, AS_T, AMB_TMAX);
                double X[3][3] = {};
                for (int li = 0; li < NL; ++li) {
                    const float u = std::min(std::max(z / dz, 0.0f), float(NZ - 1) - 1e-3f);
                    const int j = int(u);
                    const float fa = u - j;
                    const float e = E[(size_t(li) * NZ + j) * AS_N + n] * (1.0f - fa) + E[(size_t(li) * NZ + j + 1) * AS_N + n] * fa;
                    const double v = e / PI * std::exp(-double(c[li]) * t) * DL;
                    for (int k = 0; k < 3; ++k)
                        for (int q = 0; q < 3; ++q) X[k][q] += cmf[q][li] * basis[k][li] * v;
                }
                const size_t idx = (size_t(zi) * AS_N + n) * AS_T + ti;
                for (int k = 0; k < 3; ++k) ambSurf[k][idx] = make_float4(float(X[k][0]), float(X[k][1]), float(X[k][2]), 0.0f);
            }
    });
    // ---- ambPath[z0][mu][t]: along a view ray of vertical cosine mu (direction of travel away from the camera)
    ambPath.assign(size_t(AP_T) * AP_MU * AP_Z, make_float4(0, 0, 0, 0));
    parallelFor(AP_Z, [&](int zi) {
        const float z0 = linAt(zi, AP_Z, 0.0f, AMB_ZMAX);
        std::vector<double> S(NL);
        for (int m = 0; m < AP_MU; ++m) {
            const float mu = linAt(m, AP_MU, -1.0f, 1.0f);
            // light reaching the camera travels along -d: its direction's vertical cosine is -mu
            const int out = std::min(NM - 1, std::max(0, int((-mu * 0.5f + 0.5f) * NM)));
            std::fill(S.begin(), S.end(), 0.0);
            float tPrev = 0.0f;
            for (int ti = 0; ti < AP_T; ++ti) {
                const float t = tAt(ti, AP_T, AMB_TMAX);
                const int SUB = 8;
                for (int sub = 0; sub < SUB && t > tPrev; ++sub) {
                    const float s0 = tPrev + (t - tPrev) * sub / SUB, s1 = tPrev + (t - tPrev) * (sub + 1) / SUB, sm = 0.5f * (s0 + s1);
                    const float z = z0 - mu * sm;
                    if (z < 0.0f) continue;
                    for (int li = 0; li < NL; ++li) S[li] += Jat(J, li, z, out) * std::exp(-double(c[li]) * sm) * (s1 - s0);
                }
                tPrev = t;
                double X = 0, Y = 0, Z = 0;
                for (int li = 0; li < NL; ++li) {
                    X += cmf[0][li] * S[li] * DL;
                    Y += cmf[1][li] * S[li] * DL;
                    Z += cmf[2][li] * S[li] * DL;
                }
                ambPath[(size_t(zi) * AP_MU + m) * AP_T + ti] = make_float4(float(X), float(Y), float(Z), 0.0f);
            }
        }
    });
    // ---- reflPath[ts][mu][t2]: a ray leaving the surface (z0 = 0), diffuse field + the sun's azimuth-averaged single
    //      scattering, then ts more metres of water to the camera
    reflPath.assign(size_t(RP_T) * RP_MU * RP_TS, make_float4(0, 0, 0, 0));
    {
        std::vector<double> S2(size_t(RP_MU) * RP_T * NL, 0.0);
        parallelFor(RP_MU, [&](int m) {
            const float mu = linAt(m, RP_MU, -1.0f, 1.0f);
            const int out = std::min(NM - 1, std::max(0, int((-mu * 0.5f + 0.5f) * NM)));
            std::vector<double> S(NL, 0.0);
            float tPrev = 0.0f;
            for (int ti = 0; ti < RP_T; ++ti) {
                const float t = tAt(ti, RP_T, AMB_TMAX);
                const int SUB = 8;
                for (int sub = 0; sub < SUB && t > tPrev; ++sub) {
                    const float s0 = tPrev + (t - tPrev) * sub / SUB, s1 = tPrev + (t - tPrev) * (sub + 1) / SUB, sm = 0.5f * (s0 + s1);
                    const float z = -mu * sm;
                    if (z < 0.0f) continue;
                    for (int li = 0; li < NL; ++li)
                        S[li] += (Jat(J, li, z, out) + Jat(Js, li, z, out)) * std::exp(-double(c[li]) * sm) * (s1 - s0);
                }
                tPrev = t;
                for (int li = 0; li < NL; ++li) S2[(size_t(m) * RP_T + ti) * NL + li] = S[li];
            }
        });
        parallelFor(RP_TS, [&](int si) {
            const float ts = linAt(si, RP_TS, 0.0f, RP_TSMAX);
            for (int m = 0; m < RP_MU; ++m)
                for (int ti = 0; ti < RP_T; ++ti) {
                    double X = 0, Y = 0, Z = 0;
                    for (int li = 0; li < NL; ++li) {
                        const double v = S2[(size_t(m) * RP_T + ti) * NL + li] * std::exp(-double(c[li]) * ts) * DL;
                        X += cmf[0][li] * v;
                        Y += cmf[1][li] * v;
                        Z += cmf[2][li] * v;
                    }
                    reflPath[(size_t(si) * RP_MU + m) * RP_T + ti] = make_float4(float(X), float(Y), float(Z), 0.0f);
                }
        });
    }
    // ---- pathTab: functions of the length L of water a light path crosses
    pathTab.assign(size_t(PT_W) * PT_ROWS, make_float4(0, 0, 0, 0));
    std::vector<float> flat(NL);
    double yFlat = 0;
    for (int li = 0; li < NL; ++li) yFlat += cmf[1][li] * sunWaterPerp[li] * DL;
    parallelFor(PT_W, [&](int x) {
        const float L = PT_LMAX * float(x) / float(PT_W - 1);
        double R[PT_ROWS][3] = {};
        double grey = 0.0;
        for (int li = 0; li < NL; ++li) {
            const double T = std::exp(-double(c[li]) * L) * DL;
            const double n2 = double(waterN) * waterN;
            for (int q = 0; q < 3; ++q) {
                const double cm = cmf[q][li];
                for (int k = 0; k < 3; ++k) R[PT_SURF0 + k][q] += cm * basis[k][li] / PI * sunWaterPerp[li] * T;
                R[PT_SHAFT_W][q] += cm * bw[li] * sunWaterPerp[li] * T;
                R[PT_SHAFT_P][q] += cm * bp[li] * sunWaterPerp[li] * T;
                R[PT_SKY0 + 0][q] += cm * n2 * skyBlue[li] * T;
                R[PT_SKY0 + 1][q] += cm * n2 * skyWhite[li] * T;
                R[PT_SKY0 + 2][q] += cm * n2 * rim[li] * T;
                R[PT_SUN_AIR][q] += cm * sunPerp[li] * T;
            }
            grey += cmf[1][li] * sunWaterPerp[li] * T;
        }
        for (int r = 0; r < PT_ROWS; ++r) pathTab[size_t(r) * PT_W + x] = make_float4(float(R[r][0]), float(R[r][1]), float(R[r][2]), 0.0f);
        pathTab[size_t(PT_TRANS) * PT_W + x] = make_float4(float(grey / yFlat), 0, 0, 0);
    });
}

void KoyOptics::build(const Config& cfg) {
    const double t0 = nowMs();
    spectra(cfg);
    fieldTables.clear();
    for (float b : bottoms) {   // one light field and its tables per floor depth, the deepest last (the members keep it)
        bottomDepth = b;
        lightField();
        tables();
        FieldTables ft;
        for (int k = 0; k < 3; ++k) ft.ambSurf[k] = ambSurf[k];
        ft.ambPath = ambPath;
        ft.reflPath = reflPath;
        fieldTables.push_back(std::move(ft));
    }
    msBuild = nowMs() - t0;
}

std::string KoyOptics::summary() const {
    auto at = [&](float nm) { return int(std::round((nm - L0) / DL)); };
    std::ostringstream s;
    s.setf(std::ios::fixed);
    s.precision(4);
    s << "su (Jerlov " << jerlov << ", Chl " << chl << " mg/m3, FF n " << ffN << " Junge " << ffMu << "): c(450) " << c[at(450)] << ", c(530) " << c[at(530)] << ", c(600) " << c[at(600)]
      << " 1/m; b(530) " << b[at(530)] << ", gorus 4.8/c(530) " << 4.8f / c[at(530)] << " m; gunes suda " << std::acos(sunCosW) * 57.2958f
      << " derece; FF kesilen pay " << ffTruncated << "; Kd(490, 1-6 m) " << kdDown(at(490), 1.0f, 6.0f) << "; isik alani " << int(msField) << " ms, toplam " << int(msBuild) << " ms";
    return s.str();
}

// ---------------------------------------------------------------------------------------------------
static unsigned long long makeTex2D(const std::vector<float4>& v, int w, int h, std::vector<void*>& arrays) {
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<float4>();
    cudaArray_t arr = nullptr;
    cudaMallocArray(&arr, &fd, w, h);
    cudaMemcpy2DToArray(arr, 0, 0, v.data(), size_t(w) * sizeof(float4), size_t(w) * sizeof(float4), h, cudaMemcpyHostToDevice);
    arrays.push_back(arr);
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeArray;
    rd.res.array.array = arr;
    cudaTextureDesc td{};
    td.addressMode[0] = td.addressMode[1] = cudaAddressModeClamp;
    td.filterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeElementType;
    td.normalizedCoords = 1;
    cudaTextureObject_t t = 0;
    cudaCreateTextureObject(&t, &rd, &td, nullptr);
    return t;
}
static unsigned long long makeTex3D(const std::vector<float4>& v, int w, int h, int d, std::vector<void*>& arrays) {
    cudaChannelFormatDesc fd = cudaCreateChannelDesc<float4>();
    cudaArray_t arr = nullptr;
    cudaMalloc3DArray(&arr, &fd, make_cudaExtent(w, h, d));
    cudaMemcpy3DParms cp{};
    cp.srcPtr = make_cudaPitchedPtr(const_cast<float4*>(v.data()), size_t(w) * sizeof(float4), w, h);
    cp.dstArray = arr;
    cp.extent = make_cudaExtent(w, h, d);
    cp.kind = cudaMemcpyHostToDevice;
    cudaMemcpy3D(&cp);
    arrays.push_back(arr);
    cudaResourceDesc rd{};
    rd.resType = cudaResourceTypeArray;
    rd.res.array.array = arr;
    cudaTextureDesc td{};
    td.addressMode[0] = td.addressMode[1] = td.addressMode[2] = cudaAddressModeClamp;
    td.filterMode = cudaFilterModeLinear;
    td.readMode = cudaReadModeElementType;
    td.normalizedCoords = 1;
    cudaTextureObject_t t = 0;
    cudaCreateTextureObject(&t, &rd, &td, nullptr);
    return t;
}

void KoyOptics::upload(KoyParams& p) {
    release();
    p.pathTab = makeTex2D(pathTab, PT_W, PT_ROWS, arrays_);
    textures_ = {p.pathTab};
    p.fields = int(fieldTables.size());
    for (int f = 0; f < p.fields; ++f) {
        const FieldTables& ft = fieldTables[size_t(f)];
        for (int k = 0; k < 3; ++k) textures_.push_back(p.ambSurf[f][k] = makeTex3D(ft.ambSurf[k], AS_T, AS_N, AS_Z, arrays_));
        textures_.push_back(p.ambPath[f] = makeTex3D(ft.ambPath, AP_T, AP_MU, AP_Z, arrays_));
        textures_.push_back(p.reflPath[f] = makeTex3D(ft.reflPath, RP_T, RP_MU, RP_TS, arrays_));
        p.fieldBottom[f] = bottoms[size_t(f)];
    }
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) p.basisFix[r][k] = basisFix[r][k];
    p.sunWater = sunWater;
    p.sunAir = sunAir;
    p.sunCosW = sunCosW;
    p.sunRadiusAir = sunRadiusAir;
    p.waterN = waterN;
    p.phaseB = Bp;
    p.ffNu = ffNu;
    p.ffDelta180 = ffD180;
    p.ffScale = ffScale;
    p.skyY = skyY;
    for (int k = 0; k < 5; ++k) p.perez[k] = perez[k];
    p.perezNorm = perezNorm;
    p.coastHeight = coastSin;
    p.xyzToRgb0 = xyzToRgb[0];
    p.xyzToRgb1 = xyzToRgb[1];
    p.xyzToRgb2 = xyzToRgb[2];
}

void KoyOptics::release() {
    for (auto t : textures_) if (t) cudaDestroyTextureObject(cudaTextureObject_t(t));
    textures_.clear();
    for (void* a : arrays_) cudaFreeArray(static_cast<cudaArray_t>(a));
    arrays_.clear();
}
