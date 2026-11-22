// Scene 3 "Yikim": recorded ground motion. Three components of a strong-motion record (ESM / AFAD TADAS ASCII:
// "KEY: value" header lines, then one acceleration sample per line, cm/s^2), integrated twice (trapezoid) to the
// ground's displacement. The processed records are baseline-corrected and filtered at the source: velocity and
// displacement return to zero at the end (checked for Pazarcik 4614 and Yarimca YPT).
#pragma once
#include "scenes/yikim/ymath.h"

#include <algorithm>
#include <string>
#include <vector>

struct QuakeRecord {
    std::string name;
    double dt = 0;
    std::vector<double3> acc, vel, disp;   // world axes: x east, y up, z north (m/s^2, m/s, m)
    double pga = 0;                          // horizontal peak, m/s^2
    double tPeak = 0;                        // when it comes (s from the record's start)
    double duration() const { return acc.empty() ? 0.0 : dt * (acc.size() - 1); }
    double3 displacement(double t) const;    // linear interpolation, zero outside
    bool load(const std::string& east, const std::string& north, const std::string& up, std::string& err);
};

// the records in assets/deprem (fetched by assets/deprem/fetch.py, not committed): index -> (name, files)
int quakeCount();
std::string quakeName(int i);
bool quakeRecordPresent(int i, const std::string& root);   // its three files are there
bool quakeRecordByHand(int i);                             // downloaded by hand (fetch.py says how), not by the script
bool quakeLoad(int i, const std::string& root, QuakeRecord& q, std::string& err);

// Earthquakes by magnitude, 3 to 9: what the building feels from a quake of that size some 15 km away. Each is made
// from one of the real records (its waveform), cut to the strong-motion length that size brings, played faster for the
// small ones (short, quick shaking), scaled to that size's peak ground acceleration, and baseline-corrected so the
// ground ends where it started. A game's scale, not a seismological one: magnitude alone does not fix the shaking.
struct QuakeMagnitude {
    int magnitude;
    const char* label;     // how it feels (Turkish, UTF-8, shown in the window)
    int record;            // the waveform: index into the records
    double pgaG;           // horizontal peak ground acceleration, g
    double window;         // strong-motion length kept, s (after time scaling)
    double timeScale;      // < 1: the record played faster
};
int quakeMagnitudeMin();
int quakeMagnitudeMax();
const QuakeMagnitude* quakeMagnitude(int magnitude);   // null outside 3..9
// window: the strong-motion length to keep, s (0: the magnitude's own)
bool quakeLoadMagnitude(int magnitude, const std::string& root, QuakeRecord& q, std::string& err, double window = 0.0);

// The earthquake as the ground plays it, for any length. Up to the magnitude's own length it is the record above, cut
// to that length (at its own length exactly the measured one). Longer, or until stopped, the record's strong part
// plays as a seamless loop: its end crossfaded into its start (equal power: the two parts are unrelated, so their
// energies add), integrated around the loop with the mean taken out of acceleration and velocity, so that velocity and
// displacement close on themselves (periodic ground motion, no drift), brought in and out by a smooth envelope over the
// record's own rise and fall. A stop fades the ground back over a moment instead of freezing it (a frozen ground is a
// jolt: its velocity drops to zero within a step).
struct QuakePlay {
    int magnitude = 0;
    double length = 0.0;          // s of shaking; <= 0: until stopped
    bool loop = false;
    QuakeRecord rec;              // the record, or one period of the loop
    double rise = 0.0, fall = 0.0;
    double stopAt = -1.0;         // play time at which a stop began (-1: none)
    double stopGain = 1.0;        // the envelope then
    static constexpr double kStopFade = 1.5;   // s
    double gain(double t) const;                // the envelope: 1 for the record until a stop
    double3 displacement(double t) const;       // where the ground is, relative to where it stood at the start
    bool over(double t) const;
    bool endless() const { return length <= 0.0; }
    bool stopping() const { return stopAt >= 0.0; }
    void stop(double t);
    double progress(double t) const { return endless() ? 0.0 : std::min(1.0, std::max(0.0, t / length)); }
};
double quakeOwnSeconds(int magnitude);   // the strong-motion length of a magnitude (its record's window)
// seconds: the shaking's length; 0: the magnitude's own; < 0: until stopped
bool quakePlay(int magnitude, double seconds, const std::string& root, QuakePlay& q, std::string& err);
