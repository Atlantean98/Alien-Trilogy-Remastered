#pragma once
#include "ExplorerAudio.h"
#include "ExplorerSurvivalState.h"
#include "ExplorerMenu.h"
#include "ExplorerMovie.h"
#include "PickupRuntime.h"
#include "ProjectileRuntime.h"
#include "WorldMovement.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace explorer_survival {
constexpr unsigned kTicks = 30;       // game ticks a second
constexpr unsigned kQueenHiss = 75;
constexpr unsigned kIntroBlackTicks = 55; // The original hiss lasts about 1.76 seconds.
constexpr unsigned kIntroFadeTicks = 24;
constexpr unsigned kRoomLimit = 5;    // enemies waiting in one spawn room at a time

struct Group { unsigned char kind, count, health; };
struct Wave { Group groups[4]; };
// kind (ExplorerEnemies names: 2 hugger, 3 burster, 4 bambi, 5 dog, 6 warrior, 8 ceiling warrior, 9 ceiling dog,
// 11 guard, 12 soldier, 13 synth, 14 handler), count, health byte (the levels' own values for that section).
constexpr Wave kWaveTable[10] = {
    { { { 2, 6, 2 }, { 6, 4, 30 } } },                                        // Section 1
    { { { 6, 10, 30 }, { 2, 4, 3 }, { 3, 3, 10 } } },
    { { { 6, 10, 50 }, { 11, 6, 10 }, { 8, 3, 50 } } },
    { { { 4, 10, 25 }, { 12, 6, 25 }, { 2, 4, 5 } } },                        // Section 2
    { { { 5, 12, 50 }, { 12, 8, 30 }, { 13, 2, 80 } } },
    { { { 5, 14, 70 }, { 9, 4, 50 }, { 14, 6, 30 }, { 3, 4, 5 } } },
    { { { 5, 12, 65 }, { 6, 10, 70 }, { 14, 6, 30 } } },                      // Section 3
    { { { 6, 16, 75 }, { 9, 6, 70 }, { 13, 4, 80 } } },
    { { { 6, 18, 75 }, { 5, 10, 70 }, { 13, 6, 80 }, { 14, 6, 30 } } },
    { { { 6, 12, 75 }, { 2, 6, 5 } } },                                       // the queen's escort
};
// Monster records by kind, taken from the levels (L111, L131, L211, L231, L112, L161, L122, L371): position, facing,
// health and trigger are filled in per spawn.
constexpr unsigned char kTemplates[15][20] = {
    {},
    {},
    { 2, 0, 0, 255, 0, 2, 255, 0, 0, 0, 0, 4, 252, 9, 100, 0, 0, 6, 0, 3 },
    { 3, 0, 0, 255, 0, 10, 255, 0, 0, 0, 182, 4, 46, 22, 100, 0, 130, 7, 0, 3 },
    { 4, 0, 0, 255, 0, 20, 255, 0, 0, 0, 0, 8, 170, 17, 60, 0, 228, 3, 220, 2 },
    { 5, 0, 0, 255, 0, 30, 255, 0, 0, 0, 130, 9, 0, 20, 100, 0, 244, 3, 0, 3 },
    { 6, 0, 0, 255, 0, 30, 255, 0, 0, 0, 11, 6, 184, 14, 100, 0, 18, 4, 0, 3 },
    {},
    { 8, 0, 0, 255, 0, 50, 255, 0, 0, 0, 0, 16, 199, 32, 100, 0, 241, 3, 231, 2 },
    { 9, 0, 0, 255, 0, 30, 255, 0, 0, 0, 51, 7, 0, 20, 40, 0, 192, 3, 0, 3 },
    {},
    { 11, 0, 0, 255, 0, 10, 255, 0, 0, 0, 79, 6, 137, 25, 50, 0, 120, 15, 187, 14 },
    { 12, 0, 0, 255, 0, 20, 255, 0, 0, 0, 0, 8, 210, 18, 60, 0, 0, 33, 137, 14 },
    { 13, 0, 0, 255, 0, 80, 255, 0, 0, 0, 136, 4, 146, 24, 30, 0, 114, 24, 0, 6 },
    { 14, 0, 0, 255, 0, 30, 255, 0, 0, 0, 0, 8, 26, 35, 80, 0, 0, 42, 86, 34 },
};
// Pickup kinds (PickupRuntime.cpp awardPickup): 1-4 weapons, 9-14 their ammunition, 18/19 armour, 20 medikit.
struct Item { unsigned char kind, amount, reserve; };
// Indexed by the PC HUD weapon slot: pistol, shotgun, pulse rifle, smartgun, flamethrower.
constexpr Item kWeapon[5] = { { 0, 15, 9 }, { 1, 60, 0 }, { 2, 100, 4 }, { 4, 100, 4 }, { 3, 250, 0 } };
constexpr Item kAmmo[5] = { { 9, 15, 6 }, { 10, 40, 0 }, { 11, 100, 4 }, { 14, 100, 4 }, { 13, 100, 0 } };
constexpr unsigned kResupplyTicks = 15 * kTicks;

inline unsigned facingInto(int outX, int outZ)
{
    constexpr int dx[] = { 0, 1, 1, 1, 0, -1, -1, -1 }, dz[] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    for (unsigned f = 0; f < 8; ++f) { if (dx[f] == -outX && dz[f] == -outZ) { return f; } }
    return 0;
}
inline unsigned next(unsigned& seed) { seed = seed * 1664525u + 1013904223u; return seed >> 8; }

// All arenas share Colony Hive's health and roster progression.
inline const Wave& waveFor(const ExplorerLevelConfig& cfg,unsigned wave)
{
    const unsigned regular=std::max(1u,cfg.waves-(cfg.queen?1u:0u));
    if(cfg.queen&&wave==cfg.waves)return kWaveTable[9];
    return kWaveTable[(std::min(wave,regular)-1)*8/std::max(1u,regular-1)];
}
constexpr unsigned kWeaponWave[]={1,2,4,7,5};
}   // namespace explorer_survival

// The player at the level's start, standing (respawns too).
template <class Scene>
void explorerSurvivalPlace(Scene& s)
{
    const auto& cfg = g_survival.cfg;
    const unsigned x = cfg.startX * 512 + 256, z = cfg.startZ * 512 + 256;
    s.player.position[0] = short(int(x) - int(s.player.originX));
    s.player.position[2] = short(int(z) - int(s.player.originZ));
    s.player.position[1] = short(trilogy::original::floorHeight(s.grid, x, z));
    auto m = s.mem();
    const unsigned previousCell = m.d(0xa0c40) + s.player.currentCell * 16;
    if (m.b(previousCell + 12) == 255) m.b(previousCell + 12, 0);
    s.player.currentCell = s.grid.index(x, z);
    s.previousPosition = s.player.position;
    s.pitch = 0;
    m.w(0xa0ab8, 100);
    m.b(0xa0c90, m.b(0xa0c90) & ~0x20u); // Clear the death flag that blocks pickups.
    s.player.health = 100;
    m.w(0xa0b30, 90);   // the damage cooldown: three seconds to get moving
}

// Hold back automatic spawns. Setup places the perched Queen and the waves spawn the other records.
template <class Scene, class Memory>
void explorerSurvivalBeforeEnemies(Scene&, Memory m)
{
    auto& v = g_survival;
    if (!v.active) { return; }
    auto cfg = std::move(v.cfg);
    const auto skulls=v.skulls;
    v = ExplorerSurvivalState{};
    v.skulls=skulls;
    v.cfg = std::move(cfg);
    v.active = v.setup = true;
    v.lives = survivalSkullEnabled(v.skulls,LastStandSkull)?1:v.cfg.lives;
    if (!v.cfg.survival()) { return; }
    const unsigned records = m.d(0xa0c48), count = m.w(0xa0c1a);
    for (unsigned i = 0; i < count; ++i) { m.b(records + i * 20 + 7, 0); m.b(records + i * 20 + 8, 1); }
}

template <class Scene> bool explorerSurvivalSetup(Scene& s);
template <class Scene> void explorerSaveSurvivalRun(Scene& s,const std::wstring& outcome);

template <class Scene>
void explorerSurvivalStart(Scene& s,bool queensRevenge=false)
{
    ExplorerLevelConfig cfg;
    const auto folder = s.root / "GameData" / SurvivalArenaFolders[s.menu.survivalArena];
    if (!explorerLoadSurvivalConfig(folder, cfg))
    {
        s.menu.show(false);
        s.menu.message = L"Missing arena files for "+std::wstring(SurvivalArenaNames[s.menu.survivalArena])+L".";
        return;
    }
    explorerSaveSurvivalRun(s,L"Restarted");
    if(queensRevenge)cfg.setQueensRevenge();
    g_survival = ExplorerSurvivalState{};
    g_survival.active = true;
    g_survival.cfg = std::move(cfg);
    s.difficulty = 1;
    g_survival.skulls=s.menu.survivalSkulls;
    s.level = g_survival.cfg.map;
    try {
        s.load();
        s.mission.briefing = false;
        s.mission.remaining = 0;
        if (!explorerSurvivalSetup(s)) throw std::runtime_error("Invalid survival arena");
    } catch (...) {
        g_survival = {};
        s.returnToMainMenu();
        throw;
    }
    s.menu.hasGame = true;
    s.menu.open = false;
    if(g_survival.queenEntity||g_survival.cfg.queensRevenge()){
        g_survival.introTicks=explorer_survival::kIntroBlackTicks+explorer_survival::kIntroFadeTicks;
        s.audio->pause(false);
        s.audio->play(explorer_survival::kQueenHiss);
    }
}

// Retire old bodies and remove their acid pools so later waves can reuse the enemy slots.
template <class Scene>
void explorerSurvivalClearCorpses(Scene& s, unsigned before, unsigned limit)
{
    auto& v = g_survival;
    auto m = s.mem();
    const auto list = s.enemies->active();
    unsigned done = 0;
    for (auto it = v.corpses.begin(); it != v.corpses.end() && done < limit;)
    {
        const unsigned e = it->first;
        if (before && it->second >= before) { ++it; continue; }
        if (std::find(list.begin(), list.end(), e) != list.end() && m.s(e + 0x58) <= 0)
        {
            const unsigned cell = m.d(e + 0x78);
            if (cell && m.b(cell + 10) == 12) { m.b(cell + 10, 0); }
            s.enemies->retireSurvivalCorpse(e);
            ++done;
        }
        it = v.corpses.erase(it);
    }
}

template <class Scene>
bool explorerSurvivalSpawn(Scene& s, unsigned kind, unsigned health, unsigned cx, unsigned cz, unsigned facing)
{
    using namespace explorer_survival;
    auto& v = g_survival;
    auto m = s.mem();
    const unsigned count = m.w(0x2365f0), base = m.d(0x2365d4), first = v.cfg.queen ? 1u : 0u;
    if (count <= first || kind > 14 || !kTemplates[kind][0]) { return false; }
    const unsigned index = v.nextRecord >= first && v.nextRecord < count ? v.nextRecord : first;
    v.nextRecord = index + 1 < count ? index + 1 : first;
    const unsigned r = base + index * 20;
    for (unsigned i = 0; i < 20; ++i) { m.b(r + i, kTemplates[kind][i]); }
    m.b(r + 1, cx); m.b(r + 2, cz); m.b(r + 4, facing); m.b(r + 5, std::min(255u, health)); m.b(r + 6, 255); m.b(r + 7, 0); m.b(r + 8, 0);
    const unsigned e = s.enemies->spawnSurvivalEnemy(index);
    if (!e) { return false; }
    // Later waves close the distance faster without changing the health curve.
    const unsigned pace=v.wave>=7?110:v.wave>=4?105:100;
    m.w(e+0x5e,(m.w(e+0x5e)*pace+50)/100);
    if(v.wave>=4)m.w(e+0x52,v.wave>=7?8:16);
    m.b(e + 0x6c, m.b(e + 0x6c) | 2);   // knows where the player is: comes straight out hunting
    v.corpses.erase(e);
    s.previousEnemies.erase(e);
    return true;
}

template <class Scene>
bool explorerSurvivalPlaceQueen(Scene& s)
{
    auto m = s.mem();
    const unsigned r = m.d(0x2365d4);   // record 0: the queen, where the editor put her
    m.b(r + 7, 0);
    m.b(r + 8, 0);
    const unsigned e = s.enemies->spawnSurvivalEnemy(0);
    if (!e) { return false; }
    g_survival.queenEntity = e;
    g_survival.queenSacAnchor = {m.s(e),m.s(e+0x5a),m.s(e+4)};
    s.enemies->perchSurvivalQueen(e);
    s.prepareSurvivalSacCollision();
    return true;
}

// A pickup on a cell unless one is already there; `room` caps how many the level holds (the pool is 128).
template <class Scene>
bool explorerSurvivalDrop(Scene& s, const explorer_survival::Item& item, unsigned cx, unsigned cz, unsigned room = 112)
{
    if (!item.kind) { return false; }
    auto m = s.mem();
    if(s.coopGame&&s.coopSurvivalPickup){
        s.coopSurvivalPickup(item.kind,item.amount,item.reserve,cz*s.grid.width+cx,room);
        return true;
    }
    const auto list = s.pickups.active();
    if (list.size() >= room) { return false; }
    const int ox = m.w(0xa0c34), oz = m.w(0xa0c36);
    for (auto p : list)
    {
        if (unsigned(m.s(p) + ox) >> 9 == cx && unsigned(m.s(p + 4) + oz) >> 9 == cz) { return false; }
    }
    s.pickups.dropSurvivalPickup(item.kind, item.amount, item.reserve, cx, cz);
    return true;
}

// Queen's Revenge! (every second Queen killed): a full-health medikit and 250 rounds for each gun, on the arena's
// supply spots nearest to (cx, cz) - known walkable floor, one item each, skipping a spot that still holds one.
template <class Scene>
void explorerQueenRevengeSupplies(Scene& s, unsigned cx, unsigned cz)
{
    using namespace explorer_survival;
    const auto& cfg = g_survival.cfg;
    std::vector<Item> items{ Item{ 20, 100, 0 } };
    for (unsigned w = 0; w < 5; ++w) { items.push_back(Item{ kAmmo[w].kind, 250, 0 }); }
    auto spots = cfg.ammo;
    spots.insert(spots.end(), cfg.health.begin(), cfg.health.end());
    auto away = [&](const std::pair<unsigned, unsigned>& c) { const double dx = double(c.first) - cx, dz = double(c.second) - cz; return dx * dx + dz * dz; };
    std::stable_sort(spots.begin(), spots.end(), [&](const auto& a, const auto& b) { return away(a) < away(b); });
    spots.erase(std::unique(spots.begin(), spots.end()), spots.end());
    std::size_t next = 0;
    for (const auto& c : spots) { if (next == items.size()) { break; } if (explorerSurvivalDrop(s, items[next], c.first, c.second, 128)) { ++next; } }
    s.notice = L"TWO QUEENS DOWN - SUPPLIES DROPPED";
    s.noticeTicks = 120;
}

// Rotate ammo types across weapons the player owns.
template <class Scene>
explorer_survival::Item explorerSurvivalAmmoFor(Scene& s, unsigned turn)
{
    using namespace explorer_survival;
    Item options[5];
    unsigned n = 0;
    for (unsigned w = 0; w < 5; ++w) { if (s.hud.owns(w)) { options[n++] = kAmmo[w]; } }
    return n ? options[turn % n] : kAmmo[0];
}

template <class Scene>
void explorerSurvivalRestock(Scene& s,bool initial=false)
{
    using namespace explorer_survival;
    const auto& v = g_survival;
    if(v.cfg.queensRevenge()||(!initial&&survivalSkullEnabled(v.skulls,NoResupplySkull)))return;
    // Refill the central reserve first. Each weapon gets a dedicated spot so
    // uncollected pistol ammo cannot block a later weapon's ammunition.
    for (unsigned w = 0; w < 5 && w < v.cfg.reserveAmmo.size(); ++w)
    {
        if (!s.hud.owns(w)) continue;
        const auto& cell = v.cfg.reserveAmmo[w];
        explorerSurvivalDrop(s, kAmmo[w], cell.first, cell.second);
    }
    for (const auto& cell : v.cfg.health)
        explorerSurvivalDrop(s, Item{20, 50, 0}, cell.first, cell.second);
    unsigned turn = v.wave + v.ticks / kResupplyTicks;
    for (const auto& cell : v.cfg.ammo)
        explorerSurvivalDrop(s, explorerSurvivalAmmoFor(s, turn++), cell.first, cell.second);
}

template <class Scene>
void explorerSurvivalWave(Scene& s, unsigned n)
{
    using namespace explorer_survival;
    auto& v = g_survival;
    v.wave = n;
    v.queue.clear();
    if (n >= 3) { explorerSurvivalClearCorpses(s, n - 1, 1000); }   // bodies from two waves back
    if (n >= 3 && n % 2 == 1) { s.objects.respawn(23); }   // every other wave: the destroyed explosive barrels return
    const Wave& wave = waveFor(v.cfg,n);
    std::vector<std::pair<unsigned char, unsigned char>> list;
    for (const auto& g : wave.groups)
    {
        const unsigned count = g.count;
        for (unsigned i = 0; i < count; ++i) { list.push_back({ g.kind, g.health }); }
    }
    auto isXeno=[](unsigned kind){return (kind>=2&&kind<=6)||kind==8||kind==9;};
    unsigned seed = n * 2654435761u;
    if(survivalSkullEnabled(v.skulls,SwarmSkull)){
        std::vector<std::pair<unsigned char,unsigned char>> xenos;
        for(const auto& entry:list)if(isXeno(entry.first))xenos.push_back(entry);
        const auto extra=(xenos.size()+2)/4;
        for(size_t i=0;i<extra;++i)list.push_back(xenos[next(seed)%xenos.size()]);
    }
    if(survivalSkullEnabled(v.skulls,FacehuggerPartySkull)){
        unsigned replaced=0;
        for(auto& entry:list)if(isXeno(entry.first)&&entry.first!=2&&replaced++%3==0)
            entry={2,10};
        if(!replaced&&!list.empty())list.push_back({2,10});
    }
    // Dedicated ammo reserves also serve as weapon drop points. Keep future
    // weapon slots free even where the reserve overlaps an intermission cache.
    for(unsigned weapon=1;weapon<5;++weapon){
        if(n!=kWeaponWave[weapon]||s.hud.owns(weapon))continue;
        if(weapon<v.cfg.reserveAmmo.size()){
            const auto& [x,z]=v.cfg.reserveAmmo[weapon];
            if(explorerSurvivalDrop(s,kWeapon[weapon],x,z,128))continue;
        }
        for(const auto& [x,z]:v.cfg.supply)
            if(explorerSurvivalDrop(s,kWeapon[weapon],x,z,128))break;
    }
    if(survivalSkullEnabled(v.skulls,LuckyDipSkull)){
        std::array<unsigned,5> owned{};
        unsigned count=0;
        for(unsigned weapon=0;weapon<5;++weapon)if(s.hud.owns(weapon))owned[count++]=weapon;
        if(count){
            const auto weapon=owned[trilogy::original::nextGameRandom(s.mem())%count];
            const auto& ammo=kAmmo[weapon];
            trilogy::original::awardPickup(s.mem(),ammo.kind,ammo.amount,ammo.reserve);
            s.hud.select(weapon);
            s.hud.syncAmmo();
        }
    }
    for (std::size_t i = list.size(); i > 1; --i) { std::swap(list[i - 1], list[next(seed) % i]); }
    const unsigned doors = unsigned(std::min(v.cfg.doors.size(), v.doorEntry.size()));
    for (std::size_t i = 0; i < list.size() && doors; ++i)
    {
        v.queue.push_back({ list[i].first, list[i].second, (unsigned char)((i + n) % doors) });
    }
    explorerSurvivalRestock(s);
    v.spawnTicks = kTicks;
    // Slot 75 is the cue in the PS1 Queen's original alert animation.
    if (v.cfg.queen && v.queenEntity) { s.audio->play(kQueenHiss); }
    if (v.cfg.queen && n == v.cfg.waves)
    {
        s.enemies->releaseSurvivalQueen();
        v.queenOut = true;
        s.notice = L"THE QUEEN DESCENDS FROM HER THRONE.";
    }
    else if (n == 3) { s.notice = L"THE QUEEN IS AGITATED."; }
    else if (n == 7) { s.notice = L"THE QUEEN IS LIVID."; }
    else { s.notice = L"THE QUEEN IS NOT PLEASED."; }
    s.noticeTicks = 120;
}

template <class Scene>
void explorerSurvivalCleared(Scene& s)
{
    using namespace explorer_survival;
    auto& v = g_survival;
    std::vector<Item> items;
    items.push_back(v.wave%2?Item{19,100,0}:Item{18,50,0});
    for (unsigned w = 0; w < 5; ++w) { if (s.hud.owns(w)) { items.push_back(kAmmo[w]); } }
    if (s.hud.owns(2)) { items.push_back(Item{ 12, 5, 0 }); }   // grenades for the pulse rifle
    items.push_back(Item{ 20, 40, 0 });
    std::size_t slot = 0;
    for (const auto& item : items)
    {
        if (slot >= v.cfg.supply.size()) { break; }
        while(slot<v.cfg.supply.size()){
            const auto& cell=v.cfg.supply[slot++];
            bool reserved=false;
            for(unsigned weapon=1;weapon<5&&weapon<v.cfg.reserveAmmo.size();++weapon)
                if(kWeaponWave[weapon]>v.wave&&cell==v.cfg.reserveAmmo[weapon])reserved=true;
            if(reserved)continue;
            const auto& [x,z]=cell;
            if(explorerSurvivalDrop(s,item,x,z,120))break;
        }
    }
    s.notice = L"WAVE " + std::to_wstring(v.wave) + L" CLEARED - SUPPLIES DROPPED";
    s.noticeTicks = 150;
    v.breakTicks = 12 * kTicks;
}

template <class Scene>
SurvivalRun explorerSurvivalRun(const Scene& s,const std::wstring& outcome){
    const auto& v=g_survival;
    const unsigned shots=s.enemies?s.enemies->shots:0;
    const unsigned hits=s.enemies?s.enemies->accurateShots:0;
    return {v.cfg.queensRevenge()?L"Queen's Revenge! - "+std::wstring(SurvivalArenaNames[s.menu.survivalArena]):v.cfg.name,v.scoredKills,v.deaths,v.ticks/explorer_survival::kTicks,
            s.enemies?s.enemies->ammoUsed:0,shots?unsigned(std::uint64_t(hits)*100/shots):0,
            v.wavesSurvived,v.skulls,survivalScore(v.skulls,v.baseScore),outcome};
}

template <class Scene>
void explorerSaveSurvivalRun(Scene& s,const std::wstring& outcome){
    auto& v=g_survival;
    if(!v.active||v.recorded)return;
    const auto run=explorerSurvivalRun(s,outcome);
    const auto path=s.menu.survivalHistoryPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(),error);
    std::wofstream file(path,std::ios::app);
    file<<std::quoted(run.map)<<L' '<<run.kills<<L' '<<run.deaths<<L' '<<run.seconds
        <<L' '<<run.ammo<<L' '<<run.accuracy<<L' '<<run.waves<<L' '<<run.skulls
        <<L' '<<run.score<<L' '<<std::quoted(run.outcome)<<L'\n';
    file.close();
    v.recorded=bool(file);
}
template <class Scene>
void explorerSurvivalFinish(Scene& s, bool victory)
{
    auto& v = g_survival;
    v.ended = true;
    if(!v.cfg.queensRevenge())s.menu.achievements.survival(s.menu.survivalArena,v,victory,s.coopGame);
    const auto outcome=victory?L"Victory":L"Overrun";
    if(victory&&v.cfg.survival())v.wavesSurvived=v.cfg.waves;
    explorerSaveSurvivalRun(s,outcome);
    const auto run=explorerSurvivalRun(s,outcome);
    g_survivalResults={survivalRunDetails(run)};
    const bool saved=v.recorded;
    v.active=false;
    for (unsigned i : v.doorEntry) { if (i < s.doors.entries.size()) { s.doors.entries[i].remaining = 0; } }
    s.menu.show(false);
    s.menu.page = "survival results";
    if(!saved)s.menu.message=L"Could not save Survival run history.";
    s.menu.difficultyLocked = false;
}

// A life lost: back to the start with full health, or the end of the run.
template <class Scene>
bool explorerSurvivalDeath(Scene& s)
{
    auto& v = g_survival;
    if (!v.active) { return false; }
    if (v.ended) { return true; }
    ++v.deaths;
    if (v.lives) { --v.lives; }
    if (!v.lives)
    {
        s.queueMovie(explorerDeathMovie(s.mem().w(0x22d3a8)));
        explorerSurvivalFinish(s, false);
        return true;
    }
    if (s.enemies) { s.enemies->attachmentState({}); }   // a facehugger on the face lets go
    explorerSurvivalPlace(s);
    s.firing = false;
    s.hud.stopFiring();
    s.notice = v.lives == 1 ? std::wstring(L"LAST LIFE") : L"LIVES LEFT: " + std::to_wstring(v.lives);
    s.noticeTicks = 120;
    return true;
}

template <class Scene>
bool explorerSurvivalSetup(Scene& s)
{
    using namespace explorer_survival;
    auto& v = g_survival;
    auto m = s.mem();
    const unsigned minimumRecords=v.cfg.queensRevenge()?6:2;
    if (m.w(0xa0c30) != v.cfg.width || m.w(0xa0c32) != v.cfg.height || (v.cfg.survival() && m.w(0x2365f0) < minimumRecords))
    {
        // The arena metadata must match the map.
        return false;
    }
    for (unsigned kind : { 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 11u, 12u, 13u, 14u }) { s.enemies->preloadSurvivalEnemy(kind); }
    explorerSurvivalPlace(s);
    s.yaw = 0;
    for (unsigned w : v.cfg.weapons) { if (w < 5) { trilogy::original::awardPickup(m, kWeapon[w].kind, kWeapon[w].amount, kWeapon[w].reserve); } }
    if(v.cfg.queensRevenge())
        for(const auto& ammo:kAmmo)trilogy::original::awardPickup(m,ammo.kind,ammo.amount,ammo.reserve);
    s.hud.syncAmmo();
    s.hud.select(v.cfg.weapons.empty() ? 0 : v.cfg.weapons.back() < 5 ? v.cfg.weapons.back() : 0);
    v.setup = false;
    if (v.cfg.survival())
    {
        // Each spawn door's place in the level's door list (it has the others too), by its record's cell.
        v.doorEntry.clear();
        for (const auto& d : v.cfg.doors)
        {
            const unsigned cell = m.d(0xa0c40) + (d.z * m.w(0xa0c30) + d.x) * 16;
            for (unsigned i = 0; i < s.doors.entries.size(); ++i)
            {
                if (s.doors.entries[i].cell == cell) { v.doorEntry.push_back(i); break; }
            }
            if (v.doorEntry.size() != std::size_t(&d - v.cfg.doors.data()) + 1) { break; }
        }
        if (v.doorEntry.size() != v.cfg.doors.size()) return false;
        for (unsigned i : v.doorEntry) { s.doors.entries[i].target = false; s.doors.entries[i].remaining = 0; }
        if(v.cfg.queensRevenge()){
            s.pickups.clear();
            for(unsigned index:v.doorEntry){
                auto& door=s.doors.entries[index];
                door.amount=door.previous=0.f;
                for(auto cell:door.cells){
                    m.b(cell+10,20);
                    m.b(cell+7,m.b(cell+7)|128);
                    m.b(cell+13,door.floor/32);
                }
            }
            // Use distinct authored arena pickup cells, away from the player and each other.
            auto candidates=v.cfg.ammo;
            const std::pair<unsigned,unsigned> start{v.cfg.startX,v.cfg.startZ};
            auto distance=[](auto a,auto b){return std::hypot(double(a.first)-b.first,double(a.second)-b.second);};
            std::stable_sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return distance(a,start)<distance(b,start);});
            std::vector<std::pair<unsigned,unsigned>> populated{start};
            const unsigned base=m.d(0x2365d4),cells=m.d(0xa0c40);
            for(const auto& cell:candidates){
                const auto [x,z]=cell;
                if(x>=v.cfg.width||z>=v.cfg.height||m.b(cells+(z*v.cfg.width+x)*16+12))continue;
                if(std::any_of(populated.begin(),populated.end(),[&](auto other){return distance(cell,other)<5.;}))continue;
                const unsigned index=unsigned(populated.size()),record=base+index*20;
                for(unsigned i=0;i<20;++i)m.b(record+i,m.b(base+i));
                m.b(record+1,x);m.b(record+2,z);m.b(record+7,0);m.b(record+8,0);
                const auto enemy=s.enemies->spawnSurvivalEnemy(index);
                if(!enemy)return false;
                // The Queens keep their original speed; health 1.75x the original (was 2x: 12.5% lower).
                const int health=m.s(enemy+0x58)*7/4;
                m.w(enemy+0x58,health);
                v.queenSupplies.emplace(enemy,ExplorerSurvivalState::QueenSupplies{health});
                s.enemies->activateSurvivalQueen(enemy);
                populated.push_back(cell);
                if(populated.size()==6)break;
            }
            if(populated.size()!=6)return false;
            {   // face the nearest Queen (the candidates are sorted by distance from the start)
                const double dx=(double(populated[1].first)-double(start.first))*512,dz=(double(populated[1].second)-double(start.second))*512;
                s.yaw=int(std::lround(std::atan2(-dx,dz)*4096.0/6.283185307179586))&4095;
            }
            v.wave=1;v.alive=5;
            s.notice=L"QUEEN'S REVENGE!";s.noticeTicks=150;
            return true;
        }
        if (v.cfg.queen && !explorerSurvivalPlaceQueen(s)) return false;
        if(v.queenEntity){
            const double dx=m.s(v.queenEntity)-s.player.position[0];
            const double dz=m.s(v.queenEntity+4)-s.player.position[2];
            s.yaw=int(std::lround(std::atan2(-dx,dz)*4096.0/6.283185307179586))&4095;
        }
        v.breakTicks = 6 * kTicks;
        explorerSurvivalRestock(s,true);
        s.notice = (v.cfg.name == L"Survival" ? std::wstring(L"SURVIVAL") : v.cfg.name) + L" - " + std::to_wstring(v.cfg.waves) +
                   L" WAVES, " + std::to_wstring(v.lives) + L" LIVES";
    }
    else { s.notice = v.cfg.name + L" - KILL THEM ALL"; }
    s.noticeTicks = 150;
    return true;
}

template <class Scene>
void explorerSurvivalTick(Scene& s)
{
    using namespace explorer_survival;
    auto& v = g_survival;
    if (!v.active || !s.enemies || s.menu.open || s.hasMovies()) { return; }
    if (v.setup && !explorerSurvivalSetup(s)) { return; }
    if(!v.cfg.queensRevenge())s.menu.achievements.survival(s.menu.survivalArena,v,v.ended&&v.victory,s.coopGame);
    if (v.ended)
    {
        if (v.endTicks && !--v.endTicks) { explorerSurvivalFinish(s, true); }
        return;
    }
    ++v.ticks;
    if(v.introTicks){
        --v.introTicks;
        if(v.breakTicks)--v.breakTicks;
        return;
    }
    if(v.cfg.queensRevenge()){
        constexpr unsigned firstAlarm=kIntroBlackTicks+kIntroFadeTicks+5*kTicks;
        if(v.ticks>=firstAlarm){
            const auto burstTick=(v.ticks-firstAlarm)%(25*kTicks);
            if(burstTick<9*kTicks&&burstTick%(3*kTicks)==0){s.audio->play(ExplorerAudio::SupplyAlarm);if(s.coopSound)s.coopSound(ExplorerAudio::SupplyAlarm,false);}
        }
    }
    if (v.cfg.survival() && v.ticks % kResupplyTicks == 0) explorerSurvivalRestock(s);
    auto m = s.mem();
    const auto& cfg = v.cfg;
    const unsigned doors = unsigned(std::min(cfg.doors.size(), v.doorEntry.size()));
    const int ox = m.w(0xa0c34), oz = m.w(0xa0c36);
    std::vector<std::vector<bool>> occupied(doors);
    for (unsigned d = 0; d < doors; ++d) { occupied[d].assign(cfg.doors[d].room.size(), false); }
    std::vector<unsigned> inRoom(doors, 0);     // living enemies in a door's room or doorway
    std::vector<bool> playerInside(doors, false);
    // Which spawn door's room or doorway a cell belongs to (slot in the room, or the room's size for the doorway).
    auto where = [&](unsigned cx, unsigned cz, unsigned& door, unsigned& slot) {
        for (unsigned d = 0; d < doors; ++d)
        {
            const auto& a = cfg.doors[d];
            for (unsigned j = 0; j < a.room.size(); ++j) { if (a.room[j].first == cx && a.room[j].second == cz) { door = d; slot = j; return true; } }
            for (int k = -1; k <= 2; ++k)
            {
                const unsigned x = a.outX ? a.x : unsigned(int(a.x) + k), z = a.outX ? unsigned(int(a.z) + k) : a.z;
                if (x == cx && z == cz) { door = d; slot = unsigned(a.room.size()); return true; }
            }
        }
        return false;
    };
    unsigned alive = 0;
    bool queenAlive = false;
    for (auto e : s.enemies->active())
    {
        if (e == v.queenEntity && !v.queenOut) continue;
        if(auto reward=v.queenSupplies.find(e);reward!=v.queenSupplies.end()){
            auto& supplies=reward->second;
            const int health=std::max(0,int(m.s(e+0x58)));
            supplies.damage+=unsigned(std::max(0,supplies.health-health));
            supplies.health=health;
            // (Supplies come from Queens killed, two at a time: explorerQueenRevengeSupplies, below.)
        }
        if (m.s(e + 0x58) > 0)
        {
            ++alive;
            if (m.b(e + 0x74) == 7) { queenAlive = true; }
            unsigned d = 0, j = 0;
            if (where(unsigned(m.s(e) + ox) >> 9, unsigned(m.s(e + 4) + oz) >> 9, d, j))
            {
                ++inRoom[d];
                if (j < occupied[d].size()) { occupied[d][j] = true; }
            }
        }
        else if (v.corpses.emplace(e, v.wave).second)
        {
            constexpr unsigned points[]={0,25,50,75,100,125,150,1000,150,125,25,100,125,200,175};
            const unsigned kind=m.b(e+0x74);
            if(kind>0&&kind<std::size(points)){
                ++v.scoredKills;
                v.baseScore+=points[kind];
            }
            // Queen's Revenge!: every second Queen killed (the 2nd and 4th; the 5th ends the run) drops supplies where she fell.
            if(cfg.queensRevenge()&&kind==7&&++v.queensKilled%2==0)
                explorerQueenRevengeSupplies(s,unsigned(m.s(e)+ox)>>9,unsigned(m.s(e+4)+oz)>>9);
            if(!cfg.survival()||cfg.queensRevenge()||trilogy::original::nextGameRandom(m)%5!=0)continue;
            const unsigned x = unsigned(m.s(e) + ox) >> 9;
            const unsigned z = unsigned(m.s(e + 4) + oz) >> 9;
            unsigned door = 0, slot = 0;
            const std::pair<unsigned, unsigned> location{x, z};
            auto contains = [&](const auto& cells) {
                return std::find(cells.begin(), cells.end(), location) != cells.end();
            };
            if (x < cfg.width && z < cfg.height && !where(x, z, door, slot) &&
                !contains(cfg.ammo) && !contains(cfg.health) &&
                !contains(cfg.reserveAmmo) && !contains(cfg.supply))
            {
                const unsigned cell = m.d(0xa0c40) + (z * cfg.width + x) * 16;
                // Keep enemy loot on reachable floor. Leave room in the pool
                // for the fixed ammo and health caches to replenish.
                if (!(m.b(cell + 7) & 128) && m.b(cell + 10) < 7)
                {
                    const unsigned drop = v.drops++;
                    const Item item = drop % 3 == 2 ? Item{20, 35, 0}
                        : kAmmo[std::min(4u, s.hud.selected())];
                    explorerSurvivalDrop(s, item, x, z, 72);
                }
            }
        }
    }
    v.alive = alive;
    if (!cfg.survival())   // Exterminate: done when nothing is left alive
    {
        if (!alive && v.ticks > kTicks)
        {
            v.ended = v.victory = true;
            v.endTicks = 3 * kTicks;
            s.notice = L"LEVEL CLEARED";
            s.noticeTicks = 150;
        }
        return;
    }
    {
        unsigned d = 0, j = 0;
        if (where(unsigned(s.player.position[0] + ox) >> 9, unsigned(s.player.position[2] + oz) >> 9, d, j)) { playerInside[d] = true; }
    }
    // Spawn doors: open while they have enemies to let out (or the player is in there), shut once everyone is through.
    for (unsigned d = 0; d < doors; ++d)
    {
        bool want = !cfg.queensRevenge()&&((v.wave&&!v.breakTicks)||inRoom[d]>0||playerInside[d]);
        for (const auto& p : v.queue) { if (p.door == d) { want = true; break; } }
        auto& door = s.doors.entries[v.doorEntry[d]];
        if (want) { door.target = true; door.remaining = std::max(door.remaining, 30u); }
        else {
            door.remaining = 0;
            if(cfg.queensRevenge())door.target=false;
        }
    }
    if (v.breakTicks)
    {
        if (!--v.breakTicks) { explorerSurvivalWave(s, v.wave + 1); }
        return;
    }
    if (!v.wave) { return; }
    if (v.spawnTicks) { --v.spawnTicks; }
    const unsigned cap = std::min(30u, 14u + v.wave * 2u);
    if (!v.spawnTicks && !v.queue.empty() && alive < cap)
    {
        std::vector<bool> spawned(doors);
        for (std::size_t i = 0; i < v.queue.size(); )
        {
            const auto p = v.queue[i];
            if (alive>=cap)break;
            if (p.door >= doors || inRoom[p.door] >= kRoomLimit || spawned[p.door] ||
                s.doors.entries[v.doorEntry[p.door]].amount<=.85f) { ++i; continue; }
            int slot = -1;   // the room is listed back to front: its first free cell
            for (unsigned j = 0; j < occupied[p.door].size(); ++j) { if (!occupied[p.door][j]) {
                const auto& candidate = cfg.doors[p.door].room[j];
                const unsigned cell = m.d(0xa0c40) + (candidate.second * cfg.width + candidate.first) * 16;
                if (!m.b(cell + 12)) { slot = int(j); break; }
            } }
            if (slot < 0) { ++i; continue; }
            const auto& door = cfg.doors[p.door];
            const auto& cell = door.room[unsigned(slot)];
            if (explorerSurvivalSpawn(s, p.kind, p.health, cell.first, cell.second, facingInto(door.outX, door.outZ)))
            {
                v.queue.erase(v.queue.begin() + std::ptrdiff_t(i));
                spawned[p.door]=true;
                v.alive = ++alive;
            }
            else {
                explorerSurvivalClearCorpses(s,0,8);
                break;
            }
        }
        v.spawnTicks = 8;
    }
    const bool last = v.wave >= cfg.waves;
    if (v.queue.empty() && !alive && !(cfg.queen && last))
    {
        if (last)
        {
            v.ended = v.victory = true;
            v.endTicks = 3 * kTicks;
            s.notice = cfg.queensRevenge()?L"ALL FIVE QUEENS DEFEATED":L"EVERY WAVE SURVIVED";
            s.noticeTicks = 150;
        }
        else { v.wavesSurvived=v.wave; explorerSurvivalCleared(s); }
    }
    else if (cfg.queen && last && v.queenOut && !queenAlive && v.queue.empty() && !alive)
    {
        v.ended = v.victory = true;
        v.endTicks = 4 * kTicks;
        s.notice = L"THE QUEEN IS DEAD";
        s.noticeTicks = 150;
    }
}

template <class Scene, class DC>
void explorerSurvivalHud(Scene& s, DC dc, int, int height)
{
    const auto& v = g_survival;
    if (!v.active || v.setup) { return; }
    std::wstring text;
    if (v.ended) { text = v.cfg.survival() ? L"THE HIVE IS SILENT" : L"ALL CLEAR"; }
    else if (!v.cfg.survival()) { text = L"ENEMIES LEFT " + std::to_wstring(v.alive); }
    else if (v.breakTicks) { text = L"NEXT WAVE IN " + std::to_wstring((v.breakTicks + 29) / 30); }
    else
    {
        text = L"WAVE " + std::to_wstring(v.wave) + L"/" + std::to_wstring(v.cfg.waves) + L"   ENEMIES " +
               std::to_wstring(v.queue.size() + v.alive);
    }
    text += L"   LIVES " + std::to_wstring(v.lives);
    text+=L"   SCORE "+std::to_wstring(survivalScore(v.skulls,v.baseScore));
    if(survivalScore(v.skulls,1)==0)text+=L" (UNSCORED)";
    s.textFont.shadow(dc, text, 12, int(height * .13), height / 600.);
}
