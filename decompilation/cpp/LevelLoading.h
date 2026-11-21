#pragma once
#include "PlayerControl.h"
namespace trilogy::original {
// The input MAP0 payload and all calculated regions must fit the mapped store.
// Counts consumed as signed words must be nonnegative; face count >= 1.
void relocateLevelGeometry(PlayerControlMemory,unsigned state); // 0x272f0
void initializeLevelLayout(PlayerControlMemory,unsigned state,const PlayerControlService&); // 0x27530
void initializeDoorFloorHeights(PlayerControlMemory,unsigned records,std::uint16_t count); // 0x28810
void initializeLiftFloorHeights(PlayerControlMemory,unsigned records,std::uint16_t count); // 0x413f0
void initializeLevelDoors(PlayerControlMemory); // 0x28860
void initializeLevelLifts(PlayerControlMemory); // 0x41448
void resetLevelCollisionState(PlayerControlMemory,unsigned state); // 0x27c68
PlayerControlService withLevelLoading(const PlayerControlService&);
}
