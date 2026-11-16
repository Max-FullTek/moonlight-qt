#pragma once

#include "workercommandrouting.h"

namespace WindowsWorkerLoop {
// The owner must finish initialization before running the loop. The loop
// publishes readiness and sends pipe commands to the owner thread in order.
int run(const std::function<void(WorkerCommandRouting::Input, std::string_view)>& handle);
}
