#pragma once
#include <chrono>
#include <cstdint>

namespace eb {
// Host measurements only. These never advance or alter the emulated clocks.
struct RuntimeTiming {
    bool sampled = false;
    std::uint64_t cpu_hardware_ns = 0, ppu_ns = 0, dsp_ns = 0;
    std::uint64_t ppu_calls = 0, dsp_calls = 0;
    std::uint64_t ppu_state_ns = 0, ppu_compose_ns = 0;
};
namespace runtime_timing {
enum class Phase : unsigned { CpuHardware, Ppu, Dsp, PpuState, PpuCompose };
#ifdef EB_RUNTIME_TIMING
struct Context {
    using Clock = std::chrono::steady_clock;
    RuntimeTiming* result;
    Clock::time_point last;
    Phase phase = Phase::CpuHardware;
    void charge(Clock::time_point now) {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count();
        switch (phase) {
        case Phase::CpuHardware: result->cpu_hardware_ns += ns; break;
        case Phase::Ppu: result->ppu_ns += ns; break;
        case Phase::Dsp: result->dsp_ns += ns; break;
        case Phase::PpuState: result->ppu_ns += ns; result->ppu_state_ns += ns; break;
        case Phase::PpuCompose: result->ppu_ns += ns; result->ppu_compose_ns += ns; break;
        }
        last = now;
    }
};
inline thread_local Context* active = nullptr;
#endif
// Sample entire session updates, so short DSP calls and scanlines are measured
// only on selected updates. Other updates do not read the host clock here.
class Frame {
public:
    Frame(RuntimeTiming& result, bool sample)
#ifdef EB_RUNTIME_TIMING
        : context_{&result, {}}, previous_(active)
#endif
    {
        result = {};
#ifdef EB_RUNTIME_TIMING
        result.sampled = sample;
        if (sample) context_.last = Context::Clock::now();
        active = sample ? &context_ : nullptr;
#else
        (void)sample;
#endif
    }
    ~Frame() {
#ifdef EB_RUNTIME_TIMING
        if (context_.result->sampled) context_.charge(Context::Clock::now());
        active = previous_;
#endif
    }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
private:
#ifdef EB_RUNTIME_TIMING
    Context context_;
    Context* previous_;
#endif
};
class Scope {
public:
    explicit Scope(Phase phase) {
#ifdef EB_RUNTIME_TIMING
        context_ = active;
        if (!context_) return;
        context_->charge(Context::Clock::now());
        previous_ = context_->phase;
        context_->phase = phase;
        if (phase == Phase::Ppu) ++context_->result->ppu_calls;
        if (phase == Phase::Dsp) ++context_->result->dsp_calls;
#else
        (void)phase;
#endif
    }
    ~Scope() {
#ifdef EB_RUNTIME_TIMING
        if (!context_) return;
        context_->charge(Context::Clock::now());
        context_->phase = previous_;
#endif
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
#ifdef EB_RUNTIME_TIMING
    Context* context_ = nullptr;
    Phase previous_ = Phase::CpuHardware;
#endif
};
} // namespace runtime_timing
} // namespace eb
