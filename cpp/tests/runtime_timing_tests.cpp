#include "eb/runtime_timing.hpp"
#include <stdexcept>
#include <iostream>

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        using namespace eb;
        RuntimeTiming result;
        {
            runtime_timing::Frame frame(result, true);
            for (unsigned i = 0; i < 224; ++i) {
                runtime_timing::Scope ppu(runtime_timing::Phase::Ppu);
                runtime_timing::Scope state(runtime_timing::Phase::PpuState);
                runtime_timing::Scope compose(runtime_timing::Phase::PpuCompose);
                runtime_timing::Scope dsp(runtime_timing::Phase::Dsp);
            }
        }
#ifdef EB_RUNTIME_TIMING
        require(result.sampled && result.ppu_calls == 224 && result.dsp_calls == 224,
                "Nested scopes lost or duplicated phase calls");
        require(result.cpu_hardware_ns + result.ppu_ns + result.dsp_ns > 0,
                "Sampled frame recorded no elapsed time");
        require(result.ppu_ns >= result.ppu_state_ns + result.ppu_compose_ns,
                "PPU subphase accounting exceeded total PPU time");
        require(runtime_timing::active == nullptr, "Frame leaked a timing context");
#else
        require(!result.sampled && !result.ppu_calls && !result.dsp_calls,
                "Compiled-out profiler recorded timings");
#endif
        {
            runtime_timing::Frame frame(result, false);
            runtime_timing::Scope ppu(runtime_timing::Phase::Ppu);
            runtime_timing::Scope dsp(runtime_timing::Phase::Dsp);
        }
        require(!result.sampled && !result.cpu_hardware_ns && !result.ppu_ns && !result.dsp_ns &&
                !result.ppu_calls && !result.dsp_calls, "Unsampled update retained stale timings");
        try {
            runtime_timing::Frame frame(result, true);
            runtime_timing::Scope dsp(runtime_timing::Phase::Dsp);
            throw 42;
        } catch (int) {}
#ifdef EB_RUNTIME_TIMING
        require(result.sampled && result.dsp_calls == 1 && runtime_timing::active == nullptr,
                "Exception unwinding leaked a phase or frame context");
        {
            runtime_timing::Scope outside(runtime_timing::Phase::Ppu);
        }
        require(result.ppu_calls == 0, "Scope outside a frame modified old counters");
#endif
        std::cout << "Runtime timing sampling, nested phases, reset and exception cleanup passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
