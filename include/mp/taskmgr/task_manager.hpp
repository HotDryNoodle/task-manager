#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "mp/taskmgr/plugin_registry.hpp"

namespace mp::taskmgr {

struct TaskManagerConfig {
    std::optional<std::filesystem::path> plugin_index;
    std::filesystem::path plugin_dir = "configs/plugins";
    std::vector<std::filesystem::path> plugin_dirs;
    std::filesystem::path work_root = "/tmp/mission-planer/tasks";
    std::filesystem::path plugin_bin_dir = ".";
    std::string router_endpoint = "tcp://*:5555";
    std::string pub_endpoint = "tcp://*:5556";
    int max_parallel = 2;
};

class TaskManager {
public:
    explicit TaskManager(TaskManagerConfig config);
    ~TaskManager();

    void start();
    void stop();
    nlohmann::json handle_message(const nlohmann::json& envelope);

private:
    void worker_loop();
    void execute_task(const std::string& task_id);
    void publish_event(const nlohmann::json& event);
    void warn_missing_executables() const;
    std::filesystem::path plugin_executable(const PluginInfo& plugin) const;

    TaskManagerConfig config_;
    PluginRegistry registry_;
    TaskStore store_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace mp::taskmgr
