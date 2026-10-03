// Scene 5 "Koy" inside the application: a diver in a Mediterranean cove, 3-15 m (K1: the trial area). WASD swims,
// the mouse looks (Tab holds it), the panel's knobs change the sampling live (Durak 1: sharpness and steadiness).
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/koy/koy_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

namespace {

using app::L;

const int kBaseChoices[] = {1, 4, 8, 16};
const int kEdgeChoices[] = {4, 8, 16, 32};

class KoyLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "koy";
        i.number = "05";
        i.title = L("Koy", "Cove");
        i.subtitle = L("Akdeniz'de bir koyun dibi, öğle güneşi", "The floor of a Mediterranean cove at noon");
        i.description = L("Öğle güneşi altında berrak bir koy: dipte dans eden ışık ağları, tepede parlayan Snell penceresi, aşağı inen ışık "
                          "sütunları. Tüplü bir dalgıç gibi yüzülür.",
                          "A clear cove under the noon sun: nets of light dancing on the floor, Snell's window shining above, shafts of "
                          "light coming down. You swim it like a scuba diver.");
        i.details = {
            {L("Netlik önce", "Sharpness first"),
             L("Her piksel her karede aynı sabit alt piksel örnekleriyle çizilir (titreşim yok, geçmiş yok); kenardaki piksellere 32'ye "
               "kadar örnek eklenir, pikselden biraz geniş bir süzgeçle birleştirilir. Zamansal tahmin kullanılmaz.",
               "Every pixel is drawn with the same fixed sub-pixel samples every frame (no jitter, no history); pixels on an edge get "
               "up to 32 samples, joined by a filter a little wider than a pixel. No temporal guessing.")},
            {L("Tayfsal su", "Spectral water"),
             L("Suyun soğurması ve saçılması 5 nm adımla, yayımlanmış ölçümlerden; güneş ve gök tayfı, göz eğrileri. Hepsi hazırlıkta "
               "ışığın sudaki yol uzunluğuna göre tablolara pişirilir: renkler derinlikle kendiliğinden maviye döner.",
               "The water's absorption and scattering every 5 nm from published measurements; the spectra of sun and sky, the eye's "
               "curves. All baked beforehand into tables by the light's path length in water: colours turn blue with depth on "
               "their own.")},
            {L("Yüzey ve kostik", "Surface and caustics"),
             L("Dalgalar Tessendorf'un FFT yöntemiyle, kusursuz döngüyle. Her karede yüzeyden bir milyon güneş ışını kırılır, "
               "derinlik dilimlerinde kostik hacmine işlenir; güneş diski onu derinlikle bulanıklaştırır.",
               "Waves by Tessendorf's FFT method, looping seamlessly. Every frame a million sun rays refract through the surface "
               "into a caustic volume in depth slices; the sun's disc blurs it with depth.")},
            {L("Işık alanı", "Light field"),
             L("Su sütununun dağınık ışığı (pencereden gök, çok kez saçılan güneş, kumdan yansıyan) hazırlıkta Monte Carlo ile "
               "hesaplanır; canlı karede gürültüsüz okunur.",
               "The water column's diffuse light (the sky through the window, the sun scattered many times, the light off the "
               "sand) is computed beforehand by Monte Carlo and read noise-free in every frame.")}};
        i.tags = {L("Tayfsal su optiği", "Spectral water optics"), L("Süper örnekleme", "Supersampling"), L("Kostik", "Caustics")};
        i.image = "assets/gallery/koy.jpg";
        i.accent = 0x4FB8C8;
        i.keys = {{L("Dalgıç", "Diver"),
                   {{"W|A|S|D", L("Yüz", "Swim")}, {L("Boşluk", "Space"), L("Yukarı", "Up")}, {"C", L("Aşağı", "Down")},
                    {"Shift", L("Güçlü palet vuruşu", "Strong kick")}, {"Tab", L("Fareyle bak (aç/kapa)", "Mouse look (on/off)")},
                    {L("Sağ tık", "Right button"), L("Basılı tutup bak", "Hold to look")}}},
                  {L("Sahne", "Scene"),
                   {{"P", L("Zamanı durdur (durunca örnekler birikir)", "Pause time (still: samples accumulate)")},
                    {"E", L("Kenar piksellerini göster", "Show the edge pixels")}, {"K", L("Kostik aç/kapa", "Caustics on/off")},
                    {"↑|↓", L("Pozlama", "Exposure")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<KoyLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Işık programları derleniyor", "Compiling the light programs");
            st.fraction = 0.05f;
            ++st.step;
            return;
        case 1:
            k_.root = host.root();
            k_.overrides = host.overrides();
            k_.loadConfig();
            k_.renderer.init(k_.width, k_.height, k_.root);
            if (!k_.compile()) { st.error = L("Işın izleme programları derlenemedi (ayrıntı konsolda).", "The ray-tracing programs did not compile (see the console)."); return; }
            st.label = L("Suyun ışığı hesaplanıyor: tayf, saçılma, ışık alanı", "Computing the water's light: spectra, scattering, the light field");
            st.fraction = 0.2f;
            ++st.step;
            return;
        case 2:
            k_.setup();
            st.label = L("İlk kare", "The first frame");
            st.fraction = 0.9f;
            ++st.step;
            return;
        default:
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(k_.width) * k_.height * sizeof(uchar4));
            k_.stepWorld(0.0f);
            k_.frame();
            k_.adaptExposure(0.0f, true);
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        const float dt = float(std::min(0.1, in.dt));
        for (const app::ControlEvent& e : in.events) {
            if (e.id == "ornek" && e.item >= 0) k_.quality.base = kBaseChoices[e.item];
            if (e.id == "kenar" && e.item >= 0) k_.quality.edge = kEdgeChoices[e.item];
            if (e.id == "suzgec" && e.item >= 0) k_.quality.filter = e.item;
            if (e.id == "doku" && e.item >= 0) {
                k_.setTextureMode(e.item);
                const int got = k_.world.texMode;   // the best that is loaded, at most the one asked for
                if (got != e.item)
                    host.toast(L("Bu doku yüklenmedi (dosya yok, GPU belleği yetmedi ya da ayarla kapalı; nedeni konsolda)",
                                 "These textures did not load (no files, not enough GPU memory or turned off; the console says which)"));
                else
                    host.toast(got == 2   ? L("Doku: ultra (kum ve ayrıntı katmanı 16K, kaya 8K)", "Textures: ultra (sand and detail layer 16K, rock 8K)")
                               : got == 1 ? L("Doku: yüksek (her şey 8K: kaya, kum, ayrıntı katmanı)", "Textures: high (everything 8K: rock, sand, detail layer)")
                                          : L("Doku: standart (2K)", "Textures: standard (2K)"));
            }
            k_.applyQuality();
            resetAccum();
        }
        if (!paced_) {   // the first running frame: the frame rate the plan asks for (41 fps, every 4th refresh at 165 Hz)
            host.pace(pace41_ ? 41.0f : 0.0f);
            paced_ = true;
        }
        for (const app::ControlEvent& e : in.events)
            if (e.id == "kare" && e.item >= 0) {
                pace41_ = e.item == 0;
                host.pace(pace41_ ? 41.0f : 0.0f);
            }
        if (in.pressed(GLFW_KEY_TAB)) {
            look_ = !look_;
            host.captureMouse(look_);
        }
        if (in.pressed(GLFW_KEY_P)) {
            paused_ = !paused_;
            host.toast(paused_ ? L("Zaman durdu: durunca örnekler birikir", "Time paused: samples accumulate while still") : L("Zaman akıyor", "Time runs"));
            resetAccum();
        }
        if (in.pressed(GLFW_KEY_E)) k_.renderer.params().view = k_.renderer.params().view == 1 ? 0 : 1;
        if (in.pressed(GLFW_KEY_K)) {
            k_.caustics = !k_.caustics;
            host.toast(k_.caustics ? L("Kostik açık", "Caustics on") : L("Kostik kapalı (düz güneş)", "Caustics off (flat sun)"));
        }
        if (in.pressed(GLFW_KEY_UP)) k_.ev += 0.25f;
        if (in.pressed(GLFW_KEY_DOWN)) k_.ev -= 0.25f;
        auto key = [&](int kc) { return in.isDown(kc) ? 1.0f : 0.0f; };
        const float fwd = key(GLFW_KEY_W) - key(GLFW_KEY_S), right = key(GLFW_KEY_D) - key(GLFW_KEY_A);
        const float up = key(GLFW_KEY_SPACE) - key(GLFW_KEY_C);
        const bool looking = look_ || in.rightDown;
        const float mdx = looking ? in.mouseDX : 0.0f, mdy = looking ? in.mouseDY : 0.0f;
        const Diver before = k_.diver;
        k_.stepDiver(fwd, right, up, in.shift, mdx, mdy, dt);
        const bool moved = length(k_.diver.pos - before.pos) > 1e-5f || k_.diver.yaw != before.yaw || k_.diver.pitch != before.pitch;
        if (!paused_) t_ += in.dt;
        if (!paused_ || moved) {
            k_.stepWorld(float(t_));
            k_.frame();
            k_.adaptExposure(dt, false);
            accum_ = 0;
        } else if (accum_ < 8) {
            ++accum_;   // a few still frames first, then the picture of 256 samples a pixel (the shifted patterns, once)
            if (accum_ == 8) k_.renderer.renderReference(4);
        }
        cudaMemcpy(pinned_, k_.renderer.params().output, size_t(k_.width) * k_.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
    }

    const void* image(int& w, int& h) override {
        w = int(k_.width);
        h = int(k_.height);
        return pinned_;
    }

    void status(app::Status& s) override {
        using theme::num;
        const KoyTimes& tm = k_.renderer.times();
        s.chips.push_back({L("Deneme alanı", "Trial area"), true});
        if (paused_) s.chips.push_back({accum_ ? L("Durdu · birikiyor", "Paused · accumulating") : L("Zaman durdu", "Time paused"), true});
        if (look_) s.chips.push_back({L("Fareyle bakış", "Mouse look")});
        if (k_.renderer.params().view == 1) s.chips.push_back({L("Kenar pikselleri", "Edge pixels"), true});
        if (!k_.caustics) s.chips.push_back({L("Kostik kapalı", "Caustics off"), true});
        s.metrics.push_back({L("Derinlik", "Depth"), num(-k_.diver.pos.y, 1) + " m"});
        s.metrics.push_back({L("Dünya", "World"), num(tm.world, 1) + " ms"});
        s.metrics.push_back({L("Işık sütunları", "Light shafts"), num(tm.shafts, 1) + " ms"});
        s.metrics.push_back({L("Örnekler", "Samples"), num(tm.base, 1) + " ms"});
        s.metrics.push_back({L("Kenar", "Edges"), num(tm.edge, 1) + " ms · " + theme::thousands(tm.edgePixels)});
        s.metrics.push_back({L("Çözüm ve ton", "Resolve and tone"), num(tm.resolve + tm.final, 1) + " ms"});
        s.metrics.push_back({L("Pozlama", "Exposure"), (k_.evAdapt >= 0 ? "+" : "") + num(k_.evAdapt, 2) + " EV"});
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "ornek";
            c.title = L("Örnek", "Samples");
            c.items = {"1", "4", "8", "16"};
            for (int k = 0; k < 4; ++k)
                if (kBaseChoices[k] == k_.quality.base) c.active = k;
            c.detail = L("Piksel başına sabit örnek (her yerde)", "Fixed samples a pixel (everywhere)");
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "kenar";
            c.title = L("Kenar", "Edges");
            c.items = {"4", "8", "16", "32"};
            for (int k = 0; k < 4; ++k)
                if (kEdgeChoices[k] == k_.quality.edge) c.active = k;
            c.detail = L("Kenardaki piksele toplam örnek", "Samples in all for a pixel on an edge");
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "doku";
            c.title = L("Doku", "Textures");
            c.items = {L("Standart", "Standard"), L("Yüksek", "High"), "Ultra"};
            c.active = k_.world.texMode;
            c.detail = L("Standart 2K · Yüksek: her şey 8K · Ultra: kum ve ayrıntı katmanı 16K (kayanın kendi taraması 8K'da biter)",
                         "Standard 2K · High: everything 8K · Ultra: sand and detail layer 16K (the rock's own scan ends at 8K)");
            c.section = true;
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "kare";
            c.title = L("Kare", "Frames");
            c.items = {L("41 sabit", "41 steady"), L("Serbest", "Free")};
            c.active = pace41_ ? 0 : 1;
            c.detail = L("41: ekranın her 4. tazelemesi, eşit aralık", "41: every 4th refresh of the screen, evenly spaced");
            c.section = true;
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "suzgec";
            c.title = L("Süzgeç", "Filter");
            c.items = {L("Kutu", "Box"), "Blackman-Harris", "Mitchell", "Gauss"};
            c.active = k_.quality.filter;
            c.detail = L("Örnekleri birleştiren yeniden kurma süzgeci", "The reconstruction filter that joins the samples");
            s.controls.push_back(c);
        }
    }

    void screenshot(app::Host& host) override {
        k_.save(host.capturePath());
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        k_.loadConfig();
        if (k_.compile()) {
            k_.applyQuality();
            host.toast(L("Ayarlar ve programlar yeniden yüklendi", "Settings and programs reloaded"));
        } else {
            host.toast(L("Derleme hatası: ayrıntı konsolda", "Compile error: see the console"));
        }
    }

    void unload() override {
        k_.release();
        k_.renderer.shutdown();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }
    ~KoyLive() override { unload(); }

private:
    void resetAccum() { accum_ = 0; }
    app::SceneInfo info_ = describe();
    KoyApp k_;
    uchar4* pinned_ = nullptr;
    double t_ = 0.0;
    bool paused_ = false, look_ = false, paced_ = false, pace41_ = true;
    int accum_ = 0;
};

}   // namespace

namespace koy {
app::SceneInfo info() { return KoyLive::describe(); }
std::unique_ptr<app::Scene> make() { return KoyLive::make(); }
int cli(int argc, char** argv) { return runKoyCli(argc, argv); }
}   // namespace koy
