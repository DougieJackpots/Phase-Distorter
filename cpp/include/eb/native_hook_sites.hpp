#pragma once
#include <array>
#include <cstdint>
namespace eb::native_hook_sites {
// Shared with the original handlers so admission checks cannot acquire a
// separate copy of their regional entry addresses. Inputs to may_* are
// already normalized by the caller using the original hook mapping.
// Effect entries: seed-eight, seed-four, row, column, pixel, upload, clear, reset.
inline constexpr std::array<std::uint32_t,8> effects_us{0xc4283f,0xc42884,0xc428d1,0xc428fc,0xc42965,0xc429ae,0xc4c8e9,0xc4c8a4};
inline constexpr std::array<std::uint32_t,8> effects_jp{0xc4277d,0xc427c2,0xc4280f,0xc4283a,0xc428a3,0xc428ec,0xc49bb9,0xc49b74};
inline constexpr unsigned draw_us=0xc0a3a4,draw_jp=0xc0a383,custom_us=0xc0a0fa,custom_jp=0xc0a0d9;
inline constexpr unsigned surface_us=0xc0c7db,surface_jp=0xc0c7bd;
inline constexpr unsigned depth_us=0xc0db82,depth_jp=0xc0db4a;
inline constexpr unsigned row_us=0xc00f04,row_jp=0xc00f16,column_us=0xc010bc,column_jp=0xc010ce;
constexpr std::uint32_t normalize(std::uint32_t pc) {
    const unsigned bank=pc>>16;
    return bank!=0x7e&&bank!=0x7f&&((bank&0x40)||(pc&0x8000))?pc|0xc00000:pc;
}
constexpr bool may_effect(std::uint32_t pc) {
    switch(pc) {
    case effects_us[0]:case effects_us[1]:case effects_us[2]:case effects_us[3]:
    case effects_us[4]:case effects_us[5]:case effects_us[6]:case effects_us[7]:
    case effects_jp[0]:case effects_jp[1]:case effects_jp[2]:case effects_jp[3]:
    case effects_jp[4]:case effects_jp[5]:case effects_jp[6]:case effects_jp[7]:return true;
    default:return false;
    }
}
constexpr bool may_draw(std::uint32_t pc) {return pc==draw_us||pc==draw_jp||pc==custom_us||pc==custom_jp;}
constexpr bool may_surface(std::uint32_t pc) {return pc==surface_us||pc==surface_jp;}
constexpr bool may_depth(std::uint32_t pc) {return pc==depth_us||pc==depth_jp;}
constexpr bool may_strip(std::uint32_t pc) {return pc==row_us||pc==row_jp||pc==column_us||pc==column_jp;}
// Preserve the original short-circuit order and each service's own validation.
template<class Surface,class Depth,class Strip>
bool dispatch_services(std::uint32_t pc,Surface&& surface,Depth&& depth,Strip&& strip) {
    return (may_surface(pc)&&surface())||(may_depth(pc)&&depth())||(may_strip(pc)&&strip());
}
}
