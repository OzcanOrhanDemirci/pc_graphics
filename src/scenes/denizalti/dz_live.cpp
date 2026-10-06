// Scene 8 "Denizaltı" inside the application: a fleet submarine's control room, its light baked beforehand, every
// sample of the picture shaded on its own, the surfaces tessellated and displaced every frame.
// As the lighthouse's walk: the mouse looks (captured, no button), W/A/S/D walk, Space/C up and down, Shift slow (looking
// closely), the right button the binoculars; Tab frees the cursor for the panel (and back), with it free a left drag looks.
#include "app/scene.h"
#include "app/theme.h"
#include "core/image_io.h"
#include "scenes/denizalti/dz_app.h"
#include "scenes/denizalti/dz_gl.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

using app::L;

class DenizaltiLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "denizalti";
        i.number = "08";
        i.title = L("Denizaltı", "The Submarine");
        i.subtitle = L("Gece kırmızısında bir kontrol odası", "A control room under night red");
        i.description = L("Tench sınıfı bir denizaltının kontrol odası, gece kırmızısı altında. Işık önceden, ilk açılışta "
                          "birkaç dakika boyunca izlenip pişirilir; ekran kartının bütün gücü görüntüye gidiyor: her örnek ayrı gölgelendirilir, "
                          "yüzeyler her karede yeniden bölünüp kabartılır.",
                          "The control room of a Tench-class submarine under its night red. The light is traced and baked "
                          "beforehand, for a few minutes on the first opening; the graphics card's whole strength goes to the picture: every sample shaded "
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
                    {L("Fare", "Mouse"), L("Bak", "Look")},
                    {"Tab", L("İmleç (panel için) / geri bakış", "Cursor (for the panel) / back to looking")},
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
        case 1:
            a_.root = host.root();
            a_.overrides = host.overrides();
            a_.loadConfig();
            a_.build();
            a_.applyChoices();
            st.label = L("Önceden pişirilmiş ışık okunuyor (ilk açılışta pişirilir: birkaç dakika)",
                         "Reading the baked light (baked on the first opening: a few minutes)");
            st.fraction = 0.15f;
            ++st.step;
            return;
        default: {
            std::string err;
            const bool ok = a_.ensureLight(err, [&](float f, const std::string& what) {
                host.loadProgress(0.15f + 0.85f * f, L("İlk açılış, ışık pişiriliyor: ", "First opening, baking the light: ") + what);
            });
            if (!ok) {
                st.error = err;
                return;
            }
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
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
            host.captureMouse(look_);   // the mouse looks from the start (as the lighthouse's walk)
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
            // the mouse to the right turns the view right: yaw grows towards +x, and +x is the picture's left (right =
            // forward x up)
            a_.cam.yaw -= in.mouseDX * sens;
            a_.cam.pitch = std::clamp(a_.cam.pitch - in.mouseDY * sens, -1.45f, 1.45f);
        }
        auto key = [&](int kc) { return in.isDown(kc) ? 1.0f : 0.0f; };
        const float fwd = key(GLFW_KEY_W) - key(GLFW_KEY_S), right = key(GLFW_KEY_D) - key(GLFW_KEY_A);
        const float up = key(GLFW_KEY_SPACE) - key(GLFW_KEY_C);
        const float speed = in.shift ? 0.3f : 1.3f;
        const float3 f = make_float3(std::sin(a_.cam.yaw), 0, std::cos(a_.cam.yaw));
        const float3 r = make_float3(-std::cos(a_.cam.yaw), 0, std::sin(a_.cam.yaw));   // the picture's right (forward x up)
        float3 v = (f * fwd + r * right) * speed + make_float3(0, up * speed * 0.7f, 0);
        a_.moveCamera(v * dt);   // the room's solid cells (10 cm round the camera) stop it, it slides along
        if (photo_.active) {   // the photo's next tile; the live picture waits (held)
            photoStep(host);
            return;
        }
        // 8K 16x is the photo's only (a live frame would take seconds and the card's memory): live at 8K 4x
        if (a_.resIndex == 4 && a_.ssaaIndex == 4) {
            if (!warned16_) host.toast(L("8K 16x yalnız fotoğrafta (F12): canlı görüntü 8K 4x", "8K 16x is for the photo only (F12): the live picture is 8K 4x"));
            warned16_ = true;
            a_.set.ssaa = 4;
        } else {
            warned16_ = false;
        }
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
        if (photo_.active && heldTex_) {
            w = heldW_;
            h = heldH_;
            return heldTex_;
        }
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
        if (photo_.active) {
            s.busy = float(photo_.done) / float(photo_.nx * photo_.ny);
            s.busyTitle = L("Fotoğraf hesaplanıyor", "Computing the photo");
            s.busyLabel = std::string(dz::kResNames[a_.resIndex]) + " · " + std::to_string(photo_.set.ssaa) + "x · " + L("parça ", "tile ") +
                          std::to_string(photo_.done + 1) + "/" + std::to_string(photo_.nx * photo_.ny);
        }
    }

    // F12: a photo at the panel's resolution and supersampling (8K 16x too): in tiles of 1920 x 1080 with a guard band
    // round each (the bloom and the haze see past the tile's edge), one tile a frame, the live picture held meanwhile
    void screenshot(app::Host& host) override {
        if (!a_.glReady || photo_.active) return;
        photo_ = Photo();
        photo_.active = true;
        photo_.path = host.capturePath() + ".png";
        photo_.set = a_.set;
        photo_.set.ssaa = dz::kSsaa[a_.ssaaIndex];   // the photo takes what was chosen (8K 16x included)
        photo_.W = dz::kResW[a_.resIndex];
        photo_.H = dz::kResH[a_.resIndex];
        photo_.cam = a_.cam;
        photo_.nx = (photo_.W + 1919) / 1920;
        photo_.ny = (photo_.H + 1079) / 1080;
        photo_.rgba.assign(size_t(photo_.W) * photo_.H * 4, 0);
        // hold the live picture
        if (!heldTex_) {
            dzgl::glCreateTextures(GL_TEXTURE_2D, 1, &heldTex_);
        } else {
            glDeleteTextures(1, &heldTex_);
            dzgl::glCreateTextures(GL_TEXTURE_2D, 1, &heldTex_);
        }
        heldW_ = a_.renderer.outW();
        heldH_ = a_.renderer.outH();
        dzgl::glTextureStorage2D(heldTex_, 1, GL_RGBA8, heldW_, heldH_);
        dzgl::glCopyImageSubData(a_.renderer.output(), GL_TEXTURE_2D, 0, 0, 0, 0, heldTex_, GL_TEXTURE_2D, 0, 0, 0, 0, heldW_, heldH_, 1);
        dzgl::restoreDefaults();
        host.toast(L("Fotoğraf hesaplanıyor: ", "Computing the photo: ") + std::string(dz::kResNames[a_.resIndex]) + " " +
                   std::to_string(photo_.set.ssaa) + "x");
    }

    // one tile of the photo
    void photoStep(app::Host& host) {
        const int guard = (photo_.nx * photo_.ny > 1) ? 192 : 0;
        const int t = photo_.done;
        const int tx = t % photo_.nx, ty = t / photo_.nx;
        const int x0 = tx * photo_.W / photo_.nx, x1 = (tx + 1) * photo_.W / photo_.nx;
        const int y0 = ty * photo_.H / photo_.ny, y1 = (ty + 1) * photo_.H / photo_.ny;
        dz::Settings s = photo_.set;
        s.fullW = photo_.W;
        s.fullH = photo_.H;
        s.tileX = x0 - guard;
        s.tileY = y0 - guard;
        s.width = (x1 - x0) + 2 * guard;
        s.height = (y1 - y0) + 2 * guard;
        a_.renderer.draw(photo_.cam, s, s.width, s.height, 0.0);
        std::vector<uint8_t> px;
        a_.renderer.readOutput(px);
        for (int y = y0; y < y1; ++y)
            std::memcpy(&photo_.rgba[(size_t(y) * photo_.W + x0) * 4], &px[(size_t(y - y0 + guard) * s.width + guard) * 4], size_t(x1 - x0) * 4);
        ++photo_.done;
        if (photo_.done >= photo_.nx * photo_.ny) {
            setPNGCompression(1);
            writePNG(photo_.path, photo_.W, photo_.H, photo_.rgba.data());
            host.toast(L("Fotoğraf kaydedildi: ", "Photo saved: ") + photo_.path);
            photo_ = Photo();
        }
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
        if (heldTex_) glDeleteTextures(1, &heldTex_);
        heldTex_ = 0;
        photo_ = Photo();
        if (a_.glReady) {
            a_.renderer.release();
            dzgl::restoreDefaults();
        }
        a_.glReady = false;
    }
    ~DenizaltiLive() override { unload(); }

private:
    struct Photo {
        bool active = false;
        std::string path;
        dz::Settings set;
        dz::Camera cam;
        int W = 0, H = 0, nx = 1, ny = 1, done = 0;
        std::vector<uint8_t> rgba;
    };
    app::SceneInfo info_ = describe();
    dz::DzApp a_;
    Photo photo_;
    unsigned heldTex_ = 0;
    int heldW_ = 0, heldH_ = 0;
    bool warned16_ = false;
    bool look_ = true;   // the mouse looks (captured); Tab frees the cursor for the panel
    std::string error_;
};

}   // namespace

namespace denizalti {
app::SceneInfo info() { return DenizaltiLive::describe(); }
std::unique_ptr<app::Scene> make() { return DenizaltiLive::make(); }
int cli(int argc, char** argv) { return dz::runCli(argc, argv); }
}   // namespace denizalti
