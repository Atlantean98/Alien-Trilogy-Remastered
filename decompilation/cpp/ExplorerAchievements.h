#pragma once
#include "ExplorerAchievementCatalog.h"
#include "ExplorerSurvivalState.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>

class ExplorerAchievements {
public:
    void load(const std::filesystem::path& file){
        path=file;
        unlocked={};
        toast.clear();
        std::ifstream input(path);
        unsigned id;
        while(input>>id)if(id>0&&id<unlocked.size())unlocked[id]=true;
    }
    bool campaignCompleted()const{return has(22);}
    bool has(unsigned id)const{return unlocked.at(id);}
    void unlock(unsigned id){
        if(path.empty()||has(id))return;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(),error);
        std::ofstream output(path,std::ios::app);
        output<<id<<'\n';
        output.close();
        if(!output)return;
        unlocked[id]=true;
        toast=ExplorerAchievementCatalog[id].title;
        toastTicks=120;
    }
    void campaignLoaded(){
        lastKills={};
        lastFlameKills=0;
        lastOwned={};
    }
    void died(){
        weaponKills={};
        pistolWarriors=shotgunHumans=smartgunSmall=0;
    }
    template<class Scene> void campaignTick(Scene& s){
        if(path.empty()||s.coopGame||g_survival.active||s.menu.open||s.mission.briefing)return;
        auto m=s.mem();
        const unsigned weapon=s.hud.selected();
        for(unsigned kind=2;kind<lastKills.size();++kind){
            const unsigned current=m.w(0xa0ae6+kind*2);
            const unsigned kills=current>=lastKills[kind]?current-lastKills[kind]:0;
            lastKills[kind]=current;
            if(!kills)continue;
            if(weapon<4)weaponKills[weapon]+=kills;
            if(weapon==0&&kind==6)pistolWarriors+=kills;
            if(weapon==1&&kind>=12&&kind<=14)shotgunHumans+=kills;
            if(weapon==3&&(kind==2||kind==3))smartgunSmall+=kills;
            if(weapon==0&&kind==7)unlock(89);
        }
        const auto flameKills=s.enemies->flameKills;
        if(flameKills>=lastFlameKills)weaponKills[4]+=flameKills-lastFlameKills;
        lastFlameKills=flameKills;
        constexpr unsigned goals[]={50,100,200,150,75},ids[]={79,80,82,83,81};
        for(unsigned i=0;i<5;++i)if(weaponKills[i]>=goals[i])unlock(ids[i]);
        if(pistolWarriors>=10)unlock(86);
        if(shotgunHumans>=15)unlock(87);
        if(smartgunSmall>=30)unlock(88);
        if(m.w(0xa0ab8)>=400)unlock(84);
        if(m.w(0xa0aba)>=200)unlock(85);
        constexpr const char* levels[]={"111","112","131","222"};
        constexpr unsigned weapons[]={1,4,2,3};
        for(unsigned i=0;i<4;++i){
            const bool owned=s.hud.owns(weapons[i]);
            if(owned&&!lastOwned[i]&&s.mission.sourceCode==levels[i])unlock(75+i);
            lastOwned[i]=owned;
        }
    }
    void campaignExit(const std::string& code,bool passed,const std::array<unsigned,3>& percent,unsigned difficulty){
        constexpr const char* main[]={"111","112","122","131","141","154","161","162","211","212","213",
                                      "222","231","232","262","263","311","331","351","371","381","391"};
        constexpr const char* supply[]={"113","114","115","155","242","243","352","353"};
        for(unsigned i=0;i<std::size(main);++i)if(code==main[i]&&passed){
            unlock(1+i);
            if(std::all_of(percent.begin(),percent.end(),[](unsigned value){return value==100;}))unlock(23+i);
            if(difficulty==2)unlock(45+i);
        }
        for(unsigned i=0;i<std::size(supply);++i)if(code==supply[i])unlock(67+i);
    }
    void survival(unsigned arena,const ExplorerSurvivalState& run,bool victory,bool online){
        if(arena>=3||!survivalScore(run.skulls,1))return;
        if(run.wavesSurvived>=5||victory)unlock(90+arena*2);
        if(run.scoredKills>=250)unlock(101);
        if(!victory)return;
        unlock(91+arena*2);
        if(!run.deaths)unlock(96);
        if(has(91)&&has(93)&&has(95))unlock(97);
        unsigned scoring=0;
        for(unsigned i=0;i<std::size(SurvivalSkullMultipliers);++i)
            if(SurvivalSkullMultipliers[i]&&(run.skulls&(1u<<i)))++scoring;
        if(scoring>=3)unlock(98);
        if(survivalSkullEnabled(run.skulls,LastStandSkull))unlock(99);
        if(online)unlock(100);
    }
    std::wstring toast;
    unsigned toastTicks=0;
private:
    std::filesystem::path path;
    std::array<bool,std::size(ExplorerAchievementCatalog)> unlocked{};
    std::array<unsigned,15> lastKills{};
    std::array<unsigned,5> weaponKills{};
    std::array<bool,4> lastOwned{};
    unsigned lastFlameKills=0,pistolWarriors=0,shotgunHumans=0,smartgunSmall=0;
};
