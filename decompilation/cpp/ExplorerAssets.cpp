#include "ExplorerAssets.h"
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
std::vector<unsigned char> explorerRead(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("Missing asset: "+path.string());auto n=f.tellg();if(n<0)throw std::runtime_error("Cannot size asset");std::vector<unsigned char> data(static_cast<size_t>(n));f.seekg(0);if(!f.read(reinterpret_cast<char*>(data.data()),n))throw std::runtime_error("Cannot read asset");return data;}
ExplorerChunks explorerChunks(const std::filesystem::path& path){auto data=explorerRead(path);if(data.size()<12||std::string(data.begin(),data.begin()+4)!="FORM")throw std::runtime_error("Missing FORM header: "+path.string());ExplorerChunks result;for(size_t p=12;p+8<=data.size();){unsigned n=(unsigned(data[p+4])<<24)|(unsigned(data[p+5])<<16)|(unsigned(data[p+6])<<8)|data[p+7];if(p+8ull+n>data.size())throw std::runtime_error("Truncated FORM chunk");result[std::string(data.begin()+p,data.begin()+p+4)]={data.begin()+p+8,data.begin()+p+8+n};p+=8+n+(n&1);}return result;}
void ExplorerTextures::load(const std::filesystem::path& path,int transparentIndex){
    pages.clear();rects.clear();auto chunks=explorerChunks(path);auto word=[](const auto& b,size_t p){if(p+2>b.size())throw std::runtime_error("Truncated texture descriptor");return unsigned(b[p])|(unsigned(b[p+1])<<8);};
    for(auto& [tag,data]:chunks)if(tag.starts_with("TP")){
        unsigned page=unsigned(std::stoi(tag.substr(2)));auto palette=chunks.find("CL"+tag.substr(2));if(palette==chunks.end()||palette->second.size()<36)throw std::runtime_error("Texture palette missing");if(data.size()!=65536&&data.size()!=32768)throw std::runtime_error("Unexpected texture page extent");
        unsigned colors=data.size()==32768?16:256, banks=word(palette->second,0);
        if(!banks||4ull+banks*colors*2ull>palette->second.size())throw std::runtime_error("Truncated texture palettes");
        for(unsigned bank=0;bank<banks;++bank){auto& out=pages[page*256+bank];out.resize(65536);
            for(unsigned i=0;i<65536;++i){unsigned index=data.size()==65536?data[i]:(data[i/2]>>((i&1)*4))&15;auto offset=4+(bank*colors+index)*2;auto c=word(palette->second,offset);out[i]=(int(index)==transparentIndex?0u:((c==0||(c&0x7fff)==0x7c1f)?0u:0xff000000u))|(((c&31)*255/31)<<16)|((((c>>5)&31)*255/31)<<8)|(((c>>10)&31)*255/31);if(int(index)==transparentIndex)out[i]=0;}
        }
    }
    for(auto& [tag,data]:chunks)if(tag.starts_with("BX")){unsigned count=word(data,0),page=unsigned(std::stoi(tag.substr(2)));if(2ull+count*6>data.size())throw std::runtime_error("Invalid BX texture rectangles");for(unsigned i=0;i<count;++i){auto p=2+i*6;unsigned bank=chunks.at("TP"+tag.substr(2)).size()==32768?word(data,p+4):0,key=page*256+bank;if(!pages.contains(key))throw std::runtime_error("Texture rectangle palette missing");rects.push_back({key,data[p],data[p+1],unsigned(data[p+2])+1,unsigned(data[p+3])+1});}}

}
unsigned ExplorerTextures::pixel(unsigned page,int x,int y)const{auto it=pages.find(page);if(it==pages.end())return 0xffff00ff;return it->second[(std::clamp(y,0,255))*256+std::clamp(x,0,255)];}
void ExplorerDoors::load(trilogy::original::PlayerControlMemory m,int travel){
    entries.clear();opened=closed=0;auto records=m.d(0xa0c5c);unsigned width=m.w(0xa0c30),height=m.w(0xa0c32);
    for(unsigned i=0;i<m.w(0xa0c20);++i){auto p=records+i*8;unsigned x=m.b(p),z=m.b(p+1);if(x>=width||z>=height)throw std::runtime_error("Door outside map grid");
        auto cell=m.d(0xa0c40)+(z*width+x)*16;int floor=m.b(p+5)*32;bool alongZ=m.b(p+6)&2;
        ExplorerDoor d{p,cell,m.b(p+6),m.b(p+4),unsigned(m.b(p+3))*4,m.b(cell+10),int(x*512+256+(alongZ?0:256))-m.w(0xa0c34),int(z*512+256+(alongZ?256:0))-m.w(0xa0c36),floor,floor+travel};
        d.target=(m.b(p+2)&64)!=0;d.amount=d.previous=d.target?1.f:0.f;d.remaining=std::max(90u,d.hold);
        for(int offset=-1;offset<=2;++offset){int cx=int(x)+(alongZ?0:offset),cz=int(z)+(alongZ?offset:0);if(cx<0||cz<0||cx>=int(width)||cz>=int(height))continue;auto c=m.d(0xa0c40)+(cz*width+cx)*16;d.cells.push_back(c);m.b(c+10,d.target?0:20);m.b(c+11,floor/32);m.b(c+13,(d.target?d.ceiling:d.floor)/32);m.b(c+7,(m.b(c+7)&127)|(d.target?0:128));}
        entries.push_back(std::move(d));
    }
}
void ExplorerDoors::tick(trilogy::original::PlayerControlMemory m,int x,int z,const std::function<bool(const ExplorerDoor&)>& extra){
    for(auto& d:entries){d.previous=d.amount;bool within=(d.rotation&2)?std::abs(x-d.x)<400&&std::abs(z-d.z)<1150:std::abs(z-d.z)<400&&std::abs(x-d.x)<1150;if(extra&&extra(d))within=true;
        if(d.target){d.amount=std::min(1.f,d.amount+.075f);if(d.amount>=1&&d.hold){if(d.remaining)--d.remaining;else if(!within){d.target=false;++closed;}}}
        else if(!(within&&d.amount>0))d.amount=std::max(0.f,d.amount-.075f);
        bool pass=d.amount>.85f;
        for(auto cell:d.cells){m.b(cell+10,pass?0:20);m.b(cell+7,(m.b(cell+7)&127)|(pass?0:128));m.b(cell+11,d.floor/32);m.b(cell+13,pass?d.ceiling/32:d.floor/32);}
    }
}

std::vector<unsigned char> explorerDecodeFrame(const std::vector<unsigned char>& in,unsigned start,unsigned expected){
    // Some BND sections end with an uncompressed final frame (BURSTER F005).
    if(start<=in.size()&&in.size()-start==expected)return {in.begin()+start,in.end()};
    std::vector<unsigned char> out;out.reserve(expected);unsigned p=start,mask=0;
    auto byte=[&](){if(p>=in.size())throw std::runtime_error("Truncated enemy sprite");return unsigned(in[p++]);};
    for(;;){mask>>=1;if(!(mask&0xff00))mask=0xff00|byte();if(!(mask&1)){if(out.size()>=expected)throw std::runtime_error("Enemy frame size mismatch");out.push_back(static_cast<unsigned char>(byte()));continue;}
        auto code=byte();int offset;unsigned count;
        if(code>=96){offset=int(code)-256;count=2;}else{auto distance=((code&15)<<8)|byte();if(!distance)break;offset=-int(distance);count=code>>4;count=count==5?byte()+8:count+3;}
        if(out.size()+count>expected||int(out.size())+offset<0)throw std::runtime_error("Invalid enemy sprite match");
        for(unsigned i=0;i<count;++i)out.push_back(out[out.size()+offset]);
    }
    if(out.size()!=expected)throw std::runtime_error("Enemy frame dimensions do not match data");return out;
}

bool explorerPs1Image(const std::filesystem::path& root,const std::string& name,unsigned& width,unsigned& height,std::vector<unsigned>& pixels){auto path=root/"GameData/PS1/Decoded"/(name+".rgba");if(!std::filesystem::exists(path))return false;auto data=explorerRead(path);auto d=[&](unsigned p){return unsigned(data[p])|(unsigned(data[p+1])<<8)|(unsigned(data[p+2])<<16)|(unsigned(data[p+3])<<24);};if(data.size()<12||std::string(data.begin(),data.begin()+4)!="RGBA")throw std::runtime_error("Invalid decoded PS1 still");auto w=d(4),h=d(8);if(!w||!h||w>2048||h>2048||data.size()!=12ull+w*h*4ull)throw std::runtime_error("Invalid PS1 still extent");width=w;height=h;pixels.resize(w*h);for(unsigned i=0;i<pixels.size();++i)pixels[i]=d(12+i*4);return true;}
int explorerPs1Track(const std::filesystem::path& root,const std::string& code){std::ifstream f(root/"GameData/PS1/level_music.txt");std::string level;int track;while(f>>level>>track)if(level==code&&track>=2&&track<=99)return track;return -1;}
std::string explorerPs1SfxBank(const std::filesystem::path& root,const std::string& code){std::ifstream f(root/"GameData/PS1/level_sfx.txt");std::string level,bank;while(f>>level>>bank)if(level==code&&std::filesystem::exists(root/"GameData/PS1/CD"/bank))return bank;return {};}
