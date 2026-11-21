#pragma once
#include <cstdint>
#include <array>
#include <string>
struct SDL_Gamepad;
class ExplorerController {
public:
 enum Button:unsigned {Confirm=1,Cancel=2,Use=4,Next=8,Previous=16,Sprint=32,Pause=64,Objectives=128,Grenade=256,Fire=512,Up=1024,Down=2048,Left=4096,Right=8192,Map=16384};
 inline static constexpr std::array<unsigned,9> DefaultBindings{33,32,2,10,9,7,11,12,6};
 struct State {bool connected=false,playstation=false;float moveX=0,moveY=0,lookX=0,lookY=0;unsigned buttons=0,pressed=0;std::uint64_t raw=0,rawPressed=0;};
 void select(unsigned instance){preferred=instance;}bool initialize();~ExplorerController();State poll(float deadzone=.18f,bool focused=true);std::string name()const;void configure(const std::array<unsigned,9>& values){bindings=values;}bool rumble(float low,float high,unsigned duration);void stopRumble();
 static std::array<float,2> stick(short x,short y,float deadzone);
private:SDL_Gamepad* device=nullptr;bool initialized=false,rumbling=false;unsigned previous=0,preferred=0;unsigned long long scanAt=0;std::uint64_t previousRaw=0;std::array<unsigned,9> bindings=DefaultBindings;
};
