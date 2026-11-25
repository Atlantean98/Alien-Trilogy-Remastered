#include "ExplorerVersion.h"
#include "ExplorerDiagnostics.h"
#include "ExplorerReplay.h"
#include "ExplorerPs1Memory.h"
#include "ExplorerCoopMarker.h"
#include "ExplorerCoop.h"
#include "ExplorerCoopRules.h"
#include "ExplorerDepthOfField.h"
#include "ExplorerPhotoFilters.h"
#include "ExplorerPerformance.h"
#include "ExplorerMovie.h"
#include <deque>
#include "ExplorerCheckpoint.h"
#include <SDL3/SDL.h>
#include "ExplorerController.h"
#include "ExplorerPassword.h"
#include "ExplorerSurvival.h"
#include "ExplorerTextureAnimation.h"
#include "ExplorerProjectileVisuals.h"
#include "ExplorerDisplay.h"
#include "ExplorerFog.h"
#include "ExplorerGpu.h"
// Interactive integration harness. The display/input layer is provisional;
// recovered generic enemy AI/movement/animation run through explicit host bridges.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>
#include <bit>
#include "LevelLoading.h"
#include "EnemyRuntime.h"
#include "EnemyMovement.h"
#include "ExplorerEnemies.h"
#include "ExplorerAssets.h"
#include "ExplorerAudio.h"
#include "ExplorerHud.h"
#include "ExplorerLogic.h"
#include "ExplorerLighting.h"
#include "ExplorerPickups.h"
#include "ExplorerObjects.h"
#include "ExplorerMission.h"
#include "ExplorerMenu.h"
#include "PickupRuntime.h"
#include "CombatRuntime.h"
#include "WeaponRuntime.h"
#include "ProjectileRuntime.h"
#include <memory>
#include <set>
#include "WorldMovement.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace trilogy::original;
namespace fs=std::filesystem;
namespace {
constexpr double Pi=3.14159265358979323846;
std::vector<unsigned char> read(const fs::path& path){std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)throw std::runtime_error("Cannot open "+path.string());auto n=f.tellg();if(n<0)throw std::runtime_error("Cannot size input");std::vector<unsigned char> b(static_cast<size_t>(n));f.seekg(0);if(!f.read(reinterpret_cast<char*>(b.data()),n))throw std::runtime_error("Cannot read input");return b;}
struct Face {std::array<int,4> vertices;unsigned light;unsigned texture=0,flags=0;bool model=false,survivalSac=false;unsigned atlas=0,sourceFace=0xffffffffu;};
struct Point {double x,y,z;double u=0,v=0;std::array<double,3> color{128,128,128};};
struct Mesh {std::vector<Point> vertices;std::vector<Face> faces;};
Mesh decodeMesh(const std::vector<unsigned char>& data){
    auto word=[&](size_t p){if(p+2>data.size())throw std::runtime_error("Truncated door mesh");return unsigned(data[p])|(unsigned(data[p+1])<<8);};
    auto dword=[&](size_t p){return word(p)|(word(p+2)<<16);};
    if(data.size()<20||std::string(data.begin(),data.begin()+4)!="OBJ1")throw std::runtime_error("Invalid door model header");
    unsigned fc=dword(12),vc=dword(16);if(20ull+fc*20ull+vc*8ull!=data.size())throw std::runtime_error("Invalid door model extent");
    Mesh mesh;for(unsigned i=0;i<vc;++i){auto p=20+fc*20+i*8;mesh.vertices.push_back({double(static_cast<short>(word(p))),double(static_cast<short>(word(p+2))),double(static_cast<short>(word(p+4)))});}
    for(unsigned i=0;i<fc;++i){auto p=20+i*20;Face f{};f.model=true;f.texture=word(p+16);f.flags=data[p+18]&127;f.light=data[p+19]&127;for(unsigned j=0;j<4;++j){auto n=std::bit_cast<int>(dword(p+j*4));if(n<0&&j==3)f.vertices[j]=-1;else{if(n<0||unsigned(n)>=vc)throw std::runtime_error("Door vertex outside model");f.vertices[j]=n;}}mesh.faces.push_back(f);}
    return mesh;
}
// The sac is part of the PS1 Queen's Lair map, separate from her sprite.
constexpr double SurvivalSacAngle=3*Pi/4;
constexpr int ExterminationLevel2Track=20;   // GameData/PS1/Music/track20.wav
Mesh loadSurvivalQueenSac(const fs::path& lair){
    const auto chunks=explorerChunks(lair);
    const auto& map=chunks.at("MAP0");
    auto word=[&](size_t p){return unsigned(map.at(p))|(unsigned(map.at(p+1))<<8);};
    auto signedWord=[&](size_t p){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word(p)));};
    auto dword=[&](size_t p){return word(p)|(word(p+2)<<16);};
    const unsigned vertexCount=word(0),faceCount=word(2),width=word(4),height=word(6);
    const size_t faceStart=36+vertexCount*8ull;
    const size_t enemies=faceStart+faceCount*20ull+width*height*16ull+map.at(12)*8ull;
    if(map.at(enemies)!=7)throw std::runtime_error("Queen missing from PS1 lair");
    const int queenX=int(map.at(enemies+1))*512+256-int(width)*256;
    const int queenZ=int(map.at(enemies+2))*512+256-int(height)*256;
    // SLUS-00007 L162 MAP0 stores the complete sac as a separate vertex block.
    constexpr unsigned sacVertexStart=4773, sacVertexEnd=5425, sacFaceCount=632;
    if(vertexCount<sacVertexEnd)throw std::runtime_error("Egg sac vertices missing from PS1 lair");
    Mesh sac;
    std::vector<int> remapped(vertexCount,-1);
    for(unsigned i=0;i+1<faceCount;++i){
        const auto p=faceStart+i*20;
        std::array<unsigned,4> indices{};
        for(unsigned j=0;j<indices.size();++j)indices[j]=dword(p+j*4);
        const auto corners=(indices[3]&0x80000000u)?3:4;
        if(!std::all_of(indices.begin(),indices.begin()+corners,[](unsigned index){
            return index>=sacVertexStart&&index<sacVertexEnd;
        }))continue;
        Face face{};
        face.texture=word(p+16);
        face.flags=map.at(p+18)&127;
        face.light=map.at(p+19)&127;
        face.sourceFace=i;
        face.survivalSac=true;
        face.vertices.fill(-1);
        for(int j=0;j<corners;++j){
            const auto index=indices[j];
            auto& mapped=remapped.at(index);
            if(mapped<0){
                mapped=int(sac.vertices.size());
                const auto v=36+index*8ull;
                // Keep the original proportions and scale about the attachment point.
                constexpr double scale=.85, attachmentHeight=768;
                sac.vertices.push_back({(signedWord(v)-queenX)*scale,
                                        attachmentHeight+(signedWord(v+2)-attachmentHeight)*scale,
                                        (signedWord(v+4)-queenZ)*scale});
            }
            face.vertices[j]=mapped;
        }
        sac.faces.push_back(face);
    }
    if(sac.faces.size()!=sacFaceCount)throw std::runtime_error("Incomplete egg sac geometry in PS1 lair");
    return sac;
}
Point cameraPoint(Point point,const Vec3s& position,int yaw,int pitch=0,int roll=0){
    double angle=yaw*2*Pi/4096,cs=std::cos(angle),sn=std::sin(angle);
    double x=point.x-position[0],z=point.z-position[2];
    // Original camera roll is 2048: screen-right is negative world X at
    // yaw zero. Match that handedness without reflecting the minimap.
    double y=point.y-position[1]-768,depth=-x*sn+z*cs,p=pitch*2*Pi/4096;
    double cx=-(x*cs+z*sn),cy=y*std::cos(p)-depth*std::sin(p),cz=y*std::sin(p)+depth*std::cos(p);
    if(roll){
        double r=roll*2*Pi/4096,rc=std::cos(r),rs=std::sin(r);
        return {cx*rc-cy*rs,cx*rs+cy*rc,cz};
    }
    return {cx,cy,cz};
}
Point cameraPoint(Point point,const std::array<double,3>& position,int yaw,int pitch,int roll){
    double angle=yaw*2*Pi/4096,cs=std::cos(angle),sn=std::sin(angle);
    double x=point.x-position[0],z=point.z-position[2];
    double y=point.y-position[1]-768,depth=-x*sn+z*cs,p=pitch*2*Pi/4096;
    double cx=-(x*cs+z*sn),cy=y*std::cos(p)-depth*std::sin(p),cz=y*std::sin(p)+depth*std::cos(p);
    if(roll){
        double r=roll*2*Pi/4096,rc=std::cos(r),rs=std::sin(r);
        return {cx*rc-cy*rs,cx*rs+cy*rc,cz};
    }
    return {cx,cy,cz};
}
double renderClock(){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return double(t.QuadPart)*1000/f.QuadPart;}
std::unique_ptr<ExplorerGpu> gpuRenderer;
struct Scene {
    unsigned isolationStage=0,isolationTerminal=0,isolationExit=0;
    // container: 0 box, 1 locker, 2 badge - props of ours against the wall on side 1-4 (-z, +z, -x, +x) - or 9/10:
    // inside the map's own crate/locker, a hidden pickup it drops when broken open, or 11: a tag pickup lying on the
    // floor (tools/build_extermination_decks.py).
    struct IsolationTag {unsigned cell,container;bool collected=false;unsigned side=0;};
    std::array<unsigned,3> exterminationLocker{157,154,155};   // props.txt: the locker's front, side and top texture
    // A deck of two campaign maps in one grid (tools/stitch_maps.py): parts.bin says which map each cell is in (1, 2);
    // the far end of the first map's exit elevator room is the start of the second map's elevator room (stitch.txt:
    // x z facing, then its name): the squad goes straight on once that first area is done. (The two rooms cannot be
    // one cell-for-cell: no layout of the two maps shares them inside the engine's 127 x 127 grid.)
    std::vector<unsigned char> exterminationParts;
    unsigned exterminationArrival=~0u,exterminationArrivalFacing=0,arriveTicks=0;
    unsigned exterminationReturn=~0u,exterminationReturnFacing=0;   // back to the first map, beside its exit room
    std::wstring exterminationSecondTitle,exterminationFirstTitle;
    bool exterminationRiding=false;unsigned queenDownTicks=0;
    unsigned exterminationPart(unsigned cell)const{return cell<exterminationParts.size()?exterminationParts[cell]:0;}
    struct IsolationPanel {unsigned cell,door;};
    std::vector<IsolationTag> isolationTags,isolationFixtures;
    std::vector<IsolationPanel> isolationPanels;
    bool isolationPowered=false, exterminationSmartgunPlaced=false;
    bool isolationTagsComplete()const{
        return std::all_of(isolationTags.begin(),isolationTags.end(),[](const auto& tag){return tag.collected;});
    }
    unsigned isolationQueen=0;
    void startIsolation(unsigned stage){
        const auto folder=root/"GameData"/"Isolation"/std::to_string(stage);
        std::ifstream objectives(folder/"objectives.txt");
        std::string title;
        unsigned tx,tz,ex,ez;
        if(!std::getline(objectives,title)||!(objectives>>tx>>tz>>ex>>ez))
            throw std::runtime_error("Extermination mission data is missing");
        // The deck is a remix of a campaign map (tools/build_extermination_decks.py): its own L<code>LEV.MAP and GFX.
        level.clear();
        for(const auto& entry:fs::directory_iterator(folder)){
            const auto name=entry.path().filename().string();
            if(name.size()==11&&name[0]=='L'&&name.ends_with("LEV.MAP"))level=entry.path();
        }
        if(level.empty())throw std::runtime_error("Extermination deck map is missing");
        isolationStage=stage;isolationPowered=exterminationSmartgunPlaced=false;isolationTags.clear();isolationFixtures.clear();isolationPanels.clear();isolationQueen=0;difficulty=1;
        try{load(true);}catch(...){isolationStage=0;throw;}
        // 255: no such place (the terminal only exists on deck 3; extraction is the level's own exit).
        if((tx!=255&&(tx>=grid.width||tz>=grid.height))||(ex!=255&&(ex>=grid.width||ez>=grid.height))){
            isolationStage=0;throw std::runtime_error("Extermination objective lies outside the map");
        }
        isolationTerminal=tx==255?~0u:tz*grid.width+tx;isolationExit=ex==255?~0u:ez*grid.width+ex;
        const auto active=enemies->active();
        auto m=mem();
        for(auto actor:active){
            const auto kind=m.b(actor+0x74);
            if(kind==13)enemies->addExterminationAlly(actor);
            else if(kind>=1&&kind<=6){
                // Tougher and more aggressive than the campaign: a bit more health (Dogs and Warriors x1.75, the rest
                // x1.3), 12% faster, the quickest reactions Survival uses, and they notice the squad from 60% further.
                m.w(actor+0x58,unsigned(std::min(32767,m.s(actor+0x58)*(kind==5||kind==6?7:13)/(kind==5||kind==6?4:10))));
                m.w(actor+0x4e,0);
                if(kind>=2){
                    m.w(actor+0x5e,(m.w(actor+0x5e)*112+50)/100);
                    m.w(actor+0x52,8);
                    m.w(actor+0x50,unsigned(std::min(32767,m.s(actor+0x50)*8/5)));
                }
            }
        }
        if(enemies->exterminationSquad().size()!=3)
            throw std::runtime_error("Extermination requires three squad members");
        unsigned x,z,container;
        while(objectives>>x>>z>>container){
            const unsigned side=container>>4;container&=15;
            if(x>=grid.width||z>=grid.height||container>11||side>4)
                throw std::runtime_error("Invalid Isolation tag location");
            if(container==3)continue;
            else if(container>=9){
                if(container!=11&&!exterminationContainer(z*grid.width+x))   // 11: lying on the floor
                    throw std::runtime_error("An Extermination ID tag's crate or locker is missing");
                isolationTags.push_back({z*grid.width+x,container});
            }else if(container>=7){
                const auto door=container-7;
                if(door>=doors.entries.size()||!doors.entries[door].lock)
                    throw std::runtime_error("Invalid Isolation panel link");
                isolationPanels.push_back({z*grid.width+x,door});
            }else if(container>3)isolationFixtures.push_back({z*grid.width+x,container});
            else isolationTags.push_back({z*grid.width+x,container,false,side});
        }
        {std::ifstream props(folder/"props.txt");unsigned front,side,top;if(props>>front>>side>>top)exterminationLocker={front,side,top};}
        exterminationParts.clear();exterminationArrival=exterminationReturn=~0u;exterminationRiding=false;arriveTicks=0;exterminationSecondTitle.clear();exterminationFirstTitle.clear();
        if(std::ifstream stitch(folder/"stitch.txt");stitch){
            unsigned ax,az,facing;std::string name;
            if(!(stitch>>ax>>az>>facing)||ax>=grid.width||az>=grid.height)throw std::runtime_error("Invalid Extermination elevator");
            std::getline(stitch>>std::ws,name);
            std::ifstream parts(folder/"parts.bin",std::ios::binary);
            exterminationParts.assign(std::istreambuf_iterator<char>(parts),{});
            if(exterminationParts.size()!=size_t(grid.width)*grid.height||exterminationPart(az*grid.width+ax)!=2)
                throw std::runtime_error("Extermination deck parts do not match its map");
            exterminationArrival=az*grid.width+ax;exterminationArrivalFacing=facing&7;
            exterminationSecondTitle.assign(name.begin(),name.end());
            unsigned bx,bz,backFacing;std::string first;
            if(stitch>>bx>>bz>>backFacing&&bx<grid.width&&bz<grid.height&&exterminationPart(bz*grid.width+bx)==1){
                std::getline(stitch>>std::ws,first);
                exterminationReturn=bz*grid.width+bx;exterminationReturnFacing=backFacing&7;
                exterminationFirstTitle.assign(first.begin(),first.end());
            }
        }
        if(isolationTags.empty()&&stage!=4)throw std::runtime_error("Isolation has no ID tags");
        // The props drawn on cells (tag containers, furniture, wing panels) are solid: byte 7 bit 0x80, as
        // WorldMovement's collide and the enemies read it (closed doors use it too). Tags and panels are used
        // from the next cell.
        auto solid=[&](unsigned cell){const auto at=m.d(0xa0c40)+cell*16;m.b(at+7,m.b(at+7)|0x80);cells[cell].bytes[7]|=0x80;};
        for(const auto& tag:isolationTags)if(tag.container<9)solid(tag.cell);   // a crate or locker blocks its cell itself
        for(const auto& fixture:isolationFixtures)solid(fixture.cell);
        for(const auto& panel:isolationPanels)solid(panel.cell);
        // The map's own consoles and switch panels (logic.panels): solid too - the object system leaves them to the
        // switch logic, which never claims their cell. Used from the next cell, as before.
        for(const auto& panel:logic.panels)solid((panel.cell-m.d(0xa0c40))/16);
        std::array<unsigned char,50> equipment{};equipment[4]=100;
        inventory(equipment);awardPickup(m,2,99,4);
        m.w(0xa0ac8,std::max(4u,unsigned(m.w(0xa0ac8))));m.w(0xa0ad0,2);   // four pulse grenades and two seismic charges for the walls
        hud.select(2);hud.syncAmmo();
        mission.remaining=0;mission.kinds.clear();mission.title.assign(title.begin(),title.end());
        mission.text=mission.markup=std::wstring(L"Mission brief: ")+mission.title+
            L"\n\nSweep the deck with your squad of Alien Handlers. Kill every Xenomorph - Facehuggers, Chestbursters, Bambis, Dogs and Warriors; eggs can be left - and recover every crew ID tag, then reach the exit elevator. The deck's own doors, lifts and switches stand between you and the tags. Medikits heal you and the whole squad; hold Use on a downed marine to get them back up."+
            (stage==3?std::wstring(L"\n\nOnce Egg Chambers is clear - every alien, every tag - its exit elevator takes the squad down to the Queen's Lair. A Smartgun waits there. Kill the Queen."):std::wstring());
        mission.briefing=stage!=4;menu.open=false;menu.hasGame=true;menu.difficultyLocked=true;
        if(stage==4){   // the finale: the Queen comes for the squad at once; the Smartgun lies just ahead
            isolationQueen=enemies->spawnSurvivalEnemy(1);
            if(!isolationQueen)throw std::runtime_error("Could not release the Queen");
            enemies->preloadSurvivalEnemy(7);enemies->activateSurvivalQueen(isolationQueen);
            isolationPowered=true;
            const double angle=yaw*2*Pi/4096;const int ax=int(std::lround(-std::sin(angle))),az=int(std::lround(std::cos(angle)));
            unsigned gx=unsigned(player.position[0]+player.originX)>>9,gz=unsigned(player.position[2]+player.originZ)>>9;   // where the squad stands
            for(int step=2;step>=1;--step){   // two cells ahead if that is open floor, else one, else at your feet
                const int cx=int(gx)+ax*step,cz=int(gz)+az*step;
                if(cx<0||cz<0||cx>=int(grid.width)||cz>=int(grid.height))continue;
                const auto at=m.d(0xa0c40)+unsigned(cz*int(grid.width)+cx)*16;
                if(int(m.d(at))<0||(m.b(at+7)&0x80)||m.b(at+12)||m.b(at+13)<=m.b(at+11)+32)continue;
                gx=unsigned(cx);gz=unsigned(cz);break;
            }
            pickups.dropSurvivalPickup(4,100,8,gx,gz);
            notice=L"QUEEN'S LAIR - TAKE THE SMARTGUN AND LET RIP";noticeTicks=150;
        }
        menu.exterminationGame=true;menu.exterminationLevel=stage;
    }
    const wchar_t* isolationWing(unsigned door)const{
        static constexpr const wchar_t* names[3][2]={
            {L"CREW ARCHIVES",L"QUARANTINE LOCKERS"},
            {L"RELAY CONTROL",L"COOLANT MONITORING"},
            {L"DEPARTURE LOUNGE",L"SHUTTLE SECURITY"}
        };
        return names[std::clamp(isolationStage,1u,3u)-1][std::min(door,1u)];
    }
    bool isolationNear(unsigned target)const{
        const auto cell=player.currentCell;
        return std::abs(int(target%grid.width)-int(cell%grid.width))+
               std::abs(int(target/grid.width)-int(cell/grid.width))<=1;
    }
    // The sweep: every Facehugger, Chestburster, Bambi, Dog and Warrior dead. Eggs don't count - an egg can be
    // left, and one that hatches later just puts a Facehugger back on the list - so destroying or leaving eggs can
    // never block extraction.
    bool exterminationClear(){
        auto m=mem();
        for(auto entity:enemies->active()){
            const auto kind=m.b(entity+0x74);
            if(kind>=2&&kind<=6&&m.s(entity+0x58)>0&&m.b(entity+0x6f)<7)return false;
        }
        return true;
    }
    // What still stands between the squad and extraction (nullptr: nothing - board the exit elevator).
    const wchar_t* exterminationBlocker(){
        if(!isolationTagsComplete())return L"RECOVER ALL ID TAGS FIRST";
        if(!exterminationClear())return L"CLEAR THE DECK FIRST";
        if(isolationQueen&&mem().s(isolationQueen+0x58)>0)return L"DEFEAT THE QUEEN BEFORE BOARDING";
        return nullptr;
    }
    // The map's own crate or locker on a cell holding an ID tag: its object (0x320000 + i*40), else 0.
    unsigned exterminationContainer(unsigned cell){
        auto m=mem();
        const auto at=m.d(0xa0c40)+cell*16;
        for(auto i:objects.entries){
            const unsigned p=0x320000+i*40,kind=m.b(p+24);
            if((kind!=20&&kind!=25)||m.d(p+20)!=at)continue;
            if(exterminationTagSlot(p)!=255)return p;
        }
        return 0;
    }
    // The pickup record of the tag inside container p (its byte 7 contents, or byte 6 when not an enemy), else 255.
    unsigned exterminationTagSlot(unsigned p){
        auto m=mem();
        for(unsigned at:{p+29,p+28}){
            const unsigned index=m.b(at);
            if(index==255||(at==p+28&&(m.b(p+25)&2))||index>=m.w(0x236c4c))continue;
            if(m.b(m.d(0x236c34)+index*8+2)==15)return index;
        }
        return 255;
    }
    // A tag a crate or locker dropped was picked up (pickups.lastKind 15): the nearest such tag is recovered.
    void exterminationTagFound(){
        IsolationTag* found=nullptr;double best=1e30;
        const unsigned px=unsigned(pickups.lastX+player.originX)>>9,pz=unsigned(pickups.lastZ+player.originZ)>>9;
        const auto here=px<grid.width&&pz<grid.height?exterminationPart(pz*grid.width+px):0;
        // The nearest uncollected tag to where it was picked up: in that map; if it came to rest on a cell of neither (a
        // crate's drop flies a little), the nearest within three cells.
        for(unsigned pass=0;pass<2&&!found;++pass)for(auto& tag:isolationTags){
            if(tag.collected||tag.container<9||(pass==0&&exterminationPart(tag.cell)!=here))continue;
            const double dx=double(tag.cell%grid.width)*512+256-player.originX-pickups.lastX;
            const double dz=double(tag.cell/grid.width)*512+256-player.originZ-pickups.lastZ;
            const double d=dx*dx+dz*dz;
            if(d<best&&(pass==0||d<=1536.*1536.)){best=d;found=&tag;}
        }
        if(!found)return;
        found->collected=true;audio->playMenu(1);
        notice=L"CREW ID TAG RECOVERED";noticeTicks=90;
    }
    // Before going on: this first area's tags and aliens (a stranded tag or alien would block extraction).
    const wchar_t* exterminationRideBlocker(){
        for(const auto& tag:isolationTags)if(!tag.collected&&exterminationPart(tag.cell)==1)return L"RECOVER THIS AREA'S ID TAGS FIRST";
        auto m=mem();
        for(auto entity:enemies->active()){
            const auto kind=m.b(entity+0x74);
            if(kind<2||kind>6||m.s(entity+0x58)<=0||m.b(entity+0x6f)>=7)continue;
            const unsigned x=unsigned(m.s(entity)+player.originX)>>9,z=unsigned(m.s(entity+4)+player.originZ)>>9;
            if(x<grid.width&&z<grid.height&&exterminationPart(z*grid.width+x)==1)return L"CLEAR THIS AREA FIRST";
        }
        return nullptr;
    }
    void exterminationArrive(bool back=false){   // into the second map's start elevator room (or back beside the first's exit), the squad along
        const unsigned target=back?exterminationReturn:exterminationArrival;
        const int wx=int(target%grid.width)*512+256,wz=int(target/grid.width)*512+256;
        const Vec3s at{short(wx-player.originX),short(floorHeight(grid,wx,wz)),short(wz-player.originZ)};
        player.position=at;previousPosition=at;player.currentCell=grid.index(wx,wz);
        constexpr int facing[]={2048,2560,-1024,-512,0,512,1024,1536};
        yaw=facing[back?exterminationReturnFacing:exterminationArrivalFacing]&4095;pitch=0;acceleration.speed=0;firing=false;
        enemies->moveExterminationSquad(at);
        notice=back?exterminationFirstTitle:exterminationSecondTitle;noticeTicks=150;logic.message=notice;   // what the exit trigger reports
    }
    // Extermination's own save, apart from the story's three slots and co-op: the level reached, written as each
    // level is cleared (0: the run is over, the save goes). The menu's Extermination page offers to continue it.
    fs::path exterminationSavePath()const{return (checkpointDirectory.empty()?menu.root/"decompilation/user/saves":checkpointDirectory)/"extermination.sav";}
    void saveExtermination(unsigned stage){
        std::error_code error;const auto path=exterminationSavePath();
        if(!stage){fs::remove(path,error);return;}
        fs::create_directories(path.parent_path(),error);
        const auto temporary=path.parent_path()/"extermination.sav.tmp";
        {std::ofstream f(temporary,std::ios::trunc);f<<"ATEXTERM 1 "<<stage<<"\n";if(!f)return;}
        fs::rename(temporary,path,error);
    }
    bool isolationUsed=false;
    // Reviving a downed marine: Use held (the main loop sets useHeld every tick) beside their body for 1.5 seconds.
    bool useHeld=false;unsigned reviveTarget=0,reviveTicks=0;
    static constexpr unsigned ReviveTicks=45;
    void tickExterminationRevive(){
        unsigned body=0,member=0,which=0;
        if(useHeld&&player.health)for(const auto& [entity,maximum]:enemies->exterminationSquad()){
            ++member;
            if(!enemies->exterminationDowned(entity))continue;
            auto m=mem();
            if(std::hypot(double(m.s(entity)-player.position[0]),double(m.s(entity+4)-player.position[2]))<=700){body=entity;which=member;break;}
        }
        if(!body){reviveTarget=reviveTicks=0;return;}
        if(body!=reviveTarget){reviveTarget=body;reviveTicks=0;}
        if(++reviveTicks<ReviveTicks){
            notice=L"REVIVING HANDLER "+std::to_wstring(which)+L"  "+std::to_wstring(reviveTicks*100/ReviveTicks)+L"%";noticeTicks=4;
            return;
        }
        enemies->reviveExterminationAlly(body);
        notice=L"HANDLER "+std::to_wstring(which)+L" IS BACK ON THEIR FEET";noticeTicks=90;
        reviveTarget=reviveTicks=0;
    }   // the last useIsolation() acted on a tag, panel or terminal (else the map's own switches get the Use)
    bool exterminationNearArrival()const{
        if(exterminationArrival==~0u||exterminationReturn==~0u||exterminationPart(player.currentCell)!=2)return false;
        const int dx=int(player.currentCell%grid.width)-int(exterminationArrival%grid.width);
        const int dz=int(player.currentCell/grid.width)-int(exterminationArrival/grid.width);
        return std::max(std::abs(dx),std::abs(dz))<=2;
    }
    bool useIsolation(){
        const auto cell=player.currentCell;
        isolationUsed=true;
        if(exterminationNearArrival()){
            // The elevator room: Use works its door and panels first (the same key opens the door ahead); facing nothing
            // to use - the back of the elevator - it takes the squad back the way it came.
            const auto used=logic.use(mem(),doors,player.position[0],player.position[1],player.position[2],yaw);
            if(!used.empty()){notice=used;noticeTicks=60;return false;}
            exterminationArrive(true);return false;
        }
        for(const auto& panel:isolationPanels){
            if(!isolationNear(panel.cell))continue;
            auto& door=doors.entries[panel.door];
            if(!door.target){
                door.target=true;door.remaining=door.hold=0;++doors.opened;
                audio->playAt(42,float(door.x),float(door.z));
            }
            notice=std::wstring(isolationWing(panel.door))+L" - ACCESS GRANTED";
            noticeTicks=90;
            return false;
        }
        for(auto& tag:isolationTags){
            if(tag.container>=9)continue;   // shot out of its crate or locker, then picked up
            const int dx=int(tag.cell%grid.width)-int(cell%grid.width);
            const int dz=int(tag.cell/grid.width)-int(cell/grid.width);
            if(std::abs(dx)+std::abs(dz)>1)continue;
            if(!tag.collected){
                tag.collected=true;awardPickup(mem(),15,1,0);audio->playMenu(1);
                notice=L"CREW ID TAG RECOVERED";noticeTicks=90;return false;
            }
        }
        if(cell==isolationExit){
            if(const auto blocker=exterminationBlocker()){notice=blocker;noticeTicks=90;return false;}
            exitFading=true;exitFadeTicks=0;return true;
        }
        isolationUsed=false;
        return false;
    }
    ExplorerReplay replay;
    std::vector<ExplorerAudio::ReplayCue> replayCues;
    bool replayEnabled=false,replaying=false;
    unsigned replayMode=0;
    int replayPreviousYaw=0,replayPreviousPitch=0;
    fs::path watchedReplay;
    int replayViewYaw()const{
        const int turn=((yaw-replayPreviousYaw+6144)&4095)-2048;
        return (replayPreviousYaw+int(std::lround(turn*renderAlpha)))&4095;
    }
    int replayViewPitch()const{return int(std::lerp(double(replayPreviousPitch),double(pitch),renderAlpha));}
    void advanceReplay(double elapsed,double& clock){
        if(!replaying||replay.paused||photoMode)return;
        clock+=elapsed;
        constexpr double tick=1.0/ExplorerReplay::Hz;
        while(clock>=tick){
            clock-=tick;
            if(!replayFrame()){stopReplay();return;}
        }
        renderAlpha=std::clamp(clock/tick,0.,1.);
    }
    void finishReplayRecording(){replay.finish();replayCues.clear();}
    void recordReplayFrame(){
        if(!replayEnabled||isolationStage||replaying||menu.open||mission.briefing||mission.complete||hasMovies()||photoMode)return;
        try{
            if(!replay.recording.is_open()){
                ExplorerReplay::Writer header;
                replayMode=g_survival.active?(g_survival.cfg.queensRevenge()?(coopGame?5u:3u):(coopGame?4u:1u)):coopGame?2u:0u;
                header.pod(ExplorerReplay::Version);
                header.pod(replayMode);header.pod(menu.survivalArena);header.pod(difficulty);
                header.pod(g_survival.skulls);
                header.text(level.stem().string());
                header.pod(ExplorerReplay::checksum(explorerRead(level)));
                SYSTEMTIME date{};GetLocalTime(&date);
                wchar_t stamp[64];
                swprintf(stamp,64,L"%04u-%02u-%02u %02u-%02u-%02u-%03u",date.wYear,date.wMonth,date.wDay,
                         date.wHour,date.wMinute,date.wSecond,date.wMilliseconds);
                const std::wstring mode=replayMode>=4?L"Online Co-op "+std::wstring(replayMode==5?L"Queen's Revenge! ":L"Survival ")+SurvivalArenaNames[menu.survivalArena]:replayMode==3?L"Queen's Revenge! "+std::wstring(SurvivalArenaNames[menu.survivalArena]):replayMode==1?SurvivalArenaNames[menu.survivalArena]:replayMode==2?L"Online Co-op":L"Campaign";
                replay.start(menu.replayDirectory()/(std::wstring(stamp)+L"-"+std::to_wstring(GetCurrentProcessId())+L"_"+mode+L" "+level.stem().wstring()+L".atr"),header);
            }
            ExplorerReplay::Writer frame;
            frame.pod(player);frame.pod(hud.networkState());frame.pod(enemies->attachmentState());
            frame.pod(yaw&4095);frame.pod(pitch);frame.pod(steps);frame.pod(muzzleTicks);
            frame.pod(swayPhase);frame.pod(swayAmount);
            frame.pod(mission.remaining);frame.pod(unsigned(exitFading));frame.pod(exitFadeTicks);
            frame.pod(coopLocal);frame.pod(coopMarines);frame.pod(coopScore);
            frame.pod(g_survival.wave);frame.pod(g_survival.alive);frame.pod(g_survival.lives);
            frame.pod(g_survival.breakTicks);frame.pod(g_survival.introTicks);frame.pod(g_survival.baseScore);
            frame.pod(unsigned(g_survival.ended));
            frame.pod(unsigned(doors.entries.size()));
            for(const auto& door:doors.entries)frame.pod(door.amount);
            frame.list(logic.lifts);frame.list(objects.networkRemoved());frame.list(objects.effects);
            frame.list(animatedTextures);
            frame.pod(lighting.phase);frame.list(ps1Lighting.networkRecords());
            frame.list(std::vector<wchar_t>(notice.begin(),notice.end()));frame.pod(noticeTicks);
            const auto& messages=pickups.networkMessages();
            frame.pod(unsigned(messages.size()));
            for(const auto& message:messages){
                frame.list(std::vector<wchar_t>(message.text.begin(),message.text.end()));
                frame.pod(message.length);frame.pod(message.age);
            }
            frame.list(replayCues);replayCues.clear();
            replay.writeMemory(frame,memory);
            replay.write(frame);++replay.frames;
            if(replay.bytes>=ExplorerReplay::Limit)finishReplayRecording();
        }catch(const std::exception& error){
            ExplorerDiagnostics::error(std::string("replay recording stopped: ")+error.what());
            replay.abortRecording();
            replayEnabled=false;
            notice=L"Replay recording stopped";noticeTicks=120;
        }
    }
    bool replayFrame(){
        if(!replay.read())return false;
        ExplorerReplay::Reader frame{replay.packet};
        const bool first=replay.baseline.empty();
        previousPosition=player.position;
        replayPreviousYaw=yaw&4095;replayPreviousPitch=pitch;
        previousSwayPhase=swayPhase;previousSwayAmount=swayAmount;
        previousEnemies.clear();
        if(!first){
            auto old=mem();
            for(auto e:enemies->active())previousEnemies[e]={short(old.s(e)),short(old.s(e+2)),short(old.s(e+4))};
        }
        const auto gridBase=mem().d(0xa0c40);
        const auto originX=player.originX,originZ=player.originZ;
        player=frame.pod<WorldPlayerState>();
        const auto weapon=frame.pod<ExplorerHud::NetworkState>();
        const auto attachment=frame.pod<ExplorerEnemies::AttachmentState>();
        yaw=frame.pod<int>();pitch=frame.pod<int>();steps=frame.pod<unsigned>();muzzleTicks=frame.pod<unsigned>();
        swayPhase=frame.pod<double>();swayAmount=frame.pod<double>();
        if(first){
            previousPosition=player.position;
            replayPreviousYaw=yaw;replayPreviousPitch=pitch;
            previousSwayPhase=swayPhase;previousSwayAmount=swayAmount;
        }
        mission.remaining=frame.pod<unsigned>();exitFading=frame.pod<unsigned>()!=0;exitFadeTicks=frame.pod<unsigned>();
        coopLocal=frame.pod<unsigned>();coopMarines=frame.pod<decltype(coopMarines)>();coopScore=frame.pod<decltype(coopScore)>();
        if(coopLocal>1||yaw<0||yaw>4095||pitch< -1024||pitch>1024)
            throw std::runtime_error("Invalid replay camera");
        g_survival.wave=frame.pod<unsigned>();g_survival.alive=frame.pod<unsigned>();g_survival.lives=frame.pod<unsigned>();
        g_survival.breakTicks=frame.pod<unsigned>();g_survival.introTicks=frame.pod<unsigned>();
        g_survival.baseScore=frame.pod<std::uint64_t>();g_survival.ended=frame.pod<unsigned>()!=0;
        if(frame.pod<unsigned>()!=doors.entries.size())throw std::runtime_error("Replay door catalogue differs");
        for(auto& door:doors.entries){
            door.previous=door.amount;door.amount=frame.pod<float>();
            if(first)door.previous=door.amount;
            if(!std::isfinite(door.amount)||door.amount<0||door.amount>1)throw std::runtime_error("Invalid replay door");
        }
        auto lifts=frame.list<ExplorerLift>(512);
        if(lifts.size()!=logic.lifts.size())throw std::runtime_error("Replay lift catalogue differs");
        for(size_t i=0;i<lifts.size();++i)lifts[i].previous=first?lifts[i].y:logic.lifts[i].y;
        logic.lifts=std::move(lifts);
        objects.networkRemoved(frame.list<unsigned char>(65536));
        objects.effects=frame.list<ExplorerObjects::Effect>(512);
        animatedTextures=frame.list<unsigned>(65536);
        const auto phase=frame.pod<unsigned>();
        if(phase>2)throw std::runtime_error("Invalid replay lighting bank");
        if(phase!=lighting.phase){ps1Lighting.load(level,root,int(phase));lighting.phase=phase;}
        ps1Lighting.networkRecords(frame.list<std::array<unsigned char,28>>(256));
        const auto words=frame.list<wchar_t>(4096);
        notice.assign(words.begin(),words.end());noticeTicks=frame.pod<unsigned>();
        const auto messageCount=frame.pod<unsigned>();
        if(messageCount>4)throw std::runtime_error("Invalid replay pickup messages");
        std::vector<ExplorerPickups::QueuedMessage> messages;
        for(unsigned i=0;i<messageCount;++i){
            const auto characters=frame.list<wchar_t>(128);
            messages.push_back({{characters.begin(),characters.end()},frame.pod<unsigned>(),frame.pod<unsigned>()});
        }
        const auto cues=frame.list<ExplorerAudio::ReplayCue>(256);
        trilogy::coop::applyDelta(frame,replay.baseline,memory.size());
        frame.end();
        std::copy(replay.baseline.begin(),replay.baseline.end(),memory.begin());
        auto m=mem();
        if(m.d(0xa0c40)!=gridBase||m.w(0xa0c30)!=grid.width||m.w(0xa0c32)!=grid.height||
           player.originX!=originX||player.originZ!=originZ||player.currentCell>=cells.size())
            throw std::runtime_error("Invalid replay level state");
        auto chain=[&](unsigned p,unsigned base,unsigned stride,unsigned link){
            for(unsigned count=0;p;p=m.d(p+link)){
                if(count++>=128||p<base||(p-base)%stride||(p-base)/stride>=128)
                    throw std::runtime_error("Invalid replay object chain");
                if(base==0x350000&&(m.d(p)!=0x235700+(p-base)/stride*8||
                                   m.d(p+4)!=0x235f00+(p-base)/stride*8))
                    throw std::runtime_error("Invalid replay projectile vector");
            }
        };
        chain(m.d(0x2365e0),0x300000,208,200);
        chain(m.d(0x236c44),0x340000,48,40);
        chain(m.d(0x236300),0x350000,44,36);
        hud.networkState(weapon);
        std::copy_n(replay.baseline.begin()+0x31d000,0x50,memory.begin()+0x31d000);
        enemies->attachmentState(attachment);
        pickups.networkMessages(messages);
        audio->listener(float(player.position[0]),float(player.position[2]),float(yaw*2*Pi/4096));
        for(const auto& cue:cues){
            if(!std::isfinite(cue.x)||!std::isfinite(cue.z)||cue.slot>255)continue;
            switch(cue.type){
                case ExplorerAudio::CueType::Centered:audio->play(cue.slot,cue.x,cue.z);break;
                case ExplorerAudio::CueType::Positioned:audio->playAt(cue.slot,cue.x,cue.z);break;
                case ExplorerAudio::CueType::Weapon:audio->playWeapon(cue.slot,cue.x!=0);break;
            }
        }
        return true;
    }
    void stopReplay(){
        replay.playback.close();replay.playback.clear();replay.baseline.clear();
        replaying=false;coopGame=false;g_survival={};exitFading=false;photoMode=false;photoPointerMode=false;
        level=root/"GameData/PS1/CD/SECT11/L111LEV.MAP";
        menu.show(false);menu.showReplays();
        mission.briefing=false;mission.complete=false;
        audio->pause(false);
    }
    void watchReplay(const fs::path& path){
        try{
            replay.open(path);
            watchedReplay=path;
            if(!replay.read())throw std::runtime_error("Empty replay");
            ExplorerReplay::Reader header{replay.packet};
            if(header.pod<unsigned>()!=ExplorerReplay::Version)throw std::runtime_error("Replay version differs");
            const unsigned mode=header.pod<unsigned>(),arena=header.pod<unsigned>(),savedDifficulty=header.pod<unsigned>();
            const unsigned skulls=header.pod<unsigned>();
            const auto stem=header.text();
            const auto hash=header.pod<std::uint64_t>();header.end();
            const bool survivalReplay=mode==1||mode>=3;
            if(mode>5||arena>=std::size(SurvivalArenaFolders)||savedDifficulty>2)
                throw std::runtime_error("Invalid replay header");
            auto map=std::find_if(physicalLevels.begin(),physicalLevels.end(),[&](const auto& p){return p.stem()==stem;});
            ExplorerLevelConfig config;
            const auto folder=root/"GameData"/SurvivalArenaFolders[arena];
            if(survivalReplay){
                if(!explorerLoadSurvivalConfig(folder,config)||config.map.stem()!=stem)
                    throw std::runtime_error("Replay arena unavailable");
                level=config.map;
            }else{
                if(map==physicalLevels.end())throw std::runtime_error("Replay level unavailable");
                level=*map;
            }
            if(ExplorerReplay::checksum(explorerRead(level))!=hash)throw std::runtime_error("Replay map has changed");
            replaying=true;difficulty=savedDifficulty;coopGame=mode==2||mode>=4;
            g_survival={};
            if(mode==3||mode==5)config.setQueensRevenge();
            if(survivalReplay){g_survival.active=true;g_survival.cfg=std::move(config);g_survival.skulls=skulls;}
            load(stem!="L113LEV");
            if(survivalReplay&&!explorerSurvivalSetup(*this))throw std::runtime_error("Replay arena setup failed");
            menu.open=false;menu.hasGame=false;mission.briefing=false;mission.complete=false;
            audio->pause(false);
            renderAlpha=1;
            if(!replayFrame())throw std::runtime_error("Replay has no gameplay frames");
        }catch(const std::exception& error){
            ExplorerDiagnostics::error(std::string("replay could not start: ")+error.what());
            stopReplay();
            const std::string message=error.what();
            menu.message.assign(message.begin(),message.end());
        }
    }
 trilogy::coop::TeamScore coopScore;trilogy::coop::Recovery coopRecovery;
 std::function<void(double,double,bool)> coopTick;std::function<void()> coopRestore;std::function<void(unsigned,bool)> coopSound;bool coopDriving=false,coopWorld=true,coopEnemyPass=true;
 std::function<void(unsigned)> coopQueenReward;
 std::function<void(unsigned,unsigned,unsigned,unsigned,unsigned)> coopSurvivalPickup;
 bool coopGame=false;unsigned coopLocal=0,coopActor=0;std::array<trilogy::coop::Marine,2> coopMarines{};std::function<bool(bool)> coopExit;

 ExplorerPerformance performance;
 void drawPerformance(HDC dc,int width,int height){if(!menu.performanceDisplay||g_survival.introTicks||(activeSurvivalSkull(BrokenVisorSkull)&&!menu.open))return;auto text=performance.label();double scale=std::max(.5,height/700.);int padding=std::max(3,height/140),x=padding,y=padding,w=textFont.width(text,scale)+padding*2,h=int(18*scale)+padding*2;RECT panel{x,y,std::min(width,x+w),y+h};auto brush=CreateSolidBrush(RGB(3,7,4));FillRect(dc,&panel,brush);DeleteObject(brush);textFont.draw(dc,text,x+padding,y+padding,scale,0xe8ffe8);}

 ExplorerMovie movie;std::deque<std::string> movieQueue;bool moviesEnabled=false,movieRunning=false;
 bool hasMovies()const{return movieRunning||!movieQueue.empty();}
 void queueMovie(const std::string& name){if(moviesEnabled)movieQueue.push_back(name);}

    std::vector<unsigned char> memory=std::vector<unsigned char>(0x800000);
    std::vector<LevelCell> cells;LevelGrid grid;WorldPlayerState player;PlayerMovementState acceleration;
    std::array<short,4096> sine{};std::vector<Point> vertices;std::vector<Face> faces;
    std::vector<std::array<unsigned,2>> spawns;std::map<unsigned,unsigned> pending;
    fs::path root,level,checkpointDirectory;int yaw=0,pitch=0;unsigned steps=0,footstepClock=0,footsteps=0;
    std::map<unsigned,Mesh> doorMeshes,panelMeshes,liftMeshes,pickupMeshes;ExplorerTextures effectTextures,panelTextures,alienPanelTextures,batteryTextures;bool alienPanels=false;bool exitFading=false;unsigned exitFadeTicks=0;ExplorerMission::Statistics exitStatistics{};bool debugOverlay=false;ExplorerMenu menu;ExplorerFont textFont,pickupFont;ExplorerObjects objects;ExplorerMission mission;unsigned difficulty=1;ExplorerLogic logic;ExplorerLighting lighting;ExplorerPs1Lighting ps1Lighting;ExplorerPickups pickups;ExplorerTextures textures;ExplorerDoors doors;ExplorerHud hud;std::unique_ptr<ExplorerAudio> audio;std::vector<fs::path> levels,physicalLevels;ULONGLONG briefingStarted{};bool briefingRevealed=false;std::array<unsigned char,50> entryInventory{};unsigned entryWeapon{};unsigned levelIndex=0;bool useRequested=false,musicOn=true,musicPlaying=false,menuMusic=false;unsigned noticeTicks=0;std::wstring notice;std::vector<unsigned> animatedTextures;ExplorerTextureAnimation textureAnimation;std::array<double,5> renderStages{};double swayPhase=0,previousSwayPhase=0,swayAmount=0,previousSwayAmount=0;
    std::unique_ptr<ExplorerEnemies> enemies;bool firing=false,grenadeRequested=false;unsigned muzzleTicks=0;double aimFocal=1280*.72;unsigned lastFlameSprites=0,lastFlamePixels=0,lastBulletTrails=0,lastBulletTrailPixels=0,lastSpritePixels=0,lastTrackerPixels=0,lastAttachmentPixels=0;int lastWeaponBottom=-1;double renderAlpha=1,measuredFps=0;bool interpolate=false;Vec3s previousPosition{};std::map<unsigned,Vec3s> previousEnemies;
    bool photoMode=false,photoShowUi=true,photoCapturing=false,photoLamp=false;
    unsigned photoDof=0;bool photoPointerMode=false,photoDofDragging=false;
    RECT photoDofTrack(int width,int height)const{double scale=std::max(1.,height/360.);int y=int(24*scale)+18;return {14,y+int(17*scale),14+std::min(width-28,int(210*scale)),y+int(27*scale)};}
    void adjustPhotoDof(int value){photoDof=unsigned(std::clamp(value,0,100));}
    unsigned photoGrid=0,photoFilter=0,photoFlashTicks=0,photoNoticeTicks=0;
    double photoX=0,photoY=0,photoZ=0,photoFov=70.0,photoSpeed=24.0;
    int photoYaw=0,photoPitch=0,photoRoll=0;
    std::wstring photoNotice;
    fs::path lastCapturedPhoto;
    bool canEnterPhotoMode()const{return replaying||(!coopGame&&!hasMovies()&&!menu.open&&!mission.briefing&&!mission.complete&&!exitFading&&!g_survival.introTicks&&player.health>0&&menu.hasGame);}
    void enterPhotoMode(){if(!canEnterPhotoMode())return;photoMode=true;photoDof=0;photoShowUi=true;photoCapturing=false;photoX=player.position[0];photoY=player.position[1];photoZ=player.position[2];photoYaw=yaw;photoPitch=pitch;photoRoll=0;photoFov=menu.fov?double(menu.fov):70.0;photoSpeed=24.0;photoFlashTicks=0;photoNotice=L"PHOTO MODE - Press H to hide guides, Enter to capture";photoNoticeTicks=90;firing=false;grenadeRequested=false;useRequested=false;}
    void exitPhotoMode(){photoMode=false;photoLamp=false;photoFlashTicks=0;photoCapturing=false;firing=false;grenadeRequested=false;useRequested=false;notice=L"Resumed gameplay";noticeTicks=45;}
    void togglePhotoMode(){if(photoMode)exitPhotoMode();else enterPhotoMode();}
    fs::path capturePhoto(HWND window=nullptr,int reqWidth=0,int reqHeight=0);
    PlayerControlMemory mem(){return {memory};}
    struct FaceBounds {
        Point low{1e30,1e30,1e30},high{-1e30,-1e30,-1e30};
    };
    struct FaceSphere {Point center{};double radius=-1;};
    std::vector<FaceSphere> faceSpheres;
    struct OverlayBuffer {
        HDC dc=nullptr;
        HBITMAP bitmap=nullptr;
        HGDIOBJ previous=nullptr;
        unsigned* pixels=nullptr;
        int width=0,height=0;
        ~OverlayBuffer(){reset();}
        void reset(){
            if(dc&&previous)SelectObject(dc,previous);
            if(bitmap)DeleteObject(bitmap);
            if(dc)DeleteDC(dc);
            dc=nullptr;bitmap=nullptr;previous=nullptr;pixels=nullptr;
        }
        void ensure(HDC target,int w,int h){
            if(pixels&&w==width&&h==height)return;
            reset();
            BITMAPINFO info{};
            info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            void* bits=nullptr;
            dc=CreateCompatibleDC(target);
            bitmap=CreateDIBSection(target,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            if(!dc||!bitmap||!bits)throw std::runtime_error("Cannot allocate enemy display buffer");
            previous=SelectObject(dc,bitmap);
            pixels=static_cast<unsigned*>(bits);
            width=w;height=h;
        }
    } overlayBuffer;
    std::vector<unsigned> faceGroups;
    std::vector<unsigned char> hiddenGroups;
    bool cullStaticFaces=true,fastOpaqueFaces=true;
    void buildFaceGroups(unsigned width,unsigned height){
        constexpr unsigned tile=2048;
        const unsigned columns=(width*512+tile-1)/tile;
        const unsigned rows=(height*512+tile-1)/tile;
        std::vector<FaceBounds> faceBounds(columns*rows);
        faceGroups.clear();
        faceGroups.reserve(faces.size());
        for(const auto& face:faces){
            double x=0,z=0;
            unsigned corners=0;
            for(int i:face.vertices)if(i>=0){x+=vertices[i].x;z+=vertices[i].z;++corners;}
            const auto col=std::clamp(int((x/corners+width*256)/tile),0,int(columns)-1);
            const auto row=std::clamp(int((z/corners+height*256)/tile),0,int(rows)-1);
            const unsigned group=row*columns+col;
            faceGroups.push_back(group);
            auto& box=faceBounds[group];
            for(int i:face.vertices)if(i>=0){
                const auto& p=vertices[i];
                box.low.x=std::min(box.low.x,p.x);box.high.x=std::max(box.high.x,p.x);
                box.low.y=std::min(box.low.y,p.y);box.high.y=std::max(box.high.y,p.y);
                box.low.z=std::min(box.low.z,p.z);box.high.z=std::max(box.high.z,p.z);
            }
        }
        faceSpheres.assign(faceBounds.size(),{});
        for(unsigned i=0;i<faceBounds.size();++i){
            const auto& b=faceBounds[i];
            if(b.low.x>b.high.x)continue;
            faceSpheres[i]={{(b.low.x+b.high.x)*.5,(b.low.y+b.high.y)*.5,(b.low.z+b.high.z)*.5},
                std::hypot(b.high.x-b.low.x,b.high.y-b.low.y,b.high.z-b.low.z)*.5+1};
        }
    }
    Mesh survivalQueenSac;
    ExplorerPs1Lighting survivalSacLighting;
    struct SacSlice { double left=1e20,right=-1e20; };
    static constexpr int SacSliceDepth=128, SacPlayerRadius=128;
    int survivalSacFirstSlice=0;
    std::vector<SacSlice> survivalSacSlices;

    void prepareSurvivalSacCollision(){
        survivalSacSlices.clear();
        if(survivalQueenSac.faces.empty())return;
        const auto& anchor=g_survival.queenSacAnchor;
        const double cs=std::cos(SurvivalSacAngle),sn=std::sin(SurvivalSacAngle);
        std::vector<std::array<double,2>> footprint;
        for(const auto& v:survivalQueenSac.vertices)
            footprint.push_back({anchor[0]+v.x*cs+v.z*sn,anchor[2]-v.x*sn+v.z*cs});
        auto slice=[](double z){return int(std::floor(z/SacSliceDepth));};
        const auto [first,last]=std::minmax_element(footprint.begin(),footprint.end(),
            [](const auto& a,const auto& b){return a[1]<b[1];});
        survivalSacFirstSlice=slice((*first)[1]-SacPlayerRadius);
        survivalSacSlices.resize(slice((*last)[1]+SacPlayerRadius)-survivalSacFirstSlice+1);
        // Merge the membrane and cord footprint across each slice of the sac.
        for(const auto& face:survivalQueenSac.faces){
            double left=1e20,right=-1e20,minZ=1e20,maxZ=-1e20;
            for(int index:face.vertices){
                if(index<0)continue;
                const auto [x,z]=footprint[index];
                left=std::min(left,x);right=std::max(right,x);
                minZ=std::min(minZ,z);maxZ=std::max(maxZ,z);
            }
            for(int i=slice(minZ-SacPlayerRadius);i<=slice(maxZ+SacPlayerRadius);++i){
                auto& band=survivalSacSlices[i-survivalSacFirstSlice];
                band.left=std::min(band.left,left-SacPlayerRadius);
                band.right=std::max(band.right,right+SacPlayerRadius);
            }
        }
    }
    bool survivalSacBlocks(const Vec3s& from,const Vec3s& to)const{
        if(!g_survival.active||survivalSacSlices.empty())return false;
        auto slice=[&](double z){return int(std::floor(z/SacSliceDepth))-survivalSacFirstSlice;};
        auto inside=[&](const Vec3s& p){
            const int i=slice(p[2]);
            return i>=0&&i<int(survivalSacSlices.size())&&
                p[0]>=survivalSacSlices[i].left&&p[0]<=survivalSacSlices[i].right;
        };
        if(inside(from))return false; // Allow leaving an overlap.
        const double dx=to[0]-from[0],dz=to[2]-from[2];
        const int first=std::max(0,slice(std::min(from[2],to[2])));
        const int last=std::min(int(survivalSacSlices.size())-1,slice(std::max(from[2],to[2])));
        for(int i=first;i<=last;++i){
            const auto& band=survivalSacSlices[i];
            const double low=(i+survivalSacFirstSlice)*SacSliceDepth;
            double enter=0,leave=1;
            if(dz){
                const double a=(low-from[2])/dz,b=(low+SacSliceDepth-from[2])/dz;
                enter=std::max(0.,std::min(a,b));leave=std::min(1.,std::max(a,b));
            }
            const double a=from[0]+enter*dx,b=from[0]+leave*dx;
            if(std::max(a,b)>=band.left&&std::min(a,b)<=band.right)return true;
        }
        return false;
    }
    void load(bool standalone=false){
        menu.exterminationGame=isolationStage!=0;   // the pause menu saves and loads Extermination's own slots only in Extermination
        if(!replaying)finishReplayRecording();
        menu.achievements.campaignLoaded();
        if(g_survival.active&&level!=g_survival.cfg.map){
            explorerSaveSurvivalRun(*this,L"Quit");
            g_survival={};
        }
        if(gpuRenderer)gpuRenderer->invalidate();
        if(physicalLevels.empty()){for(auto& file:fs::recursive_directory_iterator(root/"GameData/PS1/CD"))if(file.is_regular_file()&&file.path().extension()==".MAP")physicalLevels.push_back(file.path());std::sort(physicalLevels.begin(),physicalLevels.end());for(auto& file:physicalLevels)if(file.stem()!="L114LEV"&&file.stem()!="L115LEV")levels.push_back(file);}
        textFont.load(root);pickupFont.loadSmall(root);
        auto requestedCode=level.stem().string().substr(1,3);bool lightingFamily=level.stem()=="L113LEV"||level.stem()=="L114LEV"||level.stem()=="L115LEV";
        if(lightingFamily&&!standalone)level=level.parent_path()/"L113LEV.MAP";
        auto selected=std::find(physicalLevels.begin(),physicalLevels.end(),level);levelIndex=selected==physicalLevels.end()?0:unsigned(selected-physicalLevels.begin());
        exitFading=false;exitFadeTicks=0;exitStatistics={};notice.clear();noticeTicks=0;photoMode=photoLamp=photoCapturing=false;photoFlashTicks=photoNoticeTicks=0;swayPhase=previousSwayPhase=swayAmount=previousSwayAmount=0;lighting.load(level,lightingFamily&&!standalone);ps1Lighting.load(level,root,lighting.active?0:-1);


        initializePs1Memory(mem(),root);
        auto bytes=read(level);if(bytes.size()<20||std::string(bytes.begin(),bytes.begin()+4)!="FORM")throw std::runtime_error("Level has no FORM container");
        size_t payload=0,length=0;for(size_t p=12;p+8<=bytes.size();){auto n=(unsigned(bytes[p+4])<<24)|(unsigned(bytes[p+5])<<16)|(unsigned(bytes[p+6])<<8)|bytes[p+7];if(p+8+n>bytes.size())throw std::runtime_error("Bad level chunk bounds");if(std::string(bytes.begin()+p,bytes.begin()+p+4)=="MAP0"){payload=p+8;length=n;break;}p+=8+n+(n&1);}
        if(length<36||length>0x130000)throw std::runtime_error("Unsupported MAP0 extent");
        auto word=[&](size_t p){return unsigned(bytes[payload+p])|(unsigned(bytes[payload+p+1])<<8);};
        // Validate every serialized region before calling the original-style
        // trusted-buffer routines. The final extent is verified by the loader.
        unsigned vc=word(0),fc=word(2),width=word(4),height=word(6);
        if(!fc||vc>32767||fc>32767||!width||!height||width>127||height>127)throw std::runtime_error("Level dimensions/counts exceed exploration harness limits");
        size_t end=36+vc*8ull+fc*20ull+width*height*16ull+bytes[payload+12]*8ull+word(14)*20ull+word(16)*8ull+word(18)*16ull+word(20)*8ull+word(22)*16ull+bytes[payload+28]*4ull+bytes[payload+29]*4ull+bytes[payload+13]*28ull+word(26)*4ull+word(30)*4ull;
        if(end!=length)throw std::runtime_error("Level region layout is incomplete");
        std::copy_n(bytes.begin()+payload,length,memory.begin()+0x100000);auto m=mem();m.d(0xa0bc8+0xa4,0x100000);
        if(level.filename()=="L111LEV.MAP"){
            // 1.1.1: Facehugger record 23 waits at load like its five crate siblings (records 18-24), but no crate,
            // egg or script ever releases it - yet it counts toward the level's aliens, so 100% was never reachable
            // (27 of 28: 96%). Give it a home: object 21, an empty crate at cell (69,67), now holds it (flag bit 2,
            // link 23), as the other Facehugger crates do. Only when the data is exactly as shipped.
            const size_t objects=36+vc*8ull+fc*20ull+width*height*16ull+bytes[payload+12]*8ull+word(14)*20ull+word(16)*8ull;
            const size_t record=36+vc*8ull+fc*20ull+width*height*16ull+bytes[payload+12]*8ull+23*20ull;
            auto o=memory.begin()+0x100000+objects+21*16;auto r=memory.begin()+0x100000+record;
            if(word(18)>21&&word(14)>23&&o[0]==69&&o[1]==67&&o[2]==25&&o[6]==255&&!(o[3]&2)&&r[0]==2&&r[7]==0&&r[8]==1){o[3]|=2;o[6]=23;}
        }
        auto unresolved=[&](PlayerControlMemory&,unsigned address,unsigned,unsigned,unsigned){++pending[address];return PlayerControlResult{};};
        initializeLevelLayout(m,0xa0bc8,withLevelLoading(unresolved));resetLevelCollisionState(m,0xa0bc8);
        cells.resize(width*height);std::memcpy(cells.data(),m.bytes.data()+m.d(0xa0c40),cells.size()*sizeof(LevelCell));
        grid={static_cast<unsigned short>(width),static_cast<unsigned short>(height),cells};
        for(unsigned i=0;i<4096;++i)sine[i]=static_cast<short>(m.w(0x9763c+i*2));
        vertices.clear();faces.clear();spawns.clear();for(unsigned i=0;i<vc;++i){auto p=0x100000+36+i*8;vertices.push_back({double(m.s(p)),double(m.s(p+2)),double(m.s(p+4))});}
        auto faceStart=0x100000+36+vc*8;for(unsigned i=0;i+1<fc;++i){auto p=faceStart+i*20;Face f{};f.sourceFace=i;bool valid=true;for(unsigned j=0;j<4;++j){auto address=m.d(p+j*4);if(j==3&&std::bit_cast<int>(address)<0)f.vertices[j]=-1;else{auto vertexBase=0x100000+36;auto delta=address-vertexBase;if(address<vertexBase||delta%8||delta/8>=vc)valid=false;f.vertices[j]=int(delta/8);}}f.light=m.b(p+18)&127;f.texture=m.w(p+16);f.flags=m.b(p+19)&127;if(valid)faces.push_back(f);}
        buildFaceGroups(width,height);
        auto monsters=m.d(0xa0c48);for(unsigned i=0;i<m.w(0xa0c1a);++i)spawns.push_back({m.b(monsters+i*20+1),m.b(monsters+i*20+2)});
        enemies.reset();
        player={};player.originX=m.w(0xa0c34);player.originZ=m.w(0xa0c36);player.position={static_cast<short>(m.w(0xa0c24)*512-player.originX),0,static_cast<short>(m.w(0xa0c26)*512-player.originZ)};
        auto x=unsigned(int(player.position[0]))+player.originX,z=unsigned(int(player.position[2]))+player.originZ;
        if(x<512||z<512||x+512>=width*512||z+512>=height*512||((grid.cell(x,z).bytes[10]>=20)&&(grid.cell(x,z).bytes[10]<45))){
            bool found=false;double best=1e30;unsigned bx=0,bz=0;for(unsigned cz=1;cz+1<height;++cz)for(unsigned cx=1;cx+1<width;++cx){auto& cell=cells[cz*width+cx];if((cell.bytes[10]>=20&&cell.bytes[10]<45)||int(cell.bytes[13])-int(cell.bytes[11])<32)continue;double dx=double(cx*512+256)-x,dz=double(cz*512+256)-z,dist=dx*dx+dz*dz;if(dist<best){best=dist;bx=cx*512+256;bz=cz*512+256;found=true;}}
            if(!found)throw std::runtime_error("No valid player start in level");x=bx;z=bz;player.position[0]=static_cast<short>(x-player.originX);player.position[2]=static_cast<short>(z-player.originZ);
        }
        player.position[1]=static_cast<short>(floorHeight(grid,x,z));player.currentCell=grid.index(x,z);player.health=100;player.terrainGate=0;
        acceleration={};acceleration.maximumSpeed=120;acceleration.acceleration=9;acceleration.terrainGate=1;
        constexpr int startYaw[]={2048,2560,-1024,-512,0,512,1024,1536};yaw=m.w(0xa0c28)<8?startYaw[m.w(0xa0c28)]:0;pitch=0;previousPosition=player.position;previousEnemies.clear();steps=footstepClock=footsteps=0;pending.clear();firing=false;muzzleTicks=0;
        doorMeshes.clear();liftMeshes.clear();for(auto& [tag,data]:explorerChunks(level))if(tag.size()==4&&std::isdigit(static_cast<unsigned char>(tag[1]))){if(tag[0]=='D')doorMeshes.emplace(unsigned(std::stoi(tag.substr(1))),decodeMesh(data));if(tag[0]=='L')liftMeshes.emplace(unsigned(std::stoi(tag.substr(1))),decodeMesh(data));}
        auto code=level.stem().string().substr(1,3);mission.load(m,root,requestedCode,levelIndex);m.d(0xa0c80,mission.index<45?mission.index:0);const auto lair=root/"GameData/PS1/CD/SECT12/L162LEV.MAP";
        const bool colonySurvival=g_survival.active&&code=="162";
        const auto textureFolder=colonySurvival?lair.parent_path():level.parent_path();
        textures.load(textureFolder/(code+"GFX.BIN"));
        survivalQueenSac=g_survival.active&&g_survival.cfg.queen
            ?loadSurvivalQueenSac(colonySurvival?lair:level.parent_path()/"QUEENSAC.MAP"):Mesh{};
        if(!survivalQueenSac.faces.empty())survivalSacLighting.load(lair,root);
        textureAnimation.load(m,animatedTextures);
        if(!audio)audio=std::make_unique<ExplorerAudio>();
        audio->replayCue=[this](const ExplorerAudio::ReplayCue& cue){
            if(replayEnabled&&!replaying&&replayCues.size()<256&&!menu.open)replayCues.push_back(cue);
        };audio->music(false);musicPlaying=false;menuMusic=false;int ps1Track=isolationStage==2?ExterminationLevel2Track:explorerPs1Track(root,requestedCode);audio->level(root,requestedCode,ps1Track>=0?ps1Track:mission.index<45?int(m.w(0xacefa+(mission.index+1)*2)):-1,g_survival.active||isolationStage,g_survival.active&&g_survival.cfg.queensRevenge());
        explorerSurvivalBeforeEnemies(*this,m);
        if(isolationStage==4){   // the finale (Queen's Lair): record 1 is the held Queen
            const auto records=m.d(0xa0c48);
            if(m.w(0xa0c1a)<2||m.b(records+20)!=7)
                throw std::runtime_error("Extermination finale needs a Queen spawn");
            m.b(records+28,1);
        }
        enemies=std::make_unique<ExplorerEnemies>(m,root);enemies->initialize(difficulty);if(level.filename()=="L263LEV.MAP"){   /* 2.6.3: the level data stands the Queen 1.3 cells to the side of her egg sac; in 1.6.2 and 3.9.1 the sac's centre is 2.05 cells straight behind her. Move her onto her sac's line (world 31718,10337: cell 61.95,20.19). */auto mm=mem();for(auto e:enemies->active())if(mm.b(e+0x74)==7){const unsigned base=mm.d(0xa0c40),width=mm.w(0xa0c30),old=mm.d(e+0x78),cell=base+(20*width+61)*16;if(old&&mm.b(old+12)==7)mm.b(old+12,0);mm.w(e,31718-mm.w(0xa0c34));mm.w(e+4,10337-mm.w(0xa0c36));mm.d(e+0x78,cell);mm.b(cell+12,7);mm.b(cell+4,mm.b(e+0x70));}}   /* in line with the sac's centre on the flat floor at cell (61,20): the raised mound under the sac starts at x 62 */enemies->survivalSkulls=g_survival.active?g_survival.skulls:0;enemies->sound=[this](unsigned slot,unsigned entity){if(entity>=0x300000&&entity<0x300000+128*208){auto data=mem();audio->playAt(slot,float(data.s(entity)),float(data.s(entity+4)));}else audio->play(slot);};doors.load(m,isolationStage?1920:code=="111"||code=="112"?960:1376);hud.load(m,root,code[0]=='3');logic.load(m,doors,code=="331"||isolationStage!=0);pickups.load(m);objects.load(m,requestedCode=="161");objects.pickup=[this](unsigned object,unsigned index){if(!g_survival.active||!g_survival.cfg.queensRevenge())pickups.crateDrop(object,index);};objects.enemy=[this](unsigned object,unsigned index){enemies->crateEnemy(object,index);};objects.blast=[this](unsigned pos,unsigned cell){enemies->blast(pos,cell);};objects.sound=[this](unsigned slot){audio->play(slot);};briefingStarted=GetTickCount64();briefingRevealed=false;logic.exit=[this](){return beginExit(false);};pickups.sound=[this](unsigned slot){audio->play(slot);};enemies->droppedItem=[this](unsigned entity){if(!g_survival.active||!g_survival.cfg.queensRevenge())pickups.enemyDrop(entity);};logic.triggerPickup=[this](unsigned delta,unsigned index){if(!g_survival.active||!g_survival.cfg.queensRevenge())pickups.trigger(delta,index);};logic.sound=[this](unsigned slot){audio->play(slot);};logic.spawnEnemy=[this](unsigned delta,unsigned index){enemies->trigger(delta,index);};logic.textureChange=[this](unsigned delta,unsigned index){textureAnimation.activate(delta,index);};
        enemies->projectileAction=[this](unsigned p){logic.projectile(mem(),doors,p);};enemies->levelAction=[this](unsigned index){logic.command(mem(),doors,index);};enemies->ambient=[this](unsigned e,unsigned slot,float gain,float pan,bool active){auto data=mem();audio->ambient(e,slot,gain,pan,active,float(data.s(e)),float(data.s(e+4)),true);};enemies->hazardNotice=[this](const std::wstring& text){notice=text;noticeTicks=90;};enemies->explosion=[this](int x,int y,int z){objects.effects.push_back({x,y,z,0,true});audio->playAt(41,float(x),float(z));};objects.textureChange=logic.textureChange;logic.lightChange=[this](unsigned delta,unsigned index){ps1Lighting.toggle(delta,index);};
        panelMeshes.clear();for(auto& [tag,data]:explorerChunks(root/"GameData/PS1/CD/GFX/OBJ3D.BND"))if(tag.size()==4&&tag[0]=='M')panelMeshes.emplace(unsigned(std::stoi(tag.substr(1))),decodeMesh(data));
        panelTextures.load(root/"GameData/PS1/CD/LANGUAGE/PNL0GFXU.BND");alienPanelTextures.load(root/"GameData/PS1/CD/LANGUAGE/PNL1GFXU.BND");alienPanels=code[0]=='3'||(mission.index>=35&&mission.index<45&&m.b(0x96ea3+mission.index));effectTextures.load(root/"GameData/PS1/CD/GFX/EXPLGFX.BND");for(auto& [page,pixels]:effectTextures.pages)for(auto& color:pixels)if((color&0xffffff)==0)color=0;
        batteryTextures.load(root/"GameData/PS1/CD/GFX/PICKGFX.BND");pickupMeshes.clear();for(auto& [tag,data]:explorerChunks(root/"GameData/PS1/CD/GFX/PICKMOD.BND"))if(tag.size()==4&&tag[0]=='M')pickupMeshes.emplace(unsigned(std::stoi(tag.substr(1))),decodeMesh(data));useRequested=false;grenadeRequested=false;
        std::memcpy(cells.data(),m.bytes.data()+m.d(0xa0c40),cells.size()*sizeof(LevelCell));
        if(!coopGame&&!g_survival.active&&menu.giveWeapons){for(unsigned kind=0;kind<5;++kind)awardPickup(m,kind,100,10);hud.syncAmmo();}
        clearLevelItems();
        mission.visit(m.d(0xa0c40)+player.currentCell*16);cells[player.currentCell].bytes[7]=m.b(m.d(0xa0c40)+player.currentCell*16+7);entryInventory=inventory();entryWeapon=hud.selected();
    }
    void clearLevelItems(){
        auto m=mem();
        m.w(0xa0ab4,m.w(0xa0ab4)&0xbc7fu);
        for(unsigned a:{0xa0abcu,0xa0ad2u,0xa0ad4u,0xa0ad6u,0xa0ad8u,0xa0adau,0xa0ae2u,0xa0ae4u})m.w(a,0);
    }
    void syncPauseMap(){
        auto m=mem();unsigned w=m.w(0xa0c30),h=m.w(0xa0c32),gridBase=m.d(0xa0c40);
        menu.mapWidth=w;menu.mapHeight=h;menu.mapPlayerX=player.currentCell%std::max(1u,w);menu.mapPlayerZ=player.currentCell/std::max(1u,w);
        menu.mapPlayerYaw=yaw;menu.mapAutoMapper=(m.b(0xa0ab4)&128)!=0;if(!menu.mapAutoMapper){menu.mapWidth=menu.mapHeight=0;menu.mapCells.clear();return;}
        menu.mapCells.assign(size_t(w)*h,0);
        for(unsigned i=0;i<w*h;++i){
            auto c=gridBase+i*16;auto attr=m.b(c+10);bool wall=attr>=20&&attr<45,visited=(m.b(c+7)&1)!=0,secret=m.b(c+5)!=0;
            if(!wall||visited)menu.mapCells[i]=(visited?1:0)|(secret?8:0)|(!wall?16:0);
        }
        for(auto& d:doors.entries)for(auto cell:d.cells){unsigned idx=(cell-gridBase)/16;if(idx<menu.mapCells.size()){bool vis=(m.b(cell+7)&1)!=0;menu.mapCells[idx]=(vis?1:0)|2|16;}}
        for(auto& l:logic.lifts)for(unsigned dz=0;dz<l.height;++dz)for(unsigned dx=0;dx<l.width;++dx){unsigned x=l.gx+dx,z=l.gz+dz;if(x<w&&z<h){unsigned idx=z*w+x,cell=gridBase+idx*16;bool vis=(m.b(cell+7)&1)!=0;menu.mapCells[idx]=(vis?1:0)|4|16;}}
    }
    std::array<unsigned char,50> inventory(){std::array<unsigned char,50> data;auto m=mem();for(unsigned i=0;i<data.size();++i)data[i]=m.b(0xa0ab4+i);data[0]&=0x7f;data[1]&=0xbc;for(unsigned off:{0xa0abcu,0xa0ad2u,0xa0ad4u,0xa0ad6u,0xa0ad8u,0xa0adau,0xa0ae2u,0xa0ae4u})if(off>=0xa0ab4&&off+1<0xa0ab4+data.size()){data[off-0xa0ab4]=0;data[off-0xa0ab4+1]=0;}return data;}
    void beginCoopPlayers(bool pistolOnly=true){
        coopGame=true;coopLocal=0;menu.coopSession=true;coopScore={};coopRecovery={};coopMarines={};
        auto m=mem();m.b(0x94b9c,m.b(0x94b9c)&~36u);
        std::array<unsigned char,50> starting{};
        starting[4]=100; // health, 0xa0ab8
        if(pistolOnly){inventory(starting,trilogy::coop::StartingWeapon);
        awardPickup(m,0,15,2);hud.select(trilogy::coop::StartingWeapon);hud.syncAmmo();}
        entryInventory=inventory();entryWeapon=hud.selected();
        for(auto& buddy:coopMarines){buddy.position=player.position;buddy.yaw=yaw;buddy.weapon=trilogy::coop::StartingWeapon;}
    }
    void inventory(const std::array<unsigned char,50>& data,unsigned selected=0){auto m=mem();for(unsigned i=0;i<data.size();++i)m.b(0xa0ab4+i,data[i]);clearLevelItems();player.health=m.w(0xa0ab8);hud.select(selected);hud.syncAmmo();entryInventory=inventory();entryWeapon=selected;}
    void syncMusic(){bool front=menu.open&&!menu.hasGame;if(front!=menuMusic){audio->music(false);musicPlaying=false;audio->soundtrack(root,front?"menu":mission.sourceCode,front?2:isolationStage==2?ExterminationLevel2Track:explorerPs1Track(root,mission.sourceCode));menuMusic=front;}bool play=musicOn&&(front||!mission.briefing);if(play!=musicPlaying){audio->music(play);musicPlaying=play;}}
    bool panelBlocks(const Vec3s& from,const Vec3s& to)const{
        for(const auto& panel:logic.panels){unsigned model=panel.kind==32?40:panel.kind==22?13:panel.kind==24?5:panel.kind==26?17:9;if(panel.kind!=32&&alienPanels)model+=16;auto found=panelMeshes.find(model);if(found==panelMeshes.end()||found->second.vertices.empty())continue;
            double lo[3]={1e20,1e20,1e20},hi[3]={-1e20,-1e20,-1e20};for(const auto& v:found->second.vertices){double roll=panel.kind==32?std::bit_cast<short>(static_cast<unsigned short>(memory[0x320000+panel.index*40+12]|(memory[0x320000+panel.index*40+13]<<8)))*2*Pi/4096:0,scale=panel.kind==32?1:.875;double p[]={(v.x*std::cos(roll)-v.y*std::sin(roll))*scale,(v.x*std::sin(roll)+v.y*std::cos(roll))*scale,v.z*scale};for(unsigned i=0;i<3;++i){lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}}
            if(to[1]+1024<=panel.y+lo[1]||to[1]>=panel.y+hi[1])continue;double angle=Pi-panel.rotation*Pi/4,cs=std::cos(angle),sn=std::sin(angle);auto local=[&](const Vec3s& p){double dx=p[0]-panel.x,dz=p[2]-panel.z;return std::array<double,2>{dx*cs-dz*sn,dx*sn+dz*cs};};auto a=local(from),b=local(to);double minX=lo[0]-128,maxX=hi[0]+128,minZ=lo[2]-128,maxZ=hi[2]+128;
            if(a[0]>minX&&a[0]<maxX&&a[1]>minZ&&a[1]<maxZ)continue; // Allow leaving an existing overlap.
            double enter=0,leave=1;bool hit=true;for(unsigned axis=0;axis<2;++axis){double low=axis?minZ:minX,high=axis?maxZ:maxX,d=b[axis]-a[axis];if(std::abs(d)<1e-8){if(a[axis]<=low||a[axis]>=high){hit=false;break;}}else{double t0=(low-a[axis])/d,t1=(high-a[axis])/d;if(t0>t1)std::swap(t0,t1);enter=std::max(enter,t0);leave=std::min(leave,t1);if(enter>leave){hit=false;break;}}}if(hit)return true;
        }return false;
    }
    void resolvePanels(const Vec3s& before){
        auto blocks=[&](const Vec3s& from,const Vec3s& to){return panelBlocks(from,to)||survivalSacBlocks(from,to);};
        auto proposed=player.position;if(!blocks(before,proposed))return;unsigned oldCell=player.currentCell;auto adjusted=before;auto x=before;x[0]=proposed[0];x[1]=proposed[1];if(!blocks(before,x))adjusted[0]=x[0];auto z=adjusted;z[2]=proposed[2];z[1]=proposed[1];if(!blocks(adjusted,z))adjusted[2]=z[2];
        player.position=adjusted;auto wx=unsigned(int(adjusted[0])+player.originX),wz=unsigned(int(adjusted[2])+player.originZ);player.position[1]=short(floorHeight(grid,wx,wz));player.currentCell=grid.index(wx,wz);if(oldCell!=player.currentCell)cells[oldCell].bytes[12]=0;cells[player.currentCell].bytes[12]=255;
    }
    bool showAutoMap(){if(!menu.hasGame||menu.open||!(mem().b(0xa0ab4)&128))return false;syncPauseMap();menu.show(true);menu.page="map";firing=false;grenadeRequested=false;return true;}
    void returnToMainMenu(){menu.exterminationGame=false;if(isolationStage){isolationStage=0;level=root/"GameData/PS1/CD/SECT11/L111LEV.MAP";}finishReplayRecording();explorerSaveSurvivalRun(*this,L"Quit");g_survival={};exitFading=false;exitFadeTicks=0;mission.complete=false;mission.briefing=false;mission.remaining=0;firing=grenadeRequested=useRequested=false;muzzleTicks=0;acceleration.speed=0;notice.clear();noticeTicks=0;hud.stopFiring();hud.takeSounds();menu.show(false);menu.difficultyLocked=false;audio->pause(false);syncMusic();}
    void returnAfterDeath(){if(isolationStage){startIsolation(isolationStage);return;}if(explorerSurvivalDeath(*this))return;menu.achievements.died();queueMovie(explorerDeathMovie(mem().w(0x22d3a8)));exitFading=false;exitFadeTicks=0;mission.complete=false;mission.briefing=false;mission.remaining=0;firing=false;grenadeRequested=false;useRequested=false;muzzleTicks=0;acceleration.speed=0;notice.clear();noticeTicks=0;hud.stopFiring();hud.takeSounds();menu.show(false);}
    void restart(){if(isolationStage){startIsolation(isolationStage);return;}if(g_survival.active||!g_survival.cfg.map.empty()){explorerSurvivalStart(*this,g_survival.cfg.queensRevenge());return;}   /* a Survival run that just ended (died, won) restarts as itself - not as the campaign map of its code */auto saved=entryInventory;auto selected=entryWeapon;auto code=mission.sourceCode;auto it=std::find_if(physicalLevels.begin(),physicalLevels.end(),[&](auto& p){return p.stem().string()=="L"+code+"LEV";});if(it!=physicalLevels.end())level=*it;load();inventory(saved,selected);mission.briefing=true;}
    void checkpoint(unsigned slot,bool save){if(coopGame)throw std::runtime_error("Co-op runs do not use solo save slots.");
        auto dir=checkpointDirectory.empty()?root/"decompilation/user/saves":checkpointDirectory;auto path=dir/("slot"+std::to_string(slot+1)+".sav");
        if(save){if(!mission.complete||!mission.eligible())throw std::runtime_error("Progress can only be saved after passing a mission.");if(!player.health)throw std::runtime_error("Restart the mission before saving a checkpoint.");std::filesystem::create_directories(dir);auto temp=path;temp+=".tmp";std::ofstream f(temp,std::ios::trunc);if(!f)throw std::runtime_error("Cannot write checkpoint");auto next=mission.nextCode();f<<"TRILOGY_NATIVE_PROGRESS 2 "<<(next.empty()?mission.sourceCode:next)<<" "<<difficulty<<" "<<hud.selected()<<" "<<(next.empty()?1:0)<<"\n";for(auto byte:inventory())f<<unsigned(byte)<<" ";f<<"\n";f.close();if(!f||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot finish checkpoint save");menu.message=L"Progress and carried loot saved.";return;}
        auto saved=explorerReadCheckpoint(path);if(!saved)throw std::runtime_error("This slot has no valid native progress save.");auto& code=saved->code;auto mode=saved->difficulty,selected=saved->weapon,finished=saved->finished;auto& data=saved->inventory;auto it=std::find_if(physicalLevels.begin(),physicalLevels.end(),[&](auto& p){return p.stem().string()=="L"+code+"LEV";});if(it==physicalLevels.end())throw std::runtime_error("The saved mission is not installed.");difficulty=mode;menu.difficulty=mode;menu.difficultyLocked=true;if(!menu.root.empty())menu.persist();level=*it;load();inventory(data,selected);mission.briefing=!finished;mission.complete=finished!=0;menu.open=false;menu.hasGame=true;if(!finished&&(code=="211"||code=="311"))queueMovie(code=="211"?"S1_2":"S2_3");
    }

    bool beginExit(bool timed){if(g_survival.active)return false;if(coopGame&&coopExit)return coopExit(timed);
        if(isolationStage&&!exitFading&&exterminationArrival!=~0u&&exterminationPart(player.currentCell)==1){   // on into the second map
            exterminationArrive();   // the same elevator room: its far end is the second map's start, no ride (any time;
                                     // only the second map's exit needs every tag and alien)
            return true;
        }
        if(isolationStage){if(exitFading)return true;if(const auto blocker=exterminationBlocker()){notice=blocker;noticeTicks=90;logic.message=blocker;return false;}exitFading=true;exitFadeTicks=0;firing=false;return true;}
        if(exitFading||mission.complete&&!timed)return true;
        mission.complete=false;menu.levelPassword=explorerPassword({mission.eligible()?inventory():entryInventory,std::min(34u,mission.nextIndex()),difficulty,hud.selected()});exitStatistics=mission.statistics();menu.achievements.campaignExit(mission.sourceCode,mission.eligible(),exitStatistics.percent,difficulty);exitFading=true;exitFadeTicks=0;firing=false;useRequested=false;muzzleTicks=0;acceleration.speed=0;previousPosition=player.position;previousEnemies.clear();hud.stopFiring();for(auto cue:hud.takeSounds())audio->playWeapon(cue);auto m=mem();m.b(0xa0c90,m.b(0xa0c90)|4);m.b(0x9cddd,1);return true;
    }
    unsigned exitBrightness()const{unsigned value=255;for(unsigned i=0;i<std::min(exitFadeTicks,25u);++i)value=value*(25-i)/25;return value;}
    unsigned transitionBrightness()const{
        if(exitFading)return exitBrightness();
        if(arriveTicks)return 255*(25-arriveTicks)/25;
        const unsigned remaining=std::min(g_survival.introTicks,explorer_survival::kIntroFadeTicks);
        return 255*(explorer_survival::kIntroFadeTicks-remaining)/explorer_survival::kIntroFadeTicks;
    }
    void tick(double forward,double strafe,bool sprint=false){if(coopGame&&!coopDriving){if(coopTick)coopTick(forward,strafe,sprint);return;}if(hasMovies()||g_survival.introTicks)return;
        if(!player.health&&!menu.open){if(!coopGame)returnAfterDeath();return;}
        if((!coopGame&&menu.open)||mission.briefing||mission.complete){grenadeRequested=false;return;}
        if(arriveTicks)--arriveTicks;
        if(isolationStage==4&&isolationQueen&&!exitFading&&mem().s(isolationQueen+0x58)<=0){
            if(!queenDownTicks){notice=L"THE QUEEN IS DEAD - EXTRACTION INBOUND";noticeTicks=120;}
            if(++queenDownTicks>=90&&!exterminationBlocker()){exitFading=true;exitFadeTicks=0;firing=false;}
        }else if(isolationStage!=4)queenDownTicks=0;
        if(exitFading){if(++exitFadeTicks>=25){
            if(exterminationRiding){exterminationRiding=exitFading=false;exitFadeTicks=0;exterminationArrive();return;}
            if(isolationStage){
                if(isolationStage<3){saveExtermination(isolationStage+1);startIsolation(isolationStage+1);}
                else if(isolationStage==3){   // down to the Queen's Lair, with what the squad carries
                    const auto carried=inventory();const auto health=player.health,armor=mem().w(0xa0aba);
                    saveExtermination(4);startIsolation(4);
                    inventory(carried);auto m=mem();m.w(0xa0ab8,health);m.w(0xa0aba,armor);player.health=health;hud.syncAmmo();
                }
                else{saveExtermination(0);returnToMainMenu();menu.message=L"EXTERMINATION COMPLETE - SHUTTLE AWAY";}
                return;
            }exitFading=false;mission.complete=true;if(!menu.assets.empty())menu.showResults(exitStatistics.percent,mission.eligible(),mission.minimumPercent());}return;}
        previousPosition=player.position;previousEnemies.clear();auto beforeMemory=mem();for(auto e:enemies->active())previousEnemies[e]={static_cast<short>(beforeMemory.s(e)),static_cast<short>(beforeMemory.s(e+2)),static_cast<short>(beforeMemory.s(e+4))};
        if(!player.health){forward=0;strafe=0;sprint=false;}
        auto m=mem();m.b(0x94b9c,(m.b(0x94b9c)&~36)|(!isolationStage&&!coopGame&&!g_survival.active&&menu.godMode?4:0)|((!isolationStage&&!coopGame&&!g_survival.active&&menu.infiniteAmmo)||activeSurvivalSkull(InfiniteAmmoSkull)?32:0));if(coopWorld&&mission.tick()){beginExit(true);return;}if(coopWorld){auto oldPhase=lighting.phase;int alarm=lighting.tick();if(oldPhase!=lighting.phase)ps1Lighting.load(level,root,int(lighting.phase));ps1Lighting.tick((steps*1664525u+1013904223u)^(steps*22695477u>>8));if(alarm>=0)audio->play(unsigned(alarm),.5f);}if(noticeTicks)--noticeTicks;if(isolationStage)tickExterminationRevive();if(useRequested&&isolationStage){if(useIsolation()){useRequested=false;return;}if(isolationUsed)useRequested=false;}if(useRequested){auto used=logic.use(m,doors,player.position[0],player.position[1],player.position[2],yaw);if(!used.empty()){notice=used;noticeTicks=60;}useRequested=false;}if(exitFading)return;if(coopWorld){auto closures=doors.closed;doors.tick(m,player.position[0],player.position[2],[this](const ExplorerDoor& d){if(!coopGame)return false;auto& other=coopMarines[1-coopLocal];return other.health&&std::hypot(double(other.position[0]-d.x),double(other.position[2]-d.z))<1024;});if(doors.closed!=closures)audio->play(43);}
        if(activeSurvivalSkull(InfiniteAmmoSkull)){
            constexpr unsigned ammunition[]={0xa0abe,0xa0ac2,0xa0ac4,0xa0acc,0xa0aca};
            for(unsigned weapon=0;weapon<5;++weapon)if(hud.owns(weapon))
                m.w(ammunition[weapon],weapon==0?15:100);
            m.w(0xa0ac8,99);m.w(0xa0ad0,99);
        }
        hud.tick(activeSurvivalSkull(TriggerFingerSkull));
        std::memcpy(cells.data(),m.bytes.data()+m.d(0xa0c40),cells.size()*sizeof(LevelCell));
        player.terrainGate=m.w(0xa0abc);player.damageGate=m.w(0xa0ae4);acceleration.terrainGate=player.terrainGate;
        acceleration.maximumSpeed=sprint&&player.health?144:120;acceleration.acceleration=sprint&&player.health?12:9;
        const double speedScale=(activeSurvivalSkull(CrippledSkull)?.75:1.)*
                                (activeSurvivalSkull(ElectricBootsSkull)?2.:1.);
        acceleration.maximumSpeed=short(std::lround(acceleration.maximumSpeed*speedScale*
            1.));
        acceleration.acceleration=short(std::lround(acceleration.acceleration*speedScale));m.b(0xa0ab4,(m.b(0xa0ab4)&253)|(sprint&&player.health?2:0));
        if(forward||strafe){acceleration.terrain=cells[player.currentCell].bytes[10];if(forward>0)accelerateForward(acceleration);else accelerateOtherDirection(acceleration);}else acceleration.speed=0;
        player.moveAngles={0,static_cast<short>(yaw),2048};player.localMovement={static_cast<short>(strafe*acceleration.speed),0,static_cast<short>(forward*acceleration.speed)};
        // N0CL1P: move through an open copy of the grid (map border stays solid) so walls, steps and ceilings never block.
        std::vector<LevelCell> openCells;/* Scratch copy reused across ticks: same contents and the same discarded writes as a fresh copy, without reallocating the whole grid 30 times a second. */static LevelGrid moveGrid;moveGrid=grid;
        if(!isolationStage&&!coopGame&&!g_survival.active&&menu.noClip){openCells.assign(grid.cells.size(),LevelCell{});for(unsigned cz=0;cz<grid.height;++cz)for(unsigned cx=0;cx<grid.width;++cx){auto& c=openCells[cz*grid.width+cx];c.bytes[13]=255;if(cx==0||cz==0||cx+1==grid.width||cz+1==grid.height)c.bytes[7]=0x80;}moveGrid.cells=openCells;}
        movePlayer(player,moveGrid,sine,[&](unsigned address,unsigned a,unsigned b,WorldPlayerState& state,LevelGrid&){if(address==0x3dde8||address==0x3dd24){auto before=state.health;enemies->terrainDamage(a,b,address==0x3dd24);state.health=static_cast<unsigned short>(enemies->health());if(state.health<before){notice=address==0x3dd24?L"Lethal surface":cells[player.currentCell].bytes[10]==8?L"Molten surface damage":L"Corrosive surface damage";noticeTicks=30;}}else ++pending[address];});if(!isolationStage&&!coopGame&&!g_survival.active&&menu.noClip){auto wx=unsigned(int(player.position[0])+player.originX),wz=unsigned(int(player.position[2])+player.originZ);player.position[1]=short(floorHeight(grid,wx,wz));player.currentCell=grid.index(wx,wz);}else resolvePanels(previousPosition);if(coopWorld)++steps;
        if(!player.health){if(!coopGame)returnAfterDeath();return;}
        bool walking=(forward||strafe)&&(player.position[0]!=previousPosition[0]||player.position[2]!=previousPosition[2]);
        previousSwayPhase=swayPhase;previousSwayAmount=swayAmount;double distance=std::hypot(double(player.position[0]-previousPosition[0]),double(player.position[2]-previousPosition[2]));if(walking&&player.health)swayPhase+=distance*Pi/1024;swayAmount+=((walking&&player.health?1.:0.)-swayAmount)*.25;

        if(walking&&cells[player.currentCell].bytes[10]!=9){if(++footstepClock>=(sprint?8u:12u)){audio->play((player.terrainFlags&0x40)?38:44);footstepClock=0;++footsteps;}}else footstepClock=0;
        std::memcpy(m.bytes.data()+m.d(0xa0c40),cells.data(),cells.size()*sizeof(LevelCell));
        mission.visit(m.d(0xa0c40)+player.currentCell*16);for(unsigned axis=0;axis<3;++axis)m.w(0xa0a48+axis*2,player.position[axis]);m.w(0xa0ab8,player.health);m.w(0xa0ba2,yaw);m.w(0xa0ba8,-pitch);if(coopWorld){enemies->tickProjectiles();if(coopGame&&coopRestore)coopRestore();objects.tick();textureAnimation.tick(m,animatedTextures);}player.health=static_cast<unsigned short>(m.w(0xa0ab8));
        auto interaction=logic.tick(m,doors,player.position[0],player.position[1],player.position[2],coopWorld);if(!interaction.empty()){notice=interaction;noticeTicks=60;}
        if(exitFading)return;
        if(!player.health){if(!coopGame)returnAfterDeath();return;}
        auto previousCollected=pickups.collected;auto collectedItem=pickups.tick(player.position[0],player.position[1],player.position[2]);if(pickups.collected!=previousCollected){if(isolationStage&&pickups.lastKind==20)enemies->healExterminationSquad();player.health=static_cast<unsigned short>(m.w(0xa0ab8));hud.syncAmmo();}if(!collectedItem.empty()){notice=collectedItem;noticeTicks=pickups.remainingTicks();}if(isolationStage&&pickups.collected!=previousCollected&&pickups.lastKind==15)exterminationTagFound();
        if(coopEnemyPass)enemies->tick(player.position,player.health);if(coopGame&&coopRestore)coopRestore();player.health=static_cast<unsigned short>(enemies->health());if(!player.health){if(!coopGame)returnAfterDeath();return;}
        audio->listener(float(player.position[0]),float(player.position[2]),float(yaw*2*Pi/4096));auto list=enemies->active();auto contacts=ExplorerHud::contacts(m,list,player.position,yaw);int ping=hud.trackerSound(unsigned(std::count_if(contacts.begin(),contacts.end(),ExplorerHud::onTracker)));if(ping>=0){if(coopSound)coopSound(unsigned(ping),false);if(!coopGame||coopActor==0)audio->play(unsigned(ping),1.f);}
        if(grenadeRequested){grenadeRequested=false;if(hud.canSecondary()&&!enemies->attached()){
            // Extermination: the pulse rifle is all the squad carries, so its grenades go first and then the seismic charges.
            const auto ammo=mem();const bool pulse=hud.selected()==2&&!(isolationStage&&!ammo.w(0xa0ac8)&&ammo.w(0xa0ad0));
            if(enemies->throwGrenade(player.position,yaw,pitch,pulse,player.localMovement[2])){hud.secondaryFire();}else{notice=pulse?L"No pulse rifle grenades":L"No seismic charges";noticeTicks=60;}}}
        if(muzzleTicks)--muzzleTicks;if(firing&&hud.canFire()&&!enemies->attached()){auto before=enemies->shots;enemies->fireWeapon(player.position,yaw,pitch,hud.selected(),player.localMovement[2]);
        if(activeSurvivalSkull(TriggerFingerSkull)&&hud.selected()==4&&enemies->shots!=before)
            enemies->fireWeapon(player.position,yaw,pitch,4,player.localMovement[2]);
        if(enemies->shots!=before){muzzleTicks=2;hud.fire(false);}}
        if(!firing||!hud.ammo||hud.reloadTicks||enemies->attached())hud.stopFiring();
        for(auto sound:hud.takeSounds()){if(coopSound)coopSound(sound,true);if(!coopGame||coopActor==0)audio->playWeapon(sound,sound==11);else audio->playAt(sound,float(player.position[0]),float(player.position[2]));}
        std::memcpy(cells.data(),m.bytes.data()+m.d(0xa0c40),cells.size()*sizeof(LevelCell));
    }
    void draw(HDC dc,int width,int height,ExplorerGpu* gpu=nullptr){
        if(hasMovies()){movie.draw(dc,width,height);return;}
        if(width<=0||height<=0)return;
        if(menu.open){menu.draw(dc,width,height);return;}
        if(mission.briefing){
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=mission.backgroundWidth;info.bmiHeader.biHeight=-int(mission.backgroundHeight);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,0,0,width,height,0,0,mission.backgroundWidth,mission.backgroundHeight,mission.background.data(),&info,DIB_RGB_COLORS,SRCCOPY);
            // 0x80019e54 in SLUS_000.07: PS1 briefing screen uses the small panel font in green.
            double sx=width/512.,sy=height/360.,fs=sy*1.1;auto px=[&](double v){return int(v*sx);};auto py=[&](double v){return int(v*sy);};
            pickupFont.draw(dc,L"Incoming data transfer ...",px(14),py(20),fs,0x33e633);pickupFont.draw(dc,L"Mission brief:",px(14),py(50),fs,0x33e633);
            auto elapsed=GetTickCount64()-briefingStarted;size_t total=std::count_if(mission.markup.begin(),mission.markup.end(),[](wchar_t c){return c>32;}),characters=briefingRevealed?total:std::min<size_t>(elapsed/25,total),shown=0;bool revealed=characters>=total;bool bright=false;int cx=px(14),cy=py(84),limit=px(430),step=std::max(1,py(15)),spaceWidth=pickupFont.width(L" ",fs);
            const std::wstring& src=mission.markup;size_t at=0;
            while(at<src.size()&&shown<characters){
                if(src[at]==L'\n'){cx=px(14);cy+=step;++at;continue;}
                if(src[at]==L' '){cx+=spaceWidth;++at;continue;}
                size_t end=at;std::wstring plain;while(end<src.size()&&src[end]!=L' '&&src[end]!=L'\n'){if(src[end]>2)plain+=src[end];++end;}
                if(cx>px(14)&&cx+pickupFont.width(plain,fs)>limit){cx=px(14);cy+=step;}
                std::wstring run;auto flush=[&]{if(!run.empty()){pickupFont.draw(dc,run,cx,cy,fs,bright?0x4dff4d:0x28a03c);cx+=pickupFont.width(run,fs);run.clear();}};
                for(;at<end&&shown<characters;++at){if(src[at]==1){flush();bright=true;}else if(src[at]==2){flush();bright=false;}else{run+=src[at];++shown;}}
                flush();
            }
            // SLUS_000.07 0x80019c60/0x80019cd4: glyph 0x2a, blink bit 8.
            if((elapsed*30/1000)&8)pickupFont.draw(dc,L"*",cx,cy,fs,0x33e633);
            {if(menu.usingController){menu.drawPrompts(dc,width,py(332),fs,{{0,revealed?L"Continue":L"Reveal briefing"}});}else{std::wstring status=revealed?L"Hit Enter to Continue...":L"Loading data";int fy=py(332),tx=px(56),tw=pickupFont.width(status,fs),sq=std::max(4,px(10));auto square=[&](int left){RECT r{left,fy+px(1),left+sq,fy+px(1)+sq};auto brush=CreateSolidBrush(RGB(255,190,200));FillRect(dc,&r,brush);DeleteObject(brush);};square(px(26));pickupFont.draw(dc,status,tx,fy,fs,0xf0f0f0);square(tx+tw+px(14));}}
            return;
        }
        RECT all{0,0,width,height};auto background=CreateSolidBrush(RGB(3,7,7));if(!gpu)FillRect(dc,&all,background);DeleteObject(background);
        double renderStarted=renderClock();
        Vec3s viewPosition;
        std::array<double,3> camPos{};
        int renderYaw=yaw,renderPitch=pitch,renderRoll=0;
        double focal=0,bobPhase=0,bobAmount=0;
        if(photoMode){
            camPos={photoX,photoY,photoZ};
            viewPosition={static_cast<short>(std::lround(photoX)),static_cast<short>(std::lround(photoY)),static_cast<short>(std::lround(photoZ))};
            renderYaw=photoYaw;renderPitch=photoPitch;renderRoll=photoRoll;
            focal=width*.5/std::tan(photoFov*Pi/360.);
        }else{
            viewPosition=player.position;if(interpolate)for(unsigned i=0;i<3;++i)viewPosition[i]=static_cast<short>(previousPosition[i]+(player.position[i]-previousPosition[i])*renderAlpha);
            if(replaying){
                renderYaw=replayViewYaw();renderPitch=replayViewPitch();
            }
    
    
            bobPhase=interpolate?previousSwayPhase+(swayPhase-previousSwayPhase)*renderAlpha:swayPhase;bobAmount=menu.sway&&player.health?(interpolate?previousSwayAmount+(swayAmount-previousSwayAmount)*renderAlpha:swayAmount):0;
            double side=std::sin(bobPhase)*8*bobAmount;viewPosition[0]=short(viewPosition[0]-std::cos(renderYaw*2*Pi/4096)*side);viewPosition[2]=short(viewPosition[2]-std::sin(renderYaw*2*Pi/4096)*side);viewPosition[1]=short(viewPosition[1]+std::sin(bobPhase*2)*12*bobAmount);
            camPos={double(viewPosition[0]),double(viewPosition[1]),double(viewPosition[2])};
            focal=width*.5/std::tan(menu.fov*Pi/360.);
        }


        aimFocal=focal;/* Per-frame working copies reuse their storage (no whole-level allocation every frame). */static std::vector<Face> drawFaces;drawFaces.clear();static std::vector<Point> drawVertices;drawVertices.assign(vertices.begin(),vertices.end());
        for(auto& door:doors.entries){
            float open=interpolate?door.previous+(door.amount-door.previous)*float(renderAlpha):door.amount;
            auto found=doorMeshes.find(mem().b(door.record+7));if(found==doorMeshes.end())continue;const auto& mesh=found->second;int base=int(drawVertices.size());
            double angle=Pi-door.rotation*Pi/4,cs=std::cos(angle),sn=std::sin(angle);
            const bool survivalElevator=g_survival.active&&std::any_of(
                g_survival.doorEntry.begin(),g_survival.doorEntry.end(),[&](unsigned index){
                    return &doors.entries[index]==&door;
                });
            // Survival's four-tile opening is wider than the campaign panel.
            const double widthScale=survivalElevator?1184.0/896:1.0;
            for(auto v:mesh.vertices){
                const double x=v.x*widthScale;
                drawVertices.push_back({door.x+x*cs+v.z*sn,
                    door.floor+(survivalElevator?0:16)+v.y+(door.ceiling-door.floor)*open,
                    door.z-x*sn+v.z*cs});
            }
            for(auto face:mesh.faces){for(auto& index:face.vertices)if(index>=0)index+=base;drawFaces.push_back(face);}
        }
        auto appendModel=[&](const Mesh& mesh,double x,double y,double z,double angle,double scale,unsigned atlas,double roll=0){int base=int(drawVertices.size());double cs=std::cos(angle),sn=std::sin(angle);for(auto v:mesh.vertices){double vx=v.x*std::cos(roll)-v.y*std::sin(roll),vy=v.x*std::sin(roll)+v.y*std::cos(roll);drawVertices.push_back({x+(vx*cs+v.z*sn)*scale,y+vy*scale,z+(-vx*sn+v.z*cs)*scale});}for(auto f:mesh.faces){f.atlas=atlas;for(auto& i:f.vertices)if(i>=0)i+=base;drawFaces.push_back(f);}};
        if(g_survival.active&&g_survival.queenEntity&&!survivalQueenSac.faces.empty()){
            const auto& anchor=g_survival.queenSacAnchor;
            // Align the original sac's diagonal spine directly behind the Queen.
            appendModel(survivalQueenSac,anchor[0],anchor[1],anchor[2],SurvivalSacAngle,1,0);
        }
        if(isolationStage){
            auto furnish=[&](double x,double y,double z,double width,double height,double depth,
                             unsigned front,unsigned side,unsigned top,bool turned=false){
                if(turned)std::swap(width,depth);   // facing along x: its front on the x faces
                const int base=int(drawVertices.size());
                for(double level:{y,y+height}){
                    drawVertices.insert(drawVertices.end(),{
                        {x-width/2,level,z-depth/2},{x+width/2,level,z-depth/2},
                        {x+width/2,level,z+depth/2},{x-width/2,level,z+depth/2},
                    });
                }
                constexpr std::array<std::array<int,4>,5> sides{{
                    {1,0,4,5},{2,1,5,6},{3,2,6,7},{0,3,7,4},{7,6,5,4}
                }};
                const std::array paint=turned?std::array{side,front,side,front,top}:std::array{front,side,front,side,top};
                for(unsigned i=0;i<sides.size();++i){
                    Face face{};face.model=true;face.light=9;face.texture=paint[i];
                    for(unsigned v=0;v<4;++v)face.vertices[v]=base+sides[i][v];
                    drawFaces.push_back(face);
                }
            };
            auto locker=[&](double x,double y,double z,bool turned=false){
                const auto& t=exterminationLocker;
                furnish(x,y,z,400,820,320,t[0],t[1],t[1],turned);
                furnish(x,y+820,z,400,400,320,t[2],t[1],t[1],turned);
            };
            for(const auto& panel:isolationPanels){
                const int wx=int(panel.cell%grid.width)*512+256,wz=int(panel.cell/grid.width)*512+256;
                const double x=wx-player.originX,z=wz-player.originZ,y=floorHeight(grid,wx,wz);
                furnish(x,y,z,260,640,220,122,125,122);
                furnish(x,y+640,z,330,355,250,doors.entries[panel.door].target?139:140,152,125);
            }
            for(const auto& fixture:isolationFixtures){
                const int wx=int(fixture.cell%grid.width)*512+256,wz=int(fixture.cell/grid.width)*512+256;
                const double x=wx-player.originX,z=wz-player.originZ,y=floorHeight(grid,wx,wz);
                if(fixture.container==4)locker(x,y,z);
                else if(fixture.container==5){
                    furnish(x,y,z,460,550,380,228,237,122);
                    furnish(x,y+550,z,400,490,300,140,152,125);
                }else{
                    furnish(x,y,z,470,280,345,304,305,129);
                    furnish(x,y+280,z,470,125,345,51,51,46);
                }
            }
            for(const auto& tag:isolationTags){
                if(tag.container>=9)continue;   // inside the map's own crate or locker (drawn as objects)
                const auto prop=panelMeshes.find(1);   // the deck's box crate model (M001)
                const int wx=int(tag.cell%grid.width)*512+256,wz=int(tag.cell/grid.width)*512+256;
                constexpr std::array<Point,3> sizes{{{448,480,448},{400,1220,360},{520,560,360}}};
                const auto size=sizes[tag.container];
                // Flush against the wall behind it (a 4-unit gap), its front to the room: the map objects' facing
                // (angle pi - facing*pi/4, facing 0 toward -z, 2 +x, 4 +z, 6 -x).
                constexpr int toward[5][2]{{0,0},{0,-1},{0,1},{-1,0},{1,0}};
                constexpr unsigned facing[5]{0,4,0,2,6};
                const bool turned=tag.side>=3;
                const double back=tag.side?256-(tag.container==1?320:size.z)/2-4:0;
                const double x=wx-player.originX+toward[tag.side][0]*back,z=wz-player.originZ+toward[tag.side][1]*back;
                const double y=floorHeight(grid,wx,wz);
                if(tag.container==1){locker(x,y,z,turned);continue;}
                if(prop==panelMeshes.end()||prop->second.vertices.empty()){   // no crate model: a plain cabinet
                    const auto& t=exterminationLocker;
                    furnish(x,y,z,size.x,size.y,size.z,t[1],t[1],t[2],turned);
                    const auto badge=pickupMeshes.find(15);
                    if(!tag.collected&&tag.container==2&&badge!=pickupMeshes.end())appendModel(badge->second,x,y+size.y+40,z,0,1,2);
                    continue;
                }
                const auto first=drawVertices.size();
                appendModel(prop->second,x,y,z,Pi-facing[tag.side]*Pi/4,1,2);
                Point low=drawVertices[first],high=low;   // the turned model's bounds, scaled to the prop's size
                for(size_t i=first;i<drawVertices.size();++i){
                    const auto& v=drawVertices[i];
                    low.x=std::min(low.x,v.x-x);high.x=std::max(high.x,v.x-x);
                    low.y=std::min(low.y,v.y-y);high.y=std::max(high.y,v.y-y);
                    low.z=std::min(low.z,v.z-z);high.z=std::max(high.z,v.z-z);
                }
                const double sizeX=turned?size.z:size.x,sizeZ=turned?size.x:size.z;
                const auto scaled=[](double value,double origin,double low,double high,double size,double anchor){
                    return origin+((value-origin-low)/std::max(1.,high-low)-anchor)*size;
                };
                for(size_t i=first;i<drawVertices.size();++i){
                    auto& v=drawVertices[i];
                    v.x=scaled(v.x,x,low.x,high.x,sizeX,.5);
                    v.y=scaled(v.y,y,low.y,high.y,size.y,0);
                    v.z=scaled(v.z,z,low.z,high.z,sizeZ,.5);
                }
                const auto badge=pickupMeshes.find(15);
                if(!tag.collected&&tag.container==2&&badge!=pickupMeshes.end())
                    appendModel(badge->second,x,y+size.y+40,z,0,1,2);
            }
        }
        for(auto i:objects.entries){if(!objects.visible(i))continue;auto m=mem();auto p=0x320000+i*40;unsigned kind=m.b(p+24);if(kind==21||kind==28||kind==29)continue;unsigned model=kind==23?0:kind==25?2:kind==33?35:kind==20&&alienPanels?41:1;auto found=panelMeshes.find(model);if(found!=panelMeshes.end())appendModel(found->second,m.s(p),m.s(p+2),m.s(p+4),Pi-m.w(p+34)*Pi/4,kind==23?1:.875,kind==33?1:kind==20&&alienPanels?3:2);}
        for(auto& lift:logic.lifts){auto found=liftMeshes.find(lift.model);if(found!=liftMeshes.end()){double y=interpolate?lift.previous+(lift.y-lift.previous)*renderAlpha:lift.y;appendModel(found->second,lift.x,y+16,lift.z,0,1,0);}}
        for(auto& panel:logic.panels){unsigned model=panel.kind==32?40:panel.kind==22?13:panel.kind==24?5:panel.kind==26?17:9;if(panel.kind!=32){if(logic.panelOn(mem(),panel.index))++model;if(alienPanels)model+=16;}appendModel(panelMeshes.at(model),panel.x,panel.y,panel.z,Pi-panel.rotation*Pi/4,panel.kind==32?1:.875,panel.kind==32?2:alienPanels?3:1,panel.kind==32?mem().s(0x320000+panel.index*40+12)*2*Pi/4096:0);}
        for(auto item:pickups.active()){unsigned kind=mem().b(item+32);auto mesh=pickupMeshes.find(kind);if(mesh==pickupMeshes.end())continue;auto m=mem();int scale=m.d(0x9cb74+kind*16);appendModel(mesh->second,m.s(item),m.s(item+2),m.s(item+4),m.w(item+18)*2*Pi/4096,std::clamp(scale/4096.0,.1,2.0),2);}
        for(auto p=mem().d(0x236300);p;p=mem().d(p+36)){auto m=mem();unsigned kind=m.b(p+30);if(kind!=12&&kind!=13&&kind!=19)continue;unsigned meshKind=kind==19?6:12;auto mesh=pickupMeshes.find(meshKind);if(mesh==pickupMeshes.end())continue;auto pos=m.d(p);int scale=m.d(0x9cb74+meshKind*16);appendModel(mesh->second,m.s(pos),m.s(pos+2),m.s(pos+4),m.w(p+18)*2*Pi/4096,std::clamp(scale/4096.0,.1,2.0),2);}
        // Original effect textures for the native steam/flame projectile pools.
        for(auto p=mem().d(0x236300);p;p=mem().d(p+36)){auto m=mem();unsigned kind=m.b(p+30);if(kind!=14&&kind!=16&&kind!=17&&kind!=18)continue;unsigned age=32-std::min(32u,unsigned(m.b(p+28)));bool steam=kind==14||kind==18;double halfWidth=steam?64+age*3:64+age*2,halfHeight=steam?40+age*2:96+age*3;auto pos=m.d(p);double x=m.s(pos),y=m.s(pos+2),z=m.s(pos+4),angle=renderYaw*2*Pi/4096,dx=std::cos(angle)*halfWidth,dz=std::sin(angle)*halfWidth;int base=int(drawVertices.size());drawVertices.push_back({x-dx,y-halfHeight,z-dz});drawVertices.push_back({x+dx,y-halfHeight,z+dz});drawVertices.push_back({x+dx,y+halfHeight,z+dz});drawVertices.push_back({x-dx,y+halfHeight,z-dz});Face f{};f.vertices={base,base+1,base+2,base+3};f.model=true;f.atlas=4;f.texture=(steam?24:11)+std::min(5u,age/5);drawFaces.push_back(f);}
        for(auto& effect:objects.effects){auto angle=renderYaw*2*Pi/4096;double dx=std::cos(angle)*512,dz=std::sin(angle)*512;int base=int(drawVertices.size());drawVertices.push_back({effect.x-dx,double(effect.y),effect.z-dz});drawVertices.push_back({effect.x+dx,double(effect.y),effect.z+dz});drawVertices.push_back({effect.x+dx,double(effect.y+1024),effect.z+dz});drawVertices.push_back({effect.x-dx,double(effect.y+1024),effect.z-dz});Face face{};face.vertices={base,base+1,base+2,base+3};face.model=true;face.atlas=4;face.texture=effect.barrel?11+std::min(6u,effect.age/2):32+std::min(2u,effect.age/6);drawFaces.push_back(face);}
        static std::vector<Point> projected;
        static std::vector<unsigned char> projectedReady;
        projected.resize(drawVertices.size());
        projectedReady.assign(drawVertices.size(),0);
        /* cameraPoint() recomputed up to six cos/sin per vertex for angles that are constant
           across the frame; compute them once and apply its exact per-vertex formulas. */
        const double camAngle=renderYaw*2*Pi/4096,camCs=std::cos(camAngle),camSn=std::sin(camAngle);const double camP=renderPitch*2*Pi/4096,camCp=std::cos(camP),camSp=std::sin(camP);const bool camRolled=renderRoll!=0;const double camR=renderRoll*2*Pi/4096,camRc=camRolled?std::cos(camR):0.0,camRs=camRolled?std::sin(camR):0.0;
        auto toCamera=[&](const Point& v){
            const double x=v.x-camPos[0],z=v.z-camPos[2],y=v.y-camPos[1]-768;
            const double depth=-x*camSn+z*camCs;
            const double cx=-(x*camCs+z*camSn),cy=y*camCp-depth*camSp,cz=y*camSp+depth*camCp;
            return camRolled?Point{cx*camRc-cy*camRs,cx*camRs+cy*camRc,cz}:Point{cx,cy,cz};
        };
        hiddenGroups.assign(faceSpheres.size(),0);
        const bool useFaceGroups=cullStaticFaces&&faceGroups.size()==faces.size();
        if(useFaceGroups){
            const double hx=width*.5+1,hy=height*.5+1;
            const double nx=std::hypot(focal,hx),ny=std::hypot(focal,hy);
            for(unsigned i=0;i<faceSpheres.size();++i){
                const auto& sphere=faceSpheres[i];
                const auto p=toCamera(sphere.center);
                const double r=sphere.radius;
                hiddenGroups[i]=r<0||p.z+r<32||
                    std::abs(p.x)*focal-hx*p.z>r*nx||
                    std::abs(p.y)*focal-hy*p.z>r*ny;
            }
        }

        renderStages[0]=renderClock()-renderStarted;renderStarted=renderClock();
        struct Polygon {std::array<POINT,8> points;std::array<std::array<double,2>,8> precisePoints;bool precise=false;std::array<double,8> depths;std::array<std::array<double,2>,8> uv;std::array<std::array<double,3>,8> colors;unsigned count=0;bool nativeLighting=false,directColor=false,fog=true,additive=false,translucent=false;double depth;COLORREF color;const std::vector<unsigned>* texture=nullptr;};static std::vector<Polygon> polygons;polygons.clear();polygons.reserve(faces.size()+drawFaces.size());auto m=mem();
        bool lampActive=m.w(0xa0ad6)>0||(m.w(0xa0ab4)&0x100)||(photoMode&&photoLamp),nvActive=m.w(0xa0ada)>0||(m.w(0xa0ab4)&0x200);
        auto boostLight=[&](Point& pt){
            if(nvActive){pt.color[0]=std::max(pt.color[0],170.0);pt.color[1]=std::max(pt.color[1],235.0);pt.color[2]=std::max(pt.color[2],170.0);}
            if(lampActive&&pt.z>0&&pt.z<3584){double radial=std::hypot(pt.x,pt.y),cone=std::clamp(1.0-radial/(pt.z*0.95+256.0),0.0,1.0),atten=std::clamp(1.0-pt.z/3584.0,0.0,1.0),add=150.0*cone*atten;for(unsigned c=0;c<3;++c)pt.color[c]=std::min(255.0,pt.color[c]+add);}
        };
        for(size_t index=0;index<faces.size()+drawFaces.size();++index){
            if(useFaceGroups&&index<faces.size()&&hiddenGroups[faceGroups[index]])continue;
            const auto& face=index<faces.size()?faces[index]:drawFaces[index-faces.size()];
            for(int vertex:face.vertices)if(vertex>=0&&!projectedReady[vertex]){
                projected[vertex]=toCamera(drawVertices[vertex]);
                projectedReady[vertex]=1;
            }
            const auto& faceLighting=face.survivalSac?survivalSacLighting:ps1Lighting;{bool inFront=false;for(unsigned i=0;i<4;++i)if(face.vertices[i]>=0&&projected[face.vertices[i]].z>=32){inFront=true;break;}if(!inFront)continue;}auto& atlas=face.atlas==4?effectTextures:face.atlas==1?panelTextures:face.atlas==2?batteryTextures:face.atlas==3?alienPanelTextures:textures;std::array<Point,4> input;unsigned inputCount=0;unsigned texture=(face.flags==8||face.flags==10||face.flags==26||face.flags==28||face.flags==30||face.flags==32)&&face.texture<animatedTextures.size()?animatedTextures[face.texture]:face.texture;auto rect=atlas.rect(texture);std::array<std::array<double,2>,4> uv{};if(rect){double x0=rect->x,y0=rect->y,x1=x0+rect->width-1,y1=y0+rect->height-1;if(face.model&&face.flags==2)uv={{{x0,y1},{x1,y0},{x0,y0},{x0,y0}}};else if(face.model&&face.flags==11)uv={{{x1,y1},{x0,y1},{x0,y0},{x1,y0}}};else if(face.vertices[3]<0)uv={{{x0,y0},{x1,y0},{x0,y1},{x0,y1}}};else if(face.flags==2)uv={{{x1,y1},{x0,y1},{x0,y0},{x1,y0}}};else uv={{{x0,y1},{x1,y1},{x1,y0},{x0,y0}}};}
            const bool translucent=face.flags==12||face.flags==13||face.flags==14||
                                   face.flags==30||face.flags==32||face.flags==34;
            const bool nativeLighting=(faceLighting.active&&!face.model)||lampActive||nvActive;
            const auto* page=rect?&atlas.pages.at(rect->page):nullptr;
            auto flatColor=[&](double depth){
                const auto light=m.d(0xa0bc8+0xb0)+face.light*28;
                const double shade=std::clamp(2400.0/(depth+1200),.15,1.0);
                const auto alarm=lighting.active?lighting.color(face.sourceFace,face.light):std::array<unsigned char,3>{};
                const int lamp=lampActive&&depth<3584?int((1-depth/3584)*120):0;
                auto channel=[&](unsigned i){
                    if(nvActive)return i==1?235:170;
                    if(face.atlas==4)return 128;
                    if(face.model&&face.atlas==2)return std::min(255,int(128*shade)+lamp);
                    const auto value=lighting.active?std::clamp(alarm[i]*1.25*shade+4,4.,255.)
                        :std::clamp((m.b(light+i)*2+70)*shade+12,24.,210.);
                    return std::min(255,int(value)+lamp);
                };
                return RGB(channel(0),channel(1),channel(2));
            };
            for(unsigned i=0;i<4;++i)if(face.vertices[i]>=0){auto point=projected[face.vertices[i]];point.u=uv[i][0];point.v=uv[i][1];if(faceLighting.active&&!face.model){auto rgb=faceLighting.color(face.sourceFace,i);for(unsigned c=0;c<3;++c)point.color[c]=rgb[c];}boostLight(point);input[inputCount++]=point;}            auto screenPoint=[&](const Point& v){
                return std::array{std::clamp(width*.5+v.x*focal/v.z,-100000.,100000.),
                                  std::clamp(height*.5-v.y*focal/v.z,-100000.,100000.)};
            };
            auto submitOpaque=[&](const Point* points,unsigned count){
                std::array<ExplorerGpuVertex,8> vertices;
                unsigned outside=15;
                double depth=0;
                for(unsigned i=0;i<count;++i){
                    const auto& v=points[i];
                    const auto [x,y]=screenPoint(v);
                    outside&=(int(x)<0?1u:0u)|(int(x)>width?2u:0u)|(int(y)<0?4u:0u)|(int(y)>height?8u:0u);
                    vertices[i]={float(x),float(y),float(v.z),float((v.u+.5)/256),float((v.v+.5)/256),
                        float(v.color[0]/128),float(v.color[1]/128),float(v.color[2]/128),1.f};
                    depth+=v.z;
                }
                if(outside)return;
                if(!nativeLighting){
                    const auto color=flatColor(depth/count);
                    for(unsigned i=0;i<count;++i){
                        vertices[i].r=GetRValue(color)/128.f;
                        vertices[i].g=GetGValue(color)/128.f;
                        vertices[i].b=GetBValue(color)/128.f;
                    }
                }
                gpu->opaqueFan(page,vertices.data(),count,!nvActive);
            };
            const bool directOpaque=gpu&&fastOpaqueFaces&&!translucent;
            if(directOpaque&&std::all_of(input.begin(),input.begin()+inputCount,[](const Point& v){return v.z>=32;})){
                if(inputCount>=3)submitOpaque(input.data(),inputCount);
                continue;
            }
            std::array<Point,8> clipped;unsigned clippedCount=0;
            for(unsigned i=0;i<inputCount;++i){auto a=input[i],b=input[(i+1)%inputCount];bool inside=a.z>=32,next=b.z>=32;if(inside)clipped[clippedCount++]=a;if(inside!=next){double t=(32-a.z)/(b.z-a.z);Point intersection{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,32,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t};for(unsigned c=0;c<3;++c)intersection.color[c]=a.color[c]+(b.color[c]-a.color[c])*t;clipped[clippedCount++]=intersection;}}
            if(clippedCount<3)continue;
            if(directOpaque){submitOpaque(clipped.data(),clippedCount);continue;}
            Polygon p;
            p.translucent=translucent;p.nativeLighting=nativeLighting;p.fog=!nvActive;
            double depth=0;
            bool left=true,right=true,above=true,below=true;
            for(unsigned corner=0;corner<clippedCount;++corner){
                const auto& v=clipped[corner];
                const auto [px,py]=screenPoint(v);
                const int x=int(px),y=int(py);
                p.precise=true;p.precisePoints[corner]={px,py};p.points[corner]={x,y};
                p.depths[corner]=v.z;p.uv[corner]={v.u,v.v};p.colors[corner]=v.color;
                depth+=v.z;left&=x<0;right&=x>width;above&=y<0;below&=y>height;
            }
            if(left||right||above||below)continue;
            p.count=clippedCount;p.depth=depth/clippedCount;p.color=flatColor(p.depth);p.texture=page;
            polygons.push_back(std::move(p));
        }
        lastFlameSprites=lastFlamePixels=0;
        for(auto projectile=m.d(0x236300);projectile;projectile=m.d(projectile+36)){auto glow=explorerFlameGlow(m,projectile,projectile*1664525u+steps*1013904223u);if(!glow)continue;auto pos=m.d(projectile);auto point=cameraPoint({double(m.s(pos)),double(m.s(pos+2)),double(m.s(pos+4))},camPos,renderYaw,renderPitch,renderRoll);if(point.z<32||point.z>6144)continue;double x=width*.5+point.x*focal/point.z,y=height*.5-point.y*focal/point.z;if(x<0||x>width||y<0||y>height)continue;auto rect=effectTextures.rect(glow->frame);if(!rect)continue;double half=glow->halfSize*focal/point.z;Polygon p;p.count=4;p.nativeLighting=true;p.fog=false;p.additive=true;p.depth=point.z;p.color=RGB(glow->color[0],glow->color[1],glow->color[2]);p.texture=&effectTextures.pages.at(rect->page);p.points={POINT{LONG(x-half),LONG(y+half)},POINT{LONG(x+half),LONG(y+half)},POINT{LONG(x+half),LONG(y-half)},POINT{LONG(x-half),LONG(y-half)}};p.uv[0]={double(rect->x),double(rect->y+rect->height-1)};p.uv[1]={double(rect->x+rect->width-1),double(rect->y+rect->height-1)};p.uv[2]={double(rect->x+rect->width-1),double(rect->y)};p.uv[3]={double(rect->x),double(rect->y)};for(unsigned i=0;i<4;++i){p.depths[i]=point.z;for(unsigned c=0;c<3;++c)p.colors[i][c]=glow->color[c];}polygons.push_back(std::move(p));++lastFlameSprites;
        }
        lastBulletTrails=lastBulletTrailPixels=0;
        std::vector<std::tuple<ExplorerBulletTrail,Point,Point>> trails;
        for(auto projectile=m.d(0x236300);projectile;projectile=m.d(projectile+36)){auto spec=explorerBulletTrail(m,projectile);if(!spec)continue;Point start{double(spec->start[0]),double(spec->start[1]),double(spec->start[2])},end{double(spec->end[0]),double(spec->end[1]),double(spec->end[2])};if(interpolate&&m.b(projectile+28)<m.b(projectile+29)){auto velocity=m.d(projectile+4);double alpha=1-renderAlpha;start.x-=m.s(velocity)*alpha;start.y-=m.s(velocity+2)*alpha;start.z-=m.s(velocity+4)*alpha;end.x-=m.s(velocity)*alpha;end.y-=m.s(velocity+2)*alpha;end.z-=m.s(velocity+4)*alpha;}trails.push_back({*spec,start,end});}
        if(isolationStage)for(const auto& round:enemies->squadRounds){   // the marines' pulse rounds, cyan as the player's
            const double dx=round.to[0]-round.from[0],dy=round.to[1]-round.from[1],dz=round.to[2]-round.from[2],length=std::max(1.,std::sqrt(dx*dx+dy*dy+dz*dz));
            const double head=std::min(length,(round.age+(interpolate?renderAlpha:1.))*ExplorerEnemies::SquadRoundSpeed),tail=std::max(0.,head-ExplorerEnemies::SquadRoundLength);
            if(tail>=length)continue;
            ExplorerBulletTrail trail{};trail.color={40,220,255};trail.endColor={255,255,255};trail.lines=2;
            trails.push_back({trail,Point{round.from[0]+dx*tail/length,round.from[1]+dy*tail/length,round.from[2]+dz*tail/length},
                                    Point{round.from[0]+dx*head/length,round.from[1]+dy*head/length,round.from[2]+dz*head/length}});
        }
        for(const auto& [trail,start,end]:trails){const auto* spec=&trail;auto a=cameraPoint(start,camPos,renderYaw,renderPitch,renderRoll),b=cameraPoint(end,camPos,renderYaw,renderPitch,renderRoll);if(a.z<512||a.z>6144||b.z<32)continue;double ax=width*.5+a.x*focal/a.z,ay=height*.5-a.y*focal/a.z,bx=width*.5+b.x*focal/b.z,by=height*.5-b.y*focal/b.z;if(ax<0||ay<0||ax>width||ay>height)continue;double thickness=std::max(1.,height/240.),length=std::hypot(bx-ax,by-ay);if(length<.5){bx=ax;by=ay+1;length=1;}double nx=-(by-ay)/length*thickness*.5,ny=(bx-ax)/length*thickness*.5;
            for(unsigned line=0;line<spec->lines;++line){Polygon p;p.count=4;p.nativeLighting=p.directColor=true;p.fog=false;p.uv.fill({0,0});p.depth=(a.z+b.z)*.5;p.color=RGB(spec->color[0],spec->color[1],spec->color[2]);double offset=line*thickness;p.points={POINT{LONG(std::lround(ax+nx+offset)),LONG(std::lround(ay+ny+offset))},POINT{LONG(std::lround(bx+nx+offset)),LONG(std::lround(by+ny+offset))},POINT{LONG(std::lround(bx-nx+offset)),LONG(std::lround(by-ny+offset))},POINT{LONG(std::lround(ax-nx+offset)),LONG(std::lround(ay-ny+offset))}};p.depths[0]=p.depths[3]=a.z;p.depths[1]=p.depths[2]=b.z;for(unsigned c=0;c<3;++c){p.colors[0][c]=p.colors[3][c]=spec->color[c];p.colors[1][c]=p.colors[2][c]=spec->endColor[c];}polygons.push_back(std::move(p));++lastBulletTrails;}
        }
        renderStages[1]=renderClock()-renderStarted;renderStarted=renderClock();
        // Host depth buffer masks original sprite pixels against the level.
        int depthScale=menu.renderQuality?1:2;int depthWidth=(width+depthScale-1)/depthScale,depthHeight=(height+depthScale-1)/depthScale;
        std::vector<float> depthBuffer(gpu?0:static_cast<size_t>(depthWidth)*depthHeight,1e30f);
        std::vector<unsigned> levelPixels(gpu?0:static_cast<size_t>(depthWidth)*depthHeight,0x00030707);
        if(!gpu){
        std::sort(polygons.begin(),polygons.end(),[](const auto& a,const auto& b){return a.additive!=b.additive?!a.additive:a.depth<b.depth;});
        for(auto& polygon:polygons)if(!polygon.translucent)for(unsigned i=1;i+1<polygon.count;++i){
            auto a=polygon.points[0],b=polygon.points[i],c=polygon.points[i+1];for(auto point:{&a,&b,&c}){point->x/=depthScale;point->y/=depthScale;}double area=double(b.x-a.x)*(c.y-a.y)-double(b.y-a.y)*(c.x-a.x);if(std::abs(area)<.01)continue;
            int x0=std::clamp(std::min({a.x,b.x,c.x}),0L,LONG(depthWidth-1)),x1=std::clamp(std::max({a.x,b.x,c.x}),0L,LONG(depthWidth-1));
            int y0=std::clamp(std::min({a.y,b.y,c.y}),0L,LONG(depthHeight-1)),y1=std::clamp(std::max({a.y,b.y,c.y}),0L,LONG(depthHeight-1));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){double u=(double(b.x-x)*(c.y-y)-double(b.y-y)*(c.x-x))/area,v=(double(c.x-x)*(a.y-y)-double(c.y-y)*(a.x-x))/area,w=1-u-v;
                if(u<0||v<0||w<0)continue;double reciprocal=u/polygon.depths[0]+v/polygon.depths[i]+w/polygon.depths[i+1];if(reciprocal>0){auto& z=depthBuffer[y*depthWidth+x];float depth=float(1/reciprocal);if(depth<z){unsigned color=(unsigned(GetRValue(polygon.color))<<16)|(unsigned(GetGValue(polygon.color))<<8)|GetBValue(polygon.color);if(polygon.directColor){auto channel=[&](unsigned c){return unsigned(std::clamp(u*polygon.colors[0][c]+v*polygon.colors[i][c]+w*polygon.colors[i+1][c],0.,255.));};color=(channel(0)<<16)|(channel(1)<<8)|channel(2);}if(polygon.texture){double tx=(u*polygon.uv[0][0]/polygon.depths[0]+v*polygon.uv[i][0]/polygon.depths[i]+w*polygon.uv[i+1][0]/polygon.depths[i+1])*depth,ty=(u*polygon.uv[0][1]/polygon.depths[0]+v*polygon.uv[i][1]/polygon.depths[i]+w*polygon.uv[i+1][1]/polygon.depths[i+1])*depth;auto tex=(*polygon.texture)[std::clamp(int(ty),0,255)*256+std::clamp(int(tx),0,255)];if(!(tex>>24))continue;auto lightChannel=[&](unsigned channel,unsigned fallback){return polygon.nativeLighting?unsigned(std::clamp(u*polygon.colors[0][channel]+v*polygon.colors[i][channel]+w*polygon.colors[i+1][channel],0.0,255.0)):fallback;};auto r=std::min(255u,((tex>>16)&255)*lightChannel(0,GetRValue(polygon.color))/128),g=std::min(255u,((tex>>8)&255)*lightChannel(1,GetGValue(polygon.color))/128),b=std::min(255u,(tex&255)*lightChannel(2,GetBValue(polygon.color))/128);color=(r<<16)|(g<<8)|b;}if(polygon.directColor)++lastBulletTrailPixels;color=polygon.fog?explorerFogPixel(color,depth):color;if(polygon.additive){auto previous=levelPixels[y*depthWidth+x];auto r=std::min(255u,((previous>>16)&255)+((color>>16)&255)),g=std::min(255u,((previous>>8)&255)+((color>>8)&255)),b=std::min(255u,(previous&255)+(color&255));levelPixels[y*depthWidth+x]=(r<<16)|(g<<8)|b;++lastFlamePixels;}else{levelPixels[y*depthWidth+x]=color;z=depth;}}}}
        }
        // One software colour/depth pass replaces thousands of slow GDI polygons.
        if(photoMode&&photoFilter)for(auto& px:levelPixels)px=applyPhotoFilter(px,photoFilter);
        BITMAPINFO levelInfo{};levelInfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);levelInfo.bmiHeader.biWidth=depthWidth;levelInfo.bmiHeader.biHeight=-depthHeight;levelInfo.bmiHeader.biPlanes=1;levelInfo.bmiHeader.biBitCount=32;
        SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,0,0,width,height,0,0,depthWidth,depthHeight,levelPixels.data(),&levelInfo,DIB_RGB_COLORS,SRCCOPY);
        }else{for(auto& polygon:polygons)for(unsigned i=1;i+1<polygon.count;++i){std::array<ExplorerGpuVertex,3> triangle{};unsigned indices[]={0,i,i+1};for(unsigned c=0;c<3;++c){unsigned k=indices[c];auto& v=triangle[c];v.x=float(polygon.precise?polygon.precisePoints[k][0]:polygon.points[k].x);v.y=float(polygon.precise?polygon.precisePoints[k][1]:polygon.points[k].y);v.z=float(polygon.depths[k]);v.u=float((polygon.uv[k][0]+.5)/256);v.v=float((polygon.uv[k][1]+.5)/256);v.r=float((polygon.nativeLighting?polygon.colors[k][0]:GetRValue(polygon.color))/(polygon.directColor?255.:128.));v.g=float((polygon.nativeLighting?polygon.colors[k][1]:GetGValue(polygon.color))/(polygon.directColor?255.:128.));v.b=float((polygon.nativeLighting?polygon.colors[k][2]:GetBValue(polygon.color))/(polygon.directColor?255.:128.));v.a=polygon.translucent?.5f:1.f;}gpu->triangle(polygon.texture,triangle,polygon.fog,polygon.additive,polygon.translucent);}}
        renderStages[2]=renderClock()-renderStarted;renderStarted=renderClock();
        overlayBuffer.ensure(dc,width,height);
        lastSpritePixels=0;
        auto overlay=overlayBuffer.pixels;
        std::fill_n(overlay,size_t(width)*height,0);
        auto drawEntities=enemies->active();if(coopGame){auto& buddy=coopMarines[1-coopLocal];if(buddy.health){unsigned e=0x31e200;for(unsigned i=0;i<208;++i)m.b(e+i,0);m.w(e,buddy.position[0]);m.w(e+2,buddy.position[1]+32);m.w(e+4,buddy.position[2]);m.b(e+0x74,14);m.d(e+0x7c,0x9c3fc);m.d(e+0xa0,0x31f000);m.w(e+0xb4,0x900);int facing=((buddy.yaw-int(std::atan2(double(viewPosition[0]-buddy.position[0]),double(buddy.position[2]-viewPosition[2]))*4096/(2*Pi))+4352)>>9)&7;unsigned animation=std::min(4,facing>=5?8-facing:facing);if(facing>=5)m.b(e+0xb6,32);startEntityAnimation(m,e,short(animation));for(unsigned i=0;i<(steps/6)%4;++i)advanceAnimation(m,e+128);drawEntities.push_back(e);}}
        for(auto e:drawEntities){
            if(enemies->isAttachedEntity(e))continue;
            Point location{double(m.s(e)),double(m.s(e+2)),double(m.s(e+4))};if(interpolate&&previousEnemies.contains(e)){auto old=previousEnemies.at(e);location={old[0]+(location.x-old[0])*renderAlpha,old[1]+(location.y-old[1])*renderAlpha,old[2]+(location.z-old[2])*renderAlpha};}
            auto point=cameraPoint(location,camPos,renderYaw,renderPitch,renderRoll);if(point.z<32)continue;auto& frame=enemies->sprite(e);if(!frame.width)continue;
            // Original gameplay projection uses focal 256; b4 is the sprite scale numerator.
            double factor=focal/point.z*m.w(e+0xb4)/256.0;bool flipped=m.b(e+0xb6)&32;double left=width*.5+point.x*focal/point.z+(flipped?-int(frame.width)-frame.left:frame.left)*factor,top=height*.5-point.y*focal/point.z+frame.top*factor;
            double rollRad=renderRoll*2.0*Pi/4096.0,centerX=width*.5+point.x*focal/point.z,centerY=height*.5-point.y*focal/point.z;
            if(gpu){gpu->sprite(frame.pixels,frame.width,frame.height,float(left),float(top),float(factor),float(point.z),flipped,float(rollRad),float(centerX),float(centerY));continue;}
            if(!renderRoll){
            int x0=std::clamp(int(left),0,width),x1=std::clamp(int(std::ceil(left+frame.width*factor)),0,width),y0=std::clamp(int(top),0,height),y1=std::clamp(int(std::ceil(top+frame.height*factor)),0,height);
            for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){int u=int((x-left)/factor),v=int((y-top)/factor);if(u<0||v<0||u>=int(frame.width)||v>=int(frame.height))continue;if(flipped)u=frame.width-1-u;auto pixel=frame.pixels[v*frame.width+u];auto index=y*width+x,depthIndex=(y/depthScale)*depthWidth+x/depthScale;if(pixel&&point.z<depthBuffer[depthIndex]+1){overlay[index]=explorerFogPixel(pixel,point.z);++lastSpritePixels;depthBuffer[depthIndex]=float(point.z);}}
            }else{
                double cr=std::cos(rollRad),sr=std::sin(rollRad);
                std::array<std::pair<double,double>,4> corners={std::make_pair(left,top),std::make_pair(left+frame.width*factor,top),std::make_pair(left+frame.width*factor,top+frame.height*factor),std::make_pair(left,top+frame.height*factor)};
                double minX=width,maxX=0,minY=height,maxY=0;
                for(auto& c:corners){double lx=c.first-centerX,ly=c.second-centerY;double rx=centerX+(lx*cr+ly*sr),ry=centerY+(-lx*sr+ly*cr);minX=std::min(minX,rx);maxX=std::max(maxX,rx);minY=std::min(minY,ry);maxY=std::max(maxY,ry);}
                int x0=std::clamp(int(minX),0,width),x1=std::clamp(int(std::ceil(maxX)),0,width),y0=std::clamp(int(minY),0,height),y1=std::clamp(int(std::ceil(maxY)),0,height);
                for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){double dx=x-centerX,dy=y-centerY;double lx=dx*cr-dy*sr,ly=dx*sr+dy*cr;int u=int((centerX+lx-left)/factor),v=int((centerY+ly-top)/factor);if(u<0||v<0||u>=int(frame.width)||v>=int(frame.height))continue;if(flipped)u=frame.width-1-u;auto pixel=frame.pixels[v*frame.width+u];auto index=y*width+x,depthIndex=(y/depthScale)*depthWidth+x/depthScale;if(pixel&&point.z<depthBuffer[depthIndex]+1){overlay[index]=explorerFogPixel(pixel,point.z);++lastSpritePixels;depthBuffer[depthIndex]=float(point.z);}}
            }
        }
        if(photoMode&&photoFilter)for(size_t i=0;i<size_t(width)*height;++i)if(overlay[i])overlay[i]=(overlay[i]&0xff000000)|applyPhotoFilter(overlay[i],photoFilter);
        // Blend glass after world and enemies, before first-person artwork.
        if(!gpu){std::vector<const Polygon*> glass;for(auto& p:polygons)if(p.translucent)glass.push_back(&p);std::stable_sort(glass.begin(),glass.end(),[](auto a,auto b){return a->depth>b->depth;});std::vector<unsigned> stamps(size_t(width)*height,0);unsigned stamp=0;
        for(auto ptr:glass){const auto& p=*ptr;++stamp;for(unsigned tri=1;tri+1<p.count;++tri){auto a=p.points[0],b=p.points[tri],c=p.points[tri+1];double area=double(b.x-a.x)*(c.y-a.y)-double(b.y-a.y)*(c.x-a.x);if(std::abs(area)<.01)continue;int x0=std::clamp(std::min({a.x,b.x,c.x}),0L,LONG(width-1)),x1=std::clamp(std::max({a.x,b.x,c.x}),0L,LONG(width-1)),y0=std::clamp(std::min({a.y,b.y,c.y}),0L,LONG(height-1)),y1=std::clamp(std::max({a.y,b.y,c.y}),0L,LONG(height-1));for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){auto index=y*width+x,di=(y/depthScale)*depthWidth+x/depthScale;if(stamps[index]==stamp)continue;double u=(double(b.x-x)*(c.y-y)-double(b.y-y)*(c.x-x))/area,v=(double(c.x-x)*(a.y-y)-double(c.y-y)*(a.x-x))/area,w=1-u-v;if(u<0||v<0||w<0)continue;double z=1/(u/p.depths[0]+v/p.depths[tri]+w/p.depths[tri+1]);if(z>=depthBuffer[di])continue;unsigned tex=0xffffffff;if(p.texture){int tx=int((u*p.uv[0][0]/p.depths[0]+v*p.uv[tri][0]/p.depths[tri]+w*p.uv[tri+1][0]/p.depths[tri+1])*z),ty=int((u*p.uv[0][1]/p.depths[0]+v*p.uv[tri][1]/p.depths[tri]+w*p.uv[tri+1][1]/p.depths[tri+1])*z);tex=(*p.texture)[std::clamp(ty,0,255)*256+std::clamp(tx,0,255)];}if(!(tex>>24))continue;auto light=[&](unsigned k){return p.nativeLighting?unsigned(std::clamp(u*p.colors[0][k]+v*p.colors[tri][k]+w*p.colors[tri+1][k],0.,255.)):k==0?GetRValue(p.color):k==1?GetGValue(p.color):GetBValue(p.color);};unsigned color=(std::min(255u,((tex>>16)&255)*light(0)/128)<<16)|(std::min(255u,((tex>>8)&255)*light(1)/128)<<8)|std::min(255u,(tex&255)*light(2)/128);if(p.fog)color=explorerFogPixel(color,z);if(photoMode&&photoFilter)color=applyPhotoFilter(color,photoFilter);unsigned behind=overlay[index]?overlay[index]:levelPixels[di];overlay[index]=0xff000000|(((color&0xfefefe)+(behind&0xfefefe))>>1);stamps[index]=stamp;}}}}
        lastWeaponBottom=-1;if(!photoMode&&player.health){auto& gun=hud.weapon();double scale=height/240.0;double left=width*.5+gun.left*scale+(std::sin(bobPhase)*3*bobAmount*scale),top=height+gun.top*scale;for(int y=std::max(0,int(top));y<std::min(height,int(top+gun.height*scale));++y)for(int x=std::max(0,int(left));x<std::min(width,int(left+gun.width*scale));++x){int u=int((x-left)/scale),v=int((y-top)/scale);if(u>=0&&v>=0&&u<int(gun.width)&&v<int(gun.height)){auto pixel=gun.pixels[v*gun.width+u];if(pixel){overlay[y*width+x]=pixel;lastWeaponBottom=std::max(lastWeaponBottom,y);}}}}
        // Original ammo/health/armor artwork and proportional digit sprites.
        if(!photoMode&&!activeSurvivalSkull(BrokenVisorSkull)){
        const auto& status=hud.status();
        const double statusScale=height/240.;
        static std::vector<int> statusColumns[2];
        const int starts[]={0,std::max(0,int(width-105*statusScale))};
        const int ends[]={std::min(width,int(145*statusScale)),width};
        for(int side=0;side<2;++side){
            auto& columns=statusColumns[side];
            columns.resize(ends[side]-starts[side]);
            for(int x=starts[side];x<ends[side];++x)
                columns[x-starts[side]]=side?int(320-(width-x)/statusScale):int(x/statusScale);
        }
        for(int y=0;y<height;++y){
            const auto* source=status.pixels.data()+std::min(239,int(y/statusScale))*320;
            auto* target=overlay+size_t(y)*width;
            for(int side=0;side<2;++side)for(int x=starts[side];x<ends[side];++x){
                const int u=statusColumns[side][x-starts[side]];
                if(unsigned(u)<320&&source[u])target[x]=source[u];
            }
        }
        // Original 320x240 tracker layout, anchored to the right edge on wide screens.
        const auto& dish=hud.tracker();double hudScale=height/240.0,trackerLeft=width-97*hudScale,trackerTop=155*hudScale;lastTrackerPixels=0;
        for(int y=std::max(0,int(trackerTop));y<std::min(height,int(trackerTop+dish.height*hudScale));++y)for(int x=std::max(0,int(trackerLeft));x<std::min(width,int(trackerLeft+dish.width*hudScale));++x){int u=int((x-trackerLeft)/hudScale),v=int((y-trackerTop)/hudScale);if(u>=0&&v>=0&&u<int(dish.width)&&v<int(dish.height)){auto pixel=dish.pixels[v*dish.width+u];if(pixel){overlay[y*width+x]=pixel;++lastTrackerPixels;}}}
        auto entities=enemies->active();for(auto contact:ExplorerHud::contacts(m,entities,viewPosition,yaw)){if(!ExplorerHud::onTracker(contact))continue;int px=int(width-56*hudScale+contact[0]*hudScale),py=int(192*hudScale+contact[1]*hudScale);for(int y=std::max(0,py);y<std::min(height,py+std::max(1,int(2*hudScale)));++y)for(int x=std::max(0,px);x<std::min(width,px+std::max(1,int(2*hudScale)));++x)overlay[y*width+x]=0xffffffff;}
        if(isolationStage){
            const double angle=yaw*2*Pi/4096,cs=std::cos(angle),sn=std::sin(angle);
            const auto here=exterminationPart(player.currentCell);
            for(const auto& tag:isolationTags){
                if(tag.collected||exterminationPart(tag.cell)!=here)continue;   // the other map's tags lie out of reach
                const double dx=viewPosition[0]-(double(tag.cell%grid.width)*512+256-player.originX);
                const double dz=viewPosition[2]-(double(tag.cell/grid.width)*512+256-player.originZ);
                std::array<double,2> c{(dx*cs+dz*sn)/256,(-dx*sn+dz*cs)/256};
                const bool rim=!ExplorerHud::onTracker(c);
                if(rim){const double f=std::min(39/std::max(1e-6,std::abs(c[0])),31/std::max(1e-6,std::abs(c[1])));c[0]*=f;c[1]*=f;}
                const int size=std::max(3,int((rim?3:4)*hudScale));   // bigger than an alien contact, so a tag at the edge still shows
                const int px=int(width-56*hudScale+c[0]*hudScale)-size/2,py=int(192*hudScale+c[1]*hudScale)-size/2;
                for(int y=std::max(0,py);y<std::min(height,py+size);++y)
                    for(int x=std::max(0,px);x<std::min(width,px+size);++x)overlay[y*width+x]=0xff40ff40;
            }
        }
        }
        lastAttachmentPixels=0;if(!photoMode&&enemies->attached())for(unsigned half=0;half<2;++half){auto& face=enemies->attachmentSprite(half);double scale=height/240.0*4,left=width*.5+face.left*scale,top=(enemies->attachmentY()+face.top*4)*height/240.0;
            for(int y=std::max(0,int(top));y<std::min(height,int(std::ceil(top+face.height*scale)));++y)for(int x=std::max(0,int(left));x<std::min(width,int(std::ceil(left+face.width*scale)));++x){int u=int((x-left)/scale),v=int((y-top)/scale);if(u>=0&&v>=0&&u<int(face.width)&&v<int(face.height)){auto pixel=face.pixels[v*face.width+u];if(pixel){overlay[y*width+x]=pixel;++lastAttachmentPixels;}}}
        }
        renderStages[3]=renderClock()-renderStarted;renderStarted=renderClock();
        // Teammate marker is composed after all world/glass/sprite depth tests.
        if(coopGame&&!photoMode&&coopLocal<2){auto& buddy=coopMarines[1-coopLocal];if(buddy.health){auto point=cameraPoint({double(buddy.position[0]),double(buddy.position[1])+1536.,double(buddy.position[2])},camPos,renderYaw,renderPitch,renderRoll);auto marker=explorerCoopMarker(point.x,point.y,point.z,focal,width,height);explorerDrawCoopMarker(overlay,width,height,marker);}}
        BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        AlphaBlend(dc,0,0,width,height,overlayBuffer.dc,0,0,width,height,blend);
        GdiFlush(); // The DIB pixels will be reused on the next frame.
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(190,255,210));if(!photoMode&&!activeSurvivalSkull(BrokenVisorSkull)&&debugOverlay){std::wstring title=L"ALIEN TRILOGY - "+(lighting.active?std::wstring(L"LEVEL 3"):level.stem().wstring());textFont.shadow(dc,title,12,10,height/900.);
        std::wstring controls=L"WASD move   Mouse look / LMB fire   Q / wheel switch   1-5 select   E use   B objectives   PgUp/PgDn level   R restart   Esc pause";textFont.box(dc,controls,RECT{12,32,width-12,54},height/1000.);
        auto live=enemies->active();auto liveCount=std::count_if(live.begin(),live.end(),[&](unsigned e){return m.s(e+0x58)>0;});
        std::wstring scope=L"Health "+std::to_wstring(player.health)+L"   Armor "+std::to_wstring(mem().w(0xa0aba))+L"   "+hud.weaponName()+L" "+std::to_wstring(hud.ammo)+(hud.reloadTicks?L" RELOADING":L"")+L" / "+std::to_wstring(hud.reserve())+L" clips   Batteries "+std::to_wstring(mem().w(0xa0ad2))+L"   Kills "+std::to_wstring(enemies->kills)+L"   Live enemies "+std::to_wstring(liveCount)+L"   FPS "+std::to_wstring(int(measuredFps+.5));textFont.shadow(dc,scope,12,height-28,height/1000.);}
        if(isolationStage&&!photoMode){
            // What is left: the tags (below), the aliens, the Queen; then the level's own exit elevator.
            std::wstring objective;
            if(isolationStage==4)objective=hud.owns(3)?L"KILL THE QUEEN":L"TAKE THE SMARTGUN - THEN KILL THE QUEEN";
            else if(!exterminationClear()){
                unsigned left=0;auto mm=mem();
                for(auto e:enemies->active()){const auto k=mm.b(e+0x74);left+=k>=2&&k<=6&&mm.s(e+0x58)>0&&mm.b(e+0x6f)<7;}
                objective=L"XENOS LEFT   "+std::to_wstring(left);
            }else objective=isolationStage==3?L"DECK CLEAR - THE EXIT ELEVATOR TO THE QUEEN'S LAIR":L"DECK CLEAR - TO THE EXIT ELEVATOR";
            if(!isolationTagsComplete()){
                const auto here=exterminationPart(player.currentCell);
                unsigned collected=0,total=0;
                for(const auto& tag:isolationTags)if(exterminationPart(tag.cell)==here){++total;collected+=tag.collected;}
                if(total&&collected==total&&here==1)objective=L"AREA TAGS RECOVERED - ON TO THE ELEVATOR";
                else objective=L"CREW ID TAGS   "+std::to_wstring(collected)+L" / "+std::to_wstring(total)+(exterminationArrival!=~0u?L"  (THIS AREA)":L"");
            }
            if(exterminationNearArrival())objective=L"FACE THE ELEVATOR WALL, USE: BACK TO "+exterminationFirstTitle;
            for(const auto& panel:isolationPanels)if(isolationNear(panel.cell)){
                objective=(doors.entries[panel.door].target?std::wstring(L"ACCESS GRANTED: "):std::wstring(L"USE: OPEN "))+isolationWing(panel.door);
                break;
            }
            for(const auto& tag:isolationTags){
                const auto cell=player.currentCell;
                if(std::abs(int(tag.cell%grid.width)-int(cell%grid.width))+
                   std::abs(int(tag.cell/grid.width)-int(cell/grid.width))>1)continue;
                if(!tag.collected)objective+=tag.container>=9?(tag.container==9?L"   ID TAG INSIDE: SHOOT THE CRATE OPEN":L"   ID TAG INSIDE: SHOOT THE LOCKER OPEN"):
                    tag.container==0?L"   USE: SEARCH BOX":tag.container==1?L"   USE: SEARCH LOCKER":L"   USE: TAKE TAG";
                break;
            }
            const double squadScale=std::max(1.,height/480.);
            int squadY=int(54*squadScale);
            unsigned member=0;
            for(const auto& [entity,maximum]:enemies->exterminationSquad()){
                auto hp=std::clamp(m.s(entity+0x58),0,int(maximum));
                const bool down=enemies->exterminationDowned(entity);
                pickupFont.shadow(dc,L"HANDLER "+std::to_wstring(++member)+L"   "+(down?std::wstring(L"DOWN - HOLD USE TO REVIVE"):std::to_wstring(hp)+
                    L" / "+std::to_wstring(maximum)),20,squadY,squadScale,down?0xff6040:0x50ff60);
                const int left=20, right=left+int(120*squadScale);
                const int top=squadY+int(12*squadScale), bottom=top+int(3*squadScale);
                RECT track{left,top,right,bottom}, fill{left,top,left+(right-left)*hp/int(maximum),bottom};
                FillRect(dc,&track,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                auto green=CreateSolidBrush(RGB(80,255,96));
                FillRect(dc,&fill,green);
                DeleteObject(green);
                squadY+=int(22*squadScale);
            }
            if(isolationQueen&&m.s(isolationQueen+0x58)>0)objective=L"DEFEAT THE QUEEN";
            pickupFont.shadow(dc,objective,20,30,std::max(1.,height/480.),0x50ff60);
        }
        if(!photoMode&&!activeSurvivalSkull(BrokenVisorSkull)&&menu.achievements.toastTicks)
            textFont.box(dc,L"Achievement unlocked   "+menu.achievements.toast,
                RECT{width/6,height/18,width*5/6,height/8},height/700.,0xffffff,true);
        if(!photoMode&&!activeSurvivalSkull(BrokenVisorSkull))explorerSurvivalHud(*this,dc,width,height);
        if(!photoMode&&mission.remaining){std::wstring timer=L"ALARM "+std::to_wstring(((mission.remaining+29)/30)/60)+L":"+(((mission.remaining+29)/30)%60<10?std::wstring(L"0"):std::wstring())+std::to_wstring(((mission.remaining+29)/30)%60);textFont.shadow(dc,timer,12,int(height*.13),height/600.);}
        if(!photoMode&&!activeSurvivalSkull(BrokenVisorSkull)&&noticeTicks){double scale=height/240.;bool pickup=pickups.queuedCount()&&notice==pickups.messageText();std::wstring displayed=notice;
            // 0x80037a8c: reveal one character per tick, then blink using age bit 4.
            if(pickup){auto age=pickups.messageAge();displayed=notice.substr(0,std::min<size_t>(age,notice.size()));if(age<=notice.size()||!(age&4))displayed+=L"*";}
            scale=std::min(scale,(width-24.)/std::max(1,pickupFont.width(notice+(pickup?L"*":L""),1)));pickupFont.shadow(dc,displayed,int(15*height/240.),int(56*height/240.),scale,0x33ff33);}
        if(mission.complete){RECT r{width/10,height/12,width*9/10,height*11/12};auto brush=CreateSolidBrush(RGB(2,12,8));FillRect(dc,&r,brush);DeleteObject(brush);r.left+=20;r.right-=20;r.top+=16;std::wstring text=L"MISSION ASSESSMENT\n\n"+mission.title+L"\n\n"+mission.text+L"\n"+mission.progress()+L"\nEnter: mission assessment";textFont.box(dc,text,r,height/700.);}
        if(coopGame&&!menu.open&&!mission.briefing){auto text=L"TEAM SCORE "+std::to_wstring(coopScore.total);pickupFont.shadow(dc,text,12,12,std::max(1.,height/480.),0x50ff60);}
        if(!gpu&&transitionBrightness()<255){auto blackDC=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,1,1);auto old=SelectObject(blackDC,bitmap);SetPixel(blackDC,0,0,RGB(0,0,0));BLENDFUNCTION fade{AC_SRC_OVER,0,BYTE(255-transitionBrightness()),0};AlphaBlend(dc,0,0,width,height,blackDC,0,0,1,1,fade);SelectObject(blackDC,old);DeleteObject(bitmap);DeleteDC(blackDC);}
        if(photoMode&&photoDof&&!gpu){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto copyDC=CreateCompatibleDC(dc);auto old=SelectObject(copyDC,bitmap);if(bitmap&&bits){BitBlt(copyDC,0,0,width,height,dc,0,0,SRCCOPY);GdiFlush();auto data=static_cast<unsigned*>(bits);std::vector<unsigned> pixels(data,data+size_t(width)*height);explorerDepthOfField(pixels,width,height,depthBuffer,depthWidth,depthHeight,photoDof/100.f);std::copy(pixels.begin(),pixels.end(),data);BitBlt(dc,0,0,width,height,copyDC,0,0,SRCCOPY);}SelectObject(copyDC,old);DeleteDC(copyDC);DeleteObject(bitmap);}
        if(photoMode&&!photoCapturing){
            if(photoFlashTicks>0){
                auto flashDC=CreateCompatibleDC(dc);auto flashBmp=CreateCompatibleBitmap(dc,1,1);auto oldFlash=SelectObject(flashDC,flashBmp);
                SetPixel(flashDC,0,0,RGB(255,255,255));
                BYTE flashAlpha=BYTE(std::min(255u,photoFlashTicks*85u));
                BLENDFUNCTION flashBlend{AC_SRC_OVER,0,flashAlpha,0};
                AlphaBlend(dc,0,0,width,height,flashDC,0,0,1,1,flashBlend);
                SelectObject(flashDC,oldFlash);DeleteObject(flashBmp);DeleteDC(flashDC);
            }
            if(photoGrid==1){
                HPEN gridPen=CreatePen(PS_SOLID,1,RGB(110,190,140));auto oldPen=SelectObject(dc,gridPen);
                int x1=width/3,x2=width*2/3,y1=height/3,y2=height*2/3,arm=std::max(4,height/100);
                MoveToEx(dc,x1,0,nullptr);LineTo(dc,x1,height);MoveToEx(dc,x2,0,nullptr);LineTo(dc,x2,height);
                MoveToEx(dc,0,y1,nullptr);LineTo(dc,width,y1);MoveToEx(dc,0,y2,nullptr);LineTo(dc,width,y2);
                for(int ix:{x1,x2})for(int iy:{y1,y2}){MoveToEx(dc,ix-arm,iy,nullptr);LineTo(dc,ix+arm+1,iy);MoveToEx(dc,ix,iy-arm,nullptr);LineTo(dc,ix,iy+arm+1);}
                SelectObject(dc,oldPen);DeleteObject(gridPen);
            }else if(photoGrid==2){
                HPEN framePen=CreatePen(PS_SOLID,1,RGB(130,210,160));auto oldPen=SelectObject(dc,framePen);
                int cx=width/2,cy=height/2,len=std::max(12,height/40),gap=std::max(4,height/120);
                MoveToEx(dc,cx-len,cy,nullptr);LineTo(dc,cx-gap,cy);MoveToEx(dc,cx+gap,cy,nullptr);LineTo(dc,cx+len,cy);
                MoveToEx(dc,cx,cy-len,nullptr);LineTo(dc,cx,cy-gap);MoveToEx(dc,cx,cy+gap,nullptr);LineTo(dc,cx,cy+len);
                int mx=width/20,my=height/20;RECT safeRect{mx,my,width-mx,height-my};FrameRect(dc,&safeRect,(HBRUSH)GetStockObject(WHITE_BRUSH));
                SelectObject(dc,oldPen);DeleteObject(framePen);
            }
            if(photoShowUi){
                double scale=std::max(1.,height/360.);int line=int(12*scale),barH=line*2+12;RECT topBar{0,0,width,barH};auto brush=CreateSolidBrush(RGB(2,10,6));FillRect(dc,&topBar,brush);DeleteObject(brush);
                const auto& labels=ExplorerPhotoFilterLabels;int rollDeg=int(std::lround(photoRoll*360./4096));if(rollDeg>180)rollDeg-=360;
                std::wstring header=L"PHOTO MODE   FOV "+std::to_wstring(int(photoFov))+L"   Roll "+std::to_wstring(rollDeg)+L"   Speed "+std::to_wstring(int(photoSpeed));pickupFont.draw(dc,header,14,5,scale,0x60ff80);
                std::wstring status=L"Filter: "+std::wstring(labels[std::min(unsigned(labels.size()-1),photoFilter)])+L"   Torch: "+(photoLamp?L"ON":L"OFF")+L"   Grid: "+std::array<const wchar_t*,3>{L"Off",L"Thirds",L"Crosshair"}[std::min(2u,photoGrid)];pickupFont.draw(dc,status,14,5+line,scale,0xffffff);
                auto track=photoDofTrack(width,height);RECT panel{track.left-6,barH+3,track.right+6,track.bottom+int(16*scale)};brush=CreateSolidBrush(RGB(2,10,6));FillRect(dc,&panel,brush);DeleteObject(brush);pickupFont.draw(dc,photoDof?L"Depth of field: "+std::to_wstring(photoDof)+L"%":L"Depth of field: Off",track.left,barH+7,scale,0xffffff);brush=CreateSolidBrush(RGB(40,75,50));FillRect(dc,&track,brush);DeleteObject(brush);RECT fill=track;fill.right=track.left+(track.right-track.left)*photoDof/100;brush=CreateSolidBrush(RGB(50,220,90));FillRect(dc,&fill,brush);int knob=track.left+(track.right-track.left)*photoDof/100;RECT handle{knob-2,track.top-2,knob+3,track.bottom+2};FillRect(dc,&handle,brush);DeleteObject(brush);pickupFont.draw(dc,L"Focus: centre of frame",track.left,track.bottom+int(3*scale),scale*.85,0xb8e8c0);
                int bottomHeight=menu.usingController?int(68*scale):line*3+12;RECT bottom{0,height-bottomHeight,width,height};brush=CreateSolidBrush(RGB(2,10,6));FillRect(dc,&bottom,brush);DeleteObject(brush);
                if(menu.usingController){explorerDrawPrompts(dc,pickupFont,{{0,L"Capture"},{1,L"Exit"},{2,L"Filter"},{3,L"UI"}},menu.playstationPrompts,width,bottom.top+3,scale);const std::array<std::wstring,3> hints{L"Sticks Fly / Look   "+explorerButtonName(32,menu.playstationPrompts)+L" / "+explorerButtonName(33,menu.playstationPrompts)+L" Down / Up",explorerButtonName(4,menu.playstationPrompts)+L" + D-pad L/R: DOF   D-pad L/R: Zoom",L"D-pad Up Grid   D-pad Down Torch   "+explorerButtonName(8,menu.playstationPrompts)+L" Reset roll"};for(unsigned i=0;i<hints.size();++i){double hintScale=std::min(scale,(width-24.)/std::max(1,pickupFont.width(hints[i],1)));pickupFont.draw(dc,hints[i],std::max(8,(width-pickupFont.width(hints[i],hintScale))/2),bottom.top+int(26*scale)+i*line,hintScale,0xffffff);}}
                else{const std::array<std::wstring,3> lines{L"WASD Fly   Space/C Up/Down   Z/X Roll   R Reset   [ ] Zoom",L"F Filter   G Grid   L Torch   . Step   , / DOF   Alt Mouse slider",L"H Hide UI   Enter Capture   Esc Exit"};for(unsigned i=0;i<lines.size();++i){double textScale=std::min(scale,(width-24.)/std::max(1,pickupFont.width(lines[i],1)));pickupFont.draw(dc,lines[i],std::max(8,(width-pickupFont.width(lines[i],textScale))/2),bottom.top+5+int(i)*line,textScale,0xffffff);}}
            }
            if(photoNoticeTicks>0){
                double scale=std::min(std::max(1.,height/360.),(width-40.)/std::max(1,pickupFont.width(photoNotice,1)));int tw=pickupFont.width(photoNotice,scale),th=int(12*scale),nx=(width-tw)/2,ny=int(height*.30);
                RECT panel{nx-14,ny-6,nx+tw+14,ny+th+6};auto brush=CreateSolidBrush(RGB(4,18,10));FillRect(dc,&panel,brush);DeleteObject(brush);
                HPEN borderPen=CreatePen(PS_SOLID,1,RGB(50,200,100));auto oldBorder=SelectObject(dc,borderPen);SelectObject(dc,GetStockObject(NULL_BRUSH));
                Rectangle(dc,panel.left,panel.top,panel.right,panel.bottom);SelectObject(dc,oldBorder);DeleteObject(borderPen);
                pickupFont.shadow(dc,photoNotice,nx,ny,scale,0x50ff60);
            }
        }
        if(replaying&&!photoMode&&!photoCapturing){
            if(menu.usingController)menu.drawPrompts(dc,width,height-40,height/700.,{{0,replay.paused?L"Resume":L"Pause"},{2,L"Photo mode"},{1,L"Exit"}});
            else textFont.box(dc,replay.paused?L"REPLAY PAUSED   Space: resume   P: photo mode   Esc: exit":
                L"REPLAY   Space: pause   P: photo mode   Esc: exit",RECT{12,height-48,width-12,height-10},height/700.,0xffffff,true);
        }
        renderStages[4]=renderClock()-renderStarted;


    }
};
Scene scene;std::array<bool,256> held{},tapped{};bool mouseCaptured=false,controllerInputDispatch=false;
fs::path Scene::capturePhoto(HWND window,int reqWidth,int reqHeight){
    int w=reqWidth,h=reqHeight;
    if(w<=0||h<=0){
        if(window){RECT r{};GetClientRect(window,&r);w=r.right;h=r.bottom;}
        if(w<=0||h<=0){w=1280;h=800;}
    }
    fs::path folder=root/"screenshots";
    const auto clip=replaying?watchedReplay:replay.recording.is_open()?replay.destination:fs::path{};
    if(!clip.empty())folder=menu.replayScreenshots(clip);
    std::error_code ec;
    fs::create_directories(folder,ec);
    SYSTEMTIME st;GetLocalTime(&st);
    wchar_t name[128];
    swprintf_s(name,L"Trilogy_Photo_%04d%02d%02d_%02d%02d%02d.bmp",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
    fs::path outPath=folder/name;
    for(int suffix=1;fs::exists(outPath);++suffix){
        swprintf_s(name,L"Trilogy_Photo_%04d%02d%02d_%02d%02d%02d_%d.bmp",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,suffix);
        outPath=folder/name;
    }
    photoCapturing=true;
    auto screenDC=GetDC(nullptr);auto backDC=CreateCompatibleDC(screenDC);
    auto bitmap=CreateCompatibleBitmap(screenDC,w,h);auto oldBmp=SelectObject(backDC,bitmap);
    draw(backDC,w,h,nullptr);
    SelectObject(backDC,oldBmp);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    std::vector<unsigned char> pixels(size_t(w)*h*4);
    bool saved=false;
    if(GetDIBits(backDC,bitmap,0,h,pixels.data(),&info,DIB_RGB_COLORS)){
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+DWORD(pixels.size());
        std::ofstream f(outPath,std::ios::binary);
        if(f){
            f.write(reinterpret_cast<const char*>(&header),sizeof(header));
            f.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));
            f.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
            f.close();
            saved=bool(f);
        }
    }
    DeleteObject(bitmap);DeleteDC(backDC);ReleaseDC(nullptr,screenDC);
    if(!saved){
        photoCapturing=false;
        photoNotice=L"Could not save screenshot";
        photoNoticeTicks=120;
        return {};
    }
    photoCapturing=false;photoFlashTicks=3;
    if(audio)audio->playMenu(0);
    photoNotice=L"Photo saved: "+outPath.filename().wstring();
    photoNoticeTicks=120;
    lastCapturedPhoto=outPath;
    return outPath;
}
void confineMouse(HWND window){RECT r;GetClientRect(window,&r);POINT a{r.left,r.top},b{r.right,r.bottom};ClientToScreen(window,&a);ClientToScreen(window,&b);RECT screen{a.x,a.y,b.x,b.y};ClipCursor(&screen);}
void mouseCapture(HWND window,bool capture){if(capture==mouseCaptured)return;mouseCaptured=capture;held.fill(false);tapped.fill(false);if(capture){confineMouse(window);ShowCursor(FALSE);SetCursor(nullptr);}else{ClipCursor(nullptr);ShowCursor(TRUE);}scene.firing=false;scene.grenadeRequested=false;if(scene.audio)scene.audio->pause(!capture&&scene.menu.hasGame);SetWindowTextW(window,capture?TrilogyTitle:(std::wstring(TrilogyTitle)+L" - Paused (click to capture mouse)").c_str());}
RECT previousWindowedBounds{};bool haveWindowedBounds=false;std::string previousWindowedMonitor;unsigned previousWindowedResolution=0;
std::wstring fullscreenDevice;
unsigned fullscreenResolution=~0u;
bool displayTransition=false, displaySuspended=false;

void restoreDesktopMode(){
    if(fullscreenDevice.empty())return;
    const auto device=std::exchange(fullscreenDevice,{});
    fullscreenResolution=~0u;
    ChangeDisplaySettingsExW(device.c_str(),nullptr,nullptr,0,nullptr);
}

void applyDisplay(HWND window){
    if(displayTransition||displaySuspended)return;
    displayTransition=true;
    scene.menu.refreshMonitors();
    auto monitor=scene.menu.monitors[explorerMonitorIndex(scene.menu.monitors,scene.menu.monitorDevice)];
    auto& mode=scene.menu.displayMode;
    const auto resolution=ExplorerResolutions[scene.menu.resolution];
    const auto style=GetWindowLongPtrW(window,GWL_STYLE);
    const bool wasWindowed=(style&WS_OVERLAPPEDWINDOW)!=0;
    if(mode!=ExplorerMenu::Windowed&&wasWindowed&&IsWindowVisible(window)){
        GetWindowRect(window,&previousWindowedBounds);
        previousWindowedMonitor=monitor.id;
        previousWindowedResolution=scene.menu.resolution;
        haveWindowedBounds=true;
    }

    if(mode!=ExplorerMenu::Fullscreen||fullscreenDevice!=monitor.device||
       fullscreenResolution!=scene.menu.resolution)restoreDesktopMode();
    if(mode==ExplorerMenu::Fullscreen&&fullscreenDevice.empty()){
        DEVMODEW requested{};
        requested.dmSize=sizeof(requested);
        requested.dmPelsWidth=resolution.width;
        requested.dmPelsHeight=resolution.height;
        requested.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT;
        if(!monitor.device.empty()&&ChangeDisplaySettingsExW(monitor.device.c_str(),&requested,nullptr,CDS_FULLSCREEN,nullptr)==DISP_CHANGE_SUCCESSFUL){
            fullscreenDevice=monitor.device;
            fullscreenResolution=scene.menu.resolution;
        }else{
            mode=ExplorerMenu::Borderless;
            scene.menu.message=L"Display mode unavailable. Using borderless fullscreen.";
            scene.menu.persist();
        }
    }

    scene.menu.refreshMonitors();
    monitor=scene.menu.monitors[explorerMonitorIndex(scene.menu.monitors,scene.menu.monitorDevice)];
    const bool windowed=mode==ExplorerMenu::Windowed;
    RECT bounds=explorerWindowBounds(monitor,resolution.width,resolution.height,!windowed);
    if(windowed&&!wasWindowed&&haveWindowedBounds&&previousWindowedMonitor==monitor.id&&
       previousWindowedResolution==scene.menu.resolution&&previousWindowedBounds.left>=monitor.work.left&&
       previousWindowedBounds.top>=monitor.work.top&&previousWindowedBounds.right<=monitor.work.right&&
       previousWindowedBounds.bottom<=monitor.work.bottom)bounds=previousWindowedBounds;
    SetWindowLongPtrW(window,GWL_STYLE,(windowed?WS_OVERLAPPEDWINDOW:WS_POPUP)|(style&WS_VISIBLE));
    SetWindowPos(window,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
                 SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE);
    displayTransition=false;
    if(mouseCaptured)confineMouse(window);
}

#include "ExplorerCoopRuntime.inc"
ExplorerCoopRuntime coop(scene);
void menuCommand(HWND window,ExplorerMenu::Command command){
 if(coop.command(command)){held.fill(false);tapped.fill(false);if(!scene.menu.open&&scene.menu.hasGame&&window)mouseCapture(window,true);if(window)InvalidateRect(window,nullptr,FALSE);return;}
 try{if(command==ExplorerMenu::ReplayScreenshots){
    if(scene.menu.replayIndex<scene.menu.replayFiles.size()){
        const auto folder=scene.menu.replayScreenshots(scene.menu.replayFiles[scene.menu.replayIndex]);
        if(!fs::is_directory(folder))scene.menu.message=L"No screenshots for this replay yet.";
        else if(reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
            scene.menu.message=L"Could not open the screenshots folder.";
    }
    return;
 }if(command==ExplorerMenu::ReplayWatch){
    if(!coop.active()&&scene.menu.replayIndex<scene.menu.replayFiles.size()){
        scene.watchReplay(scene.menu.replayFiles[scene.menu.replayIndex]);
        mouseCapture(window,false);
    }
    return;
 }if(command==ExplorerMenu::SurvivalStart||command==ExplorerMenu::QueensRevengeStart){explorerSurvivalStart(scene,command==ExplorerMenu::QueensRevengeStart);scene.menu.difficultyLocked=g_survival.active;held.fill(false);tapped.fill(false);if(window){mouseCapture(window,g_survival.active);InvalidateRect(window,nullptr,FALSE);}return;}if(command==ExplorerMenu::MainMenu){scene.returnToMainMenu();held.fill(false);tapped.fill(false);if(window){mouseCapture(window,false);InvalidateRect(window,nullptr,FALSE);}return;}if(scene.coopGame&&(command==ExplorerMenu::CheatsChanged||command==ExplorerMenu::LoadPassword||command==ExplorerMenu::Restart||command==ExplorerMenu::Start||(command>=ExplorerMenu::Load1&&command<=ExplorerMenu::Save3))){scene.menu.message=L"Unavailable during co-op";return;}if(command==ExplorerMenu::LoadPassword){auto code=scene.menu.passwordText;for(auto& c:code)c=towupper(c);const std::array<std::wstring,5> cheats={L"1G0TP1NK8C1DB00TS0N",L"DY1NGT0N1GHT",L"B0X0VTR1CKS",L"0BL1V10NTR1P",L"N0CL1P"};auto cheat=std::find(cheats.begin(),cheats.end(),code);if(cheat!=cheats.end()){scene.menu.cheatsUnlocked=true;unsigned id=unsigned(cheat-cheats.begin());if(id==1){scene.menu.giveWeapons=true;for(unsigned kind=0;kind<5;++kind)awardPickup(scene.mem(),kind,100,10);scene.hud.syncAmmo();}if(id==2)scene.menu.infiniteAmmo=true;if(id==3)scene.menu.godMode=true;if(id==4)scene.menu.noClip=true;scene.menu.message=L"CHEATS ACTIVATED";scene.menu.passwordText.clear();return;}
 if(code.size()==7&&code.substr(0,5)==L"G0LVL"&&code[5]>=L'0'&&code[5]<=L'9'&&code[6]>=L'0'&&code[6]<=L'9'){unsigned n=(code[5]-L'0')*10+code[6]-L'0';if(n<35){scene.menu.cheatsUnlocked=true;scene.menu.startLevel=n;scene.menu.message=L"CHEATS ACTIVATED";scene.menu.passwordText.clear();return;}}
 auto state=explorerDecodePassword(code);if(!state){scene.menu.message=L"INVALID PASSWORD";return;}auto m=scene.mem();auto resource=m.w(0x9d800+state->level*2),unused=resource;auto name=m.d(0x94cdc+resource*12);std::string asset;for(unsigned j=0;j<80&&m.b(name+j);++j)asset+=char(m.b(name+j));auto levelCode=asset.substr(asset.find("LEV")+3,3);auto found=std::find_if(scene.physicalLevels.begin(),scene.physicalLevels.end(),[&](auto& p){return p.stem().string()=="L"+levelCode+"LEV";});if(found==scene.physicalLevels.end())throw std::runtime_error("Password level unavailable");scene.level=*found;scene.difficulty=state->difficulty;scene.menu.difficulty=state->difficulty;scene.menu.difficultyLocked=true;scene.load();scene.inventory(state->inventory,state->weapon);scene.menu.hasGame=true;scene.menu.open=false;scene.mission.briefing=true;return;
 }if(command==ExplorerMenu::CheatsChanged){if(g_survival.active||scene.isolationStage)return;if(scene.menu.selection==2){scene.menu.giveWeapons=true;for(unsigned kind=0;kind<5;++kind)awardPickup(scene.mem(),kind,100,10);scene.hud.syncAmmo();}return;}if(command==ExplorerMenu::Quit){DestroyWindow(window);return;}if(command==ExplorerMenu::IsolationStart){if(!scene.menu.isolationUnlocked())return;scene.startIsolation(1);}
    else if(command>=ExplorerMenu::ExterminationSave1&&command<=ExplorerMenu::ExterminationSave3){
        if(!scene.isolationStage)return;
        const unsigned slot=unsigned(command-ExplorerMenu::ExterminationSave1);const auto path=scene.menu.exterminationSlotPath(slot);
        std::error_code error;fs::create_directories(path.parent_path(),error);
        {std::ofstream f(path.parent_path()/"extermination_slot.tmp",std::ios::trunc);f<<"ATEXTERM_SLOT 1 "<<scene.isolationStage<<"\n";}
        fs::rename(path.parent_path()/"extermination_slot.tmp",path,error);
        scene.menu.message=error?L"Could not save to this slot.":L"Extermination level "+std::to_wstring(scene.isolationStage)+L" saved to slot "+std::to_wstring(slot+1)+L".";
    }
    else if(command>=ExplorerMenu::ExterminationLoad1&&command<=ExplorerMenu::ExterminationLoad3){
        if(!scene.menu.isolationUnlocked())return;
        if(const auto level=scene.menu.exterminationSlotLevel(unsigned(command-ExplorerMenu::ExterminationLoad1)))scene.startIsolation(level);
    }
    else if(command==ExplorerMenu::IsolationContinue){if(!scene.menu.isolationUnlocked())return;scene.menu.refreshExtermination();if(scene.menu.exterminationSaved)scene.startIsolation(scene.menu.exterminationSaved);}else if(command==ExplorerMenu::Start){scene.isolationStage=0;scene.difficulty=scene.menu.difficulty;scene.menu.difficultyLocked=true;scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";if(scene.menu.cheatsUnlocked&&scene.menu.startLevel){auto m=scene.mem();auto name=m.d(0x94cdc+m.w(0x9d800+scene.menu.startLevel*2)*12);std::string asset;for(unsigned j=0;j<80&&m.b(name+j);++j)asset+=char(m.b(name+j));auto code=asset.substr(asset.find("LEV")+3,3);for(auto p:scene.physicalLevels)if(p.stem().string()=="L"+code+"LEV")scene.level=p;}scene.load();scene.mission.briefing=true;scene.menu.hasGame=true;scene.menu.open=false;}else if(command>=ExplorerMenu::Load1&&command<=ExplorerMenu::Load3){scene.isolationStage=0;scene.checkpoint(command-ExplorerMenu::Load1,false);}else if(command>=ExplorerMenu::Save1&&command<=ExplorerMenu::Save3)scene.checkpoint(command-ExplorerMenu::Save1,true);else if(command==ExplorerMenu::NextMission){if(!scene.mission.eligible()){scene.restart();scene.menu.open=false;return;}auto code=scene.mission.nextCode();auto it=std::find_if(scene.physicalLevels.begin(),scene.physicalLevels.end(),[&](auto& p){return p.stem().string()=="L"+code+"LEV";});if(it==scene.physicalLevels.end()){if(scene.mission.index==34)scene.queueMovie("GAMEOVER");scene.menu.show(false);}else{if(!code.empty()&&code[0]!=scene.mission.sourceCode[0])scene.queueMovie(code[0]=='2'?"S1_2":"S2_3");auto carried=scene.inventory();auto selected=scene.hud.selected();scene.level=*it;scene.difficulty=scene.menu.difficulty;scene.load();scene.inventory(carried,selected);scene.mission.briefing=true;scene.menu.open=false;}}else if(command==ExplorerMenu::Display)applyDisplay(window);else if(command==ExplorerMenu::Restart){scene.restart();scene.menu.open=false;}else if(command==ExplorerMenu::UseBattery){auto m=scene.mem();if(m.w(0xa0ad4)&&m.w(0xa0ad2)){m.w(0xa0ad2,m.w(0xa0ad2)-1);m.w(0xa0ad6,900);m.w(0xa0ab4,m.w(0xa0ab4)|0x100u);scene.menu.batteries=m.w(0xa0ad2);scene.menu.message=L"Shoulder lamp recharged.";}else if(!m.w(0xa0ad4))scene.menu.message=L"No shoulder lamp collected.";}else if(command>=ExplorerMenu::Weapon0&&command<=ExplorerMenu::Weapon4){auto weapon=unsigned(command-ExplorerMenu::Weapon0);if(scene.hud.owns(weapon)){scene.hud.select(weapon);scene.menu.equipped=weapon;scene.menu.message=L"Selected";}else scene.menu.message=L"You have not collected this weapon.";}
  if(gpuRenderer){gpuRenderer->setVsync(scene.menu.vsync);scene.menu.antialiasingModes=gpuRenderer->supportedAntialiasing();scene.menu.antialiasing=gpuRenderer->setAntialiasing(scene.menu.antialiasing);gpuRenderer->setTextureFiltering(scene.menu.textureFiltering);}scene.audio->volume(scene.menu.sfx,scene.menu.music);scene.audio->headphones(scene.menu.headphoneAudio);if(scene.menu.soundEvent>=0){scene.audio->playMenu(unsigned(scene.menu.soundEvent));scene.menu.soundEvent=-1;}held.fill(false);tapped.fill(false);if(window&&!scene.menu.open&&scene.menu.hasGame)mouseCapture(window,true);if(window)InvalidateRect(window,nullptr,FALSE);
 }catch(const std::exception& e){std::string text=e.what();ExplorerDiagnostics::error("menu command failed: "+text);scene.menu.message.assign(text.begin(),text.end());scene.menu.open=true;}
}
std::array<int,4> menuPointer(HWND window,LPARAM point){RECT r;GetClientRect(window,&r);auto ratio=ExplorerAspects[scene.menu.aspect];auto fit=explorerFit(r.right,r.bottom,ratio.x,ratio.y);return {GET_X_LPARAM(point)-fit.x,GET_Y_LPARAM(point)-fit.y,fit.width,fit.height};}
void toggleAutoMap(HWND window){if(scene.menu.open&&scene.menu.page=="map"){scene.menu.close();mouseCapture(window,true);return;}if(!scene.menu.hasGame||scene.menu.open)return;if(!(scene.mem().b(0xa0ab4)&128)){scene.notice=L"Auto Mapper not collected in this level";scene.noticeTicks=60;return;}if(scene.showAutoMap())mouseCapture(window,false);}
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
        case WM_GETMINMAXINFO:{RECT maximum{0,0,3840,2160};AdjustWindowRect(&maximum,WS_OVERLAPPEDWINDOW,FALSE);auto limits=reinterpret_cast<MINMAXINFO*>(l);LONG maxWidth=maximum.right-maximum.left,maxHeight=maximum.bottom-maximum.top;if(!scene.menu.monitors.empty()){auto& selected=scene.menu.monitors[explorerMonitorIndex(scene.menu.monitors,scene.menu.monitorDevice)];maxWidth=std::max(maxWidth,selected.area.right-selected.area.left);maxHeight=std::max(maxHeight,selected.area.bottom-selected.area.top);}limits->ptMaxTrackSize={maxWidth,maxHeight};limits->ptMinTrackSize={640,400};return 0;}
        case WM_CREATE:{RAWINPUTDEVICE mouse{1,2,0,window};if(!RegisterRawInputDevices(&mouse,1,sizeof(mouse)))return -1;return 0;}
        case WM_DESTROY:scene.finishReplayRecording();displaySuspended=true;restoreDesktopMode();explorerSaveSurvivalRun(scene,L"Quit");mouseCapture(window,false);PostQuitMessage(0);return 0;
        case WM_ACTIVATEAPP:
            displaySuspended=!w;
            if(!w&&scene.menu.displayMode==ExplorerMenu::Fullscreen){
                restoreDesktopMode();
                ShowWindow(window,SW_MINIMIZE);
            }else if(w&&scene.menu.displayMode==ExplorerMenu::Fullscreen){
                if(IsIconic(window))ShowWindow(window,SW_RESTORE);
                applyDisplay(window);
            }
            return 0;
        case WM_SETFOCUS:return 0;
        case WM_KILLFOCUS:if(scene.photoDofDragging){scene.photoDofDragging=false;ReleaseCapture();}mouseCapture(window,false);held.fill(false);tapped.fill(false);return 0;
        case WM_DISPLAYCHANGE:scene.menu.refreshMonitors();applyDisplay(window);return 0;
        case WM_DPICHANGED:if((scene.menu.displayMode!=ExplorerMenu::Windowed))applyDisplay(window);else{auto bounds=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);}return 0;
        case WM_SYSKEYUP:if(scene.photoMode&&w==VK_MENU)return 0;break;
        case WM_SYSKEYDOWN:if(scene.photoMode&&w==VK_MENU)return 0;if(w==VK_F10){if(!(l&(1LL<<30))){scene.menu.performanceDisplay=!scene.menu.performanceDisplay;scene.menu.persist();}return 0;}if(w==VK_RETURN){scene.menu.displayMode=scene.menu.displayMode==ExplorerMenu::Windowed?ExplorerMenu::Borderless:ExplorerMenu::Windowed;scene.menu.persist();applyDisplay(window);return 0;}break;
        case WM_SIZE:case WM_MOVE:if(mouseCaptured)confineMouse(window);break;
        case WM_SETCURSOR:if(mouseCaptured){SetCursor(nullptr);return TRUE;}break;
        case WM_MOUSEMOVE:{static LPARAM previousMouse=-1;if(l!=previousMouse){scene.menu.usingController=false;previousMouse=l;}}if(scene.hasMovies())return 0;if(scene.photoMode){if(scene.photoPointerMode&&scene.photoDofDragging&&(w&MK_LBUTTON)){auto p=menuPointer(window,l);auto track=scene.photoDofTrack(p[2],p[3]);scene.adjustPhotoDof(int(std::lround((p[0]-track.left)*100./std::max(1L,track.right-track.left))));}return 0;}if(scene.menu.open){auto p=menuPointer(window,l);if((w&MK_LBUTTON)&&scene.menu.page=="graphics"&&scene.menu.selection==4){int line=std::clamp(p[3]/27,20,34),top=p[3]/5+4*line;if(p[1]>=top&&p[1]<top+line){menuCommand(window,scene.menu.click(p[0],p[1],p[2],p[3],true));return 0;}}if(scene.menu.hover(p[0],p[1],p[2],p[3]))menuCommand(window,ExplorerMenu::None);return 0;}break;
        case WM_LBUTTONDOWN:if(scene.replaying&&!scene.photoMode)return 0;scene.menu.usingController=false;if(scene.photoMode&&scene.photoPointerMode){auto p=menuPointer(window,l);auto track=scene.photoDofTrack(p[2],p[3]);if(scene.photoShowUi&&p[1]>=track.top-6&&p[1]<=track.bottom+6&&p[0]>=track.left-6&&p[0]<=track.right+6){scene.photoDofDragging=true;SetCapture(window);scene.adjustPhotoDof(int(std::lround((p[0]-track.left)*100./std::max(1L,track.right-track.left))));}return 0;}if(scene.hasMovies())return 0;if(scene.menu.open){auto p=menuPointer(window,l);menuCommand(window,scene.menu.click(p[0],p[1],p[2],p[3]));return 0;}if(!mouseCaptured){mouseCapture(window,true);return 0;}if(scene.photoMode)return 0;held[VK_LBUTTON]=true;tapped[VK_LBUTTON]=true;return 0;
        case WM_RBUTTONDOWN:case WM_MBUTTONDOWN:scene.menu.usingController=false;if(scene.hasMovies())return 0;if(scene.menu.open){menuCommand(window,scene.menu.key(message==WM_RBUTTONDOWN?VK_RBUTTON:VK_MBUTTON));return 0;}if(scene.photoMode)return 0;held[message==WM_RBUTTONDOWN?VK_RBUTTON:VK_MBUTTON]=true;tapped[message==WM_RBUTTONDOWN?VK_RBUTTON:VK_MBUTTON]=true;return 0;
        case WM_RBUTTONUP:case WM_MBUTTONUP:held[message==WM_RBUTTONUP?VK_RBUTTON:VK_MBUTTON]=false;return 0;
        case WM_LBUTTONUP:if(scene.photoDofDragging){scene.photoDofDragging=false;ReleaseCapture();}held[VK_LBUTTON]=false;return 0;
        case WM_CHAR:scene.menu.usingController=false;if(scene.hasMovies())return 0;if(scene.menu.open){scene.menu.inputCharacter(wchar_t(w));return 0;}break;
        case WM_MOUSEWHEEL:scene.menu.usingController=false;if(scene.photoMode){int delta=GET_WHEEL_DELTA_WPARAM(w)>0?1:-1;scene.photoSpeed=std::clamp(scene.photoSpeed+delta*4.0,4.0,120.0);scene.photoNotice=L"Camera Speed: "+std::to_wstring(int(scene.photoSpeed));scene.photoNoticeTicks=45;return 0;}if(mouseCaptured&&!scene.enemies->attached()){if(scene.hud.cycle(GET_WHEEL_DELTA_WPARAM(w)>0?-1:1)){scene.notice=scene.hud.weaponName();scene.noticeTicks=45;}}return 0;
        case WM_KEYDOWN:if(scene.replaying&&!scene.photoMode){
            if(!(l&(1LL<<30))){
                if(w==VK_ESCAPE)scene.stopReplay();
                else if(w=='P'||w==VK_F2){
                    scene.enterPhotoMode();
                    const auto current=scene.player.position,previous=scene.previousPosition;
                    scene.photoX=std::lerp(double(previous[0]),double(current[0]),scene.renderAlpha);
                    scene.photoY=std::lerp(double(previous[1]),double(current[1]),scene.renderAlpha);
                    scene.photoZ=std::lerp(double(previous[2]),double(current[2]),scene.renderAlpha);
                    scene.photoYaw=scene.replayViewYaw();scene.photoPitch=scene.replayViewPitch();
                    scene.audio->pause(true);
                    if(window)mouseCapture(window,true);
                }
                else if(w==VK_SPACE||w==VK_RETURN){
                    scene.replay.paused=!scene.replay.paused;
                    scene.audio->pause(scene.replay.paused);
                }
            }
            return 0;
        }if(!controllerInputDispatch)scene.menu.usingController=false;if(w==VK_F10){if(!(l&(1LL<<30))){scene.menu.performanceDisplay=!scene.menu.performanceDisplay;scene.menu.persist();}return 0;}if(scene.hasMovies()){if(!(l&(1LL<<30))&&(w==VK_ESCAPE||w==VK_RETURN||w==VK_SPACE))scene.movie.skip();return 0;}if(scene.exitFading&&!scene.replaying)return 0;
        if((w=='P'||w==VK_F2)&&!(l&(1LL<<30))){if(scene.photoMode){scene.exitPhotoMode();if(scene.replaying){if(window)mouseCapture(window,false);scene.audio->pause(scene.replay.paused);}held.fill(false);tapped.fill(false);return 0;}if(scene.canEnterPhotoMode()){scene.enterPhotoMode();held.fill(false);tapped.fill(false);return 0;}}
        if(scene.photoMode){
            if(w==VK_ESCAPE){scene.exitPhotoMode();if(scene.replaying){if(window)mouseCapture(window,false);scene.audio->pause(scene.replay.paused);}held.fill(false);tapped.fill(false);return 0;}
            if((w==VK_OEM_COMMA||w==VK_OEM_2)&&!(l&(1LL<<30))){scene.adjustPhotoDof(int(scene.photoDof)+(w==VK_OEM_COMMA?-5:5));return 0;}
            if(w=='H'&&!(l&(1LL<<30))){scene.photoShowUi=!scene.photoShowUi;return 0;}
            if(w=='F'&&!(l&(1LL<<30))){scene.photoFilter=(scene.photoFilter+1)%ExplorerPhotoFilterNames.size();const auto& fn=ExplorerPhotoFilterNames;scene.photoNotice=std::wstring(L"Filter: ")+fn[scene.photoFilter];scene.photoNoticeTicks=60;return 0;}
            if(w=='G'&&!(l&(1LL<<30))){scene.photoGrid=(scene.photoGrid+1)%3;const wchar_t* gn[]={L"Grid: Off",L"Grid: Rule of Thirds",L"Grid: Crosshair & Frame"};scene.photoNotice=gn[scene.photoGrid];scene.photoNoticeTicks=60;return 0;}
            if(w=='L'&&!(l&(1LL<<30))){scene.photoLamp=!scene.photoLamp;scene.photoNotice=scene.photoLamp?L"Studio Fill Light: ON":L"Studio Fill Light: OFF";scene.photoNoticeTicks=60;return 0;}
            if((w==VK_OEM_4||w==VK_LEFT)&&!(l&(1LL<<30))){scene.photoFov=std::clamp(scene.photoFov-5.,15.,120.);scene.photoNotice=L"FOV: "+std::to_wstring(int(scene.photoFov))+L"";scene.photoNoticeTicks=45;return 0;}
            if((w==VK_OEM_6||w==VK_RIGHT)&&!(l&(1LL<<30))){scene.photoFov=std::clamp(scene.photoFov+5.,15.,120.);scene.photoNotice=L"FOV: "+std::to_wstring(int(scene.photoFov))+L"";scene.photoNoticeTicks=45;return 0;}
            if(w=='R'&&!(l&(1LL<<30))){scene.photoRoll=0;scene.photoNotice=L"Camera Roll Reset";scene.photoNoticeTicks=45;return 0;}
            if(!scene.replaying&&(w==VK_OEM_PERIOD||w=='T')&&!(l&(1LL<<30))){scene.tick(0,0,false);scene.photoNotice=L"Frame Advanced (Tick "+std::to_wstring(scene.steps)+L")";scene.photoNoticeTicks=45;return 0;}
            if((w==VK_RETURN||w==VK_F12||w==VK_SNAPSHOT)&&!(l&(1LL<<30))){scene.capturePhoto(window);return 0;}
            if(w<256){held[w]=true;if(!(l&(1LL<<30)))tapped[w]=true;}
            return 0;
        }if(w=='M'&&!(l&(1LL<<30))&&(!scene.menu.open||scene.menu.page=="map")){toggleAutoMap(window);return 0;}if(scene.menu.open){if(!(l&(1LL<<30)))menuCommand(window,scene.menu.key(unsigned(w)));return 0;}if(scene.coopGame&&!(l&(1LL<<30))&&scene.mission.briefing&&(w==VK_RETURN||w==scene.menu.bindings[7])){coop.briefingEnter();return 0;}if(!(l&(1LL<<30))&&(w==VK_RETURN||w==scene.menu.bindings[7])){if(g_survival.active)return 0;if(scene.mission.complete&&w==VK_RETURN){scene.menu.show(true);scene.menu.showResults(scene.mission.statistics().percent,scene.mission.eligible(),scene.mission.minimumPercent());mouseCapture(window,false);return 0;}else if(w==VK_RETURN){if(scene.mission.briefing&&!scene.briefingRevealed&&(GetTickCount64()-scene.briefingStarted)/25<size_t(std::count_if(scene.mission.markup.begin(),scene.mission.markup.end(),[](wchar_t c){return c>32;})))scene.briefingRevealed=true;else scene.mission.briefing=false;}else{scene.mission.briefing=!scene.mission.briefing;scene.briefingRevealed=true;}scene.firing=false;held[VK_LBUTTON]=false;return 0;}if(w==VK_F9){scene.debugOverlay=!scene.debugOverlay;return 0;}if(w==VK_ESCAPE){scene.hud.stopFiring();for(auto sound:scene.hud.takeSounds())scene.audio->playWeapon(sound);scene.menu.pauseGame();scene.menu.batteries=scene.mem().w(0xa0ad2);scene.menu.equipped=scene.hud.selected();constexpr unsigned ammo[]={0xa0abe,0xa0ac2,0xa0ac4,0xa0acc,0xa0aca},clips[]={0xa0ac0,0,0xa0ac6,0xa0ace,0};for(unsigned i=0;i<5;++i){scene.menu.owned[i]=scene.hud.owns(i);scene.menu.ammunition[i]=scene.mem().w(ammo[i]);scene.menu.reserves[i]=clips[i]?scene.mem().w(clips[i]):0;}scene.menu.missionNote=g_survival.active?(g_survival.cfg.queensRevenge()?L"Queen's Revenge!\n\nDefeat all five Queens.":L"Classic Survival\n\n" + std::to_wstring(g_survival.cfg.waves) + L" waves. Defeat the Queen to win."):scene.mission.title+L"\n\n"+scene.mission.text+L"\n"+scene.mission.progress();scene.syncPauseMap();mouseCapture(window,false);return 0;}if(w<256){held[w]=true;if(!(l&(1LL<<30)))tapped[w]=true;}if(!(l&(1LL<<30))&&mouseCaptured&&!scene.enemies->attached()&&(w==scene.menu.bindings[6]||(w>='1'&&w<='5'))){bool changed=w==scene.menu.bindings[6]?scene.hud.cycle():scene.hud.select(unsigned(w-'1'));scene.notice=changed?scene.hud.weaponName():L"Weapon not available";scene.noticeTicks=45;}if(!scene.coopGame&&scene.player.health&&!(l&(1LL<<30))&&w==scene.menu.bindings[8])scene.restart();if(!(l&(1LL<<30))&&w==scene.menu.bindings[5])scene.useRequested=true;if(!scene.coopGame&&!g_survival.active&&!scene.isolationStage&&!(l&(1LL<<30))&&(w==VK_NEXT||w==VK_PRIOR)){auto index=unsigned(std::find(scene.levels.begin(),scene.levels.end(),scene.level)-scene.levels.begin());index=(index+scene.levels.size()+(w==VK_NEXT?1:-1))%scene.levels.size();scene.level=scene.levels[index];scene.load();scene.mission.briefing=true;}return 0;
        case WM_KEYUP:if(w<256)held[w]=false;return 0;
        case WM_INPUT:{RAWINPUT input{};UINT size=sizeof(input);if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))!=UINT(-1)&&input.header.dwType==RIM_TYPEMOUSE&&mouseCaptured&&!scene.exitFading&&!scene.menu.open&&!scene.mission.briefing&&!scene.mission.complete&&!(input.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)){if(input.data.mouse.lLastX||input.data.mouse.lLastY)scene.menu.usingController=false;if(scene.photoMode){scene.photoYaw=(scene.photoYaw+input.data.mouse.lLastX*int(scene.menu.sensitivity))&4095;scene.photoPitch=std::clamp(scene.photoPitch-int(input.data.mouse.lLastY)*int(scene.menu.sensitivity)*(scene.menu.invert?-1:1),-1800,1800);return DefWindowProcW(window,message,w,l);}scene.yaw=(scene.yaw+input.data.mouse.lLastX*int(scene.menu.sensitivity))&4095;scene.pitch=std::clamp(scene.pitch-int(input.data.mouse.lLastY)*int(scene.menu.sensitivity)*(scene.menu.invert?-1:1),-910,910);}return DefWindowProcW(window,message,w,l);}
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT paint;auto dc=BeginPaint(window,&paint);RECT r;GetClientRect(window,&r);auto back=CreateCompatibleDC(dc);const auto resolution=ExplorerResolutions[scene.menu.resolution];int rw=std::max(1L,r.right),rh=std::max(1L,r.bottom);
        if(scene.menu.displayMode==ExplorerMenu::Borderless&&!scene.menu.open&&!scene.mission.briefing){
            const auto fit=explorerFit(rw,rh,resolution.width,resolution.height);
            const double scale=std::min({1.,double(resolution.width)/fit.width,double(resolution.height)/fit.height});
            rw=std::max(1,int(rw*scale));rh=std::max(1,int(rh*scale));
        }{auto aspect=ExplorerAspects[scene.menu.aspect];auto fit=explorerFit(rw,rh,aspect.x?aspect.x:int(r.right),aspect.y?aspect.y:int(r.bottom));rw=fit.width;rh=fit.height;}if(gpuRenderer){gpuRenderer->setPhotoFilter(scene.photoMode?scene.photoFilter:0);gpuRenderer->setDepthOfField(scene.photoMode?scene.photoDof/100.f:0);int uw=rw,uh=rh;if((scene.hasMovies()||!scene.menu.open&&!scene.mission.briefing)&&rh>480){uh=480;uw=std::max(1,int(std::lround(double(rw)*uh/rh)));}auto target=gpuRenderer->begin(rw,rh,uw,uh);try{scene.draw(target,uw,uh,gpuRenderer.get());}catch(const std::exception& e){static std::string last;if(last!=e.what()){last=e.what();SYSTEMTIME st;GetLocalTime(&st);std::ofstream(scene.root/"decompilation/reports/runtime-errors.log",std::ios::app)<<st.wYear<<"-"<<st.wMonth<<"-"<<st.wDay<<" "<<st.wHour<<":"<<st.wMinute<<":"<<st.wSecond<<" draw: "<<e.what()<<'\n';ExplorerDiagnostics::error(std::string("draw: ")+e.what());}}   /* a frame that cannot be drawn is skipped and logged: an error escaping WM_PAINT aborts the game */scene.drawPerformance(target,uw,uh);gpuRenderer->finish(scene.hasMovies()||scene.menu.open||scene.mission.briefing,scene.transitionBrightness());DeleteDC(back);EndPaint(window,&paint);return 0;}auto bitmap=CreateCompatibleBitmap(dc,rw,rh);auto old=SelectObject(back,bitmap);try{scene.draw(back,rw,rh);}catch(const std::exception& e){static std::string last;if(last!=e.what()){last=e.what();SYSTEMTIME st;GetLocalTime(&st);std::ofstream(scene.root/"decompilation/reports/runtime-errors.log",std::ios::app)<<st.wYear<<"-"<<st.wMonth<<"-"<<st.wDay<<" "<<st.wHour<<":"<<st.wMinute<<":"<<st.wSecond<<" draw: "<<e.what()<<'\n';ExplorerDiagnostics::error(std::string("draw: ")+e.what());}}   /* a frame that cannot be drawn is skipped and logged: an error escaping WM_PAINT aborts the game */scene.drawPerformance(back,rw,rh);SetStretchBltMode(dc,COLORONCOLOR);auto fit=explorerFit(r.right,r.bottom,rw,rh);FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));StretchBlt(dc,fit.x,fit.y,fit.width,fit.height,back,0,0,rw,rh,SRCCOPY);SelectObject(back,old);DeleteObject(bitmap);DeleteDC(back);EndPaint(window,&paint);return 0;}}
    return DefWindowProcW(window,message,w,l);
}
void preview(const fs::path& path){int width=1100,height=700;auto dc=GetDC(nullptr),back=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,width,height);auto old=SelectObject(back,bitmap);scene.draw(back,width,height);scene.drawPerformance(back,width,height);SelectObject(back,old);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;std::vector<unsigned char> pixels(width*height*4);if(!GetDIBits(back,bitmap,0,height,pixels.data(),&info,DIB_RGB_COLORS))throw std::runtime_error("Cannot capture preview");BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+DWORD(pixels.size());std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<char*>(&header),sizeof(header));f.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));f.write(reinterpret_cast<char*>(pixels.data()),pixels.size());DeleteObject(bitmap);DeleteDC(back);ReleaseDC(nullptr,dc);
}
unsigned setupBulletTrailFixture(){
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();scene.menu.open=false;scene.menu.fov=70;scene.mission.briefing=false;scene.faces.clear();scene.vertices.clear();scene.doors.entries.clear();scene.logic.panels.clear();scene.logic.lifts.clear();scene.objects.entries.clear();scene.pickups.clear();auto m=scene.mem();m.d(0x2365e0,0);scene.player.position={0,0,0};scene.previousPosition=scene.player.position;scene.yaw=scene.pitch=0;scene.interpolate=false;m.w(0x96234,1);m.b(0x95f9c,1);resetProjectilePool(m);auto p=acquireProjectile(m),pos=m.d(p),vel=m.d(p+4);m.w(pos,128);m.w(pos+2,640);m.w(pos+4,1024);m.w(vel,0);m.w(vel+2,0);m.w(vel+4,256);m.b(p+30,0);m.b(p+28,10);m.b(p+29,20);m.b(p+32,0);activateProjectile(m,p);return p;
}
void coopConnectedSelfTest(){
 std::ofstream trace(scene.root/"decompilation/reports/coop_connection_timing.log");auto started=GetTickCount64();auto mark=[&](const char* stage){trace<<GetTickCount64()-started<<" "<<stage<<"\n";trace.flush();};auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};Scene a,b;a.root=b.root=scene.root;a.level=b.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";a.load();b.load();ExplorerCoopRuntime host(a),guest(b);a.menu.coopPort=b.menu.coopPort=L"27017";host.command(ExplorerMenu::CoopHost);auto until=GetTickCount64()+15000;while(host.connection.invitation().empty()&&host.active()&&GetTickCount64()<until){host.poll();Sleep(1);}require(host.connection.invitation().size()==88,"Session invitation unavailable");b.menu.coopAddress=L"127.0.0.1";auto invite=host.connection.invitation();b.menu.coopInvitation.assign(invite.begin(),invite.end());guest.command(ExplorerMenu::CoopJoin);until=GetTickCount64()+15000;while((!host.testCompatible()||!guest.testCompatible())&&GetTickCount64()<until){host.poll();guest.poll();host.frame(.005,false,0,0,false,false);guest.frame(.005,false,0,0,false,false);Sleep(1);}require(host.testCompatible()&&guest.testCompatible(),"Session build handshake failed");guest.command(ExplorerMenu::CoopReady);until=GetTickCount64()+5000;while(!host.testReady()&&GetTickCount64()<until){host.poll();guest.poll();Sleep(1);}require(host.testReady(),"Guest ready did not reach host");host.command(ExplorerMenu::CoopStart);host.briefingEnter();host.briefingEnter();until=GetTickCount64()+15000;for(unsigned i=0;i<300&&GetTickCount64()<until;++i){host.poll();guest.poll();host.frame(1./30,false,0,0,false,false);guest.frame(1./30,true,0,0,false,false);Sleep(15);}require(host.playing()&&guest.playing(),"Connected co-op did not enter gameplay");require(b.coopLocal==1&&b.enemies->active()==a.enemies->active(),"Connected world replica differs");host.testDamage(1,0);host.testClearEnemies();for(unsigned i=0;i<90;++i){host.poll();guest.poll();host.frame(1./30,false,0,0,false,false);guest.frame(1./30,false,0,0,false,false);Sleep(15);}require(a.coopMarines[1].health&&b.player.health,"Networked safe respawn missing");host.testObjectivesPassed();host.testExtract();host.frame(1./30,false,0,0,false,false);host.command(ExplorerMenu::CoopAdvance);for(unsigned i=0;i<90;++i){host.poll();guest.poll();host.frame(1./30,false,0,0,false,false);guest.frame(1./30,false,0,0,false,false);Sleep(15);}if(a.mission.sourceCode!="112"||b.mission.sourceCode!="112")throw std::runtime_error("Host level transition did not reach guest: "+a.mission.sourceCode+" / "+b.mission.sourceCode+" host="+host.connection.status()+" guest="+guest.connection.status()+" detail="+std::string(b.menu.message.begin(),b.menu.message.end()));host.briefingEnter();host.briefingEnter();
 host.testDamage(0,0);host.testDamage(1,0);until=GetTickCount64()+10000;while((host.phase!=trilogy::coop::Phase::Lobby||guest.phase!=trilogy::coop::Phase::Lobby)&&GetTickCount64()<until){host.poll();guest.poll();host.frame(1./30,false,0,0,false,false);guest.frame(1./30,false,0,0,false,false);Sleep(15);}if(!(host.phase==trilogy::coop::Phase::Lobby&&guest.phase==trilogy::coop::Phase::Lobby&&host.connection.connected()&&guest.connection.connected()))throw std::runtime_error("Team wipe lobby states: "+std::to_string(unsigned(host.phase))+" / "+std::to_string(unsigned(guest.phase))+" health="+std::to_string(a.coopMarines[0].health)+","+std::to_string(a.coopMarines[1].health)+" queued="+std::to_string(host.connection.queuedBytes())+" connection="+host.connection.status()+" / "+guest.connection.status());mark("loading final");host.testLevel("391");mark("loaded final");host.testObjectivesPassed();host.testExtract();host.frame(1./30,false,0,0,false,false);mark("assessment final");host.command(ExplorerMenu::CoopAdvance);mark("advanced final");until=GetTickCount64()+10000;while(guest.phase!=trilogy::coop::Phase::Finished&&GetTickCount64()<until){trace<<GetTickCount64()-started<<" phase "<<unsigned(host.phase)<<" "<<unsigned(guest.phase)<<" queue "<<host.connection.queuedBytes()<<"\n";trace.flush();host.poll();guest.poll();host.frame(1./30,false,0,0,false,false);guest.frame(1./30,false,0,0,false,false);Sleep(15);}if(!(host.phase==trilogy::coop::Phase::Finished&&guest.phase==trilogy::coop::Phase::Finished&&b.mission.sourceCode=="391"))throw std::runtime_error("Section boundary states "+std::to_string(unsigned(host.phase))+" / "+std::to_string(unsigned(guest.phase))+" codes="+a.mission.sourceCode+" / "+b.mission.sourceCode+" next="+std::to_string(a.mission.nextIndex())+" queue="+std::to_string(host.connection.queuedBytes())+" status="+host.connection.status()+" / "+guest.connection.status());host.command(ExplorerMenu::CoopAdvance);until=GetTickCount64()+10000;while(guest.phase!=trilogy::coop::Phase::Lobby&&GetTickCount64()<until){host.poll();guest.poll();host.frame(1./30,false,0,0,false,false);guest.frame(1./30,false,0,0,false,false);Sleep(15);}require(guest.phase==trilogy::coop::Phase::Lobby,"Completion did not return guest to lobby");
 trilogy::coop::Writer malformed;trilogy::coop::PlayerInput invalid{};invalid.buttons=~0u;malformed.pod(invalid);guest.connection.send(trilogy::coop::Input,malformed.data);until=GetTickCount64()+5000;while(host.active()&&GetTickCount64()<until){guest.poll();host.poll();Sleep(15);}require(!host.active()&&a.menu.message==L"Invalid co-op player input","Host accepted malformed guest input");guest.leave();std::ofstream(scene.root/"decompilation/reports/coop_connected_checks.json")<<"{\"status\":\"passed\",\"tls_loopback\":true,\"build_handshake\":true,\"guest_ready\":true,\"live_world_replication\":true,\"safe_respawn\":true,\"host_level_transition\":true,\"campaign_boundary\":true,\"connected_lobby_after_wipe\":true,\"invalid_input_rejected\":true,\"disconnect\":true}";
}
// Developer check for the Extermination squad's facing: deck 1 cleared of aliens, one ally holding a fixed heading
// (yaw 0), the player looking at it from its front, its side and its back. Writes decompilation/reports/
// extermination-ally-{front,side,back}.bmp (software renderer).
// L131's secret pulse rifle wall (lift 0, threshold 7: panel chains 2, 4, 8, 12 and area triggers 13, 14, 15), in
// an order that once broke it: door 2 unlocked by its other switch (action 11) and opened from its far side
// (action 5) before panel 36's chain (action 4) runs - which must still count toward the wall.
void secretOrderSelfTest(){
    auto check=[](bool ok,const char* what){if(!ok)throw std::runtime_error(what);};
    scene.level=scene.root/"GameData/PS1/CD/SECT11/L131LEV.MAP";scene.load(true);auto m=scene.mem();
    m.w(0xa0ad2,1);   // one battery, for panel 39
    auto& wall=scene.logic.lifts.at(0);
    check(scene.logic.action(m,scene.doors,11,true),"L131 panel 1 did not unlock door 2");
    check(scene.logic.action(m,scene.doors,5,true)&&scene.doors.entries.at(2).target,"L131 door 2 did not open from its far side");
    const auto before=wall.progress;
    check(scene.logic.action(m,scene.doors,4,true)&&wall.progress==before+1,"L131 panel 36 stopped short of the secret wall after door 2 opened");
    for(unsigned action:{2u,8u,12u})check(scene.logic.action(m,scene.doors,action,true),"An L131 secret panel chain was refused");
    for(unsigned action:{13u,14u,15u})check(scene.logic.action(m,scene.doors,action,false),"An L131 secret area trigger was refused");
    check(wall.moving&&!wall.targetHigh,"L131's secret pulse rifle wall did not lower after all seven steps");
    // As played: Use in front of each of the four panels (one battery for panel 39) and walking onto each trigger area,
    // in the campaign map and in Extermination level 1 (Medical Laboratory is its first map).
    for(unsigned deck=0;deck<2;++deck){
        if(deck){scene.startIsolation(1);scene.mission.briefing=false;}else{scene.level=scene.root/"GameData/PS1/CD/SECT11/L131LEV.MAP";scene.load(true);}
        auto mm=scene.mem();mm.w(0xa0ad2,1);auto& secret=scene.logic.lifts.at(0);
        const int ox=mm.w(0xa0c34),oz=mm.w(0xa0c36);
        // Extermination places the map in a bigger grid: its offset from panel 0 (campaign cell 21,49).
        const auto first=*std::find_if(scene.logic.panels.begin(),scene.logic.panels.end(),[](const auto& p){return p.index==0;});
        const unsigned shiftX=((first.cell-mm.d(0xa0c40))/16)%scene.grid.width-21,shiftZ=((first.cell-mm.d(0xa0c40))/16)/scene.grid.width-49;
        for(unsigned index:{0u,36u,37u,39u}){
            const auto panel=*std::find_if(scene.logic.panels.begin(),scene.logic.panels.end(),[&](const auto& p){return p.index==index;});
            const double angle=panel.rotation*Pi/4;
            const int x=panel.x+int(std::lround(std::sin(angle)*160)),z=panel.z-int(std::lround(std::cos(angle)*160));
            const auto text=scene.logic.use(mm,scene.doors,x,panel.y,z,panel.rotation*512);
            check(text==L"Panel activated",deck?"An Extermination L131 secret panel did not activate":"An L131 secret panel did not activate");
        }
        for(auto cell:{std::pair{35u,30u},std::pair{35u,64u},std::pair{64u,13u}}){
            scene.logic.tick(mm,scene.doors,-32768,0,-32768);   // from elsewhere, then onto the trigger
            scene.logic.tick(mm,scene.doors,int((cell.first+shiftX)*512+256)-ox,0,int((cell.second+shiftZ)*512+256)-oz);
        }
        check(secret.moving&&!secret.targetHigh,deck?"Extermination L131 secret wall did not lower as played":"L131 secret wall did not lower as played");
    }
    std::ofstream(scene.root/"decompilation/reports/secret_order_checks.json")<<"{\"status\":\"passed\"}"<<'\n';
}
// Developer audit: Queen's Revenge on each arena, the Queens left alive over 20 seconds (decompilation/reports/
// queens_revenge_audit.txt).
void queensRevengeAudit(){
    std::ofstream out(scene.root/"decompilation/reports/queens_revenge_audit.txt");
    for(unsigned arena=0;arena<std::size(SurvivalArenaNames);++arena)for(unsigned run=0;run<3;++run){
        scene.menu.survivalArena=arena;if(run==0)menuCommand(nullptr,ExplorerMenu::QueensRevengeStart);else if(run==1)scene.restart();else{scene.player.health=0;scene.mem().w(0xa0ab8,0);scene.returnAfterDeath();if(!g_survival.active)menuCommand(nullptr,ExplorerMenu::QueensRevengeStart);}
        while(g_survival.introTicks)explorerSurvivalTick(scene);
        auto m=scene.mem();out<<"arena "<<arena<<" run "<<run;out<<" level="<<scene.level.filename().string()<<" records="<<m.w(0x2365f0);
        for(unsigned tick=0;tick<=600;++tick){
            if(tick%150==0){unsigned alive=0;std::string why;for(auto e:scene.enemies->active())if(m.b(e+0x74)==7){if(m.s(e+0x58)>0&&m.b(e+0x6f)<7)++alive;else why+=" dead(hp "+std::to_string(m.s(e+0x58))+" state "+std::to_string(m.b(e+0x6f))+")";}out<<" t"<<tick<<"="<<alive<<why;}
            scene.tick(0,0);
        }
        out<<'\n';
    }
}
// Developer audit: pulse rounds at Atmosphere Sub-level's lift crates, campaign and Extermination level 1
// (decompilation/reports/crate_shot_audit.txt): from the next cell on the lift, health before and after.
void crateShotAudit(){
    std::ofstream out(scene.root/"decompilation/reports/crate_shot_audit.txt");
    for(unsigned deck=0;deck<2;++deck){
        if(deck){scene.startIsolation(1);scene.mission.briefing=false;}else{scene.level=scene.root/"GameData/PS1/CD/SECT12/L154LEV.MAP";scene.load(true);}
        auto m=scene.mem();const int ox=m.w(0xa0c34),oz=m.w(0xa0c36);
        for(unsigned tick=0;tick<3;++tick)scene.logic.tick(m,scene.doors,-32768,0,-32768,true);
        const std::pair<unsigned,unsigned> from=deck?std::pair{51u,92u}:std::pair{16u,32u},crate=deck?std::pair{54u,92u}:std::pair{13u,32u};
        const auto at=m.d(0xa0c40)+(crate.second*scene.grid.width+crate.first)*16;const unsigned object=0x320000+m.b(at+4)*40;
        const int fx=int(from.first*512+256),fz=int(from.second*512+256),tx=int(crate.first*512+256),tz=int(crate.second*512+256);
        const Vec3s pos{short(fx-ox),short(floorHeight(scene.grid,fx,fz)),short(fz-oz)};
        const int yaw=int(std::lround(std::atan2(-double(tx-fx),double(tz-fz))*4096/(2*Pi)))&4095;
        const auto before=m.s(object+26);
        trilogy::original::awardPickup(m,2,99,4);scene.hud.select(2);bool fired=false;
        for(unsigned shot=0;shot<6;++shot){fired|=scene.enemies->fireWeapon(pos,yaw,0,2);for(unsigned t=0;t<8;++t){scene.enemies->tickProjectiles();scene.objects.tick();}}
        for(unsigned t=0;t<20;++t){scene.enemies->tickProjectiles();scene.objects.tick();}
        out<<(deck?"extermination":"campaign")<<" fired "<<fired<<" crate kind "<<int(m.b(object+24))<<" health "<<before<<" -> "<<m.s(object+26)
           <<" crateY "<<m.s(object+2)<<" shooterY "<<pos[1]<<" cellAttr "<<int(m.b(at+10))<<" floor "<<int(m.b(at+11))*32<<" destroyed "<<!scene.objects.visible(m.b(at+4))<<"|";
        out<<'\n';
    }
}
// Developer check: walk onto the first map's exit trigger of an Extermination level 1/2 through the real tick
// (decompilation/reports/exit_walk_audit.txt): what the logic says and where the player ends up.
void exitWalkAudit(){
    std::ofstream out(scene.root/"decompilation/reports/exit_walk_audit.txt");
    for(unsigned deck=1;deck<=2;++deck){
        scene.startIsolation(deck);scene.mission.briefing=false;auto m=scene.mem();const int ox=m.w(0xa0c34),oz=m.w(0xa0c36);
        unsigned trigger=~0u;
        for(unsigned c=0;c<scene.cells.size()&&trigger==~0u;++c){const auto id=m.b(m.d(0xa0c40)+c*16+15);if(!id||id>=m.b(0xa0c2a)||scene.exterminationPart(c)!=1)continue;
            for(unsigned pc=m.b(m.d(0xa0c64)+id*4+1),n=0;pc!=255&&pc<m.b(0xa0c2b)&&n<64;pc=m.b(m.d(0xa0c68)+pc*4+1),++n)if(m.b(m.d(0xa0c68)+pc*4)==8){trigger=c;break;}}
        out<<"deck "<<deck<<" exit trigger cell "<<trigger%scene.grid.width<<","<<trigger/scene.grid.width;
        const int x=int(trigger%scene.grid.width)*512+256,z=int(trigger/scene.grid.width)*512+256;
        scene.logic.tick(m,scene.doors,-32768,0,-32768);
        scene.player.position={short(x-ox),short(floorHeight(scene.grid,x,z)),short(z-oz)};scene.player.currentCell=trigger;
        const auto said=scene.logic.tick(m,scene.doors,x-ox,0,z-oz);
        out<<" says \""<<std::string(said.begin(),said.end())<<"\" message \""<<std::string(scene.logic.message.begin(),scene.logic.message.end())<<"\" now at "<<scene.player.currentCell%scene.grid.width<<","<<scene.player.currentCell/scene.grid.width<<" part "<<int(scene.exterminationPart(scene.player.currentCell))<<" unsupported "<<scene.logic.unsupported<<'\n';
    }
}
// Developer check: Extermination level 3's big lift (12) as walked: its side trigger, then onto it.
void liftWalkAudit(){
    std::ofstream out(scene.root/"decompilation/reports/lift_walk_audit.txt");
    scene.startIsolation(3);scene.mission.briefing=false;auto m=scene.mem();const int ox=m.w(0xa0c34),oz=m.w(0xa0c36);
    auto& lift=scene.logic.lifts.at(12);
    auto walk=[&](unsigned x,unsigned z){scene.logic.tick(m,scene.doors,-32768,0,-32768);const auto said=scene.logic.tick(m,scene.doors,int(x*512+256)-ox,0,int(z*512+256)-oz);
        out<<"at "<<x<<","<<z<<" says \""<<std::string(said.begin(),said.end())<<"\" lift y "<<lift.y<<" progress "<<lift.progress<<" moving "<<lift.moving<<" targetHigh "<<lift.targetHigh<<'\n';};
    out<<"lift 12 low "<<lift.low<<" high "<<lift.high<<" y "<<lift.y<<" threshold "<<lift.threshold<<'\n';
    walk(55,31);walk(55,33);
    for(unsigned t=0;t<200;++t)scene.logic.tick(m,scene.doors,int(55*512+256)-ox,0,int(33*512+256)-oz);
    out<<"after 200 ticks lift y "<<lift.y<<" moving "<<lift.moving<<'\n';
}
// Developer check: a campaign level loaded and run for a while with the player at its start, the enemies awake
// (--level-soak <code> <ticks>): decompilation/reports/level_soak.txt, written as it goes.
void levelSoak(){
    std::wistringstream args(std::wstring(GetCommandLineW()).substr(std::wstring(GetCommandLineW()).find(L"--level-soak")+12));
    std::wstring code;unsigned ticks=600;args>>code>>ticks;
    std::ofstream out(scene.root/"decompilation/reports/level_soak.txt");
    for(const auto& path:scene.physicalLevels.empty()?std::vector<fs::path>{}:scene.physicalLevels){(void)path;}
    fs::path level;for(auto& e:fs::recursive_directory_iterator(scene.root/"GameData/PS1/CD"))if(e.path().filename().wstring()==L"L"+code+L"LEV.MAP")level=e.path();
    out<<"level "<<level.string()<<std::endl;
    scene.level=level;scene.load();scene.mission.briefing=false;scene.menu.open=false;
    out<<"loaded, enemies "<<scene.enemies->active().size()<<std::endl;
    for(unsigned t=0;t<ticks;++t){scene.tick(0,0);if(t%60==0)out<<"tick "<<t<<" enemies "<<scene.enemies->active().size()<<" health "<<scene.player.health<<std::endl;}
    // walk: every open cell in turn, a tick on each (its triggers, its collisions, the enemies around it)
    auto m=scene.mem();const int ox=m.w(0xa0c34),oz=m.w(0xa0c36);unsigned visited=0;
    for(unsigned c=0;c<scene.cells.size();++c){
        const auto at=m.d(0xa0c40)+c*16;if(int(m.d(at))<0||(m.b(at+7)&0x80))continue;
        const int x=int(c%scene.grid.width)*512+256,z=int(c/scene.grid.width)*512+256;
        out<<"cell "<<c%scene.grid.width<<","<<c/scene.grid.width<<std::endl;
        scene.player.position={short(x-ox),short(floorHeight(scene.grid,x,z)),short(z-oz)};scene.previousPosition=scene.player.position;scene.player.currentCell=c;
        m.w(0xa0ab8,100);scene.player.health=100;scene.tick(0,0);++visited;
        if(!scene.player.health||scene.mission.complete||scene.exitFading){scene.exitFading=false;scene.mission.complete=false;}
    }
    out<<"walked "<<visited<<std::endl;
    for(unsigned id=1;id<m.b(0xa0c2a);++id){out<<"action "<<id<<std::endl;scene.logic.action(m,scene.doors,id,true);for(unsigned t=0;t<5;++t)scene.tick(0,0);scene.exitFading=false;scene.mission.complete=false;}
    out<<"done"<<std::endl;
}
// Developer check: draw a campaign level from every third open cell, four ways (--render-sweep <code>), logging
// each view to decompilation/reports/render_sweep.txt before it is drawn (a crash leaves the last one).
void renderSweep(){
    std::wistringstream args(std::wstring(GetCommandLineW()).substr(std::wstring(GetCommandLineW()).find(L"--render-sweep")+14));
    std::wstring code;args>>code;
    fs::path level;for(auto& e:fs::recursive_directory_iterator(scene.root/"GameData/PS1/CD"))if(e.path().filename().wstring()==L"L"+code+L"LEV.MAP")level=e.path();
    scene.level=level;scene.load();scene.mission.briefing=false;scene.menu.open=false;
    auto m=scene.mem();const int ox=m.w(0xa0c34),oz=m.w(0xa0c36);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=640;info.bmiHeader.biHeight=-480;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;auto dc=CreateCompatibleDC(nullptr);auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto previous=SelectObject(dc,bitmap);
    std::ofstream out(scene.root/"decompilation/reports/render_sweep.txt");unsigned views=0;
    for(unsigned c=0;c<scene.cells.size();c+=3){
        const auto at=m.d(0xa0c40)+c*16;if(int(m.d(at))<0||(m.b(at+7)&0x80))continue;
        const int x=int(c%scene.grid.width)*512+256,z=int(c/scene.grid.width)*512+256;
        scene.player.position={short(x-ox),short(floorHeight(scene.grid,x,z)),short(z-oz)};scene.previousPosition=scene.player.position;scene.player.currentCell=c;
        scene.tick(0,0);
        for(int yaw=0;yaw<4096;yaw+=1024){out<<"cell "<<c%scene.grid.width<<","<<c/scene.grid.width<<" yaw "<<yaw<<std::endl;scene.yaw=yaw;scene.draw(dc,640,480);GdiFlush();++views;}
    }
    out<<"done "<<views<<" views"<<std::endl;
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
}
// Developer check: shoot every enemy of a campaign level dead, as played (--shoot-sweep <code>): stand two cells
// off, face it, fire the pulse rifle and draw each tick; decompilation/reports/shoot_sweep.txt says where it got to.
void shootSweep(){
    std::wistringstream args(std::wstring(GetCommandLineW()).substr(std::wstring(GetCommandLineW()).find(L"--shoot-sweep")+13));
    std::wstring code;unsigned rounds=1,only=0;args>>code>>rounds>>only;   // only: one enemy kind (0: all)
    fs::path level;for(auto& e:fs::recursive_directory_iterator(scene.root/"GameData/PS1/CD"))if(e.path().filename().wstring()==L"L"+code+L"LEV.MAP")level=e.path();
    std::ofstream out(scene.root/"decompilation/reports/shoot_sweep.txt");
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=640;info.bmiHeader.biHeight=-480;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;auto dc=CreateCompatibleDC(nullptr);auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto previous=SelectObject(dc,bitmap);
    for(unsigned round=0;round<rounds;++round){
    scene.level=level;scene.load();scene.mission.briefing=false;scene.menu.open=false;
    auto m=scene.mem();m.d(0xa09c0,0x9e3779b9u*(round+1));m.d(0xa09b4,0x85ebca6bu^(round*7919));m.d(0xa09c4,round*104729u);
    out<<"round "<<round<<std::endl;
    for(unsigned w=0;w<5;++w)trilogy::original::awardPickup(m,w,99,9);scene.hud.select(2);scene.hud.syncAmmo();
    const auto targets=scene.enemies->active();
    for(auto e:targets){
        if(m.s(e+0x58)<=0)continue;const unsigned kind=m.b(e+0x74);if(only&&kind!=only)continue;
        for(unsigned t=0;t<240&&m.s(e+0x58)>0;++t){
            const int ex=m.s(e),ez=m.s(e+4);
            scene.player.position={short(ex-1024),short(m.s(e+2)),short(ez)};scene.previousPosition=scene.player.position;
            const int ox=m.w(0xa0c34),oz=m.w(0xa0c36);scene.player.currentCell=((ez+oz)>>9)*scene.grid.width+((ex-1024+ox)>>9);
            scene.yaw=3072;scene.pitch=0;m.w(0xa0ab8,100);scene.player.health=100;
            out<<"enemy "<<e<<" kind "<<kind<<" tick "<<t<<" health "<<m.s(e+0x58)<<std::endl;
            scene.firing=true;scene.tick(0,0);scene.draw(dc,640,480);GdiFlush();
        }
        scene.firing=false;for(unsigned t=0;t<60;++t){scene.tick(0,0);if(t%10==0){scene.draw(dc,640,480);GdiFlush();}}
        out<<"enemy "<<e<<" kind "<<kind<<" done health "<<m.s(e+0x58)<<std::endl;
    }
    }
    out<<"done"<<std::endl;
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
}
// Developer check: the sounds a level's Facehuggers ask for, and whether its bank has them (--hugger-sound-audit <code>).
void huggerSoundAudit(){
    std::wistringstream args(std::wstring(GetCommandLineW()).substr(std::wstring(GetCommandLineW()).find(L"--hugger-sound-audit")+20));
    std::wstring code;args>>code;
    fs::path level;for(auto& e:fs::recursive_directory_iterator(scene.root/"GameData/PS1/CD"))if(e.path().filename().wstring()==L"L"+code+L"LEV.MAP")level=e.path();
    scene.level=level;scene.load();scene.mission.briefing=false;scene.menu.open=false;auto m=scene.mem();
    std::map<unsigned,unsigned> asked;auto previous=scene.enemies->sound;
    scene.enemies->sound=[&](unsigned slot,unsigned entity){if(entity>=0x300000&&entity<0x300000+128*208&&m.b(entity+0x74)==2)++asked[slot];};
    std::ofstream out(scene.root/"decompilation/reports/hugger_sound_audit.txt");
    unsigned eggs=0;
    for(auto e:scene.enemies->active())if(m.b(e+0x74)==1||m.b(e+0x74)==2){   // beside eggs until they hatch, then the Facehuggers
        const Vec3s at{short(m.s(e)-900),short(m.s(e+2)),short(m.s(e+4))};
        for(unsigned t=0;t<300;++t){scene.player.position=at;m.w(0xa0ab8,100);scene.player.health=100;scene.tick(0,0);}
        if(++eggs==4)break;
    }
    for(auto [slot,count]:asked)out<<"slot "<<slot<<" asked "<<count<<" sample "<<scene.audio->hasSample(slot)<<'\n';
    {const auto list=scene.enemies->active();out<<"facehuggers "<<std::count_if(list.begin(),list.end(),[&](unsigned e){return m.b(e+0x74)==2;})<<'\n';}
    scene.enemies->sound=previous;
}
// Developer check: each level bank's sample lengths in the creature slots (--sound-bank-audit), one line a level.
void soundBankAudit(){
    std::ofstream out(scene.root/"decompilation/reports/sound_bank_audit.txt");
    for(const char* code:{"111","122","161","162","231","263","311","321","351","381","391"}){
        scene.audio->level(scene.root,code,-1,false,false);out<<code;
        for(unsigned slot=41;slot<70;++slot)out<<' '<<scene.audio->sampleLength(slot);
        out<<'\n';
    }
}
// Developer audit: the map objects of an Extermination level as loaded (decompilation/reports/object_audit.txt).
void objectAudit(){
    std::wistringstream args(std::wstring(GetCommandLineW()).substr(std::wstring(GetCommandLineW()).find(L"--object-audit")+14));
    unsigned deck=1;args>>deck;scene.startIsolation(deck);auto m=scene.mem();
    std::ofstream out(scene.root/"decompilation/reports/object_audit.txt");
    for(auto i:scene.objects.entries){
        const unsigned p=0x320000+i*40,cell=(m.d(p+20)-m.d(0xa0c40))/16,at=m.d(p+20);
        out<<"obj "<<i<<" kind "<<int(m.b(p+24))<<" cell "<<cell%scene.grid.width<<","<<cell/scene.grid.width<<" occupant "<<int(m.b(at+12))<<" index "<<int(m.b(at+4))
           <<" y "<<m.s(p+2)<<" floor "<<int(m.b(at+11))*32<<" health "<<m.s(p+26)<<" part "<<int(scene.exterminationPart(cell))<<'\n';
    }
    auto ops=[&](){unsigned bad=0;for(unsigned i=0;i<m.b(0xa0c2b);++i){const auto op=m.b(m.d(0xa0c68)+i*4);bad+=op>9&&op!=255;}return bad;};
    out<<"commands "<<int(m.b(0xa0c2b))<<" actions "<<int(m.b(0xa0c2a))<<" at "<<std::hex<<m.d(0xa0c68)<<std::dec<<" map end "<<std::hex<<(0x100000+0)<<std::dec<<" unsupported after load "<<ops();
    scene.mission.briefing=false;for(unsigned t=0;t<60;++t)scene.tick(0,0);out<<" after 60 ticks "<<ops()<<'\n';
    for(unsigned i=0;i<scene.logic.lifts.size();++i){const auto& l=scene.logic.lifts[i];out<<"lift "<<i<<" at "<<l.gx<<","<<l.gz<<" size "<<l.width<<"x"<<l.height<<" y "<<l.y<<" low "<<l.low<<" high "<<l.high<<" threshold "<<l.threshold<<'\n';}
}
// Developer audit: Use pressed in front of every panel of every map with no battery; writes
// decompilation/reports/panel_audit.txt (map, panel, kind, message, whether it switched on).
void panelAudit(){
    std::ofstream out(scene.root/"decompilation/reports/panel_audit.txt");
    for(const auto& path:scene.physicalLevels){
        scene.level=path;scene.load(true);
        const auto panels=scene.logic.panels;
        for(const auto& panel:panels){
            scene.level=path;scene.load(true);auto m=scene.mem();m.w(0xa0ad2,0);
            const double angle=panel.rotation*Pi/4;
            const int x=panel.x+int(std::lround(std::sin(angle)*160)),z=panel.z-int(std::lround(std::cos(angle)*160));
            const auto text=scene.logic.use(m,scene.doors,x,panel.y,z,panel.rotation*512);
            unsigned opened=0;for(const auto& d:scene.doors.entries)opened+=d.target;
            out<<path.filename().string()<<" panel "<<panel.index<<" kind "<<panel.kind<<" on "<<scene.logic.panelOn(m,panel.index)
               <<" doorsOpen "<<opened<<" msg "<<std::string(text.begin(),text.end())<<'\n';
        }
    }
}
// Developer camera shot on an Extermination deck: --extermination-shot <deck> <cell x> <cell z> <yaw> <pitch> <name>
// writes decompilation/reports/extermination-<name>.bmp (software renderer), aliens cleared, from that cell's floor.
void exterminationShot(){
    std::wistringstream args(std::wstring(GetCommandLineW()).substr(std::wstring(GetCommandLineW()).find(L"--extermination-shot")+20));
    unsigned deck=1,cx=0,cz=0;int yaw=0,pitch=0;std::wstring name=L"shot";
    args>>deck>>cx>>cz>>yaw>>pitch>>name;
    scene.startIsolation(deck);scene.mission.briefing=false;
    auto m=scene.mem();
    for(auto e:scene.enemies->active()){const auto kind=m.b(e+0x74);if(kind>=1&&kind<=7)scene.enemies->killWithTrap(e);}
    const int x=int(cx)*512+256,z=int(cz)*512+256;
    Vec3s at{short(x-scene.player.originX),short(floorHeight(scene.grid,x,z)),short(z-scene.player.originZ)};
    scene.player.position=at;scene.previousPosition=at;scene.player.currentCell=scene.grid.index(x,z);
    scene.yaw=yaw&4095;scene.pitch=pitch;
    for(unsigned tick=0;tick<3;++tick)scene.enemies->tick(at,100);
    if(name.find(L"round")!=std::wstring::npos){   // a marine's pulse round crossing the view, for checking how it draws
        const double angle=scene.yaw*2*Pi/4096,fx=-std::sin(angle),fz=std::cos(angle),rx=std::cos(angle),rz=std::sin(angle);
        scene.enemies->squadRounds.push_back({{short(at[0]+fx*900-rx*500),short(at[1]+620),short(at[2]+fz*900-rz*500)},{short(at[0]+fx*4000+rx*600),short(at[1]+500),short(at[2]+fz*4000+rz*600)},1});
    }
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=640;info.bmiHeader.biHeight=-480;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;auto dc=CreateCompatibleDC(nullptr);auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    auto previous=SelectObject(dc,bitmap);
    scene.draw(dc,640,480);GdiFlush();
    std::ofstream out(scene.root/"decompilation/reports"/(L"extermination-"+name+L".bmp"),std::ios::binary);
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+640*480*4;
    out.write(reinterpret_cast<const char*>(&file),sizeof file);out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof info.bmiHeader);
    out.write(static_cast<const char*>(bits),640*480*4);
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
}
void exterminationViewsCheck(){
    scene.startIsolation(1);scene.mission.briefing=false;
    auto m=scene.mem();
    for(auto e:scene.enemies->active()){const auto kind=m.b(e+0x74);if(kind>=1&&kind<=7)scene.enemies->killWithTrap(e);}
    for(unsigned tick=0;tick<40;++tick)scene.enemies->tick(scene.player.position,100);   // let the bodies settle
    const auto ally=scene.enemies->exterminationSquad().begin()->first;
    const struct {const char* name;int dx,dz;} views[]={{"front",0,-1400},{"side",-1400,0},{"back",0,1400}};
    for(const auto& v:views){
        m.w(ally+0x42,0);m.w(ally+0x44,0);m.b(ally+0x6d,0);   // heading 0: facing -z (an enemy heading is the camera's yaw plus a half turn)
        Vec3s at{short(m.s(ally)+v.dx),scene.player.position[1],short(m.s(ally+4)+v.dz)};
        scene.player.position=at;scene.previousPosition=at;
        scene.yaw=int(std::lround(std::atan2(-double(m.s(ally)-at[0]),double(m.s(ally+4)-at[2]))*4096.0/(2*Pi)))&4095;
        scene.pitch=0;
        for(unsigned tick=0;tick<3;++tick){scene.enemies->tick(at,100);m.w(ally+0x42,0);m.b(ally+0x6d,0);}
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=640;info.bmiHeader.biHeight=-480;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* bits=nullptr;auto dc=CreateCompatibleDC(nullptr);auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        auto previous=SelectObject(dc,bitmap);
        scene.draw(dc,640,480);GdiFlush();
        std::ofstream out(scene.root/"decompilation/reports"/(std::string("extermination-ally-")+v.name+".bmp"),std::ios::binary);
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);
        file.bfSize=file.bfOffBits+640*480*4;
        out.write(reinterpret_cast<const char*>(&file),sizeof file);out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof info.bmiHeader);
        out.write(static_cast<const char*>(bits),640*480*4);
        SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
    }
}
void isolationSelfTest(){
    auto check=[](bool value,const char* what){if(!value)throw std::runtime_error(what);};
    const auto fixture=scene.root/"decompilation/reports"/("isolation-test-"+std::to_string(GetCurrentProcessId()));{std::error_code stale;fs::remove_all(fixture,stale);}
    scene.menu.load(scene.root,fixture);
    const auto achievements=fixture/"achievements.txt";
    scene.menu.achievements.load(achievements);
    auto visible=[](){
        const auto rows=scene.menu.labels();
        return std::find(rows.begin(),rows.end(),L"Extermination")!=rows.end();
    };
    scene.menu.show(false);check(!visible(),"Isolation unlocked before campaign victory");
    scene.menu.achievements.campaignExit("391",true,{100,100,100},1);
    scene.menu.achievements.load(achievements);
    check(visible(),"Campaign victory did not persist the Isolation unlock");
    const auto unlockedRows=scene.menu.labels();
    const auto survival=std::find(unlockedRows.begin(),unlockedRows.end(),L"Survival");
    check(survival!=unlockedRows.end()&&std::next(survival)!=unlockedRows.end()&&
          *std::next(survival)==L"Extermination","Isolation is not below Survival");
    scene.menu.selection=unsigned(std::next(survival)-unlockedRows.begin());
    scene.menu.key(VK_RETURN);
    check(scene.menu.page=="colony isolation"&&scene.menu.labels().front()==L"Play Extermination",
          "Isolation submenu did not open");
    menuCommand(nullptr,scene.menu.key(VK_RETURN));
    if(scene.isolationStage!=1||!scene.mission.briefing)
        throw std::runtime_error(std::string(scene.menu.message.begin(),scene.menu.message.end()));
    for(unsigned stage=1;stage<=3;++stage){
        check(scene.isolationStage==stage&&scene.mission.briefing,"Isolation did not advance");
        scene.menu.refreshExtermination();   // its own save: the level reached
        check(scene.menu.exterminationSaved==(stage==1?0u:stage),"Extermination did not save the level reached");
        const auto actors=scene.enemies->active();
        check(std::any_of(actors.begin(),actors.end(),[&](unsigned e){return scene.mem().b(e+0x74)==5;})&&
              std::any_of(actors.begin(),actors.end(),[&](unsigned e){return scene.mem().b(e+0x74)==6;}),
              "Extermination requires Dogs and Warriors");
        check(scene.enemies->exterminationSquad().size()==3&&scene.hud.selected()==2&&scene.hud.owns(2)&&
              !scene.hud.owns(0)&&!scene.hud.owns(1)&&!scene.hud.owns(3)&&!scene.hud.owns(4),
              "Extermination loadout is invalid");
        {
            auto m=scene.mem();
            for(const auto& [ally,maximum]:scene.enemies->exterminationSquad())
                check(m.b(ally+0x74)==14&&m.d(ally+0x7c)==0x9c3fc,"Extermination squad does not wear the co-op Alien Handler suit");
            scene.enemies->tick(scene.player.position,100);
            for(const auto& [ally,maximum]:scene.enemies->exterminationSquad())   // the player walks through the squad
                check(m.b(m.d(ally+0x78)+12)!=13&&m.b(m.d(ally+0x78)+12)!=14,"An Extermination marine blocks the player");
            std::set<unsigned> kinds,cells;unsigned aliens=0;
            for(auto e:actors){
                const auto kind=m.b(e+0x74);
                if(kind<1||kind>6)continue;
                kinds.insert(kind);++aliens;
                check(cells.insert(m.d(e+0x78)).second,"Two Extermination aliens share a spawn cell");
            }
            check(kinds.size()==6&&aliens>=30,"Extermination deck lacks the mixed alien packs");
            // A Warrior beside an ally, the player far off: it goes for the ally, and its blows land there - the
            // player's health and the ally's stay as they were.
            const auto ally=scene.enemies->exterminationSquad().begin()->first;
            const auto warrior=*std::find_if(actors.begin(),actors.end(),[&](unsigned e){return m.b(e+0x74)==6;});
            for(auto e:actors){const auto kind=m.b(e+0x74);if(e!=warrior&&kind>=1&&kind<=6)scene.enemies->killWithTrap(e);}   // only this Warrior left
            const int ox=m.w(0xa0c34),oz=m.w(0xa0c36),wx=m.s(ally)+384,wz=m.s(ally+4);
            m.w(warrior,wx);m.w(warrior+2,m.s(ally+2));m.w(warrior+4,wz);
            m.d(warrior+0x78,m.d(0xa0c40)+(((wz+oz)>>9)*scene.grid.width+((wx+ox)>>9))*16);
            // Far off: the pickup (on reachable floor) farthest from the ally, in the same map of a stitched deck.
            Vec3s distant=scene.player.position;double farthest=0;
            const auto allyPart=scene.exterminationPart(((m.s(ally+4)+oz)>>9)*scene.grid.width+((m.s(ally)+ox)>>9));
            for(auto item:scene.pickups.active()){
                const unsigned cell=((m.s(item+4)+oz)>>9)*scene.grid.width+((m.s(item)+ox)>>9);
                const double d=std::hypot(double(m.s(item)-m.s(ally)),double(m.s(item+4)-m.s(ally+4)));
                if(scene.exterminationPart(cell)==allyPart&&d>farthest){farthest=d;distant={short(m.s(item)),scene.player.position[1],short(m.s(item+4))};}
            }
            // The ally is nearly spent: the Warrior's blows land on it (never the player) and put it down.
            m.w(ally+0x58,1);
            const unsigned absorbed=scene.enemies->squadAttacksAbsorbed;
            const auto shotsBefore=scene.enemies->squadShots;bool roundsSeen=false;for(unsigned tick=0;tick<120;++tick){scene.enemies->tick(distant,100);roundsSeen|=!scene.enemies->squadRounds.empty();check(m.w(0xa0ab8)==100,"An attack on an ally hurt the player");}
            check(scene.enemies->squadShots>shotsBefore&&roundsSeen,"The marines fired without visible rounds");
            check(scene.enemies->squadAttacksAbsorbed>absorbed&&scene.enemies->exterminationDowned(ally)&&m.s(ally+0x58)==0,
                  "Aliens do not attack and down the Extermination squad");
            check(m.s(0xa0a48)==distant[0]&&m.s(0xa0a4c)==distant[2],"Extermination targeting left the player position changed");
            // Held Use beside the body for 1.5 seconds: back up with a third of their health.
            scene.player.position={short(m.s(ally)+200),short(m.s(ally+2)),short(m.s(ally+4))};scene.player.health=100;
            scene.useHeld=true;
            for(unsigned tick=0;tick<Scene::ReviveTicks;++tick)scene.tickExterminationRevive();
            scene.useHeld=false;
            const auto maximum=scene.enemies->exterminationSquad().at(ally);
            check(!scene.enemies->exterminationDowned(ally)&&unsigned(m.s(ally+0x58))==std::max(1u,maximum/3),"Holding Use did not revive the downed marine");
            // A medikit: 40% back to every marine standing.
            m.w(ally+0x58,1);scene.enemies->healExterminationSquad();
            check(unsigned(m.s(ally+0x58))==std::min(maximum,1+maximum*2/5),"A medikit did not heal the squad by 40%");
            // A marine with no way out (boxed in by solid cells, like the pool) and the player far off: within three
            // seconds they catch up to free floor near the player, never onto another marine. (Deck 1: it clears the
            // deck; decks 2 and 3 keep the Warrior for the sweep check below.)
            if(stage==1){
                scene.enemies->killWithTrap(warrior);   // nothing left to shoot: the marine wants to rejoin the player
                const int ax=distant[0]+ox,az=distant[2]+oz;
                const Vec3s away{distant[0],short(floorHeight(scene.grid,unsigned(ax),unsigned(az))),distant[2]};
                const auto boxed=std::next(scene.enemies->exterminationSquad().begin())->first;
                const auto home=m.d(boxed+0x78);const unsigned width=scene.grid.width;
                std::vector<std::pair<unsigned,unsigned>> walls;
                for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){if(!dx&&!dz)continue;const auto c=home+unsigned((dz*int(width)+dx)*16);walls.push_back({c,m.b(c+7)});m.b(c+7,m.b(c+7)|0x80);}
                bool rejoined=false;
                for(unsigned tick=0;tick<90&&!rejoined;++tick){
                    scene.enemies->tick(away,100);
                    rejoined=std::hypot(double(m.s(boxed)-away[0]),double(m.s(boxed+4)-away[2]))<=2600;
                }
                for(const auto& [c,b7]:walls)m.b(c+7,b7);
                check(rejoined,"A stuck marine did not catch up with the player");
                for(const auto& [other,unused]:scene.enemies->exterminationSquad())
                    check(other==boxed||m.d(other+0x78)!=m.d(boxed+0x78),"A marine caught up onto another marine");
            }
        }
        scene.mission.briefing=false;
        auto m=scene.mem();
        const auto supplies=scene.pickups.active();
        for(unsigned kind:{11u,20u})check(std::any_of(supplies.begin(),supplies.end(),[&](unsigned item){
            return m.b(item+32)==kind&&(kind!=20||m.b(item+33)==40);
        }),"Extermination ammo or 40-health medikit is missing");
        for(auto item:supplies)check(m.b(item+32)<1||m.b(item+32)>4,"A remixed deck still offers a weapon");
        // The remixed campaign map: its own textures, PS1 lighting, doors and lifts; tags in solid props.
        check(scene.level.parent_path().filename()==std::to_string(stage)&&scene.textures.rect(0)!=nullptr,"Extermination deck map or textures did not load");
        check(scene.ps1Lighting.active,"Extermination deck lost its PS1 lighting");
        check(!scene.doors.entries.empty()&&!scene.logic.lifts.empty(),"Extermination deck lost its doors or lifts");
        for(const auto& tag:scene.isolationTags)check(tag.container>=9||(m.b(m.d(0xa0c40)+tag.cell*16+7)&0x80),"An ID tag prop is not solid");
        const auto inContainers=std::count_if(scene.isolationTags.begin(),scene.isolationTags.end(),[](const auto& tag){return tag.container>=9;});
        check(inContainers*2>=long(scene.isolationTags.size()),"Most ID tags should be in the deck's own crates and lockers");
        {   // the only ID tag pickups are Extermination's own (a campaign tag picked up would count as one of them)
            unsigned records=0;for(unsigned i=0;i<m.w(0x236c4c);++i)records+=m.b(m.d(0x236c34)+i*8+2)==15;
            check(records==unsigned(inContainers),"A campaign ID tag was left in an Extermination level");
        }
        if(scene.exterminationArrival!=~0u)for(unsigned part=1;part<=2;++part)   // both maps of a stitched level hold tags
            check(std::any_of(scene.isolationTags.begin(),scene.isolationTags.end(),[&](const auto& tag){return scene.exterminationPart(tag.cell)==part;}),"A map of a stitched Extermination level has no ID tags");
        for(const auto& tag:scene.isolationTags)if(tag.container==9||tag.container==10){
            const auto box=scene.exterminationContainer(tag.cell);
            check(box&&m.b(box+24)==(tag.container==9?20:25),"An ID tag's crate or locker is missing or the wrong kind");
        }
        for(const auto& panel:scene.logic.panels)check(m.b(panel.cell+7)&0x80,"A console or switch panel can be walked through");
        for(auto entity:scene.enemies->active()){   // no alien or marine starts inside a wall block or pillar
            const unsigned x=unsigned(m.s(entity)+m.w(0xa0c34))>>9,z=unsigned(m.s(entity+4)+m.w(0xa0c36))>>9;
            const auto type=m.b(m.d(0xa0c40)+(z*scene.grid.width+x)*16+10);
            check(type!=1&&(type<20||type>24),"An Extermination alien or marine starts inside scenery");
        }
        for(auto i:scene.objects.entries){   // crates, lockers and barrels stand on open floor or a lift, never in rock
            const auto p=0x320000+i*40;const unsigned kind=m.b(p+24);
            if(kind==20||kind==23||kind==25)check(m.b(m.d(p+20)+10)!=20,"A crate, locker or barrel sits in solid rock (it cannot be shot)");
        }
        for(auto i:scene.objects.entries){   // blastable walls stand across the map's own passages (turned and mirrored maps too)
            const auto p=0x320000+i*40;const unsigned kind=m.b(p+24);if(kind!=21&&kind!=28&&kind!=29)continue;
            const auto corner=(m.d(p+20)-m.d(0xa0c40))/16;
            unsigned open=0,total=0;   // (Mining and Smelting authors one wall a cell into the rock)
            for(unsigned dz=0;dz<m.b(p+33);++dz)for(unsigned dx=0;dx<m.b(p+32);++dx){
                const auto at=m.d(0xa0c40)+(corner+dz*scene.grid.width+dx)*16;
                ++total;open+=int(m.d(at))>=0;check(m.b(at+12)==kind,"A blastable wall does not block its cells");
            }
            check(open*4>=total*3,"A blastable wall is out of place (it can be walked through)");
        }
        check(m.w(0xa0ac8)>=4&&m.w(0xa0ad0)>=2,"Extermination did not hand out grenades and seismic charges");
        for(const auto& tag:scene.isolationTags){   // a tag prop stands against a wall, never in the open
            if(tag.container>=9)continue;
            check(tag.side>=1&&tag.side<=4,"An ID tag prop has no wall to stand against");
            const unsigned w=scene.grid.width,c=tag.cell;
            auto wall=[&](unsigned n){const auto at=m.d(0xa0c40)+n*16;return int(m.d(at))<0||(m.b(at+7)&0x80);};
            check(wall(c-1)||wall(c+1)||wall(c-w)||wall(c+w),"An ID tag prop stands in the open");
        }
        {   // the level's exit (in a stitched level, the second map's - the first map's only leads on) stays shut
            const auto where=scene.player.currentCell;
            if(scene.exterminationArrival!=~0u)scene.player.currentCell=scene.exterminationArrival;
            check(!scene.beginExit(false)&&!scene.exitFading,"Extermination extraction opened before the tags and the sweep");
            scene.player.currentCell=where;
        }
        for(const auto& tag:scene.isolationTags){
            if(tag.container>=9){   // break it open (or find it on the floor), walk over it
                const auto before=scene.pickups.active();
                unsigned item=0;
                if(tag.container==11){
                    for(auto p:before)if(m.b(p+32)==15&&(m.d(p+28)-m.d(0xa0c40))/16==tag.cell)item=p;
                    check(item!=0,"An ID tag on the floor is missing");
                }else{
                    const auto box=scene.exterminationContainer(tag.cell);
                    scene.pickups.crateDrop(box,scene.exterminationTagSlot(box));
                    const auto dropped=scene.pickups.active();
                    check(dropped.size()==before.size()+1&&m.b(dropped.front()+32)==15,"A crate or locker did not drop its ID tag");
                    item=dropped.front();
                }
                const auto after=scene.pickups.active();
                for(unsigned t=0;t<90&&scene.pickups.active().size()==after.size();++t){
                    scene.player.position={short(m.s(item)),short(m.s(item+2)),short(m.s(item+4))};
                    const auto previous=scene.pickups.collected;
                    scene.pickups.tick(m.s(item),m.s(item+2),m.s(item+4));
                    if(scene.pickups.collected!=previous&&scene.pickups.lastKind==15)scene.exterminationTagFound();
                }
                check(tag.collected,"A dropped ID tag was not recovered");
                continue;
            }
            scene.player.currentCell=tag.cell;
            scene.useIsolation();
        }
        check(scene.isolationTagsComplete()&&m.w(0xa0ae2)==scene.isolationTags.size(),"Isolation did not collect every tag");
        if(scene.exterminationArrival!=~0u)scene.player.currentCell=scene.exterminationArrival;   // at the second map's end, not the first's
        if(!scene.exterminationClear())check(!scene.beginExit(false),"Extermination allowed extraction before the sweep");
        for(auto entity:scene.enemies->active()){
            const auto kind=m.b(entity+0x74);
            if(kind>=2&&kind<=6)scene.enemies->killWithTrap(entity);
        }
        if(stage==3){
            {   // the way down to the Queen's level: Egg Chambers' big lift (12), its side trigger then onto it, as walked
                const int ox=m.w(0xa0c34),oz=m.w(0xa0c36);auto& lift=scene.logic.lifts.at(12);
                for(auto cell:{std::pair{55,31},std::pair{55,33}}){
                    scene.logic.tick(m,scene.doors,-32768,0,-32768);
                    scene.logic.tick(m,scene.doors,cell.first*512+256-ox,0,cell.second*512+256-oz);
                }
                for(unsigned t=0;t<200;++t)scene.logic.tick(m,scene.doors,55*512+256-ox,0,33*512+256-oz);
                check(lift.y==lift.low,"Egg Chambers' big lift did not take the squad down to the Queen's level");
                // its lift switch (panel 0's chain) works again and again, not once only
                for(unsigned press=0;press<2;++press){
                    check(scene.logic.action(m,scene.doors,1,true),"Egg Chambers' lift switch would not work a second time");
                    for(unsigned t=0;t<300;++t)scene.logic.tick(m,scene.doors,-32768,0,-32768);
                }
            }
            check(scene.isolationTerminal==~0u&&!scene.isolationQueen,"Egg Chambers still has a security terminal or a Queen");
        }
        if(scene.exterminationArrival!=~0u){   // two maps: the first area's exit elevator rides the squad into the second
            unsigned first=0;
            while(first<scene.exterminationParts.size()&&scene.exterminationParts[first]!=1)++first;
            scene.player.currentCell=first;
            {   // walked onto, through the logic as in play: the exit trigger leads on and reports nothing unsupported
                unsigned trigger=~0u;
                for(unsigned c=0;c<scene.cells.size()&&trigger==~0u;++c){const auto id=m.b(m.d(0xa0c40)+c*16+15);if(!id||id>=m.b(0xa0c2a)||scene.exterminationPart(c)!=1)continue;
                    for(unsigned pc=m.b(m.d(0xa0c64)+id*4+1),n=0;pc!=255&&pc<m.b(0xa0c2b)&&n<64;pc=m.b(m.d(0xa0c68)+pc*4+1),++n)if(m.b(m.d(0xa0c68)+pc*4)==8){trigger=c;break;}}
                check(trigger!=~0u,"The first map has no exit trigger");
                const int ox=m.w(0xa0c34),oz=m.w(0xa0c36),tx=int(trigger%scene.grid.width)*512+256,tz=int(trigger/scene.grid.width)*512+256;
                const auto unsupported=scene.logic.unsupported;
                scene.logic.tick(m,scene.doors,-32768,0,-32768);
                scene.player.position={short(tx-ox),short(floorHeight(scene.grid,tx,tz)),short(tz-oz)};scene.player.currentCell=trigger;
                const auto said=scene.logic.tick(m,scene.doors,tx-ox,0,tz-oz);
                check(scene.logic.unsupported==unsupported&&said==scene.exterminationSecondTitle&&scene.player.currentCell==scene.exterminationArrival,"Walking onto the first map's exit did not lead cleanly into the second map");
                scene.yaw=(scene.yaw+2048)&4095;scene.useIsolation();   // back, for the checks below
                scene.player.currentCell=first;
            }
            check(scene.beginExit(false)&&!scene.exitFading&&scene.player.currentCell==scene.exterminationArrival,"The first area's elevator room did not lead into the second map");
            {   // facing the way the elevator opens, Use works what is there (its door) and never turns the squad back
                const auto opened=scene.doors.opened;
                scene.useIsolation();
                check(scene.player.currentCell==scene.exterminationArrival||scene.doors.opened==opened,"Use at the elevator's door took the squad back instead of opening it");
                if(scene.player.currentCell!=scene.exterminationArrival)scene.beginExit(false);   // (no door there: it went back; on again)
            }
            // and back: Use in the arrival elevator, facing its back wall (facing the door, Use is for the door)
            scene.yaw=(scene.yaw+2048)&4095;
            scene.useIsolation();
            check(scene.exterminationReturn!=~0u&&scene.player.currentCell==scene.exterminationReturn,"Use in the second map's elevator did not lead back to the first map");
            check(scene.beginExit(false)&&scene.player.currentCell==scene.exterminationArrival,"The first map's exit did not lead into the second map again");
            for(const auto& [ally,maximum]:scene.enemies->exterminationSquad()){
                const unsigned x=unsigned(m.s(ally)+scene.player.originX)>>9,z=unsigned(m.s(ally+4)+scene.player.originZ)>>9;
                check(scene.exterminationPart(z*scene.grid.width+x)==2,"A marine was left behind in the first map");
            }
        }
        check(scene.beginExit(false)&&scene.exitFading,"The deck's exit elevator did not extract the squad");
        for(unsigned tick=0;tick<25;++tick)scene.tick(0,0);
    }
    {   // level 3's finale: the Queen's Lair, the Queen out at once, a Smartgun just ahead, the squad's kit carried over
        check(scene.isolationStage==4&&!scene.mission.briefing,"Egg Chambers' exit did not lead to the Queen's Lair");
        auto m=scene.mem();
        check(scene.isolationQueen&&m.s(scene.isolationQueen+0x58)>0&&m.b(scene.isolationQueen+0x74)==7,"The Queen was not released in her lair");
        const auto items=scene.pickups.active();
        const auto smartgun=std::find_if(items.begin(),items.end(),[&](unsigned p){return m.b(p+32)==4;});
        check(smartgun!=items.end(),"No Smartgun waiting in the Queen's Lair");
        check(std::hypot(double(m.s(*smartgun)-scene.player.position[0]),double(m.s(*smartgun+4)-scene.player.position[2]))<=1600,"The Smartgun is not right in front of the squad");   // (two cells ahead, from wherever in the cell the squad stands)
        check(scene.hud.owns(2),"The squad's pulse rifle did not come down to the lair");
        scene.pickups.tick(m.s(*smartgun),m.s(*smartgun+2),m.s(*smartgun+4));
        check(scene.hud.owns(3),"The lair's Smartgun could not be picked up");
        check(!scene.beginExit(false),"The finale allowed extraction with the Queen alive");
        m.w(scene.isolationQueen+0x58,0);
        for(unsigned tick=0;tick<200&&scene.isolationStage;++tick)scene.tick(0,0);
    }
    check(!scene.isolationStage&&scene.menu.page=="main"&&scene.menu.open,"Isolation finale did not return to the title");
    scene.menu.refreshExtermination();check(!scene.menu.exterminationSaved,"A finished Extermination run left its save behind");
    {   // Continue from a level-2 save, through the menu
        scene.saveExtermination(2);scene.menu.show(false);
        const auto rows=scene.menu.labels();scene.menu.selection=unsigned(std::find(rows.begin(),rows.end(),L"Extermination")-rows.begin());
        scene.menu.key(VK_RETURN);
        check(scene.menu.labels().front()==L"Continue Extermination - Level 2","The Extermination page did not offer its save");
        scene.menu.selection=0;menuCommand(nullptr,scene.menu.key(VK_RETURN));
        check(scene.isolationStage==2,"Continue Extermination did not start the saved level");
        // Extermination's own slots: Save Game in its pause menu, Load Extermination on its page
        check(scene.menu.exterminationGame,"The Extermination pause menu does not save to Extermination's slots");
        scene.menu.show(true);scene.menu.page="pause";scene.menu.selection=0;
        {const auto rows=scene.menu.labels();check(std::find(rows.begin(),rows.end(),L"Save Game")!=rows.end(),"The Extermination pause menu has no Save Game");}
        menuCommand(nullptr,ExplorerMenu::ExterminationSave2);
        check(scene.menu.exterminationSlotLevel(1)==2&&scene.menu.exterminationSlot(1).starts_with(L"Slot 2 - Level 2"),"Extermination did not save level 2 to its slot 2");
        scene.returnToMainMenu();scene.saveExtermination(0);scene.menu.show(false);
        {const auto rows=scene.menu.labels();scene.menu.selection=unsigned(std::find(rows.begin(),rows.end(),L"Extermination")-rows.begin());}
        scene.menu.key(VK_RETURN);
        {const auto rows=scene.menu.labels();scene.menu.selection=unsigned(std::find(rows.begin(),rows.end(),L"Load Extermination")-rows.begin());check(scene.menu.selection<rows.size(),"The Extermination page has no Load Extermination");}
        scene.menu.key(VK_RETURN);check(scene.menu.page=="extermination load","Load Extermination did not open its slots");
        scene.menu.selection=1;menuCommand(nullptr,scene.menu.key(VK_RETURN));
        check(scene.isolationStage==2,"Loading Extermination slot 2 did not start level 2");
        scene.returnToMainMenu();
        check(!scene.menu.exterminationGame,"The story's pause menu kept Extermination's slots");
        {std::error_code gone;fs::remove(scene.menu.exterminationSlotPath(1),gone);}
    }
    fs::remove(achievements);fs::remove_all(fixture);
}
void replaySelfTest(){
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    scene.menu.load(scene.root,scene.root/"decompilation/reports"/("replay_fixture_"+std::to_string(GetCurrentProcessId())));
    scene.replayEnabled=true;
    auto recordAndWatch=[&](unsigned mode){
        scene.menu.open=false;scene.mission.briefing=false;
        const auto originalYaw=scene.yaw&4095;
        const auto position=scene.player.position;
        const auto wave=g_survival.wave;
        scene.recordReplayFrame();
        scene.yaw=(originalYaw+128)&4095;
        scene.recordReplayFrame();
        const auto path=scene.replay.destination;
        scene.finishReplayRecording();
        check(fs::is_regular_file(path),"Replay clip was not published");
        scene.menu.show(false);scene.menu.showReplays();
        check(std::find(scene.menu.replayFiles.begin(),scene.menu.replayFiles.end(),path)!=scene.menu.replayFiles.end(),"Replay clip missing in browser");
        scene.watchReplay(path);
        if(!scene.replaying)throw std::runtime_error("Replay failed: "+std::string(scene.menu.message.begin(),scene.menu.message.end()));
        check(scene.player.position==position&&scene.yaw==originalYaw,"First replay frame differs");
        check(g_survival.active==(mode==1||mode==3)&&scene.coopGame==(mode==2)&&g_survival.wave==wave,"Replay mode state differs");
        auto dc=CreateCompatibleDC(nullptr);
        auto bitmap=CreateBitmap(640,480,1,32,nullptr);
        auto previous=SelectObject(dc,bitmap);
        scene.draw(dc,640,480);
        SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
        check(scene.replayFrame()&&scene.yaw==((originalYaw+128)&4095),"Replay delta frame differs");
        double clock=0;
        scene.advanceReplay(1./120,clock);
        check(std::abs(scene.renderAlpha-.25)<1e-6&&scene.replayPreviousYaw==originalYaw,"Replay interpolation lost the previous camera");
        windowProc(nullptr,WM_KEYDOWN,'P',0);
        check(scene.photoMode,"Replay photo mode did not open");
        scene.advanceReplay(1.,clock);
        check(scene.replaying&&std::abs(scene.renderAlpha-.25)<1e-6,"Photo mode did not freeze replay playback");
        if(mode==0){
            const auto photo=scene.capturePhoto(nullptr,640,480);
            check(photo.parent_path()==scene.menu.replayScreenshots(path)&&fs::file_size(photo)>54,"Replay screenshot was not saved");
            fs::remove(photo);fs::remove(photo.parent_path());
        }
        windowProc(nullptr,WM_KEYDOWN,VK_ESCAPE,0);
        check(scene.replaying&&!scene.photoMode,"Exiting photo mode closed the replay");
        check(!scene.replayFrame(),"Replay should end at recorded frame");
        scene.stopReplay();
        check(scene.menu.open&&scene.menu.page=="replays","Replay did not return to browser");
        fs::remove(path);
    };
    recordAndWatch(0);
    scene.menu.survivalArena=0;
    menuCommand(nullptr,ExplorerMenu::SurvivalStart);
    check(g_survival.active,"Could not initialize replay survival fixture");
    recordAndWatch(1);
    coop.testStart();
    recordAndWatch(2);
    for(unsigned arena=0;arena<std::size(SurvivalArenaNames);++arena){
        scene.menu.survivalArena=arena;
        menuCommand(nullptr,ExplorerMenu::QueensRevengeStart);
        check(g_survival.active&&g_survival.cfg.queensRevenge()&&g_survival.wave==1&&g_survival.cfg.waves==1,"Queen's Revenge did not start");
        check(g_survival.lives==1,"Queen's Revenge must start with one life");
        check(g_survival.introTicks==explorer_survival::kIntroBlackTicks+explorer_survival::kIntroFadeTicks,
              "Queen's Revenge did not start with a fade");
        scene.audio->validateMixer(ExplorerAudio::SupplyAlarm);
        while(g_survival.introTicks)explorerSurvivalTick(scene);
        check(g_survival.breakTicks==0,"Queen's Revenge gained a wave countdown");

        for(unsigned weapon=0;weapon<5;++weapon)check(scene.hud.owns(weapon),"Queen's Revenge weapon missing");
        const auto queens=scene.enemies->active();
        auto m=scene.mem();
        check(queens.size()==5,"Queen's Revenge did not spawn five enemies");
        check(scene.pickups.active().empty(),"Queen's Revenge spawned arena supplies");
        const auto target=queens.front();
        m.w(0xa0ab8,60);scene.player.health=60;
        m.w(target+0x58,m.w(target+0x58)-100);
        explorerSurvivalTick(scene);
        check(scene.player.health==60&&scene.pickups.active().empty(),"Queen damage paid out supplies (only every second Queen killed does)");
        explorerSurvivalTick(scene);
        for(auto index:g_survival.doorEntry){
            const auto& door=scene.doors.entries[index];
            check(!door.target&&door.amount==0.f,"Queen's Revenge elevator opened");
            for(auto cell:door.cells)check(m.b(cell+10)==20,"Queen's Revenge elevator is passable");
        }
        for(auto queen:queens)
            check(m.b(queen+0x74)==7&&m.s(queen+0x58)>0&&!m.w(queen+0x4e),"Queen was not released");
        if(arena==0)recordAndWatch(3);
        else{
            // Supplies: none for the first Queen killed; the second drops a full medikit and 250 rounds for each gun.
            m.w(queens[0]+0x58,0);explorerSurvivalTick(scene);
            check(scene.pickups.active().empty(),"Queen's Revenge dropped supplies for one Queen");
            m.w(queens[1]+0x58,0);explorerSurvivalTick(scene);
            {
                const auto drops=scene.pickups.active();unsigned medikits=0,clips=0;
                for(auto p:drops){if(m.b(p+32)==20&&m.b(p+33)==100)++medikits;else if(m.b(p+33)==250)++clips;}
                check(drops.size()==6&&medikits==1&&clips==5,"Two Queens killed did not drop a full medikit and 250 rounds per gun");
            }
            for(unsigned i=2;i<4;++i)m.w(queens[i]+0x58,0);
            explorerSurvivalTick(scene);
            check(!g_survival.ended&&g_survival.alive==1,"Queen's Revenge ended before the last Queen");
            m.w(queens.back()+0x58,0);
            explorerSurvivalTick(scene);
            check(g_survival.victory&&g_survival.ended,"Queen's Revenge did not finish after the fifth Queen");
            // Retry after the run ended (death or victory): the arena again with five Queens, not the campaign map
            // that shares its code.
            g_survival.active=false;scene.restart();
            while(g_survival.introTicks)explorerSurvivalTick(scene);
            unsigned again=0;for(auto e:scene.enemies->active())again+=scene.mem().b(e+0x74)==7;
            check(g_survival.active&&scene.level==g_survival.cfg.map&&again==5,"Restarting Queen's Revenge lost its arena or its five Queens");
            g_survival={};
        }
    }
    const auto achievementFile=scene.menu.replayDirectory().parent_path()/"achievements.txt";
    scene.menu.achievements.load(achievementFile);
    scene.menu.achievements.unlock(1);
    std::wofstream(scene.menu.survivalHistoryPath())<<L"\"Colony Hive\" 23 2 120 105 48 8 0 5000 \"Victory\"\n";
    scene.menu.show(false);scene.menu.showStats();
    const auto summary=scene.menu.statsSummary();
    check(summary.find(L"Survival kills  23")!=std::wstring::npos&&summary.find(L"Best score  5000")!=std::wstring::npos,"Stats did not read Survival history");
    scene.menu.key(VK_RETURN);
    check(scene.menu.page=="unlocked achievements"&&scene.menu.unlockedAchievements==std::vector<unsigned>{1},"Stats did not show unlocked achievements");
    fs::remove(achievementFile);fs::remove(scene.menu.survivalHistoryPath());
    std::ofstream(scene.root/"decompilation/reports/replay_checks.json")<<"{\"status\":\"passed\",\"campaign\":true,\"survival\":true,\"coop_snapshot\":true}";
}
void coopSessionSelfTest(){
 auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};
 Scene hostScene,guestScene;hostScene.root=guestScene.root=scene.root;hostScene.level=guestScene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";hostScene.load();guestScene.load();ExplorerCoopRuntime host(hostScene),guest(guestScene);host.testStart();
 auto input=trilogy::coop::PlayerInput{};input.yaw=hostScene.yaw&4095;auto initial=hostScene.player.position;host.testStep(input);host.testReplicate(guest);require(guestScene.coopGame&&guestScene.coopLocal==1&&guestScene.player.health==hostScene.coopMarines[1].health,"Guest player replica differs");require(guestScene.enemies->active()==hostScene.enemies->active(),"Enemy state is not shared");
 auto target=host.testCombatFixture();trilogy::coop::PlayerInput guestFire{};guestFire.yaw=0;guestFire.buttons=trilogy::coop::Fire;for(unsigned i=0;i<20;++i){host.testGuestInput(guestFire);host.testStep(input);}require(hostScene.mem().s(target+0x58)<=0&&hostScene.coopScore.kills[1]==1&&hostScene.coopScore.kills[0]==0,"Guest projectile kill did not award shared team score");host.testReplicate(guest);require(guestScene.mem().s(target+0x58)<=0&&guestScene.coopScore.total==hostScene.coopScore.total,"Guest kill not shared with host world");require(host.testAmmo(0)==15&&host.testAmmo(1)<15,"Guest firing changed host ammo");
 host.testStart();input.yaw=hostScene.yaw&4095;
 host.testClearEnemies();auto m=hostScene.mem();auto pickups=hostScene.pickups.active();require(!pickups.empty(),"Co-op pickup fixture missing");auto item=pickups.front();auto before=host.testPickupCount(0);auto beforeGuest=host.testPickupCount(1);Vec3s position{short(m.s(item)),short(m.s(item+2)),short(m.s(item+4))};host.testPosition(0,position);host.testPosition(1,initial);host.testStep(input);require(host.testPickupCount(0)<before,"Host did not collect personal pickup");require(host.testPickupCount(1)==beforeGuest,"Host pickup disappeared for teammate");host.testReplicate(guest);require(guestScene.pickups.active().size()==host.testPickupCount(1),"Guest saw host pickup removals");
 host.testDamage(1,0);for(unsigned i=0;i<61;++i)host.testStep(input);require(hostScene.coopMarines[1].health==100,"Safe teammate respawn did not run");host.testReplicate(guest);require(guestScene.player.health==100,"Guest did not receive respawn");
 host.testExtract();host.testStep(input);require(host.phase==trilogy::coop::Phase::Results&&hostScene.menu.page=="results","Shared assessment not reached");host.testReplicate(guest);require(guestScene.menu.page=="results"&&guestScene.menu.open,"Guest did not mirror assessment");hostScene.menu.resultsRevealed=true;hostScene.menu.key(VK_RETURN);host.testReplicate(guest);require(guestScene.menu.page=="coop-results","Guest did not mirror host menu navigation");
 host.testLevel("161");host.testClearEnemies();host.testSpawnPersonalTag();input.yaw=hostScene.yaw&4095;host.testStep(input);require(hostScene.mem().w(0xa0ae2)==1&&hostScene.coopScore.pickups[0]&&hostScene.coopScore.pickups[1],"Personal ID copies counted twice or were not available to both");host.testReplicate(guest);require(guestScene.mem().w(0xa0ae2)==1,"Shared ID objective differs on guest");
 unsigned levelChecks=0;for(const auto& map:hostScene.physicalLevels){const auto code=map.stem().string().substr(1,3);host.testLevel(code);input.yaw=hostScene.yaw&4095;for(unsigned tick=0;tick<6;++tick)host.testStep(input);host.testReplicate(guest);require(guestScene.mission.sourceCode==code,"Co-op level replica changed campaign stage");++levelChecks;}
 for(const auto& [last,next]:std::array<std::pair<const char*,const char*>,2>{{{"162","211"},{"263","311"}}}){
  host.testLevel(last);host.testObjectivesPassed();host.testExtract();host.testStep(input);
  host.testAdvanceCampaign();host.testReplicate(guest);
  require(guestScene.mission.sourceCode==next,"Co-op campaign stopped at a section boundary");
 }
 host.testLevel("391");host.testObjectivesPassed();host.testExtract();host.testStep(input);
 host.testAdvanceCampaign();host.testReplicate(guest);
 require(host.phase==trilogy::coop::Phase::Finished&&guest.phase==trilogy::coop::Phase::Finished,
         "Co-op campaign did not finish at the last mission");
 for(unsigned mode:{1u,2u}){
  for(unsigned arena=0;arena<std::size(SurvivalArenaNames);++arena){
   g_survival={};
   host.testSurvival(mode,arena);
   require(host.testPickupCount(0)==host.testPickupCount(1),"Survival supplies were not instanced");
   if(mode==2)require(hostScene.hud.owns(4)&&hostScene.enemies->active().size()==5&&g_survival.lives==1,
                      "Co-op Queen's Revenge loadout or enemies differ");
   const auto world=hostScene.enemies->active();
   host.testReplicate(guest);
   require(guestScene.coopLocal==1&&guestScene.enemies->active()==world&&g_survival.cfg.queensRevenge()==(mode==2),
           "Co-op Survival replica did not load the selected mode and arena");
  }
 }
 g_survival={};
 // Team wipe preserves connection/lobby flow, not a single-player restart.
 host.testStart();host.testDamage(0,0);host.testDamage(1,0);host.testStep(input);require(host.phase==trilogy::coop::Phase::Lobby&&!host.playing()&&hostScene.menu.page=="coop-lobby","Team wipe did not return host to lobby");host.testReplicate(guest);require(guestScene.menu.page=="coop-lobby"&&!guestScene.menu.hasGame,"Team wipe did not return guest to lobby");
 std::ofstream(scene.root/"decompilation/reports/coop_session_checks.json")<<"{\"status\":\"passed\",\"shared_enemies\":true,\"guest_projectile_kill_and_score\":true,\"campaign_maps\":36,\"survival_mode_arena_pairs\":6,\"personal_pickups\":true,\"safe_respawn\":true,\"team_wipe_lobby\":true,\"host_menus_mirrored\":true}";
}
void coopRulesSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
 auto m=scene.mem();
 for(unsigned kind:{13u,14u}){
  unsigned e=0x31e000;for(unsigned i=0;i<208;++i)m.b(e+i,0);
  m.b(e+0x74,kind);m.d(e+0x7c,kind==13?0x9c67c:0x9c3fc);m.d(e+0xa0,0x31f000);startEntityAnimation(m,e,0);
  auto sprite=scene.enemies->sprite(e);require(!sprite.pixels.empty(),"Co-op avatar sprite missing");
  BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=sprite.width;info.biHeight=-int(sprite.height);info.biPlanes=1;info.biBitCount=32;file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+sprite.pixels.size()*4;
  std::ofstream out(scene.root/("decompilation/reports/coop-avatar-"+std::to_string(kind)+".bmp"),std::ios::binary);out.write((char*)&file,sizeof(file));out.write((char*)&info,sizeof(info));out.write((char*)sprite.pixels.data(),sprite.pixels.size()*4);
 }
 scene.menu.cheatsUnlocked=scene.menu.godMode=scene.menu.infiniteAmmo=scene.menu.noClip=true;
 scene.beginCoopPlayers();require(scene.hud.selected()==0&&scene.hud.owns(0)&&m.w(0xa0abe)==15,"Co-op pistol loadout missing");for(unsigned i=1;i<5;++i)require(!scene.hud.owns(i),"Co-op inherited a solo weapon");require(!(m.b(0x94b9c)&36),"Co-op inherited cheat flags");
 scene.menu.page="options";auto labels=scene.menu.labels();require(std::find(labels.begin(),labels.end(),L"Cheats")==labels.end(),"Co-op exposed cheats menu");
 auto ammo=m.w(0xa0abe);scene.menu.selection=2;menuCommand(nullptr,ExplorerMenu::CheatsChanged);require(m.w(0xa0abe)==ammo&&!scene.hud.owns(1),"Co-op accepted direct give-weapons action");
 scene.menu.page="cheats";scene.menu.selection=0;require(scene.menu.key(VK_RIGHT)==ExplorerMenu::None,"Co-op accepted hidden cheat action");
 auto level=scene.level;scene.menu.open=false;for(auto key:{VK_NEXT,VK_PRIOR})windowProc(nullptr,WM_KEYDOWN,key,0);require(scene.level==level,"Co-op allowed level skipping");
 auto& score=scene.coopScore;using Event=trilogy::coop::TeamScore::Event;score.award(0,Event::AlienKill);score.award(1,Event::QueenKill);score.award(1,Event::Pickup);require(score.total==1110&&score.kills[0]==1&&score.kills[1]==1,"Co-op score is not shared");score.award(0,Event::Death);require(score.total==1010&&score.deaths[0]==1,"Co-op team penalty missing");bool rejected=false;try{score.award(2,Event::AlienKill);}catch(...){rejected=true;}require(rejected,"Invalid scoring player accepted");
 scene.coopGame=false;scene.menu.coopSession=false;scene.menu.page="options";labels=scene.menu.labels();require(std::find(labels.begin(),labels.end(),L"Cheats")!=labels.end(),"Co-op restriction leaked into solo menu");
 scene.menu.coopSession=true;scene.menu.page="coop-results";scene.menu.coopHost=false;scene.menu.selection=0;require(scene.menu.key(VK_RETURN)==ExplorerMenu::None&&scene.menu.page=="coop-results","Guest advanced host results");require(scene.menu.key(VK_ESCAPE)==ExplorerMenu::None&&scene.menu.page=="coop-results","Guest dismissed host results");scene.menu.coopHost=true;require(scene.menu.key(VK_RETURN)==ExplorerMenu::CoopAdvance,"Host could not advance results");
 trilogy::coop::Recovery recovery;std::array<trilogy::coop::Marine,2> players{};players[1].health=0;std::array<trilogy::coop::Recovery::Threat,1> threats{{{{0,0,0},true}}};for(unsigned i=0;i<90;++i)require(recovery.tick(players,threats)==trilogy::coop::Recovery::Action::None,"Respawn allowed beside hostile enemy");threats[0].position[0]=8192;for(unsigned i=0;i<59;++i)require(recovery.tick(players,threats)==trilogy::coop::Recovery::Action::None,"Respawn safe interval skipped");require(recovery.tick(players,threats)==trilogy::coop::Recovery::Action::RespawnGuest,"Safe guest respawn missing");players[0].health=0;require(recovery.tick(players,threats)==trilogy::coop::Recovery::Action::ReturnToLobby,"Team wipe did not select lobby");
 scene.menu.coopSession=false;
 std::ofstream(scene.root/"decompilation/reports/coop_rules_checks.json")<<"{\"status\":\"passed\",\"pistol_only\":true,\"shared_score\":true,\"cheats_blocked\":true,\"level_skip_blocked\":true}";
}
void bulletTrailSelfTest(){
 auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};auto p=setupBulletTrailFixture();auto m=scene.mem();unsigned profiles=0;
 for(unsigned mode=0;mode<3;++mode)for(unsigned kind:{0u,1u,2u,3u,4u,5u}){m.w(0x96234,mode);m.b(p+30,kind);auto trail=explorerBulletTrail(m,p);bool expected=(kind==0||kind==2||kind==4||kind==5)&&mode!=2;require(bool(trail)==expected,"Projectile visual dispatch differs from original kind/difficulty rules");if(trail){require(trail->end[2]-trail->start[2]==(kind==0?128:256),"Native bullet trail length mismatch");require(trail->lines==(kind==4?2u:1u),"Native smartgun double trail missing");require(trail->color[0]==0||trail->color[1]==0||trail->color[2]==0,"Native tracer start colour mismatch");}++profiles;}
 m.w(0x96234,1);m.b(p+30,0);preview(scene.root/"decompilation/reports/pistol-projectile-preview.bmp");require(scene.lastBulletTrails==1&&scene.lastBulletTrailPixels>0,"Live software view did not draw pistol projectile");m.b(p+30,4);preview(scene.root/"decompilation/reports/smartgun-projectile-preview.bmp");require(scene.lastBulletTrails==2&&scene.lastBulletTrailPixels>0,"Live software view did not draw smartgun double trail");m.b(p+30,0);
 scene.vertices={{-1024,0,512},{1024,0,512},{1024,2048,512},{-1024,2048,512}};Face wall{};wall.vertices={0,1,2,3};wall.model=true;wall.atlas=4;wall.texture=9999;scene.faces={wall};preview(scene.root/"decompilation/reports/projectile-wall-occlusion-preview.bmp");require(scene.lastBulletTrails==1&&!scene.lastBulletTrailPixels,"Projectile trail rendered through opaque wall");scene.faces.clear();scene.vertices.clear();m.w(m.d(p)+4,6145);preview(scene.root/"decompilation/reports/projectile-distance-preview.bmp");require(!scene.lastBulletTrails,"Original maximum trail draw distance ignored");m.w(m.d(p)+4,256);preview(scene.root/"decompilation/reports/projectile-distance-preview.bmp");require(!scene.lastBulletTrails,"Original near trail cull ignored");
 std::ofstream out(scene.root/"decompilation/reports/bullet_trail_checks.json");out<<"{\"status\":\"passed\",\"native_dispatch_profiles\":"<<profiles<<",\"pistol_half_velocity\":true,\"smartgun_double_line\":true,\"wall_occlusion\":true,\"native_distance_culling\":true}";
}
void flameSelfTest(){
 auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};auto p=setupBulletTrailFixture();auto m=scene.mem();m.b(p+30,3);m.b(p+28,8);
 for(unsigned difficulty=0;difficulty<3;++difficulty){m.w(0x96234,difficulty);auto glow=explorerFlameGlow(m,p,7);require(glow&&glow->frame==7&&glow->halfSize==48,"Original flame dispatch/size mismatch");}
 preview(scene.root/"decompilation/reports/flamethrower-preview.bmp");require(scene.lastFlameSprites==1&&scene.lastFlamePixels>100,"Flamethrower sprite missing from software renderer");
 m.b(p+28,2);require(explorerFlameGlow(m,p,0)->halfSize==140,"Original late flame growth missing");
 scene.vertices={{-1024,0,512},{1024,0,512},{1024,2048,512},{-1024,2048,512}};Face wall{};wall.vertices={0,1,2,3};wall.model=true;wall.atlas=4;wall.texture=9999;scene.faces={wall};preview(scene.root/"decompilation/reports/flame-wall-preview.bmp");require(scene.lastFlameSprites==1&&!scene.lastFlamePixels,"Flames visible through solid wall");
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();m=scene.mem();scene.menu.open=false;scene.mission.briefing=false;awardPickup(m,3,100,3);scene.hud.select(4);auto fuel=m.w(0xa0aca);require(scene.enemies->fireWeapon(scene.player.position,scene.yaw,0,4,0),"Actual flamethrower did not fire");unsigned streams=0;for(auto projectile=m.d(0x236300);projectile;projectile=m.d(projectile+36))streams+=m.b(projectile+30)==3;require(streams==2&&m.w(0xa0aca)==fuel-1,"Original flame streams/fuel consumption changed");preview(scene.root/"decompilation/reports/flamethrower-live-preview.bmp");require(scene.lastFlameSprites>0,"Actual fired flame not rendered");
 std::ofstream out(scene.root/"decompilation/reports/flame_checks.json");out<<R"({"status":"passed","difficulties":3,"original_growth":true,"software_pixels":true,"wall_occlusion":true})";
}
void passwordSelfTest(){
 auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();ExplorerPasswordState state;state.level=24;state.difficulty=1;state.weapon=3;state.inventory[0]=124;auto set=[&](unsigned offset,unsigned n){state.inventory[offset]=n;state.inventory[offset+1]=n>>8;};set(4,123);set(6,72);set(24,99);set(10,7);set(14,12);set(28,5);set(16,88);set(22,333);set(12,2);set(18,3);set(20,15);set(26,4);
 require(explorerPassword(state)==L"TQBGVBFF4ZBVSFBHPBBG5BLDBQQM9F3G","Password differs from original PS1 0x80047584 oracle");unsigned profiles=0;for(unsigned level=0;level<35;++level)for(unsigned mode=0;mode<3;++mode)for(unsigned weapon=0;weapon<5;++weapon){state.level=level;state.difficulty=mode;state.weapon=weapon;auto code=explorerPassword(state);auto restored=explorerDecodePassword(code);require(restored&&restored->inventory==state.inventory&&restored->level==level&&restored->difficulty==mode&&restored->weapon==weapon,"Original password did not restore packed state");auto corrupt=code;corrupt[0]=corrupt[0]==L'B'?L'C':L'B';require(!explorerDecodePassword(corrupt),"Password checksum failed to reject corruption");++profiles;}
 state.level=24;state.difficulty=1;state.weapon=3;scene.menu.load(scene.root,scene.root/"decompilation/reports/password_menu_fixture");scene.menu.show(false);scene.menu.page="password";scene.menu.passwordText.clear();preview(scene.root/"decompilation/reports/password-empty-preview.bmp");scene.menu.inputCharacter(L'B');preview(scene.root/"decompilation/reports/password-partial-preview.bmp");scene.menu.key(VK_BACK);for(auto c:explorerPassword(state))scene.menu.inputCharacter(c);require(scene.menu.key(VK_RETURN)==ExplorerMenu::LoadPassword,"Password menu submission missing");menuCommand(nullptr,ExplorerMenu::LoadPassword);require(scene.mission.index==24&&scene.mission.briefing&&scene.difficulty==1&&scene.player.health==123&&scene.mem().w(0xa0aca)==333,"Password menu did not load original campaign/inventory");
 for(const auto& code:{L"1G0TP1NK8C1DB00TS0N",L"DY1NGT0N1GHT",L"B0X0VTR1CKS",L"0BL1V10NTR1P",L"G0LVL03"}){scene.menu.page="password";scene.menu.passwordText=code;menuCommand(nullptr,ExplorerMenu::LoadPassword);require(scene.menu.message==L"CHEATS ACTIVATED","Original PS1 cheat code rejected");}require(scene.menu.cheatsUnlocked&&scene.menu.godMode&&scene.menu.infiniteAmmo&&scene.menu.startLevel==3,"Original cheat effects missing");scene.menu=ExplorerMenu{};scene.load();auto m=scene.mem();m.w(0xa0abe,15);scene.hud.syncAmmo();auto before=scene.hud.status();auto beforeHealth=scene.hud.statusHealth;require(scene.enemies->fireWeapon(scene.player.position,scene.yaw,0,0,0),"HUD shot fixture failed");scene.hud.syncAmmo();auto afterShot=scene.hud.status();require(scene.hud.statusHealth==beforeHealth,"Firing changed health HUD value");unsigned changedLeft=0,changedRight=0;for(unsigned y=0;y<150;++y)for(unsigned x=215;x<320;++x)changedRight+=before.pixels[y*320+x]!=afterShot.pixels[y*320+x];for(unsigned y=200;y<240;++y)for(unsigned x=0;x<150;++x)changedLeft+=before.pixels[y*320+x]!=afterShot.pixels[y*320+x];require(changedLeft&&!changedRight,"Firing changed upper-right health instead of lower-left ammo");auto shot=afterShot;m.w(0xa0ab8,beforeHealth-10);auto damaged=scene.hud.status();unsigned damageLeft=0,damageRight=0;for(unsigned y=0;y<150;++y)for(unsigned x=215;x<320;++x)damageRight+=shot.pixels[y*320+x]!=damaged.pixels[y*320+x];for(unsigned y=200;y<240;++y)for(unsigned x=0;x<150;++x)damageLeft+=shot.pixels[y*320+x]!=damaged.pixels[y*320+x];require(damageRight&&!damageLeft,"Damage changed ammo display instead of health display");require(scene.hud.statusHealth==beforeHealth-10,"Damage did not update health HUD");require((scene.hud.status().pixels[19*320+215]&0xffffff)==0,"Health backing is not black");
 std::ofstream out(scene.root/"decompilation/reports/password_checks.json");out<<"{\"status\":\"passed\",\"round_trip_profiles\":"<<profiles<<",\"native_ps1_oracle\":true,\"cheat_codes\":5,\"hud_shot_wiring\":true}";
}
void missionThresholdSelfTest(){
 auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();auto m=scene.mem();unsigned profiles=0;std::ofstream audit(scene.root/"decompilation/reports/mission_threshold_audit.json");audit<<"{\"levels\":[";
 for(unsigned index=0;index<35;++index){scene.mission.index=index;scene.mission.kinds={20};m.w(0xa0bc8+40,100);unsigned minimum=index<12?16:index<22?26:46;require(scene.mission.minimumPercent()==minimum,"Chapter mission threshold mismatch");for(unsigned difficulty=0;difficulty<3;++difficulty){m.w(0x96234,difficulty);for(unsigned score=0;score<=100;++score){m.w(0xa0ae6+40,score);require(scene.mission.percent()==score&&scene.mission.eligible()==(score>=minimum),"Original assessment boundary differs");unsigned expected=index;if(score>=minimum){expected=index+1;if(score<85)for(unsigned j=0;j<8;++j)if(expected==m.w(0x95fa6+j*2)){++expected;break;}}require(scene.mission.nextIndex()==expected,"Original retry/bonus progression mismatch");++profiles;}}
  auto resource=m.w(0x9d800+index*2);auto name=m.d(0x94cdc+resource*12);std::string code;for(unsigned j=0;j<80&&m.b(name+j);++j)code+=char(m.b(name+j));if(index)audit<<",";audit<<"{\"index\":"<<index<<",\"code\":\""<<code.substr(code.find("LEV")+3,3)<<"\",\"minimum_mission_percent\":"<<minimum<<",\"objective_kinds\":[";bool firstKind=true;for(unsigned k=0;k<5;++k){auto kind=m.b(0x96121+index*5+k);if(kind==255)continue;if(!firstKind)audit<<",";firstKind=false;audit<<unsigned(kind);}audit<<"]}";
 }audit<<"],\"status\":\"passed\",\"bonus_minimum\":85}";
 scene.mission.index=34;scene.mission.kinds={7};m.w(0xa0bc8+14,1);m.w(0xa0ae6+14,0);require(!scene.mission.eligible(),"Living queen satisfied queen objective");m.w(0xa0ae6+14,1);require(scene.mission.eligible()&&scene.mission.percent()==100,"Dead queen did not satisfy queen objective");
 scene.load();m=scene.mem();auto entry=scene.entryInventory;scene.mission.complete=true;scene.checkpointDirectory=scene.root/"decompilation/reports/failed_mission_save_fixture";bool rejected=false;try{scene.checkpoint(0,true);}catch(const std::exception&){rejected=true;}require(rejected,"Failed assessment permitted progress save");scene.menu.load(scene.root,scene.root/"decompilation/reports/threshold_menu_fixture");scene.menu.showResults(scene.mission.statistics().percent,false,scene.mission.minimumPercent());scene.menu.key(VK_RETURN);scene.menu.key(VK_RETURN);require(scene.menu.labels()==std::vector<std::wstring>{L"Retry Mission",L"Quit Game"},"Failed mission offered advance/save");preview(scene.root/"decompilation/reports/mission-failed-assessment.bmp");require(scene.menu.key(VK_RETURN)==ExplorerMenu::Restart,"Failed mission did not request retry");m.w(0xa0aca,99);scene.restart();require(scene.inventory()==entry&&scene.mission.sourceCode=="111"&&scene.mission.briefing,"Retry did not restore mission-entry equipment");
 std::ofstream out(scene.root/"decompilation/reports/mission_threshold_checks.json");out<<"{\"status\":\"passed\",\"assessment_profiles\":"<<profiles<<",\"campaign_levels\":35,\"failed_save_rejected\":true,\"retry_inventory_restored\":true}";scene.menu=ExplorerMenu{};scene.checkpointDirectory.clear();scene.load();
}
void liftSelfTest(){
 auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};std::ostringstream report;report<<"{\n  \"levels\": [\n";bool first=true;unsigned unreachable=0;
 for(auto path:scene.physicalLevels){scene.level=path;scene.load(true);auto m=scene.mem();auto& lifts=scene.logic.lifts;if(lifts.empty())continue;
  std::vector<unsigned> moves(lifts.size(),0),startY(lifts.size());std::vector<int> last(lifts.size());for(unsigned i=0;i<lifts.size();++i)last[i]=lifts[i].y;
  auto settle=[&]{for(unsigned tick=0;tick<400;++tick){scene.logic.tick(m,scene.doors,scene.player.position[0],scene.player.position[1],scene.player.position[2]);for(unsigned i=0;i<lifts.size();++i)if(lifts[i].y!=last[i]){if(!lifts[i].moving)++moves[i];last[i]=lifts[i].y;}}};
  for(unsigned round=0;round<6;++round)for(unsigned action=1;action<64;++action){m.w(0xa0ad2,10);scene.logic.action(m,scene.doors,action,true);settle();}
  std::string never,once;for(unsigned i=0;i<lifts.size();++i){if(!moves[i]&&lifts[i].threshold)never+=(never.empty()?"":",")+std::to_string(i);else if(moves[i]==1)once+=(once.empty()?"":",")+std::to_string(i);}
  unreachable+=std::count(never.begin(),never.end(),',')+(never.empty()?0:1);
  if(!first)report<<",\n";first=false;report<<"    {\"level\":\""<<path.stem().string()<<"\",\"lifts\":"<<lifts.size()<<",\"never_moved\":\""<<never<<"\",\"moved_once\":\""<<once<<"\"}";
 }
 // 131 Medical Laboratory: Secret Pulse Rifle room (door #2, unlocked by action #4 + action #11, re-openable via action #5)
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L131LEV.MAP";scene.load(true);auto m=scene.mem();
 require(!scene.logic.action(m,scene.doors,5,true),"131 secret door #2 opened before unlocking panels");
 require(scene.logic.action(m,scene.doors,4,true)&&!scene.doors.entries.at(2).target,"131 first secret panel #4 failed");
 require(scene.logic.action(m,scene.doors,11,true)&&scene.doors.entries.at(2).target,"131 second secret panel #11 did not open door #2");
 for(unsigned tick=0;tick<200;++tick){scene.logic.tick(m,scene.doors,-32768,0,-32768);scene.doors.tick(m,-32768,-32768);}
 require(!scene.doors.entries.at(2).target&&scene.doors.entries.at(2).amount==0,"131 secret door #2 did not auto-close after hold timer");
 require(scene.logic.action(m,scene.doors,5,true)&&scene.doors.entries.at(2).target,"131 secret door #2 locked player inside after closing");
 // 161 Atmosphere Sub-Basement: Killing ceiling Xenomorphs (WARCEIL=8 / DOGCEIL=9) must switch animation table and decode death sprites cleanly
 scene.level=scene.root/"GameData/PS1/CD/SECT12/L161LEV.MAP";scene.load(true);m=scene.mem();unsigned ceilingKilled=0;
 for(auto e:scene.enemies->active()){auto kind=m.b(e+0x74);if(kind!=8&&kind!=9)continue;
  scene.player.position={short(m.s(e)),short(m.s(e+2)-512),short(m.s(e+4))};
  while(m.s(e+0x58)>0){m.w(0xa0abe,10);scene.enemies->shoot(scene.player.position,0,512,256.0);if(m.s(e+0x58)>0){m.w(e+0x58,1);trilogy::original::enterEntityDeath(m,e,[](auto&,unsigned,unsigned,unsigned,unsigned){return trilogy::original::PlayerControlResult{};});m.w(e+0x58,0);}}
  require(m.b(e+0x6f)==7&&m.b(e+0x74)==(kind==8?6:5)&&m.d(e+0x7c)==(kind==8?0x9c324u:0x9c37cu),"Ceiling Xenomorph death did not switch animation table");
  for(unsigned tick=0;tick<40;++tick){scene.enemies->tick(scene.player.position,100);scene.enemies->sprite(e);}++ceilingKilled;
 }
 require(ceilingKilled>0,"No ceiling Xenomorphs found in L161LEV");
 // 141 Garage: Walk-on & panel lifts (#10, #11 auto-return, #12 repeatable up/down)
 scene.level=scene.root/"GameData/PS1/CD/SECT12/L141LEV.MAP";scene.load(true);m=scene.mem();
 m.w(0xa0ad2,5);require(scene.logic.action(m,scene.doors,16,true),"141 battery panel #16 failed");
 require(scene.logic.action(m,scene.doors,27,false)&&scene.logic.lifts.at(10).moving,"141 walk-on action #27 did not start lift #10");
 for(unsigned tick=0;tick<60&&scene.logic.lifts.at(10).moving;++tick)scene.logic.tick(m,scene.doors,-32768,0,-32768);
 require(!scene.logic.lifts.at(10).atHigh&&scene.logic.lifts.at(10).remaining==90,"141 lift #10 did not reach low position with hold timer");
 for(unsigned tick=0;tick<300;++tick)scene.logic.tick(m,scene.doors,-32768,0,-32768);
 require(scene.logic.lifts.at(10).atHigh&&!scene.logic.lifts.at(10).moving&&scene.logic.lifts.at(10).progress==0,"141 lift #10 did not auto-return to high position");
 require(scene.logic.action(m,scene.doors,26,true)&&scene.logic.lifts.at(10).moving,"141 repeatable panel #26 failed to trigger lift #10");
 require(scene.logic.action(m,scene.doors,36,true)&&scene.logic.action(m,scene.doors,35,false)&&scene.logic.lifts.at(12).moving,"141 lift #12 panel #36 + walk-on #35 failed");
 for(unsigned tick=0;tick<60&&scene.logic.lifts.at(12).moving;++tick)scene.logic.tick(m,scene.doors,-32768,0,-32768);
 require(!scene.logic.lifts.at(12).atHigh&&scene.logic.lifts.at(12).progress==0,"141 lift #12 did not stay at low position ready for return");
 require(scene.logic.action(m,scene.doors,39,true)&&scene.logic.action(m,scene.doors,35,false)&&scene.logic.lifts.at(12).moving,"141 lift #12 return panel #39 + walk-on #35 failed");
 // 154 Atmosphere Sub-level: Walk-on before panel must not desync paired lifts #1 and #8 or burn panel #13
 scene.level=scene.root/"GameData/PS1/CD/SECT12/L154LEV.MAP";scene.load(true);m=scene.mem();
 require(!scene.logic.action(m,scene.doors,14,false),"154 walk-on #14 before panel #13 should not succeed");
 require(scene.logic.lifts.at(1).progress==0&&scene.logic.lifts.at(8).progress==0,"154 walk-on #14 before panel #13 corrupted lift #8 progress");
 require(scene.logic.action(m,scene.doors,13,true),"154 panel #13 failed");
 require(!scene.logic.action(m,scene.doors,13,true),"154 one-shot panel #13 should not re-trigger while lift #1/#8 already armed");
 require(scene.logic.action(m,scene.doors,14,false)&&scene.logic.lifts.at(1).moving&&scene.logic.lifts.at(8).moving,"154 walk-on #14 did not start both lifts #1 and #8");
 report<<"\n  ],\n  \"status\": \"passed\",\n  \"lifts_never_moved\": "<<unreachable<<"\n}\n";std::ofstream(scene.root/"decompilation/reports/lift_checks.json")<<report.str();scene.load();
}
void podSelfTest(){
 auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};unsigned count=0,maps=0;
 for(auto path:scene.physicalLevels){scene.level=path;scene.load(true);auto m=scene.mem();bool hasPods=false;for(auto panel:scene.logic.panels){if(panel.kind!=32)continue;hasPods=true;auto object=0x320000+panel.index*40;auto action=scene.logic.panelAction(m,panel.index);require(action!=0,"Pod has no authored activation chain");require(m.s(object+12)==0,"Pod starts partly open");
  if(path.stem()=="L131LEV"&&panel.index==7){scene.menu.open=false;scene.mission.briefing=false;scene.interpolate=false;scene.player.position={short(panel.x-1400),short(panel.y-512),short(panel.z)};scene.previousPosition=scene.player.position;scene.yaw=3072;scene.pitch=-100;preview(scene.root/"decompilation/reports/pod-closed.bmp");}
  require(scene.logic.action(m,scene.doors,action,true),"Original pod action failed");for(unsigned tick=0;tick<32;++tick){scene.logic.tick(m,scene.doors,-32768,0,-32768);require(m.s(object+12)==-int((tick+1)*8),"Pod hinge differs from native -8/tick");}require(m.s(object+12)==-256,"Pod did not reach native open angle");scene.logic.tick(m,scene.doors,-32768,0,-32768);require(m.s(object+12)==-256,"Pod hinge passed native stop");
  if(path.stem()=="L131LEV"&&panel.index==7)preview(scene.root/"decompilation/reports/pod-open.bmp");++count;
 }maps+=hasPods;}require(count>0,"No original pod fixtures found");std::ofstream out(scene.root/"decompilation/reports/pod_checks.json");out<<"{\"status\":\"passed\",\"pods\":"<<count<<",\"maps_with_pods\":"<<maps<<",\"hinge_ticks\":32,\"native_open_angle\":-256}";
}
void allEntranceSelfTest(){
 auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};unsigned maps=0,lifts=0,models=0,actions=0,shotLifts=0;std::ofstream out(scene.root/"decompilation/reports/all_entrance_checks.json");out<<"{\"levels\":[";bool first=true;
 for(auto path:scene.physicalLevels){scene.level=path;scene.load(true);auto m=scene.mem();auto raw=explorerChunks(path).at("MAP0");auto word=[&](unsigned p){return unsigned(raw.at(p))|(unsigned(raw.at(p+1))<<8);};unsigned grid=36+word(0)*8+word(2)*20,la=grid+word(4)*word(6)*16+raw[12]*8+word(14)*20+word(16)*8+word(18)*16+word(20)*8;auto originals=scene.logic.lifts;unsigned localModels=0;
  for(unsigned i=0;i<originals.size();++i){auto lift=originals[i];unsigned r=la+i*16;unsigned floor=raw.at(grid+(raw[r+1]*word(4)+raw[r])*16+11);require(lift.low==int(floor*32)&&lift.high==int(((floor+raw[r+5])&255)*32),path.stem().string()+" duplicated lift height");auto model=scene.liftMeshes.find(lift.model);if(model!=scene.liftMeshes.end()){require(!model->second.faces.empty()&&!model->second.vertices.empty(),"Empty authored lift model");++models;++localModels;}else { /* 0x41bb4/0x41464 explicitly allow a null model: preserve invisible movers. */ }
   scene.logic.lifts={lift};auto& moving=scene.logic.lifts.front();moving.threshold=1;moving.progress=0;moving.hold=moving.remaining=0;moving.targetHigh=!moving.atHigh;moving.moving=true;int target=moving.targetHigh?moving.high:moving.low;for(unsigned tick=0;tick<270&&moving.moving;++tick)scene.logic.tick(m,scene.doors,-32768,0,-32768);require(moving.y==target&&!moving.moving,"Lift did not reach authored endpoint in "+path.stem().string());for(unsigned z=0;z<moving.height;++z)for(unsigned x=0;x<moving.width;++x){auto c=m.d(0xa0c40)+((moving.gz+z)*m.w(0xa0c30)+moving.gx+x)*16;require(m.b(c+11)==unsigned(target/32),"Lift collision floor differs from visible endpoint");}++lifts;
  }
  scene.load(true);m=scene.mem();unsigned aa=m.d(0xa0c64),cc=m.d(0xa0c68),count=m.b(0xa0c2a);unsigned localActions=0;
  for(unsigned id=1;id<count;++id){auto a=aa+id*4;if(!(m.b(a)&2))continue;++actions;++localActions;unsigned pc=m.b(a+1);if(pc==255)continue;auto command=cc+pc*4;unsigned op=m.b(command),delta=m.b(command+2),target=m.b(command+3);if(op!=7||m.b(command+1)!=255)continue;auto cell=0u;for(unsigned n=0;n<scene.cells.size();++n){auto c=m.d(0xa0c40)+n*16;if(m.b(c+15)==id){cell=c;break;}}if(!cell)continue;
   // Exercise the original projectile callback and its per-projectile latch.
   auto p=acquireProjectile(m);require(p!=0,"Projectile trigger fixture pool empty");auto pos=m.d(p),index=(cell-m.d(0xa0c40))/16;m.w(pos,int(index%m.w(0xa0c30))*512+256-m.w(0xa0c34));m.w(pos+4,int(index/m.w(0xa0c30))*512+256-m.w(0xa0c36));auto before=scene.logic.lifts.at(target).progress;unsigned needed=scene.logic.lifts[target].threshold-before;unsigned iterations=delta?(needed+delta-1)/delta:0;
   for(unsigned n=0;n<iterations;++n){m.b(p+31,0);auto old=scene.logic.lifts[target].progress;scene.logic.projectile(m,scene.doors,p);require(scene.logic.lifts[target].progress==old+delta,"Original shooting trigger did not increment lift in "+path.stem().string());scene.logic.projectile(m,scene.doors,p);require(scene.logic.lifts[target].progress==old+delta,"Projectile counted twice within trigger cell");}require(scene.logic.lifts[target].moving,"Authored shooting lift did not start");releaseProjectile(m,p);++shotLifts;
  }
  if(!first)out<<",";first=false;out<<"{\"map\":\""<<path.stem().string()<<"\",\"lifts\":"<<originals.size()<<",\"models\":"<<localModels<<",\"projectile_actions\":"<<localActions<<"}";++maps;
 }out<<"],\"status\":\"passed\",\"maps\":"<<maps<<",\"lifts\":"<<lifts<<",\"models\":"<<models<<",\"projectile_actions\":"<<actions<<",\"shooting_lifts\":"<<shotLifts<<"}";
}
void secretEntranceSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
 for(const char* code:{"122","131"}){
  scene.level=scene.root/"GameData/PS1/CD/SECT11"/(std::string("L")+code+"LEV.MAP");scene.load(true);scene.menu.open=false;scene.mission.briefing=false;scene.interpolate=false;auto m=scene.mem();bool fourth=std::string(code)=="122";auto& lift=scene.logic.lifts[fourth?1:0];
  // The lift's original high offset is applied once, never twice.
  require(lift.low==(fourth?112*32:0)&&lift.high==(fourth?160*32:68*32),"Secret lift original heights incorrect");
  m.d(0x2365e0,0);int gx=fourth?8:17,gz=73;auto cell=m.d(0xa0c40)+(gz*m.w(0xa0c30)+gx)*16;scene.player.position={short(gx*512+256-m.w(0xa0c34)),short(m.b(cell+11)*32),short(gz*512+256-m.w(0xa0c36))};scene.previousPosition=scene.player.position;scene.yaw=1024;scene.pitch=0;scene.enemies->terrainDamage(0,0);m.w(0xa0abe,255);m.w(0xa0ab8,100);
  preview(scene.root/(std::string("decompilation/reports/secret-entrance-")+code+"-closed.bmp"));
  for(unsigned shot=0;shot<25;++shot){require(scene.enemies->fireWeapon(scene.player.position,1024,0,0,0),"Secret entrance pistol did not fire");for(unsigned t=0;t<8;++t){scene.enemies->tickProjectiles();scene.logic.tick(m,scene.doors,scene.player.position[0],scene.player.position[1],scene.player.position[2]);}}
  if(fourth){require(lift.flags&128,"Level 4 did not respond to 25 real pistol shots");for(unsigned t=0;t<60;++t)scene.logic.tick(m,scene.doors,scene.player.position[0],scene.player.position[1],scene.player.position[2]);require(lift.y==lift.low,"Level 4 secret barrier did not lower");}
  else{require(!lift.moving&&lift.y==lift.high&&lift.progress==0,"Level 5 gained an unauthored shooting trigger");m.w(0xa0ad2,1);for(unsigned action:{2u,4u,8u,12u,13u,14u,15u})require(scene.logic.action(m,scene.doors,action,true),"Level 5 original secret prerequisite failed");for(unsigned t=0;t<80;++t)scene.logic.tick(m,scene.doors,scene.player.position[0],scene.player.position[1],scene.player.position[2]);require(lift.y==lift.low,"Level 5 original seven-trigger secret barrier did not lower");}
  preview(scene.root/(std::string("decompilation/reports/secret-entrance-")+code+"-open.bmp"));
 }
 std::ofstream out(scene.root/"decompilation/reports/secret_entrance_checks.json");out<<R"({"status":"passed","level4_real_pistol_shots":25,"level5_original_prerequisites":7,"lift_heights_applied_once":true})";
}
void secretEnemySelfTest(){
 auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};unsigned deaths=0,walls=0,maps=0,animatedFaces=0;std::ofstream audit(scene.root/"decompilation/reports/secret_level_audit.json");audit<<"{\"levels\":[";bool first=true;
 for(auto path:scene.physicalLevels){scene.level=path;scene.load(true);auto m=scene.mem();++maps;unsigned levelWalls=0;unsigned secretAreas=m.w(0xa0c3e);std::vector<unsigned> secretCells;for(unsigned n=0;n<scene.cells.size();++n)if(scene.cells[n].bytes[5])secretCells.push_back(n);auto floors=scene.cells;auto originalTextures=scene.animatedTextures;
  for(auto& face:scene.faces)if(face.flags==8||face.flags==10||face.flags==26||face.flags==28||face.flags==30||face.flags==32){if(face.texture>=scene.animatedTextures.size())throw std::runtime_error("Animated slot invalid: "+path.stem().string()+" flags="+std::to_string(face.flags)+" slot="+std::to_string(face.texture)+" count="+std::to_string(scene.animatedTextures.size()));require(scene.textures.rect(scene.animatedTextures[face.texture]),"Original animated face has no texture");++animatedFaces;}
  for(auto i:scene.objects.entries){auto object=0x320000+i*40;unsigned kind=m.b(object+24);if(kind!=21&&kind!=28&&kind!=29)continue;++walls;++levelWalls;auto p=acquireProjectile(m);require(p!=0,"Secret test projectile pool empty");auto pos=m.d(p);for(unsigned j=0;j<3;++j)m.w(pos+j*2,m.w(object+j*2)+(j==1?256:0));unsigned hitCell=0;for(unsigned n=0;n<scene.cells.size();++n){auto cell=m.d(0xa0c40)+n*16;if(m.b(cell+12)==kind&&m.b(cell+4)==i&&m.b(cell+10)!=20){hitCell=cell;break;}}m.d(p+12,hitCell?hitCell:m.d(object+20));m.b(p+30,0);m.w(p+26,32767);auto health=m.w(object+26);
   if(!hitCell)damageMapObject(m,i,32767,[](auto&,auto,auto,auto,auto){return PlayerControlResult{};});else if(kind==21){collideProjectileWithMap(m,p,[](auto&,auto,auto,auto,auto){return PlayerControlResult{};});require(m.w(object+26)==health,"Pistol damaged seismic-only wall");m.b(p+30,19);collideBallisticWithMap(m,p,[](auto&,auto,auto,auto,auto){return PlayerControlResult{};});}else collideProjectileWithMap(m,p,[](auto&,auto,auto,auto,auto){return PlayerControlResult{};});releaseProjectile(m,p);
   for(unsigned tick=0;tick<12;++tick){scene.objects.tick();scene.textureAnimation.tick(m,scene.animatedTextures);}if(scene.objects.visible(i))throw std::runtime_error("Secret destruction failed: "+path.stem().string()+" object="+std::to_string(i)+" kind="+std::to_string(kind)+" hp="+std::to_string(m.w(object+26)));
   unsigned start=m.w(object+36),count=m.w(object+38);for(unsigned j=0;j<count;++j){auto slot=start+j;require(slot<scene.animatedTextures.size(),"Secret wall animation range invalid");auto group=m.w(m.d(0xa0bc8+0xa8)+slot*4);unsigned expected=originalTextures[slot];for(unsigned n=0;n<m.w(0xa0bc8+0x72);++n){auto command=m.d(0xa0bc8+0xac)+n*4;if(m.w(command)==group&&!(m.w(command+2)&32768))expected=m.w(command+2);}require(scene.animatedTextures[slot]==expected,"Secret wall did not reach authored final texture");}
   auto raw=m.d(0xa0c58)+i*16;for(unsigned z=0;z<m.b(object+33);++z)for(unsigned x=0;x<m.b(object+32);++x){auto cell=m.d(0xa0c40)+((m.b(raw+1)+z)*m.w(0xa0c30)+m.b(raw)+x)*16;if(m.b(cell+12)==kind&&m.b(cell+4)==i)throw std::runtime_error("Destroyed secret wall retains collision: "+path.stem().string()+" object="+std::to_string(i)+" kind="+std::to_string(m.b(cell+12))+" index="+std::to_string(m.b(cell+4)));}
  }
  for(unsigned i=0;i<floors.size();++i){auto cell=m.d(0xa0c40)+i*16;require(m.b(cell+11)==floors[i].bytes[11]&&m.b(cell+13)==floors[i].bytes[13],"Destroying secret wall changed authored floor/ceiling");}
  for(auto e:scene.enemies->active()){if(m.b(e+0x74)!=3)continue;damageEntity(m,e,32767,0,withCombatRuntime([](auto&,auto,auto,auto,auto){return PlayerControlResult{};}));for(unsigned tick=0;tick<140;++tick){scene.enemies->tick(scene.player.position,100);for(auto entity:scene.enemies->active())scene.enemies->sprite(entity);}++deaths;}
  for(auto n:secretCells)scene.mission.visit(m.d(0xa0c40)+n*16);require(scene.mission.statistics().percent[1]==100,"Authored secret areas did not count toward original assessment");
  if(!first)audit<<",";first=false;audit<<"{\"map\":\""<<path.stem().string()<<"\",\"breakable_wall_objects\":"<<levelWalls<<",\"secret_area_ids\":"<<secretAreas<<",\"secret_cells\":"<<secretCells.size()<<"}";
 }require(deaths>0&&walls>0,"Secret/enemy fixtures absent");audit<<"],\"status\":\"passed\",\"maps\":"<<maps<<",\"walls\":"<<walls<<",\"animated_faces\":"<<animatedFaces<<"}";
 std::ofstream out(scene.root/"decompilation/reports/secret_enemy_checks.json");out<<"{\"status\":\"passed\",\"maps\":"<<maps<<",\"secret_wall_objects\":"<<walls<<",\"chestburster_deaths\":"<<deaths<<"}";
}
void gpuSelfTest(unsigned backend){
    scene.fastOpaqueFaces=std::wstring_view(GetCommandLineW()).find(L"--no-fast-opaque")==std::wstring_view::npos;
 if(backend>2)backend=0;WNDCLASSW wc{};wc.lpfnWndProc=[](HWND w,UINT message,WPARAM a,LPARAM b)->LRESULT{if(message==WM_GETMINMAXINFO){auto limits=reinterpret_cast<MINMAXINFO*>(b);limits->ptMaxTrackSize={8192,8192};return 0;}return DefWindowProcW(w,message,a,b);};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TrilogyGpuCheck";RegisterClassW(&wc);RECT r{0,0,1280,800};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);HWND window=CreateWindowW(wc.lpszClassName,L"GPU check",WS_OVERLAPPEDWINDOW,0,0,r.right-r.left,r.bottom-r.top,nullptr,nullptr,wc.hInstance,nullptr);if(!window)throw std::runtime_error("Cannot create hidden GPU check window");
 auto gpu=std::make_unique<ExplorerGpu>();if(!gpu->initialize(window,scene.root,backend,false))throw std::runtime_error("Requested GPU backend unavailable");scene.menu.open=false;scene.mission.briefing=false;
 std::vector<double> times;double beginMs=0,drawMs=0,finishMs=0;LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);unsigned calls=0,triangles=0;std::string tag=backend==1?"dx12":backend==2?"vulkan":"dx11";
 for(unsigned frame=0;frame<150;++frame){LARGE_INTEGER start,end;QueryPerformanceCounter(&start);auto dc=gpu->begin(1280,800);LARGE_INTEGER beginDone,drawDone;QueryPerformanceCounter(&beginDone);scene.draw(dc,1280,800,gpu.get());QueryPerformanceCounter(&drawDone);if(frame==145)gpu->screenshot(scene.root/("decompilation/reports/gpu-"+tag+"-preview.bmp"));gpu->finish(false);calls=gpu->drawCalls();triangles=gpu->triangles();QueryPerformanceCounter(&end);if(frame>=30){times.push_back(double(end.QuadPart-start.QuadPart)*1000/frequency.QuadPart);beginMs+=double(beginDone.QuadPart-start.QuadPart)*1000/frequency.QuadPart;drawMs+=double(drawDone.QuadPart-beginDone.QuadPart)*1000/frequency.QuadPart;finishMs+=double(end.QuadPart-drawDone.QuadPart)*1000/frequency.QuadPart;}}
 if(!calls||triangles<100)throw std::runtime_error("GPU did not submit the real level");gpu->setVsync(true);gpu->setVsync(false);
 auto diagnostic=scene.root/("decompilation/reports/gpu-"+tag+"-depth-alpha.bmp");std::vector<unsigned> cutout(32*32,0xff00ff00);for(unsigned y=8;y<24;++y)for(unsigned x=8;x<24;++x)cutout[y*32+x]=0;
 auto triangle=[&](float x,float y,float z,float red,float green,float blue){gpu->triangle(nullptr,{{{x,y,z,0,0,red,green,blue,1},{x+400,y,z,0,0,red,green,blue,1},{x,y+400,z,0,0,red,green,blue,1}}});};
 for(unsigned frame=0;frame<4;++frame){gpu->begin(1280,800);triangle(100,100,100,1,0,0);triangle(100,100,200,0,0,1);triangle(700,100,200,1,0,1);gpu->sprite(cutout,32,32,700,100,4,80,false);if(frame==2)gpu->screenshot(diagnostic);gpu->finish(false);}
 auto bmp=read(diagnostic);auto pixel=[&](unsigned x,unsigned y){size_t offset=54+(y*1280+x)*4;if(offset+3>=bmp.size())throw std::runtime_error("GPU screenshot missing or truncated");return unsigned(bmp[offset])|(unsigned(bmp[offset+1])<<8)|(unsigned(bmp[offset+2])<<16);};if((pixel(150,150)&0xf8f8f8)!=0xf80000||(pixel(708,108)&0xf8f8f8)!=0x00f800||(pixel(764,164)&0xf8f8f8)!=0xf800f8)throw std::runtime_error("GPU depth or sprite alpha regression");
 // A magnified two-colour texture distinguishes nearest from bilinear sampling.
 std::vector<unsigned> filteringPattern(256*256);for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)filteringPattern[y*256+x]=x<128?0xff000000u:0xffffffffu;
 for(unsigned mode:{0u,1u,0u}){gpu->setTextureFiltering(mode);auto output=scene.root/("decompilation/reports/gpu-"+tag+"-filter-"+std::to_string(mode)+".bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(1280,800);ExplorerGpuVertex a{100,100,100,0,0,1,1,1,1},b{612,100,100,1,0,1,1,1,1},c{612,612,100,1,1,1,1,1,1},d{100,612,100,0,1,1,1,1,1};gpu->triangle(&filteringPattern,{a,b,c},false);gpu->triangle(&filteringPattern,{a,c,d},false);RECT marker{700,100,701,116};auto brush=CreateSolidBrush(RGB(0,255,0));FillRect(dc,&marker,brush);DeleteObject(brush);if(frame==2)gpu->screenshot(output);gpu->finish(false);}bmp=read(output);unsigned partial=0;for(unsigned x=345;x<365;++x){unsigned color=pixel(x,300)&255;if(color>0&&color<255)++partial;}if((mode==0&&partial)||(mode==1&&!partial))throw std::runtime_error("Texture filtering did not change sampling");if(pixel(700,104)!=0x00ff00)throw std::runtime_error("Texture filtering changed HUD pixels");}
 gpu->invalidate();
 // Transparent magenta borders must not leak RGB under bilinear filtering.
 std::vector<unsigned> keyed(32*32,0x00ff00ff);for(unsigned y=8;y<24;++y)for(unsigned x=8;x<24;++x)keyed[y*32+x]=0xff00ff00;
 gpu->setTextureFiltering(1);auto keyedImage=scene.root/("decompilation/reports/gpu-"+tag+"-filtered-cutout.bmp");for(unsigned frame=0;frame<4;++frame){gpu->begin(1280,800);gpu->sprite(keyed,32,32,100,100,12,100,false);if(frame==2)gpu->screenshot(keyedImage);gpu->finish(false);}bmp=read(keyedImage);unsigned visible=0;for(unsigned y=100;y<484;++y)for(unsigned x=100;x<484;++x){unsigned color=pixel(x,y);if((color&0xff0000)||(color&255))throw std::runtime_error("Filtered cutout leaked transparent colour key");if(color&0xff00)++visible;}if(visible<30000)throw std::runtime_error("Filtered cutout lost opaque interior");gpu->setTextureFiltering(0);gpu->invalidate();
 {auto fixture=scene.root/"decompilation/reports/filter_settings_fixture";ExplorerMenu settings;settings.load(scene.root,fixture);settings.page="graphics";settings.selection=10;settings.renderer=0;settings.textureFiltering=0;settings.key(VK_RIGHT);if(settings.textureFiltering!=1)throw std::runtime_error("Filtering menu toggle failed");ExplorerMenu restored;restored.load(scene.root,fixture);if(restored.textureFiltering!=1)throw std::runtime_error("Filtering preference did not persist");settings.renderer=3;settings.key(VK_RIGHT);if(settings.textureFiltering!=1)throw std::runtime_error("Software changed GPU filtering preference");}
 unsigned aaModes=0;for(auto samples:gpu->supportedAntialiasing()){if(gpu->setAntialiasing(samples)!=samples)throw std::runtime_error("Supported MSAA setting not applied");gpu->setVsync(true);gpu->setVsync(false);if(gpu->antialiasing()!=samples)throw std::runtime_error("VSync change cleared MSAA");auto aaImage=scene.root/("decompilation/reports/gpu-"+tag+"-msaa-"+std::to_string(samples)+".bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(1280,800);gpu->triangle(nullptr,{{{100.2f,100.3f,100,0,0,1,1,1,1},{500.7f,100.3f,100,0,0,1,1,1,1},{100.2f,500.8f,100,0,0,1,1,1,1}}},false);for(int x=700;x<716;++x){RECT marker{x,100,x+1,116};auto brush=CreateSolidBrush((x&1)?RGB(0,255,0):RGB(255,0,255));FillRect(dc,&marker,brush);DeleteObject(brush);}if(frame==2)gpu->screenshot(aaImage);gpu->finish(false);}bmp=read(aaImage);unsigned partial=0;for(unsigned y=90;y<520;++y)for(unsigned x=90;x<520;++x){auto color=pixel(x,y)&255;if(color>0&&color<255)++partial;}if((samples==0&&partial)||(samples&&partial<20))throw std::runtime_error("MSAA did not change diagonal edge coverage: samples="+std::to_string(samples)+" partial="+std::to_string(partial));if(pixel(700,104)!=0xff00ff||pixel(701,104)!=0x00ff00)throw std::runtime_error("MSAA blurred HUD pixels");++aaModes;}
 auto fogImage=scene.root/("decompilation/reports/gpu-"+tag+"-fog.bmp");for(unsigned frame=0;frame<4;++frame){gpu->begin(1280,800);triangle(100,100,1000,1,0,0);triangle(500,100,3000,0,1,0);triangle(900,100,6000,0,0,1);if(frame==2)gpu->screenshot(fogImage);gpu->finish(false);}bmp=read(fogImage);unsigned middle=(pixel(550,150)>>8)&255;if(((pixel(150,150)>>16)&255)<250||middle<170||middle>180||pixel(950,150)!=0)throw std::runtime_error("GPU distance fog ramp failed");if(explorerFogFactor(1000)!=255||explorerFogFactor(3000)!=175||explorerFogFactor(6000)!=0)throw std::runtime_error("Software fog ramp differs");
 auto fade=scene.root/("decompilation/reports/gpu-"+tag+"-fade.bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(640,480);RECT full{0,0,640,480};FillRect(dc,&full,(HBRUSH)GetStockObject(WHITE_BRUSH));if(frame==2)gpu->screenshot(fade);gpu->finish(true,0);}bmp=read(fade);if(pixel(640,400)!=0)throw std::runtime_error("GPU whole-frame fade or resized overlay failed");

 RECT resized{0,0,640,480};AdjustWindowRect(&resized,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(window,nullptr,0,0,resized.right-resized.left,resized.bottom-resized.top,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER);auto resizedImage=scene.root/("decompilation/reports/gpu-"+tag+"-resized.bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(640,480);RECT all{0,0,640,480};FillRect(dc,&all,(HBRUSH)GetStockObject(WHITE_BRUSH));if(frame==2)gpu->screenshot(resizedImage);gpu->finish(true);}bmp=read(resizedImage);if(bmp.size()!=54+640*480*4||bmp[54]!=255||bmp[55]!=255||bmp[56]!=255)throw std::runtime_error("GPU swap-chain resize failed");
 scene.menu.load(scene.root);scene.menu.show(false);auto menuImage=scene.root/("decompilation/reports/gpu-"+tag+"-menu.bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(640,480);scene.draw(dc,640,480,gpu.get());if(frame==2)gpu->screenshot(menuImage);gpu->finish(true);}scene.menu.open=false;
 RECT ultra{0,0,3840,2160};AdjustWindowRect(&ultra,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(window,nullptr,0,0,ultra.right-ultra.left,ultra.bottom-ultra.top,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER);auto image4k=scene.root/("decompilation/reports/gpu-"+tag+"-4k.bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(3840,2160);scene.draw(dc,3840,2160,gpu.get());if(frame==2)gpu->screenshot(image4k);gpu->finish(false);}bmp=read(image4k);if(bmp.size()!=54+3840ull*2160*4||scene.lastWeaponBottom!=2159||!scene.lastTrackerPixels)throw std::runtime_error("4K rendering or HUD scaling failed: bytes="+std::to_string(bmp.size())+" bottom="+std::to_string(scene.lastWeaponBottom)+" tracker="+std::to_string(scene.lastTrackerPixels));
 auto compact4k=scene.root/("decompilation/reports/gpu-"+tag+"-4k-compact-ui.bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(3840,2160,854,480);scene.draw(dc,854,480,gpu.get());if(frame==2)gpu->screenshot(compact4k);gpu->finish(false);}auto compactBmp=read(compact4k);if(compactBmp.size()!=54+3840ull*2160*4||scene.lastWeaponBottom!=479||!scene.lastTrackerPixels)throw std::runtime_error("Optimized 4K HUD or native target size failed");
 for(unsigned aspect:{1u,4u}){auto ratio=ExplorerAspects[aspect];auto fitted=explorerFit(3840,2160,ratio.x,ratio.y);auto capture=scene.root/("decompilation/reports/gpu-"+tag+"-aspect-"+std::to_string(aspect)+".bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(fitted.width,fitted.height);RECT all{0,0,fitted.width,fitted.height};FillRect(dc,&all,(HBRUSH)GetStockObject(WHITE_BRUSH));if(frame==2)gpu->screenshot(capture);gpu->finish(true);}auto data=read(capture);auto at=[&](unsigned x,unsigned y){auto n=54+(y*3840+x)*4;return unsigned(data.at(n))|(unsigned(data.at(n+1))<<8)|(unsigned(data.at(n+2))<<16);};if(at(1920,1080)!=0xffffff||(fitted.x?at(1,1080):at(1920,1))!=0)throw std::runtime_error("Aspect ratio bars or viewport placement failed");}
 gpu->invalidate();setupBulletTrailFixture();RECT bulletWindow{0,0,1280,800};AdjustWindowRect(&bulletWindow,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(window,nullptr,0,0,bulletWindow.right-bulletWindow.left,bulletWindow.bottom-bulletWindow.top,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER);auto bulletImage=scene.root/("decompilation/reports/gpu-"+tag+"-bullet-trail.bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(1280,800);scene.draw(dc,1280,800,gpu.get());if(frame==2)gpu->screenshot(bulletImage);gpu->finish(false);}auto tracer=read(bulletImage);double ax=640-128*scene.aimFocal/1024,ay=400+128*scene.aimFocal/1024,bx=640-128*scene.aimFocal/1152,by=400+128*scene.aimFocal/1152;unsigned tx=unsigned(std::lround((ax+bx)*.5)),ty=unsigned(std::lround((ay+by)*.5));auto offset=54+(ty*1280+tx)*4;if(!scene.lastBulletTrails||tracer.at(offset+2)<220||tracer.at(offset+1)<220||tracer.at(offset)>240)throw std::runtime_error("GPU original-colour bullet trail missing");
 auto flame=scene.mem().d(0x236300);scene.mem().b(flame+30,3);scene.mem().b(flame+28,8);auto flameImage=scene.root/("decompilation/reports/gpu-"+tag+"-flame.bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(1280,800);scene.draw(dc,1280,800,gpu.get());if(frame==2)gpu->screenshot(flameImage);gpu->finish(false);}auto flameBmp=read(flameImage);unsigned flamePixels=0;for(unsigned y=440;y<580;++y)for(unsigned x=450;x<600;++x){auto n=54+(y*1280+x)*4;if(flameBmp.at(n+2)>60&&flameBmp.at(n+2)>flameBmp.at(n+1)&&flameBmp.at(n+1)>10)++flamePixels;}if(!scene.lastFlameSprites||flamePixels<100)throw std::runtime_error("GPU original flamethrower fire sprite missing");
 scene.movie.open(scene.root/"GameData/PS1/Movies/INTRO.fmv");scene.movieRunning=true;scene.movie.tick(30,true);auto movieImage=scene.root/("decompilation/reports/gpu-"+tag+"-movie.bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(1280,800);scene.draw(dc,1280,800,gpu.get());if(frame==2)gpu->screenshot(movieImage);gpu->finish(true);}scene.movie.close();scene.movieRunning=false;bmp=read(movieImage);unsigned moviePixels=0;for(unsigned y=140;y<650;y+=4)for(unsigned x=120;x<1150;x+=4)moviePixels+=(pixel(x,y)&0xffffff)!=0;if(moviePixels<1000||pixel(10,400)!=0||pixel(640,10)!=0)throw std::runtime_error("Movie frame or original aspect-ratio bars missing");
 scene.performance.reset();for(unsigned frame=0;frame<=120;++frame)scene.performance.frame(double(frame)/60);scene.menu.performanceDisplay=true;auto performanceImage=scene.root/("decompilation/reports/gpu-"+tag+"-performance.bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(1280,800);scene.draw(dc,1280,800,gpu.get());scene.drawPerformance(dc,1280,800);if(frame==2)gpu->screenshot(performanceImage);gpu->finish(false);}scene.menu.performanceDisplay=false;bmp=read(performanceImage);if(pixel(6,6)!=0x030704)throw std::runtime_error("GPU performance panel missing");unsigned letters=0;for(unsigned y=8;y<35;++y)for(unsigned x=8;x<210;++x)letters+=(pixel(x,y)&255)>150;if(letters<50)throw std::runtime_error("GPU performance text missing");
 // Compare the software screenshot palette with actual GPU output for every filter.
 gpu->setAntialiasing(0);std::array<unsigned,8> filterSamples{0xff000000,0xff101010,0xff303030,0xff606060,0xff909090,0xffc8c8c8,0xffffffff,0xff804020};
 for(unsigned filter=0;filter<ExplorerPhotoFilterNames.size();++filter){gpu->setPhotoFilter(filter);auto image=scene.root/("decompilation/reports/gpu-"+tag+"-photo-filter-"+std::to_string(filter)+".bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(1280,800);for(unsigned i=0;i<filterSamples.size();++i){auto color=filterSamples[i];float x=30+i*150,r=float(color>>16&255)/255,g=float(color>>8&255)/255,b=float(color&255)/255;ExplorerGpuVertex a{x,100,100,0,0,r,g,b,1},bb{x+100,100,100,0,0,r,g,b,1},c{x+100,300,100,0,0,r,g,b,1},d{x,300,100,0,0,r,g,b,1};gpu->triangle(nullptr,{a,bb,c},false);gpu->triangle(nullptr,{a,c,d},false);}RECT marker{50,400,70,420};auto brush=CreateSolidBrush(RGB(255,0,255));FillRect(dc,&marker,brush);DeleteObject(brush);if(frame==2)gpu->screenshot(image);gpu->finish(false);}bmp=read(image);for(unsigned i=0;i<filterSamples.size();++i){unsigned actual=pixel(80+i*150,200),expected=applyPhotoFilter(filterSamples[i],filter);for(unsigned shift:{0u,8u,16u})if(std::abs(int(actual>>shift&255)-int(expected>>shift&255))>2)throw std::runtime_error("Photo filter GPU/software palette mismatch");}if(pixel(60,410)!=0xff00ff)throw std::runtime_error("Photo filter tinted UI overlay");}
 gpu->setPhotoFilter(0);
 // Depth-of-field keeps the centred subject sharp and blurs other distances.
 std::vector<unsigned> dofPattern(256*256);for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)dofPattern[y*256+x]=((x+y)&1)?0xffffffff:0xff000000;
 auto rectangle=[&](float x,float y,float w,float h,float z){ExplorerGpuVertex a{x,y,z,0,0,1,1,1,1},b{x+w,y,z,1,0,1,1,1,1},c{x+w,y+h,z,1,1,1,1,1,1},d{x,y+h,z,0,1,1,1,1,1};gpu->triangle(&dofPattern,{a,b,c},false);gpu->triangle(&dofPattern,{a,c,d},false);};
 unsigned dofModes=0;for(auto samples:gpu->supportedAntialiasing()){gpu->setAntialiasing(samples);std::vector<unsigned> reference;for(unsigned strength:{0u,50u,100u}){gpu->setDepthOfField(strength/100.f);auto file=scene.root/("decompilation/reports/gpu-"+tag+"-dof-"+std::to_string(samples)+"-"+std::to_string(strength)+".bmp");for(unsigned frame=0;frame<4;++frame){auto dc=gpu->begin(1280,800);rectangle(0,0,1280,800,4000);rectangle(540,300,200,200,1000);rectangle(100,300,200,200,300);RECT marker{20,20,40,40};auto brush=CreateSolidBrush(RGB(255,0,255));FillRect(dc,&marker,brush);DeleteObject(brush);if(frame==2)gpu->screenshot(file);gpu->finish(false);}bmp=read(file);if(pixel(30,30)!=0xff00ff)throw std::runtime_error("Depth of field blurred HUD");std::vector<unsigned> subject;for(unsigned y=330;y<470;++y)for(unsigned x=570;x<710;++x)subject.push_back(pixel(x,y));if(!strength)reference=subject;else{for(unsigned i=0;i<subject.size();++i)if(std::abs(int(subject[i]&255)-int(reference[i]&255))>2)throw std::runtime_error("Autofocus subject was blurred");unsigned softened=0;for(unsigned y=150;y<650;++y)for(unsigned x=850;x<1150;++x){unsigned value=pixel(x,y)&255;softened+=value>20&&value<235;}if(softened<30000)throw std::runtime_error("Depth of field did not blur background");}}++dofModes;}
 gpu->setDepthOfField(0);gpu->setAntialiasing(0);gpu->invalidate();
 std::sort(times.begin(),times.end());double average=0;for(auto t:times)average+=t;average/=times.size();std::ofstream report(scene.root/("decompilation/reports/gpu_"+tag+"_checks.json"));report<<"{\"status\":\"passed\",\"renderer\":\""<<gpu->name()<<"\",\"average_ms\":"<<average<<",\"begin_ms\":"<<beginMs/times.size()<<",\"draw_ms\":"<<drawMs/times.size()<<",\"finish_ms\":"<<finishMs/times.size()<<",\"p95_ms\":"<<times[times.size()*95/100]<<",\"uncapped_fps\":"<<1000/average<<",\"draw_calls\":"<<calls<<",\"stages_ms\":["<<scene.renderStages[0]<<","<<scene.renderStages[1]<<","<<scene.renderStages[2]<<","<<scene.renderStages[3]<<","<<scene.renderStages[4]<<"],\"triangles\":"<<triangles<<",\"resolution_4k\":true,\"msaa_modes_checked\":"<<aaModes<<",\"dof_msaa_modes_checked\":"<<dofModes<<",\"depth_alpha_resize_fade_checks\":4,\"scope\":\"Hidden-window GPU rendering, 120 measured frames after 30 warmup; no simulation, no vsync\"}";gpu.reset();DestroyWindow(window);
}
void environmentSelfTest(){
 auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};unsigned maps=0,vents=0,liveVents=0;std::array<unsigned,4> floors{};
 auto paths=scene.physicalLevels;for(const auto& path:paths){scene.level=path;scene.load(true);auto m=scene.mem();++maps;unsigned expected=0;for(unsigned i=0;i<m.w(0xa0c1a);++i){auto r=m.d(0xa0c48)+i*20;unsigned k=m.b(r);if(k>=16&&k<=19){++vents;expected+=m.b(r+7)==m.b(r+8);}}unsigned found=0;for(auto e:scene.enemies->active())found+=m.b(e+0x74)>=16&&m.b(e+0x74)<=19;require(found==expected,"Level hazard emitters did not spawn from original trigger data");liveVents+=found;for(auto& c:scene.cells){unsigned t=c.bytes[10];if(t==7)++floors[0];else if(t==8)++floors[1];else if(t==10)++floors[2];else if(t==12)++floors[3];}}
 require(maps==36&&vents==77,"Environment audit map/emitter totals changed");require(scene.audio->validateEnvironmentMixer()>0,"Environmental audio signal failed");
 auto fixture=[&](unsigned kind=16,unsigned hp=0){scene.level=scene.root/"GameData/PS1/CD/SECT11/L112LEV.MAP";scene.load(true);auto m=scene.mem();scene.menu.open=false;scene.mission.briefing=false;scene.mission.remaining=0;scene.doors.entries.clear();scene.logic.panels.clear();scene.logic.lifts.clear();scene.objects.entries.clear();scene.pickups.clear();for(unsigned i=0;i<scene.cells.size();++i){auto c=m.d(0xa0c40)+i*16;for(unsigned j=0;j<16;++j)m.b(c+j,0);m.b(c+13,128);m.w(c+8,65535);}m.w(0xa0c1a,1);auto r=m.d(0xa0c48);for(unsigned j=0;j<20;++j)m.b(r+j,0);m.b(r,kind);m.b(r+1,10);m.b(r+2,10);m.b(r+5,hp);m.b(r+6,16);m.b(r+7,1);m.b(r+8,1);m.w(r+10,1024);m.w(r+14,20);m.w(r+16,80);m.w(r+18,20);scene.enemies=std::make_unique<ExplorerEnemies>(m,scene.root);scene.enemies->initialize();auto e=scene.enemies->active().front();scene.player.health=100;scene.player.position={short(m.s(e)),0,short(m.s(e+4)-(kind<=17?600:0))};scene.previousPosition=scene.player.position;scene.player.currentCell=scene.grid.index(scene.player.position[0]+scene.player.originX,scene.player.position[2]+scene.player.originZ);scene.acceleration.speed=0;scene.yaw=0;return e;};
 unsigned hazardProfiles=0;for(unsigned kind:{16u,17u,18u,19u}){auto e=fixture(kind);auto m=scene.mem();unsigned damage=kind==17?10:5;for(unsigned i=0;i<100&&scene.player.health==100;++i)scene.tick(0,0);require(scene.player.health==100-damage&&scene.enemies->hazardHits,"Vent did not damage player through the live Scene tick");auto hp=scene.player.health;for(unsigned i=0;i<10;++i)scene.tick(0,0);require(scene.player.health==hp,"Vent ignored original damage cooldown");m.w(0xa0aba,100);m.w(0xa0b30,0);for(unsigned i=0;i<100&&m.w(0xa0aba)==100;++i)scene.tick(0,0);require(m.w(0xa0aba)==100-damage&&scene.player.health==hp,"Vent bypassed original armor behavior");m.w(0xa0ae4,100);m.w(0xa0aba,0);m.w(0xa0b30,0);for(unsigned i=0;i<50;++i)scene.tick(0,0);require(scene.player.health==hp,"Vent bypassed immunity");auto emissions=scene.enemies->ventEmissions;scene.enemies->trigger(255,0);for(unsigned i=0;i<40;++i)scene.tick(0,0);require(scene.enemies->ventEmissions==emissions&&scene.enemies->ventsDisabled==1&&!m.d(0x236300),"Closing valve did not stop emissions and drain remaining puffs");++hazardProfiles;}
 // Emission cycles retain the source off-window and require breaking intact pipes.
 auto e=fixture(16,2);auto m=scene.mem();auto safe=scene.player.position;safe[0]+=1600;for(unsigned i=0;i<100;++i){scene.enemies->tick(safe,100);scene.enemies->tickProjectiles();}require(!scene.enemies->ventEmissions,"Intact pipe emitted before damage");damageEntity(m,e,m.w(e+0x58),0,withCombatRuntime([](auto&,auto,auto,auto,auto){return PlayerControlResult{};}));for(unsigned i=0;i<100;++i){scene.enemies->tick(safe,100);scene.enemies->tickProjectiles();}require(scene.enemies->ventEmissions&&m.b(e+0x6f)==8,"Breaking pipe did not activate its original hazard cycle");
 // A wall between a horizontal jet and the player must stop all damage.
 e=fixture(16);m=scene.mem();int cx=(m.s(e)+m.w(0xa0c34))>>9,cz=(m.s(e+4)+m.w(0xa0c36))>>9;auto wall=m.d(0xa0c40)+((cz-1)*m.w(0xa0c30)+cx)*16;m.b(wall+10,20);m.b(wall+7,128);m.b(wall+13,0);for(unsigned i=0;i<120;++i){scene.enemies->tick(scene.player.position,100);scene.enemies->tickProjectiles();require(scene.enemies->health()==100,"Steam damaged player through a solid wall");}
 // Floor hazards: 5/10 damage, boots/immunity, and armor-independent lethal zones.
 for(unsigned terrain:{7u,8u,10u,12u}){e=fixture();m=scene.mem();m.d(0x22ceb8,0);m.d(0x2365e0,0);auto c=m.d(0xa0c40)+scene.player.currentCell*16;m.b(c+10,terrain);m.w(0xa0aba,0);m.w(0xa0b30,0);scene.tick(0,0);require(scene.player.health==(terrain==7?95u:terrain==8?90u:terrain==12?99u:0u),"Native damaging-floor callback not applied");if(terrain!=10){m.w(0xa0abc,100);m.w(0xa0b30,0);auto hp=scene.player.health;scene.tick(0,0);require(scene.player.health==hp,"Surface protection did not block floor damage");}else{scene.menu.open=false;scene.player.health=100;m.w(0xa0ab8,100);m.w(0xa0aba,100);m.w(0xa0ae4,100);m.w(0xa0abc,100);scene.tick(0,0);require(!scene.player.health&&m.w(0xa0aba)==100,"Lethal floor failed to bypass armor/protection as original health-only callback");}}
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L112LEV.MAP";scene.load();m=scene.mem();auto list=scene.enemies->active();e=*std::find_if(list.begin(),list.end(),[&](unsigned p){return m.b(p+0x74)==16;});scene.player.position={short(m.s(e)-800),0,short(m.s(e+4)+600)};auto wx=unsigned(int(scene.player.position[0])+scene.player.originX),wz=unsigned(int(scene.player.position[2])+scene.player.originZ);scene.player.position[1]=short(floorHeight(scene.grid,wx,wz));scene.previousPosition=scene.player.position;scene.player.currentCell=scene.grid.index(wx,wz);scene.yaw=int(std::atan2(-400.,-600.)*4096/(2*Pi));scene.pitch=-200;m.w(0xa0ae4,100);for(unsigned i=0;i<100;++i){scene.enemies->tick(scene.player.position,100);scene.enemies->tickProjectiles();}preview(scene.root/"decompilation/reports/level2-steam-preview.bmp");
 std::ofstream report(scene.root/"decompilation/reports/environment_checks.json");report<<"{\"status\":\"passed\",\"maps\":"<<maps<<",\"authored_vents\":"<<vents<<",\"initially_spawned_vents\":"<<liveVents<<",\"vent_profiles\":"<<hazardProfiles<<",\"floor_profiles\":4,\"cooldown_armor_immunity\":true,\"valve_shutdown\":true,\"pipe_break_activation\":true,\"wall_occlusion\":true,\"live_damage_preserved\":true}";
}
void controlsWorldSelfTest(){
 auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};auto fixture=scene.root/"decompilation/reports"/("controls_world_"+std::to_string(GetCurrentProcessId()));std::error_code cleanup;fs::remove_all(fixture,cleanup);ExplorerMenu options;options.load(scene.root,fixture);options.page="graphics";options.selection=4;options.key(VK_RIGHT);require(options.fov==75,"FOV slider did not adjust");for(unsigned i=0;i<30;++i)options.key(VK_RIGHT);require(options.fov==110,"FOV upper bound failed");for(unsigned i=0;i<30;++i)options.key(VK_LEFT);require(options.fov==50,"FOV lower bound failed");options.click(640/3,480/5+4*20+10,640,480);require(options.fov==50,"FOV mouse slider minimum failed");options.click(640*2/3,480/5+4*20+10,640,480);require(options.fov==110,"FOV mouse slider maximum failed");options.selection=1;options.resolution=0;options.key(VK_LEFT);require(options.resolution==ExplorerResolutions.size()-1&&ExplorerResolutions.back().width==3840&&ExplorerResolutions.back().height==2160,"4K resolution or wrap failed");options.key(VK_RIGHT);require(options.resolution==0,"Resolution forward wrap failed");options.resolution=unsigned(ExplorerResolutions.size()-1);options.persist();options.selection=5;options.key(VK_RIGHT);require(options.aspect==1,"Aspect ratio option did not change");for(unsigned i=0;i<6;++i){auto ratio=ExplorerAspects[i];auto fitted=explorerFit(3840,2160,ratio.x,ratio.y);require(fitted.width>0&&fitted.height>0&&fitted.x>=0&&fitted.y>=0&&fitted.x*2+fitted.width<=3840&&fitted.y*2+fitted.height<=2160,"Aspect viewport escaped display");if(ratio.x)require(std::abs(double(fitted.width)/fitted.height-double(ratio.x)/ratio.y)<.003,"Aspect ratio distorted");}options.fov=90;options.page="controls";options.selection=9;options.key(VK_RETURN);options.key(VK_CONTROL);options.selection=10;options.key(VK_RETURN);options.key('G');options.persist();ExplorerMenu loaded;loaded.load(scene.root,fixture);require(loaded.aspect==1&&loaded.resolution==ExplorerResolutions.size()-1&&loaded.fov==90&&loaded.bindings[9]==VK_CONTROL&&loaded.bindings[10]=='G',"FOV/sprint/grenade controls did not persist");
 unsigned panelChecks=0;for(bool alien:{false,true})for(unsigned kind:{22u,24u,26u,27u,32u})for(unsigned rotation=0;rotation<8;++rotation){scene.alienPanels=alien;unsigned model=kind==32?40:kind==22?13:kind==24?5:kind==26?17:9;if(kind!=32&&alien)model+=16;auto& mesh=scene.panelMeshes.at(model);double low[3]={1e9,1e9,1e9},high[3]={-1e9,-1e9,-1e9};for(auto v:mesh.vertices){double modelScale=kind==32?1:.875;double xyz[]={v.x*modelScale,v.y*modelScale,v.z*modelScale};for(unsigned c=0;c<3;++c){low[c]=std::min(low[c],xyz[c]);high[c]=std::max(high[c],xyz[c]);}}scene.logic.panels={{0,kind,rotation,0,0,0,0}};double angle=Pi-rotation*Pi/4;auto world=[&](double x,double z,double y){return Vec3s{short(std::lround(x*std::cos(angle)+z*std::sin(angle))),short(y),short(std::lround(-x*std::sin(angle)+z*std::cos(angle)))};};double middle=(low[0]+high[0])/2;auto a=world(middle,low[2]-512,low[1]),b=world(middle,high[2]+512,low[1]);require(scene.panelBlocks(a,b),"Panel permitted crossing its original model footprint");require(!scene.panelBlocks(world(high[0]+256,low[2]-512,low[1]),world(high[0]+256,high[2]+512,low[1])),"Panel blocked passing beside it");a[1]=b[1]=short(high[1]+1);require(!scene.panelBlocks(a,b),"Panel blocked movement on another floor");++panelChecks;}
 scene.load();auto m=scene.mem();scene.menu.open=false;scene.mission.briefing=false;scene.mission.remaining=0;scene.doors.entries.clear();scene.logic.panels.clear();scene.logic.lifts.clear();scene.objects.entries.clear();scene.pickups.clear();m.d(0x22ceb8,0);m.d(0x2365e0,0);for(unsigned i=0;i<scene.cells.size();++i){auto p=m.d(0xa0c40)+i*16;for(unsigned j=0;j<16;++j)m.b(p+j,0);m.b(p+13,128);}Vec3s origin{short((m.w(0xa0c30)/2)*512+256-m.w(0xa0c34)),0,short((m.w(0xa0c32)/2)*512+256-m.w(0xa0c36))};auto reset=[&](){scene.player.position=origin;scene.previousPosition=origin;scene.player.currentCell=scene.grid.index(origin[0]+scene.player.originX,origin[2]+scene.player.originZ);scene.player.health=100;scene.acceleration.speed=0;scene.yaw=scene.pitch=0;for(unsigned i=0;i<scene.cells.size();++i)m.b(m.d(0xa0c40)+i*16+12,0);};
 reset();for(unsigned i=0;i<30;++i)scene.tick(1,0,false);int walking=scene.player.position[2]-origin[2];require(scene.acceleration.speed==120,"Walk speed changed");reset();for(unsigned i=0;i<30;++i)scene.tick(1,0,true);int sprinting=scene.player.position[2]-origin[2];require(sprinting>walking+600&&scene.acceleration.speed==144,"Sprint did not increase actual movement");scene.tick(1,0,false);require(scene.acceleration.speed==120,"Releasing sprint did not restore walking");
 reset();scene.logic.panels={{0,24,0,0,origin[0],0,origin[2]+1500}};for(unsigned i=0;i<40;++i)scene.tick(1,0,true);require(scene.player.position[2]-origin[2]<walking&&scene.player.position[2]<origin[2]+1500,"Sprinting crossed a solid power panel");scene.logic.panels.clear();reset();
 scene.menu.fov=50;preview(scene.root/"decompilation/reports/fov-50-preview.bmp");double narrow=scene.aimFocal;int bottom=scene.lastWeaponBottom;scene.menu.fov=110;preview(scene.root/"decompilation/reports/fov-110-preview.bmp");require(scene.aimFocal<narrow&&scene.lastWeaponBottom==bottom&&bottom==699,"FOV changed weapon/HUD anchoring");scene.menu.fov=70;
 m.w(0xa0ad0,2);m.w(0xa0ac8,3);scene.grenadeRequested=true;scene.tick(0,0);auto p=m.d(0x236300);require(p&&m.b(p+30)==19&&m.b(p+28)==64&&m.w(p+24)==8&&m.w(0xa0ad0)==1&&m.w(0xa0ac8)==3,"Regular secondary did not use original seismic charge constructor/inventory");scene.tick(0,0);require(m.w(0xa0ad0)==1,"Secondary auto-repeated without a new press");resetProjectilePool(m);awardPickup(m,2,20,0);require(scene.hud.select(2),"Pulse rifle did not select for grenade check");scene.grenadeRequested=true;scene.tick(0,0);p=m.d(0x236300);require(p&&m.b(p+30)==12&&m.b(p+28)==48&&m.w(p+24)==16&&m.w(0xa0ac8)==2&&m.w(0xa0ad0)==1,"Pulse secondary did not use dedicated grenade inventory");scene.pitch=-150;scene.tick(0,0);preview(scene.root/"decompilation/reports/pulse-grenade-preview.bmp");scene.pitch=0;auto effects=scene.enemies->effects[0x2a3e0];for(unsigned i=0;i<70;++i){scene.enemies->tickProjectiles();scene.hud.tick();scene.hud.weapon();}require(!m.d(0x236300)&&scene.enemies->effects[0x2a3e0]>effects,"Pulse grenade did not detonate through original simulation");
 m.w(0xa0ab8,100);m.w(0xa0ac8,0);m.w(0xa0ad0,1);require(!scene.enemies->throwGrenade(origin,0,0,true)&&m.w(0xa0ad0)==1,"Empty pulse grenades consumed a regular charge");require(scene.enemies->throwGrenade(origin,0,0,false),"Regular grenade did not throw");effects=scene.enemies->effects[0x2a3e0];for(unsigned i=0;i<80;++i)scene.enemies->tickProjectiles();require(!m.d(0x236300)&&scene.enemies->effects[0x2a3e0]>effects,"Regular charge did not detonate");require(!scene.enemies->throwGrenade(origin,0,0,false),"Empty regular charges fired");
 scene.menu.load(scene.root,fixture);scene.menu.open=true;scene.menu.page="graphics";scene.menu.selection=4;scene.menu.fov=90;preview(scene.root/"decompilation/reports/fov-options-preview.bmp");scene.menu.open=false;
 std::ofstream report(scene.root/"decompilation/reports/controls_world_checks.json");report<<"{\"status\":\"passed\",\"panel_orientation_catalogue_checks\":"<<panelChecks<<",\"walk_distance\":"<<walking<<",\"sprint_distance\":"<<sprinting<<",\"fov_hud_invariance\":true,\"secondary_inventory_constructor_fuse_checks\":true}";
}
void ps1SelfTest(){
 auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
 unsigned maps=0,matched=0,faces=0;
 for(auto& file:fs::recursive_directory_iterator(scene.root/"GameData/PS1/CD"))if(file.path().extension()==".MAP"){
  auto pc=scene.root/"GameData/PS1/CD"/file.path().parent_path().filename()/file.path().filename();
  ExplorerPs1Lighting light;light.load(pc,scene.root);require(light.active,"PS1 map geometry could not be matched");++maps;matched+=light.matched;faces+=light.total;
 }
 require(maps==36,"PS1 level count mismatch");
 unsigned tracks=0;std::ifstream mapping(scene.root/"GameData/PS1/level_music.txt");std::string code;unsigned track;
 while(mapping>>code>>track){require(explorerPs1Track(scene.root,code)==int(track),"PS1 music lookup mismatch");char name[32];std::snprintf(name,sizeof(name),"track%02u.wav",track);require(fs::file_size(scene.root/"GameData/PS1/Music"/name)>44,"Missing PS1 level music");++tracks;}
 require(tracks==35&&scene.audio->musicTrack()==10,"First mission did not select original PS1 music");
 unsigned sfxBanks=0;std::ifstream sfxMapping(scene.root/"GameData/PS1/level_sfx.txt");std::string sfxRel;
 while(sfxMapping>>code>>sfxRel){require(explorerPs1SfxBank(scene.root,code)==sfxRel,"PS1 SFX lookup mismatch");require(fs::file_size(scene.root/"GameData/PS1/CD"/sfxRel)>8+70*56,"Missing PS1 level SFX bank");++sfxBanks;}
 require(sfxBanks==35&&scene.audio->ps1PermanentSamples()==41&&scene.audio->ps1LevelSamples()>0,"First mission did not load original PS1 SFX banks");
 for(auto name:{"COLONY","PRISHOLD","BONESHIP","BACKDROP"}){unsigned w=0,h=0;std::vector<unsigned> pixels;require(explorerPs1Image(scene.root,name,w,h,pixels)&&w==320&&h==240&&pixels.size()==76800,"PS1 presentation image failed");}
 require(scene.ps1Lighting.active,"Live scene did not activate PS1 lighting");
 scene.menu.show(false);scene.syncMusic();require(scene.menuMusic&&scene.musicPlaying&&scene.audio->musicTrack()==2,"Main menu did not play PS1 title track");scene.menu.page="options";scene.syncMusic();require(scene.audio->musicTrack()==2&&scene.musicPlaying,"Menu options interrupted theme");scene.menu.open=false;scene.menu.hasGame=true;scene.mission.briefing=true;scene.syncMusic();require(!scene.menuMusic&&!scene.musicPlaying&&scene.audio->musicTrack()==10,"Briefing did not stop title music and prepare mission track");scene.mission.briefing=false;scene.syncMusic();require(scene.musicPlaying&&scene.audio->musicTrack()==10,"Gameplay did not start mission music after briefing");scene.menu.hasGame=false;

 scene.mission.briefing=true;scene.briefingRevealed=true;preview(scene.root/"decompilation/reports/ps1-briefing-preview.bmp");scene.briefingRevealed=false;scene.briefingStarted=GetTickCount64()-400;preview(scene.root/"decompilation/reports/ps1-briefing-cursor-preview.bmp");scene.briefingRevealed=true;scene.mission.briefing=false;preview(scene.root/"decompilation/reports/ps1-level-preview.bmp");
 std::ofstream report(scene.root/"decompilation/reports/ps1_playable_checks.json");report<<"{\"status\":\"passed\",\"maps\":"<<maps<<",\"matched_faces\":"<<matched<<",\"total_faces\":"<<faces<<",\"level_music_assignments\":"<<tracks<<",\"level_sfx_assignments\":"<<sfxBanks<<",\"ps1_permanent_sfx\":41}";
}
void endLevelSelfTest(){
 auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();auto m=scene.mem();
 for(unsigned k=1;k<15;++k){m.w(0xa0bc8+k*2,0);m.w(0xa0ae6+k*2,0);}m.w(0xa0bc8+2*2,4);m.w(0xa0ae6+2*2,2);m.w(0xa0c3e,4);for(unsigned i=0;i<256;++i)m.b(0x234780+i,0);auto cell=m.d(0xa0c40)+scene.player.currentCell*16;m.b(cell+5,2);scene.mission.visit(cell);scene.mission.visit(cell);scene.mission.kinds={20};m.w(0xa0bc8+20*2,4);m.w(0xa0ae6+20*2,1);auto stats=scene.mission.statistics();require(stats.percent==std::array<unsigned,3>{50,25,25},"Original percentage formula or unique secret visits incorrect");m.w(0xa0ae6+2*2,20);require(scene.mission.statistics().percent[0]==100,"Alien percentage did not cap at 100");m.w(0xa0bc8+2*2,0);m.w(0xa0ae6+2*2,0);m.w(0xa0c3e,0);scene.mission.kinds.clear();require(scene.mission.statistics().percent==std::array<unsigned,3>{0,100,100},"Empty-level original assessment rules incorrect");scene.mission.kinds={35};m.w(0xa0c0e,4);m.w(0xa0ae2,2);require(scene.mission.statistics().percent[2]==50,"Assessment did not read the actual ID-tag counter");
 unsigned maps=0,exitMaps=0,exitCells=0;for(auto& path:scene.physicalLevels){scene.level=path;scene.load(true);m=scene.mem();++maps;bool found=false;for(unsigned i=0;i<unsigned(m.w(0xa0c30))*m.w(0xa0c32);++i){auto id=m.b(m.d(0xa0c40)+i*16+15);if(!id||id>=m.b(0xa0c2a))continue;auto pc=m.b(m.d(0xa0c64)+id*4+1);bool seen[256]{};while(pc!=255&&pc<m.b(0xa0c2b)&&!seen[pc]){seen[pc]=true;auto p=m.d(0xa0c68)+pc*4;if(m.b(p)==8){found=true;++exitCells;break;}pc=m.b(p+1);}}exitMaps+=found;}
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();m=scene.mem();require(scene.beginExit(false)&&scene.exitFading,"Low-score mission could not reach original assessment");scene.exitFading=false;
 unsigned exitCell=0;for(unsigned i=0;i<scene.cells.size();++i){auto id=m.b(m.d(0xa0c40)+i*16+15);if(!id||id>=m.b(0xa0c2a))continue;auto pc=m.b(m.d(0xa0c64)+id*4+1);if(pc<m.b(0xa0c2b)&&m.b(m.d(0xa0c68)+pc*4)==8){exitCell=i;break;}}require(exitCell!=0,"First-level actual exit-elevator trigger missing");for(auto k:scene.mission.kinds)m.w(0xa0ae6+k*2,m.w(0xa0bc8+k*2));auto c=m.d(0xa0c40)+exitCell*16;int x=exitCell%m.w(0xa0c30),z=exitCell/m.w(0xa0c30);scene.player.position={short(x*512+256-m.w(0xa0c34)),short(m.b(c+11)*32),short(z*512+256-m.w(0xa0c36))};scene.player.currentCell=exitCell;scene.menu.load(scene.root,scene.root/"decompilation/reports/endlevel_menu_fixture");scene.menu.open=false;scene.logic.tick(m,scene.doors,scene.player.position[0],scene.player.position[1],scene.player.position[2]);require(scene.exitFading&&!scene.mission.complete&&!scene.menu.open,"Entering original exit cell skipped fade or opened save early");auto pos=scene.player.position;auto inventory=scene.inventory();unsigned brightness=255;for(unsigned tick=1;tick<=25;++tick){scene.tick(1,1);require(scene.player.position==pos&&scene.inventory()==inventory,"Exit transition changed player position or loot");require(scene.exitBrightness()<=brightness,"Exit fade brightened");brightness=scene.exitBrightness();if(tick<25)require(!scene.mission.complete&&!scene.menu.open,"Exit transition ended before native fade steps");}require(scene.mission.complete&&!scene.exitFading&&scene.menu.page=="results","Fade did not lead to original results backdrop");require(scene.menu.resultPercent==scene.exitStatistics.percent,"Results did not use frozen exit statistics");scene.menu.key(VK_RETURN);require(scene.menu.page=="results"&&scene.menu.resultsReady(),"First results key did not reveal count-up");preview(scene.root/"decompilation/reports/mission-assessment-preview.bmp");scene.menu.key(VK_RETURN);require(scene.menu.page=="assessment"&&scene.menu.labels()==std::vector<std::wstring>{L"Continue game",L"Save game"},"Results did not lead to original end-level choices");preview(scene.root/"decompilation/reports/ps1-postlevel-preview.bmp");scene.menu.key(VK_DOWN);scene.menu.key(VK_RETURN);require(scene.menu.page=="save","Post-results Save Game option missing");scene.menu.key(VK_ESCAPE);scene.menu.key(VK_ESCAPE);require(scene.menu.open&&scene.menu.page=="assessment","Completed-level prompt escaped into live gameplay");
 std::ofstream report(scene.root/"decompilation/reports/end_level_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"physical_maps_audited\": "<<maps<<",\n  \"maps_with_exit_cells\": "<<exitMaps<<",\n  \"exit_cells\": "<<exitCells<<",\n  \"original_percentage_rules\": true,\n  \"unique_secret_visits\": true,\n  \"original_exit_cell_trigger\": true,\n  \"fade_steps\": 25,\n  \"loot_frozen_during_fade\": true,\n  \"original_results_backdrop\": true,\n  \"animated_percentages\": true,\n  \"save_after_results_only\": true\n}\n";scene.menu=ExplorerMenu{};scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();
}
void promptSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};auto fixture=scene.root/"decompilation/reports/prompt_settings_fixture";scene.menu.load(scene.root,fixture);scene.menu.show(false);scene.menu.usingController=true;for(bool ps:{false,true}){scene.menu.playstationPrompts=ps;scene.menu.page="main";preview(scene.root/(ps?"decompilation/reports/playstation-prompts-preview.bmp":"decompilation/reports/xbox-prompts-preview.bmp"));scene.menu.page="controller-bindings";require(scene.menu.labels()[0].find(ps?L"R2":L"RT")!=std::wstring::npos,"Remapped controller labels use wrong family");preview(scene.root/(ps?"decompilation/reports/playstation-bindings-preview.bmp":"decompilation/reports/xbox-bindings-preview.bmp"));scene.menu.open=false;scene.mission.briefing=true;scene.briefingRevealed=true;preview(scene.root/(ps?"decompilation/reports/playstation-briefing-preview.bmp":"decompilation/reports/xbox-briefing-preview.bmp"));scene.menu.open=true;}
 scene.menu.page="main";scene.menu.usingController=true;controllerInputDispatch=true;windowProc(nullptr,WM_KEYDOWN,VK_F9,0);controllerInputDispatch=false;require(scene.menu.usingController,"Controller-generated key changed input hints");windowProc(nullptr,WM_KEYDOWN,VK_F9,0);require(!scene.menu.usingController,"Keyboard did not restore keyboard hints");scene.menu.usingController=true;windowProc(nullptr,WM_MOUSEMOVE,0,12345);require(!scene.menu.usingController,"Mouse did not restore keyboard hints");std::ofstream(scene.root/"decompilation/reports/controller_prompt_checks.json")<<"{\"status\":\"passed\",\"xbox_playstation_previews\":true,\"remapped_labels\":true,\"keyboard_mouse_switching\":true}";
}
void performanceSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};for(unsigned rate:{30u,60u,120u,144u,175u,1000u}){ExplorerPerformance stats;for(unsigned frame=0;frame<=rate*2;++frame)stats.frame(double(frame)/rate);require(std::abs(stats.fps()-rate)<.001&&std::abs(stats.frameMs()-1000./rate)<.001,"Performance cadence measurement incorrect");stats.reset();require(stats.fps()==0&&stats.frameMs()==0,"Performance reset retained stale measurements");}ExplorerPerformance uneven;for(unsigned i=0;i<=60;++i)uneven.frame((i/2)*.04+(i%2)*.01);require(std::abs(uneven.fps()-50)<2,"Uneven frame intervals measured incorrectly");
 auto fixture=scene.root/"decompilation/reports/performance_settings_fixture";scene.menu.load(scene.root,fixture);scene.menu.page="graphics";scene.menu.selection=11;scene.menu.performanceDisplay=false;require(scene.menu.key(VK_RIGHT)==ExplorerMenu::Apply&&scene.menu.performanceDisplay,"Performance menu toggle failed");ExplorerMenu restored;restored.load(scene.root,fixture);require(restored.performanceDisplay,"Performance preference did not persist");scene.menu.open=false;scene.mission.briefing=false;for(unsigned i=0;i<=120;++i)scene.performance.frame(double(i)/60);preview(scene.root/"decompilation/reports/performance-preview.bmp");std::ofstream(scene.root/"decompilation/reports/performance_checks.json")<<"{\"status\":\"passed\",\"frame_rates_checked\":6,\"uneven_intervals\":true,\"settings\":true}";
}
void movieSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};unsigned count=0;for(const auto& file:fs::directory_iterator(scene.root/"GameData/PS1/Movies")){if(file.path().extension()!=".fmv")continue;ExplorerMovie movie;require(movie.open(file.path()),"Imported movie did not open");require(movie.frame()==0,"Movie did not begin at frame zero");movie.tick(.5,false);require(movie.time()==0,"Unfocused movie clock advanced");movie.skip();require(movie.active(),"New movie accepted carried-over skip input");movie.tick(.5,true);require(movie.frame()>0,"Movie frame did not advance");movie.skip();require(!movie.active(),"Movie skip failed");require(movie.open(file.path()),"Movie could not reopen");movie.tick(movie.duration()-.001,true);require(movie.active(),"Movie ended before its audio tail");movie.tick(.01,true);require(!movie.active(),"Movie did not finish");++count;}
 require(count==15,"Disc movie import is incomplete");auto fixture=scene.root/"decompilation/reports/movie_invalid.fmv";std::ofstream(fixture)<<"ATFMV002";ExplorerMovie invalid;require(!invalid.open(fixture),"Truncated movie metadata accepted");require(!invalid.open(scene.root/"GameData/PS1/Movies/ABSENT.fmv"),"Missing movie accepted");
 scene.moviesEnabled=true;auto settingsFixture=scene.root/"decompilation/reports/movie_settings_fixture";scene.menu.load(scene.root,settingsFixture);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TrilogyMovieCheck";RegisterClassW(&wc);auto window=CreateWindowW(wc.lpszClassName,L"Movie transition check",WS_OVERLAPPEDWINDOW,0,0,1280,800,nullptr,nullptr,wc.hInstance,nullptr);
 for(auto code:{"162","263","391"}){auto found=std::find_if(scene.physicalLevels.begin(),scene.physicalLevels.end(),[&](const auto& path){return path.stem().string()==std::string("L")+code+"LEV";});require(found!=scene.physicalLevels.end(),"Movie boundary mission unavailable");scene.level=*found;scene.load();auto memory=scene.mem();for(auto kind:scene.mission.kinds)memory.w(kind==35?0xa0ae2:0xa0ae6+kind*2,memory.w(0xa0bc8+kind*2));scene.mission.complete=true;scene.menu.hasGame=false;scene.movieQueue.clear();menuCommand(window,ExplorerMenu::NextMission);auto expected=std::string(code)=="162"?"S1_2":std::string(code)=="263"?"S2_3":"GAMEOVER";require(scene.movieQueue.size()==1&&scene.movieQueue.front()==expected,"Actual campaign transition did not queue its original FMV");if(std::string(code)!="391")require(scene.mission.briefing,"Chapter movie bypassed the mission briefing");}
 DestroyWindow(window);scene.movieQueue.clear();for(unsigned type=0;type<6;++type){scene.movieQueue.clear();scene.mem().w(0x22d3a8,type);scene.returnAfterDeath();constexpr const char* names[]={"DEATH2","DEATH5","DEATH3","DEATH4","DEATH1","DEATH6"};require(scene.movieQueue.size()==1&&scene.movieQueue.front()==names[type],"Original death-type movie selection differs");require(scene.menu.open&&!scene.menu.hasGame,"Death movie changed return-to-menu policy");}scene.movieQueue.clear();scene.queueMovie("INTRO");auto before=scene.player.position;auto ticks=scene.steps;scene.tick(1,1);require(scene.player.position==before&&scene.steps==ticks,"Movie did not freeze gameplay");scene.movieQueue.clear();
 require(scene.audio->validateMovieMixer(scene.root/"GameData/PS1/Movies/INTRO.pcm")>0,"Movie audio mixer failed");scene.audio->validateMixer();scene.moviesEnabled=false;
 require(scene.movie.open(scene.root/"GameData/PS1/Movies/INTRO.fmv"),"Intro unavailable for preview");scene.movieRunning=true;scene.movie.tick(30,true);preview(scene.root/"decompilation/reports/ps1-movie-preview.bmp");scene.movie.close();scene.movieRunning=false;
 std::ofstream(scene.root/"decompilation/reports/movie_checks.json")<<"{\"status\":\"passed\",\"movies_checked\":"<<count<<",\"skip_focus_timing\":true,\"original_death_selection\":true,\"gameplay_frozen\":true,\"movie_audio_pause_mute_stop\":true,\"legacy_audio\":true}";
}
void continueSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};auto fixture=scene.root/"decompilation/reports"/("continue_test_"+std::to_string(GetCurrentProcessId()));auto directory=fixture/"decompilation/user/saves";fs::create_directories(directory);scene.menu.load(scene.root,fixture);scene.menu.show(false);require(scene.menu.continueSlot<0&&scene.menu.labels().front()==L"Start","Empty profile offered Continue");
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L122LEV.MAP";scene.load();scene.difficulty=2;auto m=scene.mem();scene.player.health=69;m.w(0xa0ab8,69);m.w(0xa0abe,7);scene.mission.complete=true;for(auto kind:scene.mission.kinds)m.w(kind==35?0xa0ae2:0xa0ae6+kind*2,m.w(0xa0bc8+kind*2));scene.checkpointDirectory=directory;scene.checkpoint(0,true);scene.checkpoint(1,true);auto original=explorerRead(directory/"slot1.sav");auto write=[&](unsigned slot,const std::string& text){std::ofstream(directory/("slot"+std::to_string(slot)+".sav"))<<text;};auto now=fs::file_time_type::clock::now();fs::last_write_time(directory/"slot1.sav",now-std::chrono::seconds(10));fs::last_write_time(directory/"slot2.sav",now);write(3,"TRILOGY_NATIVE_PROGRESS 2 131 2 0 0\n1 2");fs::last_write_time(directory/"slot3.sav",now+std::chrono::seconds(10));scene.menu.show(false);require(scene.menu.continueSlot==1&&scene.menu.labels().front()==L"Continue","Latest valid slot not selected or truncated latest was accepted");scene.menu.selection=0;require(scene.menu.key(VK_RETURN)==ExplorerMenu::Load2,"Continue did not use existing checkpoint load command");scene.checkpoint(1,false);m=scene.mem();require(scene.mission.sourceCode=="131"&&scene.mission.briefing&&scene.player.health==69&&m.w(0xa0abe)==7&&scene.difficulty==2&&scene.menu.difficultyLocked,"Continue lost mission, equipment, briefing or locked difficulty");scene.menu.show(true);require(scene.menu.labels().front()==L"Continue","Live campaign did not offer Continue Game");
 std::string valid(original.begin(),original.end()),missing=valid;auto position=missing.find("131");require(position!=std::string::npos,"Continue fixture mission unexpected");missing.replace(position,3,"999");write(3,missing);scene.menu.show(false);require(scene.menu.continueSlot==1,"Unavailable mission masked valid Continue");write(2,"corrupt");scene.menu.show(false);require(scene.menu.continueSlot==0,"Corrupt newer slot prevented fallback");write(1,"corrupt");scene.menu.selection=0;require(scene.menu.key(VK_RETURN)==ExplorerMenu::None&&scene.menu.continueSlot<0&&scene.menu.open,"Deleted/corrupt Continue target was loaded");
 write(1,valid);write(2,valid);fs::last_write_time(directory/"slot1.sav",now);fs::last_write_time(directory/"slot2.sav",now);scene.menu.show(false);require(scene.menu.continueSlot==0,"Equal save timestamps were not deterministic");scene.menu.selection=0;preview(scene.root/"decompilation/reports/continue-menu-preview.bmp");std::ofstream(scene.root/"decompilation/reports/continue_checks.json")<<"{\"status\":\"passed\",\"latest_valid_save\":true,\"corruption_fallback\":true,\"missing_mission_skipped\":true,\"inventory_difficulty_briefing_restored\":true,\"live_resume_preserved\":true,\"equal_time_order\":true}";
}
void menuSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};auto fixture=scene.root/"decompilation/reports"/("menu_test_"+std::to_string(GetCurrentProcessId()));{std::error_code stale;fs::remove_all(fixture,stale);}scene.menu.load(scene.root,fixture);scene.menu.show(false);auto& menu=scene.menu;require(menu.labels()==std::vector<std::wstring>({L"Start",L"Load",L"Survival",L"Online Co-Op",L"Stats",L"Replays",L"Options",L"Enter Password",L"Exit"}),"Unsupported/main menu entries incorrect");menu.key(VK_DOWN);require(menu.soundEvent==0,"Menu movement cue missing");menu.key(VK_UP);require(menu.soundEvent==0&&menu.selection==0,"Menu return movement cue missing");require(menu.hover(550,525,1100,700)&&menu.soundEvent==0&&menu.selection==1,"Mouse hover cue missing");require(!menu.hover(550,526,1100,700)&&menu.soundEvent==-1,"Mouse movement replayed unchanged hover sound");menu.selection=0;
 preview(scene.root/"decompilation/reports/main-menu-preview.bmp");require(menu.key(VK_RETURN)==ExplorerMenu::None&&menu.page=="new-difficulty"&&menu.soundEvent==1,"Start Game did not request difficulty/confirmation sound");menu.selection=1;require(menu.key(VK_RETURN)==ExplorerMenu::Start&&!menu.open,"Difficulty confirmation did not start game");
 menu.show(true);require(menu.key(VK_RETURN)==ExplorerMenu::Resume&&!menu.open,"Resume Game failed");menu.show(true);menu.key(VK_F1);require(menu.page=="options","Options entry did not open");menu.key(VK_RETURN);require(menu.page=="volume","Volume submenu failed");menu.sfx=100;menu.key(VK_LEFT);require(menu.sfx==95,"SFX volume slider did not change");menu.selection=1;menu.music=100;menu.key(VK_LEFT);require(menu.music==95,"Music volume slider did not change");menu.key(VK_ESCAPE);menu.selection=1;menu.key(VK_RETURN);require(menu.page=="controls","Controls submenu failed");menu.selection=4;menu.key(VK_RETURN);menu.key('Z');require(menu.bindings[4]=='Z',"Keyboard remapping failed");menu.selection=11;menu.key(VK_RIGHT);require(menu.sensitivity==4,"Mouse sensitivity option failed");menu.selection=12;menu.key(VK_RETURN);require(menu.invert,"Mouse inversion option failed");menu.key(VK_ESCAPE);menu.selection=2;menu.key(VK_RETURN);menu.selection=2;menu.key(VK_RETURN);require(menu.difficulty==2,"Difficulty selection failed");menu.key(VK_ESCAPE);menu.selection=3;menu.key(VK_RETURN);require(menu.sway,"Camera sway toggle failed");menu.selection=4;menu.key(VK_RETURN);require(menu.page=="graphics","Graphics submenu failed");menu.key(VK_RETURN);require(menu.displayMode==ExplorerMenu::Borderless,"Display mode option failed");menu.selection=1;menu.key(VK_RIGHT);require(menu.resolution==1,"Resolution option failed");menu.selection=2;menu.key(VK_RETURN);require(menu.renderQuality==1,"Rendering quality option failed");preview(scene.root/"decompilation/reports/options-menu-preview.bmp");ExplorerMenu loaded;loaded.load(scene.root,fixture);require(loaded.sfx==95&&loaded.music==95&&loaded.bindings[4]=='Z'&&loaded.sensitivity==4&&loaded.invert&&loaded.difficulty==2&&loaded.displayMode==ExplorerMenu::Borderless&&loaded.resolution==1&&loaded.renderQuality==1&&loaded.sway,"Menu settings did not survive relaunch");
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L122LEV.MAP";scene.load();auto m=scene.mem();m.w(0xa0ab8,69);scene.player.health=69;m.w(0xa0abe,7);scene.checkpointDirectory=fixture/"decompilation/user/saves";bool anywhereRejected=false;try{scene.checkpoint(0,true);}catch(const std::exception&){anywhereRejected=true;}require(anywhereRejected,"Saving during a mission was permitted");scene.mission.complete=true;for(auto kind:scene.mission.kinds)m.w(kind==35?0xa0ae2:0xa0ae6+kind*2,m.w(0xa0bc8+kind*2));scene.checkpoint(0,true);m.w(0xa0abe,1);scene.checkpoint(0,false);m=scene.mem();require(scene.mission.sourceCode=="131"&&scene.mission.index==4&&scene.player.health==69&&m.w(0xa0abe)==7&&scene.mission.briefing,"Checkpoint did not restore mission/inventory");require(menu.slot(0).find(L"131")!=std::wstring::npos,"Load menu did not identify occupied checkpoint");bool rejected=false;std::ofstream(scene.checkpointDirectory/"slot2.sav")<<"bad checkpoint";try{scene.checkpoint(1,false);}catch(const std::exception&){rejected=true;}require(rejected&&scene.mission.sourceCode=="131","Invalid checkpoint was not rejected before mutation");
 menu.show(true);menu.selection=1;menu.key(VK_RETURN);require(menu.page=="new-confirm","New game did not protect running session");menu.key(VK_RETURN);require(menu.page=="main","New game cancel failed");menu.selection=unsigned(menu.labels().size()-1);menu.key(VK_RETURN);menu.selection=1;require(menu.key(VK_RETURN)==ExplorerMenu::Quit,"Exit confirmation failed");
 menu.show(true);menu.pauseGame();menu.owned[0]=true;menu.ammunition[0]=7;menu.reserves[0]=2;menu.equipped=0;menu.missionNote=scene.mission.title+L"\n\n"+scene.mission.text+L"\n"+scene.mission.progress();require(menu.labels()==std::vector<std::wstring>({L"9mm Pistol",L"Shotgun",L"Flamethrower",L"Pulse Rifle",L"Smart Gun",L"Batteries",L"Mission",L"Load Game",L"Options",L"Exit to main menu",L"Exit Game"}),"Original pause inventory/save restrictions incorrect");auto before=scene.player.position;auto ticks=scene.steps;scene.tick(1,1);require(scene.steps==ticks&&scene.player.position==before,"Pause menu did not stop simulation");preview(scene.root/"decompilation/reports/original-pause-preview.bmp");menu.selection=6;menu.key(VK_RETURN);require(menu.page=="mission","Pause mission entry failed");menu.key(VK_ESCAPE);require(menu.page=="pause","Pause mission back failed");require(menu.key(VK_ESCAPE)==ExplorerMenu::Resume&&!menu.open,"Escape did not resume gameplay");
 menu.pauseGame();menu.selection=9;auto exitCommand=menu.key(VK_RETURN);require(exitCommand==ExplorerMenu::MainMenu,"Pause exit-to-menu command failed");menuCommand(nullptr,exitCommand);require(menu.open&&menu.page=="main"&&!menu.hasGame&&!scene.mission.briefing&&!scene.mission.complete,"Pause exit did not abandon the active level");require(menu.key(VK_ESCAPE)==ExplorerMenu::None&&menu.open,"Escape resumed an abandoned level");
 menu.page="difficulty";menu.open=true;menu.difficultyLocked=true;auto locked=menu.difficulty;menu.selection=(locked+1)%3;menu.key(VK_RETURN);require(menu.difficulty==locked,"Campaign difficulty changed after locking");menu.open=false;
 // Original HUD digits, frame transparency, total ammunition and armor.
 m=scene.mem();m.w(0xa0ab8,73);m.w(0xa0aba,40);m.w(0xa0abe,7);m.w(0xa0ac0,2);scene.hud.syncAmmo();auto& panel=scene.hud.status();require(panel.width==320&&panel.height==240&&scene.hud.statusHealth==73&&scene.hud.statusArmor==40&&scene.hud.statusAmmo==37&&scene.hud.statusPixels>1000,"Original HUD state/artwork failed");scene.mission.briefing=false;scene.player.health=73;preview(scene.root/"decompilation/reports/original-hud-preview.bmp");
 scene.menu.open=false;scene.mission.briefing=false;scene.notice=L"Grenades";scene.noticeTicks=60;preview(scene.root/"decompilation/reports/ps1-pickup-text-preview.bmp");scene.mission.briefing=true;scene.briefingRevealed=true;preview(scene.root/"decompilation/reports/ps1-briefing-text-preview.bmp");
 std::ofstream report(scene.root/"decompilation/reports/menu_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"original_menu_backgrounds\": 2,\n  \"unsupported_entries_hidden\": true,\n  \"start_resume_navigation\": true,\n  \"audio_sliders\": true,\n  \"keyboard_mouse_settings\": true,\n  \"difficulty_selection\": true,\n  \"graphics_options\": true,\n  \"settings_persisted\": true,\n  \"progress_round_trip\": true,\n  \"invalid_progress_save_rejected\": true,\n  \"new_game_and_quit_confirmation\": true,\n  \"original_pause_inventory\": true,\n  \"pause_freezes_gameplay\": true,\n  \"difficulty_locked\": true,\n  \"end_of_level_saves_only\": true,\n  \"original_hud_artwork\": true\n}\n";
 scene.checkpointDirectory.clear();menu=ExplorerMenu{};scene.difficulty=1;scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();
}
void missionObjectSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};unsigned maps=0,objects=0,barrels=0,crates=0,contents=0,textureFaces=0,profiles=0;
 for(auto& path:scene.physicalLevels){scene.level=path;scene.load(true);auto m=scene.mem();++maps;for(auto i:scene.objects.entries){auto p=0x320000+i*40,r=m.d(0xa0c58)+i*16;unsigned kind=m.b(p+24);require(m.w(p+26)==m.b(r+4)*16,"Object health did not come from map data");++objects;if(kind==23)++barrels;if(kind==20||kind==25)++crates;
   if(kind==20||kind==23||kind==25){auto model=kind==23?0u:kind==25?2u:1u;for(auto& face:scene.panelMeshes.at(model).faces){require(scene.batteryTextures.rect(face.texture)!=nullptr,"Destructible face has no original texture");++textureFaces;}}
   if(kind>=33)continue;
   // Remove blasts from this coverage pass to isolate each record's contents.
   scene.objects.blast={};scene.objects.enemy={};scene.objects.pickup=[&](unsigned object,unsigned index){require(index<m.w(0xa0c1c),"Crate contents referenced an invalid pickup");scene.pickups.crateDrop(object,index);++contents;};
   m.b(p+25,m.b(p+25)&254);damageMapObject(m,i,32767,[](auto&,auto,auto,auto,auto){return PlayerControlResult{};});require(scene.objects.visible(i),"Object vanished before original destruction delay");for(unsigned tick=0;tick<9;++tick)scene.objects.tick();require(!scene.objects.visible(i),"Destroyed object retained its model/collision");
  }
 }
 require(maps==36&&crates&&barrels&&contents&&textureFaces,"Missing destructible/texture map coverage");
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();auto m=scene.mem();require(scene.mission.index==0&&scene.mission.kinds==std::vector<unsigned>({20,25,23}),"Entrance objective table mapping incorrect");require(!scene.mission.eligible(),"Fresh entrance already satisfies objectives");
 for(auto k:scene.mission.kinds)m.w(0xa0ae6+k*2,m.w(0xa0bc8+k*2));require(scene.mission.eligible(),"Completed entrance objective counters did not unlock extraction");
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L122LEV.MAP";scene.load();m=scene.mem();require(scene.mission.index==3,"Campaign order followed filename order");require(!scene.mission.eligible(),"Fresh panel mission skipped its objectives");
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L131LEV.MAP";scene.load();m=scene.mem();require(scene.mission.index==4&&scene.mission.kinds==std::vector<unsigned>({35})&&!scene.mission.eligible(),"ID-tag mission mapping/denominator incorrect");m.w(0xa0ae2,m.w(0xa0c0e));require(scene.mission.eligible(),"ID-tag collection did not satisfy objective");
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L113LEV.MAP";scene.load();require(scene.mission.remaining==1800,"Original ammunition-dump countdown incorrect");scene.mission.briefing=true;scene.mission.tick();require(scene.mission.remaining==1800,"Briefing consumed mission countdown");scene.mission.briefing=false;for(unsigned t=0;t<1800;++t)scene.mission.tick();require(scene.mission.complete,"Timed mission did not extract at timeout");
 // The exact recovered hit rule is independent of difficulty. Compare both
 // halves of projectile life, all five weapon kinds, and all three modes.
 constexpr unsigned base[]={16,80,96,160,80},kind[]={0,1,2,4,3},life[]={20,16,28,28,15};
 for(unsigned difficulty=0;difficulty<3;++difficulty)for(unsigned weapon=0;weapon<5;++weapon)for(unsigned late=0;late<2;++late){
  scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.difficulty=difficulty;scene.load();m=scene.mem();auto list=scene.enemies->active();auto e=*std::find_if(list.begin(),list.end(),[&](unsigned p){return m.b(p+0x74)==6;});m.d(0x2365e0,e);m.d(e+0xc8,0);m.b(e+0x6f,0);m.w(e+0x4e,0);m.w(e+0x58,1000);unsigned projectile=0x350000,pos=m.d(projectile);m.b(projectile+32,0);m.b(projectile+30,kind[weapon]);m.w(projectile+26,base[weapon]);m.b(projectile+29,life[weapon]);m.b(projectile+28,late?life[weapon]/2-1:life[weapon]/2);m.d(projectile+12,m.d(e+0x78));for(unsigned axis=0;axis<3;++axis)m.w(pos+axis*2,m.w(e+axis*2));
  collideProjectileWithEntities(m,projectile,[](auto&,auto,auto,auto,auto){return PlayerControlResult{};});require(m.w(e+0x58)==1000-base[weapon]*(late?1:2),"Weapon/difficulty damage differs from recovered collision rule");++profiles;
 }
 // Regression: real pistol flight must reach an isolated native enemy.
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();m=scene.mem();auto e=scene.enemies->active().front();m.d(0x2365e0,e);m.d(e+200,0);m.b(e+0x74,6);m.b(e+0x6f,0);m.w(e+0x4e,0);m.w(e+0x58,1000);m.w(e+8,256);m.w(e+10,768);m.w(e+12,256);
 Vec3s origin{short(8*512+256-m.w(0xa0c34)),0,short(8*512+256-m.w(0xa0c36))};m.w(e,origin[0]);m.w(e+2,32);m.w(e+4,origin[2]+1200);auto enemyCell=m.d(0xa0c40)+(10*m.w(0xa0c30)+8)*16;m.d(e+0x78,enemyCell);
 for(unsigned z=0;z<m.w(0xa0c32);++z)for(unsigned x=0;x<m.w(0xa0c30);++x){auto p=m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16;m.b(p+10,0);m.b(p+11,0);m.b(p+13,120);m.b(p+12,0);m.w(p+8,65535);}
 m.w(0xa0abe,10);require(scene.enemies->fireWeapon(origin,0,0,0),"Pistol did not create a projectile");std::ofstream trace(scene.root/"decompilation/reports/pistol-flight.txt");for(unsigned tick=0;tick<20;++tick){for(auto p=m.d(0x236300);p;p=m.d(p+36)){auto pos=m.d(p);trace<<tick<<" "<<m.s(pos)<<" "<<m.s(pos+2)<<" "<<m.s(pos+4)<<" life "<<unsigned(m.b(p+28))<<" target "<<m.s(e)<<" "<<m.s(e+2)<<" "<<m.s(e+4)<<" hp "<<m.s(e+0x58)<<"\n";}scene.enemies->tickProjectiles();}require(m.w(e+0x58)==968,"Live pistol projectile failed to damage a standing alien");
 resetProjectilePool(m);m.w(e+4,origin[2]+3500);for(unsigned axis=0;axis<3;++axis)m.w(0xa0a48+axis*2,origin[axis]);m.w(e+0x58,1000);m.w(e+0x4e,0);m.b(e+0x6f,0);for(unsigned i=0;i<m.w(0xa0c30)*m.w(0xa0c32);++i)m.w(m.d(0xa0c40)+i*16+8,0);auto distantCell=m.d(0xa0c40)+(((m.s(e+4)+m.w(0xa0c36))>>9)*m.w(0xa0c30)+((m.s(e)+m.w(0xa0c34))>>9))*16;m.d(e+0x78,distantCell);m.w(distantCell+8,1);require(scene.enemies->fireWeapon(origin,0,0,0),"Long-range pistol did not fire");for(unsigned tick=0;tick<80;++tick)scene.enemies->tickProjectiles();require(m.w(e+0x58)==984,"Player round failed in visible fog range with disjoint visibility regions");resetProjectilePool(m);m.w(e+4,origin[2]+8000);m.w(e+0x58,1000);m.w(e+0x4e,0);m.b(e+0x6f,0);require(scene.enemies->fireWeapon(origin,0,0,0),"Fog-limit shot did not fire");for(unsigned tick=0;tick<80;++tick)scene.enemies->tickProjectiles();require(m.w(e+0x58)==1000,"Player shot damaged an enemy hidden beyond black fog");m.w(e+4,origin[2]+1200);m.d(e+0x78,enemyCell);for(unsigned i=0;i<m.w(0xa0c30)*m.w(0xa0c32);++i)m.w(m.d(0xa0c40)+i*16+8,65535);

 resetProjectilePool(m);m.b(e+0x74,2);m.b(e+0x6f,2);m.w(e+10,512);m.w(e+0x58,1000);m.w(e+0x4e,0);m.w(e+0x30,16);m.w(e+0x34,16);
 require(scene.enemies->fireWeapon(origin,0,-280,0),"Pistol did not fire at a low target");for(unsigned tick=0;tick<20;++tick)scene.enemies->tickProjectiles();require(m.w(e+0x58)<1000,"Aimed-down pistol failed to damage a walking facehugger");
 
for(unsigned difficulty=0;difficulty<3;++difficulty){m.w(0x96234,difficulty);m.b(0x351840+5,3);m.b(0x351840,6);require((enemySpawnHealth(m,0x351840)&65535)==(3+difficulty)*16,"Original regular enemy difficulty health scaling failed");m.b(0x351840,7);require((enemySpawnHealth(m,0x351840)&65535)==(3+difficulty)*64,"Original queen difficulty health scaling failed");}
 // Exercise real burst constructors, not the old instant-hit AI fixture.
 scene.difficulty=1;scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();m=scene.mem();for(unsigned z=0;z<m.w(0xa0c32);++z)for(unsigned x=0;x<m.w(0xa0c30);++x){auto p=m.d(0xa0c40)+(z*m.w(0xa0c30)+x)*16;m.b(p+10,0);m.b(p+11,0);m.b(p+13,120);m.b(p+12,0);}m.d(0x2365e0,0);
 for(unsigned weapon=0;weapon<5;++weapon){resetProjectilePool(m);constexpr unsigned ammo[]={0xa0abe,0xa0ac2,0xa0ac4,0xa0acc,0xa0aca};m.w(ammo[weapon],10);Vec3s pos{short(8*512-m.w(0xa0c34)),0,short(8*512-m.w(0xa0c36))};require(scene.enemies->fireWeapon(pos,0,0,weapon),"Weapon constructor did not fire");unsigned count=0;for(auto p=m.d(0x236300);p;p=m.d(p+36)){require(m.w(p+26)==base[weapon],"Weapon constructor used wrong damage");++count;}unsigned expected=weapon==2||weapon==3?3:weapon==4?2:1;require(count==expected,"Original burst/stream count incorrect");require(m.w(ammo[weapon])==10-(weapon==2||weapon==3?3:1),"Burst ammunition consumption incorrect");if(weapon==0){auto p=m.d(0x236300),vel=m.d(p+4),pos=m.d(p);m.w(vel,0);m.w(vel+2,0);m.w(vel+4,1);auto z=m.s(pos+4);for(unsigned tick=0;tick<100;++tick)scene.enemies->tickProjectiles();require(m.d(0x236300)==p&&m.b(p+28)==1&&m.s(pos+4)==z+100,"Ballistic round expired instead of continuing its flight");}for(unsigned t=0;t<40;++t)scene.enemies->tickProjectiles();if(weapon==4)require(!m.d(0x236300),"Flame exceeded its original lifetime");else{for(unsigned c=0;c<m.w(0xa0c30)*m.w(0xa0c32);++c)m.b(m.d(0xa0c40)+c*16+10,20);scene.enemies->tickProjectiles();require(!m.d(0x236300),"Bullets did not stop at walls");for(unsigned c=0;c<m.w(0xa0c30)*m.w(0xa0c32);++c)m.b(m.d(0xa0c40)+c*16+10,0);}}
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();m=scene.mem();auto i=*std::find_if(scene.objects.entries.begin(),scene.objects.entries.end(),[&](unsigned i){return m.b(0x320000+i*40+24)==20;});auto object=0x320000+i*40;scene.player.position={short(m.s(object)),short(m.s(object+2)),short(m.s(object+4)-1600)};scene.yaw=0;scene.pitch=-120;preview(scene.root/"decompilation/reports/destructibles-preview.bmp");scene.mission.briefing=true;scene.briefingRevealed=true;preview(scene.root/"decompilation/reports/mission-briefing-preview.bmp");scene.load();
 std::ofstream report(scene.root/"decompilation/reports/mission_object_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"maps\": "<<maps<<",\n  \"placed_objects\": "<<objects<<",\n  \"crates\": "<<crates<<",\n  \"barrels\": "<<barrels<<",\n  \"content_releases\": "<<contents<<",\n  \"textured_model_faces\": "<<textureFaces<<",\n  \"difficulty_damage_profiles\": "<<profiles<<",\n  \"weapon_constructor_checks\": 5,\n  \"campaign_mapping\": true,\n  \"id_tag_objectives\": true,\n  \"countdown_extraction\": true\n}\n";
}
void enemySelfTest(){
    auto require=[](bool condition,const char* message){if(!condition)throw std::runtime_error(message);};
    auto m=scene.mem();require(scene.enemies->active().size()==19,"First-level active enemy count differs");
    unsigned frameChecks=0;
    for(unsigned kind:{2u,6u}){auto list=scene.enemies->active();auto it=std::find_if(list.begin(),list.end(),[&](unsigned e){return m.b(e+0x74)==kind;});require(it!=list.end(),"Expected first-level species missing");auto e=*it;
        for(unsigned animation=0;animation<(kind==2?11u:13u);++animation){startEntityAnimation(m,e,static_cast<short>(animation));for(unsigned tick=0;tick<80;++tick){auto& sprite=scene.enemies->sprite(e);require(sprite.width&&sprite.pixels.size()==sprite.width*sprite.height,"Enemy sprite did not decode");++frameChecks;if(m.b(e+0xb6)&6)break;advanceAnimation(m,e+128);}}
    }
    auto decoded=scene.enemies->decodedFrames;
    // Controlled encounter in the same mapped level store. The flat bounded
    // fixture isolates native AI/contact combat from unfinished map triggers.
    scene.load();m=scene.mem();auto grid=m.d(0xa0c40);unsigned width=m.w(0xa0c30),height=m.w(0xa0c32);
    for(unsigned z=0;z<height;++z)for(unsigned x=0;x<width;++x){auto p=grid+(z*width+x)*16;m.b(p+10,!x||!z||x+1==width||z+1==height?20:0);m.b(p+11,0);m.b(p+13,96);m.b(p+12,0);}
    auto records=m.d(0xa0c48);m.w(0xa0c1a,2);
    for(unsigned i=0;i<2;++i){auto p=records+i*20;m.b(p,i?6:2);m.b(p+1,8);m.b(p+2,i?12:8);m.b(p+4,i?0:4);m.b(p+5,3);m.b(p+6,255);m.b(p+7,0);m.b(p+8,0);m.w(p+10,2048);m.w(p+12,8192);m.w(p+14,100);m.w(p+16,4096);m.w(p+18,1536);}
    scene.enemies=std::make_unique<ExplorerEnemies>(m,scene.root);scene.enemies->initialize();
    Vec3s player{static_cast<short>(8*512+256-m.w(0xa0c34)),0,static_cast<short>(10*512+256-m.w(0xa0c36))};unsigned minimumHealth=100;
    for(unsigned tick=0;tick<600;++tick){scene.enemies->tick(player,100);minimumHealth=std::min(minimumHealth,scene.enemies->health());for(auto e:scene.enemies->active()){auto x=m.s(e)+m.w(0xa0c34),z=m.s(e+4)+m.w(0xa0c36);require(x>=200&&z>=200&&x<int(width*512)-200&&z<int(height*512)-200,"Enemy left safe grid bounds");scene.enemies->sprite(e);}}
    require(scene.enemies->movingTicks>10,"Enemy encounter did not move");require(scene.enemies->attackTicks>0,"Enemy encounter did not attack");require(minimumHealth<100,"Enemy encounter did not damage player");
    unsigned deathHealth=1;m.w(0xa0b30,0);for(unsigned i=0;i<600&&deathHealth;++i){scene.enemies->tick(player,deathHealth);deathHealth=scene.enemies->health();}require(!deathHealth,"Enemy attacks did not trigger player death");scene.enemies->tick(player,100);
    auto movement=scene.enemies->movingTicks,attacks=scene.enemies->attackTicks;require(!scene.enemies->unknownServices,"Unrecognized service in playable encounter");
    auto list=scene.enemies->active();auto target=*std::find_if(list.begin(),list.end(),[&](unsigned e){return m.b(e+0x74)==6;});
    for(auto e:list){m.w(e+0x30,0);m.w(e+0x32,0);m.w(e+0x34,0);m.w(e+0x52,65535);m.b(e+0x6f,0);m.b(e+0x6c,0);}
    m.w(target,player[0]);m.w(target+2,32);m.w(target+4,player[2]+1200);m.w(target+0x4e,0);m.w(target+0x58,16);m.d(target+0x78,grid+((unsigned(m.s(target+4)+m.w(0xa0c36))>>9)*width+8)*16);
    // A wall between player and target must stop the host's firing bridge.
    auto wall=grid+(11*width+8)*16;m.b(wall+10,20);require(!scene.enemies->shoot(player,0,0,1280*.72),"Shot passed through a wall");require(m.w(target+0x58)==16,"Occluded enemy lost health");m.b(wall+10,0);
    for(unsigned i=0;i<6;++i)scene.enemies->tick(player,100);
    m.w(target+0x4e,0);m.b(target+0x6f,2);auto priorKills=scene.enemies->kills;require(scene.enemies->shoot(player,0,0,1280*.72),"Visible enemy was not hit");require(scene.enemies->kills==priorKills+1&&m.b(target+0x6f)==7,"Native enemy death did not trigger");
    for(unsigned i=0;i<80;++i){scene.enemies->tick(player,100);scene.enemies->sprite(target);}
    require(m.b(target+0x6f)==8,"Corpse animation did not finish");require(!scene.enemies->unknownServices,"Unknown service during enemy death/corpse processing");
    scene.load();require(scene.enemies->active().size()==19&&scene.player.health==100&&!scene.enemies->kills&&!scene.enemies->shots,"Restart did not reset encounter");
    for(unsigned i=0;i<2;++i)scene.tick(0,0);
    // Validate sprite display and depth masking independently of camera choice.
    auto displayList=scene.enemies->active();auto displayEnemy=*std::find_if(displayList.begin(),displayList.end(),[&](unsigned e){return m.b(e+0x74)==6;});
    m.d(0x2365e0,displayEnemy);m.d(displayEnemy+200,0);m.w(displayEnemy,scene.player.position[0]);m.w(displayEnemy+2,scene.player.position[1]+32);m.w(displayEnemy+4,scene.player.position[2]+2000);startEntityAnimation(m,displayEnemy,0);scene.yaw=0;scene.pitch=0;scene.faces.clear();
    preview(scene.root/"decompilation/reports/enemy-sprite-preview.bmp");auto visiblePixels=scene.lastSpritePixels;require(visiblePixels>100,"Original enemy sprite was not displayed");
    double px=scene.player.position[0],py=scene.player.position[1],pz=scene.player.position[2]+1700;
    scene.vertices={{px-2000,py-2000,pz},{px+2000,py-2000,pz},{px+2000,py+3000,pz},{px-2000,py+3000,pz}};scene.faces={Face{{0,1,2,3},0}};
    preview(scene.root/"decompilation/reports/enemy-occlusion-preview.bmp");require(scene.lastSpritePixels==0,"Enemy sprite showed through opaque geometry");
    scene.load();for(unsigned i=0;i<2;++i)scene.tick(0,0);
    // Find a real-map camera/actor placement with visible pixels for visual QA.
    auto realList=scene.enemies->active();auto e=*std::find_if(realList.begin(),realList.end(),[&](unsigned e){return m.b(e+0x74)==6;});m.d(0x2365e0,e);m.d(e+200,0);startEntityAnimation(m,e,0);
    bool visibleOnMap=false;
    for(short distance:{short(700),short(1100),short(1800)})for(int yaw=0;yaw<4096&&!visibleOnMap;yaw+=512){auto v=rotateVector({0,static_cast<short>(yaw),2048},{0,0,distance},scene.sine);int x=scene.player.position[0]+v[0]+scene.player.originX,z=scene.player.position[2]+v[2]+scene.player.originZ;if(x<512||z<512||x>=(width-1)*512||z>=(height-1)*512)continue;auto cell=scene.grid.cell(x,z);if(cell.bytes[10]>=20&&cell.bytes[10]<45)continue;m.w(e,x-scene.player.originX);m.w(e+2,floorHeight(scene.grid,x,z)+32);m.w(e+4,z-scene.player.originZ);scene.yaw=yaw;scene.pitch=0;preview(scene.root/"decompilation/reports/enemy-playable-preview.bmp");visibleOnMap=scene.lastSpritePixels>100;}
    require(visibleOnMap,"Enemy display could not be verified against real level geometry");
    scene.load();
    std::ofstream report(scene.root/"decompilation/reports/enemy_playable_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"initial_active_enemies\": 19,\n  \"sprite_frame_checks\": "<<frameChecks<<",\n  \"visible_sprite_pixels\": "<<visiblePixels<<",\n  \"distinct_decoded_sprite_frames\": "<<decoded<<",\n  \"encounter_ticks\": 600,\n  \"movement_ticks\": "<<movement<<",\n  \"attack_ticks\": "<<attacks<<",\n  \"minimum_health_after_enemy_hit\": "<<minimumHealth<<",\n  \"wall_occlusion_checks\": 2,\n  \"player_death\": true,\n  \"native_death_and_corpse\": true,\n  \"restart_reset\": true,\n  \"unknown_services\": 0,\n  \"scope\": \"Host integration checks; navigation, hitscan and display provisional. No original comparison credit.\"\n}\n";
}
void weaponAudioSelfTest(){
 if(scene.audio->ps1WeaponSamples()!=16||scene.audio->ps1PermanentSamples()!=41||!scene.audio->ps1LevelSamples())throw std::runtime_error("PS1 sound banks not active");
    auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();auto m=scene.mem();for(unsigned kind=1;kind<5;++kind)awardPickup(m,kind,100,3);
    constexpr unsigned expectedFire[]={13,16,6,19,11};unsigned pumpCues=0,reloadCues=0,frameChecks=0;
    for(unsigned weapon=0;weapon<5;++weapon){if(weapon)scene.hud.select(weapon);scene.hud.takeSounds();scene.hud.fire();auto cues=scene.hud.takeSounds();require(cues.size()==1&&cues[0]==expectedFire[weapon],"Weapon firing cue does not match its original animation program");if(weapon==1)require(!scene.hud.canFire(),"Shotgun could restart before its pump animation");
        for(unsigned tick=0;tick<40;++tick){scene.hud.tick();scene.hud.weapon();++frameChecks;for(auto cue:scene.hud.takeSounds()){if(weapon==1){require(cue==15,"Unexpected shotgun firing-cycle cue");require(tick>=4,"Shotgun pump played with the shot instead of after it");++pumpCues;}else require(weapon==4&&cue==11,"Weapon fire animation emitted another weapon's cue");}}
        if(weapon==4){scene.hud.stopFiring();auto stop=scene.hud.takeSounds();require(stop.size()==1&&stop[0]==10,"Flamethrower release cue is missing");scene.hud.stopFiring();require(scene.hud.takeSounds().empty(),"Flamethrower release cue repeated while idle");}
        if(scene.hud.reserve()){auto expected=scene.hud.reloadSound();scene.hud.reload();unsigned count=0;for(unsigned tick=0;tick<31;++tick){scene.hud.weapon();++frameChecks;for(auto cue:scene.hud.takeSounds()){require(cue==expected,"Reload played an unrelated weapon sound");++count;}scene.hud.tick();}require(count==1,"Reload sound did not play exactly once");++reloadCues;}
    }
    require(pumpCues==1&&reloadCues==3,"Weapon pump/reload cue count mismatch");scene.load();std::ofstream report(scene.root/"decompilation/reports/weapon_audio_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"fire_slots\": [13,16,6,19,11],\n  \"shotgun_pump_slot\": 15,\n  \"shotgun_pump_cues\": "<<pumpCues<<",\n  \"single_reload_cue_checks\": "<<reloadCues<<",\n  \"flamethrower_release_slot\": 10,\n  \"original_frame_checks\": "<<frameChecks<<"\n}\n";
}
void weaponSwitchSelfTest(){
    auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();auto m=scene.mem();require(!scene.hud.select(1)&&!scene.hud.cycle(),"Weapon selector allowed an uncollected gun");for(unsigned kind=1;kind<5;++kind)awardPickup(m,kind,100,3);
    m.w(0xa0abe,1);m.w(0xa0ac0,2);scene.hud.syncAmmo();scene.hud.fire();require(!scene.hud.ammo,"Pistol last round was not spent");for(unsigned tick=0;tick<120&&!scene.hud.reloadTicks;++tick)scene.hud.tick();require(scene.hud.reloadTicks&&m.w(0xa0ac0)==2&&!scene.hud.canFire(),"Empty pistol did not start automatic reload");for(unsigned tick=0;tick<30;++tick){scene.hud.tick();scene.hud.weapon();}require(scene.hud.ammo==15&&m.w(0xa0ac0)==1,"Automatic pistol reload did not consume exactly one magazine");for(unsigned tick=0;tick<60;++tick)scene.hud.tick();require(m.w(0xa0ac0)==1,"Loaded pistol repeated automatic reload");m.w(0xa0abe,0);m.w(0xa0ac0,0);scene.hud.syncAmmo();for(unsigned tick=0;tick<60;++tick)scene.hud.tick();require(scene.hud.ammo==1&&!scene.hud.reloadTicks&&!m.w(0xa0ac0),"Empty-reserve pistol did not reload exactly one fallback round");for(unsigned cycle=0;cycle<3;++cycle){scene.hud.fire();require(!scene.hud.ammo&&!scene.hud.canFire(),"Fallback round did not require another reload");for(unsigned tick=0;tick<120&&!scene.hud.reloadTicks;++tick)scene.hud.tick();require(scene.hud.reloadTicks&&!scene.hud.ammo,"Fallback skipped reload delay");for(unsigned tick=0;tick<30;++tick)scene.hud.tick();require(scene.hud.ammo==1&&!m.w(0xa0ac0),"Fallback accumulated ammo or changed reserve count");}m.w(0xa0abe,15);m.w(0xa0ac0,2);scene.hud.syncAmmo();
    unsigned frames=0,reloads=0;for(unsigned weapon=0;weapon<5;++weapon){if(weapon)require(scene.hud.select(weapon),"Collected weapon did not select");require(scene.hud.selected()==weapon&&m.w(0xa0aae)==weapon,"Selected weapon state mismatch");auto before=scene.hud.ammo;require(before>0,"Selected weapon has no native ammunition");scene.hud.fire();require(scene.hud.ammo==before-1,"Weapon consumed the wrong ammunition");for(unsigned tick=0;tick<40;++tick){auto& frame=scene.hud.weapon();require(frame.width&&frame.height&&frame.pixels.size()==frame.width*frame.height,"Weapon idle/fire frame did not decode");++frames;scene.hud.tick();}if(scene.hud.reserve()){auto reserve=scene.hud.reserve();scene.hud.reload();require(scene.hud.reloadTicks>0,"Magazine weapon refused reload");for(unsigned tick=0;tick<30;++tick)scene.hud.tick();require(scene.hud.ammo==(weapon==0?15u:100u)&&scene.hud.reserve()==reserve-1,"Reload did not use the selected weapon's reserve");++reloads;}preview(scene.root/("decompilation/reports/weapon-"+std::to_string(weapon)+"-preview.bmp"));}
    require(scene.hud.cycle()&&scene.hud.selected()==0,"Q/next weapon did not wrap to pistol");require(scene.hud.cycle(-1)&&scene.hud.selected()==4,"Previous weapon did not wrap backwards");require(ExplorerAudio::PistolFire==13&&ExplorerAudio::PistolReload==12,"Pistol fire/reload sound assignments regressed");scene.load();
    std::ofstream report(scene.root/"decompilation/reports/weapon_switch_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"collected_weapons\": 5,\n  \"original_animation_frame_checks\": "<<frames<<",\n  \"magazine_reload_checks\": "<<reloads<<",\n  \"unowned_selection_blocked\": true,\n  \"forward_backward_wrap\": true,\n  \"pistol_sound_mapping\": true\n}\n";
}
void pickupSelfTest(){
    auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};unsigned totalInitial=0,totalTriggered=0,totalCollected=0,types=0;bool seen[28]{};
    for(auto& path:scene.physicalLevels){scene.level=path;scene.load(true);auto m=scene.mem();unsigned expected=0;for(unsigned i=0;i<m.w(0xa0c1c);++i)expected+=m.b(m.d(0xa0c54)+i*8+6)==0;
        require(scene.pickups.active().size()==expected,"Initial pickup count differs from original spawn rules");totalInitial+=expected;
        for(unsigned i=0;i<m.w(0xa0c1c);++i){auto rule=m.d(0xa0c54)+i*8;if(m.b(rule+6)){scene.pickups.trigger(m.b(rule+6),i);++totalTriggered;}}
        auto list=scene.pickups.active();require(list.size()==m.w(0xa0c1c),"Triggered pickup rule did not spawn exactly one item");
        for(auto item:list){auto kind=m.b(item+32),amount=m.b(item+33),reserve=m.b(item+34);require(scene.pickupMeshes.contains(kind),"Original pickup model is missing");if(!seen[kind]){seen[kind]=true;++types;}
            // Isolate one real record so nearby items cannot mask its award.
            m.d(0x236c44,item);m.d(item+40,0);m.d(item+44,0);m.b(0xa0ab4,0);for(unsigned p=0xa0abe;p<=0xa0ada;p+=2)m.w(p,0);m.w(0xa0ab8,50);m.w(0xa0abc,0);m.w(0xa0ae2,0);m.w(0xa0aba,0);m.w(0xa0ae4,0);m.w(0xa0b30,0);m.d(0xa0c90,0);m.w(item+8,0);m.w(item+10,0);m.w(item+12,0);
            scene.pickups.tick(m.s(item),m.s(item+2),m.s(item+4));require(scene.pickups.active().empty(),"Collectible pickup was not removed by recovered collection");
            if(kind<5)require(m.b(0xa0ab4)&(4u<<kind),"Weapon pickup did not grant ownership");
            if(kind==7)require(m.w(0xa0ad2)==1,"Battery award mismatch");if(kind==8)require(m.w(0xa0ad8)==1,"Night vision award mismatch");if(kind==15)require(m.w(0xa0ae2)==1,"ID badge award mismatch");if(kind==16)require(m.b(0xa0ab4)&128,"Auto mapper award mismatch");if(kind==17)require(m.w(0xa0ab8)==100&&m.w(0xa0ae4)==900,"Hypo award mismatch");if(kind==18||kind==19)require(m.w(0xa0aba)==amount,"Armor award mismatch");if(kind==20)require(m.w(0xa0ab8)==std::min(100u,50u+amount),"Medikit award mismatch");if(kind==21)require(m.w(0xa0ab8)==50+amount,"Dermpatch award mismatch");if(kind==22)require(m.w(0xa0abc)==amount*30,"Boot timer award mismatch");if(kind==23)require(m.w(0xa0ab8)==200,"Adrenaline award mismatch");if(kind==25)require(m.w(0xa0ad4)==1,"Lamp award mismatch");if(kind==9)require(m.w(0xa0abe)==std::min(15u,unsigned(amount))&&m.w(0xa0ac0)==std::min(9u,unsigned(reserve)),"Pistol ammunition award mismatch");++totalCollected;
        }
    }
    scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();auto m=scene.mem();auto items=scene.pickups.active();auto it=std::find_if(items.begin(),items.end(),[&](unsigned p){return m.b(p+32)==20;});require(it!=items.end(),"First-level medikit fixture missing");auto item=*it;m.d(0x236c44,item);m.d(item+40,0);m.d(item+44,0);m.w(0xa0ab8,100);int x=m.s(item),y=m.s(item+2),z=m.s(item+4);scene.pickups.tick(x,y,z);require(scene.pickups.active().size()==1,"Full-health player consumed a medikit");auto amount=m.b(item+33);m.w(0xa0ab8,60);scene.pickups.tick(x,y,z);require(scene.pickups.active().empty()&&m.w(0xa0ab8)==std::min(100,60+int(amount)),"Wounded player did not collect the medikit");auto health=m.w(0xa0ab8);scene.pickups.tick(x,y,z);require(m.w(0xa0ab8)==health,"Removed pickup awarded twice");
    m.w(0xa0ae4,2);m.w(0xa0abc,2);scene.pickups.tick(x,y,z);scene.pickups.tick(x,y,z);require(!m.w(0xa0ae4)&&!m.w(0xa0abc),"Equipment timers froze after all pickups were collected");
    scene.load();m=scene.mem();scene.pickups.clear();unsigned hidden=0;for(unsigned i=0;i<m.w(0xa0c1c);++i)if(m.b(m.d(0xa0c54)+i*8+6)){hidden=i;break;}auto rule=m.d(0xa0c54)+hidden*8;m.b(rule+5,0);m.b(rule+6,2);auto owner=scene.enemies->active().front();m.b(owner+0x72,hidden);require(bool(scene.enemies->droppedItem),"Enemy drop bridge is missing");scene.enemies->droppedItem(owner);require(scene.pickups.active().empty(),"Hidden enemy drop spawned before its threshold");scene.enemies->droppedItem(owner);auto drops=scene.pickups.active();require(drops.size()==1&&m.w(drops[0]+10)==64,"Enemy drop did not use recovered launch physics");auto drop=drops[0];for(unsigned tick=0;tick<160;++tick)scene.pickups.tick(m.s(owner)+5000,m.s(owner+2),m.s(owner+4)+5000);require(scene.pickups.active().size()==1&&!m.w(drop+10),"Uncollected enemy drop did not settle on the floor");
    // 0x80037b98 / 0x800379ac: multiple pickups collected together queue sequentially (up to 4 slots, strlen + 40 ticks each).
    scene.load();m=scene.mem();auto multi=scene.pickups.active();require(multi.size()>=2,"Multi-pickup fixture requires two records");
    auto firstItem=multi[0],secondItem=multi[1];m.d(0x236c44,firstItem);m.d(firstItem+44,0);m.d(firstItem+40,secondItem);m.d(secondItem+44,firstItem);m.d(secondItem+40,0);
    m.b(firstItem+32,10);m.b(firstItem+33,8);m.b(firstItem+34,0);m.b(secondItem+32,9);m.b(secondItem+33,15);m.b(secondItem+34,1);
    m.w(secondItem,m.w(firstItem));m.w(secondItem+2,m.w(firstItem+2));m.w(secondItem+4,m.w(firstItem+4));m.w(firstItem+8,0);m.w(firstItem+10,0);m.w(firstItem+12,0);m.w(secondItem+8,0);m.w(secondItem+10,0);m.w(secondItem+12,0);
    m.w(0xa0ac2,0);m.w(0xa0abe,0);m.w(0xa0ac0,0);
    int mx=m.s(firstItem),my=m.s(firstItem+2),mz=m.s(firstItem+4);
    std::wstring firstText=L"Shotgun cartridges",secondText=L"9mm ammunition clip";
    for(unsigned t=0;t<firstText.size()+39;++t){auto shown=scene.pickups.tick(mx,my,mz);if(t==4||t==23||t==27){scene.notice=shown;scene.noticeTicks=scene.pickups.remainingTicks();preview(scene.root/("decompilation/reports/ps1-pickup-cursor-"+std::to_string(t)+".bmp"));}require(shown==firstText&&scene.pickups.queuedCount()==2&&m.w(0x9c9c8)==2,"First simultaneous pickup text did not stay active for its full PS1 duration");if(t==0)require(scene.pickups.active().empty()&&m.w(0xa0ac2)==8&&m.w(0xa0abe)==15,"Simultaneous pickups did not award both items immediately");}
    for(unsigned t=0;t<secondText.size()+39;++t){auto shown=scene.pickups.tick(mx,my,mz);require(shown==secondText&&scene.pickups.queuedCount()==1&&m.w(0x9c9c8)==1,"Second simultaneous pickup text did not follow the first pickup text");}
    require(scene.pickups.tick(mx,my,mz).empty()&&scene.pickups.queuedCount()==0&&m.w(0x9c9c8)==0,"Queued pickup text did not clear after completion");
    scene.load();auto real=scene.pickups.active();auto visible=std::find_if(real.begin(),real.end(),[&](unsigned p){return m.b(p+32)==7;});require(visible!=real.end(),"Battery visual fixture missing");scene.player.position={short(m.s(*visible)),short(m.s(*visible+2)-64),short(m.s(*visible+4)-1400)};scene.yaw=0;scene.pitch=-350;preview(scene.root/"decompilation/reports/pickup-preview.bmp");scene.load();
    std::ofstream report(scene.root/"decompilation/reports/pickup_playable_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"physical_levels\": 36,\n  \"initial_items\": "<<totalInitial<<",\n  \"triggered_items\": "<<totalTriggered<<",\n  \"native_collection_checks\": "<<totalCollected<<",\n  \"pickup_types\": "<<types<<",\n  \"full_health_gate\": true,\n  \"no_duplicate_awards\": true,\n  \"equipment_timers_expire\": true,\n  \"enemy_drop_threshold_and_landing\": true,\n  \"sequential_pickup_message_queue\": true\n}\n";
}
void alarmSelfTest(){
    auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};scene.level=scene.root/"GameData/PS1/CD/SECT11/L114LEV.MAP";scene.load();auto m=scene.mem();require(scene.level.stem()=="L113LEV"&&scene.lighting.active,"Level 3 variant did not select the shared level");require(scene.physicalLevels.size()==36&&scene.levels.size()==34,"Lighting variants changed physical asset coverage or were left in level navigation");
    scene.player.health=73;m.w(0xa0ab8,73);m.w(0xa0aba,11);m.w(0xa0ad2,3);scene.hud.ammo=42;auto entitySystem=scene.enemies.get();auto panelCount=scene.logic.panels.size(),vertexCount=scene.vertices.size();auto dataPointer=m.d(0xa0c40);std::array<unsigned long long,3> hashes{};unsigned alarms=0;
    for(unsigned tick=0;tick<1800;++tick){if(tick==0||tick==90||tick==180){unsigned phase=tick/90;require(scene.lighting.phase==phase,"Alarm lighting failed to advance every three seconds");unsigned long long hash=1469598103934665603ull;for(auto& face:scene.faces)for(auto channel:scene.lighting.color(face.sourceFace,face.light)){hash^=channel;hash*=1099511628211ull;}hashes[phase]=hash;preview(scene.root/("decompilation/reports/level3-lighting-"+std::to_string(phase)+".bmp"));}
        int sound=scene.lighting.tick();if(sound>=0)++alarms;require(scene.enemies.get()==entitySystem&&scene.vertices.size()==vertexCount&&scene.logic.panels.size()==panelCount&&m.d(0xa0c40)==dataPointer&&scene.player.health==73&&m.w(0xa0aba)==11&&m.w(0xa0ad2)==3&&scene.hud.ammo==42,"Lighting swap reset live gameplay state");
    }
    require(hashes[0]!=hashes[1]&&hashes[1]!=hashes[2]&&hashes[0]!=hashes[2],"Original lighting banks resolve to identical colours");require(!scene.lighting.remaining&&scene.lighting.elapsed==1800&&scene.lighting.changes==19&&alarms==20,"Sixty-second alarm sequence timing mismatch");for(unsigned i=0;i<120;++i)require(scene.lighting.tick()==-1&&!scene.lighting.remaining,"Expired alarm sequence restarted");
    for(unsigned i=0;i<3;++i)require(scene.lighting.matchedFaces(i)>scene.faces.size()*98/100,"Lighting bank did not match original face geometry");scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();require(!scene.lighting.active,"Alarm sequence leaked into another level");
    std::ofstream report(scene.root/"decompilation/reports/alarm_lighting_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"physical_map_assets\": 36,\n  \"navigable_levels\": 34,\n  \"lighting_banks\": 3,\n  \"countdown_ticks\": 1800,\n  \"lighting_changes\": 19,\n  \"gameplay_state_preserved\": true,\n  \"scope\": \"User-requested 60-second alarm presentation; original mission failure/transition trigger is not yet recovered.\"\n}\n";
}
void dayOneSelfTest(){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    // A raised three-cell box, with a real recovered facehugger actor.
    auto fixture=[&](unsigned boxUnits=24){scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();auto m=scene.mem();unsigned width=m.w(0xa0c30),height=m.w(0xa0c32),grid=m.d(0xa0c40);
        for(unsigned z=0;z<height;++z)for(unsigned x=0;x<width;++x){auto p=grid+(z*width+x)*16;for(unsigned j=4;j<16;++j)m.b(p+j,0);m.b(p+7,!x||!z||x+1==width||z+1==height?128:0);m.b(p+10,!x||!z||x+1==width||z+1==height?20:0);m.b(p+13,128);}
        for(unsigned z=7;z<=9;++z)for(unsigned x=7;x<=9;++x)m.b(grid+(z*width+x)*16+11,boxUnits);
        m.w(0xa0c1a,1);auto r=m.d(0xa0c48);m.b(r,2);m.b(r+1,8);m.b(r+2,8);m.b(r+4,0);m.b(r+5,255);m.b(r+6,255);m.b(r+7,0);m.b(r+8,0);m.w(r+10,2048);m.w(r+12,8192);m.w(r+14,80);m.w(r+16,4096);m.w(r+18,1536);
        scene.enemies=std::make_unique<ExplorerEnemies>(m,scene.root);scene.enemies->initialize();return scene.enemies->active().front();
    };
    unsigned e=fixture();auto m=scene.mem();Vec3s player{short(m.s(e)),0,short(m.s(e+4)+2000)};int startZ=m.s(e+4);scene.enemies->tick(player,100);
    require(scene.enemies->ledgeLeaps==1&&m.s(e+0x32)>0&&m.w(e+0xc4)==10,"Nearby facehugger did not start its original ledge leap");
    player[2]+=3000;int peak=m.s(e+2),minimum=m.s(e+2);for(unsigned tick=0;tick<80;++tick){scene.enemies->tick(player,100);peak=std::max(peak,m.s(e+2));minimum=std::min(minimum,m.s(e+2));}
    require(m.s(e+4)>startZ+768&&minimum<=32&&peak>800,"Leaping facehugger did not leave the box and land below");
    e=fixture(64);m=scene.mem();player={short(m.s(e)),0,short(m.s(e+4)+2000)};startZ=m.s(e+4);scene.enemies->tick(player,100);require(scene.enemies->ledgeLeaps==1&&m.s(e+0x32)>0,"Facehugger stayed stuck on a tall original-sized box");player[2]+=3000;minimum=m.s(e+2);for(unsigned tick=0;tick<100;++tick){scene.enemies->tick(player,100);minimum=std::min(minimum,m.s(e+2));}require(m.s(e+4)>startZ+768&&minimum<=32,"Facehugger did not leave a 2048-unit box and land");
    e=fixture();m=scene.mem();player={short(m.s(e)),0,short(m.s(e+4)+5000)};scene.enemies->tick(player,100);require(!scene.enemies->ledgeLeaps,"Distant player triggered a ledge leap");
    e=fixture();m=scene.mem();player={short(m.s(e)),0,short(m.s(e+4)+2000)};auto wall=m.d(0xa0c40)+(10*m.w(0xa0c30)+8)*16;m.b(wall+10,20);m.b(wall+7,128);scene.enemies->tick(player,100);require(!scene.enemies->ledgeLeaps,"Facehugger leapt through a wall");
    e=fixture();m=scene.mem();player={short(m.s(e)),0,short(m.s(e+4)+2000)};m.w(e+0x58,0);m.b(e+0x6f,8);scene.enemies->tick(player,100);require(!scene.enemies->ledgeLeaps,"Dead facehugger leapt");
    // Kill through native combat, finish its animation, then step onto the corpse.
    e=fixture();m=scene.mem();auto cell=m.d(e+0x78);m.b(cell+11,0);m.w(e+2,32);m.w(e+0x5a,0);m.w(e+0x4e,0);m.w(e+0x58,16);m.b(e+0x6f,2);startEntityAnimation(m,e,0);
    player={short(m.s(e)),0,short(m.s(e+4)-800)};require(scene.enemies->shoot(player,0,-400,1280*.72),"Acid fixture shot failed");for(unsigned tick=0;tick<120&&m.b(e+0x6f)!=8;++tick)scene.enemies->tick(player,100);scene.enemies->tick(player,100);
    require(m.b(e+0x6f)==8&&m.b(cell+10)==12,"Original corpse did not create its acid terrain");
    scene.doors.entries.clear();scene.logic.panels.clear();scene.pickups.clear();scene.logic.lifts.clear();scene.player.position={short(m.s(e)),0,short(m.s(e+4))};scene.player.currentCell=(cell-m.d(0xa0c40))/16;scene.player.health=100;scene.player.terrainGate=scene.player.damageGate=0;m.w(0xa0ab8,100);m.w(0xa0aba,0);m.w(0xa0b30,0);
    scene.tick(0,0);require(scene.player.health==99&&scene.enemies->acidHits==1,"Walking on a corpse did not apply acid damage");for(unsigned tick=0;tick<10;++tick)scene.tick(0,0);require(scene.player.health==99,"Acid ignored original damage cooldown");for(unsigned tick=0;tick<30;++tick)scene.tick(0,0);require(scene.player.health<99,"Corpse acid stopped damaging after its first tick");
    m.w(0xa0b30,0);m.w(0xa0aba,5);auto health=scene.player.health;scene.tick(0,0);require(scene.player.health==health&&m.w(0xa0aba)==4,"Acid did not respect original armor absorption");
    auto safeCell=cell+16;m.b(safeCell+10,0);m.b(safeCell+11,0);m.b(safeCell+13,128);scene.player.position[0]+=512;scene.player.currentCell=(safeCell-m.d(0xa0c40))/16;m.w(0xa0b30,0);for(unsigned tick=0;tick<35;++tick)scene.tick(0,0);require(scene.player.health==health&&m.w(0xa0aba)==4,"Corpse acid damaged player after leaving the body cell");
    // Dead alien contact must work even when a death bridge never stamps acid.
    m.b(cell+10,0);m.d(m.d(e+0xc0)+4,0);Vec3s corpsePosition{short(m.s(e)),0,short(m.s(e+4))};
    for(unsigned kind=1;kind<=14;++kind){m.b(e+0x74,kind);m.w(e+0x58,0);m.w(e+0x5a,0);m.w(0xa0aba,0);m.w(0xa0abc,0);m.w(0xa0ae4,0);m.w(0xa0b30,0);m.b(cell+10,0);scene.enemies->tick(corpsePosition,100);require((scene.enemies->health()==99)==(kind<=9),"Corpse acid species filter failed");}
    m.b(e+0x74,6);m.w(e+0x58,0);m.w(0xa0b30,0);auto upperFloor=corpsePosition;upperFloor[1]+=512;scene.enemies->tick(upperFloor,100);require(scene.enemies->health()==100,"Alien corpse damaged another floor");
    // Let the recovered attack select its original attach callback, render both
    // source sprite halves, and observe the complete rise/hold/fall/death cycle.
    e=fixture();m=scene.mem();for(unsigned i=0;i<scene.cells.size();++i)m.b(m.d(0xa0c40)+i*16+11,0);m.w(e+2,32);m.w(e+0x5a,0);
    player={short(m.s(e)),0,short(m.s(e+4)+2000)};auto playerCell=m.d(0xa0c40)+(((player[2]+m.w(0xa0c36))>>9)*m.w(0xa0c30)+((player[0]+m.w(0xa0c34))>>9))*16;m.b(playerCell+12,255);for(unsigned tick=0;tick<600&&!scene.enemies->attached();++tick)scene.enemies->tick(player,100);
    require(scene.enemies->attached()&&scene.enemies->attachments==1&&(m.b(0xa0ab5)&1),"Facehugger attack did not enter screen attachment");
    scene.player.position=player;scene.player.health=100;scene.faces.clear();scene.vertices.clear();scene.doors.entries.clear();scene.logic.panels.clear();scene.pickups.clear();scene.logic.lifts.clear();m.w(0xa0aba,5);m.w(0xa0b30,0);
    bool covered=false,falling=false;unsigned attachmentFrames=0,attachmentHealth=100;
    for(unsigned tick=0;tick<240&&scene.enemies->attached();++tick){for(unsigned half=0;half<2;++half){auto& frame=scene.enemies->attachmentSprite(half);require(frame.width&&frame.height&&frame.pixels.size()==frame.width*frame.height,"Original face-covering frame failed to decode");++attachmentFrames;}
        scene.enemies->tick(player,attachmentHealth);attachmentHealth=scene.enemies->health();if(scene.enemies->attached()&&!covered&&scene.enemies->attachmentY()<=120){preview(scene.root/"decompilation/reports/facehugger-attached-preview.bmp");require(scene.lastAttachmentPixels>50000,"Attached facehugger did not cover the view");covered=true;}if(covered&&!falling&&scene.enemies->attached()&&scene.enemies->attachmentY()>=240){preview(scene.root/"decompilation/reports/facehugger-detaching-preview.bmp");falling=true;}
    }
    require(covered&&falling&&!scene.enemies->attached()&&scene.enemies->completedAttachments==1,"Facehugger did not finish falling off");require(!(m.b(0xa0ab5)&1)&&m.d(m.d(e+0xc0)+4)==0x33cc8&&m.s(e+0x58)==0,"Attachment failed to clear player lock or enter native death");require(attachmentHealth<100&&m.w(0xa0aba)==5,"Facehugger attachment did not apply original health-only damage");for(unsigned tick=0;tick<120&&m.b(e+0x6f)!=8;++tick)scene.enemies->tick(player,attachmentHealth);require(m.b(e+0x6f)==8,"Detached hugger did not finish its corpse animation");
    // Tracker filters and orientation use the same handedness as the camera.
    scene.load();m=scene.mem();e=scene.enemies->active().front();std::array<unsigned,1> list{e};Vec3s origin{};m.b(e+0x74,2);m.b(e+0x6f,2);m.w(e,0);m.w(e+4,1024);m.w(e+0x30,0);m.w(e+0x32,0);m.w(e+0x34,0);
    require(ExplorerHud::contacts(m,list,origin,0).empty(),"Tracker showed an idle alien");m.w(e+0x34,32);auto contacts=ExplorerHud::contacts(m,list,origin,0);require(contacts.size()==1&&std::abs(contacts[0][0])<.01&&contacts[0][1]==-4,"Tracker forward direction is wrong");contacts=ExplorerHud::contacts(m,list,origin,1024);require(contacts.size()==1&&contacts[0][0]<-3.99&&std::abs(contacts[0][1])<.01,"Tracker rotation is mirrored");
    m.b(e+0x6f,7);require(ExplorerHud::contacts(m,list,origin,0).empty(),"Tracker showed a dead contact");m.b(e+0x6f,2);m.w(e+4,12289);require(ExplorerHud::contacts(m,list,origin,0).empty(),"Tracker showed an out-of-range contact");m.w(e+4,100);m.b(e+0x74,16);require(ExplorerHud::contacts(m,list,origin,0).empty(),"Tracker showed a vent");m.b(e+0x74,10);m.w(e+0x34,0);m.b(e+0x6f,0);require(ExplorerHud::contacts(m,list,origin,0).size()==1,"Tracker missed colonist exception");m.b(e+0x6f,4);require(ExplorerHud::contacts(m,list,origin,0).empty(),"Tracker missed colonist state exclusion");for(unsigned humanKind:{11u,12u,13u,14u}){m.b(e+0x74,humanKind);m.b(e+0x6f,2);require(ExplorerHud::contacts(m,list,origin,0).empty(),"Tracker showed idle human/synth enemy");for(unsigned activeState:{3u,4u,5u,6u}){m.b(e+0x6f,activeState);auto hc=ExplorerHud::contacts(m,list,origin,0);require(hc.size()==1&&ExplorerHud::onTracker(hc[0])&&scene.hud.trackerSound(unsigned(hc.size()))==22,"Tracker missed active human/synth enemy or ping");}}
    scene.load();unsigned pings=0;for(unsigned tick=0;tick<256;++tick){scene.hud.tick();pings+=scene.hud.takeTrackerPing();}require(pings==3,"Original tracker sweep/ping cadence mismatch");
    scene.hud.load(m,scene.root);m.b(e+0x74,2);m.b(e+0x6f,2);m.w(e,0);m.w(e+4,4096);m.w(e+0x34,48);auto distant=ExplorerHud::contacts(m,list,origin,0);require(distant.size()==1&&ExplorerHud::onTracker(distant[0]),"Moving distant enemy was not on radar");require(scene.hud.trackerSound(unsigned(distant.size()))==22,"Radar contact did not ping immediately at eight cells distance");for(unsigned tick=0;tick<60;++tick){scene.hud.tick();require(scene.hud.trackerSound(1)==22,"Moving radar contact lost its sound request");}for(unsigned tick=0;tick<60;++tick){scene.hud.tick();require(scene.hud.trackerSound(0)!=22,"Tracker emitted a contact ping without a moving contact");}
    auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);unsigned hudChecks=0;
    for(auto dimensions:{std::array<int,2>{640,480},{1100,700},{1280,800}}){auto bitmap=CreateCompatibleBitmap(screen,dimensions[0],dimensions[1]);auto old=SelectObject(dc,bitmap);scene.draw(dc,dimensions[0],dimensions[1]);require(scene.lastWeaponBottom==dimensions[1]-1,"Weapon/hand floats above the bottom edge");require(scene.lastTrackerPixels>1000,"Original tracker artwork was not rendered");++hudChecks;SelectObject(dc,old);DeleteObject(bitmap);}DeleteDC(dc);ReleaseDC(nullptr,screen);
    scene.load();m=scene.mem();bool realContact=false;
    for(auto actor:scene.enemies->active()){if(m.b(actor+0x74)!=2)continue;int gx=(m.s(actor)+scene.player.originX)>>9,gz=(m.s(actor+4)+scene.player.originZ)>>9;
        for(auto delta:{std::array<int,2>{0,-1},{1,0},{0,1},{-1,0}}){int x=gx+delta[0],z=gz+delta[1];if(x<1||z<1||x>=scene.grid.width-1||z>=scene.grid.height-1)continue;auto cell=m.d(0xa0c40)+(z*scene.grid.width+x)*16;if(m.b(cell+7)&128||m.b(cell+12)||m.b(cell+13)-m.b(cell+11)<32||std::abs(m.s(actor+2)-m.b(cell+11)*32)>1024)continue;
            scene.player.position={short(x*512+256-scene.player.originX),short(m.b(cell+11)*32),short(z*512+256-scene.player.originZ)};scene.player.currentCell=z*scene.grid.width+x;for(unsigned tick=0;tick<3&&!scene.enemies->attached();++tick)scene.tick(0,0);if(scene.enemies->attached()){realContact=true;break;}
        }if(realContact)break;
    }require(realContact,"Original first-level facehugger failed to latch during real Scene ticks");
    for(unsigned tick=0;tick<180&&scene.enemies->attached();++tick)scene.tick(0,0);require(!scene.enemies->attached()&&scene.enemies->completedAttachments>0,"Original-map facehugger failed to detach and die");
    scene.load();preview(scene.root/"decompilation/reports/day-one-preview.bmp");
    std::ofstream report(scene.root/"decompilation/reports/day_one_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"corpse_acid\": true,\n  \"acid_cooldown_armor_and_exit\": true,\n  \"facehugger_box_leap_and_landing\": true,\n  \"tall_box_2048_unit_drop\": true,\n  \"far_wall_and_dead_leap_guards\": true,\n  \"tracker_filter_and_orientation_checks\": 8,\n  \"tracker_pings_in_256_ticks\": "<<pings<<",\n  \"hud_resolution_checks\": "<<hudChecks<<",\n  \"attachment_frame_checks\": "<<attachmentFrames<<",\n  \"facehugger_attaches_falls_and_dies\": true,\n  \"unforced_chase_attachment\": true,\n  \"original_map_scene_attachment\": true,\n  \"distant_moving_contact_sound_request\": true\n}\n";
}
void logicSelfTest(){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};scene.load();auto m=scene.mem();
    require(scene.logic.panels.size()==3,"First-level panel catalogue mismatch");
    require(!scene.logic.action(m,scene.doors,2,true),"Powered door bypassed its switch");require(!scene.doors.entries[2].target,"Unpowered door started opening");
    require(scene.logic.action(m,scene.doors,5,true),"Normal power panel failed");require(scene.logic.panelOn(m,30),"Power panel visual state did not change");require(!scene.doors.entries[2].target,"One prerequisite opened a two-step door");
    require(scene.logic.action(m,scene.doors,2,true),"Powered door did not accept use");require(scene.doors.entries[2].target,"Powered door failed to open");
    require(!scene.logic.action(m,scene.doors,8,false),"Walking activated a battery panel");
    require(!scene.logic.action(m,scene.doors,8,true),"Battery panel accepted empty inventory");require(!scene.doors.entries[1].target&&!scene.logic.panelOn(m,42),"Failed battery panel changed state");
    auto items=scene.pickups.active();auto battery=std::find_if(items.begin(),items.end(),[&](unsigned p){return m.b(p+32)==7;});require(battery!=items.end(),"Original battery not spawned");auto address=*battery;int bx=m.s(address),by=m.s(address+2),bz=m.s(address+4);scene.pickups.tick(bx,by,bz);require(m.w(0xa0ad2)==1,"Original battery collection did not award inventory");scene.pickups.tick(bx,by,bz);require(m.w(0xa0ad2)==1,"Battery was collected twice");
    require(scene.logic.action(m,scene.doors,8,true),"Charged battery panel failed");require(m.w(0xa0ad2)==0&&scene.logic.panelOn(m,42)&&scene.doors.entries[1].target,"Battery consumption/panel/door chain mismatch");
    require(!scene.logic.action(m,scene.doors,8,true)&&m.w(0xa0ad2)==0,"Latched panel consumed a second battery");
    scene.load();auto door=scene.doors.entries[0];scene.doors.tick(m,door.x,door.z);require(!scene.doors.entries[0].target&&scene.doors.entries[0].amount==0,"Proximity automatically opened a closed door");
    // Exercise recovered movement through all four door cells, from both sides.
    unsigned collisionChecks=0;std::vector<LevelCell> cells(scene.cells.size());for(auto& c:cells){c.bytes[11]=door.floor/32;c.bytes[13]=door.floor/32+96;}LevelGrid grid{scene.grid.width,scene.grid.height,cells};
    auto install=[&](bool open){for(auto& c:cells)c.bytes[12]=0;for(auto address:door.cells){auto& c=cells[(address-m.d(0xa0c40))/16];c.bytes[7]=open?0:128;c.bytes[10]=open?0:20;c.bytes[13]=open?door.ceiling/32:door.floor/32;}};
    for(bool opened:{false,true})for(int side:{-1,1})for(int lateral:{-768,-256,256,768}){
        install(opened);WorldPlayerState player{};player.originX=scene.player.originX;player.originZ=scene.player.originZ;player.health=100;player.terrainGate=0;
        player.position={short(door.x+side*850),short(door.floor),short(door.z+lateral)};player.currentCell=grid.index(player.position[0]+player.originX,player.position[2]+player.originZ);
        player.moveAngles={0,short(side>0?1024:3072),2048};player.localMovement={0,0,100};
        for(unsigned tick=0;tick<24;++tick)movePlayer(player,grid,scene.sine,[](unsigned,unsigned,unsigned,WorldPlayerState&,LevelGrid&){});
        require(opened?(side*(player.position[0]-door.x)<0):(side*(player.position[0]-door.x)>0),opened?"Open door blocked recovered player movement":"Closed door permitted recovered player movement");++collisionChecks;
    }
    scene.load();preview(scene.root/"decompilation/reports/door-closed-preview.bmp");
    auto panel=scene.logic.panels.front();scene.player.position={short(panel.x),short(panel.y),short(panel.z-800)};scene.yaw=0;scene.pitch=0;preview(scene.root/"decompilation/reports/panel-preview.bmp");
    scene.logic.use(m,scene.doors,panel.x,panel.y,panel.z+800,2048);require(!scene.logic.panelOn(m,panel.index),"Panel activated through its back wall");
    scene.logic.use(m,scene.doors,scene.player.position[0],scene.player.position[1],scene.player.position[2],scene.yaw);require(scene.logic.panelOn(m,panel.index),"Visible panel did not respond to E use");preview(scene.root/"decompilation/reports/panel-active-preview.bmp");
    unsigned liftChecks=0;for(auto& path:scene.levels){scene.level=path;scene.load();auto found=std::find_if(scene.logic.lifts.begin(),scene.logic.lifts.end(),[](const auto& lift){return lift.low!=lift.high&&lift.width&&lift.height;});if(found==scene.logic.lifts.end())continue;auto& lift=*found;lift.hold=lift.remaining=0;lift.threshold=1;lift.targetHigh=!lift.atHigh;lift.moving=true;int destination=lift.targetHigh?lift.high:lift.low;
        WorldPlayerState rider=scene.player;rider.position={short(lift.x),short(lift.y),short(lift.z)};rider.localMovement={0,0,0};rider.impulse=0;rider.currentCell=scene.grid.index(lift.x+rider.originX,lift.z+rider.originZ);
        for(unsigned tick=0;tick<260&&lift.moving;++tick){scene.logic.tick(m,scene.doors,-int(rider.originX)-512,0,-int(rider.originZ)-512);for(unsigned i=0;i<scene.cells.size();++i)for(unsigned j=0;j<16;++j)scene.cells[i].bytes[j]=m.b(m.d(0xa0c40)+i*16+j);movePlayer(rider,scene.grid,scene.sine,[](unsigned,unsigned,unsigned,WorldPlayerState&,LevelGrid&){});require(rider.position[1]==lift.y,"Elevator moved without carrying the player floor");++liftChecks;}
        require(lift.y==destination&&!lift.moving,"Original elevator failed to reach its target floor");break;
    }require(liftChecks>0,"No moving original elevator fixture found");scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();
    std::ofstream report(scene.root/"decompilation/reports/door_panel_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"original_level\": \"L111LEV.MAP\",\n  \"panels\": 3,\n  \"movement_collision_checks\": "<<collisionChecks<<",\n  \"lift_rider_ticks\": "<<liftChecks<<",\n  \"power_dependency\": true,\n  \"battery_collection\": true,\n  \"battery_required\": true,\n  \"battery_consumed_once\": true,\n  \"proximity_bypass_blocked\": true\n}\n";
}
bool allLevelsSelfTest(){
    auto paths=scene.physicalLevels;unsigned passed=0,totalDoors=0,totalPages=0,totalMissing=0,mixedSamples=0,musicTracks=0,totalPanels=0,totalBatteryPanels=0;std::ofstream report(scene.root/"decompilation/reports/all_levels_playable_checks.json");report<<"{\n  \"levels\": [\n";bool first=true;
    auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);auto bitmap=CreateCompatibleBitmap(screen,800,500);auto old=SelectObject(dc,bitmap);
    for(auto& path:paths){std::string error;unsigned doorCount=0,pages=0,slots=0,missing=0;
        try{scene.level=path;scene.load(true);auto m=scene.mem();if(scene.textures.pages.empty()||scene.textures.rects.empty())throw std::runtime_error("No level textures");pages=unsigned(scene.textures.pages.size());doorCount=unsigned(scene.doors.entries.size());slots=scene.audio->slots();musicTracks+=scene.audio->musicAvailable();if(!mixedSamples){mixedSamples=scene.audio->validateMixer();scene.audio->writeDiagnostic(scene.root/"decompilation/reports/sfx-diagnostic.wav");}missing=scene.audio->missingSamples();if(!slots||scene.audio->ps1PermanentSamples()!=41||(path.stem()!="L361LEV"&&!scene.audio->ps1LevelSamples()))throw std::runtime_error("PS1 sound bank not loaded for level");
            scene.hud.fire();if(scene.hud.ammo!=14)throw std::runtime_error("HUD ammunition did not change");for(unsigned tick=0;tick<40;++tick){scene.hud.tick();scene.hud.weapon();}scene.hud.reload();for(unsigned tick=0;tick<30;++tick)scene.hud.tick();if(scene.hud.ammo!=15)throw std::runtime_error("HUD reload did not reset ammunition");
            for(unsigned tick=0;tick<4;++tick)scene.tick(0,0);scene.draw(dc,800,500);GdiFlush();
            for(auto& door:scene.doors.entries){door.target=true;door.remaining=90;for(unsigned tick=0;tick<20;++tick)scene.doors.tick(m,door.x+1600,door.z+1600);if(door.amount<.99f||m.b(door.cell+10)!=0)throw std::runtime_error("Door did not open collision cell");door.target=false;for(unsigned tick=0;tick<20;++tick)scene.doors.tick(m,door.x+1600,door.z+1600);if(door.amount>.01f||m.b(door.cell+10)!=20)throw std::runtime_error("Door did not close collision cell");}
            for(auto& panel:scene.logic.panels){auto action=scene.logic.panelAction(m,panel.index);if(!action)throw std::runtime_error("Panel has no original action chain");bool battery=panel.kind==22||panel.kind==26;m.w(0xa0ad2,10);for(auto& lift:scene.logic.lifts)if(lift.moving||lift.remaining||lift.progress>=lift.threshold){lift.moving=false;lift.remaining=0;lift.progress=0;}bool alreadyOn=scene.logic.panelOn(m,panel.index);auto activatedBefore=scene.logic.activated;scene.logic.action(m,scene.doors,action,true);if(alreadyOn){}else if(scene.logic.activated==activatedBefore||(!scene.logic.panelOn(m,panel.index)&&battery))throw std::runtime_error("Original panel action failed: object "+std::to_string(panel.index)+" action "+std::to_string(action));if(m.w(0xa0ad2)!=(battery&&!alreadyOn?9:10))throw std::runtime_error("Panel battery classification/consumption mismatch");++totalPanels;totalBatteryPanels+=battery;}
            ++passed;totalDoors+=doorCount;totalPages+=pages;totalMissing+=missing;
        }catch(const std::exception& e){error=e.what();std::replace(error.begin(),error.end(),'"','\'');std::replace(error.begin(),error.end(),'\\','/');}
        if(!first)report<<",\n";first=false;report<<"    {\"level\":\""<<path.stem().string()<<"\",\"status\":\""<<(error.empty()?"passed":"failed")<<"\",\"texture_pages\":"<<pages<<",\"doors\":"<<doorCount<<",\"sound_slots\":"<<slots<<",\"missing_samples\":"<<missing<<",\"error\":\""<<error<<"\"}";
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);report<<"\n  ],\n  \"status\":\""<<(passed==paths.size()?"passed":"failed")<<"\",\n  \"passed_levels\":"<<passed<<",\n  \"total_levels\":"<<paths.size()<<",\n  \"door_open_close_checks\":"<<totalDoors*2<<",\n  \"panel_activation_checks\": "<<totalPanels<<",\n  \"battery_panel_checks\": "<<totalBatteryPanels<<",\n  \"texture_pages\":"<<totalPages<<",\n  \"missing_sound_samples\":"<<totalMissing<<",\n  \"music_tracks_present\":"<<(musicTracks?"true":"false")<<",\n  \"mixed_nonzero_audio_samples\": "<<mixedSamples<<",\n  \"scope\":\"Host load/render/door/audio-manifest checks; not full-level gameplay completion or original-routine comparisons.\"\n}\n";return passed==paths.size();
}
}
void eggSelfTest(){
 auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};unsigned maps=0,eggs=0,linked=0,empty=0,hatches=0,emptyChecks=0,killedChecks=0;
 std::ofstream audit(scene.root/"decompilation/reports/egg_spawn_audit.json");audit<<"{\n  \"levels\":[";bool first=true;
 for(auto path:scene.physicalLevels){scene.level=path;scene.load(true);auto m=scene.mem();auto base=m.d(0x2365d4);unsigned count=m.w(0x2365f0);std::vector<unsigned char> original(m.bytes.begin()+base,m.bytes.begin()+base+count*20);std::vector<unsigned> records;for(unsigned i=0;i<count;++i)if(original[i*20]==1)records.push_back(i);++maps;if(records.empty())continue;
 if(!first)audit<<",";first=false;audit<<"\n    {\"level\":\""<<path.stem().string()<<"\",\"eggs\":[";bool firstEgg=true;
 auto fixture=[&](unsigned index,unsigned difficulty){std::copy(original.begin(),original.end(),m.bytes.begin()+base);for(unsigned i=0;i<count;++i){m.b(base+i*20+7,0);m.b(base+i*20+8,1);}scene.enemies->initialize(difficulty);scene.enemies->trigger(1,index);auto active=scene.enemies->active();require(active.size()==1&&m.b(active[0]+0x74)==1,"Isolated original egg failed to spawn");return active[0];};
 for(auto index:records){++eggs;unsigned link=original[index*20+6];bool canHatch=link!=255;if(canHatch){require(link<count&&original[link*20]==2,"Egg link is not an original facehugger record");++linked;}else ++empty;
 if(!firstEgg)audit<<",";firstEgg=false;audit<<"{\"record\":"<<index<<",\"facehugger_record\":"<<(canHatch?int(link):-1)<<",\"initial_trigger\":"<<unsigned(original[index*20+7])<<",\"required_trigger\":"<<unsigned(original[index*20+8])<<"}";
 for(unsigned difficulty=0;difficulty<3;++difficulty){auto e=fixture(index,difficulty);Vec3s distant{short(m.s(e)+8192),short(m.s(e+0x5a)),short(m.s(e+4))};for(unsigned t=0;t<96;++t)scene.enemies->tick(distant,100);require(scene.enemies->active().size()==1&&m.b(e+0x72)==link,"Distant egg released its contents prematurely");Vec3s nearEgg{short(m.s(e)),short(m.s(e+0x5a)),short(m.s(e+4))};for(unsigned t=0;t<320&&m.b(e+0x6f)!=8;++t){scene.enemies->tick(nearEgg,100);scene.enemies->sprite(e);}require(m.b(e+0x6f)==8&&m.b(e+0x72)==255,"Nearby egg did not complete original hatching sequence");unsigned children=0;for(auto child:scene.enemies->active())if(m.b(child+0x74)==2&&m.b(child+0x77)==link)++children;require(children==(canHatch?1u:0u),"Egg spawned contents different from original link");for(unsigned t=0;t<80;++t)scene.enemies->tick(nearEgg,100);unsigned later=0;for(auto child:scene.enemies->active())if(m.b(child+0x74)==2&&m.b(child+0x77)==link)++later;require(later==children,"Egg spawned a second facehugger");if(canHatch)++hatches;else ++emptyChecks;}
 auto e=fixture(index,1);m.w(e+0x4e,0);damageEntity(m,e,4096,0,withCombatRuntime([](auto&,unsigned,unsigned,unsigned,unsigned){return PlayerControlResult{};}));Vec3s p{short(m.s(e)),short(m.s(e+0x5a)),short(m.s(e+4))};for(unsigned t=0;t<120;++t)scene.enemies->tick(p,100);require(m.b(e+0x6f)==8&&scene.enemies->active().size()==1,"Destroyed egg released a facehugger");++killedChecks;
 }
 audit<<"]}";
 }
 audit<<"\n  ],\n  \"maps\":"<<maps<<",\n  \"eggs\":"<<eggs<<",\n  \"linked_facehuggers\":"<<linked<<",\n  \"empty_eggs\":"<<empty<<"\n}\n";require(maps==36&&eggs>0&&linked>0,"Egg audit did not cover supplied maps");
 std::ofstream out(scene.root/"decompilation/reports/egg_checks.json");out<<"{\n  \"status\":\"passed\",\n  \"maps\":"<<maps<<",\n  \"eggs\":"<<eggs<<",\n  \"linked_eggs\":"<<linked<<",\n  \"empty_eggs\":"<<empty<<",\n  \"hatches_all_difficulties\":"<<hatches<<",\n  \"empty_egg_checks\":"<<emptyChecks<<",\n  \"destroyed_before_hatching_checks\":"<<killedChecks<<"\n}\n";
}
void lateColonySelfTest(){
 std::vector<fs::path> queenPaths;{std::ofstream audit(scene.root/"decompilation/reports/queen_exit_audit.txt");for(auto path:scene.physicalLevels){scene.level=path;scene.load(true);auto m=scene.mem();for(auto e:scene.enemies->active())if(m.b(e+0x74)==7){queenPaths.push_back(path);audit<<path.stem().string()<<" queen link "<<unsigned(m.b(e+0x72))<<"\n";unsigned index=m.b(e+0x72),budget=100;while(index!=255&&index<m.b(0xa0c2b)&&budget--){auto p=m.d(0xa0c68)+index*4;audit<<index<<": "<<unsigned(m.b(p))<<","<<unsigned(m.b(p+1))<<","<<unsigned(m.b(p+2))<<","<<unsigned(m.b(p+3))<<"\n";index=m.b(p+1);}}}}
 auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};
 auto load=[&](const char* code){scene.level=scene.root/(std::string("GameData/PS1/CD/SECT12/L")+code+"LEV.MAP");scene.load(true);};
 auto impact=[&](unsigned e,unsigned damage){auto m=scene.mem();if(m.b(e+0x74)!=7){m.w(0xa0a48,m.w(e));m.w(0xa0a4a,m.w(e+2));m.w(0xa0a4c,m.s(e+4)-512);m.w(0xa0ba2,0);m.w(0xa0ba8,0);}resetProjectilePool(m);auto p=acquireProjectile(m),pos=m.d(p),vel=m.d(p+4);for(unsigned a=0;a<3;++a){m.w(pos+a*2,m.w(e+a*2));m.w(vel+a*2,0);}m.w(pos+2,m.s(pos+2)+(m.b(e+0x74)==10?704:((m.b(e+0x6c)&8)?-128:128)));m.w(p+26,damage);m.b(p+32,0);m.b(p+28,10);m.b(p+29,10);m.b(p+30,0);m.d(p+12,m.d(e+0x78));m.w(e+0x4e,0);activateProjectile(m,p);scene.enemies->tickProjectiles();};
 unsigned queenExits=0;require(queenPaths.size()==3,"Queen-map audit did not cover all three battles");
 for(auto path:queenPaths){scene.level=path;scene.load(true);auto m=scene.mem();unsigned queen=0;for(auto e:scene.enemies->active())if(m.b(e+0x74)==7)queen=e;require(queen&&bool(scene.enemies->levelAction),"Queen death command bridge missing");require(!scene.doors.entries.at(1).target,"Queen exit opened before boss death");Vec3s nearQueen{short(m.s(queen)+1024),short(m.s(queen+0x5a)),short(m.s(queen+4))};for(unsigned i=0;i<65;++i)scene.enemies->tick(nearQueen,100);impact(queen,16000);require(m.s(queen+0x58)<=0&&m.b(queen+0x6f)==7,"Queen projectile death fixture failed");
 for(unsigned i=0;i<180;++i){scene.enemies->tick(nearQueen,100);scene.enemies->sprite(queen);scene.doors.tick(m,-32768,-32768);}require(scene.enemies->effects[0x406ec]==1&&m.b(queen+0x72)==255,"Queen death commands did not run exactly once");require(scene.doors.entries.at(1).target&&scene.doors.entries.at(1).amount>0,"Queen death did not automatically open exit lift");++queenExits;}
 load("162");auto m=scene.mem();unsigned queen=0;for(auto e:scene.enemies->active())if(m.b(e+0x74)==7)queen=e;require(queen,"Queen missing from original lair");
 Vec3s target{short(m.s(queen)+4000),short(m.s(queen+0x5a)),short(m.s(queen+4))};
 for(unsigned i=0;i<65;++i)scene.enemies->tick(target,100);auto originX=m.s(queen),originZ=m.s(queen+4);auto hp=m.s(queen+0x58);impact(queen,32);require(m.s(queen+0x58)<hp&&m.b(queen+0x6f)==6,"Projectile did not wake dormant Queen through pain entry");
 unsigned queenFrames=0;for(unsigned i=0;i<360;++i){scene.enemies->tick(target,100);scene.enemies->sprite(queen);++queenFrames;}
 require(m.d(m.d(queen+0xc0)+12)==3,"Queen did not finish detachment");require(std::hypot(double(m.s(queen)-originX),double(m.s(queen+4)-originZ))>256,"Detached Queen did not chase player");
 load("161");m=scene.mem();std::vector<unsigned> ceilingRecords,colonistRecords;for(auto e:scene.enemies->active()){auto k=m.b(e+0x74);if(k==8||k==9)ceilingRecords.push_back(m.b(e+0x77));if(k==10)colonistRecords.push_back(m.b(e+0x77));}
 unsigned ceilingFrames=0;for(auto record:ceilingRecords){load("161");m=scene.mem();unsigned e=0;for(auto candidate:scene.enemies->active())if(m.b(candidate+0x77)==record)e=candidate;require(e,"Ceiling fixture record missing");auto originalKind=m.b(e+0x74);auto table=m.d(e+0x7c);Vec3s p{short(m.s(e)+512),short(m.s(e+0x5a)),short(m.s(e+4))};scene.enemies->tick(p,100);impact(e,4096);require(m.b(e+0x6f)==7&&m.d(e+0x7c)==table&&m.b(e+0x74)==(originalKind==8?6:5),"Ceiling death lost original animation table");
 for(unsigned i=0;i<120;++i){scene.enemies->tick(p,100);scene.enemies->sprite(e);++ceilingFrames;}require(m.b(e+0x6f)==8&&(m.b(e+0xb6)&4),"Ceiling alien did not reach terminal corpse frame");require(m.s(e+2)==m.s(e+0x5a)+32,"Ceiling corpse did not settle onto floor");}
 unsigned tagDrops=0;for(auto record:colonistRecords){load("161");m=scene.mem();unsigned e=0;for(auto candidate:scene.enemies->active())if(m.b(candidate+0x77)==record)e=candidate;require(e,"Colonist fixture record missing");auto before=scene.pickups.active().size();auto count=m.w(0xa0ae6+20);impact(e,4096);require(m.s(e+0x58)<=0&&m.b(e+0x6f)==7,"Colonist could not be killed by player projectile");require(m.w(0xa0ae6+20)==count+1,"Colonist death not counted");auto list=scene.pickups.active();require(list.size()==before+1,"Colonist did not release linked pickup");unsigned drop=0;for(auto p:list)if(m.b(p+32)==15)drop=p;require(drop,"Colonist drop is not an identity tag");
 Vec3s distantPosition{short(m.s(e)+4096),short(m.s(e+2)),short(m.s(e+4))};for(unsigned i=0;i<150;++i){scene.enemies->tick(distantPosition,100);scene.enemies->sprite(e);scene.pickups.tick(distantPosition[0],distantPosition[1],distantPosition[2]);}require(m.b(e+0x6f)==8,"Colonist death animation never completed");auto tags=m.w(0xa0ae2);scene.pickups.tick(m.s(drop),m.s(drop+2),m.s(drop+4));require(m.w(0xa0ae2)==tags+1,"Dropped colonist identity tag could not be collected");++tagDrops;}
 load("154");m=scene.mem();auto& lift=scene.logic.lifts.at(1);auto& paired=scene.logic.lifts.at(8);int x=lift.x+512,z=lift.z+1536;scene.logic.tick(m,scene.doors,x,lift.y,z);require(!lift.moving&&!paired.moving,"Lift activated before its panel prerequisite");require(scene.logic.action(m,scene.doors,13,true),"Level 7 lift panel failed");scene.logic.tick(m,scene.doors,x,lift.y,z);require(lift.moving&&paired.moving,"Failed walk-on trigger latched and trapped player after panel use");
 WorldPlayerState rider=scene.player;rider.position={short(x),short(lift.y),short(z)};rider.currentCell=scene.grid.index(x+rider.originX,z+rider.originZ);rider.localMovement={0,0,0};rider.impulse=0;unsigned liftTicks=0;while(lift.moving&&liftTicks<160){scene.logic.tick(m,scene.doors,x,rider.position[1],z);for(unsigned i=0;i<scene.cells.size();++i)for(unsigned j=0;j<16;++j)scene.cells[i].bytes[j]=m.b(m.d(0xa0c40)+i*16+j);movePlayer(rider,scene.grid,scene.sine,[](unsigned,unsigned,unsigned,WorldPlayerState&,LevelGrid&){});require(rider.position[1]==lift.y,"Level 7 lift did not carry player");++liftTicks;}require(lift.atHigh&&paired.atHigh&&rider.position[1]==lift.high,"Level 7 lift failed to deliver player to upper floor");
 std::ofstream out(scene.root/"decompilation/reports/late_colony_checks.json");out<<"{\n  \"status\":\"passed\",\n  \"queen_detaches_and_chases_after_projectile\":true,\n  \"queen_battles_auto_open_exit\":"<<queenExits<<",\n  \"queen_animation_frames\":"<<queenFrames<<",\n  \"ceiling_deaths\":"<<ceilingRecords.size()<<",\n  \"ceiling_animation_frames\":"<<ceilingFrames<<",\n  \"colonist_identity_tags_killed_dropped_collected\":"<<tagDrops<<",\n  \"level7_lift_carries_player_ticks\":"<<liftTicks<<"\n}\n";
}
void performance4kSelfTest(bool compact){WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"Trilogy4kPerformance";RegisterClassW(&wc);RECT rect{0,0,3840,2160};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);auto window=CreateWindowW(wc.lpszClassName,L"4K performance check",WS_OVERLAPPEDWINDOW,0,0,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,wc.hInstance,nullptr);ExplorerGpu gpu;if(!gpu.initialize(window,scene.root,0,false))throw std::runtime_error("4K benchmark GPU unavailable");scene.menu.open=false;scene.mission.briefing=false;std::ofstream report(scene.root/(compact?"decompilation/reports/performance4k_after.json":"decompilation/reports/performance4k_before.json"));report<<"{\"renderer\":\""<<gpu.name()<<"\",\"results\":[";bool first=true;for(auto size:{std::array<unsigned,2>{1280,720},{3840,2160}}){unsigned w=size[0],h=size[1],ow=w,oh=h;if(compact&&h>480){oh=480;ow=unsigned(std::lround(double(w)*oh/h));}double begin=0,draw=0,finish=0;for(unsigned i=0;i<40;++i){auto a=renderClock();auto dc=gpu.begin(w,h,ow,oh);auto b=renderClock();scene.draw(dc,ow,oh,&gpu);auto c=renderClock();gpu.finish(false);auto d=renderClock();if(i>=10){begin+=b-a;draw+=c-b;finish+=d-c;}}if(!first)report<<",";first=false;report<<"{\"width\":"<<w<<",\"height\":"<<h<<",\"begin_ms\":"<<begin/30<<",\"draw_ms\":"<<draw/30<<",\"finish_ms\":"<<finish/30<<",\"total_ms\":"<<(begin+draw+finish)/30<<"}";}report<<"]}";}
void antialiasingSettingsSelfTest(){ExplorerMenu menu;auto fixture=scene.root/"decompilation/reports/msaa_settings_fixture";menu.load(scene.root,fixture);menu.page="graphics";menu.selection=8;menu.renderer=0;menu.antialiasing=0;for(unsigned expected:{2u,4u,8u,0u}){menu.key(VK_RIGHT);if(menu.antialiasing!=expected)throw std::runtime_error("MSAA chooser order failed");}menu.antialiasing=4;menu.persist();ExplorerMenu restored;restored.load(scene.root,fixture);if(restored.antialiasing!=4)throw std::runtime_error("MSAA setting did not persist");menu.antialiasingModes={0,4};menu.antialiasing=0;menu.key(VK_RIGHT);if(menu.antialiasing!=4)throw std::runtime_error("MSAA chooser failed to skip unsupported modes");menu.key(VK_RIGHT);if(menu.antialiasing!=0)throw std::runtime_error("Restricted MSAA chooser wrap failed");menu.renderer=3;menu.key(VK_RIGHT);if(menu.antialiasing!=0)throw std::runtime_error("Software renderer exposed MSAA");scene.menu=restored;scene.menu.open=true;scene.menu.page="graphics";scene.menu.selection=8;preview(scene.root/"decompilation/reports/msaa-options-preview.bmp");std::ofstream(scene.root/"decompilation/reports/msaa_settings_checks.json")<<"{\"status\":\"passed\",\"modes\":4,\"unsupported_modes_skipped\":true,\"settings_persisted\":true}";}
void monitorDisplaySelfTest(){
 auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};std::vector<ExplorerMonitor> synthetic{{"primary",L"Primary",{0,0,1920,1080},{0,0,1920,1040},true},{"left",L"Left",{-2560,0,0,1440},{-2560,0,0,1400},false}};auto rectangle=explorerWindowBounds(synthetic[1],3840,2160,false);require(rectangle.left>=-2560&&rectangle.top>=0&&rectangle.right<=0&&rectangle.bottom<=1400,"Windowed bounds escape negative-coordinate monitor");auto borderless=explorerWindowBounds(synthetic[1],1280,720,true);require(EqualRect(&borderless,&synthetic[1].area),"Borderless bounds depend on render resolution");require(explorerMonitorIndex(synthetic,"left")==1,"Saved monitor identity not found");std::reverse(synthetic.begin(),synthetic.end());require(explorerMonitorIndex(synthetic,"left")==0&&explorerMonitorIndex(synthetic,"disconnected")==1,"Monitor reorder/disconnect fallback failed");
 auto fixture=scene.root/"decompilation/reports/monitor_settings_fixture";scene.menu.load(scene.root,fixture);auto monitors=scene.menu.monitors;require(!monitors.empty(),"No display fallback available");WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TrilogyMonitorCheck";RegisterClassW(&wc);auto window=CreateWindowW(wc.lpszClassName,L"Monitor check",WS_OVERLAPPEDWINDOW,0,0,1280,800,nullptr,nullptr,wc.hInstance,nullptr);require(window,"Display test window unavailable");scene.menu.resolution=0;unsigned checked=0;for(auto& monitor:monitors){scene.menu.monitorDevice=monitor.id;scene.menu.displayMode=ExplorerMenu::Borderless;applyDisplay(window);RECT actual{};GetWindowRect(window,&actual);require(EqualRect(&actual,&monitor.area),"Borderless window did not cover selected monitor");require(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CAPTION),"Borderless window retained a caption");require(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST),"Borderless window became topmost");scene.menu.displayMode=ExplorerMenu::Windowed;applyDisplay(window);GetWindowRect(window,&actual);require(actual.left>=monitor.work.left&&actual.top>=monitor.work.top&&actual.right<=monitor.work.right&&actual.bottom<=monitor.work.bottom,"Windowed mode escapes selected work area");require(!IsWindowVisible(window),"Hidden display test unexpectedly appeared");++checked;}
 scene.menu.monitorDevice=monitors.back().id;scene.menu.displayMode=ExplorerMenu::Borderless;scene.menu.persist();ExplorerMenu restored;restored.load(scene.root,fixture);require(restored.monitorDevice==monitors.back().id&&restored.displayMode==ExplorerMenu::Borderless,"Monitor/mode settings did not persist");scene.menu.monitorDevice="disconnected-test-display";applyDisplay(window);auto fallback=explorerMonitorIndex(monitors,scene.menu.monitorDevice);RECT actual{};GetWindowRect(window,&actual);require(EqualRect(&actual,&monitors[fallback].area),"Missing monitor did not fall back to primary");scene.menu.monitorDevice=monitors.front().id;scene.menu.displayMode=ExplorerMenu::Windowed;applyDisplay(window);windowProc(window,WM_SYSKEYDOWN,VK_RETURN,1LL<<29);require((scene.menu.displayMode!=ExplorerMenu::Windowed),"Alt-Enter did not enter borderless mode");windowProc(window,WM_SYSKEYDOWN,VK_RETURN,1LL<<29);require(scene.menu.displayMode==ExplorerMenu::Windowed,"Alt-Enter did not return to windowed mode");DestroyWindow(window);scene.menu.page="graphics";scene.menu.selection=9;scene.menu.open=true;preview(scene.root/"decompilation/reports/monitor-options-preview.bmp");std::ofstream(scene.root/"decompilation/reports/monitor_display_checks.json")<<"{\"status\":\"passed\",\"connected_monitors_checked\":"<<checked<<",\"negative_coordinates\":true,\"identity_persistence\":true,\"disconnect_fallback\":true,\"alt_enter\":true}";
}
void controllerRemapSelfTest(){
 auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};auto fixture=scene.root/"decompilation/reports/controller_remap_fixture";ExplorerMenu menu;menu.load(scene.root,fixture);menu.controllerBindings=ExplorerController::DefaultBindings;menu.controllerListening=0;menu.bindController(32);require(menu.controllerBindings[0]==32&&menu.controllerBindings[1]==33,"Duplicate binding did not swap fire/grenade");menu.controllerSensitivity=8;menu.controllerSensitivityY=4;menu.controllerVibration=30;menu.persist();ExplorerMenu restored;restored.load(scene.root,fixture);require(restored.controllerBindings==menu.controllerBindings&&restored.controllerSensitivity==8&&restored.controllerSensitivityY==4&&restored.controllerVibration==30,"Remapping or independent sensitivities failed to persist");menu.page="controller-bindings";menu.selection=2;menu.key(VK_RETURN);require(menu.controllerListening==2,"Remap row did not listen");menu.key(VK_ESCAPE);require(menu.controllerListening<0&&menu.page=="controller-bindings","Cancel exited instead of canceling binding capture");
 ExplorerController controller;require(controller.initialize(),"SDL initialization failed");struct Motors{unsigned calls=0;Uint16 low=0,high=0;}motors;SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;desc.name="Remapping and rumble test";desc.userdata=&motors;desc.Rumble=[](void* user,Uint16 low,Uint16 high)->bool{auto& m=*static_cast<Motors*>(user);++m.calls;m.low=low;m.high=high;return true;};auto id=SDL_AttachVirtualJoystick(&desc);require(id,"Virtual rumble pad attach failed");auto joystick=SDL_OpenJoystick(id);controller.select(id);controller.configure(menu.controllerBindings);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);SDL_UpdateJoysticks();controller.poll();SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767);SDL_UpdateJoysticks();auto state=controller.poll();require((state.pressed&ExplorerController::Grenade)&&!(state.buttons&ExplorerController::Fire),"Remapped right trigger did not throw grenade");require(state.rawPressed&(1ull<<33),"Trigger unavailable for remap capture");require(controller.rumble(.5f,.75f,100)&&motors.calls&&motors.low>32000&&motors.low<33500&&motors.high>48000&&motors.high<50000,"Rumble motor command failed");controller.poll(.18f,false);require(motors.low==0&&motors.high==0,"Losing focus left motors running");require(controller.rumble(.2f,.4f,75),"Second rumble failed");controller.stopRumble();require(!motors.low&&!motors.high,"Stop rumble failed");SDL_CloseJoystick(joystick);SDL_DetachVirtualJoystick(id);require(!controller.poll().connected&&!controller.rumble(1,1,100),"Disconnected pad accepted vibration");
 menu.page="controller";menu.selection=8;menu.key(VK_RETURN);require(menu.controllerBindings==ExplorerController::DefaultBindings&&menu.controllerVibration==70&&menu.controllerSensitivity==3&&menu.controllerSensitivityY==3,"Controller defaults failed to restore");scene.menu=menu;scene.menu.open=true;preview(scene.root/"decompilation/reports/controller-rumble-options.bmp");scene.menu.page="controller-bindings";scene.menu.selection=0;preview(scene.root/"decompilation/reports/controller-remap-options.bmp");std::ofstream(scene.root/"decompilation/reports/controller_remap_rumble_checks.json")<<"{\"status\":\"passed\",\"remap_swap_and_persistence\":true,\"independent_sensitivities\":true,\"trigger_remap\":true,\"rumble_motors\":true,\"focus_stop\":true,\"disconnect_stop\":true}";
}
void controllerSettingsSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};auto settings=scene.root/"decompilation/reports/controller_settings_fixture";ExplorerMenu menu;menu.load(scene.root,settings);menu.page="graphics";menu.selection=6;menu.frameLimit=30;for(unsigned expected:{60u,120u,144u,175u,0u,30u}){menu.key(VK_RIGHT);require(menu.frameLimit==expected,"Frame-rate chooser order failed");}menu.frameLimit=175;menu.controllerEnabled=true;menu.controllerSensitivity=7;menu.controllerDeadzone=22;menu.controllerInvert=true;menu.vsync=false;menu.persist();ExplorerMenu restored;restored.load(scene.root,settings);require(restored.frameLimit==175&&restored.controllerSensitivity==7&&restored.controllerDeadzone==22&&restored.controllerInvert&&!restored.vsync,"Controller/FPS settings failed to persist");
 unsigned virtualPads=0;for(auto type:{SDL_GAMEPAD_TYPE_XBOXONE,SDL_GAMEPAD_TYPE_PS4,SDL_GAMEPAD_TYPE_PS5}){ExplorerController controller;require(controller.initialize(),"SDL gamepad initialization failed");SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;desc.name=type==SDL_GAMEPAD_TYPE_PS4?"Test DualShock 4":type==SDL_GAMEPAD_TYPE_PS5?"Test DualSense":"Test Xbox One/Series";auto id=SDL_AttachVirtualJoystick(&desc);require(id,"Virtual gamepad attach failed");auto mapping=SDL_GetGamepadMappingForID(id);require(mapping,"Virtual mapping unavailable");std::string typedMapping(mapping);SDL_free(mapping);auto typeAt=typedMapping.find("type:");if(typeAt!=std::string::npos){auto end=typedMapping.find(',',typeAt);typedMapping.erase(typeAt,end==std::string::npos?std::string::npos:end-typeAt+1);}typedMapping+="type:"+std::string(SDL_GetGamepadStringForType(type))+",";require(SDL_SetGamepadMapping(id,typedMapping.c_str()),"Virtual controller family mapping failed");auto joystick=SDL_OpenJoystick(id);require(joystick,"Virtual joystick open failed");controller.select(id);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);SDL_UpdateJoysticks();auto neutral=controller.poll();require(neutral.playstation==(type==SDL_GAMEPAD_TYPE_PS4||type==SDL_GAMEPAD_TYPE_PS5),"Controller prompt family detection failed");require(neutral.connected&&neutral.buttons==0&&neutral.moveX==0&&neutral.lookY==0,"Neutral controller drifts or fires");SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,16384);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,32767);SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_WEST,true);SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_LEFT_STICK,true);SDL_UpdateJoysticks();auto active=controller.poll();require((active.pressed&ExplorerController::Grenade)&&(active.buttons&ExplorerController::Fire)&&(active.pressed&ExplorerController::Use)&&(active.buttons&ExplorerController::Sprint),"Modern FPS trigger/use/sprint mapping failed");require(active.moveX>0&&active.moveX<1&&active.lookX>.99f,"Analog movement/look values lost");auto heldPad=controller.poll();require(!(heldPad.pressed&ExplorerController::Grenade),"Held grenade trigger repeats");auto unfocused=controller.poll(.18f,false);require(!unfocused.buttons&&!unfocused.moveX&&!unfocused.lookX,"Controller leaked input while unfocused");SDL_CloseJoystick(joystick);SDL_DetachVirtualJoystick(id);require(!controller.poll().connected,"Disconnected controller remains active");++virtualPads;}
 require(ExplorerController::stick(1500,-1500,.18f)==std::array<float,2>{0,0},"Stick deadzone failed");auto diagonal=ExplorerController::stick(32767,32767,.18f);require(std::abs(std::hypot(diagonal[0],diagonal[1])-1)<.001,"Diagonal stick exceeds normal speed");
 scene.load();scene.menu.load(scene.root,settings);scene.menu.hasGame=true;scene.menu.open=false;auto m=scene.mem();m.b(0xa0ab4,m.b(0xa0ab4)&127);scene.syncPauseMap();require(!scene.menu.mapAutoMapper&&scene.menu.mapCells.empty(),"Pause map exposed without Auto Mapper");toggleAutoMap(nullptr);require(!scene.menu.open,"M opened unowned map");awardPickup(m,16,1,1);scene.syncPauseMap();require(scene.menu.mapAutoMapper&&!scene.menu.mapCells.empty(),"Collected Auto Mapper did not enable map");require(scene.showAutoMap()&&scene.menu.page=="map","Collected Auto Mapper did not open its map page");scene.menu.page="map";preview(scene.root/"decompilation/reports/owned-automap-preview.bmp");scene.menu=restored;scene.menu.open=true;scene.menu.page="graphics";preview(scene.root/"decompilation/reports/fps-options-preview.bmp");scene.menu.page="controller";preview(scene.root/"decompilation/reports/controller-options-preview.bmp");scene.load();scene.syncPauseMap();require(!scene.menu.mapAutoMapper&&scene.menu.mapCells.empty(),"Auto Mapper carried into a new level");std::ofstream(scene.root/"decompilation/reports/controller_fps_map_checks.json")<<"{\"status\":\"passed\",\"virtual_controller_families\":"<<virtualPads<<",\"fps_presets\":6,\"trigger_edges\":true,\"focus_and_disconnect\":true,\"map_requires_current_level_pickup\":true}";
}
void meetingTowerPanelSelfTest(){
 unsigned checked=0;for(unsigned object:{0u,2u}){scene.level=scene.root/"GameData/PS1/CD/SECT21/L213LEV.MAP";scene.load(true);auto m=scene.mem();auto found=std::find_if(scene.logic.panels.begin(),scene.logic.panels.end(),[&](auto& p){return p.index==object;});if(found==scene.logic.panels.end())throw std::runtime_error("Meeting Tower lower panel missing");auto panel=*found;auto use=[&](){return scene.logic.use(m,scene.doors,panel.x,panel.y,panel.z+128,2048);};auto first=use();if(first!=L"Panel activated"||!scene.logic.panelOn(m,object))throw std::runtime_error("Lower side-lift panel did not activate once");unsigned counter=m.w(0xa0ae6+panel.kind*2),activations=scene.logic.activated;std::vector<unsigned> progress;for(auto& lift:scene.logic.lifts)progress.push_back(lift.progress);for(unsigned i=0;i<10;++i)if(!use().empty())throw std::runtime_error("Used lower panel repeated its inactive message");if(m.w(0xa0ae6+panel.kind*2)!=counter||scene.logic.activated!=activations)throw std::runtime_error("Used panel repeated activation/counter");for(unsigned i=0;i<progress.size();++i)if(scene.logic.lifts[i].progress!=progress[i])throw std::runtime_error("Used panel added lift trigger progress");for(unsigned t=0;t<400;++t)scene.logic.tick(m,scene.doors,-32768,0,-32768);if(!use().empty()||!scene.logic.panelOn(m,object))throw std::runtime_error("Lower panel re-armed after lift cycle");++checked;}
 std::ofstream(scene.root/"decompilation/reports/meeting_tower_panel_checks.json")<<"{\"status\":\"passed\",\"opposite_lower_panels_checked\":"<<checked<<",\"repeat_use_silent\":true,\"activation_counters_unchanged\":true}";
}
void glassSelfTest(unsigned backend){
 auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};scene.menu.open=false;scene.mission.briefing=false;scene.menu.renderQuality=true;scene.player.position={0,0,0};scene.yaw=scene.pitch=0;scene.doors.entries.clear();scene.logic.panels.clear();scene.logic.lifts.clear();scene.objects.entries.clear();scene.objects.effects.clear();scene.pickups.clear();auto m=scene.mem();m.d(0x2365e0,0);resetProjectilePool(m);scene.vertices={{-1024,-256,2048},{1024,-256,2048},{1024,1792,2048},{-1024,1792,2048},{-512,256,1024},{512,256,1024},{512,1280,1024},{-512,1280,1024}};scene.faces.clear();scene.effectTextures.pages[900]=std::vector<unsigned>(65536,0xffff0000);scene.effectTextures.pages[901]=std::vector<unsigned>(65536,0xff0020c0);unsigned red=scene.effectTextures.rects.size();scene.effectTextures.rects.push_back({900,0,0,256,256});unsigned blue=scene.effectTextures.rects.size();scene.effectTextures.rects.push_back({901,0,0,256,256});Face back{};back.vertices={0,1,2,3};back.model=true;back.atlas=4;back.texture=red;Face glass=back;glass.vertices={4,5,6,7};glass.texture=blue;glass.flags=12;scene.faces={back,glass};auto image=scene.root/("decompilation/reports/glass-"+std::to_string(backend)+".bmp");
 if(backend<3){WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TrilogyGlassCheck";RegisterClassW(&wc);RECT r{0,0,1100,700};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);auto window=CreateWindowW(wc.lpszClassName,L"Glass check",WS_OVERLAPPEDWINDOW,0,0,r.right-r.left,r.bottom-r.top,nullptr,nullptr,wc.hInstance,nullptr);auto gpu=std::make_unique<ExplorerGpu>();require(gpu->initialize(window,scene.root,backend,false),"Glass GPU backend unavailable");for(unsigned i=0;i<4;++i){auto dc=gpu->begin(1100,700);scene.draw(dc,1100,700,gpu.get());if(i==2)gpu->screenshot(image);gpu->finish(false);}gpu.reset();DestroyWindow(window);}else preview(image);
 auto bmp=read(image);unsigned offset=54+(350*1100+550)*4;unsigned b=bmp.at(offset),g=bmp.at(offset+1),r=bmp.at(offset+2);require(r>110&&r<145&&g>=12&&g<=20&&b>80&&b<110,"Tinted glass did not blend foreground and background at half strength");
 scene.level=scene.root/"GameData/PS1/CD/SECT21/L212LEV.MAP";scene.load(true);scene.menu.open=false;scene.mission.briefing=false;unsigned panes=0;for(auto& f:scene.faces)if(f.flags==30||f.flags==32||f.flags==34)++panes;require(panes>100,"Section 2 glass flags were not recognized");if(backend==3){auto object=0x320000+65*40;scene.player.position={short(m.s(object)+1600),short(m.s(object+2)),short(m.s(object+4)+1024)};scene.yaw=1024;scene.pitch=0;preview(scene.root/"decompilation/reports/section2-glass-preview.bmp");}std::ofstream(scene.root/("decompilation/reports/glass_"+std::to_string(backend)+"_checks.json"))<<"{\"status\":\"passed\",\"glass_faces\":"<<panes<<",\"foreground_red\":"<<r<<",\"tint_green\":"<<g<<",\"tint_blue\":"<<b<<"}";
}
void section2BarrelSelfTest(){
 std::ofstream log(scene.root/"decompilation/reports/section2-barrel-audit.txt");unsigned checked=0;scene.level=scene.root/"GameData/PS1/CD/SECT21/L211LEV.MAP";scene.load();auto m=scene.mem();auto entries=scene.objects.entries;for(auto i:entries){if(m.b(0x320000+i*40+24)!=23)continue;scene.load();m=scene.mem();auto obj=0x320000+i*40,cell=m.d(obj+20),shot=0x350000u,pos=0x351000u;log<<"object="<<i<<" cell="<<unsigned(m.b(cell+12))<<" index="<<unsigned(m.b(cell+4))<<" floor="<<int(m.b(cell+11))*32<<std::endl;m.d(shot,pos);m.d(shot+12,cell);m.b(shot+30,3);m.w(shot+26,80);m.w(pos,m.w(obj));m.w(pos+2,m.s(obj+2)+256);m.w(pos+4,m.w(obj+4));auto service=[](auto&,auto,auto,auto,auto){return PlayerControlResult{};};auto attr=m.b(cell+10);m.b(shot+30,0);collideProjectileWithMap(m,shot,service);if(m.w(obj+26)!=16)throw std::runtime_error("Pistol changed original barrel immunity");m.b(shot+30,3);if(attr==20){m.w(pos+2,m.s(obj+2)+1024);if(collideProjectileWithMap(m,shot,service)!=0xffffffff||m.w(obj+26)!=16)throw std::runtime_error("Shot above wall-cell barrel damaged it or passed wall");m.w(pos+2,m.s(obj+2)+256);m.d(shot+4,0x351010);for(unsigned a=0;a<3;++a)m.w(0x351010+a*2,0);collideBallisticWithMap(m,shot,service);if(m.s(obj+26)>0)throw std::runtime_error("Grenade missed exposed barrel in wall cell");m.w(obj+26,16);m.b(obj+25,0);m.b(obj+31,0);if(m.b(cell+10)!=20)throw std::runtime_error("Barrel hit removed backing wall");}collideProjectileWithMap(m,shot,[](auto&,auto,auto,auto,auto){return PlayerControlResult{};});for(unsigned t=0;t<24;++t)scene.objects.tick();log<<"removed="<<!scene.objects.visible(i)<<" health="<<m.s(obj+26)<<std::endl;if(scene.objects.visible(i))throw std::runtime_error("Section 2 barrel failed direct shot: "+std::to_string(i));++checked;}std::ofstream(scene.root/"decompilation/reports/section2_barrel_checks.json")<<"{\"status\":\"passed\",\"barrels\":"<<checked<<"}";
}
void deathTimeoutSelfTest(){
 auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};scene.menu.load(scene.root,scene.root/"decompilation/reports/death_timeout_menu_fixture");scene.menu.open=false;scene.mission.briefing=false;scene.player.health=0;scene.mem().w(0xa0ab8,0);scene.firing=scene.grenadeRequested=true;scene.tick(1,1);require(scene.menu.open&&!scene.menu.hasGame&&scene.menu.page=="main","Death did not return to frontend");for(auto& label:scene.menu.labels())require(label!=L"Resume Game","Dead run exposed Resume");require(!scene.firing&&!scene.grenadeRequested&&!scene.exitFading&&!scene.mission.complete,"Death retained active gameplay/exit state");auto pos=scene.player.position;scene.tick(1,1);require(scene.player.position==pos,"Dead run continued moving");
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L113LEV.MAP";scene.load();scene.menu.open=false;scene.menu.hasGame=true;scene.mission.briefing=false;scene.mission.remaining=1;scene.tick(0,0);require(scene.exitFading&&!scene.mission.complete&&scene.player.health,"Timeout failed to start normal living-player exit");for(unsigned i=0;i<25;++i)scene.tick(0,0);require(scene.mission.complete&&scene.menu.page=="results","Timeout failed to show normal assessment");std::ofstream(scene.root/"decompilation/reports/death_timeout_checks.json")<<"{\"status\":\"passed\",\"death_returns_to_main_menu\":true,\"dead_run_cannot_resume\":true,\"original_timeout_completes_level\":true}";
}
void photoDofSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};unsigned w=256,h=128;std::vector<unsigned> pixels(w*h);std::vector<float> depth(w*h,4000);for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){pixels[y*w+x]=((x/2+y/2)&1)?0xffffffff:0xff000000;if(x>=100&&x<156&&y>=40&&y<88)depth[y*w+x]=1000;}
 auto original=pixels;explorerDepthOfField(pixels,w,h,depth,w,h,0);require(pixels==original,"Disabled DOF changed scene");explorerDepthOfField(pixels,w,h,depth,w,h,1);unsigned blurred=0;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){if(x>104&&x<150&&y>44&&y<84)require(pixels[y*w+x]==original[y*w+x],"Software DOF blurred focus subject");else blurred+=(pixels[y*w+x]&255)>0&&(pixels[y*w+x]&255)<255;}require(blurred>10000,"Software DOF did not blur background");require(explorerDofRadius(1000,1000,1,720)==0&&explorerDofRadius(4000,1000,1,720)>explorerDofRadius(4000,1000,.5,720),"DOF strength/focus incorrect");
 scene.menu.load(scene.root,scene.root/"decompilation/reports/dof_settings_fixture");scene.menu.open=false;scene.menu.hasGame=true;scene.mission.briefing=false;scene.enterPhotoMode();require(scene.photoDof==0,"DOF was not off by default");windowProc(nullptr,WM_KEYDOWN,VK_OEM_2,0);require(scene.photoDof==5,"DOF keyboard increment failed");windowProc(nullptr,WM_KEYDOWN,VK_OEM_COMMA,0);require(scene.photoDof==0,"DOF keyboard decrement failed");scene.adjustPhotoDof(200);require(scene.photoDof==100,"DOF upper bound failed");scene.adjustPhotoDof(-1);require(scene.photoDof==0,"DOF lower bound failed");WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TrilogyDofSliderCheck";RegisterClassW(&wc);auto window=CreateWindowW(wc.lpszClassName,L"DOF slider check",WS_OVERLAPPEDWINDOW,0,0,1100,700,nullptr,nullptr,wc.hInstance,nullptr);RECT client{};GetClientRect(window,&client);scene.menu.aspect=0;auto track=scene.photoDofTrack(client.right,client.bottom);scene.photoPointerMode=true;windowProc(window,WM_LBUTTONDOWN,0,MAKELPARAM((track.left+track.right)/2,(track.top+track.bottom)/2));require(scene.photoDof>=49&&scene.photoDof<=51&&scene.photoDofDragging,"Mouse slider click failed");windowProc(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(track.right,(track.top+track.bottom)/2));require(scene.photoDof==100,"Mouse slider drag failed");windowProc(window,WM_LBUTTONUP,0,0);require(!scene.photoDofDragging,"Mouse slider capture was not released");scene.photoPointerMode=false;DestroyWindow(window);scene.photoNoticeTicks=0;scene.photoDof=70;preview(scene.root/"decompilation/reports/photo-dof-ui-preview.bmp");scene.menu.usingController=true;scene.menu.playstationPrompts=true;preview(scene.root/"decompilation/reports/photo-dof-controller-preview.bmp");scene.exitPhotoMode();require(!scene.photoMode,"DOF changed photo exit behaviour");std::ofstream(scene.root/"decompilation/reports/photo_dof_checks.json")<<"{\"status\":\"passed\",\"off_identity\":true,\"focus_preserved\":true,\"background_blurred\":true,\"keyboard_slider_bounds\":true}";
}
void photoFilterSelfTest(){
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};for(unsigned filter=0;filter<ExplorerPhotoFilterNames.size();++filter)for(unsigned value=0;value<256;++value){unsigned color=0x7f000000|(value<<16)|(value<<8)|value;auto out=applyPhotoFilter(color,filter);require((out>>24)==0x7f,"Photo filter changed alpha");if(!filter)require(out==color,"Normal photo filter changed pixels");if(filter==1)require((out>>16&255)==(out>>8&255)&&(out>>8&255)==(out&255),"Noir has colour tint");}
 auto phosphor=applyPhotoFilter(0xff303030,2);require((phosphor>>8&255)>100&&(phosphor>>8&255)>4*(phosphor>>16&255),"Phosphor did not lift dark detail or retain strong green");auto cold=applyPhotoFilter(0xff000000,6),hot=applyPhotoFilter(0xffffffff,6);require((cold&255)>(cold>>16&255)&&(hot>>16&255)>240&&(hot>>8&255)>240,"Thermal palette endpoints incorrect");
 scene.menu.load(scene.root,scene.root/"decompilation/reports/photo_filter_settings_fixture");scene.menu.open=false;scene.menu.hasGame=true;scene.mission.briefing=false;scene.enterPhotoMode();require(scene.photoMode,"Photo filter fixture could not enter photo mode");auto position=scene.player.position;scene.photoShowUi=false;scene.photoCapturing=true;for(unsigned filter=0;filter<ExplorerPhotoFilterNames.size();++filter){scene.photoFilter=filter;preview(scene.root/("decompilation/reports/photo-filter-"+std::to_string(filter)+"-preview.bmp"));}require(scene.player.position==position,"Photo grading moved the player");scene.photoShowUi=true;scene.photoCapturing=false;scene.photoFilter=5;windowProc(nullptr,WM_KEYDOWN,'F',0);require(scene.photoFilter==6,"Keyboard did not cycle to Thermal");windowProc(nullptr,WM_KEYDOWN,'F',0);require(scene.photoFilter==0,"Photo filter cycle did not wrap to Normal");scene.exitPhotoMode();std::ofstream(scene.root/"decompilation/reports/photo_filter_checks.json")<<"{\"status\":\"passed\",\"filters\":7,\"alpha_preserved\":true,\"normal_unchanged\":true,\"thermal_endpoints\":true,\"keyboard_cycle\":true}";
}
void photoModeSelfTest(){
 auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};
 scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";scene.load();
 scene.menu.hasGame=true;
 scene.menu.open=true;
 require(!scene.canEnterPhotoMode(),"Photo mode allowed while menu open");
 scene.menu.open=false;
 scene.mission.briefing=true;
 require(!scene.canEnterPhotoMode(),"Photo mode allowed during mission briefing");
 scene.mission.briefing=false;
 scene.exitFading=true;
 require(!scene.canEnterPhotoMode(),"Photo mode allowed during level exit fading");
 scene.exitFading=false;
 scene.player.health=0;
 require(!scene.canEnterPhotoMode(),"Photo mode allowed when dead");
 scene.player.health=100;
 require(scene.canEnterPhotoMode(),"Photo mode disallowed during active gameplay");
 scene.firing=scene.grenadeRequested=scene.useRequested=true;
 auto origPos=scene.player.position;
 auto origYaw=scene.yaw;
 auto origPitch=scene.pitch;
 scene.enterPhotoMode();
 require(scene.photoMode,"Failed to enter photo mode");
 require(!scene.firing&&!scene.grenadeRequested&&!scene.useRequested,"Entering photo mode failed to clear held inputs");
 require(scene.photoX==origPos[0]&&scene.photoZ==origPos[2],"Free camera X/Z did not match player");
 require(scene.photoYaw==origYaw&&scene.photoPitch==origPitch&&scene.photoRoll==0,"Free camera orientation did not match player");
 require(scene.photoFov>60.0&&scene.photoFov<100.0,"Free camera FOV out of expected default range");
 scene.photoX+=500.0;scene.photoY-=200.0;scene.photoZ+=300.0;
 scene.photoYaw=(scene.photoYaw+600)&4095;scene.photoPitch=250;scene.photoRoll=150;scene.photoFov=55.0;
 require(scene.player.position==origPos,"Player position altered while free camera moved");
 require(scene.yaw==origYaw,"Player yaw altered while free camera turned");
 require(scene.pitch==origPitch,"Player pitch altered while free camera pitched");
 unsigned stepsBefore=scene.steps;
 scene.tick(0,0,false);
 require(scene.steps==stepsBefore+1,"Step-frame tick did not increment steps");
 require(scene.player.position==origPos,"Player moved during zero-movement step-frame");
 uint32_t sample=0xff804020;
 for(unsigned f=0;f<ExplorerPhotoFilterNames.size();++f){
  uint32_t out=applyPhotoFilter(sample,f);
  require((out&0xff000000)!=0,"Filter wiped alpha channel");
  if(f==1){
   uint8_t r=(out>>16)&0xff,g=(out>>8)&0xff,b=out&0xff;
   require(r==g&&g==b,"Noir filter did not produce monochrome grayscale");
  }
 }
 auto shotPath=scene.capturePhoto(nullptr,640,480);
 require(!shotPath.empty()&&fs::exists(shotPath),"capturePhoto failed to create output file");
 auto bmpData=read(shotPath);
 require(bmpData.size()==54+size_t(640)*480*4,"Captured screenshot size mismatch");
 require(bmpData[0]=='B'&&bmpData[1]=='M',"Captured screenshot missing BMP signature");
 require(scene.photoFlashTicks>0,"Capture did not trigger flash ticks");
 scene.photoFilter=1;
 auto shotNoir=scene.capturePhoto(nullptr,640,480);
 require(fs::exists(shotNoir),"Noir photo capture failed");
 auto bmpNoir=read(shotNoir);
 require(bmpNoir.size()==54+size_t(640)*480*4,"Noir screenshot size mismatch");
 scene.photoRoll=512;
 auto shotRoll=scene.capturePhoto(nullptr,640,480);
 require(fs::exists(shotRoll),"Rotated photo capture failed");
 scene.photoRoll=0;
 scene.photoFilter=0;
 scene.firing=scene.grenadeRequested=true;
 scene.exitPhotoMode();
 require(!scene.photoMode,"Failed to exit photo mode");
 require(!scene.firing&&!scene.grenadeRequested,"Exiting photo mode failed to clear held inputs");
 require(scene.player.position==origPos,"Player position changed after photo mode exit");
 require(scene.yaw==origYaw,"Player yaw changed after photo mode exit");
 require(scene.pitch==origPitch,"Player pitch changed after photo mode exit");
 std::ofstream(scene.root/"decompilation/reports/photo_mode_checks.json")<<"{\"status\":\"passed\",\"entry_restrictions_verified\":true,\"free_camera_isolated\":true,\"simulation_frozen\":true,\"step_frame_verified\":true,\"filters_verified\":6,\"screenshot_saved\":true,\"upright_sprites_verified\":true,\"inputs_cleared\":true}";
}

void walkThroughWallSelfTest(){
 auto require=[](bool v,const std::string& why){if(!v)throw std::runtime_error(why);};
 unsigned maps=0,ghostWalls=0,ghostCells=0,solidWalls=0,movementChecks=0;
 auto paths=scene.physicalLevels;
 for(auto path:paths){scene.level=path;scene.load(true);++maps;auto m=scene.mem();bool ghosts=scene.mission.sourceCode=="161";
  for(auto i:scene.objects.entries){auto p=0x320000+i*40;if(m.b(p+24)!=21)continue;unsigned base=m.d(p+20);if(ghosts)++ghostWalls;else ++solidWalls;
   for(unsigned z=0;z<m.b(p+33);++z)for(unsigned x=0;x<m.b(p+32);++x){auto c=base+(z*m.w(0xa0c30)+x)*16;
    if(!ghosts){require(m.b(c+12)==21||m.b(c+4)!=i,"Blast-only wall collision changed: "+path.stem().string());continue;}
    require(m.b(c+12)!=21,"L161 organic wall remains solid");++ghostCells;
    if(m.b(c+12)||m.b(c+7)&128||m.b(c+13)-m.b(c+11)<24)continue;
    auto index=(c-m.d(0xa0c40))/16;auto gx=index%m.w(0xa0c30),gz=index/m.w(0xa0c30);
    auto cx=gx*512+256,cz=gz*512+256;
    std::vector<LevelCell> cells(scene.cells.size());for(unsigned n=0;n<cells.size();++n)for(unsigned b=0;b<16;++b)cells[n].bytes[b]=m.b(m.d(0xa0c40)+n*16+b);
    LevelGrid grid{m.w(0xa0c30),m.w(0xa0c32),cells};
    for(int direction:{-1,1}){Vec3s dx{short(direction*32),short(m.b(c+11)*32),0};require(collideX(grid,cx,cz,dx,0)==cx+direction*32,"Walk-through wall blocked X movement");Vec3s dz{0,short(m.b(c+11)*32),short(direction*32)};require(collideZ(grid,cx,cz,dz,0)==cz+direction*32,"Walk-through wall blocked Z movement");movementChecks+=2;}
   }
  }
 }
 require(ghostWalls==7&&movementChecks,"L161 walk-through wall coverage incomplete");
 std::ofstream(scene.root/"decompilation/reports/walk_through_wall_checks.json")<<"{\"status\":\"passed\",\"maps\":"<<maps<<",\"level9_walls\":"<<ghostWalls<<",\"level9_cells\":"<<ghostCells<<",\"movement_checks\":"<<movementChecks<<",\"other_blast_walls_preserved\":"<<solidWalls<<"}";
}
void coopMarkerSelfTest(){
 auto require=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};unsigned checks=0;
 for(auto size:{std::array<int,2>{320,240},{1280,720},{3840,2160},{3440,1440}}){int w=size[0],h=size[1];double focal=w*.72;auto center=explorerCoopMarker(0,0,4096,focal,w,h);require(center.x==w/2&&center.y==h/2&&!center.edge,"Teammate marker projection failed");++checks;
  for(auto point:{std::array<double,3>{0,0,32000},{-32000,0,100},{32000,0,100},{0,10000,100},{0,-10000,100},{0,0,-4096},{0,0,0}}){auto m=explorerCoopMarker(point[0],point[1],point[2],focal,w,h);require(m.x>0&&m.y>0&&m.x<w&&m.y<h,"Teammate edge marker left screen");std::vector<unsigned> pixels(size_t(w)*h,0xff111111);require(explorerDrawCoopMarker(pixels.data(),w,h,m)>0&&pixels[m.y*w+m.x]==0xffffffff,"Teammate marker hidden by opaque background");checks+=2;}
 }
 std::ofstream(scene.root/"decompilation/reports/coop_marker_checks.json")<<"{\"status\":\"passed\",\"checks\":"<<checks<<",\"depth_independent\":true}";
}
void survivalSelfTest(){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    scene.menu.load(scene.root,scene.root/"decompilation/reports/survival_settings_fixture");
    scene.menu.show(false);
    const auto labels=scene.menu.labels();
    const auto entry=std::find(labels.begin(),labels.end(),L"Survival");
    require(entry!=labels.end(),"Survival missing from PC menu");
    scene.menu.selection=unsigned(entry-labels.begin());
    scene.menu.key(VK_RETURN);
    require(scene.menu.page=="survival","Survival menu did not open");
    scene.menu.selection=1;
    scene.menu.key(VK_RETURN);
    require(scene.menu.page=="survival-skulls","Xeno Skulls menu did not open");
    scene.menu.key(VK_RETURN);
    require(scene.menu.page=="scoring-skulls"&&scene.menu.labels().size()==10,"Scoring Skulls list differs");
    scene.menu.key(VK_RETURN);
    require(scene.menu.survivalSkulls==(1u<<AcidSkull),"Scoring skull row mapped to the wrong toggle");
    scene.menu.key(VK_RETURN);
    scene.menu.key(VK_ESCAPE);
    scene.menu.selection=1;
    scene.menu.key(VK_RETURN);
    const auto funSkulls=scene.menu.labels();
    require(scene.menu.page=="fun-skulls"&&funSkulls.size()==9,"Fun Skulls list differs");
    const auto shrunk=std::find_if(funSkulls.begin(),funSkulls.end(),[](const auto& label){
        return label.find(L"Honey, I Shrunk The Kids Skull")!=std::wstring::npos;
    });
    require(shrunk!=funSkulls.end(),"Enemy size skull is missing from Fun Skulls");
    scene.menu.selection=unsigned(shrunk-funSkulls.begin());
    scene.menu.key(VK_RETURN);
    require(scene.menu.survivalSkulls==(1u<<ShrunkKidsSkull)&&survivalScore(scene.menu.survivalSkulls,100)==0,
            "Fun skull did not toggle or void the score");
    scene.menu.key(VK_RETURN);
    scene.menu.key(VK_ESCAPE);
    scene.menu.key(VK_ESCAPE);
    scene.menu.selection=2;
    require(survivalScore((1u<<AcidSkull)|(1u<<ExoskeletonSkull),100)==7500,
            "Scoring skulls did not multiply");
    constexpr unsigned gameOver=1u<<GameOverManSkull, exoskeleton=1u<<ExoskeletonSkull;
    require(survivalScore(gameOver,100)==1500&&survivalScore(gameOver|exoskeleton,100)==2000&&
            survivalScore(gameOver|exoskeleton|(1u<<AcidSkull),100)==10000&&
            survivalScore(gameOver|exoskeleton|(1u<<RocketManSkull),100)==0,
            "Game Over skull multiplier differs");
    const auto historyBefore=readSurvivalRuns(scene.menu.survivalHistoryPath()).size();
    menuCommand(nullptr,scene.menu.key(VK_RETURN));
    if(!g_survival.active||scene.menu.open||!scene.menu.hasGame)
        throw std::runtime_error("Survival did not start: "+std::string(scene.menu.message.begin(),scene.menu.message.end()));
    require(g_survival.cfg.waves==10&&g_survival.lives==5&&g_survival.doorEntry.size()==6,"Xbox arena settings differ");
    require(scene.survivalQueenSac.faces.size()==632&&scene.survivalQueenSac.vertices.size()==652,
            "Survival egg sac mesh is incomplete");
    require(scene.survivalSacLighting.active,"Original egg sac lighting did not load");
    const auto& sacVertices=scene.survivalQueenSac.vertices;
    const auto [nearEnd,farEnd]=std::minmax_element(sacVertices.begin(),sacVertices.end(),
        [](const Point& a,const Point& b){return a.z<b.z;});
    require(farEnd->z-nearEnd->z>5000,"Survival egg sac lost its original depth");
    for(const auto& face:scene.survivalQueenSac.faces)
        require(scene.textures.rect(face.texture),"Survival egg sac texture missing");
    require(scene.audio->ps1LevelSamples()==43,"Survival creature sound banks are incomplete");
    for(unsigned slot:{54u,63u,67u,75u})
        require(scene.audio->validateMixer(slot),"Survival creature sample did not produce sound");
    {
        auto fixture=scene.memory;
        PlayerControlMemory crowd{fixture};
        const auto base=crowd.d(0xa0c40);
        crowd.w(0xa0c30,5);crowd.w(0xa0c32,5);
        crowd.w(0xa0c34,0);crowd.w(0xa0c36,0);
        std::fill_n(fixture.begin()+base,25*16,0);
        for(unsigned i=0;i<25;++i)crowd.b(base+i*16+13,64);
        constexpr unsigned first=0x300000, second=first+208;
        crowd.d(0x2365e0,second);crowd.d(second+200,first);crowd.d(first+200,0);
        for(unsigned actor:{first,second}){
            crowd.b(actor+0x74,6);crowd.b(actor+0x6f,3);
            crowd.w(actor+0x58,100);crowd.w(actor+8,256);crowd.w(actor+10,768);
            crowd.w(actor+2,32);crowd.w(actor+4,1280);
        }
        crowd.w(first,1120);crowd.w(second,1440);
        const auto roomCell=base+12*16;
        crowd.d(first+0x78,roomCell);crowd.d(second+0x78,roomCell);
        crowd.b(roomCell+12,6);
        crowd.b(second+0x70,1);crowd.b(roomCell+4,1);
        crowd.w(first+0x30,64);
        const auto service=[](auto&,auto,auto,auto,auto){return PlayerControlResult{};};
        moveEnemy(crowd,first,service,true);
        require(crowd.s(first)==1184,"Crowded tile blocked an enemy before physical contact");
        moveEnemy(crowd,first,service,true);
        require(crowd.s(first)==1184,"Enemy moved through another enemy");
        crowd.w(first,1320);crowd.w(first+0x30,64);crowd.w(first+0x34,64);
        crowd.w(second+0x58,0);
        crowd.b(base+13*16+10,20);
        moveEnemy(crowd,first,service,true);
        require(crowd.s(first)==1335&&crowd.s(first+4)==1344,"Enemy failed to slide along a wall");
    }
    const auto queen=g_survival.queenEntity;
    require(queen&&scene.enemies->active()==std::vector<unsigned>{queen}&&!scene.mission.briefing&&!scene.beginExit(false),
            "Survival should start with only the perched Queen");
    auto queenMemory=scene.mem();
    const auto queenHealth=queenMemory.w(queen+0x58);
    const auto& sacAnchor=g_survival.queenSacAnchor;
    for(int distance:{800,2700,4900}){
        const short z=short(sacAnchor[2]+distance);
        require(scene.survivalSacBlocks({short(sacAnchor[0]-1600),0,z},
                                       {short(sacAnchor[0]+1600),0,z}),
                "Player crossed the Survival sac footprint");
    }
    require(!scene.survivalSacBlocks({short(sacAnchor[0]-1800),0,short(sacAnchor[2]+2000)},
                                     {short(sacAnchor[0]-1800),0,short(sacAnchor[2]+3000)}),
            "Clear path beside the sac was blocked");
    const Vec3s throne{short(queenMemory.s(queen)),short(queenMemory.s(queen+2)),short(queenMemory.s(queen+4))};
    require(queenMemory.w(queen+0xc4)==8&&scene.enemies->sprite(queen).width,"The perched Queen has no visible frame");
    const auto combat=withCombatRuntime([](auto&,auto,auto,auto,auto){return PlayerControlResult{};});
    const Vec3s nearby{throne[0],throne[1],short(throne[2]-512)};
    const Vec3s distant{throne[0],throne[1],short(throne[2]-4000)};
    for(unsigned i=0;i<40;++i){
        scene.enemies->tick(distant,100);
        damageEntity(queenMemory,queen,1024,0,combat);
    }
    require(queenMemory.w(queen+0x58)==queenHealth&&queenMemory.w(queen+0xc4)==8,
            "Shooting the perched Queen from a distance triggered a reaction");
    bool swiped=false;
    for(unsigned i=0;i<100;++i){
        scene.enemies->tick(nearby,100);
        swiped|=queenMemory.w(queen+0xc4)==9;
        require(scene.enemies->health()==100&&queenMemory.w(queen+0x58)==queenHealth&&
                queenMemory.w(queen+0x4e)==PerchedQueenDamageGate&&
                queenMemory.s(queen)==throne[0]&&queenMemory.s(queen+2)==throne[1]&&queenMemory.s(queen+4)==throne[2],
                "The perched swipe hurt the player or moved the Queen");
    }
    require(swiped&&queenMemory.w(queen+0xc4)==8,"The warning swipe did not play and return to idle");
    const auto round=acquireProjectile(queenMemory);
    require(round!=0,"Queen test projectile pool empty");
    const auto roundPosition=queenMemory.d(round);
    for(unsigned axis=0;axis<3;++axis)queenMemory.w(roundPosition+axis*2,throne[axis]);
    queenMemory.d(round+12,queenMemory.d(queen+0x78));
    queenMemory.b(round+32,0);
    queenMemory.b(round+30,0);
    queenMemory.w(round+26,1024);
    require(!collideProjectileWithEntities(queenMemory,round,combat,true),
            "A projectile hit the perched Queen");
    releaseProjectile(queenMemory,round);
    auto tick=[](){scene.tick(0,0);explorerSurvivalTick(scene);};
    require(scene.transitionBrightness()==0,"Survival did not start on black");
    for(unsigned i=0;i<explorer_survival::kIntroBlackTicks;++i)tick();
    require(scene.transitionBrightness()==0,"Survival faded in before the Queen hiss finished");
    tick();
    require(scene.transitionBrightness()>0&&scene.transitionBrightness()<255,"Survival fade-in did not start");
    for(unsigned i=explorer_survival::kIntroBlackTicks+1;i<180;++i)tick();
    require(scene.transitionBrightness()==255,"Survival fade-in did not finish");
    require(g_survival.wave==1&&g_survival.queue.size()==10,"First wave countdown/roster failed");
    for(unsigned i=0;i<90;++i)tick();
    require(g_survival.alive>0&&g_survival.queue.size()<10,"Wave did not spawn enemies");
    require(std::any_of(scene.doors.entries.begin(),scene.doors.entries.end(),[](const auto& d){return d.amount>.85f;}),"Spawn doors did not open");
    const auto playerPosition=scene.player.position;
    const int playerYaw=scene.yaw;
    scene.player.position={short(throne[0]+3800),throne[1],short(throne[2]-2200)};
    scene.yaw=470;
    preview(scene.root/"decompilation/reports/survival-pc-preview.bmp");
    scene.player.position=playerPosition;
    scene.yaw=playerYaw;
    require(g_survival.cfg.ammo.size()==26&&g_survival.cfg.health.size()==14&&
            scene.pickups.active().size()>=41,"Survival starting supplies differ");
    auto collectAt=[&](const std::pair<unsigned,unsigned>& cell){
        auto m=scene.mem();
        const unsigned x=cell.first*512+256,z=cell.second*512+256;
        scene.pickups.tick(int(x)-m.w(0xa0c34),floorHeight(scene.grid,x,z),int(z)-m.w(0xa0c36));
    };
    auto memory=scene.mem();
    memory.w(0xa0abe,0);memory.w(0xa0ac0,0);memory.w(0xa0ab8,50);
    collectAt(g_survival.cfg.reserveAmmo.front());
    collectAt(g_survival.cfg.health.front());
    require(memory.w(0xa0abe)==15&&memory.w(0xa0ac0)==6&&memory.w(0xa0ab8)==100,
            "Survival ammo reserve or health pickup failed");
    memory.w(0xa0abe,0);memory.w(0xa0ac0,0);memory.w(0xa0ab8,50);
    const unsigned wave=g_survival.wave;
    g_survival.ticks=explorer_survival::kResupplyTicks-1;
    explorerSurvivalTick(scene);
    collectAt(g_survival.cfg.reserveAmmo.front());
    collectAt(g_survival.cfg.health.front());
    require(g_survival.wave==wave&&memory.w(0xa0abe)==15&&memory.w(0xa0ac0)==6&&memory.w(0xa0ab8)==100,
            "Supplies did not respawn during an active wave");

    const auto startX=g_survival.cfg.startX,startZ=g_survival.cfg.startZ;
    require(explorerSurvivalSpawn(scene,13,80,startX,startZ+2,4),"Cannot spawn Survival smartgun synth");
    const auto synth=memory.d(0x2365e0);
    require(memory.b(synth+0x74)==13,"Smartgun synth fixture spawned a different actor");
    const auto shotsBefore=scene.enemies->effects[0x2a8bc];
    for(unsigned i=0;i<180&&scene.enemies->effects[0x2a8bc]<shotsBefore+3;++i){
        scene.enemies->tick(scene.player.position,100);
        require(scene.enemies->sprite(synth).width&&memory.w(synth+0xc4)<13,
                "Smartgun synth vanished during its attack");
    }
    require(scene.enemies->effects[0x2a8bc]>=shotsBefore+3&&memory.d(0x236300)&&
            memory.b(memory.d(0x236300)+30)==5,"Smartgun synth did not fire a projectile");
    memory.w(synth+0x58,0);
    enterEntityDeath(memory,synth,[](auto&,auto,auto,auto,auto){return PlayerControlResult{};});
    scene.enemies->retireSurvivalCorpse(synth);

    scene.enemies->terrainDamage(100,4,true);
    require((memory.b(0xa0c90)&0x20)&&!scene.enemies->health(),"Test did not trigger native death flag");
    scene.returnAfterDeath();
    memory.w(0xa0abe,0);memory.w(0xa0ac0,0);
    explorerSurvivalRestock(scene);
    collectAt(g_survival.cfg.reserveAmmo.front());
    require(memory.w(0xa0abe)==15&&memory.w(0xa0ac0)==6,"Floor pickups blocked after respawn");
    require(g_survival.lives==4&&g_survival.deaths==1&&scene.player.health==100,"Survival did not respawn with one life spent");
    unsigned reached=1;
    for(unsigned i=0;i<1500&&g_survival.active;++i){
        reached=std::max(reached,g_survival.wave);
        if(g_survival.breakTicks>1){
            g_survival.breakTicks=1;
        }
        g_survival.spawnTicks=0;
        auto m=scene.mem();
        for(auto enemy:scene.enemies->active()){
            if(m.s(enemy+0x58)<=0||(enemy==queen&&!g_survival.queenOut))continue;
            m.w(enemy+0x58,0);
            enterEntityDeath(m,enemy,[](auto&,auto,auto,auto,auto){return PlayerControlResult{};});
        }
        const auto previousWave=g_survival.wave;
        tick();
        if(g_survival.wave!=previousWave){
            if(g_survival.wave==2||g_survival.wave==4){
                const unsigned weapon=g_survival.wave==2?1:2;
                collectAt(g_survival.cfg.reserveAmmo[weapon]);
                require(scene.hud.owns(weapon)&&scene.hud.select(weapon),"Scheduled weapon was not collected");
                if(weapon==2)scene.enemies->fireWeapon(scene.player.position,scene.yaw,scene.pitch,2);
            }
            if(g_survival.wave==3)require(scene.notice==L"THE QUEEN IS AGITATED.","Wave 3 announcement differs");
            if(g_survival.wave==7)require(scene.notice==L"THE QUEEN IS LIVID.","Wave 7 announcement differs");
        }
        if(g_survival.wave<10){
            require(!g_survival.queenOut&&m.w(queen+0x58)==queenHealth&&(m.w(queen+0xc4)==8||m.w(queen+0xc4)==9)&&
                    m.s(queen)==throne[0]&&m.s(queen+2)==throne[1]&&m.s(queen+4)==throne[2],
                    "The Queen left her throne before wave ten");
        }else if(g_survival.queenOut&&!g_survival.ended&&m.s(queen+0x58)>0){
            require(g_survival.queenEntity==queen&&!m.w(queen+0x4e),"Final wave did not release the original Queen");
        }
        for(auto enemy:scene.enemies->active())scene.enemies->sprite(enemy);
        if(scene.hud.selected()==2)scene.hud.weapon();
    }
    std::ofstream(scene.root/"decompilation/reports/survival_pc_state.txt")
        <<"wave="<<g_survival.wave<<" reached="<<reached<<" queued="<<g_survival.queue.size()
        <<" alive="<<g_survival.alive<<" lives="<<g_survival.lives<<" queen="<<g_survival.queenOut
        <<" victory="<<g_survival.victory<<" remaining="<<g_survival.breakTicks
        <<" enemies="<<scene.enemies->active().size()<<" free="<<scene.mem().d(0x2365d8);
    require(reached==10&&g_survival.queenOut&&g_survival.victory&&scene.menu.page=="survival results","Survival did not complete ten waves and the Queen finale");
    require(scene.pickups.active().size()>0,"Survival did not drop supplies");
    const auto savedRuns=readSurvivalRuns(scene.menu.survivalHistoryPath());
    require(savedRuns.size()==historyBefore+1&&savedRuns.back().waves==10&&
            savedRuns.back().score>0&&savedRuns.back().kills>0,"Survival victory history was not recorded");
    menuCommand(nullptr,scene.menu.key(VK_RETURN));
    require(scene.menu.page=="main"&&!scene.menu.hasGame&&!g_survival.active,"Survival results did not return to the title");
    scene.menu.survivalSkulls=(1u<<AcidSkull)|(1u<<GunMalfunctionSkull)|(1u<<ExoskeletonSkull)
        |(1u<<RocketManSkull)|(1u<<CrippledSkull)|(1u<<BrokenVisorSkull);
    menuCommand(nullptr,ExplorerMenu::SurvivalStart);
    auto skullMemory=scene.mem();
    const auto boostedQueen=g_survival.queenEntity;
    require(skullMemory.w(boostedQueen+0x58)==queenHealth+(queenHealth+3)/4,
            ("Exoskeleton Queen HP actual "+std::to_string(skullMemory.w(boostedQueen+0x58))+" baseline "+std::to_string(queenHealth)+" mask "+std::to_string(g_survival.skulls)).c_str());
    scene.enemies->terrainDamage(3,5,true);
    require(scene.enemies->health()==94,"Acid skull did not double acid damage");
    scene.enemies->terrainDamage(3,4,true);
    require(scene.enemies->health()==91,"Acid skull changed non-acid damage");
    require(scene.enemies->fireWeapon(scene.player.position,scene.yaw,scene.pitch,0),
            "Rocket Man failed to launch a grenade");
    require(skullMemory.w(0xa0abe)==13&&scene.enemies->ammoUsed==2&&
            skullMemory.b(skullMemory.d(0x236300)+30)==12,
            "Rocket Man or Gun Malfunction consumed the wrong ammo");
    g_survival.introTicks=0;
    scene.tick(1,0);
    require(scene.acceleration.maximumSpeed==90,"Crippled did not reduce walk speed");
    scene.tick(1,0,true);
    require(scene.acceleration.maximumSpeed==108,"Crippled did not reduce sprint speed");
    scene.enemies->survivalSkulls|=1u<<ShrunkKidsSkull;
    bool resized=false;
    for(unsigned i=0;i<9;++i){
        require(explorerSurvivalSpawn(scene,6,60,g_survival.cfg.startX,g_survival.cfg.startZ+2,4),
                "Cannot spawn a size-skull test enemy");
        const auto actor=skullMemory.d(0x2365e0);
        const auto scale=skullMemory.w(actor+0xb4);
        resized|=scale!=0xa00;
        require((scale==0x500||scale==0xa00||scale==0x1400)&&
                skullMemory.w(actor+8)==256&&skullMemory.w(actor+10)==768&&skullMemory.w(actor+12)==256,
                "Size skull changed the original hitbox");
    }
    require(resized,"Size skull did not produce a resized enemy");
    scene.moviesEnabled=false;
    for(unsigned i=0;i<5;++i)scene.returnAfterDeath();
    require(!g_survival.active&&g_survival.deaths==5&&scene.menu.page=="survival results","Survival did not end when lives ran out");
    const auto allRuns=readSurvivalRuns(scene.menu.survivalHistoryPath());
    require(allRuns.size()==historyBefore+2&&allRuns.back().score==0&&allRuns.back().deaths==5&&
            allRuns.back().ammo==2,"Unscored Survival run history differs");
    menuCommand(nullptr,ExplorerMenu::MainMenu);
    scene.menu.survivalSkulls=(1u<<LastStandSkull)|(1u<<NoResupplySkull)|(1u<<SwarmSkull);
    menuCommand(nullptr,ExplorerMenu::SurvivalStart);
    require(g_survival.lives==1&&survivalScore(g_survival.skulls,100)==150000,
            "New scoring skulls did not stack or set one life");
    const auto startingCaches=scene.pickups.active();
    require(!startingCaches.empty(),"No Resupply removed starting caches");
    scene.mem().w(0xa0abe,0);
    collectAt(g_survival.cfg.reserveAmmo.front());
    require(scene.pickups.active().size()<startingCaches.size(),"No Resupply test could not empty a cache");
    const auto beforeRestock=scene.pickups.active();
    explorerSurvivalRestock(scene);
    require(scene.pickups.active()==beforeRestock,"No Resupply restocked a cache");
    explorerSurvivalWave(scene,1);
    require(g_survival.queue.size()==13,"Swarm did not add the rounded 25 percent");
    const auto huggersBefore=std::count_if(g_survival.queue.begin(),g_survival.queue.end(),
        [](const auto& enemy){return enemy.kind==2;});
    g_survival.skulls|=(1u<<FacehuggerPartySkull)|(1u<<LuckyDipSkull);
    const auto skullMemoryForAmmo=scene.mem();
    scene.mem().w(0xa0abe,0);
    explorerSurvivalWave(scene,1);
    require(g_survival.queue.size()==13&&std::count_if(g_survival.queue.begin(),g_survival.queue.end(),
            [](const auto& enemy){return enemy.kind==2;})>huggersBefore&&skullMemoryForAmmo.w(0xa0abe)>0&&
            survivalScore(g_survival.skulls,100)==0,"Fun wave skulls did not apply");
    scene.returnAfterDeath();
    require(!g_survival.active&&g_survival.deaths==1,"Last Stand allowed a respawn");
    menuCommand(nullptr,ExplorerMenu::MainMenu);
    menuCommand(nullptr,ExplorerMenu::Start);
    require(scene.mission.sourceCode=="111"&&!g_survival.active&&!scene.enemies->active().empty(),"Campaign did not resume normally after Survival");
    scene.menu.survivalSkulls=0;
    for(unsigned arena:{1u,2u}){
        scene.menu.survivalArena=arena;
        menuCommand(nullptr,ExplorerMenu::SurvivalStart);
        require(g_survival.active&&g_survival.cfg.name==SurvivalArenaNames[arena]&&
                g_survival.doorEntry.size()==6,"Additional Survival arena failed to start");
        require(scene.hud.selected()==0&&scene.hud.ammo>0,
                "Additional arena loadout differs");
        const auto map=scene.mem();
        const auto queen=g_survival.queenEntity;
        const double dx=map.s(queen)-scene.player.position[0],dz=map.s(queen+4)-scene.player.position[2];
        const double angle=scene.yaw*2*Pi/4096;
        require(queen&&std::hypot(dx,dz)>3500&&std::hypot(dx,dz)<4700&&
                -dx*std::sin(angle)+dz*std::cos(angle)>3500,"Arena start does not face the Queen");
        for(const auto& face:scene.survivalQueenSac.faces)
            require(scene.textures.rect(face.texture),"Additional arena Queen sac texture missing");
        explorerSurvivalWave(scene,1);
        require(!g_survival.queue.empty(),"Additional arena roster did not queue");
        require(g_survival.queue.size()==10&&std::count_if(g_survival.queue.begin(),g_survival.queue.end(),
            [](const auto& enemy){return enemy.kind==6&&enemy.health==30;})==4,
            "Lair opening roster differs from Colony Hive");
        for(const auto [wave,weapon]:{std::pair{2u,1u},std::pair{4u,2u},std::pair{7u,3u}}){
            const auto kind=explorer_survival::kWeapon[weapon].kind;
            auto offered=[&]{
                const auto items=scene.pickups.active();
                return std::any_of(items.begin(),items.end(),[&](unsigned item){return map.b(item+32)==kind;});
            };
            explorerSurvivalWave(scene,wave-1);
            explorerSurvivalCleared(scene);
            require(!offered(),"Weapon dropped ahead of its scheduled wave");
            explorerSurvivalWave(scene,wave);
            require(offered(),"Scheduled weapon missing from lair");
        }
        g_survival.breakTicks=0;
        g_survival.introTicks=0;
        explorerSurvivalWave(scene,1);
        const auto startingPosition=scene.player.position;
        bool farEnemyMoved=false;
        std::map<unsigned,std::array<int,2>> distant;
        for(unsigned tick=0;tick<300;++tick){
            scene.tick(0,0);
            explorerSurvivalTick(scene);
            for(auto e:scene.enemies->active()){
                if(e==queen)continue;
                const int x=map.s(e),z=map.s(e+4);
                if(std::hypot(x-startingPosition[0],z-startingPosition[2])<17000)continue;
                auto [entry,newEnemy]=distant.try_emplace(e,std::array{x,z});
                if(!newEnemy&&std::hypot(x-entry->second[0],z-entry->second[1])>256)farEnemyMoved=true;
            }
        }
        if(arena==1)require(farEnemyMoved,"Foundry's distant enemies did not move without player approach");
        require(g_survival.queue.empty(),
                ("Opening lair wave did not spawn: arena "+std::to_string(arena)+" queued "+std::to_string(g_survival.queue.size())+" alive "+std::to_string(g_survival.alive)+" wave "+std::to_string(g_survival.wave)).c_str());
        menuCommand(nullptr,ExplorerMenu::MainMenu);
    }
    const auto achievementPath=scene.menu.root/("achievement_check_"+std::to_string(GetCurrentProcessId())+".txt");
    ExplorerAchievements achievements;
    achievements.load(achievementPath);
    achievements.campaignExit("111",true,{100,100,100},2);
    achievements.campaignExit("113",false,{0,0,0},0);
    ExplorerSurvivalState achievementRun;
    achievementRun.wavesSurvived=5;
    achievementRun.skulls=1u<<RocketManSkull;
    achievements.survival(0,achievementRun,false,false);
    require(!achievements.has(90),"Fun Skull unlocked a Survival achievement");
    achievementRun.skulls=1u<<LastStandSkull;
    for(unsigned arena=0;arena<3;++arena)achievements.survival(arena,achievementRun,true,false);
    achievements.load(achievementPath);
    require(achievements.has(1)&&achievements.has(23)&&achievements.has(45)&&achievements.has(67)&&
            achievements.has(90)&&achievements.has(97)&&achievements.has(99)&&!achievements.has(100),
            "Local achievements did not persist the expected rules");
    fs::remove(achievementPath);
    std::ofstream(scene.root/"decompilation/reports/survival_pc_checks.json")
        <<"{\"status\":\"passed\",\"waves\":10,\"spawn_doors\":6,\"mid_wave_resupply\":true,\"queen_finale\":true,\"lives_and_results\":true,\"campaign_return\":true}";
}
void goopPanelSelfTest(){
    scene.level=scene.root/"GameData/PS1/CD/SECT31/L331LEV.MAP";
    scene.load(true);
    auto m=scene.mem();
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const unsigned liftIndices[]={1,0,2};
    for(unsigned panelIndex=0;panelIndex<3;++panelIndex){
        const auto found=std::find_if(scene.logic.panels.begin(),scene.logic.panels.end(),
            [=](const auto& panel){return panel.index==panelIndex;});
        require(found!=scene.logic.panels.end(),"L331 goop panel missing");
        const auto& panel=*found;
        const double angle=panel.rotation*Pi/4;
        const int x=panel.x+int(std::lround(std::sin(angle)*160));
        const int z=panel.z-int(std::lround(std::cos(angle)*160));
        auto use=[&](){return scene.logic.use(m,scene.doors,x,panel.y,z,panel.rotation*512);};
        auto& lift=scene.logic.lifts.at(liftIndices[panelIndex]);
        const auto before=m.w(0xa0ae6+27*2);
        require(use()==L"Panel activated"&&lift.moving,"L331 panel did not move its lift");
        use(); // A press during travel must leave the panel available for the next use.
        require(!scene.logic.panelOn(m,panelIndex),"Busy L331 lift latched its panel");
        for(unsigned tick=0;tick<450&&(lift.moving||lift.remaining);++tick)
            scene.logic.tick(m,scene.doors,-32768,0,-32768);
        require(!lift.moving&&!lift.remaining,"L331 lift did not settle");
        require(use()==L"Panel activated"&&lift.moving,"L331 panel did not move its lift a second time");
        require(m.w(0xa0ae6+27*2)==before+1,"Repeated panel use inflated objective credit");
    }
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR,int show){bool selfTest=false,photoModeTest=false,enemyTest=false,benchmark=false,allTest=false,audioTest=false,logicTest=false,dayOneTest=false,alarmTest=false,pickupTest=false,weaponTest=false,weaponAudioTest=false,missionObjectTest=false,menuTest=false,endLevelTest=false,ps1Test=false,gpuTest=false,controlsWorldTest=false,environmentTest=false,bulletTrailTest=false,flameTest=false,secretEnemyTest=false,secretEntranceTest=false,allEntranceTest=false,podTest=false,liftTest=false,thresholdTest=false,passwordTest=false,eggTest=false,lateColonyTest=false;unsigned requestedBackend=99;fs::path replayCheck;try{explorerEnablePerMonitorDpi();wchar_t executable[32768];GetModuleFileNameW(nullptr,executable,32768);scene.root=fs::path(executable).parent_path().parent_path().parent_path().parent_path();{const std::wstring line=GetCommandLineW();bool tool=false;for(auto word:{L"-test",L"audit",L"sweep",L"soak",L"benchmark",L"-check",L"--level"})tool|=line.find(word)!=std::wstring::npos;if(!tool)ExplorerDiagnostics::start(scene.root,line);if(line.find(L"--simulate-crash")!=std::wstring::npos){ExplorerDiagnostics::note("crash check","Crash report check");volatile int* nowhere=nullptr;*nowhere=1;}if(line.find(L"--simulate-error")!=std::wstring::npos){ExplorerDiagnostics::note("error check","Error log check");ExplorerDiagnostics::error("simulated minor error (the game carried on)");ExplorerDiagnostics::finish(0);return 0;}if(line.find(L"--simulate-abort")!=std::wstring::npos){ExplorerDiagnostics::note("abort check","Crash report check");std::abort();}}scene.level=scene.root/"GameData/PS1/CD/SECT11/L111LEV.MAP";int argc;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);for(int i=1;i<argc;++i){if(std::wstring(argv[i])==L"--photo-mode-self-test"){selfTest=true;photoModeTest=true;}else if(std::wstring(argv[i])==L"--egg-self-test"){selfTest=true;eggTest=true;}else if(std::wstring(argv[i])==L"--late-colony-self-test"){selfTest=true;lateColonyTest=true;}else if(std::wstring(argv[i])==L"--password-self-test"){selfTest=true;passwordTest=true;}else if(std::wstring(argv[i])==L"--mission-threshold-self-test"){selfTest=true;thresholdTest=true;}else if(std::wstring(argv[i])==L"--lift-self-test"){selfTest=true;liftTest=true;}else if(std::wstring(argv[i])==L"--pod-self-test"){selfTest=true;podTest=true;}else if(std::wstring(argv[i])==L"--all-entrance-self-test"){selfTest=true;allEntranceTest=true;}else if(std::wstring(argv[i])==L"--secret-entrance-self-test"){selfTest=true;secretEntranceTest=true;}else if(std::wstring(argv[i])==L"--secret-enemy-self-test"){selfTest=true;secretEnemyTest=true;}else if(std::wstring(argv[i])==L"--flame-self-test"){selfTest=true;flameTest=true;}else if(std::wstring(argv[i])==L"--bullet-trail-self-test"){selfTest=true;bulletTrailTest=true;}else if(std::wstring(argv[i])==L"--environment-self-test"){selfTest=true;environmentTest=true;}else if(std::wstring(argv[i])==L"--controls-world-self-test"){selfTest=true;controlsWorldTest=true;}else if(std::wstring(argv[i])==L"--renderer"&&i+1<argc){std::wstring name=argv[++i];requestedBackend=name==L"dx12"?1:name==L"vulkan"?2:name==L"software"?3:0;}else if(std::wstring(argv[i])==L"--gpu-self-test"){selfTest=true;gpuTest=true;}else if(std::wstring(argv[i])==L"--ps1-self-test"){selfTest=true;ps1Test=true;}else if(std::wstring(argv[i])==L"--end-level-self-test"){selfTest=true;endLevelTest=true;}else if(std::wstring(argv[i])==L"--menu-self-test"){selfTest=true;menuTest=true;}else if(std::wstring(argv[i])==L"--mission-object-self-test"){selfTest=true;missionObjectTest=true;}else if(std::wstring(argv[i])==L"--weapon-audio-self-test"){selfTest=true;weaponAudioTest=true;}else if(std::wstring(argv[i])==L"--weapon-switch-self-test"){selfTest=true;weaponTest=true;}else if(std::wstring(argv[i])==L"--pickup-self-test"){selfTest=true;pickupTest=true;}else if(std::wstring(argv[i])==L"--alarm-self-test"){selfTest=true;alarmTest=true;}else if(std::wstring(argv[i])==L"--day-one-self-test"){selfTest=true;dayOneTest=true;}else if(std::wstring(argv[i])==L"--door-panel-self-test"){selfTest=true;logicTest=true;}else if(std::wstring(argv[i])==L"--audio-device-self-test"){selfTest=true;benchmark=true;audioTest=true;}else if(std::wstring(argv[i])==L"--all-levels-self-test"){selfTest=true;allTest=true;}else if(std::wstring(argv[i])==L"--render-benchmark"){selfTest=true;benchmark=true;}else if(std::wstring(argv[i])==L"--self-test")selfTest=true;else if(std::wstring(argv[i])==L"--enemy-self-test"){selfTest=true;enemyTest=true;}else if(std::wstring(argv[i])==L"--check-replay"&&i+1<argc){selfTest=true;replayCheck=fs::absolute(argv[++i]);}else if(std::wstring(argv[i])==L"--level"&&i+1<argc)scene.level=fs::absolute(argv[++i]);}LocalFree(argv);scene.load();if(!replayCheck.empty()){
 scene.menu.load(scene.root);
 scene.watchReplay(replayCheck);
 if(!scene.replaying)throw std::runtime_error(std::string(scene.menu.message.begin(),scene.menu.message.end()));
 unsigned frames=1;
 while(scene.replayFrame())++frames;
 if(frames<2)throw std::runtime_error("Replay has only one frame");
 return 0;
}if(std::wstring(GetCommandLineW()).find(L"--goop-panel-self-test")!=std::wstring::npos){goopPanelSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--isolation-self-test")!=std::wstring::npos){selfTest=true;isolationSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--extermination-views")!=std::wstring::npos){selfTest=true;exterminationViewsCheck();return 0;}if(std::wstring(GetCommandLineW()).find(L"--extermination-shot")!=std::wstring::npos){selfTest=true;exterminationShot();return 0;}if(std::wstring(GetCommandLineW()).find(L"--panel-audit")!=std::wstring::npos){selfTest=true;panelAudit();return 0;}if(std::wstring(GetCommandLineW()).find(L"--sound-bank-audit")!=std::wstring::npos){selfTest=true;soundBankAudit();return 0;}if(std::wstring(GetCommandLineW()).find(L"--hugger-sound-audit")!=std::wstring::npos){selfTest=true;huggerSoundAudit();return 0;}if(std::wstring(GetCommandLineW()).find(L"--shoot-sweep")!=std::wstring::npos){selfTest=true;shootSweep();return 0;}if(std::wstring(GetCommandLineW()).find(L"--render-sweep")!=std::wstring::npos){selfTest=true;renderSweep();return 0;}if(std::wstring(GetCommandLineW()).find(L"--level-soak")!=std::wstring::npos){selfTest=true;levelSoak();return 0;}if(std::wstring(GetCommandLineW()).find(L"--lift-walk-audit")!=std::wstring::npos){selfTest=true;liftWalkAudit();return 0;}if(std::wstring(GetCommandLineW()).find(L"--exit-walk-audit")!=std::wstring::npos){selfTest=true;exitWalkAudit();return 0;}if(std::wstring(GetCommandLineW()).find(L"--crate-shot-audit")!=std::wstring::npos){selfTest=true;crateShotAudit();return 0;}if(std::wstring(GetCommandLineW()).find(L"--object-audit")!=std::wstring::npos){selfTest=true;objectAudit();return 0;}if(std::wstring(GetCommandLineW()).find(L"--queens-revenge-audit")!=std::wstring::npos){selfTest=true;queensRevengeAudit();return 0;}if(std::wstring(GetCommandLineW()).find(L"--secret-order-self-test")!=std::wstring::npos){selfTest=true;secretOrderSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--replay-self-test")!=std::wstring::npos){selfTest=true;replaySelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--survival-self-test")!=std::wstring::npos){survivalSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--coop-marker-self-test")!=std::wstring::npos){coopMarkerSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--walk-through-wall-self-test")!=std::wstring::npos){walkThroughWallSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--coop-connected-self-test")!=std::wstring::npos){selfTest=true;coopConnectedSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--coop-session-self-test")!=std::wstring::npos){selfTest=true;coopSessionSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--coop-rules-self-test")!=std::wstring::npos){coopRulesSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--coop-network-self-test")!=std::wstring::npos){auto checks=trilogy::coop::selfTest();std::ofstream(scene.root/"decompilation/reports/coop_network_checks.json")<<"{\"status\":\"passed\",\"checks\":"<<checks<<"}";return 0;}if(std::wstring(GetCommandLineW()).find(L"--photo-dof-self-test")!=std::wstring::npos){photoDofSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--photo-filter-self-test")!=std::wstring::npos){photoFilterSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--controller-prompts-self-test")!=std::wstring::npos){promptSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--performance-self-test")!=std::wstring::npos){performanceSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--movie-self-test")!=std::wstring::npos){movieSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--continue-self-test")!=std::wstring::npos){selfTest=true;continueSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--spatial-audio-self-test")!=std::wstring::npos){auto checks=scene.audio->validateSpatialMixer();scene.audio->validateMixer();scene.audio->validateEnvironmentMixer();auto fixture=scene.root/"decompilation/reports/spatial_settings_fixture";ExplorerMenu settings;settings.load(scene.root,fixture);settings.page="volume";settings.selection=2;settings.headphoneAudio=false;settings.key(VK_RIGHT);ExplorerMenu restored;restored.load(scene.root,fixture);if(!restored.headphoneAudio)throw std::runtime_error("Headphone setting not saved");std::ofstream(scene.root/"decompilation/reports/spatial_audio_checks.json")<<"{\"status\":\"passed\",\"signal_checks\":"<<checks<<",\"legacy_mixer\":true,\"environment_mixer\":true,\"settings\":true}";return 0;}if(std::wstring(GetCommandLineW()).find(L"--monitor-display-self-test")!=std::wstring::npos){monitorDisplaySelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--controller-remap-self-test")!=std::wstring::npos){controllerRemapSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--4k-performance-self-test")!=std::wstring::npos){performance4kSelfTest(std::wstring(GetCommandLineW()).find(L"--compact-ui")!=std::wstring::npos);return 0;}if(std::wstring(GetCommandLineW()).find(L"--msaa-settings-self-test")!=std::wstring::npos){antialiasingSettingsSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--controller-settings-self-test")!=std::wstring::npos){controllerSettingsSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--meeting-tower-panel-self-test")!=std::wstring::npos){meetingTowerPanelSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--glass-self-test")!=std::wstring::npos){glassSelfTest(requestedBackend==99?3:requestedBackend);return 0;}if(std::wstring(GetCommandLineW()).find(L"--section2-barrel-self-test")!=std::wstring::npos){section2BarrelSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--death-timeout-self-test")!=std::wstring::npos){deathTimeoutSelfTest();return 0;}if(std::wstring(GetCommandLineW()).find(L"--photo-mode-self-test")!=std::wstring::npos){photoModeSelfTest();return 0;}if(photoModeTest){photoModeSelfTest();return 0;}if(eggTest){eggSelfTest();return 0;}if(lateColonyTest){lateColonySelfTest();return 0;}
    if(passwordTest){passwordSelfTest();return 0;}if(thresholdTest){missionThresholdSelfTest();return 0;}if(liftTest){liftSelfTest();return 0;}if(podTest){podSelfTest();return 0;}if(allEntranceTest){allEntranceSelfTest();return 0;}if(secretEntranceTest){secretEntranceSelfTest();return 0;}if(flameTest){flameSelfTest();return 0;}if(secretEnemyTest){secretEnemySelfTest();return 0;}
    if(bulletTrailTest){bulletTrailSelfTest();return 0;}
    if(environmentTest){environmentSelfTest();return 0;}
    if(controlsWorldTest){controlsWorldSelfTest();return 0;}
    if(gpuTest){gpuSelfTest(requestedBackend);return 0;}
    if(ps1Test){ps1SelfTest();return 0;}
    if(endLevelTest){endLevelSelfTest();return 0;}
    if(menuTest){menuSelfTest();return 0;}
    if(missionObjectTest){missionObjectSelfTest();return 0;}
    if(allTest)return allLevelsSelfTest()?0:1;
    if(weaponAudioTest){weaponAudioSelfTest();return 0;}
    if(weaponTest){weaponSwitchSelfTest();return 0;}
    if(pickupTest){pickupSelfTest();return 0;}
    if(alarmTest){alarmSelfTest();return 0;}
    if(dayOneTest){dayOneSelfTest();return 0;}
    if(logicTest){logicSelfTest();return 0;}
    if(benchmark){
        if(audioTest){scene.audio->pause(true);if(!scene.audio->start())throw std::runtime_error("Cannot start WASAPI output device");}
        auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);auto bitmap=CreateCompatibleBitmap(screen,1280,800);auto old=SelectObject(dc,bitmap);std::vector<double> durations;LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);
        const auto args=std::wstring_view(GetCommandLineW());
        scene.cullStaticFaces=args.find(L"--no-view-culling")==std::wstring_view::npos;
        if(args.find(L"--verify-view-culling")!=std::wstring_view::npos){
            std::vector<unsigned> reference(1280*800),optimized(reference.size());
            for(int angle:{0,512,1024,1536,2048,2560,3072,3584}){
                scene.yaw=angle;
                scene.pitch=(angle%1536)-512;
                scene.cullStaticFaces=false;
                scene.draw(dc,1280,800);GdiFlush();
                const LONG bytes=LONG(reference.size()*sizeof(unsigned));
                if(GetBitmapBits(bitmap,bytes,reference.data())!=bytes)throw std::runtime_error("Cannot read culling reference");
                scene.cullStaticFaces=true;
                scene.draw(dc,1280,800);GdiFlush();
                if(GetBitmapBits(bitmap,bytes,optimized.data())!=bytes||reference!=optimized)
                    throw std::runtime_error("Static culling changed the rendered view");
            }
            scene.yaw=0;scene.pitch=0;
        }
        for(unsigned i=0;i<100;++i){LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);scene.yaw=(scene.yaw+17)&4095;if(!(i&1))scene.tick(0,0);scene.draw(dc,1280,800);GdiFlush();QueryPerformanceCounter(&end);if(i>=10)durations.push_back(double(end.QuadPart-begin.QuadPart)*1000/frequency.QuadPart);}
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);double total=0;for(auto t:durations)total+=t;std::sort(durations.begin(),durations.end());double average=total/durations.size();std::ofstream report(scene.root/"decompilation/reports/render_performance.json");report<<"{\n  \"status\": \"measured\",\n  \"width\": 1280,\n  \"height\": 800,\n  \"frames\": "<<durations.size()<<",\n  \"average_ms\": "<<average<<",\n  \"p95_ms\": "<<durations[durations.size()*95/100]<<",\n  \"uncapped_average_fps\": "<<1000/average<<",\n  \"render_target_fps\": 60,\n  \"simulation_hz\": 30,\n  \"scope\": \"Offscreen CPU/GDI benchmark, including simulation on alternating frames; interactive performance may differ.\"\n}\n";return 0;
    }
    if(enemyTest){enemySelfTest();return 0;}
    if(selfTest){
        for(int yaw:{0,1024,2048,3072}){
            auto right=rotateVector({0,static_cast<short>(yaw),2048},{256,0,0},scene.sine);
            auto forward=rotateVector({0,static_cast<short>(yaw),2048},{0,0,256},scene.sine);
            auto r=cameraPoint({double(right[0]),double(right[1]),double(right[2])},{0,0,0},yaw);
            auto f=cameraPoint({double(forward[0]),double(forward[1]),double(forward[2])},{0,0,0},yaw);
            if(r.x<=0||std::abs(r.z)>.01||f.z<=0||std::abs(f.x)>.01)throw std::runtime_error("Camera/movement handedness mismatch");
        }
        for(int pitch:{-910,-512,0,512,910}){double p=pitch*2*Pi/4096;auto point=cameraPoint({0,768+std::sin(p)*1024,std::cos(p)*1024},{0,0,0},0,pitch);if(std::abs(point.y)>.001||std::abs(point.z-1024)>.001)throw std::runtime_error("Freelook pitch axis mismatch");}
        if(scene.faces.empty())throw std::runtime_error("No display geometry loaded");preview(scene.root/"decompilation/reports/exploration-preview.bmp");auto start=scene.player.position;for(int i=0;i<300;++i){scene.yaw=(scene.yaw+7)&4095;scene.tick(i%100<70?1:0,i%100>=70?1:0);auto x=unsigned(int(scene.player.position[0]))+scene.player.originX,z=unsigned(int(scene.player.position[2]))+scene.player.originZ;if((x>>9)>=scene.grid.width||(z>>9)>=scene.grid.height||scene.player.position[1]!=floorHeight(scene.grid,x,z))throw std::runtime_error("Exploration movement left valid level/floor state");}if(scene.player.position==start)throw std::runtime_error("Player did not move");if(scene.swayPhase<=0)throw std::runtime_error("Walking did not advance camera sway");auto stoppedPhase=scene.swayPhase;for(unsigned i=0;i<40;++i)scene.tick(0,0);if(scene.swayPhase!=stoppedPhase||scene.swayAmount>.001)throw std::runtime_error("Camera sway did not settle at rest");if(scene.enemies->unknownServices)throw std::runtime_error("Unknown service during real-map enemy ticks");std::ofstream f(scene.root/"decompilation/reports/exploration_checks.json");f<<"{\n  \"status\": \"passed\",\n  \"camera_basis_checks\": 8,\n  \"pitch_basis_checks\": 5,\n  \"render_target_fps\": 60,\n  \"simulation_hz\": 30,\n  \"native_movement_ticks\": 300,\n  \"original_level\": \"L111LEV.MAP\",\n  \"vertices\": "<<scene.vertices.size()<<",\n  \"display_faces\": "<<scene.faces.size()<<",\n  \"enemy_spawn_markers\": "<<scene.spawns.size()<<",\n  \"scope\": \"Live first-level enemies; provisional navigation, display and hitscan. Doors/triggers pending\"\n}\n";return 0;}
    scene.menu.load(scene.root);scene.replayEnabled=true;scene.menu.achievements.load(scene.root/"decompilation/user/achievements.txt");scene.menu.show(false);scene.mission.briefing=true;WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.lpszClassName=L"TrilogyNativeExplorer";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);auto window=CreateWindowW(wc.lpszClassName,TrilogyTitle,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1280,800,nullptr,nullptr,instance,nullptr);if(!window)throw std::runtime_error("Cannot create window");if(!scene.audio->start()){ExplorerDiagnostics::error("audio device could not start");scene.notice=L"Audio device could not start";scene.noticeTicks=300;}applyDisplay(window);unsigned backend=requestedBackend==99?scene.menu.renderer:requestedBackend;if(backend<3){gpuRenderer=std::make_unique<ExplorerGpu>();if(!gpuRenderer->initialize(window,scene.root,backend,scene.menu.vsync)){gpuRenderer.reset();ExplorerDiagnostics::error("GPU renderer unavailable (requested backend "+std::to_string(backend)+"); using software");scene.notice=L"GPU renderer unavailable; using software";scene.noticeTicks=300;}}if(gpuRenderer){scene.menu.antialiasingModes=gpuRenderer->supportedAntialiasing();scene.menu.antialiasing=gpuRenderer->setAntialiasing(scene.menu.antialiasing);gpuRenderer->setTextureFiltering(scene.menu.textureFiltering);}else{scene.menu.antialiasingModes={0};scene.menu.antialiasing=0;}ShowWindow(window,show);SetForegroundWindow(window);scene.audio->volume(scene.menu.sfx,scene.menu.music);scene.audio->headphones(scene.menu.headphoneAudio);scene.syncMusic();scene.audio->pause(false);scene.interpolate=true;
    scene.moviesEnabled=true;for(auto name:{"LEGAL","FOX","ACCLAIM","PROBE","INTRO"})scene.queueMovie(name);
    ExplorerController controller;if(!controller.initialize()){scene.notice=L"Controller input could not start; keyboard/mouse are available";scene.noticeTicks=180;}
    ExplorerController::State pad;bool hadPad=false,padGrenadePending=false;unsigned navDirection=0;ULONGLONG navAt=0,rumbleTestUntil=0;double lookRemainderX=0,lookRemainderY=0;unsigned previousLimit=scene.menu.frameLimit;
    // Render pacing is independent of the original 30 Hz gameplay clock.
    LARGE_INTEGER frequency,last;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&last);double accumulator=0,nextFrame=double(last.QuadPart)/frequency.QuadPart,statsStart=nextFrame;unsigned frames=0;
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);if(!timer)timer=CreateWaitableTimerW(nullptr,FALSE,nullptr);
    MSG msg{};bool running=true;
    while(running){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){running=false;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}if(!running)break;coop.poll();
        if(ExplorerDiagnostics::active){static ULONGLONG lastNote=0;const auto tickNow=GetTickCount64();if(tickNow-lastNote>=1000){lastNote=tickNow;   // what the player is doing, for crash reports
            const auto& code=scene.mission.sourceCode;std::string where=scene.menu.open&&!scene.menu.hasGame?"Main menu":scene.isolationStage?"Extermination stage "+std::to_string(scene.isolationStage)+" ("+code+")":g_survival.active?"Survival ("+code+")":scene.coopGame?"Co-op campaign, level "+code:"Campaign, level "+code;
            if(scene.menu.open&&scene.menu.hasGame)where+=", paused";
            {static std::wstring lastShown;const std::wstring shown=!scene.menu.message.empty()?scene.menu.message:scene.noticeTicks?scene.notice:std::wstring();
             if(shown!=lastShown){lastShown=shown;std::wstring lower=shown;for(auto& c:lower)c=towlower(c);
              for(auto word:{L"could not",L"couldn't",L"cannot",L"failed",L"unavailable",L"missing",L"invalid",L"error",L"not found"})if(lower.find(word)!=std::wstring::npos){ExplorerDiagnostics::event("WARNING (shown to player): "+ExplorerDiagnostics::utf8(shown));break;}}}
            ExplorerDiagnostics::note("position "+std::to_string(scene.player.position[0])+","+std::to_string(scene.player.position[1])+","+std::to_string(scene.player.position[2])+" health "+std::to_string(scene.player.health)+" weapon "+std::to_string(scene.hud.selected())+" fps "+std::to_string(int(scene.measuredFps+.5))+(gpuRenderer?" (GPU renderer)":" (software renderer)"),where);}}
        LARGE_INTEGER stamp;QueryPerformanceCounter(&stamp);double now=double(stamp.QuadPart)/frequency.QuadPart,elapsed=std::min(.1,double(stamp.QuadPart-last.QuadPart)/frequency.QuadPart);last=stamp;
        bool focused=GetForegroundWindow()==window;controller.configure(scene.menu.controllerBindings);pad=controller.poll(scene.menu.controllerDeadzone/100.f,focused&&scene.menu.controllerEnabled);
        if(scene.movieRunning&&!scene.movie.active()){scene.audio->stopMovie();scene.movie.close();scene.movieRunning=false;if(scene.mission.briefing){scene.briefingStarted=GetTickCount64();scene.briefingRevealed=false;}scene.audio->pause(scene.menu.open&&scene.menu.hasGame);if(scene.movieQueue.empty()&&!scene.menu.open&&scene.menu.hasGame)mouseCapture(window,true);held.fill(false);tapped.fill(false);}
        while(!scene.movieRunning&&!scene.movieQueue.empty()){auto name=scene.movieQueue.front();scene.movieQueue.pop_front();auto path=scene.root/"GameData/PS1/Movies"/(name+".fmv");if(scene.movie.open(path)){scene.movieRunning=true;mouseCapture(window,false);scene.audio->pause(true);auto pcm=path;pcm.replace_extension(".pcm");try{if(fs::exists(pcm))scene.audio->movie(pcm);}catch(const std::exception& error){ExplorerDiagnostics::error("movie "+name+" audio: "+error.what());std::ofstream(scene.root/"decompilation/reports/movie-playback.log",std::ios::app)<<name<<": "<<error.what()<<"\n";}std::ofstream(scene.root/"decompilation/reports/movie-playback.log",std::ios::app)<<"Playing "<<name<<"\n";}else{ExplorerDiagnostics::event("WARNING: missing or invalid movie "+name+" (skipped)");std::ofstream(scene.root/"decompilation/reports/movie-playback.log",std::ios::app)<<"Missing or invalid movie: "<<name<<"\n";}}
        if(scene.hasMovies()){scene.audio->pauseMovie(!focused);scene.movie.tick(elapsed,focused);controller.stopRumble();if(pad.pressed&(ExplorerController::Confirm|ExplorerController::Cancel|ExplorerController::Pause))scene.movie.skip();pad.buttons=pad.pressed=0;pad.moveX=pad.moveY=pad.lookX=pad.lookY=0;padGrenadePending=false;}
        scene.menu.playstationPrompts=pad.playstation;if(!pad.connected||!scene.menu.controllerEnabled)scene.menu.usingController=false;else if(focused&&(pad.rawPressed||std::abs(pad.moveX)>.2||std::abs(pad.moveY)>.2||std::abs(pad.lookX)>.2||std::abs(pad.lookY)>.2))scene.menu.usingController=true;
        auto padName=controller.name();scene.menu.controllerName.assign(padName.begin(),padName.end());
        if(hadPad&&!pad.connected&&!scene.menu.open&&!scene.mission.briefing){SendMessageW(window,WM_KEYDOWN,VK_ESCAPE,0);scene.menu.message=L"Controller disconnected";}hadPad=pad.connected;
        if(!focused||!scene.menu.controllerEnabled||!pad.connected){pad.buttons=pad.pressed=0;pad.moveX=pad.moveY=pad.lookX=pad.lookY=0;padGrenadePending=false;lookRemainderX=lookRemainderY=0;}
        auto press=[&](unsigned key){controllerInputDispatch=true;SendMessageW(window,WM_KEYDOWN,key,0);SendMessageW(window,WM_KEYUP,key,0);controllerInputDispatch=false;};
        if(!scene.hasMovies()&&focused&&scene.menu.controllerEnabled&&pad.connected){
          if(scene.menu.controllerListening>=0){if(pad.rawPressed&(1ull<<1))menuCommand(window,scene.menu.key(VK_ESCAPE));else for(unsigned button=0;button<34;++button)if(pad.rawPressed&(1ull<<button)){scene.menu.bindController(button);break;}}
          else if(scene.replaying&&!scene.photoMode){if(pad.pressed&ExplorerController::Confirm)press(VK_RETURN);if(pad.pressed&ExplorerController::Cancel)press(VK_ESCAPE);if(pad.rawPressed&(1ull<<2))press('P');}
          else if(scene.menu.open||scene.mission.briefing||scene.mission.complete){unsigned direction=pad.buttons&(ExplorerController::Up|ExplorerController::Down|ExplorerController::Left|ExplorerController::Right);if(direction!=navDirection){navDirection=direction;navAt=0;}auto tick=GetTickCount64();if(direction&&tick>=navAt){unsigned key=(direction&ExplorerController::Up)?VK_UP:(direction&ExplorerController::Down)?VK_DOWN:(direction&ExplorerController::Left)?VK_LEFT:VK_RIGHT;press(key);navAt=tick+(navAt?110:350);}if(pad.pressed&ExplorerController::Confirm)press(VK_RETURN);if((pad.pressed&ExplorerController::Cancel)||(pad.rawPressed&(1ull<<6)))press(VK_ESCAPE);if(scene.menu.open&&scene.menu.page=="main"&&(pad.rawPressed&(1ull<<3)))press(VK_F1);if((pad.pressed&ExplorerController::Map)&&scene.menu.page=="map")press('M');}
          else if(scene.photoMode){
            if(pad.pressed&(ExplorerController::Pause|ExplorerController::Cancel))press(VK_ESCAPE);
            else if(pad.pressed&ExplorerController::Confirm){scene.capturePhoto(window);}
            else if(pad.rawPressed&(1ull<<2)){scene.photoFilter=(scene.photoFilter+1)%ExplorerPhotoFilterNames.size();const auto& fn=ExplorerPhotoFilterNames;scene.photoNotice=std::wstring(L"Filter: ")+fn[scene.photoFilter];scene.photoNoticeTicks=60;}
            else if(pad.rawPressed&(1ull<<3)){scene.photoShowUi=!scene.photoShowUi;}
            else if(pad.rawPressed&(1ull<<8)){scene.photoRoll=0;scene.photoNotice=L"Camera Roll Reset";scene.photoNoticeTicks=45;}
            else if(pad.rawPressed&(1ull<<11)){scene.photoGrid=(scene.photoGrid+1)%3;const wchar_t* gn[]={L"Grid: Off",L"Grid: Rule of Thirds",L"Grid: Crosshair & Frame"};scene.photoNotice=gn[scene.photoGrid];scene.photoNoticeTicks=60;}
            else if(pad.rawPressed&(1ull<<12)){scene.photoLamp=!scene.photoLamp;scene.photoNotice=scene.photoLamp?L"Studio Fill Light: ON":L"Studio Fill Light: OFF";scene.photoNoticeTicks=60;}
            else if((pad.raw&(1ull<<4))&&(pad.rawPressed&((1ull<<13)|(1ull<<14)))){scene.adjustPhotoDof(int(scene.photoDof)+((pad.rawPressed&(1ull<<13))?-5:5));}
            else if(pad.rawPressed&(1ull<<13)){scene.photoFov=std::clamp(scene.photoFov-5.,15.,120.);scene.photoNotice=L"FOV: "+std::to_wstring(int(scene.photoFov))+L"";scene.photoNoticeTicks=45;}
            else if(pad.rawPressed&(1ull<<14)){scene.photoFov=std::clamp(scene.photoFov+5.,15.,120.);scene.photoNotice=L"FOV: "+std::to_wstring(int(scene.photoFov))+L"";scene.photoNoticeTicks=45;}
            double speed=scene.menu.controllerSensitivity*360.,verticalSpeed=scene.menu.controllerSensitivityY*360.;
            lookRemainderX+=pad.lookX*std::abs(pad.lookX)*elapsed*speed;lookRemainderY+=pad.lookY*std::abs(pad.lookY)*elapsed*verticalSpeed*(scene.menu.controllerInvert?-1:1);
            int x=int(lookRemainderX),y=int(lookRemainderY);lookRemainderX-=x;lookRemainderY-=y;
            scene.photoYaw=(scene.photoYaw+x)&4095;scene.photoPitch=std::clamp(scene.photoPitch-y,-1800,1800);
          }else{
            if((pad.rawPressed&(1ull<<8))||(pad.rawPressed&(1ull<<4))){
              if(scene.canEnterPhotoMode()){scene.enterPhotoMode();held.fill(false);tapped.fill(false);}
            }else{
              navDirection=0;navAt=0;if(pad.pressed&ExplorerController::Pause)press(VK_ESCAPE);else{if(pad.pressed&ExplorerController::Use)press(scene.menu.bindings[5]);if(pad.pressed&ExplorerController::Objectives)press(scene.menu.bindings[7]);if(pad.pressed&ExplorerController::Map)press('M');if(pad.pressed&ExplorerController::Next)scene.hud.cycle(1);if(pad.pressed&ExplorerController::Previous)scene.hud.cycle(-1);padGrenadePending|=(pad.pressed&ExplorerController::Grenade)!=0;}
            }
          }
          if(!scene.replaying&&!scene.photoMode&&!scene.menu.open&&!scene.mission.briefing&&!scene.mission.complete&&!scene.exitFading){if(!mouseCaptured)mouseCapture(window,true);double speed=scene.menu.controllerSensitivity*360.,verticalSpeed=scene.menu.controllerSensitivityY*360.;lookRemainderX+=pad.lookX*std::abs(pad.lookX)*elapsed*speed;lookRemainderY+=pad.lookY*std::abs(pad.lookY)*elapsed*verticalSpeed*(scene.menu.controllerInvert?-1:1);int x=int(lookRemainderX),y=int(lookRemainderY);lookRemainderX-=x;lookRemainderY-=y;scene.yaw=(scene.yaw+x)&4095;scene.pitch=std::clamp(scene.pitch-y,-910,910);}
        }
        if(!focused||!scene.menu.controllerEnabled||!pad.connected||!scene.menu.controllerVibration){controller.stopRumble();rumbleTestUntil=0;scene.menu.controllerRumbleTest=false;}else{if((scene.menu.open||scene.mission.briefing||scene.exitFading||!scene.player.health)&&GetTickCount64()>=rumbleTestUntil)controller.stopRumble();if(scene.menu.controllerRumbleTest){float scale=scene.menu.controllerVibration/100.f;bool ok=controller.rumble(.5f*scale,.8f*scale,250);scene.menu.controllerRumbleTest=false;rumbleTestUntil=GetTickCount64()+250;if(!ok)scene.menu.message=L"Vibration is unavailable on this controller connection";}}
        if(!scene.hasMovies())scene.syncMusic();
        if(scene.menu.open||scene.mission.briefing)padGrenadePending=false;
        if(focused){bool pointer=scene.photoMode&&scene.photoShowUi&&(GetAsyncKeyState(VK_MENU)&0x8000);if(pointer!=scene.photoPointerMode){scene.photoPointerMode=pointer;scene.photoDofDragging=false;ReleaseCapture();mouseCapture(window,!pointer);}}
        bool active=!scene.hasMovies()&&!scene.menu.open&&mouseCaptured&&GetForegroundWindow()==window;
        if(scene.replaying){
            try{scene.advanceReplay(elapsed,accumulator);}
            catch(const std::exception& error){ExplorerDiagnostics::error(std::string("replay playback: ")+error.what());scene.stopReplay();scene.menu.message=L"Replay data could not be played.";}
            active=scene.replaying&&scene.photoMode&&focused;
        }
        if(coop.active()){
          auto key=[](unsigned k){return held[k]||tapped[k];};
          scene.firing=active&&(key(VK_LBUTTON)||key(scene.menu.bindings[4])||(pad.buttons&ExplorerController::Fire));
          double forward=std::clamp(double(key(scene.menu.bindings[0])-key(scene.menu.bindings[1]))+pad.moveY,-1.,1.),strafe=std::clamp(double(key(scene.menu.bindings[3])-key(scene.menu.bindings[2]))+pad.moveX,-1.,1.);
          coop.frame(elapsed,active,forward,strafe,key(scene.menu.bindings[9])||(pad.buttons&ExplorerController::Sprint),tapped[scene.menu.bindings[10]]||padGrenadePending);padGrenadePending=false;tapped.fill(false);
        }
        if(active&&!coop.active()){
          if(scene.photoMode){
            double speed=scene.photoSpeed*(elapsed*30.0);
            if(held[VK_SHIFT]||(pad.connected&&(pad.raw&(1ull<<7))))speed*=2.5;
            if(held[VK_CONTROL])speed*=0.25;
            double moveForward=(held['W']?1.:0.)-(held['S']?1.:0.);
            double moveStrafe=(held['D']?1.:0.)-(held['A']?1.:0.);
            double moveVertical=((held[VK_SPACE]||held['E'])?1.:0.)-((held['C']||held['Q'])?1.:0.);
            if(pad.connected&&scene.menu.controllerEnabled){
              moveForward+=pad.moveY;moveStrafe+=pad.moveX;
              bool adjustingDof=(pad.raw&(1ull<<4))&&(pad.raw&((1ull<<13)|(1ull<<14)));
              if(!adjustingDof){if((pad.buttons&ExplorerController::Next)||(pad.raw&(1ull<<33)))moveVertical+=1.;
              if((pad.buttons&ExplorerController::Previous)||(pad.raw&(1ull<<32)))moveVertical-=1.;}
            }
            if(held['Z'])scene.photoRoll=(scene.photoRoll-int(64*(elapsed*30.0)))&4095;
            if(held['X'])scene.photoRoll=(scene.photoRoll+int(64*(elapsed*30.0)))&4095;
            double ya=scene.photoYaw*2*Pi/4096.,pa=scene.photoPitch*2*Pi/4096.;
            double fx=-std::sin(ya)*std::cos(pa),fy=std::sin(pa),fz=std::cos(ya)*std::cos(pa);
            double rx=-std::cos(ya),rz=-std::sin(ya);
            scene.photoX+=(fx*moveForward+rx*moveStrafe)*speed;
            scene.photoY+=(fy*moveForward+moveVertical)*speed;
            scene.photoZ+=(fz*moveForward+rz*moveStrafe)*speed;
            tapped.fill(false);if(!scene.replaying)accumulator=0;
          }else{
            accumulator+=elapsed;while(accumulator>=1.0/30){auto key=[](int k){return held[k]||tapped[k];};if(!scene.exitFading)scene.yaw=(scene.yaw+(key(VK_RIGHT)-key(VK_LEFT))*32)&4095;scene.firing=key(VK_LBUTTON)||key(scene.menu.bindings[4])||(pad.buttons&ExplorerController::Fire);scene.grenadeRequested=scene.grenadeRequested||tapped[scene.menu.bindings[10]]||padGrenadePending;padGrenadePending=false;double forward=std::clamp(double(key(scene.menu.bindings[0])-key(scene.menu.bindings[1]))+pad.moveY,-1.,1.),strafe=std::clamp(double(key(scene.menu.bindings[3])-key(scene.menu.bindings[2]))+pad.moveX,-1.,1.);auto beforeHealth=scene.player.health;auto beforeArmor=scene.mem().w(0xa0aba);auto beforeShots=scene.enemies->shots;auto beforeGrenades=scene.mem().w(0xa0ad0)+scene.mem().w(0xa0ac8);scene.useHeld=key(scene.menu.bindings[5])||(pad.buttons&ExplorerController::Use);scene.tick(forward,strafe,key(scene.menu.bindings[9])||(pad.buttons&ExplorerController::Sprint));explorerSurvivalTick(scene);scene.recordReplayFrame();if(scene.mission.complete||(scene.menu.open&&!scene.menu.hasGame))scene.finishReplayRecording();scene.menu.achievements.campaignTick(scene);if(scene.menu.achievements.toastTicks)--scene.menu.achievements.toastTicks;if(scene.menu.open&&mouseCaptured)mouseCapture(window,false);if(pad.connected&&scene.menu.controllerEnabled&&scene.menu.controllerVibration&&!scene.menu.open&&!scene.mission.briefing){float strength=scene.menu.controllerVibration/100.f;if(scene.player.health<beforeHealth||scene.mem().w(0xa0aba)<beforeArmor)controller.rumble(.7f*strength,.4f*strength,150);else if(scene.mem().w(0xa0ad0)+scene.mem().w(0xa0ac8)<beforeGrenades)controller.rumble(.6f*strength,.8f*strength,140);else if(scene.enemies->shots>beforeShots){constexpr float low[]={.20f,.70f,.25f,.40f,.12f},high[]={.40f,.65f,.40f,.55f,.20f};auto weapon=std::min(4u,scene.hud.selected());controller.rumble(low[weapon]*strength,high[weapon]*strength,weapon==1?150:75);}}tapped.fill(false);accumulator-=1.0/30;}
          }
        }else if(!scene.replaying)accumulator=0;
        if(scene.menu.open&&mouseCaptured)mouseCapture(window,false);if(!coop.active()&&!scene.replaying)scene.renderAlpha=active?accumulator*30:1;
        if(previousLimit!=scene.menu.frameLimit){nextFrame=now;previousLimit=scene.menu.frameLimit;}
        if(now>=nextFrame){if(scene.photoMode){if(scene.photoNoticeTicks)--scene.photoNoticeTicks;if(scene.photoFlashTicks)--scene.photoFlashTicks;}InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);LARGE_INTEGER completed;QueryPerformanceCounter(&completed);if(focused)scene.performance.frame(double(completed.QuadPart)/frequency.QuadPart);else scene.performance.reset();++frames;nextFrame=scene.menu.frameLimit?nextFrame+1.0/scene.menu.frameLimit:now;if(nextFrame<now)nextFrame=now;if(now-statsStart>=1){scene.measuredFps=frames/(now-statsStart);frames=0;statsStart=now;}}
        QueryPerformanceCounter(&stamp);double delay=nextFrame-double(stamp.QuadPart)/frequency.QuadPart;
        if(delay>0&&timer){LARGE_INTEGER due;due.QuadPart=-std::max<LONGLONG>(1,LONGLONG(delay*10000000));SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE);MsgWaitForMultipleObjectsEx(1,&timer,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}else if(delay>0)MsgWaitForMultipleObjectsEx(0,nullptr,DWORD(delay*1000),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    if(timer)CloseHandle(timer);scene.finishReplayRecording();restoreDesktopMode();gpuRenderer.reset();mouseCapture(window,false);ExplorerDiagnostics::finish(0);return int(msg.wParam);
}catch(const std::exception& e){if(selfTest){std::ofstream f(scene.root/"decompilation/reports/exploration_checks.json");f<<"{\"status\":\"failed\"}\n";std::ofstream error(scene.root/"decompilation/reports/exploration-error.txt");error<<e.what();}else{ExplorerDiagnostics::error(e.what());ExplorerDiagnostics::finish(1);MessageBoxA(nullptr,(std::string(e.what())+(ExplorerDiagnostics::active?"\n\nA log of this session was saved in the logs folder.":"")).c_str(),"Alien Trilogy: Remastered",MB_ICONERROR);}return 1;}}
