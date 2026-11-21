#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <iomanip>
#include <cstdint>

struct ExplorerLevelDoor
{
    unsigned x = 0, z = 0;    // the door record's cell (offset 0 of its 4: -1..2 along the door)
    int outX = 0, outZ = 0;   // towards its room
    std::vector<std::pair<unsigned, unsigned>> room;
};

struct ExplorerLevelConfig
{
    std::wstring name = L"Survival";
    std::string mode = "survival";
    unsigned lives = 5, waves = 10, startX = 0, startZ = 0, width = 0, height = 0;
    std::vector<unsigned> weapons{ 0 };
    std::string roster = "campaign";
    bool queen = false;
    std::vector<ExplorerLevelDoor> doors;
    std::vector<std::pair<unsigned, unsigned>> ammo, supply, health, reserveAmmo;
    std::filesystem::path map;   // the folder's L<code>LEV.MAP
    bool survival() const { return mode != "exterminate"; }
    bool queensRevenge() const { return mode == "queens-revenge"; }
    void setQueensRevenge(){
        mode="queens-revenge";
        waves=1;
        lives=1;
        queen=false;
        weapons={1,2,3,4,0};
    }
};

inline std::vector<std::pair<unsigned, unsigned>> explorerParseSurvivalCells(const std::string& text)
{
    std::vector<std::pair<unsigned, unsigned>> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ';'))
    {
        const auto colon = item.find(':');
        if (colon != std::string::npos) { out.push_back({ unsigned(std::atoi(item.c_str())), unsigned(std::atoi(item.c_str() + colon + 1)) }); }
    }
    return out;
}

// Reads a level folder's level.txt; false if it is missing or has no map beside it.
inline bool explorerLoadSurvivalConfig(const std::filesystem::path& folder, ExplorerLevelConfig& cfg)
{
    cfg = ExplorerLevelConfig{};
    std::ifstream in(folder / "level.txt");
    if (!in) { return false; }
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        const auto eq = line.find('=');
        if (eq == std::string::npos) { continue; }
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        const char* v = value.c_str();
        if (key == "name") { cfg.name.assign(value.begin(), value.end()); }
        else if (key == "mode") { cfg.mode = value; }
        else if (key == "lives") { cfg.lives = unsigned(std::atoi(v)); }
        else if (key == "waves") { cfg.waves = unsigned(std::atoi(v)); }
        else if (key == "roster") { cfg.roster = value; }
        else if (key == "queen") { cfg.queen = std::atoi(v) != 0; }
        else if (key == "weapons")
        {
            cfg.weapons.clear();
            std::stringstream ss(value);
            std::string w;
            while (std::getline(ss, w, ',')) { if (!w.empty()) { cfg.weapons.push_back(unsigned(std::atoi(w.c_str()))); } }
        }
        else if (key == "start" || key == "size")
        {
            const auto comma = value.find(',');
            const unsigned a = unsigned(std::atoi(v)), b = comma == std::string::npos ? 0 : unsigned(std::atoi(v + comma + 1));
            if (key == "start") { cfg.startX = a; cfg.startZ = b; } else { cfg.width = a; cfg.height = b; }
        }
        else if (key == "door")
        {
            ExplorerLevelDoor d;
            int fields[4] = {};
            std::size_t p = 0;
            for (int i = 0; i < 4; ++i)
            {
                fields[i] = std::atoi(value.c_str() + p);
                p = value.find(',', p);
                if (p == std::string::npos) { break; }
                ++p;
            }
            if (p == std::string::npos) { continue; }
            d.x = unsigned(fields[0]); d.z = unsigned(fields[1]); d.outX = fields[2]; d.outZ = fields[3];
            d.room = explorerParseSurvivalCells(value.substr(p));
            cfg.doors.push_back(std::move(d));
        }
        else if (key == "ammo") { cfg.ammo = explorerParseSurvivalCells(value); }
        else if (key == "supply") { cfg.supply = explorerParseSurvivalCells(value); }
        else if (key == "health") { cfg.health = explorerParseSurvivalCells(value); }
        else if (key == "reserve_ammo") { cfg.reserveAmmo = explorerParseSurvivalCells(value); }
    }
    cfg.lives = cfg.lives ? std::min(cfg.lives, 99u) : 5;
    cfg.waves = cfg.waves ? std::min(cfg.waves, 30u) : 10;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error))
    {
        const auto name = entry.path().filename().string();
        if (name.size() == 11 && (name[0] == 'L' || name[0] == 'l') && name.substr(4) == "LEV.MAP") { cfg.map = entry.path(); }
    }
    if (cfg.map.empty() || cfg.width < 3 || cfg.width > 127 || cfg.height < 3 || cfg.height > 127 ||
        cfg.startX >= cfg.width || cfg.startZ >= cfg.height) { return false; }
    auto validCells = [&](const auto& cells) {
        return std::all_of(cells.begin(), cells.end(), [&](const auto& cell) {
            return cell.first < cfg.width && cell.second < cfg.height;
        });
    };
    return (!cfg.survival() || (!cfg.doors.empty() && cfg.doors.size() <= 32)) &&
        validCells(cfg.ammo) && validCells(cfg.supply) && validCells(cfg.health) && validCells(cfg.reserveAmmo) &&
        std::all_of(cfg.doors.begin(), cfg.doors.end(), [&](const auto& door) {
            return door.x < cfg.width && door.z < cfg.height && !door.room.empty() && validCells(door.room);
        });
}

inline constexpr const wchar_t* SurvivalArenaNames[]={L"Colony Hive",L"Foundry Lair",L"Derelict Lair"};
inline constexpr const char* SurvivalArenaFolders[]={"SURVIVAL","SURVIVAL2","SURVIVAL3"};

enum SurvivalSkull : unsigned {
    AcidSkull, CrippledSkull, BrokenVisorSkull, GunMalfunctionSkull, ExoskeletonSkull,
    TriggerFingerSkull, ElectricBootsSkull, InfiniteAmmoSkull, ShrunkKidsSkull, RocketManSkull,
    LastStandSkull, NoResupplySkull, SwarmSkull, ChainReactionSkull, FacehuggerPartySkull, LuckyDipSkull, GameOverManSkull
};
inline constexpr const wchar_t* SurvivalSkullNames[]={
    L"Acid Skull", L"Crippled Skull", L"Broken Visor Skull", L"Gun Malfunction Skull",
    L"Exoskeleton Upgrade Skull", L"Trigger Finger Skull", L"Electric Boots Skull",
    L"Infinite Ammo Skull", L"Honey, I Shrunk The Kids Skull", L"Rocket Man Skull",
    L"Last Stand Skull", L"No Resupply Skull", L"Swarm Skull",
    L"Chain Reaction Skull", L"Facehugger Party Skull", L"Lucky Dip Skull", L"Game Over, Man! Skull"
};
inline constexpr unsigned SurvivalSkullMultipliers[]={5,10,5,10,15,0,0,0,0,0,15,10,10,0,0,0,15};
inline bool survivalSkullEnabled(unsigned mask,SurvivalSkull skull){return (mask&(1u<<skull))!=0;}
inline std::uint64_t survivalScore(unsigned mask,std::uint64_t base){
    constexpr unsigned pair=(1u<<GameOverManSkull)|(1u<<ExoskeletonSkull);
    if((mask&pair)==pair){
        base*=20;
        mask&=~pair;
    }
    for(unsigned i=0;i<std::size(SurvivalSkullMultipliers);++i)
        if(mask&(1u<<i))base*=SurvivalSkullMultipliers[i];
    return base;
}
struct SurvivalRun {
    std::wstring map;
    unsigned kills{},deaths{},seconds{},ammo{},accuracy{},waves{},skulls{};
    std::uint64_t score{};
    std::wstring outcome;
};
inline std::vector<SurvivalRun> readSurvivalRuns(const std::filesystem::path& path){
    std::wifstream file(path);
    std::vector<SurvivalRun> runs;
    std::wstring line;
    while(std::getline(file,line)){
        std::wistringstream row(line);
        SurvivalRun run;
        if(row>>std::quoted(run.map)>>run.kills>>run.deaths>>run.seconds>>run.ammo
              >>run.accuracy>>run.waves>>run.skulls>>run.score>>std::quoted(run.outcome))
            runs.push_back(std::move(run));
    }
    return runs;
}
inline std::wstring survivalRunDetails(const SurvivalRun& r){
    return r.outcome+L"\nScore: "+std::to_wstring(r.score)+L"\nKills: "+std::to_wstring(r.kills)
        +L"   Deaths: "+std::to_wstring(r.deaths)+L"\nTime: "+std::to_wstring(r.seconds/60)
        +(r.seconds%60<10?L":0":L":")+std::to_wstring(r.seconds%60)
        +L"\nAmmo used: "+std::to_wstring(r.ammo)+L"   Accuracy: "+std::to_wstring(r.accuracy)
        +L"%\nWaves survived: "+std::to_wstring(r.waves);
}

struct ExplorerSurvivalState
{
    struct Pending { unsigned char kind, health, door; };
    unsigned skulls=0, scoredKills=0, wavesSurvived=0;
    std::uint64_t baseScore=0;
    bool recorded=false;
    bool active = false;      // a run is loaded (the campaign's death, exit and achievements step aside)
    bool setup = false;       // the level was (re)loaded: place the player, loadout, first countdown
    bool ended = false;       // won or lost: the results are next
    unsigned queenEntity = 0; // record 0 remains in place until the finale
    std::array<int,3> queenSacAnchor{};
    bool queenOut = false;    // the last wave released the queen
    bool victory = false;
    unsigned wave = 0;        // 1 to cfg.waves; 0 before the first
    unsigned lives = 5, deaths = 0;
    unsigned introTicks = 0;  // black opening and fade-in
    unsigned breakTicks = 0;  // countdown to the next wave (30 ticks a second)
    unsigned spawnTicks = 0;  // to the next enemy coming out of a door
    unsigned endTicks = 0;    // after the win, before the results
    unsigned nextRecord = 1;  // monster record the next spawn rewrites
    unsigned alive = 0;       // living enemies, as of the last tick
    unsigned drops = 0;
    unsigned ticks = 0;       // played time
    std::vector<Pending> queue;          // this wave's enemies still to come out of their doors
    std::map<unsigned, unsigned> corpses; // dead enemy -> the wave it died in (cleared two waves later)
    struct QueenSupplies {int health;unsigned damage=0;};
    std::map<unsigned,QueenSupplies> queenSupplies;
    unsigned queensKilled=0;   // Queen's Revenge!: every second one drops supplies
    std::vector<unsigned> doorEntry;      // cfg.doors[i] -> its door in the level's door list
    ExplorerLevelConfig cfg;
};

inline ExplorerSurvivalState g_survival;
inline std::vector<std::wstring> g_survivalResults;                     // the results page's rows

inline bool activeSurvivalSkull(SurvivalSkull skull){
    return g_survival.active&&survivalSkullEnabled(g_survival.skulls,skull);
}
