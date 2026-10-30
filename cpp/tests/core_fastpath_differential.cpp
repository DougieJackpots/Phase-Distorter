// Optional user-asset scalar-vs-fast core comparison at full composition stride.
#include "eb/asset_store.hpp"
#include "eb/game_debug.hpp"
#include "eb/input_replay.hpp"
#include "eb/main_cpu_65816.hpp"
#include "eb/snes_audio_dsp.hpp"
#include "eb/snes_bus.hpp"
#include "eb/spc700_audio_cpu.hpp"
#include "eb/snapshot_archive.hpp"
#include "generated_assets.hpp"
#include <iostream>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Picture { std::uint64_t frame; std::vector<std::uint32_t> pixels; bool operator==(const Picture&)const=default; };
struct Machine {
    eb::SnesBus bus;
    eb::Spc700AudioCpu spc;
    eb::SnesAudioDsp dsp;
    eb::MainCpu65816 cpu;
    eb::GameDebug debug;
    std::uint64_t steps = 0;
    std::vector<Picture> pictures;
    std::vector<std::uint32_t> held_picture;
    std::vector<std::uint64_t> accesses;
    Machine(const eb::GameAssets& assets, bool enhanced, unsigned stride, unsigned compare_stride, bool native)
        : bus(assets.image, assets.version, true), spc(bus), dsp(spc), cpu(bus), debug(bus,cpu) {
        cpu.reset_from_vector(); cpu.set_gameplay_timing(enhanced);
        if (native) bus.enable_native_sprite_runtime(true, true);
        bus.set_logical_clock_policy(eb::LogicalClockPolicy::ActorFrames);
        bus.set_composition_stride(stride);
        bus.on_presentation_frame = [this,compare_stride,stride](auto pixels, unsigned width, std::uint64_t frame) {
            require(width==256,"Differential canvas changed width");
            if ((frame-1)%stride==0) held_picture.assign(pixels.begin(),pixels.end());
            else require(std::equal(pixels.begin(),pixels.end(),held_picture.begin(),held_picture.end()),
                         "Skipped frame changed the held picture");
            if ((frame-1)%compare_stride==0)
                pictures.push_back({frame,{pixels.begin(),pixels.end()}});
        };
#ifdef EB_GAMEPLAY_AUDIT
        bus.observe_bus_access = [this](bool write, std::uint32_t address, std::uint8_t value) {
            accesses.push_back((std::uint64_t(write)<<40)|(std::uint64_t(address)<<8)|value);
        };
#endif
    }
    void advance(std::uint16_t buttons) {
        const auto first=bus.completed_frames; bus.set_buttons(buttons);
        do {
            require(!cpu.is_stopped,"CPU stopped"); debug.before_step();
            steps+=cpu.advance_gameplay(~0u);
        } while(bus.completed_frames==first);
    }
    std::vector<std::uint8_t> machine_state() {
        // Pixels are checked separately at their original frame indices. Strip
        // only this host picture from the complete machine snapshot comparison.
        const auto held=bus.native_framebuffer;
        bus.native_framebuffer.fill(0);
        try {
            eb::SnapshotArchive archive;
            archive(bus,spc,dsp,cpu,debug,steps);
            bus.native_framebuffer=held;
            return archive.release_bytes();
        } catch (...) {bus.native_framebuffer=held; throw;}
    }
};
}
int main(int argc,char** argv) {
    try {
        std::string pack,script; std::uint64_t frames=1800;unsigned stride=1;bool enhanced=true,native=true;
        for(int i=1;i<argc;++i){
            const std::string flag=argv[i];
            if(flag=="--replay-only") continue; // This harness always replays without physical input.
            if(i+1==argc) throw std::invalid_argument("Missing argument value");
            const std::string value=argv[++i];
            if(flag=="--assets")pack=value;else if(flag=="--input-script")script=value;
            else if(flag=="--frames")frames=eb::parse_unsigned_integer(value,flag);
            else if(flag=="--stride")stride=unsigned(eb::parse_unsigned_integer(value,flag));
            else if(flag=="--enhanced-timing" || flag=="--native-sprites") {
                if(value!="0"&&value!="1")throw std::invalid_argument("Boolean option requires 0 or 1");
                if(flag=="--enhanced-timing")enhanced=value=="1";else native=value=="1";
            }else throw std::invalid_argument("Unknown argument: "+flag);
        }
        require(!pack.empty()&&frames>0&&stride>=1&&stride<=3,"Supply --assets pack --frames N --stride 1|2|3");
        require(eb::SnesBus::compiled_core_fast_paths()==7,"Build with EB_FAST_MEMORY_ACCESS=ON, EB_FAST_EVENT_SCHEDULER=ON and EB_FAST_NATIVE_COMPOSITION=ON");
        require(stride==1,"Core comparison requires --stride 1");
        auto assets=eb::load_game_assets(pack,eb::asset_profiles());
        auto full=std::make_unique<Machine>(assets,enhanced,1,stride,native);
        auto skipped=std::make_unique<Machine>(assets,enhanced,stride,stride,native);
        full->bus.set_core_fast_paths(false,false,false);
        skipped->bus.set_core_fast_paths(true,true,true);
        eb::InputReplay input(eb::input_script(script));
        while(full->bus.completed_frames<frames){
            const auto buttons=input.buttons_for_frame(full->bus.completed_frames);
            full->advance(buttons); skipped->advance(buttons);
            require(full->dsp.take_stereo_samples()==skipped->dsp.take_stereo_samples(),"PCM differs");
            require(full->pictures==skipped->pictures,"Composed pixels/frame indices differ");
            require(full->accesses==skipped->accesses,"Ordered bus accesses differ");
            require(full->machine_state()==skipped->machine_state(),"CPU/SPC/memory/hardware/scene snapshot differs");
            full->pictures.clear();skipped->pictures.clear();full->accesses.clear();skipped->accesses.clear();
        }
        const auto count=skipped->bus.composition_stats();
        std::cout<<"frames="<<full->bus.completed_frames<<" stride="<<stride<<" enhanced="<<enhanced
                 <<" native="<<native<<" composed="<<count.composed<<" skipped="<<count.skipped
#ifdef EB_GAMEPLAY_AUDIT
                 <<" ordered_bus_audit=enabled"
#else
                 <<" ordered_bus_audit=NOT_BUILT (rebuild with EB_GAMEPLAY_AUDIT=ON)"
#endif
                 <<" scalar-vs-fast machine snapshots, PCM and composed pixels match\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
