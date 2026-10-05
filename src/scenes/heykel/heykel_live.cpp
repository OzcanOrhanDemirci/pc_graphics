// Scene 6 "Heykel" inside the application: a white fractal cathedral on a plinth in a dark room, 10,000 times deep.
// The wheel zooms towards the cursor, the right button turns the view around what was zoomed at, a click points the
// light at a spot and dragging with the left button swings it around that spot; T runs the dive tour. Still, the
// picture accumulates.
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

// The tour: four dives, each a world point the zoom closes in on in a straight line from the first view. Found by the
// study's random dives (a pixel picked again all the way down, as a hand on the wheel would) and kept as the point
// reached at 10,000 times: a pixel would lead elsewhere at another resolution (a finer threshold, another hole).
// Chosen for frames that hold at every stop (DURUM; --hedef renders them as the tour sees them).
const D3 kDives[] = {{-0.16122597601070529, 0.38169462667579562, 0.18263348231902773},
                     {-0.017099008541529775, 0.014046505992941874, -0.12788962696837886},
                     {0.018479176066056528, 0.026147856746997961, 0.1742891767649056},
                     {-0.012766515728291123, 0.19496334254062009, -0.010087507275673497}};
const int kDiveCount = 4;
const float kStops[] = {10.0f, 100.0f, 1000.0f, 10000.0f};   // where a dive stops to let the picture settle
const int kStopCount = 4;

class HeykelLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "heykel";
        i.number = "06";
        i.title = L("Heykel", "The Sculpture");
        i.subtitle = L("Fraktal bir katedral, on bin kat yakına", "A fractal cathedral, ten thousand times closer");
        i.description = L("Karanlık bir odada, bir kaidenin üstünde beyaz, mat bir heykel. Yaklaştıkça kemerlerinin içinden yeni kemerler "
                          "açılır; on bin katta ekranın genişliği 0,15 mm, iki saç teli kadardır ve yapı orada da sürer.",
                          "A white, matte sculpture on a plinth in a dark room. Come closer and new arches open inside its arches; at "
                          "ten thousand times the screen is 0.15 mm across, two hairs' width, and the structure goes on there too.");
        i.details = {
            {L("Geometri", "Geometry"),
             L("Heykel bir fraktal (sözde Klein, Knighty'nin kutu katlaması ve küre evirmesi), bir bloğa kesilmiş: biçim üçgenlerden "
               "değil bir formülden gelir, ışın yüzeye formülün söylediği uzaklıkla adım adım yürür. Yüzeyin eşiği pikselin o "
               "uzaklıktaki izi kadar; formülün tekrar sayısı da yakınlaşmayla artar: pikselden ince olan çizilmez, ayrıntı bitmez.",
               "The sculpture is a fractal (pseudo-Kleinian: Knighty's box fold and sphere inversion) cut into a block: the form "
               "comes from a formula, not from triangles, and a ray walks to the surface by the distance the formula gives. The "
               "surface's threshold is a pixel's footprint there, and the formula's iterations grow with the zoom: nothing thinner "
               "than a pixel is drawn, and the detail does not run out.")},
            {L("On bin kat", "Ten thousand times"),
             L("Ekran kartının sayıları orada bir pikseli ayırt edemez. Derin Mandelbrot yakınlaşmalarının yolu: bir çapa noktasının "
               "yörüngesi ana işlemcide çift hassasiyetle hesaplanır, her piksel ondan küçük bir fark olarak izlenir. Ölçülen hata "
               "on bin katta piksel başına binde üç.",
               "There the graphics card's numbers cannot tell one pixel from the next. The deep Mandelbrot zooms' way: one anchor "
               "point's orbit is computed in double precision on the processor, every pixel follows it as a small difference. The "
               "measured error at ten thousand times: three thousandths of a pixel.")},
            {L("Işık", "Light"),
             L("Bir spot, merceğin üstünde hafif bir halka ışık ve tavandan sönük bir ışık. Spot sen yaklaştıkça seninle yaklaşır, "
               "yolu kapalıysa yakında açık bir açı arar. Rastgele sayı yok: her piksel her karede aynı sabit örnekleri alır, "
               "durunca yenileri eklenir (kenarlar, spotun yarı gölgesi), hiç gren olmaz.",
               "A spot, a faint ring light on the lens and a dim light from the ceiling. The spot comes closer as you do, and if "
               "its way is blocked it looks for an open angle nearby. No random numbers: every pixel takes the same fixed samples "
               "every frame, still it adds more of them (the edges, the spot's penumbra), never grain.")}};
        i.tags = {L("Geometri", "Geometry"), L("Fraktal", "Fractal"), L("10.000 kat", "10,000 times")};
        i.image = "assets/gallery/heykel.jpg";
        i.accent = 0x9D8CE0;
        i.keys = {{L("Bakış", "View"),
                   {{L("Tekerlek", "Wheel"), L("İmlecin gösterdiği yere yaklaş, uzaklaş", "Zoom towards the cursor, and out")},
                    {L("Sağ tık", "Right button"), L("Sürükle: heykeli çevir", "Drag: turn the sculpture")},
                    {"R", L("İlk görünüm", "The first view")},
                    {"T", L("Dalış turu (başlat / durdur)", "The dive tour (start / stop)")}}},
                  {L("Işık", "Light"),
                   {{L("Sol tık", "Left button"), L("Işığı tıklanan yere çevir", "Point the light at what is clicked")},
                    {L("Sol tık", "Left button"), L("Sürükle: ışığı o noktanın çevresinde döndür", "Drag: swing the light around that spot")},
                    {"↑|↓", L("Pozlama", "Exposure")}, {"F", L("Netlik alanı: makro / hepsi net", "Depth of field: macro / all sharp")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<HeykelLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Heykel kaidesine oturtuluyor", "Setting the sculpture on its plinth");
            st.fraction = 0.1f;
            ++st.step;
            return;
        case 1:
            h_.root = host.root();
            h_.overrides = host.overrides();
            h_.loadConfig();
            h_.buildCandidates();
            h_.init();
            h_.setCandidate(cathedral());
            st.label = L("İlk kare", "The first frame");
            st.fraction = 0.8f;
            ++st.step;
            return;
        default:
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(h_.width) * h_.height * sizeof(uchar4));
            h_.resetSamples();
            h_.addSamples(1);
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
        // the cursor in render pixels
        const bool inView = in.view.w > 0.0f && in.view.contains(in.mouseX, in.mouseY);
        const float rx = in.view.w > 0.0f ? (in.mouseX - in.view.x) / in.view.w * h_.width : 0.0f;
        const float ry = in.view.h > 0.0f ? (in.mouseY - in.view.y) / in.view.h * h_.height : 0.0f;

        // a hand on the scene stops the tour
        const bool hand = (in.scroll != 0.0f && inView) || (in.leftDown && inView) || in.rightDown || in.pressed(GLFW_KEY_R);
        bool toggle = in.pressed(GLFW_KEY_T);
        for (const app::ControlEvent& e : in.events)
            if (e.id == "tur") toggle = true;
        if (tour_ && (hand || toggle)) {
            tour_ = false;
            toggle = false;
            host.toast(L("Tur durdu", "The tour stopped"));
        }
        if (toggle) startTour(host);

        if (in.pressed(GLFW_KEY_R)) {
            h_.home();
            h_.defaultLight();
            h_.aimLight(h_.homeFocus);
            pending_ = 0.0f;
            changed = true;
        }
        bool dofTo = h_.dof;
        if (in.pressed(GLFW_KEY_F)) dofTo = !h_.dof;
        for (const app::ControlEvent& e : in.events)
            if (e.id == "odak" && e.item >= 0) dofTo = e.item == 0;
        if (dofTo != h_.dof) {
            h_.dof = dofTo;
            changed = true;
            host.toast(h_.dof ? L("Netlik alanı: makro", "Depth of field: macro") : L("Netlik alanı: hepsi net", "Depth of field: all sharp"));
        }
        if (in.pressed(GLFW_KEY_UP)) { h_.ev += 0.25f; toneOnly = true; }
        if (in.pressed(GLFW_KEY_DOWN)) { h_.ev -= 0.25f; toneOnly = true; }

        if (tour_) {
            changed = stepTour(dt) || changed;
        } else {
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
                h_.keepLampClear();
                pending_ -= dz;
                if (h_.level == 1.0f && before != 1.0f && dz < 0.0f) pending_ = 0.0f;   // back at the first view
                if (h_.level >= h_.zoomCapAt(zoomQ_) * 0.999f && dz > 0.0f) pending_ = 0.0f;
                if (std::fabs(pending_) <= 1e-5f) {   // the zoom has stopped: the lamp settles onto what is looked at
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
        }
        leftWas_ = in.leftDown;

        if (changed) {
            // the picture again from sample 0, as many of the fixed pattern's samples as fit in ~25 ms (at least one,
            // the whole picture: a moving view never shows a half-made frame)
            h_.resetSamples();
            const int n = h_.sampleMs > 0.0f ? std::clamp(int(25.0f / h_.sampleMs), 1, 4) : 1;
            h_.addSamples(n);
            h_.resolve();
            phase_ = 0;
        } else if (h_.samples < maxSamples_) {
            // still: as many samples as fit in ~35 ms; a sample slower than that is spread over several frames by rows
            if (phase_ == 0) stripes_ = h_.sampleMs > 0.0f ? std::clamp(int(std::ceil(h_.sampleMs / 35.0f)), 1, 24) : 1;
            if (stripes_ == 1) {
                const int spp = std::clamp(int(35.0f / std::max(h_.sampleMs, 0.1f)), 1, 16);
                h_.addSamples(std::min(spp, maxSamples_ - h_.samples));
            } else {
                h_.addSamples(1, phase_, stripes_);
                if (++phase_ == stripes_) phase_ = 0;
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
        using theme::thousands;
        if (tour_) s.chips.push_back({L("Tur · dalış ", "Tour · dive ") + std::to_string(dive_ + 1) + "/" + std::to_string(kDiveCount), true});
        if (h_.samples > 0 && h_.samples < maxSamples_) s.chips.push_back({L("Birikiyor", "Accumulating")});
        const float lv = h_.level;
        s.metrics.push_back({L("Yakınlaşma", "Zoom"), "×" + (lv < 10.0f ? num(lv, 1) : thousands(int(std::lround(lv))))});
        // what the screen's width is at the focus: about 1.5 m at the first view, 0.15 mm (two hairs) at 10,000 times
        {
            float3 r, u, f;
            h_.cameraBasis(r, u, f);
            const double t = std::tan(0.5 * h_.fovDeg * 3.14159265 / 180.0);
            const double wm = 2.0 * t * double(h_.width) / h_.height * length(h_.focus - h_.camPos);
            std::string w = wm >= 0.01 ? num(float(wm * 100.0), 1) + " cm" : wm >= 1e-5 ? num(float(wm * 1000.0), wm >= 1e-3 ? 1 : 2) + " mm"
                                                                                       : num(float(wm * 1e6), 0) + " µm";
            s.metrics.push_back({L("Ekranın genişliği", "The screen's width"), w});
        }
        s.metrics.push_back({L("Örnek", "Samples"), std::to_string(h_.samples)});
        s.metrics.push_back({L("Bir örnek", "One sample"), num(h_.sampleMs, 1) + " ms"});
        app::Control c;
        c.kind = app::Control::Kind::Button;
        c.id = "tur";
        c.text = tour_ ? L("Turu durdur", "Stop the tour") : L("Dalış turu", "The dive tour");
        c.keys = "T";
        c.strong = !tour_;
        c.detail = L("Dört noktaya on bin kata kadar iner; her katta durup netleşir", "Four dives to ten thousand times; it stops to sharpen at every step");
        s.controls.push_back(c);
        app::Control d;
        d.kind = app::Control::Kind::Choice;
        d.id = "odak";
        d.title = L("Netlik alanı", "Depth of field");
        d.items = {L("Makro", "Macro"), L("Hepsi net", "All sharp")};
        d.active = h_.dof ? 0 : 1;
        d.detail = L("Makro: odak yaklaştığın noktada, ötesi yumuşar (F)", "Macro: the focus on what you zoom at, beyond it softens (F)");
        d.section = true;
        s.controls.push_back(d);
    }

    void screenshot(app::Host& host) override {
        h_.save(host.capturePath());
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        h_.loadConfig();
        h_.buildCandidates();
        h_.setCandidate(cathedral());
        pending_ = 0.0f;
        tour_ = false;
        h_.resetSamples();
        h_.addSamples(1);
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
    int cathedral() const {
        for (int k = 0; k < int(h_.cands.size()); ++k)
            if (h_.cands[k].id == "katedral") return k;
        return 0;
    }

    // the tour: home (settle), then to each stop of a dive (zooming, the point under the dive's pixel picked again
    // every frame), a pause there while the picture accumulates, out again to the first view, the next dive
    void startTour(app::Host& host) {
        tour_ = true;
        dive_ = 0;
        beginDive();
        host.toast(L("Dalış turu: dört nokta, on bin kata kadar. Bir tuş ya da fare durdurur", "The dive tour: four dives to ten thousand times. A key or the mouse stops it"));
    }
    void beginDive() {
        h_.home();
        h_.defaultLight();
        h_.aimLight(h_.homeFocus);
        stop_ = -1;   // at home
        hold_ = 0.0f;
        holding_ = true;
        out_ = false;
    }
    bool stepTour(float dt) {
        const D3 q = kDives[dive_];
        if (holding_) {   // let the picture settle: a while, and enough samples
            hold_ += dt;
            // deterministic: no grain to wait out, only the edges and the penumbra to settle (64 of the fixed samples;
            // at 10,000 times ~45 ms each at 2560x1440)
            const float maxHolds[] = {2.5f, 3.0f, 3.5f, 4.5f, 4.5f};   // home, 10, 100, 1,000, 10,000 times
            const float minHold = 2.5f, maxHold = maxHolds[stop_ + 1];
            if (hold_ < minHold || (hold_ < maxHold && h_.samples < 64)) return false;
            holding_ = false;
            if (stop_ == kStopCount - 1) {   // the bottom: back out
                out_ = true;
                return false;
            }
            ++stop_;
            return false;
        }
        if (out_) {   // out to the first view in two seconds or so
            h_.zoomAbout(h_.focus, -dt * 4.6f);
            if (h_.level <= 1.0f) {
                dive_ = (dive_ + 1) % kDiveCount;
                beginDive();
            }
            return true;
        }
        // zooming to the next stop: ten times in ~2.5 s, straight at the dive's point
        const float target = kStops[stop_];
        const float step = std::min(dt * 0.92f, std::log(target / h_.level));
        const float before = h_.level;
        h_.zoomAbout(q, step);
        h_.keepLampClear();
        if (h_.level >= target * 0.999f || h_.level <= before) {
            h_.settleLight();
            holding_ = true;
            hold_ = 0.0f;
        }
        return true;
    }

    app::SceneInfo info_ = describe();
    HeykelApp h_;
    uchar4* pinned_ = nullptr;
    bool paced_ = false;
    float pending_ = 0.0f;           // zoom still to go (log)
    D3 zoomQ_{0, 0, 0};
    bool leftWas_ = false, dragging_ = false, pressInView_ = false;
    float pressX_ = 0.0f, pressY_ = 0.0f;
    int maxSamples_ = 128;          // the fixed pattern: edges and the lamp's penumbra are smooth by then
    int stripes_ = 4, phase_ = 0;    // a slow sample spread over frames: rows y = phase (mod stripes) this frame
    // the tour
    bool tour_ = false, holding_ = false, out_ = false;
    int dive_ = 0, stop_ = -1;
    float hold_ = 0.0f;
};

}   // namespace

namespace heykel {
app::SceneInfo info() { return HeykelLive::describe(); }
std::unique_ptr<app::Scene> make() { return HeykelLive::make(); }
int cli(int argc, char** argv) { return runHeykelCli(argc, argv); }
}   // namespace heykel
