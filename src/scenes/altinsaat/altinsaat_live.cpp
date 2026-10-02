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

using app::L;
using theme::thousands;

class AltinSaatLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "altinsaat";
        i.number = "01";
        i.title = L("Altın Saat", "Golden Hour");
        i.subtitle = L("Cam ve taş, tayfına ayrılan güneş", "Glass and stone, sunlight split into its colours");
        i.description = L("Beton bir oda, beş dar yarıktan giren alçak güneş ve huzmelere asılı on dört kristal prizma. Prizmalar ışığı "
                          "renklerine ayırıp duvarlara ve pusa saçıyor.",
                          "A concrete room, a low sun coming in through five narrow slits, and fourteen crystal prisms hanging in its "
                          "beams. The prisms split the light into its colours and scatter them over the walls and the haze.");
        i.details = {
            {L("Işık nasıl hesaplanıyor", "How the light is computed"),
             L("Spektral yol izleme: her ışın 360-830 nm arasından dört dalga boyu taşır. Hiçbir renk boyanmış değil; camın kırılma "
               "indisi dalga boyuyla değiştiği için tayflar kendiliğinden çıkar, CIE 1931 gözlemcisiyle renge çevrilir.",
               "Spectral path tracing: every ray carries four wavelengths between 360 and 830 nm. No colour is painted on: glass "
               "bends each wavelength differently, so the spectra appear by themselves and become colour through the CIE 1931 "
               "observer.")},
            {L("Güneş ve gök", "Sun and sky"),
             L("5778 K kara cisim bir güneş; ışığı Rayleigh, aerosol ve ozon soğurmalı bir atmosferden geçer, ufka yakın güneş bu "
               "yüzden turuncudur. Gök aynı atmosferden hesaplanmış spektral bir haritadır.",
               "A 5778 K black-body sun whose light passes through an atmosphere with Rayleigh, aerosol and ozone absorption, which "
               "is why a sun near the horizon is orange. The sky is a spectral map computed from the same atmosphere.")},
            {L("Kostikler", "Caustics"),
             L("Prizmaların ve suyun duvara düşürdüğü tayflar fotonlarla bulunur: güneşten cama gönderilen fotonlar ekran kartında bir "
               "ızgaraya dizilir (aşamalı foton haritası). Pustaki renkli yelpazeler foton ışınlarıyla.",
               "The spectra that the prisms and the water cast on the walls are found with photons: photons sent from the sun into "
               "the glass are sorted into a grid on the graphics card (progressive photon mapping). The coloured fans in the haze "
               "come from photon beams.")},
            {L("Malzemeler", "Materials"),
             L("Brüt beton, pirinç şeritli terrazzo, sıva, SF11 ve BK7 cam, su, sabun filmi (ince film girişimi). Yüzey ayrıntısı bir "
               "kez dokuya pişirilir: kare 2,3 kat hızlandı.",
               "Board-marked concrete, terrazzo with brass strips, plaster, SF11 and BK7 glass, water, soap film (thin-film "
               "interference). Surface detail is baked into textures once: frames became 2.3 times faster.")},
            {L("Durağan ve canlı", "Still and live"),
             L("Durağan kipte örnekler sınırsız birikir, yapay zekâ gürültü gidericisi albedo ve normal kılavuzuyla temizler. Canlı "
               "kipte 1280×720 çizilir, OptiX'in zamansal modeli 1440p'ye büyütür: 24 fps.",
               "In the still mode samples accumulate without end and the AI denoiser cleans them, guided by albedo and normals. In "
               "the live mode the scene is drawn at 1280×720 and OptiX's temporal model scales it up to 1440p: 24 fps.")},
            {L("Donanım", "Hardware"),
             L("C++20, CUDA ve OptiX 9.1; oyun motoru yok, RTX 4080'in ışın izleme çekirdeklerinde. Işınlar malzemelerine göre yeniden "
               "sıralanır; bu olmadan altı kat yavaştı.",
               "C++20, CUDA and OptiX 9.1; no game engine, on the RTX 4080's ray-tracing cores. Rays are regrouped by material "
               "before shading; without that it was six times slower.")}};
        i.tags = {L("Spektral yol izleme", "Spectral path tracing"), L("Foton haritası", "Photon mapping"),
                  L("Yapay zekâ gürültü giderici", "AI denoiser")};
        i.image = "assets/gallery/altinsaat.jpg";
        i.accent = 0xE6B566;
        i.keys = {{L("Işık", "Light"),
                   {{L("Boşluk", "Space"), L("Durağan ↔ canlı", "Still ↔ live")}, {"C", L("Sonraki kamera", "Next camera")},
                    {"↑|↓", L("Pozlama", "Exposure")}, {"←|→", L("Odak uzaklığı", "Focus distance")}, {"R", L("Birikimi sıfırla", "Start over")}}},
                  {L("Katmanlar", "Layers"),
                   {{"P", L("Fotonlar (kostikler)", "Photons (caustics)")}, {"V", L("Pus", "Haze")},
                    {"N", L("Doğrudan ışık örneklemesi", "Direct light sampling")}, {"D", L("Yapay zekâ gürültü giderici", "AI denoiser")},
                    {"G", L("Güneş lekesi bağlantısı", "Sun patch connection")}}},
                  {L("Görünüm", "View"), {{"1–5", L("Güzel, albedo, normal, kostikler, maliyet", "Beauty, albedo, normal, caustics, cost")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<AltinSaatLive>(); }

    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Spektral yol izleyici derleniyor", "Compiling the spectral path tracer");
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
            if (!a_.compile()) { st.error = L("Işın izleme programları derlenemedi (ayrıntı konsolda).", "The ray-tracing programs did not compile (see the console)."); return; }
            st.label = L("Oda kuruluyor, gök hesaplanıyor", "Building the room, computing the sky");
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
            st.label = L("İlk ışınlar", "First rays");
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
            host.toast(std::string(name) + (flag ? L(" açık", " on") : L(" kapalı", " off")));
        };
        if (in.pressed(GLFW_KEY_SPACE)) {
            a_.live = !a_.live;
            a_.setLive(a_.live);
            liveStart_ = in.now;
            host.toast(a_.live ? L("Canlı: su dalgalanıyor, toz ve köpükler süzülüyor", "Live: the water ripples, dust and bubbles drift")
                             : L("Durağan: örnekler birikiyor", "Still: the samples accumulate"));
        }
        if (in.pressed(GLFW_KEY_R)) a_.renderer.resetAccumulation();
        if (in.pressed(GLFW_KEY_C)) {
            a_.setCamera(a_.camera + 1);
            host.toast(L("Kamera ", "Camera ") + std::to_string(a_.camera + 1) + " / " + std::to_string(a_.scene.cameras.size()));
        }
        if (in.pressed(GLFW_KEY_P)) toggle(L("Fotonlar", "Photons"), p.enablePhotons);
        if (in.pressed(GLFW_KEY_V)) toggle(L("Pus", "Haze"), p.enableHaze);
        if (in.pressed(GLFW_KEY_N)) toggle(L("Doğrudan ışık örneklemesi", "Direct light sampling"), p.enableNee);
        if (in.pressed(GLFW_KEY_G)) toggle(L("Güneş lekesi bağlantısı", "Sun patch connection"), p.enableSunPatch);
        if (in.pressed(GLFW_KEY_D)) { a_.denoise = !a_.denoise; host.toast(a_.denoise ? L("Gürültü giderici açık", "Denoiser on") : L("Gürültü giderici kapalı", "Denoiser off")); }
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
        s.chips.push_back({a_.live ? L("Canlı", "Live") : L("Durağan", "Still"), true});
        s.chips.push_back({L("Kamera ", "Camera ") + std::to_string(a_.camera + 1) + " / " + std::to_string(a_.scene.cameras.size())});
        const char* views[] = {"", "Albedo", "Normal", L("Yalnız kostikler", "Caustics only"), L("Maliyet ısı haritası", "Cost heat map"), L("Görünüm 6", "View 6")};
        if (p.view > 0 && p.view < 6) s.chips.push_back({views[p.view], true});
        if (std::fabs(a_.ev - evDefault_) > 1e-3f) s.chips.push_back({L("Pozlama ", "Exposure ") + std::string(a_.ev > evDefault_ ? "+" : "") + num(a_.ev - evDefault_, 2) + " EV"});
        if (!p.enablePhotons) s.chips.push_back({L("Fotonlar kapalı", "Photons off")});
        if (!p.enableHaze) s.chips.push_back({L("Pus kapalı", "Haze off")});
        if (!p.enableNee) s.chips.push_back({L("NEE kapalı", "NEE off")});
        if (!a_.denoise) s.chips.push_back({L("Gürültü giderici kapalı", "Denoiser off")});
        if (p.enableSunPatch) s.chips.push_back({L("Güneş lekesi açık", "Sun patches on")});
        s.metrics.push_back({L("Yol izleme", "Path tracing"), num(t.render, 1) + " ms"});
        s.metrics.push_back({L("Fotonlar", "Photons"), num(t.photons + t.grid, 1) + " ms"});
        s.metrics.push_back({L("Gürültü giderici", "Denoiser"), num(a_.renderer.denoiseMs(), 1) + " ms"});
        s.metrics.push_back({L("Örnek / piksel", "Samples / pixel"), thousands(a_.renderer.samplesPerPixel())});
        if (!a_.live) {
            const unsigned spp = a_.renderer.samplesPerPixel();
            s.progress = std::min(1.0f, float(spp) / float(std::max(1u, a_.denoiseUntil)));
            s.progressLabel = spp < a_.denoiseUntil ? L("Örnekler birikiyor · ", "Accumulating · ") + thousands(spp) + " / " + thousands(a_.denoiseUntil)
                                                    : L("Yakınsadı · ", "Converged · ") + thousands(spp) + L(" örnek, ham görüntü", " samples, raw image");
        }
    }

    void screenshot(app::Host& host) override {
        a_.saveCapture(host.capturePath());   // PNG and linear EXR
        host.toast(L("Ekran görüntüsü kaydedildi (PNG ve EXR)", "Screenshot saved (PNG and EXR)"));
    }

    void reload(app::Host& host) override {
        a_.loadConfig();
        if (a_.compile()) {
            a_.applyConfig();
            a_.rebuildScene();
            host.toast(L("Ayarlar ve programlar yeniden yüklendi", "Settings and programs reloaded"));
        } else {
            host.toast(L("Derleme hatası: ayrıntı konsolda", "Compile error: see the console"));
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
