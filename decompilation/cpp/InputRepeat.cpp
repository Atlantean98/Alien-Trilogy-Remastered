#include "InputRepeat.h"
#include <bit>

namespace trilogy::original {
void updateInputRepeat(InputRepeatState& s, std::uint32_t sample) noexcept {
    if (s.stableTicks == 0) {
        s.output = s.pressed;
    } else if (s.stableTicks == 20 && std::bit_cast<std::int16_t>(s.repeatTicks) > 10) {
        s.output = s.held;
        s.repeatTicks = 0;
    } else {
        s.output = 0;
    }
    s.previous = s.held;
    s.held = sample;
    s.pressed = ~s.previous & s.held;
    if (s.held == s.previous) {
        s.stableTicks = static_cast<std::uint16_t>(s.stableTicks + 1u);
        if (std::bit_cast<std::int16_t>(s.stableTicks) > 20) s.stableTicks = 20;
        s.repeatTicks = static_cast<std::uint16_t>(s.repeatTicks + 1u);
    } else {
        s.stableTicks = 0;
        s.repeatTicks = 0;
    }
}

void resetInputRepeat(InputRepeatState& s) noexcept {
    s.output = 0;
    s.repeatTicks = 0;
    s.stableTicks = 0;
}
}
