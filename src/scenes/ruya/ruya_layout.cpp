// Composition of a cell: which island it holds, how tall it stands in its
// massif, and how structures and plants occupy the rock. The whole plan is a
// closed function of the cell coordinates, so the world regenerates identically.
#include "scenes/ruya/ruya_island.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <utility>

namespace ruya {
namespace {
constexpr float kTau = 2.0f * M_PIf;
constexpr float kIslandGap = 14.0f;
constexpr float kMinRadius = 7.0f;

constexpr float kFootprintStep = 0.25f;

// Ground samples of a prototype's solid triangles, one per 0.25 m of its own plan.
// Samples run across each triangle so a wall seen edge-on still marks its cells.
std::vector<float3> solidGroundSamples(const Mesh& mesh) {
    std::set<std::pair<int, int>> seen;
    std::vector<float3> samples;
    for (const uint3 t : mesh.indices) {
        const Vertex& a = mesh.vertices[t.x];
        const Vertex& b = mesh.vertices[t.y];
        const Vertex& c = mesh.vertices[t.z];
        if (isFoliage(a.material)) continue;
        const float longest = std::max({length(b.position - a.position), length(c.position - a.position),
                                        length(c.position - b.position)});
        const int n = std::max(1, int(std::ceil(longest / kFootprintStep)));
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n - i; ++j) {
                const float u = float(i) / float(n);
                const float v = float(j) / float(n);
                const float3 p = a.position * (1.0f - u - v) + b.position * u + c.position * v;
                if (seen.insert({int(std::floor(p.x / kFootprintStep)), int(std::floor(p.z / kFootprintStep))}).second)
                    samples.push_back(p);
            }
    }
    return samples;
}

// What placement needs to know about each prototype, measured from its own mesh once.
struct PrototypeMeasures {
    std::array<float, kPrototypeCount> radii{};                   // plan radius of the whole mesh
    std::array<std::vector<float3>, kPrototypeCount> roots;       // lowest non-leaf vertices, thinned out
    std::array<std::vector<float3>, kPrototypeCount> footprints;  // solid ground samples (structures only)
};
const PrototypeMeasures& prototypeMeasures() {
    static PrototypeMeasures measures;
    static std::once_flag once;
    std::call_once(once, [] {
        const auto meshes = buildPrototypes(1);
        for (size_t p = 0; p < meshes.size(); ++p) {
            float lowest = 1e30f;
            for (const Vertex& v : meshes[p].vertices) {
                measures.radii[p] = std::max(measures.radii[p], std::hypot(v.position.x, v.position.z));
                if (!isFoliage(v.material)) lowest = std::min(lowest, v.position.y);
            }
            std::vector<float3> band;
            for (const Vertex& v : meshes[p].vertices)
                if (!isFoliage(v.material) && v.position.y < lowest + 0.3f) band.push_back(v.position);
            const size_t stride = std::max<size_t>(band.size() / 64, 1);
            for (size_t i = 0; i < band.size(); i += stride) measures.roots[p].push_back(band[i]);
            if (isStructurePrototype(unsigned(p))) measures.footprints[p] = solidGroundSamples(meshes[p]);
        }
    });
    return measures;
}
const std::array<float, kPrototypeCount>& prototypeRadii() { return prototypeMeasures().radii; }
const std::array<std::vector<float3>, kPrototypeCount>& prototypeRoots() { return prototypeMeasures().roots; }

struct Sequence {
    unsigned seed;
    unsigned index = 0;
    float operator()() { return randomUnit(seed + 982451653U * ++index); }
    float range(float a, float b) { return lerpf(a, b, (*this)()); }
};

struct Traits {
    unsigned kind;
    float radius, top, closeness;
    bool hero;
};
float closenessTo(const MassifInfo& massif) {
    return std::exp(-(massif.distance * massif.distance) / (170.0f * 170.0f));
}
// What a cell would like to be, before negotiating with its neighbours.
Traits wantedTraits(const Cell& cell) {
    const MassifInfo massif = nearestHero(cell.x, cell.z);
    Traits traits{};
    traits.hero = massif.isHero;
    traits.closeness = closenessTo(massif);
    const float c = traits.closeness;
    Sequence draw{cell.seed + 0x54524149U};
    if (traits.hero) {
        traits.kind = draw() < 0.65f ? kKindMesa : kKindWooded;
        traits.radius = draw.range(46.0f, 58.0f);
        traits.top = draw.range(56.0f, 78.0f);
        return traits;
    }
    // The origin is the arrival point: a broad monastery stack standing in the open.
    if (cell.x == 0 && cell.z == 0) {
        traits.kind = kKindMesa;
        traits.radius = 34.0f;
        traits.top = 62.0f;
        return traits;
    }
    const float pick = draw();
    if (c > 0.55f) traits.kind = pick < 0.45f ? kKindTerrace : pick < 0.8f ? kKindWooded : kKindMesa;
    else if (c > 0.22f) traits.kind = cell.region;
    else traits.kind = pick < 0.55f ? kKindSpire : pick < 0.8f ? kKindWooded : kKindMesa;
    const float size = lerpf(16.0f, 36.0f, std::pow(c, 0.6f)) * draw.range(0.88f, 1.12f);
    traits.radius = traits.kind == kKindSpire ? size * 0.5f : traits.kind == kKindTerrace ? size * 1.1f : size;
    traits.top = lerpf(-16.0f, 38.0f, std::pow(c, 0.75f)) + draw.range(-8.0f, 8.0f);
    if (traits.kind == kKindSpire) traits.top += draw.range(10.0f, 26.0f);
    return traits;
}
// Neighbours negotiate radius so the massif never interpenetrates; heroes keep theirs.
Traits cellTraits(const Cell& cell) {
    Traits traits = wantedTraits(cell);
    if (traits.hero) return traits;
    float radius = traits.radius;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) continue;
            const Cell other = describeCell(cell.x + dx, cell.z + dz);
            if (other.scale == 0) continue;
            const float distance = std::hypot(other.offset.x - cell.offset.x, other.offset.z - cell.offset.z);
            const Traits wanted = wantedTraits(other);
            const float clear = distance / kIslandMaxReach - kIslandGap;
            const float limit = wanted.hero ? clear - wanted.radius : clear * traits.radius / (traits.radius + wanted.radius);
            radius = std::min(radius, limit);
        }
    traits.radius = std::max(radius, kMinRadius);
    return traits;
}

struct Scatter {
    CellPlan& plan;
    const Cell& cell;
    const Island& island;
    Sequence draw;
    struct Disc { float x, z, radius; };
    std::vector<Disc> taken;

    // Height is scaled by `stretch` on top of the uniform scale.
    void place(unsigned prototype, float x, float z, float y, float yaw, float scale, float stretch = 1.0f) {
        plan.pieces.push_back({make_float3(x, y, z), make_float3(scale, scale * stretch, scale), yaw, prototype,
                               hash(cell.seed + prototype * 167U + unsigned(plan.pieces.size())), cell.region});
    }
    // A plant or rock is added only if every root point is in the rock, which
    // also drops candidates over a joint, an alcove or the cap's undercut.
    bool placeHeld(unsigned prototype, float x, float z, float y, float yaw, float scale, float stretch = 1.0f) {
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        for (const float3& root : prototypeRoots()[prototype]) {
            const float3 world = make_float3(x + scale * (c * root.x + s * root.z), y + scale * stretch * root.y,
                                             z + scale * (-s * root.x + c * root.z));
            if (islandDistance(island, world) > 0.3f) return false;
        }
        place(prototype, x, z, y, yaw, scale, stretch);
        return true;
    }
    bool free(float x, float z, float radius) const {
        for (const Disc& d : taken)
            if (std::hypot(x - d.x, z - d.z) < radius + d.radius) return false;
        return true;
    }
    bool insideRim(float x, float z, float margin) const {
        const float angle = std::atan2(z - island.z, x - island.x);
        return std::hypot(x - island.x, z - island.z) + margin < islandRimRadius(island, angle);
    }
    // A footprint must be inside the rim and on ground flat enough for its roots.
    bool fits(float x, float z, float radius, float tolerance, float& height) const {
        if (!free(x, z, radius) || !insideRim(x, z, 1.0f)) return false;
        height = islandTop(island, x, z);
        float low = height;
        float high = height;
        for (int k = 0; k < 8; ++k) {
            const float a = kTau * float(k) / 8.0f;
            const float px = x + std::cos(a) * radius;
            const float pz = z + std::sin(a) * radius;
            if (!insideRim(px, pz, 0.4f)) return false;
            const float h = islandTop(island, px, pz);
            low = std::min(low, h);
            high = std::max(high, h);
        }
        return high - low <= tolerance;
    }
    void claim(float x, float z, float radius) { taken.push_back({x, z, radius}); }
};

float forestAt(const Island& island, float x, float z) {
    const float wide = (std::sin(x * 0.041f + float(island.seed % 628U) * 0.01f) * 0.5f + 0.5f) *
                       (std::sin(z * 0.037f + float(island.seed % 311U) * 0.02f) * 0.5f + 0.5f);
    const float bias = island.kind == kKindWooded ? 0.22f : island.kind == kKindTerrace ? 0.12f : island.kind == kKindMesa ? 0.0f : -0.12f;
    return 0.54f + (wide - 0.25f) * 1.3f + bias + 0.2f * (randomUnit(unsigned(int(x / 7.0f) * 73 + int(z / 7.0f) * 179) + island.seed) - 0.5f);
}

// Groves first, then trees, shrubs and ground cover in what remains, so the
// crown fills like planting: big masses placed, the gaps filled with understorey.
void dressCrown(Scatter& scatter, float richness) {
    const Island& island = scatter.island;
    const float step = std::clamp(island.radius * 0.2f, 4.5f, 7.5f);
    const int rings = std::max(1, int(island.radius / step));
    for (int pass = 0; pass < 3; ++pass)
        for (int ring = 0; ring < rings; ++ring) {
            const float along = (float(ring) + 0.4f) / float(rings);
            const float circle = kTau * along * island.radius;
            const int count = std::max(ring == 0 ? 1 : 3, int(circle / step));
            for (int k = 0; k < count; ++k) {
                const float angle = kTau * (float(k) + scatter.draw()) / float(count);
                const float reach = along * islandRimRadius(island, angle) * scatter.draw.range(0.8f, 0.98f);
                const float x = island.x + std::cos(angle) * reach;
                const float z = island.z + std::sin(angle) * reach;
                const float forest = forestAt(island, x, z);
                const float roll = scatter.draw();
                float height = 0;
                if (pass == 0) {
                    // Big masses first, thickets where a whole grove does not fit.
                    if (forest < 0.42f || roll > richness) continue;
                    const float scale = scatter.draw.range(0.75f, 1.05f);
                    const float radius = 8.6f * scale;
                    if (scatter.fits(x, z, radius, 2.8f, height)) {
                        const float mix = scatter.draw();
                        const unsigned grove = island.kind == kKindWooded ? (mix < 0.6f ? kProtoGroveDense : kProtoGroveBroad)
                                                                          : (mix < 0.5f ? kProtoGroveMixed : kProtoGroveBroad);
                        if (scatter.placeHeld(grove, x, z, height - 0.25f, scatter.draw() * kTau, scale))
                            scatter.claim(x, z, radius * 0.8f);
                    } else if (scatter.fits(x, z, 4.4f * scale, 1.8f, height) &&
                               scatter.placeHeld(kProtoThicket, x, z, height - 0.2f, scatter.draw() * kTau, scale)) {
                        scatter.claim(x, z, 4.4f * scale * 0.85f);
                    }
                    continue;
                }
                if (pass == 1) {
                    if (forest < 0.26f || roll > 0.7f || !scatter.fits(x, z, 2.4f, 2.0f, height)) continue;
                    const float pick = scatter.draw();
                    const unsigned tree = pick < 0.34f ? kProtoPineFlat : pick < 0.68f ? kProtoOlive : kProtoCypress;
                    if (scatter.placeHeld(tree, x, z, height - 0.2f, scatter.draw() * kTau, scatter.draw.range(0.72f, 1.15f)))
                        scatter.claim(x, z, 2.0f);
                    continue;
                }
                // Understorey: shrubs and mats in the gaps, a few rocks where it is bare.
                if (roll < 0.92f && scatter.fits(x, z, 2.8f, 2.2f, height)) {
                    const float pick = scatter.draw();
                    const unsigned prototype = pick < 0.5f ? kProtoShrubs : pick < 0.86f ? kProtoGroundCover : pick < 0.96f ? kProtoBoulders : kProtoRockStack;
                    if (scatter.placeHeld(prototype, x, z, height - 0.15f, scatter.draw() * kTau, scatter.draw.range(0.72f, 1.1f)))
                        scatter.claim(x, z, 2.2f);
                }
            }
        }
}

// Pines and shrubs that hold the broken edge of the crown.
void dressRim(Scatter& scatter, float richness) {
    const Island& island = scatter.island;
    const int count = int(kTau * island.radius / 5.5f);
    for (int k = 0; k < count; ++k) {
        const float angle = kTau * (float(k) + scatter.draw()) / float(count);
        const float roll = scatter.draw();
        const float rim = islandRimRadius(island, angle);
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        // Pines take the prominent corners of the plan, not an even spacing.
        const float corner = rim - 0.5f * (islandRimRadius(island, angle - 0.1f) + islandRimRadius(island, angle + 0.1f));
        if (roll < (corner > 0.25f ? 0.5f : 0.07f) * richness) {
            // A pine that grows out over the edge, rooted under the lip.
            const float depth = scatter.draw.range(0.8f, 5.5f);
            const float y = islandRimHeight(island, angle) - depth;
            const float wall = islandWallRadius(island, angle, y);
            const float yaw = -angle + scatter.draw.range(-0.5f, 0.5f);
            const bool lean = scatter.draw() < 0.55f;
            scatter.placeHeld(lean ? kProtoPineLean : kProtoPineCling, island.x + c * wall, island.z + s * wall, y, yaw,
                              scatter.draw.range(0.55f, 0.95f));
        } else if (roll < 0.62f * richness) {
            // Inset past the cap's undercut so the root is over solid rock.
            const float inset = rim * 0.05f + scatter.draw.range(0.8f, 2.6f);
            const float x = island.x + c * (rim - inset);
            const float z = island.z + s * (rim - inset);
            if (!scatter.free(x, z, 1.2f)) continue;
            const float y = islandTop(island, x, z);
            const unsigned prototype = roll < 0.4f * richness ? kProtoShrubs : kProtoHedge;
            // The hedge runs along local z; yaw -angle lays it along the rim tangent.
            scatter.placeHeld(prototype, x, z, y - 0.2f, -angle, prototype == kProtoHedge ? scatter.draw.range(0.6f, 0.9f) : scatter.draw.range(0.5f, 0.8f));
        }
    }
}

// Ledge plants on hard beds, shelf gardens, and pines on the open wall.
void dressWalls(Scatter& scatter, float richness) {
    const Island& island = scatter.island;
    const float bottom = std::max(island.top - 62.0f, -40.0f);
    for (int bed = bedIndexAt(island.top - 3.0f); bedFloor(bed + 1) > bottom; --bed) {
        const float y = bedFloor(bed + 1);
        const int samples = std::max(3, int(kTau * island.radius / 11.0f));
        for (int k = 0; k < samples; ++k) {
            const float angle = kTau * (float(k) + scatter.draw()) / float(samples);
            const float roll = scatter.draw();
            if (roll > 0.42f * richness) continue;
            float radius = 0;
            const float width = bedLedgeWidth(island, angle, bed, radius);
            if (width < 0.45f) continue;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const float r = radius + width * 0.55f;
            if (width > 0.9f && roll < 0.06f * richness) {
                scatter.placeHeld(kProtoPineCling, island.x + c * r, island.z + s * r, y - 0.5f, -angle + scatter.draw.range(-0.6f, 0.6f),
                                  scatter.draw.range(0.6f, 1.0f));
            } else {
                scatter.placeHeld(kProtoLedgeTuft, island.x + c * r, island.z + s * r, y - 0.42f, -angle + scatter.draw.range(-0.4f, 0.4f),
                                  scatter.draw.range(0.55f, 1.0f));
            }
        }
    }
    for (int column = island.coreColumns; column < island.columnCount; ++column) {
        const float topY = island.columns[column].topY;
        for (int k = 0; k < 36; ++k) {
            const float angle = kTau * (float(k) + scatter.draw()) / 36.0f;
            float inner = 0, outer = 0;
            if (!columnShelf(island, column, angle, inner, outer) || outer - inner < 3.0f) continue;
            const float roll = scatter.draw();
            if (roll > 0.42f * richness) continue;
            const float t = scatter.draw.range(0.3f, 0.75f);
            const float r = lerpf(inner, outer, t);
            const float y = topY - 0.32f * t;
            const float x = island.x + std::cos(angle) * r;
            const float z = island.z + std::sin(angle) * r;
            const float pick = scatter.draw();
            const unsigned prototype = pick < 0.35f ? kProtoShrubs : pick < 0.6f ? kProtoHedge : pick < 0.78f ? kProtoPineFlat : pick < 0.9f ? kProtoRubble : kProtoBoulders;
            scatter.placeHeld(prototype, x, z, y - 0.2f, scatter.draw() * kTau,
                              prototype == kProtoPineFlat ? scatter.draw.range(0.6f, 0.9f) : scatter.draw.range(0.7f, 1.0f));
        }
    }
}

// Structures keep this much open ground (in footprint cells) between one another.
constexpr int kStructureClearance = 2;
// Height factors for towers, spread so that cells whose (x + 2z) differ mod 7, which
// holds for every pair of adjacent cells, get towers of clearly different height.
constexpr float kTowerStretch[7] = {1.09f, 0.80f, 1.02f, 1.24f, 0.87f, 1.17f, 0.95f};
}  // namespace

bool isStructurePrototype(unsigned prototype) {
    switch (prototype) {
        case 8: case 9: case 10: case 11: case 12: case 13: case 16: case 17: case 19: case 20: case 23: return true;
        default: return false;
    }
}

std::vector<std::pair<int, int>> structureFootprint(const Piece& piece) {
    std::vector<std::pair<int, int>> cells;
    if (piece.prototype >= kPrototypeCount) return cells;
    const auto& samples = prototypeMeasures().footprints[piece.prototype];
    cells.reserve(samples.size());
    for (const float3& local : samples) {
        const float3 world = piecePoint(piece, local);
        cells.push_back({int(std::floor(world.x / kFootprintGrid)), int(std::floor(world.z / kFootprintGrid))});
    }
    std::sort(cells.begin(), cells.end());
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
    return cells;
}

CellPlan planCell(const Cell& cell) {
    CellPlan plan;
    if (cell.scale == 0) return plan;
    const auto draw = [&](unsigned channel) { return randomUnit(cell.seed + 877U * channel); };
    const Traits traits = cellTraits(cell);
    const unsigned kind = traits.kind;
    Island main = makeIsland(cell.offset.x, cell.offset.z, cell.seed, kind, traits.radius, traits.top);
    const auto& radii = prototypeRadii();
    struct Pending { unsigned prototype; float x, z, yaw, scale, stretch; };
    std::vector<Pending> structures;
    // Ground under every structure so far, widened by the clearance: a new one may not touch it.
    std::set<std::pair<int, int>> occupied;
    const auto touchesOccupied = [&](const Piece& candidate) {
        const auto cells = structureFootprint(candidate);
        for (const auto& ground : cells)
            if (occupied.count(ground)) return true;
        return false;
    };
    const auto occupy = [&](const Piece& piece) {
        for (const auto& [gx, gz] : structureFootprint(piece))
            for (int dz = -kStructureClearance; dz <= kStructureClearance; ++dz)
                for (int dx = -kStructureClearance; dx <= kStructureClearance; ++dx) occupied.insert({gx + dx, gz + dz});
    };
    const auto structure = [&](unsigned prototype, float along, float angle, float wanted, float stretch = 1.0f) {
        const float limit = 0.55f * main.radius / std::max(radii[prototype], 1.0f);
        const float scale = std::min(wanted, limit);
        if (scale < 0.3f) return;
        const float footprint = radii[prototype] * scale;
        // Pull the site inward until its whole footprint stands on the crown and clear of
        // the structures already placed; a blocked bearing is swung aside before giving up.
        for (const float swing : {0.0f, 0.4f, -0.4f, 0.8f, -0.8f})
            for (float t = along; t >= 0.0f; t -= 0.12f) {
                const float bearing = angle + swing;
                const float reach = t * islandRimRadius(main, bearing);
                const float x = main.x + std::cos(bearing) * reach;
                const float z = main.z + std::sin(bearing) * reach;
                bool inside = true;
                for (int k = 0; k < 12 && inside; ++k) {
                    const float a = kTau * float(k) / 12.0f;
                    const float px = x + std::cos(a) * footprint;
                    const float pz = z + std::sin(a) * footprint;
                    const float at = std::atan2(pz - main.z, px - main.x);
                    inside = std::hypot(px - main.x, pz - main.z) < 0.9f * islandRimRadius(main, at);
                }
                if (!inside) continue;
                const Piece candidate{make_float3(x, 0.0f, z), make_float3(scale, scale * stretch, scale), cell.yaw + bearing,
                                      prototype, 0, 0};
                if (touchesOccupied(candidate)) continue;
                occupy(candidate);
                addIslandSite(main, x, z, footprint + 0.6f);
                structures.push_back({prototype, x, z, cell.yaw + bearing, scale, stretch});
                return;
            }
    };
    // Towers differ in height from every neighbouring cell's tower.
    const float towerStretch = kTowerStretch[((cell.x + 2 * cell.z) % 7 + 7) % 7] * lerpf(0.985f, 1.015f, draw(10));
    const float heading = cell.yaw;
    if (kind == kKindMesa) {
        const unsigned designs[5] = {8, 9, 16, 19, 20};
        // The main building holds the centre; a tower stands at the outer corner.
        structure(designs[unsigned(draw(3) * 5) % 5], 0.14f, heading, lerpf(0.75f, 1.05f, draw(4)));
        if (draw(5) < 0.75f || traits.hero)
            structure(kProtoTower, 0.72f, heading + 2.4f + draw(6), std::min(lerpf(0.7f, 0.95f, draw(7)), main.radius / 28.0f), towerStretch);
        if (draw(8) < 0.5f) structure(13, 0.58f, heading - 2.1f, 0.75f);
    } else if (kind == kKindTerrace) {
        structure(11, 0.0f, heading, lerpf(0.85f, 1.1f, draw(3)));
        if (draw(4) < 0.7f) structure(12, 0.62f, heading + 1.9f + draw(5), 0.55f);
    } else if (kind == kKindSpire) {
        if (draw(3) < 0.4f && main.radius > 9.0f) structure(kProtoTower, 0.05f, heading, lerpf(0.42f, 0.62f, draw(11)), towerStretch);
    } else {
        structure(draw(3) < 0.5f ? 17U : 12U, 0.22f, heading, lerpf(0.55f, 0.8f, draw(4)));
        if (traits.hero && draw(5) < 0.6f) structure(kProtoChapel, 0.6f, heading + 2.8f, 0.9f);
    }
    plan.islands[plan.islandCount++] = main;

    Scatter scatter{plan, cell, plan.islands[0], Sequence{hash(cell.seed + 0x44524553U)}, {}};
    for (const Pending& s : structures) {
        if (!scatter.placeHeld(s.prototype, s.x, s.z, islandTop(main, s.x, s.z) - 0.3f, s.yaw, s.scale, s.stretch)) continue;
        scatter.claim(s.x, s.z, std::min(radii[s.prototype] * s.scale * 0.6f + 1.5f, 9.0f));
    }
    if (kind == kKindTerrace && draw(9) < 0.6f) {
        const float x = main.x + std::cos(heading + 3.6f) * 0.45f * main.radius;
        const float z = main.z + std::sin(heading + 3.6f) * 0.45f * main.radius;
        const Piece arc{make_float3(x, 0.0f, z), make_float3(0.85f, 0.85f, 0.85f), heading, 23U, 0, 0};
        if (scatter.free(x, z, 8.5f) && !touchesOccupied(arc) && scatter.placeHeld(23, x, z, islandTop(main, x, z) - 0.15f, heading, 0.85f))
            scatter.claim(x, z, 7.0f);
    }
    const float richness = kind == kKindWooded ? 0.95f : kind == kKindTerrace ? 0.85f : kind == kKindMesa ? 0.75f : 0.65f;
    dressCrown(scatter, richness);
    dressRim(scatter, richness);
    dressWalls(scatter, richness);

    // Satellite stacks stand on their own feet beside the main island, more of them near the hero.
    const float want = kind == kKindSpire ? 2.0f + 2.0f * draw(30) : traits.closeness > 0.3f ? 1.0f + 2.0f * draw(30) : draw(30) * 1.6f;
    const int satellites = int(want);
    for (int i = 0; i < satellites && plan.islandCount < 5; ++i) {
        const unsigned key = 40U + unsigned(i) * 7U;
        const float r = lerpf(6.0f, 13.0f, draw(key));
        const float angle = kTau * (float(i) + draw(key + 1)) / float(satellites);
        float distance = main.radius * kIslandMaxReach + r * kIslandMaxReach + lerpf(6.0f, 18.0f, draw(key + 2));
        distance = std::min(distance, kCellSize * 0.62f - r * kIslandMaxReach - 4.0f);
        bool clear = distance > main.radius * kIslandMaxReach + r * kIslandMaxReach + 2.0f;
        const float sx = main.x + std::cos(angle) * distance;
        const float sz = main.z + std::sin(angle) * distance;
        for (int j = 1; j < plan.islandCount && clear; ++j)
            clear = std::hypot(sx - plan.islands[j].x, sz - plan.islands[j].z) > (r + plan.islands[j].radius) * kIslandMaxReach + 3.0f;
        for (int dz = -1; dz <= 1 && clear; ++dz)
            for (int dx = -1; dx <= 1 && clear; ++dx) {
                if (dx == 0 && dz == 0) continue;
                const Cell other = describeCell(cell.x + dx, cell.z + dz);
                if (other.scale == 0) continue;
                const float otherRadius = wantedTraits(other).radius;
                clear = std::hypot(sx - other.offset.x, sz - other.offset.z) > (otherRadius + r) * kIslandMaxReach + 5.0f;
            }
        if (!clear) continue;
        const float height = traits.top + (kind == kKindSpire ? lerpf(-36.0f, 16.0f, draw(key + 3)) : lerpf(-32.0f, 10.0f, draw(key + 3)));
        const unsigned satelliteSeed = hash(hash(cell.seed + key) + 0x53415431U);
        plan.islands[plan.islandCount] = makeIsland(sx, sz, satelliteSeed, kKindSpire, r, height);
        const Island& satellite = plan.islands[plan.islandCount++];
        Scatter small{plan, cell, satellite, Sequence{hash(satelliteSeed + 0x44524553U)}, {}};
        if (r > 8.5f && draw(key + 4) < 0.5f) {
            const float x = satellite.x;
            const float z = satellite.z;
            if (small.placeHeld(kProtoPineFlat, x, z, islandTop(satellite, x, z) - 0.25f, draw(key + 5) * kTau, lerpf(0.6f, 0.95f, draw(key + 6))))
                small.claim(x, z, 2.5f);
        }
        dressCrown(small, 0.7f);
        dressRim(small, 0.75f);
        dressWalls(small, 0.6f);
    }
    return plan;
}

namespace {
// Plans are pure, so the cache only trades memory for frame time. Two
// generations replace an LRU: a cell seen in the last few frames stays.
class PlanCache {
public:
    // Planning runs outside the lock so that worker threads plan cells in parallel;
    // two threads racing on one cell compute equal plans and the first one is kept.
    std::shared_ptr<const CellPlan> get(const Cell& cell) {
        const std::pair<int, int> key{cell.x, cell.z};
        std::shared_ptr<const CellPlan> plan;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = current_.find(key);
            if (found != current_.end()) return found->second;
            const auto old = previous_.find(key);
            if (old != previous_.end()) plan = old->second;
        }
        if (!plan) plan = std::make_shared<const CellPlan>(planCell(cell));
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = current_.find(key);
        if (found != current_.end()) return found->second;
        if (current_.size() >= kGeneration) {
            previous_ = std::move(current_);
            current_.clear();
        }
        current_.emplace(key, plan);
        return plan;
    }

private:
    static constexpr size_t kGeneration = 1024;
    std::mutex mutex_;
    std::map<std::pair<int, int>, std::shared_ptr<const CellPlan>> current_, previous_;
};
}

std::shared_ptr<const CellPlan> cachedPlan(const Cell& cell) {
    static PlanCache cache;
    return cache.get(cell);
}

float3 piecePoint(const Piece& piece, float3 local) {
    const float3 point = local * piece.scale;
    const float cosine = std::cos(piece.yaw);
    const float sine = std::sin(piece.yaw);
    return piece.offset + make_float3(cosine * point.x + sine * point.z, point.y, -sine * point.x + cosine * point.z);
}

float3 localPoint(const Piece& piece, float3 world) {
    const float3 point = world - piece.offset;
    const float cosine = std::cos(piece.yaw);
    const float sine = std::sin(piece.yaw);
    return make_float3((cosine * point.x - sine * point.z) / piece.scale.x, point.y / piece.scale.y,
                       (sine * point.x + cosine * point.z) / piece.scale.z);
}

void assembleCell(const Cell& cell, std::vector<Piece>& pieces) {
    const CellPlan plan = planCell(cell);
    pieces.insert(pieces.end(), plan.pieces.begin(), plan.pieces.end());
}

}
