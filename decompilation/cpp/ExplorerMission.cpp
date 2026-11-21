#include "ExplorerMission.h"
#include <fstream>
#include <algorithm>
#include <sstream>
using namespace trilogy::original;
void ExplorerMission::load(PlayerControlMemory memory,const std::filesystem::path& root,const std::string& code,unsigned level){
 m=memory;index=255;sourceCode=code;remaining=0;title.clear();text.clear();markup.clear();kinds.clear();complete=false;briefing=false;
 // Original campaign order (0x9d800) differs from filename order.
 for(unsigned i=0;i<35;++i){unsigned resource=m.w(0x9d800+i*2),name=m.d(0x94cdc+resource*12);std::string path;for(unsigned j=0;j<100&&m.b(name+j);++j)path+=char(m.b(name+j));if(path.find("LEV"+code+".PAL")!=std::string::npos){index=i;break;}}
 if(index<35)for(unsigned j=0;j<5;++j){auto k=m.b(0x96121+index*5+j);if(k!=255)kinds.push_back(k);}
 for(unsigned i=0;i<8;++i)if(index==m.w(0x95fa6+i*2))remaining=(index==9?30:index==27||index==30?45:60)*30;
 auto missionFile=root/"GameData/PS1/CD/LANGUAGE/MISSIONU.TXT";std::ifstream in(missionFile);std::string line;std::string wanted;for(char c:code){if(!wanted.empty())wanted+='.';wanted+=c;}
 bool found=false,body=false;
 while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(!found){if(line.rfind(wanted+" ",0)==0){found=true;auto t=line.substr(wanted.size()+1);title.assign(t.begin(),t.end());}continue;}
  if(line=="*"){if(body)break;body=true;continue;}if(!body)continue;
   if(line=="#")line.clear();
   {std::string marked=line;for(size_t at=0;(at=marked.find("&0"))!=std::string::npos;)marked.replace(at,2,"\x02");for(size_t at=0;(at=marked.find("&1"))!=std::string::npos;)marked.replace(at,2,"\x01");markup.append(marked.begin(),marked.end());markup+='\n';}
   for(size_t at=0;(at=line.find("&0"))!=std::string::npos;)line.erase(at,2);for(size_t at=0;(at=line.find("&1"))!=std::string::npos;)line.erase(at,2);
   text.append(line.begin(),line.end());text+='\n';
 }
 std::string chapter=code[0]=='2'?"PRISHOLD":code[0]=='3'?"BONESHIP":"COLONY";
 if(!explorerPs1Image(root,chapter,backgroundWidth,backgroundHeight,background))
     throw std::runtime_error("Missing PS1 briefing image. Run the full disc importer.");
 if(title.empty()){title.assign(code.begin(),code.end());text=markup=L"Complete the mission objectives and reach the extraction point.";}
}
unsigned ExplorerMission::percent()const{
 unsigned done=0,total=0;for(auto k:kinds){done+=k==35?m.w(0xa0ae2):m.w(0xa0ae6+k*2);total+=m.w(0xa0bc8+k*2);}return total?std::min(100u,done*100/total):100;
}
std::wstring ExplorerMission::progress()const{
 const wchar_t* names[]={L"",L"Eggs",L"Facehuggers",L"Chestbursters",L"Bambi aliens",L"Aliens",L"Warriors",L"Queen",L"Ceiling aliens",L"Ceiling aliens",L"Colonists",L"Guards",L"Soldiers",L"Synthetics",L"Handlers"};
 std::wostringstream out;for(auto k:kinds){auto total=m.w(0xa0bc8+k*2);if(!total)continue;auto done=k==35?m.w(0xa0ae2):m.w(0xa0ae6+k*2);if(k<15)out<<names[k];else if(k==23)out<<L"Explosive barrels";else if(k==20||k==25||k==28||k==29)out<<L"Crates";else if(k==21)out<<L"Reinforced barricades";else if(k==22||k==24||k==26||k==27)out<<L"Control panels";else if(k==33)out<<L"Doors unlocked";else if(k==34)out<<L"Lifts activated";else if(k==35)out<<L"ID tags";else out<<L"Objective type "<<k;out<<L": "<<done<<L" / "<<total<<L"\n";}
 return out.str();
}

bool ExplorerMission::tick(){if(remaining&&!briefing&&!complete){if(!--remaining){complete=true;return true;}}return false;}
unsigned ExplorerMission::nextIndex()const{
 if(!eligible())return index;unsigned next=index+1;
 // 0x14c98: scores below 85 skip an immediately following bonus stage.
 if(percent()<85)for(unsigned i=0;i<8;++i)if(next==m.w(0x95fa6+i*2)){++next;break;}
 return next;
}
std::string ExplorerMission::nextCode()const{
 unsigned next=nextIndex();if(next==index)return sourceCode;if(next>=35)return {};unsigned resource=m.w(0x9d800+next*2),name=m.d(0x94cdc+resource*12);std::string path;for(unsigned j=0;j<100&&m.b(name+j);++j)path+=char(m.b(name+j));auto at=path.find("LEV");return at==std::string::npos?std::string():path.substr(at+3,3);
}
bool ExplorerMission::eligible()const{return percent()>=minimumPercent();}

void ExplorerMission::visit(unsigned cell){auto base=m.d(0xa0c40);unsigned count=m.w(0xa0c30)*unsigned(m.w(0xa0c32));if(cell<base||(cell-base)%16||(cell-base)/16>=count)return;m.b(cell+7,m.b(cell+7)|1);m.b(0x234780+m.b(cell+5),255);}
ExplorerMission::Statistics ExplorerMission::statistics()const{
 Statistics result;
 // 0x149bc: original Aliens / Secrets / Mission assessment counters.
 for(unsigned kind=1;kind<15;++kind){result.found[0]+=m.w(0xa0ae6+kind*2);result.total[0]+=m.w(0xa0bc8+kind*2);}
 result.total[1]=m.w(0xa0c3e);for(unsigned id=1;id<256;++id)result.found[1]+=m.b(0x234780+id)!=0;
 if(!result.total[1])result.found[1]=result.total[1]=1;
 for(auto kind:kinds){result.found[2]+=kind==35?m.w(0xa0ae2):m.w(0xa0ae6+kind*2);result.total[2]+=m.w(0xa0bc8+kind*2);}
 if(!result.total[2])result.found[2]=result.total[2]=1;
 for(unsigned row=0;row<3;++row)result.percent[row]=result.total[row]?std::min(100u,result.found[row]*100/result.total[row]):0;
 return result;
}
