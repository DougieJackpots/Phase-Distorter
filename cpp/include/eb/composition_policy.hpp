#pragma once
#include <cstdint>
#include <stdexcept>
namespace eb {
struct CompositionStats {
    std::uint64_t composed = 0, skipped = 0, last_composed_frame = 0;
};
// Host-only picture policy. Hardware frame zero is composed so startup has a
// valid picture; hardware timing and input selection never depend on this policy.
class CompositionPolicy {
public:
    void set_stride(unsigned stride) {
        if (stride < 1 || stride > 3) throw std::invalid_argument("Composition stride must be 1, 2 or 3");
        stride_ = stride;
    }
    unsigned stride() const { return stride_; }
    bool compose(std::uint64_t zero_based_frame) const { return zero_based_frame % stride_ == 0; }
    void completed(std::uint64_t zero_based_frame) {
        if (compose(zero_based_frame)) {
            ++stats_.composed;
            stats_.last_composed_frame = zero_based_frame + 1;
        } else ++stats_.skipped;
    }
    CompositionStats stats() const { return stats_; }
private:
    unsigned stride_ = 1;
    CompositionStats stats_;
};
} // namespace eb
