#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mp::taskmgr {

struct ExecutableResolveContext {
    std::filesystem::path plugin_bin_dir;
};

struct ExecutableResolveResult {
    std::filesystem::path chosen;
    std::vector<std::filesystem::path> candidates;
};

ExecutableResolveResult resolve_plugin_executable(const std::string& executable,
                                                  const std::filesystem::path& manifest_path,
                                                  const ExecutableResolveContext& ctx);

}  // namespace mp::taskmgr
