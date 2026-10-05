// Scene 7 "Ölçek", second option "Hız": the side view's 2D layer (see olcek_hiz_yan.h).
#include "scenes/olcek/olcek_hiz_yan.h"
#include "scenes/olcek/olcek_hiz_pano.h"   // the numbers' words: distText, speedText, betaText
#include "app/lang.h"
#include "app/theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace olcek::hiz {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kShutter = 1.0 / 60.0;   // the eye's shutter, in the passenger's seconds (PLAN §3.2)
constexpr double kTurnTime = 0.9;         // the rocket turning round to brake, as drawn (s)

uint64_t mix64(uint64_t x) {   // splitmix64: the medium's points from their cell, the same every frame
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
double u01(uint64_t h) { return double(h >> 11) * (1.0 / 9007199254740992.0); }
double smooth01(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// the camera of the picture under this layer (olcek_world.cpp's side camera, olcek_kernels.cu's rays): a point from the
// rocket (m, rest frame) to window pixels
struct Cam {
    const SideLayer& L;
    double tX, tY;
    explicit Cam(const SideLayer& l) : L(l), tX(l.tanHalfX), tY(l.tanHalfX * l.view.h / std::max(l.view.w, 1.0f)) {}
    bool at(V3 rel, double& x, double& y, double* zOut = nullptr) const {
        const V3 r = rel - L.focus;
        const double X = dot(r, L.right), Y = dot(r, L.up), Z = dot(r, L.fwd) + L.D;
        if (Z <= 1e-3 * L.D) return false;
        x = L.view.x + L.view.w * 0.5 * (1.0 + X / (Z * tX));
        y = L.view.y + L.view.h * 0.5 * (1.0 - Y / (Z * tY));
        if (zOut) *zOut = Z;
        return true;
    }
    double pxPerM() const { return L.view.w / L.W; }   // at the rocket's depth
};

// a segment clipped to the window grown by a margin (Liang-Barsky): the far ends of the way run to 1e20 pixels
bool clipSeg(double& x0, double& y0, double& x1, double& y1, const ui::Rect& r, double m) {
    const double xmin = r.x - m, xmax = r.x + r.w + m, ymin = r.y - m, ymax = r.y + r.h + m;
    double t0 = 0.0, t1 = 1.0;
    const double dx = x1 - x0, dy = y1 - y0;
    auto edge = [&](double p, double q) {
        if (p == 0.0) return q >= 0.0;
        const double t = q / p;
        if (p < 0.0) {
            if (t > t1) return false;
            t0 = std::max(t0, t);
        } else {
            if (t < t0) return false;
            t1 = std::min(t1, t);
        }
        return true;
    };
    if (!edge(-dx, x0 - xmin) || !edge(dx, xmax - x0) || !edge(-dy, y0 - ymin) || !edge(dy, ymax - y0)) return false;
    const double ax = x0 + t0 * dx, ay = y0 + t0 * dy;
    x1 = x0 + t1 * dx;
    y1 = y0 + t1 * dy;
    x0 = ax;
    y0 = ay;
    return true;
}

void segment(double x0, double y0, double x1, double y1, const ui::Rect& view, float width, ui::Color c) {
    if (!clipSeg(x0, y0, x1, y1, view, 8.0)) return;
    ui::line(float(x0), float(y0), float(x1), float(y1), width, c);
}

// ---------------------------------------------------------------------------------------------------------------------
// the interstellar medium (PLAN §3.2: its density a representation, its motion real). Points fixed in the rest frame on
// lattices of 10^k m along the leg, the two nearest the screen's scale crossfaded; each drawn along the way it makes
// against the rocket in the shutter (gamma beta c per passenger second: the rest frame's way in your time). The rocket's
// place along the leg (double-double, up to 10^22 m) is taken modulo the lattice's period exactly, so the points stand
// still in the rest frame at any distance. As the streaks outgrow the screen the points of a cell gather onto fixed rows
// (their scatter fades) and the rows are drawn as steady lines across: no frame-to-frame shimmer at any speed.

constexpr int kPerCell = 16;      // points in a cell, and rows at speed
constexpr double kPeriod = 1024;  // the lattice's period along the leg, in cells

void drawMedium(const SideLayer& L, const Cam& cam, float S) {
    const Ship& s = *L.ship;
    const int leg = std::min(s.leg, 1);
    const V3 dir = L.route->dir[leg];
    const double move = kC * std::sinh(std::max(s.phi, 0.0)) * kShutter;   // m, in the shutter
    const V3 mv = dir * double(s.sense);
    const double hView = L.W * L.view.h / std::max(L.view.w, 1.0f);
    const double ppm = cam.pxPerM();
    const double lw = std::log10(L.W / 5.0);
    const int k0 = int(std::floor(lw));
    const double fr = lw - k0;
    const double span = 0.75 * L.W * 1.8;                  // along the motion: the screen's half width at the far depth
    const double q = smooth01(0.4 * L.W, 3.0 * L.W, move);  // the scatter gathering onto rows
    const ui::Color tint = ui::hex(0xA9C1E8);
    for (int lev = 0; lev < 2; ++lev) {
        const int kk = k0 + lev;
        const double wgt = lev == 0 ? 1.0 - fr : fr;   // the finer lattice fades out as the screen widens
        if (wgt < 0.02) continue;
        const double h = std::pow(10.0, kk);
        const double P = kPeriod * h;
        const uint64_t levKey = uint64_t(int64_t(kk) + 4096) << 40;
        // the rows (the high speeds): steady lines across at the rows' heights and depths
        if (q > 0.0) {
            for (int j = 0; j < kPerCell; ++j) {
                const uint64_t r = mix64(levKey ^ 0xABCDEFull ^ uint64_t(j));
                const double w = u01(mix64(r + 1)) * 1.4 - 0.7;      // depth: -0.7 .. +0.7 of D
                const double Z = L.D * (1.0 + w);
                const double v = (u01(mix64(r + 2)) * 2.0 - 1.0) * 0.6 * hView * Z / L.D;
                const V3 base = L.up * v + L.fwd * (w * L.D);
                double x0, y0;
                if (!cam.at(base, x0, y0)) continue;   // (the motion is across: a row is level on the screen)
                if (y0 < L.view.y || y0 > L.view.y + L.view.h) continue;
                const double near = 1.0 / std::sqrt(Z / L.D);
                const float a = float(0.20 * q * wgt * near * (0.55 + 0.45 * u01(mix64(r + 3))));
                // a streak of light across, brightest in the middle, fading to the edges
                const float th = S * float(0.8 + 0.7 * near), hw = 0.5f * L.view.w;
                ui::rectHGradient(ui::Rect{L.view.x, float(y0) - 0.5f * th, hw, th}, ui::withAlpha(tint, 0.0f), ui::withAlpha(tint, a));
                ui::rectHGradient(ui::Rect{L.view.x + hw, float(y0) - 0.5f * th, hw, th}, ui::withAlpha(tint, a), ui::withAlpha(tint, 0.0f));
            }
        }
        if (q >= 1.0) continue;
        // the points: cells along the leg from the rocket's place modulo the period (exact from the double-double)
        double ph = std::fmod(s.x.hi, P);
        ph = std::fmod(ph + s.x.lo, P);
        if (ph < 0.0) ph += P;
        // the streak ends (now) along the motion that reach the screen: [-span - ahead, span]; along dir by the sense
        const double ahead = std::min(move, 3.0 * L.W);
        double dmin = -span - ahead, dmax = span;
        if (s.sense < 0) {
            dmin = -span;
            dmax = span + ahead;
        }
        const long long c0 = (long long)std::floor((dmin + ph) / h) - 1, c1 = (long long)std::ceil((dmax + ph) / h);
        if (c1 - c0 > 4000) continue;   // (never: the lattice follows the screen's scale)
        for (long long c = c0; c <= c1; ++c) {
            const uint64_t cid = uint64_t(((c % (long long)kPeriod) + (long long)kPeriod) % (long long)kPeriod);
            for (int j = 0; j < kPerCell; ++j) {
                const uint64_t hc = mix64(levKey ^ (cid << 8) ^ uint64_t(j));
                const uint64_t r = mix64(levKey ^ 0xABCDEFull ^ uint64_t(j));
                const double o = u01(hc);
                const double d = (double(c) + o) * h - ph;   // along dir, from the rocket
                // its height and depth: scattered at rest, gathered onto its row as the streaks lengthen
                const double wCell = u01(mix64(hc + 1)) * 1.4 - 0.7, wRow = u01(mix64(r + 1)) * 1.4 - 0.7;
                const double w = wCell + (wRow - wCell) * q;
                const double Z = L.D * (1.0 + w);
                const double vCell = u01(mix64(hc + 2)) * 2.0 - 1.0, vRow = u01(mix64(r + 2)) * 2.0 - 1.0;
                const double v = (vCell + (vRow - vCell) * q) * 0.6 * hView * Z / L.D;
                const double b = 0.35 + 0.65 * u01(mix64(hc + 3));
                const V3 end = dir * d + L.up * v + L.fwd * (w * L.D);
                const V3 start = end + mv * move;
                double x0, y0, x1, y1;
                if (!cam.at(start, x0, y0) || !cam.at(end, x1, y1)) continue;
                const double len = std::hypot(x1 - x0, y1 - y0);
                const double near = 1.0 / std::sqrt(Z / L.D);
                // the same light spread along the streak (softened: a long streak stays a faint line, not nothing)
                const double a = 0.42 * wgt * (1.0 - q) * b * near * std::clamp(std::sqrt(3.0 / std::max(len, 3.0)), 0.22, 1.0);
                if (a < 0.004) continue;
                const float wpx = S * float(0.8 + 0.7 * near);
                if (len < 0.75) {
                    ui::circle(float(x1), float(y1), 0.5f * wpx + 0.25f, ui::withAlpha(tint, float(a)));
                } else {
                    segment(x0, y0, x1, y1, L.view, wpx, ui::withAlpha(tint, float(a)));
                }
            }
        }
        (void)ppm;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// the way and its stops

void drawWay(const SideLayer& L, const Cam& cam, float S) {
    const Ship& s = *L.ship;
    const Route& R = *L.route;
    const int leg = std::min(s.leg, 1);
    const V3 dir = R.dir[leg];
    const double x = s.x.value();                 // from the target (< 0 before it)
    // along the leg from the rocket: t > 0 towards the target. The far ends are brought in before projecting: a point
    // 10^16 m away seen from tens of metres moves by metres for a 1e-16 turn of the axes (the screen's edge is near)
    const double far = 40.0 * std::max(L.W, L.D);
    auto near = [&](double t) { return dir * (t >= 0.0 ? std::min(t, far) : std::max(t, -far)); };
    const double tTarget = -x, tStart = -x - R.length[leg];
    const ui::Color glow = ui::hex(0xCFE0FF);
    double ax, ay, bx, by, rx, ry;
    const bool hasA = cam.at(near(tStart), ax, ay), hasB = cam.at(near(tTarget), bx, by);
    cam.at(V3{}, rx, ry);
    if (s.leg < kLegDone && hasA && hasB) {
        // behind the rocket fainter, ahead brighter: a soft glow and a fine core
        segment(ax, ay, rx, ry, L.view, S * 5.0f, ui::withAlpha(glow, 0.035f));
        segment(ax, ay, rx, ry, L.view, S * 1.1f, ui::withAlpha(glow, 0.16f));
        segment(rx, ry, bx, by, L.view, S * 5.0f, ui::withAlpha(glow, 0.05f));
        segment(rx, ry, bx, by, L.view, S * 1.2f, ui::withAlpha(glow, 0.30f));
    }
    // the stops: on the screen a ring and their names; beyond it, at the edge with an arrow
    const ui::TextStyle nameT = theme::small(theme::Scale{S}, ui::withAlpha(theme::text(), 0.9f));
    const ui::TextStyle distT = theme::mono(theme::Scale{S}, 13.0f, ui::withAlpha(theme::text(), 0.7f));
    auto stop = [&](bool has, double t, double px, double py, double dist, const std::string& name) {
        if (!has) return;
        const std::string d = distText(dist);
        const float m = S * 26.0f;
        const float left = L.view.x + m, right = L.view.x + L.view.w - m;
        const bool inside = std::fabs(t) < 0.48 * L.W && px >= left && px <= right && py >= L.view.y && py <= L.view.y + L.view.h;
        if (inside && std::hypot(px - rx, py - ry) < S * 16.0f) return;   // where the rocket is: its own mark is enough
        if (inside) {
            ui::circle(float(px), float(py), S * 4.5f, ui::withAlpha(glow, 0.20f));
            ui::arc(float(px), float(py), S * 4.5f, 0.0f, float(2.0 * kPi), S * 1.3f, ui::withAlpha(glow, 0.85f));
            ui::text(float(px) + S * 10.0f, float(py) - S * 34.0f, name, nameT);
            ui::text(float(px) + S * 10.0f, float(py) - S * 16.0f, d, distT);
            return;
        }
        // beyond the screen: in the free middle (the graphs keep the edges), on the way's height, an arrow outwards
        double ox, oy;
        if (!cam.at(near(t >= 0.0 ? 2.0 * L.W : -2.0 * L.W), ox, oy)) return;
        const bool toRight = ox > rx;
        const float ex = float(rx + (toRight ? 0.30 : -0.30) * L.view.w);
        const double k = (ex - rx) / (ox - rx);
        const float ey = float(std::clamp(ry + (oy - ry) * k, double(L.view.y + S * 60.0f), double(L.view.y + L.view.h - S * 60.0f)));
        const std::string arrowName = toRight ? name + "  →" : "←  " + name;
        const float wName = ui::measure(arrowName, nameT), wDist = ui::measure(d, distT);
        ui::text(toRight ? ex - wName : ex, ey - S * 34.0f, arrowName, nameT);
        ui::text(toRight ? ex - wDist : ex, ey - S * 16.0f, d, distT);
    };
    if (s.leg < kLegDone) {
        const std::string from = leg == kLegProxima ? app::L("Kalkış · Dünya", "Departure · the Earth") : "Proxima Centauri";
        const std::string to = leg == kLegProxima ? "Proxima Centauri" : "Andromeda";
        stop(hasA, tStart, ax, ay, std::fabs(x + R.length[leg]), from);
        stop(hasB, tTarget, bx, by, std::fabs(x), to);
    } else {
        stop(true, 0.0, rx, ry, 0.0, app::L("Andromeda: vardın", "Andromeda: you have arrived"));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// the rocket: a photon rocket, 14 m from its nose to its mirror's rim, its passenger seated under a glass dome. Its
// length along the motion is the rest frame's: gamma times shorter (Özcan's map, PLAN §3.2). Its drive: a parabolic
// mirror at the back with the light's source at its focus; braking, it is turned round (the light must point ahead).

struct Shape {
    std::vector<float> xy;
};

void drawRocket(const SideLayer& L, const SideState& st, const Cam& cam, float S, double glowLevel) {
    const Ship& s = *L.ship;
    const Kin k = kin(s.phi);
    const double ppm = cam.pxPerM();
    double cx, cy;
    cam.at(V3{}, cx, cy);
    const double sx = ppm * st.facing / k.gamma, sy = ppm;   // metres to pixels: along the motion contracted, turned round
    const double lenPx = 14.0 * ppm / k.gamma;
    const ui::Color gold = ui::hex(0xFFE3A3);
    // far away: a point, and its light behind it
    if (14.0 * ppm < 9.0) {
        ui::circle(float(cx), float(cy), S * 6.0f, ui::withAlpha(theme::text(), 0.10f));
        ui::circle(float(cx), float(cy), S * 2.6f, ui::withAlpha(theme::text(), 0.95f));
        if (glowLevel > 0.0) {
            const double dirBeam = -st.facing >= 0.0 ? 1.0 : -1.0;   // the light leaves behind the nose
            const double x1 = cx + dirBeam * L.view.w;
            segment(cx, cy, x1, cy, L.view, S * 2.0f, ui::withAlpha(gold, float(0.10 + 0.35 * glowLevel)));
        }
        (void)lenPx;
        return;
    }
    auto P = [&](double x, double y) { return std::array<float, 2>{float(cx + x * sx), float(cy - y * sy)}; };
    auto poly = [&](const std::vector<std::array<double, 2>>& pts) {
        std::vector<float> out;
        out.reserve(pts.size() * 2);
        for (const auto& p : pts) {
            const auto q = P(p[0], p[1]);
            out.push_back(q[0]);
            out.push_back(q[1]);
        }
        return out;
    };
    auto closed = [](std::vector<float> v) {
        if (v.size() >= 2) {
            v.push_back(v[0]);
            v.push_back(v[1]);
        }
        return v;
    };
    const float line = std::max(1.0f, S * 1.4f);
    // the drive's light behind the mirror: a soft band as wide as the mirror, out to the screen's edge
    const double rimX = -4.81, rimY = 2.2;
    if (glowLevel > 0.0) {
        const auto a = P(rimX, 0.0);
        const double edge = st.facing >= 0.0 ? L.view.x - 10.0 : L.view.x + L.view.w + 10.0;
        const double x0 = std::clamp(std::min(double(a[0]), edge), double(L.view.x), double(L.view.x + L.view.w));
        const double x1 = std::clamp(std::max(double(a[0]), edge), double(L.view.x), double(L.view.x + L.view.w));
        const double half = rimY * sy * 1.15;
        if (x1 > x0 && half >= 1.0) {
            const float al = float(0.10 + 0.32 * glowLevel);
            ui::rectGradient(ui::Rect{float(x0), a[1] - float(half), float(x1 - x0), float(half)}, ui::withAlpha(gold, 0.0f), ui::withAlpha(gold, al));
            ui::rectGradient(ui::Rect{float(x0), a[1], float(x1 - x0), float(half)}, ui::withAlpha(gold, al), ui::withAlpha(gold, 0.0f));
        }
        segment(a[0], a[1], edge, a[1], L.view, float(std::max(0.16 * half, 1.0)), ui::withAlpha(ui::hex(0xFFF4DA), float(0.25 + 0.5 * glowLevel)));
    }
    // the mirror: a parabola opening backwards, x = -3.6 - y^2 / 4 (its focus at x = -4.6)
    std::vector<std::array<double, 2>> dish;
    for (int i = 0; i <= 24; ++i) {
        const double y = -rimY + 2.0 * rimY * i / 24.0;
        dish.push_back({-3.6 - y * y / 4.0, y});
    }
    {
        std::vector<float> f = poly(dish);
        ui::fill(f.data(), int(f.size() / 2), ui::hex(0x2A3038, 0.92f));
        ui::polyline(f.data(), int(f.size() / 2), line, ui::withAlpha(theme::text(), 0.85f));
    }
    // the strut from the hull to the mirror
    {
        const auto a = P(-3.3, 0.0), b = P(-3.6, 0.0);
        ui::line(a[0], a[1], b[0], b[1], std::max(1.0f, float(0.5 * sy)), ui::withAlpha(theme::text(), 0.8f));
    }
    // the source at the focus: a small white core in a soft glow
    if (glowLevel > 0.0) {
        const auto f = P(-4.6, 0.0);
        const float r = float(std::max(0.16 * sy, 1.6 * S));
        const float g = r * 6.0f;
        ui::shadow(ui::Rect{f[0] - g * 0.5f, f[1] - g * 0.5f, g, g}, g * 0.5f, g * 0.6f, ui::withAlpha(gold, float(0.20 + 0.45 * glowLevel)));
        ui::circle(f[0], f[1], r, ui::withAlpha(ui::hex(0xFFFBF0), float(0.7 + 0.3 * glowLevel)));
    }
    // the glass dome (drawn first: the hull covers its lower half), its passenger seated inside
    std::vector<std::array<double, 2>> dome;
    for (int i = 0; i <= 28; ++i) {
        const double t = kPi * i / 28.0;
        dome.push_back({2.6 + 1.5 * std::cos(t), 0.85 + 0.85 * std::sin(t)});
    }
    {
        std::vector<float> f = poly(dome);
        ui::fill(f.data(), int(f.size() / 2), ui::hex(0x8FD6FF, 0.16f));
    }
    {   // the passenger: faceless, seated and leaning back a little; above the hull's line only the shoulders and head
        std::vector<std::array<double, 2>> head;
        for (int i = 0; i < 24; ++i) {
            const double t = 2.0 * kPi * i / 24.0;
            head.push_back({2.62 + 0.115 * std::cos(t), 1.43 + 0.13 * std::sin(t)});
        }
        std::vector<float> h = poly(head);
        const ui::Color skin = ui::hex(0xDDE2EA, 0.95f);
        ui::fill(h.data(), int(h.size() / 2), skin);
        const auto b0 = P(2.47, 0.95), b1 = P(2.56, 1.22);   // the back, up to the neck
        ui::line(b0[0], b0[1], b1[0], b1[1], std::max(1.2f, float(0.27 * sy)), skin);
    }
    // the hull: a slim body, its nose rounded to a point, its back flat-rounded
    const std::vector<std::array<double, 2>> hull = {
        {-3.3, 0.55}, {-3.15, 0.85}, {-2.9, 1.0}, {4.0, 1.0}, {5.2, 0.85}, {6.2, 0.52}, {6.8, 0.22}, {7.0, 0.0},
        {6.8, -0.22}, {6.2, -0.52}, {5.2, -0.85}, {4.0, -1.0}, {-2.9, -1.0}, {-3.15, -0.85}, {-3.3, -0.55}};
    {
        std::vector<float> f = poly(hull);
        ui::fill(f.data(), int(f.size() / 2), ui::hex(0x1B2129, 0.96f));
        ui::polyline(closed(f).data(), int(f.size() / 2) + 1, line, ui::withAlpha(theme::text(), 0.9f));
    }
    {   // the dome's rim over the hull's line, and a faint band along the hull
        std::vector<float> f = poly(dome);
        ui::polyline(f.data(), int(f.size() / 2), line, ui::withAlpha(ui::hex(0xCFEFFF), 0.85f));
        const auto a = P(-2.6, -0.35), b = P(5.0, -0.35);
        ui::line(a[0], a[1], b[0], b[1], std::max(1.0f, S * 0.9f), ui::withAlpha(theme::text(), 0.18f));
    }
}

// the speed beside the rocket (Özcan's reference: v and beta along the way), and the way made in one of your seconds
void drawSpeedLabel(const SideLayer& L, const Cam& cam, float S) {
    const Ship& s = *L.ship;
    if (s.phi <= 0.0 || s.leg >= kLegDone) return;
    const Kin k = kin(s.phi);
    double cx, cy;
    cam.at(V3{}, cx, cy);
    const double half = std::max(2.4 * cam.pxPerM(), double(S * 10.0f));   // below the rocket (or its point)
    const float x = float(cx) + S * 18.0f, y0 = float(std::min(cy + half + S * 16.0f, double(L.view.y + L.view.h - S * 90.0f)));
    const ui::TextStyle v = theme::mono(theme::Scale{S}, 14.0f, ui::withAlpha(theme::text(), 0.92f));
    const ui::TextStyle b = theme::mono(theme::Scale{S}, 13.0f, ui::withAlpha(theme::text(), 0.72f));
    const ui::TextStyle w = theme::small(theme::Scale{S}, ui::hex(0xFFE3A3, 0.88f));
    ui::text(x, y0, "→  v = " + speedText(k), v);
    ui::text(x, y0 + S * 21.0f, "β = " + betaText(k), b);
    ui::text(x, y0 + S * 41.0f, app::L("senin bir saniyende ", "in one second of yours ") + distText(kC * k.gb), w);
}

}   // namespace

void drawSide(const SideLayer& L, SideState& st, float S) {
    if (!L.ship || !L.route || L.view.w <= 0.0f) return;
    const Ship& s = *L.ship;
    const Cam cam(L);
    // the drive's light: its acceleration on a log scale (1 g .. the highest), eased; off when coasting or at rest
    const bool driving = s.drive == Drive::Thrust || s.drive == Drive::Brake;
    double want = 0.0;
    if (driving) {
        const double g = driveRate(s.phi) * kC / kG0;
        want = std::clamp(0.2 + 0.8 * std::log10(std::max(g, 1.0)) / 7.05, 0.0, 1.0);
    }
    const double ease = 1.0 - std::exp(-std::max(L.dt, 1.0 / 240.0) * 6.0);
    st.glow += (want - st.glow) * ease;
    if (st.glow < 1e-3 && want == 0.0) st.glow = 0.0;
    // turned round to brake (the light must point ahead), back round to thrust; drawn as a turn about the vertical
    if (s.drive == Drive::Brake) st.turned = true;
    else if (s.drive == Drive::Thrust || s.phi <= 0.0) st.turned = false;
    const double target = st.turned ? 1.0 : 0.0;
    const double stepT = std::max(L.dt, 0.0) / kTurnTime;
    st.turn = target > st.turn ? std::min(target, st.turn + stepT) : std::max(target, st.turn - stepT);
    st.facing = std::cos(kPi * (st.turn * st.turn * (3.0 - 2.0 * st.turn)));
    drawMedium(L, cam, S);
    drawWay(L, cam, S);
    drawRocket(L, st, cam, S, st.glow);
    drawSpeedLabel(L, cam, S);
}

}   // namespace olcek::hiz
