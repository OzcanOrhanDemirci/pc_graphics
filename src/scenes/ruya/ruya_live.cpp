#include "app/scene.h"
#include "app/theme.h"
#include "scenes/ruya/ruya_app.h"

#include <windows.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <cuda_gl_interop.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

namespace ruya {
namespace {
using app::L;

app::SceneInfo describe() {
    app::SceneInfo info;
    info.id = "ruya";
    info.number = "14";
    info.title = L("Rüya", "Dream");
    info.subtitle = L("Sis denizinin üzerinde asılı bahçeler", "Hanging gardens above a sea of mist");
    info.description = L("Aşınmış kireçtaşı adaların arasında uç. Kat kat kemerler, ıslak taş, bronz işçilik ve boşluğa sarkan bahçeler. "
                         "Kamera için adalar baktığın yerde kurulur. Aynı yere dönünce aynı biçimleri bulursun.",
                         "Fly among eroded limestone islands. Layered arcades, wet stone, bronze ornament and gardens hanging into the void. "
                         "Islands are assembled for the camera where you look. Returning to a place restores its shapes.");
    info.details = {
        {L("Üretilen dünya", "A generated world"),
         L("Geometri, yerleşim ve malzemelerin tamamı matematikten üretilir. Dünya dosyası veya hazır model yok. "
           "Görüş konisine giren hücrelerin yerleşimi sabittir; çıkanların etkin verisi silinir. Ortak biçim tarifleri bellekte kalır.",
           "Geometry, placement and materials are entirely procedural. There is no saved world or imported model. "
           "Cells entering the view keep their placement; departing cells lose their active data. Shared shape recipes remain in memory.")},
        {L("Işık", "Light"),
         L("OptiX, gölge ve dolaylı ışık için çevredeki aynı üçgen kalıplarını izler. Bu geçici yerleşim tablosu izleme bitince silinir. "
           "Sis, yükseklik ve mesafeye göre hesaplanır.",
           "OptiX traces the same triangle shapes around the camera for shadows and indirect light. The temporary instance table is "
           "cleared after tracing. Mist varies with height and distance.")},
        {L("Görüntü", "Image"),
         L("Doğrudan 2560×1440 çizilir. Sabit örnek düzeni ve geçmiş kullanmayan gürültü giderme: durunca kalite değiştirmez. "
           "İnce malzeme ayrıntısı pikselin kapladığı alana göre süzülür.",
           "Rendered directly at 2560×1440. A fixed sampling pattern and history-free denoising keep the same quality in motion and at rest. "
           "Fine material detail is filtered to the pixel footprint.")}};
    info.tags = {L("Prosedürel", "Procedural"), L("Yol izleme", "Path tracing"), L("Serbest uçuş", "Free flight")};
    info.image = "assets/gallery/ruya.jpg";
    info.accent = 0xC4AB7D;
    info.keys = {{L("Uçuş", "Flight"),
        {{L("Fare", "Mouse"), L("Etrafa bak; dönüş hızı sınırlıdır", "Look around; rotation speed is limited")},
         {"W A S D", L("Serbest uç, katı yüzeyler kamerayı durdurur", "Fly freely, solid surfaces stop the camera")},
         {L("Boşluk | C", "Space | C"), L("Yüksel | alçal", "Ascend | descend")},
         {"Shift", L("Hızlı uç", "Fly faster")},
         {L("Tekerlek", "Wheel"), L("Görüş açısı: 36–48°", "Field of view: 36–48°")},
         {"Tab", L("İmleci bırak / al", "Release / capture cursor")},
         {"1 2 3 4 5 6", L("Altı bakış", "Six views")},
         {"R", L("Başlangıca dön", "Return to the start")},
         {"↑|↓", L("Pozlama", "Exposure")}}}};
    return info;
}

class Live final : public app::Scene {
public:
    ~Live() override { unload(); }
    const app::SceneInfo& info() const override { return info_; }
    void load(app::Host& host, app::LoadState& state) override {
        if (state.step++ == 0) {
            state.label = L("Kireçtaşı adalar ve bahçeler üretiliyor", "Generating limestone islands and gardens");
            state.fraction = 0.05f;
            return;
        }
        std::string error;
        if (!run_.init(host.root(), host.overrides(), error)) {
            state.error = error;
            return;
        }
        run_.render();
        state.fraction = 1.0f;
        state.done = true;
    }
    void frame(app::Host& host, const app::Input& input) override {
        if (!started_) {
            host.captureMouse(looking_);
            host.pace(0);
            started_ = true;
        }
        if (input.pressed(GLFW_KEY_TAB)) {
            looking_ = !looking_;
            settle_ = 3;
            host.captureMouse(looking_);
        }
        if (input.pressed(GLFW_KEY_R)) run_.camera = Run::view(0);
        for (int index = 0; index < 6; ++index)
            if (input.pressed(GLFW_KEY_1 + index)) run_.camera = Run::view(index);
        if (input.pressed(GLFW_KEY_UP)) run_.exposure = std::min(4.0f, run_.exposure + 0.25f);
        if (input.pressed(GLFW_KEY_DOWN)) run_.exposure = std::max(-4.0f, run_.exposure - 0.25f);
        const float dt = float(std::clamp(input.dt, 0.0, 0.1));
        const bool settled = settle_ == 0;
        if (!settled) --settle_;
        const float speed = input.shift ? 36.0f : 12.0f;
        const float forward = float(input.isDown(GLFW_KEY_W)) - float(input.isDown(GLFW_KEY_S));
        const float right = float(input.isDown(GLFW_KEY_D)) - float(input.isDown(GLFW_KEY_A));
        const float up = float(input.isDown(GLFW_KEY_SPACE)) - float(input.isDown(GLFW_KEY_C));
        if (looking_) {
            run_.camera.fovY = std::clamp(run_.camera.fovY - input.scroll * 2.0f, 36.0f, kFovY);
            run_.move(settled ? input.mouseDX : 0.0f, settled ? input.mouseDY : 0.0f,
                      speed * forward, speed * right, speed * up, dt);
        }
        run_.render();
        present();
    }
    const void* image(int& width, int& height) override {
        width = run_.width;
        height = run_.height;
        run_.renderer.download(hostPixels_);
        return hostPixels_.data();
    }
    unsigned texture(int& width, int& height) override {
        width = run_.width;
        height = run_.height;
        return textureReady_ ? texture_ : 0;
    }
    void status(app::Status& status) override {
        using theme::num;
        const auto& stats = run_.renderer.stats();
        status.chips.push_back({L("Serbest uçuş", "Free flight"), true});
        status.chips.push_back({num(stats.cells, 0) + L(" görünür hücre", " visible cells")});
        status.metrics.push_back({L("Çizim", "Render"), num(run_.frameMs, 1) + " ms"});
        status.metrics.push_back({L("Görüş", "Field of view"), num(run_.camera.fovY, 0) + "°"});
    }
    void screenshot(app::Host& host) override {
        host.toast(run_.save(host.capturePath()) ? L("Ekran görüntüsü kaydedildi", "Screenshot saved")
                                                : L("Ekran görüntüsü kaydedilemedi", "Could not save screenshot"));
    }
    void unload() override {
        if (resource_) {
            const auto result = cudaGraphicsUnregisterResource(resource_);
            if (result != cudaSuccess) std::fprintf(stderr, "CUDA unregister: %s\n", cudaGetErrorString(result));
            resource_ = nullptr;
        }
        if (texture_ && glfwGetCurrentContext()) glDeleteTextures(1, &texture_);
        texture_ = 0;
        textureReady_ = false;
        run_.release();
        hostPixels_.clear();
    }
private:
    void present() {
        if (!texture_ && !interopFailed_) {
            glGenTextures(1, &texture_);
            glBindTexture(GL_TEXTURE_2D, texture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, run_.width, run_.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindTexture(GL_TEXTURE_2D, 0);
            const auto result = cudaGraphicsGLRegisterImage(&resource_, texture_, GL_TEXTURE_2D, cudaGraphicsRegisterFlagsWriteDiscard);
            if (result != cudaSuccess) {
                std::fprintf(stderr, "Ruya: GPU sunumu acilamadi, ana bellek yolu: %s\n", cudaGetErrorString(result));
                cudaGetLastError();
                interopFailed_ = true;
                resource_ = nullptr;
            } else std::puts("Ruya: GPU-GPU OpenGL sunumu etkin");
        }
        if (!resource_) { textureReady_ = false; return; }
        auto result = cudaGraphicsMapResources(1, &resource_, 0);
        if (result != cudaSuccess) {
            std::fprintf(stderr, "Ruya: GPU sunum esleme hatasi: %s\n", cudaGetErrorString(result));
            textureReady_ = false;
            return;
        }
        cudaArray_t target = nullptr;
        result = cudaGraphicsSubResourceGetMappedArray(&target, resource_, 0, 0);
        if (result == cudaSuccess)
            result = cudaMemcpy2DToArray(target, 0, 0, run_.renderer.devicePixels(), size_t(run_.width) * sizeof(uchar4),
                                        size_t(run_.width) * sizeof(uchar4), run_.height, cudaMemcpyDeviceToDevice);
        const auto unmapResult = cudaGraphicsUnmapResources(1, &resource_, 0);
        textureReady_ = result == cudaSuccess && unmapResult == cudaSuccess;
        if (!textureReady_) std::fprintf(stderr, "Ruya: GPU sunum aktarimi basarisiz: %s / %s\n",
                                         cudaGetErrorString(result), cudaGetErrorString(unmapResult));
    }
    app::SceneInfo info_ = describe();
    Run run_;
    std::vector<unsigned char> hostPixels_;
    GLuint texture_ = 0;
    cudaGraphicsResource_t resource_ = nullptr;
    bool textureReady_ = false, interopFailed_ = false;
    bool started_ = false, looking_ = true;
    int settle_ = 3;
};
}
app::SceneInfo info() { return describe(); }
std::unique_ptr<app::Scene> make() { return std::make_unique<Live>(); }

int testLive(const std::string& root) {
    // Exercise the actual Scene and its GL handoff with an invisible context and
    // synthetic input. The test host never asks GLFW to move the system cursor.
    struct TestHost final : app::Host {
        explicit TestHost(const std::string& path) : rootPath(path) {}
        const std::string& root() const override { return rootPath; }
        const std::vector<std::string>& overrides() const override { return settings; }
        void toast(const std::string&) override {}
        void captureMouse(bool on) override { captured = on; }
        std::string capturePath() override { return {}; }
        bool testing() const override { return true; }
        const std::string& rootPath;
        std::vector<std::string> settings{"width=2560", "height=1440"};
        bool captured = false;
    } host(root);
    auto fail = [](const char* message) {
        std::fprintf(stderr, "Ruya canli yol: %s\n", message);
        return 1;
    };
    if (!glfwInit()) return fail("GLFW baslatilamadi");
    struct GlfwScope { ~GlfwScope() { glfwTerminate(); } } glfw;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    const std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(64, 64, "Ruya test", nullptr, nullptr), glfwDestroyWindow);
    if (!window || glfwGetWindowAttrib(window.get(), GLFW_VISIBLE) || glfwGetWindowAttrib(window.get(), GLFW_FOCUSED))
        return fail("gizli ve odaksiz GL baglami olusturulamadi");
    glfwMakeContextCurrent(window.get());
    auto scene = make();
    app::LoadState load;
    std::thread loader([&] {
        for (int step = 0; step < 5 && !load.done && load.error.empty(); ++step) scene->load(host, load);
    });
    loader.join();
    if (!load.error.empty()) return fail(load.error.c_str());
    if (!load.done) return fail("yukleme tamamlanmadi");

    app::Input input;
    input.dt = 1.0 / 30.0;
    for (int frame = 0; frame < 3; ++frame) scene->frame(host, input);
    int width = 0, height = 0;
    const GLuint texture = scene->texture(width, height);
    if (!texture || width != 2560 || height != 1440 || !host.captured) return fail("CUDA-GL veya fare yakalama baslamadi");
    std::vector<unsigned char> glPixels(size_t(width) * height * 4);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, glPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    const void* rendererPixels = scene->image(width, height);
    if (glGetError() != GL_NO_ERROR || std::memcmp(glPixels.data(), rendererPixels, glPixels.size()) != 0)
        return fail("GL dokusu ile renderer karesi farkli");
    input.mouseDX = 15;
    input.down = [](int key) { return key == GLFW_KEY_D; };
    scene->frame(host, input);
    rendererPixels = scene->image(width, height);
    if (std::memcmp(glPixels.data(), rendererPixels, glPixels.size()) == 0) return fail("ucus girdisi kareyi degistirmedi");
    std::memcpy(glPixels.data(), rendererPixels, glPixels.size());
    input.pressedKeys = {GLFW_KEY_TAB};
    scene->frame(host, input);
    rendererPixels = scene->image(width, height);
    if (host.captured || std::memcmp(glPixels.data(), rendererPixels, glPixels.size()) != 0)
        return fail("imlec birakilinca kamera yerinde kalmadi");
    input = {};
    input.dt = 1.0 / 30.0;
    input.pressedKeys = {GLFW_KEY_TAB, GLFW_KEY_R};
    scene->frame(host, input);
    input.pressedKeys.clear();
    input.down = [](int key) { return key == GLFW_KEY_W; };
    std::vector<double> times;
    for (int frame = -24; frame < 120; ++frame) {
        input.mouseDX = frame < 60 ? 18.0f : -18.0f;
        const auto start = std::chrono::steady_clock::now();
        scene->frame(host, input);
        if (frame >= 0) times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(times.begin(), times.end());
    const double p50 = (times[59] + times[60]) * 0.5;
    const double p95 = times[113];
    std::printf("Ruya gizli CUDA-GL 2560x1440: %zu kare, p50 %.3f ms, p95 %.3f ms\n", times.size(), p50, p95);
    scene->unload();
    if (glIsTexture(texture)) return fail("GL dokusu kapatmada kaldi");
    std::puts("Ruya: gizli GL, isci is parcaciginda yukleme, piksel esligi, ucus girdisi, Tab ve kapatma gecti");
    return 0;
}
}
