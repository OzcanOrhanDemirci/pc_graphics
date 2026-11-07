// The live scene and headless measurement use this identical OptiX render path.
#pragma once
#include "scenes/ruya/ruya_world.h"
#include <memory>
#include <string>
#include <vector>

namespace ruya {
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    [[nodiscard]] bool init(int width, int height, const std::string& root,
                            const std::vector<Mesh>& prototypes, std::string& error);
    // Both lists are sorted by z then x. Transport, when supplied, includes visible.
    // The GPU instance data survives through one render call.
    void setCells(const std::vector<Cell>& visible, const std::vector<Cell>& transport = {});
    void render(const Camera& camera, int spp = 3, int bounces = 3, int sampleOffset = 0,
                bool denoise = true, float exposure = 0.0f, int materialDetail = 1);
    void download(std::vector<unsigned char>& rgba) const;
    std::vector<float4> hdr() const;
    std::vector<float4> depth() const;
    bool opticalPoolIsClear() const;
    const uchar4* devicePixels() const;
    const FrameStats& stats() const;
    const std::string& deviceName() const;
    void release();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
