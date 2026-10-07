// Scene 10 "Kuyu": one run's state, shared by the live scene and the command-line tools.
#pragma once
#include "core/config.h"
#include "scenes/kuyu/kuyu_kernels.h"

#include <string>
#include <vector>

struct KuyuCam {
    double x = 0.0, y = 0.3, z = 4.0;   // the eye, in the lattice (doubles: the fall never ends)
    float yaw = 0.0f, pitch = 0.0f;     // yaw 0 looks along -z, grows to the left; pitch up positive (radians)
    float fovY = 62.0f;                 // degrees
};

class KuyuApp {
public:
    std::string root;
    std::vector<std::string> overrides;
    Config cfg;
    int width = 2560, height = 1440;
    KuyuCam cam;
    double time = 0.0;
    kuyu::Lattice lat;
    kuyu::Optics optics;
    std::vector<kuyu::RoomVariant> variants;
    std::vector<std::string> variantNames;
    std::vector<unsigned char> variantOf;   // 256 entries: the rooms' mix
    float3 meanGlow{0, 0, 0};
    float3 meanFace{0, 0, 0};
    float glowShape[kuyu::kShapeN] = {};
    std::vector<float4> faceGlow;       // per variant 6 faces: the glow leaving (cosine-weighted)
    std::vector<float4> faceGlowRest;   // ... without the bulbs seen straight
    double tableMs = 0.0;
    float frameMs = 0.0f;
    unsigned int lastBad = 0;
    int debug = 0;
    float evScale = 1.0f;
    float subX = 0.0f, subY = 0.0f;
    unsigned long long* statsDev = nullptr;
    uchar4* output = nullptr;   // device: the last picture

    static int viewCount();
    static KuyuCam view(int i);
    static const char* viewName(int i, bool turkish);

    void loadConfig();
    void init();
    void release();
    void render();
    bool save(const std::string& pathNoExt);
    bool saveReference(const std::string& pathNoExt);
    void probe(float x, float y);
    kuyu::View makeView() const;
    void computeOptics();
    void buildVariants();
    float3 forward() const;
    // the observer, a sphere of radius r, pushed out of the rooms' glass and the bundles; true if it touched
    bool collide(double& x, double& y, double& z, float r) const;
    // the observer moving from (x, y, z) by (mx, my, mz) (one frame's way, any length): the share of it before it meets an
    // obstacle (its boxes grown by r), walked cell by cell; 1 if nothing. n: the face met.
    double sweep(double x, double y, double z, double mx, double my, double mz, float r, double n[3]) const;
    std::vector<float4> hdrHost() const {
        std::vector<float4> h(size_t(width) * height);
        cudaMemcpy(h.data(), hdr_, h.size() * sizeof(float4), cudaMemcpyDeviceToHost);
        return h;
    }
    const std::vector<float4>& glowHost() const { return glowHost_; }

private:
    float4* hdr_ = nullptr;
    static const int kLevels = 6;
    float4* levels_[kLevels] = {};
    float4* accum_[kLevels] = {};
    kuyu::RoomVariant* dVariants_ = nullptr;
    unsigned char* dVariantOf_ = nullptr;
    float4* dGlow_ = nullptr;
    float4* dFaceGlow_ = nullptr;
    float4* dFaceGlowRest_ = nullptr;
    std::vector<float4> glowRestHost_;
    float4* dTail_ = nullptr;
    unsigned char* dLtIdx_ = nullptr;
    float* dLtG_ = nullptr;
    float2* dLtFar_ = nullptr;
    int ltNa_ = 0, ltNw_ = 0;
    void buildFaceGlow();
    std::vector<float4> faceMeans(const std::vector<float4>& table, double* shapeSum, double* shapeW) const;
    std::vector<float4> glowHost_;
    unsigned int* bad_ = nullptr;
    cudaEvent_t ev0_ = nullptr, ev1_ = nullptr;
    bool errorShown_ = false;
};

int runKuyuCli(int argc, char** argv);
