// Islands are polar height functions: a rim radius per angle and depth below
// the rim, and a top height per point. Each kind keeps its own ranges, so a
// spire field, a monastery mesa, a terraced garden and a wooded crown read as
// different places, while seeds keep any two islands of a kind apart.
#include "scenes/ruya/ruya_island.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace ruya {
namespace {
constexpr float kTau = 2.0f * M_PIf;
constexpr int kWallRows = 150;

float valueNoise(float3 p, unsigned seed) {
    const float fx = std::floor(p.x);
    const float fy = std::floor(p.y);
    const float fz = std::floor(p.z);
    const int x = int(fx);
    const int y = int(fy);
    const int z = int(fz);
    const float tx = p.x - fx;
    const float ty = p.y - fy;
    const float tz = p.z - fz;
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sy = ty * ty * (3.0f - 2.0f * ty);
    const float sz = tz * tz * (3.0f - 2.0f * tz);
    const auto at = [&](int i, int j, int k) {
        return randomUnit(unsigned(x + i) * 73856093U ^ unsigned(y + j) * 19349663U ^ unsigned(z + k) * 83492791U ^ seed);
    };
    const float a = lerpf(lerpf(at(0, 0, 0), at(1, 0, 0), sx), lerpf(at(0, 1, 0), at(1, 1, 0), sx), sy);
    const float b = lerpf(lerpf(at(0, 0, 1), at(1, 0, 1), sx), lerpf(at(0, 1, 1), at(1, 1, 1), sx), sy);
    return lerpf(a, b, sz);
}
float fbm(float3 p, unsigned seed, int octaves) {
    float sum = 0;
    float amplitude = 0.5f;
    float total = 0;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += amplitude * valueNoise(p, seed + unsigned(octave) * 1013U);
        total += amplitude;
        p = p * 2.03f;
        amplitude *= 0.5f;
    }
    return sum / total;
}
float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Radius of the wall at an angle and a depth below the rim.
float wallRadius(const Island& island, float angle, float depth) {
    float shape = 1.0f;
    for (int k = 0; k < 6; ++k) shape += island.lobes[k] * std::sin(float(k + 2) * angle + island.phases[k]);
    const float y = island.top - depth;
    const float span = std::max(island.top - kIslandBase, 1.0f);
    const float widening = 1.0f + island.flare * std::pow(std::clamp(depth / span, 0.0f, 1.0f), 1.25f);
    const float c = std::cos(angle) * island.radius * 0.085f;
    const float s = std::sin(angle) * island.radius * 0.085f;
    // Stretched along y, the noise reads as rain fluting on limestone.
    // Ridged noise gives sharp crests between the flutes.
    const float flute = 0.5f - std::abs(2.0f * fbm(make_float3(c, y * 0.018f, s), island.seed + 5U, 4) - 1.0f);
    // Sharpened sine: hard ledges where softer beds weathered back.
    const float phase = std::sin(y * 0.31f + 5.0f * valueNoise(make_float3(c * 0.5f, y * 0.04f, s * 0.5f), island.seed + 9U));
    const float bands = phase > 0 ? std::pow(phase, 0.35f) : -std::pow(-phase, 3.0f);
    const float lip = 1.0f - 0.035f * std::exp(-depth / 2.2f);
    // Metre-scale relief is absolute, so small stacks are as rough up close as large ones.
    const float3 q = make_float3(std::cos(angle) * island.radius, y, std::sin(angle) * island.radius);
    const float relief = 1.1f * (fbm(q * 0.11f, island.seed + 13U, 5) - 0.5f) + 0.35f * (valueNoise(q * 0.55f, island.seed + 17U) - 0.5f);
    return island.radius * (shape * widening * lip + island.gully * 2.0f * flute + island.strata * bands) + relief;
}
float topHeightRaw(const Island& island, float x, float z, float rimRadius) {
    const float dx = x - island.x;
    const float dz = z - island.z;
    const float s = std::sqrt(dx * dx + dz * dz) / std::max(rimRadius, 1.0f);
    const float inside = std::min(s, 1.0f);
    float h = island.dome * island.radius * (1.0f - inside * inside);
    if (island.terrace > 0) {
        const float v = h / island.terrace;
        const float step = std::floor(v);
        h = island.terrace * (step + smooth(0.78f, 1.0f, v - step));
    }
    h += island.top + 2.2f * (fbm(make_float3(x * 0.045f, 0.0f, z * 0.045f), island.seed + 21U, 4) - 0.5f) +
         0.45f * (valueNoise(make_float3(x * 0.31f, 1.7f, z * 0.31f), island.seed + 33U) - 0.5f);
    if (s > 0.84f) h -= 3.4f * (s - 0.84f) * (s - 0.84f) / (0.16f * 0.16f);
    return h;
}
float topAt(const Island& island, float x, float z, float rimRadius) {
    float h = topHeightRaw(island, x, z, rimRadius);
    for (int i = 0; i < island.siteCount; ++i) {
        const Site& site = island.sites[i];
        const float d = std::hypot(x - site.x, z - site.z);
        h = lerpf(h, site.height, 1.0f - smooth(site.radius, site.radius * 1.55f + 1.5f, d));
    }
    return h;
}
float rimRadiusAt(const Island& island, float x, float z) {
    return wallRadius(island, std::atan2(z - island.z, x - island.x), 0.0f);
}

// The footprint of each structure prototype, measured from its own mesh once.
const std::array<float, kPrototypeCount>& prototypeRadii() {
    static std::array<float, kPrototypeCount> radii{};
    static std::once_flag once;
    std::call_once(once, [] {
        const auto meshes = buildPrototypes(1);
        for (size_t i = 0; i < meshes.size(); ++i)
            for (const Vertex& v : meshes[i].vertices)
                radii[i] = std::max(radii[i], std::hypot(v.position.x, v.position.z));
    });
    return radii;
}

Island makeIsland(float x, float z, unsigned seed, unsigned kind, float radius, float top) {
    Island island{};
    island.x = x;
    island.z = z;
    island.seed = seed;
    island.radius = radius;
    island.top = top;
    const auto draw = [&](unsigned k) { return randomUnit(seed + 613U * k); };
    switch (kind) {
    case 0:  // monastery mesa: sheer walls, nearly flat crown
        island.flare = lerpf(0.08f, 0.30f, draw(1));
        island.gully = lerpf(0.035f, 0.075f, draw(2));
        island.strata = lerpf(0.016f, 0.034f, draw(3));
        island.dome = lerpf(0.01f, 0.06f, draw(4));
        break;
    case 1:  // terraced garden: stepped crown, broad base
        island.flare = lerpf(0.20f, 0.45f, draw(1));
        island.gully = lerpf(0.025f, 0.05f, draw(2));
        island.strata = lerpf(0.018f, 0.032f, draw(3));
        island.dome = lerpf(0.30f, 0.48f, draw(4));
        island.terrace = lerpf(3.2f, 5.5f, draw(5));
        break;
    case 2:  // spire: slender, deeply fluted
        island.flare = lerpf(0.30f, 0.65f, draw(1));
        island.gully = lerpf(0.08f, 0.15f, draw(2));
        island.strata = lerpf(0.012f, 0.03f, draw(3));
        island.dome = lerpf(0.10f, 0.35f, draw(4));
        break;
    default:  // wooded crown: rounded, softer walls
        island.flare = lerpf(0.15f, 0.35f, draw(1));
        island.gully = lerpf(0.04f, 0.08f, draw(2));
        island.strata = lerpf(0.008f, 0.02f, draw(3));
        island.dome = lerpf(0.18f, 0.32f, draw(4));
        break;
    }
    const float roughness = kind == 2 ? 0.10f : kind == 0 ? 0.075f : 0.12f;
    for (int k = 0; k < 6; ++k) {
        island.lobes[k] = 1.7f * roughness * (draw(10U + unsigned(k)) * 2.0f - 0.7f) / float(k + 1);
        island.phases[k] = kTau * draw(20U + unsigned(k));
    }
    return island;
}

void addSite(Island& island, float x, float z, float radius) {
    if (island.siteCount == 4) return;
    const float rim = rimRadiusAt(island, x, z);
    island.sites[island.siteCount++] = {x, z, radius, topHeightRaw(island, x, z, rim)};
}
}

float islandTop(const Island& island, float x, float z) { return topAt(island, x, z, rimRadiusAt(island, x, z)); }

float islandDistance(const Island& island, float3 p) {
    const float angle = std::atan2(p.z - island.z, p.x - island.x);
    const float rim = wallRadius(island, angle, 0.0f);
    const float rimY = topAt(island, island.x + std::cos(angle) * rim, island.z + std::sin(angle) * rim, rim);
    const float r = std::hypot(p.x - island.x, p.z - island.z);
    const float wall = r - wallRadius(island, angle, std::max(rimY - p.y, 0.0f));
    // Outside the rim circle the crown no longer bounds the rock: the wall
    // below the rim may bulge past it.
    const float crown = r <= rim ? p.y - topAt(island, p.x, p.z, rim) : p.y - rimY;
    return std::max(wall, crown);
}

float3 islandGradient(const Island& island, float3 p) {
    constexpr float e = 0.05f;
    const float3 g = make_float3(islandDistance(island, p + make_float3(e, 0, 0)) - islandDistance(island, p - make_float3(e, 0, 0)),
                                 islandDistance(island, p + make_float3(0, e, 0)) - islandDistance(island, p - make_float3(0, e, 0)),
                                 islandDistance(island, p + make_float3(0, 0, e)) - islandDistance(island, p - make_float3(0, 0, e)));
    return g * (0.5f / e);
}

Mesh islandMesh(const Island& island) {
    const int around = std::clamp(int(std::round(kTau * island.radius / 0.5f)) & ~3, 128, 512);
    const int rings = std::clamp(int(island.radius / 0.6f), 16, 66);
    const int rows = rings + kWallRows;
    Mesh mesh;
    mesh.vertices.reserve(size_t(around) * rows + 1);
    mesh.indices.reserve(size_t(around) * rows * 2);
    std::vector<float> rim(around);
    std::vector<float> rimY(around);
    for (int i = 0; i < around; ++i) {
        const float angle = kTau * float(i) / float(around);
        rim[i] = wallRadius(island, angle, 0.0f);
        rimY[i] = topAt(island, island.x + std::cos(angle) * rim[i], island.z + std::sin(angle) * rim[i], rim[i]);
    }
    mesh.vertices.push_back({make_float3(island.x, islandTop(island, island.x, island.z), island.z), {}, kMaterialStone});
    for (int row = 1; row <= rows; ++row) {
        for (int i = 0; i < around; ++i) {
            const float angle = kTau * float(i) / float(around);
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            float3 p;
            if (row <= rings) {
                const float radius = rim[i] * float(row) / float(rings);
                p = make_float3(island.x + c * radius, 0, island.z + s * radius);
                p.y = topAt(island, p.x, p.z, rim[i]);
            } else {
                const float t = float(row - rings) / float(kWallRows);
                const float depth = (rimY[i] - kIslandBase) * (0.25f * t + 0.75f * t * t * t);
                const float radius = wallRadius(island, angle, depth);
                p = make_float3(island.x + c * radius, rimY[i] - depth, island.z + s * radius);
            }
            mesh.vertices.push_back({p, {}, kMaterialStone});
        }
    }
    const auto index = [&](int row, int i) { return row == 0 ? 0U : unsigned(1 + (row - 1) * around + (i % around)); };
    for (int i = 0; i < around; ++i) mesh.indices.push_back(make_uint3(0, index(1, i + 1), index(1, i)));
    for (int row = 1; row < rows; ++row)
        for (int i = 0; i < around; ++i) {
            const unsigned a = index(row, i);
            const unsigned b = index(row, i + 1);
            const unsigned c = index(row + 1, i);
            const unsigned d = index(row + 1, i + 1);
            mesh.indices.push_back(make_uint3(a, b, d));
            mesh.indices.push_back(make_uint3(a, d, c));
        }
    std::vector<float3> normals(mesh.vertices.size(), make_float3(0, 0, 0));
    for (const uint3& t : mesh.indices) {
        const float3 n = cross(mesh.vertices[t.y].position - mesh.vertices[t.x].position,
                               mesh.vertices[t.z].position - mesh.vertices[t.x].position);
        normals[t.x] += n;
        normals[t.y] += n;
        normals[t.z] += n;
    }
    const float sign = normals[0].y < 0 ? -1.0f : 1.0f;
    for (size_t i = 0; i < normals.size(); ++i) {
        const float3 n = normals[i];
        const float l = std::sqrt(dot(n, n));
        mesh.vertices[i].normal = l > 1e-12f ? n * (sign / l) : make_float3(0, 1, 0);
    }
    return mesh;
}

CellPlan planCell(const Cell& cell) {
    CellPlan plan;
    if (cell.scale == 0) return plan;
    const auto draw = [&](unsigned channel) { return randomUnit(cell.seed + 877U * channel); };
    const unsigned kind = cell.region;
    const float radius = kind == 0 ? lerpf(21.0f, 31.0f, draw(1)) : kind == 1 ? lerpf(27.0f, 38.0f, draw(1))
                       : kind == 2 ? lerpf(9.0f, 14.0f, draw(1)) : lerpf(25.0f, 36.0f, draw(1));
    const float top = kind == 0 ? lerpf(6.0f, 42.0f, draw(2)) : kind == 1 ? lerpf(-6.0f, 16.0f, draw(2))
                    : kind == 2 ? lerpf(20.0f, 64.0f, draw(2)) : lerpf(-2.0f, 26.0f, draw(2));
    Island main = makeIsland(cell.offset.x, cell.offset.z, cell.seed, kind, radius, top);
    const auto& radii = prototypeRadii();
    const auto place = [&](unsigned prototype, float x, float z, float yaw, float scale, float y) {
        plan.pieces.push_back({make_float3(x, y, z), make_float3(scale, scale, scale), yaw, prototype,
                               hash(cell.seed + prototype * 167U + unsigned(plan.pieces.size())), cell.region});
    };
    // Structures choose their site first; the crown is levelled under them.
    struct Pending { unsigned prototype; float x, z, yaw, scale; };
    std::vector<Pending> structures;
    const auto structure = [&](unsigned prototype, float along, float angle, float wanted) {
        const float limit = 0.5f * main.radius / std::max(radii[prototype], 1.0f);
        const float scale = std::min(wanted, limit);
        if (scale < 0.25f) return;
        const float reach = along * wallRadius(main, angle, 0.0f);
        const float x = main.x + std::cos(angle) * reach;
        const float z = main.z + std::sin(angle) * reach;
        addSite(main, x, z, radii[prototype] * scale);
        structures.push_back({prototype, x, z, cell.yaw + angle, scale});
    };
    const float heading = cell.yaw;
    if (kind == 0) {
        const unsigned designs[5] = {8, 9, 16, 19, 20};
        structure(designs[unsigned(draw(3) * 5) % 5], 0.12f, heading, lerpf(0.6f, 0.85f, draw(4)));
        if (draw(5) < 0.7f) structure(10, 0.58f, heading + 2.3f, lerpf(0.55f, 0.75f, draw(6)));
        if (draw(7) < 0.4f) structure(13, 0.55f, heading - 2.0f, 0.6f);
    } else if (kind == 1) {
        structure(11, 0.0f, heading, lerpf(0.8f, 1.05f, draw(3)));
        if (draw(4) < 0.6f) structure(12, 0.6f, heading + 1.9f + draw(5), 0.6f);
    } else if (kind == 2) {
        if (draw(3) < 0.45f) structure(10, 0.0f, heading, 0.5f);
    } else {
        structure(draw(3) < 0.5f ? 17U : 12U, 0.2f, heading, lerpf(0.55f, 0.75f, draw(4)));
    }
    for (const Pending& s : structures)
        place(s.prototype, s.x, s.z, s.yaw, s.scale, islandTop(main, s.x, s.z) - 0.3f);
    if (kind == 1 && draw(9) < 0.6f) {
        const float x = main.x + std::cos(heading + 3.6f) * 0.45f * main.radius;
        const float z = main.z + std::sin(heading + 3.6f) * 0.45f * main.radius;
        place(23, x, z, heading, 0.85f, islandTop(main, x, z) - 0.15f);
    }
    plan.islands[plan.islandCount++] = main;
    // Satellite stacks stand on their own feet beside the main island.
    const int satellites = kind == 2 ? 2 + int(draw(30) * 3) : draw(30) < 0.5f ? 1 + int(draw(31) * 2) : 0;
    for (int i = 0; i < satellites && plan.islandCount < 5; ++i) {
        const unsigned key = 40U + unsigned(i) * 7U;
        const float r = kind == 2 ? lerpf(6.0f, 12.0f, draw(key)) : lerpf(5.0f, 11.0f, draw(key));
        const float angle = kTau * (float(i) + draw(key + 1)) / float(satellites);
        float distance = main.radius * 1.15f + r + lerpf(5.0f, 16.0f, draw(key + 2));
        distance = std::min(distance, kCellSize * 0.5f - r - 6.0f);
        bool clear = distance > main.radius * 1.12f + r;
        for (int j = 1; j < plan.islandCount && clear; ++j)
            clear = std::hypot(main.x + std::cos(angle) * distance - plan.islands[j].x,
                               main.z + std::sin(angle) * distance - plan.islands[j].z) > r + plan.islands[j].radius * 1.2f + 3.0f;
        if (!clear) continue;
        const float height = top + (kind == 2 ? lerpf(-40.0f, 14.0f, draw(key + 3)) : lerpf(-30.0f, 8.0f, draw(key + 3)));
        Island satellite = makeIsland(main.x + std::cos(angle) * distance, main.z + std::sin(angle) * distance,
                                      hash(cell.seed + key), 2, r, height);
        plan.islands[plan.islandCount++] = satellite;
        if (draw(key + 4) < 0.5f)
            place(15, satellite.x, satellite.z, draw(key + 5) * kTau, lerpf(0.5f, 0.9f, draw(key + 6)),
                  islandTop(satellite, satellite.x, satellite.z) - 0.2f);
    }
    // Trees and loose stones on the main crown, kept off the levelled sites.
    const int trees = kind == 3 ? 16 + int(draw(50) * 10) : kind == 1 ? 6 + int(draw(50) * 6)
                    : kind == 0 ? 2 + int(draw(50) * 4) : 1 + int(draw(50) * 2);
    for (int tree = 0; tree < trees; ++tree) {
        const unsigned key = 72U + unsigned(tree) * 5U;
        const float angle = draw(key) * kTau;
        const float along = std::sqrt(lerpf(0.04f, 0.6f, draw(key + 1)));
        const float reach = along * wallRadius(main, angle, 0.0f);
        const float x = main.x + std::cos(angle) * reach;
        const float z = main.z + std::sin(angle) * reach;
        bool free = true;
        for (int i = 0; i < main.siteCount; ++i)
            free = free && std::hypot(x - main.sites[i].x, z - main.sites[i].z) > main.sites[i].radius + 2.0f;
        if (!free) continue;
        const bool cypress = kind == 3 ? draw(key + 2) < 0.35f : draw(key + 2) < 0.6f;
        place(cypress ? 15U : 14U, x, z, draw(key + 4) * kTau, lerpf(0.55f, 1.35f, draw(key + 3)),
              islandTop(main, x, z) - 0.25f);
    }
    for (int rock = 0; rock < 5; ++rock) {
        const unsigned key = 155U + unsigned(rock) * 4U;
        const float angle = draw(key) * kTau;
        const float along = lerpf(0.55f, 0.78f, draw(key + 2));
        const float reach = along * wallRadius(main, angle, 0.0f);
        const float x = main.x + std::cos(angle) * reach;
        const float z = main.z + std::sin(angle) * reach;
        place(22, x, z, angle, lerpf(0.3f, 0.85f, draw(key + 1)), islandTop(main, x, z) - 0.2f);
    }
    return plan;
}
}
