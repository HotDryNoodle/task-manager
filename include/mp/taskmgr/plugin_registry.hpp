#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace mp::taskmgr {

struct PluginInfo {
    std::string tool_name;
    std::string executable;
    std::filesystem::path manifest_path;
    nlohmann::json manifest;
    std::optional<std::filesystem::path> executable_resolved;
};

struct TaskRecord {
    std::string task_id;
    std::string request_id;
    std::string tool_name;
    std::string status;
    std::optional<std::string> idempotency_key;
    int exit_code = 0;
    std::filesystem::path work_dir;
    nlohmann::json request;
    nlohmann::json result;
    std::string started_at_utc;
    std::string finished_at_utc;
};

class PluginRegistry {
public:
    void clear();
    void load_index(const std::filesystem::path& index_path);
    void scan_directory(const std::filesystem::path& plugin_dir);
    void scan_directories(const std::vector<std::filesystem::path>& plugin_dirs);
    std::optional<PluginInfo> find_by_tool(const std::string& tool_name) const;
    std::vector<PluginInfo> list() const;

private:
    void add_plugin(PluginInfo info);

    std::vector<PluginInfo> plugins_;
};

struct ProcessResult {
    int exit_code = 0;
    std::string stdout_text;
    std::string stderr_text;
};

struct RunPluginOptions {
    std::optional<int> timeout_sec;
};

ProcessResult run_plugin_command(const std::filesystem::path& executable,
                                 const std::vector<std::string>& args,
                                 const std::optional<std::string>& stdin_payload = std::nullopt,
                                 const RunPluginOptions& options = {});

class TaskStore {
public:
    TaskRecord create_task(const nlohmann::json& envelope);
    std::optional<TaskRecord> get(const std::string& task_id) const;
    std::optional<TaskRecord> find_by_idempotency_key(const std::string& key) const;
    void update(const TaskRecord& task);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TaskRecord> tasks_;
    std::unordered_map<std::string, std::string> idempotency_index_;
};

}  // namespace mp::taskmgr
