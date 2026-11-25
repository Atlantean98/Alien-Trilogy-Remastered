#include <string_view>
#include "ExplorerDisplay.h"
#include "ExplorerMenu.h"
#include "ExplorerVersion.h"
#include "ExplorerSurvivalState.h"
#include "ExplorerCheckpoint.h"
#include "ExplorerReplay.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
namespace {const wchar_t* Difficulty[]={L"Acid Reign",L"Raging Terror",L"Xenomania"};const wchar_t* PadActions[]={L"Fire",L"Grenade / secondary fire",L"Use",L"Next weapon",L"Previous weapon",L"Sprint",L"Objectives",L"Auto mapper",L"Pause"};const wchar_t* Actions[]={L"Forward",L"Backward",L"Strafe left",L"Strafe right",L"Fire",L"Use",L"Next weapon",L"Objectives",L"Restart",L"Sprint",L"Grenade / secondary fire"};}
void ExplorerMenu::load(const std::filesystem::path& path,const std::filesystem::path& settingsRoot){assets=path;root=settingsRoot.empty()?path:settingsRoot;isolationPlaytest=bool(std::ifstream(root/"decompilation/user/isolation-playtest.txt"));refreshMonitors();refreshContinue();originalFont.load(path);originalModels.load(path);ExplorerTextures art;art.load(assets/"GameData/PS1/CD/GFX/LOGOSGFX.BND");for(unsigned image=0;image<2;++image){auto a=art.rect(image*2),b=art.rect(image*2+1);if(!a||!b||a->height!=b->height)throw std::runtime_error("Original menu image is incomplete");auto& out=images[image];unsigned aw=a->width-1,bw=b->width-1;out.width=aw+bw;out.height=a->height-1;out.pixels.resize(out.width*out.height);for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x)out.pixels[y*out.width+x]=x<aw?art.pixel(a->page,a->x+x,a->y+y):art.pixel(b->page,b->x+x-aw,b->y+y);}
 resultsImage=images[1];explorerPs1Image(assets,"BACKDROP",resultsImage.width,resultsImage.height,resultsImage.pixels);for(auto& pixel:resultsImage.pixels)pixel=(pixel&0xff000000u)|(((pixel>>16&255)*3/8)<<16)|(((pixel>>8&255)*3/8)<<8)|((pixel&255)*3/8);
 std::ifstream f(root/"decompilation/user/settings.cfg");std::string key;unsigned value;while(f>>key){if(key=="monitorDevice"){f>>monitorDevice;continue;}if(!(f>>value))break;if(key=="difficulty")difficulty=std::min(2u,value);else if(key=="performanceDisplay")performanceDisplay=value!=0;else if(key=="headphoneAudio")headphoneAudio=value!=0;else if(key=="textureFiltering")textureFiltering=std::min(value,1u);else if(key=="msaa"){if(value==0||value==2||value==4||value==8)antialiasing=value;}else if(key=="fps"){if(value==0||value==30||value==60||value==120||value==144||value==175)frameLimit=value;}else if(key=="vsync")vsync=value!=0;else if(key=="controller")controllerEnabled=value!=0;else if(key=="padSensitivity"){controllerSensitivity=std::clamp(value,1u,10u);controllerSensitivityY=controllerSensitivity;}else if(key=="padSensitivityY")controllerSensitivityY=std::clamp(value,1u,10u);else if(key=="padVibration")controllerVibration=std::min(100u,value);else if(key.starts_with("padBind")){auto text=key.substr(7);if(!text.empty()&&text.find_first_not_of("0123456789")==std::string::npos){unsigned index=unsigned(std::stoul(text));if(index<controllerBindings.size()&&(value<26||value==32||value==33))controllerBindings[index]=value;}}else if(key=="padDeadzone")controllerDeadzone=std::clamp(value,5u,40u);else if(key=="padInvert")controllerInvert=value!=0;else if(key=="sensitivity")sensitivity=std::clamp(value,1u,10u);else if(key=="sfx")sfx=std::min(100u,value);else if(key=="music")music=std::min(100u,value);else if(key=="fullscreen")displayMode=value?Borderless:Windowed;else if(key=="display_mode")displayMode=std::min(value,2u);else if(key=="sway")sway=value!=0;else if(key=="invert")invert=value!=0;else if(key=="resolution")resolution=std::min(unsigned(ExplorerResolutions.size()-1),value);else if(key=="aspect")aspect=std::min(unsigned(ExplorerAspects.size()-1),value);else if(key=="fov")fov=std::clamp(value,50u,110u);else if(key=="renderer")renderer=std::min(3u,value);else if(key=="quality")renderQuality=std::min(1u,value);else if(key.starts_with("key")){auto i=std::stoi(key.substr(3));if(i>6)--i;else if(i==6)continue;if(i>=0&&i<int(bindings.size())&&value>0&&value<256)bindings[i]=value;}}
}
void ExplorerMenu::persist()const{auto dir=root/"decompilation/user";std::filesystem::create_directories(dir);std::ofstream f(dir/"settings.cfg",std::ios::trunc);if(!f)throw std::runtime_error("Cannot save menu settings");f<<"difficulty "<<difficulty<<"\nsensitivity "<<sensitivity<<"\nsfx "<<sfx<<"\nmusic "<<music<<"\ndisplay_mode "<<displayMode<<"\ninvert "<<invert<<"\nresolution "<<resolution<<"\nsway "<<sway<<"\nquality "<<renderQuality<<"\nrenderer "<<renderer<<"\nfov "<<fov<<"\naspect "<<aspect<<"\n";f<<"performanceDisplay "<<performanceDisplay<<"\n";f<<"headphoneAudio "<<headphoneAudio<<"\n";f<<"textureFiltering "<<textureFiltering<<"\n";f<<"msaa "<<antialiasing<<"\n";f<<"fps "<<frameLimit<<"\nvsync "<<vsync<<"\ncontroller "<<controllerEnabled<<"\npadSensitivity "<<controllerSensitivity<<"\npadDeadzone "<<controllerDeadzone<<"\npadInvert "<<controllerInvert<<"\n";f<<"padSensitivityY "<<controllerSensitivityY<<"\npadVibration "<<controllerVibration<<"\n";for(unsigned i=0;i<controllerBindings.size();++i)f<<"padBind"<<i<<" "<<controllerBindings[i]<<"\n";for(unsigned i=0;i<bindings.size();++i)f<<"key"<<(i>=6?i+1:i)<<" "<<bindings[i]<<"\n";if(!monitorDevice.empty())f<<"monitorDevice "<<monitorDevice<<"\n";}
void ExplorerMenu::defaults(){controllerEnabled=true;controllerSensitivity=controllerSensitivityY=3;controllerVibration=70;controllerBindings=ExplorerController::DefaultBindings;controllerDeadzone=18;controllerInvert=false;sensitivity=3;invert=false;bindings={'W','S','A','D',VK_SPACE,'E','Q','B','R',VK_SHIFT,VK_RBUTTON};}
void ExplorerMenu::show(bool running){refreshContinue();controllerListening=-1;open=true;hasGame=running;page="main";selection=0;listening=-1;history.clear();message.clear();}
void ExplorerMenu::showResults(const std::array<unsigned,3>& values,bool passed,unsigned required){assessmentPassed=passed;assessmentRequired=required;show(true);page="results";resultPercent=values;for(auto& value:resultPercent)value=std::min(value,100u);resultsStarted=GetTickCount64();resultsRevealed=false;}
bool ExplorerMenu::resultsReady()const{return resultsRevealed||GetTickCount64()-resultsStarted>=4500;}
void ExplorerMenu::pauseGame(){show(true);page=coopSession?"coop-pause":"pause";}
void ExplorerMenu::enter(const std::string& name){history.push_back({page,selection});page=name;selection=0;message.clear();}
void ExplorerMenu::back(){listening=-1;message.clear();if(history.empty()){if(hasGame)open=false;return;}auto entry=history.back();history.pop_back();page=entry.first;selection=entry.second;}
std::wstring ExplorerMenu::keyName(unsigned key)const{if(key==VK_LBUTTON)return L"Left mouse";if(key==VK_RBUTTON)return L"Right mouse";if(key==VK_MBUTTON)return L"Middle mouse";wchar_t text[80]{};GetKeyNameTextW(LONG(MapVirtualKeyW(key,MAPVK_VK_TO_VSC)<<16),text,80);return *text?std::wstring(text):std::to_wstring(key);}
unsigned ExplorerMenu::exterminationSlotLevel(unsigned i)const{
 std::ifstream f(exterminationSlotPath(i));std::string magic;unsigned version=0,level=0;
 return f>>magic>>version>>level&&magic=="ATEXTERM_SLOT"&&version==1&&level>=1&&level<=4?level:0;   // 4: level 3's Queen's Lair
}
std::wstring ExplorerMenu::exterminationSlot(unsigned i)const{
 const auto level=exterminationSlotLevel(i);
 if(!level)return L"Slot "+std::to_wstring(i+1)+L" - Empty";
 std::ifstream objectives(assets/"GameData/Isolation"/std::to_string(level)/"objectives.txt");std::string title;std::getline(objectives,title);
 return L"Slot "+std::to_wstring(i+1)+L" - Level "+std::to_wstring(std::min(level,3u))+(title.empty()?std::wstring():L": "+std::wstring(title.begin(),title.end()));
}
void ExplorerMenu::refreshExtermination(){
 exterminationSaved=0;std::ifstream f(root/"decompilation/user/saves/extermination.sav");std::string magic;unsigned version=0,stage=0;
 if(f>>magic>>version>>stage&&magic=="ATEXTERM"&&version==1&&stage>=2&&stage<=4)exterminationSaved=stage;
}
std::wstring ExplorerMenu::slot(unsigned i)const{std::ifstream f(root/"decompilation/user/saves"/("slot"+std::to_string(i+1)+".sav"));std::string magic,code;unsigned version,difficulty;if(f>>magic>>version>>code>>difficulty&&magic=="TRILOGY_NATIVE_PROGRESS"&&version==2)return L"Slot "+std::to_wstring(i+1)+L" - "+std::wstring(code.begin(),code.end());return L"Slot "+std::to_wstring(i+1)+L" - Empty";}
namespace {
// Original pixel art. The long dome and exposed jaw read at menu size.
void drawXenoSkull(HDC dc,int x,int y,int size,bool enabled){
    constexpr const char* art[]={
        "....########....",
        "..##++++++**##..",
        ".#++++***+++++#.",
        "#++++++#####+++#",
        "#+++###:::::##+#",
        ".####:::::###++#",
        ".....#####+o#++#",
        "........#+###++#",
        "........##++##+#",
        ".........#o*o##.",
        ".........######.",
        "..........####.."
    };
    const COLORREF colors[]={RGB(15,29,24),RGB(45,76,60),RGB(91,144,115),
        RGB(155,217,176),RGB(211,240,208)};
    constexpr std::string_view shades="#:+*o";
    HBRUSH brushes[5];
    for(unsigned i=0;i<5;++i){
        const auto c=colors[i];
        brushes[i]=CreateSolidBrush(enabled?c:RGB(GetRValue(c)/2,GetGValue(c)/2,GetBValue(c)/2));
    }
    for(int row=0;row<int(std::size(art));++row)for(int col=0;col<16;++col){
        const auto shade=shades.find(art[row][col]);
        if(shade==shades.npos)continue;
        RECT pixel{x+col*size,y+row*size,x+(col+1)*size,y+(row+1)*size};
        FillRect(dc,&pixel,brushes[shade]);
    }
    for(auto brush:brushes)DeleteObject(brush);
}
constexpr const wchar_t* SkullDescriptions[]={
    L"Enemy acid deals twice the damage.",
    L"Walk and sprint 25% slower.",
    L"Hides the gameplay HUD.",
    L"Firing uses twice the normal ammo.",
    L"Xenos, the Queen, synths and humans have 25% more health.",
    L"Fire twice as fast.",
    L"Walk and sprint twice as fast.",
    L"Infinite ammo, charges and grenades.",
    L"Some enemies look half-size or double-size. Hitboxes stay the same.",
    L"Every gun fires grenades using its own ammo.",
    L"You have one life for the entire run.",
    L"Fixed caches start stocked and never refill. Enemy loot still drops.",
    L"25% more Xenos per wave. The active enemy limit stays the same.",
    L"Barrels and grenade kills have a 50% chance to trigger a second blast.",
    L"Some Xenos are replaced by facehuggers each wave.",
    L"Each wave equips a random weapon you own and gives you ammo for it.",
    L"Enemies deal double damage. Scores 20x with Exoskeleton Upgrade."
};
static_assert(std::size(SkullDescriptions)==std::size(SurvivalSkullNames));
bool isSkullList(const std::string& page){
    return page=="scoring-skulls"||page=="fun-skulls";
}
std::vector<unsigned> skullIds(const std::string& page){
    std::vector<unsigned> ids;
    for(unsigned i=0;i<std::size(SurvivalSkullNames);++i)
        if((SurvivalSkullMultipliers[i]==0)==(page=="fun-skulls"))ids.push_back(i);
    return ids;
}
std::pair<int, int> menuRows(const std::string& page, int height, size_t count){
    const int preferredLine=std::clamp(height/27,20,34);
    if(page=="survival")return {height/5,std::max(1,std::min(preferredLine,height/23))};
    if(isSkullList(page))return {height/5,std::max(1,std::min(preferredLine,(height*4/5-52)/int(count)))};
    if(page!="main")return {height/5,preferredLine};

    // The title artwork extends to about 68% of the screen.
    const int top=height*71/100;
    const int available=height-top-52;
    const int line=std::max(1,std::min(preferredLine,available/std::max(1,int(count))));
    return {top,line};
}
int arenaPreviewHeight(int height){return height*36/100;}
std::wstring mainMenuDescription(std::wstring_view label,const std::string& continueCode){
    if(label==L"Continue"&&!continueCode.empty())return L"Continue from mission "+std::wstring(continueCode.begin(),continueCode.end())+L" with your saved equipment.";
    static constexpr std::pair<std::wstring_view,const wchar_t*> descriptions[]={
        {L"Continue",L"Return to your current game."},
        {L"Resume Game",L"Return to your current game."},
        {L"Start",L"Choose a difficulty and begin the solo campaign."},
        {L"Extermination",L"Hunt Xenomorphs with a squad of Alien Handlers across three Colony missions."},
        {L"Survival",L"Fight waves in Classic Survival or face five Queens in Queen's Revenge!"},
        {L"Custom Maps",L"Custom map support is planned for a future update."},
        {L"Online Co-Op",L"Host or join the full campaign or either Survival mode with a friend."},
        {L"Load",L"Choose an end-of-mission save and restore your equipment and progress."},
        {L"Options",L"Adjust graphics, display, audio, and controls."},
        {L"Enter Password",L"Enter an original level password or cheat code."},
        {L"Stats",L"Review campaign progress, Survival records, and achievements."},
        {L"Replays",L"Watch recorded gameplay and open its screenshots."},
        {L"Exit",L"Close Alien Trilogy and return to Windows."},
    };
    for(const auto& [option,description]:descriptions)if(option==label)return description;
    return {};
}
}
void ExplorerMenu::refreshReplays(){
    replayFiles.clear();
    std::error_code error;
    for(std::filesystem::directory_iterator it(replayDirectory(),error),end;!error&&it!=end;it.increment(error)){
        if(it->path().extension()!=".atr")continue;
        std::error_code fileError;
        if(it->is_regular_file(fileError)&&ExplorerReplay::hasPlayback(it->path(),it->file_size(fileError))&&!fileError)
            replayFiles.push_back(it->path());
    }
    std::sort(replayFiles.rbegin(),replayFiles.rend());
    replayIndex=0;
}
void ExplorerMenu::showStats(){
    survivalRuns=readSurvivalRuns(survivalHistoryPath());
    survivalRunIndex=unlockedAchievementIndex=0;
    unlockedAchievements.clear();
    for(unsigned id=1;id<std::size(ExplorerAchievementCatalog);++id)
        if(achievements.has(id))unlockedAchievements.push_back(id);
    refreshReplays();
    enter("stats");
}
std::wstring ExplorerMenu::statsSummary()const{
    unsigned campaign=0,perfect=0,victories=0,bestWaves=0,points=0;
    std::uint64_t kills=0,deaths=0,seconds=0,ammo=0,best=0;
    for(auto id:unlockedAchievements){
        points+=ExplorerAchievementCatalog[id].points;
        campaign+=id>=1&&id<=22;
        perfect+=id>=23&&id<=44;
    }
    for(const auto& run:survivalRuns){
        kills+=run.kills;deaths+=run.deaths;seconds+=run.seconds;ammo+=run.ammo;
        victories+=run.outcome==L"Victory";best=std::max(best,run.score);
        bestWaves=std::max(bestWaves,run.waves);
    }
    return L"Campaign missions cleared  "+std::to_wstring(campaign)+L" / 22   Perfect  "+std::to_wstring(perfect)+
           L"\nSurvival runs  "+std::to_wstring(survivalRuns.size())+L"   Wins  "+std::to_wstring(victories)+
           L"\nSurvival kills  "+std::to_wstring(kills)+L"   Deaths  "+std::to_wstring(deaths)+
           L"\nAmmo used  "+std::to_wstring(ammo)+L"   Minutes played  "+std::to_wstring(seconds/60)+
           L"\nBest score  "+std::to_wstring(best)+L"   Best waves  "+std::to_wstring(bestWaves)+
           L"\nAchievements  "+std::to_wstring(unlockedAchievements.size())+L" / "+std::to_wstring(std::size(ExplorerAchievementCatalog)-1)+
           L"   Points  "+std::to_wstring(points)+L"\nSaved replays  "+std::to_wstring(replayFiles.size());
}
std::vector<std::wstring> ExplorerMenu::rows()const{
 if(page=="colony isolation")return exterminationSaved?std::vector<std::wstring>{L"Continue Extermination - Level "+(exterminationSaved==4?std::wstring(L"3: Queen's Lair"):std::to_wstring(exterminationSaved)),L"Play Extermination",L"Load Extermination",L"Back"}:std::vector<std::wstring>{L"Play Extermination",L"Load Extermination",L"Back"};
 if(page=="extermination load"||page=="extermination save")return {exterminationSlot(0),exterminationSlot(1),exterminationSlot(2),L"Back"};
 if(page=="replays")return replayFiles.empty()?std::vector<std::wstring>{L"Back"}:std::vector<std::wstring>{L"Watch Replay",L"Screenshots",L"Older Replay",L"Newer Replay",L"Back"};
 if(page=="stats")return {L"Unlocked Achievements",L"All Achievements",L"Survival Run History",L"Back"};
 if(page=="unlocked achievements")return unlockedAchievements.empty()?std::vector<std::wstring>{L"Back"}:std::vector<std::wstring>{L"Previous achievement",L"Next achievement",L"Back"};
 if(page=="achievements")return {L"Previous achievement",L"Next achievement",L"Back"};
 if(page=="survival")return {std::wstring(L"Arena: ")+SurvivalArenaNames[survivalArena],L"Xeno Skulls",L"Play Classic Survival",L"Play Queen's Revenge!"};
 if(page=="survival-skulls")return {L"Scoring Skulls",L"Fun Skulls",L"Back"};
 if(isSkullList(page)){
  std::vector<std::wstring> rows;
  for(unsigned i:skullIds(page)){
   const auto multiplier=SurvivalSkullMultipliers[i];
   rows.push_back(std::wstring(SurvivalSkullNames[i])+L"  "+((survivalSkulls&(1u<<i))?L"ON":L"OFF")
       +(multiplier?L"  "+std::to_wstring(multiplier)+L"x":L"  No score"));
  }
  rows.push_back(L"Back");return rows;
 }
 if(page=="survival-history")return {L"Previous run",L"Next run",L"Back"};
 if(page=="survival results")return {L"Main menu"};
 if(page=="coop")return {L"Host game",L"Host IP: "+coopAddress,coopInvitation.empty()?L"Paste invitation code":L"Invitation code: pasted",L"Join game",std::wstring(L"Difficulty: ")+Difficulty[coopDifficulty],L"TCP port: "+coopPort,std::array{L"Mode: Campaign",L"Mode: Classic Survival",L"Mode: Queen's Revenge!"}[coopMode],std::wstring(L"Arena: ")+SurvivalArenaNames[survivalArena],L"Back"};
 if(page=="coop-lobby"){if(coopHost)return {L"Start session",L"Copy invitation code",L"Leave lobby"};return {coopReady?L"Ready - waiting":L"Ready",L"Leave lobby"};}
 if(page=="coop-pause")return {L"Resume Game",L"Scores",L"Options",L"Exit to main menu"};
 if(page=="coop-scores")return {L"Back"};
 if(page=="coop-results"){if(coopFinished)return {L"Return to co-op lobby"};return {L"Continue",L"Scores",L"Disconnect"};}
 if(page=="pause"&&exterminationGame)return {L"9mm Pistol",L"Shotgun",L"Flamethrower",L"Pulse Rifle",L"Smart Gun",L"Batteries",L"Mission",L"Save Game",L"Load Game",L"Options",L"Exit to main menu",L"Exit Game"};
 if(page=="pause")return {L"9mm Pistol",L"Shotgun",L"Flamethrower",L"Pulse Rifle",L"Smart Gun",L"Batteries",L"Mission",L"Load Game",L"Options",L"Exit to main menu",L"Exit Game"};
 if(page=="main"){
  std::vector<std::wstring> rows;
  if(hasGame||continueSlot>=0)rows.push_back(L"Continue");
  rows.insert(rows.end(),{L"Start",L"Load",L"Survival"});
  if(!hasGame&&isolationUnlocked())rows.push_back(L"Extermination");
  rows.push_back(L"Online Co-Op");
  if(!hasGame)rows.insert(rows.end(),{L"Stats",L"Replays"});
  rows.insert(rows.end(),{L"Options",L"Enter Password",L"Exit"});
  return rows;
 }
 if(page=="assessment"){if(!assessmentPassed)return {L"Retry Mission",L"Quit Game"};return {L"Continue game",L"Save game"};}
 if(page=="cheats"&&coopSession)return {L"Cheats unavailable in co-op",L"Back"};
 if(page=="cheats")return {godMode?L"Player indestructible: On":L"Player indestructible: Off",infiniteAmmo?L"Unlimited ammunition: On":L"Unlimited ammunition: Off",L"Gimme weapons",L"Starting level: "+std::to_wstring(startLevel+1),L"Back"};
 if(page=="password")return {L"Enter",L"Back"};
 if(page=="options"&&cheatsUnlocked&&!coopSession){auto r=std::vector<std::wstring>{L"SFX and music",L"Controls",L"Difficulty",L"Camera Sway",L"Graphics",L"Credits",L"Cheats",L"Back"};return r;}
 if(page=="options")return {L"Volume",L"Controls",(difficultyLocked||coopSession)?L"Difficulty (Locked)":L"Difficulty",sway?L"Camera Sway: On":L"Camera Sway: Off",L"Graphics",L"Credits",L"Back"};
 if(page=="controller")return {controllerEnabled?L"Controller: On":L"Controller: Off",L"Horizontal sensitivity: "+std::to_wstring(controllerSensitivity),L"Vertical sensitivity: "+std::to_wstring(controllerSensitivityY),controllerInvert?L"Invert look: On":L"Invert look: Off",L"Stick deadzone: "+std::to_wstring(controllerDeadzone)+L"%",controllerVibration?L"Vibration: "+std::to_wstring(controllerVibration)+L"%":L"Vibration: Off",L"Remap buttons",L"Test vibration",L"Restore controller defaults",controllerName,L"Back"};
 if(page=="controller-bindings"){std::vector<std::wstring> rows;for(unsigned i=0;i<controllerBindings.size();++i)rows.push_back(std::wstring(PadActions[i])+L": "+explorerButtonName(controllerBindings[i],playstationPrompts));rows.push_back(L"Restore default layout");rows.push_back(L"Back");return rows;}
 if(page=="volume")return {L"Sound Effects: "+std::to_wstring(sfx)+L"%",L"Disc Music: "+std::to_wstring(music)+L"%",headphoneAudio?L"Spatial audio: Headphones":L"Spatial audio: Stereo",L"Back"};
 if(page=="controls"){std::vector<std::wstring> r;for(unsigned i=0;i<bindings.size();++i)r.push_back(std::wstring(Actions[i])+L": "+keyName(bindings[i]));r.push_back(L"Mouse sensitivity: "+std::to_wstring(sensitivity));r.push_back(invert?L"Invert mouse: On":L"Invert mouse: Off");r.push_back(L"Restore defaults");r.push_back(L"Controller settings");r.push_back(L"Back");return r;}
 if(page=="difficulty"||page=="new-difficulty")return {Difficulty[0],Difficulty[1],Difficulty[2],L"Back"};
 if(page=="graphics"){return {std::array{L"Display: Windowed",L"Display: Borderless fullscreen",L"Display: Fullscreen"}[displayMode],std::wstring(L"Resolution: ")+ExplorerResolutions[resolution].label,renderQuality?L"Rendering: Full resolution":L"Rendering: Performance",std::wstring(L"Graphics API: ")+std::array<const wchar_t*,4>{L"DirectX 11",L"DirectX 12",L"Vulkan",L"Software"}[renderer],L"Field of view: "+std::to_wstring(fov)+L" degrees",std::wstring(L"Aspect ratio: ")+ExplorerAspects[aspect].label,frameLimit?L"Frame rate: "+std::to_wstring(frameLimit)+L" FPS":L"Frame rate: Unlimited",vsync?L"Vertical sync: On":L"Vertical sync: Off",renderer==3?L"Antialiasing: Off (GPU only)":antialiasing?L"Antialiasing: "+std::to_wstring(antialiasing)+L"x MSAA":L"Antialiasing: Off",L"Monitor: "+(monitors.empty()?std::wstring(L"Primary display"):monitors[explorerMonitorIndex(monitors,monitorDevice)].label),renderer==3?L"Texture filtering: Nearest (software)":textureFiltering?L"Texture filtering: Bilinear":L"Texture filtering: Nearest (original)",performanceDisplay?L"Performance display: On (F10)":L"Performance display: Off (F10)",L"Back"};}
 if(page=="load"||page=="save")return {slot(0),slot(1),slot(2),L"Back"};
 if(page=="confirm"||page=="new-confirm"||page=="restart-confirm")return {L"No",L"Yes"};return {L"Back"};
}
std::wstring ExplorerMenu::optionDescription(std::wstring_view label)const{
 if(page=="main")return mainMenuDescription(label,hasGame?std::string{}:continueCode);
 if(page=="results")return resultsReady()?L"Continue to the mission result options.":L"Reveal the full mission statistics.";
 if(page=="credits")return L"Scroll to see the credits: Alien Trilogy: Remastered, then the original game.";
 if(isSkullList(page)){
  const auto ids=skullIds(page);
  if(selection<ids.size())return SkullDescriptions[ids[selection]];
 }
 if(page=="colony isolation"&&label==L"Back")return L"Return to the main menu.";
 if(page=="extermination load")return label==L"Back"?L"Return to the previous menu.":L"Play the Extermination level saved in this slot.";
 if(page=="extermination save")return label==L"Back"?L"Return to the previous menu.":L"Save this Extermination level to this slot (you restart it from its beginning).";
 if(page=="pause"&&exterminationGame&&(label==L"Save Game"||label==L"Load Game"))return label==L"Save Game"?L"Save this Extermination level to one of Extermination's own slots.":L"Load a level from Extermination's own save slots.";
 if(label==L"Load Extermination")return L"Play an Extermination level from one of its three save slots.";
 if(label==L"Back")return page=="map"?L"Close the map and return to the game.":L"Return to the previous menu.";
 if(page=="controls"&&selection<bindings.size())return L"Choose a new keyboard or mouse input for this action.";
 if(page=="controller-bindings"&&selection<controllerBindings.size())return L"Assign a different controller input to this action.";
 if(page=="load")return L"Load campaign progress and equipment from this slot.";
 if(page=="save")return L"Save the next mission and your carried equipment to this slot.";
 if(page=="difficulty"&&(difficultyLocked||coopSession))return L"Difficulty stays locked for the current campaign.";
 if(page=="new-confirm")return selection?L"Replace the current mission and choose a difficulty for a new campaign.":L"Keep your current campaign.";
 if(page=="restart-confirm")return selection?L"Restart this mission from its entrance.":L"Keep playing from your current position.";
 if(page=="confirm")return selection?L"Close the game. Progress since your last save will be lost.":L"Cancel and return to the previous screen.";
 if(page=="coop-results"&&label==L"Continue")return L"The host advances after a pass or retries a failed mission.";
 if(page=="coop"&&selection==2)return L"Paste the invitation code shared by the host.";
 if(page=="coop"&&selection==4)return L"Set the difficulty for the next hosted campaign.";
 if(page=="controller"&&selection==9)return L"Shows the currently detected controller.";
 if(page=="pause"&&selection<5)return L"Inspect this weapon and equip it if you own it.";
 if(auto shared=mainMenuDescription(label==L"Load Game"?L"Load":label==L"Exit Game"?L"Exit":label,continueCode);!shared.empty())return shared;

 static constexpr std::pair<std::wstring_view,const wchar_t*> descriptions[]={
  {L"Play Extermination",L"Clear three Colony decks, recover crew ID tags and defeat the Queen."},
  {L"Continue Extermination",L"Pick up from the Extermination level you reached (its own save, apart from the story and co-op)."},
  {L"Play Classic Survival",L"Survive ten waves and defeat the final Queen in the selected arena."},
  {L"Arena: ",L"Cycle between Colony Hive, Foundry Lair, and Derelict Lair."},
  {L"Play Queen's Revenge!",L"Start with all five weapons and defeat five Queens in one wave."},
  {L"Xeno Skulls",L"Choose modifiers for your next Survival run."},
  {L"Scoring Skulls",L"Increase the challenge for a higher final score."},
  {L"Fun Skulls",L"Change the rules for fun and disable scoring for the run."},
  {L"Previous run",L"View an older Survival run."},{L"Next run",L"View a more recent Survival run."},
  {L"Unlocked Achievements",L"Browse the achievements you have earned."},
  {L"All Achievements",L"Browse the full achievement list and its requirements."},
  {L"Survival Run History",L"Review kills, accuracy, waves, and score for each recorded run."},
  {L"Previous achievement",L"View the previous achievement in this list."},
  {L"Next achievement",L"View the next achievement in this list."},
  {L"Watch Replay",L"Play the selected recorded mission or Survival run."},
  {L"Screenshots",L"Open the screenshot folder for this replay."},
  {L"Older Replay",L"Select an older recording."},{L"Newer Replay",L"Select a more recent recording."},
  {L"Host game",L"Open a lobby using the selected mode and arena."},{L"Mode: ",L"Choose the campaign or a Survival mode."},
  {L"Host IP: ",L"Enter the host's IP address before joining."},
  {L"Join game",L"Connect using the host's address, TCP port, and invitation code."},
  {L"TCP port: ",L"Set the connection port. Both players must use the same port."},
  {L"Start session",L"Start the selected mode once your teammate is ready."},
  {L"Copy invitation code",L"Copy the session invitation to share with your teammate."},
  {L"Leave lobby",L"Close your connection and return to the main menu."},
  {L"Ready - waiting",L"You are ready. The host can start the campaign."},
  {L"Ready",L"Tell the host you are ready to play."},
  {L"Scores",L"View the team's points, kills, and deaths."},
  {L"Return to co-op lobby",L"Return to the lobby with your teammate after the campaign."},
  {L"Disconnect",L"Leave the co-op session and return to the main menu."},
  {L"Batteries",L"Check available batteries and use one on a nearby compatible panel."},
  {L"Mission",L"Review mission objectives and the map if you have the Auto Mapper."},
  {L"Exit to main menu",L"End the current session and return to the title screen."},
  {L"Main menu",L"Return to the title screen."},
  {L"Retry Mission",L"Restart the failed mission from its entrance."},
  {L"Quit Game",L"Open the confirmation to quit Alien Trilogy."},
  {L"Continue game",L"Proceed to the next mission."},
  {L"Save game",L"Choose a slot for your mission progress and equipment."},
  {L"Volume",L"Adjust sound effects, music, and headphone audio."},
  {L"SFX and music",L"Adjust sound effects, music, and headphone audio."},
  {L"Controls",L"Set keyboard, mouse, and controller inputs."},
  {L"Difficulty (Locked)",L"Difficulty stays locked for the current campaign."},
  {L"Difficulty",L"Choose the default difficulty for a new campaign."},
  {L"Camera Sway",L"Toggle camera movement while walking or running."},
  {L"Graphics",L"Set the display mode, rendering quality, and frame rate."},
  {L"Credits",L"Read the credits for Alien Trilogy: Remastered and the original game."},
  {L"Cheats unavailable in co-op",L"Cheats are disabled during online co-op."},
  {L"Cheats",L"Manage the cheats unlocked by password."},
  {L"Player indestructible: ",L"Toggle protection from damage."},
  {L"Unlimited ammunition: ",L"Toggle unlimited ammunition."},
  {L"Gimme weapons",L"Grant all weapons."},
  {L"Starting level: ",L"Choose which mission a new campaign starts on."},
  {L"Enter",L"Submit the entered level password or cheat code."},
  {L"Controller: ",L"Enable or disable controller input."},
  {L"Horizontal sensitivity: ",L"Adjust how fast the right stick turns the camera."},
  {L"Vertical sensitivity: ",L"Adjust how fast the right stick looks up and down."},
  {L"Invert look: ",L"Reverse vertical camera movement on the right stick."},
  {L"Stick deadzone: ",L"Set how far a stick must move before it responds."},
  {L"Vibration: ",L"Adjust controller rumble strength."},
  {L"Remap buttons",L"Choose a controller input for each action."},
  {L"Test vibration",L"Play a short rumble on the connected controller."},
  {L"Restore controller defaults",L"Reset controller settings and bindings."},
  {L"Restore default layout",L"Reset controller bindings to the default layout."},
  {L"Sound Effects: ",L"Adjust the volume of effects, weapons, and menu sounds."},
  {L"Disc Music: ",L"Adjust the volume of the original soundtrack."},
  {L"Spatial audio: ",L"Switch between headphone spatial audio and stereo."},
  {L"Mouse sensitivity: ",L"Adjust how fast the camera turns when you move the mouse."},
  {L"Invert mouse: ",L"Reverse vertical mouse look."},
  {L"Restore defaults",L"Reset the controls to their default settings."},
  {L"Controller settings",L"Adjust controller aim, vibration, and button bindings."},
  {L"Acid Reign",L"Select the easiest campaign difficulty."},
  {L"Raging Terror",L"Select the standard campaign difficulty."},
  {L"Xenomania",L"Select the hardest campaign difficulty."},
  {L"Display: ",L"Switch between windowed, borderless fullscreen, and fullscreen."},
  {L"Resolution: ",L"Choose the game's display resolution."},
  {L"Rendering: ",L"Choose full-resolution rendering or reduce rendering cost in Performance mode."},
  {L"Graphics API: ",L"Choose a renderer. Restart the game to apply the change."},
  {L"Field of view: ",L"Adjust how much of the world is visible. HUD size stays the same."},
  {L"Aspect ratio: ",L"Choose the shape of the gameplay area."},
  {L"Frame rate: ",L"Set a maximum frame rate or allow unlimited FPS."},
  {L"Vertical sync: ",L"Sync presentation to the monitor to reduce tearing."},
  {L"Antialiasing: ",L"Smooth geometry edges with MSAA on a GPU renderer."},
  {L"Monitor: ",L"Choose which monitor displays the game."},
  {L"Texture filtering: ",L"Choose sharp original pixels or smoother bilinear textures."},
  {L"Performance display: ",L"Show or hide FPS and frame time. You can also press F10."},
 };
 for(const auto& [option,description]:descriptions)
  if(option.ends_with(L": ")?label.starts_with(option):label==option)return description;
 if(label.starts_with(L"Camera Sway: "))return L"Toggle camera movement while walking or running.";
 return {};
}
ExplorerMenu::Command ExplorerMenu::key(unsigned key){
 if(controllerListening>=0){soundEvent=-1;if(key==VK_ESCAPE){controllerListening=-1;message.clear();}return None;}
 soundEvent=-1;auto oldPage=page;unsigned oldSelection=selection;bool binding=listening>=0;auto command=activate(key);
 if(key==VK_UP||key==VK_DOWN){if(selection!=oldSelection){soundEvent=0;if(page!="credits")message.clear();}}
 else if(key==VK_LEFT||key==VK_RIGHT){if(command==Apply||command==Display)soundEvent=0;}
 else if(key==VK_RETURN||binding||(key==VK_ESCAPE&&(page!=oldPage||!open)))soundEvent=1;
 return command;
}
ExplorerMenu::Command ExplorerMenu::activate(unsigned key){
 if(page=="main"&&key==VK_F1){enter("options");return None;}
 if(page=="coop"&&coopEditing>=0){auto& text=coopEditing==0?coopAddress:coopPort;if(key==VK_BACK&&!text.empty())text.pop_back();if(key==VK_RETURN||key==VK_ESCAPE){coopEditing=-1;message.clear();}return None;}

 if(coopSession&&(page=="cheats"||page=="password")){if(key==VK_ESCAPE||key==VK_RETURN)back();return None;}
 if(page=="password"){if(key==VK_BACK){if(!passwordText.empty())passwordText.pop_back();return None;}if(key==VK_ESCAPE){back();return None;}if(key==VK_RETURN){if(selection==1)back();else return LoadPassword;return None;}}
 if(coopSession&&!coopHost&&(page=="results"||page=="coop-results"||page=="coop-scores"))return None;
 if(page=="results"){if(!resultsReady())resultsRevealed=true;else{page=coopSession?"coop-results":"assessment";selection=0;message.clear();}return None;}
 if(listening>=0){if(key!=VK_ESCAPE){auto it=std::find(bindings.begin(),bindings.end(),key);if(it!=bindings.end())std::swap(*it,bindings[listening]);bindings[listening]=key;persist();}listening=-1;message.clear();return Apply;}
 auto list=rows();if(page=="credits"&&(key==VK_UP||key==VK_DOWN)){if(key==VK_DOWN)++selection;else if(selection)--selection;return None;}if(key==VK_ESCAPE){if(page=="assessment"||page=="coop-results"||page=="coop-lobby")return None;back();return open?None:Resume;}if(key==VK_UP){selection=(selection+list.size()-1)%list.size();return None;}if(key==VK_DOWN){selection=(selection+1)%list.size();return None;}
 bool adjust=key==VK_LEFT||key==VK_RIGHT;int direction=key==VK_LEFT?-1:1;if(!adjust&&key!=VK_RETURN)return None;
 if(page=="coop"){if(selection==6){coopMode=(coopMode+3+direction)%3;return None;}if(selection==7){survivalArena=(survivalArena+3+direction)%3;return None;}if(selection==4){coopDifficulty=(coopDifficulty+3+direction)%3;return None;}if(adjust)return None;if(selection==0)return CoopHost;if(selection==2)return CoopPasteInvite;if(selection==3)return CoopJoin;if(selection==1||selection==5){coopEditing=selection==1?0:1;message=selection==1?L"Type the host IPv4 address. Enter to finish.":L"Type TCP port (1-65535). Enter to finish.";return None;}back();return None;}
 if(page=="coop-lobby"){if(adjust)return None;if(coopHost&&selection==1)return CoopCopyInvite;if(selection==(coopHost?2u:1u))return CoopLeave;return coopHost?CoopStart:CoopReady;}
 if(page=="coop-pause"){if(adjust)return None;if(selection==0){open=false;return Resume;}if(selection==1)enter("coop-scores");else if(selection==2)enter("options");else return CoopLeave;return None;}
 if(page=="coop-results"){if(adjust)return None;if(!coopHost)return selection==1?CoopLeave:None;if(coopFinished)return CoopAdvance;if(selection==2)return CoopLeave;if(selection==1){enter("coop-scores");return None;}return CoopAdvance;}
 if(page=="cheats"&&selection==3){startLevel=(startLevel+35+direction)%35;return None;}
 if(page=="volume"&&selection==2){headphoneAudio=!headphoneAudio;persist();return Apply;}
 if(page=="volume"&&selection<2){auto& value=selection?music:sfx;value=unsigned(std::clamp(int(value)+direction*5,0,100));persist();return Apply;}
 if(page=="controls"){if(selection<bindings.size()&&!adjust){listening=int(selection);message=L"Press a key or mouse button. Esc cancels.";return None;}if(selection==bindings.size())sensitivity=unsigned(std::clamp(int(sensitivity)+direction,1,10));else if(selection==bindings.size()+1)invert=!invert;else if(selection==bindings.size()+2&&!adjust)defaults();else if(selection==bindings.size()+3&&!adjust){enter("controller");return None;}else if(selection==bindings.size()+4&&!adjust)back();persist();return Apply;}
 if(page=="controller"){if(selection==0)controllerEnabled=!controllerEnabled;else if(selection==1)controllerSensitivity=unsigned(std::clamp(int(controllerSensitivity)+direction,1,10));else if(selection==2)controllerSensitivityY=unsigned(std::clamp(int(controllerSensitivityY)+direction,1,10));else if(selection==3)controllerInvert=!controllerInvert;else if(selection==4)controllerDeadzone=unsigned(std::clamp(int(controllerDeadzone)+direction,5,40));else if(selection==5)controllerVibration=unsigned(std::clamp(int(controllerVibration)+direction*10,0,100));else if(selection==6&&!adjust){enter("controller-bindings");return None;}else if(selection==7&&!adjust){controllerRumbleTest=true;message=controllerVibration?L"Testing vibration":L"Vibration is off";}else if(selection==8&&!adjust){controllerBindings=ExplorerController::DefaultBindings;controllerSensitivity=controllerSensitivityY=3;controllerDeadzone=18;controllerInvert=false;controllerVibration=70;controllerEnabled=true;}else if(selection==10&&!adjust)back();persist();return Apply;}
 if(page=="controller-bindings"){if(selection<controllerBindings.size()&&adjust){constexpr unsigned candidates[]={0,1,2,3,4,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,32,33};auto it=std::find(std::begin(candidates),std::end(candidates),controllerBindings[selection]);int index=it==std::end(candidates)?0:int(it-std::begin(candidates)),count=int(std::size(candidates));controllerListening=int(selection);bindController(candidates[(index+count+direction)%count]);return Apply;}if(selection<controllerBindings.size()&&!adjust){controllerListening=int(selection);message=L"Press a button/trigger. "+explorerButtonName(1,playstationPrompts)+L" or Esc cancels.";}else if(selection==9&&!adjust){controllerBindings=ExplorerController::DefaultBindings;persist();message=L"Default layout restored";}else if(selection==10&&!adjust)back();return None;}
 if(page=="graphics"&&selection==6){constexpr unsigned rates[]={30,60,120,144,175,0};auto it=std::find(std::begin(rates),std::end(rates),frameLimit);int index=it==std::end(rates)?1:int(it-std::begin(rates));frameLimit=rates[(index+6+direction)%6];persist();return Apply;}
 if(page=="graphics"&&selection==11){performanceDisplay=!performanceDisplay;persist();return Apply;}
 if(page=="graphics"&&selection==10){if(renderer==3){message=L"Bilinear filtering requires DirectX 11, DirectX 12 or Vulkan";return None;}textureFiltering=!textureFiltering;persist();return Apply;}
 if(page=="graphics"&&selection==9){refreshMonitors();unsigned current=explorerMonitorIndex(monitors,monitorDevice);monitorDevice=monitors[(int(current)+int(monitors.size())+direction)%int(monitors.size())].id;persist();return Display;}
 if(page=="graphics"&&selection==8){if(renderer==3){message=L"MSAA requires DirectX 11, DirectX 12 or Vulkan";return None;}auto it=std::find(antialiasingModes.begin(),antialiasingModes.end(),antialiasing);int index=it==antialiasingModes.end()?0:int(it-antialiasingModes.begin()),count=int(antialiasingModes.size());if(count)antialiasing=antialiasingModes[(index+count+direction)%count];persist();return Apply;}
 if(page=="graphics"&&selection==7){vsync=!vsync;persist();return Apply;}
 if(page=="graphics"&&selection==5){aspect=(aspect+int(ExplorerAspects.size())+direction)%ExplorerAspects.size();persist();return Apply;}
 if(page=="graphics"&&selection==4){fov=unsigned(std::clamp(int(fov)+direction*5,50,110));persist();return Apply;}
 if(page=="graphics"&&selection<4){if(selection==0)displayMode=(displayMode+3+direction)%3;else if(selection==1)resolution=(resolution+int(ExplorerResolutions.size())+direction)%ExplorerResolutions.size();else if(selection==2)renderQuality=!renderQuality;else{renderer=(renderer+4+direction)%4;message=L"Restart the game to apply the graphics API.";}persist();return selection==3?Apply:Display;}
 if(page=="survival"&&selection==0){
  const auto count=std::size(SurvivalArenaNames);
  survivalArena=unsigned((survivalArena+(direction<0?count-1:1))%count);
  return None;
 }
 if(page=="options"&&selection==3&&adjust){sway=!sway;persist();return Apply;}
 if(page=="cheats"&&!coopSession&&selection<2&&adjust){if(selection==0)godMode=!godMode;else infiniteAmmo=!infiniteAmmo;return CheatsChanged;}
 if(adjust)return None;
 if(page=="pause"){if(selection<5){constexpr unsigned weapons[]={0,1,4,2,3};return Command(Weapon0+weapons[selection]);}const auto label=list[selection];if(label==L"Batteries"){message=std::to_wstring(batteries)+L" batteries available";return UseBattery;}else if(label==L"Mission")enter("mission");else if(label==L"Save Game")enter("extermination save");else if(label==L"Load Game")enter(exterminationGame?"extermination load":"load");else if(label==L"Options")enter("options");else if(label==L"Exit to main menu")return MainMenu;else if(label==L"Exit Game")enter("confirm");}
 else if(page=="extermination load"||page=="extermination save"){if(selection==3)back();else{if(page=="extermination load"&&!exterminationSlotLevel(selection)){message=L"This slot is empty.";return None;}return Command((page=="extermination load"?ExterminationLoad1:ExterminationSave1)+selection);}}
 else if(page=="assessment"){if(!assessmentPassed){if(selection)enter("confirm");else{open=false;return Restart;}return None;}if(selection==1)enter("save");else if(selection==2)enter("confirm");else{open=false;return NextMission;}}
 else if(page=="cheats"){if(selection==0)godMode=!godMode;else if(selection==1)infiniteAmmo=!infiniteAmmo;else if(selection==4){back();return None;}return CheatsChanged;}
 else if(page=="main"){auto label=list[selection];if(label==L"Continue"){if(hasGame){open=false;return Resume;}refreshContinue();if(continueSlot<0){selection=0;message=L"No valid progress save is available.";return None;}return Command(Load1+continueSlot);}if(label==L"Replays"){showReplays();return None;}if(label==L"Stats"){showStats();return None;}if(label==L"Extermination"){refreshExtermination();enter("colony isolation");return None;}if(label==L"Survival"){enter("survival");return None;}if(label==L"Online Co-Op"){enter("coop");return None;}if(label==L"Options"){enter("options");return None;}if(label==L"Enter Password"){passwordText.clear();enter("password");return None;}if(label==L"Start"){if(hasGame){enter("new-confirm");return None;}enter("new-difficulty");return None;}if(label==L"Load")enter("load");else if(label==L"Exit")enter("confirm");}
 else if(page=="options"){if(cheatsUnlocked&&!coopSession&&selection==6){enter("cheats");return None;}constexpr const char* pages[]={"volume","controls","difficulty","", "graphics","credits"};if(selection==3){sway=!sway;persist();return Apply;}if(selection<6)enter(pages[selection]);else back();}
 else if(page=="survival results")return MainMenu;
 else if(page=="replays"){
  if(replayFiles.empty()||selection==4)back();
  else if(selection==0)return ReplayWatch;
  else if(selection==1)return ReplayScreenshots;
  else if(selection==2)replayIndex=std::min(replayIndex+1,unsigned(replayFiles.size()-1));
  else if(replayIndex)--replayIndex;
 }
 else if(page=="stats"){
  if(selection==0)enter("unlocked achievements");
  else if(selection==1)enter("achievements");
  else if(selection==2)enter("survival-history");
  else back();
 }
 else if(page=="unlocked achievements"){
  const auto count=unlockedAchievements.size();
  if(!count||selection==2)back();
  else unlockedAchievementIndex=unsigned((unlockedAchievementIndex+(selection==0?count-1:1))%count);
 }
 else if(page=="achievements"){
  constexpr unsigned count=std::size(ExplorerAchievementCatalog)-1;
  if(selection==0)achievementIndex=(achievementIndex+count-2)%count+1;
  else if(selection==1)achievementIndex=achievementIndex%count+1;
  else back();
 }
 else if(page=="colony isolation"){const auto label=list[selection];if(label==L"Play Extermination")return IsolationStart;if(label.starts_with(L"Continue Extermination"))return IsolationContinue;if(label==L"Load Extermination"){enter("extermination load");return None;}back();}
 else if(page=="survival"){
  if(selection==2)return SurvivalStart;
  if(selection==3)return QueensRevengeStart;
  if(selection==1)enter("survival-skulls");
 }
 else if(page=="survival-skulls"){
  if(selection==0)enter("scoring-skulls");
  else if(selection==1)enter("fun-skulls");
  else back();
 }
 else if(isSkullList(page)){
  const auto ids=skullIds(page);
  if(selection<ids.size())survivalSkulls^=1u<<ids[selection];
  else back();
 }
 else if(page=="survival-history"){
  const auto& runs=survivalRuns;
  if(selection==2)back();
  else if(!runs.empty()){
   if(selection==0)survivalRunIndex=std::min(survivalRunIndex+1,unsigned(runs.size()-1));
   else if(survivalRunIndex)--survivalRunIndex;
  }
 }
 else if(page=="new-difficulty"){if(selection<3){difficulty=selection;persist();open=false;return Start;}back();}
 else if(page=="difficulty"){if(selection<3){if(difficultyLocked||coopSession){message=L"Difficulty is locked for this campaign.";return None;}difficulty=selection;persist();message=std::wstring(Difficulty[difficulty])+L" selected for the next mission";return Apply;}back();}
 else if(page=="load"||page=="save"){if(selection==3)back();else return Command((page=="load"?Load1:Save1)+selection);}
 else if(page=="confirm"){if(selection)return Quit;back();}
 else if(page=="restart-confirm"){if(selection){open=false;return Restart;}back();}
 else if(page=="new-confirm"){if(selection){enter("new-difficulty");}else back();}
 else back();return None;
}
bool ExplorerMenu::adjustable(unsigned row)const{
 if(page=="graphics")return row<12;
 if(page=="volume")return row<3;
 if(page=="controller")return row<6;
 if(page=="controls")return row==bindings.size()||row==bindings.size()+1;
 if(page=="options")return row==3;
 if(page=="cheats")return !coopSession&&(row<2||row==3);
 if(page=="survival")return row==0;
 if(page=="coop")return row==4||row==6||row==7;
 return false;
}
// The arrows of a row: one column either side of the page's widest adjustable row (with its selection marker), so they
// line up down the page. The column is rounded out in steps and only ever widens while the page is up, so it stays put
// under the mouse while a value's text changes length.
ExplorerMenu::Arrows ExplorerMenu::arrows(int width,int rowTop,int line,int height)const{
 const double fontScale=page=="main"?std::min(height/600.,line/16.):height/600.;
 const auto list=rows();int widest=0;
 for(unsigned i=0;i<list.size();++i)if(adjustable(i))widest=std::max(widest,originalFont.width(L"> "+list[i],fontScale));
 const int step=std::max(1,line*2);widest=(widest+step-1)/step*step;
 if(arrowColumn.page!=page||arrowColumn.width!=width||arrowColumn.height!=height)arrowColumn={page,width,height,0};
 widest=arrowColumn.widest=std::max(arrowColumn.widest,widest);
 const int size=std::max(3,int(line*.22)),gap=std::max(6,line/2);
 return {(width-widest)/2-gap-size,(width+widest)/2+gap+size,rowTop+line/2,size};
}
// -1 or 1 when (x, y) is on the left or right arrow of an adjustable row, else 0.
int ExplorerMenu::arrowAt(int x,int y,int width,int height,unsigned row)const{
 if(!adjustable(row))return 0;
 const auto list=rows();if(row>=list.size())return 0;
 const auto [top,line]=menuRows(page,height,list.size());
 const int rowTop=top+int(row)*line+(page=="survival"&&row>0?arenaPreviewHeight(height):0);
 const auto a=arrows(width,rowTop,line,height);
 const int reach=std::max(a.size*2,line/2);
 if(std::abs(y-a.middle)>line/2)return 0;
 if(std::abs(x-a.left)<=reach)return -1;
 if(std::abs(x-a.right)<=reach)return 1;
 return 0;
}
int ExplorerMenu::rowAt(int x,int y,int width,int height)const{
 auto list=rows();if(page=="password"){auto fit=explorerFit(width,height,4,3);double scale=fit.height/240.;int top=fit.y+int(155*scale),line=std::max(1,int(20*scale)),row=(y-top)/line;return y>=top&&x>=fit.x&&x<fit.x+fit.width&&row>=0&&row<int(list.size())?row:-1;}if(page=="assessment"){auto fit=explorerFit(width,height,4,3);double scale=fit.height/240.;int top=fit.y+int(145*scale),line=std::max(1,int(20*scale)),row=(y-top)/line;return y>=top&&x>=fit.x&&x<fit.x+fit.width&&row>=0&&row<int(list.size())?row:-1;}if(page=="pause"){double scale=height/480.;int row=(y-int(10*scale))/std::max(1,int(23*scale));return y>=int(10*scale)&&x>=int(10*scale)&&x<=int(174*scale)&&row>=0&&row<int(list.size())?row:-1;}
 const auto [top,line]=menuRows(page,height,list.size());
 if(page=="survival"&&y>=top+line){
  if(y<top+line+arenaPreviewHeight(height))return -1;
  y-=arenaPreviewHeight(height);
 }
 int row=(y-top)/line;return y>=top&&row>=0&&row<int(list.size())&&x>width/8&&x<width*7/8?row:-1;
}
bool ExplorerMenu::hover(int x,int y,int width,int height){soundEvent=-1;if(listening>=0||controllerListening>=0||coopEditing>=0||page=="results"||page=="credits"||coopSession&&!coopHost&&(page=="coop-results"||page=="coop-scores"))return false;int row=rowAt(x,y,width,height);if(row<0||unsigned(row)==selection)return false;selection=unsigned(row);message.clear();soundEvent=0;return true;}
ExplorerMenu::Command ExplorerMenu::click(int x,int y,int width,int height,bool dragging){soundEvent=-1;if(page=="results")return key(VK_RETURN);if(listening>=0)return key(VK_LBUTTON);int row=rowAt(x,y,width,height);if(row>=0){selection=unsigned(row);
  if(!dragging)if(int step=arrowAt(x,y,width,height,unsigned(row))){const auto before=rows();auto command=key(step<0?VK_LEFT:VK_RIGHT);if(soundEvent<0&&rows()!=before)soundEvent=0;return command;}
 if(page=="graphics"&&row==4){int start=width/3,span=std::max(1,width/3);auto value=unsigned(std::clamp(50+int(std::lround(double(x-start)*60/span/5))*5,50,110));if(value==fov)return None;fov=value;soundEvent=0;persist();return Apply;}return key(VK_RETURN);}return None;}
void ExplorerMenu::drawSurvivalArena(HDC dc,int width,int top,int height){
 auto& preview=arenaPreviews[survivalArena];
 if(preview.pixels.empty()&&!preview.width){
  try{
   ExplorerLevelConfig cfg;
   if(!explorerLoadSurvivalConfig(assets/"GameData"/SurvivalArenaFolders[survivalArena],cfg))
    throw std::runtime_error("Missing Survival arena");
   const auto chunks=explorerChunks(cfg.map);
   const auto& bytes=chunks.at("MAP0");
   auto word=[&](size_t p){return unsigned(bytes.at(p))|(unsigned(bytes.at(p+1))<<8);};
   const size_t offset=36+word(0)*8ull+word(2)*20ull;
   const unsigned w=word(4),h=word(6);
   if(!w||!h||w!=cfg.width||h!=cfg.height||w>127||h>127||offset+w*h*16ull>bytes.size())
    throw std::runtime_error("Invalid arena preview grid");
   preview={w,h,std::vector<unsigned>(w*h)};
   for(unsigned z=0;z<h;++z)for(unsigned x=0;x<w;++x){
    const auto cell=offset+(z*w+x)*16;
    const auto attr=bytes[cell+10];
    const bool wall=attr>=20&&attr<45;
    preview.pixels[(h-1-z)*w+x]=wall?0x0b261b:((x+z)%2?0x47ad83:0x358d6c);
   }
   for(const auto& door:cfg.doors)if(door.x<w&&door.z<h)
    preview.pixels[(h-1-door.z)*w+door.x]=0xffad52;
   if(cfg.startX<w&&cfg.startZ<h)preview.pixels[(h-1-cfg.startZ)*w+cfg.startX]=0xffffff;
  }catch(const std::exception&){preview.width=1;}
 }
 if(preview.pixels.empty()){
  originalFont.box(dc,L"Arena map unavailable",RECT{width/8,top,width*7/8,top+height},height/230.,0xffffff,true);
  return;
 }
 const auto fit=explorerFit(width*3/4,std::max(1,height-16),preview.width,preview.height);
 RECT border{width/8+fit.x-2,top+8+fit.y-2,width/8+fit.x+fit.width+2,top+8+fit.y+fit.height+2};
 auto brush=CreateSolidBrush(RGB(76,204,146));FillRect(dc,&border,brush);DeleteObject(brush);
 BITMAPINFO info{};
 info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=preview.width;
 info.bmiHeader.biHeight=-int(preview.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
 SetStretchBltMode(dc,COLORONCOLOR);
 StretchDIBits(dc,border.left+2,border.top+2,fit.width,fit.height,0,0,preview.width,preview.height,preview.pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
 const int start=std::max(0,int(height*.04));
 originalFont.box(dc,L"White: start\nAmber: elevators",RECT{border.right+8,top+start,width-5,top+height},std::clamp(height/250.,.65,1.5),0xffffff,false);
}
void ExplorerMenu::draw(HDC dc,int width,int height){
 const auto list=rows();
 const auto description=optionDescription(selection<list.size()?std::wstring_view(list[selection]):std::wstring_view{});
 auto drawDescription=[&](const std::wstring& text){
  const int bottom=height-(usingController&&message.empty()?46:8);
  originalFont.box(dc,text,RECT{width/12,bottom-38,width*11/12,bottom},height/(usingController?800.:700.),0xffffff,true);
 };
 if(page=="results"||page=="assessment"||page=="password"){
  RECT all{0,0,width,height};FillRect(dc,&all,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));auto fit=explorerFit(width,height,4,3);double sx=fit.width/320.,sy=fit.height/240.,fontScale=fit.height/320.;auto x=[&](double n){return fit.x+int(n*sx);};auto y=[&](double n){return fit.y+int(n*sy);};
  auto& image=resultsImage;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=image.width;info.bmiHeader.biHeight=-int(image.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;SetStretchBltMode(dc,COLORONCOLOR);StretchDIBits(dc,fit.x,fit.y,fit.width,fit.height,0,0,image.width,image.height,image.pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
  auto centered=[&](const std::wstring& text,double top,unsigned color){originalFont.draw(dc,text,fit.x+(fit.width-originalFont.width(text,fontScale))/2,y(top),fontScale,color);};
  if(page=="results"){
   centered(L"MISSION ASSESSMENT",24,0x33ff33);constexpr const wchar_t* labels[]={L"Aliens:",L"Secrets:",L"Mission:"};unsigned elapsed=unsigned(GetTickCount64()-resultsStarted);
   for(unsigned row=0;row<3;++row){unsigned value=resultsReady()?resultPercent[row]:resultPercent[row]*std::min(1500u,elapsed>row*1500?elapsed-row*1500:0u)/1500;unsigned color=value>=(assessmentRequired?assessmentRequired:50)?0x33ff33:0xff2020;originalFont.draw(dc,labels[row],x(26),y(70+row*48),fontScale,color);originalFont.draw(dc,std::to_wstring(value)+L"%",x(110),y(60+row*48),fontScale,color);
    for(unsigned segment=0;segment<40;++segment){unsigned red=segment<20?255:unsigned((39-segment)*255/19),green=segment<20?segment*255/19:255;int baseline=85+row*48,height=4+22*segment/39;RECT bar{x(110+segment*4),y(baseline-height),x(112+segment*4),y(baseline)};auto brush=CreateSolidBrush(segment<value*40/100?RGB(red,green,0):RGB(0,8,0));FillRect(dc,&bar,brush);DeleteObject(brush);RECT dot{x(108+segment*4),y(baseline+3),x(109+segment*4),y(baseline+4)};brush=CreateSolidBrush(RGB(175,190,55));FillRect(dc,&dot,brush);DeleteObject(brush);}
   }
  }else if(page=="password"){
   centered(L"ENTER PASSWORD",35,0x33ff33);centered(passwordText.substr(0,16),75,0x33ff33);centered(passwordText.size()>16?passwordText.substr(16):L"",95,0x33ff33);centered(message,120,0xff3030);auto list=rows();for(unsigned i=0;i<list.size();++i)centered(list[i],155+i*20,i==selection?0xffffff:0x33ff33);
  }else{
   if(assessmentPassed&&!levelPassword.empty()){centered(L"LEVEL PASSWORD",49,0x33ff33);centered(levelPassword.substr(0,16),79,0x33ff33);centered(levelPassword.substr(16,16),99,0x33ff33);}else
   centered(assessmentPassed?L"MISSION COMPLETE":L"MISSION FAILED",49,0x33ff33);if(!assessmentPassed||levelPassword.empty())centered(L"Mission: "+std::to_wstring(resultPercent[2])+L"%",80,assessmentPassed?0x33ff33:0xff2020);if(!assessmentPassed)centered(L"Required: "+std::to_wstring(assessmentRequired)+L"%",99,0xff2020);
   auto list=rows();for(unsigned i=0;i<list.size();++i)centered(list[i],145+i*20,i==selection?0xffffff:0x33ff33);
  }
  if(message.empty()||page!="password")drawDescription(message.empty()?description:message);
  if(usingController)drawPrompts(dc,width,height-int(23*sy),sy*.65,page=="results"?std::vector<ExplorerPrompt>{{0,L"Continue"}}:page=="password"?std::vector<ExplorerPrompt>{{0,L"Enter"},{1,L"Back"}}:std::vector<ExplorerPrompt>{{100,L"Select"},{0,L"Confirm"}});return;
 }

 if(page=="pause"){
  RECT all{0,0,width,height};FillRect(dc,&all,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));double scale=height/480.;int row=int(23*scale),margin=int(10*scale),column=int(164*scale);auto list=rows();for(unsigned i=0;i<list.size();++i){RECT r{margin,margin+int(i)*row,margin+column,margin+int(i+1)*row-2};auto brush=CreateSolidBrush(i==selection?RGB(16,94,40):RGB(0,32,12));FillRect(dc,&r,brush);DeleteObject(brush);unsigned color=i==selection?0x33ff66:0x188234;if(i<5){constexpr unsigned ids[]={0,1,4,2,3};if(ids[i]==equipped)color=0xebebeb;}originalFont.draw(dc,list[i],margin+int(4*scale),r.top+2,scale,color);}
  int panel=margin+column+int(15*scale),paneWidth=width-panel-margin;double rotation=GetTickCount64()/1000.;if(selection<5){constexpr unsigned ids[]={0,1,4,2,3},models[]={0,1,3,2,4},ammo[]={9,10,13,11,14};auto weapon=ids[selection];originalModels.draw(dc,models[selection],false,panel,margin,paneWidth,int(height*.48),rotation);originalModels.draw(dc,ammo[selection],false,panel,int(height*.51),paneWidth,int(height*.25),rotation);std::wstring status=equipped==weapon?L"Selected":owned[weapon]?L"Available":L"Not available";originalFont.draw(dc,status,panel,int(height*.80),scale,0x33ff66);std::wstring amount=L"Ammunition "+std::to_wstring(ammunition[weapon]);if(reserves[weapon])amount+=L"   Clips "+std::to_wstring(reserves[weapon]);originalFont.draw(dc,amount,panel,int(height*.86),scale,0x188234);}
  else if(selection==5){originalModels.draw(dc,7,false,panel,margin,paneWidth,int(height*.68),rotation);originalFont.draw(dc,std::to_wstring(batteries)+L" Available",panel,int(height*.79),scale);}
  else if(selection==6){
   int textBottom=margin+int(height*.40);
   originalFont.paragraph(dc,missionNote,panel,margin,scale*.75,0x33ff66);
   if(mapAutoMapper&&mapWidth&&mapHeight&&mapCells.size()==size_t(mapWidth)*mapHeight){
    int mapLeft=panel,mapTop=textBottom,mapW=paneWidth,mapH=height-mapTop-(usingController?84:48);
    if(mapW>32&&mapH>32){
     RECT box{mapLeft,mapTop,mapLeft+mapW,mapTop+mapH};auto border=CreateSolidBrush(RGB(16,80,32)),bg=CreateSolidBrush(RGB(2,12,6));FillRect(dc,&box,border);RECT inner{box.left+1,box.top+1,box.right-1,box.bottom-1};FillRect(dc,&inner,bg);DeleteObject(border);DeleteObject(bg);
     int cellPx=std::max(2,std::min((mapW-8)/int(mapWidth),(mapH-8)/int(mapHeight)));
     int ox=mapLeft+(mapW-int(mapWidth)*cellPx)/2,oz=mapTop+(mapH-int(mapHeight)*cellPx)/2;
     for(unsigned z=0;z<mapHeight;++z)for(unsigned x=0;x<mapWidth;++x){
      unsigned char c=mapCells[(mapHeight-1-z)*mapWidth+x];if(!c)continue;
      bool visited=c&1,door=c&2,lift=c&4,secret=c&8,revealed=c&16;
      if(!visited&&!mapAutoMapper&&!revealed)continue;
      COLORREF col=door?RGB(40,220,240):lift?RGB(230,210,40):secret&&visited?RGB(180,255,90):visited?RGB(36,185,75):RGB(14,72,32);
      RECT cr{ox+int(x)*cellPx,oz+int(z)*cellPx,ox+int(x+1)*cellPx-std::min(1,cellPx-2),oz+int(z+1)*cellPx-std::min(1,cellPx-2)};
      auto b=CreateSolidBrush(col);FillRect(dc,&cr,b);DeleteObject(b);
     }
     int px=ox+int(mapPlayerX)*cellPx+cellPx/2,pz=oz+int(mapHeight-1-mapPlayerZ)*cellPx+cellPx/2,pr=std::max(2,cellPx/2+1);
     RECT prc{px-pr,pz-pr,px+pr+1,pz+pr+1};auto pb=CreateSolidBrush(RGB(255,255,255));FillRect(dc,&prc,pb);DeleteObject(pb);
     double ang=mapPlayerYaw*6.283185307179586/4096.;int dx=int(std::lround(-std::sin(ang)*cellPx*1.6)),dz=int(std::lround(-std::cos(ang)*cellPx*1.6));
     auto pen=CreatePen(PS_SOLID,std::max(1,int(scale*1.5)),RGB(255,80,80));auto oldPen=SelectObject(dc,pen);MoveToEx(dc,px,pz,nullptr);LineTo(dc,px+dx,pz+dz);SelectObject(dc,oldPen);DeleteObject(pen);
    }
   }
  }
  else if(selection==7)originalModels.draw(dc,5,true,panel,margin,paneWidth,int(height*.70),rotation);
  drawDescription(message.empty()?description:message);
  if(usingController)drawPrompts(dc,width,height-int(25*scale),scale*.7,{{100,L"Select"},{0,L"Use / select"},{1,L"Resume"}});return;
 }
auto& image=images[page=="main"?0:1];BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=image.width;info.bmiHeader.biHeight=-int(image.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;SetStretchBltMode(dc,COLORONCOLOR);if(page=="pause"||page=="mission"||page=="assessment"){RECT r{0,0,width,height};FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));}else StretchDIBits(dc,0,0,width,height,0,0,image.width,image.height,image.pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
 const auto [top,line]=menuRows(page,height,list.size());
 if(page=="survival")drawSurvivalArena(dc,width,top+line,arenaPreviewHeight(height));
 const auto visibleSkulls=isSkullList(page)?skullIds(page):std::vector<unsigned>{};
 if(page!="main"){SetTextColor(dc,RGB(70,255,110));std::wstring title=page=="survival-skulls"?L"Xeno Skulls":page=="scoring-skulls"?L"Scoring Skulls":page=="fun-skulls"?L"Fun Skulls":page=="survival-history"?L"Run History":std::wstring(page.begin(),page.end());std::transform(title.begin(),title.end(),title.begin(),towupper);RECT r{width/8,height/10,width*7/8,height/5};originalFont.box(dc,title,r,height/600.,0xffffff,true);}
 for(unsigned i=0;page!="credits"&&page!="map"&&i<list.size();++i){const int rowTop=top+int(i)*line+(page=="survival"&&i>0?arenaPreviewHeight(height):0);RECT r{width/8,rowTop,width*7/8,rowTop+line};if(page!="main"&&i==selection){auto brush=CreateSolidBrush(RGB(10,65,25));FillRect(dc,&r,brush);DeleteObject(brush);}SetTextColor(dc,i==selection?RGB(240,255,240):RGB(51,210,90));std::wstring text=(i==selection?L"> ":L"  ")+list[i];if(page=="difficulty"&&i==(coopSession?coopDifficulty:difficulty))text+=L" *";double fontScale=(page=="main"||isSkullList(page))?std::min(height/600.,line/16.):height/600.;unsigned tint=i==selection?0xebffeb:0x33d25a;if(page=="controller-bindings"&&i<controllerBindings.size())drawPrompts(dc,width,r.top+(line-int(20*fontScale))/2,fontScale,{{controllerBindings[i],PadActions[i]}});else if(i<visibleSkulls.size()){
     const int iconPixel=std::max(1,line/14),gap=std::max(4,line/4);
     const int left=(width-originalFont.width(text,fontScale)-16*iconPixel-gap)/2;
     drawXenoSkull(dc,left,r.top+(line-12*iconPixel)/2,iconPixel,(survivalSkulls&(1u<<visibleSkulls[i]))!=0);
     originalFont.draw(dc,text,left+16*iconPixel+gap,r.top+(line-int(16*fontScale))/2,fontScale,tint);
 }else{originalFont.draw(dc,text,(width-originalFont.width(text,fontScale))/2,r.top+(line-int(16*fontScale))/2,fontScale,tint);
  if(adjustable(i)){   // a triangle either side of the value: Left/Right (or a click on one) steps it
   const auto a=arrows(width,r.top,line,height);auto brush=CreateSolidBrush(RGB((tint>>16)&255,(tint>>8)&255,tint&255));auto oldBrush=SelectObject(dc,brush);auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
   POINT left[]{{a.left-a.size,a.middle},{a.left+a.size,a.middle-a.size},{a.left+a.size,a.middle+a.size}},right[]{{a.right+a.size,a.middle},{a.right-a.size,a.middle-a.size},{a.right-a.size,a.middle+a.size}};
   Polygon(dc,left,3);Polygon(dc,right,3);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(brush);
  }
 }}
 if(page=="fun-skulls"){
  originalFont.box(dc,L"Enabling any Fun Skull makes the run unscored.",
      RECT{width/10,top+int(list.size())*line+4,width*9/10,height-52},height/700.,0xffffff,true);
 }
 if(page=="graphics"){int x=width/3,w=std::max(1,width/3),y=top+5*line-4;RECT track{x,y,x+w,y+3};auto brush=CreateSolidBrush(RGB(12,50,22));FillRect(dc,&track,brush);DeleteObject(brush);track.right=x+w*int(fov-50)/60;brush=CreateSolidBrush(RGB(60,220,95));FillRect(dc,&track,brush);DeleteObject(brush);}
 if(page=="mission"||page=="map"){
  RECT r{width/8,height/6,width*7/8,height/2};SetTextColor(dc,RGB(80,230,120));if(page=="mission")originalFont.box(dc,missionNote,r,height/700.);
  if(mapAutoMapper&&mapWidth&&mapHeight&&mapCells.size()==size_t(mapWidth)*mapHeight){
   int mapLeft=width/8,mapTop=page=="map"?height/5:height/2-int(height*.04),mapW=width*3/4,mapH=height-mapTop-56;
   if(mapW>32&&mapH>32){
    RECT box{mapLeft,mapTop,mapLeft+mapW,mapTop+mapH};auto border=CreateSolidBrush(RGB(16,80,32)),bg=CreateSolidBrush(RGB(2,12,6));FillRect(dc,&box,border);RECT inner{box.left+1,box.top+1,box.right-1,box.bottom-1};FillRect(dc,&inner,bg);DeleteObject(border);DeleteObject(bg);
    int cellPx=std::max(2,std::min((mapW-8)/int(mapWidth),(mapH-8)/int(mapHeight)));
    int ox=mapLeft+(mapW-int(mapWidth)*cellPx)/2,oz=mapTop+(mapH-int(mapHeight)*cellPx)/2;
    for(unsigned z=0;z<mapHeight;++z)for(unsigned x=0;x<mapWidth;++x){
     unsigned char c=mapCells[(mapHeight-1-z)*mapWidth+x];if(!c)continue;
     bool visited=c&1,door=c&2,lift=c&4,secret=c&8,revealed=c&16;
     if(!visited&&!mapAutoMapper&&!revealed)continue;
     COLORREF col=door?RGB(40,220,240):lift?RGB(230,210,40):secret&&visited?RGB(180,255,90):visited?RGB(36,185,75):RGB(14,72,32);
     RECT cr{ox+int(x)*cellPx,oz+int(z)*cellPx,ox+int(x+1)*cellPx-std::min(1,cellPx-2),oz+int(z+1)*cellPx-std::min(1,cellPx-2)};
     auto b=CreateSolidBrush(col);FillRect(dc,&cr,b);DeleteObject(b);
    }
    int px=ox+int(mapPlayerX)*cellPx+cellPx/2,pz=oz+int(mapHeight-1-mapPlayerZ)*cellPx+cellPx/2,pr=std::max(2,cellPx/2+1);
    RECT prc{px-pr,pz-pr,px+pr+1,pz+pr+1};auto pb=CreateSolidBrush(RGB(255,255,255));FillRect(dc,&prc,pb);DeleteObject(pb);
    double ang=mapPlayerYaw*6.283185307179586/4096.;int dx=int(std::lround(-std::sin(ang)*cellPx*1.6)),dz=int(std::lround(-std::cos(ang)*cellPx*1.6));
    auto pen=CreatePen(PS_SOLID,std::max(1,height/320),RGB(255,80,80));auto oldPen=SelectObject(dc,pen);MoveToEx(dc,px,pz,nullptr);LineTo(dc,px+dx,pz+dz);SelectObject(dc,oldPen);DeleteObject(pen);
   }
  }
 }
 if(page=="coop-lobby"||page=="coop-scores"||page=="coop-results"){auto text=page=="coop-lobby"?coopStatus:coopScoreText;RECT panel{width/10,height/2,width*9/10,height-100};originalFont.box(dc,text,panel,height/650.,0xffffff,true);}
 if(page=="replays"){
  std::wstring summary=L"No recorded gameplay yet.";
  if(!replayFiles.empty()){
   const auto stem=replayFiles[replayIndex].stem().wstring();
   const auto split=stem.find(L'_');
   summary=split==std::wstring::npos?stem:stem.substr(split+1)+L"\n"+stem.substr(0,split);
   summary+=L"\n"+std::to_wstring(replayIndex+1)+L" / "+std::to_wstring(replayFiles.size());
  }
  originalFont.box(dc,summary,RECT{width/10,height/2,width*9/10,height-55},height/650.,0xffffff,true);
 }
 if(page=="stats"||page=="unlocked achievements"){
  std::wstring summary;
  if(page=="stats")summary=statsSummary();
  else if(unlockedAchievements.empty())summary=L"No achievements unlocked yet.";
  else{
   const auto& item=ExplorerAchievementCatalog[unlockedAchievements[unlockedAchievementIndex]];
   summary=std::wstring(item.title)+L"\n"+item.description+L"\n"+std::to_wstring(item.points)+L" points   "+
       std::to_wstring(unlockedAchievementIndex+1)+L" / "+std::to_wstring(unlockedAchievements.size());
  }
  originalFont.box(dc,summary,RECT{width/10,height/2,width*9/10,height-55},height/650.,0xffffff,true);
 }
 if(page=="achievements"){
  const auto& item=ExplorerAchievementCatalog[achievementIndex];
  unsigned points=0,earned=0;
  for(unsigned id=1;id<std::size(ExplorerAchievementCatalog);++id)
   if(achievements.has(id)){++earned;points+=ExplorerAchievementCatalog[id].points;}
  const auto summary=std::to_wstring(achievementIndex)+L"/101   "+item.title+L"\n"+item.description+
      L"\n"+(achievements.has(achievementIndex)?L"Unlocked   ":L"Locked   ")+std::to_wstring(item.points)+
      L"G\n\n"+std::to_wstring(earned)+L"/101 unlocked   "+std::to_wstring(points)+L"/1500G";
  originalFont.box(dc,summary,RECT{width/10,height*2/5,width*9/10,height-55},height/650.,0xffffff,true);
 }
 if(page=="survival-history"){
  const auto& runs=survivalRuns;
  std::wstring summary=L"No Survival runs recorded.";
  if(!runs.empty()){
   const auto index=std::min(survivalRunIndex,unsigned(runs.size()-1));
   const auto& run=runs[runs.size()-1-index];
   summary=run.map+L"  "+std::to_wstring(index+1)+L"/"+std::to_wstring(runs.size())
       +L"\n"+survivalRunDetails(run);
  }
  originalFont.box(dc,summary,RECT{width/10,height/3,width*9/10,height-55},height/650.,0xffffff,true);
 }
 if(page=="survival results"){
  std::wstring summary;
  for(const auto& row:g_survivalResults){summary+=row;summary+=L'\n';}
  originalFont.box(dc,summary,RECT{width/10,height/3,width*9/10,height-55},height/650.,0xffffff,true);
 }
 if(page=="credits"){std::ifstream f(assets/"GameData/PS1/CD/GFX/CREDITS.TXT");std::string text((std::istreambuf_iterator<char>(f)),{});std::wstring credits=std::wstring(L"Alien Trilogy: Remastered\r\n")+TrilogyVersion+std::wstring(L"\r\n\r\nCreated by\r\nAtlantean / NobleElite\r\n\r\nwith assistance from AI\r\n\r\n\r\n---\r\n\r\n\r\nOriginal Game\r\n\r\n\r\n")+std::wstring(text.begin(),text.end());int saved=SaveDC(dc);IntersectClipRect(dc,width/10,height/4,width*9/10,height*4/5);RECT r{width/10,height/4-int(selection)*line,width*9/10,height*4/5+10000};SetTextColor(dc,RGB(80,230,120));originalFont.box(dc,credits,r,height/700.,0xffffff,true);RestoreDC(dc,saved);}
 if(page=="main"){   // the version, small in the bottom-right corner
  SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(51,140,70));HFONT small=CreateFontW(-std::max(12,height/48),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");auto old=SelectObject(dc,small);
  RECT corner{0,0,width-12,height-8};DrawTextW(dc,TrilogyVersion,-1,&corner,DT_RIGHT|DT_BOTTOM|DT_SINGLELINE);SelectObject(dc,old);DeleteObject(small);
 }
 drawDescription(!message.empty()?message:!description.empty()?(page=="main"&&!usingController?description+L"   F1: Options":description):L"Arrow keys: select/change   Enter: confirm   Esc: back");
 if(usingController&&message.empty())drawPrompts(dc,width,height-40,height/700.,page=="main"?std::vector<ExplorerPrompt>{{100,L"Navigate"},{0,L"Confirm"},{3,L"Options"}}:page=="controller-bindings"?std::vector<ExplorerPrompt>{{100,L"Choose"},{0,L"Assign"},{1,L"Back"}}:std::vector<ExplorerPrompt>{{100,L"Navigate"},{0,L"Confirm"},{1,L"Back"}});
}

void ExplorerMenu::inputCharacter(wchar_t c){if(page=="coop"&&coopEditing>=0){auto& text=coopEditing==0?coopAddress:coopPort;size_t limit=coopEditing==0?15:5;if(text.size()<limit&&((c>=L'0'&&c<=L'9')||(coopEditing==0&&c==L'.')))text+=c;return;}if(page=="password"&&passwordText.size()<32&&((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'?'))passwordText+=towupper(c);}

void ExplorerMenu::bindController(unsigned button){if(controllerListening<0||controllerListening>=int(controllerBindings.size())||!(button<26||button==32||button==33))return;auto index=unsigned(controllerListening);auto other=std::find(controllerBindings.begin(),controllerBindings.end(),button);if(other!=controllerBindings.end())std::swap(*other,controllerBindings[index]);controllerBindings[index]=button;controllerListening=-1;message=L"Button assigned";persist();}

void ExplorerMenu::refreshContinue(){
 continueSlot=-1;continueCode.clear();std::filesystem::file_time_type newest{};auto directory=root/"decompilation/user/saves";
 for(unsigned slot=0;slot<3;++slot){auto path=directory/("slot"+std::to_string(slot+1)+".sav");auto save=explorerReadCheckpoint(path);if(!save)continue;
 bool installed=false;std::error_code error;auto cd=assets/"GameData/PS1/CD";for(std::filesystem::recursive_directory_iterator it(cd,std::filesystem::directory_options::skip_permission_denied,error),end;!error&&it!=end;it.increment(error)){if(it->path().filename()=="L"+save->code+"LEV.MAP"){installed=true;break;}}if(!installed)continue;
 auto written=std::filesystem::last_write_time(path,error);if(error)continue;if(continueSlot<0||written>newest){newest=written;continueSlot=int(slot);continueCode=save->code;}
 }
}
