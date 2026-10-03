// Scene 5 "Koy": the fixed sub-pixel sample patterns and the reconstruction filter, shared by the host (patterns, the
// filter's weight sums) and the device (koy.cu). Netlik before everything (docs/koy/PLAN.md §5.1): the same samples
// every frame, no jitter, no history; a pixel on an edge gets the rest of a 32-sample pattern.
#pragma once
#include "shared/vec.h"

// Reconstruction filter along one axis, x in pixels from the output pixel's centre.
//   0 box (inside the pixel), 1 Blackman-Harris over [-r, r], 2 Mitchell-Netravali (B = C = 1/3) scaled to r,
//   3 Gaussian (sigma r/3, cut at r)
HD float koyFilter(int kind, float r, float x) {
    x = fabsf(x);
    if (kind == 0) return x < 0.5f ? 1.0f : 0.0f;
    if (x >= r) return 0.0f;
    if (kind == 1) {
        const float t = 0.5f + 0.5f * x / r;
        const float a = 2.0f * M_PIf * t;
        return 0.35875f - 0.48829f * cosf(a) + 0.14128f * cosf(2.0f * a) - 0.01168f * cosf(3.0f * a);
    }
    if (kind == 2) {
        const float u = x * 2.0f / r;
        const float B = 1.0f / 3.0f, C = 1.0f / 3.0f;
        if (u < 1.0f) return ((12.0f - 9.0f * B - 6.0f * C) * u * u * u + (-18.0f + 12.0f * B + 6.0f * C) * u * u + (6.0f - 2.0f * B)) / 6.0f;
        return ((-B - 6.0f * C) * u * u * u + (6.0f * B + 30.0f * C) * u * u + (-12.0f * B - 48.0f * C) * u + (8.0f * B + 24.0f * C)) / 6.0f;
    }
    const float sg = r / 3.0f;
    return expf(-0.5f * x * x / (sg * sg)) - expf(-4.5f);
}

#if !defined(__CUDACC_RTC__)
#include <cmath>
#include <vector>

// Base patterns: the rotated grid for 4 (RGSS), Direct3D's standard MSAA patterns for 8 and 16 (well stratified).
inline std::vector<float2> koyBasePattern(int n) {
    auto p = [](float x, float y) { return make_float2(0.5f + x / 16.0f, 0.5f + y / 16.0f); };
    if (n <= 1) return {make_float2(0.5f, 0.5f)};
    if (n <= 4) return {p(-2, -6), p(6, -2), p(-6, 2), p(2, 6)};
    if (n <= 8) return {p(1, -3), p(-1, 3), p(5, 1), p(-3, -5), p(-5, 5), p(-7, -1), p(3, 7), p(7, -7)};
    return {p(1, 1), p(-1, -3), p(-3, 2), p(4, -1), p(-5, -2), p(2, 5), p(5, 3), p(3, -5),
            p(-2, 6), p(0, -7), p(-4, -6), p(-6, 4), p(-8, 0), p(7, -4), p(6, 7), p(-7, -8)};
}
// The edge pattern: the base pattern, then points added one by one where they are farthest from all the others
// (toroidal distance: neighbouring pixels' patterns meet without clumps). Candidates from a Halton sequence: deterministic.
inline std::vector<float2> koyEdgePattern(const std::vector<float2>& base, int total) {
    std::vector<float2> pts = base;
    auto halton = [](unsigned i, unsigned b) {
        float f = 1.0f, r = 0.0f;
        while (i > 0) { f /= float(b); r += f * float(i % b); i /= b; }
        return r;
    };
    unsigned next = 1;
    while (int(pts.size()) < total) {
        float bestD = -1.0f;
        float2 best{0.5f, 0.5f};
        for (int c = 0; c < 256; ++c, ++next) {
            const float2 q = make_float2(halton(next, 2), halton(next, 3));
            float dmin = 1e9f;
            for (const float2& e : pts) {
                float dx = std::fabs(q.x - e.x), dy = std::fabs(q.y - e.y);
                dx = std::fmin(dx, 1.0f - dx);
                dy = std::fmin(dy, 1.0f - dy);
                dmin = std::fmin(dmin, dx * dx + dy * dy);
            }
            if (dmin > bestD) { bestD = dmin; best = q; }
        }
        pts.push_back(best);
    }
    return pts;
}
#endif
