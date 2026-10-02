// Scene 4 "Tanecik" inside the application: the mannequin of particles (bones following his skeleton, flesh bonded
// to them, the skin over it) on a hidden stand, and the weapons: a click fires the chosen gun from the camera at the
// point under the cursor. The flesh tears, the blood runs free, the skeleton takes the push. Slow motion, pause (the
// camera still circles: freeze and look), a fresh mannequin.
#include "app/scene.h"
#include "app/theme.h"
#include "core/image_io.h"
#include "scenes/tanecik/body_world.h"
#include "scenes/tanecik/tanecik_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

#include <cstdio>
#include <memory>

namespace {

using app::L;

class TanecikLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "tanecik";
        i.number = "04";
        i.title = L("Tanecik", "Particles");
        i.subtitle = L("Tanecik fiziği ve insan fiziği", "Particle physics and the human body");
        i.description = L("Yüz binlerce tanecikten bir manken: kemikleri, kanla dolu dokusu, derisi. Seçtiğin silahla tıkladığın yere ateş "
                          "et: doku yırtılır, kan akar. Yapım aşamasında.",
                          "A mannequin made of hundreds of thousands of particles: its bones, its blood-filled tissue, its skin. Fire the "
                          "chosen gun where you click: the tissue tears, the blood runs. Under construction.");
        i.details = {
            {L("Tanecikler", "Particles"),
             L("Beden, derinin içine dizilmiş 7 mm'lik 173 bin tanecik; her biri 17 kemikten birine ait. Kemik tanecikleri iskeleti izler, "
               "doku ve deri bir milyonu aşkın bağla onlara tutunur.",
               "The body is 173,000 particles of 7 mm packed inside the skin, each belonging to one of 17 bones. The bone particles "
               "follow the skeleton; tissue and skin hold on to them by more than a million bonds.")},
            {L("Kendi çözücümüz", "Our own solver"),
             L("Ekran kartında tanecik fiziği: sıvı yoğunluk kısıtı (PBF), kopabilen XPBD bağları, iskelet için katı kemikler ve "
               "sınırlı eklemler. Bütün bağları kopan tanecik sıvıya döner: kan.",
               "Particle physics on the graphics card: a fluid density constraint (PBF), breakable XPBD bonds, rigid bones and "
               "limited joints for the skeleton. A particle that has lost all its bonds turns liquid: blood.")},
            {L("Mermiler", "Bullets"),
             L("Gerçek yükler (üretici tabloları): .22'den Desert Eagle'a, 5,56'dan .50 BMG'ye, .410'dan magnum saçmaya. Mermi dokuda "
               "balistik jele kalibre bir yasayla yavaşlar; yolundaki doku kopar, geçici boşluk çevreyi iter, momentum korunur.",
               "Real loads (manufacturers' tables): from .22 to Desert Eagle, 5.56 to .50 BMG, .410 to magnum buckshot. In tissue the "
               "bullet slows by a law calibrated to ballistic gelatin; the tissue in its path tears, the temporary cavity pushes "
               "the rest aside, momentum is conserved.")},
            {L("Görüntü", "The picture"),
             L("OptiX ile yol izleme: fotoğraf stüdyosu, ince cam deri, kanın yüzeyi her karede taneciklerden kurulur.",
               "Path tracing with OptiX: a photo studio, a thin glass skin, the blood's surface built from its particles every frame.")}};
        i.tags = {L("Tanecik fiziği", "Particle physics"), L("Gerçek balistik", "Real ballistics"), L("Yapım aşamasında", "Under construction")};
        i.image = "assets/gallery/tanecik.jpg";
        i.accent = 0x6FC2B4;
        i.keys = {{L("Silah", "Weapon"),
                   {{L("Sol tık", "Left click"), L("Ateş et (imlecin altına)", "Fire (at what is under the cursor)")},
                    {"1|2|3", L("Tabanca, tüfek, pompalı", "Pistol, rifle, shotgun")},
                    {"↑|↓", L("Güç (gerçekçi sınırlar içinde)", "Power (within real limits)")},
                    {"R", L("Yeni manken", "A fresh mannequin")}}},
                  {L("Zaman", "Time"),
                   {{"T", L("Ağır çekim: ×1, ×0,25, ×0,1", "Slow motion: ×1, ×0.25, ×0.1")},
                    {"P", L("Zamanı durdur (kamera döner)", "Pause time (the camera still turns)")}}},
                  {L("Görünüm", "Look"),
                   {{"V", L("Cam / deri", "Glass / skin")},
                    {"N", L("Tanecikler: hepsi, seyrek, gizli", "Particles: all, sparse, hidden")},
                    {"K", L("Kemikleri göster / gizle", "Show / hide the bones")}}},
                  {L("Kamera", "Camera"),
                   {{L("Sağ tık|Fare", "Right click|Mouse"), L("Etrafında dön", "Circle around")},
                    {L("Tekerlek", "Wheel"), L("Yaklaş / uzaklaş", "Closer / further")},
                    {"E|Q", L("Yukarı / aşağı", "Up / down")}}}};
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<TanecikLive>(); }

    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Ekran kartı ve ışın izleme programı hazırlanıyor", "Preparing the graphics card and the ray-tracing program");
            st.fraction = 0.05f;
            ++st.step;
            return;
        case 1: {
            t_.root = host.root();
            t_.overrides = host.overrides();
            t_.loadConfig();
            t_.width = unsigned(t_.cfg.i("display.width", 1920));
            t_.height = unsigned(t_.cfg.i("display.height", 1080));
            t_.renderer.init(t_.width, t_.height, t_.root);
            std::string log;
            if (!t_.renderer.compileDevice(log)) { st.error = L("Işın izleme programı derlenemedi.\n", "The ray-tracing program did not compile.\n") + log; return; }
            st.label = L("Manken taneciklerle dolduruluyor, bağları kuruluyor", "Filling the mannequin with particles, bonding them");
            st.fraction = 0.3f;
            ++st.step;
            return;
        }
        case 2:
            world_ = std::make_unique<BodyWorld>(t_);
            if (!world_->build()) { st.error = L("Manken kurulamadı.", "The mannequin could not be built."); return; }
            st.label = L("Görüntü hazırlanıyor", "Preparing the picture");
            st.fraction = 0.85f;
            ++st.step;
            return;
        default:
            t_.particleView = std::min(2, std::max(0, t_.cfg.i("gorunum.tanecik", 0)));
            t_.upload();
            glass_ = t_.cfg.i("gorunum.cam", 1) != 0;
            t_.applyLook(glass_);
            t_.setCamera();
            world_->feed();
            spp_ = unsigned(std::max(1, t_.cfg.i("display.spp", 2)));
            for (float& p : power_) p = 0.5f;
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(t_.width) * t_.height * sizeof(uchar4));
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        if (rebuildIn_ > 0 && --rebuildIn_ == 0) {   // shown first ("yeni manken"), built the frame after
            world_ = std::make_unique<BodyWorld>(t_);
            world_->build();
            t_.upload();
            t_.applyLook(glass_);
            world_->feed();
            moved_ = true;
            host.toast(L("Yeni manken", "A fresh mannequin"));
        }
        // the look
        if (in.pressed(GLFW_KEY_V)) setGlass(host, !glass_);
        if (in.pressed(GLFW_KEY_N)) setParticles(host, (t_.particleView + 1) % 3);
        if (in.pressed(GLFW_KEY_K)) {
            t_.renderer.setVisible(TK_BONES, !t_.renderer.visible(TK_BONES));
            host.toast(t_.renderer.visible(TK_BONES) ? L("Kemikler görünüyor", "Bones shown") : L("Kemikler gizli", "Bones hidden"));
            moved_ = true;
        }
        // the weapon, time
        for (int k = 0; k < 3; ++k)
            if (in.pressed(GLFW_KEY_1 + k) || in.pressed(GLFW_KEY_KP_1 + k)) chooseTool(host, k);
        if (in.pressed(GLFW_KEY_UP)) setPower(power_[tool_] + 0.1f);
        if (in.pressed(GLFW_KEY_DOWN)) setPower(power_[tool_] - 0.1f);
        if (in.pressed(GLFW_KEY_T)) {
            speedIdx_ = (speedIdx_ + 1) % 3;
            host.toast(speedIdx_ == 0 ? L("Gerçek hız", "Real speed") : speedIdx_ == 1 ? L("Ağır çekim ×0,25", "Slow motion ×0.25") : L("Ağır çekim ×0,1", "Slow motion ×0.1"));
        }
        if (in.pressed(GLFW_KEY_P)) { paused_ = !paused_; host.toast(paused_ ? L("Zaman durdu", "Time paused") : L("Zaman akıyor", "Time runs")); }
        if (in.pressed(GLFW_KEY_R) && rebuildIn_ == 0) {
            rebuildIn_ = 2;
            host.toast(L("Yeni manken kuruluyor…", "Building a fresh mannequin…"));
        }
        for (const app::ControlEvent& e : in.events) {
            if (e.id == "gorunum") setGlass(host, e.item == 0);
            else if (e.id == "tanecik") setParticles(host, e.item);
            else if (e.id == "arac") chooseTool(host, e.item);
            else if (e.id == "guc") setPower(e.value);
        }
        // the camera
        if (in.rightDown && looking_ && (in.mouseDX != 0.0f || in.mouseDY != 0.0f)) {
            t_.orbit.turn(in.mouseDX, in.mouseDY, t_.cfg.f("kamera.fare", 0.004f));
            moved_ = true;
        }
        looking_ = in.rightDown;
        if (in.scroll != 0.0f) {
            t_.orbit.dist = std::max(0.8f, std::min(9.0f, t_.orbit.dist * std::pow(0.9f, in.scroll)));
            moved_ = true;
        }
        const float realDt = float(std::min(0.1, in.dt));
        const float up = (in.isDown(GLFW_KEY_E) ? 1.0f : 0.0f) - (in.isDown(GLFW_KEY_Q) ? 1.0f : 0.0f);
        if (up != 0.0f) {
            t_.orbit.target.y = std::max(0.1f, std::min(1.8f, t_.orbit.target.y + up * realDt * 0.8f));
            moved_ = true;
        }
        t_.orbit.apply(t_.renderer.params().cam, t_.width, t_.height);
        // fire: from the camera at what is under the cursor (nothing under it: along the view, 5 m on); at most one shot
        // every 0.15 s of wall time
        if (in.leftClicked && in.view.w > 0.0f && in.now - lastShot_ >= 0.15 && rebuildIn_ == 0) {
            const float rx = (in.mouseX - in.view.x) / in.view.w * t_.width, ry = (in.mouseY - in.view.y) / in.view.h * t_.height;
            if (rx >= 0 && ry >= 0 && rx < t_.width && ry < t_.height) {
                const TPick p = t_.renderer.pick(rx, ry);
                const float3 origin = t_.renderer.params().cam.pos;
                const float3 aim = p.kind >= 0 ? p.pos : origin + p.dir * 5.0f;
                const HitReport h = world_->shoot(gun(), power_[tool_], origin, aim);
                lastShot_ = in.now;
                std::printf("atis %s %.2f: secilen %d (%.2f %.2f %.2f), namlu (%.2f %.2f %.2f); yol uzerinde %d, bosluk %d, kopan bag %d, momentum %.3f "
                            "N s, doku %.1f cm\n",
                            toolKey(tool_), power_[tool_], p.kind, aim.x, aim.y, aim.z, origin.x, origin.y, origin.z, h.crushed, h.thrown, h.bondsBroken,
                            h.pDeposited, h.tissue * 100.0f);
                if (h.crushed == 0 && h.thrown == 0) host.toast(L("Iska", "Missed"));
                moved_ = true;
                world_->feed();   // the shot's tear shows at once, even with time paused
            }
        }
        // time: one fixed step a frame (60 Hz of world time, slowed by the chosen speed)
        if (!paused_ && rebuildIn_ == 0) {
            world_->step((1.0f / 60.0f) * kSpeeds[speedIdx_]);
            world_->feed();
            moved_ = true;
        }
        if (in.now - lastStats_ > 0.5) {
            blood_ = world_->liquidCount();
            lastStats_ = in.now;
        }
        // a still view (time paused, the camera still) gathers samples; a moving one starts afresh
        t_.renderer.render(spp_, !moved_ && t_.renderer.params().samples < 8192);
        moved_ = false;
        t_.renderer.downloadTo(pinned_);
    }

    const void* image(int& w, int& h) override {
        w = int(t_.width);
        h = int(t_.height);
        return pinned_;
    }

    void status(app::Status& s) override {
        using theme::num;
        if (paused_) s.chips.push_back({L("Zaman durdu", "Time paused"), true});
        if (speedIdx_) s.chips.push_back({speedIdx_ == 1 ? L("Ağır çekim ×0,25", "Slow motion ×0.25") : L("Ağır çekim ×0,1", "Slow motion ×0.1"), true});
        s.metrics.push_back({L("Fizik", "Physics"), num(world_ ? world_->physicsMs : 0.0f, 1) + " ms"});
        s.metrics.push_back({L("Görüntü", "Picture"), num(t_.renderer.lastMs, 1) + " ms"});
        s.metrics.push_back({L("Tanecik", "Particles"), theme::thousands(unsigned(t_.body.pos.size()))});
        s.metrics.push_back({L("Kan", "Blood"), theme::thousands(unsigned(blood_)) + L(" tanecik", " particles")});
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "arac";
            c.title = L("Silah", "Weapon");
            c.items = {L("Tabanca", "Pistol"), L("Tüfek", "Rifle"), L("Pompalı", "Shotgun")};
            c.active = tool_;
            c.detail = L("Sol tık: imlecin altına ateş", "Left click: fire at what is under the cursor");
            s.controls.push_back(c);
        }
        {   // the power: the load at this point of the gun's real range; the notch is the common one
            const weapons::Load l = weapons::gunLoad(gun(), power_[tool_]);
            const float p = l.shot.mass * l.shot.velocity * std::max(1, l.pellets);
            app::Control c;
            c.kind = app::Control::Kind::Slider;
            c.id = "guc";
            c.title = L("Güç", "Power");
            c.value = power_[tool_];
            c.mark = 0.5f;
            c.text = l.name;
            c.detail = num(l.shot.mass * std::max(1, l.pellets) * 1000.0f, 1) + " g · " + num(l.shot.velocity, 0) + L(" m/s · ", " m/s · ") + num(p, 1) +
                       L(" N s (70 kg'a en çok ", " N s (at most ") + num(p / 70.0f, 2) + L(" m/s)", " m/s on 70 kg)");
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.section = true;
            c.kind = app::Control::Kind::Choice;
            c.id = "gorunum";
            c.title = L("Görünüm", "Look");
            c.items = {L("Cam", "Glass"), L("Deri", "Skin")};
            c.active = glass_ ? 0 : 1;
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "tanecik";
            c.title = L("Tanecik", "Particles");
            c.items = {L("Hepsi", "All"), L("Seyrek", "Sparse"), L("Gizli", "Hidden")};
            c.active = t_.particleView;
            s.controls.push_back(c);
        }
    }

    void screenshot(app::Host& host) override {
        const std::string path = host.capturePath() + ".png";
        writePNG(path, int(t_.width), int(t_.height), reinterpret_cast<const unsigned char*>(pinned_));
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void unload() override {
        world_.reset();
        t_.release();
        t_.renderer.shutdown();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }

    ~TanecikLive() override { unload(); }

private:
    void setGlass(app::Host& host, bool g) {
        if (g == glass_) return;
        glass_ = g;
        t_.applyLook(glass_);
        moved_ = true;
        host.toast(glass_ ? L("Cam görünüm", "Glass look") : L("Deri görünüm", "Skin look"));
    }
    void setParticles(app::Host& host, int v) {
        if (v == t_.particleView || v < 0 || v > 2) return;
        t_.particleView = v;
        t_.uploadParticles();
        if (world_) world_->feed();
        moved_ = true;
        host.toast(v == 0 ? L("Bütün tanecikler", "Every particle") : v == 1 ? L("Seyrek tanecikler", "Sparse particles") : L("Tanecikler gizli", "Particles hidden"));
    }
    weapons::Gun gun() const { return tool_ == 1 ? weapons::Gun::Rifle : tool_ == 2 ? weapons::Gun::Shotgun : weapons::Gun::Pistol; }
    static const char* toolKey(int t) { return t == 1 ? "tufek" : t == 2 ? "pompali" : "tabanca"; }
    void chooseTool(app::Host& host, int t) {
        if (t < 0 || t > 2 || t == tool_) return;
        tool_ = t;
        host.toast(t == 1 ? L("Tüfek", "Rifle") : t == 2 ? L("Pompalı", "Shotgun") : L("Tabanca", "Pistol"));
    }
    void setPower(float u) {   // near the notch it is the common load
        float& p = power_[tool_];
        p = std::min(1.0f, std::max(0.0f, u));
        if (std::fabs(p - 0.5f) < 0.03f) p = 0.5f;
    }

    app::SceneInfo info_ = describe();
    TanecikApp t_;
    std::unique_ptr<BodyWorld> world_;
    uchar4* pinned_ = nullptr;
    unsigned spp_ = 2;
    bool glass_ = true, looking_ = false, moved_ = true, paused_ = false;
    int tool_ = 0, speedIdx_ = 0, rebuildIn_ = 0, blood_ = 0;
    float power_[3] = {0.5f, 0.5f, 0.5f};
    double lastShot_ = -10.0, lastStats_ = -1.0;
    static constexpr float kSpeeds[3] = {1.0f, 0.25f, 0.1f};
};

}   // namespace

namespace tanecik {
app::SceneInfo info() { return TanecikLive::describe(); }
std::unique_ptr<app::Scene> make() { return TanecikLive::make(); }
int cli(int argc, char** argv) { return runTanecikCli(argc, argv); }
}   // namespace tanecik
