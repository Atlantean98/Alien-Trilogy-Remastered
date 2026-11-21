#include "PlayerStates.h"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <stdexcept>

namespace trilogy::original {
namespace {
std::int16_t sw(std::uint16_t n){return std::bit_cast<std::int16_t>(n);}
std::int16_t narrow(std::int32_t n){return sw(static_cast<std::uint16_t>(n));}
std::uint32_t sx(std::int16_t n){return static_cast<std::uint32_t>(std::int32_t(n));}
std::int16_t payload(const TaskNode32& task,unsigned index) {
    return sw(std::uint16_t(task.payload[index*4])|(std::uint16_t(task.payload[index*4+1])<<8));
}
void payload(TaskNode32& task,unsigned index,std::int16_t n) {
    auto bits=sx(n);for(unsigned i=0;i<4;++i)task.payload[index*4+i]=static_cast<std::uint8_t>(bits>>(8*i));
}
void effect(PlayerState& p,TaskScheduler& tasks,TaskAddress task,const PlayerStateEffect& invoke,
            std::uint32_t address,std::uint32_t a=0,std::uint32_t b=0,std::uint32_t c=0) {
    invoke(p,tasks,task,address,a,b,c);
}
void recenter(PlayerState& p) {
    PlayerMovementState m;m.pitch=std::uint16_t(p.view.angles[0]);m.pitchRate=p.view.pitchRate;m.restPitch=p.view.restPitch;
    recenterPitch(m);p.view.angles[0]=sw(m.pitch);p.view.pitchRate=m.pitchRate;
}
void finish(PlayerState& p,LevelGrid& grid,std::span<const std::int16_t,4096> trig,
            TaskScheduler& tasks,TaskAddress task,const PlayerStateEffect& invoke) {
    buildPlayerView(p,grid,trig,tasks,task,invoke);
    if(tasks.remainingTicks==1) {
        effect(p,tasks,task,invoke,0x3dfc0);effect(p,tasks,task,invoke,0x3e228);
    }
    tasks.cursor=tasks.node(task).next;
}
void moveTransition(PlayerState& p,LevelGrid& grid,std::span<const std::int16_t,4096> trig,
                    TaskScheduler& tasks,TaskAddress task,const WorldService& services) {
    auto previous=p.world.position[1];movePlayer(p.world,grid,trig,services);
    auto current=p.world.position[1];
    if(current!=previous) {
        payload(tasks.node(task),0,current);p.world.position[1]=previous;
        tasks.node(task).callbackAddress=current<previous?0x3e3c4:0x3e638;
    }
}
bool enterDeath(PlayerState& p,const LevelGrid& grid,std::span<const std::int16_t,4096> trig,
                TaskScheduler& tasks,TaskAddress task,const PlayerStateEffect& invoke) {
    if(!(p.flowFlags&0x20))return false;
    tasks.node(task).callbackAddress=0x3e714;p.world.localMovement={0,0,0};
    playerDeathTick(p,grid,trig,tasks,task,invoke);return true;
}
}
void buildPlayerView(PlayerState& p,const LevelGrid& grid,std::span<const std::int16_t,4096> sine,
    TaskScheduler& tasks,TaskAddress task,const PlayerStateEffect& invoke) {
    auto& v=p.view;auto phase=v.phase;
    auto wave=std::int32_t(sine[phase&4095]);
    auto dip=std::min(wave,std::int32_t(sine[(phase+2048u)&4095]));
    auto speed=std::int32_t(p.world.localMovement[2]);
    v.phase=static_cast<std::uint16_t>(phase+((speed*4-speed)>>1));
    v.weaponBobHigh=static_cast<std::uint16_t>(dip>>6);
    v.weaponBobLow=static_cast<std::uint16_t>(sw(v.weaponBobHigh)>>3);
    v.position=p.world.position;v.position[1]=narrow(std::int32_t(v.position[1])+sw(v.eyeHeight));
    v.pitch=std::uint16_t(v.angles[0]);v.yaw=std::uint16_t(v.angles[1]);v.roll=std::uint16_t(v.angles[2]);
    if(v.bobEnabled) {
        v.sway=static_cast<std::uint16_t>(wave>>9);
        v.yaw=static_cast<std::uint16_t>(v.yaw+((wave>>9)>>1));
        v.position[1]=narrow(std::int32_t(v.position[1])+sw(v.weaponBobHigh));
    } else v.sway=0;
    if((v.phase&2047)<(phase&2047) && grid.cells[p.world.currentCell].bytes[10]!=9) {
        if(p.world.terrainFlags&0x40)effect(p,tasks,task,invoke,0x40460,0x26,0xffffffff,0xfff);
        else {
            effect(p,tasks,task,invoke,0x40460,0x2c,0xffffffff,0xfff);
            effect(p,tasks,task,invoke,0x51948,0x2c,sx(p.world.position[0]),sx(p.world.position[2]));
        }
    }
}
void playerDescendingTick(PlayerState& p,LevelGrid& grid,std::span<const std::int16_t,4096> trig,
    TaskScheduler& tasks,TaskAddress task,const WorldService& services,const PlayerStateEffect& invoke) {
    if(enterDeath(p,grid,trig,tasks,task,invoke))return;
    effect(p,tasks,task,invoke,0x3e894);
    auto target=payload(tasks.node(task),0);auto& y=p.world.position[1];
    if(target<y) {
        y=narrow(std::int32_t(y)-256);
        if(y<target) {
            auto gap=std::abs(std::int32_t(y)-target);
            if(gap<=256)payload(tasks.node(task),1,target);
            y=target;
        }
    }
    if(y==target) {
        auto distance=narrow(std::abs(std::int32_t(payload(tasks.node(task),1))-y));
        if(!p.world.damageGate) {
            auto cooldown=[&](){p.world.damageCooldown=p.networkMode?8:30;};
            if(distance>2560) {if(!(p.world.collisionFlags&4))p.world.health=0;cooldown();}
            else if(distance>2048) {if(!(p.world.collisionFlags&4))p.world.health=static_cast<std::uint16_t>(p.world.health-25u);cooldown();}
            else if(distance>1536) {if(!(p.world.collisionFlags&4))p.world.health=static_cast<std::uint16_t>(p.world.health-15u);cooldown();}
            else if(distance>1024) {if(!(p.world.collisionFlags&4))p.world.health=static_cast<std::uint16_t>(p.world.health-5u);cooldown();}
            if(sw(p.world.health)<=0) {
                p.world.health=0;cooldown();
                if(!(p.world.collisionFlags&4)){p.flowFlags|=0x20;effect(p,tasks,task,invoke,0x34acc);}
            }
        }
        tasks.node(task).callbackAddress=0x3f908;
        auto terrain=grid.cells[p.world.currentCell].bytes[10];
        if(terrain==9||terrain==8) {
            effect(p,tasks,task,invoke,0x40460,0x6b,0xffffffff,0x3fff);
            if(p.networkMode)effect(p,tasks,task,invoke,0x51948,0x6b,sx(p.world.position[0]),sx(p.world.position[2]));
        }
    }
    moveTransition(p,grid,trig,tasks,task,services);finish(p,grid,trig,tasks,task,invoke);
}
void playerAscendingTick(PlayerState& p,LevelGrid& grid,std::span<const std::int16_t,4096> trig,
    TaskScheduler& tasks,TaskAddress task,const WorldService& services,const PlayerStateEffect& invoke) {
    if(enterDeath(p,grid,trig,tasks,task,invoke))return;
    effect(p,tasks,task,invoke,0x3e894);
    auto target=payload(tasks.node(task),0);auto& y=p.world.position[1];
    if(y<target){y=narrow(std::int32_t(y)+256);if(y>target)y=target;}
    if(y==target)tasks.node(task).callbackAddress=0x3f908;
    moveTransition(p,grid,trig,tasks,task,services);finish(p,grid,trig,tasks,task,invoke);
}
void playerDeathTick(PlayerState& p,const LevelGrid& grid,std::span<const std::int16_t,4096> trig,
    TaskScheduler& tasks,TaskAddress task,const PlayerStateEffect& invoke) {
    if(!p.networkMode) {
        p.view.eyeHeight=static_cast<std::uint16_t>(p.view.eyeHeight-12u);
        if(sw(p.view.eyeHeight)<384){p.flowFlags|=0x40;p.view.eyeHeight=384;}
        recenter(p);p.view.turnRate=0;buildPlayerView(p,grid,trig,tasks,task,invoke);
    } else {
        if(sw(p.view.eyeHeight)<=384) {
            p.view.eyeHeight=384;
            if(!p.deathLatch) {
                effect(p,tasks,task,invoke,0x40460,0x55,0xffffffff,0x3fff);
                effect(p,tasks,task,invoke,0x51948,0x55,sx(p.world.position[0]),sx(p.world.position[2]));
                effect(p,tasks,task,invoke,0x551ac);p.deathLatch=static_cast<std::uint8_t>(p.deathLatch+1u);
            }
            p.flowFlags|=0x40000;
        } else p.view.eyeHeight=static_cast<std::uint16_t>(p.view.eyeHeight-12u);
        effect(p,tasks,task,invoke,0x512d0,7);effect(p,tasks,task,invoke,0x51808);
        recenter(p);p.view.turnRate=0;buildPlayerView(p,grid,trig,tasks,task,invoke);
        effect(p,tasks,task,invoke,0x55dfc);
        if((p.flowFlags&0x80000)&&!p.respawnDelay) {
            effect(p,tasks,task,invoke,0x4e5a8,0);tasks.node(task).callbackAddress=0x3fbc0;
        }
    }
    tasks.cursor=tasks.node(task).next;
}
void updatePlayerState(PlayerState& p,LevelGrid& grid,std::span<const std::int16_t,4096> trig,
    TaskScheduler& tasks,TaskAddress task,const WorldService& services,const PlayerStateEffect& invoke) {
    switch(tasks.node(task).callbackAddress) {
        case 0x3e3c4:playerDescendingTick(p,grid,trig,tasks,task,services,invoke);return;
        case 0x3e638:playerAscendingTick(p,grid,trig,tasks,task,services,invoke);return;
        case 0x3e714:playerDeathTick(p,grid,trig,tasks,task,invoke);return;
        case 0x3f908:break;
        default:throw std::invalid_argument("Player callback is not reconstructed by updatePlayerState");
    }
    PlayerTickState tick{static_cast<std::uint8_t>(p.flowFlags),
        std::uint16_t(p.world.position[0])|(std::uint32_t(std::uint16_t(p.world.position[1]))<<16),
        std::uint16_t(p.world.localMovement[0]),std::uint16_t(p.world.localMovement[1]),std::uint16_t(p.world.localMovement[2])};
    auto fromTick=[&](const PlayerTickState& t) {
        p.flowFlags=(p.flowFlags&0xffffff00u)|t.flowFlags;
        p.world.position[0]=sw(static_cast<std::uint16_t>(t.packedXY));p.world.position[1]=sw(static_cast<std::uint16_t>(t.packedXY>>16));
        p.world.localMovement={sw(t.movement0),sw(t.movement1),sw(t.movement2)};
    };
    auto dispatch=[&](std::uint32_t address,PlayerTickState& t,TaskScheduler& scheduler,TaskAddress current) {
        fromTick(t);
        if(address==0x3d930)movePlayer(p.world,grid,trig,services);
        else if(address==0x3c94c)buildPlayerView(p,grid,trig,scheduler,current,invoke);
        else if(address==0x3e714)playerDeathTick(p,grid,trig,scheduler,current,invoke);
        else effect(p,scheduler,current,invoke,address);
        t.flowFlags=static_cast<std::uint8_t>(p.flowFlags);
        t.packedXY=std::uint16_t(p.world.position[0])|(std::uint32_t(std::uint16_t(p.world.position[1]))<<16);
        t.movement0=std::uint16_t(p.world.localMovement[0]);t.movement1=std::uint16_t(p.world.localMovement[1]);t.movement2=std::uint16_t(p.world.localMovement[2]);
    };
    playerGroundedTick(tick,tasks,task,dispatch);fromTick(tick);
}
}
