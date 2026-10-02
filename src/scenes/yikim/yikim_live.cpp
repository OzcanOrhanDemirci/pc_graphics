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

using app::L;

class YikimLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "yikim";
        i.number = "03";
        i.title = L("Yıkım", "Collapse");
        i.subtitle = L("Betonarme yapılar, deprem ve kırılma", "Concrete buildings, earthquakes and fracture");
        i.description = L("Üç betonarme yapı: beş katlı bir apartman, otuz katlı bir gökdelen, beşik çatılı bir ev. 3'ten 9'a kadar "
                          "depremlerle sarsılır, çatlar, gerekirse çöker; tıkladığın yere bir yıkım topu çarpar.",
                          "Three reinforced-concrete buildings: a five-storey apartment block, a thirty-storey tower and a house with a "
                          "gable roof. Earthquakes of magnitude 3 to 9 shake them, crack them and, if strong enough, bring them down; "
                          "a wrecking ball hits wherever you click.");
        i.details = {
            {L("Kendi fizik çözücümüz", "Our own physics solver"),
             L("Oyun motoru ya da hazır fizik kütüphanesi yok: ekran kartında XPBD, karede 40 alt adım. Her parçanın kendi kabuğu, "
               "kütlesi ve eylemsizliği var: apartman 3.006, gökdelen 8.404, ev 456 parça.",
               "No game engine and no ready-made physics library: XPBD on the graphics card, 40 substeps a frame. Every piece has "
               "its own hull, mass and inertia: 3,006 in the apartment block, 8,404 in the tower, 456 in the house.")},
            {L("Beton ve donatı", "Concrete and steel"),
             L("Parçalar liflerle birbirine bağlı. Beton çekmede çatlar, basınçta ezilir; donatı akar ve 6 cm uzayınca kopar. "
               "Kapasiteyi aşan kısım kalıcı olur: mafsallaşan kolon eğik kalır. Üç yapının da malzemesi aynı.",
               "The pieces are bonded by fibres. Concrete cracks in tension and crushes in compression; the bars yield and snap "
               "after 6 cm of stretch. What exceeds the capacity stays: a column that formed a hinge stays bent. All three "
               "buildings are made of the same materials.")},
            {L("Depremler", "Earthquakes"),
             L("Her büyüklük gerçek bir kuvvetli yer hareketi kaydından (AFAD, ESM): o büyüklüğün süresine kesilir, tepe ivmesine "
               "ölçeklenir, iki kez integre edilip zeminin hareketine çevrilir. Daha uzun ya da sürekli sarsıntıda kaydın güçlü "
               "kısmı dikişsiz bir döngüye dönüşür.",
               "Each magnitude comes from a real strong-motion record (AFAD, ESM): cut to that magnitude's length, scaled to its "
               "peak acceleration and integrated twice into the ground's motion. For longer or endless shaking the record's "
               "strong part becomes a seamless loop.")},
            {L("Yıkım topu", "Wrecking ball"),
             L("Tıklanan yere bakış yönünden döküm çelik bir top gelir. Gücü çubukla: yarım ton ve saatte 29 km'den, çapı 5 m, "
               "500 ton ve saatte 400 km'lik bir göktaşına. Top da öteki parçalar gibi bir cisim; neyin kırılıp devrileceğine "
               "çözücü karar verir.",
               "A cast steel ball comes from where you look to where you click. Its power is a slider: from half a tonne at 29 "
               "km/h to a meteor 5 m across, 500 tonnes at 400 km/h. The ball is a body like the others; the solver decides what "
               "breaks and topples.")},
            {L("Aynı deprem, aynı yıkım", "Same quake, same collapse"),
             L("Temas ve bağ toplamları kayan noktalı atomikler yerine sabit noktada tutulur: aynı deprem aynı binayı her seferinde "
               "aynı biçimde yıkar. Sınırdaki yapıyı ölçebilmek bunu gerektirdi.",
               "Contact and bond sums are kept in fixed point instead of floating-point atomics: the same earthquake brings the "
               "same building down the same way every time. Measuring a structure at its limit required it.")},
            {L("Görüntü", "The picture"),
             L("OptiX ile ışın izleme: yumuşak gölgeli güneş, gök ve zeminden bir sekme. Piksel başına altı örnek; gren, her parçanın "
               "hareketini izleyen kendi süzgecimizle temizlenir (1080p'de 2,4 ms).",
               "Ray tracing with OptiX: a sun with soft shadows, the sky and one bounce off the ground. Six samples a pixel; the "
               "grain is cleaned by our own filter, which follows each piece's motion (2.4 ms at 1080p).")}};
        i.tags = {L("Kendi fizik çözücüsü", "Own physics solver"), L("Gerçek deprem kayıtları", "Real earthquake records"), L("Canlı", "Live")};
        i.image = "assets/gallery/yikim.jpg";
        i.accent = 0xE5764A;
        i.keys = {{L("Deprem", "Earthquake"),
                   {{"3–9", L("Büyüklüğü seç", "Choose the magnitude")},
                    {"←|→", L("Süre: kısalt / uzat (en sonda: durdurana kadar)", "Length: shorter / longer (at the end: until stopped)")},
                    {L("Boşluk", "Space"), L("Depremi başlat / durdur", "Start / stop the earthquake")},
                    {"T", L("Ağır çekim: ×1, ×0,25, ×0,1", "Slow motion: ×1, ×0.25, ×0.1")}, {"P", L("Zamanı durdur", "Pause time")}}},
                  {L("Müdahale", "Intervene"),
                   {{L("Sol tık", "Left click"), L("Yıkım topu at (saniyede en çok bir)", "Throw a wrecking ball (at most one a second)")},
                    {"↑|↓", L("Topun gücü", "The ball's power")}, {"R", L("Binayı baştan kur", "Rebuild the building")}}},
                  {L("Kamera", "Camera"),
                   {{L("Sağ tık|Fare", "Right click|Mouse"), L("Bak", "Look")}, {"W|A|S|D", L("Uç", "Fly")},
                    {"E|Q", L("Yukarı / aşağı", "Up / down")}, {"Shift", L("Hızlı", "Fast")}, {L("Tekerlek", "Wheel"), L("Yakınlaş", "Zoom")}}},
                  {L("Görüntü", "Picture"),
                   {{"B", L("Donatıyı göster / gizle", "Show / hide the bars")},
                    {"G", L("Gürültü giderici: süzgeç, yapay zekâ, ham", "Denoising: filter, AI, raw")}}}};
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<YikimLive>(); }

    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0: {
            st.label = L("Ekran kartı ve ışın izleme programları hazırlanıyor", "Preparing the graphics card and the ray-tracing programs");
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
            if (!y_.renderer.compileDevice(log)) { st.error = L("Işın izleme programı derlenemedi.\n", "The ray-tracing program did not compile.\n") + log; return; }
            st.label = L("Bina kuruluyor ve kendi ağırlığıyla oturuyor", "Building, and letting it settle under its own weight");
            st.fraction = 0.3f;
            ++st.step;
            return;
        }
        case 2: {
            y_.build();
            st.label = L("Görüntü hazırlanıyor", "Preparing the picture");
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
            power_ = std::min(1.0f, std::max(0.0f, y_.cfg.f("darbe.guc", 0.5f)));
            for (int b = 0; b < kBuildings; ++b)
                if (y_.building() == buildingKey(b)) building_ = b;
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
            if (newBuilding_) {   // another building: its own view
                y_.setCamera();
                const YCamera& cam = y_.renderer.params().cam;
                fly_.lookAt(cam.pos, cam.pos + cam.fwd);
                newBuilding_ = false;
            }
            camMoved_ = true;
            host.toast(buildingName(building_) + std::string(L(" kuruldu", " is up")));
        }
        // keys
        if (in.pressed(GLFW_KEY_G)) {
            y_.renderer.denoiseMode = (y_.renderer.denoiseMode + 1) % 3;
            host.toast(y_.renderer.denoiseMode == 0   ? L("Gürültü giderici kapalı: ham görüntü", "Denoising off: the raw picture")
                       : y_.renderer.denoiseMode == 1 ? L("Gürültü giderici: hızlı süzgeç", "Denoising: the fast filter")
                                                      : L("Gürültü giderici: yapay zekâ (en temiz, fiziği yavaşlatır)",
                                                          "Denoising: AI (cleanest, slows the physics)"));
        }
        if (in.pressed(GLFW_KEY_P)) { paused_ = !paused_; host.toast(paused_ ? L("Zaman durdu", "Time paused") : L("Zaman akıyor", "Time runs")); }
        if (in.pressed(GLFW_KEY_B)) {
            y_.renderer.showBars = !y_.renderer.showBars;
            camMoved_ = true;
            host.toast(y_.renderer.showBars ? L("Donatı görünüyor", "Bars shown") : L("Donatı gizli", "Bars hidden"));
        }
        if (in.pressed(GLFW_KEY_T)) {
            speedIdx_ = (speedIdx_ + 1) % 3;
            host.toast(speedIdx_ == 0 ? L("Gerçek hız", "Real speed") : speedIdx_ == 1 ? L("Ağır çekim ×0,25", "Slow motion ×0.25") : L("Ağır çekim ×0,1", "Slow motion ×0.1"));
        }
        // the quake: keys and the panel's controls do the same things
        for (int m = quakeMagnitudeMin(); m <= quakeMagnitudeMax(); ++m)
            if (in.pressed(GLFW_KEY_0 + m) || in.pressed(GLFW_KEY_KP_0 + m)) chooseMagnitude(host, m);
        if (in.pressed(GLFW_KEY_LEFT)) stepLength(-1);
        if (in.pressed(GLFW_KEY_RIGHT)) stepLength(+1);
        if (in.pressed(GLFW_KEY_UP)) setPower(power_ + 0.1f);
        if (in.pressed(GLFW_KEY_DOWN)) setPower(power_ - 0.1f);
        if (in.pressed(GLFW_KEY_SPACE)) startOrStop(host);
        for (const app::ControlEvent& e : in.events) {
            if (e.id == "buyukluk") chooseMagnitude(host, quakeMagnitudeMin() + e.item);
            else if (e.id == "sure") lengthFromSlider(e.value);
            else if (e.id == "deprem") startOrStop(host);
            else if (e.id == "top") setPower(e.value);
            else if (e.id == "yapi") chooseBuilding(host, e.item);
        }
        if (in.pressed(GLFW_KEY_R) && rebuildIn_ == 0) {
            rebuildIn_ = 2;
            host.toast(L("Bina yeniden kuruluyor…", "Rebuilding…"));
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
        // a ball at most once a second (wall clock: slow motion does not stretch it), so that clicks do not pile up
        if (in.leftClicked && in.view.w > 0.0f && in.now - lastThrow_ >= kThrowEvery) {   // the cursor in render pixels
            const float rx = (in.mouseX - in.view.x) / in.view.w * y_.width, ry = (in.mouseY - in.view.y) / in.view.h * y_.height;
            if (rx >= 0 && ry >= 0 && rx < y_.width && ry < y_.height) {
                float4 hit;
                if (y_.renderer.pick(rx, ry, hit) > 0) {
                    y_.strike(y_.renderer.params().cam.pos, make_float3(hit.x, hit.y, hit.z), power_);
                    lastThrow_ = in.now;
                    std::printf("yikim topu atildi: guc %.2f, t=%.2f s\n", power_, in.now);
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
        if (y_.quakeOn) s.chips.push_back({L("Deprem sürüyor", "Earthquake"), true});
        if (paused_) s.chips.push_back({L("Zaman durdu", "Time paused"), true});
        if (speedIdx_) s.chips.push_back({speedIdx_ == 1 ? L("Ağır çekim ×0,25", "Slow motion ×0.25") : L("Ağır çekim ×0,1", "Slow motion ×0.1"), true});
        if (y_.renderer.denoiseMode != 1) s.chips.push_back({y_.renderer.denoiseMode == 0 ? L("Ham görüntü", "Raw picture") : L("Yapay zekâ gürültü giderici", "AI denoiser")});
        if (!y_.renderer.showBars) s.chips.push_back({L("Donatı gizli", "Bars hidden")});
        s.metrics.push_back({L("Fizik", "Physics"), num(y_.world.lastMs, 1) + " ms"});
        s.metrics.push_back({L("Görüntü", "Picture"), num(y_.renderer.lastMs, 1) + " ms"});
        s.metrics.push_back({L("Kırık bağ lifi", "Broken fibres"), theme::thousands(unsigned(stats_.broken)) + " / " + theme::thousands(unsigned(stats_.fibres))});
        s.metrics.push_back({L("Zaman", "Time"), num(y_.world.time, 1) + L(" sn", " s")});
        {   // which building stands
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "yapi";
            c.title = L("Yapı", "Building");
            for (int b = 0; b < kBuildings; ++b) c.items.push_back(buildingName(b));
            c.active = building_;
            c.detail = buildingNote(building_);
            s.controls.push_back(c);
        }
        {   // the ball's power: size and speed together; the notch is the balanced throw
            const YikimApp::BallThrow bt = y_.ballThrow(power_);
            app::Control c;
            c.kind = app::Control::Kind::Slider;
            c.id = "top";
            c.title = L("Top", "Ball");
            c.section = true;
            c.value = power_;
            c.mark = 0.5f;
            c.text = num(bt.mass / 1000.0, bt.mass < 1000.0f ? 2 : 1) + " t";
            c.detail = std::string(powerName(power_)) + L(" · saatte ", " · ") + num(bt.speed * 3.6, 0) + L(" km · çap ", " km/h · ") + num(2.0 * bt.radius, 1) +
                       L(" m", " m across");
            s.controls.push_back(c);
        }
        {   // the magnitude
            app::Control c;
            c.section = true;
            c.kind = app::Control::Kind::Choice;
            c.id = "buyukluk";
            c.title = L("Büyüklük", "Magnitude");
            for (int m = quakeMagnitudeMin(); m <= quakeMagnitudeMax(); ++m) c.items.push_back(std::to_string(m));
            c.active = magnitude_ - quakeMagnitudeMin();
            if (const QuakeMagnitude* q = quakeMagnitude(magnitude_))
                c.detail = std::string(magnitudeName(magnitude_)) + L(" · tepe ivme ", " · peak acceleration ") + num(q->pgaG, q->pgaG < 0.1 ? 3 : 2) + " g";
            s.controls.push_back(c);
        }
        const double own = quakeOwnSeconds(magnitude_);
        {   // how long: the notch is the magnitude's own length, the far end "until stopped"
            app::Control c;
            c.kind = app::Control::Kind::Slider;
            c.id = "sure";
            c.title = L("Süre", "Length");
            c.value = length_ == Length::Endless ? 1.0f : sliderOf(seconds());
            c.mark = sliderOf(own);
            c.text = length_ == Length::Endless ? std::string("∞") : num(seconds(), 0) + L(" sn", " s");
            c.detail = length_ == Length::Own       ? std::string(L("Bu büyüklüğün kendi süresi", "This magnitude's own length"))
                       : length_ == Length::Endless ? std::string(L("Durdurana kadar sürer", "Until you stop it"))
                                                    : L("Kendi süresi ", "Its own length: ") + num(own, 0) + L(" sn", " s");
            const bool differs = y_.quakeOn && (y_.quake.endless() != (length_ == Length::Endless) ||
                                                (!y_.quake.endless() && std::fabs(y_.quake.length - seconds()) > 0.5) || y_.quakeMag != magnitude_);
            if (differs && !y_.quake.stopping()) c.detail += L(" · bir sonraki depremde", " · from the next earthquake");
            s.controls.push_back(c);
        }
        {   // start, stop
            app::Control c;
            c.kind = app::Control::Kind::Button;
            c.id = "deprem";
            c.keys = L("Boşluk", "Space");
            if (!y_.quakeOn) { c.text = L("Depremi başlat", "Start the earthquake"); c.strong = true; }
            else if (!y_.quake.stopping()) c.text = L("Depremi durdur", "Stop the earthquake");
            else { c.text = L("Sarsıntı diniyor", "The shaking dies down"); c.enabled = false; }
            s.controls.push_back(c);
        }
        if (y_.quakeOn) {
            const double t = y_.quakeTime(y_.world.time);
            const std::string head = L("Sarsıntı · büyüklük ", "Shaking · magnitude ") + std::to_string(y_.quakeMag) + " · ";
            if (y_.quake.endless()) {   // no end: a travelling light; dying down: the bar empties over the fade
                s.progress = y_.quake.stopping() ? float(1.0 - std::min(1.0, (t - y_.quake.stopAt) / QuakePlay::kStopFade)) : 0.0f;
                s.progressEndless = !y_.quake.stopping();
                s.progressLabel = head + num(t, 0) + L(" sn", " s") + (y_.quake.stopping() ? L(" · diniyor", " · dying down") : L(" · durdurana kadar", " · until stopped"));
            } else {
                const double p = y_.quake.progress(t);
                s.progress = float(p);
                s.progressLabel = head + num(p * y_.quake.length, 1) + " / " + num(y_.quake.length, 0) + L(" sn", " s") +
                                  (y_.quake.stopping() ? L(" · diniyor", " · dying down") : "");
            }
        }
    }

    void screenshot(app::Host& host) override {
        const std::string path = host.capturePath() + ".png";
        writePNG(path, int(y_.width), int(y_.height), reinterpret_cast<const unsigned char*>(pinned_));
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
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
            host.toast(L("Büyüklük ", "Magnitude ") + std::to_string(m) + L(": süren deprem diniyor", ": the earthquake in progress dies down"));
        }
    }
    void startOrStop(app::Host& host) {
        if (y_.quakeOn) {
            if (!y_.quake.stopping()) y_.stopQuake(y_.world.time);
            return;
        }
        if (y_.startQuake(magnitude_, seconds(), y_.world.time))
            host.toast(L("Deprem: büyüklük ", "Earthquake: magnitude ") + std::to_string(magnitude_) + " · " + magnitudeName(magnitude_) +
                       (seconds() < 0.0 ? std::string(L(" · durdurana kadar", " · until stopped")) : " · " + theme::num(seconds(), 0) + L(" sn", " s")));
    }

    app::SceneInfo info_ = describe();
    YikimApp y_;
    FlyCamera fly_;
    uchar4* pinned_ = nullptr;
    unsigned spp_ = 6;
    int magnitude_ = 6, speedIdx_ = 0, frameNo_ = 0, rebuildIn_ = 0;
    bool paused_ = false, looking_ = false, camMoved_ = true;
    double tick_ = 0.0, lastStats_ = -1.0;
    static const char* magnitudeName(int m) {   // how a magnitude feels
        switch (m) {
        case 3: return L("Zayıf", "Weak");
        case 4: return L("Hafif", "Light");
        case 5: return L("Orta", "Moderate");
        case 6: return L("Kuvvetli", "Strong");
        case 7: return L("Şiddetli", "Severe");
        case 8: return L("Yıkıcı", "Destructive");
        default: return L("Felaket", "Catastrophic");
        }
    }
    // the buildings, in the panel's order, by their settings' names (YikimApp::building)
    static constexpr int kBuildings = 3;
    static const char* buildingKey(int b) { return b == 1 ? "gokdelen" : b == 2 ? "ev" : "apartman"; }
    static const char* buildingName(int b) { return b == 1 ? L("Gökdelen", "Tower") : b == 2 ? L("Ev", "House") : L("Apartman", "Apartments"); }
    static const char* buildingNote(int b) {
        return b == 1   ? L("30 kat, 97 m · camlı lobi", "30 storeys, 97 m · glass lobby")
               : b == 2 ? L("Tek kat, beşik çatı", "One storey, gable roof")
                        : L("5 kat · zemin katta dükkânlar", "5 storeys · shops on the ground floor");
    }
    int building_ = 0;
    bool newBuilding_ = false;
    void chooseBuilding(app::Host& host, int b) {
        if (b < 0 || b >= kBuildings || b == building_ || rebuildIn_ > 0) return;
        building_ = b;
        y_.cfg.set("yapi", buildingKey(b));
        newBuilding_ = true;
        rebuildIn_ = 2;
        host.toast(buildingName(b) + std::string(L(" kuruluyor…", ": building…")));
    }
    float power_ = 0.5f;                       // the ball's power (YikimApp::ballThrow), 0..1
    double lastThrow_ = -10.0;                 // when the last ball went (input time, s)
    static constexpr double kThrowEvery = 1.0;
    void setPower(float u) {   // near the notch it is the balanced throw
        power_ = std::min(1.0f, std::max(0.0f, u));
        if (std::fabs(power_ - 0.5f) < 0.03f) power_ = 0.5f;
    }
    static const char* powerName(float u) {   // what a throw of this power did to the corner column (docs/yikim/DURUM.md)
        return u < 0.2f ? L("Hafif", "Light") : u < 0.45f ? L("Orta", "Medium") : u <= 0.55f ? L("Dengeli", "Balanced") : u < 0.85f ? L("Ağır", "Heavy") : L("Göktaşı", "Meteor");
    }
    PhysStats stats_;
    static constexpr float kSpeeds[3] = {1.0f, 0.25f, 0.1f};
};

}   // namespace

namespace yikim {
app::SceneInfo info() { return YikimLive::describe(); }
std::unique_ptr<app::Scene> make() { return YikimLive::make(); }
int cli(int argc, char** argv) { return runYikimCli(argc, argv); }
}   // namespace yikim
