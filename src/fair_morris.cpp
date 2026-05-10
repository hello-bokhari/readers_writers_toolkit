#include "fair_morris.hpp"
#include "logger.hpp"
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

FairMorrisLock::FairMorrisLock() {
    pthread_mutex_init(&mtx_, nullptr);
    pthread_cond_init(&readCond_,  nullptr);
    pthread_cond_init(&writeCond_, nullptr);
}

FairMorrisLock::~FairMorrisLock() {
    pthread_cond_destroy(&writeCond_);
    pthread_cond_destroy(&readCond_);
    pthread_mutex_destroy(&mtx_);
}

// ─────────────────────────────────────────────────────────────────────────────
// readLock
// ─────────────────────────────────────────────────────────────────────────────

void FairMorrisLock::readLock(ThreadInfo* info) {
    transitionWaiting(info, "Reader requesting lock (Morris FIFO)");

    pthread_mutex_lock(&mtx_);

    while (activeWriter_.load(std::memory_order_relaxed) || waitingWriters_ > 0) {
        ++waitingReaders_;
        pthread_cond_wait(&readCond_, &mtx_);
        --waitingReaders_;
    }

    int rc = activeReaders_.fetch_add(1, std::memory_order_acq_rel) + 1;
    pthread_mutex_unlock(&mtx_);

    std::string msg = "Reader acquired lock (FIFO), readers=" + std::to_string(rc);
    transitionActive(info, msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// readUnlock
// ─────────────────────────────────────────────────────────────────────────────

void FairMorrisLock::readUnlock(ThreadInfo* info) {
    pthread_mutex_lock(&mtx_);

    int rc = activeReaders_.fetch_sub(1, std::memory_order_acq_rel) - 1;

    if (rc == 0) {
        if (waitingWriters_ > 0) {
            pthread_cond_signal(&writeCond_);
        } else if (waitingReaders_ > 0) {
            pthread_cond_broadcast(&readCond_);
        }
    }

    pthread_mutex_unlock(&mtx_);

    std::string msg = "Reader released lock (FIFO), readers=" + std::to_string(rc);
    transitionReleasing(info, msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// writeLock
// ─────────────────────────────────────────────────────────────────────────────

void FairMorrisLock::writeLock(ThreadInfo* info) {
    transitionWaiting(info, "Writer requesting lock (Morris FIFO)");

    pthread_mutex_lock(&mtx_);

    while (activeReaders_.load(std::memory_order_relaxed) > 0 ||
           activeWriter_.load(std::memory_order_relaxed)) {
        ++waitingWriters_;
        pthread_cond_wait(&writeCond_, &mtx_);
        --waitingWriters_;
    }

    activeWriter_.store(true, std::memory_order_release);
    pthread_mutex_unlock(&mtx_);

    transitionActive(info, "Writer acquired exclusive lock (Morris FIFO)");
}

// ─────────────────────────────────────────────────────────────────────────────
// writeUnlock
// ─────────────────────────────────────────────────────────────────────────────

void FairMorrisLock::writeUnlock(ThreadInfo* info) {
    pthread_mutex_lock(&mtx_);

    activeWriter_.store(false, std::memory_order_release);

    if (waitingReaders_ > 0) {
        pthread_cond_broadcast(&readCond_);
    } else if (waitingWriters_ > 0) {
        pthread_cond_signal(&writeCond_);
    }

    pthread_mutex_unlock(&mtx_);

    transitionReleasing(info, "Writer released exclusive lock (Morris FIFO)");
}
