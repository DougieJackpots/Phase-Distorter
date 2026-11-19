#include "eb/apu_clock_batch.hpp"
#include "eb/spc700_audio_cpu.hpp"
#include "eb/snes_audio_dsp.hpp"
#include "eb/snapshot_archive.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <chrono>
#include <string_view>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace eb {
// Isolated bus with the actual production synchronization methods and mailbox
// blocks. It links the real source-owned SPC executor and DSP, without a ROM.
class SnesBus {
public:
    std::array<std::uint8_t, 4> main_to_audio_ports{}, audio_to_main_ports{};
    std::function<void(unsigned)> advance_audio_master_clocks;
    std::function<std::uint8_t(unsigned, std::uint8_t)> debug_read_wram, debug_write_wram, debug_read_rom;
    ApuClockBatch apu_clock_batch_;
    void begin_audio_batch();
    void end_audio_batch();
    void cancel_audio_batch() noexcept;
    void synchronize_audio();
    void advance_audio_time(unsigned clocks);
    std::uint8_t read_port(std::uint16_t address);
    void write_port(std::uint16_t address, std::uint8_t value);
};
#include "apu_sync_under_test.inc"
}
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Machine {
    eb::SnesBus bus;
    eb::Spc700AudioCpu spc{bus.main_to_audio_ports, bus.audio_to_main_ports};
    eb::SnesAudioDsp dsp{spc};
    std::vector<std::uint32_t> writes;

    Machine() {
        bus.advance_audio_master_clocks = [this](unsigned clocks) { spc.advance_master_clocks(clocks); };
        spc.observe_memory_write = [this](auto address, auto value) {
            writes.push_back((std::uint32_t(address) << 8) | value);
        };
        // Populate the verified source sites. These bytes select no new
        // operations; the linked compact executor validates the owned sites.
#define EB_SPC_SITE(address, opcode, operand, length) \
        for (unsigned i = 0; i < length; ++i) \
            spc.audio_ram[address + i] = std::uint8_t(i ? operand >> ((i - 1) * 8) : opcode);
#include "compact_spc_sites.inc"
#undef EB_SPC_SITE
    }
};
std::uint64_t pcm_samples = 0, barriers = 0;
void compare(Machine& reference, Machine& candidate) {
    candidate.bus.synchronize_audio();
    require(reference.bus.main_to_audio_ports == candidate.bus.main_to_audio_ports &&
            reference.bus.audio_to_main_ports == candidate.bus.audio_to_main_ports, "APU mailbox differs");
    eb::SnapshotArchive a, b;
    reference.spc.snapshot_io(a); reference.dsp.snapshot_io(a);
    candidate.spc.snapshot_io(b); candidate.dsp.snapshot_io(b);
    require(a.bytes() == b.bytes(), "SPC/DSP full state differs at synchronization barrier");
    require(reference.writes == candidate.writes, "Ordered SPC memory/port writes differ");
    auto left = reference.dsp.take_stereo_samples();
    auto right = candidate.dsp.take_stereo_samples();
    require(left == right, "Audio PCM differs at synchronization barrier");
    pcm_samples += left.size();
    reference.writes.clear(); candidate.writes.clear();
    ++barriers;
}
void advance(Machine& reference, Machine& candidate, unsigned clocks) {
    reference.bus.advance_audio_time(clocks);
    candidate.bus.advance_audio_time(clocks);
}
void write(Machine& reference, Machine& candidate, unsigned port, std::uint8_t value) {
    const auto address = std::uint16_t(0x2140 + (port & 63));
    reference.bus.write_port(address, value);
    candidate.bus.write_port(address, value);
    compare(reference, candidate);
}
std::uint8_t read(Machine& reference, Machine& candidate, unsigned port) {
    const auto address = std::uint16_t(0x2140 + (port & 63));
    const auto a = reference.bus.read_port(address), b = candidate.bus.read_port(address);
    require(a == b, "CPU-visible audio port read differs");
    compare(reference, candidate);
    return a;
}
void boot_transfer() {
    Machine reference, candidate;
    reference.bus.main_to_audio_ports = candidate.bus.main_to_audio_ports = {0x21, 0x42, 0x63, 0x84};
    candidate.bus.begin_audio_batch();
    auto wait_port = [&](std::uint8_t expected) {
        for (unsigned i = 0; i < 100000; ++i) {
            advance(reference, candidate, 6);
            // Poll less often than instruction clock delivery.
            if (i % 37 == 0 && read(reference, candidate, 0) == expected)
                return;
        }
        throw std::runtime_error("SPC IPL upload handshake timed out");
    };
    wait_port(0xaa);
    require(read(reference, candidate, 1) == 0xbb, "SPC IPL ready signature missing");
    write(reference, candidate, 0, 0xcc);
    write(reference, candidate, 1, 1);
    write(reference, candidate, 2, 0);
    write(reference, candidate, 3, 3);
    wait_port(0xcc);
    constexpr std::array<std::uint8_t, 9> data{0xb3, 0x01, 0x35, 0x67, 0x65, 0x31, 0xfe, 0xcb, 0xae};
    for (unsigned i = 0; i < data.size(); ++i) {
        write(reference, candidate, 1, data[i]);
        write(reference, candidate, 0, i);
        wait_port(std::uint8_t(i));
    }
    candidate.bus.end_audio_batch(); compare(reference, candidate);
    require(std::equal(data.begin(), data.end(), reference.spc.audio_ram.begin() + 0x300), "IPL data transfer differs");
}
void configure_voice(Machine& machine) {
    auto& ram = machine.spc.audio_ram;
    for (unsigned voice = 0; voice < 8; ++voice) {
        ram[0x200 + voice * 4] = ram[0x202 + voice * 4] = 0;
        ram[0x201 + voice * 4] = ram[0x203 + voice * 4] = 3;
    }
    constexpr std::array<std::uint8_t, 9> block{0xb3, 0x01, 0x35, 0x67, 0x65, 0x31, 0xfe, 0xcb, 0xae};
    std::copy(block.begin(), block.end(), ram.begin() + 0x300);
    for (unsigned r = 0; r < 128; ++r) machine.dsp.write_register(r, 0);
    for (unsigned voice = 0; voice < 8; ++voice) {
        machine.dsp.write_register(voice * 16, 0x70);
        machine.dsp.write_register(voice * 16 + 1, 0x50);
        machine.dsp.write_register(voice * 16 + 2, voice * 11);
        machine.dsp.write_register(voice * 16 + 3, 0x10);
        machine.dsp.write_register(voice * 16 + 4, voice);
        machine.dsp.write_register(voice * 16 + 7, 0x7f);
    }
    machine.dsp.write_register(0x5d, 2);
    machine.dsp.write_register(0x0c, 0x70); machine.dsp.write_register(0x1c, 0x70);
    machine.dsp.write_register(0x6d, 0x40); machine.dsp.write_register(0x7d, 1);
    machine.dsp.write_register(0x4d, 0xff); machine.dsp.write_register(0x0d, 0x30);
    machine.dsp.write_register(0x2c, 0x20); machine.dsp.write_register(0x3c, 0x20);
    machine.dsp.write_register(0x0f, 0x7f); machine.dsp.write_register(0x4c, 0xff);
    // Repeated RETs return to the source-owned port-read routine at $0625.
    for (unsigned i = 0; i < 256; ++i) ram[0x100 + i] = i & 1 ? 6 : 0x25;
    ram[4] = 0x7d;
    machine.spc.program_counter = 0x0625;
    machine.spc.stack_pointer = 0xef;
    machine.spc.write_byte(0xf1, 4); // Timer 2 enabled, IPL hidden.
    machine.spc.write_byte(0xfc, 7);
}
void source_driver() {
    Machine reference, candidate;
    configure_voice(reference); configure_voice(candidate);
    unsigned seed = 0x12345;
    auto random = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
    candidate.bus.begin_audio_batch();
    advance(reference, candidate, 65535);
    advance(reference, candidate, 1);
    compare(reference, candidate);
    advance(reference, candidate, 300000);
    advance(reference, candidate, 0);
    compare(reference, candidate);
    for (unsigned step = 0; step < 120000; ++step) {
        advance(reference, candidate, 1 + random() % 96);
        if (step % 131 == 0) write(reference, candidate, random() % 64, std::uint8_t(random()));
        if (step % 191 == 0) read(reference, candidate, random() % 64);
        if (step % 1024 == 0) {
            candidate.bus.end_audio_batch(); compare(reference, candidate);
            candidate.bus.begin_audio_batch();
        }
        if (step % 997 == 0) {
            // Mid-scheduler DMA/HDMA access occurs before the enclosing
            // scheduler call delivers its current elapsed-clock slice.
            write(reference, candidate, 0, std::uint8_t(random()));
            read(reference, candidate, 0);
            advance(reference, candidate, 32000);
        }
        if (step % 4096 == 0) {
            compare(reference, candidate);
            eb::SnapshotArchive saved;
            candidate.spc.snapshot_io(saved); candidate.dsp.snapshot_io(saved);
            const auto ports_in = candidate.bus.main_to_audio_ports;
            const auto ports_out = candidate.bus.audio_to_main_ports;
            candidate.bus.advance_audio_time(12345);
            candidate.bus.synchronize_audio();
            eb::SnapshotArchive restore(saved.bytes());
            candidate.spc.snapshot_io(restore); candidate.dsp.snapshot_io(restore); restore.finish();
            candidate.bus.main_to_audio_ports = ports_in; candidate.bus.audio_to_main_ports = ports_out;
            candidate.writes.clear();
            compare(reference, candidate);
            // Cross the delivery threshold repeatedly without mailbox traffic.
            for (unsigned quiet = 0; quiet < 64; ++quiet)
                advance(reference, candidate, 3000);
            candidate.bus.cancel_audio_batch();
            compare(reference, candidate);
            candidate.bus.begin_audio_batch();
        }
    }
    candidate.bus.end_audio_batch(); compare(reference, candidate);
    require(candidate.bus.apu_clock_batch_.callback_calls() * 10 < reference.bus.apu_clock_batch_.callback_calls(),
            "Clock delivery was not batched in the real SPC/DSP scenario");
    std::cout << "Source driver callbacks " << reference.bus.apu_clock_batch_.callback_calls()
              << " immediate, " << candidate.bus.apu_clock_batch_.callback_calls() << " batched\n";
}
void failure_and_bounds() {
    eb::ApuClockBatch batch;
    std::vector<unsigned> requests;
    eb::ApuClockBatch::Advance callback = [&](unsigned clocks) { requests.push_back(clocks); };
    batch.begin(callback); batch.advance(17, callback); batch.cancel();
    batch.synchronize(callback);
    require(requests == std::vector<unsigned>{17}, "Cancellation lost clock debt");
    batch.begin(callback);
    batch.advance(65535, callback);
    batch.advance(std::numeric_limits<unsigned>::max(), callback);
    batch.advance(0, callback); batch.end(callback);
    require(requests == std::vector<unsigned>{17, 65535, std::numeric_limits<unsigned>::max(), 0}, "Clock request overflow or zero-slice regression");
    batch.begin(callback); batch.advance(20, callback);
    eb::ApuClockBatch::Advance failing = [&](unsigned clocks) { requests.push_back(clocks); throw std::runtime_error("synthetic callback failure"); };
    try { batch.end(failing); throw std::logic_error("Failure was swallowed"); }
    catch (const std::runtime_error&) {}
    const auto count = requests.size(); batch.synchronize(callback);
    require(requests.size() == count, "Failed callback was replayed");
    // A paused or audited owner can use immediate delivery between batches.
    batch.advance(9, callback); require(requests.back() == 9, "Immediate delivery changed");
}
}
void benchmark() {
    for (bool batched : {false, true}) {
        std::array<double, 7> timings{};
        std::uint64_t checksum = 0, calls = 0;
        for (auto& milliseconds : timings) {
            Machine machine;
            configure_voice(machine);
            machine.spc.observe_memory_write = {}; // No audit overhead in timing.
            constexpr unsigned frames = 300;
            std::vector<std::int16_t> samples;
            samples.reserve(2048);
            unsigned requests_per_frame = 0, clock_sum = 0;
            while (clock_sum < 357366) {
                clock_sum += std::min(357366 - clock_sum, 12 + requests_per_frame % 25);
                ++requests_per_frame;
            }
            const auto start = std::chrono::steady_clock::now();
            for (unsigned frame = 0; frame < frames; ++frame) {
                if (batched) machine.bus.begin_audio_batch();
                unsigned remaining = 357366;
                for (unsigned instruction = 0; remaining; ++instruction) {
                    const unsigned clocks = std::min(remaining, 12 + instruction % 25);
                    if (batched) {
                        machine.bus.advance_audio_time(clocks);
                    } else if (machine.bus.advance_audio_master_clocks) {
                        machine.bus.advance_audio_master_clocks(clocks);
                    }
                    remaining -= clocks;
                    if (instruction % 700 == 0) {
                        if (batched) {
                            machine.bus.write_port(0x2140, std::uint8_t(instruction));
                            checksum += machine.bus.read_port(0x2140);
                        } else {
                            machine.bus.main_to_audio_ports[0] = std::uint8_t(instruction);
                            checksum += machine.bus.audio_to_main_ports[0];
                        }
                    }
                }
                machine.bus.end_audio_batch();
                machine.dsp.take_stereo_samples(samples);
                for (auto sample : samples)
                    checksum = checksum * 33 + std::uint16_t(sample);
            }
            milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count() / frames;
            calls += batched ? machine.bus.apu_clock_batch_.callback_calls()
                             : std::uint64_t(requests_per_frame) * frames;
        }
        std::sort(timings.begin(), timings.end());
        std::cout << "batched=" << batched << " audio_ms_per_frame=" << timings[timings.size() / 2]
                  << " callbacks=" << calls << " checksum=" << checksum << '\n';
    }
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
            benchmark();
            return 0;
        }
        failure_and_bounds(); boot_transfer(); source_driver();
        std::cout << barriers << " barriers preserve full SPC/DSP snapshots, ordered writes and " << pcm_samples << " PCM samples\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
