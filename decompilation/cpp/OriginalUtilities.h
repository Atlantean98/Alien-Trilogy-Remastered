#pragma once
#include <cstdint>

namespace trilogy::original {
// Recovered from TRILOGY.EXE SHA256 588606092836c4a0b9e31bd89a885963cc6edc908e60789378ba23e3f109e666.
// Address 0x18590: input pointer EAX, result EAX; EDX is preserved, not a second result.
// Requires four readable bytes. No alignment requirement.
std::uint32_t readBigEndian32(const std::uint8_t* bytes) noexcept;

// Address 0x185c0: input pointer EAX, result EAX; EDX is preserved.
// Original semantics: no sign, whitespace or digit validation; arithmetic wraps.
// Requires a readable NUL-terminated byte string.
std::uint32_t parseDecimalUnchecked(const std::uint8_t* text) noexcept;
}
