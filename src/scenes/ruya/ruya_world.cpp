// Geometry is an immutable recipe in island-local coordinates. Resident cells
// only place these recipes; neither the camera nor frame number changes them.
#include "scenes/ruya/ruya_world.h"

#include <algorithm>
#include <cmath>

namespace ruya {
namespace {
inline constexpr float kTau = 2.0f * M_PIf;

float3 safeNormal(float3 value, float3 fallback) {
    const float magnitude = dot(value, value);
    return magnitude > 1e-18f ? value / std::sqrt(magnitude) : normalize(fallback);
}

float noise(float3 point, unsigned seed) {
    const int ix = int(std::floor(point.x));
    const int iy = int(std::floor(point.y));
    const int iz = int(std::floor(point.z));
    const float fx = point.x - float(ix);
    const float fy = point.y - float(iy);
    const float fz = point.z - float(iz);
    const float tx = fx * fx * (3.0f - 2.0f * fx);
    const float ty = fy * fy * (3.0f - 2.0f * fy);
    const float tz = fz * fz * (3.0f - 2.0f * fz);
    float result = 0.0f;
    for (int z = 0; z != 2; ++z)
        for (int y = 0; y != 2; ++y)
            for (int x = 0; x != 2; ++x) {
                const unsigned key = unsigned(ix + x) * 0x8da6b343U ^ unsigned(iy + y) * 0xd8163841U ^
                                     unsigned(iz + z) * 0xcb1ab31fU ^ seed;
                const float weight = (x ? tx : 1.0f - tx) * (y ? ty : 1.0f - ty) * (z ? tz : 1.0f - tz);
                result += weight * (2.0f * randomUnit(key) - 1.0f);
            }
    return result;
}

void triangle(Mesh& mesh, unsigned a, unsigned b, unsigned c) {
    const Vertex& va = mesh.vertices[a];
    const Vertex& vb = mesh.vertices[b];
    const Vertex& vc = mesh.vertices[c];
    const float3 normal = cross(vb.position - va.position, vc.position - va.position);
    if (dot(normal, normal) <= 1e-14f) return;
    if (dot(normal, va.normal + vb.normal + vc.normal) < 0.0f) std::swap(b, c);
    mesh.indices.push_back(make_uint3(a, b, c));
}

// The finite difference follows the actual displaced surface, rather than the
// smooth primitive underneath it. Winding is derived from those same normals.
template <class Position, class NormalHint>
void surface(Mesh& mesh, int columns, int rows, unsigned material, Position position, NormalHint hint) {
    const unsigned first = unsigned(mesh.vertices.size());
    for (int row = 0; row <= rows; ++row) {
        const float v = float(row) / float(rows);
        for (int column = 0; column <= columns; ++column) {
            const float u = float(column) / float(columns);
            const float3 point = position(u, v);
            const float3 expected = hint(u, v);
            const float3 du = position(u + 0.0001f, v) - position(u - 0.0001f, v);
            const float3 dv = position(u, v + 0.0001f) - position(u, v - 0.0001f);
            float3 normal = safeNormal(cross(du, dv), expected);
            if (dot(normal, expected) < 0.0f) normal = -normal;
            mesh.vertices.push_back({point, normal, material});
        }
    }
    const unsigned stride = unsigned(columns + 1);
    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < columns; ++column) {
            const unsigned a = first + unsigned(row) * stride + unsigned(column);
            triangle(mesh, a, a + 1, a + stride);
            triangle(mesh, a + 1, a + stride + 1, a + stride);
        }
}

void quad(Mesh& mesh, float3 a, float3 b, float3 c, float3 d, unsigned material) {
    const float3 normal = safeNormal(cross(b - a, c - a), make_float3(0, 1, 0));
    const unsigned first = unsigned(mesh.vertices.size());
    for (float3 point : {a, b, c, d}) mesh.vertices.push_back({point, normal, material});
    triangle(mesh, first, first + 1, first + 2);
    triangle(mesh, first, first + 2, first + 3);
}

void box(Mesh& mesh, float3 center, float3 x, float3 y, float3 z, unsigned material) {
    const float3 axes[] = {normalize(x), normalize(y), normalize(z)};
    const float extent[] = {length(x), length(y), length(z)};
    const float bevel = std::min(0.10f, std::min({extent[0], extent[1], extent[2]}) * 0.24f);
    const float core[] = {extent[0] - bevel, extent[1] - bevel, extent[2] - bevel};
    for (int face = 0; face < 6; ++face) {
        const int normalAxis = face / 2;
        const int uAxis = (normalAxis + 1) % 3;
        const int vAxis = (normalAxis + 2) % 3;
        const float sign = face % 2 ? 1.0f : -1.0f;
        const unsigned first = unsigned(mesh.vertices.size());
        for (int row = 0; row != 4; ++row)
            for (int column = 0; column != 4; ++column) {
                const float uValues[] = {-extent[uAxis], -core[uAxis], core[uAxis], extent[uAxis]};
                const float vValues[] = {-extent[vAxis], -core[vAxis], core[vAxis], extent[vAxis]};
                float local[3] = {};
                local[normalAxis] = extent[normalAxis] * sign;
                local[uAxis] = uValues[column];
                local[vAxis] = vValues[row];
                float3 closest = make_float3(0, 0, 0);
                float3 delta = make_float3(0, 0, 0);
                for (int axis = 0; axis < 3; ++axis) {
                    const float value = clampf(local[axis], -core[axis], core[axis]);
                    closest += axes[axis] * value;
                    delta += axes[axis] * (local[axis] - value);
                }
                const float3 normal = normalize(delta);
                mesh.vertices.push_back({center + closest + normal * bevel, normal, material});
            }
        for (unsigned row = 0; row != 3; ++row)
            for (unsigned column = 0; column != 3; ++column) {
                const unsigned a = first + row * 4 + column;
                triangle(mesh, a, a + 1, a + 4);
                triangle(mesh, a + 1, a + 5, a + 4);
            }
    }
}

void tube(Mesh& mesh, float3 start, float3 end, float radius, int sides, unsigned material) {
    const float3 axis = safeNormal(end - start, make_float3(0, 1, 0));
    float3 tangent;
    float3 bitangent;
    makeBasis(axis, tangent, bitangent);
    surface(mesh, sides, 1, material,
            [&](float u, float v) {
                const float angle = u * kTau;
                return lerp3(start, end, v) + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
            },
            [&](float u, float) { return tangent * std::cos(u * kTau) + bitangent * std::sin(u * kTau); });
    for (int side = 0; side < sides; ++side) {
        const float a = kTau * float(side) / float(sides);
        const float b = kTau * float(side + 1) / float(sides);
        const float3 pa = (tangent * std::cos(a) + bitangent * std::sin(a)) * radius;
        const float3 pb = (tangent * std::cos(b) + bitangent * std::sin(b)) * radius;
        const unsigned first = unsigned(mesh.vertices.size());
        mesh.vertices.push_back({start, -axis, material});
        mesh.vertices.push_back({start + pa, -axis, material});
        mesh.vertices.push_back({start + pb, -axis, material});
        triangle(mesh, first, first + 1, first + 2);
        mesh.vertices.push_back({end, axis, material});
        mesh.vertices.push_back({end + pa, axis, material});
        mesh.vertices.push_back({end + pb, axis, material});
        triangle(mesh, first + 3, first + 4, first + 5);
    }
}

void stone(Mesh& mesh, float3 center, float3 radius, unsigned seed, int detail, unsigned material) {
    surface(mesh, 12 * detail, 6 * detail, material,
            [&](float u, float v) {
                const float latitude = v * M_PIf;
                const float3 direction = make_float3(std::sin(latitude) * std::cos(u * kTau), std::cos(latitude),
                                                     std::sin(latitude) * std::sin(u * kTau));
                const auto block = [](float value) { return std::copysign(std::pow(std::abs(value), 0.58f), value); };
                const float3 shape = make_float3(block(direction.x), block(direction.y), block(direction.z));
                const float displacement = 1.0f + 0.12f * noise(direction * 3.2f, seed);
                return center + shape * radius * displacement;
            },
            [&](float u, float v) {
                return make_float3(std::sin(v * M_PIf) * std::cos(u * kTau) / radius.x,
                                   std::cos(v * M_PIf) / radius.y,
                                   std::sin(v * M_PIf) * std::sin(u * kTau) / radius.z);
            });
}

float rimRadius(float angle, unsigned seed) {
    return 22.0f * (1.0f + 0.145f * std::sin(angle * 3.0f + float(seed % 13U)) +
                   0.075f * std::sin(angle * 5.0f - float(seed % 17U)));
}

enum class IslandKind { Monastery, Garden, Monument };

void island(Mesh& mesh, unsigned seed, int detail, IslandKind kind = IslandKind::Monastery) {
    const int around = 128 * detail;
    const int rows = 48 * detail;
    surface(mesh, around, rows, kMaterialStone,
            [&](float u, float v) {
                const float t = clampf(v, 0.0f, 1.0f);
                const float angle = u * kTau;
                const float exponent = kind == IslandKind::Garden ? 0.40f : 0.56f;
                const float taper = std::pow(std::max(0.0f, 1.0f - t), exponent);
                const float3 sample = make_float3(std::cos(angle) * 4.0f, t * 13.0f, std::sin(angle) * 4.0f);
                const float broad = noise(sample, seed);
                const float ridge = std::sin(angle * 13.0f + 3.3f * broad + t * 2.0f);
                const float joint = std::pow(std::max(0.0f, std::cos(angle * 7.0f + float(seed % 7U))), 18.0f);
                const float strata = kind == IslandKind::Monastery
                    ? 0.075f * std::sin(t * 33.0f + 1.6f * broad) + 0.11f * std::exp(-sqr((t - 0.12f) / 0.035f))
                    : 0.12f * noise(sample * make_float3(0.9f, 0.45f, 0.9f) + make_float3(0, broad * 2.0f, 0), seed + 3U);
                const float radius = rimRadius(angle, seed) * taper *
                                     (1.0f + std::sin(M_PIf * t) * (0.19f * broad + 0.09f * ridge - 0.22f * joint + strata));
                const float depth = kind == IslandKind::Monastery ? 60.0f + 14.0f * randomUnit(seed + 1U)
                                  : kind == IslandKind::Garden ? 43.0f + 18.0f * randomUnit(seed + 1U)
                                                              : 69.0f + 12.0f * randomUnit(seed + 1U);
                const float y = -depth * t + 1.1f * std::sin(t * M_PIf) * noise(sample * 2.0f, seed + 1U);
                const float bend = std::sin(M_PIf * t);
                return make_float3(std::cos(angle) * radius + bend * (4.5f + 2.0f * broad), y,
                                   std::sin(angle) * radius + bend * 4.0f * std::sin(t * 5.0f + float(seed % 5U)));
            },
            [](float u, float v) { return make_float3(std::cos(u * kTau), -0.3f - 0.7f * v, std::sin(u * kTau)); });
    surface(mesh, around, 24 * detail, kMaterialStone,
            [&](float u, float v) {
                const float angle = u * kTau;
                const float radius = rimRadius(angle, seed) * v;
                const float3 point = make_float3(std::cos(angle) * radius, 0, std::sin(angle) * radius);
                const float relief = 0.22f * noise(point * 0.8f, seed) * smoothstepf(16.4f, 18.0f, radius) *
                                     smoothstepf(0.0f, 0.12f, 1.0f - v);
                return point + make_float3(0, relief, 0);
            },
            [](float, float) { return make_float3(0, 1, 0); });
}

void ring(Mesh& mesh, float radius, float width, float y, float height, int segments, unsigned material) {
    for (int side = 0; side != 4; ++side) {
        surface(mesh, segments, 1, material,
                [&](float u, float v) {
                    const float angle = u * kTau;
                    const float r = (side < 2) ? radius - width * 0.5f + v * width
                                              : radius + (side == 2 ? -0.5f : 0.5f) * width;
                    const float h = (side < 2) ? y + float(side) * height : y + v * height;
                    return make_float3(r * std::cos(angle), h, r * std::sin(angle));
                },
                [&](float u, float) {
                    if (side < 2) return make_float3(0, side == 0 ? -1.0f : 1.0f, 0);
                    const float sign = side == 2 ? -1.0f : 1.0f;
                    return make_float3(std::cos(u * kTau) * sign, 0, std::sin(u * kTau) * sign);
                });
    }
}

void arcSlab(Mesh& mesh, float3 center, float radius, float width, float height, float start, float sweep,
             int segments, unsigned material, float weathering = 0.0f) {
    const auto point = [&](float u, float radial, float vertical) {
        const float angle = start + u * sweep;
        const float r = radius + (radial - 0.5f) * width + weathering * std::sin(angle * 17.0f + 0.7f);
        return center + make_float3(std::cos(angle) * r, vertical * height, std::sin(angle) * r);
    };
    for (int side = 0; side != 4; ++side) {
        surface(mesh, segments, 1, material,
                [&](float u, float v) { return side < 2 ? point(u, v, float(side))
                                                       : point(u, float(side - 2), v); },
                [&](float u, float) {
                    if (side < 2) return make_float3(0, side == 0 ? -1.0f : 1.0f, 0);
                    const float sign = side == 2 ? -1.0f : 1.0f;
                    return make_float3(std::cos(start + u * sweep) * sign, 0, std::sin(start + u * sweep) * sign);
                });
    }
    for (int end = 0; end != 2; ++end) {
        const float u = float(end);
        if (end == 0) quad(mesh, point(u, 0, 0), point(u, 0, 1), point(u, 1, 1), point(u, 1, 0), material);
        else quad(mesh, point(u, 0, 0), point(u, 1, 0), point(u, 1, 1), point(u, 0, 1), material);
    }
}

void geology(Mesh& mesh, unsigned seed, int detail) {
    const float depth = 60.0f + 14.0f * randomUnit(seed + 1U);
    for (int ledge = 0; ledge < 5; ++ledge) {
        const unsigned key = seed + unsigned(ledge) * 2971U;
        const float angle = kTau * randomUnit(key);
        const float y = -4.0f - float(ledge) * 4.8f;
        const float radius = 14.0f - float(ledge) * 0.8f;
        const float width = 22.0f - float(ledge) * 1.4f;
        arcSlab(mesh, make_float3(0, y, 0), radius, width,
                0.9f + randomUnit(key + 2U), angle, 0.9f + randomUnit(key + 3U), 40 * detail,
                ledge % 3 == 0 ? kMaterialDarkStone : kMaterialStone, 0.35f);
    }
    for (int rock = 0; rock < 85; ++rock) {
        const unsigned key = seed + unsigned(rock) * 1013U;
        const float angle = kTau * randomUnit(key);
        const float t = 0.03f + 0.66f * randomUnit(key + 1U);
        const float radius = rimRadius(angle, seed) * std::pow(1.0f - t, 0.56f);
        const float bend = std::sin(M_PIf * t);
        const float3 center = make_float3(std::cos(angle) * radius + 4.5f * bend, -depth * t,
                                          std::sin(angle) * radius + bend * 4.0f * std::sin(t * 5.0f + float(seed % 5U)));
        const float size = 0.65f + 2.2f * randomUnit(key + 2U);
        stone(mesh, center, make_float3(size, size * (0.4f + 0.55f * randomUnit(key + 3U)), size * 0.85f),
              key, detail, rock % 8 == 0 ? kMaterialDarkStone : kMaterialStone);
    }
    for (int spur = 0; spur < 3; ++spur) {
        const unsigned key = seed + unsigned(spur) * 4001U;
        const float angle = kTau * randomUnit(key);
        const float3 center = make_float3(std::cos(angle) * 17.0f, -18.0f - float(spur) * 9.0f,
                                          std::sin(angle) * 17.0f);
        stone(mesh, center, make_float3(4.5f, 7.0f + float(spur), 4.0f), key, detail * 2, kMaterialStone);
    }
}

void arch(Mesh& mesh, float3 left, float3 right, float springHeight, float rise, float roofHeight) {
    const float3 along = normalize(right - left);
    const float3 depth = normalize(cross(along, make_float3(0, 1, 0)));
    const float3 center = (left + right) * 0.5f + make_float3(0, springHeight, 0);
    const float radius = length(right - left) * 0.5f - 0.48f;
    constexpr int kBlocks = 17;
    for (int block = 0; block < kBlocks; ++block) {
        const float a = (float(block) + 0.025f) / float(kBlocks) * M_PIf;
        const float b = (float(block + 1) - 0.025f) / float(kBlocks) * M_PIf;
        const auto corner = [&](float theta, float outer, float front) {
            return center + along * (std::cos(theta) * (radius + outer * 0.40f)) +
                   make_float3(0, std::sin(theta) * (rise + outer * 0.40f), 0) + depth * front;
        };
        const float3 p[] = {corner(a, 0, -0.58f), corner(b, 0, -0.58f), corner(b, 1, -0.58f), corner(a, 1, -0.58f),
                            corner(a, 0, 0.58f), corner(b, 0, 0.58f), corner(b, 1, 0.58f), corner(a, 1, 0.58f)};
        constexpr unsigned faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3},
                                         {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
        for (const auto& face : faces) quad(mesh, p[face[0]], p[face[1]], p[face[2]], p[face[3]], kMaterialStone);
    }
    const float top = left.y + roofHeight;
    for (int face = 0; face != 2; ++face) {
        const float sign = face == 0 ? -1.0f : 1.0f;
        surface(mesh, 32, 1, kMaterialStone,
                [&](float u, float v) {
                    const float theta = u * M_PIf;
                    const float3 lower = center + along * (std::cos(theta) * (radius + 0.40f)) +
                                         make_float3(0, std::sin(theta) * (rise + 0.40f), 0);
                    return make_float3(lower.x, lerpf(lower.y, top, v), lower.z) + depth * (sign * 0.57f);
                },
                [&](float, float) { return depth * sign; });
    }
}

void column(Mesh& mesh, float3 base, float height, float radius, int detail) {
    const int segments = 16 * detail;
    surface(mesh, segments, 10 * detail, kMaterialStone,
            [&](float u, float v) {
                const float angle = u * kTau;
                const float profile = 0.88f + 0.12f * std::sin(M_PIf * v) + 0.035f * std::cos(12.0f * angle);
                const float r = radius * profile;
                return base + make_float3(r * std::cos(angle), v * height, r * std::sin(angle));
            },
            [](float u, float) { return make_float3(std::cos(u * kTau), 0, std::sin(u * kTau)); });
    for (int cap = 0; cap != 2; ++cap) {
        const float y = base.y + float(cap) * height;
        box(mesh, make_float3(base.x, y, base.z), make_float3(radius * 1.5f, 0, 0), make_float3(0, 0.18f, 0),
            make_float3(0, 0, radius * 1.5f), kMaterialStone);
        ring(mesh, radius * 1.03f, radius * 0.25f, y - 0.12f, 0.24f, segments, kMaterialBronze);
        // ring() is island-centered; the translated collars belong to this column.
        const unsigned collarVertices = unsigned((segments + 1) * 2 * 4);
        for (size_t i = mesh.vertices.size() - collarVertices; i < mesh.vertices.size(); ++i) {
            mesh.vertices[i].position.x += base.x;
            mesh.vertices[i].position.z += base.z;
        }
    }
}

void arcade(Mesh& mesh, unsigned prototype, int detail) {
    const int bays = 5 + int(prototype % 3U);
    const int stories = 1 + int(prototype % 3U);
    const float radius = 14.0f + 0.50f * float(prototype % 3U);
    const float floorHeight = 7.5f;
    const float start = 2.45f + float(prototype % 4U) * 0.11f;
    const float sweep = 3.65f + float(prototype % 3U) * 0.23f;
    for (int story = 0; story < stories; ++story) {
        const float baseY = 0.35f + float(story) * floorHeight;
        const float ringRadius = radius - 0.4f * float(story);
        const float storyStart = start + float(story) * 0.09f;
        const float storySweep = sweep - float(story) * 0.24f;
        arcSlab(mesh, make_float3(0, baseY - 0.35f, 0), ringRadius, 3.7f, 0.46f, storyStart, storySweep,
                96 * detail, kMaterialStone);
        arcSlab(mesh, make_float3(0, baseY - 0.40f, 0), ringRadius, 4.0f, 0.12f, storyStart, storySweep,
                96 * detail, kMaterialDarkStone);
        const float step = storySweep / float(bays);
        for (int bay = 0; bay < bays; ++bay) {
            const float angle = storyStart + float(bay) * step;
            const float next = angle + step;
            const float3 left = make_float3(std::cos(angle) * ringRadius, baseY, std::sin(angle) * ringRadius);
            const float3 right = make_float3(std::cos(next) * ringRadius, baseY, std::sin(next) * ringRadius);
            const float3 direction = normalize(right - left);
            const float3 outward = normalize(make_float3(left.x + right.x, 0, left.z + right.z));
            const float halfSpan = length(right - left) * 0.5f;
            column(mesh, left, 2.9f, 0.47f, detail);
            if (bay == bays - 1) column(mesh, right, 2.9f, 0.47f, detail);
            arch(mesh, left, right, 2.7f, 3.5f, floorHeight - 0.35f);
            const float3 top = (left + right) * 0.5f + make_float3(0, floorHeight - 0.65f, 0);
            box(mesh, top, direction * (halfSpan + 0.1f), make_float3(0, 0.34f, 0), outward * 1.95f, kMaterialStone);
            box(mesh, top + make_float3(0, 0.32f, 0), direction * (halfSpan + 0.15f), make_float3(0, 0.09f, 0),
                outward * 2.10f, kMaterialStone);
            if (story == 0) {
                const float3 seat = (left + right) * 0.5f - outward * 0.95f + make_float3(0, 0.60f, 0);
                box(mesh, seat, direction * (halfSpan * 0.67f), make_float3(0, 0.12f, 0), outward * 0.45f,
                    kMaterialStone);
                for (int leg : {-1, 1})
                    box(mesh, seat + direction * (float(leg) * halfSpan * 0.46f) - make_float3(0, 0.25f, 0),
                        direction * 0.18f, make_float3(0, 0.23f, 0), outward * 0.35f, kMaterialStone);
            }
        }
        arcSlab(mesh, make_float3(0, baseY + floorHeight - 0.45f, 0), ringRadius, 4.05f, 0.45f,
                storyStart, storySweep, 96 * detail, kMaterialStone);
        const int balusters = bays * 6;
        for (int railing = 0; railing <= balusters; ++railing) {
            const float angle = storyStart + storySweep * float(railing) / float(balusters);
            const float3 base = make_float3(std::cos(angle) * (ringRadius + 1.65f), baseY + floorHeight,
                                            std::sin(angle) * (ringRadius + 1.65f));
            tube(mesh, base, base + make_float3(0, 0.8f, 0), 0.045f, 6, kMaterialBronze);
        }
        arcSlab(mesh, make_float3(0, baseY + floorHeight + 0.78f, 0), ringRadius + 1.65f, 0.17f, 0.12f,
                storyStart, storySweep, 96, kMaterialBronze);
    }
    const int steps = 42;
    const float stairStart = start - 0.88f;
    for (int step = 0; step < steps; ++step) {
        const float angle = stairStart + (float(step) + 0.5f) * 0.88f / float(steps);
        const float y = float(step + 1) * floorHeight / float(steps);
        const float3 radial = make_float3(std::cos(angle), 0, std::sin(angle));
        const float3 tangent = make_float3(-radial.z, 0, radial.x);
        box(mesh, radial * radius + make_float3(0, y * 0.5f, 0), radial * 1.2f,
            make_float3(0, y * 0.5f + 0.04f, 0), tangent * (radius * 0.88f / float(steps) * 0.54f), kMaterialStone);
    }
}

void masonryWall(Mesh& mesh, float3 center, float3 along, float3 normal, float width, float height,
                 unsigned seed, bool doorway) {
    constexpr float kCourseHeight = 0.58f;
    const int courses = int(std::ceil(height / kCourseHeight));
    const int blocks = std::max(2, int(std::ceil(width / 1.25f)));
    const float blockWidth = width / float(blocks);
    for (int course = 0; course < courses; ++course)
        for (int block = 0; block < blocks; ++block) {
            const float local = (float(block) + 0.5f) * blockWidth - width * 0.5f;
            const float y = (float(course) + 0.5f) * kCourseHeight;
            if (doorway && std::abs(local) < 1.25f && y < 3.65f) continue;
            if (!doorway && std::abs(local) < 0.70f && y > 2.0f && y < 5.2f) continue;
            const float inset = 0.025f * randomUnit(seed + unsigned(course * blocks + block));
            box(mesh, center + along * local + make_float3(0, y, 0) + normal * inset,
                along * (blockWidth * 0.5f - 0.018f), make_float3(0, kCourseHeight * 0.5f - 0.014f, 0),
                normal * 0.39f, kMaterialStone);
        }
}

void chapel(Mesh& mesh, unsigned prototype, int detail) {
    const float3 center = make_float3(prototype % 2U ? -7.3f : 7.3f, 0.4f, 6.2f);
    const float halfWidth = 2.6f;
    const float halfDepth = 3.8f;
    const float height = 6.9f;
    box(mesh, center - make_float3(0, 0.18f, 0), make_float3(3.0f, 0, 0), make_float3(0, 0.25f, 0),
        make_float3(0, 0, 4.2f), kMaterialStone);
    for (int side : {-1, 1}) {
        masonryWall(mesh, center + make_float3(0, 0, halfDepth * float(side)), make_float3(1, 0, 0),
                    make_float3(0, 0, float(side)), halfWidth * 2.0f, height, prototype * 101U, side > 0);
        masonryWall(mesh, center + make_float3(halfWidth * float(side), 0, 0), make_float3(0, 0, 1),
                    make_float3(float(side), 0, 0), halfDepth * 2.0f, height, prototype * 113U, false);
    }
    const float3 entranceLeft = center + make_float3(-1.6f, 0, halfDepth + 0.08f);
    const float3 entranceRight = center + make_float3(1.6f, 0, halfDepth + 0.08f);
    column(mesh, entranceLeft, 2.5f, 0.28f, detail);
    column(mesh, entranceRight, 2.5f, 0.28f, detail);
    arch(mesh, entranceLeft, entranceRight, 2.3f, 1.25f, 4.0f);
    for (int side : {-1, 1}) {
        const float z = float(side) * halfDepth;
        const float3 a = center + make_float3(-halfWidth, height, z);
        const float3 b = center + make_float3(halfWidth, height, z);
        const float3 c = center + make_float3(0, height + 1.95f, z);
        if (side > 0) quad(mesh, a, b, c, c, kMaterialStone);
        else quad(mesh, b, a, c, c, kMaterialStone);
    }
    for (int side : {-1, 1}) {
        const float slope = -float(side) * 0.55f;
        const float3 across = normalize(make_float3(1, slope, 0));
        const float3 up = normalize(make_float3(-slope, 1, 0));
        for (int row = 0; row < 5; ++row)
            for (int tile = 0; tile < 15; ++tile) {
                const float x = float(side) * (0.3f + float(row) * 0.59f);
                const float y = height + 1.95f - std::abs(x) * 0.55f;
                const float z = -4.0f + (float(tile) + 0.5f) * 8.0f / 15.0f;
                box(mesh, center + make_float3(x, y, z), across * 0.36f, up * 0.095f,
                    make_float3(0, 0, 0.285f), kMaterialDarkStone);
            }
    }
    tube(mesh, center + make_float3(0, height + 2.02f, -4.15f), center + make_float3(0, height + 2.02f, 4.15f),
         0.11f, 12, kMaterialBronze);
}

void tower(Mesh& mesh, unsigned prototype, int detail) {
    if (prototype == 0U || prototype == 3U || prototype == 5U) return;
    const float3 center = make_float3(prototype % 2U ? 10.0f : -10.0f, 0, -7.3f);
    const float height = 18.0f + float(prototype % 4U) * 3.8f;
    for (int side : {-1, 1}) {
        masonryWall(mesh, center + make_float3(0, 0, 2.2f * float(side)), make_float3(1, 0, 0),
                    make_float3(0, 0, float(side)), 4.4f, height - 4.2f, prototype * 41U, false);
        masonryWall(mesh, center + make_float3(2.2f * float(side), 0, 0), make_float3(0, 0, 1),
                    make_float3(float(side), 0, 0), 4.4f, height - 4.2f, prototype * 43U, false);
    }
    for (float y = 0.5f; y < height - 4.0f; y += 5.0f)
        box(mesh, center + make_float3(0, y, 0), make_float3(2.5f, 0, 0), make_float3(0, 0.17f, 0),
            make_float3(0, 0, 2.5f), kMaterialStone);
    for (int x : {-1, 1})
        for (int z : {-1, 1})
            column(mesh, center + make_float3(float(x) * 1.7f, height - 4.2f, float(z) * 1.7f), 3.7f, 0.25f, detail);
    box(mesh, center + make_float3(0, height - 0.25f, 0), make_float3(2.65f, 0, 0), make_float3(0, 0.3f, 0),
        make_float3(0, 0, 2.65f), kMaterialStone);
    for (int tier = 0; tier != 7; ++tier) {
        const float radius = 2.8f * (1.0f - float(tier) / 7.5f);
        box(mesh, center + make_float3(0, height + float(tier) * 0.28f, 0), make_float3(radius, 0, 0),
            make_float3(0, 0.17f, 0), make_float3(0, 0, radius), kMaterialDarkStone);
    }
    tube(mesh, center + make_float3(0, height + 1.9f, 0), center + make_float3(0, height + 3.4f, 0),
         0.06f, 8, kMaterialBronze);
}

void leaf(Mesh& mesh, float3 base, float3 direction, float width, float span) {
    direction = safeNormal(direction, make_float3(0, 1, 0));
    float3 tangent;
    float3 bitangent;
    makeBasis(direction, tangent, bitangent);
    surface(mesh, 4, 4, kMaterialLeaf,
            [&](float u, float v) {
                const float theta = kTau * u;
                const float profile = std::sin(M_PIf * clampf(v, 0.0f, 1.0f));
                const float3 center = base + direction * (span * v) + bitangent * (0.23f * span * v * v);
                return center + tangent * (width * profile * std::cos(theta)) +
                       bitangent * (0.055f * width * profile * std::sin(theta));
            },
            [&](float u, float) { return tangent * std::cos(kTau * u) + bitangent * std::sin(kTau * u); });
}

void foliage(Mesh& mesh, float3 center, float3 radius, unsigned seed, int leaves, float leafScale) {
    for (int index = 0; index < leaves; ++index) {
        const unsigned key = seed + unsigned(index) * 823U;
        const float angle = kTau * randomUnit(key);
        const float vertical = 2.0f * randomUnit(key + 1U) - 1.0f;
        const float horizontal = std::sqrt(std::max(0.0f, 1.0f - vertical * vertical));
        const float radial = 0.25f + 0.75f * std::cbrt(randomUnit(key + 2U));
        const float3 normal = make_float3(horizontal * std::cos(angle), vertical, horizontal * std::sin(angle));
        const float3 point = center + normal * radius * radial;
        const float tilt = randomUnit(key + 3U);
        const float3 direction = normal + make_float3(0, 0.3f + 0.5f * tilt, 0);
        leaf(mesh, point, direction, leafScale * (0.13f + 0.075f * tilt), leafScale * (0.40f + 0.28f * tilt));
    }
}

void olive(Mesh& mesh, float3 base, float height, unsigned seed, int detail) {
    const float3 bend = make_float3(0.35f * std::sin(float(seed % 17U)), height * 0.44f, 0.32f);
    tube(mesh, base, base + bend, 0.21f, 10 * detail, kMaterialDarkStone);
    const int branches = 5;
    for (int branch = 0; branch < branches; ++branch) {
        const unsigned key = seed + unsigned(branch) * 277U;
        const float angle = kTau * (float(branch) / float(branches) + randomUnit(key) * 0.16f);
        const float reach = 1.3f + 1.1f * randomUnit(key + 1U);
        const float3 elbow = base + bend + make_float3(std::cos(angle) * reach * 0.6f, height * 0.19f,
                                                       std::sin(angle) * reach * 0.6f);
        const float3 end = base + make_float3(std::cos(angle) * reach, height * (0.75f + 0.20f * randomUnit(key + 2U)),
                                              std::sin(angle) * reach);
        tube(mesh, base + bend * 0.80f, elbow, 0.11f, 8, kMaterialDarkStone);
        tube(mesh, elbow, end, 0.065f, 7, kMaterialDarkStone);
        foliage(mesh, end, make_float3(1.28f, 0.90f, 1.15f), key, 100 + 35 * detail, 0.85f);
    }
}

void cypress(Mesh& mesh, float3 base, float height, unsigned seed, int detail) {
    tube(mesh, base, base + make_float3(0.10f, height * 0.95f, 0), 0.13f, 10, kMaterialDarkStone);
    for (int tier = 0; tier < 10; ++tier) {
        const float v = float(tier) / 10.0f;
        const float radius = (0.9f + 0.45f * std::sin(v * M_PIf)) * (1.0f - v * 0.72f);
        const float3 center = base + make_float3(0.10f * v, height * (0.22f + 0.73f * v), 0);
        foliage(mesh, center, make_float3(radius, height * 0.12f, radius), seed + unsigned(tier) * 1103U,
                35 + detail * 10, 0.80f);
    }
}

void gardenBed(Mesh& mesh, float3 center, float angle, unsigned seed, int detail) {
    const float3 along = make_float3(std::cos(angle), 0, std::sin(angle));
    const float3 across = make_float3(-along.z, 0, along.x);
    box(mesh, center + make_float3(0, 0.30f, 0), along * 1.85f, make_float3(0, 0.30f, 0), across * 1.05f,
        kMaterialStone);
    box(mesh, center + make_float3(0, 0.615f, 0), along * 1.63f, make_float3(0, 0.045f, 0), across * 0.83f,
        kMaterialDarkStone);
    for (int plant = 0; plant < 10; ++plant) {
        const unsigned key = seed + unsigned(plant) * 613U;
        const float x = (randomUnit(key) - 0.5f) * 2.9f;
        const float z = (randomUnit(key + 1U) - 0.5f) * 1.3f;
        const float3 base = center + along * x + across * z + make_float3(0, 0.65f, 0);
        const float height = 0.50f + randomUnit(key + 2U) * 0.65f;
        tube(mesh, base, base + make_float3(0, height, 0), 0.022f, 5, kMaterialLeaf);
        for (int frond = 0; frond < 8 + detail * 2; ++frond) {
            const float phase = kTau * float(frond) / float(8 + detail * 2);
            const float3 direction = make_float3(std::cos(phase), 0.3f + float(frond % 3) * 0.35f, std::sin(phase));
            leaf(mesh, base + make_float3(0, height * 0.48f, 0), direction, 0.21f, 0.78f);
        }
    }
}

void garden(Mesh& mesh, unsigned seed, int detail, unsigned prototype) {
    for (int tree = 0; tree < 7; ++tree) {
        const unsigned key = seed + unsigned(tree) * 1987U;
        const float angle = 0.10f + float(tree) * 0.39f + randomUnit(key) * 0.11f;
        const float radius = rimRadius(angle, seed) * 0.82f;
        const float3 base = make_float3(radius * std::cos(angle), 0.2f, radius * std::sin(angle));
        const float chapelSide = prototype % 2U ? -7.3f : 7.3f;
        if (std::abs(base.x - chapelSide) < 4.4f && std::abs(base.z - 6.2f) < 5.5f) continue;
        if ((tree + int(prototype)) % 3 == 0) cypress(mesh, base, 6.4f + randomUnit(key + 1U) * 2.0f, key, detail);
        else olive(mesh, base, 3.6f + randomUnit(key + 1U) * 1.7f, key, detail);
        stone(mesh, base - make_float3(0, 0.25f, 0), make_float3(1.4f, 0.38f, 1.2f), key, 1, kMaterialDarkStone);
    }
    for (int bed = 0; bed < 7; ++bed) {
        const float angle = -0.1f + float(bed) * 0.49f;
        const float3 center = make_float3(std::cos(angle) * 8.7f, 0, std::sin(angle) * 8.7f);
        const float chapelSide = prototype % 2U ? -7.3f : 7.3f;
        if (std::abs(center.x - chapelSide) < 4.2f && std::abs(center.z - 6.2f) < 5.5f) continue;
        gardenBed(mesh, center, angle + M_PIf * 0.5f, seed + unsigned(bed) * 3911U, detail);
    }
    for (int shrub = 0; shrub < 26; ++shrub) {
        const unsigned key = seed + unsigned(shrub) * 811U;
        const float angle = kTau * randomUnit(key);
        const float radius = rimRadius(angle, seed) * (0.86f + 0.045f * randomUnit(key + 1U));
        const float3 center = make_float3(radius * std::cos(angle), 0.5f, radius * std::sin(angle));
        foliage(mesh, center, make_float3(1.3f, 0.7f, 1.1f), key, 80 + detail * 12, 1.0f);
    }
    // Adjacent vines form curtains and reveal intermediate ledges; isolated
    // single-pixel strings cannot carry the scale of the hanging gardens.
    for (int vine = 0; vine < 68; ++vine) {
        const unsigned key = seed + unsigned(vine) * 1063U;
        const float group = float(vine / 9) / 8.0f;
        const float angle = group * kTau + float(vine % 9) * 0.019f + randomUnit(key) * 0.012f;
        const float radius = rimRadius(angle, seed) - 0.15f;
        const float vineLength = 5.0f + 12.0f * randomUnit(key + 1U);
        float3 previous = make_float3(radius * std::cos(angle), 0.15f, radius * std::sin(angle));
        const int segments = 18;
        for (int segment = 1; segment <= segments; ++segment) {
            const float v = float(segment) / float(segments);
            const float wander = angle + 0.022f * std::sin(v * 9.0f + randomUnit(key + 2U) * 4.0f);
            const float r = radius * (1.0f - 0.095f * v);
            const float3 point = make_float3(r * std::cos(wander), -v * vineLength,
                                              r * std::sin(wander));
            tube(mesh, previous, point, 0.018f, 5, kMaterialDarkStone);
            const float side = segment % 2 ? 1.0f : -1.0f;
            const float3 direction = make_float3(-std::sin(angle) * side, -0.45f, std::cos(angle) * side);
            leaf(mesh, point, direction, 0.24f, 0.68f);
            leaf(mesh, point + make_float3(0, -0.12f, 0), -direction + make_float3(0, -1.1f, 0), 0.20f, 0.62f);
            previous = point;
        }
    }
}

void courtyard(Mesh& mesh, unsigned seed, int detail) {
    const float radius = 4.4f + randomUnit(seed + 91U) * 1.0f;
    ring(mesh, radius + 0.35f, 0.7f, 0.05f, 0.35f, 96 * detail, kMaterialStone);
    surface(mesh, 96 * detail, 12 * detail, kMaterialWater,
            [&](float u, float v) { return make_float3(radius * v * std::cos(u * kTau), 0.16f,
                                                       radius * v * std::sin(u * kTau)); },
            [](float, float) { return make_float3(0, 1, 0); });
    for (int step = 0; step < 9; ++step) {
        const float angle = float(step) * kTau / 9.0f;
        const float3 radial = make_float3(std::cos(angle), 0, std::sin(angle));
        const float3 tangent = make_float3(-radial.z, 0, radial.x);
        box(mesh, radial * (radius + 1.2f) + make_float3(0, 0.12f, 0), radial * 0.65f,
            make_float3(0, 0.12f, 0), tangent * 0.34f, kMaterialDarkStone);
    }
    for (int course = 0; course != 3; ++course) {
        const float r = radius + 0.85f + float(course) * 0.74f;
        const int tiles = int(kTau * r / 0.90f);
        for (int tile = 0; tile < tiles; ++tile) {
            const float angle = (float(tile) + 0.5f * float(course % 2)) / float(tiles) * kTau;
            const float3 radial = make_float3(std::cos(angle), 0, std::sin(angle));
            const float3 tangent = make_float3(-radial.z, 0, radial.x);
            box(mesh, radial * r + make_float3(0, 0.055f, 0), radial * 0.35f,
                make_float3(0, 0.055f, 0), tangent * (r * M_PIf / float(tiles) * 0.96f), kMaterialStone);
        }
    }
    stone(mesh, make_float3(0, 0.40f, 0), make_float3(0.85f, 0.55f, 0.85f), seed + 611U, 2, kMaterialStone);
    surface(mesh, 64, 28, kMaterialBronze,
            [](float u, float v) {
                const float radius = 0.22f + 0.40f * std::sin(v * M_PIf) + 0.11f * std::pow(v, 12.0f);
                return make_float3(std::cos(u * kTau) * radius, 0.75f + v * 1.35f, std::sin(u * kTau) * radius);
            },
            [](float u, float) { return make_float3(std::cos(u * kTau), 0, std::sin(u * kTau)); });
    ring(mesh, 0.37f, 0.10f, 2.08f, 0.13f, 64, kMaterialBronze);
}

void fractureShard(Mesh& mesh, float3 center, float3 size, unsigned seed) {
    constexpr int kSides = 7;
    float3 lower[kSides];
    float3 upper[kSides];
    for (int side = 0; side < kSides; ++side) {
        const unsigned key = seed + unsigned(side) * 157U;
        const float angle = kTau * (float(side) + 0.24f * randomUnit(key)) / float(kSides);
        const float radial = 0.72f + 0.28f * randomUnit(key + 1U);
        const float3 point = make_float3(std::cos(angle) * size.x * radial, 0, std::sin(angle) * size.z * radial);
        lower[side] = center + point + make_float3(0, -size.y * (0.5f + 0.25f * randomUnit(key + 2U)), 0);
        upper[side] = center + point * 0.76f + make_float3(size.x * 0.18f,
                          size.y * (0.5f + 0.30f * randomUnit(key + 3U)), -size.z * 0.12f);
    }
    for (int side = 0; side < kSides; ++side) {
        const int next = (side + 1) % kSides;
        quad(mesh, lower[side], lower[next], upper[next], upper[side], kMaterialStone);
        quad(mesh, center + make_float3(0, size.y * 0.58f, 0), upper[side], upper[next], upper[next], kMaterialStone);
        quad(mesh, center - make_float3(0, size.y * 0.58f, 0), lower[next], lower[side], lower[side], kMaterialStone);
    }
}

void fracturedGeology(Mesh& mesh, unsigned seed, int detail, IslandKind kind) {
    const float depth = kind == IslandKind::Garden ? 43.0f + 18.0f * randomUnit(seed + 1U)
                                                  : 69.0f + 12.0f * randomUnit(seed + 1U);
    for (int shard = 0; shard < 95; ++shard) {
        const unsigned key = seed + unsigned(shard) * 1307U;
        const float angle = kTau * randomUnit(key);
        const float t = 0.03f + 0.69f * randomUnit(key + 1U);
        const float exponent = kind == IslandKind::Garden ? 0.40f : 0.56f;
        const float radius = rimRadius(angle, seed) * std::pow(1.0f - t, exponent) * 0.94f;
        const float bend = std::sin(t * M_PIf);
        const float3 center = make_float3(radius * std::cos(angle) + bend * 4.5f, -depth * t,
                                          radius * std::sin(angle) + bend * 4.0f * std::sin(t * 5.0f + float(seed % 5U)));
        const float size = 0.8f + 2.5f * randomUnit(key + 2U);
        fractureShard(mesh, center, make_float3(size, size * (1.5f + randomUnit(key + 3U)), size * 0.85f), key);
    }
    const int ledges = kind == IslandKind::Garden ? 4 : 2;
    for (int ledge = 0; ledge < ledges; ++ledge) {
        const unsigned key = seed + unsigned(ledge) * 2161U;
        const float y = -3.8f - float(ledge) * 7.7f;
        const float radius = 13.5f - float(ledge) * 1.2f;
        arcSlab(mesh, make_float3(0, y, 0), radius, 21.0f - float(ledge) * 2.0f,
                0.72f + randomUnit(key) * 0.6f, kTau * randomUnit(key + 1U), 0.65f + randomUnit(key + 2U) * 0.8f,
                40 * detail, kMaterialStone, 0.42f);
    }
}

void brokenGallery(Mesh& mesh, unsigned seed, int detail, unsigned variant) {
    const float radius = 12.5f + float(variant % 3U) * 0.6f;
    const float start = 3.25f + randomUnit(seed) * 0.3f;
    const int bays = 2 + int(variant % 2U);
    constexpr float kStep = 0.53f;
    arcSlab(mesh, make_float3(0, 0.0f, 0), radius, 3.8f, 0.35f, start - 0.1f, float(bays) * kStep + 0.2f,
            64 * detail, kMaterialStone, 0.08f);
    for (int bay = 0; bay < bays; ++bay) {
        const float a = start + float(bay) * kStep;
        const float b = a + kStep;
        const float3 left = make_float3(radius * std::cos(a), 0.35f, radius * std::sin(a));
        const float3 right = make_float3(radius * std::cos(b), 0.35f, radius * std::sin(b));
        const bool retainsArch = bay != bays - 1 || variant % 3U == 0U;
        column(mesh, left, 2.85f, 0.46f, detail);
        if (bay == bays - 1) column(mesh, right, retainsArch ? 2.85f : 1.35f + randomUnit(seed + 1U), 0.46f, detail);
        if (retainsArch) {
            arch(mesh, left, right, 2.65f, 2.5f, 6.0f);
            if (bay == 0) {
                const float3 along = normalize(right - left);
                const float3 normal = normalize(cross(along, make_float3(0, 1, 0)));
                box(mesh, (left + right) * 0.5f + make_float3(0, 5.8f, 0), along * (length(right - left) * 0.55f),
                    make_float3(0, 0.30f, 0), normal * 1.9f, kMaterialStone);
            }
        }
    }
    for (int block = 0; block != 14; ++block) {
        const unsigned key = seed + unsigned(block) * 701U;
        const float angle = start + randomUnit(key) * 2.0f;
        const float r = 9.0f + randomUnit(key + 1U) * 5.0f;
        const float3 center = make_float3(std::cos(angle) * r, 0.25f, std::sin(angle) * r);
        fractureShard(mesh, center, make_float3(0.35f + randomUnit(key + 2U), 0.45f, 0.50f), key);
    }
}

void openGarden(Mesh& mesh, unsigned seed, int detail, unsigned prototype) {
    brokenGallery(mesh, seed + 91U, detail, prototype);
    if (prototype == 8U || prototype == 11U) chapel(mesh, prototype, detail);
    if (prototype == 8U || prototype == 10U) courtyard(mesh, seed, detail);
    else {
        ring(mesh, 1.8f, 0.6f, 0.0f, 0.85f, 64, kMaterialStone);
        ring(mesh, 1.8f, 0.8f, 0.83f, 0.20f, 64, kMaterialStone);
    }
    garden(mesh, seed, detail, prototype);
    for (int tree = 0; tree != 4; ++tree) {
        const unsigned key = seed + unsigned(tree) * 1721U;
        const float angle = 2.5f + float(tree) * 0.65f;
        const float r = 8.4f + randomUnit(key) * 1.5f;
        const float3 base = make_float3(std::cos(angle) * r, 0.20f, std::sin(angle) * r);
        if (prototype % 2U == 0U) olive(mesh, base, 5.0f + randomUnit(key + 1U) * 1.7f, key, detail);
        else cypress(mesh, base, 8.0f + randomUnit(key + 1U) * 2.8f, key, detail);
    }
}

void monument(Mesh& mesh, unsigned seed, int detail, unsigned prototype) {
    brokenGallery(mesh, seed, detail, prototype);
    if (prototype == 12U || prototype == 14U) tower(mesh, prototype == 12U ? 6U : 7U, detail);
    else if (prototype == 13U) {
        for (int tier = 0; tier < 6; ++tier) {
            const float radius = 3.7f - float(tier) * 0.43f;
            box(mesh, make_float3(1.5f, 0.18f + float(tier) * 0.30f, -2.0f), make_float3(radius, 0, 0),
                make_float3(0, 0.17f, 0), make_float3(0, 0, radius), kMaterialStone);
        }
        column(mesh, make_float3(1.5f, 1.85f, -2.0f), 17.0f, 0.92f, detail);
        fractureShard(mesh, make_float3(1.5f, 20.5f, -2.0f), make_float3(1.4f, 2.7f, 1.3f), seed);
    }
    for (int tree = 0; tree < 2; ++tree) {
        const float angle = 0.7f + float(tree) * 1.8f;
        const float3 base = make_float3(std::cos(angle) * 9.0f, 0.2f, std::sin(angle) * 9.0f);
        if (prototype % 2U) olive(mesh, base, 4.0f, seed + unsigned(tree), detail);
        else cypress(mesh, base, 7.8f, seed + unsigned(tree), detail);
    }
    for (int shrub = 0; shrub < 8; ++shrub) {
        const unsigned key = seed + unsigned(shrub) * 647U;
        const float angle = kTau * randomUnit(key);
        const float radius = 16.0f + randomUnit(key + 1U) * 3.0f;
        foliage(mesh, make_float3(radius * std::cos(angle), 0.40f, radius * std::sin(angle)),
                make_float3(1.2f, 0.65f, 1.0f), key, 75, 0.90f);
    }
}

void reshape(Mesh& mesh, float3 scale) {
    for (Vertex& vertex : mesh.vertices) {
        vertex.position *= scale;
        vertex.normal = normalize(make_float3(vertex.normal.x / scale.x, vertex.normal.y / scale.y,
                                               vertex.normal.z / scale.z));
    }
}
}

std::vector<Mesh> buildPrototypes(int detail) {
    detail = std::clamp(detail, 1, 4);
    std::vector<Mesh> result(kPrototypeCount);
    for (unsigned prototype = 0; prototype < unsigned(kPrototypeCount); ++prototype) {
        Mesh& mesh = result[prototype];
        mesh.vertices.reserve(size_t(180000) * size_t(detail));
        mesh.indices.reserve(size_t(120000) * size_t(detail));
        const unsigned seed = hash(prototype + 0x52555941U);
        if (prototype < 8U) {
            island(mesh, seed, detail);
            geology(mesh, seed, detail);
            arcade(mesh, prototype, detail);
            chapel(mesh, prototype, detail);
            tower(mesh, prototype, detail);
            courtyard(mesh, seed, detail);
            garden(mesh, seed, detail, prototype);
        } else if (prototype < 12U) {
            island(mesh, seed, detail, IslandKind::Garden);
            fracturedGeology(mesh, seed, detail, IslandKind::Garden);
            openGarden(mesh, seed, detail, prototype);
        } else {
            island(mesh, seed, detail, IslandKind::Monument);
            fracturedGeology(mesh, seed, detail, IslandKind::Monument);
            monument(mesh, seed, detail, prototype);
        }
        for (int rock = 0; rock < 80 + 20 * detail; ++rock) {
            const unsigned key = seed + unsigned(rock) * 811U;
            const float angle = kTau * randomUnit(key);
            const float radius = 18.0f + 2.6f * randomUnit(key + 1U);
            const float size = 0.14f + 0.55f * randomUnit(key + 2U);
            const float3 center = make_float3(std::cos(angle) * radius, size * 0.32f,
                                              std::sin(angle) * radius);
            stone(mesh, center, make_float3(size, size * 0.55f, size * 0.8f), key, 1, kMaterialStone);
        }
        if (prototype >= 8U && prototype < 12U)
            reshape(mesh, make_float3(1.04f + float(prototype % 2U) * 0.09f, 1.0f, 0.88f + float(prototype % 3U) * 0.07f));
        if (prototype >= 12U)
            reshape(mesh, make_float3(0.64f + float(prototype % 3U) * 0.06f, 1.0f,
                                      0.72f + float(prototype % 2U) * 0.08f));
    }
    return result;
}

void cameraBasis(const Camera& camera, float aspect, float3& U, float3& V, float3& W) {
    const float pitch = clampf(camera.pitch, -1.45f, 1.45f);
    W = make_float3(std::sin(camera.yaw) * std::cos(pitch), std::sin(pitch),
                   -std::cos(camera.yaw) * std::cos(pitch));
    const float3 right = normalize(cross(W, make_float3(0, 1, 0)));
    const float3 up = normalize(cross(right, W));
    const float scale = std::tan(clampf(camera.fovY, 36.0f, kFovY) * M_PIf / 360.0f);
    U = right * (std::max(0.001f, aspect) * scale);
    V = up * scale;
}

void moveCamera(Camera& camera, float dx, float dy, float forward, float right, float up, float dt) {
    camera.fovY = clampf(camera.fovY, 36.0f, kFovY);
    if (!(dt > 0.0f) || !std::isfinite(dt)) return;
    float turnX = dx * 0.0022f;
    float turnY = -dy * 0.0022f;
    const float angle = std::hypot(turnX, turnY);
    const float limit = kTurnRate * dt;
    if (angle > limit) {
        turnX *= limit / angle;
        turnY *= limit / angle;
    }
    camera.yaw = std::remainder(camera.yaw + turnX, kTau);
    camera.pitch = clampf(camera.pitch + turnY, -1.45f, 1.45f);
    float3 U;
    float3 V;
    float3 W;
    cameraBasis(camera, 1.0f, U, V, W);
    const float3 side = normalize(cross(W, make_float3(0, 1, 0)));
    float3 velocity = W * forward + side * right + make_float3(0, up, 0);
    const float speed = length(velocity);
    const float speedLimit = std::min(36.0f, std::max({std::abs(forward), std::abs(right), std::abs(up)}));
    if (speed > speedLimit && speed > 0.0f) velocity *= speedLimit / speed;
    camera.eye += velocity * dt;
}

std::vector<Cell> visibleCells(const Camera& camera, float aspect, float farDistance, bool fullCircle) {
    std::vector<Cell> result;
    if (!(farDistance > 0.0f) || !std::isfinite(farDistance)) return result;
    float3 U;
    float3 V;
    float3 W;
    cameraBasis(camera, aspect, U, V, W);
    const float halfWidth = length(U);
    const float halfHeight = length(V);
    const float3 side = U / halfWidth;
    const float3 up = V / halfHeight;
    const float3 leftPlane = normalize(W * halfWidth + side);
    const float3 rightPlane = normalize(W * halfWidth - side);
    const float3 bottomPlane = normalize(W * halfHeight + up);
    const float3 topPlane = normalize(W * halfHeight - up);
    const float margin = kCellBoundsRadius * 1.18f + 9.0f;
    const int minX = int(std::floor((camera.eye.x - farDistance - margin) / kCellSize));
    const int maxX = int(std::ceil((camera.eye.x + farDistance + margin) / kCellSize));
    const int minZ = int(std::floor((camera.eye.z - farDistance - margin) / kCellSize));
    const int maxZ = int(std::ceil((camera.eye.z + farDistance + margin) / kCellSize));
    for (int z = minZ; z <= maxZ; ++z)
        for (int x = minX; x <= maxX; ++x) {
            const Cell cell = describeCell(x, z);
            const float3 center = cell.offset + make_float3(0, kCellBoundsY * cell.scale, 0);
            const float radius = kCellBoundsRadius * cell.scale;
            const float3 relative = center - camera.eye;
            if (length(relative) > farDistance + radius) continue;
            if (!fullCircle && (dot(relative, W) < -radius || dot(relative, leftPlane) < -radius ||
                                dot(relative, rightPlane) < -radius || dot(relative, bottomPlane) < -radius ||
                                dot(relative, topPlane) < -radius)) continue;
            result.push_back(cell);
        }
    return result;
}

unsigned long long fingerprint(const Mesh& mesh) {
    unsigned long long value = 14695981039346656037ULL;
    const auto append = [&](const void* data, size_t bytes) {
        const auto* source = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < bytes; ++i) {
            value ^= source[i];
            value *= 1099511628211ULL;
        }
    };
    const size_t vertexCount = mesh.vertices.size();
    const size_t triangleCount = mesh.indices.size();
    append(&vertexCount, sizeof(vertexCount));
    append(&triangleCount, sizeof(triangleCount));
    for (const Vertex& vertex : mesh.vertices) {
        append(&vertex.position.x, sizeof(float));
        append(&vertex.position.y, sizeof(float));
        append(&vertex.position.z, sizeof(float));
        append(&vertex.normal.x, sizeof(float));
        append(&vertex.normal.y, sizeof(float));
        append(&vertex.normal.z, sizeof(float));
        append(&vertex.material, sizeof(vertex.material));
    }
    for (const uint3& index : mesh.indices) {
        append(&index.x, sizeof(index.x));
        append(&index.y, sizeof(index.y));
        append(&index.z, sizeof(index.z));
    }
    return value;
}
}
