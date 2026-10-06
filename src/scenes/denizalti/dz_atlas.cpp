#include "scenes/denizalti/dz_atlas.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace dz {

Atlas buildAtlas(Mesh& m, float texel, int maxWidth) {
    Atlas a;
    a.texel = texel;
    struct C {
        float2 lo{FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX};
        float tex = 0.0f;
        int w = 0, h = 0, x = 0, y = 0;
    };
    std::vector<C> ch(m.charts);
    for (const Vtx& v : m.v) {
        C& c = ch[v.chart];
        c.lo = make_float2(std::min(c.lo.x, v.uv.x), std::min(c.lo.y, v.uv.y));
        c.hi = make_float2(std::max(c.hi.x, v.uv.x), std::max(c.hi.y, v.uv.y));
    }
    for (C& c : ch) {
        if (c.lo.x > c.hi.x) {   // a chart with no vertex
            c.lo = c.hi = make_float2(0, 0);
        }
        const float sx = c.hi.x - c.lo.x, sy = c.hi.y - c.lo.y;
        // small parts are looked at closely: up to twice as fine
        c.tex = std::max(sx, sy) < 0.06f ? texel * 0.5f : texel;
        c.w = int(std::ceil(sx / c.tex)) + 1 + 2 * a.pad;
        c.h = int(std::ceil(sy / c.tex)) + 1 + 2 * a.pad;
        if (c.w > maxWidth) {   // too wide for the atlas: coarser
            c.tex = sx / float(maxWidth - 2 * a.pad - 2);
            c.w = int(std::ceil(sx / c.tex)) + 1 + 2 * a.pad;
            c.h = int(std::ceil(sy / c.tex)) + 1 + 2 * a.pad;
        }
    }
    // shelves: tallest first
    std::vector<int> order(ch.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    std::sort(order.begin(), order.end(), [&](int x, int y) { return ch[x].h != ch[y].h ? ch[x].h > ch[y].h : ch[x].w > ch[y].w; });
    int x = 0, y = 0, shelf = 0;
    for (int i : order) {
        C& c = ch[i];
        if (x + c.w > maxWidth) {
            y += shelf;
            x = 0;
            shelf = 0;
        }
        c.x = x;
        c.y = y;
        x += c.w;
        shelf = std::max(shelf, c.h);
    }
    a.width = maxWidth;
    a.height = y + shelf;
    for (Vtx& v : m.v) {
        const C& c = ch[v.chart];
        const float px = c.x + a.pad + 0.5f + (v.uv.x - c.lo.x) / c.tex;
        const float py = c.y + a.pad + 0.5f + (v.uv.y - c.lo.y) / c.tex;
        v.uv2 = make_float2(px / a.width, py / a.height);
    }
    return a;
}

Rasterised rasterise(const Mesh& m, const Atlas& a) {
    Rasterised r;
    r.index.assign(size_t(a.width) * a.height, -1);
    std::vector<float> slack(size_t(a.width) * a.height, FLT_MAX);   // how far outside its triangle a texel's centre was
    r.texels.reserve(size_t(a.width) * a.height / 2);
    for (size_t k = 0; k + 2 < m.idx.size(); k += 3) {
        const Vtx* v[3] = {&m.v[m.idx[k]], &m.v[m.idx[k + 1]], &m.v[m.idx[k + 2]]};
        float px[3], py[3];
        for (int i = 0; i < 3; ++i) {
            px[i] = v[i]->uv2.x * a.width;
            py[i] = v[i]->uv2.y * a.height;
        }
        const float area = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
        if (std::fabs(area) < 1e-12f) continue;   // degenerate (a fan's centre)
        const int x0 = std::max(0, int(std::floor(std::min({px[0], px[1], px[2]}) - 1.0f)));
        const int x1 = std::min(a.width - 1, int(std::ceil(std::max({px[0], px[1], px[2]}) + 1.0f)));
        const int y0 = std::max(0, int(std::floor(std::min({py[0], py[1], py[2]}) - 1.0f)));
        const int y1 = std::min(a.height - 1, int(std::ceil(std::max({py[0], py[1], py[2]}) + 1.0f)));
        // the edges' lengths in texels (for the distance outside an edge)
        float el[3];
        for (int i = 0; i < 3; ++i) {
            const int j = (i + 1) % 3;
            el[i] = std::hypot(px[j] - px[i], py[j] - py[i]);
        }
        for (int ty = y0; ty <= y1; ++ty)
            for (int tx = x0; tx <= x1; ++tx) {
                const float cx = tx + 0.5f, cy = ty + 0.5f;
                // barycentrics
                const float w0 = ((px[1] - cx) * (py[2] - cy) - (px[2] - cx) * (py[1] - cy)) / area;
                const float w1 = ((px[2] - cx) * (py[0] - cy) - (px[0] - cx) * (py[2] - cy)) / area;
                const float w2 = 1.0f - w0 - w1;
                // how far outside (texels): the most negative barycentric times its height
                float out = 0.0f;
                const float w[3] = {w0, w1, w2};
                for (int i = 0; i < 3; ++i)
                    if (w[i] < 0.0f) {
                        // the height over the edge opposite vertex i: 2*area / its length
                        const float len = el[(i + 1) % 3];
                        out = std::max(out, -w[i] * std::fabs(area) / std::max(len, 1e-6f));
                    }
                if (out > 0.75f) continue;
                const size_t ai = size_t(ty) * a.width + tx;
                if (out >= slack[ai]) continue;   // a triangle that holds it better already took it
                // clamped barycentrics: the nearest point of the triangle
                float b0 = std::max(w0, 0.0f), b1 = std::max(w1, 0.0f), b2 = std::max(w2, 0.0f);
                const float bs = b0 + b1 + b2;
                b0 /= bs;
                b1 /= bs;
                b2 /= bs;
                Texel t;
                t.p = v[0]->p * b0 + v[1]->p * b1 + v[2]->p * b2;
                t.n = normalize(v[0]->n * b0 + v[1]->n * b1 + v[2]->n * b2);
                t.xy = uint32_t(tx) | (uint32_t(ty) << 16);
                t.tri = uint32_t(k / 3);
                if (r.index[ai] < 0) {
                    r.index[ai] = int32_t(r.texels.size());
                    r.texels.push_back(t);
                } else {
                    r.texels[r.index[ai]] = t;
                }
                slack[ai] = out;
            }
    }
    return r;
}

void dilate(std::vector<float>& data, int channels, const Atlas& a, const std::vector<int32_t>& index) {
    if (channels > 32) return;
    std::vector<uint8_t> filled(size_t(a.width) * a.height, 0);
    for (size_t i = 0; i < filled.size(); ++i) filled[i] = index[i] >= 0;
    std::vector<size_t> add;
    std::vector<float> val;
    for (int pass = 0; pass < a.pad + 1; ++pass) {
        add.clear();
        val.clear();
        for (int y = 0; y < a.height; ++y)
            for (int x = 0; x < a.width; ++x) {
                const size_t i = size_t(y) * a.width + x;
                if (filled[i]) continue;
                float acc[32] = {};   // channels <= 32
                int n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if ((!dx && !dy) || xx < 0 || yy < 0 || xx >= a.width || yy >= a.height) continue;
                        const size_t j = size_t(yy) * a.width + xx;
                        if (!filled[j]) continue;
                        for (int c = 0; c < channels; ++c) acc[c] += data[j * channels + c];
                        ++n;
                    }
                if (!n) continue;
                add.push_back(i);
                for (int c = 0; c < channels; ++c) val.push_back(acc[c] / n);
            }
        for (size_t k = 0; k < add.size(); ++k) {
            filled[add[k]] = 1;
            for (int c = 0; c < channels; ++c) data[add[k] * channels + c] = val[k * channels + c];
        }
    }
}

}   // namespace dz
