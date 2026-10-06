// Scene 9 "Ayna": the run's state, the optics from measured constants, and the command-line tools.
//   pc_graphics.exe --scene ayna --headless --out captures/ayna [--view 0..n|all] [--cam "x y z yaw pitch fov"]
//                   [--w 2560 --h 1440] [--set key=value] [--debug 1|2] [--plan]
#include "scenes/ayna/ayna_app.h"
#include "core/bloom.h"
#include "core/image_io.h"
#include "shared/display.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace ayna;

namespace {

template <class T>
T* devAlloc(size_t n) {
    T* p = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&p), n * sizeof(T));
    return p;
}

using cd = std::complex<double>;

// unpolarised Fresnel reflectance from a medium of index n1 into one of complex index n2 (n + i k), cos(theta) in n1
double fresnel(double n1, cd n2, double cosi) {
    cosi = std::clamp(cosi, 0.0, 1.0);
    const double sini2 = 1.0 - cosi * cosi;
    const cd cost = std::sqrt(cd(1.0, 0.0) - (n1 * n1 * sini2) / (n2 * n2));
    const cd rs = (n1 * cosi - n2 * cost) / (n1 * cosi + n2 * cost);
    const cd rp = (n2 * cosi - n1 * cost) / (n2 * cosi + n1 * cost);
    return 0.5 * (std::norm(rs) + std::norm(rp));
}

// measured constants at the three channels' representative wavelengths (610, 550, 465 nm), see docs/ayna/DURUM.md:
//   float glass: M. Rubin, Sol. Energy Mater. 12, 275 (1985), "clear" (refractiveindex.info): n from his formula,
//                k tabulated -> absorption 4 pi k / lambda
//   silver:      P. B. Johnson, R. W. Christy, Phys. Rev. B 6, 4370 (1972), interpolated
//   chromium:    A. D. Rakić et al., Appl. Opt. 37, 5271 (1998), Brendel-Bormann fit, interpolated
const double kLambda[3] = {0.610, 0.550, 0.465};   // um
const double kGlassK[3] = {5.217e-7, 2.200e-7, 1.685e-7};
const double kAgN[3] = {0.058, 0.060, 0.0469}, kAgK[3] = {4.094, 3.597, 2.803};
const double kCrN[3] = {3.1935, 2.7892, 2.1410}, kCrK[3] = {4.266, 4.1977, 3.9299};
const double kStoneN = 1.57;   // polished marble (calcite's two indices 1.486 and 1.658, averaged)

double glassN(double lum) { return 1.5130 - 0.003169 * lum * lum + 0.003962 / (lum * lum); }

}   // namespace

// ---------------------------------------------------------------------------------------------------------------
int AynaApp::paletteCount() { return 8; }
float3 AynaApp::paletteColor(int i) {
    float3 c;
    switch ((i % 8 + 8) % 8) {
    default:
    case 0: c = make_float3(1.0f, 0.397f, 0.095f); break;    // warm white, 2700 K
    case 1: c = make_float3(1.0f, 1.0f, 1.0f); break;        // daylight, 6500 K (the white point)
    case 2: c = make_float3(1.0f, 0.05f, 0.02f); break;      // red
    case 3: c = make_float3(1.0f, 0.35f, 0.05f); break;      // orange
    case 4: c = make_float3(0.10f, 1.0f, 0.15f); break;      // green
    case 5: c = make_float3(0.05f, 0.80f, 1.0f); break;      // cyan
    case 6: c = make_float3(0.05f, 0.15f, 1.0f); break;      // blue
    case 7: c = make_float3(0.60f, 0.08f, 1.0f); break;      // violet
    }
    return c * (1.0f / luminance709(c));
}
const char* AynaApp::paletteName(int i, bool tr) {
    static const char* t[8] = {"Sıcak beyaz", "Gün ışığı", "Kırmızı", "Turuncu", "Yeşil", "Camgöbeği", "Mavi", "Mor"};
    static const char* e[8] = {"Warm white", "Daylight", "Red", "Orange", "Green", "Cyan", "Blue", "Violet"};
    return (tr ? t : e)[(i % 8 + 8) % 8];
}
int AynaApp::levelCount() { return 8; }
float AynaApp::levelValue(int i) { return 25.0f * std::exp2(float(std::clamp(i, 0, 7))); }

int AynaApp::viewCount() { return 4; }
AynaCam AynaApp::view(int i) {
    AynaCam c;
    switch (i) {
    default:
    case 0:   // the hall, from the cell U(0,0)
        c.pos = make_float3(0.80f, 1.30f, 0.46f);
        c.yaw = 0.0f;
        c.pitch = 0.0f;
        break;
    case 1:
        c.pos = make_float3(0.80f, 1.30f, 0.46f);
        c.yaw = 2.094f;
        c.pitch = -0.1f;
        break;
    case 2:
        c.pos = make_float3(0.80f, 1.30f, 0.46f);
        c.yaw = -2.094f;
        c.pitch = 0.15f;
        break;
    case 3:
        c.pos = make_float3(0.80f, 1.30f, 0.46f);
        c.yaw = 1.0f;
        c.pitch = -0.35f;
        break;
    }
    return c;
}
const char* AynaApp::viewName(int i, bool tr) {
    static const char* t[4] = {"Salon", "Salon 2", "Salon 3", "Zemin"};
    static const char* e[4] = {"The hall", "The hall 2", "The hall 3", "The floor"};
    return (tr ? t : e)[std::clamp(i, 0, 3)];
}

void AynaApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/ayna.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    width = cfg.i("width", width);
    height = cfg.i("height", height);
    debug = cfg.i("debug", debug);
}

void AynaApp::computeOptics() {
    std::memset(&optics, 0, sizeof(optics));
    const double g = cfg.f("maze.glass", 0.006f);
    optics.glassN = float(glassN(0.55));
    for (int s = 0; s < kOpticsN; ++s) {
        const double ci = double(s) / double(kOpticsN - 1);
        const double si = std::sqrt(std::max(0.0, 1.0 - ci * ci));
        for (int ch = 0; ch < 3; ++ch) {
            const double ng = glassN(kLambda[ch]);
            const double F = fresnel(1.0, cd(ng, 0.0), ci);
            const double st = si / ng, ct = std::sqrt(std::max(1e-12, 1.0 - st * st));
            const double Rag = fresnel(ng, cd(kAgN[ch], kAgK[ch]), ct);
            const double alpha = 4.0 * 3.14159265358979 * kGlassK[ch] / (kLambda[ch] * 1e-6);   // per metre
            const double T = std::exp(-alpha * 2.0 * g / ct);
            optics.mirror[ch][s] = float((1.0 - F) * (1.0 - F) * Rag * T);
            optics.chrome[ch][s] = float(fresnel(1.0, cd(kCrN[ch], kCrK[ch]), ci));
            if (ch == 1) optics.ghost[s] = float(F);
        }
        optics.stone[s] = float(fresnel(1.0, cd(kStoneN, 0.0), ci));
    }
    setOptics(optics);
}

void AynaApp::init() {
    Lattice lat;
    lat.L = cfg.f("maze.edge", 1.6f);
    lat.t = cfg.f("maze.wall", 0.12f);
    lat.rc = cfg.f("maze.column", 0.125f);
    lat.wHalf = 0.5f * cfg.f("maze.window", 1.23f);
    lat.ra = cfg.f("maze.arch", 0.66f);
    lat.yc = cfg.f("maze.arch.centre", 2.05f);
    lat.yk = lat.yc - std::sqrt(std::max(0.0f, lat.ra * lat.ra - lat.wHalf * lat.wHalf));
    lat.Hc = cfg.f("maze.ceiling", 3.4f);
    lat.glass = cfg.f("maze.glass", 0.006f);
    maze.build(lat, cfg.i("maze.radius", 3), unsigned(cfg.i("maze.seed", 7)), cfg.i("maze.loops", 4), cfg.i("maze.layout", 1));
    computeOptics();
    // the brass lanterns: hung over the middles of a few cells (cfg "lantern.cells": i j up, ...)
    lanterns.clear();
    {
        const float level = cfg.f("lantern.level", 6.0f), h = cfg.f("lantern.height", 2.55f);
        const int cellsList[4][3] = {{-1, 0, 1}, {-2, 1, 0}, {1, -2, 1}, {2, 0, 0}};
        for (int q = 0; q < cfg.i("lantern.count", 4) && q < 4; ++q) {
            if (!maze.inside(cellsList[q][0], cellsList[q][1], cellsList[q][2])) continue;
            const float2 cc = maze.cellCentre(cellsList[q][0], cellsList[q][1], cellsList[q][2]);
            AynaLamp lp;
            lp.pos = make_float3(cc.x, h, cc.y);
            // a candle's flame behind brass: about 1900 K
            lp.color = make_float3(1.0f, 0.33f, 0.06f) * (1.0f / luminance709(make_float3(1.0f, 0.33f, 0.06f)));
            lp.level = level;
            lanterns.push_back(lp);
        }
    }
    cudaDeviceSetLimit(cudaLimitStackSize, size_t(cfg.i("debug.stack", 1024)));

    hdr_ = devAlloc<float4>(size_t(width) * height);
    haze_ = devAlloc<float4>(size_t((width + 1) / 2) * ((height + 1) / 2));
    output = devAlloc<uchar4>(size_t(width) * height);
    int w = width, h = height;
    for (int k = 0; k < kLevels; ++k) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        levels_[k] = devAlloc<float4>(size_t(w) * h);
        accum_[k] = devAlloc<float4>(size_t(w) * h);
    }
    dEdges_ = devAlloc<unsigned char>(maze.edges.size());
    cudaMemcpy(dEdges_, maze.edges.data(), maze.edges.size(), cudaMemcpyHostToDevice);
    dColumns_ = devAlloc<unsigned char>(maze.columns.size());
    cudaMemcpy(dColumns_, maze.columns.data(), maze.columns.size(), cudaMemcpyHostToDevice);
    cellObj_.assign(size_t(cellCount(maze.g)) * kCellObjects, -1);
    dCellObj_ = devAlloc<signed char>(cellObj_.size());
    cudaMemcpy(dCellObj_, cellObj_.data(), cellObj_.size(), cudaMemcpyHostToDevice);
    cudaEventCreate(&ev0_);
    cudaEventCreate(&ev1_);
    bad_ = devAlloc<unsigned int>(1);
    oob_ = devAlloc<unsigned int>(1);
    cudaMemset(oob_, 0, 4);
    if (cfg.i("lightmap", 1)) buildLightMap();
}

// the light map's blocks: the floor and ceiling grids over the maze, then every face, reveal and column there is
void AynaApp::buildLightMap() {
    const Lattice& g = maze.g;
    const float ts = cfg.f("lightmap.texel", 0.01f);
    lm_ = LightMap();
    lm_.texel = ts;
    lmBlocks_.clear();
    const float R = float(maze.radius);
    lm_.x0 = -(R + 0.6f) * g.L;
    lm_.z0 = -(R + 0.2f) * g.h;
    lm_.gw = int(std::ceil(2.0f * (R + 0.6f) * g.L / ts));
    lm_.gh = int(std::ceil(2.0f * (R + 0.2f) * g.h / ts));
    size_t off = 0;
    LightMapBlock b;
    b.type = 0;
    b.offset = int(off);
    b.w = lm_.gw;
    b.h = lm_.gh;
    lm_.floorOff = b.offset;
    lmBlocks_.push_back(b);
    off += size_t(b.w) * b.h;
    b.type = 1;
    b.offset = int(off);
    lm_.ceilOff = b.offset;
    lmBlocks_.push_back(b);
    off += size_t(b.w) * b.h;
    const double thL = std::atan2(g.yk - g.yc, -g.wHalf) + 2.0 * 3.14159265358979, thR = std::atan2(g.yk - g.yc, g.wHalf);
    lm_.perimeter = float(2.0 * g.yk + g.ra * (thL - thR));
    lmFace_.assign(size_t(edgeCount(g)) * 2, -1);
    lmRev_.assign(size_t(edgeCount(g)) * 2, -1);
    lmCol_.assign(size_t(vertexCount(g)), -1);
    const int fw = int(std::ceil(g.L / ts)), fh = int(std::ceil(g.Hc / ts));
    const int rw = int(std::ceil(lm_.perimeter / ts)), rh = std::max(2, int(std::ceil(0.5f * g.t / ts)));
    for (int j = g.j0; j < g.j0 + g.nj; ++j)
        for (int i = g.i0; i < g.i0 + g.ni; ++i)
            for (int up = 0; up < 2; ++up) {
                if (!maze.inside(i, j, up)) continue;
                for (int k = 0; k < 3; ++k) {
                    const EdgeRef e = cellEdge(i, j, up, k);
                    const int ei = edgeIndex(g, e.fam, e.i, e.j);
                    const int slot = ei * 2 + (e.side > 0 ? 0 : 1);
                    if (lmFace_[slot] < 0) {
                        LightMapBlock f;
                        f.type = 2;
                        f.offset = int(off);
                        f.w = fw;
                        f.h = fh;
                        f.fam = e.fam;
                        f.i = e.i;
                        f.j = e.j;
                        f.side = e.side;
                        lmFace_[slot] = int(lmBlocks_.size());
                        lmBlocks_.push_back(f);
                        off += size_t(fw) * fh;
                        LightMapBlock r = f;
                        r.type = 3;
                        r.offset = int(off);
                        r.w = rw;
                        r.h = rh;
                        lmRev_[slot] = int(lmBlocks_.size());
                        lmBlocks_.push_back(r);
                        off += size_t(rw) * rh;
                    }
                }
                int ci[3], cj[3];
                cellCornerIdx(i, j, up, ci, cj);
                for (int k = 0; k < 3; ++k) {
                    const int vi = vertexIndex(g, ci[k], cj[k]);
                    if (vi < 0 || lmCol_[vi] >= 0) continue;
                    LightMapBlock c;
                    c.type = 4;
                    c.offset = int(off);
                    c.w = int(std::ceil(2.0f * 3.14159265f * g.rc / ts));
                    c.h = fh;
                    c.i = ci[k];
                    c.j = cj[k];
                    lmCol_[vi] = int(lmBlocks_.size());
                    lmBlocks_.push_back(c);
                    off += size_t(c.w) * c.h;
                }
            }
    lmTexels_ = off;
    lmMaxW_ = lmMaxH_ = 0;
    for (const LightMapBlock& bb : lmBlocks_) {
        lmMaxW_ = std::max(lmMaxW_, bb.w);
        lmMaxH_ = std::max(lmMaxH_, bb.h);
    }
    dLm_ = devAlloc<float4>(lmTexels_);
    cudaMemset(dLm_, 0, lmTexels_ * sizeof(float4));
    dLm2_ = devAlloc<float4>(lmTexels_);
    cudaMemset(dLm2_, 0, lmTexels_ * sizeof(float4));
    dLmBlocks_ = devAlloc<LightMapBlock>(lmBlocks_.size());
    cudaMemcpy(dLmBlocks_, lmBlocks_.data(), lmBlocks_.size() * sizeof(LightMapBlock), cudaMemcpyHostToDevice);
    dLmFace_ = devAlloc<int>(lmFace_.size());
    cudaMemcpy(dLmFace_, lmFace_.data(), lmFace_.size() * sizeof(int), cudaMemcpyHostToDevice);
    dLmRev_ = devAlloc<int>(lmRev_.size());
    cudaMemcpy(dLmRev_, lmRev_.data(), lmRev_.size() * sizeof(int), cudaMemcpyHostToDevice);
    dLmCol_ = devAlloc<int>(lmCol_.size());
    cudaMemcpy(dLmCol_, lmCol_.data(), lmCol_.size() * sizeof(int), cudaMemcpyHostToDevice);
    lm_.faceBlock = dLmFace_;
    lm_.revBlock = dLmRev_;
    lm_.colBlock = dLmCol_;
    lm_.blocks = dLmBlocks_;
    lm_.E = dLm_;
    lm_.E2 = dLm2_;
    lm_.on = 1;
    baked_ = 0;
}

// the lamps not yet in the map: each one's beams, baked into it (gathered lamps: the map starts again)
void AynaApp::bakeLamps() {
    std::vector<AynaLamp> all = lanterns;
    for (const AynaLamp& l : lamps) all.push_back(l);
    if (int(all.size()) < baked_) {
        cudaMemset(dLm_, 0, lmTexels_ * sizeof(float4));
        baked_ = 0;
        lampStraight_.clear();
    }
    if (int(all.size()) == baked_) return;
    const auto t0 = std::chrono::high_resolution_clock::now();
    View base = makeView();
    const float lampR = cfg.f("lamp.radius", 0.03f), lampK = cfg.f("lamp.share", 0.03f);
    const float eps = cfg.f("beam.eps", 0.01f) / std::max(1e-6f, base.exposure / 0.12f);
    std::vector<BeamNode> nodes;
    std::vector<int> cs, cn;
    std::vector<signed char> none(size_t(cellCount(maze.g)) * kCellObjects, -1);
    const int nLant = int(lanterns.size());
    for (; baked_ < int(all.size()) && baked_ < kMaxLamps; ++baked_) {
        const AynaLamp& lp = all[baked_];
        View v = base;
        v.nLights = 1;
        v.light[0] = Light();
        v.light[0].pos = lp.pos;
        v.light[0].radius = baked_ < nLant ? cfg.f("lantern.radius", 0.07f) : lampR;
        v.light[0].color = lp.color * (baked_ < nLant ? lp.level : lp.level * lampK);
        v.light[0].object = -1;
        v.nObjects = 0;
        v.bake = 1;
        v.lm = lm_;
        nodes.clear();
        BeamBuildStats st;
        buildBeams(maze, optics, v.light[0], 0, eps * cfg.f("lightmap.eps", 0.5f), cfg.i("beam.max", 60000), nodes, &st);
        sortIntoCells(maze, nodes, cs, cn);
        {
            // its straight beams (no mirror), for the mist: their parents are kept as indices into this list
            std::vector<BeamNode> st;
            std::vector<int> remap(nodes.size(), -1);
            for (size_t q = 0; q < nodes.size(); ++q) {
                const BeamNode& nd = nodes[q];
                if (nd.m[0] | nd.m[1] | nd.m[2]) continue;
                remap[q] = int(st.size());
                st.push_back(nd);
            }
            for (BeamNode& nd : st) nd.parent = nd.parent >= 0 ? remap[nd.parent] : -1;
            if (int(lampStraight_.size()) <= baked_) lampStraight_.resize(baked_ + 1);
            lampStraight_[baked_] = st;
        }
        BeamNode* dn = devAlloc<BeamNode>(std::max<size_t>(1, nodes.size()));
        int* dcs = devAlloc<int>(cs.size());
        int* dcn = devAlloc<int>(std::max<size_t>(1, cn.size()));
        cudaMemcpy(dn, nodes.data(), nodes.size() * sizeof(BeamNode), cudaMemcpyHostToDevice);
        cudaMemcpy(dcs, cs.data(), cs.size() * sizeof(int), cudaMemcpyHostToDevice);
        cudaMemcpy(dcn, cn.data(), cn.size() * sizeof(int), cudaMemcpyHostToDevice);
        v.nodes = dn;
        v.cellStart = dcs;
        v.cellNodes = dcn;
        v.nNodes = int(nodes.size());
        v.nEntries = int(cn.size() / 8);
        v.beamEps = eps * cfg.f("lightmap.eps", 0.5f);
        v.beams = bakeStraightOnly ? 2 : 1;
        bakeLightMap(v, dLmBlocks_, int(lmBlocks_.size()), lmMaxW_, lmMaxH_, 1u << baked_, 0);
        cudaDeviceSynchronize();
        cudaFree(dn);
        cudaFree(dcs);
        cudaFree(dcn);
    }
    bakeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
}

void AynaApp::release() {
    cudaFree(hdr_);
    cudaFree(haze_);
    haze_ = nullptr;
    cudaFree(output);
    for (int k = 0; k < kLevels; ++k) {
        cudaFree(levels_[k]);
        cudaFree(accum_[k]);
        levels_[k] = accum_[k] = nullptr;
    }
    cudaFree(dEdges_);
    cudaFree(dColumns_);
    cudaFree(dCellObj_);
    cudaFree(dLm_);
    cudaFree(dLm2_);
    dLm2_ = nullptr;
    cudaFree(dLmBlocks_);
    cudaFree(dLmFace_);
    cudaFree(dLmRev_);
    cudaFree(dLmCol_);
    dLm_ = nullptr;
    dLmBlocks_ = nullptr;
    dLmFace_ = dLmRev_ = dLmCol_ = nullptr;
    lm_ = LightMap();
    baked_ = 0;
    cudaFree(dNodes_);
    cudaFree(dCellNodes_);
    cudaFree(dCellStart_);
    cudaFree(dHazeNodes_);
    cudaFree(dHazeStart_);
    dHazeNodes_ = dHazeStart_ = nullptr;
    capHaze_ = 0;
    lampStraight_.clear();
    dNodes_ = nullptr;
    dCellNodes_ = nullptr;
    dCellStart_ = nullptr;
    capNodes_ = capCellNodes_ = 0;
    cudaFree(bad_);
    if (ev0_) cudaEventDestroy(ev0_);
    if (ev1_) cudaEventDestroy(ev1_);
    hdr_ = nullptr;
    output = nullptr;
    dEdges_ = dColumns_ = nullptr;
    dCellObj_ = nullptr;
    bad_ = nullptr;
    ev0_ = ev1_ = nullptr;
}

float3 AynaApp::forward() const {
    const float cp = std::cos(cam.pitch);
    return normalize(make_float3(-std::sin(cam.yaw) * cp, std::sin(cam.pitch), -std::cos(cam.yaw) * cp));
}

View AynaApp::makeView() const {
    View v;
    v.width = width;
    v.height = height;
    v.camPos = cam.pos;
    v.camFwd = forward();
    v.camRight = normalize(cross(v.camFwd, make_float3(0.0f, 1.0f, 0.0f)));
    v.camUp = cross(v.camRight, v.camFwd);
    v.tanHalf = std::tan(0.5f * cam.fovY * 3.14159265f / 180.0f);
    v.pixAngle = 2.0f * v.tanHalf / float(height);
    v.g = maze.g;
    v.edges = dEdges_;
    v.columns = dColumns_;
    v.cellObj = dCellObj_;
    v.exposure = cfg.f("exposure", 0.12f) * evScale;
    v.bloom = cfg.f("bloom", 0.04f);
    v.vignette = cfg.f("vignette", 0.2f);
    v.samples = cfg.i("samples", 4);
    v.minWeight = cfg.f("min.weight", 1.0f / 4096.0f);
    v.maxSteps = cfg.i("steps", 1500);
    v.debug = debug;
    v.beams = cfg.i("beams", 1);
    v.hazeSigma = cfg.f("haze.sigma", 0.0f);
    v.hazeAbsorb = cfg.f("haze.absorb", 0.0f);
    v.hazeMinWeight = cfg.f("haze.min.weight", 0.03f);
    v.hazeBuf = haze_;
    v.hazeOnly = cfg.i("debug.hazeonly", 0);
    v.plainMat = cfg.i("debug.plain", 0);
    v.coneScale = cfg.f("cone.scale", 0.5f);
    v.testMirror = testMirror;
    v.tmN = tmN;
    v.tmD = tmD;
    v.badCount = bad_;
    v.oob = oob_;
    v.stats = statsDev;
    // our sphere: chrome, the eye at its centre
    const float R = cfg.f("sphere.radius", 0.175f);
    v.obj[0].c = cam.pos;
    v.obj[0].r = R;
    v.obj[0].kind = 0;
    v.nObjects = 1;
    // our light: a spot from the slit in the sphere's front
    if (lightOn) {
        Light& L = v.light[v.nLights++];
        L.pos = cam.pos + v.camFwd * (R + 0.002f);
        L.axis = v.camFwd;
        L.radius = 0.01f;
        L.color = paletteColor(colorIndex) * levelValue(levelIndex);
        L.cosOuter = std::cos(cfg.f("spot.outer", 28.0f) * 3.14159265f / 180.0f);
        L.cosInner = std::cos(cfg.f("spot.inner", 12.0f) * 3.14159265f / 180.0f);
        L.object = -1;
    }
    // the lamps left behind
    const float lampR = cfg.f("lamp.radius", 0.03f), lampK = cfg.f("lamp.share", 0.03f);
    v.lm = lm_;
    const int nLant = int(lanterns.size());
    const float lantR = cfg.f("lantern.radius", 0.07f);
    v.nLamps = std::min(nLant + int(lamps.size()), std::min(baked_, kMaxLamps));
    for (int i = 0; i < v.nLamps; ++i) {
        const bool lant = i < nLant;
        const AynaLamp& l = lant ? lanterns[i] : lamps[i - nLant];
        v.lamp[i] = make_float4(l.pos.x, l.pos.y, l.pos.z, lant ? lantR : lampR);
        v.lampI[i] = l.color * (lant ? l.level : l.level * lampK);
    }
    v.firstLamp = v.nLights;
    for (const AynaLamp& lp : lanterns) {
        if (v.nLights >= kMaxLights || v.nObjects >= kMaxObjects) break;
        Object& ob = v.obj[v.nObjects];
        ob.c = lp.pos;
        ob.r = lantR;
        ob.kind = 2;
        ob.light = v.nLights;
        Light& L = v.light[v.nLights++];
        L.pos = lp.pos;
        L.radius = lantR;
        L.color = lp.color * lp.level;
        L.object = v.nObjects++;
    }
    for (const AynaLamp& lp : lamps) {
        if (v.nLights >= kMaxLights || v.nObjects >= kMaxObjects) break;
        Object& ob = v.obj[v.nObjects];
        ob.c = lp.pos;
        ob.r = lampR;
        ob.kind = 1;
        ob.light = v.nLights;
        Light& L = v.light[v.nLights++];
        L.pos = lp.pos;
        L.radius = lampR;
        L.color = lp.color * (lp.level * lampK);
        L.object = v.nObjects++;
    }
    return v;
}

// each cell lists the objects whose sphere reaches into it (plan circle against the cell's triangle, loosely)
void AynaApp::uploadObjects(const View& v) {
    std::fill(cellObj_.begin(), cellObj_.end(), (signed char)-1);
    const Lattice& g = maze.g;
    for (int id = 0; id < v.nObjects; ++id) {
        const Object& ob = v.obj[id];
        const float reach = ob.r + 0.02f;
        int ci, cj, cu;
        locate(g, ob.c.x, ob.c.z, ci, cj, cu);
        for (int j = cj - 2; j <= cj + 2; ++j)
            for (int i = ci - 2; i <= ci + 2; ++i)
                for (int up = 0; up < 2; ++up) {
                    const int idx = cellIndex(g, i, j, up);
                    if (idx < 0) continue;
                    // distance from the circle's centre to the triangle
                    float2 c[3];
                    cellCorners(g, i, j, up, c);
                    bool inside = true;
                    float dmin = 1e30f;
                    for (int k = 0; k < 3; ++k) {
                        const float2 a = c[k], b = c[(k + 1) % 3];
                        const float2 ab = make_float2(b.x - a.x, b.y - a.y), ap = make_float2(ob.c.x - a.x, ob.c.z - a.y);
                        const float cr = ab.x * ap.y - ab.y * ap.x;
                        const float s = std::clamp((ap.x * ab.x + ap.y * ab.y) / (ab.x * ab.x + ab.y * ab.y), 0.0f, 1.0f);
                        const float dx = ap.x - ab.x * s, dz = ap.y - ab.y * s;
                        dmin = std::min(dmin, std::sqrt(dx * dx + dz * dz));
                        if ((up ? cr : cr) < 0.0f) inside = false;
                    }
                    // the corner order is counter-clockwise in (x, z) for both kinds: a point inside has all cross >= 0
                    if (!inside && dmin > reach) continue;
                    for (int s = 0; s < kCellObjects; ++s)
                        if (cellObj_[size_t(idx) * kCellObjects + s] < 0) {
                            cellObj_[size_t(idx) * kCellObjects + s] = (signed char)id;
                            break;
                        }
                }
    }
    cudaMemcpy(dCellObj_, cellObj_.data(), cellObj_.size(), cudaMemcpyHostToDevice);
}

void AynaApp::uploadBeams(View& v) {
    nodes_.clear();
    beamStats = BeamBuildStats();
    const float eps = cfg.f("beam.eps", 0.01f) / std::max(1e-6f, v.exposure / 0.12f);
    const int maxNodes = cfg.i("beam.max", 60000);
    setBeamBulge(cfg.i("beam.bulge", 1) != 0);
    double ms = 0.0;
    for (int l = 0; l < v.nLights; ++l) {
        if (lm_.on && l >= v.firstLamp && l - v.firstLamp < baked_) continue;   // baked into the light map (lanterns, lamps)
        BeamBuildStats st;
        buildBeams(maze, optics, v.light[l], l, eps, maxNodes, nodes_, &st);
        beamStats.nodes += st.nodes;
        beamStats.cut += st.cut;
        beamStats.maxDepth = std::max(beamStats.maxDepth, st.maxDepth);
        ms += st.ms;
    }
    const size_t surfaceNodes = nodes_.size();
    // the lamps' straight beams join for the mist only (their light on surfaces is in the map)
    if (lm_.on) {
        for (int i = 0; i < baked_ && i < int(lampStraight_.size()); ++i) {
            const int base = int(nodes_.size());
            for (BeamNode nd : lampStraight_[i]) {
                nd.light = short(v.firstLamp + i);
                if (nd.parent >= 0) nd.parent += base;
                nodes_.push_back(nd);
            }
        }
    }
    // the mist's lists: every node once per cell (our light's and the lamps' straight ones)
    {
        const int nc = cellCount(maze.g);
        hazeStart_.assign(nc + 1, 0);
        for (const BeamNode& n : nodes_)
            if (n.cell >= 0) ++hazeStart_[n.cell + 1];
        for (int c = 0; c < nc; ++c) hazeStart_[c + 1] += hazeStart_[c];
        hazeNodes_.assign(hazeStart_[nc], 0);
        std::vector<int> fill(hazeStart_.begin(), hazeStart_.end() - 1);
        for (int q = 0; q < int(nodes_.size()); ++q)
            if (nodes_[q].cell >= 0) hazeNodes_[fill[nodes_[q].cell]++] = q;
    }
    // the surface lists only from our own nodes (sortIntoCells looks at all: hide the lamps' with cell -1 meanwhile)
    std::vector<int> savedCells;
    for (size_t q = surfaceNodes; q < nodes_.size(); ++q) {
        savedCells.push_back(nodes_[q].cell);
        nodes_[q].cell = -1;
    }
    sortIntoCells(maze, nodes_, cellStart_, cellNodes_);
    for (size_t q = surfaceNodes; q < nodes_.size(); ++q) nodes_[q].cell = savedCells[q - surfaceNodes];
    if (hazeNodes_.size() > capHaze_ || !dHazeStart_) {
        cudaFree(dHazeNodes_);
        cudaFree(dHazeStart_);
        capHaze_ = hazeNodes_.size() * 2 + 1024;
        dHazeNodes_ = devAlloc<int>(capHaze_);
        dHazeStart_ = devAlloc<int>(hazeStart_.size());
    }
    cudaMemcpy(dHazeNodes_, hazeNodes_.data(), hazeNodes_.size() * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(dHazeStart_, hazeStart_.data(), hazeStart_.size() * sizeof(int), cudaMemcpyHostToDevice);
    v.hazeStart = dHazeStart_;
    v.hazeNodes = dHazeNodes_;
    if (nodes_.size() > capNodes_) {
        cudaFree(dNodes_);
        capNodes_ = nodes_.size() * 2 + 1024;
        dNodes_ = devAlloc<BeamNode>(capNodes_);
    }
    if (cellNodes_.size() > capCellNodes_) {
        cudaFree(dCellNodes_);
        capCellNodes_ = cellNodes_.size() * 2 + 1024;
        dCellNodes_ = devAlloc<int>(capCellNodes_);
    }
    if (!dCellStart_) dCellStart_ = devAlloc<int>(cellStart_.size());
    cudaMemcpy(dNodes_, nodes_.data(), nodes_.size() * sizeof(BeamNode), cudaMemcpyHostToDevice);
    cudaMemcpy(dCellNodes_, cellNodes_.data(), cellNodes_.size() * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(dCellStart_, cellStart_.data(), cellStart_.size() * sizeof(int), cudaMemcpyHostToDevice);
    v.nodes = dNodes_;
    v.cellStart = dCellStart_;
    v.cellNodes = dCellNodes_;
    v.nNodes = int(nodes_.size());
    v.nEntries = int(cellNodes_.size() / 8);
    v.beamEps = eps;
    beamStats.ms = ms;
}

// the tools: after each stage, wait and name the first CUDA error
static bool stageCheck(bool on, const char* stage) {
    if (!on) return true;
    const cudaError_t e = cudaDeviceSynchronize();
    if (e != cudaSuccess) {
        std::fprintf(stderr, "ayna: asama '%s': %s\n", stage, cudaGetErrorString(e));
        return false;
    }
    return true;
}

void AynaApp::render() {
    const bool chk = cfg.i("debug.sync", 0) != 0;
    if (lm_.on) bakeLamps();
    if (!stageCheck(chk, "pisirme")) return;
    View v = makeView();
    uploadObjects(v);
    const auto tb = std::chrono::high_resolution_clock::now();
    if (v.beams > 0) uploadBeams(v);
    if (!stageCheck(chk, "huzme yukleme")) return;
    if (v.beams == 3 && lm_.on) {
        cudaEvent_t d0, d1;
        cudaEventCreate(&d0);
        cudaEventCreate(&d1);
        cudaEventRecord(d0, 0);
        View bv = v;
        bv.bake = 2;
        bakeDynamicMap(bv, dLmBlocks_, int(lmBlocks_.size()), lmMaxW_, lmMaxH_, 0);
        cudaEventRecord(d1, 0);
        cudaEventSynchronize(d1);
        cudaEventElapsedTime(&dynMs, d0, d1);
        cudaEventDestroy(d0);
        cudaEventDestroy(d1);
    }
    if (!stageCheck(chk, "isigimizin haritasi")) return;
    if (chk) {
        // the haze pass and the frame apart
        View hz = v;
        if (hz.hazeSigma > 0.0f) {
            View only = v;
            renderFrame(only, hdr_, 0);
            if (!stageCheck(true, "kare (pus + yuzeyler)")) {
                std::fprintf(stderr, "  dugum %zu, pus listesi %zu, kamera (%.4f %.4f %.4f) %.3f %.3f\n", nodes_.size(), hazeNodes_.size(), cam.pos.x,
                             cam.pos.y, cam.pos.z, cam.yaw, cam.pitch);
                return;
            }
        }
    }
    beamMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - tb).count();
    cudaMemsetAsync(bad_, 0, sizeof(unsigned int), 0);
    cudaEventRecord(ev0_, 0);
    renderFrame(v, hdr_, 0);
    launchBloom(hdr_, width, height, levels_, accum_, kLevels, 0);
    resolveFrame(v, hdr_, accum_[0], output, 0);
    cudaEventRecord(ev1_, 0);
    cudaEventSynchronize(ev1_);
    float ms = 0.0f;
    cudaEventElapsedTime(&ms, ev0_, ev1_);
    cudaMemcpy(&lastBad, bad_, sizeof(unsigned int), cudaMemcpyDeviceToHost);
    const cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess && !errorShown_) {
        errorShown_ = true;
        std::fprintf(stderr, "ayna: CUDA hatasi '%s'  kamera (%.4f %.4f %.4f) yaw %.3f pitch %.3f  lamba %zu  dugum %d\n", cudaGetErrorString(err),
                     cam.pos.x, cam.pos.y, cam.pos.z, cam.yaw, cam.pitch, lamps.size(), beamStats.nodes);
    }
    frameMs = frameMs <= 0.0f ? ms : frameMs + (ms - frameMs) * 0.1f;
}

void AynaApp::probe(float x, float y) {
    View v = makeView();
    uploadObjects(v);
    if (v.beams > 0) uploadBeams(v);
    probePixel(v, x, y);
}

bool AynaApp::save(const std::string& pathNoExt) {
    std::vector<unsigned char> px(size_t(width) * height * 4);
    cudaMemcpy(px.data(), output, px.size(), cudaMemcpyDeviceToHost);
    return writePNG(pathNoExt + ".png", width, height, px.data());
}

// ---------------------------------------------------------------------------------------------------------------
// Ö2: in corridor A (the strip between the lines z = h and z = 2h, both walls mirrors) a point X and the eye at the
// same height; the ray that should meet the n-th image of X is solved for analytically (n slabs of glass 2g thick
// crossed in the unfolded picture, Snell in each), then the tracer follows that ray through n mirrors: how far from X
// it passes, in pixels at 2560 x 1440 and the default field of view.
static int testO2(AynaApp& a) {
    a.loadConfig();
    a.init();
    const Lattice& g = a.maze.g;
    // the corridor's walls: silver planes z = h and z = 2h; the glass fronts g inside them
    const float zLo = g.h, zHi = 2.0f * g.h;
    // the windows of both walls overlap (staggered by L/2) over x in [L + 0.185, L + 0.615] (cell i = 0): eye and X there
    // (cell i = -2: the walls f0(-2,1) and f0(-2,2); the doors f0(-1,1) and f0(0,2) are elsewhere)
    const float cB = -1.0f * g.L, cT = -0.5f * g.L;
    const float xa = cT - g.wHalf, xb = cB + g.wHalf;
    const float y = 1.30f;
    const float3 eye = make_float3(xa + 0.08f, y, zLo + 0.45f);
    const float3 X = make_float3(xa + 0.30f, y, zLo + 0.90f);
    const double n_g = a.optics.glassN, gl = g.glass;
    const double D = zHi - zLo;   // silver to silver
    View v = a.makeView();
    // no objects: the tracer only meets the walls (the ray passes through X's place)
    std::vector<signed char> none(size_t(cellCount(g)) * kCellObjects, -1);
    signed char* dNone = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&dNone), none.size());
    cudaMemcpy(dNone, none.data(), none.size(), cudaMemcpyHostToDevice);
    v.cellObj = dNone;
    const double pixAngle = 2.0 * std::tan(0.5 * a.cam.fovY * 3.14159265358979 / 180.0) / 1440.0;
    std::printf("O2: goz (%.3f %.3f %.3f), X (%.3f %.3f %.3f), koridor genisligi (gumus-gumus) %.4f m, cam %.1f mm n=%.4f\n", eye.x, eye.y,
                eye.z, X.x, X.y, X.z, D, gl * 1000.0, n_g);
    (void)xb;
    double worst = 0.0;
    std::vector<float3> pts(64);
    for (int n = 1; n <= 40; ++n) {
        // the n-th image, unfolded: first towards the far wall (+z): the planes crossed are zHi, then zLo, zHi, ...
        // perpendicular distance in the unfolded picture from the eye to X_n
        double dz;
        // reflect X about the walls alternately: images at z' = 2 zHi - X.z, 2 zHi - (2 zLo - X.z)... a closed form:
        // after n crossings starting with zHi, X_n.z = X.z + 2 D * ceil(n/2) * ... ; simplest: build it
        double xz = X.z;
        double planes[2] = {zHi, zLo};
        for (int k = 0; k < n; ++k) {
            // unfolded: each crossing mirrors the remaining world about the current plane; the planes themselves move
            const double pl = (k % 2 == 0) ? planes[0] + double(k) * D : planes[0] + double(k) * D;
            xz = 2.0 * pl - xz;
            (void)pl;
        }
        // in the unfolded picture the k-th plane crossed is at zHi + k D; X_n.z after n crossings:
        // X_n = 2 (zHi + (n-1) D) - X_{n-1} ... (built above as xz)
        dz = xz - eye.z;
        const double du = X.x - eye.x;
        // solve du = (dz - 2 n g) tan(th) + 2 n g tan(th_t) for th
        auto lateral = [&](double th) {
            const double st = std::sin(th) / n_g, ct = std::sqrt(1.0 - st * st);
            return (dz - 2.0 * n * gl) * std::tan(th) + 2.0 * n * gl * st / ct;
        };
        double lo = -1.5, hi = 1.5;
        for (int it = 0; it < 200; ++it) {
            const double mid = 0.5 * (lo + hi);
            if ((lateral(mid) - du) * (lateral(lo) - du) <= 0.0) hi = mid;
            else lo = mid;
        }
        const double th = 0.5 * (lo + hi);
        const float3 d = normalize(make_float3(float(std::sin(th)), 0.0f, float(std::cos(th))));
        const int got = traceMirrorPoints(v, eye, d, n, pts.data());
        if (got < n) {
            std::printf("  n=%2d: isin %d aynada kaldi (sutun ya da kemer)\n", n, got);
            continue;
        }
        // the last segment: from the n-th mirror point, the direction after n reflections (n even: +z start again)
        const float3 last = pts[n - 1];
        // the exit point after the glass shift and the direction: recompute like the kernel
        const double ci = std::cos(th);
        const double st = std::sin(th) / n_g, ct = std::sqrt(1.0 - st * st);
        const double shift = 2.0 * gl * st / ct;
        float3 q = make_float3(float(last.x + shift * (d.x > 0 ? 1.0 : -1.0) * (std::fabs(d.x) > 0 ? 1.0 : 0.0)), last.y, last.z);
        (void)ci;
        const float3 dn = (n % 2 == 1) ? make_float3(d.x, d.y, -d.z) : d;
        // closest approach of q + s dn to X
        const float3 w = X - q;
        const float s = dot(w, dn);
        const float3 miss = w - dn * s;
        double total = 0.0;
        {
            float3 prev = eye;
            for (int k = 0; k < n; ++k) {
                total += length(pts[k] - prev);
                prev = pts[k];
            }
            total += s;
        }
        const double px = length(miss) / total / pixAngle;
        worst = std::max(worst, px);
        if (n <= 3 || n % 5 == 0) std::printf("  n=%2d: yol %.3f m, X'ten gecis %.2e m = %.4f piksel\n", n, total, double(length(miss)), px);
    }
    std::printf("O2 en kotu: %.4f piksel (hedef < 0,1)\n", worst);
    cudaFree(dNone);
    a.release();
    return worst < 0.1 ? 0 : 1;
}

// Ö1: sharpness. The same view straight (A) and seen in a mirror plane 5 cm before the eye (B: the eye mirrored behind
// the plane, looking back at it; the samples mirrored too). With an ideal mirror B is A mirrored, pixel for pixel: the
// reflection blurs nothing (the footprint, the filters, the light are the same). With the maze's own mirror (glass and
// silver), B / W(theta) against A: what is left is the glass's own sideways shift (its apparent depth).
static int testO1(AynaApp& a) {
    a.overrides.push_back("haze.sigma=0");   // the mist's half-resolution grid does not mirror onto itself (said in DURUM)
    a.loadConfig();
    a.width = a.cfg.i("o1.w", 1280);
    a.height = a.cfg.i("o1.h", 720);
    a.init();
    AynaLamp lp;
    lp.pos = make_float3(-1.10f, 1.10f, 2.10f);
    lp.color = AynaApp::paletteColor(0);
    lp.level = AynaApp::levelValue(5);
    a.lamps.push_back(lp);
    // the eye in corridor A, looking along it (6 m and more of arches and columns ahead)
    a.cam.pos = make_float3(-3.3f, 1.3f, 2.05f);
    a.cam.yaw = -1.5708f;   // towards +x
    a.cam.pitch = 0.0f;   // level: the mirrored eye looks straight back
    a.cam.fovY = 40.0f;
    a.lightOn = false;
    const float3 D = a.forward();
    a.testMirror = 0;
    a.render();
    a.render();
    const std::vector<float4> A = a.hdrHost();
    a.save("captures/ayna/o1_A");
    double worst = 0;
    for (int mode = 1; mode <= 2; ++mode) {
        // the plane 5 cm ahead, facing the eye; the mirrored eye 10 cm ahead, looking back
        const float3 P0 = a.cam.pos + D * 0.05f;
        const float3 n = D * -1.0f;
        AynaCam saved = a.cam;
        a.cam.pos = saved.pos + D * 0.10f;
        a.cam.yaw = saved.yaw + 3.14159265f;
        a.cam.pitch = saved.pitch;
        a.testMirror = mode;
        a.tmN = make_float3(-n.x, -n.y, -n.z);   // the face the mirrored eye looks at
        a.tmD = dot(a.tmN, P0);
        a.render();
        const std::vector<float4> B = a.hdrHost();
        a.save(mode == 1 ? "captures/ayna/o1_B_ideal" : "captures/ayna/o1_B_ayna");
        a.cam = saved;
        a.testMirror = 0;
        // B mirrored left-right against A (pitch keeps up and down); with the real mirror, B over W(theta) of each pixel
        double sumA = 0, sumE = 0, maxRel = 0;
        long long over1 = 0, n0 = 0;
        const float tanHalf = std::tan(0.5f * a.cam.fovY * 3.14159265f / 180.0f), aspect = float(a.width) / a.height;
        for (int y = 0; y < a.height; ++y)
            for (int x = 0; x < a.width; ++x) {
                const float4 pa = A[size_t(y) * a.width + x], pb = B[size_t(y) * a.width + (a.width - 1 - x)];
                float w = 1.0f;
                if (mode == 2) {
                    const float sx = ((x + 0.5f) / a.width * 2.0f - 1.0f) * tanHalf * aspect, sy = (1.0f - (y + 0.5f) / a.height * 2.0f) * tanHalf;
                    const float ci = 1.0f / std::sqrt(1.0f + sx * sx + sy * sy);   // the ray's angle to the plane's normal (pitch small)
                    const float xx = std::clamp(ci, 0.0f, 1.0f) * (kOpticsN - 1);
                    const int i0 = std::min(int(xx), kOpticsN - 2);
                    w = a.optics.mirror[1][i0] + (a.optics.mirror[1][i0 + 1] - a.optics.mirror[1][i0]) * (xx - i0);
                }
                const double la = 0.2126 * pa.x + 0.7152 * pa.y + 0.0722 * pa.z, lb = (0.2126 * pb.x + 0.7152 * pb.y + 0.0722 * pb.z) / w;
                sumA += la;
                sumE += std::fabs(la - lb);
                const double rel = std::fabs(la - lb) / std::max(la, 1e-3);
                maxRel = std::max(maxRel, rel);
                if (rel > 0.01) ++over1;
                ++n0;
            }
        std::printf("O1 %s: ortalama |fark| / ortalama parlaklik %.6f, en buyuk bagil %.4f, %%1'i asan piksel %.3f %%\n",
                    mode == 1 ? "ideal ayna" : "labirentin aynasi (/W)", sumE / sumA, maxRel, 100.0 * double(over1) / double(n0));
        if (mode == 1) worst = sumE / sumA;
    }
    a.release();
    return worst < 1e-3 ? 0 : 1;
}

// Ö4: one lamp's light on the floor, the beams (baked into the light map) against photons through the same tracer.
// Floor bins of 5 cm; the map's texels averaged in each (valid floor only); straight and by mirrors apart.
static int testO4(AynaApp& a) {
    a.overrides.push_back("lantern.count=0");
    a.loadConfig();
    a.width = 64;
    a.height = 36;
    a.init();
    const float3 lampPos = make_float3(a.cfg.f("o4.x", -0.55f), a.cfg.f("o4.y", 1.3f), a.cfg.f("o4.z", 1.9f));
    const long long total = (long long)(a.cfg.f("o4.photons", 2e8f));
    const float bin = 0.05f;
    const LightMap& lm = a.lightMap();
    const float x0 = lm.x0, z0 = lm.z0;
    const int gw = int(lm.gw * lm.texel / bin), gh = int(lm.gh * lm.texel / bin);
    const int per = int(std::lround(bin / lm.texel));
    // the photons
    View v = a.makeView();
    std::vector<signed char> none(size_t(cellCount(a.maze.g)) * kCellObjects, -1);
    signed char* dNone = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&dNone), none.size());
    cudaMemcpy(dNone, none.data(), none.size(), cudaMemcpyHostToDevice);
    v.cellObj = dNone;
    const float3 I = AynaApp::paletteColor(1) * (AynaApp::levelValue(6) * a.cfg.f("lamp.share", 0.03f));
    float *gd = nullptr, *gm = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&gd), size_t(gw) * gh * 4);
    cudaMalloc(reinterpret_cast<void**>(&gm), size_t(gw) * gh * 4);
    cudaMemset(gd, 0, size_t(gw) * gh * 4);
    cudaMemset(gm, 0, size_t(gw) * gh * 4);
    const auto t0 = std::chrono::high_resolution_clock::now();
    tracePhotons(v, lampPos, I, total, gd, gm, x0, z0, bin, gw, gh);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
    std::vector<float> hd(size_t(gw) * gh), hm(size_t(gw) * gh);
    cudaMemcpy(hd.data(), gd, hd.size() * 4, cudaMemcpyDeviceToHost);
    cudaMemcpy(hm.data(), gm, hm.size() * 4, cudaMemcpyDeviceToHost);
    // the beams: the full bake, then a straight-only bake, read back from the floor's block
    auto bakeFloor = [&](bool straight) {
        a.clearLightMap();
        a.overrides.push_back(std::string("o4.straight=") + (straight ? "1" : "0"));
        a.lamps.clear();
        AynaLamp lp;
        lp.pos = lampPos;
        lp.color = AynaApp::paletteColor(1);
        lp.level = AynaApp::levelValue(6);
        a.lamps.push_back(lp);
        a.bakeStraightOnly = straight;
        a.bakeLampsPublic();
        std::vector<float4> f(size_t(lm.gw) * lm.gh);
        cudaMemcpy(f.data(), lm.E + lm.floorOff, f.size() * sizeof(float4), cudaMemcpyDeviceToHost);
        return f;
    };
    const std::vector<float4> full = bakeFloor(false), str = bakeFloor(true);
    double sumRefD = 0, sumMapD = 0, errD = 0, sumRefM = 0, sumMapM = 0, errM = 0;
    int bins = 0;
    double maxRef = 0;
    for (int bz = 0; bz < gh; ++bz)
        for (int bx = 0; bx < gw; ++bx) maxRef = std::max(maxRef, double(hd[size_t(bz) * gw + bx] + hm[size_t(bz) * gw + bx]) / (bin * bin));
    std::vector<unsigned char> img(size_t(gw) * gh * 4, 0);
    for (int bz = 0; bz < gh; ++bz)
        for (int bx = 0; bx < gw; ++bx) {
            double md = 0, mm = 0;
            int nv = 0;
            for (int ty = 0; ty < per; ++ty)
                for (int tx = 0; tx < per; ++tx) {
                    const int X = bx * per + tx, Z = bz * per + ty;
                    if (X >= lm.gw || Z >= lm.gh) continue;
                    const float4 F = full[size_t(Z) * lm.gw + X], S = str[size_t(Z) * lm.gw + X];
                    unsigned bits;
                    std::memcpy(&bits, &F.w, 4);
                    if (!(bits & (1u << 30))) continue;
                    md += S.y;
                    mm += F.y - S.y;
                    ++nv;
                }
            if (nv < per * per) continue;   // only bins wholly on visible floor
            md /= nv;
            mm /= nv;
            const double rd = hd[size_t(bz) * gw + bx] / (bin * bin), rm = hm[size_t(bz) * gw + bx] / (bin * bin);
            sumRefD += rd;
            sumMapD += md;
            errD += std::fabs(rd - md);
            sumRefM += rm;
            sumMapM += mm;
            errM += std::fabs(rm - mm);
            ++bins;
            unsigned char* o = &img[(size_t(gh - 1 - bz) * gw + bx) * 4];
            const double e = (mm + md) - (rm + rd);
            o[0] = (unsigned char)std::min(255.0, std::max(0.0, -e / maxRef * 2550.0));
            o[1] = (unsigned char)std::min(255.0, std::max(0.0, e / maxRef * 2550.0));
            o[2] = (unsigned char)std::min(255.0, (rm + rd) / maxRef * 255.0 * 4.0);
            o[3] = 255;
        }
    writePNG("captures/ayna/o4_fark.png", gw, gh, img.data());
    std::printf("O4: lamba (%.2f %.2f %.2f), %lld foton %.0f ms, %d kutu (5 cm)\n", lampPos.x, lampPos.y, lampPos.z, total, ms, bins);
    std::printf("  dogrudan:  toplam harita/foton %.4f, kutu basina agirlikli hata %.4f -> uyum %.4f\n", sumMapD / sumRefD, errD / sumRefD,
                1.0 - errD / sumRefD);
    std::printf("  aynalardan: toplam harita/foton %.4f, kutu basina agirlikli hata %.4f -> uyum %.4f\n", sumMapM / sumRefM, errM / sumRefM,
                1.0 - errM / sumRefM);
    std::printf("  aynalarin payi (foton): %.1f %% ; hepsi: uyum %.4f\n", 100.0 * sumRefM / (sumRefM + sumRefD),
                1.0 - (errD + errM) / (sumRefD + sumRefM));
    cudaFree(gd);
    cudaFree(gm);
    cudaFree(dNone);
    a.release();
    return 0;
}

int runAynaCli(int argc, char** argv) {
    AynaApp a;
    a.root = PROJECT_ROOT;
    std::string out = "captures/ayna", which = "all";
    int w = 0, h = 0;
    bool headless = false, hasFree = false, plan = false;
    std::string test;
    std::vector<float2> probes;
    AynaCam freeCam;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (s == "--headless") headless = true;
        else if (s == "--out") out = next();
        else if (s == "--view") which = next();
        else if (s == "--w") w = std::stoi(next());
        else if (s == "--h") h = std::stoi(next());
        else if (s == "--set") a.overrides.push_back(next());
        else if (s == "--root") a.root = next();
        else if (s == "--scene") next();
        else if (s == "--plan") plan = true;
        else if (s == "--probe") {   // "x y" at the output's size: the first surface there and its light, both ways
            float x = 0, y = 0;
            if (std::sscanf(next().c_str(), "%f %f", &x, &y) == 2) probes.push_back(make_float2(x, y));
        }
        else if (s == "--test") { test = next(); headless = true; }
        else if (s == "--debug") a.overrides.push_back("debug=" + next());
        else if (s == "--lamp") {   // "x y z colour level": a lamp left there
            AynaLamp lp;
            int ci = 0;
            float lv = 4;
            if (std::sscanf(next().c_str(), "%f %f %f %d %f", &lp.pos.x, &lp.pos.y, &lp.pos.z, &ci, &lv) == 5) {
                lp.color = AynaApp::paletteColor(ci);
                lp.level = AynaApp::levelValue(int(lv));
                a.lamps.push_back(lp);
            }
        } else if (s == "--light") {   // "colour level on"
            int ci = 0, lv = 4, on = 1;
            if (std::sscanf(next().c_str(), "%d %d %d", &ci, &lv, &on) == 3) {
                a.colorIndex = ci;
                a.levelIndex = lv;
                a.lightOn = on != 0;
            }
        } else if (s == "--cam") {   // "x y z yaw pitch fov": one free view instead of the composed ones
            AynaCam c;
            if (std::sscanf(next().c_str(), "%f %f %f %f %f %f", &c.pos.x, &c.pos.y, &c.pos.z, &c.yaw, &c.pitch, &c.fovY) == 6) {
                freeCam = c;
                hasFree = true;
            }
        }
    }
    if (test == "o2") return testO2(a);
    if (test == "fuzz") {   // random poses (fixed seed) inside the maze, lamps left and gathered: CUDA must stay clean
        a.loadConfig();
        a.width = w > 0 ? w : 320;
        a.height = h > 0 ? h : 180;
        a.init();
        unsigned st = 12345u;
        auto rnd = [&]() {
            st ^= st << 13;
            st ^= st >> 17;
            st ^= st << 5;
            return float(st >> 8) * (1.0f / 16777216.0f);
        };
        const float R = a.cfg.f("sphere.radius", 0.175f);
        int fails = 0;
        const int poses = a.cfg.i("fuzz.poses", 400);
        for (int k = 0; k < poses && fails < 3; ++k) {
            float3 p = make_float3((rnd() * 2.0f - 1.0f) * 4.5f, 0.3f + rnd() * 2.8f, (rnd() * 2.0f - 1.0f) * 4.0f);
            if (!a.maze.insidePoint(p.x, p.z)) continue;
            a.maze.collide(p, R);
            if (!a.maze.insidePoint(p.x, p.z)) continue;
            a.cam.pos = p;
            a.cam.yaw = rnd() * 6.2832f;
            a.cam.pitch = (rnd() * 2.0f - 1.0f) * 1.4f;
            if (rnd() < 0.3f && a.lamps.size() < 8) {
                AynaLamp lp;
                const float3 f = make_float3(-std::sin(a.cam.yaw), 0.0f, -std::cos(a.cam.yaw));
                float3 q = p + f * 0.32f;
                a.maze.collide(q, 0.04f);
                if (!a.maze.insidePoint(q.x, q.z)) q = p;
                lp.pos = q;
                lp.color = AynaApp::paletteColor(int(rnd() * 8));
                lp.level = AynaApp::levelValue(int(rnd() * 8));
                a.lamps.push_back(lp);
            }
            if (rnd() < 0.05f) a.lamps.clear();
            a.colorIndex = int(rnd() * 8);
            a.levelIndex = int(rnd() * 8);
            a.lightOn = rnd() < 0.85f;
            a.render();
            const cudaError_t e = cudaGetLastError();
            if (e != cudaSuccess) {
                ++fails;
                std::printf("HATA %d: %s  kamera \"%.4f %.4f %.4f %.4f %.4f 62\"  isik %d %d %d  lamba %zu\n", k, cudaGetErrorString(e), p.x, p.y, p.z,
                            a.cam.yaw, a.cam.pitch, a.colorIndex, a.levelIndex, a.lightOn ? 1 : 0, a.lamps.size());
                for (const AynaLamp& lp : a.lamps) std::printf("   --lamp \"%.4f %.4f %.4f\"\n", lp.pos.x, lp.pos.y, lp.pos.z);
                break;   // the context is gone after an illegal access
            }
        }
        std::printf("fuzz: %s\n", fails ? "HATA BULUNDU" : "temiz"); std::printf("(%d poz)\n", poses);
        a.release();
        return fails ? 1 : 0;
    }
    if (test == "o4") return testO4(a);
    if (test == "o1") return testO1(a);
    if (test == "o3") {
        // Ö3: a walk along corridor A turning slowly, our light on, a lamp, the lanterns, the mist: the live frames
        // (4 samples) and the same moments with 16 (the reference), for tools/kararlilik.py
        a.loadConfig();
        a.width = w > 0 ? w : 1280;
        a.height = h > 0 ? h : 720;
        a.init();
        AynaLamp lp;
        lp.pos = make_float3(-1.10f, 1.10f, 2.10f);
        lp.color = AynaApp::paletteColor(5);
        lp.level = AynaApp::levelValue(5);
        a.lamps.push_back(lp);
        const std::string dir = out + "/o3";
        std::filesystem::create_directories(dir);
        const int frames = a.cfg.i("o3.frames", 48);
        for (int f = 0; f < frames; ++f) {
            const float t = float(f) / 30.0f * a.cfg.f("o3.speed", 1.0f);   // 30 frames a second
            a.cam.pos = make_float3(-3.2f + 0.9f * t, 1.3f + 0.05f * std::sin(t * 2.0f), 2.05f + 0.10f * std::sin(t * 1.3f));
            a.cam.yaw = -1.25f - 0.35f * t;
            a.cam.pitch = 0.05f * std::sin(t * 1.7f);
            char name[64];
            for (int pass = 0; pass < 2; ++pass) {
                a.overrides.push_back(pass == 0 ? "samples=" + std::to_string(a.cfg.i("o3.live", 4)) : std::string("samples=16"));
                a.loadConfig();
                a.width = w > 0 ? w : 1280;
                a.height = h > 0 ? h : 720;
                a.render();
                std::snprintf(name, sizeof(name), "/%s_%04d", pass == 0 ? "live" : "ref", f);
                a.save(dir + name);
                a.overrides.pop_back();
            }
        }
        std::printf("O3: %d kare -> %s (python tools/kararlilik.py %s)\n", frames, dir.c_str(), dir.c_str());
        a.release();
        return 0;
    }
    if (test == "lambalar") {   // lamps left one at a time, a frame between each: CUDA must stay clean
        a.loadConfig();
        if (w > 0) a.width = w;
        if (h > 0) a.height = h;
        a.init();
        a.cam = AynaApp::view(0);
        int bad = 0;
        for (int k = 0; k < 10; ++k) {
            if (k < 9) {
                AynaLamp lp;
                lp.pos = a.cam.pos + make_float3(0.0f, -0.1f * k, -0.32f);
                lp.color = AynaApp::paletteColor(k);
                lp.level = AynaApp::levelValue(4);
                if (int(a.lamps.size()) < 8) a.lamps.push_back(lp);
            } else {
                a.lamps.clear();
            }
            a.render();
            const cudaError_t e = cudaGetLastError();
            std::printf("adim %d: %zu lamba, kare %.1f ms, pisirme %.1f ms, %s\n", k, a.lamps.size(), a.frameMs, a.bakeMs,
                        e == cudaSuccess ? "temiz" : cudaGetErrorString(e));
            if (e != cudaSuccess) ++bad;
        }
        a.release();
        return bad ? 1 : 0;
    }
    if (!headless) {
        std::printf("ayna: --headless --out <klasor> [--view 0..n|all] [--cam \"x y z yaw pitch fov\"] [--w --h] [--set k=v] [--plan]\n");
        return 2;
    }
    a.loadConfig();
    if (w > 0) a.width = w;
    if (h > 0) a.height = h;
    a.init();
    std::filesystem::create_directories(out);
    std::printf("View: cpp %zu / cu %zu, hazeBuf %zu / %zu\n", sizeof(View), viewSizeCu(), offsetof(View, hazeBuf), viewHazeOffsetCu());
    std::printf("labirent: %d ayna, %d gecit, hucre %d\n", a.maze.mirrorCount(), a.maze.openCount(),
                int(std::count(a.maze.cells.begin(), a.maze.cells.end(), (unsigned char)1)));
    if (plan) {
        const std::string p = out + "/ayna_plan.png";
        std::printf("plan: %s\n", a.maze.savePlan(p, 80) ? p.c_str() : "YAZILAMADI");
    }
    for (int i = 0; i < AynaApp::viewCount(); ++i) {
        if (hasFree ? i > 0 : (which != "all" && std::stoi(which) != i)) continue;
        a.cam = hasFree ? freeCam : AynaApp::view(i);
        a.frameMs = 0.0f;
        a.render();   // the first launch carries the kernels' loading; then the median of several
        std::vector<float> times;
        for (int r = 0; r < std::max(1, a.cfg.i("bench", 7)); ++r) {
            a.frameMs = 0.0f;
            a.render();
            times.push_back(a.frameMs);
        }
        std::sort(times.begin(), times.end());
        a.frameMs = times[times.size() / 2];
        for (const float2& pr : probes) a.probe(pr.x, pr.y);
        if (a.debug == 3 || a.debug == 4) {
            unsigned long long* st = nullptr;
            cudaMalloc(reinterpret_cast<void**>(&st), 16);
            cudaMemset(st, 0, 16);
            a.statsDev = st;
            a.render();
            unsigned long long h[2];
            cudaMemcpy(h, st, 16, cudaMemcpyDeviceToHost);
            const double px = double(a.width) * a.height * std::max(1, a.cfg.i("samples", 4));
            std::printf("dugum: ornek basina bakilan %.1f, isik veren %.1f\n", double(h[0]) / px, double(h[1]) / px);
            a.statsDev = nullptr;
            cudaFree(st);
        }
        const std::string path = out + "/ayna_bakis" + std::to_string(i);
        const bool ok = a.save(path);
        std::printf("pisirme %.1f ms, isigimiz %.2f ms, %zu texel, dizin ihlali %u\n", a.bakeMs, a.dynMs, a.lmTexelCount(), a.oobCount());
        std::printf("bakis %d: %s  %.1f ms  (%dx%d, %d ornek)  bozuk piksel %u  huzme %d dugum (kesilen %d, derinlik %d, %.2f ms)\n", i,
                    ok ? (path + ".png").c_str() : "YAZILAMADI", a.frameMs, a.width, a.height, a.cfg.i("samples", 4), a.lastBad, a.beamStats.nodes,
                    a.beamStats.cut, a.beamStats.maxDepth, a.beamMs);
    }
    const cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) std::printf("CUDA hatasi: %s\n", cudaGetErrorString(e));
    a.release();
    return e == cudaSuccess ? 0 : 1;
}
