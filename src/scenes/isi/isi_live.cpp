// Scene 9 "Isı" (draft) inside the application: a computer from inside, grown to a landscape, seen from a balcony.
// Four composed views (1-4) and free flight (W A S D, Space up, C down, Shift fast); the drag of the left button or Tab
// looks around. The processor, the memory and the storage are switches that only wake their part of the picture: the
// draft carries no load of its own (Özcan 2026-10-06: compose first, walk in it, then decide).
#include "app/scene.h"
#include "app/theme.h"
#include "core/gpu_stats.h"
#include "scenes/isi/isi_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>

namespace {

using app::L;

float wrapAngle(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

class IsiLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "isi";
        i.number = "09";
        i.title = L("Isı", "Heat");
        i.subtitle = L("Taslak: bilgisayarın içi, bir manzara boyunda", "Draft: the inside of a computer, grown to a landscape");
        i.description = L("Bir balkondan bakıyorsun: ekran kartı üç dev fanıyla sıcak bir duvar, bellek kuleler şehri, işlemcinin pompası bir "
                          "tapınak, güç kaynağı uzakta bir dağ, kasanın delikli paneli yıldızlı bir gök. Kaba bir taslak: kompozisyonu ve içinde "
                          "dolaşmanın hissini görmek için.",
                          "You look out from a balcony: the graphics card a hot wall of three giant fans, the memory a city of towers, the "
                          "processor's pump a temple, the power supply a far mountain, the case's perforated panel a starry sky. A rough draft, "
                          "to see the composition and how it feels to walk in it.");
        i.details = {
            {L("Taslak", "Draft"),
             L("Biçimler formülle tanımlanmış kaba kütleler; ayrıntı yok. İşlemci, RAM ve SSD düğmeleri şimdilik yalnız görüntüyü uyandırır, "
               "yük üretmez. Konsoldaki ekran kartı göstergesi kartın kendi ölçtüğü gücü gösterir.",
               "The forms are rough masses defined by formulas, no detail. The processor, memory and storage switches only wake the "
               "picture for now, they carry no load. The console's graphics card gauge shows the power the card measures itself.")},
            {L("Işık", "Light"),
             L("Rastgele sayı yok: her piksel her karede aynı sabit örnekleri alır. Pus, sıcak parçaların pustaki parıltısı ve gökten inen "
               "huzmeler ışın boyunca kapalı biçimde hesaplanır, adım adım örneklenmez: gren olmaz.",
               "No random numbers: every pixel takes the same fixed samples every frame. The haze, the hot parts' glow in it and the "
               "beams from the sky are computed in closed form along the ray, not sampled step by step: no grain.")}};
        i.tags = {L("Taslak", "Draft"), L("Isı", "Heat"), L("Manzara", "Landscape")};
        i.image = "assets/gallery/isi.jpg";
        i.accent = 0xE0603A;
        i.keys = {{L("Bakış", "View"),
                   {{"1 2 3 4", L("Bestelenmiş bakışlar: balkon, duvarın dibi, kulenin tepesi, pompa", "Composed views: the balcony, under the wall, the tower's top, the pump")},
                    {"R", L("Balkona dön", "Back to the balcony")},
                    {L("Sol tık", "Left button"), L("Sürükle: etrafa bak", "Drag: look around")},
                    {"Tab", L("Fare bakışı (aç / kapat)", "Mouse look (on / off)")},
                    {"↑|↓", L("Pozlama", "Exposure")}}},
                  {L("Uçuş", "Flight"),
                   {{"W A S D", L("İleri, geri, yanlara", "Forward, back, sideways")},
                    {L("Boşluk", "Space"), L("Yüksel", "Up")},
                    {"C", L("Alçal", "Down")},
                    {"Shift", L("Hızlı", "Fast")}}},
                  {L("Parçalar", "Parts"),
                   {{"5 6 7", L("İşlemci, RAM, SSD (taslakta yalnız görüntü)", "Processor, memory, storage (in the draft the picture only)")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<IsiLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Dünya kuruluyor", "Building the world");
            st.fraction = 0.2f;
            ++st.step;
            return;
        default:
            a_.root = host.root();
            a_.overrides = host.overrides();
            a_.loadConfig();
            a_.init();
            a_.cam = IsiApp::view(0);
            gpuStatsInit();
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(a_.width) * a_.height * sizeof(uchar4));
            a_.render();
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        const float dt = float(std::min(0.1, in.dt));
        if (!paced_) {
            host.pace(0.0f);
            paced_ = true;
        }
        // the panel and the keys
        for (const app::ControlEvent& e : in.events) {
            if (e.id == "bakis" && e.item >= 0) goTo(e.item);
            else if (e.id == "cpu" && e.item >= 0) a_.cpuOn = float(e.item);
            else if (e.id == "ram" && e.item >= 0) a_.ramOn = float(e.item);
            else if (e.id == "ssd" && e.item >= 0) a_.ssdOn = float(e.item);
            else if (e.id == "enerji") a_.energy = std::clamp(e.value, 0.0f, 1.0f);
        }
        for (int k = 0; k < 4; ++k)
            if (in.pressed(GLFW_KEY_1 + k)) goTo(k);
        if (in.pressed(GLFW_KEY_R)) goTo(0);
        if (in.pressed(GLFW_KEY_5)) a_.cpuOn = a_.cpuOn > 0.5f ? 0.0f : 1.0f;
        if (in.pressed(GLFW_KEY_6)) a_.ramOn = a_.ramOn > 0.5f ? 0.0f : 1.0f;
        if (in.pressed(GLFW_KEY_7)) a_.ssdOn = a_.ssdOn > 0.5f ? 0.0f : 1.0f;
        if (in.pressed(GLFW_KEY_UP)) a_.ev += 0.25f;
        if (in.pressed(GLFW_KEY_DOWN)) a_.ev -= 0.25f;
        if (in.pressed(GLFW_KEY_TAB)) {
            look_ = !look_;
            host.captureMouse(look_);
        }

        // looking: the drag of the left button, or the captured mouse
        const bool dragging = in.leftDown && in.view.contains(in.mouseX, in.mouseY);
        const float sens = 0.0022f * std::tan(0.5f * a_.cam.fovY * 3.14159265f / 180.0f) / std::tan(0.5f * 50.0f * 3.14159265f / 180.0f);
        if ((look_ || dragging) && (in.mouseDX != 0.0f || in.mouseDY != 0.0f)) {
            tween_ = -1.0f;
            a_.cam.yaw -= in.mouseDX * sens;
            a_.cam.pitch = std::clamp(a_.cam.pitch - in.mouseDY * sens, -1.45f, 1.45f);
            view_ = -1;
        }
        // flight: along the view, sideways, straight up and down
        auto key = [&](int kc) { return in.isDown(kc) ? 1.0f : 0.0f; };
        const float fwd = key(GLFW_KEY_W) - key(GLFW_KEY_S), right = key(GLFW_KEY_D) - key(GLFW_KEY_A);
        const float up = key(GLFW_KEY_SPACE) - key(GLFW_KEY_C);
        if (fwd != 0.0f || right != 0.0f || up != 0.0f) {
            tween_ = -1.0f;
            view_ = -1;
            const float cp = std::cos(a_.cam.pitch);
            const float3 f = make_float3(-std::sin(a_.cam.yaw) * cp, std::sin(a_.cam.pitch), -std::cos(a_.cam.yaw) * cp);
            const float3 r = normalize(cross(f, make_float3(0.0f, 1.0f, 0.0f)));
            const float speed = (in.shift ? 90.0f : 18.0f) * dt;
            a_.cam.pos += (f * fwd + r * right + make_float3(0.0f, up, 0.0f)) * speed;
        }
        // a composed view: flown to slowly
        if (tween_ >= 0.0f) {
            tween_ = std::min(1.0f, tween_ + dt / 2.6f);
            const float s = tween_ * tween_ * tween_ * (tween_ * (tween_ * 6.0f - 15.0f) + 10.0f);
            a_.cam.pos = lerp3(from_.pos, to_.pos, s);
            a_.cam.yaw = from_.yaw + wrapAngle(to_.yaw - from_.yaw) * s;
            a_.cam.pitch = lerpf(from_.pitch, to_.pitch, s);
            a_.cam.fovY = lerpf(from_.fovY, to_.fovY, s);
            if (tween_ >= 1.0f) tween_ = -1.0f;
        }

        // the card's own readings, twice a second: its power wakes the wall
        statsClock_ += dt;
        if (statsClock_ > 0.5f) {
            statsClock_ = 0.0f;
            const GpuStats g = gpuStatsRead();
            a_.gpuWatts = g.watts;
            a_.gpuLimit = g.limitWatts;
            a_.gpuTemp = float(g.tempC);
        }
        const float target = a_.gpuLimit > 0.0f ? std::clamp(a_.gpuWatts / a_.gpuLimit * 1.15f, 0.3f, 1.0f) : 0.8f;
        a_.gpu += (target - a_.gpu) * (1.0f - std::exp(-dt / 1.5f));

        a_.step(dt);
        a_.render();
        cudaMemcpy(pinned_, a_.output, size_t(a_.width) * a_.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
    }

    const void* image(int& w, int& h) override {
        w = a_.width;
        h = a_.height;
        return pinned_;
    }

    void status(app::Status& s) override {
        using theme::num;
        s.chips.push_back({L("Taslak", "Draft"), true});
        s.chips.push_back({view_ >= 0 ? std::string(IsiApp::viewName(view_, app::lang() == app::Lang::TR)) : L("Serbest uçuş", "Free flight")});
        // the card's power and temperature are on the application's own panel already
        s.metrics.push_back({L("Kare", "Frame"), num(a_.frameMs, 1) + " ms"});
        app::Control v;
        v.kind = app::Control::Kind::Choice;
        v.id = "bakis";
        v.title = L("Bakış", "View");
        for (int i = 0; i < IsiApp::viewCount(); ++i) v.items.push_back(IsiApp::viewName(i, app::lang() == app::Lang::TR));
        v.active = view_;
        v.detail = L("1-4 · W A S D ile serbest uçuş", "1-4 · free flight with W A S D");
        s.controls.push_back(v);
        auto onOff = [&](const char* id, const std::string& title, float on, bool first) {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = id;
            c.title = title;
            c.items = {L("Kapalı", "Off"), L("Açık", "On")};
            c.active = on > 0.5f ? 1 : 0;
            c.section = first;
            if (first) c.detail = L("Taslakta yalnız görüntü: yük yok (5 6 7)", "In the draft the picture only: no load (5 6 7)");
            s.controls.push_back(c);
        };
        onOff("cpu", L("İşlemci", "Processor"), a_.cpuOn, true);
        onOff("ram", "RAM", a_.ramOn, false);
        onOff("ssd", "SSD", a_.ssdOn, false);
        app::Control e;
        e.kind = app::Control::Kind::Slider;
        e.id = "enerji";
        e.title = L("Harcanan enerji", "Energy spent");
        e.value = a_.energy;
        e.mark = 0.0f;
        e.text = num(a_.energy * 100.0f, 0) + " %";
        e.detail = L("Taslak: dünyanın soğuktan kehribara dönüşü", "Draft: the world turning from cold to amber");
        e.section = true;
        s.controls.push_back(e);
    }

    void screenshot(app::Host& host) override {
        a_.save(host.capturePath());
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        a_.release();
        if (pinned_) cudaFreeHost(pinned_);
        a_.loadConfig();
        a_.init();
        cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(a_.width) * a_.height * sizeof(uchar4));
        host.toast(L("Ayarlar yeniden yüklendi", "Settings reloaded"));
    }

    void unload() override {
        a_.release();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }
    ~IsiLive() override { unload(); }

private:
    void goTo(int i) {
        from_ = a_.cam;
        to_ = IsiApp::view(i);
        tween_ = 0.0f;
        view_ = i;
    }

    app::SceneInfo info_ = describe();
    IsiApp a_;
    uchar4* pinned_ = nullptr;
    bool paced_ = false, look_ = false;
    int view_ = 0;
    float tween_ = -1.0f;
    IsiCam from_, to_;
    float statsClock_ = 1.0f;
};

}   // namespace

namespace isi {
app::SceneInfo info() { return IsiLive::describe(); }
std::unique_ptr<app::Scene> make() { return IsiLive::make(); }
int cli(int argc, char** argv) { return runIsiCli(argc, argv); }
}   // namespace isi
