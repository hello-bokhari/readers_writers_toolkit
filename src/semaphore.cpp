/**
 * semaphore.cpp
 * -------------
 * Implementation of the custom Semaphore class declared in semaphore.hpp.
 *
 * All POSIX sem_* calls are fully error-checked. EINTR (signal interruption)
 * is handled by retrying the operation — this is the correct POSIX practice
 * for wait/timedwait.
 *
 * Authors: Abbad Hasan, Muhammed Ahmed, Talal Tariq
 * Course:  Operating Systems, Spring 2026 — FAST-NUCES
 */

#include "semaphore.hpp"

#include <cerrno>
#include <cstring>    // strerror
#include <system_error>

// ─────────────────────────────────────────────────────────────────────────────
// Helper
// ─────────────────────────────────────────────────────────────────────────────

void Semaphore::throwErrno(const char* context) {
    throw std::system_error(errno, std::generic_category(), context);
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

/**
 * sem_init(sem, pshared, value)
 *   pshared = 0  → semaphore shared between threads of the same process.
 *                  (pshared = 1 would share between processes via shared memory.)
 *   value         → initial count; must be ≤ SEM_VALUE_MAX (at least 32767).
 *
 * Returns 0 on success, -1 on error:
 *   EINVAL  value exceeds SEM_VALUE_MAX
 *   ENOSYS  semaphores not supported (very old kernels — won't happen on Ubuntu)
 */
Semaphore::Semaphore(unsigned int initialValue) {
    if (sem_init(&sem_, 0, initialValue) != 0) {
        throwErrno("sem_init");
    }
}

/**
 * sem_destroy(sem)
 * Frees resources associated with the semaphore.
 * Undefined behaviour if threads are still blocked on it —
 * the simulator ensures all threads join before destroying semaphores.
 */
Semaphore::~Semaphore() {
    // Best-effort destroy; ignore errors in destructor (can't throw).
    sem_destroy(&sem_);
}

// ─────────────────────────────────────────────────────────────────────────────
// wait()  — P / acquire / decrement
// ─────────────────────────────────────────────────────────────────────────────

/**
 * sem_wait(sem)
 *   Atomically decrements the semaphore count.
 *   If count == 0, the calling thread blocks until another thread calls sem_post.
 *
 *   Returns 0 on success, -1 on error:
 *     EINTR   interrupted by a signal — we retry (POSIX-correct behaviour)
 *     EINVAL  semaphore was not initialised — programming error
 */
void Semaphore::wait() {
    while (sem_wait(&sem_) != 0) {
        if (errno == EINTR) {
            continue;   // Interrupted by signal; retry.
        }
        throwErrno("sem_wait");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// post()  — V / release / increment
// ─────────────────────────────────────────────────────────────────────────────

/**
 * sem_post(sem)
 *   Atomically increments the semaphore count.
 *   If one or more threads are blocked in sem_wait(), exactly one is unblocked.
 *
 *   Returns 0 on success, -1 on error:
 *     EOVERFLOW  count would exceed SEM_VALUE_MAX (misuse — too many posts)
 *
 *   sem_post is async-signal-safe (may be called from signal handlers).
 */
void Semaphore::post() {
    if (sem_post(&sem_) != 0) {
        throwErrno("sem_post");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// try_wait()  — non-blocking P
// ─────────────────────────────────────────────────────────────────────────────

/**
 * sem_trywait(sem)
 *   Identical to sem_wait() but never blocks.
 *   Returns 0 if decremented successfully.
 *   Returns -1 with EAGAIN if count == 0.
 */
bool Semaphore::try_wait() {
    while (sem_trywait(&sem_) != 0) {
        if (errno == EAGAIN) {
            return false;   // Semaphore count was zero; did not acquire.
        }
        if (errno == EINTR) {
            continue;       // Signal interrupted; retry.
        }
        throwErrno("sem_trywait");
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// timed_wait()  — bounded P
// ─────────────────────────────────────────────────────────────────────────────

/**
 * sem_timedwait(sem, abstime)
 *   Like sem_wait() but gives up at the absolute wall-clock time 'abstime'.
 *
 *   abstime is an absolute time (not a relative timeout) measured by
 *   CLOCK_REALTIME.  We compute it here:
 *       abstime = clock_gettime(CLOCK_REALTIME) + timeoutMs
 *
 *   Returns 0 on success, -1 on:
 *     ETIMEDOUT  deadline passed without acquiring
 *     EINTR      signal interrupted — we retry with the ORIGINAL deadline
 *                (not a new one), so we don't drift on repeated signals.
 */
bool Semaphore::timed_wait(int timeoutMs) {
    struct timespec abstime;

    // Get current CLOCK_REALTIME and add timeout.
    // clock_gettime: fills timespec with {tv_sec, tv_nsec}.
    if (clock_gettime(CLOCK_REALTIME, &abstime) != 0) {
        throwErrno("clock_gettime");
    }

    // Add timeout in milliseconds, carrying over nanoseconds.
    long addNs = static_cast<long>(timeoutMs) * 1'000'000L;
    abstime.tv_nsec += addNs;
    if (abstime.tv_nsec >= 1'000'000'000L) {
        abstime.tv_sec  += abstime.tv_nsec / 1'000'000'000L;
        abstime.tv_nsec  = abstime.tv_nsec % 1'000'000'000L;
    }

    while (sem_timedwait(&sem_, &abstime) != 0) {
        if (errno == ETIMEDOUT) {
            return false;   // Timed out; did not acquire.
        }
        if (errno == EINTR) {
            continue;       // Signal interrupted; retry with same deadline.
        }
        throwErrno("sem_timedwait");
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// value()
// ─────────────────────────────────────────────────────────────────────────────

/**
 * sem_getvalue(sem, valptr)
 *   Places the current semaphore count in *valptr.
 *   On Linux, if threads are blocked, *valptr may be 0 or negative
 *   (encoding the number of waiters as a negative value).
 *   The POSIX standard only guarantees the value is ≥ 0 or the count
 *   of waiters as a non-positive value — behaviour is implementation-defined.
 */
int Semaphore::value() const {
    int val = 0;
    // sem_getvalue takes a non-const pointer, but logically this is a
    // read-only observer — we cast away const on the internal sem_.
    if (sem_getvalue(const_cast<sem_t*>(&sem_), &val) != 0) {
        throwErrno("sem_getvalue");
    }
    return val;
}
