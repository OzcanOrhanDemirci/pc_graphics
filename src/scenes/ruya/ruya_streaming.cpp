#include "scenes/ruya/ruya_streaming.h"

#include <algorithm>

namespace ruya {
// Registered after PlanCache, so even std::exit joins workers before that cache dies.
struct IslandBuilder::Registry {
    std::mutex mutex;
    std::vector<IslandBuilder*> builders;
    ~Registry() {
        std::lock_guard<std::mutex> lock(mutex);
        for (IslandBuilder* builder : builders) builder->stop();
    }
};

IslandBuilder::Registry& IslandBuilder::registry() {
    static Registry value;
    return value;
}

IslandBuilder::IslandBuilder(unsigned workers) {
    cachedPlan(describeCell(0, 0));
    Registry& active = registry();
    std::lock_guard<std::mutex> lock(active.mutex);
    active.builders.push_back(this);
    try {
        for (unsigned i = 0; i < std::max(1U, workers); ++i) threads_.emplace_back([this] { work(); });
    } catch (...) {
        stop();
        active.builders.pop_back();
        throw;
    }
}

IslandBuilder::~IslandBuilder() {
    Registry& active = registry();
    std::lock_guard<std::mutex> lock(active.mutex);
    stop();
    active.builders.erase(std::remove(active.builders.begin(), active.builders.end(), this), active.builders.end());
}

void IslandBuilder::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        queue_.clear();
    }
    wake_.notify_all();
    finished_.notify_all();
    for (std::thread& thread : threads_) if (thread.joinable()) thread.join();
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
        if (error_) std::rethrow_exception(error_);
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
    if (error_) std::rethrow_exception(error_);
    results.reserve(done_.size());
    for (auto& [key, result] : done_) results.push_back(std::move(result));
    done_.clear();
    return results;
}

IslandBuilder::Result IslandBuilder::wait(const Cell& cell) {
    const Key key{cell.x, cell.z};
    std::unique_lock<std::mutex> lock(mutex_);
    finished_.wait(lock, [&] { return error_ || !running_.count(key); });
    if (error_) std::rethrow_exception(error_);
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
    try {
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
    } catch (...) {
        if (!lock.owns_lock()) lock.lock();
        if (!error_) error_ = std::current_exception();
        stop_ = true;
        queue_.clear();
        wake_.notify_all();
        finished_.notify_all();
    }
}
}
