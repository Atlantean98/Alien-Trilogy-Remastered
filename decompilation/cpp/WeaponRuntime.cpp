#include "WeaponRuntime.h"

namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;using Service=PlayerControlService;
constexpr std::uint32_t Object=0xa0a68,Table=0xa0aa8,State=0xa0aac,Selected=0xa0aae,Slots=0x2368f8,Current=0x236a3a;
PlayerControlResult invoke(Mem m,const Service& f,unsigned address,unsigned a=0,unsigned b=0,unsigned c=0){return f(m,address,a,b,c);}
std::uint32_t sx(std::int32_t n){return static_cast<std::uint32_t>(n);}
std::int32_t findNext(Mem m) {
    auto current=m.s(Current);
    for(auto i=current+1;i<5;++i)if(m.w(Slots+i*4+2))return i;
    for(int i=0;i<current;++i)if(m.w(Slots+i*4+2))return i;
    return -1;
}
void selectFromSlot(Mem m,int slot) {
    auto kind=m.w(Slots+slot*4);
    if(kind<5)selectWeapon(m,kind);
}
}
void decodeAnimationCommands(Mem m,std::uint32_t object) {
    m.b(object+0x36,m.b(object+0x36)&0xef);
    for(;;) {
        auto pc=m.d(object+0x18);auto token=m.w(pc);if(!(token&0x8000))return;
        auto opcode=(token>>8)&0x7f;
        switch(opcode) {
            case 1:m.w(object+0x38,token&255);m.b(object+0x36,m.b(object+0x36)|16);break;
            case 2:
                if(!m.w(object+0x3a))m.w(object+0x3a,token&255);
                else {m.w(object+0x3a,m.w(object+0x3a)-1u);if(!m.w(object+0x3a)){m.b(object+0x36,m.b(object+0x36)|2);return;}}
                m.d(object+0x18,m.d(object+0x1c));continue;
            case 3:m.b(object+0x36,m.b(object+0x36)|1);break;
            case 4:m.b(object+0x36,m.b(object+0x36)&0xfe);break;
            case 6:m.b(object+0x36,m.b(object+0x36)|2);return;
            case 8:m.b(object+0x36,m.b(object+0x36)|4);return;
            case 9:m.b(object+0x36,m.b(object+0x36)|8);break;
            default:break;
        }
        m.d(object+0x18,pc+2);
    }
}
void advanceAnimation(Mem m,std::uint32_t object) {
    m.w(object+0x30,m.w(object+0x30)-1u);if(m.w(object+0x30))return;
    m.w(object+0x30,m.w(object+0x32));m.d(object+0x18,m.d(object+0x18)+2);
    decodeAnimationCommands(m,object);
    if(!(m.b(object+0x36)&6)) {
        auto index=m.s(m.d(object+0x18));m.d(object+0x10,m.d(object+0x14)+sx(index*12));
        m.w(object+0x3c,m.w(m.d(object+0x18)));
    }
}
void startWeaponAnimation(Mem m) {
    if(!m.d(Table))return;
    auto record=m.d(Table)+sx(m.s(State)*8);auto sequence=m.d(record+4);auto descriptor=m.d(0xa0a8c);
    m.d(Object+0x14,m.d(record));m.d(Object+0x1c,sequence+4);m.d(Object+0x18,sequence+2);
    m.w(Object+0x30,1);m.w(Object+0x32,m.w(sequence));
    m.b(Object,0);m.b(Object+1,m.b(descriptor+4));m.w(Object+2,m.w(m.d(0xa0a88)));
    m.d(Object+0x10,0);m.b(Object+5,m.b(descriptor+4));m.w(Object+6,m.w(descriptor));
    m.w(Object+0x3a,0);m.w(Object+0x36,0);m.b(Object+8,0);m.d(descriptor+8,0);
    advanceAnimation(m,Object);
}
void selectWeapon(Mem m,std::uint16_t weapon) {
    m.w(State,0);m.w(Object+0x36,0);m.w(Selected,weapon);
    constexpr unsigned tables[]={0x9ce14,0x9ce04,0x9cde4,0x9ce44,0x9ce2c};
    constexpr unsigned resources[]={0x23441c,0x234414,0x234450,0x234404,0x234418};
    if(weapon<5){m.d(Table,tables[weapon]);m.d(0xa0a88,m.d(resources[weapon]));}
    else m.d(Table,0);
    startWeaponAnimation(m);
}
std::uint32_t hasAlternativeWeapon(Mem m){return findNext(m)<0?0:0xffffffff;}
std::uint32_t cycleWeapon(Mem m) {
    auto next=findNext(m);if(next<0)return 0;m.w(Current,next);selectFromSlot(m,next);return 0xffffffff;
}
std::uint32_t discardAndCycleWeapon(Mem m) {
    auto next=findNext(m);if(next<0)return 0;auto old=m.s(Current);m.w(Current,next);m.w(Slots+old*4+2,0);selectFromSlot(m,next);return 0xffffffff;
}
void fallbackToPistol(Mem m){auto old=m.s(Current);m.w(Current,0);m.w(Slots+old*4+2,0);m.w(Slots+2,0xffff);selectWeapon(m,0);}
std::uint32_t chooseWeaponSlot(Mem m,std::int16_t slot) {
    if(!m.w(Slots+sx(slot*4)+2))return 0;m.w(Current,static_cast<std::uint16_t>(slot));selectFromSlot(m,slot);return 0xffffffff;
}
void updateWeaponView(Mem m,const Service& f) {
    if(m.d(0xa0b58)!=0xffffffff) {
        if(m.w(Selected)==4&&m.s(0xa0b5c)>0)m.w(0xa0b5c,m.w(0xa0b5c)-1u);
        else invoke(m,f,0x59edd);
    }
    if(!m.d(Table))return;
    advanceAnimation(m,Object);
    if(m.b(Object+0x36)&16) {
        auto sound=sx(m.s(Object+0x38));invoke(m,f,0x403ec,sound,0xffffffff,0xffffffff);
        if(m.w(Object+0x38)!=11)invoke(m,f,0x51948,sx(m.s(Object+0x38)),sx(m.s(0xa0a48)),sx(m.s(0xa0a4c)));
    }
    if((m.b(Object+0x36)&2)&&m.w(State)!=4&&m.w(State)!=5) {
        m.b(Object+0x36,m.b(Object+0x36)&0xfd);m.w(State,0);startWeaponAnimation(m);
    }
    auto index=sx(m.s(Selected))*4u;
    m.w(Object+0x28,160);m.w(Object+0x2a,240-(m.s(0xa0b50)>>4)+m.b(0xa0b57));
    m.w(Object+0x28,m.w(Object+0x28)+m.w(0x9ce5e + index));m.w(Object+0x2a,m.w(Object+0x2a)+m.w(0x9ce60 + index));
    auto descriptor=m.d(Object+0x24),frame=m.d(Object+0x10),resource=m.d(0x2344a8);
    if(m.d(descriptor+8)!=m.d(frame+8)) {
        if(!invoke(m,f,0x27f54,Object,1,resource).eax)return;
        descriptor=m.d(Object+0x24);frame=m.d(Object+0x10);
        auto x=m.b(frame+4)-1u,y=m.b(descriptor+4)+m.b(frame+6)-1u;
        m.b(Object+4,x);m.b(Object+12,x);m.b(Object+9,y);m.b(Object+13,y);m.d(descriptor+8,m.d(frame+8));
    }
    invoke(m,f,0x285f4,Object,resource);
}
void spawnPistolShot(Mem m,const Service& f) {
    if(m.b(0x9e0a8))invoke(m,f,0x51cc4,sx(m.s(0xa0a64)),0);
    if(invoke(m,f,0x2ae64,sx(m.s(0xa0a64)),0).eax) {
        invoke(m,f,0x2acac,0);if(!(m.b(0x94b9c)&0x20))m.w(0xa0abe,m.w(0xa0abe)-1u);
    }
}
void spawnShotgunShot(Mem m,const Service& f) {
    if(invoke(m,f,0x2ae64,sx(m.s(0xa0a64)),1).eax) {
        invoke(m,f,0x2acac,1);m.w(0xa0ae0,(m.w(0xa0ba2)+2048u)&4095);m.w(0xa0ade,64);
        if(!(m.b(0x94b9c)&0x20))m.w(0xa0ac2,m.w(0xa0ac2)-1u);
    }
}
void spawnPulseBurst(Mem m,const Service& f) {
    int index=0;
    while(index<3) {
        m.w(0x236314,index);auto speed=sx(static_cast<std::int16_t>(m.w(0xa0a64)+index*128));
        auto result=invoke(m,f,0x2ae64,speed,2);index=m.s(0x236314);if(!result.eax)break;
        if(m.b(0x9e0a8)){speed=sx(static_cast<std::int16_t>(m.w(0xa0a64)+index*128));invoke(m,f,0x51cc4,speed,2);index=m.s(0x236314);}
        if(!(m.b(0x94b9c)&0x20)){m.w(0xa0ac4,m.w(0xa0ac4)-1u);if(!m.w(0xa0ac4))break;}
        ++index;
    }
    m.w(0x236314,index);
}
PlayerControlService withWeaponRuntime(const Service& external) {
    return [external](Mem& m,unsigned address,unsigned a,unsigned b,unsigned c)->PlayerControlResult {
        switch(address) {
            case 0x28280:decodeAnimationCommands(m,a);break;
            case 0x2840c:advanceAnimation(m,a);break;
            case 0x3f9c4:startWeaponAnimation(m);break;
            case 0x3fa98:selectWeapon(m,static_cast<std::uint16_t>(a));break;
            case 0x38628:return {hasAlternativeWeapon(m),0};
            case 0x386d4:return {cycleWeapon(m),0};
            case 0x38814:return {discardAndCycleWeapon(m),0};
            case 0x38958:fallbackToPistol(m);break;
            case 0x389a8:return {chooseWeaponSlot(m,static_cast<std::int16_t>(a)),0};
            case 0x3e228:updateWeaponView(m,external);break;
            case 0x2b4a4:spawnPistolShot(m,external);break;
            case 0x2b4f0:spawnShotgunShot(m,external);break;
            case 0x2b548:spawnPulseBurst(m,external);break;
            default:return external(m,address,a,b,c);
        }
        return {};
    };
}
}
