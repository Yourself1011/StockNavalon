#include "doctest/doctest.h"
#include "parallel.h"
#include <atomic>
#include <stdexcept>
#include <vector>

TEST_CASE("a thread pool runs every index once, call after call") {
    for (int threads : {1, 2, 4}) {
        CAPTURE(threads);
        ThreadPool pool(threads);
        CHECK(pool.size() == threads);
        for (int n : {0, 1, 3, 100, 1000}) {
            std::vector<std::atomic<int>> hits(n);
            pool.run(n, [&](int i) { hits[i]++; });
            for (int i = 0; i < n; i++) {
                REQUIRE(hits[i] == 1);
            }
        }
    }
}

TEST_CASE("a thread pool passes on an exception and keeps working") {
    ThreadPool pool(4);
    CHECK_THROWS_AS(pool.run(100,
                             [](int i) {
                                 if (i == 37) {
                                     throw std::runtime_error("no");
                                 }
                             }),
                    std::runtime_error);
    std::atomic<int> sum{0};
    pool.run(100, [&](int i) { sum += i; });
    CHECK(sum == 4950);
}
