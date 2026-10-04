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

const int kBaseChoices[] = {1, 4, 8, 16, 32};
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
            // settled (Ozcan 2026-10-05: "şu ayarlar sabitlensin seçmeyelim"): beams for the caustics, Blackman-Harris, the
            // ripples on, the frame rate free; no torch ("fener olmasın")
            k_.ocean.cset.method = 1;
            k_.quality.filter = 1;
            k_.applyQuality();
            k_.setRipples(true);
            k_.torchOn = false;
            k_.setCamera();
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
        if (loading_) {
            // another texture tier on its way: the picture held on purpose (the old textures are gone from the card), the
            // diver held too; the panel's bar and the card over the picture say how far (status)
            if (k_.pollTextureTier(&loadFraction_, &loadWhat_)) {
                loading_ = false;
                host.toast(tierToast(k_.world.texMode));
                refresh = true;
                resetAccum();
            } else {
                return;
            }
        }
        for (const app::ControlEvent& e : in.events) {
            if (e.id == "ornek" && e.item >= 0) k_.quality.base = kBaseChoices[e.item];
            if (e.id == "kenar" && e.item >= 0) k_.quality.edge = kEdgeChoices[e.item];
            if (e.id == "doku" && e.item >= 0 && e.item != k_.world.texMode) {
                k_.beginTextureTier(e.item);
                loading_ = true;
                loadFraction_ = 0.0f;
                loadWhat_.clear();
                return;   // from the next frame on: the bar
            }
            k_.applyQuality();
            if (e.id == "ornek" && k_.quality.baseGot < k_.quality.base)
                host.toast(L("Kart belleği " + std::to_string(k_.quality.base) + " örneğe yetmedi: " + std::to_string(k_.quality.baseGot) +
                                 " kullanılıyor (dokuyu düşürünce yer açılır)",
                             "Not enough GPU memory for " + std::to_string(k_.quality.base) + " samples: using " + std::to_string(k_.quality.baseGot) +
                                 " (lower textures free room)"));
            resetAccum();
        }
        if (!paced_) {   // the first running frame: free (Ozcan 2026-10-05: "kare serbest")
            host.pace(0.0f);
            paced_ = true;
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
        const bool out = k_.blackedOut();   // the convulsion and the blackout: no swimming, no looking
        k_.stepDiver(out ? 0.0f : fwd, out ? 0.0f : right, out ? 0.0f : up, in.shift, out ? 0.0f : mdx, out ? 0.0f : mdy, dt);
        k_.stepBreath(paused_ ? 0.0f : dt, in.shift && (fwd != 0.0f || right != 0.0f || up != 0.0f));
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
        if (loading_) {
            s.chips.push_back({L("Doku yükleniyor", "Textures loading"), true});
            s.busy = loadFraction_;
            s.busyTitle = L("Doku değişiyor · ", "Textures changing · ") + tierName(k_.world.texMode);
            s.busyLabel = loadWhat_.empty() ? L("Hazırlanıyor", "Getting ready") : roleName(loadWhat_) + L(" çözülüyor", " decoding");
        }
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
            c.items = {"1", "4", "8", "16", "32"};
            for (int k = 0; k < 5; ++k)
                if (kBaseChoices[k] == k_.quality.baseGot) c.active = k;
            c.detail = L("Piksel başına sabit örnek (her yerde); 32: tamponu 1,9 GB, saniyede bir iki kare",
                         "Fixed samples a pixel (everywhere); 32: a 1.9 GB buffer, a frame or two a second");
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
            c.detail = L("Kenardaki piksele toplam örnek (örnekten az olamaz)", "Samples in all for a pixel on an edge (never fewer than the samples)");
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "doku";
            c.title = L("Doku", "Textures");
            c.items = {L("Düşük", "Low"), L("Normal", "Normal"), L("Yüksek", "High")};
            c.active = k_.world.texMode;
            c.detail = L("Düşük 1K · Normal 2K · Yüksek 8K, kum ve ayrıntı katmanı 16K; yalnız seçilen kartta durur",
                         "Low 1K · Normal 2K · High 8K, the sand and the detail layer 16K; only the one chosen stays on the card");
            c.section = true;
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
    static std::string tierName(int t) { return t == 0 ? L("Düşük", "Low") : t == 1 ? L("Normal", "Normal") : L("Yüksek", "High"); }
    static std::string tierToast(int t) {
        return t == 0   ? L("Doku: düşük (her şey 1K, ayrıntı katmanı dahil)", "Textures: low (everything 1K, the detail layer too)")
               : t == 1 ? L("Doku: normal (her şey 2K)", "Textures: normal (everything 2K)")
                        : L("Doku: yüksek (taramalar 8K, kum ve ayrıntı katmanı 16K)", "Textures: high (the scans 8K, the sand and the detail layer 16K)");
    }
    // a set's folder (assets/koy/cc0) in words
    static std::string roleName(const std::string& f) {
        const bool k16 = f.find("16k") != std::string::npos, k8 = f.find("8k") != std::string::npos;
        const std::string size = k16 ? " (16K)" : k8 ? " (8K)" : "";
        if (f.rfind("kum2", 0) == 0) return L("İkinci kum", "The second sand") + size;
        if (f.rfind("kum", 0) == 0) return L("Kum", "Sand") + size;
        if (f.rfind("kaya_ayrinti", 0) == 0) return L("Kaya ayrıntısı (atlas)", "Rock detail (atlas)") + size;
        if (f.rfind("kaya", 0) == 0) return L("Kaya", "Rock") + size;
        if (f.rfind("amfora", 0) == 0) return L("Amfora kili", "The jars' clay") + size;
        if (f.rfind("kabuk", 0) == 0) return L("Deniz kabuğu", "Sea crust") + size;
        if (f.rfind("ahsap", 0) == 0) return L("Ahşap", "Wood") + size;
        if (f.rfind("beton", 0) == 0) return L("Beton", "Concrete") + size;
        return f;
    }
    app::SceneInfo info_ = describe();
    KoyApp k_;
    uchar4* pinned_ = nullptr;
    double t_ = 0.0;
    bool paused_ = false, look_ = false, paced_ = false;
    bool loading_ = false;          // another texture tier on its way: the picture held
    float loadFraction_ = 0.0f;
    std::string loadWhat_;
    int accum_ = 0;
};

}   // namespace

namespace koy {
app::SceneInfo info() { return KoyLive::describe(); }
std::unique_ptr<app::Scene> make() { return KoyLive::make(); }
int cli(int argc, char** argv) { return runKoyCli(argc, argv); }
}   // namespace koy
