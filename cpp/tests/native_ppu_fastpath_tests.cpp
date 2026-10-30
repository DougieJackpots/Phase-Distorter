#include "eb/scene_read_view.hpp"
#include "eb/game_scene_renderer.hpp"
#include "generated_profile.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#ifndef EB_PPU_FAST_STANDALONE
#include "eb/snes_bus.hpp"
#endif
int main(){
#ifndef EB_PPU_FAST_STANDALONE
 if(!(eb::SnesBus::compiled_core_fast_paths()&4))return 77;
#endif
 std::uint64_t checked=0,accelerated_rows=0;
 std::array<std::uint8_t,0x20000> ram{};std::array<std::uint8_t,65536> vram{};
 std::array<std::uint8_t,512> pal{};std::array<std::uint8_t,544> oam{};
 std::array<std::uint8_t,64> regs{};std::array<std::uint16_t,4> sx{},sy{};
 std::array<std::int16_t,6> affine{};std::array<std::int16_t,2> offsets{};
 std::array<std::uint32_t,256*224> native{};eb::SourceProfile profile{};
 std::uint32_t seed=42;const auto random=[&]{seed=seed*1664525+1013904223;return seed;};
 for(unsigned trial=0;trial<16384;++trial){
  for(auto& b:vram)b=random()>>24;
  for(auto& b:pal)b=random()>>24;
  for(auto& b:oam)b=random()>>24;
  for(auto& b:regs)b=random()>>24;
  for(auto& v:sx)v=std::uint16_t(random());
  for(auto& v:sy)v=std::uint16_t(random());
  for(auto& v:affine)v=std::int16_t(int(random()%1024)-512);
  for(auto& v:offsets)v=std::int16_t(int(random()%1024)-512);
  regs[1]=std::uint8_t((trial%8)*32 | (random()&31));regs[5]=trial%8;
  if(trial%3==0){regs[0x30]=0;regs[0x31]=0;regs[0x2e]=0;}
  else if(trial%3==1){regs[0x30]=trial&1;regs[0x31]&=0xbf;regs[0x2e]=0;}
  else if(trial%7==0){regs[0x30]=2;regs[0x31]=0;regs[0x2e]=0;}
  eb::SceneReadView view{ram,vram,pal,oam,{},regs,sx,sy,affine,offsets,native,profile,eb::GameVersion::US,0,0,std::uint16_t(random())};
  view.fixed_color=trial%5==0?std::uint16_t(random()&32767):0;
  std::array<eb::PpuPixel,256> pixels{};
  for(auto& p:pixels){p.color=random()&32767;p.priority=int(random()%13)-1;p.layer=4;p.math=random()&1;p.palette_index=random()&255;}
  std::array<std::uint32_t,256> output{};
  eb::GameSceneRenderer renderer;
  eb::BackgroundTileRows reference_rows,fast_rows;
  if(trial&1)view.tile_rows=&reference_rows;
  renderer.set_fast_native_composition(false);
  renderer.compose_native_scanline(view,trial%224,pixels,output);
  std::array<std::uint32_t,256> accelerated{};
  if(trial&1)view.tile_rows=&fast_rows;
  renderer.set_fast_native_composition(true);
  renderer.compose_native_scanline(view,trial%224,pixels,accelerated);
  if(output!=accelerated)throw std::runtime_error("Guarded PPU composition differs from canonical output");
  const auto counts=renderer.native_composition_counts();
  if(counts[0]+counts[1]!=2)throw std::runtime_error("Native row counter mismatch");
  accelerated_rows+=counts[0];
  checked+=256;
 }
 if(!accelerated_rows)throw std::runtime_error("No accelerated rows exercised");
 std::cout<<"accelerated_rows="<<accelerated_rows<<" / ";
 std::cout<<checked<<" native PPU pixels match canonical composition across modes, math, windows, mosaic, scroll and priority\n";
}
