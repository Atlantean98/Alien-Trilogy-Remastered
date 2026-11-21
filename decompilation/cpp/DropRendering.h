#pragma once
#include "PlayerControl.h"
#include "WorldMovement.h"

namespace trilogy::original {
using ScreenPoint=std::array<std::int16_t,2>;
// Meaningful scalar arguments; stack-local vectors are supplied by value.
using RenderService=std::function<std::uint32_t(PlayerControlMemory&,std::uint32_t,std::span<const std::uint32_t>)>;
void spawnEntityDrop(PlayerControlMemory,std::uint32_t entity); // 0x3b61c
std::int32_t projectWorldPoint(PlayerControlMemory,const Vec3s&,ScreenPoint&,std::uint32_t flagsAddress); // 0x47f40
std::uint16_t getRenderMode(PlayerControlMemory); // 0x12ed4
void blendVectorsQ12(PlayerControlMemory,std::uint32_t a,std::uint32_t b,std::int32_t scaleA,std::int32_t scaleB,std::uint32_t output); // 0x4cf78
std::array<std::uint8_t,3> shadeRgb(const std::array<std::uint8_t,3>&,std::int32_t depth); // 0x4df44
void enqueueRenderPacket(PlayerControlMemory,std::uint32_t bucket,std::uint32_t packet); // 0x48658
void buildProjectileTrail(PlayerControlMemory,std::uint32_t,ScreenPoint); // 0x2ccfc
void buildProjectilePoint(PlayerControlMemory,ScreenPoint,std::uint32_t); // 0x2cacc
void buildProjectileDiamond(PlayerControlMemory,std::uint32_t,ScreenPoint); // 0x2cbd4
void buildProjectileGlow(PlayerControlMemory,ScreenPoint,std::uint32_t,const RenderService&); // 0x2c27c
void buildProjectilePlume(PlayerControlMemory,ScreenPoint,std::int16_t life,std::int16_t kind); // 0x2c3d8
void buildProjectileFlame(PlayerControlMemory,ScreenPoint,std::int16_t life,std::int16_t kind); // 0x2c614
void renderProjectileMesh(PlayerControlMemory,std::uint32_t,const RenderService&); // 0x2c82c
void renderProjectileShard(PlayerControlMemory,std::uint32_t,const RenderService&); // 0x2c96c
void renderProjectile(PlayerControlMemory,std::uint32_t,const RenderService&); // 0x2d004
// Compose below combat: withCombatRuntime(withDropRendering(external,renderer)).
PlayerControlService withDropRendering(const PlayerControlService&,const RenderService&);
}
