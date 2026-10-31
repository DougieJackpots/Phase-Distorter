#include "eb/asset_store.hpp"
#include "eb/game_session.hpp"
#include "eb/frame_pacer.hpp"
#include "eb/audio_output.hpp"
#include "eb/logical_clock_policy.hpp"
#include "generated_assets.hpp"
#include "eb/input_replay.hpp"
#include "performance_options.hpp"
#include <array>

#include <SDL.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef EB_VITA_COMPACT_SPC_DISPATCH
#define EB_VITA_COMPACT_SPC_DISPATCH 0
#endif
#ifndef EB_VITA_BUILD_TYPE
#define EB_VITA_BUILD_TYPE "unreported"
#endif
namespace {
constexpr int game_width = 256;
constexpr int game_height = 224;
const std::filesystem::path data_dir{"ux0:data/PhaseDistorter"};
const std::filesystem::path asset_file = data_dir / "game.ebpak";
std::filesystem::path save_file;
std::vector<std::uint8_t> saved_memory;
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}
struct Timing {
    double total = 0, maximum = 0;
    void add(double value) { total += value; maximum = std::max(maximum, value); }
};
struct Performance {
    std::ofstream output{data_dir / "performance.log", std::ios::trunc};
    Clock::time_point epoch = Clock::now();
    std::uint64_t updates = 0, frames = 0, late = 0, saves = 0;
    Timing simulation, audio, upload, present, sleep, save;
    Timing cpu_hardware, ppu, dsp, ppu_state, ppu_compose;
    std::array<double, 512> simulation_samples{};
    std::size_t sample_count = 0;
    std::uint64_t over_budget = 0, composed = 0, skipped = 0;
    std::uint64_t main_instructions = 0, spc_instructions = 0, master_clocks = 0, native_batches = 0;
    std::uint64_t fast_ppu_rows=0,canonical_ppu_rows=0;
    void add_simulation(double ms) {
        simulation.add(ms);
        if (sample_count < simulation_samples.size()) simulation_samples[sample_count++] = ms;
        if (ms > 1000.0 / eb::FramePacer::frame_rate) ++over_budget;
    }
    std::uint64_t core_samples = 0, ppu_calls = 0, dsp_calls = 0;
    void add_core(const eb::RuntimeTiming& timing) {
        if (!timing.sampled) return;
        ++core_samples;
        cpu_hardware.add(timing.cpu_hardware_ns / 1e6);
        ppu.add(timing.ppu_ns / 1e6);
        dsp.add(timing.dsp_ns / 1e6);
        ppu_state.add(timing.ppu_state_ns / 1e6);
        ppu_compose.add(timing.ppu_compose_ns / 1e6);
        ppu_calls += timing.ppu_calls;
        dsp_calls += timing.dsp_calls;
    }
    void report(Clock::time_point now, bool force = false) {
        const double seconds = std::chrono::duration<double>(now - epoch).count();
        if (!updates || (!force && seconds < 5)) return;
        output << std::fixed << std::setprecision(3)
               << "seconds=" << seconds << " game_fps=" << frames / seconds
               << " displayed_fps=" << updates / seconds << " late=" << late;
        const auto write = [&](const char* name, const Timing& timing, std::uint64_t count) {
            output << ' ' << name << "_avg_ms=" << timing.total / std::max<std::uint64_t>(count, 1)
                   << ' ' << name << "_max_ms=" << timing.maximum;
        };
        write("simulation", simulation, updates); write("audio_submit", audio, updates);
        write("upload", upload, updates); write("present", present, updates);
        write("sleep", sleep, updates); write("save", save, saves);
        write("cpu_hardware_sample", cpu_hardware, core_samples);
        write("ppu_sample", ppu, core_samples); write("dsp_sample", dsp, core_samples);
        write("ppu_state_sample", ppu_state, core_samples);
        write("ppu_compose_sample", ppu_compose, core_samples);
        auto sorted = simulation_samples;
        std::sort(sorted.begin(), sorted.begin() + sample_count);
        if (sample_count)
            output << " simulation_p50_ms=" << sorted[(sample_count * 50 + 99) / 100 - 1]
                   << " simulation_p95_ms=" << sorted[(sample_count * 95 + 99) / 100 - 1];
        output << " distribution_samples=" << sample_count << " over_budget_updates=" << over_budget
               << " over_budget_fraction=" << double(over_budget) / updates
               << " composed_frames=" << composed << " skipped_frames=" << skipped
               << " composed_fps=" << composed / seconds;
        output << " fast_ppu_rows=" << fast_ppu_rows << " canonical_ppu_rows=" << canonical_ppu_rows
               << " fast_ppu_fraction=" << double(fast_ppu_rows) / std::max<std::uint64_t>(fast_ppu_rows+canonical_ppu_rows,1);
        output << " main_instructions_per_game_frame=" << double(main_instructions) / std::max<std::uint64_t>(frames, 1)
               << " spc_instructions_per_game_frame=" << double(spc_instructions) / std::max<std::uint64_t>(frames, 1)
               << " master_clocks_per_game_frame=" << double(master_clocks) / std::max<std::uint64_t>(frames, 1)
               << " simulation_ns_per_main_instruction=" << simulation.total * 1e6 / std::max<std::uint64_t>(main_instructions, 1)
               << " native_batches=" << native_batches;
        output << " core_samples=" << core_samples << " ppu_calls=" << ppu_calls
               << " dsp_calls=" << dsp_calls;
        output << " saves=" << saves << '\n';
        output.flush();
        updates = frames = late = saves = 0;
        simulation = audio = upload = present = sleep = save = {};
        cpu_hardware = ppu = dsp = ppu_state = ppu_compose = {};
        sample_count = 0;
        over_budget = composed = skipped = 0;
        main_instructions = spc_instructions = master_clocks = native_batches = 0;
        fast_ppu_rows=canonical_ppu_rows=0;
        core_samples = ppu_calls = dsp_calls = 0;
        epoch = Clock::now();
    }
};

std::uint16_t read_buttons() {
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    const auto p = pad.buttons;
    std::uint16_t buttons = 0;
    // SNES JOY1 bit layout, matching the desktop keyboard mapping:
    // B, Y, Select, Start, directions, A, X, L, R.
    if (p & SCE_CTRL_CIRCLE)    buttons |= 1u << 15; // B
    if (p & SCE_CTRL_TRIANGLE)  buttons |= 1u << 14; // Y
    if (p & SCE_CTRL_SELECT)    buttons |= 1u << 13;
    if (p & SCE_CTRL_START)     buttons |= 1u << 12;
    if (p & SCE_CTRL_UP)        buttons |= 1u << 11;
    if (p & SCE_CTRL_DOWN)      buttons |= 1u << 10;
    if (p & SCE_CTRL_LEFT)      buttons |= 1u << 9;
    if (p & SCE_CTRL_RIGHT)     buttons |= 1u << 8;
    if (p & SCE_CTRL_CROSS)     buttons |= 1u << 7; // A
    if (p & SCE_CTRL_SQUARE)    buttons |= 1u << 6; // X
    if (p & SCE_CTRL_LTRIGGER)  buttons |= 1u << 5;
    if (p & SCE_CTRL_RTRIGGER)  buttons |= 1u << 4;
    return buttons;
}

void load_save(eb::GameSession& session) {
    if (!std::filesystem::exists(save_file))
        return;
    auto memory = session.save_memory();
    std::ifstream input(save_file, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char*>(memory.data()),
                              static_cast<std::streamsize>(memory.size())))
        throw std::runtime_error("The save file is unreadable or incomplete.");
    if (input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("The save file has an unexpected size.");
    saved_memory.assign(memory.begin(), memory.end());
}

bool write_save(eb::GameSession& session) {
    const auto memory = session.save_memory();
    if (std::equal(memory.begin(), memory.end(), saved_memory.begin(), saved_memory.end()))
        return false;
    const auto temporary = save_file.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char*>(memory.data()),
                                 static_cast<std::streamsize>(memory.size())))
        throw std::runtime_error("Could not write the save file.");
    output.close();
    if (!output)
        throw std::runtime_error("Could not finish writing the save file.");
    std::filesystem::rename(temporary, save_file);
    saved_memory.assign(memory.begin(), memory.end());
    return true;
}

void show_error(const std::string& message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Phase Distorter", message.c_str(), nullptr);
}
} // namespace

int main(int argc, char** argv) {
    try {
        // Give the CPU-heavy game core the standard Vita performance clock.
        scePowerSetArmClockFrequency(444);
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0)
            throw std::runtime_error(std::string("SDL initialization failed: ") + SDL_GetError());
        std::filesystem::create_directories(data_dir);
        vita::PerformanceOptions options;
        std::ifstream config(data_dir / "performance.cfg");
        if (config) options.read(config);
        options.arguments(argc, argv);
        const auto script_path = options.input_script.empty() ? std::string{} :
            (data_dir / options.input_script).string();
        eb::InputReplay replay(eb::input_script(script_path));
        if (!std::filesystem::exists(asset_file))
            throw std::runtime_error(
                "No game asset pack was found. On your PC, use the desktop version to "
                "import your own supported EarthBound/Mother 2 ROM, then copy the resulting "
                "game.ebpak file to ux0:data/PhaseDistorter/ on your Vita.");

        auto assets = eb::load_game_assets(asset_file, eb::asset_profiles());
        save_file = data_dir / (assets.version == eb::GameVersion::US
            ? "earthbound-us.srm" : "mother2-jp.srm");
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

        Performance performance;
        if (!performance.output)
            std::cerr << "Could not open performance.log; gameplay will continue.\n";
        performance.output << "Vita performance diagnostics v4; sampled core timings include instrumentation overhead\n";
        performance.output << "native_compositor_revision=3\n";
#ifdef __OPTIMIZE__
        performance.output << "compiler_optimisation=enabled\n";
#else
        performance.output << "compiler_optimisation=DISABLED\n";
#endif
        performance.output << "compiler=" << __VERSION__
                           << " arm_mhz=" << scePowerGetArmClockFrequency() << '\n';
        performance.output << "enhanced_timing=" << options.enhanced_timing
            << " composition_stride=" << options.composition_stride
            << " core_timing_runtime=" << options.core_timing << " replay_only=" << options.replay_only << " save_enabled=" << options.save_enabled
            << " input_script=" << options.input_script << " replay_frames=" << options.frames << '\n';
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Phase Distorter", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             960, 544, SDL_WINDOW_SHOWN), SDL_DestroyWindow);
        if (!window)
            throw std::runtime_error(std::string("Could not create Vita display: ") + SDL_GetError());

        SDL_Renderer* raw_renderer = SDL_CreateRenderer(window.get(), -1,
            SDL_RENDERER_ACCELERATED);
        if (!raw_renderer)
            raw_renderer = SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE);
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(raw_renderer,
                                                                                SDL_DestroyRenderer);
        if (!renderer)
            throw std::runtime_error(std::string("Could not create SDL renderer: ") + SDL_GetError());

        SDL_RendererInfo renderer_info{};
        if (SDL_GetRendererInfo(renderer.get(), &renderer_info) == 0)
            performance.output << "renderer=" << (renderer_info.name ? renderer_info.name : "unknown")
                << " flags=" << renderer_info.flags
                << " software=" << bool(renderer_info.flags & SDL_RENDERER_SOFTWARE) << '\n';
#ifdef EB_RUNTIME_TIMING
        performance.output << "core_timing=enabled interval_updates=61 cpu_hardware_includes_spc_cpu=1\n";
#else
        performance.output << "core_timing=disabled\n";
#endif
        performance.output.flush();

        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(
            SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, game_width, game_height),
            SDL_DestroyTexture);
        if (!texture)
            throw std::runtime_error(std::string("Could not create game framebuffer: ") + SDL_GetError());
        SDL_RenderSetLogicalSize(renderer.get(), game_width, game_height);

        eb::GameSession session(assets.image, assets.version, options.enhanced_timing);
        session.set_logical_clock_policy(eb::LogicalClockPolicy::ActorFrames);
        session.enable_native_sprite_runtime();
        session.configure_presentation(game_width, false, false);
        session.set_core_fast_paths(options.fast_memory,options.fast_scheduler,options.fast_composition);
        session.set_composition_stride(options.composition_stride);
        if (options.save_enabled) load_save(session);
        session.enable_runtime_timing(options.core_timing);
        performance.output << "fast_core_compiled_mask=" << session.compiled_core_fast_paths()
            << " fast_composition_requested=" << options.fast_composition << " fast_memory_requested=" << options.fast_memory << " fast_scheduler_requested=" << options.fast_scheduler << " compact_spc_dispatch=" << EB_VITA_COMPACT_SPC_DISPATCH << " build_configuration=" << EB_VITA_BUILD_TYPE
            << " session_unit_optimisation=" << session.session_unit_optimised()
            << " dsp_unit_optimisation=" << session.dsp_unit_optimised() << '\n';
        performance.output.flush();

        std::unique_ptr<eb::DeviceAudioQueue> audio;
        try {
            audio = std::make_unique<eb::DeviceAudioQueue>(eb::FramePacer::frame_rate);
        } catch (const std::exception& error) {
            std::cerr << "Audio disabled: " << error.what() << '\n';
        }

        // Producer and consumer exchange their retained buffers each frame.
        std::vector<std::int16_t> audio_samples;
        audio_samples.reserve(2048);

        // Use one host clock and avoid unbounded presentation catch-up.
        eb::FramePacer pacer(eb::FramePacer::Clock::now());
        bool running = true;
        auto last_save = std::chrono::steady_clock::now();
        auto previous_composition = session.composition_stats();
        auto previous_diagnostics = session.diagnostics();
        auto previous_native_rows=session.native_composition_counts();
        while (running) {
            SDL_Event event{};
            while (SDL_PollEvent(&event))
                if (event.type == SDL_QUIT ||
                    (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
                    running = false;
            if (!running || (options.frames && session.frames() >= options.frames))
                break;

            const auto simulation_start = Clock::now();
            const auto scripted = replay.buttons_for_frame(session.frames());
            const auto buttons = options.replay_only ? scripted : std::uint16_t(read_buttons() | scripted);
            const auto frames = session.advance_frame(buttons);
            const auto audio_start = Clock::now();
            performance.add_simulation(milliseconds(simulation_start, audio_start));
            performance.add_core(session.last_runtime_timing());
            const auto composition = session.composition_stats();
            performance.composed += composition.composed - previous_composition.composed;
            performance.skipped += composition.skipped - previous_composition.skipped;
            previous_composition = composition;
            const auto diagnostics = session.diagnostics();
            performance.main_instructions += diagnostics.cpu_instructions - previous_diagnostics.cpu_instructions;
            performance.spc_instructions += diagnostics.audio_cpu_instructions - previous_diagnostics.audio_cpu_instructions;
            performance.master_clocks += diagnostics.master_clocks - previous_diagnostics.master_clocks;
            performance.native_batches += diagnostics.native_gameplay_batches - previous_diagnostics.native_gameplay_batches;
            previous_diagnostics = diagnostics;
            const auto native_rows=session.native_composition_counts();
            performance.fast_ppu_rows+=native_rows[0]-previous_native_rows[0];
            performance.canonical_ppu_rows+=native_rows[1]-previous_native_rows[1];
            previous_native_rows=native_rows;
            session.take_audio_samples(audio_samples);
            if (audio)
                audio->append(audio_samples);

            const auto upload_start = Clock::now();
            performance.audio.add(milliseconds(audio_start, upload_start));
            const auto pixels = session.native_pixels();
            if (SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), game_width * sizeof(std::uint32_t)) != 0)
                throw std::runtime_error(std::string("Framebuffer upload failed: ") + SDL_GetError());
            const auto present_start = Clock::now();
            performance.upload.add(milliseconds(upload_start, present_start));
            SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
            SDL_RenderClear(renderer.get());
            SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
            SDL_RenderPresent(renderer.get());
            const auto save_start = Clock::now();
            performance.present.add(milliseconds(present_start, save_start));
            if (options.save_enabled && save_start - last_save >= std::chrono::seconds(10)) {
                if (write_save(session)) {
                    ++performance.saves;
                    performance.save.add(milliseconds(save_start, Clock::now()));
                }
                last_save = Clock::now();
            }
            // Always display a completed update. When the core cannot keep up,
            // discard the host backlog rather than hiding many consecutive images.
            const auto sleep_start = Clock::now();
            (void)pacer.advance(sleep_start, frames);
            if (sleep_start < pacer.deadline()) {
                std::this_thread::sleep_until(pacer.deadline());
            } else {
                ++performance.late;
                pacer.set_rate(sleep_start, eb::FramePacer::frame_rate);
            }
            const auto now = Clock::now();
            performance.sleep.add(milliseconds(sleep_start, now));
            ++performance.updates;
            performance.frames += frames;
            performance.report(now);
        }
        performance.report(Clock::now(), true);

        if (options.save_enabled) write_save(session);
        audio.reset();
        texture.reset();
        renderer.reset();
        window.reset();
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Phase Distorter Vita: " << error.what() << '\n';
        if (SDL_WasInit(0))
            show_error(error.what());
        SDL_Quit();
        return 1;
    }
}
