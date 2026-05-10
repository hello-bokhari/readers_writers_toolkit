
#pragma once

#include <cstdint>

class SharedResource {
public:
    SharedResource() : data_(0), readCount_(0), writeCount_(0) {}

    // ── Reader operations ─────────────────────────────────────────────────

    int read() const { return data_; }

    /** Increment internal read-operation counter (for metrics). */
    void recordRead() { ++readCount_; }

    // ── Writer operations ─────────────────────────────────────────────────

    void write(int value) { data_ = value; }

    void increment() { ++data_; }

    void recordWrite() { ++writeCount_; }

    // ── Accessors ─────────────────────────────────────────────────────────

    int data()       const { return data_;       }
    int readCount()  const { return readCount_;  }
    int writeCount() const { return writeCount_; }

    void reset() {
        data_       = 0;
        readCount_  = 0;
        writeCount_ = 0;
    }

private:
    int data_;        ///< The shared integer value
    int readCount_;   ///< Cumulative reads performed
    int writeCount_;  ///< Cumulative writes performed
};
