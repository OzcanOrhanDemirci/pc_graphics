// Scene 12 "Ateş": the light of hot things, from Planck's law (host): the tables the eye and the light read. Soot
// absorbs and emits like small spheres (Rayleigh): kappa = 6 pi E(m) fv / lambda, E(m) 0.26 (n 1.57, k 0.56), so its
// emission per metre is kappa B_lambda(T); a solid surface emits epsilon B_lambda(T). Both are weighted by the CIE 1931
// curves and given in cd/m^2 (683 lm/W), then turned into linear Rec.709. The flame front's own light (CH* at 431 nm,
// C2* at 516 nm) is a small share of the heat it releases. docs/ates/PLAN.md §3.5.
#pragma once
#include "scenes/ates/ates_shared.h"
#include "shared/display.h"

#include <cmath>
#include <vector>

namespace ates_spectra {

// CIE 1931 2-degree colour matching functions, multi-lobe Gaussian fit (Wyman, Sloan, Shirley 2013), as shared/spectrum.h
inline float lobe(float x, float mu, float s1, float s2) {
    const float t = (x - mu) / (x < mu ? s1 : s2);
    return std::exp(-0.5f * t * t);
}
inline float3 cieXYZ(float l) {
    const float x = 1.056f * lobe(l, 599.8f, 37.9f, 31.0f) + 0.362f * lobe(l, 442.0f, 16.0f, 26.7f) - 0.065f * lobe(l, 501.1f, 20.4f, 26.2f);
    const float y = 0.821f * lobe(l, 568.8f, 46.9f, 40.5f) + 0.286f * lobe(l, 530.9f, 16.3f, 31.1f);
    const float z = 1.217f * lobe(l, 437.0f, 11.8f, 36.0f) + 0.681f * lobe(l, 459.0f, 26.0f, 13.8f);
    return make_float3(x, y, z);
}
inline float3 xyzToLinearSRGB(float3 c) {
    return make_float3(3.2404542f * c.x - 1.5371385f * c.y - 0.4985314f * c.z, -0.9692660f * c.x + 1.8760108f * c.y + 0.0415560f * c.z,
                       0.0556434f * c.x - 0.2040259f * c.y + 1.0572252f * c.z);
}

inline double planck(double lambdaM, double T) {   // W / (m^2 sr m)
    const double h = 6.62607015e-34, c = 2.99792458e8, k = 1.380649e-23;
    const double x = h * c / (lambdaM * k * T);
    if (x > 700.0) return 0.0;
    return 2.0 * h * c * c / std::pow(lambdaM, 5) / (std::exp(x) - 1.0);
}

inline float3 toRgb(double X, double Y, double Z) {
    float3 rgb = xyzToLinearSRGB(make_float3(float(X), float(Y), float(Z)));
    return gamutCompress(rgb, float(Y));
}

const double kSootE = 6.0 * 3.14159265358979 * 0.26;   // 6 pi E(m): 4.9

// soot of fv = 1e-6: cd/m^2 per metre of path (the emission coefficient), and the black body's luminance (cd/m^2)
inline void tables(std::vector<float3>& soot, std::vector<float3>& grey) {
    soot.assign(ATES_TABLE_N, make_float3(0, 0, 0));
    grey.assign(ATES_TABLE_N, make_float3(0, 0, 0));
    for (int i = 0; i < ATES_TABLE_N; ++i) {
        const double T = ATES_TABLE_T0 + ATES_TABLE_DT * i;
        double sx = 0, sy = 0, sz = 0, gx = 0, gy = 0, gz = 0;
        for (int l = 360; l <= 830; ++l) {
            const double lm = l * 1e-9;
            const double B = planck(lm, T) * 1e-9;   // per nm
            const float3 cmf = cieXYZ(float(l));
            const double kap = kSootE * 1e-6 / lm;   // 1/m for fv = 1e-6
            sx += kap * B * cmf.x;
            sy += kap * B * cmf.y;
            sz += kap * B * cmf.z;
            gx += B * cmf.x;
            gy += B * cmf.y;
            gz += B * cmf.z;
        }
        soot[i] = toRgb(683.0 * sx, 683.0 * sy, 683.0 * sz);
        grey[i] = toRgb(683.0 * gx, 683.0 * gy, 683.0 * gz);
    }
}

// soot's absorption per metre for fv = 1e-6 at the display's primaries' wavelengths
inline float3 sootAbsorption() {
    return make_float3(float(kSootE * 1e-6 / 610e-9), float(kSootE * 1e-6 / 550e-9), float(kSootE * 1e-6 / 465e-9));
}

// the flame front's chemiluminescence: a share eta of the heat release leaves as light, isotropically; per W/m^3,
// cd/m^2 per metre. Its spectrum: CH*'s band at 431 nm, C2*'s Swan bands (473, 516, 563 nm) and the CO + O -> CO2*
// continuum (broad, from the near ultraviolet over the blue; the whole light of carbon monoxide burning over embers).
// Together they are the sky blue of a gas flame, not the violet of CH* alone
inline float3 chemiluminescence(double eta) {
    const double perSr = eta / (4.0 * 3.14159265358979);
    const struct { double mu, s, w; } part[5] = {{431, 4, 0.3}, {473, 5, 0.1}, {516, 5, 0.15}, {563, 5, 0.1}, {420, 55, 0.35}};
    double X = 0, Y = 0, Z = 0;
    for (const auto& b : part) {
        double sum = 0, x = 0, y = 0, z = 0;
        for (int l = 360; l <= 830; ++l) {
            const double t = (l - b.mu) / b.s, g = std::exp(-0.5 * t * t);
            const float3 c = cieXYZ(float(l));
            sum += g;
            x += g * c.x;
            y += g * c.y;
            z += g * c.z;
        }
        X += b.w * x / sum;
        Y += b.w * y / sum;
        Z += b.w * z / sum;
    }
    return toRgb(683.0 * perSr * X, 683.0 * perSr * Y, 683.0 * perSr * Z);
}

}   // namespace ates_spectra
