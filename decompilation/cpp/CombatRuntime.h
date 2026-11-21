#pragma once
#include "PlayerControl.h"

namespace trilogy::original {
void saveTransformState(PlayerControlMemory); // 0x46c9c
void restoreTransformState(PlayerControlMemory); // 0x46d78
void applyEntityKnockback(PlayerControlMemory,std::uint32_t entity,std::uint16_t yaw,std::uint16_t speed); // 0x3298c
void startEntityAnimation(PlayerControlMemory,std::uint32_t entity,std::int16_t animation); // 0x2ee40
void enterEntityPain(PlayerControlMemory,std::uint32_t,const PlayerControlService&); // 0x30ac0
void enterEntityDeath(PlayerControlMemory,std::uint32_t,const PlayerControlService&); // 0x30518
void applyAreaBlast(PlayerControlMemory,std::uint32_t position,std::uint32_t sourceCell,const PlayerControlService&); // 0x362a8
// Compose as withProjectileRuntime(withCombatRuntime(external)).
PlayerControlService withCombatRuntime(const PlayerControlService& external);
}
