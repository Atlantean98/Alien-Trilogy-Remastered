#pragma once
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "ExplorerDisplay.h"
inline const char* explorerDeathMovie(unsigned type){constexpr const char* clips[]={"DEATH2","DEATH5","DEATH3","DEATH4","DEATH1","DEATH6"};return clips[std::min(type,5u)];}
class ExplorerMovie {
 std::ifstream file;std::vector<unsigned> pixels;unsigned width=0,height=0,num=0,den=0,frames=0,lastFrame=~0u,durationTicks=0;double elapsed=0;bool playing=false;
public:
 bool open(const std::filesystem::path& path){close();std::ifstream header(path,std::ios::binary);char magic[8];unsigned values[6]{};if(!header.read(magic,8)||std::string(magic,8)!="ATFMV002"||!header.read(reinterpret_cast<char*>(values),24))return false;width=values[0];height=values[1];num=values[2];den=values[3];frames=values[4];durationTicks=values[5];if(!width||width>640||!height||height>480||!num||num>120000||!den||den>10000||!frames||frames>100000||!durationTicks||durationTicks>150*1800)return false;auto video=path;video.replace_extension(".bgra");std::error_code error;auto bytes=std::filesystem::file_size(video,error);if(error||bytes!=uint64_t(width)*height*frames*4)return false;file.open(video,std::ios::binary);if(!file)return false;pixels.resize(size_t(width)*height);elapsed=0;lastFrame=~0u;playing=true;return true;}
 void close(){file.close();file.clear();pixels.clear();playing=false;}
 bool active()const{return playing;}
 double time()const{return elapsed;}
 double duration()const{return std::max(double(frames)*den/num,double(durationTicks)/150);}
 unsigned frame()const{return std::min(frames-1,unsigned(elapsed*num/den));}
 void tick(double seconds,bool focused){if(playing&&focused){elapsed+=std::max(0.,seconds);if(elapsed>=duration())playing=false;}}
 void skip(){if(elapsed>=.25)playing=false;}
 void draw(HDC dc,int w,int h){RECT screen{0,0,w,h};FillRect(dc,&screen,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));if(!playing)return;auto current=frame();if(current!=lastFrame){file.seekg(uint64_t(current)*width*height*4);if(!file.read(reinterpret_cast<char*>(pixels.data()),pixels.size()*4)){playing=false;return;}lastFrame=current;}auto canvas=explorerFit(w,h,4,3);auto fit=explorerFit(canvas.width,canvas.height,width,height);fit.x+=canvas.x;fit.y+=canvas.y;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-int(height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,fit.x,fit.y,fit.width,fit.height,0,0,width,height,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);}
};
