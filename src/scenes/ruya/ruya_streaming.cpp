#include "scenes/ruya/ruya_streaming.h"

#include <algorithm>

namespace ruya {
IslandBuilder::IslandBuilder(unsigned workers) {
    for (unsigned i = 0; i < std::max(1U, workers); ++i) threads_.emplace_back([this] { work(); });
}

IslandBuilder::~IslandBuilder() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    for (std::thread& thread : threads_) thread.join();
}

IslandBuilder::Result IslandBuilder::build(const Cell& cell) {
    Result result;
    result.cell = cell;
    result.plan = cachedPlan(cell);
    result.meshes.reserve(size_t(result.plan->islandCount));
    for (int i = 0; i < result.plan->islandCount; ++i) result.meshes.push_back(islandMesh(result.plan->islands[i]));
    return result;
}

void IslandBuilder::request(const std::vector<Cell>& cells) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        for (auto cell = cells.rbegin(); cell != cells.rend(); ++cell) {
            const Key key{cell->x, cell->z};
            if (!running_.count(key) && !done_.count(key)) queue_.push_back(*cell);
        }
    }
    wake_.notify_all();
}

std::vector<IslandBuilder::Result> IslandBuilder::collect() {
    std::vector<Result> results;
    std::lock_guard<std::mutex> lock(mutex_);
    results.reserve(done_.size());
    for (auto& [key, result] : done_) results.push_back(std::move(result));
    done_.clear();
    return results;
}

IslandBuilder::Result IslandBuilder::wait(const Cell& cell) {
    const Key key{cell.x, cell.z};
    std::unique_lock<std::mutex> lock(mutex_);
    finished_.wait(lock, [&] { return !running_.count(key); });
    const auto found = done_.find(key);
    if (found != done_.end()) {
        Result result = std::move(found->second);
        done_.erase(found);
        return result;
    }
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [&](const Cell& queued) { return queued.x == cell.x && queued.z == cell.z; }),
                 queue_.end());
    lock.unlock();
    return build(cell);
}

void IslandBuilder::work() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
        if (stop_) return;
        const Cell cell = queue_.back();
        queue_.pop_back();
        const Key key{cell.x, cell.z};
        running_.insert(key);
        lock.unlock();
        Result result = build(cell);
        lock.lock();
        running_.erase(key);
        done_[key] = std::move(result);
        finished_.notify_all();
    }
}
}
