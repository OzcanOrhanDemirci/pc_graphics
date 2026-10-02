// Scene 4 "Laboratuvar": a recording. The trigger played live on the coarse world; the same experiment, the same
// inputs and the same trigger are computed again on a FINE world on a worker thread (its own CUDA stream), every
// frameDt of world time stored on the GPU (the drawn particles' positions, quantised to 16 bits an axis in the
// experiment's box, with their draw codes; the props' poses on the host). What is computed can be watched at once:
// frames < framesDone() are final. Playing is only drawing: a time between two stored frames is interpolated.
#pragma once
#include "scenes/laboratuvar/lab_kernels.h"
#include "scenes/laboratuvar/lab_world.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class LabRecording {
public:
    // what it records, how finely, for how long; budget: GPU bytes for the frames (the duration shrinks to fit)
    bool start(const Config& cfg, Deney d, const LabInputs& in, const LabEvent& e, double duration, double frameDt, size_t budgetBytes);
    void cancel();               // stops the worker (what is done stays playable)
    void release();              // frees everything
    ~LabRecording() { release(); }

    bool active() const { return started_; }
    bool computing() const { return started_ && !done_.load(); }
    int framesDone() const { return framesDone_.load(std::memory_order_acquire); }
    int framesTotal() const { return total_.load(); }
    double frameDt() const { return dt_; }
    double duration() const { return total_.load() * dt_; }
    double computed() const { return framesDone() * dt_; }   // seconds of world time ready
    float msPerFrame() const { return msPerFrame_.load(); }  // the worker's wall time per stored frame (ms)
    std::string state() const;   // what the worker is doing (for the panel)
    bool failed() const { return failed_; }
    // the drawing of world time t (clamped to what is computed): positions and codes of the drawn particles, the props
    int drawnCount() const { return drawn_.load(); }
    float spacing() const { return spacing_; }
    void frameAt(double t, float3* dPos, uint8_t* dCode, PropPose* poses, cudaStream_t st) const;
    // what the drawing needs from the fine world: the membrane (indices into the drawn set) and the ball
    int membraneFirst = -1, membraneCount = 0;
    std::vector<uint3> membraneTris;
    int ballFirst = -1, ballCount = 0;
    float ballRadius = 0.0f;
    std::string summary;         // the fine world's counts

private:
    std::thread worker_;
    std::atomic<bool> stop_{false}, done_{false};
    std::atomic<int> framesDone_{0};
    std::atomic<float> msPerFrame_{0.0f};
    bool started_ = false, failed_ = false;
    mutable std::mutex stateMutex_;
    std::string state_;
    std::atomic<int> total_{0}, drawn_{0};
    double dt_ = 1.0 / 600.0;
    size_t budget_ = 0;
    float spacing_ = 0.0f;
    QuantBox box_{};
    ushort4* frames_ = nullptr;      // total_ x drawn_
    std::vector<PropPose> poses_;    // total_ x P_COUNT
    void run(Config cfg, Deney d, LabInputs in, LabEvent e);
    void setState(const std::string& s);
};
