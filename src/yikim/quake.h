// Scene 3 "Yikim": recorded ground motion. Three components of a strong-motion record (ESM / AFAD TADAS ASCII:
// "KEY: value" header lines, then one acceleration sample per line, cm/s^2), integrated twice (trapezoid) to the
// ground's displacement. The processed records are baseline-corrected and filtered at the source: velocity and
// displacement return to zero at the end (checked for Pazarcik 4614 and Yarimca YPT).
#pragma once
#include "yikim/ymath.h"

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

// the records shipped in assets/deprem: index -> (name, files)
int quakeCount();
std::string quakeName(int i);
bool quakeLoad(int i, const std::string& root, QuakeRecord& q, std::string& err);
