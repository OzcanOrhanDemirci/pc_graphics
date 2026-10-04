// Scene 5 "Koy": surface nets over a narrow band (see koy_sdf.h).
#include "scenes/koy/koy_sdf.h"

#include "scenes/koy/koy_noise.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

KMeshCPU koyBuildSdfMesh(const std::function<float(float3)>& sdf, const KoySdfBox& box, const char* name, unsigned int material,
                         unsigned int id, std::string* log) {
    const auto t0 = std::chrono::steady_clock::now();
    KMeshCPU m;
    m.name = name;
    m.material = material;
    m.id = id;
    const float cell = box.cell;
    const float3 lo = box.lo;
    const int nx = int(std::ceil((box.hi.x - lo.x) / cell)), ny = int(std::ceil((box.hi.y - lo.y) / cell)), nz = int(std::ceil((box.hi.z - lo.z) / cell));
    const size_t px = size_t(nx) + 1, py = size_t(ny) + 1, pz = size_t(nz) + 1;
    auto at = [&](size_t i, size_t j, size_t k) { return make_float3(lo.x + i * cell, lo.y + j * cell, lo.z + k * cell); };
    // the narrow band: the field on a grid 8 cells coarse; a fine point whose coarse cell lies wholly on one side, farther
    // from the surface than the cell is wide (with room for the field not being a true distance), takes the coarse
    // cell's interpolated value (its sign is all the surface nets need)
    const int C8 = 8;
    const size_t cx = size_t(nx / C8) + 2, cy = size_t(ny / C8) + 2, cz = size_t(nz / C8) + 2;
    std::vector<float> fc(cx * cy * cz);
    kn::parallelFor(int(cz), [&](int k) {
        for (size_t j = 0; j < cy; ++j)
            for (size_t i = 0; i < cx; ++i) fc[(size_t(k) * cy + j) * cx + i] = sdf(at(i * C8, j * C8, size_t(k) * C8));
    });
    const float safe = box.slope * float(C8) * cell * 1.7320508f + box.reach;
    std::vector<float> f(px * py * pz);
    std::atomic<size_t> evaluated{0};
    kn::parallelFor(int(pz), [&](int k) {
        size_t n = 0;
        for (size_t j = 0; j < py; ++j)
            for (size_t i = 0; i < px; ++i) {
                const size_t ci = i / C8, cj = j / C8, ck = size_t(k) / C8;
                float l = 1e30f, h = -1e30f, v[8];
                for (int q = 0; q < 8; ++q) {
                    v[q] = fc[((ck + ((q >> 2) & 1)) * cy + cj + ((q >> 1) & 1)) * cx + ci + (q & 1)];
                    l = std::min(l, v[q]);
                    h = std::max(h, v[q]);
                }
                float val;
                if (l > safe || h < -safe) {
                    const float tx = float(i % C8) / C8, ty = float(j % C8) / C8, tz = float(size_t(k) % C8) / C8;
                    auto L = [](float p, float q, float t) { return p + (q - p) * t; };
                    val = L(L(L(v[0], v[1], tx), L(v[2], v[3], tx), ty), L(L(v[4], v[5], tx), L(v[6], v[7], tx), ty), tz);
                } else {
                    val = sdf(at(i, j, size_t(k)));
                    ++n;
                }
                f[(size_t(k) * py + j) * px + i] = val;
            }
        evaluated += n;
    });
    auto F = [&](size_t i, size_t j, size_t k) { return f[(k * py + j) * px + i]; };
    // a vertex in every cell the surface crosses: the mean of its edges' crossings
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
                partPos[k].push_back(make_float3(lo.x + (i + sx / n) * cell, lo.y + (j + sy / n) * cell, lo.z + (k + sz / n) * cell));
                partCell[k].push_back((size_t(k) * ny + j) * nx + i);
            }
    });
    for (int k = 0; k < nz; ++k)
        for (size_t q = 0; q < partPos[k].size(); ++q) {
            cv[partCell[k][q]] = int(m.pos.size());
            m.pos.push_back(partPos[k][q]);
        }
    // normals: the field's gradient at each vertex
    m.nrm.resize(m.pos.size());
    kn::parallelFor(int((m.pos.size() + 1023) / 1024), [&](int b) {
        const size_t e0 = size_t(b) * 1024, e1 = std::min(m.pos.size(), e0 + 1024);
        const float h = 0.5f * cell;
        for (size_t v = e0; v < e1; ++v) {
            const float3 p = m.pos[v];
            const float gx = sdf(make_float3(p.x + h, p.y, p.z)) - sdf(make_float3(p.x - h, p.y, p.z));
            const float gy = sdf(make_float3(p.x, p.y + h, p.z)) - sdf(make_float3(p.x, p.y - h, p.z));
            const float gz = sdf(make_float3(p.x, p.y, p.z + h)) - sdf(make_float3(p.x, p.y, p.z - h));
            const float3 g = make_float3(gx, gy, gz);
            const float L = length(g);
            m.nrm[v] = L > 1e-8f ? g / L : make_float3(0.0f, 1.0f, 0.0f);
        }
    });
    // quads across the grid edges the surface crosses, wound outwards (f > 0 outside), split along the shorter diagonal
    auto C = [&](int i, int j, int k) { return cv[(size_t(k) * ny + j) * nx + i]; };
    auto quad = [&](int q0, int q1, int q2, int q3, bool flip) {
        if (q0 < 0 || q1 < 0 || q2 < 0 || q3 < 0) return;
        if (flip) std::swap(q1, q3);
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
                quad(C(i, j - 1, k - 1), C(i, j, k - 1), C(i, j, k), C(i, j - 1, k), va > 0.0f);
            }
    for (int k = 1; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 1; i < nx; ++i) {   // y edges
                const float va = F(size_t(i), size_t(j), size_t(k)), vb = F(size_t(i), size_t(j + 1), size_t(k));
                if ((va < 0.0f) == (vb < 0.0f)) continue;
                quad(C(i - 1, j, k - 1), C(i - 1, j, k), C(i, j, k), C(i, j, k - 1), va > 0.0f);
            }
    for (int k = 0; k < nz; ++k)
        for (int j = 1; j < ny; ++j)
            for (int i = 1; i < nx; ++i) {   // z edges
                const float va = F(size_t(i), size_t(j), size_t(k)), vb = F(size_t(i), size_t(j), size_t(k + 1));
                if ((va < 0.0f) == (vb < 0.0f)) continue;
                quad(C(i - 1, j - 1, k), C(i, j - 1, k), C(i, j, k), C(i - 1, j, k), va > 0.0f);
            }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s: %dx%dx%d hucre (%.0f mm, %.1f%% tam hesap), %zu kose, %zu ucgen, %.0f ms", name, nx, ny, nz, cell * 1000.0f,
                  100.0 * double(evaluated.load()) / double(px * py * pz), m.pos.size(), m.idx.size(), ms);
    if (log) *log = buf;
    return m;
}
