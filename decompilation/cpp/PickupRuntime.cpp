#include "PickupRuntime.h"
#include "WeaponRuntime.h"
#include "WorldMovement.h"
#include <bit>
#include <vector>

namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;using Service=PlayerControlService;
unsigned sx(int n){return static_cast<unsigned>(n);}
int word(unsigned n){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}
int integer(unsigned n){return std::bit_cast<std::int32_t>(n);}
int distance(int a,int b){auto n=a-b;return n<0?-n:n;}
void call(Mem m,const Service& f,unsigned routine,unsigned a=0,unsigned b=0,unsigned c=0){f(m,routine,a,b,c);}
int heightAt(Mem m,unsigned x,unsigned z,bool ceiling=false){
    auto width=m.w(0xa0c30),height=m.w(0xa0c32);std::vector<LevelCell> cells(static_cast<unsigned>(width)*height);
    for(unsigned i=0;i<cells.size();++i)for(unsigned j=0;j<16;++j)cells[i].bytes[j]=m.b(m.d(0xa0c40)+i*16+j);
    LevelGrid grid{width,height,cells};return ceiling?ceilingHeight(grid,x,z):floorHeight(grid,x,z);
}
unsigned cellAt(Mem m,unsigned x,unsigned z){return m.d(0xa0c40)+((z>>9)*m.w(0xa0c30)+(x>>9))*16;}
unsigned clipped(unsigned coordinate,bool negative){auto base=coordinate&0xfffffe00u;return negative?base+512:base-1;}
void cap(Mem m,unsigned a,int maximum){if(m.s(a)>maximum)m.w(a,maximum);}
void restoreOwnedSlot(Mem m,unsigned slot,unsigned flag){auto a=0x2368fa+slot*4;if(!m.w(a)&&(m.b(0xa0ab4)&flag))m.w(a,65535);}
}
void releaseDroppedPickup(Mem m,unsigned p){m.d(p+0x28,m.d(0x236c38));m.d(0x236c38,p);}
void resetDroppedPickupPool(Mem m,const Service& f){
    m.d(0x236c38,0);m.d(0x236c44,0);auto p=m.d(0x236c40);for(unsigned i=0;i<128;++i,p+=48)releaseDroppedPickup(m,p);
    if(m.w(0xa0c1c)){call(m,f,0x3b2bc,m.d(0xa0c54),sx(m.s(0xa0c1c)));call(m,f,0x3af20);}
}
void moveDroppedPickup(Mem m,unsigned p,const Service& f){
    if(!(m.b(0x95f9c)&1)||(m.b(0x9e0a8)&&!m.b(0x3f953e)))return;
    auto x=sx(m.s(p))+m.w(0xa0c34),z=sx(m.s(p+4))+m.w(0xa0c36);auto floor=sx(heightAt(m,x,z));bool moved=false;
    for(unsigned axis=0;axis<2;++axis){
        auto velocity=p+(axis?12:8);if(!m.w(velocity))continue;moved=true;
        auto candidate=(axis?z:x)+sx(m.s(velocity));auto cell=cellAt(m,axis?x:candidate,axis?candidate:z);auto attr=m.b(cell+10);
        if(!m.d(cell)||m.b(cell+12)>=20||(attr>=20&&attr<45))candidate=clipped(candidate,m.s(velocity)<0);
        else if(attr<20){
            auto newFloor=sx(word(sx(heightAt(m,axis?x:candidate,axis?candidate:z))));
            auto difference=newFloor>floor?newFloor-floor:floor-newFloor;
            if(word(difference)>256)candidate=clipped(candidate,m.s(velocity)<0);
            if(m.b(cell+10)==6&&word(sx(heightAt(m,axis?x:candidate,axis?candidate:z,true)))<m.s(p+2)+128)candidate=clipped(candidate,m.s(velocity)<0);
        }
        if(axis){z=candidate;m.w(p+4,z-m.w(0xa0c36));}else{x=candidate;m.w(p,x-m.w(0xa0c34));}
    }
    x=sx(m.s(p))+m.w(0xa0c34);z=sx(m.s(p+4))+m.w(0xa0c36);auto ground=heightAt(m,x,z)+m.w(0x9cda4+m.b(p+0x20)*2);
    if(!m.w(p+10))m.w(p+2,ground);
    else{
        moved=true;m.w(p+2,m.w(p+2)+m.w(p+10));auto vertical=m.s(p+10);
        if(m.s(p+2)<word(sx(ground))){m.w(p+2,ground);m.w(p+0x24,m.w(p+0x24)-8u);vertical=m.s(p+0x24)<4?0:m.s(p+0x24);m.w(p+8,0);m.w(p+12,0);}
        else vertical=vertical==4?-2:vertical-4;
        m.w(p+10,vertical);
    }
    m.d(p+0x1c,cellAt(m,x,z));if(moved)call(m,f,0x5225c,sx(integer(p-m.d(0x236c40))/48),p);
}
std::uint32_t canTouchDroppedPickup(Mem m,unsigned p){
    auto flags=m.d(0xa0c90);if((flags&0x80060)||((flags&8)&&m.b(0x9e0a8))||(m.s(p)==-1&&m.s(p+4)==-1))return 0;
    auto kind=m.b(p+0x20);if(m.b(0x9e0a8)&&kind<5&&(m.b(0xa0ab4)&(4u<<kind)))return 0;
    return distance(m.s(p),m.s(0xa0a48))<768&&distance(m.s(p+2),m.s(0xa0a4a))<1024&&distance(m.s(p+4),m.s(0xa0a4c))<768?1:0;
}
void resetWeaponSlots(Mem m){m.w(0x236a3a,0);for(unsigned i=0;i<5;++i){m.w(0x2368f8+i*4,i);m.w(0x2368fa+i*4,0);}}
void syncCurrentWeaponSlot(Mem m){m.w(0x236a3a,m.w(0xa0aae));}
void awardPickup(Mem m,std::uint16_t kind,std::uint16_t amount,std::uint16_t reserve){
    auto add=[&](unsigned a,unsigned n,int maximum){m.w(a,m.w(a)+n);cap(m,a,maximum);};
    switch(kind){
        case 0:m.b(0xa0ab4,m.b(0xa0ab4)|4);add(0xa0abe,amount,15);add(0xa0ac0,reserve,9);m.w(0x2368fa,65535);break;
        case 1:m.b(0xa0ab4,m.b(0xa0ab4)|8);add(0xa0ac2,amount,100);m.w(0x2368fe,m.w(0xa0ac2));break;
        case 2:m.b(0xa0ab4,m.b(0xa0ab4)|16);add(0xa0ac4,amount,100);add(0xa0ac6,reserve,9);m.w(0x236902,m.w(0xa0ac4)||m.w(0xa0ac8)?1:0);break;
        case 3:m.b(0xa0ab4,m.b(0xa0ab4)|32);add(0xa0aca,amount,500);m.w(0x23690a,m.w(0xa0aca));break;
        case 4:m.b(0xa0ab4,m.b(0xa0ab4)|64);add(0xa0ace,reserve,9);add(0xa0acc,amount,100);m.w(0x236906,m.w(0xa0acc));break;
        case 6:m.w(0xa0ad0,m.w(0xa0ad0)+amount);break;
        case 7:m.w(0xa0ad2,m.w(0xa0ad2)+1u);break;
        case 8:m.w(0xa0ad8,m.w(0xa0ad8)+1u);break;
        case 9:add(0xa0abe,amount,15);add(0xa0ac0,reserve,9);restoreOwnedSlot(m,0,4);break;
        case 10:add(0xa0ac2,amount,100);restoreOwnedSlot(m,1,8);break;
        case 11:add(0xa0ac4,amount,100);add(0xa0ac6,reserve,9);restoreOwnedSlot(m,2,16);break;
        case 12:add(0xa0ac8,amount,20);restoreOwnedSlot(m,2,16);break;
        case 13:add(0xa0aca,100,500);restoreOwnedSlot(m,4,32);break;
        case 14:add(0xa0acc,amount,100);add(0xa0ace,reserve,9);restoreOwnedSlot(m,3,64);break;
        case 15:m.w(0xa0ae2,m.w(0xa0ae2)+1u);break;
        case 16:m.b(0xa0ab4,m.b(0xa0ab4)|128);break;
        case 17:if(m.s(0xa0ab8)<100)m.w(0xa0ab8,100);m.w(0xa0ae4,900);break;
        case 18:case 19:m.w(0xa0aba,amount);break;
        case 20:add(0xa0ab8,amount,100);break;
        case 21:m.w(0xa0ab8,m.w(0xa0ab8)+amount);break;
        case 22:m.w(0xa0abc,m.w(0xa0abc)+amount*30u);break;
        case 23:m.w(0xa0ab8,200);break;
        case 25:m.w(0xa0ad4,m.w(0xa0ad4)+1u);break;
    }
}
void refreshCollectedWeapon(Mem m,std::uint16_t weapon){if(weapon==m.w(0xa0aae)&&!m.w(0xa0aac)&&(weapon==0||weapon==2||weapon==3)){m.w(0xa0aac,2);startWeaponAnimation(m);}}
void updateDroppedPickups(Mem m,unsigned task,const Service& f){
    int weapon=-1,sound=40;
    for(auto p=m.d(0x236c44);p;){
        moveDroppedPickup(m,p,f);auto next=m.d(p+0x28);bool collect=false;
        if(canTouchDroppedPickup(m,p)){
            auto kind=m.b(p+0x20);
            if(kind<5){constexpr int sounds[]={12,15,5,10,18};sound=sounds[kind];collect=true;}
            else switch(kind){
                case 6:case 7:case 8:case 15:case 17:case 21:case 22:case 25:collect=true;break;
                case 9:collect=m.s(0xa0abe)<15||m.s(0xa0ac0)<9;sound=12;weapon=0;break;
                case 10:collect=m.s(0xa0ac2)<100;sound=15;weapon=1;break;
                case 11:collect=m.s(0xa0ac4)<100||m.s(0xa0ac6)<9;sound=5;weapon=2;break;
                case 12:collect=m.s(0xa0ac8)<20;sound=7;break;
                case 13:collect=m.s(0xa0aca)<500;sound=10;weapon=4;break;
                case 14:collect=m.s(0xa0acc)<100||m.s(0xa0ace)<9;sound=18;weapon=3;break;
                case 16:collect=!(m.b(0xa0ab4)&128);break;
                case 18:case 19:collect=m.s(0xa0aba)<m.b(p+0x21);break;
                case 20:collect=m.s(0xa0ab8)<100;break;
                case 23:collect=m.s(0xa0ab8)<200;break;
            }
        }
        if(collect){
            if(!m.b(0x9e0a8)||m.b(0x3f953e)){call(m,f,0x3ab6c,m.d(m.d(0x9cd34+m.b(p+0x20)*4)));call(m,f,0x40460,sx(sound),0xffffffff,0x3fff);}
            if(m.b(0x9e0a8)){
                if(!m.b(0x3f953e))call(m,f,0x52138,sx(integer(p-m.d(0x236c40))/48));
                else{m.w(p,65535);m.w(p+2,65535);m.w(p+4,65535);awardPickup(m,m.b(p+0x20),m.b(p+0x21),m.b(p+0x22));auto index=sx(integer(p-m.d(0x236c40))/48);m.b(0x236a44+index,1);m.b(0x236b44+index,m.b(p+0x20));call(m,f,0x5225c,index,p);}
            }
            if(!m.b(0x9e0a8)){
                auto following=m.d(p+0x28),previous=m.d(p+0x2c);if(following)m.d(following+0x2c,previous);if(previous)m.d(previous+0x28,following);else m.d(0x236c44,following);
                m.d(p+0x2c,0);m.d(p+0x28,0);releaseDroppedPickup(m,p);awardPickup(m,m.b(p+0x20),m.b(p+0x21),m.b(p+0x22));
            }
            if(weapon!=-1)refreshCollectedWeapon(m,static_cast<std::uint16_t>(weapon));
        }else if(m.b(0x95f9c)==1)call(m,f,0x3c074,p);
        p=next;
    }
    m.d(0x22cec0,m.d(task+24));
}
PlayerControlService withPickupRuntime(const Service& external){
    return [external](Mem& m,unsigned routine,unsigned a,unsigned b,unsigned c)->PlayerControlResult{
        switch(routine){
            case 0x3b0ac:releaseDroppedPickup(m,a);break;
            case 0x3b43c:resetDroppedPickupPool(m,external);break;
            case 0x3bbd8:moveDroppedPickup(m,a,external);break;
            case 0x3bf50:return {canTouchDroppedPickup(m,a),0};
            case 0x380c0:resetWeaponSlots(m);break;case 0x380f4:syncCurrentWeaponSlot(m);break;
            case 0x38170:awardPickup(m,static_cast<std::uint16_t>(a),static_cast<std::uint16_t>(b),static_cast<std::uint16_t>(c));return {};
            case 0x3f8c8:refreshCollectedWeapon(m,static_cast<std::uint16_t>(a));break;
            case 0x3c250:updateDroppedPickups(m,a,external);break;
            default:return external(m,routine,a,b,c);
        }
        return {};
    };
}
}
