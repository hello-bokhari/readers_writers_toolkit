#include "writer_priority.hpp"
#include "logger.hpp"
#include <string>

WriterPriorityLock::WriterPriorityLock()
    : mutex1_(1)    // protects readCount
    , mutex2_(1)    // protects writeCount
    , rsem_(1)      // reader entry gate; held by first writer
    , wrt_(1)       // exclusive write lock
    , readTry_(1)   // serializes readers competing for rsem
{}

// ─────────────────────────────────────────────────────────────────────────────
// readLock
// ─────────────────────────────────────────────────────────────────────────────

void WriterPriorityLock::readLock(ThreadInfo* info) {
    transitionWaiting(info, "Reader requesting lock");

    readTry_.wait();

    rsem_.wait();

    mutex1_.wait();
    int rc = ++readCount_;
    if (rc == 1) {
        wrt_.wait();
    }
    mutex1_.post();
    rsem_.post();
    readTry_.post();

    std::string msg = "Reader acquired lock, readers=" + std::to_string(rc);
    transitionActive(info, msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// readUnlock
// ─────────────────────────────────────────────────────────────────────────────

void WriterPriorityLock::readUnlock(ThreadInfo* info) {
    mutex1_.wait();
    int rc = --readCount_;
    if (rc == 0) {
        wrt_.post();   // last reader frees exclusive lock
    }
    mutex1_.post();

    std::string msg = "Reader released lock, readers=" + std::to_string(rc);
    transitionReleasing(info, msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// writeLock
// ─────────────────────────────────────────────────────────────────────────────

void WriterPriorityLock::writeLock(ThreadInfo* info) {
    transitionWaiting(info, "Writer requesting lock");

    mutex2_.wait();
    int wc = ++writeCount_;
    if (wc == 1) {
        rsem_.wait();   // first writer: close the reader gate
    }
    mutex2_.post();

    wrt_.wait();

    writerActive_.store(true, std::memory_order_release);
    transitionActive(info, "Writer acquired exclusive lock");
}

// ─────────────────────────────────────────────────────────────────────────────
// writeUnlock
// ─────────────────────────────────────────────────────────────────────────────

void WriterPriorityLock::writeUnlock(ThreadInfo* info) {
    writerActive_.store(false, std::memory_order_release);

    wrt_.post();
    mutex2_.wait();
    int wc = --writeCount_;
    if (wc == 0) {
        rsem_.post();   // last writer: open reader gate
    }
    mutex2_.post();

    transitionReleasing(info, "Writer released exclusive lock");
}
