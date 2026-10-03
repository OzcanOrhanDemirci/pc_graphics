#include "app/app.h"
#include "core/gpu_stats.h"
#include "core/image_io.h"
#include "ui/gl_api.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <thread>

namespace app {

bool Input::pressed(int key) const { return std::find(pressedKeys.begin(), pressedKeys.end(), key) != pressedKeys.end(); }

namespace {

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
std::string stamp() {
    std::time_t t = std::time(nullptr);
    char b[64];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", std::localtime(&t));
    return b;
}
App* g_app = nullptr;
void keyCb(GLFWwindow*, int key, int, int action, int mods) { if (g_app) g_app->onKey(key, action, mods); }
void scrollCb(GLFWwindow*, double, double dy) { if (g_app) g_app->onScroll(dy); }
void buttonCb(GLFWwindow*, int button, int action, int) { if (g_app) g_app->onMouseButton(button, action); }

}   // namespace

// ---------------------------------------------------------------------------------------------------
void App::onKey(int key, int action, int mods) {
    mods_ = mods;
    if (action == GLFW_PRESS && key >= 0) keyQueue_.push_back(key);
}
void App::onScroll(double dy) { scroll_ += dy; }
void App::onMouseButton(int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) leftClick_ = true;
}

void App::setLanguage(Lang l) {
    setLang(l);
    if (!opt_.autotest) saveLang(opt_.root, l);   // a test run leaves the choice alone
    infos_.clear();
    for (const Entry& e : registry()) infos_.push_back(e.info());   // the descriptions, in the new language
}

void App::setHandCursor(bool on) {
    if (on == handShown_ || !win_) return;
    handShown_ = on;
    glfwSetCursor(win_, on ? handCursor_ : nullptr);
}

// The panel's controls take the mouse before the scene does: hovering (the hand), a click on a choice or a button, a
// slider's drag (it keeps the mouse until the button is let go, wherever the cursor goes; a press on the track jumps
// the handle there). A press anywhere on a panel is the panel's: the scene gets neither the click nor the held button.
// The places come from the frame drawn last.
std::vector<ControlEvent> App::panelInput(bool& clickTaken, bool& pressTaken) {
    std::vector<ControlEvent> ev;
    const float mx = float(mouseX_), my = float(mouseY_);
    const bool held = virtualLeft_ || glfwGetMouseButton(win_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool live = hud_ && !help_ && !mouseCaptured_;
    hotHit_ = -1;
    bool overPanel = false;
    if (live) {
        for (int k = 0; k < int(panelHits_.size()); ++k) {
            const Control& c = panelControls_[panelHits_[k].control];
            if (panelHits_[k].r.contains(mx, my) && (c.kind != Control::Kind::Button || c.enabled)) hotHit_ = k;
        }
        for (const ui::Rect& r : panels_) overPanel = overPanel || r.contains(mx, my);
    }
    if (leftClick_ && live) {
        if (hotHit_ >= 0) {
            const PanelHit& h = panelHits_[hotHit_];
            const Control& c = panelControls_[h.control];
            if (c.kind == Control::Kind::Slider) {
                dragControl_ = h.control;
                dragId_ = c.id;
                dragTrack_ = h.track;
            } else {
                ev.push_back({c.id, c.kind == Control::Kind::Choice ? h.item : 0, 0.0f, false});
            }
        }
        if (overPanel) pressOnPanel_ = true;
    }
    clickTaken = leftClick_ && (overPanel || !dragId_.empty());
    if (!dragId_.empty()) {
        const float v = std::min(1.0f, std::max(0.0f, (mx - dragTrack_.x) / std::max(1.0f, dragTrack_.w)));
        ev.push_back({dragId_, -1, v, !held});
        if (!held) {
            dragId_.clear();
            dragControl_ = -1;
        }
    }
    pressTaken = pressOnPanel_ || !dragId_.empty();
    if (!held) pressOnPanel_ = false;
    setHandCursor(hotHit_ >= 0 || !dragId_.empty());
    return ev;
}

bool App::consume(int key) {
    auto it = std::find(keyQueue_.begin(), keyQueue_.end(), key);
    if (it == keyQueue_.end()) return false;
    keyQueue_.erase(it);
    return true;
}

void App::toast(const std::string& text) {
    std::lock_guard<std::mutex> lock(toastMx_);
    toasts_.push_back({text, nowSec()});
    if (toasts_.size() > 3) toasts_.erase(toasts_.begin());
    std::printf("%s\n", text.c_str());
    std::fflush(stdout);
}

void App::captureMouse(bool on) {
    if (on == mouseCaptured_) return;
    mouseCaptured_ = on;
    glfwSetInputMode(win_, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(win_, GLFW_RAW_MOUSE_MOTION, on ? GLFW_TRUE : GLFW_FALSE);
    glfwGetCursorPos(win_, &mouseX_, &mouseY_);
    prevMouseX_ = mouseX_;
    prevMouseY_ = mouseY_;
}

std::string App::capturePath() {
    std::filesystem::create_directories(opt_.root + "/captures");
    const std::string id = sceneIndex_ >= 0 ? infos_[sceneIndex_].id : std::string("galeri");
    return opt_.root + "/captures/" + id + "_" + stamp();
}

void App::saveFramebuffer(const std::string& path) {
    std::vector<unsigned char> px(size_t(fbw_) * fbh_ * 4), flipped(px.size());
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, fbw_, fbh_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < fbh_; ++y) std::copy_n(&px[size_t(fbh_ - 1 - y) * fbw_ * 4], size_t(fbw_) * 4, &flipped[size_t(y) * fbw_ * 4]);
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    writePNG(path, fbw_, fbh_, flipped.data());
    std::printf("arayuz goruntusu: %s\n", path.c_str());
}

// ---------------------------------------------------------------------------------------------------
void App::openScene(int index) {
    sceneIndex_ = index;
    scene_ = registry()[index].make();
    help_ = false;
    hud_ = true;
    {
        std::lock_guard<std::mutex> lock(loadMx_);
        loadShared_ = LoadState();
        loadShared_.label = L("Hazırlanıyor", "Preparing");
    }
    mode_ = Mode::Loading;
    modeT0_ = nowSec();
    cancelLoad_ = false;
    loading_ = true;
    Scene* sc = scene_.get();
    loader_ = std::thread([this, sc] {
        LoadState st;
        while (!st.done && st.error.empty() && !cancelLoad_) {
            sc->load(*this, st);
            std::lock_guard<std::mutex> lock(loadMx_);
            loadShared_ = st;
        }
        loading_ = false;
    });
}

extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);   // winmm: 1 ms sleeps for pace()

void App::pace(float fps) {
    static bool fineTimer = false;
    if (fps > 0.0f && !fineTimer) { timeBeginPeriod(1); fineTimer = true; }
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
    const double hz = vm ? double(vm->refreshRate) : 60.0;
    const int interval = fps > 0.0f ? std::max(1, int(hz / fps + 0.5)) : 0;
    paceInterval_ = interval;
    pacePeriod_ = interval > 0 ? interval / hz : 0.0;
    glfwSwapInterval(interval > 0 ? 1 : 0);
}

void App::closeScene() {
    if (loader_.joinable()) {
        cancelLoad_ = true;
        loader_.join();
    }
    captureMouse(false);
    if (scene_) {
        scene_->unload();
        scene_.reset();
        // whatever a scene left on the GPU goes now: a clean device for the next one (it comes back on first use)
        cudaDeviceSynchronize();
        cudaDeviceReset();
        size_t freeB = 0, totalB = 0;
        cudaMemGetInfo(&freeB, &totalB);
        std::printf("sahne kapandi: ekran karti bellegi bos %.0f / %.0f MB\n", freeB / 1048576.0, totalB / 1048576.0);
    }
    ui::freeImage(frame_);
    {
        std::lock_guard<std::mutex> lock(toastMx_);
        toasts_.clear();
    }
    panelHits_.clear();
    panels_.clear();
    panelControls_.clear();
    dragId_.clear();
    dragControl_ = hotHit_ = -1;
    pressOnPanel_ = false;
    mode_ = Mode::Gallery;
    modeT0_ = nowSec();
    pacePeriod_ = 0.0;
    paceInterval_ = 0;
    glfwSwapInterval(1);
}

// ---------------------------------------------------------------------------------------------------
void App::frameGallery(double now, double dt) {
    const int n = int(infos_.size());
    auto select = [&](int i) {
        i = (i % n + n) % n;
        if (i != selected_) { prevSelected_ = selected_; selected_ = i; selChangedAt_ = now; }
    };
    if (consume(GLFW_KEY_DOWN) || consume(GLFW_KEY_S)) select(selected_ + 1);
    if (consume(GLFW_KEY_UP) || consume(GLFW_KEY_W)) select(selected_ - 1);
    for (int i = 0; i < n && i < 9; ++i)
        if (consume(GLFW_KEY_1 + i)) select(i);
    hovered_ = -1;
    const float mx = float(mouseX_), my = float(mouseY_);
    for (int i = 0; i < int(entryRects_.size()); ++i)
        if (entryRects_[i].contains(mx, my)) hovered_ = i;
    auto toggleDetails = [&] {
        details_ = !details_;
        detailsAt_ = now;
    };
    if (leftClick_) {
        if (hovered_ >= 0) select(hovered_);
        else if (openButton_.contains(mx, my)) keyQueue_.push_back(GLFW_KEY_ENTER);
        else if (detailsButton_.contains(mx, my)) toggleDetails();
        else if (langRects_[0].contains(mx, my)) setLanguage(Lang::TR);
        else if (langRects_[1].contains(mx, my)) setLanguage(Lang::EN);
    }
    if (consume(GLFW_KEY_A)) toggleDetails();
    if (consume(GLFW_KEY_L)) setLanguage(lang() == Lang::EN ? Lang::TR : Lang::EN);
    setHandCursor(hovered_ >= 0 || openButton_.contains(mx, my) || detailsButton_.contains(mx, my) || langRects_[0].contains(mx, my) ||
                  langRects_[1].contains(mx, my));
    selAnim_ += (float(selected_) - selAnim_) * float(1.0 - std::exp(-dt * 14.0));
    if (consume(GLFW_KEY_ENTER) || consume(GLFW_KEY_KP_ENTER)) { setHandCursor(false); openScene(selected_); return; }
    if (consume(GLFW_KEY_ESCAPE)) {
        if (details_) toggleDetails();   // Esc closes the details first
        else { quit_ = true; return; }
    }
    drawGallery(now);
}

void App::frameLoading(double now) {
    setHandCursor(false);
    LoadState st;
    {
        std::lock_guard<std::mutex> lock(loadMx_);
        st = loadShared_;
    }
    if (consume(GLFW_KEY_ESCAPE)) { closeScene(); return; }
    if (!loading_) {
        if (loader_.joinable()) loader_.join();
        if (!st.error.empty()) {
            failure_ = st.error;   // the scene goes (unload, device reset) when the message is dismissed
            mode_ = Mode::Failed;
            modeT0_ = now;
        } else if (st.done) {
            mode_ = Mode::Running;
            modeT0_ = now;
            sceneT0_ = sceneLast_ = now;
            keyQueue_.clear();
            glfwSwapInterval(0);
            return;
        }
    }
    drawLoading(now);
}

void App::frameRunning(double now, double dt) {
    // the application's keys first; the rest belong to the scene
    if (consume(GLFW_KEY_ESCAPE)) {
        if (help_) help_ = false;
        else { closeScene(); return; }
    }
    if (consume(GLFW_KEY_F1)) help_ = !help_;
    if (consume(GLFW_KEY_H)) hud_ = !hud_;
    const bool shot = consume(GLFW_KEY_F12);
    if (consume(GLFW_KEY_F5) && infos_[sceneIndex_].reloadable) scene_->reload(*this);

    Input in;
    bool clickTaken = false, pressTaken = false;
    in.events = panelInput(clickTaken, pressTaken);
    in.now = now - sceneT0_;
    in.dt = std::min(0.1, dt);
    in.pressedKeys = keyQueue_;
    keyQueue_.clear();
    GLFWwindow* w = win_;
    in.down = [w, this](int k) {
        return glfwGetKey(w, k) == GLFW_PRESS || std::find(virtualHeld_.begin(), virtualHeld_.end(), k) != virtualHeld_.end();
    };
    in.shift = glfwGetKey(win_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(win_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    in.ctrl = glfwGetKey(win_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(win_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    in.mouseX = float(mouseX_);
    in.mouseY = float(mouseY_);
    in.mouseDX = float(mouseX_ - prevMouseX_);
    in.mouseDY = float(mouseY_ - prevMouseY_);
    in.leftClicked = leftClick_ && !help_ && !clickTaken;
    in.leftDown = !pressTaken && (virtualLeft_ || glfwGetMouseButton(win_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    in.rightDown = virtualRight_ || glfwGetMouseButton(win_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    in.scroll = float(scroll_);
    in.view = view_;
    scene_->frame(*this, in);
    if (shot) scene_->screenshot(*this);

    int iw = 0, ih = 0;
    const void* px = scene_->image(iw, ih);
    if (px && iw > 0) ui::updateImage(frame_, px, iw, ih);

    Status st;
    scene_->status(st);
    ui::begin(fbw_, fbh_);
    ui::clear(ui::Color{0, 0, 0, 1.0f});
    if (frame_.valid()) {
        const float s = std::min(float(fbw_) / frame_.w, float(fbh_) / frame_.h);
        view_ = ui::Rect{0.5f * (fbw_ - frame_.w * s), 0.5f * (fbh_ - frame_.h * s), frame_.w * s, frame_.h * s};
        ui::image(frame_, view_);
    }
    // the first second: the picture rises out of black, the panel slides in
    const float fade = float(std::min(1.0, (now - modeT0_) / 0.6));
    if (fade < 1.0f) ui::rect(ui::Rect{0, 0, float(fbw_), float(fbh_)}, ui::hex(0x000000, 1.0f - fade * fade * (3.0f - 2.0f * fade)));
    panelHits_.clear();
    panels_.clear();
    panelControls_ = st.controls;
    if (hud_) drawHud(now, st);
    if (help_) drawHelp(now);
    drawToasts(now);
    ui::end();
}

void App::frameFailed(double now) {
    if (consume(GLFW_KEY_ESCAPE) || consume(GLFW_KEY_ENTER) || leftClick_) { closeScene(); return; }
    drawFailed(now);
}

// ---------------------------------------------------------------------------------------------------
// --autotest: the whole application once through, by the same paths as real keys; captures of the composed screen
void App::autotestStep(double now) {
    while (scriptAt_ < script_.size() && now >= scriptWaitUntil_) {
        const Step& s = script_[scriptAt_];
        if (s.kind == 3 && mode_ != Mode::Running) {
            if (mode_ == Mode::Failed) { std::printf("autotest: sahne yuklenemedi: %s\n", failure_.c_str()); quit_ = true; }
            return;
        }
        if (s.kind == 4 && mode_ != Mode::Gallery) return;
        ++scriptAt_;
        switch (s.kind) {
        case 0: scriptWaitUntil_ = now + s.seconds; return;
        case 1: keyQueue_.push_back(s.key); break;
        case 2: pendingShot_ = s.name; return;   // taken after this frame is drawn
        case 5:
            std::printf("autotest %s: %.1f fps, kare %.1f ms\n", s.name.c_str(), fps_, frameMs_);
            if (scene_) {
                Status st;
                scene_->status(st);
                for (const Metric& m : st.metrics) std::printf("    %s %s\n", m.label.c_str(), m.value.c_str());
            }
            std::fflush(stdout);
            break;
        case 6: quit_ = true; return;
        case 7: virtualMouse_ = true; virtualX_ = s.fx * fbw_; virtualY_ = s.fy * fbh_; break;
        case 8: leftClick_ = true; return;   // seen by this frame
        case 9: virtualRight_ = s.key != 0; break;
        case 13: {   // hold a key (fx 1) or let it go (fx 0)
            const auto it = std::find(virtualHeld_.begin(), virtualHeld_.end(), s.key);
            if (s.fx > 0.5f && it == virtualHeld_.end()) virtualHeld_.push_back(s.key);
            if (s.fx <= 0.5f && it != virtualHeld_.end()) virtualHeld_.erase(it);
            break;
        }
        case 10:
            virtualLeft_ = s.key != 0;
            if (virtualLeft_) leftClick_ = true;
            return;
        case 11:   // a click on a panel control by id (a choice's item: key), where it was last drawn; "@ac", "@ayrintilar":
                   // the gallery's buttons
        case 12:   // a press on a slider's track at fx (0..1); let go with a 10
            if (s.name == "@ac" || s.name == "@ayrintilar") {
                const ui::Rect& r = s.name == "@ac" ? openButton_ : detailsButton_;
                virtualMouse_ = true;
                virtualX_ = r.x + 0.5f * r.w;
                virtualY_ = r.y + 0.5f * r.h;
                leftClick_ = true;
                return;
            }
            for (const PanelHit& h : panelHits_) {
                const Control& c = panelControls_[h.control];
                if (c.id != s.name || (s.kind == 11 && c.kind == Control::Kind::Choice && h.item != s.key)) continue;
                virtualMouse_ = true;
                virtualX_ = s.kind == 12 ? h.track.x + s.fx * h.track.w : h.r.x + 0.5f * h.r.w;
                virtualY_ = h.r.y + 0.5f * h.r.h;
                leftClick_ = true;
                if (s.kind == 12) virtualLeft_ = true;
                return;
            }
            std::printf("autotest: denetim bulunamadi: %s %d\n", s.name.c_str(), s.key);
            break;
        default: break;
        }
    }
}

int App::run(const Options& opt) {
    opt_ = opt;
    g_app = this;
    setLang(opt.lang == "en" ? Lang::EN : opt.lang == "tr" ? Lang::TR : loadLang(opt.root));
    for (const Entry& e : registry()) infos_.push_back(e.info());
    if (!glfwInit()) { std::printf("glfwInit failed\n"); return 1; }
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = glfwGetVideoMode(mon);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);   // losing focus must not minimise it (a minimised window's swap can stall)
    if (opt.windowed) {
        win_ = glfwCreateWindow(1280, 720, "pc_graphics", nullptr, nullptr);
    } else {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_REFRESH_RATE, vm->refreshRate);
        win_ = glfwCreateWindow(vm->width, vm->height, "pc_graphics", mon, nullptr);
    }
    if (!win_) { std::printf("window creation failed\n"); return 1; }
    glfwMakeContextCurrent(win_);
    glfwSwapInterval(1);
    glfwSetKeyCallback(win_, keyCb);
    glfwSetScrollCallback(win_, scrollCb);
    glfwSetMouseButtonCallback(win_, buttonCb);
    handCursor_ = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    std::printf("GL_RENDERER: %s, monitor %dx%d @ %d Hz\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)), vm->width, vm->height,
                vm->refreshRate);
    std::string err;
    if (!ui::init(opt.root, err)) { std::printf("arayuz baslatilamadi: %s\n", err.c_str()); return 1; }
    {
        cudaDeviceProp prop{};
        if (cudaGetDeviceProperties(&prop, 0) == cudaSuccess) gpuName_ = prop.name;
        const std::string nv = "NVIDIA ";
        if (gpuName_.rfind(nv, 0) == 0) gpuName_ = gpuName_.substr(nv.size());
    }
    gpuStatsInit();
    for (const SceneInfo& si : infos_) {
        Picture p;
        p.sharp = ui::loadImage(opt.root + "/" + si.image, 1920);
        p.soft = ui::loadBlurred(opt.root + "/" + si.image, 480, 3);
        pictures_.push_back(p);
    }
    if (opt.autotest) {
        auto wait = [&](double s) { script_.push_back({0, s, 0, ""}); };
        auto key = [&](int k) { script_.push_back({1, 0, k, ""}); };
        auto shot = [&](const std::string& n) { script_.push_back({2, 0, 0, n}); };
        auto running = [&] { script_.push_back({3, 0, 0, ""}); };
        auto gallery = [&] { script_.push_back({4, 0, 0, ""}); };
        auto log = [&](const std::string& n) { script_.push_back({5, 0, 0, n}); };
        auto mouse = [&](float fx, float fy) { Step st{7, 0, 0, ""}; st.fx = fx; st.fy = fy; script_.push_back(st); };
        auto click = [&] { script_.push_back({8, 0, 0, ""}); };
        auto right = [&](bool down) { script_.push_back({9, 0, down ? 1 : 0, ""}); };
        auto left = [&](bool down) { script_.push_back({10, 0, down ? 1 : 0, ""}); };
        auto control = [&](const std::string& id, int item) { script_.push_back({11, 0, item, id}); };
        auto slide = [&](const std::string& id, float v) { Step st{12, 0, 0, id}; st.fx = v; script_.push_back(st); };
        auto hold = [&](int k, bool down) { Step st{13, 0, k, ""}; st.fx = down ? 1.0f : 0.0f; script_.push_back(st); };
        wait(1.5); shot("galeri_1");
        key(GLFW_KEY_DOWN); wait(0.8); shot("galeri_2");
        key(GLFW_KEY_DOWN); wait(0.8); shot("galeri_3");
        // how it is made: A opens it, the next scene's shows in its place, the button closes it
        key(GLFW_KEY_A); wait(0.6); shot("galeri_ayrintilar_3");
        key(GLFW_KEY_1); wait(0.6); shot("galeri_ayrintilar_1");
        key(GLFW_KEY_2); wait(0.6); shot("galeri_ayrintilar_2");
        control("@ayrintilar", 0); wait(0.6); shot("galeri_ayrintilar_kapandi");
        // the other language and back (L)
        key(GLFW_KEY_L); wait(0.6); shot("galeri_dil"); key(GLFW_KEY_L); wait(0.4);
        // the mouse: over the first entry, a click selects it
        mouse(0.18f, 0.43f); wait(0.5); click(); wait(0.6); shot("galeri_fare");
        // "laboratuvar/kup": a scene, of it one part
        std::string only = opt.autotestOnly, part;
        if (const size_t k = only.find('/'); k != std::string::npos) {
            part = only.substr(k + 1);
            only = only.substr(0, k);
        }
        for (int s = 0; s < int(infos_.size()); ++s) {
            if (!only.empty() && infos_[s].id != only) continue;
            key(GLFW_KEY_1 + s); wait(0.4);
            if (infos_[s].id == "yikim") { mouse(0.88f, 0.735f); wait(0.3); shot("galeri_dugme"); click(); }   // the "open" button
            else key(GLFW_KEY_ENTER);
            wait(0.25); shot(infos_[s].id + "_yukleniyor");
            running(); wait(5.0); log(infos_[s].id); shot(infos_[s].id + "_arayuz");
            key(GLFW_KEY_F1); wait(0.5); shot(infos_[s].id + "_yardim"); key(GLFW_KEY_F1);
            if (infos_[s].id == "altinsaat") { key(GLFW_KEY_SPACE); wait(6.0); log("altinsaat canli"); shot("altinsaat_canli"); }
            if (infos_[s].id == "fener") {
                key(GLFW_KEY_SPACE); wait(6.0); log("fener yolculuk"); shot("fener_yolculuk");
                key(GLFW_KEY_TAB); wait(2.0); shot("fener_oyuncu"); key(GLFW_KEY_TAB); key(GLFW_KEY_TAB);
            }
            if (infos_[s].id == "yikim") {
                // look right with the right button, then blow up a piece of the ground storey
                mouse(0.50f, 0.50f); right(true); wait(0.1);
                for (int k = 1; k <= 10; ++k) { mouse(0.50f + 0.006f * k, 0.50f); wait(0.03); }
                right(false); wait(0.3); shot("yikim_bakis_saga");
                for (int k = 9; k >= 0; --k) { mouse(0.50f + 0.006f * k, 0.50f); wait(0.03); }
                right(true); wait(0.1);
                for (int k = 1; k <= 10; ++k) { mouse(0.50f - 0.006f * k, 0.50f); wait(0.03); }
                right(false); wait(0.3);
                mouse(0.47f, 0.68f); wait(0.2); click(); wait(0.25); shot("yikim_top_yolda"); wait(1.5); shot("yikim_top");
                // the strongest throw; three clicks, the second within a second of the first: two balls go (the log says)
                slide("top", 1.0f); wait(0.1); left(false); wait(0.3); shot("yikim_top_guc");
                mouse(0.40f, 0.45f); wait(0.1); click(); wait(0.3); click(); wait(1.2); click(); wait(0.1); shot("yikim_mermi_yolda");
                wait(1.5); shot("yikim_mermi");
                slide("top", 0.52f); wait(0.1); left(false); wait(0.2);   // near the notch: back to the balanced throw
                // the panel by mouse: magnitude 7, the length to "until stopped", start, stop
                control("buyukluk", 4); wait(0.3);
                slide("sure", 1.0f); wait(0.1); left(false); wait(0.3); shot("yikim_sure_sonsuz");
                control("deprem", 0); wait(6.0); log("yikim deprem sonsuz"); shot("yikim_deprem_sonsuz");
                control("deprem", 0); wait(0.4); shot("yikim_diniyor"); wait(2.0);
                // near the notch it snaps to the magnitude's own length
                slide("sure", 0.5f); wait(0.1); left(false); wait(0.3); shot("yikim_sure_kendi");
                key(GLFW_KEY_R); wait(4.0);
                key(GLFW_KEY_SPACE); wait(9.0); log("yikim deprem 7"); shot("yikim_deprem"); key(GLFW_KEY_G); wait(1.0);
                key(GLFW_KEY_G); wait(0.1); key(GLFW_KEY_G); wait(0.1);   // AI -> raw -> back to the filter
                // the other buildings: the tower, the house (a ball at it), back to the apartment block
                control("yapi", 1); wait(10.0); log("yikim gokdelen"); shot("yikim_gokdelen");
                // the tower through magnitude 7: the world's clock (Time) against the wall's while it collapses; the
                // window takes one physics step a frame, so a slow step is a slow world (Özcan: "ağır çekimde gibi")
                control("buyukluk", 4); wait(0.3); key(GLFW_KEY_SPACE);
                wait(15.0); log("yikim gokdelen deprem 7, 15 sn");
                wait(10.0); log("yikim gokdelen deprem 7, 25 sn"); shot("yikim_gokdelen_gocme");
                wait(10.0); log("yikim gokdelen deprem 7, 35 sn");
                control("yapi", 2); wait(6.0); log("yikim ev"); shot("yikim_ev");
                mouse(0.50f, 0.50f); wait(0.1); click(); wait(2.0); shot("yikim_ev_top");
                // the seismic tower on its isolators through magnitude 8 (the base glides, the tower stands)
                control("yapi", 3); wait(8.0); log("yikim sismik"); shot("yikim_sismik");
                control("buyukluk", 5); wait(0.3); key(GLFW_KEY_SPACE); wait(14.0); log("yikim sismik deprem 8"); shot("yikim_sismik_deprem");
                key(GLFW_KEY_SPACE); wait(2.0);
                control("yapi", 0); wait(6.0);
            }
            if (infos_[s].id == "laboratuvar") {
                // every experiment: live, its trigger on the coarse world, the fine pass asked for, the recording as it
                // computes and when it plays; the panel by mouse (the time bar, the speed); slow motion on a resting
                // world; the gel shot three times ("laboratuvar/kup": only the cube, its fine pass to the end)
                if (part.empty()) {
                    wait(1.0); shot("lab_su_canli");
                    control("eylem", 0); wait(1.2); log("lab su kaba"); shot("lab_su_kaba");
                    control("ince", 0); wait(9.0); log("lab su kayit"); shot("lab_su_kayit");
                    wait(6.0); shot("lab_su_bitti");
                    slide("zaman", 0.35f); wait(0.1); left(false); control("hiz", 1); control("durdur", 0); wait(2.0); shot("lab_su_yavas");
                    key(GLFW_KEY_R); wait(1.0); shot("lab_su_kabaya_don"); control("hiz", 0);
                    control("deney", 1); wait(6.0); log("lab bal"); shot("lab_bal");
                    mouse(0.45f, 0.70f); left(true); wait(0.1);
                    for (int k = 1; k <= 20; ++k) { mouse(0.45f + 0.004f * k, 0.70f); wait(0.05); }
                    left(false); wait(3.0); shot("lab_bal_gezdi");
                    control("hiz", 2); wait(3.0); log("lab bal x1/100"); shot("lab_bal_yavas"); control("hiz", 0);
                    control("deney", 2); wait(10.0); log("lab kum"); shot("lab_kum");
                    control("deney", 3); wait(4.0); shot("lab_balon");
                    mouse(0.50f, 0.50f); wait(0.2); click(); wait(0.6); log("lab balon tik"); shot("lab_balon_kaba");   // the needle by the mouse (the log says where)
                    control("ince", 0); wait(12.0); log("lab balon kayit"); shot("lab_balon_kayit");
                    control("deney", 4); wait(4.0); shot("lab_karpuz");
                    mouse(0.50f, 0.56f); wait(0.2); click(); wait(0.6); shot("lab_karpuz_tik"); log("lab karpuz tik");
                    key(GLFW_KEY_R); wait(4.0);
                    control("eylem", 0); wait(0.6); shot("lab_karpuz_kaba"); control("ince", 0); wait(14.0); log("lab karpuz kayit"); shot("lab_karpuz_kayit");
                    control("deney", 6); wait(4.0); shot("lab_damla");
                    control("hiz", 2); wait(3.0); log("lab damla x1/100 dokunmadan"); shot("lab_damla_yavas"); control("hiz", 0);
                    control("eylem", 0); wait(0.6); shot("lab_damla_kaba"); control("ince", 0); wait(8.0); log("lab damla kayit"); shot("lab_damla_kayit");
                    control("deney", 5); wait(4.0); shot("lab_jel");
                    control("eylem", 0); wait(1.2);
                    mouse(0.47f, 0.55f); wait(0.1); click(); wait(1.2);
                    mouse(0.53f, 0.58f); wait(0.1); click(); wait(0.6); log("lab jel uc atis"); shot("lab_jel_kaba");
                    control("ince", 0); wait(45.0); log("lab jel kayit"); shot("lab_jel_kayit");
                }
                // the cube: set up, a medium blast, slow motion, the fine pass; then a finer cube
                if (part.empty() || part == "kup") {
                    control("deney", 7); wait(3.0); log("lab kup"); shot("lab_kup");
                    slide("guc", 0.38f); wait(0.1); left(false); wait(0.2);
                    control("eylem", 0); wait(0.25); shot("lab_kup_patlama"); wait(1.2); log("lab kup dagildi"); shot("lab_kup_dagildi");
                    control("hiz", 2); wait(2.0); log("lab kup x1/100"); control("hiz", 0);
                    control("ince", 0); wait(part.empty() ? 30.0 : 150.0); log("lab kup kayit"); shot("lab_kup_kayit");
                    key(GLFW_KEY_R); wait(0.5); key(GLFW_KEY_R); wait(3.0);
                    slide("detay", 0.80f); wait(0.1); left(false); wait(5.0); log("lab kup ince kup"); shot("lab_kup_ince");
                    control("eylem", 0); wait(1.5); log("lab kup ince patladi"); shot("lab_kup_ince_dagildi");
                }
                // "laboratuvar/kup1m": a million cubes through the fine pass to the end, then its burst at x1/100
                if (part == "kup1m") {
                    control("deney", 7); wait(3.0);
                    slide("detay", 1.0f); wait(0.1); left(false); wait(15.0); log("lab kup 1m"); shot("lab_kup_1m");
                    slide("guc", 0.38f); wait(0.1); left(false); wait(0.2);
                    control("eylem", 0); wait(2.0); log("lab kup 1m patladi"); shot("lab_kup_1m_patladi");
                    control("ince", 0); wait(600.0); log("lab kup 1m kayit"); shot("lab_kup_1m_kayit");
                    slide("zaman", 0.03f); wait(0.1); left(false); control("hiz", 2); wait(2.0); log("lab kup 1m x1/100"); shot("lab_kup_1m_yavas");
                    control("hiz", 0);
                }
            }
            if (infos_[s].id == "koy") {
                // swim forward, look right with the right button, the panel's knobs, the edge pixels, time paused (the
                // still picture accumulates to 256 samples a pixel)
                hold(GLFW_KEY_W, true); wait(2.5); hold(GLFW_KEY_W, false); wait(0.5); log("koy yuzdu"); shot("koy_yuzdu");
                mouse(0.50f, 0.50f); right(true); wait(0.1);
                for (int k = 1; k <= 10; ++k) { mouse(0.50f + 0.008f * k, 0.50f); wait(0.03); }
                right(false); wait(0.5); shot("koy_saga_bakti");
                control("ornek", 3); wait(1.0); log("koy 16 ornek"); shot("koy_16_ornek");
                control("ornek", 1); control("kenar", 3); wait(1.0); log("koy kenar 32"); shot("koy_kenar_32");
                control("kenar", 2); control("suzgec", 0); wait(0.6); shot("koy_kutu_suzgec"); control("suzgec", 1);
                control("doku", 0); wait(0.6); log("koy doku standart"); shot("koy_doku_standart");
                control("doku", 1); wait(0.6); log("koy doku yuksek"); shot("koy_doku_yuksek");
                control("doku", 2); wait(0.6); log("koy doku ultra"); shot("koy_doku_ultra");
                key(GLFW_KEY_E); wait(0.6); shot("koy_kenar_pikselleri"); key(GLFW_KEY_E);
                key(GLFW_KEY_P); wait(2.0); log("koy durdu"); shot("koy_durdu_birikti"); key(GLFW_KEY_P); wait(0.5);
            }
            key(GLFW_KEY_H); wait(0.3); shot(infos_[s].id + "_arayuzsuz"); key(GLFW_KEY_H);
            key(GLFW_KEY_ESCAPE); gallery(); wait(0.8);
        }
        shot("galeri_son");
        script_.push_back({6, 0, 0, ""});
    }
    if (!opt.startScene.empty()) {
        for (int i = 0; i < int(infos_.size()); ++i)
            if (infos_[i].id == opt.startScene) { selected_ = i; selAnim_ = float(i); openScene(i); }
    }

    double last = nowSec();
    fpsT0_ = last;
    while (!glfwWindowShouldClose(win_) && !quit_) {
        glfwPollEvents();
        const double now = nowSec(), dt = now - last;
        last = now;
        if (opt.autotest) autotestStep(now);
        if (glfwGetWindowAttrib(win_, GLFW_ICONIFIED)) { glfwWaitEventsTimeout(0.05); last = nowSec(); continue; }
        glfwGetCursorPos(win_, &mouseX_, &mouseY_);
        if (virtualMouse_) { mouseX_ = virtualX_; mouseY_ = virtualY_; }
        glfwGetFramebufferSize(win_, &fbw_, &fbh_);
        S_.s = std::max(0.7f, float(fbh_) / 1440.0f);   // drawn for 1440 lines; a small window keeps readable text
        if (now - lastStats_ > 0.5) {
            const GpuStats g = gpuStatsRead();
            gpuW_ = g.watts;
            gpuC_ = float(g.tempC);
            lastStats_ = now;
        }
        const double f0 = nowSec();
        auto dispatch = [&](double step) {
            switch (mode_) {
            case Mode::Gallery: frameGallery(now, step); break;
            case Mode::Loading: frameLoading(now); break;
            case Mode::Running: frameRunning(now, step); break;
            case Mode::Failed: frameFailed(now); break;
            }
        };
        const Mode before = mode_;
        dispatch(dt);
        if (mode_ != before && !quit_) dispatch(0.0);   // the new screen draws this frame (no stale picture)
        if (quit_) break;
        if (mode_ != Mode::Running) keyQueue_.clear();
        leftClick_ = false;
        scroll_ = 0.0;
        prevMouseX_ = mouseX_;
        prevMouseY_ = mouseY_;
        if (!pendingShot_.empty()) {
            saveFramebuffer(opt_.root + "/captures/arayuz/" + pendingShot_ + ".png");
            pendingShot_.clear();
        }
        frameMs_ = float((nowSec() - f0) * 1000.0);
        ++fpsFrames_;
        if (now - fpsT0_ > 0.5) { fps_ = float(fpsFrames_ / (now - fpsT0_)); fpsFrames_ = 0; fpsT0_ = now; }
        if (pacePeriod_ > 0.0 && mode_ == Mode::Running) {
            // wait until a little before the period is over (sleep, then spin the last 2 ms), then swap with vsync
            const double due = lastPresent_ + pacePeriod_ - 0.0006;
            for (double t = nowSec(); t < due; t = nowSec()) {
                if (due - t > 0.002) std::this_thread::sleep_for(std::chrono::microseconds(int((due - t - 0.0015) * 1e6)));
            }
        }
        glfwSwapBuffers(win_);
        lastPresent_ = nowSec();
    }
    closeScene();
    for (Picture& p : pictures_) { ui::freeImage(p.sharp); ui::freeImage(p.soft); }
    ui::shutdown();
    gpuStatsShutdown();
    if (handCursor_) glfwDestroyCursor(handCursor_);
    glfwDestroyWindow(win_);
    glfwTerminate();
    return 0;
}

}   // namespace app
