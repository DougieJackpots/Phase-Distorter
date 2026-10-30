#include "eb/core_fastpaths.hpp"
#include "eb/threed_npc_restoration.hpp"
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace eb {
struct FetchModel {
    std::vector<std::uint8_t> cartridge_rom_;
    std::array<std::uint8_t,32> cpu_io_registers_{};
    std::function<std::uint8_t(unsigned,std::uint8_t)> debug_read_rom;
    std::function<void(bool,std::uint32_t,std::uint8_t)> observe_bus_access;
    bool restore_threed_npcs_=false;
    bool fast_memory_access_=true;
    GameVersion game_version_=GameVersion::US;
    std::uint8_t open_bus_=0;
    bool try_fetch_rom_instruction(std::uint32_t address,unsigned length,unsigned& extra);
};
#include "rom_fetch_under_test.inc"
}
std::size_t reference_offset(std::uint32_t address,std::size_t original_size){
    std::size_t index=address&0x3fffff,size=original_size,base=0,mask=0x200000;
    while(index>=size&&mask){if(index&mask){index-=mask;if(size>mask){size-=mask;base+=mask;}}mask>>=1;}
    return(base+index)%original_size;
}
unsigned access_clocks(std::uint32_t address,bool fast){
    unsigned bank=address>>16,offset=address&65535;
    if(bank==0x7e||bank==0x7f)return 8;
    if(bank&0x40)return (bank&0x80)&&fast?6:8;
    if(offset<0x2000)return 8;
    if(offset<0x4000)return 6;
    if(offset<0x4200)return 12;
    if(offset<0x6000)return 6;
    if(offset<0x8000)return 8;
    return (bank&0x80)&&fast?6:8;
}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    std::uint64_t offsets=0,fetches=0,accepted=0;
    for(unsigned size:{1u,0x8000u,0x10000u,0x18000u,0x100000u,0x280000u,0x300000u,0x400000u})
        for(unsigned address=0;address<0x400000;++address){
            require(eb::detail::rom_offset(address,size)==reference_offset(address,size),"ROM mirror mapping changed");++offsets;
        }
    eb::FetchModel bus;bus.cartridge_rom_.resize(0x300000);
    for(unsigned i=0;i<bus.cartridge_rom_.size();++i)bus.cartridge_rom_[i]=std::uint8_t((i*17+5)^(i>>8));
    const auto check=[&](unsigned address,unsigned length,bool fast,bool restore){
        bus.cpu_io_registers_[13]=fast;bus.restore_threed_npcs_=restore;bus.open_bus_=0x9a;
        unsigned extra=0xdead;
        const bool ok=bus.try_fetch_rom_instruction(address,length,extra);++fetches;
        const auto plan=eb::detail::rom_fetch_plan(address,length,bus.cartridge_rom_.size(),fast);
        require(ok==plan.accepted,"Production fetch admission differs from helper");
        if(!ok){require(bus.open_bus_==0x9a&&extra==0xdead,"Rejected fetch mutated bus state");return;}
        ++accepted;unsigned clocks=0;std::uint8_t expected=0x9a;
        for(unsigned i=0;i<length;++i){
            const auto at=(address&0xff0000)|std::uint16_t(address+i);
            const auto bank=at>>16,offset=at&65535;
            require(bank!=0x7e&&bank!=0x7f&&((bank&0x40)||offset>=0x8000),"Fetch elided a non-ROM bus operation");
            clocks+=access_clocks(at,fast)-6;
            const auto mapped=reference_offset(at,bus.cartridge_rom_.size());expected=bus.cartridge_rom_[mapped];
            if(restore)expected=eb::restored_threed_npc_byte(eb::threed_npc_table_offset(bus.game_version_),mapped,expected);
        }
        require(bus.open_bus_==expected&&extra==clocks,"Fetch open bus or wait clocks differ");
    };
    for(unsigned bank=0;bank<256;++bank)
        for(unsigned offset:{0u,1u,0x1ffdu,0x1fffu,0x2000u,0x5fffu,0x6000u,0x7ffdu,0x7fffu,0x8000u,0xfffbu,0xfffcu,0xfffdu,0xfffeu,0xffffu})
            for(unsigned length=0;length<6;++length)for(bool fast:{false,true})for(bool restore:{false,true})
                check((bank<<16)|offset,length,fast,restore);
    std::uint32_t seed=42;
    for(unsigned i=0;i<1000000;++i){seed=seed*1664525+1013904223;check(seed&0xffffff,1+(seed>>28)%4,seed&1,seed&2);}
    for(unsigned at=eb::threed_npc_table_offset(eb::GameVersion::US);at<eb::threed_npc_table_offset(eb::GameVersion::US)+128;++at)
        check(0xc00000|at,4,false,true);
    bus.game_version_=eb::GameVersion::JP;
    for(unsigned at=eb::threed_npc_table_offset(eb::GameVersion::JP);at<eb::threed_npc_table_offset(eb::GameVersion::JP)+128;++at)
        check(0xc00000|at,4,true,true);
    bus.fast_memory_access_=false;unsigned declined_extra=7;
    require(!bus.try_fetch_rom_instruction(0xc00000,3,declined_extra)&&declined_extra==7,"Disabled fetch path still executed");
    bus.fast_memory_access_=true;
    bus.debug_read_rom=[](unsigned,std::uint8_t v){return v;};
    unsigned extra=17;bus.open_bus_=33;
    require(!bus.try_fetch_rom_instruction(0xc00000,3,extra)&&extra==17&&bus.open_bus_==33,"Debug hook was bypassed");
    bus.debug_read_rom={};bus.observe_bus_access=[](bool,std::uint32_t,std::uint8_t){};
#ifdef EB_GAMEPLAY_AUDIT
    require(!bus.try_fetch_rom_instruction(0xc00000,3,extra),"Bus audit was bypassed");
#endif
    std::cout<<offsets<<" ROM mappings and "<<fetches<<" production fetch cases match scalar mapping/bus clocks; accepted="<<accepted<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
