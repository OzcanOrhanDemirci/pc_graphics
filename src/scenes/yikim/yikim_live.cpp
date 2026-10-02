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
        i.description = "Beş katlı betonarme bir apartman. 3'ten 9'a kadar depremlerle sarsılır, çatlar, gerekirse çöker; tıkladığın yere bir yıkım "
                        "topu çarpar.";
        i.details = {
            {"Kendi fizik çözücümüz", "Oyun motoru ya da hazır fizik kütüphanesi yok: ekran kartında XPBD, karede 40 alt adım. Bina 3.007 katı "
                                      "parçadan kurulu; her birinin kendi kabuğu, kütlesi ve eylemsizliği var."},
            {"Beton ve donatı", "Parçalar 44.378 lifle birbirine bağlı. Beton çekmede çatlar, basınçta ezilir; donatı akar ve 6 cm uzayınca "
                                "kopar. Kapasiteyi aşan kısım kalıcı olur: mafsallaşan kolon eğik kalır."},
            {"Depremler", "Her büyüklük gerçek bir kuvvetli yer hareketi kaydından (AFAD, ESM): o büyüklüğün süresine kesilir, tepe ivmesine "
                          "ölçeklenir, iki kez integre edilip zeminin hareketine çevrilir. Daha uzun ya da sürekli sarsıntıda kaydın güçlü "
                          "kısmı dikişsiz bir döngüye dönüşür."},
            {"Yıkım topu", "Tıklanan yere bakış yönünden döküm çelik bir top gelir: 4,1 ton (Ctrl ile 11,4 ton), saatte 50 km. Top da öteki "
                           "parçalar gibi bir cisim; neyin kırılıp devrileceğine çözücü karar verir."},
            {"Aynı deprem, aynı yıkım", "Temas ve bağ toplamları kayan noktalı atomikler yerine sabit noktada tutulur: aynı deprem aynı binayı "
                                       "her seferinde aynı biçimde yıkar. Sınırdaki yapıyı ölçebilmek bunu gerektirdi."},
            {"Görüntü", "OptiX ile ışın izleme: yumuşak gölgeli güneş, gök ve zeminden bir sekme. Piksel başına altı örnek; gren, her parçanın "
                        "hareketini izleyen kendi süzgecimizle temizlenir (1080p'de 2,4 ms)."}};
        i.tags = {"Kendi fizik çözücüsü", "Gerçek deprem kayıtları", "Canlı"};
        i.image = "assets/gallery/yikim.jpg";
        i.accent = 0xE5764A;
        i.keys = {{"Deprem", {{"3–9", "Büyüklüğü seç"}, {"←|→", "Süre: kısalt / uzat (en sonda: durdurana kadar)"}, {"Boşluk", "Depremi başlat / durdur"},
                              {"T", "Ağır çekim: ×1, ×0,25, ×0,1"}, {"P", "Zamanı durdur"}}},
                  {"Müdahale", {{"Sol tık", "Yıkım topu (4,1 ton)"}, {"Ctrl|Sol tık", "Ağır top (11,4 ton)"}, {"R", "Binayı baştan kur"}}},
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
        // the quake: keys and the panel's controls do the same things
        for (int m = quakeMagnitudeMin(); m <= quakeMagnitudeMax(); ++m)
            if (in.pressed(GLFW_KEY_0 + m) || in.pressed(GLFW_KEY_KP_0 + m)) chooseMagnitude(host, m);
        if (in.pressed(GLFW_KEY_LEFT)) stepLength(-1);
        if (in.pressed(GLFW_KEY_RIGHT)) stepLength(+1);
        if (in.pressed(GLFW_KEY_SPACE)) startOrStop(host);
        for (const app::ControlEvent& e : in.events) {
            if (e.id == "buyukluk") chooseMagnitude(host, quakeMagnitudeMin() + e.item);
            else if (e.id == "sure") lengthFromSlider(e.value);
            else if (e.id == "deprem") startOrStop(host);
        }
        if (in.pressed(GLFW_KEY_R) && rebuildIn_ == 0) {
            rebuildIn_ = 2;
            host.toast("Bina yeniden kuruluyor…");
        }
        // mouse: right button looks, left button throws a wrecking ball at what is under it, the wheel zooms
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
                if (y_.renderer.pick(rx, ry, hit) > 0) y_.strike(y_.renderer.params().cam.pos, make_float3(hit.x, hit.y, hit.z), in.ctrl);
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
        {   // the magnitude
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "buyukluk";
            c.title = "Büyüklük";
            for (int m = quakeMagnitudeMin(); m <= quakeMagnitudeMax(); ++m) c.items.push_back(std::to_string(m));
            c.active = magnitude_ - quakeMagnitudeMin();
            if (const QuakeMagnitude* q = quakeMagnitude(magnitude_))
                c.detail = std::string(q->label) + " · tepe ivme " + num(q->pgaG, q->pgaG < 0.1 ? 3 : 2) + " g";
            s.controls.push_back(c);
        }
        const double own = quakeOwnSeconds(magnitude_);
        {   // how long: the notch is the magnitude's own length, the far end "until stopped"
            app::Control c;
            c.kind = app::Control::Kind::Slider;
            c.id = "sure";
            c.title = "Süre";
            c.value = length_ == Length::Endless ? 1.0f : sliderOf(seconds());
            c.mark = sliderOf(own);
            c.text = length_ == Length::Endless ? "∞" : num(seconds(), 0) + " sn";
            c.detail = length_ == Length::Own       ? "Bu büyüklüğün kendi süresi"
                       : length_ == Length::Endless ? "Durdurana kadar sürer"
                                                    : "Kendi süresi " + num(own, 0) + " sn";
            const bool differs = y_.quakeOn && (y_.quake.endless() != (length_ == Length::Endless) ||
                                                (!y_.quake.endless() && std::fabs(y_.quake.length - seconds()) > 0.5) || y_.quakeMag != magnitude_);
            if (differs && !y_.quake.stopping()) c.detail += " · bir sonraki depremde";
            s.controls.push_back(c);
        }
        {   // start, stop
            app::Control c;
            c.kind = app::Control::Kind::Button;
            c.id = "deprem";
            c.keys = "Boşluk";
            if (!y_.quakeOn) { c.text = "Depremi başlat"; c.strong = true; }
            else if (!y_.quake.stopping()) c.text = "Depremi durdur";
            else { c.text = "Sarsıntı diniyor"; c.enabled = false; }
            s.controls.push_back(c);
        }
        if (y_.quakeOn) {
            const double t = y_.quakeTime(y_.world.time);
            const std::string head = "Sarsıntı · büyüklük " + std::to_string(y_.quakeMag) + " · ";
            if (y_.quake.endless()) {   // no end: a travelling light; dying down: the bar empties over the fade
                s.progress = y_.quake.stopping() ? float(1.0 - std::min(1.0, (t - y_.quake.stopAt) / QuakePlay::kStopFade)) : 0.0f;
                s.progressEndless = !y_.quake.stopping();
                s.progressLabel = head + num(t, 0) + " sn" + (y_.quake.stopping() ? " · diniyor" : " · durdurana kadar");
            } else {
                const double p = y_.quake.progress(t);
                s.progress = float(p);
                s.progressLabel = head + num(p * y_.quake.length, 1) + " / " + num(y_.quake.length, 0) + " sn" + (y_.quake.stopping() ? " · diniyor" : "");
            }
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
    // the next quake's length: the magnitude's own (the notch), one chosen on the slider, or until stopped
    enum class Length { Own, Chosen, Endless };
    Length length_ = Length::Own;
    double chosen_ = 0.0;
    // the slider: seconds on a log scale over most of the track, "until stopped" at its far end
    static constexpr double kShortest = 2.0, kLongest = 180.0;
    static constexpr float kSpan = 0.9f;   // the share of the track the seconds take
    static float sliderOf(double s) {
        return float(kSpan * std::log(std::max(s, kShortest) / kShortest) / std::log(kLongest / kShortest));
    }
    double seconds() const {   // < 0: until stopped
        return length_ == Length::Endless ? -1.0 : length_ == Length::Chosen ? chosen_ : quakeOwnSeconds(magnitude_);
    }
    void setLength(double s) {   // seconds, rounded; at the magnitude's own: Own
        const double own = quakeOwnSeconds(magnitude_);
        if (std::fabs(sliderOf(s) - sliderOf(own)) < 0.02f) { length_ = Length::Own; return; }
        length_ = Length::Chosen;
        chosen_ = s < 60.0 ? std::round(s) : 5.0 * std::round(s / 5.0);
    }
    void lengthFromSlider(float u) {
        if (u > 0.5f * (kSpan + 1.0f)) { length_ = Length::Endless; return; }
        setLength(kShortest * std::pow(kLongest / kShortest, std::min(u, kSpan) / kSpan));
    }
    void stepLength(int dir) {   // the arrows: through round lengths and the magnitude's own; past the longest, until stopped
        static const double kStops[] = {2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 25, 30, 40, 50, 60, 75, 90, 120, 150, 180};
        std::vector<double> stops(std::begin(kStops), std::end(kStops));
        stops.push_back(quakeOwnSeconds(magnitude_));
        std::sort(stops.begin(), stops.end());
        if (length_ == Length::Endless) {
            if (dir < 0) setLength(kLongest);
            return;
        }
        const double now = seconds();
        if (dir > 0) {
            for (double s : stops)
                if (s > now + 1e-6) { setLength(s); return; }
            length_ = Length::Endless;
        } else {
            for (auto it = stops.rbegin(); it != stops.rend(); ++it)
                if (*it < now - 1e-6) { setLength(*it); return; }
        }
    }
    void chooseMagnitude(app::Host& host, int m) {
        if (m == magnitude_ || !quakeMagnitude(m)) return;
        magnitude_ = m;
        if (length_ == Length::Chosen) setLength(chosen_);   // a chosen length that is this magnitude's own becomes its own
        if (y_.quakeOn && !y_.quake.stopping()) {
            y_.stopQuake(y_.world.time);
            host.toast("Büyüklük " + std::to_string(m) + ": süren deprem diniyor");
        }
    }
    void startOrStop(app::Host& host) {
        if (y_.quakeOn) {
            if (!y_.quake.stopping()) y_.stopQuake(y_.world.time);
            return;
        }
        if (y_.startQuake(magnitude_, seconds(), y_.world.time))
            host.toast("Deprem: büyüklük " + std::to_string(magnitude_) + " · " + quakeMagnitude(magnitude_)->label +
                       (seconds() < 0.0 ? std::string(" · durdurana kadar") : " · " + theme::num(seconds(), 0) + " sn"));
    }

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
