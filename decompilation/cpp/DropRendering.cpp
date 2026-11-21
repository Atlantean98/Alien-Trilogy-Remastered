#include "DropRendering.h"
#include "ProjectileRuntime.h"
#include "CombatRuntime.h"
#include <bit>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;
unsigned sx(int n){return static_cast<unsigned>(n);}
int si(unsigned n){return std::bit_cast<std::int32_t>(n);}
std::int16_t word(unsigned n){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}
int mul(int a,int b){return si(sx(a)*sx(b));}
Vec3s vectorAt(Mem m,unsigned a){return {word(m.w(a)),word(m.w(a+2)),word(m.w(a+4))};}
Matrix3s matrixAt(Mem m,unsigned a){Matrix3s matrix;for(unsigned i=0;i<9;++i)matrix[i]=word(m.w(a+i*2));return matrix;}
unsigned invoke(Mem m,const RenderService& f,unsigned routine,std::initializer_list<unsigned> args){return f(m,routine,std::span<const unsigned>(args.begin(),args.size()));}
void uv(Mem m,unsigned packet,int frame){for(unsigned i=0;i<4;++i)m.d(packet+0x10+i*8,m.d(0x22f16c+sx(frame*16)+i*4));}
void color(Mem m,unsigned packet,const std::array<std::uint8_t,3>& rgb){for(unsigned i=0;i<3;++i)m.b(packet+8+i,rgb[i]);}
std::array<std::uint8_t,3> palette(Mem m){auto p=m.d(0x2344a8);return {m.b(p),m.b(p+1),m.b(p+2)};}
void rectangle(Mem m,unsigned p,ScreenPoint s,int x,int y){
    m.w(p+12,s[0]-x);m.w(p+14,s[1]-y);m.w(p+20,s[0]+x);m.w(p+22,s[1]-y);
    m.w(p+28,m.w(p+12));m.w(p+30,s[1]+y);m.w(p+36,m.w(p+20));m.w(p+38,m.w(p+30));
}
void submit(Mem m,unsigned packet,unsigned size,unsigned callback){
    m.d(packet+4,callback);auto bucket=m.d(0x22ceb0)+sx((si(m.d(0xa0ca0))>>5)*12);enqueueRenderPacket(m,bucket,packet);m.d(0x22ceac,packet+size);
}
void transformTo(Mem m,unsigned matrix,unsigned input,unsigned output){auto v=transformVector(matrixAt(m,matrix),vectorAt(m,input));for(unsigned i=0;i<3;++i)m.d(output+i*4,sx(v[i]));}
}
void spawnEntityDrop(Mem m,unsigned e) {
    if(!m.w(0x236c4c))return;
    auto rule=m.d(0x236c34)+m.b(e+0x72)*8u;
    auto count=static_cast<std::uint8_t>(m.b(rule+5)+1u);m.b(rule+5,std::min(count,m.b(rule+6)));
    if(m.b(rule+5)!=m.b(rule+6))return;
    auto p=m.d(0x236c38);if(!p)throw std::out_of_range("Original triggered drop requires a nonempty pickup pool");
    m.d(0x236c38,m.d(p+0x28));auto kind=m.b(rule+2);m.b(p+0x20,kind==24?21:kind);m.b(p+0x21,m.b(rule+3));m.b(p+0x22,m.b(rule+4));
    m.w(p,m.w(e));m.w(p+4,m.w(e+4));auto x=sx(m.s(p))+m.w(0xa0c34),z=sx(m.s(p+4))+m.w(0xa0c36);
    auto width=m.w(0xa0c30),height=m.w(0xa0c32);std::vector<LevelCell> cells(static_cast<unsigned>(width)*height);
    for(unsigned i=0;i<cells.size();++i)for(unsigned j=0;j<16;++j)cells[i].bytes[j]=m.b(m.d(0xa0c40)+i*16+j);
    LevelGrid grid{width,height,cells};m.w(p+2,sx(floorHeight(grid,x,z))+512u);m.d(p+0x1c,m.d(0xa0c40)+((z>>9)*width+(x>>9))*16);
    auto random=[&](){return static_cast<int>((nextGameRandom(m)&15)+16);};
    switch(m.b(e+0x6d)) {
        case 0:m.w(p+8,0);m.w(p+12,-random());break;
        case 1:m.w(p+8,random());m.w(p+12,-random());break;
        case 2:m.w(p+8,random());m.w(p+12,0);break;
        case 3:m.w(p+8,random());m.w(p+12,random());break;
        case 4:m.w(p+8,0);m.w(p+12,random());break;
        case 5:m.w(p+8,-random());m.w(p+12,random());break;
        case 6:m.w(p+8,-random());m.w(p+12,0);break;
        case 7:m.w(p+8,-random());m.w(p+12,-random());break;
    }
    m.w(p+8,m.w(p+8)+(nextGameRandom(m)&15)-8u);auto jitter=nextGameRandom(m)&15;
    m.w(p+10,64);m.w(p+0x24,32);m.w(p+0x10,0);m.w(p+0x12,0);m.w(p+0x14,0);m.w(p+12,m.w(p+12)+jitter-8u);
    m.d(p+0x18,m.d(0x236bc4+m.b(p+0x20)*4));auto head=m.d(0x236c44);if(head)m.d(head+0x2c,p);m.d(p+0x2c,0);m.d(0x236c44,p);m.d(p+0x28,head);
}
std::int32_t projectWorldPoint(Mem m,const Vec3s& position,ScreenPoint& screen,unsigned flagsAddress) {
    auto v=transformVector(matrixAt(m,0x23ad08),position);
    for(unsigned i=0;i<3;++i){v[i]=si(sx(v[i])+m.d(0x23ad1c+i*4));m.d(0x23a4d0+i*4,sx(v[i]));}
    if(v[2]<32)return -1;
    auto scale=si(m.d(0x3f6d04));screen[0]=word(sx(mul(v[0],scale)/v[2])+m.w(0x9d5cc));screen[1]=word(sx(mul(v[1],scale)/v[2])+m.w(0x9d5ce));
    m.d(flagsAddress,0);return v[2]>>2;
}
std::uint16_t getRenderMode(Mem m){return m.w(0x96234);}
void blendVectorsQ12(Mem m,unsigned a,unsigned b,int scaleA,int scaleB,unsigned output){
    for(unsigned i=0;i<3;++i)m.w(output+i*2,sx(si(sx(mul(m.s(a+i*2),scaleA))+sx(mul(m.s(b+i*2),scaleB)))>>12));
}
std::array<std::uint8_t,3> shadeRgb(const std::array<std::uint8_t,3>& rgb,int depth) {
    int fade=255-std::clamp(std::max(depth,0)/80-170,0,255);std::array<std::uint8_t,3> out;
    for(unsigned i=0;i<3;++i)out[i]=static_cast<std::uint8_t>((rgb[i]*fade)>>8);return out;
}
void enqueueRenderPacket(Mem m,unsigned bucket,unsigned packet){m.d(packet,m.d(bucket));m.d(bucket,packet);}
void buildProjectileTrail(Mem m,unsigned p,ScreenPoint screen) {
    if(m.b(0x95f9c)!=1)return;auto packet=m.d(0x22ceac),pal=m.d(0x2344a8);auto kind=m.b(p+0x1e);
    auto end=vectorAt(m,m.d(p)),velocity=vectorAt(m,m.d(p+4));for(unsigned i=0;i<3;++i)end[i]=word(sx(end[i])+sx(kind==0?velocity[i]>>1:velocity[i]));
    if(kind==0)color(m,packet,{m.b(pal+16),m.b(pal+17),0});
    else if(kind==2)color(m,packet,{0,m.b(pal+1),m.b(pal+18)});
    else if(kind==4)color(m,packet,{0,m.b(pal+17),m.b(pal+18)});else color(m,packet,{m.b(pal),0,0});
    ScreenPoint projected{end[0],end[1]};projectWorldPoint(m,end,projected,0xa0c9c);
    auto fill=[&](unsigned q,int offset){m.w(q+12,screen[0]+offset);m.w(q+14,screen[1]+offset);m.w(q+20,projected[0]+offset);m.w(q+22,projected[1]+offset);for(unsigned i=0;i<3;++i)m.b(q+16+i,m.b(pal+16+i));};
    fill(packet,0);submit(m,packet,24,0x48bac);
    if(m.b(p+0x1e)==4){packet+=24;fill(packet,1);color(m,packet,{0,m.b(pal+17),m.b(pal+18)});submit(m,packet,24,0x48bac);}
}
void buildProjectilePoint(Mem m,ScreenPoint s,unsigned p) {
    if(m.b(0x95f9c)!=1)return;auto q=m.d(0x22ceac);
    // The original divides here although the quotient is discarded.
    if(!m.d(0xa0ca0))throw std::domain_error("Original point renderer divides by zero");
    for(unsigned i=0;i<4;++i){m.w(q+12+i*4,s[0]+(i&1));m.w(q+14+i*4,s[1]+(i>>1));}
    auto damage=m.w(p+0x1a);color(m,q,{static_cast<std::uint8_t>(damage==1?0x68:damage==2?0x47:0x56),0,0});
    if(m.b(p+0x1c)<9)color(m,q,shadeRgb({m.b(q+8),m.b(q+9),m.b(q+10)},4096-(m.b(p+0x1c)<<9)));
    m.b(q+11,0);submit(m,q,28,0x48e40);
}
void buildProjectileDiamond(Mem m,unsigned p,ScreenPoint s) {
    if(m.b(0x95f9c)!=1)return;auto q=m.d(0x22ceac);int size=(m.b(p+0x1c)<<(m.b(p+0x1d)?10:8))/si(m.d(0xa0ca0));
    auto frame=m.b(p+0x21)==3?24:m.b(p+0x21)+32;if(m.b(p+0x21)!=3)m.b(p+0x21,m.b(p+0x21)+1u);uv(m,q,frame);
    m.w(q+12,s[0]);m.w(q+14,s[1]-size);m.w(q+20,s[0]+size);m.w(q+22,s[1]);m.w(q+28,s[0]-size);m.w(q+30,s[1]);m.w(q+36,s[0]);m.w(q+38,s[1]+size);
    m.d(q+8,m.d(m.d(0x2344a8)));if(m.b(p+0x1e)==9){m.b(q+8,0);m.b(q+10,0);}else if(m.b(p+0x1e)==8){m.b(q+9,0);m.b(q+10,0);}
    m.w(q+34,0);submit(m,q,44,0x4a938);
}
void buildProjectileGlow(Mem m,ScreenPoint s,unsigned p,const RenderService& f) {
    auto q=m.d(0x22ceac);int life=m.b(p+0x1c);int size=((16-life)*(life<4?5:3)*512)/si(m.d(0xa0ca0));
    uv(m,q,nextGameRandom(m)&7);rectangle(m,q,s,size,size);m.d(q+8,m.d(m.d(0x2344a8)));m.b(q+8,250);
    m.w(q+26,invoke(m,f,0x4d2a8,{0,1,960,256}));int depth=si(m.d(0xa0ca0));m.d(0xa0ca0,depth<=512?64:depth-512);submit(m,q,44,0x4aae0);
}
void buildProjectilePlume(Mem m,ScreenPoint s,std::int16_t life,std::int16_t kind) {
    if(m.b(0x95f9c)!=1)return;int t=std::clamp<int>(life,0,31);auto q=m.d(0x22ceac);
    int frame=m.s(0x9c044+(32-t+(nextGameRandom(m)&3))*2);uv(m,q,frame);m.w(q+18,6);
    auto a=mul(si(m.d(0x9bf44+t*4)),15000)/si(m.d(0xa0ca0)),b=mul(si(m.d(0x9bfc4+t*4)),15000)/si(m.d(0xa0ca0));
    int x=kind==14?a:b,y=kind==14?b:a;x=si(sx(x)+3u-(nextGameRandom(m)&7));y=si(sx(y)+3u-(nextGameRandom(m)&7));rectangle(m,q,s,x,y);
    auto value=static_cast<std::uint8_t>(m.b(0xa0c90)&128?m.b(m.d(0x2344a8))>>1:0);m.d(0xa0cac,m.d(0xa0ca0)*4-0x1200u);
    color(m,q,shadeRgb({value,value,value},si(m.d(0xa0cac))));submit(m,q,44,0x4aae0);
}
void buildProjectileFlame(Mem m,ScreenPoint s,std::int16_t life,std::int16_t kind) {
    if(m.b(0x95f9c)!=1)return;int t=std::clamp<int>(life,0,31);auto q=m.d(0x22ceac);uv(m,q,m.s(0x9c18c+(32-t+(nextGameRandom(m)&3))*2));
    auto a=si(m.d(0x9c08c+t*4)),b=si(m.d(0x9c10c+t*4));auto depth=si(m.d(0xa0ca0));int x=mul(kind==16?a:b,kind==16?16000:12000)/depth,y=mul(kind==16?b:a,kind==16?8000:16000)/depth;
    rectangle(m,q,s,x,y);auto rgb=m.b(0xa0c90)&128?palette(m):std::array<std::uint8_t,3>{105,105,105};m.d(0xa0cac,m.d(0xa0ca0)*4);
    if(si(m.d(0xa0cac))<0x1200)m.d(q+8,0xffffffff);else {m.d(0xa0cac,m.d(0xa0cac)-0x1200u);color(m,q,shadeRgb(rgb,si(m.d(0xa0cac))));}
    submit(m,q,44,t<28?0x4aae0:0x4a938);
}
void renderProjectileMesh(Mem m,unsigned p,const RenderService& f) {
    if(m.b(0x95f9c)!=1)return;m.d(0x2344a4,0x2331d4);blendVectorsQ12(m,m.d(p),0xa0a20,4096,-4096,0xa0b98);saveTransformState(m);transformTo(m,0xa0b68,0xa0b98,0xa0b88);
    if(si(m.d(0xa0b90))>512&&si(m.d(0xa0b90))<7680&&si(m.d(0xa0b88))<7680){
        invoke(m,f,0x47630,{p+16,0xa0a28});invoke(m,f,0x47294,{0xa0b68,0xa0a28});if(m.b(p+0x1e)==21)invoke(m,f,0x46ef4,{0xa0a28,2048,2048,2048});
        invoke(m,f,0x46e4c,{0xa0a28});invoke(m,f,0x477f8,{0xa0a28,0xa0b88});invoke(m,f,0x46ec4,{0xa0a28});invoke(m,f,0x26358,{m.d(p+8),m.d(p+12)});
    }
    m.d(0x2344a4,0x2311d4);restoreTransformState(m);
    if(m.b(p+0x1e)==19)m.w(p+16,(m.w(p+16)+32u)&4095);
    if(m.b(p+0x1e)==20||m.b(p+0x1e)==21){m.w(p+16,(m.w(p+16)+64u)&4095);m.w(p+18,(m.w(p+18)+96u)&4095);}
}
void renderProjectileShard(Mem m,unsigned p,const RenderService& f) {
    if(m.b(0x95f9c)!=1)return;auto scale=m.b(p+0x1c)<<6;blendVectorsQ12(m,m.d(p),0xa0a20,4096,-4096,0xa0b98);saveTransformState(m);transformTo(m,0xa0b68,0xa0b98,0xa0b88);
    if(si(m.d(0xa0b90))>512&&si(m.d(0xa0b90))<8192&&si(m.d(0xa0b88))<8192) {
        invoke(m,f,0x47630,{p+16,0xa0a28});invoke(m,f,0x47294,{0xa0b68,0xa0a28});invoke(m,f,0x46ef4,{0xa0a28,static_cast<unsigned>(scale),static_cast<unsigned>(scale),static_cast<unsigned>(scale)});
        invoke(m,f,0x46e4c,{0xa0a28});invoke(m,f,0x477f8,{0xa0a28,0xa0b88});invoke(m,f,0x46ec4,{0xa0a28});
        auto resource=m.d(p+8);auto kind=m.b(p+0x1e);if(kind==6||kind==15){auto shade=kind==6?(m.b(resource+18)&127):m.b(m.d(p+12)+14);invoke(m,f,m.d(kind==6?0x96f6c:0x96f8c),{resource,m.d(0x2344a8)+shade*16u,m.d(0x23a494)+m.w(resource+16)*16u});}
    }
    restoreTransformState(m);if(m.b(p+0x1e)!=15)m.w(p+20,(m.w(p+20)+64u)&4095);
}
void renderProjectile(Mem m,unsigned p,const RenderService& f) {
    if(m.b(0x95f9c)!=1)return;auto kind=m.b(p+0x1e);if(kind==1)return;
    if((kind>=11&&kind<=13)||(kind>=19&&kind<=21)){renderProjectileMesh(m,p,f);return;}
    ScreenPoint screen{};m.d(0xa0ca0,sx(projectWorldPoint(m,vectorAt(m,m.d(p)),screen,0xa0c9c))*4u);auto depth=si(m.d(0xa0ca0));
    if(depth>6144||screen[0]<0||screen[0]>320||screen[1]<0||screen[1]>240)return;kind=m.b(p+0x1e);
    if(kind==0||kind==2||kind==4){if(getRenderMode(m)!=2&&depth>=512)buildProjectileTrail(m,p,screen);}
    else if(kind==5){if(!getRenderMode(m)&&depth>=512)buildProjectileTrail(m,p,screen);}
    else if(kind==3){if(depth>=16)buildProjectileGlow(m,screen,p,f);}
    else if(kind==6||kind==15){if(depth>=512)renderProjectileShard(m,p,f);}
    else if(kind==7){if(depth>=512)buildProjectilePoint(m,screen,p);}
    else if(kind>=8&&kind<=10){if(depth>=512)buildProjectileDiamond(m,p,screen);}
    else if(kind==14||kind==18){if(depth>=512)buildProjectilePlume(m,screen,m.b(p+0x1c),kind);}
    else if(kind==16||kind==17){if(depth>=512)buildProjectileFlame(m,screen,m.b(p+0x1c),kind);}
}
PlayerControlService withDropRendering(const PlayerControlService& external,const RenderService& renderer) {
    return [external,renderer](Mem& m,unsigned routine,unsigned a,unsigned b,unsigned c)->PlayerControlResult {
        if(routine==0x3b61c){spawnEntityDrop(m,a);return {};}
        if(routine==0x2d004){renderProjectile(m,a,renderer);return {};}
        return external(m,routine,a,b,c);
    };
}
}
