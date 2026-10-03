// Scene 4 "Laboratuvar": a recording. The triggers played live on the coarse world; asked for, the same experiment,
// the same inputs and the same triggers are computed again on a FINE world on a worker thread (its own CUDA stream):
// the drawn particles' positions (quantised to 16 bits an axis in the experiment's box) with their draw codes on the
// GPU, the props' poses on the host. Every world step is kept while something fast happens (for `after` seconds from
// each trigger), between triggers only every sparseDt. What is computed can be watched at once: frames <
// framesDone() are final. Playing is only drawing: a time between two stored frames is interpolated.
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
    // what it records: the experiment, its inputs, its triggers (the first at time zero, the others at their `at`; a
    // pause between two longer than gapMax is shortened to it, if gapMax > 0), until `after` seconds past the last;
    // frameDt the world's step (every one kept while busy), sparseDt between; budget: GPU bytes for the frames (the busy
    // parts thin out to fit, then the end goes)
    bool start(const Config& cfg, Deney d, const LabInputs& in, const std::vector<LabEvent>& events, double after, double frameDt, double sparseDt,
               double gapMax, size_t budgetBytes);
    void cancel();               // stops the worker (what is done stays playable)
    void release();              // frees everything
    ~LabRecording() { release(); }

    bool active() const { return started_; }
    bool computing() const { return started_ && !done_.load(); }
    int framesDone() const { return framesDone_.load(std::memory_order_acquire); }
    int framesTotal() const { return total_.load(); }
    float progress() const { return float(stepsDone_.load()) / float(std::max(1, stepsTotal_.load())); }
    double frameDt() const { return dt_; }
    double busyRate() const { return 1.0 / (dt_ * denseStride_.load()); }   // moments a second kept while busy
    double duration() const { return duration_.load(); }
    double computed() const;     // seconds of world time ready
    double slowdown() const { return slowdown_.load(); }   // wall time over world time, so far
    int shots() const { return int(events_.size()); }
    std::string state() const;   // what the worker is doing (for the panel)
    bool failed() const { return failed_; }
    // the drawing of world time t (clamped to what is computed): positions and codes of the drawn particles, the props
    int drawnCount() const { return drawn_.load(); }
    float spacing() const { return spacing_; }
    void frameAt(double t, float3* dPos, uint8_t* dCode, PropPose* poses, cudaStream_t st) const;
    // what the drawing needs from the fine world: the membrane (indices into the drawn set)
    int membraneFirst = -1, membraneCount = 0;
    std::vector<uint3> membraneTris;
    std::string summary;         // the fine world's counts

private:
    std::thread worker_;
    std::atomic<bool> stop_{false}, done_{false};
    std::atomic<int> framesDone_{0}, stepsDone_{0}, stepsTotal_{0}, denseStride_{1};
    std::atomic<float> msPerFrame_{0.0f}, slowdown_{0.0f};
    std::atomic<double> duration_{0.0};
    bool started_ = false, failed_ = false;
    mutable std::mutex stateMutex_;
    std::string state_;
    std::atomic<int> total_{0}, drawn_{0};
    double dt_ = 1.0 / 600.0, after_ = 1.0, sparseDt_ = 1.0 / 120.0;
    size_t budget_ = 0;
    float spacing_ = 0.0f;
    QuantBox box_{};
    std::vector<LabEvent> events_;   // their `at` shortened (gapMax)
    std::vector<int> steps_;         // the world steps kept (frame k is step steps_[k]); set before the first frame
    ushort4* frames_ = nullptr;      // total_ x drawn_
    std::vector<PropPose> poses_;    // total_ x P_COUNT
    void schedule(int denseStride);  // steps_ from events_, after_, dt_, sparseDt_
    void run(Config cfg, Deney d, LabInputs in);
    void setState(const std::string& s);
};
