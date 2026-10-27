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

enum Kind { GroundPlant, Patch, Tall, Rock, Stone, Log, Root, Tree, Moss };

Kind classify(const std::string& id, float height, float width) {
    auto has = [&](const char* k) { return id.find(k) != std::string::npos; };
    if (id.rfind("moss", 0) == 0) return Moss;   // moss sprigs (a mossy rock set is a rock)
    if (has("root")) return Root;
    if (has("trunk") || has("log")) return Log;
    if (has("stump")) return Rock;
    if (has("boulder") || has("rock") || has("stone")) return width > 0.6f ? Rock : Stone;
    if (height > 6.0f) return Tree;
    if (height > 2.2f) return Tall;
    if (has("anthurium") || has("calathea") || has("alocasia") || has("philodendron") || has("monstera")) return Patch;
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
}

float OrmanWorld::streamCentre(float x) const { return 1.8f * std::sin(0.14f * x + 0.6f) + 0.7f * std::sin(0.33f * x + 2.1f) - 1.5f; }

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
    ground.bmin = make_float3(-half, -2, -half);
    ground.bmax = make_float3(half, 2, half);
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
        const int nx = int(size / 0.1f) + 1, nv = 33;
        const float halfW = 2.2f * streamWidth_;
        for (int i = 0; i < nx; ++i) {
            const float x = -half + i * 0.1f, c = streamCentre(x), y = waterLevel(x);
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
        water.bmin = make_float3(-half, -2, -half);
        water.bmax = make_float3(half, 2, half);
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
    auto put = [&](int proto, float x, float z, float yaw, float s, float tilt, float sink, float3 tint, float lift = 0.0f) -> float {
        const OProtoCPU& P = A.protos[size_t(proto)];
        // the lowest ground under its footprint: nothing floats on a slope
        const float rx = 0.35f * s * std::max(P.bmax.x - P.bmin.x, P.bmax.z - P.bmin.z);
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
        lo.y = std::min(lo.y, y + s * P.bmin.y);
        hi.y = std::max(hi.y, y + s * P.bmax.y);
        return y + s * P.bmax.y;
    };
    auto variant = [&](const std::vector<int>& v) { return v[size_t(U(rng) * v.size()) % v.size()]; };
    auto tintOf = [&](float amount) {
        const float v = 1.0f + amount * (U(rng) - 0.5f) * 2.0f;
        return make_float3(v * (1.0f + 0.05f * (U(rng) - 0.5f)), v, v * (1.0f + 0.08f * (U(rng) - 0.5f)));
    };

    // the assets by what they are, the big ones first (the small ones fill around them)
    std::map<int, std::vector<std::string>> kinds;
    for (const auto& [id, protos] : A.byAsset) {
        float h = 0.0f, w = 0.0f;
        for (int p : protos) {
            h = std::max(h, A.protos[size_t(p)].bmax.y - A.protos[size_t(p)].bmin.y);
            w = std::max(w, std::max(A.protos[size_t(p)].bmax.x - A.protos[size_t(p)].bmin.x, A.protos[size_t(p)].bmax.z - A.protos[size_t(p)].bmin.z));
        }
        kinds[classify(id, h, w)].push_back(id);
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
    // understory in patches, ferns crowding the banks, moss along the water: a jittered grid, kept by a density
    auto scatter = [&](const std::string& id, float spacing, float sMin, float sMax, auto density) {
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
                put(proto, x, z, U(rng) * 6.2832f, s, 0.6f, 0.03f, tintOf(0.12f));
            }
    };
    unsigned salt = 0;
    for (const std::string& id : kinds[Tall]) {
        const unsigned sa = seed_ + 300u + salt++;
        scatter(id, 2.6f, 0.8f, 1.2f, [&, sa](float x, float z) {
            const float d = streamDistance(x, z);
            const float edge = smoothstep(0.6f * hw, hw, std::max(std::fabs(x), std::fabs(z)));   // denser at the edges: the walls
            return (d < 1.6f ? 0.0f : 0.35f) * (0.5f + 0.5f * smoothstep(0.45f, 0.6f, vnoise(x * 0.15f, z * 0.15f, sa))) + 0.4f * edge;
        });
    }
    for (const std::string& id : kinds[Patch]) {
        const unsigned sa = seed_ + 100u + salt++;
        scatter(id, 0.55f, 0.8f, 1.25f, [&, sa](float x, float z) {
            const float d = streamDistance(x, z);
            if (d < 1.0f) return 0.0f;
            const float patch = smoothstep(0.55f, 0.75f, vnoise(x * 0.35f, z * 0.35f, sa));
            return 0.8f * patch;
        });
    }
    for (const std::string& id : kinds[GroundPlant]) {
        const unsigned sa = seed_ + 200u + salt++;
        scatter(id, 0.7f, 0.7f, 1.35f, [&, sa](float x, float z) {
            const float d = streamDistance(x, z);
            if (d < 0.85f) return 0.0f;
            const float bank = 1.0f - smoothstep(1.5f, 5.0f, d);   // moist banks
            const float patch = smoothstep(0.35f, 0.7f, vnoise(x * 0.25f, z * 0.25f, sa));
            return 0.18f + 0.45f * bank + 0.3f * patch;
        });
    }
    for (const std::string& id : kinds[Moss]) {
        const unsigned sa = seed_ + 400u + salt++;
        // a moss sprig is two or three centimetres: carpets of them on the wet rim of the bed and in damp patches
        scatter(id, 0.07f, 0.8f, 1.6f, [&, sa](float x, float z) {
            const float d = streamDistance(x, z);
            const float rim = smoothstep(0.5f, 0.75f, d) * (1.0f - smoothstep(1.1f, 2.0f, d));
            const float patch = smoothstep(0.6f, 0.8f, vnoise(x * 0.5f, z * 0.5f, sa));
            return 0.85f * rim * (0.4f + 0.6f * vnoise(x * 2.0f, z * 2.0f, sa + 1u)) + 0.5f * patch;
        });
    }
    lo.y -= 1.0f;
    hi.y += 1.0f;
}
