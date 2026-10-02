#include "scenes/tanecik/body_graph.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <unordered_map>

namespace {

template <class F> void parallelFor(int n, F f) {
    const int T = std::max(1, int(std::thread::hardware_concurrency()));
    std::vector<std::thread> th;
    for (int t = 0; t < T; ++t)
        th.emplace_back([=] {
            for (int i = t; i < n; i += T) f(i);
        });
    for (auto& x : th) x.join();
}

float primRadius(const BonePrim& q) {
    if (q.kind == BonePrim::Ellipsoid) return std::min(q.b.x, std::min(q.b.y, q.b.z));
    if (q.kind == BonePrim::Capsule) return std::min(q.ra, q.rb);
    return q.ra;
}

float deg(float d) { return d * 3.14159265f / 180.0f; }

}   // namespace

size_t BodyGraph::countKind(PartKind k) const { return size_t(std::count(kind.begin(), kind.end(), uint8_t(k))); }
size_t BodyGraph::countBonds(BondKind k) const {
    return size_t(std::count_if(bonds.begin(), bonds.end(), [&](const BodyBond& b) { return b.kind == k; }));
}

bool bonesAdjacent(int a, int b) { return a == b || boneInfo(a).parent == b || boneInfo(b).parent == a; }

BodyGraph buildBodyGraph(const BodyParticles& body, float bondRadius, float minBoneRadius) {
    BodyGraph g;
    const int n = int(body.pos.size());
    g.kind.resize(n);
    g.bone = body.bone;
    for (int i = 0; i < n; ++i) g.kind[i] = body.skin[i] ? PK_SKIN : PK_TISSUE;

    // bone: inside a primitive of the skeleton (thin ones widened to minBoneRadius), the nearest one wins; the skin
    // layer never is
    const std::vector<BonePrim>& sk = skeleton();
    parallelFor(n, [&](int i) {
        if (g.kind[i] == PK_SKIN) return;
        const float3 p = body.pos[i];
        float best = 0.0f;
        int bone = -1;
        for (const BonePrim& q : sk) {
            const float d = bonePrimDistance(q, p) - std::max(0.0f, minBoneRadius - primRadius(q));
            if (d <= 0.0f && (bone < 0 || d < best)) { best = d; bone = q.bone; }
        }
        if (bone >= 0) {
            g.kind[i] = PK_BONE;
            g.bone[i] = uint8_t(bone);
        }
    });
    g.boneParticles.assign(B_COUNT, {});
    for (int i = 0; i < n; ++i)
        if (g.kind[i] == PK_BONE) g.boneParticles[g.bone[i]].push_back(uint32_t(i));

    // bonds: neighbours on a hash grid of the bond radius, each pair once (i < j), in order (the same graph every time)
    const float h = bondRadius, r2 = bondRadius * bondRadius;
    auto cellOf = [&](float3 p) { return make_int3(int(std::floor(p.x / h)), int(std::floor(p.y / h)), int(std::floor(p.z / h))); };
    auto key = [](int x, int y, int z) { return (uint64_t(uint32_t(x + 4096)) << 42) | (uint64_t(uint32_t(y + 4096)) << 21) | uint64_t(uint32_t(z + 4096)); };
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    grid.reserve(size_t(n) / 2);
    for (int i = 0; i < n; ++i) {
        const int3 c = cellOf(body.pos[i]);
        grid[key(c.x, c.y, c.z)].push_back(uint32_t(i));
    }
    std::vector<std::vector<BodyBond>> per(n);
    parallelFor(n, [&](int i) {
        const float3 p = body.pos[i];
        const int3 c = cellOf(p);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const auto it = grid.find(key(c.x + dx, c.y + dy, c.z + dz));
                    if (it == grid.end()) continue;
                    for (uint32_t j : it->second) {
                        if (int(j) <= i) continue;
                        const float3 d = body.pos[j] - p;
                        const float d2 = dot(d, d);
                        if (d2 > r2) continue;
                        const uint8_t ki = g.kind[i], kj = g.kind[j];
                        const int bi = g.bone[i], bj = g.bone[j];
                        uint8_t kind;
                        if (ki == PK_BONE && kj == PK_BONE) continue;   // a bone holds itself; bones meet at joints
                        if (ki == PK_BONE || kj == PK_BONE) {
                            if (bi != bj) continue;                     // flesh holds only to its own bone
                            kind = BK_ATTACH;
                        } else {
                            if (!bonesAdjacent(bi, bj)) continue;       // the two thighs touch, they do not grow together
                            kind = ki == PK_SKIN && kj == PK_SKIN ? BK_SKIN : BK_TISSUE;
                        }
                        per[i].push_back({uint32_t(i), j, std::sqrt(d2), kind});
                    }
                }
        std::sort(per[i].begin(), per[i].end(), [](const BodyBond& a, const BodyBond& b) { return a.b < b.b; });
    });
    size_t total = 0;
    for (const auto& v : per) total += v.size();
    g.bonds.reserve(total);
    for (const auto& v : per) g.bonds.insert(g.bonds.end(), v.begin(), v.end());

    // joints: pivots from the rest joints; limits a plain human's (approximate, generous where unsure)
    const RestJoints& J = restJoints();
    auto ball = [&](int parent, int child, float3 pivot, float3 towards, float swingDeg, float twistDeg) {
        JointDef j;
        j.parent = parent;
        j.child = child;
        j.pivot = pivot;
        j.axis = normalize(towards - pivot);
        j.swing = deg(swingDeg);
        j.lo = -deg(twistDeg);
        j.hi = deg(twistDeg);
        g.joints.push_back(j);
    };
    auto hinge = [&](int parent, int child, float3 pivot, float3 down, float3 flexTowards, float loDeg, float hiDeg) {
        JointDef j;
        j.parent = parent;
        j.child = child;
        j.pivot = pivot;
        j.hinge = true;
        // rotating the child's direction about cross(dir, towards) by a positive angle turns it towards 'towards'
        j.axis = normalize(cross(normalize(down), flexTowards));
        j.lo = deg(loDeg);
        j.hi = deg(hiDeg);
        g.joints.push_back(j);
    };
    ball(B_NECK, B_HEAD, J.neckTop, J.neckTop + make_float3(0, 1, 0), 35, 55);
    ball(B_CHEST, B_NECK, J.neckBase, J.neckTop, 35, 45);
    ball(B_ABDOMEN, B_CHEST, J.chestLow, J.neckBase, 20, 25);
    ball(B_PELVIS, B_ABDOMEN, J.lumbarLow, J.chestLow, 25, 20);
    const float3 fwd = make_float3(0, 0, 1), back = make_float3(0, 0, -1);
    for (int s = 0; s < 2; ++s) {
        auto Q = [&](float3 p) { return s ? mirrorX(p) : p; };
        const int ua = s ? B_UARM_R : B_UARM_L, fa = s ? B_FARM_R : B_FARM_L, ha = s ? B_HAND_R : B_HAND_L;
        const int th = s ? B_THIGH_R : B_THIGH_L, sh = s ? B_SHIN_R : B_SHIN_L, ft = s ? B_FOOT_R : B_FOOT_L;
        ball(B_CHEST, ua, Q(J.shoulder), Q(J.elbow), 95, 70);
        hinge(ua, fa, Q(J.elbow), Q(J.wrist) - Q(J.elbow), fwd, 0, 145);
        ball(fa, ha, Q(J.wrist), Q(J.handTip), 60, 15);
        ball(B_PELVIS, th, Q(J.hip), Q(J.knee), 75, 35);
        hinge(th, sh, Q(J.knee), Q(J.ankle) - Q(J.knee), back, 0, 140);
        ball(sh, ft, Q(J.ankle), Q(J.toe), 30, 15);
    }
    return g;
}
