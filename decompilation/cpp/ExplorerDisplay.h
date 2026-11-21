#pragma once
#include <array>
struct ExplorerResolution {int width,height;const wchar_t* label;};
inline constexpr std::array<ExplorerResolution,9> ExplorerResolutions{{
 {1280,800,L"1280 x 800"},{1280,720,L"1280 x 720"},
 {1600,900,L"1600 x 900"},{1920,1080,L"1920 x 1080"},
 {2560,1440,L"2560 x 1440"},{2560,1600,L"2560 x 1600"},
 {3200,1800,L"3200 x 1800"},{3440,1440,L"3440 x 1440"},
 {3840,2160,L"3840 x 2160 (4K)"}
}};

struct ExplorerAspect {int x,y;const wchar_t* label;};
inline constexpr std::array<ExplorerAspect,6> ExplorerAspects{{
 {0,0,L"Auto"},{4,3,L"4:3"},{16,9,L"16:9"},
 {16,10,L"16:10"},{21,9,L"21:9"},{32,9,L"32:9"}
}};
struct ExplorerViewport {int x,y,width,height;};
inline ExplorerViewport explorerFit(int width,int height,int ratioX,int ratioY){
 if(!ratioX||!ratioY)return {0,0,width,height};int w=width,h=height;
 if(static_cast<long long>(width)*ratioY>static_cast<long long>(height)*ratioX)w=int(static_cast<long long>(height)*ratioX/ratioY);
 else h=int(static_cast<long long>(width)*ratioY/ratioX);
 return {(width-w)/2,(height-h)/2,w>0?w:1,h>0?h:1};
}
