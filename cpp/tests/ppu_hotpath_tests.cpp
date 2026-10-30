#include "planar_tile_row.hpp"
#include "ppu_brightness.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
void require(bool value) { if (!value) throw std::runtime_error("PPU scalar comparison failed"); }
int main() {
    try {
        // Exhaust every 15-bit colour at all brightness levels against the
        // previous arithmetic, including its rounding and RGB channel order.
        for (unsigned b = 0; b < 16; ++b)
            for (unsigned color = 0; color < 32768; ++color) {
                unsigned scalar = 0xff000000, table = scalar;
                for (unsigned channel = 0; channel < 3; ++channel) {
                    const auto v = (color >> (5 * channel)) & 31;
                    const auto scaled = (v * b + 7) / 15;
                    const auto shift = (2 - channel) * 8;
                    scalar |= ((scaled << 3) | (scaled >> 2)) << shift;
                    table |= unsigned(eb::detail::brightness_component(b, v)) << shift;
                }
                require(scalar == table);
            }
        std::array<std::uint8_t, 65536> vram{};
        std::uint32_t seed = 42;
        const auto random = [&] { seed = seed * 1664525 + 1013904223; return seed; };
        for (unsigned raster = 0; raster < 4000; ++raster) {
            // Change VRAM between cache lifetimes, including wrapped rows.
            for (auto& byte : vram) byte = std::uint8_t(random() >> 24);
            eb::detail::PlanarTileRow row;
            const unsigned base = raster % 2 ? 65530 : random() & 65535;
            for (unsigned depth : {2u, 4u, 8u})
                for (unsigned reverse = 0; reverse < 2; ++reverse)
                    for (unsigned offset : {0u, 32u, 0u})
                        // Start partway through a tile to model clipped sprites.
                        for (unsigned col = raster % 8; col < 8; ++col) {
                            const auto lane = reverse ? 7 - col : col;
                            unsigned scalar = 0;
                            for (unsigned plane = 0; plane < depth; ++plane)
                                scalar |= ((vram[std::uint16_t(base + offset + (plane / 2) * 16 + (plane & 1))]
                                            >> (7 - lane)) & 1) << plane;
                            require(row.sample(vram, base + offset, depth, lane) == scalar);
                        }
        }
        std::cout << "All 524288 brightness colours and clipped/flipped/wrapped planar-row fixtures match scalar output\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
