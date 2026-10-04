// Scene 5 "Koy": the run's state (koy_app.h) and the command-line tools.
//
//   pc_graphics.exe --scene koy --headless --out captures/koy/x [--cam "x y z" --look "x y z"] [--t s] [--ref]
//            one frame as the window draws it (or, --ref, the converged 256-sample reference of the same moment)
//   pc_graphics.exe --scene koy --kararlilik --yol sabit|yuz|don|yukari --kare N --out captures/kararlilik/koy_sabit
//            steadiness and sharpness (PLAN §9): N live frames of a path at 41 fps and the reference of each; then
//            python tools/kararlilik.py <out>
//   pc_graphics.exe --scene koy --zaman N                 N live frames: the passes' timings
//   pc_graphics.exe --scene koy --doku --out captures/koy/doku/x --cam "x y z" --look "x y z" [--cam .. --look ..]
//            each view in the three texture modes with one exposure (only the textures differ); then
//            python tools/doku_kiyas.py captures/koy/doku/x
//   pc_graphics.exe --scene koy --yokla --cam "x y z" [--cam ..]   height above the sand, the nearest surface
//   pc_graphics.exe --scene koy --dalgacik --out captures/koy/dalgacik/x --cam .. --look ..   ripples off and on, one exposure
//   pc_graphics.exe --scene koy --test [ad]               the physics tests (Snell's window, colour at depth, visibility, ...)
//   pc_graphics.exe --scene koy --map captures/koy/harita.png   the trial area from above (the same floor function)
//   options: --set key=value, --width W, --height H, --spp 1|4|8|16, --edge N, --filter 0..3
#include "scenes/koy/koy_app.h"
#include "core/gpu_stats.h"
#include "scenes/koy/koy_island.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <thread>

namespace {
std::string stamp() {
    std::time_t t = std::time(nullptr);
    char b[64];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", std::localtime(&t));
    return b;
}
}   // namespace

void KoyApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/koy.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    quality.base = cfg.i("sample.base", quality.base);
    quality.edge = cfg.i("sample.edge", quality.edge);
    quality.filter = cfg.i("sample.filter", quality.filter);
    quality.radius = cfg.f("sample.radius", quality.radius);
    quality.contrast = cfg.f("sample.edge_contrast", quality.contrast);
}

bool KoyApp::compile() {
    std::string log;
    const double t0 = know();
    const bool ok = renderer.compileDevice(log);
    std::printf("%s koy programs (%.1f s)\n", ok ? "compiled" : "COMPILE FAILED:", know() - t0);
    if (!log.empty() && (!ok || log.size() > 2)) std::printf("%s\n", log.c_str());
    return ok;
}

void KoyApp::setup() {
    KoyParams& p = renderer.params();
    // optics: spectra, the light field, the tables
    optics.build(cfg);
    optics.upload(p);
    std::printf("koy optik: %s\n", optics.summary().c_str());
    // the sea surface and its caustics
    KoyOceanSettings os;
    os.N = cfg.i("ocean.n", 512);
    os.patch = cfg.f("ocean.patch", 16.0f);
    os.wind = cfg.f("ocean.wind", 4.0f);
    os.windDirDeg = cfg.f("ocean.wind_dir", 30.0f);
    os.fetchKm = cfg.f("ocean.fetch_km", 8.0f);
    os.amplitude = cfg.f("ocean.amplitude", 1.0f);
    os.loopSeconds = cfg.f("ocean.loop", 24.0f);
    os.minWave = cfg.f("ocean.min_wave", 0.06f);
    os.swellAmplitude = cfg.f("ocean.swell", 0.05f);
    os.unresolvedVar = cfg.f("ocean.unresolved_var", -1.0f);
    KoyCausticSettings cs;
    cs.N = cfg.i("caustic.n", 1024);
    cs.method = cfg.i("caustic.method", 1);
    cs.beams = cfg.i("caustic.beams", 1024);
    cs.fineZ0 = cfg.f("caustic.fine_z0", 4.2f);
    cs.fineZ1 = cfg.f("caustic.fine_z1", 15.5f);
    cs.rays = cfg.i("caustic.rays", 1024);
    cs.slices = cfg.i("caustic.slices", 32);
    cs.z0 = cfg.f("caustic.z0", 0.5f);
    cs.z1 = cfg.f("caustic.z1", 20.0f);
    cs.minBlurTexels = cfg.f("caustic.min_blur", 0.6f);
    cs.levels = cfg.i("caustic.levels", 4);
    ocean.init(os, cs, optics.sunAir, optics.waterN, optics.sunRadiusAir);
    p.ocean = ocean.surfaceTexture();
    p.oceanPatch = os.patch;
    p.oceanTexels = float(os.N);
    p.oceanHeightMax = ocean.heightMax();
    p.slopeVarFloor = ocean.slopeVarianceUnresolved() * cfg.f("ocean.unresolved_scale", 1.0f);
    // The ripples (Ara durak 3): the waves shorter than ocean.min_wave, which the main surface leaves out so that the
    // caustics stay clean, on a small patch of their own and seen only by the view (Snell's window, the mirror, the
    // glints). They take over most of the slope variance that was statistical; what is shorter still (below
    // ripple.min_wave) stays statistical. From afar the footprint averages them (LEAN) back into the same roughness.
    floorVarOff = p.slopeVarFloor;
    floorVarOn = p.slopeVarFloor;
    if (cfg.i("ripple.use", 1)) {
        KoyOceanSettings rs = os;
        rs.name = "dalgacik";
        rs.N = cfg.i("ripple.n", 1024);
        rs.patch = cfg.f("ripple.patch", 4.0f);
        rs.minWave = cfg.f("ripple.min_wave", 0.02f);
        rs.maxWave = os.minWave;
        rs.swellAmplitude = 0.0f;
        rs.unresolvedVar = 0.0f;
        rs.targetVar = p.slopeVarFloor * cfg.f("ripple.share", 0.8f);
        rs.seed = 23;
        KoyCausticSettings none;   // the ripples never make caustics: the smallest volume, never updated
        none.N = 8;
        none.rays = 8;
        none.slices = 2;
        none.levels = 1;
        if (ripples.init(rs, none, optics.sunAir, optics.waterN, optics.sunRadiusAir)) {
            floorVarOn = std::max(p.slopeVarFloor - ripples.slopeVarianceResolved(), 0.0f);
            p.oceanFinePatch = rs.patch;
            p.oceanFineTexels = float(rs.N);
        }
    }
    setRipples(cfg.i("ripple.on", 1) != 0);
    for (int l = 0; l < 4; ++l) p.caustic[l] = ocean.causticTexture(std::min(l, ocean.causticLevels() - 1));
    p.causticLevels = ocean.causticLevels();
    p.causticPatch = os.patch;
    p.causticLz0 = std::log(cs.z0);
    p.causticLz1 = std::log(cs.z1);
    p.causticSlices = cs.slices;
    p.causticFlat = cfg.i("caustic.flat", 0);
    p.shaftStep0 = cfg.f("shaft.step0", 0.06f);
    p.shaftStepGrow = cfg.f("shaft.grow", 0.06f);
    p.shaftMax = std::max(cfg.f("shaft.max", 70.0f), 1.0f);
    p.farDistance = cfg.f("view.far", 180.0f);
    p.airGeometry = cfg.i("air.geometry", 1);
    p.shadowSway = cfg.i("sun.sway", 1);
    p.upwell = cfg.f("air.upwell", 0.06f);
    p.chromaAdapt = cfg.f("display.chroma_adapt", 0.5f);
    p.glare = cfg.f("display.glare", 0.04f);
    // the thermocline: dn/dT of sea water ~ -1.2e-4 per degree near 20 C: 8 degrees colder, the index 1e-3 higher (7.2e-4
    // of 1.338 relative); the step's slopes from internal waves and the mixing at its face
    p.thermoOn = cfg.i("thermo.use", 0);   // off: one sharp sheet drew a band at eye level (Ozcan 10-05: "ufuk cizgisi")
    p.thermoY = -cfg.f("thermo.depth", 27.0f);
    p.thermoDn = cfg.f("thermo.dt", 8.0f) * 1.2e-4f / 1.338f;
    p.thermoSlope = cfg.f("thermo.slope", 0.18f);
    p.thermoHaze = cfg.f("thermo.haze", 2.5f);
    p.lookPower = cfg.f("display.look_power", 1.0f);
    p.lookSat = cfg.f("display.look_sat", 1.0f);
    // the world
    size_t freeBefore = 0, freeAfter = 0, totalMem = 0;
    cudaMemGetInfo(&freeBefore, &totalMem);
    world.build(cfg, root);
    cudaMemGetInfo(&freeAfter, &totalMem);
    std::printf("koy dokular: %.1f s (paralel cozme), dizilerde %.2f GB; GPU bellegi dunyayla %.2f GB artti, kartta %.2f / %.2f GB dolu\n",
                world.textureSeconds, world.textureBytes / 1073741824.0, (double(freeBefore) - double(freeAfter)) / 1073741824.0,
                (totalMem - freeAfter) / 1073741824.0, totalMem / 1073741824.0);
    renderer.uploadWorld(world.meshes, world.instances, world.materials, world.textureSets());
    {   // the floor's depth under every (x, z): which light fields a point reads (koy.cu fieldsFor)
        const int B = cfg.i("light.bottom_map", 1024);
        const float3 lo = cfg.v3("floor.extent_min", make_float3(-420.0f, 0.0f, -520.0f)), hi = cfg.v3("floor.extent_max", make_float3(420.0f, 0.0f, 480.0f));
        const KoyFloorShape fs = KoyFloorShape::from(cfg);
        std::vector<float> m(size_t(B) * B);
        for (int j = 0; j < B; ++j)
            for (int i = 0; i < B; ++i) m[size_t(j) * B + i] = -fs(lo.x + (i + 0.5f) / B * (hi.x - lo.x), lo.z + (j + 0.5f) / B * (hi.z - lo.z));
        cudaChannelFormatDesc fd = cudaCreateChannelDesc<float>();
        cudaArray_t arr = nullptr;
        cudaMallocArray(&arr, &fd, B, B);
        cudaMemcpy2DToArray(arr, 0, 0, m.data(), size_t(B) * sizeof(float), size_t(B) * sizeof(float), B, cudaMemcpyHostToDevice);
        cudaResourceDesc rd{};
        rd.resType = cudaResourceTypeArray;
        rd.res.array.array = arr;
        cudaTextureDesc td{};
        td.addressMode[0] = td.addressMode[1] = cudaAddressModeClamp;
        td.filterMode = cudaFilterModeLinear;
        td.readMode = cudaReadModeElementType;
        td.normalizedCoords = 1;
        cudaTextureObject_t t = 0;
        cudaCreateTextureObject(&t, &rd, &td, nullptr);
        bottomArr = arr;
        bottomTex = t;
        p.bottomMap = t;
        p.bottomMapMin = make_float2(lo.x, lo.z);
        p.bottomMapSize = make_float2(hi.x - lo.x, hi.z - lo.z);
        std::printf("koy isik alanlari: %d dip derinligi", p.fields);
        for (int f = 0; f < p.fields; ++f) std::printf(" %.1f", p.fieldBottom[f]);
        std::printf(" m\n");
    }
    {   // the floor's variety (Ada enriched): the second sand's patches, silt, grit; zero outside the map
        const KoyFloorMask& fm = world.floorMask;
        p.floorMask = 0;
        p.floor2Set = world.setFloor2;
        p.floor2Tile = cfg.f("floor2.tile", 3.0f);
        p.floor2Normal = cfg.f("floor2.normal", 1.0f);
        p.floor2Tint = cfg.v3("floor2.tint", make_float3(1.0f, 1.0f, 1.0f));
        p.siltTint = cfg.v3("floor2.silt_tint", make_float3(0.72f, 0.70f, 0.60f));
        p.gritTint = cfg.v3("floor2.grit_tint", make_float3(0.78f, 0.76f, 0.70f));
        if (fm.w > 0 && fm.h > 0) {
            cudaChannelFormatDesc fd = cudaCreateChannelDesc<uchar4>();
            cudaArray_t arr = nullptr;
            cudaMallocArray(&arr, &fd, fm.w, fm.h);
            cudaMemcpy2DToArray(arr, 0, 0, fm.px.data(), size_t(fm.w) * sizeof(uchar4), size_t(fm.w) * sizeof(uchar4), fm.h, cudaMemcpyHostToDevice);
            cudaResourceDesc rd{};
            rd.resType = cudaResourceTypeArray;
            rd.res.array.array = arr;
            cudaTextureDesc td{};
            td.addressMode[0] = td.addressMode[1] = cudaAddressModeBorder;
            td.filterMode = cudaFilterModeLinear;
            td.readMode = cudaReadModeNormalizedFloat;
            td.normalizedCoords = 1;
            cudaTextureObject_t t = 0;
            cudaCreateTextureObject(&t, &rd, &td, nullptr);
            floorMaskArr = arr;
            floorMaskTex = t;
            p.floorMask = t;
            p.floorMaskMin = fm.min;
            p.floorMaskSize = fm.size;
        }
    }
    {   // the sand ripples (analytic in koy.cu)
        p.ripples = cfg.i("ripple.sand", 1);
        p.rippleLength = cfg.f("floor.ripple_length", 0.09f);
        p.rippleHeight = cfg.f("floor.ripple_height", 0.011f);
        p.rippleDepth0 = cfg.f("floor.ripple_depth0", 6.0f);
        p.rippleDepth1 = cfg.f("floor.ripple_depth1", 24.0f);
        const float3 ic = cfg.v3("island.centre", make_float3(0.0f, 0.0f, 60.0f));
        p.islandCentre = make_float2(ic.x, ic.z);
    }
    std::printf("koy dunya: %s\n", world.summary().c_str());
    stepWorld(0.0f);
    const double t0 = know();
    renderer.bakeAO();
    // the sun depth maps (the light shafts' shadows): sunmap.N = x0 z0 x1 z1 texel
    renderer.params().sunMaps = 0;
    renderer.params().swayMax = cfg.f("sun.sway_max", 8.0f);
    size_t mapTexels = 0;
    for (int k = 0; k < KOY_SUNMAPS; ++k) {
        const std::vector<float> v = koyList(cfg, "sunmap." + std::to_string(k));
        if (v.size() < 5) continue;
        if (renderer.bakeSunMap(make_float2(v[0], v[1]), make_float2(v[2], v[3]), v[4])) {
            const KSunMap& M = renderer.params().sunMap[renderer.params().sunMaps - 1];
            mapTexels += size_t(M.w) * M.h;
        }
    }
    std::printf("koy firin: golgeleme ve %d gunes derinlik haritasi (%.1f M tekstel) %.2f s\n", renderer.params().sunMaps, mapTexels / 1e6, know() - t0);
    // the light under the pier: hall.box = x0 y0 z0 x1 y1 z1, hall.cell, hall.dirs (no box: the sky map's cone)
    renderer.params().hallOn = 0;
    {
        const std::vector<float> b = koyList(cfg, "hall.box");
        if (b.size() >= 6 && cfg.i("hall.use", 1)) {
            const double th = know();
            renderer.bakeHall(make_float3(b[0], b[1], b[2]), make_float3(b[3], b[4], b[5]), cfg.f("hall.cell", 0.25f), cfg.i("hall.dirs", 256),
                               cfg.i("hall.bounces", 2));
            const KoyParams& hp = renderer.params();
            std::printf("koy iskele alti isik hacmi: %d x %d x %d hucre, %d yon, pay %.3f - %.3f, %.2f s\n", hp.hallNx, hp.hallNy, hp.hallNz,
                        cfg.i("hall.dirs", 256), renderer.hallStats_[0], renderer.hallStats_[1], know() - th);
        }
    }
    // blue noise for the dither
    {
        int w = 0, h = 0;
        std::vector<unsigned char> g;
        if (readPNGGray(root + "/assets/bluenoise128.png", w, h, g) && w == 128 && h == 128) {
            std::vector<float> f(g.size());
            for (size_t i = 0; i < g.size(); ++i) f[i] = (g[i] + 0.5f) / 256.0f;
            float* d = nullptr;
            cudaMalloc(&d, f.size() * sizeof(float));
            cudaMemcpy(d, f.data(), f.size() * sizeof(float), cudaMemcpyHostToDevice);
            p.blueNoise = d;
        }
    }
    restartDiver();
    ev = cfg.f("display.ev", 0.0f);
    torchOn = cfg.i("torch.on", 0) != 0;
    applyQuality();
    setCamera();
}

void KoyApp::setTextureMode(int mode) {
    world.applyTextureMode(cfg, mode);
    renderer.updateMaterials(world.materials);
}

void KoyApp::setRipples(bool on) {
    KoyParams& p = renderer.params();
    const bool was = ripplesOn;
    ripplesOn = on && ripples.surfaceTexture() != 0;
    // turned on: their field now, at this moment (paused, no stepWorld comes to fill it: the old or an unset one)
    if (ripplesOn && !was) ripples.update(worldTime, renderer.stream(), false);
    p.oceanFine = ripplesOn ? ripples.surfaceTexture() : 0;
    p.slopeVarFloor = ripplesOn ? floorVarOn : floorVarOff;   // the same roughness in all, only part of it now resolved
}

void KoyApp::applyQuality() {
    renderer.params().edgeIdShare = cfg.f("sample.edge_id_share", 0.25f);
    renderer.setSampling(quality.base, quality.edge, quality.filter, quality.radius, quality.contrast);
}

void KoyApp::setCamera() {
    KCamera& c = renderer.params().cam;
    const float3 f = diver.forward();
    const float3 r = normalize(cross(f, make_float3(0, 1, 0)));
    const float3 u = cross(r, f);
    c.pos = diver.pos;
    c.fwd = f;
    c.right = r;
    c.up = u;
    // the field of view inside the mask (in air): the water narrows it (flat port)
    const float hfov = cfg.f("camera.hfov_air", 90.0f) * 3.14159265f / 180.0f;
    c.tanHalfX = std::tan(0.5f * hfov);
    c.tanHalfY = c.tanHalfX * float(height) / float(width);
    c.portN = cfg.f("camera.port", 1.0f) > 0.5f ? optics.waterN : 1.0f;
    c.pixelAngle = 2.0f * c.tanHalfX / float(width) / c.portN;
    // the torch in the right hand, a little ahead and below the eye, aimed at a point two metres out
    KoyParams& p = renderer.params();
    p.torch = torchOn ? 1 : 0;
    p.torchPos = c.pos + r * 0.25f - u * 0.12f + f * 0.10f;
    p.torchDir = normalize(c.pos + f * 2.0f - p.torchPos);
    p.torchCos0 = std::cos(cfg.f("torch.inner_deg", 20.0f) * 3.14159265f / 180.0f);
    p.torchCos1 = std::cos(cfg.f("torch.outer_deg", 32.0f) * 3.14159265f / 180.0f);
}

void KoyApp::stepDiver(float fwd, float right, float up, bool strong, float lookDX, float lookDY, float dt) {
    const float sens = cfg.f("diver.mouse", 0.0022f);
    diver.yaw -= lookDX * sens;
    diver.pitch = std::max(-1.45f, std::min(1.45f, diver.pitch - lookDY * sens));
    const float3 f = diver.forward();
    const float3 r = normalize(cross(f, make_float3(0, 1, 0)));
    float3 wish = f * fwd + r * right + make_float3(0, 1, 0) * up;
    const float wl = length(wish);
    if (wl > 1e-4f) wish = wish / wl;
    // fin thrust against quadratic drag: F = 1/2 rho CdA v^2; the mass includes the water moved along (added mass)
    const float mass = cfg.f("diver.mass", 90.0f) * 1.5f;
    const float thrust = (strong ? cfg.f("diver.thrust_strong", 85.0f) : cfg.f("diver.thrust", 32.0f)) * (wl > 1e-4f ? 1.0f : 0.0f);
    const float cda = cfg.f("diver.cda", 0.25f), rho = 1025.0f;
    const int sub = 4;
    for (int k = 0; k < sub; ++k) {
        const float h = dt / sub;
        const float v = length(diver.vel);
        const float3 drag = v > 1e-5f ? diver.vel * (-0.5f * rho * cda * v) : make_float3(0, 0, 0);
        // the breath's buoyancy (stepBreath): a little up on the inhale, down on the exhale, damped by the water's linear
        // drag at walking-slow speeds (the quadratic one vanishes there)
        const float3 lift = make_float3(0.0f, buoyancy - 6.0f * diver.vel.y, 0.0f);
        diver.vel = diver.vel + (wish * thrust + drag + lift) * (h / mass);
        diver.pos = diver.pos + diver.vel * h;
        // collisions: the floor (the eye at least 0.3 m above it, as near as the boulder lets you), the boulder, the
        // surface's ceiling (1 m under it)
        const float fy = koyFloor(cfg, diver.pos.x, diver.pos.z) + cfg.f("diver.floor_clear", 0.3f);
        if (diver.pos.y < fy) { diver.pos.y = fy; diver.vel.y = std::max(diver.vel.y, 0.0f); }
        if (k == sub - 1) {   // the rocks: the probe's distances in 26 directions push the head out (0.3 m)
            float dist[26];
            renderer.probe(diver.pos, dist);
            const float rmin = cfg.f("diver.head", 0.3f);
            for (int q = 0; q < 26; ++q) {
                if (dist[q] >= rmin) continue;
                const int kk = q >= 13 ? q + 1 : q;
                const float3 dir = normalize(make_float3(float(kk % 3 - 1), float((kk / 3) % 3 - 1), float(kk / 9 - 1)));
                diver.pos = diver.pos - dir * (rmin - dist[q]);
                const float into = dot(diver.vel, dir);
                if (into > 0.0f) diver.vel = diver.vel - dir * into;
            }
        }
        const float ceil = -cfg.f("diver.ceiling", 1.0f);
        if (diver.pos.y > ceil) { diver.pos.y = ceil; diver.vel.y = std::min(diver.vel.y, 0.0f); }
    }
    setCamera();
}

void KoyApp::restartDiver() {
    diver.pos = cfg.v3("diver.start", make_float3(1.9f, -1.2f, 0.8f));
    diver.yaw = cfg.f("diver.yaw_deg", 180.0f) * 3.14159265f / 180.0f;
    diver.pitch = cfg.f("diver.pitch_deg", -8.0f) * 3.14159265f / 180.0f;
    diver.vel = make_float3(0, 0, 0);
    o2Dose = 0.0f;
}

void KoyApp::stepBreath(float dt, bool strong) {
    if (!cfg.i("view.bubbles", 1)) {
        bubbleList.clear();
        buoyancy = 0.0f;
        return;
    }
    // the cycle: inhale over 40 %, a pause, exhale over 50 %; harder work breathes faster
    // deeper the gas is denser (P bar: P times the surface's), each breath more work: divers breathe slower and deeper,
    // the exhale drawn out (a slow cycle grows about 7 % a bar; the tidal volume a little)
    const float Pb = pressureBar();
    const float period = cfg.f("breath.period", 4.6f) * (strong ? 0.7f : 1.0f) * (1.0f + 0.07f * std::max(Pb - 1.0f, 0.0f));
    const float tidal = 0.0015f * (1.0f + 0.04f * std::max(Pb - 1.0f, 0.0f));
    const float ph0 = breathPhase;
    breathPhase = std::fmod(breathPhase + dt / period, 1.0f);
    // the lungs: +-0.75 l about the mean (a relaxed tidal breath of 1.5 l): rho g dV
    auto lung = [](float ph) {   // 0..1 of the tidal volume
        if (ph < 0.40f) return 0.5f - 0.5f * std::cos(3.14159265f * ph / 0.40f);
        if (ph < 0.48f) return 1.0f;
        if (ph < 0.98f) return 0.5f + 0.5f * std::cos(3.14159265f * (ph - 0.48f) / 0.50f);
        return 0.0f;
    };
    buoyancy = 1025.0f * 9.81f * tidal * (lung(breathPhase) - 0.5f);
    // the exhale's gas leaves as bubbles: its volume at this depth is the tidal volume (the lungs at ambient pressure)
    const float exhaled = std::max(0.0f, lung(ph0) - lung(breathPhase)) * tidal;   // m3 this step (a cycle wrap gives 0)
    const KCamera& c = renderer.params().cam;
    const float depth0 = std::max(-c.pos.y, 0.0f);
    exhaleCarry += exhaled;
    auto rnd = [&](unsigned k) { unsigned x = k * 747796405u + 2891336453u; x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u; return float((x >> 22u) ^ x) / 4294967296.0f; };
    // each bubble's volume: a few big wobbling caps (r 8-25 mm) carry most of it, a cloud of small ones (1-5 mm) the rest
    while (exhaleCarry > 1e-6f && int(bubbleList.size()) < 400) {
        const unsigned id = ++bubbleSeq;
        const bool big = rnd(id * 3u) < 0.16f;
        const float r = big ? 0.008f + 0.017f * rnd(id * 5u) : 0.001f + 0.004f * rnd(id * 7u);
        const float vol = 4.18879f * r * r * r;
        if (vol > exhaleCarry && !bubbleList.empty()) break;
        exhaleCarry -= vol;
        Bubble B;
        const float side = rnd(id * 11u) < 0.5f ? -1.0f : 1.0f;   // the regulator's two exhausts, at the mouth's corners
        B.p = c.pos - c.up * 0.11f + c.fwd * 0.07f + c.right * (0.06f * side);
        B.v = c.right * (0.25f * side) - c.up * 0.05f + make_float3(0, 0.1f, 0);
        B.r0 = r;
        B.y0 = B.p.y;
        B.age = 0.0f;
        B.seed = rnd(id * 13u) * 100.0f;
        bubbleList.push_back(B);
    }
    if (exhaleCarry > 0.003f) exhaleCarry = 0.0f;
    // the motion: the exhaust's push dies in the water (~0.2 s); rise at the bubble's terminal speed (small: Stokes to
    // a quarter metre a second; caps: 0.711 sqrt(g d), Davies and Taylor), wobbling; Boyle as they rise; gone at the
    // surface or far off
    std::vector<Bubble> keep;
    keep.reserve(bubbleList.size());
    for (Bubble B : bubbleList) {
        B.age += dt;
        const float P0 = 1.013f + 0.1005f * std::max(-B.y0, 0.0f), P = 1.013f + 0.1005f * std::max(-B.p.y, 0.0f);
        const float r = B.r0 * std::cbrt(P0 / P);
        const float U = r < 0.0007f ? 0.12f : r < 0.005f ? 0.20f + 8.0f * (r - 0.0007f) : std::max(0.25f, 0.711f * std::sqrt(9.81f * 2.0f * r));
        const float k = std::exp(-dt / 0.2f);
        B.v = B.v * k + make_float3(0.0f, U, 0.0f) * (1.0f - k);
        const float w = (r > 0.005f ? 0.6f : 0.25f) * U;   // zigzag: bigger for the wobbling caps
        const float3 wob = make_float3(std::sin(B.age * 7.0f + B.seed), 0.0f, std::cos(B.age * 5.3f + 1.7f * B.seed)) * w;
        B.p = B.p + (B.v + wob) * dt;
        const float far = length(B.p - c.pos);
        if (B.p.y > -0.02f || far > 8.0f || B.age > 40.0f) continue;
        keep.push_back(B);
    }
    bubbleList.swap(keep);
    (void)depth0;
}

// the bubbles on the screen: the camera's own mapping inverted (the mask's flat port: tangential times n), far to near
void KoyApp::projectBubbles() {
    KoyParams& p = renderer.params();
    p.maskOn = cfg.i("view.mask", 1);
    p.bubbleGlint = cfg.f("view.bubble_glint", 40.0f);
    struct S { float4 b; float d, blur; };
    std::vector<S> list;
    const KCamera& c = p.cam;
    for (const Bubble& B : bubbleList) {
        const float3 v = B.p - c.pos;
        const float d = length(v);
        if (d < 0.05f) continue;
        const float3 dir = v * (1.0f / d);
        const float z = dot(dir, c.fwd);
        if (z < 0.05f) continue;
        float tx = dot(dir, c.right), ty = dot(dir, c.up);
        if (c.portN > 1.0f) { tx *= c.portN; ty *= c.portN; }
        const float t2 = tx * tx + ty * ty;
        if (t2 >= 0.999f) continue;
        const float zA = std::sqrt(1.0f - t2);
        const float u = tx / zA / c.tanHalfX, w = ty / zA / c.tanHalfY;
        const float px = (u + 1.0f) * 0.5f * float(width), py = (1.0f - w) * 0.5f * float(height);
        const float P0 = 1.013f + 0.1005f * std::max(-B.y0, 0.0f), P = 1.013f + 0.1005f * std::max(-B.p.y, 0.0f);
        const float r = B.r0 * std::cbrt(P0 / P);
        const float rpx = (c.portN > 1.0f ? c.portN : 1.0f) * (r / d) / zA * float(width) / (2.0f * c.tanHalfX);
        if (rpx < 0.35f) continue;   // below a pixel's third: lost in the water
        if (px < -rpx || py < -rpx || px > float(width) + rpx || py > float(height) + rpx) continue;
        const float squash = r > 0.006f ? 0.62f + 0.1f * std::sin(B.age * 9.0f + B.seed) : 1.0f;   // caps flatten and rock
        // the eye's defocus: nearer than its near point (~0.3 m; the mask shows things a quarter nearer) a blur circle of
        // the pupil's (4 mm) size seen through the defocus, in pixels
        const float dv = d / (c.portN > 1.0f ? c.portN : 1.0f), nearPt = 0.3f;
        const float blur = dv < nearPt ? 0.004f * (1.0f / dv - 1.0f / nearPt) * nearPt * float(width) / (2.0f * c.tanHalfX) : 0.0f;
        list.push_back({make_float4(px, py, rpx, squash), d, blur});
    }
    std::sort(list.begin(), list.end(), [](const S& a, const S& b) { return a.d > b.d; });
    if (list.size() > KOY_BUBBLES) list.erase(list.begin(), list.begin() + (list.size() - KOY_BUBBLES));   // keep the nearest
    p.bubbleCount = int(list.size());
    float4 box = make_float4(1e9f, 1e9f, -1e9f, -1e9f);
    for (size_t k = 0; k < list.size(); ++k) {
        p.bubbles[k] = list[k].b;
        p.bubbleBlur[k] = list[k].blur;
        const float ext = 1.25f * list[k].b.z + 2.0f * list[k].blur + 2.0f;
        box = make_float4(std::min(box.x, list[k].b.x - ext), std::min(box.y, list[k].b.y - ext), std::max(box.z, list[k].b.x + ext),
                          std::max(box.w, list[k].b.y + ext));
    }
    p.bubbleBox = box;
}

// PLAN_ADA §6. Above ppO2 1.6 bar the dose builds, faster the higher it is (8 s at 1.6, 3 s at 1.8: an art shortening of
// the real minutes, named in the plan); shallower it ebbs. Its symptoms: the field of view closing in. Full: the
// convulsion (the view black within 1.4 s), the restart at the ladder in the dark, waking over 2 s.
void KoyApp::stepBody(float dt) {
    KoyParams& p = renderer.params();
    const float limit = cfg.f("o2.limit", 1.6f), t0 = cfg.f("o2.seconds", 8.0f), k = cfg.f("o2.steep", 4.9f);
    if (blackTimer > 0.0f) {
        blackTimer += dt;
        const float tA = 1.4f, tB = 2.6f, tC = 4.6f;
        if (blackTimer < tA) {
            const float u = blackTimer / tA;
            p.tunnel = std::max(p.tunnel, u);
            p.blackout = u * u;
        } else if (blackTimer < tB) {
            p.tunnel = 1.0f;
            p.blackout = 1.0f;
            if (blackTimer - dt < 2.0f && blackTimer >= 2.0f) {
                restartDiver();
                ++restarts;
                setCamera();
            }
        } else if (blackTimer < tC) {
            const float u = (blackTimer - tB) / (tC - tB);
            p.blackout = 1.0f - u * u * (3.0f - 2.0f * u);
            p.tunnel = 0.7f * (1.0f - u);
        } else {
            blackTimer = 0.0f;
            p.tunnel = p.blackout = 0.0f;
        }
        return;
    }
    const float pp = ppO2();
    if (pp > limit) o2Dose += dt / (t0 * std::exp(-k * (pp - limit)));
    else o2Dose = std::max(0.0f, o2Dose - dt / 6.0f);
    const float u = std::min(std::max((o2Dose - 0.1f) / 0.8f, 0.0f), 1.0f);
    p.tunnel = 0.85f * u * u * (3.0f - 2.0f * u);
    p.blackout = 0.0f;
    if (o2Dose >= 1.0f) blackTimer = 1e-4f;
}

void KoyApp::stepWorld(float t) {
    worldTime = t;
    KoyParams& p = renderer.params();
    p.time = t;
    const double t0 = know();
    ocean.update(t, renderer.stream(), caustics);
    if (ripplesOn) ripples.update(t, renderer.stream(), false);
    // the floats ride the waves: the surface's height under each, then their instances and their ropes moved
    if (!world.buoys.floaters.empty()) {
        const std::vector<float2> pts = world.buoys.samplePoints();
        if (!dFloatPts) {
            cudaMalloc(reinterpret_cast<void**>(&dFloatPts), pts.size() * sizeof(float2));
            cudaMalloc(reinterpret_cast<void**>(&dFloatH), pts.size() * sizeof(float));
            cudaMemcpy(dFloatPts, pts.data(), pts.size() * sizeof(float2), cudaMemcpyHostToDevice);
        }
        koySampleHeights(ocean.surfaceTexture(), ocean.oset.patch, dFloatPts, int(pts.size()), dFloatH, renderer.stream());
        std::vector<float> h(pts.size());
        cudaMemcpyAsync(h.data(), dFloatH, h.size() * sizeof(float), cudaMemcpyDeviceToHost, renderer.stream());
        cudaStreamSynchronize(renderer.stream());
        std::vector<int> idx;
        std::vector<std::array<float, 12>> m;
        world.buoys.transforms(h, idx, m);
        renderer.setTransforms(idx, m);
    }
    cudaStreamSynchronize(renderer.stream());
    renderer.worldMs = float((know() - t0) * 1000.0);
}

void KoyApp::frame() {
    projectBubbles();
    renderer.renderFrame();
}

// The eye adapts halfway (PLAN §5.9): deeper and darker the exposure opens, but only half the difference.
void KoyApp::adaptExposure(float dt, bool instant) {
    const float key = std::log2(0.18f) + ev;
    const float ref = cfg.f("display.adapt_ref", -99.0f) > -50.0f ? cfg.f("display.adapt_ref", -99.0f) : renderer.meteredLog2();
    if (instant) adaptRef = ref;
    const float a = cfg.f("display.adapt", 0.5f);
    const float target = key - (a * renderer.meteredLog2() + (1.0f - a) * adaptRef);
    evAdapt = instant ? target : evAdapt + (target - evAdapt) * (1.0f - std::exp(-dt / cfg.f("display.adapt_seconds", 1.5f)));
    renderer.params().exposure = std::pow(2.0f, evAdapt);
}

void KoyApp::calibrateEye() {
    const Diver keep = diver;
    restartDiver();
    diver.yaw = cfg.f("display.calib_yaw_deg", 180.0f) * 3.14159265f / 180.0f;
    diver.pitch = cfg.f("display.calib_pitch_deg", -8.0f) * 3.14159265f / 180.0f;
    setCamera();
    frame();
    adaptExposure(0.0f, true);
    diver = keep;
    setCamera();
    frame();
    adaptExposure(1e6f, false);
}

void KoyApp::save(const std::string& prefix) {
    std::filesystem::create_directories(std::filesystem::path(prefix).parent_path());
    renderer.download(pixels);
    writePNG(prefix + ".png", int(width), int(height), reinterpret_cast<const unsigned char*>(pixels.data()));
}

void KoyApp::release() {
    if (bottomTex) cudaDestroyTextureObject(cudaTextureObject_t(bottomTex));
    if (floorMaskTex) cudaDestroyTextureObject(cudaTextureObject_t(floorMaskTex));
    if (floorMaskArr) cudaFreeArray(static_cast<cudaArray_t>(floorMaskArr));
    floorMaskTex = 0;
    floorMaskArr = nullptr;
    renderer.params().floorMask = 0;
    if (bottomArr) cudaFreeArray(static_cast<cudaArray_t>(bottomArr));
    bottomTex = 0;
    bottomArr = nullptr;
    renderer.params().bottomMap = 0;
    if (dFloatPts) cudaFree(dFloatPts);
    if (dFloatH) cudaFree(dFloatH);
    dFloatPts = nullptr;
    dFloatH = nullptr;
    ocean.release();
    ripples.release();
    world.release();
    optics.release();
}

// ---------------------------------------------------------------------------------------------------
namespace {
void printTimes(const KoyApp& app) {
    const KoyTimes& t = app.renderer.times();
    std::printf("gecisler: dunya %.2f (yuzey %.2f, kostik %.2f)  sutun %.2f  temel %.2f  kenar %.2f (%u piksel)  cozum %.2f  son %.2f  = %.2f ms\n",
                t.world, app.ocean.msSurface, app.ocean.msCaustic, t.shafts, t.base, t.edge, t.edgePixels, t.resolve, t.final, t.total());
}
float3 parse3(const std::string& s) {
    float x = 0, y = 0, z = 0;
    std::sscanf(s.c_str(), "%f %f %f", &x, &y, &z);
    return make_float3(x, y, z);
}
}   // namespace

int runKoyTests(KoyApp& app, const std::string& which);   // koy_tests.cpp

int runKoyCli(int argc, char** argv) {
    KoyApp app;
    std::string mode, out, path = "sabit", testName, mapOut;
    int frames = 120, refN = 4, timing = 0;
    float breathe = 0.0f;   // --nefes S: seconds of breathing before each still (the bubbles)
    float t0 = 0.0f, fps = 41.0f;
    bool camGiven = false, ref = false, worker = false, liveOnly = false, fromFloor = false;
    float3 camPos{}, camLook{};
    std::vector<float3> cams, looks;   // --doku and --yokla take several --cam/--look pairs
    int spp = -1, edge = -1, filter = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        auto opt = [&]() { return (i + 1 < argc && argv[i + 1][0] != '-') ? next() : std::string(); };
        if (a == "--scene") next();
        else if (a == "--headless") mode = "still";
        else if (a == "--ref") ref = true;
        else if (a == "--nefes") breathe = std::stof(next());
        else if (a == "--isci") worker = true;
        else if (a == "--kararlilik") mode = "stab";
        else if (a == "--canli") liveOnly = true;   // --kararlilik: the live pass alone (two runs that differ in one setting)
        else if (a == "--zaman") { mode = "time"; timing = std::stoi(next()); }
        else if (a == "--test") { mode = "test"; testName = opt(); }
        else if (a == "--map") { mode = "map"; mapOut = next(); }
        else if (a == "--doku") mode = "tex";
        else if (a == "--dalgacik") mode = "ripple";
        else if (a == "--yokla") mode = "probe";
        else if (a == "--kareler") mode = "views";
        else if (a == "--yerden") fromFloor = true;   // --cam / --look y: metres over the sand there
        else if (a == "--yol") path = next();
        else if (a == "--kare") frames = std::stoi(next());
        else if (a == "--refn") refN = std::stoi(next());
        else if (a == "--hiz") fps = std::stof(next());
        else if (a == "--t") t0 = std::stof(next());
        else if (a == "--cam") { camPos = parse3(next()); camGiven = true; cams.push_back(camPos); }
        else if (a == "--look") { camLook = parse3(next()); looks.push_back(camLook); }
        else if (a == "--spp") spp = std::stoi(next());
        else if (a == "--edge") edge = std::stoi(next());
        else if (a == "--filter") filter = std::stoi(next());
        else if (a == "--out") out = next();
        else if (a == "--width") app.width = unsigned(std::stoi(next()));
        else if (a == "--height") app.height = unsigned(std::stoi(next()));
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else { std::printf("bilinmeyen secenek %s\n", a.c_str()); return 2; }
    }
    if (mode.empty()) {
        std::printf("koy: --headless, --kararlilik, --zaman N, --doku, --yokla, --test, --map (canli sahne uygulamadan acilir)\n");
        return 2;
    }
    app.loadConfig();
    if (fromFloor) {   // the given heights are over the sand
        for (auto& c : cams) c.y += koyFloor(app.cfg, c.x, c.z);
        for (auto& l : looks) l.y += koyFloor(app.cfg, l.x, l.z);
        if (!cams.empty()) camPos = cams[0];
        if (!looks.empty()) camLook = looks[0];
    }
    if (spp > 0) app.quality.base = spp;
    if (edge > 0) app.quality.edge = edge;
    if (filter >= 0) app.quality.filter = filter;
    if (mode == "map") {
        // Ada from above (PLAN_ADA §4): the floor's depth (the same function as the mesh) shaded with contours every 5 m,
        // the island, the pier, the buoy lines, the stations, the start; 10 cm a pixel
        const float3 lo = app.cfg.v3("map.min", make_float3(-60.0f, 0.0f, -200.0f)), hi = app.cfg.v3("map.max", make_float3(60.0f, 0.0f, 100.0f));
        const float px = app.cfg.f("map.pixel", 0.1f);
        const int W = int((hi.x - lo.x) / px), H = int((hi.z - lo.z) / px);
        std::vector<unsigned char> img(size_t(W) * H * 4);
        const KoyFloorShape fs = KoyFloorShape::from(app.cfg);
        std::vector<float> dep(size_t(W) * H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) dep[size_t(y) * W + x] = -fs(lo.x + (x + 0.5f) * px, hi.z - (y + 0.5f) * px);
        auto put = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b) {
            if (x < 0 || y < 0 || x >= W || y >= H) return;
            unsigned char* q = &img[(size_t(y) * W + x) * 4];
            q[0] = r; q[1] = g; q[2] = b; q[3] = 255;
        };
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const float d = dep[size_t(y) * W + x];
                const float v = std::exp(-d / 18.0f);   // shallow light, deep dark
                const int band = int(d / 5.0f);
                const bool line = x + 1 < W && y + 1 < H && (int(dep[size_t(y) * W + x + 1] / 5.0f) != band || int(dep[size_t(y + 1) * W + x] / 5.0f) != band);
                const float o2 = 0.2095f * (1.013f + 0.1005f * d);
                unsigned char r = (unsigned char)(20 + 200 * v), g = (unsigned char)(50 + 190 * v), b = (unsigned char)(90 + 160 * v);
                if (o2 >= 1.6f) { r = (unsigned char)(r * 0.5f + 90); g = (unsigned char)(g * 0.5f); b = (unsigned char)(b * 0.5f); }
                if (line) { r = g = b = (band % 2 == 0) ? 255 : 160; }
                put(x, y, r, g, b);
            }
        auto toPx = [&](float wx, float wz, int& x, int& y) { x = int((wx - lo.x) / px); y = int((hi.z - wz) / px); };
        auto disc = [&](float wx, float wz, float rad, unsigned char r, unsigned char g, unsigned char b, bool ring) {
            int cx, cy;
            toPx(wx, wz, cx, cy);
            const int R = int(rad / px) + 1;
            for (int y = cy - R; y <= cy + R; ++y)
                for (int x = cx - R; x <= cx + R; ++x) {
                    const float dd = std::sqrt(float((x - cx) * (x - cx) + (y - cy) * (y - cy))) * px;
                    if (ring ? std::fabs(dd - rad) < 1.5f * px : dd <= rad) put(x, y, r, g, b);
                }
        };
        auto rect = [&](float x0, float z0, float x1, float z1, unsigned char r, unsigned char g, unsigned char b) {
            int ax, ay, bx, by;
            toPx(x0, z1, ax, ay);
            toPx(x1, z0, bx, by);
            for (int y = ay; y <= by; ++y)
                for (int x = ax; x <= bx; ++x) put(x, y, r, g, b);
        };
        const float3 ic = app.cfg.v3("island.centre", make_float3(0.0f, 0.0f, 60.0f));
        disc(ic.x, ic.z, app.cfg.f("island.radius", 35.0f), 120, 100, 70, false);
        for (const KoyPierSection& S : koyPierSpec(app.cfg).sections) {
            rect(S.x0, S.z0, S.x1, S.z1, 150, 110, 70);
            for (float px : S.pileX)
                for (float pz : S.pileZ) disc(px, pz, 0.25f, 90, 90, 90, false);
        }
        const float bh = app.cfg.f("buoy.half", 15.0f), bz0 = app.cfg.f("buoy.z0", 24.0f), bz1 = app.cfg.f("buoy.z1", -160.0f);
        for (float z = bz1; z <= bz0; z += 3.0f) {
            disc(-bh, z, 0.25f, 255, 210, 0, false);
            disc(bh, z, 0.25f, 255, 210, 0, false);
        }
        for (float x = -bh; x <= bh; x += 3.0f) disc(x, bz1, 0.25f, 255, 210, 0, false);
        const float3 sm = app.cfg.v3("samandira.pos", make_float3(0.0f, 0.0f, -160.0f));
        disc(sm.x, sm.z, 0.8f, 255, 120, 0, false);
        const float3 am = app.cfg.v3("amfora.centre", make_float3(0.0f, 0.0f, -50.0f));
        disc(am.x, am.z, 6.0f, 200, 90, 50, true);
        const float3 wc = app.cfg.v3("batik.centre", make_float3(2.0f, 0.0f, -95.0f));
        {   // the wreck's outline: its length along batik.deg
            const float a = app.cfg.f("batik.deg", 35.0f) * 3.14159265f / 180.0f, L = app.cfg.f("batik.length", 38.0f), B = app.cfg.f("batik.beam", 10.5f);
            for (float u = -0.5f * L; u <= 0.5f * L; u += 0.1f)
                for (float v = -0.5f * B; v <= 0.5f * B; v += 0.1f) {
                    const float taper = 1.0f - std::pow(std::fabs(2.0f * u / L), 3.0f);
                    if (std::fabs(v) > 0.5f * B * std::max(taper, 0.0f)) continue;
                    int x, y;
                    toPx(wc.x + std::sin(a) * u + std::cos(a) * v, wc.z - std::cos(a) * u + std::sin(a) * v, x, y);
                    put(x, y, 110, 70, 40);
                }
        }
        const float3 st = app.cfg.v3("diver.start", make_float3(1.9f, -1.2f, 0.8f));
        disc(st.x, st.z, 0.6f, 255, 255, 255, false);
        std::filesystem::create_directories(std::filesystem::path(mapOut).parent_path());
        writePNG(mapOut, W, H, img.data());
        std::printf("harita: %s (%d x %d, %.2f m piksel)\n", mapOut.c_str(), W, H, px);
        // the profile along the corridor's axis (x = 0): depth, slope, ppO2
        for (float z = 20.0f; z >= -260.0f; z -= 10.0f) {
            const float d = -fs(0.0f, z), d2 = -fs(0.0f, z - 1.0f);
            std::printf("  z %6.0f: derinlik %6.1f m, egim %4.1f derece, ppO2 %.2f bar\n", z, d, std::atan(d2 - d) * 57.2958f, 0.2095f * (1.013f + 0.1005f * d));
        }
        return 0;
    }
    app.renderer.init(app.width, app.height, app.root);
    std::printf("GPU: %s\n", app.renderer.deviceName().c_str());
    if (worker) {   // load on another thread, as the application does (load() runs on its worker)
        bool ok = false;
        std::thread th([&] { ok = app.compile(); if (ok) app.setup(); });
        th.join();
        if (!ok) return 1;
    } else {
        if (!app.compile()) return 1;
        app.setup();
    }
    if (mode == "test") {
        const int rc = runKoyTests(app, testName);
        app.release();
        app.renderer.shutdown();
        return rc;
    }
    if (camGiven) {
        app.diver.pos = camPos;
        const float3 f = normalize(camLook - camPos);
        app.diver.yaw = std::atan2(f.x, f.z);
        app.diver.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
        app.setCamera();
    }
    int rc = 0;
    app.renderer.params().view = app.cfg.i("view.mode", 0);
    auto aim = [&](size_t c) {   // the c-th --cam/--look pair
        app.diver.pos = cams[c];
        const float3 f = normalize((c < looks.size() ? looks[c] : cams[c] + make_float3(0, 0, -1)) - cams[c]);
        app.diver.yaw = std::atan2(f.x, f.z);
        app.diver.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
        app.setCamera();
    };
    auto nearest = [&](float3 p, float3& dir) {   // the probe's nearest static surface (26 directions, up to 2 m)
        float dist[26];
        app.renderer.probe(p, dist);
        int best = 0;
        for (int q = 1; q < 26; ++q)
            if (dist[q] < dist[best]) best = q;
        const int kk = best >= 13 ? best + 1 : best;
        dir = make_float3(float(kk % 3 - 1), float((kk / 3) % 3 - 1), float(kk / 9 - 1));
        return dist[best];
    };
    if (mode == "views") {
        // several views in one load: each --cam/--look pair, its own metered exposure (as the eye would settle there),
        // world time --t; <out>_NN.png
        const std::string o = out.empty() ? app.root + "/captures/koy/kareler" : out;
        std::filesystem::create_directories(std::filesystem::path(o).parent_path());
        // the eye's reference as the live scene sets it: metered at the start (the ladder), then each view adapted only
        // display.adapt of the way from there, settled (as after a few seconds of looking)
        app.stepWorld(t0);
        app.calibrateEye();
        for (size_t c = 0; c < cams.size(); ++c) {
            aim(c);
            app.bubbleList.clear();
            for (float tb = 0.0f; tb < breathe; tb += 1.0f / 60.0f) app.stepBreath(1.0f / 60.0f, false);
            app.stepWorld(t0);
            app.frame();
            app.adaptExposure(1e6f, false);
            app.frame();
            if (ref) app.renderer.renderReference(refN);
            char nm[16];
            std::snprintf(nm, sizeof(nm), "_%02d", int(c));
            app.save(o + nm);
            const KoyTimes& tm = app.renderer.times();
            std::printf("kare %d: (%.1f %.1f %.1f) derinlik %.1f m, kum %.2f m altta, pozlama %+.2f EV, %.1f ms (kenar %u)\n", int(c), cams[c].x, cams[c].y,
                        cams[c].z, -cams[c].y, cams[c].y - koyFloor(app.cfg, cams[c].x, cams[c].z), app.evAdapt, tm.total(), tm.edgePixels);
        }
    } else if (mode == "probe") {
        for (size_t c = 0; c < cams.size(); ++c) {
            float3 dir;
            const float d = nearest(cams[c], dir);
            std::printf("yokla (%.2f %.2f %.2f): kumun %.2f m ustunde, en yakin yuzey %.2f m (yon %+.0f %+.0f %+.0f)\n", cams[c].x, cams[c].y,
                        cams[c].z, cams[c].y - koyFloor(app.cfg, cams[c].x, cams[c].z), d, dir.x, dir.y, dir.z);
        }
    } else if (mode == "ripple") {
        // the same moment with the ripples off and on, one exposure (metered off): python tools/doku_kiyas.py <out>
        // --kipler kapali,acik
        const std::string o = out.empty() ? app.root + "/captures/koy/dalgacik/kiyas" : out;
        std::filesystem::create_directories(std::filesystem::path(o).parent_path());
        for (size_t c = 0; c < std::max<size_t>(1, cams.size()); ++c) {
            if (c < cams.size()) aim(c);
            app.setRipples(false);
            app.stepWorld(t0);
            app.frame();
            app.adaptExposure(0.0f, true);
            const float exposure = app.renderer.params().exposure;
            for (int on = 0; on < 2; ++on) {
                app.setRipples(on != 0);
                if (on && !app.ripplesOn) {
                    std::printf("dalgacik yok: atlandi\n");
                    continue;
                }
                app.stepWorld(t0);
                app.renderer.params().exposure = exposure;
                app.frame();
                char nm[48];
                std::snprintf(nm, sizeof(nm), "_%02d_%s", int(c), on ? "acik" : "kapali");
                app.save(o + nm);
            }
            std::printf("dalgacik kiyas %d: derinlik %.2f m -> %s_%02d_*.png\n", int(c), -app.diver.pos.y, o.c_str(), int(c));
        }
    } else if (mode == "tex") {
        // the same moment in the three texture modes (the panel's "Doku"), one exposure for all three (metered on
        // "Yüksek"): the pictures differ by the textures alone. python tools/doku_kiyas.py <out> measures them
        const std::string o = out.empty() ? app.root + "/captures/koy/doku/kiyas" : out;
        std::filesystem::create_directories(std::filesystem::path(o).parent_path());
        static const char* names[3] = {"standart", "yuksek", "ultra"};
        for (size_t c = 0; c < std::max<size_t>(1, cams.size()); ++c) {
            if (c < cams.size()) aim(c);
            app.setTextureMode(1);
            app.stepWorld(t0);
            app.frame();
            app.adaptExposure(0.0f, true);
            const float exposure = app.renderer.params().exposure;
            for (int m = 0; m < 3; ++m) {
                app.setTextureMode(m);
                if (app.world.texMode != m) {
                    std::printf("doku %s yuklu degil: atlandi\n", names[m]);
                    continue;
                }
                app.renderer.params().exposure = exposure;
                app.frame();
                char nm[48];
                std::snprintf(nm, sizeof(nm), "_%02d_%s", int(c), names[m]);
                app.save(o + nm);
            }
            float3 dir;
            const float d = nearest(app.diver.pos, dir);
            std::printf("doku kiyas %d: kamera kumun %.2f m ustunde, en yakin yuzey %.2f m -> %s_%02d_*.png\n", int(c),
                        app.diver.pos.y - koyFloor(app.cfg, app.diver.pos.x, app.diver.pos.z), d, o.c_str(), int(c));
        }
    } else if (mode == "still") {
        app.stepWorld(t0);
        app.frame();
        app.adaptExposure(0.0f, true);
        // display.exposure_fixed > 0: that exposure, not the metered one (two runs that differ in one setting compare
        // pixel for pixel)
        if (app.cfg.f("display.exposure_fixed", 0.0f) > 0.0f) app.renderer.params().exposure = app.cfg.f("display.exposure_fixed", 0.0f);
        app.frame();
        std::printf("pozlama carpani %.6f\n", app.renderer.params().exposure);
        printTimes(app);
        if (ref) app.renderer.renderReference(refN);
        const std::string o = out.empty() ? app.root + "/captures/koy/kare_" + stamp() : out;
        app.save(o);
        std::printf("yazildi: %s.png (pozlama %+.2f EV, olculen log2 Y %.2f)\n", o.c_str(), app.evAdapt, app.renderer.meteredLog2());
    } else if (mode == "time") {
        double sum = 0.0;
        for (float tb = 0.0f; tb < breathe; tb += 1.0f / 60.0f) app.stepBreath(1.0f / 60.0f, false);   // --nefes: bubbles there
        for (int k = 0; k < timing; ++k) {
            const double s = know();
            if (breathe > 0.0f) app.stepBreath(1.0f / fps, false);
            app.stepWorld(t0 + k / fps);
            app.frame();
            app.adaptExposure(1.0f / fps, k == 0);
            if (k >= 3) sum += know() - s;
        }
        std::printf("canli %ux%u: %.2f ms/kare duvar saati (%.1f fps)\n", app.width, app.height, 1000.0 * sum / std::max(1, timing - 3),
                    std::max(1, timing - 3) / sum);
        printTimes(app);
        if (!out.empty()) {
            app.save(out);
            std::printf("yazildi: %s.png (t %.2f s)\n", out.c_str(), app.worldTime);
        }
    } else if (mode == "stab") {
        // PLAN §9: the live frames of a path (as the window draws them) and the converged reference of each moment
        const std::string o = out.empty() ? app.root + "/captures/kararlilik/koy_" + path : out;
        std::filesystem::create_directories(o);
        setPNGCompression(1);   // hundreds of frames: fast zlib, bigger files
        const float dt = 1.0f / fps;
        struct Moment { Diver d; float t, exposure; };
        std::vector<Moment> at(frames);
        if (path == "yakin" || path == "kum") {
            // yakin: close to the boulder (about a metre), looking at it, gliding sideways along it; kum: 30 cm over the
            // sand, looking down at it, swimming forward (the 16K grain at its nearest: the hardest case for shimmer)
            const bool sand = path == "kum";
            app.diver.pos = sand ? app.cfg.v3("path.sand_pos", make_float3(1.0f, -6.5f, 3.0f)) : app.cfg.v3("path.near_pos", make_float3(-2.2f, -6.1f, -4.1f));
            const float3 look = sand ? app.cfg.v3("path.sand_look", make_float3(1.0f, -6.8f, 2.7f)) : app.cfg.v3("path.near_look", make_float3(-2.4f, -6.6f, -6.0f));
            const float3 f = normalize(look - app.diver.pos);
            app.diver.yaw = std::atan2(f.x, f.z);
            app.diver.pitch = std::asin(f.y);
            app.diver.vel = make_float3(0, 0, 0);
            app.setCamera();
        }
        if (!cams.empty()) aim(0);   // --cam / --look: the path starts there
        app.stepWorld(t0);
        app.frame();
        app.adaptExposure(0.0f, true);
        // display.exposure_fixed > 0: that exposure all the way (two runs that differ in one setting compare pixel for pixel)
        const float fixedExposure = app.cfg.f("display.exposure_fixed", 0.0f);
        if (fixedExposure > 0.0f) app.renderer.params().exposure = fixedExposure;
        double sum = 0.0;
        for (int k = 0; k < frames; ++k) {   // pass 1: live
            const float t = t0 + k * dt;
            // the path: still (the world moves), swimming forward, turning, looking up at the surface, along the boulder
            float fwd = 0, right = 0, up = 0, lookX = 0, lookY = 0;
            if (path == "yuz" || path == "kum") fwd = 1.0f;
            else if (path == "yakin") right = 1.0f;
            else if (path == "don") lookX = -dt * 12.0f / (app.cfg.f("diver.mouse", 0.0022f) * 57.2958f);   // 12 deg/s
            else if (path == "yukari" && k == 0) lookY = -(55.0f / 57.2958f) / app.cfg.f("diver.mouse", 0.0022f);
            app.stepDiver(fwd, right, up, false, lookX, lookY, dt);
            app.stepWorld(t);
            if (fixedExposure > 0.0f) app.renderer.params().exposure = fixedExposure;
            at[k] = {app.diver, t, app.renderer.params().exposure};
            const double s = know();
            app.frame();
            sum += know() - s;
            app.adaptExposure(dt, false);
            char nm[32];
            std::snprintf(nm, sizeof(nm), "/live_%04d", k);
            app.save(o + nm);
        }
        std::printf("kararlilik koy %s: %d canli kare, %.2f ms/kare\n", path.c_str(), frames, 1000.0 * sum / frames);
        printTimes(app);
        for (int k = 0; k < frames && !liveOnly; ++k) {   // pass 2: the reference
            app.diver = at[k].d;
            app.setCamera();
            app.stepWorld(at[k].t);
            app.renderer.params().exposure = at[k].exposure;
            app.renderer.renderReference(refN);
            char nm[32];
            std::snprintf(nm, sizeof(nm), "/ref_%04d", k);
            app.save(o + nm);
        }
        std::printf("kararlilik koy %s: %d referans (%d ornek) -> %s\n", path.c_str(), frames, 16 * refN * refN, o.c_str());
    }
    app.release();
    app.renderer.shutdown();
    return rc;
}
