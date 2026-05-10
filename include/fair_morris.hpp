#pragma once

#include "rw_lock_base.hpp"
#include <pthread.h>
#include <atomic>

class FairMorrisLock : public RWLockBase {
public:
    FairMorrisLock();
    ~FairMorrisLock() override;

    void readLock   (ThreadInfo* info) override;
    void readUnlock (ThreadInfo* info) override;
    void writeLock  (ThreadInfo* info) override;
    void writeUnlock(ThreadInfo* info) override;

    AlgoType algo()         const override { return AlgoType::FAIR_MORRIS; }
    int      readerCount()  const override { return activeReaders_.load(); }
    bool     writerActive() const override { return activeWriter_.load();  }

private:
    pthread_mutex_t mtx_;         ///< Protects all state below
    pthread_cond_t  readCond_;    ///< Readers wait here
    pthread_cond_t  writeCond_;   ///< Writers wait here

    std::atomic<int>  activeReaders_{0};
    std::atomic<bool> activeWriter_{false};
    int               waitingReaders_{0};
    int               waitingWriters_{0};
    bool              readerPhaseActive_{false};
};
