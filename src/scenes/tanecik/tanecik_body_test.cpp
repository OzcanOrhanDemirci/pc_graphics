// Scene 4 "Tanecik": the body in the solver (T2) and shots at it (T4), on a hidden stand (the pelvis held, as a shop
// mannequin's pole holds it, until the balance comes), the muscles holding the rest pose:
//   --scene tanecik --body-test        3 s standing: the flesh settles under gravity on its bones; nothing tears, nothing
//                                      falls through the floor, the flesh comes to rest
//   --scene tanecik --body-clip dur|tabanca|tufek|pompali [--out path] [--set ...]   a clip; the shot at 0.5 s, then
//                                      slow motion (x0.1) for 0.6 s of its time, then real time
#include "scenes/tanecik/balance.h"
#include "scenes/tanecik/body_hits.h"
#include "scenes/tanecik/body_sim.h"
#include "scenes/tanecik/body_world.h"
#include "scenes/tanecik/tanecik_app.h"
#include "core/image_io.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

struct BodyRun {
    TanecikApp& app;
    SkeletonSim skel;
    BalanceController muscles;
    BodySim body;
    int sub = 20, iters = 4;
    explicit BodyRun(TanecikApp& a) : app(a) {}
    // the body built into the solver (the body and graph reordered), the skin bound again, the skeleton on its stand
    bool build() {
        app.build();
        if (!body.build(app.body, app.graph, app.cfg)) return false;
        app.skinBind = bindSkin(app.skin, app.body, app.graph);
        const float s = app.body.spacing;
        if (!skel.init(app.graph, app.body, app.cfg.f("beden.yogunluk", 1000.0f) * s * s * s, true)) return false;
        SkelGround gr;
        gr.radius = 0.5f * s;
        skel.setGround(app.graph, app.body, gr, 6);
        if (app.cfg.i("beden.ayak", 1)) skel.pin(B_PELVIS, true);   // the hidden stand
        muscles.init(skel);
        muscles.ankleStrategy = false;
        sub = app.cfg.i("iskelet.alt_adim", 20);
        iters = app.cfg.i("iskelet.yineleme", 4);
        return true;
    }
    // dt of world time: the skeleton, then the flesh driven by it
    void step(float dt) {
        std::vector<SkelBody> b;
        skel.readBodies(b);
        muscles.update(b, dt);
        muscles.apply(skel);
        const float h = dt / sub;
        for (int k = 0; k < sub; ++k) skel.step(h, make_float3(0, -9.81f, 0), iters, app.cfg.f("iskelet.surtunme", 2.0f), nullptr);
        Xform xf[B_COUNT];
        skel.transforms(xf);
        body.drive(xf, dt);
        body.step(dt);
    }
};

// the renderer fed from the solver: particles, liquid, skin, bones
struct Feed {
    float* dTint = nullptr;
    ~Feed() {
        if (dTint) cudaFree(dTint);
    }
    void operator()(TanecikApp& app, BodyRun& r) {
        const int n = r.body.world.count();
        if (!dTint) cudaMalloc(reinterpret_cast<void**>(&dTint), size_t(n) * sizeof(float));
        r.body.world.exportPositions(app.dPos);
        r.body.world.exportDisplay(dTint);
        cudaDeviceSynchronize();
        Xform xf[B_COUNT];
        r.skel.transforms(xf);
        app.renderer.setBoneXforms(xf);
        app.renderer.deformSkin(app.dPos, dTint);
        app.renderer.updateParticles(app.dPos, dTint);
        app.renderer.updateLiquid(app.dPos, dTint, n, 0.5f * app.body.spacing);
    }
};

weapons::Projectile projectileOf(const std::string& name, float power) {
    using weapons::Gun;
    const Gun g = name == "tufek" ? Gun::Rifle : name == "pompali" ? Gun::Shotgun : Gun::Pistol;
    return weapons::gunLoad(g, power).shot;
}

}   // namespace

int runBodyTests(TanecikApp& app, const std::string&) {
    BodyRun r(app);
    if (!r.build()) { std::printf("beden kurulamadi\n"); return 1; }
    std::printf("beden cozucude: %d tanecik (%d kemik, kinematik), %d bag\n", r.body.world.count(), r.body.boneParticles(), r.body.world.stats().bonds);
    // what shows is how far a particle moves, not how fast it shakes (a residual of the bonds' solve: high frequency,
    // sub-millimetre): the largest displacement over the last half second, and the mean
    std::vector<float3> x, v, xEarlier;
    for (int f = 0; f < 180; ++f) {
        r.step(1.0f / 60.0f);
        if (f == 149) r.body.world.readPositions(xEarlier);
    }
    const PStats st = r.body.world.stats();
    r.body.world.readPositions(x);
    float dmax = 0.0f;
    double dmean = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const float3 dd = x[i] - xEarlier[i];
        const float l = std::sqrt(dot(dd, dd));
        dmax = std::fmax(dmax, l);
        dmean += l;
    }
    dmean /= std::max<size_t>(1, x.size());
    r.body.world.readVelocities(v);
    float vmax = 0.0f, ymin = 1e9f;
    double vmean = 0.0;
    for (size_t i = 0; i < v.size(); ++i) {
        const float s = std::sqrt(dot(v[i], v[i]));
        vmax = std::fmax(vmax, s);
        vmean += s;
        ymin = std::fmin(ymin, x[i].y);
    }
    vmean /= std::max<size_t>(1, v.size());
    {   // the fastest few: where they are, what they are (a diagnosis)
        std::vector<size_t> ord(v.size());
        for (size_t i = 0; i < ord.size(); ++i) ord[i] = i;
        std::partial_sort(ord.begin(), ord.begin() + 8, ord.end(), [&](size_t a, size_t b) { return dot(v[a], v[a]) > dot(v[b], v[b]); });
        int over = 0;
        for (size_t i = 0; i < v.size(); ++i) over += dot(v[i], v[i]) > 0.05f * 0.05f;
        std::printf("  0,05 m/s'den hizli: %d tanecik; en hizlilar:\n", over);
        for (int k = 0; k < 8; ++k) {
            const size_t i = ord[size_t(k)];
            std::printf("    %zu: %.3f m/s, %s, tur %d, (%.3f %.3f %.3f)\n", i, std::sqrt(dot(v[i], v[i])), boneInfo(app.graph.bone[i]).name, int(app.graph.kind[i]),
                        x[i].x, x[i].y, x[i].z);
        }
    }
    const bool ok = st.broken <= int(0.0001 * st.bonds) && dmax < 0.005f && dmean < 0.001 && ymin > 0.0f;   // sub-mm shake: unseen
    std::printf("3 sn sonra: kopuk bag %d (on binde birden az), son 0,5 sn'de en cok %.2f mm kaydi (< 5), ortalama %.3f mm (< 1); en hizli tanecik %.3f "
                "m/s, ortalama %.4f m/s; en alcak tanecik %.4f m (> 0); adim %.2f ms\n",
                st.broken, dmax * 1000.0f, dmean * 1000.0, vmax, vmean, ymin, st.ms);
    std::printf("  -> beden: %s\n", ok ? "GECTI" : "KALDI");
    return ok ? 0 : 1;
}

int runBodyClip(TanecikApp& app, const std::string& name, const std::string& outPrefix) {
    app.width = unsigned(app.cfg.i("test.klip.genislik", 1280));
    app.height = unsigned(app.cfg.i("test.klip.yukseklik", 720));
    app.renderer.init(app.width, app.height, app.root);
    std::string log;
    if (!app.renderer.compileDevice(log)) { std::printf("DERLEME HATASI\n%s\n", log.c_str()); return 1; }
    BodyWorld world(app);
    if (!world.build()) return 1;
    app.upload();
    app.applyLook(app.cfg.i("gorunum.cam", 1) != 0);
    app.cfg.set("kamera.yaw_deg", app.cfg.values.count("klip.yaw") ? app.cfg.values["klip.yaw"] : "35");
    app.cfg.set("kamera.uzaklik", app.cfg.values.count("klip.uzaklik") ? app.cfg.values["klip.uzaklik"] : "2.4");
    app.cfg.set("kamera.hedef", app.cfg.values.count("klip.hedef") ? app.cfg.values["klip.hedef"] : "0 1.1 0");
    app.setCamera();
    std::filesystem::create_directories(outPrefix);
    const unsigned spp = unsigned(app.cfg.i("test.klip.spp", 24));
    const float power = app.cfg.f("klip.guc", 0.5f);
    // the timeline: settle 0.5 s, the shot, slow motion (x0.1) for klip.agir s of its time, then klip.sonra s real time
    const float shotAt = 0.5f, slowFor = app.cfg.f("klip.agir", 0.6f), slow = 0.1f, after = app.cfg.f("klip.sonra", 1.5f);
    double t = 0.0;
    bool shot = name == "dur";
    int frame = 0;
    const double total = shotAt + slowFor + after;
    const weapons::Gun gun = name == "tufek" ? weapons::Gun::Rifle : name == "pompali" ? weapons::Gun::Shotgun : weapons::Gun::Pistol;
    while (t < total) {
        const bool inSlow = shot && t >= shotAt && t < shotAt + slowFor && name != "dur";
        const float dt = (1.0f / 30.0f) * (inSlow ? slow : 1.0f);
        if (!shot && t >= shotAt) {
            const float3 origin = app.cfg.v3("klip.namlu", make_float3(1.2f, 1.25f, 2.6f)), aim = app.cfg.v3("klip.hedef_nokta", make_float3(0.0f, 1.25f, 0.06f));
            const HitReport h = world.shoot(gun, power, origin, aim);
            std::printf("atis %s (guc %.2f): yol uzerinde %d tanecik, boslukla itilen %d, kopan bag %d, momentum %.3f / %.3f N s, doku %.1f cm, %s\n",
                        name.c_str(), power, h.crushed, h.thrown, h.bondsBroken, h.pApplied, h.pDeposited, h.tissue * 100.0f,
                        h.exited ? "cikti" : "icinde kaldi");
            shot = true;
        }
        world.step(dt);
        t += dt;
        world.feed();
        for (unsigned k = 0; k < spp; k += 8) app.renderer.render(8, k > 0);
        std::vector<uchar4> px;
        app.renderer.download(px);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/f_%05d.png", outPrefix.c_str(), frame++);
        writePNG(path, int(app.width), int(app.height), reinterpret_cast<const unsigned char*>(px.data()));
        if (frame % 30 == 0) std::printf("  %.2f s: kan %d tanecik\n", t, world.liquidCount());
    }
    const std::string mp4 = outPrefix + ".mp4";
    const std::string cmd = "ffmpeg -loglevel error -y -framerate 30 -i \"" + outPrefix + "/f_%05d.png\" -c:v libx264 -pix_fmt yuv420p -crf 18 \"" + mp4 + "\"";
    const int rc = std::system(cmd.c_str());
    std::printf("%d kare; %s %s\n", frame, rc == 0 ? "video:" : "VIDEO YAPILAMADI:", mp4.c_str());
    app.release();
    app.renderer.shutdown();
    return 0;
}
