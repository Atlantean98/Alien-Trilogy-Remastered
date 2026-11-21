#include "ExplorerLogic.h"
#include "DoorRuntime.h"
#include "PickupRuntime.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
using namespace trilogy::original;
namespace {constexpr unsigned Objects=0x320000,Doors=0x330000;}
void ExplorerLogic::load(PlayerControlMemory m,ExplorerDoors& doors,bool repeatGoopPanels){
    rearmGoopPanels=repeatGoopPanels;
    panels.clear();lifts.clear();enabled.clear();unsupported=activated=collected=lastAction=0;message.clear();powered.assign(doors.entries.size(),false);counted.assign(m.w(0xa0c1e),false);armed.clear();
    m.d(0x2367fc,Objects);m.d(0x2348d8,Doors);m.b(0x9e0a8,0);m.w(0xa0ad2,0);
    for(unsigned i=0;i<m.w(0xa0c1e);++i){auto raw=m.d(0xa0c58)+i*16,p=Objects+i*40;for(unsigned j=0;j<40;++j)m.b(p+j,0);unsigned kind=m.b(raw+2);m.b(p+24,kind);
        if(kind!=22&&kind!=24&&kind!=26&&kind!=27&&kind!=32)continue;
        unsigned x=m.b(raw),z=m.b(raw+1),cell=m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16;
        m.b(p+25,1);m.w(0xa0bc8+kind*2,m.w(0xa0bc8+kind*2)+1);panels.push_back({i,kind,m.b(raw+14),cell,int(x*512+256)-m.w(0xa0c34),int(m.b(cell+11))*32,int(z*512+256)-m.w(0xa0c36)});
    }
    m.w(0xa0c0a,doors.entries.size());m.w(0xa0c0c,m.w(0xa0c22));
    for(unsigned i=0;i<doors.entries.size();++i){auto p=Doors+i*32;for(unsigned j=0;j<32;++j)m.b(p+j,0);auto& d=doors.entries[i];m.b(p+24,m.b(d.record+2));m.b(p+26,d.lock);m.b(p+27,0);if(!d.lock){powered[i]=true;d.target=true;}if(!d.lock||d.target){m.b(p+24,m.b(p+24)|128);m.w(0xa0b28,m.w(0xa0b28)+1);}}
    for(unsigned i=0;i<m.w(0xa0c22);++i){auto p=m.d(0xa0c60)+i*16;ExplorerLift lift{};lift.record=p;lift.gx=m.b(p);lift.gz=m.b(p+1);lift.width=m.b(p+2);lift.height=m.b(p+3);lift.low=m.b(p+4)*32;lift.high=m.b(p+5)*32;lift.flags=m.b(p+7);lift.hold=m.b(p+9);lift.threshold=m.b(p+10);lift.progress=m.b(p+11);lift.model=m.b(p+12);lift.x=int(lift.gx*512+256)-m.w(0xa0c34);lift.z=int(lift.gz*512+256)-m.w(0xa0c36);lift.atHigh=lift.targetHigh=(lift.flags&1)!=0;if(m.b(p+8))lift.flags|=64;if(lift.atHigh)lift.flags|=32;lift.y=lift.previous=lift.atHigh?lift.high:lift.low;if(lift.gx+lift.width>m.w(0xa0c30)||lift.gz+lift.height>m.w(0xa0c32))throw std::runtime_error("Lift rectangle outside grid");lifts.push_back(lift);
        for(unsigned z=0;z<lift.height;++z)for(unsigned x=0;x<lift.width;++x)m.b(m.d(0xa0c40)+((lift.gz+z)*m.w(0xa0c30)+lift.gx+x)*16+11,lift.y/32);
    }
    // 0x80014318/0x80014320: the original initializes lifts before map objects, so panel heights use lift floors.
    for(auto& panel:panels)panel.y=int(m.b(panel.cell+11))*32;
    actions=m.d(0xa0c64);commands=m.d(0xa0c68);actionCount=m.b(0xa0c2a);commandCount=m.b(0xa0c2b);
    for(unsigned i=0;i<actionCount;++i)enabled.push_back(m.b(actions+i*4+2));

}
bool ExplorerLogic::panelOn(PlayerControlMemory m,unsigned i)const{return m.w(Objects+i*40+26)!=0;}
bool ExplorerLogic::has(PlayerControlMemory m,unsigned action,unsigned opcode,unsigned target)const{
    if(!action||action>=actionCount)return false;unsigned pc=m.b(actions+action*4+1);bool seen[256]{};
    while(pc!=255&&pc<commandCount&&!seen[pc]){seen[pc]=true;auto p=commands+pc*4;if(m.b(p)==opcode&&(target==256||m.b(p+3)==target))return true;pc=m.b(p+1);}return false;
}
bool ExplorerLogic::lineOfSight(PlayerControlMemory m,int x,int z,int tx,int tz)const{
    unsigned steps=unsigned(std::hypot(tx-x,tz-z)/96)+1;
    for(unsigned i=1;i<steps;++i){int cx=(x+(tx-x)*int(i)/int(steps)+m.w(0xa0c34))>>9,cz=(z+(tz-z)*int(i)/int(steps)+m.w(0xa0c36))>>9;if(cx<0||cz<0||cx>=m.w(0xa0c30)||cz>=m.w(0xa0c32))return false;if(cx==((tx+m.w(0xa0c34))>>9)&&cz==((tz+m.w(0xa0c36))>>9))continue;auto p=m.d(0xa0c40)+(cz*m.w(0xa0c30)+cx)*16;if((m.b(p+7)&128)||m.b(p+13)<=m.b(p+11))return false;}return true;
}
void ExplorerLogic::open(ExplorerDoors& doors,unsigned i){auto& d=doors.entries[i];if(!d.target)++doors.opened;d.target=true;d.remaining=d.hold?std::max(90u,d.hold):0u;}
// 0x40db0: projectile-entered action cells use flag 2 and p+31 as the latch.
void ExplorerLogic::projectile(PlayerControlMemory m,ExplorerDoors& doors,unsigned p){
 auto pos=m.d(p);int x=(m.s(pos)+m.w(0xa0c34))>>9,z=(m.s(pos+4)+m.w(0xa0c36))>>9;
 if(x<0||z<0||x>=m.w(0xa0c30)||z>=m.w(0xa0c32))return;
 unsigned id=m.b(m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16+15);
 if(!id){m.b(p+31,0);return;}if(id==m.b(p+31))return;
 auto previous=lastAction;if(action(m,doors,id,false,2))m.b(p+31,id);else m.b(p+31,0);lastAction=previous;
}
// Repeatable (count 255) control-panel actions re-arm their panel after each use, so one
// switch can send a shared lift back and forth; a refused lift never burns the panel.
bool ExplorerLogic::action(PlayerControlMemory m,ExplorerDoors& doors,unsigned index,bool use,unsigned activator){
    armed.clear();bool ok=runAction(m,doors,index,use,activator);
    for(auto target:armed){
        const auto object=Objects+target*40;
        const auto kind=m.b(object+24);
        // L331's three goop-area lift controls are repeatable kind-27 panels.
        const bool repeatable=enabled[index]==255&&(kind==24||(rearmGoopPanels&&kind==27));
        if(!ok&&!repeatable){
            // The chain was refused after this panel switched on (a later step's prerequisite is missing): it goes back
            // off, with its battery, so it can be used again once the prerequisite is met - not latched with its work
            // half done (L131's secret pulse rifle wall counts seven such chains).
            m.w(object+26,0);
            const auto counter=0xa0ae6u+kind*2u;
            if(m.w(counter))m.w(counter,m.w(counter)-1);
            if((kind==22||kind==26)&&!m.b(0x9e0a8))m.w(0xa0ad2,m.w(0xa0ad2)+1);
            continue;
        }
        if(!repeatable)continue;
        m.w(object+26,0);
        const auto counter=0xa0ae6u+kind*2u;
        if(!ok||counted[target]){
            if(m.w(counter))m.w(counter,m.w(counter)-1);
        }else counted[target]=true;
    }
    armed.clear();return ok;
}
bool ExplorerLogic::runAction(PlayerControlMemory m,ExplorerDoors& doors,unsigned index,bool use,unsigned activator){
    if(!index||index>=actionCount||!enabled[index]||!(m.b(actions+index*4)&activator))return false;
    bool alreadyInCell=(activator==1)&&!use&&(index==lastAction);
    bool success=runCommands(m,doors,m.b(actions+index*4+1),use,alreadyInCell);
    if(success&&enabled[index]!=255)--enabled[index];
    return success;
}
// 0x406ec accepts a command index, bypassing the floor/panel action records.
// Queen death uses this route to unlock its exit without player interaction.
bool ExplorerLogic::command(PlayerControlMemory m,ExplorerDoors& doors,unsigned index){return runCommands(m,doors,index,true,false);}
bool ExplorerLogic::runCommands(PlayerControlMemory m,ExplorerDoors& doors,unsigned pc,bool use,bool alreadyInCell){
    unsigned budget=512;bool success=false;
    auto service=[&](PlayerControlMemory&,unsigned routine,unsigned a,unsigned,unsigned){if(routine==0x40460&&sound)sound(a);return PlayerControlResult{};};
    while(pc!=255){if(pc>=commandCount||!budget--){message=L"Invalid level action";return false;}auto p=commands+pc*4;unsigned op=m.b(p),delta=m.b(p+2),target=m.b(p+3);
        switch(op){
        case 0:if(alreadyInCell||target>=m.w(0xa0c3c))return false;if(lightChange)lightChange(delta,target);success=true;break; // light variants are reflected by texture/panel state below
        case 1:case 6:{if(target>=doors.entries.size()||(op==6&&!use)||(op==1&&alreadyInCell))return false;
            // A door already unlocked (no lock, or unlocked by another switch or its other side's request): that step is
            // done, so the chain carries on (else a switch pressed afterwards stops short of its later steps).
            if(!(m.b(Doors+target*32+24)&24)&&m.b(Doors+target*32+27)>=m.b(Doors+target*32+26)){if(op==6)open(doors,target);success=true;break;}unsigned result=op==1?incrementDoorUnlock(m,delta,target,service):requestDoorOpen(m,delta,target,service);if(!result){message=L"Power or another switch is required";return false;}if(m.b(Doors+target*32+27)>=m.b(Doors+target*32+26)){open(doors,target);if(op==1&&!(m.b(Doors+target*32+24)&64))powered[target]=true;}success=true;break;}
        case 2:if(alreadyInCell||target>=m.w(0xa0c1c))return false;if(triggerPickup)triggerPickup(delta,target);success=true;break;
        case 3:if(alreadyInCell||target>=m.w(0xa0c1a))return false;if(spawnEnemy)spawnEnemy(delta,target);success=true;break;
        case 4:if(!use||target>=m.w(0xa0c1e))return false;if(!activateMapObject(m,target,delta,service)){auto kind=m.b(Objects+target*40+24);message=(kind==22||kind==26)&&!panelOn(m,target)&&!m.w(0xa0ad2)?L"Battery required":L"Panel already activated";return false;}++activated;armed.push_back(target);success=true;break;
        case 5:case 7:{if(alreadyInCell||target>=lifts.size())return false;auto& lift=lifts[target];if(lift.moving||lift.remaining||lift.progress>=lift.threshold||(op==5&&int(lift.progress)<int(lift.threshold)-1)){message=L"Lift requires another switch";return false;}lift.progress=static_cast<unsigned char>(lift.progress+delta);if(lift.progress>=lift.threshold&&!(lift.flags&128)){lift.flags|=128;m.w(0xa0b2a,m.w(0xa0b2a)+1);}if(lift.progress>=lift.threshold){lift.targetHigh=!lift.atHigh;lift.moving=true;if(sound)sound(94);}success=true;break;}
        case 9:if(alreadyInCell)return false;if(textureChange)textureChange(delta,target);success=true;break;
        case 255:break;
        case 8:if(exit&&!exit()){if(message.empty())message=L"Mission objectives incomplete — press B";return false;}if(message.empty())message=L"Extraction complete";success=true;break;   // exit() may say why it refused, or where it led
        default:++unsupported;message=L"This level mechanism is not integrated yet";return false;
        }
        pc=m.b(p+1);
    }
    return success;
}
std::wstring ExplorerLogic::use(PlayerControlMemory m,ExplorerDoors& doors,int x,int y,int z,int yaw){
    message.clear();double angle=yaw*6.283185307179586/4096,fx=-std::sin(angle),fz=std::cos(angle),best=1100;const ExplorerPanel* selected=nullptr;
    for(auto& panel:panels){double dx=panel.x-x,dz=panel.z-z,d=std::hypot(dx,dz),facing=panel.rotation*6.283185307179586/8;if(panel.kind!=32&&(-dx*std::sin(facing)+dz*std::cos(facing))<0)continue;if(d<best&&std::abs(y-panel.y)<1024&&(d<350||(dx*fx+dz*fz)/std::max(1.,d)>.55)&&lineOfSight(m,x,z,panel.x,panel.z)){best=d;selected=&panel;}}
    // 0x36950: an already-latched panel returns silently, without another cue.
    if(selected){if(panelOn(m,selected->index))return {};auto i=panelAction(m,selected->index);if(!i)return L"No action linked to this panel";if(action(m,doors,i,true))return L"Panel activated";return message.empty()?L"Panel inactive":message;}
    best=1400;unsigned chosen=unsigned(doors.entries.size());for(unsigned i=0;i<doors.entries.size();++i){auto& d=doors.entries[i];double dx=d.x-x,dz=d.z-z,dist=std::hypot(dx,dz);if(dist<best&&(dist<400||(dx*fx+dz*fz)/std::max(1.,dist)>.55)){chosen=i;best=dist;}}
    if(chosen<doors.entries.size()){
        if(powered[chosen]){open(doors,chosen);if(sound)sound(42);return L"Door activated";}
        for(unsigned i=1;i<actionCount;++i)if(has(m,i,6,chosen)&&!has(m,i,4,256)){if(action(m,doors,i,true)){if(sound)sound(42);return L"Door activated";}return message.empty()?L"Door locked":message;}
        return L"Activate the linked power or battery panel";
    }
    return {};
}
std::wstring ExplorerLogic::tick(PlayerControlMemory m,ExplorerDoors& doors,int x,int y,int z,bool advance){
    if(advance){
    // 0x37564: activated pod lids hinge by -8 per tick to -256.
    for(auto& panel:panels)if(panel.kind==32){auto p=Objects+panel.index*40;if(m.w(p+26)&&m.s(p+12)>-256)m.w(p+12,m.s(p+12)-8);}

    for(auto& lift:lifts){lift.previous=lift.y;
        if(!lift.moving&&lift.remaining){if(!--lift.remaining){lift.targetHigh=!lift.atHigh;lift.moving=true;if(sound)sound(94);}}
        if(!lift.threshold&&!lift.moving&&!lift.remaining&&lift.progress==0){lift.targetHigh=!lift.atHigh;lift.moving=true;lift.progress=1;if(!(lift.flags&128)){lift.flags|=128;m.w(0xa0b2a,m.w(0xa0b2a)+1);}}
        if(lift.moving){int target=lift.targetHigh?lift.high:lift.low,speed=(lift.flags&16)?64:32;lift.y+=std::clamp(target-lift.y,-speed,speed);if(lift.y==target){lift.moving=false;lift.atHigh=lift.targetHigh;if(lift.hold&&((lift.flags&64)||(lift.atHigh!=((lift.flags&32)!=0))))lift.remaining=lift.hold;else{lift.progress=0;lift.remaining=0;}if(sound)sound(95);}}
        for(unsigned z=0;z<lift.height;++z)for(unsigned x=0;x<lift.width;++x)m.b(m.d(0xa0c40)+((lift.gz+z)*m.w(0xa0c30)+lift.gx+x)*16+11,lift.y/32);
        // Crates, barrels and the like standing on the lift ride it (they keep the height they had when the level loaded otherwise).
        for(unsigned o=0;o<m.w(0xa0c1e);++o){const auto p=Objects+o*40,cell=m.d(p+20);if(!cell||cell<m.d(0xa0c40))continue;const unsigned at=(cell-m.d(0xa0c40))/16,ox=at%m.w(0xa0c30),oz=at/m.w(0xa0c30);
            if(ox>=lift.gx&&ox<lift.gx+lift.width&&oz>=lift.gz&&oz<lift.gz+lift.height)m.w(p+2,lift.y);}
    }
    for(unsigned i=0;i<doors.entries.size();++i){auto& d=doors.entries[i];auto p=Doors+i*32;if(!d.target&&d.amount==0&&d.lock&&m.b(p+27)>=d.lock&&!powered[i])m.b(p+27,(m.b(p+24)&64)?0:d.lock-1);}
    }
    std::wstring result;
    // A refused floor action must retry after its switch prerequisite is met.
    // Latch only successful entry, so standing in the lift cannot consume it.
    int cx=(x+m.w(0xa0c34))>>9,cz=(z+m.w(0xa0c36))>>9;if(cx>=0&&cz>=0&&cx<m.w(0xa0c30)&&cz<m.w(0xa0c32)){auto id=m.b(m.d(0xa0c40)+(cz*m.w(0xa0c30)+cx)*16+15);if(!id)lastAction=0;else{message.clear();if(action(m,doors,id,false))lastAction=id;if(!message.empty())result=message;}}
    return result;
}

unsigned ExplorerLogic::panelAction(PlayerControlMemory m,unsigned index)const{
    // Some repeatable lift actions include a panel command after prerequisite
    // checks. The panel's own activation action starts with that object.
    for(unsigned i=1;i<actionCount;++i)if(m.b(actions+i*4)&1){auto pc=m.b(actions+i*4+1);if(pc<commandCount&&m.b(commands+pc*4)==4&&m.b(commands+pc*4+3)==index)return i;}
    for(unsigned i=1;i<actionCount;++i)if((m.b(actions+i*4)&1)&&has(m,i,4,index))return i;
    for(unsigned i=1;i<actionCount;++i){auto pc=m.b(actions+i*4+1);if(pc<commandCount&&m.b(commands+pc*4)==4&&m.b(commands+pc*4+3)==index)return i;}
    for(unsigned i=1;i<actionCount;++i)if(has(m,i,4,index))return i;return 0;
}
