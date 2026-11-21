#include "ProjectileRuntime.h"
#include "WorldMovement.h"
#include <vector>
#include <bit>

namespace trilogy::original {
namespace {using Mem=PlayerControlMemory;using Service=PlayerControlService;
constexpr unsigned Free=0x236308,Active=0x236300;
}
std::uint32_t nextGameRandom(Mem m) {
    auto old=m.d(0xa09c0),sum=old+m.d(0xa09b4);
    if(sum<old)++sum;
    auto result=sum+1u+m.d(0xa09c4);m.d(0xa09b0,result);
    // These forward overlapping stores really do propagate the new value.
    for(unsigned i=0;i<5;++i)m.d(0xa09b4+i*4,m.d(0xa09b0+i*4));
    return result;
}
void releaseProjectile(Mem m,unsigned p){auto head=m.d(Free);m.d(Free,p);m.d(p+0x24,head);}
void resetProjectilePool(Mem m) {
    auto p=m.d(0x236304);unsigned previous=0;m.d(Active,0);
    for(unsigned i=0;i<128;++i,p+=44) {
        m.w(p+0x18,0);m.b(p+0x1c,0);m.b(p+0x1e,0);m.b(p+0x20,1);
        m.d(p,0x235700+i*8);m.d(p+4,0x235f00+i*8);m.d(p+0x24,previous);previous=p;
    }
    m.d(Free,previous);
}
std::uint32_t acquireProjectile(Mem m){auto p=m.d(Free);if(p)m.d(Free,m.d(p+0x24));return p;}
void activateProjectile(Mem m,unsigned p) {
    auto head=m.d(Active);if(head)m.d(head+0x28,p);m.d(p+0x28,0);m.d(Active,p);m.d(p+0x24,head);
}
void unlinkProjectile(Mem m,unsigned p,const Service& f) {
    if(m.b(p+0x1e)==6||m.b(p+0x1e)==15)f(m,0x44fa0,p,0,0);
    auto next=m.d(p+0x24),previous=m.d(p+0x28);
    if(next)m.d(next+0x28,previous);
    if(previous)m.d(previous+0x24,next);else m.d(Active,next);
    m.d(p+0x28,0);m.d(p+0x24,0);
}
void damageMapObject(Mem m,std::uint8_t index,std::uint16_t damage,const Service& f) {
    auto p=m.d(0x2367fc)+index*40u;if(m.b(p+0x19)&1)return;
    m.w(p+0x1a,m.w(p+0x1a)-damage);if(m.s(p+0x1a)>0)return;
    m.b(p+0x19,m.b(p+0x19)|1);auto random=nextGameRandom(m);m.b(p+0x1f,(random&7)+1);
    if(m.b(0x9e0a8)&&m.b(0x9e0a9)){m.b(0x2365f4+index,0);f(m,0x5287c,0,0,0);}
}
std::uint32_t collideProjectileWithMap(Mem m,unsigned p,const Service& f) {
    constexpr unsigned blocked=0xffffffff;auto pos=m.d(p),cell=m.d(p+12);
    auto x=static_cast<unsigned>(m.s(pos))+m.w(0xa0c34),z=static_cast<unsigned>(m.s(pos+4))+m.w(0xa0c36);
    auto attr=m.b(cell+10),occupant=m.b(cell+12),kind=m.b(p+0x1e);
    auto hit=[&](unsigned damage){damageMapObject(m,m.b(cell+4),static_cast<std::uint16_t>(damage),f);};
    // L211 has authored barrels in solid wall cells beside the secret exit.
    // Their exposed model can be shot, but the backing wall still blocks a miss.
    if(attr==0x14&&occupant!=0x17)return blocked;
    if(occupant==0x15){if(kind==4)hit(m.w(p+0x1a));return blocked;}
    if(occupant==0x1d){if(kind<=2||kind==4)hit(m.w(p+0x1a));return blocked;}
    if(occupant==0x1c){if(kind<=2||kind==4)hit(m.w(p+0x1a));else if(kind==5)hit(m.w(p+0x1a)<<4);return blocked;}
    auto width=m.w(0xa0c30),height=m.w(0xa0c32);std::vector<LevelCell> cells(static_cast<unsigned>(width)*height);
    auto data=m.d(0xa0c40);for(unsigned i=0;i<cells.size();++i)for(unsigned j=0;j<16;++j)cells[i].bytes[j]=m.b(data+i*16+j);
    LevelGrid grid{width,height,cells};auto floor=static_cast<unsigned>(floorHeight(grid,x,z));auto y=static_cast<unsigned>(m.s(pos+2));
    if(occupant>=0x14&&occupant<0xff&&floor<=y&&y<=floor+(occupant==0x19?0x400u:0x200u)) {
        if(kind!=0||occupant!=0x17)hit(m.w(p+0x1a));return blocked;
    }
    if(attr==0x14)return blocked;
    if(attr==9&&y<floor+0x180u)return blocked;
    if(attr==8&&y<floor+0x100u)return blocked;
    if(y<floor||static_cast<unsigned>(ceilingHeight(grid,x,z))<y)return blocked;
    return 0;
}
std::uint32_t createPlayerShot(Mem m,std::uint16_t speed,std::uint8_t kind,const Service& f,std::uint16_t priorStackYaw) {
    auto p=acquireProjectile(m);if(!p)return 0;
    auto pos=m.d(p),vel=m.d(p+4);m.b(p+0x20,0);m.b(p+0x1e,kind);
    for(unsigned i=0;i<3;++i)m.w(pos+i*2,m.w(0xa0a20+i*2)-(i==1?0x60u:0u));
    auto read=[&](unsigned a){return Vec3s{static_cast<std::int16_t>(m.s(a)),static_cast<std::int16_t>(m.s(a+2)),static_cast<std::int16_t>(m.s(a+4))};};
    auto write=[&](unsigned a,const Vec3s& v){for(unsigned i=0;i<3;++i)m.w(a+i*2,v[i]);};
    std::array<std::int16_t,4096> sine;for(unsigned i=0;i<4096;++i)sine[i]=static_cast<std::int16_t>(m.s(0x9763c+i*2));
    auto rotate=[&](const Vec3s& angles,const Vec3s& v){
        auto matrix=buildRotation(angles,sine);for(unsigned i=0;i<9;++i)m.w(0xa0a28+i*2,matrix[i]);
        return rotateVector(angles,v,sine);
    };
    auto yaw=priorStackYaw;
    if(kind<=2) {
        constexpr unsigned lifetimes[]={20,16,28},damage[]={16,80,96};
        m.b(p+0x1c,lifetimes[kind]);m.w(p+0x1a,damage[kind]);yaw=kind==2?48:40;
        write(vel,Vec3s{static_cast<std::int16_t>(kind==0?40:32),0,0});
        auto v=rotate(read(0xa0ba0),read(vel)),base=read(pos);
        for(unsigned i=0;i<3;++i)m.w(pos+i*2,static_cast<unsigned>(base[i])+v[i]);
    } else if(kind==4){m.w(pos+2,m.w(pos+2)-0xa0u);m.b(p+0x1c,28);m.w(p+0x1a,160);yaw=0;}
    else if(kind!=3){m.b(p+0x1c,20);m.w(p+0x1a,16);yaw=40;}
    m.b(p+0x1d,m.b(p+0x1c));
    Vec3s local{static_cast<std::int16_t>((0u-m.w(0xa0ba8))&4095),std::bit_cast<std::int16_t>(yaw),0};
    write(vel,Vec3s{0,0,std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(speed+0x100u))});
    write(vel,rotate(local,read(vel)));write(vel,rotate(read(0xa0ba0),read(vel)));
    auto setCell=[&](){
        auto x=(static_cast<unsigned>(m.s(pos))+m.w(0xa0c34))>>9;
        auto z=(static_cast<unsigned>(m.s(pos+4))+m.w(0xa0c36))>>9;
        // The original deliberately/accidentally accepts equality here.
        if(x>m.w(0xa0c30)||z>m.w(0xa0c32))return false;
        m.d(p+12,m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16);return true;
    };
    if(!setCell()){releaseProjectile(m,p);return 0;}
    if(collideProjectileWithMap(m,p,f)){releaseProjectile(m,p);return 0xffffffff;}
    for(unsigned i=0;i<3;++i)m.w(pos+i*2,m.w(pos+i*2)+m.w(vel+i*2));
    if(!setCell()){releaseProjectile(m,p);return 0;}
    if(collideProjectileWithMap(m,p,f)){releaseProjectile(m,p);return 0xffffffff;}
    activateProjectile(m,p);return 0xffffffff;
}
PlayerControlService withProjectileRuntime(const Service& external) {
    return [external](Mem& m,unsigned address,unsigned a,unsigned b,unsigned c)->PlayerControlResult {
        switch(address) {
            case 0x29b50:rewindProjectile(m,a);break;
            case 0x29f18:return {collideProjectileWithEntities(m,a,external),0};
            case 0x2a1e8:return {collideProjectileWithPlayer(m,a,external),0};
            case 0x29bc8:return {collideBallisticWithMap(m,a,external),0};
            case 0x30dc4:damageEntity(m,a,static_cast<std::uint16_t>(b),static_cast<std::uint16_t>(c),external);break;
            case 0x2a860:emitEntityHitEffect(m,a,b,external);break;
            case 0x2d234:updateProjectiles(m,a,external);break;
            case 0x100ec:return {nextGameRandom(m),0};
            case 0x2a2bc:resetProjectilePool(m);break;
            case 0x2a32c:return {acquireProjectile(m),0};
            case 0x2a2a4:releaseProjectile(m,a);break;
            case 0x2a348:activateProjectile(m,a);break;
            case 0x2a36c:unlinkProjectile(m,a,external);break;
            case 0x3687c:damageMapObject(m,static_cast<std::uint8_t>(a),static_cast<std::uint16_t>(b),external);break;
            case 0x29d38:return {collideProjectileWithMap(m,a,external),0};
            case 0x2ae64:if(static_cast<std::uint8_t>(b)!=3)return {createPlayerShot(m,static_cast<std::uint16_t>(a),static_cast<std::uint8_t>(b),external,0),0};
                return external(m,address,a,b,c);
            default:return external(m,address,a,b,c);
        }
        return {};
    };
}
}
