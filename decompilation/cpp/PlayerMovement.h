#pragma once
#include "TaskScheduler.h"
#include <cstdint>
#include <functional>

namespace trilogy::original {
struct PlayerMovementState {
    std::uint16_t speed{};        // 0xa0a64
    std::uint16_t maximumSpeed{}; // 0xa0b46
    std::uint16_t acceleration{}; // 0xa0b48
    std::uint16_t terrainGate{};  // 0xa0abc; disables terrain slowdown when nonzero
    std::uint8_t terrain{};       // current cell +0xa
    std::uint16_t pitch{};        // 0xa0a50
    std::uint16_t pitchRate{};    // 0xa0b38
    std::uint16_t restPitch{};    // 0xa0b42
};
void accelerateForward(PlayerMovementState&) noexcept; // 0x3cbf8
void accelerateOtherDirection(PlayerMovementState&) noexcept; // 0x3cc80
void recenterPitch(PlayerMovementState&) noexcept; // 0x3ce50

struct PlayerTickState {
    std::uint8_t flowFlags{};      // 0xa0c90 low byte
    std::uint32_t packedXY{}; // 0xa0a48: X in low word, Y in high word; each is signed 16-bit
    std::uint16_t movement0{};     // 0xa0a60
    std::uint16_t movement1{};     // 0xa0a62
    std::uint16_t movement2{};     // 0xa0a64
};
// Unrecovered services are explicit dependencies, never silently omitted.
using InvokePlayerService=std::function<void(std::uint32_t,PlayerTickState&,TaskScheduler&,TaskAddress)>;
void playerGroundedTick(PlayerTickState&,TaskScheduler&,TaskAddress,const InvokePlayerService&); // 0x3f908
}
