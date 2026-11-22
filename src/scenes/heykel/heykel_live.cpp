// Scene 6 "Heykel" inside the application: a white Menger sponge cut through, on a plinth in a dark room, 10,000 times
// deep.
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
const D3 kDives[] = {{-0.10428323765439799, 0.32260425148676414, 0.14364993971126663},     // stars all the way down
                     {-0.0090906256663228751, 0.21663721015711312, 0.0030597578837242754},  // a corner, then galleries
                     {0.16226973321421534, 0.32145899943419298, 0.023848270069743566},      // stars, a triangle, stars
                     {-0.04421500363838525, 0.13596154418132148, 0.11260087443935322}};     // from the stars into the tunnels
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
        i.subtitle = L("Kesilmiş bir Menger süngeri, on bin kat yakın", "A cut Menger sponge, ten thousand times closer");
        i.description = L("Karanlık bir odada, kaidenin üstünde ortasından kesilmiş beyaz bir küp durur; kesitte altı köşeli yıldızlar "
                          "vardır. Tekerlekle imlece doğru yaklaşırsın; yıldızların ve kare tünellerin içinden yenileri açılır. "
                          "On bin katta ekran 0,15 mm genişliğindedir, yapı orada da sürer.",
                          "A white cube cut through its middle stands on a plinth in a dark room, six-pointed stars in the cut. The "
                          "wheel takes you towards the cursor, and new stars and square tunnels open inside them. At ten thousand "
                          "times the screen is 0.15 mm across, and the structure goes on.");
        i.details = {
            {L("Formülden gelen biçim", "A shape from a formula"),
             L("Heykel bir Menger süngerinin yarısıdır: küp, uzun köşegenine dik olarak tam ortasından kesilmiş, altıgen kesit "
               "sana dönük. Biçim üçgenlerden değil bir formülden gelir. Formül her noktada yüzeye olan uzaklığı verir; ışın her "
               "adımda o uzaklık kadar ilerleyerek yüzeye ulaşır (en çok 400 adım).",
               "The sculpture is half a Menger sponge: the cube is cut through its centre, across its long diagonal, and the "
               "hexagonal cut faces you. The shape comes from a formula, not from triangles. The formula gives the distance to "
               "the surface at any point, and a ray reaches the surface by stepping that distance at a time (at most 400 steps).")},
            {L("Dört piksellik delik kuralı", "The four-pixel hole rule"),
             L("Her noktada yalnız ekranda en az dört piksel büyüklüğündeki delikler çizilir; daha küçükleri gren olurdu. "
               "Yaklaştıkça bir sonraki düzeyin delikleri gerçek kare tüneller olarak, merkezlerinden genişleyerek açılır. "
               "İki düzeyin biçimini karıştırmak yıldızları üçgene çeviriyordu; bu yolla biçim hiçbir yakınlıkta bozulmaz.",
               "At every point only holes at least four pixels wide on screen are drawn; smaller ones would be grain. As you "
               "come closer, the next level's holes open as true square tunnels, widening from their centres. Blending two "
               "levels' shapes turned the stars into triangles; this way the form holds at every distance.")},
            {L("On bin kat: pertürbasyon", "Ten thousand times: perturbation"),
             L("On bin katta komşu pikseller, ekran kartının tek hassasiyetli sayılarıyla birbirinden ayrılamaz; düz hesapta hata "
               "0,24 piksel olur. Derin Mandelbrot yakınlaştırmalarının yöntemi kullanılır: bin kattan sonra bir çapa noktasının "
               "yolu CPU'da çift hassasiyetle hesaplanır, her piksel ondan küçük bir fark olarak izlenir. On bin katta ölçülen "
               "hata 0,017 piksel.",
               "At ten thousand times the GPU's single-precision numbers can no longer tell neighbouring pixels apart; computed "
               "plainly, the error is 0.24 pixel. The method of deep Mandelbrot zooms is used: beyond a thousand times, one anchor "
               "point's path is computed in double precision on the CPU, and every pixel follows it as a small difference. The "
               "measured error at ten thousand times is 0.017 pixel.")},
            {L("Her kare aynı, birikim yok", "Every frame the same"),
             L("Rastgele sayı kullanılmaz. Her piksel her karede aynı dört sabit noktadan örneklenir ve örnekler 1,5 piksellik "
               "Blackman-Harris süzgeciyle birleştirilir. Kareler üst üste biriktirilmez: hareket ederken gördüğün, durunca "
               "gördüğünle aynıdır. Ekran kartı yalnız bir şey değiştiğinde yeni kare çizer.",
               "No random numbers are used. Every pixel is sampled at the same four fixed points every frame, and the samples "
               "are combined with a 1.5-pixel Blackman-Harris filter. Frames are not accumulated: what you see in motion is what "
               "you see at rest. The GPU draws a new frame only when something changes.")},
            {L("Işık", "Light"),
             L("Ana ışık bir spot. Sen yaklaştıkça hedefi yaklaştığın yere yumuşakça kayar; yolu kapanırsa yakında açık bir hat "
               "arar ve oraya bir karede değil, yavaşça döner. Yumuşak gölgesi, gölge ışını boyunca ölçülen uzaklıklardan kestirilir. "
               "Kameranın hemen üstündeki zayıf, gölgesiz bir ışık spotun gölgesinde kalan yapıyı da okutur.",
               "The main light is a spot. As you come closer its target slides smoothly to where you are going; if its way is "
               "blocked it looks for an open line nearby and turns there slowly, not in one frame. Its soft shadow is estimated "
               "from the distances measured along the shadow ray. A faint, shadowless light just above the camera keeps the "
               "structure readable in the spot's shadow too.")},
            {L("Kamera heykelin yanında kalır", "The camera stays by the sculpture"),
             L("Tekerlek yalnız heykelin bir noktasına yaklaşır, boşluğa değil. Kamera geri çekilirken ve dönerken yolunu sınar, "
               "modele çarpınca durur ve ilk görünümdeki uzaklığından öteye gidemez. Arkası duvarsa uzaklaşma, gelinen yolu geri "
               "sarar.",
               "The wheel only closes in on a point of the sculpture, never on empty space. Pulling back and turning, the camera "
               "tests its path, stops at the model, and never goes further than its distance in the first view. With a wall "
               "behind it, zooming out retraces the way it came.")}};
        i.tags = {L("Mesafe alanı (SDF)", "Signed distance field"), L("Fraktal", "Fractal"), L("Pertürbasyon", "Perturbation")};
        i.image = "assets/gallery/heykel.jpg";
        i.accent = 0x9D8CE0;
        i.keys = {{L("Bakış", "View"),
                   {{L("Tekerlek", "Wheel"), L("İmlece doğru yaklaş, geri uzaklaş", "Zoom to the cursor, and back out")},
                    {L("Sağ tık", "Right click"), L("Basılı sürükle: heykeli çevir", "Drag: turn the sculpture")},
                    {"R", L("İlk görünüme dön", "Back to the first view")},
                    {"T", L("Dalış turunu başlat / durdur", "Start / stop the dive tour")}}},
                  {L("Işık", "Light"),
                   {{L("Sol tık", "Left click"), L("Işığı tıklanan yere çevir", "Aim the light at the spot clicked")},
                    {L("Sol tık", "Left click"), L("Sürükle: ışığı çevresinde gezdir", "Drag: swing the light around it")},
                    {"↑|↓", L("Pozlama ±0,25 EV", "Exposure ±0.25 EV")}, {"F", L("Netlik alanı: makro / hepsi net", "Depth of field: macro / all sharp")}}}};
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
            h_.setCandidate(sculptureIndex());
            st.label = L("İlk kare", "The first frame");
            st.fraction = 0.8f;
            ++st.step;
            return;
        default:
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(h_.width) * h_.height * sizeof(uchar4));
            h_.renderFrame();
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
                if (in.scroll < 0.0f) {   // out: about the focus (the way back to the first view, zoomAbout)
                    zoomQ_ = h_.focus;
                    pending_ += in.scroll * std::log(1.35f);
                } else if (h_.zoomTarget(rx, ry, q)) {   // in: towards the sculpture only (on the cursor, else nearest)
                    zoomQ_ = q;
                    pending_ += in.scroll * std::log(1.35f);
                }
            }
            if (std::fabs(pending_) > 1e-5f) {
                float dz = pending_ * (1.0f - std::exp(-dt * 12.0f));
                if (std::fabs(pending_) < 2e-3f) dz = pending_;
                // out: about the focus as it is now (on the way back to the first view it moves with the camera)
                const bool clear = h_.zoomAbout(dz < 0.0f ? h_.focus : zoomQ_, dz);
                h_.followLight(dt);
                pending_ = clear ? pending_ - dz : 0.0f;   // nowhere further to go: it stops there
                if (dz < 0.0f && h_.atHome()) pending_ = 0.0f;   // back at the first view
                if (h_.level >= h_.zoomCapAt(zoomQ_) * 0.999f && dz > 0.0f) pending_ = 0.0f;
                if (std::fabs(pending_) <= 1e-5f) pending_ = 0.0f;   // the lamp came along: nothing to re-aim
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

        // The lamp turns smoothly towards a clear line (a frame's step, never a jump)
        if (h_.turnLampTowardsGoal(dt)) changed = true;
        // One frame, the same whether anything moves or not (Ozcan 2026-10-05: "RTX'in olayi nedir, isik oradadir [...]
        // simdiki hissiyat sen yakinlastin ben bi ayarliyim kendimi dur diyormus gibi"): no accumulation that settles
        // afterwards; a still view keeps the frame it was drawn with
        if (changed) {
            h_.renderFrame();
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
        s.metrics.push_back({L("Kare", "Frame"), num(h_.frameMs, 1) + " ms"});
        app::Control c;
        c.kind = app::Control::Kind::Button;
        c.id = "tur";
        c.text = tour_ ? L("Turu durdur", "Stop the tour") : L("Dalış turu", "The dive tour");
        c.keys = "T";
        c.strong = !tour_;
        c.detail = L("Dört noktaya on bin kata dek iner; 10, 100, 1.000 ve 10.000 katta durur", "Four dives to 10,000 times, pausing at 10, 100, 1,000 and 10,000");
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
        h_.setCandidate(sculptureIndex());
        pending_ = 0.0f;
        tour_ = false;
        h_.renderFrame();
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
    int sculptureIndex() const { return h_.sculpture(); }

    // the tour: home (settle), then to each stop of a dive (zooming, the point under the dive's pixel picked again
    // every frame), a pause there while the picture accumulates, out again to the first view, the next dive
    void startTour(app::Host& host) {
        tour_ = true;
        dive_ = 0;
        beginDive();
        h_.defaultLight();
        h_.aimLight(h_.homeFocus);
        host.toast(L("Dalış turu: dört nokta, on bin kata dek. Fare, R ya da T durdurur", "The dive tour: four dives to ten thousand times. The mouse, R or T stops it"));
    }
    void beginDive() {
        h_.home();   // the lamp keeps where it is (out of the last dive it came back to the first view's scale)
        stop_ = -1;   // at home
        hold_ = 0.0f;
        holding_ = true;
        out_ = false;
    }
    bool stepTour(float dt) {
        const D3 q = kDives[dive_];
        if (holding_) {   // a pause to look (nothing settles: the frame is the same moving or still)
            hold_ += dt;
            if (hold_ < 2.5f) return false;
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
            h_.followLight(dt);
            if (h_.atHome()) {
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
        h_.followLight(dt);
        if (h_.level >= target * 0.999f || h_.level <= before) {
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
