#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include "config.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Thread state enumeration  (proposal §7.1)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * ThreadState — every reader/writer thread is always in exactly one of these.
 *
 *  INIT       Thread object created and registered.
 *  WAITING    Thread has called lock(); blocked on semaphore/mutex.
 *  ACTIVE     Thread holds the lock and is performing its read or write.
 *  BLOCKED    Wait time exceeded STARVE_MS; watchdog flagged starvation.
 *  RELEASING  Thread finished its operation; releasing synchronization primitive.
 *  DONE       Thread has fully exited and freed all resources.
 */
enum class ThreadState : uint8_t {
    INIT      = 0,
    WAITING   = 1,
    ACTIVE    = 2,
    BLOCKED   = 3,
    RELEASING = 4,
    DONE      = 5
};

/// Convert a ThreadState to its canonical string name (used in log records).
inline const char* stateName(ThreadState s) {
    switch (s) {
        case ThreadState::INIT:      return "INIT";
        case ThreadState::WAITING:   return "WAITING";
        case ThreadState::ACTIVE:    return "ACTIVE";
        case ThreadState::BLOCKED:   return "BLOCKED";
        case ThreadState::RELEASING: return "RELEASING";
        case ThreadState::DONE:      return "DONE";
    }
    return "UNKNOWN";
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread type
// ─────────────────────────────────────────────────────────────────────────────

enum class ThreadType : uint8_t {
    READER = 0,
    WRITER = 1
};

inline const char* threadTypeName(ThreadType t) {
    return (t == ThreadType::READER) ? "R" : "W";
}

// ─────────────────────────────────────────────────────────────────────────────
// ThreadInfo — shared record for one reader or writer thread
// ─────────────────────────────────────────────────────────────────────────────

using Clock    = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Ms        = std::chrono::milliseconds;

struct ThreadInfo {
    // ── Identity ──────────────────────────────────────────────────────────
    int        tid        = 0;                      
    ThreadType type       = ThreadType::READER;
    AlgoType   algo       = AlgoType::READER_PRIORITY;

    // ── State ─────────────────────────────────────────────────────────────
    std::atomic<ThreadState> state{ThreadState::INIT};

    // ── Timing ────────────────────────────────────────────────────────────
    TimePoint  waitStart;     ///< When the thread entered WAITING state
    TimePoint  activeStart;   ///< When the thread entered ACTIVE state
    TimePoint  doneTime;      ///< When the thread entered DONE state

    // ── Accumulated metrics ────────────────────────────────────────────────
    long       totalWaitMs   = 0;  ///< Cumulative wait time across all ops
    int        opsCompleted  = 0;  ///< Number of read/write ops finished
    int        starveCount   = 0;  ///< Number of times starvation threshold hit

    // ── Abort flag (TC-12) ─────────────────────────────────────────────────
    std::atomic<bool> abortRequested{false};

    // ── Non-copyable (atomic members are not copyable) ────────────────────
    ThreadInfo() = default;
    ThreadInfo(const ThreadInfo&) = delete;
    ThreadInfo& operator=(const ThreadInfo&) = delete;

    // ─────────────────────────────────────────────────────────────────────
    // Helper: record transition into WAITING and capture start time
    // ─────────────────────────────────────────────────────────────────────
    void enterWaiting() {
        waitStart = Clock::now();
        state.store(ThreadState::WAITING, std::memory_order_release);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Helper: record transition into ACTIVE; accumulate wait time
    // ─────────────────────────────────────────────────────────────────────
    void enterActive() {
        activeStart = Clock::now();
        auto waited = std::chrono::duration_cast<Ms>(activeStart - waitStart).count();
        totalWaitMs += waited;
        state.store(ThreadState::ACTIVE, std::memory_order_release);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Helper: current wait duration in ms (called from watchdog)
    // ─────────────────────────────────────────────────────────────────────
    long currentWaitMs() const {
        if (state.load(std::memory_order_acquire) == ThreadState::WAITING) {
            auto now = Clock::now();
            return std::chrono::duration_cast<Ms>(now - waitStart).count();
        }
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Helper: last recorded wait in ms (for log records during transition)
    // ─────────────────────────────────────────────────────────────────────
    long lastWaitMs() const {
        auto now = Clock::now();
        auto ref = (state.load(std::memory_order_acquire) == ThreadState::WAITING)
                   ? waitStart : activeStart;
        return std::chrono::duration_cast<Ms>(now - ref).count();
    }
};
