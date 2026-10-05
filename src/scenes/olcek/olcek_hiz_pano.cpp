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
// HIZ: the speed bar against the wall of light, gamma's curve (the nines: in the title)

void drawSpeed(const Pano& p, PanoState& st, Box b, float S) {
    const Kin& k = p.k;
    const ui::TextStyle lab = theme::mono(theme::Scale{S}, 12.5f, white(0.55f));
    const ui::TextStyle small = theme::small(theme::Scale{S}, white(0.62f));
    const float axisH = b.h - S * 30.0f;   // the room under the plots for their names
    // the speed bar: 0 .. c, the wall of light at c
    {
        const float x = b.x + S * 20.0f, y0 = b.y + axisH, y1 = b.y + S * 14.0f;
        ui::rect(ui::Rect{x - S * 5.0f, y1, S * 10.0f, y0 - y1}, white(0.07f), S * 3.0f);
        const float yb = y0 - (y0 - y1) * float(k.beta);
        ui::rect(ui::Rect{x - S * 5.0f, yb, S * 10.0f, y0 - yb}, white(0.85f), S * 3.0f);
        ui::line(x - S * 16.0f, y1, x + S * 16.0f, y1, S * 2.0f, gold(0.95f));
        ui::text(x + S * 20.0f, y1 - S * 10.0f, "c", theme::mono(theme::Scale{S}, 15.0f, gold(0.95f)));
        ui::text(x - S * 10.0f, b.y + axisH + S * 8.0f, "v", theme::caption(theme::Scale{S}, white(0.5f)));
    }
    // gamma(beta): beta across (0 .. 1), log10 gamma up, the axis grows with it
    {
        const float px = b.x + S * 84.0f, pw = b.w - S * 92.0f, py0 = b.y + axisH, py1 = b.y + S * 4.0f;
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
// UZAY-ZAMAN: the rocket's own space and time drawn in the Earth's (Özcan's reference, assets/olcek/referans/
// uzay-kisalmasi.png). The Earth's axes, and a patch of the rocket's grid whose sides run along its x' and t': they lean
// towards the light line as the speed grows (each atan beta from its axis) and the patch flattens into a line. Only the
// folding: the far time and space fold up and shorten for you; the other panels carry the numbers

void drawSpacetime(const Pano& p, Box b, float S) {
    const double beta = std::clamp(p.k.beta, 0.0, 1.0);
    const float side = std::min(b.w, b.h);
    const float len = side - S * 44.0f;   // the axes' length
    const float ox = b.x + 0.5f * (b.w - len) - S * 6.0f, oy = b.y + 0.5f * (b.h + len) - S * 4.0f;   // the origin
    // the Earth's axes: space across, time up
    const ui::Color ax = white(0.7f);
    const float h = S * 7.0f;
    ui::line(ox, oy, ox + len, oy, S * 1.3f, ax);
    ui::line(ox, oy, ox, oy - len, S * 1.3f, ax);
    const float tipX[6] = {ox + len - h, oy - 0.6f * h, ox + len, oy, ox + len - h, oy + 0.6f * h};
    const float tipT[6] = {ox - 0.6f * h, oy - len + h, ox, oy - len, ox + 0.6f * h, oy - len + h};
    ui::polyline(tipX, 3, S * 1.3f, ax);
    ui::polyline(tipT, 3, S * 1.3f, ax);
    const ui::TextStyle name = theme::mono(theme::Scale{S}, 15.0f, white(0.8f));
    const ui::TextStyle cap = theme::caption(theme::Scale{S}, white(0.45f));
    ui::text(ox + len + S * 6.0f, oy - S * 9.0f, "x", name);
    ui::text(ox - S * 4.0f, oy - len - S * 26.0f, "t", name);
    ui::text(ox + S * 14.0f, oy - len - S * 22.0f, L("ZAMAN", "TIME"), cap);
    const std::string space = L("UZAY", "SPACE");
    ui::text(ox + len - ui::measure(space, cap), oy + S * 8.0f, space, cap);
    // the rocket's grid: 5 x 5 cells, its x' side along (1, beta) and its t' side along (beta, 1), each side ell long
    // (a sketch's cells, not the calibrated ones: those would stretch by gamma off the panel)
    const double n = 1.0 / std::sqrt(1.0 + beta * beta);
    const float ell = 0.64f * len;
    const float ux = float(ell * n), uy = float(ell * beta * n);   // x'
    const float tx = float(ell * beta * n), ty = float(ell * n);   // t'
    for (int i = 0; i <= 5; ++i) {
        const float f = float(i) / 5.0f;
        const float w = S * (i == 0 ? 1.8f : 1.1f);   // its own axes heavier
        const ui::Color c = white(i == 0 ? 0.95f : 0.7f);
        ui::line(ox + f * ux, oy - f * uy, ox + f * ux + tx, oy - f * uy - ty, w, c);   // x' constant: along t'
        ui::line(ox + f * tx, oy - f * ty, ox + f * tx + ux, oy - f * ty - uy, w, c);   // t' constant: along x'
    }
    // its axes' names at their ends: t' up and to the left, x' down and to the right (apart when they fold together)
    ui::text(ox + tx - S * 26.0f, oy - ty - S * 22.0f, "t′", name);
    ui::text(ox + ux + S * 8.0f, oy - uy + S * 2.0f, "x′", name);
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
    row(L("İvme", "Acceleration"), !driving ? "0 g" : c.gees < 1e6 ? sig3(c.gees) + " g" : theme::num(c.gees / 1e6, 1) + L(" milyon g", " million g"),
        white(0.95f));
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
    if (k.beta < 1e-3) return sciText(k.beta, 2);   // (a slow ship: 1,73 × 10⁻⁷, not 0,000000)
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
    c.gees = driveRate(phi) * kC / kG0;
    c.plate = cmbOnPlate(k);
    c.cmbAhead = kCmbToday * std::exp(phi);
    c.hydrogen = gm1 * kMpC2eV;
    return c;
}

// ---------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------

void drawPanels(const Pano& p, PanoState& st, const ui::Rect& view, float Sapp, unsigned show, const std::vector<ui::Rect>& avoid) {
    if (!p.ship || !p.route) return;
    // laid out for 1440 lines and scaled with the picture (the application's own panels keep a readable minimum; these
    // keep their places, smaller in a small window)
    const float S = std::min(Sapp, view.h / 1440.0f);
    const float m = S * 40.0f, w = S * 520.0f, gap = S * 16.0f;
    const float vx1 = view.x + view.w, vy1 = view.y + view.h, midY = view.y + 0.5f * view.h;
    // one panel at (x, y), its sizes times Sk (h: its height for 1440)
    auto one = [&](unsigned bit, float x, float y, float Sk, float h, float w) {
        const float wk = Sk * w, hk = Sk * h;
        switch (bit) {
        case 1:
            drawSpeed(p, st, panel(x, y, wk, hk, Sk, L("HIZ", "SPEED"), theme::num(std::max(p.k.nines, 0.0), 2) + L(" dokuz", " nines")), Sk);
            break;
        case 2: drawSpacetime(p, panel(x, y, wk, hk, Sk, L("UZAY-ZAMAN", "SPACETIME")), Sk); break;
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
        float h, w;   // for 1440 lines
    };
    // a column at an edge: its panels stacked from the top of the room the application's panels leave it (those in the
    // upper half push its top down, those in the lower half its bottom up), all shrunk together if they do not fit;
    // returns its inner edge (the right column's panels sit flush right)
    auto column = [&](std::initializer_list<Item> items, bool right, float y0, float y1) {
        float need = 0.0f, wMax = 0.0f;
        int n = 0;
        for (const Item& it : items)
            if (show & it.bit) {
                need += S * it.h;
                wMax = std::max(wMax, S * it.w);
                ++n;
            }
        if (n == 0) return right ? vx1 : view.x;
        need += gap * float(n - 1);
        const float x0 = right ? vx1 - m - wMax : view.x + m;
        for (const ui::Rect& r : avoid) {
            if (r.w <= 0.0f || r.h <= 0.0f || r.x >= x0 + wMax || r.x + r.w <= x0) continue;
            if (r.y + 0.5f * r.h < midY) y0 = std::max(y0, r.y + r.h + gap);
            else y1 = std::min(y1, r.y - gap);
        }
        const float k = std::clamp((y1 - y0) / need, 0.35f, 1.0f);
        float y = y0, edge = right ? vx1 : view.x;
        for (const Item& it : items) {
            if (!(show & it.bit)) continue;
            const float wk = S * k * it.w;
            const float x = right ? vx1 - m - wk : x0;
            one(it.bit, x, y, S * k, it.h, it.w);
            edge = right ? std::min(edge, x) : std::max(edge, x + wk);
            y += S * k * it.h + gap * k;
        }
        return edge;
    };
    column({{1, 300.0f, 520.0f}, {4, 240.0f, 520.0f}}, false, view.y + S * 110.0f, vy1 - m);
    const float rightEdge = column({{2, 400.0f, 400.0f}, {8, 330.0f, 520.0f}}, true, view.y + m, vy1 - m);
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
        one(16, x0, vy1 - m - h * k, S * k, 330.0f, 520.0f);
    }
}

void drawEyeLabels(const std::vector<EyeLabel>& labels, const ui::Rect& view, float Sapp, const std::vector<ui::Rect>& avoid) {
    const float S = std::min(Sapp, view.h / 1440.0f);
    const ui::TextStyle nameT = theme::small(theme::Scale{S}, white(0.88f));
    const ui::TextStyle distT = theme::mono(theme::Scale{S}, 12.5f, white(0.62f));
    std::vector<ui::Rect> taken = avoid;
    for (const EyeLabel& l : labels) {
        const float m = S * 24.0f;
        if (l.x < view.x + m || l.x > view.x + view.w - m || l.y < view.y + m || l.y > view.y + view.h - m) continue;
        const std::string d = distText(l.dist);
        // up and to the right of its disc, a short line to it
        const float off = std::max(l.r, S * 3.0f) * 0.7071f + S * 8.0f;
        const float ax = l.x + off, ay = l.y - off, tx = ax + S * 14.0f, ty = ay - S * 14.0f;
        const float w = std::max(ui::measure(l.name, nameT), ui::measure(d, distT)) + S * 8.0f, h = S * 40.0f;
        const ui::Rect box{tx - S * 4.0f, ty - h + S * 2.0f, w, h};
        bool clash = false;
        for (const ui::Rect& r : taken)
            if (r.w > 0.0f && box.x < r.x + r.w && box.x + box.w > r.x && box.y < r.y + r.h && box.y + box.h > r.y) clash = true;
        if (clash) continue;
        taken.push_back(box);
        ui::line(l.x + (std::max(l.r, S * 3.0f) + S * 3.0f) * 0.7071f, l.y - (std::max(l.r, S * 3.0f) + S * 3.0f) * 0.7071f, tx - S * 2.0f,
                 ty + S * 2.0f, std::max(1.0f, S), white(0.45f));
        ui::text(tx, ty - S * 36.0f, l.name, nameT);
        ui::text(tx, ty - S * 17.0f, d, distT);
    }
}

void drawEnergyKey(const ui::Rect& view, float Sapp, double phi, const Blackbody& bb, const std::vector<ui::Rect>& avoid) {
    const float S = std::min(Sapp, view.h / 1440.0f);
    // the bands by their peak's temperature (Wien: lambda = 2.898e-3 / T): microwaves (from 1 m), infrared (from 1 mm),
    // visible (750 to 380 nm), ultraviolet (to 10 nm), X-rays (to 10 pm), gamma rays; each a sixth of the bar
    const double edge[7] = {-2.5383, 0.4621, 3.5862, 3.8818, 5.4621, 8.4621, 13.0};
    const char* tr[6] = {"mikrodalga", "kızılötesi", "görünür", "morötesi", "X ışını", "gama"};
    const char* en[6] = {"microwaves", "infrared", "visible", "ultraviolet", "X-rays", "gamma rays"};
    auto at = [&](double lt) {   // where log10 T sits along the bar (0 .. 1)
        lt = std::clamp(lt, edge[0], edge[6]);
        int i = 0;
        while (i < 5 && lt > edge[i + 1]) ++i;
        return (i + (lt - edge[i]) / (edge[i + 1] - edge[i])) / 6.0;
    };
    auto colourAt = [&](double lt) {   // the false colour there, its brightest channel 1, for the screen (sRGB)
        float3 c = energyColour(float(lt), bb.colour(std::pow(10.0, std::clamp(lt, -1.0, 12.0))));
        const float mx = std::max({c.x, c.y, c.z, 1e-6f});
        auto enc = [](float v) { return std::pow(std::clamp(v, 0.0f, 1.0f), 1.0f / 2.2f); };
        return ui::Color{enc(c.x / mx), enc(c.y / mx), enc(c.z / mx), 1.0f};
    };
    const float w = S * 640.0f, h = S * 128.0f, barW = S * 580.0f, barH = S * 7.0f;
    float x = view.x + 0.5f * (view.w - w), y = view.y + S * 28.0f;
    for (const ui::Rect& r : avoid)   // below an application panel it would cover
        if (r.w > 0.0f && r.h > 0.0f && r.x < x + w && r.x + r.w > x && r.y < y + h && r.y + r.h > y) y = r.y + r.h + S * 16.0f;
    const ui::Rect box{x, y, w, h};
    ui::shadow(box, S * 18.0f, S * 24.0f, ui::hex(0x000000, 0.30f));
    ui::panel(box, theme::glass(), theme::glassBorder(), S * 16.0f, std::max(1.0f, S));
    const ui::TextStyle cap = theme::caption(theme::Scale{S}, theme::faint());
    const ui::TextStyle sub = theme::small(theme::Scale{S}, white(0.62f));
    const ui::TextStyle band = theme::mono(theme::Scale{S}, 12.0f, white(0.62f));
    const ui::TextStyle mark = theme::mono(theme::Scale{S}, 12.5f, white(0.92f));
    const std::string title = L("ENERJİ GÖZÜ", "ENERGY EYE");
    const std::string what = L("bütün bantlar, sahte renkle · fonun duruştaki parıltısı çıkarılmış",
                               "every band, in false colour · the background's glow at rest taken out");
    const float tw = ui::measure(title, cap), gw = S * 14.0f, sw = ui::measure(what, sub);
    const float tx = x + 0.5f * (w - tw - gw - sw);
    ui::text(tx, y + S * 16.0f, title, cap);
    ui::text(tx + tw + gw, y + S * 12.0f, what, sub);
    // the bar
    const float bx = x + 0.5f * (w - barW), by = y + S * 78.0f;
    const int n = std::max(24, int(barW / std::max(S * 2.0f, 1.0f)));
    for (int i = 0; i < n; ++i) {
        const double u = (i + 0.5) / n;
        const int b = std::min(int(u * 6.0), 5);
        const double lt = edge[b] + (u * 6.0 - b) * (edge[b + 1] - edge[b]);
        ui::rect(ui::Rect{bx + barW * float(i) / n, by, barW / n + 0.5f, barH}, colourAt(lt));
    }
    for (int b = 1; b < 6; ++b)   // the bands' borders
        ui::line(bx + barW * b / 6.0f, by - S * 2.0f, bx + barW * b / 6.0f, by + barH + S * 2.0f, std::max(1.0f, S), ui::hex(0x000000, 0.55f));
    for (int b = 0; b < 6; ++b) {
        const std::string t = L(tr[b], en[b]);
        ui::text(bx + barW * (b + 0.5f) / 6.0f - 0.5f * ui::measure(t, band), by + barH + S * 8.0f, t, band);
    }
    // the light dead ahead: the cosmic background (T0 e^phi) and a Sun-like star (5,772 K e^phi), their peaks' bands
    const double lD = phi / std::log(10.0);
    const double ltC = std::log10(kCmbToday) + lD, ltS = std::log10(5772.0) + lD;
    struct Mark {
        double lt;
        std::string name;
        float x, tw;
    };
    Mark ms[2] = {{ltC, L("fon ışıması", "the background glow"), 0, 0}, {ltS, L("yıldız ışığı", "starlight"), 0, 0}};
    for (Mark& m : ms) {
        m.x = bx + barW * float(at(m.lt));
        m.tw = ui::measure(m.name + (m.lt > edge[6] ? " →" : ""), mark);
    }
    // their names above, kept apart and inside the box
    float l0 = ms[0].x - 0.5f * ms[0].tw, l1 = ms[1].x - 0.5f * ms[1].tw;
    const float lo = x + S * 14.0f, hi = x + w - S * 14.0f;
    l0 = std::clamp(l0, lo, hi - ms[0].tw);
    l1 = std::clamp(l1, lo, hi - ms[1].tw);
    if (l1 < l0 + ms[0].tw + S * 12.0f) {
        const float over = l0 + ms[0].tw + S * 12.0f - l1;
        l0 -= 0.5f * over;
        l1 += 0.5f * over;
        if (l0 < lo) { l1 += lo - l0; l0 = lo; }
        if (l1 + ms[1].tw > hi) { l0 -= l1 + ms[1].tw - hi; l1 = hi - ms[1].tw; }
    }
    const float lx[2] = {l0, l1};
    for (int i = 0; i < 2; ++i) {
        const Mark& m = ms[i];
        const ui::Color c = colourAt(m.lt);
        const float tipY = by - S * 3.0f, baseY = by - S * 12.0f, hw = S * 5.5f;
        const float tri[6] = {m.x, tipY, m.x - hw, baseY, m.x + hw, baseY};
        ui::fill(tri, 3, c);
        ui::text(lx[i], y + S * 40.0f, m.name + (m.lt > edge[6] ? " →" : ""), mark);
    }
}

}   // namespace olcek::hiz
