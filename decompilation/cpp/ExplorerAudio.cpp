#include "ExplorerDiagnostics.h"
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <future>
#include "ExplorerAudio.h"
#include "ExplorerAssets.h"
#include <array>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <stdexcept>
namespace {constexpr unsigned OutputRate=44100, BufferFrames=256,TrackerRestFrames=OutputRate*2;
struct Sound{std::vector<float> samples;unsigned rate=11025,channels=1;};struct Voice{std::shared_ptr<Sound> sound;double position=0;float gain=.5f;bool loop=false;float pan=0;unsigned tracker=0;bool weapon=false;bool menu=false;unsigned ambient=0;bool spatial=false;float x=0,z=0;std::array<float,32> history{};unsigned cursor=0;std::array<float,2> filtered{};bool movie=false;};}
namespace {
// 0x80043ecc in SLUS_000.07: PS1 SPU sound bank loader (PERMSFX.BIN at slot 0, ###SFX.BIN at slot 41).
std::vector<std::shared_ptr<Sound>> ps1SoundBank(const std::filesystem::path& path){
    auto b=explorerRead(path);
    auto word=[&](size_t i){if(i+2>b.size())throw std::runtime_error("Truncated PS1 sound bank");return unsigned(b[i])|(unsigned(b[i+1])<<8);};
    auto dword=[&](size_t i){return word(i)|(word(i+2)<<16);};
    if(b.size()<8)throw std::runtime_error("Invalid PS1 sound bank header");
    unsigned count=dword(0),spuBytes=dword(4);
    size_t dataStart=8ull+count*56ull;
    if(!count||count>128||dataStart+spuBytes!=b.size())throw std::runtime_error("Invalid PS1 sound bank layout");
    static constexpr int k0[]={0,60,115,98,122},k1[]={0,0,-52,-55,-60};
    std::vector<std::shared_ptr<Sound>> result(count);
    for(unsigned i=0;i<count;++i){
        size_t rec=8+i*56;
        unsigned offset=dword(rec+8),pitch=dword(rec+20);
        if(pitch<=1||offset>=spuBytes)continue;
        auto sound=std::make_shared<Sound>();
        sound->rate=std::max(1000u,unsigned(std::lround(44100.0*pitch/4096.0)));
        int s1=0,s2=0;
        for(size_t pos=dataStart+offset;pos+16<=b.size();pos+=16){
            unsigned shift=std::min(12u,unsigned(b[pos]&0xf)),flt=unsigned((b[pos]>>4)&7);
            if(flt>4)flt=0;
            unsigned flags=b[pos+1];
            for(unsigned byteIndex=2;byteIndex<16;++byteIndex){
                unsigned byte=b[pos+byteIndex];
                for(unsigned half=0;half<2;++half){
                    int nibble=int((byte>>(half*4))&0xf);
                    int sample=(int(static_cast<signed char>(nibble<<4))<<8)>>shift;
                    sample+=(s1*k0[flt]+s2*k1[flt]+32)>>6;
                    sample=std::clamp(sample,-32768,32767);
                    s2=s1;s1=sample;
                    sound->samples.push_back(float(sample));
                }
            }
            if(flags&1)break;
        }
        if(!sound->samples.empty())result[i]=std::move(sound);
    }
    return result;
}
}
struct ExplorerAudio::Impl {
    HANDLE event{};std::thread worker;std::atomic<bool> stop=false,deviceReady=false;std::atomic<unsigned> submitted=0,starved=0;std::atomic<long> deviceError=0;unsigned deviceFrames=0;std::filesystem::path statusPath;std::mutex mutex;unsigned trackNumber=0;bool paused=false,musicEnabled=true;float sfxVolume=1,musicVolume=1;bool moviePaused=false;bool headphones=false;float listenerX=0,listenerZ=0,listenerYaw=0;
    std::vector<Voice> voices;std::vector<std::shared_ptr<Sound>> bank;std::map<std::string,std::shared_ptr<Sound>> cache;std::map<std::string,std::vector<std::shared_ptr<Sound>>> ps1Banks;std::shared_ptr<Sound> track;std::array<std::shared_ptr<Sound>,2> trackerSamples;unsigned missing{},trackerTailFrames{},ps1Weapons{},ps1Permanent{},ps1Level{};
    const std::vector<std::shared_ptr<Sound>>& loadPs1Bank(const std::filesystem::path& root,const std::string& relative){
        auto found=ps1Banks.find(relative);
        if(found!=ps1Banks.end())return found->second;
        auto full=root/"GameData/PS1/CD"/relative;
        auto decoded=std::filesystem::exists(full)?ps1SoundBank(full):std::vector<std::shared_ptr<Sound>>{};
        return ps1Banks.emplace(relative,std::move(decoded)).first->second;
    }
    // Both tracker tones share one channel; a clear sweep (21) must never lock
    // out or delay an incoming enemy contact ping (22).
    bool queueTracker(unsigned slot,float gain){
        auto sample=trackerSamples[slot==22?1:0];if(paused||!sample||voices.size()>=16||trackerTailFrames)return false;
        if(slot==22){
            if(std::any_of(voices.begin(),voices.end(),[](const Voice& v){return v.tracker==22;}))return false;
            std::erase_if(voices,[](const Voice& v){return v.tracker==21;});
        }else if(std::any_of(voices.begin(),voices.end(),[](const Voice& v){return v.tracker!=0;}))return false;
        voices.push_back({sample,0,.24f*gain,false,0,slot});return true;
    }
    bool queueWeapon(unsigned slot,bool looping){
        if(paused||slot>=bank.size()||!bank[slot])return false;auto sample=bank[slot];
        if(looping&&std::any_of(voices.begin(),voices.end(),[&](const Voice& voice){return voice.weapon&&voice.loop&&voice.sound==sample;}))return true;
        std::erase_if(voices,[](const Voice& voice){return voice.weapon;});
        voices.push_back({sample,0,.24f,looping,0,false,true});return true;
    }
    bool queueMenu(unsigned event){if(event>1||bank.size()<2||!bank[event])return false;std::erase_if(voices,[](const Voice& v){return v.menu;});voices.push_back({bank[event],0,.24f,false,0,false,false,true});return true;}
    void loadTrack(const std::filesystem::path& root,const std::string& code,int trackNumber){
        std::erase_if(voices,[](const Voice& v){return v.loop&&!v.weapon&&!v.ambient;});track.reset();
        auto& p=*this;
    p.trackNumber=0;
    if(trackNumber<2)return;
    char name[32];
    std::snprintf(name,sizeof(name),"track%02d.wav",trackNumber);
    const auto musicPath=root/"GameData/PS1/Music"/name;
    if(std::filesystem::exists(musicPath))p.trackNumber=trackNumber;

    if(std::filesystem::exists(musicPath)){auto data=explorerRead(musicPath);auto word=[&](size_t o){if(o+2>data.size())throw std::runtime_error("Truncated music WAV");return unsigned(data[o])|(unsigned(data[o+1])<<8);};auto dword=[&](size_t o){return word(o)|(word(o+2)<<16);};if(data.size()<12||std::string(data.begin(),data.begin()+4)!="RIFF"||std::string(data.begin()+8,data.begin()+12)!="WAVE")throw std::runtime_error("Music must be PCM WAV");unsigned rate=0,channels=0,bits=0;size_t begin=0,length=0;for(size_t o=12;o+8<=data.size();){auto n=dword(o+4);if(o+8ull+n>data.size())throw std::runtime_error("Truncated music chunk");std::string tag(data.begin()+o,data.begin()+o+4);if(tag=="fmt "){if(n<16||word(o+8)!=1)throw std::runtime_error("Only PCM music WAV is supported");channels=word(o+10);rate=dword(o+12);bits=word(o+22);}if(tag=="data"){begin=o+8;length=n;}o+=8+n+(n&1);}if(!rate||channels<1||channels>2||(bits!=8&&bits!=16)||!length||length%(channels*(bits/8)))throw std::runtime_error("Unsupported music WAV layout");p.track=std::make_shared<Sound>();p.track->rate=rate;p.track->channels=channels;for(size_t o=begin;o<begin+length;o+=bits/8)p.track->samples.push_back(bits==8?(int(data[o])-128)*256.f:float(static_cast<short>(word(o))));if(p.musicEnabled)p.voices.push_back({p.track,0,.22f,true});}
    }
    void fill(short* out,unsigned count=BufferFrames){std::lock_guard lock(mutex);for(unsigned frame=0;frame<count;++frame){if(!paused&&trackerTailFrames)--trackerTailFrames;float mixed[2]{};for(auto& voice:voices){if((voice.movie&&moviePaused)||(paused&&!voice.menu&&!voice.movie))continue;auto& sound=*voice.sound;auto frames=sound.samples.size()/sound.channels;if(!frames)continue;if(voice.position>=frames){if(voice.loop)voice.position=std::fmod(voice.position,double(frames));else continue;}auto a=size_t(voice.position),b=std::min(a+1,frames-1);float t=float(voice.position-a);float pan=voice.pan,gain=voice.gain,front=1;if(voice.spatial){float dx=voice.x-listenerX,dz=voice.z-listenerZ,distance=std::hypot(dx,dz);pan=std::clamp(-(dx*std::cos(listenerYaw)+dz*std::sin(listenerYaw))/std::max(1.f,distance),-1.f,1.f);front=(-dx*std::sin(listenerYaw)+dz*std::cos(listenerYaw))/std::max(1.f,distance);gain=voice.gain/(1+distance/1400);}
                float mono=sound.samples[a*sound.channels]*(1-t)+sound.samples[b*sound.channels]*t;voice.history[voice.cursor]=mono;
                for(unsigned c=0;c<2;++c){unsigned channel=std::min(c,sound.channels-1);float sample=sound.samples[a*sound.channels+channel]*(1-t)+sound.samples[b*sound.channels+channel]*t;
                    float ear=c==0?pan:-pan;
                    if(headphones&&voice.spatial&&sound.channels==1){unsigned delay=unsigned(std::max(0.f,ear)*26);sample=voice.history[(voice.cursor+32-delay)%32];float coefficient=1-.65f*std::max(0.f,ear)-.15f*std::max(0.f,-front);voice.filtered[c]+=coefficient*(sample-voice.filtered[c]);sample=voice.filtered[c];sample*=std::sqrt(std::max(0.f,1-ear))*.70710678f;}
                    else sample*=c==0?(pan>0?1-pan:1):(pan<0?1+pan:1);
                    mixed[c]+=sample*gain*((voice.movie||(voice.loop&&!voice.weapon&&!voice.ambient))?musicVolume:sfxVolume);
                }voice.cursor=(voice.cursor+1)%32;voice.position+=double(sound.rate)/OutputRate;if(voice.tracker&&voice.position>=frames){if(voice.tracker==22)trackerTailFrames=std::max(1u,deviceFrames)+TrackerRestFrames;voice.tracker=0;}}
            for(unsigned c=0;c<2;++c)out[frame*2+c]=static_cast<short>(std::tanh(mixed[c]/32768.f)*30000.f);}
        std::erase_if(voices,[](const Voice& v){return !v.loop&&v.position>=v.sound->samples.size()/v.sound->channels;});}
};
ExplorerAudio::ExplorerAudio():impl(std::make_unique<Impl>()){}
ExplorerAudio::~ExplorerAudio(){auto& p=*impl;p.stop=true;if(p.event)SetEvent(p.event);if(p.worker.joinable())p.worker.join();if(p.event)CloseHandle(p.event);
    if(!p.statusPath.empty()&&p.submitted){std::ofstream log(p.statusPath);log<<"{\n  \"backend\": \"WASAPI shared event\",\n  \"sample_rate\": "<<OutputRate<<",\n  \"endpoint_buffer_frames\": "<<p.deviceFrames<<",\n  \"submitted_buffers\": "<<p.submitted<<",\n  \"empty_buffer_observations\": "<<p.starved<<",\n  \"last_hresult\": "<<p.deviceError<<"\n}\n";}
}
bool ExplorerAudio::start(){
    auto& p=*impl;if(p.deviceReady)return true;if(p.worker.joinable())return false;
    p.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!p.event)return false;
    std::promise<bool> initialized;auto result=initialized.get_future();
    p.worker=std::thread([&p,started=std::move(initialized)]()mutable{
        IMMDeviceEnumerator* enumerator=nullptr;IMMDevice* endpoint=nullptr;IAudioClient* client=nullptr;IAudioRenderClient* render=nullptr;
        HANDLE scheduling=nullptr;DWORD task=0;bool signaled=false,running=false;HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        auto check=[&](HRESULT hr){if(FAILED(hr)){p.deviceError=hr;throw hr;}};
        try{
            check(apartment);check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&enumerator)));
            check(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&endpoint));check(endpoint->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&client)));
            WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=OutputRate;format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=OutputRate*4;
            check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,200000,0,&format,nullptr));
            check(client->SetEventHandle(p.event));check(client->GetBufferSize(&p.deviceFrames));check(client->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(&render)));
            // WASAPI owns the queue and tells us the exact writable extent.
            BYTE* buffer=nullptr;check(render->GetBuffer(p.deviceFrames,&buffer));p.fill(reinterpret_cast<short*>(buffer),p.deviceFrames);check(render->ReleaseBuffer(p.deviceFrames,0));++p.submitted;
            scheduling=AvSetMmThreadCharacteristicsW(L"Audio",&task);check(client->Start());running=true;p.deviceReady=true;started.set_value(true);signaled=true;
            while(!p.stop){
                DWORD wake=WaitForSingleObject(p.event,100);if(p.stop)break;if(wake==WAIT_FAILED)check(HRESULT_FROM_WIN32(GetLastError()));
                UINT32 padding=0;check(client->GetCurrentPadding(&padding));if(padding>p.deviceFrames)check(E_UNEXPECTED);if(!padding)++p.starved;
                UINT32 available=p.deviceFrames-padding;if(!available)continue;
                check(render->GetBuffer(available,&buffer));p.fill(reinterpret_cast<short*>(buffer),available);check(render->ReleaseBuffer(available,0));++p.submitted;
            }
        }catch(HRESULT code){char text[64];snprintf(text,sizeof text,"audio device error 0x%08lX",static_cast<unsigned long>(code));ExplorerDiagnostics::error(text);if(!signaled){started.set_value(false);signaled=true;}}
        p.deviceReady=false;if(running)client->Stop();if(scheduling)AvRevertMmThreadCharacteristics(scheduling);
        if(render)render->Release();if(client)client->Release();if(endpoint)endpoint->Release();if(enumerator)enumerator->Release();if(SUCCEEDED(apartment))CoUninitialize();
    });
    return result.get();
}
void ExplorerAudio::pause(bool value){std::lock_guard lock(impl->mutex);impl->paused=value;}
void ExplorerAudio::music(bool value){auto& p=*impl;std::lock_guard lock(p.mutex);p.musicEnabled=value;std::erase_if(p.voices,[](const Voice& v){return v.loop&&!v.weapon&&!v.ambient;});if(value&&p.track)p.voices.push_back({p.track,0,.22f,true});}
void ExplorerAudio::playWeapon(unsigned slot,bool looping){if(replayCue)replayCue({CueType::Weapon,slot,float(looping),0});auto& p=*impl;std::lock_guard lock(p.mutex);if(p.deviceReady)p.queueWeapon(slot,looping);}
void ExplorerAudio::play(unsigned slot,float gain,float pan){if(replayCue)replayCue({CueType::Centered,slot,gain,pan});auto& p=*impl;std::lock_guard lock(p.mutex);if(!p.deviceReady||p.paused||slot>=p.bank.size()||!p.bank[slot]||p.voices.size()>=16||gain<.02f)return;if(slot==21||slot==22){p.queueTracker(slot,std::clamp(gain,0.f,1.f));return;}auto sample=p.bank[slot];unsigned same=0;for(auto& voice:p.voices)if(voice.sound==sample&&!voice.loop){++same;
        // Duplicate callbacks in the same simulation tick must not create a delayed copy.
        if(voice.position<sample->rate*.025)return;
    }
    if((slot==38||slot==44)&&same)return;
    // A new local weapon/door/lift event replaces its old tail rather than comb-filtering it.
    if(slot==12||slot==13||slot==42||slot==43||slot==94||slot==95)std::erase_if(p.voices,[&](const Voice& v){return !v.loop&&v.sound==sample;});
    else if(same>=2)return;
    p.voices.push_back({sample,0,.24f*std::clamp(gain,0.f,1.f),false,std::clamp(pan,-1.f,1.f)});}

void ExplorerAudio::level(const std::filesystem::path& root,const std::string& code,int trackNumber,bool survival,bool queensRevenge){auto& p=*impl;std::lock_guard lock(p.mutex);p.statusPath=root/"decompilation/reports/audio_device_status.json";p.bank.clear();p.voices.clear();p.trackerTailFrames=0;p.track.reset();p.missing=0;p.ps1Weapons=0;p.ps1Permanent=0;p.ps1Level=0;
    p.bank.resize(111);
    const auto& permanent=p.loadPs1Bank(root,"SFX/PERMSFX.BIN");
    if(permanent.size()!=41)throw std::runtime_error("Missing or invalid PS1 permanent sound bank");
    for(unsigned slot=0;slot<41;++slot){
        p.bank[slot]=permanent[slot];
        if(p.bank[slot])++p.ps1Permanent;
        if(slot>=5&&slot<=20&&p.bank[slot])++p.ps1Weapons;
    }
    const auto bankPath=explorerPs1SfxBank(root,code);
    if(!bankPath.empty()){
        const auto& levelBank=p.loadPs1Bank(root,bankPath);
        if(levelBank.size()!=70)throw std::runtime_error("Invalid PS1 level sound bank");
        for(unsigned slot=0;slot<70;++slot){
            p.bank[41+slot]=levelBank[slot];
            if(levelBank[slot])++p.ps1Level;
        }
    }
    {
        // Any level: keep its own samples and fill only creature slots its bank leaves empty from the original banks
        // (a story level's bank can lack a species' cries - the Facehuggers went silent in Section 3). Survival
        // combines species from several chapters the same way.
        for(const auto* donor : {"161", "321", "351"}){
            const auto path=explorerPs1SfxBank(root,donor);
            if(path.empty())continue;
            const auto& samples=p.loadPs1Bank(root,path);
            if(samples.size()!=70)throw std::runtime_error("Invalid PS1 Survival sound bank");
            for(unsigned slot=45;slot<=81;++slot){
                if(!p.bank[slot]&&samples[slot-41]){
                    p.bank[slot]=samples[slot-41];
                    ++p.ps1Level;
                }
            }
        }
    }
    if(queensRevenge){
        const auto donor=explorerPs1SfxBank(root,"113");
        if(!donor.empty()){
            const auto& samples=p.loadPs1Bank(root,donor);
            if(samples.size()==70)p.bank[SupplyAlarm]=samples[SupplyAlarm-41];
        }
    }
    if(!bankPath.empty()&&p.bank[63]){
        const auto donor=explorerPs1SfxBank(root,"111");
        if(!donor.empty()){
            const auto& samples=p.loadPs1Bank(root,donor);
            if(samples.size()==70)for(unsigned slot=67;slot<=73;++slot)
                if(!p.bank[slot])p.bank[slot]=samples[slot-41];
        }
    }
    p.trackerSamples={p.bank[21],p.bank[22]};
    p.loadTrack(root,code,trackNumber);
}
unsigned ExplorerAudio::slots()const{return unsigned(impl->bank.size());}unsigned ExplorerAudio::missingSamples()const{return impl->missing;}unsigned ExplorerAudio::sampleLength(unsigned slot)const{std::lock_guard lock(impl->mutex);return slot<impl->bank.size()&&impl->bank[slot]?unsigned(impl->bank[slot]->samples.size()):0;}bool ExplorerAudio::hasSample(unsigned slot)const{std::lock_guard lock(impl->mutex);return slot<impl->bank.size()&&bool(impl->bank[slot]);}bool ExplorerAudio::musicAvailable()const{return bool(impl->track);}bool ExplorerAudio::ready()const{return bool(impl->deviceReady);}
unsigned ExplorerAudio::validateMixer(unsigned slot){
    auto& p=*impl;
    {std::lock_guard lock(p.mutex);if(p.bank.size()<=slot||!p.bank[slot])throw std::runtime_error("Missing audio sample for mixer test");p.voices.clear();p.paused=false;p.voices.push_back({p.bank[slot],0,.24f,false});}
    unsigned nonzero=0;std::array<short,BufferFrames*2> out{};std::vector<short> reference;
    const auto frames=p.bank[slot]->samples.size();
    const unsigned blocks=unsigned(std::ceil(double(frames)*OutputRate/p.bank[slot]->rate/BufferFrames))+2;
    for(unsigned i=0;i<blocks;++i){p.fill(out.data());reference.insert(reference.end(),out.begin(),out.end());for(unsigned j=0;j<BufferFrames;++j){
        if(out[j*2]!=out[j*2+1])throw std::runtime_error("Centred mono SFX acquired a stereo phase difference");
        if(std::abs(int(out[j*2]))>=30000)throw std::runtime_error("Single SFX saturated the mixer");
        nonzero+=out[j*2]!=0;
    }}
    if(!nonzero||!p.voices.empty())throw std::runtime_error("SFX duration or signal check failed");
    p.fill(out.data());for(auto sample:out)if(sample)throw std::runtime_error("Finished SFX left a delayed tail");
    // Menu voices remain audible while gameplay voices and music are paused.
    {std::lock_guard lock(p.mutex);p.paused=true;p.voices.push_back({p.bank[slot],0,.24f,false});if(!p.queueMenu(0)||!p.queueMenu(1))throw std::runtime_error("Missing original menu selection sounds");if(p.voices.size()!=2)throw std::runtime_error("Menu sounds accumulated a delayed queue");}
    unsigned menuSignal=0;for(unsigned i=0;i<16;++i){p.fill(out.data());for(auto sample:out)menuSignal+=sample!=0;}
    {std::lock_guard lock(p.mutex);if(!menuSignal||p.voices.front().position!=0)throw std::runtime_error("Pause-menu sound was silent or unpaused gameplay audio");p.sfxVolume=0;p.queueMenu(0);}
    p.fill(out.data());for(auto sample:out)if(sample)throw std::runtime_error("Menu ignored SFX mute");
    {std::lock_guard lock(p.mutex);p.voices.clear();p.sfxVolume=1;p.paused=false;}
    // The live device requests variable frame counts, unlike the WAV writer.
    // Chunk boundaries must leave the resampling timeline bit-identical.
    {std::lock_guard lock(p.mutex);p.voices.push_back({p.bank[slot],0,.24f,false});}
    std::vector<short> variable(reference.size());constexpr unsigned sizes[]={1,479,16,1036,3,441};
    for(unsigned offset=0,block=0;offset<variable.size()/2;++block){auto count=std::min(sizes[block%6],unsigned(variable.size()/2)-offset);p.fill(variable.data()+offset*2,count);offset+=count;}
    if(variable!=reference)throw std::runtime_error("Device-sized audio chunks changed the PCM timeline");
    // Repeated flamethrower animation cues must keep one uninterrupted loop;
    // its release cue replaces that loop rather than leaving it running.
    {std::lock_guard lock(p.mutex);p.voices.clear();if(!p.queueWeapon(11,true))throw std::runtime_error("Missing flamethrower loop sample");}
    p.fill(out.data());
    {std::lock_guard lock(p.mutex);double position=p.voices.front().position;p.queueWeapon(11,true);if(p.voices.size()!=1||p.voices.front().position!=position)throw std::runtime_error("Flamethrower sound restarted or layered");if(!p.queueWeapon(10,false)||p.voices.size()!=1||p.voices.front().loop)throw std::runtime_error("Flamethrower release failed to stop sustained audio");p.voices.clear();}
    // A complete single pulse and two seconds of quiet must finish before
    // another tone starts, even when gameplay requests a ping every frame.
    // A clear sweep (21) must not block an incoming enemy contact ping (22).
    if(p.bank.size()<=22||!p.bank[22])throw std::runtime_error("Missing tracker ping sample");
    {std::lock_guard lock(p.mutex);p.voices.clear();p.trackerTailFrames=0;if(!p.queueTracker(21,1.f)||!p.queueTracker(22,1.f))throw std::runtime_error("Sweep click blocked immediate enemy contact ping");p.voices.clear();p.trackerTailFrames=0;if(!p.queueTracker(21,1.f))throw std::runtime_error("Missing tracker sweep sample");}
    for(unsigned i=0;i<8;++i)p.fill(out.data());
    {std::lock_guard lock(p.mutex);if(p.trackerTailFrames||!p.queueTracker(22,1.f))throw std::runtime_error("Finished sweep click locked out enemy contact ping");p.voices.clear();p.trackerTailFrames=0;}
    std::array<short,(OutputRate/30)*2> block{};std::vector<unsigned> starts;std::vector<short> trackerPcm;
    const unsigned clipFrames=unsigned(std::ceil(double(p.trackerSamples[1]->samples.size()/p.trackerSamples[1]->channels)*OutputRate/p.trackerSamples[1]->rate));
    for(unsigned tick=0;tick<300;++tick){
        {std::lock_guard lock(p.mutex);if(p.queueTracker(22,1.f))starts.push_back(tick*(OutputRate/30));if(p.queueTracker(21,1.f))throw std::runtime_error("Tracker tones overlapped");}
        p.fill(block.data(),OutputRate/30);trackerPcm.insert(trackerPcm.end(),block.begin(),block.end());
    }
    if(starts.size()<2||starts.front()!=0)throw std::runtime_error("Tracker did not start/repeat after completion");
    for(unsigned i=1;i<starts.size();++i)if(starts[i]-starts[i-1]<clipFrames+TrackerRestFrames)throw std::runtime_error("Tracker restarted before its pulse and quiet interval finished");
    if(!p.statusPath.empty()){
        std::ofstream wav(p.statusPath.parent_path()/"motion-tracker-paced.wav",std::ios::binary);auto word=[&](unsigned n){wav.put(char(n));wav.put(char(n>>8));};auto dword=[&](unsigned n){word(n);word(n>>16);};unsigned bytes=unsigned(trackerPcm.size()*2);
        wav.write("RIFF",4);dword(36+bytes);wav.write("WAVEfmt ",8);dword(16);word(1);word(2);dword(OutputRate);dword(OutputRate*4);word(4);word(16);wav.write("data",4);dword(bytes);for(auto sample:trackerPcm)word(static_cast<unsigned short>(sample));if(!wav)throw std::runtime_error("Cannot write paced tracker diagnostic");
        std::ofstream report(p.statusPath.parent_path()/"tracker_audio_checks.json");report<<"{\n  \"status\": \"passed\",\n  \"pulse_seconds\": "<<double(clipFrames)/OutputRate<<",\n  \"quiet_seconds_after_pulse\": 2,\n  \"test_seconds\": 10,\n  \"pulse_start_seconds\": [";for(unsigned i=0;i<starts.size();++i){if(i)report<<", ";report<<double(starts[i])/OutputRate;}report<<"],\n  \"tones_overlap\": false\n}\n";
    }
    {std::lock_guard lock(p.mutex);p.voices.clear();p.trackerTailFrames=0;}
    return nonzero*2;
}

void ExplorerAudio::writeDiagnostic(const std::filesystem::path& path){
    auto& p=*impl;
    {std::lock_guard lock(p.mutex);p.voices.clear();p.paused=false;if(p.bank.size()<=PistolFire||!p.bank[PistolFire])throw std::runtime_error("Missing diagnostic sound");p.voices.push_back({p.bank[PistolFire],0,.24f,false});}
    constexpr unsigned blocks=(OutputRate*2+BufferFrames-1)/BufferFrames,bytes=blocks*BufferFrames*4;
    std::ofstream file(path,std::ios::binary);auto word=[&](unsigned n){file.put(char(n));file.put(char(n>>8));};auto dword=[&](unsigned n){word(n);word(n>>16);};
    file.write("RIFF",4);dword(36+bytes);file.write("WAVEfmt ",8);dword(16);word(1);word(2);dword(OutputRate);dword(OutputRate*4);word(4);word(16);file.write("data",4);dword(bytes);
    std::array<short,BufferFrames*2> out{};for(unsigned i=0;i<blocks;++i){p.fill(out.data());for(short sample:out)word(static_cast<unsigned short>(sample));}
    if(!file)throw std::runtime_error("Cannot write diagnostic WAV");
}

void ExplorerAudio::volume(unsigned sfx,unsigned music){auto& p=*impl;std::lock_guard lock(p.mutex);p.sfxVolume=std::min(100u,sfx)/100.f;p.musicVolume=std::min(100u,music)/100.f;}

unsigned ExplorerAudio::musicTrack()const{return impl->trackNumber;}

void ExplorerAudio::soundtrack(const std::filesystem::path& root,const std::string& code,int track){std::lock_guard lock(impl->mutex);impl->loadTrack(root,code,track);}

void ExplorerAudio::playMenu(unsigned event){std::lock_guard lock(impl->mutex);if(impl->deviceReady)impl->queueMenu(event);}

void ExplorerAudio::ambient(unsigned source,unsigned slot,float gain,float pan,bool active,float x,float z,bool positional){auto& p=*impl;std::lock_guard lock(p.mutex);auto voice=std::find_if(p.voices.begin(),p.voices.end(),[&](const Voice& v){return v.ambient==source;});if(!active){if(voice!=p.voices.end())p.voices.erase(voice);return;}if(!p.deviceReady||slot>=p.bank.size()||!p.bank[slot])return;if(voice!=p.voices.end()){voice->gain=gain;voice->pan=pan;}else {unsigned count=0;auto quiet=p.voices.end();for(auto i=p.voices.begin();i!=p.voices.end();++i)if(i->ambient){++count;if(quiet==p.voices.end()||i->gain<quiet->gain)quiet=i;}if(count>=8){if(quiet->gain>=gain)return;*quiet={p.bank[slot],0,gain,true,pan,false,false,false,source};}else if(p.voices.size()<12)p.voices.push_back({p.bank[slot],0,gain,true,pan,false,false,false,source});}for(auto& v:p.voices)if(v.ambient==source){v.spatial=positional;if(positional)v.gain=.3f;v.x=x;v.z=z;}}

unsigned ExplorerAudio::validateEnvironmentMixer(){
 auto& p=*impl;bool paused,enabled;float sfx,musicVolume;{std::lock_guard lock(p.mutex);if(p.bank.size()<=PistolFire||!p.bank[PistolFire])throw std::runtime_error("Missing ambient mixer fixture");paused=p.paused;enabled=p.musicEnabled;sfx=p.sfxVolume;musicVolume=p.musicVolume;p.voices.clear();p.paused=false;p.sfxVolume=1;p.musicVolume=0;p.voices.push_back({p.bank[PistolFire],0,.24f,true,0,false,false,false,0x300000});}
 std::array<short,BufferFrames*2> block{};unsigned signal=0;for(unsigned i=0;i<16;++i){p.fill(block.data());for(auto value:block)signal+=value!=0;}if(!signal)throw std::runtime_error("Ambient sound used music volume instead of SFX volume");this->music(false);{std::lock_guard lock(p.mutex);if(p.voices.size()!=1||!p.voices.front().ambient)throw std::runtime_error("Music toggle removed environmental audio");p.sfxVolume=0;}p.fill(block.data());for(auto v:block)if(v)throw std::runtime_error("Ambient SFX mute failed");double position;{std::lock_guard lock(p.mutex);p.sfxVolume=1;p.paused=true;position=p.voices.front().position;}p.fill(block.data());{std::lock_guard lock(p.mutex);if(p.voices.front().position!=position)throw std::runtime_error("Paused environmental audio continued playing");p.voices.clear();p.paused=paused;p.musicEnabled=enabled;p.sfxVolume=sfx;p.musicVolume=musicVolume;}return signal;
}

unsigned ExplorerAudio::ps1WeaponSamples()const{return impl->ps1Weapons;}
unsigned ExplorerAudio::ps1PermanentSamples()const{return impl->ps1Permanent;}
unsigned ExplorerAudio::ps1LevelSamples()const{return impl->ps1Level;}

void ExplorerAudio::headphones(bool enabled){std::lock_guard lock(impl->mutex);if(impl->headphones==enabled)return;impl->headphones=enabled;for(auto& v:impl->voices){v.history.fill(0);v.filtered.fill(0);v.cursor=0;}}
void ExplorerAudio::listener(float x,float z,float yaw){std::lock_guard lock(impl->mutex);impl->listenerX=x;impl->listenerZ=z;impl->listenerYaw=yaw;}
void ExplorerAudio::playAt(unsigned slot,float x,float z){if(replayCue)replayCue({CueType::Positioned,slot,x,z});auto& p=*impl;std::lock_guard lock(p.mutex);if(!p.deviceReady||p.paused||slot>=p.bank.size()||!p.bank[slot]||p.voices.size()>=16||std::hypot(x-p.listenerX,z-p.listenerZ)>8192)return;auto sample=p.bank[slot];unsigned same=0;for(auto& v:p.voices)if(v.sound==sample&&!v.loop){if(v.position<sample->rate*.025&&v.spatial&&std::hypot(v.x-x,v.z-z)<64)return;++same;}if(same>=2)return;Voice voice{sample,0,.24f};voice.spatial=true;voice.x=x;voice.z=z;p.voices.push_back(std::move(voice));}

unsigned ExplorerAudio::validateSpatialMixer(){
 auto& p=*impl;auto signal=std::make_shared<Sound>();signal->rate=OutputRate;signal->samples.assign(2048,0);signal->samples[0]=16000;
 auto render=[&](float x,float z,float yaw,bool spatial=true,unsigned chunk=512){std::vector<short> out(1024);{std::lock_guard lock(p.mutex);p.voices.clear();p.paused=false;p.headphones=true;p.sfxVolume=1;p.listenerX=p.listenerZ=0;p.listenerYaw=yaw;Voice v{signal,0,.24f};v.spatial=spatial;v.x=x;v.z=z;p.voices.push_back(v);}for(unsigned offset=0;offset<512;){unsigned n=std::min(chunk,512-offset);p.fill(out.data()+offset*2,n);offset+=n;}return out;};
 auto energy=[](const auto& out,unsigned ear){double e=0;for(unsigned i=ear;i<out.size();i+=2)e+=double(out[i])*out[i];return e;};
 auto right=render(-1000,0,0),left=render(1000,0,0);if(energy(right,1)<=energy(right,0)||energy(left,0)<=energy(left,1))throw std::runtime_error("Headphone left/right direction failed");for(unsigned i=0;i<512;++i)if(right[i*2]!=left[i*2+1]||right[i*2+1]!=left[i*2])throw std::runtime_error("Headphone directional symmetry failed");
 auto rotated=render(-1000,0,3.14159265f);if(energy(rotated,0)<=energy(rotated,1))throw std::runtime_error("Headphone listener rotation failed");auto closeSignal=render(0,100,0),distantSignal=render(0,4000,0);if(energy(closeSignal,0)<=energy(distantSignal,0))throw std::runtime_error("Spatial distance attenuation failed");auto front=render(0,1000,0),rear=render(0,-1000,0);if(front==rear)throw std::runtime_error("Headphone front/rear tone failed");auto local=render(0,0,0,false);for(unsigned i=0;i<512;++i)if(local[i*2]!=local[i*2+1])throw std::runtime_error("Local audio became spatial");auto chunked=render(-1000,0,0,true,7);if(chunked!=right)throw std::runtime_error("Spatial audio changed across buffer boundaries");
 {std::lock_guard lock(p.mutex);p.paused=true;}std::array<short,512> quiet{};p.fill(quiet.data());for(auto v:quiet)if(v)throw std::runtime_error("Spatial audio ignored pause");{std::lock_guard lock(p.mutex);p.paused=false;p.sfxVolume=0;}p.fill(quiet.data());for(auto v:quiet)if(v)throw std::runtime_error("Spatial audio ignored mute");{std::lock_guard lock(p.mutex);p.voices.clear();p.sfxVolume=1;p.headphones=false;}return 8;
}


void ExplorerAudio::movie(const std::filesystem::path& path){auto raw=explorerRead(path);if(raw.empty()||raw.size()%4)throw std::runtime_error("Invalid movie PCM");auto sound=std::make_shared<Sound>();sound->rate=OutputRate;sound->channels=2;sound->samples.reserve(raw.size()/2);for(size_t i=0;i<raw.size();i+=2)sound->samples.push_back(float(static_cast<short>(unsigned(raw[i])|(unsigned(raw[i+1])<<8))));std::lock_guard lock(impl->mutex);std::erase_if(impl->voices,[](const Voice& v){return v.movie;});Voice voice{sound,0,.85f};voice.movie=true;impl->voices.push_back(std::move(voice));impl->moviePaused=false;}
void ExplorerAudio::stopMovie(){std::lock_guard lock(impl->mutex);std::erase_if(impl->voices,[](const Voice& v){return v.movie;});}
void ExplorerAudio::pauseMovie(bool value){std::lock_guard lock(impl->mutex);impl->moviePaused=value;}

unsigned ExplorerAudio::validateMovieMixer(const std::filesystem::path& path){
 auto& p=*impl;{std::lock_guard lock(p.mutex);p.voices.clear();p.paused=true;p.musicVolume=1;p.sfxVolume=0;}movie(path);std::array<short,BufferFrames*2> block{};unsigned signal=0;for(unsigned i=0;i<400;++i){p.fill(block.data());for(auto sample:block)signal+=sample!=0;}if(!signal)throw std::runtime_error("Movie audio was silent while gameplay was paused");pauseMovie(true);double position=p.voices.front().position;p.fill(block.data());for(auto sample:block)if(sample)throw std::runtime_error("Unfocused movie audio was not paused");if(p.voices.front().position!=position)throw std::runtime_error("Paused movie audio clock advanced");pauseMovie(false);{std::lock_guard lock(p.mutex);p.musicVolume=0;}p.fill(block.data());for(auto sample:block)if(sample)throw std::runtime_error("Movie audio ignored music volume");stopMovie();if(!p.voices.empty())throw std::runtime_error("Skipping movie left an audio voice");{std::lock_guard lock(p.mutex);p.musicVolume=p.sfxVolume=1;p.paused=false;}return signal;
}
