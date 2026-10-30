// Dispatcher-only differential. Both paths use the same recording CPU boundary;
// the original generated switches are an independent selection/guard oracle.
#include "eb/spc700_audio_cpu.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace {
struct Site {unsigned address, opcode, operand, length;};
constexpr Site source_sites[] = {
#define EB_SPC_SITE(address, opcode, operand, length) {address, opcode, operand, length},
#include "compact_spc_sites.inc"
#undef EB_SPC_SITE
};
struct Trace {
    std::array<unsigned,3> addresses{};
    unsigned reads=0, opcode=0, operand=0, length=0, executions=0, pc=0;
    bool accepted=false;
    bool operator==(const Trace&)const=default;
};
Trace trace;
bool record=true;
unsigned checksum=0;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
namespace eb {
Spc700AudioCpu::Spc700AudioCpu(std::span<std::uint8_t> memory) : instruction_test_memory_(memory) {}
Spc700AudioCpu::~Spc700AudioCpu() = default;
std::uint8_t Spc700AudioCpu::read_byte(std::uint16_t address) {
    if(record){require(trace.reads<3,"Dispatcher read more than three instruction bytes");trace.addresses[trace.reads++]=address;}
    return instruction_test_memory_[address];
}
void Spc700AudioCpu::execute_opcode_semantics(std::uint8_t opcode,std::uint16_t operand,unsigned length) {
    if(record){++trace.executions;trace.opcode=opcode;trace.operand=operand;trace.length=length;}
    checksum+=unsigned(opcode)+operand+length;
    program_counter=std::uint16_t(program_counter+length);
}
bool execute_reference_audio_instruction(Spc700AudioCpu&);
}
int main(){try{
    std::array<std::uint8_t,65536> memory{};
    std::array<bool,65536> covered{},starts{};
    for(const auto& site:source_sites){
        require(!starts[site.address],"Duplicate source instruction address");starts[site.address]=true;
        for(unsigned i=0;i<site.length;++i){
            const auto address=std::uint16_t(site.address+i);
            const auto value=std::uint8_t(i?site.operand>>(8*(i-1)):site.opcode);
            require(!covered[address]||memory[address]==value,"Conflicting source instruction bytes");
            covered[address]=true;memory[address]=value;
        }
    }
    eb::Spc700AudioCpu cpu{std::span<std::uint8_t>(memory)};
    const auto run=[&](auto function,unsigned pc){trace={};cpu.program_counter=std::uint16_t(pc);
        trace.accepted=function(cpu);trace.pc=cpu.program_counter;return trace;};
    const auto compare=[&](unsigned pc){
        const auto reference=run(eb::execute_reference_audio_instruction,pc);
        const auto compact=run(eb::execute_translated_audio_instruction,pc);
        require(reference==compact,"Compact SPC selection/guard order/semantic arguments differ");
        return compact;
    };
    for(unsigned pc=0;pc<65536;++pc){auto result=compare(pc);require(result.accepted==starts[pc],"Source coverage changed");}
    std::uint64_t mutations=0;
    for(const auto& site:source_sites)
        for(unsigned byte=0;byte<site.length;++byte){
            auto& value=memory[site.address+byte];const auto original=value;
            for(unsigned replacement=0;replacement<256;++replacement){
                if(replacement==original) continue;
                value=std::uint8_t(replacement);
                auto result=compare(site.address);require(!result.accepted&&!result.executions,"Modified SPC code executed");++mutations;
            }
            value=original;
        }
    std::cout<<"65536 addresses and "<<mutations<<" writable-code mutations match original dispatch and ordered byte reads\n";
    record=false;
    const auto bench=[&](auto function,const char* label,unsigned hot_sites){
        constexpr unsigned iterations=1000000;const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<iterations;++i){cpu.program_counter=std::uint16_t(source_sites[i%hot_sites].address);require(function(cpu),"Benchmark rejected valid source site");}
        const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/iterations;
        std::cout<<label<<" hot_sites="<<hot_sites<<" host_dispatch_only_ns="<<ns<<'\n';
    };
    for(unsigned count:{64u,unsigned(std::size(source_sites))}){bench(eb::execute_reference_audio_instruction,"switch",count);bench(eb::execute_translated_audio_instruction,"compact",count);}
    std::cout<<"checksum="<<checksum<<" (host microbenchmark excludes real opcode execution, DSP and timers)\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
