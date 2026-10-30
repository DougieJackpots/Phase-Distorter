#include "../../vita/src/performance_options.hpp"
#include "eb/composition_policy.hpp"
#include <sstream>
#include <iostream>
#include <stdexcept>
void require(bool v){if(!v)throw std::runtime_error("Experiment option/policy check failed");}
int main(){try{
 vita::PerformanceOptions options;require(options.enhanced_timing&&options.composition_stride==1&&options.save_enabled);
 std::istringstream config("# example\nenhanced_timing=0\ncomposition_stride=2\ncore_timing=0\nreplay_only=1\ninput_script=route.txt\nframes=3600\n");
 options.read(config);options.arguments(0,nullptr);
 require(!options.core_timing&&!options.enhanced_timing&&!options.save_enabled&&options.replay_only&&options.frames==3600&&options.composition_stride==2);
 for(const auto& text:{"composition_stride=0","composition_stride=4","enhanced_timing=2","frames=-1","frames=2junk","unknown=1"}){
  bool rejected=false;try{vita::PerformanceOptions o;std::istringstream in(text);o.read(in);}catch(const std::exception&){rejected=true;}require(rejected);
 }
 for(unsigned stride:{1u,2u,3u}){
  eb::CompositionPolicy policy;policy.set_stride(stride);
  for(unsigned frame=0;frame<61;++frame){require(policy.compose(frame)==(frame%stride==0));policy.completed(frame);}
  auto stats=policy.stats();require(stats.composed==(61+stride-1)/stride&&stats.composed+stats.skipped==61);
  require(stats.last_composed_frame==1+60/stride*stride);
 }
 std::cout<<"Experiment defaults, strict parsing, replay SRAM protection and composition counts passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
