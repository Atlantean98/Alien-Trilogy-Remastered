#pragma once
#include "PlayerControl.h"
#include "ExplorerAssets.h"
#include <filesystem>
#include <string>
#include <vector>
class ExplorerMission {
public:
 void load(trilogy::original::PlayerControlMemory,const std::filesystem::path&,const std::string&,unsigned);
 struct Statistics {std::array<unsigned,3> percent{},found{},total{};};
 Statistics statistics()const;void visit(unsigned cell);
 unsigned percent()const;
 bool eligible()const;
 unsigned minimumPercent()const{return index<12?16:index<22?26:46;}
 unsigned nextIndex()const;
 std::wstring progress()const;
 std::wstring title,text,markup;
 unsigned backgroundWidth{},backgroundHeight{};std::vector<unsigned> background;
 bool briefing=false,complete=false;
 std::vector<unsigned> kinds;
 unsigned index{},remaining{};
 std::string sourceCode;
 bool tick();
 std::string nextCode()const;
private:
 trilogy::original::PlayerControlMemory m{};
};
