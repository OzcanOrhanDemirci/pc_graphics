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
// a light besides the catalogue's stars (the galaxies: the survey's points; the Galaxy, M31 and M33 as wholes)
struct Light {
    V3 pos;                  // from the Sun (m)
    double LV = 0.0;         // visual luminosity (L_sun)
    double T = 5772.0;       // its colour temperature (K)
    float weight = 1.0f;     // the share drawn as a point (all of it counts in the sums: the models draw the rest)
    float sigma = 0.75f;     // the point's radius (pixels)
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
    double adaptDiag = 0.722;   // the eye adapts to the light within this angle of where it looks (radians): the unzoomed
                                // field's half diagonal (75 x 42 degrees), so the binoculars change neither the points
                                // nor the exposure
    bool energy = false;        // the energy eye: every band's light (bolometric), the band in false colour
    double worldT = 0.0;        // the rest frame's time since the journey's first thrust (s): the world's clocks
    // the catalogue's Sun and Proxima: their points' share (the rest drawn as their balls where they are wide)
    float sunPoint = 1.0f, proximaPoint = 1.0f;
    int proximaStar = -1;       // Proxima's index in the catalogue
};
struct EyeSum {
    double visible = 0.0;       // the visible light from the whole sky at the rocket (W/m^2, the Sun's spectrum's units)
    double visibleView = 0.0;   // ... of it what falls in the eye's field (a soft cone: the eye adapts to what it sees)
    double viewShare = 1.0;     // the cone's share of the whole sky (its solid angle / 4 pi): visibleView / viewShare is
                                // the whole sky's light if it were all like the field (the exposure's measure)
    double visibleSun = 0.0;    // ... of it the Sun's
    int inView = 0;
    double maxD = 0.0, minD = 0.0;   // the largest and smallest Doppler factor among the stars
    double bolometric = 0.0, bolometricView = 0.0;   // the same in every band (W/m^2): the energy eye's measure
    double fwdVisible = 0.0, fwdBolometric = 0.0;     // the diffuse sky's light within 5 degrees of the motion (the glare
    double fwdT = 0.0;                                // round the bright cap ahead), and its light-weighted temperature
    // the energy eye's own measure: every band, with the cosmic background's glow at rest taken out (a detector zeroed
    // at the start, as an astronomer takes out the sky: what is left of it is what the motion adds ahead; behind, less
    // than nothing, is black). At rest the background is 60% of the night sky's energy and a red veil over everything
    double energy = 0.0, energyView = 0.0, fwdEnergy = 0.0;
    // the exposure's measure (W/m^2 as a whole sky's): the field's light as if all the sky were like it, or where the
    // eye looks at a body a degree or more wide, its face's (the eye adapts to what it looks at: a lamp's face, up to
    // ~1e5 W/m^2/sr; the Sun's 2e7 dazzles)
    double adaptVisible = 0.0, adaptEnergy = 0.0;
};
// the diffuse sky's light (the Milky Way's panorama, the cosmic microwave background) at the rocket, added to the sums as
// the stars' are: integrated over the rest frame's sphere on the panorama's coarse grid (each cell's light D^2 times, its
// visible share at D T: the forward cap of the rocket's sky is the rest frame's whole forward hemisphere, so a coarse
// grid there misses nothing at any speed)
struct SkyCoarse {
    std::vector<float4> px;     // linear RGB (W/m^2/sr, the Sun's spectrum's watts), T / 1e4 K; galactic, l 0 at the middle
    int w = 0, h = 0;
};
void skySums(const SkyCoarse& sky, const Blackbody& bb, const Eye& e, double panoWeight, V3 galX, V3 galY, V3 galN, EyeSum& sum);
// the stars as splats (camera space with z = 1: x and y are the tangents, so a 1e-11 rad field keeps its digits)
void buildEye(const StarSet& st, const Blackbody& bb, const Eye& e, std::vector<GSplat>& out, EyeSum& sum,
              const std::vector<Light>* extra = nullptr);

}   // namespace olcek::hiz
