#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>

namespace eb {
// Owns only undelivered clock requests. The SPC and DSP still execute their
// original instructions and cycles when the owner reaches a synchronization
// barrier. The callback must keep the same owner for the duration of a batch.
class ApuClockBatch {
public:
    using Advance = std::function<void(unsigned)>;
    static constexpr unsigned maximum_pending_clocks = 65536;

    void begin(const Advance& advance) {
        if (active_)
            throw std::logic_error("Nested APU clock batch");
        synchronize(advance);
        active_ = true;
        ++batches_started_;
    }

    void end(const Advance& advance) {
        active_ = false;
        synchronize(advance);
    }

    // Preserve the original CPU exception. Its pending audio time is delivered
    // at the next diagnostics, snapshot, audio-consumption or execution barrier.
    void cancel() noexcept { active_ = false; }

    void advance(unsigned clocks, const Advance& callback) {
        if (!callback) {
            pending_clocks_ = 0;
            return;
        }
        if (!active_ || clocks == 0 || clocks >= maximum_pending_clocks) {
            synchronize(callback);
            deliver(clocks, callback);
            return;
        }
        // Both operands are below the threshold, so this sum cannot overflow.
        pending_clocks_ += clocks;
        if (pending_clocks_ >= maximum_pending_clocks)
            synchronize(callback);
    }

    void synchronize(const Advance& callback) {
        // Clear before invoking the owner. A partially failing SPC callback
        // keeps its own clock debt and must never receive this request twice.
        const unsigned clocks = std::exchange(pending_clocks_, 0);
        if (clocks && callback)
            deliver(clocks, callback);
    }

    std::uint64_t callback_calls() const noexcept { return callback_calls_; }
    std::uint64_t batches_started() const noexcept { return batches_started_; }

private:
    void deliver(unsigned clocks, const Advance& callback) {
        ++callback_calls_;
        callback(clocks);
    }

    unsigned pending_clocks_ = 0;
    bool active_ = false;
    std::uint64_t callback_calls_ = 0;
    std::uint64_t batches_started_ = 0;
};
}
