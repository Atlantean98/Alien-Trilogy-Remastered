#pragma once
#include <functional>
#include <filesystem>
#include <memory>
#include <string>
// Host mixer for supplied unsigned-8-bit SFX and optional PCM WAV CD tracks.
class ExplorerAudio {
public:
    static constexpr unsigned PistolFire=13,PistolReload=12;
    static constexpr unsigned SupplyAlarm=101;
    ExplorerAudio();~ExplorerAudio();
    void level(const std::filesystem::path& root,const std::string& code,int track,bool survival=false,bool queensRevenge=false);
    void soundtrack(const std::filesystem::path&,const std::string&,int track);
    bool start();void pause(bool);void volume(unsigned sfx,unsigned music);
    void ambient(unsigned source,unsigned slot,float gain,float pan,bool active,float x=0,float z=0,bool positional=false);
    void movie(const std::filesystem::path&);void stopMovie();void pauseMovie(bool);
    void headphones(bool);void listener(float x,float z,float yaw);
    enum class CueType : unsigned {Centered,Positioned,Weapon};
    struct ReplayCue {CueType type;unsigned slot;float x,z;};
    std::function<void(const ReplayCue&)> replayCue;
    void playAt(unsigned slot,float x,float z);
    unsigned validateSpatialMixer();unsigned validateMovieMixer(const std::filesystem::path&);
    void playMenu(unsigned event);
    void playWeapon(unsigned slot,bool loop=false);
    void play(unsigned slot,float gain=1,float pan=0);void music(bool);
    unsigned ps1WeaponSamples()const;unsigned ps1PermanentSamples()const;unsigned ps1LevelSamples()const;unsigned musicTrack()const;unsigned slots()const;unsigned missingSamples()const;bool hasSample(unsigned slot)const;unsigned sampleLength(unsigned slot)const;bool musicAvailable()const;bool ready()const;
    void writeDiagnostic(const std::filesystem::path&);
    unsigned validateEnvironmentMixer();
    unsigned validateMixer(unsigned slot=PistolFire); // Headless signal check using a loaded sample.
private:struct Impl;std::unique_ptr<Impl> impl;
};
