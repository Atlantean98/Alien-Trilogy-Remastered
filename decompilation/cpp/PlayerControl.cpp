#include "PlayerControl.h"
#include <bit>
#include <cstring>
#include <vector>

namespace trilogy::original {
// PlayerControlMemory::s is now defined inline in PlayerControl.h.
namespace {
using Mem=PlayerControlMemory;using Service=PlayerControlService;
std::int16_t sw(std::uint16_t n){return std::bit_cast<std::int16_t>(n);}
std::uint32_t signMask(Mem m,std::uint32_t address){return static_cast<std::uint32_t>(m.s(address));}
PlayerControlResult call(Mem m,const Service& f,std::uint32_t address,std::uint32_t a=0,std::uint32_t b=0,std::uint32_t c=0){return f(m,address,a,b,c);}
constexpr std::uint32_t State=0xa0aac,Weapon=0xa0aae,Held=0xa0c98,Pressed=0xa0c94,Previous=0xa0c7c;
constexpr std::uint32_t Flags=0xa0ab4,TerrainFlags=0xa0ab5,Speed=0xa0a64,Yaw=0xa0a52,Pitch=0xa0a50;
constexpr std::uint32_t Ammo[]={0xa0abe,0xa0ac2,0xa0ac4,0xa0acc,0xa0aca};
void recenter(Mem m){PlayerMovementState p;p.pitch=m.w(Pitch);p.pitchRate=m.w(0xa0b38);p.restPitch=m.w(0xa0b42);recenterPitch(p);m.w(Pitch,p.pitch);m.w(0xa0b38,p.pitchRate);}
void accelerate(Mem m,bool forward) {
    PlayerMovementState p{m.w(Speed),m.w(0xa0b46),m.w(0xa0b48),m.w(0xa0abc),m.b(m.d(0xa0ab0)+10)};
    if(forward)accelerateForward(p);else accelerateOtherDirection(p);m.w(Speed,p.speed);
}
void pitch(Mem m,bool up) {
    if(m.s(0xa0b38)<m.s(0xa0b36))m.w(0xa0b38,m.w(0xa0b38)+8u);
    if(m.s(0xa0b38)>m.s(0xa0b36))m.w(0xa0b38,m.w(0xa0b36));
    m.w(Pitch,up?m.w(Pitch)-m.w(0xa0b38):m.w(Pitch)+m.w(0xa0b38));
    auto bound=up?0xa0b3au:0xa0b3cu;
    if(up?m.s(Pitch)<m.s(bound):m.s(Pitch)>m.s(bound))m.w(Pitch,m.w(bound));
}
void slow(Mem m){if(m.s(Speed)>0)m.w(Speed,m.s(Speed)<m.s(0xa0b4a)?0:m.w(Speed)-m.w(0xa0b4a));}
void beginReload(Mem m,const Service& f,std::uint32_t clip,std::uint32_t reserve,std::uint16_t capacity) {
    m.w(State,2);m.w(reserve,m.w(reserve)-1u);m.w(clip,capacity);call(m,f,0x3f9c4);
}
void switchWeapon(Mem m,const Service& f,unsigned index,unsigned flag) {
    m.b(TerrainFlags,m.b(TerrainFlags)|flag);m.w(State,4);call(m,f,0x512d0,index);
}
}
void firePrimary(Mem m,const Service& f) {
    if(m.b(0x95f9c)!=1)return;
    auto weapon=m.w(Weapon);if(weapon>4||!m.w(Ammo[weapon]))return;
    if(weapon!=4||m.w(State)==0) {
        constexpr unsigned noise[]={0x80,0x200,0x140,0x200,0x80};
        m.w(0xa0adc,noise[weapon]);m.w(State,1);
        if(weapon==0)m.d(0x23630c,m.d(0x23630c)+1u);
        call(m,f,0x3f9c4);m.b(0xa0c91,m.b(0xa0c91)|4);
    }
    constexpr std::uint32_t projectile[]={0x2b4a4,0x2b4f0,0x2b548,0x2bb04,0x2b9d0};
    call(m,f,projectile[weapon]);call(m,f,0x34a9c);
}
void fireSecondary(Mem m,const Service& f) {
    if(m.b(0x95f9c)!=1||!m.w(0xa0ad0))return;
    m.w(0xa0adc,0x280);if(!(m.b(0x94b9c)&0x20))m.w(0xa0ad0,m.w(0xa0ad0)-1u);
    call(m,f,0x2c160);call(m,f,0x40460,9,0xffffffff,0x1fff);
    call(m,f,0x51948,9,static_cast<std::uint32_t>(m.s(0xa0a48)),static_cast<std::uint32_t>(m.s(0xa0a4c)));
}
void playerInputAndWeaponIntent(Mem m,const Service& f) {
    if(m.b(0xa0cc7)){m.w(Speed,0);return;}
    if(m.b(0xa0c92)&2) {
        m.d(Previous,m.d(Held));auto sample=call(m,f,0x10a50).eax;
        m.d(Held,sample);m.d(Pressed,~m.d(Previous)&sample);
        if(sample==0xffff)m.b(0xa0c90,m.b(0xa0c90)|8);
    }
    if(!(m.b(0xa0c92)&2)) {
        if(m.b(0x3f6e7f))call(m,f,0x5de4c);
        if(m.w(0x3fae1a)){m.w(Yaw,(m.w(Yaw)+m.w(0x3fae1a))&4095);m.w(0x3fae1a,0);m.b(0x9e16d,0);}
    } else {m.w(0x3fae1a,0);m.w(0x3fae1c,0);}
    if(m.b(0xa0c90)&8)return;
    auto heading=m.w(Yaw)&4095;
    if((m.d(Pressed)&0x800)&&m.w(State)==0){m.b(0xa0c91,m.b(0xa0c91)|0x20);return;}
    if(m.w(State)==4) {
        m.b(0xa0b57,m.b(0xa0b57)+16u);
        if(m.b(0xa0b57)>128) {
            m.b(0xa0b57,128);
            if(m.b(TerrainFlags)&4){call(m,f,0x38958);m.b(TerrainFlags,m.b(TerrainFlags)&0xfb);}
            else if(m.b(TerrainFlags)&2){call(m,f,0x38814);m.b(TerrainFlags,m.b(TerrainFlags)&0xfd);}
            else if(m.s(0x3f6e6a)==200)call(m,f,0x386d4);
            else call(m,f,0x389a8,static_cast<std::uint32_t>(m.s(0x3f6e6a)));
            call(m,f,0x4e6c8);m.w(State,5);
        }
    } else if(m.w(State)==5){m.b(0xa0b57,m.b(0xa0b57)-16u);if(!m.b(0xa0b57))m.w(State,0);}
    auto held=[&](unsigned a){return (signMask(m,a)&m.d(Held))!=0;};
    if(held(0x94b8a)) {
        m.b(Flags,m.b(Flags)|2);m.w(0xa0b48,12);m.w(0xa0b4a,9);m.w(0xa0b32,60);m.w(0xa0b46,144);
    } else {
        m.b(Flags,m.b(Flags)&0xfd);m.w(0xa0b46,120);m.w(0xa0b48,9);m.w(0xa0b4a,6);m.w(0xa0b32,m.w(0x9cde0));
    }
    bool left=held(0x94b8c),right=held(0x94b88);
    m.b(Flags,left||right?m.b(Flags)|1:m.b(Flags)&0xfe);
    if(held(0x94b8e)) {
        if(held(0x94b7a))pitch(m,true);else if(held(0x94b7c))pitch(m,false);
    } else {
        auto attr=m.b(m.d(0xa0ab0)+10);int direction=0;
        if(attr>=0x2d&&attr<=0x38) {
            auto group=(attr-0x2d)%4;
            if(group==0||group==3) {
                if(heading<=512)direction=group==0?-1:1;
                else if(heading>=1536&&heading<=2560)direction=group==0?1:-1;
            } else {
                if(heading>=2560&&heading<=3584)direction=group==1?1:-1;
                else if(heading>=512&&heading<=1536)direction=group==1?-1:1;
            }
        }
        if(direction)pitch(m,direction<0);else recenter(m);
    }
    if(!m.w(0xa0b40)&&(m.d(Held)&0x200)){m.w(0xa0b3e,0);m.w(0xa0b40,1);}
    if(m.w(0xa0b40)){m.w(Yaw,(m.w(Yaw)-128u)&4095);m.w(0xa0b3e,m.w(0xa0b3e)+1u);if(m.s(0xa0b3e)>15)m.w(0xa0b40,0);}
    bool turnLeft=(m.d(Held)&0x8000)&&!(m.d(Previous)&0x2000);
    bool turnRight=(m.d(Held)&0x2000)&&!(m.d(Previous)&0x8000);
    if(turnLeft||turnRight) {
        if(m.s(0xa0b34)<m.s(0xa0b32))m.w(0xa0b34,m.w(0xa0b34)+4u);
        if(m.s(0xa0b34)>m.s(0xa0b32))m.w(0xa0b34,m.w(0xa0b32));
        bool add=!turnLeft;
        m.w(Yaw,(add?m.w(Yaw)+m.w(0xa0b34):m.w(Yaw)-m.w(0xa0b34))&4095);
    } else m.w(0xa0b34,0);
    bool forward=m.d(Held)&0x1000,back=m.d(Held)&0x4000;
    if(!(m.b(Flags)&1)) {
        if(forward){accelerate(m,true);m.w(0xa0a5a,m.w(Yaw));}
        else if(back){accelerate(m,false);m.w(0xa0a5a,(m.w(Yaw)+2048u)&4095);}
        else if(!m.w(0x3fae1c))slow(m);
        else if(m.s(0x3fae1c)<-5){m.w(Speed,75);m.w(0xa0a5a,m.w(Yaw));}
        else if(m.s(0x3fae1c)<6)m.w(Speed,0);
        else {m.w(Speed,75);m.w(0xa0a5a,(m.w(Yaw)+2048u)&4095);}
    } else {
        int offset=0;bool moving=true;
        if(forward&&left)offset=-512;else if(forward&&right)offset=512;
        else if(back&&left)offset=2560;else if(back&&right)offset=1536;
        else if(left)offset=-1024;else if(right)offset=1024;else moving=false;
        if(moving){accelerate(m,false);m.w(0xa0a5a,(m.w(Yaw)+offset)&4095);}else slow(m);
    }
    call(m,f,m.w(Speed)==0?0x34a1c:0x34a5c);
    auto primaryPressed=(signMask(m,0x94b7e)&m.d(Pressed))!=0;
    auto secondaryPressed=(signMask(m,0x94b90)&m.d(Pressed))!=0;
    auto primaryHeld=(signMask(m,0x94b7e)&m.d(Held))!=0;
    switch(m.w(Weapon)) {
        case 0:case 1:if(!m.w(State)){if(primaryPressed)firePrimary(m,f);else if(secondaryPressed)fireSecondary(m,f);}break;
        case 2:if(!m.w(State)) {
            if(primaryHeld)firePrimary(m,f);
            if((signMask(m,0x94b90)&m.d(Pressed))&&m.b(0x95f9c)==1&&m.w(0xa0ac8)) {
                m.w(0xa0adc,0x280);if(!(m.b(0x94b9c)&0x20))m.w(0xa0ac8,m.w(0xa0ac8)-1u);
                m.w(State,3);call(m,f,0x3f9c4);call(m,f,0x2be10);
            }
        }break;
        case 3:if(!m.w(State)){if(primaryHeld)firePrimary(m,f);else if(secondaryPressed)fireSecondary(m,f);}break;
        case 4:if(m.w(State)!=4&&m.w(State)!=5) {
            if(primaryHeld)firePrimary(m,f);else{call(m,f,0x59edd);if(signMask(m,0x94b90)&m.d(Pressed))fireSecondary(m,f);}
        }break;
    }
    if(m.w(State))return;
    switch(m.w(Weapon)) {
        case 0:if(!m.w(0xa0abe)) {
            if(m.w(0xa0ac0))beginReload(m,f,0xa0abe,0xa0ac0,15);
            else if(!call(m,f,0x38628).eax){m.w(State,2);m.w(0xa0abe,1);call(m,f,0x3f9c4);}
            else switchWeapon(m,f,0,2);
        }break;
        case 1:if(!m.w(0xa0ac2)) {
            if(call(m,f,0x38628).eax)switchWeapon(m,f,1,2);
            else{if(!m.w(0xa0abe))m.w(0xa0abe,1);switchWeapon(m,f,1,4);}
        }break;
        case 2:if(!m.w(0xa0ac4)) {
            if(m.w(0xa0ac6))beginReload(m,f,0xa0ac4,0xa0ac6,100);
            else if(!m.w(0xa0ac8)) {
                m.w(State,4);call(m,f,0x512d0,2);
                if(call(m,f,0x38628).eax)m.b(TerrainFlags,m.b(TerrainFlags)|2);
                else if(!m.w(0xa0abe)){m.w(0xa0abe,1);m.b(TerrainFlags,m.b(TerrainFlags)|4);}
            }
        }break;
        case 3:if(!m.w(0xa0acc)) {
            if(m.w(0xa0ace))beginReload(m,f,0xa0acc,0xa0ace,100);
            else if(call(m,f,0x38628).eax)switchWeapon(m,f,4,2);
            else{if(!m.w(0xa0abe))m.w(0xa0abe,1);switchWeapon(m,f,4,4);}
        }break;
        case 4:if(!m.w(0xa0aca)) {
            if(call(m,f,0x38628).eax)switchWeapon(m,f,3,2);
            else{if(!m.w(0xa0abe))m.w(0xa0abe,1);switchWeapon(m,f,3,4);}
        }break;
    }
}
PlayerControlResult resetPlayerInventory(Mem m,const Service& f) {
    call(m,f,0x380c0);m.d(Flags,0);m.w(0xa0aba,0);
    for(auto a:{0xa0ad0u,0xa0abeu,0xa0ac0u,0xa0ac2u,0xa0ac4u,0xa0ac6u,0xa0ac8u,0xa0acau,0xa0accu,0xa0aceu,Weapon})m.w(a,0);
    m.w(0xa0ab8,100);
    for(unsigned i=0;i<2;++i){m.d(0xa0b58 + i*8,0xffffffff);m.w(0xa0b5c + i*8,0);m.w(0xa0b5e + i*8,0);}
    return call(m,f,0x38170,0,15,2);
}
namespace {
PlayerState load(Mem m) {
    PlayerState p;auto& w=p.world;auto& v=p.view;
    for(unsigned i=0;i<3;++i){w.position[i]=sw(m.w(0xa0a48+i*2));w.moveAngles[i]=sw(m.w(0xa0a58+i*2));w.localMovement[i]=sw(m.w(0xa0a60+i*2));v.angles[i]=sw(m.w(0xa0a50+i*2));v.position[i]=sw(m.w(0xa0a20+i*2));}
    w.originX=m.w(0xa0c34);w.originZ=m.w(0xa0c36);w.currentCell=(m.d(0xa0ab0)-m.d(0xa0c40))/16u;
    w.movementFlags=m.b(Flags);w.collisionFlags=m.b(0x94b9c);w.terrainFlags=m.b(TerrainFlags);
    w.impulse=m.w(0xa0ade);w.impulseYaw=m.w(0xa0ae0);w.terrainGate=m.w(0xa0abc);w.damageGate=m.w(0xa0ae4);
    w.damageCooldown=m.w(0xa0b30);w.health=m.w(0xa0ab8);w.region=m.w(0xa0b44);w.pressedInput=m.d(Pressed);w.useMask=m.w(0x94b7a);
    for(unsigned i=0;i<9;++i)w.scratchRotation[i]=sw(m.w(0xa0a28+i*2));
    v.phase=m.w(0xa0b54);v.eyeHeight=m.w(0xa0b4c);v.bobEnabled=m.w(0x9ce5c);v.pitchRate=m.w(0xa0b38);v.restPitch=m.w(0xa0b42);v.turnRate=m.w(0xa0b34);
    v.weaponBobLow=m.w(0xa0b4e);v.weaponBobHigh=m.w(0xa0b50);v.yaw=m.w(0xa0ba2);v.roll=m.w(0xa0ba4);v.pitch=m.w(0xa0ba8);v.sway=m.w(0xa0bac);
    p.flowFlags=m.d(0xa0c90);p.networkMode=m.b(0x9e0a8);p.deathLatch=m.b(0x9cddc);p.respawnDelay=m.w(0x9d7fe);return p;
}
void store(Mem m,const PlayerState& p) {
    auto& w=p.world;auto& v=p.view;
    for(unsigned i=0;i<3;++i){m.w(0xa0a48+i*2,std::uint16_t(w.position[i]));m.w(0xa0a58+i*2,std::uint16_t(w.moveAngles[i]));m.w(0xa0a60+i*2,std::uint16_t(w.localMovement[i]));m.w(0xa0a50+i*2,std::uint16_t(v.angles[i]));m.w(0xa0a20+i*2,std::uint16_t(v.position[i]));}
    m.w(0xa0c34,w.originX);m.w(0xa0c36,w.originZ);m.d(0xa0ab0,m.d(0xa0c40)+w.currentCell*16u);
    m.b(Flags,w.movementFlags);m.b(0x94b9c,w.collisionFlags);m.b(TerrainFlags,w.terrainFlags);
    m.w(0xa0ade,w.impulse);m.w(0xa0ae0,w.impulseYaw);m.w(0xa0abc,w.terrainGate);m.w(0xa0ae4,w.damageGate);
    m.w(0xa0b30,w.damageCooldown);m.w(0xa0ab8,w.health);m.w(0xa0b44,w.region);m.d(Pressed,w.pressedInput);m.w(0x94b7a,w.useMask);
    for(unsigned i=0;i<9;++i)m.w(0xa0a28+i*2,std::uint16_t(w.scratchRotation[i]));
    m.w(0xa0b54,v.phase);m.w(0xa0b4c,v.eyeHeight);m.w(0x9ce5c,v.bobEnabled);m.w(0xa0b38,v.pitchRate);m.w(0xa0b42,v.restPitch);m.w(0xa0b34,v.turnRate);
    m.w(0xa0b4e,v.weaponBobLow);m.w(0xa0b50,v.weaponBobHigh);m.w(0xa0ba2,v.yaw);m.w(0xa0ba4,v.roll);m.w(0xa0ba8,v.pitch);m.w(0xa0bac,v.sway);
    m.d(0xa0c90,p.flowFlags);m.b(0x9e0a8,p.networkMode);m.b(0x9cddc,p.deathLatch);m.w(0x9d7fe,p.respawnDelay);
}
struct Scene {
    std::vector<LevelCell> cells;std::array<std::int16_t,4096> trig;LevelGrid grid;
    explicit Scene(Mem m):cells(m.w(0xa0c30)*m.w(0xa0c32)),grid{m.w(0xa0c30),m.w(0xa0c32),cells} {
        std::memcpy(cells.data(),m.bytes.data()+m.d(0xa0c40),cells.size()*16);
        for(unsigned i=0;i<4096;++i)trig[i]=sw(m.w(0x9763c+i*2));
    }
    void flush(Mem m){std::memcpy(m.bytes.data()+m.d(0xa0c40),cells.data(),cells.size()*16);}
    void reload(Mem m){std::memcpy(cells.data(),m.bytes.data()+m.d(0xa0c40),cells.size()*16);}
};
void view(Mem m,const Service& f) {
    auto p=load(m);Scene scene(m);TaskScheduler tasks(0x300000);
    auto effects=[&](PlayerState& current,TaskScheduler&,TaskAddress,std::uint32_t address,std::uint32_t a,std::uint32_t b,std::uint32_t c) {
        store(m,current);scene.flush(m);call(m,f,address,a,b,c);current=load(m);scene.reload(m);
    };
    buildPlayerView(p,scene.grid,scene.trig,tasks,0x300000,effects);store(m,p);scene.flush(m);
}
std::int32_t floor(Mem m,std::uint32_t x,std::uint32_t z){Scene scene(m);return floorHeight(scene.grid,x,z);}
}
void initializePlayer(Mem m,std::uint32_t task,const Service& f) {
    m.b(0x236c94,255);
    if((m.b(0xa0c92)&8)&&m.b(0x9e0a8)){m.b(0xa0c90,m.b(0xa0c90)|0x80);call(m,f,0x4e130);}
    for(unsigned i=0;i<2;++i){m.d(0xa0b58 + i*8,0xffffffff);m.w(0xa0b5c + i*8,0);m.w(0xa0b5e + i*8,0);}
    m.w(0xa0a48,m.w(0xa0c24)*512u-m.w(0xa0c34));m.w(0xa0a4c,m.w(0xa0c26)*512u-m.w(0xa0c36));
    auto x=static_cast<std::uint32_t>(m.s(0xa0a48))+m.w(0xa0c34),z=static_cast<std::uint32_t>(m.s(0xa0a4c))+m.w(0xa0c36);
    m.w(0xa0a4a,floor(m,x,z));auto cell=m.d(0xa0c40)+((z>>9)*m.w(0xa0c30)+(x>>9))*16u;
    m.d(0xa0ab0,cell);m.b(cell+12,255);
    if(!m.b(0x9e0a8))m.w(0xa0b44,m.b(cell+6));
    else {
        m.w(0xa0b44,m.w(0xa0bc0));auto spawn=m.d(0xa0c50)+m.d(0xa0bc0)*8u;
        m.w(0xa0a48,m.b(spawn)*512u-m.w(0xa0c34));m.w(0xa0a4c,m.b(spawn+1)*512u-m.w(0xa0c36));
        x=static_cast<std::uint32_t>(m.s(0xa0a48))+m.w(0xa0c34);z=static_cast<std::uint32_t>(m.s(0xa0a4c))+m.w(0xa0c36);
        m.w(0xa0a4a,floor(m,x,z));m.b(0xa0c92,m.b(0xa0c92)&0xf3);m.w(0xa0ab8,100);m.b(0x9cddc,0);
    }
    if((m.b(0xa0c92)&8)&&m.s(0xa0ab8)<100)m.w(0xa0ab8,100);
    auto region=static_cast<std::uint32_t>(m.s(0xa0b44));call(m,f,0x27b6c,m.d(0xa0c50)+region*8u,region);
    m.w(0xa0a54,2048);m.w(Pitch,48);
    constexpr std::uint16_t yaw[]={0x800,0xa00,0xfc00,0xfe00,0,0x200,0x400,0x600};
    if(m.w(0xa0c28)<8)m.w(Yaw,yaw[m.w(0xa0c28)]);
    for(auto a:{0xa0a58u,0xa0a5au,0xa0a60u,0xa0a62u,Speed})m.w(a,0);m.w(0xa0a5c,2048);
    if(m.b(0x9e0a8)&&!m.b(0x9cddd)&&!(m.b(0x94b9c)&0x20)) {
        auto armor=resetPlayerInventory(m,f).ecx&255;m.w(0xa0aba,armor);m.b(Flags,m.b(Flags)&7);m.w(Weapon,0);
        call(m,f,0x3fa98,0);call(m,f,0x4e570);m.w(State,0);
    }
    if(m.b(0x9e0a8)&&m.b(0x9cddd)&&m.s(0xa0ab8)<100)m.w(0xa0ab8,100);
    if(m.b(0x9e0a8)){m.w(0xa0abe,15);m.w(0xa0ac0,6);m.b(Flags,m.b(Flags)|4);}
    m.b(0x9cddd,0);m.w(Flags,m.w(Flags)&0xbe7f);
    for(auto a:{0xa0ad2u,0xa0ad4u,0xa0ad8u,0xa0adau,0xa0abcu,0xa0ae2u,0xa0ad6u,0xa0ae4u})m.w(a,0);
    for(unsigned i=0;i<37;++i)m.w(0xa0ae6 + i*2,0);
    for(auto a:{0xa0adcu,0xa0b30u,0xa0adeu,0xa0ae0u,0xa0b38u,0xa0b34u,0xa0b3eu,0xa0b40u,0xa0b4eu,0xa0b50u})m.w(a,0);
    m.w(0xa0b42,48);m.w(0xa0b3a,0xff80);m.w(0xa0b36,16);m.w(0xa0b3c,128);m.w(0xa0b46,120);m.w(0xa0b4a,6);
    m.w(0xa0b52,1);m.w(0xa0b48,9);m.w(0xa0b32,m.w(0x9cde0));m.w(0xa0b54,2);m.b(0xa0b56,0);m.b(0xa0b57,0);m.w(0xa0b4c,768);
    view(m,f);call(m,f,0x3fa98,static_cast<std::uint32_t>(m.s(Weapon)));call(m,f,0x380f4);
    m.d(task+4,0x3f908);call(m,f,0x34a04);m.d(0x22cec0,m.d(task+24));
}
void updateControlledPlayer(Mem m,std::uint32_t task,const Service& f) {
    if(m.d(task+4)==0x3fbc0){initializePlayer(m,task,f);return;}
    auto p=load(m);Scene scene(m);TaskScheduler tasks(task);std::memcpy(&tasks.nodes[0],m.bytes.data()+task,32);
    tasks.cursor=m.d(0x22cec0);tasks.remainingTicks=m.b(0x95f9c);
    auto flush=[&](){store(m,p);scene.flush(m);std::memcpy(m.bytes.data()+task,&tasks.nodes[0],32);m.d(0x22cec0,tasks.cursor);m.b(0x95f9c,tasks.remainingTicks);};
    auto reload=[&](){p=load(m);scene.reload(m);std::memcpy(&tasks.nodes[0],m.bytes.data()+task,32);tasks.cursor=m.d(0x22cec0);tasks.remainingTicks=m.b(0x95f9c);};
    auto effects=[&](PlayerState&,TaskScheduler&,TaskAddress,std::uint32_t address,std::uint32_t a,std::uint32_t b,std::uint32_t c){
        flush();if(address==0x3e894)playerInputAndWeaponIntent(m,f);else call(m,f,address,a,b,c);reload();
    };
    auto services=[&](std::uint32_t address,std::uint32_t a,std::uint32_t b,WorldPlayerState&,LevelGrid&){
        flush();if(address==0x27b6c){b=a;a=m.d(0xa0c50)+a*8u;}call(m,f,address,a,b);reload();
    };
    updatePlayerState(p,scene.grid,scene.trig,tasks,task,services,effects);flush();
}
}
