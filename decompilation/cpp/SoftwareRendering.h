#pragma once
#include "DropRendering.h"

namespace trilogy::original {
std::int32_t absoluteWrapped(std::int32_t); // 0x608d4
void initializeOrderingTable(PlayerControlMemory,std::uint32_t,std::uint16_t); // 0x48614
void submitModelNear(PlayerControlMemory,std::uint32_t model,std::uint32_t cell,const RenderService&); // 0x2504c
void submitModelMiddle(PlayerControlMemory,std::uint32_t model,std::uint32_t cell,const RenderService&); // 0x26358
void submitModelFar(PlayerControlMemory,std::uint32_t model,std::uint32_t cell,const RenderService&); // 0x25a6c
void rasterizeLinePacket(PlayerControlMemory,std::uint32_t); // 0x48bac (old false entry 0x48bb1)
void drawRenderPacket(PlayerControlMemory,std::uint32_t,const RenderService&); // 0x48a28
void clearSoftwareFramebuffers(PlayerControlMemory); // 0x489c4
void buildPaletteShadingTables(PlayerControlMemory); // 0x4877c
void buildPaletteBlendTable(PlayerControlMemory); // 0x486e4
void rebuildPaletteTables(PlayerControlMemory); // 0x486d4
void configureSoftwareViewport(PlayerControlMemory); // 0x467f4
void clearCurrentFramebuffer(PlayerControlMemory); // 0x5c4fe
// Packet callbacks obey the original preserved-EDX cursor convention.
// The host poll services the timer formerly advanced by a DOS interrupt.
using FrameTimerPoll=std::function<void(PlayerControlMemory&)>;
void renderSoftwareFrame(PlayerControlMemory,std::uint32_t head,const RenderService&,const FrameTimerPoll&); // 0x488cc
RenderService withSoftwareRendering(const RenderService& external);
}
