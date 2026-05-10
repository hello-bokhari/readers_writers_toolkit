#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "thread_state.hpp"
#include "config.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// MetricsResult — holds one simulation run's results
// ─────────────────────────────────────────────────────────────────────────────

struct MetricsResult {
    AlgoType    algo;
    int         numReaders;
    int         numWriters;
    int         totalOps;

    double      avgReaderWaitMs;    ///< Mean reader WAITING time (ms)
    double      avgWriterWaitMs;    ///< Mean writer WAITING time (ms)
    double      throughputOpsPerSec;///< Ops completed per wall-clock second
    double      jainsFairnessIndex; ///< [0,1]; ≥0.90 expected for Morris
    int         starvationCount;    ///< Threads that exceeded STARVE_MS
    long        contextSwitchesVol; ///< Voluntary context switches (getrusage)
    long        contextSwitchesInv; ///< Involuntary context switches (getrusage)
    double      simulationDurationMs; ///< Wall-clock duration of simulation

    std::string toString() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// Metrics — collector and calculator
// ─────────────────────────────────────────────────────────────────────────────

class Metrics {
public:
    /**
     * compute()
     * ---------
     * Calculate all metrics from a completed simulation.
     *
     * @param threads    All ThreadInfo records after simulation completes.
     * @param cfg        The simulation configuration used.
     * @param wallMs     Total wall-clock duration of the simulation in ms.
     * @param ctxVol     Voluntary context switches (from getrusage delta).
     * @param ctxInv     Involuntary context switches (from getrusage delta).
     */
    static MetricsResult compute(
        const std::vector<ThreadInfo*>& threads,
        const SimConfig&                cfg,
        double                          wallMs,
        long                            ctxVol,
        long                            ctxInv
    );

    static void exportCSV(const std::string& filepath, const MetricsResult& r);
    static void printTable(const std::vector<MetricsResult>& results);
    static void captureContextSwitches(long& volOut, long& invOut);
};
