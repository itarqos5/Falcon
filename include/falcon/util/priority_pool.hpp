#pragma once

#include "falcon/platform/sampling_profiler.hpp"
#include "falcon/platform/system_info.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace falcon {

// A few worker threads that always run the waiting job with the smallest priority number first (for world
// streaming: priority is the distance to the player, so the nearest chunks are made first). Jobs report their
// results themselves (typically into a mutex-protected list the main thread drains); the pool has no futures.
// Destroying the pool finishes the jobs that are running and drops the ones still waiting.
//
// All the threads it will ever use are made at the start, but only the first `active` of them take jobs (setActive changes that): the rest sleep. So the world can be made by
// most of the PC while it loads, and by about half of it once it is being played. The threads run at below-normal priority, so the frame thread keeps its core either way.
class PriorityPool {
public:
    explicit PriorityPool(unsigned threads) : active_(std::max(1u, threads)) {
        for (unsigned i = 0; i < std::max(1u, threads); ++i) workers_.emplace_back([this, i] { run(i); });
    }
    ~PriorityPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        for (std::thread& worker : workers_) worker.join();
    }
    PriorityPool(const PriorityPool&) = delete;
    PriorityPool& operator=(const PriorityPool&) = delete;

    void submit(float priority, std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(Task{priority, next_++, std::move(job)});
        }
        wake_.notify_one();
    }
    // Drops every job that has not started yet.
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_ = {};
    }
    // Blocks until no job is waiting and none is running (after clear(): what was started has finished).
    void waitIdle() {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [this] { return queue_.empty() && running_ == 0; });
    }
    [[nodiscard]] std::size_t waiting() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    [[nodiscard]] unsigned threadCount() const { return static_cast<unsigned>(workers_.size()); }
    // How many of the threads take jobs (1 .. threadCount()). A thread that is in the middle of a job finishes it first.
    void setActive(unsigned active) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = std::clamp(active, 1u, threadCount());
        }
        wake_.notify_all();
    }
    [[nodiscard]] unsigned activeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

private:
    struct Task {
        float priority;
        std::uint64_t order;  // first come, first served among equals
        std::function<void()> job;
        bool operator<(const Task& other) const {  // std::priority_queue keeps the greatest first, so invert
            return priority != other.priority ? priority > other.priority : order > other.order;
        }
    };

    void run(unsigned index) {
        falcon::profiler::registerThread("worker");  // (does nothing unless FALCON_SAMPLE is set)
        lowerCurrentThreadPriority();
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this, index] { return stopping_ || (index < active_ && !queue_.empty()); });
                if (stopping_) return;
                task = std::move(const_cast<Task&>(queue_.top()));
                queue_.pop();
                ++running_;
            }
            task.job();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                --running_;
            }
            idle_.notify_all();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    unsigned running_ = 0;  // jobs being run (under mutex_)
    std::priority_queue<Task> queue_;
    std::vector<std::thread> workers_;
    std::uint64_t next_ = 0;
    unsigned active_ = 1;  // (under mutex_)
    bool stopping_ = false;
};

}  // namespace falcon
