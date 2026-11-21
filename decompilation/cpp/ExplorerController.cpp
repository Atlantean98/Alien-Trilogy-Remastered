#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "ExplorerController.h"
#include <algorithm>
#include <cmath>
bool ExplorerController::initialize(){SDL_SetMainReady();SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4,"1");SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5,"1");initialized=SDL_InitSubSystem(SDL_INIT_GAMEPAD);return initialized;}
ExplorerController::~ExplorerController(){stopRumble();if(device)SDL_CloseGamepad(device);if(initialized)SDL_QuitSubSystem(SDL_INIT_GAMEPAD);}
std::array<float,2> ExplorerController::stick(short x,short y,float dz){float a=std::clamp(x/32767.f,-1.f,1.f),b=std::clamp(y/32767.f,-1.f,1.f),length=std::hypot(a,b);dz=std::clamp(dz,0.f,.9f);if(length<=dz)return {0,0};float scale=std::min(1.f,(length-dz)/(1-dz))/length;return {a*scale,b*scale};}
ExplorerController::State ExplorerController::poll(float dz,bool focused){State state;if(!initialized)return state;SDL_Event event;while(SDL_PollEvent(&event)){}if(device&&!SDL_GamepadConnected(device)){SDL_CloseGamepad(device);device=nullptr;previous=0;previousRaw=0;scanAt=0;}if(!device&&SDL_GetTicks()>=scanAt){int count=0;auto ids=SDL_GetGamepads(&count);for(int i=0;i<count&&!device;++i)if(!preferred||preferred==ids[i])device=SDL_OpenGamepad(ids[i]);SDL_free(ids);scanAt=SDL_GetTicks()+500;}if(!device)return state;state.connected=true;auto type=SDL_GetGamepadType(device);state.playstation=type==SDL_GAMEPAD_TYPE_PS3||type==SDL_GAMEPAD_TYPE_PS4||type==SDL_GAMEPAD_TYPE_PS5;if(!focused){previous=0xffffffffu;previousRaw=~0ull;stopRumble();return state;}
 auto left=stick(SDL_GetGamepadAxis(device,SDL_GAMEPAD_AXIS_LEFTX),SDL_GetGamepadAxis(device,SDL_GAMEPAD_AXIS_LEFTY),dz),right=stick(SDL_GetGamepadAxis(device,SDL_GAMEPAD_AXIS_RIGHTX),SDL_GetGamepadAxis(device,SDL_GAMEPAD_AXIS_RIGHTY),dz);state.moveX=left[0];state.moveY=-left[1];state.lookX=right[0];state.lookY=right[1];
 for(unsigned b=0;b<SDL_GAMEPAD_BUTTON_COUNT;++b)if(SDL_GetGamepadButton(device,SDL_GamepadButton(b)))state.raw|=1ull<<b;
 if(SDL_GetGamepadAxis(device,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)>6000)state.raw|=1ull<<32;if(SDL_GetGamepadAxis(device,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)>6000)state.raw|=1ull<<33;
 constexpr unsigned actions[]={Fire,Grenade,Use,Next,Previous,Sprint,Objectives,Map,Pause};for(unsigned i=0;i<bindings.size();++i)if(state.raw&(1ull<<bindings[i]))state.buttons|=actions[i];
 auto alias=[&](unsigned physical,unsigned bit){if(std::find(bindings.begin(),bindings.end(),physical)==bindings.end()&&(state.raw&(1ull<<physical)))state.buttons|=bit;};if(bindings[3]==DefaultBindings[3]){alias(SDL_GAMEPAD_BUTTON_NORTH,Next);alias(SDL_GAMEPAD_BUTTON_DPAD_RIGHT,Next);}if(bindings[4]==DefaultBindings[4])alias(SDL_GAMEPAD_BUTTON_DPAD_LEFT,Previous);if(bindings[6]==DefaultBindings[6]){alias(SDL_GAMEPAD_BUTTON_BACK,Objectives);alias(SDL_GAMEPAD_BUTTON_TOUCHPAD,Objectives);}
 auto button=[&](SDL_GamepadButton b,unsigned bit){if(SDL_GetGamepadButton(device,b))state.buttons|=bit;};button(SDL_GAMEPAD_BUTTON_SOUTH,Confirm);button(SDL_GAMEPAD_BUTTON_EAST,Cancel);
 // Menu navigation uses D-pad and left stick, while gameplay keeps these shortcuts.
 button(SDL_GAMEPAD_BUTTON_DPAD_UP,Up);button(SDL_GAMEPAD_BUTTON_DPAD_DOWN,Down);button(SDL_GAMEPAD_BUTTON_DPAD_LEFT,Left);button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT,Right);if(left[1]<-.5f)state.buttons|=Up;if(left[1]>.5f)state.buttons|=Down;if(left[0]<-.5f)state.buttons|=Left;if(left[0]>.5f)state.buttons|=Right;state.rawPressed=state.raw&~previousRaw;previousRaw=state.raw;state.pressed=state.buttons&~previous;previous=state.buttons;return state;}
std::string ExplorerController::name()const{auto value=device?SDL_GetGamepadName(device):nullptr;return value?value:"No controller connected";}

bool ExplorerController::rumble(float low,float high,unsigned duration){if(!device||!SDL_GamepadConnected(device))return false;bool ok=SDL_RumbleGamepad(device,Uint16(std::clamp(low,0.f,1.f)*65535),Uint16(std::clamp(high,0.f,1.f)*65535),duration);rumbling=ok&&(low>0||high>0);return ok;}
void ExplorerController::stopRumble(){if(device&&rumbling)SDL_RumbleGamepad(device,0,0,0);rumbling=false;}
