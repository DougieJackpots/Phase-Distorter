#include "eb/asset_store.hpp"
#include "eb/game_session.hpp"
#include "eb/audio_output.hpp"
#include "eb/logical_clock_policy.hpp"
#include "generated_assets.hpp"

#include <SDL.h>
#include <psp2/ctrl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
}

void write_save(eb::GameSession& session) {
    const auto memory = session.save_memory();
    const auto temporary = save_file.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char*>(memory.data()),
                                 static_cast<std::streamsize>(memory.size())))
        throw std::runtime_error("Could not write the save file.");
    output.close();
    if (!output)
        throw std::runtime_error("Could not finish writing the save file.");
    std::filesystem::rename(temporary, save_file);
}

void show_error(const std::string& message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Phase Distorter", message.c_str(), nullptr);
}
} // namespace

int main(int, char**) {
    try {
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

        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Phase Distorter", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             960, 544, SDL_WINDOW_SHOWN), SDL_DestroyWindow);
        if (!window)
            throw std::runtime_error(std::string("Could not create Vita display: ") + SDL_GetError());

        SDL_Renderer* raw_renderer = SDL_CreateRenderer(window.get(), -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!raw_renderer)
            raw_renderer = SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE);
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(raw_renderer,
                                                                                SDL_DestroyRenderer);
        if (!renderer)
            throw std::runtime_error(std::string("Could not create SDL renderer: ") + SDL_GetError());

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
            audio = std::make_unique<eb::DeviceAudioQueue>(60.0);
        } catch (const std::exception& error) {
            std::cerr << "Audio disabled: " << error.what() << '\n';
        }

        bool running = true;
        auto last_save = std::chrono::steady_clock::now();
        while (running) {
            const auto frame_start = std::chrono::steady_clock::now();
            SDL_Event event{};
            while (SDL_PollEvent(&event))
                if (event.type == SDL_QUIT ||
                    (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
                    running = false;
            if (!running)
                break;

            session.advance_frame(read_buttons());
            if (audio) {
                const auto samples = session.take_audio_samples();
                audio->append(samples);
            } else {
                (void)session.take_audio_samples();
            }

            const auto pixels = session.native_pixels();
            if (SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), game_width * sizeof(std::uint32_t)) != 0)
                throw std::runtime_error(std::string("Framebuffer upload failed: ") + SDL_GetError());
            SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
            SDL_RenderClear(renderer.get());
            SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
            SDL_RenderPresent(renderer.get());
            std::this_thread::sleep_until(frame_start + std::chrono::duration_cast<
                std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / 60.0)));

            const auto now = std::chrono::steady_clock::now();
            if (now - last_save >= std::chrono::seconds(10)) {
                write_save(session);
                last_save = now;
            }
        }

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
