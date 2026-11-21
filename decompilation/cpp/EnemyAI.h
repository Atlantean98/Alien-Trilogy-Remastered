#pragma once
#include "EnemyRuntime.h"
namespace trilogy::original {
// Square-root approximation in Q10, using the original 256-word table.
// The original routine terminates only for nonnegative signed inputs.
unsigned approximateRootQ10(PlayerControlMemory,unsigned); // 0x4d15c
unsigned enemyHorizontalDistance(PlayerControlMemory,unsigned,unsigned); // 0x31b0c
unsigned yawBetweenPoints(PlayerControlMemory,unsigned,unsigned); // 0x26504
void updateEnemyPlayerDistance(PlayerControlMemory,unsigned); // 0x31b68
void updateEnemyTargetDistance(PlayerControlMemory,unsigned); // 0x31c18
void unfoldEnemy(PlayerControlMemory,unsigned,std::int16_t,const PlayerControlService&); // 0x2eb40
void adjustEnemySpawnTrigger(PlayerControlMemory,std::uint8_t,std::uint8_t,const PlayerControlService&); // 0x2ebb4
void beginEnemyAttack(PlayerControlMemory,unsigned,unsigned,const PlayerControlService&); // 0x336d0
void applyEnemyAttackHit(PlayerControlMemory,unsigned,const PlayerControlService&); // 0x33a0c
void updateEnemyTask(PlayerControlMemory,unsigned,const PlayerControlService&,bool survival=false); // 0x33cc8
void updateEggTask(PlayerControlMemory,unsigned,const PlayerControlService&); // 0x35088
void updateQueenTask(PlayerControlMemory,unsigned,const PlayerControlService&); // 0x355d4
void updateColonistTask(PlayerControlMemory,unsigned,const PlayerControlService&); // 0x35ebc
PlayerControlService withEnemyAI(const PlayerControlService&,bool survival=false);
}
