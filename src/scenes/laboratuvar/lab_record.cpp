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

bool LabRecording::start(const Config& cfg, Deney d, const LabInputs& in, const std::vector<LabEvent>& events, double after, double frameDt, double sparseDt,
                         double gapMax, size_t budgetBytes, double busy) {
    release();
    if (events.empty()) return false;
    dt_ = frameDt;
    after_ = std::max(frameDt, after);
    busy_ = busy < 0.0 ? after_ : std::min(after_, std::max(frameDt, busy));
    sparseDt_ = std::max(frameDt, sparseDt);
    // the triggers' times from the first; a long pause shortened (nothing fast happens in it: the world only rests)
    events_ = events;
    events_[0].at = 0.0;
    for (size_t i = 1; i < events_.size(); ++i) {
        double gap = std::max(0.0, events[i].at - events[i - 1].at);
        if (gapMax > 0.0) gap = std::min(gap, gapMax);
        events_[i].at = events_[i - 1].at + gap;
    }
    stop_ = false;
    done_ = false;
    failed_ = false;
    framesDone_ = 0;
    stepsDone_ = 0;
    budget_ = budgetBytes;
    schedule(1);
    started_ = true;
    worker_ = std::thread([this, cfg, d, in] { run(cfg, d, in); });
    return true;
}

void LabRecording::schedule(int denseStride, int sparseMul) {
    const double end = events_.back().at + after_;
    const int last = std::max(0, int(std::ceil(end / dt_ - 1e-9)));
    const int sparse = std::max(1, int(std::lround(sparseDt_ / dt_))) * std::max(1, sparseMul);
    steps_.clear();
    size_t next = 0;
    double busyUntil = -1.0;
    for (int k = 0; k <= last; ++k) {
        const double t = k * dt_;
        while (next < events_.size() && events_[next].at <= t + 1e-9) busyUntil = events_[next++].at + busy_;
        const bool busy = t <= busyUntil + 1e-9;
        if ((busy && k % denseStride == 0) || k % sparse == 0 || k == last) steps_.push_back(k);
    }
    denseStride_ = denseStride;
    total_ = int(steps_.size());
    stepsTotal_ = last;
    duration_ = steps_.empty() ? 0.0 : steps_.back() * dt_;
}

double LabRecording::computed() const {
    const int done = framesDone();
    return done > 0 ? steps_[size_t(done - 1)] * dt_ : 0.0;
}

void LabRecording::cancel() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
}

void LabRecording::release() {
    cancel();
    if (frames_) cudaFree(frames_);
    frames_ = nullptr;
    if (cubePos_) cudaFree(cubePos_);
    if (cubeRot_) cudaFree(cubeRot_);
    cubePos_ = cubeRot_ = nullptr;
    cubes_ = false;
    nCubes_ = 0;
    poses_.clear();
    steps_.clear();
    events_.clear();
    started_ = false;
    framesDone_ = 0;
    stepsDone_ = 0;
    total_ = 0;
    drawn_ = 0;
    duration_ = 0.0;
    membraneFirst = -1;
    membraneCount = 0;
    membraneTris.clear();
}

void LabRecording::run(Config cfg, Deney d, LabInputs in) {
    const double t0 = nowS();
    setState("build");
    LabWorld w;
    if (!w.build(d, cfg, true, in)) { failed_ = true; done_ = true; return; }
    summary = w.summary();
    setState("settle");
    const std::string pre = std::string(deneyKey(d)) + ".";
    w.settle(cfg.f(pre + "oturma", 1.0f), std::max(dt_, 1.0 / cfg.f(pre + "oturma_hiz", float(1.0 / dt_))));   // a solid may settle in larger steps   // in the recording's own steps (its substeps are set for them)
    if (stop_) { done_ = true; return; }
    // the first trigger first: it may add particles (a drop), and they must be among the drawn ones and fit the
    // buffers (the later ones only shoot: they add none)
    w.trigger(events_[0]);
    if (d == Deney::Kup) {
        runCubes(w, t0);
        return;
    }
    // the drawn particles, and where the membrane is among them
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
    spacing_ = w.s;
    // the box: 16 bits an axis
    box_.lo = w.boxLo;
    const float3 ext = w.boxHi - w.boxLo;
    box_.scale = make_float3(ext.x / 65535.0f, ext.y / 65535.0f, ext.z / 65535.0f);
    // the frames within the budget: the busy parts thin out first (every 2nd step, 3rd...), then the end goes
    const size_t perFrame = size_t(n) * sizeof(ushort4);
    const size_t fitFrames = budget_ / std::max<size_t>(perFrame, 1);
    for (int stride = 1; size_t(total_.load()) > fitFrames && stride < 8; ++stride) schedule(stride + 1);
    if (size_t(total_.load()) > fitFrames) {
        steps_.resize(std::max<size_t>(1, fitFrames));
        total_ = int(steps_.size());
        stepsTotal_ = steps_.back();
        duration_ = steps_.back() * dt_;
    }
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
    std::printf("kayit: %s; %d tetik, %d kare (yogun her %d adimda) x %.2f ms adim, %.2f s dunya zamani, %d cizilen tanecik, %.0f MB (kurma + oturma %.1f s)\n",
                summary.c_str(), int(events_.size()), total_.load(), denseStride_.load(), dt_ * 1000.0, duration_.load(), n,
                double(perFrame) * total_.load() / 1048576.0, nowS() - t0);
    setState("compute");
    const double tc = nowS();
    const int total = total_.load();
    size_t next = 1;   // the first trigger is in
    int stored = 0;
    for (int k = 0; stored < total; ++k) {
        if (stop_) break;
        if (k > 0) w.step(float(dt_));
        while (next < events_.size() && events_[next].at <= k * dt_ + 1e-9) w.trigger(events_[next++]);
        stepsDone_ = k;
        if (steps_[size_t(stored)] != k) continue;
        w.exportDraw(dAll, dCodeAll, st);
        launchGather(dAll, dCodeAll, dIdx, n, dDrawn, dCodeDrawn, st);
        launchQuantise(dDrawn, dCodeDrawn, n, box_, frames_ + size_t(stored) * n, st);
        for (int p = 0; p < LabWorld::P_COUNT; ++p) poses_[size_t(stored) * LabWorld::P_COUNT + p] = w.prop[p];
        cudaStreamSynchronize(st);
        ++stored;
        framesDone_.store(stored, std::memory_order_release);
        msPerFrame_ = float((nowS() - tc) * 1000.0 / stored);
        slowdown_ = float((nowS() - tc) / std::max(dt_, k * dt_));
    }
    std::printf("kayit bitti: %d kare, %.1f s (dunya saniyesi basina %.1f s)\n", framesDone_.load(), nowS() - tc, slowdown_.load());
    for (void* p : {static_cast<void*>(dAll), static_cast<void*>(dDrawn), static_cast<void*>(dCodeAll), static_cast<void*>(dCodeDrawn), static_cast<void*>(dIdx)})
        cudaFree(p);
    cudaStreamDestroy(st);
    w.destroy();
    setState("done");
    done_ = true;
}

// the Küp experiment: its cubes, 16 bytes each a frame (a position in the box, 16 bits an axis; an orientation,
// snorm16), the busy part thinned first, then the end cut, to fit the budget
void LabRecording::runCubes(LabWorld& w, double t0) {
    const int n = w.cubes.count();
    nCubes_ = n;
    cubeEdge_ = w.cubes.edge();
    spacing_ = cubeEdge_;
    summary = w.summary();
    box_.lo = w.boxLo;
    const float3 ext = w.boxHi - w.boxLo;
    box_.scale = make_float3(ext.x / 65535.0f, ext.y / 65535.0f, ext.z / 65535.0f);
    const size_t perFrame = size_t(n) * 2 * sizeof(ushort4);
    const size_t fitFrames = budget_ / std::max<size_t>(perFrame, 1);
    // within the budget (a million cubes: 16 MB a frame, 256 of them): the burst thins to every 4th step (slow motion
    // watches it), then the flight after it to a half and a quarter of its rate (at real speed, lerped), then the burst
    // further, then the end goes. Thinning the burst first left a million cubes 10 frames in their 80 ms.
    int dense = 1, sparseMul = 1;
    auto over = [&] { return size_t(total_.load()) > fitFrames; };
    while (over() && dense < 4) schedule(++dense, sparseMul);
    while (over() && sparseMul < 4) schedule(dense, sparseMul *= 2);
    while (over() && dense < 16) schedule(++dense, sparseMul);
    if (over()) {
        steps_.resize(std::max<size_t>(1, fitFrames));
        total_ = int(steps_.size());
        stepsTotal_ = steps_.back();
        duration_ = steps_.back() * dt_;
    }
    if (cudaMalloc(reinterpret_cast<void**>(&cubePos_), perFrame / 2 * size_t(total_.load())) != cudaSuccess ||
        cudaMalloc(reinterpret_cast<void**>(&cubeRot_), perFrame / 2 * size_t(total_.load())) != cudaSuccess) {
        failed_ = true;
        done_ = true;
        return;
    }
    poses_.assign(size_t(total_.load()) * LabWorld::P_COUNT, PropPose{});
    cubes_ = true;
    drawn_ = n;
    cudaStream_t st;
    cudaStreamCreateWithFlags(&st, cudaStreamNonBlocking);
    std::printf("kayit: %s; %d tetik, %d kare (yogun her %d adimda, sonra saniyede %.0f) x %.2f ms adim, %.2f s dunya zamani, %.0f MB (kurma %.1f s)\n",
                summary.c_str(), int(events_.size()), total_.load(), denseStride_.load(), 1.0 / (sparseDt_ * sparseMul), dt_ * 1000.0, duration_.load(),
                double(perFrame) * total_.load() / 1048576.0, nowS() - t0);
    setState("compute");
    const double tc = nowS();
    const int total = total_.load();
    size_t next = 1;
    int stored = 0, k = 0;
    while (stored < total) {
        if (stop_) break;
        // to the next stored frame in one go (one call, its own substeps; a trigger on the way breaks it up)
        int target = steps_[size_t(stored)];
        if (next < events_.size()) target = std::min(target, std::max(k + 1, int(std::ceil(events_[next].at / dt_ - 1e-9))));
        if (target > k) {
            w.step(float((target - k) * dt_));
            k = target;
        }
        while (next < events_.size() && events_[next].at <= k * dt_ + 1e-9) w.trigger(events_[next++]);
        stepsDone_ = k;
        if (steps_[size_t(stored)] != k) continue;
        w.cubes.quantise(cubePos_ + size_t(stored) * n, cubeRot_ + size_t(stored) * n, box_.lo, box_.scale, st);
        cudaStreamSynchronize(st);
        ++stored;
        framesDone_.store(stored, std::memory_order_release);
        msPerFrame_ = float((nowS() - tc) * 1000.0 / stored);
        slowdown_ = float((nowS() - tc) / std::max(dt_, k * dt_));
    }
    std::printf("kayit bitti: %d kare, %.1f s (dunya saniyesi basina %.1f s)\n", framesDone_.load(), nowS() - tc, slowdown_.load());
    cudaStreamDestroy(st);
    w.destroy();
    setState("done");
    done_ = true;
}

void LabRecording::locate(double t, int& k0, int& k1, float& a) const {
    const int done = framesDone();
    const double s = std::max(0.0, t / dt_);
    k0 = int(std::upper_bound(steps_.begin(), steps_.begin() + done, int(std::floor(s + 1e-9))) - steps_.begin()) - 1;
    k0 = std::max(0, std::min(done - 1, k0));
    k1 = std::min(done - 1, k0 + 1);
    const double span = double(steps_[size_t(k1)] - steps_[size_t(k0)]);
    a = k1 != k0 && span > 0.0 ? float(std::min(1.0, std::max(0.0, (s - steps_[size_t(k0)]) / span))) : 0.0f;
}

void LabRecording::cubesAt(double t, float4* pose, cudaStream_t st) const {
    if (!cubes_ || framesDone() <= 0) return;
    int k0, k1;
    float a;
    locate(t, k0, k1, a);
    const size_t n = size_t(nCubes_);
    launchCubeDequantise(cubePos_ + size_t(k0) * n, cubeRot_ + size_t(k0) * n, k1 != k0 ? cubePos_ + size_t(k1) * n : nullptr,
                         k1 != k0 ? cubeRot_ + size_t(k1) * n : nullptr, a, nCubes_, box_.lo, box_.scale, cubeEdge_, pose, st);
}

void LabRecording::frameAt(double t, float3* dPos, uint8_t* dCode, PropPose* poses, cudaStream_t st) const {
    const int done = framesDone();
    const int n = drawnCount();
    if (done <= 0 || n <= 0) return;
    // the stored frame at or before t, the next one, how far between them
    const double s = std::max(0.0, t / dt_);
    int k0 = int(std::upper_bound(steps_.begin(), steps_.begin() + done, int(std::floor(s + 1e-9))) - steps_.begin()) - 1;
    k0 = std::max(0, std::min(done - 1, k0));
    const int k1 = std::min(done - 1, k0 + 1);
    const double span = double(steps_[size_t(k1)] - steps_[size_t(k0)]);
    const float a = k1 != k0 && span > 0.0 ? float(std::min(1.0, std::max(0.0, (s - steps_[size_t(k0)]) / span))) : 0.0f;
    launchDequantise(frames_ + size_t(k0) * n, k1 != k0 ? frames_ + size_t(k1) * n : nullptr, a, n, box_, dPos, dCode, st);
    if (poses)
        for (int p = 0; p < LabWorld::P_COUNT; ++p) {
            const PropPose& A = poses_[size_t(k0) * LabWorld::P_COUNT + p];
            const PropPose& B = poses_[size_t(k1) * LabWorld::P_COUNT + p];
            for (int q = 0; q < 12; ++q) poses[p].xf[q] = A.xf[q] + (B.xf[q] - A.xf[q]) * a;
        }
}
