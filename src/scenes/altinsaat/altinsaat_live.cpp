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
                          "renklerine ayırıp duvarlara ve pusa saçar. Durağan kipte görüntü örnek biriktikçe netleşir; canlı kipte su "
                          "dalgalanır, toz ve köpükler süzülür.",
                          "A concrete room, a low sun through five narrow slits, and fourteen crystal prisms in its beams, splitting the "
                          "light into colours over the walls and the haze. In still mode the image clears as samples build up; in live "
                          "mode the water ripples, dust and bubbles drift.");
        i.details = {
            {L("Tayfsal yol izleme", "Spectral path tracing"),
             L("Her ışın 360-830 nm arasından dört dalga boyu taşır. Hiçbir renk boyanmaz: camın kırılma indisi dalga boyuyla "
               "değiştiği için (Sellmeier formülü) tayflar kendiliğinden çıkar ve CIE 1931 gözlemcisiyle renge çevrilir. Prizmayı "
               "gören pikseller aynı rastgele sayılarla dört kez izlenir, camda her seferinde başka bir dalga boyu yol alır; "
               "prizmadaki renk gürültüsü böylece düşer.",
               "Every ray carries four wavelengths between 360 and 830 nm. No colour is painted on: the glass's refractive index "
               "changes with wavelength (the Sellmeier formula), so the spectra appear by themselves and become colour through the "
               "CIE 1931 observer. Pixels that see a prism are traced four times with the same random numbers, a different "
               "wavelength carrying on through the glass each time, which lowers the colour noise there.")},
            {L("Güneş ve gök", "Sun and sky"),
             L("Güneş, ufkun 8° üstünde 5778 K sıcaklıkta bir kara cisimdir. Işığı Rayleigh ve aerosol saçılması, ozon soğurması "
               "olan küresel bir atmosferden geçer; alçak güneş bu yüzden turuncudur. Gök, aynı atmosferden hesaplanmış "
               "512×256 piksellik, 48 tayf bantlı bir haritadır.",
               "The sun is a 5778 K black body 8° above the horizon. Its light passes through a spherical atmosphere with Rayleigh "
               "and aerosol scattering and ozone absorption, which is why the low sun is orange. The sky is a 512×256 map with 48 "
               "spectral bands, computed from the same atmosphere.")},
            {L("Kostik: foton haritası", "Caustics: photon mapping"),
             L("Prizmaların, su dolu cam kürenin ve havuzun duvarlara düşürdüğü tayflar fotonlarla bulunur. Her karede güneşten "
               "cama ve suya 4 milyon foton gönderilir, ekran kartında bir ızgaraya dizilir; toplama yarıçapı kare kare küçülür "
               "(aşamalı foton haritası). Pustaki renkli yelpazeleri foton ışınları, yani fotonun yolu boyunca pusa bırakılan "
               "ışık çizer.",
               "The spectra that the prisms, the water-filled glass globe and the pool cast on the walls are found with photons. "
               "Every frame 4 million photons are sent from the sun into the glass and water and sorted into a grid on the GPU; the "
               "gathering radius shrinks frame by frame (progressive photon mapping). The coloured fans in the haze come from photon "
               "beams, light left in the haze along each photon's path.")},
            {L("Malzemeler", "Materials"),
             L("Kalıp izli brüt beton, pirinç şeritli terrazzo zemin, beyaz sıva, SF11 ve BK7 cam, su ve sabun köpükleri; "
               "köpüklerin renkleri ince film girişiminden gelir. Yüzey ayrıntısı her ışında yeniden hesaplanmaz, açılışta bir kez "
               "dokuya pişirilir: bu, kareyi 2,3 kat hızlandırdı.",
               "Board-marked concrete, a terrazzo floor with brass strips, white plaster, SF11 and BK7 glass, water and soap "
               "bubbles, whose colours come from thin-film interference. Surface detail is not recomputed for every ray but baked "
               "into a texture once at load time, which made frames 2.3 times faster.")},
            {L("Durağan ve canlı kip", "Still and live mode"),
             L("Durağan kipte örnekler sınırsız birikir. OptiX'in yapay zekâ gürültü gidericisi, albedo ve normal kılavuzuyla "
               "görüntüyü 8.192 örneğe kadar temizler, sonra ham görüntü gösterilir; kostikler gökkuşakları bulanmasın diye "
               "gürültü gidericiye girmez. Canlı kipte 1280×720 çizilir, OptiX'in zamansal modeli 2560×1440'a büyütür.",
               "In still mode samples accumulate without end. OptiX's AI denoiser, guided by albedo and normals, cleans the image up "
               "to 8,192 samples, then the raw image is shown; the caustics bypass the denoiser so the rainbows stay sharp. In live "
               "mode the scene is drawn at 1280×720 and OptiX's temporal model scales it up to 2560×1440.")},
            {L("Motor ve donanım", "Engine and hardware"),
             L("C++20, CUDA ve OptiX 9.1 ile yazıldı; oyun motoru yok, ışınlar RTX 4080'in ışın izleme çekirdeklerinde izlenir. "
               "Işınlar gölgelendirmeden önce çarptıkları malzemeye göre yeniden gruplanır (OptiX SER); bu olmadan kare altı kat "
               "yavaştı.",
               "Written in C++20, CUDA and OptiX 9.1 with no game engine; rays are traced on the RTX 4080's ray-tracing cores. "
               "Before shading, rays are regrouped by the material they hit (OptiX SER); without that a frame was six times "
               "slower.")}};
        i.tags = {L("Tayfsal yol izleme", "Spectral path tracing"), L("Foton haritası", "Photon mapping"),
                  L("Gürültü giderme", "AI denoising")};
        i.image = "assets/gallery/altinsaat.jpg";
        i.accent = 0xE6B566;
        i.keys = {{L("Kip ve kamera", "Mode and camera"),
                   {{L("Boşluk", "Space"), L("Durağan / canlı kip", "Still / live mode")}, {"C", L("Sonraki kamera", "Next camera")},
                    {"←|→", L("Odağı yaklaştır / uzaklaştır", "Focus nearer / farther")},
                    {"↑|↓", L("Pozlamayı artır / azalt", "Exposure up / down")}, {"R", L("Birikimi baştan başlat", "Restart the accumulation")}}},
                  {L("Katmanlar", "Layers"),
                   {{"P", L("Fotonları aç / kapat (kostik)", "Photons on / off (caustics)")}, {"V", L("Pusu aç / kapat", "Haze on / off")},
                    {"N", L("Doğrudan ışık örneklemesini aç / kapat", "Direct light sampling on / off")},
                    {"G", L("Güneşli yüzeyleri ışık say (aç / kapat)", "Sunlit patches as lights (on / off)")},
                    {"D", L("Gürültü gidericiyi aç / kapat", "Denoiser on / off")}}},
                  {L("Görünüm", "View"),
                   {{"1", L("Son görüntü", "Final image")}, {"2|3", L("Albedo / normaller", "Albedo / normals")},
                    {"4", L("Yalnız kostikler", "Caustics only")}, {"5", L("Maliyet ısı haritası", "Cost heat map")},
                    {"6", L("Ham görüntü, gürültü giderilmeden", "Raw image, not denoised")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<AltinSaatLive>(); }

    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Tayfsal yol izleyici derleniyor", "Compiling the spectral path tracer");
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
        if (in.pressed(GLFW_KEY_G)) toggle(L("Güneş lekesi örneklemesi", "Sun patch sampling"), p.enableSunPatch);
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
        const char* views[] = {"", "Albedo", "Normal", L("Yalnız kostikler", "Caustics only"), L("Maliyet ısı haritası", "Cost heat map"), L("Ham görüntü", "Raw image")};
        if (p.view > 0 && p.view < 6) s.chips.push_back({views[p.view], true});
        if (std::fabs(a_.ev - evDefault_) > 1e-3f) s.chips.push_back({L("Pozlama ", "Exposure ") + std::string(a_.ev > evDefault_ ? "+" : "") + num(a_.ev - evDefault_, 2) + " EV"});
        if (!p.enablePhotons) s.chips.push_back({L("Fotonlar kapalı", "Photons off")});
        if (!p.enableHaze) s.chips.push_back({L("Pus kapalı", "Haze off")});
        if (!p.enableNee) s.chips.push_back({L("Doğrudan ışık kapalı", "Direct light sampling off")});
        if (!a_.denoise) s.chips.push_back({L("Gürültü giderici kapalı", "Denoiser off")});
        if (p.enableSunPatch) s.chips.push_back({L("Güneş lekesi örneklemesi açık", "Sun patch sampling on")});
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
