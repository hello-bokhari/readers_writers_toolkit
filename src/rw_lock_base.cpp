#include "rw_lock_base.hpp"
#include "logger.hpp"
#include <string>

void RWLockBase::transitionWaiting(ThreadInfo* info, const std::string& msg) {
    info->enterWaiting();
    std::string m = msg.empty()
        ? (info->type == ThreadType::READER ? "Reader requesting lock"
                                            : "Writer requesting lock")
        : msg;
    Logger::instance().record(*info, ThreadState::WAITING, 0, m);
}

void RWLockBase::transitionActive(ThreadInfo* info, const std::string& msg) {
    long waited = info->currentWaitMs();
    info->enterActive();
    std::string m = msg.empty()
        ? (info->type == ThreadType::READER ? "Reader acquired lock"
                                            : "Writer acquired exclusive lock")
        : msg;
    Logger::instance().record(*info, ThreadState::ACTIVE, waited, m);
}

void RWLockBase::transitionReleasing(ThreadInfo* info, const std::string& msg) {
    info->state.store(ThreadState::RELEASING, std::memory_order_release);
    std::string m = msg.empty()
        ? (info->type == ThreadType::READER ? "Reader released lock"
                                            : "Writer released exclusive lock")
        : msg;
    Logger::instance().record(*info, ThreadState::RELEASING, -1, m);
}
