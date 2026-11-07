// Test public world contracts against independent projection and geometry checks.
#include "scenes/ruya/ruya_world.h"
#include "scenes/ruya/ruya_collision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <utility>

namespace ruya {
namespace {
using Key = std::pair<int, int>;

bool sameCell(const Cell& a, const Cell& b) {
    return a.x == b.x && a.z == b.z && a.region == b.region && a.seed == b.seed &&
           a.offset.x == b.offset.x && a.offset.y == b.offset.y && a.offset.z == b.offset.z &&
           a.scale == b.scale && a.yaw == b.yaw;
}

bool finiteVector(float3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool validMesh(const Mesh& mesh, size_t& invalid) {
    invalid = 0;
    if (mesh.vertices.empty() || mesh.indices.empty()) ++invalid;
    for (const auto& vertex : mesh.vertices) {
        const double normalLength = double(vertex.normal.x) * vertex.normal.x +
                                    double(vertex.normal.y) * vertex.normal.y +
                                    double(vertex.normal.z) * vertex.normal.z;
        if (!finiteVector(vertex.position) || !finiteVector(vertex.normal) ||
            normalLength < 0.98 || normalLength > 1.02 || vertex.material > kMaterialWater) ++invalid;
    }
    for (const auto triangle : mesh.indices) {
        if (triangle.x >= mesh.vertices.size() || triangle.y >= mesh.vertices.size() ||
            triangle.z >= mesh.vertices.size()) {
            ++invalid;
            continue;
        }
        const auto a = mesh.vertices[triangle.x].position;
        const auto b = mesh.vertices[triangle.y].position;
        const auto c = mesh.vertices[triangle.z].position;
        const double abx = double(b.x) - a.x;
        const double aby = double(b.y) - a.y;
        const double abz = double(b.z) - a.z;
        const double acx = double(c.x) - a.x;
        const double acy = double(c.y) - a.y;
        const double acz = double(c.z) - a.z;
        const double nx = aby * acz - abz * acy;
        const double ny = abz * acx - abx * acz;
        const double nz = abx * acy - aby * acx;
        if (!(nx * nx + ny * ny + nz * nz > 1e-20)) ++invalid;
    }
    return invalid == 0;
}

bool identicalMesh(const Mesh& a, const Mesh& b) {
    return a.vertices.size() == b.vertices.size() && a.indices.size() == b.indices.size() &&
           std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(Vertex)) == 0 &&
           std::memcmp(a.indices.data(), b.indices.data(), a.indices.size() * sizeof(uint3)) == 0;
}

// This projects transformed vertices, not the culler's bounds or its plane test.
bool projectsInside(float3 local, const Piece& cell, const Camera& camera, double aspect, double farDistance) {
    const double cosine = std::cos(double(cell.yaw));
    const double sine = std::sin(double(cell.yaw));
    const double x = cell.offset.x + (cosine * local.x * cell.scale.x + sine * local.z * cell.scale.z) - camera.eye.x;
    const double y = cell.offset.y + cell.scale.y * local.y - camera.eye.y;
    const double z = cell.offset.z + (-sine * local.x * cell.scale.x + cosine * local.z * cell.scale.z) - camera.eye.z;
    const double sy = std::sin(double(camera.yaw));
    const double cy = std::cos(double(camera.yaw));
    const double sp = std::sin(double(camera.pitch));
    const double cp = std::cos(double(camera.pitch));
    const double horizontal = cy * x + sy * z;
    const double vertical = -sy * sp * x + cp * y + cy * sp * z;
    const double depth = sy * cp * x + sp * y - cy * cp * z;
    const double tangent = std::tan(double(camera.fovY) * 3.14159265358979323846 / 360.0);
    return depth > 0.01 && x * x + y * y + z * z < farDistance * farDistance &&
           std::abs(horizontal) < depth * tangent * aspect * 0.999 &&
           std::abs(vertical) < depth * tangent * 0.999;
}

std::map<Key, Cell> cellMap(const std::vector<Cell>& cells) {
    std::map<Key, Cell> result;
    for (const auto& cell : cells) result[{cell.x, cell.z}] = cell;
    return result;
}

// A two-triangle wall with no thickness. Entry, free travel, sliding and
// the outer edge are independently observable from the analytic plane.
void collisionChecks(int& failures) {
    const float3 normal = make_float3(0, 0, 1);
    Mesh wall;
    wall.vertices = {{{-4, -4, 0}, normal, 0}, {{4, -4, 0}, normal, 0},
                     {{4, 4, 0}, normal, 0}, {{-4, 4, 0}, normal, 0}};
    wall.indices = {make_uint3(0, 1, 2), make_uint3(0, 2, 3)};
    const Piece panel{{-6, 3, 11}, {1.6f, 0.52f, 0.74f}, 0.7f, 0, 0, 0};
    const std::vector<Piece> pieces{panel};
    CollisionWorld collision;
    collision.build({wall});
    const auto contact = [&](float3 start, float3 end, const std::vector<Piece>& geometry) {
        return localPoint(panel, collision.move(piecePoint(panel, start), piecePoint(panel, end), geometry));
    };
    const auto impact = contact({0.25f, 0.32f, 53}, {0.25f, 0.32f, -67}, pieces);
    const auto reverse = contact({0.25f, 0.32f, -31}, {0.25f, 0.32f, 82}, pieces);
    const auto negative = contact({0.25f, 0.32f, 53}, {0.25f, 0.32f, -67}, {});
    const auto glide = contact({-2, 0.18f, 1.5f}, {1, 0.18f, -2}, pieces);
    const auto edge = contact({4.09f, 0.3f, 1.5f}, {4.09f, 0.3f, 0}, pieces);
    const auto outside = contact({4.3f, 0.3f, 1.5f}, {4.3f, 0.3f, -2}, pieces);
    const auto report = [&](const char* name, bool ok) {
        std::printf("[%s] ruya %s\n", ok ? "GECTI" : "KALDI", name);
        if (!ok) ++failures;
    };
    const float r = CollisionWorld::radius;
    report("carpisma-ince-duvar-hizli-cift-yon", std::abs(impact.z * panel.scale.z - r) < 0.004f &&
                                               std::abs(reverse.z * panel.scale.z + r) < 0.004f);
    report("carpisma-negatif-kontrol-duvarsiz-gecer", negative.z < -66.9f);
    report("carpisma-duvar-boyunca-kayma", std::abs(glide.x - 1.0f) < 0.004f &&
                                          std::abs(glide.z * panel.scale.z - r) < 0.004f);
    report("carpisma-kenar-ve-acik-bosluk", edge.x > 4.09f && edge.z > 0.08f && outside.z < -1.99f);
}
}

int testWorld() {
    int failures = 0;
    auto report = [&](const char* name, bool passed, size_t value = 0) {
        std::printf("[%s] ruya %s: %zu\n", passed ? "GECTI" : "KALDI", name, value);
        if (!passed) ++failures;
    };
    auto prototypes = buildPrototypes(0);
    const auto repeated = buildPrototypes(0);
    report("prototip-sayisi", prototypes.size() == kPrototypeCount && repeated.size() == prototypes.size(),
           prototypes.size());
    if (prototypes.empty() || prototypes.size() != repeated.size()) return 1;

    size_t invalid = 0;
    size_t outsideBounds = 0;
    size_t totalVertices = 0;
    size_t totalTriangles = 0;
    double largestRadius = 0;
    size_t changed = 0;
    std::set<unsigned long long> digests;
    for (size_t index = 0; index < prototypes.size(); ++index) {
        size_t bad = 0;
        validMesh(prototypes[index], bad);
        invalid += bad;
        totalVertices += prototypes[index].vertices.size();
        totalTriangles += prototypes[index].indices.size();
        for (const auto& vertex : prototypes[index].vertices) {
            const double x = vertex.position.x;
            const double y = double(vertex.position.y) + 20.0;
            const double z = vertex.position.z;
            largestRadius = std::max(largestRadius, std::sqrt(x * x + y * y + z * z));
            if (x * x + y * y + z * z > sqr(kCellBoundsRadius)) ++outsideBounds;
        }
        if (!identicalMesh(prototypes[index], repeated[index]) ||
            fingerprint(prototypes[index]) != fingerprint(repeated[index])) ++changed;
        digests.insert(fingerprint(prototypes[index]));
    }
    report("geometri-sonlu-normal-indeks-alan", invalid == 0, invalid);
    report("butun-vertexler-culling-sinirinda", outsideBounds == 0, outsideBounds);
    std::printf("ruya detail0 etkin min1: %zu vertex, %zu ucgen, sinir merkezine en uzak %.6f m\n",
                totalVertices, totalTriangles, largestRadius);
    report("yeniden-uretim-bayt-ayni", changed == 0, changed);
    report("prototipler-farkli", digests.size() == prototypes.size(), digests.size());

    if (!prototypes.front().vertices.empty()) {
        const auto originalHash = fingerprint(prototypes.front());
        Mesh mutation = prototypes.front();
        mutation.vertices.front().position.x += 0.125f;
        report("karma-konum-degisimini-gorur", fingerprint(mutation) != originalHash);
        mutation = prototypes.front();
        mutation.vertices.front().material = (mutation.vertices.front().material + 1) % 5;
        report("karma-malzeme-degisimini-gorur", fingerprint(mutation) != originalHash);
        mutation = prototypes.front();
        mutation.vertices.front().normal.x += 0.125f;
        report("karma-normal-degisimini-gorur", fingerprint(mutation) != originalHash);
        size_t bad = 0;
        mutation.vertices.front().normal = make_float3(0, 0, 0);
        report("bozuk-normal-negatif-kontrol", !validMesh(mutation, bad));
        if (!mutation.indices.empty()) {
            mutation = prototypes.front();
            std::swap(mutation.indices.front().x, mutation.indices.front().y);
            report("karma-ucgen-degisimini-gorur", fingerprint(mutation) != originalHash);
            mutation = prototypes.front();
            mutation.indices.front().x = unsigned(mutation.vertices.size());
            report("bozuk-indeks-negatif-kontrol", !validMesh(mutation, bad));
            mutation = prototypes.front();
            mutation.indices.front().y = mutation.indices.front().x;
            report("sifir-alan-negatif-kontrol", !validMesh(mutation, bad));
        }
    }

    constexpr float kAspect = 2560.0f / 1440.0f;
    constexpr float kTestFar = 360.0f;
    Camera camera;
    const auto original = cellMap(visibleCells(camera, kAspect, kTestFar));
    report("gorunen-hucre-var", !original.empty(), original.size());
    Camera away = camera;
    away.yaw += M_PIf;
    const auto turned = cellMap(visibleCells(away, kAspect, kTestFar));
    const auto returned = cellMap(visibleCells(camera, kAspect, kTestFar));
    size_t evicted = 0;
    size_t mismatch = 0;
    for (const auto& [key, cell] : original) {
        if (!turned.contains(key)) ++evicted;
        const auto found = returned.find(key);
        if (found == returned.end() || !sameCell(cell, found->second) ||
            !sameCell(cell, describeCell(key.first, key.second))) ++mismatch;
    }
    report("arkadaki-hucreler-listeden-cikar", evicted > 0, evicted);
    report("geri-bakista-ayni-dunya", mismatch == 0, mismatch);

    size_t retained = 0;
    mismatch = 0;
    for (int step = 1; step <= 12; ++step) {
        Camera moved = camera;
        moved.eye.x += step * 0.31f;
        moved.eye.y += step * 0.13f;
        moved.yaw += step * 0.006f;
        moved.pitch -= step * 0.002f;
        const auto next = cellMap(visibleCells(moved, kAspect, kTestFar));
        for (const auto& [key, cell] : original) {
            const auto found = next.find(key);
            if (found == next.end()) continue;
            ++retained;
            if (!sameCell(cell, found->second)) ++mismatch;
        }
    }
    report("gorunen-hucre-donus-ve-ucusta-sabit", mismatch == 0 && retained > 0, retained);

    size_t missed = 0;
    size_t projected = 0;
    size_t duplicate = 0;
    for (int view = 0; view < 8; ++view) {
        Camera probe = camera;
        probe.yaw = float(view) * (M_PIf / 4.0f);
        probe.pitch = view % 2 ? -0.28f : 0.32f;
        probe.eye.x = view % 2 ? -31.7f : 28.3f;
        const auto listed = visibleCells(probe, kAspect, kTestFar);
        const auto active = cellMap(listed);
        duplicate += listed.size() - active.size();
        const int centerX = int(std::floor(probe.eye.x / kCellSize));
        const int centerZ = int(std::floor(probe.eye.z / kCellSize));
        for (int z = centerZ - 8; z <= centerZ + 8; ++z) {
            for (int x = centerX - 8; x <= centerX + 8; ++x) {
                const auto cell = describeCell(x, z);
                std::vector<Piece> pieces;
                assembleCell(cell, pieces);
                bool inside = false;
                for (const auto& piece : pieces) {
                    const auto& mesh = prototypes.at(piece.prototype);
                    for (size_t vertex = 0; vertex < mesh.vertices.size(); vertex += 17) {
                        if (projectsInside(mesh.vertices[vertex].position, piece, probe, kAspect, kTestFar)) {
                            inside = true;
                            break;
                        }
                    }
                    if (inside) break;
                }
                if (!inside) continue;
                ++projected;
                if (!active.contains({x, z})) ++missed;
            }
        }
    }
    report("frustum-bagimsiz-kose-izdusumu", missed == 0 && projected > 0, missed);
    report("gorunurlukte-tekrar-yok", duplicate == 0, duplicate);

    std::array<std::set<std::array<unsigned, kPrototypeCount>>, 4> layouts;
    std::array<float3, kPrototypeCount> lower{}, upper{};
    for (size_t i = 0; i < prototypes.size(); ++i) {
        lower[i] = upper[i] = prototypes[i].vertices.front().position;
        for (const auto& vertex : prototypes[i].vertices) {
            const auto p = vertex.position;
            lower[i] = make_float3(std::min(lower[i].x, p.x), std::min(lower[i].y, p.y), std::min(lower[i].z, p.z));
            upper[i] = make_float3(std::max(upper[i].x, p.x), std::max(upper[i].y, p.y), std::max(upper[i].z, p.z));
        }
    }
    std::vector<Piece> pieces, regenerated;
    size_t badAssembly = 0, emptyCells = 0;
    float largestBound = 0;
    for (int z = -16; z < 16; ++z) for (int x = -16; x < 16; ++x) {
        const Cell cell = describeCell(x, z);
        pieces.clear();
        regenerated.clear();
        assembleCell(cell, pieces);
        assembleCell(describeCell(x, z), regenerated);
        if (pieces.size() != regenerated.size() ||
            (!pieces.empty() && std::memcmp(pieces.data(), regenerated.data(), pieces.size() * sizeof(Piece)) != 0)) ++badAssembly;
        if (pieces.empty()) { ++emptyCells; continue; }
        std::array<unsigned, kPrototypeCount> signature{};
        for (const Piece& piece : pieces) {
            const unsigned i = piece.prototype;
            ++signature.at(i);
            for (unsigned k = 0; k < 8; ++k) {
                const float3 corner = make_float3(k & 1 ? lower[i].x : upper[i].x, k & 2 ? lower[i].y : upper[i].y,
                                                  k & 4 ? lower[i].z : upper[i].z);
                const float3 p = piecePoint(piece, corner) - cell.offset - make_float3(0, kCellBoundsY * cell.scale, 0);
                largestBound = std::max(largestBound, length(p) / cell.scale);
            }
        }
        layouts[cell.region].insert(signature);
    }
    size_t distinct = 0;
    for (const auto& region : layouts) distinct += region.size();
    report("yerlesim-bilesen-sayilari-farkli", distinct > 96, distinct);
    report("yerlesim-bayt-ayni", badAssembly == 0, badAssembly);
    report("yerlesim-bos-koridor", emptyCells > 120 && emptyCells < 450, emptyCells);
    report("yerlesim-donusumlu-kose-siniri", largestBound < kCellBoundsRadius, size_t(largestBound * 1000));
    collisionChecks(failures);

    Camera level;
    level.yaw = 0;
    level.pitch = 0;
    float3 U, V, W;
    cameraBasis(level, kAspect, U, V, W);
    report("ekran-sagi-arti-x", U.x > 0 && std::abs(U.y) < 1e-5f && std::abs(U.z) < 1e-5f && W.z < 0);
    Camera right = level;
    Camera left = level;
    moveCamera(right, 0, 0, 0, 1, 0, 0.1f);
    moveCamera(left, 0, 0, 0, -1, 0, 0.1f);
    report("d-saga-a-sola", right.eye.x > level.eye.x && left.eye.x < level.eye.x);
    Camera flying = level;
    moveCamera(flying, 0, 0, 12, 12, 0, 0.1f);
    report("capraz-ucus-hizi-sabit", std::abs(length(flying.eye - level.eye) - 1.2f) < 1e-4f);
    Camera rising = level;
    moveCamera(rising, 0, 0, 0, 0, 12, 0.1f);
    report("serbest-yukselis", std::abs(rising.eye.y - level.eye.y - 1.2f) < 1e-4f);
    Camera turnedCamera = level;
    moveCamera(turnedCamera, 1e6f, 1e6f, 0, 0, 0, 0.1f);
    const double angle = std::hypot(double(turnedCamera.yaw - level.yaw), double(turnedCamera.pitch - level.pitch));
    report("donus-vektor-hizi-sinirli", angle <= double(kTurnRate) * 0.1 + 1e-5);
    report("fare-saga-bakisi-saga-cevirir", turnedCamera.yaw > level.yaw);
    Camera split = level;
    for (int part = 0; part < 10; ++part) moveCamera(split, 1e6f, 0, 0, 0, 0, 0.01f);
    Camera single = level;
    moveCamera(single, 1e6f, 0, 0, 0, 0, 0.1f);
    report("donus-dt-bolumlemesi", std::abs(split.yaw - single.yaw) < 1e-5f);
    report("fov-varsayilani-sinirli", level.fovY == kFovY && level.fovY > 0 && level.fovY < 90);
    Camera extreme = level;
    extreme.fovY = 179;
    moveCamera(extreme, 0, 0, 0, 0, 0, 0);
    report("fov-ust-sinir", extreme.fovY <= kFovY);
    extreme.fovY = 0;
    moveCamera(extreme, 0, 0, 0, 0, 0, 0);
    report("fov-alt-sinir", extreme.fovY >= 36);
    const auto paused = extreme;
    moveCamera(extreme, 1e6f, 1e6f, 36, 36, 36, 0);
    report("sifir-dt-hareket-etmez", extreme.eye.x == paused.eye.x && extreme.eye.y == paused.eye.y &&
           extreme.eye.z == paused.eye.z && extreme.yaw == paused.yaw && extreme.pitch == paused.pitch);
    std::printf("ruya dunya sonucu: %d basarisiz kosul\n", failures);
    return failures == 0 ? 0 : 1;
}
}
