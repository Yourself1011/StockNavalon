#pragma once

// Where self-play's CPU time goes: thread time per phase, summed over the
// threads that ran it, for finding what to speed up. Off unless switched on
// (each timed scope then costs one branch); read and reset through the
// Python module (polyengine.profile).

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace profiling {

enum Phase {
    ADVANCE,      // advancing games: playing commands, starting searches
    START_SEARCH, // determinizing and setting up a decision's trees (in ADVANCE)
    REUSE,        // continuing a tree from the played move (in ADVANCE)
    DESCEND,      // the search choosing its next positions
    NEW_NODE,     // a new search position: the game copied, the command
                  // applied with its legal moves updated (in DESCEND)
    ENCODE,       // a position encoded for the network
    FINGERPRINT,  // its cache key
    CACHE,        // looking evaluations up in the cache, and keeping them
    MOVE_SLOTS,   // each request's moves as policy slots
    PRIORS,       // network outputs turned into priors and values
    EXPAND,       // the search taking its evaluations (expand and back up)
    // finer: parts of the phases above
    NODE_COPY,      // NEW_NODE: copying the game and its legal moves
    NODE_APPLY,     // NEW_NODE: the command applied, legal moves updated
    NODE_MAKE,      // NEW_NODE: the node built (its move list)
    REUSE_TREE,     // REUSE: the subtree kept and renumbered
    REUSE_CHECK,    // REUSE: checking the tree's position against the real one
    ENCODE_VIEW,    // ENCODE: the player's view of the game
    ENCODE_TILES,   // ENCODE: tiles, cities and units
    ENCODE_THREATS, // ENCODE: threats, reach and lethal damage
    ENCODE_GLOBALS, // ENCODE: the global vector
    PHASES
};

inline constexpr std::array<const char *, PHASES> NAMES{
    "advance",     "start_search", "reuse",       "descend",     "new_node",
    "encode",      "fingerprint",  "cache",       "move_slots",  "priors",
    "expand",      "node_copy",    "node_apply",  "node_make",   "reuse_tree",
    "reuse_check", "encode_view",  "encode_tiles", "encode_threats", "encode_globals"};

inline std::atomic<bool> enabled{false};
inline std::array<std::atomic<std::int64_t>, PHASES> nanoseconds{};
inline std::array<std::atomic<std::int64_t>, PHASES> counts{};

// times its scope into a phase, while profiling is on
class Timer {
  public:
    explicit Timer(Phase phase) : phase(phase), on(enabled.load(std::memory_order_relaxed)) {
        if (on) {
            start = std::chrono::steady_clock::now();
        }
    }
    ~Timer() {
        if (on) {
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();
            nanoseconds[phase].fetch_add(ns, std::memory_order_relaxed);
            counts[phase].fetch_add(1, std::memory_order_relaxed);
        }
    }
    Timer(const Timer &) = delete;
    Timer &operator=(const Timer &) = delete;

  private:
    Phase phase;
    bool on;
    std::chrono::steady_clock::time_point start;
};

// times consecutive stretches of a function into phases: each mark() ends
// the stretch since the last one
class Laps {
  public:
    Laps() : on(enabled.load(std::memory_order_relaxed)) {
        if (on) {
            last = std::chrono::steady_clock::now();
        }
    }
    void mark(Phase phase) {
        if (!on) {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        nanoseconds[phase].fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count(),
            std::memory_order_relaxed);
        counts[phase].fetch_add(1, std::memory_order_relaxed);
        last = now;
    }

  private:
    bool on;
    std::chrono::steady_clock::time_point last;
};

inline void reset() {
    for (int p = 0; p < PHASES; p++) {
        nanoseconds[p] = 0;
        counts[p] = 0;
    }
}

} // namespace profiling
