// Scene 4 "Laboratuvar" inside the application: five experiments on a high-speed photography table.
//   live       the coarse world runs; honey and sand are only this (their flowing is the scene)
//   recorded   water poured, the balloon pricked, the melon shot: the trigger plays at once on the coarse world (a
//              preview, marked so), while a fine world computes the same trigger on a worker thread; what is computed
//              can be watched at once (the time bar fills like a video loading); when it is done it plays once at
//              real speed, then it is free to look at: scrub, slow down to 1/10 or 1/100, pause, fly round it.
// No video is made: what is kept is the physics, drawn live from any view.
#include "app/lang.h"
#include "app/scene.h"
#include "app/theme.h"
#include "core/image_io.h"
#include "scenes/laboratuvar/lab_record.h"
#include "scenes/laboratuvar/laboratuvar_app.h"
#include "scenes/laboratuvar/weapons.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

#include <cstdio>
#include <memory>

namespace {

using app::L;

std::string deneyName(Deney d) {
    switch (d) {
    case Deney::Su: return L("Su", "Water");
    case Deney::Bal: return L("Bal", "Honey");
    case Deney::Kum: return L("Kum", "Sand");
    case Deney::Balon: return L("Su balonu", "Water balloon");
    case Deney::Karpuz: return L("Karpuz", "Watermelon");
    case Deney::Jel: return L("Jel", "Gel");
    case Deney::Damla: return L("Süt damlası", "Milk drop");
    }
    return "";
}
std::string deneyNote(Deney d) {
    switch (d) {
    case Deney::Su: return L("Beher bardağa dökülür · kayıt", "A beaker poured into a glass · recorded");
    case Deney::Bal: return L("Kaşıktan cam tabağa · canlı · fareyle gezdir", "From a dipper onto a glass dish · live · drag to move");
    case Deney::Kum: return L("Huniden masaya ve behere · canlı · fareyle gezdir", "From a funnel onto the table and a beaker · live · drag to move");
    case Deney::Balon: return L("Balona tıkla: iğne · kayıt", "Click the balloon: a needle · recorded");
    case Deney::Karpuz: return L("Karpuza tıkla: tüfek · kayıt", "Click the melon: a rifle · recorded");
    case Deney::Jel: return L("Balistik jel bloğu, bloğa tıkla: tabanca · kayıt", "A ballistic gel block, click it: a pistol · recorded");
    case Deney::Damla: return L("İnce süt katmanına bir damla: taç · kayıt", "A drop into a thin layer of milk: the crown · recorded");
    }
    return "";
}

const float kSpeeds[3] = {1.0f, 0.1f, 0.01f};

class LabLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "laboratuvar";
        i.number = "04";
        i.title = L("Laboratuvar", "Laboratory");
        i.subtitle = L("Sıvı ve tanecik fiziği, ağır çekim", "Liquids and grains in slow motion");
        i.description = L("Bir ağır çekim fotoğraf masası: bardağa dökülen su, kaşıktan akan bal, huniden dökülen kum, patlayan su balonu, "
                          "vurulan karpuz, balistik jel. Fizik ne derse o; hızlı anlar ince hesaplanır, istediğin hızda ve açıdan izlenir.",
                          "A high-speed photography table: water poured into a glass, honey off a dipper, sand from a funnel, a water "
                          "balloon popping, a watermelon shot, ballistic gel. Whatever the physics says; fast moments are computed finely and watched "
                          "at any speed, from any side.");
        i.details = {
            {L("Tek çözücü", "One solver"),
             L("Her şey tanecik: su ve bal yoğunluk kısıtıyla (PBF), kum sürtünmeli tanelerle, lastik ve karpuz kopabilen bağlarla (XPBD). "
               "Bağları kopan karpuz eti suyuna döner.",
               "Everything is particles: water and honey by a density constraint (PBF), sand by grains with friction, rubber and "
               "watermelon by breakable bonds (XPBD). Melon flesh that loses its bonds turns to juice.")},
            {L("Kayıt", "Recording"),
             L("Tetik kaba dünyada hemen oynar; aynı girdilerle ince dünya (1,5-2,5 mm tanecik) arka planda saniyenin 1/600'ü ile "
               "1/1000'i aralıklarla hesaplanır. Video yok: saklanan fiziktir, her açıdan canlı çizilir.",
               "The trigger plays at once on a coarse world; a fine world (1.5-2.5 mm particles) computes the same inputs in the "
               "background, kept every 1/600 to 1/1000 of a second. No video: what is kept is the physics, drawn live from any view.")},
            {L("Yüzeyler", "Surfaces"),
             L("Sıvıların yüzeyi her karede taneciklerinden kurulur (anizotropik çekirdekler); tane görünmez.",
               "Every frame each liquid's surface is built from its particles (anisotropic kernels); no grain shows.")},
            {L("Cam ve ışık", "Glass and light"),
             L("OptiX ile yol izleme: kalınlıklı cam, kırılan su ve bal, kehribar soğurma; arkadan yumuşak panel, sıvı fotoğrafçılığının "
               "klasik ışığı.",
               "Path tracing with OptiX: thick glass, refracting water and honey, amber absorption; a soft panel behind, the classic "
               "light of liquid photography.")},
            {L("Gerçek ölçüler", "Real numbers"),
             L("Tüfek yükleri üretici tablolarından, dokuda balistik jele kalibre; suyun hacmi korunur, kum 30-35° yığılır.",
               "Rifle loads from manufacturers' tables, calibrated in tissue to ballistic gelatin; water keeps its volume, sand piles "
               "at 30-35°.")}};
        i.tags = {L("Sıvı fiziği", "Fluid physics"), L("Ağır çekim", "Slow motion"), L("Yol izleme", "Path tracing")};
        i.image = "assets/gallery/laboratuvar.jpg";
        i.accent = 0x6FC2B4;
        i.reloadable = true;
        i.keys = {{L("Deney", "Experiment"),
                   {{"1-7", L("Su, bal, kum, balon, karpuz, jel, süt damlası", "Water, honey, sand, balloon, melon, gel, milk drop")},
                    {L("Boşluk", "Space"), L("Dök / akıt / durdur", "Pour / run / stop")},
                    {L("Sol tık", "Left click"), L("İğne, ateş; balda ve kumda sürükle: gezdir", "Needle, fire; honey and sand: drag to move")},
                    {"B", L("Kuma çelik bilye bırak", "Drop a steel ball on the sand")},
                    {"R", L("Yeniden kur (canlıya dön)", "Set up again (back to live)")}}},
                  {L("Zaman", "Time"),
                   {{"T", L("Hız: ×1, ×1/10, ×1/100", "Speed: ×1, ×1/10, ×1/100")},
                    {"P", L("Durdur / oynat", "Pause / play")},
                    {"← →", L("Kayıtta geri / ileri", "Back / on in a recording")}}},
                  {L("Kamera", "Camera"),
                   {{L("Sağ tık|Fare", "Right click|Mouse"), L("Bak", "Look")},
                    {"W A S D", L("Baktığın yöne git", "Move where you look")},
                    {"E Q", L("Yukarı / aşağı", "Up / down")},
                    {"Shift", L("Hızlı", "Fast")},
                    {"C", L("Kamerayı deneye döndür", "Camera back to the experiment")}}}};
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<LabLive>(); }
    LabLive() : info_(describe()) {}

    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Ekran kartı ve ışın izleme programı hazırlanıyor", "Preparing the graphics card and the ray-tracing program");
            st.fraction = 0.05f;
            ++st.step;
            return;
        case 1: {
            a_.root = host.root();
            a_.overrides = host.overrides();
            a_.loadConfig();
            a_.width = unsigned(a_.cfg.i("display.width", 1920));
            a_.height = unsigned(a_.cfg.i("display.height", 1080));
            std::string log;
            if (!a_.initRenderer(log)) { st.error = L("Işın izleme programı derlenemedi.\n", "The ray-tracing program did not compile.\n") + log; return; }
            for (LabInputs& i : in_) i = LabInputs{};
            in_[int(Deney::Su)].height = a_.cfg.f("su.yukseklik_baslangic", 0.25f);
            in_[int(Deney::Bal)].height = 0.15f;   // low: the honey reaches its pile before a drop pinches off
            in_[int(Deney::Bal)].flow = 0.6f;
            in_[int(Deney::Kum)].height = 0.4f;
            in_[int(Deney::Kum)].flow = 0.5f;
            st.label = L("Deney kuruluyor: su, beher, bardak", "Setting up the experiment: water, a beaker, a glass");
            st.fraction = 0.35f;
            ++st.step;
            return;
        }
        case 2:
            buildWorld(Deney(std::min(kDeneyCount - 1, std::max(0, a_.cfg.i("deney.ilk", 0)))));
            st.label = L("Görüntü hazırlanıyor", "Preparing the picture");
            st.fraction = 0.85f;
            ++st.step;
            return;
        default:
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(a_.width) * a_.height * sizeof(uchar4));
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        const float realDt = float(std::min(0.1, in.dt));
        if (rebuildIn_ > 0 && --rebuildIn_ == 0) {
            buildWorld(pending_);
            host.toast(deneyName(deney_));
        }
        // ---- keys and the panel
        for (int k = 0; k < kDeneyCount; ++k)
            if (in.pressed(GLFW_KEY_1 + k) || in.pressed(GLFW_KEY_KP_1 + k)) choose(host, Deney(k));
        if (in.pressed(GLFW_KEY_R)) choose(host, deney_, true);
        if (in.pressed(GLFW_KEY_SPACE)) mainAction(host);
        if (in.pressed(GLFW_KEY_B) && deney_ == Deney::Kum) dropBall(host);
        if (in.pressed(GLFW_KEY_T)) setSpeed(host, (speedIdx_ + 1) % 3);
        if (in.pressed(GLFW_KEY_P)) togglePause(host);
        if (in.pressed(GLFW_KEY_C)) { a_.setCamera(deney_); moved_ = true; }
        if (mode_ == Mode::Playback) {
            const double step = (in.shift ? 0.1 : 0.01);
            if (in.pressed(GLFW_KEY_RIGHT)) { playT_ = std::min(rec_.computed(), playT_ + step); playing_ = false; changed_ = true; }
            if (in.pressed(GLFW_KEY_LEFT)) { playT_ = std::max(0.0, playT_ - step); playing_ = false; changed_ = true; }
        }
        for (const app::ControlEvent& e : in.events) {
            if (e.id == "deney") choose(host, Deney(e.item));
            else if (e.id == "eylem") mainAction(host);
            else if (e.id == "bilye") dropBall(host);
            else if (e.id == "yeniden") choose(host, deney_, true);
            else if (e.id == "hiz") setSpeed(host, e.item);
            else if (e.id == "durdur") togglePause(host);
            else if (e.id == "yukseklik") { in_[int(deney_)].height = e.value; if (e.released || deney_ != Deney::Su) inputsChanged(); }
            else if (e.id == "akis") { in_[int(deney_)].flow = e.value; inputsChanged(); }
            else if (e.id == "guc") in_[int(deney_)].power = e.value;
            else if (e.id == "silah") in_[int(deney_)].gun = e.item;
            else if (e.id == "zaman" && rec_.active() && rec_.framesDone() > 0) {
                mode_ = Mode::Playback;
                playing_ = false;
                firstPlayDone_ = true;
                playT_ = std::min(rec_.computed(), double(e.value) * rec_.duration());
                changed_ = true;
            }
        }
        // a Su change of height rebuilds the setup (the beaker moves before the pour only)
        // ---- the camera
        LabCam& cam = a_.cam;
        if (in.rightDown && (in.mouseDX != 0.0f || in.mouseDY != 0.0f)) {
            cam.look(in.mouseDX, in.mouseDY, a_.cfg.f("kamera.fare", 0.003f));
            moved_ = true;
        }
        {
            const float f = (in.isDown(GLFW_KEY_W) ? 1.0f : 0.0f) - (in.isDown(GLFW_KEY_S) ? 1.0f : 0.0f);
            const float r = (in.isDown(GLFW_KEY_D) ? 1.0f : 0.0f) - (in.isDown(GLFW_KEY_A) ? 1.0f : 0.0f);
            const float u = (in.isDown(GLFW_KEY_E) ? 1.0f : 0.0f) - (in.isDown(GLFW_KEY_Q) ? 1.0f : 0.0f);
            if (f != 0.0f || r != 0.0f || u != 0.0f) {
                cam.move(f, r, u, realDt * (in.shift ? 0.9f : 0.25f));
                moved_ = true;
            }
            if (in.scroll != 0.0f) {
                cam.move(in.scroll > 0 ? 1.0f : -1.0f, 0.0f, 0.0f, 0.03f * std::fabs(in.scroll));
                moved_ = true;
            }
        }
        if (moved_) {   // autofocus: on what is in the middle of the picture
            cam.apply(a_.renderer.params().cam, a_.width, a_.height);
            const LPick c = a_.renderer.pick(0.5f * a_.width, 0.5f * a_.height);
            if (c.object >= 0 && c.t < 5.0f) cam.focus = c.t * std::max(0.2f, dot(c.dir, cam.forward()));
        }
        cam.apply(a_.renderer.params().cam, a_.width, a_.height);
        // ---- the mouse on the experiment
        if (rebuildIn_ == 0 && in.view.w > 0.0f) {
            const float rx = (in.mouseX - in.view.x) / in.view.w * a_.width, ry = (in.mouseY - in.view.y) / in.view.h * a_.height;
            const bool inside = rx >= 0 && ry >= 0 && rx < a_.width && ry < a_.height;
            if (inside && in.leftDown && (deney_ == Deney::Bal || deney_ == Deney::Kum) && mode_ == Mode::Live) {
                // the stream follows the cursor over the table (where the camera's ray meets the table, limited)
                const LCamera& C = a_.renderer.params().cam;
                const float u = (rx / a_.width) * 2.0f - 1.0f, v = 1.0f - (ry / a_.height) * 2.0f;
                const float3 d = normalize(C.fwd + C.right * (u * C.tanHalfX) + C.up * (v * C.tanHalfY));
                if (d.y < -1e-3f) {
                    const float t = -C.pos.y / d.y;
                    float3 p = C.pos + d * t;
                    p.x = std::max(-0.25f, std::min(0.25f, p.x));
                    p.z = std::max(-0.15f, std::min(0.15f, p.z));
                    LabInputs& li = in_[int(deney_)];
                    // at most 0.4 m/s: a hand
                    float3 dv = make_float3(p.x - li.nozzle.x, 0.0f, p.z - li.nozzle.z);
                    const float l = length(dv), mx = 0.4f * realDt;
                    if (l > mx) dv = dv * (mx / l);
                    li.nozzle = li.nozzle + dv;
                    w_.setInputs(li);
                }
            }
            if (inside && in.leftClicked && in.now - lastClick_ > 0.3 && mode_ == Mode::Live) {
                const LPick p = a_.renderer.pick(rx, ry);
                std::printf("tik: nesne %d, hedef %d, (%.3f %.3f %.3f)\n", p.object, a_.isTarget(p.object) ? 1 : 0, p.pos.x, p.pos.y, p.pos.z);
                if (deney_ == Deney::Balon && a_.isTarget(p.object)) {
                    const float3 c = a_.cfg.v3("balon.merkez", make_float3(0.0f, 0.20f, 0.0f));
                    if (length(p.pos - c) < a_.cfg.f("balon.yaricap", 0.06f) * 1.6f) {
                        LabEvent e;
                        e.kind = LabEvent::Prick;
                        e.a = p.pos;
                        startRecorded(host, e);
                        lastClick_ = in.now;
                    }
                } else if ((deney_ == Deney::Karpuz || deney_ == Deney::Jel) && a_.isTarget(p.object)) {
                    if (length(p.pos - w_.aim) < w_.aimRadius) {
                        LabEvent e;
                        e.kind = LabEvent::Shot;
                        e.a = a_.renderer.params().cam.pos;
                        e.b = p.pos;
                        e.power = in_[int(deney_)].power;
                        e.gun = deney_ == Deney::Jel ? 0 : in_[int(deney_)].gun;
                        startRecorded(host, e);
                        lastClick_ = in.now;
                    }
                }
            }
        }
        // ---- time
        const float speed = kSpeeds[speedIdx_];
        if (mode_ != Mode::Playback && !paused_ && rebuildIn_ == 0) {
            w_.step((1.0f / 60.0f) * speed);
            changed_ = true;
        }
        if (rec_.active()) {
            if (rec_.failed()) {
                host.toast(L("İnce hesap başarısız oldu", "The fine pass failed"));
                rec_.release();
            } else if (mode_ == Mode::Preview && !rec_.computing() && rec_.framesDone() > 0) {
                // the fine pass is done: play it once at real speed, in full
                mode_ = Mode::Playback;
                playT_ = 0.0;
                playing_ = true;
                firstPlayDone_ = false;
                speedIdx_ = 0;
                host.toast(L("İnce kayıt hazır: gerçek hızda oynuyor", "The fine recording is ready: playing at real speed"));
            }
            if (mode_ == Mode::Playback && playing_ && !paused_) {
                playT_ += realDt * (firstPlayDone_ ? kSpeeds[speedIdx_] : 1.0f);
                if (playT_ >= rec_.duration() - 1e-6) {
                    playT_ = rec_.duration();
                    if (!rec_.computing()) {
                        playing_ = false;
                        if (!firstPlayDone_) host.toast(L("Kayıt bitti: zaman çubuğuyla gez, hızı seç, kamerayla dolaş", "Done: scrub the time bar, pick a speed, fly round"));
                        firstPlayDone_ = true;
                    }
                }
                playT_ = std::min(playT_, rec_.computed());   // what is computed so far (a video loading)
                changed_ = true;
            }
        }
        // ---- draw
        drawNow();
        const bool still = !changed_ && !moved_;
        const unsigned spp = unsigned(std::max(1, a_.cfg.i("display.spp", 4)));
        // every frame is blended into the last ones (reprojected through the camera); while nothing changes the blend
        // becomes a plain mean, and the picture converges
        a_.renderer.still = still;
        a_.renderer.render(spp, false);
        a_.renderer.downloadTo(pinned_);
        changed_ = moved_ = false;
    }

    const void* image(int& w, int& h) override {
        w = int(a_.width);
        h = int(a_.height);
        return pinned_;
    }

    void status(app::Status& s) override {
        using theme::num;
        // what is on screen
        if (mode_ == Mode::Preview) s.chips.push_back({L("Kaba önizleme", "Coarse preview"), true});
        else if (mode_ == Mode::Playback) s.chips.push_back({firstPlayDone_ ? L("İnce kayıt", "Fine recording") : L("İnce kayıt · gerçek hız", "Fine recording · real speed"), true});
        else s.chips.push_back({L("Canlı", "Live"), false});
        if (rec_.computing()) s.chips.push_back({L("İnce hesap %", "Fine pass ") + num(100.0 * rec_.framesDone() / std::max(1, rec_.framesTotal()), 0) + L("", "%")});
        if (paused_) s.chips.push_back({L("Zaman durdu", "Time paused"), true});
        if (speedIdx_) s.chips.push_back({speedIdx_ == 1 ? L("×1/10", "×1/10") : L("×1/100", "×1/100"), true});
        s.metrics.push_back({L("Fizik", "Physics"), num(w_.pw.lastStepMs(), 1) + " ms"});
        s.metrics.push_back({L("Görüntü", "Picture"), num(a_.renderer.lastMs, 1) + " ms"});
        if (mode_ == Mode::Playback) s.metrics.push_back({L("Tanecik", "Particles"), theme::thousands(unsigned(rec_.drawnCount()))});
        else s.metrics.push_back({L("Tanecik", "Particles"), theme::thousands(unsigned(w_.count()))});
        if (rec_.active() && rec_.computing()) s.metrics.push_back({L("İnce hesap", "Fine pass"), num(rec_.msPerFrame() / (rec_.frameDt() * 1000.0), 1) + L(" kat yavaş", "x slower")});
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "deney";
            c.title = L("Deney", "Experiment");
            for (int k = 0; k < kDeneyCount; ++k) c.items.push_back(deneyName(Deney(k)));
            c.active = int(deney_);
            c.detail = deneyNote(deney_);
            s.controls.push_back(c);
        }
        const LabInputs& li = in_[int(deney_)];
        if (deney_ == Deney::Damla) {
            app::Control c;
            c.kind = app::Control::Kind::Slider;
            c.id = "yukseklik";
            c.title = L("Yükseklik", "Height");
            c.section = true;
            c.value = li.height;
            const float h = a_.cfg.f("damla.yukseklik_en_az", 0.05f) + li.height * (a_.cfg.f("damla.yukseklik_en_cok", 0.5f) - a_.cfg.f("damla.yukseklik_en_az", 0.05f));
            c.text = num(h * 100.0, 0) + " cm";
            c.detail = L("Damlanın düştüğü yükseklik: çarpma hızı ", "The drop falls from here: it lands at ") + num(std::sqrt(2.0 * 9.81 * h), 1) + L(" m/sn", " m/s");
            s.controls.push_back(c);
        }
        if (deney_ == Deney::Su) {
            app::Control c;
            c.kind = app::Control::Kind::Slider;
            c.id = "yukseklik";
            c.title = L("Yükseklik", "Height");
            c.section = true;
            c.value = li.height;
            const float h = a_.cfg.f("su.yukseklik_en_az", 0.12f) + li.height * (a_.cfg.f("su.yukseklik_en_cok", 0.45f) - a_.cfg.f("su.yukseklik_en_az", 0.12f));
            c.text = num(h * 100.0, 0) + " cm";
            c.detail = L("Beherin ağzı bardağın üstünde · bırakınca yeniden kurulur", "The beaker's lip over the glass · rebuilt when let go");
            s.controls.push_back(c);
        }
        if (deney_ == Deney::Bal || deney_ == Deney::Kum) {
            const bool honey = deney_ == Deney::Bal;
            const std::string pre = honey ? "bal." : "kum.";
            app::Control c;
            c.kind = app::Control::Kind::Slider;
            c.id = "akis";
            c.title = L("Akış", "Flow");
            c.section = true;
            c.value = li.flow;
            const float q = a_.cfg.f(pre + "akis_en_az", honey ? 0.5f : 3.0f) + li.flow * (a_.cfg.f(pre + "akis_en_cok", honey ? 5.0f : 25.0f) - a_.cfg.f(pre + "akis_en_az", honey ? 0.5f : 3.0f));
            c.text = num(q, 1) + L(" mL/sn", " mL/s");
            s.controls.push_back(c);
            app::Control h;
            h.kind = app::Control::Kind::Slider;
            h.id = "yukseklik";
            h.title = L("Yükseklik", "Height");
            h.value = li.height;
            const float y = a_.cfg.f(pre + "yukseklik_en_az", honey ? 0.05f : 0.10f) + li.height * (a_.cfg.f(pre + "yukseklik_en_cok", honey ? 0.32f : 0.30f) - a_.cfg.f(pre + "yukseklik_en_az", honey ? 0.05f : 0.10f));
            h.text = num(y * 100.0, 0) + " cm";
            s.controls.push_back(h);
        }
        if (deney_ == Deney::Karpuz) {   // which gun: a pistol cracks it open, a rifle blows it apart
            app::Control g;
            g.kind = app::Control::Kind::Choice;
            g.id = "silah";
            g.title = L("Silah", "Gun");
            g.section = true;
            g.items = {L("Tabanca", "Pistol"), L("Tüfek", "Rifle")};
            g.active = li.gun;
            s.controls.push_back(g);
        }
        if (deney_ == Deney::Karpuz || deney_ == Deney::Jel) {
            const bool pistol = deney_ == Deney::Jel || li.gun == 0;
            const weapons::Load L0 = weapons::gunLoad(pistol ? weapons::Gun::Pistol : weapons::Gun::Rifle, li.power);
            app::Control c;
            c.kind = app::Control::Kind::Slider;
            c.id = "guc";
            c.title = L("Mermi", "Round");
            c.section = deney_ == Deney::Jel;
            c.value = li.power;
            c.mark = 0.5f;
            c.text = L0.name;
            c.detail = num(L0.shot.mass * 1000.0, 1) + " g · " + num(L0.shot.velocity, 0) + L(" m/sn · ", " m/s · ") + num(L0.energy(), 0) + " J";
            s.controls.push_back(c);
        }
        {   // the main action
            app::Control c;
            c.kind = app::Control::Kind::Button;
            c.id = "eylem";
            c.keys = L("Boşluk", "Space");
            c.section = deney_ == Deney::Balon;
            c.strong = true;
            if (deney_ == Deney::Su) { c.text = L("Dök", "Pour"); c.enabled = mode_ == Mode::Live && !w_.triggered(); }
            else if (deney_ == Deney::Bal || deney_ == Deney::Kum) c.text = li.flowing ? L("Durdur", "Stop") : (deney_ == Deney::Bal ? L("Akıt", "Run") : L("Dök", "Pour"));
            else if (deney_ == Deney::Balon) { c.text = L("İğne batır", "Prick it"); c.enabled = mode_ == Mode::Live; }
            else if (deney_ == Deney::Damla) { c.text = L("Damla bırak", "Let a drop fall"); c.enabled = mode_ == Mode::Live; }
            else { c.text = L("Ateş et (ortadan)", "Fire (through the middle)"); c.enabled = mode_ == Mode::Live; }
            s.controls.push_back(c);
        }
        if (deney_ == Deney::Kum) {
            app::Control c;
            c.kind = app::Control::Kind::Button;
            c.id = "bilye";
            c.keys = "B";
            c.text = L("Çelik bilye bırak", "Drop a steel ball");
            s.controls.push_back(c);
        }
        if (rec_.active() && rec_.framesDone() > 0) {   // the recording's time
            app::Control c;
            c.kind = app::Control::Kind::Slider;
            c.id = "zaman";
            c.title = L("Zaman", "Time");
            c.section = true;
            const double T = rec_.duration();
            c.value = float(mode_ == Mode::Playback ? playT_ / T : rec_.computed() / T);
            c.text = num((mode_ == Mode::Playback ? playT_ : rec_.computed()) * 1000.0, 0) + " ms";
            c.detail = rec_.computing() ? L("Hesaplanan ", "Computed ") + num(rec_.computed(), 2) + " / " + num(T, 2) + L(" sn · tıkla, izle", " s · click to watch")
                                        : L("İnce kayıt: ", "Fine recording: ") + num(rec_.spacing() * 1000.0, 1) + L(" mm tanecik, saniyede ", " mm particles, ") +
                                              num(1.0 / rec_.frameDt(), 0) + L(" an", " moments a second");
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "hiz";
            c.title = L("Hız", "Speed");
            c.section = !(rec_.active() && rec_.framesDone() > 0);
            c.items = {"×1", "×1/10", "×1/100"};
            c.active = speedIdx_;
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Button;
            c.id = "durdur";
            c.keys = "P";
            c.text = paused_ || (mode_ == Mode::Playback && !playing_) ? L("Oynat", "Play") : L("Durdur", "Pause");
            s.controls.push_back(c);
            app::Control r;
            r.kind = app::Control::Kind::Button;
            r.id = "yeniden";
            r.keys = "R";
            r.text = mode_ == Mode::Live ? L("Yeniden kur", "Set up again") : L("Canlıya dön", "Back to live");
            s.controls.push_back(r);
        }
        if (mode_ == Mode::Preview) {
            s.progressLabel = L("İnce hesap", "Fine pass");
            s.progress = float(rec_.framesDone()) / float(std::max(1, rec_.framesTotal()));
        }
    }

    void screenshot(app::Host& host) override {
        const std::string path = host.capturePath() + ".png";
        writePNG(path, int(a_.width), int(a_.height), reinterpret_cast<const unsigned char*>(pinned_));
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        a_.loadConfig();
        std::string log;
        if (!a_.renderer.compileDevice(log)) { host.toast(L("Program derlenemedi", "The program did not compile")); std::printf("%s\n", log.c_str()); }
        choose(host, deney_, true);
    }

    void unload() override {
        rec_.release();
        w_.destroy();
        freeBuffers();
        a_.release();
        a_.renderer.shutdown();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }
    ~LabLive() override { unload(); }

private:
    enum class Mode { Live, Preview, Playback };
    app::SceneInfo info_;
    LaboratuvarApp a_;
    LabWorld w_;
    LabRecording rec_;
    Deney deney_ = Deney::Su, pending_ = Deney::Su;
    LabInputs in_[kDeneyCount];
    Mode mode_ = Mode::Live;
    bool paused_ = false, playing_ = false, firstPlayDone_ = false, changed_ = true, moved_ = true;
    double playT_ = 0.0, lastClick_ = -1.0;
    int speedIdx_ = 0, rebuildIn_ = 0;
    float3* dPos_ = nullptr;
    uint8_t* dCode_ = nullptr;
    int cap_ = 0;
    uchar4* pinned_ = nullptr;

    void freeBuffers() {
        if (dPos_) cudaFree(dPos_);
        if (dCode_) cudaFree(dCode_);
        dPos_ = nullptr;
        dCode_ = nullptr;
        cap_ = 0;
    }

    void buildWorld(Deney d) {
        rec_.release();
        deney_ = d;
        mode_ = Mode::Live;
        paused_ = playing_ = false;
        playT_ = 0.0;
        speedIdx_ = 0;
        w_.build(d, a_.cfg, false, in_[int(d)]);
        w_.settle(a_.cfg.f(std::string(deneyKey(d)) + ".oturma", 1.0f));
        const int cap = std::max(w_.count() + a_.cfg.i(std::string(deneyKey(d)) + ".ek_kapasite", 0), a_.cfg.i(std::string(deneyKey(d)) + ".cizim_kapasite", 0));
        freeBuffers();
        cap_ = std::max(cap, 1024);
        cudaMalloc(reinterpret_cast<void**>(&dPos_), size_t(cap_) * sizeof(float3));
        cudaMalloc(reinterpret_cast<void**>(&dCode_), size_t(cap_));
        a_.setupScene(d, cap_, &w_.membraneTris);
        a_.renderer.resetHistory();
        changed_ = moved_ = true;
        std::printf("%s\n", w_.summary().c_str());
    }

    void choose(app::Host& host, Deney d, bool again = false) {
        if (d == deney_ && !again) return;
        if (rebuildIn_ > 0) return;
        rec_.cancel();
        pending_ = d;
        rebuildIn_ = 2;
        host.toast(L("Kuruluyor: ", "Setting up: ") + deneyName(d) + "…");
    }

    void inputsChanged() {
        LabInputs& li = in_[int(deney_)];
        if (deney_ == Deney::Su) {
            if (mode_ == Mode::Live && !w_.triggered()) {   // the beaker moves: set up again at the new height
                pending_ = deney_;
                rebuildIn_ = 2;
            }
        } else {
            w_.setInputs(li);
        }
    }

    void setSpeed(app::Host& host, int k) {
        speedIdx_ = std::max(0, std::min(2, k));
        if (mode_ == Mode::Playback) firstPlayDone_ = true;
        host.toast(speedIdx_ == 0 ? L("Gerçek hız", "Real speed") : speedIdx_ == 1 ? L("Ağır çekim ×1/10", "Slow motion ×1/10") : L("Ağır çekim ×1/100", "Slow motion ×1/100"));
    }

    void togglePause(app::Host& host) {
        if (mode_ == Mode::Playback) {
            if (!playing_ && playT_ >= rec_.duration() - 1e-6) playT_ = 0.0;   // from the start again
            playing_ = !playing_;
            firstPlayDone_ = true;
            paused_ = false;
        } else {
            paused_ = !paused_;
            host.toast(paused_ ? L("Zaman durdu", "Time paused") : L("Zaman akıyor", "Time runs"));
        }
        changed_ = true;
    }

    void dropBall(app::Host& host) {
        if (deney_ != Deney::Kum || mode_ != Mode::Live) return;
        LabEvent e;
        e.kind = LabEvent::Ball;
        const float3 nz = in_[int(deney_)].nozzle;
        e.a = make_float3(nz.x + 0.0f, a_.cfg.f("kum.bilye_yukseklik", 0.28f), nz.z);
        w_.trigger(e);
        host.toast(L("Bilye bırakıldı", "A ball let go"));
    }

    void mainAction(app::Host& host) {
        if (rebuildIn_) return;
        LabInputs& li = in_[int(deney_)];
        if (deney_ == Deney::Bal || deney_ == Deney::Kum) {
            li.flowing = !li.flowing;
            w_.setInputs(li);
            return;
        }
        if (mode_ != Mode::Live) return;
        LabEvent e;
        if (deney_ == Deney::Su) {
            if (w_.triggered()) return;
            e.kind = LabEvent::Pour;
        } else if (deney_ == Deney::Damla) {
            e.kind = LabEvent::Drop;
        } else if (deney_ == Deney::Balon) {
            const float R = a_.cfg.f("balon.yaricap", 0.06f);
            const float3 m = a_.cfg.v3("balon.merkez", make_float3(0.0f, 0.20f, 0.0f));
            e.kind = LabEvent::Prick;
            e.a = m + make_float3(0.0f, -0.25f * R, 0.97f * R);   // the front, a little low
        } else {
            e.kind = LabEvent::Shot;   // from the left, through its middle
            e.a = make_float3(-1.5f, w_.aim.y, w_.aim.z);
            e.b = w_.aim;
            e.power = li.power;
            e.gun = deney_ == Deney::Jel ? 0 : li.gun;
        }
        startRecorded(host, e);
    }

    void startRecorded(app::Host& host, const LabEvent& e) {
        w_.trigger(e);
        const std::string pre = std::string(deneyKey(deney_)) + ".";
        rec_.start(a_.cfg, deney_, in_[int(deney_)], e, a_.cfg.f(pre + "kayit_sure", 1.5f), 1.0 / a_.cfg.f(pre + "kayit_hiz", 600.0f),
                   size_t(a_.cfg.f("kayit.butce_mb", 4096.0f)) << 20);
        mode_ = Mode::Preview;
        paused_ = false;
        host.toast(L("Kaba önizleme; ince hesap arka planda", "Coarse preview; the fine pass runs behind"));
    }

    void drawNow() {
        DrawInput d;
        PropPose poses[LabWorld::P_COUNT];
        if (mode_ == Mode::Playback && rec_.framesDone() > 0 && rec_.drawnCount() <= cap_) {
            if (a_.memSource != &rec_.membraneTris && !rec_.membraneTris.empty()) a_.setMembraneTris(rec_.membraneTris);
            rec_.frameAt(playT_, dPos_, dCode_, poses, a_.renderer.stream());
            d.n = rec_.drawnCount();
            d.spacing = rec_.spacing();
            d.membraneFirst = rec_.membraneFirst;
            d.membraneCount = rec_.membraneCount;
            d.ballFirst = rec_.ballFirst;
            d.ballCount = rec_.ballCount;
            d.ballRadius = rec_.ballRadius;
        } else {
            if (w_.count() > cap_) return;   // over the drawing's room (an emitter ran long): keep the last picture
            if (a_.memSource != &w_.membraneTris && !w_.membraneTris.empty()) a_.setMembraneTris(w_.membraneTris);
            w_.exportDraw(dPos_, dCode_, a_.renderer.stream());
            for (int p = 0; p < LabWorld::P_COUNT; ++p) poses[p] = w_.prop[p];
            d.n = w_.count();
            d.spacing = w_.s;
            d.membraneFirst = w_.membraneFirst;
            d.membraneCount = w_.membraneCount;
            d.ballFirst = w_.ballFirst;
            d.ballCount = w_.ballCount;
            d.ballRadius = w_.ballRadius;
        }
        d.pos = dPos_;
        d.code = dCode_;
        d.poses = poses;
        a_.draw(d);
    }
};

}   // namespace

namespace laboratuvar {
app::SceneInfo info() { return LabLive::describe(); }
std::unique_ptr<app::Scene> make() { return LabLive::make(); }
int cli(int argc, char** argv) { return runLaboratuvarCli(argc, argv); }
}   // namespace laboratuvar
