// Scene 8 "Denizaltı" inside the application: a fleet submarine's control room, its light baked beforehand, every
// sample of the picture shaded on its own, the surfaces tessellated and displaced every frame.
// W/A/S/D walk, Space/C up and down, Shift slow (looking closely), Tab mouse look (or drag with the left button), the
// right button the binoculars.
#include "app/scene.h"
#include "app/theme.h"
#include "core/image_io.h"
#include "scenes/denizalti/dz_app.h"
#include "scenes/denizalti/dz_gl.h"

#include <algorithm>
#include <cmath>

namespace {

using app::L;

class DenizaltiLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "denizalti";
        i.number = "08";
        i.title = L("Denizaltı", "The Submarine");
        i.subtitle = L("Bir filo denizaltısının kontrol odası, ışığı önceden pişirilmiş",
                       "A fleet submarine's control room, its light baked beforehand");
        i.description = L("Tench sınıfı bir denizaltının kontrol odası, gece kırmızısı altında. Işık önceden, bir gece boyunca "
                          "izlenip pişirildi; ekran kartının bütün gücü görüntüye gidiyor: her örnek ayrı gölgelendirilir, "
                          "yüzeyler her karede yeniden bölünüp kabartılır.",
                          "The control room of a Tench-class submarine under its night red. The light was traced and baked "
                          "beforehand, a night long; the graphics card's whole strength goes to the picture: every sample shaded "
                          "on its own, the surfaces cut and raised again every frame.");
        i.details = {
            {L("Önceden pişirilmiş ışık", "Baked light"),
             L("Canlıda ışık hesabı yok. Işık, ışın izlemeyle önceden ve normalde izlenenden kat kat uzun izlendi; "
               "her yüzey noktasına yönüyle birlikte yazıldı. Görüntü onu okur.",
               "No light is computed live. It was traced beforehand, far longer than a frame would allow, and written onto "
               "every point of every surface with its direction. The picture reads it.")},
            {L("Tessellation", "Tessellation"),
             L("Yüzeyler her karede kameraya göre bölünür ve kabartılır: perçin, kaynak dikişi, diş, sac deseni. High'da bir "
               "üçgen bir örnek kadardır.",
               "The surfaces are cut and raised for the camera every frame: rivets, weld seams, threads, tread plate. On "
               "High a triangle is as small as a sample.")},
            {L("Süper örnekleme", "Supersampling"),
             L("Her piksel 16 örneğe kadar ayrı ayrı gölgelendirilir; rastgele sayı yok, her karede aynı desen: gren ve "
               "kıpırtı olmaz.",
               "Each pixel is shaded up to 16 times; no random numbers, the same pattern every frame: no grain, no shimmer.")}};
        i.tags = {L("Önceden pişirilmiş ışık", "Baked light"), L("Tessellation", "Tessellation"), L("SSAA 16x", "SSAA 16x")};
        i.image = "assets/gallery/denizalti.jpg";
        i.accent = 0xD9583F;
        i.keys = {{L("Hareket", "Moving"),
                   {{"W|A|S|D", L("Yürü", "Walk")},
                    {L("Boşluk", "Space"), L("Yüksel", "Rise")},
                    {"C", L("Alçal", "Lower")},
                    {"Shift", L("Yavaş (yakından incele)", "Slow (look closely)")},
                    {"Tab", L("Fareyle bak (aç/kapa)", "Mouse look (on/off)")},
                    {L("Sol tık", "Left button"), L("Sürükle: bak", "Drag: look")},
                    {L("Sağ tık", "Right button"), L("Dürbün (basılı tut)", "Binoculars (hold)")},
                    {"R", L("Başa dön", "Back to the start")}}},
                  {L("Görüntü", "Picture"), {{"↑|↓", L("Pozlama", "Exposure")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<DenizaltiLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Kontrol odası kuruluyor", "Building the control room");
            st.fraction = 0.1f;
            ++st.step;
            return;
        default:
            a_.root = host.root();
            a_.overrides = host.overrides();
            a_.loadConfig();
            a_.build();
            a_.applyChoices();
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        if (!a_.glReady) {
            std::string err;
            if (!a_.initGL(err)) {
                host.toast(err);
                error_ = err;
                return;
            }
            host.pace(0.0f);
        }
        const float dt = float(std::min(0.1, in.dt));
        // the panel
        for (const app::ControlEvent& e : in.events) {
            if (e.item < 0) continue;
            if (e.id == "res") a_.resIndex = e.item;
            else if (e.id == "ssaa") a_.ssaaIndex = e.item;
            else if (e.id == "tess") a_.set.tess = e.item;
            else if (e.id == "gfx") a_.set.gfx = e.item;
            else if (e.id == "light") a_.set.light = e.item;
            else if (e.id == "fx") a_.set.fx = e.item;
        }
        a_.applyChoices();
        if (in.pressed(GLFW_KEY_TAB)) {
            look_ = !look_;
            host.captureMouse(look_);
        }
        if (in.pressed(GLFW_KEY_R)) a_.home();
        if (in.pressed(GLFW_KEY_UP)) a_.set.exposure += 0.25f;
        if (in.pressed(GLFW_KEY_DOWN)) a_.set.exposure -= 0.25f;
        // looking: the binoculars narrow the view four times and slow the mouse with it
        const float fov0 = a_.cfg.f("cam.fov", 60.0f) * 3.14159265f / 180.0f;
        const float fovT = in.rightDown ? 2.0f * std::atan(std::tan(0.5f * fov0) / 4.0f) : fov0;
        a_.cam.fovY += (fovT - a_.cam.fovY) * (1.0f - std::exp(-dt * 14.0f));
        const float sens = 0.0022f * std::tan(0.5f * a_.cam.fovY) / std::tan(0.5f * fov0);
        const bool dragging = in.leftDown && in.view.contains(in.mouseX, in.mouseY);
        if (look_ || dragging) {
            a_.cam.yaw += in.mouseDX * sens;
            a_.cam.pitch = std::clamp(a_.cam.pitch - in.mouseDY * sens, -1.45f, 1.45f);
        }
        auto key = [&](int kc) { return in.isDown(kc) ? 1.0f : 0.0f; };
        const float fwd = key(GLFW_KEY_W) - key(GLFW_KEY_S), right = key(GLFW_KEY_D) - key(GLFW_KEY_A);
        const float up = key(GLFW_KEY_SPACE) - key(GLFW_KEY_C);
        const float speed = in.shift ? 0.3f : 1.3f;
        const float3 f = make_float3(std::sin(a_.cam.yaw), 0, std::cos(a_.cam.yaw));
        const float3 r = make_float3(std::cos(a_.cam.yaw), 0, -std::sin(a_.cam.yaw));
        float3 v = (f * fwd + r * right) * speed + make_float3(0, up * speed * 0.7f, 0);
        float3 p = a_.cam.pos + v * dt;
        // D0: the room's box (the collision with the parts comes with the room, D3)
        p.x = std::clamp(p.x, -2.0f, 1.5f);
        p.y = std::clamp(p.y, 0.35f, 2.3f);
        p.z = std::clamp(p.z, -1.2f, 1.2f);
        a_.cam.pos = p;
        // the window's size for the output
        int ow = 0, oh = 0;
        glfwGetFramebufferSize(glfwGetCurrentContext(), &ow, &oh);
        if (ow <= 0 || oh <= 0) return;
        a_.renderer.draw(a_.cam, a_.set, ow, oh, in.now);
    }

    const void* image(int& w, int& h) override {
        w = h = 0;
        return nullptr;
    }
    unsigned texture(int& w, int& h) override {
        if (!a_.glReady) return 0;
        w = a_.renderer.outW();
        h = a_.renderer.outH();
        return a_.renderer.output();
    }

    void status(app::Status& s) override {
        auto choice = [&](const char* id, const std::string& title, std::vector<std::string> items, int active, const std::string& detail,
                          bool section = false) {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = id;
            c.title = title;
            c.items = std::move(items);
            c.active = active;
            c.detail = detail;
            c.section = section;
            s.controls.push_back(c);
        };
        choice("res", L("Çözünürlük", "Resolution"), {"720p", "1080p", "1440p", "4K", "8K"}, a_.resIndex,
               L("Çizim çözünürlüğü; ekrandan büyükse küçültülerek gelir", "The drawing's resolution; above the screen's it is scaled down"));
        choice("ssaa", "SSAA", {"1x", "2x", "4x", "8x", "16x"}, a_.ssaaIndex, L("Piksel başına gölgelendirilen örnek", "Samples shaded per pixel"));
        const std::vector<std::string> lmh = {"Low", "Medium", "High"};
        choice("tess", "Tessellation", lmh, a_.set.tess, "", true);
        choice("gfx", L("Grafik", "Graphics"), lmh, a_.set.gfx, "");
        choice("light", L("Işık", "Light"), lmh, a_.set.light, "");
        choice("fx", L("Efekt", "Effects"), lmh, a_.set.fx, "");
        if (!error_.empty()) s.chips.push_back({error_, true});
    }

    void screenshot(app::Host& host) override {
        if (!a_.glReady) return;
        std::vector<uint8_t> px;
        a_.renderer.readOutput(px);
        writePNG(host.capturePath() + ".png", a_.renderer.outW(), a_.renderer.outH(), px.data());
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        a_.loadConfig();
        std::string err;
        if (a_.glReady && !a_.renderer.reloadShaders(err)) {
            host.toast(L("Gölgelendirici hatası (konsolda)", "Shader error (in the console)"));
            std::printf("%s\n", err.c_str());
            return;
        }
        host.toast(L("Ayarlar ve gölgelendiriciler yeniden yüklendi", "Settings and shaders reloaded"));
    }

    void unload() override {
        if (a_.glReady) {
            a_.renderer.release();
            dzgl::restoreDefaults();
        }
        a_.glReady = false;
    }
    ~DenizaltiLive() override { unload(); }

private:
    app::SceneInfo info_ = describe();
    dz::DzApp a_;
    bool look_ = false;
    std::string error_;
};

}   // namespace

namespace denizalti {
app::SceneInfo info() { return DenizaltiLive::describe(); }
std::unique_ptr<app::Scene> make() { return DenizaltiLive::make(); }
int cli(int argc, char** argv) { return dz::runCli(argc, argv); }
}   // namespace denizalti
