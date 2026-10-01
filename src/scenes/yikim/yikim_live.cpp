// Scene 3 "Yikim" inside the application: a reinforced-concrete apartment block, earthquakes of magnitude 3 to 9,
// explosive charges under the mouse. The physics steps on its own stream while the picture of the previous step is
// drawn (one step a frame at most, on a 60 Hz tick: when a step costs more, the world slows a little instead of the
// frame rate collapsing).
#include "app/scene.h"
#include "app/theme.h"
#include "core/image_io.h"
#include "scenes/yikim/fly_camera.h"
#include "scenes/yikim/yikim_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

namespace {

class YikimLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "yikim";
        i.number = "03";
        i.title = "Yıkım";
        i.subtitle = "Betonarme bir apartman, deprem ve kırılma";
        i.description = "Beş katlı bir apartman: 3.007 parça, 44.378 lifli bağ. Beton çatlar ve ezilir, donatı akar ve kopar; çözücü ekran kartında, "
                        "her karede kırk alt adım. 3'ten 9'a kadar her büyüklük gerçek bir kayıttan türetildi; fareyle kolon patlatılabilir.";
        i.tags = {"Kendi fizik çözücüsü", "Gerçek deprem kayıtları", "Canlı"};
        i.image = "assets/gallery/yikim.jpg";
        i.accent = 0xE5764A;
        i.keys = {{"Deprem", {{"3–9", "Büyüklüğü seç"}, {"Boşluk", "Depremi başlat / durdur"}, {"T", "Ağır çekim: ×1, ×0,25, ×0,1"}, {"P", "Zamanı durdur"}}},
                  {"Müdahale", {{"Sol tık", "Tıklanan yeri patlat"}, {"Ctrl|Sol tık", "Büyük patlama"}, {"R", "Binayı baştan kur"}}},
                  {"Kamera", {{"Sağ tık|Fare", "Bak"}, {"W|A|S|D", "Uç"}, {"E|Q", "Yukarı / aşağı"}, {"Shift", "Hızlı"}, {"Tekerlek", "Yakınlaş"}}},
                  {"Görüntü", {{"B", "Donatıyı göster / gizle"}, {"G", "Gürültü giderici: süzgeç, yapay zekâ, ham"}}}};
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<YikimLive>(); }

    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0: {
            st.label = "Ekran kartı ve ışın izleme programları hazırlanıyor";
            st.fraction = 0.05f;
            ++st.step;
            return;
        }
        case 1: {
            y_.root = host.root();
            y_.overrides = host.overrides();
            y_.loadConfig();
            y_.scenario = "none";
            y_.width = unsigned(y_.cfg.i("display.width", 1920));
            y_.height = unsigned(y_.cfg.i("display.height", 1080));
            y_.renderer.init(y_.width, y_.height, y_.root);
            std::string log;
            if (!y_.renderer.compileDevice(log)) { st.error = "Işın izleme programı derlenemedi.\n" + log; return; }
            st.label = "Bina kuruluyor ve kendi ağırlığıyla oturuyor";
            st.fraction = 0.3f;
            ++st.step;
            return;
        }
        case 2: {
            y_.build();
            st.label = "Görüntü hazırlanıyor";
            st.fraction = 0.85f;
            ++st.step;
            return;
        }
        default: {
            y_.renderer.uploadWorld(y_.world);
            y_.applyRender();
            y_.setCamera();
            YCamera& cam = y_.renderer.params().cam;
            fly_.lookAt(cam.pos, cam.pos + cam.fwd);
            fly_.fovDeg = y_.cfg.f("camera.fov", 50.0f);
            fly_.apply(cam, y_.width, y_.height);
            spp_ = unsigned(y_.cfg.i("display.spp", 6));
            y_.renderer.denoiseMode = y_.cfg.i("display.denoise", 1);
            y_.renderer.filterPasses(y_.cfg.i("display.filter_passes", 4));
            magnitude_ = std::min(std::max(y_.cfg.i("deprem.magnitude", 6), quakeMagnitudeMin()), quakeMagnitudeMax());
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(y_.width) * y_.height * sizeof(uchar4));
            stats_ = y_.world.stats();
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        if (rebuildIn_ > 0 && --rebuildIn_ == 0) {   // shown first ("kuruluyor"), done the frame after
            y_.build();
            y_.renderer.uploadWorld(y_.world);
            camMoved_ = true;
            host.toast("Bina yeniden kuruldu");
        }
        // keys
        if (in.pressed(GLFW_KEY_G)) {
            y_.renderer.denoiseMode = (y_.renderer.denoiseMode + 1) % 3;
            host.toast(y_.renderer.denoiseMode == 0 ? "Gürültü giderici kapalı: ham görüntü"
                       : y_.renderer.denoiseMode == 1 ? "Gürültü giderici: hızlı süzgeç"
                                                      : "Gürültü giderici: yapay zekâ (en temiz, fiziği yavaşlatır)");
        }
        if (in.pressed(GLFW_KEY_P)) { paused_ = !paused_; host.toast(paused_ ? "Zaman durdu" : "Zaman akıyor"); }
        if (in.pressed(GLFW_KEY_B)) {
            y_.renderer.showBars = !y_.renderer.showBars;
            camMoved_ = true;
            host.toast(y_.renderer.showBars ? "Donatı görünüyor" : "Donatı gizli");
        }
        if (in.pressed(GLFW_KEY_T)) {
            speedIdx_ = (speedIdx_ + 1) % 3;
            host.toast(speedIdx_ == 0 ? "Gerçek hız" : speedIdx_ == 1 ? "Ağır çekim ×0,25" : "Ağır çekim ×0,1");
        }
        for (int m = quakeMagnitudeMin(); m <= quakeMagnitudeMax(); ++m)
            if (in.pressed(GLFW_KEY_0 + m) || in.pressed(GLFW_KEY_KP_0 + m)) {
                magnitude_ = m;
                if (y_.quakeOn) y_.stopQuake();
                const QuakeMagnitude* q = quakeMagnitude(m);
                host.toast("Büyüklük " + std::to_string(m) + " · " + q->label + " — Boşluk ile başlat");
            }
        if (in.pressed(GLFW_KEY_SPACE)) {
            if (y_.quakeOn) { y_.stopQuake(); host.toast("Deprem durduruldu"); }
            else if (y_.startQuake(magnitude_, y_.world.time)) host.toast("Deprem: büyüklük " + std::to_string(magnitude_) + " · " + quakeMagnitude(magnitude_)->label);
        }
        if (in.pressed(GLFW_KEY_R) && rebuildIn_ == 0) {
            rebuildIn_ = 2;
            host.toast("Bina yeniden kuruluyor…");
        }
        // mouse: right button looks, left button blasts, the wheel zooms
        if (in.rightDown && looking_ && (in.mouseDX != 0.0f || in.mouseDY != 0.0f)) {
            fly_.look(in.mouseDX, in.mouseDY, y_.cfg.f("camera.mouse", 0.0025f));
            camMoved_ = true;
        }
        looking_ = in.rightDown;
        if (in.scroll != 0.0f) { fly_.fovDeg = std::max(15.0f, std::min(90.0f, fly_.fovDeg - in.scroll * 3.0f)); camMoved_ = true; }
        const float realDt = float(std::min(0.1, in.dt));
        {   // WASD + E/Q, Shift faster
            auto key = [&](int k) { return in.isDown(k) ? 1.0f : 0.0f; };
            const float inF = key(GLFW_KEY_W) - key(GLFW_KEY_S), inR = key(GLFW_KEY_D) - key(GLFW_KEY_A), inU = key(GLFW_KEY_E) - key(GLFW_KEY_Q);
            if (inF != 0.0f || inR != 0.0f || inU != 0.0f) {
                fly_.move(inF, inR, inU, (in.shift ? 24.0f : 7.0f) * realDt);
                camMoved_ = true;
            }
        }
        fly_.apply(y_.renderer.params().cam, y_.width, y_.height);
        if (in.leftClicked && in.view.w > 0.0f) {   // the cursor in render pixels
            const float rx = (in.mouseX - in.view.x) / in.view.w * y_.width, ry = (in.mouseY - in.view.y) / in.view.h * y_.height;
            if (rx >= 0 && ry >= 0 && rx < y_.width && ry < y_.height) {
                float4 hit;
                const int body = y_.renderer.pick(rx, ry, hit);
                if (body > 0) {
                    const int n = y_.blast(make_float3(hit.x, hit.y, hit.z), in.ctrl ? 1.3f : 0.55f);
                    host.toast(std::to_string(n) + " parça patlatıldı");
                }
            }
        }
        // draw the current state while the GPU computes the next one: the transforms are read first (an event), the
        // step waits for them, the render runs on its own stream meanwhile
        if (++frameNo_ % 10 == 0) y_.renderer.setDamage(y_.world.damage());
        y_.renderer.updateWorld(y_.world);
        {
            YikimParams& P = y_.renderer.params();
            const double3 g = y_.world.groundPos();
            P.groundY = float(g.y);
            P.groundShift = make_float2(float(g.x), float(g.z));
        }
        bool stepped = false;
        if (!paused_) {
            tick_ += realDt;
            if (tick_ >= 1.0 / 60.0) {
                tick_ = std::min(tick_ - 1.0 / 60.0, 1.0 / 60.0);   // no backlog
                const double dt = kSpeeds[speedIdx_] / 60.0;
                y_.driveQuake(y_.world.time + dt);
                y_.world.stepBegin(float(dt), y_.renderer.transformsDone());
                stepped = true;
                camMoved_ = true;
            }
        }
        // a still world under a still camera gathers samples; anything moving starts afresh
        y_.renderer.render(camMoved_ ? spp_ : spp_ * 2, !camMoved_ && y_.renderer.params().samples < 4096);
        camMoved_ = false;
        y_.renderer.downloadTo(pinned_);
        if (stepped) y_.world.stepEnd();
        if (in.now - lastStats_ > 0.5) { stats_ = y_.world.stats(); lastStats_ = in.now; }
    }

    const void* image(int& w, int& h) override {
        w = int(y_.width);
        h = int(y_.height);
        return pinned_;
    }

    void status(app::Status& s) override {
        using theme::num;
        if (y_.quakeOn) s.chips.push_back({"Deprem sürüyor", true});
        if (paused_) s.chips.push_back({"Zaman durdu", true});
        if (speedIdx_) s.chips.push_back({speedIdx_ == 1 ? "Ağır çekim ×0,25" : "Ağır çekim ×0,1", true});
        if (y_.renderer.denoiseMode != 1) s.chips.push_back({y_.renderer.denoiseMode == 0 ? "Ham görüntü" : "Yapay zekâ gürültü giderici"});
        if (!y_.renderer.showBars) s.chips.push_back({"Donatı gizli"});
        s.metrics.push_back({"Fizik", num(y_.world.lastMs, 1) + " ms"});
        s.metrics.push_back({"Görüntü", num(y_.renderer.lastMs, 1) + " ms"});
        s.metrics.push_back({"Kırık bağ lifi", std::to_string(stats_.broken) + " / " + std::to_string(stats_.fibres)});
        s.metrics.push_back({"Zaman", num(y_.world.time, 1) + " sn"});
        s.selector.title = "Büyüklük";
        for (int m = quakeMagnitudeMin(); m <= quakeMagnitudeMax(); ++m) s.selector.items.push_back(std::to_string(m));
        s.selector.active = magnitude_ - quakeMagnitudeMin();
        if (const QuakeMagnitude* q = quakeMagnitude(magnitude_))
            s.selector.detail = std::string(q->label) + " · tepe ivme " + num(q->pgaG, q->pgaG < 0.1 ? 3 : 2) + " g · " + num(q->window, 0) +
                                " sn güçlü sarsıntı" + (y_.quakeOn ? "" : " · Boşluk ile başlat");
        if (y_.quakeOn) {
            const double p = y_.quakeProgress(y_.world.time);
            s.progress = float(p);
            s.progressLabel = "Sarsıntı · büyüklük " + std::to_string(y_.quakeMag) + " · " + num(p * y_.quake.duration(), 1) + " / " +
                              num(y_.quake.duration(), 0) + " sn";
        }
    }

    void screenshot(app::Host& host) override {
        const std::string path = host.capturePath() + ".png";
        writePNG(path, int(y_.width), int(y_.height), reinterpret_cast<const unsigned char*>(pinned_));
        host.toast("Ekran görüntüsü kaydedildi");
    }

    void unload() override {
        y_.renderer.shutdown();
        y_.world.clear();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }

    ~YikimLive() override { unload(); }

private:
    app::SceneInfo info_ = describe();
    YikimApp y_;
    FlyCamera fly_;
    uchar4* pinned_ = nullptr;
    unsigned spp_ = 6;
    int magnitude_ = 6, speedIdx_ = 0, frameNo_ = 0, rebuildIn_ = 0;
    bool paused_ = false, looking_ = false, camMoved_ = true;
    double tick_ = 0.0, lastStats_ = -1.0;
    PhysStats stats_;
    static constexpr float kSpeeds[3] = {1.0f, 0.25f, 0.1f};
};

}   // namespace

namespace yikim {
app::SceneInfo info() { return YikimLive::describe(); }
std::unique_ptr<app::Scene> make() { return YikimLive::make(); }
int cli(int argc, char** argv) { return runYikimCli(argc, argv); }
}   // namespace yikim
