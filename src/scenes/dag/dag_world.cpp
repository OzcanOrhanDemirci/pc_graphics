#define _USE_MATH_DEFINES
#include "scenes/dag/dag_world.h"

#include <windows.h>
#undef far
#undef near

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

// the height of the model at a point of the plane, smooth (a B-spline over its cells), and the share of water there
struct DagDemSample {
    float h, water;
};
void dagBuildNear(DagWorld& w, const Config& cfg, const std::function<DagDemSample(double, double, bool)>& demAt);

namespace {

const double kR = 6371000.0;                 // the sphere the plane is drawn on (tools/dag_veri.py the same)
const double kDagLat0 = 40.835555555555555, kDagLon0 = 41.161388888888887;   // the summit: the highest cell of the model (LAT0, LON0 there)
const int kDemW = 14 * 3600, kDemH = 10 * 3600;
const double kDemLatTop = 46.0, kDemLonLeft = 34.0;
const int kClipN = 8192;
const float kClipCell[3] = {25.0f, 65.0f, 130.0f};
const float kSlopeMax = 6.0f;
const float kHoriLo = -20.0f, kHoriHi = 70.0f;   // degrees, the horizon's code range

double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void parallelFor(int n, const std::function<void(int)>& fn, int chunk = 1) {
    std::atomic<int> next{0};
    const int threads = std::max(1, int(std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (;;) {
                const int i0 = next.fetch_add(chunk);
                if (i0 >= n) return;
                for (int i = i0; i < std::min(i0 + chunk, n); ++i) fn(i);
            }
        });
    for (auto& t : pool) t.join();
}

struct Dem {
    HANDLE file = INVALID_HANDLE_VALUE, map = nullptr;
    const uint16_t* v = nullptr;
    bool open(const std::string& path) {
        file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        map = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!map) return false;
        v = static_cast<const uint16_t*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0));
        return v != nullptr;
    }
    void close() {
        if (v) UnmapViewOfFile(v);
        if (map) CloseHandle(map);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        v = nullptr;
        map = nullptr;
        file = INVALID_HANDLE_VALUE;
    }
    float at(int r, int c) const {
        r = std::clamp(r, 0, kDemH - 1);
        c = std::clamp(c, 0, kDemW - 1);
        return float(v[size_t(r) * kDemW + c]) / 8.0f - 500.0f;
    }
    float bilinear(double lat, double lon) const {
        const double fr = (kDemLatTop - lat) * 3600.0, fc = (lon - kDemLonLeft) * 3600.0;
        const int r = int(std::floor(fr)), c = int(std::floor(fc));
        const float ar = float(fr - r), ac = float(fc - c);
        return (1 - ar) * ((1 - ac) * at(r, c) + ac * at(r, c + 1)) + ar * ((1 - ac) * at(r + 1, c) + ac * at(r + 1, c + 1));
    }
};

struct Geo {
    double la0, sin0, cos0, lo0;
    explicit Geo(double lat0, double lon0) {
        la0 = lat0 * M_PI / 180.0;
        sin0 = std::sin(la0);
        cos0 = std::cos(la0);
        lo0 = lon0 * M_PI / 180.0;
    }
    // the plane (x east, z south, metres) to lat, lon (degrees): tools/dag_veri.py's ae_inverse
    void inverse(double mx, double mz, double& lat, double& lon) const {
        const double s = std::sqrt(mx * mx + mz * mz) / kR;
        if (s < 1e-12) {
            lat = la0 * 180.0 / M_PI;
            lon = lo0 * 180.0 / M_PI;
            return;
        }
        const double az = std::atan2(mx, -mz);
        const double ss = std::sin(s), cs = std::cos(s);
        const double sl = sin0 * cs + cos0 * ss * std::cos(az);
        lat = std::asin(std::clamp(sl, -1.0, 1.0)) * 180.0 / M_PI;
        lon = (lo0 + std::atan2(std::sin(az) * ss * cos0, cs - sin0 * sl)) * 180.0 / M_PI;
    }
    // lat, lon (degrees) to the distance on the sphere and the azimuth from north (radians)
    void forward(double lat, double lon, double& s, double& az) const {
        const double la = lat * M_PI / 180.0, dl = lon * M_PI / 180.0 - lo0;
        const double sl = std::sin(la), cl = std::cos(la), sd = std::sin(dl), cd = std::cos(dl);
        const double y = sd * cl, x = cos0 * sl - sin0 * cl * cd;
        az = std::atan2(y, x);
        s = kR * std::atan2(std::sqrt(x * x + y * y), sin0 * sl + cos0 * cl * cd);
    }
};

template <class T> bool writeVec(std::ofstream& f, const std::vector<T>& v) {
    const uint64_t n = v.size();
    f.write(reinterpret_cast<const char*>(&n), sizeof(n));
    if (n) f.write(reinterpret_cast<const char*>(v.data()), std::streamsize(n * sizeof(T)));
    return bool(f);
}
template <class T> bool readVec(std::ifstream& f, std::vector<T>& v) {
    uint64_t n = 0;
    f.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (!f) return false;
    v.resize(size_t(n));
    if (n) f.read(reinterpret_cast<char*>(v.data()), std::streamsize(n * sizeof(T)));
    return bool(f);
}

// lines through a square grid along a direction: every cell on exactly one line, the cells in order along it. The direction is
// (dx, dz) (one step goes this way in the plane); visit(line state, cell index, distance along the line)
template <class Begin, class Visit> void sweepLines(int n, float cell, double dx, double dz, const Begin& begin, const Visit& visit) {
    const bool xMain = std::fabs(dx) >= std::fabs(dz);
    const double main = xMain ? dx : dz, minor = xMain ? dz : dx;
    const int stepMain = main >= 0 ? 1 : -1;
    const double m = std::fabs(main) > 1e-12 ? minor / std::fabs(main) : 0.0;   // minor cells per main cell
    const double ds = double(cell) / std::fabs(main);
    // j = v + round(i * m), i counted from the start of the line
    const int shift = int(std::ceil(std::fabs(m) * n)) + 1;
    parallelFor(n + 2 * shift, [&](int lineIndex) {
        auto st = begin();
        const int v = lineIndex - shift;
        for (int i = 0; i < n; ++i) {
            const int jm = v + int(std::lround(i * m));
            if (jm < 0 || jm >= n) continue;
            const int im = stepMain > 0 ? i : n - 1 - i;
            const int x = xMain ? im : jm, z = xMain ? jm : im;
            visit(st, size_t(z) * n + x, i * ds);
        }
    }, 16);
}

// the upper hull towards where the line came from: each visit gives the steepest angle to anything before it
struct Hull {
    std::vector<double> s, y;
    Hull() {
        s.reserve(4096);
        y.reserve(4096);
    }
    // the tangent's slope from (sp, yp) to the hull; lowest when empty
    double step(double sp, double yp) {
        while (s.size() >= 2) {
            const size_t k = s.size();
            const double a = (y[k - 2] - yp) / (sp - s[k - 2]), b = (y[k - 1] - yp) / (sp - s[k - 1]);
            if (a >= b) {
                s.pop_back();
                y.pop_back();
            } else {
                break;
            }
        }
        const double m = s.empty() ? -1e9 : (y.back() - yp) / (sp - s.back());
        s.push_back(sp);
        y.push_back(yp);
        return m;
    }
};

}   // namespace

float3 DagWorld::worldPoint(double mx, double mz, double h) const {
    const double s = std::sqrt(mx * mx + mz * mz);
    const double th = s / double(Re);
    const double a = double(Re) + h;
    const double sn = std::sin(th), half = std::sin(th * 0.5);
    const double k = s > 1e-9 ? a * sn / s : 1.0;
    const double y = h - a * 2.0 * half * half - double(h0);   // a cos(th) - Re - h0, kept precise
    return make_float3(float(mx * k), float(y), float(mz * k));
}

static bool loadClips(const std::string& dir, DagClipCPU* clips) {
    for (int k = 0; k < 3; ++k) {
        std::ifstream f(dir + "/i" + std::to_string(k) + ".bin", std::ios::binary);
        if (!f) return false;
        clips[k].n = kClipN;
        clips[k].cell = kClipCell[k];
        if (!readVec(f, clips[k].info)) return false;
    }
    return true;
}

bool DagWorld::prepare(const std::string& root, const Config& cfg) {
    const double t0 = nowMs();
    root_ = root;
    lat0 = cfg.f("summit.lat", 40.8342f);
    lon0 = cfg.f("summit.lon", 41.1586f);
    // (the config reads floats: the summit's exact place is written here, the same as tools/dag_veri.py's LAT0, LON0)

    lat0 = kDagLat0;
    lon0 = kDagLon0;
    Re = float(kR / (1.0 - cfg.f("air.bending", 0.13f)));
    sunAzimuth = cfg.f("sun.azimuth", 299.0f);
    sunElevation = cfg.f("sun.elevation", 2.0f);
    nearRadius = cfg.f("near.radius", 1500.0f);
    const std::string assets = root + "/assets/dag", cache = root + "/cache/dag";
    std::error_code ec;
    fs::create_directories(cache, ec);
    auto say = [&](float f, const std::string& s) {
        if (progress) progress(f, s);
        std::printf("dag: %s\n", s.c_str());
        std::fflush(stdout);
    };
    Dem dem;
    if (!dem.open(assets + "/dem_1s.u16")) {
        error = "assets/dag/dem_1s.u16 yok: python tools/dag_veri.py dem, sonra mosaic";
        return false;
    }
    const Geo geo(lat0, lon0);
    h0 = dem.bilinear(lat0, lon0) + cfg.f("terrace.lift", 0.0f);
    std::printf("dag: zirve %.5f %.5f, %.1f m\n", lat0, lon0, h0);

    // the sun's direction along the plane: azimuth from north, clockwise; x east, z south
    const double azr = sunAzimuth * M_PI / 180.0;
    const double sunX = std::sin(azr), sunZ = -std::cos(azr);

    // ---------------------------------------------------------------------------------------------------------------
    // the clip stacks of the ground's slope, sky share and horizon towards the sun; the satellite's own light
    std::vector<float> satLight[3];
    const std::string sunTag = std::to_string(int(std::lround(sunAzimuth * 10)));
    const std::string clipTag = cache + "/clips_" + sunTag + ".ok";
    bool haveClips = fs::exists(clipTag) && loadClips(cache, clips);
    {
        std::ifstream sf(cache + "/sat.bin", std::ios::binary);
        for (int k = 0; k < 3 && sf; ++k) readVec(sf, satLight[k]);
        if (!sf) haveClips = false;
    }
    if (!haveClips) {
        const double t1 = nowMs();
        std::vector<float> horizonCoarse;
        int coarseN = 0;
        float coarseCell = 0.0f;
        for (int k = 2; k >= 0; --k) {
            say(0.02f + 0.03f * (2 - k), "Yeryuzunun egimi ve ufuklari: katman " + std::to_string(k));
            const int n = kClipN;
            const float cell = kClipCell[k];
            const float half = cell * n * 0.5f;
            std::vector<float> h(size_t(n) * n);
            const int ss = k == 0 ? 1 : k == 1 ? 3 : 5;   // cells a side read per cell: the mean ground of the cell
            parallelFor(n, [&](int r) {
                for (int c = 0; c < n; ++c) {
                    double sum = 0.0;
                    for (int a = 0; a < ss; ++a)
                        for (int b = 0; b < ss; ++b) {
                            const double mx = (c + (b + 0.5) / ss) * cell - half, mz = (r + (a + 0.5) / ss) * cell - half;
                            double la, lo;
                            geo.inverse(mx, mz, la, lo);
                            sum += dem.bilinear(la, lo);
                        }
                    h[size_t(r) * n + c] = float(sum / (ss * ss));
                }
            }, 8);
            // the horizon towards the sun, with the planet's curve: the line runs away from the sun
            std::vector<float> H(size_t(n) * n, -1.0f);
            const double Rd = double(Re);
            sweepLines(n, cell, -sunX, -sunZ, [] { return Hull(); }, [&](Hull& hull, size_t i, double s) {
                const double yp = double(h[i]) - s * s / (2.0 * Rd);
                const double m = hull.step(s, yp);
                H[i] = m < -1e8 ? -0.35f : float(std::atan(m - s / Rd));
            });
            // what lies beyond this level's square: the coarser level's horizon
            if (!horizonCoarse.empty()) {
                parallelFor(n, [&](int r) {
                    for (int c = 0; c < n; ++c) {
                        const double fx = ((c + 0.5) * cell - half + coarseCell * coarseN * 0.5) / coarseCell - 0.5,
                                     fz = ((r + 0.5) * cell - half + coarseCell * coarseN * 0.5) / coarseCell - 0.5;
                        const int x0 = int(fx), z0 = int(fz);
                        const float ax = float(fx - x0), az = float(fz - z0);
                        const float* C = horizonCoarse.data();
                        const float v = (1 - az) * ((1 - ax) * C[size_t(z0) * coarseN + x0] + ax * C[size_t(z0) * coarseN + x0 + 1]) +
                                        az * ((1 - ax) * C[size_t(z0 + 1) * coarseN + x0] + ax * C[size_t(z0 + 1) * coarseN + x0 + 1]);
                        float& H0 = H[size_t(r) * n + c];
                        H0 = std::max(H0, v);
                    }
                }, 16);
            }
            // the sky's share: the mean of cos^2 of the horizon in eight directions (four lines, both ways)
            std::vector<float> ao(size_t(n) * n, 0.0f);
            for (int d = 0; d < 8; ++d) {
                const double a = d * M_PI / 4.0 + 0.2;
                sweepLines(n, cell, std::cos(a), std::sin(a), [] { return Hull(); }, [&](Hull& hull, size_t i, double s) {
                    const double m = hull.step(s, double(h[i]));
                    const double up = std::max(m, 0.0);   // m: rise over run: cos^2 = 1 / (1 + m^2)
                    ao[i] += float(0.125 / (1.0 + up * up));
                });
            }
            // the satellite's own light (late morning, from the south-east, high): how bright each cell was in the picture
            const double satAz = 145.0 * M_PI / 180.0, satEl = 58.0 * M_PI / 180.0;
            const double lx = std::sin(satAz) * std::cos(satEl), lz = -std::cos(satAz) * std::cos(satEl), ly = std::sin(satEl);
            std::vector<float> Hs(size_t(n) * n, 0.0f);
            sweepLines(n, cell, -lx, -lz, [] { return Hull(); }, [&](Hull& hull, size_t i, double s) {
                const double m = hull.step(s, double(h[i]));
                Hs[i] = float(m);
            });
            const double tanSat = std::tan(satEl);
            std::vector<float>& sat = satLight[k];
            sat.assign(size_t(n) * n, 1.0f);
            clips[k].n = n;
            clips[k].cell = cell;
            clips[k].info.assign(size_t(n) * n * 4, 0);
            parallelFor(n, [&](int r) {
                for (int c = 0; c < n; ++c) {
                    const size_t i = size_t(r) * n + c;
                    const size_t xa = size_t(r) * n + std::max(c - 1, 0), xb = size_t(r) * n + std::min(c + 1, n - 1);
                    const size_t za = size_t(std::max(r - 1, 0)) * n + c, zb = size_t(std::min(r + 1, n - 1)) * n + c;
                    const float sx = (h[xb] - h[xa]) / (2.0f * cell), sz = (h[zb] - h[za]) / (2.0f * cell);
                    auto code = [](float v) { return uint16_t(std::clamp(v, 0.0f, 1.0f) * 65535.0f + 0.5f); };
                    uint16_t* o = &clips[k].info[i * 4];
                    o[0] = code(sx / (2.0f * kSlopeMax) + 0.5f);
                    o[1] = code(sz / (2.0f * kSlopeMax) + 0.5f);
                    o[2] = code(ao[i]);
                    o[3] = code((H[i] * 180.0f / float(M_PI) - kHoriLo) / (kHoriHi - kHoriLo));
                    const float nl = 1.0f / std::sqrt(1.0f + sx * sx + sz * sz);
                    const float cosl = float((-sx * lx - sz * lz + ly)) * nl;
                    const float vis = Hs[i] > tanSat ? 0.0f : 1.0f;
                    const float a = 0.2f;
                    sat[i] = a + (1.0f - a) * vis * std::max(cosl, 0.0f) / float(ly);
                }
            }, 16);
            horizonCoarse = H;
            coarseN = n;
            coarseCell = cell;
            std::ofstream f(cache + "/i" + std::to_string(k) + ".bin", std::ios::binary);
            writeVec(f, clips[k].info);
        }
        std::ofstream sf(cache + "/sat.bin", std::ios::binary);
        for (int k = 0; k < 3; ++k) writeVec(sf, satLight[k]);
        std::ofstream(clipTag) << "ok";
        std::printf("dag: katmanlar %.1f s\n", (nowMs() - t1) * 1e-3);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // the ground's colour with the satellite's light taken out
    {
        const int an[3] = {8192, 16384, 16384};
        const float ac[3] = {8.0f, 16.0f, 64.0f};
        const std::string aname[3] = {"a0", "a1", "a2"};
        bool have = fs::exists(cache + "/albedo.ok");
        if (!have) {
            std::vector<float> toLin(256);
            for (int k = 0; k < 256; ++k) {
                const float x = k / 255.0f;
                toLin[k] = x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
            }
            std::vector<uint8_t> toSrgb(4096);
            for (int k = 0; k < 4096; ++k) {
                const float x = k / 4095.0f;
                const float e = x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
                toSrgb[k] = uint8_t(std::clamp(e, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            for (int k = 0; k < 3; ++k) {
                say(0.12f + 0.03f * k, "Uydunun gunesi cikariliyor: katman " + std::to_string(k));
                const int n = an[k];
                const float cell = ac[k], half = cell * n * 0.5f;
                std::ifstream in(assets + "/" + aname[k] + ".rgb8", std::ios::binary);
                if (!in) {
                    error = "assets/dag/" + aname[k] + ".rgb8 yok: python tools/dag_veri.py imgfetch, imgbuild";
                    dem.close();
                    return false;
                }
                std::vector<uint8_t> src(size_t(n) * n * 3);
                in.read(reinterpret_cast<char*>(src.data()), std::streamsize(src.size()));
                std::vector<uint8_t> out(size_t(n) * n * 4);
                parallelFor(n, [&](int r) {
                    for (int c = 0; c < n; ++c) {
                        const double mx = (c + 0.5) * cell - half, mz = (r + 0.5) * cell - half;
                        // the finest clip that holds the cell
                        float L = 1.0f;
                        for (int q = 0; q < 3; ++q) {
                            const float qc = kClipCell[q], qh = qc * kClipN * 0.5f;
                            if (std::fabs(mx) >= qh - qc || std::fabs(mz) >= qh - qc) continue;
                            const double fx = (mx + qh) / qc - 0.5, fz = (mz + qh) / qc - 0.5;
                            const int x0 = int(fx), z0 = int(fz);
                            const float ax = float(fx - x0), az = float(fz - z0);
                            const float* S = satLight[q].data();
                            L = (1 - az) * ((1 - ax) * S[size_t(z0) * kClipN + x0] + ax * S[size_t(z0) * kClipN + x0 + 1]) +
                                az * ((1 - ax) * S[size_t(z0 + 1) * kClipN + x0] + ax * S[size_t(z0 + 1) * kClipN + x0 + 1]);
                            break;
                        }
                        L = std::clamp(L, 0.25f, 1.6f);
                        const size_t i = size_t(r) * n + c;
                        for (int q = 0; q < 3; ++q) {
                            const float lin = toLin[src[i * 3 + q]] / L;
                            out[i * 4 + q] = toSrgb[std::clamp(int(lin * 4095.0f + 0.5f), 0, 4095)];
                        }
                        // the alpha: land's share. The elevation model's water is exactly flat: the sea at 0 and the lakes
                        double la, lo;
                        geo.inverse(mx, mz, la, lo);
                        const int dr = int((kDemLatTop - la) * 3600.0 + 0.5), dc = int((lo - kDemLonLeft) * 3600.0 + 0.5);
                        uint8_t land = 255;
                        if (dr >= 2 && dc >= 2 && dr < kDemH - 2 && dc < kDemW - 2) {
                            const uint16_t v0 = dem.v[size_t(dr) * kDemW + dc];
                            if (v0 == 4000) {
                                land = 0;
                            } else if (v0 == dem.v[size_t(dr) * kDemW + dc + 1] && v0 == dem.v[size_t(dr + 1) * kDemW + dc]) {
                                bool flat = true;
                                for (int a = -2; a <= 2 && flat; ++a)
                                    for (int b = -2; b <= 2; ++b)
                                        if (std::abs(int(dem.v[size_t(dr + a) * kDemW + dc + b]) - int(v0)) > 1) {
                                            flat = false;
                                            break;
                                        }
                                if (flat) land = 0;
                            }
                        }
                        out[i * 4 + 3] = land;
                    }
                }, 16);
                std::ofstream f(cache + "/" + aname[k] + ".bin", std::ios::binary);
                writeVec(f, out);
            }
            std::ofstream(cache + "/albedo.ok") << "ok";
        }
        for (int k = 0; k < 3; ++k) {
            say(0.2f + 0.03f * k, "Yeryuzunun rengi okunuyor: katman " + std::to_string(k));
            std::ifstream f(cache + "/" + aname[k] + ".bin", std::ios::binary);
            albedo[k].n = an[k];
            albedo[k].cell = ac[k];
            if (!readVec(f, albedo[k].rgba)) {
                error = "cache/dag/" + aname[k] + ".bin okunamadi";
                dem.close();
                return false;
            }
        }
    }
    for (auto& s : satLight) std::vector<float>().swap(s);

    // ---------------------------------------------------------------------------------------------------------------
    // the far mesh: every cell of the elevation model the terrace can see
    const int step1 = cfg.i("far.step1", 1), step2 = cfg.i("far.step2", 2), step3 = cfg.i("far.step3", 3);
    const double band1 = cfg.f("far.band1", 120000.0f), band2 = cfg.f("far.band2", 300000.0f);
    const std::string farPath = cache + "/far_" + std::to_string(step1) + "_" + std::to_string(step2) + "_" + std::to_string(step3) + "_" +
                                std::to_string(int(band1 / 1000)) + "_" + std::to_string(int(band2 / 1000)) + "_" + std::to_string(int(cfg.f("mid.radius", 30000.0f))) + ".bin";
    bool haveFar = false;
    {
        std::ifstream f(farPath, std::ios::binary);
        if (f) {
            say(0.3f, "Gorunen yeryuzu okunuyor");
            uint64_t count = 0;
            f.read(reinterpret_cast<char*>(&count), sizeof(count));
            far.resize(size_t(count));
            f.read(reinterpret_cast<char*>(&farthestM), sizeof(farthestM));
            haveFar = bool(f);
            for (auto& t : far) {
                f.read(reinterpret_cast<char*>(&t.lat), sizeof(int));
                f.read(reinterpret_cast<char*>(&t.lon), sizeof(int));
                if (!readVec(f, t.tris)) haveFar = false;
            }
            readVec(f, bands);
        }
    }
    if (!haveFar) {
        const double t1 = nowMs();
        say(0.3f, "Tepeden gorunen her hucre bulunuyor");
        const double Rd = double(Re);
        std::vector<uint8_t> vis(size_t(kDemW) * kDemH, 0);
        struct Eye {
            double x, z, up;
        };
        const float eyeRing = cfg.f("far.eyeRing", 10.0f);
        std::vector<Eye> eyes = {{0, 0, 1.7}, {0, 0, 3.5}};
        for (int k = 0; k < 6; ++k) eyes.push_back({eyeRing * std::cos(k * M_PI / 3), eyeRing * std::sin(k * M_PI / 3), 1.7});
        const double farHole = double(cfg.f("mid.radius", 30000.0f)) - 1500.0;
        const int lines = cfg.i("far.lines", 72000);
        const double tol = cfg.f("far.tolerance", 0.0006f);
        const double maxS = cfg.f("far.max", 560000.0f);
        for (size_t e = 0; e < eyes.size(); ++e) {
            const Eye eye = eyes[e];
            const double eyeH = double(h0) + eye.up;
            // the eye's place: ring eyes offset the line's start in the plane
            parallelFor(lines, [&](int li) {
                const double a = (li + 0.5) / lines * 2.0 * M_PI;
                const double dx = std::sin(a), dz = -std::cos(a);
                double best = -1e9;
                // way points every 2 km in lat, lon: the samples between them by interpolation
                double la0p, lo0p, la1p, lo1p;
                geo.inverse(eye.x + dx * 50.0, eye.z + dz * 50.0, la0p, lo0p);
                double segStart = 50.0, segEnd = 50.0;
                la1p = la0p;
                lo1p = lo0p;
                for (double s = 50.0; s < maxS;) {
                    if (s >= segEnd) {
                        segStart = segEnd;
                        la0p = la1p;
                        lo0p = lo1p;
                        segEnd = segStart + 2000.0;
                        geo.inverse(eye.x + dx * segEnd, eye.z + dz * segEnd, la1p, lo1p);
                    }
                    const double w = (s - segStart) / (segEnd - segStart);
                    const double la = la0p + (la1p - la0p) * w, lo = lo0p + (lo1p - lo0p) * w;
                    const int r = int((kDemLatTop - la) * 3600.0 + 0.5), c = int((lo - kDemLonLeft) * 3600.0 + 0.5);
                    if (r < 0 || r >= kDemH || c < 0 || c >= kDemW) break;
                    const size_t i = size_t(r) * kDemW + c;
                    const double h = double(dem.v[i]) / 8.0 - 500.0;
                    const double slope = (h - eyeH) / s - s / (2.0 * Rd);
                    if (slope >= best - tol) vis[i] = 1;
                    // (around the terrace the rock's breakup can lower what hides the cells behind it)
                    const double hides = s < 40000.0 ? slope - 22.0 / s : slope;
                    if (hides > best) best = hides;
                    s += s < 60000.0 ? 15.0 : s < 200000.0 ? 25.0 : 40.0;
                }
            }, 64);
            say(0.3f + 0.1f * float(e + 1) / eyes.size(), "Gorunen hucreler: goz " + std::to_string(e + 1) + "/" + std::to_string(eyes.size()));
        }
        // the cells to triangles, by whole degree
        say(0.42f, "Ucgenler kuruluyor");
        struct TileJob {
            int lat, lon;
        };
        std::vector<TileJob> jobs;
        for (int la = 36; la < 46; ++la)
            for (int lo = 34; lo < 48; ++lo) jobs.push_back({la, lo});
        std::vector<DagFarTile> tiles(jobs.size());
        std::vector<std::array<uint64_t, 12>> tileBands(jobs.size());
        std::atomic<float> farthest{0.0f};
        parallelFor(int(jobs.size()), [&](int ji) {
            const TileJob job = jobs[ji];
            DagFarTile& T = tiles[ji];
            T.lat = job.lat;
            T.lon = job.lon;
            tileBands[ji].fill(0);
            const int r0 = (46 - (job.lat + 1)) * 3600, c0 = (job.lon - 34) * 3600;
            float localFar = 0.0f;
            auto anyVis = [&](int r, int c, int st) {
                // the cell and one cell of margin around it (a line may just have missed it)
                for (int a = std::max(r - 1, 0); a <= std::min(r + st, kDemH - 1); ++a) {
                    const uint8_t* row = vis.data() + size_t(a) * kDemW;
                    for (int b = std::max(c - 1, 0); b <= std::min(c + st, kDemW - 1); ++b)
                        if (row[b]) return true;
                }
                return false;
            };
            for (int rr = 0; rr < 3600; ++rr) {
                const int r = r0 + rr;
                if (r + 4 >= kDemH) break;
                for (int cc = 0; cc < 3600; ++cc) {
                    const int c = c0 + cc;
                    if (c + 4 >= kDemW) break;
                    // the step depends on the distance: read it once at the coarse corners
                    if (!(rr % step3 == 0 && cc % step3 == 0) && !(rr % step2 == 0 && cc % step2 == 0) && !(rr % step1 == 0 && cc % step1 == 0)) continue;
                    if (!anyVis(r, c, 3)) continue;
                    const double la = kDemLatTop - r / 3600.0, lo = kDemLonLeft + c / 3600.0;
                    double s, az;
                    geo.forward(la, lo, s, az);
                    const int st = s < band1 ? step1 : s < band2 ? step2 : step3;
                    // a cell of this band only on its own grid; the bands overlap by a little so no crack opens between them
                    const bool on1 = s < band1 + 1000.0 && rr % step1 == 0 && cc % step1 == 0;
                    const bool on2 = s >= band1 - 1000.0 && s < band2 + 2000.0 && rr % step2 == 0 && cc % step2 == 0;
                    const bool on3 = s >= band2 - 2000.0 && rr % step3 == 0 && cc % step3 == 0;
                    (void)st;
                    for (int band = 0; band < 3; ++band) {
                        if (!(band == 0 ? on1 : band == 1 ? on2 : on3)) continue;
                        const int sb = band == 0 ? step1 : band == 1 ? step2 : step3;
                        if (band > 0 && band == 1 && step2 == step1) continue;
                        if (band == 2 && step3 == step2) continue;
                        if (!anyVis(r, c, sb)) continue;
                        if (s < farHole) continue;
                        const int ra = r, rb = r + sb, ca = c, cb = c + sb;
                        const uint16_t va = dem.v[size_t(ra) * kDemW + ca], vb = dem.v[size_t(ra) * kDemW + cb], vc = dem.v[size_t(rb) * kDemW + ca],
                                       vd = dem.v[size_t(rb) * kDemW + cb];
                        if (va == 4000 && vb == 4000 && vc == 4000 && vd == 4000) continue;   // open sea: drawn by itself
                        auto vert = [&](int rq, int cq, uint16_t v) {
                            const double laq = kDemLatTop - rq / 3600.0, loq = kDemLonLeft + cq / 3600.0;
                            double sq, azq;
                            geo.forward(laq, loq, sq, azq);
                            return worldPoint(sq * std::sin(azq), -sq * std::cos(azq), double(v) / 8.0 - 500.0);
                        };
                        const float3 pa = vert(ra, ca, va), pb = vert(ra, cb, vb), pc = vert(rb, ca, vc), pd = vert(rb, cb, vd);
                        // the shorter diagonal in height: ridges stay ridges
                        if (std::abs(int(va) - int(vd)) <= std::abs(int(vb) - int(vc))) {
                            T.tris.push_back(pa); T.tris.push_back(pc); T.tris.push_back(pd);
                            T.tris.push_back(pa); T.tris.push_back(pd); T.tris.push_back(pb);
                        } else {
                            T.tris.push_back(pa); T.tris.push_back(pc); T.tris.push_back(pb);
                            T.tris.push_back(pb); T.tris.push_back(pc); T.tris.push_back(pd);
                        }
                        const int bi = std::clamp(int(s / 50000.0), 0, 11);
                        tileBands[ji][bi]++;
                        if (float(s) > localFar) localFar = float(s);
                    }
                }
            }
            float prev = farthest.load();
            while (localFar > prev && !farthest.compare_exchange_weak(prev, localFar)) {
            }
        });
        far.clear();
        bands.assign(12, DagBand());
        for (int b = 0; b < 12; ++b) {
            bands[b].from = b * 50000.0f;
            bands[b].to = (b + 1) * 50000.0f;
        }
        for (size_t k = 0; k < tiles.size(); ++k) {
            for (int b = 0; b < 12; ++b) bands[b].cells += tileBands[k][b];
            if (!tiles[k].tris.empty()) far.push_back(std::move(tiles[k]));
        }
        farthestM = farthest.load();
        std::ofstream f(farPath, std::ios::binary);
        const uint64_t count = far.size();
        f.write(reinterpret_cast<const char*>(&count), sizeof(count));
        f.write(reinterpret_cast<const char*>(&farthestM), sizeof(farthestM));
        for (const auto& t : far) {
            f.write(reinterpret_cast<const char*>(&t.lat), sizeof(int));
            f.write(reinterpret_cast<const char*>(&t.lon), sizeof(int));
            writeVec(f, t.tris);
        }
        writeVec(f, bands);
        std::printf("dag: uzak ag %.1f s\n", (nowMs() - t1) * 1e-3);
    }
    farTriangles = 0;
    for (const auto& t : far) farTriangles += t.tris.size() / 3;

    // the places the binoculars can name
    {
        struct Known {
            const char* tr;
            const char* en;
            double lat, lon;
            int kind;   // 0 peak, 1 town, 2 lake
        };
        static const Known known[] = {
            {"Elbruz", "Elbrus", 43.3499, 42.4453, 0},
            {"Kazbek", "Kazbek", 42.6970, 44.5181, 0},
            {"Şhara", "Shkhara", 42.9972, 43.1083, 0},
            {"Uşba", "Ushba", 43.1250, 42.6600, 0},
            {"Tebulosmta", "Tebulosmta", 42.5636, 45.2967, 0},
            {"Ağrı Dağı", "Mount Ararat", 39.7019, 44.2983, 0},
            {"Küçük Ağrı", "Little Ararat", 39.6517, 44.4067, 0},
            {"Aragats", "Aragats", 40.5253, 44.2025, 0},
            {"Süphan", "Süphan", 38.9250, 42.8350, 0},
            {"Palandöken", "Palandöken", 39.8333, 41.2833, 0},
            {"Rize", "Rize", 41.0201, 40.5234, 1},
            {"Trabzon", "Trabzon", 41.0015, 39.7178, 1},
            {"Giresun", "Giresun", 40.9128, 38.3895, 1},
            {"Hopa", "Hopa", 41.3903, 41.4190, 1},
            {"Batum", "Batumi", 41.6168, 41.6367, 1},
            {"Poti", "Poti", 42.1462, 41.6717, 1},
            {"Artvin", "Artvin", 41.1828, 41.8183, 1},
            {"Erzurum", "Erzurum", 39.9043, 41.2679, 1},
            {"Kars", "Kars", 40.6013, 43.0975, 1},
            {"Çıldır Gölü", "Lake Çıldır", 41.0500, 43.2000, 2},
            {"Van Gölü", "Lake Van", 38.6300, 42.9000, 2},
            {"Sevan Gölü", "Lake Sevan", 40.3800, 45.3000, 2},
        };
        places.clear();
        for (const Known& k : known) {
            double s, az;
            geo.forward(k.lat, k.lon, s, az);
            DagPlace P;
            P.nameTr = k.tr;
            P.nameEn = k.en;
            P.mx = s * std::sin(az);
            P.mz = -s * std::cos(az);
            P.altitude = 0.0f;
            P.reach = k.kind == 0 ? 7000.0f : k.kind == 1 ? 5000.0f : 25000.0f;
            if (k.kind == 0) {
                // the top within a few kilometres, from the model itself
                float best = 0.0f;
                for (int a = -90; a <= 90; ++a)
                    for (int b = -90; b <= 90; ++b) best = std::max(best, dem.at(int((kDemLatTop - k.lat) * 3600.0) + a, int((k.lon - kDemLonLeft) * 3600.0) + b));
                P.altitude = best;
            }
            places.push_back(P);
        }
    }

    // the air's shadow map's ground: the highest ground in each 500 m cell, square around the summit
    {
        const std::string sbPath = cache + "/shadowbase.bin";
        std::ifstream f(sbPath, std::ios::binary);
        shadowCell = 500.0f;
        shadowN = 2240;
        if (!(f && readVec(f, shadowBase))) {
            say(0.5f, "Havanin golgesi icin yuksekler");
            shadowBase.assign(size_t(shadowN) * shadowN, 0.0f);
            const float half = shadowCell * shadowN * 0.5f;
            parallelFor(shadowN, [&](int r) {
                for (int c = 0; c < shadowN; ++c) {
                    float best = 0.0f;
                    for (int a = 0; a < 6; ++a)
                        for (int b = 0; b < 6; ++b) {
                            const double mx = (c + (b + 0.5) / 6) * shadowCell - half, mz = (r + (a + 0.5) / 6) * shadowCell - half;
                            double la, lo;
                            geo.inverse(mx, mz, la, lo);
                            best = std::max(best, dem.bilinear(la, lo));
                        }
                    shadowBase[size_t(r) * shadowN + c] = best;
                }
            }, 8);
            std::ofstream g(sbPath, std::ios::binary);
            writeVec(g, shadowBase);
        }
    }
    // the ground around the terrace: its height smooth (a cubic B-spline over the model's cells)
    say(0.55f, "Terasin cevresindeki kaya");
    {
        const Geo g2 = geo;
        const Dem& D = dem;
        dagBuildNear(*this, cfg, [&](double mx, double mz, bool wantWater) {
            double la, lo;
            g2.inverse(mx, mz, la, lo);
            const double fr = (kDemLatTop - la) * 3600.0, fc = (lo - kDemLonLeft) * 3600.0;
            const int r = int(std::floor(fr)), c = int(std::floor(fc));
            const float ar = float(fr - r), ac = float(fc - c);
            float wr[4], wc[4];
            const auto B = [](float t, float* w) {
                const float t2 = t * t, t3 = t2 * t;
                w[0] = (1 - 3 * t + 3 * t2 - t3) / 6;
                w[1] = (4 - 6 * t2 + 3 * t3) / 6;
                w[2] = (1 + 3 * t + 3 * t2 - 3 * t3) / 6;
                w[3] = t3 / 6;
            };
            B(ar, wr);
            B(ac, wc);
            float sum = 0.0f, wat = 0.0f;
            for (int a2 = 0; a2 < 4; ++a2)
                for (int b2 = 0; b2 < 4; ++b2) {
                    const int rr = r - 1 + a2, cc = c - 1 + b2;
                    const float w = wr[a2] * wc[b2];
                    sum += w * D.at(rr, cc);
                    if (wantWater) {
                        const uint16_t v = D.v[size_t(rr) * kDemW + cc];
                        if (v == D.v[size_t(rr) * kDemW + cc + 1] && v == D.v[size_t(rr + 1) * kDemW + cc] && v == D.v[size_t(rr) * kDemW + cc - 1] && v == D.v[size_t(rr - 1) * kDemW + cc])
                            wat += w;
                    }
                }
            return DagDemSample{sum, wat};
        });
    }
    dem.close();
    buildMs = nowMs() - t0;
    return true;
}

const DagPlace* DagWorld::placeAt(double mx, double mz, double range) const {
    const DagPlace* best = nullptr;
    double bestD = 1e18;
    for (const DagPlace& P : places) {
        const double d = std::hypot(P.mx - mx, P.mz - mz);
        // (far away a cell of the picture covers more ground: the reach grows with the distance)
        if (d < double(P.reach) + range * 0.01 && d < bestD) {
            best = &P;
            bestD = d;
        }
    }
    return best;
}

void DagWorld::toPlane(float3 p, double& mx, double& mz, double& alt) const {
    const double Y = double(p.y) + double(h0) + double(Re);
    const double rho = std::sqrt(double(p.x) * p.x + double(p.z) * p.z);
    const double k = rho > 1e-6 ? double(Re) * std::atan2(rho, Y) / rho : 1.0;
    mx = p.x * k;
    mz = p.z * k;
    alt = std::sqrt(rho * rho + Y * Y) - double(Re);
}

std::string DagWorld::summary() const {
    char b[512];
    std::snprintf(b, sizeof(b), "dag: %zu derece karosu, %.1f milyon ucgen, en uzak gorunen hucre %.1f km", far.size(), double(farTriangles) * 1e-6, farthestM * 1e-3);
    std::string s = b;
    for (const auto& band : bands) {
        std::snprintf(b, sizeof(b), "\n  %3.0f-%3.0f km: %.2f milyon hucre", band.from * 1e-3, band.to * 1e-3, double(band.cells) * 1e-6);
        s += b;
    }
    return s;
}

void DagWorld::release() {
    far.clear();
    for (auto& c : clips) std::vector<uint16_t>().swap(c.info);
    for (auto& a : albedo) std::vector<uint8_t>().swap(a.rgba);
}
