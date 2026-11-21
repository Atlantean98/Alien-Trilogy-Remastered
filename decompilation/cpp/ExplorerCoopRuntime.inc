// Host adapter kept out of the original single-player simulation routines.
class ExplorerCoopRuntime {
 using Connection=trilogy::coop::Connection;using Writer=trilogy::coop::Writer;using Reader=trilogy::coop::Reader;
 using Input=trilogy::coop::PlayerInput;using Phase=trilogy::coop::Phase;
 Scene& s;
 struct Slot {
  WorldPlayerState world{};PlayerMovementState movement{};Vec3s previous{};double swayPhase{},previousSwayPhase{},swayAmount{},previousSwayAmount{};ExplorerHud hud;ExplorerPickups pickups;
  std::vector<unsigned char> personal;ExplorerEnemies::AttachmentState attachment{};
  std::wstring notice;unsigned noticeTicks{},action{},muzzle{},footstep{};int yaw{},pitch{};
  std::array<unsigned char,50> entry{};unsigned entryWeapon{};
  std::map<unsigned,unsigned> pickupIds;
 };
 std::array<Slot,2> slots;
 std::vector<std::pair<unsigned,unsigned>> ranges;
 unsigned selected=0,epoch=0,receivedEpoch=0,sequence=0,lastInput=0,ticks=0,fade=0,pickupSerial=128;
 bool helloSent=false,compatible=false,ready=false,running=false,passed=false,finished=false;
 bool pendingFire=false;bool suppressed=false;unsigned savedFlags=0;int projectileOwner=-1;
 double accumulator=0,snapshotClock=0,replicaAge=0;unsigned pendingWeapon=~0u,pendingWeaponSequence=0,serverWeapon=0;ULONGLONG lastRemote=0;
 Input remote{};double inputTokens=64;ULONGLONG tokenAt=0;
 std::map<unsigned,unsigned> owners,queenShooter;
 std::map<unsigned,std::pair<int,unsigned>> enemyBefore;
 struct Sound {unsigned slot,weapon,player;int x,z;};std::vector<Sound> sounds;
 std::set<unsigned> tags;
 std::vector<unsigned char> memorySent,memoryReceived,lightsSent,lightsReceived;
 std::uint64_t fingerprint=0;
 static std::wstring wide(const std::string& text){return {text.begin(),text.end()};}
 static std::string narrow(const std::wstring& text){return {text.begin(),text.end()};}
 unsigned survivalMode=0,arena=0;
 bool survival()const{return survivalMode!=0;}
 void loadSurvival(unsigned mode,unsigned selectedArena){
  s.menu.survivalArena=selectedArena;
  const auto skulls=s.menu.survivalSkulls;s.menu.survivalSkulls=0;
  try{explorerSurvivalStart(s,mode==2);}catch(...){s.menu.survivalSkulls=skulls;throw;}
  s.menu.survivalSkulls=skulls;
  if(!g_survival.active)throw std::runtime_error("Co-op Survival arena could not load");
 }
 void advanceCampaign(){
  const auto next=s.mission.nextIndex();
  if(next>=35){
   finished=true;phase=Phase::Finished;s.menu.page="coop-results";
   s.menu.coopFinished=true;s.menu.selection=0;snapshotClock=1;return;
  }
  newLevel(s.mission.nextCode(),false,!passed);
 }
 void newSurvival(){
  s.coopSurvivalPickup={};s.coopQueenReward={};
  survivalMode=s.menu.coopMode;arena=s.menu.survivalArena;
  s.coopGame=true;
  loadSurvival(survivalMode,arena);
  initialisePlayers(true);
  remote={};remote.yaw=s.yaw&4095;lastRemote=GetTickCount64();
  running=true;finished=false;phase=Phase::Playing;fade=0;++epoch;
  memorySent.clear();lightsSent.clear();snapshotClock=1;
  sound(explorer_survival::kQueenHiss,false,s.player.position[0],s.player.position[2]);
 }
 fs::path findLevel(const std::string& code){for(auto& file:s.physicalLevels)if(file.stem()=="L"+code+"LEV")return file;throw std::runtime_error("Co-op mission is not installed");}
 std::uint64_t buildFingerprint(){
  if(fingerprint)return fingerprint;std::uint64_t hash=1469598103934665603ull;
  auto add=[&](const fs::path& path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Co-op build/data check could not read installed file");std::array<char,65536> block;while(f){f.read(block.data(),block.size());for(std::streamsize i=0;i<f.gcount();++i){hash^=static_cast<unsigned char>(block[i]);hash*=1099511628211ull;}}};
  wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);add(exe);
  add(s.root/"GameData/PS1/CD/SLUS_000.07");
  for(const auto& map:s.physicalLevels)add(map);
  for(const auto* folder:SurvivalArenaFolders){
   const auto path=s.root/"GameData"/folder;
   ExplorerLevelConfig cfg;
   if(!explorerLoadSurvivalConfig(path,cfg))throw std::runtime_error("Co-op Survival arena is missing");
   add(path/"level.txt");add(cfg.map);
  }
  return fingerprint=hash;
 }
 void personalOut(Slot& slot){slot.personal.clear();for(auto [address,count]:ranges)slot.personal.insert(slot.personal.end(),s.memory.begin()+address,s.memory.begin()+address+count);}
 void personalIn(const Slot& slot){size_t offset=0;for(auto [address,count]:ranges){std::copy_n(slot.personal.begin()+offset,count,s.memory.begin()+address);offset+=count;}}
 void capture(){
  auto& slot=slots[selected];s.player.health=s.mem().w(0xa0ab8);slot.world=s.player;slot.movement=s.acceleration;slot.previous=s.previousPosition;slot.swayPhase=s.swayPhase;slot.previousSwayPhase=s.previousSwayPhase;slot.swayAmount=s.swayAmount;slot.previousSwayAmount=s.previousSwayAmount;
  slot.yaw=s.yaw;slot.pitch=s.pitch;slot.notice=s.notice;slot.noticeTicks=s.noticeTicks;slot.action=s.logic.playerAction();slot.attachment=s.enemies->attachmentState();slot.muzzle=s.muzzleTicks;slot.footstep=s.footstepClock;personalOut(slot);
  auto& marine=s.coopMarines[selected];marine.position=s.player.position;marine.yaw=s.yaw;marine.health=s.player.health;marine.armor=s.mem().w(0xa0aba);marine.weapon=s.hud.selected();
 }
 void activate(unsigned id){
  if(id==selected)return;capture();std::swap(s.hud,slots[selected].hud);std::swap(s.pickups,slots[selected].pickups);
  selected=id;s.coopActor=id;auto& slot=slots[id];std::swap(s.hud,slot.hud);std::swap(s.pickups,slot.pickups);personalIn(slot);
  s.player=slot.world;s.acceleration=slot.movement;s.previousPosition=slot.previous;s.swayPhase=slot.swayPhase;s.previousSwayPhase=slot.previousSwayPhase;s.swayAmount=slot.swayAmount;s.previousSwayAmount=slot.previousSwayAmount;s.yaw=slot.yaw;s.pitch=slot.pitch;s.notice=slot.notice;s.noticeTicks=slot.noticeTicks;s.muzzleTicks=slot.muzzle;s.footstepClock=slot.footstep;s.logic.playerAction(slot.action);s.enemies->attachmentState(slot.attachment);
 }
 unsigned nearest(int x,int z){capture();unsigned best=selected;std::int64_t distance=INT64_MAX;for(unsigned id=0;id<2;++id){auto& p=s.coopMarines[id];if(!p.health)continue;auto dx=std::int64_t(p.position[0])-x,dz=std::int64_t(p.position[2])-z;auto d=dx*dx+dz*dz;if(d<distance){distance=d;best=id;}}return best;}
 void eachPickup(const std::function<void()>& fn){
  unsigned original=selected,id=++pickupSerial;
  for(unsigned player=0;player<2;++player){activate(player);auto before=s.pickups.active();fn();for(auto p:s.pickups.active())if(std::find(before.begin(),before.end(),p)==before.end())slots[player].pickupIds[p]=id;capture();}
  activate(original);
 }
 void rememberEnemies(){enemyBefore.clear();auto m=s.mem();for(auto e:s.enemies->active())enemyBefore[e]={m.s(e+0x58),m.b(e+0x74)};}
 void scoreDeaths(unsigned owner){auto m=s.mem();for(auto [e,old]:enemyBefore)if(old.first>0&&m.s(e+0x58)<=0&&old.second!=10&&old.second<=14){using Event=trilogy::coop::TeamScore::Event;s.coopScore.award(owner,old.second==7?Event::QueenKill:old.second==1?Event::EggKill:Event::AlienKill);}enemyBefore.clear();}
 void sound(unsigned slot,bool weapon,int x,int z){if(sounds.size()<64)sounds.push_back({slot,unsigned(weapon),selected,x,z});}
 void hooks(){
  s.coopSound=[this](unsigned slot,bool weapon){sound(slot,weapon,s.player.position[0],s.player.position[2]);};
  auto worldSound=[this](unsigned slot){sound(slot,false,s.player.position[0],s.player.position[2]);s.audio->play(slot);};
  s.objects.sound=s.logic.sound=s.pickups.sound=worldSound;
  slots[1-selected].pickups.sound=worldSound;
  s.enemies->sound=[this](unsigned slot,unsigned entity){auto m=s.mem();int x=s.player.position[0],z=s.player.position[2];if(entity>=0x300000&&entity<0x300000+128*208){x=m.s(entity);z=m.s(entity+4);}sound(slot,false,x,z);s.audio->playAt(slot,float(x),float(z));};
  s.enemies->explosion=[this](int x,int y,int z){s.objects.effects.push_back({x,y,z,0,true});sound(41,false,x,z);s.audio->playAt(41,float(x),float(z));};
  s.enemies->coopEmitted=[this](unsigned p){owners[p]=selected;};
  s.enemies->coopTarget=[this](unsigned e){
   capture();for(unsigned id=0;id<2;++id)if(slots[id].attachment.entity==e){activate(id);return;}
   auto m=s.mem();activate(nearest(m.s(e),m.s(e+4)));
  };
  s.enemies->coopProjectile=[this](unsigned p,bool before){
   if(before){auto m=s.mem();auto position=m.d(p);auto it=owners.find(p);projectileOwner=it==owners.end()?-1:int(it->second);activate(projectileOwner>=0?unsigned(projectileOwner):nearest(m.s(position),m.s(position+4)));rememberEnemies();savedFlags=m.b(0x94b9c);suppressed=projectileOwner>=0;if(suppressed)m.b(0x94b9c,savedFlags|4);}
   else{if(projectileOwner>=0){for(auto [entity,before]:enemyBefore)if(before.first>s.mem().s(entity+0x58)&&before.second==7)queenShooter[entity]=unsigned(projectileOwner);scoreDeaths(unsigned(projectileOwner));}if(suppressed)s.mem().b(0x94b9c,savedFlags);suppressed=false;capture();}
  };
  s.coopQueenReward=[this](unsigned queen){
   auto owner=queenShooter.find(queen);
   if(owner!=queenShooter.end()&&s.coopMarines[owner->second].health)activate(owner->second);
  };
  s.coopSurvivalPickup=[this](unsigned kind,unsigned amount,unsigned reserve,unsigned index,unsigned limit){
   eachPickup([&]{
    const auto x=index%s.grid.width,z=index/s.grid.width;
    auto m=s.mem();
    const auto pickups=s.pickups.active();
    if(pickups.size()>=limit)return;
    for(auto p:pickups)if(m.d(p+28)==m.d(0xa0c40)+16*index)return;
    s.pickups.dropSurvivalPickup(kind,amount,reserve,x,z);
   });
  };
  s.objects.pickup=[this](unsigned o,unsigned i){if(!g_survival.active||!g_survival.cfg.queensRevenge())eachPickup([&]{s.pickups.crateDrop(o,i);});};
  s.logic.triggerPickup=[this](unsigned d,unsigned i){if(!g_survival.active||!g_survival.cfg.queensRevenge())eachPickup([&]{s.pickups.trigger(d,i);});};
  s.enemies->droppedItem=[this](unsigned e){if(!g_survival.active||!g_survival.cfg.queensRevenge())eachPickup([&]{s.pickups.enemyDrop(e);});};
  s.coopExit=[this](bool timed){if(timed){s.mission.complete=false;phase=Phase::Extracting;fade=0;s.exitFading=true;s.exitFadeTicks=0;snapshotClock=1;return true;}return extract();};
 }
 void initialisePlayers(bool fresh,const std::array<std::array<unsigned char,50>,2>& carry={},const std::array<unsigned,2>& weapons={}){
  selected=0;s.coopActor=0;slots={};s.yaw&=4095;s.beginCoopPlayers(!survival());ranges={{0xa0a00,0xe2},{0xa0ae4,2},{0xa0b30,4},{0xa0ba0,0x20},{0x31d000,0x50},{0x31e000,128},{0x22d3a8,2},{0x2368f8,24},{0x236a3a,2},{0x340000,0x3100},{0x236c34,0x1c},{0x9c994,0x36}};
  auto m=s.mem();ranges.push_back({m.d(0x236c34),unsigned(m.w(0x236c4c))*8});
  if(!fresh)s.inventory(carry[0],weapons[0]);
  slots[1].hud=s.hud;slots[1].pickups=s.pickups;capture();slots[0].entry=s.inventory();slots[0].entryWeapon=s.hud.selected();
  slots[1].world=slots[0].world;slots[1].previous=slots[0].previous;slots[1].movement=slots[0].movement;slots[1].personal=slots[0].personal;slots[1].yaw=s.yaw;slots[1].pitch=s.pitch;
  for(auto p:s.pickups.active())slots[0].pickupIds[p]=slots[1].pickupIds[p]=(p-0x340000)/48+1;
  activate(1);if(!fresh)s.inventory(carry[1],weapons[1]);slots[1].entry=s.inventory();slots[1].entryWeapon=s.hud.selected();capture();activate(0);
  tags.clear();owners.clear();queenShooter.clear();pickupSerial=128;s.coopRecovery={};hooks();s.coopLocal=0;s.coopGame=s.menu.coopSession=true;s.menu.coopHost=true;s.menu.difficultyLocked=true;s.menu.hasGame=true;
 }
 void newLevel(const std::string& code,bool fresh,bool retry=false){
  std::array<std::array<unsigned char,50>,2> carry{};std::array<unsigned,2> weapons{};
  auto score=s.coopScore;
  if(running&&connection.connected()){phase=Phase::Briefing;s.mission.briefing=true;snapshot();connection.poll();}
  if(running)for(unsigned id=0;id<2;++id){activate(id);carry[id]=retry?slots[id].entry:s.inventory();weapons[id]=retry?slots[id].entryWeapon:s.hud.selected();}
  if(running)activate(0);s.coopGame=true;survivalMode=0;s.level=findLevel(code);s.difficulty=s.menu.coopDifficulty;s.load();initialisePlayers(fresh,carry,weapons);if(!fresh)s.coopScore=score;
  remote={};remote.yaw=s.yaw&4095;lastRemote=GetTickCount64();running=true;finished=false;phase=Phase::Briefing;s.mission.briefing=true;s.menu.open=false;s.briefingRevealed=false;s.briefingStarted=GetTickCount64();fade=0;epoch++;memorySent.clear();lightsSent.clear();snapshotClock=1;
 }
 std::vector<unsigned> projectiles(){std::vector<unsigned> result;auto m=s.mem();for(unsigned p=m.d(0x236300);p;p=m.d(p+36)){if(result.size()>=256)throw std::runtime_error("Too many co-op projectiles");result.push_back(p);}return result;}
 void playerStep(unsigned id,const Input& input,bool world,bool enemies){
  activate(id);auto m=s.mem();auto beforePickups=s.pickups.active();std::map<unsigned,unsigned> kinds;for(auto p:beforePickups)kinds[p]=m.b(p+32);
  auto beforeProjectiles=projectiles();s.yaw=input.yaw;s.pitch=input.pitch;if(input.weapon!=s.hud.selected())s.hud.select(input.weapon);
  s.coopRestore=[this,id](){activate(id);};s.coopWorld=world;s.coopEnemyPass=enemies;s.useRequested=(input.buttons&trilogy::coop::Use)!=0;s.grenadeRequested=(input.buttons&trilogy::coop::Grenade)!=0;s.firing=(input.buttons&trilogy::coop::Fire)!=0;
  auto wasAlive=s.player.health;s.coopDriving=true;s.tick(input.forward/1000.,input.strafe/1000.,(input.buttons&trilogy::coop::Sprint)!=0);s.coopDriving=false;
  // Enemy task targeting may have changed the active player. Restore this
  // player's context before reading their pickup results or playing their gun.
  capture();activate(id);auto after=s.pickups.active();
  for(auto p:beforePickups)if(std::find(after.begin(),after.end(),p)==after.end()){
   s.coopScore.award(id,trilogy::coop::TeamScore::Event::Pickup);if(kinds[p]==15)tags.insert(slots[id].pickupIds[p]);
  }m.w(0xa0ae2,unsigned(tags.size()));
  for(auto it=owners.begin();it!=owners.end();){auto live=projectiles();if(std::find(live.begin(),live.end(),it->first)==live.end())it=owners.erase(it);else ++it;}

  capture();
 }
 void respawn(unsigned id){
  activate(id);auto survivor=s.coopMarines[1-id];s.player.position=survivor.position;s.player.currentCell=s.grid.index(unsigned(int(survivor.position[0])+s.player.originX),unsigned(int(survivor.position[2])+s.player.originZ));s.acceleration.speed=0;s.yaw=survivor.yaw;
  auto m=s.mem();s.player.health=100;m.w(0xa0ab8,100);m.w(0xa0aba,0);m.w(0xa0ae4,90);m.b(0xa0ab5,m.b(0xa0ab5)&254);s.enemies->attachmentState({});s.notice=L"Respawned beside teammate";s.noticeTicks=90;capture();
 }
 void lobby(){
  s.finishReplayRecording();
  if(running)activate(0);if(survival()){g_survival={};s.level=findLevel("111");}s.coopSurvivalPickup={};survivalMode=0;running=false;phase=Phase::Lobby;ready=false;s.coopGame=false;s.coopExit={};s.coopDriving=false;s.enemies->coopTarget={};s.enemies->coopProjectile={};s.enemies->coopEmitted={};s.menu.show(false);s.menu.coopSession=true;s.menu.coopHost=connection.hosting();s.menu.page="coop-lobby";s.menu.coopReady=false;s.menu.coopStatus=L"Back in lobby. Host can start a new run.";s.audio->pause(false);snapshotClock=1;
 }
 bool extract(){
  if(phase!=Phase::Playing)return true;capture();auto& a=s.coopMarines[0];auto& b=s.coopMarines[1];
  if(!a.health||!b.health||std::hypot(double(a.position[0]-b.position[0]),double(a.position[2]-b.position[2]))>2048){s.notice=L"Both players must reach extraction";s.noticeTicks=60;return false;}
  phase=Phase::Extracting;fade=0;s.exitFading=true;s.exitFadeTicks=0;s.exitStatistics=s.mission.statistics();passed=s.mission.eligible();snapshotClock=1;return true;
 }
 void hostStep(const Input& local){
  if(!running)return;
  if(phase==Phase::Playing){
   if(survival()&&g_survival.introTicks){
    explorerSurvivalTick(s);s.recordReplayFrame();return;
   }
   auto old=s.coopMarines;Input guest=remote;if(GetTickCount64()-lastRemote>400){guest.buttons=0;guest.forward=guest.strafe=0;}
   // The world and original task queue run once; inventories/pickups/movement run once per player.
   unsigned worldPlayer=s.coopMarines[0].health?0:1;
   if(s.coopMarines[0].health)playerStep(0,local,worldPlayer==0,false);if(phase==Phase::Playing&&s.coopMarines[1].health)playerStep(1,guest,worldPlayer==1,false);
   if(!running)return;for(unsigned id=0;id<2;++id){activate(id);auto m=s.mem();if(m.w(0xa0b30))m.w(0xa0b30,m.w(0xa0b30)-1);capture();}
   if(phase==Phase::Playing){capture();s.enemies->tick(s.player.position,s.player.health);capture();}
   capture();for(unsigned id=0;id<2;++id){activate(id);s.enemies->corpseContact(s.player.position,s.player.health);capture();}
   std::vector<trilogy::coop::Recovery::Threat> threats;auto m=s.mem();for(auto e:s.enemies->active())if(m.s(e+0x58)>0&&m.b(e+0x74)!=10)threats.push_back({{short(m.s(e)),short(m.s(e+2)),short(m.s(e+4))},true});
   for(unsigned id=0;id<2;++id)if(old[id].health&&!s.coopMarines[id].health)s.coopScore.award(id,trilogy::coop::TeamScore::Event::Death);
   auto action=s.coopRecovery.tick(s.coopMarines,threats);using Action=trilogy::coop::Recovery::Action;
   if(survival())for(unsigned id=0;id<2;++id)if(old[id].health&&!s.coopMarines[id].health)++g_survival.deaths;
   if(action==Action::ReturnToLobby){if(survival()){activate(0);explorerSurvivalFinish(s,false);}lobby();return;}if(action==Action::RespawnHost||action==Action::RespawnGuest){
    if(!survival()||(!g_survival.cfg.queensRevenge()&&g_survival.lives>1)){
     if(survival())--g_survival.lives;
     respawn(action==Action::RespawnHost?0:1);
    }
   }
   if(survival()){
    activate(s.coopMarines[0].health?0:1);
    const unsigned survivor=selected;
    const bool menuWasOpen=s.menu.open;s.menu.open=false;
    explorerSurvivalTick(s);
    capture();activate(survivor);
    if(!g_survival.active){
     s.menu.coopScoreText=L"Survival victory\n"+scores();
     activate(0);s.finishReplayRecording();finished=true;phase=Phase::Finished;
     s.menu.open=true;s.menu.hasGame=true;s.menu.coopSession=s.menu.coopHost=s.menu.coopFinished=true;
     s.menu.page="coop-results";s.menu.selection=0;snapshotClock=1;return;
    }
    s.menu.open=menuWasOpen;
   }
   auto live=projectiles();for(auto it=owners.begin();it!=owners.end();)if(std::find(live.begin(),live.end(),it->first)==live.end())it=owners.erase(it);else ++it;
   activate(0);capture();s.audio->listener(float(s.player.position[0]),float(s.player.position[2]),float(s.yaw*2*Pi/4096));
   s.recordReplayFrame();
  }else if(phase==Phase::Extracting){s.exitFadeTicks=++fade;s.recordReplayFrame();if(fade>=25){s.finishReplayRecording();s.exitFading=false;s.mission.complete=true;phase=Phase::Results;passed=s.mission.eligible();if(passed)s.coopScore.award(0,trilogy::coop::TeamScore::Event::MissionPassed);s.menu.showResults(s.mission.statistics().percent,passed,s.mission.minimumPercent());s.menu.coopHost=true;s.menu.coopSession=true;snapshotClock=1;}}
 }
 std::wstring scores()const{return L"TEAM SCORE: "+std::to_wstring(s.coopScore.total)+L"\nHost kills: "+std::to_wstring(s.coopScore.kills[0])+L"   Guest kills: "+std::to_wstring(s.coopScore.kills[1])+L"\nHost deaths: "+std::to_wstring(s.coopScore.deaths[0])+L"   Guest deaths: "+std::to_wstring(s.coopScore.deaths[1]);}
 static void text(Writer& w,const std::wstring& value){w.text(narrow(value));}
 static std::wstring text(Reader& r){return wide(r.text());}
 void validateReplica(const WorldPlayerState& player,unsigned gridBase,unsigned width,unsigned height){
  auto m=s.mem();int worldX=int(player.position[0])+player.originX,worldZ=int(player.position[2])+player.originZ;if(worldX<0||worldZ<0||worldX>=int(width*512)||worldZ>=int(height*512))throw std::runtime_error("Invalid co-op player location");if(m.d(0xa0c40)!=gridBase||m.w(0xa0c30)!=width||m.w(0xa0c32)!=height||player.currentCell>=width*height||player.originX!=s.player.originX||player.originZ!=s.player.originZ)throw std::runtime_error("Invalid co-op level state");
  auto chain=[&](unsigned head,unsigned base,unsigned count,unsigned stride,unsigned link){std::set<unsigned> seen;for(unsigned p=head;p;p=m.d(p+link)){if(p<base||(p-base)%stride||(p-base)/stride>=count||!seen.insert(p).second)throw std::runtime_error("Invalid co-op object chain");}};
  chain(m.d(0x2365e0),0x300000,128,208,200);chain(m.d(0x236c44),0x340000,128,48,40);
  // Projectile records come from the fixed original 0x350000 pool.
  unsigned count=0;std::set<unsigned> seen;for(unsigned p=m.d(0x236300);p;p=m.d(p+36)){if(p<0x350000||(p-0x350000)%44||(p-0x350000)/44>=128||!seen.insert(p).second||++count>128)throw std::runtime_error("Invalid co-op projectile chain");for(unsigned offset:{0u,4u})if(m.d(p+offset)!=(offset?0x235f00u:0x235700u)+(p-0x350000)/44*8)throw std::runtime_error("Invalid co-op projectile vector");}
  s.enemies->active();s.pickups.active();
 }
 void snapshot(){
  if(!compatible||!connection.connected()||connection.queuedBytes()>512*1024)return;
  auto w=encode();connection.send(trilogy::coop::Snapshot,w.data);
 }
 Writer encode(){
  Writer w;w.pod(epoch);w.pod(unsigned(phase));w.pod(unsigned(running));w.pod(unsigned(ready));w.pod(s.menu.coopDifficulty);w.pod(lastInput);w.pod(s.coopScore);w.pod(s.coopMarines);
  w.pod(running?survivalMode:s.menu.coopMode);w.pod(running?arena:s.menu.survivalArena);w.pod(g_survival.wave);w.pod(g_survival.alive);w.pod(g_survival.lives);w.pod(g_survival.introTicks);w.pod(g_survival.breakTicks);w.pod(g_survival.baseScore);
  w.text(running?s.mission.sourceCode:std::string());w.text((phase==Phase::Results||phase==Phase::Finished)?s.menu.page:std::string());w.pod(s.menu.selection);w.pod(unsigned(s.menu.resultsReady()));w.pod(s.menu.resultPercent);w.pod(unsigned(passed));w.pod(unsigned(finished));w.pod(unsigned(s.briefingRevealed));w.list(sounds);sounds.clear();
  if(running){
   unsigned original=selected;activate(1);w.pod(s.player);w.pod(s.hud.networkState());w.pod(s.enemies->attachmentState());w.pod(s.steps);w.pod(s.mission.remaining);w.pod(s.exitFadeTicks);w.pod(s.muzzleTicks);text(w,s.notice);w.pod(s.noticeTicks);w.pod(s.yaw);w.pod(s.pitch);w.pod(unsigned(s.pickups.networkMessages().size()));for(auto& message:s.pickups.networkMessages()){text(w,message.text);w.pod(message.length);w.pod(message.age);}
   w.pod(unsigned(s.doors.entries.size()));for(auto& d:s.doors.entries){w.pod(d.amount);w.pod(d.previous);w.pod(d.lock);w.pod(d.remaining);w.pod(unsigned(d.target));}
   w.list(s.logic.lifts);w.list(s.objects.networkRemoved());w.list(s.objects.effects);w.list(s.animatedTextures);w.pod(s.lighting.phase);w.pod(s.lighting.remaining);w.pod(s.lighting.elapsed);
   auto records=s.ps1Lighting.networkRecords();trilogy::coop::delta(w,{reinterpret_cast<const unsigned char*>(records.data()),records.size()*28},lightsSent);
   trilogy::coop::delta(w,s.memory,memorySent);activate(original);
  }
  return w;
 }
 void replica(Reader& r){
  auto incomingEpoch=r.pod<unsigned>(),incomingPhase=r.pod<unsigned>(),hasWorld=r.pod<unsigned>(),isReady=r.pod<unsigned>(),difficulty=r.pod<unsigned>();
  if(incomingPhase>unsigned(Phase::Finished)||hasWorld>1||difficulty>2||isReady>1)throw std::runtime_error("Invalid co-op session state");
  auto acknowledgedInput=r.pod<unsigned>();auto score=r.pod<trilogy::coop::TeamScore>();auto marines=r.pod<std::array<trilogy::coop::Marine,2>>();auto mode=r.pod<unsigned>(),map=r.pod<unsigned>(),wave=r.pod<unsigned>(),alive=r.pod<unsigned>(),lives=r.pod<unsigned>(),intro=r.pod<unsigned>(),countdown=r.pod<unsigned>();auto survivalScore=r.pod<std::uint64_t>();auto code=r.text(),page=r.text();auto selection=r.pod<unsigned>(),revealed=r.pod<unsigned>();auto percent=r.pod<std::array<unsigned,3>>();auto success=r.pod<unsigned>(),done=r.pod<unsigned>(),briefing=r.pod<unsigned>();auto soundEvents=r.list<Sound>(64);
  if(mode>2||map>=std::size(SurvivalArenaNames)||wave>10||alive>128||lives>5||intro>100||countdown>3000)throw std::runtime_error("Invalid co-op Survival state");
  if(score.total<0||score.total>999999999||selection>20||revealed>1||success>1||done>1||briefing>1||std::any_of(percent.begin(),percent.end(),[](unsigned v){return v>100;}))throw std::runtime_error("Invalid co-op assessment");
  if(page!=""&&page!="results"&&page!="coop-results"&&page!="coop-scores")throw std::runtime_error("Invalid host menu");
  for(auto& marine:marines)if(marine.health>65535||marine.armor>65535||marine.weapon>4||marine.yaw<0||marine.yaw>4095)throw std::runtime_error("Invalid co-op player summary");
  phase=Phase(incomingPhase);s.menu.coopDifficulty=difficulty;s.menu.coopMode=mode;s.menu.survivalArena=map;ready=isReady!=0;s.menu.coopReady=ready;
  if(hasWorld){
   bool changed=incomingEpoch!=receivedEpoch;
   if(changed){
    survivalMode=mode;arena=map;s.coopGame=true;
    if(survival()){
     ExplorerLevelConfig cfg;
     if(!explorerLoadSurvivalConfig(s.root/"GameData"/SurvivalArenaFolders[map],cfg))
      throw std::runtime_error("Co-op arena is missing");
     if(mode==2)cfg.setQueensRevenge();
     g_survival={};g_survival.active=true;g_survival.cfg=std::move(cfg);
     s.menu.survivalArena=map;s.level=g_survival.cfg.map;s.difficulty=difficulty;s.load();
     if(!explorerSurvivalSetup(s))throw std::runtime_error("Co-op arena setup failed");
    }
    else{g_survival={};s.level=findLevel(code);s.difficulty=difficulty;s.load();}
    memoryReceived.clear();lightsReceived.clear();receivedEpoch=incomingEpoch;s.briefingStarted=GetTickCount64();
   }else if(survivalMode!=mode||(mode&&arena!=map))throw std::runtime_error("Co-op mode changed inside a level");
   if(survival()){
    if(code!=s.mission.sourceCode)throw std::runtime_error("Co-op arena differs");
    g_survival.wave=wave;g_survival.alive=alive;g_survival.lives=lives;
    g_survival.introTicks=intro;g_survival.breakTicks=countdown;g_survival.baseScore=survivalScore;
   }
   auto world=r.pod<WorldPlayerState>();auto hud=r.pod<ExplorerHud::NetworkState>();auto attachment=r.pod<ExplorerEnemies::AttachmentState>();auto steps=r.pod<unsigned>(),remaining=r.pod<unsigned>(),fade=r.pod<unsigned>(),muzzle=r.pod<unsigned>();auto notice=text(r);auto noticeTicks=r.pod<unsigned>();auto yaw=r.pod<int>(),pitch=r.pod<int>();auto messageCount=r.pod<unsigned>();if(messageCount>4)throw std::runtime_error("Invalid co-op pickup queue");std::vector<ExplorerPickups::QueuedMessage> messages;for(unsigned i=0;i<messageCount;++i)messages.push_back({text(r),r.pod<unsigned>(),r.pod<unsigned>()});
   if(fade>25||remaining>18000||muzzle>10||noticeTicks>3000||yaw<0||yaw>4095||pitch< -910||pitch>910)throw std::runtime_error("Invalid co-op view state");
   auto doors=r.pod<unsigned>();if(doors!=s.doors.entries.size())throw std::runtime_error("Co-op door catalogue differs");for(auto& d:s.doors.entries){d.amount=r.pod<float>();d.previous=r.pod<float>();d.lock=r.pod<unsigned>();d.remaining=r.pod<unsigned>();auto target=r.pod<unsigned>();if(!std::isfinite(d.amount)||!std::isfinite(d.previous)||d.amount<0||d.amount>1||d.previous<0||d.previous>1||target>1)throw std::runtime_error("Invalid co-op door animation");d.target=target!=0;}
   auto lifts=r.list<ExplorerLift>(512);if(lifts.size()!=s.logic.lifts.size())throw std::runtime_error("Co-op lift catalogue differs");s.logic.lifts=std::move(lifts);s.objects.networkRemoved(r.list<unsigned char>(65536));s.objects.effects=r.list<ExplorerObjects::Effect>(512);s.animatedTextures=r.list<unsigned>(65536);
   auto lightPhase=r.pod<unsigned>();s.lighting.remaining=r.pod<unsigned>();s.lighting.elapsed=r.pod<unsigned>();if(lightPhase>2)throw std::runtime_error("Invalid co-op lighting bank");if(s.lighting.phase!=lightPhase){s.ps1Lighting.load(s.level,s.root,int(lightPhase));s.lighting.phase=lightPhase;}
   auto records=s.ps1Lighting.networkRecords();trilogy::coop::applyDelta(r,lightsReceived,records.size()*28);if(!records.empty())std::memcpy(records.data(),lightsReceived.data(),lightsReceived.size());s.ps1Lighting.networkRecords(records);
   s.previousEnemies.clear();if(!changed)for(auto e:s.enemies->active()){auto m=s.mem();s.previousEnemies[e]={short(m.s(e)),short(m.s(e+2)),short(m.s(e+4))};}unsigned gridBase=s.mem().d(0xa0c40),width=s.grid.width,height=s.grid.height;trilogy::coop::applyDelta(r,memoryReceived,s.memory.size());std::copy(memoryReceived.begin(),memoryReceived.end(),s.memory.begin());validateReplica(world,gridBase,width,height);
   s.previousPosition=s.player.position;s.player=world;s.hud.networkState(hud);serverWeapon=hud.selected;
   if(changed||int(acknowledgedInput-pendingWeaponSequence)>=0)pendingWeapon=~0u;
   if(pendingWeapon!=~0u&&s.hud.owns(pendingWeapon))s.hud.select(pendingWeapon);else std::copy_n(memoryReceived.begin()+0x31d000,0x50,s.memory.begin()+0x31d000);s.pickups.networkMessages(messages);s.enemies->attachmentState(attachment);s.steps=steps;s.mission.remaining=remaining;s.exitFadeTicks=fade;s.exitFading=phase==Phase::Extracting;s.muzzleTicks=muzzle;s.notice=notice;s.noticeTicks=noticeTicks;
   if(changed||!s.coopMarines[1].health||phase!=Phase::Playing){s.yaw=yaw;s.pitch=pitch;}
   for(unsigned i=0;i<s.cells.size();++i)for(unsigned j=0;j<16;++j)s.cells[i].bytes[j]=s.mem().b(gridBase+i*16+j);
   s.mission.briefing=phase==Phase::Briefing;s.mission.complete=phase==Phase::Results||phase==Phase::Finished;s.briefingRevealed=briefing!=0;
   s.menu.hasGame=true;s.coopGame=s.menu.coopSession=true;s.coopLocal=1;
   if(phase==Phase::Results||phase==Phase::Finished){s.menu.open=true;s.menu.page=page.empty()?"coop-results":page;s.menu.selection=selection;s.menu.resultPercent=percent;s.menu.resultsRevealed=revealed!=0;s.menu.assessmentPassed=success!=0;s.menu.coopFinished=done!=0;}
   else if(changed||s.menu.page=="coop-results"||s.menu.page=="results"||s.menu.page=="coop-lobby"||s.menu.page=="coop-scores")s.menu.open=false;
  }else{if(running)s.audio->pause(false);if(survival()){g_survival={};s.level=findLevel("111");}survivalMode=0;s.coopGame=false;s.mission.briefing=false;s.menu.hasGame=false;s.menu.open=true;s.menu.page="coop-lobby";}
  s.audio->listener(float(s.player.position[0]),float(s.player.position[2]),float(s.yaw*2*Pi/4096));for(auto event:soundEvents){if((event.slot==ExplorerAudio::SupplyAlarm||event.slot==explorer_survival::kQueenHiss)&&survival()&&!event.weapon){s.audio->play(event.slot);continue;}if((event.slot==21||event.slot==22)&&!event.weapon){if(event.player==1)s.audio->play(event.slot,1.f);continue;}if(event.slot>255||event.weapon>1||event.player>1||std::abs(std::int64_t(event.x))>65536||std::abs(std::int64_t(event.z))>65536)throw std::runtime_error("Invalid co-op sound event");if(event.weapon&&event.player==1)s.audio->playWeapon(event.slot,event.slot==11);else s.audio->playAt(event.slot,float(event.x),float(event.z));}
  replicaAge=0;running=hasWorld!=0;s.coopScore=score;s.coopMarines=marines;s.menu.coopHost=false;if(!isHost()&&running&&phase==Phase::Extracting)s.recordReplayFrame();if(phase==Phase::Results||phase==Phase::Finished)s.finishReplayRecording();
   s.menu.coopScoreText=(survival()&&phase==Phase::Finished?L"Survival victory\n":L"")+scores();r.end();
 }
 void failure(std::string why){
  fs::create_directories(s.root/"decompilation/user/coop");std::ofstream(s.root/"decompilation/user/coop/errors.log",std::ios::app)<<"Co-op session stopped: "<<why<<"\n";ExplorerDiagnostics::error("co-op session stopped: "+why);
  leave();s.menu.message=wide(why);
 }
public:
 Connection connection;Phase phase=Phase::Lobby;
 explicit ExplorerCoopRuntime(Scene& scene):s(scene){}
 bool active()const{return connection.active();}bool playing()const{return running;}bool isHost()const{return connection.hosting();}
 void leave(){
  const bool hosting=isHost();if(running&&hosting)activate(0);connection.close();if(survival()){if(hosting)explorerSaveSurvivalRun(s,L"Quit");g_survival={};s.level=findLevel("111");}survivalMode=0;running=false;compatible=helloSent=ready=false;s.coopTick={};s.coopExit={};s.coopRestore={};s.coopSound={};s.coopSurvivalPickup={};s.coopQueenReward={};s.coopGame=s.menu.coopSession=false;s.coopDriving=false;s.coopWorld=s.coopEnemyPass=true;s.enemies->coopTarget={};s.enemies->coopProjectile={};s.enemies->coopEmitted={};s.load();s.menu.show(false);s.menu.difficultyLocked=false;s.audio->pause(false);phase=Phase::Lobby;memorySent.clear();memoryReceived.clear();
 }
 bool command(ExplorerMenu::Command command){
  using Menu=ExplorerMenu;
  if(command==Menu::CoopHost||command==Menu::CoopJoin){
   try{unsigned port=std::stoul(s.menu.coopPort);if(command==Menu::CoopHost)connection.host(port);else connection.join(narrow(s.menu.coopAddress),port,narrow(s.menu.coopInvitation));buildFingerprint();sequence=lastInput=receivedEpoch=0;inputTokens=64;tokenAt=0;pendingWeapon=~0u;pendingWeaponSequence=0;serverWeapon=0;accumulator=0;memorySent.clear();memoryReceived.clear();lightsSent.clear();lightsReceived.clear();sounds.clear();helloSent=compatible=ready=false;running=false;phase=Phase::Lobby;if(!connection.active()){auto why=connection.status();failure(why);return true;}s.menu.coopHost=command==Menu::CoopHost;s.menu.coopSession=true;s.menu.open=true;s.menu.hasGame=false;s.menu.page="coop-lobby";s.menu.selection=0;s.menu.coopStatus=wide(connection.status());s.audio->pause(false);}catch(const std::exception& error){failure(error.what());}return true;
  }
  if(command==Menu::CoopLeave){leave();return true;}
  if(command==Menu::CoopStart){if(!isHost()||!compatible||!ready){s.menu.message=L"Wait for your teammate to connect and choose Ready.";return true;}try{if(s.menu.coopMode)newSurvival();else newLevel("111",true);}catch(const std::exception& error){failure(error.what());}return true;}
  if(command==Menu::CoopReady){if(!isHost()&&compatible&&!ready&&phase==Phase::Lobby){Writer w;w.pod(1u);connection.send(trilogy::coop::Ready,w.data);ready=true;s.menu.coopReady=true;}return true;}
  if(command==Menu::CoopAdvance){if(!isHost()||phase!=Phase::Results&&phase!=Phase::Finished)return true;if(finished){lobby();return true;}advanceCampaign();return true;}
  if(command==Menu::CoopCopyInvite||command==Menu::CoopPasteInvite){
   if(!OpenClipboard(nullptr)){s.menu.message=L"Clipboard is busy. Try again.";return true;}
   if(command==Menu::CoopCopyInvite){auto value=wide(connection.invitation());if(value.size()==88){auto memory=GlobalAlloc(GMEM_MOVEABLE,(value.size()+1)*sizeof(wchar_t));if(memory){auto data=GlobalLock(memory);std::memcpy(data,value.c_str(),(value.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);EmptyClipboard();if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);else s.menu.message=L"Invitation copied. Share privately with your friend.";}}else s.menu.message=L"Host is still preparing the invitation.";}
   else{auto handle=GetClipboardData(CF_UNICODETEXT);if(handle){auto size=GlobalSize(handle);auto data=static_cast<const wchar_t*>(GlobalLock(handle));if(data){size_t count=0;while(count<size/sizeof(wchar_t)&&count<=88&&data[count])++count;if(count==88)s.menu.coopInvitation.assign(data,count);else s.menu.message=L"Paste the complete 88-character invitation.";GlobalUnlock(handle);}}}CloseClipboard();return true;
  }
  return false;
 }
 void briefingEnter(){if(!running||!isHost()||phase!=Phase::Briefing)return;if(!s.briefingRevealed)s.briefingRevealed=true;else{s.mission.briefing=false;phase=Phase::Playing;}snapshotClock=1;}
 void poll(){
  if(!active())return;
  try{
   connection.poll();if(!active()){failure(connection.status());return;}
   s.menu.coopConnected=connection.connected();if(!running)s.menu.coopStatus=wide(connection.status())+(compatible?L"\nTeammate connected. Choose Ready on guest, then Start on host.":L"\nDirect internet play: host TCP port must be reachable.\nShare your public IP and private invitation code.");
   if(connection.connected()&&!helloSent){Writer w;w.pod(trilogy::coop::Protocol);w.pod(buildFingerprint());w.pod(unsigned(isHost()));connection.send(trilogy::coop::Hello,w.data);helloSent=true;}
   auto tokenNow=GetTickCount64();if(tokenAt)inputTokens=std::min(64.,inputTokens+(tokenNow-tokenAt)*.09);tokenAt=tokenNow;
   for(auto& message:connection.receive()){
    Reader r{message.bytes};if(message.type==trilogy::coop::Hello){if(compatible||r.pod<unsigned>()!=trilogy::coop::Protocol||r.pod<std::uint64_t>()!=buildFingerprint()||r.pod<unsigned>()==unsigned(isHost()))throw std::runtime_error("Co-op requires identical game builds and installed campaign and Survival data");r.end();compatible=true;snapshotClock=1;}
    else if(!compatible)throw std::runtime_error("Co-op message before build verification");
    else if(message.type==trilogy::coop::Input&&isHost()){
     if(inputTokens<1)throw std::runtime_error("Co-op input rate exceeded");--inputTokens;auto input=r.pod<Input>();r.end();if(input.forward< -1000||input.forward>1000||input.strafe< -1000||input.strafe>1000||input.yaw<0||input.yaw>4095||input.pitch< -910||input.pitch>910||input.buttons&~15u||input.weapon>4)throw std::runtime_error("Invalid co-op player input");if(int(input.sequence-lastInput)>0){lastInput=input.sequence;auto edges=remote.buttons&(trilogy::coop::Use|trilogy::coop::Grenade);remote=input;remote.buttons|=edges;lastRemote=GetTickCount64();}
    }else if(message.type==trilogy::coop::Ready&&isHost()){if(r.pod<unsigned>()!=1||phase!=Phase::Lobby)throw std::runtime_error("Invalid co-op readiness");r.end();ready=true;snapshotClock=1;}
    else if(message.type==trilogy::coop::Snapshot&&!isHost())replica(r);
    else if(message.type==trilogy::coop::Leave){leave();return;}
    else throw std::runtime_error("Unexpected co-op message for this role");
   }
   s.menu.coopScoreText=scores();
  }catch(const std::exception& error){failure(error.what());}
 }
 void frame(double elapsed,bool controls,double forward,double strafe,bool sprint,bool grenade){
  if(!active())return;
  try{
   if(controls){s.grenadeRequested|=grenade;pendingFire|=s.firing;}else pendingFire=false;
   if(!isHost()&&running&&s.hud.selected()!=serverWeapon&&(pendingWeapon==~0u||s.hud.selected()!=pendingWeapon)){pendingWeapon=s.hud.selected();pendingWeaponSequence=sequence+1;}
   Input input;input.yaw=s.yaw&4095;input.pitch=std::clamp(s.pitch,-910,910);input.weapon=pendingWeapon==~0u?s.hud.selected():pendingWeapon;if(controls&&running&&phase==Phase::Playing){input.forward=short(std::clamp(forward,-1.,1.)*1000);input.strafe=short(std::clamp(strafe,-1.,1.)*1000);input.buttons=(s.firing||pendingFire?trilogy::coop::Fire:0)|(s.useRequested?trilogy::coop::Use:0)|(grenade||s.grenadeRequested?trilogy::coop::Grenade:0)|(sprint?trilogy::coop::Sprint:0);}
   accumulator+=std::min(elapsed,.1);while(accumulator>=1./30){input.sequence=++sequence;if(isHost())hostStep(input);else if(compatible&&running&&phase==Phase::Playing){s.recordReplayFrame();Writer w;w.pod(input);connection.send(trilogy::coop::Input,w.data);}input.buttons&=~(trilogy::coop::Use|trilogy::coop::Grenade);remote.buttons&=~(trilogy::coop::Use|trilogy::coop::Grenade);s.useRequested=s.grenadeRequested=false;pendingFire=false;accumulator-=1./30;++ticks;}
   replicaAge+=elapsed;s.renderAlpha=isHost()?std::min(1.,accumulator*30):std::min(1.,replicaAge*15);
   snapshotClock+=elapsed;if(isHost()&&snapshotClock>=1./15){snapshot();snapshotClock=0;}
   if(running&&phase==Phase::Playing&&s.menu.open)s.audio->pause(false);
  }catch(const std::exception& error){s.coopDriving=false;failure(error.what());}
 }
 // Deterministic gameplay tests use the same host step and snapshot codec.
 void testReplicate(ExplorerCoopRuntime& guest){auto w=encode();Reader r{w.data};guest.replica(r);}
 unsigned testPickupCount(unsigned id){activate(id);auto value=unsigned(s.pickups.active().size());activate(0);return value;}
 void testGuestInput(Input input){remote=input;lastRemote=GetTickCount64();}
 void testSpawnPersonalTag(){
  auto m=s.mem();unsigned ruleIndex=~0u;for(unsigned i=0;i<m.w(0x236c4c);++i)if(m.b(m.d(0x236c34)+i*8+2)==15){ruleIndex=i;break;}if(ruleIndex==~0u)throw std::runtime_error("Co-op tag fixture missing rule");
  unsigned gx=(int(s.player.position[0])+s.player.originX)/512,gz=(int(s.player.position[2])+s.player.originZ)/512;
  auto a=testPickupCount(0),b=testPickupCount(1);eachPickup([&]{auto rule=m.d(0x236c34)+ruleIndex*8;m.b(rule,gx);m.b(rule+1,gz);m.b(rule+5,0);m.b(rule+6,1);s.pickups.trigger(1,ruleIndex);});
  if(testPickupCount(0)!=a+1||testPickupCount(1)!=b+1)throw std::runtime_error("Triggered pickup was not instanced for both players");
 }
 unsigned testCombatFixture(){
  activate(0);auto m=s.mem();unsigned target=0;for(auto e:s.enemies->active())if(m.b(e+0x74)==6){target=e;break;}if(!target)throw std::runtime_error("Co-op combat fixture needs warrior");
  m.d(0x2365e0,target);m.d(target+200,0);m.d(0x22ceb8,0);resetProjectilePool(m);owners.clear();
  for(unsigned i=0;i<s.cells.size();++i){auto c=m.d(0xa0c40)+i*16;m.b(c+7,0);m.b(c+10,0);m.b(c+11,0);m.b(c+13,128);m.w(c+8,65535);}
  Vec3s position{short(4096-s.player.originX),0,short(4096-s.player.originZ)};testPosition(1,position);position[0]-=1536;testPosition(0,position);position[0]+=1536;
  m.w(target,position[0]);m.w(target+2,32);m.w(target+4,position[2]+768);m.w(target+0x58,1);m.w(target+0x4e,0);m.b(target+0x6f,0);m.d(target+0x78,m.d(0xa0c40)+((4096+768)/512*s.grid.width+4096/512)*16);capture();return target;
 }
 void testClearEnemies(){s.mem().d(0x2365e0,0);s.mem().d(0x22ceb8,0);}
 bool testCompatible()const{return compatible;}bool testReady()const{return ready;}
 void testSurvival(unsigned mode,unsigned map){s.menu.coopMode=mode;s.menu.survivalArena=map;newSurvival();}
 void testLevel(const std::string& code){newLevel(code,true);phase=Phase::Playing;s.mission.briefing=false;}
 void testStart(){s.menu.coopDifficulty=1;newLevel("111",true);phase=Phase::Playing;s.mission.briefing=false;remote.yaw=s.yaw&4095;remote.pitch=s.pitch;lastRemote=GetTickCount64();}
 void testStep(Input input){hostStep(input);}
 void testDamage(unsigned id,unsigned health){activate(id);s.mem().w(0xa0ab8,health);s.player.health=static_cast<unsigned short>(health);capture();activate(0);}
 void testPosition(unsigned id,Vec3s position){activate(id);s.player.position=position;for(unsigned a=0;a<3;++a)s.mem().w(0xa0a48+a*2,position[a]);capture();activate(0);}
 unsigned testAmmo(unsigned id){activate(id);auto value=s.mem().w(0xa0abe);activate(0);return value;}
 void testObjectivesPassed(){auto m=s.mem();for(auto kind:s.mission.kinds){if(kind==35)m.w(0xa0ae2,m.w(0xa0bc8+kind*2));else m.w(0xa0ae6+kind*2,m.w(0xa0bc8+kind*2));}}
 void testAdvanceCampaign(){advanceCampaign();}
 void testExtract(){capture();phase=Phase::Extracting;fade=24;}
};
