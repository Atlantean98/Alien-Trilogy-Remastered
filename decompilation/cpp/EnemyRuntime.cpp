#include "EnemyRuntime.h"
#include "ProjectileRuntime.h"
#include <bit>
namespace trilogy::original {
namespace {using Mem=PlayerControlMemory;int si(unsigned n){return std::bit_cast<std::int32_t>(n);}int word(unsigned n){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}bool visible(Mem m,unsigned e){return ((m.d(m.d(e+0x78)+6)&m.d(0xa0cbe))>>16)!=0;}}
void resetEnemyPool(Mem m){m.w(0x2365ee,0);m.d(0x2365e0,0);m.d(0x2365d8,0);auto p=m.d(0x2365e4);for(unsigned i=0;i<128;++i,p+=208){m.b(p+0x70,i);m.d(p+200,m.d(0x2365d8));m.d(0x236380+i*4,0);m.d(0x2365d8,p);}}
unsigned enemySpawnHealth(Mem m,unsigned spawn){auto mode=m.w(0x96234);auto health=static_cast<unsigned>(m.b(spawn+5));if((mode==1||mode==2)&&!health)return 0;if(mode==1)++health;else if(mode==2)health+=2;
    // GetRenderMode only replaces AX. On mode zero the upper pointer bits
    // survive into EAX; the actual constructor consumes the low AX health.
    else if(mode==0)health|=spawn&0xffff0000u;health<<=m.b(spawn)==7?6:4;auto low=word(health);return low&&low<16?16:health;}
void populateLevelEnemies(Mem m,unsigned records,std::uint16_t count,const PlayerControlService& f){
    m.d(0x2365d4,records);m.w(0x2365f0,count);
    for(unsigned i=0;i<count;++i,records+=20){auto old=m.b(records);auto kind=m.b(0x22e944+old*2);m.b(records,kind);
        if(kind!=old){if(kind==2){m.w(records+18,768);m.w(records+16,(nextGameRandom(m)&255u)+1200);m.w(records+14,(nextGameRandom(m)&15u)+105);}else if(kind==5||kind==6){m.w(records+16,1000);m.w(records+18,768);m.w(records+14,(nextGameRandom(m)&15u)+(kind==5?95:100));}}
        auto counter=0xa0bc8u+kind*2;m.w(counter,m.w(counter)+1);
        if(m.b(records+7)==m.b(records+8))f(m,0x2dfc8,static_cast<unsigned>(word(m.w(0x2365f0)-(count-i))),0,0);
    }
}
void initializeLevelEnemies(Mem m,const PlayerControlService& f){resetEnemyPool(m);populateLevelEnemies(m,m.d(0xa0c48),m.w(0xa0c1a),f);}
void turnEnemyTowardYaw(Mem m,unsigned e,std::uint16_t target){m.w(e+0x44,target);if(m.w(e+0x42)==m.w(e+0x44))return;int delta=word((m.w(e+0x42)-m.w(e+0x44))*16u)>>4,step=m.s(e+0x46);
    if(delta>0){m.w(e+0x42,delta<step?m.w(e+0x44):m.w(e+0x42)-m.w(e+0x46));m.b(e+0x43,m.b(e+0x43)&15);}
    else if(delta<0){m.w(e+0x42,-delta<step?m.w(e+0x44):m.w(e+0x42)+m.w(e+0x46));m.b(e+0x43,m.b(e+0x43)&15);}
    else m.w(e+0x42,m.w(e+0x44));m.b(e+0x6d,(m.s(e+0x42)+256)>>9&7);
}
void setEnemyPathTarget(Mem m,unsigned e){auto path=m.d(0xa0c50)+m.b(e+0x76)*8;auto flags=m.b(path+3);m.w(e+0x3a,0);m.w(e+0x38,m.b(path)*512u+(flags&1?256:0)-m.w(0xa0c34));m.w(e+0x3c,m.b(path+1)*512u+(flags&2?256:0)-m.w(0xa0c36));}
unsigned enemyReachedTarget(Mem m,unsigned e){int x=m.s(e+0x30),z=m.s(e+0x34);if(x<0)x=-x;if(z<0)z=-z;if(x||z){if(!x||!z){if(x){if(si(m.d(e+0x20))<=x)return 0xffffffff;return 0;}}else if(x<si(m.d(e+0x20)))return 0;if(si(m.d(e+0x28))<=z)return 0xffffffff;}return 0;}
void updateEnemyFacing(Mem m,unsigned e,const PlayerControlService& f){auto facing=(m.b(e+0x6d)-static_cast<unsigned>((m.s(e+0x54)+2304)>>9))&7u;if(m.b(e+0x6e)==facing)return;m.b(e+0x6e,facing);auto kind=m.b(e+0x74);if(kind==1)return;if((kind>=2&&kind<=9)||kind==11||kind==12||kind==14)m.b(e+0xb6,(m.b(e+0xb6)&0xdf)|(facing>=5?32:0));f(m,0x2ee40,e,facing,0);}
unsigned enemyWithinViewCone(Mem m,unsigned e){if(!visible(m,e))return 0;auto difference=word((m.w(e+0x42)-m.w(e+0x54))*16u)>>4;if(difference<0)difference=-difference;return difference<=m.s(e+0x40);}
unsigned enemyDetectsPlayer(Mem m,unsigned e){if(!m.d(m.d(e+0x78))||visible(m,e)){auto distance=m.d(e+0x60);if(distance<static_cast<unsigned>(m.s(e+0x50)>>1))return 0xffffffff;if(distance<static_cast<unsigned>(m.s(e+0x50))&&m.w(0xa0adc))return 0xffffffff;}return 0;}
PlayerControlService withEnemyRuntime(const PlayerControlService& f){return [f](Mem& m,unsigned routine,unsigned a,unsigned b,unsigned c){switch(routine){case 0x2dfc8:return PlayerControlResult{spawnEnemyFromRecord(m,a,f),0};case 0x2ddc8:resetEnemyPool(m);break;case 0x2ed40:populateLevelEnemies(m,a,b,withEnemyRuntime(f));break;case 0x2ee20:initializeLevelEnemies(m,withEnemyRuntime(f));break;case 0x2debc:return PlayerControlResult{enemySpawnHealth(m,a),0};case 0x328d8:turnEnemyTowardYaw(m,a,b);break;case 0x32d68:setEnemyPathTarget(m,a);break;case 0x31cbc:return PlayerControlResult{enemyReachedTarget(m,a),0};case 0x30ecc:updateEnemyFacing(m,a,f);break;case 0x33430:return PlayerControlResult{enemyWithinViewCone(m,a),0};case 0x33484:return PlayerControlResult{enemyDetectsPlayer(m,a),0};default:return f(m,routine,a,b,c);}return PlayerControlResult{};};}
}
