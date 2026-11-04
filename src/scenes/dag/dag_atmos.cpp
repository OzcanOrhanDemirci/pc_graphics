#define _USE_MATH_DEFINES
#include "scenes/dag/dag_atmos.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <thread>

namespace {

void parallelFor(int n, const std::function<void(int)>& fn) {
    std::atomic<int> next{0};
    const int threads = std::max(1, int(std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (;;) {
                const int i = next.fetch_add(1);
                if (i >= n) return;
                fn(i);
            }
        });
    for (auto& t : pool) t.join();
}

struct D3 {
    double x, y, z;
};
inline D3 operator+(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline D3 operator*(D3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline float3 exp3(float3 v) { return make_float3(std::exp(v.x), std::exp(v.y), std::exp(v.z)); }

struct Medium {
    float3 scatR, scatM, ext;
};
inline Medium medium(const DagAir& a, double h) {
    h = std::max(h, 0.0);
    const float r = float(std::exp(-h / a.rayleighH));
    const float m = float(std::exp(-h / a.mieH));
    const float hz = float(std::exp(-h / a.hazeH));
    const float oz = float(std::max(0.0, 1.0 - std::abs(h - a.ozoneC) / a.ozoneW));
    Medium o;
    o.scatR = a.rayleigh * r;
    const float lw = float(std::exp(-h / a.lowH));
    o.scatM = a.mieScat * m + a.hazeScat * hz + a.lowScat * lw;
    o.ext = o.scatR + o.scatM + a.mieAbs * m + a.ozone * oz;
    return o;
}

// distance along a ray from radius r with cosine mu to a sphere of radius R it is inside of (the far side)
inline double toSphere(double r, double mu, double R) {
    const double d = r * r * (mu * mu - 1.0) + R * R;
    return -r * mu + std::sqrt(std::max(d, 0.0));
}

// Bruneton's parameterization of the transmittance (the same in device/dag.cu)
inline void trUv(const DagAir& a, double r, double mu, float& u, float& v) {
    const double Rg = a.Re, Rt = double(a.Re) + a.top;
    const double H = std::sqrt(Rt * Rt - Rg * Rg);
    const double rho = std::sqrt(std::max(r * r - Rg * Rg, 0.0));
    const double d = toSphere(r, mu, Rt);
    const double dmin = Rt - r, dmax = rho + H;
    u = float((d - dmin) / (dmax - dmin));
    v = float(rho / H);
}

struct Tr {
    const DagAir& a;
    const std::vector<float4>& t;
    int w, h;
    // the light let through towards the top from radius r at cosine mu; 0 under the planet's horizon
    float3 sample(double r, double mu) const {
        const double Rg = a.Re;
        r = std::max(r, Rg + 0.5);
        const double horizon = -std::sqrt(std::max(1.0 - (Rg * Rg) / (r * r), 0.0));
        if (mu < horizon) return make_float3(0, 0, 0);
        float u, v;
        trUv(a, r, mu, u, v);
        const float fx = std::clamp(u, 0.0f, 1.0f) * (w - 1), fy = std::clamp(v, 0.0f, 1.0f) * (h - 1);
        const int x0 = std::min(int(fx), w - 2), y0 = std::min(int(fy), h - 2);
        const float ax = fx - x0, ay = fy - y0;
        const float4 A = t[size_t(y0) * w + x0], B = t[size_t(y0) * w + x0 + 1], C = t[size_t(y0 + 1) * w + x0], E = t[size_t(y0 + 1) * w + x0 + 1];
        const float4 o = A * ((1 - ax) * (1 - ay)) + B * (ax * (1 - ay)) + C * ((1 - ax) * ay) + E * (ax * ay);
        return make_float3(o.x, o.y, o.z);
    }
};

}   // namespace

static void rwHeader(std::fstream& f, DagTables& t, bool write) {
    auto io = [&](void* p, size_t n) {
        if (write) f.write(static_cast<const char*>(p), std::streamsize(n));
        else f.read(static_cast<char*>(p), std::streamsize(n));
    };
    io(&t.trW, 4); io(&t.trH, 4); io(&t.msW, 4); io(&t.msH, 4); io(&t.skyW, 4); io(&t.skyH, 4); io(&t.frW, 4); io(&t.frH, 4); io(&t.frD, 4);
    io(t.sh, sizeof(t.sh)); io(&t.eyeDip, 4); io(&t.froxR0, 4); io(&t.froxLogK, 4); io(&t.froxElMax, 4);
}

bool dagTablesRead(const std::string& path, DagTables& t) {
    std::fstream f(path, std::ios::in | std::ios::binary);
    if (!f) return false;
    rwHeader(f, t, false);
    auto vec = [&](std::vector<float4>& v, size_t n) {
        v.resize(n);
        f.read(reinterpret_cast<char*>(v.data()), std::streamsize(n * sizeof(float4)));
    };
    vec(t.transmittance, size_t(t.trW) * t.trH);
    vec(t.multi, size_t(t.msW) * t.msH);
    vec(t.sky, size_t(t.skyW) * t.skyH);
    vec(t.froxS, size_t(t.frW) * t.frH * t.frD);
    vec(t.froxT, size_t(t.frW) * t.frH * t.frD);
    return bool(f);
}

void dagTablesWrite(const std::string& path, const DagTables& tc) {
    DagTables& t = const_cast<DagTables&>(tc);
    std::fstream f(path, std::ios::out | std::ios::binary);
    if (!f) return;
    rwHeader(f, t, true);
    for (const std::vector<float4>* v : {&t.transmittance, &t.multi, &t.sky, &t.froxS, &t.froxT}) f.write(reinterpret_cast<const char*>(v->data()), std::streamsize(v->size() * sizeof(float4)));
}

float3 dagSkyDir(const DagTables& t, float3 d) {
    const float az = std::atan2(d.x, -d.z);
    const float el = std::asin(std::clamp(d.y, -1.0f, 1.0f));
    const float l = el + t.eyeDip;
    const float lmax = 1.5707963f + t.eyeDip, lmin = 1.5707963f - t.eyeDip;
    const float v = l >= 0 ? 0.5f + 0.5f * std::sqrt(l / lmax) : 0.5f - 0.5f * std::sqrt(-l / lmin);
    const float u = az / 6.2831853f + 0.5f;
    const int x = std::clamp(int(u * t.skyW), 0, t.skyW - 1), y = std::clamp(int(v * t.skyH), 0, t.skyH - 1);
    const float4 c = t.sky[size_t(y) * t.skyW + x];
    return make_float3(c.x, c.y, c.z);
}

void dagBuildTables(const DagAir& air, float eyeAlt, float3 sunF, const DagShadowMap* shadow, float h0, DagTables& out, int skyW, int skyH, int frW, int frH,
                    int frD) {
    const auto t0 = std::chrono::steady_clock::now();
    const double Rg = air.Re, Rt = double(air.Re) + air.top;
    const D3 sun{sunF.x, sunF.y, sunF.z};
    // the transmittance
    out.trW = 512;
    out.trH = 256;
    out.transmittance.assign(size_t(out.trW) * out.trH, make_float4(1, 1, 1, 1));
    const double H = std::sqrt(Rt * Rt - Rg * Rg);
    parallelFor(out.trH, [&](int y) {
        for (int x = 0; x < out.trW; ++x) {
            const double fu = double(x) / (out.trW - 1), fv = double(y) / (out.trH - 1);
            const double rho = fv * H;
            const double r = std::sqrt(rho * rho + Rg * Rg);
            const double dmin = Rt - r, dmax = rho + H;
            const double d = dmin + fu * (dmax - dmin);
            const double mu = d <= 0 ? 1.0 : std::clamp((H * H - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
            const int n = 512;
            float3 od = make_float3(0, 0, 0);
            for (int k = 0; k < n; ++k) {
                const double t = (k + 0.5) / n * d;
                const double rr = std::sqrt(t * t + 2.0 * r * mu * t + r * r);
                od += medium(air, rr - Rg).ext * float(d / n);
            }
            const float3 T = exp3(od * -1.0f);
            out.transmittance[size_t(y) * out.trW + x] = make_float4(T.x, T.y, T.z, 1.0f);
        }
    });
    const Tr tr{air, out.transmittance, out.trW, out.trH};

    // the second and further scatterings (Hillaire 2020): over height and the sun's cosine, an isotropic source
    out.msW = 32;
    out.msH = 32;
    out.multi.assign(size_t(out.msW) * out.msH, make_float4(0, 0, 0, 0));
    parallelFor(out.msH, [&](int y) {
        for (int x = 0; x < out.msW; ++x) {
            const double muS = -1.0 + 2.0 * (x + 0.5) / out.msW;
            const double h = air.top * (y + 0.5) / out.msH;
            const double r = Rg + h;
            const D3 s{std::sqrt(std::max(1.0 - muS * muS, 0.0)), muS, 0.0};
            float3 L2 = make_float3(0, 0, 0), Fms = make_float3(0, 0, 0);
            const int dirs = 64;
            for (int k = 0; k < dirs; ++k) {
                const double cy = 1.0 - (2.0 * k + 1.0) / dirs, sy = std::sqrt(std::max(1.0 - cy * cy, 0.0));
                const double ph = k * 2.39996323;
                const D3 d{sy * std::cos(ph), cy, sy * std::sin(ph)};
                const double dg = r * r * (cy * cy - 1.0) + Rg * Rg;
                const bool ground = cy < 0.0 && dg >= 0.0;
                const double len = ground ? -r * cy - std::sqrt(dg) : toSphere(r, cy, Rt);
                const int n = 24;
                float3 od = make_float3(0, 0, 0);
                float3 L = make_float3(0, 0, 0), F = make_float3(0, 0, 0);
                const float dt = float(len / n);
                for (int i = 0; i < n; ++i) {
                    const double t = (i + 0.5) / n * len;
                    const D3 p{d.x * t, r + d.y * t, d.z * t};
                    const double pr = std::sqrt(dot(p, p));
                    const Medium m = medium(air, pr - Rg);
                    const float3 Tc = exp3((od + m.ext * (0.5f * dt)) * -1.0f);
                    const float3 Ts = tr.sample(pr, dot(p, s) / pr);
                    const float3 sc = m.scatR + m.scatM;
                    L += Tc * sc * Ts * float(1.0 / (4.0 * M_PIf) * dt);
                    F += Tc * sc * dt;
                    od += m.ext * dt;
                }
                if (ground) {
                    const D3 p{d.x * len, r + d.y * len, d.z * len};
                    const double pr = std::sqrt(dot(p, p));
                    const double c = dot(p, s) / pr;
                    if (c > 0.0) L += exp3(od * -1.0f) * tr.sample(pr, c) * air.groundAlbedo * float(c / M_PIf);
                }
                L2 += L * (1.0f / dirs);
                Fms += F * (1.0f / dirs);
            }
            const float3 psi = make_float3(L2.x / std::max(1.0f - Fms.x, 1e-3f), L2.y / std::max(1.0f - Fms.y, 1e-3f), L2.z / std::max(1.0f - Fms.z, 1e-3f));
            out.multi[size_t(y) * out.msW + x] = make_float4(psi.x, psi.y, psi.z, 0);
        }
    });
    auto msAt = [&](double h, double muS) {
        const float fx = float(std::clamp((muS + 1.0) * 0.5 * out.msW - 0.5, 0.0, double(out.msW - 1))),
                    fy = float(std::clamp(h / air.top * out.msH - 0.5, 0.0, double(out.msH - 1)));
        const int x0 = std::min(int(fx), out.msW - 2), y0 = std::min(int(fy), out.msH - 2);
        const float ax = fx - x0, ay = fy - y0;
        const float4 A = out.multi[size_t(y0) * out.msW + x0], B = out.multi[size_t(y0) * out.msW + x0 + 1], C = out.multi[size_t(y0 + 1) * out.msW + x0],
                     E = out.multi[size_t(y0 + 1) * out.msW + x0 + 1];
        const float4 o = A * ((1 - ax) * (1 - ay)) + B * (ax * (1 - ay)) + C * ((1 - ax) * ay) + E * (ax * ay);
        return make_float3(o.x, o.y, o.z);
    };

    // the eye's rays: from the eye's height, along the planet's up at the eye
    const double r0 = Rg + eyeAlt;
    const float g = air.mieG;
    const float mieNorm = 3.0f / (8.0f * M_PIf) * (1.0f - g * g) / (2.0f + g * g);
    auto shadowAt = [&](const D3& p, double alt) -> float {
        if (!shadow || shadow->n == 0) return 1.0f;
        // the plane's coordinates of the point under p
        const double rho = std::sqrt(p.x * p.x + p.z * p.z);
        const double k = rho > 1.0 ? Rg * std::atan2(rho, p.y) / rho : 1.0;
        const double mx = p.x * k, mz = p.z * k;
        const double fx = (mx + shadow->half) / shadow->cell - 0.5, fy = (mz + shadow->half) / shadow->cell - 0.5;
        if (fx < 0 || fy < 0 || fx >= shadow->n - 1 || fy >= shadow->n - 1) return 1.0f;
        const int x0 = int(fx), y0 = int(fy);
        const float ax = float(fx - x0), ay = float(fy - y0);
        const float* S = shadow->s.data();
        const int n = shadow->n;
        const float a = S[size_t(y0) * n + x0], b = S[size_t(y0) * n + x0 + 1], c = S[size_t(y0 + 1) * n + x0], e = S[size_t(y0 + 1) * n + x0 + 1];
        const float hs = (1 - ay) * ((1 - ax) * a + ax * b) + ay * ((1 - ax) * c + ax * e);
        // the sun's disk blurs the edge (the penumbra grows with the distance from what casts it: an average here)
        const float w = 250.0f;
        const float x = std::clamp((float(alt) - hs + w) / (2.0f * w), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    };
    // gathers along d from the eye; at each boundary tb[k] (growing) calls emit(k, S, T)
    auto gather = [&](const D3& d, const double* tb, int nb, int sub, const std::function<void(int, float3, float3)>& emit) {
        const double mu = d.y;
        const double cs = dot(d, sun);
        const float csf = float(cs);
        const float phR = 3.0f / (16.0f * M_PIf) * (1.0f + csf * csf);
        const float phM = mieNorm * (1.0f + csf * csf) / std::pow(std::max(1.0f + g * g - 2.0f * g * csf, 1e-4f), 1.5f);
        float3 S = make_float3(0, 0, 0), od = make_float3(0, 0, 0);
        double prev = 0.0;
        for (int b = 0; b < nb; ++b) {
            const double t1 = tb[b];
            const double dt = (t1 - prev) / sub;
            for (int i = 0; i < sub; ++i) {
                const double t = prev + (i + 0.5) * dt;
                const D3 p{d.x * t, r0 + d.y * t, d.z * t};
                const double pr = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
                const double h = pr - Rg;
                if (h < air.top) {
                    const Medium m = medium(air, h);
                    const float fdt = float(dt);
                    const float3 Tc = exp3((od + m.ext * (0.5f * fdt)) * -1.0f);
                    const double muS = dot(p, sun) / pr;
                    float3 Ts = tr.sample(pr, muS);
                    if (Ts.x + Ts.y + Ts.z > 0.0f) Ts = Ts * shadowAt(p, h);
                    float3 src = (m.scatR * phR + m.scatM * phM) * Ts;
                    src += (m.scatR + m.scatM) * msAt(h, muS);
                    S += Tc * src * air.sunE * fdt;
                    od += m.ext * fdt;
                }
            }
            prev = t1;
            emit(b, S, exp3(od * -1.0f));
        }
        (void)mu;
    };

    // the eye's horizon on the sphere
    out.eyeDip = float(std::acos(std::clamp(Rg / r0, -1.0, 1.0)));
    // the sky
    out.skyW = skyW;
    out.skyH = skyH;
    out.sky.assign(size_t(skyW) * skyH, make_float4(0, 0, 0, 1));
    const double lmax = M_PI / 2 + out.eyeDip, lmin = M_PI / 2 - out.eyeDip;
    parallelFor(skyH, [&](int y) {
        const double v = (y + 0.5) / skyH;
        const double q = (v - 0.5) * 2.0;
        const double l = q >= 0 ? q * q * lmax : -q * q * lmin;
        const double el = l - out.eyeDip;
        const double ce = std::cos(el), se = std::sin(el);
        std::vector<double> tb(64);
        for (int x = 0; x < skyW; ++x) {
            const double az = ((x + 0.5) / skyW - 0.5) * 2.0 * M_PI;
            const D3 d{ce * std::sin(az), se, -ce * std::cos(az)};
            // to the top of the air, or the planet
            const double dg = r0 * r0 * (se * se - 1.0) + Rg * Rg;
            const double len = (se < 0.0 && dg >= 0.0) ? -r0 * se - std::sqrt(dg) : toSphere(r0, se, Rt);
            // geometric steps: fine near the eye, where the mountains' shadows are near
            for (int k = 0; k < 64; ++k) tb[k] = 200.0 * (std::pow(std::max(len / 200.0, 1.0001), (k + 1) / 64.0) - 1.0) / (1.0 - 200.0 / len) * 1.0;
            tb[63] = len;
            float3 Sf = make_float3(0, 0, 0);
            // (close to the horizon a ray lies long in thin low layers: too few steps show as bands in the sky through the binoculars)
            const int subSky = std::fabs(l) < 0.10 ? 10 : 3;
            gather(d, tb.data(), 64, subSky, [&](int b, float3 S, float3) {
                if (b == 63) Sf = S;
            });
            out.sky[size_t(y) * skyW + x] = make_float4(Sf.x, Sf.y, Sf.z, 1.0f);
        }
    });

    // the froxels: light gathered and let through to each range
    out.frW = frW;
    out.frH = frH;
    out.frD = frD;
    out.froxR0 = 100.0f;
    out.froxLogK = std::log(700000.0f / out.froxR0);
    out.froxElMax = 35.0f * M_PIf / 180.0f;
    out.froxS.assign(size_t(frW) * frH * frD, make_float4(0, 0, 0, 0));
    out.froxT.assign(size_t(frW) * frH * frD, make_float4(1, 1, 1, 1));
    parallelFor(frH, [&](int y) {
        const double v = (y + 0.5) / frH;
        const double q = (v - 0.5) * 2.0;
        const double l = (q >= 0 ? q * q : -q * q) * out.froxElMax;
        const double el = l - out.eyeDip;
        const double ce = std::cos(el), se = std::sin(el);
        std::vector<double> tb(frD);
        for (int k = 0; k < frD; ++k) tb[k] = out.froxR0 * std::exp(double(k) / (frD - 1) * out.froxLogK);
        for (int x = 0; x < frW; ++x) {
            const double az = ((x + 0.5) / frW - 0.5) * 2.0 * M_PI;
            const D3 d{ce * std::sin(az), se, -ce * std::cos(az)};
            gather(d, tb.data(), frD, std::fabs(l) < 0.10 ? 10 : 4, [&](int b, float3 S, float3 T) {
                const size_t i = (size_t(y) * frW + x) * frD + b;
                out.froxS[i] = make_float4(S.x, S.y, S.z, 1.0f);
                out.froxT[i] = make_float4(T.x, T.y, T.z, 1.0f);
            });
        }
    });

    // the sky's irradiance as L1 harmonics: E(n) = sh[0] + sh[1] n.x + sh[2] n.y + sh[3] n.z
    float3 a0 = make_float3(0, 0, 0), ax = a0, ay = a0, az = a0;
    const int H2 = 256, W2 = 512;
    for (int y = 0; y < H2; ++y) {
        const float cy = 1.0f - 2.0f * (y + 0.5f) / H2;
        const float sy = std::sqrt(std::max(1.0f - cy * cy, 0.0f));
        for (int x = 0; x < W2; ++x) {
            const float ph = (x + 0.5f) / W2 * 6.2831853f;
            const float3 d = make_float3(sy * std::cos(ph), cy, sy * std::sin(ph));
            float3 L;
            if (d.y > -std::sin(out.eyeDip)) {
                L = dagSkyDir(out, d);
            } else {
                // the ground below: its own light is small; the sky's light on the mean ground
                L = air.groundAlbedo * 0.25f * dagSkyDir(out, make_float3(0, 1, 0));
            }
            const float w = 4.0f * M_PIf / (H2 * W2);
            a0 += L * (0.282095f * w);
            ax += L * (0.488603f * d.x * w);
            ay += L * (0.488603f * d.y * w);
            az += L * (0.488603f * d.z * w);
        }
    }
    const float A0 = M_PIf * 0.282095f, A1 = 2.0f * M_PIf / 3.0f * 0.488603f;
    out.sh[0] = a0 * A0;
    out.sh[1] = ax * A1;
    out.sh[2] = ay * A1;
    out.sh[3] = az * A1;
    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    (void)h0;
}
