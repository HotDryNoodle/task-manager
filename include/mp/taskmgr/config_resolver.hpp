#pragma once

#include <filesystem>

#include "mp/taskmgr/task_manager.hpp"

namespace mp::taskmgr {

TaskManagerConfig resolve_config(TaskManagerConfig config);

}  // namespace mp::taskmgr
