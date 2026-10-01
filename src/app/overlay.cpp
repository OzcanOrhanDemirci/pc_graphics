// Drawing of the application's screens: the gallery, loading, the interface panel over a running scene, help,
// notices. Everything is laid out for 2560x1440 and scaled (S_). The top left corner stays empty: it belongs to
// the frame-rate overlay (RivaTuner) on Özcan's screen.
#include "app/app.h"

#include <algorithm>
#include <cmath>

namespace app {

namespace {

float smooth(float t) {
    t = std::min(std::max(t, 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
float easeOut(float t) {
    t = std::min(std::max(t, 0.0f), 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}
ui::Color accentOf(const SceneInfo& s, float a = 1.0f) { return ui::hex(s.accent, a); }
// the accent lifted towards white: text on dark glass
ui::Color accentText(const SceneInfo& s) { return ui::mix(ui::hex(s.accent), ui::hex(0xFFFFFF), 0.25f); }

std::vector<std::string> splitCaps(const std::string& keys) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : keys) {
        if (c == '|') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

}   // namespace

// ---------------------------------------------------------------------------------------------------
// small pieces
float App::keyCaps(float x, float y, const std::string& keys, float height) {
    const ui::TextStyle t = theme::mono(S_, 14.5f);
    float cx = x;
    for (const std::string& k : splitCaps(keys)) {
        const float w = std::max(height, ui::measure(k, t) + S_(18));
        const ui::Rect r{cx, y, w, height};
        ui::rect(ui::Rect{r.x, r.y + S_(2), r.w, r.h}, ui::hex(0x000000, 0.35f), S_(7));
        ui::panel(r, theme::capFill(), theme::capBorder(), S_(7), std::max(1.0f, S_(1)));
        ui::text(cx + 0.5f * (w - ui::measure(k, t)), y + 0.5f * (height - ui::lineHeight(t)), k, t);
        cx += w + S_(6);
    }
    return cx - S_(6) - x;
}

float App::keyHint(float x, float y, const std::string& keys, const std::string& what) {
    const float h = S_(30);
    float w = keyCaps(x, y, keys, h);
    const ui::TextStyle t = theme::small(S_);
    w += S_(10);
    w += ui::text(x + w, y + 0.5f * (h - ui::lineHeight(t)), what, t);
    return w;
}

// ---------------------------------------------------------------------------------------------------
void App::drawGallery(double now) {
    const float W = float(fbw_), H = float(fbh_);
    ui::begin(fbw_, fbh_);
    ui::clear(ui::Color{0.04f, 0.045f, 0.05f, 1.0f});
    const SceneInfo& cur = infos_[selected_];
    const float swap = smooth(float((now - selChangedAt_) / 0.45));
    const float appear = smooth(float((now - modeT0_) / 0.7));

    // backdrop: the selected picture, soft and dark, drifting slowly; cross-fades from the previous one
    const ui::Rect screen{0, 0, W, H};
    const float drift = 1.06f + 0.02f * float(std::sin(now * 0.07));
    if (swap < 1.0f) ui::image(pictures_[prevSelected_].soft, screen, 0, 1.0f, drift, 0.5f, 0.5f, 0.42f, 0.85f);
    ui::image(pictures_[selected_].soft, screen, 0, swap, drift, 0.5f, 0.5f, 0.42f, 0.85f);
    ui::rectHGradient(ui::Rect{0, 0, W * 0.5f, H}, ui::hex(0x08090B, 0.94f), ui::hex(0x08090B, 0.62f));
    ui::rectHGradient(ui::Rect{W * 0.5f, 0, W * 0.5f, H}, ui::hex(0x08090B, 0.62f), ui::hex(0x08090B, 0.40f));
    ui::rectGradient(ui::Rect{0, H * 0.72f, W, H * 0.28f}, ui::hex(0x08090B, 0.0f), ui::hex(0x08090B, 0.85f));
    ui::rectGradient(ui::Rect{0, 0, W, H * 0.16f}, ui::hex(0x08090B, 0.55f), ui::hex(0x08090B, 0.0f));

    // left: the title of the whole thing, then the scenes
    const float x0 = S_(150);
    const float lift = (1.0f - easeOut(appear)) * S_(24);
    float y = S_(196) + lift;
    ui::text(x0, y, "PC_GRAPHICS  ·  SAHNELER", theme::caption(S_, theme::faint()));
    y += S_(46);
    ui::text(x0, y, "Işık, atmosfer, fizik.", theme::display(S_, 66.0f, ui::withAlpha(theme::text(), appear)));
    y += S_(96);
    ui::textWrapped(x0, y, S_(700), "Ekran kartında, oyun motoru kullanmadan, baştan sona kendi kodumuzla kurulmuş üç sahne. Her biri tek bir soruya odaklanır.",
                    theme::body(S_, ui::withAlpha(theme::muted(), appear)), 1.5f);

    const float listY = S_(566) + lift, itemH = S_(164), itemW = S_(770);
    entryRects_.clear();
    // the highlight glides to the selected entry
    {
        const ui::Rect hl{x0 - S_(26), listY + selAnim_ * itemH, itemW, itemH - S_(14)};
        ui::rect(hl, ui::hex(0xFFFFFF, 0.045f * appear), S_(16));
        ui::stroke(hl, ui::hex(0xFFFFFF, 0.07f * appear), S_(16), std::max(1.0f, S_(1)));
        ui::rect(ui::Rect{hl.x, hl.y + S_(26), S_(3), hl.h - S_(52)}, accentOf(cur, appear), S_(1.5f));
    }
    for (int i = 0; i < int(infos_.size()); ++i) {
        const SceneInfo& si = infos_[i];
        const ui::Rect r{x0 - S_(26), listY + i * itemH, itemW, itemH - S_(14)};
        entryRects_.push_back(r);
        const float sel = std::max(0.0f, 1.0f - std::fabs(selAnim_ - float(i)));
        const float on = (0.5f + 0.5f * sel + (hovered_ == i ? 0.12f : 0.0f)) * appear;
        ui::text(r.x + S_(30), r.y + S_(34), si.number, theme::mono(S_, 17.0f, ui::mix(theme::faint(), accentText(si), sel)));
        ui::text(r.x + S_(84), r.y + S_(20), si.title, theme::serif(S_, 44.0f, ui::withAlpha(theme::text(), std::min(1.0f, on))));
        ui::text(r.x + S_(86), r.y + S_(86), si.subtitle, theme::small(S_, ui::withAlpha(theme::text(), std::min(1.0f, 0.42f + 0.3f * sel) * appear)));
    }

    // right: the picture, large, with its name and tags over it
    const float cardX = S_(1010), cardW = W - cardX - S_(150), cardH = cardW * 9.0f / 16.0f, cardY = S_(196) + lift;
    const ui::Rect card{cardX, cardY, cardW, cardH};
    ui::shadow(card, S_(20), S_(46), ui::hex(0x000000, 0.65f * appear));
    const float zoom = 1.02f + 0.025f * float(0.5 + 0.5 * std::sin(now * 0.09));
    if (swap < 1.0f) ui::image(pictures_[prevSelected_].sharp, card, S_(20), appear, zoom);
    ui::image(pictures_[selected_].sharp, card, S_(20), appear * swap, zoom);
    ui::rectGradient(ui::Rect{card.x, card.y + card.h * 0.52f, card.w, card.h * 0.48f}, ui::hex(0x050506, 0.0f), ui::hex(0x050506, 0.78f * appear), S_(20));
    ui::stroke(card, ui::hex(0xFFFFFF, 0.09f * appear), S_(20), std::max(1.0f, S_(1)));
    {
        float tx = card.x + S_(44), ty = card.y + card.h - S_(150);
        ui::text(tx, ty, cur.number + "  ·  " + cur.title, theme::caption(S_, ui::withAlpha(accentText(cur), swap * appear)));
        ty += S_(40);
        ui::text(tx, ty, cur.subtitle, theme::serif(S_, 34.0f, ui::withAlpha(theme::text(), swap * appear)));
        ty += S_(58);
        float cx = tx;
        const ui::TextStyle ts = {ui::Font::SansMedium, S_(14.0f), ui::withAlpha(theme::text(), 0.85f * swap * appear), S_(1.2f), true};
        for (const std::string& tag : cur.tags) {
            const float w = ui::measure(tag, ts) + S_(26);
            const ui::Rect pr{cx, ty, w, S_(30)};
            ui::panel(pr, ui::hex(0x000000, 0.35f * swap * appear), ui::hex(0xFFFFFF, 0.22f * swap * appear), S_(15), std::max(1.0f, S_(1)));
            ui::text(cx + S_(13), ty + 0.5f * (S_(30) - ui::lineHeight(ts)), tag, ts);
            cx += w + S_(10);
        }
    }

    // under the picture: what it is, and the way in
    const float y2 = card.y + card.h + S_(46);
    ui::textWrapped(card.x, y2, card.w - S_(470), cur.description, theme::body(S_, ui::withAlpha(theme::muted(), appear * swap)), 1.5f, 4);
    {
        const ui::TextStyle bt = {ui::Font::SansSemiBold, S_(21.0f), ui::hex(0x0B0B0C), 0.0f, false};
        const std::string label = "Sahneyi aç";
        const float capW = ui::measure("Enter", theme::mono(S_, 14.5f)) + S_(18);
        const float bw = ui::measure(label, bt) + capW + S_(86), bh = S_(66);
        openButton_ = ui::Rect{card.x + card.w - bw, y2, bw, bh};
        const bool hot = openButton_.contains(float(mouseX_), float(mouseY_));
        ui::shadow(openButton_, bh * 0.5f, S_(20), ui::withAlpha(accentOf(cur), 0.35f * appear));
        ui::rect(openButton_, ui::mix(accentOf(cur, appear), ui::hex(0xFFFFFF, appear), hot ? 0.18f : 0.0f), bh * 0.5f);
        ui::text(openButton_.x + S_(34), openButton_.y + 0.5f * (bh - ui::lineHeight(bt)), label, bt);
        const ui::Rect cap{openButton_.x + bw - capW - S_(22), openButton_.y + 0.5f * (bh - S_(30)), capW, S_(30)};
        ui::panel(cap, ui::hex(0x000000, 0.12f), ui::hex(0x000000, 0.25f), S_(7), std::max(1.0f, S_(1)));
        const ui::TextStyle ct = theme::mono(S_, 14.5f, ui::hex(0x0B0B0C, 0.85f));
        ui::text(cap.x + 0.5f * (cap.w - ui::measure("Enter", ct)), cap.y + 0.5f * (cap.h - ui::lineHeight(ct)), "Enter", ct);
    }

    // the bottom line: keys on the left, the machine on the right
    const float by = H - S_(84);
    ui::rect(ui::Rect{x0 - S_(26), by - S_(26), W - 2 * (x0 - S_(26)), std::max(1.0f, S_(1))}, ui::hex(0xFFFFFF, 0.07f * appear));
    float bx = x0;
    bx += keyHint(bx, by, "↑|↓", "Seç") + S_(34);
    bx += keyHint(bx, by, "Enter", "Aç") + S_(34);
    bx += keyHint(bx, by, "Esc", "Çık");
    const std::string sys = gpuName_ + "  ·  OptiX 9.1  ·  CUDA 13.4  ·  " + std::to_string(fbw_) + "×" + std::to_string(fbh_);
    const ui::TextStyle st = theme::mono(S_, 14.0f, theme::faint());
    ui::text(W - S_(150) - ui::measure(sys, st), by + 0.5f * (S_(30) - ui::lineHeight(st)), sys, st);
    drawToasts(now);
    ui::end();
}

// ---------------------------------------------------------------------------------------------------
void App::drawLoading(double now) {
    const float W = float(fbw_), H = float(fbh_);
    const SceneInfo& si = infos_[sceneIndex_];
    LoadState st;
    {
        std::lock_guard<std::mutex> lock(loadMx_);
        st = loadShared_;
    }
    const float t = float(now - modeT0_);
    const float in = smooth(t / 0.5f);
    ui::begin(fbw_, fbh_);
    ui::clear(ui::Color{0, 0, 0, 1.0f});
    const ui::Rect screen{0, 0, W, H};
    ui::image(pictures_[sceneIndex_].soft, screen, 0, 1.0f, 1.08f + 0.04f * smooth(t / 20.0f), 0.5f, 0.5f, 0.42f + 0.12f * in, 0.85f);
    ui::rectHGradient(ui::Rect{0, 0, W * 0.5f, H}, ui::hex(0x060708, 0.90f), ui::hex(0x060708, 0.55f));
    ui::rectHGradient(ui::Rect{W * 0.5f, 0, W * 0.5f, H}, ui::hex(0x060708, 0.55f), ui::hex(0x060708, 0.30f));
    const float x0 = S_(150);
    float y = H * 0.5f - S_(150) + (1.0f - easeOut(in)) * S_(20);
    ui::text(x0, y, "Sahne " + si.number, theme::caption(S_, ui::withAlpha(accentText(si), in)));
    y += S_(44);
    ui::text(x0 - S_(4), y, si.title, theme::display(S_, 104.0f, ui::withAlpha(theme::text(), in)));
    y += S_(142);
    ui::text(x0, y, si.subtitle, theme::body(S_, ui::withAlpha(theme::muted(), in)));
    y += S_(84);
    // progress: the steps' fraction, eased; a travelling glint while a step runs
    static float shown = 0.0f;
    if (t < 0.05f) shown = 0.0f;
    shown += (st.fraction - shown) * 0.12f;
    const float bw = S_(620), bh = S_(4);
    ui::rect(ui::Rect{x0, y, bw, bh}, ui::hex(0xFFFFFF, 0.12f * in), bh * 0.5f);
    ui::rect(ui::Rect{x0, y, std::max(bh, bw * shown), bh}, accentOf(si, in), bh * 0.5f);
    {   // a glint travelling along the filled part (kept inside it)
        const float fill = bw * shown, g = float(std::fmod(now * 0.6, 1.0));
        const float gx0 = std::max(x0, x0 + fill * g - S_(40)), gx1 = std::min(x0 + fill, x0 + fill * g + S_(40));
        if (gx1 > gx0 + 1.0f) ui::rect(ui::Rect{gx0, y, gx1 - gx0, bh}, ui::hex(0xFFFFFF, 0.35f * in), bh * 0.5f);
    }
    y += S_(26);
    std::string label = st.label;
    const int dots = int(now * 2.5) % 4;
    for (int k = 0; k < dots; ++k) label += ".";
    ui::text(x0, y, label, theme::small(S_, ui::withAlpha(theme::muted(), in)));
    keyHint(W - S_(150) - S_(140), H - S_(84), "Esc", "Vazgeç");
    drawToasts(now);
    ui::end();
}

void App::drawFailed(double now) {
    const float W = float(fbw_), H = float(fbh_);
    ui::begin(fbw_, fbh_);
    ui::clear(ui::Color{0.04f, 0.04f, 0.05f, 1.0f});
    const float x0 = S_(150);
    float y = H * 0.5f - S_(120);
    ui::text(x0, y, "Sahne açılamadı", theme::caption(S_, ui::hex(0xE8806A)));
    y += S_(44);
    ui::text(x0, y, sceneIndex_ >= 0 ? infos_[sceneIndex_].title : std::string(), theme::display(S_, 72.0f));
    y += S_(112);
    ui::textWrapped(x0, y, W - 2 * x0, failure_, theme::body(S_), 1.5f, 6);
    keyHint(x0, H - S_(84), "Esc", "Galeriye dön");
    ui::end();
}

// ---------------------------------------------------------------------------------------------------
// over a running scene: who and what (bottom left), the way out and help (bottom right), timings (top right)
void App::drawHud(double now, const Status& st) {
    const float W = float(fbw_), H = float(fbh_);
    const SceneInfo& si = infos_[sceneIndex_];
    const float in = easeOut(float((now - modeT0_ - 0.2) / 0.6));
    const float slide = (1.0f - in) * S_(30);
    const float m = S_(34), pad = S_(22);

    // ---- bottom left: number, name, state; a selector or a progress bar under it
    const ui::TextStyle chipT = {ui::Font::SansMedium, S_(15.5f), theme::text(), 0.0f, false};
    const float row1 = S_(34);
    float w1 = 0.0f;
    {
        w1 += ui::measure(si.number, theme::mono(S_, 15.0f)) + S_(14);
        w1 += ui::measure(si.title, theme::caption(S_)) + S_(22);
        for (const Chip& c : st.chips) w1 += ui::measure(c.text, chipT) + S_(28) + S_(8);
    }
    float h2 = 0.0f, w2 = 0.0f;
    const ui::TextStyle detailT = theme::small(S_);
    if (!st.selector.items.empty()) {
        const float seg = S_(48), gap = S_(8);
        w2 = ui::measure(st.selector.title, theme::caption(S_, theme::faint())) + S_(20) + st.selector.items.size() * (seg + gap) - gap;
        w2 = std::max(w2, ui::measure(st.selector.detail, detailT));
        h2 = S_(16) + seg + S_(14) + ui::lineHeight(detailT);
    }
    if (st.progress >= 0.0f) {
        w2 = std::max(w2, std::max(S_(420), ui::measure(st.progressLabel, detailT)));
        h2 += S_(18) + ui::lineHeight(detailT) + S_(10) + S_(4);
    }
    const float pw = std::max(w1, w2) + 2 * pad, ph = row1 + h2 + 2 * pad - S_(6);
    const ui::Rect P{m, H - m - ph + slide, pw, ph};
    ui::shadow(P, S_(18), S_(28), ui::hex(0x000000, 0.45f * in));
    ui::panel(P, ui::withAlpha(theme::glass(), in), ui::withAlpha(theme::glassBorder(), in), S_(18), std::max(1.0f, S_(1)));
    float x = P.x + pad, y = P.y + pad - S_(3);
    {
        const ui::TextStyle nt = theme::mono(S_, 15.0f, ui::withAlpha(accentText(si), in));
        x += ui::text(x, y + 0.5f * (row1 - ui::lineHeight(nt)), si.number, nt) + S_(14);
        const ui::TextStyle tt = theme::caption(S_, ui::withAlpha(theme::text(), in));
        x += ui::text(x, y + 0.5f * (row1 - ui::lineHeight(tt)), si.title, tt) + S_(22);
        for (const Chip& c : st.chips) {
            const float cw = ui::measure(c.text, chipT) + S_(28);
            const ui::Rect cr{x, y + 0.5f * (row1 - S_(30)), cw, S_(30)};
            if (c.strong) ui::panel(cr, accentOf(si, 0.20f * in), accentOf(si, 0.55f * in), S_(15), std::max(1.0f, S_(1)));
            else ui::panel(cr, ui::hex(0xFFFFFF, 0.06f * in), ui::hex(0xFFFFFF, 0.12f * in), S_(15), std::max(1.0f, S_(1)));
            ui::TextStyle ct = chipT;
            ct.color = ui::withAlpha(c.strong ? accentText(si) : theme::text(), in);
            ui::text(cr.x + S_(14), cr.y + 0.5f * (cr.h - ui::lineHeight(ct)), c.text, ct);
            x += cw + S_(8);
        }
    }
    y += row1;
    if (!st.selector.items.empty()) {
        y += S_(16);
        x = P.x + pad;
        const float seg = S_(48), gap = S_(8);
        const ui::TextStyle lt = theme::caption(S_, ui::withAlpha(theme::faint(), in));
        x += ui::text(x, y + 0.5f * (seg - ui::lineHeight(lt)), st.selector.title, lt) + S_(20);
        for (int i = 0; i < int(st.selector.items.size()); ++i) {
            const ui::Rect r{x, y, seg, seg};
            const bool on = i == st.selector.active;
            if (on) {
                ui::shadow(r, S_(12), S_(14), accentOf(si, 0.45f * in));
                ui::rect(r, accentOf(si, in), S_(12));
            } else {
                ui::panel(r, ui::hex(0xFFFFFF, 0.05f * in), ui::hex(0xFFFFFF, 0.14f * in), S_(12), std::max(1.0f, S_(1)));
            }
            const ui::TextStyle it = theme::mono(S_, 20.0f, on ? ui::hex(0x0B0B0C, in) : ui::withAlpha(theme::text(), 0.85f * in));
            ui::text(r.x + 0.5f * (seg - ui::measure(st.selector.items[i], it)), r.y + 0.5f * (seg - ui::lineHeight(it)), st.selector.items[i], it);
            x += seg + gap;
        }
        y += seg + S_(14);
        ui::text(P.x + pad, y, st.selector.detail, theme::small(S_, ui::withAlpha(theme::muted(), in)));
        y += ui::lineHeight(detailT);
    }
    if (st.progress >= 0.0f) {
        y += S_(18);
        ui::text(P.x + pad, y, st.progressLabel, theme::small(S_, ui::withAlpha(theme::text(), 0.9f * in)));
        y += ui::lineHeight(detailT) + S_(10);
        const float bw = P.w - 2 * pad;
        ui::rect(ui::Rect{P.x + pad, y, bw, S_(4)}, ui::hex(0xFFFFFF, 0.12f * in), S_(2));
        ui::rect(ui::Rect{P.x + pad, y, std::max(S_(4), bw * std::min(1.0f, st.progress)), S_(4)}, accentOf(si, in), S_(2));
    }

    // ---- bottom right: help and the way back
    {
        const float hh = S_(30);
        float tw = 0.0f;
        const ui::TextStyle ht = theme::small(S_);
        auto hintW = [&](const std::string& keys, const std::string& what) {
            float w = 0.0f;
            for (const std::string& k : splitCaps(keys)) w += std::max(hh, ui::measure(k, theme::mono(S_, 14.5f)) + S_(18)) + S_(6);
            return w - S_(6) + S_(10) + ui::measure(what, ht);
        };
        tw = hintW("F1", "Tuşlar") + hintW("H", "Gizle") + hintW("Esc", "Galeri") + 2 * S_(26);
        const ui::Rect R{W - m - tw - 2 * S_(18), H - m - hh - 2 * S_(16) + slide, tw + 2 * S_(18), hh + 2 * S_(16)};
        ui::panel(R, ui::withAlpha(theme::glass(), in), ui::withAlpha(theme::glassBorder(), in), S_(16), std::max(1.0f, S_(1)));
        float hx = R.x + S_(18);
        hx += keyHint(hx, R.y + S_(16), "F1", "Tuşlar") + S_(26);
        hx += keyHint(hx, R.y + S_(16), "H", "Gizle") + S_(26);
        keyHint(hx, R.y + S_(16), "Esc", "Galeri");
    }

    // ---- top right: how fast
    {
        const ui::TextStyle big = theme::mono(S_, 30.0f);
        const ui::TextStyle lab = theme::small(S_, theme::muted());
        const ui::TextStyle val = theme::mono(S_, 15.5f, theme::text());
        const std::string fpsS = std::to_string(int(std::lround(fps_)));
        float lw = 0.0f, vw = 0.0f;
        std::vector<Metric> rows = st.metrics;
        if (gpuW_ > 0.0f) rows.push_back({"Ekran kartı", std::to_string(int(std::lround(gpuW_))) + " W · " + std::to_string(int(gpuC_)) + " °C"});
        for (const Metric& r : rows) { lw = std::max(lw, ui::measure(r.label, lab)); vw = std::max(vw, ui::measure(r.value, val)); }
        const float rowH = S_(28);
        const float w = std::max(lw + S_(24) + vw, ui::measure(fpsS, big) + S_(70)) + 2 * S_(20);
        const float h = S_(20) + ui::lineHeight(big) + S_(10) + rows.size() * rowH + S_(14);
        const ui::Rect R{W - m - w, m - slide, w, h};
        ui::panel(R, ui::withAlpha(theme::glass(), in), ui::withAlpha(theme::glassBorder(), in), S_(16), std::max(1.0f, S_(1)));
        float yy = R.y + S_(16);
        const float fw = ui::text(R.x + S_(20), yy, fpsS, theme::mono(S_, 30.0f, ui::withAlpha(theme::text(), in)));
        ui::text(R.x + S_(20) + fw + S_(10), yy + ui::ascent(big) - ui::ascent(theme::caption(S_)) , "fps", theme::caption(S_, ui::withAlpha(theme::faint(), in)));
        yy += ui::lineHeight(big) + S_(10);
        ui::rect(ui::Rect{R.x + S_(20), yy - S_(6), R.w - S_(40), std::max(1.0f, S_(1))}, ui::hex(0xFFFFFF, 0.08f * in));
        for (const Metric& r : rows) {
            ui::text(R.x + S_(20), yy + S_(2), r.label, theme::small(S_, ui::withAlpha(theme::muted(), in)));
            ui::text(R.x + R.w - S_(20) - ui::measure(r.value, val), yy + S_(3), r.value, theme::mono(S_, 15.5f, ui::withAlpha(theme::text(), in)));
            yy += rowH;
        }
    }
}

// ---------------------------------------------------------------------------------------------------
void App::drawHelp(double now) {
    (void)now;
    const float W = float(fbw_), H = float(fbh_);
    const SceneInfo& si = infos_[sceneIndex_];
    ui::rect(ui::Rect{0, 0, W, H}, ui::hex(0x000000, 0.58f));
    std::vector<KeyGroup> groups = si.keys, left, right;
    KeyGroup general{"Genel", {{"Esc", "Galeriye dön (yardım açıkken: kapat)"}, {"F1", "Bu tuş listesi"}, {"H", "Arayüzü gizle / göster"},
                               {"F12", "Ekran görüntüsü (captures/)"}}};
    if (si.reloadable) general.items.push_back({"F5", "Ayarları ve programları yeniden yükle"});
    groups.push_back(general);
    {
        float hl = 0.0f, hr = 0.0f;
        for (const KeyGroup& g : groups) {
            const float h = float(g.items.size()) + 1.6f;
            if (hl <= hr) { left.push_back(g); hl += h; }
            else { right.push_back(g); hr += h; }
        }
    }
    const ui::TextStyle head = theme::caption(S_, theme::faint());
    const ui::TextStyle desc = theme::small(S_, theme::text());
    const float rowH = S_(40), capCol = S_(250), colW = S_(640), gapCol = S_(70);
    auto colHeight = [&](const std::vector<KeyGroup>& gs) {
        float h = 0;
        for (const KeyGroup& g : gs) h += S_(44) + g.items.size() * rowH + S_(22);
        return h;
    };
    const float bodyH = std::max(colHeight(left), colHeight(right));
    const float pw = 2 * colW + gapCol + 2 * S_(56), ph = S_(56) + S_(150) + bodyH + S_(70);
    const ui::Rect P{0.5f * (W - pw), 0.5f * (H - ph), pw, ph};
    ui::shadow(P, S_(24), S_(60), ui::hex(0x000000, 0.7f));
    ui::panel(P, ui::hex(0x0E1013, 0.96f), theme::glassBorder(), S_(24), std::max(1.0f, S_(1)));
    float x = P.x + S_(56), y = P.y + S_(52);
    ui::text(x, y, si.number + "  ·  tuşlar", theme::caption(S_, accentText(si)));
    y += S_(40);
    ui::text(x - S_(2), y, si.title, theme::display(S_, 52.0f));
    y += S_(100);
    ui::rect(ui::Rect{x, y - S_(18), P.w - 2 * S_(56), std::max(1.0f, S_(1))}, ui::hex(0xFFFFFF, 0.08f));
    auto column = [&](float cx, float cy, const std::vector<KeyGroup>& gs) {
        for (const KeyGroup& g : gs) {
            ui::text(cx, cy, g.title, head);
            cy += S_(44);
            for (const KeyHelp& k : g.items) {
                // caps right-aligned in their column, the words after
                float capsW = 0.0f;
                for (const std::string& c : splitCaps(k.keys)) capsW += std::max(S_(30), ui::measure(c, theme::mono(S_, 14.5f)) + S_(18)) + S_(6);
                capsW -= S_(6);
                keyCaps(cx + capCol - capsW, cy, k.keys, S_(30));
                ui::text(cx + capCol + S_(22), cy + 0.5f * (S_(30) - ui::lineHeight(desc)), k.what, desc);
                cy += rowH;
            }
            cy += S_(22);
        }
    };
    column(x, y, left);
    column(x + colW + gapCol, y, right);
    const ui::TextStyle ft = theme::small(S_, theme::faint());
    const std::string foot = "F1 ya da Esc ile kapanır";
    ui::text(P.x + P.w - S_(56) - ui::measure(foot, ft), P.y + P.h - S_(52), foot, ft);
}

void App::drawToasts(double now) {
    std::vector<Toast> ts;
    {
        std::lock_guard<std::mutex> lock(toastMx_);
        toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return now - t.t0 > 3.2; }), toasts_.end());
        ts = toasts_;
    }
    const float W = float(fbw_), H = float(fbh_);
    float y = H - S_(200);
    const ui::TextStyle tt = theme::bodyStrong(S_);
    const ui::Color acc = sceneIndex_ >= 0 && mode_ == Mode::Running ? ui::hex(infos_[sceneIndex_].accent) : ui::hex(0xF2EFE9);
    for (auto it = ts.rbegin(); it != ts.rend(); ++it) {
        const float age = float(now - it->t0);
        const float a = smooth(age / 0.18f) * (1.0f - smooth((age - 2.6f) / 0.6f));
        const float w = ui::measure(it->text, tt) + S_(78), h = S_(52);
        const ui::Rect r{0.5f * (W - w), y + (1.0f - smooth(age / 0.25f)) * S_(12), w, h};
        ui::shadow(r, h * 0.5f, S_(22), ui::hex(0x000000, 0.45f * a));
        ui::panel(r, ui::withAlpha(theme::glass(), a), ui::withAlpha(theme::glassBorder(), a), h * 0.5f, std::max(1.0f, S_(1)));
        ui::circle(r.x + S_(28), r.y + 0.5f * h, S_(5), ui::withAlpha(acc, a));
        ui::TextStyle t2 = tt;
        t2.color = ui::withAlpha(theme::text(), a);
        ui::text(r.x + S_(48), r.y + 0.5f * (h - ui::lineHeight(t2)), it->text, t2);
        y -= h + S_(12);
    }
}

}   // namespace app
