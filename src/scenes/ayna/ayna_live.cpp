// Scene 9 "Ayna" inside the application: a mirror labyrinth, seen from inside a floating chrome sphere. The mouse
// looks (captured from the start, as the lighthouse's walk; Tab gives the cursor back), W A S D move, Space and C rise
// and sink, Shift is fast. Our light shines from the slit in the sphere's front: Q / E its colour, the wheel its
// strength, F on and off; G or the left button leaves a lamp behind in that colour, X gathers them, R goes back.
// No quality settings (Özcan 2026-10-07): the picture is set for 2560 x 1440.
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/ayna/ayna_app.h"

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

class AynaLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "ayna";
        i.number = "09";
        i.title = L("Ayna", "The Mirror");
        i.subtitle = L("Ayna labirentinde yansımanın yansıması", "Reflections of reflections in a mirror maze");
        i.description = L("Endülüs kemerli bir ayna labirentinde süzülen krom bir küresin. Önündeki yarıktan kendi ışığını yakar, rengini ve "
                          "gücünü seçer, ardında renkli lambalar bırakırsın. Karşılıklı aynalar her şeyi derin bir tünelde yeniden ve "
                          "yeniden çoğaltır.",
                          "You are a chrome sphere floating through a mirror labyrinth of Moorish arches. You shine your own light from a "
                          "slit in your front, choose its colour and strength, and leave coloured lamps behind. Facing mirrors repeat "
                          "everything down a deep tunnel.");
        i.details = {
            {L("Ölçülmüş değerlerle ayna", "Mirrors from measured optics"),
             L("Her ayna, 6 mm float camın arkasındaki gümüştür ve ölçülmüş optik sabitlerle hesaplanır: camın ön yüzündeki "
               "yansıma, gümüş ve camın iki geçişteki soğurması (Rubin 1985, Johnson ve Christy 1972). Dik gelişte kırmızının "
               "%79'u, yeşil ve mavinin %84'ü geri döner. Bu yüzden karşılıklı aynalardaki tünel derinleştikçe kararır ve "
               "camgöbeği-yeşile döner; bunun için ayrı bir ayar yok.",
               "Every mirror is silver behind 6 mm of float glass, computed from measured optical constants: the reflection off "
               "the glass's front face, the silver, and the glass's absorption on the way in and out (Rubin 1985, Johnson and "
               "Christy 1972). Straight on, 79% of red and 84% of green and blue come back, so the tunnel between facing mirrors "
               "darkens and turns cyan-green as it deepens, with no setting for it.")},
            {L("Hücreden hücreye kesin izleme", "Exact tracing, cell by cell"),
             L("OptiX değil, CUDA ile yazılmış kendi izleyicimiz. Labirent 60 derecelik üçgen bir ızgarada 54 hücredir; ışın "
               "hücreden hücreye yürür ve her hücrede yalnız oradaki zemin, tavan, sütun, kemer ve aynalarla kesişir. Yüzeyler "
               "düzlem, çember ve silindir olarak kesin hesaplanır: iki paralel ayna arasındaki tünelin 40. kopyası, olması "
               "gereken yerden on binde bir pikselden az sapar.",
               "Not OptiX but our own tracer, written in CUDA. The labyrinth is 54 cells on a 60-degree triangular grid; a ray "
               "walks from cell to cell and meets in each only that cell's floor, ceiling, columns, arches and mirrors. Surfaces "
               "are solved exactly as planes, circles and cylinders: between two parallel mirrors the tunnel's 40th copy lies "
               "within a ten-thousandth of a pixel of where it should be.")},
            {L("Aynadan yansıyan ışık", "Light off the mirrors"),
             L("Yol izleme, nokta bir ışığın aynadan duvara giden yolunu bulamaz. Aynalar dik ve ızgara çizgileri üstünde durduğu "
               "için bu yol planda kesin bulunur: her ışığın her aynadaki görüntüsünden, pencerelerden ve sütunların arasından "
               "geçebilen bir kama çıkar; bunların hepsi bir hüzme ağacı oluşturur. Örnekleme olmadığı için gren de yok. Aynı "
               "izleyiciyle atılan bir milyar fotonla %99 uyuşur.",
               "Path tracing cannot find a point light's way off a mirror onto a wall. Because the mirrors are upright and stand "
               "on the grid lines, that way is found exactly in plan: from each light's image in each mirror comes a wedge that "
               "passes the windows and the gaps between columns, and together they form a tree of beams. No sampling, so no "
               "grain. It agrees to 99% with a billion photons shot through the same tracer.")},
            {L("Işık haritaları", "Light maps"),
             L("Bırakılan lambalar ve dört pirinç fener yerinde durur. Işıkları, doğrudan ve bütün aynalardan gelen haliyle, "
               "bırakıldıkları an yüzeylerin ışık haritasına pişirilir; küremizin onlardan düşen gölgesi her karede piksel başına "
               "hesaplanır. Bizim ışığımız bizimle yürüdüğü için her kare yarım çözünürlüklü bir haritaya yeniden pişer (1,8 ms); "
               "kendi hücresindeki doğrudan ışığı piksel başına keskin hesaplanır.",
               "The lamps you leave and the four brass lanterns stay put. Their light, direct and from every mirror, is baked "
               "into the surfaces' light maps the moment they are placed; our sphere's shadow from them is worked out per pixel "
               "every frame. Our own light moves with us, so it is baked again every frame into a half-resolution map (1.8 ms); "
               "its direct light in its own cell is computed per pixel, sharp.")},
            {L("Işının izdüşümüyle süzme", "Filtering by the ray's footprint"),
             L("Her ışın, piksele düşen kendi izdüşüm konisini taşır. Desenler bu koniyle süzülür; bu yüzden aynanın ardındaki bir "
               "desen, aynı uzaklıktan doğrudan bakıldığı kadar keskin ve sakindir. Küçük lambalar da koninin örttüğü paylarıyla "
               "toplanır: derin tünelde piksel boyunun altına inen lambalar titremez, sabit bir nokta olarak kalır.",
               "Every ray carries its own footprint, the cone a pixel covers. Patterns are filtered by that cone, so a pattern "
               "behind a mirror is as sharp and as steady as one seen directly from the same distance. Small lamps count by the "
               "share of the cone they cover: deep in the tunnel, lamps smaller than a pixel do not twinkle but stay a steady "
               "point.")},
            {L("Örnekleme, pus ve hız", "Sampling, haze and speed"),
             L("Pikselde her kare aynı 6 sabit örnek alınır; önceki karelerden birikim ve gürültü giderici yok, örnekler komşu "
               "piksellere hafifçe taşarak birleşir. Hafif, ileri saçan bir pus her yol parçası boyunca örneklemeden, formülle "
               "hesaplanır ve ışığımızın konisini görünür kılar. 2560 × 1440'ta saniyede yaklaşık 30-44 kare.",
               "Six fixed samples per pixel, the same every frame; nothing carried over from earlier frames, no denoiser. Each "
               "pixel's samples spill slightly into its neighbours. A light, forward-scattering haze is "
               "computed in closed form along every piece of a path, without sampling, and shows the cone of our light. About "
               "30 to 44 frames a second at 2560 × 1440.")}};
        i.tags = {L("Analitik ışın izleme", "Analytic ray tracing"), L("Ölçülmüş optik", "Measured optics"), L("Işık haritaları", "Light maps")};
        i.image = "assets/gallery/ayna.jpg";
        i.accent = 0x7FC8C2;
        i.keys = {{L("Hareket", "Movement"),
                   {{L("Fare", "Mouse"), L("Etrafa bak", "Look around")},
                    {"W|A|S|D", L("İleri, geri, yanlara", "Forward, back, sideways")},
                    {L("Boşluk", "Space"), L("Yüksel", "Rise")},
                    {"C", L("Alçal", "Sink")},
                    {"Shift", L("Basılı tutunca hızlı", "Hold to go fast")},
                    {"Tab", L("İmleci bırak / geri al", "Free / capture the cursor")},
                    {"1|2|3|4", L("Bakışlar: koridor, ayna, salon, zemin", "Views: corridor, mirror, hall, floor")},
                    {"R", L("Başa dön", "Back to the start")}}},
                  {L("Işık", "Light"),
                   {{"Q|E", L("Işığın rengi (8 renk)", "Light colour (8 colours)")},
                    {L("Tekerlek", "Wheel"), L("Işığın gücü (8 kademe)", "Light strength (8 steps)")},
                    {"F", L("Işığı aç / kapat", "Light on / off")},
                    {L("Sol tık|/|G", "Left click|/|G"), L("Lamba bırak (en çok 8)", "Drop a lamp (at most 8)")},
                    {"X", L("Lambaları topla", "Gather the lamps")},
                    {"↑|↓", L("Pozlama", "Exposure")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<AynaLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Labirent kuruluyor, fenerlerin ışığı pişiyor", "Building the labyrinth and baking the lanterns' light");
            st.fraction = 0.2f;
            ++st.step;
            return;
        default:
            a_.root = host.root();
            a_.overrides = host.overrides();
            a_.loadConfig();
            a_.init();
            a_.cam = AynaApp::view(0);
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(a_.width) * a_.height * sizeof(uchar4));
            a_.render();
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
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
            a_.cam = AynaApp::view(0);
            vel_ = make_float3(0, 0, 0);
        }
        for (int k = 0; k < AynaApp::viewCount(); ++k)
            if (in.pressed(GLFW_KEY_1 + k)) {
                a_.cam = AynaApp::view(k);
                vel_ = make_float3(0, 0, 0);
            }
        if (in.pressed(GLFW_KEY_Q)) a_.colorIndex = (a_.colorIndex + AynaApp::paletteCount() - 1) % AynaApp::paletteCount();
        if (in.pressed(GLFW_KEY_E)) a_.colorIndex = (a_.colorIndex + 1) % AynaApp::paletteCount();
        if (in.pressed(GLFW_KEY_F)) a_.lightOn = !a_.lightOn;
        if (in.scroll != 0.0f) a_.levelIndex = std::clamp(a_.levelIndex + (in.scroll > 0.0f ? 1 : -1), 0, AynaApp::levelCount() - 1);
        if (in.pressed(GLFW_KEY_UP)) ev_ += 0.25f;
        if (in.pressed(GLFW_KEY_DOWN)) ev_ -= 0.25f;
        if (in.pressed(GLFW_KEY_G) || (in.leftClicked && in.view.contains(in.mouseX, in.mouseY))) dropLamp(host);
        if (in.pressed(GLFW_KEY_X) && !a_.lamps.empty()) {
            a_.lamps.clear();
            host.toast(L("Lambalar toplandı", "Lamps gathered"));
        }

        // looking: the mouse to the right turns the view right (yaw grows to the left; the picture's right is
        // forward x up, the renderer's own camRight)
        const float sens = 0.0022f;
        if (look_) {
            a_.cam.yaw -= in.mouseDX * sens;
            a_.cam.pitch = std::clamp(a_.cam.pitch - in.mouseDY * sens, -1.45f, 1.45f);
        }
        // moving: level along the view's heading, straight up and down; eased, and pushed out of the walls
        auto key = [&](int kc) { return in.isDown(kc) ? 1.0f : 0.0f; };
        const float fwd = key(GLFW_KEY_W) - key(GLFW_KEY_S), right = key(GLFW_KEY_D) - key(GLFW_KEY_A);
        const float up = key(GLFW_KEY_SPACE) - key(GLFW_KEY_C);
        const float3 f = normalize(make_float3(-std::sin(a_.cam.yaw), 0.0f, -std::cos(a_.cam.yaw)));
        const float3 r = normalize(cross(f, make_float3(0.0f, 1.0f, 0.0f)));
        const float speed = in.shift ? 2.6f : 1.0f;
        float3 want = f * fwd + r * right + make_float3(0.0f, up, 0.0f);
        const float wl = length(want);
        if (wl > 1.0f) want = want * (1.0f / wl);
        vel_ += (want * speed - vel_) * (1.0f - std::exp(-dt * 8.0f));
        const int sub = std::max(1, int(std::ceil(length(vel_) * dt / 0.02f)));
        const float R = a_.cfg.f("sphere.radius", 0.175f);
        for (int s = 0; s < sub; ++s) {
            float3 p = a_.cam.pos + vel_ * (dt / float(sub));
            if (a_.maze.collide(p, R)) {
                // the push takes the velocity's part into the wall away (a slide along it)
                const float3 push = p - (a_.cam.pos + vel_ * (dt / float(sub)));
                const float pl = length(push);
                if (pl > 1e-6f) {
                    const float3 n = push * (1.0f / pl);
                    const float vn = dot(vel_, n);
                    if (vn < 0.0f) vel_ -= n * vn;
                }
            }
            if (a_.maze.insidePoint(p.x, p.z)) a_.cam.pos = p;
        }

        a_.evScale = std::exp2(ev_);
        a_.render();
        // the picture straight into an OpenGL texture, on the card (no trip through main memory)
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
            cudaMemcpy2DToArray(arr, 0, 0, a_.output, size_t(a_.width) * sizeof(uchar4), size_t(a_.width) * sizeof(uchar4), a_.height,
                                cudaMemcpyDeviceToDevice);
            cudaGraphicsUnmapResources(1, &res_, 0);
            texReady_ = true;
        } else {
            cudaMemcpy(pinned_, a_.output, size_t(a_.width) * a_.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
            texReady_ = false;
        }
    }

    const void* image(int& w, int& h) override {
        w = a_.width;
        h = a_.height;
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
        const bool tr = app::lang() == app::Lang::TR;
        s.chips.push_back({std::string(AynaApp::paletteName(a_.colorIndex, tr)), true});
        s.chips.push_back({a_.lightOn ? L("Işık ", "Light ") + std::to_string(a_.levelIndex + 1) + "/" + std::to_string(AynaApp::levelCount())
                                      : L("Işık kapalı", "Light off")});
        if (!a_.lamps.empty()) s.chips.push_back({L("Lamba: ", "Lamps: ") + std::to_string(a_.lamps.size())});
        s.metrics.push_back({L("Kare süresi", "Frame time"), num(a_.frameMs, 1) + " ms"});
    }

    void screenshot(app::Host& host) override {
        a_.save(host.capturePath());
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        if (res_) cudaGraphicsUnregisterResource(res_);
        res_ = nullptr;
        if (tex_) glDeleteTextures(1, &tex_);
        tex_ = 0;
        texReady_ = false;
        a_.release();
        if (pinned_) cudaFreeHost(pinned_);
        a_.loadConfig();
        a_.init();
        cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(a_.width) * a_.height * sizeof(uchar4));
        host.toast(L("Ayarlar yeniden yüklendi", "Settings reloaded"));
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
    ~AynaLive() override { unload(); }

private:
    void dropLamp(app::Host& host) {
        if (int(a_.lamps.size()) >= a_.cfg.i("lamp.max", 8)) {
            host.toast(L("En çok 8 lamba bırakılır: X hepsini toplar", "Eight lamps at most: X gathers them all"));
            return;
        }
        // in front of the sphere, level, where it fits
        AynaLamp lp;
        const float3 f = normalize(make_float3(-std::sin(a_.cam.yaw), 0.0f, -std::cos(a_.cam.yaw)));
        float3 p = a_.cam.pos + f * a_.cfg.f("lamp.ahead", 0.32f);
        a_.maze.collide(p, a_.cfg.f("lamp.radius", 0.03f) + 0.01f);
        if (!a_.maze.insidePoint(p.x, p.z)) p = a_.cam.pos;
        lp.pos = p;
        lp.color = AynaApp::paletteColor(a_.colorIndex);
        lp.level = AynaApp::levelValue(a_.levelIndex);
        a_.lamps.push_back(lp);
    }

    app::SceneInfo info_ = describe();
    AynaApp a_;
    uchar4* pinned_ = nullptr;
    GLuint tex_ = 0;
    cudaGraphicsResource_t res_ = nullptr;
    bool texReady_ = false;
    bool started_ = false, look_ = true;
    float3 vel_{0, 0, 0};
    float ev_ = 0.0f;
};

}   // namespace

namespace ayna {
app::SceneInfo info() { return AynaLive::describe(); }
std::unique_ptr<app::Scene> make() { return AynaLive::make(); }
int cli(int argc, char** argv) { return runAynaCli(argc, argv); }
}   // namespace ayna
