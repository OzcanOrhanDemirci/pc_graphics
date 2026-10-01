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

namespace app {

struct Options {
    std::string root;
    bool windowed = false;
    bool autotest = false;
    std::string startScene;               // --scene id: straight into it
    std::vector<std::string> overrides;   // --set key=value, handed to the scenes
};

class App : public Host {
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
    void drawFailed(double now);
    float keyCaps(float x, float y, const std::string& keys, float height);
    float keyHint(float x, float y, const std::string& keys, const std::string& what);
    void saveFramebuffer(const std::string& path);
    bool consume(int key);
    void autotestStep(double now);

    Options opt_;
    GLFWwindow* win_ = nullptr;
    int fbw_ = 0, fbh_ = 0;
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
    ui::Rect openButton_;
    std::vector<ui::Rect> entryRects_;

    int sceneIndex_ = -1;
    std::unique_ptr<Scene> scene_;
    double sceneT0_ = 0.0, sceneLast_ = 0.0;
    ui::Image frame_;
    ui::Rect view_;
    bool hud_ = true, help_ = false;
    std::string failure_;

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

    // timing
    double fpsT0_ = 0.0;
    int fpsFrames_ = 0;
    float fps_ = 0.0f, frameMs_ = 0.0f;
    double lastStats_ = 0.0;
    float gpuW_ = 0, gpuC_ = 0;

    // --autotest: a script of keys, waits and framebuffer captures
    struct Step {
        int kind;   // 0 wait s, 1 key, 2 capture name, 3 wait until running, 4 wait until gallery, 5 log, 6 quit
        double seconds;
        int key;
        std::string name;
    };
    std::vector<Step> script_;
    size_t scriptAt_ = 0;
    double scriptWaitUntil_ = 0.0;
    bool quit_ = false;
    std::string pendingShot_;
};

}   // namespace app
