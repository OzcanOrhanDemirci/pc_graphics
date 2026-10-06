// Scene 7 "Ölçek", second option "Hız": the five panels of graphs (PLAN §4: "the graphs are as important as the
// picture"). Drawn with the interface kit over the picture (crisp text and lines at the screen's resolution): thin white
// lines, capital titles with their letters spaced, the passenger's clock white, the Earth's red, light gold.
//   HIZ          the speed bar against the wall of light; gamma's curve (the nines are in its title)
//   UZAY-ZAMAN   the rocket's own space and time drawn in the Earth's: a patch of its grid folding towards the light
//                line and flattening into a line as the speed grows (Özcan's reference: only the folding)
//   YOL          two rulers to the target (the Earth's, fixed; yours, melting by gamma), the stopping distance, a grid
//                of space squeezed along the motion
//   SAATLER      two dials (yours; the Earth's, gamma times faster, its hands blurred by the eye's shutter), the date,
//                the Earth's clock as seen (its light delayed and Doppler-shifted) and as "now" for you
//   BEDEL        what it would take: energy, the photon rocket's fuel, the acceleration, the light from ahead, a
//                hydrogen atom's blow; the motion's physics true, the rocket's engineering idealised
// Everything scales itself (logarithmic where it must), meaningful from rest to 21 nines.
#pragma once
#include "scenes/olcek/olcek_hiz.h"
#include "scenes/olcek/olcek_hiz_goz.h"
#include "ui/ui.h"

#include <string>
#include <vector>

namespace olcek::hiz {

// the panel's words and numbers (the language in use: theme::num / app::L)
std::string sciText(double v, int digits);          // 2,24 × 10¹⁰
std::string distText(double m);                     // m, km, million km, AU, light years ...
std::string clockText(double s);                    // m:ss, h:mm:ss
std::string spanText(double s);                     // seconds to millions of years
std::string dateText(double t);                     // "today" + t as a calendar date
std::string betaText(const Kin& k);                 // 0,999 999 9xx (from 1 - beta)
std::string speedText(const Kin& k);                // m/s, or c − x m/s
std::string gammaText(double g);

// what the motion would take (the BEDEL panel), for one tonne of rocket; tested against the plan's numbers at 21 nines
constexpr double kSunLuminosity = 3.828e26;   // W (IAU 2015 nominal)
constexpr double kSolarConstant = 1361.0;     // W/m^2 at the Earth
constexpr double kCmbToday = 2.72548;         // K (Fixsen 2009)
struct Cost {
    double kinetic = 0.0;         // (gamma - 1) m c^2 (J)
    double photon = 0.0;          // a photon rocket's fuel burned to reach this speed: (e^phi - 1) m c^2 (J)
    double photonStop = 0.0;      // and to stop again: (e^(2 phi) - 1) m c^2 (J)
    double massRatio = 1.0;       // e^phi
    double massRatioStop = 1.0;   // e^(2 phi)
    double gees = 0.0;            // the drive's acceleration (g)
    double plate = 0.0;           // the CMB on a black plate facing forward (W/m^2)
    double cmbAhead = 0.0;        // the CMB's temperature dead ahead (K)
    double hydrogen = 0.0;        // a hydrogen atom's energy in the rocket's frame (eV)
};
Cost costOf(double phi);
// the forward-facing plate's light from the CMB: the forward hemisphere of D^4 (sigma T0^4 / pi) cos theta'
double cmbOnPlate(const Kin& k);

// one frame's numbers for the panels (the scene fills it from the ship and the route)
struct Pano {
    const Ship* ship = nullptr;
    const Route* route = nullptr;
    const Blackbody* bb = nullptr;
    Kin k;
    double earthNow = 0.0;        // the Earth's time simultaneous with "now" in the rocket's frame (s since today)
    double earthSeen = 0.0;       // the Earth's time the light reaching the rocket now left it (s since today)
    double earthSeenRate = 1.0;   // how fast that seen clock runs against yours (its Doppler factor)
    double earthDist = 0.0;       // the rest frame's distance to the Earth (m)
    double dt = 0.0;              // the passenger's seconds of this frame (the shutter of the dials)
    double now = 0.0;             // the scene's clock (the panels' own easing)
};

// what is shown: a bit per panel (HIZ 1, UZAY-ZAMAN 2, YOL 4, SAATLER 8, BEDEL 16)
constexpr unsigned kPanoAll = 31;
// draws the panels over `view` (window pixels), S the layout's scale (sizes for 2560x1440). The panels' own state (the
// axes growing smoothly) lives in `state`, kept by the scene between frames.
struct PanoState {
    double gammaTop = -1.0;       // the gamma curve's axis top (log10), eased
};
// `avoid`: the application's panels; each column keeps to the room between them, smaller if it must. `placed`: where
// each was drawn, added (the eye's labels keep off them)
void drawPanels(const Pano& p, PanoState& state, const ui::Rect& view, float S, unsigned show, const std::vector<ui::Rect>& avoid = {},
                std::vector<ui::Rect>* placed = nullptr);

// the energy eye's key (top middle of the eye's view): its name, the bands in its false colours (each band as wide as
// the next: microwaves to gamma rays) and where the light dead ahead peaks now (Wien), the cosmic background's and a
// Sun-like star's: both move up the bands as the speed grows (D = e^phi)
void drawEnergyKey(const ui::Rect& view, float S, double phi, const Blackbody& bb, const std::vector<ui::Rect>& avoid = {},
                   std::vector<ui::Rect>* placed = nullptr);

// the eye's labels (H5): a named thing where the eye sees it (window pixels, aberrated), its disc's radius there and its
// distance; drawn in the given order (the first wins where two would meet), none over the application's panels
struct EyeLabel {
    std::string name;
    float x = 0.0f, y = 0.0f;
    float r = 0.0f;
    double dist = 0.0;   // m
    float alpha = 1.0f;  // 0 .. 1: a label that would run across the screen faster than it reads fades out
};
void drawEyeLabels(const std::vector<EyeLabel>& labels, const ui::Rect& view, float S, const std::vector<ui::Rect>& avoid = {});

}   // namespace olcek::hiz
