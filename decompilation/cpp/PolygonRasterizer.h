#pragma once
#include "RenderCommands.h"
namespace trilogy::original {
void sortTriangleVertices(PlayerControlMemory); // 0x57664
void setupTexturedRectangle(PlayerControlMemory,unsigned,const RenderService&); // 0x4a0e0
void setupTriangleSpans(PlayerControlMemory,unsigned,const RenderService&); // 0x4acb8
void rasterizeSolidSpans(PlayerControlMemory); // 0x5b428
void rasterizeShadedSprite(PlayerControlMemory); // 0x5c519
void renderShadedSpritePacket(PlayerControlMemory,unsigned); // 0x4abc0
void rasterizeIndexedTextureSpans(PlayerControlMemory); // 0x5a186
void rasterizeTransparentTextureSpans(PlayerControlMemory); // 0x5a567
void rasterizeShadedTextureSpans(PlayerControlMemory); // 0x5a991
void rasterizeTransparentShadedSpans(PlayerControlMemory); // 0x5ad18
void rasterizeBlendedShadedSpans(PlayerControlMemory); // 0x5b0f7
RenderService withPolygonRasterizer(const RenderService&);
}
