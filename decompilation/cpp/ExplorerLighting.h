#pragma once
// Host presentation sequence for the level 3 lighting family (L113, L114, L115).
#include "ExplorerAssets.h"
#include <stdexcept>
class ExplorerLighting {
public:
    void load(const std::filesystem::path&,bool enabled);
    int tick(); // Original SFX slot to cue, or -1.
    std::array<unsigned char,3> color(unsigned sourceFace,unsigned fallbackLight)const;
    bool active=false;unsigned remaining{},elapsed{},phase{},changes{};
    unsigned seconds()const{return (remaining+29)/30;}
    unsigned matchedFaces(unsigned index)const{return matched[index];}
private:
    struct Bank {std::vector<std::array<unsigned char,28>> lights;std::vector<unsigned> faces;};
    std::array<Bank,3> banks;std::array<unsigned,3> matched{};
    bool firstCue=false;
};

// PS1 MAP0 lighting with original corner colours and switch/blink state.
class ExplorerPs1Lighting {
public:
 void load(const std::filesystem::path& pc,const std::filesystem::path& root,int bank=-1);
 void tick(unsigned random);void toggle(unsigned delta,unsigned index);
 std::array<unsigned char,3> color(unsigned face,unsigned corner)const;
 std::vector<std::array<unsigned char,28>> networkRecords()const{return records;}
 void networkRecords(const std::vector<std::array<unsigned char,28>>& state){if(state.size()!=records.size())throw std::runtime_error("Co-op lighting catalogue differs");records=state;}
 bool active=false;unsigned matched{},total{};
private:
 std::vector<std::array<unsigned char,28>> records,initial;std::vector<unsigned> faces;
};
