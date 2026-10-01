// Scene 2 "Fener" inside the application: two fixed views across the fog (A "Pencere", B "Yücelik"), the walk between
// them through the forest (Space), and the player (Tab): the islands on foot.
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/fener/fener_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

namespace {

class FenerLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "fener";
        i.number = "02";
        i.title = "Fener";
        i.subtitle = "Siste dönen bir huzme, gece ve orman";
        i.description = "Playdead'in Inside'ı tarzında bir gece: dönen huzmeyi taşıyacak kadar yoğun sis, kara köknar silüetleri, tek sıcak pencere. "
                        "İki sabit bakış arasında 778 metrelik bir orman yolculuğu, gözün karanlığa uyumuyla. Tab ile adalarda yürünebilir.";
        i.tags = {"Gerçek zamanlı hacimsel ışık", "Zamansal birikim", "Oyuncu kipi"};
        i.image = "assets/gallery/fener.jpg";
        i.accent = 0x86B3DA;
        i.keys = {{"Sahne", {{"Boşluk", "A ↔ B yürü (yolda: geri dön)"}, {"B", "Öbür bakışa atla"}, {"P", "Zamanı durdur"}, {"←|→", "Zaman ∓1 sn"},
                             {"↑|↓", "Pozlama"}, {"1–5", "Son, yüzey, hacim, derinlik, normal"}}},
                  {"Oyuncu", {{"Tab", "Sahne → oyuncu → fener adası → sahne"}, {"W|A|S|D", "Yürü"}, {"Fare", "Bak"}, {"Shift", "Koş"}, {"Boşluk", "Zıpla"}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<FenerLive>(); }

    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = "Hacimsel ışık programları derleniyor";
            st.fraction = 0.05f;
            ++st.step;
            return;
        case 1: {
            f_.root = host.root();
            f_.overrides = host.overrides();
            f_.loadConfig();
            f_.renderer.init(f_.width, f_.height, f_.root, unsigned(f_.cfg.i("volume.scale", 1)));
            if (!f_.compile()) { st.error = "Işın izleme programları derlenemedi (ayrıntı konsolda)."; return; }
            st.label = "Adalar, orman ve fener kuruluyor";
            st.fraction = 0.5f;
            ++st.step;
            return;
        }
        case 2:
            f_.loadBlueNoise();
            f_.applyConfig();
            f_.rebuildScene();
            t_ = f_.cfg.f("time", 0.0f);
            st.label = "Sis toplanıyor";
            st.fraction = 0.9f;
            ++st.step;
            return;
        default:
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(f_.width) * f_.height * sizeof(uchar4));
            // the first frames: the eye's adaptation starts where it settles
            f_.setTime(float(t_));
            f_.frame(0);
            f_.adaptExposure(0.0f, true);
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        FenerParams& p = f_.renderer.params();
        const float dt = float(std::min(0.1, in.dt));
        if (in.pressed(GLFW_KEY_P)) { paused_ = !paused_; host.toast(paused_ ? "Zaman durdu" : "Zaman akıyor"); }
        if (in.pressed(GLFW_KEY_RIGHT)) t_ += 1.0;
        if (in.pressed(GLFW_KEY_LEFT)) t_ -= 1.0;
        if (in.pressed(GLFW_KEY_TAB)) {
            f_.mode = (f_.mode + 1) % 3;
            if (f_.mode == 0) {
                host.captureMouse(false);
                f_.applyJourney();
                f_.renderer.resetHistory();
                host.toast("Sahne");
            } else {
                host.captureMouse(true);
                f_.placePlayer(f_.mode);
                host.toast(f_.mode == 1 ? "Oyuncu · WASD yürü, fare bak, Shift koş, Boşluk zıpla" : "Oyuncu · fener adası");
            }
        }
        bool jump = false;
        if (in.pressed(GLFW_KEY_SPACE)) {
            if (f_.mode == 0) {
                f_.journey.target ^= 1;
                host.toast(f_.journey.target ? "Yola çıkış: B · Yücelik" : "Yola çıkış: A · Pencere");
            } else {
                jump = true;
            }
        }
        if (in.pressed(GLFW_KEY_B) && f_.mode == 0) {
            f_.setView(f_.view + 1);
            host.toast(f_.view == 0 ? "Bakış A · Pencere" : "Bakış B · Yücelik");
        }
        for (int v = 0; v < 5; ++v)
            if (in.pressed(GLFW_KEY_1 + v)) p.view = v;
        if (in.pressed(GLFW_KEY_UP)) { f_.ev += 0.25f; p.exposure = std::pow(2.0f, f_.ev); }
        if (in.pressed(GLFW_KEY_DOWN)) { f_.ev -= 0.25f; p.exposure = std::pow(2.0f, f_.ev); }

        if (!paused_) t_ += in.dt;
        f_.setTime(float(t_));
        if (f_.mode == 0 && f_.journey.moving()) {
            f_.journey.step(dt, f_.cfg.f("journey.speed", 12.0f), f_.cfg.f("journey.accel", 1.0f));
            f_.applyJourney();
        }
        if (f_.mode != 0) {
            const float sens = f_.cfg.f("player.mouse", 0.0022f);
            f_.player.yaw += in.mouseDX * sens;
            f_.player.pitch = std::max(-1.45f, std::min(1.45f, f_.player.pitch - in.mouseDY * sens));
            auto key = [&](int k) { return in.isDown(k) ? 1.0f : 0.0f; };
            const float inF = key(GLFW_KEY_W) - key(GLFW_KEY_S), inR = key(GLFW_KEY_D) - key(GLFW_KEY_A);
            f_.stepPlayer(inF, inR, in.shift, jump, std::min(dt, 0.05f));
            f_.applyPlayerCamera();
        }
        f_.frame(k_++);
        f_.adaptExposure(dt, false);
        cudaMemcpy(pinned_, p.output, size_t(f_.width) * f_.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
    }

    const void* image(int& w, int& h) override {
        w = int(f_.width);
        h = int(f_.height);
        return pinned_;
    }

    void status(app::Status& s) override {
        using theme::num;
        const FenerTimes& tm = f_.renderer.times();
        if (f_.mode == 0) s.chips.push_back({"Sahne", true});
        else s.chips.push_back({f_.player.swimming ? "Oyuncu · yüzüyor" : (f_.mode == 1 ? "Oyuncu" : "Oyuncu · fener adası"), true});
        if (f_.mode == 0 && !f_.journey.moving()) s.chips.push_back({f_.view == 0 ? "A · Pencere" : "B · Yücelik"});
        if (paused_) s.chips.push_back({"Zaman durdu", true});
        static const char* views[] = {"", "Yalnız yüzey", "Yalnız hacim", "Derinlik", "Normal"};
        if (f_.renderer.params().view > 0 && f_.renderer.params().view < 5) s.chips.push_back({views[f_.renderer.params().view], true});
        s.metrics.push_back({"Yüzey", num(tm.surface, 1) + " ms"});
        s.metrics.push_back({"Hacim", num(tm.volume + tm.resolve, 1) + " ms"});
        s.metrics.push_back({"Birikim ve ışıltı", num(tm.compose + tm.taa + tm.glow + tm.final, 1) + " ms"});
        s.metrics.push_back({"Göz uyumu", (f_.evAdapt >= 0 ? "+" : "") + num(f_.evAdapt, 2) + " EV"});
        if (f_.mode == 0 && f_.journey.moving()) {
            s.progress = f_.journey.d / std::max(1.0f, f_.journey.length());
            s.progressLabel = std::string("Yolculuk ") + (f_.journey.target ? "A → B" : "B → A") + " · " + num(f_.journey.d, 0) + " / " +
                              num(f_.journey.length(), 0) + " m";
        }
    }

    void screenshot(app::Host& host) override {
        f_.save(host.capturePath());
        host.toast("Ekran görüntüsü kaydedildi");
    }

    void reload(app::Host& host) override {
        f_.loadConfig();
        if (f_.compile()) {
            f_.applyConfig();
            f_.rebuildScene();
            host.toast("Ayarlar ve programlar yeniden yüklendi");
        } else {
            host.toast("Derleme hatası: ayrıntı konsolda");
        }
    }

    void unload() override {
        f_.renderer.shutdown();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }

    ~FenerLive() override { unload(); }

private:
    app::SceneInfo info_ = describe();
    FenerApp f_;
    uchar4* pinned_ = nullptr;
    double t_ = 0.0;
    unsigned int k_ = 0;
    bool paused_ = false;
};

}   // namespace

namespace fener {
app::SceneInfo info() { return FenerLive::describe(); }
std::unique_ptr<app::Scene> make() { return FenerLive::make(); }
int cli(int argc, char** argv) { return runFenerCli(argc, argv); }
}   // namespace fener
