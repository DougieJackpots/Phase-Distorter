#include "spc_timer_step.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
int main(){
    unsigned seed=12345;auto random=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    for(unsigned trial=0;trial<262144;++trial){
        std::array<unsigned,3> dividers{};
        std::array<std::uint8_t,3> counters{},targets{},outputs{};
        for(unsigned t=0;t<3;++t){dividers[t]=random()%(t==2?16:128);counters[t]=random();targets[t]=random();outputs[t]=random()&15;}
        auto expected_dividers=dividers;auto expected_counters=counters;auto expected_outputs=outputs;
        const auto control=std::uint8_t(random()),test=std::uint8_t(random());
        const unsigned elapsed=trial%2?random()%13:random()%513;
        // Independent model: determine elapsed prescaler ticks by quotient,
        // then apply the 8-bit target counter and four-bit output latch.
        for(unsigned t=0;t<3;++t){
            const auto period=t==2?16u:128u;
            unsigned ticks=(expected_dividers[t]+elapsed)/period;
            expected_dividers[t]=(expected_dividers[t]+elapsed)%period;
            if((control&(1u<<t))&&(test&8)&&!(test&1))
                while(ticks--){expected_counters[t]=std::uint8_t(expected_counters[t]+1);
                    if(expected_counters[t]==targets[t]){expected_counters[t]=0;expected_outputs[t]=(expected_outputs[t]+1)&15;}}
        }
        eb::detail::advance_spc_timers(elapsed,control,test,dividers,counters,targets,outputs);
        if(dividers!=expected_dividers||counters!=expected_counters||outputs!=expected_outputs)
            throw std::runtime_error("SPC timer fast path differs from quotient/tick model");
    }
    std::cout<<"262144 SPC timer states match independent prescaler/target model\n";
}
