#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <span>
#include "ExplorerCoop.h"
namespace trilogy::coop {
// Co-op rules are chosen by the host. No network message accepts a point value.
struct TeamScore {
 enum class Event {AlienKill,EggKill,QueenKill,Pickup,MissionPassed,Death};
 std::int64_t total=0;
 std::array<unsigned,2> kills{},pickups{},deaths{};
 void award(unsigned player,Event event){
  if(player>=2)throw std::runtime_error("Invalid co-op scoring player");
  std::int64_t points=0;
  switch(event){
   case Event::AlienKill:points=100;++kills[player];break;
   case Event::EggKill:points=50;++kills[player];break;
   case Event::QueenKill:points=1000;++kills[player];break;
   case Event::Pickup:points=10;++pickups[player];break;
   case Event::MissionPassed:points=1000;break;
   case Event::Death:points=-100;++deaths[player];break;
   default:throw std::runtime_error("Invalid co-op scoring event");
  }
  total=std::clamp(total+points,std::int64_t(0),std::int64_t(999999999));
 }
};
inline constexpr unsigned StartingWeapon=0; // Original pistol slot.
// Fixed 30 Hz host simulation. Require two continuously safe seconds before
// respawning beside the survivor; threats are living hostile enemies only.
struct Recovery {
 enum class Action {None,RespawnHost,RespawnGuest,ReturnToLobby};
 struct Threat {std::array<short,3> position;bool alive=true;};
 unsigned safeTicks=0;
 Action tick(const std::array<Marine,2>& players,std::span<const Threat> threats){
  if(!players[0].health&&!players[1].health){safeTicks=0;return Action::ReturnToLobby;}
  if(players[0].health&&players[1].health){safeTicks=0;return Action::None;}
  unsigned survivor=players[0].health?0:1;
  auto& position=players[survivor].position;
  for(const auto& enemy:threats){
   if(!enemy.alive)continue;
   auto dx=std::int64_t(position[0])-enemy.position[0],dz=std::int64_t(position[2])-enemy.position[2];
   if(dx*dx+dz*dz<4096ll*4096){safeTicks=0;return Action::None;}
  }
  if(++safeTicks<60)return Action::None;
  safeTicks=0;
  return survivor?Action::RespawnHost:Action::RespawnGuest;
 }
};
}
