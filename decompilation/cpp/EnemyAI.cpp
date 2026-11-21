#include "EnemyAI.h"
#include "ProjectileRuntime.h"
#include <bit>
#include <cstdlib>
#include <memory>
namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;using Service=PlayerControlService;
int signed32(unsigned n){return std::bit_cast<std::int32_t>(n);}
unsigned call(Mem m,const Service& f,unsigned routine,unsigned a,unsigned b=0,unsigned c=0){return f(m,routine,a,b,c).eax;}
void distances(Mem m,unsigned e,unsigned target,unsigned out,unsigned range,unsigned category){
    for(unsigned i=0;i<3;++i)m.d(e+out+i*4,std::abs(m.s(target+i*2)-m.s(e+i*2)));
    auto d=enemyHorizontalDistance(m,target,e);m.d(e+range,d);
    m.b(e+category,d<1024?0:d<static_cast<unsigned>(m.s(e+0x4a))?1:d<static_cast<unsigned>(m.s(e+0x48))?2:d<16384?3:4);
}
}
unsigned approximateRootQ10(Mem m,unsigned n){unsigned shift=0;while(n>255){n>>=2;++shift;}return m.w(0x9d5e2+n*2)<<shift;}
unsigned enemyHorizontalDistance(Mem m,unsigned a,unsigned b){auto x=std::abs(m.s(a)-m.s(b)),z=std::abs(m.s(a+4)-m.s(b+4));return x>=16384||z>=16384?16384:approximateRootQ10(m,static_cast<unsigned>(x*x+z*z))>>10;}
unsigned yawBetweenPoints(Mem m,unsigned a,unsigned b){
    int dx=m.s(a)-m.s(b),dz=m.s(a+4)-m.s(b+4),x=std::abs(dx),z=std::abs(dz),largest=x>z?x:z;
    if(largest>16384){x>>=2;z>>=2;}else if(largest<1024){x<<=2;z<<=2;}
    int distance=static_cast<int>(approximateRootQ10(m,static_cast<unsigned>(x*x+z*z))>>10);if(distance<2)return 0;
    int nx=(x<<10)/distance,nz=(z<<10)/distance;
    int angle=nx<724?m.s(0x97074+nx*2):1024-m.s(0x97074+(nz>723?723:nz)*2);
    if(dz<0)angle=2048-angle;if(dx<0)angle=4096-angle;return static_cast<unsigned>(4096-angle);
}
void updateEnemyPlayerDistance(Mem m,unsigned e){distances(m,e,0xa0a48,0x10,0x60,0x73);}
void updateEnemyTargetDistance(Mem m,unsigned e){distances(m,e,e+0x38,0x20,0x64,0x75);}
void unfoldEnemy(Mem m,unsigned e,std::int16_t animation,const Service& f){auto kind=m.b(e+0x74);if(kind!=8&&kind!=9)return;m.b(e+0x74,kind==8?6:5);m.d(e+0x7c,kind==8?0x9c294:0x9c71c);m.w(e+8,256);m.w(e+10,768);m.w(e+12,256);m.b(e+0x6c,m.b(e+0x6c)&0xf7);if(animation!=-1)call(m,f,0x2ee40,e,static_cast<unsigned>(animation));}
void adjustEnemySpawnTrigger(Mem m,std::uint8_t delta,std::uint8_t index,const Service& f){int n=std::bit_cast<std::int8_t>(delta);auto source=m.d(0x2365d4)+index*20u;if(n>0&&m.b(source+7)==m.b(source+8))return;auto value=static_cast<std::uint8_t>(m.b(source+7)+n);m.b(source+7,value>m.b(source+8)?m.b(source+8):value);if(m.b(source+7)==m.b(source+8))call(m,f,0x2dfc8,index);}
void beginEnemyAttack(Mem m,unsigned task,unsigned e,const Service& f){
    m.d(e+0x68,60);m.b(e+0x6f,5);auto kind=m.b(e+0x74),category=m.b(e+0x73);auto animation=[&](unsigned a){call(m,f,0x2ee40,e,a);};
    auto jump=[&](unsigned speed,unsigned frame){call(m,f,0x3298c,e,static_cast<unsigned>(m.s(e+0x42)),speed);animation(frame);if(!m.w(e+0x32))m.w(e+0x32,128);};
    // SLUS-00007 SYNTH program 10 carries the smartgun projectile cue.
    constexpr int synthFireAnimation=10;
    if(category<2){switch(kind){
        case 2:if(!category&&signed32(m.d(e+0x14))<=1536&&!(m.b(0xa0ab5)&1)&&(call(m,f,0x2fe00,e)&65535)){m.d(task+4,0x3032c);m.d(task+12,64);m.b(0xa0ab5,m.b(0xa0ab5)|1);m.d(task+16,0);m.b(m.d(e+0x78)+12,0);return;}jump(96,10);break;
        case 3:jump(64,12);break;case 4:animation(12);break;case 5:animation(10);break;case 6:case 14:animation(11);break;
        case 7:{auto n=nextGameRandom(m)&3;animation(n==0?12:n==1?11:14);if(n==3)m.b(e+0xb6,m.b(e+0xb6)|32);break;}
        case 8:unfoldEnemy(m,e,9,f);break;case 9:unfoldEnemy(m,e,8,f);break;case 11:case 12:animation(9);break;case 13:animation(synthFireAnimation);break;
    }}else{switch(kind){
        case 2:jump(96,10);break;case 3:jump(64,12);break;case 4:case 6:animation(12);break;case 5:animation(11);break;
        case 7:{auto n=nextGameRandom(m)&3;if(n==0||n==1){if(!(nextGameRandom(m)&1)){m.b(e+0x6f,2);return;}animation(n==0?12:11);}else{animation(14);if(n==3)m.b(e+0xb6,m.b(e+0xb6)|32);}break;}
        case 8:case 9:m.b(e+0x6f,0);m.w(e+0x52,2);break;case 11:case 12:animation(9);break;case 13:animation(synthFireAnimation);break;
        case 14:if((nextGameRandom(m)&7)<3)animation(11);else{m.b(e+0x6c,m.b(e+0x6c)|1);animation(10);}break;
    }}
}
void applyEnemyAttackHit(Mem m,unsigned e,const Service& f){
    auto kind=m.b(e+0x74),category=m.b(e+0x73);if(category>2||kind<2||kind>7||!enemyWithinViewCone(m,e))return;
    if(category&&!call(m,f,0x31d30,e,static_cast<unsigned>(m.s(e+0x54)),m.d(e+0x60)))return;
    if(kind<=3)call(m,f,0x3dee8,category==2?1:5,4);
    else call(m,f,0x3df14,kind<=5?(category==2?5:10):kind==6?(category==2?10:15):(category==2?15:25),(m.w(e+0x42)+2048u)&4095,kind<=5?1:kind==6?0:2);
}
// The enemy task chain withEnemyAI(withEnemyRuntime(f),survival) used to be rebuilt on
// every task update - a deep copy of every nested std::function in the
// service stack, per enemy, per tick. The wrappers are stateless forwarders
// (no mutable captures anywhere), so the chain is built once, lazily, and
// reused: identical behaviour without the copies and allocations.
PlayerControlService withEnemyAI(const Service& f,bool survival){auto taskChain=std::make_shared<Service>();return [f,taskChain,survival](Mem& m,unsigned routine,unsigned a,unsigned b,unsigned c){auto chain=[&]()->const Service&{if(!*taskChain)*taskChain=withEnemyAI(withEnemyRuntime(f),survival);return *taskChain;};switch(routine){
    case 0x33cc8:updateEnemyTask(m,a,chain(),survival);break;
    case 0x35088:updateEggTask(m,a,chain());break;
    case 0x355d4:updateQueenTask(m,a,chain());break;
    case 0x35ebc:updateColonistTask(m,a,chain());break;
    case 0x26504:return PlayerControlResult{yawBetweenPoints(m,a,b),0};
    case 0x4d15c:return PlayerControlResult{approximateRootQ10(m,a),0};case 0x31b0c:return PlayerControlResult{enemyHorizontalDistance(m,a,b),0};
    case 0x31b68:updateEnemyPlayerDistance(m,a);break;case 0x31c18:updateEnemyTargetDistance(m,a);break;
    case 0x2eb40:unfoldEnemy(m,a,static_cast<std::int16_t>(b),f);break;case 0x2ebb4:adjustEnemySpawnTrigger(m,a,b,f);break;
    case 0x336d0:beginEnemyAttack(m,a,b,f);break;case 0x33a0c:applyEnemyAttackHit(m,a,f);break;
    default:return f(m,routine,a,b,c);
}return PlayerControlResult{};};}
}
