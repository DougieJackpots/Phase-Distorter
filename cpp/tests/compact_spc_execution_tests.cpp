// Real SPC/DSP single-step differential across every source-owned instruction.
#include "eb/spc700_audio_cpu.hpp"
#include "eb/snes_audio_dsp.hpp"
#include "eb/snapshot_archive.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace eb {
bool execute_reference_audio_instruction(Spc700AudioCpu&);
bool execute_compact_audio_instruction(Spc700AudioCpu&);
}
namespace {
struct Site {unsigned address,opcode,operand,length;};
constexpr Site sites[]={
#define EB_SPC_SITE(a,o,v,l) {a,o,v,l},
#include "compact_spc_sites.inc"
#undef EB_SPC_SITE
};
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
int main(){try{
    std::array<std::uint8_t,65536> program{};
    std::uint32_t seed=123;
    for(auto& byte:program){seed=seed*1664525+1013904223;byte=std::uint8_t(seed>>24);}
    for(const auto& site:sites)
        for(unsigned i=0;i<site.length;++i)
            program[site.address+i]=std::uint8_t(i?site.operand>>(8*(i-1)):site.opcode);
    std::uint64_t checked=0,pcm_samples=0;
    for(const auto& site:sites){
        std::array<std::uint8_t,4> to_a{1,2,3,4},to_b=to_a,from_a{},from_b{};
        eb::Spc700AudioCpu a(to_a,from_a),b(to_b,from_b);
        a.audio_ram=program;b.audio_ram=program;
        eb::SnesAudioDsp dsp_a(a),dsp_b(b);
        seed=seed*1664525+1013904223;
        a.program_counter=b.program_counter=std::uint16_t(site.address);
        a.accumulator=b.accumulator=std::uint8_t(seed);
        a.x_index=b.x_index=std::uint8_t(seed>>8);a.y_index=b.y_index=std::uint8_t(seed>>16);
        a.stack_pointer=b.stack_pointer=std::uint8_t(seed>>24);a.status_register=b.status_register=std::uint8_t(seed>>12);
        std::vector<std::uint32_t> writes_a,writes_b;
        a.observe_memory_write=[&](auto address,auto value){writes_a.push_back((std::uint32_t(address)<<8)|value);};
        b.observe_memory_write=[&](auto address,auto value){writes_b.push_back((std::uint32_t(address)<<8)|value);};
        // Vary the DSP phase so instruction cycle counts can cross a sample boundary.
        dsp_a.advance_audio_clocks(128+seed%32);dsp_b.advance_audio_clocks(128+seed%32);
        const bool reference=eb::execute_reference_audio_instruction(a);
        const bool compact=eb::execute_compact_audio_instruction(b);
        require(reference&&compact,"Valid source instruction was rejected");
        require(a.instruction_count==1&&b.instruction_count==1,"Source instruction did not execute exactly once");
        require(writes_a==writes_b&&to_a==to_b&&from_a==from_b,"SPC writes or audio ports differ");
        const auto pcm_a=dsp_a.take_stereo_samples(),pcm_b=dsp_b.take_stereo_samples();
        require(pcm_a==pcm_b,"PCM samples differ");pcm_samples+=pcm_a.size();
        eb::SnapshotArchive state_a,state_b;
        state_a(a,dsp_a);state_b(b,dsp_b);
        require(state_a.bytes()==state_b.bytes(),"SPC registers/RAM/timers/clocks or DSP state differ");
        ++checked;
    }
    std::cout<<checked<<" real SPC instruction executions match registers, memory, timers, clocks, ordered writes, DSP state and "<<pcm_samples<<" PCM samples\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
