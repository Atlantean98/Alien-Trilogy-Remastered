#pragma once
// Host asset/display adapters; these are not counted as recovered routines.
#include "PlayerControl.h"
#include <filesystem>
#include <array>
#include <map>
#include <string>
#include <vector>
using ExplorerChunks=std::map<std::string,std::vector<unsigned char>>;
ExplorerChunks explorerChunks(const std::filesystem::path&);
std::vector<unsigned char> explorerRead(const std::filesystem::path&);
struct ExplorerTextureRect {unsigned page{},x{},y{},width{},height{};};
class ExplorerTextures {
public:
    void load(const std::filesystem::path&,int transparentIndex=-1);
    const ExplorerTextureRect* rect(unsigned index)const{return index<rects.size()?&rects[index]:nullptr;}
    unsigned pixel(unsigned page,int x,int y)const;
    std::vector<ExplorerTextureRect> rects;
    std::map<unsigned,std::vector<unsigned>> pages;
};
struct ExplorerDoor {unsigned record{},cell{},rotation{},lock{},hold{},baseAttribute{};int x{},z{},floor{},ceiling{};float amount{},previous{};unsigned remaining{};bool target{};std::vector<unsigned> cells;};
class ExplorerDoors {
public:
    void load(trilogy::original::PlayerControlMemory,int travel=1376);
    void tick(trilogy::original::PlayerControlMemory,int playerX,int playerZ,const std::function<bool(const ExplorerDoor&)>& extra={});
    std::vector<ExplorerDoor> entries;unsigned opened{},closed{};
};

std::vector<unsigned char> explorerDecodeFrame(const std::vector<unsigned char>&,unsigned,unsigned);

bool explorerPs1Image(const std::filesystem::path& root,const std::string& name,unsigned& width,unsigned& height,std::vector<unsigned>& pixels);
int explorerPs1Track(const std::filesystem::path& root,const std::string& code);
std::string explorerPs1SfxBank(const std::filesystem::path& root,const std::string& code);
