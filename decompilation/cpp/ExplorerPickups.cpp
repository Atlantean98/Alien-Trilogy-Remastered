#include "ExplorerPickups.h"
#include "PickupRuntime.h"
#include "ProjectileRuntime.h"
#include "PickupScene.h"
#include "DropRendering.h"
#include "WorldMovement.h"
#include <algorithm>
#include <stdexcept>
using namespace trilogy::original;
namespace {constexpr unsigned Pool=0x340000,Task=0x343000,MessageTable=0x9c994,MessageTask=0x9c9c4,MessageCount=0x9c9c8,MaxQueuedMessages=4;}
const wchar_t* ExplorerPickups::name(unsigned kind){static const wchar_t* names[]={L"Pistol",L"Shotgun",L"Pulse rifle",L"Flamethrower",L"Smartgun",L"",L"Seismic charges",L"Battery",L"Night vision goggles",L"9mm ammunition clip",L"Shotgun cartridges",L"Pulse rifle clip",L"Grenades",L"Flamethrower fuel",L"Smartgun ammunition",L"Identity tag",L"Auto mapper",L"Hypo pack",L"Acid vest",L"Body suit",L"Medikit",L"DermPatch",L"Boots",L"Adrenaline",L"DermPatch",L"Shoulder lamp",L"Shotgun cartridges",L"9mm ammunition clip"};return kind<28?names[kind]:L"Item";}
// 0x80037c78 / 0x80037b98 / 0x800379ac in SLUS_000.07 (0x3abe8 / 0x3ab6c / 0x3aa44 in object1.bin):
// 4-slot pickup message table at 0x80072c40 (0x9c994), where each message displays for strlen(text) + 40 ticks.
void ExplorerPickups::syncMessageTable(){
    if(m.bytes.empty())return;
    for(unsigned i=0;i<MaxQueuedMessages;++i){
        auto slot=MessageTable+i*12;
        m.w(slot,16);
        m.w(slot+2,58);
        if(i<messages.size()){
            m.w(slot+4,messages[i].length);
            m.w(slot+6,messages[i].age);
            m.d(slot+8,0x3f98f4+i*4);
        }else{
            m.w(slot+4,0);
            m.w(slot+6,0);
            m.d(slot+8,0);
        }
    }
    m.d(MessageTask,messages.empty()?0:MessageTable);
    m.w(MessageCount,unsigned(messages.size()));
}
std::wstring ExplorerPickups::advanceMessages(){
    while(!messages.empty()){
        auto& front=messages.front();
        ++front.age;
        if(front.age<front.length+40u){
            syncMessageTable();
            return front.text;
        }
        messages.erase(messages.begin());
    }
    syncMessageTable();
    return {};
}
void ExplorerPickups::load(PlayerControlMemory memory){m=memory;collected=0;lastKind=255;messages.clear();syncMessageTable();m.d(0x236c40,Pool);std::fill_n(m.bytes.begin()+Pool,128*48,0);m.d(Task+24,0);
    auto service=[&](PlayerControlMemory& mem,unsigned address,unsigned a,unsigned b,unsigned){if(address==0x3b2bc)populateLevelPickups(mem,a,b);else if(address==0x3af20)initializeNetworkPickupPlacement(mem);return PlayerControlResult{};};resetDroppedPickupPool(m,service);
}
void ExplorerPickups::clear(){messages.clear();syncMessageTable();m.d(0x236c44,0);m.d(0x236c38,0);for(unsigned i=0;i<128;++i)releaseDroppedPickup(m,Pool+i*48);}
std::vector<unsigned> ExplorerPickups::active()const{std::vector<unsigned> list;for(auto p=m.d(0x236c44);p;p=m.d(p+0x28)){if(p<Pool||p>=Pool+128*48||(p-Pool)%48||std::find(list.begin(),list.end(),p)!=list.end())throw std::runtime_error("Invalid active pickup list");list.push_back(p);}return list;}
std::wstring ExplorerPickups::tick(int x,int y,int z){
    m.w(0xa0a48,x);m.w(0xa0a4a,y);m.w(0xa0a4c,z);
    for(unsigned timer:{0xa0ae4u,0xa0abcu,0xa0ad6u,0xa0adau})if(m.w(timer)){
        m.w(timer,m.w(timer)-1);
        if(!m.w(timer)&&(timer==0xa0ad6u||timer==0xa0adau)){
            m.w(0xa0ab4,m.w(0xa0ab4)&(timer==0xa0ad6u?~0x100u:~0x200u));
            if(sound)sound(25);
        }
    }
    auto before=active();if(before.empty())return advanceMessages();
    auto service=[&](PlayerControlMemory&,unsigned address,unsigned a,unsigned,unsigned){if(address==0x40460&&sound)sound(a);return PlayerControlResult{};};
    // Static pickups need no full per-item map copies. Move flying drops through
    // recovered physics, then run recovered touch/cap/inventory/removal logic.
    std::vector<LevelCell> cells(m.w(0xa0c30)*unsigned(m.w(0xa0c32)));for(unsigned i=0;i<cells.size();++i)for(unsigned j=0;j<16;++j)cells[i].bytes[j]=m.b(m.d(0xa0c40)+i*16+j);LevelGrid grid{m.w(0xa0c30),m.w(0xa0c32),cells};
    for(auto p:before){if(m.w(p+8)||m.w(p+10)||m.w(p+12))moveDroppedPickup(m,p,service);else{auto gx=m.s(p)+m.w(0xa0c34),gz=m.s(p+4)+m.w(0xa0c36);m.w(p+2,floorHeight(grid,gx,gz)+m.w(0x9cda4+m.b(p+32)*2));}m.w(p+18,(m.w(p+18)+16)&4095);}
    auto flags=m.b(0x95f9c);m.b(0x95f9c,0);updateDroppedPickups(m,Task,service);m.b(0x95f9c,flags);auto after=active();
    for(auto p:before)if(std::find(after.begin(),after.end(),p)==after.end()){
        lastKind=m.b(p+32);lastX=m.s(p);lastZ=m.s(p+4);++collected;
        if(lastKind==25){m.w(0xa0ad6,900);m.w(0xa0ab4,m.w(0xa0ab4)|0x100u);}
        else if(lastKind==8){m.w(0xa0ada,900);m.w(0xa0ab4,m.w(0xa0ab4)|0x200u);}
        std::wstring text(name(lastKind));
        if(!text.empty()&&messages.size()<MaxQueuedMessages)messages.push_back({text,unsigned(text.size()),0});
    }
    return advanceMessages();
}
void ExplorerPickups::trigger(unsigned delta,unsigned index){
    if(index>=m.w(0x236c4c))return;auto rule=m.d(0x236c34)+index*8;auto count=static_cast<unsigned char>(m.b(rule+5)+delta);m.b(rule+5,std::min(count,m.b(rule+6)));if(m.b(rule+5)!=m.b(rule+6)||!m.d(0x236c38))return;
    auto p=m.d(0x236c38);m.d(0x236c38,m.d(p+40));for(unsigned i=0;i<48;++i)m.b(p+i,0);auto kind=m.b(rule+2);m.b(p+32,kind==24?21:kind);m.b(p+33,m.b(rule+3));m.b(p+34,m.b(rule+4));unsigned x=m.b(rule)*512+256,z=m.b(rule+1)*512+256,cell=m.d(0xa0c40)+((z>>9)*m.w(0xa0c30)+(x>>9))*16;
    m.w(p,x-m.w(0xa0c34));m.w(p+4,z-m.w(0xa0c36));m.w(p+2,m.b(cell+11)*32+m.w(0x9cda4+m.b(p+32)*2));m.d(p+28,cell);m.d(p+24,m.d(0x236bc4+m.b(p+32)*4));auto head=m.d(0x236c44);if(head)m.d(head+44,p);m.d(p+40,head);m.d(0x236c44,p);
}
void ExplorerPickups::dropSurvivalPickup(unsigned kind,unsigned amount,unsigned reserve,unsigned x,unsigned z){
    const auto pickup=m.d(0x236c38);
    if(!pickup||kind>=28||x>=m.w(0xa0c30)||z>=m.w(0xa0c32))return;
    m.d(0x236c38,m.d(pickup+40));
    for(unsigned i=0;i<48;++i)m.b(pickup+i,0);
    const auto cell=m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16;
    m.w(pickup,x*512+256-m.w(0xa0c34));
    m.w(pickup+4,z*512+256-m.w(0xa0c36));
    m.w(pickup+2,m.b(cell+11)*32+m.w(0x9cda4+kind*2));
    m.b(pickup+32,kind);m.b(pickup+33,amount);m.b(pickup+34,reserve);
    m.d(pickup+28,cell);m.d(pickup+24,m.d(0x236bc4+kind*4));
    const auto head=m.d(0x236c44);
    if(head)m.d(head+44,pickup);
    m.d(pickup+40,head);
    m.d(0x236c44,pickup);
}
void ExplorerPickups::enemyDrop(unsigned e){if(m.b(e+0x72)>=m.w(0x236c4c)||!m.d(0x236c38))return;spawnEntityDrop(m,e);}

// 0x3b8e4: a crate references pickup records, not pickup type numbers.
void ExplorerPickups::crateDrop(unsigned object,unsigned index){
 if(index>=m.w(0x236c4c)||!m.d(0x236c38))return;
 auto old=active();trigger(1,index);auto list=active();if(list.size()==old.size())return;auto p=list.front();
 m.w(p,m.w(object));m.w(p+4,m.w(object+4));m.d(p+28,m.d(object+20));m.w(p+2,m.b(m.d(p+28)+11)*32+m.w(0x9cda4+m.b(p+32)*2));
 int vx=0,vz=0;auto random=[&](){return int(nextGameRandom(m)&15)+16;};
 switch(m.w(object+34)){case 0:vz=-random();break;case 1:vx=random();vz=-random();break;case 2:vx=random();break;case 3:vx=random();vz=random();break;case 4:vz=random();break;case 5:vx=-random();vz=random();break;case 6:vx=-random();break;case 7:vx=-random();vz=-random();break;}
 vx+=int(nextGameRandom(m)&15)-8;vz+=int(nextGameRandom(m)&15)-8;m.w(p+8,vx);m.w(p+10,64);m.w(p+12,vz);m.w(p+36,32);
}

void ExplorerPickups::networkMessages(const std::vector<QueuedMessage>& value){if(value.size()>4)throw std::runtime_error("Invalid co-op pickup messages");for(auto& entry:value)if(entry.text.size()>128||entry.length!=entry.text.size()||entry.age>entry.length+40)throw std::runtime_error("Invalid co-op pickup message age");messages=value;syncMessageTable();}
