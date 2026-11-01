#pragma once
#include <array>
#include <cstdint>
namespace eb::detail {
// Pure fixed-colour/brightness tables, owned by one renderer. No RAM, palette,
// raster view or hardware pointers are retained. Safe to copy with the owner;
// its key describes every mathematical input, including both window operations.
struct NativeFixedColorCache {
    std::uint32_t key=~0u;
    std::array<std::array<std::array<std::uint8_t,32>,3>,2> channels{};
};
}
