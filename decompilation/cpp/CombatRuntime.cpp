#include "CombatRuntime.h"
#include "ProjectileRuntime.h"
#include "WeaponRuntime.h"
#include "WorldMovement.h"
#include <bit>

namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;using Service=PlayerControlService;
unsigned sx(int n){return static_cast<unsigned>(n);}
int word(unsigned n){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}
void call(Mem m,const Service& f,unsigned address,unsigned a=0,unsigned b=0,unsigned c=0){f(m,address,a,b,c);}
void reverseVertical(Mem m,unsigned e){if(m.s(e+0x32)>0)m.w(e+0x32,0u-m.w(e+0x32));}
void haltHorizontal(Mem m,unsigned e){m.w(e+0x30,0);reverseVertical(m,e);m.w(e+0x34,0);m.w(e+0x4c,0);}
void kick(Mem m,unsigned e){applyEntityKnockback(m,e,(m.w(e+0x54)+2048u)&4095,static_cast<std::uint16_t>(m.s(e+0x5e)>>2));}
void deathMotion(Mem m,unsigned e){if(m.b(m.d(e+0x78)+10)<45)kick(m,e);else haltHorizontal(m,e);}
}
void saveTransformState(Mem m){for(unsigned i=0;i<9;++i)m.w(0x23ad28+i*2,m.w(0x23ad08+i*2));for(unsigned i=0;i<6;++i)m.w(0x23ad3c+i*2,m.w(0x23ad1c+i*2));}
void restoreTransformState(Mem m){for(unsigned i=0;i<9;++i)m.w(0x23ad08+i*2,m.w(0x23ad28+i*2));for(unsigned i=0;i<6;++i)m.w(0x23ad1c+i*2,m.w(0x23ad3c+i*2));}
void applyEntityKnockback(Mem m,unsigned e,std::uint16_t yaw,std::uint16_t speed) {
    m.w(e+0x30,0);auto vertical=m.w(e+0x32);m.w(e+0x34,speed);
    m.w(0x236328,0);m.w(0x23632a,(yaw+2048u)&4095);m.w(0x23632c,0);
    saveTransformState(m);
    std::array<std::int16_t,4096> sine;for(unsigned i=0;i<4096;++i)sine[i]=static_cast<std::int16_t>(m.s(0x9763c+i*2));
    Vec3s angles{0,static_cast<std::int16_t>(m.s(0x23632a)),0};
    Vec3s velocity{0,static_cast<std::int16_t>(word(vertical)),static_cast<std::int16_t>(word(speed))};
    auto matrix=buildRotation(angles,sine);for(unsigned i=0;i<9;++i)m.w(0xa0a28+i*2,matrix[i]);
    auto result=rotateVector(angles,velocity,sine);for(unsigned i=0;i<3;++i)m.w(e+0x30+i*2,result[i]);
    restoreTransformState(m);m.w(e+0x32,vertical);
}
void startEntityAnimation(Mem m,unsigned e,std::int16_t animation) {
    auto entry=m.d(e+0x7c)+sx(animation*8),sprite=e+0x80,program=m.d(entry+4);
    m.b(sprite+8,0);m.b(sprite,0);m.w(sprite+2,m.w(m.d(sprite+0x20)));
    m.b(sprite+0x36,m.b(sprite+0x36)&0xf0);m.d(sprite+0x10,0);m.w(sprite+0x30,1);m.d(sprite+0x14,m.d(entry));
    m.w(sprite+0x3a,0);m.w(sprite+0x32,m.w(program));m.d(sprite+0x18,program+2);m.d(sprite+0x1c,program+4);
    m.w(e+0xc4,animation);m.w(sprite+0x3c,0);advanceAnimation(m,sprite);
}
void enterEntityPain(Mem m,unsigned e,const Service&) {
    auto kind=m.b(e+0x74);if(kind==8||kind==9)return;
    m.b(e+0x6f,6);m.w(e+0x4e,kind==7?18:16);reverseVertical(m,e);
    switch(m.b(e+0x74)) {
        case 1:{auto state=m.d(m.d(e+0xc0)+12);if(state==0||state==2)startEntityAnimation(m,e,state==0?3:4);break;}
        case 2:case 11:case 12:case 14:kick(m,e);startEntityAnimation(m,e,8);break;
        case 3:case 4:case 5:case 6:case 13:{
            if(m.b(e+0x74)==13&&m.b(0x9e0a8))break;
            kick(m,e);auto facing=m.b(e+0x6e);startEntityAnimation(m,e,facing==0||facing==7||facing==1?8:9);break;
        }
        case 7:{auto state=m.d(m.d(e+0xc0)+12);if(state==2)m.w(e+0x4e,64);else{startEntityAnimation(m,e,state<2?16:17);m.b(e+0xb6,m.b(e+0xb6)&0xdf);}break;}
        case 10:if(m.b(e+0x6f)!=4)startEntityAnimation(m,e,16+m.b(e+0x6e));break;
    }
}
void enterEntityDeath(Mem m,unsigned e,const Service& f) {
    m.b(e+0x6f,7);m.b(m.d(e+0x78)+12,0);
    auto counter=0xa0ae6+m.b(e+0x74)*2u;m.w(counter,m.w(counter)+1u);
    auto kind=m.b(e+0x74);
    if(kind==1&&m.b(e+0x72)!=255)m.w(0xa0aea,m.w(0xa0aea)+1u);
    else if(kind==10&&m.w(e+0x5e)!=255)m.w(0xa0aec,m.w(0xa0aec)+1u);
    kind=m.b(e+0x74);if(kind>=16&&kind<=19)return;
    if(kind!=1&&kind!=7&&m.b(e+0x72)!=255)call(m,f,0x3b61c,e);
    reverseVertical(m,e);kind=m.b(e+0x74);
    if(kind==1){startEntityAnimation(m,e,1);return;}
    if(kind==8||kind==9){haltHorizontal(m,e);m.b(e+0x6c,m.b(e+0x6c)&0xf7);m.b(e+0x74,kind==8?6:5);startEntityAnimation(m,e,kind==8?8:10);return;}
    if(kind==10){startEntityAnimation(m,e,25);return;}
    if(kind==13&&m.b(0x9e0a8))return;
    if(kind>=2&&kind<=14) {
        deathMotion(m,e);
        constexpr int animations[]={0,1,9,11,11,13,10,15,8,10,25,10,10,11,9};
        if(kind==7)m.b(e+0xb6,m.b(e+0xb6)&0xdf);
        startEntityAnimation(m,e,animations[kind]);
        if(kind==7&&(nextGameRandom(m)&3))call(m,f,0x560a4,42);
    }
}
void applyAreaBlast(Mem m,unsigned position,unsigned sourceCell,const Service& f) {
    auto floor=m.b(sourceCell+11);auto x=(sx(m.s(position))+m.w(0xa0c34))>>9,z=(sx(m.s(position+4))+m.w(0xa0c36))>>9;
    for(unsigned ring=0;ring<2;++ring) {
        auto table=ring?0x9c954u:0x9c934u,output=ring?0x2366f4u:0x236734u,count=ring?16u:8u;
        for(unsigned i=0;i<count;++i) {
            auto cx=x+sx(m.s(table+i*4)),cz=z+sx(m.s(table+i*4+2));
            m.d(output+i*4,cx<m.w(0xa0c30)&&cz<m.w(0xa0c32)?m.d(0xa0c40)+(cz*m.w(0xa0c30)+cx)*16:0);
        }
    }
    for(unsigned i=0;i<8;++i) {
        auto cell=m.d(0x236734+i*4);bool clear=false;
        if(cell){int difference=m.b(cell+11)-floor;if(difference<0)difference=-difference;
            clear=m.b(cell+10)<20&&difference<=12&&(m.b(cell+10)!=6||m.b(cell+13)-m.b(cell+11)>=8);
        }
        if(!clear) {
            m.d(0x236734+i*4,0);
            if(i&1)m.d(0x2366f4+i*8,0);
            else for(int offset=-1;offset<=1;++offset)m.d(0x2366f4+((i*2+offset)&15)*4,0);
        }
    }
    auto services=withCombatRuntime(f);
    for(unsigned ring=0;ring<2;++ring)for(unsigned i=0;i<(ring?16u:8u);++i) {
        auto cell=m.d((ring?0x2366f4:0x236734)+i*4);if(!cell)continue;
        auto occupant=m.b(cell+12);if(!occupant)continue;
        if(occupant==255){call(m,f,0x3dee8,ring?25:50,5);continue;}
        auto index=m.b(cell+4);
        if(occupant<=15) {
            auto e=m.d(0x2365e4)+index*208u;
            if(m.b(e+0x74)!=7)damageEntity(m,e,m.w(e+0x58),0,services);
            call(m,f,0x2a3e0,sx(m.s(e)),sx(word(m.w(e+2)+256u)),sx(m.s(e+4)));
        }else {
            auto object=m.d(0x2367fc)+index*40u;if(m.b(object+0x19)&1)continue;
            m.b(object+0x19,m.b(object+0x19)|1);m.b(0x2365f4+index,0);
            if(m.b(0x9e0a8))call(m,f,0x5287c);
            auto random=nextGameRandom(m);m.b(object+0x1f,(random&(ring?15:7))+(ring?8:4));
        }
    }
}
PlayerControlService withCombatRuntime(const Service& external) {
    return [external](Mem& m,unsigned address,unsigned a,unsigned b,unsigned c)->PlayerControlResult {
        switch(address) {
            case 0x30518:enterEntityDeath(m,a,external);break;
            case 0x30ac0:enterEntityPain(m,a,external);break;
            case 0x3298c:applyEntityKnockback(m,a,static_cast<std::uint16_t>(b),static_cast<std::uint16_t>(c));break;
            case 0x2ee40:startEntityAnimation(m,a,static_cast<std::int16_t>(b));break;
            case 0x362a8:applyAreaBlast(m,a,b,external);break;
            default:return external(m,address,a,b,c);
        }
        return {};
    };
}
}
