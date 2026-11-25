#include "ExplorerObjects.h"
#include <algorithm>
#include <stdexcept>
using namespace trilogy::original;
namespace {constexpr unsigned Pool=0x320000;}
void ExplorerObjects::load(PlayerControlMemory memory,bool walkThroughWalls){
 m=memory;effects.clear();entries.clear();removed.assign(m.w(0xa0c1e),false);destroyed=exploded=0;
 // 0x36b38: the serialized health byte is measured in sixteen-point units.
 for(unsigned i=0;i<m.w(0xa0c1e);++i){auto r=m.d(0xa0c58)+i*16,p=Pool+i*40;unsigned kind=m.b(r+2);
  if(kind==22||kind==24||kind==26||kind==27||kind==32)continue;
  if(kind<20||kind>37)continue;
  entries.push_back(i);place(i,walkThroughWalls);
  if(kind<33)m.w(0xa0bc8+kind*2,m.w(0xa0bc8+kind*2)+1);
 }
}
// One object's pool slot, position and cell occupancy, from its map record (load, and respawn).
void ExplorerObjects::place(unsigned i,bool walkThroughWalls){
  auto r=m.d(0xa0c58)+i*16,p=Pool+i*40;unsigned kind=m.b(r+2);
  m.b(p+24,kind);m.b(p+25,m.b(r+3)|(kind>=33?1:0));m.w(p+26,m.b(r+4)*16);m.b(p+28,m.b(r+6));m.b(p+29,m.b(r+7));m.b(p+30,i);m.b(p+31,0);m.b(p+32,m.b(r+8));m.b(p+33,m.b(r+9));m.w(p+34,m.w(r+14));m.w(p+36,m.w(r+10));m.w(p+38,m.w(r+12));
  unsigned x=m.b(r),z=m.b(r+1),cell=m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16;
  m.w(p,int(x*512+256)-m.w(0xa0c34));m.w(p+4,int(z*512+256)-m.w(0xa0c36));m.w(p+2,m.b(cell+11)*32);m.d(p+20,cell);
  // L161's confirmed walk-through organic walls retain their visible geometry.
  // Do not turn their object rectangles into solid occupants. Other levels keep
  // the recovered blast-only wall behaviour, including L112's seismic secret.
  if(walkThroughWalls&&kind==21)return;
  // Large objects occupy their complete serialized rectangle (0x36a38).
  bool rectangle=kind==21||kind==28||kind==29;
  for(unsigned dz=0;dz<(rectangle?m.b(p+33):1);++dz)for(unsigned dx=0;dx<(rectangle?m.b(p+32):1);++dx)if(x+dx<m.w(0xa0c30)&&z+dz<m.w(0xa0c32)){auto c=cell+(dz*m.w(0xa0c30)+dx)*16;m.b(c+12,kind);m.b(c+4,i);}
}
// Survival: destroyed objects of one kind (explosive barrels) come back where the map put them. A cell something now
// stands on (an alien, the player's pickup) is left until the next time.
unsigned ExplorerObjects::respawn(unsigned kind){
 unsigned count=0;
 for(auto i:entries){
  if(!removed[i]||m.b(m.d(0xa0c58)+i*16+2)!=kind)continue;
  auto r=m.d(0xa0c58)+i*16;auto cell=m.d(0xa0c40)+(m.b(r+1)*m.w(0xa0c30)+m.b(r))*16;
  if(m.b(cell+12))continue;
  place(i,false);removed[i]=false;++count;
 }
 return count;
}
bool ExplorerObjects::visible(unsigned i)const{return i<removed.size()&&!removed[i];}
void ExplorerObjects::clear(unsigned p){
 auto cell=m.d(p+20);unsigned kind=m.b(p+24);bool rectangle=kind==21||kind==28||kind==29;
 // 0x36a88 releases the whole wall rectangle; floor and ceiling are retained.
 for(unsigned z=0;z<(rectangle?m.b(p+33):1);++z)for(unsigned x=0;x<(rectangle?m.b(p+32):1);++x)m.b(cell+(z*m.w(0xa0c30)+x)*16+12,0);
}
void ExplorerObjects::tick(){
 for(auto& effect:effects)++effect.age;std::erase_if(effects,[](auto& effect){return effect.age>=18;});
 for(auto i:entries){if(!visible(i))continue;auto p=Pool+i*40;if(!(m.b(p+25)&1)||!m.b(p+31))continue;
  m.b(p+31,m.b(p+31)-1);if(m.b(p+31))continue;
  auto kind=m.b(p+24);effects.push_back({m.s(p),m.s(p+2),m.s(p+4),0,kind==23});removed[i]=true;clear(p);++destroyed;if(kind!=28)m.w(0xa0ae6+kind*2,m.w(0xa0ae6+kind*2)+1);
  if((kind==21||kind==28||kind==29)&&textureChange)for(unsigned j=0;j<m.w(p+38);++j)textureChange(1,m.w(p+36)+j);
  // 0x375d0 and 0x376b8: contents appear only after destruction delay.
  if(kind==23){++exploded;if(sound)sound(33);if(blast)blast(p,m.d(p+20));}
  else{if(sound)sound(kind==28?108:41);if(m.b(p+28)!=255){if(m.b(p+25)&2){if(enemy)enemy(p,m.b(p+28));}else if(pickup)pickup(p,m.b(p+28));}if(m.b(p+29)!=255&&pickup)pickup(p,m.b(p+29));}
 }
}

std::vector<unsigned char> ExplorerObjects::networkRemoved()const{std::vector<unsigned char> state;for(bool value:removed)state.push_back(value);return state;}
void ExplorerObjects::networkRemoved(const std::vector<unsigned char>& state){if(state.size()!=removed.size())throw std::runtime_error("Co-op object catalogue differs");for(unsigned i=0;i<state.size();++i){if(state[i]>1)throw std::runtime_error("Invalid co-op object flag");removed[i]=state[i]!=0;}}
