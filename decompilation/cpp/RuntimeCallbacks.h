#pragma once
#include <cstdint>
#include <functional>
#include <span>

namespace trilogy::original {
// Host representation. Original records are six bytes: state:u8,
// priority:u8, callbackAddress:u32. Host padding must not be serialized.
struct RuntimeCallback {
    std::uint8_t state{};
    std::uint8_t priority{};
    std::uint32_t callbackAddress{};
};
using InvokeRuntimeCallback = std::function<void(std::uint32_t)>;
// 0x6bd56, original table [0xa097a, 0xa09a4).
void runInitializers(std::span<RuntimeCallback> table, std::uint8_t maximumPriority,
                     const InvokeRuntimeCallback& invoke);
// 0x6bda1, original table [0xa09a4, 0xa09b0).
// Out-of-range entries selected above maximumPriority are still marked done.
void runFinalizers(std::span<RuntimeCallback> table, std::uint8_t minimumPriority,
                   std::uint8_t maximumPriority, const InvokeRuntimeCallback& invoke);
}
