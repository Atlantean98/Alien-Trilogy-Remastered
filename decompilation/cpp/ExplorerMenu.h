#pragma once
#include "ExplorerSurvivalState.h"
#include "ExplorerAchievements.h"
#define NOMINMAX
#include <windows.h>
#include "ExplorerAssets.h"
#include "ExplorerMonitors.h"
#include "ExplorerPrompts.h"
#include "ExplorerController.h"
#include "ExplorerPresentation.h"
#include <array>
#include <functional>
class ExplorerMenu {
public:
 ExplorerAchievements achievements;
 bool isolationPlaytest=false;
 bool isolationUnlocked()const{return isolationPlaytest||achievements.campaignCompleted();}
 unsigned achievementIndex=1;
 std::vector<std::filesystem::path> replayFiles;
 unsigned replayIndex=0;
 std::filesystem::path replayDirectory()const{return root/"replays";}
 std::filesystem::path replayScreenshots(const std::filesystem::path& clip)const{return root/"screenshots"/clip.stem();}
 void refreshReplays();
 void showReplays(){refreshReplays();enter("replays");}
 void showStats();
 std::wstring statsSummary()const;
 std::vector<unsigned> unlockedAchievements;
 unsigned unlockedAchievementIndex=0;
 unsigned survivalArena=0;
 unsigned survivalSkulls=0,survivalRunIndex=0;
 std::vector<SurvivalRun> survivalRuns;
 std::filesystem::path survivalHistoryPath()const{return root/"survival_history.txt";}
 enum Command {None,Start,Resume,Load1,Load2,Load3,Save1,Save2,Save3,Display,Quit,Apply,Restart,Weapon0,Weapon1,Weapon2,Weapon3,Weapon4,NextMission,LoadPassword,CheatsChanged,UseBattery,CoopHost,CoopJoin,CoopStart,CoopReady,CoopLeave,CoopCopyInvite,CoopPasteInvite,CoopAdvance,MainMenu,SurvivalStart,ReplayWatch,ReplayScreenshots,QueensRevengeStart,IsolationStart,IsolationContinue,ExterminationSave1,ExterminationSave2,ExterminationSave3,ExterminationLoad1,ExterminationLoad2,ExterminationLoad3};
 bool coopSession=false;bool coopHost=false,coopConnected=false,coopReady=false,coopFinished=false;unsigned coopDifficulty=1,coopMode=0;std::wstring coopStatus,coopScoreText;std::wstring coopAddress=L"127.0.0.1",coopPort=L"27871",coopInvitation;int coopEditing=-1;
 std::vector<ExplorerMonitor> monitors;std::string monitorDevice;void refreshMonitors(){monitors=explorerMonitors();}
 bool usingController=false,playstationPrompts=false;void drawPrompts(HDC dc,int width,int top,double scale,const std::vector<ExplorerPrompt>& prompts)const{explorerDrawPrompts(dc,originalFont,prompts,playstationPrompts,width,top,scale);}
 int continueSlot=-1;std::string continueCode;void refreshContinue();
 // Extermination's own save (decompilation/user/saves/extermination.sav, apart from the story's slots and co-op):
 // the level to continue from (2 or 3), 0 when there is none. Read when its page opens.
 unsigned exterminationSaved=0;void refreshExtermination();
 // Extermination's three save slots (decompilation/user/saves/extermination_slot<n>.sav: the level to play), its own
 // apart from the story's slots and co-op. exterminationGame: an Extermination level is being played (its pause menu
 // saves and loads these slots), exterminationLevel which one.
 bool exterminationGame=false;unsigned exterminationLevel=0;
 std::filesystem::path exterminationSlotPath(unsigned i)const{return root/"decompilation/user/saves"/("extermination_slot"+std::to_string(i+1)+".sav");}
 unsigned exterminationSlotLevel(unsigned i)const;std::wstring exterminationSlot(unsigned i)const;
 void inputCharacter(wchar_t c);void bindController(unsigned button);
 void load(const std::filesystem::path&,const std::filesystem::path& settingsRoot={});
 void showResults(const std::array<unsigned,3>&,bool passed=true,unsigned required=0);bool resultsReady()const;
 void show(bool running);void pauseGame();void close(){open=false;}
 int soundEvent=-1;bool hover(int x,int y,int width,int height);
 Command key(unsigned key);Command click(int x,int y,int width,int height,bool dragging=false);
 // Rows whose value Left/Right steps through (drawn with an arrow either side; clicking an arrow steps it).
 bool adjustable(unsigned row)const;int arrowAt(int x,int y,int width,int height,unsigned row)const;
 void draw(HDC,int width,int height);
 void drawOriginalText(HDC dc,const std::wstring& text,int x,int y,double scale,unsigned color=0xffffff)const{originalFont.paragraph(dc,text,x,y,scale,color);}
 void persist()const;void defaults();
 std::wstring slot(unsigned)const;
 std::vector<std::wstring> labels()const{return rows();}
 bool performanceDisplay=false;bool headphoneAudio=false;unsigned textureFiltering=0;unsigned antialiasing=0;std::vector<unsigned> antialiasingModes{0,2,4,8};
 std::array<unsigned,9> controllerBindings=ExplorerController::DefaultBindings;int controllerListening=-1;bool controllerRumbleTest=false;unsigned controllerSensitivityY=3,controllerVibration=70;
 bool controllerEnabled=true,controllerInvert=false,vsync=false;unsigned controllerSensitivity=3,controllerDeadzone=18,frameLimit=60;
 enum DisplayMode : unsigned {Windowed, Borderless, Fullscreen};
 unsigned displayMode=Windowed;
 bool difficultyLocked=false;bool open=false,hasGame=false,invert=false,sway=false;unsigned difficulty=1,sensitivity=3,sfx=100,music=100,resolution=0,renderQuality=0,renderer=0,fov=70,aspect=0;
 std::array<unsigned,11> bindings{'W','S','A','D',VK_SPACE,'E','Q','B','R',VK_SHIFT,VK_RBUTTON};
 std::array<unsigned,3> resultPercent{};ULONGLONG resultsStarted{};bool resultsRevealed=false,assessmentPassed=true;unsigned assessmentRequired=0;
 std::wstring controllerName=L"No controller connected";
 std::wstring message,missionNote,passwordText,levelPassword;bool cheatsUnlocked=false,godMode=false,infiniteAmmo=false,noClip=false,giveWeapons=false;unsigned startLevel=0;std::array<unsigned,5> ammunition{},reserves{};std::array<bool,5> owned{};unsigned equipped{};unsigned batteries{};unsigned selection=0;std::string page="main";
 unsigned mapWidth=0,mapHeight=0,mapPlayerX=0,mapPlayerZ=0;int mapPlayerYaw=0;bool mapAutoMapper=false;std::vector<unsigned char> mapCells;
 std::filesystem::path root,assets;
private:
 ExplorerFont originalFont;ExplorerModelPreview originalModels;
 struct Image {unsigned width{},height{};std::vector<unsigned> pixels;};std::array<Image,2> images;Image resultsImage;
 std::vector<std::wstring> rows()const;
 std::wstring optionDescription(std::wstring_view label)const;
 void drawSurvivalArena(HDC dc,int width,int top,int height);
 struct ArenaPreview {unsigned width=0,height=0;std::vector<unsigned> pixels;};
 std::array<ArenaPreview,std::size(SurvivalArenaNames)> arenaPreviews;
 int listening=-1;std::vector<std::pair<std::string,unsigned>> history;
 Command activate(unsigned key);int rowAt(int x,int y,int width,int height)const;
 struct Arrows {int left,right,middle,size;};Arrows arrows(int width,int rowTop,int line,int height)const;struct ArrowColumn {std::string page;int width=0,height=0,widest=0;};mutable ArrowColumn arrowColumn;
 void enter(const std::string&);void back();std::wstring keyName(unsigned)const;
};
