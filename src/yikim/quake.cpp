#include "yikim/quake.h"

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
        pga = std::max(pga, std::sqrt(c[0][i] * c[0][i] + c[1][i] * c[1][i]));
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

int quakeCount() { return int(sizeof(kQuakes) / sizeof(kQuakes[0])); }
std::string quakeName(int i) { return i >= 0 && i < quakeCount() ? kQuakes[i].name : std::string(); }
bool quakeLoad(int i, const std::string& root, QuakeRecord& q, std::string& err) {
    if (i < 0 || i >= quakeCount()) { err = "no such record"; return false; }
    const std::string d = root + "/assets/deprem/";
    q.name = kQuakes[i].name;
    return q.load(d + kQuakes[i].e, d + kQuakes[i].n, d + kQuakes[i].u, err);
}
