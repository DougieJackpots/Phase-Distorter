// Synthetic renderer benchmark only: excludes game logic, audio and display.
#include "eb/game_scene_renderer.hpp"
#include "generated_profile.hpp"
#ifndef EB_PPU_FAST_STANDALONE
#include "eb/snes_bus.hpp"
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <string_view>
struct Fixture {
    std::array<std::uint8_t,0x20000> ram{};
    std::array<std::uint8_t,65536> vram{};
    std::array<std::uint8_t,512> palette{};
    std::array<std::uint8_t,544> oam{};
    std::array<std::uint8_t,64> regs{};
    std::array<std::uint16_t,4> sx{},sy{};
    std::array<std::int16_t,6> affine{256,0,0,256,0,0};
    std::array<std::int16_t,2> offsets{};
    std::array<std::uint32_t,256*224> native{};
    eb::SourceProfile profile{};
    std::array<eb::PpuPixel,256> objects{};
    eb::SceneReadView view(){return {ram,vram,palette,oam,{},regs,sx,sy,affine,offsets,native,profile,eb::GameVersion::US,0,0,0};}
};
int main(){
#ifndef EB_PPU_FAST_STANDALONE
    if(!(eb::SnesBus::compiled_core_fast_paths()&4)) {
        std::cerr<<"Build with EB_FAST_NATIVE_COMPOSITION=ON for this benchmark.\n";
        return 1;
    }
#endif
    Fixture f;
    std::uint32_t seed=12345;
    const auto random=[&]{seed=1664525*seed+1013904223;return seed>>24;};
    for(auto& b:f.vram)b=random();
    for(auto& b:f.palette)b=random();
    for(unsigned x=0;x<256;++x)f.objects[x]={std::uint16_t((x*137)&32767),x%7? -1:10,4,true,128};
    const std::array<std::string_view,7> names={"mode1-plain","mode1-math","mode3-static","mode1-window","mode7","mode1-mosaic","mode1-fixed"};
    std::uint64_t checksum=0;
    for(unsigned scenario=0;scenario<names.size();++scenario){
        f.regs.fill(0);f.regs[0]=15;f.regs[5]=scenario==2?3:scenario==4?7:1;
        f.regs[7]=0x71;f.regs[8]=0x65;f.regs[9]=0x59;f.regs[11]=0x21;f.regs[12]=3;
        f.regs[0x2c]=scenario==2?1:scenario==4?0x11:0x17;
        if(scenario==1||scenario==2||scenario==3){f.regs[0x2d]=2;f.regs[0x30]=2;f.regs[0x31]=0x43;}
        if(scenario==3){f.regs[0x23]=0xaa;f.regs[0x24]=0xaa;f.regs[0x25]=0xaa;f.regs[0x26]=35;f.regs[0x27]=180;f.regs[0x28]=70;f.regs[0x29]=220;f.regs[0x2e]=7;f.regs[0x2f]=2;f.regs[0x30]=0x62;}
        if(scenario==5)f.regs[6]=0x37;
        if(scenario==6)f.regs[0x31]=0x43;
        auto view=f.view();if(scenario==6)view.fixed_color=0x1a7c;
        eb::GameSceneRenderer renderer;renderer.set_fast_native_composition(true);
        std::array<double,5> timings{};
        for(auto& ms:timings){
            const auto start=std::chrono::steady_clock::now();
            for(unsigned frame=0;frame<40;++frame){
                // Raster changes invalidate prior rows, as on the real bus.
                for(unsigned y=0;y<224;++y){
                    f.sx[0]=std::uint16_t(frame*3+y/8);f.sx[1]=std::uint16_t(frame+y*3);
                    eb::BackgroundTileRows rows;view.tile_rows=&rows;
                    std::span<std::uint32_t,256> output(f.native.data()+y*256,256);
                    renderer.compose_native_scanline(view,y,f.objects,output);
                }
                checksum+=f.native[(frame*997)%f.native.size()];
            }
            ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/40;
        }
        std::sort(timings.begin(),timings.end());
        std::cout<<names[scenario]<<" median_ms_per_frame="<<timings[2]<<'\n';
    }
    std::cout<<"checksum="<<checksum<<'\n';
}
