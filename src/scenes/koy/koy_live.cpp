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
        i.description = L("Küçük bir adanın beton iskelesinin altındaki karanlık salonda başlarsın. Dubaların çizdiği yolda kum "
                          "derinleşir: kayalara yaslanmış amforalar, yarı yıkık bir kalyon, en uçta maviye inen bir şamandıra halatı. "
                          "Yüzeye çıkamazsın; fazla derinde oksijen seni zehirler.",
                          "You start in a dark hall under a small island's concrete pier. Along a lane of buoys the sand deepens: "
                          "amphorae against the rocks, a half-fallen galleon, at the end a buoy's rope going down into the blue. "
                          "You cannot surface; dive too deep and the oxygen poisons you.");
        i.details = {
            {L("Sabit örnekli süper örnekleme", "Fixed-pattern supersampling"),
             L("Her piksel, her karede aynı noktalardan alınan sabit örneklerle çizilir (varsayılan 4). Karede rastgele sayı yoktur, "
               "önceki karelerden de bir şey taşınmaz; bu yüzden görüntü kıpırdamaz ve hareket edince bulanmaz. Örnekleri birbirini "
               "tutmayan kenar piksellerine toplam 12 örnek verilir ve hepsi 1,5 piksel yarıçaplı bir Blackman-Harris süzgeciyle "
               "birleştirilir. TAA ya da DLSS gibi zamansal bir yöntem kullanılmaz.",
               "Every pixel is drawn from the same fixed set of samples every frame (4 by default). A frame uses no random numbers "
               "and carries nothing over from earlier frames, so the image neither shimmers nor smears in motion. Edge pixels, whose "
               "samples disagree, get 12 samples in all, combined with a Blackman-Harris filter of 1.5 pixel radius. No temporal "
               "method such as TAA or DLSS is used.")},
            {L("Dalgalar ve kostik", "Waves and caustics"),
             L("Deniz yüzeyi her karede FFT ile hesaplanır: 4 m/s'lik bir esintinin dalgaları. Kostik için yüzeydeki 1024×1024 "
               "hücrenin her biri bir güneş demeti sayılır; kırılan her demetin kapladığı alan 20 derinlik diliminde tam olarak "
               "hesaplanır. Hiçbir şey sayılmadığı için kostikte gürültü yoktur.",
               "The sea surface is computed every frame with an FFT: the waves of a 4 m/s breeze. For the caustics every cell of a "
               "1024×1024 grid on the surface is a beam of sunlight, and the area each refracted beam covers is computed exactly in "
               "20 depth slices. Nothing is counted, so the caustics have no noise.")},
            {L("Gölgeler ve karanlık salon", "Shadows and the dark hall"),
             L("Güneşin gölgesi su yüzeyinden sonra havada da sürer: iskelenin tahta aralıklarından ve beton başlığın kırık "
               "deliklerinden sızan ışık, kumda ve suda dalgayla salınır. Başlığın altındaki salonun ışığı açılışta önceden "
               "hesaplanır; gökten gelen ışık orada kesilir. Su üstü yalnız Snell penceresinden, suyun altından gökyüzünün "
               "göründüğü daireden görünür.",
               "The sun's shadow carries on through the air above the water: light leaking between the pier's boards and through "
               "the broken holes of its concrete head sways with the waves on the sand and in the water. The light of the hall under "
               "the head is computed at load time; the sky's light is cut off there. Above the water is seen only through Snell's "
               "window, the circle of sky visible from below.")},
            {L("Tayfsal su ve göz", "Spectral water and the eye"),
             L("Suyun soğurması ve saçılması 380-780 nm arasında 5 nm adımla, yayımlanmış ölçümlerden hesaplanır: saf su Pope ve "
               "Fry 1997, parçacıklar Jerlov IB tipi (Solonenko ve Mobley 2015), güneş ASTM G173, göz CIE 1931. Renkler derinlikle "
               "kendiliğinden maviye döner. Göz, çevresinin parlaklığına ve rengine yarı yolda uyar.",
               "The water's absorption and scattering are computed every 5 nm from 380 to 780 nm from published measurements: pure "
               "water from Pope and Fry 1997, particles from Jerlov type IB (Solonenko and Mobley 2015), the sun from ASTM G173, the "
               "eye from CIE 1931. Colours turn blue with depth on their own. The eye adapts halfway to the brightness and colour "
               "around it.")},
            {L("Derinlik, basınç ve oksijen", "Depth, pressure and oxygen"),
             L("Kum adadan uzaklaştıkça derinleşir: iskelenin ucunda 3,5 m, amforalarda 12 m, batıkta 22 m, ötede 100 m'yi aşar. "
               "Havadaki oksijenin kısmi basıncı (ppO₂) 1,6 bar'ı, yani yaklaşık 66 m'yi geçince zehirlenme başlar: görüş daralır, "
               "kararır ve iskele merdiveninde yeniden başlarsın. Gerçekte dakikalar süren etki burada saniyelere kısaltıldı.",
               "The sand deepens away from the island: 3.5 m at the end of the pier, 12 m at the amphorae, 22 m at the wreck, past "
               "100 m beyond. When the oxygen's partial pressure in the air (ppO₂) passes 1.6 bar, about 66 m, toxicity sets in: the "
               "view narrows, goes black, and you start again at the pier's ladder. What takes minutes in reality takes seconds here.")},
            {L("Batık ve amforalar", "The wreck and the amphorae"),
             L("38 metrelik kalyon, gövdenin biçimini veren tek bir fonksiyondan kereste kereste kurulur: postalar, kaplama, iki "
               "güverte, kasaralar, pencereli ayna, toplar, kırık direkler, çapa. Başı aşağıda, sancağa yatık durur; içine "
               "girebilirsin. Amforaların dört tipi müze çizimlerinden (ArchAIDE, Southampton Üniversitesi, CC BY 4.0), dokular "
               "CC0 taramalardan (ambientCG, Poly Haven).",
               "The 38 m galleon is built timber by timber from one function that gives the hull its shape: frames, planking, two "
               "decks, castles, a windowed stern, guns, broken masts, an anchor. It lies bow down, listing to starboard, and you can "
               "swim inside. The four amphora types come from museum drawings (ArchAIDE, University of Southampton, CC BY 4.0), the "
               "textures from CC0 scans (ambientCG, Poly Haven).")}};
        i.tags = {L("Sabit süper örnekleme", "Fixed supersampling"), L("Kostik demetleri", "Beam caustics"),
                  L("Tayfsal su optiği", "Spectral water optics")};
        i.image = "assets/gallery/koy.jpg";
        i.accent = 0x4FB8C8;
        i.keys = {{L("Dalgıç", "Diver"),
                   {{"W|A|S|D", L("Yüz", "Swim")}, {L("Boşluk", "Space"), L("Yukarı çık", "Rise")}, {"C", L("Aşağı in", "Sink")},
                    {"Shift", L("Güçlü palet vuruşu (basılı tut)", "Hold for a strong kick")},
                    {"Tab", L("Fareyle bakışı aç / kapat", "Mouse look on / off")},
                    {L("Sağ tık", "Right click"), L("Basılı tutup bak", "Hold to look")}}},
                  {L("Sahne", "Scene"),
                   {{"P", L("Zamanı durdur / sürdür", "Pause / resume time")},
                    {"E", L("Kenar piksellerini göster / gizle", "Show / hide the edge pixels")},
                    {"K", L("Kostiği aç / kapat", "Caustics on / off")}, {"↑|↓", L("Pozlamayı artır / azalt", "Exposure up / down")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<KoyLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Işın izleme programları derleniyor", "Compiling the ray-tracing programs");
            st.fraction = 0.05f;
            ++st.step;
            return;
        case 1:
            k_.root = host.root();
            k_.overrides = host.overrides();
            k_.loadConfig();
            k_.renderer.init(k_.width, k_.height, k_.root);
            if (!k_.compile()) { st.error = L("Işın izleme programları derlenemedi (ayrıntı konsolda).", "The ray-tracing programs did not compile (see the console)."); return; }
            st.label = L("Dünya kuruluyor, suyun ışığı hesaplanıyor", "Building the world, computing the water's light");
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
                host.toast(L("Ekran kartı belleği " + std::to_string(k_.quality.base) + " örneğe yetmedi: " + std::to_string(k_.quality.baseGot) +
                                 " kullanılıyor (dokuyu düşürmek yer açar)",
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
            host.toast(paused_ ? L("Zaman durdu: kıpırdamazsan piksel başına 256 örnekli bir kare çizilir", "Time paused: hold still for a frame of 256 samples per pixel")
                               : L("Zaman yeniden akıyor", "Time runs again"));
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
            host.toast(L("Oksijen zehirlenmesi: ppO₂ 1,6 bar'ı aştı (yaklaşık 66 m). Merdivende yeniden başladın",
                         "Oxygen toxicity: ppO₂ passed 1.6 bar (about 66 m). You start again at the ladder"));
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
        if (paused_) s.chips.push_back({accum_ ? L("Durdu · 256 örnek", "Paused · 256 samples") : L("Zaman durdu", "Time paused"), true});
        if (look_) s.chips.push_back({L("Fareyle bakış", "Mouse look")});
        if (k_.renderer.params().view == 1) s.chips.push_back({L("Kenar pikselleri", "Edge pixels"), true});
        if (loading_) {
            s.chips.push_back({L("Doku yükleniyor", "Textures loading"), true});
            s.busy = loadFraction_;
            s.busyTitle = L("Doku değişiyor · ", "Textures changing · ") + tierName(k_.world.texMode);
            s.busyLabel = loadWhat_.empty() ? L("Hazırlanıyor", "Getting ready") : roleName(loadWhat_) + L(" açılıyor", " being decoded");
        }
        if (!k_.caustics) s.chips.push_back({L("Kostik kapalı", "Caustics off"), true});
        s.metrics.push_back({L("Derinlik", "Depth"), num(-k_.diver.pos.y, 1) + " m"});
        s.metrics.push_back({L("Basınç", "Pressure"), num(k_.pressureBar(), 2) + " bar"});
        s.metrics.push_back({L("Oksijen (ppO₂)", "Oxygen (ppO₂)"), num(k_.ppO2(), 2) + " bar"});
        if (k_.ppO2() >= k_.cfg.f("o2.limit", 1.6f))
            s.chips.push_back({L("ppO₂ 1,6 üstü: zehirlenme", "ppO₂ above 1.6: toxicity"), true});
        else if (k_.ppO2() >= 1.4f)
            s.chips.push_back({L("ppO₂ 1,4 üstü: dalış sınırı", "ppO₂ above 1.4: dive limit"), true});
        s.metrics.push_back({L("Deniz ve kostik", "Sea and caustics"), num(tm.world, 1) + " ms"});
        s.metrics.push_back({L("Işık sütunları", "Light shafts"), num(tm.shafts, 1) + " ms"});
        s.metrics.push_back({L("Örnekler", "Samples"), num(tm.base, 1) + " ms"});
        s.metrics.push_back({L("Kenar", "Edges"), num(tm.edge, 1) + " ms · " + theme::thousands(tm.edgePixels)});
        s.metrics.push_back({L("Süzgeç ve ton eşleme", "Filter and tone map"), num(tm.resolve + tm.final, 1) + " ms"});
        s.metrics.push_back({L("Pozlama", "Exposure"), (k_.evAdapt >= 0 ? "+" : "") + num(k_.evAdapt, 2) + " EV"});
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "ornek";
            c.title = L("Piksel örneği", "Pixel samples");
            c.items = {"1", "4", "8", "16", "32"};
            for (int k = 0; k < 5; ++k)
                if (kBaseChoices[k] == k_.quality.baseGot) c.active = k;
            c.detail = L("Her pikselin sabit örneği; 32'de 1,9 GB tampon, sığmazsa daha az",
                         "Fixed samples for every pixel; 32 needs a 1.9 GB buffer, else fewer");
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "kenar";
            c.title = L("Kenar örneği", "Edge samples");
            c.items = {"4", "8", "12", "16", "32"};
            for (int k = 0; k < 5; ++k)
                if (kEdgeChoices[k] == k_.quality.edge) c.active = k;
            c.detail = L("Kenardaki bir pikselin toplam örneği (piksel örneğinden az olmaz)",
                         "Samples in all for a pixel on an edge (never fewer than pixel samples)");
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "doku";
            c.title = L("Doku", "Textures");
            c.items = {L("Düşük", "Low"), L("Normal", "Normal"), L("Yüksek", "High")};
            c.active = k_.world.texMode;
            c.detail = L("Düşük 1K, normal 2K, yüksek 8K; yüksekte kum ve kaya ayrıntısı 16K",
                         "Low 1K, normal 2K, high 8K; on high the sand and rock detail are 16K");
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
        return t == 0   ? L("Doku: düşük (hepsi 1K, kaya ayrıntısı dahil)", "Textures: low (all 1K, the rock detail too)")
               : t == 1 ? L("Doku: normal (hepsi 2K)", "Textures: normal (all 2K)")
                        : L("Doku: yüksek (taramalar 8K, kum ve kaya ayrıntısı 16K)", "Textures: high (scans 8K, the sand and rock detail 16K)");
    }
    // a set's folder (assets/koy/cc0) in words
    static std::string roleName(const std::string& f) {
        const bool k16 = f.find("16k") != std::string::npos, k8 = f.find("8k") != std::string::npos;
        const std::string size = k16 ? " (16K)" : k8 ? " (8K)" : "";
        if (f.rfind("kum2", 0) == 0) return L("Kabuk kırıntılı kum", "Shell-grit sand") + size;
        if (f.rfind("kum", 0) == 0) return L("Kum", "Sand") + size;
        if (f.rfind("kaya_ayrinti", 0) == 0) return L("Kaya ayrıntısı", "Rock detail") + size;
        if (f.rfind("kaya", 0) == 0) return L("Kaya", "Rock") + size;
        if (f.rfind("amfora", 0) == 0) return L("Amfora kili", "Amphora clay") + size;
        if (f.rfind("kabuk", 0) == 0) return L("Kabuk örtüsü", "Encrusting coat") + size;
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
