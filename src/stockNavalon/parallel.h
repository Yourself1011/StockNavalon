#pragma once

// Running independent pieces of work over threads

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

// Runs f(i) for every i below n, over up to `threads` threads started for
// this call. Each i must be independent of the others. For work done once;
// work repeated many times a second wants a ThreadPool.
template <typename F> void parallelFor(int n, int threads, F f) {
    threads = std::min(threads, n);
    if (threads <= 1) {
        for (int i = 0; i < n; i++) {
            f(i);
        }
        return;
    }
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) {
        pool.emplace_back([&] {
            for (int i = next++; i < n; i = next++) {
                f(i);
            }
        });
    }
    for (std::thread &t : pool) {
        t.join();
    }
}

// Threads kept waiting between calls, so a call costs a wake-up rather than
// starting threads. The calling thread works too: a pool of `threads` starts
// threads - 1 of its own, and a pool of one runs everything on the caller.
// One call at a time.
class ThreadPool {
  public:
    explicit ThreadPool(int threads) {
        for (int t = 1; t < threads; t++) {
            workers.emplace_back([this] { work(); });
        }
    }
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        for (std::thread &t : workers) {
            t.join();
        }
    }
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    int size() const { return static_cast<int>(workers.size()) + 1; }

    // Runs f(i) for every i below n, as parallelFor does. The first
    // exception thrown is rethrown here once every thread has stopped.
    void run(int n, const std::function<void(int)> &f) {
        if (workers.empty() || n <= 1) {
            for (int i = 0; i < n; i++) {
                f(i);
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            job = &f;
            count = n;
            next = 0;
            busy = static_cast<int>(workers.size());
            error = nullptr;
            generation++;
        }
        wake.notify_all();
        take();
        // the job lives on this call's stack: every worker must be done
        std::unique_lock<std::mutex> lock(mutex);
        idle.wait(lock, [this] { return busy == 0; });
        job = nullptr;
        if (error) {
            std::rethrow_exception(std::exchange(error, nullptr));
        }
    }

  private:
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable wake, idle;
    const std::function<void(int)> *job = nullptr;
    int count = 0, busy = 0;
    std::atomic<int> next{0};
    std::uint64_t generation = 0;
    bool stopping = false;
    std::exception_ptr error;

    // takes indices of the current job until there are none left
    void take() {
        for (int i = next++; i < count; i = next++) {
            try {
                (*job)(i);
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex);
                if (!error) {
                    error = std::current_exception();
                }
                // the rest are skipped
                next = count;
            }
        }
    }

    void work() {
        std::uint64_t seen = 0;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stopping || generation != seen; });
                if (stopping) {
                    return;
                }
                seen = generation;
            }
            take();
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (--busy > 0) {
                    continue;
                }
            }
            idle.notify_one();
        }
    }
};
