#pragma once
#include "ProjectileRuntime.h"
#include <vector>
#include <algorithm>
#include <stdexcept>
// Host adapter of 0x4261c, 0x42770, 0x4286c and 0x42894.
class ExplorerTextureAnimation {
 struct State {unsigned begin=0,cursor=0,end=0,delay=1,period=1,progress=0,required=0;bool stopped=false;};
 std::vector<State> states;
 void enter(trilogy::original::PlayerControlMemory m,State& s,unsigned& texture){
  for(unsigned budget=0;budget<1024;++budget){
   if(s.cursor>=s.end){s.stopped=true;return;}unsigned value=m.w(s.cursor+2);
   if(!(value&0x8000)){texture=value;return;}
   switch(value&0x7fff){
    case 1:s.cursor=s.begin;continue;
    case 2:s.period=1;break;case 3:s.period=2;break;case 4:s.period=3;break;case 5:s.period=4;break;case 6:s.period=16;break;
    case 7:case 8:case 9:case 10:s.period=(trilogy::original::nextGameRandom(m)&((1u<<((value&0x7fff)-5))-1))+1;break;
    case 11:s.stopped=true;return;
    default:throw std::runtime_error("Unknown original texture animation command");
   }s.delay=s.period;s.cursor+=4;
  }throw std::runtime_error("Cyclic original texture animation commands");
 }
public:
 void load(trilogy::original::PlayerControlMemory m,std::vector<unsigned>& values){
  states.clear();values.clear();unsigned scripts=m.d(0xa0bc8+0xac),end=scripts+m.w(0xa0bc8+0x72)*4;
  std::vector<unsigned> groups;int previous=-1;for(unsigned p=scripts;p<end;p+=4)if(int(m.w(p))!=previous){groups.push_back(p);previous=m.w(p);}
  for(unsigned i=0;i<m.w(0xa0bc8+0x70);++i){unsigned p=m.d(0xa0bc8+0xa8)+i*4;State s;s.progress=m.b(p+2);s.required=m.b(p+3);unsigned group=m.w(p);values.push_back(0);
   if(group==65535){s.stopped=true;}else{if(group>=groups.size())throw std::runtime_error("Invalid original texture animation group");s.begin=s.cursor=groups[group];s.end=group+1<groups.size()?groups[group+1]:end;enter(m,s,values.back());}states.push_back(s);
  }
 }
 void activate(unsigned delta,unsigned index){if(index<states.size()){auto& s=states[index];s.progress=std::min(s.required,unsigned(static_cast<unsigned char>(s.progress+delta)));}}
 void tick(trilogy::original::PlayerControlMemory m,std::vector<unsigned>& values){for(unsigned i=0;i<states.size();++i){auto& s=states[i];if(s.stopped||s.progress<s.required)continue;if(--s.delay)continue;s.delay=s.period;s.cursor+=4;enter(m,s,values[i]);}}
};
