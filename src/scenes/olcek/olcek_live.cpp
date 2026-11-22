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
#include "scenes/olcek/olcek_hiz_yan.h"

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
        i.subtitle = L("Bir sahilden evrenin kenarına ve Planck boyuna", "From a beach to the universe's edge and Planck");
        i.description = L("İzmir'de bir sahilde ayağının ucundaki kumdan başlarsın. Yalnız tekerlekle dışarı, Samanyolu'na ve kozmik ağa, "
                          "ya da içeri, atoma, kuarka ve Planck uzunluğuna gidersin; boylar ve uzaklıklar gerçektir. Hız "
                          "seçeneğinde roketle Proxima'ya ve Andromeda'ya uçarsın.",
                          "You start at the sand by your feet on a beach in Izmir. With the wheel alone you go out to the Milky Way "
                          "and the cosmic web, or in to an atom, a quark and the Planck length, at true sizes and distances. In the "
                          "Speed option you fly a rocket to Proxima and Andromeda.");
        i.details = {
            {L("62 mertebe, tek sayı türü", "62 orders of magnitude, doubles"),
             L("Ekranın genişliği 1,6 × 10⁻³⁵ m'den (Planck uzunluğu) 1,8 × 10²⁷ m'ye kadar değişir; kamera tek bir doğru "
               "üzerinde gider gelir. Mutlak konum tutulmaz: Laniakea'dan Güneş'e, Dünya'ya, sahile, bir silisyum çekirdeğine, "
               "protona ve kuarka inen bir ağaçta her şey üstündekine göre metreyle durur. Uzaklıklar yalnız ortak ataya kadar "
               "toplanır; ekranda aynı anda beş mertebe kadar göründüğünden çift duyarlıklı sayılar yeter.",
               "The screen's width spans 1.6 × 10⁻³⁵ m (the Planck length) to 1.8 × 10²⁷ m; the camera only moves along one "
               "line. No absolute position is stored: in a tree from Laniakea through the Sun, the Earth, the beach, a silicon "
               "nucleus and a proton to a quark, everything sits in metres from its parent. Distances add up only to the "
               "common ancestor, and with about five orders of magnitude on screen at once, doubles suffice.")},
            {L("Sabit örnekler, gözün kapağı", "Fixed samples, the eye's shutter"),
             L("Her piksel her karede aynı dört sabit örnekle çizilir ve örnekler pikselden biraz geniş bir süzgeçle birleşir; "
               "önceki karelerden bir şey taşınmaz, görüntü kıpırdamaz. Pikselden küçülen bir cisim ışığını koruyarak noktaya "
               "döner. Gözün 0,15 sn'lik kapağından hızlı olan zaman ortalamasıyla çizilir: hızlı dönen bir yörünge halka, hızlı "
               "titreşen bir sicim kenarları parlak bir bant olur.",
               "Every pixel is drawn from the same four fixed samples every frame, combined with a filter slightly wider than a "
               "pixel; nothing is carried over between frames, so nothing shimmers. A body below a pixel becomes a point that "
               "keeps its light. What is faster than the eye's 0.15 s shutter is drawn as its time average: a fast orbit becomes "
               "a ring, a vibrating string a band with bright edges.")},
            {L("Tek bir an: 5 Ekim 2026 öğlesi", "One moment: noon, 5 October 2026"),
             L("Gezegenler, Ay, Güneş ve Samanyolu'nun merkezi, 5 Ekim 2026'da Çandarlı'da (Dikili) güneş öğlesindeki gerçek "
               "yerlerindedir (JPL DE440). Zaman her ölçekte kendi hızında akar: Dünya döner, gezegenler yörüngelerinde ilerler. "
               "Sahile dönerken saatler o ana geri sarılır, her seferinde aynı öğleye varırsın.",
               "The planets, the Moon, the Sun and the Galaxy's centre stand where they were at solar noon at Çandarlı (Dikili) "
               "on 5 October 2026 (JPL DE440). Time runs at its own pace at every scale: the Earth turns, the planets orbit. On "
               "the way back the clocks wind back to that moment, so the beach is always at the same noon.")},
            {L("Ölçümlerden gelen dünya", "The world from measurements"),
             L("Kumsal ve kıyı Sentinel-2'den (10 m), Dünya o günlerin MODIS ve VIIRS bulutlarıyla Blue Marble'dan, Ay LRO'dan. "
               "Asteroit ve Kuiper kuşakları Minor Planet Center'ın 1,57 milyon yörüngesinden, 109.400 yıldız HYG'den, "
               "Samanyolu'nun kolları Reid 2019'dan, 43.490 galaksi 2MRS'ten; M31 ve M33 gerçek yönelimlerinde. Kataloğun "
               "ötesindeki kozmik ağ, gerçek istatistikten üretilmiş bir örnektir.",
               "Beach and shore from Sentinel-2 (10 m), the Earth from Blue Marble with those days' MODIS and VIIRS clouds, the "
               "Moon from LRO. Asteroid and Kuiper belts from 1.57 million Minor Planet Center orbits, 109,400 stars "
               "from HYG, the Milky Way's arms from Reid 2019, 43,490 galaxies from 2MRS, M31 and M33 in true orientation. Beyond "
               "it the cosmic web is a sample with real statistics.")},
            {L("Işığın dalga boyunun altında", "Below the wavelength of light"),
             L("Renk burada biter: önce mikroskopların gri görüntüleri, atomdan aşağısı temsili renkli parçacıklar gelir, sayılar "
               "gerçektir. Elektronlar kabuklarının dağılımında noktalardır; sonra silisyum-28 çekirdeğinin 28 nükleonu, protonun "
               "renk yüklü üç kuarkı ve gluon akısı. Kuarkın çevresinde her ölçekte sanal çiftler halka çizer; en dipte Planck "
               "boyunda köpük ve bir sicim vardır, ikisi de kuramdır.",
               "Here colour ends: first microscopes' grey images, then, below the atom, particles in representational "
               "colours with true numbers. Electrons are points spread like their shells; then the 28 nucleons of a silicon-28 "
               "nucleus, the proton's three colour-charged quarks and their gluon flux. Virtual pairs loop around the quark at "
               "every scale; at the bottom lie Planck-scale foam and a string, both theory.")},
            {L("İkinci seçenek: Hız", "Second option: Speed"),
             L("Roketle Proxima'ya (senin saatinle 15 dk, Dünya'da 4,25 yıl) ya da Andromeda'ya (72 dk, 2,4 milyon yıl) gidersin; "
               "ekranın saati senin saatindir, özel görelilik tam formüllerle hesaplanır. Roketin gözünde sapınç göğü öne "
               "toplar, Doppler ışığı kaydırır; ışık hızının %99,999'unda kozmik fon önde kırmızı bir güneş gibi doğar. Samanyolu "
               "ESO/S. Brunier (CC BY 4.0), Proxima Gaia DR3, Alfa Erboğa Akeson 2021.",
               "You fly to Proxima (15 min on your clock, 4.25 years on the Earth) or Andromeda (72 min, 2.4 million years); the "
               "screen's clock is yours, and special relativity is exact. In the rocket's eye aberration gathers the sky ahead "
               "and Doppler shifts its light; at 99.999% of light speed the cosmic background rises ahead as a red sun. Milky Way ESO/S. Brunier (CC BY 4.0), Proxima Gaia DR3, Alpha Centauri "
               "Akeson 2021.")}};
        i.tags = {L("62 mertebe", "62 orders of magnitude"), L("Gerçek konumlar", "True positions"),
                  L("Özel görelilik", "Special relativity")};
        i.image = "assets/gallery/olcek.jpg";
        i.accent = 0xE08BAE;
        i.keys = {{L("Ölçek", "Scale"),
                   {{L("Tekerlek", "Wheel"), L("Uzaklaş / yaklaş", "Zoom out / in")},
                    {"R", L("Sahile dön", "Back to the beach")},
                    {"T", L("Turu başlat / durdur", "Start / stop the tour")}}},
                  {L("Hız · uçuş", "Speed · flight"),
                   {{"W", L("Basılı tutup hızlan (1 g'den artarak)", "Hold to thrust (from 1 g, rising)")},
                    {"S", L("Basılı tutup frenle", "Hold to brake")},
                    {L("Boşluk", "Space"), L("Hedefe otomatik varış aç / kapat", "Auto-arrival at target on / off")}}},
                  {L("Hız · görüntü", "Speed · view"),
                   {{"Tab", L("Roketin gözü / yandan bakış", "Rocket's eye / side view")},
                    {"G", L("Grafikler aç / kapat", "Graphs on / off")},
                    {"E", L("Enerji gözü aç / kapat", "Energy eye on / off")},
                    {L("Sol tık", "Left click"), L("Basılı tutup sürükle, etrafa bak", "Hold and drag to look around")},
                    {L("Tekerlek", "Wheel"), L("Gözde yakınlaş, yandan ölçek değiştir", "Zoom (eye) / change scale (side)")},
                    {"1|2|3|4|5|6", L("Yandan ölçek: roketten Yerel Grup'a", "Side scales: rocket to Local Group")},
                    {"R", L("Öne bak / rokete dön", "Look ahead / back to the rocket")}}}};
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
            testing_ = host.testing();
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
                std::string note = mode_ == 1 ? L("Hız: W itiş, S fren, Boşluk otomatik varış, Tab görüntü", "Speed: W thrust, S brake, Space automatic arrival, Tab view")
                                              : L("Ölçek: tekerlekle uzaklaş, yaklaş", "Scale: move out and in with the wheel");
                // the first time in: what became of the saved journey (resumed where it was left, or not continued and
                // why); a resumed one also keeps a chip on the panel until the first W, S or Space (a toast fades in 3 s)
                if (mode_ == 1 && (restoreNote_ == 1 || restoreNote_ == 5)) {
                    const hz::Ship& s = a_.ship;
                    const hz::Kin k = hz::kin(s.phi);
                    const std::string pace = s.phi <= 0.0   ? L("duruyorsun", "at rest")
                                             : k.nines < 1.0 ? "β " + theme::num(k.beta, 2)
                                                             : theme::num(k.nines, 1) + L(" dokuz", " nines");
                    note = std::string(L("Kaldığın yerden: ", "Resumed: ")) + hz::targetName(s.target) + ", " + pace +
                           L(". Panelden baştan başlatabilirsin", ". You can start over on the panel");
                    // (a journey of the two legs, carried over: the choice told once)
                    if (restoreNote_ == 5)
                        note += L(". Hedef artık panelde seçilir", ". The target is now chosen on the panel");
                    resumeChip_ = true;
                    resumeChipUntil_ = in.now + 20.0;
                } else if (mode_ == 1 && restoreNote_ >= 2) {
                    const std::string why = restoreNote_ == 2 ? L("eski bir sürümden (ivme değişti)", "from an older version (the drive changed)")
                                          : restoreNote_ == 3 ? L("daha yeni bir sürümden", "from a newer version")
                                                              : L("okunamadı", "unreadable");
                    note = L("Kayıtlı yolculuk ", "The saved journey is ") + why + L(": Dünya'nın yanından başlıyorsun",
                                                                                     ": you start beside the Earth");
                }
                if (mode_ == 1) restoreNote_ = 0;
                host.toast(note);
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
            host.toast(L("Tur: evrenin kenarına, sahile, Planck uzunluğuna, sahile. Tekerlek, R ya da T durdurur",
                         "Tour: to the universe's edge, the beach, the Planck length, the beach. Wheel, R or T stops it"));
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
        s.metrics.push_back({L("Kare süresi", "Frame time"), num(a_.frameMs, 1) + " ms"});
        app::Control c;
        c.kind = app::Control::Kind::Button;
        c.id = "tur";
        c.text = tour_ ? L("Turu durdur", "Stop the tour") : L("Tur", "Tour");
        c.keys = "T";
        c.strong = !tour_;
        c.section = true;
        c.detail = L("Evrenin kenarına, sahile, Planck uzunluğuna, sahile", "To the universe's edge, the beach, the Planck length, the beach");
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
        if (loaded_ && entered_) persist();   // (never in Hız this session: the file as it was, its notes still to tell)
        a_.release();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }
    ~OlcekLive() override { unload(); }

    // the second option's graphs over the picture (both views), under the application's panels
    void overlay(const app::Overlay& o) override {
        if (mode_ != 1) return;
        if (view_ == 1) {   // the side view's own layer: the medium, the way, the rocket (part of the picture)
            const olcek::Scale& sc = a_.sc;
            hz::SideLayer L;
            L.ship = &a_.ship;
            L.route = &a_.route;
            L.right = sc.cam.right;
            L.up = sc.cam.up;
            L.fwd = sc.cam.fwd;
            L.focus = sc.hiz.focus;
            L.D = sc.cam.D;
            L.W = sc.cam.W;
            L.tanHalfX = std::tan(0.5 * sc.fovX * kPi / 180.0);
            L.view = o.view;
            L.dt = lastDtau_;
            hz::drawSide(L, side_, std::min(o.scale, o.view.h / 1440.0f));
        }
        // the eye's labels last, clear of every panel: the application's, the energy key and the graphs (one under a graph's
        // glass was half read)
        std::vector<ui::Rect> taken = o.panels;
        if (view_ == 0 && energy_ && o.hud) hz::drawEnergyKey(o.view, o.scale, a_.ship.phi, a_.bb, o.panels, &taken);
        if (o.hud && graphs_) graphPanels(o, taken);
        if (view_ == 0 && o.hud) eyeLabels(o, taken);
    }

    void graphPanels(const app::Overlay& o, std::vector<ui::Rect>& taken) {
        const hz::Ship& s = a_.ship;
        hz::Pano p;
        p.ship = &s;
        p.route = &a_.route;
        p.bb = &a_.bb;
        p.k = hz::kin(s.phi);
        // the Earth: "now" in the rocket's frame (its simultaneity), and the Earth seen (its light, delayed, Doppler-shifted)
        const olcek::V3 pos = hz::position(s);
        const olcek::V3 rel = pos - a_.route.earth;
        const double d = olcek::length(rel);
        const double t = s.t.value();
        const olcek::V3 m = hz::motionDir(s);
        p.earthDist = d;
        p.earthNow = s.phi > 0.0 ? t - p.k.beta * olcek::dot(rel, m) / hz::kC : t;
        p.earthSeen = t - d / hz::kC;
        double D = 1.0;
        if (s.phi > 0.0 && d > 0.0) hz::aberrateDirFast(rel * (-1.0 / d), m, p.k, std::exp(-s.phi), D);
        p.earthSeenRate = D;
        p.dt = lastDtau_;
        p.now = o.now;
        hz::drawPanels(p, pano_, o.view, o.scale, hz::kPanoAll, o.panels, &taken);
    }

private:

    // ---- the second option: Hız ---------------------------------------------------------------------------------------

    void enterHiz() {
        entered_ = true;
        olcekClk_ = a_.sc.clk;
        olcekS_ = a_.sc.s;
        mode_ = 1;
        a_.sc.hiz.on = true;
        lastNow_ = -1.0;
        evInit_ = false;
        // a test run's fixture (--set hiz.test.varis=1): a minute before Proxima at 1% of c, braking on the stopping curve
        // with the automatic arrival (the arrival and the turn without the quarter hour's flight)
        if (testing_ && a_.cfg.f("hiz.test.varis", 0.0f) > 0.5f && !fixtureDone_) {
            hz::Ship& s = a_.ship;
            s = hz::depart(a_.route);
            s.phi = std::atanh(0.01);
            s.x = hz::dd(-hz::stopDistance(s.phi));
            s.autopilot = true;
            s.drive = hz::Drive::Brake;
            s.legTau = 0.0;
            s.legT = hz::dd(0.0);
            s.departT = hz::dd(0.0);
            fixtureDone_ = true;
        }
        // ... or (--set hiz.test.phi=v) coasting at rapidity v two fifths along the first leg (the highest speeds' views)
        if (testing_ && a_.cfg.f("hiz.test.phi", 0.0f) > 0.0f && !fixtureDone_) {
            hz::Ship& s = a_.ship;
            s = hz::depart(a_.route);
            s.phi = std::min(double(a_.cfg.f("hiz.test.phi", 0.0f)), hz::phiCap());
            s.x = hz::dd(-0.6 * a_.route.length[0]);
            s.drive = hz::Drive::Coast;
            s.legTau = 0.0;
            s.legT = hz::dd(0.0);
            s.departT = hz::dd(0.0);
            fixtureDone_ = true;
        }
        // ... or (--set hiz.test.kayit=path) a journey's file as it was, in the eye (Özcan's of 2026-10-06: 21 nines,
        // 156,000 ly past Proxima, the widest view; its labels ran across the screen)
        if (testing_ && !fixtureDone_) {
            if (const auto it = a_.cfg.values.find("hiz.test.kayit"); it != a_.cfg.values.end()) {
                if (!readJourney(it->second, false)) std::printf("autotest: hiz.test.kayit okunamadi: %s\n", it->second.c_str());
                view_ = 0;
                fixtureDone_ = true;
            }
        }
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
            if (e.id == "hedef" && e.item >= 0) cmd.choose = e.item;
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
        for (const app::ControlEvent& e : in.events)
            if (e.id == "enerji" && e.item >= 0) energy_ = e.item == 0;
        if (in.pressed(GLFW_KEY_E)) energy_ = !energy_;
        if (reset) {
            a_.ship = hz::depart(a_.route);
            pano_ = hz::PanoState{};
            resetArmed_ = -1.0;
            yaw_ = pitch_ = 0.0;
            evInit_ = false;
            host.toast(L("Yolculuk baştan: Dünya'nın yanındasın", "The journey starts over: you are beside the Earth"));
        }
        if (resumeChip_ && (in.now > resumeChipUntil_ || cmd.thrustPressed || cmd.brakePressed || cmd.toggleAuto || reset)) resumeChip_ = false;
        const hz::Events ev = hz::step(a_.ship, a_.route, dtau, cmd);
        notices(host, ev);
        if (ev.chosen || ev.arrived) pano_ = hz::PanoState{};   // a new trip: the graphs' axes afresh
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
            if (in.scroll != 0.0f && inView) {
                sidePending_ -= in.scroll * wheelStep;
                sideSlow_ = false;
            }
            if (in.pressed(GLFW_KEY_R)) {
                sidePending_ = sideDefault() - sideS_;
                sideSlow_ = false;
            }
            // the scale's stops (PLAN §3.2): a slow, even zoom there (the same decades per second whatever the jump)
            for (int i = 0; i < 6; ++i)
                if (in.pressed(GLFW_KEY_1 + i)) {
                    sidePending_ = kSideStops[i] - sideS_;
                    sideSlow_ = true;
                }
        }
        hizRender(dtau);
        cudaMemcpy(pinned_, a_.output, size_t(a_.sc.width) * a_.sc.height * sizeof(uchar4), cudaMemcpyDeviceToHost);
        if (in.now >= saveAt_ || ev.arrived) {
            persist();
            saveAt_ = in.now + 2.0;
        }
    }

    // the side view's screen width (log10 m): the rocket (14 m) a quarter of it; and the stops of the keys 1-6: the
    // rocket, the Earth and the Moon, the Solar System, the nearby stars, the Milky Way, the Local Group (with Andromeda)
    static double sideDefault() { return 1.75; }
    static constexpr double kSideStops[6] = {1.75, 9.08, 13.08, 17.4, 21.2, 22.63};   // (the last: the leg in the graphs' gap)

    void notices(app::Host& host, const hz::Events& ev) {
        const hz::Ship& s = a_.ship;
        const std::string name = hz::targetName(s.target);
        if (ev.chosen) {   // the choice answered: where, how far, and what flies there (the physics' own rule)
            const double far = hz::toTarget(s, a_.route);
            std::string note = L("Hedef: ", "Target: ") + name + " (" + distText(far) + ")";
            // moving along a line laid for the other target, or away from this one: the autopilot stops first
            const bool stopsFirst = s.phi > 0.0 && (s.course.target != s.target || !hz::approaching(s));
            if (hz::atTarget(s)) note += L(": buradasın", ": you are here");
            else if (s.autopilot)
                note += stopsFirst ? L(". Otomatik varış önce durup dönüyor", ". Automatic arrival stops and turns first")
                                   : L(". Otomatik varış oraya gidiyor", ". Automatic arrival is heading there");
            else if (s.phi <= 0.0 && far <= hz::captureRadius(a_.route, s.target))
                note += L(". Yakınsın: son kısmı otomatik varış uçar", ". Close by: automatic arrival flies the last stretch");
            else
                note += stopsFirst ? L(". Boşluk oraya götürür (önce durup döner)", ". Space takes you there (stopping and turning first)")
                                   : L(". Boşluk oraya götürür", ". Space takes you there");
            host.toast(note);
        }
        const bool andromeda = s.target == hz::kTargetAndromeda;
        if (ev.turnStarted && s.phi <= 0.0 && !ev.arrived)
            host.toast((andromeda ? L("Roket Andromeda'ya dönüyor (", "The rocket turns towards Andromeda (")
                                  : L("Roket Proxima Centauri'ye dönüyor (", "The rocket turns towards Proxima Centauri (")) +
                       theme::num(ev.turnDeg, 0) + "°)");
        if (ev.alreadyThere)
            host.toast(andromeda ? L("Zaten Andromeda'dasın: panelden başka bir hedef seç", "Already at Andromeda: choose another target on the panel")
                                 : L("Zaten Proxima Centauri'desin: panelden başka bir hedef seç",
                                     "Already at Proxima Centauri: choose another target on the panel"));
        if (ev.autoOn)
            host.toast(L("Otomatik varış: en kısa sürede gider, doğru anda frenler, tam hedefte durur",
                         "Automatic arrival: the quickest trip, braking in time to stop right at the target"));
        if (ev.captured)
            host.toast(L("Hedefin yakınında durdun: son kısmı otomatik varış uçar", "You stopped near the target: automatic arrival flies the last stretch"));
        if (ev.autoOff) host.toast(L("Otomatik varış kapandı", "Automatic arrival off"));
        if (ev.autoCancelled) host.toast(L("Otomatik varış kapandı: kumanda sende", "Automatic arrival off: you are flying"));
        if (ev.overshoot && !overshootShown_)   // (reported every step of that brake: told once)
            host.toast(L("Bu hızla durulamaz: geçip duracak, dönüp gelecek", "Too fast to stop: it will pass, stop and come back"));
        overshootShown_ = ev.overshoot;
        if (ev.capReached)
            host.toast(L("Hız tavanı: 21 dokuz (0,999…9). Işık hızına daha çok yaklaşılmaz (tasarım kararı)",
                         "Speed cap: 21 nines (0.999…9). No closer to light speed, by design"));
        if (ev.arrived) {
            const std::string where = ev.arrivedAt == hz::kTargetProxima ? L("Proxima'ya vardın", "At Proxima")
                                                                         : L("Andromeda'ya vardın", "At Andromeda");
            std::string note = where;
            if (ev.legTau > 0.0)
                note += L(": senin saatinle ", ": ") + clockText(ev.legTau) + L(", Dünya'da ", " on your clock, ") +
                        spanText(ev.legEarth) + L(" geçti", " on the Earth");
            // what next: the other target is a choice on the panel
            note += ev.arrivedAt == hz::kTargetProxima ? L(". Andromeda'yı panelden seç", ". Pick Andromeda on the panel")
                                                       : L(". Dönüş için panelden Proxima'yı seç", ". Pick Proxima on the panel to go back");
            host.toast(note);
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
            // the wheel: eased like the first option's; a stop: at most 3.5 decades a second, easing in at the end
            const double easeSide = sideSlow_ ? std::min(1.0 - std::exp(-std::max(dtau, 1.0 / 240.0) * 2.5),
                                                         3.5 * std::max(dtau, 1.0 / 240.0) / std::max(std::fabs(sidePending_), 1e-9))
                                              : ease;
            double d = std::fabs(sidePending_) < 2e-4 ? sidePending_ : sidePending_ * easeSide;
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
        const olcek::V3 pos = hz::position(s);
        const hz::Kin k = hz::kin(s.phi);
        if (view_ == 0) {
            hz::Eye e = a_.eyeAt(s, yaw_, pitch_, std::pow(10.0, zoom_));
            e.energy = energy_;
            for (int pass = 0; pass < 2; ++pass) {
                e.k = std::exp2(eyeEv_) * 3.0e-3;
                a_.renderEye(e, eyeSum_);
                // the eye adapts to the light in its field, a known function of where, how fast and where it looks
                // (never read from the picture), slowly (a second or so); the energy eye to every band's light
                double target = energy_ ? a_.eyeEvEnergy(eyeSum_.adaptEnergy) : a_.eyeEv(eyeSum_.adaptVisible);
                if (!std::isfinite(target)) target = std::isfinite(eyeEv_) ? eyeEv_ : 17.5;   // (never a NaN kept)
                if (!evInit_) {
                    eyeEv_ = target;
                    evInit_ = true;
                    continue;   // once more at the adapted exposure
                }
                // a bright light come into the field: at once to 3 stops over, and this frame drawn again at that (no
                // white frame); then light adaptation in a fraction of a second, dark adaptation in a second or so
                if (const double dazzled = hz::eyeDazzle(eyeEv_, target); pass == 0 && dazzled < eyeEv_) {
                    eyeEv_ = dazzled;
                    continue;
                }
                eyeEv_ = hz::eyeAdapt(eyeEv_, target, dtau);
                break;
            }
        } else {
            olcek::Scale& sc = a_.sc;
            sc.hiz.on = true;
            sc.hiz.rocket = pos;
            sc.hiz.motion = s.heading;
            sc.hiz.rate = k.gamma;
            sc.hiz.shutterMove = hz::kC * k.gb * hz::kEyeShutter;
            {   // at the Local Group's scale the course's middle (the rocket crawls between its ends); nearer, the rocket
                const double tMid = -s.x.value() - 0.5 * s.course.length;
                const double u = std::clamp((sideS_ - 21.6) / 0.8, 0.0, 1.0);
                sc.hiz.focus = s.course.dir * (tMid * u * u * (3.0 - 2.0 * u));
            }
            sc.hiz.ev = a_.sideEv(sideS_, pos);
            sc.hiz.mark = make_float3(0.0f, 0.0f, 0.0f);   // (the rocket: the side layer draws it)
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
        default:
            drive = hz::atTarget(s) ? (s.at == hz::kTargetAndromeda ? L("Andromeda'da", "At Andromeda") : L("Proxima'da", "At Proxima"))
                    : s.departT.hi < 0.0 ? L("Kalkışta", "Ready")
                                         : L("Duruyor", "At rest");
            break;
        }
        st.chips.push_back({drive, s.drive == hz::Drive::Thrust || s.drive == hz::Drive::Brake});
        if (resumeChip_) st.chips.push_back({L("Kaldığın yerden", "Resumed"), true});
        if (s.autopilot) st.chips.push_back({L("Otomatik varış", "Automatic arrival"), true});
        st.metrics.push_back({L("Hedef", "Target"), hz::targetName(s.target)});
        st.metrics.push_back({L("Hız", "Speed"), speedText(k)});
        if (graphs_) {   // the graphs carry the rest; the numbers they do not show
            st.metrics.push_back({L("β (ışık hızına oranı)", "β (of light's speed)"), betaText(k)});
            if (view_ == 0) st.metrics.push_back({L("Görüş açısı", "Field of view"), fovText(std::pow(10.0, zoom_))});
            else st.metrics.push_back({L("Ekranın genişliği", "The screen's width"), a_.sc.widthText()});
            st.metrics.push_back({L("Kare süresi", "Frame time"), num(a_.frameMs + float(a_.hostMs), 1) + " ms"});
        } else {
        st.metrics.push_back({L("β (ışık hızına oranı)", "β (of light's speed)"), betaText(k)});
        st.metrics.push_back({L("Dokuz", "Nines"), num(k.nines, 2)});
        st.metrics.push_back({L("γ (zaman genişlemesi)", "γ (time dilation)"), gammaText(k.gamma)});
        st.metrics.push_back({L("Hız açısı φ", "Rapidity φ"), num(s.phi, 3)});
        st.metrics.push_back({L("Senin saatin", "Your clock"), clockText(s.tau)});
        const double t = s.t.value();
        st.metrics.push_back({L("Dünya'da geçen", "Passed on the Earth"), spanText(t)});
        if (t < 1e4 * hz::kYear) st.metrics.push_back({L("Dünya'da tarih", "The date on the Earth"), dateText(t)});
        if (!hz::atTarget(s)) {
            st.metrics.push_back({L("Kalan (Dünya'nın ölçtüğü)", "Left (as the Earth measures)"), distText(hz::toTarget(s, a_.route))});
            st.metrics.push_back({L("Kalan (senin ölçtüğün)", "Left (as you measure)"), distText(hz::toTargetMeasured(s, a_.route))});
            st.metrics.push_back({L("Durma mesafesi", "Stopping distance"), distText(hz::stopDistance(s.phi))});
        }
        if (view_ == 0) st.metrics.push_back({L("Görüş açısı", "Field of view"), fovText(std::pow(10.0, zoom_))});
        else st.metrics.push_back({L("Ekranın genişliği", "The screen's width"), a_.sc.widthText()});
        st.metrics.push_back({L("Kare süresi", "Frame time"), num(a_.frameMs + float(a_.hostMs), 1) + " ms"});
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
        if (view_ == 0) {
            app::Control en;
            en.kind = app::Control::Kind::Choice;
            en.id = "enerji";
            en.title = L("Enerji gözü", "Energy eye");
            en.items = {L("Açık", "On"), L("Kapalı", "Off")};
            en.active = energy_ ? 0 : 1;
            en.detail = "E";
            st.controls.push_back(en);
        }
        // where to (Özcan 2026-10-06: chosen at any time, Proxima at the start), and the automatic arrival there
        app::Control h;
        h.kind = app::Control::Kind::Choice;
        h.id = "hedef";
        h.section = true;
        h.title = L("Hedef", "Target");
        h.items = {"Proxima", "Andromeda"};
        h.active = s.target;
        st.controls.push_back(h);
        const bool here = hz::atTarget(s);
        app::Control a;
        a.kind = app::Control::Kind::Button;
        a.id = "oto";
        a.text = s.autopilot ? L("Otomatik varışı kapat", "Stop automatic arrival") : L("Otomatik varış", "Automatic arrival");
        a.keys = L("Boşluk", "Space");
        a.strong = !s.autopilot && !here;
        a.enabled = !here || s.autopilot;
        a.detail = here ? L("Buradasın: başka bir hedef seç", "You are here: choose another target")
                        : hz::targetName(s.target) + std::string(": ") + distText(hz::toTarget(s, a_.route));
        st.controls.push_back(a);
        app::Control r;
        r.kind = app::Control::Kind::Button;
        r.id = "sifirla";
        r.text = L("Yolculuğu baştan başlat", "Start the journey over");
        r.detail = L("Onaylamak için iki kez bas", "Press twice to confirm");
        st.controls.push_back(r);
    }

    // the eye's labels (H5): where the eye sees each named thing (aberrated as its light), the distance to it. The bodies
    // stand where renderEye placed them, each at its light's time (still behind the rocket near c). A label that would run
    // across the screen faster than it reads (over 2 to 5 pixels in a sixtieth of a second) fades out: near c the rocket's
    // own way turns the directions behind it fast (the rear point magnifies them), and when zoomed in a body's orbit at
    // its light's clock (D times the passenger's: fast ahead) jumps
    void eyeLabels(const app::Overlay& o, const std::vector<ui::Rect>& avoid) {
        const olcek::Scale& sc = a_.sc;
        const hz::Eye e = a_.eyeAt(a_.ship, yaw_, pitch_, std::pow(10.0, zoom_));
        const hz::Kin k = hz::kin(e.phi);
        const double emp = std::exp(-e.phi);
        const float S = std::min(o.scale, o.view.h / 1440.0f);
        const double dTau = 1.0 / 60.0;
        const olcek::V3 step = e.motion * (hz::kC * k.gb * dTau);   // the rocket's way in it (the rest frame's metres)
        // ... and its speed then (the drive turns the aberration: at 20 nines ~3 pixels a frame), and whether the world
        // runs yet (before the first thrust its clock stands at the departure's)
        const hz::Ship& sh = a_.ship;
        const double push = sh.drive == hz::Drive::Thrust ? 1.0 : sh.drive == hz::Drive::Brake ? -1.0 : 0.0;
        const hz::Kin k1 = hz::kin(std::max(e.phi + push * hz::driveRate(e.phi) * dTau, 0.0));
        const double emp1 = std::exp(-k1.phi);
        const bool still = sh.departT.hi < 0.0;
        // where the eye sees a thing (from the eye, rest frame) on the view (-1 .. 1 across) at a speed, and its light's D
        auto onViewAt = [&](olcek::V3 rel, const hz::Kin& kk, double empk, double& x, double& y, double* Dout) {
            const double d = olcek::length(rel);
            double D = 1.0;
            const olcek::V3 np = kk.phi > 0.0 ? hz::aberrateDirFast(rel * (1.0 / d), e.motion, kk, empk, D) : rel * (1.0 / d);
            if (Dout) *Dout = D;
            const double z = olcek::dot(np, e.fwd);
            if (!(z > 0.0)) return false;
            x = olcek::dot(np, e.right) / z / e.tanHalfX;
            y = olcek::dot(np, e.up) / z / e.tanHalfY;
            return true;
        };
        std::vector<hz::EyeLabel> labels;
        // v, a: its orbit's speed and radius (m/s, m) round what carries it (0: it stands still)
        auto at = [&](const std::string& name, olcek::V3 p, double radius, double v, double a, hz::EyeLabel* out = nullptr) {
            const olcek::V3 rel = p - e.pos;
            const double d = olcek::length(rel);
            if (!(d > radius)) return false;
            double x, y, D = 1.0;
            if (!onViewAt(rel, k, emp, x, y, &D) || std::fabs(x) > 1.0 || std::fabs(y) > 1.0) return false;
            // its way on the screen in that sixtieth (pixels): the rocket's step at its speed then, and the body's own
            // along its orbit (never more than across it) the worst way across the line of sight
            auto px = [&](olcek::V3 r2, const hz::Kin& kk, double empk) {
                double x2, y2;
                if (!onViewAt(r2, kk, empk, x2, y2, nullptr)) return 1e9;
                return std::hypot((x2 - x) * 0.5 * o.view.w, (y2 - y) * 0.5 * o.view.h);
            };
            double run = e.phi > 0.0 || k1.phi > 0.0 ? px(rel - step, k1, emp1) : 0.0;
            if (v > 0.0 && !still) {
                const double h = std::min(v * D * dTau, 2.0 * a);
                const olcek::V3 n = rel * (1.0 / d);
                const olcek::V3 u1 = olcek::normalize(olcek::cross(n, std::fabs(n.z) < 0.9 ? olcek::v3(0.0, 0.0, 1.0) : olcek::v3(1.0, 0.0, 0.0)));
                const olcek::V3 u2 = olcek::cross(n, u1);
                run += std::hypot(px(rel + u1 * h, k, emp), px(rel + u2 * h, k, emp));
            }
            hz::EyeLabel l;
            l.name = name;
            l.x = o.view.x + float((x + 1.0) * 0.5) * o.view.w;
            l.y = o.view.y + float((1.0 - y) * 0.5) * o.view.h;
            l.r = float(std::asin(std::min(1.0, radius / d)) / std::max(D, 1e-300) / (2.0 * e.tanHalfX) * o.view.w);
            l.dist = d;
            const double u = std::clamp((run - 2.0 * S) / (3.0 * S), 0.0, 1.0);
            l.alpha = float(1.0 - u * u * (3.0 - 2.0 * u));
            if (out) *out = l;
            else labels.push_back(l);
            return true;
        };
        // the Sun: from Proxima's neighbourhood a star of Cassiopeia (the sky's other side from the Earth's Proxima)
        const bool nearProxima = olcek::length(e.pos - a_.route.proxima) < 0.5 * hz::kLy;
        at(nearProxima ? L("Güneş · Kraliçe takımyıldızında", "the Sun · in Cassiopeia") : L("Güneş", "the Sun"), olcek::V3{}, 6.957e8, 0.0, 0.0);
        // (the Moon with the Earth round the Sun: its month's 1 km/s inside the year's 30)
        at(L("Dünya", "the Earth"), sc.nodeFromSun(olcek::kEarth), 6.371e6, 29.8e3, 1.496e11);
        at(L("Ay", "the Moon"), sc.nodeFromSun(olcek::kMoon), 1.7374e6, 30.8e3, 1.496e11);
        at("Proxima Centauri", a_.route.proxima, 1.0728e8, 0.0, 0.0);
        at("Proxima b", sc.nodeFromSun(olcek::kProximaB), 6.5e6, 47.2e3, 0.04857 * 1.496e11);
        if (a_.alphaCenStar[0] >= 0 && a_.alphaCenStar[1] >= 0) {   // one label while the pair is one point
            hz::EyeLabel A, B;
            const bool a = at(L("Alfa Erboğa A", "Alpha Centauri A"), sc.stars()[size_t(a_.alphaCenStar[0])], 8.5e8, 0.0, 0.0, &A);
            const bool b = at(L("Alfa Erboğa B", "Alpha Centauri B"), sc.stars()[size_t(a_.alphaCenStar[1])], 6.0e8, 0.0, 0.0, &B);
            if (a && b && std::hypot(A.x - B.x, A.y - B.y) < 40.0f * o.scale) {
                A.name = L("Alfa Erboğa A ve B", "Alpha Centauri A and B");
                A.alpha = std::min(A.alpha, B.alpha);
                labels.push_back(A);
            } else {
                if (a) labels.push_back(A);
                if (b) labels.push_back(B);
            }
        }
        at("Andromeda (M31)", sc.nodeFromSun(olcek::kM31), 30e3 * hz::kPc, 0.0, 0.0);
        at(L("Üçgen (M33)", "Triangulum (M33)"), sc.nodeFromSun(olcek::kM33), 8e3 * hz::kPc, 0.0, 0.0);
        at(L("Samanyolu'nun merkezi", "the Galaxy's centre"), sc.nodeFromSun(olcek::kGalCentre), 1.0, 0.0, 0.0);
        hz::drawEyeLabels(labels, o.view, S, avoid);
    }

    // ---- the journey on disk: "key = value" lines next to the project (user.cfg's neighbour, not in git)
    void persist() {
        if (savePath_.empty()) return;
        std::string out = "# pc_graphics · Ölçek / Hız: yolculuğun durumu (sahne kapanırken ve birkaç saniyede bir yazılır)\n";
        out += hz::serialize(a_.ship);
        char b[256];
        if (!std::isfinite(a_.ship.phi) || !std::isfinite(a_.ship.x.hi) || !std::isfinite(a_.ship.t.hi)) return;   // never a NaN on disk
        std::snprintf(b, sizeof(b), "hiz.goruntu = %d\nhiz.bakis = %.17g %.17g\nhiz.yakinlik = %.17g\nhiz.yan = %.17g\nhiz.grafik = %d\nhiz.enerji = %d\n",
                      view_, yaw_, pitch_, zoom_, sideS_, graphs_ ? 1 : 0, energy_ ? 1 : 0);
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
        readJourney(savePath_, true);
    }
    // a journey's file: the rocket and the view. own: the user's (the last good one kept aside, what became of it told).
    // false: no journey in it
    bool readJourney(const std::string& path, bool own) {
        std::ifstream f(path);
        if (!f) return false;
        if (own) {   // the last good journey kept aside once a session (a bad write can never lose it)
            std::error_code ec;
            std::filesystem::copy_file(path, path + ".yedek", std::filesystem::copy_options::overwrite_existing, ec);
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
        const bool ok = hz::parse(s, kv, a_.route);
        if (ok) {
            a_.ship = s;
            // a journey under way: said when Hız opens, kept on the panel (one of the two legs', carried over: the new
            // choice of target told with it)
            const auto ver = kv.find("hiz.surum");
            const bool carried = ver != kv.end() && std::atof(ver->second.c_str()) < double(hz::kSaveVersion);
            if (own && s.departT.hi >= 0.0) restoreNote_ = carried ? 5 : 1;
        } else if (own) {
            // not continued: a journey that had departed is told of when Hız opens (older or newer version, or damaged),
            // its file kept aside for good (the .yedek turns over every session)
            double departed = -1.0, version = 0.0;
            if (const auto it = kv.find("hiz.kalkis"); it != kv.end()) departed = std::atof(it->second.c_str());
            if (const auto it = kv.find("hiz.surum"); it != kv.end()) version = std::atof(it->second.c_str());
            if (departed >= 0.0) {
                restoreNote_ = version >= 1.0 && version < 2.0 ? 2 : version > double(hz::kSaveVersion) ? 3 : 4;   // (2 carries over)
                std::error_code ec;
                std::filesystem::copy_file(path, path + ".eski-surum", std::filesystem::copy_options::overwrite_existing, ec);
            }
        }
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
        if (num("hiz.enerji", 1, v)) energy_ = v[0] != 0.0;
        // (the scene always opens on its first option: the journey waits in the second, where it was left)
        return ok;
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
    bool loaded_ = false, entered_ = false;   // the journey read; Hız opened this session
    std::string savePath_;
    double lastNow_ = -1.0;             // the scene's clock at the last frame of the journey
    double saveAt_ = 0.0;
    double resetArmed_ = -1.0;          // "start over" pressed once: until when a second press confirms
    double yaw_ = 0.0, pitch_ = 0.0;    // the head (radians; the sky follows a drag)
    bool lookAhead_ = false;            // R: the head turning back to the bow
    bool overshootShown_ = false;       // the "too fast to stop" notice given (once per such brake)
    bool graphs_ = true;                // the five panels of graphs (G)
    int restoreNote_ = 0;               // the saved journey: 1 resumed; not continued: 2 older, 3 newer, 4 damaged (told once)
    bool resumeChip_ = false;           // "Kaldığın yerden" on the panel: until the first W, S or Space, or 20 s
    double resumeChipUntil_ = 0.0;
    bool energy_ = true;                // the energy eye (E): every band, in false colour (Özcan 2026-10-06: on at first)
    hz::PanoState pano_;
    hz::SideState side_;
    double lastDtau_ = 0.0;
    double zoom_ = std::log10(std::tan(37.5 * kPi / 180.0));   // log10 tan(fov/2): 75 degrees across
    double zoomPending_ = 0.0;
    double sideS_ = 1.75, sidePending_ = 0.0;   // the side view's screen width (log10 m)
    bool sideSlow_ = false;                     // the zoom to a stop (1-6): slow and even
    bool testing_ = false, fixtureDone_ = false;   // a test run; its fixture set once
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
