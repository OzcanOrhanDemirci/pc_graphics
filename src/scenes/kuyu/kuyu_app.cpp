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
    case 0:   // the shaft, straight down: one vanishing point, the rooms round it level after level
        c.x = 4.0; c.y = 0.0; c.z = 4.0;
        c.yaw = 0.35f;
        c.pitch = -1.5708f;
        break;
    case 1:   // at a room's glass, looking in
        c.x = 0.0; c.y = 0.25; c.z = 3.7;
        c.yaw = 0.0f;
        c.pitch = 0.0f;
        break;
    case 2:   // sideways, along the lattice
        c.x = 4.0; c.y = 0.4; c.z = 4.0;
        c.yaw = -1.25f;
        c.pitch = -0.12f;
        break;
    case 3:   // up the shaft
        c.x = 4.0; c.y = 0.0; c.z = 4.0;
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
        }
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
        r.nL = 5 + k % 3;
        for (int l = 0; l < r.nL; ++l)
            r.L[l] = bulb((rnd() * 2.0f - 1.0f) * (h - 0.35f), -h + 0.6f + rnd() * (2.0f * h - 1.2f), (rnd() * 2.0f - 1.0f) * (h - 0.35f), starCd,
                          2500.0f + 400.0f * rnd());
        add(r, "yildizlar", 5);
    }
    // one lamp, a little off the middle
    for (int k = 0; k < 2; ++k) {
        RoomVariant r;
        r.kind = 1;
        r.nL = 1;
        r.L[0] = bulb((rnd() - 0.5f) * 0.8f, 0.2f + rnd() * 0.5f, (rnd() - 0.5f) * 0.8f, oneCd, 2300.0f + 300.0f * rnd());
        add(r, "tek", 9);
    }
    // moonlight: a cold lamp
    for (int k = 0; k < 2; ++k) {
        RoomVariant r;
        r.kind = 2;
        r.nL = 1 + k;
        for (int l = 0; l < r.nL; ++l) r.L[l] = bulb((rnd() - 0.5f) * 1.4f, 0.0f + rnd() * 0.8f, (rnd() - 0.5f) * 1.4f, oneCd * 0.6f, 4100.0f);
        add(r, "ay", 6);
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
        r.L[0] = bulb(0.9f, 0.5f + 0.4f * k, -0.6f, oneCd, 2700.0f);
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
        r.L[0] = bulb(0.95f, 0.8f, -0.85f + 1.7f * k, oneCd, 2500.0f);
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
        r.L[0] = bulb(-0.8f, 0.5f, -0.9f, oneCd * 0.5f, 2600.0f);
        r.L[1] = bulb(0.9f, 0.2f, 0.7f, oneCd * 0.5f, 2600.0f);
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
        r.L[0] = bulb(0.7f, 0.6f, 0.5f, oneCd, 2600.0f);
        add(r, "kose", 4);
    }
    // a row of lamps along one axis: endless lines of lights
    {
        RoomVariant r;
        r.kind = 7;
        r.nL = 5;
        for (int l = 0; l < 5; ++l) r.L[l] = bulb(-1.2f + 0.6f * l, 0.3f, 0.0f, starCd, 2700.0f);
        add(r, "sira", 5);
    }
    // dark: no light of its own
    {
        RoomVariant r;
        r.kind = 8;
        add(r, "karanlik", 14);
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
        buildGlowTable(v, dGlow_, cfg.i("glow.grid", 64));
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
        ltNa_ = std::max(8, int(std::lround(lat.C / cfg.f("light.texel.along", 0.04f))));
        ltNw_ = std::max(2, int(std::lround(lat.b / cfg.f("light.texel.across", 0.05f))));
        const size_t texels = size_t(48) * ltNa_ * ltNw_;
        dLtIdx_ = devAlloc<unsigned char>(texels * kLtK);
        dLtG_ = devAlloc<float>(texels * kLtK);
        dLtFar_ = devAlloc<float2>(texels);
        View v = makeView();
        buildLightTable(v, dLtIdx_, dLtG_, dLtFar_, cfg.i("light.shadow.grid", 8), cfg.i("light.far.rays", 256));
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
            for (size_t t : {size_t(1000), size_t(20000), size_t(60000)}) {
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
void KuyuApp::buildFaceGlow() {
    const int N = kGlowN, nv = int(variants.size());
    auto lookup = [&](int var, float3 w) {
        const float2 e = octEncode(w);
        const float x = std::clamp(e.x * N - 0.5f, 0.0f, N - 1.001f), y = std::clamp(e.y * N - 0.5f, 0.0f, N - 1.001f);
        const int x0 = int(x), y0 = int(y);
        const float fx = x - x0, fy = y - y0;
        const float4* t = glowHost_.data() + size_t(var) * N * N;
        const float4 m = (t[y0 * N + x0] * (1.0f - fx) + t[y0 * N + x0 + 1] * fx) * (1.0f - fy) + (t[(y0 + 1) * N + x0] * (1.0f - fx) + t[(y0 + 1) * N + x0 + 1] * fx) * fy;
        return make_float3(m.x, m.y, m.z);
    };
    const int M = 8192;
    std::vector<float3> dirs(M);
    for (int k = 0; k < M; ++k) {
        const double z = 1.0 - 2.0 * (k + 0.5) / M, r = std::sqrt(std::max(0.0, 1.0 - z * z)), ph = k * 2.39996322972865332;
        dirs[k] = make_float3(float(r * std::cos(ph)), float(z), float(r * std::sin(ph)));
    }
    faceGlow.assign(size_t(nv) * 6, make_float4(0, 0, 0, 0));
    double shapeSum[kShapeN] = {}, shapeW[kShapeN] = {};
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
            faceGlow[size_t(vi) * 6 + face] = Lf;
            if (Lf.y <= 0.0f) continue;
            for (const float3& w : dirs) {
                const float c = -comp(w, ax) * sg;
                if (c <= 0.0f) continue;
                const int b = std::min(kShapeN - 1, int(c * kShapeN));
                shapeSum[b] += lookup(vi, w).y;   // (the face's own weight Lf.y cancels: ratio x weight)
                shapeW[b] += Lf.y;
            }
        }
    }
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
    const float3 slat[8] = {cfg.v3("slat.0", make_float3(0.105f, 0.062f, 0.036f)),   // dark walnut
                            cfg.v3("slat.1", make_float3(0.032f, 0.029f, 0.027f)),   // soot
                            cfg.v3("slat.2", make_float3(0.46f, 0.41f, 0.32f)),      // bone
                            cfg.v3("slat.3", make_float3(0.20f, 0.065f, 0.055f)),    // faded bordeaux
                            cfg.v3("slat.4", make_float3(0.30f, 0.21f, 0.09f)),      // dull brass
                            cfg.v3("slat.5", make_float3(0.16f, 0.10f, 0.06f)),      // tan leather
                            cfg.v3("slat.6", make_float3(0.06f, 0.07f, 0.055f)),     // dark green
                            cfg.v3("slat.7", make_float3(0.10f, 0.09f, 0.08f))};
    v.nSlatColors = std::clamp(cfg.i("slat.colors", 7), 1, 8);
    for (int k = 0; k < 8; ++k) v.slatAlbedo[k] = slat[k];
    v.frameAlbedo = cfg.v3("frame.albedo", make_float3(0.08f, 0.05f, 0.03f));
    v.hazeSigma = cfg.f("haze.sigma", 0.006f);
    v.hazeAbsorb = cfg.f("haze.absorb", 0.001f);
    v.hazeAmbient = meanGlow * cfg.f("haze.cover", 0.3f);
    v.ltIdx = dLtIdx_;
    v.ltG = dLtG_;
    v.ltFar = dLtFar_;
    v.ltNa = ltNa_;
    v.ltNw = ltNw_;
    v.faceGlow = dFaceGlow_;
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
