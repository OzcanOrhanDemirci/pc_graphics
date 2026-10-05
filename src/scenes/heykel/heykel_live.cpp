// Scene 6 "Heykel" inside the application (G0: the study). One sculpture on a plinth in a dark room; the wheel zooms
// towards the cursor, the right button turns the view around what was zoomed at, a click points the light at a spot
// and dragging with the left button swings it around that spot. Still, the picture accumulates.
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/heykel/heykel_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>

namespace {

using app::L;

class HeykelLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "heykel";
        i.number = "06";
        i.title = L("Heykel", "The Sculpture");
        i.subtitle = L("Taslak: beş aday", "Draft: five candidates");
        i.description = L("Karanlık bir odada, bir kaidenin üstünde beyaz, mat bir heykel. Yaklaştıkça yüzeyinin içinden yeni bir mimari "
                          "açılır. Işığı istediğin yere tıklayarak çevirirsin.",
                          "A white, matte sculpture on a plinth in a dark room. Come closer and a new architecture opens inside its "
                          "surface. A click turns the light wherever you want it.");
        i.details = {
            {L("Geometri", "Geometry"),
             L("Heykeller birer fraktal: biçim üçgenlerden değil bir formülden gelir, ışın yüzeye formülün söylediği uzaklıkla "
               "adım adım yürür. Yüzeyin eşiği pikselin o uzaklıktaki izi kadar: pikselden ince olan çizilmez.",
               "The sculptures are fractals: the form comes from a formula, not from triangles, and a ray walks to the surface "
               "step by step by the distance the formula gives. The surface's threshold is a pixel's footprint at that distance: "
               "what is thinner than a pixel is not drawn.")},
            {L("Işık", "Light"),
             L("Tek bir spot ve çok hafif bir dolgu. Hareket sürerken doğrudan ışık ve tahmini gölge; durunca yol izleme birikir: "
               "spotun gerçek yumuşak gölgesi ve oyuklara seke seke giren ışık.",
               "One spot and a very faint fill. While anything moves: the direct light and an estimated shadow; when still, path "
               "tracing accumulates: the spot's true soft shadow and light bouncing into the hollows.")}};
        i.tags = {L("Geometri", "Geometry"), L("Fraktal", "Fractal"), L("Taslak", "Draft")};
        i.image = "assets/gallery/heykel.jpg";
        i.accent = 0x9D8CE0;
        i.keys = {{L("Bakış", "View"),
                   {{L("Tekerlek", "Wheel"), L("İmlecin gösterdiği yere yaklaş, uzaklaş", "Zoom towards the cursor, and out")},
                    {L("Sağ tık", "Right button"), L("Sürükle: heykeli çevir", "Drag: turn the sculpture")},
                    {"R", L("İlk görünüm", "The first view")}}},
                  {L("Işık", "Light"),
                   {{L("Sol tık", "Left button"), L("Işığı tıklanan yere çevir", "Point the light at what is clicked")},
                    {L("Sol tık", "Left button"), L("Sürükle: ışığı o noktanın çevresinde döndür", "Drag: swing the light around that spot")},
                    {"↑|↓", L("Pozlama", "Exposure")}}},
                  {L("Aday", "Candidate"), {{"1-5", L("Aday seç", "Choose a candidate")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<HeykelLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Heykeller kaidelerine oturtuluyor", "Setting the sculptures on their plinths");
            st.fraction = 0.1f;
            ++st.step;
            return;
        case 1:
            h_.root = host.root();
            h_.overrides = host.overrides();
            h_.loadConfig();
            h_.buildCandidates();
            h_.init();
            h_.setCandidate(0);
            st.label = L("İlk kare", "The first frame");
            st.fraction = 0.8f;
            ++st.step;
            return;
        default:
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(h_.width) * h_.height * sizeof(uchar4));
            h_.renderPreview();
            h_.resolve();
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        const float dt = float(std::min(0.1, in.dt));
        bool changed = false, toneOnly = false;
        if (!paced_) {
            host.pace(0.0f);
            paced_ = true;
        }
        for (const app::ControlEvent& e : in.events)
            if (e.id == "aday" && e.item >= 0 && e.item != h_.cand) {
                choose(e.item);
                changed = true;
            }
        for (int k = 0; k < int(h_.cands.size()) && k < 9; ++k)
            if (in.pressed(GLFW_KEY_1 + k) && k != h_.cand) {
                choose(k);
                changed = true;
            }
        if (in.pressed(GLFW_KEY_R)) {
            h_.home();
            h_.defaultLight();
            h_.aimLight(h_.homeFocus);
            pending_ = 0.0f;
            changed = true;
        }
        if (in.pressed(GLFW_KEY_UP)) { h_.ev += 0.25f; toneOnly = true; }
        if (in.pressed(GLFW_KEY_DOWN)) { h_.ev -= 0.25f; toneOnly = true; }

        // the cursor in render pixels
        const bool inView = in.view.w > 0.0f && in.view.contains(in.mouseX, in.mouseY);
        const float rx = in.view.w > 0.0f ? (in.mouseX - in.view.x) / in.view.w * h_.width : 0.0f;
        const float ry = in.view.h > 0.0f ? (in.mouseY - in.view.y) / in.view.h * h_.height : 0.0f;

        // the wheel: what is under the cursor is the point the camera closes in on (it stays put on the screen)
        if (in.scroll != 0.0f && inView) {
            D3 q;
            if (!h_.pick(rx, ry, q)) {   // nothing there: a point on the ray as far as the focus
                float3 r, u, f;
                h_.cameraBasis(r, u, f);
                const float t = std::tan(0.5f * h_.fovDeg * 3.14159265f / 180.0f);
                const float sx = (rx / h_.width * 2.0f - 1.0f) * t * float(h_.width) / h_.height, sy = (1.0f - ry / h_.height * 2.0f) * t;
                q = h_.camPos + d3(normalize(f + r * sx + u * sy)) * length(h_.focus - h_.camPos);
            }
            zoomQ_ = q;
            pending_ += in.scroll * std::log(1.35f);
        }
        if (std::fabs(pending_) > 1e-5f) {
            float dz = pending_ * (1.0f - std::exp(-dt * 12.0f));
            if (std::fabs(pending_) < 2e-3f) dz = pending_;
            const float before = h_.level;
            h_.zoomAbout(zoomQ_, dz);
            pending_ -= dz;
            if (h_.level == 1.0f && before != 1.0f && dz < 0.0f) pending_ = 0.0f;   // back at the first view
            if (h_.level >= h_.zoomCapAt(zoomQ_) * 0.999f && dz > 0.0f) pending_ = 0.0f;
            if (std::fabs(pending_) <= 1e-5f) {   // the zoom has stopped: the lamp settles onto what is being looked at
                pending_ = 0.0f;
                if (h_.level > 1.0f) h_.settleLight();
            }
            changed = true;
        }
        // the right button turns the view around the focus (the plinth turning)
        if (in.rightDown && (in.mouseDX != 0.0f || in.mouseDY != 0.0f))
            changed = h_.orbit(in.mouseDX * 0.0025f, -in.mouseDY * 0.0025f) || changed;
        // the left button: a click points the light, a drag swings it
        if (in.leftDown && !leftWas_) {
            pressX_ = in.mouseX;
            pressY_ = in.mouseY;
            dragging_ = false;
            pressInView_ = inView;
        }
        if (in.leftDown && pressInView_) {
            if (!dragging_ && std::hypot(in.mouseX - pressX_, in.mouseY - pressY_) > 5.0f) dragging_ = true;
            if (dragging_ && (in.mouseDX != 0.0f || in.mouseDY != 0.0f)) {
                h_.turnLight(in.mouseDX * 0.003f, -in.mouseDY * 0.003f);
                changed = true;
            }
        }
        if (!in.leftDown && leftWas_ && pressInView_ && !dragging_) {
            D3 q;
            float3 n;
            if (h_.pick(rx, ry, q, &n)) {
                h_.aimLight(q, &n);
                changed = true;
            }
        }
        leftWas_ = in.leftDown;

        if (changed) {
            h_.renderPreview();
            h_.resolve();
            phase_ = 0;
        } else if (h_.samples < maxSamples_) {
            // still: as many samples as fit in ~35 ms; a sample slower than that is spread over several frames by rows
            if (phase_ == 0) stripes_ = h_.pathMs > 0.0f ? std::clamp(int(std::ceil(h_.pathMs / 35.0f)), 1, 24) : 4;
            if (stripes_ == 1) {
                const int spp = std::clamp(int(35.0f / std::max(h_.pathMs, 0.1f)), 1, 16);
                h_.accumulate(std::min(spp, maxSamples_ - h_.samples));
            } else {
                h_.pathPass(1, phase_, stripes_);
                if (++phase_ == stripes_) {
                    phase_ = 0;
                    ++h_.samples;
                }
            }
            h_.resolve();
        } else if (toneOnly) {
            h_.resolve();
        }
        cudaMemcpy(pinned_, h_.output, size_t(h_.width) * h_.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
    }

    const void* image(int& w, int& h) override {
        w = h_.width;
        h = h_.height;
        return pinned_;
    }

    void status(app::Status& s) override {
        using theme::num;
        s.chips.push_back({L("Taslak · G0", "Draft · G0"), true});
        if (h_.samples > 0 && h_.samples < maxSamples_) s.chips.push_back({L("Birikiyor", "Accumulating")});
        s.metrics.push_back({L("Yakınlaşma", "Zoom"), "×" + num(h_.level, h_.level < 10.0f ? 1 : 0)});
        s.metrics.push_back({L("Örnek", "Samples"), std::to_string(h_.samples)});
        s.metrics.push_back({L("Önizleme", "Preview"), num(h_.previewMs, 1) + " ms"});
        s.metrics.push_back({L("Yol izleme", "Path tracing"), num(h_.pathMs, 1) + L(" ms/örnek", " ms/sample")});
        app::Control c;
        c.kind = app::Control::Kind::Choice;
        c.id = "aday";
        c.title = L("Aday", "Candidate");
        for (const HeykelCandidate& k : h_.cands) c.items.push_back(L(k.tr, k.en));
        c.active = h_.cand;
        c.detail = L("Taslak: biçim ve genel görünüm için; ayrıntı sonra", "A draft: for the form and the overall look; the detail later");
        s.controls.push_back(c);
    }

    void screenshot(app::Host& host) override {
        h_.save(host.capturePath());
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        const int c = h_.cand;
        h_.loadConfig();
        h_.buildCandidates();
        choose(c);
        h_.renderPreview();
        h_.resolve();
        host.toast(L("Ayarlar yeniden yüklendi", "Settings reloaded"));
    }

    void unload() override {
        h_.release();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }
    ~HeykelLive() override { unload(); }

private:
    void choose(int k) {
        h_.setCandidate(k);
        pending_ = 0.0f;
    }
    app::SceneInfo info_ = describe();
    HeykelApp h_;
    uchar4* pinned_ = nullptr;
    bool paced_ = false;
    float pending_ = 0.0f;           // zoom still to go (log)
    D3 zoomQ_{0, 0, 0};
    bool leftWas_ = false, dragging_ = false, pressInView_ = false;
    float pressX_ = 0.0f, pressY_ = 0.0f;
    int maxSamples_ = 1024;
    int stripes_ = 4, phase_ = 0;    // a slow sample spread over frames: rows y = phase (mod stripes) this frame
};

}   // namespace

namespace heykel {
app::SceneInfo info() { return HeykelLive::describe(); }
std::unique_ptr<app::Scene> make() { return HeykelLive::make(); }
int cli(int argc, char** argv) { return runHeykelCli(argc, argv); }
}   // namespace heykel
