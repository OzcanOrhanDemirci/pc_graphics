// Scene 11 "Orman" inside the application: walking in a small patch of rainforest, early morning after the rain. The
// mouse looks (captured from the start, Tab gives the cursor back), W A S D walk, Shift runs, Space jumps, C crouches
// (the lighthouse's walk: a body on its feet, no flying). The light is baked at load; the sun through the leaves is
// traced every frame.
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/orman/orman_app.h"

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

const int kSamples[4] = {1, 4, 8, 16};
const int kDlss = 4;   // the panel's fifth choice: DLSS Ray Reconstruction

class OrmanLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "orman";
        i.number = "11";
        i.title = L("Orman", "Rainforest");
        i.subtitle = L("Yağmurdan sonra sabah, sık bir yağmur ormanı", "A dense rainforest, early morning after rain");
        i.description = L("Bir derenin kıyısında, her yanı bitkiyle dolu bir yağmur ormanında yürürsün: eğrelti, kök, yosun, taş, "
                          "devrik gövde. Güneş yaprakların arasından süzülür, yerde pus durur. Orman bir duvarla bitmez; dümdüz "
                          "yürürsen 396 m sonra dereye geri dönersin.",
                          "You walk by a stream in a rainforest full of plants on every side: ferns, roots, moss, stones, a fallen "
                          "trunk. The sun filters through the leaves and mist lies on the ground. No wall ends the forest; walk "
                          "straight on and you reach the stream again after 396 m.");
        i.details = {
            {L("Katman katman yoğunluk", "Density in layers"),
             L("Orman katlardan kurulur, her katın yeri bir sebepten: yerde düşmüş yapraklar ve eğreltiler, nemli kıyıda daha "
               "sık; göz hizasında aralarında geçit kalan çalılıklar; onların üstünde fidanlar, genç ağaçlar ve tepe. Kayalar "
               "kıyıda yarı gömülü, taşlar yatakta, devrik gövde derenin üstünde. Düşmüş yapraklar yaprak atlaslarından tek "
               "tek kesilir ve zeminin üçgenlerine yatırılır.",
               "The forest is built in layers, each placed for a reason: fallen leaves and ferns on the ground, thicker on the "
               "moist banks; thickets at eye height with gaps between them; saplings, young trees and the canopy above. Rocks "
               "sit half buried on the banks, stones in the bed, the fallen trunk across the stream. The fallen leaves are cut "
               "one by one from leaf atlases and laid on the ground's triangles.")},
            {L("Kopyalanan taramalar", "Instanced scans"),
             L("Her bitki, taş ve gövde açık lisanslı gerçek bir taramadır; bir model binlerce kez kopyalanır ama bellekte bir "
               "kez durur. Derenin çevresindeki 44 m'lik parça, kenarları birbirine oturan tek bir orman karosunun "
               "kopyalarıyla çevrilidir: 9 × 9 karo, 396 m. Son karo ilkine dikilidir, orman simit gibi kendine döner. Dere "
               "kopyalanmaz, geçtiği her karede kendi şeridi ayrı büyür.",
               "Every plant, stone and trunk is a real open-licence scan; one model is copied thousands of times but stays in "
               "memory once. The 44 m patch around the stream is surrounded by copies of one forest tile whose sides meet: "
               "9 × 9 tiles, 396 m. The last tile is joined to the first, so the forest wraps round like a torus. The stream "
               "is not copied: each tile it crosses grows its own band of it.")},
            {L("Tek ışın, DLSS Ray Reconstruction", "One ray, DLSS Ray Reconstruction"),
             L("Görüntü OptiX ile ışın izlenerek çizilir. Dallar ve yaprakçıklar pikselden incedir, birkaç sabit ışın yürürken "
               "onları titretir. Bu yüzden her karede piksel başına tek ışın, pikselin içinde 16 adımlık bir dizinin sıradaki "
               "noktasına gider ve NVIDIA DLSS Ray Reconstruction kareleri hareket vektörleriyle birleştirir; büyütme yapılmaz. "
               "Kıyas için panelde 1-16 sabit örnek de var.",
               "The image is ray traced with OptiX. Twigs and leaflets are finer than a pixel, and a few fixed rays make them "
               "shimmer as you walk. So each frame sends one ray per pixel to the next point of a 16-step sequence inside it, "
               "and NVIDIA DLSS Ray Reconstruction combines the frames along motion vectors, with no upscaling. For comparison "
               "the panel also offers 1 to 16 fixed samples.")},
            {L("Pişmiş gök ışığı, canlı güneş", "Baked skylight, live sun"),
             L("Gök ışığı ve ışığın bir sekmesi yüklemede pişirilir: ormanın içinde 30 cm arayla yoklamalar, her biri her "
               "yönden gelen ışığı 192 ışınla toplar. Güneş her karede canlıdır: her örnek güneş diskine kendi gölge ışınını "
               "yollar, ışın bir yapraktan açık kalan payı kadar geçer. Pustaki huzmeler için güneşin havadaki görünürlüğü "
               "10 cm'lik bir hacimde pişirilir.",
               "The skylight and one bounce of light are baked while loading: probes every 30 cm through the forest, each "
               "gathering the light from every direction with 192 rays. The sun is live every frame: each sample sends its own "
               "shadow ray to the sun's disk, and a leaf lets it through by its open share. For the shafts in the mist, the "
               "sun's visibility in the air is baked in a 10 cm volume.")},
            {L("Islak orman ve dere", "The wet forest and the stream"),
             L("Yağmurdan sonra pürüzlü yüzeyler ıslak parlar. Derenin suyu yansıma ve kırılma ışınlarıyla çizilir, "
               "derinleştikçe çay rengine döner ve akışla kayan dalgacıklar taşır. Pus yerde yoğun, yukarı doğru incelir.",
               "After the rain rough surfaces have a wet sheen. The stream's water is drawn with reflection and refraction "
               "rays, turns tea-coloured with depth and carries ripples that drift with the flow. The mist is thick near the "
               "ground and thins upwards.")},
            {L("Teşekkür", "Credits"), L("NVIDIA DLSS (NVIDIA RTX SDK). Varlıklar: Poly Haven ve ambientCG (CC0), docs/orman/VARLIK.md.",
                                         "NVIDIA DLSS (NVIDIA RTX SDK). Assets: Poly Haven and ambientCG (CC0), docs/orman/VARLIK.md.")}};
        i.tags = {L("Yoğunluk", "Density"), L("Kopyalama", "Instancing"), L("DLSS-RR", "DLSS-RR")};
        i.image = "assets/gallery/orman.jpg";
        i.accent = 0x8DBF6A;
        i.keys = {{L("Hareket", "Movement"),
                   {{L("Fare", "Mouse"), L("Etrafa bak", "Look around")},
                    {"W|A|S|D", L("Yürü", "Walk")},
                    {"Shift", L("Basılı tutup koş", "Hold to run")},
                    {L("Boşluk", "Space"), L("Zıpla", "Jump")},
                    {"C", L("Basılı tutup eğil", "Hold to crouch")},
                    {"Tab", L("İmleci panel için bırak / geri al", "Free the cursor for the panel / back")},
                    {"R", L("Başa dön", "Back to the start")}}},
                  {L("Bakış", "View"),
                   {{"1|2|3|4", L("Hazır bakışlar", "Preset views")},
                    {"↑|↓", L("Pozlamayı artır / azalt", "Exposure up / down")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<OrmanLive>(); }
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
            st.label = L("Orman kuruluyor", "Planting the forest");
            st.fraction = 0.45f;
            ++st.step;
            return;
        case 2:
            a_.buildWorld();
            st.label = L("Işın izleme yapıları kuruluyor", "Building the ray tracing structures");
            st.fraction = 0.55f;
            ++st.step;
            return;
        case 3: {
            std::string err;
            if (!a_.initRenderer(err)) {
                st.error = err;
                return;
            }
            st.label = L("Işık pişiriliyor", "Baking the light");
            st.fraction = 0.7f;
            ++st.step;
            return;
        }
        default:
            a_.bakeLight();
            if (a_.cfg.i("dlss", 1) != 0) setMode(kDlss);
            else setSamples(a_.cfg.i("samples", 4));
            reset();
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(a_.width) * a_.height * sizeof(uchar4));
            a_.render(0.0f);
            a_.render(0.0f);
            std::printf("%s\n", a_.summary().c_str());
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void reset() { goView(0); }
    void goView(int k) {
        a_.cam = OrmanApp::view(k);
        a_.world.collide(a_.cam.feet.x, a_.cam.feet.z, -1e9f, 0.3f);
        a_.cam.feet.y = a_.world.height(a_.cam.feet.x, a_.cam.feet.z);
        vy_ = 0.0f;
        crouch_ = 0.0f;
    }
    void setSamples(int n) {
        int m = 0;
        for (int k = 0; k < 4; ++k)
            if (kSamples[k] <= n) m = k;
        setMode(m);
    }
    // 0..3 the fixed samples, kDlss DLSS (back to 4 samples if it cannot be set up)
    void setMode(int m) {
        if (m == kDlss) {
            if (a_.setDlss(true)) {
                samples_ = kDlss;
                return;
            }
            std::fprintf(stderr, "orman: DLSS-RR kapali: %s\n", a_.R.dlssError.c_str());
            m = 1;
        }
        a_.setDlss(false);
        samples_ = m;
        a_.R.setPattern(kSamples[m], a_.cfg.f("filter.sigma", 0.42f));
    }

    void frame(app::Host& host, const app::Input& in) override {
        const float dt = float(std::min(0.1, in.dt));
        if (!started_) {
            host.pace(0.0f);
            host.captureMouse(look_);
            started_ = true;
        }
        for (const auto& e : in.events)
            if (e.id == "samples" && e.item >= 0) setMode(e.item);
        if (in.pressed(GLFW_KEY_TAB)) {
            look_ = !look_;
            host.captureMouse(look_);
        }
        if (in.pressed(GLFW_KEY_R)) reset();
        for (int k = 0; k < OrmanApp::viewCount(); ++k)
            if (in.pressed(GLFW_KEY_1 + k)) goView(k);
        if (in.pressed(GLFW_KEY_UP)) a_.exposureEv += 0.25f;
        if (in.pressed(GLFW_KEY_DOWN)) a_.exposureEv -= 0.25f;

        // looking: the mouse to the right turns the view right (the picture's right is forward x up, as the renderer's)
        const float sens = 0.0022f;
        if (look_) {
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
        const float speed = in.shift ? 3.2f : 1.3f;
        want = want * (speed * (1.0f - 0.5f * crouch_));
        vel_ += (want - vel_) * (1.0f - std::exp(-dt * 8.0f));
        float3 p = a_.cam.feet + vel_ * dt;
        // the forest goes round (a torus, no end); the patch alone stops at its edge
        const float lim = a_.world.limit();
        if (lim > 0.0f) {
            p.x = std::clamp(p.x, -lim, lim);
            p.z = std::clamp(p.z, -lim, lim);
        }
        a_.world.collide(p.x, p.z, a_.cam.feet.y, 0.3f);
        const float ground = a_.world.height(p.x, p.z);
        // a body: gravity, a jump from the ground, the feet never under it
        vy_ -= 9.81f * dt;
        p.y += vy_ * dt;
        const bool onGround = p.y <= ground + 1e-3f;
        if (onGround) {
            p.y = ground;
            vy_ = 0.0f;
            if (in.pressed(GLFW_KEY_SPACE)) vy_ = 3.2f;
        }
        a_.cam.feet = p;
        crouch_ += ((in.isDown(GLFW_KEY_C) ? 1.0f : 0.0f) - crouch_) * (1.0f - std::exp(-dt * 10.0f));
        // a slight sway of the head while walking (the steps), none standing
        const float moving = std::min(1.0f, length(vel_) / 1.3f);
        stepPhase_ += dt * length(vel_) * 1.9f;
        a_.cam.eyeHeight = 1.7f - 0.7f * crouch_ + 0.012f * moving * std::sin(stepPhase_ * 2.0f);

        a_.render(dt);
        if (!tex_) {
            glGenTextures(1, &tex_);
            glBindTexture(GL_TEXTURE_2D, tex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);   // GL_CLAMP_TO_EDGE
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
        s.chips.push_back({theme::thousands(a_.R.instanceCount) + L(" kopya", " copies"), true});
        s.chips.push_back({num(double(a_.R.instancedTriangles) / 1e6, 0) + L(" milyon üçgen", " million triangles")});
        s.metrics.push_back({L("Kare", "Frame"), num(a_.R.renderMs + a_.R.displayMs, 1) + " ms"});
        if (samples_ == kDlss) s.metrics.push_back({"DLSS", num(a_.R.dlssMs, 1) + " ms"});
        app::Control c;
        c.kind = app::Control::Kind::Choice;
        c.id = "samples";
        c.title = L("Örnek", "Samples");
        c.items = {"1", "4", "8", "16", "DLSS"};
        c.active = samples_;
        c.detail = samples_ == kDlss ? L("Karede piksel başına tek ışın, DLSS Ray Reconstruction birleştirir", "One ray per pixel a frame, combined by DLSS Ray Reconstruction")
                                     : L("Piksel başına ışın, her karede aynı desende", "Rays per pixel, the same pattern every frame");
        s.controls.push_back(c);
    }

    void screenshot(app::Host& host) override {
        a_.save(host.capturePath());
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        // settings and the device programs again (the world and its light stay: they take the load's minutes)
        a_.loadConfig();
        std::string err;
        if (!a_.R.compile(err)) {
            host.toast(L("Derleme hatası: ", "Compile error: ") + err.substr(0, 200));
            std::fprintf(stderr, "%s\n", err.c_str());
            return;
        }
        a_.applyLight();
        if (samples_ != kDlss) a_.R.setPattern(kSamples[samples_], a_.cfg.f("filter.sigma", 0.42f));
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
    ~OrmanLive() override { unload(); }

private:
    app::SceneInfo info_ = describe();
    OrmanApp a_;
    uchar4* pinned_ = nullptr;
    GLuint tex_ = 0;
    cudaGraphicsResource_t res_ = nullptr;
    bool texReady_ = false;
    bool started_ = false, look_ = true;
    float3 vel_{0, 0, 0};
    float vy_ = 0.0f, crouch_ = 0.0f, stepPhase_ = 0.0f;
    int samples_ = 1;
};

}   // namespace

namespace orman {
app::SceneInfo info() { return OrmanLive::describe(); }
std::unique_ptr<app::Scene> make() { return OrmanLive::make(); }
int cli(int argc, char** argv) { return runOrmanCli(argc, argv); }
}   // namespace orman
