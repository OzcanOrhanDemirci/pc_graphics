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
    bool straight = false;  // "over" compositing in straight alpha (the chart); else the ink lerps the colour (dials)
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
        if (straight) {
            const float na = a + p[3] * (1.0f - a);
            const float wk = a / na, wp = 1.0f - wk;
            p[0] = k.r * wk + p[0] * wp;
            p[1] = k.g * wk + p[1] * wp;
            p[2] = k.b * wk + p[2] * wp;
            p[3] = na;
            return;
        }
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

// ---- the sea chart: an Aegean coast at 1:50 000 (made up: a wrong copy of a real coast would read as a mistake),
// printed in buff, blue and black with a magenta compass rose, worked over in pencil on watch

uint32_t hashI(int x, int y, int s) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(s) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float hashF(int x, int y, int s) { return float(hashI(x, y, s) & 0xffffffu) / 16777215.0f; }
float vnoise(float x, float y, int s) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = hashF(xi, yi, s), b = hashF(xi + 1, yi, s), c = hashF(xi, yi + 1, s), d = hashF(xi + 1, yi + 1, s);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
float fbm2(float x, float y, int oct, int s) {
    float v = 0.0f, a = 0.5f, n = 0.0f;
    for (int o = 0; o < oct; ++o) {
        v += a * vnoise(x, y, s + o * 17);
        n += a;
        x = x * 2.03f + 7.1f;
        y = y * 2.03f + 3.7f;
        a *= 0.5f;
    }
    return v / n;
}

void drawChart(Canvas& cv, const Font& bold, const Font& reg) {
    const int W = cv.w, H = cv.h;
    const float ppm = W / 0.56f;   // pixels a metre of paper
    // the land: positive towards the upper right, a ragged coast, an island off it
    auto land = [&](float u, float v) {
        float f = u * 0.95f + (1.0f - v) * 0.55f - 1.0f;
        f += 0.24f * (fbm2(u * 3.0f, v * 3.0f, 3, 11) - 0.5f) + 0.07f * (fbm2(u * 14.0f, v * 14.0f, 3, 23) - 0.5f);
        f += 0.43f * std::exp(-((u - 0.34f) * (u - 0.34f) + (v - 0.4f) * (v - 0.4f) * 0.6f) / 0.0035f);
        f += 0.4f * std::exp(-((u - 0.25f) * (u - 0.25f) + (v - 0.47f) * (v - 0.47f)) / 0.0005f);
        return f;
    };
    // the field at a quarter of the resolution, read bilinearly (smooth contours, quick)
    const int fw = W / 4 + 2, fh = H / 4 + 2;
    std::vector<float> field(size_t(fw) * fh);
    for (int j = 0; j < fh; ++j)
        for (int i = 0; i < fw; ++i) field[size_t(j) * fw + i] = land((i * 4.0f) / W, (j * 4.0f) / H);
    auto F = [&](float x, float y) {
        const float gx = std::clamp(x / 4.0f, 0.0f, fw - 1.001f), gy = std::clamp(y / 4.0f, 0.0f, fh - 1.001f);
        const int i = int(gx), j = int(gy);
        const float tx = gx - i, ty = gy - j;
        const float* r0 = &field[size_t(j) * fw + i];
        const float* r1 = r0 + fw;
        return (r0[0] * (1 - tx) + r0[1] * tx) * (1 - ty) + (r1[0] * (1 - tx) + r1[1] * tx) * ty;
    };
    auto depth = [&](float f) { return f >= 0.0f ? 0.0f : 380.0f * std::pow(-f, 1.35f); };
    const Col buff{0.95f, 0.86f, 0.60f}, blue{0.68f, 0.82f, 0.93f}, pale{0.84f, 0.91f, 0.96f}, black{0.08f, 0.08f, 0.09f};
    const Col magenta{0.66f, 0.12f, 0.46f}, pencil{0.20f, 0.20f, 0.22f};
    const float contours[] = {2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f};
    const float mIn = 0.03f * ppm;   // the inner border (the neatline): the printed area ends there
    for (int y = int(mIn); y < H - int(mIn); ++y)
        for (int x = int(mIn); x < W - int(mIn); ++x) {
            const float f = F(x + 0.5f, y + 0.5f);
            const float gx = F(x + 1.5f, y + 0.5f) - F(x - 0.5f, y + 0.5f), gy = F(x + 0.5f, y + 1.5f) - F(x + 0.5f, y - 0.5f);
            const float g = std::max(0.5f * std::hypot(gx, gy), 1e-7f);   // per pixel
            const float onLand = std::clamp(f / g + 0.5f, 0.0f, 1.0f);
            const float d = depth(f);
            if (onLand > 0.0f) cv.blend(x, y, buff, 0.9f * onLand);
            if (onLand < 1.0f && f < 0.0f) {
                const float dd = std::max(380.0f * 1.35f * std::pow(-f, 0.35f) * g, 1e-6f);   // metres of depth a pixel
                const float in10 = std::clamp((10.0f - d) / dd + 0.5f, 0.0f, 1.0f), in20 = std::clamp((20.0f - d) / dd + 0.5f, 0.0f, 1.0f);
                if (in20 > in10) cv.blend(x, y, pale, 0.7f * (in20 - in10));
                if (in10 > 0.0f) cv.blend(x, y, blue, 0.8f * in10 * (1.0f - onLand));
            }
            // the coast, heavier; the depth contours, fine (the depth's own gradient in metres a pixel)
            cv.blend(x, y, black, std::clamp(1.4f - std::fabs(f) / g + 0.5f, 0.0f, 1.0f));
            if (f > 0.0f)
                for (float c : {0.035f, 0.075f, 0.12f, 0.17f})
                    cv.blend(x, y, Col{0.55f, 0.38f, 0.2f}, 0.6f * std::clamp(0.45f - std::fabs(f - c) / g + 0.5f, 0.0f, 1.0f));
            if (f < 0.0f) {
                const float dd = 380.0f * 1.35f * std::pow(-f, 0.35f) * g;
                for (float c : contours) {
                    const float cov = std::clamp(0.55f - std::fabs(d - c) / std::max(dd, 1e-6f) + 0.5f, 0.0f, 1.0f);
                    if (cov > 0.0f) cv.blend(x, y, Col{0.25f, 0.42f, 0.6f}, cov * 0.9f);
                }
            }
        }
    // the graticule every 5' (latitude 926 px, longitude at 38 degrees 730 px), the border's minute bars, the labels
    const float m = 0.03f * ppm, latStep = 0.1852f * ppm, lonStep = 0.1460f * ppm;
    const float x0 = m, x1 = W - m, y0 = m, y1 = H - m;
    for (float x = x0 + 0.37f * lonStep; x < x1; x += lonStep) cv.line(x, y0, x, y1, 0.7f, black);
    for (float y = y1 - 0.29f * latStep; y > y0; y -= latStep) cv.line(x0, y, x1, y, 0.7f, black);
    for (float t : {0.0f, 0.004f * ppm}) {
        cv.line(x0 - t, y0 - t, x1 + t, y0 - t, 1.1f, black);
        cv.line(x0 - t, y1 + t, x1 + t, y1 + t, 1.1f, black);
        cv.line(x0 - t, y0 - t, x0 - t, y1 + t, 1.1f, black);
        cv.line(x1 + t, y0 - t, x1 + t, y1 + t, 1.1f, black);
    }
    {
        const float bw = 0.004f * ppm;
        int k = 0;
        for (float x = x0 + 0.37f * lonStep - 5.0f * lonStep / 5.0f * 1.0f; x < x1; x += lonStep / 5.0f, ++k)
            if (k % 2 && x > x0 && x + lonStep / 5.0f < x1)
                for (float t = 0.0f; t < bw; t += 1.0f) {
                    cv.line(x, y0 - bw + t + 0.5f, x + lonStep / 5.0f, y0 - bw + t + 0.5f, 0.6f, black);
                    cv.line(x, y1 + t + 0.5f, x + lonStep / 5.0f, y1 + t + 0.5f, 0.6f, black);
                }
        k = 0;
        for (float y = y1 - 0.29f * latStep + latStep; y > y0; y -= latStep / 5.0f, ++k)
            if (k % 2 && y < y1 && y - latStep / 5.0f > y0)
                for (float t = 0.0f; t < bw; t += 1.0f) {
                    cv.line(x0 - bw + t + 0.5f, y, x0 - bw + t + 0.5f, y - latStep / 5.0f, 0.6f, black);
                    cv.line(x1 + t + 0.5f, y, x1 + t + 0.5f, y - latStep / 5.0f, 0.6f, black);
                }
    }
    {
        int i = 0;
        for (float x = x0 + 0.37f * lonStep; x < x1; x += lonStep, ++i) {
            char b[32];
            std::snprintf(b, sizeof(b), "%02d'", 35 + 5 * i);
            text(cv, reg, b, 0.0045f * ppm, x, y1 + 0.021f * ppm, black);
        }
        i = 0;
        for (float y = y1 - 0.29f * latStep; y > y0 + 0.02f * ppm; y -= latStep, ++i) {
            char b[32];
            std::snprintf(b, sizeof(b), "%02d'", 10 + 5 * i);
            text(cv, reg, b, 0.0045f * ppm, x0 + 0.032f * ppm, y - 0.006f * ppm, black);
        }
    }
    // the title, on the land
    {
        const float cx = W * 0.74f, cy = H * 0.12f;
        text(cv, bold, "EGE DEN\xC4\xB0Z\xC4\xB0", 0.016f * ppm, cx, cy, black, 0.003f * ppm);
        text(cv, reg, "SEY\xC4\xB0R HAR\xC4\xB0TASI", 0.0085f * ppm, cx, cy + 0.024f * ppm, black, 0.0012f * ppm);
        text(cv, reg, "\xC3\x96L\xC3\x87" "EK 1 : 50 000", 0.0055f * ppm, cx, cy + 0.04f * ppm, black);
        text(cv, reg, "DER\xC4\xB0NL\xC4\xB0KLER METRE \xC4\xB0LE", 0.0055f * ppm, cx, cy + 0.052f * ppm, black);
        text(cv, reg, "MERCATOR PROJEKS\xC4\xB0YONU", 0.0045f * ppm, cx, cy + 0.063f * ppm, black);
    }
    // the compass rose, magenta: true outside, magnetic inside (variation 5 degrees east)
    const float rcx = W * 0.27f, rcy = H * 0.74f, R = 0.068f * ppm;
    auto rose = [&](float r, float rot, bool numbers) {
        cv.ring(rcx, rcy, r - 1.0f, r + 1.0f, magenta);
        for (int a = 0; a < 360; ++a) {
            const float th = (a + rot) * kPi / 180.0f;
            const float len = a % 10 == 0 ? 0.11f * r : (a % 5 == 0 ? 0.07f * r : 0.035f * r);
            const float s = std::sin(th), c = -std::cos(th);
            cv.line(rcx + s * r, rcy + c * r, rcx + s * (r - len), rcy + c * (r - len), a % 10 == 0 ? 1.0f : 0.6f, magenta);
            if (numbers && a % 30 == 0) {
                char b[8];
                std::snprintf(b, sizeof(b), "%d", a);
                text(cv, reg, b, 0.11f * r, rcx + s * (r - 0.2f * r), rcy + c * (r - 0.2f * r), magenta);
            }
        }
    };
    rose(R, 0.0f, true);
    rose(R * 0.68f, 2.5f, true);
    cv.ring(rcx, rcy, R * 0.43f - 0.8f, R * 0.43f + 0.8f, magenta);
    for (int i = 0; i < 4; ++i) {   // the star at north and the arms
        const float th = i * 0.5f * kPi;
        const float s = std::sin(th), c = -std::cos(th);
        cv.line(rcx, rcy, rcx + s * R * 0.4f, rcy + c * R * 0.4f, i == 0 ? 2.2f : 1.0f, magenta);
    }
    text(cv, reg, "VAR. 2\xC2\xB0" "30'E (1968)", 0.0042f * ppm, rcx, rcy + 0.2f * R, magenta);
    // soundings on a jittered grid in the water, clear of the rose and the title
    {
        const float step = 0.017f * ppm;
        int n = 0;
        for (float y = y0 + step; y < y1 - step * 0.5f; y += step)
            for (float x = x0 + step; x < x1 - step * 0.5f; x += step, ++n) {
                const float jx = x + (hashF(int(x), int(y), 5) - 0.5f) * step * 0.7f, jy = y + (hashF(int(x), int(y), 6) - 0.5f) * step * 0.7f;
                const float f = F(jx, jy);
                if (f > -0.004f) continue;
                if (std::hypot(jx - rcx, jy - rcy) < R * 1.15f) continue;
                const float d = depth(f);
                char b[16];
                std::snprintf(b, sizeof(b), "%d", int(d));
                text(cv, reg, b, 0.0024f * ppm, jx, jy, black);
            }
        (void)n;
    }
    // rocks awash (+ with dots) and sunken (+) off the coast, a wreck, the bottom's quality here and there
    {
        int placed = 0;
        for (int k = 0; k < 4000 && placed < 26; ++k) {
            const float x = x0 + hashF(k, 1, 91) * (x1 - x0), y = y0 + hashF(k, 2, 91) * (y1 - y0);
            const float f = F(x, y);
            if (f > -0.003f || f < -0.035f) continue;
            const float s = 0.0022f * ppm;
            cv.line(x - s, y, x + s, y, 0.9f, black);
            cv.line(x, y - s, x, y + s, 0.9f, black);
            if (placed % 3 == 0)
                for (int q = 0; q < 4; ++q) cv.ring(x + (q % 2 ? 0.6f : -0.6f) * s, y + (q < 2 ? 0.6f : -0.6f) * s, 0.0f, 1.3f, black);
            ++placed;
        }
        const float wx = W * 0.47f, wy = H * 0.3f;   // a wreck: the hull's line with three masts, "Wk"
        cv.line(wx - 0.006f * ppm, wy, wx + 0.006f * ppm, wy, 1.0f, black);
        for (int q = -1; q <= 1; ++q) cv.line(wx + q * 0.003f * ppm, wy - 0.0025f * ppm, wx + q * 0.003f * ppm, wy + 0.0025f * ppm, 0.9f, black);
        text(cv, reg, "Wk", 0.0026f * ppm, wx, wy + 0.006f * ppm, black);
        const char* q[] = {"S", "M", "Sh", "S.M", "R", "fS"};
        for (int k = 0, n = 0; k < 600 && n < 18; ++k) {
            const float x = x0 + hashF(k, 3, 93) * (x1 - x0), y = y0 + hashF(k, 4, 93) * (y1 - y0);
            if (F(x, y) > -0.01f || std::hypot(x - rcx, y - rcy) < R * 1.2f) continue;
            text(cv, reg, q[n % 6], 0.0022f * ppm, x, y, black);
            ++n;
        }
    }
    // a lighthouse on the island's cape: the magenta flare and its characteristics
    {
        float bx = W * 0.34f, by = H * 0.4f;
        for (int k = 0; k < 400; ++k) {   // walk out to the coast from the island's middle (to the south west)
            if (F(bx, by) < 0.0f) break;
            bx -= 1.4f;
            by += 1.6f;
        }
        cv.line(bx, by, bx + 0.008f * ppm, by - 0.016f * ppm, 2.6f, magenta);
        cv.ring(bx, by, 0.0f, 3.0f, black);
        text(cv, reg, "Fl(2)10s 21m 12M", 0.0042f * ppm, bx - 0.03f * ppm, by + 0.008f * ppm, black);
    }
    // the watch's pencil: the course line, fixes in circles with their times, a dead-reckoning half circle; an older
    // track rubbed out
    {
        const float px[] = {0.12f, 0.2f, 0.29f, 0.4f, 0.52f, 0.6f}, py[] = {0.94f, 0.84f, 0.73f, 0.6f, 0.52f, 0.47f};
        const char* times[] = {"0352", "0410", "0428", "0446", "0504", ""};
        for (int i = 0; i + 1 < 6; ++i) cv.line(px[i] * W, py[i] * H, px[i + 1] * W, py[i + 1] * H, 1.1f, pencil);
        for (int i = 0; i < 5; ++i) {
            const float cx = px[i] * W, cy = py[i] * H;
            cv.ring(cx, cy, 13.0f, 15.0f, pencil);
            cv.ring(cx, cy, 0.0f, 2.2f, pencil);
            text(cv, reg, times[i], 0.0045f * ppm, cx + 0.02f * ppm, cy - 0.004f * ppm, pencil);
        }
        // the half circle of a dead-reckoning position ahead, its time
        {
            const float cx = px[5] * W, cy = py[5] * H;
            for (int a = 0; a < 18; ++a) {
                const float t0 = kPi * a / 18.0f, t1 = kPi * (a + 1) / 18.0f;
                cv.line(cx + 14.0f * std::cos(t0), cy - 14.0f * std::sin(t0), cx + 14.0f * std::cos(t1), cy - 14.0f * std::sin(t1), 0.9f, pencil);
            }
            text(cv, reg, "0520 DR", 0.0045f * ppm, cx + 0.024f * ppm, cy - 0.006f * ppm, pencil);
        }
        text(cv, reg, "C 047  S 8", 0.0055f * ppm, 0.3f * W + 0.04f * ppm, 0.66f * H, pencil, 0.001f * ppm);
        cv.line(0.08f * W, 0.7f * H, 0.33f * W, 0.42f * H, 1.0f, Col{0.45f, 0.45f, 0.45f});   // rubbed out: a ghost
    }
    // a coffee ring by the edge, a crease where the chart was folded
    {
        const float cx = W * 0.9f, cy = H * 0.28f, r = 0.039f * ppm;
        for (int y = int(cy - r - 12); y <= int(cy + r + 12); ++y)
            for (int x = int(cx - r - 12); x <= int(cx + r + 12); ++x) {
                const float d = std::hypot(x - cx, y - cy);
                const float w = 3.0f + 4.0f * vnoise(std::atan2(y - cy, x - cx) * 3.0f + 10.0f, 0.5f, 77);
                const float a = std::clamp(1.0f - std::fabs(d - r) / w, 0.0f, 1.0f) * 0.35f + (d < r ? 0.05f : 0.0f);
                cv.blend(x, y, Col{0.48f, 0.30f, 0.15f}, a);
            }
        for (int x = 0; x < W; ++x) cv.blend(x, H / 2, Col{0.5f, 0.48f, 0.42f}, 0.12f);
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
            const float pxPerM = s.style == PLATE_CHART ? 5000.0f : 6000.0f;   // ~0.17 mm a pixel (the chart 0.2 mm)
            items[i] = {std::min(4096, std::max(64, int(s.w * pxPerM))), std::min(s.style == PLATE_CHART ? 4096 : 2048, std::max(32, int(s.h * pxPerM)))};
        }
    }
    // shelf packing, tallest first
    std::vector<int> order(specs.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return items[a].h > items[b].h; });
    std::vector<std::pair<int, int>> pos(specs.size());
    int x = 0, y = 0, shelf = 0;
    const int pad = 24;   // mips to 4 stay clear of the neighbours
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
        Canvas cv(items[i].w, items[i].h, s.style == PLATE_CHART && s.kind < 0 ? Col{0.906f, 0.898f, 0.855f} : bg);
        cv.straight = s.style == PLATE_CHART && s.kind < 0;
        if (s.kind >= 0) drawDial(cv, s.kind, bold, reg);
        else if (s.style == PLATE_CHART) drawChart(cv, bold, reg);
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
