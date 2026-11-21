#pragma once
#include "PlayerControl.h"
namespace trilogy::original {
void resetEnemyPool(PlayerControlMemory); // 0x2ddc8
unsigned spawnEnemyFromRecord(PlayerControlMemory,std::uint16_t,const PlayerControlService&); // 0x2dfc8
unsigned enemySpawnHealth(PlayerControlMemory,unsigned); // 0x2debc
void populateLevelEnemies(PlayerControlMemory,unsigned,std::uint16_t,const PlayerControlService&); // 0x2ed40
void initializeLevelEnemies(PlayerControlMemory,const PlayerControlService&); // 0x2ee20
void turnEnemyTowardYaw(PlayerControlMemory,unsigned,std::uint16_t); // 0x328d8
void setEnemyPathTarget(PlayerControlMemory,unsigned); // 0x32d68
unsigned enemyReachedTarget(PlayerControlMemory,unsigned); // 0x31cbc
void updateEnemyFacing(PlayerControlMemory,unsigned,const PlayerControlService&); // 0x30ecc
unsigned enemyWithinViewCone(PlayerControlMemory,unsigned); // 0x33430
unsigned enemyDetectsPlayer(PlayerControlMemory,unsigned); // 0x33484
PlayerControlService withEnemyRuntime(const PlayerControlService&);
}
