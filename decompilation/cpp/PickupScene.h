#pragma once
#include "DropRendering.h"

namespace trilogy::original {
void buildObjectRotation(PlayerControlMemory,std::uint32_t angles,std::uint32_t matrix); // 0x47630
void composeObjectRotation(PlayerControlMemory,std::uint32_t left,std::uint32_t rightOutput); // 0x47294
void scaleObjectRotation(PlayerControlMemory,std::uint32_t matrix,std::array<std::int32_t,3> scales); // 0x46ef4
void installObjectRotation(PlayerControlMemory,std::uint32_t matrix); // 0x46e4c
void installObjectTranslation(PlayerControlMemory,std::uint32_t transform); // 0x46ec4
void assignObjectTranslation(PlayerControlMemory,std::uint32_t transform,std::uint32_t translation); // 0x477f8
void populateLevelPickups(PlayerControlMemory,std::uint32_t rules,std::uint16_t count); // 0x3b2bc
void initializeNetworkPickupPlacement(PlayerControlMemory); // 0x3af20
void renderDroppedPickup(PlayerControlMemory,std::uint32_t,const RenderService&); // 0x3c074
RenderService withNativeRenderGeometry(const RenderService& external);
PlayerControlService withPickupScene(const PlayerControlService&,const RenderService&);
}
