#include "eb/core_fastpaths.hpp"
#include "eb/runtime_timing.hpp"
#include "eb/composition_policy.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>
namespace eb {
struct ClockState {
    std::array<std::uint8_t,64> ppu_registers_{};
    std::array<std::uint8_t,32> cpu_io_registers_{};
    unsigned scanline_master_clock_=0,scanline_index_=0,refresh_clock_=538,autojoy_remaining_clocks_=0;
    std::uint64_t master_clocks_=0,completed_frames=0;
    bool refresh_done_=false,irq_flag_=false,nmi_flag_=false,nmi_pending_=false;
    unsigned joy_result_=0,buttons_=0,sprite_status_=0,oam_address_=0,oam_reload_=0;
    bool operator==(const ClockState&)const=default;
};
struct Event {char kind;std::uint64_t master,frame;unsigned line,clock,value;bool operator==(const Event&)const=default;};
class ClockModel:public ClockState {
public:
    detail::HardwareEventCache hardware_event_cache_;
    bool fast_event_scheduler_=true;
    CompositionPolicy composition_policy_;
    std::vector<Event> events;
    std::function<void(unsigned)> advance_audio_master_clocks;
    std::function<void(std::span<const std::uint32_t>,unsigned,std::uint64_t)> on_presentation_frame;
    ClockModel(){
        events.reserve(256);
        advance_audio_master_clocks=[this](unsigned clocks){event('A',clocks);};
        on_presentation_frame=[this](auto,unsigned,std::uint64_t){event('F',0);};
    }
    void event(char kind,unsigned value){events.push_back({kind,master_clocks_,completed_frames,scanline_index_,scanline_master_clock_,value});}
    void hdma_init(){event('I',0);}
    void hdma_line(){
        event('H',0);
        // Model an HDMA register write at the exact observed event boundary.
        if((scanline_index_%13)==0){ppu_registers_[0x33]^=1;hardware_event_cache_.invalidate();}
    }
    void render_scanline(unsigned y){event('P',y);}
    std::span<const std::uint32_t> presentation_pixels()const{return {};}
    unsigned presentation_width()const{return 256;}
    unsigned next_hardware_event_clocks()const;
    void reference_advance(unsigned clocks,bool pause_for_refresh);
    void fast_advance(unsigned clocks,bool pause_for_refresh);
};
#include "scheduler_reference.inc"
#include "scheduler_under_test.inc"
}
int main(){try{
    std::uint32_t seed=42;
    const auto random=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    unsigned long long checked=0,fast_intervals=0;
    for(unsigned epoch=0;epoch<128;++epoch){
        eb::ClockModel slow,fast;
        eb::ClockState state;
        state.scanline_index_=random()%262;state.scanline_master_clock_=random()%1360;
        state.master_clocks_=random();state.completed_frames=random()%8;
        state.refresh_clock_=538+(random()%8);state.refresh_done_=state.scanline_master_clock_>=state.refresh_clock_;
        state.cpu_io_registers_[0]=std::uint8_t(random());
        state.cpu_io_registers_[7]=std::uint8_t(random());state.cpu_io_registers_[8]=std::uint8_t(random()&1);
        state.cpu_io_registers_[9]=std::uint8_t(random());state.cpu_io_registers_[10]=std::uint8_t(random()&1);
        state.ppu_registers_[0]=std::uint8_t(random());state.ppu_registers_[0x33]=std::uint8_t(random()&1);
        state.buttons_=random()&65535;state.oam_reload_=random()&511;
        state.autojoy_remaining_clocks_=epoch%2?0:random()%4225;
        static_cast<eb::ClockState&>(slow)=state;static_cast<eb::ClockState&>(fast)=state;
        slow.composition_policy_.set_stride(1+epoch%3);fast.composition_policy_.set_stride(1+epoch%3);
        for(unsigned i=0;i<4096;++i){
            if(i%31==0){
                const auto mode=std::uint8_t(random());
                slow.cpu_io_registers_[0]=fast.cpu_io_registers_[0]=mode;
                auto ht=std::uint8_t(random());slow.cpu_io_registers_[7]=fast.cpu_io_registers_[7]=ht;
                slow.hardware_event_cache_.invalidate();fast.hardware_event_cache_.invalidate();
            }
            if(i%127==0){ // Snapshot-load invalidation / external frame epoch change.
                ++slow.completed_frames;++fast.completed_frames;
                if(i%254==0){slow.hardware_event_cache_.invalidate();fast.hardware_event_cache_.invalidate();}
            }
            const unsigned clocks=i%97==0?random()%30000:random()%121;
            const bool pause=random()&1;
            if(fast.hardware_event_cache_.admits(fast.master_clocks_,fast.completed_frames,clocks))++fast_intervals;
            slow.reference_advance(clocks,pause);fast.fast_advance(clocks,pause);
            if(static_cast<eb::ClockState&>(slow)!=static_cast<eb::ClockState&>(fast)||slow.events!=fast.events)
                throw std::runtime_error("Clock state or ordered PPU/HDMA/frame/APU event trace differs");
            auto a=slow.composition_policy_.stats(),b=fast.composition_policy_.stats();
            if(a.composed!=b.composed||a.skipped!=b.skipped||a.last_composed_frame!=b.last_composed_frame)
                throw std::runtime_error("Frame composition policy counters differ");
            slow.events.clear();fast.events.clear();++checked;
        }
    }
    if(!fast_intervals)throw std::runtime_error("No event-free intervals exercised");
    std::cout<<"fast_intervals="<<fast_intervals<<" / ";
    std::cout<<checked<<" production-kernel clock advances match reference state and ordered event/APU slices\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
