#include "app/lang.h"
#include "ui/ui.h"

#include <fstream>
#include <sstream>
#include <vector>

namespace app {

namespace {
Lang g_lang = Lang::TR;
}

Lang lang() { return g_lang; }

void setLang(Lang l) {
    g_lang = l;
    ui::setTurkishCase(l == Lang::TR);
}

Lang loadLang(const std::string& root) {
    std::ifstream in(root + "/user.cfg");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key, eq, value;
        if (ls >> key >> eq >> value && key == "lang" && eq == "=") return value == "en" ? Lang::EN : Lang::TR;
    }
    return Lang::TR;
}

void saveLang(const std::string& root, Lang l) {
    std::vector<std::string> lines;
    {
        std::ifstream in(root + "/user.cfg");
        std::string line;
        while (std::getline(in, line))
            if (line.rfind("lang", 0) != 0) lines.push_back(line);
    }
    if (lines.empty()) lines.push_back("# this computer's choices for pc_graphics (not in git)");
    lines.push_back(std::string("lang = ") + (l == Lang::EN ? "en" : "tr"));
    std::ofstream out(root + "/user.cfg", std::ios::trunc);
    for (const std::string& line : lines) out << line << "\n";
}

}   // namespace app
