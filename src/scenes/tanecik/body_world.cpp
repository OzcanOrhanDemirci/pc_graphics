#include "scenes/tanecik/body_world.h"

#include <chrono>

BodyWorld::~BodyWorld() {
    if (dTint_) cudaFree(dTint_);
}

bool BodyWorld::build() {
    if (dTint_) cudaFree(dTint_);
    dTint_ = nullptr;
    app_.build();
    if (!body.build(app_.body, app_.graph, app_.cfg)) return false;
    app_.skinBind = bindSkin(app_.skin, app_.body, app_.graph);
    const float s = app_.body.spacing;
    if (!skel.init(app_.graph, app_.body, app_.cfg.f("beden.yogunluk", 1000.0f) * s * s * s, true)) return false;
    if (app_.cfg.i("beden.ayak", 1)) skel.pin(B_PELVIS, true);   // the hidden stand: no ground needed (it cost ~70 ms a frame)
    else {
        SkelGround gr;
        gr.radius = 0.5f * s;
        skel.setGround(app_.graph, app_.body, gr, app_.cfg.i("denge.temas_seyrek", 6));
    }
    muscles.init(skel);
    muscles.ankleStrategy = false;
    sub_ = app_.cfg.i("iskelet.alt_adim", 20);
    iters_ = app_.cfg.i("iskelet.yineleme", 4);
    cudaMalloc(reinterpret_cast<void**>(&dTint_), size_t(body.world.count()) * sizeof(float));
    return true;
}

void BodyWorld::step(float dt) {
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<SkelBody> b;
    skel.readBodies(b);
    muscles.update(b, dt);
    muscles.apply(skel);
    const float h = dt / sub_;
    for (int k = 0; k < sub_; ++k) skel.step(h, make_float3(0, -9.81f, 0), iters_, app_.cfg.f("iskelet.surtunme", 2.0f), nullptr);
    Xform xf[B_COUNT];
    skel.transforms(xf);
    body.drive(xf, dt);
    {   // the wounds' pressure: the blood is pushed out of the holes, fading as the bleeding does (else it pools inside)
        const float v0 = app_.cfg.f("yara.basinc", 0.4f), r = app_.cfg.f("yara.basinc_yaricap", 0.035f), fade = app_.cfg.f("yara.pihtilasma", 4.0f);
        for (Wound& w : wounds) {
            body.world.woundPressure(w.at, r, w.out, v0 * std::exp(-w.age / fade));
            w.age += dt;
        }
    }
    body.step(dt);
    physicsMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void BodyWorld::feed() {
    const int n = body.world.count();
    body.world.exportPositions(app_.dPos);
    body.world.exportDisplay(dTint_);
    cudaDeviceSynchronize();
    Xform xf[B_COUNT];
    skel.transforms(xf);
    app_.renderer.setBoneXforms(xf);
    app_.renderer.deformSkin(app_.dPos, dTint_);
    app_.renderer.updateParticles(app_.dPos, dTint_);
    app_.renderer.updateLiquid(app_.dPos, dTint_, n, 0.5f * app_.body.spacing);
}

HitReport BodyWorld::shoot(weapons::Gun g, float power, float3 origin, float3 aim) {
    const float3 dir = normalize(aim - origin);
    const weapons::Load l = weapons::gunLoad(g, power);
    const float share = app_.cfg.f("yara.bosluk_pay", 0.8f), rate = app_.cfg.f("yara.kanama", 0.8f), radius = app_.cfg.f("yara.kanama_yaricap", 0.025f);
    auto remember = [&](const HitReport& h, float3 d) {
        if (h.crushed <= 0) return;
        wounds.push_back({h.entry, d * -1.0f, 0.0f});
        if (h.exited) wounds.push_back({h.exit, d, 0.0f});
    };
    if (g != weapons::Gun::Shotgun) {
        const HitReport h = shootBody(body, app_.graph, l.shot, origin, dir, &skel, share, rate, radius);
        remember(h, dir);
        return h;
    }
    HitReport sum;
    for (const float3& d : weapons::pelletDirections(l, dir, shotSeed_++)) {
        const HitReport h = shootBody(body, app_.graph, l.shot, origin, d, &skel, share, rate, radius);
        remember(h, d);
        sum.crushed += h.crushed;
        sum.thrown += h.thrown;
        sum.bondsBroken += h.bondsBroken;
        sum.pDeposited += h.pDeposited;
        sum.pApplied += h.pApplied;
        sum.tissue = std::fmax(sum.tissue, h.tissue);
        sum.exited = sum.exited || h.exited;
    }
    return sum;
}

int BodyWorld::liquidCount() {
    std::vector<uint8_t> f;
    body.world.readFluid(f);
    int n = 0;
    for (uint8_t x : f) n += x;
    return n;
}
