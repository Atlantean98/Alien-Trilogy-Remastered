#include "EnemyAI.h"
#include "ProjectileRuntime.h"
#include <bit>
namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;using Service=PlayerControlService;
// Shared helpers for the dedicated species tasks below.
struct Task {
    Mem m;unsigned task,e;const Service& f;
    unsigned call(unsigned routine,unsigned a,unsigned b=0,unsigned c=0){return f(m,routine,a,b,c).eax;}
    unsigned entity(unsigned routine){return call(routine,e);}
    unsigned stage(){return m.d(task+12);}
    void setStage(unsigned value){m.d(task+12,value);}
    void next(){m.d(0x22cec0,m.d(task+24));}
    void animate(unsigned animation){call(0x2ee40,e,animation);}
    void stop(){m.w(e+0x30,0);if(m.s(e+0x32)>0)m.w(e+0x32,0u-m.w(e+0x32));m.w(e+0x34,0);m.w(e+0x4c,0);}
    unsigned sight(unsigned routine=0x31d30){return call(routine,e,static_cast<unsigned>(m.s(e+0x54)),m.d(e+0x60));}
    void face(unsigned offset){m.w(e+0x44,m.w(e+offset));m.w(e+0x42,m.w(e+offset));m.b(e+0x6d,(m.s(e+0x42)+256)>>9&7);m.b(e+0x6e,255);entity(0x30ecc);}
    void attack(){call(0x336d0,task,e);}
    void tail(){
        if(!(m.b(e+0xb6)&6)){call(0x2840c,e+128);if(m.b(e+0xb6)&16)call(0x40604,static_cast<unsigned>(m.s(e+0xb8)),e);}
        entity(0x2fd18);next();
    }
    // Retire a finished corpse: unlink from the active list, return it to the free list, drop the task.
    void release(){auto n=m.d(e+200),p=m.d(e+204);if(n)m.d(n+204,p);if(p)m.d(p+200,n);else m.d(0x2365e0,n);m.d(e+204,0);m.d(e+200,m.d(0x2365d8));m.d(0x2365d8,e);call(0x12394,task);}
};
}
// 0x355d4: the Queen. Dormant until the player is close (or she is shot), then she
// unfolds, breaks off her perch and hunts the player with the generic enemy steps.
void updateQueenTask(PlayerControlMemory m,unsigned task,const PlayerControlService& f){
    Task t{m,task,m.d(task+8),f};auto e=t.e;
    if(m.w(e+0x4e))m.w(e+0x4e,m.w(e+0x4e)-1);
    if(m.w(e+0x52))m.w(e+0x52,m.w(e+0x52)-1);
    if(m.d(e+0x68))m.d(e+0x68,m.d(e+0x68)-1);
    t.entity(0x31950);t.entity(0x31b68);
    if(m.b(e+0x73)==4){t.stop();t.next();return;}
    if(m.b(e+0x6f)!=8)m.w(e+0x54,t.call(0x26504,e,0xa0a48)&4095);
    switch(m.b(e+0x6f)){
    case 0:{auto stage=t.stage();
        if(stage==3){if(!m.w(e+0x52)&&(m.b(0x94b9d)&1))m.b(e+0x6f,2);t.entity(0x30ecc);}
        else if(stage==2){if(m.b(e+0xb6)&2){t.setStage(3);t.entity(0x30ecc);}}
        else if(stage==1){if(m.b(e+0xb6)&2){
            if(m.b(e+0x73)<2){t.setStage(2);t.animate(10);}
            else{t.setStage(0);t.animate(8);m.w(e+0x52,0x80);}
        }}
        else if(!m.w(e+0x52)&&(m.b(0x94b9d)&1)&&m.b(e+0x73)<3){t.setStage(1);t.animate(9);}
        break;}
    case 2:
        if(!(m.b(e+0x6c)&2)){if(t.entity(0x33484)||(t.entity(0x33430)&&(!m.b(e+0x73)||t.sight())))m.b(e+0x6c,m.b(e+0x6c)|2);}
        else if(t.entity(0x329e0)||t.entity(0x32ddc)||t.entity(0x334e4))m.b(e+0x6f,3);
        break;
    case 3:
        t.entity(0x31c18);
        if(!t.entity(0x31cbc)){
            if(!m.d(e+0x68)&&!m.b(e+0x73)){m.d(e+0x68,60);t.stop();m.b(e+0x6f,4);}
            else if(!m.d(e+0x68)&&m.b(e+0x73)>0&&m.b(e+0x73)<3&&t.sight()){m.d(e+0x68,60);t.stop();m.b(e+0x6f,4);}
            else{
                m.w(e+0x56,t.call(0x26504,e,e+0x38)&4095);t.call(0x328d8,e,static_cast<unsigned>(m.s(e+0x56)));t.entity(0x30ecc);
                if(m.w(e+0x42)==m.w(e+0x44)){
                    if(!t.call(0x31d30,e,static_cast<unsigned>(m.s(e+0x56)),m.d(e+0x64))){t.stop();m.b(e+0x6f,0);m.w(e+0x52,8);}
                    else t.call(0x3298c,e,static_cast<unsigned>(m.s(e+0x42)),static_cast<unsigned>(m.s(e+0x5e)));
                }
            }
        }else{t.stop();m.b(e+0x6f,4);}
        break;
    case 4:
        if(m.b(e+0x73)<3){
            if(!m.b(e+0x73)){t.call(0x328d8,e,static_cast<unsigned>(m.s(e+0x54)));t.entity(0x30ecc);if(m.w(e+0x42)==m.w(e+0x44))t.attack();}
            else if(!t.sight())m.b(e+0x6f,2);
            else{t.call(0x328d8,e,static_cast<unsigned>(m.s(e+0x54)));t.entity(0x30ecc);if(m.w(e+0x42)==m.w(e+0x44))t.attack();}
        }else m.b(e+0x6f,2);
        break;
    case 5:
        t.call(0x328d8,e,static_cast<unsigned>(m.s(e+0x54)));
        if(m.b(e+0xb6)&2){
            m.b(e+0xb6,m.b(e+0xb6)&0xdf);t.stop();t.entity(0x33a0c);
            if(m.b(e+0x73)<3){
                if(!m.b(e+0x73)){t.face(0x54);t.attack();}
                else if(t.sight()){t.face(0x54);t.attack();}
                else if(t.entity(0x329e0)){m.d(e+0x68,30);t.face(0x56);m.b(e+0x6f,3);}
                else{t.face(0x54);m.b(e+0x6f,0);m.w(e+0x52,4);}
            }else if(t.entity(0x329e0)){t.face(0x56);m.b(e+0x6f,3);}
            else{t.face(0x54);m.b(e+0x6f,0);m.w(e+0x52,4);}
        }
        break;
    case 6:
        if(m.b(e+0xb6)&2){
            t.stop();
            if(t.stage()==2){t.setStage(3);t.entity(0x30ecc);}
            if(t.stage()<2){t.setStage(2);t.animate(10);m.b(e+0x6f,0);m.w(e+0x52,0x40);}
            else if(m.b(e+0x73)<3&&(!m.b(e+0x73)||t.sight())){t.face(0x54);t.attack();}
            else if(!t.entity(0x334e4)){t.face(0x54);m.b(e+0x6f,0);m.w(e+0x52,8);}
            else{m.d(e+0x68,60);t.face(0x56);m.b(e+0x6f,3);}
        }
        break;
    case 7:
        if(m.b(e+0xb6)&4){
            m.b(e+0x6f,8);auto attr=m.b(m.d(e+0x78)+10);
            if(attr>=45&&attr<=52){auto direction=(attr-45)&3;auto speed=m.w(e+0x5e);
                if(direction==0)m.w(e+0x34,0u-speed);else if(direction==1)m.w(e+0x30,speed);else if(direction==2)m.w(e+0x30,0u-speed);else m.w(e+0x34,speed);
                if(direction==0||direction==3)m.w(e+0x30,0);else m.w(e+0x34,0);}
        }
        break;
    case 8:{
        // The Queen's serialized link is a level command chain (e.g. unlock the exit); run it once.
        if(m.b(e+0x72)!=255){auto action=m.b(e+0x72);m.b(e+0x72,255);t.call(0x406ec,action);}
        auto cell=m.d(e+0x78);
        if(m.b(cell+10)==6&&int(m.b(cell+13))-int(m.b(cell+11))<4){t.release();return;}
        if(m.b(cell+10)<45){t.stop();if((t.call(0x12ed4,e)&65535)&&!m.b(cell+10))m.b(cell+10,12);}
        break;}
    }
    t.tail();
}
// 0x35ebc: cocooned/impregnated colonists. They turn to follow the player, wake and burst
// when approached, flinch when shot, and fall into a corpse after dying.
void updateColonistTask(PlayerControlMemory m,unsigned task,const PlayerControlService& f){
    Task t{m,task,m.d(task+8),f};auto e=t.e;
    t.entity(0x31b68);
    if(m.b(e+0x73)==4){t.next();return;}
    if(m.s(e+0x4e)>0)m.w(e+0x4e,m.w(e+0x4e)-1);
    if(m.s(e+0x52)>0)m.w(e+0x52,m.w(e+0x52)-1);
    m.w(e+0x54,t.call(0x26504,e,0xa0a48)&4095);
    unsigned relative=(m.b(e+0x6d)-(((m.s(e+0x54)+0x900)>>9)&7))&7;
    if(m.b(e+0x6f)<9)switch(m.b(e+0x6f)){
    case 0:
        if(m.w(e+0x52)==0){
            unsigned timer;
            if(m.b(e+0x73)<2&&relative==4){t.animate(0x1a);m.b(e+0x6f,3);timer=(nextGameRandom(m)&0x7f)+0x28;}
            else{
                if(m.b(e+0x73)>2)break;
                if(relative==4)t.animate(0x18);
                m.b(e+0x6f,2);timer=(nextGameRandom(m)&0x7f)+0x78;
            }
            m.w(e+0x52,timer);
        }else if(m.b(e+0x6e)!=relative){m.b(e+0x6e,relative);t.animate(relative+8);}
        break;
    case 2:case 3:
        if(!(m.b(e+0xb6)&2)&&relative==4)break;
        if(m.b(e+0x6f)==2){t.animate(relative+8);m.b(e+0x6f,0);break;}
        m.b(e+0x6f,4);
        {   // 0x3500c: the host bursts and its linked chestburster leaps out.
            auto record=m.w(e+0x5e);m.w(e+0x5e,0xff);
            if(record!=0xff){
                auto spawn=m.d(0x2365d4)+record*20u;
                m.b(spawn+1,(m.s(e)+m.w(0xa0c34))>>9);m.b(spawn+2,(m.s(e+4)+m.w(0xa0c36))>>9);
                if(auto burster=spawnEnemyFromRecord(m,static_cast<std::uint16_t>(record),f)){
                    m.w(burster+0x44,m.w(e+0x54));m.w(burster+0x42,m.w(e+0x54));
                    m.b(burster+0x6d,(m.s(burster+0x42)+256)>>9&7);m.b(burster+0x6e,m.b(burster+0x6d));
                    m.b(burster+0x6c,m.b(burster+0x6c)|2);m.w(burster+0x52,0);
                    t.call(0x3298c,burster,m.w(burster+0x42),0x40);m.w(burster+0x32,0x60);m.b(burster+0x6f,5);
                    t.call(0x2ee40,burster,0xd);m.w(burster+2,m.w(burster+0x5a)+0x40);
                    t.call(0x3298c,burster,m.w(burster+0x42),0x60);m.w(burster+0x32,0x78);
                }
            }
        }
        if(m.b(e+0x72)!=255)t.call(0x3b61c,e);
        break;
    case 4:
        if(m.b(e+0x6e)!=relative){m.b(e+0x6e,relative);t.animate(relative);}
        break;
    case 6:
        if(m.b(e+0xb6)&2){t.animate(relative+8);m.b(e+0x6f,0);m.w(e+0x52,8);}
        break;
    case 7:
        if(m.b(e+0xb6)&4){m.b(e+0x6f,8);t.animate(relative+0x1b);}
        break;
    case 8:
        if(m.b(e+0x6e)!=relative){m.b(e+0x6e,relative);t.animate(relative+0x1b);}
        break;
    }
    t.tail();
}
// 0x35088: eggs open nearby, then release only their authored linked record.
void updateEggTask(PlayerControlMemory m,unsigned task,const PlayerControlService& f){
    Task t{m,task,m.d(task+8),f};auto e=t.e;
    if(m.w(e+0x4e))m.w(e+0x4e,m.w(e+0x4e)-1);
    if(m.w(e+0x52))m.w(e+0x52,m.w(e+0x52)-1);
    t.entity(0x31950);t.entity(0x31b68);
    if(m.b(e+0x73)==4){t.next();return;}
    if(m.b(e+0x6f)!=8)m.w(e+0x54,t.call(0x26504,e,0xa0a48)&4095);
    switch(m.b(e+0x6f)){
    case 0:if(!m.w(e+0x52)&&(m.b(e+0xb6)&4))m.b(e+0x6f,2);break;
    case 2:
        if(m.b(e+0x73)<3){t.animate(2);t.setStage(1);m.b(e+0x6f,3);}
        else{t.animate(0);m.b(e+0x6f,0);m.w(e+0x52,(nextGameRandom(m)&127)+64);}
        break;
    case 3:if(m.b(e+0xb6)&4){t.setStage(2);m.b(e+0x6f,4);}break;
    case 4:
        if(m.b(e+0x73)<2&&t.sight(0x32500)){
            auto record=m.b(e+0x72);unsigned child=0;
            if(record!=255&&record<m.w(0x2365f0)){
                auto source=m.d(0x2365d4)+record*20;
                m.b(source+1,(m.s(e)+m.w(0xa0c34))>>9);m.b(source+2,(m.s(e+4)+m.w(0xa0c36))>>9);
                child=spawnEnemyFromRecord(m,record,f);
                if(child){m.w(child+0x44,m.w(e+0x54));m.w(child+0x42,m.w(e+0x54));auto facing=((m.s(child+0x42)+256)>>9)&7;m.b(child+0x6d,facing);m.b(child+0x6e,facing);m.b(child+0x6c,m.b(child+0x6c)|2);m.w(child+0x52,0);t.call(0x3298c,child,m.w(child+0x42),64);m.w(child+0x32,96);m.b(child+0x6f,5);}
            }
            m.b(e+0x72,255);if(child)t.call(0x2ee40,child,10);t.setStage(3);t.entity(0x30518);
        }break;
    case 6:
        if(!m.w(e+0x4e)){
            if(t.stage()==1)m.b(e+0x6f,3);
            else if(m.b(e+0xb6)&2){if(!t.stage()){t.animate(2);t.setStage(1);m.b(e+0x6f,3);}else if(t.stage()==2)m.b(e+0x6f,4);}
        }break;
    case 7:if(m.b(e+0xb6)&4)m.b(e+0x6f,8);break;
    }
    t.tail();
}

}
