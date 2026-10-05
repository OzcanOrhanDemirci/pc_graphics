// Scene 7 "Ölçek" inside the application: from a beach in Izmir out to the edge of the observable universe and in to
// the Planck length, 62 decades, with the wheel only. R goes back to the beach, T runs the tour.
#include "app/scene.h"
#include "app/theme.h"
#include "scenes/olcek/olcek_app.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>

namespace {

using app::L;

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
                    {"T", L("Tur: evrenin kenarına, geri, Planck'a, geri", "The tour: to the edge of the universe, back, to Planck, back")}}}};
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
            st.label = L("İlk kare", "The first frame");
            st.fraction = 0.9f;
            ++st.step;
            return;
        default:
            cudaMallocHost(reinterpret_cast<void**>(&pinned_), size_t(a_.sc.width) * a_.sc.height * sizeof(uchar4));
            a_.render();
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
        a_.loadConfig();
        a_.sc.init();
        a_.sc.s = std::clamp(s, a_.sc.sMin, a_.sc.sMax);
        a_.sc.clk = c;
        a_.render();
        host.toast(L("Ayarlar yeniden yüklendi", "Settings reloaded"));
    }

    void unload() override {
        a_.release();
        if (pinned_) cudaFreeHost(pinned_);
        pinned_ = nullptr;
    }
    ~OlcekLive() override { unload(); }

private:
    app::SceneInfo info_ = describe();
    OlcekApp a_;
    uchar4* pinned_ = nullptr;
    bool paced_ = false;
    double pending_ = 0.0;   // decades still to go (the wheel's)
    bool home_ = false;      // R: on the way back to the beach
    bool tour_ = false;
    int leg_ = 0;
    double hold_ = 0.0;
};

}   // namespace

namespace olcek {
app::SceneInfo info() { return OlcekLive::describe(); }
std::unique_ptr<app::Scene> make() { return OlcekLive::make(); }
int cli(int argc, char** argv) { return runOlcekCli(argc, argv); }
}   // namespace olcek
