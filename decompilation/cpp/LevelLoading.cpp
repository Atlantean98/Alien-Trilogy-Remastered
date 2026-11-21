#include "LevelLoading.h"
#include <bit>
namespace trilogy::original {
namespace {using Mem=PlayerControlMemory;int si(unsigned n){return std::bit_cast<std::int32_t>(n);}}
void relocateLevelGeometry(Mem m,unsigned state){
    auto faces=m.d(state+0x7c),vertices=m.d(state+0x84),cell=m.d(state+0x78);
    auto cells=m.w(state+0x68)*static_cast<unsigned>(m.w(state+0x6a));
    for(unsigned i=0;i<cells;++i,cell+=16){
        auto face=m.d(cell);m.d(cell,si(face)<0?0:faces+face*20);
        if(!m.b(cell+13))m.b(cell+13,m.b(cell+11)+48);
    }
    auto count=m.s(state+0x4e)-1;
    for(int i=0;i<count;++i,faces+=20){
        for(unsigned j=0;j<3;++j)m.d(faces+j*4,vertices+m.d(faces+j*4)*8);
        if(si(m.d(faces+12))>=0)m.d(faces+12,vertices+m.d(faces+12)*8);
        auto pair=m.w(faces+18);m.b(faces+18,pair>>8);m.b(faces+19,pair);
    }
}
void initializeLevelLayout(Mem m,unsigned state,const PlayerControlService& service){
    for(unsigned i=0;i<37;++i)m.w(state+i*2,0);
    auto header=m.d(state+0xa4);
    const unsigned fields[][2]={{0x4c,0},{0x4e,2},{0x68,4},{0x6a,6},{0x5c,8},{0x5e,10},{0x52,14},{0x54,16},{0x56,18},{0x58,20},{0x5a,22},{0x60,24},{0x70,26},{0x72,30}};
    for(auto& field:fields)m.w(state+field[0],m.w(header+field[1]));
    m.w(state+0x50,m.b(header+12));m.w(state+0x74,m.b(header+13));m.b(state+0x62,m.b(header+28));m.b(state+0x63,m.b(header+29));m.d(state+0x64,m.d(header+32));
    auto cursor=header+36;m.d(state+0x84,cursor);
    cursor+=static_cast<unsigned>(m.s(state+0x4c))*8;m.d(state+0x7c,cursor);
    cursor+=static_cast<unsigned>(m.s(state+0x4e))*20;m.d(state+0x78,cursor);
    cursor+=m.w(state+0x68)*static_cast<unsigned>(m.w(state+0x6a))*16;m.d(state+0x88,cursor);
    cursor+=static_cast<unsigned>(m.s(state+0x50))*8;m.d(state+0x80,cursor);
    cursor+=static_cast<unsigned>(m.s(state+0x52))*20;m.d(state+0x8c,cursor);
    cursor+=static_cast<unsigned>(m.s(state+0x54))*8;m.d(state+0x90,cursor);
    cursor+=static_cast<unsigned>(m.s(state+0x56))*16;m.d(state+0x94,cursor);
    cursor+=static_cast<unsigned>(m.s(state+0x58))*8;m.d(state+0x98,cursor);
    cursor+=static_cast<unsigned>(m.s(state+0x5a))*16;m.d(state+0x9c,cursor);
    cursor+=m.b(state+0x62)*4;m.d(state+0xa0,cursor);
    cursor+=m.b(state+0x63)*4;m.d(state+0xb0,cursor);
    cursor+=static_cast<unsigned>(m.s(state+0x74))*28;m.d(state+0xa8,cursor);
    cursor+=static_cast<unsigned>(m.s(state+0x70))*4;m.d(state+0xac,cursor);
    m.w(state+0x6c,m.w(state+0x68)<<8);m.w(state+0x6e,m.w(state+0x6a)<<8);
    relocateLevelGeometry(m,state);
    for(auto& cap:std::initializer_list<std::array<unsigned,2>>{{0x52,128},{0x54,128},{0x56,128},{0x58,64},{0x5a,32},{0x70,256}})if(m.s(state+cap[0])>static_cast<int>(cap[1]))m.w(state+cap[0],cap[1]);
    /* 128: a stitched Extermination deck carries both maps' trigger actions and commands (indices are bytes). */if(m.b(state+0x62)>128)m.b(state+0x62,128);if(m.b(state+0x63)>128)m.b(state+0x63,128);
    service(m,0x28860,0,0,0);service(m,0x41448,0,0,0);
}
void initializeDoorFloorHeights(Mem m,unsigned records,std::uint16_t count){
    m.d(0x2348d4,records);m.w(0x2348dc,count);
    for(unsigned i=0;i<count;++i,records+=8)m.b(records+5,m.b(m.d(0xa0c40)+(m.b(records+1)*m.w(0xa0c30)+m.b(records))*16+11));
}
void initializeLiftFloorHeights(Mem m,unsigned records,std::uint16_t count){
    m.d(0x236ea0,records);m.w(0x236eaa,count);
    for(unsigned i=0;i<count;++i,records+=16){auto floor=m.b(m.d(0xa0c40)+(m.b(records+1)*m.w(0xa0c30)+m.b(records))*16+11);m.b(records+4,floor);m.b(records+5,m.b(records+5)+floor);}
}
void initializeLevelDoors(Mem m){auto count=m.w(0xa0c20);if(count)initializeDoorFloorHeights(m,m.d(0xa0c5c),count);}
void initializeLevelLifts(Mem m){initializeLiftFloorHeights(m,m.d(0xa0c60),m.w(0xa0c22));}
void resetLevelCollisionState(Mem m,unsigned state){
    auto cell=m.d(state+0x78);unsigned maximum=0,count=m.w(state+0x68)*static_cast<unsigned>(m.w(state+0x6a));
    for(unsigned i=0;i<count;++i,cell+=16){m.b(cell+12,0);m.b(cell+4,0);auto type=m.b(cell+10);m.b(cell+7,type==1||(type>19&&type<25)?128:0);if(maximum<m.b(cell+5))maximum=m.b(cell+5);}
    for(unsigned i=0;i<256;++i)m.b(0x234780+i,0);m.b(0x234780,255);m.w(state+0x76,maximum);
}
PlayerControlService withLevelLoading(const PlayerControlService& external){return [external](Mem& m,unsigned routine,unsigned a,unsigned b,unsigned c){
    switch(routine){case 0x27530:initializeLevelLayout(m,a,withLevelLoading(external));break;case 0x272f0:relocateLevelGeometry(m,a);break;case 0x28810:initializeDoorFloorHeights(m,a,static_cast<std::uint16_t>(b));break;case 0x413f0:initializeLiftFloorHeights(m,a,static_cast<std::uint16_t>(b));break;case 0x28860:initializeLevelDoors(m);break;case 0x41448:initializeLevelLifts(m);break;case 0x27c68:resetLevelCollisionState(m,a);break;default:return external(m,routine,a,b,c);}return PlayerControlResult{};
};}
}
