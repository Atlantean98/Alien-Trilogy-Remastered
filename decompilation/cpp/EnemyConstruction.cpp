#include "EnemyRuntime.h"
#include "WorldMovement.h"
#include "ProjectileRuntime.h"
#include <bit>
#include <vector>
namespace trilogy::original {
namespace {using Mem=PlayerControlMemory;int word(unsigned n){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}}
unsigned spawnEnemyFromRecord(Mem m,std::uint16_t index,const PlayerControlService& f){
    if(m.b(0xa0c92)&2)return 0;auto e=m.d(0x2365d8);auto source=m.d(0x2365d4)+static_cast<unsigned>(word(index))*20u;if(!e)return 0;
    m.d(0x2365d8,m.d(e+200));m.w(e,m.b(source+1)*512u+256-m.w(0xa0c34));m.w(e+4,m.b(source+2)*512u+256-m.w(0xa0c36));
    auto x=static_cast<unsigned>(m.s(e))+m.w(0xa0c34),z=static_cast<unsigned>(m.s(e+4))+m.w(0xa0c36);
    std::vector<LevelCell> cells(m.w(0xa0c30)*static_cast<unsigned>(m.w(0xa0c32)));std::memcpy(cells.data(),m.bytes.data()+m.d(0xa0c40),cells.size()*sizeof(LevelCell));LevelGrid grid{m.w(0xa0c30),m.w(0xa0c32),cells};
    m.w(e+0x5a,static_cast<unsigned>(floorHeight(grid,x,z)));m.w(e+0x5c,static_cast<unsigned>(ceilingHeight(grid,x,z)));m.w(e+2,m.w(e+0x5a)+32);
    auto cell=m.d(0xa0c40)+(z>>9)*m.w(0xa0c30)*16+(x>>9)*16;m.d(e+0x78,cell);m.b(e+0x74,m.b(source));m.w(e+0x58,enemySpawnHealth(m,source));
    if(m.w(e+0x58)){if(!m.b(0x9e0a8)||!(m.w(e+0xc4)==11||m.w(e+0xc4)==10)){m.b(cell+12,m.b(e+0x74));m.b(cell+4,m.b(e+0x70));}else m.b(cell+12,0);}
    m.b(e+0x6f,0);m.w(e+0x52,32);m.w(e+0x4e,64);m.w(e+0x68,0);m.w(e+0x6a,0);m.b(e+0x77,index);m.b(e+0x6d,m.b(source+4));m.b(e+0x6e,255);m.b(e+0x76,m.b(cell+6));m.b(e+0x72,m.b(source+6));m.b(e+0x6c,0);
    for(auto pair:std::initializer_list<std::pair<unsigned,unsigned>>{{0x40,10},{0x50,12},{0x5e,14},{0x48,16},{0x4a,18}})m.w(e+pair.first,m.w(source+pair.second));
    m.b(e+0x71,0);for(unsigned a:{0x4cu,0x30u,0x32u,0x34u})m.w(e+a,0);
    auto kind=m.b(e+0x74);if(kind==2)m.w(e+0x5e,m.w(e+0x5e)+20);
    if(kind<16||kind>19){if(m.s(e+0x40)<256)m.w(e+0x40,256);if(m.s(e+0x40)>2048)m.w(e+0x40,1792);if(m.s(e+0x4a)<1536)m.w(e+0x4a,1536);}
    if(m.b(e+0x6d)<8){m.w(e+0x42,m.b(e+0x6d)*512);m.w(e+0x44,m.w(e+0x42));}
    auto call=[&](unsigned address,unsigned a,unsigned b=0,unsigned c=0){return f(m,address,a,b,c).eax;};auto task=[&](unsigned callback){return call(0x12364,50,callback);};auto animation=[&](unsigned n){call(0x2ee40,e,n);};
    unsigned t=0;
    const unsigned animations[]={0,0x9c1d4,0x9c204,0x9c474,0x9c50c,0x9c71c,0x9c294,0x9c78c,0x9c324,0x9c37c,0x9c81c,0x9c60c,0x9c59c,0x9c67c,0x9c3fc};
    const unsigned resources[]={0,0x234410,0x23440c,0x23442c,0x234408,0x234434,0x2343fc,0x234420,0x2343fc,0x234434,0x234428,0x234438,0x234430,0x234424,0x23443c};
    const unsigned values[]={0,0xa00,0x858,0x790,0x900,0xa00,0xa00,0xb00,0xa00,0xa00,0x834,0x800,0x880,0x900,0x900};
    if(kind>=1&&kind<=14){
        m.d(e+0x7c,animations[kind]);m.d(e+0xa4,0);m.w(e+0xb4,values[kind]);m.w(e+0xb6,0);m.w(e+8,kind==7?384:256);m.w(e+10,kind==7?1536:kind==8||kind==9?256:kind<=4||kind==10?512:768);m.w(e+12,kind==7?384:256);m.d(e+0xa0,m.d(resources[kind]));
        if(kind==1){t=task(0x35088);m.d(t+12,0);m.d(t+8,e);m.w(e+0x48,3072);m.w(e+0x4a,2048);animation(0);}
        else {
            m.w(e+0x46,512);if(kind==8||kind==9)m.b(e+0x6c,m.b(e+0x6c)|8);
            if(kind==7){m.d(e+0xa4,m.d(0x94bc8)+12);t=task(0x355d4);m.d(t+12,0);m.d(t+8,e);animation(8);}
            else if(kind==10){
                constexpr int dx[]={0,1,1,1,0,-1,-1,-1},dz[]={-1,-1,0,1,1,1,0,-1};auto facing=m.b(e+0x6d);int distance=m.s(e+0x50);if(facing<8){m.w(e,m.w(e)+dx[facing]*distance);m.w(e+4,m.w(e+4)+dz[facing]*distance);}animation(12);m.w(e+2,m.w(e+0x5a)+64);t=task(0x35ebc);if(t){m.d(t+8,e);auto random=nextGameRandom(m);m.d(t+16,0);m.d(t+20,0);m.d(t+12,random&5);}
            }else {if(kind==14)m.w(e+0x5e,static_cast<unsigned>(m.s(e+0x5e)>>1));t=task(kind==13&&m.b(0x9e0a8)?0x34c78:0x33cc8);m.d(t+8,e);if(kind==13&&m.b(0x9e0a8)){updateEnemyFacing(m,e,f);animation(0);call(0x2840c,e+128);m.w(0x2365e8,0);m.d(0x2365d0,0xffffffff);m.w(0x2365ea,1);}}
        }
    }else if(kind>=16&&kind<=19){
        t=task(kind==16?0x35488:kind==17?0x354d0:kind==18?0x35550:0x35510);m.w(e+8,256);m.w(e+10,768);m.w(e+12,256);
        if(kind<=17){auto amount=m.b(e+0x72);m.w(e+0x4e,0);m.b(e+0x72,255);m.w(e+2,m.w(e+2)+amount*32u);}
        else {int amount=std::bit_cast<std::int8_t>(m.b(e+0x72));m.w(e+2,static_cast<unsigned>(amount<0?m.s(e+0x5c)-(10-amount)*32:m.s(e+0x5a)+amount*32));m.w(e+0x4e,0);}
        m.w(e+0x42,(m.w(e+0x42)+2048)&4095);if(t){m.d(t+12,0);m.d(t+16,0xffffffff);m.d(t+20,0);m.d(t+8,e);}
    }
    if(!t){m.d(e+200,m.d(0x2365d8));m.d(0x2365d8,e);return 0;}
    auto previous=m.d(0x2365e0);if(previous)m.d(previous+204,e);m.d(e+204,0);m.d(e+200,previous);m.d(0x2365e0,e);m.d(e+0xc0,t);return e;
}
}
