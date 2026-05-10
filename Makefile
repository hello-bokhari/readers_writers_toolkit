# ─────────────────────────────────────────────────────────────────────────────
# Makefile — Readers–Writers Toolkit
# OS Spring 2026 | FAST-NUCES
#
# Targets:
#   all        Build simulator + test suite (default)
#   rw_toolkit Build the main simulator binary
#   rw_tests   Build the test suite binary
#   clean      Remove all build artifacts
#   run        Quick demo run (Reader-Priority, balanced load)
#   test       Build and run all test cases
#   benchmark  Run full benchmark across all algorithms and load profiles
# ─────────────────────────────────────────────────────────────────────────────

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
INCLUDES := -I include
LDFLAGS  := -lpthread

# Source files for the main simulator (excluding test files).
SIM_SRCS := \
    src/semaphore.cpp       \
    src/logger.cpp          \
	src/shared_resource.cpp \
    src/rw_lock_base.cpp    \
    src/reader_priority.cpp \
    src/writer_priority.cpp \
    src/fair_morris.cpp     \
    src/watchdog.cpp        \
    src/metrics.cpp         \
    src/main.cpp

# Source files for the test suite (shared sim sources + test files).
TEST_SRCS := \
    src/semaphore.cpp       \
    src/logger.cpp          \
	src/shared_resource.cpp \
    src/rw_lock_base.cpp    \
    src/reader_priority.cpp \
    src/writer_priority.cpp \
    src/fair_morris.cpp     \
    src/watchdog.cpp        \
    src/metrics.cpp         \
    tests/test_cases.cpp    \
    tests/test_runner.cpp

# Object directories.
OBJ_DIR      := build/obj
TEST_OBJ_DIR := build/test_obj

SIM_OBJS  := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SIM_SRCS))
TEST_OBJS := $(patsubst %.cpp,$(TEST_OBJ_DIR)/%.o,$(TEST_SRCS))

.PHONY: all rw_toolkit rw_tests clean run test benchmark dirs

# ── Default target ────────────────────────────────────────────────────────────
all: rw_toolkit rw_tests

# ── Create build directories ──────────────────────────────────────────────────
dirs:
	@mkdir -p $(OBJ_DIR)/src $(OBJ_DIR)/tests \
	          $(TEST_OBJ_DIR)/src $(TEST_OBJ_DIR)/tests \
	          tests/results

# ── Main simulator ────────────────────────────────────────────────────────────
rw_toolkit: dirs $(SIM_OBJS)
	@echo "  [LD] $@"
	$(CXX) $(CXXFLAGS) $(SIM_OBJS) -o $@ $(LDFLAGS)
	@echo "  ✓  Built: ./$@"

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  [CXX] $<"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ── Test suite ────────────────────────────────────────────────────────────────
rw_tests: dirs $(TEST_OBJS)
	@echo "  [LD] $@"
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) -o $@ $(LDFLAGS)
	@echo "  ✓  Built: ./$@"

$(TEST_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  [CXX] $<"
	$(CXX) $(CXXFLAGS) $(INCLUDES) -Itests -c $< -o $@

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	@rm -rf build rw_toolkit rw_tests tests/results/*.csv
	@echo "  ✓  Clean complete"

# ── Quick run (Reader-Priority, 5R+3W, 20 ops) ───────────────────────────────
run: rw_toolkit
	./rw_toolkit --algo rp --readers 5 --writers 3 --ops 20 --log standard

# ── Run all test cases ────────────────────────────────────────────────────────
test: rw_tests
	./rw_tests

# ── Full benchmark ────────────────────────────────────────────────────────────
benchmark: rw_toolkit
	@mkdir -p tests/results
	./rw_toolkit --benchmark --log summary --output tests/results/benchmark.csv
	@echo "  ✓  Benchmark results: tests/results/benchmark.csv"
