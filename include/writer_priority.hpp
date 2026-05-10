#pragma once

#include "rw_lock_base.hpp"
#include "semaphore.hpp"
#include <atomic>

class WriterPriorityLock : public RWLockBase {
public:
    WriterPriorityLock();
    ~WriterPriorityLock() override = default;

    void readLock   (ThreadInfo* info) override;
    void readUnlock (ThreadInfo* info) override;
    void writeLock  (ThreadInfo* info) override;
    void writeUnlock(ThreadInfo* info) override;

    AlgoType algo()         const override { return AlgoType::WRITER_PRIORITY; }
    int      readerCount()  const override { return readCount_.load(); }
    bool     writerActive() const override { return writerActive_.load(); }

private:
    std::atomic<int>  readCount_{0};
    std::atomic<int>  writeCount_{0};
    std::atomic<bool> writerActive_{false};

    Semaphore mutex1_;    // protects readCount
    Semaphore mutex2_;    // protects writeCount
    Semaphore rsem_;      // blocks readers when writer is waiting/active
    Semaphore wrt_;       // exclusive write resource lock
    Semaphore readTry_;   // serializes reader attempts to acquire rsem
};
