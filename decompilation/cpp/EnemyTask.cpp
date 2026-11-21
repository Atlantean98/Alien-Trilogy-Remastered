#include "EnemyAI.h"
#include <bit>
namespace trilogy::original {
void updateEnemyTask(PlayerControlMemory m,unsigned task,const PlayerControlService& f,bool survival){
    auto e=m.d(task+8);
    auto call=[&](unsigned routine,unsigned a,unsigned b=0,unsigned c=0){return f(m,routine,a,b,c).eax;};
    auto entity=[&](unsigned routine){return call(routine,e);};
    auto signedD=[&](unsigned p){return std::bit_cast<std::int32_t>(m.d(p));};
    auto stop=[&]{m.w(e+0x30,0);if(m.s(e+0x32)>0)m.w(e+0x32,0u-m.w(e+0x32));m.w(e+0x34,0);m.w(e+0x4c,0);};
    auto next=[&]{m.d(0x22cec0,m.d(task+24));};
    auto sight=[&](unsigned routine=0x31d30){return call(routine,e,static_cast<unsigned>(m.s(e+0x54)),m.d(e+0x60));};
    auto special=[&]{return m.b(e+0x74)>=11&&m.b(e+0x74)<=14;};
    auto face=[&](unsigned offset){m.w(e+0x44,m.w(e+offset));m.w(e+0x42,m.w(e+offset));m.b(e+0x6d,(m.s(e+0x42)+256)>>9&7);m.b(e+0x6e,255);entity(0x30ecc);};
    auto attack=[&]{call(0x336d0,task,e);};
    auto turn=[&]{call(0x328d8,e,static_cast<unsigned>(m.s(e+0x54)));entity(0x30ecc);if(m.w(e+0x42)==m.w(e+0x44))attack();};
    auto release=[&]{auto n=m.d(e+200),p=m.d(e+204);if(n)m.d(n+204,p);if(p)m.d(p+200,n);else m.d(0x2365e0,n);m.d(e+204,0);m.d(e+200,m.d(0x2365d8));m.d(0x2365d8,e);call(0x12394,task);};
    if(m.b(0x95f9c)&1){
        for(auto offset:{0x4eu,0x52u})if(m.w(e+offset))m.w(e+offset,m.w(e+offset)-1);
        if(m.d(e+0x68))m.d(e+0x68,m.d(e+0x68)-1);
        int verticalLimit=m.b(e+0x6c)&8?4096:1024;
        entity(0x31950);entity(0x31b68);
        if(m.b(e+0x73)==4&&!survival){
            if((m.b(0xa0c91)&128)&&m.b(e+0x6f)==8){auto source=m.d(0x2365d4)+m.b(e+0x77)*20u;auto kind=m.b(source);if(kind!=8&&kind!=9){auto cell=m.d(e+0x78);if(m.b(cell+10)==12)m.b(cell+10,0);auto health=call(0x2debc,source);m.b(e+0x6c,0);m.b(e+0x6f,0);m.w(e+0x52,32);m.w(e+0x58,health);entity(0x30ecc);call(0x2ee40,e,0);call(0x2840c,e+128);}}
            stop();next();return;
        }
        if(m.b(e+0x6f)!=8)m.w(e+0x54,call(0x26504,e,0xa0a48)&4095);
        switch(m.b(e+0x6f)){
        case 0:
            if(!m.w(e+0x52)&&(m.b(0x94b9d)&1))m.b(e+0x6f,2);
            else if(m.b(e+0x6c)&2)call(0x328d8,e,static_cast<unsigned>(m.s(e+0x54)));
            entity(0x30ecc);break;
        case 2:
            if(m.b(e+0x6c)&4){if(entity(0x33208)){m.b(e+0x6f,3);break;}m.b(e+0x6c,m.b(e+0x6c)&0xfb);}
            if(m.b(e+0x6c)&2){if(entity(0x329e0)||entity(0x32ddc)||entity(0x334e4))m.b(e+0x6f,3);else if(special()&&signedD(e+0x14)<=1536&&(!m.b(e+0x73)||sight(0x32500)))m.b(e+0x6f,4);}
            else if(entity(0x33484))m.b(e+0x6c,m.b(e+0x6c)|2);
            else if(entity(0x33430)){if(!m.b(e+0x73)&&signedD(e+0x14)<=1536&&sight())m.b(e+0x6f,4);else if(sight(0x32500)||sight())m.b(e+0x6c,m.b(e+0x6c)|2);}
            break;
        case 3:{
            entity(0x31c18);if(entity(0x31cbc)){stop();m.b(e+0x6f,survival?2:4);break;}
            if(!(m.b(e+0x6c)&4)&&!m.d(e+0x68)){
                if((!m.b(e+0x73)&&signedD(e+0x14)<=1536)||(m.b(e+0x73)>0&&m.b(e+0x73)<3&&sight(special()?0x32500:0x31d30))){m.d(e+0x68,60);stop();m.b(e+0x6f,4);break;}
            }
            m.w(e+0x56,call(0x26504,e,e+0x38)&4095);call(0x328d8,e,static_cast<unsigned>(m.s(e+0x56)));entity(0x30ecc);
            if(m.w(e+0x42)==m.w(e+0x44)){if(survival||call(0x31d30,e,static_cast<unsigned>(m.s(e+0x56)),m.d(e+0x64)))call(0x3298c,e,static_cast<unsigned>(m.s(e+0x42)),static_cast<unsigned>(m.s(e+0x5e)));else{stop();m.b(e+0x6f,0);m.w(e+0x52,8);}}
            break;}
        case 4:
            if((m.b(e+0x6c)&4)||m.b(e+0x73)>=3){m.b(e+0x6f,2);break;}
            if(!m.b(e+0x73)&&signedD(e+0x14)<=verticalLimit&&sight())turn();
            else if(special()){if(sight(0x32500))turn();else m.b(e+0x6f,2);}
            else if(signedD(e+0x14)<=verticalLimit&&sight())turn();else m.b(e+0x6f,2);
            break;
        case 5:
            call(0x328d8,e,static_cast<unsigned>(m.s(e+0x54)));
            if(survival&&m.b(e+0x74)==13){
                // Replay the native bullet emitter through the firing animation.
                if(m.b(e+0xb6)&8)m.d(task+16,3);
                const auto remaining=m.d(task+16);
                const bool burst=remaining!=0;
                if(burst)m.d(task+16,remaining-1);
                if(burst&&sight(0x32500))entity(0x2a8bc);
            }
            if(m.b(e+0xb6)&8){if(!survival||m.b(e+0x74)!=13)entity(m.b(e+0x6c)&1?0x2ab70:0x2a8bc);m.b(e+0x6c,m.b(e+0x6c)&0xfe);m.b(e+0xb6,m.b(e+0xb6)&0xf7);}
            if(m.b(e+0xb6)&2){
                if(survival&&m.b(e+0x74)==13&&m.d(task+16))break;
                stop();entity(0x33a0c);
                if(m.b(e+0x74)==2){m.b(e+0x6c,m.b(e+0x6c)|4);face(0x54);m.b(e+0x6f,2);break;}
                if(m.b(e+0x73)<3){
                    if(!m.b(e+0x73)&&signedD(e+0x14)<=1536&&sight()){face(0x54);attack();break;}
                    if(special()){
                        if(m.b(e+0x73)==1&&sight(0x32500)){face(0x54);attack();break;}
                        if(entity(0x329e0)){m.d(e+0x68,30);face(0x56);m.b(e+0x6f,3);break;}
                        if(sight(0x32500)){face(0x54);attack();break;}
                    }else{
                        if(sight()){face(0x54);attack();break;}
                        if(entity(0x329e0)){m.d(e+0x68,30);face(0x56);m.b(e+0x6f,3);break;}
                    }
                }else if(entity(0x329e0)){face(0x56);m.b(e+0x6f,3);break;}
                face(0x54);m.b(e+0x6f,0);m.w(e+0x52,4);
            }break;
        case 6:
            if(m.b(e+0xb6)&2){
                stop();if(m.b(e+0x74)==3){m.b(e+0x6c,m.b(e+0x6c)|4);face(0x54);m.b(e+0x6f,2);break;}
                if(m.b(e+0x73)<3&&((!m.b(e+0x73)&&signedD(e+0x14)<=1536)||sight(special()?0x32500:0x31d30))){face(0x54);attack();}
                else if(entity(0x334e4)){m.d(e+0x68,60);face(0x56);m.b(e+0x6f,3);}
                else{face(0x54);m.b(e+0x6f,0);m.w(e+0x52,8);}
            }break;
        case 7:
            if(m.b(e+0x6c)&32){release();return;}
            if(m.b(e+0xb6)&4){m.b(e+0x6f,8);stop();auto attr=m.b(m.d(e+0x78)+10);if(attr>=45&&attr<=52){auto direction=(attr-45)&3;auto speed=m.w(e+0x5e);if(direction==0)m.w(e+0x34,0u-speed);else if(direction==1)m.w(e+0x30,speed);else if(direction==2)m.w(e+0x30,0u-speed);else m.w(e+0x34,speed);}}
            break;
        case 8:{auto cell=m.d(e+0x78);if(m.b(cell+10)==6&&int(m.b(cell+13))-int(m.b(cell+11))<4){release();return;}
            if(m.b(cell+10)<45){stop();if((call(0x12ed4,e)&65535)&&m.b(e+0x74)>=2&&m.b(e+0x74)<=6&&!m.b(cell+10))m.b(cell+10,12);}break;}
        }
        if(!(m.b(e+0xb6)&6)&&m.b(e+0x6f)>2){
            const auto previous=m.d(e+0x98);
            call(0x2840c,e+128);
            if(m.b(e+0xb6)&16)call(0x40604,static_cast<unsigned>(m.s(e+0xb8)),e);
            else if(m.b(e+0x74)==5&&m.w(e+0xc4)<8&&m.d(e+0x98)<previous)
                call(0x40604,m.w(e+0xc4)==4?69:71,e);
        }
    }
    entity(0x2fd18);next();
}
}
