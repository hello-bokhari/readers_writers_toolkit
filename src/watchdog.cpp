#include "watchdog.hpp"
#include "logger.hpp"
#include <unistd.h>
#include <system_error>

Watchdog::Watchdog() = default;

Watchdog::~Watchdog() {
    if (running_.load()) {
        stop();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// start() — spawn the watchdog pthread
// ─────────────────────────────────────────────────────────────────────────────

void Watchdog::start(std::vector<ThreadInfo*>* registry,
                     pthread_mutex_t*           regMutex,
                     const SimConfig*           cfg) {
    registry_  = registry;
    regMutex_  = regMutex;
    cfg_       = cfg;
    running_.store(true, std::memory_order_release);

    int rc = pthread_create(&thread_, nullptr, Watchdog::threadFunc, this);
    if (rc != 0) {
        running_.store(false);
        throw std::system_error(rc, std::generic_category(), "Watchdog pthread_create");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// stop() — signal stop and join
// ─────────────────────────────────────────────────────────────────────────────

void Watchdog::stop() {
    if (!running_.exchange(false)) {
        return;   // Already stopped.
    }
    pthread_join(thread_, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// threadFunc — pthread start routine (static)
// ─────────────────────────────────────────────────────────────────────────────

void* Watchdog::threadFunc(void* arg) {
    // Recover the Watchdog* from the void* argument.
    static_cast<Watchdog*>(arg)->run();
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// run() — watchdog main loop
// ─────────────────────────────────────────────────────────────────────────────

void Watchdog::run() {
    Logger& log = Logger::instance();

    while (running_.load(std::memory_order_acquire)) {.
        usleep(static_cast<useconds_t>(WATCHDOG_POLL_MS) * 1000u);

        pthread_mutex_lock(regMutex_);

        for (ThreadInfo* info : *registry_) {
            if (!info) continue;

            ThreadState s = info->state.load(std::memory_order_acquire);

            if (s != ThreadState::WAITING) continue;

            long waitMs = info->currentWaitMs();

            if (waitMs >= cfg_->starveThresholdMs) {
                info->state.store(ThreadState::BLOCKED, std::memory_order_release);
                ++info->starveCount;
                totalEvents_.fetch_add(1, std::memory_order_relaxed);
                log.alert(*info, waitMs);
            }
        }

        pthread_mutex_unlock(regMutex_);
    }
}
