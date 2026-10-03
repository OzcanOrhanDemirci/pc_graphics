#include "scenes/laboratuvar/lab_world.h"
#include "scenes/laboratuvar/lab_kernels.h"
#include "scenes/laboratuvar/weapons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace {

constexpr float kPi = 3.14159265358979f;

unsigned hashU(unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float hashF(unsigned x) { return (hashU(x) >> 8) * (1.0f / 16777216.0f); }

void identity(float m[12]) {
    const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    std::memcpy(m, id, sizeof(id));
}
void translation(float m[12], float3 t) {
    identity(m);
    m[3] = t.x;
    m[7] = t.y;
    m[11] = t.z;
}

// the glass ware of the experiments (lab_geom.h profiles), one place: the renderer draws the same profiles
struct Glass {
    float ri, H, wall, base, taper;
    float fillet = 0.004f;
    Profile profile() const { return glassProfile(ri, H, wall, base, taper, fillet); }
};

// a disc of points (a hexagonal lattice of spacing s inside radius r), turned by angle
std::vector<float2> disc(float r, float s, float angle) {
    std::vector<float2> out;
    const float dy = 0.8660254f * s;
    const int ny = int(std::ceil(r / dy));
    const float c = std::cos(angle), sn = std::sin(angle);
    for (int j = -ny; j <= ny; ++j) {
        const float y = j * dy, off = (j & 1) ? 0.5f * s : 0.0f;
        const int nx = int(std::ceil(r / s)) + 1;
        for (int i = -nx; i <= nx; ++i) {
            const float x = i * s + off;
            if (x * x + y * y > r * r) continue;
            out.push_back(make_float2(c * x - sn * y, sn * x + c * y));
        }
    }
    if (out.empty()) out.push_back(make_float2(0, 0));
    return out;
}

}   // namespace

const char* deneyKey(Deney d) {
    switch (d) {
    case Deney::Su: return "su";
    case Deney::Bal: return "bal";
    case Deney::Kum: return "kum";
    case Deney::Balon: return "balon";
    case Deney::Karpuz: return "karpuz";
    case Deney::Jel: return "jel";
    case Deney::Damla: return "damla";
    }
    return "su";
}

void LabWorld::destroy() {
    for (Held& h : held_)
        for (void* p : {static_cast<void*>(h.dRest), static_cast<void*>(h.dTarget), static_cast<void*>(h.dIdx)})
            if (p) cudaFree(p);
    held_.clear();
    if (dBase_) cudaFree(dBase_);
    if (dFluid_) cudaFree(dFluid_);
    dBase_ = nullptr;
    dFluid_ = nullptr;
    dCap_ = 0;
    pw.destroy();
}

int LabWorld::addHeld(const std::vector<float3>& restLocal, int propId, int group) {
    std::vector<float3> world(restLocal.size());
    for (size_t i = 0; i < restLocal.size(); ++i) world[i] = xfApply(prop[propId].xf, restLocal[i]);
    const int first = pw.addParticles(world, 2500.0f, group);
    pw.setKinematic(first, int(world.size()));
    code.insert(code.end(), world.size(), DC_NONE);
    Held h;
    h.first = first;
    h.count = int(world.size());
    h.propId = propId;
    cudaMalloc(reinterpret_cast<void**>(&h.dRest), world.size() * sizeof(float3));
    cudaMalloc(reinterpret_cast<void**>(&h.dTarget), world.size() * sizeof(float3));
    cudaMalloc(reinterpret_cast<void**>(&h.dIdx), world.size() * sizeof(uint32_t));
    cudaMemcpy(h.dRest, restLocal.data(), world.size() * sizeof(float3), cudaMemcpyHostToDevice);
    std::vector<uint32_t> idx(world.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = uint32_t(first + int(i));
    cudaMemcpy(h.dIdx, idx.data(), idx.size() * sizeof(uint32_t), cudaMemcpyHostToDevice);
    held_.push_back(h);
    return first;
}

bool LabWorld::build(Deney d, const Config& sceneCfg, bool fine_, const LabInputs& in_) {
    destroy();
    deney = d;
    fine = fine_;
    in = in_;
    // the experiment's own settings over the shared ones: "<deney>.fizik.x" stands for "fizik.x"
    cfg = sceneCfg;
    const std::string pre = std::string(deneyKey(d)) + ".";
    for (const auto& kv : sceneCfg.values)
        if (kv.first.rfind(pre + "fizik.", 0) == 0) cfg.values["fizik." + kv.first.substr(pre.size() + 6)] = kv.second;
    s = cfg.f(pre + (fine ? "aralik_ince" : "aralik_kaba"), fine ? 0.0015f : 0.0025f);
    pw.begin(s);
    // every setting afresh: a trigger may have changed one (the needle turns the tear's tip on), and the world of the
    // next experiment must not inherit it (a melon after the balloon crumbled on its stand: measured)
    pw.params = PParams{};
    restScale_ = restTarget_ = 1.0f;
    applyPhysics(pw, cfg);
    if (fine) {   // smaller particles move as far in fewer of their own sizes: the same substeps per second suffice
        pw.params.substeps = cfg.i(pre + "ince_alt_adim", pw.params.substeps);
        // a finer membrane has more bonds between its ends: Gauss-Seidel needs more passes for the same stiffness
        pw.params.bondIterations = cfg.i(pre + "ince_bag_yineleme", pw.params.bondIterations);
    }
    code.clear();
    for (bool& u : propUsed) u = false;
    for (PropPose& p : prop) identity(p.xf);
    membraneFirst = -1;
    membraneCount = 0;
    membraneTris.clear();
    ballFirst = -1;
    ballCount = 0;
    ballGroup = 0;
    sandGroup_ = 0;
    emitCarry_ = 0.0;
    emitSeed_ = 1;
    heldLayers_.clear();
    lastLayerPos_.clear();
    lastLayerFirst_ = -1;
    triggered_ = false;
    ev_ = LabEvent{};
    switch (d) {
    case Deney::Su: buildSu(); break;
    case Deney::Bal: buildBal(); break;
    case Deney::Kum: buildKum(); break;
    case Deney::Balon: buildBalon(); break;
    case Deney::Karpuz: buildKarpuz(); break;
    case Deney::Jel: buildJel(); break;
    case Deney::Damla: buildDamla(); break;
    }
    pw.reserve(int(code.size()) + cfg.i(pre + "ek_kapasite", 0));
    if (d == Deney::Bal && cfg.i("bal.yaylar", 1)) pw.reserveSprings(cfg.i("bal.yay_kapasite", 1200000));
    pw.commit();
    t0_ = pw.time();
    placeProps();
    return true;
}

void LabWorld::settle(double seconds, double dt) {
    const double ramp = pw.params.gravityRamp;
    pw.params.gravityRamp = float(std::min(0.4, seconds * 0.5));
    const int frames = int(std::ceil(seconds / dt));
    for (int k = 0; k < frames; ++k) {
        if (restTarget_ != restScale_) {
            // a stretched membrane's rest length brought in over the first half of the settling (at once, the light
            // rubber would snap inwards faster than the water can push back, and tear: measured, 4133 of 31256 bonds)
            const float u = std::min(1.0f, float((k + 1) * dt / std::max(0.05, 0.5 * seconds)));
            const float want = 1.0f + (restTarget_ - 1.0f) * smoothstepf(0.0f, 1.0f, u);
            pw.scaleAllBondRest(want / restScale_);
            restScale_ = want;
        }
        pw.step(float(dt));
        if (std::getenv("LAB_TANI") && (k + 1) % std::max(1, int(0.1 / dt)) == 0) {
            const PStats st = pw.stats();
            std::printf("    oturma %.2f s: dinlenme payi %.2f, kopuk bag %d, en hizli %.2f m/s, en buyuk sikisma %.1f%%\n", (k + 1) * dt, restScale_, st.broken,
                        st.maxSpeed, st.densityMax * 100.0f);
        }
    }
    if (restTarget_ != restScale_) {
        pw.scaleAllBondRest(restTarget_ / restScale_);
        restScale_ = restTarget_;
    }
    pw.params.gravityRamp = float(ramp);
    t0_ = pw.time();
}

// ---------------------------------------------------------------------------------------------------- Su
// a tumbler on the table; a beaker of water over it, its lip (the pivot) `height` above the tumbler's rim, tilting
// clockwise (towards +x) over a few seconds once poured

namespace {
const Glass kTumbler{0.033f, 0.105f, 0.0025f, 0.008f, 0.92f};
const Glass kBeaker{0.040f, 0.115f, 0.002f, 0.004f, 1.0f};
}   // namespace

float LabWorld::suAngle(double t) const {
    if (t <= 0.0) return 0.0f;
    const float A = cfg.f("su.egim_derece", 118.0f) * kPi / 180.0f, T = cfg.f("su.egim_sure", 2.6f);
    const float u = float(std::min(1.0, t / T));
    // quickly to where the water reaches the lip, then slowly on (a hand pours so)
    const float e = u < 0.35f ? 0.55f * smoothstepf(0.0f, 0.35f, u) : 0.55f + 0.45f * smoothstepf(0.35f, 1.0f, u);
    return -A * e;
}

void LabWorld::buildSu() {
    const float h = cfg.f("su.yukseklik_en_az", 0.12f) + in.height * (cfg.f("su.yukseklik_en_cok", 0.45f) - cfg.f("su.yukseklik_en_az", 0.12f));
    propUsed[P_TUMBLER] = propUsed[P_BEAKER] = true;
    identity(prop[P_TUMBLER].xf);
    // the beaker: its pivot (the right rim, mid wall) in its own frame, placed over the tumbler's left rim
    const float3 pivotLocal = make_float3(kBeaker.ri + 0.5f * kBeaker.wall, kBeaker.H, 0.0f);
    // the lip over the tumbler, further back the higher it is: the stream leaves it with some speed across and falls
    // longer from higher up (from 45 cm it missed the glass altogether); a hand pouring from high aims so
    const float lipX = cfg.f("su.dudak_x", -0.050f) - cfg.f("su.dudak_egim", 0.25f) * std::max(0.0f, h - 0.19f);
    const float3 pivot = make_float3(lipX, kTumbler.H + h, 0.0f);
    suPivot_ = pivotLocal;
    suBeakerBase_ = pivot - pivotLocal;
    xfRotateZ(prop[P_BEAKER].xf, 0.0f, suPivot_, suBeakerBase_);
    const int gT = pw.addGroup(0);
    addHeld(sampleProfileWalls(kTumbler.profile(), s, 0.75f * s), P_TUMBLER, gT);
    const int gB = pw.addGroup(0);
    const std::vector<float3> beakerWalls = sampleProfileWalls(kBeaker.profile(), s, 0.75f * s);
    addHeld(beakerWalls, P_BEAKER, gB);
    // the water, at rest in the beaker: a spacing from the walls' particles
    const float wallIn = std::min(0.5f * s, 0.5f * kBeaker.wall), baseIn = std::min(0.5f * s, 0.5f * kBeaker.base);
    const float rWater = kBeaker.ri + wallIn - 0.5f * s;
    const float y0 = kBeaker.base - baseIn + 0.5f * s;
    std::vector<float3> water = fillCylinder(rWater, y0, y0 + cfg.f("su.dolum", 0.070f), s, 7u);
    removeNear(water, beakerWalls, 0.9f * s);   // the beaker's rounded inside corner
    for (float3& p : water) p = xfApply(prop[P_BEAKER].xf, p);
    pw.addParticles(water, 1000.0f, 0);
    code.insert(code.end(), water.size(), DC_WATER);
    boxLo = make_float3(-0.45f, -0.01f, -0.35f);
    boxHi = make_float3(0.45f, kTumbler.H + h + 0.15f, 0.35f);
}

// ---------------------------------------------------------------------------------------------------- Bal
// a glass dish on the table, a dipper over it (it follows the mouse); honey runs off it as a thin rope

namespace {
const Glass kDish{0.058f, 0.016f, 0.003f, 0.004f, 0.93f};
}

void LabWorld::buildBal() {
    propUsed[P_DISH] = propUsed[P_DIPPER] = true;
    identity(prop[P_DISH].xf);
    const int g = pw.addGroup(0);
    addHeld(sampleProfileWalls(kDish.profile(), s, 0.75f * s), P_DISH, g);
    boxLo = make_float3(-0.45f, -0.01f, -0.35f);
    boxHi = make_float3(0.45f, 0.5f, 0.35f);
}

// ---------------------------------------------------------------------------------------------------- Kum
// a funnel over the table (it follows the mouse) pours sand; a beaker to fill; a steel ball parked aside

namespace {
const Glass kSandBeaker{0.034f, 0.085f, 0.002f, 0.004f, 1.0f};
}

void LabWorld::buildKum() {
    propUsed[P_BEAKER] = propUsed[P_FUNNEL] = true;
    translation(prop[P_BEAKER].xf, cfg.v3("kum.beher", make_float3(0.075f, 0.0f, 0.0f)));
    const int g = pw.addGroup(0);
    addHeld(sampleProfileWalls(kSandBeaker.profile(), s, 0.75f * s), P_BEAKER, g);
    // the steel ball: a rigid cluster, parked on the table far to the side until it is let go
    ballRadius = cfg.f("kum.bilye_yaricap", 0.0125f);
    ballGroup = pw.addGroup(PG_RIGID);
    std::vector<float3> ball;
    const int k = int(std::ceil(ballRadius / s));
    const float3 park = make_float3(2.0f, ballRadius, 0.0f);
    for (int z = -k; z <= k; ++z)
        for (int y = -k; y <= k; ++y)
            for (int x = -k; x <= k; ++x) {
                const float3 p = make_float3(x * s, y * s, z * s);
                if (length(p) <= ballRadius - 0.5f * s) ball.push_back(park + p);
            }
    ballFirst = pw.addParticles(ball, cfg.f("kum.bilye_yogunluk", 7850.0f), ballGroup);
    ballCount = int(ball.size());
    code.insert(code.end(), ball.size(), DC_STEEL);
    sandGroup_ = pw.addGroup(PG_GRAIN);
    boxLo = make_float3(-0.45f, -0.01f, -0.35f);
    boxHi = make_float3(0.45f, 0.5f, 0.35f);
}

// ---------------------------------------------------------------------------------------------------- Balon
// a water balloon hangs from a hook by its knot: one layer of rubber (bonded to its neighbours, stretched: its rest
// length is a share of the inflated one, so the rubber's tension holds the water's pressure), water inside

void LabWorld::buildBalon() {
    const float R = cfg.f("balon.yaricap", 0.06f);
    const float3 c = cfg.v3("balon.merkez", make_float3(0.0f, 0.20f, 0.0f));
    // hung by its knot (balon.asili 1) or resting on the table (0): hanging, the light rubber does not carry the
    // water's weight up to the knot in time with a few iterations a substep (it tore off at the knot at once,
    // measured: docs/laboratuvar/DURUM.md); on the table the table carries it
    const bool hung = cfg.i("balon.asili", 0) != 0;
    propUsed[P_HOOK] = hung;
    translation(prop[P_HOOK].xf, c + make_float3(0, R, 0));
    std::vector<float3> skin, water;
    const float a = cfg.f("balon.deri_aralik", 0.85f) * s;   // the membrane's lattice: a little closer than the water's
    const int m = int(4.0f * kPi * R * R / (0.8660254f * a * a));
    const float golden = kPi * (3.0f - std::sqrt(5.0f));
    for (int q = 0; q < m; ++q) {
        const float y = 1.0f - 2.0f * (q + 0.5f) / m, r = std::sqrt(std::max(0.0f, 1.0f - y * y)), ph = golden * q;
        skin.push_back(c + make_float3(r * std::cos(ph), y, r * std::sin(ph)) * R);
    }
    const int k = int(std::ceil(R / s)) + 1;
    for (int z = -k; z <= k; ++z)
        for (int y = -k; y <= k; ++y)
            for (int x = -k; x <= k; ++x) {
                const unsigned hh = unsigned((z + 64) * 16384 + (y + 64) * 128 + (x + 64));
                const float3 p = make_float3(x * s, y * s, z * s) + make_float3(hashF(hh) - 0.5f, hashF(hh + 1) - 0.5f, hashF(hh + 2) - 0.5f) * (0.08f * s);
                if (length(p) <= R - s) water.push_back(c + p);
            }
    // the knot: the membrane's top cap is held (kinematic)
    const int gSkin = pw.addGroup(0);
    membraneFirst = pw.addParticles(skin, cfg.f("balon.deri_yogunluk", 1100.0f) * 0.0008f / s, gSkin);   // rubber 0.8 mm thick, over a spacing
    membraneCount = int(skin.size());
    code.insert(code.end(), skin.size(), DC_RUBBER);
    if (hung) {
        // the cap: Fibonacci points are sorted by height (q = 0 at the top): the first few are the knot
        const float capR = cfg.f("balon.dugum_yaricap", 0.006f);
        int cap = 0;
        while (cap < membraneCount && length(skin[size_t(cap)] - (c + make_float3(0, R, 0))) < capR) ++cap;
        pw.setKinematic(membraneFirst, std::max(cap, 3));
    }
    const int b0 = pw.bondCount();
    pw.bondWithin(membraneFirst, membraneCount, cfg.f("balon.bag_uzaklik", 1.6f) * a, cfg.f("balon.uyum", 2e-4f), cfg.f("balon.kopma", 1.0f),
                  cfg.f("balon.ezilme", 0.9f));
    (void)b0;   // the stretch comes in while it settles (settle(): from 1 to balon.on_gerilme)
    restScale_ = 1.0f;
    restTarget_ = cfg.f("balon.on_gerilme", 0.6f);
    // the membrane's triangles, for the drawing: the convex hull of points on a sphere is its triangulation; a
    // cheap way: each point to its neighbours sorted by angle (a Delaunay-like fan), kept where three agree
    {
        std::vector<float3> unit(skin.size());
        for (size_t i = 0; i < skin.size(); ++i) unit[i] = normalize(skin[i] - c);
        const float cell = 2.5f * a / R;
        std::unordered_map<long long, std::vector<int>> grid;
        auto key = [&](int x, int y, int z) { return (long long)(x + 512) * 1048576LL + (long long)(y + 512) * 1024LL + (long long)(z + 512); };
        for (size_t i = 0; i < unit.size(); ++i)
            grid[key(int(std::floor(unit[i].x / cell)), int(std::floor(unit[i].y / cell)), int(std::floor(unit[i].z / cell)))].push_back(int(i));
        std::unordered_map<long long, int> seen;
        for (size_t i = 0; i < unit.size(); ++i) {
            const float3 u = unit[i];
            std::vector<std::pair<float, int>> nb;
            const int cx = int(std::floor(u.x / cell)), cy = int(std::floor(u.y / cell)), cz = int(std::floor(u.z / cell));
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                        if (it == grid.end()) continue;
                        for (int j : it->second) {
                            if (j == int(i)) continue;
                            const float3 w = unit[size_t(j)] - u;
                            if (dot(w, w) < cell * cell) nb.push_back({0.0f, j});
                        }
                    }
            // the nearest 6-7 neighbours, by angle round u
            std::sort(nb.begin(), nb.end(), [&](const auto& A, const auto& B) {
                const float3 wa = unit[size_t(A.second)] - u, wb = unit[size_t(B.second)] - u;
                return dot(wa, wa) < dot(wb, wb);
            });
            if (nb.size() > 7) nb.resize(7);
            const Frame f = Frame::from(u);
            for (auto& e : nb) {
                const float3 w = unit[size_t(e.second)] - u;
                e.first = std::atan2(dot(w, f.b), dot(w, f.t));
            }
            std::sort(nb.begin(), nb.end());
            for (size_t q = 0; q < nb.size(); ++q) {
                const int j = nb[q].second, l = nb[(q + 1) % nb.size()].second;
                float da = nb[(q + 1) % nb.size()].first - nb[q].first;
                if (da < 0) da += 2.0f * kPi;
                if (da > 0.5f * kPi * 1.4f) continue;   // a gap, not a triangle
                int t[3] = {int(i), j, l};
                std::sort(t, t + 3);
                const long long kk = (long long)t[0] * 4000000000LL / 1000 + (long long)t[1] * 100000LL + t[2];
                if (++seen[kk] == 2) {   // two of its corners agree: a face
                    // outward winding
                    const float3 A = skin[size_t(i)], B = skin[size_t(j)], C = skin[size_t(l)];
                    uint3 tri = make_uint3(unsigned(i), unsigned(j), unsigned(l));
                    if (dot(cross(B - A, C - A), A - c) < 0.0f) std::swap(tri.y, tri.z);
                    membraneTris.push_back(tri);
                }
            }
        }
    }
    pw.addParticles(water, 1000.0f, 0);
    code.insert(code.end(), water.size(), DC_WATER);
    boxLo = make_float3(-0.5f, -0.01f, -0.4f);
    boxHi = make_float3(0.5f, 0.4f, 0.4f);
}

// ---------------------------------------------------------------------------------------------------- Karpuz
// a small watermelon on a stand: the rind (stiff, brittle bonds), the flesh (weak bonds, turning to juice when they
// are all gone), bonded to each other; the stand's top kinematic

void LabWorld::buildKarpuz() {
    propUsed[P_STAND] = true;
    const float standH = cfg.f("karpuz.kaide", 0.06f);
    identity(prop[P_STAND].xf);
    const float3 R = cfg.v3("karpuz.yaricap", make_float3(0.085f, 0.075f, 0.085f));
    const float3 c = make_float3(0.0f, standH + R.y - 0.004f, 0.0f);
    // the stand: its top a shallow cup that follows the melon's underside, one layer of particles a spacing under it
    // (on a flat top a round melon is in neutral balance and rolled off while it settled: measured)
    {
        std::vector<float3> top;
        const float rr = cfg.f("karpuz.kaide_yaricap", 0.05f);
        for (const float2& q : disc(rr, 0.75f * s, 0.0f)) {
            // the ellipsoid's lower surface over (x, z), lowered by a spacing
            const float u = q.x / R.x, w = q.y / R.z;
            const float yy = -R.y * std::sqrt(std::max(0.0f, 1.0f - u * u - w * w));
            top.push_back(make_float3(q.x, c.y + yy - 1.0f * s, q.y));
        }
        const int g = pw.addGroup(0);
        addHeld(top, P_STAND, g);
    }
    std::vector<float3> pos;
    std::vector<uint8_t> isRind;
    watermelonFill(R, cfg.f("karpuz.kabuk", 0.011f), std::max(cfg.f("karpuz.deri", 0.003f), 1.6f * s)   /* two particles at least: a one-particle skin shows the white under it in rings (lattice planes) */, s, 3u, pos, isRind);
    std::vector<float3> rind, flesh;
    std::vector<uint8_t> rindCode;
    for (size_t i = 0; i < pos.size(); ++i) {
        if (isRind[i]) {
            rind.push_back(c + pos[i]);
            rindCode.push_back(isRind[i] == 2 ? DC_RIND : DC_PITH);
        } else {
            flesh.push_back(c + pos[i]);
        }
    }
    const int gR = pw.addGroup(0);
    const int rFirst = pw.addParticles(rind, cfg.f("karpuz.kabuk_yogunluk", 950.0f), gR);
    code.insert(code.end(), rindCode.begin(), rindCode.end());
    const int gF = pw.addGroup(PG_LIQUEFY);
    const int fFirst = pw.addParticles(flesh, cfg.f("karpuz.et_yogunluk", 1000.0f), gF);
    code.insert(code.end(), flesh.size(), DC_FLESH);
    const float reach = cfg.f("karpuz.bag_uzaklik", 1.5f) * s;
    // a bond of a lattice of spacing s stands for a stiffness ~ E s (Young's modulus): its compliance 1 / (E s), so
    // the melon is as stiff at any spacing
    const float cRind = 1.0f / (cfg.f("karpuz.kabuk_E", 15e6f) * s), cFlesh = 1.0f / (cfg.f("karpuz.et_E", 1.0e6f) * s);
    const int b0 = pw.bondCount();
    pw.bondWithin(rFirst, int(rind.size()), reach, cRind, cfg.f("karpuz.kabuk_kopma", 0.05f), 0.5f);
    pw.bondWithin(fFirst, int(flesh.size()), reach, cFlesh, cfg.f("karpuz.et_kopma", 0.10f), 0.5f);
    // rind to flesh: across the boundary, as weak as the flesh
    {
        std::unordered_map<long long, std::vector<int>> grid;
        const float cell = reach;
        auto key = [&](int x, int y, int z) { return (long long)(x + 4096) * 67108864LL + (long long)(y + 4096) * 8192LL + (long long)(z + 4096); };
        for (size_t i = 0; i < rind.size(); ++i)
            grid[key(int(std::floor(rind[i].x / cell)), int(std::floor(rind[i].y / cell)), int(std::floor(rind[i].z / cell)))].push_back(int(i));
        for (size_t j = 0; j < flesh.size(); ++j) {
            const float3 p = flesh[j];
            const int cx = int(std::floor(p.x / cell)), cy = int(std::floor(p.y / cell)), cz = int(std::floor(p.z / cell));
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                        if (it == grid.end()) continue;
                        for (int i : it->second)
                            if (length(rind[size_t(i)] - p) < reach)
                                pw.addBond(rFirst + i, fFirst + int(j), cFlesh, cfg.f("karpuz.et_kopma", 0.10f), 0.5f);
                    }
        }
    }
    aim = c;
    aimRadius = std::max(R.x, std::max(R.y, R.z)) * 1.6f;
    // flaws: the same draw at any spacing per bond number is not the same material, but its spread is
    pw.varyBreakStrain(b0, pw.bondCount(), cfg.f("karpuz.kusur", 0.6f), 11u);
    boxLo = make_float3(-1.2f, -0.01f, -1.2f);
    boxHi = make_float3(1.2f, 1.2f, 1.2f);
}

// ---------------------------------------------------------------------------------------------------- Jel
// a block of 10% ordnance gelatin on the table, its long side across the view: an elastic solid of bonded particles
// (compliance 1 / (E s), so as stiff at any spacing); the bullet's temporary cavity stretches it, its bonds pull it back

void LabWorld::buildJel() {
    const float3 B = cfg.v3("jel.boyut", make_float3(0.24f, 0.10f, 0.10f));
    std::vector<float3> pos;
    const int nx = int(std::floor(B.x / s)), ny = int(std::floor(B.y / s)), nz = int(std::floor(B.z / s));
    int k = 0;
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x, ++k) {
                const unsigned h = 5u * 7919u + unsigned(k) * 2654435761u;
                const float j = 0.3f;   // a third of a spacing: no lattice layers through the clear block
                pos.push_back(make_float3(-0.5f * B.x + (x + 0.5f) * s + (hashF(h) - 0.5f) * j * s, (y + 0.5f) * s + (hashF(h + 1) - 0.5f) * j * s,
                                          -0.5f * B.z + (z + 0.5f) * s + (hashF(h + 2) - 0.5f) * j * s));
            }
    const int g = pw.addGroup(0);
    const int first = pw.addParticles(pos, cfg.f("jel.yogunluk", 1030.0f), g);
    code.insert(code.end(), pos.size(), DC_GEL);
    const int b0 = pw.bondCount();
    pw.bondWithin(first, int(pos.size()), cfg.f("jel.bag_uzaklik", 1.5f) * s, 1.0f / (cfg.f("jel.E", 3e5f) * s), cfg.f("jel.kopma", 1.5f), 0.9f);
    pw.varyBreakStrain(b0, pw.bondCount(), cfg.f("jel.kusur", 0.2f), 5u);
    aim = make_float3(0.0f, 0.5f * B.y, 0.0f);
    aimRadius = 0.5f * B.x + 0.02f;
    boxLo = make_float3(-1.0f, -0.01f, -1.0f);
    boxHi = make_float3(1.0f, 1.0f, 1.0f);
}

// ---------------------------------------------------------------------------------------------------- Damla
// a glass petri dish with a thin layer of milk; the trigger lets a drop fall into it (placed just above the surface,
// moving at the speed of a fall from `height`)

namespace {
const Glass kPetri{0.032f, 0.012f, 0.0015f, 0.003f, 0.98f, 0.0008f};
}

void LabWorld::buildDamla() {
    propUsed[P_DISH] = true;
    identity(prop[P_DISH].xf);
    const int g = pw.addGroup(0);
    const std::vector<float3> walls = sampleProfileWalls(kPetri.profile(), s, 0.75f * s);
    addHeld(walls, P_DISH, g);
    const float wallIn = std::min(0.5f * s, 0.5f * kPetri.wall), baseIn = std::min(0.5f * s, 0.5f * kPetri.base);
    const float y0 = kPetri.base - baseIn + 0.5f * s;
    const float depth = cfg.f("damla.derinlik", 0.003f);
    std::vector<float3> milk = fillCylinder(kPetri.ri * kPetri.taper + wallIn - 0.5f * s, y0, y0 + depth, s, 13u);
    removeNear(milk, walls, 0.9f * s);
    pw.addParticles(milk, cfg.f("damla.yogunluk", 1030.0f), 0);
    code.insert(code.end(), milk.size(), DC_MILK);
    milkTop_ = y0 + depth - 0.5f * s;
    aim = make_float3(0.0f, milkTop_, 0.0f);
    boxLo = make_float3(-0.2f, -0.01f, -0.2f);
    boxHi = make_float3(0.2f, 0.2f, 0.2f);
}

// ---------------------------------------------------------------------------------------------------- props

HostMesh labPropMesh(Deney d, int id) {
    switch (id) {
    case LabWorld::P_TUMBLER: return d == Deney::Su ? revolve(kTumbler.profile(), 160) : HostMesh{};
    case LabWorld::P_BEAKER: return d == Deney::Su ? revolve(kBeaker.profile(), 160) : d == Deney::Kum ? revolve(kSandBeaker.profile(), 160) : HostMesh{};
    case LabWorld::P_DISH: return d == Deney::Bal ? revolve(kDish.profile(), 160) : d == Deney::Damla ? revolve(kPetri.profile(), 192) : HostMesh{};
    case LabWorld::P_DIPPER: return d == Deney::Bal ? dipperMesh(0.011f, 0.032f, 0.0045f, 0.26f) : HostMesh{};
    case LabWorld::P_FUNNEL: return d == Deney::Kum ? revolve(funnelProfile(0.045f, 0.0045f, 0.05f, 0.03f, 0.0018f), 128) : HostMesh{};
    case LabWorld::P_STAND: {
        if (d != Deney::Karpuz) return HostMesh{};
        Profile p;
        p.push_back({0.0f, 0.0f, false});
        p.push_back({0.055f, 0.0f, false});
        p.push_back({0.055f, 0.012f, false});
        p.push_back({0.035f, 0.02f, true});
        p.push_back({0.03f, 0.045f, true});
        p.push_back({0.05f, 0.056f, false});
        p.push_back({0.05f, 0.06f, false});
        p.push_back({0.0f, 0.06f, false});
        return revolve(p, 96);
    }
    case LabWorld::P_HOOK: {
        if (d != Deney::Balon || true) return HostMesh{};   // the balloon rests on the table (buildBalon)
        // a thin steel rod from far above down to a small clip at the knot (its frame: the knot at the origin)
        Profile p;
        p.push_back({0.0f, -0.004f, false});
        p.push_back({0.0045f, -0.004f, false});
        p.push_back({0.0045f, 0.012f, false});
        p.push_back({0.0015f, 0.014f, false});
        p.push_back({0.0015f, 0.6f, false});
        p.push_back({0.0f, 0.6f, false});
        return revolve(p, 32);
    }
    }
    return HostMesh{};
}

int labPropMaterial(Deney, int id) {
    if (id == LabWorld::P_DIPPER || id == LabWorld::P_STAND) return 1;
    if (id == LabWorld::P_HOOK) return 2;
    return 0;
}

// ---------------------------------------------------------------------------------------------------- running

void LabWorld::placeProps() {
    if (deney == Deney::Su) {
        const float a = triggered_ ? suAngle(time() - tTrigger_) : 0.0f;
        xfRotateZ(prop[P_BEAKER].xf, a, suPivot_, suBeakerBase_);
    } else if (deney == Deney::Bal) {
        const float h = cfg.f("bal.yukseklik_en_az", 0.05f) + in.height * (cfg.f("bal.yukseklik_en_cok", 0.32f) - cfg.f("bal.yukseklik_en_az", 0.05f));
        translation(prop[P_DIPPER].xf, make_float3(in.nozzle.x, h, in.nozzle.z));
    } else if (deney == Deney::Kum) {
        const float h = cfg.f("kum.yukseklik_en_az", 0.10f) + in.height * (cfg.f("kum.yukseklik_en_cok", 0.30f) - cfg.f("kum.yukseklik_en_az", 0.10f));
        translation(prop[P_FUNNEL].xf, make_float3(in.nozzle.x, h, in.nozzle.z));
    }
}

void LabWorld::driveHeld(float dt) {
    for (Held& h : held_) {
        if (!(deney == Deney::Su && h.propId == P_BEAKER)) continue;   // only the pouring beaker moves
        launchTransform(h.dRest, h.count, prop[h.propId].xf, h.dTarget, pw.stream());   // ordered before the drive (same stream)
        pw.driveKinematic(h.dIdx, h.dTarget, h.count, dt);
    }
}

void LabWorld::setInputs(const LabInputs& i) {
    in = i;
    placeProps();
}

void LabWorld::trigger(const LabEvent& e) {
    ev_ = e;
    triggered_ = true;
    tTrigger_ = time();
    if (e.kind == LabEvent::Ball && ballGroup > 0) {
        // let go where asked, at rest
        std::vector<float3> x;
        pw.readPositions(x);
        float3 c = make_float3(0, 0, 0);
        for (int i = ballFirst; i < ballFirst + ballCount; ++i) c = c + x[size_t(i)];
        c = c * (1.0f / std::max(1, ballCount));
        pw.placeGroup(ballGroup, e.a - c, make_float3(0, 0, 0));
    } else if (e.kind == LabEvent::Drop && deney == Deney::Damla) {
        // a drop of radius r, a little above the milk, moving down at sqrt(2 g h): as if it fell from h
        const float r = cfg.f("damla.yaricap", 0.0025f);
        const float h = cfg.f("damla.yukseklik_en_az", 0.05f) + in.height * (cfg.f("damla.yukseklik_en_cok", 0.5f) - cfg.f("damla.yukseklik_en_az", 0.05f));
        const float v = std::sqrt(2.0f * pw.params.gravity * h);
        const float3 c = make_float3(e.a.x, milkTop_ + r + cfg.f("damla.aralik", 0.006f), e.a.z);
        std::vector<float3> pos, vel;
        const int k = int(std::ceil(r / s)) + 1;
        for (int z = -k; z <= k; ++z)
            for (int y = -k; y <= k; ++y)
                for (int x = -k; x <= k; ++x) {
                    const float3 p = make_float3(x * s, y * s, z * s);
                    if (length(p) <= r - 0.3f * s) {
                        pos.push_back(c + p);
                        vel.push_back(make_float3(0.0f, -v, 0.0f));
                    }
                }
        const int added = pw.appendFluid(pos, vel, cfg.f("damla.yogunluk", 1030.0f));
        code.insert(code.end(), size_t(added), DC_MILK);
        std::printf("damla: %d tanecik, %.1f mm, %.0f cm'den (%.2f m/s)\n", added, 2000.0f * r, h * 100.0f, v);
    } else if (e.kind == LabEvent::Prick && membraneFirst >= 0) {
        // a needle: the bonds of the membrane within a few millimetres of the point are cut (at its nearest membrane
        // particle: a recording's finer balloon sags a little differently). From now on a tear's tip runs: a bond
        // next to a broken one breaks sooner (the stress a coarse lattice cannot concentrate at the tip). Not before:
        // the few bonds the resting balloon loses to the solver's noise (2 of 31256) are not flaws a real one has.
        std::vector<float3> x;
        pw.readPositions(x);
        int near = membraneFirst;
        float best = 1e9f;
        for (int i = membraneFirst; i < membraneFirst + membraneCount; ++i) {
            const float d = length(x[size_t(i)] - e.a);
            if (d < best) { best = d; near = i; }
        }
        pw.breakBondsInSphere(x[size_t(near)], cfg.f("balon.igne", 0.004f));
        pw.params.tearTip = cfg.f("balon.yirtilma_ucu", 0.5f);
        std::printf("balon: igne (%.3f %.3f %.3f), en yakin zar taneciginden %.1f mm\n", e.a.x, e.a.y, e.a.z, best * 1000.0f);
    } else if (e.kind == LabEvent::Shot) {
        // a rifle bullet along the ray a -> b through the melon: its path (weapons.h, calibrated in gelatin) gives
        // each step's momentum, energy and cavity; the particles near the path take them
        using namespace weapons;
        const Load L = gunLoad(deney == Deney::Jel || e.gun == 0 ? Gun::Pistol : Gun::Rifle, e.power);
        std::vector<float3> x;
        pw.readPositions(x);
        const float3 d = normalize(e.b - e.a);
        // the medium along the ray: tissue where a melon particle is within a spacing of it
        std::unordered_map<long long, int> occ;
        const float cell = s;
        auto key = [&](int xx, int yy, int zz) { return (long long)(xx + 100000) * 40000000000LL + (long long)(yy + 100000) * 200000LL + (long long)(zz + 100000); };
        // every cell within a spacing of a melon particle (a lattice point's own cell alone leaves holes)
        for (size_t i = 0; i < x.size(); ++i)
            if (code[i] == DC_RIND || code[i] == DC_PITH || code[i] == DC_FLESH || code[i] == DC_GEL) {
                const int cx = int(std::floor(x[i].x / cell)), cy = int(std::floor(x[i].y / cell)), cz = int(std::floor(x[i].z / cell));
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) occ[key(cx + dx, cy + dy, cz + dz)] = 1;
            }
        const float maxLen = length(e.b - e.a) + 0.5f;
        const MediumAt medium = [&](float t) {
            const float3 p = e.a + d * t;
            return occ.count(key(int(std::floor(p.x / cell)), int(std::floor(p.y / cell)), int(std::floor(p.z / cell)))) ? Medium::Tissue : Medium::Air;
        };
        const Path path = traceShot(L.shot, medium, maxLen, 0.5f * s);
        std::vector<uint32_t> idx, cut;
        std::vector<float3> dv;
        std::vector<float> mass(x.size(), 0.0f);
        const float share = cfg.f(std::string(deneyKey(deney)) + ".bosluk_payi", 0.8f);
        for (const Segment& sg : path.segments) {
            if (sg.medium == Medium::Air || sg.s1 <= sg.s0) continue;
            const float3 a = e.a + d * sg.s0, b = e.a + d * sg.s1;
            const float rc = std::max(sg.cavityRadius, sg.crushRadius);
            const float r0 = std::max(sg.crushRadius, 0.5f * s);
            // the particles within the cavity of this step: radial flow (the temporary cavity), the step's momentum
            // to those on the path
            std::vector<int> onPath;
            for (size_t i = 0; i < x.size(); ++i) {
                if (code[i] != DC_RIND && code[i] != DC_PITH && code[i] != DC_FLESH && code[i] != DC_GEL) continue;
                const float3 w = x[i] - a;
                const float t = dot(w, d);
                if (t < 0.0f || t > sg.s1 - sg.s0) continue;
                const float3 rr = w - d * t;
                const float r = length(rr);
                if (r > rc + s) continue;
                if (r <= r0 + 0.5f * s) { onPath.push_back(int(i)); cut.push_back(uint32_t(i)); }
                const float vr = radialSpeed(sg, std::max(r, r0), r0, share);
                if (vr > 0.0f && r > 1e-6f) {
                    idx.push_back(uint32_t(i));
                    dv.push_back(rr * (vr / r));
                }
            }
            if (!onPath.empty()) {
                const float m = 1000.0f * s * s * s;
                const float dvAlong = sg.dp / (m * onPath.size());
                for (int i : onPath) {
                    idx.push_back(uint32_t(i));
                    dv.push_back(d * dvAlong);
                }
            }
        }
        pw.addVelocity(idx, dv);
        const int broken = pw.breakBondsOf(cut);
        std::printf("%s: %s, %.0f m/s; yol %d adim, dokuda %.1f cm, momentum %.2f N s birakti, %.0f J; %zu taneciğe hiz, %d bag koptu; %s\n", deneyKey(deney), L.name.c_str(),
                    L.shot.velocity, int(path.segments.size()), path.tissueLength * 100.0f, path.pDeposited, path.eDeposited, idx.size(), broken,
                    path.stopped ? "durdu" : "cikti");
    }
}

void LabWorld::emit(float dt) {
    if (deney != Deney::Bal && deney != Deney::Kum) return;
    if (!in.flowing) {   // stopped: what is in the nozzle falls
        const float density = deney == Deney::Bal ? cfg.f("bal.yogunluk", 1420.0f) : cfg.f("kum.yogunluk", 1600.0f);
        for (const HeldLayer& h : heldLayers_) pw.release(h.first, h.count, density);
        heldLayers_.clear();
        return;
    }
    const bool honey = deney == Deney::Bal;
    const std::string pre = honey ? "bal." : "kum.";
    const float Q = (cfg.f(pre + "akis_en_az", honey ? 0.5f : 3.0f) + in.flow * (cfg.f(pre + "akis_en_cok", honey ? 5.0f : 25.0f) - cfg.f(pre + "akis_en_az", honey ? 0.5f : 3.0f))) * 1e-6f;   // m^3/s
    const float r = cfg.f(pre + "ip_yaricap", honey ? 0.0032f : 0.0045f);
    const float area = kPi * r * r;
    const float v0 = std::max(Q / area, cfg.f(pre + "en_az_hiz", honey ? 0.05f : 0.3f));
    const float layer = 0.8660254f * s;            // a hexagonal disc's rows are this far apart: the layers too
    const float rate = v0 / layer;                 // layers per second
    emitCarry_ += rate * dt;
    // where the stream starts: under the dipper's head / the funnel's neck
    const float3 o = xfApply(prop[honey ? P_DIPPER : P_FUNNEL].xf, make_float3(0.0f, honey ? -0.004f : -0.002f, 0.0f));
    std::vector<float3> pos, vel;
    // the flow a stream this thin carries at v0 (a disc of the lattice holds fewer particles than its area suggests)
    const std::vector<float2> d0 = disc(std::max(r, 0.6f * s), s, 0.0f);
    const float want = Q / (v0 * s * s * s / layer);   // particles a layer should hold for the flow
    const float keep = std::min(1.0f, want / float(d0.size()));
    while (emitCarry_ >= 1.0) {
        emitCarry_ -= 1.0;
        const float age = float(emitCarry_) / rate;   // how long ago (in this step) the layer left the nozzle
        const std::vector<float2> dd = disc(std::max(r, 0.6f * s), s, 0.7f * float(emitSeed_));
        for (size_t qi = 0; qi < dd.size(); ++qi) {
            const float2 q = dd[qi];
            // thinned to the flow (by the point's own number: a rejected point must not change the next one's draw)
            if (keep < 1.0f && hashF(emitSeed_ * 977u + unsigned(qi) * 31u) > keep) continue;
            pos.push_back(o + make_float3(q.x, -v0 * age, q.y));
            vel.push_back(make_float3(0, -v0, 0));
        }
        ++emitSeed_;
    }
    if (std::getenv("LAB_TANI") && emitSeed_ < 40)
        std::printf("    akit: Q %.2f mL/s, v0 %.3f m/s, %.1f katman/s, disk %zu, pay %.2f, bu adim %zu tanecik\n", Q * 1e6, v0, rate, d0.size(), keep, pos.size());
    const float density = honey ? cfg.f("bal.yogunluk", 1420.0f) : cfg.f("kum.yogunluk", 1600.0f);
    // the layers that have left the nozzle (held for a few layers' travel) are let go
    const double hold = cfg.f(pre + "tutma", 3.0f) * layer / v0;
    const double now = time();
    size_t k = 0;
    while (k < heldLayers_.size() && now - heldLayers_[k].born > hold) {
        pw.release(heldLayers_[k].first, heldLayers_[k].count, density);
        ++k;
    }
    heldLayers_.erase(heldLayers_.begin(), heldLayers_.begin() + long(k));
    if (pos.empty()) return;
    const int first = pw.count();
    const int added = pw.appendHeld(pos, vel, density, honey ? 0 : sandGroup_);
    if (added > 0) heldLayers_.push_back({first, added, now});
    if (honey && added > 0 && pw.springCount() >= 0 && cfg.i("bal.yaylar", 1)) {
        // springs: every new particle to its neighbours within 1.6 spacings, among the new ones and the layer before
        // (the thread's lattice: it stretches and flows, it does not fall apart into drops)
        std::vector<float3> cand = lastLayerPos_;
        const int candFirst = lastLayerFirst_;
        std::vector<int2> pairs;
        const float reach = cfg.f("bal.yay_uzaklik", 1.6f) * s;
        for (int i = 0; i < added; ++i) {
            for (int j = i + 1; j < added; ++j)
                if (length(pos[size_t(i)] - pos[size_t(j)]) < reach) pairs.push_back(make_int2(first + i, first + j));
            if (candFirst >= 0)
                for (size_t j = 0; j < cand.size(); ++j)
                    if (length(pos[size_t(i)] - (cand[j] + make_float3(0, -v0 * float(1.0 / rate), 0))) < reach * 1.2f) pairs.push_back(make_int2(first + i, candFirst + int(j)));
        }
        pw.addSprings(pairs);
        lastLayerPos_.assign(pos.end() - added, pos.end());
        lastLayerFirst_ = first + added - int(lastLayerPos_.size());
        // only the newest layer's particles: they are the last of pos
        const int perLayer = int(std::max<size_t>(1, d0.size()));
        if (int(lastLayerPos_.size()) > perLayer) {
            lastLayerPos_.erase(lastLayerPos_.begin(), lastLayerPos_.end() - perLayer);
            lastLayerFirst_ = first + added - perLayer;
        }
    }
    code.insert(code.end(), size_t(added), honey ? DC_HONEY : DC_SAND);
}

void LabWorld::step(float dt) {
    if (triggered_ || deney == Deney::Bal || deney == Deney::Kum) placeProps();
    // the props at the end of this step
    if (deney == Deney::Su && triggered_) {
        const float a = suAngle(time() + dt - tTrigger_);
        xfRotateZ(prop[P_BEAKER].xf, a, suPivot_, suBeakerBase_);
        driveHeld(dt);
    }
    emit(dt);
    pw.step(dt);
}

void LabWorld::exportDraw(float3* dPos, uint8_t* dCode, cudaStream_t st) {
    const int n = pw.count();
    if (n > dCap_) {
        if (dBase_) cudaFree(dBase_);
        if (dFluid_) cudaFree(dFluid_);
        dCap_ = std::max(n, int(code.size())) + 65536;
        cudaMalloc(reinterpret_cast<void**>(&dBase_), size_t(dCap_));
        cudaMalloc(reinterpret_cast<void**>(&dFluid_), size_t(dCap_) * sizeof(float));
        cudaMemcpyAsync(dBase_, code.data(), code.size(), cudaMemcpyHostToDevice, st);
        baseUploaded_ = int(code.size());
    } else if (baseUploaded_ < int(code.size())) {
        cudaMemcpyAsync(dBase_ + baseUploaded_, code.data() + baseUploaded_, code.size() - size_t(baseUploaded_), cudaMemcpyHostToDevice, st);
        baseUploaded_ = int(code.size());
    }
    pw.exportPositions(dPos, st);   // step() has finished the solver's own stream
    pw.exportDisplay(dFluid_, st);
    launchCodes(dBase_, dFluid_, n, dCode, st);
}

std::string LabWorld::summary() const {
    int counts[kDrawCodes] = {};
    for (uint8_t c : code) counts[c]++;
    char b[256];
    std::snprintf(b, sizeof(b), "%s %s: aralik %.2f mm, %d tanecik (cam/kaide %d, su %d, bal %d, kum %d, lastik %d, kabuk %d + %d, et %d, celik %d, jel %d, sut %d)",
                  deneyKey(deney), fine ? "ince" : "kaba", s * 1000.0f, pw.count(), counts[DC_NONE], counts[DC_WATER], counts[DC_HONEY], counts[DC_SAND],
                  counts[DC_RUBBER], counts[DC_RIND], counts[DC_PITH], counts[DC_FLESH], counts[DC_STEEL], counts[DC_GEL], counts[DC_MILK]);
    return b;
}
