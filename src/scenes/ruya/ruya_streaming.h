// Builds cell plans and island meshes on worker threads ahead of the camera.
// Both are pure functions of the cell, so which thread builds one, and when,
// cannot change what is drawn.
#pragma once
#include "scenes/ruya/ruya_island.h"

#include <condition_variable>
#include <exception>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ruya {
class IslandBuilder {
public:
    struct Result {
        Cell cell{};
        std::shared_ptr<const CellPlan> plan;
        std::vector<Mesh> meshes;
    };
    explicit IslandBuilder(unsigned workers);
    ~IslandBuilder();
    IslandBuilder(const IslandBuilder&) = delete;
    IslandBuilder& operator=(const IslandBuilder&) = delete;

    // Replaces the queue with these cells, nearest first. Cells already running or
    // finished are skipped, so the caller may pass cells it has not collected yet.
    void request(const std::vector<Cell>& cells);
    // Finished cells, without waiting.
    std::vector<Result> collect();
    // The cell's result, building it on the calling thread if no worker has started it.
    Result wait(const Cell& cell);

private:
    struct Registry;
    static Registry& registry();
    void stop();
    using Key = std::pair<int, int>;
    static Result build(const Cell& cell);
    void work();

    std::mutex mutex_;
    std::condition_variable wake_, finished_;
    // Reversed priority: the nearest cell is at the back.
    std::vector<Cell> queue_;
    std::set<Key> running_;
    std::map<Key, Result> done_;
    bool stop_ = false;
    std::exception_ptr error_;
    std::vector<std::thread> threads_;
};
}
