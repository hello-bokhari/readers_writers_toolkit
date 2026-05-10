#pragma once

#include <pthread.h>
#include <cstdio>
#include <string>
#include "config.hpp"
#include "thread_state.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Logger — singleton, process-wide
// ─────────────────────────────────────────────────────────────────────────────

class Logger {
public:
    // ── Singleton access ─────────────────────────────────────────────────
    static Logger& instance();

    // ── Initialization ────────────────────────────────────────────────────
    /**
     * Configure the logger.  Must be called once before any threads start.
     *
     * @param level    Verbosity level.
     * @param filepath Optional path for a log file.  Empty string = stdout only.
     */
    void init(LogLevel level, const std::string& filepath = "");

    /** Flush and close the log file (if open). */
    void shutdown();

    // ── Primary logging interface ─────────────────────────────────────────

    /**
     * record() — emit one structured log line.
     *
     * Internally acquires the write mutex, formats the fixed-width
     * pipe-delimited record, and writes to stdout (and log file if set).
     *
     * Whether the line is actually emitted depends on the current log level
     * and the state being logged:
     *   VERBOSE  → always emitted
     *   STANDARD → only WAITING, ACTIVE, BLOCKED, DONE
     *   SUMMARY  → suppressed (use summary() for final stats)
     *
     * @param info    ThreadInfo for the calling thread.
     * @param state   The state being entered (used for filtering + display).
     * @param waitMs  Current or just-ended wait time in ms.
     * @param msg     Human-readable message for the MSG field.
     */
    void record(const ThreadInfo& info,ThreadState state,long waitMs,const std::string& msg);

    /**
     * summary() — emit the final SUMMARY line for one thread.
     *
     * Always emitted regardless of log level (it is the only line emitted
     * in SUMMARY mode).
     *
     * @param info  Completed ThreadInfo (state must be DONE).
     */
    void summary(const ThreadInfo& info);

    /**
     * alert() — write a starvation alert to both stderr and the log.
     *
     * Called by the watchdog when a thread exceeds STARVE_MS.
     */
    void alert(const ThreadInfo& info, long waitMs);

    /**
     * header() — print a separator / section banner.
     * Useful for delineating test cases or benchmark runs.
     */
    void header(const std::string& text);

    // ── State ─────────────────────────────────────────────────────────────
    LogLevel level() const { return level_; }

private:
    Logger();
    ~Logger();
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    void writeLine(const char* line);

    // Formats a wall-clock timestamp as "HH:MM:SS.mmm" into buf (≥14 chars).
    static void formatTimestamp(char* buf, size_t len);

    bool shouldLog(ThreadState state) const;

    pthread_mutex_t writeMutex_ = PTHREAD_MUTEX_INITIALIZER;
    LogLevel        level_      = LogLevel::VERBOSE;
    FILE*           logFile_    = nullptr;
};
