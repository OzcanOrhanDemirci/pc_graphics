// The application: one window, the gallery of scenes, a loading screen while a scene starts (its loading runs on a
// worker thread), the scene itself under a common interface panel, help, notices, screenshots. Esc always leads
// back: from help to the scene, from the scene to the gallery, from the gallery out.
#pragma once
#include "app/scene.h"
#include "app/theme.h"
#include "ui/ui.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct GLFWwindow;
struct GLFWcursor;

namespace app {

struct Options {
    std::string root;
    bool windowed = false;
    bool autotest = false;
    std::string autotestOnly;             // --autotest-only id: the gallery steps, then only that scene
    std::string startScene;               // --scene id: straight into it
    std::string lang;                     // --lang tr|en (empty: the one remembered in user.cfg)
    std::vector<std::string> overrides;   // --set key=value, handed to the scenes
};

class App : public Host {
public:
    void pace(float fps) override;
public:
    int run(const Options& opt);

    // Host
    void toast(const std::string& text) override;
    void captureMouse(bool on) override;
    const std::string& root() const override { return opt_.root; }
    std::string capturePath() override;
    const std::vector<std::string>& overrides() const override { return opt_.overrides; }

    // called from the GLFW callbacks
    void onKey(int key, int action, int mods);
    void onScroll(double dy);
    void onMouseButton(int button, int action);

private:
    enum class Mode { Gallery, Loading, Running, Failed };
    struct Toast {
        std::string text;
        double t0;
    };
    struct Picture {
        ui::Image sharp, soft;
    };

    void openScene(int index);
    void closeScene();
    void frameGallery(double now, double dt);
    void frameLoading(double now);
    void frameRunning(double now, double dt);
    void frameFailed(double now);
    // drawing (overlay.cpp)
    void drawGallery(double now);
    void drawLoading(double now);
    void drawHud(double now, const Status& st);
    void drawHelp(double now);
    void drawToasts(double now);
    void drawBusy(double now, const Status& st);
    void drawFailed(double now);
    void drawDetails(const ui::Rect& area, float alpha);
    float keyCaps(float x, float y, const std::string& keys, float height, float alpha = 1.0f, bool dark = false);
    float keyHint(float x, float y, const std::string& keys, const std::string& what);
    void saveFramebuffer(const std::string& path);
    bool consume(int key);
    void autotestStep(double now);
    std::vector<ControlEvent> panelInput(bool& clickTaken, bool& pressTaken);   // the panel's controls, before the scene's frame
    void setHandCursor(bool on);

    Options opt_;
    GLFWwindow* win_ = nullptr;
    int fbw_ = 0, fbh_ = 0;
    int paceInterval_ = 0;
    double pacePeriod_ = 0.0, lastPresent_ = 0.0;   // pace(): the frame's period, when the last one was presented
    theme::Scale S_;
    std::vector<SceneInfo> infos_;
    std::vector<Picture> pictures_;
    std::string gpuName_;

    Mode mode_ = Mode::Gallery;
    double modeT0_ = 0.0;
    int selected_ = 0, hovered_ = -1;
    float selAnim_ = 0.0f;                // the highlight glides between entries
    double selChangedAt_ = -10.0;
    int prevSelected_ = 0;
    ui::Rect openButton_, detailsButton_;
    std::vector<ui::Rect> entryRects_;
    ui::Rect langRects_[2];               // the gallery's language switch: TR, EN
    void setLanguage(Lang l);             // the texts, the scenes' descriptions, user.cfg
    bool details_ = false;                // the gallery's "Ayrıntılar" over the picture
    double detailsAt_ = -10.0;            // when it was last opened or closed

    int sceneIndex_ = -1;
    std::unique_ptr<Scene> scene_;
    double sceneT0_ = 0.0, sceneLast_ = 0.0;
    ui::Image frame_;
    ui::Rect view_;
    bool hud_ = true, help_ = false;
    std::string failure_;
    // the panel's controls as last drawn: where each can be clicked, and the panels themselves (clicks on them are
    // not the scene's)
    struct PanelHit {
        ui::Rect r;                       // the clickable area
        ui::Rect track;                   // Slider: the track's span
        int control, item;                // item: a Choice's choice
    };
    std::vector<PanelHit> panelHits_;
    std::vector<ui::Rect> panels_;
    std::vector<Control> panelControls_;
    int hotHit_ = -1;                     // the control under the mouse
    int dragControl_ = -1;                // a slider being dragged (index into panelControls_)
    std::string dragId_;
    ui::Rect dragTrack_;
    bool pressOnPanel_ = false;           // the left button went down over a panel: the scene does not get that press
    GLFWcursor* handCursor_ = nullptr;
    bool handShown_ = false;

    // loading on a worker thread
    std::thread loader_;
    std::mutex loadMx_;
    LoadState loadShared_;
    std::atomic<bool> loading_{false}, cancelLoad_{false};

    std::mutex toastMx_;
    std::vector<Toast> toasts_;

    // input gathered by the callbacks between frames
    std::vector<int> keyQueue_;
    int mods_ = 0;
    double scroll_ = 0.0;
    bool leftClick_ = false;
    double mouseX_ = 0, mouseY_ = 0, prevMouseX_ = 0, prevMouseY_ = 0;
    bool mouseCaptured_ = false;
    bool virtualMouse_ = false, virtualRight_ = false, virtualLeft_ = false;   // --autotest drives the mouse
    std::vector<int> virtualHeld_;                                               // --autotest holds keys down
    double virtualX_ = 0.0, virtualY_ = 0.0;

    // timing
    double fpsT0_ = 0.0;
    int fpsFrames_ = 0;
    float fps_ = 0.0f, frameMs_ = 0.0f;
    double lastStats_ = 0.0;
    float gpuW_ = 0, gpuC_ = 0, gpuMemUsed_ = 0, gpuMemTotal_ = 0;
    double busyT0_ = -1.0;   // when the scene's busy card came up (< 0: none)
    float busyShown_ = 0.0f;  // its bar, eased

    // --autotest: a script of keys, waits and framebuffer captures
    struct Step {
        // 0 wait s, 1 key, 2 capture name, 3 wait until running, 4 wait until gallery, 5 log, 6 quit,
        // 7 mouse to (fx, fy) of the screen, 8 left click, 9 right button (key: 1 down, 0 up), 10 left button held
        // (key: 1 down, a press; 0 up)
        int kind;
        double seconds;
        int key;
        std::string name;
        float fx = 0.0f, fy = 0.0f;
    };
    std::vector<Step> script_;
    size_t scriptAt_ = 0;
    double scriptWaitUntil_ = 0.0;
    bool quit_ = false;
    std::string pendingShot_;
};

}   // namespace app
