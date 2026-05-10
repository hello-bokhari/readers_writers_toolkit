# Readers–Writers Toolkit
## Operating Systems Project – Spring 2026  
FAST-NUCES  

### Group Members
- Abbad Hasan (24K-0600)  
- Muhammed Ahmed (24K-0845)  
- Talal Tariq (24K-0556)  

---

# 1. Introduction

The Readers–Writers problem is a classical synchronization problem in Operating Systems that deals with concurrent access to shared resources. Multiple readers are allowed to access the resource simultaneously, while writers require exclusive access.

This project implements and analyzes three variants:

- Reader-Priority (RP)
- Writer-Priority (WP)
- Fair (Morris Algorithm) (FA)

The goal is to evaluate fairness, starvation prevention, and performance under different workloads.

---

# 2. Objectives

- Implement correct synchronization using threads
- Prevent race conditions and deadlocks
- Compare three algorithms
- Measure performance using:
  - Waiting Time
  - Throughput
  - Fairness Index
  - Starvation Count

---

# 3. System Overview

The system simulates:

- Multiple Reader threads
- Multiple Writer threads
- Shared resource
- Synchronization using mutexes/semaphores
- Logging system for state transitions

---

---

# Build & Compile

The project uses a custom `Makefile` for building the simulator and test suite.

## Available Make Targets

| Target | Description |
|---|---|
| `make` / `make all` | Build simulator + test suite |
| `make rw_toolkit` | Build main simulator binary |
| `make rw_tests` | Build test suite binary |
| `make clean` | Remove build artifacts |
| `make run` | Quick demo run |
| `make test` | Run all test cases |
| `make benchmark` | Run benchmark suite |

---

## 1. Compile Everything

Build both:

- Main simulator (`rw_toolkit`)
- Test suite (`rw_tests`)

```bash
make
```

or

```bash
make all
```

Output:

```text
rw_toolkit
rw_tests
```

---

## 2. Compile Main Simulator Only

```bash
make rw_toolkit
```

Builds:

```text
./rw_toolkit
```

---

## 3. Compile Test Suite Only

```bash
make rw_tests
```

Builds:

```text
./rw_tests
```

---

## 4. Clean Build Files

Removes:

- build/
- rw_toolkit
- rw_tests
- benchmark CSV files

```bash
make clean
```

---

## 5. Quick Demo Run

Runs:

- Reader-Priority
- 5 readers
- 3 writers
- 20 operations

```bash
make run
```

Equivalent to:

```bash
./rw_toolkit --algo rp --readers 5 --writers 3 --ops 20 --log standard
```

---

## 6. Run Test Suite

Executes all automated test cases.

```bash
make test
```

Equivalent to:

```bash
./rw_tests
```

---

## 7. Run Full Benchmark

Runs:

- All algorithms
- All load profiles
- Summary mode
- Exports CSV

```bash
make benchmark
```

Equivalent to:

```bash
./rw_toolkit --benchmark --log summary --output tests/results/benchmark.csv
```

Output:

```text
tests/results/benchmark.csv
```

---

## Build Requirements

Required tools:

- g++
- pthread
- make

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential g++
```

Verify installation:

```bash
g++ --version
make --version
```

---

## Build Directory Structure

Generated after build:

```text
build/
├── obj/
├── test_obj/

rw_toolkit
rw_tests
tests/results/
```

---

# 4. Command Line Usage

```bash
./rw_toolkit [options]
```

## Important Options

| Option        | Description          |
|-------------|----------------------|
| `--algo`      | rp / wp / fa         |
| `--readers`   | Number of readers    |
| `--writers`   | Number of writers    |
| `--ops`       | Total operations     |
| `--log`       | verbose / summary    |
| `--logfile`   | Save logs            |
| `--starve`    | Starvation threshold |
| `--benchmark` | Run all tests        |
| `--output`    | Save CSV             |

---

# 5. Performance Evaluation

## 5.1 Benchmark Execution

```bash
./rw_toolkit --benchmark --output results.csv --logfile benchmark.log
```

This runs:

- All 3 algorithms
- All load profiles
- 5 runs each

---

## 5.2 Load Profiles

### Light Load

```bash
./rw_toolkit --algo rp --readers 3 --writers 1 --ops 100 --log summary
./rw_toolkit --algo wp --readers 3 --writers 1 --ops 100 --log summary
./rw_toolkit --algo fa --readers 3 --writers 1 --ops 100 --log summary
```

### Balanced Load

```bash
./rw_toolkit --algo rp --readers 5 --writers 5 --ops 500 --log summary
./rw_toolkit --algo wp --readers 5 --writers 5 --ops 500 --log summary
./rw_toolkit --algo fa --readers 5 --writers 5 --ops 500 --log summary
```

### Heavy Load

```bash
./rw_toolkit --algo rp --readers 10 --writers 10 --ops 1000 --log summary
./rw_toolkit --algo wp --readers 10 --writers 10 --ops 1000 --log summary
./rw_toolkit --algo fa --readers 10 --writers 10 --ops 1000 --log summary
```

---

## 5.3 Results Table
| Algorithm         | NumReaders | NumWriters | TotalOps | AvgReaderWaitMs | AvgWriterWaitMs | ThroughputOpsPerSec | Jain's Fairness Index | StarvationCount | ContextSwitchesVol | ContextSwitchesInv | DurationMs |
|------------------|------------|------------|----------|------------------|------------------|----------------------|------------------------|-----------------|--------------------|--------------------|------------|
| Reader-Priority   | 3          | 1          | 100      | 0.92             | 2980.6           | 28.62                | 0.768                  | 1               | 193                | 0                  | 3495.01    |
| Writer-Priority   | 3          | 1          | 100      | 1147.36          | 41.8             | 8.16                 | 0.5576                 | 23              | 503                | 0                  | 12338.97   |
| Fair (Morris)     | 3          | 1          | 100      | 185.54           | 93.87            | 14.42                | 0.9929                 | 1               | 505                | 0                  | 6946.64    |
| Reader-Priority   | 5          | 5          | 500      | 0                | 10252.68         | 46.69                | 0.5101                 | 5               | 869                | 0                  | 10707.87   |
| Writer-Priority   | 5          | 5          | 500      | 42893.12         | 654.88           | 6.75                 | 0.4869                 | 109             | 2566               | 0                  | 74110.3    |

---

## 5.4 Observations

### Reader-Priority

- High reader throughput
- Writer starvation occurs
- Low fairness

### Writer-Priority

- Writers get fast access
- Readers may starve
- Biased behavior

### Fair (Morris)

- Balanced execution
- No starvation
- Highest fairness index

---

# 6. Test Cases

## TC-01 Concurrent Reads

```bash
./rw_toolkit --algo rp --readers 8 --writers 0 --ops 8 --log verbose
```

✔ Expected: All readers ACTIVE simultaneously

---

## TC-02 Exclusive Write

```bash
./rw_toolkit --algo rp --readers 0 --writers 1 --ops 1 --log verbose
```

✔ Expected: Writer gets exclusive access

---

## TC-03 Read-Write Conflict

```bash
./rw_toolkit --algo rp --readers 4 --writers 1 --ops 20 --log verbose
```

✔ Expected: Writer waits

---

## TC-04 Writer Starvation

```bash
./rw_toolkit --algo rp --readers 10 --writers 1 --ops 200 --starve 500 --log verbose
```

✔ Expected: Writer blocked > 500ms

---

## TC-05 Reader Starvation

```bash
./rw_toolkit --algo wp --readers 5 --writers 10 --ops 200 --starve 500 --log verbose
```

✔ Expected: Readers blocked

---

## TC-06 High Contention

```bash
./rw_toolkit --algo fa --readers 10 --writers 10 --ops 500 --log verbose
```

✔ Expected: No deadlock

---

## TC-07 Zero Readers

```bash
./rw_toolkit --algo wp --readers 0 --writers 5 --ops 50
```

---

## TC-08 Zero Writers

```bash
./rw_toolkit --algo rp --readers 8 --writers 0 --ops 50
```

---

## TC-09 Single Thread

```bash
./rw_toolkit --readers 1 --writers 0 --ops 1
```

---

## TC-10 Rapid Re-entry

```bash
./rw_toolkit --algo fa --readers 3 --writers 0 --ops 300
```

---

## TC-11 Fairness Validation

```bash
./rw_toolkit --algo fa --readers 5 --writers 5 --ops 500 --log summary
```

---

## TC-12 Abort Test

```bash
./rw_toolkit --algo wp --abort-test --log verbose
```

---

# 7. Logging System

## Log Format

```text
[HH:MM:SS.mmm] | TID:<id> | TYPE:<R|W> | ALGO:<RP|WP|FA> | STATE:<state> | WAIT:<Xms> | MSG:<detail>
```

## Generate Logs

```bash
./rw_toolkit --algo fa --readers 5 --writers 5 --ops 100 --log verbose --logfile fair.log
```

---
# 10. Conclusion

This project successfully demonstrates:

- Correct synchronization
- Deadlock-free execution
- Starvation behavior in priority algorithms
- Fair scheduling using Morris algorithm

## Final Insight:

- RP → Best for read-heavy systems  
- WP → Best for write-critical systems  
- FA → Best overall fairness and stability  

---

# 11. Future Improvements

- GUI visualization
- Real-time graphs
- Advanced scheduling policies
- Distributed system extension

---

# 12. How to Reproduce Results

Run:

```bash
make
./rw_toolkit --benchmark --output results.csv --logfile benchmark.log
```

---

# END OF REPORT
