#include "scenes/denizalti/dz_decal.h"

#include "stb_truetype.h"   // the implementation is compiled in the interface kit (ui.cpp)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

namespace dz {

Decals* activeDecals = nullptr;

int Decals::dial(DialKind k) {
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].kind == int(k)) return int(i);
    DecalSpec s;
    s.kind = int(k);
    specs.push_back(s);
    return int(specs.size()) - 1;
}

int Decals::plate(const std::string& text, float w, float h, int style) {
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].kind < 0 && specs[i].text == text && specs[i].style == style && std::fabs(specs[i].w - w) < 1e-4f && std::fabs(specs[i].h - h) < 1e-4f)
            return int(i);
    DecalSpec s;
    s.text = text;
    s.style = style;
    s.w = w;
    s.h = h;
    specs.push_back(s);
    return int(specs.size()) - 1;
}

namespace {

const float kPi = 3.14159265358979f;

struct Col {
    float r, g, b;
};

// a canvas in linear coverage, colours sRGB 0..1; a: ink (what the shader may raise or cut)
struct Canvas {
    int w, h;
    std::vector<float> c;   // r, g, b, ink
    Canvas(int W, int H, Col bg) : w(W), h(H), c(size_t(W) * H * 4) {
        for (size_t i = 0; i < size_t(W) * H; ++i) {
            c[i * 4] = bg.r;
            c[i * 4 + 1] = bg.g;
            c[i * 4 + 2] = bg.b;
            c[i * 4 + 3] = 0.0f;
        }
    }
    void blend(int x, int y, Col k, float a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.0f) return;
        a = std::min(a, 1.0f);
        float* p = &c[(size_t(y) * w + x) * 4];
        p[0] += (k.r - p[0]) * a;
        p[1] += (k.g - p[1]) * a;
        p[2] += (k.b - p[2]) * a;
        p[3] = std::max(p[3], a);
    }
    // a thick segment, its ends round
    void line(float x0, float y0, float x1, float y1, float hw, Col k) {
        const int bx0 = int(std::floor(std::min(x0, x1) - hw - 1)), bx1 = int(std::ceil(std::max(x0, x1) + hw + 1));
        const int by0 = int(std::floor(std::min(y0, y1) - hw - 1)), by1 = int(std::ceil(std::max(y0, y1) + hw + 1));
        const float dx = x1 - x0, dy = y1 - y0, L2 = std::max(dx * dx + dy * dy, 1e-6f);
        for (int y = by0; y <= by1; ++y)
            for (int x = bx0; x <= bx1; ++x) {
                const float px = x + 0.5f - x0, py = y + 0.5f - y0;
                const float t = std::clamp((px * dx + py * dy) / L2, 0.0f, 1.0f);
                const float ex = px - dx * t, ey = py - dy * t;
                const float d = std::sqrt(ex * ex + ey * ey);
                blend(x, y, k, std::clamp(hw - d + 0.5f, 0.0f, 1.0f));
            }
    }
    void ring(float cx, float cy, float r0, float r1, Col k) {
        for (int y = int(cy - r1 - 2); y <= int(cy + r1 + 2); ++y)
            for (int x = int(cx - r1 - 2); x <= int(cx + r1 + 2); ++x) {
                const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                blend(x, y, k, std::clamp(std::min(d - r0, r1 - d) + 0.5f, 0.0f, 1.0f));
            }
    }
};

struct Font {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)) != 0;
    }
};

// UTF-8 to code points
std::vector<int> decode(const std::string& s) {
    std::vector<int> o;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        int cp = c, n = 1;
        if (c >= 0xF0) { cp = c & 0x07; n = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3F);
        o.push_back(cp);
        i += n;
    }
    return o;
}

float textWidth(const Font& f, const std::vector<int>& cps, float px, float tracking) {
    const float sc = stbtt_ScaleForPixelHeight(&f.info, px);
    float x = 0.0f;
    for (size_t i = 0; i < cps.size(); ++i) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&f.info, cps[i], &adv, &lsb);
        x += adv * sc + tracking;
        if (i + 1 < cps.size()) x += stbtt_GetCodepointKernAdvance(&f.info, cps[i], cps[i + 1]) * sc;
    }
    return x - tracking;
}

// text centred at (cx, cy) (the cap height's middle), px tall
void text(Canvas& cv, const Font& f, const std::string& s, float px, float cx, float cy, Col k, float tracking = 0.0f) {
    const std::vector<int> cps = decode(s);
    const float sc = stbtt_ScaleForPixelHeight(&f.info, px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f.info, &asc, &desc, &gap);
    const float capMid = 0.36f * px;   // roughly half the cap height above the baseline
    float x = cx - 0.5f * textWidth(f, cps, px, tracking);
    const float base = cy + capMid;
    for (size_t i = 0; i < cps.size(); ++i) {
        const float fx = x - std::floor(x);
        int bw, bh, ox, oy;
        unsigned char* bmp = stbtt_GetCodepointBitmapSubpixel(&f.info, sc, sc, fx, 0.0f, cps[i], &bw, &bh, &ox, &oy);
        if (bmp) {
            for (int yy = 0; yy < bh; ++yy)
                for (int xx = 0; xx < bw; ++xx)
                    cv.blend(int(std::floor(x)) + ox + xx, int(std::floor(base)) + oy + yy, k, bmp[yy * bw + xx] / 255.0f);
            stbtt_FreeBitmap(bmp, nullptr);
        }
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&f.info, cps[i], &adv, &lsb);
        x += adv * sc + tracking;
        if (i + 1 < cps.size()) x += stbtt_GetCodepointKernAdvance(&f.info, cps[i], cps[i + 1]) * sc;
    }
}

struct DialDef {
    Col face, ink;
    float vmin, vmax, sweep, start;   // degrees: start at the minimum, clockwise sweep
    float major, minor, numbers;      // tick spacing in value units, numerals every
    std::string unit, caption, maker;
    bool compass = false;
};

DialDef dialDef(int k) {
    const Col black{0.035f, 0.035f, 0.035f}, cream{0.93f, 0.90f, 0.80f}, white{0.92f, 0.92f, 0.89f}, ink{0.05f, 0.05f, 0.05f};
    switch (k) {
    case DIAL_DEPTH16: return {black, cream, 0, 165, 300, 210, 10, 2, 20, "FEET", "SALT WATER DEPTH TO KEEL", "SP. GR. OF SEA WATER 1.025"};
    case DIAL_DEPTH_DEEP: return {black, cream, 0, 600, 300, 210, 50, 10, 100, "FEET", "DEEP DEPTH GAUGE", ""};
    case DIAL_AIR: return {black, cream, 0, 4000, 270, 225, 500, 100, 1000, "LBS. PER SQ. IN.", "", "ASHCROFT"};
    case DIAL_WHITE: return {white, ink, 0, 3000, 270, 225, 250, 50, 500, "P.S.I.", "", "U.S. GAUGE"};
    case DIAL_SMALL: return {white, ink, 0, 300, 270, 225, 50, 10, 50, "P.S.I.", "", ""};
    case DIAL_METER: return {white, ink, 0, 150, 90, 315, 25, 5, 50, "AMPERES", "D.C.", "WESTON"};
    default: {
        DialDef d{black, cream, 0, 360, 360, 0, 10, 2, 30, "", "", ""};
        d.compass = true;
        return d;
    }
    }
}

void drawDial(Canvas& cv, int kind, const Font& bold, const Font& reg) {
    const DialDef d = dialDef(kind);
    const float S = float(cv.w), c = 0.5f * S, R = 0.5f * S;
    // a thin outer ring and an inner one where the scale sits
    cv.ring(c, c, R * 0.955f, R * 0.975f, d.ink);
    const auto ang = [&](float v) { return (d.start - d.sweep * (v - d.vmin) / (d.vmax - d.vmin)) * kPi / 180.0f; };
    // ticks: minor, major (longer, heavier)
    const int nMinor = int(std::round((d.vmax - d.vmin) / d.minor));
    for (int i = 0; i <= nMinor; ++i) {
        const float v = d.vmin + i * d.minor;
        if (d.compass && v >= 360.0f) break;
        const bool major = std::fabs(std::fmod(v - d.vmin + 1e-3f, d.major)) < 1e-2f;
        const float a = ang(v);
        const float r1 = R * 0.93f, r0 = major ? R * 0.83f : R * 0.885f;
        const float ca = std::cos(a), sa = -std::sin(a);   // image y down
        cv.line(c + ca * r0, c + sa * r0, c + ca * r1, c + sa * r1, major ? R * 0.0085f : R * 0.0045f, d.ink);
    }
    // numerals
    const int nNum = int(std::round((d.vmax - d.vmin) / d.numbers));
    for (int i = 0; i <= nNum; ++i) {
        const float v = d.vmin + i * d.numbers;
        if (d.compass && v >= 360.0f) break;
        const float a = ang(v);
        const float rr = R * 0.7f;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", d.compass ? int(v) / 10 : int(v));
        const float px = d.vmax >= 1000 ? R * 0.1f : R * 0.13f;
        text(cv, bold, buf, px, c + std::cos(a) * rr, c - std::sin(a) * rr, d.ink);
    }
    if (!d.unit.empty()) text(cv, bold, d.unit, R * 0.065f, c, c + R * 0.3f, d.ink, R * 0.004f);
    if (!d.caption.empty()) text(cv, reg, d.caption, R * 0.052f, c, c + R * 0.42f, d.ink, R * 0.003f);
    if (!d.maker.empty()) text(cv, reg, d.maker, R * 0.05f, c, c - R * 0.32f, d.ink, R * 0.006f);
}

void drawPlate(Canvas& cv, const DecalSpec& s, const Font& bold) {
    // lines of text filling the plate, upper case engraving
    std::vector<std::string> lines;
    {
        std::stringstream ss(s.text);
        std::string l;
        while (std::getline(ss, l, '\n')) lines.push_back(l);
    }
    const Col ink = s.style == PLATE_BLACK ? Col{0.92f, 0.92f, 0.9f} : (s.style == PLATE_BRASS ? Col{0.04f, 0.035f, 0.03f} : Col{0.05f, 0.05f, 0.05f});
    const float H = float(cv.h), W = float(cv.w);
    const float lineH = H / (lines.size() + 0.6f);
    for (size_t i = 0; i < lines.size(); ++i) {
        float px = lineH * 0.62f;
        const float tw = textWidth(bold, decode(lines[i]), px, px * 0.08f);
        if (tw > W * 0.9f) px *= W * 0.9f / tw;
        text(cv, bold, lines[i], px, 0.5f * W, (i + 0.8f) * lineH, ink, px * 0.08f);
    }
    // the plate's rim (a fine bevel line) for real plates
    if (s.style != PLATE_STENCIL) {
        const Col rim = s.style == PLATE_BLACK ? Col{0.25f, 0.25f, 0.25f} : Col{0.5f, 0.42f, 0.25f};
        const float m = H * 0.04f;
        cv.line(m, m, W - m, m, H * 0.006f, rim);
        cv.line(m, H - m, W - m, H - m, H * 0.006f, rim);
        cv.line(m, m, m, H - m, H * 0.006f, rim);
        cv.line(W - m, m, W - m, H - m, H * 0.006f, rim);
    }
}

}   // namespace

bool Decals::draw(const std::string& root, std::string& err) {
    Font bold, reg;
    if (!bold.load(root + "/assets/fonts/IBMPlexSans-SemiBold.ttf") || !reg.load(root + "/assets/fonts/IBMPlexSans-Medium.ttf")) {
        err = "fontlar okunamadi";
        return false;
    }
    // sizes: the big depth gauges at 2048 px (0.2 mm a pixel on a 40 cm face), the rest 768-1024; plates by their size
    struct Item {
        int w, h;
    };
    std::vector<Item> items(specs.size());
    for (size_t i = 0; i < specs.size(); ++i) {
        const DecalSpec& s = specs[i];
        if (s.kind >= 0) {
            const int S = s.kind == DIAL_DEPTH16 ? 2048 : (s.kind == DIAL_DEPTH_DEEP || s.kind == DIAL_COMPASS ? 1024 : 768);
            items[i] = {S, S};
        } else {
            const float pxPerM = 6000.0f;   // ~0.17 mm a pixel
            items[i] = {std::min(4096, std::max(64, int(s.w * pxPerM))), std::min(2048, std::max(32, int(s.h * pxPerM)))};
        }
    }
    // shelf packing, tallest first
    std::vector<int> order(specs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return items[a].h > items[b].h; });
    std::vector<std::pair<int, int>> pos(specs.size());
    int x = 0, y = 0, shelf = 0;
    const int pad = 8;
    for (int i : order) {
        if (x + items[i].w + pad > width) {
            y += shelf + pad;
            x = 0;
            shelf = 0;
        }
        pos[i] = {x, y};
        x += items[i].w + pad;
        shelf = std::max(shelf, items[i].h);
    }
    height = std::max(64, y + shelf);
    rgba.assign(size_t(width) * height * 4, 0);
    entries.resize(specs.size());
    for (size_t i = 0; i < specs.size(); ++i) {
        const DecalSpec& s = specs[i];
        const Col bg = s.kind >= 0 ? dialDef(s.kind).face
                                   : (s.style == PLATE_BLACK ? Col{0.03f, 0.03f, 0.03f} : (s.style == PLATE_BRASS ? Col{0.80f, 0.66f, 0.36f} : Col{0, 0, 0}));
        Canvas cv(items[i].w, items[i].h, bg);
        if (s.kind >= 0) drawDial(cv, s.kind, bold, reg);
        else drawPlate(cv, s, bold);
        for (int yy = 0; yy < cv.h; ++yy)
            for (int xx = 0; xx < cv.w; ++xx) {
                const float* p = &cv.c[(size_t(yy) * cv.w + xx) * 4];
                uint8_t* o = &rgba[((size_t(pos[i].second) + yy) * width + pos[i].first + xx) * 4];
                for (int k = 0; k < 4; ++k) o[k] = uint8_t(std::clamp(p[k], 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        DecalEntry e{};
        e.u0 = float(pos[i].first) / width;
        e.v0 = float(pos[i].second) / height;
        e.u1 = float(pos[i].first + items[i].w) / width;
        e.v1 = float(pos[i].second + items[i].h) / height;
        e.halfW = s.kind >= 0 ? 0.0f : 0.5f * s.w;
        e.halfH = s.kind >= 0 ? 0.0f : 0.5f * s.h;
        e.mode = s.kind >= 0 ? 0 : 1;
        e.style = s.style;
        entries[i] = e;
    }
    return true;
}

}   // namespace dz
