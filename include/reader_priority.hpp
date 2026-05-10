
#pragma once

#include "rw_lock_base.hpp"
#include "semaphore.hpp"
#include <atomic>

class ReaderPriorityLock : public RWLockBase {
public:
    ReaderPriorityLock();
    ~ReaderPriorityLock() override = default;

    void readLock  (ThreadInfo* info) override;
    void readUnlock(ThreadInfo* info) override;
    void writeLock (ThreadInfo* info) override;
    void writeUnlock(ThreadInfo* info) override;

    AlgoType algo()         const override { return AlgoType::READER_PRIORITY; }
    int      readerCount()  const override { return readCount_.load(); }
    bool     writerActive() const override { return writerActive_.load(); }

private:
    std::atomic<int>  readCount_{0};
    std::atomic<bool> writerActive_{false};

    Semaphore mutex_;
    Semaphore wrt_;
};
