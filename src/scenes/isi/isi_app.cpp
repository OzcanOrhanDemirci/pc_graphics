// Scene 9 "Isı" (draft): the run's state, the views, and the command-line tool that renders the composed views to PNG.
//   pc_graphics.exe --scene isi --headless --out captures/isi [--view 0..3|all] [--w 1280 --h 720]
//                   [--cpu 0..1 --ram 0..1 --ssd 0..1 --gpu 0..1 --energy 0..1 --t seconds]
#include "scenes/isi/isi_app.h"
#include "core/bloom.h"
#include "core/image_io.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {

constexpr float kWallZ = -230.0f, kSky = 360.0f;

// the same glowing run as the kernels' hot(): dark red, orange, yellow-white
float3 hotHost(float h) {
    h = std::clamp(h, 0.0f, 1.0f);
    const float3 c1 = make_float3(0.85f, 0.10f, 0.02f), c2 = make_float3(1.0f, 0.36f, 0.06f), c3 = make_float3(1.0f, 0.74f, 0.38f);
    return h < 0.5f ? lerp3(c1, c2, h * 2.0f) : lerp3(c2, c3, h * 2.0f - 1.0f);
}

unsigned hashU(unsigned v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return v;
}
float hashF(int a, int b) { return float(hashU(unsigned(a) * 73856093u ^ hashU(unsigned(b) * 19349663u)) >> 8) * (1.0f / 16777216.0f); }

template <class T>
T* devAlloc(size_t n) {
    T* p = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&p), n * sizeof(T));
    return p;
}

}   // namespace

int IsiApp::viewCount() { return 4; }

IsiCam IsiApp::view(int i) {
    IsiCam c;
    switch (i) {
    default:
    case 0:   // the balcony: the console under the eyes, the avenue of lava to the wall, the towers on the left, the pump on the right
        c.pos = make_float3(0.0f, 25.7f, 60.5f);
        c.yaw = 0.0f;
        c.pitch = -0.09f;
        c.fovY = 56.0f;
        break;
    case 1:   // at the wall's foot, looking up at the fans
        c.pos = make_float3(-34.0f, 7.5f, kWallZ + 52.0f);
        c.yaw = -0.22f;
        c.pitch = 0.40f;
        c.fovY = 62.0f;
        break;
    case 2:   // on the nearest tower's top, over the world
        c.pos = make_float3(-70.0f, 133.7f, 8.0f);
        c.yaw = -0.30f;
        c.pitch = -0.30f;
        c.fovY = 55.0f;
        break;
    case 3:   // low by the pump
        c.pos = make_float3(60.0f, 6.0f, -60.0f);
        c.yaw = -0.53f;
        c.pitch = 0.38f;
        c.fovY = 58.0f;
        break;
    }
    return c;
}

const char* IsiApp::viewName(int i, bool tr) {
    switch (i) {
    case 0: return tr ? "Balkon" : "The balcony";
    case 1: return tr ? "Duvarın dibi" : "Under the wall";
    case 2: return tr ? "Kulenin tepesi" : "The tower's top";
    default: return tr ? "Pompa" : "The pump";
    }
}

void IsiApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/isi.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    width = cfg.i("width", width);
    height = cfg.i("height", height);
}

void IsiApp::init() {
    hdr_ = devAlloc<float4>(size_t(width) * height);
    output = devAlloc<uchar4>(size_t(width) * height);
    int w = width, h = height;
    for (int k = 0; k < kLevels; ++k) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        levels_[k] = devAlloc<float4>(size_t(w) * h);
        accum_[k] = devAlloc<float4>(size_t(w) * h);
    }
    // the beams: some of the panel's holes (the same staggered grid the kernels draw), the room's light falling through
    // them, all leaning the same way
    std::vector<IsiBeam> beams;
    const float g = cfg.f("sky.hole", 30.0f), dz = g * 0.866f;
    const float frac = cfg.f("beam.fraction", 0.035f);
    const float3 dir = normalize(make_float3(cfg.f("beam.lean.x", 0.16f), -1.0f, cfg.f("beam.lean.z", 0.09f)));
    for (int row = int(std::floor(-1000.0f / dz)); row <= int(std::ceil(80.0f / dz)) && int(beams.size()) < kIsiMaxBeams; ++row) {
        const float parity = float(std::abs(row) % 2);
        for (int k = int(std::floor(-520.0f / g)); k <= int(std::ceil(540.0f / g)) && int(beams.size()) < kIsiMaxBeams; ++k) {
            if (hashF(row * 7919 + 13, k * 104729 + 7) >= frac) continue;
            IsiBeam b;
            b.top = make_float3((float(k) - 0.5f * parity) * g, kSky - 0.5f, float(row) * dz);
            b.dir = dir;
            b.sigma = cfg.f("beam.sigma", 1.1f);
            b.le = 0.6f + 0.8f * hashF(k, row);
            beams.push_back(b);
        }
    }
    beamCount_ = int(beams.size());
    beams_ = devAlloc<IsiBeam>(std::max(1, beamCount_));
    cudaMemcpy(beams_, beams.data(), beams.size() * sizeof(IsiBeam), cudaMemcpyHostToDevice);
    isiFindBeamLengths(beams_, beamCount_, 0);
    cudaDeviceSynchronize();
    cudaEventCreate(&ev0_);
    cudaEventCreate(&ev1_);
    bad_ = devAlloc<unsigned int>(1);
}

void IsiApp::release() {
    cudaFree(hdr_);
    cudaFree(output);
    for (int k = 0; k < kLevels; ++k) {
        cudaFree(levels_[k]);
        cudaFree(accum_[k]);
        levels_[k] = accum_[k] = nullptr;
    }
    cudaFree(beams_);
    cudaFree(bad_);
    bad_ = nullptr;
    if (ev0_) cudaEventDestroy(ev0_);
    if (ev1_) cudaEventDestroy(ev1_);
    hdr_ = nullptr;
    output = nullptr;
    beams_ = nullptr;
    ev0_ = ev1_ = nullptr;
}

void IsiApp::step(float dt) {
    // a part wakes over a second and a half, and goes to sleep a little slower: the world warming and cooling
    auto ease = [&](float& x, float target) {
        const float tau = target > x ? 1.5f : 2.5f;
        x += (target - x) * (1.0f - std::exp(-dt / tau));
    };
    ease(cpu, cpuOn);
    ease(ram, ramOn);
    ease(ssd, ssdOn);
    time += dt;
    // heavy and slow: a fan this size turning at its real speed would look frantic (the real one is on the gauge)
    fanAngle += dt * (0.25 + 0.9 * gpu);
    flow += dt * (5.0 + 12.0 * gpu);
}

void IsiApp::snapAwake() {
    cpu = cpuOn;
    ram = ramOn;
    ssd = ssdOn;
}

IsiView IsiApp::makeView() const {
    IsiView v;
    v.width = width;
    v.height = height;
    v.camPos = cam.pos;
    const float cp = std::cos(cam.pitch);
    v.camFwd = normalize(make_float3(-std::sin(cam.yaw) * cp, std::sin(cam.pitch), -std::cos(cam.yaw) * cp));
    v.camRight = normalize(cross(v.camFwd, make_float3(0.0f, 1.0f, 0.0f)));
    v.camUp = cross(v.camRight, v.camFwd);
    v.tanHalf = std::tan(0.5f * cam.fovY * 3.14159265f / 180.0f);
    v.pixAngle = 2.0f * v.tanHalf / float(height);
    v.time = float(time);
    v.fanAngle = float(std::fmod(fanAngle, 6.283185307179586));
    v.flow = float(std::fmod(flow, 3960.0));   // a multiple of every band's period (9, 11, 30 and 40 m)
    v.gpu = gpu;
    v.cpu = cpu;
    v.ram = ram;
    v.ssd = ssd;
    v.energy = energy;
    // the console's needles: the card's power its own reading; the rest the draft's switches (no load yet)
    const float gw = gpuLimit > 0.0f ? std::clamp(gpuWatts / gpuLimit, 0.0f, 1.0f) : gpu;
    v.gauge[0] = gw;
    v.gauge[1] = 0.04f + 0.86f * cpu;
    v.gauge[2] = 0.04f + 0.80f * ram;
    v.gauge[3] = 0.04f + 0.75f * ssd;
    v.gauge[4] = 0.10f + 0.78f * cpu;
    v.gauge[5] = 0.15f + 0.6f * (0.45f * gw + 0.3f * cpu + 0.15f * ram + 0.1f * ssd);
    v.gauge[6] = 0.08f + 0.85f * (0.45f * gw + 0.3f * cpu + 0.15f * ram + 0.1f * ssd);
    v.exposure = cfg.f("exposure", 7.0f) * std::exp2(ev);
    v.base = cfg.i("samples", 2);
    v.edge = cfg.i("samples.edge", 4);
    v.contrast = cfg.f("samples.contrast", 0.06f);
    v.maxSteps = cfg.i("steps", 320);
    v.fogDensity = cfg.f("fog.density", 0.0016f);
    v.fogHeight = cfg.f("fog.height", 130.0f);
    v.beamLe = cfg.f("beam.le", 30.0f);
    v.glowLe = cfg.f("glow.le", 1.0f);
    v.bloom = cfg.f("bloom", 0.06f);
    v.vignette = cfg.f("vignette", 0.25f);
    v.beams = beamCount_;
    v.skyHole = cfg.f("sky.hole", 30.0f);
    v.fogLe = cfg.f("fog.le", 1.0f);
    v.skyLe = cfg.f("sky.le", 1.0f);
    v.beam = beams_;
    v.badCount = bad_;
    // the hot parts' lights
    const float lg = cfg.f("light.gpu", 9000.0f) * gpu;
    for (int i = 0; i < 3; ++i) {
        v.light[i].pos = make_float3(-72.0f + 72.0f * i, 64.0f, kWallZ - 20.0f);
        v.light[i].color = hotHost(0.45f + 0.3f * gpu) * lg;
        v.light[i].soft = 22.0f;
    }
    v.light[3].pos = make_float3(98.0f, 52.0f, -125.0f);
    v.light[3].color = hotHost(0.6f) * (cfg.f("light.cpu", 5000.0f) * cpu);
    v.light[3].soft = 14.0f;
    v.light[4].pos = make_float3(132.0f, 3.0f, -195.0f);
    v.light[4].color = hotHost(0.3f) * (cfg.f("light.vrm", 6000.0f) * cpu);
    v.light[4].soft = 30.0f;
    v.light[5].pos = make_float3(-85.0f, 70.0f, -45.0f);
    v.light[5].color = make_float3(0.7f, 0.82f, 1.0f) * (cfg.f("light.ram", 5000.0f) * ram);
    v.light[5].soft = 40.0f;
    v.light[6].pos = make_float3(52.0f, 7.0f, -2.0f);
    v.light[6].color = make_float3(0.5f, 0.85f, 1.0f) * (cfg.f("light.ssd", 900.0f) * ssd);
    v.light[6].soft = 10.0f;
    const float total = 0.45f * gpu + 0.3f * cpu + 0.15f * ram + 0.1f * ssd;
    v.light[7].pos = make_float3(450.0f, 150.0f, -1000.0f);
    v.light[7].color = hotHost(0.2f) * (cfg.f("light.psu", 60000.0f) * (0.25f + 0.75f * total));
    v.light[7].soft = 80.0f;
    return v;
}

void IsiApp::render() {
    const IsiView v = makeView();
    cudaMemsetAsync(bad_, 0, sizeof(unsigned int), 0);
    cudaEventRecord(ev0_, 0);
    isiFrame(v, hdr_, 0);
    launchBloom(hdr_, width, height, levels_, accum_, kLevels, 0);
    isiResolve(v, hdr_, accum_[0], output, 0);
    cudaEventRecord(ev1_, 0);
    cudaEventSynchronize(ev1_);
    float ms = 0.0f;
    cudaEventElapsedTime(&ms, ev0_, ev1_);
    cudaMemcpy(&lastBad, bad_, sizeof(unsigned int), cudaMemcpyDeviceToHost);
    frameMs = frameMs <= 0.0f ? ms : frameMs + (ms - frameMs) * 0.1f;
}

bool IsiApp::save(const std::string& pathNoExt) {
    std::vector<unsigned char> px(size_t(width) * height * 4);
    cudaMemcpy(px.data(), output, px.size(), cudaMemcpyDeviceToHost);
    return writePNG(pathNoExt + ".png", width, height, px.data());
}

// ---------------------------------------------------------------------------------------------------------------
int runIsiCli(int argc, char** argv) {
    IsiApp a;
    a.root = PROJECT_ROOT;
    std::string out = "captures/isi", which = "all";
    int w = 0, h = 0;
    float t = 12.0f;
    bool headless = false, hasFree = false;
    IsiCam freeCam;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (s == "--headless") headless = true;
        else if (s == "--out") out = next();
        else if (s == "--view") which = next();
        else if (s == "--w") w = std::stoi(next());
        else if (s == "--h") h = std::stoi(next());
        else if (s == "--cpu") a.cpuOn = std::stof(next());
        else if (s == "--ram") a.ramOn = std::stof(next());
        else if (s == "--ssd") a.ssdOn = std::stof(next());
        else if (s == "--gpu") a.gpu = std::stof(next());
        else if (s == "--energy") a.energy = std::stof(next());
        else if (s == "--ev") a.ev = std::stof(next());
        else if (s == "--t") t = std::stof(next());
        else if (s == "--set") a.overrides.push_back(next());
        else if (s == "--root") a.root = next();
        else if (s == "--scene") next();
        else if (s == "--cam") {   // "x y z yaw pitch fov": one free view instead of the composed ones
            IsiCam c;
            if (std::sscanf(next().c_str(), "%f %f %f %f %f %f", &c.pos.x, &c.pos.y, &c.pos.z, &c.yaw, &c.pitch, &c.fovY) == 6) {
                freeCam = c;
                hasFree = true;
            }
        }
    }
    if (!headless) {
        std::printf("isi: --headless --out <klasor> [--view 0..3|all] [--w --h] [--cpu --ram --ssd --gpu --energy 0..1] [--t sn]\n");
        return 2;
    }
    a.loadConfig();
    if (w > 0) a.width = w;
    if (h > 0) a.height = h;
    a.init();
    a.snapAwake();
    a.time = t;
    a.fanAngle = t * (0.25 + 0.9 * a.gpu);
    a.flow = t * (5.0 + 12.0 * a.gpu);
    std::filesystem::create_directories(out);
    for (int i = 0; i < IsiApp::viewCount(); ++i) {
        if (hasFree ? i > 0 : (which != "all" && std::stoi(which) != i)) continue;
        a.cam = hasFree ? freeCam : IsiApp::view(i);
        a.frameMs = 0.0f;
        a.render();   // the first launch carries the kernels' loading; time the second
        a.frameMs = 0.0f;
        a.render();
        const std::string path = out + "/isi_bakis" + std::to_string(i);
        const bool ok = a.save(path);
        std::printf("bakis %d: %s  %.1f ms  (%dx%d)  bozuk piksel %u\n", i, ok ? (path + ".png").c_str() : "YAZILAMADI", a.frameMs, a.width, a.height,
                    a.lastBad);
    }
    const cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) std::printf("CUDA hatasi: %s\n", cudaGetErrorString(e));
    a.release();
    return e == cudaSuccess ? 0 : 1;
}
