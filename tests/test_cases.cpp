#include "test_cases.hpp"

#include "logger.hpp"
#include "reader_priority.hpp"
#include "writer_priority.hpp"
#include "fair_morris.hpp"
#include "shared_resource.hpp"
#include "watchdog.hpp"
#include "rw_lock_base.hpp"

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <memory>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <vector>
// ─────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────

static std::atomic<bool> g_testAbortSignal{false};

static void testSigusr1Handler(int) {
    g_testAbortSignal.store(true, std::memory_order_release);
}

// Install signal handler ONCE
static void installSignalHandler() {
    struct sigaction sa{};
    sa.sa_handler = testSigusr1Handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, nullptr);
}

// ─────────────────────────────────────────────
// Thread argument
// ─────────────────────────────────────────────

struct TestThreadArg {
    ThreadInfo*      info;
    RWLockBase*      lock;
    SharedResource*  resource;
    const SimConfig* cfg;
    std::atomic<int>* opsRemaining;
    bool              isAbortTarget{false};
};

// ─────────────────────────────────────────────
// Reader
// ─────────────────────────────────────────────

static void* testReaderFn(void* arg) {
    auto* ta = static_cast<TestThreadArg*>(arg);
    ThreadInfo* info = ta->info;

    info->state.store(ThreadState::INIT, std::memory_order_release);

    while (true) {
        int rem;

        while (true) {
            rem = ta->opsRemaining->load(std::memory_order_relaxed);
            if (rem <= 0) goto finish;

            if (ta->opsRemaining->compare_exchange_weak(rem, rem - 1))
                break;
        }

        ta->lock->readLock(info);
        ta->resource->read();
        ta->resource->recordRead();

        usleep(ta->cfg->readWorkMs * 1000);

        info->opsCompleted++;
        ta->lock->readUnlock(info);
    }

finish:
    info->state.store(ThreadState::DONE, std::memory_order_release);
    info->doneTime = Clock::now();
    Logger::instance().summary(*info);
    return nullptr;
}

// ─────────────────────────────────────────────
// Writer
// ─────────────────────────────────────────────

static void* testWriterFn(void* arg) {
    auto* ta = static_cast<TestThreadArg*>(arg);
    ThreadInfo* info = ta->info;

    info->state.store(ThreadState::INIT, std::memory_order_release);

    while (true) {

        if (ta->isAbortTarget &&
            g_testAbortSignal.load(std::memory_order_acquire))
            break;

        int rem;

        while (true) {
            rem = ta->opsRemaining->load(std::memory_order_relaxed);
            if (rem <= 0) goto finish;

            if (ta->opsRemaining->compare_exchange_weak(rem, rem - 1))
                break;
        }

        ta->lock->writeLock(info);

        if (ta->isAbortTarget &&
            g_testAbortSignal.load(std::memory_order_acquire)) {
            ta->lock->writeUnlock(info);
            goto finish;
        }

        ta->resource->increment();
        ta->resource->recordWrite();

        usleep(ta->cfg->writeWorkMs * 1000);

        info->opsCompleted++;
        ta->lock->writeUnlock(info);
    }

finish:
    info->state.store(ThreadState::DONE, std::memory_order_release);
    info->doneTime = Clock::now();
    Logger::instance().summary(*info);
    return nullptr;
}

// ─────────────────────────────────────────────
// runScenario
// ─────────────────────────────────────────────

SimOutcome runScenario(const SimConfig& cfg, int) {

    installSignalHandler();

    int total = cfg.numReaders + cfg.numWriters;

    std::unique_ptr<RWLockBase> lock;

    switch (cfg.algo) {
        case AlgoType::READER_PRIORITY:
            lock = std::make_unique<ReaderPriorityLock>(); break;
        case AlgoType::WRITER_PRIORITY:
            lock = std::make_unique<WriterPriorityLock>(); break;
        case AlgoType::FAIR_MORRIS:
            lock = std::make_unique<FairMorrisLock>(); break;
    }

    SharedResource resource;
    std::atomic<int> opsRem{cfg.totalOps};

    std::vector<ThreadInfo> infos(total);
    std::vector<TestThreadArg> args(total);
    std::vector<pthread_t> threads(total);
    std::vector<ThreadInfo*> registry;

    registry.reserve(total);

    int tid = 1;

    for (int i = 0; i < cfg.numReaders; ++i, ++tid) {
        infos[i].tid = tid;
        infos[i].type = ThreadType::READER;
        infos[i].algo = cfg.algo;

        args[i] = { &infos[i], lock.get(), &resource, &cfg, &opsRem, false };
        registry.push_back(&infos[i]);
    }

    for (int i = 0; i < cfg.numWriters; ++i, ++tid) {
        int idx = cfg.numReaders + i;

        infos[idx].tid = tid;
        infos[idx].type = ThreadType::WRITER;
        infos[idx].algo = cfg.algo;

        args[idx] = {
            &infos[idx],
            lock.get(),
            &resource,
            &cfg,
            &opsRem,
            (i == 0 && cfg.enableAbortTest)
        };

        registry.push_back(&infos[idx]);
    }

    Watchdog watchdog;
    pthread_mutex_t regMtx = PTHREAD_MUTEX_INITIALIZER;

    watchdog.start(&registry, &regMtx, &cfg);

    for (int i = 0; i < total; ++i)
        pthread_create(&threads[i], nullptr,
                       (i < cfg.numReaders) ? testReaderFn : testWriterFn,
                       &args[i]);

    for (int i = 0; i < total; ++i)
        pthread_join(threads[i], nullptr);

    watchdog.stop();
    pthread_mutex_destroy(&regMtx);

    SimOutcome out;
    out.infos = std::move(infos);
    out.timedOut = false;

    std::vector<ThreadInfo*> ptrs;
    for (auto& t : out.infos) ptrs.push_back(&t);
    out.metrics = Metrics::compute(ptrs, cfg, 0.0, 0, 0);

    return out;
}

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

bool allThreadsDone(const std::vector<ThreadInfo>& infos) {
    for (auto& t : infos)
        if (t.state.load() != ThreadState::DONE)
            return false;
    return true;
}

bool noDeadlock(const std::vector<ThreadInfo>& infos) {
    for (auto& t : infos)
        if (t.state.load() == ThreadState::WAITING)
            return false;
    return true;
}


// ─────────────────────────────────────────────────────────────────────────────
// TC-01: Concurrent reads
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc01_concurrentReads() {
    SimConfig cfg;
    cfg.algo = AlgoType::READER_PRIORITY;
    cfg.numReaders = 8; cfg.numWriters = 0;
    cfg.totalOps = 8; cfg.logLevel = LogLevel::SUMMARY;
    cfg.readWorkMs = 50;

    SimOutcome o = runScenario(cfg);
    int ops = 0; for (auto& t : o.infos) ops += t.opsCompleted;
    bool pass = allThreadsDone(o.infos) && ops >= 8;
    return {1, "Concurrent Reads (8R,0W)", pass,
            "Done=" + std::string(allThreadsDone(o.infos)?"yes":"no") + " Ops=" + std::to_string(ops)};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-02: Exclusive write
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc02_exclusiveWrite() {
    SimConfig cfg;
    cfg.algo = AlgoType::READER_PRIORITY;
    cfg.numReaders = 0; cfg.numWriters = 1;
    cfg.totalOps = 1; cfg.logLevel = LogLevel::SUMMARY;
    cfg.writeWorkMs = 50;

    SimOutcome o = runScenario(cfg);
    bool pass = allThreadsDone(o.infos) && o.infos[0].opsCompleted == 1;
    return {2, "Exclusive Write (0R,1W)", pass,
            "Done=" + std::string(pass?"yes":"no") + " OpsCompleted=" + std::to_string(o.infos[0].opsCompleted)};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-03: Read-Write conflict
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc03_readWriteConflict() {
    SimConfig cfg;
    cfg.algo = AlgoType::READER_PRIORITY;
    cfg.numReaders = 4; cfg.numWriters = 1;
    cfg.totalOps = 20; cfg.logLevel = LogLevel::SUMMARY;
    cfg.readWorkMs = 40; cfg.writeWorkMs = 60;

    SimOutcome o = runScenario(cfg);
    bool pass = allThreadsDone(o.infos);
    std::ostringstream d;
    d << "Done=" << (pass?"yes":"no") << " WriterAvgWait=" << o.metrics.avgWriterWaitMs << "ms";
    return {3, "Read-Write Conflict (4R,1W,RP)", pass, d.str()};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-04: Writer starvation
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc04_writerStarvation() {
    SimConfig base;
    base.numReaders = 6; base.numWriters = 1;
    base.totalOps = 30; base.logLevel = LogLevel::SUMMARY;
    base.readWorkMs = 80; base.writeWorkMs = 60;
    base.starveThresholdMs = 500;

    base.algo = AlgoType::READER_PRIORITY;
    SimOutcome rpOut = runScenario(base);

    base.algo = AlgoType::FAIR_MORRIS;
    SimOutcome faOut = runScenario(base);

    bool pass = allThreadsDone(rpOut.infos) && allThreadsDone(faOut.infos)
                && faOut.metrics.starvationCount == 0;
    std::ostringstream d;
    d << "RP_done=" << (allThreadsDone(rpOut.infos)?"yes":"no")
      << " RP_Starve=" << rpOut.metrics.starvationCount
      << " | FA_done=" << (allThreadsDone(faOut.infos)?"yes":"no")
      << " FA_Starve=" << faOut.metrics.starvationCount << " (expect 0)";
    return {4, "Writer Starvation (6R,1W RP vs FA)", pass, d.str()};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-05: Reader starvation
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc05_readerStarvation() {
    SimConfig base;
    base.numReaders = 3; base.numWriters = 6;
    base.totalOps = 30; base.logLevel = LogLevel::SUMMARY;
    base.readWorkMs = 60; base.writeWorkMs = 80;
    base.starveThresholdMs = 500;

    base.algo = AlgoType::WRITER_PRIORITY;
    SimOutcome wpOut = runScenario(base);

    base.algo = AlgoType::FAIR_MORRIS;
    SimOutcome faOut = runScenario(base);

    bool pass = allThreadsDone(wpOut.infos) && allThreadsDone(faOut.infos)
                && faOut.metrics.starvationCount == 0;
    std::ostringstream d;
    d << "WP_done=" << (allThreadsDone(wpOut.infos)?"yes":"no")
      << " WP_Starve=" << wpOut.metrics.starvationCount
      << " | FA_done=" << (allThreadsDone(faOut.infos)?"yes":"no")
      << " FA_Starve=" << faOut.metrics.starvationCount << " (expect 0)";
    return {5, "Reader Starvation (3R,6W WP vs FA)", pass, d.str()};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-06: High contention
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc06_highContention() {
    auto run = [](AlgoType a) -> bool {
        SimConfig cfg;
        cfg.algo = a; cfg.numReaders = 10; cfg.numWriters = 10;
        cfg.totalOps = 60; cfg.logLevel = LogLevel::SUMMARY;
        cfg.readWorkMs = 20; cfg.writeWorkMs = 30;
        return allThreadsDone(runScenario(cfg, 20000).infos);
    };

    bool rp = run(AlgoType::READER_PRIORITY);
    bool wp = run(AlgoType::WRITER_PRIORITY);
    bool fa = run(AlgoType::FAIR_MORRIS);

    std::ostringstream d;
    d << "RP=" << (rp?"PASS":"FAIL") << " WP=" << (wp?"PASS":"FAIL") << " FA=" << (fa?"PASS":"FAIL");
    return {6, "High Contention (10R+10W all algos)", rp&&wp&&fa, d.str()};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-07: Zero readers
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc07_zeroReaders() {
    SimConfig cfg;
    cfg.algo = AlgoType::READER_PRIORITY;
    cfg.numReaders = 0; cfg.numWriters = 5;
    cfg.totalOps = 10; cfg.logLevel = LogLevel::SUMMARY;
    cfg.writeWorkMs = 30;

    SimOutcome o = runScenario(cfg);
    bool pass = allThreadsDone(o.infos);
    return {7, "Zero Readers (0R,5W)", pass, "Done=" + std::string(pass?"yes":"no")};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-08: Zero writers
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc08_zeroWriters() {
    SimConfig cfg;
    cfg.algo = AlgoType::READER_PRIORITY;
    cfg.numReaders = 8; cfg.numWriters = 0;
    cfg.totalOps = 16; cfg.logLevel = LogLevel::SUMMARY;
    cfg.readWorkMs = 50;

    SimOutcome o = runScenario(cfg);
    bool pass = allThreadsDone(o.infos);
    return {8, "Zero Writers (8R,0W)", pass,
            "Done=" + std::string(pass?"yes":"no") + " Throughput=" + std::to_string((int)o.metrics.throughputOpsPerSec) + "ops/s"};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-09: Single thread baseline
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc09_singleThread() {
    SimConfig cfgR;
    cfgR.algo = AlgoType::READER_PRIORITY;
    cfgR.numReaders = 1; cfgR.numWriters = 0;
    cfgR.totalOps = 1; cfgR.logLevel = LogLevel::SUMMARY;
    cfgR.readWorkMs = 20;
    SimOutcome oR = runScenario(cfgR);

    SimConfig cfgW = cfgR;
    cfgW.numReaders = 0; cfgW.numWriters = 1;
    cfgW.writeWorkMs = 20;
    SimOutcome oW = runScenario(cfgW);

    bool rDone = allThreadsDone(oR.infos);
    bool wDone = allThreadsDone(oW.infos);
    return {9, "Single Thread Baseline (1R & 1W)", rDone && wDone,
            "ReaderDone=" + std::string(rDone?"yes":"no") + " WriterDone=" + std::string(wDone?"yes":"no")};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-10: Rapid re-entry
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc10_rapidReentry() {
    SimConfig cfg;
    cfg.algo = AlgoType::FAIR_MORRIS;
    cfg.numReaders = 3; cfg.numWriters = 2;
    cfg.totalOps = 50; cfg.logLevel = LogLevel::SUMMARY;
    cfg.readWorkMs = 5; cfg.writeWorkMs = 15;

    SimOutcome o = runScenario(cfg, 12000);
    bool pass = allThreadsDone(o.infos) && o.metrics.starvationCount == 0;
    std::ostringstream d;
    d << "Done=" << (allThreadsDone(o.infos)?"yes":"no")
      << " Starve=" << o.metrics.starvationCount
      << " Jain=" << o.metrics.jainsFairnessIndex;
    return {10, "Rapid Re-entry (3R,2W Fair)", pass, d.str()};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-11: Fairness validation
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc11_fairnessValidation() {
    auto makeCfg = [](AlgoType a) {
        SimConfig cfg;
        cfg.algo = a; cfg.numReaders = 5; cfg.numWriters = 5;
        cfg.totalOps = 100; cfg.logLevel = LogLevel::SUMMARY;
        cfg.readWorkMs = 30; cfg.writeWorkMs = 40;
        return cfg;
    };

    double rpJ = runScenario(makeCfg(AlgoType::READER_PRIORITY)).metrics.jainsFairnessIndex;
    double wpJ = runScenario(makeCfg(AlgoType::WRITER_PRIORITY)).metrics.jainsFairnessIndex;
    SimOutcome faOut = runScenario(makeCfg(AlgoType::FAIR_MORRIS));
    double faJ = faOut.metrics.jainsFairnessIndex;

    bool pass = faJ >= 0.80 && allThreadsDone(faOut.infos);
    std::ostringstream d;
    d << std::fixed; d.precision(3);
    d << "RP_Jain=" << rpJ << " WP_Jain=" << wpJ << " FA_Jain=" << faJ << " (expect>=0.80)";
    return {11, "Fairness Validation Jain's Index", pass, d.str()};
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-12: Interrupt / Abort via SIGUSR1
// ─────────────────────────────────────────────────────────────────────────────

TestResult tc12_interruptAbort() {
    g_testAbortSignal.store(false, std::memory_order_release);

    SimConfig cfg;
    cfg.algo = AlgoType::FAIR_MORRIS;
    cfg.numReaders = 3; cfg.numWriters = 2;
    cfg.totalOps = 20; cfg.logLevel = LogLevel::SUMMARY;
    cfg.readWorkMs = 30; cfg.writeWorkMs = 80;
    cfg.enableAbortTest = true;

    struct Ctx { SimConfig cfg; SimOutcome out; };
    Ctx ctx{cfg, {}};

    pthread_t simTh;
    pthread_create(&simTh, nullptr, [](void* a) -> void* {
        auto* c = static_cast<Ctx*>(a);
        c->out = runScenario(c->cfg, 12000);
        return nullptr;
    }, &ctx);

    usleep(250'000);        // 250 ms — writer should be active
    kill(getpid(), SIGUSR1);

    pthread_join(simTh, nullptr);

    bool pass = allThreadsDone(ctx.out.infos);
    std::ostringstream d;
    d << "AllDone=" << (pass?"yes":"no");
    for (auto& t : ctx.out.infos)
        d << " TID" << t.tid << "=" << stateName(t.state.load());
    return {12, "Interrupt/Abort SIGUSR1 (TC-12)", pass, d.str()};
}