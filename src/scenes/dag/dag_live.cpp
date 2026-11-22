// Scene 13 "Dağ" inside the application: standing on a small railed terrace on Kaçkar's summit at sunset. The mouse looks
// (captured from the start, Tab gives the cursor back), W A S D walk, Shift runs, Alt sprints, Space jumps, C crouches, and the
// right mouse button looks through binoculars. The gap in the railing leads onto the mountain.
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/dag/dag_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <windows.h>
#include <GL/gl.h>
#include <cuda_gl_interop.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>

namespace {

using app::L;

const int kSamples[5] = {4, 8, 16, 32, 64};
constexpr float kWalkSpeed = 1.3f;

class DagLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "dag";
        i.number = "13";
        i.title = L("Dağ", "Mountain");
        i.subtitle = L("Kaçkar'da gün batımı, ufka kadar gerçek yeryüzü", "Sunset on Kaçkar, the real Earth to the horizon");
        i.description = L("Kaçkar'ın 3.937 metrelik doruğunda, korkuluklu küçük bir terastasın. Önünde Karadeniz'e batan güneş, sağında "
                          "Kafkaslar, arkanda Ağrı. Gördüğün her tepe gerçek, en uzağı yaklaşık 390 km ötede. Dürbünle uzağa bakar, "
                          "korkuluğun açıklığından dağa inersin.",
                          "You stand on a small railed terrace on Kaçkar's 3,937 m summit, the sun setting over the Black Sea ahead, "
                          "the Caucasus to the right, Ararat behind. Every peak is real, the farthest about 390 km away. Look far "
                          "with the binoculars or step through the gap onto the mountain.");
        i.details = {
            {L("LOD olmadan çizim mesafesi", "Draw distance without LOD"),
             L("Oyunlar uzağı ayrıntı düzeyiyle (LOD) kabalaştırır, çünkü göz her yere gidebilir. Burada görüş terasın "
               "çevresinden hesaplanır: tepeden görünebilen yeryüzünün her hücresi önceden bir kez bulunur ve üçgen olur, "
               "1 yay saniyelik (yaklaşık 30 m) yükseklik modelinin hücreleri, yüzlerce kilometre ötede bile. Uzak ağ "
               "yaklaşık 52 milyon üçgendir; hiçbir şey sonradan değişmez ya da belirmez, dürbün de aynı geometriyi gösterir.",
               "Games coarsen the distance with levels of detail (LOD) because the eye can go anywhere. Here the view is "
               "worked out from around the terrace: every cell of the Earth visible from the summit is found once beforehand "
               "and becomes triangles, the 1 arc second (about 30 m) elevation model's cells, even hundreds of kilometres "
               "away. The far mesh is about 52 million triangles; nothing switches or pops in, and the binoculars show the "
               "same geometry.")},
            {L("Hava ve Dünya'nın eğriliği", "The air and the Earth's curve"),
             L("Uzaktaki yerin solması bir sis efekti değil, havanın hesabıdır: Rayleigh ve Mie saçılması, ozonun soğurması, "
               "çoklu saçılma ve dağların havaya düşen gölgesi. Dünya geometrinin içinde eğridir; havanın kırılmasıyla etkin "
               "yarıçapı 7.323 km, uzak sırtlar ufkun ardına batar. Güneş yere havanın süzdüğü renkle gelir: dorukta altın, "
               "vadide mavi.",
               "The fading of the distant ground is not a fog effect but the air worked out: Rayleigh and Mie scattering, "
               "ozone absorption, multiple scattering and the mountains' shadows cast into the air. The Earth is curved in "
               "the geometry, with an effective radius of 7,323 km for the air's refraction, so far ridges sink behind the "
               "horizon. The sun reaches the ground in the colour the air lets through: gold on the peaks, blue in the valleys.")},
            {L("Sabit desenli süper örnekleme", "Fixed-pattern supersampling"),
             L("Her piksel, her karede aynı noktalardan atılan 16 ışınla çizilir (panelde 4-64) ve ışınlar Gauss biçimli bir "
               "süzgeçle birleştirilir. Önceki karelerden bir şey taşınmaz: görüntüde gren yoktur, durunca netleşen ya da "
               "hareket edince bulanan bir şey de yoktur. Yüzlerce kilometre ötedeki ince sırt çizgileri bu yüzden kıpırdamaz.",
               "Every pixel is drawn from 16 rays (4 to 64 in the panel) cast through the same points every frame and combined "
               "with a Gaussian filter. Nothing is carried over from earlier frames: there is no grain, and nothing sharpens "
               "when you stop or smears when you move. That keeps the thin ridgelines hundreds of kilometres away steady.")},
            {L("Terasın çevresindeki kaya", "The rock around the terrace"),
             L("Korkuluktan aşağı bakınca yakın zemin de ufuk kadar doğru olmalı. Terasın çevresinde iki halka ağ var, 1,5 "
               "km'ye kadar ince, 30 km'ye kadar daha kaba; taranmış kaya dokusu bunlarda gerçek kabartma olur. Üç taranmış "
               "kayanın 1.500 kopyası korkuluğun dışına dağılır. Yakında her örnek güneşe bir gölge ışını atar, uzak yer "
               "gölgesini önceden hesaplanmış ufuklardan alır.",
               "Looking down from the railing, the near ground has to hold up as well as the horizon. Two ring meshes "
               "surround the terrace, fine out to 1.5 km and coarser out to 30 km, and a scanned rock texture becomes real "
               "relief on them. 1,500 copies of three scanned rocks lie outside the railing. Near the summit each sample casts "
               "a shadow ray to the sun; the far ground takes its shadow from precomputed horizons.")},
            {L("Teşekkür", "Credits"),
             L("Yükseklik: Copernicus DEM GLO-30 (© DLR, © Airbus, Copernicus); Ermenistan ve Azerbaycan çevresinde SRTM (Mapzen Terrain Tiles, USGS). Renk: Sentinel-2 cloudless 2016, EOX (CC BY 4.0, "
               "değiştirilmiş Copernicus Sentinel verisi). Kaya: Poly Haven taramaları (CC0). assets/dag/KAYNAKLAR.md.",
               "Elevation: Copernicus DEM GLO-30 (© DLR, © Airbus, Copernicus); around Armenia and Azerbaijan SRTM (Mapzen Terrain Tiles, USGS). Colour: Sentinel-2 cloudless 2016 by EOX (CC BY 4.0, "
               "modified Copernicus Sentinel data). Rock: Poly Haven scans (CC0). assets/dag/KAYNAKLAR.md.")}};
        i.tags = {L("Çizim mesafesi", "Draw distance"), L("Atmosfer", "Atmosphere"), L("Gerçek arazi", "Real terrain")};
        i.image = "assets/gallery/dag.jpg";
        i.accent = 0xE89A6C;
        i.keys = {{L("Hareket", "Movement"),
                   {{L("Fare", "Mouse"), L("Etrafa bak", "Look around")},
                    {"W|A|S|D", L("Yürü", "Walk")},
                    {"Shift", L("Basılı tutup koş", "Hold to run")},
                    {"Alt", L("Basılı tutup on kat hızlı git", "Hold to move ten times faster")},
                    {L("Boşluk", "Space"), L("Zıpla", "Jump")},
                    {"C", L("Basılı tutup eğil", "Hold to crouch")},
                    {"Tab", L("İmleci panel için bırak / geri al", "Free the cursor for the panel / back")},
                    {"R", L("Başa dön", "Back to the start")}}},
                  {L("Bakış", "View"),
                   {{L("Sağ tık", "Right click"), L("Basılı tutup dürbünle bak", "Hold for the binoculars")},
                    {L("Sağ tık|+|Tekerlek", "Right click|+|Wheel"), L("Dürbünün büyütmesi (3x-30x)", "Binocular zoom (3x-30x)")},
                    {"1|2|3|4|5", L("Hazır bakış yönleri", "Preset views")},
                    {"↑|↓", L("Pozlamayı artır / azalt", "Exposure up / down")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<DagLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Yeryüzü okunuyor", "Reading the Earth");
            st.fraction = 0.02f;
            ++st.step;
            return;
        case 1: {
            a_.root = host.root();
            a_.overrides = host.overrides();
            a_.loadConfig();
            a_.progress = [&host](float f, const std::string& s) { host.loadProgress(f, s); };
            std::string err;
            if (!a_.prepareWorld(err)) {
                st.error = err;
                return;
            }
            st.label = L("Işın izleme yapıları kuruluyor", "Building the ray tracing structures");
            st.fraction = 0.6f;
            ++st.step;
            return;
        }
        default: {
            std::string err;
            if (!a_.initRenderer(err)) {
                st.error = err;
                return;
            }
            samples_ = a_.R.params.spp >= 64 ? 4 : a_.R.params.spp >= 32 ? 3 : a_.R.params.spp >= 16 ? 2 : a_.R.params.spp >= 8 ? 1 : 0;
            reset();
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(a_.width) * a_.height * sizeof(uchar4));
            a_.render();
            std::printf("%s\n", a_.summary().c_str());
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
        }
    }

    void reset() { goView(0); }
    void goView(int k) {
        a_.cam = DagApp::view(k);
        a_.cam.feet.y = a_.world.groundY(a_.cam.feet.x, a_.cam.feet.z);
        smoothFeet_ = a_.cam.feet.y;
        vy_ = 0.0f;
        crouch_ = 0.0f;
        vel_ = make_float3(0, 0, 0);
    }

    void frame(app::Host& host, const app::Input& in) override {
        const float dt = float(std::min(0.1, in.dt));
        if (!started_) {
            host.pace(0.0f);
            host.captureMouse(look_);
            started_ = true;
        }
        for (const auto& e : in.events)
            if (e.id == "samples" && e.item >= 0 && e.item < 5) {
                samples_ = e.item;
                a_.R.setPattern(kSamples[samples_], a_.cfg.f("filter.sigma", 0.40f));
            }
        if (in.pressed(GLFW_KEY_TAB)) {
            look_ = !look_;
            host.captureMouse(look_);
        }
        if (in.pressed(GLFW_KEY_R)) reset();
        for (int k = 0; k < std::min(5, DagApp::viewCount()); ++k)
            if (in.pressed(GLFW_KEY_1 + k)) goView(k);
        if (in.pressed(GLFW_KEY_UP)) {
            a_.exposureEv += 0.25f;
            a_.applyDisplay();
        }
        if (in.pressed(GLFW_KEY_DOWN)) {
            a_.exposureEv -= 0.25f;
            a_.applyDisplay();
        }
        // binoculars: the right button; the wheel while holding it zooms between 3x and 30x
        if (in.rightDown) {
            binoZoom_ = std::clamp(binoZoom_ * std::pow(1.15f, in.scroll), 3.0f, 30.0f);
        }
        const float target = in.rightDown ? binoZoom_ : 1.0f;
        zoom_ += (target - zoom_) * (1.0f - std::exp(-dt * 10.0f));
        const float baseFov = a_.cfg.f("fov", 45.0f);
        a_.cam.fovY = 2.0f * std::atan(std::tan(baseFov * 0.5f * M_PIf / 180.0f) / zoom_) * 180.0f / M_PIf;
        // looking: the mouse to the right turns the view right (the picture's right is forward x up, as the renderer's)
        const float sens = 0.0022f / zoom_;
        // (the first frames carry the jump from where the cursor was to the capture: not a look)
        if (settle_ > 0) --settle_;
        else if (look_) {
            a_.cam.yaw -= in.mouseDX * sens;
            a_.cam.pitch = std::clamp(a_.cam.pitch - in.mouseDY * sens, -1.45f, 1.45f);
        }
        movePlayer(in, dt);
        const float3& feet = a_.cam.feet;
        if (host.testing() && std::fmod(in.now, 1.0) < double(dt)) {
            std::printf("dag: ayak %.2f %.2f, icerde %d, yon %.1f\n", feet.x, feet.z, a_.world.inside(feet.x, feet.z, 0.0f) ? 1 : 0, -a_.cam.yaw * 180.0f / M_PIf);
            std::fflush(stdout);
        }

        a_.R.params.time = float(in.now);
        a_.render();
        if (!tex_) {
            glGenTextures(1, &tex_);
            glBindTexture(GL_TEXTURE_2D, tex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, a_.width, a_.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindTexture(GL_TEXTURE_2D, 0);
            if (cudaGraphicsGLRegisterImage(&res_, tex_, GL_TEXTURE_2D, cudaGraphicsRegisterFlagsWriteDiscard) != cudaSuccess) {
                cudaGetLastError();
                res_ = nullptr;
            }
        }
        if (res_ && cudaGraphicsMapResources(1, &res_, 0) == cudaSuccess) {
            cudaArray_t arr = nullptr;
            cudaGraphicsSubResourceGetMappedArray(&arr, res_, 0, 0);
            cudaMemcpy2DToArray(arr, 0, 0, a_.R.params.output, size_t(a_.width) * sizeof(uchar4), size_t(a_.width) * sizeof(uchar4), a_.height,
                                cudaMemcpyDeviceToDevice);
            cudaGraphicsUnmapResources(1, &res_, 0);
            texReady_ = true;
        } else {
            cudaMemcpy(pinned_, a_.R.params.output, size_t(a_.width) * a_.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
            texReady_ = false;
        }
    }

    const void* image(int& w, int& h) override {
        w = a_.width;
        h = a_.height;
        if (texReady_ && pinned_) cudaMemcpy(pinned_, a_.R.params.output, size_t(a_.width) * a_.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
        return pinned_;
    }
    unsigned texture(int& w, int& h) override {
        if (!texReady_) return 0;
        w = a_.width;
        h = a_.height;
        return tex_;
    }

    void status(app::Status& s) override {
        using theme::num;
        s.chips.push_back({num(double(a_.R.totalTriangles) / 1e6, 0) + L(" milyon üçgen", " million triangles"), true});
        s.chips.push_back({L("en uzak çizilen yer ", "farthest ground drawn ") + num(a_.farthestDrawn * 1e-3, 0) + " km"});
        if (zoom_ > 1.05f) {
            s.chips.push_back({L("dürbün ", "binoculars ") + num(zoom_, 0) + "x"});
            const std::string at = a_.lookingAt();
            if (!at.empty()) s.chips.push_back({at, true});
        }
        s.metrics.push_back({L("Kare", "Frame"), num(a_.R.renderMs + a_.R.displayMs, 1) + " ms"});
        app::Control c;
        c.kind = app::Control::Kind::Choice;
        c.id = "samples";
        c.title = L("Örnek", "Samples");
        c.items = {"4", "8", "16", "32", "64"};
        c.active = samples_;
        c.detail = L("Piksel başına ışın, her karede aynı desende", "Rays per pixel, the same pattern every frame");
        s.controls.push_back(c);
    }

    void screenshot(app::Host& host) override {
        a_.save(host.capturePath());
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        a_.loadConfig();
        std::string err;
        if (!a_.R.compile(err)) {
            host.toast(L("Derleme hatası: ", "Compile error: ") + err.substr(0, 200));
            std::fprintf(stderr, "%s\n", err.c_str());
            return;
        }
        a_.buildAir();
        a_.applyDisplay();
        host.toast(L("Ayarlar, hava tabloları ve programlar yeniden yüklendi", "Settings, air tables and programs reloaded"));
    }

    void unload() override {
        if (res_) cudaGraphicsUnregisterResource(res_);
        res_ = nullptr;
        if (tex_ && glfwGetCurrentContext()) glDeleteTextures(1, &tex_);
        tex_ = 0;
        texReady_ = false;
        a_.release();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }
    ~DagLive() override { unload(); }

private:
    void movePlayer(const app::Input& in, float dt) {
        auto key = [&](int kc) { return in.isDown(kc) ? 1.0f : 0.0f; };
        const float fwd = key(GLFW_KEY_W) - key(GLFW_KEY_S), right = key(GLFW_KEY_D) - key(GLFW_KEY_A);
        const float3 f = make_float3(-std::sin(a_.cam.yaw), 0.0f, -std::cos(a_.cam.yaw));
        const float3 r = normalize(cross(f, make_float3(0, 1, 0)));
        float3 want = f * fwd + r * right;
        const float wl = length(want);
        if (wl > 1.0f) want = want * (1.0f / wl);
        const bool sprint = in.isDown(GLFW_KEY_LEFT_ALT) || in.isDown(GLFW_KEY_RIGHT_ALT);
        const float speed = kWalkSpeed * (sprint ? 10.0f : (in.shift ? 2.0f : 1.0f));
        want = want * (speed * (1.0f - 0.5f * crouch_));
        vel_ += (want - vel_) * (1.0f - std::exp(-dt * 8.0f));
        constexpr float kBodyRadius = 0.32f;
        const float3 displacement = vel_ * dt;
        const int steps = std::max(1, int(std::ceil(length(displacement) / (kBodyRadius * 0.5f))));
        const float3 step = displacement * (1.0f / steps);
        float3 p = a_.cam.feet;
        for (int i = 0; i < steps; ++i) a_.world.moveOnGround(p, step, kBodyRadius);
        // the feet on the scree: a body does not step down into every gap between the stones (the mean ground under a stride, followed softly)
        const float e = 0.28f;
        const float ground = 0.2f * (a_.world.groundY(p.x, p.z) + a_.world.groundY(p.x + e, p.z) + a_.world.groundY(p.x - e, p.z) + a_.world.groundY(p.x, p.z + e) + a_.world.groundY(p.x, p.z - e));
        vy_ -= 9.81f * dt;
        p.y += vy_ * dt;
        const bool onTerrace = a_.world.inside(p.x, p.z, 0.0f);
        const bool followingGround = vy_ <= 0.0f && p.y <= smoothFeet_ + 1e-3f && (onTerrace || p.y <= ground + 0.2f);
        const bool onGround = p.y <= ground + 1e-3f || followingGround;
        if (onGround) {
            smoothFeet_ += (ground - smoothFeet_) * (1.0f - std::exp(-dt * 10.0f));
            if (!onTerrace) smoothFeet_ = std::max(smoothFeet_, ground - 0.2f);
            p.y = smoothFeet_;
            vy_ = 0.0f;
            if (in.pressed(GLFW_KEY_SPACE)) vy_ = 3.2f;
        } else {
            smoothFeet_ = ground;
        }
        a_.cam.feet = p;
        crouch_ += ((in.isDown(GLFW_KEY_C) ? 1.0f : 0.0f) - crouch_) * (1.0f - std::exp(-dt * 10.0f));
        const float moving = std::min(1.0f, length(vel_) / kWalkSpeed);
        stepPhase_ += dt * length(vel_) * 1.9f;
        a_.cam.eyeHeight = 1.7f - 0.7f * crouch_ + 0.012f * moving * std::sin(stepPhase_ * 2.0f);
    }

    app::SceneInfo info_ = describe();
    DagApp a_;
    uchar4* pinned_ = nullptr;
    GLuint tex_ = 0;
    cudaGraphicsResource_t res_ = nullptr;
    bool texReady_ = false;
    bool started_ = false, look_ = true;
    float3 vel_{0, 0, 0};
    float vy_ = 0.0f, crouch_ = 0.0f, stepPhase_ = 0.0f, smoothFeet_ = 0.0f;
    float zoom_ = 1.0f, binoZoom_ = 8.0f;
    int settle_ = 3;
    int samples_ = 2;
};

}   // namespace

namespace dag {
app::SceneInfo info() { return DagLive::describe(); }
std::unique_ptr<app::Scene> make() { return DagLive::make(); }
int cli(int argc, char** argv) { return runDagCli(argc, argv); }
}   // namespace dag
