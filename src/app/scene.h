// The contract between the application and its scenes. A scene is a folder under src/scenes with its renderer, its
// settings (scenes/<id>.cfg) and its OptiX programs; to the application it is this interface: what it is (shown in
// the gallery, with how it is made), how it loads (in steps, on a worker thread, while the loading screen runs), one
// frame (input in, an image out), what its interface panel should say and offer (controls), and how it lets go of
// the GPU. Adding a scene: implement Scene, add one entry to src/app/registry.cpp.
#pragma once
#include "ui/ui.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

struct KeyHelp {
    std::string keys;   // as printed on the key caps, " " between caps: "Ctrl Sol tık"
    std::string what;
};
struct KeyGroup {
    std::string title;
    std::vector<KeyHelp> items;
};

struct Detail {                // a paragraph of the gallery's "Ayrıntılar": how the scene is made
    std::string title, text;
};

struct SceneInfo {
    std::string id;            // command line and file names: --scene <id>, captures/<id>_...
    std::string number;        // "01"
    std::string title;         // "Altın Saat"
    std::string subtitle;      // one line under the title
    std::string description;   // what it is, in a sentence or two (the gallery, under the picture)
    std::vector<Detail> details;   // how it is made (the gallery's "Ayrıntılar", in two columns)
    std::vector<std::string> tags;
    std::string image;         // gallery picture, relative to the project root
    uint32_t accent = 0xE8B46A;
    std::vector<KeyGroup> keys;
    bool reloadable = false;   // F5 reloads its settings and programs
};

// what was done to the panel's controls (see Control), by the control's id
struct ControlEvent {
    std::string id;
    int item = -1;                       // Choice: the one clicked; Button: 0
    float value = 0.0f;                  // Slider: where it is dragged, 0..1 (every frame while the button is held)
    bool released = false;               // Slider: the drag ended
};

// one frame of input, already stripped of the application's own keys and of the clicks that went to the panel
struct Input {
    double now = 0.0, dt = 0.0;          // seconds since the scene started, since the last frame
    std::vector<int> pressedKeys;        // GLFW key codes pressed since the last frame
    std::function<bool(int)> down;       // held now
    bool shift = false, ctrl = false;
    float mouseX = 0, mouseY = 0;        // window pixels
    float mouseDX = 0, mouseDY = 0;      // since the last frame
    bool leftClicked = false, leftDown = false, rightDown = false;
    float scroll = 0.0f;
    ui::Rect view;                       // where the scene's picture is shown
    std::vector<ControlEvent> events;    // the panel's controls
    bool pressed(int key) const;
    bool isDown(int key) const { return down && down(key); }
};

// what the interface panel shows for the scene this frame
struct Chip {
    std::string text;
    bool strong = false;                 // the scene's accent
};
struct Metric {
    std::string label, value;
};
// A control on the scene's panel. The application draws it, takes the mouse over it (the scene never sees those
// clicks) and hands what was done back as events in the next frame's input, by id. The scene stays the owner of
// the state: it says every frame what the control shows.
struct Control {
    enum class Kind { Choice, Slider, Button };
    Kind kind = Kind::Choice;
    std::string id;
    std::string title;                   // on its left, in capitals ("BÜYÜKLÜK"); a button has none
    std::vector<std::string> items;      // Choice: the choices
    int active = -1;                     // Choice: the one chosen
    float value = 0.0f;                  // Slider: the handle, 0..1
    float mark = -1.0f;                  // Slider: a notch (the default), < 0 none
    std::string text;                    // Slider: the value in words; Button: its label
    std::string keys;                    // Button: the key that does the same, on a cap
    bool strong = false;                 // Button: in the scene's colour
    bool enabled = true;                 // Button: false shows it dimmed and ignores clicks
    std::string detail;                  // a line under it
    bool section = false;                // a thin rule above it: the start of another group
};
struct Status {
    std::vector<Chip> chips;
    std::vector<Metric> metrics;         // timings, top right
    std::vector<Control> controls;       // bottom left, under the scene's name
    std::string progressLabel;           // with progress >= 0: a bar
    float progress = -1.0f;
    bool progressEndless = false;        // the bar runs without an end (until stopped)
};

// what the application offers a scene
class Host {
public:
    virtual ~Host() = default;
    virtual void toast(const std::string& text) = 0;
    virtual void captureMouse(bool on) = 0;   // hidden and unbounded: first-person looking
    virtual const std::string& root() const = 0;
    virtual std::string capturePath() = 0;    // captures/<id>_<date_time> (no extension)
    virtual const std::vector<std::string>& overrides() const = 0;   // --set key=value, for the scene's settings
};

struct LoadState {
    int step = 0;                        // the scene's own counter
    float fraction = 0.0f;
    std::string label;                   // what the step about to run does
    std::string error;                   // set: loading failed
    bool done = false;
};

class Scene {
public:
    virtual ~Scene() = default;
    virtual const SceneInfo& info() const = 0;
    // called repeatedly (on a worker thread) until state.done or state.error; each call does one step
    virtual void load(Host& host, LoadState& state) = 0;
    virtual void frame(Host& host, const Input& in) = 0;
    virtual const void* image(int& width, int& height) = 0;   // RGBA8, top row first, valid until the next frame
    virtual void status(Status& s) = 0;
    virtual void screenshot(Host& host) = 0;
    virtual void reload(Host&) {}
    virtual void unload() = 0;           // its GPU resources freed (the application resets the device afterwards anyway)
};

struct Entry {
    SceneInfo (*info)();
    std::unique_ptr<Scene> (*make)();
    // headless tools and tests on the command line (--scene <id> --headless ...); 0: none
    int (*cli)(int argc, char** argv);
};
const std::vector<Entry>& registry();

}   // namespace app
