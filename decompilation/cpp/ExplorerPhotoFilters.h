#pragma once
#include <algorithm>
#include <array>
#include <cmath>
inline constexpr std::array<const wchar_t*,7> ExplorerPhotoFilterNames{L"Normal",L"Noir (B&W)",L"Motion Tracker (Phosphor)",L"Surveillance (CCTV)",L"Hive Amber",L"Sepia",L"Predator Tint"};
inline constexpr std::array<const wchar_t*,7> ExplorerPhotoFilterLabels{L"Normal",L"Noir",L"Phosphor",L"CCTV",L"Hive Amber",L"Sepia",L"Predator Tint"};
// Match fs_world.sc: colour grading only, with no changes to gameplay lighting.
inline unsigned applyPhotoFilter(unsigned pixel,unsigned filter){
 if(!filter||filter>=ExplorerPhotoFilterNames.size())return pixel;
 float r=float(pixel>>16&255)/255,g=float(pixel>>8&255)/255,b=float(pixel&255)/255;
 float lum=std::clamp(r*.299f+g*.587f+b*.114f,0.f,1.f),lift=std::pow(lum,.72f);
 switch(filter){
 case 1:{float value=std::clamp((lift-.38f)*1.85f+.35f,0.f,1.f);r=g=b=value;break;}
 case 2:{float value=std::clamp(std::pow(lum,.58f)*1.35f,0.f,1.f);r=value*.035f;g=value;b=value*.12f;break;}
 case 3:{float value=std::clamp((lift-.32f)*1.4f+.35f,0.f,1.f);r=value*.30f;g=value*.82f;b=std::min(1.f,value*1.05f+.035f);break;}
 case 4:{float value=std::clamp(lift*1.18f,0.f,1.f);r=std::min(1.f,value*1.30f);g=value*.64f;b=value*.055f;break;}
 case 5:{float value=std::clamp((lift-.3f)*1.25f+.3f,0.f,1.f);r=std::min(1.f,value*1.18f);g=value*.85f;b=value*.48f;break;}
 case 6:{constexpr std::array<std::array<float,3>,6> palette{{{.005f,.005f,.06f},{.10f,.025f,.48f},{.72f,.025f,.25f},{1.f,.18f,.015f},{1.f,.78f,.02f},{1.f,1.f,.90f}}};float heat=std::pow(lum,.65f)*5;unsigned index=std::min(4u,unsigned(heat));float t=std::min(1.f,heat-index);r=palette[index][0]*(1-t)+palette[index+1][0]*t;g=palette[index][1]*(1-t)+palette[index+1][1]*t;b=palette[index][2]*(1-t)+palette[index+1][2]*t;break;}
 }
 auto byte=[](float value){return unsigned(std::lround(std::clamp(value,0.f,1.f)*255));};return (pixel&0xff000000)|(byte(r)<<16)|(byte(g)<<8)|byte(b);
}
