// Scene 2 "Fener": islands, forests, the lighthouse and the cabin, generated from code and scenes/fener.cfg.
// Numbers come from the F0 value study (docs/fener/DURUM.md): camera A 470 m from the lighthouse, B 900 m.
#include "fener/fener_scene.h"
#include "image_io.h"
#include "stb_easy_font.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <random>
#include <sstream>

static const float PI = 3.14159265358979f;

// ---------------------------------------------------------------------------------------------------
// noise (host)
// ---------------------------------------------------------------------------------------------------
static float hash1(float i, float seed) {
    const double x = std::sin(double(i) * 12.9898 + double(seed) * 78.233) * 43758.5453;
    return float(x - std::floor(x));
}
static float hash2(int x, int y, int seed) {
    unsigned int h = unsigned(x) * 73856093u ^ unsigned(y) * 19349663u ^ unsigned(seed) * 83492791u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return (h >> 8) * (1.0f / 16777216.0f);
}
static float vnoise2(float x, float y, int seed) {
    const float ix = std::floor(x), iy = std::floor(y), fx = x - ix, fy = y - iy;
    const float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    const int X = int(ix), Y = int(iy);
    const float a = hash2(X, Y, seed), b = hash2(X + 1, Y, seed), c = hash2(X, Y + 1, seed), d = hash2(X + 1, Y + 1, seed);
    return (a * (1 - ux) + b * ux) * (1 - uy) + (c * (1 - ux) + d * ux) * uy;
}
static float fbm2(float x, float y, int seed, int oct = 5) {
    float s = 0, a = 0.5f, n = 0;
    for (int k = 0; k < oct; ++k) { s += a * vnoise2(x, y, seed + k * 7); n += a; x = x * 2.03f + 11.3f; y = y * 2.03f + 7.1f; a *= 0.5f; }
    return s / n;
}
static float ridged1(float x, int seed, int oct = 6) {
    float s = 0, a = 0.5f, n = 0;
    for (int k = 0; k < oct; ++k) {
        const float i = std::floor(x), f = x - i, u = f * f * (3 - 2 * f);
        const float v = hash1(i, float(seed + k * 3)) * (1 - u) + hash1(i + 1, float(seed + k * 3)) * u;
        const float r = 1.0f - std::fabs(2.0f * v - 1.0f);
        s += a * r * r; n += a; x *= 2.1f; a *= 0.5f;
    }
    return s / n;
}
static float sstep(float a, float b, float x) {
    const float t = std::min(std::max((x - a) / (b - a), 0.0f), 1.0f);
    return t * t * (3 - 2 * t);
}
static float lerp1(float a, float b, float t) { return a + (b - a) * t; }

// ---------------------------------------------------------------------------------------------------
// mesh helpers
// ---------------------------------------------------------------------------------------------------
void FMeshCPU::tri(float3 a, float3 b, float3 c, int m) {
    float3 n = cross(b - a, c - a);
    const float l = length(n);
    if (l < 1e-12f) return;
    n = n / l;
    const unsigned int base = unsigned(pos.size());
    pos.push_back(a); pos.push_back(b); pos.push_back(c);
    nrm.push_back(n); nrm.push_back(n); nrm.push_back(n);
    idx.push_back(make_uint3(base, base + 1, base + 2));
    mat.push_back((unsigned char)m);
}
void FMeshCPU::quad(float3 a, float3 b, float3 c, float3 d, int m) {
    tri(a, b, c, m);
    tri(a, c, d, m);
}
void FMeshCPU::box(float3 c, float3 h, float3 ax, float3 ay, float3 az, int m) {
    float3 v[8];
    for (int k = 0; k < 8; ++k)
        v[k] = c + ax * ((k & 1) ? h.x : -h.x) + ay * ((k & 2) ? h.y : -h.y) + az * ((k & 4) ? h.z : -h.z);
    quad(v[0], v[2], v[3], v[1], m);   // -z
    quad(v[4], v[5], v[7], v[6], m);   // +z
    quad(v[0], v[4], v[6], v[2], m);   // -x
    quad(v[1], v[3], v[7], v[5], m);   // +x
    quad(v[0], v[1], v[5], v[4], m);   // -y
    quad(v[2], v[6], v[7], v[3], m);   // +y
}
// a surface of revolution around +y: profile (radius, y) pairs, `seg` sides
static void lathe(FMeshCPU& m, const std::vector<std::pair<float, float>>& prof, float3 c, int seg, int mat,
                  const std::function<int(int, int)>& matOf = nullptr) {
    for (size_t k = 0; k + 1 < prof.size(); ++k)
        for (int s = 0; s < seg; ++s) {
            const float a0 = 2 * PI * s / seg, a1 = 2 * PI * (s + 1) / seg;
            auto P = [&](size_t j, float a) { return c + make_float3(prof[j].first * std::cos(a), prof[j].second, prof[j].first * std::sin(a)); };
            const int mm = matOf ? matOf(int(k), s) : mat;
            m.quad(P(k, a0), P(k, a1), P(k + 1, a1), P(k + 1, a0), mm);
        }
}

// A heightfield as a mesh with smooth normals; material chosen per triangle.
static FMeshCPU terrain(const std::string& name, float x0, float x1, float z0, float z1, float step, const std::function<float(float, float)>& h,
                        const std::function<int(float, float, float, float)>& matOf) {
    FMeshCPU m;
    m.name = name;
    const int nx = int((x1 - x0) / step) + 1, nz = int((z1 - z0) / step) + 1;
    std::vector<float> H(size_t(nx) * nz);
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) H[size_t(j) * nx + i] = h(x0 + i * step, z0 + j * step);
    auto at = [&](int i, int j) { return H[size_t(std::min(std::max(j, 0), nz - 1)) * nx + std::min(std::max(i, 0), nx - 1)]; };
    m.pos.resize(size_t(nx) * nz);
    m.nrm.resize(m.pos.size());
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) {
            m.pos[size_t(j) * nx + i] = make_float3(x0 + i * step, at(i, j), z0 + j * step);
            const float dx = (at(i + 1, j) - at(i - 1, j)) / (2 * step), dz = (at(i, j + 1) - at(i, j - 1)) / (2 * step);
            m.nrm[size_t(j) * nx + i] = normalize(make_float3(-dx, 1.0f, -dz));
        }
    for (int j = 0; j + 1 < nz; ++j)
        for (int i = 0; i + 1 < nx; ++i) {
            const unsigned int a = unsigned(j * nx + i), b = a + 1, c = a + unsigned(nx), d = c + 1;
            // skip cells wholly under the sea floor's depth: nothing sees them
            const float hmax = std::max(std::max(H[a], H[b]), std::max(H[c], H[d]));
            if (hmax < -6.0f) continue;
            const float cx = x0 + (i + 0.5f) * step, cz = z0 + (j + 0.5f) * step;
            const float3 n = normalize(m.nrm[a] + m.nrm[b] + m.nrm[c] + m.nrm[d]);
            const int mm = matOf(cx, cz, (H[a] + H[b] + H[c] + H[d]) * 0.25f, n.y);
            m.idx.push_back(make_uint3(a, c, d)); m.mat.push_back((unsigned char)mm);
            m.idx.push_back(make_uint3(a, d, b)); m.mat.push_back((unsigned char)mm);
        }
    return m;
}

// ---------------------------------------------------------------------------------------------------
// the spruce: whorls of drooping branches; each branch a hanging curtain of needles with a toothed lower edge.
// Unit height (scaled per instance), trunk at the origin.
// ---------------------------------------------------------------------------------------------------
static FMeshCPU makeSpruce(int seed, float crownBase) {
    FMeshCPU m;
    m.name = "spruce" + std::to_string(seed);
    std::mt19937 rng(1000u + unsigned(seed));
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    auto R = [&](float a, float b) { return a + (b - a) * U(rng); };
    // trunk
    {
        const int seg = 7;
        const float r0 = 0.011f, r1 = 0.002f;
        lathe(m, {{r0 * 1.3f, -0.03f}, {r0, 0.04f}, {r0 * 0.55f + r1 * 0.45f, 0.5f}, {r1, 1.0f}}, make_float3(0, 0, 0), seg, FM_BARK);
    }
    // dead twigs on the bare lower trunk: thin, short, sharp, slightly drooping (Inside's forest is full of them)
    for (float ty = 0.03f; ty < crownBase - 0.01f; ty += R(0.008f, 0.02f)) {
        const float th = R(0.0f, 2 * PI);
        const float3 dir = make_float3(std::cos(th), R(-0.35f, 0.1f), std::sin(th));
        const float ll = R(0.01f, 0.05f) * (0.5f + ty / std::max(crownBase, 0.05f));
        const float3 b = make_float3(0.0f, ty, 0.0f) + make_float3(dir.x, 0.0f, dir.z) * 0.008f;
        const float3 side = normalize(cross(dir, make_float3(0, 1, 0))) * 0.0012f;
        m.tri(b - side + make_float3(0, 0.0012f, 0), b + side - make_float3(0, 0.0012f, 0), b + dir * ll, FM_BARK);
    }
    float y = 0.985f;
    float rot = R(0.0f, 2 * PI);
    while (y > std::max(crownBase, 0.05f)) {
        const float u = y;                                   // height fraction
        const float sp = 0.020f + 0.020f * (1.0f - u);       // whorl spacing
        const int nb = int(R(5.0f, 7.99f));
        for (int b = 0; b < nb; ++b) {
            const float th = rot + 2 * PI * (b + R(-0.25f, 0.25f)) / nb;
            const float3 dir = make_float3(std::cos(th), 0.0f, std::sin(th));
            const float L = (0.26f * std::pow(1.0f - u, 0.9f) + 0.014f) * R(0.8f, 1.12f);
            const float dro = R(0.26f, 0.50f);
            const float r0 = 0.006f;
            const int n = 7;
            float3 A[n + 1], B[n + 1];
            const float nOff = R(0.0f, 50.0f);
            for (int k = 0; k <= n; ++k) {
                const float t = float(k) / n;
                const float3 a = dir * (r0 + L * t) + make_float3(0.0f, y - dro * L * t + 0.10f * L * t * t * t, 0.0f);
                const float sk = sp * 1.45f * std::pow(1.0f - t, 1.05f) * (0.8f + 0.4f * fbm2(t * 4.0f + nOff, 0.5f, seed, 2)) + 0.002f;
                A[k] = a;
                B[k] = a - make_float3(0.0f, sk, 0.0f);
            }
            for (int k = 0; k < n; ++k) m.quad(A[k], A[k + 1], B[k + 1], B[k], FM_NEEDLE);
            // teeth: hanging needles along the lower edge, swept towards the tip; short ones on top
            for (int k = 0; k < n; ++k)
                for (int j = 0; j < 3; ++j) {
                    const float f = (j + R(0.1f, 0.9f)) / 3.0f;
                    const float3 base = B[k] + (B[k + 1] - B[k]) * f;
                    const float ll = R(0.004f, 0.011f) * (0.7f + 0.5f * float(k) / n);
                    const float sweep = R(0.35f, 1.15f);
                    const float3 tip = base + dir * (std::sin(sweep) * ll) - make_float3(0.0f, std::cos(sweep) * ll, 0.0f);
                    const float3 w = (B[k + 1] - B[k]) * 0.12f;
                    m.tri(base - w, base + w, tip, FM_NEEDLE);
                }
            for (int k = 1; k < n; ++k)
                if (U(rng) < 0.6f) {
                    const float3 base = A[k];
                    const float ll = R(0.003f, 0.007f);
                    const float sw = R(0.6f, 1.3f);
                    const float3 tip = base + dir * (std::sin(sw) * ll) + make_float3(0.0f, std::cos(sw) * ll, 0.0f);
                    const float3 w = (A[k + 1] - A[k]) * 0.15f;
                    m.tri(base - w, base + w, tip, FM_NEEDLE);
                }
            {   // tip
                const float3 a = A[n];
                const float3 tip = a + dir * R(0.006f, 0.016f) - make_float3(0.0f, R(0.0f, 0.006f), 0.0f);
                m.tri(a + make_float3(0.0f, 0.0015f, 0.0f), a - make_float3(0.0f, 0.0025f, 0.0f), tip, FM_NEEDLE);
            }
        }
        rot += 2.39996f;   // golden angle between whorls
        y -= sp * R(0.85f, 1.15f);
    }
    return m;
}

// ---------------------------------------------------------------------------------------------------
// the lighthouse: tower with six bands, gallery and railing, lantern (glass is camera-only), mullions, dome
// ---------------------------------------------------------------------------------------------------
static void makeLighthouse(FMeshCPU& body, FMeshCPU& glass, float3 base, float& lampY) {
    const float th = 36.0f, rb = 4.4f, rt = 3.1f;
    const int seg = 48;
    std::vector<std::pair<float, float>> prof;
    const int nb = 6;
    for (int k = 0; k <= nb; ++k) {
        const float y = th * k / nb;
        prof.push_back({rb + (rt - rb) * y / th, y});
    }
    prof.insert(prof.begin(), {rb + 0.25f, -1.5f});
    lathe(body, prof, base, seg, FM_TOWER_LIGHT, [&](int k, int) { return (k == 0) ? FM_ROCK : (((k - 1) % 2 == 0) ? FM_TOWER_LIGHT : FM_TOWER_DARK); });
    const float g = th;   // gallery deck
    lathe(body, {{rt - 0.2f, g - 0.35f}, {3.95f, g - 0.35f}, {3.95f, g}, {2.6f, g}}, base, seg, FM_METAL);
    for (int s = 0; s < 28; ++s) {   // railing posts and rail
        const float a = 2 * PI * s / 28;
        const float3 c = base + make_float3(3.85f * std::cos(a), g + 0.55f, 3.85f * std::sin(a));
        body.box(c, make_float3(0.04f, 0.55f, 0.04f), make_float3(1, 0, 0), make_float3(0, 1, 0), make_float3(0, 0, 1), FM_METAL);
    }
    lathe(body, {{3.80f, g + 1.08f}, {3.92f, g + 1.08f}, {3.92f, g + 1.16f}, {3.80f, g + 1.16f}, {3.80f, g + 1.08f}}, base, seg, FM_METAL);
    // lantern: low wall, glass, mullions, roof
    const float l0 = g + 0.0f, l1 = g + 0.65f, l2 = g + 4.0f;
    lathe(body, {{2.55f, l0}, {2.55f, l1}, {2.45f, l1}}, base, seg, FM_METAL);
    lathe(glass, {{2.40f, l1}, {2.40f, l2}}, base, seg, FM_LAMP_GLASS);
    for (int s = 0; s < 10; ++s) {
        const float a = 2 * PI * (s + 0.5f) / 10;
        const float3 c = base + make_float3(2.47f * std::cos(a), (l1 + l2) * 0.5f, 2.47f * std::sin(a));
        const float3 ax = make_float3(-std::sin(a), 0, std::cos(a)), az = make_float3(std::cos(a), 0, std::sin(a));
        body.box(c, make_float3(0.06f, (l2 - l1) * 0.5f, 0.05f), ax, make_float3(0, 1, 0), az, FM_METAL);
    }
    lathe(body, {{2.75f, l2}, {2.6f, l2 + 0.6f}, {1.9f, l2 + 1.5f}, {1.0f, l2 + 2.1f}, {0.25f, l2 + 2.4f}, {0.0f, l2 + 2.45f}}, base, seg, FM_METAL);
    // the vane rides with the glass (camera only): as a shadow caster it drew a dark line up into the fog
    glass.box(base + make_float3(0, l2 + 3.2f, 0), make_float3(0.05f, 0.8f, 0.05f), make_float3(1, 0, 0), make_float3(0, 1, 0), make_float3(0, 0, 1), FM_METAL);
    // door and two small windows facing the cabin side (east-south)
    {
        const float a = -0.25f;
        const float3 out = make_float3(std::cos(a), 0, std::sin(a)), side = make_float3(-std::sin(a), 0, std::cos(a));
        const float3 c = base + out * (rb + 0.02f) + make_float3(0, 1.1f, 0);
        body.quad(c - side * 0.6f - make_float3(0, 1.1f, 0), c + side * 0.6f - make_float3(0, 1.1f, 0), c + side * 0.6f + make_float3(0, 1.1f, 0),
                  c - side * 0.6f + make_float3(0, 1.1f, 0), FM_METAL);
    }
    lampY = base.y + (l1 + l2) * 0.5f;
}

// ---------------------------------------------------------------------------------------------------
// the keeper's cabin: plank walls, a sagging gable roof, a chimney, one window facing the strait
// ---------------------------------------------------------------------------------------------------
static void makeCabin(FMeshCPU& body, FMeshCPU& pane, float3 c, float yaw, float3& winCorner, float3& winU, float3& winV, float3& winN) {
    const float3 X = make_float3(std::cos(yaw), 0, std::sin(yaw)), Z = make_float3(-std::sin(yaw), 0, std::cos(yaw)), Y = make_float3(0, 1, 0);
    const float w = 4.0f, d = 2.6f, h = 3.6f, t = 0.12f;
    const float lean = 0.06f;   // a weak, tired house: the walls lean a little
    auto P = [&](float x, float y, float z) { return c + X * (x + lean * y) + Y * y + Z * z; };
    // front wall (facing -Z, towards the strait) with the window opening
    const float wx0 = -2.3f, wx1 = -1.0f, wy0 = 1.2f, wy1 = 2.3f;
    auto wallPiece = [&](float x0, float x1, float y0, float y1) {
        body.quad(P(x0, y0, -d), P(x1, y0, -d), P(x1, y1, -d), P(x0, y1, -d), FM_WOOD);
        body.quad(P(x0, y0, -d + t), P(x0, y1, -d + t), P(x1, y1, -d + t), P(x1, y0, -d + t), FM_WOOD);
    };
    wallPiece(-w, wx0, -0.3f, h);
    wallPiece(wx1, w, -0.3f, h);
    wallPiece(wx0, wx1, -0.3f, wy0);
    wallPiece(wx0, wx1, wy1, h);
    // reveals of the opening
    body.quad(P(wx0, wy0, -d), P(wx0, wy0, -d + t), P(wx1, wy0, -d + t), P(wx1, wy0, -d), FM_WOOD);
    body.quad(P(wx0, wy1, -d), P(wx1, wy1, -d), P(wx1, wy1, -d + t), P(wx0, wy1, -d + t), FM_WOOD);
    // back and side walls
    body.box(P(0, h * 0.5f - 0.15f, d - t * 0.5f), make_float3(w, h * 0.5f + 0.15f, t * 0.5f), X, Y, Z, FM_WOOD);
    body.box(P(-w + t * 0.5f, h * 0.5f - 0.15f, 0), make_float3(t * 0.5f, h * 0.5f + 0.15f, d), X, Y, Z, FM_WOOD);
    body.box(P(w - t * 0.5f, h * 0.5f - 0.15f, 0), make_float3(t * 0.5f, h * 0.5f + 0.15f, d), X, Y, Z, FM_WOOD);
    // interior: a dark floor so the window shows depth
    body.quad(P(-w, 0.05f, -d), P(w, 0.05f, -d), P(w, 0.05f, d), P(-w, 0.05f, d), FM_WOOD);
    // gable roof along X, sagging in the middle, overhanging
    const int n = 8;
    for (int k = 0; k < n; ++k) {
        const float x0 = -w - 0.5f + (2 * w + 1.0f) * k / n, x1 = -w - 0.5f + (2 * w + 1.0f) * (k + 1) / n;
        auto sag = [&](float x) { return -0.18f * (1.0f - sqr(x / (w + 0.5f))); };
        const float r0 = h + 2.0f + sag(x0), r1 = h + 2.0f + sag(x1);
        body.quad(P(x0, r0, 0), P(x1, r1, 0), P(x1, h - 0.25f, -d - 0.6f), P(x0, h - 0.25f, -d - 0.6f), FM_ROOF);
        body.quad(P(x0, r0, 0), P(x0, h - 0.25f, d + 0.6f), P(x1, h - 0.25f, d + 0.6f), P(x1, r1, 0), FM_ROOF);
    }
    // gable ends
    body.tri(P(-w, h, -d), P(-w, h, d), P(-w, h + 1.85f, 0), FM_WOOD);
    body.tri(P(w, h, -d), P(w, h + 1.85f, 0), P(w, h, d), FM_WOOD);
    // chimney
    body.box(P(2.2f, h + 1.6f, 0.9f), make_float3(0.35f, 1.4f, 0.35f), X, Y, Z, FM_ROCK);
    // window pane: camera-only warm emitter, a little behind the wall's face; mullions in front
    pane.quad(P(wx0, wy0, -d + 0.06f), P(wx1, wy0, -d + 0.06f), P(wx1, wy1, -d + 0.06f), P(wx0, wy1, -d + 0.06f), FM_WINDOW);
    body.box(P((wx0 + wx1) * 0.5f, (wy0 + wy1) * 0.5f, -d + 0.03f), make_float3(0.03f, (wy1 - wy0) * 0.5f, 0.02f), X, Y, Z, FM_WOOD);
    body.box(P((wx0 + wx1) * 0.5f, (wy0 + wy1) * 0.5f, -d + 0.03f), make_float3((wx1 - wx0) * 0.5f, 0.03f, 0.02f), X, Y, Z, FM_WOOD);
    // the window as a light: a rectangle in the opening, emitting outwards (-Z)
    winCorner = P(wx0, wy0, -d - 0.01f);
    winU = P(wx1, wy0, -d - 0.01f) - winCorner;
    winV = P(wx0, wy1, -d - 0.01f) - winCorner;
    winN = -Z;
}

// ---------------------------------------------------------------------------------------------------
// the world
// ---------------------------------------------------------------------------------------------------
static float lighthouseIslandH(float x, float z, float Ht, float3 cabin) {
    const float r = std::sqrt(x * x + z * z);
    const float th = std::atan2(z, x);
    const float R = 215.0f + 45.0f * (fbm2(std::cos(th) * 1.4f + 3.0f, std::sin(th) * 1.4f + 5.0f, 21, 4) - 0.5f) * 2.0f;
    const float coast = sstep(R + 35.0f, R - 25.0f, r);
    float hill = Ht * std::exp(-std::pow(r / 95.0f, 1.7f));
    const float rough = 2.5f * (fbm2(x / 9.0f, z / 9.0f, 31) - 0.5f) + 7.0f * (fbm2(x / 60.0f, z / 60.0f, 33) - 0.5f);
    float h = lerp1(-14.0f, 2.5f + hill + rough * sstep(0.0f, 30.0f, R - r), coast);
    // the cabin's level yard
    const float dc = std::sqrt(sqr(x - cabin.x) + sqr(z - cabin.z));
    h = lerp1(h, cabin.y, sstep(16.0f, 9.0f, dc));
    // the lighthouse's level top
    h = lerp1(h, Ht + 0.3f, sstep(14.0f, 7.0f, r));
    return h;
}

// The two viewpoints look out through small coves carved into the bank: water starts a few metres ahead and the
// framing spruces stand on the points either side (set by buildFenerScene before the terrain is generated).
static float3 s_viewA = {0.0f, 0.0f, -470.0f}, s_viewB = {690.0f, 0.0f, -540.0f};
static float carveCove(float h, float x, float z, float3 at, float3 fwd, float open) {
    const float3 rt = make_float3(fwd.z, 0.0f, -fwd.x);
    const float dx = x - at.x, dz = z - at.z;
    const float f = dx * fwd.x + dz * fwd.z, r = dx * rt.x + dz * rt.z;
    if (f < 2.0f || f > 220.0f) return h;
    const float edge = 4.5f + open * f;
    const float w = sstep(edge + 5.0f, edge, std::fabs(r)) * sstep(2.0f, 6.0f, f);
    const float target = lerp1(1.2f, -9.0f, sstep(6.0f, 15.0f, f));
    return lerp1(h, std::min(h, target), w);
}

static float cameraIslandH(float x, float z) {
    // a broad island south of the strait; its north shore runs ~440 m from the lighthouse at A and bends south-east
    // to B (~920 m from the lighthouse, open water to its north)
    const float cx = 150.0f, cz = -1020.0f;
    const float dx = (x - cx) / 760.0f, dz = (z - cz) / 590.0f;
    float r = std::sqrt(dx * dx + dz * dz);
    const float th = std::atan2(dz, dx);
    r *= 1.0f - 0.045f * (fbm2(std::cos(th) * 2.2f + 7.0f, std::sin(th) * 2.2f + 1.0f, 41, 4) - 0.5f) * 2.0f;
    r += 0.02f * (fbm2(x / 40.0f, z / 40.0f, 47, 3) - 0.5f);   // small coves and points
    // a headland in the north-east whose west shore faces the lighthouse (B stands on it)
    const float hx = (x - 760.0f) / 120.0f, hz = (z + 600.0f) / 95.0f;
    r = std::min(r, 0.985f + 0.05f * (std::sqrt(hx * hx + hz * hz) - 1.0f) * 3.0f);
    const float coast = sstep(1.035f, 0.985f, r);               // a ~30 m shore band
    const float inland = sstep(0.98f, 0.70f, r);
    const float hills = 28.0f * std::exp(-sqr((x - 100.0f) / 420.0f) - sqr((z + 1150.0f) / 280.0f));
    const float rough = 2.0f * (fbm2(x / 10.0f, z / 10.0f, 43) - 0.5f) + 6.0f * (fbm2(x / 70.0f, z / 70.0f, 45) - 0.5f);
    float h = lerp1(-12.0f, 3.0f + hills * inland + rough * 0.6f * (0.4f + 0.6f * inland), coast);
    h = carveCove(h, x, z, s_viewA, make_float3(0.0f, 0.0f, 1.0f), 0.35f);
    h = carveCove(h, x, z, s_viewB, normalize(make_float3(-s_viewB.x, 0.0f, -s_viewB.z)), 0.45f);
    return h;
}

float fenerGround(float x, float z) { return cameraIslandH(x, z); }

static void placement(float m[12], float3 t, float s, float yaw) {
    const float c = std::cos(yaw) * s, n = std::sin(yaw) * s;
    const float r[12] = {c, 0, n, t.x, 0, s, 0, t.y, -n, 0, c, t.z};
    std::memcpy(m, r, sizeof(r));
}

static float distToPolyline(float x, float z, const std::vector<float3>& pl) {
    float best = 1e9f;
    for (size_t k = 0; k + 1 < pl.size(); ++k) {
        const float ax = pl[k].x, az = pl[k].z, bx = pl[k + 1].x, bz = pl[k + 1].z;
        const float vx = bx - ax, vz = bz - az;
        const float t = std::min(std::max(((x - ax) * vx + (z - az) * vz) / (vx * vx + vz * vz), 0.0f), 1.0f);
        best = std::min(best, std::sqrt(sqr(x - ax - vx * t) + sqr(z - az - vz * t)));
    }
    return best;
}

size_t FenerScene::triangles() const {
    size_t n = 0;
    for (const auto& i : instances) n += meshes[i.mesh].idx.size();
    return n;
}

FenerScene buildFenerScene(const Config& cfg) {
    FenerScene s;
    // ---- materials (linear albedo; Inside keeps everything dark and muted)
    s.materials.resize(FM_COUNT);
    auto M = [&](int id, float3 a, int kind = FK_DIFFUSE, float3 e = make_float3(0, 0, 0), float wrap = 0.0f) {
        s.materials[id] = FMaterial{a, kind, e, wrap};
    };
    M(FM_GRASS, cfg.v3("mat.grass", make_float3(0.040f, 0.045f, 0.040f)));
    M(FM_ROCK, cfg.v3("mat.rock", make_float3(0.060f, 0.062f, 0.064f)));
    M(FM_PATH, cfg.v3("mat.path", make_float3(0.110f, 0.105f, 0.098f)));
    M(FM_BARK, cfg.v3("mat.bark", make_float3(0.030f, 0.028f, 0.026f)));
    M(FM_NEEDLE, cfg.v3("mat.needle", make_float3(0.030f, 0.040f, 0.036f)), FK_DIFFUSE, make_float3(0, 0, 0), cfg.f("mat.needle_wrap", 0.45f));
    M(FM_TOWER_LIGHT, cfg.v3("mat.tower_light", make_float3(0.32f, 0.32f, 0.31f)));
    M(FM_TOWER_DARK, cfg.v3("mat.tower_dark", make_float3(0.085f, 0.075f, 0.072f)));
    M(FM_METAL, cfg.v3("mat.metal", make_float3(0.025f, 0.026f, 0.028f)));
    M(FM_LAMP_GLASS, make_float3(0, 0, 0), FK_LAMP_GLASS);
    M(FM_WOOD, cfg.v3("mat.wood", make_float3(0.045f, 0.038f, 0.032f)));
    M(FM_ROOF, cfg.v3("mat.roof", make_float3(0.030f, 0.030f, 0.032f)));
    M(FM_WINDOW, make_float3(0, 0, 0), FK_WINDOW, cfg.v3("window.emission", make_float3(1.6f, 0.62f, 0.17f)));
    M(FM_FAR, cfg.v3("mat.far", make_float3(0.012f, 0.013f, 0.015f)));

    const float Ht = cfg.f("island.hill", 44.0f);
    s_viewA = cfg.v3("view.A.pos", s_viewA);
    s_viewB = cfg.v3("view.B.pos", s_viewB);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);

    // ---- places
    const float3 cabinXZ = make_float3(74.0f, 0.0f, -22.0f);
    float3 cabin = cabinXZ;
    cabin.y = Ht * std::exp(-std::pow(std::sqrt(74.0f * 74.0f + 22.0f * 22.0f) / 95.0f, 1.7f)) + 2.5f;
    const std::vector<float3> path = {{4.4f, 0, -1.4f}, {14, 0, -5}, {27, 0, -8}, {41, 0, -11}, {55, 0, -15}, {66, 0, -19}, {71, 0, -20}};
    auto lhH = [&](float x, float z) { return lighthouseIslandH(x, z, Ht, cabin); };

    // ---- meshes: islands
    auto islandMat = [&](float x, float z, float h, float ny) {
        if (h < 0.6f) return int(FM_ROCK);
        if (ny < 0.72f) return int(FM_ROCK);
        if (distToPolyline(x, z, path) < 0.85f) return int(FM_PATH);
        return int(FM_GRASS);
    };
    const int mLh = int(s.meshes.size());
    s.meshes.push_back(terrain("lighthouse_island", -300, 300, -300, 300, cfg.f("terrain.step", 1.25f), lhH, islandMat));
    const int mCam = int(s.meshes.size());
    s.meshes.push_back(terrain("camera_island", -700, 1000, -1650, -400, cfg.f("terrain.step_cam", 1.25f), cameraIslandH,
                               [](float, float, float h, float ny) { return (h < 0.5f || ny < 0.7f) ? int(FM_ROCK) : int(FM_GRASS); }));
    // back islands, low and wooded
    const int mBack = int(s.meshes.size());
    s.meshes.push_back(terrain("back_islands", -1600, 1500, 900, 2400, 6.0f,
                               [](float x, float z) {
                                   const float a = std::exp(-sqr((x + 800.0f) / 420.0f) - sqr((z - 1250.0f) / 160.0f));
                                   const float b = std::exp(-sqr((x - 760.0f) / 520.0f) - sqr((z - 2030.0f) / 200.0f));
                                   const float m = std::max(a, b);
                                   return -10.0f + 72.0f * m * (0.8f + 0.4f * fbm2(x / 80.0f, z / 80.0f, 51));
                               },
                               [](float, float, float, float) { return int(FM_FAR); }));
    // the fjord walls: three ridges on arcs around the strait, kilometres away, colossal; every view meets them
    const int mWalls = int(s.meshes.size());
    {
        FMeshCPU w;
        w.name = "fjord_walls";
        const float R[3] = {16000.0f, 10000.0f, 6000.0f}, h0[3] = {1500.0f, 800.0f, 250.0f}, h1[3] = {3400.0f, 2000.0f, 900.0f};
        for (int L = 0; L < 3; ++L) {
            const int n = 2400;
            const float a0 = -2.1f, a1 = 2.1f;   // radians from north, both sides
            float3 prevB{}, prevT{};
            for (int k = 0; k <= n; ++k) {
                const float a = a0 + (a1 - a0) * k / n;
                const float s_ = R[L] * a;          // arc length: the ridge profile runs along it
                float y = h0[L] + (h1[L] - h0[L]) * ridged1(s_ / (R[L] * 0.25f) + 21 + L, 21 + L);
                if (L == 0) y += 1600.0f * std::exp(-sqr((s_ + 3200.0f) / 2200.0f)) + 900.0f * std::exp(-sqr((s_ - 4600.0f) / 1800.0f)) +
                                 1300.0f * std::exp(-sqr((s_ + 12500.0f) / 2600.0f));
                const float3 b = make_float3(R[L] * std::sin(a), -50.0f, R[L] * std::cos(a)), t = make_float3(b.x, y, b.z);
                if (k > 0) w.quad(prevB, b, t, prevT, FM_FAR);
                prevB = b; prevT = t;
            }
        }
        s.meshes.push_back(w);
    }
    // the lighthouse and the cabin
    const int mLight = int(s.meshes.size());
    s.meshes.push_back(FMeshCPU{"lighthouse"});
    const int mGlass = int(s.meshes.size());
    s.meshes.push_back(FMeshCPU{"lantern_glass"});
    float lampY = 0.0f;
    makeLighthouse(s.meshes[mLight], s.meshes[mGlass], make_float3(0, Ht + 0.3f, 0), lampY);
    s.lampPos = make_float3(0, lampY, 0);
    const int mCabin = int(s.meshes.size());
    s.meshes.push_back(FMeshCPU{"cabin"});
    const int mPane = int(s.meshes.size());
    s.meshes.push_back(FMeshCPU{"window_pane"});
    makeCabin(s.meshes[mCabin], s.meshes[mPane], cabin, 0.0f, s.winCorner, s.winU, s.winV, s.winN);
    // spruce variants
    const int nFull = 3, nStand = 4;
    const int mSpruce = int(s.meshes.size());
    for (int k = 0; k < nFull; ++k) s.meshes.push_back(makeSpruce(k, 0.05f));
    for (int k = 0; k < nStand; ++k) s.meshes.push_back(makeSpruce(nFull + k, 0.34f + 0.05f * k));

    // ---- instances
    auto place = [&](int mesh, float3 t, float scale, float yaw, unsigned int mask = 1u) {
        FInstanceCPU in;
        in.mesh = mesh;
        in.mask = mask;
        placement(in.m, t, scale, yaw);
        s.instances.push_back(in);
    };
    for (int m : {mLh, mCam, mBack, mWalls, mLight, mCabin}) place(m, make_float3(0, 0, 0), 1.0f, 0.0f);
    place(mGlass, make_float3(0, 0, 0), 1.0f, 0.0f, 2u);
    place(mPane, make_float3(0, 0, 0), 1.0f, 0.0f, 2u);

    // stand trees (high crowns, bare trunks) inside the forest; skirted trees where the forest meets the open
    auto open10 = [&](float x, float z, const std::function<float(float, float)>& g) {
        for (int k = 0; k < 8; ++k) {
            const float a = k * PI / 4;
            if (g(x + 11.0f * std::cos(a), z + 11.0f * std::sin(a)) < 0.8f) return true;
        }
        return false;
    };
    // forest on the lighthouse island: jittered grid, clear around the tower, the cabin and the path
    const float spacing = cfg.f("forest.spacing", 4.4f);
    for (float x = -280; x < 280; x += spacing)
        for (float z = -280; z < 280; z += spacing) {
            const float px = x + (U(rng) - 0.5f) * spacing * 0.9f, pz = z + (U(rng) - 0.5f) * spacing * 0.9f;
            const float g = lhH(px, pz);
            if (g < 1.8f) continue;
            const float r = std::sqrt(px * px + pz * pz);
            if (r < 17.0f) continue;
            if (std::sqrt(sqr(px - cabin.x) + sqr(pz - cabin.z)) < 13.0f) continue;
            if (distToPolyline(px, pz, path) < 2.2f) continue;
            // slope
            const float gx = lhH(px + 1, pz) - lhH(px - 1, pz), gz = lhH(px, pz + 1) - lhH(px, pz - 1);
            if (std::sqrt(gx * gx + gz * gz) * 0.5f > 0.9f) continue;
            float h = 18.0f + 14.0f * U(rng);
            if (r < 40.0f) h = std::min(h, 10.0f + (r - 17.0f) * 0.55f);   // the tower rises above its own clearing
            const bool edge = open10(px, pz, lhH) || r < 30.0f || distToPolyline(px, pz, path) < 9.0f;
            const int var = edge ? int(U(rng) * nFull) % nFull : nFull + int(U(rng) * nStand) % nStand;
            place(mSpruce + var, make_float3(px, g - 0.4f, pz), h, U(rng) * 2 * PI);
        }
    // back islands: smaller trees (they read as a serrated line)
    for (float x = -1600; x < 1500; x += 9.0f)
        for (float z = 900; z < 2400; z += 9.0f) {
            const float a = std::exp(-sqr((x + 800.0f) / 420.0f) - sqr((z - 1250.0f) / 160.0f));
            const float b = std::exp(-sqr((x - 760.0f) / 520.0f) - sqr((z - 2030.0f) / 200.0f));
            const float m = std::max(a, b);
            const float g = -10.0f + 72.0f * m * (0.8f + 0.4f * fbm2(x / 80.0f, z / 80.0f, 51));
            if (g < 2.0f || U(rng) > 0.75f) continue;
            place(mSpruce + int(U(rng) * nFull) % nFull, make_float3(x + (U(rng) - 0.5f) * 6, g - 0.5f, z + (U(rng) - 0.5f) * 6), 14.0f + 10.0f * U(rng),
                  U(rng) * 2 * PI);
        }
    // ---- the journey from A to B through the camera island's forest (Catmull-Rom through journey.points)
    {
        std::vector<float> pts;
        auto it = cfg.values.find("journey.points");
        const std::string def = "0 -470  25 -505  110 -560  230 -600  360 -630  480 -650  590 -640  660 -585  690 -540";
        std::istringstream ss(it != cfg.values.end() ? it->second : def);
        float v;
        while (ss >> v) pts.push_back(v);
        std::vector<float3> cp;
        for (size_t k = 0; k + 1 < pts.size(); k += 2) cp.push_back(make_float3(pts[k], 0.0f, pts[k + 1]));
        for (size_t k = 0; k + 1 < cp.size(); ++k) {
            const float3 p0 = cp[k > 0 ? k - 1 : k], p1 = cp[k], p2 = cp[k + 1], p3 = cp[k + 2 < cp.size() ? k + 2 : k + 1];
            const int n = std::max(2, int(length(p2 - p1)));
            for (int j = 0; j < n; ++j) {
                const float t = float(j) / n, t2 = t * t, t3 = t2 * t;
                const float3 q = (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
                s.journey.push_back(make_float3(q.x, cameraIslandH(q.x, q.z), q.z));
            }
        }
        if (!cp.empty()) s.journey.push_back(make_float3(cp.back().x, cameraIslandH(cp.back().x, cp.back().z), cp.back().z));
    }
    const float3 A = cfg.v3("view.A.pos", make_float3(0.0f, 0.0f, -470.0f));
    const float3 Bp = cfg.v3("view.B.pos", make_float3(690.0f, 0.0f, -540.0f));
    // B looks at the lighthouse: its corridor runs from B along that direction to the shore
    const float3 bF = normalize(make_float3(-Bp.x, 0.0f, -Bp.z)), bR = normalize(cross(make_float3(0, 1, 0), bF));
    auto inViewA = [&](float x, float z) {
        const float dx = x - A.x, dz = z - A.z;
        return dz > -6.0f && dz < 70.0f && std::fabs(dx) < 7.0f + dz * 0.9f;
    };
    auto inViewB = [&](float x, float z) {
        const float3 v = make_float3(x - Bp.x, 0.0f, z - Bp.z);
        const float f = dot(v, bF), r = dot(v, bR);
        return f > -6.0f && f < 90.0f && (r > 0.0f ? r < 8.0f + f * 1.6f : -r < 6.0f + f * 0.45f);
    };
    for (float x = -700; x < 1000; x += spacing)
        for (float z = -1650; z < -420; z += spacing) {
            const float px = x + (U(rng) - 0.5f) * spacing * 0.9f, pz = z + (U(rng) - 0.5f) * spacing * 0.9f;
            const float g = cameraIslandH(px, pz);
            if (g < 1.8f) continue;
            if (inViewA(px, pz) || inViewB(px, pz)) continue;
            // glades and gaps: the beam and the moon reach the forest floor through them
            if (fbm2(px / 55.0f, pz / 55.0f, 61, 3) > cfg.f("forest.glades", 0.63f) || U(rng) < cfg.f("forest.gaps", 0.08f)) continue;
            const float dTrail = distToPolyline(px, pz, s.journey);
            if (dTrail < 2.4f) continue;
            if (sqr(px - A.x) + sqr(pz - A.z) < 36.0f || sqr(px - Bp.x) + sqr(pz - Bp.z) < 36.0f) continue;
            const bool edge = open10(px, pz, cameraIslandH) || inViewA(px + 8, pz) || inViewA(px - 8, pz) || inViewB(px + 8 * bR.x, pz + 8 * bR.z) ||
                              inViewB(px - 8 * bR.x, pz - 8 * bR.z);
            const int var = edge && dTrail > 10.0f ? int(U(rng) * nFull) % nFull : nFull + int(U(rng) * nStand) % nStand;
            place(mSpruce + var, make_float3(px, g - 0.4f, pz), 18.0f + 14.0f * U(rng), U(rng) * 2 * PI);
        }
    // the framing spruces of the value study, placed in each view's own frame (right, forward)
    {
        const float3 fr[3] = {{-9.5f, 0, 11.0f}, {12.0f, 0, 12.5f}, {-14.0f, 0, 19.0f}};
        const float hh[3] = {30.0f, 28.0f, 26.0f};
        for (int k = 0; k < 3; ++k) {
            const float px = A.x + fr[k].x, pz = A.z + fr[k].z;
            place(mSpruce + k % nFull, make_float3(px, cameraIslandH(px, pz) - 0.4f, pz), hh[k], U(rng) * 2 * PI);
        }
        const float2 frB[2] = {{-11.5f, 9.5f}, {-16.5f, 17.0f}};
        const float hB[2] = {30.0f, 26.0f};
        for (int k = 0; k < 2; ++k) {
            const float px = Bp.x + bR.x * frB[k].x + bF.x * frB[k].y, pz = Bp.z + bR.z * frB[k].x + bF.z * frB[k].y;
            place(mSpruce + (k + 1) % nFull, make_float3(px, cameraIslandH(px, pz) - 0.4f, pz), hB[k], U(rng) * 2 * PI);
        }
    }

    // ---- views
    {
        const float gA = cameraIslandH(A.x, A.z);
        FenerView a{"A", make_float3(A.x, gA + cfg.f("view.A.eye", 1.7f), A.z), make_float3(0, 0, 0), cfg.f("view.A.fov", 38.0f)};
        const float pitch = cfg.f("view.A.pitch", 5.0f) * PI / 180.0f;
        a.target = a.pos + make_float3(0.0f, std::tan(pitch), 1.0f) * 100.0f;
        s.views.push_back(a);
        const float3 B = cfg.v3("view.B.pos", make_float3(690.0f, 0.0f, -540.0f));
        const float gB = cameraIslandH(B.x, B.z);
        FenerView b{"B", make_float3(B.x, std::max(gB, 1.0f) + cfg.f("view.B.eye", 1.7f), B.z), make_float3(0, 0, 0), cfg.f("view.B.fov", 44.0f)};
        const float pb = cfg.f("view.B.pitch", 10.0f) * PI / 180.0f;
        float3 f = normalize(make_float3(-b.pos.x, 0.0f, -b.pos.z));
        const float yaw = cfg.f("view.B.yaw", 9.0f) * PI / 180.0f;   // + turns right (towards the moon)
        f = make_float3(f.x * std::cos(yaw) + f.z * std::sin(yaw), 0.0f, -f.x * std::sin(yaw) + f.z * std::cos(yaw));
        f.y = std::tan(pb);
        b.target = b.pos + f * 100.0f;
        s.views.push_back(b);
    }
    return s;
}

void fenerCamera(const FenerView& v, unsigned int width, unsigned int height, FCamera& cam) {
    cam.pos = v.pos;
    cam.fwd = normalize(v.target - v.pos);
    cam.right = normalize(cross(make_float3(0, 1, 0), cam.fwd));
    cam.up = cross(cam.fwd, cam.right);
    cam.tanHalfY = std::tan(v.vfovDeg * 0.5f * PI / 180.0f);
    cam.tanHalfX = cam.tanHalfY * float(width) / float(height);
    cam.jitter = make_float2(0.0f, 0.0f);
}

// ---------------------------------------------------------------------------------------------------
// the map: the same height functions and tree instances, seen from above (1 px = 1.5 m)
// ---------------------------------------------------------------------------------------------------
bool fenerMap(const FenerScene& s, const Config& cfg, const std::string& path) {
    const float x0 = -900.0f, x1 = 1100.0f, z0 = -1350.0f, z1 = 450.0f, mpp = 1.5f;
    const int W = int((x1 - x0) / mpp), H = int((z1 - z0) / mpp);
    std::vector<float3> img(size_t(W) * H);
    const float Ht = cfg.f("island.hill", 44.0f);
    float3 cabin = make_float3(74.0f, 0.0f, -22.0f);
    cabin.y = Ht * std::exp(-std::pow(std::sqrt(74.0f * 74.0f + 22.0f * 22.0f) / 95.0f, 1.7f)) + 2.5f;
    auto height = [&](float x, float z) { return std::max(lighthouseIslandH(x, z, Ht, cabin), cameraIslandH(x, z)); };
    auto toPx = [&](float x, float z, int& px, int& py) { px = int((x - x0) / mpp); py = int((z1 - z) / mpp); };   // north up
    for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px) {
            const float x = x0 + (px + 0.5f) * mpp, z = z1 - (py + 0.5f) * mpp;
            const float h = height(x, z);
            float3 c;
            if (h < 0.0f) {
                const float d = std::min(-h / 12.0f, 1.0f);
                c = make_float3(0.10f, 0.16f, 0.24f) * (1.0f - 0.5f * d);
            } else {
                const float hx = height(x + mpp, z) - height(x - mpp, z), hz = height(x, z + mpp) - height(x, z - mpp);
                const float3 n = normalize(make_float3(-hx, 2.0f * mpp, -hz));
                const float shade = 0.55f + 0.45f * std::max(0.0f, dot(n, normalize(make_float3(-1.0f, 1.2f, 1.0f))));
                c = make_float3(0.36f, 0.40f, 0.32f) * shade * (0.75f + 0.25f * std::min(h / 50.0f, 1.0f));
            }
            img[size_t(py) * W + px] = c;
        }
    auto disc = [&](float x, float z, float r, float3 col, float a) {
        int cx, cy;
        toPx(x, z, cx, cy);
        const int R = std::max(1, int(r / mpp + 0.5f));
        for (int dy = -R; dy <= R; ++dy)
            for (int dx = -R; dx <= R; ++dx) {
                if (dx * dx + dy * dy > R * R) continue;
                const int X = cx + dx, Y = cy + dy;
                if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                float3& p = img[size_t(Y) * W + X];
                p = p * (1.0f - a) + col * a;
            }
    };
    // trees: crown discs (stand trees darker)
    for (const FInstanceCPU& in : s.instances) {
        if (s.meshes[in.mesh].name.rfind("spruce", 0) != 0) continue;
        const float scale = in.m[5];
        const bool stand = s.meshes[in.mesh].name >= "spruce3";
        disc(in.m[3], in.m[11], scale * (stand ? 0.12f : 0.2f), stand ? make_float3(0.08f, 0.12f, 0.09f) : make_float3(0.12f, 0.18f, 0.12f), 0.85f);
    }
    // the journey, A and B with their view wedges, the lighthouse and the cabin
    for (size_t k = 0; k < s.journey.size(); k += 2) disc(s.journey[k].x, s.journey[k].z, 1.6f, make_float3(1.0f, 1.0f, 1.0f), 1.0f);
    for (size_t v = 0; v < s.views.size(); ++v) {
        const FenerView& vw = s.views[v];
        const float3 f = normalize(make_float3(vw.target.x - vw.pos.x, 0.0f, vw.target.z - vw.pos.z));
        const float halfH = std::atan(std::tan(vw.vfovDeg * 0.5f * PI / 180.0f) * 16.0f / 9.0f);
        for (int side = -1; side <= 1; side += 2) {
            const float a = std::atan2(f.z, f.x) + side * halfH;
            for (float t = 0; t < 420.0f; t += 3.0f) disc(vw.pos.x + std::cos(a) * t, vw.pos.z + std::sin(a) * t, 1.0f, make_float3(1.0f, 0.4f, 0.3f), 0.8f);
        }
        disc(vw.pos.x, vw.pos.z, 7.0f, make_float3(1.0f, 0.3f, 0.2f), 1.0f);
    }
    disc(0.0f, 0.0f, 9.0f, make_float3(1.0f, 1.0f, 0.9f), 1.0f);
    disc(cabin.x, cabin.z, 6.0f, make_float3(1.0f, 0.6f, 0.15f), 1.0f);
    // labels (stb_easy_font quads, rasterised)
    auto label = [&](float x, float z, const char* text, float scale) {
        static char buf[20000];
        const int q = stb_easy_font_print(0, 0, const_cast<char*>(text), nullptr, buf, sizeof(buf));
        int cx, cy;
        toPx(x, z, cx, cy);
        for (int k = 0; k < q; ++k) {
            const float* v = reinterpret_cast<const float*>(buf + k * 4 * 16);
            const int qx0 = cx + int(v[0] * scale), qy0 = cy + int(v[1] * scale), qx1 = cx + int(v[8] * scale), qy1 = cy + int(v[9] * scale);
            for (int Y = std::max(0, qy0); Y < std::min(H, qy1); ++Y)
                for (int X = std::max(0, qx0); X < std::min(W, qx1); ++X) img[size_t(Y) * W + X] = make_float3(1.0f, 1.0f, 1.0f);
        }
    };
    label(15.0f, 25.0f, "FENER", 3.0f);
    label(cabin.x + 12.0f, cabin.z, "KULUBE", 2.5f);
    for (const FenerView& vw : s.views) label(vw.pos.x + 14.0f, vw.pos.z - 10.0f, vw.name.c_str(), 5.0f);
    label(x0 + 20.0f, z1 - 20.0f, "KUZEY YUKARI   1 px = 1.5 m   beyaz: A -> B yolculugu   koyu daireler: govdeli (ic) agaclar", 2.0f);
    std::vector<unsigned char> rgba(size_t(W) * H * 4);
    for (size_t i = 0; i < img.size(); ++i) {
        const float3 c = img[i];
        rgba[i * 4 + 0] = (unsigned char)(std::min(std::max(std::pow(c.x, 1 / 2.2f), 0.0f), 1.0f) * 255);
        rgba[i * 4 + 1] = (unsigned char)(std::min(std::max(std::pow(c.y, 1 / 2.2f), 0.0f), 1.0f) * 255);
        rgba[i * 4 + 2] = (unsigned char)(std::min(std::max(std::pow(c.z, 1 / 2.2f), 0.0f), 1.0f) * 255);
        rgba[i * 4 + 3] = 255;
    }
    return writePNG(path, W, H, rgba.data());
}
