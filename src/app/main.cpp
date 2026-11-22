// pc_graphics: art-directed scenes on the GPU, in one application.
//
//   pc_graphics.exe                         the gallery, full screen on the primary monitor
//   pc_graphics.exe --scene <id>            straight into a scene (altinsaat, fener, yikim); Esc leads to the gallery
//   pc_graphics.exe --windowed              in a 1280x720 window
//   pc_graphics.exe --size <w>x<h>          in a window of that size (the screens of another shape: 1024x768, 3440x1440)
//   pc_graphics.exe --autotest              the whole application once through by script, screens to captures/arayuz/
//   pc_graphics.exe --autotest-only <id>    the same, but of the scenes only that one (laboratuvar/kup: of it only the cube;
//                                           laboratuvar/kup1m: a million cubes' fine pass to the end)
//   pc_graphics.exe --set key=value         a setting for the scenes (scenes/<id>.cfg), repeatable
//   pc_graphics.exe --lang tr|en            the language on screen (else the one chosen last, in user.cfg)
//
// Every other option belongs to a scene's command-line tools (renders without a window, tests):
//   pc_graphics.exe --scene altinsaat --headless --spp 2048 --out captures/x     (also without --scene, as it always was)
//   pc_graphics.exe --scene fener --headless --frames 64 --out captures/x
//   pc_graphics.exe --scene yikim --test
// See each scene's *_app.cpp for its list.
#include "app/app.h"

#include <cstdio>
#include <string>

// Ask hybrid-graphics systems for the discrete NVIDIA GPU.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0;
}

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a piped log keeps its last lines when the run is killed (MSVC has no
                                                // line buffering: _IOLBF is full)
    app::Options opt;
    opt.root = PROJECT_ROOT;
    std::string scene;
    bool windowOnly = true;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--scene") scene = next();
        else if (a == "--windowed") opt.windowed = true;
        else if (a == "--size") {
            opt.windowed = true;
            std::sscanf(next().c_str(), "%dx%d", &opt.windowW, &opt.windowH);
        }
        else if (a == "--autotest") opt.autotest = true;
        else if (a == "--autotest-only") { opt.autotest = true; opt.autotestOnly = next(); }
        else if (a == "--set") opt.overrides.push_back(next());
        else if (a == "--root") opt.root = next();
        else if (a == "--lang") opt.lang = next();
        else windowOnly = false;
    }
    if (!windowOnly) {
        // a scene's tools; without --scene the first scene's, as pc_graphics.exe's command line always was
        const std::string id = scene.empty() ? app::registry().front().info().id : scene;
        for (const app::Entry& e : app::registry())
            if (e.info().id == id) {
                if (!e.cli) break;
                return e.cli(argc, argv);
            }
        std::printf("bilinmeyen sahne ya da secenek: %s\n", id.c_str());
        return 2;
    }
    if (!scene.empty()) {
        bool known = false;
        for (const app::Entry& e : app::registry()) known = known || e.info().id == scene;
        if (!known) { std::printf("bilinmeyen sahne: %s\n", scene.c_str()); return 2; }
    }
    opt.startScene = scene;
    app::App a;
    return a.run(opt);
}
