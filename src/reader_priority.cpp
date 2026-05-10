#include "reader_priority.hpp"
#include "logger.hpp"
#include <string>

ReaderPriorityLock::ReaderPriorityLock()
    : mutex_(1)   // binary semaphore protecting readCount
    , wrt_(1)     // binary semaphore for exclusive write access
{}

// ─────────────────────────────────────────────────────────────────────────────
// readLock
// ─────────────────────────────────────────────────────────────────────────────

void ReaderPriorityLock::readLock(ThreadInfo* info) {
    transitionWaiting(info, "Reader requesting lock");

    mutex_.wait();
    int rc = ++readCount_;
    if (rc == 1) {
        wrt_.wait();
    }
    mutex_.post();
    std::string msg = "Reader acquired lock, readers=" + std::to_string(rc);
    transitionActive(info, msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// readUnlock
// ─────────────────────────────────────────────────────────────────────────────

void ReaderPriorityLock::readUnlock(ThreadInfo* info) {
    mutex_.wait();
    int rc = --readCount_;
    if (rc == 0) {
        wrt_.post();
    }
    mutex_.post();

    std::string msg = "Reader released lock, readers=" + std::to_string(rc);
    transitionReleasing(info, msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// writeLock
// ─────────────────────────────────────────────────────────────────────────────

void ReaderPriorityLock::writeLock(ThreadInfo* info) {
    transitionWaiting(info, "Writer requesting lock");

    wrt_.wait();

    writerActive_.store(true, std::memory_order_release);

    transitionActive(info, "Writer acquired exclusive lock");
}

// ─────────────────────────────────────────────────────────────────────────────
// writeUnlock
// ─────────────────────────────────────────────────────────────────────────────

void ReaderPriorityLock::writeUnlock(ThreadInfo* info) {
    writerActive_.store(false, std::memory_order_release);
    wrt_.post();

    transitionReleasing(info, "Writer released exclusive lock");
}
