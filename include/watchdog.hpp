#pragma once

#include <pthread.h>
#include <atomic>
#include <vector>
#include "thread_state.hpp"
#include "config.hpp"

class Watchdog {
public:
    Watchdog();
    ~Watchdog();

    void start(std::vector<ThreadInfo*>* registry,
               pthread_mutex_t*          regMutex,
               const SimConfig*          cfg);


    void stop();

    int totalStarvationEvents() const { return totalEvents_.load(); }

private:
    static void* threadFunc(void* arg);
    void run();

    pthread_t                 thread_{};
    std::atomic<bool>         running_{false};
    std::atomic<int>          totalEvents_{0};

    std::vector<ThreadInfo*>* registry_{nullptr};
    pthread_mutex_t*          regMutex_{nullptr};
    const SimConfig*          cfg_{nullptr};
};
