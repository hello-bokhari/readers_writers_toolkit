#pragma once

#include "thread_state.hpp"
#include "config.hpp"

class Logger;

// ─────────────────────────────────────────────────────────────────────────────
// RWLockBase — abstract interface
// ─────────────────────────────────────────────────────────────────────────────

class RWLockBase {
public:
    virtual ~RWLockBase() = default;


    virtual void readLock(ThreadInfo* info) = 0;

    virtual void readUnlock(ThreadInfo* info) = 0;

    virtual void writeLock(ThreadInfo* info) = 0;

    virtual void writeUnlock(ThreadInfo* info) = 0;

    virtual AlgoType algo() const = 0;

    virtual int readerCount() const = 0;

    virtual bool writerActive() const = 0;

protected:
 
    static void transitionWaiting(ThreadInfo* info, const std::string& msg = "");

    static void transitionActive(ThreadInfo* info, const std::string& msg = "");

    static void transitionReleasing(ThreadInfo* info, const std::string& msg = "");
};
