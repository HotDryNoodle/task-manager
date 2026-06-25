#include "mp/taskmgr/config_resolver.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

namespace mp::taskmgr {

namespace {

std::filesystem::path env_path(const char* name) {
    if (const char* value = std::getenv(name)) {
        return value;
    }
    return {};
}

void warn_deprecated(const char* old_name, const char* new_name) {
    std::cerr << "Warning: " << old_name << " is deprecated; use " << new_name << '\n';
}

std::vector<std::filesystem::path> split_paths(const std::string& text) {
    std::vector<std::filesystem::path> paths;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, ':')) {
        if (!part.empty()) {
            paths.push_back(part);
        }
    }
    return paths;
}

std::filesystem::path find_project_root() {
    auto cursor = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::exists(cursor / "meson.build") &&
            std::filesystem::exists(cursor / "configs" / "plugins")) {
            return cursor;
        }
        if (!cursor.has_parent_path() || cursor == cursor.parent_path()) {
            break;
        }
        cursor = cursor.parent_path();
    }
    return {};
}

}  // namespace

TaskManagerConfig resolve_config(TaskManagerConfig config) {
    if (auto index = env_path("SATELLITE_PLUGIN_INDEX"); !index.empty()) {
        config.plugin_index = index;
    }

    if (auto dirs = env_path("SATELLITE_PLUGIN_DIR"); !dirs.empty()) {
        config.plugin_dirs = split_paths(dirs.string());
    } else if (auto legacy_root = env_path("MISSION_PLANER_ROOT"); !legacy_root.empty()) {
        warn_deprecated("MISSION_PLANER_ROOT", "SATELLITE_PLUGIN_DIR");
        config.plugin_dirs = {legacy_root / "configs" / "plugins"};
    }

    if (auto work = env_path("SATELLITE_TASK_WORK_ROOT"); !work.empty()) {
        config.work_root = work;
    }

    if (auto bin = env_path("SATELLITE_PLUGIN_BIN"); !bin.empty()) {
        config.plugin_bin_dir = bin;
    } else if (auto legacy_bin = env_path("MISSION_PLANER_BIN"); !legacy_bin.empty()) {
        warn_deprecated("MISSION_PLANER_BIN", "SATELLITE_PLUGIN_BIN");
        config.plugin_bin_dir = legacy_bin;
    }

    const auto root = find_project_root();
    if (!root.empty()) {
        if (config.plugin_dirs.empty() && config.plugin_dir.is_relative()) {
            config.plugin_dir = root / config.plugin_dir;
        }
        if (config.work_root.is_relative()) {
            config.work_root = root / config.work_root;
        }
        if (config.plugin_bin_dir == std::filesystem::path(".") &&
            std::filesystem::exists(root / "build")) {
            config.plugin_bin_dir = root / "build";
        }
    }

    if (!config.plugin_index) {
        if (config.plugin_dirs.empty() && config.plugin_dir.is_relative()) {
            config.plugin_dir = std::filesystem::absolute(config.plugin_dir);
        }
    }

    config.work_root = std::filesystem::absolute(config.work_root);
    config.plugin_bin_dir = std::filesystem::absolute(config.plugin_bin_dir);
    for (auto& dir : config.plugin_dirs) {
        dir = std::filesystem::absolute(dir);
    }
    if (config.plugin_index) {
        config.plugin_index = std::filesystem::absolute(*config.plugin_index);
    }
    return config;
}

}  // namespace mp::taskmgr
