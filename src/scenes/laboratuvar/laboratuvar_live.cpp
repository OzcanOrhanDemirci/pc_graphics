// Scene 4 "Laboratuvar" inside the application: seven experiments on a high-speed photography table.
//   live       the coarse world runs, at any speed (slow motion takes the world's own substeps less often, and the
//              drawing goes between them); honey and sand are only this (their flowing is the scene)
//   recorded   water poured, the balloon pricked, the melon or the gel shot (the gel up to once a second), a drop of
//              milk: the triggers play at once on the coarse world, marked so. Asked for (the fine pass button), a
//              fine world computes the same triggers on a worker thread; what is computed can be watched at once (the
//              time bar fills like a video loading); when it is done it plays once at real speed, then it is free to
//              look at: scrub, slow down to 1/10 or 1/100, pause, fly round it; back to the coarse world as it was.
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
    case Deney::Kup: return L("Küp", "Cube");
    }
    return "";
}
std::string deneyNote(Deney d) {
    switch (d) {
    case Deney::Su: return L("Beherdeki su bardağa dökülür; ince hesap isteğe bağlı.", "A beaker of water is poured into a glass; fine pass on request.");
    case Deney::Bal: return L("Bal kaşıktan cam tabağa akar; canlı hesaplanır, fareyle sürükle.", "Honey runs from a dipper onto a glass dish; live, drag to move it.");
    case Deney::Kum: return L("Kum huniden masaya ve behere dökülür; canlı, fareyle sürükle.", "Sand pours from a funnel onto the table and a beaker; live, drag it.");
    case Deney::Balon: return L("Balona tıkla, iğne batsın; ince hesap isteğe bağlı.", "Click the balloon to prick it; fine pass on request.");
    case Deney::Karpuz: return L("Karpuza tıkla, ateş edilsin; ince hesap isteğe bağlı.", "Click the melon to shoot it; fine pass on request.");
    case Deney::Jel: return L("Balistik jel: tıkla, saniyede bir atış; ince hesap isteğe bağlı.",
                              "Ballistic gel: click to shoot, once a second; fine pass on request.");
    case Deney::Damla: return L("İnce süt katmanına düşen damla taç açar; ince hesap isteğe bağlı.", "A drop into a thin layer of milk makes a crown; fine pass on request.");
    case Deney::Kup: return L("Plastik küplerden küp, ortasında patlayıcı. Patlat ya da küpe tıkla.",
                              "A cube of plastic cubes with a charge inside. Blow it up or click it.");
    }
    return "";
}

const float kSpeeds[3] = {1.0f, 0.1f, 0.01f};
const double kCooldown = 1.0;   // seconds between two shots into the gel

class LabLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "laboratuvar";
        i.number = "04";
        i.title = L("Laboratuvar", "Laboratory");
        i.subtitle = L("Ağır çekimde sıvı, kum ve kırılan cisimler", "Liquids, sand and breaking solids in slow motion");
        i.description = L("Ağır çekim fotoğraf masasında sekiz deney: dökülen su, akan bal ve kum, patlayan su balonu, vurulan karpuz "
                          "ve jel, süte düşen damla, havaya uçan bir küp. Deneyi başlatırsın, hızlı anı istersen ince hesaplatırsın, "
                          "sonra istediğin hızda ve açıdan izlersin.",
                          "Eight experiments on a high-speed photography table: water poured, honey and sand flowing, a water balloon "
                          "popped, a melon and gel shot, a drop into milk, a cube blown apart. You trigger it, have the fast moment "
                          "computed finely, then watch at any speed from any side.");
        i.details = {
            {L("Tek tanecik çözücüsü", "One particle solver"),
             L("Küp dışındaki yedi deney aynı ekran kartı çözücüsünde taneciklerle kurulur. Su, bal ve süt bir yoğunluk kısıtıyla "
               "(PBF, konuma dayalı akışkan) sıkışmadan akar; kum sürtünmeli tanelerdir. Balon lastiği, karpuz ve jel kopabilen "
               "esnek bağlarla (XPBD) bir arada durur; bütün bağları kopan karpuz eti suya döner. Kum yığını 32,5° ile durur, "
               "kuru kumun 30-35° aralığında.",
               "The seven experiments other than the cube run as particles on one GPU solver. Water, honey and milk flow without "
               "compressing under a density constraint (PBF, position-based fluids); sand is grains with friction. The balloon's "
               "rubber, the melon and the gel hold together with breakable elastic bonds (XPBD), and melon flesh that loses all "
               "its bonds turns to juice. The sand pile stands at 32.5°, within dry sand's 30-35°.")},
            {L("Ağır çekim ve ince hesap", "Slow motion and the fine pass"),
             L("Tetik önce kaba dünyada hemen oynar. İstersen aynı girdiler arka planda ince bir dünyada yeniden hesaplanır: "
               "tanecikler 0,4-2,5 mm, saniyede 600 ile 5.000 an saklanır. Video kaydedilmez, saklanan fiziktir; kayıt her açıdan "
               "canlı çizilir, ileri geri sarılır, 1/100 hıza dek yavaşlatılır. Ağır çekimde alt adım kısalmaz, seyrekleşir; "
               "aradaki kareler iki adımın arasından çizilir.",
               "A trigger plays at once on the coarse world. If you ask, the same inputs are computed again in a fine world in "
               "the background, with particles of 0.4 to 2.5 mm and 600 to 5,000 moments kept a second. No video is made: what is "
               "kept is the physics, drawn live from any side, scrubbed and slowed down to 1/100. In slow motion the substeps get "
               "rarer, not shorter, and the frames between them are interpolated.")},
            {L("Katı küpler", "Rigid cubes"),
             L("Küp deneyinde her küçük küp kendi dönüşü olan katı bir cisimdir, 64 taneden bir milyona dek. Kutu kutuya temaslar "
               "ekran kartında renklere bölünmüş Gauss-Seidel ile çözülür: aynı renkteki küpler birbirine değmez, bu yüzden aynı "
               "anda çözülebilir. Şok yayılımı yığını durdurur; sürtünme, sekme ve hava direnci de hesaba girer. Patlama her yöne "
               "Gurney denklemiyle, o yöndeki küplerin kütlesine göre hız verir.",
               "In the cube experiment each small cube is a rigid body with its own rotation, from 64 of them up to a million. "
               "Box-on-box contacts are solved on the GPU by Gauss-Seidel split into colours: cubes of one colour never touch, so "
               "they can be solved at once. Shock propagation lets a stack come to rest, with friction, bounce and air drag on top. "
               "The blast gives each direction a speed by the Gurney equation, from the mass of cubes lying that way.")},
            {L("Taneciklerden sıvı yüzeyi", "Liquid surfaces from particles"),
             L("Sıvının yüzeyi her karede taneciklerden yeniden kurulur. Her taneciğe, komşularının dizilişine göre basık bir "
               "elipsoit çekirdek verilir (Yu ve Turk, 2013); düz yüzey düz kalır, ince bir su perdesi dağılmaz. Yoğunluk alanından "
               "ekran kartında üçgen ağ çıkarılır, tek kalan damlacıklar küre olarak çizilir. Tanecik görünmez, sıvı görünür.",
               "Each frame the liquid's surface is rebuilt from its particles. Every particle gets an ellipsoid kernel flattened by "
               "how its neighbours lie (Yu and Turk, 2013), so a flat surface stays flat and a thin sheet holds together. A triangle "
               "mesh is taken from the density field on the GPU, and lone droplets are drawn as spheres. You see liquid, not "
               "particles.")},
            {L("Cam, sıvı ve ışık", "Glass, liquid and light"),
             L("Görüntü OptiX ile yol izlenerek çizilir: ışın camda ve sıvıda Fresnel'e göre yansır ya da kırılır, içinden geçtiği "
               "yol boyunca soğurulur; balın kehribar rengi buradan gelir. Arkada, sıvı fotoğrafçılığındaki gibi kenarları yumuşak "
               "düşen büyük bir ışık paneli durur. Her kare 1920 × 1080'de piksel başına 4 örnekle çizilir, önceki karelerle "
               "harmanlanır ve OptiX'in yapay zekâ gürültü gidericisinden geçer.",
               "The image is path traced with OptiX: in glass and liquid a ray reflects or refracts by Fresnel and is absorbed along "
               "its way through, which is where honey's amber comes from. Behind stands a large light panel whose edges fall off "
               "softly, as in liquid photography. Each frame is drawn at 1920 × 1080 with 4 samples per pixel, blended with earlier "
               "frames and passed through OptiX's AI denoiser.")},
            {L("Gerçek mermiler", "Real rounds"),
             L("Tabanca ve tüfek mermileri gerçek yüklerdir: .22 LR'den .50 AE'ye, 5,56 mm'den .50 BMG'ye; kütle ve hızları üretici "
               "ve ordu veri sayfalarından alınır. Merminin yol boyunca yavaşlaması ve bıraktığı enerji, %10'luk balistik jelde "
               "yayımlanmış ölçümlere göre ayarlanır.",
               "The pistol and rifle rounds are real loads, from .22 LR to .50 AE and from 5.56 mm to .50 BMG, with masses and "
               "speeds from manufacturer and army data sheets. How a bullet slows along its path and the energy it leaves are "
               "calibrated against published measurements in 10% ballistic gelatin.")}};
        i.tags = {L("Sıvı fiziği", "Fluid physics"), L("Ağır çekim", "Slow motion"), L("Yol izleme", "Path tracing")};
        i.image = "assets/gallery/laboratuvar.jpg";
        i.accent = 0x6FC2B4;
        i.reloadable = true;
        i.keys = {{L("Deney", "Experiment"),
                   {{"1|–|8", L("Deneyi seç (paneldeki sırayla)", "Choose an experiment (panel order)")},
                    {L("Boşluk", "Space"), L("Başlat: dök, ateş et, patlat", "Trigger: pour, fire, blow up")},
                    {L("Sol tık", "Left click"), L("Hedefe tıkla, sürükleyip akışı gezdir", "Click a target, drag to steer a stream")},
                    {"I", L("İnce hesabı başlat / durdur", "Start / stop the fine pass")},
                    {"R", L("Yeniden kur ya da ince kayıttan çık", "Reset, or leave the fine recording")}}},
                  {L("Zaman", "Time"),
                   {{"T", L("Hız: ×1, ×1/10, ×1/100", "Speed: ×1, ×1/10, ×1/100")},
                    {"P", L("Durdur / oynat", "Pause / play")},
                    {"←|→", L("Kayıtta 10 ms geri / ileri", "10 ms back / on in a recording")},
                    {"Shift|+|←|→", L("Kayıtta 100 ms geri / ileri", "100 ms back / on in a recording")}}},
                  {L("Kamera", "Camera"),
                   {{L("Sağ tık|+|Fare", "Right click|+|Mouse"), L("Basılı tutup bak", "Hold to look around")},
                    {"W|A|S|D", L("Hareket et", "Move")},
                    {"Q|E", L("Aşağı / yukarı", "Down / up")},
                    {L("Tekerlek", "Wheel"), L("İleri / geri", "Forward / back")},
                    {"Shift", L("Basılı tutup hızlı git", "Hold to move fast")},
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
            in_[int(Deney::Kup)].fineness = kupFineness(a_.cfg.i("kup.baslangic_kenar", 16), a_.cfg.i("kup.en_cok_kenar", 100));
            in_[int(Deney::Kup)].power = a_.cfg.f("kup.baslangic_siddet", 0.42f);
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
        now_ = in.now;
        if (rebuildIn_ > 0 && --rebuildIn_ == 0) {
            buildWorld(pending_);
            host.toast(deneyName(deney_));
        }
        // ---- keys and the panel
        for (int k = 0; k < kDeneyCount; ++k)
            if (in.pressed(GLFW_KEY_1 + k) || in.pressed(GLFW_KEY_KP_1 + k)) choose(host, Deney(k));
        if (in.pressed(GLFW_KEY_R)) resetAction(host);
        if (in.pressed(GLFW_KEY_SPACE)) mainAction(host);
        if (in.pressed(GLFW_KEY_I)) fineAction(host);
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
            else if (e.id == "ince") fineAction(host);
            else if (e.id == "yeniden") resetAction(host);
            else if (e.id == "hiz") setSpeed(host, e.item);
            else if (e.id == "durdur") togglePause(host);
            else if (e.id == "yukseklik") { in_[int(deney_)].height = e.value; if (e.released || deney_ != Deney::Su) inputsChanged(); }
            else if (e.id == "akis") { in_[int(deney_)].flow = e.value; inputsChanged(); }
            else if (e.id == "guc") in_[int(deney_)].power = e.value;
            else if (e.id == "detay") {   // the cube's fineness: set up again when let go
                in_[int(deney_)].fineness = e.value;
                if (e.released && mode_ == Mode::Live) {
                    pending_ = deney_;
                    rebuildIn_ = 2;
                }
            }
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
                if (deney_ == Deney::Kup && a_.isTarget(p.object)) {
                    LabEvent e;
                    e.kind = LabEvent::Blast;
                    e.power = in_[int(deney_)].power;
                    fire(host, e);
                    lastClick_ = in.now;
                } else if (deney_ == Deney::Balon && a_.isTarget(p.object)) {
                    const float3 c = a_.cfg.v3("balon.merkez", make_float3(0.0f, 0.20f, 0.0f));
                    if (length(p.pos - c) < a_.cfg.f("balon.yaricap", 0.06f) * 1.6f) {
                        LabEvent e;
                        e.kind = LabEvent::Prick;
                        e.a = p.pos;
                        fire(host, e);
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
                        fire(host, e);
                        lastClick_ = in.now;
                    }
                }
            }
        }
        // ---- time
        const float speed = kSpeeds[speedIdx_];
        if (mode_ != Mode::Playback && !paused_ && rebuildIn_ == 0 && !liveHeld()) {
            // the world's own substeps, fewer of them in slow motion; when a frame owes less than one, the drawing
            // goes from the state before the last substep to the state after it
            const double dt = (1.0 / 60.0) * speed;
            const bool between = w_.substepsIn(dt) < 0.999;
            if (between && w_.due(dt) > 0 && w_.count() <= cap_) {
                w_.pw.exportPositions(dPrev_, w_.pw.stream());   // on the solver's stream: before the substep
                prevCount_ = w_.count();
                prevValid_ = true;
            }
            w_.advance(dt);
            lerpA_ = between && prevValid_ ? float(w_.owed()) : 1.0f;
            cubeAlpha_ = between ? float(w_.owed()) : 1.0f;   // the cubes keep their own state before the last step
            changed_ = true;
        }
        if (rec_.active()) {
            if (rec_.failed()) {
                host.toast(L("İnce hesap başarısız oldu", "The fine pass failed"));
                rec_.release();
                autoPlay_ = false;
            } else if (autoPlay_ && !rec_.computing() && rec_.framesDone() > 0) {
                // the fine pass asked for is done: play it once at real speed, in full (unless it is being watched)
                autoPlay_ = false;
                if (mode_ != Mode::Playback) {
                    mode_ = Mode::Playback;
                    playT_ = 0.0;
                    playing_ = true;
                    firstPlayDone_ = false;
                    speedIdx_ = 0;
                    host.toast(L("İnce kayıt hazır, gerçek hızda oynuyor", "The fine recording is ready, playing at real speed"));
                }
            }
            if (mode_ == Mode::Playback && playing_ && !paused_) {
                playT_ += realDt * (firstPlayDone_ ? kSpeeds[speedIdx_] : 1.0f);
                if (playT_ >= rec_.duration() - 1e-6) {
                    playT_ = rec_.duration();
                    if (!rec_.computing()) {
                        playing_ = false;
                        if (!firstPlayDone_) host.toast(L("Kayıt bitti. Zaman çubuğunda gezin, hızı seç, kamerayla dolaş.", "Recording done. Scrub the time bar, pick a speed, fly around."));
                        firstPlayDone_ = true;
                    }
                }
                playT_ = std::min(playT_, rec_.computed());   // what is computed so far (a video loading)
                changed_ = true;
            }
        }
        // ---- draw
        const bool still = !changed_ && !moved_;
        stillFrames_ = still ? stillFrames_ + 1 : 0;
        // the coarse cubes waiting on a fine pass, the camera at rest: once the picture has converged it is kept, and the
        // GPU is the fine pass's
        if (liveHeld() && stillFrames_ > a_.cfg.i("kup.bekle_kare", 48)) return;
        drawNow();
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
        if (mode_ == Mode::Playback) s.chips.push_back({firstPlayDone_ ? L("İnce kayıt", "Fine recording") : L("İnce kayıt · gerçek hız", "Fine recording · real speed"), true});
        else if (!events_.empty() && deneyRecorded(deney_)) s.chips.push_back({L("Kaba hesap", "Coarse pass"), true});
        else s.chips.push_back({L("Canlı", "Live"), false});
        if (rec_.computing()) s.chips.push_back({L("İnce hesap %", "Fine pass ") + num(100.0 * rec_.progress(), 0) + L("", "%")});
        if (paused_) s.chips.push_back({L("Zaman durdu", "Time paused"), true});
        if (speedIdx_) s.chips.push_back({speedIdx_ == 1 ? L("×1/10", "×1/10") : L("×1/100", "×1/100"), true});
        if (liveHeld() && mode_ == Mode::Live) s.chips.push_back({L("Kaba küpler bekliyor", "Coarse cubes on hold"), true});
        // the cubes not stepped (waiting, or the recording on screen): no physics this frame
        if (deney_ == Deney::Kup && (liveHeld() || mode_ == Mode::Playback)) s.metrics.push_back({L("Fizik", "Physics"), "-"});
        else s.metrics.push_back({L("Fizik", "Physics"), num(deney_ == Deney::Kup ? w_.cubes.lastStepMs() : w_.pw.lastStepMs(), 1) + " ms"});
        s.metrics.push_back({L("Görüntü", "Picture"), num(a_.renderer.lastMs, 1) + " ms"});
        if (mode_ == Mode::Playback && rec_.hasCubes()) s.metrics.push_back({L("Küp", "Cubes"), theme::thousands(unsigned(rec_.cubeCount()))});
        else if (mode_ == Mode::Playback) s.metrics.push_back({L("Tanecik", "Particles"), theme::thousands(unsigned(rec_.drawnCount()))});
        else if (deney_ == Deney::Kup) {
            s.metrics.push_back({L("Küp", "Cubes"), theme::thousands(unsigned(w_.cubes.count()))});
            s.metrics.push_back({L("Hareketli", "Moving"), theme::thousands(unsigned(w_.cubes.lastStats().awake))});
        } else s.metrics.push_back({L("Tanecik", "Particles"), theme::thousands(unsigned(w_.count()))});
        if (rec_.active() && rec_.computing() && rec_.slowdown() > 0.0) s.metrics.push_back({L("İnce hesap", "Fine pass"), num(rec_.slowdown(), 1) + L(" kat yavaş", "x slower")});
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
            c.detail = L("Bu yükseklikten düşen damla ", "From this height the drop lands at ") + num(std::sqrt(2.0 * 9.81 * h), 1) + L(" m/s ile çarpar", " m/s");
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
            c.detail = L("Beherin ağzının yüksekliği; bırakınca deney yeniden kurulur", "The height of the beaker's lip; set up again when you let go");
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
            c.text = num(q, 1) + L(" mL/s", " mL/s");
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
        if (deney_ == Deney::Kup) {
            const int maxN = a_.cfg.i("kup.en_cok_kenar", 100);
            const int n = kupPerEdge(li.fineness, maxN);
            app::Control c;
            c.kind = app::Control::Kind::Slider;
            c.id = "detay";
            c.title = L("Küp sayısı", "Cubes");
            c.section = true;
            c.value = li.fineness;
            c.text = theme::thousands(unsigned(n * n * n)) + " (" + std::to_string(n) + "³)";
            const float side = a_.cfg.f("kup.boyut", 0.10f);
            c.detail = L("Her biri ", "Each ") + num(side / n * 1000.0, n >= 32 ? 1 : 0) + L(" mm; bırakınca deney yeniden kurulur", " mm; set up again when you let go") +
                       (n >= 64 ? L(". Canlıda yavaş, ince hesapla izle", ". Slow live: watch the fine pass") : std::string());
            s.controls.push_back(c);
            app::Control g;
            g.kind = app::Control::Kind::Slider;
            g.id = "guc";
            g.title = L("Şiddet", "Strength");
            g.value = li.power;
            const float ch = kupCharge(li.power);
            g.text = ch < 1e-3f ? num(ch * 1e6, ch < 1e-5f ? 1 : 0) + L(" µg TNT", " µg TNT") : ch < 1.0f ? num(ch * 1000.0, ch < 1e-2f ? 1 : 0) + L(" mg TNT", " mg TNT")
                                                                                          : num(ch, 1) + L(" g TNT", " g TNT");
            // the speed of a face's middle (Gurney: the cone's mass over the charge's)
            const float M = a_.cfg.f("kup.yogunluk", 1050.0f) * std::pow(0.5f * side, 3.0f) / 3.0f, Cs = ch * 1e-3f / (4.0f * 3.14159265f);
            const float vf = 2440.0f / std::sqrt(M / Cs + 0.6f);
            g.detail = L("Bir yüzün ortası ~", "A face's middle leaves at ~") + num(vf, vf < 10.0f ? 1 : 0) + L(" m/s ile çıkar: ", " m/s: ") +
                       (vf < 0.6f ? L("dökülür", "crumbles") : vf < 6.0f ? L("dağılır", "scatters") : L("patlar", "bursts"));
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
            c.detail = num(L0.shot.mass * 1000.0, 1) + " g · " + num(L0.shot.velocity, 0) + L(" m/s · ", " m/s · ") + num(L0.energy(), 0) + " J";
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
            else if (deney_ == Deney::Kup) { c.text = L("Patlat", "Blow it up"); c.enabled = mode_ == Mode::Live && events_.empty(); }
            else { c.text = L("Ateş et (ortadan)", "Fire (through the middle)"); c.enabled = mode_ == Mode::Live && (deney_ == Deney::Jel || events_.empty()); }
            if (deney_ == Deney::Balon || deney_ == Deney::Damla || deney_ == Deney::Kup) c.enabled = c.enabled && events_.empty();
            if (deney_ == Deney::Jel && !events_.empty()) c.detail = num(double(events_.size()), 0) + L(" atış", events_.size() == 1 ? " shot" : " shots");
            s.controls.push_back(c);
        }
        if (deneyRecorded(deney_) && !events_.empty()) {   // the fine pass, only when asked for
            app::Control c;
            c.kind = app::Control::Kind::Button;
            c.id = "ince";
            c.keys = "I";
            if (rec_.computing()) c.text = L("İnce hesabı durdur", "Stop the fine pass");
            else if (rec_.active()) { c.text = L("İnce kayıt hazır", "The fine recording is ready"); c.enabled = false; }
            else {
                c.text = L("İnce hesap yap", "Compute it finely");
                if (deney_ == Deney::Kup) {   // the same cubes, finer steps
                    const float k = float(a_.cfg.i("kup.ince_alt_adim", 48)) / float(std::max(1, a_.cfg.i("kup.alt_adim", 20)));
                    c.detail = L("Aynı küpler arka planda ", "The same cubes in the background, ") + num(k, 1) + L(" kat ince adımla hesaplanır", "x finer steps");
                } else {
                    c.detail = L("Aynı girdiler arka planda ", "The same inputs with ") + num(a_.cfg.f(std::string(deneyKey(deney_)) + ".aralik_ince", 0.0015f) * 1000.0, 1) +
                               L(" mm tanecikle hesaplanır", " mm particles, in the background");
                }
            }
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
            c.detail = rec_.computing() ? L("Hesaplanan ", "Computed ") + num(rec_.computed(), 2) + " / " + num(T, 2) + L(" sn · tıklayıp izle", " s · click to watch")
                                        : L("İnce kayıt: ", "Fine recording: ") + num(rec_.spacing() * 1000.0, 1) +
                                              (rec_.hasCubes() ? L(" mm küp, saniyede ", " mm cubes, ") : L(" mm tanecik, saniyede ", " mm particles, ")) +
                                              num(rec_.busyRate(), 0) + L(" an", " moments a second") +
                                              (rec_.shots() > 1 ? " · " + num(double(rec_.shots()), 0) + L(" atış", " shots") : std::string());
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
            r.text = mode_ == Mode::Live ? L("Yeniden kur", "Set up again") : L("Kaba hesaba dön", "Back to the coarse world");
            s.controls.push_back(r);
        }
        if (rec_.computing()) {
            s.progressLabel = L("İnce hesap", "Fine pass");
            s.progress = rec_.progress();
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
    enum class Mode { Live, Playback };
    app::SceneInfo info_;
    LaboratuvarApp a_;
    LabWorld w_;
    LabRecording rec_;
    Deney deney_ = Deney::Su, pending_ = Deney::Su;
    LabInputs in_[kDeneyCount];
    Mode mode_ = Mode::Live;
    bool paused_ = false, playing_ = false, firstPlayDone_ = false, changed_ = true, moved_ = true;
    double playT_ = 0.0, lastClick_ = -1.0;
    int speedIdx_ = 0, rebuildIn_ = 0, stillFrames_ = 0;
    // the coarse cubes wait while the fine pass computes: they have shown their burst, and a million of them took ~0.9 s
    // a step beside it (the panel at 1 fps for ten minutes, the fine pass at half the GPU)
    bool liveHeld() const { return deney_ == Deney::Kup && rec_.active() && rec_.computing(); }
    double now_ = 0.0;
    // what was triggered in this world (a recording replays it), with the inputs at the first
    std::vector<LabEvent> events_;
    LabInputs firedWith_;
    double firstAt_ = 0.0, lastFire_ = -1e9;
    bool autoPlay_ = false;   // a fine pass was asked for: play it once when it is done
    // slow motion's drawing between substeps: where the particles were before the last one
    float3* dPrev_ = nullptr;
    int prevCount_ = 0;
    bool prevValid_ = false;
    float lerpA_ = 1.0f;
    float cubeAlpha_ = 1.0f;
    float4* dCubes_ = nullptr;   // Kup: the cubes' poses for the drawing
    int nCubes_ = 0;
    float3* dPos_ = nullptr;
    uint8_t* dCode_ = nullptr;
    int cap_ = 0;
    uchar4* pinned_ = nullptr;

    void freeBuffers() {
        if (dPos_) cudaFree(dPos_);
        if (dCode_) cudaFree(dCode_);
        if (dPrev_) cudaFree(dPrev_);
        if (dCubes_) cudaFree(dCubes_);
        dCubes_ = nullptr;
        nCubes_ = 0;
        dPos_ = dPrev_ = nullptr;
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
        events_.clear();
        autoPlay_ = false;
        lastFire_ = -1e9;
        prevValid_ = false;
        lerpA_ = 1.0f;
        w_.build(d, a_.cfg, false, in_[int(d)]);
        w_.settle(a_.cfg.f(std::string(deneyKey(d)) + ".oturma", 1.0f));
        const int cap = std::max(w_.count() + a_.cfg.i(std::string(deneyKey(d)) + ".ek_kapasite", 0), a_.cfg.i(std::string(deneyKey(d)) + ".cizim_kapasite", 0));
        freeBuffers();
        cap_ = std::max(cap, 1024);
        cudaMalloc(reinterpret_cast<void**>(&dPos_), size_t(cap_) * sizeof(float3));
        cudaMalloc(reinterpret_cast<void**>(&dCode_), size_t(cap_));
        cudaMalloc(reinterpret_cast<void**>(&dPrev_), size_t(cap_) * sizeof(float3));
        cubeAlpha_ = 1.0f;
        if (d == Deney::Kup) {
            nCubes_ = w_.cubes.count();
            cudaMalloc(reinterpret_cast<void**>(&dCubes_), size_t(nCubes_) * 2 * sizeof(float4));
        }
        a_.setupScene(d, std::max(cap_, nCubes_), &w_.membraneTris);
        if (d == Deney::Kup) a_.setCubeColours(w_.cubes.deviceColours(), nCubes_);
        a_.renderer.resetHistory();
        changed_ = moved_ = true;
        std::printf("%s\n", w_.summary().c_str());
    }

    void choose(app::Host& host, Deney d, bool again = false) {
        if (d == deney_ && !again) return;
        if (rebuildIn_ > 0) return;
        rec_.cancel();
        autoPlay_ = false;   // a pass cut short is not to be played as if done
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
        prevValid_ = false;   // the state before the last substep is the next one's to keep
        lerpA_ = 1.0f;
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

    // R: in a fine recording back to the coarse world as it was; else the experiment set up again
    void resetAction(app::Host& host) {
        if (mode_ == Mode::Playback) {
            mode_ = Mode::Live;
            playing_ = false;
            prevValid_ = false;
            lerpA_ = 1.0f;
            changed_ = true;
            host.toast(L("Kaba hesaba dönüldü; ince kayıt zaman çubuğunda duruyor", "Back to the coarse world; the fine recording stays on the time bar"));
            return;
        }
        choose(host, deney_, true);
    }

    // the fine pass: started when asked for, stopped when asked again
    void fineAction(app::Host& host) {
        if (rebuildIn_ || !deneyRecorded(deney_)) return;
        if (rec_.computing()) {
            rec_.release();
            autoPlay_ = false;
            if (mode_ == Mode::Playback) mode_ = Mode::Live;
            host.toast(L("İnce hesap durduruldu", "The fine pass stopped"));
            return;
        }
        if (events_.empty()) {
            host.toast(L("Önce deneyi başlat; ince hesap aynı girdileri yeniden hesaplar", "Trigger it first; the fine pass computes the same inputs again"));
            return;
        }
        if (rec_.active()) return;   // done: it is on the time bar
        const std::string pre = std::string(deneyKey(deney_)) + ".";
        rec_.start(a_.cfg, deney_, firedWith_, events_, a_.cfg.f(pre + "kayit_sure", 1.5f), 1.0 / a_.cfg.f(pre + "kayit_hiz", 600.0f),
                   1.0 / a_.cfg.f(pre + "kayit_seyrek_hiz", a_.cfg.f("kayit.seyrek_hiz", 120.0f)), a_.cfg.f(pre + "kayit_ara_en_cok", 0.8f),
                   size_t(a_.cfg.f("kayit.butce_mb", 4096.0f)) << 20, a_.cfg.f(pre + "kayit_yogun", -1.0f));
        autoPlay_ = true;
        if (deney_ == Deney::Kup)
            host.toast(L("İnce hesap başladı: kaba küpler bekler, bitince gerçek hızda oynar", "The fine pass started: the coarse cubes wait, it plays at real speed when done"));
        else
            host.toast(L("İnce hesap başladı: bitince gerçek hızda oynar", "The fine pass started: it plays at real speed when done"));
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
        } else if (deney_ == Deney::Kup) {
            e.kind = LabEvent::Blast;
            e.power = li.power;
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
        fire(host, e);
    }

    // a trigger on the coarse world, kept for a fine pass; the gel takes one a second, the others one
    void fire(app::Host& host, const LabEvent& e) {
        const bool many = deney_ == Deney::Jel;
        if (!events_.empty() && !many) return;
        if (many && now_ - lastFire_ < kCooldown) return;
        if (events_.empty()) {
            firstAt_ = w_.time();
            firedWith_ = in_[int(deney_)];
        }
        LabEvent ev = e;
        ev.at = w_.time() - firstAt_;
        w_.trigger(ev);
        events_.push_back(ev);
        lastFire_ = now_;
        paused_ = false;
        if (rec_.active()) {   // a new shot: the fine recording no longer is this world's
            rec_.release();
            autoPlay_ = false;
        }
        if (events_.size() == 1) host.toast(L("Kaba hesap oynuyor. İnce hesap için I'ya bas.", "The coarse pass is playing. Press I for the fine pass."));
        else host.toast(L("Atış ", "Shot ") + std::to_string(events_.size()));
    }

    void drawNow() {
        DrawInput d;
        PropPose poses[LabWorld::P_COUNT];
        if (nCubes_ > 0) {   // Kup: the cubes, live or recorded
            if (mode_ == Mode::Playback && rec_.framesDone() > 0 && rec_.hasCubes() && rec_.cubeCount() == nCubes_) rec_.cubesAt(playT_, dCubes_, a_.renderer.stream());
            else w_.exportCubes(dCubes_, cubeAlpha_, a_.renderer.stream());
            d.cubePose = dCubes_;
            d.cubeCount = nCubes_;
            d.pos = dPos_;
            d.code = dCode_;
            d.poses = poses;
            for (int p = 0; p < LabWorld::P_COUNT; ++p) poses[p] = w_.prop[p];
            a_.draw(d);
            return;
        }
        if (mode_ == Mode::Playback && rec_.framesDone() > 0 && rec_.drawnCount() <= cap_) {
            if (a_.memSource != &rec_.membraneTris && !rec_.membraneTris.empty()) a_.setMembraneTris(rec_.membraneTris);
            rec_.frameAt(playT_, dPos_, dCode_, poses, a_.renderer.stream());
            d.n = rec_.drawnCount();
            d.spacing = rec_.spacing();
            d.membraneFirst = rec_.membraneFirst;
            d.membraneCount = rec_.membraneCount;
        } else {
            if (w_.count() > cap_) return;   // over the drawing's room (an emitter ran long): keep the last picture
            if (a_.memSource != &w_.membraneTris && !w_.membraneTris.empty()) a_.setMembraneTris(w_.membraneTris);
            w_.exportDraw(dPos_, dCode_, a_.renderer.stream());
            if (prevValid_ && lerpA_ < 0.9999f) launchLerp(dPrev_, dPos_, std::min(prevCount_, w_.count()), lerpA_, a_.renderer.stream());
            for (int p = 0; p < LabWorld::P_COUNT; ++p) poses[p] = w_.prop[p];
            d.n = w_.count();
            d.spacing = w_.s;
            d.membraneFirst = w_.membraneFirst;
            d.membraneCount = w_.membraneCount;
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
