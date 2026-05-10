#pragma once

#include <cstdint>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time constants
// ─────────────────────────────────────────────────────────────────────────────

/// Starvation threshold in milliseconds (proposal §6.1).
/// A WAITING thread exceeding this becomes BLOCKED.
constexpr int STARVE_MS = 500;

/// Watchdog polling interval in milliseconds (proposal §7.4).
constexpr int WATCHDOG_POLL_MS = 50;

/// Number of independent benchmark runs per configuration (proposal §5.4).
constexpr int BENCHMARK_RUNS = 5;

/// Simulated read-operation duration in milliseconds.
constexpr int READ_WORK_MS = 100;

/// Simulated write-operation duration in milliseconds.
constexpr int WRITE_WORK_MS = 150;

/// Maximum threads supported in a single simulation.
constexpr int MAX_THREADS = 64;

// ─────────────────────────────────────────────────────────────────────────────
// Algorithm variants  (proposal §3)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * AlgoType — selects which Readers–Writers variant to use.
 *
 *  READER_PRIORITY  Courtois (1971): readers never wait while other readers hold lock.
 *  WRITER_PRIORITY  Courtois (1971): a queued writer blocks all new readers.
 *  FAIR_MORRIS      Morris  (1979): FIFO ordering; no starvation of either side.
 */
enum class AlgoType : uint8_t {
    READER_PRIORITY = 0,
    WRITER_PRIORITY = 1,
    FAIR_MORRIS     = 2
};

/// Human-readable string for an AlgoType (used in log records as RP/WP/FA).
inline const char* algoShortName(AlgoType a) {
    switch (a) {
        case AlgoType::READER_PRIORITY: return "RP";
        case AlgoType::WRITER_PRIORITY: return "WP";
        case AlgoType::FAIR_MORRIS:     return "FA";
    }
    return "??";
}

inline const char* algoLongName(AlgoType a) {
    switch (a) {
        case AlgoType::READER_PRIORITY: return "Reader-Priority";
        case AlgoType::WRITER_PRIORITY: return "Writer-Priority";
        case AlgoType::FAIR_MORRIS:     return "Fair (Morris)";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// Log verbosity levels  (proposal §7.3)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * LogLevel — controls how much the logger emits.
 *
 *  VERBOSE   All state transitions including INIT and RELEASING. Default during testing.
 *  STANDARD  WAITING, ACTIVE, BLOCKED, DONE only. Default for benchmarking.
 *  SUMMARY   Final per-thread statistics only (wait time, ops completed).
 */
enum class LogLevel : uint8_t {
    VERBOSE  = 0,
    STANDARD = 1,
    SUMMARY  = 2
};

// ─────────────────────────────────────────────────────────────────────────────
// Load profiles  (proposal §5.4)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * LoadProfile — pre-defined benchmark configurations.
 *
 *  LIGHT     3 readers + 1 writer,   100 total operations
 *  BALANCED  5 readers + 5 writers,  500 total operations
 *  HEAVY     10 readers + 10 writers, 1000 total operations
 *  CUSTOM    User-specified via --readers / --writers / --ops CLI flags
 */
enum class LoadProfile : uint8_t {
    LIGHT    = 0,
    BALANCED = 1,
    HEAVY    = 2,
    CUSTOM   = 3
};

// ─────────────────────────────────────────────────────────────────────────────
// Runtime simulation configuration
// ─────────────────────────────────────────────────────────────────────────────

struct SimConfig {
    // ── Algorithm ─────────────────────────────────────────────────────────
    AlgoType  algo        = AlgoType::READER_PRIORITY;

    // ── Thread counts ─────────────────────────────────────────────────────
    int       numReaders  = 5;
    int       numWriters  = 3;

    // ── Operations ────────────────────────────────────────────────────────
    /// Total read+write operations to perform before the simulation ends.
    int       totalOps    = 100;

    // ── Logging ───────────────────────────────────────────────────────────
    LogLevel  logLevel    = LogLevel::VERBOSE;

    /// If non-empty, write log to this file in addition to stdout.
    std::string logFile;

    // ── Benchmarking ──────────────────────────────────────────────────────
    /// Run benchmark mode: all profiles × all algorithms × BENCHMARK_RUNS.
    bool      benchmarkMode = false;

    /// CSV output path for benchmark results.
    std::string csvOutput;

    // ── Starvation ────────────────────────────────────────────────────────
    /// Configurable starvation threshold (default = STARVE_MS constant).
    int       starveThresholdMs = STARVE_MS;

    // ── Timing ────────────────────────────────────────────────────────────
    int       readWorkMs  = READ_WORK_MS;
    int       writeWorkMs = WRITE_WORK_MS;

    // ── Signal handling ───────────────────────────────────────────────────
    bool      enableAbortTest = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Load profile helpers
// ─────────────────────────────────────────────────────────────────────────────

inline void applyLoadProfile(SimConfig& cfg, LoadProfile profile) {
    switch (profile) {
        case LoadProfile::LIGHT:
            cfg.numReaders = 3;
            cfg.numWriters = 1;
            cfg.totalOps   = 100;
            break;
        case LoadProfile::BALANCED:
            cfg.numReaders = 5;
            cfg.numWriters = 5;
            cfg.totalOps   = 500;
            break;
        case LoadProfile::HEAVY:
            cfg.numReaders = 10;
            cfg.numWriters = 10;
            cfg.totalOps   = 1000;
            break;
        case LoadProfile::CUSTOM:
        default:
            break;
    }
}
