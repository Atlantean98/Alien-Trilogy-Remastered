#include "DoorRuntime.h"
#include "ProjectileRuntime.h"
namespace trilogy::original {
namespace {unsigned door(PlayerControlMemory m,unsigned delta,unsigned index,bool finalOnly,const PlayerControlService& f){
    auto p=m.d(0x2348d8)+index*32u;auto have=m.b(p+27),need=m.b(p+26);if((m.b(p+24)&24)||have>=need||(finalOnly&&int(have)<int(need)-1))return 0;
    m.b(p+27,have+delta);if(m.b(p+27)==need){if(!m.b(0x9e0a8))f(m,0x3ab6c,m.d(0x3f9818),0,0);if(!(m.b(p+24)&128)){m.b(p+24,m.b(p+24)|128);m.w(0xa0b28,m.w(0xa0b28)+1);}}
    else f(m,0x3ab6c,m.d(0x3f981c),0,0);return 0xffffffff;
}}
unsigned incrementDoorUnlock(PlayerControlMemory m,std::uint8_t delta,std::uint8_t index,const PlayerControlService& f){return door(m,delta,index,false,f);}
unsigned requestDoorOpen(PlayerControlMemory m,std::uint8_t delta,std::uint8_t index,const PlayerControlService& f){return door(m,delta,index,true,f);}
unsigned activateMapObject(PlayerControlMemory m,std::uint8_t index,std::uint8_t mode,const PlayerControlService& f){auto p=m.d(0x2367fc)+index*40u;if(m.w(p+26))return 0;auto kind=m.b(p+24);
    if(kind==22||kind==26){if(!m.b(0x9e0a8)){if(!m.w(0xa0ad2)){f(m,0x3ab6c,m.d(0x3f982c),0,0);if(nextGameRandom(m)&1)f(m,0x560a4,14,0,0);return 0;}}
        else{if(m.b(0x3f953b)||!m.b(0x236c4e))return 0;m.b(0x3f953b,1);m.d(0x3f94ec,0x834);f(m,0x51e68,1,0x834,0);}if(!m.b(0x9e0a8))m.w(0xa0ad2,m.w(0xa0ad2)-1);}
    f(m,0x40460,92,0xffffffff,0x3fff);auto counter=0xa0ae6u+m.b(p+24)*2u;m.w(counter,m.w(counter)+1);m.w(p+26,mode==1?1:2);return 0xffffffff;
}
}
