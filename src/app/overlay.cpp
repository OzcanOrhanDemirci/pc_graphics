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
// key caps, split at "|"; dark: on a button in the accent colour (dark ink on a slightly darker cap). Three tokens are
// words between caps, not caps: "+" (together: "Ctrl|+|Sol tık"), "/" (either: "Sol tık|/|G") and "–" (a range: "3|–|9")
namespace {
bool capJoiner(const std::string& k) { return k == "+" || k == "/" || k == "–"; }
}   // namespace

float App::keyCapsWidth(const std::string& keys, float height) {
    const ui::TextStyle t = theme::mono(S_, 14.5f);
    float w = 0.0f;
    for (const std::string& k : splitCaps(keys))
        w += (capJoiner(k) ? ui::measure(k, theme::small(S_)) + S_(6) : std::max(height, ui::measure(k, t) + S_(18))) + S_(6);
    return keys.empty() ? 0.0f : w - S_(6);
}

float App::keyCaps(float x, float y, const std::string& keys, float height, float alpha, bool dark) {
    const ui::TextStyle t = theme::mono(S_, 14.5f, dark ? ui::hex(0x0B0B0C, 0.85f * alpha) : ui::withAlpha(theme::text(), alpha));
    float cx = x;
    for (const std::string& k : splitCaps(keys)) {
        if (capJoiner(k)) {
            const ui::TextStyle j = theme::small(S_, dark ? ui::hex(0x0B0B0C, 0.7f * alpha) : ui::withAlpha(theme::muted(), alpha));
            ui::text(cx + S_(3), y + 0.5f * (height - ui::lineHeight(j)), k, j);
            cx += ui::measure(k, j) + S_(6) + S_(6);
            continue;
        }
        const float w = std::max(height, ui::measure(k, t) + S_(18));
        const ui::Rect r{cx, y, w, height};
        if (dark) {
            ui::panel(r, ui::hex(0x000000, 0.12f * alpha), ui::hex(0x000000, 0.25f * alpha), S_(7), std::max(1.0f, S_(1)));
        } else {
            ui::rect(ui::Rect{r.x, r.y + S_(2), r.w, r.h}, ui::hex(0x000000, 0.35f * alpha), S_(7));
            ui::panel(r, ui::withAlpha(theme::capFill(), alpha), ui::withAlpha(theme::capBorder(), alpha), S_(7), std::max(1.0f, S_(1)));
        }
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
    ui::text(x0, y, L("PC_GRAPHICS  ·  SAHNELER", "PC_GRAPHICS  ·  SCENES"), theme::caption(S_, theme::faint()));
    y += S_(46);
    ui::text(x0, y, L("Işık, atmosfer, fizik.", "Light, atmosphere, physics."), theme::display(S_, 66.0f, ui::withAlpha(theme::text(), appear)));
    y += S_(96);
    {   // how many scenes there are, in words (the registry decides)
        static const char* tr[] = {"", "bir", "iki", "üç", "dört", "beş", "altı", "yedi", "sekiz", "dokuz"};
        static const char* en[] = {"", "One scene", "Two scenes", "Three scenes", "Four scenes", "Five scenes", "Six scenes", "Seven scenes",
                                   "Eight scenes", "Nine scenes"};
        const size_t n = infos_.size();
        const std::string countTr = n < 10 ? std::string(tr[n]) : std::to_string(n);
        const std::string countEn = n < 10 ? std::string(en[n]) : std::to_string(n) + " scenes";
        ui::textWrapped(x0, y, S_(700),
                        L("Oyun motoru kullanmadan, ekran kartında baştan sona kendi kodumuzla kurulmuş " + countTr +
                              " sahne. Her biri tek bir dar alana odaklanır ve o alanda gidilebilecek en uca gitmeyi dener.",
                          countEn + " built from scratch on the GPU in our own code, without a game engine. Each one keeps to a single narrow "
                                    "subject and tries to take it as far as it will go."),
                        theme::body(S_, ui::withAlpha(theme::muted(), appear)), 1.5f);
    }

    // the scenes' list fills the room above the key hints; each entry as tall as the design's until there are too many
    const float listY = S_(566) + lift, itemW = S_(770);
    const float itemH = std::max(S_(26), std::min(S_(164), (H - S_(154) - S_(566)) / float(std::max<size_t>(1, infos_.size()))));
    entryRects_.clear();
    // the highlight glides to the selected entry
    {
        const ui::Rect hl{x0 - S_(26), listY + selAnim_ * itemH, itemW, itemH - S_(14)};
        ui::rect(hl, ui::hex(0xFFFFFF, 0.045f * appear), S_(16));
        ui::stroke(hl, ui::hex(0xFFFFFF, 0.07f * appear), S_(16), std::max(1.0f, S_(1)));
        const float barH = std::max(hl.h - S_(52), 0.5f * hl.h), barY = hl.y + 0.5f * (hl.h - barH);
        ui::rect(ui::Rect{hl.x, barY, S_(3), barH}, accentOf(cur, appear), S_(1.5f));
    }
    for (int i = 0; i < int(infos_.size()); ++i) {
        const SceneInfo& si = infos_[i];
        const ui::Rect r{x0 - S_(26), listY + i * itemH, itemW, itemH - S_(14)};
        entryRects_.push_back(r);
        const float sel = std::max(0.0f, 1.0f - std::fabs(selAnim_ - float(i)));
        const float on = (0.5f + 0.5f * sel + (hovered_ == i ? 0.12f : 0.0f)) * appear;
        // a shorter entry (many scenes) draws its name smaller and the line under it just below: at eight scenes the
        // design's fixed sizes ran each entry's line into the next one's name
        // (and in a small window, where not even that fits, the name alone, as large as the entry allows)
        const float k = std::clamp(itemH / S_(164), 0.5f, 1.0f);
        float size = 44.0f * (0.45f + 0.55f * k);
        ui::TextStyle ts = theme::serif(S_, size, ui::withAlpha(theme::text(), std::min(1.0f, on)));
        const ui::TextStyle ss = theme::small(S_, ui::withAlpha(theme::text(), std::min(1.0f, 0.42f + 0.3f * sel) * appear));
        const float room = itemH - S_(14);
        bool sub = true;
        if (k < 1.0f && ui::lineHeight(ts) + S_(4) + ui::lineHeight(ss) > room) {
            sub = false;
            if (ui::lineHeight(ts) > room) ts = theme::serif(S_, size * room / ui::lineHeight(ts), ts.color);
        }
        const float ty = k < 1.0f ? r.y + (sub ? S_(20) * k : 0.5f * (room - ui::lineHeight(ts))) : r.y + S_(20);
        ui::text(r.x + S_(30), k < 1.0f ? ty + 0.5f * (ui::lineHeight(ts) - S_(17)) : r.y + S_(34), si.number, theme::mono(S_, 17.0f, ui::mix(theme::faint(), accentText(si), sel)));
        ui::text(r.x + S_(84), ty, si.title, ts);
        if (sub) ui::text(r.x + S_(86), k < 1.0f ? ty + ui::lineHeight(ts) + S_(4) : r.y + S_(86), si.subtitle, ss);
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
    // the details over the picture (A): how far open, 0..1
    const float detailsOpen = [&] {
        const float t = smooth(float((now - detailsAt_) / 0.28));
        return details_ ? t : 1.0f - t;
    }();
    {   // the picture's own words step aside while the details cover it
        const float on = swap * appear * (1.0f - detailsOpen);
        float tx = card.x + S_(44), ty = card.y + card.h - S_(150);
        ui::text(tx, ty, cur.number + "  ·  " + cur.title, theme::caption(S_, ui::withAlpha(accentText(cur), on)));
        ty += S_(40);
        ui::text(tx, ty, cur.subtitle, theme::serif(S_, 34.0f, ui::withAlpha(theme::text(), on)));
        ty += S_(58);
        float cx = tx;
        const ui::TextStyle ts = {ui::Font::SansMedium, S_(14.0f), ui::withAlpha(theme::text(), 0.85f * on), S_(1.2f), true};
        for (const std::string& tag : cur.tags) {
            const float w = ui::measure(tag, ts) + S_(26);
            const ui::Rect pr{cx, ty, w, S_(30)};
            ui::panel(pr, ui::hex(0x000000, 0.35f * on), ui::hex(0xFFFFFF, 0.22f * on), S_(15), std::max(1.0f, S_(1)));
            ui::text(cx + S_(13), ty + 0.5f * (S_(30) - ui::lineHeight(ts)), tag, ts);
            cx += w + S_(10);
        }
    }

    // over the picture, when asked for: how it is made
    if (detailsOpen > 0.003f) drawDetails(card, detailsOpen);

    // under the picture: what it is, and the ways in (how it is made; open it)
    const float y2 = card.y + card.h + S_(46);
    {
        const float bh = S_(66);
        const ui::TextStyle bt = {ui::Font::SansSemiBold, S_(21.0f), ui::hex(0x0B0B0C), 0.0f, false};
        const float mx = float(mouseX_), my = float(mouseY_);
        auto capW = [&](const std::string& k) { return std::max(S_(30), ui::measure(k, theme::mono(S_, 14.5f)) + S_(18)); };
        {   // open: in the scene's colour
            const std::string label = L("Sahneyi aç", "Open scene");
            const float bw = ui::measure(label, bt) + capW("Enter") + S_(86);
            openButton_ = ui::Rect{card.x + card.w - bw, y2, bw, bh};
            const bool hot = openButton_.contains(mx, my);
            ui::shadow(openButton_, bh * 0.5f, S_(20), ui::withAlpha(accentOf(cur), 0.35f * appear));
            ui::rect(openButton_, ui::mix(accentOf(cur, appear), ui::hex(0xFFFFFF, appear), hot ? 0.18f : 0.0f), bh * 0.5f);
            ui::text(openButton_.x + S_(34), openButton_.y + 0.5f * (bh - ui::lineHeight(bt)), label, bt);
            keyCaps(openButton_.x + bw - capW("Enter") - S_(22), openButton_.y + 0.5f * (bh - S_(30)), "Enter", S_(30), appear, true);
        }
        {   // how it is made: glass, before it
            const std::string label = details_ ? L("Ayrıntıları kapat", "Hide details") : L("Ayrıntılar", "Details");
            ui::TextStyle dt = bt;
            dt.font = ui::Font::SansMedium;
            dt.color = ui::withAlpha(theme::text(), appear);
            const float bw = ui::measure(label, dt) + capW("A") + S_(86);
            detailsButton_ = ui::Rect{openButton_.x - S_(16) - bw, y2, bw, bh};
            const bool hot = detailsButton_.contains(mx, my);
            const float fill = details_ ? 0.16f : hot ? 0.12f : 0.06f;
            ui::panel(detailsButton_, ui::hex(0xFFFFFF, fill * appear), ui::hex(0xFFFFFF, (hot || details_ ? 0.34f : 0.20f) * appear), bh * 0.5f,
                      std::max(1.0f, S_(1)));
            ui::text(detailsButton_.x + S_(34), detailsButton_.y + 0.5f * (bh - ui::lineHeight(dt)), label, dt);
            keyCaps(detailsButton_.x + bw - capW("A") - S_(22), detailsButton_.y + 0.5f * (bh - S_(30)), "A", S_(30), appear);
        }
    }
    ui::textWrapped(card.x, y2 - S_(4), detailsButton_.x - S_(48) - card.x, cur.description,
                    theme::body(S_, ui::withAlpha(theme::muted(), appear * swap)), 1.5f, 3);

    // the language, top right: two segments, the one in use filled
    {
        const ui::TextStyle lt = theme::mono(S_, 15.0f);
        const float h = S_(38), segW = S_(58), gap = S_(4);
        const float x = W - S_(150) - (2 * segW + gap + 2 * S_(4)), y = S_(110) + lift;
        const ui::Rect pill{x, y, 2 * segW + gap + 2 * S_(4), h + 2 * S_(4)};
        ui::panel(pill, ui::hex(0xFFFFFF, 0.05f * appear), ui::hex(0xFFFFFF, 0.14f * appear), pill.h * 0.5f, std::max(1.0f, S_(1)));
        const char* names[2] = {"TR", "EN"};
        for (int k = 0; k < 2; ++k) {
            const ui::Rect r{x + S_(4) + k * (segW + gap), y + S_(4), segW, h};
            langRects_[k] = r;
            const bool on = (k == 1) == (lang() == Lang::EN);
            const bool hot = !on && r.contains(float(mouseX_), float(mouseY_));
            if (on) ui::rect(r, ui::hex(0xF2EFE9, 0.92f * appear), h * 0.5f);
            else if (hot) ui::rect(r, ui::hex(0xFFFFFF, 0.10f * appear), h * 0.5f);
            ui::TextStyle t = lt;
            t.color = on ? ui::hex(0x0B0B0C, appear) : ui::withAlpha(theme::text(), 0.75f * appear);
            ui::text(r.x + 0.5f * (r.w - ui::measure(names[k], t)), r.y + 0.5f * (h - ui::lineHeight(t)), names[k], t);
        }
    }

    // the bottom line: keys on the left, the machine on the right
    const float by = H - S_(84);
    ui::rect(ui::Rect{x0 - S_(26), by - S_(26), W - 2 * (x0 - S_(26)), std::max(1.0f, S_(1))}, ui::hex(0xFFFFFF, 0.07f * appear));
    float bx = x0;
    bx += keyHint(bx, by, "↑|↓", L("Seç", "Select")) + S_(34);
    bx += keyHint(bx, by, "Enter", L("Aç", "Open")) + S_(34);
    bx += keyHint(bx, by, "A", L("Ayrıntılar", "Details")) + S_(34);
    bx += keyHint(bx, by, "L", L("English", "Türkçe")) + S_(34);
    bx += keyHint(bx, by, "Esc", details_ ? L("Ayrıntıları kapat", "Hide details") : L("Çık", "Quit"));
    const std::string sys = gpuName_ + "  ·  OptiX 9.1  ·  CUDA 13.4  ·  " + std::to_string(fbw_) + "×" + std::to_string(fbh_);
    const ui::TextStyle st = theme::mono(S_, 14.0f, theme::faint());
    ui::text(W - S_(150) - ui::measure(sys, st), by + 0.5f * (S_(30) - ui::lineHeight(st)), sys, st);
    drawToasts(now);
    ui::end();
}

// the gallery's "Ayrıntılar": how the selected scene is made, over its picture, in two columns balanced by height
void App::drawDetails(const ui::Rect& area, float a) {
    const SceneInfo& si = infos_[selected_];
    const ui::Rect P{area.x, area.y + (1.0f - a) * S_(16), area.w, area.h};
    ui::shadow(P, S_(20), S_(40), ui::hex(0x000000, 0.5f * a));
    ui::panel(P, ui::hex(0x0B0C0F, 0.975f * a), ui::hex(0xFFFFFF, 0.10f * a), S_(20), std::max(1.0f, S_(1)));
    const float pad = S_(54);
    float y = P.y + S_(46);
    ui::text(P.x + pad, y, si.number + "  ·  " + si.title + L("  ·  nasıl yapıldı", "  ·  how it was made"), theme::caption(S_, ui::withAlpha(accentText(si), a)));
    y += S_(34);
    ui::rect(ui::Rect{P.x + pad, y, P.w - 2 * pad, std::max(1.0f, S_(1))}, ui::hex(0xFFFFFF, 0.08f * a));
    y += S_(30);
    const float gapC = S_(60), colW = (P.w - 2 * pad - gapC) * 0.5f;
    const ui::TextStyle head = {ui::Font::SansSemiBold, S_(23.0f), ui::withAlpha(theme::text(), a), 0.0f, false};
    ui::TextStyle body = {ui::Font::Sans, S_(20.5f), ui::withAlpha(theme::muted(), a), 0.0f, false};
    const float spacing = 1.5f, headGap = S_(12);
    float after = S_(40);
    std::vector<float> hs;
    float total = 0.0f;
    int split = 0;   // the first `split` sections go left: the taller column as short as the order allows
    // a long text's body a little smaller where it would not fit (never below 85%): no section cut off
    for (float k = 1.0f; k >= 0.849f; k -= 0.03f) {
        body.size = S_(20.5f * k);
        after = S_(40.0f * k);
        hs.clear();
        total = 0.0f;
        for (const Detail& d : si.details) {
            hs.push_back(ui::lineHeight(head) + headGap + ui::measureWrapped(colW, d.text, body, spacing) + after);
            total += hs.back();
        }
        float best = 1e30f, left = 0.0f;
        for (int i = 0; i <= int(hs.size()); ++i) {
            if (std::max(left, total - left) < best) { best = std::max(left, total - left); split = i; }
            if (i < int(hs.size())) left += hs[i];
        }
        if (best - after <= P.y + P.h - S_(40) - y) break;
    }
    {   // the columns sit in the middle of the room below the rule (a little high)
        float tall = 0.0f, l = 0.0f;
        for (int i = 0; i < int(hs.size()); ++i) l += i < split ? hs[i] : 0.0f;
        tall = std::max(l, total - l) - after;
        const float room = P.y + P.h - S_(40) - y;
        if (tall < room) y += 0.4f * (room - tall);
    }
    const ui::Rect clip{P.x, P.y, P.w, P.h - S_(20)};
    ui::scissor(&clip);
    float yy[2] = {y, y};
    for (int i = 0; i < int(si.details.size()); ++i) {
        const int col = i < split ? 0 : 1;
        const float x = P.x + pad + col * (colW + gapC);
        ui::rect(ui::Rect{x, yy[col] + S_(4), S_(3), ui::lineHeight(head) - S_(8)}, accentOf(si, a), S_(1.5f));
        ui::text(x + S_(16), yy[col], si.details[i].title, head);
        yy[col] += ui::lineHeight(head) + headGap;
        yy[col] += ui::textWrapped(x, yy[col], colW, si.details[i].text, body, spacing) + after;
    }
    ui::scissor(nullptr);
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
    ui::text(x0, y, L("Sahne ", "Scene ") + si.number, theme::caption(S_, ui::withAlpha(accentText(si), in)));
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
    keyHint(W - S_(150) - S_(140), H - S_(84), "Esc", L("Vazgeç", "Cancel"));
    drawToasts(now);
    ui::end();
}

// The scene stopped its picture on purpose (another texture tier loading): the last frame held and dimmed, a card in
// the middle with what it does and a bar (the loading screen's: the scene's colour, a glint travelling along the filled
// part), so a held picture reads as work, not as a hang
void App::drawBusy(double now, const Status& st) {
    const SceneInfo& si = infos_[sceneIndex_];
    if (busyT0_ < 0.0) {
        busyT0_ = now;
        busyShown_ = 0.0f;
    }
    const float in = smooth(float((now - busyT0_) / 0.35));
    ui::rect(view_, ui::hex(0x000000, 0.38f * in));
    busyShown_ += (std::min(1.0f, std::max(0.0f, st.busy)) - busyShown_) * 0.15f;
    const ui::TextStyle titleT = theme::caption(S_, ui::withAlpha(accentText(si), in));
    const ui::TextStyle noteT = theme::small(S_, ui::withAlpha(theme::muted(), in));
    const ui::TextStyle labelT = theme::small(S_, ui::withAlpha(theme::text(), 0.9f * in));
    const ui::TextStyle pctT = theme::mono(S_, 15.0f, ui::withAlpha(theme::text(), 0.9f * in));
    const std::string note = L("Görüntü bilerek durduruldu: eski dokular kart belleğinden çıkarıldı, yenileri yükleniyor",
                               "The picture is held on purpose: the old textures are out of GPU memory, the new ones are loading");
    const float pad = S_(34), bw = std::max(S_(560), ui::measure(note, noteT));
    const float h = pad + ui::lineHeight(titleT) + S_(12) + ui::lineHeight(noteT) + S_(30) + S_(4) + S_(18) + ui::lineHeight(labelT) + pad;
    const ui::Rect P{view_.x + 0.5f * (view_.w - bw - 2 * pad), view_.y + 0.5f * (view_.h - h) + (1.0f - easeOut(in)) * S_(14), bw + 2 * pad, h};
    ui::shadow(P, S_(18), S_(28), ui::hex(0x000000, 0.45f * in));
    ui::panel(P, ui::withAlpha(theme::glass(), in), ui::withAlpha(theme::glassBorder(), in), S_(18), std::max(1.0f, S_(1)));
    float y = P.y + pad;
    ui::text(P.x + pad, y, st.busyTitle, titleT);
    y += ui::lineHeight(titleT) + S_(12);
    ui::text(P.x + pad, y, note, noteT);
    y += ui::lineHeight(noteT) + S_(30);
    const float x0 = P.x + pad, bh = S_(4);
    ui::rect(ui::Rect{x0, y, bw, bh}, ui::hex(0xFFFFFF, 0.12f * in), bh * 0.5f);
    ui::rect(ui::Rect{x0, y, std::max(bh, bw * busyShown_), bh}, accentOf(si, in), bh * 0.5f);
    {   // a glint travelling along the filled part (kept inside it): moving, so the wait reads as work
        const float fill = bw * busyShown_, g = float(std::fmod(now * 0.6, 1.0));
        const float gx0 = std::max(x0, x0 + fill * g - S_(40)), gx1 = std::min(x0 + fill, x0 + fill * g + S_(40));
        if (gx1 > gx0 + 1.0f) ui::rect(ui::Rect{gx0, y, gx1 - gx0, bh}, ui::hex(0xFFFFFF, 0.35f * in), bh * 0.5f);
    }
    y += bh + S_(18);
    std::string label = st.busyLabel;
    for (int k = 0; k < int(now * 2.5) % 4; ++k) label += ".";
    ui::text(x0, y, label, labelT);
    const std::string pct = std::to_string(int(std::lround(100.0f * busyShown_))) + " %";
    ui::text(x0 + bw - ui::measure(pct, pctT), y + 0.5f * (ui::lineHeight(labelT) - ui::lineHeight(pctT)), pct, pctT);
}

void App::drawFailed(double now) {
    const float W = float(fbw_), H = float(fbh_);
    ui::begin(fbw_, fbh_);
    ui::clear(ui::Color{0.04f, 0.04f, 0.05f, 1.0f});
    const float x0 = S_(150);
    float y = H * 0.5f - S_(120);
    ui::text(x0, y, L("Sahne açılamadı", "The scene could not open"), theme::caption(S_, ui::hex(0xE8806A)));
    y += S_(44);
    ui::text(x0, y, sceneIndex_ >= 0 ? infos_[sceneIndex_].title : std::string(), theme::display(S_, 72.0f));
    y += S_(112);
    ui::textWrapped(x0, y, W - 2 * x0, failure_, theme::body(S_), 1.5f, 6);
    keyHint(x0, H - S_(84), "Esc", L("Galeriye dön", "Back to the gallery"));
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

    // ---- bottom left: number, name, state; the scene's controls under it (titles in one column, their rows lined up);
    // a progress bar last
    const ui::TextStyle chipT = {ui::Font::SansMedium, S_(15.5f), theme::text(), 0.0f, false};
    const float row1 = S_(34);
    float w1 = 0.0f;
    {
        w1 += ui::measure(si.number, theme::mono(S_, 15.0f)) + S_(14);
        w1 += ui::measure(si.title, theme::caption(S_)) + S_(22);
        for (const Chip& c : st.chips) w1 += ui::measure(c.text, chipT) + S_(28) + S_(8);
    }
    const ui::TextStyle detailT = theme::small(S_);
    const ui::TextStyle titleT = theme::caption(S_, ui::withAlpha(theme::faint(), in));
    const ui::TextStyle valueT = theme::mono(S_, 17.0f, ui::withAlpha(theme::text(), in));
    const ui::TextStyle buttonT = {ui::Font::SansSemiBold, S_(18.0f), theme::text(), 0.0f, false};
    const float seg = S_(48), gap = S_(8), sliderH = S_(40), buttonH = S_(54), rowGap = S_(16), detailGap = S_(10);
    float titleW = 0.0f, contentW = 0.0f;
    for (const Control& c : st.controls)
        if (!c.title.empty()) titleW = std::max(titleW, ui::measure(c.title, titleT) + S_(22));
    // a choice's buttons: square for a digit or two (the magnitudes), as wide as the word otherwise (the buildings)
    auto itemStyle = [&](const Control& c, ui::Color col) {
        bool words = false;
        for (const std::string& it : c.items) words = words || it.size() > 2;
        return words ? ui::TextStyle{ui::Font::SansMedium, S_(17.0f), col, 0.0f, false} : theme::mono(S_, 20.0f, col);
    };
    auto itemWidth = [&](const Control& c, const std::string& item) {
        return std::max(seg, ui::measure(item, itemStyle(c, theme::text())) + S_(32));
    };
    auto choiceWidth = [&](const Control& c) {
        float w = 0.0f;
        for (const std::string& it : c.items) w += itemWidth(c, it) + gap;
        return c.items.empty() ? 0.0f : w - gap;
    };
    float trackW = S_(300);
    for (const Control& c : st.controls)
        if (c.kind == Control::Kind::Choice && c.items.size() > 3) trackW = std::max(trackW, choiceWidth(c) - S_(90));
    auto capsWidth = [&](const std::string& keys, float hh) { return keyCapsWidth(keys, hh); };
    for (const Control& c : st.controls) {
        if (c.kind == Control::Kind::Choice) contentW = std::max(contentW, choiceWidth(c));
        if (c.kind == Control::Kind::Slider) contentW = std::max(contentW, trackW + S_(22) + std::max(S_(70), ui::measure(c.text, valueT)));
        if (c.kind == Control::Kind::Button)
            contentW = std::max(contentW, S_(26) + ui::measure(c.text, buttonT) + S_(30) + capsWidth(c.keys, S_(28)) + S_(14));
    }
    float h2 = 0.0f, w2 = st.controls.empty() ? 0.0f : titleW + contentW;
    const float sectionGap = S_(20);   // a group's rule: space above and below it
    for (const Control& c : st.controls) {
        if (c.section) h2 += 2.0f * sectionGap;
        h2 += rowGap + (c.kind == Control::Kind::Choice ? seg : c.kind == Control::Kind::Slider ? sliderH : buttonH);
        if (!c.detail.empty()) {
            h2 += detailGap + ui::lineHeight(detailT);
            w2 = std::max(w2, titleW + ui::measure(c.detail, detailT));
        }
    }
    if (st.progress >= 0.0f) {
        w2 = std::max(w2, std::max(S_(420), ui::measure(st.progressLabel, detailT)));
        h2 += S_(20) + ui::lineHeight(detailT) + S_(10) + S_(4);
    }
    const float pw = std::max(w1, w2) + 2 * pad, ph = row1 + h2 + 2 * pad - S_(6);
    const ui::Rect P{m, H - m - ph + slide, pw, ph};
    panels_.push_back(P);
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
    const float cx0 = P.x + pad + titleW;   // where the controls' rows begin
    for (int ci = 0; ci < int(st.controls.size()); ++ci) {
        const Control& c = st.controls[ci];
        if (c.section) {
            y += sectionGap;
            ui::rect(ui::Rect{P.x + pad, y + rowGap * 0.5f, P.w - 2 * pad, std::max(1.0f, S_(1))}, ui::hex(0xFFFFFF, 0.08f * in));
            y += sectionGap;
        }
        y += rowGap;
        const float rowH = c.kind == Control::Kind::Choice ? seg : c.kind == Control::Kind::Slider ? sliderH : buttonH;
        if (!c.title.empty()) ui::text(P.x + pad, y + 0.5f * (rowH - ui::lineHeight(titleT)), c.title, titleT);
        const float mx = float(mouseX_), my = float(mouseY_);   // hover is drawn from where the mouse is now
        if (c.kind == Control::Kind::Choice) {
            float sx = cx0;
            for (int i = 0; i < int(c.items.size()); ++i) {
                const float iw = itemWidth(c, c.items[i]);
                const ui::Rect r{sx, y, iw, seg};
                const bool on = i == c.active, over = !mouseCaptured_ && r.contains(mx, my);
                if (on) {
                    ui::shadow(r, S_(12), S_(14), accentOf(si, 0.45f * in));
                    ui::rect(r, ui::mix(accentOf(si, in), ui::hex(0xFFFFFF, in), over ? 0.15f : 0.0f), S_(12));
                } else {
                    ui::panel(r, ui::hex(0xFFFFFF, (over ? 0.13f : 0.05f) * in), ui::hex(0xFFFFFF, (over ? 0.30f : 0.14f) * in), S_(12),
                              std::max(1.0f, S_(1)));
                }
                const ui::TextStyle it = itemStyle(c, on ? ui::hex(0x0B0B0C, in) : ui::withAlpha(theme::text(), (over ? 1.0f : 0.85f) * in));
                ui::text(r.x + 0.5f * (iw - ui::measure(c.items[i], it)), r.y + 0.5f * (seg - ui::lineHeight(it)), c.items[i], it);
                panelHits_.push_back({r, r, ci, i});
                sx += iw + gap;
            }
        } else if (c.kind == Control::Kind::Slider) {
            const float ty = y + 0.5f * sliderH, th = S_(4);
            const ui::Rect track{cx0, ty - 0.5f * th, trackW, th};
            const ui::Rect hit{cx0 - S_(14), y, trackW + S_(28), sliderH};
            const bool over = dragControl_ == ci || (!mouseCaptured_ && hit.contains(mx, my));
            const float v = std::min(1.0f, std::max(0.0f, c.value));
            ui::rect(track, ui::hex(0xFFFFFF, 0.14f * in), th * 0.5f);
            ui::rect(ui::Rect{track.x, track.y, std::max(th, trackW * v), th}, accentOf(si, in), th * 0.5f);
            if (c.mark >= 0.0f) {   // the notch: the default
                const float nx = track.x + trackW * std::min(1.0f, c.mark);
                ui::rect(ui::Rect{nx - S_(1.5f), ty - S_(9), S_(3), S_(18)}, ui::hex(0xFFFFFF, 0.55f * in), S_(1.5f));
            }
            const float hx = track.x + trackW * v, hr = over ? S_(12) : S_(10);
            ui::circle(hx, ty, hr + S_(5), accentOf(si, (over ? 0.35f : 0.20f) * in));
            ui::circle(hx, ty, hr, ui::withAlpha(theme::text(), in));
            ui::text(track.x + trackW + S_(22), y + 0.5f * (sliderH - ui::lineHeight(valueT)), c.text, valueT);
            panelHits_.push_back({hit, ui::Rect{track.x, y, trackW, sliderH}, ci, -1});
        } else {   // a button across the row
            const ui::Rect r{cx0, y, contentW, buttonH};
            const bool over = c.enabled && !mouseCaptured_ && r.contains(mx, my);
            const float a = (c.enabled ? 1.0f : 0.45f) * in;
            if (c.strong) {
                if (c.enabled) ui::shadow(r, buttonH * 0.5f, S_(16), accentOf(si, 0.30f * a));
                ui::rect(r, ui::mix(accentOf(si, a), ui::hex(0xFFFFFF, a), over ? 0.18f : 0.0f), buttonH * 0.5f);
            } else {
                ui::panel(r, ui::hex(0xFFFFFF, (over ? 0.14f : 0.07f) * a), ui::hex(0xFFFFFF, (over ? 0.32f : 0.18f) * a), buttonH * 0.5f,
                          std::max(1.0f, S_(1)));
            }
            ui::TextStyle bt = buttonT;
            bt.color = c.strong ? ui::hex(0x0B0B0C, a) : ui::withAlpha(theme::text(), a);
            ui::text(r.x + S_(26), r.y + 0.5f * (buttonH - ui::lineHeight(bt)), c.text, bt);
            if (!c.keys.empty()) {
                const float kw = capsWidth(c.keys, S_(28));
                keyCaps(r.x + r.w - S_(14) - kw, r.y + 0.5f * (buttonH - S_(28)), c.keys, S_(28), a, c.strong);
            }
            panelHits_.push_back({r, r, ci, 0});
        }
        y += rowH;
        if (!c.detail.empty()) {
            y += detailGap;
            ui::text(cx0, y, c.detail, theme::small(S_, ui::withAlpha(theme::muted(), in)));
            y += ui::lineHeight(detailT);
        }
    }
    if (st.progress >= 0.0f) {
        y += S_(20);
        ui::text(P.x + pad, y, st.progressLabel, theme::small(S_, ui::withAlpha(theme::text(), 0.9f * in)));
        y += ui::lineHeight(detailT) + S_(10);
        const float bw = P.w - 2 * pad;
        ui::rect(ui::Rect{P.x + pad, y, bw, S_(4)}, ui::hex(0xFFFFFF, 0.12f * in), S_(2));
        if (st.progressEndless) {   // no end: a stretch of light travels along the bar
            const float g = float(std::fmod(now * 0.45, 1.0)), len = 0.28f;
            const float x0 = std::max(0.0f, g * (1.0f + len) - len), x1 = std::min(1.0f, g * (1.0f + len));
            if (x1 > x0) ui::rect(ui::Rect{P.x + pad + bw * x0, y, bw * (x1 - x0), S_(4)}, accentOf(si, in), S_(2));
        } else {
            ui::rect(ui::Rect{P.x + pad, y, std::max(S_(4), bw * std::min(1.0f, st.progress)), S_(4)}, accentOf(si, in), S_(2));
        }
    }

    // ---- bottom right: help and the way back
    {
        const float hh = S_(30);
        float tw = 0.0f;
        const ui::TextStyle ht = theme::small(S_);
        auto hintW = [&](const std::string& keys, const std::string& what) { return keyCapsWidth(keys, hh) + S_(10) + ui::measure(what, ht); };
        const std::string kKeys = L("Tuşlar", "Keys"), kHide = L("Gizle", "Hide"), kGallery = L("Galeri", "Gallery");
        tw = hintW("F1", kKeys) + hintW("H", kHide) + hintW("Esc", kGallery) + 2 * S_(26);
        const ui::Rect R{W - m - tw - 2 * S_(18), H - m - hh - 2 * S_(16) + slide, tw + 2 * S_(18), hh + 2 * S_(16)};
        panels_.push_back(R);
        ui::panel(R, ui::withAlpha(theme::glass(), in), ui::withAlpha(theme::glassBorder(), in), S_(16), std::max(1.0f, S_(1)));
        float hx = R.x + S_(18);
        hx += keyHint(hx, R.y + S_(16), "F1", kKeys) + S_(26);
        hx += keyHint(hx, R.y + S_(16), "H", kHide) + S_(26);
        keyHint(hx, R.y + S_(16), "Esc", kGallery);
    }

    // ---- top right: how fast
    {
        const ui::TextStyle big = theme::mono(S_, 30.0f);
        const ui::TextStyle lab = theme::small(S_, theme::muted());
        const ui::TextStyle val = theme::mono(S_, 15.5f, theme::text());
        const std::string fpsS = std::to_string(int(std::lround(fps_)));
        float lw = 0.0f, vw = 0.0f;
        std::vector<Metric> rows = st.metrics;
        if (gpuW_ > 0.0f) rows.push_back({L("Ekran kartı", "GPU"), std::to_string(int(std::lround(gpuW_))) + " W · " + std::to_string(int(gpuC_)) + " °C"});
        if (gpuMemTotal_ > 0.0f) rows.push_back({L("Kart belleği", "GPU memory"), theme::num(gpuMemUsed_, 1) + " / " + theme::num(gpuMemTotal_, 1) + " GB"});
        for (const Metric& r : rows) { lw = std::max(lw, ui::measure(r.label, lab)); vw = std::max(vw, ui::measure(r.value, val)); }
        const float rowH = S_(28);
        const float w = std::max(lw + S_(24) + vw, ui::measure(fpsS, big) + S_(70)) + 2 * S_(20);
        const float h = S_(20) + ui::lineHeight(big) + S_(10) + rows.size() * rowH + S_(14);
        const ui::Rect R{W - m - w, m - slide, w, h};
        panels_.push_back(R);
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
    KeyGroup general{L("Genel", "General"),
                     {{"Esc", L("Galeriye dön", "Back to the gallery")},
                      {"F1", L("Bu yardımı aç / kapat", "This help on / off")},
                      {"H", L("Arayüzü gizle / göster", "Hide / show the interface")},
                      {"F12", L("Ekran görüntüsü al (captures/)", "Save a screenshot (captures/)")}}};
    if (si.reloadable) general.items.push_back({"F5", L("Ayarları ve GPU programlarını yeniden yükle", "Reload the settings and GPU programs")});
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
    const float rowH = S_(40), colW = S_(640), gapCol = S_(70), capGap = S_(22);
    // each column's caps as wide as its widest keys (right-aligned there), the words after them
    auto capColumn = [&](const std::vector<KeyGroup>& gs) {
        float w = S_(110);
        for (const KeyGroup& g : gs)
            for (const KeyHelp& k : g.items) w = std::max(w, keyCapsWidth(k.keys, S_(30)));
        return w;
    };
    auto colHeight = [&](const std::vector<KeyGroup>& gs) {
        float h = 0;
        for (const KeyGroup& g : gs) h += S_(44) + g.items.size() * rowH + S_(22);
        return h;
    };
    const bool sub = !si.subtitle.empty();
    const float headH = S_(150) + (sub ? S_(40) : 0.0f);
    const float bodyH = std::max(colHeight(left), colHeight(right));
    const float pw = 2 * colW + gapCol + 2 * S_(56), ph = S_(56) + headH + bodyH + S_(70);
    const ui::Rect P{0.5f * (W - pw), 0.5f * (H - ph), pw, ph};
    ui::shadow(P, S_(24), S_(60), ui::hex(0x000000, 0.7f));
    ui::panel(P, ui::hex(0x0E1013, 0.96f), theme::glassBorder(), S_(24), std::max(1.0f, S_(1)));
    float x = P.x + S_(56), y = P.y + S_(52);
    ui::text(x, y, si.number + L("  ·  tuşlar", "  ·  keys"), theme::caption(S_, accentText(si)));
    y += S_(40);
    ui::text(x - S_(2), y, si.title, theme::display(S_, 52.0f));
    y += S_(100);
    if (sub) {   // what the scene is about, under its name
        ui::text(x, y - S_(26), si.subtitle, theme::serif(S_, 24.0f, theme::muted()));
        y += S_(40);
    }
    ui::rect(ui::Rect{x, y - S_(18), P.w - 2 * S_(56), std::max(1.0f, S_(1))}, ui::hex(0xFFFFFF, 0.08f));
    auto column = [&](float cx, float cy, const std::vector<KeyGroup>& gs) {
        const float capCol = capColumn(gs), room = colW - capCol - capGap;
        for (const KeyGroup& g : gs) {
            ui::text(cx, cy, g.title, head);
            cy += S_(44);
            for (const KeyHelp& k : g.items) {
                keyCaps(cx + capCol - keyCapsWidth(k.keys, S_(30)), cy, k.keys, S_(30));
                ui::TextStyle d = desc;   // a long line a little smaller rather than into the next column
                if (const float w = ui::measure(k.what, d); w > room) d.size *= std::max(0.8f, room / w);
                ui::text(cx + capCol + capGap, cy + 0.5f * (S_(30) - ui::lineHeight(d)), k.what, d);
                cy += rowH;
            }
            cy += S_(22);
        }
    };
    column(x, y, left);
    column(x + colW + gapCol, y, right);
    const ui::TextStyle ft = theme::small(S_, theme::faint());
    const std::string foot = L("F1 ya da Esc ile kapanır", "F1 or Esc to close");
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
