#include "core/config.h"

#include <cstdio>
#include <fstream>
#include <sstream>

static std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
bool Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        values[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return true;
}
float Config::f(const std::string& key, float def) const {
    auto it = values.find(key);
    return it == values.end() ? def : std::stof(it->second);
}
float3 Config::v3(const std::string& key, float3 def) const {
    auto it = values.find(key);
    if (it == values.end()) return def;
    float3 r = def;
    std::string v = it->second;
    for (char& ch : v)
        if (ch == ',') ch = ' ';   // "x, y, z" and "x y z" both (a comma used to stop the read silently after x)
    std::sscanf(v.c_str(), "%f %f %f", &r.x, &r.y, &r.z);
    return r;
}
