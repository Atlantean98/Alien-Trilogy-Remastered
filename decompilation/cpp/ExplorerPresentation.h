#pragma once
#define NOMINMAX
#include <windows.h>
#include "ExplorerAssets.h"
class ExplorerFont {
public:
 void load(const std::filesystem::path& root);
 void loadSmall(const std::filesystem::path& root);
 int width(const std::wstring&,double scale)const;
 void paragraph(HDC,const std::wstring&,int x,int y,double scale,unsigned color=0xffffff)const;
 void draw(HDC,const std::wstring&,int x,int y,double scale,unsigned color=0xffffff)const;
 void box(HDC,const std::wstring&,RECT,double scale,unsigned color=0xffffff,bool center=false)const;
 void shadow(HDC dc,const std::wstring& text,int x,int y,double scale,unsigned color=0xffffff)const{draw(dc,text,x+std::max(1,int(scale)),y+std::max(1,int(scale)),scale,0);draw(dc,text,x,y,scale,color);}
 bool ps1()const{return ps1Font;}
private:ExplorerTextures atlas;bool ps1Font=false;unsigned glyphHeight=16;int glyphOffset=-32,advanceAdjust=0,spaceWidth=8;
};
class ExplorerModelPreview {
public:
 void load(const std::filesystem::path& root);
 void draw(HDC,unsigned model,bool option,int x,int y,int width,int height,double angle)const;
 unsigned draws()const{return rendered;}unsigned pixels()const{return drawnPixels;}
private:
 struct Vertex{double x,y,z;};struct Face{std::array<int,4> indices;unsigned texture,flags;};struct Mesh{std::vector<Vertex> vertices;std::vector<Face> faces;};
 std::map<unsigned,Mesh> pickups,options;ExplorerTextures pickupArt,optionArt;
 mutable unsigned rendered{},drawnPixels{};
};
