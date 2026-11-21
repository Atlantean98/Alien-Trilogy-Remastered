#pragma once
#include "DropRendering.h"
#include <optional>
#include <algorithm>
struct ExplorerBulletTrail {
 trilogy::original::Vec3s start,end;
 std::array<unsigned char,3> color,endColor;
 unsigned lines=1;
};
// Host adapter for the verified 0x2d004 dispatch and 0x2ccfc line packets.
inline std::optional<ExplorerBulletTrail> explorerBulletTrail(trilogy::original::PlayerControlMemory m,unsigned p){
 if(m.b(0x95f9c)!=1)return {};
 unsigned kind=m.b(p+30),mode=trilogy::original::getRenderMode(m);
 if(!((kind==0||kind==2||kind==4||kind==5)&&mode!=2))return {};
 ExplorerBulletTrail trail;auto pos=m.d(p),vel=m.d(p+4);for(unsigned c=0;c<3;++c){trail.start[c]=m.s(pos+c*2);trail.end[c]=short(trail.start[c]+(kind==0?(m.s(vel+c*2)>>1):m.s(vel+c*2)));}
 std::array<unsigned char,3> first{160,160,160},second{255,255,255};if(m.w(0xa0c3c)>=2){auto lights=m.d(0xa0bc8+0xb0);for(unsigned c=0;c<3;++c){unsigned factor=m.d(0x9bf24+c*4);first[c]=std::min(255u,unsigned(m.b(lights+4+c))*factor/3072);second[c]=std::min(255u,unsigned(m.b(lights+28+4+c))*factor/3072);}}
 trail.color=kind==0?std::array<unsigned char,3>{second[0],second[1],0}:kind==2?std::array<unsigned char,3>{0,first[1],second[2]}:kind==4?std::array<unsigned char,3>{0,second[1],second[2]}:std::array<unsigned char,3>{first[0],0,0};trail.endColor=second;trail.lines=kind==4?2:1;return trail;
}

struct ExplorerFlameGlow {double halfSize;unsigned frame;std::array<unsigned char,3> color;};
inline std::optional<ExplorerFlameGlow> explorerFlameGlow(trilogy::original::PlayerControlMemory m,unsigned p,unsigned frameSeed){
 if(m.b(0x95f9c)!=1||m.b(p+30)!=3)return {};unsigned life=m.b(p+28);if(!life||life>=16)return {};
 // 0x2c27c: screen half-size=(16-life)*(life<4?5:3)*512/depth.
 // Convert its focal-256 projection to world units for modern FOV support.
 ExplorerFlameGlow glow{double((16-life)*(life<4?5:3)*2),frameSeed&7,{250,160,160}};
 if(m.w(0xa0c3c)){auto light=m.d(0xa0bc8+0xb0);for(unsigned c=1;c<3;++c)glow.color[c]=std::min(255u,unsigned(m.b(light+4+c))*m.d(0x9bf24+c*4)/3072);}
 return glow;
}
