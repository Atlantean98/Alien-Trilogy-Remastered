#include "ExplorerDiagnostics.h"
#include "ExplorerEnemies.h"
#include "ExplorerSurvivalState.h"
#include "EnemyMovement.h"
#include "MappedTaskScheduler.h"
#include "CombatRuntime.h"
#include "WeaponRuntime.h"
#include "ProjectileRuntime.h"
#include "PlayerDamage.h"
#include "DropRendering.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <queue>
#include <stdexcept>
#include <limits>
using namespace trilogy::original;
namespace {
std::vector<unsigned char> readFile(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("Missing enemy asset: "+p.string());auto n=f.tellg();if(n<0)throw std::runtime_error("Cannot size enemy asset");std::vector<unsigned char> b(static_cast<size_t>(n));f.seekg(0);if(!f.read(reinterpret_cast<char*>(b.data()),n))throw std::runtime_error("Cannot read enemy asset");return b;}
// Bounded host decoder for original F-section LZ streams. No original-routine
// recovery credit is assigned to this display adapter.
std::vector<unsigned char> decode(const std::vector<unsigned char>& in,unsigned start,unsigned expected){
    std::vector<unsigned char> out;out.reserve(expected);unsigned p=start;
    while(p<in.size()){unsigned token=in[p++],count=token&3,index=token>>2;if(!count){if(p>=in.size())break;count=in[p++];if(!count){if(out.size()==expected)return out;break;}}if(out.size()+count>expected)break;out.insert(out.end(),count,static_cast<unsigned char>(index));}
    if(start<=in.size()&&in.size()-start>=expected&&in.size()-start<=expected+3)return {in.begin()+start,in.begin()+start+expected};
    throw std::runtime_error("PS1 RLE frame dimensions do not match data");
}
const char* names[]={"","EGGS","HUGGER","BURSTER","BAMBI","DOG","WAR","QUEEN","WARCEIL","DOGCEIL","COLONIST","GUARD","SOLDIER","SYNTH","HANDLER"};
}
ExplorerEnemies::ExplorerEnemies(Mem mem,const std::filesystem::path& path):m(mem),root(path){
    for(unsigned i=0;i<4096;++i)sine[i]=static_cast<short>(m.s(0x9763c+i*2));
    auto provider=[this](Mem& mem,unsigned r,unsigned a,unsigned b,unsigned c){return external(mem,r,a,b,c);};
    auto damage=withPlayerDamageRuntime(provider);
    auto survivalDamage=[this,damage](Mem& mem,unsigned routine,unsigned a,unsigned b,unsigned c){
        if(!explosiveDamage&&survivalSkullEnabled(survivalSkulls,AcidSkull)&&
           (((routine==0x3dde8||routine==0x3dee8||routine==0x3df04)&&b==5)||
            (routine==0x3df14&&c==5)))a=std::min(a,16383u)*2;
        const bool enemyAttack=(routine==0x3dee8||routine==0x3df14)&&
                               (!explosiveDamage||enemyGrenadeDamage);
        if(enemyAttack&&survivalSkullEnabled(survivalSkulls,GameOverManSkull))
            a=std::min(a,16383u)*2;
        // Extermination: an alien after an ally lands its attack on the ally, never on the player. The squad
        // shrugs it off (a pain flash, no damage).
        if(enemyAttack&&squadVictim){
            const auto ally=squadVictim;
            if(!squadHurt[ally])++squadAttacksAbsorbed;
            squadHurt[ally]=8;
            // A marine takes the blow: scaled to their health so one lasts about three times what the player does.
            const unsigned maximum=squadHealth.at(ally),loss=std::max(1u,std::min(a,16383u)*maximum/300);
            const unsigned health=unsigned(std::max(0,int(m.s(ally+0x58))));
            if(health<=loss){
                m.w(ally+0x58,0);squadDowned.insert(ally);squadFiring.erase(ally);
                startEntityAnimation(m,ally,9);   // goes down (the handler's death fall), held on its last frame
            }else m.w(ally+0x58,health-loss);
            return PlayerControlResult{};
        }
        return damage(mem,routine,a,b,c);
    };
    auto common=withEnemyRuntime(withMappedTaskScheduler(withCombatRuntime(withWeaponRuntime(withProjectileRuntime(survivalDamage)))));
    services=withEnemyAI(common);
    survivalServices=withEnemyAI(common,true);
}
void ExplorerEnemies::initialize(unsigned difficulty){
    ventOriginCells.clear();
    // Host-owned pools do not overlap original globals, MAP0 or each other.
    std::fill(m.bytes.begin()+0x300000,m.bytes.begin()+0x308000,0);std::fill(m.bytes.begin()+0x310000,m.bytes.begin()+0x312000,0);
    m.d(0x2365e4,0x300000);m.d(0x22ceb4,0x310000);services(m,0x12258,0,0,0);
    m.b(0x9e0a8,0);m.b(0xa0c92,0);m.w(0x96234,difficulty);m.b(0x95f9c,1);m.b(0x94b9d,m.b(0x94b9d)|1);m.d(0xa0cbe,0xffff0000);
    m.w(0xa0ab8,100);m.w(0xa0aba,0);m.w(0xa0b30,0);m.w(0xa0ae4,0);m.b(0xa0ab5,0);m.b(0xa0c90,0);m.w(0xa0adc,0);
    for(unsigned p=0xa0ae6;p<0xa0b12;p+=2)m.w(p,0);
    for(unsigned k=0;k<256;++k)m.w(0x22e944+k*2,k);
    for(unsigned p:{0x234410u,0x23440cu,0x23442cu,0x234408u,0x234434u,0x2343fcu,0x234420u,0x234428u,0x234438u,0x234430u,0x234424u,0x23443cu}){m.d(p,0x31f000);}
    m.d(0x236304,0x350000);resetProjectilePool(m);m.d(0x351900+24,0);
    m.w(0x31f000,0);m.d(0x94bc8,0x31f100);
    for(unsigned i=0;i<unsigned(m.w(0xa0c30))*m.w(0xa0c32);++i)m.w(m.d(0xa0c40)+i*16+8,0xffff);
    services(m,0x2ee20,0,0,0);
    for(auto e:active()){auto kind=m.b(e+0x74);if(kind<15)loadBundle(names[kind]);sprite(e);}
}
std::vector<unsigned> ExplorerEnemies::active()const{std::vector<unsigned> out;for(auto e=m.d(0x2365e0);e&&out.size()<128;e=m.d(e+200)){if(e<0x300000||e>=0x300000+128*208||(e-0x300000)%208)throw std::runtime_error("Invalid active enemy link");if(std::find(out.begin(),out.end(),e)!=out.end())throw std::runtime_error("Cyclic enemy list");out.push_back(e);}return out;}
unsigned ExplorerEnemies::health()const{return m.w(0xa0ab8);}
bool ExplorerEnemies::clearSegment(unsigned e,int ax,int az,int bx,int bz,bool footprint)const{
    auto width=m.w(0xa0c30),height=m.w(0xa0c32);auto grid=m.d(0xa0c40);int count=std::max(1,std::max(std::abs(bx-ax),std::abs(bz-az))/64);
    int baseline=m.s(e+0x5a);for(int i=1;i<=count;++i){int x=ax+(bx-ax)*i/count,z=az+(bz-az)*i/count;
        for(int side=0;side<(footprint?5:1);++side){int px=x+(side==1?180:side==2?-180:0),pz=z+(side==3?180:side==4?-180:0);if(px<200||pz<200||px>=width*512-200||pz>=height*512-200)return false;
            auto cell=grid+((pz>>9)*width+(px>>9))*16;auto attr=m.b(cell+10);if(attr>=20&&attr<45)return false;
            int floor=m.b(cell+11)*32,ceiling=m.b(cell+13)*32;if(footprint){if(floor-baseline>256||baseline-floor>1024||ceiling-floor<m.s(e+10)+32)return false;baseline=floor;}
        }
    }return true;
}
bool ExplorerEnemies::sight(unsigned e,int yaw,unsigned distance,bool footprint)const{
    if(distance>(footprint?8192u:5461u))return false;auto v=rotateVector({0,static_cast<short>((yaw+2048)&4095),0},{0,0,static_cast<short>(distance)},sine);
    int x=m.s(e)+m.w(0xa0c34),z=m.s(e+4)+m.w(0xa0c36);return clearSegment(e,x,z,x+v[0],z+v[2],footprint);
}
bool ExplorerEnemies::navigate(unsigned e,bool recovering){
    const bool allowQueenOverlap=g_survival.active&&g_survival.cfg.queensRevenge()&&m.b(e+0x74)==7;
    int width=m.w(0xa0c30),height=m.w(0xa0c32),sx=(m.s(e)+m.w(0xa0c34))>>9,sz=(m.s(e+4)+m.w(0xa0c36))>>9,tx=(m.s(0xa0a48)+m.w(0xa0c34))>>9,tz=(m.s(0xa0a4c)+m.w(0xa0c36))>>9;
    auto valid=[&](int x,int z){return x>0&&z>0&&x<width-1&&z<height-1;};if(!valid(sx,sz)||!valid(tx,tz))return false;
    // Search buffers are reused across calls (this runs on every enemy re-plan):
    // a per-search stamp marks visited cells instead of reallocating and
    // clearing a whole-level parent array, and a flat vector replaces the queue.
    // Visiting order (FIFO, same neighbour order) is unchanged, so paths are identical.
    static std::vector<int> parent;static std::vector<unsigned> seen;static std::vector<int> q;static unsigned stamp=0;
    const size_t cells=size_t(width)*size_t(height);if(parent.size()<cells){parent.resize(cells);seen.resize(cells,0);}if(++stamp==0){std::fill(seen.begin(),seen.end(),0u);stamp=1;}
    auto visited=[&](int c){return seen[c]==stamp;};auto visit=[&](int c,int from){seen[c]=stamp;parent[c]=from;};
    auto grid=m.d(0xa0c40);q.clear();size_t head=0;int start=sz*width+sx,target=tz*width+tx;visit(start,start);q.push_back(start);
    while(head<q.size()&&!visited(target)){int cell=q[head++];for(auto [dx,dz]:std::initializer_list<std::pair<int,int>>{{1,0},{-1,0},{0,1},{0,-1}}){int x=cell%width+dx,z=cell/width+dz;if(!valid(x,z))continue;int next=z*width+x;if(visited(next))continue;auto p=grid+next*16,old=grid+cell*16;int delta=int(m.b(p+11))-int(m.b(old+11));auto attr=m.b(p+10);if(recovering&&cell==start&&next!=target&&m.b(p+12)&&m.b(p+4)!=m.b(e+0x70)&&!(allowQueenOverlap&&m.b(p+12)==7))continue;if((attr>=20&&attr<45)||delta>8||delta<-32||int(m.b(p+13))-int(m.b(p+11))<(m.s(e+10)+63)/32)continue;visit(next,cell);q.push_back(next);}}
    if(!visited(target))return false;int next=target;while(parent[next]!=start&&next!=start)next=parent[next];
    int x=next%width*512+256,z=next/width*512+256;
    if(start==target){x=m.s(0xa0a48)+m.w(0xa0c34);z=m.s(0xa0a4c)+m.w(0xa0c36);}
    if(recovering){
        const int wx=m.s(e)+m.w(0xa0c34),wz=m.s(e+4)+m.w(0xa0c36);
        const int centerX=sx*512+256,centerZ=sz*512+256;
        if((std::abs(wx-centerX)>128||std::abs(wz-centerZ)>128)&&
                !clearSegment(e,wx,wz,x,z,true)&&clearSegment(e,wx,wz,centerX,centerZ,true)){
            x=centerX;z=centerZ;
        }
    }
    m.w(e+0x38,x-m.w(0xa0c34));m.w(e+0x3a,0);m.w(e+0x3c,z-m.w(0xa0c36));updateEnemyTargetDistance(m,e);m.w(e+0x56,yawBetweenPoints(m,e,e+0x38)&4095);return true;
}
PlayerControlResult ExplorerEnemies::external(Mem&,unsigned r,unsigned a,unsigned b,unsigned c){
    ++effects[r];if(sound&&(r==0x40604||r==0x40460||r==0x403ec)){
        unsigned slot=a;
        if(r==0x40604&&b>=0x300000&&b<0x300000+128*208&&m.b(b+0x74)==5){
            constexpr unsigned warrior[]={67,72,70,73};
            if(a>=63&&a<=66)slot=warrior[a-63];
        }
        sound(slot,r==0x40604?b:0);
    }switch(r){
        case 0x31950:moveEnemy(m,a,services,survivalMovement,
            g_survival.active&&g_survival.cfg.queensRevenge()&&m.b(a+0x74)==7);return {};
        case 0x600000:++flameKills;return {};
        case 0x12ed4:return {getRenderMode(m),0};
        case 0x31d30:case 0x32500:return {sight(a,static_cast<short>(b),c,r==0x31d30)?0xffffffffu:0u,0};
        case 0x329e0:case 0x32ddc:case 0x33208:case 0x334e4:return {navigate(a)?0xffffffffu:0u,0};
        // Host sight/path adapters above are provisional. These effects have
        // no host sound/drop/region implementation yet and are counted.
        case 0x3b61c:if(droppedItem)droppedItem(a);return {};
        case 0x2acac:case 0x2a4e8:case 0x2a6e0:case 0x41044:case 0x2d004:case 0x44fa0:return {};
        case 0x40db0:if(projectileAction)projectileAction(a);return {};
        case 0x406ec:if(levelAction)levelAction(a);return {};
        case 0x404f8:if(sound)sound(a,0);return {};
        case 0x2fd18:case 0x40b5c:case 0x40604:case 0x560a4:case 0x40460:case 0x403ec:return {};
        case 0x2a3e0:if(explosion)explosion(static_cast<short>(a),static_cast<short>(b),static_cast<short>(c));return {};
        case 0x2fe00:return {attach(a)?0xffffffffu:0u,0};
        case 0x2a8bc:{ // 0x80024e00: enemy bullet projectile (kind 5)
            auto p=acquireProjectile(m);if(!p)return {};
            auto pos=m.d(p),vel=m.d(p+4);
            m.w(pos,m.w(a));m.w(pos+2,m.w(a+2)+(m.b(a+0x74)==11?704:576));m.w(pos+4,m.w(a+4));
            unsigned scratch=0x351800;int dist=enemyHorizontalDistance(m,pos,0xa0a48);
            m.w(scratch,m.s(pos+2)-640-m.s(0xa0a4a));m.w(scratch+2,0);m.w(scratch+4,dist);
            short pitch=short(yawBetweenPoints(m,0x94b92,scratch)-2048);
            short yaw=short((m.w(a+0x42)+2048)&4095);
            auto v=rotateVector({pitch,0,0},{0,0,short(m.s(a+0x4c)+128)},sine);
            v=rotateVector({0,yaw,2048},v,sine);
            for(unsigned i=0;i<3;++i)m.w(vel+i*2,v[i]);
            m.b(p+0x1c,64);m.b(p+0x1d,64);m.b(p+0x1e,5);m.w(p+0x1a,1);
            m.w(p+0x10,0);m.w(p+0x12,yaw);m.w(p+0x14,0);m.b(p+0x20,0);
            int cx=(m.s(pos)+m.w(0xa0c34))>>9,cz=(m.s(pos+4)+m.w(0xa0c36))>>9;
            if(cx<0||cz<0||cx>=m.w(0xa0c30)||cz>=m.w(0xa0c32)){releaseProjectile(m,p);return {};}
            m.d(p+12,m.d(0xa0c40)+(cz*m.w(0xa0c30)+cx)*16);activateProjectile(m,p);
            return {};
        }
        case 0x2ab70:{ // 0x80025120: handler grenade projectile (kind 13)
            auto p=acquireProjectile(m);if(!p)return {};
            auto pos=m.d(p),vel=m.d(p+4);
            m.w(pos,m.w(a));m.w(pos+2,m.w(a+2)+384);m.w(pos+4,m.w(a+4));
            short pitch=m.s(a+2)<m.s(0xa0a4a)?512:(m.s(0xa0a4a)<m.s(a+2)?-512:0);
            short yaw=short((m.w(a+0x42)+2048)&4095);
            auto v=rotateVector({pitch,0,0},{0,0,short(m.s(a+0x4c)+128)},sine);
            v=rotateVector({0,yaw,0},v,sine);v[1]=32;
            for(unsigned i=0;i<3;++i)m.w(vel+i*2,v[i]);
            m.d(p+8,m.d(0x236bf4));m.w(p+0x10,0);m.w(p+0x12,(0-m.w(a+0x42))&4095);m.w(p+0x14,0);
            m.w(p+0x18,16);m.w(p+0x1a,25);m.b(p+0x1c,64);m.b(p+0x1d,64);m.b(p+0x1e,13);m.b(p+0x20,0);
            int cx=(m.s(pos)+m.w(0xa0c34))>>9,cz=(m.s(pos+4)+m.w(0xa0c36))>>9;
            if(cx<0||cz<0||cx>=m.w(0xa0c30)||cz>=m.w(0xa0c32)){releaseProjectile(m,p);return {};}
            m.d(p+12,m.d(0xa0c40)+(cz*m.w(0xa0c30)+cx)*16);activateProjectile(m,p);return {};
        }
        case 0x3032c:tickAttachment(a);return {};
        default:++unknownServices;return {};
    }
}
void ExplorerEnemies::addExterminationAlly(unsigned entity){
    squadHealth[entity]=unsigned(std::max(1,m.s(entity+0x58)))*4;
    m.w(entity+0x58,squadHealth.at(entity));
    m.w(entity+0x4e,65535);
    // The co-op player's Alien Handler: the HANDLER bundle and its animation table (0-4 walking views from the
    // back round to the front, 5-7 the same views to be mirrored, 8 pain, 10 raising the rifle, 11 firing). The
    // squad stays ours by entity (squadHealth), not by kind, so this doesn't make them hostile.
    m.b(entity+0x74,14);
    m.d(entity+0x7c,0x9c3fc);
    m.w(entity+0xb4,0x900);
    m.b(entity+0x6d,(m.s(entity+0x42)+256)>>9&7);
    startEntityAnimation(m,entity,4);
    releaseSquadCell(entity);
}
// The marines don't claim the cell they stand in (byte 12, which the player's collision treats as solid): the player
// walks through them. Only the marine's own claim (it spawned as a synth, kind 13; Alien Handler kind 14) - never an alien's.
void ExplorerEnemies::releaseSquadCell(unsigned e){
    const auto cell=m.d(e+0x78);
    if(cell&&(m.b(cell+12)==13||m.b(cell+12)==14))m.b(cell+12,0);
}
void ExplorerEnemies::healExterminationSquad(){   // a medikit: 40% back to every marine still standing
    for(const auto& [entity,maximum]:squadHealth)
        if(!squadDowned.contains(entity))m.w(entity+0x58,std::min(maximum,unsigned(m.w(entity+0x58))+maximum*2/5));
}
bool ExplorerEnemies::reviveExterminationAlly(unsigned entity){
    if(!squadDowned.erase(entity))return false;
    m.w(entity+0x58,std::max(1u,squadHealth.at(entity)/3));
    squadHurt[entity]=0;squadFiring[entity]=0;
    startEntityAnimation(m,entity,4);
    return true;
}
void ExplorerEnemies::tickExterminationAlly(unsigned e,const Vec3s& position){
    releaseSquadCell(e);
    if(squadDowned.contains(e)){   // lying where they fell: the fall plays out, then holds
        m.w(e+0x30,0);m.w(e+0x34,0);
        if(m.d(e+0x90)&&!(m.b(e+0xb6)&6))advanceAnimation(m,e+128);
        return;
    }
    auto& cooldown=squadCooldown[e];
    if(cooldown)--cooldown;
    const int ox=m.w(0xa0c34),oz=m.w(0xa0c36);
    const int x=m.s(e),z=m.s(e+4);
    unsigned target=0;
    double best=6800;
    for(auto other:active()){
        if(squadHealth.contains(other))continue;
        const auto kind=m.b(other+0x74);
        // Every Xenomorph; eggs only close by (a nest next to the squad), so the squad doesn't waste fire on far ones.
        if(kind<1||kind>9||m.s(other+0x58)<=0||m.b(other+0x6f)>=7)continue;
        const int tx=m.s(other),tz=m.s(other+4);
        const auto distance=std::hypot(tx-x,tz-z);
        if(kind==1&&distance>2048)continue;
        if(distance<best&&clearSegment(e,x+ox,z+oz,tx+ox,tz+oz,false)){
            target=other;
            best=distance;
        }
    }
    m.w(e+0x30,0);
    m.w(e+0x34,0);
    auto& firing=squadFiring[e];
    auto& hurt=squadHurt[e];
    if(firing)--firing;
    if(hurt)--hurt;
    bool moved=false,wants=false,pathless=false;
    if(target&&!cooldown){
        cooldown=12;
        // Face the target: the heading and its eighth (+0x6d, what the view below is worked out from).
        const auto heading=yawBetweenPoints(m,e,target)&4095;
        m.w(e+0x42,heading);
        m.w(e+0x44,heading);
        m.b(e+0x6d,(heading+256)>>9&7);
        firing=8;
        {   // the round: from the gun (chest height, a little ahead) to the middle of the alien
            const double angle=heading*6.283185307179586/4096;
            const short gx=short(m.s(e)-std::sin(angle)*160),gz=short(m.s(e+4)+std::cos(angle)*160);
            const auto kind=m.b(target+0x74);
            const int rise=kind==2||kind==3?96:kind==1?224:kind==4||kind==5?320:560;
            squadRounds.push_back({{gx,short(m.s(e+2)+620),gz},{short(m.s(target)),short(m.s(target+2)+rise),short(m.s(target+4))}});
            ++squadShots;
        }
        damageEntity(m,target,18,0,services);
        if(sound)sound(6,e);
    }else if(!target){
        // Engage: with no alien in sight, a marine closes on the nearest one within reach (the pathfinder heads for
        // the player globals, so they hold that alien's position for this step); with none about, back to the player.
        unsigned quarry=0;double nearest=5200;
        for(auto other:active()){
            if(squadHealth.contains(other))continue;
            const auto kind=m.b(other+0x74);
            if(kind<2||kind>9||m.s(other+0x58)<=0||m.b(other+0x6f)>=7)continue;
            const double distance=std::hypot(double(m.s(other)-x),double(m.s(other+4)-z));
            if(distance<nearest){nearest=distance;quarry=other;}
        }
        // Following: each marine has their own spot behind the player (two at the shoulders, one further back), so
        // the three don't all walk into the same place and block each other. A spot off the floor the player stands
        // on (a wall, a drop, the pool) falls back to the player.
        int gx=position[0],gz=position[2];
        if(!quarry){
            const unsigned slot=unsigned(std::distance(squadHealth.begin(),squadHealth.find(e)));
            const double yaw=m.s(0xa0ba2)*6.283185307179586/4096,fx=-std::sin(yaw),fz=std::cos(yaw),rx=std::cos(yaw),rz=std::sin(yaw);
            const double back=slot==2?1300:700,side=slot==0?-600:slot==1?600:0;
            const int ox=m.w(0xa0c34),oz=m.w(0xa0c36);
            const int sx=int(position[0]-fx*back+rx*side),sz=int(position[2]-fz*back+rz*side);
            const int cx=(sx+ox)>>9,cz=(sz+oz)>>9;
            if(cx>0&&cz>0&&cx<m.w(0xa0c30)-1&&cz<m.w(0xa0c32)-1){
                const auto cell=m.d(0xa0c40)+unsigned(cz*m.w(0xa0c30)+cx)*16;
                if(int(m.d(cell))>=0&&!(m.b(cell+7)&0x80)&&std::abs(m.b(cell+11)*32-position[1])<=256&&
                   clearSegment(e,position[0]+ox,position[2]+oz,sx+ox,sz+oz,false)){gx=sx;gz=sz;}
            }
        }
        const bool follow=!quarry&&std::hypot(double(gx-x),double(gz-z))>350;
        wants=quarry||follow;
        if(wants){
            const short px=m.s(0xa0a48),pz=m.s(0xa0a4c);
            m.w(0xa0a48,quarry?m.s(quarry):gx);m.w(0xa0a4c,quarry?m.s(quarry+4):gz);
            if(navigate(e,true)){
                turnEnemyTowardYaw(m,e,m.w(e+0x56));   // keeps +0x6d up to date
                services(m,0x3298c,e,m.w(e+0x42),116);
                moveEnemy(m,e,services,true);
                moved=true;
            }else pathless=true;
            m.w(0xa0a48,px);m.w(0xa0a4c,pz);
        }
    }
    // Stuck - on scenery, an edge, another marine, or somewhere with no way back (the pool): wanting to move but
    // getting nowhere. After half a second a sidestep; after a second and a half, if there's no route at all or
    // they're well away from the player, they catch up: free floor a couple of cells from the player, in sight.
    {
        auto& last=squadLast[e];auto& stuck=squadStuck[e];
        const bool progress=std::abs(m.s(e)-last[0])>=8||std::abs(m.s(e+4)-last[1])>=8;
        last={m.s(e),m.s(e+4)};
        if(!wants||progress)stuck=0;
        else ++stuck;
        if(stuck==15&&moved){
            m.w(e+0x30,0);m.w(e+0x34,0);
            const auto sidestep=(m.w(e+0x42)+(e&2?1024:3072))&4095;
            turnEnemyTowardYaw(m,e,sidestep);
            services(m,0x3298c,e,m.w(e+0x42),116);
            moveEnemy(m,e,services,true);
        }else if(stuck>=45&&(pathless||std::hypot(position[0]-x,position[2]-z)>900)&&squadCatchUp(e,position)){
            stuck=0;last={m.s(e),m.s(e+4)};
        }
    }
    // Which side of the marine the player sees: the original facing rule (updateEnemyFacing) - the ally's own
    // heading against the bearing from the ally to the player - so turning the camera doesn't spin them, walking
    // round them does. 0 is their back, 4 their front; 5-7 mirror 3-1.
    const unsigned bearing=yawBetweenPoints(m,e,0xa0a48)&4095;
    const unsigned view=(m.b(e+0x6d)-((bearing+2304)>>9))&7u;
    unsigned animation=view<=4?view:8-view;
    if(hurt)animation=8;                                  // an alien's blow landing (no damage taken)
    else if(firing&&view>=3&&view<=5)animation=11;        // the firing frames exist from the front only
    m.b(e+0xb6,(m.b(e+0xb6)&0xdf)|(view>=5?32:0));
    if(m.w(e+0xc4)!=animation){startEntityAnimation(m,e,short(animation));m.b(e+0xb6,(m.b(e+0xb6)&0xdf)|(view>=5?32:0));}
    else if(m.d(e+0x90)&&!(m.b(e+0xb6)&6)&&(moved||animation==11||animation==8))advanceAnimation(m,e+128);
    releaseSquadCell(e);   // movement and catching up claim the new cell
}
bool ExplorerEnemies::squadCatchUp(unsigned e,const Vec3s& player,int nearest){
    const int ox=m.w(0xa0c34),oz=m.w(0xa0c36),width=m.w(0xa0c30),height=m.w(0xa0c32);
    const int px=(player[0]+ox)>>9,pz=(player[2]+oz)>>9;
    for(int ring=nearest;ring<=4;++ring)for(int dz=-ring;dz<=ring;++dz)for(int dx=-ring;dx<=ring;++dx){
        if(std::max(std::abs(dx),std::abs(dz))!=ring)continue;
        const int cx=px+dx,cz=pz+dz;
        if(cx<1||cz<1||cx>=width-1||cz>=height-1)continue;
        const auto cell=m.d(0xa0c40)+unsigned(cz*width+cx)*16;
        const int floor=m.b(cell+11)*32;
        if(m.b(cell+7)&0x80||m.b(cell+12)||std::abs(floor-player[1])>256||(m.b(cell+13)-m.b(cell+11))<32)continue;
        if(std::any_of(squadHealth.begin(),squadHealth.end(),[&](const auto& other){return other.first!=e&&m.d(other.first+0x78)==cell;}))continue;   // not onto another marine
        if(!clearSegment(e,player[0]+ox,player[2]+oz,cx*512+256,cz*512+256,false))continue;   // in reach of the player
        m.w(e,cx*512+256-ox);m.w(e+2,floor);m.w(e+4,cz*512+256-oz);m.d(e+0x78,cell);
        m.w(e+0x30,0);m.w(e+0x34,0);
        return true;
    }
    return false;
}
// A stitched Extermination deck: the player rode the first map's exit elevator into the second map's start
// elevator, and the marines (downed ones carried along) come out beside them.
void ExplorerEnemies::moveExterminationSquad(const Vec3s& player){
    for(const auto& [entity,maximum]:squadHealth){
        releaseSquadCell(entity);
        squadLast.erase(entity);squadStuck.erase(entity);
        if(!squadCatchUp(entity,player,1)){
            const int ox=m.w(0xa0c34),oz=m.w(0xa0c36);
            m.w(entity,player[0]);m.w(entity+2,player[1]);m.w(entity+4,player[2]);
            m.d(entity+0x78,m.d(0xa0c40)+unsigned(((player[2]+oz)>>9)*m.w(0xa0c30)+((player[0]+ox)>>9))*16);
        }
        releaseSquadCell(entity);
    }
}
unsigned ExplorerEnemies::exterminationVictim(unsigned alien,const Vec3s& player,unsigned playerHealth){
    // Who this alien goes for: the nearest of the player and the squad, those in sight first. Chosen again every
    // half second (and at once if the choice is gone), so packs split between the marines instead of flickering.
    auto& choice=squadChoice[alien];
    auto& timer=squadRetarget[alien];
    if(timer&&(choice?squadHealth.contains(choice)&&!squadDowned.contains(choice):playerHealth!=0)){--timer;return choice;}
    timer=15;
    const int ox=m.w(0xa0c34),oz=m.w(0xa0c36),x=m.s(alien),z=m.s(alien+4);
    auto score=[&](int tx,int tz){
        const double distance=std::hypot(double(tx-x),double(tz-z));
        return distance+(clearSegment(alien,x+ox,z+oz,tx+ox,tz+oz,false)?0.:6000.);
    };
    double best=playerHealth?score(player[0],player[2]):1e18;
    choice=0;
    for(const auto& [ally,maximum]:squadHealth){
        if(squadDowned.contains(ally))continue;
        const double s=score(m.s(ally),m.s(ally+4));
        if(s<best){best=s;choice=ally;}
    }
    return choice;
}

void ExplorerEnemies::tick(const Vec3s& initialPosition,unsigned healthValue){
    for(auto& round:squadRounds)++round.age;   // the marines' rounds fly on; gone once at the alien
    std::erase_if(squadRounds,[](const SquadRound& r){return r.age*SquadRoundSpeed>=std::hypot(double(r.to[0]-r.from[0]),double(r.to[1]-r.from[1]),double(r.to[2]-r.from[2]))+SquadRoundSpeed;});
    Vec3s position=initialPosition;
    ++ticks;if(fireCooldown)--fireCooldown;for(unsigned i=0;i<3;++i)m.w(0xa0a48+i*2,position[i]);m.w(0xa0ab8,healthValue);if(!coopTarget&&m.w(0xa0b30))m.w(0xa0b30,m.w(0xa0b30)-1);
    m.b(0x95f9c,1);m.w(0xa0adc,0);
    // Snapshot task handles so creation/removal during a callback is safe.
    std::vector<unsigned> tasks;for(auto t=m.d(0x22ceb8);t&&tasks.size()<256;t=m.d(t+24))tasks.push_back(t);
    for(auto t:tasks){auto e=m.d(t+8);if(e<0x300000||e>=0x300000+128*208)continue;if(coopTarget){coopTarget(e);for(unsigned axis=0;axis<3;++axis)position[axis]=short(m.w(0xa0a48+axis*2));healthValue=m.w(0xa0ab8);}
        if(squadHealth.contains(e)){tickExterminationAlly(e,position);continue;}
        if(e==perchedQueen){
            // Play the campaign's attached warning animation without running
            // the task stages that detach the Queen or attack the player.
            if(m.w(e+0xc4)==9){
                const auto previousCommand=m.d(e+0x98);
                advanceAnimation(m,e+128);
                if(m.d(e+0x98)!=previousCommand&&(m.b(e+0xb6)&16)&&sound)sound(m.w(e+0xb8),e);
                if(m.b(e+0xb6)&2){
                    startEntityAnimation(m,e,8);
                    m.w(e+0x52,128);
                }
            }else if(m.w(e+0x52)){
                m.w(e+0x52,m.w(e+0x52)-1);
            }else if(healthValue&&std::abs(m.s(e+2)-position[1])<=1024){
                updateEnemyPlayerDistance(m,e);
                if(m.b(e+0x73)<3&&clearSegment(e,m.s(e)+m.w(0xa0c34),m.s(e+4)+m.w(0xa0c36),
                        position[0]+m.w(0xa0c34),position[2]+m.w(0xa0c36),false))
                    startEntityAnimation(m,e,9);
            }
            continue;
        }
        // Extermination: the alien goes after its chosen marine. The recovered tasks chase, test and attack the
        // player globals (0xa0a48 position), so for an ally they hold the ally's position while this alien runs;
        // squadVictim routes its blows to the ally (survivalDamage, attach). Restored after the alien, however
        // its turn ends.
        struct SquadTargetRestore{ExplorerEnemies& self;Vec3s& position;Vec3s saved;bool active=false;
            ~SquadTargetRestore(){if(!active)return;for(unsigned axis=0;axis<3;++axis)self.m.w(0xa0a48+axis*2,saved[axis]);position=saved;self.squadVictim=0;}
        } squadRestore{*this,position,position};
        if(!squadHealth.empty()&&!coopTarget&&m.b(e+0x74)>=1&&m.b(e+0x74)<=9&&m.s(e+0x58)>0){
            if(const auto ally=exterminationVictim(e,position,healthValue)){
                squadRestore.active=true;squadVictim=ally;
                for(unsigned axis=0;axis<3;++axis){position[axis]=m.s(ally+axis*2);m.w(0xa0a48+axis*2,position[axis]);}
            }
        }
        auto before=m.d(e);auto beforeZ=m.w(e+4);auto callback=m.d(t+4);
        const bool moving=m.w(e+0x30)||m.w(e+0x34);
        // Resolve real player contact even while the generic hugger is in its
        // leap/recovery state. The provisional navigation adapter can keep its
        // recovery flag set, otherwise preventing a second attack/latch decision.
        if(callback==0x33cc8&&m.b(e+0x74)==2&&m.b(e+0x6f)<7&&m.s(e+0x58)>0&&healthValue&&!attached()){
            double distance=std::hypot(double(m.s(e)-position[0]),double(m.s(e+4)-position[2]));
            if(distance<=768&&std::abs(m.s(e+2)-position[1])<=1024&&clearSegment(e,m.s(e)+m.w(0xa0c34),m.s(e+4)+m.w(0xa0c36),position[0]+m.w(0xa0c34),position[2]+m.w(0xa0c36),false)){
                updateEnemyPlayerDistance(m,e);auto yaw=yawBetweenPoints(m,e,0xa0a48);m.w(e+0x42,yaw);m.w(e+0x44,yaw);m.b(e+0x6c,m.b(e+0x6c)&0xfb);updateEnemyFacing(m,e,services);beginEnemyAttack(m,t,e,services);callback=m.d(t+4);
            }
        }
        // The provisional pathfinder rejected large drops before the recovered
        // attack could launch. Give a grounded, nearby hugger on a raised ledge
        // its normal recovered leap, retaining original velocity/gravity/collision.
        if(callback==0x33cc8&&m.b(e+0x74)==2&&m.b(e+0x6f)<5&&m.s(e+0x58)>0&&!m.w(e+0x32)&&!m.d(e+0x68)){
            int elevation=m.s(e+2)-position[1];double distance=std::hypot(double(m.s(e)-position[0]),double(m.s(e+4)-position[2]));
            if(elevation>256&&elevation<=4096&&distance>256&&distance<=2048&&clearSegment(e,m.s(e)+m.w(0xa0c34),m.s(e+4)+m.w(0xa0c36),position[0]+m.w(0xa0c34),position[2]+m.w(0xa0c36),false)){
                updateEnemyPlayerDistance(m,e);auto yaw=yawBetweenPoints(m,e,0xa0a48);m.w(e+0x42,yaw);m.w(e+0x44,yaw);m.b(e+0x6c,m.b(e+0x6c)&0xfb);updateEnemyFacing(m,e,services);beginEnemyAttack(m,t,e,services);++ledgeLeaps;
            }
        }
        // Generic enemies, eggs, Queens and colonists own their task timers.
        // Remaining unsupported tasks retain their animation/display fallback.
        if(callback!=0x33cc8&&callback!=0x35088&&callback!=0x355d4&&callback!=0x35ebc&&callback!=0x3032c&&m.w(e+0x4e))m.w(e+0x4e,m.w(e+0x4e)-1);
        if(m.b(e+0x74)>=16&&m.b(e+0x74)<=19)tickVent(t);else if(callback==0x3032c)tickAttachment(t);else if(callback==0x33cc8||callback==0x35088||callback==0x355d4||callback==0x35ebc)(survivalMovement?survivalServices:services)(m,callback,t,0,0);else{if(m.d(e+0x90)&&!(m.b(e+0xb6)&6))advanceAnimation(m,e+128);}
        if(survivalMovement&&(callback==0x33cc8||callback==0x355d4)){
            auto& stalled=stalledTicks[(e-0x300000)/208];
            const auto state=m.b(e+0x6f);
            if(m.s(e+0x58)>0&&moving&&state>=2&&state<=4&&
                    short(before)==m.s(e)&&beforeZ==m.w(e+4)){
                if(++stalled>=10){
                    stalled=0;
                    m.w(e+0x30,0);m.w(e+0x34,0);m.w(e+0x4c,0);
                    if(navigate(e,true)){
                        m.b(e+0x6f,3);
                        m.d(e+0x68,30);
                    }else m.b(e+0x6f,2);
                }
            }else stalled=0;
        }
        movingTicks+=before!=m.d(e)||beforeZ!=m.w(e+4);attackTicks+=m.b(e+0x6f)==5;
    }
    if(!coopTarget)corpseContact(position,healthValue);
}
void ExplorerEnemies::corpseContact(const Vec3s& position,unsigned healthValue){
    // Some special/death animation bridges never stamp the native acid floor.
    // Resolve contact from retained alien bodies as well, without altering terrain.
    if(healthValue&&!m.w(0xa0abc)&&!m.w(0xa0ae4))for(auto e:active()){
        unsigned kind=m.b(e+0x74);if(kind<1||kind>9||m.s(e+0x58)>0||isAttachedEntity(e))continue;
        double radius=kind==2||kind==3?192:kind==7?640:320;
        if(std::abs(int(position[1])-m.s(e+0x5a))>128||std::hypot(double(m.s(e)-position[0]),double(m.s(e+4)-position[2]))>radius)continue;
        if(!clearSegment(e,m.s(e)+m.w(0xa0c34),m.s(e+4)+m.w(0xa0c36),position[0]+m.w(0xa0c34),position[2]+m.w(0xa0c36),false))continue;
        terrainDamage(survivalSkullEnabled(survivalSkulls,GameOverManSkull)?2:1,5);break;
    }
}
bool ExplorerEnemies::shoot(const Vec3s& position,int yaw,int pitch,double focal){
    if(fireCooldown||!health())return false;fireCooldown=6;++shots;m.w(0xa0adc,20);double best=1e9;unsigned selected=0;double angle=yaw*6.283185307179586/4096,cs=std::cos(angle),sn=std::sin(angle);
    for(auto e:active()){if(e==perchedQueen||m.s(e+0x58)<=0||m.b(e+0x6f)>=7)continue;double dx=m.s(e)-position[0],dz=m.s(e+4)-position[2],depth=-dx*sn+dz*cs,right=-(dx*cs+dz*sn);if(depth<16||depth>=best)continue;double aimY=position[1]+768+std::tan(pitch*6.283185307179586/4096)*depth;int height=std::max(256,m.s(e+10));if(std::abs(right)>std::max(200.0,depth*8/focal)||aimY<m.s(e+2)-64||aimY>m.s(e+2)+height+64)continue;
        if(!clearSegment(e,position[0]+m.w(0xa0c34),position[2]+m.w(0xa0c36),m.s(e)+m.w(0xa0c34),m.s(e+4)+m.w(0xa0c36),false))continue;best=depth;selected=e;
    }
    if(selected){++hits;auto before=m.w(selected+0x58);damageEntity(m,selected,32,0,services);if(before&&m.s(selected+0x58)<=0)++kills;return true;}return false;
}
void ExplorerEnemies::loadBundle(const std::string& name){
    if(bundles.contains(name))return;auto data=readFile(root/"GameData/PS1/CD/NME"/(name+".BND"));if(data.size()<12||std::string(data.begin(),data.begin()+4)!="FORM")throw std::runtime_error("Invalid enemy FORM");std::vector<std::vector<unsigned char>> sections;
    for(size_t p=12;p+8<=data.size();){unsigned n=(unsigned(data[p+4])<<24)|(unsigned(data[p+5])<<16)|(unsigned(data[p+6])<<8)|data[p+7];if(p+8ull+n>data.size())throw std::runtime_error("Enemy chunk exceeds bundle");if(data[p]=='C'&&n>=512){std::vector<unsigned> colors(256);for(unsigned i=0;i<256;++i){unsigned c=data[p+8+i*2]|(unsigned(data[p+9+i*2])<<8);colors[i]=0xff000000u|((c&31)*255/31<<16)|(((c>>5)&31)*255/31<<8)|((c>>10)&31)*255/31;}bundlePalettes[name]=std::move(colors);}if(data[p]=='F'){auto index=unsigned(std::stoi(std::string(data.begin()+p+1,data.begin()+p+4)));if(index>99)throw std::runtime_error("Enemy section index out of range");if(sections.size()<=index)sections.resize(index+1);sections[index]={data.begin()+p+8,data.begin()+p+8+n};}p+=8+n+(n&1);}bundles[name]=std::move(sections);
}
const ExplorerSprite& ExplorerEnemies::sprite(unsigned e){
    auto frame=m.d(e+0x90);auto kind=m.b(e+0x74);static const ExplorerSprite empty{};if(!frame||kind<1||kind>14)return empty;
    // Death keeps the ceiling animation table while changing the gameplay species.
    auto table=m.d(e+0x7c);if(table==0x9c324)kind=8;else if(table==0x9c37c)kind=9;
    auto entry=table+m.w(e+0xc4)*8;
    // A frame that cannot be decoded is drawn as nothing (and logged once), never thrown from the middle of a frame.
    try{return frameSprite(kind,m.d(entry),m.d(entry+4),frame);}
    catch(const std::exception& error){
        static std::set<unsigned> logged;
        if(logged.insert(frame^(kind<<24)).second)std::ofstream(root/"decompilation/reports/runtime-errors.log",std::ios::app)<<"sprite kind "<<kind<<" frame "<<frame<<": "<<error.what()<<'\n';ExplorerDiagnostics::error("sprite kind "+std::to_string(kind)+" frame "+std::to_string(frame)+": "+error.what());
        return empty;
    }
}
const ExplorerSprite& ExplorerEnemies::frameSprite(unsigned kind,unsigned group,unsigned program,unsigned frame){
    auto key=frame^(kind<<24);if(sprites.contains(key))return sprites.at(key);const char* bundle=kind==2&&(group==m.d(0x9c284)||group==m.d(0x9c28c))?"FINGERS":names[kind];loadBundle(bundle);auto& sections=bundles.at(bundle);unsigned section=0;
    auto groupKey=group^(unsigned(kind)<<24);if(sectionForGroup.contains(groupKey))section=sectionForGroup.at(groupKey);else{
        bool found=false;unsigned count=program>group&&(program-group)%12==0?std::min(1024u,(program-group)/12):1;
        for(unsigned candidate=0;candidate<sections.size();++candidate){bool raw=true;unsigned extent=0;for(unsigned i=0;i<count;++i){auto record=group+i*12;auto offset=m.d(record+8),size=m.w(record+4)*unsigned(m.w(record+6));if(!size||size>262144||offset>extent){raw=false;break;}extent=std::max(extent,offset+size);}raw=raw&&sections[candidate].size()>=extent&&sections[candidate].size()<=extent+3;if(raw){section=candidate;found=true;rawForGroup[groupKey]=true;break;}bool matches=true;for(unsigned i=0;i<count;++i){auto record=group+i*12;unsigned size=m.w(record+4)*unsigned(m.w(record+6));if(!size||size>262144){matches=false;break;}try{decode(sections[candidate],m.d(record+8),size);}catch(const std::exception&){matches=false;break;}}if(matches){section=candidate;found=true;break;}}
        if(!found)throw std::runtime_error("Cannot match original sprite frames to "+std::string(names[kind])+" group="+std::to_string(group)+" program="+std::to_string(program)+" frame="+std::to_string(frame));sectionForGroup[groupKey]=section;
    }
    ExplorerSprite result;result.width=m.w(frame+4);result.height=m.w(frame+6);if(!result.width||!result.height||result.width>2048||result.height>2048||result.width*result.height>262144)throw std::runtime_error("PS1 sprite dimensions exceed limits");result.left=m.s(frame);result.top=m.s(frame+2);std::vector<unsigned char> pixels;try{if(rawForGroup[groupKey]){auto offset=m.d(frame+8);auto size=result.width*result.height;if(offset>sections[section].size()||size>sections[section].size()-offset)throw std::runtime_error("Raw sprite frame outside section");pixels={sections[section].begin()+offset,sections[section].begin()+offset+size};}else pixels=decode(sections[section],m.d(frame+8),result.width*result.height);}catch(const std::exception& ex){throw std::runtime_error(std::string(ex.what())+" kind="+std::to_string(kind)+" group="+std::to_string(group)+" program="+std::to_string(program)+" frame="+std::to_string(frame)+" section="+std::to_string(section));}result.pixels.resize(pixels.size());
    auto& colors=bundlePalettes.at(bundle);for(unsigned i=0;i<pixels.size();++i){auto index=pixels[i];if(index)result.pixels[i]=colors.at(index);}
    ++decodedFrames;return sprites.emplace(key,std::move(result)).first->second;
}

void ExplorerEnemies::trigger(unsigned delta,unsigned index){adjustEnemySpawnTrigger(m,delta,index,services);}

void ExplorerEnemies::terrainDamage(unsigned amount,unsigned type,bool healthOnly){auto before=health();auto armor=m.w(0xa0aba);if(healthOnly){m.w(0xa0aba,0);m.w(0xa0b30,0);m.w(0xa0ae4,0);}services(m,0x3dde8,amount,type,0);if(healthOnly)m.w(0xa0aba,armor);if(type==5&&health()!=before)++acidHits;}
void ExplorerEnemies::emitVent(unsigned e,unsigned kind,int speed){
 auto p=acquireProjectile(m);if(!p)return;auto pos=m.d(p),vel=m.d(p+4);for(unsigned i=0;i<3;++i)m.w(pos+i*2,m.w(e+i*2));Vec3s velocity=kind>=17?Vec3s{0,short(m.b(e+0x72)>127?-speed:speed),0}:Vec3s{0,0,short(speed)};velocity=rotateVector({0,short(m.w(e+0x42)),0},velocity,sine);for(unsigned i=0;i<3;++i)m.w(vel+i*2,velocity[i]);m.d(p+12,m.d(e+0x78));ventOriginCells[p]=m.d(e+0x78);m.w(p+26,kind==16?10:5);m.b(p+28,32);m.b(p+29,32);m.b(p+30,kind);m.b(p+32,0);activateProjectile(m,p);++ventEmissions;
}
void ExplorerEnemies::tickVent(unsigned task){
 auto e=m.d(task+8);unsigned kind=m.b(e+0x74);bool steam=kind==16||kind==18;unsigned phase=m.d(task+20)+1;if(phase>m.w(e+0x48))phase=0;m.d(task+20,phase);auto raw=m.d(0x2365d4)+m.b(e+0x77)*20;bool active=false;
 if(m.b(raw+7)<m.b(raw+8)){if(!(m.b(e+0x6c)&16)){m.b(e+0x6c,m.b(e+0x6c)|16);m.w(0xa0ae6+kind*2,m.w(0xa0ae6+kind*2)+1);++ventsDisabled;if(hazardNotice)hazardNotice(steam?L"Steam valve closed":L"Flame jet shut down");}}
 else {updateEnemyPlayerDistance(m,e);if(m.d(e+0x60)<=7680&&m.w(m.d(e+0x78)+8)){
  switch(m.b(e+0x6f)){
   case 0:if(m.s(e+0x58)<=0){m.d(task+12,0);m.d(task+20,0);m.b(e+0x6f,8);}break;
   case 6:m.d(task+12,0);m.d(task+20,0);m.w(e+0x4e,0);m.b(e+0x6f,0);m.w(e+0x52,1);break;
   case 7:if(explosion)explosion(m.s(e),m.s(e+2),m.s(e+4));m.b(e+0x6f,8);break;
   case 8:{int difference=short(phase-m.w(e+0x4a));active=difference<0||difference>m.s(e+0x5e);if(active){unsigned interval=kind==16?3:2,counter=m.d(task+12)+1;m.d(task+12,counter);if(counter>interval){m.d(task+12,0);unsigned particle=kind==16?14:kind==17?16:kind==18?18:17;emitVent(e,particle,kind==16?60:kind==17?120:kind==18?65:90);}}break;}
   default:m.b(e+0x6f,6);break;
  }
 }}
 if(ambient){double dx=m.s(e)-m.s(0xa0a48),dz=m.s(e+4)-m.s(0xa0a4c),distance=std::hypot(dx,dz),yaw=m.s(0xa0ba2)*6.283185307179586/4096;float gain=float(.3/(1+distance/1400)),pan=float(std::clamp(-(dx*std::cos(yaw)+dz*std::sin(yaw))/std::max(1.,distance),-1.,1.));ambient(e,steam?104:28,gain,pan,active);}
}


namespace {constexpr unsigned FaceObjects[]={0x31e000,0x31e040};}
bool ExplorerEnemies::attach(unsigned e){
    if(squadVictim){if(!squadHurt[squadVictim])++squadAttacksAbsorbed;squadHurt[squadVictim]=8;return false;}   // a Facehugger can't latch onto an ally (or the player from there)
    if(attachmentEntity||m.b(0xa0ab5)&1||!health()||m.b(e+0x74)!=2||m.b(e+0x6f)>=7)return false;
    attachmentEntity=e;attachmentScreenY=340;++attachments;
    for(unsigned half=0;half<2;++half){auto object=FaceObjects[half],entry=m.d(e+0x7c)+0x80+half*8,program=m.d(entry+4);attachmentPrograms[half]=program;
        for(unsigned i=0;i<64;++i)m.b(object+i,0);m.d(object+0x14,m.d(entry));m.d(object+0x18,program+2);m.d(object+0x1c,program+4);m.w(object+0x30,1);m.w(object+0x32,m.w(program));advanceAnimation(m,object);
        attachmentSprite(half);if(m.b(object+0x36)&16)services(m,0x403ec,static_cast<unsigned>(m.s(object+0x38)),0xffffffff,0xffffffff);
    }
    m.w(e+0x30,0);m.w(e+0x32,0);m.w(e+0x34,0);return true;
}
const ExplorerSprite& ExplorerEnemies::attachmentSprite(unsigned half){
    static const ExplorerSprite empty{};if(!attachmentEntity||half>1)return empty;auto object=FaceObjects[half];auto frame=m.d(object+0x10);if(!frame)return empty;return frameSprite(2,m.d(object+0x14),attachmentPrograms[half],frame);
}
void ExplorerEnemies::tickAttachment(unsigned task){
    auto e=m.d(task+8);if(e!=attachmentEntity){m.d(task+4,0x33cc8);m.b(0xa0ab5,m.b(0xa0ab5)&254);return;}
    auto phase=m.d(task+16),timer=m.d(task+12);
    if(!timer&&phase==2){
        // Original detachment samples nearby empty floor before entering the
        // normal death path. Keep retrying if this tick chose a wall/occupied cell.
        int x=m.s(0xa0a48)+int(nextGameRandom(m)&1023)-512,z=m.s(0xa0a4c)+int(nextGameRandom(m)&1023)-512;
        int gx=(x+m.w(0xa0c34))>>9,gz=(z+m.w(0xa0c36))>>9;
        if(gx<1||gz<1||gx>=m.w(0xa0c30)-1||gz>=m.w(0xa0c32)-1)return;auto cell=m.d(0xa0c40)+(gz*m.w(0xa0c30)+gx)*16;
        if(m.b(cell+12)||m.b(cell+10)>=20)return;int floor=m.b(cell+11)*32;if(std::abs(floor-m.s(0xa0a4a))>=256)return;
        m.w(e,x);m.w(e+4,z);m.w(e+2,floor+32);m.w(e+0x5a,floor);m.w(e+0x5c,m.b(cell+13)*32);m.d(e+0x78,cell);m.w(e+0x58,0);enterEntityDeath(m,e,services);++kills;
        m.b(0xa0ab5,m.b(0xa0ab5)&254);m.d(task+4,0x33cc8);attachmentEntity=0;++completedAttachments;return;
    }
    if(timer)m.d(task+12,timer-1);
    for(unsigned half=0;half<2;++half){auto object=FaceObjects[half];bool advance=m.w(object+0x30)==1;advanceAnimation(m,object);if(advance&&(m.b(object+0x36)&16))services(m,0x403ec,static_cast<unsigned>(m.s(object+0x38)),0xffffffff,0xffffffff);}
    if(!phase){if(attachmentScreenY>120)attachmentScreenY-=8;}
    else if(attachmentScreenY<340)attachmentScreenY+=16;else m.d(task+16,2);
    // Source 0x30331 calls health-only damage 1/type 4 through 0x3dd24.
    if(!m.w(0xa0b30)&&!m.w(0xa0ae4)){auto armor=m.w(0xa0aba);m.w(0xa0aba,0);services(m,0x3dee8,1,4,0);m.w(0xa0aba,armor);}
    if(timer==1)m.d(task+16,1);
}

void ExplorerEnemies::blast(unsigned p,unsigned cell){
    const bool wasExplosive=explosiveDamage;
    explosiveDamage=true;
    applyAreaBlast(m,p,cell,services);
    if(survivalSkullEnabled(survivalSkulls,ChainReactionSkull)&&((nextGameRandom(m)>>8)&1)){
        if(explosion)explosion(m.s(p),m.s(p+2),m.s(p+4));
        applyAreaBlast(m,p,cell,services);
    }
    explosiveDamage=wasExplosive;
}
void ExplorerEnemies::flushChainBlasts(){
    for(const auto& [entity,cell]:pendingChainBlasts){
        if(explosion)explosion(m.s(entity),m.s(entity+2),m.s(entity+4));
        explosiveDamage=true;
        applyAreaBlast(m,entity,cell,services);
        explosiveDamage=false;
    }
    pendingChainBlasts.clear();
}
void ExplorerEnemies::applySurvivalSpawnModifiers(unsigned entity){
    const auto kind=m.b(entity+0x74);
    if(kind<2||kind>14)return;
    const auto health=m.s(entity+0x58);
    if(health>0&&survivalSkullEnabled(survivalSkulls,ExoskeletonSkull))
        m.w(entity+0x58,health+(health+3)/4);
    if(survivalSkullEnabled(survivalSkulls,ShrunkKidsSkull)&&kind!=7){
        const unsigned variant=(nextGameRandom(m)>>8)%3;
        const auto scale=m.w(entity+0xb4);
        if(variant)m.w(entity+0xb4,variant==1?scale/2:scale*2);
    }
}
unsigned ExplorerEnemies::spawnSurvivalEnemy(unsigned index){
    survivalMovement=true;
    const auto slot=m.d(0x2365d8);
    if(index>=m.w(0x2365f0)||!slot)return 0;

    // The recovered constructor assumes a fresh slot for the first animation.
    // Survival reuses slots across species, so retain only the pool bookkeeping.
    const auto next=m.d(slot+200);
    const auto poolIndex=m.b(slot+0x70);
    stalledTicks[(slot-0x300000)/208]=0;
    std::fill_n(m.bytes.begin()+slot,208,0);
    m.d(slot+200,next);
    m.b(slot+0x70,poolIndex);

    const auto enemy=spawnEnemyFromRecord(m,static_cast<std::uint16_t>(index),services);
    if(enemy)applySurvivalSpawnModifiers(enemy);
    if(enemy&&!m.d(enemy+0x90)){
        updateEnemyPlayerDistance(m,enemy);
        updateEnemyFacing(m,enemy,services);
    }
    return enemy;
}
void ExplorerEnemies::perchSurvivalQueen(unsigned entity){
    perchedQueen=entity;
    // The perched Queen runs only the warning animation, so this gate cannot expire.
    m.w(entity+0x4e,PerchedQueenDamageGate);
}
void ExplorerEnemies::releaseSurvivalQueen(){
    const auto entity=perchedQueen;
    if(!entity)return;
    perchedQueen=0;
    activateSurvivalQueen(entity);
}
void ExplorerEnemies::activateSurvivalQueen(unsigned entity){
    m.w(entity+0x4e,0);
    m.w(entity+0x52,0);
    m.b(entity+0x6c,m.b(entity+0x6c)|2);
    // Enter the original Queen task's detachment stage directly.
    m.d(m.d(entity+0xc0)+12,2);
    startEntityAnimation(m,entity,10);
}
void ExplorerEnemies::preloadSurvivalEnemy(unsigned kind){
    if(kind>0&&kind<15)loadBundle(names[kind]);
}
void ExplorerEnemies::killWithTrap(unsigned entity){
    m.w(entity+0x4e,0);
    m.w(entity+0x58,0);
    services(m,0x30518,entity,0,0);
}
void ExplorerEnemies::retireSurvivalCorpse(unsigned entity){
    // Dead enemies outside the player's active area can skip their task indefinitely.
    // Unlink the corpse and release its task using the recovered pool layout.
    const auto next=m.d(entity+200);
    const auto previous=m.d(entity+204);
    if(next)m.d(next+204,previous);
    if(previous)m.d(previous+200,next);
    else m.d(0x2365e0,next);
    m.d(entity+204,0);
    m.d(entity+200,m.d(0x2365d8));
    m.d(0x2365d8,entity);
    services(m,0x12394,m.d(entity+0xc0),0,0);
}
void ExplorerEnemies::crateEnemy(unsigned object,unsigned index){
 if(index>=m.w(0x2365f0)||!m.d(0x2365d8))return;auto r=m.d(0x2365d4)+index*20;
 m.b(r+7,std::min(unsigned(m.b(r+8)),unsigned(m.b(r+7))+1));if(m.b(r+7)!=m.b(r+8))return;
 m.b(r+1,(m.s(object)+m.w(0xa0c34))>>9);m.b(r+2,(m.s(object+4)+m.w(0xa0c36))>>9);auto e=spawnEnemyFromRecord(m,index,services);if(!e)return;
 if(m.b(e+0x74)==2||m.b(e+0x74)==3){auto facing=m.w(object+34);m.w(e+0x42,facing<<9);m.w(e+0x44,facing<<9);m.b(e+0x6d,facing);m.b(e+0x6e,facing);m.b(e+0x6c,m.b(e+0x6c)|2);m.w(e+0x52,0);applyEntityKnockback(m,e,facing<<9,64);startEntityAnimation(m,e,8);m.w(e+0x32,96);m.b(e+0x6f,5);}
}

// Playable weapon path: recovered projectile flight, collision, damage and pain
// gates. The older shoot() helper is retained solely for existing AI fixtures.
void ExplorerEnemies::tickProjectiles(){
 // Hazard puffs cannot cross solid world cells, including closed doors.
 for(auto p=m.d(0x236300);p;){auto next=m.d(p+36);unsigned kind=m.b(p+30);if(kind==14||kind==16||kind==17||kind==18){auto pos=m.d(p),vel=m.d(p+4);int dx=m.s(vel),dz=m.s(vel+4);unsigned samples=std::max(std::abs(dx),std::abs(dz))/64+1;bool blocked=false;for(unsigned i=1;i<=samples;++i){int x=(m.s(pos)+dx*int(i)/int(samples)+m.w(0xa0c34))>>9,z=(m.s(pos+4)+dz*int(i)/int(samples)+m.w(0xa0c36))>>9;if(x<0||z<0||x>=m.w(0xa0c30)||z>=m.w(0xa0c32)){blocked=true;break;}auto c=m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16;if((!ventOriginCells.contains(p)||ventOriginCells.at(p)!=c)&&((m.b(c+7)&128)||m.b(c+10)==20||m.b(c+13)<=m.b(c+11))){blocked=true;break;}}if(blocked){unlinkProjectile(m,p,services);releaseProjectile(m,p);}}p=next;}
 unsigned beforeHazardHealth=health(),beforeHazardArmor=m.w(0xa0aba);
 auto list=active();std::vector<int> hp;for(auto e:list)hp.push_back(m.s(e+0x58));
 // Keep ballistic player rounds alive at their late-flight damage stage.
 // Flame, grenades and enemy projectiles retain their original lifetimes.
 for(auto p=m.d(0x236300);p;p=m.d(p+0x24)){unsigned kind=m.b(p+0x1e);if((kind==0||kind==1||kind==2||kind==4)&&m.b(p+0x1c)<=1)m.b(p+0x1c,2);}
 auto observe=coopProjectile;
 std::vector<int> before;
 if(survivalMovement){
    before.resize(list.size());
    observe=[&,this](unsigned p,bool entering){
        if(!entering)explosiveDamage=enemyGrenadeDamage=false;
        if(entering){
            const unsigned kind=m.b(p+30);
            enemyGrenadeDamage=kind==13;
            explosiveDamage=kind==11||kind==12||enemyGrenadeDamage||kind==19;
            playerGrenadeDamage=survivalShotProjectiles.contains(p)&&(kind==11||kind==12||kind==19);
        }
        if(coopProjectile)coopProjectile(p,entering);
        const auto shot=survivalShotProjectiles.find(p);
        if(shot==survivalShotProjectiles.end())return;
        for(size_t i=0;i<list.size();++i){
            const int hpNow=m.s(list[i]+0x58);
            if(entering)before[i]=hpNow;
            else{
                if(hpNow<before[i]&&survivalShotsHit.insert(shot->second).second)++accurateShots;
                if(playerGrenadeDamage&&before[i]>0&&hpNow<=0&&
                   survivalSkullEnabled(survivalSkulls,ChainReactionSkull)&&((nextGameRandom(m)>>8)&1))
                    pendingChainBlasts.emplace_back(list[i],m.d(list[i]+0x78));
            }
        }
        if(!entering)flushChainBlasts();
    };
 }
 updateProjectiles(m,0x351900,services,true,observe);
 if(!survivalShotProjectiles.empty()){
    std::set<unsigned> liveProjectiles,liveShots;
    for(auto p=m.d(0x236300);p;p=m.d(p+36))liveProjectiles.insert(p);
    std::erase_if(survivalShotProjectiles,[&](const auto& item){return !liveProjectiles.contains(item.first);});
    for(const auto& [p,shot]:survivalShotProjectiles)liveShots.insert(shot);
    std::erase_if(survivalShotsHit,[&](unsigned shot){return !liveShots.contains(shot);});
 }if(health()<beforeHazardHealth||m.w(0xa0aba)<beforeHazardArmor)++hazardHits;
 for(unsigned i=0;i<list.size();++i)if(m.s(list[i]+0x58)<hp[i]){++hits;if(hp[i]>0&&m.s(list[i]+0x58)<=0)++kills;}
}
bool ExplorerEnemies::fireWeapon(const Vec3s& position,int yaw,int pitch,unsigned weapon,short movement){
 std::vector<unsigned> existingProjectiles;for(auto p=m.d(0x236300);p;p=m.d(p+36))existingProjectiles.push_back(p);
 constexpr unsigned ammunition[]={0xa0abe,0xa0ac2,0xa0ac4,0xa0acc,0xa0aca};if(weapon>=5||!health()||!m.w(ammunition[weapon]))return false;
 for(unsigned i=0;i<3;++i)m.w(0xa0a20+i*2,position[i]+(i==1?768:0));m.w(0xa0ba0,0);m.w(0xa0ba2,yaw);m.w(0xa0ba4,2048);// Native projectile pitch has the opposite sign to the freelook camera.
    m.w(0xa0ba8,-pitch);m.w(0xa0a52,yaw);m.w(0xa0a64,movement);
 auto before=m.w(ammunition[weapon]);auto write=[&](unsigned a,const Vec3s& v){for(unsigned i=0;i<3;++i)m.w(a+i*2,v[i]);};
 auto cell=[&](unsigned projectile){auto pos=m.d(projectile);int x=(m.s(pos)+m.w(0xa0c34))>>9,z=(m.s(pos+4)+m.w(0xa0c36))>>9;if(x<0||z<0||x>=m.w(0xa0c30)||z>=m.w(0xa0c32))return false;m.d(projectile+12,m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16);return true;};
 if(survivalSkullEnabled(survivalSkulls,RocketManSkull)){
    if(!throwGrenade(position,yaw,pitch,true,movement,true))return false;
    if(!(m.b(0x94b9c)&32))m.w(ammunition[weapon],before-1);
 }else if(weapon==0)spawnPistolShot(m,services);else if(weapon==1)spawnShotgunShot(m,services);else if(weapon==2)spawnPulseBurst(m,services);
 else if(weapon==4){
  // 0x2b5dc/0x2b9d0: two flame streams, one unit of fuel, 80 base
  // damage, 15/14 tick lifetimes. Contact stops the stream, not its damage.
  bool first=false;for(unsigned stream=1;stream<3;++stream){m.w(0x236310,stream);auto p=acquireProjectile(m);if(!p)break;auto pos=m.d(p),vel=m.d(p+4);m.b(p+32,0);m.b(p+30,3);m.w(p+26,80);m.b(p+28,16-stream);m.b(p+29,16-stream);
   Vec3s v{position[0],short(position[1]+768-128+int(nextGameRandom(m)&31)),position[2]};auto offset=rotateVector({0,short(yaw),2048},{56,0,0},sine);for(unsigned i=0;i<3;++i)v[i]+=offset[i];write(pos,v);
   auto velocity=rotateVector({short(pitch&4095),56,0},{0,0,short(movement+128+int(nextGameRandom(m)&63))},sine);velocity=rotateVector({0,short(yaw),2048},velocity,sine);write(vel,velocity);bool valid=true;
   for(unsigned step=0;step<stream;++step){for(unsigned i=0;i<3;++i)m.w(pos+i*2,m.w(pos+i*2)+m.w(vel+i*2));if(!cell(p)){valid=false;break;}if(collideProjectileWithMap(m,p,services)){write(vel,{0,0,0});break;}}
   if(!valid){releaseProjectile(m,p);break;}activateProjectile(m,p);if(stream==1){first=true;m.w(0xa0aca,m.w(0xa0aca)-1);}
  }if(!first)return false;
 }else{
  // 0x2ba5c/0x2bb04: at most three targets in the original narrow cone,
  // nearest first; remaining rounds travel forward at staggered speeds.
  std::vector<unsigned> targets;for(auto e:active()){if(e==perchedQueen)continue;updateEnemyPlayerDistance(m,e);int difference=((yaw-m.s(e+0x54)+2048)&4095)-2048;if(m.b(e+0x74)<15&&m.b(e+0x6f)<6&&m.d(e+0x60)<5100&&std::abs(difference)<=128)targets.push_back(e);}
  std::stable_sort(targets.begin(),targets.end(),[&](unsigned a,unsigned b){return m.w(a+0x60)<m.w(b+0x60);});unsigned remaining=3;
  for(auto e:targets){if(!remaining||!m.w(0xa0acc))break;auto p=acquireProjectile(m);if(!p)return false;auto pos=m.d(p),vel=m.d(p+4);Vec3s v{position[0],short(position[1]+512),position[2]},off=rotateVector({0,short(yaw),2048},{0,0,400},sine);for(unsigned i=0;i<3;++i)v[i]+=off[i];write(pos,v);
   unsigned scratch=0x351800;int distance=enemyHorizontalDistance(m,pos,e);if(distance<2)distance=0;write(scratch,{short(m.s(pos+2)-m.s(e+2)-(m.s(e+8)>>1)),0,short(distance)});
   auto elevation=short(yawBetweenPoints(m,0x94b92,scratch)-2048);auto aimYaw=short(yawBetweenPoints(m,e,0xa0a48)&4095);
   auto velocity=rotateVector({elevation,0,0},{0,0,short(movement+256)},sine);velocity=rotateVector({0,aimYaw,2048},velocity,sine);write(vel,velocity);m.b(p+32,0);m.b(p+30,4);m.w(p+26,160);m.b(p+28,28);m.b(p+29,28);
   if(!cell(p)||collideProjectileWithMap(m,p,services)){releaseProjectile(m,p);return false;}activateProjectile(m,p);m.w(0xa0acc,m.w(0xa0acc)-1);--remaining;
  }
  while(remaining&&m.w(0xa0acc)){--remaining;m.w(0x236312,remaining);createPlayerShot(m,movement+remaining*128,4,services,0);m.w(0xa0acc,m.w(0xa0acc)-1);}
 }
 for(auto p=m.d(0x236300);p;p=m.d(p+36))if(std::find(existingProjectiles.begin(),existingProjectiles.end(),p)==existingProjectiles.end())m.b(p+31,0);
 if(weapon<3){double y=yaw*6.283185307179586/4096,pitchRadians=pitch*6.283185307179586/4096;for(auto p=m.d(0x236300);p;p=m.d(p+36))if(std::find(existingProjectiles.begin(),existingProjectiles.end(),p)==existingProjectiles.end()){auto vel=m.d(p+4);double speed=std::sqrt(double(m.s(vel))*m.s(vel)+double(m.s(vel+2))*m.s(vel+2)+double(m.s(vel+4))*m.s(vel+4));m.w(vel,short(std::lround(-std::sin(y)*std::cos(pitchRadians)*speed)));m.w(vel+2,short(std::lround(std::sin(pitchRadians)*speed)));m.w(vel+4,short(std::lround(std::cos(y)*std::cos(pitchRadians)*speed)));}}
 bool emitted=false;
 const unsigned shot=++survivalShotSerial;
 for(auto p=m.d(0x236300);p;p=m.d(p+36)){
    if(std::find(existingProjectiles.begin(),existingProjectiles.end(),p)!=existingProjectiles.end())continue;
    emitted=true;
    if(survivalMovement)survivalShotProjectiles[p]=shot;
    if(coopEmitted)coopEmitted(p);
 }
 unsigned used=before-m.w(ammunition[weapon]);
 if(!used&&!emitted)return false;
 if(m.b(0x94b9c)&32)m.w(ammunition[weapon],before);
 else{
    if(survivalSkullEnabled(survivalSkulls,GunMalfunctionSkull)){
        used=std::min(unsigned(before),used*2);
        m.w(ammunition[weapon],before-used);
    }
    ammoUsed+=used;
 }
 ++shots;
 constexpr unsigned noise[]={128,512,320,512,128};
 m.w(0xa0adc,noise[weapon]);return true;
}

// Player constructors 0x2be10 (pulse grenade) and 0x2c160 (seismic charge).
bool ExplorerEnemies::throwGrenade(const Vec3s& position,int yaw,int pitch,bool pulse,short movement,bool rocket){
 unsigned ammo=pulse?0xa0ac8:0xa0ad0;if(!health()||attached()||(!rocket&&!m.w(ammo)))return false;auto p=acquireProjectile(m);if(!p)return false;auto pos=m.d(p),vel=m.d(p+4);
 for(unsigned axis=0;axis<3;++axis)m.w(pos+axis*2,position[axis]+(axis==1?(pulse?640:512):0));auto velocity=rotateVector({short(pitch),0,0},{0,-24,short(std::min(int(movement)+(pulse?256:64),pulse?256:128))},sine);velocity=rotateVector({0,short(yaw),2048},velocity,sine);for(unsigned axis=0;axis<3;++axis)m.w(vel+axis*2,velocity[axis]);
 m.d(p+8,m.d(pulse?0x236bf4:0x236bdc));m.w(p+16,0);m.w(p+18,pulse?(-yaw)&4095:0);m.w(p+20,0);m.w(p+24,pulse?16:8);m.w(p+26,320);m.b(p+28,pulse?48:64);m.b(p+29,0);m.b(p+30,pulse?12:19);m.b(p+32,0);
 int x=(m.s(pos)+m.w(0xa0c34))>>9,z=(m.s(pos+4)+m.w(0xa0c36))>>9;if(x<0||z<0||x>=m.w(0xa0c30)||z>=m.w(0xa0c32)){releaseProjectile(m,p);return false;}m.d(p+12,m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16);activateProjectile(m,p);if(!rocket){
     if(coopEmitted)coopEmitted(p);
     if(survivalMovement)survivalShotProjectiles[p]=++survivalShotSerial;
     if(survivalMovement)++shots;
     if(!(m.b(0x94b9c)&32)){m.w(ammo,m.w(ammo)-1);++ammoUsed;}
 }m.w(0xa0adc,640);if(sound)sound(pulse?8:9,0);return true;
}
