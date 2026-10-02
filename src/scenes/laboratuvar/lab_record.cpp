#include "scenes/laboratuvar/lab_record.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace {
double nowS() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}   // namespace

void LabRecording::setState(const std::string& s) {
    std::lock_guard<std::mutex> l(stateMutex_);
    state_ = s;
}

std::string LabRecording::state() const {
    std::lock_guard<std::mutex> l(stateMutex_);
    return state_;
}

bool LabRecording::start(const Config& cfg, Deney d, const LabInputs& in, const LabEvent& e, double duration, double frameDt, size_t budgetBytes) {
    release();
    dt_ = frameDt;
    stop_ = false;
    done_ = false;
    failed_ = false;
    framesDone_ = 0;
    total_ = std::max(1, int(std::ceil(duration / frameDt)));
    budget_ = budgetBytes;
    started_ = true;
    worker_ = std::thread([this, cfg, d, in, e] { run(cfg, d, in, e); });
    return true;
}

void LabRecording::cancel() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
}

void LabRecording::release() {
    cancel();
    if (frames_) cudaFree(frames_);
    frames_ = nullptr;
    poses_.clear();
    started_ = false;
    framesDone_ = 0;
    total_ = 0;
    drawn_ = 0;
    membraneFirst = -1;
    membraneCount = 0;
    membraneTris.clear();
    ballFirst = -1;
    ballCount = 0;
}

void LabRecording::run(Config cfg, Deney d, LabInputs in, LabEvent e) {
    const double t0 = nowS();
    setState("build");
    LabWorld w;
    if (!w.build(d, cfg, true, in)) { failed_ = true; done_ = true; return; }
    summary = w.summary();
    setState("settle");
    const std::string pre = std::string(deneyKey(d)) + ".";
    w.settle(cfg.f(pre + "oturma", 1.0f), std::max(dt_, 1.0 / cfg.f(pre + "oturma_hiz", float(1.0 / dt_))));   // a solid may settle in larger steps   // in the recording's own steps (its substeps are set for them)
    if (stop_) { done_ = true; return; }
    // the drawn particles, and where the membrane and the ball are among them
    std::vector<uint32_t> idx;
    std::vector<int> drawnOf(w.code.size(), -1);
    for (size_t i = 0; i < w.code.size(); ++i)
        if (w.code[i] != DC_NONE) {
            drawnOf[i] = int(idx.size());
            idx.push_back(uint32_t(i));
        }
    const int n = int(idx.size());
    if (w.membraneFirst >= 0) {
        membraneFirst = drawnOf[size_t(w.membraneFirst)];
        membraneCount = w.membraneCount;
        membraneTris = w.membraneTris;
    }
    if (w.ballFirst >= 0) {
        ballFirst = drawnOf[size_t(w.ballFirst)];
        ballCount = w.ballCount;
        ballRadius = w.ballRadius;
    }
    spacing_ = w.s;
    // the box: 16 bits an axis
    box_.lo = w.boxLo;
    const float3 ext = w.boxHi - w.boxLo;
    box_.scale = make_float3(ext.x / 65535.0f, ext.y / 65535.0f, ext.z / 65535.0f);
    const size_t perFrame = size_t(n) * sizeof(ushort4);
    const int fit = int(std::min<size_t>(size_t(total_.load()), budget_ / std::max<size_t>(perFrame, 1)));
    total_ = std::max(1, fit);
    if (cudaMalloc(reinterpret_cast<void**>(&frames_), perFrame * size_t(total_.load())) != cudaSuccess) {
        failed_ = true;
        done_ = true;
        return;
    }
    poses_.assign(size_t(total_.load()) * LabWorld::P_COUNT, PropPose{});
    cudaStream_t st;
    cudaStreamCreateWithFlags(&st, cudaStreamNonBlocking);
    float3 *dAll = nullptr, *dDrawn = nullptr;
    uint8_t *dCodeAll = nullptr, *dCodeDrawn = nullptr;
    uint32_t* dIdx = nullptr;
    cudaMalloc(reinterpret_cast<void**>(&dAll), w.code.size() * sizeof(float3));
    cudaMalloc(reinterpret_cast<void**>(&dCodeAll), w.code.size());
    cudaMalloc(reinterpret_cast<void**>(&dDrawn), size_t(n) * sizeof(float3));
    cudaMalloc(reinterpret_cast<void**>(&dCodeDrawn), size_t(n));
    cudaMalloc(reinterpret_cast<void**>(&dIdx), size_t(n) * sizeof(uint32_t));
    cudaMemcpy(dIdx, idx.data(), size_t(n) * sizeof(uint32_t), cudaMemcpyHostToDevice);
    drawn_ = n;
    std::printf("kayit: %s; %d kare x %.2f ms dunya zamani, %d cizilen tanecik, %.0f MB (kurma + oturma %.1f s)\n", summary.c_str(), total_.load(), dt_ * 1000.0, n,
                double(perFrame) * total_.load() / 1048576.0, nowS() - t0);
    setState("compute");
    w.trigger(e);
    const double tc = nowS();
    for (int k = 0; k < total_.load(); ++k) {
        if (stop_) break;
        if (k > 0) w.step(float(dt_));
        w.exportDraw(dAll, dCodeAll, st);
        launchGather(dAll, dCodeAll, dIdx, n, dDrawn, dCodeDrawn, st);
        launchQuantise(dDrawn, dCodeDrawn, n, box_, frames_ + size_t(k) * n, st);
        for (int p = 0; p < LabWorld::P_COUNT; ++p) poses_[size_t(k) * LabWorld::P_COUNT + p] = w.prop[p];
        cudaStreamSynchronize(st);
        framesDone_.store(k + 1, std::memory_order_release);
        msPerFrame_ = float((nowS() - tc) * 1000.0 / (k + 1));
    }
    std::printf("kayit bitti: %d kare, %.1f s (kare basina %.1f ms)\n", framesDone_.load(), nowS() - tc, msPerFrame_.load());
    for (void* p : {static_cast<void*>(dAll), static_cast<void*>(dDrawn), static_cast<void*>(dCodeAll), static_cast<void*>(dCodeDrawn), static_cast<void*>(dIdx)})
        cudaFree(p);
    cudaStreamDestroy(st);
    w.destroy();
    setState("done");
    done_ = true;
}

void LabRecording::frameAt(double t, float3* dPos, uint8_t* dCode, PropPose* poses, cudaStream_t st) const {
    const int done = framesDone();
    const int n = drawnCount();
    if (done <= 0 || n <= 0) return;
    const double f = std::max(0.0, std::min(double(done - 1), t / dt_));
    const int k0 = int(std::floor(f));
    const int k1 = std::min(done - 1, k0 + 1);
    const float a = float(f - k0);
    launchDequantise(frames_ + size_t(k0) * n, k1 != k0 ? frames_ + size_t(k1) * n : nullptr, a, n, box_, dPos, dCode, st);
    if (poses)
        for (int p = 0; p < LabWorld::P_COUNT; ++p) {
            const PropPose& A = poses_[size_t(k0) * LabWorld::P_COUNT + p];
            const PropPose& B = poses_[size_t(k1) * LabWorld::P_COUNT + p];
            for (int q = 0; q < 12; ++q) poses[p].xf[q] = A.xf[q] + (B.xf[q] - A.xf[q]) * a;
        }
}
