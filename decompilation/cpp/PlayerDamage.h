#pragma once
#include "PlayerControl.h"

namespace trilogy::original {
void setDamageFeedback(PlayerControlMemory,std::uint16_t); // 0x157c4
void markPlayerHurt(PlayerControlMemory); // 0x34ab4
void markPlayerDead(PlayerControlMemory); // 0x34acc
void playPlayerDamageVoice(PlayerControlMemory,const PlayerControlService&); // 0x3df58
void applyPlayerDamage(PlayerControlMemory,std::uint16_t amount,std::uint16_t type,const PlayerControlService&); // 0x3dde8
void damagePlayer(PlayerControlMemory,std::uint16_t amount,std::uint16_t type,const PlayerControlService&); // 0x3dee8
void damagePlayerDirect(PlayerControlMemory,std::uint16_t amount,std::uint16_t type,const PlayerControlService&); // 0x3df04
void damagePlayerDirectional(PlayerControlMemory,std::uint16_t amount,std::uint16_t yaw,std::uint16_t type,const PlayerControlService&); // 0x3df14
using PlayerStatusSprite=std::function<void(PlayerControlMemory&,std::uint32_t,std::int16_t,std::int16_t,std::uint8_t)>;
void updatePlayerStatus(PlayerControlMemory,const PlayerControlService&,const PlayerStatusSprite&); // 0x3dfc0
// The legacy three-argument provider for 0x42ea4 implicitly draws at opacity
// 255. The direct status API exposes that fourth argument explicitly.
PlayerControlService withPlayerDamageRuntime(const PlayerControlService& external);
}
