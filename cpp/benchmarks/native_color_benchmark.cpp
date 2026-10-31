#include "native_ppu_color.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <vector>
int main(){
    std::vector<std::array<unsigned,2>> pairs(131072);
    unsigned seed=12345;auto random=[&]{seed=seed*1664525+1013904223;return(seed>>8)&32767;};
    for(auto& pair:pairs)pair={random(),random()};
    std::array<std::uint64_t,2> checksums{};
    for(unsigned operation=0;operation<4;++operation){
        const auto measure=[&](auto mix,unsigned index){
            std::array<double,7> times{};std::uint64_t checksum=0;
            for(auto& ns:times){
                const auto start=std::chrono::steady_clock::now();
                for(unsigned repeat=0;repeat<16;++repeat)
                    for(const auto& pair:pairs)checksum+=mix(pair[0],pair[1],operation);
                ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/(16*pairs.size());
            }
            std::sort(times.begin(),times.end());checksums[index]=checksum;return times[times.size()/2];
        };
        const auto scalar=[](unsigned a,unsigned b,unsigned op){return eb::detail::native_math_channel(a&31,b&31,op)|
            (eb::detail::native_math_channel((a>>5)&31,(b>>5)&31,op)<<5)|
            (eb::detail::native_math_channel((a>>10)&31,(b>>10)&31,op)<<10);};
        const double old_ns=measure(scalar,0),packed_ns=measure(eb::detail::native_mix_color,1);
        if(checksums[0]!=checksums[1])return 1;
        std::cout<<"operation="<<operation<<" channel_table_ns="<<old_ns<<" packed_ns="<<packed_ns
            <<" checksum="<<checksums[0]<<'\n';
    }
}
