#pragma once

// Running independent pieces of work over threads

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

// Runs f(i) for every i below n, over up to `threads` threads. Each i must
// be independent of the others.
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
