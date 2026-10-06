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

    hdr_ = devAlloc<float4>(size_t(width) * height);
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
}

void AynaApp::release() {
    cudaFree(hdr_);
    cudaFree(output);
    for (int k = 0; k < kLevels; ++k) {
        cudaFree(levels_[k]);
        cudaFree(accum_[k]);
        levels_[k] = accum_[k] = nullptr;
    }
    cudaFree(dEdges_);
    cudaFree(dColumns_);
    cudaFree(dCellObj_);
    cudaFree(dNodes_);
    cudaFree(dCellNodes_);
    cudaFree(dCellStart_);
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
    v.badCount = bad_;
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
        BeamBuildStats st;
        buildBeams(maze, optics, v.light[l], l, eps, maxNodes, nodes_, &st);
        beamStats.nodes += st.nodes;
        beamStats.cut += st.cut;
        beamStats.maxDepth = std::max(beamStats.maxDepth, st.maxDepth);
        ms += st.ms;
    }
    sortIntoCells(maze, nodes_, cellStart_, cellNodes_);
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
    v.beamEps = eps;
    beamStats.ms = ms;
}

void AynaApp::render() {
    View v = makeView();
    uploadObjects(v);
    const auto tb = std::chrono::high_resolution_clock::now();
    if (v.beams > 0) uploadBeams(v);
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
    if (!headless) {
        std::printf("ayna: --headless --out <klasor> [--view 0..n|all] [--cam \"x y z yaw pitch fov\"] [--w --h] [--set k=v] [--plan]\n");
        return 2;
    }
    a.loadConfig();
    if (w > 0) a.width = w;
    if (h > 0) a.height = h;
    a.init();
    std::filesystem::create_directories(out);
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
        a.render();   // the first launch carries the kernels' loading; time the second
        a.frameMs = 0.0f;
        a.render();
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
        std::printf("bakis %d: %s  %.1f ms  (%dx%d, %d ornek)  bozuk piksel %u  huzme %d dugum (kesilen %d, derinlik %d, %.2f ms)\n", i,
                    ok ? (path + ".png").c_str() : "YAZILAMADI", a.frameMs, a.width, a.height, a.cfg.i("samples", 4), a.lastBad, a.beamStats.nodes,
                    a.beamStats.cut, a.beamStats.maxDepth, a.beamMs);
    }
    const cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) std::printf("CUDA hatasi: %s\n", cudaGetErrorString(e));
    a.release();
    return e == cudaSuccess ? 0 : 1;
}
