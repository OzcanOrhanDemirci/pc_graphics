// A camera path for the scenes' headless captures (--path <file>): one pose a line, "x y z yaw pitch fov [time]",
// '#' starts a comment line; every pose is one output frame, frame_NNNN.png. The same format as scene 9's --path
// (tools such as C:/dev/video/tools/showreel/dolly.py write it). What the numbers mean (yaw's zero, units, whether y is
// the eye or the feet) is each scene's own, written in its *_app.cpp; time is the scene's clock for that frame, in
// seconds, where the scene has one.
#pragma once
#include <cstdio>
#include <string>
#include <vector>

struct CamPose {
    double x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0, fov = 0;
    double time = -1.0;    // the 7th column; < 0: not given
};

// reads the file; false (and why in err) if it cannot be read or holds no pose. timed: every pose had a time
inline bool readCamPath(const std::string& file, std::vector<CamPose>& poses, std::string& err, bool* timed = nullptr) {
    poses.clear();
    std::FILE* f = std::fopen(file.c_str(), "r");
    if (!f) {
        err = "yol okunamadi: " + file;
        return false;
    }
    char line[512];
    bool all = true;
    while (std::fgets(line, sizeof(line), f)) {
        const char* s = line;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '#') continue;
        CamPose p;
        const int got = std::sscanf(s, "%lf %lf %lf %f %f %f %lf", &p.x, &p.y, &p.z, &p.yaw, &p.pitch, &p.fov, &p.time);
        if (got < 6) continue;
        if (got < 7) {
            p.time = -1.0;
            all = false;
        }
        poses.push_back(p);
    }
    std::fclose(f);
    if (poses.empty()) {
        err = "yolda poz yok: " + file;
        return false;
    }
    if (timed) *timed = all;
    return true;
}

// frame_NNNN (no extension), the name every path capture uses
inline std::string camPathFrameName(int i) {
    char b[32];
    std::snprintf(b, sizeof(b), "frame_%04d", i);
    return b;
}
