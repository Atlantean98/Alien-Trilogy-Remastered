#pragma once
#include "PlayerMovement.h"
#include <array>
#include <cstdint>
#include <span>

namespace trilogy::original {
using Vec3s=std::array<std::int16_t,3>;
using Matrix3s=std::array<std::int16_t,9>;
struct LevelCell {std::array<std::uint8_t,16> bytes{};};
static_assert(sizeof(LevelCell)==16);
struct LevelGrid {
    std::uint16_t width{},height{};
    std::span<LevelCell> cells;
    LevelCell& cell(std::uint32_t x,std::uint32_t z) const {return cells[(z>>9)*width+(x>>9)];}
    std::uint32_t index(std::uint32_t x,std::uint32_t z) const {return (z>>9)*width+(x>>9);}
};
// Original trig samples are supplied as signed Q12 values, 4096 entries.
Matrix3s buildRotation(const Vec3s& angles,std::span<const std::int16_t,4096> sine); // 0x266bc
std::array<std::int32_t,3> transformVector(const Matrix3s&,const Vec3s&); // 0x47578
Vec3s rotateVector(const Vec3s&,const Vec3s&,std::span<const std::int16_t,4096>); // 0x263c0
Vec3s addRotatedVector(const Vec3s&,const Vec3s&,const Vec3s&,std::span<const std::int16_t,4096>); // 0x26400
std::int32_t floorHeight(const LevelGrid&,std::uint32_t x,std::uint32_t z); // 0x277c8
std::int32_t ceilingHeight(const LevelGrid&,std::uint32_t x,std::uint32_t z); // 0x27950
std::uint32_t clipDiagonalX(const LevelGrid&,std::uint32_t x,std::uint32_t z); // 0x279ac
std::uint32_t clipDiagonalZ(const LevelGrid&,std::uint32_t x,std::uint32_t z); // 0x27a7c
std::uint32_t wallFallbackX(const LevelGrid&,std::uint32_t x,std::uint32_t z,std::uint32_t tested,bool negative); // 0x3d144
std::uint32_t wallFallbackZ(const LevelGrid&,std::uint32_t x,std::uint32_t z,std::uint32_t tested,bool negative); // 0x3d52c
std::uint32_t collideX(const LevelGrid&,std::uint32_t x,std::uint32_t z,Vec3s& displacement,std::uint8_t flags); // 0x3d19c
std::uint32_t collideZ(const LevelGrid&,std::uint32_t x,std::uint32_t z,Vec3s& displacement,std::uint8_t flags); // 0x3d584

struct WorldPlayerState {
    Vec3s position{}; // 0xa0a48: X, Y, Z signed words; NOT fixed-point XY.
    Vec3s moveAngles{}; // 0xa0a58
    Vec3s localMovement{}; // 0xa0a60
    std::uint16_t originX{},originZ{};
    std::uint32_t currentCell{}; // index in supplied grid, not a host pointer
    std::uint8_t movementFlags{},collisionFlags{},terrainFlags{};
    std::uint16_t impulse{},impulseYaw{},terrainGate{},damageGate{},damageCooldown{},health{};
    std::uint16_t region{};
    std::uint32_t pressedInput{};
    std::uint16_t useMask{};
    Matrix3s scratchRotation{}; // original temporary matrix at 0xa0a28
};
// Remaining services are visibility/region updates, damage and map interaction.
using WorldService=std::function<void(std::uint32_t,std::uint32_t,std::uint32_t,WorldPlayerState&,LevelGrid&)>;
void movePlayer(WorldPlayerState&,LevelGrid&,std::span<const std::int16_t,4096>,const WorldService&); // 0x3d930
// Connects the recovered normal player callback to real movement/floor/collision.
// Other original player services (input/weapon intent, view, alternate state) remain supplied.
void playerTickWithWorld(PlayerTickState&,WorldPlayerState&,LevelGrid&,TaskScheduler&,TaskAddress,
    std::span<const std::int16_t,4096>,const WorldService&,const InvokePlayerService&);
}
