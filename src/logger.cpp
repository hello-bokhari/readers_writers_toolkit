#include "logger.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <time.h>
#include <unistd.h> 

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::Logger() {
    pthread_mutex_init(&writeMutex_, nullptr);
}

Logger::~Logger() {
    shutdown();
    pthread_mutex_destroy(&writeMutex_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void Logger::init(LogLevel level, const std::string& filepath) {
    pthread_mutex_lock(&writeMutex_);
    level_ = level;
    if (!filepath.empty()) {
        if (logFile_) { fclose(logFile_); logFile_ = nullptr; }
        logFile_ = fopen(filepath.c_str(), "w");
        if (!logFile_) {
            fprintf(stderr, "[Logger] WARNING: could not open log file '%s': %s\n",
                    filepath.c_str(), strerror(errno));
        }
    }
    pthread_mutex_unlock(&writeMutex_);
}

void Logger::shutdown() {
    pthread_mutex_lock(&writeMutex_);
    if (logFile_) {
        fflush(logFile_);
        fclose(logFile_);
        logFile_ = nullptr;
    }
    pthread_mutex_unlock(&writeMutex_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Timestamp formatting
// ─────────────────────────────────────────────────────────────────────────────

void Logger::formatTimestamp(char* buf, size_t len) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);

    char hms[10];
    strftime(hms, sizeof(hms), "%H:%M:%S", &tm_info);

    int ms = static_cast<int>(ts.tv_nsec / 1'000'000L);
    snprintf(buf, len, "%s.%03d", hms, ms);
}

// ─────────────────────────────────────────────────────────────────────────────
// Filtering
// ─────────────────────────────────────────────────────────────────────────────

bool Logger::shouldLog(ThreadState state) const {
    switch (level_) {
        case LogLevel::VERBOSE:
            return true;  
        case LogLevel::STANDARD:
            return (state == ThreadState::WAITING  ||
                    state == ThreadState::ACTIVE   ||
                    state == ThreadState::BLOCKED  ||
                    state == ThreadState::DONE);
        case LogLevel::SUMMARY:
            return false;
    }
    return true;
}


void Logger::writeLine(const char* line) {
    puts(line);

    if (logFile_) {
        fputs(line, logFile_);
        fputc('\n', logFile_);
    }
}


void Logger::record(const ThreadInfo& info,
                    ThreadState        state,
                    long               waitMs,
                    const std::string& msg) {
    if (!shouldLog(state)) return;

    char tsBuf[20];
    formatTimestamp(tsBuf, sizeof(tsBuf));

    char waitBuf[32];
    if (waitMs >= 0) {
        snprintf(waitBuf, sizeof(waitBuf), "%ldms", waitMs);
    } else {
        snprintf(waitBuf, sizeof(waitBuf), "--");
    }

    char line[512];
    snprintf(line, sizeof(line),
        "[%s] | TID:%03d | TYPE:%-1s | ALGO:%-2s | STATE:%-9s | WAIT:%-8s | MSG:%s",
        tsBuf,
        info.tid,
        threadTypeName(info.type),
        algoShortName(info.algo),
        stateName(state),
        waitBuf,
        msg.c_str()
    );

    pthread_mutex_lock(&writeMutex_);
    writeLine(line);
    pthread_mutex_unlock(&writeMutex_);
}

// ─────────────────────────────────────────────────────────────────────────────
// summary() — final per-thread statistics line
// ─────────────────────────────────────────────────────────────────────────────

void Logger::summary(const ThreadInfo& info) {
    char tsBuf[20];
    formatTimestamp(tsBuf, sizeof(tsBuf));

    long avgWait = (info.opsCompleted > 0)
                 ? (info.totalWaitMs / info.opsCompleted)
                 : 0;

    char line[512];
    snprintf(line, sizeof(line),
        "[%s] | TID:%03d | TYPE:%-1s | ALGO:%-2s | SUMMARY | "
        "OPS:%d | TOTAL_WAIT:%ldms | AVG_WAIT:%ldms | STARVE_EVENTS:%d",
        tsBuf,
        info.tid,
        threadTypeName(info.type),
        algoShortName(info.algo),
        info.opsCompleted,
        info.totalWaitMs,
        avgWait,
        info.starveCount
    );

    pthread_mutex_lock(&writeMutex_);
    writeLine(line);
    pthread_mutex_unlock(&writeMutex_);
}

// ─────────────────────────────────────────────────────────────────────────────
// alert() — starvation warning
// ─────────────────────────────────────────────────────────────────────────────

void Logger::alert(const ThreadInfo& info, long waitMs) {
    char tsBuf[20];
    formatTimestamp(tsBuf, sizeof(tsBuf));

    char line[512];
    snprintf(line, sizeof(line),
        "[%s] | TID:%03d | TYPE:%-1s | ALGO:%-2s | STATE:BLOCKED    | WAIT:%-8ldms | MSG:STARVATION ALERT - threshold exceeded",
        tsBuf,
        info.tid,
        threadTypeName(info.type),
        algoShortName(info.algo),
        waitMs
    );

    pthread_mutex_lock(&writeMutex_);
    fprintf(stderr, "\033[1;31m[STARVE ALERT] TID:%03d TYPE:%s WAIT:%ldms\033[0m\n",
            info.tid, threadTypeName(info.type), waitMs);
    writeLine(line);
    pthread_mutex_unlock(&writeMutex_);
}

// ─────────────────────────────────────────────────────────────────────────────
// header() — section separator
// ─────────────────────────────────────────────────────────────────────────────

void Logger::header(const std::string& text) {
    char line[512];
    snprintf(line, sizeof(line),
        "\n======================================================\n"
        "  %s\n"
        "======================================================",
        text.c_str()
    );

    pthread_mutex_lock(&writeMutex_);
    writeLine(line);
    pthread_mutex_unlock(&writeMutex_);
}
