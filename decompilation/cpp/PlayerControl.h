#pragma once
#include "PlayerStates.h"
#include <bit>
#include <cstring>
#include <span>
#include <stdexcept>

namespace trilogy::original {
// Transitional backing store preserves overlapping globals/inventory exactly.
// Addresses identify data, never executable host pointers. Supply an image-sized
// buffer covering all referenced state, grid, region and task data (>= 4 MiB).
// This is compiled C++ logic, not an instruction interpreter.
// Accessor performance: w/d/s are on every hot path (enemy AI, movement,
// collision), so a fully in-range access does one bounds check and one direct
// little-endian load/store. Anything that would touch past the end takes the
// original byte-by-byte path, so out-of-range behaviour is unchanged: the same
// exception, and a straddling write still stores its in-range bytes first.
struct PlayerControlMemory {
    static_assert(std::endian::native==std::endian::little,"Fast accessors assume a little-endian host");
    std::span<std::uint8_t> bytes;
    std::uint8_t b(std::uint32_t a) const {if(a>=bytes.size())throw std::out_of_range("Mapped game data address outside buffer");return bytes[a];}
    std::uint16_t w(std::uint32_t a) const {if(a<bytes.size()&&bytes.size()-a>=2){std::uint16_t v;std::memcpy(&v,bytes.data()+a,2);return v;}return b(a)|(std::uint16_t(b(a+1))<<8);}
    std::uint32_t d(std::uint32_t a) const {if(a<bytes.size()&&bytes.size()-a>=4){std::uint32_t v;std::memcpy(&v,bytes.data()+a,4);return v;}return w(a)|(std::uint32_t(w(a+2))<<16);}
    std::int32_t s(std::uint32_t a) const {return std::bit_cast<std::int16_t>(w(a));}
    void b(std::uint32_t a,std::uint32_t n){if(a>=bytes.size())throw std::out_of_range("Mapped game data address outside buffer");bytes[a]=static_cast<std::uint8_t>(n);}
    void w(std::uint32_t a,std::uint32_t n){if(a<bytes.size()&&bytes.size()-a>=2){const auto v=static_cast<std::uint16_t>(n);std::memcpy(bytes.data()+a,&v,2);return;}b(a,n);b(a+1,n>>8);}
    void d(std::uint32_t a,std::uint32_t n){if(a<bytes.size()&&bytes.size()-a>=4){std::memcpy(bytes.data()+a,&n,4);return;}w(a,n);w(a+2,n>>16);}
};
struct PlayerControlResult {std::uint32_t eax{},ecx{};};
// Relevant arguments only; unspecified incoming register values are not part
// of this API. Providers implement animation/projectiles, selection, UI, etc.
using PlayerControlService=std::function<PlayerControlResult(PlayerControlMemory&,std::uint32_t,
    std::uint32_t,std::uint32_t,std::uint32_t)>;
void playerInputAndWeaponIntent(PlayerControlMemory,const PlayerControlService&); // 0x3e894
void firePrimary(PlayerControlMemory,const PlayerControlService&); // 0x3cef0
void fireSecondary(PlayerControlMemory,const PlayerControlService&); // 0x3d0d4
PlayerControlResult resetPlayerInventory(PlayerControlMemory,const PlayerControlService&); // 0x40098
void initializePlayer(PlayerControlMemory,std::uint32_t task,const PlayerControlService&); // 0x3fbc0
// Bridges the mapped control/inventory data to the typed verified movement,
// camera and lifecycle core. Handles initialization and all recovered states.
void updateControlledPlayer(PlayerControlMemory,std::uint32_t task,const PlayerControlService&);
}
