#include "ExplorerPs1Memory.h"
#include "ExplorerAssets.h"
#include <fstream>
#include <cmath>
#include <regex>
#include <algorithm>
#include <stdexcept>
void initializePs1Memory(trilogy::original::PlayerControlMemory m,const std::filesystem::path& root){
 constexpr unsigned Base=0x400000,Virtual=0x80010000;
 auto exe=explorerRead(root/"GameData/PS1/CD/SLUS_000.07");
 if(m.bytes.size()<Base||exe.size()<2048||exe.size()-2048>m.bytes.size()-Base||std::string(exe.begin(),exe.begin()+8)!="PS-X EXE")throw std::runtime_error("Expected the supported PS1 executable from your disc");
 auto word=[&](unsigned n){return unsigned(exe.at(n))|(unsigned(exe.at(n+1))<<8);};
 auto dword=[&](unsigned n){return word(n)|(word(n+2)<<16);};
 if(dword(0x18)!=Virtual)throw std::runtime_error("Unsupported PS1 executable load address");
 std::fill(m.bytes.begin(),m.bytes.end(),0);std::copy(exe.begin()+2048,exe.end(),m.bytes.begin()+Base);
 auto host=[&](unsigned a){if(a<Virtual||a>=Virtual+exe.size()-2048)throw std::runtime_error("PS1 data reference outside executable");return Base+a-Virtual;};
 for(unsigned p=Base;p+4<=Base+exe.size()-2048;p+=4){auto value=m.d(p);if(value>=Virtual&&value<Virtual+exe.size()-2048)m.d(p,host(value));}
 auto copy=[&](unsigned target,unsigned native,unsigned size){auto source=host(native);if(size>Base+exe.size()-2048-source||target+size>Base)throw std::runtime_error("PS1 table exceeds executable bounds");std::copy_n(m.bytes.begin()+source,size,m.bytes.begin()+target);};
 // Address/length bindings are derived from SLUS-00007; data comes only from the user's disc.
 copy(0x94b7a,0x8008cd84,24);copy(0x96121,0x8006c084,35*5);
 copy(0x97074,0x8006d4ac,0x5c8);copy(0x9763c,0x8006da74,8192);
 copy(0x9bf24,0x80072318,0x2b0);copy(0x9c934,0x80072be0,96);
 copy(0x9cb60,0x80072df8,20);copy(0x9cb74,0x80072e0c,28*16);
 copy(0x9cda4,0x8007303c,28*2);copy(0x9cde4,0x80073074,0x78);
 struct Binding{unsigned target,native,count;};
 for(auto b:{Binding{0x9c1d4,0x800725c8,6},{0x9c204,0x800725f8,13},{0x9c294,0x80072660,13},{0x9c324,0x800726c8,11},{0x9c37c,0x80072720,11},{0x9c3fc,0x80072778,12},{0x9c474,0x800727d8,14},{0x9c50c,0x80072848,13},{0x9c59c,0x800728b0,11},{0x9c60c,0x80072908,11},{0x9c67c,0x80072960,13},{0x9c71c,0x800729c8,14},{0x9c78c,0x80072a38,18},{0x9c81c,0x80072ac8,35}})copy(b.target,b.native,b.count*8);
 copy(0x9c284,0x80072650,16);
 for(unsigned i=0;i<256;++i)m.w(0x9d5e2+i*2,unsigned(std::sqrt(double(i))*1024));
 m.w(0x9ce5c,0xffff);m.d(0xa09b4,1);m.d(0xa09c0,0x12345678);m.d(0xa09c4,0x31415926);
 // The original campaign ordering is read from the user's mission file.
 std::ifstream missions(root/"GameData/PS1/CD/LANGUAGE/MISSIONU.TXT");std::string line;std::regex heading("^([123])\\.([0-9])\\.([0-9]) ");std::smatch match;unsigned index=0;
 while(std::getline(missions,line))if(std::regex_search(line,match,heading)){
  if(index>=35)throw std::runtime_error("Unexpected PS1 campaign length");
  std::string code=match[1].str()+match[2].str()+match[3].str(),name="LEV"+code+".PAL";unsigned address=0x2b0000+index*32;
  for(unsigned i=0;i<name.size();++i)m.b(address+i,name[i]);m.d(0x94cdc+index*12,address);m.w(0x9d800+index*2,index);++index;
 }
 if(index!=35)throw std::runtime_error("PS1 mission ordering could not be read");
 // Bonus-stage ordinals are control-flow facts, not redistributed assets.
 unsigned bonus[]={2,5,7,9,16,19,27,30};for(unsigned i=0;i<8;++i)m.w(0x95fa6+i*2,bonus[i]);
}
