#pragma once
#include "scenes/ruya/ruya_world.h"

namespace ruya {
class CollisionWorld {
public:
    static constexpr float radius = 0.27f;

    void build(std::vector<Mesh> meshes);
    [[nodiscard]] float3 move(float3 start, float3 target) const;
    [[nodiscard]] float3 move(float3 start, float3 target, const std::vector<Piece>& pieces) const;

private:
    struct Bounds {
        float3 low, high;
        Bounds();
        void include(float3 point);
        [[nodiscard]] bool overlaps(float3 start, float3 end, float3 margin) const;
    };
    struct Node {
        Bounds bounds;
        unsigned first = 0, count = 0, right = 0;
    };
    struct Collider {
        Mesh mesh;
        std::vector<unsigned> order;
        std::vector<Node> nodes;
        unsigned split(unsigned first, unsigned end, const std::vector<float3>& centers);
    };
    std::vector<Collider> colliders_;
};
}
