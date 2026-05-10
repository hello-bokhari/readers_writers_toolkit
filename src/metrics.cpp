#include "metrics.hpp"

#include <sys/resource.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>

// ─────────────────────────────────────────────────────────────────────────────
// captureContextSwitches
// ─────────────────────────────────────────────────────────────────────────────

void Metrics::captureContextSwitches(long& volOut, long& invOut) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        volOut = invOut = -1;
        return;
    }
    volOut = usage.ru_nvcsw;
    invOut = usage.ru_nivcsw;
}

// ─────────────────────────────────────────────────────────────────────────────
// compute — main metrics calculation
// ─────────────────────────────────────────────────────────────────────────────

MetricsResult Metrics::compute(
    const std::vector<ThreadInfo*>& threads,
    const SimConfig&                cfg,
    double                          wallMs,
    long                            ctxVol,
    long                            ctxInv)
{
    MetricsResult r{};
    r.algo       = cfg.algo;
    r.numReaders = cfg.numReaders;
    r.numWriters = cfg.numWriters;
    r.totalOps   = cfg.totalOps;
    r.simulationDurationMs  = wallMs;
    r.contextSwitchesVol    = ctxVol;
    r.contextSwitchesInv    = ctxInv;

    double  readerWaitSum  = 0.0;
    int     readerCount    = 0;
    double  writerWaitSum  = 0.0;
    int     writerCount    = 0;
    int     totalOpsActual = 0;
    int     starveEvents   = 0;

    std::vector<double> perThreadOps;

    for (const ThreadInfo* t : threads) {
        if (!t) continue;

        int ops = t->opsCompleted;
        totalOpsActual += ops;
        starveEvents   += t->starveCount;
        perThreadOps.push_back(static_cast<double>(ops));

        double avgWait = (ops > 0) ? (static_cast<double>(t->totalWaitMs) / ops) : 0.0;

        if (t->type == ThreadType::READER) {
            readerWaitSum += avgWait;
            ++readerCount;
        } else {
            writerWaitSum += avgWait;
            ++writerCount;
        }
    }

    r.avgReaderWaitMs = (readerCount > 0) ? (readerWaitSum / readerCount) : 0.0;
    r.avgWriterWaitMs = (writerCount > 0) ? (writerWaitSum / writerCount) : 0.0;

    if (wallMs > 0) {
        r.throughputOpsPerSec = (static_cast<double>(totalOpsActual) / wallMs) * 1000.0;
    }

    r.starvationCount = starveEvents;

    // Jain's Fairness Index
    // J = (Σ xᵢ)² / (n · Σ xᵢ²)
    int n = static_cast<int>(perThreadOps.size());
    if (n > 0) {
        double sumX  = 0.0;
        double sumX2 = 0.0;
        for (double x : perThreadOps) {
            sumX  += x;
            sumX2 += x * x;
        }
        if (sumX2 > 0.0) {
            r.jainsFairnessIndex = (sumX * sumX) / (static_cast<double>(n) * sumX2);
        } else {
            r.jainsFairnessIndex = 1.0;  // all zeros = perfectly equal (vacuously)
        }
    }

    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// toString
// ─────────────────────────────────────────────────────────────────────────────

std::string MetricsResult::toString() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "[" << algoLongName(algo) << "]"
        << " R=" << numReaders << " W=" << numWriters
        << " OPS=" << totalOps
        << " | AvgRWait=" << avgReaderWaitMs << "ms"
        << " AvgWWait=" << avgWriterWaitMs << "ms"
        << " Throughput=" << throughputOpsPerSec << "ops/s"
        << " Jain=" << std::setprecision(3) << jainsFairnessIndex
        << " Starve=" << starvationCount
        << " CSW(vol/inv)=" << contextSwitchesVol << "/" << contextSwitchesInv
        << " Duration=" << std::setprecision(1) << simulationDurationMs << "ms";
    return oss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// exportCSV
// ─────────────────────────────────────────────────────────────────────────────

void Metrics::exportCSV(const std::string& filepath, const MetricsResult& r) {
    // Check if file exists to decide whether to write header.
    struct stat st;
    bool writeHeader = (stat(filepath.c_str(), &st) != 0);

    FILE* f = fopen(filepath.c_str(), "a");
    if (!f) {
        fprintf(stderr, "[Metrics] WARNING: cannot open CSV '%s'\n", filepath.c_str());
        return;
    }

    if (writeHeader) {
        fprintf(f,
            "Algorithm,NumReaders,NumWriters,TotalOps,"
            "AvgReaderWaitMs,AvgWriterWaitMs,ThroughputOpsPerSec,"
            "JainsFairnessIndex,StarvationCount,"
            "ContextSwitchesVol,ContextSwitchesInv,DurationMs\n"
        );
    }

    fprintf(f,
        "%s,%d,%d,%d,%.2f,%.2f,%.2f,%.4f,%d,%ld,%ld,%.2f\n",
        algoLongName(r.algo),
        r.numReaders,
        r.numWriters,
        r.totalOps,
        r.avgReaderWaitMs,
        r.avgWriterWaitMs,
        r.throughputOpsPerSec,
        r.jainsFairnessIndex,
        r.starvationCount,
        r.contextSwitchesVol,
        r.contextSwitchesInv,
        r.simulationDurationMs
    );

    fclose(f);
}

// ─────────────────────────────────────────────────────────────────────────────
// printTable
// ─────────────────────────────────────────────────────────────────────────────

void Metrics::printTable(const std::vector<MetricsResult>& results) {
    if (results.empty()) return;

    // Header
    printf("\n%-20s %-6s %-6s %-8s %-12s %-12s %-14s %-8s %-8s\n",
        "Algorithm", "R", "W", "Ops",
        "AvgRWait(ms)", "AvgWWait(ms)", "Throughput(ops/s)",
        "Jain", "Starve");
    printf("%s\n", std::string(104, '-').c_str());

    for (const auto& r : results) {
        printf("%-20s %-6d %-6d %-8d %-12.1f %-12.1f %-14.1f %-8.3f %-8d\n",
            algoLongName(r.algo),
            r.numReaders,
            r.numWriters,
            r.totalOps,
            r.avgReaderWaitMs,
            r.avgWriterWaitMs,
            r.throughputOpsPerSec,
            r.jainsFairnessIndex,
            r.starvationCount
        );
    }
    printf("\n");
}
