#include "eb/native_hook_sites.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
namespace baseline {
// Golden entry addresses from revision 6, independent of shared production metadata.
constexpr std::array<unsigned,16> fx{0xc4283f,0xc42884,0xc428d1,0xc428fc,0xc42965,0xc429ae,0xc4c8e9,0xc4c8a4,
 0xc4277d,0xc427c2,0xc4280f,0xc4283a,0xc428a3,0xc428ec,0xc49bb9,0xc49b74};
constexpr std::array<unsigned,4> draw{0xc0a3a4,0xc0a0fa,0xc0a383,0xc0a0d9};
constexpr std::array<unsigned,2> surface{0xc0c7db,0xc0c7bd},depth{0xc0db82,0xc0db4a};
constexpr std::array<unsigned,4> strip{0xc00f04,0xc010bc,0xc00f16,0xc010ce};
unsigned normalize(unsigned pc){unsigned bank=pc>>16;if(bank!=0x7e&&bank!=0x7f&&((bank&0x40)||(pc&0x8000)))pc|=0xc00000;return pc;}
template<std::size_t N>bool contains(const std::array<unsigned,N>& values,unsigned pc){return std::find(values.begin(),values.end(),pc)!=values.end();}
}
namespace eb {
struct MainCpu65816{std::uint32_t program_counter;unsigned disposition;};
struct Renderer{};class SnesBus;
struct Runtime{bool try_execute(MainCpu65816&,SnesBus&);};
struct Effects{bool try_execute(MainCpu65816&,SnesBus&,Runtime&);};
class SnesBus {
public:
    std::array<unsigned,16> events{};unsigned used=0;bool jp=false,guarded=false;
    Runtime runtime;Effects effects;Runtime* native_sprite_runtime_=&runtime;Effects* native_sprite_effects_=&effects;Renderer scene_renderer_;
    void record(unsigned event){if(used==events.size())throw std::runtime_error("Trace overflow");events[used++]=event;}
    bool try_execute_native_sprite_operation(MainCpu65816&);
};
bool Effects::try_execute(MainCpu65816& cpu,SnesBus& bus,Runtime&){
    const auto pc=baseline::normalize(cpu.program_counter);
    for(unsigned i=0;i<8;++i)if(pc==baseline::fx[(bus.jp?8:0)+i]){
        bus.record(10+i);return i!=7&&(cpu.disposition&1);
    }
    return false;
}
bool Runtime::try_execute(MainCpu65816& cpu,SnesBus& bus){
    bus.record(1); // Original region validation remains in every runtime call.
    const auto pc=baseline::normalize(cpu.program_counter);
    const auto surface=[&]{if(pc!=baseline::surface[bus.jp])return false;bus.record(20);return bool(cpu.disposition&2);};
    const auto depth=[&]{if(pc!=baseline::depth[bus.jp])return false;bus.record(21);return bool(cpu.disposition&2);};
    const auto strip=[&]{if(pc!=baseline::strip[bus.jp?2:0]&&pc!=baseline::strip[bus.jp?3:1])return false;bus.record(22);return bool(cpu.disposition&2);};
    if(bus.guarded){if(native_hook_sites::dispatch_services(pc,surface,depth,strip))return true;}
    else if(surface()||depth()||strip())return true;
    bus.record(2); // Transaction cleanup cannot be skipped on unrelated PCs.
    if(pc==0xc01e49){bus.record(30);if(cpu.disposition&4)return true;
        // Ensure the bus tests the PC current at draw entry, not a stale PC.
        cpu.program_counter=baseline::draw[bus.jp?2:0];}
    return false;
}
bool try_native_sprite_draw(MainCpu65816& cpu,SnesBus& bus,Runtime&,Renderer&){
    const auto pc=baseline::normalize(cpu.program_counter);
    if(pc!=baseline::draw[bus.jp?2:0]&&pc!=baseline::draw[bus.jp?3:1])return false;
    bus.record(40);return bool(cpu.disposition&4);
}
#include "native_hook_dispatch_under_test.inc"
bool reference_dispatch(MainCpu65816& cpu,SnesBus& bus){
    if(!bus.native_sprite_runtime_)return false;
    if(bus.native_sprite_effects_->try_execute(cpu,bus,*bus.native_sprite_runtime_))return true;
    if(bus.native_sprite_runtime_->try_execute(cpu,bus))return true;
    return try_native_sprite_draw(cpu,bus,*bus.native_sprite_runtime_,bus.scene_renderer_);
}
}
int main(){
    using namespace eb;SnesBus original,candidate;candidate.guarded=true;
    auto compare=[&](unsigned pc,unsigned disposition,bool jp,bool enabled){
        original.used=candidate.used=0;original.jp=candidate.jp=jp;
        original.native_sprite_runtime_=enabled?&original.runtime:nullptr;
        candidate.native_sprite_runtime_=enabled?&candidate.runtime:nullptr;
        MainCpu65816 a{pc,disposition},b=a;
        const bool ra=reference_dispatch(a,original),rb=candidate.try_execute_native_sprite_operation(b);
        if(ra!=rb||a.program_counter!=b.program_counter||original.used!=candidate.used||
            !std::equal(original.events.begin(),original.events.begin()+original.used,candidate.events.begin()))
            throw std::runtime_error("Production native hook dispatch changes handler order/state");
    };
    for(unsigned pc=0;pc<0x1000000;++pc){
        const auto normalized=baseline::normalize(pc),n=native_hook_sites::normalize(pc);
        if(n!=normalized||native_hook_sites::may_effect(n)!=baseline::contains(baseline::fx,normalized)||
            native_hook_sites::may_draw(n)!=baseline::contains(baseline::draw,normalized)||
            native_hook_sites::may_surface(n)!=baseline::contains(baseline::surface,normalized)||
            native_hook_sites::may_depth(n)!=baseline::contains(baseline::depth,normalized)||
            native_hook_sites::may_strip(n)!=baseline::contains(baseline::strip,normalized))
            throw std::runtime_error("Native hook admission differs from revision 6 entry mapping");
        compare(pc,(pc>>3)&7,pc&1,pc%2048!=0);
    }
    // Every admitted entry/mirror, both regions, all handler return masks.
    for(unsigned bank=0;bank<256;++bank)for(bool jp:{false,true})for(unsigned mask=0;mask<8;++mask){
        for(auto pc:baseline::fx)compare((bank<<16)|(pc&65535),mask,jp,true);
        for(auto pc:baseline::draw)compare((bank<<16)|(pc&65535),mask,jp,true);
        for(auto pc:baseline::surface)compare((bank<<16)|(pc&65535),mask,jp,true);
        for(auto pc:baseline::depth)compare((bank<<16)|(pc&65535),mask,jp,true);
        for(auto pc:baseline::strip)compare((bank<<16)|(pc&65535),mask,jp,true);
        compare((bank<<16)|0x1e49,mask,jp,true);
    }
    std::cout<<"All 16777216 PCs and hook mirrors preserve admission, dispatch order, false-return effects and runtime cleanup\n";
}
