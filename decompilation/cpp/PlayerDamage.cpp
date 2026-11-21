#include "PlayerDamage.h"
#include "ProjectileRuntime.h"
#include <bit>

namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;using Service=PlayerControlService;
unsigned sx(int n){return static_cast<unsigned>(n);}
std::int16_t word(unsigned n){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}
PlayerControlResult call(Mem m,const Service& f,unsigned address,unsigned a=0,unsigned b=0,unsigned c=0){return f(m,address,a,b,c);}
}
void setDamageFeedback(Mem m,std::uint16_t type){m.w(0x22d3a8,type);}
void markPlayerHurt(Mem m){if(m.b(0x9e0a8))m.w(0x2365e8,4);}
void markPlayerDead(Mem m){if(m.b(0x9e0a8))m.w(0x2365e8,5);}
void playPlayerDamageVoice(Mem m,const Service& f) {
    unsigned sound;
    if(m.s(0xa0ab8)<=0){if(!(nextGameRandom(m)&3))return;sound=23;}
    else if(m.s(0xa0ab8)<=15)sound=9;
    else if((nextGameRandom(m)&15)<5)sound=nextGameRandom(m)&1?5:36;
    else {if(!(nextGameRandom(m)&1))return;sound=12;}
    call(m,f,0x560a4,sound);
}
void applyPlayerDamage(Mem m,std::uint16_t amount,std::uint16_t type,const Service& f) {
    m.d(0x3f94f8,0xffffffff);int sound=-1;
    if(!m.w(0xa0b30)&&!m.w(0xa0ae4)) {
        if(m.s(0xa0aba)>0&&type==3)sound=36;
        setDamageFeedback(m,type);
        if(!(m.b(0x94b9c)&4)) {
            if(m.w(0xa0aba)) {
                m.w(0xa0aba,m.w(0xa0aba)-amount);if(m.s(0xa0aba)<=0)m.w(0xa0aba,0);
            }else {
                m.w(0xa0ab8,m.w(0xa0ab8)-amount);playPlayerDamageVoice(m,f);markPlayerHurt(m);sound=3;
                if(m.s(0xa0ab8)<=0){m.b(0xa0c90,m.b(0xa0c90)|32);m.w(0xa0ab8,0);markPlayerDead(m);}
            }
        }
        m.w(0xa0b30,m.b(0x9e0a8)?8:30);
    }
    if(sound!=-1)call(m,f,0x40460,sound,0xffffffff,0x3fff);
}
void damagePlayer(Mem m,std::uint16_t amount,std::uint16_t type,const Service& f){m.d(0x3f94f8,0xffffffff);applyPlayerDamage(m,amount,type,f);}
void damagePlayerDirect(Mem m,std::uint16_t amount,std::uint16_t type,const Service& f){applyPlayerDamage(m,amount,type,f);}
void damagePlayerDirectional(Mem m,std::uint16_t amount,std::uint16_t yaw,std::uint16_t type,const Service& f) {
    auto health=m.w(0xa0ab8);m.d(0x3f94f8,0xffffffff);applyPlayerDamage(m,amount,type,f);
    if(health!=m.w(0xa0ab8)){m.w(0xa0ae0,yaw);m.w(0xa0ade,128);}
}
void updatePlayerStatus(Mem m,const Service& f,const PlayerStatusSprite& draw) {
    m.b(0x236c95,m.b(0x3fa91f));
    for(unsigned a:{0xa0ae4u,0xa0b30u,0xa0adcu,0xa0abcu})if(m.w(a))m.w(a,m.w(a)-1u);
    for(unsigned a:{0xa0ad6u,0xa0adau})if(m.w(a)) {
        m.w(a,m.w(a)-1u);if(!m.w(a)){m.b(0xa0c91,m.b(0xa0c91)|16);call(m,f,0x3c8a8,1);call(m,f,0x403ec,25,0xffffffff,0x3fff);}
    }
    if(m.b(0x95f9c)==1)call(m,f,0x3a120);
    call(m,f,0x3a444);
    auto sprite=[&](unsigned resource,int x,int y){draw(m,m.d(resource),word(sx(x)),word(sx(y)),255);};
    if((m.b(0xa0c92)&2)&&!(m.b(0xa0c90)&128)&&(m.b(0xa0bb4)&32)&&m.b(0x95f9c)==1)sprite(0x3f98e4,132,44);
    if(m.b(0x9d53c)&&m.b(0x95f9c)==1)sprite(0x3f98e8,8,m.b(0x9e0a8)?231:8);
    if(m.w(0xa0ad2)&&m.b(0x95f9c)==1&&m.b(0x9e0a8)==1)sprite(0x3f98ec,80,231);
    if(m.b(0x9e0a8)&&m.b(0x95f9c)==1) {
        if(m.b(0xa0cc7)==1) {
            auto text=m.d(0x3f98f0);auto width=std::bit_cast<std::int32_t>(call(m,f,0x18450,text).eax);
            draw(m,m.d(0x3f98f0),word(160u-sx(width>>1)),100,255);
            if(m.b(0x3e88ed)) {
                m.b(0xa0c90,m.b(0xa0c90)|8);m.b(0xa0cc6,1);m.b(0xa0cc7,0);
                call(m,f,0x512d0,7);call(m,f,0x51808);call(m,f,0x52fb0);
            }
            if(m.b(0x3e8909))m.b(0xa0cc7,0);
        }
        call(m,f,0x55dfc);
    }
    call(m,f,0x50c40);
}
PlayerControlService withPlayerDamageRuntime(const Service& external) {
    return [external](Mem& m,unsigned address,unsigned a,unsigned b,unsigned c)->PlayerControlResult {
        switch(address) {
            case 0x157c4:setDamageFeedback(m,static_cast<std::uint16_t>(a));break;
            case 0x34ab4:markPlayerHurt(m);break;case 0x34acc:markPlayerDead(m);break;
            case 0x3df58:playPlayerDamageVoice(m,external);break;
            case 0x3dde8:applyPlayerDamage(m,static_cast<std::uint16_t>(a),static_cast<std::uint16_t>(b),external);break;
            case 0x3dee8:damagePlayer(m,static_cast<std::uint16_t>(a),static_cast<std::uint16_t>(b),external);break;
            case 0x3df04:damagePlayerDirect(m,static_cast<std::uint16_t>(a),static_cast<std::uint16_t>(b),external);break;
            case 0x3df14:damagePlayerDirectional(m,static_cast<std::uint16_t>(a),static_cast<std::uint16_t>(b),static_cast<std::uint16_t>(c),external);break;
            case 0x3dfc0:updatePlayerStatus(m,external,[external](Mem& memory,unsigned resource,std::int16_t x,std::int16_t y,std::uint8_t){external(memory,0x42ea4,resource,sx(x),sx(y));});break;
            default:return external(m,address,a,b,c);
        }
        return {};
    };
}
}
