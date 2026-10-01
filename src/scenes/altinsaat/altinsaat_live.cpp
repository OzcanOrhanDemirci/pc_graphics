// Scene 1 "Altin saatte cam ve tas" inside the application: the spectral path tracer, still (accumulating towards the
// reference, about 30 ms of samples a frame) or live (the water moves, dust and bubbles drift; the AI denoiser's
// temporal model upscales 720p to the screen).
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/altinsaat/altinsaat_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

namespace {

std::string thousands(unsigned v) {   // 12.345, the Turkish way
    std::string s = std::to_string(v), out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i && (s.size() - i) % 3 == 0) out += '.';
        out += s[i];
    }
    return out;
}

class AltinSaatLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "altinsaat";
        i.number = "01";
        i.title = "Altın Saat";
        i.subtitle = "Cam ve taş, tayfına ayrılan güneş";
        i.description = "Tadao Ando dilinde bir oda: brüt beton, terrazzo, sığ bir havuz. Güneş ufkun sekiz derece üstünde ve yalnız beş dar yarıktan "
                        "giriyor; huzmelere asılı on dört kristal prizma ışığı tayfına ayırıp duvarlara saçıyor. Her ışın dalga boyuyla hesaplanıyor; "
                        "hiçbir renk boyanmış değil.";
        i.tags = {"Spektral yol izleme", "Foton haritası", "Yapay zekâ gürültü giderici"};
        i.image = "assets/gallery/altinsaat.jpg";
        i.accent = 0xE6B566;
        i.keys = {{"Işık", {{"Boşluk", "Durağan ↔ canlı"}, {"C", "Sonraki kamera"}, {"↑|↓", "Pozlama"}, {"←|→", "Odak uzaklığı"}, {"R", "Birikimi sıfırla"}}},
                  {"Katmanlar", {{"P", "Fotonlar (kostikler)"}, {"V", "Pus"}, {"N", "Doğrudan ışık örneklemesi"}, {"D", "Yapay zekâ gürültü giderici"},
                                 {"G", "Güneş lekesi bağlantısı"}}},
                  {"Görünüm", {{"1–5", "Güzel, albedo, normal, kostikler, maliyet"}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<AltinSaatLive>(); }

    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = "Spektral yol izleyici derleniyor";
            st.fraction = 0.05f;
            ++st.step;
            return;
        case 1: {
            a_.root = host.root();
            a_.overrides = host.overrides();
            a_.loadConfig();
            a_.ev = a_.cfg.f("exposure.ev", 0.0f);
            evDefault_ = a_.ev;
            a_.renderer.init(a_.width, a_.height, a_.root);
            if (!a_.compile()) { st.error = "Işın izleme programları derlenemedi (ayrıntı konsolda)."; return; }
            st.label = "Oda kuruluyor, gök hesaplanıyor";
            st.fraction = 0.6f;
            ++st.step;
            return;
        }
        case 2:
            a_.loadBlueNoise();
            a_.applyConfig();
            a_.renderer.params().view = 0;
            a_.renderer.params().sppPerLaunch = 1;
            a_.rebuildScene();
            st.label = "İlk ışınlar";
            st.fraction = 0.9f;
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
        Params& p = a_.renderer.params();
        auto toggle = [&](const char* name, int& flag) {
            flag = !flag;
            a_.renderer.resetAccumulation();
            host.toast(std::string(name) + (flag ? " açık" : " kapalı"));
        };
        if (in.pressed(GLFW_KEY_SPACE)) {
            a_.live = !a_.live;
            a_.setLive(a_.live);
            liveStart_ = in.now;
            host.toast(a_.live ? "Canlı: su dalgalanıyor, toz ve köpükler süzülüyor" : "Durağan: örnekler birikiyor");
        }
        if (in.pressed(GLFW_KEY_R)) a_.renderer.resetAccumulation();
        if (in.pressed(GLFW_KEY_C)) {
            a_.setCamera(a_.camera + 1);
            host.toast("Kamera " + std::to_string(a_.camera + 1) + " / " + std::to_string(a_.scene.cameras.size()));
        }
        if (in.pressed(GLFW_KEY_P)) toggle("Fotonlar", p.enablePhotons);
        if (in.pressed(GLFW_KEY_V)) toggle("Pus", p.enableHaze);
        if (in.pressed(GLFW_KEY_N)) toggle("Doğrudan ışık örneklemesi", p.enableNee);
        if (in.pressed(GLFW_KEY_G)) toggle("Güneş lekesi bağlantısı", p.enableSunPatch);
        if (in.pressed(GLFW_KEY_D)) { a_.denoise = !a_.denoise; host.toast(std::string("Gürültü giderici ") + (a_.denoise ? "açık" : "kapalı")); }
        for (int k = 0; k < 6; ++k)
            if (in.pressed(GLFW_KEY_1 + k)) p.view = k == 0 ? 0 : k;
        if (in.pressed(GLFW_KEY_UP)) { a_.ev += 0.25f; p.exposure = std::pow(2.0f, a_.ev); }
        if (in.pressed(GLFW_KEY_DOWN)) { a_.ev -= 0.25f; p.exposure = std::pow(2.0f, a_.ev); }
        if (in.pressed(GLFW_KEY_RIGHT)) { p.cam.focusDist *= 1.05f; a_.renderer.resetAccumulation(); }
        if (in.pressed(GLFW_KEY_LEFT)) { p.cam.focusDist /= 1.05f; a_.renderer.resetAccumulation(); }

        // still: keep accumulating for ~30 ms per shown frame; live: one frame of the moving scene
        const double t0 = nowSec();
        if (a_.live) a_.liveStep(float(in.now - liveStart_));
        else do { a_.renderer.renderFrame(); } while (nowSec() - t0 < 0.030);
        p.ditherFrame = a_.live ? int(a_.renderer.frame()) : 0;
        // AI denoiser: every frame live; still at most four times a second, then the last result is shown (the caustics
        // on top are always current). Past denoise.until samples the raw image has converged.
        const unsigned int spp = a_.renderer.samplesPerPixel();
        if (a_.denoise && p.view == 0 && (a_.live || spp < a_.denoiseUntil)) {
            if (a_.live || in.now - lastDenoise_ > 0.25 || spp < 16) {
                a_.renderer.denoise(0.0f, a_.live && a_.liveDenoiseCaustics);
                lastDenoise_ = in.now;
            } else {
                a_.renderer.reuseDenoised();
            }
        }
        a_.renderer.tonemap();
        cudaMemcpy(pinned_, p.output, size_t(a_.width) * a_.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
    }

    const void* image(int& w, int& h) override {
        w = int(a_.width);
        h = int(a_.height);
        return pinned_;
    }

    void status(app::Status& s) override {
        using theme::num;
        const Params& p = a_.renderer.params();
        const PassTimes& t = a_.renderer.times();
        s.chips.push_back({a_.live ? "Canlı" : "Durağan", true});
        s.chips.push_back({"Kamera " + std::to_string(a_.camera + 1) + " / " + std::to_string(a_.scene.cameras.size())});
        static const char* views[] = {"", "Albedo", "Normal", "Yalnız kostikler", "Maliyet ısı haritası", "Görünüm 6"};
        if (p.view > 0 && p.view < 6) s.chips.push_back({views[p.view], true});
        if (std::fabs(a_.ev - evDefault_) > 1e-3f) s.chips.push_back({"Pozlama " + std::string(a_.ev > evDefault_ ? "+" : "") + num(a_.ev - evDefault_, 2) + " EV"});
        if (!p.enablePhotons) s.chips.push_back({"Fotonlar kapalı"});
        if (!p.enableHaze) s.chips.push_back({"Pus kapalı"});
        if (!p.enableNee) s.chips.push_back({"NEE kapalı"});
        if (!a_.denoise) s.chips.push_back({"Gürültü giderici kapalı"});
        if (p.enableSunPatch) s.chips.push_back({"Güneş lekesi açık"});
        s.metrics.push_back({"Yol izleme", num(t.render, 1) + " ms"});
        s.metrics.push_back({"Fotonlar", num(t.photons + t.grid, 1) + " ms"});
        s.metrics.push_back({"Gürültü giderici", num(a_.renderer.denoiseMs(), 1) + " ms"});
        s.metrics.push_back({"Örnek / piksel", thousands(a_.renderer.samplesPerPixel())});
        if (!a_.live) {
            const unsigned spp = a_.renderer.samplesPerPixel();
            s.progress = std::min(1.0f, float(spp) / float(std::max(1u, a_.denoiseUntil)));
            s.progressLabel = spp < a_.denoiseUntil ? "Örnekler birikiyor · " + thousands(spp) + " / " + thousands(a_.denoiseUntil)
                                                    : "Yakınsadı · " + thousands(spp) + " örnek, ham görüntü";
        }
    }

    void screenshot(app::Host& host) override {
        a_.saveCapture(host.capturePath());   // PNG and linear EXR
        host.toast("Ekran görüntüsü kaydedildi (PNG ve EXR)");
    }

    void reload(app::Host& host) override {
        a_.loadConfig();
        if (a_.compile()) {
            a_.applyConfig();
            a_.rebuildScene();
            host.toast("Ayarlar ve programlar yeniden yüklendi");
        } else {
            host.toast("Derleme hatası: ayrıntı konsolda");
        }
    }

    void unload() override {
        a_.renderer.shutdown();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }

    ~AltinSaatLive() override { unload(); }

private:
    app::SceneInfo info_ = describe();
    AltinSaat a_;
    uchar4* pinned_ = nullptr;
    double liveStart_ = 0.0, lastDenoise_ = -1.0;
    float evDefault_ = 0.0f;
};

}   // namespace

namespace altinsaat {
app::SceneInfo info() { return AltinSaatLive::describe(); }
std::unique_ptr<app::Scene> make() { return AltinSaatLive::make(); }
int cli(int argc, char** argv) { return runAltinSaatCli(argc, argv); }
}   // namespace altinsaat
