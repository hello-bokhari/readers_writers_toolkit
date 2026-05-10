#pragma once

#include <semaphore.h>
#include <time.h>
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Semaphore class
// ─────────────────────────────────────────────────────────────────────────────

class Semaphore {
public:
    // ── Construction / Destruction ────────────────────────────────────────

    explicit Semaphore(unsigned int initialValue = 1);

    ~Semaphore();

    // Semaphores own OS resources; prohibit copy and move.
    Semaphore(const Semaphore&)            = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Semaphore(Semaphore&&)                 = delete;
    Semaphore& operator=(Semaphore&&)      = delete;

    // ── Core operations ───────────────────────────────────────────────────

    void wait();
    void post();
    bool try_wait();
    bool timed_wait(int timeoutMs);
    int value() const;
    const std::string& name() const { return name_; }
    void setName(const std::string& n) { name_ = n; }

private:
    sem_t       sem_;   ///< Underlying POSIX semaphore
    std::string name_;  ///< Optional label for diagnostics

    [[ noreturn ]] static void throwErrno(const char* context);
};

// ─────────────────────────────────────────────────────────────────────────────
// SemaphoreGuard — scoped RAII guard (calls wait on ctor, post on dtor)
// ─────────────────────────────────────────────────────────────────────────────

class SemaphoreGuard {
public:
    explicit SemaphoreGuard(Semaphore& sem) : sem_(sem) { sem_.wait(); }
    ~SemaphoreGuard()                                   { sem_.post(); }

    SemaphoreGuard(const SemaphoreGuard&)            = delete;
    SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;

private:
    Semaphore& sem_;
};
