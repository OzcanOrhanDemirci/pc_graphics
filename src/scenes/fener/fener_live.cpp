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
        i.subtitle = L("Gece sisinde dönen bir fener huzmesi", "A lighthouse beam turning in the night fog");
        i.description = L("Sisli bir gecede bir fener döner; huzmeleri sisi yarar, kara köknarlar ve tek sıcak pencere onlara karşı "
                          "kararır. Boşlukla iki bakış arasında ormanın içinden yürürsün, Tab ile adalarda kendin dolaşırsın.",
                          "On a foggy night a lighthouse turns. Its beams cut through the fog, and black firs and one warm window stand "
                          "dark against them. Space walks you through the forest between two views; Tab lets you roam the islands.");
        i.details = {
            {L("Siste tek saçılmalı ışık", "Single scattering in the fog"),
             L("Sis, yoğunluğu yükseklikle azalan bir ortam olarak hesaplanır. Her pikselde bakış ışını sisin içinde ilerler, fenerin "
               "ışığını 8, ayınkini 4 örnekle toplar. Her örnek kendi gölge ışınını atar; huzme bu yüzden ağaçların ve kulenin "
               "ardında kesilir. Fener örnekleri, ışının lambaya en yakın geçtiği yerde sıklaşan eşit açılı örneklemeyle seçilir.",
               "The fog is a medium whose density falls with height. In every pixel the view ray marches through it and gathers "
               "the lamp's light with 8 samples and the moon's with 4. Each sample casts its own shadow ray, which is why the beams "
               "stop behind the trees and the tower. Equi-angular sampling crowds the lamp's samples where the ray passes closest "
               "to the light.")},
            {L("Az örnek, zamansal birikim", "Few samples over many frames"),
             L("Huzme hiç durmadığı için her kare kendi başına temiz olmalı. INSIDE'ın yolu izlenir: örnekler mavi gürültüyle "
               "dağıtılır, sis önceki karelerle birleştirilir (eski kare yeni bakışa taşınır, komşu piksellere göre kırpılır), "
               "sonra bütün görüntüye TAA uygulanır. Sis tam çözünürlükte çizilir; yarım çözünürlük yakın silüetlerde parlak hale "
               "bırakıyordu.",
               "The beams never stop, so every frame has to be clean on its own. The recipe is INSIDE's: samples are spread with "
               "blue noise, the fog is blended with earlier frames (the old frame reprojected to the new view and clamped to its "
               "neighbours), then TAA runs over the whole picture. The fog is drawn at full resolution; half resolution left "
               "bright halos on near silhouettes.")},
            {L("Sanat için fizikten sapmalar", "Departures from physics, for art"),
             L("Gerçek siste huzme ileriye, yanına göre yaklaşık 100 kat güçlü saçılır; kamera huzmenin içine girince bütün sis "
               "düz beyaza döner. Burada bu oran yaklaşık 5'e indirildi ve huzmenin sisteki parlaklığına yumuşak bir tavan kondu. "
               "Ay diski 0,55° (gerçeği 0,26°). Zayıf, aşağı eğik bir ışık lobu ormanı ve denizi süpürür.",
               "In real fog the beam scatters about 100 times more forwards than sideways, and with the camera inside the beam "
               "all the fog turns flat white. Here that ratio is brought down to about 5, and the beam's brightness in the fog "
               "has a soft ceiling. The moon's disc is 0.55° (the real one 0.26°). A faint lobe tilted downwards sweeps the "
               "forest and the sea.")},
            {L("Göz uyumu ve renk", "Eye adaptation and colour"),
             L("Sık ormanın içi açık bakışlardan yaklaşık 4,8 EV daha karanlıktır. Göz bu farkın yarısına 1,5 saniyede uyar: orman "
               "yine karanlık hissettirir ama okunur. Renk RGB'de hesaplanır (tayfsal değil), ACES eğrisiyle ekrana indirilir, "
               "siyahlara hafif soğuk bir ton eklenir. 8 bitlik ekranda bant oluşmasın diye her piksele çok az mavi gürültü katılır.",
               "Inside the dense forest it is about 4.8 EV darker than in the open views. The eye adapts to half that difference "
               "in 1.5 seconds, so the forest still feels dark but reads. Colour is computed in RGB (not spectral), brought to "
               "the screen with the ACES curve, with a slight cold tint in the blacks. A trace of blue noise in every pixel "
               "keeps an 8-bit screen free of banding.")},
            {L("Adalar, deniz ve orman", "Islands, sea and forest"),
             L("İki ada, yükseklik fonksiyonlarından üretilen ağlardır. Deniz düz bir yüzeydir; kırk dalganın toplamı yalnız "
               "yüzeyin eğimini verir ve ayın parıltısı bu dalgalarda kırılır. Orman yedi çeşit köknarın on binlerce kopyasıdır; "
               "ışın izleme donanımı aynı ağları yeniden kullanarak yüz milyonlarca üçgeni taşır.",
               "The two islands are meshes generated from height functions. The sea is a flat surface; a sum of forty waves "
               "gives only its slope, and the moon's glitter breaks up on those waves. The forest is tens of thousands of copies "
               "of seven kinds of fir; the ray-tracing hardware reuses the same meshes to carry hundreds of millions of "
               "triangles.")},
            {L("Yürüyüş ve oyuncu kipi", "The walk and player mode"),
             L("Boşluk kamerayı iki bakış arasında 778 m'lik bir yoldan ormanın içinden yürütür; yumuşak kalkar, yumuşak durur. "
               "Tab birinci şahıs kipine geçer: yer yüksekliği adaların kendi fonksiyonundan gelir, ağaç gövdeleri, kule ve kulübe "
               "engeldir, derin suda yüzersin. Dünyada oyuncu için ayrıca kurulmuş bir şey yoktur.",
               "Space walks the camera 778 m through the forest between the two views, easing off and easing to a stop. Tab "
               "switches to first person: the ground height comes from the islands' own functions, tree trunks, the tower and "
               "the cabin are in the way, and in deep water you swim. Nothing in the world was built for the player.")}};
        i.tags = {L("Hacimsel ışık", "Volumetric light"), L("Zamansal birikim", "Temporal accumulation"),
                  L("Oyuncu kipi", "Player mode")};
        i.image = "assets/gallery/fener.jpg";
        i.accent = 0x86B3DA;
        i.keys = {{L("Sahne", "Scene"),
                   {{L("Boşluk", "Space"), L("A ↔ B yürü; yoldayken geri dön", "Walk A ↔ B; on the way, turn back")},
                    {"B", L("Öbür bakışa atla", "Jump to the other view")}, {"P", L("Zamanı durdur / sürdür", "Pause / resume time")},
                    {"←|→", L("Zamanı 1 sn geri / ileri al", "Time 1 s back / forward")}, {"↑|↓", L("Pozlama ±0,25 EV", "Exposure ±0.25 EV")},
                    {"1|2|3|4|5", L("Son, yüzey, sis, derinlik, normal", "Final, surface, fog, depth, normal")}}},
                  {L("Oyuncu", "Player"),
                   {{"Tab", L("Sahne → oyuncu → fener adası → sahne", "Scene → player → lighthouse → scene")},
                    {"W|A|S|D", L("Yürü", "Walk")}, {L("Fare", "Mouse"), L("Etrafa bak", "Look around")},
                    {"Shift", L("Basılı tutup koş", "Hold to run")}, {L("Boşluk", "Space"), L("Zıpla", "Jump")}}}};
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
        const char* views[] = {"", L("Yalnız yüzey", "Surface only"), L("Yalnız sis", "Fog only"), L("Derinlik", "Depth"), "Normal"};
        if (f_.renderer.params().view > 0 && f_.renderer.params().view < 5) s.chips.push_back({views[f_.renderer.params().view], true});
        s.metrics.push_back({L("Yüzey", "Surface"), num(tm.surface, 1) + " ms"});
        s.metrics.push_back({L("Sis", "Fog"), num(tm.volume + tm.resolve, 1) + " ms"});
        s.metrics.push_back({L("Birikim ve parlama", "Accumulation and glow"), num(tm.compose + tm.taa + tm.glow + tm.final, 1) + " ms"});
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
