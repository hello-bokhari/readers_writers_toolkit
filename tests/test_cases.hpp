#pragma once

#include <string>
#include <vector>

#include "config.hpp"
#include "thread_state.hpp"
#include "metrics.hpp"

// ── Test Result ─────────────────────────────

struct TestResult {
    int         tcNum;
    std::string name;
    bool        passed;
    std::string detail;
};

// ── Simulation Output ───────────────────────

struct SimOutcome {
    std::vector<ThreadInfo> infos;
    MetricsResult           metrics;
    bool                    timedOut;
};

// ── Core API ────────────────────────────────

SimOutcome runScenario(const SimConfig& cfg, int timeoutMs = 8000);

bool allThreadsDone(const std::vector<ThreadInfo>& infos);
bool noDeadlock(const std::vector<ThreadInfo>& infos);

// ── Test Cases ──────────────────────────────

TestResult tc01_concurrentReads();
TestResult tc02_exclusiveWrite();
TestResult tc03_readWriteConflict();
TestResult tc04_writerStarvation();
TestResult tc05_readerStarvation();
TestResult tc06_highContention();
TestResult tc07_zeroReaders();
TestResult tc08_zeroWriters();
TestResult tc09_singleThread();
TestResult tc10_rapidReentry();
TestResult tc11_fairnessValidation();
TestResult tc12_interruptAbort();
