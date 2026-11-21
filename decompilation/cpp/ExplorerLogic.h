#pragma once
#include "ExplorerAssets.h"
#include <functional>
struct ExplorerPanel {unsigned index{},kind{},rotation{},cell{};int x{},y{},z{};};
struct ExplorerLift {unsigned record{},model{},width{},height{},gx{},gz{},threshold{},progress{},flags{},hold{},remaining{};int x{},z{},low{},high{},y{},previous{};bool moving{},targetHigh{},atHigh{};};
class ExplorerLogic {
public:
    void load(trilogy::original::PlayerControlMemory,ExplorerDoors&,bool rearmGoopPanels=false);
    std::wstring use(trilogy::original::PlayerControlMemory,ExplorerDoors&,int x,int y,int z,int yaw);
    std::wstring tick(trilogy::original::PlayerControlMemory,ExplorerDoors&,int x,int y,int z,bool advance=true);
    bool action(trilogy::original::PlayerControlMemory,ExplorerDoors&,unsigned index,bool usingPanel,unsigned activator=1);
    bool command(trilogy::original::PlayerControlMemory,ExplorerDoors&,unsigned commandIndex);
    bool runAction(trilogy::original::PlayerControlMemory,ExplorerDoors&,unsigned index,bool usingPanel,unsigned activator);
    unsigned playerAction()const{return lastAction;}void playerAction(unsigned value){lastAction=value;}
    void projectile(trilogy::original::PlayerControlMemory,ExplorerDoors&,unsigned);
    unsigned panelAction(trilogy::original::PlayerControlMemory,unsigned index)const;
    bool panelOn(trilogy::original::PlayerControlMemory,unsigned index) const;
    std::function<void(unsigned)> sound;
    std::function<bool()> exit;
    std::function<void(unsigned,unsigned)> spawnEnemy;
    std::function<void(unsigned,unsigned)> lightChange,textureChange,triggerPickup;
    std::vector<ExplorerLift> lifts;std::vector<ExplorerPanel> panels;
    std::wstring message;unsigned unsupported{},activated{},collected{};
private:
    bool rearmGoopPanels=false;
    unsigned actions{},commands{},actionCount{},commandCount{},lastAction{};
    std::vector<unsigned> enabled;std::vector<bool> powered,counted;std::vector<unsigned> armed;
    bool runCommands(trilogy::original::PlayerControlMemory,ExplorerDoors&,unsigned,bool,bool);
    bool has(trilogy::original::PlayerControlMemory,unsigned action,unsigned opcode,unsigned target)const;
    bool lineOfSight(trilogy::original::PlayerControlMemory,int,int,int,int)const;
    void open(ExplorerDoors&,unsigned);
};
