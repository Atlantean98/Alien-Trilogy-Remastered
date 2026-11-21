#include "WorldMovement.h"
#include <bit>

namespace trilogy::original {
namespace {
std::int16_t word(std::int32_t n) {return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}
std::int32_t wrapped(std::uint32_t n) {return std::bit_cast<std::int32_t>(n);}
std::int32_t product(std::int32_t a,std::int32_t b) {return wrapped(static_cast<std::uint32_t>(a)*static_cast<std::uint32_t>(b));}
std::int32_t sum(std::int32_t a,std::int32_t b) {return wrapped(static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b));}
bool blocked(const LevelCell& c) {return (c.bytes[7]&0x80)||c.bytes[12];}
}
Matrix3s buildRotation(const Vec3s& a,std::span<const std::int16_t,4096> t) {
    auto sn=[&](int n){return std::int32_t(t[static_cast<unsigned>(n)&4095]);};
    auto s0=sn(a[0]),c0=sn(a[0]+1024),s1=sn(a[1]),c1=sn(a[1]+1024),s2=sn(a[2]),c2=sn(a[2]+1024);
    auto p=std::int32_t(word(product(s1,s0)>>12));
    auto q=std::int32_t(word(product(c0,s1)>>12));
    return {word(product(c1,c2)>>12),word(product(-c1,s2)>>12),word(-s1),
        word(sum(product(c0,s2),-product(p,c2))>>12),word(sum(product(p,s2),product(c0,c2))>>12),word(product(-s0,c1)>>12),
        word(sum(product(q,c2),product(s0,s2))>>12),word(sum(product(c2,s0),-product(s2,q))>>12),word(product(c0,c1)>>12)};
}
std::array<std::int32_t,3> transformVector(const Matrix3s& m,const Vec3s& v) {
    std::array<std::int32_t,3> out{};
    for(unsigned i=0;i<3;++i) out[i]=sum(sum(product(m[i*3],v[0]),product(m[i*3+1],v[1])),product(m[i*3+2],v[2]))>>12;
    return out;
}
Vec3s rotateVector(const Vec3s& a,const Vec3s& v,std::span<const std::int16_t,4096> t) {
    auto r=transformVector(buildRotation(a,t),v);return {word(r[0]),word(r[1]),word(r[2])};
}
Vec3s addRotatedVector(const Vec3s& a,const Vec3s& v,const Vec3s& base,std::span<const std::int16_t,4096> t) {
    auto r=transformVector(buildRotation(a,t),v);return {word(r[0]+base[0]),word(r[1]+base[1]),word(r[2]+base[2])};
}
std::int32_t floorHeight(const LevelGrid& g,std::uint32_t x,std::uint32_t z) {
    if((x>>9)>=g.width || (z>>9)>=g.height) return 0;
    auto& c=g.cell(x,z);auto base=std::int32_t(c.bytes[11])*32;
    auto lx=x&511u,lz=z&511u;
    switch(c.bytes[10]) {
        case 0x2d:if(lz>256) return base+128;break;
        case 0x2e:if(lx<=256) return base+128;break;
        case 0x2f:if(lx>256) return base+128;break;
        case 0x30:if(lz<=256) return base+128;break;
        case 0x31:case 0x35:return base+(lz>>2);
        case 0x32:case 0x36:return base+((512-lx)>>2)-128;
        case 0x33:case 0x37:return base+(lx>>2);
        case 0x34:case 0x38:return base+((512-lz)>>2)-128;
    }
    return base;
}
std::int32_t ceilingHeight(const LevelGrid& g,std::uint32_t x,std::uint32_t z) {
    if((x>>9)>=g.width || (z>>9)>=g.height) return 0;
    return std::int32_t(g.cell(x,z).bytes[13])*32;
}
std::uint32_t clipDiagonalX(const LevelGrid& g,std::uint32_t x,std::uint32_t z) {
    auto local=x&511u;auto line=z&511u;auto base=x&~511u;
    switch(g.cell(x,z).bytes[10]) {
        case 0x19:line=511-line;if(local<=line)return base+line+1;break;
        case 0x1a:if(line<=local)return base+line-1;break;
        case 0x1b:if(local<=line)return base+line+1;break;
        case 0x1c:line=511-line;if(line<=local)return base+line-1;break;
    }return x;
}
std::uint32_t clipDiagonalZ(const LevelGrid& g,std::uint32_t x,std::uint32_t z) {
    auto local=z&511u;auto line=x&511u;auto base=z&~511u;
    switch(g.cell(x,z).bytes[10]) {
        case 0x19:line=511-line;if(local<=line)return base+line+1;break;
        case 0x1a:if(local<=line)return base+line+1;break;
        case 0x1b:if(line<=local)return base+line-1;break;
        case 0x1c:line=511-line;if(line<=local)return base+line-1;break;
    }return z;
}
std::uint32_t wallFallbackX(const LevelGrid& g,std::uint32_t x,std::uint32_t z,std::uint32_t tested,bool negative) {
    auto result=(tested&~511u)+(negative?712u:std::uint32_t(-201));
    return blocked(g.cell(result,z))?x:result;
}
std::uint32_t wallFallbackZ(const LevelGrid& g,std::uint32_t x,std::uint32_t z,std::uint32_t tested,bool negative) {
    auto result=(tested&~511u)+(negative?712u:std::uint32_t(-201));
    return blocked(g.cell(x,result))?z:result;
}
namespace {
std::uint32_t collide(const LevelGrid& g,std::uint32_t x,std::uint32_t z,Vec3s& d,std::uint8_t flags,bool zAxis) {
    auto original=zAxis?z:x,other=zAxis?x:z;
    auto delta=d[zAxis?2:0];bool negative=delta<0;
    auto required=word(std::int32_t(d[1])+768);
    auto tested=original+std::uint32_t(std::int32_t(delta))+(negative?std::uint32_t(-200):200u);
    auto fallback=[&](){return zAxis?wallFallbackZ(g,x,z,tested,negative):wallFallbackX(g,x,z,tested,negative);};
    for(int probe=0;probe<3;++probe) {
        auto cross=other+(probe==1?std::uint32_t(-200):probe==2?200u:0u);
        auto px=zAxis?cross:tested,pz=zAxis?tested:cross;
        auto floor=word(floorHeight(g,px,pz));
        auto& cell=g.cell(px,pz);auto attribute=cell.bytes[10];
        if(blocked(cell) && !(flags&1))return fallback();
        if(attribute<20 || (attribute>24 && attribute<45)) {
            if(word(ceilingHeight(g,px,pz))<required)return fallback();
            if(flags&8)d[1]=floor;
            if(d[1]<floor && std::int32_t(floor)-d[1]>256)return fallback();
        }
        tested=zAxis?clipDiagonalZ(g,px,pz):clipDiagonalX(g,px,pz);
    }
    auto desired=original+std::uint32_t(std::int32_t(delta));
    if((!negative && tested>desired)||(negative && tested<desired))tested=desired;
    auto floor=word(floorHeight(g,zAxis?other:tested,zAxis?tested:other));
    if(floor<d[1] && std::int32_t(d[1])-floor>256) {
        auto edge=(tested&~511u)+(negative?312u:201u);
        if((!negative && tested<edge)||(negative && tested>edge))tested=edge;
    }return tested;
}
}
std::uint32_t collideX(const LevelGrid& g,std::uint32_t x,std::uint32_t z,Vec3s& d,std::uint8_t f){return collide(g,x,z,d,f,false);}
std::uint32_t collideZ(const LevelGrid& g,std::uint32_t x,std::uint32_t z,Vec3s& d,std::uint8_t f){return collide(g,x,z,d,f,true);}

void movePlayer(WorldPlayerState& s,LevelGrid& g,std::span<const std::int16_t,4096> trig,const WorldService& invoke) {
    g.cells[s.currentCell].bytes[12]=0;
    s.scratchRotation=buildRotation(s.moveAngles,trig);
    auto d=rotateVector(s.moveAngles,s.localMovement,trig);
    auto speed=s.localMovement[2];
    auto conveyorSpeed=std::int16_t(speed==0?128:speed<32?32:(s.movementFlags&2)?word(std::int32_t(speed)-32):speed);
    // On run mode the speed<32 case still yields 32; this mirrors the original branches.
    Vec3s auxiliary{0,0,conveyorSpeed};
    auto attr=g.cells[s.currentCell].bytes[10];
    if(attr>=0x35 && attr<=0x38) {
        std::int16_t yaw=attr==0x35?2048:attr==0x36?-1024:attr==0x37?1024:0;
        s.scratchRotation=buildRotation({0,yaw,0},trig);
        d=addRotatedVector({0,yaw,0},auxiliary,d,trig);
    }
    if(s.impulse) {
        s.scratchRotation=buildRotation({0,word(s.impulseYaw),2048},trig);
        d=addRotatedVector({0,word(s.impulseYaw),2048},{0,0,word(s.impulse)},d,trig);
        s.impulse=static_cast<std::uint16_t>(s.impulse-8u);
        if(word(s.impulse)<0)s.impulse=0;
    }
    auto x=std::uint32_t(std::int32_t(s.position[0]))+s.originX;
    auto z=std::uint32_t(std::int32_t(s.position[2]))+s.originZ;
    if(d[0]||d[2]) {
        d[1]=word(floorHeight(g,x,z));
        if(d[0])x=collideX(g,x,z,d,s.collisionFlags);
        if(d[2])z=collideZ(g,x,z,d,s.collisionFlags);
        s.position[0]=word(x-s.originX);s.position[2]=word(z-s.originZ);
        x=std::uint32_t(std::int32_t(s.position[0]))+s.originX;
        z=std::uint32_t(std::int32_t(s.position[2]))+s.originZ;
        auto old=s.region;s.region=g.cell(x,z).bytes[6];
        if(old!=s.region){invoke(0x27b40,0,0,s,g);invoke(0x27b6c,s.region,0,s,g);}
    }
    x=std::uint32_t(std::int32_t(s.position[0]))+s.originX;
    z=std::uint32_t(std::int32_t(s.position[2]))+s.originZ;
    auto previous=s.position[1];s.position[1]=word(floorHeight(g,x,z));
    s.terrainFlags&=0xbf;
    auto index=g.index(x,z);
    if(previous==s.position[1]) {
        auto terrain=g.cells[index].bytes[10];
        if(!s.terrainGate&&!s.damageGate) {
            if(terrain==7||terrain==8||terrain==12) {
                invoke(0x3dde8,terrain==7?5:terrain==8?10:1,5,s,g);s.terrainFlags|=0x40;
            } else if(terrain==10){s.damageCooldown=0;s.damageGate=0;invoke(0x3dd24,std::uint32_t(std::int32_t(word(s.health))),5,s,g);}
        } else if(terrain==10){s.damageCooldown=0;s.damageGate=0;invoke(0x3dd24,std::uint32_t(std::int32_t(word(s.health))),5,s,g);}
    }
    s.currentCell=index;
    auto use=static_cast<std::uint32_t>(std::int32_t(word(s.useMask)))&s.pressedInput;
    invoke(0x408b0,use,0,s,g);
    g.cells[index].bytes[12]=255;
}
void playerTickWithWorld(PlayerTickState& tick,WorldPlayerState& player,LevelGrid& grid,TaskScheduler& scheduler,
    TaskAddress address,std::span<const std::int16_t,4096> trig,const WorldService& worldService,const InvokePlayerService& other) {
    auto services=[&](std::uint32_t routine,PlayerTickState& state,TaskScheduler& tasks,TaskAddress task) {
        if(routine==0x3d930) {
            player.position[0]=word(state.packedXY&0xffffu);
            player.position[1]=word(state.packedXY>>16);
            player.localMovement={word(state.movement0),word(state.movement1),word(state.movement2)};
            movePlayer(player,grid,trig,worldService);
            state.packedXY=std::uint16_t(player.position[0])|(std::uint32_t(std::uint16_t(player.position[1]))<<16);
        } else other(routine,state,tasks,task);
    };
    playerGroundedTick(tick,scheduler,address,services);
    player.position[0]=word(tick.packedXY&0xffffu);player.position[1]=word(tick.packedXY>>16);
    player.localMovement={word(tick.movement0),word(tick.movement1),word(tick.movement2)};
}
}
