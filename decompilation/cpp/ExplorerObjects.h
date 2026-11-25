#pragma once
#include "PlayerControl.h"
#include <functional>
#include <vector>
class ExplorerObjects {
public:
 void load(trilogy::original::PlayerControlMemory,bool walkThroughWalls=false);
 void tick();
 bool visible(unsigned i)const;
 unsigned respawn(unsigned kind);   // Survival: destroyed objects of this kind back where the map put them
 std::vector<unsigned char> networkRemoved()const;void networkRemoved(const std::vector<unsigned char>&);
 struct Effect {int x,y,z;unsigned age;bool barrel;};
 std::vector<Effect> effects;
 std::vector<unsigned> entries;
 std::function<void(unsigned,unsigned)> pickup,enemy;
 std::function<void(unsigned,unsigned)> blast,textureChange;
 std::function<void(unsigned)> sound;
 unsigned destroyed{},exploded{};
private:
 trilogy::original::PlayerControlMemory m{};
 std::vector<bool> removed;
 void clear(unsigned);
 void place(unsigned i,bool walkThroughWalls);
};
