#include "scenes/tanecik/body_hits.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

HitReport shootBody(BodySim& body, const BodyGraph& g, const weapons::Projectile& proj, float3 o, float3 d, SkeletonSim* skel, float cavityShare) {
    HitReport rep;
    ParticleWorld& w = body.world;
    const float s = w.spacing();
    std::vector<float3> x;
    w.readPositions(x);
    std::vector<uint8_t> liquid;
    w.readFluid(liquid);
    // the particles near the ray (out to the widest cavity a bullet makes, ~10 cm): their distance along it and from it
    struct Near {
        float t, r;
        uint32_t i;
    };
    const float reach = 0.12f;
    std::vector<Near> near;
    for (size_t i = 0; i < x.size(); ++i) {
        if (i < liquid.size() && liquid[i]) continue;   // blood already out: not tissue any more
        const float3 v = x[i] - o;
        const float t = dot(v, d);
        if (t <= 0.0f) continue;
        const float3 q = v - d * t;
        const float r2 = dot(q, q);
        if (r2 < reach * reach) near.push_back({t, std::sqrt(r2), uint32_t(i)});
    }
    if (near.empty()) return rep;
    // the media along the ray, in 2 mm bins: a particle on the path (within 0.6 spacings) covers its own spacing
    const float bin = 0.002f, maxLen = 3.0f;
    std::vector<uint8_t> med(size_t(maxLen / bin) + 1, uint8_t(weapons::Medium::Air));
    for (const Near& n : near) {
        if (n.r > 0.6f * s) continue;
        const uint8_t m = uint8_t(g.kind[n.i] == PK_BONE ? weapons::Medium::Bone : weapons::Medium::Tissue);
        const int a = std::max(0, int((n.t - 0.5f * s) / bin)), b = std::min(int(med.size()) - 1, int((n.t + 0.5f * s) / bin));
        for (int k = a; k <= b; ++k) med[size_t(k)] = std::max(med[size_t(k)], m);
    }
    const weapons::MediumAt medium = [&](float sAlong) {
        const int k = int(sAlong / bin);
        return k >= 0 && k < int(med.size()) ? weapons::Medium(med[size_t(k)]) : weapons::Medium::Air;
    };
    const weapons::Path path = weapons::traceShot(proj, medium, maxLen);
    rep.pDeposited = path.pDeposited;
    rep.tissue = path.tissueLength;
    rep.exited = path.exitSpeed > 0.0f;
    rep.exitSpeed = path.exitSpeed;
    // each step's particles: the path's (crush) take its momentum along the ray and lose their bonds; the cavity's are
    // thrown outward
    std::sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.t < b.t; });
    const float pm = body.fleshParticleMass();   // kg
    std::unordered_map<uint32_t, float3> dv;
    std::vector<uint32_t> crush;
    float3 bonePush[B_COUNT];
    float3 boneAt[B_COUNT];
    float boneN[B_COUNT];
    for (int b = 0; b < B_COUNT; ++b) bonePush[b] = boneAt[b] = make_float3(0, 0, 0), boneN[b] = 0.0f;
    bool first = true;
    size_t lo = 0;
    double pending = 0.0;   // momentum of steps that fell between particles: handed to the next particles on the path
    for (const weapons::Segment& seg : path.segments) {
        if (seg.dp <= 0.0f && seg.cavityRadius <= 0.0f) continue;
        // the particles whose place along the ray falls in this step
        while (lo < near.size() && near[lo].t < seg.s0) ++lo;
        std::vector<const Near*> inPath, inCavity;
        const float rc = std::max(seg.crushRadius, 0.5f * s);
        for (size_t k = lo; k < near.size() && near[k].t < seg.s1; ++k) {
            if (near[k].r <= rc) inPath.push_back(&near[k]);
            else if (near[k].r <= seg.cavityRadius) inCavity.push_back(&near[k]);
        }
        if (!inPath.empty()) {
            if (first) { rep.entry = o + d * inPath.front()->t; first = false; }
            const float share = float((seg.dp + pending) / double(inPath.size()));   // N s each, along the ray
            pending = 0.0;
            for (const Near* n : inPath) {
                const int bone = g.bone[n->i];
                if (g.kind[n->i] == PK_BONE) {   // a bone particle does not move: its share goes to the skeleton
                    bonePush[bone] = bonePush[bone] + d * share;
                } else {
                    dv[n->i] = dv[n->i] + d * (share / pm);
                    crush.push_back(n->i);
                    bonePush[bone] = bonePush[bone] + d * share;   // carried by its bone as well (its mass is the bone's)
                }
                boneAt[bone] = boneAt[bone] + x[n->i];
                boneN[bone] += 1.0f;
                rep.pApplied += share;
            }
        } else {
            pending += seg.dp;   // a step between lattice points: no particle sits on it
        }
        for (const Near* n : inCavity) {
            if (g.kind[n->i] == PK_BONE) continue;
            const float3 c = o + d * n->t;
            const float3 out = (x[n->i] - c) / std::max(n->r, 1e-6f);
            const float v = weapons::radialSpeed(seg, n->r, 0.5f * s, cavityShare);
            dv[n->i] = dv[n->i] + out * v;
            ++rep.thrown;
        }
    }
    std::sort(crush.begin(), crush.end());
    crush.erase(std::unique(crush.begin(), crush.end()), crush.end());
    rep.crushed = int(crush.size());
    rep.bondsBroken = w.breakBondsOf(crush);
    std::vector<uint32_t> idx;
    std::vector<float3> vel;
    idx.reserve(dv.size());
    vel.reserve(dv.size());
    for (const auto& kv : dv) { idx.push_back(kv.first); vel.push_back(kv.second); }
    // in index order: the same shot, the same result
    std::vector<size_t> ord(idx.size());
    for (size_t k = 0; k < ord.size(); ++k) ord[k] = k;
    std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return idx[a] < idx[b]; });
    std::vector<uint32_t> idx2(idx.size());
    std::vector<float3> vel2(vel.size());
    for (size_t k = 0; k < ord.size(); ++k) { idx2[k] = idx[ord[k]]; vel2[k] = vel[ord[k]]; }
    w.addVelocity(idx2, vel2);
    if (skel)
        for (int b = 0; b < B_COUNT; ++b)
            if (boneN[b] > 0.0f) skel->push(b, bonePush[b], boneAt[b] / boneN[b]);
    return rep;
}
