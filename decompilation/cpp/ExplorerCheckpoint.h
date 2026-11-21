#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
struct ExplorerCheckpoint {std::string code;unsigned difficulty=0,weapon=0,finished=0;std::array<unsigned char,50> inventory{};};
inline std::optional<ExplorerCheckpoint> explorerReadCheckpoint(const std::filesystem::path& path){
 std::ifstream file(path);std::string magic;unsigned version=0;ExplorerCheckpoint save;
 if(!(file>>magic>>version>>save.code>>save.difficulty>>save.weapon>>save.finished)||magic!="TRILOGY_NATIVE_PROGRESS"||version!=2||save.code.size()!=3||save.code.find_first_not_of("0123456789")!=std::string::npos||save.difficulty>2||save.weapon>4||save.finished>1)return {};
 for(auto& byte:save.inventory){unsigned value=256;if(!(file>>value)||value>255)return {};byte=static_cast<unsigned char>(value);}return save;
}
