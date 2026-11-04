#include "scenes/dag/dag_app.h"

#include "app/lang.h"
#include "app/theme.h"
#include "core/image_io.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

// the elevation model read directly (the holes' check: nothing of the mesh in it)
namespace {
struct Dem2 {
    std::vector<uint16_t> v;
    bool open(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        v.resize(size_t(14 * 3600) * 10 * 3600);
        f.read(reinterpret_cast<char*>(v.data()), std::streamsize(v.size() * 2));
        return bool(f);
    }
    float heightAt(double lat0, double lon0, double mx, double mz) const {
        const double R = 6371000.0;
        const double s = std::sqrt(mx * mx + mz * mz) / R;
        const double az = std::atan2(mx, -mz);
        const double la0 = lat0 * M_PIf / 180.0;
        const double sinLat = std::sin(la0) * std::cos(s) + std::cos(la0) * std::sin(s) * std::cos(az);
        const double lat = std::asin(sinLat);
        const double lon = lon0 * M_PIf / 180.0 + std::atan2(std::sin(az) * std::sin(s) * std::cos(la0), std::cos(s) - std::sin(la0) * sinLat);
        const double fr = (46.0 - lat * 180.0 / M_PIf) * 3600.0, fc = (lon * 180.0 / M_PIf - 34.0) * 3600.0;
        const int W = 14 * 3600, H = 10 * 3600;
        const int r = std::clamp(int(fr), 0, H - 2), c = std::clamp(int(fc), 0, W - 2);
        const float ar = float(fr - r), ac = float(fc - c);
        const auto at = [&](int a, int b) { return float(v[size_t(a) * W + b]) / 8.0f - 500.0f; };
        return (1 - ar) * ((1 - ac) * at(r, c) + ac * at(r, c + 1)) + ar * ((1 - ac) * at(r + 1, c) + ac * at(r + 1, c + 1));
    }
};
}   // namespace

// a tiling sheet of high cloud: long streaks along the upper wind, broken by a warp. Gradient noise on a wrapped lattice
static void makeCirrus(std::vector<unsigned char>& out, int n, float stretch) {
    out.assign(size_t(n) * n, 0);
    auto hash = [](int x, int y, int seed) {
        unsigned h = unsigned(x) * 0x8DA6B343u ^ unsigned(y) * 0xD8163841u ^ unsigned(seed) * 0xCB1AB31Fu;
        h ^= h >> 13;
        h *= 0x5BD1E995u;
        h ^= h >> 15;
        return h;
    };
    auto noise = [&](float x, float y, int px, int py, int seed) {
        const int xi = int(std::floor(x)), yi = int(std::floor(y));
        const float fx = x - xi, fy = y - yi;
        const float u = fx * fx * fx * (fx * (fx * 6 - 15) + 10), v = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
        auto g = [&](int ix, int iy, float dx, float dy) {
            const unsigned h = hash(((ix % px) + px) % px, ((iy % py) + py) % py, seed);
            const float a = float(h & 0xFFFF) / 65535.0f * 6.2831853f;
            return dx * std::cos(a) + dy * std::sin(a);
        };
        const float n00 = g(xi, yi, fx, fy), n10 = g(xi + 1, yi, fx - 1, fy), n01 = g(xi, yi + 1, fx, fy - 1), n11 = g(xi + 1, yi + 1, fx - 1, fy - 1);
        return 1.5f * ((1 - v) * ((1 - u) * n00 + u * n10) + v * ((1 - u) * n01 + u * n11));
    };
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    const int threads = std::max(1, int(std::thread::hardware_concurrency()));
    const int sx = 8, sy = int(8 * stretch);
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (;;) {
                const int y = next.fetch_add(1);
                if (y >= n) return;
                for (int x = 0; x < n; ++x) {
                    const float u = float(x) / n, v = float(y) / n;
                    const float wx = 0.03f * noise(u * 5, v * 5, 5, 5, 11) + 0.008f * noise(u * 23, v * 23, 23, 23, 12);
                    const float wy = 0.012f * noise(u * 5, v * 5, 5, 5, 13) + 0.004f * noise(u * 23, v * 23, 23, 23, 14);
                    const float pu = u + wx, pv = v + wy;
                    float sum = 0.0f, amp = 0.5f, tot = 0.0f;
                    int px = sx, py = sy;
                    for (int o = 0; o < 7; ++o) {
                        sum += amp * noise(pu * px, pv * py, px, py, o);
                        tot += amp;
                        amp *= 0.55f;
                        px *= 2;
                        py *= 2;
                    }
                    // long fine filaments, stronger than the sheet they lie in
                    float fil = 0.0f, fa = 0.5f, ft = 0.0f;
                    int fx = sx * 3, fy = sy * 6;
                    for (int o = 0; o < 4; ++o) {
                        fil += fa * (1.0f - 2.0f * std::fabs(noise(pu * fx + 3.1f * o, pv * fy, fx, fy, 40 + o)));
                        ft += fa;
                        fa *= 0.6f;
                        fx *= 2;
                        fy *= 2;
                    }
                    const float val = std::clamp((0.5f + 0.9f * sum / tot) * (0.72f + 0.42f * fil / ft), 0.0f, 1.0f);
                    out[size_t(y) * n + x] = (unsigned char)(val * 255.0f + 0.5f);
                }
            }
        });
    for (auto& t : pool) t.join();
}

void DagApp::loadConfig() {
    cfg = Config{};
    cfg.load(root + "/scenes/dag.cfg");
    for (const std::string& o : overrides) {
        const size_t e = o.find('=');
        if (e != std::string::npos) cfg.set(o.substr(0, e), o.substr(e + 1));
    }
    width = cfg.i("width", 2560);
    height = cfg.i("height", 1440);
    air.mieScat = make_float3(1, 1, 1) * cfg.f("air.mie", 3.0e-6f);
    air.mieAbs = make_float3(1, 1, 1) * (cfg.f("air.mie", 3.0e-6f) * 0.11f);
    air.mieH = cfg.f("air.mieH", 1200.0f);
    air.mieG = cfg.f("air.mieG", 0.80f);
    air.hazeScat = make_float3(1, 1, 1) * cfg.f("air.haze", 1.0e-6f);
    air.hazeH = cfg.f("air.hazeH", 4000.0f);
    air.lowScat = make_float3(1, 1, 1) * cfg.f("air.low", 4.0e-6f);
    air.lowH = cfg.f("air.lowH", 500.0f);
    air.rayleigh = make_float3(5.802e-6f, 13.558e-6f, 33.1e-6f) * cfg.f("air.rayleigh", 1.0f);
    air.top = cfg.f("air.top", 100000.0f);
    air.sunE = cfg.v3("sun.E", make_float3(1.7f, 1.85f, 1.9f));
}

float3 DagApp::sunDir() const {
    const float az = cfg.f("sun.azimuth", 299.0f) * M_PIf / 180.0f, el = cfg.f("sun.elevation", 2.0f) * M_PIf / 180.0f;
    return make_float3(std::cos(el) * std::sin(az), std::sin(el), -std::cos(el) * std::cos(az));
}

bool DagApp::prepareWorld(std::string& err) {
    world.progress = progress;
    if (!world.prepare(root, cfg)) {
        err = world.error;
        return false;
    }
    air.Re = world.Re;
    eyeAlt = world.h0 + 1.7f;
    return true;
}

bool DagApp::initRenderer(std::string& err) {
    if (!R.init(width, height, root)) {
        err = "renderer";
        return false;
    }
    if (!R.compile(err)) return false;
    R.uploadGround(world);
    R.params.atmTop = air.top;
    R.params.sunRad = 0.2665f * M_PIf / 180.0f;
    R.params.sunCos = std::cos(R.params.sunRad);
    R.params.sunFlat = cfg.f("sun.flat", 0.88f);
    R.params.sunZoomGain = 1.0f;
    R.params.sunE = air.sunE;
    R.uploadRock(0, root + "/assets/dag/cc0/kayaA_albedo.jpg", root + "/assets/dag/cc0/kayaA_normal.jpg");
    R.uploadRock(1, root + "/assets/dag/cc0/kayaB_albedo.jpg", root + "/assets/dag/cc0/kayaB_normal.jpg");
    R.params.rockTile[0] = cfg.f("rock.tile.fine", 1.8f);
    R.params.rockTile[1] = cfg.f("rock.tile.mid", 80.0f);
    R.params.rockTile[2] = cfg.f("rock.tile.large", 900.0f);
    R.params.rockTile[3] = cfg.f("rock.tile.field", 14.4f);
    R.params.seaSlope2 = cfg.f("sea.slope2", 0.022f);
    R.params.seaColour = cfg.v3("sea.colour", make_float3(0.006f, 0.018f, 0.028f));
    // the high thin cloud: a sheet, streaks along the upper wind
    R.params.cirrusAlt = cfg.f("cirrus.alt", 9500.0f);
    R.params.cirrusTile = cfg.f("cirrus.tile", 180000.0f);
    R.params.cirrusCover = cfg.f("cirrus.cover", 0.0f);
    R.params.cirrusThick = cfg.f("cirrus.thick", 0.18f);
    if (R.params.cirrusCover > 0.0f) {
        std::vector<unsigned char> c;
        const int n = 4096;
        makeCirrus(c, n, cfg.f("cirrus.stretch", 5.0f));
        R.uploadCirrus(c, n);
    }
    buildAir();
    applyDisplay();
    R.setPattern(cfg.i("samples", 16), cfg.f("filter.sigma", 0.40f));
    farthestDrawn = measureFarthest();
    return true;
}

void DagApp::buildAir() {
    const float3 sun = sunDir();
    R.params.sunDir = sun;
    // the shadow height map: over each 500 m cell the altitude above which the sun is seen; a sweep away from the sun
    {
        const int n = world.shadowN;
        const float cell = world.shadowCell;
        shadow.n = n;
        shadow.cell = cell;
        shadow.half = cell * n * 0.5f;
        shadow.s.assign(size_t(n) * n, -1e9f);
        // in the plane, away from the sun
        const float gx = -sun.x, gz = -sun.z;
        const float gl = std::sqrt(gx * gx + gz * gz);
        const float dx = gx / gl, dz = gz / gl;
        const bool xMain = std::fabs(dx) >= std::fabs(dz);
        const float mn = xMain ? dx : dz, mi = xMain ? dz : dx;
        const int stepMain = mn >= 0 ? 1 : -1;
        const float slope = mi / std::fabs(mn);
        const float ds = cell / std::fabs(mn);
        const double Rd = world.Re;
        std::vector<float>& S = shadow.s;
        const std::vector<float>& B = world.shadowBase;
        for (int im = 0; im < n; ++im) {
            const int i = stepMain > 0 ? im : n - 1 - im;
            for (int j = 0; j < n; ++j) {
                const int x = xMain ? i : j, z = xMain ? j : i;
                const float base = B[size_t(z) * n + x];
                float up = -1e9f;
                if (im > 0) {
                    const float jf = j - slope;
                    const int j0 = int(std::floor(jf));
                    const float a = jf - j0;
                    const int ip = i - stepMain;
                    if (j0 >= 0 && j0 + 1 < n) {
                        const float s0 = xMain ? S[size_t(j0) * n + ip] : S[size_t(ip) * n + j0];
                        const float s1 = xMain ? S[size_t(j0 + 1) * n + ip] : S[size_t(ip) * n + j0 + 1];
                        up = s0 + (s1 - s0) * a;
                    }
                }
                // the sun's height over the local horizon
                const double mx = (x + 0.5) * cell - shadow.half, mz = (z + 0.5) * cell - shadow.half;
                const double r2 = mx * mx + mz * mz;
                const double t = std::sqrt(r2) / Rd;
                const double ux = t > 1e-9 ? mx / std::sqrt(r2) * std::sin(t) : 0.0, uz = t > 1e-9 ? mz / std::sqrt(r2) * std::sin(t) : 0.0, uy = std::cos(t);
                const double mu = sun.x * ux + sun.y * uy + sun.z * uz;
                const float tanE = float(mu / std::sqrt(std::max(1.0 - mu * mu, 1e-9)));
                S[size_t(z) * n + x] = std::max(base, up - ds * tanE);
            }
        }
    }
    // the tables are kept by what makes them: sun, air, sizes. A new build of the code does not count: --set air.tazele=1 (or delete cache/dag/hava_*)
    {
        char tag[256];
        std::snprintf(tag, sizeof(tag), "hava_%.4f_%.4f_%.3g_%.3g_%.3g_%.3g_%.3g_%.3g_%.3g_%d_%d_%d_%d_%d_v3.bin", sun.x, sun.y, air.mieScat.x, air.mieH, air.mieG, air.hazeScat.x, air.hazeH, air.lowScat.x, air.lowH,
                      cfg.i("sky.w", 4096), cfg.i("sky.h", 1536), cfg.i("frox.w", 1024), cfg.i("frox.h", 640), cfg.i("frox.d", 48));
        const std::string path = root + "/cache/dag/" + tag;
        if (cfg.i("air.tazele", 0) == 0 && dagTablesRead(path, tables)) {
            std::printf("dag: hava tablolari diskten\n");
        } else {
            dagBuildTables(air, eyeAlt, sun, cfg.i("air.shadow", 1) ? &shadow : nullptr, world.h0, tables, cfg.i("sky.w", 4096), cfg.i("sky.h", 1536), cfg.i("frox.w", 1024), cfg.i("frox.h", 640), cfg.i("frox.d", 48));
            dagTablesWrite(path, tables);
            std::printf("dag: hava tablolari %.1f s\n", tables.ms * 1e-3);
        }
    }
    R.uploadTables(tables);
    std::vector<float4>().swap(tables.froxS);
    std::vector<float4>().swap(tables.froxT);
}

void DagApp::applyDisplay() {
    R.params.exposure = std::pow(2.0f, cfg.f("exposure", 2.0f) + exposureEv);
    R.params.bloomStrength = cfg.f("bloom", 0.03f);
    R.params.vignette = cfg.f("vignette", 0.08f);
    R.params.saturation = cfg.f("saturation", 1.25f);
    R.params.shadowLift = cfg.f("lift", 2.5f);
    R.params.liftKnee = cfg.f("lift.knee", 0.06f);
    R.params.white = cfg.v3("white", make_float3(1.05f, 1.0f, 0.90f));
    R.params.zoomK = 1.0f;
    R.params.albedoGain = cfg.f("albedo.gain", 0.55f);
    R.params.albedoGainSnow = cfg.f("albedo.snow", 1.0f);
    R.params.rockGain = cfg.f("rock.gain", 1.7f);
    R.params.debug = cfg.i("debug", 0);
}

float DagApp::measureFarthest() {
    const DagCam keep = cam;
    float best = 0.0f;
    const float vfov = 16.0f;
    const float aspect = float(width) / float(height);
    const float hfov = 2.0f * std::atan(std::tan(vfov * 0.5f * M_PIf / 180.0f) * aspect) * 180.0f / M_PIf;
    for (float bearing = 0.0f; bearing < 360.0f; bearing += hfov * 0.95f) {
        DagCam c;
        c.yaw = -bearing * M_PIf / 180.0f;
        c.pitch = -2.0f * M_PIf / 180.0f;
        c.fovY = vfov;
        c.feet.y = world.groundY(0, 0);
        cam = c;
        render();
        const std::vector<float4> h = R.hdrHost();
        for (const float4& q : h)
            if (q.w > best) best = q.w;
    }
    cam = keep;
    return best;
}

float3 DagApp::eye() const { return cam.feet + make_float3(0, cam.eyeHeight, 0); }

void DagApp::render() {
    const float3 fwd = make_float3(-std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch), -std::cos(cam.yaw) * std::cos(cam.pitch));
    const float3 right = normalize(cross(fwd, make_float3(0, 1, 0)));
    const float3 up = cross(right, fwd);
    const float tanY = std::tan(cam.fovY * 0.5f * M_PIf / 180.0f);
    const float aspect = float(width) / float(height);
    R.params.eye = eye();
    R.params.W = fwd;
    R.params.U = right * (tanY * aspect);
    R.params.V = up * tanY;
    R.params.pixelAngle = 2.0f * tanY / float(height);
    // through the binoculars: the display's contrast around the sky's brightness there (the eye settles on what little there is)
    const float baseTan = std::tan(cfg.f("fov", 45.0f) * 0.5f * M_PIf / 180.0f);
    const float zoom = baseTan / tanY;
    const float k = std::clamp((zoom - 2.0f) / 14.0f, 0.0f, 1.0f);
    R.params.zoomK = 1.0f + cfg.f("zoom.contrast", 0.45f) * k;
    R.params.sunZoomGain = 1.0f / std::max(zoom * zoom, 1.0f);
    if (k > 0.0f) {
        const float3 sk = dagSkyDir(tables, normalize(fwd + up * (6.0f * tanY / float(height) * 60.0f)));
        R.params.zoomPivot = (0.2126f * sk.x + 0.7152f * sk.y + 0.0722f * sk.z) * R.params.exposure;
    }
    R.render();
}

bool DagApp::save(const std::string& pathNoExt) {
    std::vector<unsigned char> rgba;
    R.download(rgba);
    return writePNG(pathNoExt + ".png", width, height, rgba.data());
}

DagCam DagApp::view(int i) {
    DagCam c;
    const float deg = M_PIf / 180.0f;
    // bearings from north, clockwise; yaw grows to the left
    const float bearing[] = {299.0f, 30.0f, 118.0f, 200.0f, 330.0f, 299.0f};
    const float pitch[] = {-2.0f, -2.5f, -2.5f, -4.0f, -3.0f, -14.0f};
    const int k = std::clamp(i, 0, viewCount() - 1);
    c.yaw = -bearing[k] * deg;
    c.pitch = pitch[k] * deg;
    // a few steps from the railing on that side: the view at once, the railing low in the picture
    const float r = 4.2f;
    c.feet = make_float3(r * std::sin(bearing[k] * deg), 0.0f, -r * std::cos(bearing[k] * deg));
    return c;
}

int DagApp::viewCount() { return 6; }

std::string DagApp::summary() const {
    char b[512];
    std::snprintf(b, sizeof(b), "\ndag: GAS %.2f GB, dokular %.2f GB, tablolar %.2f GB, kurulum %.1f s", double(R.gasBytes) / 1e9, double(R.texBytes) / 1e9,
                  double(R.tableBytes) / 1e9, R.buildGasMs * 1e-3);
    return world.summary() + b;
}

std::string DagApp::lookingAt() const {
    const float t = R.probe.x;
    const int kind = int(R.probe.y + 0.5f);
    if (kind == 0 || t <= 0.0f) return std::string();
    const float3 fwd = normalize(R.params.W);
    const float3 p = R.params.eye + fwd * t;
    double mx, mz, alt;
    world.toPlane(p, mx, mz, alt);
    const std::string range = theme::num(t >= 10000.0f ? std::round(t * 1e-3f) : t * 1e-3f, t >= 10000.0f ? 0 : 1) + " km";
    if (kind == 2) return app::L("Karadeniz", "Black Sea") + std::string(" · ") + range;
    const DagPlace* P = world.placeAt(mx, mz, t);
    std::string s;
    if (P) {
        s = app::L(P->nameTr.c_str(), P->nameEn.c_str());
        if (P->published > 0.0f) s += " · " + theme::thousands((unsigned long long)std::lround(P->published)) + " m";
        s += " · ";
    }
    return s + range;
}

void DagApp::release() {
    R.release();
    world.release();
}

int runDagCli(int argc, char** argv) {
    DagApp a;
    a.root = PROJECT_ROOT;
    std::string out = "captures/dag/kare";
    std::vector<int> views;
    std::vector<DagCam> freeCams;
    int bench = 0, stab = 0, refSpp = 64;
    bool countOnly = false, menzil = false, delik = false;
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
        else if (s == "--say") countOnly = true;
        else if (s == "--menzil") menzil = true;
        else if (s == "--delik") delik = true;
        else if (s == "--kararlilik") stab = std::stoi(next());
        else if (s == "--yol") path = next();
        else if (s == "--hiz") speed = std::stof(next());
        else if (s == "--ref") refSpp = std::stoi(next());
        else if (s == "--cam") {
            // feet x y z, bearing and pitch in degrees, vertical field in degrees
            DagCam c;
            float bearing = 0, pitch = 0;
            if (std::sscanf(next().c_str(), "%f %f %f %f %f %f", &c.feet.x, &c.feet.y, &c.feet.z, &bearing, &pitch, &c.fovY) >= 5) {
                c.yaw = -bearing * M_PIf / 180.0f;
                c.pitch = pitch * M_PIf / 180.0f;
                freeCams.push_back(c);
            }
        }
    }
    a.loadConfig();
    std::string err;
    if (!a.prepareWorld(err)) {
        std::fprintf(stderr, "dag: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s\n", a.world.summary().c_str());
    if (countOnly) return 0;
    if (!a.initRenderer(err)) {
        std::fprintf(stderr, "dag: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s\n", a.summary().c_str());
    std::error_code ec;
    fs::create_directories(fs::path(a.root) / fs::path(out).parent_path(), ec);
    if (menzil) {
        // the far distance's evidence: a whole circle of frames, the farthest ground any sample of any pixel hit
        float best = 0.0f;
        float bestBearing = 0.0f, bestEl = 0.0f;
        std::vector<float> perBearing;
        const float vfov = 16.0f;
        const float aspect = float(a.width) / float(a.height);
        const float hfov = 2.0f * std::atan(std::tan(vfov * 0.5f * M_PIf / 180.0f) * aspect) * 180.0f / M_PIf;
        for (float bearing = 0.0f; bearing < 360.0f; bearing += hfov * 0.95f) {
            DagCam c;
            c.yaw = -bearing * M_PIf / 180.0f;
            c.pitch = -2.0f * M_PIf / 180.0f;
            c.fovY = vfov;
            c.feet.y = a.world.groundY(0, 0);
            a.cam = c;
            a.render();
            const std::vector<float4> h = a.R.hdrHost();
            float here = 0.0f;
            size_t at = 0;
            for (size_t i = 0; i < h.size(); ++i)
                if (h[i].w > here) {
                    here = h[i].w;
                    at = i;
                }
            const float px = float(at % a.width), py = float(at / a.width);
            const float ang = std::atan(((px + 0.5f) / a.width * 2.0f - 1.0f) * std::tan(hfov * 0.5f * M_PIf / 180.0f)) * 180.0f / M_PIf;
            const float el = -2.0f + std::atan((1.0f - (py + 0.5f) / a.height * 2.0f) * std::tan(vfov * 0.5f * M_PIf / 180.0f)) * 180.0f / M_PIf;
            std::printf("dag: menzil: yon %5.1f derece: en uzak %.1f km (yon %.2f, yukseklik %.2f)\n", bearing, here * 1e-3f, bearing + ang, el);
            if (here > best) {
                best = here;
                bestBearing = bearing + ang;
                bestEl = el;
            }
        }
        std::printf("dag: menzil: en uzak cizilen yer %.1f km, yon %.2f derece, yukseklik %.2f derece\n", best * 1e-3f, bestBearing, bestEl);
        // the named places: is each where it should be? A ray at its top (or just under it), the distance it meets, against the distance by the map
        for (const DagPlace& P : a.world.places) {
            const float3 top = a.world.worldPoint(P.mx, P.mz, P.altitude > 0.0f ? double(P.altitude) - 15.0 : 300.0);
            DagCam c;
            c.feet = make_float3(0, a.world.groundY(0, 0), 0);
            c.fovY = 0.5f;
            const float3 toTop = top - (c.feet + make_float3(0, c.eyeHeight, 0));
            const float d = length(toTop);
            c.pitch = std::asin(toTop.y / d);
            c.yaw = std::atan2(-toTop.x, -toTop.z);
            a.cam = c;
            a.render();
            const float bearing = std::fmod(-c.yaw * 180.0f / M_PIf + 360.0f, 360.0f);
            std::printf("dag: yer: %-16s yon %6.2f, yukseklik %5.2f, haritada %6.1f km, isin %6.1f km (%s)\n", P.nameEn.c_str(), bearing, c.pitch * 180.0f / M_PIf, std::hypot(P.mx, P.mz) * 1e-3,
                        a.R.probe.x * 1e-3f, a.R.probe.y < 0.5f ? "gok" : a.R.probe.y > 1.5f ? "deniz" : "zemin");
        }
        // what is SEEN, not only drawn: over a whole circle through the binoculars' narrowest view, every picture column's silhouette against the
        // sky: how far it is, how much it stands out. Threshold, written before measuring: Koschmieder's 2 % (the classic limit of sight)
        {
            const float vf = 1.5f;
            const float hf = 2.0f * std::atan(std::tan(vf * 0.5f * M_PIf / 180.0f) * aspect) * 180.0f / M_PIf;
            std::vector<float> ranges, contrasts;
            for (float pitchDeg : {-1.05f, -1.75f})
                for (float bearing = 0.0f; bearing < 360.0f; bearing += hf * 0.98f) {
                    DagCam c;
                    c.yaw = -bearing * M_PIf / 180.0f;
                    c.pitch = pitchDeg * M_PIf / 180.0f;
                    c.fovY = vf;
                    c.feet.y = a.world.groundY(0, 0);
                    a.cam = c;
                    a.render();
                    const std::vector<float4> h = a.R.hdrHost();
                    for (int x = 0; x < a.width; x += 2) {
                        int yS = -1;
                        for (int y = 8; y < a.height - 8; ++y)
                            if (h[size_t(y) * a.width + x].w > 0.0f) {
                                yS = y;
                                break;
                            }
                        if (yS < 8) continue;
                        // (the whole picture column is sky above it)
                        float sky = 0.0f, ground = 0.0f, far2 = 0.0f;
                        auto lum = [](const float4& q) { return 0.2126f * q.x + 0.7152f * q.y + 0.0722f * q.z; };
                        for (int k = 3; k <= 6; ++k) {
                            sky += lum(h[size_t(yS - k) * a.width + x]);
                            ground += lum(h[size_t(yS + k) * a.width + x]);
                            far2 = std::max(far2, h[size_t(yS + 1) * a.width + x].w);
                        }
                        if (sky <= 0.0f || far2 <= 0.0f) continue;
                        ranges.push_back(far2);
                        contrasts.push_back(std::fabs(ground - sky) / sky);
                    }
                }
            // by distance bands: the median contrast of the columns there, how many pass 2 %
            float seen = 0.0f;
            std::vector<float> bin[60];
            for (size_t i = 0; i < ranges.size(); ++i) bin[std::min(59, int(ranges[i] / 10000.0f))].push_back(contrasts[i]);
            for (int b = 0; b < 60; ++b) {
                if (bin[b].size() < 40) continue;
                std::sort(bin[b].begin(), bin[b].end());
                const float med = bin[b][bin[b].size() / 2], p90 = bin[b][bin[b].size() * 9 / 10];
                std::printf("dag: kontrast %3d-%3d km: %7zu sutun, ortanca %.1f %%, en iyi onda bir %.1f %%\n", b * 10, b * 10 + 10, bin[b].size(), med * 100.0f, p90 * 100.0f);
                if (med >= 0.02f) seen = (b + 1) * 10000.0f;
            }
            std::printf("dag: gorunen en uzak (ortanca kontrast >= %%2): %.0f km\n", seen * 1e-3f);
        }
        // the sea's own horizon
        const double h = a.eyeAlt;
        std::printf("dag: menzil: denizin ufku %.1f km (etkin yaricap %.0f km)\n", std::sqrt(2.0 * a.world.Re * h) * 1e-3, a.world.Re * 1e-3);
        a.release();
        return 0;
    }
    if (delik) {
        // holes: the mesh leaves out what the terrace cannot see. From the terrace's corners, jumping too: does any ray go through the
        // ground? Each ray of a sparse grid of pixels against the elevation model itself (no mesh), on the same curved Earth
        std::string e2;
        Dem2 dem;
        if (!dem.open(a.root + "/assets/dag/dem_1s.u16")) {
            std::fprintf(stderr, "dag: model yok\n");
            return 1;
        }
        a.R.setPattern(1, 0.0f);
        long long rays = 0, holes = 0, holesFar = 0;
        float3 worst = make_float3(0, 0, 0);
        float worstDepth = 0.0f;
        const int stepPx = 6;
        for (const float2& corner : a.world.terrace) {
            for (float lift : {0.0f, 1.2f}) {
                for (float bearing = 0.0f; bearing < 360.0f; bearing += 40.0f) {
                    DagCam c;
                    c.feet = make_float3(corner.x * 0.93f, a.world.groundY(corner.x * 0.93f, corner.y * 0.93f) + lift, corner.y * 0.93f);
                    c.yaw = -bearing * M_PIf / 180.0f;
                    c.pitch = -6.0f * M_PIf / 180.0f;
                    c.fovY = 45.0f;
                    a.cam = c;
                    a.render();
                    const std::vector<float4> h = a.R.hdrHost();
                    const float3 eye = a.R.params.eye, W = a.R.params.W, U = a.R.params.U, V = a.R.params.V;
                    std::atomic<long long> rayC{0}, holeC{0}, holeFarC{0};
                    std::mutex mu;
                    std::vector<std::thread> pool;
                    std::atomic<int> nextRow{0};
                    const int threads = std::max(1, int(std::thread::hardware_concurrency()));
                    for (int th = 0; th < threads; ++th)
                        pool.emplace_back([&] {
                            for (;;) {
                                const int y0 = nextRow.fetch_add(stepPx);
                                if (y0 >= a.height) return;
                                for (int x0 = 0; x0 < a.width; x0 += stepPx) {
                                    const float xn = ((x0 + 0.5f) / a.width) * 2.0f - 1.0f, yn = 1.0f - ((y0 + 0.5f) / a.height) * 2.0f;
                                    const float3 d = normalize(W + U * xn + V * yn);
                                    if (d.y > 0.02f) continue;
                                    const float tMesh = h[size_t(y0) * a.width + x0].w;
                                    // up to the mesh's hit, or to the air's limit of sight
                                    const float tEnd = tMesh > 0.0f ? tMesh * 0.985f : 560000.0f;
                                    rayC.fetch_add(1);
                                    float t = 1600.0f;
                                    while (t < tEnd) {
                                        const float3 q = eye + d * t;
                                        double mx, mz, alt;
                                        a.world.toPlane(q, mx, mz, alt);
                                        if (alt < 0.0) break;   // the sea: drawn by itself
                                        const float g = dem.heightAt(a.world.lat0, a.world.lon0, mx, mz);
                                        const double r = std::hypot(mx, mz);
                                        // (margins: 45 m where the rock's breakup is, 8 m where the mesh is the model's own triangles)
                                        const double margin = r < 30000.0 ? 45.0 : 8.0;
                                        if (alt + margin < g) {
                                            holeC.fetch_add(1);
                                            if (r >= 30000.0) holeFarC.fetch_add(1);
                                            std::lock_guard<std::mutex> lk(mu);
                                            if (float(g - alt) > worstDepth) {
                                                worstDepth = float(g - alt);
                                                worst = make_float3(float(mx), float(mz), t);
                                            }
                                            break;
                                        }
                                        t += std::max(12.0f, t * 0.0008f);
                                    }
                                }
                            }
                        });
                    for (auto& th : pool) th.join();
                    rays += rayC.load();
                    holes += holeC.load();
                    holesFar += holeFarC.load();
                }
            }
        }
        std::printf("dag: delik: %lld isin, %lld yerin icinden gecti (%lld tanesi 30 km otesinde)", rays, holes, holesFar);
        if (holes) std::printf(", en derini %.0f m, uzaklik %.1f km", worstDepth, worst.z * 1e-3f);
        std::printf("\n");
        a.release();
        return 0;
    }
    if (stab > 0) {
        // a walk at 60 frames a second for tools/titreme.py and tools/kararlilik.py: each moment drawn live (the configured
        // samples) and as a reference (--ref samples)
        const DagCam c0 = !freeCams.empty() ? freeCams[0] : DagApp::view(views.empty() ? 0 : views[0]);
        const int live = a.cfg.i("samples", 16);
        const float sigma = a.cfg.f("filter.sigma", 0.40f);
        const std::string dir = (fs::path(a.root) / out).generic_string();
        fs::create_directories(dir, ec);
        double liveMs = 0.0;
        setPNGCompression(1);
        for (int f = 0; f < stab; ++f) {
            const float t = f / 60.0f;
            DagCam c = c0;
            const float fx = -std::sin(c0.yaw), fz = -std::cos(c0.yaw);
            if (path == "ileri") c.feet = c0.feet + make_float3(fx, 0, fz) * (speed * t);
            else if (path == "don") c.yaw = c0.yaw - speed * t;
            else c.feet = c0.feet + make_float3(-fz, 0, fx) * (speed * t);
            c.feet.y = a.world.groundY(c.feet.x, c.feet.z);
            a.cam = c;
            a.R.params.time = t;
            a.R.setPattern(live, sigma);
            a.render();
            liveMs += a.R.renderMs + a.R.displayMs;
            char name[64];
            std::snprintf(name, sizeof(name), "/live_%04d", f);
            a.save(dir + name);
            a.R.setPattern(refSpp, sigma);
            a.render();
            std::snprintf(name, sizeof(name), "/ref_%04d", f);
            a.save(dir + name);
        }
        std::printf("dag: kararlilik %d kare (%s, %.2f) -> %s, canli %d ornek (ortalama %.2f ms), referans %d\n", stab, path.c_str(), speed, dir.c_str(), live, liveMs / stab, refSpp);
        a.release();
        return 0;
    }
    if (views.empty() && freeCams.empty()) views = {0, 1, 2, 3, 4, 5};
    auto shoot = [&](const DagCam& c, const std::string& name) {
        a.cam = c;
        a.cam.feet.y = a.world.groundY(a.cam.feet.x, a.cam.feet.z);
        a.render();
        a.render();
        const std::string path = (fs::path(a.root) / (out + "_" + name)).generic_string();
        a.save(path);
        std::printf("dag: %s (%.2f ms isleme, %.2f ms goruntu, sonlu olmayan %u)\n", path.c_str(), a.R.renderMs, a.R.displayMs, a.R.badSamples);
        if (a.R.badSamples) std::printf("dag: sonlu olmayan ornek: piksel %.0f %.0f, yol %.0f, uzaklik %.1f m\n", a.R.badAt.x, a.R.badAt.y, a.R.badAt.z, a.R.badAt.w);
        std::fflush(stdout);
        if (bench > 0) {
            double sum = 0.0, worst = 0.0;
            for (int k = 0; k < bench; ++k) {
                a.render();
                sum += a.R.renderMs + a.R.displayMs;
                worst = std::max(worst, double(a.R.renderMs + a.R.displayMs));
            }
            std::printf("dag: olcum %d kare: ortalama %.2f ms (%.1f FPS), en kotu %.2f ms\n", bench, sum / bench, 1000.0 / (sum / bench), worst);
        }
    };
    for (size_t k = 0; k < freeCams.size(); ++k) shoot(freeCams[k], freeCams.size() == 1 ? std::string("serbest") : "serbest" + std::to_string(k));
    for (int v : views) shoot(DagApp::view(v), "bakis" + std::to_string(v));
    a.release();
    return 0;
}
