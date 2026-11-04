// Scene 13 "Dağ": the ground around the terrace, where one elevation cell would be many pixels. Two polar meshes around the eye
// (their cells grow with the distance, so they cover about the same picture everywhere): the real ground underneath, the rock's
// breakup over it (fractal, ridged, stronger on steep ground, none on water), fading out where the far mesh takes over. What the
// terrace cannot see is left out. The terrace's flat, the railing.
#include "scenes/dag/dag_world.h"

#include <stb_image.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

struct DagDemSample {
    float h, water;
};

namespace {

void parallelFor(int n, const std::function<void(int)>& fn, int chunk = 1) {
    std::atomic<int> next{0};
    const int threads = std::max(1, int(std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (;;) {
                const int i0 = next.fetch_add(chunk);
                if (i0 >= n) return;
                for (int i = i0; i < std::min(i0 + chunk, n); ++i) fn(i);
            }
        });
    for (auto& t : pool) t.join();
}

inline float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline unsigned hash2(int x, int y) {
    unsigned h = unsigned(x) * 0x8DA6B343u ^ unsigned(y) * 0xD8163841u;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}
// gradient noise, about -1 .. 1
inline float gnoise(float x, float y) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const float fx = x - xi, fy = y - yi;
    const float u = fx * fx * fx * (fx * (fx * 6 - 15) + 10), v = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
    auto g = [](int ix, int iy, float dx, float dy) {
        const unsigned h = hash2(ix, iy);
        const float a = float(h & 0xFFFF) / 65535.0f * 6.2831853f;
        return dx * std::cos(a) + dy * std::sin(a);
    };
    const float n00 = g(xi, yi, fx, fy), n10 = g(xi + 1, yi, fx - 1, fy), n01 = g(xi, yi + 1, fx, fy - 1), n11 = g(xi + 1, yi + 1, fx - 1, fy - 1);
    return 1.5f * ((1 - v) * ((1 - u) * n00 + u * n10) + v * ((1 - u) * n01 + u * n11));
}

// jointed rock at one scale: the plane broken into blocks (the cells of jittered points), each block a tilted slab of its own, a crack
// along the joints between them. Granite breaks like this: flat faces, sharp edges, no smooth lumps
inline float blocks(float x, float z, float lambda, int seed) {
    const float px = x / lambda, pz = z / lambda;
    const int xi = int(std::floor(px)), zi = int(std::floor(pz));
    float d1 = 1e9f, d2 = 1e9f, fx1 = 0.0f, fz1 = 0.0f;
    unsigned h1 = 0;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            const unsigned h = hash2(xi + dx + seed * 7919, zi + dz - seed * 104729);
            const float jx = float(h & 0x3FF) / 1023.0f, jz = float((h >> 10) & 0x3FF) / 1023.0f;
            const float fx = float(xi + dx) + jx, fz = float(zi + dz) + jz;
            const float qx = fx - px, qz = fz - pz;
            const float d = qx * qx + qz * qz;
            if (d < d1) {
                d2 = d1;
                d1 = d;
                fx1 = fx;
                fz1 = fz;
                h1 = h;
            } else if (d < d2) {
                d2 = d;
            }
        }
    const float e = std::sqrt(d2) - std::sqrt(d1);    // towards the joint (in cells)
    const float a = (float((h1 >> 3) & 0xFF) / 255.0f - 0.5f) * 0.9f, b = (float((h1 >> 11) & 0xFF) / 255.0f - 0.5f) * 0.9f;
    const float c = float((h1 >> 19) & 0xFF) / 255.0f - 0.5f;
    const float slab = c * 0.42f + a * (px - fx1) + b * (pz - fz1);
    // (the slabs meet at the joint: a heightfield's walls would stretch; the crack shows the break)
    const float meet = std::clamp(e / 0.14f, 0.0f, 1.0f);
    const float crack = -0.10f * std::exp(-e / 0.05f);
    return lambda * (slab * meet * meet * (3.0f - 2.0f * meet) + crack);
}

// the rock's breakup (metres) at a point of the plane: strength 0 .. 1 from the slope, finest wavelength in metres
float rockDetail(float x, float z, float steep, float finest) {
    // a warp: rock does not break along lattice lines
    const float wx = 24.0f * gnoise(x / 73.0f + 11.3f, z / 73.0f - 4.1f) + 6.0f * gnoise(x / 17.0f - 7.7f, z / 17.0f + 2.9f);
    const float wz = 24.0f * gnoise(x / 73.0f - 3.3f, z / 73.0f + 8.7f) + 6.0f * gnoise(x / 17.0f + 5.1f, z / 17.0f - 9.3f);
    float sum = 0.0f;
    // the large forms: spurs and gullies
    float lambda = 150.0f, amp = 9.0f * (0.45f + 0.85f * steep);
    float px = x + wx, pz = z + wz;
    const float cr = 0.8f, sr = 0.6f;
    for (int oct = 0; oct < 2; ++oct) {
        if (lambda > finest) sum += amp * gnoise(px / lambda, pz / lambda);
        const float npx = cr * px - sr * pz + 31.7f, npz = sr * px + cr * pz - 17.3f;
        px = npx;
        pz = npz;
        lambda *= 0.5f;
        amp *= 0.5f;
    }
    // the joints: blocks within blocks, each scale riding on the larger
    float bl = 46.0f;
    float strength = 0.28f + 0.5f * steep;
    px = x + wx * 0.5f;
    pz = z + wz * 0.5f;
    for (int oct = 0; oct < 8; ++oct) {
        if (bl * 1.2f <= finest) break;
        // a block fades in as the mesh can hold it
        const float show = smooth(finest, finest * 2.5f, bl);
        sum += strength * show * blocks(px, pz, bl, oct);
        const float npx = cr * px + sr * pz - 13.1f, npz = -sr * px + cr * pz + 27.3f;
        px = npx;
        pz = npz;
        bl *= 0.42f;
        strength *= 0.9f;
    }
    return sum;
}

}   // namespace

// the fine scan's height, the same place as its colour and normals in device/dag.cu (triplanar's top face: x, z, turned), as levels
// of a pyramid: the mesh takes the level of its own cell
struct DispMap {
    std::vector<std::vector<float>> levels;
    int n = 0;
    float tile = 1.8f, cr = 1.0f, sr = 0.0f, texel = 0.0f;
    bool ok() const { return !levels.empty(); }
    float at(float x, float z, float cell) const {
        const float u = (cr * x - sr * z) / tile, v = (sr * x + cr * z) / tile;
        int L = 0;
        float t = texel;
        while (L + 1 < int(levels.size()) && t * 2.0f <= cell) {
            t *= 2.0f;
            ++L;
        }
        const int m = n >> L;
        const float fx = (u - std::floor(u)) * m - 0.5f, fy = (v - std::floor(v)) * m - 0.5f;
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        const float ax = fx - x0, ay = fy - y0;
        const auto P = [&](int a, int b) { return levels[L][size_t((b + m) % m) * m + (a + m) % m]; };
        return (1 - ay) * ((1 - ax) * P(x0, y0) + ax * P(x0 + 1, y0)) + ay * ((1 - ax) * P(x0, y0 + 1) + ax * P(x0 + 1, y0 + 1));
    }
};

static DispMap loadDisp(const std::string& path, float tile, float rot) {
    DispMap D;
    int w = 0, h = 0, c = 0;
    unsigned char* d = stbi_load(path.c_str(), &w, &h, &c, 1);
    if (!d || w != h) {
        if (d) stbi_image_free(d);
        return D;
    }
    D.tile = tile;
    D.cr = std::cos(rot);
    D.sr = std::sin(rot);
    // from 4096 down: the 8 bit source's steps are averaged out in its first level
    int m = w / 2;
    D.n = m;
    D.texel = tile / m;
    std::vector<float> base(size_t(m) * m);
    parallelFor(m, [&](int y) {
        for (int x = 0; x < m; ++x)
            base[size_t(y) * m + x] = (float(d[size_t(y * 2) * w + x * 2]) + d[size_t(y * 2) * w + x * 2 + 1] + d[size_t(y * 2 + 1) * w + x * 2] + d[size_t(y * 2 + 1) * w + x * 2 + 1]) / (4.0f * 255.0f);
    }, 16);
    stbi_image_free(d);
    D.levels.push_back(std::move(base));
    while (m > 64 && D.levels.size() < 8) {
        const int k = m / 2;
        std::vector<float> nxt(size_t(k) * k);
        const std::vector<float>& P = D.levels.back();
        for (int y = 0; y < k; ++y)
            for (int x = 0; x < k; ++x)
                nxt[size_t(y) * k + x] = 0.25f * (P[size_t(y * 2) * m + x * 2] + P[size_t(y * 2) * m + x * 2 + 1] + P[size_t(y * 2 + 1) * m + x * 2] + P[size_t(y * 2 + 1) * m + x * 2 + 1]);
        D.levels.push_back(std::move(nxt));
        m = k;
    }
    return D;
}

static float polyRadius(const std::vector<float2>& poly, float a) {
    // the distance from the centre to the polygon along azimuth a (x = sin a, z = -cos a); the polygon is star shaped around it
    const float dx = std::sin(a), dz = -std::cos(a);
    float best = 1e9f;
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const float2 p = poly[i], q = poly[(i + 1) % n];
        const float ex = q.x - p.x, ez = q.y - p.y;
        const float det = dx * ez - dz * ex;
        if (std::fabs(det) < 1e-6f) continue;
        const float t = (p.x * ez - p.y * ex) / det;
        const float u = (p.x * dz - p.y * dx) / det;
        if (t > 0.0f && u >= -1e-4f && u <= 1.0001f) best = std::min(best, t);
    }
    return best;
}

void dagAddTube(DagMeshCPU& m, float3 a, float3 b, float radius, int sides) {
    const float3 axis = normalize(b - a);
    const float3 helper = std::fabs(axis.y) < 0.9f ? make_float3(0, 1, 0) : make_float3(1, 0, 0);
    const float3 u = normalize(cross(axis, helper)), v = cross(axis, u);
    const unsigned base = unsigned(m.pos.size());
    for (int s = 0; s <= sides; ++s) {
        const float ph = s * 6.2831853f / sides;
        const float3 n = u * std::cos(ph) + v * std::sin(ph);
        for (int e = 0; e < 2; ++e) {
            m.pos.push_back((e == 0 ? a : b) + n * radius);
            m.nrm.push_back(n);
        }
    }
    for (int s = 0; s < sides; ++s) {
        const unsigned i = base + unsigned(s) * 2;
        m.idx.push_back(make_uint3(i, i + 2, i + 1));
        m.idx.push_back(make_uint3(i + 1, i + 2, i + 3));
    }
}

static void buildPolar(DagWorld& w, DagWorld::Polar& P, const std::vector<float2>* terrace, int nAz, float r0, float rOut, float fadeFrom, float fadeTo, float finestMin,
                       const std::function<DagDemSample(double, double, bool)>& demAt, bool cull, const DispMap* disp, float dispAmp, const DispMap* field, float fieldAmp) {
    const float q = 1.0f + 6.2831853f / nAz;
    const float logQ1 = std::log(q);
    const int nR = int(std::ceil(std::log(rOut / r0) / logQ1)) + 1;
    P.az = nAz;
    P.rings = nR;
    P.r0 = r0;
    P.rOut = rOut;
    P.logQ = logQ1 * (nR - 1);
    const float h0 = w.h0;
    std::vector<float> polyR;
    if (terrace) {
        polyR.resize(nAz);
        for (int a = 0; a < nAz; ++a) polyR[a] = polyRadius(*terrace, a * 6.2831853f / nAz);
    }
    P.pos.resize(size_t(nAz) * nR);
    std::vector<float> Y(size_t(nAz) * nR);
    const double Rd = w.Re;
    parallelFor(nR, [&](int j) {
        const float r = r0 * std::exp(logQ1 * j);
        const float cell = r * 6.2831853f / nAz;   // the mesh's own cell: finer breakup would not show in the geometry
        for (int a = 0; a < nAz; ++a) {
            const float ang = a * 6.2831853f / nAz;
            const float x = r * std::sin(ang), z = -r * std::cos(ang);
            const float e = 40.0f;
            const float hx0 = demAt(x - e, z, false).h, hx1 = demAt(x + e, z, false).h, hz0 = demAt(x, z - e, false).h, hz1 = demAt(x, z + e, false).h;
            const float sx = (hx1 - hx0) / (2 * e), sz = (hz1 - hz0) / (2 * e);
            const float slope = std::sqrt(sx * sx + sz * sz);
            const DagDemSample ds = demAt(x, z, slope < 0.08f);
            const float base = ds.h - h0;
            const float steep = std::clamp(slope * 0.8f, 0.0f, 1.0f);
            const float fade = 1.0f - smooth(fadeFrom, fadeTo, r);
            // forest and meadow lie soft over the rock: strong breakup only high up
            const float highUp = 0.35f + 0.65f * smooth(1500.0f, 2400.0f, ds.h);
            const float onLand = 1.0f - smooth(0.0f, 0.5f, ds.water);
            const float detail = rockDetail(x, z, steep, std::max(cell * 1.5f, finestMin)) * fade * highUp * onLand;
            float y = base + detail;
            // the boulder field: the fine scan at eight times its size, real relief (real shadows at this low sun); fades where a cell holds less than a boulder
            if (field && field->ok()) {
                const float boulder = (field->at(x, z, cell) - 0.4f) * fieldAmp * (0.45f + 0.55f * steep);
                y += boulder * fade * onLand * smooth(1700.0f, 2500.0f, ds.h) * smooth(12.0f, 60.0f, r) * (1.0f - smooth(field->tile * 0.08f, field->tile * 0.28f, cell));
            }
            if (terrace) {
                // the scree: the fine scan's stones in the geometry, close by
                const float stones = disp && disp->ok() ? (disp->at(x, z, cell) - 0.4f) * dispAmp : 0.0f;
                const float stoneFade = 1.0f - smooth(40.0f, 140.0f, r);
                // the terrace: flat with a little unevenness inside its polygon, the natural ground beyond a short shoulder
                const float d = r - polyR[a];
                const float flat = 1.0f - smooth(0.6f, 4.0f, d);
                const float top = 0.06f * gnoise(x / 2.1f, z / 2.1f) + 0.02f * gnoise(x / 0.7f, z / 0.7f) - 0.02f;
                // around the terrace the rock stays at most a little over the floor, here and there an outcrop; it can fall away as far as it likes
                const float crop = gnoise(ang * 3.1f + 5.0f, 0.3f) * 0.6f + gnoise(ang * 9.7f, 1.7f) * 0.4f;
                const float limit = 0.4f + 0.012f * d + std::max(crop, 0.0f) * 1.3f * smooth(0.5f, 5.0f, d);
                // (over the limit the rock keeps a share of its relief: a clipped top would be a smooth dome)
                const float nat2 = y <= limit ? y : limit + (y - limit) * 0.12f;
                y = flat * top + (1.0f - flat) * nat2 + stones * stoneFade;
            }
            Y[size_t(j) * nAz + a] = y;
            // on the planet's curve: the same formula as the far mesh's vertices
            const double th = double(r) / Rd;
            const double half = std::sin(th * 0.5);
            const double aa = Rd + double(y) + double(h0);
            const double kk = aa * std::sin(th) / double(r);
            P.pos[size_t(j) * nAz + a] = make_float3(float(x * kk), float(double(y) - aa * 2.0 * half * half), float(z * kk));
        }
    }, 8);
    // the triangles: what the terrace can see of them, with a margin for walking round it
    std::vector<uint8_t> keep;
    size_t kept = 0;
    if (cull) {
        keep.assign(size_t(nAz) * nR, 0);
        parallelFor(nAz, [&](int a) {
            const float ang = a * 6.2831853f / nAz;
            const float dx = std::sin(ang), dz = -std::cos(ang);
            // what stands between the terrace and the mesh's first ring
            float best = -1e9f;
            for (float s = 30.0f; s < r0; s *= 1.02f) {
                const float hh = demAt(dx * s, dz * s, false).h - h0;
                best = std::max(best, (hh - 1.7f - 25.0f) / s);
            }
            for (int j = 0; j < nR; ++j) {
                const float3 p = P.pos[size_t(j) * nAz + a];
                const float rr = std::sqrt(p.x * p.x + p.z * p.z);
                const float slope = (p.y - 1.7f) / rr;
                if (slope >= best - 0.003f) keep[size_t(j) * nAz + a] = 1;
                best = std::max(best, slope - 12.0f / rr);
            }
        }, 16);
    }
    P.idx.clear();
    P.idx.reserve(cull ? size_t(nAz) * nR / 2 : size_t(nAz) * (nR - 1) * 2 + nAz);
    for (int j = 0; j + 1 < nR; ++j)
        for (int a = 0; a < nAz; ++a) {
            if (cull) {
                // the cell and a margin of cells around it
                bool any = false;
                for (int dj = -4; dj <= 5 && !any; ++dj) {
                    const int jj = j + dj;
                    if (jj < 0 || jj >= nR) continue;
                    for (int da = -4; da <= 5; ++da)
                        if (keep[size_t(jj) * nAz + ((a + da + nAz) % nAz)]) {
                            any = true;
                            break;
                        }
                }
                if (!any) continue;
                ++kept;
            }
            const unsigned i00 = unsigned(j * nAz + a), i10 = unsigned(j * nAz + (a + 1) % nAz), i01 = unsigned((j + 1) * nAz + a), i11 = unsigned((j + 1) * nAz + (a + 1) % nAz);
            P.idx.push_back(make_uint3(i00, i01, i11));
            P.idx.push_back(make_uint3(i00, i11, i10));
        }
    if (!cull) {
        // the centre's small hole is under the eye's feet: covered by a fan
        const unsigned centre = unsigned(P.pos.size());
        P.pos.push_back(make_float3(0, Y[0], 0));
        for (int a = 0; a < nAz; ++a) P.idx.push_back(make_uint3(centre, unsigned(a), unsigned((a + 1) % nAz)));
    }
    // the polar normal map: central differences of the mesh
    P.info.assign(size_t(nAz) * nR * 4, 0);
    parallelFor(nR, [&](int j) {
        const int ja = std::max(j - 1, 0), jb = std::min(j + 1, nR - 1);
        for (int a = 0; a < nAz; ++a) {
            const int aa = (a + nAz - 1) % nAz, ab = (a + 1) % nAz;
            const float3 pa = P.pos[size_t(j) * nAz + aa], pb = P.pos[size_t(j) * nAz + ab];
            const float3 pc = P.pos[size_t(ja) * nAz + a], pd = P.pos[size_t(jb) * nAz + a];
            float3 n = normalize(cross(pb - pa, pc - pd));
            if (n.y < 0.0f) n = -n;
            uint16_t* o = &P.info[(size_t(j) * nAz + a) * 4];
            auto code = [](float v) { return uint16_t(std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f) * 65535.0f + 0.5f); };
            o[0] = code(n.x);
            o[1] = code(n.z);
            o[2] = 65535;
            o[3] = 65535;
        }
    }, 8);
    if (cull) std::printf("dag: orta halka: %zu / %zu hucre gorunur\n", kept, size_t(nAz) * (nR - 1));
}

void dagBuildNear(DagWorld& w, const Config& cfg, const std::function<DagDemSample(double, double, bool)>& demAt) {
    // the terrace: a small uneven polygon on the top
    w.terrace.clear();
    const float rad[10] = {8.6f, 9.8f, 8.4f, 7.2f, 8.2f, 10.0f, 9.0f, 7.6f, 8.0f, 9.2f};
    for (int k = 0; k < 10; ++k) {
        const float a = k * 6.2831853f / 10.0f + 0.15f;
        w.terrace.push_back(make_float2(rad[k] * std::sin(a), -rad[k] * std::cos(a)));
    }
    w.terraceRadius = 10.0f;
    const float rNear = cfg.f("near.radius", 1500.0f);
    w.nearRadius = rNear;
    w.midRadius = cfg.f("mid.radius", 30000.0f);
    // the breakup's finest wavelength where the two meshes meet is the same in both: they coincide there
    const DispMap disp = loadDisp(w.rootPath() + "/assets/dag/cc0/kayaB_disp.jpg", cfg.f("rock.tile.fine", 1.8f), 0.33f);
    DispMap field = disp;
    field.tile = cfg.f("rock.tile.field", 14.4f);
    field.cr = std::cos(1.13f);
    field.sr = std::sin(1.13f);
    field.texel = field.tile / field.n;
    const float fieldAmp = cfg.f("rock.field", 1.2f);
    buildPolar(w, w.polar[0], &w.terrace, cfg.i("near.az", 4096), cfg.f("near.r0", 0.25f), rNear, 1e9f, 2e9f, cfg.f("near.finest", 0.03f), demAt, false, &disp, cfg.f("rock.disp", 0.2f), &field, fieldAmp);
    buildPolar(w, w.polar[1], nullptr, cfg.i("mid.az", 8192), rNear - 30.0f, w.midRadius, w.midRadius * 0.75f, w.midRadius * 0.97f, 2.0f, demAt, true, nullptr, 0.0f, &field, fieldAmp);

    // the railing: posts at every corner and at most kSpan apart, a top rail and a middle rail following the ground
    DagMeshCPU rail;
    rail.tint = cfg.v3("rail.tint", make_float3(0.24f, 0.25f, 0.27f));
    rail.rough = cfg.f("rail.rough", 0.62f);
    rail.material = 2;
    rail.id = 0;
    const float kSpan = 1.6f;
    const float kTop = 1.08f, kMid = 0.58f;
    auto groundAt = [&](float x, float z) { return w.groundY(x, z); };
    for (size_t e = 0; e < w.terrace.size(); ++e) {
        const float2 p = w.terrace[e], q2 = w.terrace[(e + 1) % w.terrace.size()];
        const float ex = q2.x - p.x, ez = q2.y - p.y;
        const float len = std::sqrt(ex * ex + ez * ez);
        const int spans = std::max(1, int(std::ceil(len / kSpan)));
        float3 prevTop{}, prevMid{};
        for (int s = 0; s <= spans; ++s) {
            const float t = float(s) / spans;
            const float x = p.x + ex * t, z = p.y + ez * t;
            const float g = groundAt(x, z);
            const float3 baseP = make_float3(x, g - 0.35f, z);
            const float3 topP = make_float3(x, std::max(g, 0.0f) + kTop, z), midP = make_float3(x, std::max(g, 0.0f) + kMid, z);
            dagAddTube(rail, baseP, topP, 0.024f, 12);
            // a flange at the foot
            dagAddTube(rail, make_float3(x, g - 0.01f, z), make_float3(x, g + 0.015f, z), 0.07f, 16);
            if (s > 0) {
                dagAddTube(rail, prevTop, topP, 0.021f, 12);
                dagAddTube(rail, prevMid, midP, 0.013f, 10);
            }
            prevTop = topP;
            prevMid = midP;
        }
    }
    w.meshes.push_back(std::move(rail));
}

// the ground's height from the near polar mesh
float DagWorld::groundY(float x, float z) const {
    const Polar& P = polar[0];
    if (P.az == 0) return 0.0f;
    const float r = std::sqrt(x * x + z * z);
    const float logQ = P.logQ / (P.rings - 1);
    const float fj = std::clamp(std::log(std::max(r, P.r0) / P.r0) / logQ, 0.0f, float(P.rings - 1) - 0.001f);
    float ang = std::atan2(x, -z);
    if (ang < 0) ang += 6.2831853f;
    const float fa = ang / 6.2831853f * P.az;
    const int j = int(fj), a0 = int(fa) % P.az, a1 = (a0 + 1) % P.az;
    const float tj = fj - j, ta = fa - std::floor(fa);
    const auto Y = [&](int jj, int aa) { return P.pos[size_t(jj) * P.az + aa].y; };
    return (1 - tj) * ((1 - ta) * Y(j, a0) + ta * Y(j, a1)) + tj * ((1 - ta) * Y(j + 1, a0) + ta * Y(j + 1, a1));
}

bool DagWorld::inside(float x, float z, float margin) const {
    if (terrace.empty()) return true;
    const float r = std::sqrt(x * x + z * z);
    return r + margin <= polyRadius(terrace, std::atan2(x, -z));
}

void DagWorld::keepIn(float& x, float& z, float margin) const {
    if (terrace.empty()) return;
    // the inward normal of each edge: pushed back inside whichever edge it crosses (a convex-enough polygon)
    const size_t n = terrace.size();
    for (int pass = 0; pass < 3; ++pass) {
        bool moved = false;
        for (size_t i = 0; i < n; ++i) {
            const float2 p = terrace[i], q = terrace[(i + 1) % n];
            const float ex = q.x - p.x, ez = q.y - p.y;
            const float len = std::sqrt(ex * ex + ez * ez);
            float nx = ez / len, nz = -ex / len;
            // towards the centre
            if (nx * -p.x + nz * -p.y < 0) {
                nx = -nx;
                nz = -nz;
            }
            const float d = (x - p.x) * nx + (z - p.y) * nz;   // positive inside
            if (d < margin) {
                // only when the point lies along this edge (a star polygon's other edges are handled by theirs)
                const float t = ((x - p.x) * ex + (z - p.y) * ez) / (len * len);
                if (t > -0.3f && t < 1.3f) {
                    x += nx * (margin - d);
                    z += nz * (margin - d);
                    moved = true;
                }
            }
        }
        if (!moved) break;
    }
}
