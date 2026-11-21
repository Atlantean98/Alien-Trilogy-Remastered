#pragma once
#include <cstdint>

namespace trilogy::original {
struct InputRepeatState {
    std::uint32_t output{};       // 0x000a0bb8
    std::uint32_t previous{};     // 0x000a0c7c
    std::uint32_t pressed{};      // 0x000a0c94
    std::uint32_t held{};         // 0x000a0c98
    std::uint16_t repeatTicks{};  // 0x000a0cb6: signed comparisons, wrapping storage
    std::uint16_t stableTicks{};  // 0x000a0cb8: signed comparison at saturation
};
// 0x10d00. sample is the result of the original pollInput(0) call.
// Output is selected from OLD state before the poll result is stored.
// The external device poll itself is not reconstructed by this function.
void updateInputRepeat(InputRepeatState& state, std::uint32_t sample) noexcept;
// 0x10db4: leaves previous, pressed and held untouched.
void resetInputRepeat(InputRepeatState& state) noexcept;
}
