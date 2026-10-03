// Scene 5 "Koy": deterministic noise and a parallel loop for the load-time builders (the arch, the amphorae). The same
// gradient noise as the floor's (koy_world.cpp), so a shape and the sand around it speak the same language.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <thread>
#include <vector>

namespace kn {

inline unsigned int hash3(int x, int y, int z) {
    unsigned int h = unsigned(x) * 73856093u ^ unsigned(y) * 19349663u ^ unsigned(z) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}
inline float hash01(int x, int y, int z) { return float(hash3(x, y, z) & 0xFFFFFFu) / 16777216.0f; }
inline float grad3(int x, int y, int z, float fx, float fy, float fz) {
    const unsigned int h = hash3(x, y, z) & 15u;
    const float u = h < 8 ? fx : fy, v = h < 4 ? fy : (h == 12 || h == 14 ? fx : fz);
    return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
}
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
// gradient noise, about -1 .. 1
inline float noise3(float x, float y, float z) {
    const int X = int(std::floor(x)), Y = int(std::floor(y)), Z = int(std::floor(z));
    const float fx = x - X, fy = y - Y, fz = z - Z;
    const float u = fade(fx), v = fade(fy), w = fade(fz);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    return L(L(L(grad3(X, Y, Z, fx, fy, fz), grad3(X + 1, Y, Z, fx - 1, fy, fz), u),
               L(grad3(X, Y + 1, Z, fx, fy - 1, fz), grad3(X + 1, Y + 1, Z, fx - 1, fy - 1, fz), u), v),
             L(L(grad3(X, Y, Z + 1, fx, fy, fz - 1), grad3(X + 1, Y, Z + 1, fx - 1, fy, fz - 1), u),
               L(grad3(X, Y + 1, Z + 1, fx, fy - 1, fz - 1), grad3(X + 1, Y + 1, Z + 1, fx - 1, fy - 1, fz - 1), u), v),
             w);
}
inline float fbm3(float x, float y, float z, int oct, float lac = 2.03f, float gain = 0.5f) {
    float s = 0.0f, a = 0.5f, f = 1.0f;
    for (int k = 0; k < oct; ++k) {
        s += a * noise3(x * f + 17.3f * k, y * f, z * f);
        f *= lac;
        a *= gain;
    }
    return s;
}
// ridged: sharp creases where the noise crosses zero (eroded rock)
inline float ridged3(float x, float y, float z, int oct) {
    float s = 0.0f, a = 0.5f, f = 1.0f;
    for (int k = 0; k < oct; ++k) {
        s += a * (1.0f - std::fabs(noise3(x * f + 5.1f * k, y * f, z * f)));
        f *= 2.07f;
        a *= 0.5f;
    }
    return s;
}
inline float smoothstep(float a, float b, float x) {
    const float t = std::min(std::max((x - a) / (b - a), 0.0f), 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline void parallelFor(int n, const std::function<void(int)>& f) {
    const int T = std::max(1, int(std::thread::hardware_concurrency()));
    std::atomic<int> next{0};
    std::vector<std::thread> th;
    for (int t = 0; t < T; ++t)
        th.emplace_back([&] {
            for (int i = next++; i < n; i = next++) f(i);
        });
    for (auto& x : th) x.join();
}

}   // namespace kn
