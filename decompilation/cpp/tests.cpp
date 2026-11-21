#include "OriginalUtilities.h"
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <limits>

static void check(bool condition) {
    if (!condition) { std::fputs("Recovered utility check failed\n", stderr); std::exit(1); }
}

int main() {
    using namespace trilogy::original;
    const std::array<std::uint8_t, 5> bytes{0x55, 0x12, 0x34, 0x80, 0xff};
    check(readBigEndian32(bytes.data() + 1) == 0x123480ffu);
    const std::array<std::uint8_t, 4> high{0xff, 0xff, 0xff, 0xff};
    check(readBigEndian32(high.data()) == 0xffffffffu);
    auto parse = [](const char* text) {
        return parseDecimalUnchecked(reinterpret_cast<const std::uint8_t*>(text));
    };
    check(parse("") == 0);
    check(parse("000123") == 123);
    check(parse("4294967295") == 0xffffffffu);
    check(parse("4294967296") == 0);
    check(parse("/") == 0xffffffffu);
    check(parse(":") == 10);
    const std::uint8_t highText[]{0xff, 0};
    check(parseDecimalUnchecked(highText) == 207);
    // Round-trip many independent standard-library decimal conversions.
    std::uint32_t state = 123;
    for (unsigned i=0; i<10000; ++i) {
        state = state * 1664525u + 1013904223u;
        char buffer[16]{};
        auto result = std::to_chars(buffer, buffer + 15, state);
        *result.ptr = 0;
        check(parse(buffer) == state);
    }
    std::puts("Recovered utility checks passed");
}
