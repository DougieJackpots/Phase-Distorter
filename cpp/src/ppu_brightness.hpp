#pragma once
#include <array>
#include <cstdint>
namespace eb::detail {
// Exact integer SNES brightness scaling, then five-to-eight-bit expansion.
// 512 bytes, independent of CGRAM/VRAM and valid for every raster row.
inline std::uint8_t brightness_component(unsigned brightness, unsigned value) {
    static constexpr auto table = [] {
        std::array<std::array<std::uint8_t, 32>, 16> result{};
        for (unsigned b = 0; b < 16; ++b)
            for (unsigned v = 0; v < 32; ++v) {
                const auto scaled = (v * b + 7) / 15;
                result[b][v] = std::uint8_t((scaled << 3) | (scaled >> 2));
            }
        return result;
    }();
    return table[brightness][value];
}
} // namespace eb::detail
