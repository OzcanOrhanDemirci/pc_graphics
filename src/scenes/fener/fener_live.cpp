// Scene 2 "Fener" inside the application: two fixed views across the fog (A "Pencere", B "Yücelik"), the walk between
// them through the forest (Space), and the player (Tab): the islands on foot.
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/fener/fener_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

namespace {

using app::L;

class FenerLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "fener";
        i.number = "02";
        i.title = L("Fener", "Lighthouse");
        i.subtitle = L("Siste dönen bir huzme, gece ve orman", "A beam turning in the fog, night and forest");
        i.description = L("Sisli bir gece: siste dönen bir fener huzmesi, kara köknarlar ve tek sıcak pencere. İki bakış arasında orman "
                          "içinden yürünür; istersen adalarda kendin dolaşırsın.",
                          "A foggy night: a lighthouse beam turning in the fog, black firs and a single warm window. A walk through the "
                          "forest joins two views; you can also roam the islands yourself.");
        i.details = {
            {L("Hacimsel ışık", "Volumetric light"),
             L("Sis gerçek bir ortam olarak hesaplanır: her pikselde ışın sisin içinde yürür, fenerin ve ayın ışığını gölgeleriyle "
               "toplar. Huzme bu yüzden ağaçların ve kulenin ardında kesilir.",
               "The fog is computed as a real medium: in every pixel a ray marches through it and gathers the light of the "
               "lighthouse and the moon with their shadows, which is why the beam is cut off behind the trees and the tower.")},
            {L("Gerçek zamanlı", "Real time"),
             L("1440p'de kare başına 7,6 ms: yüzeyler, tam çözünürlükte hacim, zamansal birikim, TAA ve parlama. Az örnek, mavi gürültü "
               "ve önceki karelerin birikimiyle temiz kalır.",
               "7.6 ms a frame at 1440p: surfaces, the volume at full resolution, temporal accumulation, TAA and glow. Few samples, "
               "blue noise and the frames before keep it clean.")},
            {L("Dünya", "The world"),
             L("İki ada yükseklik alanıyla; deniz kırk dalganın toplamından analitik bir yüzey. Orman altı çeşit köknarın yaklaşık 32 "
               "bin örneği; ışın izleme donanımı yüz milyonlarca üçgeni örnekleyerek taşır.",
               "Two islands as height fields; the sea an analytic surface summed from forty waves. The forest is about 32 thousand "
               "instances of six kinds of fir: the ray-tracing hardware carries hundreds of millions of triangles by instancing.")},
            {L("Sanat kararları", "Art decisions"),
             L("Gerçek siste huzmenin ileri saçılımı yanına göre ~100 kat güçlüdür; kamera huzmenin içindeyken bütün sis düz beyaza "
               "döner. Oran 5'e indirildi. Göz, ormanın karanlığına yarı yolda uyar.",
               "In real fog the beam scatters ~100 times more forwards than sideways, and with the camera inside the beam all the "
               "fog turns flat white. The ratio was brought down to 5. The eye adapts halfway to the darkness of the forest.")},
            {L("Yolculuk ve oyuncu", "The walk and the player"),
             L("Boşluk kamerayı iki bakış arasında 778 m orman içinden yürütür. Tab birinci şahıs kipine geçer: yer yüksekliği "
               "adaların kendisinden, ağaçlar ve kule engel, derin suda yüzülür.",
               "Space walks the camera 778 m through the forest between the two views. Tab switches to first person: the ground "
               "is the islands' own height, trees and the tower are in the way, and deep water is swum.")},
            {L("Görüntü", "The picture"),
             L("Renk RGB'de (tayfsal değil), ACES ton eşleme ve soğuk gölgelerle; 8 bit panelde bantlanmasın diye mavi gürültüyle "
               "titreştirilir.",
               "Colour in RGB (not spectral), ACES tone mapping and cold shadows; dithered with blue noise so that an 8-bit panel "
               "shows no banding.")}};
        i.tags = {L("Gerçek zamanlı hacimsel ışık", "Real-time volumetric light"), L("Zamansal birikim", "Temporal accumulation"),
                  L("Oyuncu kipi", "Player mode")};
        i.image = "assets/gallery/fener.jpg";
        i.accent = 0x86B3DA;
        i.keys = {{L("Sahne", "Scene"),
                   {{L("Boşluk", "Space"), L("A ↔ B yürü (yolda: geri dön)", "Walk A ↔ B (on the way: turn back)")},
                    {"B", L("Öbür bakışa atla", "Jump to the other view")}, {"P", L("Zamanı durdur", "Pause time")},
                    {"←|→", L("Zaman ∓1 sn", "Time ∓1 s")}, {"↑|↓", L("Pozlama", "Exposure")},
                    {"1–5", L("Son, yüzey, hacim, derinlik, normal", "Final, surface, volume, depth, normal")}}},
                  {L("Oyuncu", "Player"),
                   {{"Tab", L("Sahne → oyuncu → fener adası → sahne", "Scene → player → lighthouse island → scene")},
                    {"W|A|S|D", L("Yürü", "Walk")}, {L("Fare", "Mouse"), L("Bak", "Look")}, {"Shift", L("Koş", "Run")},
                    {L("Boşluk", "Space"), L("Zıpla", "Jump")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<FenerLive>(); }

    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Hacimsel ışık programları derleniyor", "Compiling the volumetric light programs");
            st.fraction = 0.05f;
            ++st.step;
            return;
        case 1: {
            f_.root = host.root();
            f_.overrides = host.overrides();
            f_.loadConfig();
            f_.renderer.init(f_.width, f_.height, f_.root, unsigned(f_.cfg.i("volume.scale", 1)));
            if (!f_.compile()) { st.error = L("Işın izleme programları derlenemedi (ayrıntı konsolda).", "The ray-tracing programs did not compile (see the console)."); return; }
            st.label = L("Adalar, orman ve fener kuruluyor", "Building the islands, the forest and the lighthouse");
            st.fraction = 0.5f;
            ++st.step;
            return;
        }
        case 2:
            f_.loadBlueNoise();
            f_.applyConfig();
            f_.rebuildScene();
            t_ = f_.cfg.f("time", 0.0f);
            st.label = L("Sis toplanıyor", "Gathering the fog");
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
        if (in.pressed(GLFW_KEY_P)) { paused_ = !paused_; host.toast(paused_ ? L("Zaman durdu", "Time paused") : L("Zaman akıyor", "Time runs")); }
        if (in.pressed(GLFW_KEY_RIGHT)) t_ += 1.0;
        if (in.pressed(GLFW_KEY_LEFT)) t_ -= 1.0;
        if (in.pressed(GLFW_KEY_TAB)) {
            f_.mode = (f_.mode + 1) % 3;
            if (f_.mode == 0) {
                host.captureMouse(false);
                f_.applyJourney();
                f_.renderer.resetHistory();
                host.toast(L("Sahne", "Scene"));
            } else {
                host.captureMouse(true);
                f_.placePlayer(f_.mode);
                host.toast(f_.mode == 1 ? L("Oyuncu · WASD yürü, fare bak, Shift koş, Boşluk zıpla", "Player · WASD walk, mouse look, Shift run, Space jump")
                                        : L("Oyuncu · fener adası", "Player · lighthouse island"));
            }
        }
        bool jump = false;
        if (in.pressed(GLFW_KEY_SPACE)) {
            if (f_.mode == 0) {
                f_.journey.target ^= 1;
                host.toast(f_.journey.target ? L("Yola çıkış: B · Yücelik", "Setting off: B · Lookout") : L("Yola çıkış: A · Pencere", "Setting off: A · Window"));
            } else {
                jump = true;
            }
        }
        if (in.pressed(GLFW_KEY_B) && f_.mode == 0) {
            f_.setView(f_.view + 1);
            host.toast(f_.view == 0 ? L("Bakış A · Pencere", "View A · Window") : L("Bakış B · Yücelik", "View B · Lookout"));
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
        if (f_.mode == 0) s.chips.push_back({L("Sahne", "Scene"), true});
        else s.chips.push_back({f_.player.swimming ? L("Oyuncu · yüzüyor", "Player · swimming")
                                                    : (f_.mode == 1 ? L("Oyuncu", "Player") : L("Oyuncu · fener adası", "Player · lighthouse island")),
                                true});
        if (f_.mode == 0 && !f_.journey.moving()) s.chips.push_back({f_.view == 0 ? L("A · Pencere", "A · Window") : L("B · Yücelik", "B · Lookout")});
        if (paused_) s.chips.push_back({L("Zaman durdu", "Time paused"), true});
        const char* views[] = {"", L("Yalnız yüzey", "Surface only"), L("Yalnız hacim", "Volume only"), L("Derinlik", "Depth"), "Normal"};
        if (f_.renderer.params().view > 0 && f_.renderer.params().view < 5) s.chips.push_back({views[f_.renderer.params().view], true});
        s.metrics.push_back({L("Yüzey", "Surface"), num(tm.surface, 1) + " ms"});
        s.metrics.push_back({L("Hacim", "Volume"), num(tm.volume + tm.resolve, 1) + " ms"});
        s.metrics.push_back({L("Birikim ve ışıltı", "Accumulation and glow"), num(tm.compose + tm.taa + tm.glow + tm.final, 1) + " ms"});
        s.metrics.push_back({L("Göz uyumu", "Eye adaptation"), (f_.evAdapt >= 0 ? "+" : "") + num(f_.evAdapt, 2) + " EV"});
        if (f_.mode == 0 && f_.journey.moving()) {
            s.progress = f_.journey.d / std::max(1.0f, f_.journey.length());
            s.progressLabel = L("Yolculuk ", "Walk ") + std::string(f_.journey.target ? "A → B" : "B → A") + " · " + num(f_.journey.d, 0) + " / " +
                              num(f_.journey.length(), 0) + " m";
        }
    }

    void screenshot(app::Host& host) override {
        f_.save(host.capturePath());
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        f_.loadConfig();
        if (f_.compile()) {
            f_.applyConfig();
            f_.rebuildScene();
            host.toast(L("Ayarlar ve programlar yeniden yüklendi", "Settings and programs reloaded"));
        } else {
            host.toast(L("Derleme hatası: ayrıntı konsolda", "Compile error: see the console"));
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
