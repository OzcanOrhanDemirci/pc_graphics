// Scene 12 "Ateş" inside the application: a fire in a stone pit, outdoors on a moonless night. The mouse looks (captured
// from the start, Tab gives the cursor back), W A S D walk round the pit, C crouches; the left button (held) blows
// along the look, F strikes a match, Q throws on a handful of twigs, E a log; the panel sets the wood's time.
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

const float kScales[5] = {1.0f, 5.0f, 15.0f, 30.0f, 60.0f};

class AtesLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "ates";
        i.number = "12";
        i.title = L("Ateş", "Fire");
        i.subtitle = L("Karanlıkta, açık havada taş bir ocakta yanan ateş", "A fire burning in a stone pit, outdoors in the dark");
        i.description = L("Taş bir ocakta odun ateşi. Yakıtı gerçekten azalır, harlanır, kora döner; fazla odunla boğulur, "
                          "kordan üfleyince yeniden alev alır. Hiçbiri kuralla yazılmadı: hepsi yanmanın fiziğinden çıkıyor.",
                          "A wood fire in a stone pit. Its fuel really runs down; it flares, sinks to embers, chokes under too much "
                          "wood and flames again from its embers when blown on. None of it is scripted: it all comes out of the "
                          "physics of burning.");
        i.details = {
            {L("Gaz", "Gas"),
             L("Ocağın üstünde 7,5 milimetrelik bir ızgarada sıcak gaz akar: kaldırma, ısının genleştirdiği hacim, havanın "
               "değişen yoğunluğu. Odunun uçucuları oksijenle karıştıkları hızla yanar; karışım alev sıcaklığına ulaşamıyorsa "
               "(boğulan, havasız) yanmaz. İs yakıt tarafında oluşur, alev cephesinde yanar; ışığını Planck yasasıyla saçar.",
               "Over the pit hot gas flows on a 7.5 millimetre grid: buoyancy, the volume heat expands, the air's changing "
               "density. The wood's volatiles burn as fast as they mix with oxygen; a mixture that cannot reach flame "
               "temperature (smothered, starved of air) does not burn. Soot forms on the fuel side, burns at the flame front, "
               "and sends out its light by Planck's law.")},
            {L("Odun", "Wood"),
             L("Her kütük yamalara bölünür, her yama yüzeyinden özüne katman katman: ısınır, kurur, Di Blasi'nin üç tepkimesiyle "
               "uçucuya ve köre ayrışır. Kömür yüzeyde oksijenle yanar (kor), külü onu örter; üfleyince kül kalkar, kor "
               "harlanır. Kütükler birbirlerini ışımayla ısıtır: tek kütük yanmaz.",
               "Each log is cut into patches, each patch layered from its surface to its core: it heats, dries and breaks "
               "down by Di Blasi's three reactions into volatiles and char. The char burns at the surface with oxygen (an "
               "ember), its ash covers it; blown on, the ash lifts and the ember flares. The logs warm each other by "
               "radiation: a single log will not burn.")},
            {L("Işık", "Light"),
             L("Ateşin ışığı gazın kendi ışımasından gelir: taşlar, toprak ve kütükler her karede ondan aydınlanır, gölgeleri "
               "ışın izlemeyle. Alevin rengi isin sıcaklığından, korun kızıllığı yüzeyinkinden.",
               "The fire's light comes from the gas's own emission: the stones, the ground and the logs are lit by it each "
               "frame, their shadows ray traced. The flame's colour is its soot's temperature, the ember's red its surface's.")},
            {L("Teşekkür", "Credits"), L("NVIDIA DLSS (NVIDIA RTX SDK). Varlıklar: Poly Haven (CC0), docs/ates/VARLIK.md.",
                                         "NVIDIA DLSS (NVIDIA RTX SDK). Assets: Poly Haven (CC0), docs/ates/VARLIK.md.")}};
        i.tags = {L("Fizik", "Physics"), L("Yanma", "Combustion"), L("Işık", "Light")};
        i.image = "assets/gallery/ates.jpg";
        i.accent = 0xE8894A;
        i.keys = {{L("Ateş", "Fire"),
                   {{L("Sol tık (basılı)", "Left click (held)"), L("Üfle", "Blow")},
                    {"F", L("Kibrit çak", "Strike a match")},
                    {"Q", L("Bir avuç çalı at", "Throw on a handful of twigs")},
                    {"E", L("Kütük at", "Throw on a log")},
                    {"T", L("Zamanı hızlandır", "Speed time up")}}},
                  {L("Yürüyüş", "Walking"),
                   {{L("Fare", "Mouse"), L("Etrafa bak", "Look around")},
                    {"W A S D", L("Yürü", "Walk")},
                    {"C", L("Çömel", "Crouch")},
                    {"Tab", L("İmleci bırak / al", "Release / take the cursor")},
                    {"1 2 3 4", L("Bakışlar", "Views")},
                    {"R", L("Ateşi baştan yak", "Lay the fire again")},
                    {"↑|↓", L("Pozlama", "Exposure")}}}};
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
            st.label = L("Alevler doğruluyor", "The flames standing up");
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
        for (const auto& e : in.events) {
            if (e.id == "time" && e.item >= 0) setScale(e.item);
            if (e.id == "match") a_.strikeMatch();
            if (e.id == "twigs") a_.addFuel(0);
            if (e.id == "log") a_.addFuel(1);
        }
        if (in.pressed(GLFW_KEY_TAB)) {
            look_ = !look_;
            host.captureMouse(look_);
        }
        if (in.pressed(GLFW_KEY_R)) {
            a_.lay(a_.layout);
            host.toast(L("Ateş yeniden yakıldı", "The fire laid again"));
        }
        for (int k = 0; k < AtesApp::viewCount(); ++k)
            if (in.pressed(GLFW_KEY_1 + k)) goView(k);
        if (in.pressed(GLFW_KEY_UP)) a_.exposureEv += 0.25f;
        if (in.pressed(GLFW_KEY_DOWN)) a_.exposureEv -= 0.25f;
        if (in.pressed(GLFW_KEY_F)) {
            a_.strikeMatch();
            host.toast(L("Kibrit çakıldı", "A match struck"));
        }
        if (in.pressed(GLFW_KEY_Q)) a_.addFuel(0);
        if (in.pressed(GLFW_KEY_E)) a_.addFuel(1);
        if (in.pressed(GLFW_KEY_T)) setScale((scale_ + 1) % 5);
        a_.blowing = in.leftDown && look_;

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

    void setScale(int k) {
        scale_ = std::clamp(k, 0, 4);
        a_.timeScale = kScales[scale_];
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
        s.chips.push_back({num(a_.gas.stats.hrr / 1000.0, 1) + " kW", true});
        s.chips.push_back({num(a_.wood.stats.mass, 2) + L(" kg odun", " kg wood")});
        s.chips.push_back({L("kor ", "embers ") + num(a_.wood.stats.hottest - 273.15, 0) + " °C"});
        s.metrics.push_back({L("Kare", "Frame"), num(a_.R.renderMs + a_.R.volumeMs + a_.R.displayMs, 1) + " ms"});
        s.metrics.push_back({L("Gaz", "Gas"), num(a_.gas.stats.stepMs, 1) + " ms"});
        app::Control c;
        c.kind = app::Control::Kind::Choice;
        c.id = "time";
        c.title = L("ZAMAN", "TIME");
        c.items = {"×1", "×5", "×15", "×30", "×60"};
        c.active = scale_;
        c.detail = L("Odunun zamanı; alev hep gerçek hızında", "The wood's time; the flame always runs real time");
        s.controls.push_back(c);
        app::Control m;
        m.kind = app::Control::Kind::Button;
        m.id = "match";
        m.text = L("Kibrit", "Match");
        m.keys = "F";
        m.section = true;
        s.controls.push_back(m);
        app::Control t;
        t.kind = app::Control::Kind::Button;
        t.id = "twigs";
        t.text = L("Çalı", "Twigs");
        t.keys = "Q";
        s.controls.push_back(t);
        app::Control l;
        l.kind = app::Control::Kind::Button;
        l.id = "log";
        l.text = L("Kütük", "Log");
        l.keys = "E";
        s.controls.push_back(l);
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
    int scale_ = 0;
};

}   // namespace

namespace ates {
app::SceneInfo info() { return AtesLive::describe(); }
std::unique_ptr<app::Scene> make() { return AtesLive::make(); }
int cli(int argc, char** argv) { return runAtesCli(argc, argv); }
}   // namespace ates
