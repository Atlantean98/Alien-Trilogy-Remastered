#pragma once
#include "PlayerControl.h"

namespace trilogy::original {
void decodeAnimationCommands(PlayerControlMemory,std::uint32_t object); // 0x28280
void advanceAnimation(PlayerControlMemory,std::uint32_t object); // 0x2840c
void startWeaponAnimation(PlayerControlMemory); // 0x3f9c4
void selectWeapon(PlayerControlMemory,std::uint16_t weapon); // 0x3fa98
std::uint32_t hasAlternativeWeapon(PlayerControlMemory); // 0x38628
std::uint32_t cycleWeapon(PlayerControlMemory); // 0x386d4
std::uint32_t discardAndCycleWeapon(PlayerControlMemory); // 0x38814
void fallbackToPistol(PlayerControlMemory); // 0x38958
std::uint32_t chooseWeaponSlot(PlayerControlMemory,std::int16_t slot); // 0x389a8
void updateWeaponView(PlayerControlMemory,const PlayerControlService&); // 0x3e228
void spawnPistolShot(PlayerControlMemory,const PlayerControlService&); // 0x2b4a4
void spawnShotgunShot(PlayerControlMemory,const PlayerControlService&); // 0x2b4f0
void spawnPulseBurst(PlayerControlMemory,const PlayerControlService&); // 0x2b548
// Routes recovered selection/animation and the three shot wrappers internally;
// forwards rendering, sound, low-level projectile creation and other effects.
PlayerControlService withWeaponRuntime(const PlayerControlService& external);
}
