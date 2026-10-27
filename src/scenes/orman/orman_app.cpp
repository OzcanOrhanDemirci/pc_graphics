#include "scenes/orman/orman_app.h"

#include "core/image_io.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace {
double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool isSolidId(const std::string& id) {
    for (const char* k : {"boulder", "rock", "stone", "trunk", "log", "stump"})
        if (id.find(k) != std::string::npos) return true;
    return false;
}
}   // namespace

void OrmanApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/orman.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    width = cfg.i("width", width);
    height = cfg.i("height", height);
    world.configure(cfg);
}

void OrmanApp::loadAssets() {
    const double t0 = nowMs();
    assets.release();
    assets.root = root + "/assets/orman/cc0";
    loaded.clear();
    std::vector<std::string> skip;
    {
        std::stringstream ss(cfg.values.count("assets.skip") ? cfg.values.at("assets.skip") : std::string());
        std::string s;
        while (std::getline(ss, s, ',')) {
            s.erase(0, s.find_first_not_of(' '));
            s.erase(s.find_last_not_of(' ') + 1);
            if (!s.empty()) skip.push_back(s);
        }
    }
    const std::string groundId = cfg.values.count("ground.material") ? cfg.values.at("ground.material") : std::string("forest_leaves_02");
    std::error_code ec;
    std::vector<std::string> ids;
    for (const auto& e : fs::directory_iterator(assets.root, ec))
        if (e.is_directory()) ids.push_back(e.path().filename().string());
    std::sort(ids.begin(), ids.end());
    for (const std::string& id : ids) {
        if (std::find(skip.begin(), skip.end(), id) != skip.end() || id == groundId) continue;
        bool hasGltf = false;
        for (const auto& f : fs::directory_iterator(assets.root + "/" + id, ec))
            if (f.path().extension() == ".gltf") hasGltf = true;
        if (!hasGltf) continue;
        if (assets.loadModel(id, isSolidId(id))) loaded.push_back(id);
    }
    groundMaterial_ = assets.loadMaterial(groundId);
    // the fallen leaves: mostly brown and dry, some fallen green and yellowing (its share of the litter, a leaf's length,
    // its curl, its colour times the atlas's)
    const struct {
        const char* id;
        float share, length, curl;
        float3 tint;
    } litter[] = {{"LeafSet011", 0.72f, 0.11f, 0.3f, make_float3(1.0f, 1.0f, 1.0f)},
                  {"LeafSet022", 0.2f, 0.15f, 0.1f, make_float3(0.95f, 0.72f, 0.4f)},
                  {"LeafSet023", 0.08f, 0.13f, 0.12f, make_float3(0.9f, 0.68f, 0.38f)}};
    world.litterSets.clear();
    for (const auto& s : litter) {
        if (std::find(skip.begin(), skip.end(), s.id) != skip.end()) continue;
        std::vector<float4> leaves;
        const int m = assets.loadAtlas(s.id, leaves);
        if (m < 0 || leaves.empty()) continue;
        assets.materials[size_t(m)].tint = s.tint;
        world.litterSets.push_back({m, std::move(leaves), s.share, s.length, s.curl});
        loaded.push_back(s.id);
    }
    {
        OMaterial w{};
        w.flags = OMAT_WATER;
        w.tint = make_float3(0, 0, 0);
        w.roughness = 0.02f;
        waterMaterial_ = assets.addMaterial(w, "water");
    }
    if (groundMaterial_ < 0) {
        // no ground scan: a plain brown so the rest still shows
        OMaterial m{};
        m.tint = make_float3(0.18f, 0.13f, 0.09f);
        m.roughness = 0.9f;
        groundMaterial_ = assets.addMaterial(m, "ground (plain)");
    }
    assetsMs = nowMs() - t0;
}

void OrmanApp::buildWorld() {
    const double t0 = nowMs();
    // the views' places stay open: the eye stands among the plants, not inside a shrub
    world.clearings.clear();
    for (int i = 0; i < viewCount(); ++i) world.clearings.push_back({view(i).feet.x, view(i).feet.z, 0.8f});
    world.build(assets, groundMaterial_, int(assets.protos.size()), waterMaterial_);
    worldMs = nowMs() - t0;
}

bool OrmanApp::initRenderer(std::string& err) {
    const double t0 = nowMs();
    R.init(width, height, root);
    if (!R.compile(err)) return false;
    R.upload(assets, world.extras, world.places, world.tile);
    applyLight();
    uploadMs = nowMs() - t0;
    return true;
}

void OrmanApp::applyLight() {
    OrmanParams& p = R.params;
    const float el = cfg.f("sun.elevation", 25.0f) * 0.0174533f, az = cfg.f("sun.azimuth", 20.0f) * 0.0174533f;
    p.sunDir = normalize(make_float3(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az)));
    p.sunE = cfg.v3("sun.color", make_float3(1.0f, 0.9f, 0.76f)) * cfg.f("sun.intensity", 3.0f);
    p.sunCos = std::cos(cfg.f("sun.radius", 0.27f) * 0.0174533f);
    p.skyZenith = cfg.v3("sky.zenith", make_float3(0.16f, 0.26f, 0.48f)) * cfg.f("sky.intensity", 1.0f);
    p.skyHorizon = cfg.v3("sky.horizon", make_float3(0.42f, 0.47f, 0.52f)) * cfg.f("sky.intensity", 1.0f);
    p.skyGround = cfg.v3("sky.ground", make_float3(0.03f, 0.035f, 0.025f));
    p.giScale = cfg.f("gi.scale", 1.0f);
    p.wetness = cfg.f("wetness", 0.6f);
    p.waterAbsorb = cfg.v3("water.absorb", make_float3(1.6f, 2.4f, 4.5f));
    p.waterFlow = cfg.f("water.flow", 0.35f);
    p.fogDensity = cfg.f("fog.density", 0.004f);
    p.fogGround = cfg.f("fog.ground", 0.03f);
    p.fogHeight = cfg.f("fog.height", 2.5f);
    p.fogG = cfg.f("fog.g", 0.7f);
    p.fogFar = cfg.f("fog.far", 60.0f);
    p.fogSteps = cfg.i("fog.steps", 24);
    p.fogAmbient = cfg.f("fog.ambient", 0.5f);
    p.wetGloss = cfg.f("wet.gloss", 0.5f);
    p.saturation = cfg.f("saturation", 1.15f);
    p.aoPower = cfg.f("ao.power", 1.0f);
    p.bloomStrength = cfg.f("bloom", 0.04f);
    p.vignette = cfg.f("vignette", 0.25f);
    p.debug = cfg.i("debug", 0);
    autoOn = cfg.i("exposure.auto", 1) != 0;
    R.setPattern(cfg.i("samples", 4), cfg.f("filter.sigma", 0.42f));
}

void OrmanApp::bakeLight() {
    bake = OBakeStats();
    R.visBytes = 0;
    R.params.patchHalf = 0.5f * world.size;
    for (const OrmanWorld::AoJob& j : world.aoJobs) R.bakeGroundAO(j.proto, j.at, cfg.f("ao.radius", 0.6f), cfg.i("ao.rays", 48), bake);
    const float h = cfg.f("grid.spacing", 0.3f), top = cfg.f("grid.top", 40.0f), vh = cfg.f("vis.spacing", 0.1f);
    const float half = 0.5f * world.size;
    const float3 zero = make_float3(0, 0, 0);
    // the tile's light first, in a cell deep among its copies (the patch's then sees the forest around it lit)
    if (!world.tile.cells.empty()) {
        const float3 lo = make_float3(-half, world.tileLo.y, -half), hi = make_float3(half, std::min(world.tileHi.y, top), half);
        R.bakeGrid(true, lo, hi, h, world.tileBakeAt, cfg.i("grid.rays", 192), cfg.i("grid.passes", 2), bake);
        R.bakeSunVis(true, lo, make_float3(half, std::min(world.tileHi.y + 1.0f, top), half), vh, world.tileBakeAt, bake);
    }
    const float3 lo = make_float3(-half, world.lo.y, -half), hi = make_float3(half, std::min(world.hi.y, top), half);
    R.bakeGrid(false, lo, hi, h, zero, cfg.i("grid.rays", 192), cfg.i("grid.passes", 2), bake);
    // the sun in the air: the mist's shafts (up to where the leaves end)
    R.bakeSunVis(false, lo, make_float3(half, std::min(world.hi.y + 1.0f, top), half), vh, zero, bake);
}

float3 OrmanApp::forward() const {
    return make_float3(-std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), -std::cos(cam.yaw) * std::cos(cam.pitch));
}
float3 OrmanApp::eye() const { return cam.feet + make_float3(0.0f, cam.eyeHeight, 0.0f); }

void OrmanApp::render(float dt) {
    OrmanParams& p = R.params;
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
    p.exposure = std::exp2(cfg.f("exposure", 0.0f) + exposureEv) * (autoOn ? autoExposure : 1.0f);
    clock_ += dt;
    p.time = float(clock_);
    if (R.dlssOn() && p.dlss) {
        // this frame's place in the pixel (Halton 2, 3: the sequence DLSS was trained on) and the last frame's eye; a
        // jump (a view key, the start) drops the history
        const unsigned int ph = frame_++ % ORMAN_PHASES;
        auto halton = [](unsigned int i, unsigned int b) {
            float f = 1.0f, r = 0.0f;
            for (; i > 0; i /= b) {
                f /= float(b);
                r += f * float(i % b);
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
        // DLSS's jitter is the picture's shift: the opposite of where the sample sits
        fr.jitterX = -p.jitter.x;
        fr.jitterY = -p.jitter.y;
        fr.reset = fr.reset || jump;
        fr.frameMs = dt > 0.0f ? dt * 1000.0f : 16.7f;
        // the eye's matrices, row-major for row vectors (v' = v M): view x right, y up, z ahead; the projection D3D's
        const float3 r = normalize(p.U), u = normalize(p.V), w = p.W, e = p.eye;
        const float m[16] = {r.x, u.x, w.x, 0, r.y, u.y, w.y, 0, r.z, u.z, w.z, 0, -dot(r, e), -dot(u, e), -dot(w, e), 1};
        const float zn = 0.02f, zf = 1e4f;
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
        // the eye's adaptation: the frame's mean log brightness on a sparse grid (centre-weighted), followed over a
        // second or so; the picture's key 0.16 at the mean
        const int gx = 48, gy = 27;
        std::vector<float4> s(size_t(gx) * gy);
        const int sx = width / gx, sy = height / gy;
        for (int j = 0; j < gy; ++j)
            cudaMemcpy2D(&s[size_t(j) * gx], sizeof(float4), p.hdr + size_t(j * sy + sy / 2) * width + sx / 2, size_t(sx) * sizeof(float4), sizeof(float4), gx,
                         cudaMemcpyDeviceToHost);
        double acc = 0.0, wsum = 0.0;
        for (int j = 0; j < gy; ++j)
            for (int i = 0; i < gx; ++i) {
                const float4 c = s[size_t(j) * gx + i];
                const float Y = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
                const float u = (i + 0.5f) / gx - 0.5f, v = (j + 0.5f) / gy - 0.5f;
                const float w = std::exp(-(u * u + v * v) * 6.0f);
                acc += w * std::log(std::max(Y, 1e-4f));
                wsum += w;
            }
        const float lum = float(std::exp(acc / wsum));
        if (adaptLum_ < 0.0f || dt <= 0.0f) adaptLum_ = lum;
        else adaptLum_ += (lum - adaptLum_) * (1.0f - std::exp(-dt / 1.2f));
        autoExposure = std::clamp(cfg.f("exposure.key", 0.16f) / std::max(adaptLum_, 1e-4f), cfg.f("exposure.min", 0.05f), cfg.f("exposure.max", 40.0f));
    }
}

bool OrmanApp::setDlss(bool on) {
    // the sequences (the sun's disk points, the mist's steps, the alpha thresholds) need all sixteen places
    if (on) R.setPattern(ORMAN_PHASES, cfg.f("filter.sigma", 0.42f));
    const bool ok = R.enableDlss(on);
    if (ok && on) R.params.covBias = 0.6f;   // one sample a pixel: the leaf's coverage over the pixel's own reach
    frame_ = 0;
    havePrev_ = false;
    R.dlssFrame.reset = true;
    return ok && on;
}

bool OrmanApp::save(const std::string& pathNoExt) {
    std::vector<unsigned char> px;
    R.download(px);
    return writePNG(pathNoExt + ".png", width, height, px.data());
}

int OrmanApp::viewCount() { return 4; }
OrmanCam OrmanApp::view(int i) {
    OrmanCam c;
    switch (i) {
    case 1: c.feet = make_float3(1.0f, 0, 4.5f); c.yaw = 2.6f; c.pitch = -0.12f; break;     // the trunk over the stream
    case 2: c.feet = make_float3(-2.0f, 0, -6.0f); c.yaw = -0.6f; c.pitch = 0.55f; break;   // up into the leaves
    case 3: c.feet = make_float3(6.0f, 0, 2.0f); c.yaw = 1.2f; c.pitch = -0.35f; break;     // the ground close
    default: c.feet = make_float3(-6.0f, 0, 1.2f); c.yaw = -1.45f; c.pitch = -0.08f; break; // down the stream, into the light
    }
    return c;
}

std::string OrmanApp::summary() const {
    char b[1024];
    std::snprintf(b, sizeof(b),
                  "orman: %zu varlik, %zu prototip, %zu malzeme, doku %.0f MB | kopya %u (ust duzey %u; karo %zu yer, %zu hucre), kopyalanmis ucgen "
                  "%.1f M | GAS %.0f MB, IAS %.1f MB, geometri %.0f MB, izgara %.0f + karo %.0f MB (%zu yoklama, gecerli %%%.0f), gunes hacmi %.0f MB | "
                  "sureler: varlik %.0f ms, dunya %.0f ms, yukleme %.0f ms, AO %.0f ms, izgara %.0f ms, gunes hacmi %.0f ms",
                  loaded.size(), assets.protos.size(), assets.materials.size(), assets.textureBytes() / 1e6, R.instanceCount, R.topInstances,
                  world.tile.places.size(), world.tile.cells.size(), R.instancedTriangles / 1e6, R.gasBytes / 1e6, R.iasBytes / 1e6, R.geomBytes / 1e6,
                  R.gridBytes / 1e6, R.tileGridBytes / 1e6, bake.probes, 100.0 * double(bake.valid) / double(std::max<size_t>(bake.probes, 1)),
                  R.visBytes / 1e6, assetsMs, worldMs, uploadMs, bake.aoMs, bake.gridMs, bake.visMs);
    return b;
}

void OrmanApp::release() {
    R.release();
    assets.release();
}

// ---------------------------------------------------------------------------------------------------------------
int runOrmanCli(int argc, char** argv) {
    OrmanApp a;
    a.root = PROJECT_ROOT;
    std::string out = "captures/orman";
    std::vector<int> views;
    std::vector<OrmanCam> freeCams;   // --cam, as many as given
    int bench = 0, stab = 0, refSpp = 16;
    std::string path = "yan";
    float speed = 1.3f;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (s == "--out") out = next();
        else if (s == "--view") views.push_back(std::stoi(next()));
        else if (s == "--set") a.overrides.push_back(next());
        else if (s == "--root") a.root = next();
        else if (s == "--w") a.overrides.push_back("width=" + next());
        else if (s == "--h") a.overrides.push_back("height=" + next());
        else if (s == "--bench") bench = std::stoi(next());
        else if (s == "--kararlilik") stab = std::stoi(next());
        else if (s == "--yol") path = next();
        else if (s == "--hiz") speed = std::stof(next());
        else if (s == "--ref") refSpp = std::stoi(next());
        else if (s == "--cam") {
            OrmanCam c;
            if (std::sscanf(next().c_str(), "%f %f %f %f %f %f", &c.feet.x, &c.feet.y, &c.feet.z, &c.yaw, &c.pitch, &c.fovY) >= 5) {
                freeCams.push_back(c);
            }
        }
    }
    a.loadConfig();
    a.loadAssets();
    std::printf("orman: varliklar: ");
    for (const auto& id : a.loaded) std::printf("%s ", id.c_str());
    std::printf("\n");
    for (const auto& id : a.assets.missing) std::printf("orman: eksik %s\n", id.c_str());
    a.buildWorld();
    for (const auto& [id, n] : a.world.counts) std::printf("  %-28s %lld kopya\n", id.c_str(), n);
    std::string err;
    if (!a.initRenderer(err)) {
        std::fprintf(stderr, "orman: %s\n", err.c_str());
        return 1;
    }
    a.bakeLight();
    if (a.cfg.i("dlss", 1) != 0) {
        if (a.setDlss(true)) std::printf("orman: DLSS-RR acik (DLAA, piksel basina bir ornek)\n");
        else std::printf("orman: DLSS-RR kapali: %s\n", a.R.dlssError.c_str());
    }
    std::printf("%s\n", a.summary().c_str());
    std::error_code ec;
    fs::create_directories(fs::path(a.root) / fs::path(out).parent_path(), ec);
    if (views.empty() && freeCams.empty()) views = {0, 1, 2, 3};
    auto shoot = [&](const OrmanCam& c, const std::string& name) {
        a.cam = c;
        a.world.collide(a.cam.feet.x, a.cam.feet.z, -1e9f, 0.3f);
        a.cam.feet.y = a.world.height(a.cam.feet.x, a.cam.feet.z);
        // the eye's adaptation settled: a few frames (DLSS: its history filled, three rounds of the sequence)
        const int settle = a.R.dlssOn() ? 3 * ORMAN_PHASES : 4;
        for (int k = 0; k < settle; ++k) a.render(0.0f);
        a.render(0.0f);
        const std::string path = (fs::path(a.root) / (out + "_" + name)).generic_string();
        a.save(path);
        std::printf("orman: %s (%.2f ms isleme, %.2f ms goruntu, pozlama x%.2f, sonlu olmayan ornek %u)\n", path.c_str(), a.R.renderMs, a.R.displayMs,
                    a.autoExposure, a.R.badSamples);
        if (bench > 0) {
            double sum = 0.0, worst = 0.0;
            for (int k = 0; k < bench; ++k) {
                a.render(1.0f / 60.0f);
                sum += a.R.renderMs + a.R.displayMs;
                worst = std::max(worst, double(a.R.renderMs + a.R.displayMs));
            }
            std::printf("orman: olcum %d kare: ortalama %.2f ms, en kotu %.2f ms\n", bench, sum / bench, worst);
        }
    };
    if (stab > 0) {
        // a walk at 60 frames a second for tools/titreme.py and tools/kararlilik.py: each moment drawn live (the
        // configured samples) and as a reference (--ref samples), the same exposure (settled once, then held) and the
        // same clock. --yol yan: a step sideways (the parallax that makes leaves crawl), ileri: forwards, don: turning
        // on the spot
        const OrmanCam c0 = !freeCams.empty() ? freeCams[0] : OrmanApp::view(views.empty() ? 0 : views[0]);
        const int live = a.cfg.i("samples", 4);
        const float sigma = a.cfg.f("filter.sigma", 0.42f);
        const std::string dir = (fs::path(a.root) / out).generic_string();
        fs::create_directories(dir, ec);
        a.cam = c0;
        a.cam.feet.y = a.world.height(a.cam.feet.x, a.cam.feet.z);
        for (int k = 0; k < (a.R.dlssOn() ? 3 * ORMAN_PHASES : 6); ++k) a.render(0.0f);
        a.exposureEv = std::log2(a.autoExposure);
        a.autoOn = false;
        double liveMs = 0.0;
        for (int f = 0; f < stab; ++f) {
            const float t = f / 60.0f;
            OrmanCam c = c0;
            const float fx = -std::sin(c0.yaw), fz = -std::cos(c0.yaw);
            if (path == "ileri") c.feet = c0.feet + make_float3(fx, 0, fz) * (speed * t);
            else if (path == "don") c.yaw = c0.yaw - speed * t;   // (--hiz in radians a second)
            else c.feet = c0.feet + make_float3(-fz, 0, fx) * (speed * t);
            c.feet.y = a.world.height(c.feet.x, c.feet.z);
            a.cam = c;
            if (!a.R.dlssOn()) a.R.setPattern(live, sigma);
            a.render(1.0f / 60.0f);
            liveMs += a.R.renderMs + a.R.displayMs;
            char name[64];
            std::snprintf(name, sizeof(name), "/live_%04d", f);
            a.save(dir + name);
            // the reference: the fixed samples, DLSS paused (its history waits for the next live frame)
            const bool dl = a.R.dlssOn();
            if (dl) {
                a.R.params.dlss = 0;
                a.R.params.lodBias = 0.0f;
            }
            a.R.setPattern(refSpp, sigma);
            a.render(0.0f);
            std::snprintf(name, sizeof(name), "/ref_%04d", f);
            a.save(dir + name);
            if (dl) {
                a.R.setPattern(ORMAN_PHASES, sigma);
                a.R.params.covBias = 0.6f;
                a.R.params.lodBias = -1.0f;
                a.R.params.dlss = 1;
            }
        }
        const std::string liveName = a.R.dlssOn() ? std::string("DLSS-RR") : std::to_string(live) + " ornek";
        std::printf("orman: kararlilik %d kare (%s, %.2f m/s) -> %s, canli %s (ortalama %.2f ms), referans %d\n", stab, path.c_str(), speed,
                    dir.c_str(), liveName.c_str(), liveMs / stab, refSpp);
        a.release();
        return 0;
    }
    for (size_t k = 0; k < freeCams.size(); ++k) shoot(freeCams[k], freeCams.size() == 1 ? std::string("serbest") : "serbest" + std::to_string(k));
    for (int v : views) shoot(OrmanApp::view(v), "bakis" + std::to_string(v));
    a.release();
    return 0;
}
