#include "DoorRuntime.h"
#include "EnemyMovement.h"
#include "EnemyAI.h"
#include "EnemyRuntime.h"
#include "LevelLoading.h"
#include "PolygonRasterizer.h"
#include "RenderCommands.h"
// Test-only C ABI for comparing the native C++ with original x86 in Unicorn.
#include "InputRepeat.h"
#include "OriginalUtilities.h"
#include "RuntimeCallbacks.h"
#include "MappedTaskScheduler.h"
#include "PlayerMovement.h"
#include "WorldMovement.h"
#include "PlayerStates.h"
#include "PlayerControl.h"
#include "WeaponRuntime.h"
#include "CombatRuntime.h"
#include "PlayerDamage.h"
#include "DropRendering.h"
#include "PickupRuntime.h"
#include "PickupScene.h"
#include "SoftwareRendering.h"
#include "ProjectileRuntime.h"
#include <cstring>
#include <vector>
#define PROBE extern "C" __declspec(dllexport)

PROBE void input_probe(std::uint32_t* words, std::uint16_t* counters,
                       std::uint32_t sample, int reset) {
    trilogy::original::InputRepeatState s{words[0],words[1],words[2],words[3],counters[0],counters[1]};
    if (reset) trilogy::original::resetInputRepeat(s);
    else trilogy::original::updateInputRepeat(s,sample);
    words[0]=s.output; words[1]=s.previous; words[2]=s.pressed; words[3]=s.held;
    counters[0]=s.repeatTicks; counters[1]=s.stableTicks;
}
PROBE std::uint32_t be32_probe(const std::uint8_t* p) {
    return trilogy::original::readBigEndian32(p);
}
PROBE std::uint32_t decimal_probe(const std::uint8_t* p) {
    return trilogy::original::parseDecimalUnchecked(p);
}
PROBE unsigned runtime_probe(std::uint32_t* rows, unsigned count, unsigned low,
                              unsigned high, int finalizers, std::uint32_t* log) {
    std::vector<trilogy::original::RuntimeCallback> table;
    for (unsigned i=0;i<count;++i)
        table.push_back({static_cast<std::uint8_t>(rows[i*3]),static_cast<std::uint8_t>(rows[i*3+1]),rows[i*3+2]});
    unsigned calls=0;
    auto invoke=[&](std::uint32_t address) {log[calls++]=address;};
    if (finalizers) trilogy::original::runFinalizers(table,static_cast<std::uint8_t>(low),static_cast<std::uint8_t>(high),invoke);
    else trilogy::original::runInitializers(table,static_cast<std::uint8_t>(high),invoke);
    for (unsigned i=0;i<count;++i) rows[i*3]=table[i].state;
    return calls;
}

// Snapshot: poolBase,head,freeHead,cursor,remainingTicks, then all 256 raw nodes.
PROBE std::uint32_t scheduler_probe(std::uint32_t* snapshot, unsigned operation,
    std::uint32_t first, std::uint32_t second, std::uint32_t* log, unsigned* logCount) {
    using namespace trilogy::original;
    TaskScheduler scheduler(snapshot[0]);
    scheduler.head=snapshot[1]; scheduler.freeHead=snapshot[2]; scheduler.cursor=snapshot[3];
    scheduler.remainingTicks=static_cast<std::uint8_t>(snapshot[4]);
    std::memcpy(scheduler.nodes.data(),snapshot+5,sizeof(scheduler.nodes));
    std::uint32_t result=0;
    unsigned calls=0;
    auto invoke=[&](TaskScheduler& s,TaskAddress address) {
        if (calls>=65536) throw "Test callback limit exceeded";
        log[calls++]=address;
        auto kind=s.node(address).callbackAddress;
        // Test callback implementations have equivalent x86 stubs in the oracle.
        if (kind==0x710020) s.remove(address);
        else if (kind==0x710040) {s.remainingTicks=1; s.advance(address);}
        else if (kind==0x710060) {
            auto priority=s.node(address).priority;
            s.create(priority,0x710000);
            s.advance(address);
        } else s.advance(address);
    };
    switch(operation) {
        case 0:scheduler.reset();break;
        case 1:result=scheduler.create(static_cast<std::int32_t>(first),second);break;
        case 2:scheduler.remove(first);break;
        case 3:scheduler.advance(first);break;
        case 4:scheduler.dispatch(invoke);break;
        case 5:scheduler.dispatchCompensated(first,second!=0,invoke);break;
        case 6:scheduler.insert(first);break;
    }
    snapshot[1]=scheduler.head; snapshot[2]=scheduler.freeHead; snapshot[3]=scheduler.cursor;
    snapshot[4]=scheduler.remainingTicks;
    std::memcpy(snapshot+5,scheduler.nodes.data(),sizeof(scheduler.nodes));
    *logCount=calls;
    return result;
}
PROBE void movement_probe(std::uint16_t* words,unsigned terrain,unsigned operation) {
    using namespace trilogy::original;
    PlayerMovementState s{words[0],words[1],words[2],words[3],static_cast<std::uint8_t>(terrain),words[4],words[5],words[6]};
    if(operation==0) accelerateForward(s);
    else if(operation==1) accelerateOtherDirection(s);
    else recenterPitch(s);
    words[0]=s.speed;words[1]=s.maximumSpeed;words[2]=s.acceleration;words[3]=s.terrainGate;
    words[4]=s.pitch;words[5]=s.pitchRate;words[6]=s.restPitch;
}
PROBE unsigned player_tick_probe(std::uint32_t* state,std::uint8_t* taskBytes,
    std::uint32_t movedY,std::uint32_t* log) {
    using namespace trilogy::original;
    TaskScheduler scheduler(0x300000);
    scheduler.cursor=state[2];scheduler.remainingTicks=static_cast<std::uint8_t>(state[3]);
    std::memcpy(&scheduler.nodes[0],taskBytes,32);
    PlayerTickState s{static_cast<std::uint8_t>(state[0]),state[1],static_cast<std::uint16_t>(state[4]),static_cast<std::uint16_t>(state[5]),static_cast<std::uint16_t>(state[6])};
    unsigned count=0;
    auto service=[&](std::uint32_t routine,PlayerTickState& p,TaskScheduler& tasks,TaskAddress a) {
        log[count++]=routine;
        if(routine==0x3d930) p.packedXY=movedY;
        if(routine==0x3e714) tasks.advance(a);
    };
    playerGroundedTick(s,scheduler,0x300000,service);
    state[0]=s.flowFlags;state[1]=s.packedXY;state[2]=scheduler.cursor;state[3]=scheduler.remainingTicks;
    state[4]=s.movement0;state[5]=s.movement1;state[6]=s.movement2;
    std::memcpy(taskBytes,&scheduler.nodes[0],32);
    return count;
}
PROBE void matrix_probe(const std::int16_t* angles,const std::int16_t* trig,std::int16_t* output) {
    using namespace trilogy::original;
    Vec3s a{angles[0],angles[1],angles[2]};
    auto m=buildRotation(a,std::span<const std::int16_t,4096>(trig,4096));
    std::memcpy(output,m.data(),18);
}
PROBE void transform_probe(const std::int16_t* matrix,const std::int16_t* vector,std::int32_t* output) {
    using namespace trilogy::original;
    Matrix3s m;std::memcpy(m.data(),matrix,18);
    auto result=transformVector(m,{vector[0],vector[1],vector[2]});std::memcpy(output,result.data(),12);
}
PROBE void rotate_probe(const std::int16_t* angles,const std::int16_t* vector,const std::int16_t* base,
    const std::int16_t* trig,std::int16_t* output,unsigned add) {
    using namespace trilogy::original;
    Vec3s a{angles[0],angles[1],angles[2]},v{vector[0],vector[1],vector[2]},b{base[0],base[1],base[2]};
    auto t=std::span<const std::int16_t,4096>(trig,4096);
    auto result=add?addRotatedVector(a,v,b,t):rotateVector(a,v,t);std::memcpy(output,result.data(),6);
}
PROBE std::uint32_t world_query_probe(std::uint8_t* cells,unsigned width,unsigned height,
    unsigned x,unsigned z,std::int16_t* displacement,unsigned flags,unsigned operation,unsigned tested) {
    using namespace trilogy::original;
    LevelGrid grid{static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height),
        std::span<LevelCell>(reinterpret_cast<LevelCell*>(cells),width*height)};
    Vec3s d{displacement[0],displacement[1],displacement[2]};std::uint32_t result=0;
    switch(operation) {
        case 0:result=floorHeight(grid,x,z);break;case 1:result=ceilingHeight(grid,x,z);break;
        case 2:result=clipDiagonalX(grid,x,z);break;case 3:result=clipDiagonalZ(grid,x,z);break;
        case 4:result=wallFallbackX(grid,x,z,tested,flags!=0);break;
        case 5:result=wallFallbackZ(grid,x,z,tested,flags!=0);break;
        case 6:result=collideX(grid,x,z,d,flags);break;case 7:result=collideZ(grid,x,z,d,flags);break;
    }
    std::memcpy(displacement,d.data(),6);return result;
}
namespace {
trilogy::original::WorldPlayerState unpack_world(const std::uint32_t* p) {
    using namespace trilogy::original;
    auto s=[](std::uint32_t n){return static_cast<std::int16_t>(n);};
    WorldPlayerState w;
    w.position={s(p[0]),s(p[1]),s(p[2])};w.moveAngles={s(p[3]),s(p[4]),s(p[5])};w.localMovement={s(p[6]),s(p[7]),s(p[8])};
    w.originX=p[9];w.originZ=p[10];w.currentCell=p[11];w.movementFlags=p[12];w.collisionFlags=p[13];w.terrainFlags=p[14];
    w.impulse=p[15];w.impulseYaw=p[16];w.terrainGate=p[17];w.damageGate=p[18];w.damageCooldown=p[19];
    w.health=p[20];w.region=p[21];w.pressedInput=p[22];w.useMask=p[23];return w;
}
void pack_world(std::uint32_t* p,const trilogy::original::WorldPlayerState& w) {
    for(unsigned i=0;i<3;++i){p[i]=std::uint16_t(w.position[i]);p[i+3]=std::uint16_t(w.moveAngles[i]);p[i+6]=std::uint16_t(w.localMovement[i]);}
    p[9]=w.originX;p[10]=w.originZ;p[11]=w.currentCell;p[12]=w.movementFlags;p[13]=w.collisionFlags;p[14]=w.terrainFlags;
    p[15]=w.impulse;p[16]=w.impulseYaw;p[17]=w.terrainGate;p[18]=w.damageGate;p[19]=w.damageCooldown;
    p[20]=w.health;p[21]=w.region;p[22]=w.pressedInput;p[23]=w.useMask;
}
}
PROBE unsigned world_move_probe(std::uint32_t* state,std::uint8_t* cells,unsigned width,unsigned height,
    const std::int16_t* trig,std::uint32_t* log) {
    using namespace trilogy::original;
    auto w=unpack_world(state);unsigned count=0;
    LevelGrid g{static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height),
        std::span<LevelCell>(reinterpret_cast<LevelCell*>(cells),width*height)};
    auto service=[&](std::uint32_t address,std::uint32_t a,std::uint32_t b,WorldPlayerState&,LevelGrid&) {
        log[count++]=address;
        log[count++]=(address==0x27b40)?0:a;
        log[count++]=(address==0x27b6c||address==0x27b40||address==0x408b0)?0:b;
    };
    movePlayer(w,g,std::span<const std::int16_t,4096>(trig,4096),service);
    pack_world(state,w);return count;
}
PROBE unsigned world_tick_probe(std::uint32_t* state,std::uint32_t* metadata,std::uint8_t* taskBytes,
    std::uint8_t* cells,unsigned width,unsigned height,const std::int16_t* trig,std::uint32_t* log) {
    using namespace trilogy::original;
    auto w=unpack_world(state);unsigned count=0;
    LevelGrid g{static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height),
        std::span<LevelCell>(reinterpret_cast<LevelCell*>(cells),width*height)};
    TaskScheduler tasks(0x300000);tasks.cursor=metadata[1];tasks.remainingTicks=static_cast<std::uint8_t>(metadata[2]);
    std::memcpy(&tasks.nodes[0],taskBytes,32);
    PlayerTickState tick{static_cast<std::uint8_t>(metadata[0]),
        std::uint16_t(w.position[0])|(std::uint32_t(std::uint16_t(w.position[1]))<<16),
        std::uint16_t(w.localMovement[0]),std::uint16_t(w.localMovement[1]),std::uint16_t(w.localMovement[2])};
    auto worldService=[&](std::uint32_t address,std::uint32_t a,std::uint32_t b,WorldPlayerState&,LevelGrid&) {
        log[count++]=address;log[count++]=address==0x27b40?0:a;
        log[count++]=(address==0x27b6c||address==0x27b40||address==0x408b0)?0:b;
    };
    auto other=[&](std::uint32_t address,PlayerTickState&,TaskScheduler& scheduler,TaskAddress task) {
        log[count++]=address;log[count++]=0;log[count++]=0;
        if(address==0x3e714)scheduler.advance(task);
    };
    playerTickWithWorld(tick,w,g,tasks,0x300000,std::span<const std::int16_t,4096>(trig,4096),worldService,other);
    pack_world(state,w);metadata[0]=tick.flowFlags;metadata[1]=tasks.cursor;metadata[2]=tasks.remainingTicks;
    std::memcpy(taskBytes,&tasks.nodes[0],32);return count;
}
PROBE unsigned player_states_probe(std::uint32_t* world,std::uint16_t* view,std::uint32_t* metadata,
    std::uint8_t* taskBytes,std::uint8_t* cells,unsigned width,unsigned height,const std::int16_t* trig,
    unsigned cameraOnly,std::uint32_t* log) {
    using namespace trilogy::original;
    PlayerState p;p.world=unpack_world(world);auto& v=p.view;
    v.angles={static_cast<std::int16_t>(view[0]),static_cast<std::int16_t>(view[1]),static_cast<std::int16_t>(view[2])};
    v.phase=view[3];v.eyeHeight=view[4];v.bobEnabled=view[5];v.pitchRate=view[6];v.restPitch=view[7];v.turnRate=view[8];
    v.weaponBobLow=view[9];v.weaponBobHigh=view[10];
    v.position={static_cast<std::int16_t>(view[11]),static_cast<std::int16_t>(view[12]),static_cast<std::int16_t>(view[13])};
    v.yaw=view[14];v.roll=view[15];v.pitch=view[16];v.sway=view[17];
    p.flowFlags=metadata[0];p.networkMode=metadata[1];p.deathLatch=metadata[2];p.respawnDelay=metadata[3];
    TaskScheduler tasks(0x300000);tasks.cursor=metadata[4];tasks.remainingTicks=metadata[5];
    std::memcpy(&tasks.nodes[0],taskBytes,32);
    LevelGrid grid{static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height),
        std::span<LevelCell>(reinterpret_cast<LevelCell*>(cells),width*height)};
    auto table=std::span<const std::int16_t,4096>(trig,4096);unsigned count=0;
    auto record=[&](std::uint32_t address,std::uint32_t a,std::uint32_t b,std::uint32_t c) {
        log[count++]=address;log[count++]=a;log[count++]=b;log[count++]=c;
    };
    auto effects=[&](PlayerState&,TaskScheduler&,TaskAddress,std::uint32_t address,
        std::uint32_t a,std::uint32_t b,std::uint32_t c) {record(address,a,b,c);};
    auto worldService=[&](std::uint32_t address,std::uint32_t a,std::uint32_t b,WorldPlayerState&,LevelGrid&) {
        if(address==0x27b40)a=b=0;
        if(address==0x27b6c||address==0x408b0)b=0;
        record(address,a,b,0);
    };
    if(cameraOnly)buildPlayerView(p,grid,table,tasks,0x300000,effects);
    else updatePlayerState(p,grid,table,tasks,0x300000,worldService,effects);
    pack_world(world,p.world);
    for(unsigned i=0;i<3;++i)view[i]=static_cast<std::uint16_t>(v.angles[i]);
    view[3]=v.phase;view[4]=v.eyeHeight;view[5]=v.bobEnabled;view[6]=v.pitchRate;view[7]=v.restPitch;view[8]=v.turnRate;
    view[9]=v.weaponBobLow;view[10]=v.weaponBobHigh;
    for(unsigned i=0;i<3;++i)view[i+11]=static_cast<std::uint16_t>(v.position[i]);
    view[14]=v.yaw;view[15]=v.roll;view[16]=v.pitch;view[17]=v.sway;
    metadata[0]=p.flowFlags;metadata[1]=p.networkMode;metadata[2]=p.deathLatch;metadata[3]=p.respawnDelay;
    metadata[4]=tasks.cursor;metadata[5]=tasks.remainingTicks;
    std::memcpy(taskBytes,&tasks.nodes[0],32);return count;
}
PROBE unsigned control_probe(std::uint8_t* bytes,unsigned size,unsigned operation,unsigned task,
    const std::uint32_t* responses,std::uint32_t* log) {
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};unsigned count=0;
    auto service=[&](PlayerControlMemory&,std::uint32_t address,std::uint32_t a,std::uint32_t b,std::uint32_t c) {
        log[count++]=address;log[count++]=a;log[count++]=b;log[count++]=c;
        auto mutations=responses[3];
        if((mutations&1)&&(address==0x2b4a4||address==0x2b4f0||address==0x2b548||address==0x2bb04||address==0x2b9d0)) {
            constexpr std::uint32_t ammo[]={0xa0abe,0xa0ac2,0xa0ac4,0xa0acc,0xa0aca};
            auto weapon=m.w(0xa0aae);if(weapon<5&&m.w(ammo[weapon]))m.w(ammo[weapon],m.w(ammo[weapon])-1u);
            m.w(0xa0aac,0);
        }
        if((mutations&2)&&address==0x59edd)m.d(0xa0c94,0x20);
        if((mutations&4)&&address==0x38958)m.w(0xa0aae,(m.w(0xa0aae)+1u)%5u);
        if((mutations&8)&&address==0x5de4c)m.d(0x3fae1a,0x00080010);
        if(address==0x38628)return PlayerControlResult{responses[0],0};
        if(address==0x10a50)return PlayerControlResult{responses[1],0};
        if(address==0x38170)return PlayerControlResult{0,responses[2]};
        return PlayerControlResult{};
    };
    switch(operation) {
        case 0:playerInputAndWeaponIntent(m,service);break;
        case 1:firePrimary(m,service);break;case 2:fireSecondary(m,service);break;
        case 3:resetPlayerInventory(m,service);break;case 4:initializePlayer(m,task,service);break;
        case 5:updateControlledPlayer(m,task,service);break;
    }
    return count;
}
PROBE std::uint32_t weapon_probe(std::uint8_t* bytes,unsigned size,unsigned operation,unsigned argument,
    const std::uint32_t* responses,std::uint32_t* log,unsigned* count) {
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};
    unsigned calls=0,projectiles=0;
    auto external=[&](PlayerControlMemory&,unsigned address,unsigned a,unsigned b,unsigned c) {
        log[calls++]=address;log[calls++]=a;log[calls++]=b;log[calls++]=c;
        if(address==0x2ae64)return PlayerControlResult{projectiles++<responses[0]?1u:0u,0};
        if(address==0x27f54)return PlayerControlResult{responses[1],0};
        if(address==0x38170)return PlayerControlResult{0,25};
        return PlayerControlResult{};
    };
    std::uint32_t result=0;
    switch(operation) {
        case 0:decodeAnimationCommands(m,argument);break;
        case 1:advanceAnimation(m,argument);break;
        case 2:startWeaponAnimation(m);break;
        case 3:selectWeapon(m,static_cast<std::uint16_t>(argument));break;
        case 4:result=hasAlternativeWeapon(m);break;
        case 5:result=cycleWeapon(m);break;
        case 6:result=discardAndCycleWeapon(m);break;
        case 7:fallbackToPistol(m);break;
        case 8:result=chooseWeaponSlot(m,static_cast<std::int16_t>(argument));break;
        case 9:updateWeaponView(m,external);break;
        case 10:spawnPistolShot(m,external);break;
        case 11:spawnShotgunShot(m,external);break;
        case 12:spawnPulseBurst(m,external);break;
        case 13:updateControlledPlayer(m,argument,withWeaponRuntime(external));break;
    }
    *count=calls;return result;
}

PROBE std::uint32_t projectile_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,std::uint32_t* log,unsigned* count) {
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto service=[&](PlayerControlMemory&,unsigned address,unsigned arg,unsigned,unsigned){log[(*count)++]=address;log[(*count)++]=arg;return PlayerControlResult{};};
    switch(op){
    case 0:return nextGameRandom(m);
    case 1:resetProjectilePool(m);break;
    case 2:return acquireProjectile(m);
    case 3:releaseProjectile(m,a);break;
    case 4:activateProjectile(m,a);break;
    case 5:unlinkProjectile(m,a,service);break;
    case 6:damageMapObject(m,static_cast<std::uint8_t>(a),static_cast<std::uint16_t>(b),service);break;
    case 7:return collideProjectileWithMap(m,a,service);
    case 8:return createPlayerShot(m,static_cast<std::uint16_t>(a),static_cast<std::uint8_t>(b),service,static_cast<std::uint16_t>(b>>16));
    case 9:case 10:case 11:{
        constexpr unsigned addresses[]={0x2b4a4,0x2b4f0,0x2b548};auto routed=withWeaponRuntime(withProjectileRuntime(service));routed(m,addresses[op-9],0,0,0);break;
    }
    }return 0;
}

PROBE std::uint32_t simulation_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,unsigned c,unsigned d,std::uint32_t* log,unsigned* count) {
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto service=[&](PlayerControlMemory& mem,unsigned address,unsigned x,unsigned y,unsigned z){
        log[(*count)++]=address;log[(*count)++]=x;log[(*count)++]=y;log[(*count)++]=z;
        if(address==0x30518)mem.b(x+0x6f,6);
        if(address==0x30ac0)mem.b(x+0x6f,1);
        return PlayerControlResult{};
    };
    switch(op){
        case 0:advanceVectorBatch(m,a,b,static_cast<std::int32_t>(c),static_cast<std::int32_t>(d));break;
        case 1:damageEntity(m,a,static_cast<std::uint16_t>(b),static_cast<std::uint16_t>(c),service);break;
        case 2:emitEntityHitEffect(m,a,b,service);break;
        case 3:return collideProjectileWithEntities(m,a,service);
        case 4:return collideProjectileWithPlayer(m,a,service);
        case 5:return collideBallisticWithMap(m,a,service);
        case 6:updateProjectiles(m,a,service);break;
        case 7:{auto f=withProjectileRuntime(service);f(m,0x2d234,a,0,0);break;}
        case 8:createPlayerShot(m,static_cast<std::uint16_t>(a),static_cast<std::uint8_t>(b),service,0);updateProjectiles(m,0x310000,service);break;
        case 9:rewindProjectile(m,a);break;
    }return 0;
}

PROBE void combat_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,unsigned c,std::uint32_t* log,unsigned* count) {
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto service=[&](PlayerControlMemory& mem,unsigned address,unsigned x,unsigned y,unsigned z){
        log[(*count)++]=address;log[(*count)++]=x;log[(*count)++]=y;log[(*count)++]=z;
        if(address==0x3b61c&&mem.b(0x310800)){
            mem.b(x+0x72,255);mem.w(x+0x32,16);mem.b(x+0x6e,2);
            if(mem.b(0x310800)==2)mem.b(x+0x74,8);
        }
        return PlayerControlResult{};
    };
    switch(op){
        case 0:saveTransformState(m);break;case 1:restoreTransformState(m);break;
        case 2:applyEntityKnockback(m,a,static_cast<std::uint16_t>(b),static_cast<std::uint16_t>(c));break;
        case 3:startEntityAnimation(m,a,static_cast<std::int16_t>(b));break;
        case 4:enterEntityPain(m,a,service);break;case 5:enterEntityDeath(m,a,service);break;
        case 6:applyAreaBlast(m,a,b,service);break;
        case 7:damageEntity(m,a,static_cast<std::uint16_t>(b),static_cast<std::uint16_t>(c),withCombatRuntime(service));break;
        case 8:updateProjectiles(m,a,withCombatRuntime(service));break;
    }
}

PROBE void damage_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,unsigned c,std::uint32_t* log,unsigned* count) {
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto service=[&](PlayerControlMemory& mem,unsigned address,unsigned x,unsigned y,unsigned z){
        log[(*count)++]=address;log[(*count)++]=x;log[(*count)++]=y;log[(*count)++]=z;log[(*count)++]=address==0x42ea4?255:0;
        if(address==0x3a444&&mem.b(0x310800)){mem.b(0xa0cc7,1);mem.b(0x9d53c,1);}
        if(address==0x18450&&mem.b(0x310808))mem.d(0x3f98f0,0x56789abc);
        return PlayerControlResult{address==0x18450?mem.d(0x310804):0,0};
    };
    auto draw=[&](PlayerControlMemory&,unsigned resource,std::int16_t x,std::int16_t y,std::uint8_t opacity){
        log[(*count)++]=0x42ea4;log[(*count)++]=resource;log[(*count)++]=static_cast<unsigned>(x);log[(*count)++]=static_cast<unsigned>(y);log[(*count)++]=opacity;
    };
    auto f=withPlayerDamageRuntime(service);
    switch(op){
        case 0:setDamageFeedback(m,static_cast<std::uint16_t>(a));break;
        case 1:markPlayerHurt(m);break;case 2:markPlayerDead(m);break;case 3:playPlayerDamageVoice(m,service);break;
        case 4:applyPlayerDamage(m,static_cast<std::uint16_t>(a),static_cast<std::uint16_t>(b),service);break;
        case 5:damagePlayer(m,static_cast<std::uint16_t>(a),static_cast<std::uint16_t>(b),service);break;
        case 6:damagePlayerDirect(m,static_cast<std::uint16_t>(a),static_cast<std::uint16_t>(b),service);break;
        case 7:damagePlayerDirectional(m,static_cast<std::uint16_t>(a),static_cast<std::uint16_t>(b),static_cast<std::uint16_t>(c),service);break;
        case 8:updatePlayerStatus(m,service,draw);break;
        case 9:collideProjectileWithPlayer(m,a,f);break;
        case 10:applyAreaBlast(m,a,b,f);break;
        case 11:updateProjectiles(m,a,withCombatRuntime(f));break;
        case 12:f(m,0x3dfc0,0,0,0);break;
    }
}

PROBE unsigned drop_render_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,std::uint32_t* log,unsigned* count) {
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto render=[&](PlayerControlMemory& mem,unsigned address,std::span<const unsigned> args)->unsigned {
        log[(*count)++]=address;log[(*count)++]=static_cast<unsigned>(args.size());
        for(unsigned i=0;i<4;++i)log[(*count)++]=i<args.size()?args[i]:0;
        if(address==0x47630)for(unsigned i=0;i<9;++i)mem.w(0xa0a28+i*2,100+i);
        if(address==0x46ec4)for(unsigned i=0;i<9;++i)mem.w(0x23ad08+i*2,200+i);
        return address==0x4d2a8?0x1357u:0u;
    };
    auto external=[&](PlayerControlMemory& mem,unsigned address,unsigned x,unsigned y,unsigned z){
        unsigned n=address==0x560a4?1:address==0x40460||address==0x2a3e0||address==0x404f8?3:address==0x2a6e0||address==0x2a4e8||address==0x51360?2:0;
        unsigned args[]={x,y,z};render(mem,address,std::span<const unsigned>(args,n));return PlayerControlResult{};
    };
    ScreenPoint screen{};if(op==1||op==6||op==8)screen={static_cast<std::int16_t>(m.s(b)),static_cast<std::int16_t>(m.s(b+2))};
    switch(op){
        case 0:spawnEntityDrop(m,a);break;
        case 1:{auto v=Vec3s{static_cast<std::int16_t>(m.s(a)),static_cast<std::int16_t>(m.s(a+2)),static_cast<std::int16_t>(m.s(a+4))};auto result=projectWorldPoint(m,v,screen,c);m.w(b,screen[0]);m.w(b+2,screen[1]);return static_cast<unsigned>(result);}
        case 2:return getRenderMode(m);
        case 3:blendVectorsQ12(m,a,b,static_cast<int>(c),static_cast<int>(d),e);break;
        case 4:{auto rgb=shadeRgb({m.b(a),m.b(a+1),m.b(a+2)},static_cast<int>(b));for(unsigned i=0;i<3;++i)m.b(c+i,rgb[i]);break;}
        case 5:enqueueRenderPacket(m,a,b);break;
        case 6:buildProjectileTrail(m,a,screen);break;
        case 7:buildProjectilePoint(m,ScreenPoint{static_cast<std::int16_t>(m.s(a)),static_cast<std::int16_t>(m.s(a+2))},b);break;
        case 8:buildProjectileDiamond(m,a,screen);break;
        case 9:buildProjectileGlow(m,ScreenPoint{static_cast<std::int16_t>(m.s(a)),static_cast<std::int16_t>(m.s(a+2))},b,render);break;
        case 10:buildProjectilePlume(m,ScreenPoint{static_cast<std::int16_t>(m.s(a)),static_cast<std::int16_t>(m.s(a+2))},static_cast<std::int16_t>(b),static_cast<std::int16_t>(c));break;
        case 11:buildProjectileFlame(m,ScreenPoint{static_cast<std::int16_t>(m.s(a)),static_cast<std::int16_t>(m.s(a+2))},static_cast<std::int16_t>(b),static_cast<std::int16_t>(c));break;
        case 12:renderProjectileMesh(m,a,render);break;case 13:renderProjectileShard(m,a,render);break;
        case 14:renderProjectile(m,a,render);break;
        case 15:enterEntityDeath(m,a,withDropRendering(external,render));break;
        case 16:updateProjectiles(m,a,withCombatRuntime(withPlayerDamageRuntime(withDropRendering(external,render))));break;
        case 17:{auto f=withDropRendering(external,render);f(m,0x2d004,a,0,0);break;}
    }return 0;
}

PROBE unsigned pickup_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,unsigned c,std::uint32_t* log,unsigned* count) {
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto external=[&](PlayerControlMemory& mem,unsigned address,unsigned x,unsigned y,unsigned z){
        log[(*count)++]=address;log[(*count)++]=x;log[(*count)++]=y;log[(*count)++]=z;
        if(address==0x3ab6c&&mem.b(0x310800))mem.b(0x9e0a8,1);
        return PlayerControlResult{};
    };
    switch(op){
        case 0:releaseDroppedPickup(m,a);break;case 1:resetDroppedPickupPool(m,external);break;
        case 2:moveDroppedPickup(m,a,external);break;case 3:return canTouchDroppedPickup(m,a);
        case 4:resetWeaponSlots(m);break;case 5:syncCurrentWeaponSlot(m);break;
        case 6:awardPickup(m,static_cast<std::uint16_t>(a),static_cast<std::uint16_t>(b),static_cast<std::uint16_t>(c));break;
        case 7:refreshCollectedWeapon(m,static_cast<std::uint16_t>(a));break;
        case 8:updateDroppedPickups(m,a,external);break;
        case 9:{auto f=withPickupRuntime(external);f(m,0x3c250,a,0,0);break;}
        case 10:return resetPlayerInventory(m,withPickupRuntime(external)).ecx;
        case 11:spawnEntityDrop(m,a);updateDroppedPickups(m,0x310000,external);break;
    }return 0;
}

PROBE void pickup_scene_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,unsigned c,unsigned d,std::uint32_t* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto renderer=[&](PlayerControlMemory&,unsigned address,std::span<const unsigned> args)->unsigned{
        log[(*count)++]=address;log[(*count)++]=static_cast<unsigned>(args.size());for(unsigned i=0;i<4;++i)log[(*count)++]=i<args.size()?args[i]:0;return 0;
    };
    auto external=[&](PlayerControlMemory& mem,unsigned address,unsigned x,unsigned y,unsigned z){
        unsigned n=address==0x5225c?2:address==0x40460?3:address==0x3ab6c||address==0x52138?1:0;
        unsigned args[]={x,y,z};renderer(mem,address,std::span<const unsigned>(args,n));return PlayerControlResult{};
    };
    auto f=withPickupScene(external,renderer);
    switch(op){
        case 0:buildObjectRotation(m,a,b);break;case 1:composeObjectRotation(m,a,b);break;
        case 2:scaleObjectRotation(m,a,{static_cast<int>(m.d(b)),static_cast<int>(m.d(b+4)),static_cast<int>(m.d(b+8))});break;
        case 3:installObjectRotation(m,a);break;case 4:installObjectTranslation(m,a);break;case 5:assignObjectTranslation(m,a,b);break;
        case 6:populateLevelPickups(m,a,static_cast<std::uint16_t>(b));break;
        case 7:initializeNetworkPickupPlacement(m);break;
        case 8:renderDroppedPickup(m,a,renderer);break;
        case 9:renderProjectile(m,a,withNativeRenderGeometry(renderer));break;
        case 10:resetDroppedPickupPool(m,f);break;
        case 11:updateDroppedPickups(m,a,f);break;
    }
}

PROBE unsigned software_render_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,std::uint32_t* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto renderer=[&](PlayerControlMemory& mem,unsigned routine,std::span<const unsigned> args)->unsigned{
        log[(*count)++]=routine;log[(*count)++]=static_cast<unsigned>(args.size());for(unsigned i=0;i<3;++i)log[(*count)++]=i<args.size()?args[i]:0;
        if(mem.b(0x310800)&&args.size()==3)mem.b(args[0]+18,mem.b(args[0]+18)|128);
        return 0;
    };
    switch(op){
        case 0:return static_cast<unsigned>(absoluteWrapped(static_cast<int>(a)));
        case 1:initializeOrderingTable(m,a,static_cast<std::uint16_t>(b));break;
        case 2:submitModelNear(m,a,b,renderer);break;case 3:submitModelMiddle(m,a,b,renderer);break;case 4:submitModelFar(m,a,b,renderer);break;
        case 5:rasterizeLinePacket(m,a);break;
        case 6:drawRenderPacket(m,a,withSoftwareRendering(renderer));break;
        case 7:clearSoftwareFramebuffers(m);break;
        case 9:buildPaletteShadingTables(m);break;
        case 10:buildPaletteBlendTable(m);break;
        case 11:rebuildPaletteTables(m);break;
        case 8:{initializeOrderingTable(m,0x390000,256);renderProjectile(m,a,withSoftwareRendering(renderer));for(auto packet=0x390000+255*12;packet;packet=m.d(packet))drawRenderPacket(m,packet,withSoftwareRendering(renderer));break;}
    }return 0;
}

PROBE void software_frame_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned head,std::uint32_t* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto external=[&](PlayerControlMemory& mem,unsigned routine,std::span<const unsigned> args)->unsigned{
        auto packet=args.empty()?0:args[0];log[(*count)++]=routine;log[(*count)++]=packet;
        log[(*count)++]=mem.d(0x9d544);log[(*count)++]=mem.d(0x3f6e48);log[(*count)++]=mem.b(0x3fa91f);
        if(routine==0x577c8)mem.b(0x3fa91f,mem.b(0x310820));
        else {mem.b(0x3fa91f,mem.b(0x3fa91f)+mem.b(0x310821));if(mem.b(0x310822))mem.d(packet,mem.d(packet+8));}
        return 0;
    };
    if(op==0)configureSoftwareViewport(m);
    else if(op==1)clearCurrentFramebuffer(m);
    else renderSoftwareFrame(m,head,withSoftwareRendering(withRenderCommands(external)),[](PlayerControlMemory& mem){mem.b(0x3fa91f,mem.b(0x3fa91f)+1);});
}

PROBE unsigned render_commands_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,std::uint32_t* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto renderer=[&](PlayerControlMemory& mem,unsigned routine,std::span<const unsigned>)->unsigned{
        log[(*count)++]=routine;for(unsigned addr:{0x3f6de0u,0x3f6de4u,0x3f6de8u,0x3f6df0u,0x3f6df4u,0x3f6d9cu,0x9d544u,0x9d504u,0x9d508u})log[(*count)++]=mem.d(addr);
        if(mem.b(0x310820))mem.w(0x390000+16,mem.w(0x390000+16)+7);return 0;
    };
    switch(op){
        case 0:initializeRenderRectangle(m,a,b,c,d,e);break;case 1:buildDrawAreaPacket(m,a,b);break;case 2:buildDepthStatePacket(m,a,d);break;
        case 3:writeScreenPoint(m,a,b,c);break;case 4:setRenderClearColor(m,a,b,c);break;case 5:setProjectionCenter(m,a,b);break;
        case 6:readProjectionDepths(m,a,b,c,d);break;case 7:writeNeutralRgb(m,c);break;case 8:setPacketHeight(m,a,b);break;
        case 9:applyDepthStatePacket(m,a);break;case 10:applyDrawAreaPacket(m,a);break;case 11:buildClearBufferPacket(m,a);break;
        case 12:clearBaseBufferPacket(m);break;case 13:rasterizeRectanglePacket(m,a);break;case 14:submitFlatQuadPacket(m,a,renderer);break;
        case 15:drawRenderPacket(m,a,withRenderCommands(renderer));break;
        case 16:return resolveTexturePageBuffer(m,a);case 17:return screenTriangleArea(a,b,c);case 18:return triangleDepthBucket(m);case 19:return quadDepthBucket(m);
        case 20:modulateRgb(m,a,b,d);break;case 21:blendLongVectorsQ12(m,a,b,c,d,e);break;case 22:rasterizeGradientRectangle(m,a);break;
    }
    return 0;
}

PROBE unsigned polygon_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,std::uint32_t* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto spans=[&](PlayerControlMemory& mem,unsigned address,std::span<const unsigned>)->unsigned{
        log[(*count)++]=address;for(unsigned p=0x3f6cf0;p<0x3f6e88;p+=4)log[(*count)++]=mem.d(p);
        return 0;
    };
    switch(op){case 0:sortTriangleVertices(m);break;case 1:setupTexturedRectangle(m,a,spans);break;case 2:setupTriangleSpans(m,a,spans);break;case 3:rasterizeSolidSpans(m);break;
        case 4:setupTriangleSpans(m,a,withPolygonRasterizer(spans));break;
        case 5:submitFlatQuadPacket(m,a,withPolygonRasterizer(spans));break;
        case 6:renderSoftwareFrame(m,a,withSoftwareRendering(withRenderCommands(withPolygonRasterizer(spans))),[](PlayerControlMemory& mem){mem.b(0x3fa91f,3);});break;
        case 7:rasterizeShadedSprite(m);break;case 8:renderShadedSpritePacket(m,a);break;
        case 9:rasterizeIndexedTextureSpans(m);break;case 10:setupTriangleSpans(m,a,withPolygonRasterizer(spans));break;
        case 11:rasterizeTransparentTextureSpans(m);break;
        case 12:rasterizeShadedTextureSpans(m);break;case 13:rasterizeTransparentShadedSpans(m);break;case 14:rasterizeBlendedShadedSpans(m);break;
    }return 0;
}

PROBE unsigned level_loading_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,std::uint32_t* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto external=[&](PlayerControlMemory&,unsigned routine,unsigned,unsigned,unsigned){log[(*count)++]=routine;return PlayerControlResult{};};
    switch(op){case 0:relocateLevelGeometry(m,a);break;case 1:initializeLevelLayout(m,a,external);break;case 2:initializeLevelLayout(m,a,withLevelLoading(external));break;case 3:initializeDoorFloorHeights(m,a,b);break;case 4:initializeLiftFloorHeights(m,a,b);break;case 5:initializeLevelDoors(m);break;case 6:initializeLevelLifts(m);break;
        case 7:resetLevelCollisionState(m,a);break;case 8:initializeLevelLayout(m,a,withLevelLoading(external));resetLevelCollisionState(m,a);break;
        case 9:case 10:{LevelGrid grid;grid.width=m.w(0xa0c30);grid.height=m.w(0xa0c32);std::vector<LevelCell> cells(grid.width*static_cast<unsigned>(grid.height));auto start=m.d(0xa0c40);for(unsigned i=0;i<cells.size();++i)for(unsigned j=0;j<16;++j)cells[i].bytes[j]=m.b(start+i*16+j);grid.cells=cells;return static_cast<unsigned>(op==9?floorHeight(grid,a,b):ceilingHeight(grid,a,b));}
    }return 0;
}

PROBE unsigned enemy_runtime_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,std::uint32_t* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto external=[&](PlayerControlMemory&,unsigned routine,unsigned x,unsigned y,unsigned){log[(*count)++]=routine;log[(*count)++]=x;log[(*count)++]=routine==0x2ee40?y:0;return PlayerControlResult{};};
    switch(op){case 0:resetEnemyPool(m);break;case 1:return enemySpawnHealth(m,a);case 2:populateLevelEnemies(m,a,b,external);break;case 3:initializeLevelEnemies(m,external);break;case 4:turnEnemyTowardYaw(m,a,b);break;case 5:setEnemyPathTarget(m,a);break;case 6:return enemyReachedTarget(m,a);case 7:updateEnemyFacing(m,a,external);break;case 8:return enemyWithinViewCone(m,a);case 9:return enemyDetectsPlayer(m,a);}
    return 0;
}

PROBE unsigned enemy_construction_probe(std::uint8_t* bytes,unsigned size,unsigned index,std::uint32_t* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto external=[&](PlayerControlMemory& mem,unsigned routine,unsigned a,unsigned b,unsigned){log[(*count)++]=routine;log[(*count)++]=a;log[(*count)++]=routine==0x2840c?0:b;
        if(routine==0x12364){auto t=mem.b(0x310800)?0:mem.d(0x310804);mem.d(0x310804,mem.d(0x310804)+32);return PlayerControlResult{t,0};}return PlayerControlResult{};};
    return spawnEnemyFromRecord(m,static_cast<std::uint16_t>(index),external);
}

PROBE unsigned enemy_integration_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};
    auto external=[](PlayerControlMemory&,unsigned,unsigned,unsigned,unsigned)->PlayerControlResult{throw "Unrecovered enemy integration service";};
    auto service=withEnemyRuntime(withMappedTaskScheduler(withCombatRuntime(withWeaponRuntime(external))));
    if(op==0)return service(m,0x12258,0,0,0).eax;
    if(op==1)return service(m,0x2dfc8,a,0,0).eax;
    if(op==2)return service(m,0x2ee20,0,0,0).eax;
    if(op==3){initializeLevelLayout(m,0xa0bc8,withLevelLoading(service));resetLevelCollisionState(m,0xa0bc8);return 0;}
    return 0;
}

PROBE unsigned enemy_ai_probe(std::uint8_t* bytes,unsigned size,unsigned routine,unsigned a,unsigned b,unsigned* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto external=[&](PlayerControlMemory& mem,unsigned address,unsigned x,unsigned y,unsigned z){
        log[(*count)++]=address;log[(*count)++]=x;log[(*count)++]=y;log[(*count)++]=z;
        return PlayerControlResult{mem.d(routine==0x33cc8?0x310100+((address>>2)&255)*4:0x310000),0};};
    if(routine==0x33cc8){updateEnemyTask(m,a,external);return 0;}
    return withEnemyAI(external)(m,routine,a,b,0).eax;
}

PROBE unsigned enemy_tick_integration_probe(std::uint8_t* bytes,unsigned size,unsigned task,unsigned* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto external=[&](PlayerControlMemory& mem,unsigned routine,unsigned a,unsigned b,unsigned c){
        log[(*count)++]=routine;log[(*count)++]=a;
        log[(*count)++]=(routine==0x31d30||routine==0x32500)?b:0;
        log[(*count)++]=(routine==0x31d30||routine==0x32500)?c:0;
        return PlayerControlResult{mem.d(0x35b000+((routine>>2)&255)*4),0};};
    auto service=withEnemyAI(withEnemyRuntime(withMappedTaskScheduler(withCombatRuntime(withWeaponRuntime(withPlayerDamageRuntime(external))))));
    return service(m,0x33cc8,task,0,0).eax;
}

PROBE unsigned enemy_movement_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned e,unsigned x,unsigned z,unsigned y,unsigned negative,unsigned* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto external=[&](PlayerControlMemory&,unsigned routine,unsigned a,unsigned,unsigned){log[(*count)++]=routine;log[(*count)++]=a;return PlayerControlResult{};};
    switch(op){case 0:return enemyWallFallbackX(m,x,z,e,negative);case 1:return enemyWallFallbackZ(m,x,z,e,negative);case 2:return collideEnemyX(m,e,x,z,static_cast<std::int16_t>(y));case 3:return collideEnemyZ(m,e,x,z,static_cast<std::int16_t>(y));case 4:moveEnemy(m,e,external);}
    return 0;
}

PROBE unsigned enemy_moving_tick_integration_probe(std::uint8_t* bytes,unsigned size,unsigned task,unsigned* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto external=[&](PlayerControlMemory& mem,unsigned routine,unsigned a,unsigned b,unsigned c){
        log[(*count)++]=routine;log[(*count)++]=a;
        log[(*count)++]=(routine==0x31d30||routine==0x32500)?b:0;
        log[(*count)++]=(routine==0x31d30||routine==0x32500)?c:0;
        return PlayerControlResult{mem.d(0x35b000+((routine>>2)&255)*4),0};};
    auto service=withEnemyAI(withEnemyMovement(withEnemyRuntime(withMappedTaskScheduler(withCombatRuntime(withWeaponRuntime(withPlayerDamageRuntime(external)))))));
    return service(m,0x33cc8,task,0,0).eax;
}

PROBE unsigned door_runtime_probe(std::uint8_t* bytes,unsigned size,unsigned op,unsigned a,unsigned b,unsigned* log,unsigned* count){
    using namespace trilogy::original;PlayerControlMemory m{std::span<std::uint8_t>(bytes,size)};*count=0;
    auto external=[&](PlayerControlMemory&,unsigned r,unsigned x,unsigned y,unsigned z){log[(*count)++]=r;log[(*count)++]=x;log[(*count)++]=r==0x40460||r==0x51e68?y:0;log[(*count)++]=r==0x40460?z:0;return PlayerControlResult{};};
    return op==0?incrementDoorUnlock(m,a,b,external):op==1?requestDoorOpen(m,a,b,external):activateMapObject(m,a,b,external);
}
