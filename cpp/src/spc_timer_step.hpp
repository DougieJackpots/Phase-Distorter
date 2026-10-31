#pragma once
#include <array>
#include <cstdint>
namespace eb::detail {
inline void advance_spc_timers(unsigned elapsed,std::uint8_t control,std::uint8_t test,
    std::array<unsigned,3>& dividers,std::array<std::uint8_t,3>& counters,
    const std::array<std::uint8_t,3>& targets,std::array<std::uint8_t,3>& outputs) {
    dividers[0]+=elapsed;dividers[1]+=elapsed;dividers[2]+=elapsed;
    // Most instruction slices do not cross a timer boundary. Preserve every
    // divider increment but avoid three independent prescaler loops then.
    if((dividers[0]|dividers[1])<128&&dividers[2]<16)return;
    for(unsigned timer=0;timer<3;++timer) {
        const unsigned period=timer==2?16:128;
        while(dividers[timer]>=period) {
            dividers[timer]-=period;
            if((control&(1u<<timer))&&(test&8)&&!(test&1)) {
                if(++counters[timer]==targets[timer]) {
                    counters[timer]=0;outputs[timer]=(outputs[timer]+1)&15;
                }
            }
        }
    }
}
} // namespace eb::detail
