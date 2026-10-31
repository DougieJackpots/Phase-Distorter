#pragma once
#include "eb/scene_read_view.hpp"
#include "planar_tile_row.hpp"
#include "snes_ppu_constants.hpp"
#include <algorithm>

namespace eb::detail {
// Native-only tile runs: immutable raster spans, no world-map continuation or
// effect reference. Keep address/priority/palette decisions outside pixel loops.
// Emit opaque colors directly, without building and copying eight PpuPixels.
template<class Consumer>
void emit_native_background_row(const SceneReadView& view,unsigned layer,unsigned y,Consumer&& emit) {
    const auto& regs=view.ppu_registers;
    const unsigned mode=regs[5]&7;
    const bool mosaic=(regs[6]&(1u<<layer))&&(regs[6]>>4);
    if((mode>1&&mode!=3)||mosaic) {
        // The scalar sampler snaps ordinary mosaic pixels to the same x.
        // Mode 7 intentionally keeps its own affine sampling semantics.
        const unsigned step=mosaic&&mode!=7?(regs[6]>>4)+1:1;
        for(unsigned x=0;x<256;x+=step) {
            const auto pixel=view.sample_background_pixel(layer,int(x),y);
            if(pixel.priority>=0)
                for(unsigned i=0;i<std::min(step,256-x);++i)
                    emit(x+i,pixel.color,pixel.priority);
        }
        return;
    }
    const unsigned depth=background_color_depths[mode][layer];
    if(!depth)return;
    const unsigned tile_shift=(regs[5]&(0x10u<<layer))?4:3;
    const unsigned map=regs[7+layer],width=(map&1)?64:32,height=(map&2)?64:32;
    const unsigned map_base=(map&0xfc)<<9;
    const unsigned base=((regs[0x0b+layer/2]>>((layer&1)*4))&15)*8192;
    const unsigned sy=(y+view.background_scroll_y[layer])&1023;
    const unsigned map_y=(sy>>tile_shift)&(height-1);
    const unsigned map_row=map_base+(map_y/32)*(width/32)*2048+(map_y&31)*64;
    const unsigned palette_layer=mode==0?layer*32:0;
    const bool direct=depth==8&&layer==0&&(regs[0x30]&1);
    for(unsigned x=0;x<256;) {
        const unsigned sx=(x+view.background_scroll_x[layer])&1023;
        const unsigned map_x=(sx>>tile_shift)&(width-1);
        const unsigned entry=view.vram_word(map_row+(map_x/32)*2048+(map_x&31)*2);
        unsigned px=sx&((1u<<tile_shift)-1),py=sy&((1u<<tile_shift)-1);
        if(entry&0x4000)px=((1u<<tile_shift)-1)-px;
        if(entry&0x8000)py=((1u<<tile_shift)-1)-py;
        const unsigned tile=((entry&1023)+px/8+(py/8)*16)&1023;
        const unsigned address=base+tile*depth*8+(py&7)*2;
        const auto decoded=decode_planar_row_halves(view.video_ram,address,depth);
        const unsigned palette_number=(entry>>10)&7;
        const unsigned palette_base=palette_layer+(depth==8?0:palette_number*(1u<<depth));
        const bool high=entry&0x2000;
        int priority;
        if(mode==0) {
            constexpr int levels[4][2]={{7,10},{6,9},{1,4},{0,3}};
            priority=levels[layer][high];
        } else if(mode==1) {
            constexpr int levels[3][2]={{6,9},{5,8},{0,2}};
            priority=levels[layer][high];
            if(layer==2&&high&&(regs[5]&8))priority=11;
        } else {
            constexpr int levels[2][2]={{2,6},{0,4}};
            priority=levels[layer][high];
        }
        const unsigned lane=sx&7,count=std::min(8-lane,256-x);
        for(unsigned i=0;i<count;++i) {
            const unsigned source=(entry&0x4000)?7-lane-i:lane+i;
            const auto word=source<4?decoded.low:decoded.high;
            const unsigned index=(word>>((source&3)*8))&255;
            if(!index)continue;
            const auto color=direct?
                ((index&7)<<2)|((palette_number&1)<<1)|((index&0x38)<<4)|
                ((palette_number&2)<<5)|((index&0xc0)<<7)|((palette_number&4)<<10):
                view.palette(palette_base+index);
            emit(x+i,std::uint16_t(color),priority);
        }
        x+=count;
    }
}
} // namespace eb::detail
