// Scene 5 "Koy", Amfora: the limestone arch (see koy_arch.h).
#include "scenes/koy/koy_arch.h"

#include "scenes/koy/koy_noise.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Local {
    float x, y, z;
};

// distance in the arch's plane from (x, y) to its centre line, and where along it (s in 0..1; < 0 and > 1 the feet's
// vertical runs down into the sand). The line: a half-ellipse x = -w cos(pi s) + lean sin(pi s), y = h sin(pi s)^flat
// (a flatter top for flat < 1), sampled.
struct LineHit {
    float d, s;
};
LineHit toLine(float x, float y, float w, float h, float lean, float flat) {
    constexpr int K = 48;
    float best = 1e30f, bs = 0.0f;
    float px = -w, py = 0.0f;
    {   // the left foot's run below the base: from (-w, -6) up to (-w, 0)
        const float yy = std::min(std::max(y, -6.0f), 0.0f);
        const float d2 = (x + w) * (x + w) + (y - yy) * (y - yy);
        if (d2 < best) { best = d2; bs = yy / 6.0f; }   // s < 0 below the left foot
    }
    for (int k = 1; k <= K; ++k) {
        const float s = float(k) / K;
        const float sn = std::sin(3.14159265f * s);
        const float qx = -w * std::cos(3.14159265f * s) + lean * sn, qy = h * std::pow(std::max(sn, 0.0f), flat);
        const float ex = qx - px, ey = qy - py;
        const float t = std::min(std::max(((x - px) * ex + (y - py) * ey) / (ex * ex + ey * ey), 0.0f), 1.0f);
        const float dx = x - (px + ex * t), dy = y - (py + ey * t);
        const float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; bs = (float(k - 1) + t) / K; }
        px = qx;
        py = qy;
    }
    {
        const float yy = std::min(std::max(y, -6.0f), 0.0f);
        const float d2 = (x - w) * (x - w) + (y - yy) * (y - yy);
        if (d2 < best) { best = d2; bs = 1.0f - yy / 6.0f; }   // s > 1 below the right foot
    }
    return {std::sqrt(best), bs};
}

Local toLocal(const KoyArchShape& a, float3 p) {
    const float dx = p.x - a.base.x, dz = p.z - a.base.z;
    const float c = std::cos(a.yaw), s = std::sin(a.yaw);
    return {c * dx + s * dz, p.y - a.base.y, -s * dx + c * dz};
}

float smin(float a, float b, float k) {   // smooth union (polynomial)
    const float h = std::max(k - std::fabs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

}   // namespace

// The base shape (no relief): an elliptic tube about the centre line, thick at the feet and thinner over the top, the
// left foot heavier than the right; the eroded ridge it stands in, running out from both feet and sinking into the
// sand; and blocks fallen from it.
static float baseSdf(const KoyArchShape& a, float x, float y, float z) {
    const float w = 0.5f * a.span;
    const LineHit L = toLine(x, y, w, a.rise, a.lean, a.flat);
    const float sc = std::min(std::max(L.s, 0.0f), 1.0f);
    const float foot = std::pow(std::fabs(std::cos(3.14159265f * sc)), 1.4f);   // 1 at the feet, 0 at the top
    const float side = sc < 0.5f ? 1.0f + a.asym : 1.0f - a.asym;
    float r = (a.topR + (a.footR * side - a.topR) * foot), dd = a.topD + (a.footD * side - a.topD) * foot;
    if (L.s < 0.0f || L.s > 1.0f) {   // the feet flare a little into the sand
        r *= 1.12f;
        dd *= 1.08f;
    }
    const float e = std::sqrt((L.d / r) * (L.d / r) + (z / dd) * (z / dd));
    float d = (e - 1.0f) * std::min(r, dd);
    // the ridge: from each foot outwards along x, an elongated hump falling from spurH to nothing over spurLen
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        const float fx = sgn * w;                       // the foot's centre
        const float u = (x - fx) * float(sgn);         // outwards from the foot
        if (u < -3.0f) continue;
        const float t = std::min(std::max(u / a.spurLen, 0.0f), 1.0f);
        const float hh = a.spurH * (1.0f - t * t) * (sgn < 0 ? 1.0f : 0.75f);   // its crest's height there
        const float half = (a.footD * 0.9f) * (1.0f - 0.55f * t);               // its half-width (along z)
        // a rounded ridge: its section an ellipse (half-width by its height), its end a rounded cap
        const float ey = std::max(hh, 0.3f);
        const float sx = u > a.spurLen ? (u - a.spurLen) : (u < 0.0f ? -u : 0.0f);   // rounded caps at both ends
        const float es = std::sqrt((z / half) * (z / half) + (std::max(y, 0.0f) / ey) * (std::max(y, 0.0f) / ey) + (sx / 1.5f) * (sx / 1.5f));
        const float ds = (es - 1.0f) * std::min(half, ey);
        d = smin(d, ds, 1.6f);
    }
    // fallen blocks: rough spheres about the feet, half sunk
    for (int b = 0; b < a.blocks; ++b) {
        const float hx = kn::hash01(b, int(a.seed), 21), hz = kn::hash01(b, int(a.seed), 22), hr = kn::hash01(b, int(a.seed), 23);
        const float bx = (hx - 0.5f) * 2.0f * (w + 6.0f), bz = (hz - 0.5f) * 2.0f * (a.footD + 6.0f) * (hz < 0.5f ? 1.0f : 0.7f);
        if (std::fabs(bz) < a.footD + 0.5f && std::fabs(bx) < w - a.footR) continue;   // not under the span itself
        const float br = 0.7f + 1.6f * hr;
        const float dx = x - bx, dy = y - 0.25f * br, dz = z - bz;
        d = std::min(d, std::sqrt(dx * dx + dy * dy * 1.6f + dz * dz) - br);
    }
    return d;
}

float KoyArchShape::sdf(float3 world) const {
    const Local l = toLocal(*this, world);
    const float o = float(seed) * 13.7f;
    // low warp: the whole shape bent by a metre or so of noise, so it is no drawn ellipse
    const float f = 0.09f;
    const float wx = warp * kn::fbm3(l.x * f + o, l.y * f, l.z * f, 3);
    const float wy = 0.6f * warp * kn::fbm3(l.x * f, l.y * f + o + 7.1f, l.z * f, 3);
    const float wz = warp * kn::fbm3(l.x * f, l.y * f, l.z * f + o + 3.3f, 3);
    const float x = l.x + wx, y = l.y + wy, z = l.z + wz;
    float d = baseSdf(*this, x, y, z);
    // far from the surface the relief cannot change the sign: the base distance is enough (the surface nets need the
    // sign everywhere, the exact value only near the surface)
    if (std::fabs(d) > 2.5f * (bump + bedStep) + 0.8f) return d;
    // blocks: limestone splits along joints (vertical) and bedding (horizontal) into blocks; the sea opened the joints
    // into crevices and every block weathered on its own, standing out or set back a little (cells wider than tall)
    {
        const kn::Cell cb = kn::cellular3(x / 4.5f + o, y / 2.2f, z / 4.5f, seed);
        const float crack = cb.f2 - cb.f1;   // small on a boundary between two blocks
        d += 0.09f * (1.0f - kn::smoothstep(0.0f, 0.035f, crack));
        d -= 0.32f * (float(unsigned(cb.id) & 0xFFFu) / 4095.0f - 0.5f) * kn::smoothstep(0.0f, 0.05f, crack);
    }
    // limestone beds: tilted and wavy planes; a few of them stand out as ledges or are cut back as grooves (most bedding
    // planes are tight and show only where the blocks end)
    const float by = (y + 0.08f * x + 0.6f * kn::noise3(x * 0.06f + o, 2.1f, z * 0.06f)) / bed;
    const int layer = int(std::floor(by));
    const float fr = by - float(layer);
    const float patch = kn::smoothstep(-0.1f, 0.45f, kn::fbm3(x * 0.16f + o, y * 0.05f, z * 0.16f, 3));
    const float hl = kn::hash01(layer, int(seed), 11);
    const float step = hl > 0.62f ? bedStep * (hl - 0.62f) / 0.38f * 2.0f : 0.0f;   // a protruding ledge, some beds only
    d -= patch * step * (1.0f - kn::smoothstep(0.75f, 1.0f, fr)) * kn::smoothstep(0.0f, 0.15f, fr);
    if (kn::hash01(layer, int(seed), 12) > 0.55f)   // a groove at this bedding plane
        d += patch * 0.06f * (1.0f - kn::smoothstep(0.0f, 0.07f, std::min(fr, 1.0f - fr)));
    // mid relief and eroded creases
    d += bump * kn::fbm3(x * 0.45f + o, y * 0.5f, z * 0.45f, 6);
    d -= 0.4f * bump * (kn::ridged3(x * 0.3f, y * 0.3f + o, z * 0.3f, 4) - 0.5f);
    return d;
}

float KoyArchShape::bound() const {
    return 0.5f * span + spurLen + std::max(footR, topR) * 1.2f + warp * 2.0f + 4.0f;
}

KoyArchShape koyArchShape(const Config& cfg, float floorY) {
    KoyArchShape a;
    const float3 c = cfg.v3("arch.centre", make_float3(3.0f, 0.0f, -33.0f));
    a.base = make_float3(c.x, floorY + cfg.f("arch.lift", 0.0f), c.z);
    a.yaw = cfg.f("arch.yaw_deg", 8.0f) * 3.14159265f / 180.0f;
    a.span = cfg.f("arch.span", 15.0f);
    a.rise = cfg.f("arch.rise", 12.6f);
    a.footR = cfg.f("arch.foot_r", 3.0f);
    a.topR = cfg.f("arch.top_r", 2.6f);
    a.footD = cfg.f("arch.foot_d", 4.2f);
    a.topD = cfg.f("arch.top_d", 2.8f);
    a.warp = cfg.f("arch.warp", 1.1f);
    a.bump = cfg.f("arch.bump", 0.32f);
    a.bed = cfg.f("arch.bed", 0.7f);
    a.bedStep = cfg.f("arch.bed_step", 0.22f);
    a.asym = cfg.f("arch.asym", 0.15f);
    a.lean = cfg.f("arch.lean", 1.2f);
    a.flat = cfg.f("arch.flat", 0.8f);
    a.spurLen = cfg.f("arch.spur_len", 11.0f);
    a.spurH = cfg.f("arch.spur_h", 6.0f);
    a.blocks = cfg.i("arch.blocks", 7);
    a.seed = unsigned(cfg.i("arch.seed", 3));
    return a;
}

// Surface nets: one vertex per cell the surface crosses (the mean of its edges' crossings), one quad per grid edge the
// surface crosses (joining the four cells around it). Watertight, welded by construction.
KMeshCPU koyBuildArch(const KoyArchShape& a, float cell, unsigned int material, unsigned int id, std::string* log) {
    const auto t0 = std::chrono::steady_clock::now();
    KMeshCPU m;
    m.name = "kemer";
    m.material = material;
    m.id = id;
    // the box in the arch's local frame, turned into world points per sample
    const float lx0 = -(0.5f * a.span + std::max(a.footR * 1.4f, a.spurLen + 1.6f) + a.warp * 2.0f + 2.0f), lx1 = -lx0;
    const float ly0 = -2.5f, ly1 = a.rise + a.topR + a.warp * 1.5f + 1.5f;
    const float lz0 = -(a.footD * 1.2f + a.warp * 2.0f + 7.0f), lz1 = -lz0;   // + the fallen blocks
    const int nx = int(std::ceil((lx1 - lx0) / cell)), ny = int(std::ceil((ly1 - ly0) / cell)), nz = int(std::ceil((lz1 - lz0) / cell));
    const size_t px = size_t(nx) + 1, py = size_t(ny) + 1, pz = size_t(nz) + 1;
    const float c = std::cos(a.yaw), s = std::sin(a.yaw);
    auto world = [&](float lx, float ly, float lz) {   // local -> world (inverse of toLocal)
        return make_float3(a.base.x + c * lx - s * lz, a.base.y + ly, a.base.z + s * lx + c * lz);
    };
    // narrow band: the field first on a grid 8 cells coarse; a fine point whose coarse cell lies wholly on one side,
    // farther from the surface than that cell is wide (with room for the field not being an exact distance), takes the
    // coarse cell's interpolated value (its sign is all the surface nets need). Only the band about the surface is
    // evaluated in full: most of the box is water or solid rock.
    const int C8 = 8;
    const size_t cx = size_t(nx / C8) + 2, cy = size_t(ny / C8) + 2, cz = size_t(nz / C8) + 2;
    std::vector<float> fc(cx * cy * cz);
    kn::parallelFor(int(cz), [&](int k) {
        for (size_t j = 0; j < cy; ++j)
            for (size_t i = 0; i < cx; ++i)
                fc[(size_t(k) * cy + j) * cx + i] = a.sdf(world(lx0 + i * C8 * cell, ly0 + j * C8 * cell, lz0 + k * C8 * cell));
    });
    const float safe = 1.6f * float(C8) * cell * 1.7320508f;   // the coarse cell's diagonal, with room
    std::vector<float> f(px * py * pz);
    std::atomic<size_t> evaluated{0};
    kn::parallelFor(int(pz), [&](int k) {
        size_t n = 0;
        for (size_t j = 0; j < py; ++j)
            for (size_t i = 0; i < px; ++i) {
                const size_t ci = i / C8, cj = j / C8, ck = size_t(k) / C8;
                float lo = 1e30f, hi = -1e30f, v[8];
                for (int q = 0; q < 8; ++q) {
                    v[q] = fc[((ck + ((q >> 2) & 1)) * cy + cj + ((q >> 1) & 1)) * cx + ci + (q & 1)];
                    lo = std::min(lo, v[q]);
                    hi = std::max(hi, v[q]);
                }
                float val;
                if (lo > safe || hi < -safe) {
                    const float tx = float(i % C8) / C8, ty = float(j % C8) / C8, tz = float(size_t(k) % C8) / C8;
                    auto L = [](float p, float q, float t) { return p + (q - p) * t; };
                    val = L(L(L(v[0], v[1], tx), L(v[2], v[3], tx), ty), L(L(v[4], v[5], tx), L(v[6], v[7], tx), ty), tz);
                } else {
                    val = a.sdf(world(lx0 + i * cell, ly0 + j * cell, lz0 + k * cell));
                    ++n;
                }
                f[(size_t(k) * py + j) * px + i] = val;
            }
        evaluated += n;
    });
    auto F = [&](size_t i, size_t j, size_t k) { return f[(k * py + j) * px + i]; };
    // cell vertices
    std::vector<int> cv(size_t(nx) * ny * nz, -1);
    std::vector<std::vector<float3>> partPos(nz);
    std::vector<std::vector<size_t>> partCell(nz);
    static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    kn::parallelFor(nz, [&](int k) {
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                float v[8];
                int inside = 0;
                for (int q = 0; q < 8; ++q) {
                    v[q] = F(size_t(i + (q & 1)), size_t(j + ((q >> 1) & 1)), size_t(k + ((q >> 2) & 1)));
                    inside += v[q] < 0.0f;
                }
                if (inside == 0 || inside == 8) continue;
                float sx = 0, sy = 0, sz = 0;
                int n = 0;
                for (const auto& e : E) {
                    const float va = v[e[0]], vb = v[e[1]];
                    if ((va < 0.0f) == (vb < 0.0f)) continue;
                    const float t = va / (va - vb);
                    const int qa = e[0], qb = e[1];
                    sx += float(qa & 1) + (float(qb & 1) - float(qa & 1)) * t;
                    sy += float((qa >> 1) & 1) + (float((qb >> 1) & 1) - float((qa >> 1) & 1)) * t;
                    sz += float((qa >> 2) & 1) + (float((qb >> 2) & 1) - float((qa >> 2) & 1)) * t;
                    ++n;
                }
                partPos[k].push_back(make_float3(lx0 + (i + sx / n) * cell, ly0 + (j + sy / n) * cell, lz0 + (k + sz / n) * cell));
                partCell[k].push_back((size_t(k) * ny + j) * nx + i);
            }
    });
    for (int k = 0; k < nz; ++k)
        for (size_t q = 0; q < partPos[k].size(); ++q) {
            cv[partCell[k][q]] = int(m.pos.size());
            const float3 l = partPos[k][q];
            m.pos.push_back(world(l.x, l.y, l.z));
        }
    // normals: the field's gradient at each vertex
    m.nrm.resize(m.pos.size());
    kn::parallelFor(int((m.pos.size() + 1023) / 1024), [&](int b) {
        const size_t e0 = size_t(b) * 1024, e1 = std::min(m.pos.size(), e0 + 1024);
        const float h = 0.5f * cell;
        for (size_t v = e0; v < e1; ++v) {
            const float3 p = m.pos[v];
            const float gx = a.sdf(make_float3(p.x + h, p.y, p.z)) - a.sdf(make_float3(p.x - h, p.y, p.z));
            const float gy = a.sdf(make_float3(p.x, p.y + h, p.z)) - a.sdf(make_float3(p.x, p.y - h, p.z));
            const float gz = a.sdf(make_float3(p.x, p.y, p.z + h)) - a.sdf(make_float3(p.x, p.y, p.z - h));
            const float3 g = make_float3(gx, gy, gz);
            const float L = length(g);
            m.nrm[v] = L > 1e-8f ? g / L : make_float3(0.0f, 1.0f, 0.0f);
        }
    });
    // quads across the grid edges the surface crosses; the outward side is where f > 0
    auto C = [&](int i, int j, int k) { return cv[(size_t(k) * ny + j) * nx + i]; };
    auto quad = [&](int q0, int q1, int q2, int q3, bool flip) {
        if (q0 < 0 || q1 < 0 || q2 < 0 || q3 < 0) return;
        if (flip) std::swap(q1, q3);
        // split along the shorter diagonal
        const float d02 = length(m.pos[size_t(q0)] - m.pos[size_t(q2)]), d13 = length(m.pos[size_t(q1)] - m.pos[size_t(q3)]);
        if (d02 <= d13) {
            m.idx.push_back(make_uint3(unsigned(q0), unsigned(q1), unsigned(q2)));
            m.idx.push_back(make_uint3(unsigned(q0), unsigned(q2), unsigned(q3)));
        } else {
            m.idx.push_back(make_uint3(unsigned(q0), unsigned(q1), unsigned(q3)));
            m.idx.push_back(make_uint3(unsigned(q1), unsigned(q2), unsigned(q3)));
        }
    };
    for (int k = 1; k < nz; ++k)
        for (int j = 1; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {   // x edges
                const float va = F(size_t(i), size_t(j), size_t(k)), vb = F(size_t(i + 1), size_t(j), size_t(k));
                if ((va < 0.0f) == (vb < 0.0f)) continue;
                quad(C(i, j - 1, k - 1), C(i, j, k - 1), C(i, j, k), C(i, j - 1, k), va > 0.0f);   // wound outwards (f > 0 outside)
            }
    for (int k = 1; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 1; i < nx; ++i) {   // y edges
                const float va = F(size_t(i), size_t(j), size_t(k)), vb = F(size_t(i), size_t(j + 1), size_t(k));
                if ((va < 0.0f) == (vb < 0.0f)) continue;
                quad(C(i - 1, j, k - 1), C(i - 1, j, k), C(i, j, k), C(i, j, k - 1), va > 0.0f);   // wound outwards (f > 0 outside)
            }
    for (int k = 0; k < nz; ++k)
        for (int j = 1; j < ny; ++j)
            for (int i = 1; i < nx; ++i) {   // z edges
                const float va = F(size_t(i), size_t(j), size_t(k)), vb = F(size_t(i), size_t(j), size_t(k + 1));
                if ((va < 0.0f) == (vb < 0.0f)) continue;
                quad(C(i - 1, j - 1, k), C(i, j - 1, k), C(i, j, k), C(i - 1, j, k), va > 0.0f);   // wound outwards (f > 0 outside)
            }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "kemer: %dx%dx%d hucre (%.0f mm, %.1f%% tam hesap), %zu kose, %zu ucgen, %.0f ms", nx, ny, nz, cell * 1000.0f,
                  100.0 * double(evaluated.load()) / double(px * py * pz), m.pos.size(), m.idx.size(), ms);
    if (log) *log = buf;
    return m;
}
