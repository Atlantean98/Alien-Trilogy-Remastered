#include "ExplorerHud.h"
#include "WeaponRuntime.h"
#include "PickupRuntime.h"
#include "ExplorerAudio.h"
#include <algorithm>
#include <stdexcept>
#include <cmath>
using namespace trilogy::original;
namespace {constexpr unsigned Object=0x31d000;}
void ExplorerHud::load(PlayerControlMemory memory,const std::filesystem::path& root,bool alien){m=memory;soundCues.clear();flameActive=false;assetRoot=root;auto statusFile=root/"GameData/PS1/CD/LANGUAGE"/(alien?"PNL1GFXU.BND":"PNL0GFXU.BND");if(!std::filesystem::exists(statusFile))statusFile=root/"GameData/PS1/CD/LANGUAGE"/(alien?"PNL1GFXU.BND":"PNL0GFXU.BND");statusArt.load(statusFile,0);statusIndices=explorerChunks(statusFile).at("TP00");statusPalette=explorerChunks(statusFile).at("CL00");for(auto& [page,pixels]:statusArt.pages)for(auto& pixel:pixels)if((pixel&0xffffff)==0)pixel=0;statusImage.width=320;statusImage.height=240;statusImage.pixels.resize(320*240);selectedWeapon=0;table=0x9ce14;
    trackerFrame=trackerTicks=trackerPause=trackerHold=0;trackerPing=false;ExplorerTextures art;art.load(root/"GameData/PS1/CD/GFX"/(alien?"PANEL3GF.BND":"PANELGFX.BND"));
    for(unsigned frame=0;frame<8;++frame){auto& image=trackerFrames[frame];image.width=82;image.height=74;image.left=223;image.top=155;image.pixels.assign(82*74,0);
        auto blit=[&](unsigned descriptor,int left,int top,int width,int height,bool flipX,bool flipY){auto r=art.rect(descriptor);if(!r)throw std::runtime_error("Missing original motion tracker descriptor");for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto color=art.pixel(r->page,r->x+(flipX?width-1-x:x),r->y+(flipY?height-1-y:y));if(color>>24)image.pixels[(top+y)*82+left+x]=0x80000000u|(((color>>16&255)/2)<<16)|(((color>>8&255)/2)<<8)|((color&255)/2);}};
        blit(203,0,0,82,5,false,true);blit(203,0,69,82,5,false,false);
        blit(205+frame,1,5,40,32,false,false);blit(205+frame,41,5,40,32,true,false);blit(205+frame,1,37,40,32,false,true);blit(205+frame,41,37,40,32,true,true);
    }
chunks=explorerChunks(root/"GameData/PS1/CD/GFX/MM9.BND");auto clut=chunks.at("C000");palette.assign(768,0);for(unsigned i=0;i<256;++i){unsigned c=clut.at(i*2)|(unsigned(clut.at(i*2+1))<<8);palette[i*3]=(c&31)*63/31;palette[i*3+1]=((c>>5)&31)*63/31;palette[i*3+2]=((c>>10)&31)*63/31;}m.b(0xa0ab4,0);for(unsigned p=0xa0abe;p<=0xa0ad4;p+=2)m.w(p,0);resetWeaponSlots(m);m.w(0xa0aae,0);m.w(0xa0aac,0);awardPickup(m,0,15,2);ammo=m.w(0xa0abe);reloadTicks=0;cache.clear();animation(0);weapon();}
void ExplorerHud::animation(unsigned index){mode=index;group=m.d(table+index*8);program=m.d(table+index*8+4);m.d(Object+0x14,group);m.d(Object+0x18,program+2);m.d(Object+0x1c,program+4);m.w(Object+0x30,1);m.w(Object+0x32,m.w(program));m.b(Object+0x36,0);m.w(Object+0x3a,0);advanceAnimation(m,Object);captureSound();}
void ExplorerHud::tick(bool rapidFire){
    ++trackerTicks;if(trackerTicks&1){if(trackerPause)--trackerPause;else if(trackerHold)--trackerHold;else{trackerFrame=(trackerFrame+1)&7;trackerHold=1;if(!trackerFrame){trackerPause=32;trackerPing=true;}}}
if(reloadTicks&&!--reloadTicks){auto address=reserveAddress();if(address&&m.w(address)){m.w(address,m.w(address)-1);m.w(ammoAddress(),selectedWeapon==0?15:100);}else if(selectedWeapon==0)m.w(ammoAddress(),1);}syncAmmo();for(unsigned i=0;i<(rapidFire&&mode==1?2u:1u);++i){bool due=m.w(Object+0x30)==1;advanceAnimation(m,Object);if(due)captureSound();if(m.b(Object+0x36)&6)break;}if(mode&&(m.b(Object+0x36)&6))animation(0);if(!ammo&&!mode&&!reloadTicks)reload();}
void ExplorerHud::fire(bool consume){if(consume&&!canFire())return;if(consume){--ammo;m.w(ammoAddress(),ammo);}else syncAmmo();if(selectedWeapon==4)flameActive=true;if(mode!=1||selectedWeapon!=4)animation(1);}
void ExplorerHud::reload(){if(!reloadTicks&&reserveAddress()&&ammo<(selectedWeapon==0?15u:100u)&&(reserve()||(selectedWeapon==0&&!ammo))){reloadTicks=30;animation(2);if(selectedWeapon!=3)soundCues.push_back(reloadSound());}}
const ExplorerSprite& ExplorerHud::weapon(){auto frame=m.d(Object+0x10);static const ExplorerSprite empty{};if(!frame)return empty;if(cache.contains(frame))return cache.at(frame);unsigned width=m.w(frame+4),height=m.w(frame+6);if(!width||!height||width*height>65536)throw std::runtime_error("Invalid weapon frame size");const std::vector<unsigned char>* selected=nullptr;unsigned count=program>group&&(program-group)%12==0?std::min(32u,(program-group)/12):1;
    for(auto& [tag,data]:chunks)if(tag[0]=='F'){bool valid=true;for(unsigned i=0;i<count;++i){auto r=group+i*12;try{explorerDecodeFrame(data,m.d(r+8),m.w(r+4)*unsigned(m.w(r+6)));}catch(const std::exception&){valid=false;break;}}if(valid){selected=&data;break;}}
    if(!selected)throw std::runtime_error("Weapon frame group does not match selected weapon bundle");auto raw=explorerDecodeFrame(*selected,m.d(frame+8),width*height);ExplorerSprite sprite;sprite.width=width;sprite.height=height;sprite.left=m.s(frame);sprite.top=m.s(frame+2);sprite.pixels.resize(raw.size());for(unsigned i=0;i<raw.size();++i){unsigned index=raw[i];if(!index)continue;unsigned p=index*3;if(p+2>=palette.size())p=0;auto channel=[&](unsigned c){return std::min(255u,unsigned(palette[p+c])*4);};sprite.pixels[i]=0xff000000u|(channel(0)<<16)|(channel(1)<<8)|channel(2);}return cache.emplace(frame,std::move(sprite)).first->second;
}

std::vector<std::array<double,2>> ExplorerHud::contacts(PlayerControlMemory m,std::span<const unsigned> entities,const Vec3s& player,int yaw){
    std::vector<std::array<double,2>> result;double angle=yaw*6.283185307179586/4096,cs=std::cos(angle),sn=std::sin(angle);
    for(auto e:entities){auto kind=m.b(e+0x74),state=m.b(e+0x6f);if(kind>=15||!kind||state>=7||(kind==10&&state==4))continue;if(kind!=10&&!m.w(e+0x30)&&!m.w(e+0x32)&&!m.w(e+0x34)&&(state<3||state>6))continue;
        int dx=int(player[0])-m.s(e),dz=int(player[2])-m.s(e+4);if(std::abs(dx)>12288||std::abs(dz)>12288)continue;
        result.push_back({(dx*cs+dz*sn)/256.0,(-dx*sn+dz*cs)/256.0});
    }return result;
}

int ExplorerHud::trackerSound(unsigned movingContacts){
    bool sweep=takeTrackerPing();
    // Request only the currently appropriate tone. The audio channel owns
    // playback completion, so the long original clip cannot overlap itself.
    return movingContacts?22:sweep?21:-1;
}

unsigned ExplorerHud::ammoAddress()const{constexpr unsigned addresses[]={0xa0abe,0xa0ac2,0xa0ac4,0xa0acc,0xa0aca};return addresses[selectedWeapon];}
unsigned ExplorerHud::reserveAddress()const{constexpr unsigned addresses[]={0xa0ac0,0,0xa0ac6,0xa0ace,0};return addresses[selectedWeapon];}
void ExplorerHud::syncAmmo(){ammo=m.w(ammoAddress());}
unsigned ExplorerHud::reserve()const{return reserveAddress()?m.w(reserveAddress()):0;}
bool ExplorerHud::owns(unsigned weapon)const{constexpr unsigned flags[]={4,8,16,64,32};return weapon<5&&(m.b(0xa0ab4)&flags[weapon]);}
const wchar_t* ExplorerHud::weaponName()const{constexpr const wchar_t* names[]={L"Pistol",L"Shotgun",L"Pulse rifle",L"Smartgun",L"Flamethrower"};return names[selectedWeapon];}
unsigned ExplorerHud::fireSound()const{constexpr unsigned sounds[]={ExplorerAudio::PistolFire,16,6,19,11};return sounds[selectedWeapon];}
unsigned ExplorerHud::reloadSound()const{constexpr unsigned sounds[]={ExplorerAudio::PistolReload,15,5,18,10};return sounds[selectedWeapon];}
bool ExplorerHud::select(unsigned weapon){
    if(!owns(weapon)||weapon==selectedWeapon)return false;constexpr unsigned tables[]={0x9ce14,0x9ce04,0x9cde4,0x9ce44,0x9ce2c};constexpr const char* bundles[]={"MM9.BND","SHOTGUN.BND","PULSE.BND","SMART.BND","FLAME.BND"};
    soundCues.clear();stopFiring();selectedWeapon=weapon;table=tables[weapon];m.w(0xa0aae,weapon);reloadTicks=0;chunks=explorerChunks(assetRoot/"GameData/PS1/CD/GFX"/bundles[weapon]);auto clut=chunks.at("C000");for(unsigned i=0;i<256;++i){unsigned c=clut.at(i*2)|(unsigned(clut.at(i*2+1))<<8);palette[i*3]=(c&31)*63/31;palette[i*3+1]=((c>>5)&31)*63/31;palette[i*3+2]=((c>>10)&31)*63/31;}cache.clear();syncAmmo();animation(0);soundCues.push_back(reloadSound());this->weapon();return true;
}
bool ExplorerHud::cycle(int direction){for(unsigned offset=1;offset<5;++offset){unsigned candidate=(int(selectedWeapon)+direction*int(offset)+25)%5;if(owns(candidate))return select(candidate);}return false;}

void ExplorerHud::captureSound(){if(m.b(Object+0x36)&16)soundCues.push_back(m.w(Object+0x38));}
void ExplorerHud::stopFiring(){if(flameActive){flameActive=false;animation(0);soundCues.push_back(10);}}

const ExplorerSprite& ExplorerHud::status(){
 auto& pixels=statusImage.pixels;std::fill(pixels.begin(),pixels.end(),0);statusHealth=m.w(0xa0ab8);statusArmor=m.w(0xa0aba);statusAmmo=ammo+(selectedWeapon==0?reserve()*15:selectedWeapon==2||selectedWeapon==3?reserve()*100:0);
 auto rectangle=[&](int x,int y,int w,int h,unsigned color){for(int py=std::max(0,y);py<std::min(240,y+h);++py)for(int px=std::max(0,x);px<std::min(320,x+w);++px)pixels[py*320+px]=color;};
 auto sprite=[&](unsigned descriptor,int x,int y){auto r=statusArt.rect(descriptor);if(!r)return;for(unsigned py=0;py<r->height-(statusIndices.size()==32768?1:0);++py)for(unsigned px=0;px<r->width-(statusIndices.size()==32768?1:0);++px){auto color=statusArt.pixel(r->page,r->x+px,r->y+py);if(statusIndices.size()==32768){unsigned n=(r->y+py)*256+r->x+px,index=(statusIndices[n/2]>>((n&1)*4))&15;unsigned offset=4+(48+index)*2;if(offset+1>=statusPalette.size())continue;unsigned c=statusPalette[offset]|(statusPalette[offset+1]<<8);if(!index||(index==3&&(descriptor==175||descriptor==176||descriptor==201||(descriptor>=39&&descriptor<=48))))continue;color=0xff000000u|((c&31)*255/31<<16)|(((c>>5)&31)*255/31<<8)|((c>>10)&31)*255/31;if(descriptor==175||descriptor==176||descriptor==201){unsigned light=std::max({color>>16&255,color>>8&255,color&255});color=0xff000000u|(light/5<<16)|(light<<8)|light/5;}}if(!(color>>24))continue;if(x+int(px)>=0&&x+int(px)<320&&y+int(py)>=0&&y+int(py)<240)pixels[(y+py)*320+x+px]=color;}};
 auto number=[&](unsigned amount,unsigned digitBase,int x,int y){amount=std::min(999u,amount);for(unsigned divisor:{100u,10u,1u}){auto digit=amount/divisor%10;auto r=statusArt.rect(digitBase+digit);if(r){sprite(digitBase+digit,x,y);x+=r->width-1;}}};
 auto slots=[&](unsigned descriptor,int left,int top,unsigned lit,bool flashRed){auto r=statusArt.rect(descriptor);if(!r)return;unsigned slot=0;bool run=false;for(unsigned x=0;x<r->width;++x){int first=-1,last=-1;auto clear=[&](int y){auto c=statusArt.pixel(r->page,r->x+x,r->y+y);unsigned n=(r->y+y)*256+r->x+x;return (statusIndices.size()==65536?statusIndices[n]:(statusIndices[n/2]>>((n&1)*4))&15)==0;};for(unsigned y=1;y+1<r->height;++y){if(!clear(y))continue;if(first<0&&!clear(y-1))first=y;if(first>=0)last=y;}bool enclosed=first>=0&&last>=first&&last-first+1>=3&&!clear(last+1);if(enclosed){if(!run)++slot;unsigned color=slot<=lit?(flashRed?0xffd83024u:0xff20ff20u):0xff000000u;rectangle(left+x,top+first,1,last-first+1,color);}run=enclosed;}};
 // Requested layout: ammo lower-left, health upper-right, PS1 artwork/backing.
 rectangle(14,211,131,19,0xff000000);rectangle(215,19,99,35,0xff000000);
 slots(173,14,211,std::min(22u,ammo),false);sprite(173,14,211);number(statusAmmo,39,18,217);
 slots(174,215,19,std::min(20u,statusHealth*20/100),m.w(0xa0b30)!=0);sprite(174,215,19);number(statusHealth,130,221,22);
 unsigned grenades=m.w(selectedWeapon==2?0xa0ac8:0xa0ad0);if(grenades){sprite(selectedWeapon==2?201:175,221,57);number(grenades,39,281,57);}
 if(statusArmor){int y=grenades?72:57;sprite(176,221,y);number(statusArmor,39,281,y);}
 if(statusHealth>100){unsigned bars=std::min(10u,std::max(1u,(std::min(200u,statusHealth)-100)/10));for(unsigned i=0;i<bars;++i){unsigned h=m.w(0x9cb60+i*2);rectangle(265+i*4,47-h,3,h,0xff5a9a9c);}}
 statusPixels=0;for(auto pixel:pixels)statusPixels+=pixel!=0;return statusImage;
}

ExplorerHud::NetworkState ExplorerHud::networkState()const{return {selectedWeapon,mode,group,program,ammo,reloadTicks,trackerFrame,trackerTicks,trackerPause,trackerHold,flameActive};}
void ExplorerHud::networkState(const NetworkState& state){if(state.selected>4||state.mode>3||state.trackerFrame>7||state.reloadTicks>300)throw std::runtime_error("Invalid co-op HUD state");if(state.selected!=selectedWeapon)select(state.selected);selectedWeapon=state.selected;mode=state.mode;group=state.group;program=state.program;ammo=state.ammo;reloadTicks=state.reloadTicks;trackerFrame=state.trackerFrame;trackerTicks=state.trackerTicks;trackerPause=state.trackerPause;trackerHold=state.trackerHold;flameActive=state.flameActive;soundCues.clear();}
