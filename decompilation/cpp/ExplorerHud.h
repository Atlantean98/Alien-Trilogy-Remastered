#pragma once
#include "ExplorerEnemies.h"
#include "ExplorerAssets.h"
#include <cmath>
class ExplorerHud {
public:
    struct NetworkState {unsigned selected{},mode{},group{},program{},ammo{},reloadTicks{},trackerFrame{},trackerTicks{},trackerPause{},trackerHold{};bool flameActive{};};
    NetworkState networkState()const;void networkState(const NetworkState&);
    void load(trilogy::original::PlayerControlMemory,const std::filesystem::path&,bool alien=false);
    void tick(bool rapidFire=false);void fire(bool consume=true);void reload();void stopFiring();
    bool canSecondary()const{return !reloadTicks&&mode==0;}
    void secondaryFire(){if(selectedWeapon==2)animation(3);}
    bool canFire()const{return ammo&&!reloadTicks&&(mode==0||selectedWeapon==4);}
    std::vector<unsigned> takeSounds(){std::vector<unsigned> result;result.swap(soundCues);return result;}
    bool select(unsigned weapon);bool cycle(int direction=1);bool owns(unsigned weapon)const;
    void syncAmmo();unsigned selected()const{return selectedWeapon;}
    unsigned reserve()const;unsigned fireSound()const;unsigned reloadSound()const;
    const wchar_t* weaponName()const;
    const ExplorerSprite& tracker()const{return trackerFrames[trackerFrame];}
    int trackerSound(unsigned movingContacts);
    static bool onTracker(const std::array<double,2>& point){return std::abs(point[0])<=39&&std::abs(point[1])<=31;}
    bool takeTrackerPing(){bool value=trackerPing;trackerPing=false;return value;}
    static std::vector<std::array<double,2>> contacts(trilogy::original::PlayerControlMemory,std::span<const unsigned>,const trilogy::original::Vec3s&,int yaw);
    unsigned trackerFrame{},trackerTicks{},trackerPause{},trackerHold{};bool trackerPing=false;
    const ExplorerSprite& weapon();
    const ExplorerSprite& status();
    unsigned statusPixels{},statusAmmo{},statusHealth{},statusArmor{};unsigned ammo=99,reloadTicks=0;
private:
    trilogy::original::PlayerControlMemory m{};ExplorerChunks chunks;std::vector<unsigned char> palette;
    std::vector<unsigned char> statusIndices,statusPalette;ExplorerTextures statusArt;ExplorerSprite statusImage;
    std::array<ExplorerSprite,8> trackerFrames;
    std::filesystem::path assetRoot;unsigned selectedWeapon{},table=0x9ce14,mode{},group{},program{};
    std::vector<unsigned> soundCues;bool flameActive=false;void captureSound();
    unsigned ammoAddress()const;unsigned reserveAddress()const;std::map<unsigned,ExplorerSprite> cache;void animation(unsigned);
};
