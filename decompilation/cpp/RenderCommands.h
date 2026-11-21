#pragma once
#include "SoftwareRendering.h"
namespace trilogy::original {
void initializeRenderRectangle(PlayerControlMemory,unsigned,int x,int y,int width,int height); // 4c510
void buildDrawAreaPacket(PlayerControlMemory,unsigned,unsigned rectangle); // 4c554
void buildDepthStatePacket(PlayerControlMemory,unsigned,std::uint16_t); // 4c58c
void writeScreenPoint(PlayerControlMemory,unsigned,std::uint16_t,std::uint16_t); // 4c5d4
void setRenderClearColor(PlayerControlMemory,std::uint8_t,std::uint8_t,std::uint8_t); // 4c5e4
void setProjectionCenter(PlayerControlMemory,unsigned,unsigned); // 4c5fc
void readProjectionDepths(PlayerControlMemory,unsigned,unsigned,unsigned,unsigned); // 4cf28
void writeNeutralRgb(PlayerControlMemory,unsigned); // 4cf4c
void setPacketHeight(PlayerControlMemory,unsigned,std::uint16_t); // 4cf64
void applyDepthStatePacket(PlayerControlMemory,unsigned); // 48a3c
void applyDrawAreaPacket(PlayerControlMemory,unsigned); // 4bc38
void buildClearBufferPacket(PlayerControlMemory,unsigned); // 4c504
void clearBaseBufferPacket(PlayerControlMemory); // 4bc6c
void rasterizeRectanglePacket(PlayerControlMemory,unsigned); // 4bc88
void submitFlatQuadPacket(PlayerControlMemory,unsigned,const RenderService&); // 48e40
unsigned resolveTexturePageBuffer(PlayerControlMemory,std::uint16_t); // 4d578
unsigned screenTriangleArea(unsigned,unsigned,unsigned); // 4de74
int triangleDepthBucket(PlayerControlMemory); // 4dee0
int quadDepthBucket(PlayerControlMemory); // 4df0c
void modulateRgb(PlayerControlMemory,unsigned factors,unsigned rgb,unsigned output); // 4dfc0
void blendLongVectorsQ12(PlayerControlMemory,unsigned,unsigned,unsigned,unsigned,unsigned); // 4cfe8
void rasterizeGradientRectangle(PlayerControlMemory,unsigned); // 4d834
RenderService withRenderCommands(const RenderService&);
}
