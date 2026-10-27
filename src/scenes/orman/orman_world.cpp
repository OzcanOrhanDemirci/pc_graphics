#include "scenes/orman/orman_world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace {

float hash2(int x, int y, unsigned s) {
    unsigned h = unsigned(x) * 374761393u + unsigned(y) * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h & 0xFFFFFFu) / float(0xFFFFFF);
}
float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
float vnoise(float x, float y, unsigned s) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const float fx = smooth(x - ix), fy = smooth(y - iy);
    const float a = hash2(ix, iy, s), b = hash2(ix + 1, iy, s), c = hash2(ix, iy + 1, s), d = hash2(ix + 1, iy + 1, s);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}
float fbm(float x, float y, int oct, unsigned s) {
    float v = 0.0f, a = 0.5f, sum = 0.0f;
    for (int o = 0; o < oct; ++o) {
        v += a * (vnoise(x, y, s + unsigned(o) * 101u) * 2.0f - 1.0f);
        sum += a;
        x *= 2.03f;
        y *= 2.03f;
        a *= 0.5f;
    }
    return v / sum;
}
// the same over a tile: n noise cells to its side s (m), so it wraps and the tile meets its own copies
float vnoiseP(float x, float y, float s, int n, unsigned seed) {
    const float u = x / s * float(n), v = y / s * float(n);
    const int ix = int(std::floor(u)), iy = int(std::floor(v));
    const float fx = smooth(u - ix), fy = smooth(v - iy);
    auto at = [&](int i, int j) { return hash2(((i % n) + n) % n, ((j % n) + n) % n, seed); };
    const float a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}
float fbmP(float x, float y, float s, int n, int oct, unsigned seed) {
    float v = 0.0f, a = 0.5f, sum = 0.0f;
    for (int o = 0; o < oct; ++o) {
        v += a * (vnoiseP(x, y, s, n, seed + unsigned(o) * 101u) * 2.0f - 1.0f);
        sum += a;
        n *= 2;
        a *= 0.5f;
    }
    return v / sum;
}
float wrapTo(float x, float s) { return x - s * std::floor(x / s); }   // into [0, s)
float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// row-major 3x4: translate(t) * rotate(the y axis onto up, by `tilt` of the way) * rotY(yaw) * scale(s)
void placeMatrix(float m[12], float3 t, float3 up, float tilt, float yaw, float s) {
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    // R_yaw (about y): x' = c x + s z, z' = -s x + c z
    float Ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    // the tilt: about axis y x up by tilt * angle(y, up)
    float3 ax = cross(make_float3(0, 1, 0), up);
    const float al = length(ax);
    float Rt[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    if (al > 1e-5f) {
        ax = ax * (1.0f / al);
        const float ang = std::acos(std::clamp(up.y, -1.0f, 1.0f)) * tilt;
        const float c = std::cos(ang), sn = std::sin(ang), k = 1.0f - c;
        Rt[0] = c + ax.x * ax.x * k;
        Rt[1] = ax.x * ax.y * k - ax.z * sn;
        Rt[2] = ax.x * ax.z * k + ax.y * sn;
        Rt[3] = ax.y * ax.x * k + ax.z * sn;
        Rt[4] = c + ax.y * ax.y * k;
        Rt[5] = ax.y * ax.z * k - ax.x * sn;
        Rt[6] = ax.z * ax.x * k - ax.y * sn;
        Rt[7] = ax.z * ax.y * k + ax.x * sn;
        Rt[8] = c + ax.z * ax.z * k;
    }
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            float v = 0.0f;
            for (int k = 0; k < 3; ++k) v += Rt[r * 3 + k] * Ry[k * 3 + c];
            m[r * 4 + c] = v * s;
        }
    m[3] = t.x;
    m[7] = t.y;
    m[11] = t.z;
}

enum Kind { GroundPlant, Patch, Tall, Rock, Stone, Log, Root, Tree, Moss, Shrub, Debris };

Kind classify(const std::string& id, float height, float width) {
    auto has = [&](const char* k) { return id.find(k) != std::string::npos; };
    if (id.rfind("moss", 0) == 0) return Moss;   // moss sprigs (a mossy rock set is a rock)
    if (has("debris") || has("branch")) return Debris;
    if (has("root")) return Root;
    if (has("trunk") || has("log")) return Log;
    if (has("stump")) return Rock;
    if (has("boulder") || has("rock") || has("stone")) return width > 0.6f ? Rock : Stone;
    if (height > 6.0f) return Tree;
    if (height > 2.2f) return Tall;
    if (has("anthurium") || has("calathea") || has("alocasia") || has("philodendron") || has("monstera")) return Patch;
    if (height > 0.9f) return Shrub;
    return GroundPlant;
}

}   // namespace

void OrmanWorld::configure(const Config& cfg) {
    size = cfg.f("world.size", size);
    walk = cfg.f("world.walk", walk);
    res = cfg.f("ground.res", res);
    tile_ = cfg.f("ground.tile", tile_);
    seed_ = unsigned(cfg.i("world.seed", int(seed_)));
    streamDepth_ = cfg.f("stream.depth", streamDepth_);
    streamWidth_ = cfg.f("stream.width", streamWidth_);
    waterDepth_ = cfg.f("stream.water", waterDepth_);
    cells = std::max(0, cfg.i("world.cells", cells));
    youngTrees_ = cfg.i("young.trees", youngTrees_);
    dense_[0] = cfg.f("dense.floor", dense_[0]);
    dense_[1] = cfg.f("dense.shrub", dense_[1]);
    dense_[2] = cfg.f("dense.sapling", dense_[2]);
    dense_[3] = cfg.f("dense.debris", dense_[3]);
    dense_[4] = cfg.f("dense.litter", dense_[4]);
    sunAzimuth_ = cfg.f("sun.azimuth", sunAzimuth_);
    sunElevation_ = cfg.f("sun.elevation", sunElevation_);
}

float3 OrmanWorld::home(float3 p) const {
    const float W = period();
    if (W <= 0.0f) return p;
    p.x -= W * std::floor(p.x / W + 0.5f);
    p.z -= W * std::floor(p.z / W + 0.5f);
    return p;
}

template <class F> float OrmanWorld::roundTorus(float x, F f) const {
    const float W = period();
    if (W <= 0.0f) return f(x);
    x -= W * std::floor(x / W + 0.5f);
    const float L = size;
    if (x > 0.5f * W - L) return f(x) + (f(x - W) - f(x)) * smoothstep(-L, L, x - 0.5f * W);
    if (x < L - 0.5f * W) return f(x + W) + (f(x) - f(x + W)) * smoothstep(-L, L, x + 0.5f * W);
    return f(x);
}

float OrmanWorld::streamCentre(float x) const {
    return roundTorus(x, [](float u) { return 1.8f * std::sin(0.14f * u + 0.6f) + 0.7f * std::sin(0.33f * u + 2.1f) - 1.5f; });
}

float OrmanWorld::streamDistance(float x, float z) const {
    const float c = streamCentre(x);
    const float dc = (streamCentre(x + 0.05f) - streamCentre(x - 0.05f)) / 0.1f;
    return std::fabs(z - c) / std::sqrt(1.0f + dc * dc);
}

float OrmanWorld::tileHeight(float x, float z) const {
    // the patch's two scales of rise and fall (cells of about 15 and 2 m), whole cells to the tile's side; the banks'
    // level (the stream's cut is not in it)
    return 0.55f * fbmP(x, z, size, 3, 4, seed_) + 0.10f * fbmP(x, z, size, 20, 3, seed_ + 7u) + 0.25f;
}

float OrmanWorld::height(float x, float z) const {
    const float3 q = home(make_float3(x, 0.0f, z));
    x = q.x;
    z = q.z;
    const float d = streamDistance(x, z);
    // the stream's bed: cut down between its banks, with its own small unevenness
    const float carve = streamDepth_ * (1.0f - smoothstep(0.4f * streamWidth_, 1.8f * streamWidth_, d));
    const float bed = 0.04f * roundTorus(x, [&](float u) { return fbm(u * 1.7f, z * 1.7f, 2, seed_ + 13u); }) *
                      (1.0f - smoothstep(0.0f, 1.5f * streamWidth_, d));
    // the forest around the patch: the tile's ground, the banks rising away from the stream over 4 m (by the band's
    // sides it is the tile's again)
    auto world = [&]() { return tileHeight(x, z) - 0.25f * (1.0f - smoothstep(1.0f, 4.0f, d)) - carve + bed; };
    const float e = std::max(std::fabs(x), std::fabs(z));
    if (cells > 0 && e >= 0.5f * size) return world();
    // the patch's own, as it was made; over its last metres (past its views and its saplings' edge) it becomes the
    // forest's, so its sides meet the cells around it
    float h = 0.55f * fbm(x * 0.07f + 3.1f, z * 0.07f - 1.7f, 4, seed_) + 0.10f * fbm(x * 0.45f, z * 0.45f, 3, seed_ + 7u) + 0.02f * x;
    h += 0.25f * smoothstep(1.0f, 6.0f, d);   // the banks rise away from the stream (it has cut its way down)
    h -= carve;
    h += bed;
    const float w = cells > 0 ? smoothstep(0.5f * walk + 2.0f, 0.5f * size, e) : 0.0f;
    return w > 0.0f ? h + (world() - h) * w : h;
}

float3 OrmanWorld::normal(float x, float z) const {
    const float e = 0.05f;
    const float hx = height(x + e, z) - height(x - e, z), hz = height(x, z + e) - height(x, z - e);
    return normalize(make_float3(-hx, 2.0f * e, -hz));
}

float OrmanWorld::waterLevel(float x) const {
    // the bed's middle, averaged over two metres down the stream (the small stones of the bed do not lift the surface)
    float s = 0.0f;
    for (int k = -4; k <= 4; ++k) {
        const float xx = x + 0.25f * k;
        s += height(xx, streamCentre(xx));
    }
    return s / 9.0f + waterDepth_;
}

void OrmanWorld::collide(float& x, float& z, float feet, float r) const {
    // (round the torus: each solid's copy nearest the body)
    const float W = period();
    auto nearest = [&](float s, float v) { return W > 0.0f ? s + W * std::round((v - s) / W) : s; };
    for (int it = 0; it < 3; ++it)
        for (const Solid& d : solids) {
            if (d.top < feet + 0.45f) continue;
            const float sx = nearest(d.x, x), sz = nearest(d.z, z);
            const float dx = x - sx, dz = z - sz, dd = dx * dx + dz * dz, rr = r + d.r;
            if (dd >= rr * rr) continue;
            const float l = std::sqrt(std::max(dd, 1e-8f));
            x = sx + dx / l * rr;
            z = sz + dz / l * rr;
        }
}

float OrmanWorld::limit() const {
    // the patch alone has an edge; the forest of cells goes round
    return cells <= 0 ? 0.5f * size - 2.0f : 0.0f;
}

bool OrmanWorld::towardSun(float x, float z) const {
    const float az = sunAzimuth_ * 0.0174533f;
    return x * std::cos(az) + z * std::sin(az) > 0.82f * std::sqrt(x * x + z * z);
}

template <class H> OMeshCPU OrmanWorld::groundMesh(float x0, float z0, int nx, int nz, H h) const {
    OMeshCPU g;
    g.pos.resize(size_t(nx) * nz);
    g.nrm.resize(g.pos.size());
    g.uv.resize(g.pos.size());
    g.ao.assign(g.pos.size(), 1.0f);
    const float e = 0.05f;
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) {
            const float x = x0 + i * res, z = z0 + j * res;
            const size_t k = size_t(j) * nx + i;
            g.pos[k] = make_float3(x, h(x, z), z);
            g.nrm[k] = normalize(make_float3(-(h(x + e, z) - h(x - e, z)), 2.0f * e, -(h(x, z + e) - h(x, z - e))));
            g.uv[k] = make_float2(x, z);
        }
    g.idx.reserve(size_t(nx - 1) * (nz - 1) * 2);
    for (int j = 0; j + 1 < nz; ++j)
        for (int i = 0; i + 1 < nx; ++i) {
            const unsigned a = unsigned(j * nx + i), b = a + 1, c = a + unsigned(nx), d = c + 1;
            // counter-clockwise seen from above (the normal up)
            g.idx.push_back(make_uint3(a, c, b));
            g.idx.push_back(make_uint3(b, c, d));
        }
    g.material = groundMaterial_;
    g.uvScale = 1.0f / tile_;
    return g;
}

// Each leaf is cut from its atlas, lies on the ground's own triangles (not on the smooth height: it rests on what is
// drawn), its edges curled up and one end lifted, a few millimetres over the leaves under it
template <class D, class P>
void OrmanWorld::layLitter(const OMeshCPU& gm, float x0, float z0, int nx, int nz, bool wrap, float cell, std::mt19937& lr, D density, P piece,
                           std::vector<std::vector<OMeshCPU>>& meshes) const {
    float shares = 0.0f;
    for (const LitterSet& s : litterSets) shares += s.leaves.empty() ? 0.0f : s.share;
    if (shares <= 0.0f || dense_[4] <= 0.0f) return;
    const float sx = (nx - 1) * res, sz = (nz - 1) * res;
    auto groundY = [&](float x, float z) {
        if (wrap) {
            x = x0 + wrapTo(x - x0, sx);
            z = z0 + wrapTo(z - z0, sz);
        }
        const float gx = (x - x0) / res, gz = (z - z0) / res;
        const int i = std::clamp(int(gx), 0, nx - 2), j = std::clamp(int(gz), 0, nz - 2);
        const float fx = std::clamp(gx - i, 0.0f, 1.0f), fz = std::clamp(gz - j, 0.0f, 1.0f);
        const float a = gm.pos[size_t(j) * nx + i].y, b = gm.pos[size_t(j) * nx + i + 1].y;
        const float c = gm.pos[size_t(j + 1) * nx + i].y, d = gm.pos[size_t(j + 1) * nx + i + 1].y;
        return fx + fz <= 1.0f ? a + fx * (b - a) + fz * (c - a) : d + (1.0f - fx) * (c - d) + (1.0f - fz) * (b - d);
    };
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    auto L = [&]() { return U(lr); };
    // (a tile's cells fit its side exactly: no seam where its copies meet)
    const int cx = wrap ? std::max(1, int(std::round(sx / cell))) : int(sx / cell), cz = wrap ? std::max(1, int(std::round(sz / cell))) : int(sz / cell);
    const float hx = wrap ? sx / cx : cell, hz = wrap ? sz / cz : cell;
    for (int j = 0; j < cz; ++j)
        for (int i = 0; i < cx; ++i) {
            const float x = x0 + (i + L()) * hx, z = z0 + (j + L()) * hz;
            const float p = density(x, z);
            if (p < 0.0f || L() >= std::min(1.0f, dense_[4] * p)) continue;
            float pick = L() * shares;
            size_t si = 0;
            for (; si < litterSets.size(); ++si) {
                if (litterSets[si].leaves.empty()) continue;
                if (pick < litterSets[si].share) break;
                pick -= litterSets[si].share;
            }
            if (si == litterSets.size()) continue;
            const LitterSet& S = litterSets[si];
            const float4 r = S.leaves[size_t(L() * S.leaves.size()) % S.leaves.size()];
            const bool alongV = r.w - r.y >= r.z - r.x;
            const float len = S.length * (0.7f + 0.6f * L());
            const float wid = len * (alongV ? (r.z - r.x) / (r.w - r.y) : (r.w - r.y) / (r.z - r.x));
            const float yaw = L() * 6.2832f;
            const float3 a = make_float3(std::cos(yaw), 0, std::sin(yaw)), b = make_float3(-std::sin(yaw), 0, std::cos(yaw));
            const float lift = 0.002f + 0.008f * L(), curl = S.curl * (0.5f + L()), tip = 0.12f * L();
            const bool flip = L() < 0.5f;
            OMeshCPU& m = meshes[size_t(piece(z))][si];
            const unsigned base = unsigned(m.pos.size());
            for (int k = 0; k < 3; ++k)
                for (int l = 0; l < 3; ++l) {
                    const float t = 0.5f * k, s = float(l - 1), te = flip ? 1.0f - t : t;
                    const float px = x + a.x * (t - 0.5f) * len + b.x * s * 0.5f * wid, pz = z + a.z * (t - 0.5f) * len + b.z * s * 0.5f * wid;
                    m.pos.push_back(make_float3(px, groundY(px, pz) + lift + curl * s * s * 0.5f * wid + tip * len * te * te, pz));
                    const float u = 0.5f * (s + 1.0f);
                    m.uv.push_back(alongV ? make_float2(r.x + (r.z - r.x) * u, r.w + (r.y - r.w) * t) : make_float2(r.x + (r.z - r.x) * t, r.y + (r.w - r.y) * u));
                }
            for (int k = 0; k < 2; ++k)
                for (int l = 0; l < 2; ++l) {
                    const unsigned v0 = base + unsigned(k * 3 + l), v1 = v0 + 1, v2 = v0 + 3, v3 = v2 + 1;
                    m.idx.push_back(make_uint3(v0, v2, v1));
                    m.idx.push_back(make_uint3(v1, v2, v3));
                }
            // its normals from its own bend, up
            m.nrm.resize(m.pos.size(), make_float3(0, 0, 0));
            for (size_t q = m.idx.size() - 8; q < m.idx.size(); ++q) {
                const uint3 f = m.idx[q];
                float3 fn = cross(m.pos[f.y] - m.pos[f.x], m.pos[f.z] - m.pos[f.x]);
                if (fn.y < 0.0f) fn = fn * -1.0f;
                m.nrm[f.x] += fn;
                m.nrm[f.y] += fn;
                m.nrm[f.z] += fn;
            }
            for (unsigned v = base; v < base + 9; ++v) m.nrm[v] = normalize(m.nrm[v]);
        }
}

void OrmanWorld::build(OrmanAssets& A, int groundMaterial, int firstProto, int waterMaterial) {
    places.clear();
    counts.clear();
    extras.clear();
    aoJobs.clear();
    solids.clear();
    tile = OTile();
    groundMaterial_ = groundMaterial;
    groundProto = litterProto = -1;
    auto addExtra = [&](OProtoCPU&& p) {
        extras.push_back(std::move(p));
        return firstProto + int(extras.size()) - 1;
    };
    auto placeAt = [&](std::vector<OPlace>& out, int proto, unsigned mask = OMASK_SOLID) {
        OPlace q;
        q.proto = proto;
        q.mask = mask;
        out.push_back(q);
    };
    // the assets by what they are
    kinds_.clear();
    widths_.clear();
    for (const auto& [id, protos] : A.byAsset) {
        float h = 0.0f, w = 0.0f;
        for (int p : protos) {
            h = std::max(h, A.protos[size_t(p)].bmax.y - A.protos[size_t(p)].bmin.y);
            w = std::max(w, std::max(A.protos[size_t(p)].bmax.x - A.protos[size_t(p)].bmin.x, A.protos[size_t(p)].bmax.z - A.protos[size_t(p)].bmin.z));
        }
        kinds_[classify(id, h, w)].push_back(id);
        widths_[id] = w;
    }
    const float half = 0.5f * size;
    const int n = int(std::round(size / res)) + 1;
    auto H = [&](float x, float z) { return height(x, z); };
    // the patch's ground
    {
        OProtoCPU ground;
        ground.asset = "ground";
        ground.name = "ground";
        ground.solid = true;
        ground.geoms.push_back(groundMesh(-half, -half, n, n, H));
        ground.bmin = make_float3(-half, -2, -half);
        ground.bmax = make_float3(half, 4, half);
        groundProto = addExtra(std::move(ground));
        placeAt(places, groundProto);
        aoJobs.push_back({groundProto, make_float3(0, 0, 0)});
    }
    // the water: a ribbon along the stream at its level, `count` steps of `step` from xa; where the banks rise over it
    // the ground hides it (the shore). The patch's every 10 cm, each cell of the stream's row its own, coarser
    auto ribbon = [&](float xa, float step, int count, float zlo, float zhi) {
        if (waterMaterial < 0) return;
        OProtoCPU water;
        water.asset = "water";
        water.name = "water";
        OMeshCPU w;
        const int nx = count + 1, nv = 33;
        const float halfW = 2.2f * streamWidth_;
        for (int i = 0; i < nx; ++i) {
            const float x = xa + i * step, c = streamCentre(x), y = waterLevel(x);
            const float dc = (streamCentre(x + 0.05f) - streamCentre(x - 0.05f)) / 0.1f;
            const float il = 1.0f / std::sqrt(1.0f + dc * dc);
            for (int j = 0; j < nv; ++j) {
                const float v = (float(j) / (nv - 1) * 2.0f - 1.0f) * halfW;
                const float3 p = make_float3(x - dc * il * v, y, c + il * v);
                w.pos.push_back(p);
                w.nrm.push_back(make_float3(0, 1, 0));
                w.uv.push_back(make_float2(p.x, p.z));
            }
        }
        for (int i = 0; i + 1 < nx; ++i)
            for (int j = 0; j + 1 < nv; ++j) {
                const unsigned a = unsigned(i * nv + j), b = a + 1, c = a + unsigned(nv), d = c + 1;
                w.idx.push_back(make_uint3(a, b, c));
                w.idx.push_back(make_uint3(b, d, c));
            }
        w.material = waterMaterial;
        water.geoms.push_back(std::move(w));
        water.bmin = make_float3(xa, -2, zlo);
        water.bmax = make_float3(xa + count * step, 2, zhi);
        placeAt(places, addExtra(std::move(water)), OMASK_WATER);
    };
    ribbon(-half, 0.1f, int(std::round(size / 0.1f)), -half, half);

    std::mt19937 rng(seed_);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    solids.clear();
    auto free = [&](float x, float z, float r) {
        for (const Solid& d : solids)
            if ((x - d.x) * (x - d.x) + (z - d.z) * (z - d.z) < (r + d.r) * (r + d.r)) return false;
        return true;
    };
    lo = make_float3(-half, 1e9f, -half);
    hi = make_float3(half, -1e9f, half);
    auto put = [&](int proto, float x, float z, float yaw, float s, float tilt, float sink, float3 tint, float lift = 0.0f, float foot = -1.0f) -> float {
        const OProtoCPU& P = A.protos[size_t(proto)];
        // the lowest ground under its footprint: nothing floats on a slope (a tree's footprint is its trunk, not its crown)
        const float rx = foot >= 0.0f ? foot : 0.35f * s * std::max(P.bmax.x - P.bmin.x, P.bmax.z - P.bmin.z);
        float y = height(x, z);
        for (int k = 0; k < 4; ++k) y = std::min(y, height(x + rx * std::cos(k * 1.5708f + yaw), z + rx * std::sin(k * 1.5708f + yaw)));
        y += lift - sink * s * (P.bmax.y - P.bmin.y);
        OPlace p;
        p.proto = proto;
        p.scale = s;
        p.tint = tint;
        placeMatrix(p.m, make_float3(x, y, z), normal(x, z), tilt, yaw, s);
        places.push_back(p);
        // the baked light's bounds: the patch only (around it the light of the grid's nearest face)
        if (std::fabs(x) <= half && std::fabs(z) <= half) {
            lo.y = std::min(lo.y, y + s * P.bmin.y);
            hi.y = std::max(hi.y, y + s * P.bmax.y);
        }
        return y + s * P.bmax.y;
    };
    auto variant = [&](const std::vector<int>& v) { return v[size_t(U(rng) * v.size()) % v.size()]; };
    auto tintOf = [&](float amount) {
        const float v = 1.0f + amount * (U(rng) - 0.5f) * 2.0f;
        return make_float3(v * (1.0f + 0.05f * (U(rng) - 0.5f)), v, v * (1.0f + 0.08f * (U(rng) - 0.5f)));
    };

    // the assets by what they are, the big ones first (the small ones fill around them)
    auto& kinds = kinds_;
    auto& widths = widths_;
    const float hw = 0.5f * walk;

    // fallen trunks: one across the stream near the middle (the bridge the eye meets), the others on the ground
    for (const std::string& id : kinds[Log]) {
        const auto& v = A.byAsset[id];
        for (int k = 0; k < 2; ++k) {
            const int proto = variant(v);
            const OProtoCPU& P = A.protos[size_t(proto)];
            const float len = P.bmax.x - P.bmin.x;
            float x, z, yaw;
            if (k == 0) {
                x = 3.0f;
                z = streamCentre(x);
                const float dc = (streamCentre(x + 0.05f) - streamCentre(x - 0.05f)) / 0.1f;
                // the trunk's long axis (its x, turned to (cos yaw, 0, -sin yaw)) across the stream, a little askew
                yaw = std::atan2(-1.0f, -dc) + 0.25f;
            } else {
                x = -7.0f + 4.0f * U(rng);
                z = streamCentre(x) + 4.0f + 2.0f * U(rng);
                yaw = U(rng) * 6.2832f;
            }
            const float s = 1.2f;
            // across the stream it rests on the banks: its height from the banks, not from the bed
            float lift = 0.0f;
            if (k == 0) {
                const float ca = std::cos(yaw), sa = std::sin(yaw);
                const float e = 0.45f * len * s;
                const float hA = height(x + ca * e, z - sa * e), hB = height(x - ca * e, z + sa * e);
                lift = std::min(hA, hB) - height(x, z) + 0.05f;
            }
            const float top = put(proto, x, z, yaw, s, 0.0f, 0.15f, tintOf(0.05f), lift);
            // a trunk is a row of discs along its length
            const float thick = 0.5f * s * std::max(P.bmax.y - P.bmin.y, P.bmax.z - P.bmin.z);
            const float ca = std::cos(yaw), sa = std::sin(yaw);
            for (float u = -0.45f; u <= 0.451f; u += 0.1f)
                solids.push_back({x + ca * u * len * s, z - sa * u * len * s, 0.5f * thick, top});
        }
    }
    // rocks on the banks, half in the ground
    for (const std::string& id : kinds[Rock]) {
        const auto& v = A.byAsset[id];
        int made = 0;
        for (int tries = 0; tries < 200 && made < 4; ++tries) {
            const float x = (U(rng) - 0.5f) * walk, z = (U(rng) - 0.5f) * walk;
            const float d = streamDistance(x, z);
            if (d < 1.2f || d > 5.0f) continue;
            const int proto = variant(v);
            const OProtoCPU& P = A.protos[size_t(proto)];
            const float s = 0.45f + 0.5f * U(rng);
            const float r = 0.45f * s * std::max(P.bmax.x - P.bmin.x, P.bmax.z - P.bmin.z);
            if (!free(x, z, r)) continue;
            const float top = put(proto, x, z, U(rng) * 6.2832f, s, 0.5f, 0.3f, tintOf(0.06f));
            solids.push_back({x, z, r, top});
            ++made;
        }
    }
    // stones in the bed (small rocks; the boulder scan at a stone's size if there is nothing smaller)
    std::vector<std::string> stones = kinds[Stone];
    if (stones.empty()) stones = kinds[Rock];
    for (const std::string& id : stones) {
        const auto& v = A.byAsset[id];
        for (int k = 0; k < 160; ++k) {
            const float x = (U(rng) - 0.5f) * size * 0.95f;
            const float z = streamCentre(x) + (U(rng) - 0.5f) * 2.0f * streamWidth_ * 1.1f;
            const int proto = variant(v);
            const OProtoCPU& P = A.protos[size_t(proto)];
            const float target = 0.06f + 0.22f * U(rng) * U(rng);   // most small, a few big
            const float s = target / std::max(0.05f, std::max(P.bmax.x - P.bmin.x, P.bmax.z - P.bmin.z));
            put(proto, x, z, U(rng) * 6.2832f, s, 0.8f, 0.35f, tintOf(0.12f));
        }
    }
    // the tall ones: the edges and here and there, never in the light's gap over the middle
    for (const std::string& id : kinds[Tree]) {
        const auto& v = A.byAsset[id];
        int made = 0;
        for (int tries = 0; tries < 800 && made < 14; ++tries) {
            const float x = (U(rng) - 0.5f) * size, z = (U(rng) - 0.5f) * size;
            const float rr = std::sqrt(x * x + z * z);
            if (rr < 7.0f || streamDistance(x, z) < 2.0f) continue;
            if (!free(x, z, 1.5f)) continue;
            const float top = put(variant(v), x, z, U(rng) * 6.2832f, 0.9f + 0.3f * U(rng), 0.0f, 0.02f, tintOf(0.08f));
            solids.push_back({x, z, 0.5f, top});
            ++made;
        }
    }
    // roots by the trunks and rocks
    for (const std::string& id : kinds[Root]) {
        const auto& v = A.byAsset[id];
        for (int k = 0; k < 6 && !solids.empty(); ++k) {
            const Solid& d = solids[size_t(U(rng) * solids.size()) % solids.size()];
            const float a = U(rng) * 6.2832f;
            const float x = d.x + std::cos(a) * (d.r + 0.4f), z = d.z + std::sin(a) * (d.r + 0.4f);
            if (streamDistance(x, z) < 0.8f) continue;
            put(variant(v), x, z, a, 0.8f + 0.4f * U(rng), 0.7f, 0.1f, tintOf(0.06f));
        }
    }
    // the light's gap: within 35 degrees of the sun's bearing from the patch's middle
    const float sunAz = sunAzimuth_ * 0.0174533f;
    auto towardSun = [&](float x, float z) { return x * std::cos(sunAz) + z * std::sin(sunAz) > 0.82f * std::sqrt(x * x + z * z); };
    // the views' places kept open of anything taller than the knee (0 in a clearing, 1 a metre past its edge)
    auto open = [&](float x, float z) {
        float o = 1.0f;
        for (const Clearing& c : clearings) o = std::min(o, smoothstep(c.r, c.r + 1.0f, std::sqrt((x - c.x) * (x - c.x) + (z - c.z) * (z - c.z))));
        return o;
    };
    // young trees under the crowns: the middle storey between the saplings and the canopy, off the light's gap
    for (const std::string& id : kinds[Tree]) {
        const auto& v = A.byAsset[id];
        int made = 0;
        for (int tries = 0; tries < 800 && made < youngTrees_; ++tries) {
            const float x = (U(rng) - 0.5f) * size, z = (U(rng) - 0.5f) * size;
            if (std::sqrt(x * x + z * z) < 5.0f || towardSun(x, z) || streamDistance(x, z) < 2.0f || open(x, z) < 1.0f) continue;
            if (!free(x, z, 1.0f)) continue;
            const float top = put(variant(v), x, z, U(rng) * 6.2832f, 0.3f + 0.25f * U(rng), 0.0f, 0.02f, tintOf(0.1f), 0.0f, 0.3f);
            solids.push_back({x, z, 0.25f, top});
            ++made;
        }
    }
    // the understory, each layer by its own reason, on a jittered grid kept by a density (times its dense.* setting)
    auto scatter = [&](const std::string& id, float spacing, float sMin, float sMax, float tilt, auto density) {
        const auto& v = A.byAsset[id];
        const int cells = int(size / spacing);
        for (int j = 0; j < cells; ++j)
            for (int i = 0; i < cells; ++i) {
                const float x = -half + (i + U(rng)) * spacing, z = -half + (j + U(rng)) * spacing;
                const float p = density(x, z);
                if (U(rng) >= p) continue;
                const int proto = variant(v);
                const OProtoCPU& P = A.protos[size_t(proto)];
                const float s = sMin + (sMax - sMin) * U(rng);
                if (!free(x, z, 0.25f * s * std::max(P.bmax.x - P.bmin.x, P.bmax.z - P.bmin.z))) continue;
                put(proto, x, z, U(rng) * 6.2832f, s, tilt, 0.03f, tintOf(0.12f));
            }
    };
    auto patchy = [&](float x, float z, float f, float a, float b, unsigned s) { return smoothstep(a, b, vnoise(x * f, z * f, s)); };
    unsigned salt = 0;
    // saplings: everywhere but the stream, thicker at the walked part's edge (the walls) and in clumps; from knee-high
    // shoots to small trees, so the space under the crowns fills at every height
    for (const std::string& id : kinds[Tall]) {
        const unsigned sa = seed_ + 300u + salt++;
        scatter(id, 1.7f, 0.45f, 1.25f, 0.1f, [&, sa](float x, float z) {
            if (streamDistance(x, z) < 1.6f) return 0.0f;
            const float edge = smoothstep(0.6f * hw, hw, std::max(std::fabs(x), std::fabs(z)));
            return std::min(1.0f, dense_[2] * (0.3f + 0.35f * patchy(x, z, 0.15f, 0.4f, 0.65f, sa) + 0.35f * edge)) * open(x, z);
        });
    }
    // shrubs at the eye's height: in thickets with gaps between them (a look ends in leaves a few metres on, but
    // there is a way through), not on the stream's rim
    for (const std::string& id : kinds[Shrub]) {
        const unsigned sa = seed_ + 500u + salt++;
        scatter(id, 0.8f, 0.7f, 1.5f, 0.3f, [&, sa](float x, float z) {
            if (streamDistance(x, z) < 1.3f) return 0.0f;
            return std::min(1.0f, dense_[1] * (0.3f + 0.6f * patchy(x, z, 0.22f, 0.4f, 0.7f, sa))) * open(x, z);
        });
    }
    // broad leaves in patches, and a few anywhere
    for (const std::string& id : kinds[Patch]) {
        const unsigned sa = seed_ + 100u + salt++;
        scatter(id, 0.5f, 0.8f, 1.35f, 0.6f, [&, sa](float x, float z) {
            if (streamDistance(x, z) < 1.0f) return 0.0f;
            return std::min(1.0f, dense_[0] * (0.2f + 0.75f * patchy(x, z, 0.35f, 0.45f, 0.7f, sa))) * (0.3f + 0.7f * open(x, z));
        });
    }
    // the floor: ferns and small weeds everywhere, crowding the moist banks; spaced by the plant's own width
    for (const std::string& id : kinds[GroundPlant]) {
        const unsigned sa = seed_ + 200u + salt++;
        const float spacing = std::clamp(0.5f * widths[id], 0.18f, 0.6f);
        scatter(id, spacing, 0.7f, 1.4f, 0.6f, [&, sa](float x, float z) {
            const float d = streamDistance(x, z);
            if (d < 0.85f) return 0.0f;
            const float bank = 1.0f - smoothstep(1.5f, 5.0f, d);   // moist banks
            return std::min(1.0f, dense_[0] * (0.4f + 0.45f * bank + 0.35f * patchy(x, z, 0.25f, 0.35f, 0.7f, sa)));
        });
    }
    // what falls from the crowns: bark and dry branches, gathered where the ground is low and still
    for (const std::string& id : kinds[Debris]) {
        const unsigned sa = seed_ + 600u + salt++;
        scatter(id, 0.8f, 0.7f, 1.3f, 0.9f, [&, sa](float x, float z) {
            if (streamDistance(x, z) < 1.0f) return 0.0f;
            return std::min(1.0f, dense_[3] * (0.25f + 0.4f * patchy(x, z, 0.3f, 0.4f, 0.7f, sa)));
        });
    }
    for (const std::string& id : kinds[Moss]) {
        const unsigned sa = seed_ + 400u + salt++;
        // a moss sprig is two or three centimetres: carpets of them on the wet rim of the bed and in damp patches
        scatter(id, 0.07f, 0.8f, 1.6f, 0.6f, [&, sa](float x, float z) {
            const float d = streamDistance(x, z);
            const float rim = smoothstep(0.5f, 0.75f, d) * (1.0f - smoothstep(1.1f, 2.0f, d));
            const float patch = smoothstep(0.6f, 0.8f, vnoise(x * 0.5f, z * 0.5f, sa));
            return 0.85f * rim * (0.4f + 0.6f * vnoise(x * 2.0f, z * 2.0f, sa + 1u)) + 0.5f * patch;
        });
    }


    // the fallen leaves over the patch, last and with their own sequence (nothing else moves for them): everywhere,
    // piled against what stands (trunks, rocks, logs), a few sunk in the bed
    auto leafMeshes = [&](int pieces) { return std::vector<std::vector<OMeshCPU>>(size_t(pieces), std::vector<OMeshCPU>(litterSets.size())); };
    // a litter's meshes as prototypes (one per piece), placed by `place`; the leaves each holds
    std::map<int, long long> leavesOf;
    auto addLitter = [&](std::vector<std::vector<OMeshCPU>>& meshes, float3 lo3, float3 hi3, auto place) {
        for (size_t pc = 0; pc < meshes.size(); ++pc) {
            OProtoCPU lp;
            lp.asset = "litter";
            lp.name = "litter";
            long long leaves = 0;
            for (size_t s = 0; s < meshes[pc].size(); ++s) {
                OMeshCPU& m = meshes[pc][s];
                if (m.idx.empty()) continue;
                m.material = litterSets[s].material;
                m.ao.assign(m.pos.size(), 1.0f);   // baked like the ground's: the leaves under the plants darken
                leaves += (long long)(m.idx.size() / 8);
                lp.geoms.push_back(std::move(m));
            }
            if (lp.geoms.empty()) continue;
            lp.bmin = lo3;
            lp.bmax = hi3;
            const int proto = addExtra(std::move(lp));
            leavesOf[proto] = leaves;
            place(int(pc), proto);
        }
    };
    {
        std::mt19937 lr(seed_ + 77u);
        auto meshes = leafMeshes(1);
        const OMeshCPU& gm = extras[size_t(groundProto - firstProto)].geoms[0];
        layLitter(gm, -half, -half, n, n, false, 0.09f, lr,
                  [&](float x, float z) {
                      const float d = streamDistance(x, z);
                      float p = (0.45f + 0.35f * patchy(x, z, 0.4f, 0.3f, 0.7f, seed_ + 700u)) *
                                (d < 0.9f * streamWidth_ ? 0.15f : 0.4f + 0.6f * smoothstep(0.6f * streamWidth_, 1.5f * streamWidth_, d));
                      for (const Solid& s : solids) {
                          const float e = std::sqrt((x - s.x) * (x - s.x) + (z - s.z) * (z - s.z)) - s.r;
                          if (e < 0.0f) return -1.0f;
                          if (e < 0.8f) p += 0.5f * (1.0f - e / 0.8f);
                      }
                      return p;
                  },
                  [](float) { return 0; }, meshes);
        addLitter(meshes, make_float3(-half, -2, -half), make_float3(half, 4, half), [&](int, int proto) {
            litterProto = proto;
            placeAt(places, proto);
            aoJobs.push_back({proto, make_float3(0, 0, 0)});
        });
    }
    lo.y -= 1.0f;
    hi.y += 1.0f;

    // the forest's tile: the same rules without the stream, grown on a ground that wraps
    if (cells > 0) {
        // what the patch and each cell hold of their own, in `places` (the rest of a cell is the tile's copy)
        struct Own {
            int i, j;
            size_t from;
        };
        std::vector<Own> own{{0, 0, 0}};
        std::vector<Solid> tsol;
        std::vector<unsigned char> ttall, band;
        tileLo = make_float3(-half, 1e9f, -half);
        tileHi = make_float3(half, -1e9f, half);
        grow(A, {-half, -half, size, size, true, false, seed_ + 1000u}, tile.places, tsol, ttall, tileLo, tileHi);
        // left out of the stream's row: what stands in its band, and the tall ones within 6 m of it (over a stream the
        // crowns open, so its water mirrors the sky)
        auto rowCut = [&](float z, bool isTall) { return (z >= bandLo_ && z < bandHi_) || (isTall && z >= bandLo_ - 6.0f && z < bandHi_ + 6.0f); };
        for (size_t q = 0; q < tile.places.size(); ++q) band.push_back(rowCut(tile.places[q].m[11], ttall[q] != 0) ? 1 : 0);
        const int k = std::min(2, cells);
        tileBakeAt = make_float3(-k * size, 0.0f, -k * size);
        auto tileH = [&](float x, float z) { return tileHeight(x, z); };
        // its ground in three pieces: the stream's row has its own band
        const int r0 = int(std::round((bandLo_ + half) / res)), r1 = int(std::round((bandHi_ + half) / res));
        const int rows[4] = {0, r0, r1, n - 1};
        for (int pc = 0; pc < 3; ++pc) {
            OProtoCPU gp;
            gp.asset = "ground";
            gp.name = "tile ground";
            gp.solid = true;
            gp.geoms.push_back(groundMesh(-half, -half + rows[pc] * res, n, rows[pc + 1] - rows[pc] + 1, tileH));
            gp.bmin = make_float3(-half, -2, -half);
            gp.bmax = make_float3(half, 4, half);
            const int proto = addExtra(std::move(gp));
            placeAt(tile.places, proto);
            ttall.push_back(0);
            band.push_back(pc == 1 ? 1 : 0);
            aoJobs.push_back({proto, tileBakeAt});
        }
        // its litter (its own sequence), in the same three pieces
        {
            const OMeshCPU whole = groundMesh(-half, -half, n, n, tileH);
            std::mt19937 lr(seed_ + 1077u);
            auto meshes = leafMeshes(3);
            const int nz = std::max(1, int(std::round(size * 0.4f)));
            layLitter(whole, -half, -half, n, n, true, 0.09f, lr,
                      [&](float x, float z) {
                          float p = 0.45f + 0.35f * smoothstep(0.3f, 0.7f, vnoiseP(x, z, size, nz, seed_ + 1700u));
                          for (const Solid& s : tsol) {
                              float dx = x - s.x, dz = z - s.z;
                              dx -= size * std::round(dx / size);
                              dz -= size * std::round(dz / size);
                              const float e = std::sqrt(dx * dx + dz * dz) - s.r;
                              if (e < 0.0f) return -1.0f;
                              if (e < 0.8f) p += 0.5f * (1.0f - e / 0.8f);
                          }
                          return p;
                      },
                      [&](float z) { return z < bandLo_ ? 0 : (z < bandHi_ ? 1 : 2); }, meshes);
            addLitter(meshes, make_float3(-half, -2, -half), make_float3(half, 4, half), [&](int pc, int proto) {
                placeAt(tile.places, proto);
                ttall.push_back(0);
                band.push_back(pc == 1 ? 1 : 0);
                aoJobs.push_back({proto, tileBakeAt});
            });
        }
        tileLo.y -= 1.0f;
        tileHi.y += 1.0f;
        tile.variants.assign(2, {});
        for (unsigned q = 0; q < unsigned(tile.places.size()); ++q) {
            tile.variants[0].push_back(q);
            if (!band[q]) tile.variants[1].push_back(q);
        }
        // the sun comes to the patch low over the forest (25 degrees): the tile's tall trees in its way, as far as its
        // tallest top could reach into it, are left out of that cell's copies (the patch's light's gap)
        const float reach = std::max(tileHi.y, 1.0f) / std::tan(std::max(sunElevation_, 5.0f) * 0.0174533f) + half * 1.42f;
        sunReach_ = reach;
        auto inSun = [&](float x, float z) { return towardSun(x, z) && x * x + z * z < reach * reach; };
        for (int j = -cells; j <= cells; ++j)
            for (int i = -cells; i <= cells; ++i) {
                if (i == 0 && j == 0) continue;
                own.push_back({i, j, places.size()});
                const float ox = i * size, oz = j * size;
                const bool row = j == 0;
                bool sunny = false;
                for (size_t q = 0; q < tile.places.size() && !sunny; ++q)
                    if (ttall[q] && !(row && band[q]) && inSun(tile.places[q].m[3] + ox, tile.places[q].m[11] + oz)) sunny = true;
                if (!sunny) {
                    tile.cells.push_back({make_float3(ox, 0.0f, oz), row ? 1 : 0, i, j});
                } else {
                    for (size_t q = 0; q < tile.places.size(); ++q) {
                        if ((row && band[q]) || (ttall[q] && inSun(tile.places[q].m[3] + ox, tile.places[q].m[11] + oz))) continue;
                        OPlace c = tile.places[q];
                        c.m[3] += ox;
                        c.m[11] += oz;
                        places.push_back(c);
                    }
                }
                for (const Solid& s : tsol) {
                    if ((row && rowCut(s.z, s.tall)) || (sunny && s.tall && inSun(s.x + ox, s.z + oz))) continue;
                    solids.push_back({s.x + ox, s.z + oz, s.r, s.top, s.tall});
                }
                if (!row) continue;
                // the stream's band in this cell: its own bed, banks, stones and plants, its own ground and leaves
                const float x0 = ox - half, bz = bandHi_ - bandLo_;
                std::vector<Solid> bsol;
                std::vector<unsigned char> btall;
                float3 blo = make_float3(0, 1e9f, 0), bhi = make_float3(0, -1e9f, 0);
                grow(A, {x0, bandLo_, size, bz, false, true, seed_ + 2000u + unsigned(i + cells)}, places, bsol, btall, blo, bhi);
                const int bn = int(std::round(bz / res)) + 1;
                OProtoCPU gp;
                gp.asset = "ground";
                gp.name = "band ground";
                gp.solid = true;
                gp.geoms.push_back(groundMesh(x0, bandLo_, n, bn, H));
                gp.bmin = make_float3(x0, -2, bandLo_);
                gp.bmax = make_float3(x0 + size, 4, bandHi_);
                const OMeshCPU& bg = gp.geoms[0];
                std::mt19937 lr(seed_ + 2077u + unsigned(i + cells));
                auto meshes = leafMeshes(1);
                layLitter(bg, x0, bandLo_, n, bn, false, 0.09f, lr,
                          [&](float x, float z) {
                              const float d = streamDistance(x, z);
                              float p = (0.45f + 0.35f * smoothstep(0.3f, 0.7f, vnoise(x * 0.4f, z * 0.4f, seed_ + 700u))) *
                                        (d < 0.9f * streamWidth_ ? 0.15f : 0.4f + 0.6f * smoothstep(0.6f * streamWidth_, 1.5f * streamWidth_, d));
                              for (const Solid& s : bsol) {
                                  const float e = std::sqrt((x - s.x) * (x - s.x) + (z - s.z) * (z - s.z)) - s.r;
                                  if (e < 0.0f) return -1.0f;
                                  if (e < 0.8f) p += 0.5f * (1.0f - e / 0.8f);
                              }
                              return p;
                          },
                          [](float) { return 0; }, meshes);
                const int proto = addExtra(std::move(gp));
                placeAt(places, proto);
                aoJobs.push_back({proto, make_float3(0, 0, 0), i, j});
                addLitter(meshes, make_float3(x0, -2, bandLo_), make_float3(x0 + size, 4, bandHi_), [&](int, int lp) {
                    placeAt(places, lp);
                    aoJobs.push_back({lp, make_float3(0, 0, 0), i, j});
                });
                ribbon(x0, 0.4f, int(std::round(size / 0.4f)), bandLo_, bandHi_);
                for (const Solid& s : bsol) solids.push_back(s);
            }
        // the torus: every cell a copy of the tile (whole, or without the band) and its own as one more group of its
        // own, where it stands. Its own come first among the places, so their instance ids stay what they were
        const unsigned nOwn = unsigned(places.size());
        for (auto& v : tile.variants)
            for (unsigned& q : v) q += nOwn;
        places.insert(places.end(), tile.places.begin(), tile.places.end());
        tile.places = std::move(places);
        places.clear();
        for (size_t c = 0; c < own.size(); ++c) {
            const size_t to = c + 1 < own.size() ? own[c + 1].from : nOwn;
            if (to <= own[c].from) continue;
            std::vector<unsigned> g;
            for (size_t q = own[c].from; q < to; ++q) g.push_back(unsigned(q));
            tile.variants.push_back(std::move(g));
            tile.cells.push_back({make_float3(0, 0, 0), int(tile.variants.size()) - 1, own[c].i, own[c].j});
        }
        tile.n = 2 * cells + 1;
        tile.side = size;
    }

    // the copies of each asset, every cell's (the litter: its leaves)
    const size_t nA = A.protos.size();
    std::vector<long long> per(nA + extras.size(), 0);
    for (const OPlace& q : places)
        if (q.proto >= 0 && size_t(q.proto) < per.size()) ++per[size_t(q.proto)];
    std::vector<long long> way(tile.variants.size() * per.size(), 0);
    for (size_t v = 0; v < tile.variants.size(); ++v)
        for (unsigned q : tile.variants[v]) ++way[v * per.size() + size_t(tile.places[q].proto)];
    for (const OTile::Cell& c : tile.cells)
        for (size_t k = 0; k < per.size(); ++k) per[k] += way[size_t(c.variant) * per.size() + k];
    for (size_t k = 0; k < per.size(); ++k) {
        if (!per[k]) continue;
        const std::string& name = k < nA ? A.protos[k].asset : extras[k - nA].asset;
        const auto lf = leavesOf.find(int(k));
        counts[name] += lf != leavesOf.end() ? per[k] * lf->second : per[k];
    }
}

void OrmanWorld::grow(OrmanAssets& A, const Area& a, std::vector<OPlace>& out, std::vector<Solid>& sol, std::vector<unsigned char>& tall, float3& blo,
                      float3& bhi) {
    std::mt19937 rng(a.seed);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const float share = a.sx * a.sz / (size * size);   // of a patch's worth of forest
    auto count = [&](float perPatch) { return int(std::round(perPatch * share)); };
    auto hgt = [&](float x, float z) { return a.wrap ? tileHeight(x, z) : height(x, z); };
    auto nrm = [&](float x, float z) {
        const float e = 0.05f;
        return normalize(make_float3(-(hgt(x + e, z) - hgt(x - e, z)), 2.0f * e, -(hgt(x, z + e) - hgt(x, z - e))));
    };
    auto sd = [&](float x, float z) { return a.stream ? streamDistance(x, z) : 1e9f; };
    // (the tile: the shorter way round, to its own copies)
    auto gap = [&](float d, float s) { return a.wrap ? d - s * std::round(d / s) : d; };
    auto free = [&](float x, float z, float r) {
        for (const Solid& d : sol) {
            const float dx = gap(x - d.x, a.sx), dz = gap(z - d.z, a.sz);
            if (dx * dx + dz * dz < (r + d.r) * (r + d.r)) return false;
        }
        return true;
    };
    auto noise = [&](float x, float z, float f, unsigned s) {
        return a.wrap ? vnoiseP(x - a.x0, z - a.z0, a.sx, std::max(1, int(std::round(a.sx * f))), s) : vnoise(x * f, z * f, s);
    };
    auto patchy = [&](float x, float z, float f, float lo, float hi, unsigned s) { return smoothstep(lo, hi, noise(x, z, f, s)); };
    // a point near the edge: the tile's back into its own copy, the band's dropped past its sides
    auto within = [&](float& x, float& z) {
        if (a.wrap) {
            x = a.x0 + wrapTo(x - a.x0, a.sx);
            z = a.z0 + wrapTo(z - a.z0, a.sz);
            return true;
        }
        return x >= a.x0 && x < a.x0 + a.sx && z >= a.z0 && z < a.z0 + a.sz;
    };
    // the band: no tall one between the patch and the sun (the tile's are left out of the copies there)
    auto sunWay = [&](float x, float z) { return !a.wrap && towardSun(x, z) && x * x + z * z < sunReach_ * sunReach_; };
    auto put = [&](int proto, float x, float z, float yaw, float s, float tilt, float sink, float3 tint, bool isTall, float lift = 0.0f,
                   float foot = -1.0f) -> float {
        const OProtoCPU& P = A.protos[size_t(proto)];
        // the lowest ground under its footprint: nothing floats on a slope (a tree's footprint is its trunk)
        const float rx = foot >= 0.0f ? foot : 0.35f * s * std::max(P.bmax.x - P.bmin.x, P.bmax.z - P.bmin.z);
        float y = hgt(x, z);
        for (int k = 0; k < 4; ++k) y = std::min(y, hgt(x + rx * std::cos(k * 1.5708f + yaw), z + rx * std::sin(k * 1.5708f + yaw)));
        y += lift - sink * s * (P.bmax.y - P.bmin.y);
        OPlace p;
        p.proto = proto;
        p.scale = s;
        p.tint = tint;
        placeMatrix(p.m, make_float3(x, y, z), nrm(x, z), tilt, yaw, s);
        out.push_back(p);
        tall.push_back(isTall ? 1 : 0);
        blo.y = std::min(blo.y, y + s * P.bmin.y);
        bhi.y = std::max(bhi.y, y + s * P.bmax.y);
        return y + s * P.bmax.y;
    };
    auto variant = [&](const std::vector<int>& v) { return v[size_t(U(rng) * v.size()) % v.size()]; };
    auto tintOf = [&](float amount) {
        const float v = 1.0f + amount * (U(rng) - 0.5f) * 2.0f;
        return make_float3(v * (1.0f + 0.05f * (U(rng) - 0.5f)), v, v * (1.0f + 0.08f * (U(rng) - 0.5f)));
    };
    auto rx = [&]() { return a.x0 + U(rng) * a.sx; };
    auto rz = [&]() { return a.z0 + U(rng) * a.sz; };

    // fallen trunks on the forest floor (the tile's: over the stream there is one bridge, the patch's)
    if (!a.stream)
        for (const std::string& id : kinds_[Log]) {
            const auto& v = A.byAsset[id];
            for (int tries = 0; tries < 60; ++tries) {
                const int proto = variant(v);
                const OProtoCPU& P = A.protos[size_t(proto)];
                const float len = P.bmax.x - P.bmin.x, s = 1.2f;
                const float x = rx(), z = rz(), yaw = U(rng) * 6.2832f;
                const float thick = 0.5f * s * std::max(P.bmax.y - P.bmin.y, P.bmax.z - P.bmin.z);
                const float ca = std::cos(yaw), sa = std::sin(yaw);
                bool ok = true;
                for (float u = -0.45f; u <= 0.451f && ok; u += 0.1f) ok = free(x + ca * u * len * s, z - sa * u * len * s, 0.5f * thick);
                if (!ok) continue;
                const float top = put(proto, x, z, yaw, s, 0.0f, 0.15f, tintOf(0.05f), false);
                for (float u = -0.45f; u <= 0.451f; u += 0.1f) sol.push_back({x + ca * u * len * s, z - sa * u * len * s, 0.5f * thick, top});
                break;
            }
        }
    // rocks: on the banks by the stream (the band), anywhere on the floor (the tile), half in the ground
    for (const std::string& id : kinds_[Rock]) {
        const auto& v = A.byAsset[id];
        const int want = a.stream ? 6 : 3;
        int made = 0;
        for (int tries = 0; tries < 400 && made < want; ++tries) {
            const float x = rx(), z = rz();
            const float d = sd(x, z);
            if (a.stream && (d < 1.2f || d > 5.0f)) continue;
            const int proto = variant(v);
            const OProtoCPU& P = A.protos[size_t(proto)];
            const float s = 0.45f + 0.5f * U(rng);
            const float r = 0.45f * s * std::max(P.bmax.x - P.bmin.x, P.bmax.z - P.bmin.z);
            if (!free(x, z, r)) continue;
            const float top = put(proto, x, z, U(rng) * 6.2832f, s, 0.5f, 0.3f, tintOf(0.06f), false);
            sol.push_back({x, z, r, top});
            ++made;
        }
    }
    // stones: in the bed (the band: the patch's number along its length), a few on the floor (the tile)
    std::vector<std::string> stones = kinds_[Stone];
    if (stones.empty()) stones = kinds_[Rock];
    for (const std::string& id : stones) {
        const auto& v = A.byAsset[id];
        const int want = a.stream ? int(160 * a.sx / size) : 12;
        for (int k = 0; k < want; ++k) {
            const float x = rx();
            const float z = a.stream ? streamCentre(x) + (U(rng) - 0.5f) * 2.0f * streamWidth_ * 1.1f : rz();
            const int proto = variant(v);
            const OProtoCPU& P = A.protos[size_t(proto)];
            const float target = 0.06f + 0.22f * U(rng) * U(rng);   // most small, a few big
            const float s = target / std::max(0.05f, std::max(P.bmax.x - P.bmin.x, P.bmax.z - P.bmin.z));
            put(proto, x, z, U(rng) * 6.2832f, s, 0.8f, a.stream ? 0.35f : 0.45f, tintOf(0.12f), false);
        }
    }
    // the tall ones (the band's well away from the stream: the sky opens over it)
    for (const std::string& id : kinds_[Tree]) {
        const auto& v = A.byAsset[id];
        const int want = count(14.0f);
        int made = 0;
        for (int tries = 0; tries < 800 && made < want; ++tries) {
            const float x = rx(), z = rz();
            if (sd(x, z) < (a.stream ? 8.0f : 2.0f) || sunWay(x, z) || !free(x, z, 1.5f)) continue;
            const float top = put(variant(v), x, z, U(rng) * 6.2832f, 0.9f + 0.3f * U(rng), 0.0f, 0.02f, tintOf(0.08f), true, 0.0f, 0.8f);
            sol.push_back({x, z, 0.5f, top, true});
            ++made;
        }
    }
    // roots by the trunks and rocks
    for (const std::string& id : kinds_[Root]) {
        const auto& v = A.byAsset[id];
        for (int k = 0; k < count(6.0f) && !sol.empty(); ++k) {
            const Solid& d = sol[size_t(U(rng) * sol.size()) % sol.size()];
            const float an = U(rng) * 6.2832f;
            float x = d.x + std::cos(an) * (d.r + 0.4f), z = d.z + std::sin(an) * (d.r + 0.4f);
            if (!within(x, z) || sd(x, z) < 0.8f) continue;
            put(variant(v), x, z, an, 0.8f + 0.4f * U(rng), 0.7f, 0.1f, tintOf(0.06f), false);
        }
    }
    // young trees under the crowns: the middle storey
    for (const std::string& id : kinds_[Tree]) {
        const auto& v = A.byAsset[id];
        const int want = count(float(youngTrees_));
        int made = 0;
        for (int tries = 0; tries < 800 && made < want; ++tries) {
            const float x = rx(), z = rz();
            if (sd(x, z) < 2.0f || sunWay(x, z) || !free(x, z, 1.0f)) continue;
            const float top = put(variant(v), x, z, U(rng) * 6.2832f, 0.3f + 0.25f * U(rng), 0.0f, 0.02f, tintOf(0.1f), true, 0.0f, 0.3f);
            sol.push_back({x, z, 0.25f, top, true});
            ++made;
        }
    }
    // the understory, each layer by the patch's own reasons, on a jittered grid (a tile's cells fit its side exactly:
    // no seam where its copies meet)
    auto scatter = [&](const std::string& id, float spacing, float sMin, float sMax, float tilt, auto density) {
        const auto& v = A.byAsset[id];
        const int nx = std::max(1, int(std::round(a.sx / spacing))), nz = std::max(1, int(std::round(a.sz / spacing)));
        const float hx = a.sx / nx, hz = a.sz / nz;
        for (int j = 0; j < nz; ++j)
            for (int i = 0; i < nx; ++i) {
                const float x = a.x0 + (i + U(rng)) * hx, z = a.z0 + (j + U(rng)) * hz;
                const float p = density(x, z);
                if (U(rng) >= p) continue;
                const int proto = variant(v);
                const OProtoCPU& P = A.protos[size_t(proto)];
                const float s = sMin + (sMax - sMin) * U(rng);
                if (!free(x, z, 0.25f * s * std::max(P.bmax.x - P.bmin.x, P.bmax.z - P.bmin.z))) continue;
                put(proto, x, z, U(rng) * 6.2832f, s, tilt, 0.03f, tintOf(0.12f), false);
            }
    };
    unsigned salt = 0;
    for (const std::string& id : kinds_[Tall]) {
        const unsigned sa = a.seed + 300u + salt++;
        scatter(id, 1.7f, 0.45f, 1.25f, 0.1f, [&, sa](float x, float z) {
            if (sd(x, z) < 1.6f) return 0.0f;
            return std::min(1.0f, dense_[2] * (0.4f + 0.35f * patchy(x, z, 0.15f, 0.4f, 0.65f, sa)));
        });
    }
    for (const std::string& id : kinds_[Shrub]) {
        const unsigned sa = a.seed + 500u + salt++;
        scatter(id, 0.8f, 0.7f, 1.5f, 0.3f, [&, sa](float x, float z) {
            if (sd(x, z) < 1.3f) return 0.0f;
            return std::min(1.0f, dense_[1] * (0.3f + 0.6f * patchy(x, z, 0.22f, 0.4f, 0.7f, sa)));
        });
    }
    for (const std::string& id : kinds_[Patch]) {
        const unsigned sa = a.seed + 100u + salt++;
        scatter(id, 0.5f, 0.8f, 1.35f, 0.6f, [&, sa](float x, float z) {
            if (sd(x, z) < 1.0f) return 0.0f;
            return std::min(1.0f, dense_[0] * (0.2f + 0.75f * patchy(x, z, 0.35f, 0.45f, 0.7f, sa)));
        });
    }
    for (const std::string& id : kinds_[GroundPlant]) {
        const unsigned sa = a.seed + 200u + salt++;
        const float spacing = std::clamp(0.5f * widths_[id], 0.18f, 0.6f);
        scatter(id, spacing, 0.7f, 1.4f, 0.6f, [&, sa](float x, float z) {
            const float d = sd(x, z);
            if (d < 0.85f) return 0.0f;
            const float bank = 1.0f - smoothstep(1.5f, 5.0f, d);   // moist banks
            return std::min(1.0f, dense_[0] * (0.4f + 0.45f * bank + 0.35f * patchy(x, z, 0.25f, 0.35f, 0.7f, sa)));
        });
    }
    for (const std::string& id : kinds_[Debris]) {
        const unsigned sa = a.seed + 600u + salt++;
        scatter(id, 0.8f, 0.7f, 1.3f, 0.9f, [&, sa](float x, float z) {
            if (sd(x, z) < 1.0f) return 0.0f;
            return std::min(1.0f, dense_[3] * (0.25f + 0.4f * patchy(x, z, 0.3f, 0.4f, 0.7f, sa)));
        });
    }
    for (const std::string& id : kinds_[Moss]) {
        const unsigned sa = a.seed + 400u + salt++;
        scatter(id, 0.07f, 0.8f, 1.6f, 0.6f, [&, sa](float x, float z) {
            const float d = sd(x, z);
            const float rim = smoothstep(0.5f, 0.75f, d) * (1.0f - smoothstep(1.1f, 2.0f, d));
            const float patch = smoothstep(0.6f, 0.8f, noise(x, z, 0.5f, sa));
            return 0.85f * rim * (0.4f + 0.6f * noise(x, z, 2.0f, sa + 1u)) + 0.5f * patch;
        });
    }
}
