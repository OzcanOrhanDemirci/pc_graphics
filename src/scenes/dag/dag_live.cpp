// Scene 13 "Dağ" inside the application: standing on a small railed terrace on Kaçkar's summit at sunset. The mouse looks
// (captured from the start, Tab gives the cursor back), W A S D walk, Shift runs, Space jumps, C crouches, the right mouse button
// looks through binoculars. The railing keeps you in; everything out to the air's limit of sight is the real ground.
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

class DagLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "dag";
        i.number = "13";
        i.title = L("Dağ", "Mountain");
        i.subtitle = L("Kaçkar'ın tepesinde gün batımı: ufka kadar gerçek yeryüzü", "Sunset on Kaçkar's summit: the real Earth out to the horizon");
        i.description = L("Küçük, korkuluklu bir seyir terasındasın, Kaçkar'ın 3.937 metrelik doruğunda. Aşağıda sırtlar ve Karadeniz; sağda Kafkaslar, "
                          "arkada Ağrı. Gördüğün her tepe gerçek, en uzağı yüzlerce kilometre ötede ve hava onu yavaş yavaş siler.",
                          "You stand on a small railed viewing terrace on Kaçkar's 3,937 metre summit. Below are ridges and the Black Sea; to the right the "
                          "Caucasus, behind you Ararat. Every peak you see is real, the farthest hundreds of kilometres away, and the air slowly "
                          "wipes it out.");
        i.details = {
            {L("Çizim mesafesi", "Draw distance"),
             L("Ayrıntı seviyesi yok. Terastan görünebilen yeryüzünün her hücresi önceden bulunur ve üçgen olur: gerçek yükseklik "
               "verisinin 30 metrelik hücreleri, yüzlerce kilometre ötede bile. Göz terastan çıkamadığı için hiçbir şey uzaklıkla "
               "değişmez, belirip kaybolmaz, dürbünle bakılınca aynı dağ durur.",
               "No level of detail. Every cell of the Earth the terrace can see is found beforehand and becomes triangles: the real "
               "elevation model's 30 metre cells, even hundreds of kilometres away. Since the eye cannot leave the terrace nothing "
               "changes with distance, nothing pops in, and through the binoculars the same mountain stands there.")},
            {L("Hava", "Air"),
             L("Uzaktaki yerin solması sis efekti değil: Rayleigh ve Mie saçılması, ozon, çoklu saçılma; dağların havadaki gölgesi. "
               "Dünya'nın eğriliği geometrinin içinde: uzak sırtlar ufkun arkasına batar. Güneş yere havanın süzdüğü renkle gelir: "
               "dorukta altın, vadide mavi.",
               "The fading of the distant ground is not a fog effect: Rayleigh and Mie scattering, ozone, multiple scattering, the "
               "mountains' shadows in the air. The Earth's curve is in the geometry: far ridges sink behind the horizon. The sun "
               "arrives in the colour the air lets through: gold on the peaks, blue in the valleys.")},
            {L("Net görüntü", "Sharp picture"),
             L("Her pikselde sabit desenli on altı ışın: kaba kuvvet süper örnekleme. Zamansal biriktirme yok, gren yok. Durunca "
               "güzelleşen görüntü de yok: her kare aynı kare.",
               "Sixteen rays a pixel on a fixed pattern: brute-force supersampling. No temporal accumulation, no grain, nothing that "
               "improves when you stop: every frame is the same frame.")},
            {L("Teşekkür", "Credits"),
             L("Yükseklik: Copernicus DEM GLO-30 (© DLR, © Airbus, Copernicus); Ermenistan ve Azerbaycan çevresinde SRTM (Mapzen Terrain Tiles, USGS). Renk: Sentinel-2 cloudless 2016, EOX (CC BY 4.0, "
               "değiştirilmiş Copernicus Sentinel verisi). assets/dag/KAYNAKLAR.md.",
               "Elevation: Copernicus DEM GLO-30 (© DLR, © Airbus, Copernicus); around Armenia and Azerbaijan SRTM (Mapzen Terrain Tiles, USGS). Colour: Sentinel-2 cloudless 2016 by EOX (CC BY 4.0, "
               "modified Copernicus Sentinel data). assets/dag/KAYNAKLAR.md.")}};
        i.tags = {L("Çizim mesafesi", "Draw distance"), L("Atmosfer", "Atmosphere"), L("Gerçek yer", "Real place")};
        i.image = "assets/gallery/dag.jpg";
        i.accent = 0xE89A6C;
        i.keys = {{L("Yürüyüş", "Walking"),
                   {{L("Fare", "Mouse"), L("Etrafa bak", "Look around")},
                    {"W A S D", L("Yürü", "Walk")},
                    {"Shift", L("Hızlı yürü", "Walk faster")},
                    {L("Boşluk", "Space"), L("Zıpla", "Jump")},
                    {"C", L("Eğil", "Crouch")},
                    {L("Sağ tık", "Right click"), L("Dürbün (tekerlek yaklaştırır)", "Binoculars (the wheel zooms)")},
                    {"Tab", L("İmleci bırak / al", "Release / take the cursor")},
                    {"1 2 3 4 5", L("Bakışlar", "Views")},
                    {"R", L("Başa dön", "Back to the start")},
                    {"↑|↓", L("Pozlama", "Exposure")}}}};
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
        auto key = [&](int kc) { return in.isDown(kc) ? 1.0f : 0.0f; };
        const float fwd = key(GLFW_KEY_W) - key(GLFW_KEY_S), right = key(GLFW_KEY_D) - key(GLFW_KEY_A);
        const float3 f = make_float3(-std::sin(a_.cam.yaw), 0.0f, -std::cos(a_.cam.yaw));
        const float3 r = normalize(cross(f, make_float3(0, 1, 0)));
        float3 want = f * fwd + r * right;
        const float wl = length(want);
        if (wl > 1.0f) want = want * (1.0f / wl);
        const float speed = in.shift ? 2.6f : 1.3f;
        want = want * (speed * (1.0f - 0.5f * crouch_));
        vel_ += (want - vel_) * (1.0f - std::exp(-dt * 8.0f));
        float3 p = a_.cam.feet + vel_ * dt;
        a_.world.keepIn(p.x, p.z, 0.32f);
        // the feet on the scree: a body does not step down into every gap between the stones (the mean ground under a stride, followed softly)
        const float e = 0.28f;
        const float ground = 0.2f * (a_.world.groundY(p.x, p.z) + a_.world.groundY(p.x + e, p.z) + a_.world.groundY(p.x - e, p.z) + a_.world.groundY(p.x, p.z + e) + a_.world.groundY(p.x, p.z - e));
        vy_ -= 9.81f * dt;
        p.y += vy_ * dt;
        const bool onGround = p.y <= ground + 1e-3f || (vy_ <= 0.0f && p.y <= smoothFeet_ + 1e-3f);
        if (onGround) {
            smoothFeet_ += (ground - smoothFeet_) * (1.0f - std::exp(-dt * 10.0f));
            p.y = smoothFeet_;
            vy_ = 0.0f;
            if (in.pressed(GLFW_KEY_SPACE)) vy_ = 3.2f;
        } else {
            smoothFeet_ = ground;
        }
        a_.cam.feet = p;
        if (host.testing() && std::fmod(in.now, 1.0) < double(dt)) {
            std::printf("dag: ayak %.2f %.2f, icerde %d, yon %.1f\n", p.x, p.z, a_.world.inside(p.x, p.z, 0.0f) ? 1 : 0, -a_.cam.yaw * 180.0f / M_PIf);
            std::fflush(stdout);
        }
        crouch_ += ((in.isDown(GLFW_KEY_C) ? 1.0f : 0.0f) - crouch_) * (1.0f - std::exp(-dt * 10.0f));
        const float moving = std::min(1.0f, length(vel_) / 1.3f);
        stepPhase_ += dt * length(vel_) * 1.9f;
        a_.cam.eyeHeight = 1.7f - 0.7f * crouch_ + 0.012f * moving * std::sin(stepPhase_ * 2.0f);

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
        c.title = L("ÖRNEK", "SAMPLES");
        c.items = {"4", "8", "16", "32", "64"};
        c.active = samples_;
        c.detail = L("Pikselde sabit desenli ışın", "Rays a pixel, on a fixed pattern");
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
        host.toast(L("Ayarlar ve programlar yeniden yüklendi", "Settings and programs reloaded"));
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
