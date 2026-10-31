#include "spc_timer_step.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
struct State{
    std::array<unsigned,3> dividers{};
    std::array<std::uint8_t,3> counters{},targets{7,0,13},outputs{};
};
void original(unsigned elapsed,State& state){
    for(unsigned timer=0;timer<3;++timer){
        const unsigned period=timer==2?16:128;state.dividers[timer]+=elapsed;
        while(state.dividers[timer]>=period){state.dividers[timer]-=period;
            if(++state.counters[timer]==state.targets[timer]){
                state.counters[timer]=0;state.outputs[timer]=(state.outputs[timer]+1)&15;}}
    }
}
int main(){
    std::array<std::uint64_t,2> sums{};
    const auto measure=[&](auto step,unsigned index){
        std::array<double,7> timings{};
        for(auto& ns:timings){State state;
            const auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<4000000;++i)step(2+i%7,state);
            ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/4000000;
            for(unsigned t=0;t<3;++t)sums[index]+=state.dividers[t]+state.counters[t]+state.outputs[t];
        }
        std::sort(timings.begin(),timings.end());return timings[timings.size()/2];
    };
    const auto baseline=measure(original,0);
    const auto fast=measure([](unsigned elapsed,State& s){eb::detail::advance_spc_timers(elapsed,7,8,s.dividers,s.counters,s.targets,s.outputs);},1);
    if(sums[0]!=sums[1])return 1;
    std::cout<<"SPC timers old_ns_per_slice="<<baseline<<" new_ns_per_slice="<<fast<<" checksum="<<sums[0]<<'\n';
}
