// Scene 12 "Ateş" inside the application: a fire in a stone pit, outdoors on a moonless night, to look at. The mouse
// looks (captured from the start, Tab gives the cursor back), W A S D walk round the pit, C crouches, R lays the fire
// again. (Throwing wood on, a match, blowing and the wood's fast time were taken out when the scene was closed:
// docs/ates/DURUM.md)
#include "app/lang.h"
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/ates/ates_app.h"

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

class AtesLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "ates";
        i.number = "12";
        i.title = L("Ateş", "Fire");
        i.subtitle = L("Gecenin karanlığında taş ocakta yanan ateş", "A fire burning in a stone pit in the dark");
        i.description = L("Aysız bir gecede, açık havada taş bir ocakta yanan odun ateşi. Alevin biçimi, rengi, ışığı ve "
                          "kıvılcımları elle çizilmez, yanmanın fiziğinden gerçek zamanda hesaplanır. Ocağın çevresinde yürür, "
                          "çömelip yakından bakar, R ile ateşi baştan yakarsın.",
                          "A wood fire in a stone pit, outdoors on a moonless night. The flame's shape, colour and light and its "
                          "sparks are not drawn by hand but computed from the physics of burning in real time. Walk round the "
                          "pit, crouch to look closely, and press R to lay the fire again.");
        i.details = {
            {L("Yanan gaz", "The burning gas"),
             L("Ocağın üstünde 7,5 mm'lik hücrelerden bir ızgarada (128 × 192 × 128) sıcak gaz akar, saniyede 120 adım: "
               "kaldırma kuvveti, ısının genleştirdiği hacim, havanın değişen yoğunluğu. Odunun uçucuları oksijenle "
               "karıştıkları hızla yanar; karışım alev sıcaklığına ulaşamıyorsa, yani boğulmuş ya da havasızsa yanmaz. İs "
               "yakıt tarafında oluşur, alev cephesinde yanar.",
               "Over the pit hot gas flows on a grid of 7.5 mm cells (128 × 192 × 128), 120 steps a second: buoyancy, the "
               "volume heat expands, the air's changing density. The wood's volatiles burn as fast as they mix with oxygen; "
               "a mixture that cannot reach flame temperature, smothered or starved of air, does not burn. Soot forms on the "
               "fuel side and burns at the flame front.")},
            {L("Odun ve kor", "Wood and embers"),
             L("Her kütük yamalara bölünür, her yama yüzeyden öze 12 katmandır: ısınır, 100 °C'de kurur, Di Blasi'nin üç "
               "tepkimesiyle gaza, katrana ve kömüre ayrışır. Kömür yüzeyde oksijenle yanıp kor olur, bıraktığı kül onu "
               "örter. Alevin ve sıcak yüzeylerin ışıması her yamaya ulaşır, kütükler birbirini böyle ısıtır. Kodda yanmayı "
               "ya da sönmeyi söyleyen bir kural yoktur, hepsi bu hesaptan çıkar.",
               "Each log is cut into patches, each patch 12 layers from surface to core: it heats, dries at 100 °C and breaks "
               "down by Di Blasi's three reactions into gas, tar and char. The char burns with oxygen at the surface as an "
               "ember, and the ash it leaves covers it. The radiation of the flame and the hot surfaces reaches every patch, "
               "which is how the logs warm each other. No rule in the code says burn or go out; it all follows from this.")},
            {L("Ateşin ışığı", "The fire's light"),
             L("Alevin ışığı isin sıcaklığındaki Planck tayfıdır; dipteki mavi, alev cephesinin kendi ışımasıdır (CH* ve "
               "C₂*). Alev sabit adımlarla ışın yürütülerek çizilir; sıcak hava ışını kırar, arkadaki taşlar titrer. Taşlar, "
               "toprak ve kütükler her karede gazın ışıyan hücrelerinden aydınlanır, gölgeleri OptiX ile ışın izlenir. Korun "
               "kızıllığı yüzeyin kendi sıcaklığıdır.",
               "The flame's light is the Planck spectrum of its soot's temperature; the blue at its base is the flame front's "
               "own emission (CH* and C₂*). The flame is drawn by marching rays in fixed steps, and the hot air bends them, "
               "so the stones behind it shimmer. The stones, ground and logs are lit every frame by the gas's glowing cells, "
               "with shadows ray traced in OptiX. An ember's red is its surface's own temperature.")},
            {L("Kıvılcımlar", "Sparks"),
             L("Her kıvılcım yanan bir kömür puludur: odunun içinde buhar ve uçucu cebi patlayınca (çıtırtı) fırlar ya da "
               "alevin çektiği hava onu kızgın yüzeyden koparır. Gazın akışı onu taşır; ince pul tüyle yükselir, kalını "
               "düşer. Ulaşan oksijenle yanar, rengi kendi sıcaklığıdır; yanıp bitince ya da soğuyunca söner.",
               "Each spark is a burning flake of char: thrown out when a pocket of steam and volatiles bursts in the wood (a "
               "crackle), or torn off a glowing surface by the air the flame draws in. The gas's flow carries it; a thin flake "
               "rides the plume up, a thick one falls. It burns with the oxygen that reaches it, its colour its own "
               "temperature, and goes out burnt away or cooled.")},
            {L("Yüzeyler ve DLSS", "Surfaces and DLSS"),
             L("Ateşin aydınlattığı yüzeyler piksel başına tek ışınla çizilir, NVIDIA DLSS Ray Reconstruction kareleri "
               "birleştirir. Alev ve kıvılcımlar hareket vektörü taşımadığı için DLSS'ten sonra ayrı bir katmanda eklenir. "
               "Pozlama göz uyumu gibi kendini ayarlar; ateşin ulaşmadığı gök tam siyahtır.",
               "The firelit surfaces are drawn with one ray per pixel, and NVIDIA DLSS Ray Reconstruction combines the frames. "
               "The flame and the sparks carry no motion vectors, so they are added in a layer of their own after DLSS. The "
               "exposure adjusts itself like eye adaptation; the sky the fire does not reach stays pure black.")},
            {L("Teşekkür", "Credits"), L("NVIDIA DLSS (NVIDIA RTX SDK). Varlıklar: Poly Haven (CC0), docs/ates/VARLIK.md.",
                                         "NVIDIA DLSS (NVIDIA RTX SDK). Assets: Poly Haven (CC0), docs/ates/VARLIK.md.")}};
        i.tags = {L("Yanma fiziği", "Combustion"), L("Gaz akışı", "Fluid flow"), L("Işınım", "Radiation")};
        i.image = "assets/gallery/ates.jpg";
        i.accent = 0xE8894A;
        i.keys = {{L("Hareket", "Movement"),
                   {{L("Fare", "Mouse"), L("Etrafa bak", "Look around")},
                    {"W|A|S|D", L("Ocağın çevresinde yürü", "Walk round the pit")},
                    {"C", L("Basılı tutup çömel", "Hold to crouch")},
                    {"Tab", L("İmleci bırak / geri al", "Free the cursor / back")}}},
                  {L("Sahne", "Scene"),
                   {{"1|2|3|4", L("Hazır bakışlar", "Preset views")},
                    {"R", L("Ateşi baştan yak", "Lay the fire again")},
                    {"↑|↓", L("Pozlamayı artır / azalt", "Exposure up / down")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<AtesLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Taramalar okunuyor", "Reading the scans");
            st.fraction = 0.05f;
            ++st.step;
            return;
        case 1:
            a_.root = host.root();
            a_.overrides = host.overrides();
            a_.loadConfig();
            a_.loadAssets();
            st.label = L("Işın izleme yapıları kuruluyor", "Building the ray tracing structures");
            st.fraction = 0.3f;
            ++st.step;
            return;
        case 2: {
            std::string err;
            if (!a_.initRenderer(err)) {
                st.error = err;
                return;
            }
            st.label = L("Ateş yakılıyor", "Lighting the fire");
            st.fraction = 0.5f;
            ++st.step;
            return;
        }
        case 3:
            a_.initSim();
            st.label = L("Ateşin ilk saniyeleri hesaplanıyor", "Computing the fire's first seconds");
            st.fraction = 0.6f;
            ++st.step;
            return;
        default: {
            // the fire's first seconds before the eye sees it (the kindling's burner hands over to the logs)
            const float dtGas = a_.cfg.f("gas.dt", 1.0f / 120.0f);
            const float pre = a_.cfg.f("preroll", 10.0f);
            const int chunk = 120;
            for (int k = 0; k < chunk && warm_ < pre; ++k, warm_ += dtGas) a_.update(dtGas);
            st.fraction = 0.6f + 0.38f * std::min(1.0f, warm_ / pre);
            if (warm_ < pre) return;
            if (a_.cfg.i("dlss", 1) != 0 && !a_.setDlss(true)) std::fprintf(stderr, "ates: DLSS-RR kapali: %s\n", a_.R.dlssError.c_str());
            if (!a_.R.dlssOn()) a_.R.setPattern(a_.cfg.i("samples", 4), 0.42f);
            goView(0);
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(a_.width) * a_.height * sizeof(uchar4));
            a_.render(0.0f);
            std::printf("%s\n", a_.summary().c_str());
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
        }
    }

    void goView(int k) {
        a_.cam = AtesApp::view(k);
        crouch_ = a_.cam.eyeHeight < 1.0f ? 1.0f : 0.0f;
        baseEye_ = a_.cam.eyeHeight;
    }

    void frame(app::Host& host, const app::Input& in) override {
        const float dt = float(std::min(0.1, in.dt));
        if (!started_) {
            host.pace(0.0f);
            host.captureMouse(look_);
            started_ = true;
        }
        if (in.pressed(GLFW_KEY_TAB)) {
            look_ = !look_;
            host.captureMouse(look_);
        }
        if (in.pressed(GLFW_KEY_R)) {
            a_.lay(a_.layout);
            host.toast(L("Ateş baştan yakıldı", "The fire was laid again"));
        }
        for (int k = 0; k < AtesApp::viewCount(); ++k)
            if (in.pressed(GLFW_KEY_1 + k)) goView(k);
        if (in.pressed(GLFW_KEY_UP)) a_.exposureEv += 0.25f;
        if (in.pressed(GLFW_KEY_DOWN)) a_.exposureEv -= 0.25f;

        const float sens = 0.0022f;
        if (look_) {
            a_.cam.yaw -= in.mouseDX * sens;
            a_.cam.pitch = std::clamp(a_.cam.pitch - in.mouseDY * sens, -1.5f, 1.3f);
        }
        auto key = [&](int kc) { return in.isDown(kc) ? 1.0f : 0.0f; };
        const float fwd = key(GLFW_KEY_W) - key(GLFW_KEY_S), right = key(GLFW_KEY_D) - key(GLFW_KEY_A);
        const float3 f = make_float3(-std::sin(a_.cam.yaw), 0.0f, -std::cos(a_.cam.yaw));
        const float3 r = normalize(cross(f, make_float3(0, 1, 0)));
        float3 want = f * fwd + r * right;
        const float wl = length(want);
        if (wl > 1.0f) want = want * (1.0f / wl);
        want = want * (1.2f * (1.0f - 0.5f * crouch_));
        vel_ += (want - vel_) * (1.0f - std::exp(-dt * 8.0f));
        float3 p = a_.cam.feet + vel_ * dt;
        // the pit's stones keep the feet out of the fire; the dark beyond is far enough
        const float rr = std::sqrt(p.x * p.x + p.z * p.z);
        if (rr < 0.75f && rr > 1e-4f) p = p * (0.75f / rr);
        if (rr > 8.0f) p = p * (8.0f / rr);
        p.y = 0.0f;
        a_.cam.feet = p;
        crouch_ += ((in.isDown(GLFW_KEY_C) ? 1.0f : 0.0f) - crouch_) * (1.0f - std::exp(-dt * 8.0f));
        a_.cam.eyeHeight = 1.65f - 1.0f * crouch_;

        a_.update(dt);
        a_.render(dt);
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
        s.chips.push_back({L("ısı gücü ", "heat output ") + num(a_.gas.stats.hrr / 1000.0, 1) + " kW", true});
        s.chips.push_back({L("kalan odun ve kömür ", "wood and char left ") + num(a_.wood.stats.mass, 2) + " kg"});
        s.chips.push_back({L("en sıcak yüzey ", "hottest surface ") + num(a_.wood.stats.hottest - 273.15, 0) + " °C"});
        s.metrics.push_back({L("Kare", "Frame"), num(a_.R.renderMs + a_.R.volumeMs + a_.R.displayMs, 1) + " ms"});
        s.metrics.push_back({L("Gaz adımı", "Gas step"), num(a_.gas.stats.stepMs, 1) + " ms"});
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
        a_.R.dlssFrame.reset = true;
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
    ~AtesLive() override { unload(); }

private:
    app::SceneInfo info_ = describe();
    AtesApp a_;
    uchar4* pinned_ = nullptr;
    GLuint tex_ = 0;
    cudaGraphicsResource_t res_ = nullptr;
    bool texReady_ = false;
    bool started_ = false, look_ = true;
    float3 vel_{0, 0, 0};
    float crouch_ = 0.0f, baseEye_ = 1.65f;
    float warm_ = 0.0f;
};

}   // namespace

namespace ates {
app::SceneInfo info() { return AtesLive::describe(); }
std::unique_ptr<app::Scene> make() { return AtesLive::make(); }
int cli(int argc, char** argv) { return runAtesCli(argc, argv); }
}   // namespace ates
