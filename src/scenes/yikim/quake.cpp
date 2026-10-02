#include "scenes/yikim/quake.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

static bool loadComponent(const std::string& path, double& dt, std::vector<double>& a, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot read " + path; return false; }
    dt = 0;
    a.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line.find(':') != std::string::npos) {
            if (line.rfind("SAMPLING_INTERVAL_S:", 0) == 0) dt = std::atof(line.c_str() + 20);
            continue;
        }
        char* end = nullptr;
        const double v = std::strtod(line.c_str(), &end);
        if (end != line.c_str()) a.push_back(v * 0.01);   // cm/s^2 -> m/s^2
    }
    if (dt <= 0 || a.size() < 2) { err = "no samples in " + path; return false; }
    return true;
}

bool QuakeRecord::load(const std::string& e, const std::string& n, const std::string& u, std::string& err) {
    double dts[3];
    std::vector<double> c[3];
    const std::string* files[3] = {&e, &n, &u};
    for (int k = 0; k < 3; ++k)
        if (!loadComponent(*files[k], dts[k], c[k], err)) return false;
    if (dts[0] != dts[1] || dts[0] != dts[2]) { err = "components sampled differently"; return false; }
    dt = dts[0];
    const size_t N = std::min(c[0].size(), std::min(c[1].size(), c[2].size()));
    acc.resize(N);
    vel.assign(N, d3(0, 0, 0));
    disp.assign(N, d3(0, 0, 0));
    pga = 0;
    for (size_t i = 0; i < N; ++i) {
        acc[i] = d3(c[0][i], c[2][i], c[1][i]);   // east -> x, up -> y, north -> z
        const double ah = std::sqrt(c[0][i] * c[0][i] + c[1][i] * c[1][i]);
        if (ah > pga) { pga = ah; tPeak = dt * i; }
    }
    for (size_t i = 1; i < N; ++i) {
        vel[i] = vel[i - 1] + (acc[i] + acc[i - 1]) * (0.5 * dt);
        disp[i] = disp[i - 1] + (vel[i] + vel[i - 1]) * (0.5 * dt);
    }
    return true;
}

double3 QuakeRecord::displacement(double t) const {
    if (disp.empty() || t <= 0) return d3(0, 0, 0);
    const double f = t / dt;
    const size_t i = size_t(f);
    if (i + 1 >= disp.size()) return disp.back();
    const double w = f - double(i);
    return disp[i] * (1.0 - w) + disp[i + 1] * w;
}

struct QuakeFiles { const char* name; const char* e; const char* n; const char* u; };
static const QuakeFiles kQuakes[] = {
    {"2023 Kahramanmaras, Pazarcik (TK.4614)", "TK.4614..HNE.D.INT-20230206_0000008.ACC.MP.ASC", "TK.4614..HNN.D.INT-20230206_0000008.ACC.MP.ASC",
     "TK.4614..HNZ.D.INT-20230206_0000008.ACC.MP.ASC"},
    {"2023 Kahramanmaras, Antakya (TK.3123)", "TK.3123..HNE.D.INT-20230206_0000008.ACC.MP.ASC", "TK.3123..HNN.D.INT-20230206_0000008.ACC.MP.ASC",
     "TK.3123..HNZ.D.INT-20230206_0000008.ACC.MP.ASC"},
    {"1999 Kocaeli, Yarimca (YPT)", "A.YPT.00.HNE.D.TK-1999-0077.ACC.MP.ASC", "A.YPT.00.HNN.D.TK-1999-0077.ACC.MP.ASC",
     "A.YPT.00.HNZ.D.TK-1999-0077.ACC.MP.ASC"},
    {"1999 Kocaeli, Izmit (TK.4101)", "TK.4101..HNE.D.TK-1999-0077.ACC.MP.ASC", "TK.4101..HNN.D.TK-1999-0077.ACC.MP.ASC",
     "TK.4101..HNZ.D.TK-1999-0077.ACC.MP.ASC"},
    {"2020 Sisam, Bayrakli (TK.3513)", "20201030115124_3513_mp_Acc_E.asc", "20201030115124_3513_mp_Acc_N.asc", "20201030115124_3513_mp_Acc_U.asc"},
};

// Peak ground accelerations follow ground-motion models only loosely (about 15 km away, firm ground); the waveforms
// are the real records of the larger quakes (index into kQuakes: 1 Antakya, 2 Yarimca, 3 Izmit, 4 Bayrakli). What each
// does to the building was measured (docs/yikim/DURUM.md): 3-5 cracks only, 6 parapets and some infill fall, 7 heavy
// damage but standing, 8 and 9 collapse. Pazarcik's record is not used: its energy sits in short periods, so at x1 the
// building stands and scaled up until it falls (4.2 g) it throws debris unlike anything else.
static const QuakeMagnitude kMagnitudes[] = {
    {3, "Zayıf", 4, 0.012, 3.0, 0.45},
    {4, "Hafif", 4, 0.035, 5.0, 0.55},
    {5, "Orta", 3, 0.13, 8.0, 0.7},
    {6, "Kuvvetli", 3, 0.20, 14.0, 0.85},
    {7, "Şiddetli", 2, 0.33, 26.0, 1.0},
    {8, "Yıkıcı", 1, 0.70, 32.0, 1.0},
    {9, "Felaket", 1, 1.1, 45.0, 1.0},
};

int quakeMagnitudeMin() { return kMagnitudes[0].magnitude; }
int quakeMagnitudeMax() { return kMagnitudes[sizeof(kMagnitudes) / sizeof(kMagnitudes[0]) - 1].magnitude; }
const QuakeMagnitude* quakeMagnitude(int m) {
    for (const QuakeMagnitude& q : kMagnitudes)
        if (q.magnitude == m) return &q;
    return nullptr;
}

// a magnitude's cut of its record: played faster (the same samples, closer together), the strong part around the peak
// (a third before it, the rest after, within the record), scaled to the magnitude's peak
struct QuakeCut {
    double dt, scale;
    size_t i0, N;
};
static QuakeCut cutRecord(const QuakeMagnitude& M, const QuakeRecord& r, double window) {
    QuakeCut c;
    c.dt = r.dt * M.timeScale;
    const double tPeak = r.tPeak * M.timeScale;
    const double total = c.dt * (r.acc.size() - 1);
    double t0 = std::max(0.0, tPeak - 0.3 * window), t1 = std::min(total, t0 + window);
    t0 = std::max(0.0, t1 - window);
    c.i0 = size_t(t0 / c.dt);
    const size_t i1 = std::min(r.acc.size() - 1, size_t(t1 / c.dt));
    c.N = i1 - c.i0 + 1;
    c.scale = M.pgaG * 9.81 / std::max(r.pga, 1e-9);
    return c;
}

bool quakeLoadMagnitude(int magnitude, const std::string& root, QuakeRecord& q, std::string& err, double window) {
    const QuakeMagnitude* M = quakeMagnitude(magnitude);
    if (!M) { err = "no such magnitude"; return false; }
    QuakeRecord r;
    if (!quakeLoad(M->record, root, r, err)) return false;
    const QuakeCut c = cutRecord(*M, r, window > 0.0 ? window : M->window);
    const double dt = c.dt, scale = c.scale;
    const size_t i0 = c.i0, N = c.N;
    const double T = dt * (N - 1);
    // cosine tapers so it rises and dies away instead of switching on and off
    const double rise = std::max(0.5, 0.12 * T), fall = std::max(0.8, 0.2 * T);
    q = QuakeRecord();
    q.name = std::string("buyukluk ") + std::to_string(magnitude) + " (" + r.name + " kaydindan)";
    q.dt = dt;
    q.acc.resize(N);
    q.vel.assign(N, d3(0, 0, 0));
    q.disp.assign(N, d3(0, 0, 0));
    q.pga = 0.0;
    for (size_t i = 0; i < N; ++i) {
        const double t = dt * i;
        double w = 1.0;
        if (t < rise) w = 0.5 - 0.5 * std::cos(3.14159265358979 * t / rise);
        if (T - t < fall) w = std::min(w, 0.5 - 0.5 * std::cos(3.14159265358979 * (T - t) / fall));
        q.acc[i] = r.acc[i0 + i] * (w * scale);
        const double ah = std::sqrt(q.acc[i].x * q.acc[i].x + q.acc[i].z * q.acc[i].z);
        if (ah > q.pga) { q.pga = ah; q.tPeak = t; }
    }
    for (size_t i = 1; i < N; ++i) {
        q.vel[i] = q.vel[i - 1] + (q.acc[i] + q.acc[i - 1]) * (0.5 * dt);
        q.disp[i] = q.disp[i - 1] + (q.vel[i] + q.vel[i - 1]) * (0.5 * dt);
    }
    // baseline: a cut record ends moving; a smooth correction (zero slope at the start, matching value and slope at
    // the end) brings the ground back to rest where it began. Its acceleration is ~6 d / T^2: millimetres per s^2.
    const double3 dEnd = q.disp[N - 1], vEnd = q.vel[N - 1];
    for (size_t i = 0; i < N; ++i) {
        const double s = T > 0.0 ? dt * i / T : 0.0;
        const double h = s * s * (3.0 - 2.0 * s), g = s * s * (s - 1.0);
        q.disp[i] = q.disp[i] - dEnd * h - vEnd * (T * g);
    }
    for (size_t i = 1; i + 1 < N; ++i) q.vel[i] = (q.disp[i + 1] - q.disp[i - 1]) * (0.5 / dt);
    q.vel[0] = q.vel[N - 1] = d3(0, 0, 0);
    return true;
}

// ---- playing a quake for any length
static double rampUp(double x) {   // 0 -> 1 with zero slope at both ends
    x = std::min(1.0, std::max(0.0, x));
    return 0.5 - 0.5 * std::cos(3.14159265358979 * x);
}

double QuakePlay::gain(double t) const {
    double g = 1.0;
    if (loop) {
        if (t < rise) g = rampUp(t / rise);
        if (!endless() && length - t < fall) g = std::min(g, rampUp((length - t) / fall));
    }
    if (stopAt >= 0.0) g = std::min(g, stopGain * (1.0 - rampUp((t - stopAt) / kStopFade)));
    return g;
}

double3 QuakePlay::displacement(double t) const {
    const double g = gain(t);
    if (!loop) return rec.displacement(t) * g;   // exactly the record until a stop (g = 1)
    const size_t P = rec.disp.size();
    if (P < 2 || t <= 0.0) return d3(0, 0, 0);
    const double u = std::fmod(t, rec.dt * double(P)) / rec.dt;
    const size_t i = std::min(P - 1, size_t(u));
    const double w = u - double(i);
    return (rec.disp[i] * (1.0 - w) + rec.disp[(i + 1) % P] * w) * g;
}

bool QuakePlay::over(double t) const {
    if (stopAt >= 0.0 && t >= stopAt + kStopFade) return true;
    return !endless() && t > length;
}

void QuakePlay::stop(double t) {
    if (stopAt >= 0.0) return;
    stopGain = gain(t);
    stopAt = t;
}

double quakeOwnSeconds(int magnitude) {
    const QuakeMagnitude* M = quakeMagnitude(magnitude);
    return M ? M->window : 0.0;
}

bool quakePlay(int magnitude, double seconds, const std::string& root, QuakePlay& q, std::string& err) {
    const QuakeMagnitude* M = quakeMagnitude(magnitude);
    if (!M) { err = "no such magnitude"; return false; }
    q = QuakePlay();
    q.magnitude = magnitude;
    if (seconds == 0.0) seconds = M->window;
    if (seconds > 0.0 && seconds <= M->window + 1e-9) {   // the record cut to this length (its own: the measured one)
        if (!quakeLoadMagnitude(magnitude, root, q.rec, err, std::min(seconds, M->window))) return false;
        q.length = q.rec.duration();
        return true;
    }
    QuakeRecord r;
    if (!quakeLoad(M->record, root, r, err)) return false;
    const QuakeCut c = cutRecord(*M, r, M->window);
    const double dt = c.dt, T = dt * (c.N - 1);
    const size_t C = std::max<size_t>(2, size_t(std::min(2.0, 0.1 * T) / dt));   // the crossfade
    if (c.N < 2 * C + 4) { err = "record too short to loop"; return false; }
    const size_t P = c.N - C;
    std::vector<double3> a(P), v(P), d(P);
    for (size_t i = 0; i < P; ++i) {
        a[i] = r.acc[c.i0 + i] * c.scale;
        if (i < C) {   // the end fades into the start: after the last sample comes (almost) the one that followed it
            const double s = (double(i) + 0.5) / double(C) * 1.5707963267948966;
            a[i] = a[i] * std::sin(s) + r.acc[c.i0 + P + i] * (c.scale * std::cos(s));
        }
    }
    // around the loop: with the mean out of a, the trapezoids close (v returns to v[0]); with the mean out of v, d does
    auto centre = [&](std::vector<double3>& x) {
        double3 m = d3(0, 0, 0);
        for (const double3& e : x) m = m + e;
        m = m * (1.0 / double(P));
        for (double3& e : x) e = e - m;
    };
    centre(a);
    v[0] = d3(0, 0, 0);
    for (size_t i = 1; i < P; ++i) v[i] = v[i - 1] + (a[i] + a[i - 1]) * (0.5 * dt);
    centre(v);
    d[0] = d3(0, 0, 0);
    for (size_t i = 1; i < P; ++i) d[i] = d[i - 1] + (v[i] + v[i - 1]) * (0.5 * dt);
    centre(d);
    q.rec.name = std::string("buyukluk ") + std::to_string(magnitude) + " dongusu (" + r.name + " kaydindan)";
    q.rec.dt = dt;
    q.rec.pga = 0.0;
    for (size_t i = 0; i < P; ++i) {
        const double ah = std::sqrt(a[i].x * a[i].x + a[i].z * a[i].z);
        if (ah > q.rec.pga) { q.rec.pga = ah; q.rec.tPeak = dt * i; }
    }
    q.rec.acc = std::move(a);
    q.rec.vel = std::move(v);
    q.rec.disp = std::move(d);
    q.loop = true;
    q.length = seconds > 0.0 ? seconds : 0.0;
    q.rise = std::max(0.5, 0.12 * T);
    q.fall = std::max(0.8, 0.2 * T);
    return true;
}

int quakeCount() { return int(sizeof(kQuakes) / sizeof(kQuakes[0])); }
std::string quakeName(int i) { return i >= 0 && i < quakeCount() ? kQuakes[i].name : std::string(); }
bool quakeLoad(int i, const std::string& root, QuakeRecord& q, std::string& err) {
    if (i < 0 || i >= quakeCount()) { err = "no such record"; return false; }
    const std::string d = root + "/assets/deprem/";
    q.name = kQuakes[i].name;
    return q.load(d + kQuakes[i].e, d + kQuakes[i].n, d + kQuakes[i].u, err);
}
