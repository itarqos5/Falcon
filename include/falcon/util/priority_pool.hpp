#pragma once

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
class PriorityPool {
public:
    explicit PriorityPool(unsigned threads) {
        for (unsigned i = 0; i < std::max(1u, threads); ++i) workers_.emplace_back([this] { run(); });
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
    [[nodiscard]] std::size_t waiting() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    [[nodiscard]] unsigned threadCount() const { return static_cast<unsigned>(workers_.size()); }

private:
    struct Task {
        float priority;
        std::uint64_t order;  // first come, first served among equals
        std::function<void()> job;
        bool operator<(const Task& other) const {  // std::priority_queue keeps the greatest first, so invert
            return priority != other.priority ? priority > other.priority : order > other.order;
        }
    };

    void run() {
        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_) return;
                task = std::move(const_cast<Task&>(queue_.top()));
                queue_.pop();
            }
            task.job();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::priority_queue<Task> queue_;
    std::vector<std::thread> workers_;
    std::uint64_t next_ = 0;
    bool stopping_ = false;
};

}  // namespace falcon
