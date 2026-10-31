#include "eb/snes_audio_dsp.hpp"
#include "eb/snapshot_archive.hpp"
#include "SPC_DSP.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void copy_state(unsigned char** cursor,void* source,std::size_t size){std::memcpy(*cursor,source,size);*cursor+=size;}
}
int main(){try{
    std::array<std::uint8_t,65536> ram{},reference_ram{};
    for(unsigned i=0;i<8;++i){
        ram[0x200+i*4]=0;ram[0x201+i*4]=3;
        ram[0x202+i*4]=0;ram[0x203+i*4]=3;
    }
    const std::array<std::uint8_t,9> block{0xb3,0x01,0x35,0x67,0x65,0x31,0xfe,0xcb,0xae};
    std::copy(block.begin(),block.end(),ram.begin()+0x300);reference_ram=ram;
    eb::SnesAudioDsp actual(ram);SPC_DSP reference;reference.init(reference_ram.data());
    const auto write=[&](unsigned address,unsigned value){
        actual.write_register(std::uint8_t(address),std::uint8_t(value));
        if(!(address&128))reference.write(address,value);
    };
    for(unsigned i=0;i<128;++i)write(i,0);
    for(unsigned voice=0;voice<8;++voice){
        write(voice*16,0x70-voice*3);write(voice*16+1,0x38+voice*2);
        write(voice*16+2,voice*13);write(voice*16+3,0x10);write(voice*16+4,voice);write(voice*16+7,0x7f);
    }
    write(0x6c,0);write(0x5d,2);write(0x0c,0x70);write(0x1c,0x70);
    write(0x2c,0x30);write(0x3c,0x30);write(0x0d,0x20);
    write(0x6d,0x40);write(0x7d,1);write(0x4d,0xff);write(0x0f,0x7f);write(0x4c,0xff);
    std::array<SPC_DSP::sample_t,128> output{};
    std::vector<std::int16_t> expected,received;expected.reserve(8192);received.reserve(8192);
    std::uint32_t seed=0x3120;auto random=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    std::uint64_t samples=0;unsigned snapshots=0;
    for(unsigned step=0;step<20000;++step){
        // Interleave real register changes and shared BRR/echo RAM writes.
        if(step%29==0){const auto address=random()%128;write(address,random()&255);}
        if(step%31==0){const auto at=(step&1)?0x301+(random()%8):0x4000+(random()%2048);
            ram[at]=reference_ram[at]=std::uint8_t(random());}
        const unsigned clocks=step%101==0?4096:step%17==0?1024:random()%13;
        actual.advance_audio_clocks(clocks);expected.clear();unsigned remaining=clocks;
        while(remaining){
            const auto count=std::min(remaining,1024u);reference.set_output(output.data(),output.size());
            reference.run(count);expected.insert(expected.end(),output.begin(),output.begin()+reference.sample_count());
            remaining-=count;
        }
        actual.take_stereo_samples(received);require(received==expected,"PCM availability or values differ");
        samples+=expected.size();require(actual.generated_stereo_frame_count()*2==samples,"Sample counter differs");
        require(ram==reference_ram,"Shared BRR/echo RAM differs");
        for(unsigned address=0;address<128;++address)
            require(actual.read_register(address)==reference.read(address),"DSP register differs");
        if(step%317==0){
            eb::SnapshotArchive saved;actual.snapshot_io(saved);
            eb::SnapshotArchive inspect(saved.bytes());std::vector<std::uint8_t> state;inspect(state);
            std::vector<std::uint8_t> expected_state(SPC_DSP::state_size);auto* cursor=expected_state.data();
            reference.copy_state(&cursor,copy_state);expected_state.resize(cursor-expected_state.data());
            require(state==expected_state,"Full DSP processor snapshot differs");
            const auto saved_ram=ram;
            actual.advance_audio_clocks(257);actual.write_register(0x6c,0x80);
            ram=saved_ram;eb::SnapshotArchive restore(saved.bytes());actual.snapshot_io(restore);restore.finish();
            require(actual.take_stereo_samples().empty(),"Restored output queue has stale samples");++snapshots;
        }
    }
    std::cout<<"20000 DSP clock slices, "<<samples<<" exact PCM samples, shared RAM, registers and "
        <<snapshots<<" full-state snapshot round trips match the revision 4 output path\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
