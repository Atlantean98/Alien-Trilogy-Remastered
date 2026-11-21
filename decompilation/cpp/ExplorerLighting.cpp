#include "ExplorerLighting.h"
#include <algorithm>
#include <stdexcept>
namespace {
unsigned word(const std::vector<unsigned char>& b,size_t p){if(p+2>b.size())throw std::runtime_error("Truncated lighting MAP");return unsigned(b[p])|(unsigned(b[p+1])<<8);}
unsigned dword(const std::vector<unsigned char>& b,size_t p){return word(b,p)|(word(b,p+2)<<16);}
std::array<unsigned,4> faceKey(const std::vector<unsigned char>& b,size_t p){std::array<unsigned,4> key{};for(unsigned i=0;i<4;++i)key[i]=dword(b,p+i*4);std::sort(key.begin(),key.end());return key;}
}
void ExplorerLighting::load(const std::filesystem::path& path,bool enabled){
    active=enabled;remaining=enabled?1800:0;elapsed=phase=changes=0;firstCue=enabled;matched={};for(auto& bank:banks)bank={};if(!enabled)return;
    auto original=explorerChunks(path.parent_path()/"L113LEV.MAP").at("MAP0");unsigned vc=word(original,0),fc=word(original,2);size_t faceBase=36+vc*8;
    for(unsigned variant=0;variant<3;++variant){auto filename="L11"+std::to_string(variant+3)+"LEV.MAP";auto data=explorerChunks(path.parent_path()/filename).at("MAP0");
        if(word(data,0)!=vc||word(data,2)!=fc||data.size()<faceBase+fc*20ull||!std::equal(original.begin()+36,original.begin()+faceBase,data.begin()+36))throw std::runtime_error("Lighting variants do not share the original vertex layout");
        size_t lights=faceBase+fc*20ull+word(data,4)*word(data,6)*16ull+data[12]*8ull+word(data,14)*20ull+word(data,16)*8ull+word(data,18)*16ull+word(data,20)*8ull+word(data,22)*16ull+data[28]*4ull+data[29]*4ull;
        if(lights+data[13]*28ull>data.size())throw std::runtime_error("Lighting bank outside MAP");auto& bank=banks[variant];bank.lights.resize(data[13]);for(unsigned i=0;i<bank.lights.size();++i)std::copy_n(data.begin()+lights+i*28,28,bank.lights[i].begin());
        std::map<std::array<unsigned,4>,unsigned> byGeometry;for(unsigned i=0;i+1<fc;++i)byGeometry.try_emplace(faceKey(data,faceBase+i*20),data[faceBase+i*20+19]&127);
        for(unsigned i=0;i<fc;++i){auto key=faceKey(original,faceBase+i*20);auto found=byGeometry.find(key);unsigned light=original[faceBase+i*20+19]&127;if(found!=byGeometry.end()){light=found->second;++matched[variant];}bank.faces.push_back(std::min(light,unsigned(bank.lights.size()-1)));}
    }
}
int ExplorerLighting::tick(){
    if(!active||!remaining)return -1;int sound=-1;if(firstCue){sound=99;firstCue=false;}
    --remaining;++elapsed;
    if(remaining&&elapsed%90==0){phase=(phase+1)%3;++changes;sound=101;}
    return sound;
}
std::array<unsigned char,3> ExplorerLighting::color(unsigned sourceFace,unsigned fallbackLight)const{
    const auto& bank=banks[phase];if(bank.lights.empty())return {64,64,64};unsigned index=sourceFace<bank.faces.size()?bank.faces[sourceFace]:fallbackLight;if(index>=bank.lights.size())index=0;const auto& light=bank.lights[index];std::array<unsigned char,3> rgb{};
    for(unsigned c=0;c<3;++c){if(light[24]==4)rgb[c]=(unsigned(light[c])+light[4+c]+light[8+c]+light[12+c])/4;else rgb[c]=light[(light[25]?4:0)+c];}return rgb;
}

void ExplorerPs1Lighting::load(const std::filesystem::path& pc,const std::filesystem::path& root,int bank){
 active=false;matched=total=0;records.clear();faces.clear();auto path=root/"GameData/PS1/CD"/pc.parent_path().filename()/pc.filename();if(bank>=0)path=path.parent_path()/("L11"+std::to_string(3+bank)+"LEV.MAP");/* A map outside the section folders (an Extermination deck: campaign maps remixed, or two stitched into one, from the PS1 maps) carries the PS1 lighting itself. */if(!std::filesystem::exists(path)&&bank<0)path=pc;if(!std::filesystem::exists(path))return;auto original=explorerChunks(pc).at("MAP0"),data=explorerChunks(path).at("MAP0");unsigned vc=word(data,0),fc=word(data,2);if(vc!=word(original,0)||36+vc*8ull>data.size()||36+vc*8ull>original.size())return;/* Same map plan: every vertex's x and z (an Extermination deck may lower ceilings, moving heights only). */for(unsigned v=0;v<vc;++v){const size_t at=36+v*8ull;if(!std::equal(data.begin()+at,data.begin()+at+2,original.begin()+at)||!std::equal(data.begin()+at+4,data.begin()+at+8,original.begin()+at+4))return;}size_t base=36+vc*8;
 size_t lights=base+fc*20ull+word(data,4)*word(data,6)*16ull+data[12]*8ull+word(data,14)*20ull+word(data,16)*8ull+word(data,18)*16ull+word(data,20)*8ull+word(data,22)*16ull+data[28]*4ull+data[29]*4ull;if(lights+data[13]*28ull>data.size())throw std::runtime_error("PS1 light records exceed MAP0");records.resize(data[13]);for(unsigned i=0;i<records.size();++i)std::copy_n(data.begin()+lights+i*28,28,records[i].begin());
 std::map<std::array<unsigned,4>,unsigned> lookup;for(unsigned i=0;i<fc;++i)lookup.try_emplace(faceKey(data,base+i*20),data[base+i*20+19]&127);total=word(original,2);for(unsigned i=0;i<total;++i){auto key=faceKey(original,base+i*20);unsigned light=255;if(i<fc&&faceKey(data,base+i*20)==key)light=data[base+i*20+19]&127;else if(lookup.contains(key))light=lookup.at(key);faces.push_back(light);matched+=light<records.size();}initial=records;active=matched+1>=total;
}
void ExplorerPs1Lighting::toggle(unsigned delta,unsigned index){if(index>=records.size())return;auto& r=records[index];r[26]=std::min(unsigned(r[27]),unsigned(static_cast<unsigned char>(r[26]+delta)));}
void ExplorerPs1Lighting::tick(unsigned random){
 // State transitions transcribed from original 0x29685; host supplies jitter.
 unsigned shortJitter=(random&3)+1,longJitter=((random>>8)&7)+1;
 for(unsigned i=0;i<records.size();++i){auto& r=records[i];unsigned mode=r[24];if(mode!=1&&mode!=2&&mode!=3&&mode!=5)continue;
  if(r[26]!=r[27]){for(unsigned at=16;at<20;++at)r[at]=initial[i][at];r[25]=initial[i][25];continue;}
  unsigned timer=r[16]|(r[17]<<8),repeats=r[18]|(r[19]<<8);
  if(mode==3){if(timer)--timer;if(!timer){r[25]=r[25]==1?0:1;unsigned at=r[25]?20:22;timer=r[at]|(r[at+1]<<8);}}
  else if(timer)--timer;
  else if(mode==1){if(!r[25]){r[25]=1;timer=shortJitter+r[20]+(r[21]<<8);}else if(repeats){r[25]=0;--repeats;timer=shortJitter+r[22]+(r[23]<<8);}}
  else {r[25]=r[25]?0:1;timer=r[25]?shortJitter:longJitter;if(mode==5)timer=(timer-1)>>1;}
  r[16]=timer;r[17]=timer>>8;r[18]=repeats;r[19]=repeats>>8;
 }
}
std::array<unsigned char,3> ExplorerPs1Lighting::color(unsigned face,unsigned corner)const{if(face>=faces.size()||faces[face]>=records.size())return {128,128,128};auto& r=records[faces[face]];constexpr unsigned offsets[]={4,0,8,12};unsigned at=r[24]==4?offsets[corner&3]:r[25]?4:0;return {r[at],r[at+1],r[at+2]};}
