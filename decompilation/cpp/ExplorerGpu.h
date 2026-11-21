#pragma once
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <memory>
#include <vector>
#include <array>
struct ExplorerGpuVertex {float x,y,z,u,v,r,g,b,a;};
// One GPU scene implementation shared by D3D11, D3D12 and Vulkan.
class ExplorerGpu {
public:
 ExplorerGpu();~ExplorerGpu();
 bool initialize(HWND,const std::filesystem::path&,unsigned backend,bool vsync=true);
 std::vector<unsigned> supportedAntialiasing()const;unsigned setAntialiasing(unsigned);unsigned antialiasing()const;void setTextureFiltering(unsigned);unsigned textureFiltering()const;void setVsync(bool);void invalidate();HDC begin(unsigned width,unsigned height,unsigned uiWidth=0,unsigned uiHeight=0);
 void opaqueFan(const std::vector<unsigned>* texture,const ExplorerGpuVertex* vertices,unsigned count,bool fog);
 void triangle(const std::vector<unsigned>* texture,const std::array<ExplorerGpuVertex,3>&,bool fog=true,bool additive=false,bool translucent=false);
 void sprite(const std::vector<unsigned>& pixels,unsigned width,unsigned height,float left,float top,float scale,float z,bool flipped,float roll=0.0f,float originX=0.0f,float originY=0.0f);
 void setDepthOfField(float strength);void finish(bool opaque,unsigned brightness=255);void setPhotoFilter(unsigned filter);
 void screenshot(const std::filesystem::path&);const char* name()const;
 unsigned drawCalls()const;unsigned triangles()const;
private:struct Impl;std::unique_ptr<Impl> p;
};
