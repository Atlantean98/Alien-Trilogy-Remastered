#pragma once
#include "WorldMovement.h"

namespace trilogy::original {
struct PlayerViewState {
    Vec3s angles{}; // pitch, yaw, roll at 0xa0a50
    std::uint16_t phase{}; // 0xa0b54
    std::uint16_t eyeHeight{},bobEnabled{},pitchRate{},restPitch{},turnRate{};
    std::uint16_t weaponBobLow{},weaponBobHigh{};
    Vec3s position{}; // camera position at 0xa0a20
    std::uint16_t yaw{},roll{},pitch{},sway{};
};
struct PlayerState {
    WorldPlayerState world;
    PlayerViewState view;
    std::uint32_t flowFlags{};
    std::uint8_t networkMode{},deathLatch{};
    std::uint16_t respawnDelay{};
};
// Services carry original routine address and relevant EAX/EDX/EBX values.
// Input/weapon intent, audiovisual playback, UI and other gameplay effects
// remain external; movement, view pose and the three alternate states run here.
using PlayerStateEffect=std::function<void(PlayerState&,TaskScheduler&,TaskAddress,
    std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)>;
void buildPlayerView(PlayerState&,const LevelGrid&,std::span<const std::int16_t,4096>,
    TaskScheduler&,TaskAddress,const PlayerStateEffect&); // 0x3c94c
void playerDescendingTick(PlayerState&,LevelGrid&,std::span<const std::int16_t,4096>,
    TaskScheduler&,TaskAddress,const WorldService&,const PlayerStateEffect&); // 0x3e3c4
void playerAscendingTick(PlayerState&,LevelGrid&,std::span<const std::int16_t,4096>,
    TaskScheduler&,TaskAddress,const WorldService&,const PlayerStateEffect&); // 0x3e638
void playerDeathTick(PlayerState&,const LevelGrid&,std::span<const std::int16_t,4096>,
    TaskScheduler&,TaskAddress,const PlayerStateEffect&); // 0x3e714
// Dispatch supported task states with real movement, camera and transitions.
// Initialization (0x3fbc0) is still a separate dependency and is not handled.
void updatePlayerState(PlayerState&,LevelGrid&,std::span<const std::int16_t,4096>,
    TaskScheduler&,TaskAddress,const WorldService&,const PlayerStateEffect&);
}
