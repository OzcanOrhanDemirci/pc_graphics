// Scene 7 "Ölçek" inside the application: from a beach in Izmir out to the edge of the observable universe and in to
// the Planck length, 62 decades, with the wheel only. R goes back to the beach, T runs the tour.
//
// Its second option "Hız" (docs/olcek/hiz/PLAN.md): a rocket from the Earth's orbit to Proxima Centauri and on to
// Andromeda, special relativity with its full formulas, the passenger's clock as the screen's. W thrusts, S brakes,
// Space arrives by itself; Tab switches between the rocket's eye and the view from its side. The journey is kept on
// disk: it goes on where it was left (the time the scene is closed does not count).
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/olcek/olcek_app.h"
#include "scenes/olcek/olcek_bugun.h"
#include "scenes/olcek/olcek_hiz_pano.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace {

using app::L;
namespace hz = olcek::hiz;

constexpr double kPi = 3.14159265358979323846;

// ---- numbers for the panel: one source, olcek_hiz_pano.cpp
using hz::betaText;
using hz::clockText;
using hz::dateText;
using hz::distText;
using hz::gammaText;
using hz::spanText;
using hz::speedText;
std::string sci(double v, int digits) { return hz::sciText(v, digits); }
std::string fovText(double tanHalf) {
    const double fov = 2.0 * std::atan(tanHalf), deg = fov * 180.0 / kPi;
    if (deg >= 1.0) return theme::num(deg, 1) + "°";
    if (deg * 3600.0 >= 1.0) return theme::num(deg * 3600.0, 2) + L(" yay saniyesi", " arcsec");
    return sci(fov, 2) + " rad";
}

class OlcekLive : public app::Scene {
public:
    static app::SceneInfo describe() {
        app::SceneInfo i;
        i.id = "olcek";
        i.number = "07";
        i.title = L("Ölçek", "Scale");
        i.subtitle = L("İzmir'de bir sahilden evrenin kenarına ve Planck uzunluğuna", "From a beach in Izmir to the edge of the universe and to the Planck length");
        i.description = L("Ayağının ucundaki kumdan başlayıp yalnız tekerlekle uzaklaşır ya da yaklaşırsın: dışarı Dünya, Güneş, Samanyolu, "
                          "kozmik ağ; içeri kum tanesi, atom, çekirdek, proton, kuark ve Planck. Her şey gerçek yerinde ve gerçek boyunda, "
                          "aradaki boşluklar da gerçek uzunluğunda.",
                          "Start at the sand at your feet and move out or in with the wheel alone: out to the Earth, the Sun, the Milky "
                          "Way, the cosmic web; in to a grain of sand, an atom, its nucleus, a proton, a quark and the Planck length. "
                          "Everything at its true place and size, and the empty stretches between them at their true length too.");
        i.details = {
            {L("Altmış iki onluk", "Sixty-two decades"),
             L("Ekranın genişliği 10⁻³⁵ metreden 10²⁷ metreye iner çıkar. Mutlak bir konum yok: her şey bir çapa ağacına asılı "
               "(Güneş, Dünya, sahildeki nokta, atom, çekirdek, proton, kuark), iki komşunun arası yalnız ortak atalarına kadar "
               "toplanır; ekranda aynı anda görünen beş onluk için çift hassasiyet bolca yeter.",
               "The screen's width runs from 10⁻³⁵ metres to 10²⁷ metres. There is no absolute position: everything hangs on a "
               "tree of anchors (the Sun, the Earth, the beach point, an atom, its nucleus, a proton, a quark), and the way between "
               "two neighbours is summed only up to their common ancestor; for the five decades on the screen at once, double "
               "precision is plenty.")},
            {L("Bugün", "Today"),
             L("5 Ekim 2026, Çandarlı'da (Dikili) güneşin tam tepeden geçtiği an: gezegenler, Ay, Güneş ve Samanyolu'nun merkezi "
               "o anın gerçek yerinde (JPL DE440). Zaman her ölçekte kendi hızıyla akar ama hiçbir şeyi değiştirmez: eve her "
               "dönüşte aynı öğle.",
               "5 October 2026, the moment the Sun crosses the meridian at Çandarlı (Dikili): the planets, the Moon, the Sun and "
               "the Galaxy's centre where they truly were then (JPL DE440). Time flows at its own pace at every scale but changes "
               "nothing: every way home ends at the same noon.")},
            {L("Işık olan yerde gerçek veri", "Where there is light, real data"),
             L("Kalenin önündeki kumsal Sentinel-2'den (10 m), kıyı çizgisi oradan; Dünya Blue Marble, üstünde o günlerin "
               "MODIS ve VIIRS bulutları (deniz parıltısı bulut ürünüyle ayıklanmış). Ay LRO'dan. Asteroit ve Kuiper "
               "kuşakları Minor Planet Center'ın 1,57 milyon yörüngesinden. 109.400 yıldız HYG'den, "
               "Samanyolu'nun kolları ölçümlerden (Reid 2019), M31 ve M33 gerçek yönelimleriyle, 43.490 galaksi 2MRS'ten; "
               "ötesi kozmik ağın gerçek istatistiği, bir dilim olarak.",
               "The beach in front of the castle from Sentinel-2 (10 m), the shoreline from it; the Earth from Blue Marble, "
               "with those days' MODIS and VIIRS clouds on it (the sea's glint sorted out by a cloud product). The Moon from "
               "LRO. The asteroid and Kuiper belts from the Minor Planet Center's 1.57 million orbits. 109,400 stars from "
               "HYG, the Milky Way's arms from measurements (Reid 2019), M31 and M33 in their true orientation, 43,490 "
               "galaxies from 2MRS; beyond them the cosmic web's true statistics, as a slice.")},
            {L("Işığın altında", "Below the light"),
             L("Renk ışığın dalga boyunda söner, mikroskopların gri resimleri gelir; atomdan aşağısı temsili renkle "
               "parçacıklar, sayılar gerçek. Elektronlar kabuklarının dağılımında ışık noktaları (içeri girerken yanından "
               "geçerler), çekirdeğin 28 nükleonu, protonun renk yüklü üç kuarkı ve gluon akısı, kuarkın çevresinde her "
               "ölçekte sanal çift halkaları (türleri kuvvetlerin o boydaki şiddetine göre), en dipte köpük ve bir sicim "
               "(kuram).",
               "Colour fades at the light's wavelength into the microscopes' grey pictures; from the atom down, particles "
               "in a representational palette, the numbers true. Electrons as points of light in their shells' "
               "distributions (they stream past on the way in), the nucleus's 28 nucleons, the proton's three coloured "
               "quarks and their gluon flux, virtual pairs' loops at every scale about the quark (their kinds by the "
               "forces' strengths at that size), at the bottom foam and a string (theory).")},
            {L("Görüntü", "The picture"),
             L("Her piksel her karede aynı dört sabit örneği alır: gren yok, titreme yok. Pikselin altına inen bir cisim ışığını "
               "koruyarak noktaya döner; gözün kapağından hızlı dönen bir yörünge halkaya, hızlı titreşen bir sicim kenarları "
               "parlak bir banda.",
               "Every pixel takes the same four fixed samples every frame: no grain, no flicker. A body that shrinks below a pixel "
               "turns into a point and keeps its light; an orbit faster than the eye's shutter turns into a ring, a string "
               "vibrating faster into a band with bright edges.")}};
        i.tags = {L("Ölçek", "Scale"), L("62 onluk", "62 decades"), L("Gerçek konumlar", "True positions")};
        i.image = "assets/gallery/olcek.jpg";
        i.accent = 0xE08BAE;
        i.keys = {{L("Ölçek", "Scale"),
                   {{L("Tekerlek", "Wheel"), L("Uzaklaş, yaklaş", "Move out, move in")},
                    {"R", L("Sahile dön", "Back to the beach")},
                    {"T", L("Tur: evrenin kenarına, geri, Planck'a, geri", "The tour: to the edge of the universe, back, to Planck, back")}}},
                  {L("Hız", "Speed"),
                   {{"W", L("İtiş (0,4 c/s: 12 milyon g)", "Thrust (0.4 c/s: 12 million g)")},
                    {"S", L("Fren (aynı güçle; geri vites yok)", "Brake (the same power; no reverse)")},
                    {L("Boşluk", "Space"), L("Otomatik varış: doğru anda frenleyip durur", "Automatic arrival: brakes at the right moment")},
                    {"Tab", L("Roketin gözü ↔ yandan", "The rocket's eye ↔ from the side")},
                    {"G", L("Grafikler: aç, kapat", "The graphs: on, off")},
                    {L("Sürükle", "Drag"), L("Başını çevir (roketin gözü)", "Turn your head (the rocket's eye)")},
                    {L("Tekerlek", "Wheel"), L("Dürbün (göz), ölçek (yandan)", "Binoculars (eye), scale (side)")},
                    {"R", L("İleri bak / rokete dön", "Look ahead / back to the rocket")}}}};
        i.reloadable = true;
        return i;
    }
    static std::unique_ptr<app::Scene> make() { return std::make_unique<OlcekLive>(); }
    const app::SceneInfo& info() const override { return info_; }

    void load(app::Host& host, app::LoadState& st) override {
        switch (st.step) {
        case 0:
            st.label = L("Bugünün gökyüzü kuruluyor", "Setting up today's sky");
            st.fraction = 0.1f;
            ++st.step;
            return;
        case 1:
            a_.root = host.root();
            a_.overrides = host.overrides();
            a_.loadConfig();
            a_.init();
            st.label = L("Dünya'nın ve Ay'ın haritaları", "The maps of the Earth and the Moon");
            st.fraction = 0.3f;
            ++st.step;
            return;
        case 2:
            a_.loadMaps();
            st.label = L("Roketin yolu ve ışığın renkleri", "The rocket's way and the colours of light");
            st.fraction = 0.8f;
            ++st.step;
            return;
        case 3:
            a_.prepareHiz();
            savePath_ = host.testing() ? std::string() : a_.root + "/hiz_yolculuk.txt";   // (a test run leaves it alone)
            restore();
            st.label = L("İlk kare", "The first frame");
            st.fraction = 0.9f;
            ++st.step;
            return;
        default:
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(a_.sc.width) * a_.sc.height * sizeof(uchar4));
            if (mode_ == 1) hizRender(0.0);
            else a_.render();
            st.fraction = 1.0f;
            st.done = true;
            return;
        }
    }

    void frame(app::Host& host, const app::Input& in) override {
        if (!paced_) {
            host.pace(0.0f);
            paced_ = true;
        }
        for (const app::ControlEvent& e : in.events)
            if (e.id == "secenek" && e.item >= 0 && e.item != mode_) {
                if (e.item == 1) enterHiz();
                else leaveHiz();
                host.toast(mode_ == 1 ? L("Hız: W itiş, S fren, Boşluk otomatik varış, Tab görüntü", "Speed: W thrust, S brake, Space automatic arrival, Tab view")
                                      : L("Ölçek: tekerlekle uzaklaş, yaklaş", "Scale: move out and in with the wheel"));
            }
        if (mode_ == 1) {
            hizFrame(host, in);
            return;
        }
        const double dt = std::min(0.1, in.dt);
        olcek::Scale& sc = a_.sc;
        const double sPrev = sc.s;
        const bool inView = in.view.w > 0.0f && in.view.contains(in.mouseX, in.mouseY);
        const double step = std::log10(double(a_.cfg.f("wheel", 1.5f)));
        const double maxSpeed = a_.cfg.f("speed.max", 20.0f);   // decades a second, the fastest the wheel goes

        bool toggle = in.pressed(GLFW_KEY_T);
        for (const app::ControlEvent& e : in.events)
            if (e.id == "tur") toggle = true;
        const bool hand = (in.scroll != 0.0f && inView) || in.pressed(GLFW_KEY_R);
        if (tour_ && (hand || toggle)) {
            tour_ = false;
            toggle = false;
            host.toast(L("Tur durdu", "The tour stopped"));
        }
        if (toggle) {
            tour_ = true;
            leg_ = 0;
            hold_ = 0.0;
            pending_ = 0.0;
            home_ = false;
            host.toast(L("Tur: evrenin kenarına, geri sahile, Planck'a, geri sahile. Tekerlek ya da T durdurur",
                         "The tour: to the edge of the universe, back to the beach, to Planck, back. The wheel or T stops it"));
        }
        if (in.pressed(GLFW_KEY_R)) {
            home_ = true;
            pending_ = 0.0;
        }

        if (tour_) {
            const double targets[4] = {sc.sMax, 0.0, sc.sMin, 0.0};
            const double speed = a_.cfg.f("tour.speed", 0.5f);
            const double target = targets[leg_];
            if (hold_ > 0.0) {
                hold_ -= dt;
                if (hold_ <= 0.0) leg_ = (leg_ + 1) % 4;
            } else {
                // even decades a second, easing into each end over the last half decade
                const double left = target - sc.s;
                const double v = speed * std::clamp(std::fabs(left) / 0.5, 0.15, 1.0);
                sc.s += std::clamp(left, -v * dt, v * dt);
                if (std::fabs(target - sc.s) < 1e-4) {
                    sc.s = target;
                    hold_ = 3.0;
                }
            }
        } else if (home_) {
            const double left = -sc.s;
            const double v = std::min(maxSpeed * 0.6, 0.3 + 2.5 * std::fabs(left));
            sc.s += std::clamp(left, -v * dt, v * dt);
            if (std::fabs(sc.s) < 1e-4) {
                sc.s = 0.0;
                home_ = false;
            }
        } else {
            if (in.scroll != 0.0f && inView) {
                pending_ -= in.scroll * step;   // away from the user: in (smaller)
                home_ = false;
            }
            if (pending_ != 0.0) {
                double ds = pending_ * (1.0 - std::exp(-dt * 12.0));
                if (std::fabs(pending_) < 2e-4) ds = pending_;
                ds = std::clamp(ds, -maxSpeed * dt, maxSpeed * dt);
                const double before = sc.s;
                sc.s = std::clamp(sc.s + ds, sc.sMin, sc.sMax);
                pending_ -= ds;
                if (sc.s == before || std::fabs(pending_) < 1e-6) pending_ = (sc.s == before) ? 0.0 : pending_;
                if (std::fabs(pending_) < 1e-6) pending_ = 0.0;
            }
        }
        sc.advance(dt, sPrev);
        a_.render();
        cudaMemcpy(pinned_, a_.output, size_t(sc.width) * sc.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
    }

    const void* image(int& w, int& h) override {
        w = a_.sc.width;
        h = a_.sc.height;
        return pinned_;
    }

    void status(app::Status& s) override {
        using theme::num;
        {
            app::Control c;
            c.kind = app::Control::Kind::Choice;
            c.id = "secenek";
            c.title = L("Seçenek", "Option");
            c.items = {L("Ölçek", "Scale"), L("Hız", "Speed")};
            c.active = mode_;
            s.controls.push_back(c);
        }
        if (mode_ == 1) {
            hizStatus(s);
            return;
        }
        if (tour_) s.chips.push_back({L("Tur", "Tour"), true});
        s.metrics.push_back({L("Bakılan", "Looking at"), a_.sc.lookingAt()});
        s.metrics.push_back({L("Ekranın genişliği", "The screen's width"), a_.sc.widthText()});
        s.metrics.push_back({L("Kare", "Frame"), num(a_.frameMs, 1) + " ms"});
        app::Control c;
        c.kind = app::Control::Kind::Button;
        c.id = "tur";
        c.text = tour_ ? L("Turu durdur", "Stop the tour") : L("Tur", "The tour");
        c.keys = "T";
        c.strong = !tour_;
        c.section = true;
        c.detail = L("Evrenin kenarına, geri sahile, Planck'a, geri sahile", "To the edge of the universe, back to the beach, to Planck, back");
        s.controls.push_back(c);
    }

    void screenshot(app::Host& host) override {
        a_.save(host.capturePath());
        host.toast(L("Ekran görüntüsü kaydedildi", "Screenshot saved"));
    }

    void reload(app::Host& host) override {
        const double s = a_.sc.s;
        const olcek::Clocks c = a_.sc.clk;
        const olcek::Scale::Hiz hz = a_.sc.hiz;
        a_.loadConfig();
        a_.sc.init();
        a_.sc.s = std::clamp(s, a_.sc.sMin, a_.sc.sMax);
        a_.sc.clk = c;
        a_.sc.hiz = hz;
        if (mode_ == 1) hizRender(0.0);
        else a_.render();
        host.toast(L("Ayarlar yeniden yüklendi", "Settings reloaded"));
    }

    void unload() override {
        if (loaded_) persist();
        a_.release();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }
    ~OlcekLive() override { unload(); }

    // the second option's graphs over the picture (both views), under the application's panels
    void overlay(const app::Overlay& o) override {
        if (mode_ != 1 || !graphs_) return;
        const hz::Ship& s = a_.ship;
        hz::Pano p;
        p.ship = &s;
        p.route = &a_.route;
        p.bb = &a_.bb;
        p.k = hz::kin(s.phi);
        // the Earth: "now" in the rocket's frame (its simultaneity), and the Earth seen (its light, delayed, Doppler-shifted)
        const olcek::V3 pos = hz::position(s, a_.route);
        const olcek::V3 rel = pos - a_.route.earth;
        const double d = olcek::length(rel);
        const double t = s.t.value();
        const olcek::V3 m = hz::motionDir(s, a_.route);
        p.earthDist = d;
        p.earthNow = s.phi > 0.0 ? t - p.k.beta * olcek::dot(rel, m) / hz::kC : t;
        p.earthSeen = t - d / hz::kC;
        double D = 1.0;
        if (s.phi > 0.0 && d > 0.0) hz::aberrateDirFast(rel * (-1.0 / d), m, p.k, std::exp(-s.phi), D);
        p.earthSeenRate = D;
        p.dt = lastDtau_;
        p.now = o.now;
        hz::drawPanels(p, pano_, o.view, o.scale, hz::kPanoAll, o.panels);
    }

private:

    // ---- the second option: Hız ---------------------------------------------------------------------------------------

    void enterHiz() {
        olcekClk_ = a_.sc.clk;
        olcekS_ = a_.sc.s;
        mode_ = 1;
        a_.sc.hiz.on = true;
        lastNow_ = -1.0;
        evInit_ = false;
        persist();
    }
    void leaveHiz() {
        mode_ = 0;
        persist();
        a_.sc.hizOff();
        a_.sc.clk = olcekClk_;
        a_.sc.s = olcekS_;
    }

    void hizFrame(app::Host& host, const app::Input& in) {
        // the passenger's clock is the screen's: the scene's own time, not the application's per-frame clamp (a hitch
        // still counts, up to two seconds; closed, nothing counts)
        const double dtau = lastNow_ < 0.0 ? 0.0 : std::clamp(in.now - lastNow_, 0.0, 2.0);
        lastNow_ = in.now;
        hz::Command cmd;
        cmd.thrust = in.isDown(GLFW_KEY_W);
        cmd.brake = in.isDown(GLFW_KEY_S);
        cmd.thrustPressed = in.pressed(GLFW_KEY_W);
        cmd.brakePressed = in.pressed(GLFW_KEY_S);
        cmd.toggleAuto = in.pressed(GLFW_KEY_SPACE);
        bool reset = false;
        for (const app::ControlEvent& e : in.events) {
            if (e.id == "oto") cmd.toggleAuto = true;
            if (e.id == "goruntu" && e.item >= 0 && e.item != view_) {
                view_ = e.item;
                evInit_ = false;
            }
            if (e.id == "sifirla") {
                if (in.now < resetArmed_) reset = true;
                else {
                    resetArmed_ = in.now + 4.0;
                    host.toast(L("Yolculuk baştan başlasın mı? Onaylamak için bir daha bas", "Start the journey over? Press again to confirm"));
                }
            }
        }
        if (in.pressed(GLFW_KEY_TAB)) {
            view_ ^= 1;
            evInit_ = false;   // the eye adapted at once when it comes back (no fade from a stale exposure)
        }
        for (const app::ControlEvent& e : in.events)
            if (e.id == "grafik" && e.item >= 0) graphs_ = e.item == 0;
        if (in.pressed(GLFW_KEY_G)) graphs_ = !graphs_;
        if (reset) {
            a_.ship = hz::depart(a_.route);
            pano_ = hz::PanoState{};
            resetArmed_ = -1.0;
            yaw_ = pitch_ = 0.0;
            evInit_ = false;
            host.toast(L("Yolculuk baştan: Dünya'nın yanındasın", "The journey starts over: you are beside the Earth"));
        }
        const int legBefore = a_.ship.leg;
        const hz::Events ev = hz::step(a_.ship, a_.route, dtau, cmd);
        notices(host, ev, legBefore);
        if (a_.ship.leg != legBefore) pano_ = hz::PanoState{};   // a new leg: the graphs' axes afresh
        lastDtau_ = dtau;

        // the view's own input
        const bool inView = in.view.w > 0.0f && in.view.contains(in.mouseX, in.mouseY);
        const double wheelStep = std::log10(double(a_.cfg.f("wheel", 1.5f)));
        if (view_ == 0) {
            if (in.scroll != 0.0f && inView) zoomPending_ -= in.scroll * wheelStep;
            if (in.leftDown && inView && in.view.w > 0.0f) {
                // the sky follows the cursor
                const double perPx = 2.0 * std::pow(10.0, zoom_) / in.view.w;
                yaw_ = std::remainder(yaw_ + in.mouseDX * perPx, 2.0 * kPi);
                pitch_ = std::clamp(pitch_ + in.mouseDY * perPx, -1.55, 1.55);
                lookAhead_ = false;
            }
            if (in.pressed(GLFW_KEY_R)) lookAhead_ = true;
        } else {
            if (in.scroll != 0.0f && inView) sidePending_ -= in.scroll * wheelStep;
            if (in.pressed(GLFW_KEY_R)) sidePending_ = sideDefault() - sideS_;
        }
        hizRender(dtau);
        cudaMemcpy(pinned_, a_.output, size_t(a_.sc.width) * a_.sc.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
        if (in.now >= saveAt_ || ev.arrived) {
            persist();
            saveAt_ = in.now + 2.0;
        }
    }

    static double sideDefault() { return 1.3; }

    void notices(app::Host& host, const hz::Events& ev, int legBefore) {
        const hz::Ship& s = a_.ship;
        if (ev.autoOn)
            host.toast(L("Otomatik varış: en kısa yoldan (gerekirse hızlanır), doğru anda frenleyip tam hedefte durur",
                         "Automatic arrival: the quickest way (speeding up if it helps), braking at the right moment, stopping right at the target"));
        if (ev.captured)
            host.toast(L("Hedefin yakınında durdun: son kısmı otomatik varış uçuyor", "You stopped near the target: the automatic arrival flies the last stretch"));
        (void)s;
        if (ev.autoOff) host.toast(L("Otomatik varış kapandı", "Automatic arrival off"));
        if (ev.autoCancelled) host.toast(L("Otomatik varış kapandı: kumanda sende", "Automatic arrival off: you are flying"));
        if (ev.overshoot && !overshootShown_)   // (reported every step of that brake: told once)
            host.toast(L("Bu hızla durulamaz: geçip duracak, dönüp gelecek", "Too fast to stop: it will pass, stop and come back"));
        overshootShown_ = ev.overshoot;
        if (ev.capReached)
            host.toast(L("Hız tavanı: 21 dokuz. Işık hızına bundan fazla yaklaşmıyoruz (tasarım kararı)",
                         "The speed cap: 21 nines. We go no closer to the speed of light (a design choice)"));
        if (ev.arrived) {
            const std::string where = ev.arrivedLeg == hz::kLegProxima ? L("Proxima Centauri'ye vardın", "You have reached Proxima Centauri")
                                                                       : L("Andromeda'ya vardın", "You have reached Andromeda");
            host.toast(where + L(": senin saatinle ", ": by your clock ") + clockText(ev.legTau) + L(", Dünya'da ", ", on the Earth ") +
                       spanText(ev.legEarth) + L(" geçti", " passed"));
            if (s.leg == hz::kLegAndromeda && legBefore == hz::kLegProxima && ev.turnStarted)
                host.toast(L("Roket Andromeda'ya dönüyor (", "The rocket turns towards Andromeda (") + theme::num(ev.turnDeg, 0) + "°)");
        }
    }

    // a frame of the second option (dtau: the passenger's seconds since the last one)
    void hizRender(double dtau) {
        hz::Ship& s = a_.ship;
        // the eye's zoom and head, the side's scale: eased like the first option's wheel
        const double zMin = std::log10(std::tan(0.5e-11)), zMax = std::log10(std::tan(60.0 * kPi / 180.0));
        const double ease = 1.0 - std::exp(-std::max(dtau, 1.0 / 240.0) * 12.0);
        if (zoomPending_ != 0.0) {
            double d = std::fabs(zoomPending_) < 2e-4 ? zoomPending_ : zoomPending_ * ease;
            const double before = zoom_;
            zoom_ = std::clamp(zoom_ + d, zMin, zMax);
            zoomPending_ = zoom_ == before ? 0.0 : zoomPending_ - d;
            if (std::fabs(zoomPending_) < 1e-6) zoomPending_ = 0.0;
        }
        if (sidePending_ != 0.0) {
            double d = std::fabs(sidePending_) < 2e-4 ? sidePending_ : sidePending_ * ease;
            const double before = sideS_;
            sideS_ = std::clamp(sideS_ + d, 1.0, 23.0);
            sidePending_ = sideS_ == before ? 0.0 : sidePending_ - d;
            if (std::fabs(sidePending_) < 1e-6) sidePending_ = 0.0;
        }
        if (lookAhead_) {
            const double k = std::exp(-std::max(dtau, 1.0 / 240.0) * 8.0);
            yaw_ *= k;
            pitch_ *= k;
            if (std::fabs(yaw_) + std::fabs(pitch_) < 1e-5) {
                yaw_ = pitch_ = 0.0;
                lookAhead_ = false;
            }
        }
        const olcek::V3 pos = hz::position(s, a_.route);
        const hz::Kin k = hz::kin(s.phi);
        if (view_ == 0) {
            hz::Eye e = a_.eyeAt(s, yaw_, pitch_, std::pow(10.0, zoom_));
            for (int pass = 0; pass < 2; ++pass) {
                e.k = std::exp2(eyeEv_) * 3.0e-3;
                a_.renderEye(e, eyeSum_);
                // the eye adapts to the light in its field, a known function of where, how fast and where it looks
                // (never read from the picture), slowly (a second or so)
                double target = a_.eyeEv(eyeSum_.visibleView / eyeSum_.viewShare);
                if (!std::isfinite(target)) target = std::isfinite(eyeEv_) ? eyeEv_ : 17.5;   // (never a NaN kept)
                if (!evInit_) {
                    eyeEv_ = target;
                    evInit_ = true;
                    continue;   // once more at the adapted exposure
                }
                eyeEv_ += (target - eyeEv_) * (1.0 - std::exp(-dtau / 1.2));
                break;
            }
        } else {
            olcek::Scale& sc = a_.sc;
            sc.hiz.on = true;
            sc.hiz.rocket = pos;
            sc.hiz.motion = s.heading;
            sc.hiz.rate = k.gamma;
            sc.hiz.gamma = k.gamma;
            sc.hiz.ev = a_.sideEv(sideS_, pos);
            sc.hiz.mark = make_float3(1.0f, 0.93f, 0.86f) * 30.0f;
            sc.s = sideS_;
            // the world from "today" at the first thrust (waiting beside the Earth, the rocket goes round with it)
            sc.hizClocks(s.departT.hi < 0.0 ? 0.0 : hz::add(s.t, hz::neg(s.departT)).value());
            a_.render();
        }
    }

    void hizStatus(app::Status& st) {
        using theme::num;
        const hz::Ship& s = a_.ship;
        const hz::Kin k = hz::kin(s.phi);
        // what it does
        std::string drive;
        switch (s.drive) {
        case hz::Drive::Thrust: drive = L("İtiş", "Thrust"); break;
        case hz::Drive::Brake: drive = L("Fren", "Braking"); break;
        case hz::Drive::Coast: drive = L("Süzülüyor", "Coasting"); break;
        case hz::Drive::Turn: drive = L("Dönüyor", "Turning"); break;
        case hz::Drive::Done: drive = L("Vardın", "Arrived"); break;
        default: drive = s.leg == hz::kLegProxima && s.legTau < 0.0 ? L("Kalkışta", "Ready") : L("Duruyor", "At rest"); break;
        }
        st.chips.push_back({drive, s.drive == hz::Drive::Thrust || s.drive == hz::Drive::Brake});
        if (s.autopilot) st.chips.push_back({L("Otomatik varış", "Automatic arrival"), true});
        const std::string legs[3] = {L("Dünya → Proxima", "Earth → Proxima"), L("Proxima → Andromeda", "Proxima → Andromeda"),
                                     L("Andromeda'dasın", "At Andromeda")};
        st.metrics.push_back({L("Yol", "Leg"), legs[std::clamp(s.leg, 0, 2)]});
        st.metrics.push_back({L("Hız", "Speed"), speedText(k)});
        if (graphs_) {   // the graphs carry the rest; the numbers they do not show
            st.metrics.push_back({L("β (ışık hızına oranı)", "β (of light's speed)"), betaText(k)});
            if (view_ == 0) st.metrics.push_back({L("Görüş açısı", "Field of view"), fovText(std::pow(10.0, zoom_))});
            else st.metrics.push_back({L("Ekranın genişliği", "The screen's width"), a_.sc.widthText()});
            st.metrics.push_back({L("Kare", "Frame"), num(a_.frameMs + float(a_.hostMs), 1) + " ms"});
        } else {
        st.metrics.push_back({L("β (ışık hızına oranı)", "β (of light's speed)"), betaText(k)});
        st.metrics.push_back({L("Dokuz", "Nines"), num(k.nines, 2)});
        st.metrics.push_back({L("γ (zaman genişlemesi)", "γ (time dilation)"), gammaText(k.gamma)});
        st.metrics.push_back({L("Hız açısı φ", "Rapidity φ"), num(s.phi, 3)});
        st.metrics.push_back({L("Senin saatin", "Your clock"), clockText(s.tau)});
        const double t = s.t.value();
        st.metrics.push_back({L("Dünya'da geçen", "Passed on the Earth"), spanText(t)});
        if (t < 1e4 * hz::kYear) st.metrics.push_back({L("Dünya'da tarih", "The date on the Earth"), dateText(t)});
        if (s.leg < hz::kLegDone) {
            const double rem = hz::remaining(s);
            st.metrics.push_back({L("Kalan (Dünya'nın ölçtüğü)", "Left (as the Earth measures)"), distText(rem)});
            st.metrics.push_back({L("Kalan (senin ölçtüğün)", "Left (as you measure)"), distText(rem / k.gamma)});
            st.metrics.push_back({L("Durma mesafesi", "Stopping distance"), distText(hz::stopDistance(s.phi))});
        }
        if (view_ == 0) st.metrics.push_back({L("Görüş açısı", "Field of view"), fovText(std::pow(10.0, zoom_))});
        else st.metrics.push_back({L("Ekranın genişliği", "The screen's width"), a_.sc.widthText()});
        st.metrics.push_back({L("Kare", "Frame"), num(a_.frameMs + float(a_.hostMs), 1) + " ms"});
        }

        app::Control gr;
        gr.kind = app::Control::Kind::Choice;
        gr.id = "grafik";
        gr.title = L("Grafikler", "Graphs");
        gr.items = {L("Açık", "On"), L("Kapalı", "Off")};
        gr.active = graphs_ ? 0 : 1;
        gr.detail = "G";
        st.controls.push_back(gr);
        app::Control v;
        v.kind = app::Control::Kind::Choice;
        v.id = "goruntu";
        v.title = L("Görüntü", "View");
        v.items = {L("Roketin gözü", "The rocket's eye"), L("Yandan", "From the side")};
        v.active = view_;
        v.detail = L("Tab", "Tab");
        st.controls.push_back(v);
        app::Control a;
        a.kind = app::Control::Kind::Button;
        a.id = "oto";
        a.section = true;
        a.text = s.autopilot ? L("Otomatik varışı kapat", "Automatic arrival off") : L("Otomatik varış", "Automatic arrival");
        a.keys = L("Boşluk", "Space");
        a.strong = !s.autopilot && s.leg < hz::kLegDone;
        a.enabled = s.leg < hz::kLegDone;
        a.detail = s.leg == hz::kLegProxima ? L("Proxima Centauri: 4,25 ışık yılı", "Proxima Centauri: 4.25 light years")
                 : s.leg == hz::kLegAndromeda ? L("Andromeda: 2,4 milyon ışık yılı", "Andromeda: 2.4 million light years")
                                              : L("Yolculuk tamamlandı", "The journey is complete");
        st.controls.push_back(a);
        app::Control r;
        r.kind = app::Control::Kind::Button;
        r.id = "sifirla";
        r.text = L("Yolculuğu baştan başlat", "Start the journey over");
        r.detail = L("İki kez bas", "Press twice");
        st.controls.push_back(r);
    }

    // ---- the journey on disk: "key = value" lines next to the project (user.cfg's neighbour, not in git)
    void persist() {
        if (savePath_.empty()) return;
        std::string out = "# pc_graphics · Ölçek / Hız: yolculuğun durumu (sahne kapanırken ve birkaç saniyede bir yazılır)\n";
        out += hz::serialize(a_.ship);
        char b[256];
        if (!std::isfinite(a_.ship.phi) || !std::isfinite(a_.ship.x.hi) || !std::isfinite(a_.ship.t.hi)) return;   // never a NaN on disk
        std::snprintf(b, sizeof(b), "hiz.goruntu = %d\nhiz.bakis = %.17g %.17g\nhiz.yakinlik = %.17g\nhiz.yan = %.17g\nhiz.grafik = %d\n", view_, yaw_,
                      pitch_, zoom_, sideS_, graphs_ ? 1 : 0);
        out += b;
        const std::string tmp = savePath_ + ".yeni";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return;
            f << out;
            if (!f) return;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, savePath_, ec);
        if (ec) {
            std::filesystem::remove(savePath_, ec);
            std::filesystem::rename(tmp, savePath_, ec);
        }
    }
    void restore() {
        loaded_ = true;
        if (savePath_.empty()) return;
        std::ifstream f(savePath_);
        if (!f) return;
        {   // the last good journey kept aside once a session (a bad write can never lose it)
            std::error_code ec;
            std::filesystem::copy_file(savePath_, savePath_ + ".yedek", std::filesystem::copy_options::overwrite_existing, ec);
        }
        std::map<std::string, std::string> kv;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            auto trim = [](std::string x) {
                const size_t a = x.find_first_not_of(" \t\r"), b = x.find_last_not_of(" \t\r");
                return a == std::string::npos ? std::string() : x.substr(a, b - a + 1);
            };
            kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
        }
        hz::Ship s;
        if (hz::parse(s, kv)) a_.ship = s;
        auto num = [&](const char* key, int n, double* v) {
            const auto it = kv.find(key);
            if (it == kv.end()) return false;
            std::istringstream ss(it->second);
            for (int i = 0; i < n; ++i)
                if (!(ss >> v[i]) || !std::isfinite(v[i])) return false;
            return true;
        };
        double v[2];
        if (num("hiz.goruntu", 1, v)) view_ = std::clamp(int(v[0]), 0, 1);
        if (num("hiz.bakis", 2, v)) {
            yaw_ = std::remainder(v[0], 2.0 * kPi);
            pitch_ = std::clamp(v[1], -1.55, 1.55);
        }
        if (num("hiz.yakinlik", 1, v)) zoom_ = std::clamp(v[0], std::log10(std::tan(0.5e-11)), std::log10(std::tan(60.0 * kPi / 180.0)));
        if (num("hiz.yan", 1, v)) sideS_ = std::clamp(v[0], 1.0, 23.0);
        if (num("hiz.grafik", 1, v)) graphs_ = v[0] != 0.0;
        // (the scene always opens on its first option: the journey waits in the second, where it was left)
    }

    app::SceneInfo info_ = describe();
    OlcekApp a_;
    uchar4* pinned_ = nullptr;
    bool paced_ = false;
    double pending_ = 0.0;   // decades still to go (the wheel's)
    bool home_ = false;      // R: on the way back to the beach
    bool tour_ = false;
    int leg_ = 0;
    double hold_ = 0.0;
    // the second option
    int mode_ = 0;                      // 0 Ölçek (the address), 1 Hız
    int view_ = 0;                      // 0 the rocket's eye, 1 from the side
    bool loaded_ = false;
    std::string savePath_;
    double lastNow_ = -1.0;             // the scene's clock at the last frame of the journey
    double saveAt_ = 0.0;
    double resetArmed_ = -1.0;          // "start over" pressed once: until when a second press confirms
    double yaw_ = 0.0, pitch_ = 0.0;    // the head (radians; the sky follows a drag)
    bool lookAhead_ = false;            // R: the head turning back to the bow
    bool overshootShown_ = false;       // the "too fast to stop" notice given (once per such brake)
    bool graphs_ = true;                // the five panels of graphs (G)
    hz::PanoState pano_;
    double lastDtau_ = 0.0;
    double zoom_ = std::log10(std::tan(37.5 * kPi / 180.0));   // log10 tan(fov/2): 75 degrees across
    double zoomPending_ = 0.0;
    double sideS_ = 1.3, sidePending_ = 0.0;   // the side view's screen width (log10 m)
    double eyeEv_ = 17.5;
    bool evInit_ = false;
    hz::EyeSum eyeSum_;
    olcek::Clocks olcekClk_;            // the first option's own clocks and scale while the second runs
    double olcekS_ = 0.0;
};

}   // namespace

namespace olcek {
app::SceneInfo info() { return OlcekLive::describe(); }
std::unique_ptr<app::Scene> make() { return OlcekLive::make(); }
int cli(int argc, char** argv) { return runOlcekCli(argc, argv); }
}   // namespace olcek
