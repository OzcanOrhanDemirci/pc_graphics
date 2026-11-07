#pragma once
#include "core/config.h"
#include "scenes/ruya/ruya_collision.h"
#include "scenes/ruya/ruya_renderer.h"

namespace ruya {
class Run {
public:
    [[nodiscard]] bool init(const std::string& root, const std::vector<std::string>& overrides, std::string& error);
    void render();
    void move(float dx, float dy, float forward, float right, float up, float dt);
    [[nodiscard]] bool save(const std::string& prefix) const;
    void release();
    static Camera view(int index);

    Camera camera;
    Renderer renderer;
    int width = 2560, height = 1440, spp = 3, bounces = 3, detail = 2;
    int sampleOffset = 0, materialDetail = 1;
    bool denoise = true, offscreen = true, fullWorld = false;
    float exposure = 0.0f;
    double frameMs = 0;
    unsigned added = 0, evicted = 0;
    unsigned long long geometryTriangles = 0;
    std::vector<Cell> cells;

private:
    CollisionWorld collision_;
};
int cli(int argc, char** argv);
}
