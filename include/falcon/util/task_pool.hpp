#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace falcon {

// A few worker threads that run submitted jobs in the order they were submitted. Used to do the
// start-up loading beside the main thread without starving it: with more threads than free cores
// the main thread, which is busy starting the graphics driver, would slow down.
//
//     TaskPool pool(4);
//     auto language = pool.submit([&] { return Language::load(resources, "en_us"); });
//     ...                                  // main thread keeps going
//     Language l = language.get();         // waits only if the job is not finished yet
//
// Destroying the pool finishes every queued job first. Exceptions thrown by a job come out of get().
class TaskPool {
public:
    explicit TaskPool(unsigned threads) {
        for (unsigned i = 0; i < std::max(1u, threads); ++i) workers_.emplace_back([this] { run(); });
    }
    ~TaskPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        for (std::thread& worker : workers_) worker.join();
    }
    TaskPool(const TaskPool&) = delete;
    TaskPool& operator=(const TaskPool&) = delete;

    template <class F>
    [[nodiscard]] auto submit(F&& job) -> std::future<std::invoke_result_t<F>> {
        using Result = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(job));
        std::future<Result> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push([task] { (*task)(); });
        }
        wake_.notify_one();
        return future;
    }

private:
    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) return;  // stopping, and nothing left to do
                job = std::move(queue_.front());
                queue_.pop();
            }
            job();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::queue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
};

}  // namespace falcon
