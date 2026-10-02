#include "scenes/tanecik/body_sim.h"

#include <algorithm>
#include <numeric>

namespace {

// the order: bone particles (by bone), then each bone's flesh (tissue and skin); stable within each run
std::vector<uint32_t> bodyOrder(const BodyGraph& g) {
    const size_t n = g.kind.size();
    std::vector<uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        const int ka = g.kind[a] == PK_BONE ? 0 : 1, kb = g.kind[b] == PK_BONE ? 0 : 1;
        if (ka != kb) return ka < kb;
        return g.bone[a] < g.bone[b];
    });
    return order;
}

}   // namespace

bool BodySim::build(BodyParticles& body, BodyGraph& g, const Config& cfg) {
    destroy();
    // ---- reorder the body and its graph: new index k holds old particle order[k]
    const std::vector<uint32_t> order = bodyOrder(g);
    const size_t n = order.size();
    std::vector<uint32_t> newOf(n);
    for (size_t k = 0; k < n; ++k) newOf[order[k]] = uint32_t(k);
    {
        BodyParticles b2 = body;
        BodyGraph g2 = g;
        for (size_t k = 0; k < n; ++k) {
            const uint32_t o = order[k];
            b2.pos[k] = body.pos[o];
            b2.bone[k] = body.bone[o];
            b2.skin[k] = body.skin[o];
            g2.kind[k] = g.kind[o];
            g2.bone[k] = g.bone[o];
        }
        for (BodyBond& bd : g2.bonds) {
            bd.a = newOf[bd.a];
            bd.b = newOf[bd.b];
        }
        for (auto& list : g2.boneParticles)
            for (uint32_t& i : list) i = newOf[i];
        body = std::move(b2);
        g = std::move(g2);
    }
    // ---- the world
    world.begin(body.spacing);
    applyPhysics(world, cfg);
    world.params.gravityRamp = cfg.f("beden.yercekimi_rampa", 0.5f);   // settle softly (Yıkım's lesson: load slowly)
    world.reserve(int(n) + cfg.i("beden.ek_sivi", 0));
    const float density = cfg.f("beden.yogunluk", 1000.0f);
    fleshMass_ = density * body.spacing * body.spacing * body.spacing;
    nBone_ = 0;
    while (nBone_ < int(n) && g.kind[size_t(nBone_)] == PK_BONE) ++nBone_;
    {   // the bones: one plain solid group (they never meet each other: the joints keep them apart), kinematic
        const int grp = world.addGroup(0u);
        std::vector<float3> p(body.pos.begin(), body.pos.begin() + nBone_);
        world.addParticles(p, cfg.f("kemik.yogunluk", 1900.0f), grp);
        world.setKinematic(0, nBone_);
    }
    for (int b = 0; b < B_COUNT; ++b) {   // each bone's flesh, its own group, liquefying
        std::vector<float3> p;
        for (size_t i = size_t(nBone_); i < n; ++i)
            if (g.bone[i] == b) p.push_back(body.pos[i]);
        if (p.empty()) continue;
        const int grp = world.addGroup(PG_LIQUEFY);
        world.addParticles(p, density, grp);
    }
    // the bonds, by kind: compliance (m/N), breaking strain, crushing strain
    struct K {
        float c, br, cr;
    };
    const K kind[4] = {{cfg.f("beden.doku_uyum", 1e-3f), cfg.f("beden.doku_kopma", 0.5f), cfg.f("beden.ezilme", 0.6f)},
                       {cfg.f("beden.deri_uyum", 2e-4f), cfg.f("beden.deri_kopma", 0.6f), cfg.f("beden.ezilme", 0.6f)},
                       {cfg.f("beden.tutunma_uyum", 3e-4f), cfg.f("beden.tutunma_kopma", 0.5f), cfg.f("beden.ezilme", 0.6f)},
                       {cfg.f("beden.gevsek_uyum", 1e-2f), cfg.f("beden.gevsek_kopma", 0.3f), cfg.f("beden.ezilme", 0.6f)}};
    for (const BodyBond& bd : g.bonds) {
        const K& k = kind[bd.kind];
        world.addBond(int(bd.a), int(bd.b), k.c, k.br, k.cr);
    }
    world.commit();
    // ---- the bone particles' rest and their driving buffers
    rest_.assign(body.pos.begin(), body.pos.begin() + nBone_);
    restBone_.assign(g.bone.begin(), g.bone.begin() + nBone_);
    target_.resize(size_t(nBone_));
    std::vector<uint32_t> idx(static_cast<size_t>(nBone_));
    std::iota(idx.begin(), idx.end(), 0u);
    cudaMalloc(reinterpret_cast<void**>(&dIdx_), std::max<size_t>(1, idx.size()) * sizeof(uint32_t));
    cudaMalloc(reinterpret_cast<void**>(&dTarget_), std::max<size_t>(1, idx.size()) * sizeof(float3));
    cudaMemcpy(dIdx_, idx.data(), idx.size() * sizeof(uint32_t), cudaMemcpyHostToDevice);
    return true;
}

void BodySim::drive(const Xform xf[B_COUNT], float dt) {
    if (nBone_ <= 0) return;
    for (int i = 0; i < nBone_; ++i) target_[size_t(i)] = xf[restBone_[size_t(i)]].apply(rest_[size_t(i)]);
    cudaMemcpy(dTarget_, target_.data(), target_.size() * sizeof(float3), cudaMemcpyHostToDevice);
    world.driveKinematic(dIdx_, dTarget_, nBone_, dt);
}

void BodySim::destroy() {
    if (dIdx_) cudaFree(dIdx_);
    if (dTarget_) cudaFree(dTarget_);
    dIdx_ = nullptr;
    dTarget_ = nullptr;
    world.destroy();
    nBone_ = 0;
}
