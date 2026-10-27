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
    outer = std::max(cfg.f("world.outer", outer), size);
    outerRes_ = cfg.f("ground.outer_res", outerRes_);
    hill_ = cfg.f("valley.hill", hill_);
    hillRun_ = cfg.f("valley.run", hillRun_);
    bend_ = cfg.f("valley.bend", bend_);
    ringTrees_ = cfg.i("ring.trees", ringTrees_);
    youngTrees_ = cfg.i("young.trees", youngTrees_);
    dense_[0] = cfg.f("dense.floor", dense_[0]);
    dense_[1] = cfg.f("dense.shrub", dense_[1]);
    dense_[2] = cfg.f("dense.sapling", dense_[2]);
    dense_[3] = cfg.f("dense.debris", dense_[3]);
    dense_[4] = cfg.f("dense.litter", dense_[4]);
    sunAzimuth_ = cfg.f("sun.azimuth", sunAzimuth_);
}

float OrmanWorld::streamCentre(float x) const {
    // past the patch it bends away, one end each way: down its valley the eye meets the far bank, not the world's end
    const float b = std::max(std::fabs(x) - 0.5f * size - 2.0f, 0.0f);
    return 1.8f * std::sin(0.14f * x + 0.6f) + 0.7f * std::sin(0.33f * x + 2.1f) - 1.5f + std::copysign(bend_ * b * b, x);
}

float OrmanWorld::streamDistance(float x, float z) const {
    const float c = streamCentre(x);
    const float dc = (streamCentre(x + 0.05f) - streamCentre(x - 0.05f)) / 0.1f;
    return std::fabs(z - c) / std::sqrt(1.0f + dc * dc);
}

float OrmanWorld::height(float x, float z) const {
    float h = 0.55f * fbm(x * 0.07f + 3.1f, z * 0.07f - 1.7f, 4, seed_) + 0.10f * fbm(x * 0.45f, z * 0.45f, 3, seed_ + 7u) + 0.02f * x;
    // the banks rise away from the stream (it has cut its way down)
    const float d = streamDistance(x, z);
    h += 0.25f * smoothstep(1.0f, 6.0f, d);
    const float carve = streamDepth_ * (1.0f - smoothstep(0.4f * streamWidth_, 1.8f * streamWidth_, d));
    h -= carve;
    // the bed's own small unevenness
    h += 0.04f * fbm(x * 1.7f, z * 1.7f, 2, seed_ + 13u) * (1.0f - smoothstep(0.0f, 1.5f * streamWidth_, d));
    // past the patch the valley's sides rise (nothing changes inside it), unevenly, but not over the stream's own bed
    const float e = std::max(std::fabs(x), std::fabs(z)) - 0.5f * size;
    if (e > 0.0f) {
        const float rate = 0.7f + 0.6f * vnoise(x * 0.05f + 7.3f, z * 0.05f - 2.9f, seed_ + 21u);
        h += hill_ * smoothstep(0.0f, hillRun_, e * rate) * smoothstep(1.5f * streamWidth_, 10.0f, d);
    }
    return h;
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
    for (int it = 0; it < 3; ++it)
        for (const Solid& d : solids) {
            if (d.top < feet + 0.45f) continue;
            const float dx = x - d.x, dz = z - d.z, dd = dx * dx + dz * dz, rr = r + d.r;
            if (dd >= rr * rr) continue;
            const float l = std::sqrt(std::max(dd, 1e-8f));
            x = d.x + dx / l * rr;
            z = d.z + dz / l * rr;
        }
}

void OrmanWorld::build(OrmanAssets& A, int groundMaterial, int groundProto, int waterMaterial) {
    places.clear();
    counts.clear();
    // the ground
    ground = OProtoCPU();
    ground.asset = "ground";
    ground.name = "ground";
    ground.solid = true;
    OMeshCPU g;
    const int n = int(std::round(size / res)) + 1;
    g.pos.resize(size_t(n) * n);
    g.nrm.resize(g.pos.size());
    g.uv.resize(g.pos.size());
    g.ao.assign(g.pos.size(), 1.0f);
    const float half = 0.5f * size;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float x = -half + i * res, z = -half + j * res;
            const size_t k = size_t(j) * n + i;
            g.pos[k] = make_float3(x, height(x, z), z);
            g.nrm[k] = normal(x, z);
            g.uv[k] = make_float2(x, z);
        }
    g.idx.reserve(size_t(n - 1) * (n - 1) * 2);
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            const unsigned a = unsigned(j * n + i), b = a + 1, c = a + unsigned(n), d = c + 1;
            // counter-clockwise seen from above (the normal up)
            g.idx.push_back(make_uint3(a, c, b));
            g.idx.push_back(make_uint3(b, c, d));
        }
    g.material = groundMaterial;
    g.uvScale = 1.0f / tile_;
    ground.geoms.push_back(std::move(g));
    // the valley around it, coarser: under the patch's edge by a few centimetres where the two overlap (the finer one is
    // the one seen), so no gap opens between them
    const float oh = 0.5f * outer;
    if (oh > half) {
        OMeshCPU v;
        const int m = int(std::round(outer / outerRes_)) + 1;
        const float step = outer / float(m - 1), inner = half - 2.0f * step;
        std::vector<int> at(size_t(m) * m, -1);
        auto vertex = [&](int i, int j) {
            int& k = at[size_t(j) * m + i];
            if (k < 0) {
                const float x = -oh + i * step, z = -oh + j * step;
                k = int(v.pos.size());
                v.pos.push_back(make_float3(x, height(x, z) - 0.03f, z));
                v.nrm.push_back(normal(x, z));
                v.uv.push_back(make_float2(x, z));
            }
            return unsigned(k);
        };
        for (int j = 0; j + 1 < m; ++j)
            for (int i = 0; i + 1 < m; ++i) {
                const float x0 = -oh + i * step, z0 = -oh + j * step;
                if (x0 >= -inner && x0 + step <= inner && z0 >= -inner && z0 + step <= inner) continue;   // under the patch
                const unsigned a = vertex(i, j), b = vertex(i + 1, j), c = vertex(i, j + 1), d = vertex(i + 1, j + 1);
                v.idx.push_back(make_uint3(a, c, b));
                v.idx.push_back(make_uint3(b, c, d));
            }
        v.ao.assign(v.pos.size(), 1.0f);
        v.material = groundMaterial;
        v.uvScale = 1.0f / tile_;
        ground.geoms.push_back(std::move(v));
    }
    ground.bmin = make_float3(-oh, -2, -oh);
    ground.bmax = make_float3(oh, hill_ + 2, oh);
    OPlace gp;
    gp.proto = groundProto;
    places.push_back(gp);
    counts["ground"] = 1;
    // the water: a ribbon along the stream at its level; where the banks rise over it the ground hides it (the shore)
    water = OProtoCPU();
    water.asset = "water";
    water.name = "water";
    if (waterMaterial >= 0) {
        OMeshCPU w;
        // every 10 cm along the patch, coarser down the valley on either side
        std::vector<float> xs;
        for (float x = -oh; x < -half; x += 0.4f) xs.push_back(x);
        for (int i = 0; i <= int(size / 0.1f); ++i) xs.push_back(-half + i * 0.1f);
        for (float x = half + 0.4f; x <= oh; x += 0.4f) xs.push_back(x);
        const int nx = int(xs.size()), nv = 33;
        const float halfW = 2.2f * streamWidth_;
        for (int i = 0; i < nx; ++i) {
            const float x = xs[size_t(i)], c = streamCentre(x), y = waterLevel(x);
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
        water.bmin = make_float3(-oh, -2, -oh);
        water.bmax = make_float3(oh, 2, oh);
        OPlace wp;
        wp.proto = groundProto + 1;
        wp.mask = OMASK_WATER;
        places.push_back(wp);
        counts["water"] = 1;
    }

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
        ++counts[P.asset];
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
    std::map<int, std::vector<std::string>> kinds;
    std::map<std::string, float> widths;
    for (const auto& [id, protos] : A.byAsset) {
        float h = 0.0f, w = 0.0f;
        for (int p : protos) {
            h = std::max(h, A.protos[size_t(p)].bmax.y - A.protos[size_t(p)].bmin.y);
            w = std::max(w, std::max(A.protos[size_t(p)].bmax.x - A.protos[size_t(p)].bmin.x, A.protos[size_t(p)].bmax.z - A.protos[size_t(p)].bmin.z));
        }
        kinds[classify(id, h, w)].push_back(id);
        widths[id] = w;
    }
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

    // the ring and the valley around the patch, placed after it (the patch stays as it was): big trees all round but
    // between the patch and the sun (the gap its light comes through), young trees on the slopes, and at the eye's
    // height a wall of shrubs from the walked part's edge outwards, so a look outwards ends in leaves, never in the
    // ground's end
    if (oh > half) {
        auto beyond = [&](float x, float z) { return std::max(std::fabs(x), std::fabs(z)); };
        for (const std::string& id : kinds[Tree]) {
            const auto& v = A.byAsset[id];
            int made = 0;
            for (int tries = 0; tries < 4000 && made < ringTrees_; ++tries) {
                const float x = (U(rng) - 0.5f) * (outer - 8.0f), z = (U(rng) - 0.5f) * (outer - 8.0f);
                if (beyond(x, z) < half + 1.0f || towardSun(x, z) || streamDistance(x, z) < 6.0f || !free(x, z, 4.0f)) continue;
                const float top = put(variant(v), x, z, U(rng) * 6.2832f, 0.9f + 0.35f * U(rng), 0.0f, 0.02f, tintOf(0.08f), 0.0f, 0.8f);
                solids.push_back({x, z, 0.5f, top});
                ++made;
            }
        }
        auto grid = [&](float spacing, auto each) {
            const int cells = int(outer / spacing);
            for (int j = 0; j < cells; ++j)
                for (int i = 0; i < cells; ++i) each(-oh + (i + U(rng)) * spacing, -oh + (j + U(rng)) * spacing);
        };
        for (const std::string& id : kinds[Tall]) {
            const auto& v = A.byAsset[id];
            grid(2.4f, [&](float x, float z) {
                if (beyond(x, z) < half || streamDistance(x, z) < 2.0f || U(rng) >= 0.7f || !free(x, z, 0.4f)) return;
                put(variant(v), x, z, U(rng) * 6.2832f, 0.85f + 0.45f * U(rng), 0.1f, 0.03f, tintOf(0.1f), 0.0f, 0.3f);
            });
        }
        // the shrubs: whatever low plant stands over 90 cm, grown a little bigger
        std::vector<int> shrubs;
        for (Kind k : {Shrub, GroundPlant, Patch})
            for (const std::string& id : kinds[k])
                for (int p : A.byAsset[id])
                    if (A.protos[size_t(p)].bmax.y - A.protos[size_t(p)].bmin.y >= 0.9f) shrubs.push_back(p);
        if (!shrubs.empty())
            grid(1.1f, [&](float x, float z) {
                const float p = 0.75f * smoothstep(0.5f * walk, half + 2.0f, beyond(x, z));
                if (p <= 0.0f || streamDistance(x, z) < 1.3f || U(rng) >= p || !free(x, z, 0.3f)) return;
                put(variant(shrubs), x, z, U(rng) * 6.2832f, 1.2f + 0.7f * U(rng), 0.3f, 0.03f, tintOf(0.12f));
            });
    }

    // the fallen leaves over the patch, last and with their own sequence (nothing else moves for them): everywhere,
    // piled against what stands (trunks, rocks, logs), a few sunk in the bed. Each leaf is cut from its atlas, lies on
    // the ground's own triangles (not on the smooth height: it rests on what is drawn), its edges curled up and one end
    // lifted, a few millimetres over the leaves under it
    litter = OProtoCPU();
    litterProto = -1;
    litter.asset = "litter";
    litter.name = "litter";
    float shares = 0.0f;
    for (const LitterSet& s : litterSets) shares += s.leaves.empty() ? 0.0f : s.share;
    if (shares > 0.0f && dense_[4] > 0.0f) {
        const OMeshCPU& gm = ground.geoms[0];
        auto groundY = [&](float x, float z) {
            const float gx = (x + half) / res, gz = (z + half) / res;
            const int i = std::clamp(int(gx), 0, n - 2), j = std::clamp(int(gz), 0, n - 2);
            const float fx = std::clamp(gx - i, 0.0f, 1.0f), fz = std::clamp(gz - j, 0.0f, 1.0f);
            const float a = gm.pos[size_t(j) * n + i].y, b = gm.pos[size_t(j) * n + i + 1].y;
            const float c = gm.pos[size_t(j + 1) * n + i].y, d = gm.pos[size_t(j + 1) * n + i + 1].y;
            return fx + fz <= 1.0f ? a + fx * (b - a) + fz * (c - a) : d + (1.0f - fx) * (c - d) + (1.0f - fz) * (b - d);
        };
        std::vector<OMeshCPU> meshes(litterSets.size());
        for (size_t s = 0; s < litterSets.size(); ++s) meshes[s].material = litterSets[s].material;
        std::mt19937 lr(seed_ + 77u);
        auto L = [&]() { return U(lr); };
        const float cell = 0.09f;
        const int cells = int(size / cell);
        for (int j = 0; j < cells; ++j)
            for (int i = 0; i < cells; ++i) {
                const float x = -half + (i + L()) * cell, z = -half + (j + L()) * cell;
                const float d = streamDistance(x, z);
                float p = (0.45f + 0.35f * patchy(x, z, 0.4f, 0.3f, 0.7f, seed_ + 700u)) *
                          (d < 0.9f * streamWidth_ ? 0.15f : 0.4f + 0.6f * smoothstep(0.6f * streamWidth_, 1.5f * streamWidth_, d));
                bool inside = false;
                for (const Solid& s : solids) {
                    const float e = std::sqrt((x - s.x) * (x - s.x) + (z - s.z) * (z - s.z)) - s.r;
                    if (e < 0.0f) inside = true;
                    else if (e < 0.8f) p += 0.5f * (1.0f - e / 0.8f);
                }
                if (inside || L() >= std::min(1.0f, dense_[4] * p)) continue;
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
                OMeshCPU& m = meshes[si];
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
        for (OMeshCPU& m : meshes)
            if (!m.idx.empty()) {
                m.ao.assign(m.pos.size(), 1.0f);   // baked like the ground's: the leaves under the plants darken
                litter.geoms.push_back(std::move(m));
            }
        if (!litter.geoms.empty()) {
            litter.bmin = make_float3(-half, -2, -half);
            litter.bmax = make_float3(half, hill_ + 2, half);
            OPlace lp;
            lp.proto = litterProto = groundProto + (water.geoms.empty() ? 1 : 2);
            places.push_back(lp);
            size_t leaves = 0;
            for (const OMeshCPU& m : litter.geoms) leaves += m.idx.size() / 8;
            counts["litter"] = int(leaves);
        }
    }
    lo.y -= 1.0f;
    hi.y += 1.0f;
}
