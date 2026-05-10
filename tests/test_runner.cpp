#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "test_cases.hpp"
#include "logger.hpp"
#include "config.hpp"

int main(int argc, char* argv[]) {

    int only = -1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--tc") == 0 && i + 1 < argc)
            only = atoi(argv[++i]);
    }

    Logger::instance().init(LogLevel::STANDARD);

    printf("\n=== Readers–Writers Test Suite ===\n");

    using TCFn = TestResult(*)();

    struct TCEntry { int n; TCFn fn; };

    TCEntry tcs[] = {
        { 1,  tc01_concurrentReads },
        { 2,  tc02_exclusiveWrite },
        { 3,  tc03_readWriteConflict },
        { 4,  tc04_writerStarvation },
        { 5,  tc05_readerStarvation },
        { 6,  tc06_highContention },
        { 7,  tc07_zeroReaders },
        { 8,  tc08_zeroWriters },
        { 9,  tc09_singleThread },
        { 10, tc10_rapidReentry },
        { 11, tc11_fairnessValidation },
        { 12, tc12_interruptAbort }
    };

    std::vector<TestResult> results;

    for (auto& tc : tcs) {

        if (only != -1 && tc.n != only)
            continue;

        printf("\nRunning TC-%02d...\n", tc.n);

        TestResult r = tc.fn();

        printf("[%s] %s\n",
            r.passed ? "PASS" : "FAIL",
            r.name.c_str());

        printf("Details: %s\n", r.detail.c_str());

        results.push_back(r);
    }

    Logger::instance().shutdown();

    for (auto& r : results)
        if (!r.passed) return 1;

    return 0;
}