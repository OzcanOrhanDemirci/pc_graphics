// Scene 10 "Kuyu" inside the application: falling slowly down a shaft of an endless lattice of mirrored rooms. The
// mouse looks (captured from the start, as the lighthouse's walk; Tab gives the cursor back), W A S D drift level,
// Space and C rise and sink, Q / E slow and quicken the fall, Shift held goes faster and faster. No quality settings: the picture is set
// for 2560 x 1440.
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/kuyu/kuyu_app.h"

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

const float kFall[5] = {0.0f, 0.5f, 1.5f, 4.0f, 10.0f};   // m/s

class KuyuLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "kuyu";
        i.number = "10";
        i.title = L("Kuyu", "The Well");
        i.subtitle = L("Aynalı odalardan bir kafeste sonsuz düşüş", "An endless fall through mirrored rooms");
        i.description = L("Sonsuz bir kafeste aynalı odaların arasından yavaşça aşağı düşersin. Her odanın içinde ışık duvardan "
                          "duvara seker ve sonsuza dek tekrarlanır. Etrafa bakar, bir odanın camına yaklaşır, düşüşü durdurur ya da "
                          "Shift ile kafesin içinden hızla geçersin.",
                          "You fall slowly down an endless lattice past mirrored rooms. Inside each room the light bounces from "
                          "wall to wall and repeats without end. You look around, drift up to a room's glass, stop the fall, or "
                          "hold Shift to race through the lattice.");
        i.details = {
            {L("Sonsuz oda kafesi", "An endless lattice of rooms"),
             L("Uzay 8 m'lik küplere bölünür. Her küpün ortasında 3,6 m'lik cam bir oda durur, odanın on iki kenarından sonsuza "
               "0,6 m'lik demetler uzanır. Işın kafeste hücreden hücreye yürür ve her hücrede yalnız oradaki odayı ve demetleri "
               "sınar; yüzeyler analitik olarak kesin çözülür. Her oda 20 düzenden birini, küpün 48 simetrisinden biriyle "
               "döndürülmüş olarak taşır. Kafes hiç bitmez, uzaktaki odalar pusta söner.",
               "Space is cut into 8 m cubes. Each holds a 3.6 m glass room at its centre, and 0.6 m bundles run to infinity "
               "from the room's twelve edges. A ray walks the lattice cell by cell and tests only that cell's room and bundles, "
               "all solved exactly. Each room holds one of 20 layouts, turned by one of the cube's 48 symmetries. The lattice "
               "never ends; distant rooms fade into the haze.")},
            {L("Tek yönlü cam duvar", "One-way glass walls"),
             L("Her oda, iç yüzünde 45 nm gümüş film olan 12,7 mm'lik gri camdan bir küptür. 550 nm'de duvar içeriden gelen "
               "ışığın %94,4'ünü geri döndürür, dışarıdan %4,1'ini yansıtır ve iki yönde de yalnız %0,71'ini geçirir. Tek "
               "yönlülük, aydınlık içeriyle karanlık dışarının farkından doğar. Değerler ince film formülüyle, gümüşün ölçülmüş "
               "optik sabitlerinden (Johnson ve Christy, 1972) ve camın veri sayfasından hesaplanır.",
               "Each room is a cube of 12.7 mm grey glass with a 45 nm silver film on its inner face. At 550 nm the wall "
               "returns 94.4% of the light from inside, reflects 4.1% from outside and lets only 0.71% through either way. The "
               "one-way look comes from a bright inside against a dark outside. The values follow from thin-film optics, "
               "silver's measured constants (Johnson and Christy, 1972) and the glass's data sheet.")},
            {L("Odanın içinde sonsuz yansıma", "Endless reflections in a room"),
             L("Camdan giren ışın odada gümüş duvarlar, aynalar, krom bir küre ya da bakır bir sarkaç arasında 256 kereye kadar "
               "seker. Kalan yolun ekrana katabileceği ışık yarım renk kademesinin altına inince yol biter; bu sınır pozlamayla "
               "birlikte değişir. İnce gümüş kırmızıyı maviden çok yansıttığı için derin yansımalar kehribara kayar.",
               "A ray that enters through the glass bounces up to 256 times between the silver walls, mirrors, a chrome sphere "
               "or a copper pendulum. The path ends once the light it could still add to the screen falls below half a colour "
               "step, so the limit moves with the exposure. Thin silver reflects red more than blue, so the deepest reflections "
               "turn amber.")},
            {L("Demetleri aydınlatan ışık", "Light on the bundles"),
             L("Demetleri yalnız odaların camından sızan ışık aydınlatır. Kafes yinelendiği için bu ışık bir hücrenin demetleri "
               "üzerinde bir kez hesaplanır: 2 × 2,5 cm'lik her doku noktasından Fibonacci sarmalına dizili 4096 ışın atılır ve "
               "çarptıkları oda yüzü tabloya yazılır. Odaların parlaklığı kare çizilirken bu tabloyla çarpılır. Demetlerin yüzü "
               "kitap sırtı gibi ipliklerdir; cilalılar camın, altın varaklar altının yansımasıyla parlar.",
               "The bundles are lit only by the light leaking from the rooms' glass. As the lattice repeats, this light is "
               "worked out once over one cell's bundles: 4096 rays on a Fibonacci spiral leave every 2 × 2.5 cm texel, and the "
               "room face each one meets is written to a table. Each frame multiplies the rooms' brightness by that table. The "
               "bundles are faced with threads like book spines; varnished ones shine like glass, gold leaf like gold.")},
            {L("Sabit örnekli görüntü", "Fixed-sample image"),
             L("Her piksel her karede aynı altı sabit örnekle çizilir; önceki karelerden bir şey taşınmaz, gürültü giderici "
               "yoktur. Küçük bir ampulün derin bir yansıması pikselden küçülünce ışının konisine düşen payıyla sayılır, bu "
               "yüzden hareket ederken yanıp sönmez. Aynı oda yüzüne giren örnekler odanın içini tek ışınla paylaşır; küre ya "
               "da ayna olan odalarda üç ışın kullanılır.",
               "Every pixel is drawn from the same six fixed samples every frame. Nothing is carried over from earlier frames "
               "and there is no denoiser. When the deep image of a small bulb shrinks below a pixel, it is counted by its share "
               "of the ray's cone, so it does not twinkle in motion. Samples entering the same room face share one ray for the "
               "interior; rooms with a sphere or a mirror get three.")}};
        i.tags = {L("Sonsuzluk", "Infinity"), L("Yansıma", "Reflection"), L("İnce film optiği", "Thin-film optics")};
        i.image = "assets/gallery/kuyu.jpg";
        i.accent = 0xD9A25B;
        i.keys = {{L("Hareket", "Moving"),
                   {{L("Fare", "Mouse"), L("Etrafa bak", "Look around")},
                    {"W|A|S|D", L("Yatay kay", "Drift level")},
                    {L("Boşluk", "Space"), L("Yüksel", "Rise")},
                    {"C", L("Alçal", "Sink")},
                    {"Q|E", L("Düşüşü yavaşlat / hızlandır", "Slow / quicken the fall")},
                    {"Shift", L("Basılı tutup gittikçe hızlan", "Hold to go faster and faster")},
                    {"Tab", L("İmleci bırak / geri al", "Release / take back the cursor")}}},
                  {L("Bakış", "View"),
                   {{"1|2|3|4", L("Kuyu, oda, kafes, yukarı", "Shaft, room, lattice, up")},
                    {"R", L("Başa dön", "Back to the start")},
                    {"↑|↓", L("Pozlama", "Exposure")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<KuyuLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Kafes kuruluyor", "Building the lattice");
            st.fraction = 0.2f;
            ++st.step;
            return;
        default:
            a_.root = host.root();
            a_.overrides = host.overrides();
            a_.loadConfig();
            a_.init();
            a_.cam = KuyuApp::view(0);
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
            a_.cam = KuyuApp::view(0);
            vel_ = make_float3(0, 0, 0);
            fall_ = 2;
            boostT_ = 0.0f;
        }
        for (int k = 0; k < KuyuApp::viewCount(); ++k)
            if (in.pressed(GLFW_KEY_1 + k)) {
                const KuyuCam v = KuyuApp::view(k);
                // the view's place within its cell, at the level we have fallen to
                const double C = a_.lat.C;
                double base = std::floor(a_.cam.y / C + 0.5) * C;
                if (k == 1) {
                    // a room to look into: the nearest one below with a light of its own (not a dark one)
                    const long long J = (long long)std::floor(a_.cam.y / C + 0.5);
                    for (long long j = J; j > J - 40; --j)
                        if (a_.variants[a_.variantOf[kuyu::cellKey(0, j, 0) & 255u]].nL > 0) {
                            base = double(j) * C;
                            break;
                        }
                }
                a_.cam = v;
                a_.cam.y = base + v.y;
                vel_ = make_float3(0, 0, 0);
                if (k == 1) {   // before a room the fall stops: E takes it on
                    fall_ = 0;
                    host.toast(L("Bir odanın önünde durdun. E ile düşüşe devam edersin.", "Stopped before a room. Press E to fall on."));
                }
            }
        if (in.pressed(GLFW_KEY_Q)) fall_ = std::max(0, fall_ - 1);
        if (in.pressed(GLFW_KEY_E)) fall_ = std::min(4, fall_ + 1);
        if (in.pressed(GLFW_KEY_UP)) ev_ += 0.25f;
        if (in.pressed(GLFW_KEY_DOWN)) ev_ -= 0.25f;

        // looking: the mouse to the right turns the view right (yaw grows to the left; the picture's right is the
        // renderer's own camRight, forward x up)
        const float sens = 0.0022f;
        if (look_) {
            a_.cam.yaw -= in.mouseDX * sens;
            a_.cam.pitch = std::clamp(a_.cam.pitch - in.mouseDY * sens, -1.5708f, 1.5708f);
        }
        auto key = [&](int kc) { return in.isDown(kc) ? 1.0f : 0.0f; };
        const float fwd = key(GLFW_KEY_W) - key(GLFW_KEY_S), right = key(GLFW_KEY_D) - key(GLFW_KEY_A);
        const float up = key(GLFW_KEY_SPACE) - key(GLFW_KEY_C);
        const float3 f = make_float3(-std::sin(a_.cam.yaw), 0.0f, -std::cos(a_.cam.yaw));
        const float3 r = make_float3(std::cos(a_.cam.yaw), 0.0f, -std::sin(a_.cam.yaw));
        float3 want = f * fwd + r * right + make_float3(0.0f, up, 0.0f);
        const float wl = length(want);
        if (wl > 1.0f) want = want * (1.0f / wl);
        want = want * 1.2f + make_float3(0.0f, -kFall[fall_], 0.0f);
        // Shift held: faster and faster, exponentially (e-fold every boost.tau seconds) up to boost.max m/s; let go: back
        // within a few seconds. Nothing else changes with the speed. The ceiling: above it the lattice (one cell in 8 m)
        // moves more than half a cell between frames and the eye can no longer follow it (it seems to stand or go back)
        const float tau = a_.cfg.f("boost.tau", 1.3f), vmax = a_.cfg.f("boost.max", 12000.0f);
        const float base = std::max(length(want), 0.5f);
        if (in.shift) boostT_ = std::min(boostT_ + dt, tau * std::log(std::max(vmax / base, 1.0f)));
        else boostT_ = std::max(0.0f, boostT_ - 6.0f * dt);   // let go: back from the ceiling in about 3 s
        want = want * std::exp(boostT_ / tau);
        if (length(want) > vmax) want = want * (vmax / length(want));
        vel_ += (want - vel_) * (1.0f - std::exp(-dt * 3.0f));
        // the frame's way swept through the lattice cell by cell (any speed: 1.2 million m/s crosses 4500 cells a frame):
        // up to the first obstacle, then on along it (the velocity into it taken away: a slide); a last push resolves
        // what the grown boxes' corners leave
        double m[3] = {double(vel_.x) * dt, double(vel_.y) * dt, double(vel_.z) * dt};
        // (past 100 km a frame the sweep is skipped: at a trillion m/s it would walk billions of cells; whether the way
        // passed through a bundle no longer means anything, the last push still keeps the eye out of the glass)
        const bool sweepIt = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]) < 1e5;
        if (!sweepIt) {
            a_.cam.x += m[0];
            a_.cam.y += m[1];
            a_.cam.z += m[2];
        }
        for (int it = 0; it < 4 && sweepIt; ++it) {
            double n[3];
            const double t = a_.sweep(a_.cam.x, a_.cam.y, a_.cam.z, m[0], m[1], m[2], 0.3f, n);
            const double go = std::max(0.0, t - 1e-6);
            a_.cam.x += m[0] * go;
            a_.cam.y += m[1] * go;
            a_.cam.z += m[2] * go;
            if (t >= 1.0) break;
            for (int a = 0; a < 3; ++a) m[a] *= (1.0 - go);
            const double mn = m[0] * n[0] + m[1] * n[1] + m[2] * n[2];
            if (mn < 0.0)
                for (int a = 0; a < 3; ++a) m[a] -= n[a] * mn;
            const float vn = vel_.x * float(n[0]) + vel_.y * float(n[1]) + vel_.z * float(n[2]);
            if (vn < 0.0f) vel_ -= make_float3(float(n[0]), float(n[1]), float(n[2])) * vn;
        }
        {
            double nx = a_.cam.x, ny = a_.cam.y, nz = a_.cam.z;
            // (unswept, the eye lands anywhere in the lattice, now and then inside a room or a bundle: the push only moves it
            // out; taking the velocity into it away would stop the fall every few frames)
            if (a_.collide(nx, ny, nz, 0.3f) && sweepIt) {
                const float3 push = make_float3(float(nx - a_.cam.x), float(ny - a_.cam.y), float(nz - a_.cam.z));
                const float pl = length(push);
                if (pl > 1e-6f) {
                    const float3 n = push * (1.0f / pl);
                    const float vn = dot(vel_, n);
                    if (vn < 0.0f) vel_ -= n * vn;
                }
            }
            a_.cam.x = nx;
            a_.cam.y = ny;
            a_.cam.z = nz;
        }
        a_.time += dt;

        a_.evScale = std::exp2(ev_);
        a_.render();
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
        s.chips.push_back({fall_ == 0 ? L("Düşüş durdu", "Fall stopped") : L("Düşüş ", "Falling ") + num(kFall[fall_], 1) + " m/s", true});
        const float sp = length(vel_);
        s.metrics.push_back({L("Hız", "Speed"), (sp < 10.0f ? num(sp, 1) : theme::thousands((unsigned long long)std::llround(sp))) + " m/s"});
        s.metrics.push_back({L("Kare", "Frame"), num(a_.frameMs, 1) + " ms"});
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
    ~KuyuLive() override { unload(); }

private:
    app::SceneInfo info_ = describe();
    KuyuApp a_;
    uchar4* pinned_ = nullptr;
    GLuint tex_ = 0;
    cudaGraphicsResource_t res_ = nullptr;
    bool texReady_ = false;
    bool started_ = false, look_ = true;
    float3 vel_{0, 0, 0};
    float ev_ = 0.0f;
    int fall_ = 2;
    float boostT_ = 0.0f;   // how long Shift has been held (the boost's clock)
};

}   // namespace

namespace kuyu {
app::SceneInfo info() { return KuyuLive::describe(); }
std::unique_ptr<app::Scene> make() { return KuyuLive::make(); }
int cli(int argc, char** argv) { return runKuyuCli(argc, argv); }
}   // namespace kuyu
