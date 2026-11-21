#pragma once
#include "PlayerControl.h"
namespace trilogy::original {
unsigned enemyWallFallbackX(PlayerControlMemory,unsigned x,unsigned tested,unsigned movement,bool negative); // 0x30fc8
unsigned enemyWallFallbackZ(PlayerControlMemory,unsigned z,unsigned tested,unsigned movement,bool negative); // 0x314cc
unsigned collideEnemyX(PlayerControlMemory,unsigned entity,unsigned x,unsigned z,std::int16_t y); // 0x31004
unsigned collideEnemyZ(PlayerControlMemory,unsigned entity,unsigned x,unsigned z,std::int16_t y); // 0x31510
void moveEnemy(PlayerControlMemory,unsigned entity,const PlayerControlService&,bool preciseCrowd=false,bool allowQueenOverlap=false); // 0x31950
PlayerControlService withEnemyMovement(const PlayerControlService&);
}
