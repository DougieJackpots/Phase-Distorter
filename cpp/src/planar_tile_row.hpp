#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace eb::detail {
// Eight decoded indices, leftmost pixel in the low byte. SNES bitplanes are
// paired sixteen bytes apart; each physical byte address wraps independently.
inline std::uint64_t decode_planar_row(std::span<const std::uint8_t, 65536> vram,
                                       unsigned address, unsigned depth) {
    static constexpr auto lanes = [] {
        std::array<std::uint64_t, 256> table{};
        for (unsigned byte = 0; byte < 256; ++byte)
            for (unsigned x = 0; x < 8; ++x)
                table[byte] |= std::uint64_t((byte >> (7 - x)) & 1) << (x * 8);
        return table;
    }();
    std::uint64_t decoded = 0;
    for (unsigned plane = 0; plane < depth; ++plane)
        decoded |= lanes[vram[std::uint16_t(address + (plane / 2) * 16 + (plane & 1))]] << plane;
    return decoded;
}
// Two 32-bit words avoid variable 64-bit shifts in the ARMv7 pixel loop.
struct PlanarRowHalves { std::uint32_t low=0,high=0; };
inline PlanarRowHalves decode_planar_row_halves(std::span<const std::uint8_t,65536> vram,
                                               unsigned address,unsigned depth) {
    static constexpr auto lanes=[] {
        std::array<PlanarRowHalves,256> result{};
        for(unsigned byte=0;byte<256;++byte)
            for(unsigned x=0;x<4;++x) {
                result[byte].low |= ((byte>>(7-x))&1u)<<(x*8);
                result[byte].high |= ((byte>>(3-x))&1u)<<(x*8);
            }
        return result;
    }();
    PlanarRowHalves result;
    for(unsigned plane=0;plane<depth;++plane) {
        const auto bits=lanes[vram[std::uint16_t(address+(plane/2)*16+(plane&1))]];
        result.low |= bits.low<<plane;
        result.high |= bits.high<<plane;
    }
    return result;
}
// Instantiate per sprite/scanline. It cannot survive a VRAM write or a new
// borrowed raster view; no global invalidation or retained memory pointer.
struct PlanarTileRow {
    unsigned address = ~0u, depth = 0;
    std::uint64_t pixels = 0;
    unsigned sample(std::span<const std::uint8_t, 65536> vram, unsigned row_address,
                    unsigned bitdepth, unsigned lane) {
        if (address != row_address || depth != bitdepth) {
            pixels = decode_planar_row(vram, row_address, bitdepth);
            address = row_address;
            depth = bitdepth;
        }
        return unsigned(pixels >> (lane * 8)) & 255;
    }
};
} // namespace eb::detail
