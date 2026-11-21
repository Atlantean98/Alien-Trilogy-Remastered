#pragma once
// Playable-host integration. Navigation/display bridges here are provisional,
// separate from the original routines in the recovered library.
#include "EnemyAI.h"
#include "WorldMovement.h"
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
struct ExplorerSprite {unsigned width{},height{};int left{},top{};std::vector<unsigned> pixels;};
class ExplorerEnemies {
public:
    ExplorerEnemies(trilogy::original::PlayerControlMemory,const std::filesystem::path&);
    void initialize(unsigned difficulty=1);
    void crateEnemy(unsigned object,unsigned index);
    unsigned spawnSurvivalEnemy(unsigned index);
    void perchSurvivalQueen(unsigned entity);
    void releaseSurvivalQueen();
    void activateSurvivalQueen(unsigned entity);
    void killWithTrap(unsigned entity);
    void addExterminationAlly(unsigned entity);
    void healExterminationSquad();
    void moveExterminationSquad(const trilogy::original::Vec3s& player);   // the squad rides the elevator too
    const std::map<unsigned,unsigned>& exterminationSquad()const{return squadHealth;}
    unsigned squadAttacksAbsorbed=0;   // alien attacks that reached an ally
    // The marines' pulse rounds, drawn as the player's are (a short bright streak flying from the gun to the alien;
    // the hit itself lands as the shot is fired). age: ticks since the shot.
    struct SquadRound {trilogy::original::Vec3s from,to;unsigned age=0;};
    static constexpr double SquadRoundSpeed=768,SquadRoundLength=384;
    std::vector<SquadRound> squadRounds;
    unsigned squadShots=0;
    bool exterminationDowned(unsigned entity)const{return squadDowned.contains(entity);}
    bool reviveExterminationAlly(unsigned entity);   // a downed marine back up with a third of their health
    void preloadSurvivalEnemy(unsigned kind);
    void retireSurvivalCorpse(unsigned entity);
    void blast(unsigned position,unsigned cell);
    void trigger(unsigned delta,unsigned index);
    struct AttachmentState {unsigned entity{};int screenY=340;std::array<unsigned,2> programs{};};
    AttachmentState attachmentState()const{return {attachmentEntity,attachmentScreenY,attachmentPrograms};}void attachmentState(const AttachmentState& value){attachmentEntity=value.entity;attachmentScreenY=value.screenY;attachmentPrograms=value.programs;}
    std::function<void(unsigned)> coopTarget,coopEmitted;
    std::function<void(unsigned,bool)> coopProjectile;
    void tick(const trilogy::original::Vec3s&,unsigned health);
    unsigned health() const;
    void terrainDamage(unsigned amount,unsigned type,bool healthOnly=false);
    unsigned ventEmissions{},hazardHits{},ventsDisabled{};
    std::function<void(unsigned,unsigned,float,float,bool)> ambient;
    std::function<void(const std::wstring&)> hazardNotice;
    bool attached()const{return attachmentEntity!=0;}
    bool isAttachedEntity(unsigned e)const{return attachmentEntity==e;}
    int attachmentY()const{return attachmentScreenY;}
    const ExplorerSprite& attachmentSprite(unsigned half);
    unsigned attachments{},completedAttachments{};
    std::vector<unsigned> active() const;
    const ExplorerSprite& sprite(unsigned entity);
    bool throwGrenade(const trilogy::original::Vec3s&,int yaw,int pitch,bool pulse,short movement=0,bool rocket=false);
    std::function<void(int,int,int)> explosion;
    bool fireWeapon(const trilogy::original::Vec3s&,int yaw,int pitch,unsigned weapon,short movement=0);
    void tickProjectiles();
    void corpseContact(const trilogy::original::Vec3s&,unsigned);
    bool shoot(const trilogy::original::Vec3s&,int yaw,int pitch,double focal);
    std::function<void(unsigned,unsigned)> sound;
    std::function<void(unsigned)> droppedItem,projectileAction,levelAction;
    unsigned survivalSkulls=0, ammoUsed=0, accurateShots=0;
    unsigned flameKills{};
    unsigned shots{},hits{},kills{},movingTicks{},attackTicks{},decodedFrames{},unknownServices{},ledgeLeaps{},acidHits{};
    std::map<unsigned,unsigned> effects;
private:
    std::map<unsigned,unsigned> squadHealth, squadCooldown;
    // Extermination: ticks of the firing pose / of the pain flash per ally; the target each alien chose (0 = the
    // player) and its countdown to choosing again; the ally the alien being run is after (0 = the player).
    std::map<unsigned,unsigned> squadFiring, squadHurt, squadChoice, squadRetarget;
    std::set<unsigned> squadDowned;
    std::map<unsigned,std::array<int,2>> squadLast;std::map<unsigned,unsigned> squadStuck;   // getting a marine unstuck
    bool squadCatchUp(unsigned entity,const trilogy::original::Vec3s& player,int nearest=2);
    void releaseSquadCell(unsigned entity);
    unsigned squadVictim=0;
    void tickExterminationAlly(unsigned entity,const trilogy::original::Vec3s& position);
    unsigned exterminationVictim(unsigned alien,const trilogy::original::Vec3s& player,unsigned playerHealth);
    using Mem=trilogy::original::PlayerControlMemory;
    Mem m;std::filesystem::path root;trilogy::original::PlayerControlService services,survivalServices;
    std::array<short,4096> sine{};unsigned ticks{},fireCooldown{};
    std::map<std::string,std::vector<std::vector<unsigned char>>> bundles;
    std::map<unsigned,unsigned> sectionForGroup;
    std::map<unsigned,bool> rawForGroup;
    std::map<std::string,std::vector<unsigned>> bundlePalettes;
    std::map<unsigned,ExplorerSprite> sprites;std::map<unsigned,unsigned> ventOriginCells;
    unsigned perchedQueen{};
    std::map<unsigned,unsigned> survivalShotProjectiles;
    std::set<unsigned> survivalShotsHit;
    void applySurvivalSpawnModifiers(unsigned entity);
    unsigned survivalShotSerial=0;
    bool explosiveDamage=false, enemyGrenadeDamage=false;
    bool playerGrenadeDamage=false;
    std::vector<std::pair<unsigned,unsigned>> pendingChainBlasts;
    void flushChainBlasts();
    bool survivalMovement=false;
    std::array<unsigned char,128> stalledTicks{};
    unsigned attachmentEntity{};int attachmentScreenY=340;
    std::array<unsigned,2> attachmentPrograms{};
    void tickVent(unsigned task);void emitVent(unsigned entity,unsigned kind,int speed);
    bool attach(unsigned);void tickAttachment(unsigned);
    const ExplorerSprite& frameSprite(unsigned,unsigned,unsigned,unsigned);
    bool sight(unsigned,int,unsigned,bool) const;
    bool clearSegment(unsigned,int,int,int,int,bool) const;
    bool navigate(unsigned,bool recovering=false);
    trilogy::original::PlayerControlResult external(Mem&,unsigned,unsigned,unsigned,unsigned);
    void loadBundle(const std::string&);
};
