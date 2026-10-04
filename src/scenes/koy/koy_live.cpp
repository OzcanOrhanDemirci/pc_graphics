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
const int kEdgeChoices[] = {4, 8, 12, 16, 32};

class KoyLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "koy";
        i.number = "05";
        i.title = L("Ada", "The Island");
        i.subtitle = L("Bir adanın iskelesinden maviye", "From an island's pier into the blue");
        i.description = L("Küçük bir adanın beton iskelesinin altında karanlık bir salonda başlarsın. Dubaların çizdiği yoldan kum yavaş yavaş "
                          "derinleşir: kayalara yaslanmış amforalar, yarı yıkık bir kalyon, en uçta dibi görünmeyen maviye inen bir "
                          "şamandıra halatı. Yüzeye çıkılmaz; çok derine inersen basınç seni geri yollar.",
                          "You start in a dark hall under a small island's concrete pier. Along a lane of buoys the sand deepens slowly: "
                          "amphorae gathered against rocks, a half-fallen galleon, and at the far end a buoy's rope going down into a blue "
                          "with no bottom in sight. There is no surfacing; go too deep and the pressure sends you back.");
        i.details = {
            {L("Netlik önce", "Sharpness first"),
             L("Her piksel her karede aynı sabit alt piksel örnekleriyle çizilir (titreşim yok, geçmiş yok); kenardaki piksellere örnek "
               "eklenir, pikselden biraz geniş bir süzgeçle birleştirilir. Zamansal tahmin kullanılmaz.",
               "Every pixel is drawn with the same fixed sub-pixel samples every frame (no jitter, no history); pixels on an edge get "
               "more, joined by a filter a little wider than a pixel. No temporal guessing.")},
            {L("Gerçek zamanlı ışık", "Real-time light"),
             L("Güneşin gölgesi su yüzeyinden sonra havada da sürer: iskelenin tahta aralıklarından ve beton başlığın kırık "
               "deliklerinden sızan ışık, kumda ve suda dalgayla salınır. Başlığın altında gökten gelen dağınık ışık da kesilir: "
               "karanlık bir salon. Su üstü yalnız alttan, Snell penceresinden görünür.",
               "The sun's shadow goes on through the air past the water's surface: the light leaking between the pier's boards and "
               "through the broken holes of its concrete head sways with the waves on the sand and in the water. Under the head the "
               "sky's diffuse light is cut too: a dark hall. Above the water is seen only from below, through Snell's window.")},
            {L("Batık", "The wreck"),
             L("38 metrelik bir kalyon, tek bir gövde fonksiyonundan kereste kereste kurulmuş: postalar, kaplama sıraları, iki "
               "güverte, kıç ve baş kasaraları, pencereli ayna, lumbaralar ve toplar, kırık direkler, çapa. Yamaçta başı aşağıda, "
               "sancağa yatmış; sancak tarafı çökmüş, kaburgaları açıkta. İçine girilir.",
               "A 38 metre galleon built timber by timber from one hull function: frames, strakes of planking, two decks, the "
               "castles fore and aft, the stern with its windows, gunports and guns, broken masts, an anchor. On the slope bow down, "
               "listing to starboard; the starboard side has fallen in, its ribs bare. You can swim inside.")},
            {L("Derinlik ve basınç", "Depth and pressure"),
             L("Kum adadan uzaklaştıkça hep derinleşir: 3,5 m'den 100 m'nin ötesine. Derinde zemin görünmez, dalınca belirir. "
               "Havadaki oksijenin kısmi basıncı 1,6 bar'ı (~66 m) aşınca zehirlenme başlar: görüş daralır, kararır, iskele "
               "merdiveninde yeniden başlarsın.",
               "The sand keeps deepening away from the island: from 3.5 m to past 100 m. Deep down the floor cannot be seen until "
               "you dive towards it. When the oxygen's partial pressure in the air passes 1.6 bar (~66 m) toxicity sets in: the "
               "view narrows, goes black, and you start again at the pier's ladder.")},
            {L("Tayfsal su", "Spectral water"),
             L("Suyun soğurması ve saçılması 5 nm adımla, yayımlanmış ölçümlerden; güneş ve gök tayfı, göz eğrileri. Renkler "
               "derinlikle kendiliğinden maviye döner; gözün renk uyumu yarı yolda.",
               "The water's absorption and scattering every 5 nm from published measurements; the spectra of sun and sky, the eye's "
               "curves. Colours turn blue with depth on their own; the eye adapts its colour halfway.")},
            {L("Amforalar", "Amphorae"),
             L("Dört tip (Dressel 1B, 1A, 2-4, Lamboglia 2), müze çizimlerinin profillerinden (ArchAIDE, Southampton Üniversitesi, "
               "CC BY 4.0); kırık gövdeler, kulplu boyunlar, parçalar.",
               "Four types (Dressel 1B, 1A, 2-4, Lamboglia 2), from the profiles of museum drawings (ArchAIDE, University of "
               "Southampton, CC BY 4.0); broken bodies, necks with handles, sherds.")}};
        i.tags = {L("Gerçek zamanlı ışık", "Real-time light"), L("Batık", "Wreck"), L("Tayfsal su optiği", "Spectral water optics")};
        i.image = "assets/gallery/koy.jpg";
        i.accent = 0x4FB8C8;
        i.keys = {{L("Dalgıç", "Diver"),
                   {{"W|A|S|D", L("Yüz", "Swim")}, {L("Boşluk", "Space"), L("Yukarı", "Up")}, {"C", L("Aşağı", "Down")},
                    {"Shift", L("Güçlü palet vuruşu", "Strong kick")}, {"Tab", L("Fareyle bak (aç/kapa)", "Mouse look (on/off)")},
                    {L("Sağ tık", "Right button"), L("Basılı tutup bak", "Hold to look")}}},
                  {L("Sahne", "Scene"),
                   {{"P", L("Zamanı durdur (durunca örnekler birikir)", "Pause time (still: samples accumulate)")},
                    {"E", L("Kenar piksellerini göster", "Show the edge pixels")}, {"K", L("Kostik aç/kapa", "Caustics on/off")}, {"F", L("Fener aç/kapa", "Torch on/off")},
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
            k_.calibrateEye();
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        const float dt = float(std::min(0.1, in.dt));
        bool refresh = false;   // the world's light changed: recompute it even with time paused
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
            if (e.id == "fener" && e.item >= 0) {
                k_.torchOn = e.item == 1;
                k_.setCamera();
                refresh = true;
            }
            if (e.id == "dalgacik" && e.item >= 0) {
                k_.setRipples(e.item == 1);
                if (e.item == 1 && !k_.ripplesOn)
                    host.toast(L("Dalgacık katmanı yok (ayarla kapalı ya da kurulamadı)", "No ripple layer (turned off or it could not be built)"));
                else
                    host.toast(k_.ripplesOn ? L("Dalgacıklar açık: yüzeyin 2-35 cm'lik dalgaları (yukarı bak)", "Ripples on: the surface's 2-35 cm waves (look up)")
                                            : L("Dalgacıklar kapalı: yalnız istatistiksel pürüzlülük", "Ripples off: statistical roughness only"));
                refresh = true;
            }
            if (e.id == "kostik" && e.item >= 0) {
                k_.ocean.cset.method = e.item == 0 ? 1 : 0;
                refresh = true;
                host.toast(e.item == 0 ? L("Kostik: hüzme (gürültüsüz)", "Caustics: beams (noise free)")
                                       : L("Kostik: ışın sayma (eski yöntem, kaynar)", "Caustics: counted rays (the old method, it boils)"));
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
        if (in.pressed(GLFW_KEY_F)) {
            k_.torchOn = !k_.torchOn;
            k_.setCamera();
            host.toast(k_.torchOn ? L("Fener açık: yakında gerçek renkler (kırmızı birkaç metrede söner)", "Torch on: true colours close up (red dies within metres)")
                                  : L("Fener kapalı", "Torch off"));
            resetAccum();
        }
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
        const bool out = k_.blackedOut();   // the convulsion and the blackout: no swimming, no looking
        k_.stepDiver(out ? 0.0f : fwd, out ? 0.0f : right, out ? 0.0f : up, in.shift, out ? 0.0f : mdx, out ? 0.0f : mdy, dt);
        const int restarts = k_.restarts;
        const float tunnel0 = k_.renderer.params().tunnel, black0 = k_.renderer.params().blackout;
        k_.stepBody(dt);
        if (k_.restarts != restarts)
            host.toast(L("Oksijen zehirlenmesi: ppO2 1,6 bar'ı aştı (~66 m). Merdivende yeniden başladın",
                         "Oxygen toxicity: ppO2 passed 1.6 bar (~66 m). You start again at the ladder"));
        const bool moved = length(k_.diver.pos - before.pos) > 1e-5f || k_.diver.yaw != before.yaw || k_.diver.pitch != before.pitch ||
                           k_.renderer.params().tunnel != tunnel0 || k_.renderer.params().blackout != black0;
        if (!paused_) t_ += in.dt;
        if (!paused_ || moved || refresh) {
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
        if (paused_) s.chips.push_back({accum_ ? L("Durdu · birikiyor", "Paused · accumulating") : L("Zaman durdu", "Time paused"), true});
        if (look_) s.chips.push_back({L("Fareyle bakış", "Mouse look")});
        if (k_.renderer.params().view == 1) s.chips.push_back({L("Kenar pikselleri", "Edge pixels"), true});
        if (!k_.caustics) s.chips.push_back({L("Kostik kapalı", "Caustics off"), true});
        s.metrics.push_back({L("Derinlik", "Depth"), num(-k_.diver.pos.y, 1) + " m"});
        s.metrics.push_back({L("Basınç", "Pressure"), num(k_.pressureBar(), 2) + " bar"});
        s.metrics.push_back({L("Oksijen (ppO2)", "Oxygen (ppO2)"), num(k_.ppO2(), 2) + " bar"});
        if (k_.ppO2() >= k_.cfg.f("o2.limit", 1.6f))
            s.chips.push_back({L("ppO2 1,6 üstü: oksijen zehirlenmesi", "ppO2 above 1.6: oxygen toxicity"), true});
        else if (k_.ppO2() >= 1.4f)
            s.chips.push_back({L("ppO2 1,4 üstü: dalış sınırı", "ppO2 above 1.4: the diving limit"), true});
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
            c.items = {"4", "8", "12", "16", "32"};
            for (int k = 0; k < 5; ++k)
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
            c.id = "dalgacik";
            c.title = L("Dalgacık", "Ripples");
            c.items = {L("Kapalı", "Off"), L("Açık", "On")};
            c.active = k_.ripplesOn ? 1 : 0;
            c.detail = L("Su yüzeyinin 2-35 cm'lik dalgaları; yalnız bakışta (Snell penceresi, ayna, pırıltı), kostik değişmez",
                         "The surface's 2-35 cm waves; in the view alone (Snell's window, the mirror, the glints), the caustics unchanged");
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "fener";
            c.title = L("Fener", "Torch");
            c.items = {L("Kapalı", "Off"), L("Açık", "On")};
            c.active = k_.torchOn ? 1 : 0;
            c.detail = L("Dalgıcın el feneri (beyaz LED, 1 m'de ~3000 lx): yakında amforanın kil rengi, süngerin turuncusu; ışığı suda "
                         "gidip döner, kırmızı birkaç metrede söner",
                         "The diver's torch (white LED, ~3000 lx at 1 m): close up the jars' clay, the sponges' orange; its light goes "
                         "through the water and back, red dies within metres");
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "kostik";
            c.title = L("Kostik", "Caustics");
            c.items = {L("Hüzme", "Beams"), L("Işın (eski)", "Rays (old)")};
            c.active = k_.ocean.cset.method == 1 ? 0 : 1;
            c.detail = L("Hüzme: yüzeyin her hücresi bir ışık hüzmesi, ışığı derinlikte kapladığı alana yayılır; sayma yok, gürültü yok. "
                         "Işın: eski yöntem, dokuya düşen ışınları sayar (kaynar)",
                         "Beams: every cell of the surface is a beam of light, spread over the area it covers at depth; nothing counted, no noise. "
                         "Rays: the old method, counting the rays that land in each texel (it boils)");
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
