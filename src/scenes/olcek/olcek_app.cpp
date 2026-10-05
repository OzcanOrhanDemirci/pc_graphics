// Scene 7 "Ölçek": the run's state and the command-line tools.
//
//   pc_graphics.exe --scene olcek --headless --s -3,0,3 [--t 2] [--w 1280 --h 720] [--out captures/olcek]
//       frames at those decades (s = log10 of the screen's width in metres), after t seconds of time at each
//   pc_graphics.exe --scene olcek --headless --decades [...]       every whole decade, the bottom and the top
//   pc_graphics.exe --scene olcek --test olcek-hassasiyet          the anchors' places on the screen, floats against
//                                                                  double-doubles, every 0.02 decades (pixels)
//   pc_graphics.exe --scene olcek --test olcek-sure [--step 0.01]  top to bottom and back (--set test.lo=.. test.hi=..
//                                                                  for a part): the frame's time, the home
//                                                                  in the frame, and every jump (something appearing
//                                                                  or vanishing that the zoom does not explain)
//   pc_graphics.exe --scene olcek --test olcek-zaman               the clocks: home always at today's noon, however
//                                                                  long out there and however fast back
//   pc_graphics.exe --scene olcek --test olcek-hiz                 the second option's physics (olcek_hiz_test.cpp)
//   pc_graphics.exe --scene olcek --headless --hiz --phi 0,3 [--ayak 0 --yer 0.5 --gorus 75 --bakis yaw,pitch --yan 1.3,9]
//                                                                  the rocket's eye (or the side view) at those states
#include "scenes/olcek/olcek_app.h"
#include "core/image_io.h"
#include "scenes/olcek/olcek_bugun.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

using namespace olcek;

namespace {
double nowS() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}   // namespace

void OlcekApp::loadConfig() {
    cfg = Config();
    cfg.load(root + "/scenes/olcek.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    samples = cfg.i("samples", 4);
    sc.configure(cfg);
}

void OlcekApp::init() {
    sc.init();
    sc.s = cfg.f("start", 0.0f);
    const size_t n = size_t(sc.width) * sc.height;
    cudaMalloc(reinterpret_cast<void**>(&accum), n * sizeof(float4));
    cudaMalloc(reinterpret_cast<void**>(&depth), n * sizeof(float));
    cudaMalloc(reinterpret_cast<void**>(&output), n * sizeof(uchar4));
    splatCap = 1 << 18;
    cudaMalloc(reinterpret_cast<void**>(&dSplats), size_t(splatCap) * sizeof(GSplat));
    partCap = 1 << 15;
    tileIdxCap = 1 << 20;
    cudaMalloc(reinterpret_cast<void**>(&dParts), size_t(partCap) * sizeof(GParticle));
    cudaMalloc(reinterpret_cast<void**>(&dTileIdx), size_t(tileIdxCap) * sizeof(int));
    const int tiles = ((sc.width + kTile - 1) / kTile) * ((sc.height + kTile - 1) / kTile);
    cudaMalloc(reinterpret_cast<void**>(&dTileStart), size_t(tiles + 1) * sizeof(int));
}

// the particles into 16-pixel tiles, each tile's list in the list's order (the world sorts it far to near)
void OlcekApp::binParticles() {
    const std::vector<GParticle>& P = sc.particles;
    const int tx = (sc.width + kTile - 1) / kTile, ty = (sc.height + kTile - 1) / kTile;
    tileStart.assign(size_t(tx * ty + 1), 0);
    auto box = [&](const GParticle& p, int& x0, int& x1, int& y0, int& y1) {
        float r, cx = p.c.x, cy = p.c.y, hx = 0.0f, hy = 0.0f;
        if (p.kind == kPartGlow) r = 3.5f * p.r;
        else if (p.kind == kPartSphere) r = p.r + 1.0f;
        else if (p.kind == kPartRing) r = p.r + 3.5f * p.w;
        else {
            r = 3.5f * p.w;
            cx += 0.5f * p.ax.x;
            cy += 0.5f * p.ax.y;
            hx = 0.5f * std::fabs(p.ax.x);
            hy = 0.5f * std::fabs(p.ax.y);
        }
        x0 = std::max(int(std::floor((cx - hx - r) / kTile)), 0);
        x1 = std::min(int(std::floor((cx + hx + r) / kTile)), tx - 1);
        y0 = std::max(int(std::floor((cy - hy - r) / kTile)), 0);
        y1 = std::min(int(std::floor((cy + hy + r) / kTile)), ty - 1);
    };
    for (const GParticle& p : P) {
        int x0, x1, y0, y1;
        box(p, x0, x1, y0, y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) ++tileStart[size_t(y * tx + x + 1)];
    }
    for (size_t i = 1; i < tileStart.size(); ++i) tileStart[i] += tileStart[i - 1];
    tileIdx.resize(size_t(tileStart.back()));
    std::vector<int> fill(tileStart.begin(), tileStart.end() - 1);
    for (int i = 0; i < int(P.size()); ++i) {
        int x0, x1, y0, y1;
        box(P[size_t(i)], x0, x1, y0, y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) tileIdx[size_t(fill[size_t(y * tx + x)]++)] = i;
    }
}

void OlcekApp::loadMaps() {
    mapsLog.clear();
    const std::string dir = root + "/assets/olcek/";
    if (cfg.i("stars.real", 1)) sc.loadStars(dir + "yildizlar.bin", mapsLog);
    sc.loadBelts(dir + "kusaklar.txt", mapsLog);
    {
        std::string err;
        if (cfg.i("web.volume", 1) && web.load(dir + "ag.bin", err)) {
            sc.volumeWeb_ = true;
            char b[160];
            std::snprintf(b, sizeof(b), "  ag.bin: %d^3, %.0f Mpc, %d duzey%c", web.size, web.box, web.levels, 10);
            mapsLog += b;
        } else if (!err.empty()) {
            mapsLog += "  " + err + std::string(1, char(10));
        }
    }
    if (cfg.i("galaxies.real", 1)) sc.loadGalaxies(dir + "galaksiler.bin", mapsLog);
    struct M { olcek::Texture* t; const char* file; bool wrap; };
    const M ms[] = {{&world, "bmng_dunya.tex", true}, {&zonal, "bmng_kusak.tex", false}, {&region, "bmng_ege.tex", false},
                    {&bay, "s2_ev.tex", false}, {&moon, "ay.tex", true}, {&cloudWorld, "bulut_dunya.tex", true},
                    {&cloudRegion, "bulut_ege.tex", false}};
    {
        std::string err;
        if (shore.load(dir + "kiyi.sdf", err)) {
            char b[160];
            std::snprintf(b, sizeof(b), "  kiyi.sdf: %dx%d, %.1f m, sahildeki noktada %.1f m%c", shore.size, shore.size, shore.texel, shore.centre, 10);
            mapsLog += b;
        } else {
            mapsLog += "  " + err + std::string(1, char(10));
        }
    }
    for (const M& m : ms) {
        std::string err;
        if (cfg.i(std::string("maps.") + m.file, 1) == 0) continue;   // --set maps.<file>=0: without it
        if (m.t->load(dir + m.file, m.wrap, err)) {
            char b[160];
            std::snprintf(b, sizeof(b), "  %s: %dx%d, %.0f MB\n", m.file, m.t->width, m.t->height, m.t->bytes / 1e6);
            mapsLog += b;
        } else {
            mapsLog += "  " + err + "\n";
        }
    }
}

void OlcekApp::release() {
    world.release();
    web.release();
    cloudWorld.release();
    cloudRegion.release();
    shore.release();
    zonal.release();
    region.release();
    bay.release();
    moon.release();
    cudaFree(dSky);
    cudaFree(dBB);
    dSky = nullptr;
    dBB = nullptr;
    cudaFree(accum);
    cudaFree(depth);
    cudaFree(output);
    cudaFree(dSplats);
    cudaFree(dParts);
    cudaFree(dTileStart);
    cudaFree(dTileIdx);
    accum = nullptr;
    depth = nullptr;
    output = nullptr;
    dSplats = nullptr;
}

void OlcekApp::render() {
    const double h0 = nowS();
    sc.place();
    sc.camera();
    sc.build(view, splats);
    view.filterRadius = cfg.f("filter", 1.5f);
    view.maps.world = world.tex;
    view.maps.zonal = zonal.tex;
    view.maps.region = region.tex;
    view.maps.bay = bay.tex;
    view.maps.moon = moon.tex;
    view.web.field = web.tex;
    if (!web.tex) view.web.on = 0;
    view.web.boxA = web.box > 0.0f ? web.box : 600.0f;
    view.web.boxB = view.web.boxA * 2.55f;
    view.web.cell = view.web.boxA / float(web.size > 0 ? web.size : 256);
    view.maps.cloudWorld = cloudWorld.tex;
    view.maps.cloudRegion = cloudRegion.tex;
    view.maps.cloudGain = cfg.f("maps.gain.cloud", 1.0f);
    {   // the clouds thin out as the camera comes down through their heights (~2-8 km)
        const double x = std::clamp((sc.s - 3.7) / (4.3 - 3.7), 0.0, 1.0);
        view.maps.cloudFade = float(x * x * (3.0 - 2.0 * x));
    }
    view.maps.shore = shore.tex;
    view.maps.shoreHalf = 0.5f * shore.size * shore.texel;
    view.maps.shoreCentre = shore.centre;
    view.maps.shoreSize = shore.size;
    view.maps.worldGain = cfg.f("maps.gain.world", 1.0f);
    view.maps.bayGain = cfg.f("maps.gain.bay", 0.25f);
    view.maps.moonGain = cfg.f("maps.gain.moon", 0.6f);
    if (world.width) view.maps.worldTexels = float(world.width);
    if (moon.width) view.maps.moonTexels = float(moon.width);
    hostMs = (nowS() - h0) * 1000.0;
    draw(samples);
}

void OlcekApp::draw(int samplesThis) {
    const double h0 = nowS();
    binParticles();
    if (int(sc.particles.size()) > partCap) {
        cudaFree(dParts);
        partCap = int(sc.particles.size()) * 2;
        cudaMalloc(reinterpret_cast<void**>(&dParts), size_t(partCap) * sizeof(GParticle));
    }
    if (int(tileIdx.size()) > tileIdxCap) {
        cudaFree(dTileIdx);
        tileIdxCap = int(tileIdx.size()) * 2;
        cudaMalloc(reinterpret_cast<void**>(&dTileIdx), size_t(tileIdxCap) * sizeof(int));
    }
    if (int(splats.size()) > splatCap) {
        cudaFree(dSplats);
        splatCap = int(splats.size()) * 2;
        cudaMalloc(reinterpret_cast<void**>(&dSplats), size_t(splatCap) * sizeof(GSplat));
    }
    hostMs += (nowS() - h0) * 1000.0;
    cudaEvent_t a, b;
    cudaEventCreate(&a);
    cudaEventCreate(&b);
    cudaEventRecord(a);
    setView(view);
    if (!splats.empty()) cudaMemcpyAsync(dSplats, splats.data(), splats.size() * sizeof(GSplat), cudaMemcpyHostToDevice, 0);
    renderFrame(accum, depth, sc.width, sc.height, samplesThis, 0);
    splatPoints(dSplats, int(splats.size()), accum, depth, sc.width, sc.height, 0);
    if (!sc.particles.empty()) {
        cudaMemcpyAsync(dParts, sc.particles.data(), sc.particles.size() * sizeof(GParticle), cudaMemcpyHostToDevice, 0);
        cudaMemcpyAsync(dTileStart, tileStart.data(), tileStart.size() * sizeof(int), cudaMemcpyHostToDevice, 0);
        cudaMemcpyAsync(dTileIdx, tileIdx.data(), tileIdx.size() * sizeof(int), cudaMemcpyHostToDevice, 0);
        drawParticles(dParts, dTileStart, dTileIdx, accum, sc.width, sc.height, 0);
    }
    resolve(accum, output, sc.width, sc.height, view.saturation, 0);
    cudaEventRecord(b);
    cudaEventSynchronize(b);
    cudaEventElapsedTime(&frameMs, a, b);
    cudaEventDestroy(a);
    cudaEventDestroy(b);
}

void OlcekApp::prepareHiz() {
    namespace hz = olcek::hiz;
    using namespace olcek::hiz;
    route = makeRoute();
    ship = depart(route);
    hizLog.clear();
    // the catalogue's Proxima to Gaia DR3's place (HYG has it ~1,300 AU off): the star the rocket stops by is the one it sees
    proximaStar = -1;
    const int i = sc.nearestStar(route.proxima);
    if (i >= 0 && length(sc.stars()[size_t(i)] - route.proxima) < 0.05 * kPc) {
        char b[200];
        std::snprintf(b, sizeof(b), "  Proxima: katalogda %.4f pc, Gaia DR3 %.6f pc (%.0f AU tasindi)\n", length(sc.stars()[size_t(i)]) / kPc,
                      length(route.proxima) / kPc, length(sc.stars()[size_t(i)] - route.proxima) / kAU);
        hizLog += b;
        sc.moveStar(i, route.proxima);
        proximaStar = i;
    } else {
        hizLog += "  Proxima katalogda bulunamadi (0,05 pc icinde yildiz yok)\n";
    }
    bb.build(root + "/assets/olcek/cie1931_2deg.csv");
    {   // the blackbody table on the GPU: colour and visible share over log T (the eye's diffuse sky)
        std::vector<float4> t(static_cast<size_t>(Blackbody::kN));
        for (int i = 0; i < Blackbody::kN; ++i) {
            const double T = std::pow(10.0, Blackbody::kLogT0 + (Blackbody::kLogT1 - Blackbody::kLogT0) * i / (Blackbody::kN - 1));
            const float3 c = bb.colour(T);
            t[size_t(i)] = make_float4(c.x, c.y, c.z, float(bb.logShare(T)));
        }
        cudaFree(dBB);
        cudaMalloc(reinterpret_cast<void**>(&dBB), t.size() * sizeof(float4));
        cudaMemcpy(dBB, t.data(), t.size() * sizeof(float4), cudaMemcpyHostToDevice);
    }
    {   // the Milky Way's panorama (tools/olcek_veri.py samanyolu: ESO/S. Brunier, stars taken out): float16 RGBA
        std::ifstream f(root + "/assets/olcek/samanyolu_gok.bin", std::ios::binary);
        char magic[4] = {};
        uint32_t wh[2] = {};
        if (f && f.read(magic, 4) && std::memcmp(magic, "OLSK", 4) == 0 && f.read(reinterpret_cast<char*>(wh), 8)) {
            const int w = int(wh[0]), h = int(wh[1]);
            std::vector<uint16_t> raw(size_t(w) * h * 4);
            if (f.read(reinterpret_cast<char*>(raw.data()), std::streamsize(raw.size() * 2))) {
                auto half = [](uint16_t v) {   // IEEE half to float
                    const uint32_t s = uint32_t(v & 0x8000u) << 16, e = (v >> 10) & 0x1Fu, m = v & 0x3FFu;
                    uint32_t bits;
                    if (e == 0) {
                        if (m == 0) bits = s;
                        else {   // subnormal
                            int ee = -1;
                            uint32_t mm = m;
                            do { ++ee; mm <<= 1; } while ((mm & 0x400u) == 0);
                            bits = s | uint32_t(127 - 15 - ee) << 23 | (mm & 0x3FFu) << 13;
                        }
                    } else if (e == 31) bits = s | 0x7F800000u | m << 13;
                    else bits = s | (e + 112u) << 23 | m << 13;
                    float out;
                    std::memcpy(&out, &bits, 4);
                    return out;
                };
                std::vector<float4> px(size_t(w) * h);
                for (size_t i = 0; i < px.size(); ++i) px[i] = make_float4(half(raw[4 * i]), half(raw[4 * i + 1]), half(raw[4 * i + 2]), half(raw[4 * i + 3]));
                // its levels, each half the last (the light averaged, T weighted by it): where the sky crowds forward a
                // pixel reads the level as wide as its footprint, so nothing shimmers as the speed grows
                std::vector<float4> all = px;
                {
                    int lw = w, lh = h;
                    size_t off = 0;
                    skyLod = 1;
                    while (lw > 1 && lh > 1 && skyLod < 14) {
                        const int nw = lw / 2, nh = lh / 2;
                        std::vector<float4> nx(size_t(nw) * nh);
                        for (int y = 0; y < nh; ++y)
                            for (int x = 0; x < nw; ++x) {
                                double r = 0, g = 0, b = 0, tl = 0, lsum = 0;
                                for (int j = 0; j < 2; ++j)
                                    for (int i = 0; i < 2; ++i) {
                                        const float4 p = all[off + size_t(2 * y + j) * lw + size_t(2 * x + i)];
                                        const double l = 0.2126 * p.x + 0.7152 * p.y + 0.0722 * p.z;
                                        r += p.x;
                                        g += p.y;
                                        b += p.z;
                                        tl += p.w * l;
                                        lsum += l;
                                    }
                                double T = 0.0;
                                if (lsum > 0.0) T = tl / lsum;
                                else
                                    for (int j = 0; j < 2; ++j)
                                        for (int i = 0; i < 2; ++i) T += 0.25 * all[off + size_t(2 * y + j) * lw + size_t(2 * x + i)].w;
                                nx[size_t(y) * nw + size_t(x)] = make_float4(float(r / 4), float(g / 4), float(b / 4), float(T));
                            }
                        off += size_t(lw) * lh;
                        all.insert(all.end(), nx.begin(), nx.end());
                        lw = nw;
                        lh = nh;
                        ++skyLod;
                    }
                }
                cudaFree(dSky);
                cudaMalloc(reinterpret_cast<void**>(&dSky), all.size() * sizeof(float4));
                cudaMemcpy(dSky, all.data(), all.size() * sizeof(float4), cudaMemcpyHostToDevice);
                skyW = w;
                skyH = h;
                // the coarse copy (256 x 128): light averaged, the temperature weighted by it
                const int cw = 256, ch = 128, fx = w / cw, fy = h / ch;
                skyCoarse.w = cw;
                skyCoarse.h = ch;
                skyCoarse.px.assign(size_t(cw) * ch, make_float4(0, 0, 0, 0.48f));
                for (int y = 0; y < ch; ++y)
                    for (int x = 0; x < cw; ++x) {
                        double r = 0, g = 0, b = 0, tl = 0, lw = 0;
                        for (int j = 0; j < fy; ++j)
                            for (int i = 0; i < fx; ++i) {
                                const float4 p = px[size_t(y * fy + j) * w + size_t(x * fx + i)];
                                const double l = 0.2126 * p.x + 0.7152 * p.y + 0.0722 * p.z;
                                r += p.x;
                                g += p.y;
                                b += p.z;
                                tl += p.w * l;
                                lw += l;
                            }
                        const double n = double(fx * fy);
                        skyCoarse.px[size_t(y) * cw + size_t(x)] = make_float4(float(r / n), float(g / n), float(b / n), float(lw > 0.0 ? tl / lw : 0.48));
                    }
                char b[160];
                std::snprintf(b, sizeof(b), "  Samanyolu panoramasi: %dx%d (ESO/S. Brunier, yildizlari silinmis)\n", w, h);
                hizLog += b;
            }
        }
        if (skyW == 0) hizLog += "  Samanyolu panoramasi yok (tools/olcek_veri.py samanyolu): gokyuzu yalniz yildizlar ve fon isimasi\n";
    }
    {   // the night sky at the Sun's place (the Sun sits at the eye there and is left out)
        hz::EyeSum sum;
        hz::Eye e;
        e.width = sc.width;
        e.height = sc.height;
        hz::StarSet st;
        st.pos = &sc.stars();
        st.flux = &sc.starLight();
        st.temp = &sc.starTemp();
        std::vector<GSplat> sp;
        hz::buildEye(st, bb, e, sp, sum);
        const V3 galY = cross(sc.galNorth(), sc.galCentreDir());
        hz::skySums(skyCoarse, bb, e, 1.0, sc.galCentreDir(), galY, sc.galNorth(), sum);   // the Milky Way's diffuse light, the CMB
        nightSky = std::max(sum.visible - sum.visibleSun, 1e-12);
        nightSkyBol = std::max(sum.bolometric, 1e-12);
        char b[200];
        std::snprintf(b, sizeof(b), "  gece goku (Gunes'te, Gunes haric): gorunur %.3e W/m^2, butun bantlar %.3e W/m^2\n", nightSky, nightSkyBol);
        hizLog += b;
    }
    char b[256];
    std::snprintf(b, sizeof(b), "  renk tablosu: %s, %d dalga boyu\n  yol: %.4f isik yili, donus %.1f derece, %.4f milyon isik yili (Gunes'e en yakin %.2f isik yili)\n",
                  bb.source.c_str(), bb.samples, route.length[0] / kLy, route.turnDeg, route.length[1] / kLy * 1e-6, route.sunPass / kLy);
    hizLog += b;
}

namespace {
// the eye's adaptation (EV) to a sky 2^x times the night sky's light: part of it in the dark (hiz.uyum: a brighter sky
// looks brighter), all of it in daylight (Weber: there the eye keeps the contrast, not the light), from hiz.gun EV above
// the night sky on, smoothly; a dark sky opens it by at most 6 EV. day: how far into daylight (0 .. 1)
double adaptEv(double x, double adapt, double dayEv, double* day = nullptr) {
    const double w = 1.5, u = (x - dayEv) / w;
    if (day) *day = 1.0 / (1.0 + std::exp(-std::clamp(u, -60.0, 60.0)));
    const double sp = u > 30.0 ? x - dayEv : w * std::log1p(std::exp(u));
    return std::clamp(adapt * x + (1.0 - adapt) * sp, -6.0, 120.0);
}
}   // namespace

double OlcekApp::eyeEv(double skyVisible) const {
    // at rest far from the Sun: about the 7th magnitude at the edge of seeing (hiz.ev); a sky twice as bright closes the
    // eye by hiz.uyum EV in the dark, by a whole EV in daylight (the hot cap ahead is a small sun: the eye adapts to it)
    const double base = cfg.f("hiz.ev", 17.5f), adapt = cfg.f("hiz.uyum", 0.5f);
    if (!std::isfinite(skyVisible)) return base;
    return base - adaptEv(std::log2(std::max(skyVisible, 1e-30) / nightSky), adapt, cfg.f("hiz.gun", 16.0f));
}

double OlcekApp::eyeEvEnergy(double skyBol) const {
    // the same eye on every band's light, adapting to all of it: at rest the night sky as bright as the visible eye's
    // (base shifted by their ratio: the cosmic background's microwaves are some 60% of it, so the Milky Way is dimmer
    // here), adapting the same way
    const double base = cfg.f("hiz.ev", 17.5f) + std::log2(nightSky / nightSkyBol), adapt = cfg.f("hiz.uyum", 0.5f);
    if (!std::isfinite(skyBol)) return base;
    return base - adaptEv(std::log2(std::max(skyBol, 1e-30) / nightSkyBol), adapt, cfg.f("hiz.gun", 16.0f));
}

double OlcekApp::eyeDay(double sky, bool energy) const {
    double day = 0.0;
    if (!std::isfinite(sky)) return day;
    adaptEv(std::log2(std::max(sky, 1e-30) / (energy ? nightSkyBol : nightSky)), cfg.f("hiz.uyum", 0.5f), cfg.f("hiz.gun", 16.0f), &day);
    return day;
}

double OlcekApp::skyPanoWeight(const olcek::hiz::Eye& e) const {
    // the panorama is the sky as seen from here: whole within ~300 light years of the Sun, gone by ~3,000
    const double d = length(e.pos) / olcek::hiz::kLy;
    const double u = std::clamp((d - 300.0) / 2700.0, 0.0, 1.0);
    return 1.0 - u * u * (3.0 - 2.0 * u);
}

double OlcekApp::sideEv(double s, olcek::V3 rocket) const {
    // near the Sun the first option's curve (the sunlit Earth, the planets' rings), from ~30 AU to ~3,000 AU over to
    // deep space's (the stars, the first option's own space exposure)
    const double space = cfg.f("hiz.yan.ev", 23.5f);
    const double u = std::clamp((std::log10(std::max(length(rocket), 1.0)) - std::log10(30.0 * olcek::hiz::kAU)) / 2.0, 0.0, 1.0);
    const double uu = u * u * (3.0 - 2.0 * u);
    // above the Galaxy's disc at a few thousand light years (s 18.5 - 20.5) its light fills the screen: the eye adapts to
    // it (darker), so its structure shows instead of a grey veil
    const double dip = 2.5 * std::exp(-0.5 * std::pow((s - 19.5) / 0.55, 2.0));
    // and between the galaxies, a dark screen with two small discs in it: the eye opens to them (their outer discs show)
    const double t = std::clamp((s - 21.8) / 0.8, 0.0, 1.0);
    const double open = 2.5 * t * t * (3.0 - 2.0 * t);
    return std::max(sc.ev(s), sc.ev(s) * (1.0 - uu) + space * uu) - dip + open;
}

olcek::hiz::Eye OlcekApp::eyeAt(const olcek::hiz::Ship& s, double yaw, double pitch, double tanHalfX) const {
    namespace hz = olcek::hiz;
    // the rocket's frame: the nose ahead, the Galaxy's north up across it; the head turned by yaw (about up) and pitch
    const V3 f0 = s.heading;
    V3 up0 = v3(olcek::today::kGalNorth);
    up0 = up0 - f0 * dot(up0, f0);
    if (length(up0) < 1e-6) up0 = v3(0.0, 0.0, 1.0) - f0 * f0.z;
    up0 = normalize(up0);
    const V3 f1 = rotate(f0, up0, yaw);
    const V3 r1 = normalize(cross(f1, up0));
    hz::Eye e;
    e.pos = hz::position(s, route);
    e.motion = s.phi > 0.0 ? hz::motionDir(s, route) : s.heading;
    e.phi = s.phi;
    e.fwd = rotate(f1, r1, pitch);
    e.up = rotate(up0, r1, pitch);
    e.right = r1;
    e.width = sc.width;
    e.height = sc.height;
    e.tanHalfX = tanHalfX;
    e.tanHalfY = tanHalfX * sc.height / sc.width;
    // a point's light spread over the first option's pixel (fov 40): the same star looks the same in both, and the
    // binoculars do not brighten it; the eye adapts over the unzoomed field (75 degrees across)
    const double pa0 = 2.0 * std::tan(20.0 * 3.14159265358979323846 / 180.0) / sc.width;
    e.omegaPx = pa0 * pa0;
    const double t0 = std::tan(37.5 * 3.14159265358979323846 / 180.0), t0y = t0 * sc.height / sc.width;
    e.adaptDiag = std::atan(std::sqrt(t0 * t0 + t0y * t0y));
    return e;
}

void OlcekApp::renderEye(const olcek::hiz::Eye& e, olcek::hiz::EyeSum& sum) {
    const double h0 = nowS();
    olcek::hiz::StarSet st;
    st.pos = &sc.stars();
    st.flux = &sc.starLight();
    st.temp = &sc.starTemp();
    olcek::hiz::buildEye(st, bb, e, splats, sum);
    const double panoW = skyPanoWeight(e);
    const V3 galY = cross(sc.galNorth(), sc.galCentreDir());
    olcek::hiz::skySums(skyCoarse, bb, e, panoW, sc.galCentreDir(), galY, sc.galNorth(), sum);
    view.width = sc.width;
    view.height = sc.height;
    view.tanHalfX = float(e.tanHalfX);
    view.tanHalfY = float(e.tanHalfY);
    view.pixAngle = float(2.0 * e.tanHalfX / sc.width);
    view.filterRadius = cfg.f("filter", 1.5f);
    view.saturation = 1.0f;
    view.background = make_float3(0, 0, 0);
    view.nBalls = view.nBlobs = view.nDiscs = 0;
    view.ground = GGround{};
    view.field = GField{};
    view.atmo = GAtmo{};
    view.web = GWeb{};
    view.dens = GDensity{};
    for (GGalaxy& g : view.galaxy) g = GGalaxy{};
    {   // the diffuse sky by the pixel (the kernel's eyeSky)
        GSky& g = view.sky;
        g = GSky{};
        g.on = 1;
        g.energy = e.energy ? 1 : 0;
        g.mx = dot(e.motion, e.right);
        g.my = dot(e.motion, e.up);
        g.mz = dot(e.motion, e.fwd);
        const olcek::hiz::Kin k = olcek::hiz::kin(e.phi);
        g.emp = std::exp(-e.phi);
        g.gamma = k.gamma;
        g.beta = k.beta;
        g.eps = k.eps;
        g.tanX = e.tanHalfX;
        g.tanY = e.tanHalfY;
        auto row = [&](V3 a) { return make_float3(float(dot(a, e.right)), float(dot(a, e.up)), float(dot(a, e.fwd))); };
        g.gal0 = row(sc.galCentreDir());
        g.gal1 = row(galY);
        g.gal2 = row(sc.galNorth());
        g.pano = dSky;
        g.pw = skyW;
        g.ph = skyH;
        g.nLod = skyLod;
        g.pixAngle = float(2.0 * e.tanHalfX / sc.width);
        g.panoWeight = float(panoW);
        g.bb = dBB;
        g.bbN = olcek::hiz::Blackbody::kN;
        g.bbLogT0 = float(olcek::hiz::Blackbody::kLogT0);
        g.bbStep = float((olcek::hiz::Blackbody::kLogT1 - olcek::hiz::Blackbody::kLogT0) / (olcek::hiz::Blackbody::kN - 1));
        const double dl = 264.021 * 3.14159265358979323846 / 180.0, db = 48.253 * 3.14159265358979323846 / 180.0;
        g.cmbDir = make_float3(float(std::cos(db) * std::cos(dl)), float(std::cos(db) * std::sin(dl)), float(std::sin(db)));
        // the diffuse light seen with the dark-adapted eye's summation over area (Ricco): a designed gain on it (the
        // display's pixel cannot sum; the points keep theirs), gone in daylight (the eye sums no more there)
        const double sky = (e.energy ? sum.bolometricView : sum.visibleView) / std::max(sum.viewShare, 1e-12);
        const double kd = e.k * std::pow(double(cfg.f("hiz.yaygin", 16.0f)), 1.0 - eyeDay(sky, e.energy));
        g.k = float(kd);
        g.lk = float(std::log10(std::max(kd, 1e-300)));
        // the glare: round the bright cap ahead (the diffuse sky within 5 degrees of the motion) and the brightest points
        {
            auto add = [&](V3 dirRest, double E, float3 col) {
                if (g.nGlare >= 8 || !(E > 0.0)) return;
                g.glareDir[g.nGlare] = make_float3(float(dot(dirRest, e.right)), float(dot(dirRest, e.up)), float(dot(dirRest, e.fwd)));
                g.glareE[g.nGlare] = col * float(E * e.k);
                ++g.nGlare;
            };
            const double Ef = e.energy ? sum.fwdBolometric : sum.fwdVisible;
            const float3 cf = e.energy ? energyColour(float(std::log10(std::max(sum.fwdT, 1.0))), bb.colour(sum.fwdT)) : bb.colour(sum.fwdT);
            add(e.motion, Ef * cfg.f("hiz.parlama", 1.0f), cf);
            // the brightest points (a display value above the glare's threshold): their light back from the splats
            std::vector<std::pair<double, size_t>> bright;
            for (size_t i = 0; i < splats.size(); ++i) {
                const GSplat& p = splats[i];
                const double f = double(p.flux.x + p.flux.y + p.flux.z) / 3.0;
                if (f > 20.0) bright.push_back({f, i});
            }
            std::sort(bright.begin(), bright.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (size_t j = 0; j < bright.size() && g.nGlare < 8; ++j) {
                const GSplat& p = splats[bright[j].second];
                const float3 d = make_float3(p.p.x, p.p.y, p.p.z);
                const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                g.glareDir[g.nGlare] = make_float3(d.x / len, d.y / len, d.z / len);
                // the splat's flux is display x pixel solid angle: its E x k is flux x omegaPx (the unzoomed pixel's)
                g.glareE[g.nGlare] = p.flux * float(e.omegaPx * cfg.f("hiz.parlama", 1.0f));
                ++g.nGlare;
            }
        }
        if (!dBB) g.on = 0;
    }
    sc.particles.clear();
    sc.particleTrack.clear();
    hostMs = (nowS() - h0) * 1000.0;
    draw(1);
}

void OlcekApp::save(const std::string& path) {
    std::vector<unsigned char> px(size_t(sc.width) * sc.height * 4);
    cudaMemcpy(px.data(), output, px.size(), cudaMemcpyDeviceToHost);
    writePNG(path + ".png", sc.width, sc.height, px.data());
}

void OlcekApp::luminanceQuarter(std::vector<float>& lum) {
    static float* dq = nullptr;
    static size_t cap = 0;
    const size_t n = size_t(sc.width / 4) * (sc.height / 4);
    if (cap < n) {
        if (dq) cudaFree(dq);
        cudaMalloc(reinterpret_cast<void**>(&dq), n * sizeof(float));
        cap = n;
    }
    quarterLuma(output, dq, sc.width, sc.height, 0);
    lum.resize(n);
    cudaMemcpy(lum.data(), dq, n * sizeof(float), cudaMemcpyDeviceToHost);
}

// ---------------------------------------------------------------------------------------------------------------------

namespace {

double smoothstepD(double a, double b, double x) {
    x = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

std::vector<double> doubles(const std::string& s) {
    std::vector<double> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) out.push_back(std::stod(part));
    return out;
}

// --test olcek-hassasiyet
int testPrecision(OlcekApp& app) {
    Scale& sc = app.sc;
    const int nodes[] = {kLaniakea, kLocalGroup, kGalCentre, kSun, kEarth, kBeach, kNucleus, kProton, kQuark, kMoon, kM31, kLMC};
    const char* names[] = {"laniakea", "yerel grup", "galaksi merkezi", "gunes", "dunya", "sahil", "cekirdek", "proton", "kuark", "ay", "M31", "LMC"};
    double worst = 0.0, worstS = 0.0;
    int worstNode = -1, checks = 0;
    std::printf("hassasiyet: her 0,02 onlukta gorunur capalarin ekrandaki yeri, float (cizimin yolu) ile double-double karsilastirmasi\n");
    std::printf("  onluk   en kotu (piksel)  hangisi\n");
    for (int dec = int(std::floor(sc.sMin)); dec <= int(std::ceil(sc.sMax)); ++dec) {
        double dw = 0.0;
        int dn = -1;
        for (double s = dec; s < dec + 1.0; s += 0.02) {
            if (s < sc.sMin || s > sc.sMax) continue;
            sc.s = s;
            sc.place();
            sc.camera();
            for (int i = 0; i < int(sizeof(nodes) / sizeof(nodes[0])); ++i) {
                // the anchor itself and a point a third of the screen beside it: on the screen, and near enough to be
                // more than a point at infinity (beyond 10^6 D only its direction counts, and that holds anyway)
                for (int side = 0; side < 2; ++side) {
                    const V3 local = side ? sc.cam.right * (0.3 * sc.cam.W) + sc.cam.up * (0.1 * sc.cam.W) : V3{};
                    const V3 p = sc.onScreen(nodes[i], local);
                    if (p.z <= 0.0 || std::fabs(p.x) > 0.6 || std::fabs(p.y) > 0.35) continue;
                    const V3 rel = sc.tree.rel(sc.cam.ref, nodes[i]) + local - sc.cam.pos;
                    if (length(rel) > 1e6 * sc.cam.D) continue;
                    const double e = sc.precisionError(nodes[i], local);
                    ++checks;
                    if (e > dw) { dw = e; dn = i; }
                    if (e > worst) { worst = e; worstS = s; worstNode = i; }
                }
            }
        }
        std::printf("  %4d    %.2e          %s\n", dec, dw, dn >= 0 ? names[dn] : "-");
    }
    std::printf("hassasiyet: %d olcum, en kotu %.3e piksel (s = %.2f, %s); esik 0,01 piksel: %s\n", checks, worst, worstS,
                worstNode >= 0 ? names[worstNode] : "-", worst < 0.01 ? "GECTI" : "KALDI");
    return worst < 0.01 ? 0 : 1;
}

// --test olcek-sure
int testContinuity(OlcekApp& app, double step) {
    Scale& sc = app.sc;
    const int w4 = sc.width / 4, h4 = sc.height / 4;
    std::vector<float> prev, cur;
    double prevW = 0.0;
    V3 prevF;
    int prevRef = -1;
    struct Flag { double s, v; int bx, by; };
    std::vector<Flag> flags;
    std::vector<double> msByDecade(80, 0.0), maxMsByDecade(80, 0.0), hostByDecade(80, 0.0);
    std::vector<int> nByDecade(80, 0);
    double worstHome = 0.0, worstHomeS = 0.0;
    int frames = 0;
    const double threshold = app.cfg.f("test.jump", 0.04f);
    std::printf("sure: ust uctan alta ve geri, %.3f onluk adimla, zaman donuk (yalniz yakinlasma)\n", step);
    // --set test.lo=.. test.hi=..: a part of the way only (looking into one place)
    const double lo = std::max(double(sc.sMin), double(app.cfg.f("test.lo", float(sc.sMin))));
    const double hi = std::min(double(sc.sMax), double(app.cfg.f("test.hi", float(sc.sMax))));
    // the particles below the light: one that appears or vanishes in a step well inside the screen is a jump, whatever
    // the picture's blocks say (thin rings sliding by their own width fill those with honest motion)
    std::unordered_map<uint64_t, Scale::PartTrack> prevParts;
    double popWorst[80] = {};
    double popWorstS[80] = {};
    std::vector<double> path;
    for (double s = hi; s > lo; s -= step) path.push_back(s);
    path.push_back(lo);
    for (double s = lo + step; s < hi; s += step) path.push_back(s);
    const double t0 = nowS();
    for (size_t f = 0; f < path.size(); ++f) {
        sc.s = path[f];
        app.render();
        app.luminanceQuarter(cur);
        const int dec = std::clamp(int(std::floor(sc.s)) + 40, 0, 79);
        msByDecade[dec] += app.frameMs;
        maxMsByDecade[dec] = std::max(maxMsByDecade[dec], double(app.frameMs));
        hostByDecade[dec] += app.hostMs;
        ++nByDecade[dec];
        // the home in the frame: the beach point (and, inside it, what the way in follows)
        {
            // what the way in follows at this depth (the centre's anchors, PLAN §3)
            const int home = sc.s < -14.9 ? kQuark : sc.s < -14.2 ? kProton : sc.s < -10.0 ? kNucleus : kBeach;
            const V3 p = sc.onScreen(home);
            const double m = std::max(std::fabs(p.x) / 0.5, std::fabs(p.y) / (0.5 * sc.height / sc.width));
            if (p.z <= 0.0 || m > worstHome) { worstHome = p.z <= 0.0 ? 99.0 : m; worstHomeS = sc.s; }
        }
        const V3 F = sc.focus();
        if (!prev.empty()) {
            // where frame f's pixels were in frame f-1: the focus plane scales by W and slides by the focus's move
            const V3 dF = sc.tree.rel(prevRef, sc.cam.ref) + F - prevF;
            const double dr = dot(dF, sc.cam.right), du = dot(dF, sc.cam.up);
            const double W1 = sc.cam.W;
            std::vector<float> diff(size_t(w4) * h4, 0.0f);
            for (int y = 0; y < h4; ++y)
                for (int x = 0; x < w4; ++x) {
                    const double u = (x + 0.5) / w4 - 0.5, v = (0.5 - (y + 0.5) / h4) * double(h4) / w4;
                    const double ui = (u * W1 + dr) / prevW, vi = (v * W1 + du) / prevW;
                    const double xi = (ui + 0.5) * w4 - 0.5, yi = (0.5 - vi * double(w4) / h4) * h4 - 0.5;
                    // the smallest difference within 2 pixels (motion the warp does not model: parallax, stars at
                    // infinity), so only something appearing or vanishing counts
                    float best = 1e9f;
                    bool any = false;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int xs = int(std::lround(xi)) + dx, ys = int(std::lround(yi)) + dy;
                            if (xs < 0 || ys < 0 || xs >= w4 || ys >= h4) continue;
                            any = true;
                            best = std::min(best, std::fabs(cur[size_t(y) * w4 + x] - prev[size_t(ys) * w4 + xs]));
                        }
                    diff[size_t(y) * w4 + x] = any ? best : 0.0f;
                }
            // 8x8 blocks
            float worst = 0.0f;
            int wbx = 0, wby = 0;
            // the outer ring of blocks is left out: what the zoom brings in at the border is new by design, and there the
            // warp's error from depth (parallax about the focus plane) is largest
            for (int by = 1; by < h4 / 8 - 1; ++by)
                for (int bx = 1; bx < w4 / 8 - 1; ++bx) {
                    float m = 0.0f;
                    for (int j = 0; j < 8; ++j)
                        for (int i = 0; i < 8; ++i) m += diff[size_t(by * 8 + j) * w4 + bx * 8 + i];
                    m /= 64.0f;
                    if (m > worst) { worst = m; wbx = bx; wby = by; }
                }
            if (worst > threshold) flags.push_back({sc.s, worst, wbx, wby});
        }
        {
            std::unordered_map<uint64_t, Scale::PartTrack> now;
            for (const Scale::PartTrack& t : sc.particleTrack) now[t.id] = t;
            const float mx = 0.08f * sc.width, my = 0.08f * sc.height;
            auto inside = [&](const Scale::PartTrack& t) { return t.x > mx && t.x < sc.width - mx && t.y > my && t.y < sc.height - my; };
            const int dec = std::clamp(int(std::floor(sc.s)) + 40, 0, 79);
            double worst = 0.0;
            static int shown = 0;
            const bool dbg = std::getenv("OLCEK_POPS") != nullptr;
            for (const auto& [id, t] : now) {
                const auto it = prevParts.find(id);
                const double d = it == prevParts.end() ? (inside(t) ? t.light : 0.0) : std::fabs(t.light - it->second.light);
                worst = std::max(worst, d);
                if (dbg && frames > 0 && d > 0.15 && shown < 30) {
                    ++shown;
                    std::printf("    pop s=%.3f id=%llx light %.3f -> %.3f at %.0f,%.0f (prev %s)\n", sc.s, (unsigned long long)id,
                                it == prevParts.end() ? 0.0 : it->second.light, t.light, t.x, t.y, it == prevParts.end() ? "yok" : "var");
                }
            }
            for (const auto& [id, t] : prevParts)
                if (!now.count(id) && inside(t)) {
                    worst = std::max(worst, double(t.light));
                    if (dbg && t.light > 0.15 && shown < 30) {
                        ++shown;
                        std::printf("    gone s=%.3f id=%llx light %.3f at %.0f,%.0f\n", sc.s, (unsigned long long)id, t.light, t.x, t.y);
                    }
                }
            if (frames > 0 && worst > popWorst[dec]) {
                popWorst[dec] = worst;
                popWorstS[dec] = sc.s;
            }
            prevParts.swap(now);
        }
        prev.swap(cur);
        prevW = sc.cam.W;
        prevF = F;
        prevRef = sc.cam.ref;
        ++frames;
        if (frames % 1000 == 0) std::printf("  %d / %zu kare (s = %.2f)\n", frames, path.size(), sc.s);
    }
    std::printf("sure: %d kare, %.0f sn\n", frames, nowS() - t0);
    std::printf("  onluk  kare ort / en cok (ms)  ana islemci (ms)\n");
    double worstMs = 0.0;
    for (int d = 0; d < 80; ++d) {
        if (!nByDecade[d]) continue;
        std::printf("  %4d   %6.2f / %6.2f          %5.2f\n", d - 40, msByDecade[d] / nByDecade[d], maxMsByDecade[d], hostByDecade[d] / nByDecade[d]);
        worstMs = std::max(worstMs, maxMsByDecade[d]);
    }
    std::printf("  ev kadrajda: en kotu %.2f (1 = kenar; s = %.2f)\n", worstHome, worstHomeS);
    std::printf("  sicrama (esik %.3f): %zu kare\n", threshold, flags.size());
    {   // where they are: per decade, how many and the worst (the list below is only the forty largest)
        int nFlag[80] = {};
        float wFlag[80] = {};
        for (const Flag& f : flags) {
            const int d = std::clamp(int(std::floor(f.s)) + 40, 0, 79);
            ++nFlag[d];
            wFlag[d] = std::max(wFlag[d], float(f.v));
        }
        std::printf("   onluk: sayi / en buyuk\n");
        for (int d = 0; d < 80; ++d)
            if (nFlag[d]) std::printf("    %4d: %4d / %.3f\n", d - 40, nFlag[d], wFlag[d]);
    }
    {   // the particles: the largest change of one's light in a step (appearing or vanishing inside the screen counts whole)
        std::printf("  parcaciklar (bir adimda isigin en buyuk degisimi; 0,15 ustu sicrama):\n");
        for (int d = 0; d < 80; ++d)
            if (popWorst[d] > 0.0) std::printf("    %4d: %.3f (s = %.3f)%s\n", d - 40, popWorst[d], popWorstS[d], popWorst[d] > 0.15 ? "  SICRAMA" : "");
    }
    std::sort(flags.begin(), flags.end(), [](const Flag& a, const Flag& b) { return a.v > b.v; });
    for (size_t i = 0; i < std::min<size_t>(flags.size(), 40); ++i)
        std::printf("    s = %7.3f  %.3f  blok (%d, %d)\n", flags[i].s, flags[i].v, flags[i].bx, flags[i].by);
    std::printf("sure: en uzun kare %.2f ms (butce 16): %s; sicrama %s\n", worstMs, worstMs <= 16.0 ? "GECTI" : "KALDI", flags.empty() ? "YOK" : "VAR");
    return 0;
}

// --test olcek-zaman: out, a wait, back in at a speed; the home must be at today's noon on arrival
int testTime(OlcekApp& app) {
    Scale& sc = app.sc;
    const double outs[] = {7.4, 8.0, 8.8, 9.5, 12.5, 14.0, 21.0, 27.0};
    const double waits[] = {0.0, 0.37, 1.3, 5.1, 31.0};
    const double speeds[] = {0.5, 2.0, 8.0, 40.0};
    const double dt = 1.0 / 60.0;
    double worstSpin = 0.0, worstYear = 0.0, worstBeach = 0.0, worstRatio = 0.0;
    double ratioBySpeed[4] = {}, extraBySpeed[4] = {};
    int runs = 0;
    std::printf("zaman: R(0-) = %.6f, R(0+) = %.6f (insan olceginde 1 sn = 1 sn); R(-34,79) = %.3e, Planck zamani %.3e\n",
                Scale::rate(-1e-12), Scale::rate(1e-12), Scale::rate(sc.sMin), 5.391247e-44);
    for (double out : outs)
        for (double wait : waits)
            for (int vi = 0; vi < 4; ++vi) {
                const double v = speeds[vi];
                double extra = 0.0;   // radians turned beyond the Earth's own turning, on the way in
                sc.init();
                sc.s = 0.0;
                double sPrev = 0.0;
                // out at 2 decades a second
                while (sc.s < out) {
                    sPrev = sc.s;
                    sc.s = std::min(out, sc.s + 2.0 * dt);
                    sc.advance(dt, sPrev);
                }
                for (double t = 0.0; t < wait; t += dt) sc.advance(dt, sc.s);
                // in at v decades a second; how fast the Earth turned against its own pace in the zone where it is drawn
                // back (the turn of the frame minus what the clock alone gave)
                while (sc.s > 0.0) {
                    sPrev = sc.s;
                    const double spin0 = sc.clk.spin;
                    sc.s = std::max(0.0, sc.s - v * dt);
                    sc.advance(dt, sPrev);
                    if (sc.s > 7.0 && sc.s < 8.6) {
                        double d = sc.clk.spin - spin0;
                        d = std::remainder(d, 2.0 * 3.14159265358979);
                        const double own = 7.292115146706979e-5 * Scale::rate(sc.s) * dt;
                        worstRatio = std::max(worstRatio, std::fabs(d) / std::max(own, 1e-30));
                        ratioBySpeed[vi] = std::max(ratioBySpeed[vi], std::fabs(d) / std::max(own, 1e-30));
                        extra += std::fabs(d - own * smoothstepD(7.0, 8.6, sc.s));
                    }
                    if (sc.s <= 7.0) worstSpin = std::max(worstSpin, std::fabs(sc.clk.spin));
                    if (sc.s <= 11.9) worstYear = std::max(worstYear, std::fabs(sc.clk.year));
                }
                sc.place();
                worstBeach = std::max(worstBeach, length(sc.tree.rel(kBeachToday, kBeach)));
                extraBySpeed[vi] = std::max(extraBySpeed[vi], extra);
                ++runs;
            }
    std::printf("zaman: %d yolculuk (disari %zu yere, %zu bekleme, %zu donus hizi)\n", runs, sizeof(outs) / sizeof(outs[0]),
                sizeof(waits) / sizeof(waits[0]), sizeof(speeds) / sizeof(speeds[0]));
    std::printf("  10^7 altinda Dunya'nin donusu bugunden en cok %.3e rad, 10^11,9 altinda yorungesi %.3e sn\n", worstSpin, worstYear);
    std::printf("  sahile varista sahildeki nokta bugunku yerinden %.3e m\n", worstBeach);
    std::printf("  geri cekilirken Dunya kendi hizinin en cok %.1f kati dondu (gecis bolgesi 10^7-10^8,6)\n", worstRatio);
    for (int vi = 0; vi < 4; ++vi)
        std::printf("    saniyede %4.1f onlukla iniste: en cok %.1f kat, kendi donusunden fazla en cok %.2f rad\n", speeds[vi], ratioBySpeed[vi],
                    extraBySpeed[vi]);
    const bool ok = worstSpin < 1e-9 && worstYear < 1e-3 && worstBeach < 1e-3;
    std::printf("zaman: %s\n", ok ? "GECTI" : "KALDI");
    return ok ? 0 : 1;
}

}   // namespace

int runOlcekCli(int argc, char** argv) {
    OlcekApp app;
    std::vector<double> ss, phis, sides, look;
    std::string out = "captures/olcek", test;
    double t = 0.0, step = 0.01, frac = 0.5, fov = 75.0;
    bool decades = false, hizFrames = false, outSet = false;
    int w = 0, h = 0, leg = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--s") ss = doubles(next());
        else if (a == "--t") t = std::stod(next());
        else if (a == "--out") { out = next(); outSet = true; }
        else if (a == "--w") w = std::stoi(next());
        else if (a == "--h") h = std::stoi(next());
        else if (a == "--set") app.overrides.push_back(next());
        else if (a == "--root") app.root = next();
        else if (a == "--test") test = next();
        else if (a == "--step") step = std::stod(next());
        else if (a == "--decades") decades = true;
        else if (a == "--hiz") hizFrames = true;
        else if (a == "--phi") phis = doubles(next());
        else if (a == "--ayak") leg = std::stoi(next());
        else if (a == "--yer") frac = std::stod(next());
        else if (a == "--gorus") fov = std::stod(next());
        else if (a == "--bakis") look = doubles(next());
        else if (a == "--yan") sides = doubles(next());
    }
    if (w > 0) app.overrides.push_back("width=" + std::to_string(w));
    if (h > 0) app.overrides.push_back("height=" + std::to_string(h));
    app.loadConfig();
    app.init();
    if (test != "olcek-hassasiyet" && test != "olcek-zaman") {
        app.loadMaps();
        std::printf("haritalar:\n%s", app.mapsLog.c_str());
    }
    if (test == "olcek-hiz" || hizFrames) {
        app.prepareHiz();
        std::printf("hiz:\n%s", app.hizLog.c_str());
    }
    int rc = 0;
    if (test == "olcek-hassasiyet") rc = testPrecision(app);
    else if (test == "olcek-sure") rc = testContinuity(app, step);
    else if (test == "olcek-zaman") rc = testTime(app);
    else if (test == "olcek-hiz") rc = runHizTests(app);
    else if (hizFrames) {
        if (phis.empty()) phis = {0.0};
        rc = runHizFrames(app, phis, leg, frac, fov, look.size() > 0 ? look[0] : 0.0, look.size() > 1 ? look[1] : 0.0, sides,
                          outSet ? out : std::string("captures/olcek_hiz"));
    } else {
        if (decades) {
            ss.clear();
            ss.push_back(app.sc.sMin);
            for (int d = int(std::ceil(app.sc.sMin)); d <= int(std::floor(app.sc.sMax)); ++d) ss.push_back(d);
            ss.push_back(app.sc.sMax);
        }
        if (ss.empty()) ss = {0.0};
        std::filesystem::create_directories(std::filesystem::path(app.root) / out);
        for (double s : ss) {
            app.sc.init();
            app.sc.s = std::clamp(s, app.sc.sMin, app.sc.sMax);
            for (double tt = 0.0; tt < t; tt += 1.0 / 60.0) app.sc.advance(1.0 / 60.0, app.sc.s);
            app.render();
            if (std::getenv("OLCEK_DEBUG")) {
                const OlcekView& v = app.view;
                std::printf("  ref %d, D %.6g, balls %d, blobs %d, ground %d (o %.4g %.4g %.4g, metres %.6g, beach %.3f)\n", app.sc.cam.ref,
                            app.sc.cam.D, v.nBalls, v.nBlobs, v.ground.on, v.ground.o.x, v.ground.o.y, v.ground.o.z, v.ground.metres, v.ground.beach);
                for (int i = 0; i < v.nBalls; ++i)
                    std::printf("    ball %d: c %.6g %.6g %.6g r %.6g h %.6g pattern %d weight %.3f\n", i, v.balls[i].c.x, v.balls[i].c.y, v.balls[i].c.z,
                                v.balls[i].r, v.balls[i].h, v.balls[i].pattern, v.balls[i].weight);
                for (int i = 0; i < std::min(v.nBlobs, 6); ++i)
                    std::printf("    blob %d: c %.5g %.5g %.5g sigma %.5g emit %.4g\n", i, v.blobs[i].c.x, v.blobs[i].c.y, v.blobs[i].c.z, v.blobs[i].sigma,
                                v.blobs[i].emit.x);
                std::printf("    splats %zu, particles %zu, ground fade %.3f atoms %.3f surface %.3f\n", app.splats.size(), app.sc.particles.size(),
                            v.ground.fade, v.ground.atoms, v.ground.surface);
                for (int i = 0; i < kMaxGalaxies; ++i) {   // the galaxies' centres on the screen (pixels) and size
                    const GGalaxy& g = v.galaxy[i];
                    if (!g.on) continue;
                    const float sx = (g.c.x / g.c.z / v.tanHalfX * 0.5f + 0.5f) * v.width;
                    const float sy = (0.5f - g.c.y / g.c.z / v.tanHalfY * 0.5f) * v.height;
                    std::printf("    galaxy %d: screen %.0f %.0f, depth %.3g, 20 kpc = %.1f px, |n.z| %.2f\n", i, sx, sy, g.c.z,
                                20.0f * g.kpc * g.size / g.c.z / v.pixAngle, std::fabs(g.n.z));
                }
                if (v.dens.on) {   // the density over a 33 x 19 grid on the screen, and at the focus (log10, per a0^3)
                    float lo = 1e30f, hi = -1e30f;
                    for (int j = 0; j <= 18; ++j)
                        for (int i = 0; i <= 32; ++i) {
                            const float x = std::log10(olDensity(v.dens, make_float3(v.tanHalfX * (i / 16.0f - 1.0f), v.tanHalfY * (j / 9.0f - 1.0f), 1.0f)));
                            lo = std::min(lo, x);
                            hi = std::max(hi, x);
                        }
                    std::printf("    density: screen %.2f .. %.2f, focus %.2f; fill %.2f .. %.2f, %d bodies\n", lo, hi,
                                std::log10(olDensity(v.dens, make_float3(0, 0, 1))), v.dens.lo, v.dens.hi, v.dens.n);
                }
            }
            char name[64];
            std::snprintf(name, sizeof(name), "s%+07.3f", app.sc.s);
            app.save(app.root + "/" + out + "/" + name);
            std::printf("s = %+7.2f  %-28s  %-24s  kare %5.2f ms (ana %5.2f), %zu nokta\n", app.sc.s, app.sc.lookingAt().c_str(),
                        app.sc.widthText().c_str(), app.frameMs, app.hostMs, app.splats.size());
        }
    }
    app.release();
    return rc;
}
