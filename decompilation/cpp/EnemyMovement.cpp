#include "EnemyMovement.h"
#include "WorldMovement.h"
#include <vector>
#include <algorithm>
#include <bit>
#include <cstdlib>
namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;
int word(unsigned n){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}
struct GridCopy {
    std::vector<LevelCell> cells;LevelGrid grid;
    GridCopy(Mem m):cells(m.w(0xa0c30)*unsigned(m.w(0xa0c32))),grid{m.w(0xa0c30),m.w(0xa0c32),cells}{
        std::memcpy(cells.data(),m.bytes.data()+m.d(0xa0c40),cells.size()*sizeof(LevelCell));
    }
};
unsigned fallback(unsigned current,unsigned tested,int velocity,bool negative){
    auto candidate=current+static_cast<unsigned>(velocity),edge=(tested&0xfffffe00u)+(negative?712u:0u-201u);
    return negative?(edge<candidate?candidate:edge):(edge>candidate?candidate:edge);
}
unsigned collide(Mem m,unsigned e,unsigned x,unsigned z,int y,bool axisZ,const LevelGrid& grid,bool preciseCrowd=false,bool allowQueenOverlap=false){
    int velocity=m.s(e+(axisZ?0x34:0x30));bool negative=velocity<0,ceiling=m.b(e+0x6c)&8;
    auto current=axisZ?z:x,candidate=current+static_cast<unsigned>(velocity),leading=candidate+(negative?0u-200u:200u),across=axisZ?x:z;
    auto reject=[&]{
        const auto edge=fallback(current,leading,velocity,negative);
        return preciseCrowd?std::clamp(edge,std::min(current,candidate),std::max(current,candidate)):edge;
    };
    auto sample=[&](int offset,bool top){auto a=across+static_cast<unsigned>(offset);return word(static_cast<unsigned>(top?ceilingHeight(grid,axisZ?a:leading,axisZ?leading:a):floorHeight(grid,axisZ?a:leading,axisZ?leading:a)));};
    for(int offset:{-200,0,200}){auto a=across+static_cast<unsigned>(offset);auto cell=m.d(0xa0c40)+grid.index(axisZ?a:leading,axisZ?leading:a)*16;auto attr=m.b(cell+10);if((attr>=20&&attr<45)||(m.b(cell+12)&&(!preciseCrowd||m.b(cell+12)==1||
                m.b(cell+12)==10||m.b(cell+12)>14)))return reject();}
    if(preciseCrowd&&m.b(e+0x6f)<7){
        // Cell occupancy covers a whole tile. Use actual actor separation so
        // enemies can queue and move past each other along narrow approaches.
        for(auto other=m.d(0x2365e0);other;other=m.d(other+200)){
            const auto kind=m.b(other+0x74);
            if(other==e||kind<2||kind==10||kind>14||m.s(other+0x58)<=0)continue;
            if(allowQueenOverlap&&kind==7)continue;
            if(std::abs(y-m.s(other+2))>=std::max(m.s(e+10),m.s(other+10)))continue;
            const int ox=m.s(other)+m.w(0xa0c34),oz=m.s(other+4)+m.w(0xa0c36);
            const int dx=int(x)-ox,dz=int(z)-oz;
            const int nextX=dx+(axisZ?0:velocity),nextZ=dz+(axisZ?velocity:0);
            const int clearance=(m.s(e+8)+m.s(other+8))/2;
            const auto before=1LL*dx*dx+1LL*dz*dz,after=1LL*nextX*nextX+1LL*nextZ*nextZ;
            if(after<clearance*clearance&&after<before)return current;
        }
    }
    int limit=ceiling?512:256,heights[3],i=0;
    for(int offset:{-200,0,200}){int h=sample(offset,ceiling);heights[i++]=h;
        // The ceiling branch really compares a negative difference to +512.
        if((ceiling?h<y:h>y)&&h-y>limit)return reject();
    }
    int first=heights[0]>heights[1]?heights[0]:heights[1];
    if(std::abs(first-heights[2])>limit)return reject();
    int extent=word(static_cast<unsigned>(y+(ceiling?-m.s(e+10):m.s(e+10))));
    if(!ceiling)for(int offset:{-200,0,200})if(sample(offset,true)<extent)return reject();
    auto floor=word(static_cast<unsigned>(floorHeight(grid,axisZ?x:candidate,axisZ?candidate:z)));
    if(floor<m.s(e+0x5a)&&m.s(e+0x5a)-floor>limit){
        if(!m.w(e+0x32))m.w(e+0x32,60);
        auto edge=(candidate&0xfffffe00u)+(negative?312u:200u);
        if(negative?edge<candidate:edge>candidate)return edge;
    }
    return candidate;
}
}
unsigned enemyWallFallbackX(Mem m,unsigned x,unsigned tested,unsigned movement,bool negative){return fallback(x,tested,m.s(movement),negative);}
unsigned enemyWallFallbackZ(Mem m,unsigned z,unsigned tested,unsigned movement,bool negative){return fallback(z,tested,m.s(movement+4),negative);}
unsigned collideEnemyX(Mem m,unsigned e,unsigned x,unsigned z,std::int16_t y){GridCopy g(m);return collide(m,e,x,z,y,false,g.grid);}
unsigned collideEnemyZ(Mem m,unsigned e,unsigned x,unsigned z,std::int16_t y){GridCopy g(m);return collide(m,e,x,z,y,true,g.grid);}
void moveEnemy(Mem m,unsigned e,const PlayerControlService& f,bool preciseCrowd,bool allowQueenOverlap){
    int wx=m.s(e)+m.w(0xa0c34),wz=m.s(e+4)+m.w(0xa0c36);if(wx<0||wz<0)return;
    unsigned x=wx,z=wz;GridCopy g(m);
    m.w(e+0x5a,static_cast<unsigned>(floorHeight(g.grid,x,z)));m.w(e+0x5c,static_cast<unsigned>(ceilingHeight(g.grid,x,z)));
    int y=word(m.w(e+0x5a)+32);if(m.b(e+0x6c)&8)y=word(m.w(e+0x5c)-32);
    if(m.w(e+0x32)){
        y=word(m.w(e+2)+m.w(e+0x32));
        if(y>m.s(e+0x5c)-m.s(e+10)){m.w(e+0x32,0);y=word(m.w(e+0x5c)-m.w(e+10));}
        else if(y<m.s(e+0x5a)+32){m.w(e+0x32,0);y=word(m.w(e+0x5a)+32);}
        else{m.w(e+0x32,m.w(e+0x32)-12);if(!m.w(e+0x32))m.w(e+0x32,0xfff4);}
    }
    if(m.b(e+0x6f)<7&&(!preciseCrowd||m.b(m.d(e+0x78)+4)==m.b(e+0x70)))
        m.b(m.d(e+0x78)+12,0);
    // Collision reads occupancy directly from memory. Clearing it does not change the height snapshot.
    if(m.w(e+0x30))x=collide(m,e,x,z,y,false,g.grid,preciseCrowd,allowQueenOverlap);
    if(m.w(e+0x34))z=collide(m,e,x,z,y,true,g.grid,preciseCrowd,allowQueenOverlap);
    m.w(e,x-m.w(0xa0c34));m.w(e+4,z-m.w(0xa0c36));m.w(e+2,static_cast<unsigned>(y));
    auto cell=m.d(0xa0c40)+g.grid.index(x,z)*16;
    if(cell!=m.d(e+0x78)){m.d(e+0x78,cell);m.b(e+0x76,m.b(cell+6));if(m.b(e+0x6f)!=8)f(m,0x40b5c,e,0,0);}
    if(m.b(e+0x6f)<7){if(m.b(0x9e0a8)&&(m.w(e+0xc4)==10||m.w(e+0xc4)==11))m.b(cell+12,0);else{m.b(cell+12,m.b(e+0x74));m.b(cell+4,m.b(e+0x70));}}
}
PlayerControlService withEnemyMovement(const PlayerControlService& f){return [f](Mem& m,unsigned routine,unsigned a,unsigned b,unsigned c){if(routine==0x31950){moveEnemy(m,a,f);return PlayerControlResult{};}return f(m,routine,a,b,c);};}
}
