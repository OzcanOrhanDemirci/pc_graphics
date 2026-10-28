// Scene 12 "Ateş": the run (ates_app.h): the world, the fire's wood, a frame's simulation and picture.
#include "scenes/ates/ates_app.h"

#include "core/image_io.h"
#include "scenes/ates/ates_spectra.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace {

double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
unsigned int hashU(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float rnd(unsigned int& s) {
    s = hashU(s + 0x9e3779b9u);
    return float(s >> 8) / 16777216.0f;
}
// a smooth bump of the ground away from the pit (value noise, two octaves; the pit itself level)
float groundHeight(float x, float z) {
    const float r = std::sqrt(x * x + z * z);
    if (r < 0.7f) return 0.0f;
    auto vn = [](float x, float z, float f, unsigned int seed) {
        x *= f;
        z *= f;
        const int i = int(std::floor(x)), j = int(std::floor(z));
        const float fx = x - i, fz = z - j;
        auto h = [&](int a, int b) { return float(hashU(unsigned(a) * 73856093u ^ unsigned(b) * 19349663u ^ seed) >> 8) / 16777216.0f; };
        const float sx = fx * fx * (3 - 2 * fx), sz = fz * fz * (3 - 2 * fz);
        return (h(i, j) * (1 - sx) + h(i + 1, j) * sx) * (1 - sz) + (h(i, j + 1) * (1 - sx) + h(i + 1, j + 1) * sx) * sz;
    };
    const float n = 0.08f * (vn(x, z, 0.35f, 7u) - 0.5f) + 0.025f * (vn(x, z, 1.3f, 11u) - 0.5f);
    return n * std::min(1.0f, (r - 0.7f) / 1.5f);
}
float4 quatAxisAngle(float3 axis, float a) {
    axis = normalize(axis);
    const float s = std::sin(0.5f * a);
    return make_float4(std::cos(0.5f * a), axis.x * s, axis.y * s, axis.z * s);
}
// the rotation taking x to `dir` (the logs' axis), then turned about it by `roll`
float4 quatFromX(float3 dir, float roll) {
    dir = normalize(dir);
    const float3 x = make_float3(1, 0, 0);
    const float c = dot(x, dir);
    float4 q;
    if (c > 0.9999f) q = make_float4(1, 0, 0, 0);
    else if (c < -0.9999f) q = make_float4(0, 0, 1, 0);
    else {
        const float3 ax = normalize(cross(x, dir));
        q = quatAxisAngle(ax, std::acos(c));
    }
    const float4 r = quatAxisAngle(make_float3(1, 0, 0), roll);
    // q * r
    return make_float4(q.x * r.x - q.y * r.y - q.z * r.z - q.w * r.w, q.x * r.y + q.y * r.x + q.z * r.w - q.w * r.z,
                       q.x * r.z - q.y * r.w + q.z * r.x + q.w * r.y, q.x * r.w + q.y * r.z - q.z * r.y + q.w * r.x);
}

}   // namespace

void AtesApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/ates.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    width = cfg.i("width", width);
    height = cfg.i("height", height);
    timeScale = cfg.f("time.scale", 1.0f);
    layout = cfg.values.count("layout") ? cfg.values.at("layout") : std::string("ates");
}

void AtesApp::loadAssets() {
    const double t0 = nowMs();
    assets.release();
    assets.root = root + "/assets/ates/cc0";
    assets.loadModel("rock_07", true);
    assets.loadModel("namaqualand_stones_01", true);
    groundMat_ = assets.loadMaterial("burned_ground_01");
    groundMat2_ = assets.loadMaterial("brown_mud_dry");
    barkMat_ = assets.loadMaterial("bark_brown_02");
    OMaterial endM{};
    endM.tint = make_float3(0.52f, 0.40f, 0.27f);   // fresh oak's end grain (sRGB ~ 180 140 100, linear)
    endM.roughness = 0.75f;
    endMat_ = assets.addMaterial(endM, "end_grain");
    if (barkMat_ < 0) {
        OMaterial b{};
        b.tint = make_float3(0.12f, 0.09f, 0.07f);
        b.roughness = 0.9f;
        barkMat_ = assets.addMaterial(b, "bark");
    }
    if (groundMat_ < 0) {
        OMaterial g{};
        g.tint = make_float3(0.06f, 0.055f, 0.05f);
        g.roughness = 0.95f;
        groundMat_ = assets.addMaterial(g, "ground");
    }
    for (const auto& id : assets.missing) std::fprintf(stderr, "ates: eksik varlik %s (python assets/ates/fetch.py)\n", id.c_str());
    loadMs = nowMs() - t0;
}

void AtesApp::buildWorld() {
    // the ground: a square of 30 m, 10 cm a vertex, the pit level, the burnt texture in it, the mud beyond (the shader
    // blends them by distance)
    OProtoCPU ground;
    ground.asset = "ground";
    OMeshCPU m;
    const float half = 15.0f, step = 0.1f;
    const int n = int(2 * half / step) + 1;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float x = -half + i * step, z = -half + j * step;
            m.pos.push_back(make_float3(x, groundHeight(x, z), z));
            const float e = 0.05f;
            const float dx = (groundHeight(x + e, z) - groundHeight(x - e, z)) / (2 * e), dz = (groundHeight(x, z + e) - groundHeight(x, z - e)) / (2 * e);
            m.nrm.push_back(normalize(make_float3(-dx, 1.0f, -dz)));
            m.uv.push_back(make_float2(x, z));
        }
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            const unsigned a = unsigned(j * n + i), b = a + 1, c = a + unsigned(n), d = c + 1;
            m.idx.push_back(make_uint3(a, c, b));
            m.idx.push_back(make_uint3(b, c, d));
        }
    m.material = groundMat_;
    m.uvScale = 1.0f / cfg.f("ground.tile", 2.0f);
    ground.geoms.push_back(m);
    std::vector<OProtoCPU> extra{ground};
    const int groundProto = int(assets.protos.size());
    std::vector<APlace> places;
    APlace gp;
    gp.proto = groundProto;
    places.push_back(gp);
    // the pit: a ring of stones, each a copy of the scanned rock turned and sized, sunk a little into the ground
    unsigned int seed = 12u;
    bodies.stones.clear();
    auto it = assets.byAsset.find("rock_07");
    if (it != assets.byAsset.end() && !it->second.empty()) {
        const int proto = it->second.front();
        const OProtoCPU& P = assets.protos[size_t(proto)];
        const float3 ext = P.bmax - P.bmin;
        const int count = cfg.i("pit.stones", 12);
        const float ringR = cfg.f("pit.radius", 0.56f);
        for (int s = 0; s < count; ++s) {
            const float a = 6.2831853f * (s + 0.3f * (rnd(seed) - 0.5f)) / count;
            const float want = 0.22f + 0.06f * rnd(seed);   // the stone's longest side across (m)
            const float sc = want / std::max(ext.x, std::max(ext.y, ext.z));
            const float yaw = 6.2831853f * rnd(seed);
            // lying: the scan's longest side round the ring
            const float cy = std::cos(yaw), sy = std::sin(yaw);
            APlace pl;
            pl.proto = proto;
            const float r = ringR + 0.03f * (rnd(seed) - 0.5f);
            const float px = r * std::cos(a), pz = r * std::sin(a);
            const float py = -P.bmin.y * sc - 0.03f;
            const float mm[12] = {cy * sc, 0, sy * sc, px, 0, sc, 0, py, -sy * sc, 0, cy * sc, pz};
            std::copy(mm, mm + 12, pl.m);
            const float t = 0.85f + 0.3f * rnd(seed);
            pl.tint = make_float3(t, t * 0.98f, t * 0.95f);
            places.push_back(pl);
            // for the bodies: the stone as a capsule along its longer side on the ground, as thick as its lesser
            const float3 c = make_float3(px, py, pz) + make_float3(cy * sc * 0.5f * (P.bmin.x + P.bmax.x) + sy * sc * 0.5f * (P.bmin.z + P.bmax.z),
                                                                 sc * 0.5f * (P.bmin.y + P.bmax.y),
                                                                 -sy * sc * 0.5f * (P.bmin.x + P.bmax.x) + cy * sc * 0.5f * (P.bmin.z + P.bmax.z));
            const bool alongX = ext.x >= ext.z;
            const float lng = sc * (alongX ? ext.x : ext.z), thick = sc * std::min(ext.y, alongX ? ext.z : ext.x);
            const float3 dir = alongX ? make_float3(cy, 0, -sy) : make_float3(sy, 0, cy);
            ABodyObstacle o;
            o.r = 0.5f * thick;
            o.a = c - dir * std::max(0.5f * lng - o.r, 0.0f);
            o.b = c + dir * std::max(0.5f * lng - o.r, 0.0f);
            bodies.stones.push_back(o);
        }
    }
    // a few stones scattered on the ground beyond
    auto it2 = assets.byAsset.find("namaqualand_stones_01");
    if (it2 != assets.byAsset.end() && cfg.i("scatter", 0))
        for (int s = 0; s < 3; ++s)
            for (int proto : it2->second) {
                APlace pl;
                pl.proto = proto;
                const float a = 1.3f + 2.1f * s, r = 1.6f + 0.9f * s, yaw = 0.7f * s;
                const float cy = std::cos(yaw), sy = std::sin(yaw);
                const float x = r * std::cos(a), z = r * std::sin(a);
                const float mm[12] = {cy, 0, sy, x, 0, 1, 0, groundHeight(x, z) - 0.01f, -sy, 0, cy, z};
                std::copy(mm, mm + 12, pl.m);
                places.push_back(pl);
            }
    R.uploadStatic(assets, extra, places);
}

bool AtesApp::initRenderer(std::string& err) {
    if (!R.init(width, height, root)) {
        err = "renderer";
        return false;
    }
    std::string log;
    if (!R.compile(log)) {
        err = log;
        return false;
    }
    buildWorld();
    R.uploadMaterials(assets.materials);
    R.params.groundMaterial = groundMat_;
    R.params.groundMaterial2 = groundMat2_;
    R.params.pitRadius = cfg.f("pit.burnt", 0.9f);
    // the moonless night: the sky's luminance (cd/m^2), the faint light on the ground
    R.params.skyZenith = cfg.v3("sky.zenith", make_float3(0.0004f, 0.0006f, 0.0011f));
    R.params.skyHorizon = cfg.v3("sky.horizon", make_float3(0.0016f, 0.0017f, 0.0020f));
    R.params.ambient = cfg.f("sky.ambient", 0.002f);
    R.params.march = cfg.f("march", 0.0035f);
    R.params.chemEmit = ates_spectra::chemiluminescence(cfg.f("chem.eta", 3.4e-7f));
    R.params.hazeScale = cfg.f("haze", 1.0f);
    R.params.bloomStrength = cfg.f("bloom", 0.02f);
    R.params.vignette = cfg.f("vignette", 0.25f);
    R.params.saturation = cfg.f("saturation", 1.2f);
    R.params.contrast = cfg.f("contrast", 1.5f);
    R.params.debug = cfg.i("debug", 0);
    return true;
}

void AtesApp::initSim() {
    const int nx = cfg.i("gas.nx", 128), ny = cfg.i("gas.ny", 192), nz = cfg.i("gas.nz", 128);
    const float h = cfg.f("gas.h", 0.0075f);
    gas.vcycles = cfg.i("gas.vcycles", 2);
    gas.k.wallMix = cfg.f("gas.wallMix", gas.k.wallMix);
    gas.init(nx, ny, nz, h, make_float3(-0.5f * nx * h, 0.0f, -0.5f * nz * h), R.stream());
    wood.init(unsigned(cfg.i("wood.patches", 40000)), R.stream());
    wood.k.filmT0 = cfg.f("wood.filmT0", wood.k.filmT0);
    wood.k.filmT1 = cfg.f("wood.filmT1", wood.k.filmT1);
    wood.k.flameH = cfg.f("wood.flameH", wood.k.flameH);
    bodies.ground = groundHeight;
    bodies.substeps = cfg.i("body.substeps", 16);
    sparks.init(unsigned(cfg.i("spark.max", 8192)), R.stream());
    sparks.kinA = cfg.f("spark.kinA", sparks.kinA);
    R.overlay = [this](cudaStream_t st) {
        const AtesParams& p = R.params;
        sparks.draw(p.hdr, p.depth, width, height, p.eye, p.U, p.V, p.W, p.pixelAngle, p.greyEmit, st);
    };
    simTime = woodTime = 0.0;
    lay(layout);
}

void AtesApp::placeLog(ALog& L, float3 at, float3 axis) {
    L.pos = at;
    L.q = quatFromX(axis, 0.0f);
}

void AtesApp::lay(const std::string& name) {
    wood.init(wood.capacity ? wood.capacity : 40000u, R.stream());
    gas.reset();
    sparks.clear();
    lastVel_.clear();
    gas.burner = AGasBurner();
    unsigned int seed = 99u;
    bool settle = true;   // laid down onto what is under it (the bodies settle it the rest of the way)
    auto log = [&](float r, float len, float3 at, float3 axis, float burnt, float Ts, int kind = 1) {
        ALog L;
        L.kind = kind;
        L.r0 = r;
        L.len = len;
        L.nTheta = kind == 0 ? 6 : std::max(12, int(std::round(2 * 3.14159f * r / 0.012f)));
        L.nZ = kind == 0 ? 10 : std::max(8, int(std::round(len / 0.0125f)));
        placeLog(L, at, axis);
        L.q = quatFromX(axis, 6.2831853f * rnd(seed));
        L.radius = L.r0;
        if (settle) bodies.lower(wood.logs, L);
        wood.add(L, burnt, Ts);
    };
    if (name == "brulor") {
        // the test burner alone (F1): the fire science's flame
        gas.burner.centre = make_float3(0, 0, 0);
        gas.burner.radius = 0.5f * cfg.f("test.D", 0.4f);
        gas.burner.power = cfg.f("test.Q", 20.0f) * 1000.0f;
        gas.burner.temp = 450.0f;
    } else if (name == "soguk") {
        // laid and not lit: a bundle of twigs under a tent of kindling, two logs across
        for (int i = 0; i < 14; ++i) {
            const float a = 6.2831853f * rnd(seed), r = 0.06f * rnd(seed);
            log(0.003f + 0.002f * rnd(seed), 0.22f + 0.08f * rnd(seed), make_float3(r * std::cos(a), 0.012f + 0.03f * rnd(seed), r * std::sin(a)),
                make_float3(std::cos(a * 3), 0.15f * (rnd(seed) - 0.5f), std::sin(a * 3)), 0.0f, 288.0f, 0);
        }
        for (int i = 0; i < 6; ++i) {
            const float a = 6.2831853f * i / 6;
            const float3 foot = make_float3(0.13f * std::cos(a), 0.012f, 0.13f * std::sin(a));
            const float3 axis = normalize(make_float3(-std::cos(a) * 0.55f, 1.0f, -std::sin(a) * 0.55f));
            log(0.011f, 0.3f, foot + axis * 0.15f, axis, 0.0f, 288.0f);
        }
        log(0.045f, 0.42f, make_float3(0, 0.045f, -0.2f), make_float3(1, 0, 0), 0.0f, 288.0f);
        log(0.045f, 0.42f, make_float3(0, 0.045f, 0.2f), make_float3(1, 0, 0), 0.0f, 288.0f);
    } else {
        // burning (the default, "ates") or its embers ("kor"): a bed of charred chunks, glowing; over it the logs,
        // charred a few millimetres and hot where the fire has had them
        const bool embers = name == "kor";
        {
            // the bed: each coal set down where it falls (on the ground or the coals before it); packed, they keep each
            // other hot by their radiation, as a bed of coals does
            // (chunks about as long as they are thick, as a log breaks up into: sticks a few times longer stacked into a
            // tower where they crossed, which toppled and threw them out of the pit)
            const int chunks = cfg.i("bed.chunks", 40);
            const float reach = cfg.f("bed.reach", 0.13f);
            for (int i = 0; i < chunks; ++i) {
                const float a = 6.2831853f * rnd(seed), r = reach * std::sqrt(rnd(seed));
                const float rr = 0.01f + 0.01f * rnd(seed);
                log(rr, rr * (2.0f + 1.5f * rnd(seed)), make_float3(r * std::cos(a), rr + 0.02f, r * std::sin(a)),
                    make_float3(std::cos(a + 1.3f), 0.2f * (rnd(seed) - 0.5f), std::sin(a + 1.3f)), rr * 0.95f, 1050.0f + 100.0f * rnd(seed));
            }
        }
        if (!embers) {
            // the logs, let down onto the bed and the third across them (laid after it: set down first, the coals
            // that came after rose over them and rolled off, out of the fire)
            const float over = 0.45f;
            const float apart = cfg.f("bed.logApart", 0.15f);
            log(0.042f, 0.40f, make_float3(-0.02f, over, -apart), make_float3(1, 0, 0.12f), 0.006f, 900.0f);
            log(0.038f, 0.38f, make_float3(0.03f, over, apart), make_float3(1, 0, -0.1f), 0.006f, 900.0f);
            log(0.040f, 0.42f, make_float3(0.0f, over + 0.12f, 0.0f), make_float3(0.15f, 0.0f, 1.0f), 0.005f, 880.0f);
            // the kindling that lit them has burnt down: its last seconds stand in as a small burner under the logs
            // (the loading's first seconds; then the logs carry on, or not, by themselves)
            starterLeft_ = cfg.f("starter.seconds", 8.0f);
        }
    }
    patchRad_.clear();
    prevPoses_.clear();
    syncLogs(true);
}

void AtesApp::syncLogs(bool rebuild) {
    if (rebuild || patchRad_.size() != wood.patches) {
        wood.readBack();
        // a log burnt thinner lets what lies on it down: the sleeping bodies wake
        bool thinner = false;
        for (ALog& L : wood.logs)
            if (L.alive && L.wakeRadius - L.radius > 0.0005f) thinner = true;
        if (thinner) {
            bodies.wakeAll(wood.logs);
            for (ALog& L : wood.logs) L.wakeRadius = L.radius;
        }
        patchRad_.assign(wood.patches, 0.0f);
        if (wood.patches) cudaMemcpy(patchRad_.data(), wood.radOut, wood.patches * sizeof(float), cudaMemcpyDeviceToHost);
        std::vector<ALogMesh> meshes(wood.logs.size());
        for (size_t i = 0; i < wood.logs.size(); ++i) buildLogMesh(wood.logs[i], patchRad_, meshes[i]);
        R.setLogs(meshes, {}, barkMat_, endMat_);
        // (the materials list may have grown: the end grain's)
        R.uploadMaterials(assets.materials);
        prevPoses_.clear();
    }
    placeAndView();
}

void AtesApp::placeAndView() {
    std::vector<std::array<float, 12>> poses;
    for (const ALog& L : wood.logs) poses.push_back(L.pose());
    if (prevPoses_.size() != poses.size()) prevPoses_ = poses;
    std::vector<ALogView> v = wood.views();
    bool moved = false;
    for (size_t i = 0; i < v.size(); ++i) {
        std::copy(prevPoses_[i].begin(), prevPoses_[i].end(), v[i].prevPose);
        if (poses[i] != prevPoses_[i]) moved = true;
    }
    if (moved || !R.params.handle) R.placeLogs(poses);
    R.setLogViews(v);
    prevPoses_ = poses;
}

void AtesApp::strikeMatch() {
    // where the look meets the pit's floor (a match is held to the kindling at its foot)
    const float3 e = eye(), f = forward();
    float3 at = make_float3(0, 0.04f, 0);
    if (f.y < -0.05f) {
        const float t = (0.04f - e.y) / f.y;
        at = e + f * t;
    }
    const float r = std::sqrt(at.x * at.x + at.z * at.z);
    if (r > 0.35f) at = at * (0.35f / r);
    at.y = 0.04f;
    matchAt = at;
    matchLeft = cfg.f("match.seconds", 18.0f);
}

void AtesApp::addFuel(int kind) {
    // on the pile, where the look points, dropped from a little above (the bodies settle it: ates_body)
    const float3 e = eye(), f = forward();
    float3 at = make_float3(0, 0.1f, 0);
    if (f.y < -0.05f) {
        const float t = (0.1f - e.y) / f.y;
        at = e + f * t;
    }
    const float r = std::sqrt(at.x * at.x + at.z * at.z);
    if (r > 0.25f) at = at * (0.25f / r);
    unsigned int seed = unsigned(wood.logs.size() * 7919u + 3u);
    ALog L;
    L.kind = kind;
    if (kind == 0) {
        for (int i = 0; i < 8; ++i) {
            ALog t;
            t.kind = 0;
            t.r0 = 0.003f + 0.002f * rnd(seed);
            t.len = 0.2f + 0.1f * rnd(seed);
            t.nTheta = 6;
            t.nZ = 10;
            const float a = 6.2831853f * rnd(seed);
            t.pos = at + make_float3(0.03f * (rnd(seed) - 0.5f), 0.12f + 0.01f * i, 0.03f * (rnd(seed) - 0.5f));
            t.q = quatFromX(make_float3(std::cos(a), 0.1f * (rnd(seed) - 0.5f), std::sin(a)), 0.0f);
            wood.add(t);
        }
    } else {
        L.r0 = 0.035f + 0.015f * rnd(seed);
        L.len = 0.36f + 0.08f * rnd(seed);
        L.nTheta = std::max(12, int(std::round(2 * 3.14159f * L.r0 / 0.012f)));
        L.nZ = std::max(8, int(std::round(L.len / 0.0125f)));
        const float a = std::atan2(f.z, f.x) + 1.5708f;
        L.pos = at + make_float3(0, 0.15f, 0);
        L.q = quatFromX(make_float3(std::cos(a), 0, std::sin(a)), 6.2831853f * rnd(seed));
        wood.add(L);
    }
    syncLogs(true);
}

void AtesApp::update(float dt) {
    const float dtGas = cfg.f("gas.dt", 1.0f / 120.0f);
    // the logs fall and settle in the clock's time
    bodies.step(wood.logs, std::min(dt, 0.05f));
    landingSparks();
    gasAcc_ += dt;
    const int maxSteps = cfg.i("gas.maxSteps", 3);
    // the hand: a match's flame (a small burner at its head), the breath along the look
    if (matchLeft > 0.0f) {
        gas.burner.point = true;
        gas.burner.centre = matchAt;
        gas.burner.radius = 0.012f;
        gas.burner.power = cfg.f("match.power", 80.0f);
        gas.burner.temp = 1300.0f;
        matchLeft -= dt;
    } else if (starterLeft_ > 0.0f) {
        gas.burner.point = false;
        gas.burner.centre = make_float3(0, 0, 0);
        gas.burner.radius = 0.12f;
        gas.burner.power = cfg.f("starter.power", 12000.0f) * std::min(1.0f, starterLeft_ / 2.0f);
        gas.burner.temp = 900.0f;
        starterLeft_ -= dt;
    } else if (layout != "brulor") {
        gas.burner.radius = 0.0f;
    }
    gas.jet.on = false;
    if (blowing) {
        // one leans in to blow: the mouth a little before what the look meets (the first log or coal, else the
        // ground), along the look; looking past the pile, a little before the grid
        const float3 e = eye(), f = forward();
        const float3 lo = gas.origin, hi = gas.origin + make_float3(gas.nx * gas.h, gas.ny * gas.h, gas.nz * gas.h);
        const float3 inv = make_float3(1.0f / f.x, 1.0f / f.y, 1.0f / f.z);
        const float3 a = (lo - e) * inv, b = (hi - e) * inv;
        const float t0 = std::max(std::max(std::min(a.x, b.x), std::min(a.y, b.y)), std::max(std::min(a.z, b.z), 0.0f));
        const float t1 = std::min(std::min(std::max(a.x, b.x), std::max(a.y, b.y)), std::max(a.z, b.z));
        float hit = f.y < -1e-3f ? -e.y / f.y : 1e9f;
        for (const ALog& L : wood.logs) {
            if (!L.alive) continue;
            // the ray against the log's cylinder: its distance from the axis is the radius, within its length
            const std::array<float, 12> P = L.pose();
            const float3 ax = make_float3(P[0], P[4], P[8]), o = e - L.pos;
            const float3 dp = f - ax * dot(f, ax), op = o - ax * dot(o, ax);
            const float A = dot(dp, dp), B = dot(op, dp), C = dot(op, op) - L.radius * L.radius;
            const float disc = B * B - A * C;
            if (A < 1e-9f || disc < 0.0f) continue;
            const float t = (-B - std::sqrt(disc)) / A;
            if (t > 0.0f && t < hit && std::fabs(dot(o + f * t, ax)) <= 0.5f * L.len) hit = t;
        }
        if (t1 > t0) {
            gas.jet.on = true;
            const float lean = cfg.f("blow.lean", 0.3f);
            gas.jet.origin = e + f * std::max(0.0f, hit < t1 ? hit - lean : t0 - lean);
            gas.jet.dir = f;
            gas.jet.speed = cfg.f("blow.speed", 9.0f);
            gas.jet.radius = cfg.f("blow.radius", 0.012f);
            gas.jet.length = cfg.f("blow.length", 1.2f);
        }
    }
    cost = FrameCost{};
    const int steps = std::min(int(gasAcc_ / dtGas), maxSteps);
    if (steps > 0) {
        // the wood once a frame, over all its gas steps: it gives the gas rates (kg/s, W per cell) that hold through
        // them, and its own time is seconds (a few gas steps of coupling lag are nothing to it)
        gas.clearSources();
        wood.markSolids(gas);
        wood.sparks = sparks.sink(cfg.f("spark.popPerKg", 1500.0f), cfg.f("spark.flakePerKg", 25000.0f));
        wood.step(steps * dtGas * timeScale, timeScale, gas);
        cost.woodMs = wood.stats.stepMs;
        for (int s = 0; s < steps; ++s) {
            gas.step(dtGas);
            cost.gasMs += gas.stats.stepMs;
        }
        gasAcc_ -= steps * dtGas;
        simTime += steps * dtGas;
        woodTime += steps * dtGas * timeScale;
    }
    cost.steps = steps;
    sparks.step(dt, gas);
    if (gasAcc_ > dtGas) gasAcc_ = 0.0f;   // behind: the gas runs slower than the clock rather than catching up in bursts
    gas.pack(R.params.sootEmit);
    wood.emberLight(gas, const_cast<float3*>(gas.lightIntensity()), gas.lnx, gas.lny, gas.lnz, gas.h * gas.lightDiv, R.params.greyEmit);
    R.setGas(gas.texture(), gas.bricks(), gas.origin, gas.nx, gas.ny, gas.nz, gas.h, gas.lightIntensity(), gas.lnx, gas.lny, gas.lnz, gas.h * gas.lightDiv);
    // the logs' shapes follow their burning (every half second of the clock)
    meshAge_ += dt;
    if (meshAge_ > 0.5f) {
        meshAge_ = 0.0f;
        syncLogs(true);
    } else placeAndView();
}

void AtesApp::landingSparks() {
    // wood landing on the embers knocks fragments out of them: a burst as large as the energy the landing took
    // (a share of it into hundreds of sparks), from under the log, as hot as the embers it struck
    if (lastVel_.size() != wood.logs.size()) lastVel_.assign(wood.logs.size(), make_float3(0, 0, 0));
    std::vector<ASparkSeed> seeds;
    unsigned int seed = unsigned(frame_ * 2654435761u + 17u);
    for (size_t i = 0; i < wood.logs.size(); ++i) {
        const ALog& L = wood.logs[i];
        const float3 v0 = lastVel_[i];
        lastVel_[i] = L.vel;
        const float lost = 0.5f * L.mass * (dot(v0, v0) - dot(L.vel, L.vel));
        if (!L.alive || lost < 0.05f || length(v0) < 0.5f || L.pos.y > 0.35f || wood.stats.hottest < 850.0f) continue;
        const int n = std::min(int(cfg.f("spark.perJoule", 300.0f) * lost), 400);
        const float3 axis = make_float3(1 - 2 * (L.q.z * L.q.z + L.q.w * L.q.w), 2 * (L.q.y * L.q.z + L.q.x * L.q.w), 2 * (L.q.y * L.q.w - L.q.x * L.q.z));
        for (int k = 0; k < n; ++k) {
            ASparkSeed s;
            const float along = (rnd(seed) - 0.5f) * 0.8f * L.len;
            s.p = L.pos + axis * along + make_float3(0.03f * (rnd(seed) - 0.5f), -0.6f * L.radius, 0.03f * (rnd(seed) - 0.5f));
            const float a = 6.2831853f * rnd(seed), up = 1.0f + 2.5f * rnd(seed), side = 1.2f * rnd(seed);
            s.v = make_float3(side * std::cos(a), up, side * std::sin(a));
            s.d = 5.0e-4f * std::pow(5.0f, rnd(seed));
            s.t = s.d * (0.15f + 0.25f * rnd(seed));
            s.T = wood.stats.hottest - 80.0f * rnd(seed);
            seeds.push_back(s);
        }
    }
    sparks.burst(seeds);
}

float3 AtesApp::forward() const {
    return make_float3(-std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), -std::cos(cam.yaw) * std::cos(cam.pitch));
}
float3 AtesApp::eye() const { return cam.feet + make_float3(0.0f, cam.eyeHeight, 0.0f); }

void AtesApp::render(float dt) {
    AtesParams& p = R.params;
    const float3 f = forward();
    const float3 rt = normalize(cross(f, make_float3(0, 1, 0)));
    const float3 up = cross(rt, f);
    const float th = std::tan(0.5f * cam.fovY * 0.0174533f);
    const float aspect = float(width) / float(height);
    p.eye = eye();
    p.W = f;
    p.U = rt * (th * aspect);
    p.V = up * th;
    p.pixelAngle = 2.0f * th / float(height);
    p.exposure = std::exp2(cfg.f("exposure", 0.0f) + exposureEv) * (cfg.f("exposure.fixed", 0.0f) > 0.0f ? cfg.f("exposure.fixed", 0.0f) : autoOn ? autoExposure : 1.0f);
    p.time = float(simTime);
    if (R.dlssOn() && p.dlss) {
        const unsigned int ph = frame_++ % ATES_PHASES;
        auto halton = [](unsigned int i, unsigned int b) {
            float fr = 1.0f, r = 0.0f;
            for (; i > 0; i /= b) {
                fr /= float(b);
                r += fr * float(i % b);
            }
            return r;
        };
        p.jitter = make_float2(halton(ph + 1, 2) - 0.5f, halton(ph + 1, 3) - 0.5f);
        p.phase = ph;
        const bool jump = !havePrev_ || length(p.eye - prevEye_) > 0.5f || dot(p.W, prevW_) < 0.9f;
        if (jump) {
            prevEye_ = p.eye;
            prevU_ = p.U;
            prevV_ = p.V;
            prevW_ = p.W;
        }
        p.prevEye = prevEye_;
        p.prevU = prevU_;
        p.prevV = prevV_;
        p.prevW = prevW_;
        OrmanDlssFrame& fr = R.dlssFrame;
        fr.jitterX = -p.jitter.x;
        fr.jitterY = -p.jitter.y;
        fr.reset = fr.reset || jump;
        fr.frameMs = dt > 0.0f ? dt * 1000.0f : 16.7f;
        const float3 r = normalize(p.U), u = normalize(p.V), w = p.W, e = p.eye;
        const float m[16] = {r.x, u.x, w.x, 0, r.y, u.y, w.y, 0, r.z, u.z, w.z, 0, -dot(r, e), -dot(u, e), -dot(w, e), 1};
        const float zn = 0.01f, zf = 1e4f;
        const float c[16] = {1.0f / length(p.U), 0, 0, 0, 0, 1.0f / length(p.V), 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
        for (int k = 0; k < 16; ++k) {
            fr.worldToView[k] = m[k];
            fr.viewToClip[k] = c[k];
        }
    }
    R.render();
    if (R.dlssOn() && p.dlss) {
        prevEye_ = p.eye;
        prevU_ = p.U;
        prevV_ = p.V;
        prevW_ = p.W;
        havePrev_ = true;
    }
    if (autoOn) {
        // the eye's adaptation: the frame's luminance over a grid of cells, followed over a second or so
        std::vector<float> Ys;
        const int gx = 128, gy = 72, nc = gx * gy;
        R.lumSamples(gx, gy, Ys);
        // a camera at night by a fire: its exposure is set by the brightest in the frame, the flames (or the embers
        // when there are none), not by the dark around it: the eye looking at a fire sees its flames yellow and orange,
        // not white, and the ground beyond them dim. The light's own mean luminance (each pixel weighted by its
        // luminance: the flame's body however much of the frame it fills; a percentile of the pixels slid off a thin
        // flame onto the ground as it puffed, the exposure swinging tenfold from frame to frame) lands at
        // exposure.key; the geometric mean only limits it (a frame with no fire still opens up, as the eye does)
        double acc = 0.0, s1 = 0.0, s2 = 0.0;
        for (int c = 0; c < nc; ++c) {
            acc += std::log(std::max(Ys[size_t(c)], 1e-6f));
            s1 += Ys[size_t(c)];
            s2 += Ys[size_t(nc + c)];
        }
        const float hiY = float(s2 / std::max(s1, 1e-12));
        const float meanY = float(std::exp(acc / double(nc)));
        const float want = std::min(cfg.f("exposure.key", 45.0f) / std::max(hiY, 1e-6f), cfg.f("exposure.keyMean", 0.25f) / std::max(meanY, 1e-6f));
        const float lum = 1.0f / std::max(want, 1e-6f);
        if (adaptLum_ < 0.0f || dt <= 0.0f) adaptLum_ = lum;
        else adaptLum_ += (lum - adaptLum_) * (1.0f - std::exp(-dt / 1.5f));
        autoExposure = std::clamp(1.0f / std::max(adaptLum_, 1e-9f), cfg.f("exposure.min", 1e-5f), cfg.f("exposure.max", 50.0f));
    }
}

bool AtesApp::setDlss(bool on) {
    if (on) R.setPattern(ATES_PHASES, 0.42f);
    const bool ok = R.enableDlss(on);
    frame_ = 0;
    havePrev_ = false;
    R.dlssFrame.reset = true;
    return ok && on;
}

bool AtesApp::save(const std::string& pathNoExt) {
    std::vector<unsigned char> px;
    R.download(px);
    return writePNG(pathNoExt + ".png", width, height, px.data());
}

int AtesApp::viewCount() { return 4; }
AtesCam AtesApp::view(int i) {
    AtesCam c;
    switch (i) {
    case 1: c.feet = make_float3(0.0f, 0, 0.95f); c.yaw = 0.0f; c.pitch = -0.48f; c.eyeHeight = 0.75f; break;   // crouched by it
    case 2: c.feet = make_float3(-0.7f, 0, 0.55f); c.yaw = -0.9f; c.pitch = -0.05f; c.eyeHeight = 0.35f; break;  // low, the flame against the sky
    case 3: c.feet = make_float3(0.0f, 0, 0.35f); c.yaw = 0.0f; c.pitch = -1.25f; c.eyeHeight = 1.3f; break;    // down into the embers
    default: c.feet = make_float3(0.0f, 0, 1.9f); c.yaw = 0.0f; c.pitch = -0.36f; c.eyeHeight = 1.65f; break;    // standing at it
    }
    return c;
}

std::string AtesApp::summary() const {
    char b[512];
    std::snprintf(b, sizeof(b), "ates: gaz %dx%dx%d (%.1f mm, %zu MB), odun %zu parca / %u yama, GAS %.1f MB, varlik %.0f ms", gas.nx, gas.ny, gas.nz, gas.h * 1e3f,
                  gas.bytes >> 20, wood.logs.size(), wood.patches, R.gasBytes / 1e6, loadMs);
    return b;
}

void AtesApp::release() {
    R.overlay = nullptr;
    sparks.release();
    wood.release();
    gas.release();
    R.release();
    assets.release();
}
