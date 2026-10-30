#pragma once
#include <cstddef>
#include <cstdint>
namespace eb::detail {
// Preserve the existing non-power-of-two address-line folding for mirrors.
// Ordinary in-image addresses need neither folding nor runtime division.
inline std::size_t rom_offset(std::uint32_t address, std::size_t original_size,bool direct=true) {
    std::size_t index=address&0x3fffff;
    if(direct&&index<original_size)return index;
    std::size_t size=original_size,base=0,mask=0x200000;
    while(index>=size&&mask){
        if(index&mask){index-=mask;if(size>mask){size-=mask;base+=mask;}}
        mask>>=1;
    }
    return (base+index)%original_size;
}
struct RomFetchPlan {std::size_t last_offset=0;unsigned extra_master_clocks=0;bool accepted=false;};
inline RomFetchPlan rom_fetch_plan(std::uint32_t address,unsigned length,std::size_t size,bool fast_rom) {
    address&=0xffffff;
    const auto bank=address>>16,offset=address&65535,index=address&0x3fffff;
    if(!length||length>4||bank==0x7e||bank==0x7f||(!(bank&0x40)&&offset<0x8000)||
       length>0x10000-offset||index>=size||length>size-index)return {};
    return {index+length-1,length*((bank&0x80)&&fast_rom?0u:2u),true};
}
// Derived host cache only: never serialized and never merges CPU/APU slices.
class HardwareEventCache {
public:
    bool admits(std::uint64_t master,std::uint64_t frame,unsigned clocks) const {
        return clocks && frame==frame_ && master<deadline_ && clocks<deadline_-master;
    }
    void reset(std::uint64_t master,std::uint64_t frame,unsigned distance) {
        frame_=frame;
        deadline_=distance&&master<=~std::uint64_t(0)-distance?master+distance:0;
    }
    void invalidate(){deadline_=0;}
private:
    std::uint64_t deadline_=0,frame_=0;
};
} // namespace eb::detail
