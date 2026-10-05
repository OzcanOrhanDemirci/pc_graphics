// Scene 7 "Ölçek", second option "Hız": what the rocket's eye sees (H0: the stars) and the colour of light at any speed.
//
// A Doppler factor D turns a blackbody of temperature T into a blackbody of D*T (I_nu / nu^3 is invariant), so the colour
// and the visible share of every star at any speed is one lookup in a table over log T: CIE 1931 2-degree colour matching
// functions over Planck's law (10 K .. 10^12 K), to linear Rec.709. A star's light at the rocket is its rest-frame flux
// times D^2 (the bolometric flux of a point: intensity D^4, solid angle D^-2), of which the eye takes the visible share
// at D*T. Light that slides out of the visible band goes dark: honestly, at many nines the window darkens (PLAN §3.1).
#pragma once
#include "scenes/olcek/olcek_hiz.h"
#include "scenes/olcek/olcek_types.h"

#include <cmath>
#include <string>
#include <vector>

namespace olcek::hiz {

class Blackbody {
public:
    static constexpr double kLogT0 = 1.0, kLogT1 = 12.0;   // 10 K .. 10^12 K
    static constexpr int kN = 1101;                         // every 0.01 decade
    // cmfCsv: the CIE 1931 2-degree table (wavelength, x, y, z per line, nm); missing: the multi-lobe fit
    // (Wyman, Sloan, Shirley 2013) in src/shared/spectrum.h, and `source` says so
    void build(const std::string& cmfCsv);
    float3 colour(double T) const;     // linear Rec.709, luminance 1 (pulled into the gamut towards its luminance)
    double logShare(double T) const;   // log10 of the visible (luminance) share of its light, against the Sun's (5772 K)
    double share(double T) const { return std::pow(10.0, logShare(T)); }
    double chromaticity(double T, double& y) const;   // CIE x (and y) of the Planck locus at T (tests)
    std::string source;
    int samples = 0;                   // wavelengths in the CMF table used
private:
    std::vector<float3> rgb_;
    std::vector<double> logShare_, cx_, cy_;
};

// the stars the eye sees: the catalogue (from the Sun), each with its visual luminosity (L_sun) and temperature
struct StarSet {
    const std::vector<V3>* pos = nullptr;
    const std::vector<float4>* flux = nullptr;   // .w: visual luminosity (L_sun)
    const std::vector<float>* temp = nullptr;    // K
};

// one frame of the rocket's eye
struct Eye {
    V3 pos;                     // the rocket, from the Sun (m)
    V3 motion{0, 0, 1};         // the way it moves (unit, rest frame)
    double phi = 0.0;
    V3 fwd{0, 0, 1}, up{0, 1, 0}, right{1, 0, 0};   // where the eye looks: unit vectors (the frames share the axes)
    double tanHalfX = 1.0, tanHalfY = 0.5625;
    int width = 2560, height = 1440;
    double k = 1.0;             // the exposure: display radiance per W/m^2/sr
    double omegaPx = 1e-6;      // the solid angle a point's light is spread over (the unzoomed pixel's: the field of
                                // view narrowed by the wheel does not brighten the points)
};
struct EyeSum {
    double visible = 0.0;       // the visible light from the whole sky at the rocket (W/m^2, the Sun's spectrum's units)
    double visibleView = 0.0;   // ... of it what falls in the eye's field (a soft cone: the eye adapts to what it sees)
    double viewShare = 1.0;     // the cone's share of the whole sky (its solid angle / 4 pi): visibleView / viewShare is
                                // the whole sky's light if it were all like the field (the exposure's measure)
    double visibleSun = 0.0;    // ... of it the Sun's
    int inView = 0;
    double maxD = 0.0, minD = 0.0;   // the largest and smallest Doppler factor among the stars
};
// the stars as splats (camera space with z = 1: x and y are the tangents, so a 1e-11 rad field keeps its digits)
void buildEye(const StarSet& st, const Blackbody& bb, const Eye& e, std::vector<GSplat>& out, EyeSum& sum);

}   // namespace olcek::hiz
