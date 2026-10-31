#pragma once
#include "ppu_brightness.hpp"
#include "eb/scene_read_view.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
namespace eb::detail {
struct NativeColorPixel {
    std::uint16_t color=0;
    std::int8_t priority=-1;
    std::uint8_t layer=5;
    bool math=true;
};
inline NativeColorPixel native_color_pixel(const PpuPixel& pixel) {
    return {pixel.color,std::int8_t(pixel.priority),std::uint8_t(pixel.layer),pixel.math};
}
inline bool window_policy(unsigned setting,bool inside) {
    return setting==3||(setting==1&&!inside)||(setting==2&&inside);
}
// All six window layers share the same two horizontal intervals. Compute the
// layer logic once for each of their four possible membership combinations.
inline std::array<std::uint8_t,4> native_window_regions(std::span<const std::uint8_t,64> regs) {
    std::array<std::uint8_t,4> regions{};
    for(unsigned region=0;region<4;++region) {
        for(unsigned layer=0;layer<6;++layer) {
            const auto selection=(regs[0x23+layer/2]>>((layer&1)*4))&15;
            const bool enabled1=selection&2,enabled2=selection&8;
            bool a=region&1,b=region&2;
            if(selection&1)a=!a;
            if(selection&4)b=!b;
            bool inside=false;
            if(!enabled1)inside=enabled2&&b;
            else if(!enabled2)inside=a;
            else switch(layer<4?(regs[0x2a]>>(layer*2))&3:(regs[0x2b]>>((layer-4)*2))&3) {
                case 0:inside=a||b;break;
                case 1:inside=a&&b;break;
                case 2:inside=a!=b;break;
                default:inside=a==b;break;
            }
            if(inside)regions[region]|=std::uint8_t(1u<<layer);
        }
    }
    return regions;
}
// Four 32x32 byte tables replace repeated per-channel saturation/half branches.
// Halving precedes upper saturation, exactly as in the canonical compositor.
inline unsigned native_math_channel(unsigned a,unsigned b,unsigned operation) {
    static constexpr auto table=[] {
        std::array<std::array<std::array<std::uint8_t,32>,32>,4> result{};
        for(unsigned op=0;op<4;++op)
            for(unsigned x=0;x<32;++x)
                for(unsigned y=0;y<32;++y) {
                    unsigned value=(op&1)?(x>y?x-y:0):x+y;
                    if(op&2)value/=2;
                    result[op][x][y]=std::uint8_t(value>31?31:value);
                }
        return result;
    }();
    return table[operation][a][b];
}
// Six-bit lanes leave a carry/borrow guard between each five-bit channel.
// Keep the arithmetic in 32-bit registers on ARMv7; halving still happens
// before upper saturation, as in the scalar channel operation.
inline unsigned native_mix_color(unsigned a,unsigned b,unsigned operation) {
    constexpr unsigned guards=(1u<<5)|(1u<<11)|(1u<<17);
    constexpr unsigned channels=31u|(31u<<6)|(31u<<12);
    const auto spread=[](unsigned c) {return(c&31)|((c&0x3e0)<<1)|((c&0x7c00)<<2);};
    unsigned value;
    if(operation&1) {
        value=(spread(a)|guards)-spread(b);
        const unsigned valid=value&guards;
        value &= valid-(valid>>5);
        if(operation&2)value>>=1;
    } else {
        value=spread(a)+spread(b);
        if(operation&2)value>>=1;
        else {
            const unsigned overflow=value&guards;
            value |= overflow-(overflow>>5);
        }
    }
    value &= channels;
    return (value&31)|((value>>1)&0x3e0)|((value>>2)&0x7c00);
}
inline std::uint32_t finish_native_color(const NativeColorPixel& main,const NativeColorPixel& sub,
                                        unsigned cgwsel,unsigned cgadsub,unsigned fixed,
                                        unsigned brightness,bool inside) {
    const bool clipped=window_policy(cgwsel>>6,inside);
    unsigned color=clipped?0:main.color;
    if(!window_policy((cgwsel>>4)&3,inside)&&main.math&&(cgadsub&(1u<<main.layer))) {
        const unsigned other=(cgwsel&2)?sub.color:fixed;
        const bool half=(cgadsub&0x40)&&!clipped&&(!(cgwsel&2)||sub.priority>=0);
        const unsigned operation=unsigned(bool(cgadsub&0x80))|(unsigned(half)<<1);
        color=native_mix_color(color,other,operation);
    }
    return 0xff000000 | (unsigned(brightness_component(brightness,color&31))<<16) |
        (unsigned(brightness_component(brightness,(color>>5)&31))<<8) |
         unsigned(brightness_component(brightness,(color>>10)&31));
}
} // namespace eb::detail
