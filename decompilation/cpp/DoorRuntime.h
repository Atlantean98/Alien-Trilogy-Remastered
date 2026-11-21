#pragma once
#include "PlayerControl.h"
namespace trilogy::original {
unsigned incrementDoorUnlock(PlayerControlMemory,std::uint8_t,std::uint8_t,const PlayerControlService&); // 0x28e84
unsigned requestDoorOpen(PlayerControlMemory,std::uint8_t,std::uint8_t,const PlayerControlService&); // 0x28f00
unsigned activateMapObject(PlayerControlMemory,std::uint8_t,std::uint8_t,const PlayerControlService&); // 0x3692c
}
