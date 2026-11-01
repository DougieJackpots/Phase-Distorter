#include "native_ppu_color.hpp"
#include <iostream>
#include <stdexcept>
using eb::detail::NativeColorPixel;
void require(bool ok){if(!ok)throw std::runtime_error("Native colour/window scalar comparison failed");}
bool affected(unsigned value,bool inside){return value==3||(value==1&&!inside)||(value==2&&inside);}
std::uint32_t scalar(const NativeColorPixel& main,const NativeColorPixel& sub,unsigned select,unsigned math,
                     unsigned fixed,unsigned brightness,bool inside){
    const bool clipped=affected(select>>6,inside);
    unsigned color=clipped?0:main.color;
    if(!affected((select>>4)&3,inside)&&main.math&&(math&(1u<<main.layer))){
        const auto other=(select&2)?sub.color:fixed;
        const bool half=(math&0x40)&&!clipped&&(!(select&2)||sub.priority>=0);
        unsigned mixed=0;
        for(unsigned shift=0;shift<15;shift+=5){
            int a=(color>>shift)&31,b=(other>>shift)&31;
            int c=(math&0x80)?std::max(0,a-b):a+b;
            if(half)c/=2;
            mixed|=unsigned(std::min(31,c))<<shift;
        }
        color=mixed;
    }
    const auto component=[&](unsigned v){unsigned scaled=(v*brightness+7)/15;return(scaled<<3)|(scaled>>2);};
    return 0xff000000|(component(color&31)<<16)|(component((color>>5)&31)<<8)|component((color>>10)&31);
}
bool window(const std::array<std::uint8_t,64>& r,unsigned layer,unsigned x){
    const auto selection=(r[0x23+layer/2]>>((layer&1)*4))&15;
    if(!(selection&10))return false;
    const bool e1=selection&2,e2=selection&8;
    bool a=x>=r[0x26]&&x<=r[0x27],b=x>=r[0x28]&&x<=r[0x29];
    if(selection&1)a=!a;
    if(selection&4)b=!b;
    if(!e1)return e2&&b;
    if(!e2)return a;
    switch(layer<4?(r[0x2a]>>(layer*2))&3:(r[0x2b]>>((layer-4)*2))&3){
        case 0:return a||b;case 1:return a&&b;case 2:return a!=b;default:return a==b;
    }
}
int main(){try{
    for(unsigned op=0;op<4;++op)for(unsigned a=0;a<32;++a)for(unsigned b=0;b<32;++b){
        int expected=(op&1)?std::max(0,int(a)-int(b)):int(a+b);
        if(op&2)expected/=2;
        require(eb::detail::native_math_channel(a,b,op)==unsigned(std::min(31,expected)));
    }
    std::uint64_t packed_cases=0;
    for(unsigned op=0;op<4;++op)for(unsigned a=0;a<32;++a)for(unsigned b=0;b<32;++b)
        for(unsigned shift:{0u,5u,10u})for(unsigned other_a:{0u,32767u,0x421u,0x7bdeu})
            for(unsigned other_b:{0u,32767u,0x421u,0x7bdeu}) {
                const unsigned ca=(other_a&~(31u<<shift))|(a<<shift);
                const unsigned cb=(other_b&~(31u<<shift))|(b<<shift);
                const unsigned expected=eb::detail::native_math_channel(ca&31,cb&31,op)|
                    (eb::detail::native_math_channel((ca>>5)&31,(cb>>5)&31,op)<<5)|
                    (eb::detail::native_math_channel((ca>>10)&31,(cb>>10)&31,op)<<10);
                require(eb::detail::native_mix_color(ca,cb,op)==expected);++packed_cases;
            }
    std::uint64_t windows=0;
    for(unsigned layer=0;layer<6;++layer)for(unsigned selection=0;selection<16;++selection)
        for(unsigned logic=0;logic<4;++logic)for(auto bounds:{std::array<unsigned,4>{32,128,96,224},
            {128,32,200,100},{255,255,0,0},{0,255,64,63}}){
            std::array<std::uint8_t,64> regs{};
            regs[0x23+layer/2]=std::uint8_t(selection<<((layer&1)*4));
            regs[layer<4?0x2a:0x2b]=std::uint8_t(logic<<(layer<4?layer*2:(layer-4)*2));
            for(unsigned i=0;i<4;++i)regs[0x26+i]=std::uint8_t(bounds[i]);
            const auto regions=eb::detail::native_window_regions(regs);
            for(unsigned x=0;x<256;++x){
                const unsigned region=unsigned(x>=regs[0x26]&&x<=regs[0x27])|(unsigned(x>=regs[0x28]&&x<=regs[0x29])<<1);
                require(bool(regions[region]&(1u<<layer))==window(regs,layer,x));++windows;
            }
        }
    std::uint32_t seed=42;const auto random=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    for(unsigned i=0;i<1000000;++i){
        NativeColorPixel main{std::uint16_t(random()&32767),std::int8_t(int(random()%13)-1),std::uint8_t(random()%6),bool(random()&1)};
        NativeColorPixel sub{std::uint16_t(random()&32767),std::int8_t(int(random()%13)-1),std::uint8_t(random()%6),bool(random()&1)};
        const auto select=random()&255,math=random()&255,fixed=random()&32767,brightness=random()&15;
        const bool inside=random()&1;
        require(eb::detail::finish_native_color(main,sub,select,math,fixed,brightness,inside)==scalar(main,sub,select,math,fixed,brightness,inside));
    }
    std::uint64_t row_pixels=0;
    eb::detail::NativeFixedColorCache row_cache;
    unsigned select=0,math=0,fixed=0,brightness=0;
    for(unsigned row=0;row<4096;++row) {
        std::array<NativeColorPixel,256> main{},sub{};
        std::array<std::uint8_t,256> windows{};std::array<std::uint32_t,256> output{};
        for(unsigned x=0;x<256;++x) {
            main[x]={std::uint16_t(random()&32767),std::int8_t(int(random()%13)-1),std::uint8_t(random()%6),bool(random()&1)};
            sub[x]={std::uint16_t(random()&32767),std::int8_t(int(random()%13)-1),std::uint8_t(random()%6),bool(random()&1)};
            windows[x]=std::uint8_t(random());
        }
        if(row%4==0) {select=random()&253;math=random()&255;fixed=random()&32767;brightness=random()&15;}
        eb::detail::finish_native_fixed_row(main,select,math,fixed,brightness,windows,output,row_cache);
        for(unsigned x=0;x<256;++x) {
            require(output[x]==scalar(main[x],sub[x],select,math,fixed,brightness,bool(windows[x]&32)));++row_pixels;
        }
    }
    std::cout<<row_pixels<<" prepared-row pixels match scalar colour policies; ";
    std::cout<<packed_cases<<" packed colour triples, 4096 colour-math channels, "<<windows<<" window cases and 1000000 full colour policies match scalar arithmetic\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
