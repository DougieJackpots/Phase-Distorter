// Synthetic wrapper benchmark: tiny SPC-sized clock slices, unmodified DSP work.
#include "eb/snes_audio_dsp.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <vector>
int main(){
    std::array<double,7> times{};std::uint64_t checksum=0;
    for(unsigned active=0;active<2;++active){
    for(auto& ms:times){
        std::array<std::uint8_t,65536> ram{};eb::SnesAudioDsp dsp(ram);
        if(active){
            for(unsigned v=0;v<8;++v){ram[0x200+v*4+1]=3;ram[0x200+v*4+3]=3;}
            constexpr std::array<std::uint8_t,9> wave{0xb3,0x01,0x35,0x67,0x65,0x31,0xfe,0xcb,0xae};
            std::copy(wave.begin(),wave.end(),ram.begin()+0x300);
            for(unsigned r=0;r<128;++r)dsp.write_register(r,0);
            for(unsigned v=0;v<8;++v){dsp.write_register(v*16,0x70);dsp.write_register(v*16+1,0x60);
                dsp.write_register(v*16+3,0x10);dsp.write_register(v*16+4,v);dsp.write_register(v*16+7,0x7f);}
            dsp.write_register(0x5d,2);dsp.write_register(0x0c,0x70);dsp.write_register(0x1c,0x70);
            dsp.write_register(0x6d,0x40);dsp.write_register(0x7d,1);dsp.write_register(0x4d,0xff);
            dsp.write_register(0x2c,0x20);dsp.write_register(0x3c,0x20);dsp.write_register(0x0d,0x30);
            dsp.write_register(0x0f,0x7f);dsp.write_register(0x4c,0xff);
        }
        std::vector<std::int16_t> samples;samples.reserve(2048);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<250;++frame){
            unsigned remaining=17067;
            for(unsigned instruction=0;remaining;++instruction){
                const unsigned count=std::min(remaining,2+(instruction%7));
                dsp.advance_audio_clocks(count);remaining-=count;
            }
            dsp.take_stereo_samples(samples);checksum+=samples.size();
        }
        ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/250;
    }
    std::sort(times.begin(),times.end());std::cout<<"DSP active="<<active<<" tiny-slice median_ms_per_frame="<<times[times.size()/2]
        <<" checksum="<<checksum<<'\n';
    }
}
