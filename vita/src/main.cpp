#include "eb/asset_store.hpp"
#include "eb/game_session.hpp"
#include "eb/frame_pacer.hpp"
#include "eb/audio_output.hpp"
#include "eb/logical_clock_policy.hpp"
#include "generated_assets.hpp"

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
        output << " saves=" << saves << '\n';
        output.flush();
        updates = frames = late = saves = 0;
        simulation = audio = upload = present = sleep = save = {};
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

int main(int, char**) {
    try {
        // Give the CPU-heavy game core the standard Vita performance clock.
        scePowerSetArmClockFrequency(444);
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0)
            throw std::runtime_error(std::string("SDL initialization failed: ") + SDL_GetError());
        std::filesystem::create_directories(data_dir);
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
        performance.output << "Vita performance diagnostics v2; simulation includes CPU, PPU and DSP\n";
#ifdef __OPTIMIZE__
        performance.output << "compiler_optimisation=enabled\n";
#else
        performance.output << "compiler_optimisation=DISABLED\n";
#endif
        performance.output << "compiler=" << __VERSION__
                           << " arm_mhz=" << scePowerGetArmClockFrequency() << '\n';
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
        performance.output.flush();

        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(
            SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, game_width, game_height),
            SDL_DestroyTexture);
        if (!texture)
            throw std::runtime_error(std::string("Could not create game framebuffer: ") + SDL_GetError());
        SDL_RenderSetLogicalSize(renderer.get(), game_width, game_height);

        eb::GameSession session(assets.image, assets.version);
        session.set_logical_clock_policy(eb::LogicalClockPolicy::ActorFrames);
        session.enable_native_sprite_runtime();
        session.configure_presentation(game_width, false, false);
        load_save(session);

        std::unique_ptr<eb::DeviceAudioQueue> audio;
        try {
            audio = std::make_unique<eb::DeviceAudioQueue>(eb::FramePacer::frame_rate);
        } catch (const std::exception& error) {
            std::cerr << "Audio disabled: " << error.what() << '\n';
        }

        // Use one host clock and avoid unbounded presentation catch-up.
        eb::FramePacer pacer(eb::FramePacer::Clock::now());
        bool running = true;
        auto last_save = std::chrono::steady_clock::now();
        while (running) {
            SDL_Event event{};
            while (SDL_PollEvent(&event))
                if (event.type == SDL_QUIT ||
                    (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
                    running = false;
            if (!running)
                break;

            const auto simulation_start = Clock::now();
            const auto frames = session.advance_frame(read_buttons());
            const auto audio_start = Clock::now();
            performance.simulation.add(milliseconds(simulation_start, audio_start));
            if (audio) {
                const auto samples = session.take_audio_samples();
                audio->append(samples);
            } else {
                (void)session.take_audio_samples();
            }

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
            if (save_start - last_save >= std::chrono::seconds(10)) {
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

        write_save(session);
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
