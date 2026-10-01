// Scene 3 "Yikim": an earthquake and a reinforced-concrete building, the physics on the GPU.
//
//   yikim.exe --test [name]            physics tests (dusme, yigin, egim, konsol, enerji, cerceve)
//   options: --set key=value, --root dir
#include "scene.h"   // Config

#include <cstdio>
#include <string>
#include <vector>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

int runYikimTests(const Config& cfg, const std::string& only);

int runYikim(int argc, char** argv) {
    std::string root = PROJECT_ROOT, only;
    std::vector<std::string> overrides;
    bool test = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--scene") next();
        else if (a == "--test") { test = true; if (i + 1 < argc && argv[i + 1][0] != '-') only = next(); }
        else if (a == "--set") overrides.push_back(next());
        else if (a == "--root") root = next();
        else { std::printf("unknown option %s\n", a.c_str()); return 2; }
    }
    Config cfg;
    cfg.load(root + "/scenes/yikim.cfg");
    for (const auto& o : overrides) {
        const size_t eq = o.find('=');
        if (eq != std::string::npos) cfg.set(o.substr(0, eq), o.substr(eq + 1));
    }
    if (test) return runYikimTests(cfg, only);
    std::printf("pencere henuz yok: --test\n");
    return 0;
}
