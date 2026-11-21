#include "SoftwareRendering.h"
#include <bit>

namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;
unsigned sx(int n){return static_cast<unsigned>(n);}
int si(unsigned n){return std::bit_cast<std::int32_t>(n);}
int word(unsigned n){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}
void submitModel(Mem m,unsigned model,unsigned cell,unsigned table,const RenderService& f){
    auto colors=m.d(0x2344a8)+m.b(cell+14)*16u;
    for(auto primitive=model;;primitive+=20){
        auto index=m.w(primitive+16);auto vertices=m.d(0x2344a4)+index*16u;auto kind=m.b(primitive+19);
        if(kind<35&&index<m.w(0x2344ae)){unsigned args[]={primitive,colors,vertices};f(m,m.d(table+kind*4),args);}
        if(m.b(primitive+18)&128)break;
    }
}
}
std::int32_t absoluteWrapped(std::int32_t n){return n<0?si(0u-sx(n)):n;}
void initializeOrderingTable(Mem m,unsigned table,std::uint16_t count){m.d(table,0);m.d(table+4,0);for(unsigned i=1;i<count;++i){m.d(table+i*12,table+(i-1)*12);m.d(table+i*12+4,0);}}
void submitModelNear(Mem m,unsigned model,unsigned cell,const RenderService& f){submitModel(m,model,cell,0x96ed0,f);}
void submitModelMiddle(Mem m,unsigned model,unsigned cell,const RenderService& f){submitModel(m,model,cell,0x96fe8,f);}
void submitModelFar(Mem m,unsigned model,unsigned cell,const RenderService& f){submitModel(m,model,cell,0x96f5c,f);}
void rasterizeLinePacket(Mem m,unsigned packet){
    int dx=word(m.w(packet+20)-m.w(packet+12)),dy=word(m.w(packet+22)-m.w(packet+14));
    unsigned ax=sx(absoluteWrapped(dx)),ay=sx(absoluteWrapped(dy));unsigned steps=word(ay)>word(ax)?ay:ax;
    auto x=sx(si(sx(m.s(packet+12))*m.d(0x3f6e44))/320),y=sx(si(sx(m.s(packet+14))*m.d(0x3f6e4c))/240);
    unsigned ex=0,ey=0;
    auto pixel=[&](){int px=word(x),py=word(y);if(px>=0&&py>=0&&px<=si(m.d(0x9d504))&&py<=si(m.d(0x9d508)))m.b(m.d(0x9d544)+static_cast<unsigned>(py*320+px),16);};
    // The original stores DX=16 across the abs helper; packet RGB is unused.
    pixel();for(int i=0;i<word(steps);++i){bool changed=false;ex+=ax;ey+=ay;
        if(word(ex)>word(steps)){ex-=steps;x+=dx>0?1u:0xffffffffu;changed=true;}
        if(word(ey)>word(steps)){ey-=steps;y+=dy>0?1u:0xffffffffu;changed=true;}
        if(changed)pixel();
    }
}
void drawRenderPacket(Mem m,unsigned packet,const RenderService& f){auto callback=m.d(packet+4);if(callback){unsigned args[]={packet};f(m,callback,args);}}
void clearSoftwareFramebuffers(Mem m){for(unsigned i=0;i<76800;++i)m.w(0x28c888+i*2,0);for(unsigned i=0;i<76800;++i)m.w(0x2b2088+i*2,0);}
void buildPaletteShadingTables(Mem m){
    for(unsigned i=0;i<256;++i){
        m.b(0x3f2af0+i,(m.b(0x3f88a0+i*3)>>1)<<2);
        auto blue=m.b(0x3f88a2+i*3);
        m.b(0x3f29f0+i,(m.b(0x3f88a1+i*3)>>1)<<2);
        m.b(0x3f2bf0+i,(blue>>1)<<2);
    }
    for(unsigned shade=0;shade<64;++shade)for(unsigned component=0;component<32;++component){
        auto value=(component*8*shade)>>8;if(value>31)value=31;
        auto offset=(shade*32+component)*4;
        m.d(0x3f2cf0+offset,value<<10);m.d(0x3f4cf0+offset,value<<5);m.d(0x3f09d8+offset,value);
    }
}
void buildPaletteBlendTable(Mem m){
    for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b){
        auto blue=(m.b(0x3f2bf0+b)+m.b(0x3f2bf0+a))>>3;
        auto green=(m.b(0x3f2af0+b)+m.b(0x3f2af0+a))>>3;
        auto red=(m.b(0x3f29f0+a)+m.b(0x3f29f0+b))>>3;
        m.b(0x3d88d8+b*256+a,m.b(0x3e89d8+blue+green*1024+red*32));
    }
}
void rebuildPaletteTables(Mem m){buildPaletteShadingTables(m);buildPaletteBlendTable(m);}
void configureSoftwareViewport(Mem m){
    m.b(0x3f6e85,1);auto stride=m.d(0x3f6d78);
    switch(m.d(0x9d50c)){
        case 0:m.d(0x9d544,0x28c888);m.d(0x9d504,319);m.d(0x9d508,239);m.d(0x3f6e44,320);m.d(0x3f6e4c,240);break;
        case 1:m.d(0x9d544,0x28c89c+stride*15);m.d(0x9d504,279);m.d(0x9d508,209);m.d(0x3f6e44,280);m.d(0x3f6e4c,210);break;
        case 2:case 3:case 4:m.d(0x9d544,0x28c8d8+stride*60);m.d(0x9d504,159);m.d(0x9d508,119);m.d(0x3f6e44,160);m.d(0x3f6e4c,120);break;
    }
}
void clearCurrentFramebuffer(Mem m){auto buffer=m.d(0x9d544);for(unsigned i=0;i<0x4b00;++i)m.d(buffer+i*4,0);}
void renderSoftwareFrame(Mem m,unsigned head,const RenderService& f,const FrameTimerPoll& poll){
    m.d(0x3f6e40,m.d(0x9d544));m.d(0x9d544,0x28c888);clearCurrentFramebuffer(m);m.d(0x9d544,m.d(0x3f6e40));
    if(m.b(0x3f6e85))configureSoftwareViewport(m);
    else{m.d(0x9d544,0x28c888);m.d(0x9d508,239);m.d(0x9d504,319);m.d(0x3f6e44,320);m.d(0x3f6e4c,240);}
    m.b(0x3f6e84,m.b(0x3fa91f));m.d(0x3f6e48,0);
    for(auto packet=head;packet;packet=m.d(packet))if(auto callback=m.d(packet+4)){
        unsigned args[]={packet};f(m,callback,args);m.d(0x3f6e48,m.d(0x3f6e48)+1);
    }
    m.b(0x3f6e84,m.b(0x3fa91f)-m.b(0x3f6e84));m.b(0x3fa91f,0);
    m.d(0x3f6e40,m.d(0x9d544));m.d(0x9d544,0x28c888);f(m,0x577c8,{});m.d(0x9d544,m.d(0x3f6e40));
    if(m.b(0x3f6e86))do{poll(m);}while(m.b(0x3fa91f)<3);
}
RenderService withSoftwareRendering(const RenderService& external){
    return [external](Mem& m,unsigned routine,std::span<const unsigned> a)->unsigned{
        switch(routine){
            case 0x2504c:submitModelNear(m,a[0],a[1],external);break;
            case 0x26358:submitModelMiddle(m,a[0],a[1],external);break;
            case 0x25a6c:submitModelFar(m,a[0],a[1],external);break;
            case 0x48bac:rasterizeLinePacket(m,a[0]);break;
            case 0x48a28:drawRenderPacket(m,a[0],withSoftwareRendering(external));break;
            case 0x489c4:clearSoftwareFramebuffers(m);break;
            case 0x4877c:buildPaletteShadingTables(m);break;
            case 0x486e4:buildPaletteBlendTable(m);break;
            case 0x486d4:rebuildPaletteTables(m);break;
            case 0x467f4:configureSoftwareViewport(m);break;
            case 0x5c4fe:clearCurrentFramebuffer(m);break;
            default:return external(m,routine,a);
        }
        return 0;
    };
}
}
