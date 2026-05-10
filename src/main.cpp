#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <stdexcept>

#include "config.hpp"
#include "thread_state.hpp"
#include "semaphore.hpp"
#include "logger.hpp"
#include "watchdog.hpp"
#include "metrics.hpp"
#include "shared_resource.hpp"
#include "rw_lock_base.hpp"
#include "reader_priority.hpp"
#include "writer_priority.hpp"
#include "fair_morris.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Thread argument structure
// ─────────────────────────────────────────────────────────────────────────────

struct ThreadArg {
    ThreadInfo*    info;
    RWLockBase*    lock;
    SharedResource* resource;
    const SimConfig* cfg;

    std::atomic<int>* opsRemaining;

    bool isAbortTarget{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Signal handling (TC-12)
// ─────────────────────────────────────────────────────────────────────────────

static std::atomic<bool> g_abortSignalReceived{false};

static void sigusr1Handler(int /*sig*/) {
    g_abortSignalReceived.store(true, std::memory_order_release);
}

// ─────────────────────────────────────────────────────────────────────────────
// Reader thread function
// ─────────────────────────────────────────────────────────────────────────────

static void* readerThread(void* arg) {
    ThreadArg* targ = static_cast<ThreadArg*>(arg);
    ThreadInfo* info  = targ->info;
    RWLockBase* lock  = targ->lock;
    SharedResource* res = targ->resource;
    Logger& log = Logger::instance();

    info->state.store(ThreadState::INIT, std::memory_order_release);
    log.record(*info, ThreadState::INIT, 0, "Reader thread started");

    while (true) {
        int remaining = targ->opsRemaining->fetch_sub(1, std::memory_order_acq_rel);
        if (remaining <= 0) {
            targ->opsRemaining->fetch_add(1, std::memory_order_relaxed);
            break;
        }

        lock->readLock(info);

        int value = res->read();
        res->recordRead();
        usleep(static_cast<useconds_t>(targ->cfg->readWorkMs) * 1000u);
        (void)value;

        info->opsCompleted++;
        lock->readUnlock(info);
    }

    info->state.store(ThreadState::DONE, std::memory_order_release);
    info->doneTime = Clock::now();
    log.record(*info, ThreadState::DONE, -1, "Reader thread finished");
    log.summary(*info);

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Writer thread function
// ─────────────────────────────────────────────────────────────────────────────

static void* writerThread(void* arg) {
    ThreadArg* targ = static_cast<ThreadArg*>(arg);
    ThreadInfo* info  = targ->info;
    RWLockBase* lock  = targ->lock;
    SharedResource* res = targ->resource;
    Logger& log = Logger::instance();

    if (targ->isAbortTarget) {
        struct sigaction sa{};
        sa.sa_handler = sigusr1Handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGUSR1, &sa, nullptr);
        log.record(*info, ThreadState::INIT, 0, "Writer TC-12 abort target: SIGUSR1 handler installed");
    }


    info->state.store(ThreadState::INIT, std::memory_order_release);
    log.record(*info, ThreadState::INIT, 0, "Writer thread started");

    while (true) {
        if (targ->isAbortTarget && g_abortSignalReceived.load(std::memory_order_acquire)) {
            log.record(*info, ThreadState::DONE, -1,
                       "Writer TC-12: abort signal received, exiting cleanly");
            break;
        }

        int remaining = targ->opsRemaining->fetch_sub(1, std::memory_order_acq_rel);
        if (remaining <= 0) {
            targ->opsRemaining->fetch_add(1, std::memory_order_relaxed);
            break;
        }

        lock->writeLock(info);

        if (targ->isAbortTarget && g_abortSignalReceived.load(std::memory_order_acquire)) {
            lock->writeUnlock(info);
            info->state.store(ThreadState::DONE, std::memory_order_release);
            log.record(*info, ThreadState::DONE, -1,
                       "Writer TC-12: abort signal received mid-operation, lock released cleanly");
            log.summary(*info);
            return nullptr;
        }

        res->increment();
        res->recordWrite();
        usleep(static_cast<useconds_t>(targ->cfg->writeWorkMs) * 1000u);

        info->opsCompleted++;
        lock->writeUnlock(info);
    }

    info->state.store(ThreadState::DONE, std::memory_order_release);
    info->doneTime = Clock::now();
    log.record(*info, ThreadState::DONE, -1, "Writer thread finished");
    log.summary(*info);

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// runSimulation — runs one complete simulation and returns metrics
// ─────────────────────────────────────────────────────────────────────────────

MetricsResult runSimulation(const SimConfig& cfg) {
    Logger& log = Logger::instance();

    std::string hdrText = std::string("Algorithm: ") + algoLongName(cfg.algo)
        + "  |  Readers: " + std::to_string(cfg.numReaders)
        + "  |  Writers: " + std::to_string(cfg.numWriters)
        + "  |  Ops: " + std::to_string(cfg.totalOps);
    log.header(hdrText);

    std::unique_ptr<RWLockBase> lock;
    switch (cfg.algo) {
        case AlgoType::READER_PRIORITY: lock = std::make_unique<ReaderPriorityLock>(); break;
        case AlgoType::WRITER_PRIORITY: lock = std::make_unique<WriterPriorityLock>(); break;
        case AlgoType::FAIR_MORRIS:     lock = std::make_unique<FairMorrisLock>();     break;
    }

    SharedResource resource;
    std::atomic<int> opsRemaining{cfg.totalOps};

    int totalThreads = cfg.numReaders + cfg.numWriters;
    std::vector<ThreadInfo*>  registry;
    std::vector<ThreadArg>    args(totalThreads);
    std::vector<pthread_t>    threads(totalThreads);
    registry.reserve(totalThreads);

    pthread_mutex_t regMutex = PTHREAD_MUTEX_INITIALIZER;

    std::vector<ThreadInfo> infos(totalThreads);
    int tid = 1;
    for (int i = 0; i < cfg.numReaders; ++i, ++tid) {
        ThreadInfo& info = infos[i];
        info.tid  = tid;
        info.type = ThreadType::READER;
        info.algo = cfg.algo;

        args[i].info          = &info;
        args[i].lock          = lock.get();
        args[i].resource      = &resource;
        args[i].cfg           = &cfg;
        args[i].opsRemaining  = &opsRemaining;
        args[i].isAbortTarget = false;

        pthread_mutex_lock(&regMutex);
        registry.push_back(&info);
        pthread_mutex_unlock(&regMutex);
    }

    for (int i = 0; i < cfg.numWriters; ++i, ++tid) {
        int idx = cfg.numReaders + i;
        ThreadInfo& info = infos[idx];
        info.tid  = tid;
        info.type = ThreadType::WRITER;
        info.algo = cfg.algo;

        args[idx].info          = &info;
        args[idx].lock          = lock.get();
        args[idx].resource      = &resource;
        args[idx].cfg           = &cfg;
        args[idx].opsRemaining  = &opsRemaining;
        args[idx].isAbortTarget = (i == 0 && cfg.enableAbortTest);

        pthread_mutex_lock(&regMutex);
        registry.push_back(&info);
        pthread_mutex_unlock(&regMutex);
    }

    Watchdog watchdog;
    watchdog.start(&registry, &regMutex, &cfg);

    long ctxVolBefore, ctxInvBefore;
    Metrics::captureContextSwitches(ctxVolBefore, ctxInvBefore);
    auto wallStart = std::chrono::steady_clock::now();

    for (int i = 0; i < cfg.numReaders; ++i) {

        int rc = pthread_create(&threads[i], nullptr, readerThread, &args[i]);
        if (rc != 0) {
            throw std::system_error(rc, std::generic_category(), "pthread_create reader");
        }
    }

    for (int i = 0; i < cfg.numWriters; ++i) {
        int idx = cfg.numReaders + i;
        int rc = pthread_create(&threads[idx], nullptr, writerThread, &args[idx]);
        if (rc != 0) {
            throw std::system_error(rc, std::generic_category(), "pthread_create writer");
        }
    }


    for (int i = 0; i < totalThreads; ++i) {
        pthread_join(threads[i], nullptr);
    }

    auto wallEnd = std::chrono::steady_clock::now();
    double wallMs = std::chrono::duration<double, std::milli>(wallEnd - wallStart).count();

    long ctxVolAfter, ctxInvAfter;
    Metrics::captureContextSwitches(ctxVolAfter, ctxInvAfter);
    long ctxVol = ctxVolAfter - ctxVolBefore;
    long ctxInv = ctxInvAfter - ctxInvBefore;

    watchdog.stop();
    pthread_mutex_destroy(&regMutex);

    std::vector<ThreadInfo*> tptrs;
    for (auto& t : infos) tptrs.push_back(&t);

    MetricsResult result = Metrics::compute(tptrs, cfg, wallMs, ctxVol, ctxInv);

    log.header("Simulation Complete — " + result.toString());
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// runBenchmark — all load profiles × all algorithms × BENCHMARK_RUNS
// ─────────────────────────────────────────────────────────────────────────────

void runBenchmark(const SimConfig& baseCfg) {
    std::vector<MetricsResult> allResults;

    const LoadProfile profiles[]     = { LoadProfile::LIGHT, LoadProfile::BALANCED, LoadProfile::HEAVY };
    const AlgoType    algorithms[]   = { AlgoType::READER_PRIORITY, AlgoType::WRITER_PRIORITY, AlgoType::FAIR_MORRIS };
    const char*       profileNames[] = { "Light", "Balanced", "Heavy" };

    for (auto profile : profiles) {
        for (auto algo : algorithms) {
            printf("\n[Benchmark] Profile=%-8s Algo=%-16s Averaging %d runs...\n",
                   profileNames[static_cast<int>(profile)],
                   algoLongName(algo),
                   BENCHMARK_RUNS);

            MetricsResult avg{};
            avg.algo       = algo;
            int runsOk = 0;

            for (int run = 0; run < BENCHMARK_RUNS; ++run) {
                SimConfig runCfg = baseCfg;
                runCfg.algo  = algo;
                runCfg.logLevel = LogLevel::SUMMARY;
                applyLoadProfile(runCfg, profile);

                MetricsResult r = runSimulation(runCfg);

                avg.avgReaderWaitMs      += r.avgReaderWaitMs;
                avg.avgWriterWaitMs      += r.avgWriterWaitMs;
                avg.throughputOpsPerSec  += r.throughputOpsPerSec;
                avg.jainsFairnessIndex   += r.jainsFairnessIndex;
                avg.starvationCount      += r.starvationCount;
                avg.contextSwitchesVol   += r.contextSwitchesVol;
                avg.contextSwitchesInv   += r.contextSwitchesInv;
                avg.simulationDurationMs += r.simulationDurationMs;
                avg.numReaders  = r.numReaders;
                avg.numWriters  = r.numWriters;
                avg.totalOps    = r.totalOps;
                ++runsOk;
            }

            if (runsOk > 0) {
                avg.avgReaderWaitMs      /= runsOk;
                avg.avgWriterWaitMs      /= runsOk;
                avg.throughputOpsPerSec  /= runsOk;
                avg.jainsFairnessIndex   /= runsOk;
                avg.starvationCount      /= runsOk;
                avg.contextSwitchesVol   /= runsOk;
                avg.contextSwitchesInv   /= runsOk;
                avg.simulationDurationMs /= runsOk;
            }

            allResults.push_back(avg);

            if (!baseCfg.csvOutput.empty()) {
                Metrics::exportCSV(baseCfg.csvOutput, avg);
            }
        }
    }

    printf("\n");
    Metrics::printTable(allResults);

    if (!baseCfg.csvOutput.empty()) {
        printf("[Benchmark] Results exported to: %s\n", baseCfg.csvOutput.c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CLI parsing
// ─────────────────────────────────────────────────────────────────────────────

static void printUsage(const char* progName) {
    printf(
        "Readers-Writers Toolkit — OS Project Spring 2026, FAST-NUCES\n\n"
        "Usage: %s [options]\n\n"
        "Options:\n"
        "  --algo   <rp|wp|fa>                Algorithm (rp=Reader-Priority, wp=Writer-Priority, fa=Fair-Morris)\n"
        "  --readers <n>                      Number of reader threads (default: 5)\n"
        "  --writers <n>                      Number of writer threads (default: 3)\n"
        "  --ops     <n>                      Total operations (default: 100)\n"
        "  --log    <verbose|standard|summary> Log verbosity (default: verbose)\n"
        "  --logfile <path>                   Write log to file\n"
        "  --starve  <ms>                     Starvation threshold in ms (default: 500)\n"
        "  --benchmark                        Run all profiles x all algorithms x 5 runs\n"
        "  --output  <path>                   CSV output for benchmark results\n"
        "  --abort-test                       Enable TC-12 signal abort test\n"
        "  --help                             Print this message\n",
        progName
    );
}

static SimConfig parseCLI(int argc, char* argv[]) {
    SimConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            printUsage(argv[0]);
            exit(0);
        } else if (arg == "--algo" && i + 1 < argc) {
            std::string a = argv[++i];
            if      (a == "rp") cfg.algo = AlgoType::READER_PRIORITY;
            else if (a == "wp") cfg.algo = AlgoType::WRITER_PRIORITY;
            else if (a == "fa") cfg.algo = AlgoType::FAIR_MORRIS;
            else { fprintf(stderr, "Unknown algo '%s'\n", a.c_str()); exit(1); }
        } else if (arg == "--readers"  && i + 1 < argc) { cfg.numReaders = atoi(argv[++i]); }
          else if (arg == "--writers"  && i + 1 < argc) { cfg.numWriters = atoi(argv[++i]); }
          else if (arg == "--ops"      && i + 1 < argc) { cfg.totalOps   = atoi(argv[++i]); }
          else if (arg == "--starve"   && i + 1 < argc) { cfg.starveThresholdMs = atoi(argv[++i]); }
          else if (arg == "--logfile"  && i + 1 < argc) { cfg.logFile    = argv[++i]; }
          else if (arg == "--output"   && i + 1 < argc) { cfg.csvOutput  = argv[++i]; }
          else if (arg == "--benchmark")  { cfg.benchmarkMode   = true; }
          else if (arg == "--abort-test") { cfg.enableAbortTest = true; }
          else if (arg == "--log" && i + 1 < argc) {
            std::string l = argv[++i];
            if      (l == "verbose")  cfg.logLevel = LogLevel::VERBOSE;
            else if (l == "standard") cfg.logLevel = LogLevel::STANDARD;
            else if (l == "summary")  cfg.logLevel = LogLevel::SUMMARY;
            else { fprintf(stderr, "Unknown log level '%s'\n", l.c_str()); exit(1); }
        }
    }

    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    SimConfig cfg = parseCLI(argc, argv);

    // Initialise logger.
    Logger::instance().init(cfg.logLevel, cfg.logFile);

    if (cfg.benchmarkMode) {
        runBenchmark(cfg);
    } else {
        MetricsResult r = runSimulation(cfg);
        printf("\n%s\n", r.toString().c_str());

        if (!cfg.csvOutput.empty()) {
            Metrics::exportCSV(cfg.csvOutput, r);
            printf("Results exported to: %s\n", cfg.csvOutput.c_str());
        }
    }

    Logger::instance().shutdown();
    return 0;
}
