// Scene 4 "Tanecik" inside the application. T0 (the look): the mannequin in the studio, as glass (bones and
// particles inside) or as skin; the camera circles him. The physics comes in T1.
#include "app/scene.h"
#include "app/theme.h"
#include "core/image_io.h"
#include "scenes/tanecik/tanecik_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

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
        i.description = L("Yüz binlerce tanecikten bir beden: kemikleri, dokusu, derisi. Yapım aşamasında: şimdilik görünüm.",
                          "A body made of hundreds of thousands of particles: its bones, its tissue, its skin. Under construction: "
                          "for now, the look.");
        i.details = {
            {L("Tanecikler", "Particles"),
             L("Beden, derinin içine dizilmiş 7 mm'lik taneciklerden oluşur; her tanecik 17 kemikten birine aittir. Fizik bir "
               "sonraki adımda gelecek.",
               "The body is 7 mm particles packed inside the skin; each belongs to one of 17 bones. The physics comes next.")},
            {L("Görüntü", "The picture"),
             L("OptiX ile yol izleme: bir fotoğraf stüdyosu, yumuşak ışık kutuları, ince ve renkli bir cam deri.",
               "Path tracing with OptiX: a photo studio, soft boxes, a thin tinted glass skin.")}};
        i.tags = {L("Tanecik fiziği", "Particle physics"), L("Yapım aşamasında", "Under construction")};
        i.image = "assets/gallery/tanecik.jpg";
        i.accent = 0x6FC2B4;
        i.keys = {{L("Görünüm", "Look"),
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
            st.label = L("Beden taneciklerle dolduruluyor", "Filling the body with particles");
            st.fraction = 0.35f;
            ++st.step;
            return;
        }
        case 2:
            t_.build();
            st.label = L("Görüntü hazırlanıyor", "Preparing the picture");
            st.fraction = 0.85f;
            ++st.step;
            return;
        default:
            t_.particleView = std::min(2, std::max(0, t_.cfg.i("gorunum.tanecik", 1)));
            t_.upload();
            glass_ = t_.cfg.i("gorunum.cam", 1) != 0;
            t_.applyLook(glass_);
            t_.setCamera();
            spp_ = unsigned(std::max(1, t_.cfg.i("display.spp", 2)));
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(t_.width) * t_.height * sizeof(uchar4));
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        if (in.pressed(GLFW_KEY_V)) setGlass(host, !glass_);
        if (in.pressed(GLFW_KEY_N)) setParticles(host, (t_.particleView + 1) % 3);
        if (in.pressed(GLFW_KEY_K)) {
            t_.renderer.setVisible(TK_BONES, !t_.renderer.visible(TK_BONES));
            host.toast(t_.renderer.visible(TK_BONES) ? L("Kemikler görünüyor", "Bones shown") : L("Kemikler gizli", "Bones hidden"));
            moved_ = true;
        }
        for (const app::ControlEvent& e : in.events) {
            if (e.id == "gorunum") setGlass(host, e.item == 0);
            else if (e.id == "tanecik") setParticles(host, e.item);
        }
        if (in.rightDown && looking_ && (in.mouseDX != 0.0f || in.mouseDY != 0.0f)) {
            t_.orbit.turn(in.mouseDX, in.mouseDY, t_.cfg.f("kamera.fare", 0.004f));
            moved_ = true;
        }
        looking_ = in.rightDown;
        if (in.scroll != 0.0f) {
            t_.orbit.dist = std::max(0.8f, std::min(9.0f, t_.orbit.dist * std::pow(0.9f, in.scroll)));
            moved_ = true;
        }
        const float dt = float(std::min(0.1, in.dt));
        const float up = (in.isDown(GLFW_KEY_E) ? 1.0f : 0.0f) - (in.isDown(GLFW_KEY_Q) ? 1.0f : 0.0f);
        if (up != 0.0f) {
            t_.orbit.target.y = std::max(0.1f, std::min(1.8f, t_.orbit.target.y + up * dt * 0.8f));
            moved_ = true;
        }
        t_.orbit.apply(t_.renderer.params().cam, t_.width, t_.height);
        // a still view gathers samples; a moving one starts afresh
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
        s.metrics.push_back({L("Görüntü", "Picture"), num(t_.renderer.lastMs, 1) + " ms"});
        s.metrics.push_back({L("Örnek", "Samples"), theme::thousands(t_.renderer.params().samples)});
        s.metrics.push_back({L("Tanecik", "Particles"), theme::thousands(unsigned(t_.body.pos.size()))});
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "gorunum";
            c.title = L("Görünüm", "Look");
            c.items = {L("Cam", "Glass"), L("Deri", "Skin")};
            c.active = glass_ ? 0 : 1;
            c.detail = glass_ ? L("İçi görünür: kemikler ve tanecikler", "The inside shows: bones and particles") : L("Opak deri", "Opaque skin");
            s.controls.push_back(c);
        }
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "tanecik";
            c.title = L("Tanecik", "Particles");
            c.items = {L("Hepsi", "All"), L("Seyrek", "Sparse"), L("Gizli", "Hidden")};
            c.active = t_.particleView;
            c.detail = L("Yalnız görüntü: fizik her taneciği hesaplar", "Display only: the physics computes every particle");
            s.controls.push_back(c);
        }
    }

    void screenshot(app::Host& host) override {
        const std::string path = host.capturePath() + ".png";
        writePNG(path, int(t_.width), int(t_.height), reinterpret_cast<const unsigned char*>(pinned_));
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void unload() override {
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
        moved_ = true;
        host.toast(v == 0 ? L("Bütün tanecikler", "Every particle") : v == 1 ? L("Seyrek tanecikler", "Sparse particles") : L("Tanecikler gizli", "Particles hidden"));
    }

    app::SceneInfo info_ = describe();
    TanecikApp t_;
    uchar4* pinned_ = nullptr;
    unsigned spp_ = 2;
    bool glass_ = true, looking_ = false, moved_ = true;
};

}   // namespace

namespace tanecik {
app::SceneInfo info() { return TanecikLive::describe(); }
std::unique_ptr<app::Scene> make() { return TanecikLive::make(); }
int cli(int argc, char** argv) { return runTanecikCli(argc, argv); }
}   // namespace tanecik
