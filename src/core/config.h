// key = value settings files (scenes/*.cfg), with command-line overrides (--set key=value). Unknown keys fall back
// to the defaults passed in code; "x, y, z" and "x y z" both read as vectors.
#pragma once
#include "shared/vec.h"

#include <map>
#include <string>

// key = value settings, reloaded with F5. Unknown keys fall back to the defaults passed in code.
struct Config {
    std::map<std::string, std::string> values;
    bool load(const std::string& path);
    float f(const std::string& key, float def) const;
    int i(const std::string& key, int def) const { return int(f(key, float(def))); }
    float3 v3(const std::string& key, float3 def) const;
    void set(const std::string& key, const std::string& value) { values[key] = value; }
};
