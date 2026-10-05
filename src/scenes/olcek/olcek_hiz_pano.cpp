// Scene 7 "Ölçek", second option "Hız": the five panels of graphs (see olcek_hiz_pano.h).
#include "scenes/olcek/olcek_hiz_pano.h"
#include "app/lang.h"
#include "app/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace olcek::hiz {

using app::L;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMpC2eV = 938.27208816e6 + 0.51099895e6;   // a hydrogen atom's rest energy (eV)
constexpr double kG = 9.80665;
constexpr double kRocketKg = 1000.0;        // the plan's 1 t rocket (the costs are per tonne)

const char* kSupDigits[10] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
std::string supInt(int e) {
    std::string out = e < 0 ? "⁻" : "";
    for (char c : std::to_string(std::abs(e))) out += kSupDigits[c - '0'];
    return out;
}
std::string sig3(double v) {
    if (!(v > 0.0)) return v == 0.0 ? "0" : theme::num(v, 2);
    if (v >= 1e6 || v < 0.01) return sciText(v, 2);
    if (v >= 100.0) return theme::thousands((unsigned long long)std::llround(v));
    if (v >= 10.0) return theme::num(v, 1);
    if (v >= 1.0) return theme::num(v, 2);
    return theme::num(v, 2 - int(std::floor(std::log10(v))));   // three digits below 1 too (0,0116, not 0,01)
}

ui::Color white(float a = 1.0f) { return ui::withAlpha(theme::text(), a); }
ui::Color red(float a = 1.0f) { return ui::hex(0xF06B5C, a); }      // the Earth's clock and world line
ui::Color gold(float a = 1.0f) { return ui::hex(0xE9C46A, a); }     // light: c, the light cone, the light from ahead

struct Box {
    float x = 0, y = 0, w = 0, h = 0;
};

// a panel's glass and title; the box inside it
Box panel(float x, float y, float w, float h, float S, const std::string& title, const std::string& right = std::string()) {
    const ui::Rect r{x, y, w, h};
    ui::shadow(r, S * 18.0f, S * 24.0f, ui::hex(0x000000, 0.35f));
    ui::panel(r, theme::glass(), theme::glassBorder(), S * 16.0f, std::max(1.0f, S));
    const ui::TextStyle cap = theme::caption(theme::Scale{S}, theme::faint());
    ui::text(x + S * 20.0f, y + S * 16.0f, title, cap);
    if (!right.empty()) {
        const ui::TextStyle val = theme::mono(theme::Scale{S}, 15.0f, white(0.92f));
        ui::text(x + w - S * 20.0f - ui::measure(right, val), y + S * 14.0f, right, val);
    }
    return Box{x + S * 20.0f, y + S * 46.0f, w - S * 40.0f, h - S * 62.0f};
}

// 1, 2, 5 x 10^k: the nearest at or above v
double niceStep(double v) {
    const double e = std::pow(10.0, std::floor(std::log10(v)));
    const double m = v / e;
    return (m <= 1.0 ? 1.0 : m <= 2.0 ? 2.0 : m <= 5.0 ? 5.0 : 10.0) * e;
}

// a length unit for a diagram whose half size is h metres (light-time units: the diagram's ct and x share it)
struct Unit {
    double m;
    std::string name;
};
Unit unitFor(double h) {
    constexpr double c = kC;
    if (h < 60.0 * c) return {c, L("ışık saniyesi", "light seconds")};
    if (h < 3600.0 * c) return {60.0 * c, L("ışık dakikası", "light minutes")};
    if (h < 86400.0 * c) return {3600.0 * c, L("ışık saati", "light hours")};
    if (h < kYear * c) return {86400.0 * c, L("ışık günü", "light days")};
    if (h < 1e3 * kLy) return {kLy, L("ışık yılı", "light years")};
    if (h < 1e6 * kLy) return {1e3 * kLy, L("bin ışık yılı", "thousand light years")};
    return {1e6 * kLy, L("milyon ışık yılı", "million light years")};
}

// how much of the Sun's output an energy is: seconds, minutes, hours, days, years
std::string sunTime(double joules) {
    const double s = joules / kSunLuminosity;
    if (s < 120.0) return sig3(s) + L(" saniyesi", " seconds");
    if (s < 7200.0) return sig3(s / 60.0) + L(" dakikası", " minutes");
    if (s < 86400.0 * 3.0) return sig3(s / 3600.0) + L(" saati", " hours");
    if (s < kYear) return sig3(s / 86400.0) + L(" günü", " days");
    return sig3(s / kYear) + L(" yılı", " years");
}

// the band a blackbody's peak lies in (Wien: lambda = 2.898e-3 / T)
std::string bandOf(double T) {
    const double lam = 2.897771955e-3 / T;
    if (lam > 1.0) return L("radyo", "radio");
    if (lam > 1e-3) return L("mikrodalga", "microwaves");
    if (lam > 7.5e-7) return L("kızılötesi", "infrared");
    if (lam > 3.8e-7) return L("görünür", "visible");
    if (lam > 1e-8) return L("morötesi", "ultraviolet");
    if (lam > 1e-11) return L("X ışını", "X-rays");
    return L("gama ışını", "gamma rays");
}

// ---------------------------------------------------------------------------------------------------------------------
// HIZ: the nines' ladder, the speed bar against the wall of light, gamma's curve

void drawSpeed(const Pano& p, PanoState& st, Box b, float S) {
    const Kin& k = p.k;
    const ui::TextStyle lab = theme::mono(theme::Scale{S}, 12.5f, white(0.55f));
    const ui::TextStyle small = theme::small(theme::Scale{S}, white(0.62f));
    const float axisH = b.h - S * 30.0f;   // the room under the plots for their names
    // the ladder: 0 .. 22 nines up the left; the cap at 21
    {
        const float x = b.x + S * 26.0f, y0 = b.y + axisH, y1 = b.y + S * 4.0f;
        const float per = (y0 - y1) / 22.0f;
        ui::line(x, y0, x, y1, S * 1.2f, white(0.35f));
        for (int n = 0; n <= 22; ++n) {
            const float y = y0 - per * n;
            const bool major = n % 3 == 0;
            ui::line(x - S * (major ? 8.0f : 4.0f), y, x + S * (major ? 8.0f : 4.0f), y, S * (major ? 1.2f : 1.0f), white(major ? 0.55f : 0.3f));
            if (major && n > 0) {
                const std::string t = std::to_string(n);
                ui::text(x - S * 14.0f - ui::measure(t, lab), y - S * 8.0f, t, lab);
            }
        }
        // the cap: a gold rung at 21
        const float yc = y0 - per * 21.0f;
        ui::line(x - S * 12.0f, yc, x + S * 12.0f, yc, S * 1.6f, gold(0.85f));
        // where it is: a bar up the ladder, a bright rung
        const float yn = y0 - per * float(std::clamp(k.nines, 0.0, 22.0));
        ui::rect(ui::Rect{x - S * 3.0f, yn, S * 6.0f, y0 - yn}, white(0.28f), S * 2.0f);
        ui::line(x - S * 16.0f, yn, x + S * 16.0f, yn, S * 2.2f, white(0.95f));
        ui::text(x - S * 20.0f, b.y + axisH + S * 8.0f, L("DOKUZ", "NINES"), theme::caption(theme::Scale{S}, white(0.5f)));
    }
    // the speed bar: 0 .. c, the wall of light at c
    {
        const float x = b.x + S * 96.0f, y0 = b.y + axisH, y1 = b.y + S * 4.0f + (axisH - S * 4.0f) * (1.0f / 23.0f);
        ui::rect(ui::Rect{x - S * 5.0f, y1, S * 10.0f, y0 - y1}, white(0.07f), S * 3.0f);
        const float yb = y0 - (y0 - y1) * float(k.beta);
        ui::rect(ui::Rect{x - S * 5.0f, yb, S * 10.0f, y0 - yb}, white(0.85f), S * 3.0f);
        ui::line(x - S * 16.0f, y1, x + S * 16.0f, y1, S * 2.0f, gold(0.95f));
        ui::text(x + S * 20.0f, y1 - S * 10.0f, "c", theme::mono(theme::Scale{S}, 15.0f, gold(0.95f)));
        ui::text(x - S * 10.0f, b.y + axisH + S * 8.0f, "v", theme::caption(theme::Scale{S}, white(0.5f)));
    }
    // gamma(beta): beta across (0 .. 1), log10 gamma up, the axis grows with it
    {
        const float px = b.x + S * 168.0f, pw = b.w - S * 176.0f, py0 = b.y + axisH, py1 = b.y + S * 4.0f;
        const double want = std::max(1.0, std::ceil(std::log10(std::max(k.gamma, 1.0)) + 0.35));
        if (st.gammaTop < 0.0) st.gammaTop = want;
        st.gammaTop += (want - st.gammaTop) * std::min(1.0, std::max(p.dt, 1.0 / 240.0) * 3.0);
        const double top = st.gammaTop;
        auto X = [&](double beta) { return px + float(beta) * pw; };
        auto Y = [&](double lg) { return py0 - float(lg / top) * (py0 - py1); };
        // the frame: the beta axis, the wall at 1 (gold), gamma's decades
        ui::line(px, py0, px + pw, py0, S * 1.2f, white(0.35f));
        ui::line(px + pw, py0, px + pw, py1, S * 1.4f, gold(0.75f), S * 5.0f, S * 4.0f);
        const int decStep = top <= 6.0 ? 1 : top <= 12.0 ? 2 : 5;
        for (int d = 0; d <= int(top); d += decStep) {
            const float y = Y(d);
            ui::line(px, y, px + pw, y, S * 1.0f, white(d == 0 ? 0.35f : 0.12f));
            const std::string t = d == 0 ? "1" : d == 1 ? "10" : "10" + supInt(d);
            ui::text(px + S * 4.0f, y - S * 17.0f, t, lab);
        }
        // the curve: by rapidity (dense where it climbs the wall)
        const double phiTop = std::acosh(std::pow(10.0, top));
        std::vector<float> pts;
        for (int i = 0; i <= 160; ++i) {
            const double ph = phiTop * i / 160.0;
            pts.push_back(X(std::tanh(ph)));
            pts.push_back(Y(std::log10(std::cosh(ph))));
        }
        ui::polyline(pts.data(), int(pts.size() / 2), S * 1.6f, white(0.85f));
        // the point
        const float cx = X(k.beta), cy = Y(std::log10(std::max(k.gamma, 1.0)));
        ui::circle(cx, cy, S * 5.0f, white(1.0f));
        ui::circle(cx, cy, S * 9.0f, white(0.18f));
        ui::text(px, b.y + axisH + S * 8.0f, "γ(β)", small);
        const std::string g = "γ " + gammaText(k.gamma);
        ui::text(px + pw - ui::measure(g, theme::mono(theme::Scale{S}, 14.0f)), b.y + axisH + S * 8.0f, g, theme::mono(theme::Scale{S}, 14.0f, white(0.9f)));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// UZAY-ZAMAN: the Minkowski diagram of the leg

void drawMinkowski(const Pano& p, PanoState& st, Box b, float S) {
    const Kin& k = p.k;
    const Ship& s = *p.ship;
    const Route& R = *p.route;
    const int Lg = std::min(s.leg, 1);
    const double Lm = R.length[Lg];
    const double X = p.xFromStart, CT = std::max(p.ctSinceDepart, 0.0);
    // the view: the leg's start and now, square (light at 45 degrees), growing smoothly
    const double want = std::max({0.62 * std::max(X, CT), 2.0 * kC, 0.1 * std::fabs(X - CT)});
    if (st.minkScale < 0.0) st.minkScale = want;
    st.minkScale = std::exp(std::log(st.minkScale) + (std::log(want) - std::log(st.minkScale)) * std::min(1.0, std::max(p.dt, 1.0 / 240.0) * 2.5));
    const double H = st.minkScale;
    const double cxW = 0.5 * X, cyW = 0.5 * CT;
    const float side = std::min(b.w, b.h - S * 26.0f);
    const float ox = b.x + 0.5f * (b.w - side), oy = b.y;
    const float pxPerM = float(side / (2.0 * H));
    auto SX = [&](double x) { return ox + 0.5f * side + float(x - cxW) * pxPerM; };
    auto SY = [&](double ct) { return oy + 0.5f * side - float(ct - cyW) * pxPerM; };
    ui::Rect clip{ox, oy, side, side};
    ui::rect(clip, ui::hex(0x000000, 0.25f), S * 6.0f);
    ui::scissor(&clip);
    // the Earth's frame: its grid
    const Unit u = unitFor(H);
    const double step = niceStep(2.0 * H / 5.0 / u.m) * u.m;
    {
        const double x0 = std::floor((cxW - H) / step) * step, x1 = cxW + H;
        for (double x = x0; x <= x1; x += step) ui::line(SX(x), oy, SX(x), oy + side, S * 1.0f, white(0.08f));
        const double c0 = std::floor((cyW - H) / step) * step, c1 = cyW + H;
        for (double c = c0; c <= c1; c += step) ui::line(ox, SY(c), ox + side, SY(c), S * 1.0f, white(0.08f));
    }
    // the start's world line (the Earth, then Proxima) and the target's
    ui::line(SX(0.0), oy, SX(0.0), oy + side, S * 1.6f, red(0.85f));
    ui::line(SX(Lm), oy, SX(Lm), oy + side, S * 1.2f, white(0.45f), S * 6.0f, S * 5.0f);
    // the rocket's grid: lines of its own time (along its x') and place (along its t'), a unit apart in its frame;
    // they fold towards the light line and squeeze by 1/gamma (only those a few pixels apart are drawn)
    const double gb = k.gb, g = k.gamma, beta = k.beta;
    {
        const double perp = step / (g * std::sqrt(1.0 + beta * beta)) * pxPerM;   // pixels between neighbours
        int every = 1;
        while (perp * every < 7.0 && every < (1 << 30)) every *= 2;
        const float a = float(std::clamp(0.10 * std::min(1.0, perp * every / 14.0) + 0.04, 0.04, 0.14));
        const double ext = 4.0 * H;   // long enough to cross the box at any slant
        for (int i = -40; i <= 40; ++i) {
            const double kk = double(i) * every * step;
            // constant t': through event + kk (gamma beta, gamma), along (1, beta)
            const double tx = X + kk * gb, tc = CT + kk * g;
            ui::line(SX(tx - ext), SY(tc - ext * beta), SX(tx + ext), SY(tc + ext * beta), S * 1.0f, white(a));
            // constant x': through event + kk (gamma, gamma beta), along (beta, 1)
            const double qx = X + kk * g, qc = CT + kk * gb;
            ui::line(SX(qx - ext * beta), SY(qc - ext), SX(qx + ext * beta), SY(qc + ext), S * 1.0f, white(a));
        }
    }
    // the light cone at now
    {
        const double e = 4.0 * H;
        ui::line(SX(X - e), SY(CT - e), SX(X + e), SY(CT + e), S * 1.3f, gold(0.75f), S * 5.0f, S * 4.0f);
        ui::line(SX(X - e), SY(CT + e), SX(X + e), SY(CT - e), S * 1.3f, gold(0.75f), S * 5.0f, S * 4.0f);
    }
    // the rocket's axes: t' along its world line, x' its "now" (both towards the light line as it speeds up)
    {
        const double e = 4.0 * H;
        ui::line(SX(X - e * beta), SY(CT - e), SX(X + e * beta), SY(CT + e), S * 1.4f, white(0.9f));
        ui::line(SX(X - e), SY(CT - e * beta), SX(X + e), SY(CT + e * beta), S * 1.4f, white(0.9f));
    }
    // the world line so far
    if (p.trace && p.trace->x.size() >= 2) {
        std::vector<float> pts;
        pts.reserve(p.trace->x.size() * 2 + 2);
        for (size_t i = 0; i < p.trace->x.size(); ++i) {
            pts.push_back(SX(p.trace->x[i]));
            pts.push_back(SY(p.trace->ct[i]));
        }
        pts.push_back(SX(X));
        pts.push_back(SY(CT));
        ui::polyline(pts.data(), int(pts.size() / 2), S * 2.2f, white(1.0f));
    }
    // where "now" cuts the start's world line, and where the light now arriving left it
    const double ctNowStart = CT - beta * X, ctSeen = CT - X;
    ui::circle(SX(0.0), SY(ctNowStart), S * 5.0f, red(1.0f));
    ui::circle(SX(0.0), SY(ctSeen), S * 4.0f, gold(1.0f));
    ui::circle(SX(X), SY(CT), S * 5.5f, white(1.0f));
    ui::scissor(nullptr);
    // the axes' names and the unit
    const ui::TextStyle lab = theme::mono(theme::Scale{S}, 12.5f, white(0.6f));
    ui::text(ox, oy + side + S * 6.0f, L("ızgara: ", "grid: ") + sig3(step / u.m) + " " + u.name, lab);
    const std::string leg = s.leg == kLegProxima ? L("Dünya", "Earth") : L("Proxima", "Proxima");
    const std::string tgt = s.leg == kLegProxima ? "Proxima" : "Andromeda";
    const std::string key = L("kırmızı: ", "red: ") + leg + L(" · kesik: ", " · dashed: ") + tgt;
    ui::text(ox + side - ui::measure(key, lab), oy + side + S * 6.0f, key, lab);
}

// ---------------------------------------------------------------------------------------------------------------------
// YOL: the two rulers, the stopping distance, space squeezed

void drawRoad(const Pano& p, PanoState&, Box b, float S) {
    const Kin& k = p.k;
    const Ship& s = *p.ship;
    const Route& R = *p.route;
    const int Lg = std::min(s.leg, 1);
    const double Lm = R.length[Lg];
    const double rem = remaining(s), stop = stopDistance(s.phi);
    const ui::TextStyle lab = theme::small(theme::Scale{S}, white(0.62f));
    const ui::TextStyle val = theme::mono(theme::Scale{S}, 14.0f, white(0.92f));
    const float gridW = S * 120.0f;
    const float x0 = b.x, x1 = b.x + b.w - gridW - S * 24.0f;
    auto RX = [&](double frac) { return x0 + float(std::clamp(frac, 0.0, 1.0)) * (x1 - x0); };
    const double travelled = std::clamp(1.0 - rem / Lm, 0.0, 1.0);
    // the Earth's ruler: the whole leg, the rocket on it, the way left solid
    float y = b.y + S * 30.0f;
    ui::text(x0, y - S * 28.0f, L("Dünya'nın ölçtüğü", "As the Earth measures"), lab);
    const std::string d1 = distText(rem);
    ui::text(x1 - ui::measure(d1, val), y - S * 28.0f, d1, val);
    ui::line(x0, y, RX(travelled), y, S * 2.0f, white(0.25f));
    ui::line(RX(travelled), y, x1, y, S * 2.4f, white(0.9f));
    ui::line(x1, y - S * 8.0f, x1, y + S * 8.0f, S * 1.6f, white(0.9f));
    // the stopping distance: a gold bracket from the target back
    const double sf = std::clamp(stop / Lm, 0.0, 1.0);
    if (sf > 0.0) {
        ui::line(RX(1.0 - sf), y + S * 10.0f, x1, y + S * 10.0f, S * 2.0f, gold(0.9f));
        ui::line(RX(1.0 - sf), y + S * 6.0f, RX(1.0 - sf), y + S * 14.0f, S * 1.6f, gold(0.9f));
    }
    // the rocket: a small wedge
    {
        const float rx = RX(travelled);
        const float tri[8] = {rx - S * 6.0f, y - S * 12.0f, rx + S * 6.0f, y - S * 12.0f, rx, y - S * 3.0f, rx - S * 6.0f, y - S * 12.0f};
        ui::polyline(tri, 4, S * 2.0f, white(1.0f));
    }
    // your ruler: the same scale, the way left contracted by gamma (it melts)
    y += S * 70.0f;
    ui::text(x0, y - S * 28.0f, L("Senin ölçtüğün", "As you measure"), lab);
    const std::string d2 = distText(rem / k.gamma);
    ui::text(x1 - ui::measure(d2, val), y - S * 28.0f, d2, val);
    const float rx = RX(travelled);
    const float rEnd = std::min(x1, rx + float(rem / k.gamma / Lm) * (x1 - x0));
    ui::line(rx, y, std::max(rEnd, rx + S * 2.0f), y, S * 2.4f, white(0.95f));
    ui::line(rEnd, y - S * 8.0f, rEnd, y + S * 8.0f, S * 1.6f, white(0.9f));
    // the stopping distance in words
    y += S * 34.0f;
    const std::string st = L("Durma mesafesi ", "Stopping distance ") + distText(stop);
    ui::text(x0, y, st, theme::small(theme::Scale{S}, gold(0.85f)));
    // space squeezed along the motion: a square of grid, its columns 1/gamma apart (merging into a sheet)
    {
        const float gx = b.x + b.w - gridW, gy = b.y + S * 2.0f, gh = std::min(gridW, b.h - S * 8.0f);
        ui::stroke(ui::Rect{gx, gy, gridW, gh}, white(0.35f), S * 2.0f, S * 1.0f);
        const float rowStep = gh / 6.0f;
        for (int i = 1; i < 6; ++i) ui::line(gx, gy + rowStep * i, gx + gridW, gy + rowStep * i, S * 1.0f, white(0.22f));
        const double colStep = gridW / 6.0 / k.gamma;
        if (colStep >= 2.0 * S) {
            for (double cx = gx + gridW * 0.5; cx < gx + gridW; cx += colStep) ui::line(float(cx), gy, float(cx), gy + gh, S * 1.0f, white(0.30f));
            for (double cx = gx + gridW * 0.5 - colStep; cx > gx; cx -= colStep) ui::line(float(cx), gy, float(cx), gy + gh, S * 1.0f, white(0.30f));
        } else {
            ui::rect(ui::Rect{gx, gy, gridW, gh}, white(float(std::min(0.30, 0.30 * 2.0 * S / std::max(colStep, 1e-30) * 0.05 + 0.12))), S * 2.0f);
        }
        const std::string t = std::string(L("uzay ", "space ")) + "1/γ";
        ui::text(gx, gy + gh + S * 6.0f, t, theme::mono(theme::Scale{S}, 12.5f, white(0.55f)));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// SAATLER: the two dials, the Earth's "now" and the Earth seen

// a dial: twelve ticks, three hands at (seconds of the clock); each hand drawn along its sweep while the eye's shutter is
// open (a fan of thin lines that sums to the time average; a whole turn or more is a translucent disc)
void dial(float cx, float cy, float r, double secs, double sweepSecs, ui::Color col, float S) {
    ui::arc(cx, cy, r, 0.0f, float(2.0 * kPi), S * 1.4f, ui::withAlpha(col, 0.55f));
    for (int i = 0; i < 12; ++i) {
        const float a = float(2.0 * kPi * i / 12.0);
        const float r0 = r * (i % 3 == 0 ? 0.80f : 0.87f);
        ui::line(cx + r0 * std::sin(a), cy - r0 * std::cos(a), cx + r * 0.95f * std::sin(a), cy - r * 0.95f * std::cos(a), S * 1.2f,
                 ui::withAlpha(col, 0.5f));
    }
    struct Hand { double period, len, width, alpha; };
    const Hand hands[3] = {{43200.0, 0.50, 3.0, 0.95}, {3600.0, 0.72, 2.2, 0.95}, {60.0, 0.86, 1.3, 0.9}};
    for (const Hand& h : hands) {
        const double turn = std::fmod(secs / h.period, 1.0);
        const double sweep = sweepSecs / h.period;   // turns while the shutter is open
        if (sweep >= 1.0) {
            ui::circle(cx, cy, float(r * h.len), ui::withAlpha(col, float(0.10 * h.alpha)));
            continue;
        }
        const int n = sweep * 360.0 < 2.0 ? 1 : std::min(96, int(sweep * 360.0 / 1.5) + 1);
        for (int i = 0; i < n; ++i) {
            const double t = n == 1 ? turn : turn - sweep * (1.0 - double(i) / (n - 1));
            const float a = float(2.0 * kPi * t);
            ui::line(cx, cy, cx + float(r * h.len) * std::sin(a), cy - float(r * h.len) * std::cos(a), S * float(h.width),
                     ui::withAlpha(col, float(h.alpha / std::sqrt(double(n)))));
        }
    }
    ui::circle(cx, cy, S * 3.5f, col);
}

void drawClocks(const Pano& p, PanoState&, Box b, float S) {
    const Ship& s = *p.ship;
    const Kin& k = p.k;
    const float r = std::min(b.h * 0.30f, b.w * 0.19f);
    const float cy = b.y + r + S * 6.0f;
    const float c1 = b.x + b.w * 0.25f, c2 = b.x + b.w * 0.75f;
    const double shutter = std::max(p.dt, 1.0 / 60.0);
    dial(c1, cy, r, s.tau, shutter, white(1.0f), S);
    dial(c2, cy, r, s.t.value(), shutter * k.gamma, red(1.0f), S);
    const ui::TextStyle head = theme::caption(theme::Scale{S}, white(0.55f));
    const ui::TextStyle val = theme::mono(theme::Scale{S}, 15.0f, white(0.95f));
    const ui::TextStyle valR = theme::mono(theme::Scale{S}, 15.0f, red(1.0f));
    const float ty = cy + r + S * 10.0f;
    auto centred = [&](float x, float y, const std::string& t, const ui::TextStyle& st) { ui::text(x - 0.5f * ui::measure(t, st), y, t, st); };
    centred(c1, ty, L("SEN", "YOU"), head);
    centred(c2, ty, L("DÜNYA", "EARTH"), theme::caption(theme::Scale{S}, red(0.85f)));
    centred(c1, ty + S * 24.0f, clockText(s.tau), val);
    centred(c2, ty + S * 24.0f, spanText(s.t.value()), valR);
    // the Earth "now" for you, and the Earth you see
    const ui::TextStyle lab = theme::small(theme::Scale{S}, white(0.6f));
    float y = ty + S * 56.0f;
    auto when = [&](double t) { return t < 1e4 * kYear && t > -1e4 * kYear ? dateText(t) : spanText(std::fabs(t)); };
    const std::string a = L("Dünya'da şimdi (senin için) ", "Now on the Earth (for you) ");
    ui::text(b.x, y, a, lab);
    ui::text(b.x + b.w - ui::measure(when(p.earthNow), valR), y, when(p.earthNow), valR);
    y += S * 26.0f;
    const std::string c = L("Gördüğün Dünya ", "The Earth you see ");
    ui::text(b.x, y, c, lab);
    const std::string seen = when(p.earthSeen) + "  ×" + (p.earthSeenRate < 0.01 ? sciText(p.earthSeenRate, 1) : theme::num(p.earthSeenRate, 2));
    ui::text(b.x + b.w - ui::measure(seen, theme::mono(theme::Scale{S}, 15.0f)), y, seen, theme::mono(theme::Scale{S}, 15.0f, gold(0.95f)));
}

// ---------------------------------------------------------------------------------------------------------------------
// BEDEL: what the motion would take

void drawCost(const Pano& p, PanoState&, Box b, float S) {
    const Ship& s = *p.ship;
    const ui::TextStyle lab = theme::small(theme::Scale{S}, white(0.62f));
    float y = b.y;
    auto row = [&](const std::string& l, const std::string& v, ui::Color c) {
        ui::text(b.x, y, l, lab);
        const ui::TextStyle st = theme::mono(theme::Scale{S}, 14.5f, c);
        ui::text(b.x + b.w - ui::measure(v, st), y + S * 1.0f, v, st);
        y += S * 27.0f;
    };
    const Cost c = costOf(s.phi);
    auto big = [](double v) { return v < 1e4 ? theme::num(v, 2) : sciText(v, 2); };
    row(L("Hareket enerjisi (1 ton)", "Energy of motion (1 t)"), L("Güneş'in ", "the Sun's ") + sunTime(c.kinetic), white(0.95f));
    row(L("Foton roketinin yaktığı", "The photon rocket burns"), L("Güneş'in ", "the Sun's ") + sunTime(c.photon), white(0.95f));
    row(L("Durmak için de yakarsa", "To stop again as well"), L("Güneş'in ", "the Sun's ") + sunTime(c.photonStop), white(0.95f));
    row(L("Kütle oranı (durmakla)", "Mass ratio (with stopping)"), big(c.massRatio) + "  (" + big(c.massRatioStop) + ")", white(0.95f));
    const bool driving = s.drive == Drive::Thrust || s.drive == Drive::Brake;
    row(L("İvme", "Acceleration"), driving ? theme::num(c.gees / 1e6, 1) + L(" milyon g", " million g") : "0 g", white(0.95f));
    const double sun = c.plate / kSolarConstant;
    row(L("Önden gelen ışıma", "Light from ahead"), L("Güneş ışığının ", "sunlight × ") + (sun < 1e4 ? sig3(sun) : sciText(sun, 2)) + L(" katı", ""), gold(0.95f));
    // the CMB ahead: its temperature and the colour of a blackbody that hot (and its band)
    {
        const double Tf = c.cmbAhead;
        const std::string v = (Tf < 100.0 ? sig3(Tf) : Tf < 1e5 ? theme::thousands((unsigned long long)std::llround(Tf)) : sciText(Tf, 2)) + " K · " + bandOf(Tf);
        ui::text(b.x, y, L("Önde fon ışıması", "The CMB ahead"), lab);
        const ui::TextStyle st = theme::mono(theme::Scale{S}, 14.5f, white(0.95f));
        const float vw = ui::measure(v, st);
        ui::text(b.x + b.w - vw, y + S * 1.0f, v, st);
        if (p.bb) {
            const float3 col = p.bb->colour(Tf);
            const float m = std::max({col.x, col.y, col.z, 1e-6f});
            auto toS = [](float x) { return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f; };
            const ui::Color sw{toS(col.x / m), toS(col.y / m), toS(col.z / m), Tf > 600.0 ? 1.0f : float(std::clamp((Tf - 300.0) / 300.0, 0.0, 1.0))};
            ui::circle(b.x + b.w - vw - S * 16.0f, y + S * 10.0f, S * 7.0f, sw);
        }
        y += S * 27.0f;
    }
    row(L("Her hidrojen atomu", "Each hydrogen atom"), (c.hydrogen < 1e4 ? sig3(c.hydrogen) : sciText(c.hydrogen, 2)) + " eV", white(0.95f));
    y += S * 6.0f;
    ui::textWrapped(b.x, y, b.w,
                    L("Bir düşünce deneyi: hareketin fiziği gerçek; roketin mühendisliği (yakıt, ivme, kalkan) idealize.",
                      "A thought experiment: the motion's physics is real; the rocket's engineering (fuel, acceleration, shield) idealised."),
                    theme::small(theme::Scale{S}, white(0.42f)), 1.3f);
}

}   // namespace

// ---------------------------------------------------------------------------------------------------------------------
// the words and numbers

std::string sciText(double v, int digits) {
    if (v == 0.0 || !std::isfinite(v)) return "0";
    const int e = int(std::floor(std::log10(std::fabs(v))));
    double m = v / std::pow(10.0, e);
    int ee = e;
    if (std::fabs(m) >= 10.0 - 0.5 * std::pow(10.0, -digits)) {
        m /= 10.0;
        ++ee;
    }
    return theme::num(m, digits) + " × 10" + supInt(ee);
}
std::string distText(double m) {
    const double ly = m / kLy;
    if (m < 1e3) return sig3(m) + " m";
    if (m < 1e9) return sig3(m * 1e-3) + " km";
    if (m < 1e12) return sig3(m * 1e-9) + L(" milyon km", " million km");
    if (ly < 0.05) return sig3(m / kAU) + " AU";
    if (ly < 1e3) return sig3(ly) + L(" ışık yılı", " light years");
    if (ly < 1e6) return sig3(ly * 1e-3) + L(" bin ışık yılı", " thousand light years");
    return theme::num(ly * 1e-6, 3) + L(" milyon ışık yılı", " million light years");
}
std::string clockText(double s) {
    const long long t = (long long)std::floor(std::max(s, 0.0));
    char b[64];
    if (t < 3600) std::snprintf(b, sizeof(b), "%lld:%02lld", t / 60, t % 60);
    else std::snprintf(b, sizeof(b), "%lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60);
    return b;
}
std::string spanText(double s) {
    const double y = s / kYear;
    if (s < 3600.0) return clockText(s);
    if (s < 86400.0 * 2.0) return theme::num(s / 3600.0, 1) + L(" saat", " hours");
    if (y < 1.0) return theme::num(s / 86400.0, 1) + L(" gün", " days");
    if (y < 1e4) return theme::num(y, 2) + L(" yıl", " years");
    if (y < 1e6) return theme::num(y * 1e-3, 2) + L(" bin yıl", " thousand years");
    return theme::num(y * 1e-6, 3) + L(" milyon yıl", " million years");
}
std::string dateText(double t) {
    const double jd = 2461318.9169918294 + t / 86400.0 + 0.5;   // "today" (TDB ~ UTC: a minute is nothing here)
    const long long Z = (long long)std::floor(jd);
    const long long A0 = (long long)std::floor((Z - 1867216.25) / 36524.25);
    const long long A = Z + 1 + A0 - A0 / 4, B = A + 1524;
    const long long C = (long long)std::floor((B - 122.1) / 365.25), D = (long long)std::floor(365.25 * C);
    const long long E = (long long)std::floor((B - D) / 30.6001);
    const int day = int(B - D - (long long)std::floor(30.6001 * E));
    const int month = int(E < 14 ? E - 1 : E - 13);
    const long long year = month > 2 ? C - 4716 : C - 4715;
    static const char* trM[12] = {"Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran", "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık"};
    static const char* enM[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    const std::string y = year >= 10000 ? theme::thousands((unsigned long long)year) : std::to_string(year);
    return std::to_string(day) + " " + L(trM[month - 1], enM[month - 1]) + " " + y;
}
std::string betaText(const Kin& k) {
    if (k.phi <= 0.0) return "0";
    if (k.nines < 3.0) return theme::num(k.beta, 6);
    const int n = int(std::floor(k.nines + 1e-9));
    const double r = 1.0 - std::pow(10.0, double(n)) * k.eps;   // the digits after the n nines, as 0.xyz
    std::string digits(size_t(n), '9');
    char b[16];
    std::snprintf(b, sizeof(b), "%03d", std::clamp(int(std::floor(r * 1000.0)), 0, 999));
    digits += b;
    std::string out = app::lang() == app::Lang::TR ? "0," : "0.";
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && i % 3 == 0) out += ' ';
        out += digits[i];
    }
    return out;
}
std::string speedText(const Kin& k) {
    const double v = kC * k.beta;
    if (k.eps > 1e-6) return theme::thousands((unsigned long long)std::llround(v)) + " m/s";
    return "c − " + sciText(kC * k.eps, 2) + " m/s";   // the shortfall from c: the only digits that still move
}
std::string gammaText(double g) { return g < 1e4 ? theme::num(g, g < 10.0 ? 4 : 1) : sciText(g, 3); }

// the plate: the forward hemisphere of D^4 (sigma T0^4 / pi) cos theta' with D = 1 / (gamma (1 - beta cos theta')); with
// w = 1 - beta cos theta' it is (2 sigma T0^4 / (gamma^4 beta^2)) (1/6 + 1/(3 eps^3) - 1/(2 eps^2)), eps = 1 - beta. Near
// rest its terms cancel: there the series sigma T0^4 (1 + 8 beta / 3)
double cmbOnPlate(const Kin& k) {
    const double s4 = kStefan * std::pow(kCmbToday, 4.0);
    if (k.beta < 1e-3) return s4 * (1.0 + 8.0 * k.beta / 3.0);
    const double e = k.eps;
    const double br = 1.0 / 6.0 + 1.0 / (3.0 * e * e * e) - 1.0 / (2.0 * e * e);   // (1e62 at 21 nines: fine in a double)
    return 2.0 * s4 / (std::pow(k.gamma, 4.0) * k.beta * k.beta) * br;
}

Cost costOf(double phi) {
    const Kin k = kin(phi);
    const double mc2 = kRocketKg * kC * kC;
    const double sh = std::sinh(0.5 * phi), gm1 = 2.0 * sh * sh;   // gamma - 1 without the cancellation near rest
    Cost c;
    c.kinetic = gm1 * mc2;
    c.photon = std::expm1(phi) * mc2;
    c.photonStop = std::expm1(2.0 * phi) * mc2;
    c.massRatio = std::exp(phi);
    c.massRatioStop = std::exp(2.0 * phi);
    c.gees = kAlpha * kC / kG;
    c.plate = cmbOnPlate(k);
    c.cmbAhead = kCmbToday * std::exp(phi);
    c.hydrogen = gm1 * kMpC2eV;
    return c;
}

// ---------------------------------------------------------------------------------------------------------------------

void Trace::clear() {
    x.clear();
    ct.clear();
    minStep = 0.0;
}
void Trace::add(double xm, double ctm) {
    if (!x.empty()) {
        const double dx = xm - x.back(), dc = ctm - ct.back();
        if (std::sqrt(dx * dx + dc * dc) < minStep) return;
    }
    x.push_back(xm);
    ct.push_back(ctm);
    if (int(x.size()) >= kTraceMax) {   // every other point dropped, the first and the last kept
        std::vector<double> nx, nc;
        for (size_t i = 0; i < x.size(); i += 2) {
            nx.push_back(x[i]);
            nc.push_back(ct[i]);
        }
        if ((x.size() - 1) % 2 != 0) {
            nx.push_back(x.back());
            nc.push_back(ct.back());
        }
        x.swap(nx);
        ct.swap(nc);
        // the spacing grows with the world line's length
        const double dx = x.back() - x.front(), dc = ct.back() - ct.front();
        minStep = std::sqrt(dx * dx + dc * dc) / double(kTraceMax);
    }
}

// ---------------------------------------------------------------------------------------------------------------------

void drawPanels(const Pano& p, PanoState& st, const ui::Rect& view, float Sapp, unsigned show, const std::vector<ui::Rect>& avoid) {
    if (!p.ship || !p.route) return;
    // laid out for 1440 lines and scaled with the picture (the application's own panels keep a readable minimum; these
    // keep their places, smaller in a small window)
    const float S = std::min(Sapp, view.h / 1440.0f);
    const float m = S * 40.0f, w = S * 520.0f, gap = S * 16.0f;
    const float vx1 = view.x + view.w, vy1 = view.y + view.h, midY = view.y + 0.5f * view.h;
    // one panel at (x, y), its sizes times Sk (h: its height for 1440)
    auto one = [&](unsigned bit, float x, float y, float Sk, float h) {
        const float wk = Sk * 520.0f, hk = Sk * h;
        switch (bit) {
        case 1:
            drawSpeed(p, st, panel(x, y, wk, hk, Sk, L("HIZ", "SPEED"), theme::num(std::max(p.k.nines, 0.0), 2) + L(" dokuz", " nines")), Sk);
            break;
        case 2: drawMinkowski(p, st, panel(x, y, wk, hk, Sk, L("UZAY-ZAMAN", "SPACETIME"), "t′ · x′"), Sk); break;
        case 4: {
            const std::string to = p.ship->leg == kLegProxima ? "→ Proxima" : p.ship->leg == kLegAndromeda ? "→ Andromeda" : "";
            drawRoad(p, st, panel(x, y, wk, hk, Sk, L("YOL", "THE WAY"), to), Sk);
            break;
        }
        case 8: drawClocks(p, st, panel(x, y, wk, hk, Sk, L("SAATLER", "CLOCKS"), "γ " + gammaText(p.k.gamma)), Sk); break;
        case 16: drawCost(p, st, panel(x, y, wk, hk, Sk, L("BEDEL", "THE COST"), ""), Sk); break;
        }
    };
    struct Item {
        unsigned bit;
        float h;
    };
    // a column at an edge: its panels stacked from the top of the room the application's panels leave it (those in the
    // upper half push its top down, those in the lower half its bottom up), all shrunk together if they do not fit;
    // returns its inner edge
    auto column = [&](std::initializer_list<Item> items, bool right, float y0, float y1) {
        float need = 0.0f;
        int n = 0;
        for (const Item& it : items)
            if (show & it.bit) {
                need += S * it.h;
                ++n;
            }
        if (n == 0) return right ? vx1 : view.x;
        need += gap * float(n - 1);
        const float x0 = right ? vx1 - m - w : view.x + m;
        for (const ui::Rect& r : avoid) {
            if (r.w <= 0.0f || r.h <= 0.0f || r.x >= x0 + w || r.x + r.w <= x0) continue;
            if (r.y + 0.5f * r.h < midY) y0 = std::max(y0, r.y + r.h + gap);
            else y1 = std::min(y1, r.y - gap);
        }
        const float k = std::clamp((y1 - y0) / need, 0.35f, 1.0f);
        const float x = right ? vx1 - m - w * k : x0;
        float y = y0;
        for (const Item& it : items) {
            if (!(show & it.bit)) continue;
            one(it.bit, x, y, S * k, it.h);
            y += S * k * it.h + gap * k;
        }
        return right ? x : x + w * k;
    };
    column({{1, 300.0f}, {4, 240.0f}}, false, view.y + S * 110.0f, vy1 - m);
    const float rightEdge = column({{2, 560.0f}, {8, 330.0f}}, true, view.y + m, vy1 - m);
    // the cost at the bottom, next to the application's panel there (the scene's, on the left), away from the middle
    // where the notices rise; clear of the keys and of the right column
    if (show & 16) {
        const float h = S * 330.0f, y0 = vy1 - m - h;
        float x0 = view.x + m, x1 = rightEdge - gap;
        for (const ui::Rect& r : avoid) {
            if (r.w <= 0.0f || r.h <= 0.0f || r.y >= vy1 - m || r.y + r.h <= y0) continue;
            if (r.x + 0.5f * r.w < view.x + 0.5f * view.w) x0 = std::max(x0, r.x + r.w + gap);
            else x1 = std::min(x1, r.x - gap);
        }
        const float k = std::clamp((x1 - x0) / w, 0.35f, 1.0f);
        one(16, x0, vy1 - m - h * k, S * k, 330.0f);
    }
}

}   // namespace olcek::hiz
