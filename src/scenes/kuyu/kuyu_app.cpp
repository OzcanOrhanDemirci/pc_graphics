// Scene 10 "Kuyu": the run's state, the optics from measured constants, the rooms' furniture, the command-line tools.
//   pc_graphics.exe --scene kuyu --headless --out captures/kuyu [--view 0..n|all] [--cam "x y z yaw pitch fov"]
//                   [--w 2560 --h 1440] [--set key=value] [--debug 1|2] [--time s] [--probe "x y"]
#include "scenes/kuyu/kuyu_app.h"
#include "core/bloom.h"
#include "core/image_io.h"
#include "shared/display.h"
// the spectral helpers (the CIE fit, Planck) read scene 1's wavelength range and index models
#include "scenes/altinsaat/altinsaat_params.h"
#include "shared/spectrum.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace kuyu;

namespace {

template <class T>
T* devAlloc(size_t n) {
    T* p = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&p), n * sizeof(T));
    return p;
}

using cd = std::complex<double>;

// unpolarised Fresnel reflectance from a medium of real index n1 into one of complex index n2, cos(theta) in n1
double fresnel(double n1, cd n2, double cosi) {
    cosi = std::clamp(cosi, 0.0, 1.0);
    const double sini2 = 1.0 - cosi * cosi;
    const cd cost = std::sqrt(cd(1.0, 0.0) - (n1 * n1 * sini2) / (n2 * n2));
    const cd rs = (n1 * cosi - n2 * cost) / (n1 * cosi + n2 * cost);
    const cd rp = (n2 * cosi - n1 * cost) / (n2 * cosi + n1 * cost);
    return 0.5 * (std::norm(rs) + std::norm(rp));
}

// one absorbing film (complex index n1, thickness d) between real media n0 (the light's side) and n2: the Airy sums
// of its two interfaces, s and p averaged. cos(theta) in n0. R and T of the energy.
void filmRT(double n0, cd n1, double n2, double d, double lam, double c0, double& R, double& T) {
    c0 = std::clamp(c0, 0.0, 1.0);
    const double s0 = std::sqrt(std::max(0.0, 1.0 - c0 * c0));
    const cd c1 = std::sqrt(cd(1.0, 0.0) - (n0 * s0 / n1) * (n0 * s0 / n1));
    const cd c2 = std::sqrt(cd(1.0, 0.0) - cd(n0 * s0 / n2) * cd(n0 * s0 / n2));
    const cd beta = 2.0 * 3.14159265358979 * n1 * c1 * d / lam;
    const cd e = std::exp(cd(0.0, 2.0) * beta), e1 = std::exp(cd(0.0, 1.0) * beta);
    R = T = 0.0;
    for (int pol = 0; pol < 2; ++pol) {
        cd r01, r12, t01, t12;
        if (pol == 0) {
            r01 = (n0 * c0 - n1 * c1) / (n0 * c0 + n1 * c1);
            r12 = (n1 * c1 - n2 * c2) / (n1 * c1 + n2 * c2);
            t01 = 2.0 * n0 * c0 / (n0 * c0 + n1 * c1);
            t12 = 2.0 * n1 * c1 / (n1 * c1 + n2 * c2);
        } else {
            r01 = (n1 * c0 - n0 * c1) / (n1 * c0 + n0 * c1);
            r12 = (n2 * c1 - n1 * c2) / (n2 * c1 + n1 * c2);
            t01 = 2.0 * n0 * c0 / (n1 * c0 + n0 * c1);
            t12 = 2.0 * n1 * c1 / (n2 * c1 + n1 * c2);
        }
        const cd r = (r01 + r12 * e) / (1.0 + r01 * r12 * e);
        const cd t = t01 * t12 * e1 / (1.0 + r01 * r12 * e);
        R += 0.5 * std::norm(r);
        T += c0 > 1e-9 ? 0.5 * std::norm(t) * (n2 * c2).real() / (n0 * c0) : 0.0;
    }
}

// measured constants at the channels' wavelengths (610, 550, 465 nm), see docs/kuyu/DURUM.md:
//   float glass:  M. Rubin, Sol. Energy Mater. 12, 275 (1985) (n from his formula, as in Ayna)
//   silver:       P. B. Johnson, R. W. Christy, Phys. Rev. B 6, 4370 (1972)
//   copper:       Johnson and Christy (1972), interpolated (refractiveindex.info)
//   chromium:     A. D. Rakić et al., Appl. Opt. 37, 5271 (1998)
//   grey glass:   GGI "Tinted Float Glass" data sheet: grey 1/2" (12.7 mm), visible transmission 18%
const double kLambda[3] = {0.610, 0.550, 0.465};   // um
const cd kAg[3] = {{0.058, 4.094}, {0.060, 3.597}, {0.0469, 2.803}};
const cd kCu[3] = {{0.378, 3.107}, {1.007, 2.582}, {1.247, 2.456}};
const cd kCr[3] = {{3.1935, 4.266}, {2.7892, 4.1977}, {2.1410, 3.9299}};
const cd kAu[3] = {{0.226, 3.192}, {0.424, 2.472}, {1.332, 1.869}};   // gold: Johnson and Christy (1972), interpolated
double glassN(double lum) { return 1.5130 - 0.003169 * lum * lum + 0.003962 / (lum * lum); }

// a blackbody's colour in linear Rec.709, luminance 1
float3 kelvinColor(float K) {
    float3 xyz = make_float3(0, 0, 0);
    for (float l = 380.0f; l <= 780.0f; l += 2.0f) xyz += cieXYZ(l) * blackbody(l, K);
    float3 c = xyzToLinearSRGB(xyz);
    c = fmaxf3(c, make_float3(0, 0, 0));
    return c * (1.0f / luminance709(c));
}

}   // namespace

// ---------------------------------------------------------------------------------------------------------------
int KuyuApp::viewCount() { return 4; }
KuyuCam KuyuApp::view(int i) {
    KuyuCam c;
    switch (i) {
    default:
    case 0:   // the shaft between two columns of rooms, straight down: one vanishing point, the rooms passing by
        c.x = 0.0; c.y = 0.3; c.z = 4.0;
        c.yaw = 0.2f;
        c.pitch = -1.5708f;
        break;
    case 1:   // in the shaft at a room's glass, looking in and a little down
        c.x = 0.0; c.y = 0.3; c.z = 4.4;
        c.yaw = 0.0f;
        c.pitch = -0.35f;
        c.fovY = 70.0f;
        break;
    case 2:   // sideways, out of the shaft along the lattice
        c.x = 0.0; c.y = 0.4; c.z = 4.0;
        c.yaw = -1.25f;
        c.pitch = -0.12f;
        break;
    case 3:   // up the shaft
        c.x = 0.0; c.y = 0.0; c.z = 4.0;
        c.yaw = 0.8f;
        c.pitch = 1.15f;
        break;
    }
    return c;
}
const char* KuyuApp::viewName(int i, bool tr) {
    static const char* t[4] = {"Kuyu", "Oda", "Kafes", "Yukarı"};
    static const char* e[4] = {"The shaft", "A room", "The lattice", "Up"};
    return (tr ? t : e)[std::clamp(i, 0, 3)];
}

void KuyuApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/kuyu.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    width = cfg.i("width", width);
    height = cfg.i("height", height);
    debug = cfg.i("debug", debug);
}

void KuyuApp::computeOptics() {
    std::memset(&optics, 0, sizeof(optics));
    optics.glassN = float(glassN(0.55));
    const double film = cfg.f("wall.film", 40.0f) * 1e-3;            // the silver film (um)
    const double gt = lat.glass * 1e6;                                // the glass (um)
    // the grey glass's absorption from its data sheet: Tv = (1 - R)^2 tau / (1 - R^2 tau^2) at 12.7 mm, R its surface's
    const double Tv = cfg.f("wall.grey.tv", 0.18f), Rs = 0.0426;
    const double tau127 = Tv / ((1.0 - Rs) * (1.0 - Rs));            // (the R^2 tau^2 term is under 1e-4)
    const double alpha = -std::log(tau127) / 12700.0;                 // per um
    const double rAr = cfg.f("wall.ar", 0.005f);                      // the antireflection coat, straight on
    for (int s = 0; s < kOpticsN; ++s) {
        const double ci = double(s) / double(kOpticsN - 1);
        const double si = std::sqrt(std::max(0.0, 1.0 - ci * ci));
        for (int ch = 0; ch < 3; ++ch) {
            const double ng = glassN(kLambda[ch]);
            const double cg = std::sqrt(std::max(1e-12, 1.0 - (si / ng) * (si / ng)));   // in the glass
            double Rin, Tin, Rg, Tg;
            filmRT(1.0, kAg[ch], ng, film, kLambda[ch], ci, Rin, Tin);    // from the room
            filmRT(ng, kAg[ch], 1.0, film, kLambda[ch], cg, Rg, Tg);      // from inside the glass
            const double tau = std::exp(-alpha * gt / cg);
            const double Far = fresnel(1.0, cd(ng, 0.0), ci) * (rAr / fresnel(1.0, cd(ng, 0.0), 1.0));
            optics.inR[ch][s] = float(Rin);
            optics.T[ch][s] = float((1.0 - Far) * tau * Tin);
            optics.outR[ch][s] = float(Far + (1.0 - Far) * (1.0 - Far) * tau * tau * Rg);
            optics.copper[ch][s] = float(fresnel(1.0, kCu[ch], ci));
            optics.chrome[ch][s] = float(fresnel(1.0, kCr[ch], ci));
            optics.gold[ch][s] = float(fresnel(1.0, kAu[ch], ci));
        }
        optics.varnish[s] = float(fresnel(1.0, cd(1.5, 0.0), ci));
    }
    setOptics(optics);
}

void KuyuApp::buildVariants() {
    variants.clear();
    variantNames.clear();
    const float h = lat.h;
    unsigned st = 0x2545F491u;
    auto rnd = [&]() {
        st ^= st << 13;
        st ^= st >> 17;
        st ^= st << 5;
        return float(st >> 8) * (1.0f / 16777216.0f);
    };
    const float br = cfg.f("bulb.radius", 0.02f);
    auto bulb = [&](float x, float y, float z, float cd, float K) {
        RoomLight l;
        l.p = make_float3(x, y, z);
        l.r = br;
        l.I = kelvinColor(K) * cd;
        l.wire = 1;
        return l;
    };
    std::vector<int> weight;
    auto add = [&](const RoomVariant& r, const char* name, int w) {
        variants.push_back(r);
        variantNames.push_back(name);
        weight.push_back(w);
    };
    const float starCd = cfg.f("bulb.star", 2.5f), oneCd = cfg.f("bulb.single", 12.0f);
    // stars (Kusama's rooms): bulbs hung at many heights, multiplied without end
    for (int k = 0; k < 6; ++k) {
        RoomVariant r;
        r.kind = 0;
        r.nL = 3 + k % 3;
        for (int l = 0; l < r.nL; ++l)
            r.L[l] = bulb((rnd() * 2.0f - 1.0f) * (h - 0.35f), -h + 0.6f + rnd() * (2.0f * h - 1.2f), (rnd() * 2.0f - 1.0f) * (h - 0.35f), starCd,
                          3400.0f + 900.0f * rnd());
        add(r, "yildizlar", 3);
    }
    // one lamp, a little off the middle
    for (int k = 0; k < 2; ++k) {
        RoomVariant r;
        r.kind = 1;
        r.nL = 1;
        r.L[0] = bulb((rnd() - 0.5f) * 0.8f, 0.2f + rnd() * 0.5f, (rnd() - 0.5f) * 0.8f, oneCd, 3300.0f + 900.0f * rnd());
        add(r, "tek", 9);
    }
    // moonlight: a cold lamp
    for (int k = 0; k < 2; ++k) {
        RoomVariant r;
        r.kind = 2;
        r.nL = 1 + k;
        for (int l = 0; l < r.nL; ++l) r.L[l] = bulb((rnd() - 0.5f) * 1.4f, 0.0f + rnd() * 0.8f, (rnd() - 0.5f) * 1.4f, oneCd * 0.6f, 5200.0f + 1300.0f * k);
        add(r, "ay", 8);
    }
    // a chrome sphere on the floor, a lamp beside it: every image is the whole room in a sphere
    for (int k = 0; k < 2; ++k) {
        RoomVariant r;
        r.kind = 3;
        r.nS = 1;
        r.S[0].r = 0.5f;
        r.S[0].c = make_float3(0.0f, -h + 0.5f, 0.0f);
        r.S[0].metal = 0;
        r.nL = 1;
        r.L[0] = bulb(0.9f, 0.5f + 0.4f * k, -0.6f, oneCd, 3800.0f);
        add(r, "kure", 6);
    }
    // a copper pendulum from the ceiling, swinging slowly (T = 2 pi sqrt(l / g), with the large-swing term)
    for (int k = 0; k < 2; ++k) {
        RoomVariant r;
        r.kind = 4;
        r.nS = 1;
        RoomSphere& s = r.S[0];
        s.r = 0.22f;
        s.c = make_float3(0.0f, -h + 0.55f, 0.0f);
        s.metal = 1;
        s.len = h - s.c.y;
        s.swing = 0.32f;
        s.period = float(2.0 * 3.14159265358979 * std::sqrt(s.len / 9.81) * (1.0 + s.swing * s.swing / 16.0));
        r.nL = 1;
        r.L[0] = bulb(0.95f, 0.8f, -0.85f + 1.7f * k, oneCd, 3500.0f);
        add(r, "sarkac", 6);
    }
    // a free-standing mirror at 45 degrees across the room: the images' lattice turns
    for (int k = 0; k < 2; ++k) {
        RoomVariant r;
        r.kind = 5;
        r.nP = 1;
        RoomPanel& p = r.P[0];
        p.c = make_float3(0.0f, -0.2f, 0.0f);
        p.n = normalize(make_float3(1.0f, 0.0f, -1.0f));
        p.u = normalize(make_float3(1.0f, 0.0f, 1.0f));
        p.w = make_float3(0.0f, 1.0f, 0.0f);
        p.hu = 0.9f + 0.4f * k;
        p.hw = 1.2f;
        r.nL = 2;
        r.L[0] = bulb(-0.8f, 0.5f, -0.9f, oneCd * 0.5f, 4000.0f);
        r.L[1] = bulb(0.9f, 0.2f, 0.7f, oneCd * 0.5f, 4000.0f);
        add(r, "egik", 5);
    }
    // a corner of three mirrors (a corner reflector): what goes in comes back where it came from
    {
        RoomVariant r;
        r.kind = 6;
        const float3 Q = make_float3(-0.7f, -0.7f, -0.7f);
        const float s = 0.65f;
        for (int a = 0; a < 3; ++a) {
            RoomPanel& p = r.P[a];
            const int a1 = (a + 1) % 3, a2 = (a + 2) % 3;
            p.c = Q;
            setComp(p.c, a1, comp(Q, a1) + s);
            setComp(p.c, a2, comp(Q, a2) + s);
            p.n = axisVec(a, 1.0f);
            p.u = axisVec(a1, 1.0f);
            p.w = axisVec(a2, 1.0f);
            p.hu = s;
            p.hw = s;
        }
        r.nP = 3;
        r.nL = 1;
        r.L[0] = bulb(0.7f, 0.6f, 0.5f, oneCd, 4200.0f);
        add(r, "kose", 4);
    }
    // a row of lamps along one axis: endless lines of lights
    {
        RoomVariant r;
        r.kind = 7;
        r.nL = 5;
        for (int l = 0; l < 5; ++l) r.L[l] = bulb(-1.2f + 0.6f * l, 0.3f, 0.0f, starCd, 3600.0f);
        add(r, "sira", 5);
    }
    // dark: no light of its own
    {
        RoomVariant r;
        r.kind = 8;
        add(r, "karanlik", 12);
    }
    // a candle: one small flame, deep amber (1900 K), dim
    {
        RoomVariant r;
        r.kind = 9;
        r.nL = 1;
        r.L[0] = bulb(0.15f, -0.4f, -0.1f, oneCd * 0.25f, 1900.0f);
        r.L[0].r = 0.012f;
        r.L[0].wire = 0;
        add(r, "mum", 6);
    }
    // each variant's brightest bulb (I / r^2 with r in units of the 2 cm the cut-off assumes): the cut-off's bound
    for (RoomVariant& r : variants) {
        r.Imax = 0.0f;
        for (int l = 0; l < r.nL; ++l) r.Imax = std::max(r.Imax, maxc(r.L[l].I) * (0.0004f / (r.L[l].r * r.L[l].r)) * std::max(1.0f, r.L[l].r * r.L[l].r / 0.0004f));
    }
    // the mix: 256 slots by weight, shuffled
    int total = 0;
    for (int w : weight) total += w;
    variantOf.assign(256, 0);
    int slot = 0;
    for (int vi = 0; vi < int(variants.size()); ++vi) {
        const int n = int(std::lround(256.0 * weight[vi] / total));
        for (int k = 0; k < n && slot < 256; ++k) variantOf[slot++] = (unsigned char)vi;
    }
    while (slot < 256) variantOf[slot++] = 0;
    for (int i = 255; i > 0; --i) std::swap(variantOf[i], variantOf[int(rnd() * (i + 1)) % (i + 1)]);
}

void KuyuApp::init() {
    lat.C = cfg.f("lattice.cell", 8.0f);
    lat.h = cfg.f("room.half", 1.8f);
    lat.glass = cfg.f("wall.glass", 0.0127f);
    lat.b = cfg.f("bundle.width", 0.6f);
    lat.gap = cfg.f("bundle.gap", 0.03f);
    lat.P = cfg.f("bundle.relief", 0.03f);
    lat.slats = std::clamp(cfg.i("bundle.slats", 7), 1, kStripMax);
    lat.frame = cfg.f("room.frame", 0.04f);
    computeOptics();
    buildVariants();
    hdr_ = devAlloc<float4>(size_t(width) * height);
    output = devAlloc<uchar4>(size_t(width) * height);
    int w = width, h = height;
    for (int k = 0; k < kLevels; ++k) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        levels_[k] = devAlloc<float4>(size_t(w) * h);
        accum_[k] = devAlloc<float4>(size_t(w) * h);
    }
    dVariants_ = devAlloc<RoomVariant>(variants.size());
    cudaMemcpy(dVariants_, variants.data(), variants.size() * sizeof(RoomVariant), cudaMemcpyHostToDevice);
    dVariantOf_ = devAlloc<unsigned char>(256);
    cudaMemcpy(dVariantOf_, variantOf.data(), 256, cudaMemcpyHostToDevice);
    dGlow_ = devAlloc<float4>(variants.size() * kGlowN * kGlowN);
    bad_ = devAlloc<unsigned int>(1);
    cudaEventCreate(&ev0_);
    cudaEventCreate(&ev1_);
    // the rooms' mean glow, once (the structure's light, the glass's reflections)
    {
        View v = makeView();
        v.glow = nullptr;
        v.maxBounces = cfg.i("tail.bounces", 600);   // the tables: the whole way
        v.minWeight = cfg.f("tail.min.weight", 1e-5f);
        buildGlowTable(v, dGlow_, cfg.i("glow.grid", 64), 0);
        // and again without the bulbs seen straight: a cell's bundles take their own room's straight light exactly
        {
            float4* dRest = devAlloc<float4>(variants.size() * kGlowN * kGlowN);
            buildGlowTable(v, dRest, cfg.i("glow.grid", 64), 1);
            cudaDeviceSynchronize();
            glowRestHost_.resize(variants.size() * kGlowN * kGlowN);
            cudaMemcpy(glowRestHost_.data(), dRest, glowRestHost_.size() * sizeof(float4), cudaMemcpyDeviceToHost);
            cudaFree(dRest);
        }
        if (cfg.i("tail", 0)) {   // (tried and left off: see DURUM)
            dTail_ = devAlloc<float4>(variants.size() * kGlowN * kGlowN);
            buildTailTable(v, dTail_, cfg.i("tail.grid", 12));
        }
        cudaDeviceSynchronize();
        glowHost_.resize(variants.size() * kGlowN * kGlowN);
        cudaMemcpy(glowHost_.data(), dGlow_, glowHost_.size() * sizeof(float4), cudaMemcpyDeviceToHost);
        // the mist's surroundings: the rooms' glow over all directions, by the mix
        double acc[3] = {0, 0, 0};
        for (int s = 0; s < 256; ++s) {
            const int vi = variantOf[s];
            for (int k = 0; k < kGlowN * kGlowN; ++k) {
                const float4 g = glowHost_[size_t(vi) * kGlowN * kGlowN + k];
                acc[0] += g.x;
                acc[1] += g.y;
                acc[2] += g.z;
            }
        }
        const double n = 256.0 * kGlowN * kGlowN;
        meanGlow = make_float3(float(acc[0] / n), float(acc[1] / n), float(acc[2] / n));
    }
    buildFaceGlow();
    // the structure's light: once, over one cell's bundles (the lattice repeats)
    {
        const auto t0 = std::chrono::high_resolution_clock::now();
        ltNa_ = std::max(8, int(std::lround(lat.C / cfg.f("light.texel.along", 0.02f))));
        ltNw_ = std::max(2, int(std::lround(lat.b / cfg.f("light.texel.across", 0.025f))));
        const size_t texels = size_t(48) * ltNa_ * ltNw_;
        dLtIdx_ = devAlloc<unsigned char>(texels * kLtK);
        dLtG_ = devAlloc<float>(texels * kLtK);
        dLtFar_ = devAlloc<float2>(texels);
        View v = makeView();
        buildLightTable(v, dLtIdx_, dLtG_, dLtFar_, 0, cfg.i("light.rays", 4096));
        const cudaError_t e = cudaGetLastError();
        if (e != cudaSuccess) std::fprintf(stderr, "kuyu: isik tablosu kurulamadi: %s\n", cudaGetErrorString(e));
        if (cfg.i("light.dump", 0)) {   // the tools: a few texels' entries
            std::vector<unsigned char> ix(texels * kLtK);
            std::vector<float> gg(texels * kLtK);
            std::vector<float2> ff(texels);
            cudaMemcpy(ix.data(), dLtIdx_, ix.size(), cudaMemcpyDeviceToHost);
            cudaMemcpy(gg.data(), dLtG_, gg.size() * 4, cudaMemcpyDeviceToHost);
            cudaMemcpy(ff.data(), dLtFar_, ff.size() * 8, cudaMemcpyDeviceToHost);
            double sumG = 0, sumFar = 0, sumMist = 0;
            int used = 0;
            for (size_t t = 0; t < texels; ++t) {
                for (int k = 0; k < kLtK; ++k)
                    if (ix[t * kLtK + k] != 255) { sumG += gg[t * kLtK + k]; ++used; }
                sumFar += ff[t].x;
                sumMist += ff[t].y;
            }
            std::printf("isik tablosu: %zu texel, ortalama giris %.2f, ortalama G toplami %.4f, uzak %.4f, pus %.4f\n", texels, double(used) / texels,
                        sumG / texels, sumFar / texels, sumMist / texels);
            for (size_t t : {size_t(1000), size_t(20000), size_t(cfg.i("light.dump.texel", 60000))}) {
                std::printf("  texel %zu:", t);
                for (int k = 0; k < kLtK && ix[t * kLtK + k] != 255; ++k) std::printf(" %d:%.4f", ix[t * kLtK + k], gg[t * kLtK + k]);
                std::printf("  uzak %.4f pus %.4f\n", ff[t].x, ff[t].y);
            }
        }
        tableMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
    }
}

// the glow leaving each face of each variant (its own frame), cosine-weighted over the directions that enter it, and
// the glow's angular shape over cos (the mean of all faces, normalised to a cosine-weighted mean of 1): the faces as
// emitters of the structure's light. From the glow table on an even spiral of directions.
std::vector<float4> KuyuApp::faceMeans(const std::vector<float4>& table, double* shapeSum, double* shapeW) const {
    const int N = kGlowN, nv = int(variants.size());
    auto lookup = [&](int var, float3 w) {
        const float2 e = octEncode(w);
        const float x = std::clamp(e.x * N - 0.5f, 0.0f, N - 1.001f), y = std::clamp(e.y * N - 0.5f, 0.0f, N - 1.001f);
        const int x0 = int(x), y0 = int(y);
        const float fx = x - x0, fy = y - y0;
        const float4* t = table.data() + size_t(var) * N * N;
        const float4 m = (t[y0 * N + x0] * (1.0f - fx) + t[y0 * N + x0 + 1] * fx) * (1.0f - fy) + (t[(y0 + 1) * N + x0] * (1.0f - fx) + t[(y0 + 1) * N + x0 + 1] * fx) * fy;
        return make_float3(m.x, m.y, m.z);
    };
    const int M = 8192;
    std::vector<float3> dirs(M);
    for (int k = 0; k < M; ++k) {
        const double z = 1.0 - 2.0 * (k + 0.5) / M, r = std::sqrt(std::max(0.0, 1.0 - z * z)), ph = k * 2.39996322972865332;
        dirs[k] = make_float3(float(r * std::cos(ph)), float(z), float(r * std::sin(ph)));
    }
    std::vector<float4> out(size_t(nv) * 6, make_float4(0, 0, 0, 0));
    for (int vi = 0; vi < nv; ++vi) {
        for (int face = 0; face < 6; ++face) {
            const int ax = face >> 1;
            const float sg = (face & 1) ? -1.0f : 1.0f;   // the face's outward normal: entered by w with w_ax * sg < 0
            double acc[3] = {0, 0, 0}, wsum = 0.0;
            for (const float3& w : dirs) {
                const float c = -comp(w, ax) * sg;
                if (c <= 0.0f) continue;
                const float3 L = lookup(vi, w);
                acc[0] += L.x * c;
                acc[1] += L.y * c;
                acc[2] += L.z * c;
                wsum += c;
            }
            const float4 Lf = make_float4(float(acc[0] / wsum), float(acc[1] / wsum), float(acc[2] / wsum), 0.0f);
            out[size_t(vi) * 6 + face] = Lf;
            if (!shapeSum || Lf.y <= 0.0f) continue;
            for (const float3& w : dirs) {
                const float c = -comp(w, ax) * sg;
                if (c <= 0.0f) continue;
                const int b = std::min(kShapeN - 1, int(c * kShapeN));
                shapeSum[b] += lookup(vi, w).y;   // (the face's own weight Lf.y cancels: ratio x weight)
                shapeW[b] += Lf.y;
            }
        }
    }
    return out;
}

// the glow leaving each face of each variant (its own frame), cosine-weighted over the directions that enter it, and
// the glow's angular shape over cos (the mean of all faces, normalised to a cosine-weighted mean of 1): the faces as
// emitters of the structure's light. From the glow tables on an even spiral of directions; the same without the bulbs
// seen straight (a cell's own room: those are worked out exactly).
void KuyuApp::buildFaceGlow() {
    double shapeSum[kShapeN] = {}, shapeW[kShapeN] = {};
    faceGlow = faceMeans(glowHost_, shapeSum, shapeW);
    faceGlowRest = faceMeans(glowRestHost_, nullptr, nullptr);
    double norm = 0.0;
    for (int b = 0; b < kShapeN; ++b) {
        glowShape[b] = shapeW[b] > 0.0 ? float(shapeSum[b] / shapeW[b]) : 1.0f;
        const double c = (b + 0.5) / kShapeN;
        norm += 2.0 * glowShape[b] * c / kShapeN;   // 2 int a(c) c dc
    }
    for (int b = 0; b < kShapeN; ++b) glowShape[b] = float(glowShape[b] / norm);
    double acc[3] = {0, 0, 0};
    for (int s = 0; s < 256; ++s)
        for (int face = 0; face < 6; ++face) {
            const float4 f = faceGlow[size_t(variantOf[s]) * 6 + face];
            acc[0] += f.x;
            acc[1] += f.y;
            acc[2] += f.z;
        }
    meanFace = make_float3(float(acc[0] / 1536.0), float(acc[1] / 1536.0), float(acc[2] / 1536.0));
    dFaceGlow_ = devAlloc<float4>(faceGlow.size());
    cudaMemcpy(dFaceGlow_, faceGlow.data(), faceGlow.size() * sizeof(float4), cudaMemcpyHostToDevice);
    dFaceGlowRest_ = devAlloc<float4>(faceGlowRest.size());
    cudaMemcpy(dFaceGlowRest_, faceGlowRest.data(), faceGlowRest.size() * sizeof(float4), cudaMemcpyHostToDevice);
}

void KuyuApp::release() {
    cudaFree(hdr_);
    cudaFree(output);
    for (int k = 0; k < kLevels; ++k) {
        cudaFree(levels_[k]);
        cudaFree(accum_[k]);
        levels_[k] = accum_[k] = nullptr;
    }
    cudaFree(dVariants_);
    cudaFree(dVariantOf_);
    cudaFree(dGlow_);
    cudaFree(dFaceGlow_);
    cudaFree(dFaceGlowRest_);
    dFaceGlowRest_ = nullptr;
    cudaFree(dTail_);
    dTail_ = nullptr;
    cudaFree(dLtIdx_);
    cudaFree(dLtG_);
    cudaFree(dLtFar_);
    dFaceGlow_ = nullptr;
    dLtIdx_ = nullptr;
    dLtG_ = nullptr;
    dLtFar_ = nullptr;
    cudaFree(bad_);
    if (ev0_) cudaEventDestroy(ev0_);
    if (ev1_) cudaEventDestroy(ev1_);
    hdr_ = nullptr;
    output = nullptr;
    dVariants_ = nullptr;
    dVariantOf_ = nullptr;
    dGlow_ = nullptr;
    bad_ = nullptr;
    ev0_ = ev1_ = nullptr;
}

float3 KuyuApp::forward() const {
    const float cp = std::cos(cam.pitch);
    return normalize(make_float3(-std::sin(cam.yaw) * cp, std::sin(cam.pitch), -std::cos(cam.yaw) * cp));
}

bool KuyuApp::collide(double& x, double& y, double& z, float r) const {
    const double C = lat.C;
    bool touched = false;
    const float H = lat.h + lat.glass, hb = 0.5f * lat.b;
    for (int it = 0; it < 4; ++it) {
        bool moved = false;
        const long long I = (long long)std::floor(x / C + 0.5), J = (long long)std::floor(y / C + 0.5), K = (long long)std::floor(z / C + 0.5);
        // the boxes of this cell (and its neighbours only if the observer could reach them: a cell's room and bundles stay
        // C/2 - (H + gap + b), 1.56 m, inside its walls, and a bundle running on into the next cell is that cell's too),
        // relative to the observer (centre and half extents)
        const int R = r < 0.5f * float(C) - (H + lat.gap + lat.b) ? 0 : 1;
        for (int ox = -R; ox <= R; ++ox)
            for (int oy = -R; oy <= R; ++oy)
                for (int oz = -R; oz <= R; ++oz) {
                    const float3 cc = make_float3(float(double(I + ox) * C - x), float(double(J + oy) * C - y), float(double(K + oz) * C - z));
                    for (int b = -1; b < 12; ++b) {
                        float3 c = cc, e;
                        if (b < 0) {
                            e = make_float3(H, H, H);
                        } else {
                            const BundleShape s2 = bundleShape(lat, b);
                            setComp(c, s2.u, comp(cc, s2.u) + s2.cu);
                            setComp(c, s2.v, comp(cc, s2.v) + s2.cv);
                            setComp(e, s2.a, 0.5f * float(C));
                            setComp(e, s2.u, hb);
                            setComp(e, s2.v, hb);
                        }
                        // the closest point of the box to the observer (at the origin)
                        const float3 q = make_float3(std::clamp(0.0f, c.x - e.x, c.x + e.x), std::clamp(0.0f, c.y - e.y, c.y + e.y),
                                                     std::clamp(0.0f, c.z - e.z, c.z + e.z));
                        const float d2 = dot(q, q);
                        if (d2 >= r * r) continue;
                        float3 push;
                        if (d2 > 1e-12f) {
                            const float dl = std::sqrt(d2);
                            push = q * (-(r - dl) / dl);
                        } else {   // inside: out along the shallowest side
                            float best = 1e30f;
                            push = make_float3(0, 0, 0);
                            for (int a = 0; a < 3; ++a)
                                for (int sg = -1; sg <= 1; sg += 2) {
                                    const float depth = sg * (comp(c, a) + sg * comp(e, a)) + r;   // to that face, and r beyond it
                                    if (depth < best) {
                                        best = depth;
                                        push = axisVec(a, float(sg) * depth);
                                    }
                                }
                        }
                        x += push.x;
                        y += push.y;
                        z += push.z;
                        moved = touched = true;
                    }
                }
        if (!moved) break;
    }
    return touched;
}

double KuyuApp::sweep(double x, double y, double z, double mx, double my, double mz, float r, double n[3]) const {
    const double len = std::sqrt(mx * mx + my * my + mz * mz);
    n[0] = n[1] = n[2] = 0.0;
    if (len < 1e-12) return 1.0;
    const double d[3] = {mx / len, my / len, mz / len}, p[3] = {x, y, z};
    const double C = lat.C;
    const double H = double(lat.h + lat.glass) + r, hb = 0.5 * lat.b + r;
    long long c[3];
    double tMax[3], tDel[3];
    int st[3];
    for (int a = 0; a < 3; ++a) {
        c[a] = (long long)std::floor(p[a] / C + 0.5);
        st[a] = d[a] > 0.0 ? 1 : -1;
        if (std::fabs(d[a]) < 1e-15) {
            tMax[a] = tDel[a] = 1e300;
        } else {
            tMax[a] = ((double(c[a]) + 0.5 * st[a]) * C - p[a]) / d[a];
            tDel[a] = C / std::fabs(d[a]);
        }
    }
    double best = len, t = 0.0;
    // a cell's obstacles stay 1.56 m inside its walls even grown by r (as in collide): each cell's own boxes suffice
    while (t < best) {
        const double tNext = std::min(tMax[0], std::min(tMax[1], tMax[2]));
        for (int b = -1; b < 12; ++b) {
            double cen[3], ext[3];
            for (int a = 0; a < 3; ++a) {
                cen[a] = double(c[a]) * C - p[a];   // relative to the start (doubles: the way may be kilometres)
                ext[a] = H;
            }
            if (b >= 0) {
                const BundleShape s2 = bundleShape(lat, b);
                cen[s2.u] += s2.cu;
                cen[s2.v] += s2.cv;
                ext[s2.a] = 0.5 * C;
                ext[s2.u] = ext[s2.v] = hb;
            }
            double tn = -1e300, tf = 1e300;
            int ax = 0;
            bool miss = false;
            for (int a = 0; a < 3 && !miss; ++a) {
                if (std::fabs(d[a]) < 1e-15) {
                    if (std::fabs(cen[a]) > ext[a]) miss = true;
                    continue;
                }
                double t0 = (cen[a] - ext[a]) / d[a], t1 = (cen[a] + ext[a]) / d[a];
                if (t0 > t1) std::swap(t0, t1);
                if (t0 > tn) { tn = t0; ax = a; }
                tf = std::min(tf, t1);
            }
            if (miss || tn > tf || tf <= 0.0) continue;
            if (tn < 0.0) {
                // starting inside the grown box (its corners reach farther than the sphere does): a hit if going inwards
                double toC = 0.0;
                for (int a = 0; a < 3; ++a) toC += d[a] * cen[a];
                if (toC <= 0.0) continue;
                // the face nearest the start: its normal
                double bestDepth = 1e300;
                for (int a = 0; a < 3; ++a)
                    for (int sg = -1; sg <= 1; sg += 2) {
                        const double depth = sg * cen[a] + ext[a];   // from the start out through that face
                        if (depth < bestDepth) { bestDepth = depth; ax = a; tn = 0.0; n[0] = n[1] = n[2] = 0.0; n[a] = sg; }
                    }
                best = 0.0;
                return 0.0;
            }
            if (tn < best) {
                best = tn;
                n[0] = n[1] = n[2] = 0.0;
                n[ax] = d[ax] > 0.0 ? -1.0 : 1.0;
            }
        }
        t = tNext;
        if (tMax[0] <= tMax[1] && tMax[0] <= tMax[2]) { c[0] += st[0]; tMax[0] += tDel[0]; }
        else if (tMax[1] <= tMax[2]) { c[1] += st[1]; tMax[1] += tDel[1]; }
        else { c[2] += st[2]; tMax[2] += tDel[2]; }
    }
    return best / len;
}

View KuyuApp::makeView() const {
    View v;
    v.width = width;
    v.height = height;
    v.g = lat;
    const double C = lat.C;
    const long long I = (long long)std::floor(cam.x / C + 0.5), J = (long long)std::floor(cam.y / C + 0.5), K = (long long)std::floor(cam.z / C + 0.5);
    v.camI = I;
    v.camJ = J;
    v.camK = K;
    v.camPos = make_float3(float(cam.x - double(I) * C), float(cam.y - double(J) * C), float(cam.z - double(K) * C));
    v.camFwd = forward();
    // the picture's right: level, from the heading alone (straight down stays defined); forward x up
    v.camRight = make_float3(std::cos(cam.yaw), 0.0f, -std::sin(cam.yaw));
    v.camUp = cross(v.camRight, v.camFwd);
    v.tanHalf = std::tan(cam.fovY * 0.5f * 3.14159265f / 180.0f);
    v.pixAngle = 2.0f * v.tanHalf / float(height);
    v.time = float(time);
    v.variants = dVariants_;
    v.nVariants = int(variants.size());
    v.glow = dGlow_;
    v.tail = cfg.i("tail", 0) ? dTail_ : nullptr;
    v.exactBounces = cfg.i("exact.bounces", 400);
    v.interiorReps = cfg.i("interior.reps", 1);
    v.interiorWiden = cfg.f("interior.widen", 0.5f);
    v.interiorRepsEdges = cfg.i("interior.reps.edges", 3);
    v.variantOf = dVariantOf_;
    v.exposure = cfg.f("exposure", 3.0f) * evScale;
    v.bloom = cfg.f("bloom", 0.04f);
    v.vignette = cfg.f("vignette", 0.2f);
    v.samples = cfg.i("samples", 6);
    v.coneScale = cfg.f("cone.scale", 0.6f);
    v.recon = cfg.f("recon", 0.12f);
    v.grazeClamp = cfg.f("graze.clamp", 0.05f);
    v.minWeight = cfg.f("min.weight", 1.0f / 255.0f);
    v.maxBounces = cfg.i("max.bounces", 256);
    v.cutEps = cfg.f("cut.eps", 1.86e-4f);
    v.cutEncounters = cfg.f("cut.encounters", 1.0f);
    v.maxCells = cfg.i("max.cells", 160);
    v.reflections = cfg.i("reflections", 1);
    v.lightRooms = cfg.i("light.rooms", 27);
    // the threads: book spines (bone, walnut, black, cloth grey, tan leather, oxblood, olive) and gold leaf
    const float3 slat[8] = {cfg.v3("thread.0", make_float3(0.56f, 0.51f, 0.42f)),     // bone, matte
                            cfg.v3("thread.1", make_float3(0.085f, 0.062f, 0.045f)),  // walnut, varnished
                            cfg.v3("thread.2", make_float3(0.018f, 0.017f, 0.016f)),  // black lacquer
                            cfg.v3("thread.3", make_float3(0.20f, 0.19f, 0.175f)),    // grey cloth, matte
                            cfg.v3("thread.4", make_float3(0.21f, 0.16f, 0.11f)),     // tan leather, varnished
                            cfg.v3("thread.5", make_float3(0.13f, 0.065f, 0.055f)),   // oxblood, varnished
                            cfg.v3("thread.6", make_float3(0.11f, 0.115f, 0.09f)),    // olive cloth, matte
                            make_float3(0, 0, 0)};                                     // gold leaf
    const int finish[8] = {0, 1, 1, 0, 1, 1, 0, 2};
    const int mix[16] = {0, 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7};   // gold: one thread in sixteen
    for (int k = 0; k < 8; ++k) {
        v.slatAlbedo[k] = slat[k];
        v.slatFinish[k] = finish[k];
    }
    for (int k = 0; k < 16; ++k) v.threadMap[k] = (unsigned char)mix[k];
    v.threads = std::clamp(cfg.i("bundle.threads", 5), 1, 16);
    v.frameAlbedo = cfg.v3("frame.albedo", make_float3(0.05f, 0.05f, 0.05f));
    v.hazeSigma = cfg.f("haze.sigma", 0.006f);
    v.hazeAbsorb = cfg.f("haze.absorb", 0.001f);
    v.hazeAmbient = meanGlow * cfg.f("haze.cover", 0.3f);
    v.ltIdx = dLtIdx_;
    v.ltG = dLtG_;
    v.ltFar = dLtFar_;
    v.ltNa = ltNa_;
    v.ltNw = ltNw_;
    v.faceGlow = dFaceGlow_;
    v.faceGlowRest = dFaceGlowRest_;
    v.ownDirect = cfg.i("light.own.direct", 0);
    for (int k = 0; k < kShapeN; ++k) v.glowShape[k] = glowShape[k];
    v.meanFace = meanFace;
    v.useTable = cfg.i("light.table", 1);
    v.prof = cfg.i("prof", 0);
    v.badCount = bad_;
    v.stats = statsDev;
    v.debug = debug;
    v.subX = subX;
    v.subY = subY;
    return v;
}

void KuyuApp::render() {
    View v = makeView();
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
        std::fprintf(stderr, "kuyu: CUDA hatasi '%s'  kamera (%.4f %.4f %.4f) yaw %.3f pitch %.3f\n", cudaGetErrorString(err), cam.x, cam.y, cam.z,
                     cam.yaw, cam.pitch);
    }
    frameMs = frameMs <= 0.0f ? ms : frameMs + (ms - frameMs) * 0.1f;
}

void KuyuApp::probe(float x, float y) { probePixel(makeView(), x, y); }

bool KuyuApp::saveReference(const std::string& pathNoExt) {
    // four renders shifted by a quarter pixel, averaged before the display transform (one render of 4 x the samples)
    std::vector<float4> acc(size_t(width) * height, make_float4(0, 0, 0, 0));
    const float offs[4][2] = {{-0.25f, -0.25f}, {0.25f, -0.25f}, {-0.25f, 0.25f}, {0.25f, 0.25f}};
    for (int k = 0; k < 4; ++k) {
        subX = offs[k][0];
        subY = offs[k][1];
        render();
        const std::vector<float4> h = hdrHost();
        for (size_t i = 0; i < acc.size(); ++i) acc[i] = acc[i] + h[i] * 0.25f;
    }
    subX = subY = 0.0f;
    cudaMemcpy(hdr_, acc.data(), acc.size() * sizeof(float4), cudaMemcpyHostToDevice);
    const View v = makeView();
    launchBloom(hdr_, width, height, levels_, accum_, kLevels, 0);
    resolveFrame(v, hdr_, accum_[0], output, 0);
    cudaDeviceSynchronize();
    return save(pathNoExt);
}

bool KuyuApp::save(const std::string& pathNoExt) {
    std::vector<unsigned char> px(size_t(width) * height * 4);
    cudaMemcpy(px.data(), output, px.size(), cudaMemcpyDeviceToHost);
    return writePNG(pathNoExt + ".png", width, height, px.data());
}

// ---------------------------------------------------------------------------------------------------------------
int runKuyuCli(int argc, char** argv) {
    KuyuApp a;
    a.root = PROJECT_ROOT;
    std::string out = "captures/kuyu", which = "all", test;
    int w = 0, h = 0;
    bool headless = false, hasFree = false;
    double timeSet = -1.0;
    std::vector<float2> probes;
    KuyuCam freeCam;
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
        else if (s == "--time") timeSet = std::stod(next());
        else if (s == "--test") { test = next(); headless = true; }
        else if (s == "--debug") a.overrides.push_back("debug=" + next());
        else if (s == "--probe") {
            float x = 0, y = 0;
            if (std::sscanf(next().c_str(), "%f %f", &x, &y) == 2) probes.push_back(make_float2(x, y));
        } else if (s == "--cam") {
            KuyuCam c;
            if (std::sscanf(next().c_str(), "%lf %lf %lf %f %f %f", &c.x, &c.y, &c.z, &c.yaw, &c.pitch, &c.fovY) == 6) {
                freeCam = c;
                hasFree = true;
            }
        }
    }
    if (test == "o3") {
        // Ö3: falling down the shaft at 1.5 m/s (30 frames a second), looking round slowly, the pendulums swinging: the
        // live frames and the same moments converged (16 samples, every sample its own interior, four renders shifted
        // by a quarter pixel: 64), for tools/kararlilik.py
        const std::string dir = out + "/o3";
        std::filesystem::create_directories(dir);
        const int frames = std::max(4, [&]() { KuyuApp t; t.root = a.root; t.overrides = a.overrides; t.loadConfig(); return t.cfg.i("o3.frames", 48); }());
        const int W = w > 0 ? w : 1280, Hh = h > 0 ? h : 720;
        for (int pass = 0; pass < 2; ++pass) {
            KuyuApp b;
            b.root = a.root;
            b.overrides = a.overrides;
            if (pass == 1) {
                b.overrides.push_back("samples=16");
                b.overrides.push_back("interior.reps=0");
            }
            b.loadConfig();
            b.width = W;
            b.height = Hh;
            b.init();
            const int path = b.cfg.i("o3.path", 0);
            for (int f = 0; f < frames; ++f) {
                const double t = double(f) / 30.0;
                if (path == 0) {   // down the shaft, looking down and slowly round
                    b.cam.x = 0.05 * std::sin(t * 0.7);
                    b.cam.y = 0.3 - 1.5 * t;
                    b.cam.z = 4.0 + 0.1 * std::sin(t * 0.5);
                    b.cam.yaw = float(0.2 + 0.25 * t);
                    b.cam.pitch = float(-1.2 + 0.1 * std::sin(t * 1.3));
                } else if (path == 2) {   // hovering: the view drifts a quarter of a pixel a frame (where twinkling shows)
                    b.cam.x = 0.0 + 0.002 * f;
                    b.cam.y = 0.3 - 0.01 * f;
                    b.cam.z = 4.2;
                    b.cam.yaw = float(0.2 + 0.0004 * f);
                    b.cam.pitch = float(-1.0 + 0.0003 * f);
                } else {           // the same fall, looking at a room as it passes
                    b.cam.x = 0.0;
                    b.cam.y = 1.0 - 1.5 * t;
                    b.cam.z = 4.4;
                    b.cam.yaw = float(-0.1 + 0.15 * t);
                    b.cam.pitch = -0.2f;
                    b.cam.fovY = 70.0f;
                }
                b.time = 10.0 + t;
                char name[64];
                std::snprintf(name, sizeof(name), "/%s_%04d", pass == 0 ? "live" : "ref", f);
                if (pass == 1) {
                    b.saveReference(dir + name);
                } else {
                    b.render();
                    b.save(dir + name);
                }
            }
            b.release();
        }
        std::printf("O3: %d kare -> %s (python tools/kararlilik.py %s)\n", frames, dir.c_str(), dir.c_str());
        return 0;
    }
    if (test == "o4") {
        // Ö4: points on the bundles of cell (0, 0, 0) (several bundles, faces, along and across), table <-> brute force
        a.loadConfig();
        a.width = 64;
        a.height = 36;
        a.init();
        View v = a.makeView();
        v.camI = v.camJ = v.camK = 0;
        v.exactBounces = 2000;   // the reference: every bounce
        v.cutEps = 1e-9f;
        v.minWeight = 1e-7f;
        const int rays = a.cfg.i("o4.rays", 1 << 20);
        const Lattice& g = a.lat;
        double sumT = 0, sumB = 0, sumAbs = 0, sumM = 0, sumAbsM = 0, sumG2 = 0, sumAbsG2 = 0;
        int nPts = 0;
        std::printf("O4: tablo <-> kaba kuvvet (%d isin, oda ici tam derinlik), yesil kanal\n", rays);
        std::printf("   demet yuz   boyunca  enine     tablo        kaba kuvvet   oran\n");
        for (int k = 0; k < 24; ++k) {
            const int bq = (k * 5) % 12, f = (k * 7 + k / 12) % 4;
            const BundleShape bs = bundleShape(g, bq);
            const int nAx = f < 2 ? bs.u : bs.v, wAx = f < 2 ? bs.v : bs.u;
            const float sN = (f & 1) ? -1.0f : 1.0f;
            const float cN = f < 2 ? bs.cu : bs.cv, cW = f < 2 ? bs.cv : bs.cu;
            const float along = -0.5f * g.C + g.C * (0.07f + 0.86f * float((k * 37) % 24) / 23.0f);
            const float across = 0.5f * g.b * (-0.8f + 1.6f * float((k * 11) % 7) / 6.0f);
            float3 P;
            setComp(P, bs.a, along);
            setComp(P, nAx, cN + sN * 0.5f * g.b);
            setComp(P, wAx, cW + across);
            float3 Et, Eb, Et2, Em;
            o4Point(v, bq, f, P, axisVec(nAx, sN), rays, Et, Eb, 0);
            o4Point(v, bq, f, P, axisVec(nAx, sN), rays, Et2, Em, 1);
            float3 Eg;
            o4Point(v, bq, f, P, axisVec(nAx, sN), rays, Et2, Eg, 2);
            sumG2 += Eg.y;
            sumAbsG2 += std::fabs(Eg.y - Eb.y);
            sumM += Em.y;
            sumAbsM += std::fabs(Et.y - Em.y);
            sumT += Et.y;
            sumB += Eb.y;
            sumAbs += std::fabs(Et.y - Eb.y);
            ++nPts;
            std::printf("   %5d %3d   %6.2f   %6.2f   %.6f   %.6f   %.3f   model %.6f %.3f\n", bq, f, along, across, Et.y, Eb.y, Eb.y > 0 ? Et.y / Eb.y : 0.0,
                        Em.y, Em.y > 0 ? Et.y / Em.y : 0.0);
        }
        std::printf("O4: toplam tablo / kaba kuvvet %.4f, uyum 1 - sum|fark| / sum %.4f (hedef >= 0,99)\n", sumT / sumB, 1.0 - sumAbs / sumB);
        std::printf("O4 (yayici modeli: oda turunun yone bagli paritisi <-> fizik): toplam %.4f, uyum %.4f\n", sumG2 / sumB, 1.0 - sumAbsG2 / sumB);
        std::printf("O4 (tablonun makinesi: kaba kuvvet ayni yayici modelle): toplam %.4f, uyum %.4f\n", sumT / sumM, 1.0 - sumAbsM / sumM);
        a.release();
        return 0;
    }
    if (test == "carpisma") {
        // the observer's collision: random points pushed out must end clear of every box (checked by a separate
        // distance scan), points already clear must not move; the composed views must start clear
        a.loadConfig();
        a.lat.C = a.cfg.f("lattice.cell", 8.0f);
        a.lat.h = a.cfg.f("room.half", 1.8f);
        a.lat.glass = a.cfg.f("wall.glass", 0.0127f);
        a.lat.b = a.cfg.f("bundle.width", 0.6f);
        a.lat.gap = a.cfg.f("bundle.gap", 0.03f);
        const float r = 0.3f;
        auto clearance = [&](double x, double y, double z) {   // the smallest distance to any box round the point
            const double C = a.lat.C;
            const float H = a.lat.h + a.lat.glass, hb = 0.5f * a.lat.b;
            float best = 1e30f;
            const long long I = (long long)std::floor(x / C + 0.5), J = (long long)std::floor(y / C + 0.5), K = (long long)std::floor(z / C + 0.5);
            for (int ox = -1; ox <= 1; ++ox)
                for (int oy = -1; oy <= 1; ++oy)
                    for (int oz = -1; oz <= 1; ++oz) {
                        const float3 cc = make_float3(float(double(I + ox) * C - x), float(double(J + oy) * C - y), float(double(K + oz) * C - z));
                        for (int b = -1; b < 12; ++b) {
                            float3 c = cc, e;
                            if (b < 0) e = make_float3(H, H, H);
                            else {
                                const BundleShape s2 = bundleShape(a.lat, b);
                                setComp(c, s2.u, comp(cc, s2.u) + s2.cu);
                                setComp(c, s2.v, comp(cc, s2.v) + s2.cv);
                                setComp(e, s2.a, 0.5f * float(C));
                                setComp(e, s2.u, hb);
                                setComp(e, s2.v, hb);
                            }
                            const float dx = std::max(0.0f, std::fabs(c.x) - e.x), dy = std::max(0.0f, std::fabs(c.y) - e.y), dz = std::max(0.0f, std::fabs(c.z) - e.z);
                            best = std::min(best, std::sqrt(dx * dx + dy * dy + dz * dz));
                        }
                    }
            return best;
        };
        unsigned st = 777u;
        auto rnd = [&]() {
            st ^= st << 13;
            st ^= st >> 17;
            st ^= st << 5;
            return float(st >> 8) * (1.0f / 16777216.0f);
        };
        // walks: from clear points, 400 random steps of up to 5 cm (the live app's sub-steps), each pushed out
        int bad = 0, movedClear = 0, walks = 0;
        float worst = 1e30f;
        for (int k = 0; k < 400; ++k) {
            double x = (rnd() * 2 - 1) * 16.0, y = (rnd() * 2 - 1) * 16.0, z = (rnd() * 2 - 1) * 16.0;
            if (clearance(x, y, z) < r) continue;
            ++walks;
            for (int st2 = 0; st2 < 400; ++st2) {
                const double px = x + (rnd() * 2 - 1) * 0.05, py = y + (rnd() * 2 - 1) * 0.05 - 0.02, pz = z + (rnd() * 2 - 1) * 0.05;
                const float before = clearance(px, py, pz);
                x = px;
                y = py;
                z = pz;
                a.collide(x, y, z, r);
                const float after = clearance(x, y, z);
                worst = std::min(worst, after);
                if (after < r - 1e-3f) ++bad;
                if (before >= r && (x != px || y != py || z != pz)) ++movedClear;
            }
        }
        // fast moves (up to 200 m a frame, any direction) by the live scene's way: sweep, slide, last push; every
        // piece of the way sampled every 5 cm by the distance scan
        int sweepBad = 0, sweepMoves = 0, sweepEndBad = 0;
        float sweepWorst = 1e30f;
        for (int k = 0; k < 3000; ++k) {
            double x = (rnd() * 2 - 1) * 16.0, y = (rnd() * 2 - 1) * 16.0, z = (rnd() * 2 - 1) * 16.0;
            if (clearance(x, y, z) < r) continue;
            ++sweepMoves;
            const double L = std::pow(10.0, rnd() * 3.3 - 1.0);   // 0.1 to 200 m
            double m[3] = {(rnd() * 2 - 1), (rnd() * 2 - 1), (rnd() * 2 - 1)};
            const double ml = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            for (int a = 0; a < 3; ++a) m[a] *= L / ml;
            for (int it = 0; it < 4; ++it) {
                double n[3];
                const double t = a.sweep(x, y, z, m[0], m[1], m[2], r, n);
                const double go = std::max(0.0, t - 1e-6);
                const int samples = std::max(1, int(L * go / 0.05));
                for (int q = 1; q <= samples; ++q) {
                    const double f = go * double(q) / samples;
                    const float c = clearance(x + m[0] * f, y + m[1] * f, z + m[2] * f);
                    sweepWorst = std::min(sweepWorst, c);
                    if (c < r - 0.02f) ++sweepBad;
                }
                x += m[0] * go;
                y += m[1] * go;
                z += m[2] * go;
                if (t >= 1.0) break;
                for (int a2 = 0; a2 < 3; ++a2) m[a2] *= (1.0 - go);
                const double mn = m[0] * n[0] + m[1] * n[1] + m[2] * n[2];
                if (mn < 0.0)
                    for (int a2 = 0; a2 < 3; ++a2) m[a2] -= n[a2] * mn;
            }
            a.collide(x, y, z, r);
            if (clearance(x, y, z) < r - 1e-3f) ++sweepEndBad;
        }
        std::printf("supurme: %d hizli hareket (0,1-200 m), yol boyunca engele en yakin %.4f m (icinden gecen olsa < 0), yolda kureyle "
                    "ortusen ornek %d, kare sonunda engelde %d\n", sweepMoves, sweepWorst, sweepBad, sweepEndBad);
        bad += sweepEndBad + (sweepWorst <= 0.0f ? 1 : 0);
        int viewsBad = 0;
        for (int i = 0; i < KuyuApp::viewCount(); ++i) {
            const KuyuCam c = KuyuApp::view(i);
            if (clearance(c.x, c.y, c.z) < r) ++viewsBad;
        }
        std::printf("carpisma: %d yuruyus x 400 adim, itildikten sonra engele en yakin %.4f m (gozlemci %.2f m), engelde kalan %d, bosken itilen %d, "
                    "bakislardan engelde olan %d\n", walks, worst, r, bad, movedClear, viewsBad);
        return bad || movedClear || viewsBad ? 1 : 0;
    }
    if (test == "fuzz") {   // random poses (fixed seed) over a few cells, the clock running: CUDA and the pixels must stay clean
        a.loadConfig();
        a.width = w > 0 ? w : 640;
        a.height = h > 0 ? h : 360;
        a.init();
        unsigned st = 12345u;
        auto rnd = [&]() {
            st ^= st << 13;
            st ^= st >> 17;
            st ^= st << 5;
            return float(st >> 8) * (1.0f / 16777216.0f);
        };
        int fails = 0, badFrames = 0;
        const int poses = a.cfg.i("fuzz.poses", 1000);
        for (int k = 0; k < poses && fails < 3; ++k) {
            a.cam.x = (rnd() * 2.0 - 1.0) * 12.0;
            a.cam.y = (rnd() * 2.0 - 1.0) * 400.0;
            a.cam.z = (rnd() * 2.0 - 1.0) * 12.0;
            a.cam.yaw = rnd() * 6.2832f;
            a.cam.pitch = (rnd() * 2.0f - 1.0f) * 1.5708f;
            a.time = rnd() * 1000.0;
            a.render();
            const cudaError_t e = cudaGetLastError();
            if (a.lastBad) ++badFrames;
            if (e != cudaSuccess) {
                ++fails;
                std::printf("HATA %d: %s  kamera \"%.4f %.4f %.4f %.4f %.4f 62\"\n", k, cudaGetErrorString(e), a.cam.x, a.cam.y, a.cam.z, a.cam.yaw,
                            a.cam.pitch);
                break;
            }
        }
        std::printf("fuzz: %s, bozuk pikselli kare %d (%d poz)\n", fails ? "HATA BULUNDU" : "temiz", badFrames, poses);
        a.release();
        return fails || badFrames ? 1 : 0;
    }
    if (test == "o2") {   // the wall's tables for tools/kuyu_o2.py (an independent calculation)
        a.loadConfig();
        a.lat.glass = a.cfg.f("wall.glass", 0.0127f);
        a.computeOptics();
        const std::string path = out + "/o2_tablolar.csv";
        std::filesystem::create_directories(out);
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return 1;
        std::fprintf(f, "cos,inR0,inR1,inR2,outR0,outR1,outR2,T0,T1,T2,cu0,cu1,cu2,cr0,cr1,cr2,au0,au1,au2,varnish\n");
        for (int k = 0; k < kOpticsN; ++k) {
            const Optics& o = a.optics;
            std::fprintf(f, "%.6f", double(k) / (kOpticsN - 1));
            for (int ch = 0; ch < 3; ++ch) std::fprintf(f, ",%.7f", o.inR[ch][k]);
            for (int ch = 0; ch < 3; ++ch) std::fprintf(f, ",%.7f", o.outR[ch][k]);
            for (int ch = 0; ch < 3; ++ch) std::fprintf(f, ",%.7f", o.T[ch][k]);
            for (int ch = 0; ch < 3; ++ch) std::fprintf(f, ",%.7f", o.copper[ch][k]);
            for (int ch = 0; ch < 3; ++ch) std::fprintf(f, ",%.7f", o.chrome[ch][k]);
            for (int ch = 0; ch < 3; ++ch) std::fprintf(f, ",%.7f", o.gold[ch][k]);
            std::fprintf(f, ",%.7f\n", o.varnish[k]);
        }
        std::fclose(f);
        std::printf("O2: %s (python tools/kuyu_o2.py %s)\n", path.c_str(), path.c_str());
        return 0;
    }
    if (test == "o1") {
        // Ö1: a room with one bulb (variant "tek"), seen from the shaft through its glass; for images n = 1..30 (a, b, c
        // walls crossed) the direction from the eye that should meet the bulb's n-th image is worked out analytically
        // (the image lattice: x_a = 2 a h + (-1)^a x; the glass slab's sideways shift in closed form), the ray traced by
        // the scene's own room code, its miss at the n-th bounce turned into pixels at 1440p, 62 degrees
        a.loadConfig();
        a.width = 64;
        a.height = 36;
        a.init();
        int var = -1;
        for (int k = 0; k < int(a.variantNames.size()); ++k)
            if (a.variantNames[k] == "tek") { var = k; break; }
        const RoomVariant& rv = a.variants[var];
        const float h = a.lat.h, gl = a.lat.glass, H = h + gl;
        const float3 P = rv.L[0].p;
        const float3 E = make_float3(0.31f, 0.27f, 2.62f);   // in the shaft, before the +z face
        const double pixAngle = 2.0 * std::tan(31.0 * 3.14159265358979 / 180.0) / 1440.0;
        const View v = a.makeView();
        auto mirror = [&](float x, int k) { return float(k) * 2.0f * h + ((k & 1) ? -x : x); };
        const float ng = a.optics.glassN;
        double worst = 0.0;
        int tested = 0;
        const int combos[][3] = {{0, 0, -1}, {1, 0, -1}, {0, -1, -1}, {1, 1, -2}, {-2, 1, -2}, {2, -2, -3}, {3, 1, -4}, {-3, 3, -4}, {4, -2, -6},
                                 {5, 3, -6}, {-4, 6, -7}, {6, -5, -8}, {-7, 4, -9}, {8, 6, -10}, {9, -7, -12}, {-10, 8, -12}};
        std::printf("O1: tek lambali oda, goz camin %.2f m onunde\n", double(E.z - H));
        std::printf("   (a b c)    n   iz [mm]   yol [m]   hata [px]\n");
        for (const auto& cb : combos) {
            const int n = std::abs(cb[0]) + std::abs(cb[1]) + std::abs(cb[2]);
            const float3 I = make_float3(mirror(P.x, cb[0]), mirror(P.y, cb[1]), mirror(P.z, cb[2]));
            // the direction: d proportional to I - E - shift(d), the slab's shift found by iterating
            float3 d = normalize(I - E);
            for (int it = 0; it < 30; ++it) {
                const float ci = d.z * -1.0f < 0.0f ? 0.0f : -d.z;   // the +z face's outward normal is +z: cos = -d.z
                const float eta = 1.0f / ng;
                const float ct = std::sqrt(std::max(1.0f - eta * eta * (1.0f - ci * ci), 1e-6f));
                const float3 nOut = make_float3(0, 0, 1);
                const float3 dt = d * eta + nOut * (eta * ci - ct);
                const float3 shift = dt * (gl / ct) - d * (gl / ci);   // both to the inner face
                d = normalize(I - E - shift);
            }
            std::vector<float2> tr(n + 2);
            float entry = 0.0f;
            imageTest(v, var, E, d, 2, 1.0f, n + 1, tr.data(), &entry);
            // the way to the n-th piece's closest point: the entry, the pieces before it, this piece's part
            double way = entry;
            // (the pieces' lengths are not kept: the straight unfolded line's length to the image is the way)
            way = length(I - E);
            const double px = double(tr[n].x) / way / pixAngle;
            worst = std::max(worst, px);
            ++tested;
            std::printf("  (%3d %3d %3d) %3d   %8.4f   %7.2f   %.5f\n", cb[0], cb[1], cb[2], n, double(tr[n].x) * 1000.0, way, px);
        }
        std::printf("O1: %d goruntu, en kotu %.5f px (hedef < 0,1)\n", tested, worst);
        a.release();
        return worst < 0.1 ? 0 : 1;
    }
    if (!headless) {
        std::printf("kuyu: --headless --out <klasor> [--view 0..n|all] [--cam \"x y z yaw pitch fov\"] [--w --h] [--set k=v] [--time s]\n");
        return 2;
    }
    a.loadConfig();
    if (w > 0) a.width = w;
    if (h > 0) a.height = h;
    const auto t0 = std::chrono::high_resolution_clock::now();
    a.init();
    const double initMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();
    if (timeSet >= 0.0) a.time = timeSet;
    std::filesystem::create_directories(out);
    std::printf("View: cpp %zu / cu %zu   kurulum %.0f ms   %zu oda turu\n", sizeof(View), viewSizeCu(), initMs, a.variants.size());
    std::printf("ortalama oda parlakligi %.5f %.5f %.5f   yuz %.5f %.5f %.5f   isik tablosu %.0f ms\n", a.meanGlow.x, a.meanGlow.y, a.meanGlow.z,
                a.meanFace.x, a.meanFace.y, a.meanFace.z, a.tableMs);
    std::printf("acisal bicim:");
    for (int k = 0; k < kShapeN; ++k) std::printf(" %.2f", a.glowShape[k]);
    std::printf("\n");
    for (int i = 0; i < KuyuApp::viewCount(); ++i) {
        if (hasFree ? i > 0 : (which != "all" && std::stoi(which) != i)) continue;
        a.cam = hasFree ? freeCam : KuyuApp::view(i);
        a.frameMs = 0.0f;
        a.render();
        std::vector<float> times;
        for (int r = 0; r < std::max(1, a.cfg.i("bench", 5)); ++r) {
            a.frameMs = 0.0f;
            a.render();
            times.push_back(a.frameMs);
        }
        std::sort(times.begin(), times.end());
        a.frameMs = times[times.size() / 2];
        for (const float2& pr : probes) a.probe(pr.x, pr.y);
        {
            unsigned long long* st = nullptr;
            cudaMalloc(reinterpret_cast<void**>(&st), 16);
            cudaMemset(st, 0, 16);
            a.statsDev = st;
            a.render();
            unsigned long long hs[2];
            cudaMemcpy(hs, st, 16, cudaMemcpyDeviceToHost);
            const double px = double(a.width) * a.height * std::max(1, a.cfg.i("samples", 6));
            std::printf("ornek basina hucre %.1f, oda ici sekme %.1f\n", double(hs[0]) / px, double(hs[1]) / px);
            a.statsDev = nullptr;
            cudaFree(st);
        }
        const std::string path = out + "/kuyu_bakis" + std::to_string(i);
        const bool ok = a.save(path);
        std::printf("bakis %d: %s  %.1f ms  (%dx%d, %d ornek)  bozuk piksel %u\n", i, ok ? (path + ".png").c_str() : "YAZILAMADI", a.frameMs, a.width,
                    a.height, a.cfg.i("samples", 6), a.lastBad);
    }
    const cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) std::printf("CUDA hatasi: %s\n", cudaGetErrorString(e));
    a.release();
    return e == cudaSuccess ? 0 : 1;
}
