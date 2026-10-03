// Scene 4 "Laboratuvar", the Küp experiment's rigid cubes (cubes.h): measurements, each with its criterion
// (--scene laboratuvar --kup-test [name]): PASS / FAIL.
//   durus    a cube of 8^3 loose cubes, every one awake, stands 2 s: none moves more than a tenth of its edge
//   sekme    a 25 mm cube dropped flat from 10 cm comes back up to e^2 h (e 0.45 on the table: 2 cm)
//   kayma    a 25 mm cube sent sliding at 1 m/s stops in v^2 / (2 mu_d g) (17 cm)
//   yuz      a 25 mm cube dropped tilted and spinning comes to rest on a face (an axis within 2 deg of vertical)
//   hava     a 1 mm cube falling from 3 m: its speed after 0.6 s against v_t tanh(g t / v_t) for its drag
//   patlama  a 16^3 cube's blast: the field carries Gurney's energy, a face's cubes fly faster than a corner's,
//            nothing goes NaN, everything comes to rest or leaves the stage
//   hiz      frames a second for 32^3, 64^3, 100^3: a blast and a gentle collapse (printed, no criterion)
#include "scenes/laboratuvar/cubes.h"
#include "shared/vec.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* verdict(bool ok) { return ok ? "PASS" : "FAIL"; }

double nowS() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// how far a cube's nearest axis is from the vertical (deg)
float tiltDeg(float4 q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    const float3 ay[3] = {make_float3(2 * (x * y + z * w), 0, 0), make_float3(1 - 2 * (x * x + z * z), 0, 0), make_float3(2 * (y * z - x * w), 0, 0)};
    // the world y component of each local axis: R's second row
    const float c = std::fmax(std::fabs(ay[0].x), std::fmax(std::fabs(ay[1].x), std::fabs(ay[2].x)));
    return std::acos(std::fmin(1.0f, c)) * 180.0f / 3.14159265f;
}

int testDurus() {
    CubeWorld w;
    w.build(8, 0.10f, make_float3(0, 0, 0), 1);
    w.wakeAll(make_float3(0, 0, 0), make_float3(0, 0, 0));
    std::vector<float4> x0, q, v, x;
    std::vector<uint32_t> st;
    w.read(x0, q, v, st);
    float worst = 0.0f;
    const double t0 = nowS();
    for (int k = 0; k < 120; ++k) {
        w.step(1.0f / 60.0f);
        if (k % 20 == 19) {
            w.read(x, q, v, st);
            for (size_t i = 0; i < x.size(); ++i) {
                const float3 d = make_float3(x[i].x - x0[i].x, x[i].y - x0[i].y, x[i].z - x0[i].z);
                worst = std::fmax(worst, std::sqrt(dot(d, d)));
            }
        }
    }
    const CubeStats s = w.stats();
    const bool ok = worst < 0.1f * w.edge();
    std::printf("kup durus (8^3, hepsi uyanik, 2 sn): en cok %.3f mm kaydi (kenar %.2f mm), %d uyudu, kare %.2f ms  [olcut: < kenarin 1/10'u] %s\n",
                worst * 1000.0f, w.edge() * 1000.0f, s.asleep, (nowS() - t0) * 1000.0 / 120.0, verdict(ok));
    return ok ? 0 : 1;
}

int testSekme() {
    CubeWorld w;
    const float a = 0.025f, h0 = 0.10f;
    w.build(1, a, make_float3(0, h0, 0), 1);
    w.wakeAll(make_float3(0, 0, 0), make_float3(0, 0, 0));
    std::vector<float4> x, q, v;
    std::vector<uint32_t> st;
    bool hit = false;
    float apex = 0.0f, impact = 0.0f;
    for (int k = 0; k < 600; ++k) {
        w.step(1.0f / 600.0f);
        w.read(x, q, v, st);
        const float bottom = x[0].y - 0.5f * a;
        if (!hit && bottom < 0.002f) {
            hit = true;
            impact = -v[0].y;
        }
        if (hit && v[0].y > 0.0f) apex = std::fmax(apex, bottom);
        if (hit && apex > 0.0f && v[0].y < -0.05f) break;
    }
    const float e = w.params.restitutionFloor, want = e * e * h0;
    const bool ok = apex > 0.7f * want && apex < 1.35f * want;
    std::printf("kup sekme (25 mm, 10 cm'den): carpma %.2f m/s, geri %.1f mm  (e^2 h = %.1f mm)  [olcut: %%70-135] %s\n", impact, apex * 1000.0f, want * 1000.0f,
                verdict(ok));
    return ok ? 0 : 1;
}

int testKayma() {
    CubeWorld w;
    const float a = 0.025f;
    w.build(1, a, make_float3(-0.2f, 0, 0), 1);
    w.wakeAll(make_float3(1.0f, 0, 0), make_float3(0, 0, 0));
    std::vector<float4> x, q, v;
    std::vector<uint32_t> st;
    w.read(x, q, v, st);
    const float xs = x[0].x;
    for (int k = 0; k < 120; ++k) {
        w.step(1.0f / 60.0f);
        w.read(x, q, v, st);
        if (st[0] == 1u || std::fabs(v[0].x) < 1e-3f) break;
    }
    const float dist = x[0].x - xs, want = 1.0f / (2.0f * w.params.muFloorDynamic * w.params.gravity);
    const bool ok = std::fabs(dist - want) < 0.25f * want && tiltDeg(q[0]) < 3.0f;
    std::printf("kup kayma (25 mm, 1 m/s): %.1f cm kaydi, egim %.1f derece  (v^2 / 2 mu g = %.1f cm)  [olcut: +-%%25, devrilmeden] %s\n", dist * 100.0f,
                tiltDeg(q[0]), want * 100.0f, verdict(ok));
    return ok ? 0 : 1;
}

int testYuz() {
    CubeWorld w;
    const float a = 0.025f;
    w.build(1, a, make_float3(0, 0.05f, 0), 1);
    const float ang = 0.9f, s = std::sin(0.5f * ang);
    const float3 ax = normalize(make_float3(1.0f, 0.3f, 0.6f));
    w.setPose(0, make_float3(0, 0.08f, 0), make_float4(ax.x * s, ax.y * s, ax.z * s, std::cos(0.5f * ang)));
    w.wakeAll(make_float3(0.2f, 0, 0.1f), make_float3(6.0f, 2.0f, -4.0f));
    std::vector<float4> x, q, v;
    std::vector<uint32_t> st;
    for (int k = 0; k < 240; ++k) w.step(1.0f / 60.0f);
    w.read(x, q, v, st);
    const float t = tiltDeg(q[0]);
    const float lift = x[0].y - 0.5f * a;
    const bool ok = t < 2.0f && std::fabs(lift) < 0.002f;
    std::printf("kup yuz (egik, donerek): durdugunda egim %.2f derece, tabandan %.2f mm, %s  [olcut: < 2 derece, yuzu masada] %s\n", t, lift * 1000.0f,
                st[0] == 1u ? "uyudu" : "uyanik", verdict(ok));
    return ok ? 0 : 1;
}

int testHava() {
    CubeWorld w;
    const float a = 0.001f;
    w.build(1, a, make_float3(0, 3.0f, 0), 1);
    w.wakeAll(make_float3(0, 0, 0), make_float3(0, 0, 0));
    for (int k = 0; k < 36; ++k) w.step(1.0f / 60.0f);
    std::vector<float4> x, q, v;
    std::vector<uint32_t> st;
    w.read(x, q, v, st);
    const CubeParams& P = w.params;
    const float m = P.density * a * a * a, k = 0.5f * P.airDensity * P.drag * 1.5f * a * a / m;
    const float vt = std::sqrt(P.gravity / k), want = vt * std::tanh(P.gravity * 0.6f / vt);
    const float got = -v[0].y;
    const bool ok = std::fabs(got - want) < 0.05f * want;
    std::printf("kup hava (1 mm, 0,6 sn dusus): %.2f m/s  (v_t %.2f m/s, beklenen %.2f; havasiz %.2f)  [olcut: +-%%5] %s\n", got, vt, want, P.gravity * 0.6f,
                verdict(ok));
    return ok ? 0 : 1;
}

int testHavaDebug() {
    for (int mode = 0; mode < 2; ++mode) {
        CubeWorld w;
        w.build(1, 0.001f, make_float3(0, 3.0f, 0), 1);
        if (mode == 0) w.params.airDensity = 0.0f;
        w.wakeAll(make_float3(0, 0, 0), make_float3(0, 0, 0));
        std::vector<float4> x, q, v;
        std::vector<uint32_t> st;
        for (int k = 0; k < 36; ++k) {
            w.step(1.0f / 60.0f);
            if (k % 6 == 5) {
                w.read(x, q, v, st);
                std::printf("  hava%s t %.2f: y %.4f v %.4f subs %d state %u\n", mode ? "" : "siz", (k + 1) / 60.0, x[0].y, v[0].y, w.lastSubsteps(), st[0]);
            }
        }
    }
    return 0;
}

int testDurusDebug() {
    for (int n : {2, 3, 4, 8}) {
        CubeWorld w;
        w.build(n, 0.0125f * n, make_float3(0, 0, 0), 1);
        w.wakeAll(make_float3(0, 0, 0), make_float3(0, 0, 0));
        std::vector<float4> x0, q, v, x;
        std::vector<uint32_t> st;
        w.read(x0, q, v, st);
        for (int k = 0; k < 60; ++k) {
            w.step(1.0f / 60.0f);
            if (k == 0 || k == 2 || k == 9 || k == 29 || k == 59) {
                w.read(x, q, v, st);
                float worst = 0.0f, vmax = 0.0f;
                int wi = 0;
                for (size_t i = 0; i < x.size(); ++i) {
                    const float3 d = make_float3(x[i].x - x0[i].x, x[i].y - x0[i].y, x[i].z - x0[i].z);
                    const float l = std::sqrt(dot(d, d));
                    if (l > worst) { worst = l; wi = int(i); }
                    vmax = std::fmax(vmax, std::sqrt(v[i].x * v[i].x + v[i].y * v[i].y + v[i].z * v[i].z));
                }
                const int per = n;
                std::printf("  %d^3 kare %d: en cok %.4f mm (kup %d: %d,%d,%d; dy %.4f mm), en hizli %.4f m/s\n", n, k + 1, worst * 1000.0f, wi, wi % per,
                            (wi / per) % per, wi / (per * per), (x[size_t(wi)].y - x0[size_t(wi)].y) * 1000.0f, vmax);
            }
        }
    }
    return 0;
}

// two cubes alone: one on the other (aligned, then turned 30 deg about y), then side by side pushed together
int testIkili() {
    for (int caseNo = 0; caseNo < 3; ++caseNo) {
        CubeWorld w;
        const float a0 = 0.025f;
        w.build(2, 2.0f * a0 * (1.0f + w.params.gap), make_float3(0, 0, 0), 1);
        const float a = w.edge(), h = 0.5f * a;
        for (int k = 2; k < 8; ++k) w.setPose(k, make_float3(0.3f + 0.05f * k, h, 0.2f), make_float4(0, 0, 0, 1));
        if (caseNo == 0) {
            w.setPose(0, make_float3(0, h, 0), make_float4(0, 0, 0, 1));
            w.setPose(1, make_float3(0, 3.0f * h, 0), make_float4(0, 0, 0, 1));
        } else if (caseNo == 1) {
            w.setPose(0, make_float3(0, h, 0), make_float4(0, 0, 0, 1));
            const float an = 0.5236f;
            w.setPose(1, make_float3(0.003f, 3.0f * h + 0.0005f, 0), make_float4(0, std::sin(0.5f * an), 0, std::cos(0.5f * an)));
        } else {
            w.setPose(0, make_float3(-h - 0.0002f, h, 0), make_float4(0, 0, 0, 1));
            w.setPose(1, make_float3(h + 0.0002f, h, 0), make_float4(0, 0, 0, 1));
        }
        w.wakeAll(make_float3(0, 0, 0), make_float3(0, 0, 0));
        if (caseNo == 2) {   // pushed towards each other at 0.3 m/s each: they meet and bounce apart
            std::vector<float4> x, q, v;
            std::vector<uint32_t> st;
            (void)x;
        }
        std::vector<float4> x, q, v;
        std::vector<uint32_t> st;
        const char* name[3] = {"ust uste", "ust uste 30 derece", "yan yana"};
        for (int k = 0; k < 120; ++k) {
            w.step(1.0f / 60.0f);
            if (k == 0 || k == 4 || k == 14 || k == 59 || k == 119) {
                w.read(x, q, v, st);
                std::printf("  %s kare %3d: alt (%.4f %.4f %.4f) v %.4f, ust (%.4f %.4f %.4f) v %.4f, durum %u %u\n", name[caseNo], k + 1, x[0].x, x[0].y, x[0].z,
                            std::sqrt(v[0].x * v[0].x + v[0].y * v[0].y + v[0].z * v[0].z), x[1].x, x[1].y, x[1].z,
                            std::sqrt(v[1].x * v[1].x + v[1].y * v[1].y + v[1].z * v[1].z), st[0], st[1]);
            }
        }
    }
    return 0;
}

int testIkiliDebug() {
    CubeWorld w;
    const float a0 = 0.025f;
    w.build(2, 2.0f * a0 * (1.0f + w.params.gap), make_float3(0, 0, 0), 1);
    const float a = w.edge(), h = 0.5f * a;
    for (int k = 2; k < 8; ++k) w.setPose(k, make_float3(0.3f + 0.05f * k, h, 0.2f), make_float4(0, 0, 0, 1));
    w.setPose(0, make_float3(0, h, 0), make_float4(0, 0, 0, 1));
    const float an = 0.5236f;
    w.setPose(1, make_float3(0.003f, 3.0f * h + 0.0005f, 0), make_float4(0, std::sin(0.5f * an), 0, std::cos(0.5f * an)));
    w.wakeAll(make_float3(0, 0, 0), make_float3(0, 0, 0));
    std::vector<float4> x, q, v;
    std::vector<uint32_t> st;
    for (int k = 0; k < 400; ++k) {
        w.step(1.0f / (60.0f * w.params.substeps));
        if (k % 20 == 0 || (k > 60 && k < 70)) {
            w.read(x, q, v, st);
            std::printf("  alt adim %3d: alt v (%.5f %.5f %.5f) ust v (%.5f %.5f %.5f) ust y %.6f\n", k, v[0].x, v[0].y, v[0].z, v[1].x, v[1].y, v[1].z, x[1].y);
        }
    }
    return 0;
}

int testPatlama() {
    CubeWorld w;
    w.build(16, 0.10f, make_float3(0, 0, -0.02f), 3);
    CubeBlast b;
    b.centre = w.centre();
    b.charge = 2e-3f;
    CubeBlastReport rep;
    w.blast(b, &rep);
    std::vector<float4> x, q, v;
    std::vector<uint32_t> st;
    bool finite = true;
    for (int k = 0; k < 180; ++k) {
        w.step(1.0f / 60.0f);
        if (k % 30 == 29) {
            w.read(x, q, v, st);
            for (size_t i = 0; i < x.size(); ++i) finite = finite && std::isfinite(x[i].x + x[i].y + x[i].z + q[i].x + q[i].w + v[i].x);
        }
    }
    const CubeStats s = w.stats();
    const bool energyOk = rep.energy > 0.55 * rep.gurneyEnergy && rep.energy < 1.1 * rep.gurneyEnergy;
    const bool shapeOk = rep.faceSpeed > 1.5f * rep.cornerSpeed;
    const bool ok = energyOk && shapeOk && finite && s.awake < s.asleep + s.dead + 1;
    std::printf("kup patlama (16^3, %.1f mg TNT): Gurney %.2f J, kuplerde %.2f J (masadan yansiyan yari dahil), yuz %.2f m/s, kose %.2f m/s, ort %.2f, en hizli "
                "%.2f m/s; 3 sn sonra uyanik %d, uyuyan %d, giden %d%s  [olcut: enerji %%55-110, yuz > 1,5 kose, sayi, durulur] %s\n",
                b.charge * 1000.0f, rep.gurneyEnergy, rep.energy, rep.faceSpeed, rep.cornerSpeed, rep.meanSpeed, rep.maxSpeed, s.awake, s.asleep, s.dead,
                finite ? "" : ", NaN!", verdict(ok));
    return ok ? 0 : 1;
}

void testHizDebug() {
    for (int n : {16, 32}) {
        CubeWorld w;
        w.build(n, 0.10f, make_float3(0, 0, -0.02f), 5);
        CubeBlast b;
        b.centre = w.centre();
        b.charge = 0.05f;
        CubeBlastReport rep;
        w.blast(b, &rep);
        std::printf("  %d^3: en hizli %.1f m/s\n", n, rep.maxSpeed);
        for (int k = 0; k < 6; ++k) {
            w.step(1.0f / 60.0f);
            const CubeStats s = w.stats();
            std::printf("  %d^3 kare %d: %.1f ms, %d alt adim, uyanik %d, giden %d, en hizli %.1f\n", n, k, w.lastStepMs(), w.lastSubsteps(), s.awake, s.dead, s.maxSpeed);
        }
    }
}

void testProfil() {
    CubeWorld w;
    w.params.substeps = 20;
    w.params.passes = 2;
    w.build(16, 0.10f, make_float3(0, 0, -0.02f), 7);
    CubeBlast b;
    b.centre = w.centre();
    b.charge = 0.000248f;
    w.blast(b);
    for (int k = 0; k < 150; ++k) {
        w.step(1.0f / 60.0f);
        if (k % 10 == 0) {
            int t = 0, p = 0;
            w.pairStats(t, p);
            const CubeStats s = w.stats();
            std::printf("  kare %3d: %.1f ms, uyanik %d, uyuyan %d, giden %d, degen %d kup, %d cift, en hizli %.2f\n", k, w.lastStepMs(), s.awake, s.asleep, s.dead, t, p,
                        s.maxSpeed);
        }
    }
}

void testHiz() {
    for (int n : {32, 64, 100}) {
        for (int gentle = 0; gentle < 2; ++gentle) {
            CubeWorld w;
            w.build(n, 0.10f, make_float3(0, 0, -0.02f), 5);
            CubeBlast b;
            b.centre = w.centre();
            b.charge = gentle ? 1e-6f : 0.05f;
            w.blast(b);
            double ms = 0.0;
            int subs = 0;
            const int frames = 30;
            for (int k = 0; k < frames; ++k) {
                w.step(1.0f / 60.0f);
                ms += w.lastStepMs();
                subs += w.lastSubsteps();
            }
            const CubeStats s = w.stats();
            std::printf("kup hiz %d^3 = %d kup, %s: kare %.1f ms (%.0f fps), karede %.0f alt adim; uyanik %d\n", n, w.count(), gentle ? "hafif (cokme)" : "50 mg",
                        ms / frames, 1000.0 * frames / ms, double(subs) / frames, s.awake);
        }
    }
}

}   // namespace

int runCubeTests(const std::string& only) {
    int fails = 0, ran = 0;
    auto run = [&](const char* name, int (*f)()) {
        if (only.empty() || only == name) {
            ++ran;
            fails += f();
        }
    };
    run("durus", testDurus);
    run("sekme", testSekme);
    run("kayma", testKayma);
    run("yuz", testYuz);
    run("hava", testHava);
    run("patlama", testPatlama);
    if (only == "havad") return testHavaDebug();
    if (only == "durusd") return testDurusDebug();
    if (only == "ikili") return testIkili();
    if (only == "ikilid") return testIkiliDebug();
    if (only == "profil") {
        testProfil();
        return 0;
    }
    if (only == "hizd") {
        testHizDebug();
        return 0;
    }
    if (only == "hiz") {
        testHiz();
        return 0;
    }
    if (!ran) {
        std::printf("bilinmeyen test: %s (durus, sekme, kayma, yuz, hava, patlama, hiz)\n", only.c_str());
        return 2;
    }
    std::printf("kup testleri: %d test, %d basarisiz\n", ran, fails);
    return fails ? 1 : 0;
}
