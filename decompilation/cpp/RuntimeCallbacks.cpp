#include "RuntimeCallbacks.h"

namespace trilogy::original {
void runInitializers(std::span<RuntimeCallback> table, std::uint8_t maximumPriority,
                     const InvokeRuntimeCallback& invoke) {
    for (;;) {
        auto selected = table.end();
        auto priority = maximumPriority;
        for (auto it = table.begin(); it != table.end(); ++it) {
            if (it->state != 2 && it->priority <= priority) {
                selected = it;
                priority = it->priority;
            }
        }
        if (selected == table.end()) return;
        if (selected->callbackAddress != 0) invoke(selected->callbackAddress);
        selected->state = 2;
    }
}

void runFinalizers(std::span<RuntimeCallback> table, std::uint8_t minimumPriority,
                   std::uint8_t maximumPriority, const InvokeRuntimeCallback& invoke) {
    for (;;) {
        auto selected = table.end();
        auto priority = minimumPriority;
        for (auto it = table.begin(); it != table.end(); ++it) {
            if (it->state != 2 && it->priority >= priority) {
                selected = it;
                priority = it->priority;
            }
        }
        if (selected == table.end()) return;
        if (priority <= maximumPriority && selected->callbackAddress != 0)
            invoke(selected->callbackAddress);
        selected->state = 2;
    }
}
}
