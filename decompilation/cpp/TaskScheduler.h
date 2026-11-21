#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace trilogy::original {
using TaskAddress = std::uint32_t;
struct TaskNode32 {
    std::int32_t priority{};
    std::uint32_t callbackAddress{};
    std::array<std::uint8_t,16> payload{};
    TaskAddress next{};
    TaskAddress previous{};
};
static_assert(sizeof(TaskNode32)==32 && offsetof(TaskNode32,next)==24 && offsetof(TaskNode32,previous)==28);

// Address handles preserve original 32-bit links without truncating host pointers.
// The host resolves callbackAddress through invoke; no original address is executed.
// Callers must supply valid allocated task handles, as in the original routines.
class TaskScheduler {
public:
    explicit TaskScheduler(TaskAddress base) : poolBase(base) {}
    std::array<TaskNode32,256> nodes{};
    TaskAddress poolBase;
    TaskAddress head{}, freeHead{}, cursor{};
    std::uint8_t remainingTicks{1}; // 0x95f9c; callbacks can change this byte.
    using Invoke = std::function<void(TaskScheduler&,TaskAddress)>;
    TaskNode32& node(TaskAddress address) {return nodes[(address-poolBase)/32u];}
    const TaskNode32& node(TaskAddress address) const {return nodes[(address-poolBase)/32u];}
    void reset() noexcept;                         // 0x12258
    void insert(TaskAddress task) noexcept;         // 0x122c0
    TaskAddress create(std::int32_t priority, std::uint32_t callback) noexcept; // 0x12364
    void remove(TaskAddress task) noexcept;         // 0x12394
    void advance(TaskAddress task) noexcept;        // 0x13f34
    // Callback must explicitly set cursor (e.g. advance/remove), as in original.
    void dispatch(const Invoke& invoke);            // 0x123e0
    void dispatchCompensated(std::uint32_t timing, bool compensation, const Invoke& invoke); // 0x12400
};
}
