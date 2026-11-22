// Scene 8 "Denizaltı" inside the application: a fleet submarine's control room, its light baked beforehand, every
// sample of the picture shaded on its own, the surfaces tessellated and displaced every frame.
// As the lighthouse's walk: the mouse looks (captured, no button), W/A/S/D walk, Space jumps, C crouches a little (held),
// Shift slow (looking closely), the right button the binoculars; Tab frees the cursor for the panel (and back), with it
// free a left drag looks.
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
        i.description = L("Tench sınıfı bir denizaltının kontrol odası, üç kırmızı gece lambası ve harita masasının lambasıyla. "
                          "İçinde bir insan gibi yürür, eğilir, göstergelere dürbünle yakından bakarsın. Işık ilk açılışta "
                          "birkaç dakikada pişirilir, sonra diskten okunur.",
                          "The control room of a Tench-class submarine, lit by three red night lamps and the chart table's lamp. "
                          "You walk it like a person, crouch, and look closely at the gauges through binoculars. The light is "
                          "baked for a few minutes on the first opening, then read from disk.");
        i.details = {
            {L("Önceden pişmiş ışık", "Baked lighting"),
             L("Odayı aydınlatan ışık canlıda hesaplanmaz. İlk açılışta OptiX ile yol izlenerek pişirilir: ışık haritasının "
               "2 mm'lik her noktasından 512 yol, 12 sekmeye kadar. Her noktaya ışığın miktarı ve geldiği yön (L1 küresel "
               "harmonikler) yazılır; her örnek bunu kabartılmış yüzeyin normaliyle okur, perçin ve dikişler de ışığa göre "
               "gölgelenir. Pişirme yaklaşık 5 dakika sürer.",
               "The light that fills the room is not computed live. It is path traced with OptiX on the first opening: 512 "
               "paths from every 2 mm texel of the light map, up to 12 bounces. Each texel stores the light's amount and the "
               "direction it comes from (L1 spherical harmonics), and every sample reads it with the displaced surface's "
               "normal, so rivets and seams are shaded by it too. The bake takes about 5 minutes.")},
            {L("Her karede tessellation", "Tessellation every frame"),
             L("Yüzeyler her karede kameraya göre küçük üçgenlere bölünür ve kabartılır: perçinler, kaynak dikişleri, vida "
               "dişleri, kabartmalı sac. Hedef üçgen kenarı Low'da yaklaşık 24 piksel, Medium'da 6 piksel, High'da 1,5 örnek. "
               "Kabartması olmayan düz boya daha kaba bölünür, üçgenler kabartmanın olduğu yere gider.",
               "Every frame the surfaces are cut into small triangles for the camera and displaced: rivets, weld seams, screw "
               "threads, tread plate. The target triangle edge is about 24 pixels on Low, 6 pixels on Medium and 1.5 samples "
               "on High. Flat paint with no relief is cut more coarsely, so the triangles go where the relief is.")},
            {L("Örnek başına gölgelendirme", "Per-sample supersampling"),
             L("Her piksel 16 örneğe kadar ayrı ayrı gölgelendirilir (SSAA). Örnek noktaları ekran kartının sabit desenidir; "
               "rastgele sayı ve önceki karelerden taşınan bilgi yoktur, bu yüzden görüntüde gren ya da kıpırtı olmaz. 8K "
               "16x canlıda değil, yalnız fotoğrafta (F12) 1920 × 1080'lik parçalar halinde çizilir.",
               "Every pixel is shaded at up to 16 separate samples (SSAA). The sample positions are the GPU's fixed pattern, "
               "with no random numbers and nothing carried over from earlier frames, so the picture has neither grain nor "
               "shimmer. 8K 16x is drawn only for the photo (F12), in tiles of 1920 × 1080.")},
            {L("Parlamalar ve yansımalar", "Highlights and reflections"),
             L("Dört ana lambanın, yani üç gece lambasıyla harita lambasının parlaması her örnekte küre biçimli bir ışık "
               "olarak hesaplanır; gölgesi pişmiş haritadan gelir. Metal ve parlak yüzeylerdeki yansımalar odaya dağıtılmış "
               "54 küp haritadan (yansıma yoklaması) okunur. Her pürüzlülük düzeyi fırında ayrı izlenir, sonradan "
               "bulanıklaştırılmaz.",
               "The highlights of the four key lamps, the three night lamps and the chart lamp, are computed for every sample "
               "as sphere lights; their shadows come from the bake. Reflections on metal and glossy surfaces are read from 54 "
               "cube maps (reflection probes) placed through the room. Each roughness level is traced separately in the bake, "
               "not blurred afterwards.")},
            {L("Pus ve parlama", "Haze and bloom"),
             L("Havadaki ince pus da önceden pişirilir: odayı 4 cm'lik hücrelere bölen bir hacme, lambaların gölgeleriyle "
               "birlikte havaya ulaşan ışık yazılır. Her karede bu ışık, kameranın önüne kurulan bir ızgarada (High'da "
               "160 × 90 × 64) bakış boyunca toplanır; lambaların ışık konileri buradan gelir. Parlak noktaların taşması "
               "(bloom) yarım çözünürlükte eklenir.",
               "The thin haze in the air is baked too: a volume of 4 cm cells stores the light that reaches the air, with the "
               "lamps' shadows. Each frame that light is gathered along the view in a grid fitted to the camera (160 × 90 × 64 "
               "on High), which is where the cones of lamplight come from. The glow around bright points (bloom) is added at "
               "half resolution.")},
            {L("Oda ve kaynakları", "The room and its sources"),
             L("Oda, Tench sınıfı USS Thornback'in (sonradan TCG Uluçalireis) kontrol odasıdır: perdeden perdeye 8,23 m, "
               "postalar 76,2 cm arayla. Ölçüler Balao sınıfının el kitaplarından ve Thornback'in planlarından alındı. "
               "Panolar, manifoldlar, dalış istasyonu, masadaki seyir haritası ve bütün etiketler kodla modellendi.",
               "The room is the control room of the Tench-class USS Thornback (later TCG Uluçalireis): 8.23 m from bulkhead "
               "to bulkhead, frames 76.2 cm apart. The measurements come from the Balao-class manuals and Thornback's plans. "
               "The panels, manifolds, diving station, the chart on the table and every label are modelled in code.")}};
        i.tags = {L("Önceden pişmiş ışık", "Baked lighting"), L("Tessellation", "Tessellation"), L("SSAA 16x", "SSAA 16x")};
        i.image = "assets/gallery/denizalti.jpg";
        i.accent = 0xD9583F;
        i.keys = {{L("Hareket", "Movement"),
                   {{"W|A|S|D", L("Yürü", "Walk")},
                    {"Shift", L("Basılı tutup yavaş yürü", "Hold to walk slowly")},
                    {L("Boşluk", "Space"), L("Zıpla", "Jump")},
                    {"C", L("Basılı tutup eğil", "Hold to crouch")},
                    {L("Fare", "Mouse"), L("Etrafa bak", "Look around")},
                    {L("Sağ tık", "Right click"), L("Basılı tutup dürbünle bak", "Hold for the binoculars")},
                    {"Tab", L("İmleci panel için bırak / geri al", "Free the cursor for the panel / back")},
                    {L("Sol tık", "Left click"), L("İmleç serbestken sürükleyip bak", "Drag to look while the cursor is free")},
                    {"R", L("Başa dön", "Back to the start")}}},
                  {L("Görüntü", "Image"), {{"↑|↓", L("Pozlamayı artır / azalt", "Exposure up / down")}}}};
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
            st.label = L("Pişmiş ışık okunuyor (ilk açılışta yaklaşık 5 dakika pişirilir)",
                         "Reading the baked light (baked once, about 5 minutes on the first opening)");
            st.fraction = 0.15f;
            ++st.step;
            return;
        default: {
            std::string err;
            const bool ok = a_.ensureLight(err, [&](float f, const std::string& what) {
                host.loadProgress(0.15f + 0.85f * f, L("Işık pişiriliyor (yalnız ilk açılışta) · ", "Baking the light (first opening only) · ") + what);
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
        // a person: feet on the deck, Space jumps (and falls back), C crouches a little while held
        a_.walk(fwd, right, in.shift, in.pressed(GLFW_KEY_SPACE), in.isDown(GLFW_KEY_C), dt);
        if (photo_.active) {   // the photo's next tile; the live picture waits (held)
            photoStep(host);
            return;
        }
        // 8K 16x is the photo's only (a live frame would take seconds and the card's memory): live at 8K 4x
        if (a_.resIndex == 4 && a_.ssaaIndex == 4) {
            if (!warned16_) host.toast(L("8K 16x yalnız fotoğrafta (F12) çizilir, canlı görüntü 8K 4x", "8K 16x is drawn only for the photo (F12); the live image is 8K 4x"));
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
               L("Çizimin çözünürlüğü, ekrandan büyükse küçültülerek gösterilir", "Render resolution, scaled down when larger than the screen"));
        choice("ssaa", "SSAA", {"1x", "2x", "4x", "8x", "16x"}, a_.ssaaIndex, L("Piksel başına ayrı gölgelendirilen örnek sayısı", "Samples shaded separately per pixel"));
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
