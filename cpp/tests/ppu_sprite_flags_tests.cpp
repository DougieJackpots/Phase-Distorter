#include "eb/scene_read_view.hpp"
#include "eb/game_scene_renderer.hpp"
#include "generated_profile.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
int main(){
 std::array<std::uint8_t,0x20000> ram{};std::array<std::uint8_t,65536> vram{};
 std::array<std::uint8_t,512> pal{};std::array<std::uint8_t,544> oam{};
 std::array<std::uint8_t,64> regs{};std::array<std::uint16_t,4> sx{},sy{};
 std::array<std::int16_t,6> affine{};std::array<std::int16_t,2> offsets{};
 std::array<std::uint32_t,256*224> native{};eb::SourceProfile profile{};
 std::uint32_t seed=42;const auto random=[&]{seed=seed*1664525+1013904223;return seed;};
 for(unsigned trial=0;trial<4096;++trial){
  for(auto& b:vram)b=random()>>24;for(auto& b:pal)b=random()>>24;for(auto& b:oam)b=random()>>24;
  for(auto& b:regs)b=random()>>24;
  regs[1]=std::uint8_t((trial%8)*32 | (random()&31));regs[5]=trial%8;
  eb::SceneReadView view{ram,vram,pal,oam,{},regs,sx,sy,affine,offsets,native,profile,eb::GameVersion::US,0,0,std::uint16_t(random())};
  std::array<eb::PpuPixel,358> pixels{};
  unsigned width=trial%2?256:358;int origin=trial%2?0:-51;
  const auto y=random()%224;
  auto status=view.sample_sprite_pixels(y,{pixels.data(),width},origin);
  auto flags=view.sample_sprite_pixels(y,{},origin,0,false);
  if(status!=flags) throw std::runtime_error("Flags-only raster differs from composed raster");
  // Replay the exact same raster row for flags-only evaluation below.
  (void)status;
 }
}
