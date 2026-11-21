#include "OriginalUtilities.h"

namespace trilogy::original {
std::uint32_t readBigEndian32(const std::uint8_t* bytes) noexcept {
    return (std::uint32_t{bytes[0]} << 24) |
           (std::uint32_t{bytes[1]} << 16) |
           (std::uint32_t{bytes[2]} << 8) |
           std::uint32_t{bytes[3]};
}

std::uint32_t parseDecimalUnchecked(const std::uint8_t* text) noexcept {
    std::uint32_t value = 0;
    while (*text != 0) {
        value = value * 10u + (std::uint32_t{*text} - 0x30u);
        ++text;
    }
    return value;
}
}
