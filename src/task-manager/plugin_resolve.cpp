#include "mp/taskmgr/plugin_resolve.hpp"

#include <cstdlib>
#include <sstream>
#include <unistd.h>

namespace mp::taskmgr {

namespace {

void append_candidate(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    candidates.push_back(path);
}

bool is_executable_file(const std::filesystem::path& path) {
    return std::filesystem::exists(path) && !std::filesystem::is_directory(path);
}

std::filesystem::path find_on_path(const std::string& name) {
    const char* path_env = std::getenv("PATH");
    if (!path_env) {
        return {};
    }
    std::istringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) {
            continue;
        }
        const auto candidate = std::filesystem::path(dir) / name;
        if (is_executable_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

}  // namespace

ExecutableResolveResult resolve_plugin_executable(const std::string& executable,
                                                  const std::filesystem::path& manifest_path,
                                                  const ExecutableResolveContext& ctx) {
    ExecutableResolveResult result;
    const std::filesystem::path exe_path(executable);

    if (exe_path.is_absolute()) {
        append_candidate(result.candidates, exe_path);
        if (is_executable_file(exe_path)) {
            result.chosen = exe_path;
            return result;
        }
    }

    if (!manifest_path.empty()) {
        const auto relative_manifest = manifest_path.parent_path() / executable;
        append_candidate(result.candidates, relative_manifest);
        if (is_executable_file(relative_manifest)) {
            result.chosen = relative_manifest;
            return result;
        }
    }

    if (!ctx.plugin_bin_dir.empty()) {
        const auto relative_bin = ctx.plugin_bin_dir / executable;
        append_candidate(result.candidates, relative_bin);
        if (is_executable_file(relative_bin)) {
            result.chosen = relative_bin;
            return result;
        }
    }

    const auto on_path = find_on_path(executable);
    if (!on_path.empty()) {
        append_candidate(result.candidates, on_path);
        result.chosen = on_path;
        return result;
    }

    append_candidate(result.candidates, exe_path);
    result.chosen = exe_path;
    return result;
}

}  // namespace mp::taskmgr
