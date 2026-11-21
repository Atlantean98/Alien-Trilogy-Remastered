#pragma once
#include "PlayerControl.h"

namespace trilogy::original {
void releaseDroppedPickup(PlayerControlMemory,std::uint32_t); // 0x3b0ac
void resetDroppedPickupPool(PlayerControlMemory,const PlayerControlService&); // 0x3b43c
void moveDroppedPickup(PlayerControlMemory,std::uint32_t,const PlayerControlService&); // 0x3bbd8
std::uint32_t canTouchDroppedPickup(PlayerControlMemory,std::uint32_t); // 0x3bf50
void resetWeaponSlots(PlayerControlMemory); // 0x380c0
void syncCurrentWeaponSlot(PlayerControlMemory); // 0x380f4
void awardPickup(PlayerControlMemory,std::uint16_t kind,std::uint16_t amount,std::uint16_t reserve); // 0x38170
void refreshCollectedWeapon(PlayerControlMemory,std::uint16_t); // 0x3f8c8
void updateDroppedPickups(PlayerControlMemory,std::uint32_t task,const PlayerControlService&); // 0x3c250
// Inventory award preserves original ECX. The adapter supplies ECX=0 for the
// known initializer context (CL is zero); it is not an armor return value.
PlayerControlService withPickupRuntime(const PlayerControlService& external);
}
