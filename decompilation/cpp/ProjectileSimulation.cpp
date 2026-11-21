#include <cmath>
#include <algorithm>
#include <cstdlib>
#include "ProjectileRuntime.h"
#include "WorldMovement.h"
#include <bit>
#include <vector>

namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;using Service=PlayerControlService;
unsigned sx(int value){return static_cast<unsigned>(value);}
int signed32(unsigned value){return std::bit_cast<std::int32_t>(value);}
int shortWord(unsigned value){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));}
int distance(unsigned a,unsigned b){int d=signed32(a-b);return d<0?signed32(0u-sx(d)):d;}
PlayerControlResult effect(Mem m,const Service& f,unsigned address,unsigned a=0,unsigned b=0,unsigned c=0){return f(m,address,a,b,c);}
int heightAt(Mem m,unsigned x,unsigned z,bool ceiling=false) {
    auto width=m.w(0xa0c30),height=m.w(0xa0c32);std::vector<LevelCell> cells(static_cast<unsigned>(width)*height);
    auto data=m.d(0xa0c40);for(unsigned i=0;i<cells.size();++i)for(unsigned j=0;j<16;++j)cells[i].bytes[j]=m.b(data+i*16+j);
    LevelGrid grid{width,height,cells};return ceiling?ceilingHeight(grid,x,z):floorHeight(grid,x,z);
}
void blastMap(Mem m,const Service& f,unsigned p){effect(m,f,0x362a8,m.d(p),m.d(p+12));}
void explosion(Mem m,const Service& f,unsigned p,unsigned lift=0){auto pos=m.d(p);effect(m,f,0x2a3e0,sx(m.s(pos)),sx(shortWord(m.w(pos+2)+lift)),sx(m.s(pos+4)));}
void impact(Mem m,const Service& f,unsigned p){auto attr=m.b(m.d(p+12)+10);effect(m,f,0x2a4e8,p,attr==9?1:attr==8?2:0);}
void stop(Mem m,unsigned p){auto v=m.d(p+4);m.w(v,0);m.w(v+2,0);m.w(v+4,0);}
void recycle(Mem m,const Service& f,unsigned p){unlinkProjectile(m,p,f);releaseProjectile(m,p);}
}
void advanceVectorBatch(Mem m,unsigned positions,unsigned velocities,std::int32_t count,std::int32_t scale) {
    for(int i=0;i<count;++i)for(unsigned j=0;j<3;++j){
        auto pos=positions+i*8+j*2,vel=velocities+i*8+j*2;
        auto delta=signed32(sx(m.s(vel))*sx(scale))>>12;
        m.w(pos,sx(m.s(pos))+sx(delta));
    }
}
void rewindProjectile(Mem m,unsigned p) {
    auto pos=m.d(p),vel=m.d(p+4);
    for(unsigned i=0;i<3;++i)m.w(pos+i*2,m.w(pos+i*2)-m.w(vel+i*2));
    auto x=(sx(m.s(pos))+m.w(0xa0c34))>>9,z=(sx(m.s(pos+4))+m.w(0xa0c36))>>9;
    m.d(p+12,m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16);
}
void damageEntity(Mem m,unsigned e,std::uint16_t damage,std::uint16_t lethalFlags,const Service& f) {
    if((m.w(e+0x4e)||m.b(e+0x6f)==6)&&!m.b(0x9e0a8))return;
    if(m.b(e+0x6f)>5&&!m.b(0x9e0a8))return;
    if(m.b(e+0x74)==10&&m.b(e+0x6f)==4)return;
    if(!m.b(0x9e0a8))m.w(e+0x58,m.w(e+0x58)-damage);
    if(m.b(0x9e0a8)){effect(m,f,0x51360,m.b(0x236318),sx(shortWord(damage)));return;}
    if(m.b(e+0x74)==7&&m.s(e+0x58)<=25)effect(m,f,0x560a4,32);
    if(m.s(e+0x58)<=0){
        auto kind=m.b(e+0x74);
        if(kind==2&&(nextGameRandom(m)&1))effect(m,f,0x560a4,30);
        if(kind>=2&&kind<=14&&kind!=7&&kind!=10&&lethalFlags)m.b(e+0x6c,m.b(e+0x6c)|32);
        effect(m,f,0x30518,e);
    }else effect(m,f,0x30ac0,e);
}
void emitEntityHitEffect(Mem m,unsigned e,unsigned p,const Service& f) {
    auto kind=m.b(e+0x74);if(kind<1||kind>14)return;
    if(kind==7&&!(nextGameRandom(m)&7))effect(m,f,0x560a4,4);
    effect(m,f,0x2a6e0,p,kind==13?10:kind>=10?8:9);
}
std::uint32_t collideProjectileWithEntities(Mem m,unsigned p,const Service& f,bool ignoreRegions) {
    if(m.b(p+0x20)||(m.b(p+0x1e)==10&&m.b(0x9e0a8)))return 0;
    auto region=m.w(m.d(p+12)+8);auto pos=m.d(p);
    auto damage=m.s(p+0x1a)*(m.b(p+0x1c)<m.b(p+0x1d)/2?1:2);
    unsigned result=0;std::uint8_t index=255;
    for(auto e=m.d(0x2365e0);e;e=m.d(e+0xc8)) {
        ++index;auto peer=index*60u;
        if(m.b(e+0x74)==7&&m.w(e+0x4e)==PerchedQueenDamageGate)continue;
        if(m.b(0x9e0a8)) {
            if(m.d(0x3f92d0+peer)==0xffffffff||m.d(0x3f94f4)==m.d(0x3f9308+peer)||(m.d(0x3f92fc+peer)&65535)==10)continue;
        }else if(m.b(e+0x6f)>5||(!ignoreRegions&&!(m.w(m.d(e+0x78)+8)&region)))continue;
        if(m.s(e)==-1&&m.s(e+4)==-1)continue;
        if(ignoreRegions){ // Player hit range follows the fully black fog boundary.
            constexpr double angle=6.283185307179586/4096;double yaw=m.s(0xa0ba2)*angle,pitch=-m.s(0xa0ba8)*angle;
            double dx=m.s(e)-m.s(0xa0a48),dy=m.s(e+2)-m.s(0xa0a4a)-768,dz=m.s(e+4)-m.s(0xa0a4c);double depth=dy*std::sin(pitch)+(-dx*std::sin(yaw)+dz*std::cos(yaw))*std::cos(pitch);if(depth>=5100)continue;
        }

        auto dx=distance(sx(m.s(pos)),m.b(0x9e0a8)?m.d(0x3f92f4+peer):sx(m.s(e)));
        auto dy=m.b(0x9e0a8)?0:distance(sx(m.s(pos+2)),sx(m.s(e+2)));
        auto dz=distance(sx(m.s(pos+4)),m.b(0x9e0a8)?m.d(0x3f92f8+peer):sx(m.s(e+4)));
        int scale=m.b(p+0x1e)==1?2:1;
        // Freelook player rounds must hit the visible upper body of wall hosts.
        // Their native symmetric half-height box ends below the eye-height ray.
        // Keep original collision bounds for recovered/enemy projectile paths.
        bool wallHost=ignoreRegions&&!m.b(0x9e0a8)&&m.b(e+0x74)==10;
        int vertical=m.s(pos+2)-m.s(e+2);
        bool outsideHeight=wallHost?(vertical<=-m.s(e+10)||vertical>=1024):dy>=m.s(e+10)*scale;
        if(dx>=m.s(e+8)*scale||outsideHeight||dz>=m.s(e+12)*scale)continue;
        m.b(0x236318,index);auto kind=m.b(p+0x1e);
        bool explosive=kind==11||kind==12||kind==19;
        if(explosive&&m.b(0x9e0a8)) {
            damageEntity(m,e,1024,65535,f);emitEntityHitEffect(m,e,p,f);return 0xffffffff;
        }
        auto amount=explosive&&m.b(e+0x74)!=7?m.s(e+0x58):damage;
        const bool alive=m.s(e+0x58)>0;
        damageEntity(m,e,static_cast<std::uint16_t>(amount),explosive?65535:0,f);
        if(kind==3&&alive&&m.s(e+0x58)<=0)effect(m,f,0x600000,e);
        emitEntityHitEffect(m,e,p,f);
        kind=m.b(p+0x1e);if(kind!=1&&kind!=4)return 0xffffffff;result=0xffffffff;
    }
    return result;
}
std::uint32_t collideProjectileWithPlayer(Mem m,unsigned p,const Service& f) {
    if(m.b(p+0x1e)==10&&m.b(0x9e0a8))return 0;
    auto pos=m.d(p);auto dy=m.s(pos+2)-m.s(0xa0a4a);
    if(dy<0||dy>1024||distance(sx(m.s(pos)),sx(m.s(0xa0a48)))>=256||distance(sx(m.s(pos+4)),sx(m.s(0xa0a4c)))>=256)return 0;
    if(m.b(p+0x1e)==5)effect(m,f,0x3df14,sx(m.s(p+0x1a)),sx(m.s(p+0x12)),3);
    else effect(m,f,0x3dee8,sx(m.s(p+0x1a)),5);
    return 0xffffffff;
}
std::uint32_t collideBallisticWithMap(Mem m,unsigned p,const Service& f) {
    auto pos=m.d(p),cell=m.d(p+12);auto x=sx(m.s(pos))+m.w(0xa0c34),z=sx(m.s(pos+4))+m.w(0xa0c36);
    if(m.b(cell+10)==20){
        // A barrel's exposed body can occupy a wall cell (L211's exit secret).
        // Resolve direct grenade contact before the backing wall stops flight.
        if(m.b(cell+12)==23){auto floor=heightAt(m,x,z),y=m.s(pos+2);if(floor<=y&&y<=floor+512)damageMapObject(m,m.b(cell+4),m.w(p+0x1a),f);}
        rewindProjectile(m,p);blastMap(m,f,p);return 0xffffffff;
    }
    auto occupant=m.b(cell+12);
    if(occupant==21||occupant==29||occupant==28) {
        damageMapObject(m,m.b(cell+4),m.w(p+0x1a),f);
        if(m.b(cell+12)==21){rewindProjectile(m,p);blastMap(m,f,p);}return 0xffffffff;
    }
    if(occupant>=20&&occupant<255) {
        auto floor=sx(heightAt(m,x,z));auto y=sx(m.s(pos+2));
        if(floor<=y&&y<=floor+(m.b(cell+12)==25?1024u:512u)) {
            damageMapObject(m,m.b(cell+4),m.w(p+0x1a),f);rewindProjectile(m,p);blastMap(m,f,p);return 0xffffffff;
        }
    }
    auto ceiling=sx(heightAt(m,x,z,true));
    if(ceiling<sx(m.s(pos+2))){if(m.b(cell+10)!=6)m.w(pos+2,ceiling-256u);return 0xffffffff;}
    return 0;
}
void updateProjectiles(Mem m,unsigned task,const Service& f,bool extendedPlayerRange,const std::function<void(unsigned,bool)>& observer) {
    auto p=m.d(0x236300);
    if((m.b(0x95f9c)&1)&&p) {
        advanceVectorBatch(m,0x235700,0x235f00,128,4096);
        if(m.b(0x9e0a8))advanceVectorBatch(m,0x235700,0x235f00,128,4096);
        while(p) {
            struct Observe {const std::function<void(unsigned,bool)>& fn;unsigned projectile;~Observe(){if(fn)fn(projectile,false);}} observe{observer,p};if(observer)observer(p,true);
            auto next=m.d(p+0x24),pos=m.d(p),vel=m.d(p+4);
            auto x=sx(m.s(pos))+m.w(0xa0c34),z=sx(m.s(pos+4))+m.w(0xa0c36);
            auto cellX=x>>9,cellZ=z>>9;
            if(cellX>m.w(0xa0c30)||cellZ>m.w(0xa0c32)){recycle(m,f,p);p=next;continue;}
            m.d(p+12,m.d(0xa0c40)+(cellZ*m.w(0xa0c30)+cellX)*16);
            auto kind=m.b(p+0x1e);int life=m.b(p+0x1c)-1;
            if(life<=0){if(kind==11||kind==12||kind==13||kind==19){blastMap(m,f,p);explosion(m,f,p);}recycle(m,f,p);p=next;continue;}
            m.b(p+0x1c,life);
            bool remove=false;
            if(extendedPlayerRange&&(kind==0||kind==1||kind==2||kind==4)) {
                // Optional host path: sweep player rounds in short segments,
                // bypassing visibility masks without allowing shots through walls.
                int end[3]={m.s(pos),m.s(pos+2),m.s(pos+4)},delta[3]={m.s(vel),m.s(vel+2),m.s(vel+4)};if(m.b(0x9e0a8))for(auto& d:delta)d*=2;
                unsigned steps=unsigned(std::max({std::abs(delta[0]),std::abs(delta[1]),std::abs(delta[2])})+63)/64;if(!steps)steps=1;
                for(unsigned step=1;step<=steps&&!remove;++step){for(unsigned axis=0;axis<3;++axis)m.w(pos+axis*2,end[axis]-delta[axis]+delta[axis]*int(step)/int(steps));int cx=(m.s(pos)+m.w(0xa0c34))>>9,cz=(m.s(pos+4)+m.w(0xa0c36))>>9;
                    if(cx<0||cz<0||cx>=m.w(0xa0c30)||cz>=m.w(0xa0c32)){remove=true;break;}m.d(p+12,m.d(0xa0c40)+(cz*m.w(0xa0c30)+cx)*16);
                    if(collideProjectileWithMap(m,p,f)){impact(m,f,p);int level=signed32(m.d(0xa0c80));auto sound=level>11&&level<22?35u:36u;auto attr=m.b(m.d(p+12)+10);if(attr==8||attr==9)sound=37;effect(m,f,0x404f8,sound,m.d(p),0);remove=true;}
                    else {effect(m,f,0x40db0,p);if(collideProjectileWithEntities(m,p,f,true))remove=true;}
                }
                if(!remove)effect(m,f,0x40db0,p);
            }else if(kind<=4) {
                if(collideProjectileWithMap(m,p,f)) {
                    if(m.b(p+0x1e)==3){rewindProjectile(m,p);stop(m,p);}
                    else {
                        impact(m,f,p);int level=signed32(m.d(0xa0c80));auto sound=level>11&&level<22?35u:36u;
                        auto attr=m.b(m.d(p+12)+10);if(attr==8||attr==9)sound=37;
                        effect(m,f,0x404f8,sound,m.d(p),0);remove=true;
                    }
                }else if(collideProjectileWithEntities(m,p,f,extendedPlayerRange&&(kind==0||kind==1||kind==2||kind==4))) {
                    if(m.b(p+0x1e)==3){rewindProjectile(m,p);stop(m,p);}else remove=true;
                }else effect(m,f,0x40db0,p);
            }else if(kind==5) {
                if(collideProjectileWithMap(m,p,f)){impact(m,f,p);remove=true;}
                else if(collideProjectileWithPlayer(m,p,f))remove=true;
                else effect(m,f,0x41044,p);
            }else if(kind==11||kind==12||kind==13||kind==19) {
                if(collideBallisticWithMap(m,p,f)){explosion(m,f,p);remove=true;}
                else if(kind==13?collideProjectileWithPlayer(m,p,f):collideProjectileWithEntities(m,p,f)) {
                    rewindProjectile(m,p);
                    if(kind!=13)explosion(m,f,p);
                    blastMap(m,f,p);remove=true;
                }else {
                    effect(m,f,kind==13?0x41044:0x40db0,p);
                    auto floor=shortWord(sx(heightAt(m,x,z)));
                    if(m.s(pos+2)<floor+32) {
                        impact(m,f,p);
                        if(floor-m.s(pos+2)>64){rewindProjectile(m,p);blastMap(m,f,p);explosion(m,f,p,512);remove=true;}
                        else {
                            m.w(pos+2,floor+32);m.w(vel+2,m.w(p+0x18));
                            if(m.s(p+0x18)>4)m.w(p+0x18,m.w(p+0x18)-2u);
                            else {m.w(vel,m.s(vel)>>1);m.w(vel+4,m.s(vel+4)>>1);}
                        }
                    }else m.w(vel+2,m.w(vel+2)-(kind==13?2u:1u));
                }
            }else if(kind==6||kind==15) {
                auto floor=shortWord(sx(heightAt(m,x,z))+64);
                if(!m.w(vel+2))remove=true;
                else {
                    m.w(pos+2,m.w(pos+2)+m.w(vel+2));
                    if(m.s(pos+2)<floor) {
                        m.w(pos+2,floor);m.w(p+0x18,m.w(p+0x18)-8u);
                        if(m.b(p+0x1e)==15)effect(m,f,0x404f8,109,m.d(p),1);
                        m.w(vel+2,m.s(p+0x18)<4?0:m.w(p+0x18));m.w(vel,0);m.w(vel+4,0);
                    }else m.w(vel+2,m.s(vel+2)==4?-2:m.w(vel+2)-4u);
                }
            }else if(kind==7) {
                auto floor=shortWord(sx(heightAt(m,x,z)));auto attr=m.b(m.d(p+12)+10);
                if(!m.w(vel+2))remove=true;
                else if((attr==9&&m.s(pos+2)<floor+384)||(attr==8&&m.s(pos+2)<floor+256)||m.s(pos+2)<floor)stop(m,p);
                else m.w(vel+2,m.s(vel+2)==4?-2:m.w(vel+2)-4u);
            }else if(kind==8||kind==9||kind==10) {
                if(kind!=8&&collideProjectileWithPlayer(m,p,f))remove=true;
                else {auto floor=shortWord(sx(heightAt(m,x,z)));m.w(vel+2,m.w(vel+2)-4u);if(m.s(pos+2)<floor){effect(m,f,0x2a684,p);remove=true;}}
            }else if(kind==14) {
                if(collideProjectileWithPlayer(m,p,f))remove=true;
                else if(m.b(p+0x1c)<12)m.w(vel+2,m.w(vel+2)+10u);
            }else if(kind==16) {
                m.b(p+0x1c,m.b(p+0x1c)-1u);
                if(collideProjectileWithPlayer(m,p,f))remove=true;
                else if(m.b(p+0x1c)<20){m.w(vel+2,m.w(vel+2)-2u);m.w(vel+4,m.w(vel+4)-12u);if(m.s(vel+4)<0)m.w(vel+4,0);}
            }else if(kind==17||kind==18) {
                if(kind==17)m.b(p+0x1c,m.b(p+0x1c)-1u);
                if(collideProjectileWithPlayer(m,p,f))remove=true;
                else {
                    auto cell=m.d(p+12);int direction=0;
                    if(m.s(vel+2)>0&&m.s(pos+2)>m.b(cell+13)*32-512){m.w(pos+2,(m.b(cell+13)-16)*32);direction=-10;}
                    else if(m.s(vel+2)<0&&m.s(pos+2)<m.b(cell+11)*32+128){m.w(pos+2,(m.b(cell+11)+4)*32);direction=10;}
                    if(direction) {
                        for(unsigned offset:{0u,4u}) {
                            auto magnitude=m.s(vel+2)/2; // x86 rounds toward zero here.
                            magnitude+=nextGameRandom(m)&15;auto sign=nextGameRandom(m)&1?-1:1;m.w(vel+offset,magnitude*sign);
                        }
                        m.w(vel+2,direction);
                    }
                }
            }else m.w(vel+2,m.w(vel+2)-(kind==20||kind==21?6u:4u));
            if(remove)recycle(m,f,p);else effect(m,f,0x2d004,p);
            p=next;
        }
    }
    m.d(0x22cec0,m.d(task+24));
}
}
