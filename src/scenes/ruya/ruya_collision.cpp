#include "scenes/ruya/ruya_collision.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace ruya {
namespace {
constexpr float kSkin = 0.0015f;

float axis(float3 p, int index) { return index == 0 ? p.x : index == 1 ? p.y : p.z; }

struct Contact {
    float time = 1.0f;
    float3 normal{};
};

// Earliest entry into a sphere or an infinite cylinder. The cylinder's
// axial interval is checked separately. Moving outward from contact is free.
float entry(float3 offset, float3 motion, float limit) {
    const float a = dot(motion, motion);
    const float b = dot(offset, motion);
    if (a < 1e-12f || b >= 0.0f) return limit;
    const float c = dot(offset, offset) - sqr(CollisionWorld::radius);
    const float discriminant = b * b - a * c;
    if (discriminant <= 0.0f) return limit;
    return c <= 0.0f ? 0.0f : c / (-b + std::sqrt(discriminant));
}

void sweepTriangle(float3 start, float3 motion, float3 a, float3 b, float3 c, Contact& hit) {
    const float3 crossNormal = cross(b - a, c - a);
    const float normalLength = length(crossNormal);
    if (normalLength <= 1e-10f) return;
    const float3 face = crossNormal * (1.0f / normalLength);
    const float signedDistance = dot(start - a, face);
    const float3 outward = signedDistance < 0.0f ? -face : face;
    const float approach = -dot(motion, outward);
    if (approach > 1e-7f) {
        const float t = std::max(0.0f, (std::abs(signedDistance) - CollisionWorld::radius) / approach);
        if (t < hit.time) {
            const float3 center = start + motion * t;
            if (dot(cross(b - a, center - a), face) >= 0.0f &&
                dot(cross(c - b, center - b), face) >= 0.0f &&
                dot(cross(a - c, center - c), face) >= 0.0f) hit = {t, outward};
        }
    }
    const float3 vertices[]{a, b, c};
    for (int i = 0; i < 3; ++i) {
        const float3 point = vertices[i];
        const float3 edge = vertices[(i + 1) % 3] - point;
        const float3 relative = start - point;
        const float tVertex = entry(relative, motion, hit.time);
        if (tVertex < hit.time) hit = {tVertex, normalize(relative + motion * tVertex)};
        const float inverseLength = 1.0f / dot(edge, edge);
        const float u = dot(relative, edge) * inverseLength;
        const float v = dot(motion, edge) * inverseLength;
        const float3 perpendicular = relative - edge * u;
        const float3 transverse = motion - edge * v;
        const float tEdge = entry(perpendicular, transverse, hit.time);
        const float axial = u + v * tEdge;
        if (tEdge < hit.time && axial > 0.0f && axial < 1.0f)
            hit = {tEdge, normalize(perpendicular + transverse * tEdge)};
    }
}
}

CollisionWorld::Bounds::Bounds() {
    const float infinity = std::numeric_limits<float>::infinity();
    low = make_float3(infinity, infinity, infinity);
    high = -low;
}

void CollisionWorld::Bounds::include(float3 point) {
    low = make_float3(std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z));
    high = make_float3(std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z));
}

bool CollisionWorld::Bounds::overlaps(float3 start, float3 end, float3 margin) const {
    for (int i = 0; i < 3; ++i) {
        const float a = axis(start, i);
        const float b = axis(end, i);
        const float r = axis(margin, i);
        if (std::max(a, b) + r < axis(low, i) || std::min(a, b) - r > axis(high, i)) return false;
    }
    return true;
}

unsigned CollisionWorld::Collider::split(unsigned first, unsigned end, const std::vector<float3>& centers) {
    const unsigned index = unsigned(nodes.size());
    nodes.emplace_back();
    Bounds bounds, centroids;
    for (unsigned i = first; i < end; ++i) {
        const unsigned id = order[i];
        const uint3 triangle = mesh.indices[id];
        bounds.include(mesh.vertices[triangle.x].position);
        bounds.include(mesh.vertices[triangle.y].position);
        bounds.include(mesh.vertices[triangle.z].position);
        centroids.include(centers[id]);
    }
    nodes[index].bounds = bounds;
    if (end - first <= 12) {
        nodes[index].first = first;
        nodes[index].count = end - first;
        return index;
    }
    const float3 extent = centroids.high - centroids.low;
    const int direction = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2) : (extent.y > extent.z ? 1 : 2);
    const unsigned middle = first + (end - first) / 2;
    std::nth_element(order.begin() + first, order.begin() + middle, order.begin() + end,
                     [&](unsigned a, unsigned b) { return axis(centers[a], direction) < axis(centers[b], direction); });
    split(first, middle, centers);
    nodes[index].right = split(middle, end, centers);
    return index;
}

void CollisionWorld::build(std::vector<Mesh> meshes) {
    colliders_.clear();
    colliders_.reserve(meshes.size());
    for (Mesh& mesh : meshes) {
        Collider& collider = colliders_.emplace_back();
        const size_t count = mesh.indices.size();
        if (count == 0) continue;
        std::vector<float3> centers;
        centers.reserve(count);
        for (const uint3 triangle : mesh.indices)
            centers.push_back((mesh.vertices[triangle.x].position + mesh.vertices[triangle.y].position +
                               mesh.vertices[triangle.z].position) * (1.0f / 3.0f));
        collider.mesh = std::move(mesh);
        collider.order.resize(count);
        std::iota(collider.order.begin(), collider.order.end(), 0U);
        collider.split(0, unsigned(count), centers);
    }
}

float3 CollisionWorld::move(float3 start, float3 target) const {
    const float distance = length(target - start);
    if (distance <= 1e-6f) return start;
    Camera probe;
    probe.eye = start;
    std::vector<Piece> pieces;
    std::vector<Island> islands;
    for (const Cell& cell : visibleCells(probe, 1, distance + radius, true)) {
        const auto plan = cachedPlan(cell);
        pieces.insert(pieces.end(), plan->pieces.begin(), plan->pieces.end());
        islands.insert(islands.end(), plan->islands, plan->islands + plan->islandCount);
    }
    return moveOverIslands(start, move(start, target, pieces), islands);
}

// Island rock is a distance function, not a stored mesh. The camera marches in
// short steps and slides along the estimated surface normal on contact.
float3 CollisionWorld::moveOverIslands(float3 start, float3 target, const std::vector<Island>& islands) {
    constexpr float kStep = 0.2f;
    constexpr float kClearance = radius + 0.06f;
    const auto nearest = [&](float3 p, const Island*& which) {
        float best = 1e30f;
        for (const Island& island : islands) {
            const float d = islandDistance(island, p);
            if (d < best) { best = d; which = &island; }
        }
        return best;
    };
    float3 position = start;
    float3 motion = target - start;
    const Island* contact = nullptr;
    const float startDistance = nearest(position, contact);
    for (int slide = 0; slide < 4 && length(motion) > 1e-6f; ++slide) {
        const int steps = std::max(1, int(std::ceil(length(motion) / kStep)));
        const float3 step = motion / float(steps);
        bool blocked = false;
        for (int i = 0; i < steps; ++i) {
            const float3 next = position + step;
            const float d = nearest(next, contact);
            // Never step deeper; a camera spawned inside may still walk out.
            if (d < kClearance && d < std::min(startDistance, kClearance) + 1e-4f) {
                float3 n = islandGradient(*contact, next);
                n = n * (1.0f / std::max(length(n), 1e-6f));
                motion = step * float(steps - i);
                motion -= n * std::min(0.0f, dot(motion, n));
                blocked = true;
                break;
            }
            position = next;
        }
        if (!blocked) break;
    }
    return position;
}

float3 CollisionWorld::move(float3 start, float3 target, const std::vector<Piece>& pieces) const {
    float3 motion = target - start;
    for (int slide = 0; slide < 4; ++slide) {
        const float distance = length(motion);
        if (distance <= 1e-6f) break;
        Contact nearest;
        for (const Piece& piece : pieces) {
            const Collider& collider = colliders_.at(piece.prototype);
            if (collider.nodes.empty()) continue;
            const float3 localStart = localPoint(piece, start);
            const float3 localEnd = localPoint(piece, start + motion);
            const float3 localMotion = localEnd - localStart;
            const float3 margin = make_float3(radius / piece.scale.x, radius / piece.scale.y, radius / piece.scale.z);
            Contact hit;
            hit.time = nearest.time;
            const auto visit = [&](auto&& self, unsigned id) -> void {
                const Node& node = collider.nodes[id];
                if (!node.bounds.overlaps(localStart, localStart + localMotion * hit.time, margin)) return;
                if (node.count == 0) {
                    self(self, id + 1);
                    self(self, node.right);
                    return;
                }
                // Yaw is an isometry. Scale vertices here so the sweep stays a sphere.
                const float3 origin = localStart * piece.scale;
                const float3 step = localMotion * piece.scale;
                for (unsigned i = node.first; i < node.first + node.count; ++i) {
                    const uint3 t = collider.mesh.indices[collider.order[i]];
                    sweepTriangle(origin, step, collider.mesh.vertices[t.x].position * piece.scale,
                                  collider.mesh.vertices[t.y].position * piece.scale,
                                  collider.mesh.vertices[t.z].position * piece.scale, hit);
                }
            };
            visit(visit, 0);
            if (hit.time < nearest.time) {
                const float sine = std::sin(piece.yaw);
                const float cosine = std::cos(piece.yaw);
                nearest = {hit.time, make_float3(cosine * hit.normal.x + sine * hit.normal.z, hit.normal.y,
                                               -sine * hit.normal.x + cosine * hit.normal.z)};
            }
        }
        if (nearest.time >= 1.0f) return start + motion;
        start += motion * std::max(0.0f, nearest.time - kSkin / distance);
        motion *= 1.0f - nearest.time;
        motion -= nearest.normal * std::min(0.0f, dot(motion, nearest.normal));
    }
    return start;
}
}
