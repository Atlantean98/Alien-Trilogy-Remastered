#include "PlayerMovement.h"
#include <bit>

namespace trilogy::original {
namespace {
std::int32_t signedWord(std::uint16_t word) {return std::bit_cast<std::int16_t>(word);}
void accelerate(PlayerMovementState& s,unsigned shift) {
    auto increment=signedWord(s.acceleration)>>shift;
    auto limit=signedWord(s.maximumSpeed)>>shift;
    s.speed=static_cast<std::uint16_t>(signedWord(s.speed)+increment);
    if (signedWord(s.speed)>limit) s.speed=static_cast<std::uint16_t>(limit);
}
void payloadWord(TaskNode32& task,unsigned index,std::int32_t value) {
    // Payload is kept opaque; write this observed 32-bit field without host casts.
    auto bits=static_cast<std::uint32_t>(value);
    for(unsigned i=0;i<4;++i) task.payload[index*4+i]=static_cast<std::uint8_t>(bits>>(i*8));
}
}
void accelerateForward(PlayerMovementState& s) noexcept {
    bool slowed=s.terrainGate==0 && (s.terrain==5 || s.terrain==9);
    accelerate(s,slowed?1:0);
}
void accelerateOtherDirection(PlayerMovementState& s) noexcept {
    bool slowed=s.terrainGate==0 && (s.terrain==5 || s.terrain==9);
    accelerate(s,slowed?2:1);
}
void recenterPitch(PlayerMovementState& s) noexcept {
    s.pitchRate=0;
    if(signedWord(s.pitch)<signedWord(s.restPitch)) {
        s.pitch=static_cast<std::uint16_t>(s.pitch+8u);
        if(signedWord(s.pitch)>signedWord(s.restPitch)) s.pitch=s.restPitch;
    } else if(signedWord(s.pitch)>signedWord(s.restPitch)) {
        s.pitch=static_cast<std::uint16_t>(s.pitch-8u);
        if(signedWord(s.pitch)<signedWord(s.restPitch)) s.pitch=s.restPitch;
    }
}

void playerGroundedTick(PlayerTickState& s,TaskScheduler& scheduler,TaskAddress address,
                        const InvokePlayerService& invoke) {
    if(s.flowFlags&0x20) {
        scheduler.node(address).callbackAddress=0x3e714;
        s.movement0=0; s.movement1=0; s.movement2=0;
        invoke(0x3e714,s,scheduler,address);
        return;
    }
    invoke(0x3e894,s,scheduler,address); // input/intent (also contains weapon logic)
    auto previous=signedWord(static_cast<std::uint16_t>(s.packedXY>>16));
    invoke(0x3d930,s,scheduler,address); // movement/collision wrapper
    auto current=signedWord(static_cast<std::uint16_t>(s.packedXY>>16));
    if(previous>current) {
        scheduler.node(address).callbackAddress=0x3e3c4;
        payloadWord(scheduler.node(address),0,current);
        s.packedXY=(s.packedXY&0xffffu)|(static_cast<std::uint32_t>(static_cast<std::uint16_t>(previous))<<16);
        payloadWord(scheduler.node(address),1,previous);
    } else if(previous<current) {
        scheduler.node(address).callbackAddress=0x3e638;
        payloadWord(scheduler.node(address),0,current);
        s.packedXY=(s.packedXY&0xffffu)|(static_cast<std::uint32_t>(static_cast<std::uint16_t>(previous))<<16);
    }
    invoke(0x3c94c,s,scheduler,address); // view pose wrapper
    if(scheduler.remainingTicks==1) {
        invoke(0x3dfc0,s,scheduler,address);
        invoke(0x3e228,s,scheduler,address);
    }
    scheduler.cursor=scheduler.node(address).next;
}
}
