#pragma once
#include "TaskScheduler.h"
#include "PlayerControl.h"
namespace trilogy::original {
// Callback handles remain data; this bridge never executes mapped addresses.
PlayerControlService withMappedTaskScheduler(const PlayerControlService&);
}
