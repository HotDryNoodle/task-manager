#include "mp/taskmgr/task_manager.hpp"

#include <iostream>
#include <optional>
#include <queue>

#include "mp/common/exit_codes.hpp"
#include "mp/common/json_io.hpp"
#include "mp/taskmgr/config_resolver.hpp"
#include "mp/taskmgr/plugin_resolve.hpp"

namespace mp::taskmgr {

TaskManager::TaskManager(TaskManagerConfig config) : config_(resolve_config(std::move(config))) {
    if (config_.plugin_index) {
        registry_.load_index(*config_.plugin_index);
    } else if (!config_.plugin_dirs.empty()) {
        registry_.scan_directories(config_.plugin_dirs);
    } else {
        registry_.clear();
        registry_.scan_directory(config_.plugin_dir);
    }
    std::filesystem::create_directories(config_.work_root);
    warn_missing_executables();
}

TaskManager::~TaskManager() {
    stop();
}

std::filesystem::path TaskManager::plugin_executable(const PluginInfo& plugin) const {
    if (plugin.executable_resolved && std::filesystem::exists(*plugin.executable_resolved)) {
        return *plugin.executable_resolved;
    }
    const ExecutableResolveContext ctx{config_.plugin_bin_dir};
    return resolve_plugin_executable(plugin.executable, plugin.manifest_path, ctx).chosen;
}

void TaskManager::warn_missing_executables() const {
    for (const auto& plugin : registry_.list()) {
        const ExecutableResolveContext ctx{config_.plugin_bin_dir};
        const auto resolved = resolve_plugin_executable(plugin.executable, plugin.manifest_path, ctx);
        if (!std::filesystem::exists(resolved.chosen)) {
            std::cerr << "Warning: plugin executable not found for tool '" << plugin.tool_name << "'\n";
            for (const auto& candidate : resolved.candidates) {
                std::cerr << "  tried: " << candidate.string() << '\n';
            }
        }
    }
}

namespace {

bool plugin_supports_dry_run(const nlohmann::json& manifest) {
    return manifest.contains("capabilities") && manifest.at("capabilities").value("dry_run", false);
}

bool request_wants_dry_run(const nlohmann::json& request) {
    return request.value("dry_run", false);
}

std::optional<int> plugin_timeout_sec(const nlohmann::json& manifest) {
    if (!manifest.contains("resource_limits")) {
        return std::nullopt;
    }
    const auto& limits = manifest.at("resource_limits");
    if (!limits.contains("timeout_sec")) {
        return std::nullopt;
    }
    const int timeout = limits.at("timeout_sec").get<int>();
    return timeout > 0 ? std::optional<int>{timeout} : std::nullopt;
}

}  // namespace

void TaskManager::publish_event(const nlohmann::json& event) {
    (void)event;
    // ZeroMQ PUB integration is provided by task-manager-zmq when libzmq is available.
}

nlohmann::json TaskManager::handle_message(const nlohmann::json& envelope) {
    const auto message_type = envelope.at("message_type").get<std::string>();

    if (message_type == "task.list_tools") {
        nlohmann::json tools = nlohmann::json::array();
        for (const auto& plugin : registry_.list()) {
            tools.push_back(plugin.manifest);
        }
        return {
            {"schema_version", "1.0"},
            {"message_type", "task.list_tools.result"},
            {"tools", tools},
        };
    }

    if (message_type == "task.submit") {
        if (envelope.contains("idempotency_key")) {
            const auto key = envelope.at("idempotency_key").get<std::string>();
            if (const auto existing = store_.find_by_idempotency_key(key)) {
                return {
                    {"schema_version", "1.0"},
                    {"message_type", "task.submit.result"},
                    {"task_id", existing->task_id},
                    {"status", existing->status},
                    {"idempotent_hit", true},
                };
            }
        }
        auto task = store_.create_task(envelope);
        task.work_dir = config_.work_root / task.task_id;
        std::filesystem::create_directories(task.work_dir);
        write_json_file(task.work_dir / "envelope.json", envelope, true);
        write_json_file(task.work_dir / "request.json", task.request, true);
        task.status = "queued";
        store_.update(task);
        publish_event({{"message_type", "task.status"}, {"task_id", task.task_id}, {"status", "queued"}});
        execute_task(task.task_id);
        return {
            {"schema_version", "1.0"},
            {"message_type", "task.submit.result"},
            {"task_id", task.task_id},
            {"status", store_.get(task.task_id)->status},
        };
    }

    if (message_type == "task.status") {
        const auto task_id = envelope.at("task_id").get<std::string>();
        const auto task = store_.get(task_id);
        if (!task) {
            return {{"message_type", "task.error"}, {"error", "task not found"}, {"task_id", task_id}};
        }
        return {
            {"schema_version", "1.0"},
            {"message_type", "task.status.result"},
            {"task_id", task->task_id},
            {"status", task->status},
            {"started_at_utc", task->started_at_utc},
            {"finished_at_utc", task->finished_at_utc},
        };
    }

    if (message_type == "task.result") {
        const auto task_id = envelope.at("task_id").get<std::string>();
        const auto task = store_.get(task_id);
        if (!task) {
            return {{"message_type", "task.error"}, {"error", "task not found"}, {"task_id", task_id}};
        }
        return {
            {"schema_version", "1.0"},
            {"message_type", "task.result.response"},
            {"task_id", task->task_id},
            {"status", task->status},
            {"exit_code", task->exit_code},
            {"result", task->result},
            {"work_dir", task->work_dir.string()},
        };
    }

    if (message_type == "task.cancel") {
        const auto task_id = envelope.at("task_id").get<std::string>();
        auto task = store_.get(task_id);
        if (!task) {
            return {{"message_type", "task.error"}, {"error", "task not found"}, {"task_id", task_id}};
        }
        task->status = "cancelled";
        task->finished_at_utc = mp::utc_now_iso8601();
        store_.update(*task);
        publish_event({{"message_type", "task.status"}, {"task_id", task_id}, {"status", "cancelled"}});
        return {
            {"schema_version", "1.0"},
            {"message_type", "task.cancel.result"},
            {"task_id", task_id},
            {"status", "cancelled"},
        };
    }

    return {{"message_type", "task.error"}, {"error", "unsupported message_type"}, {"message_type_in", message_type}};
}

void TaskManager::execute_task(const std::string& task_id) {
    auto task = store_.get(task_id);
    if (!task) {
        return;
    }

    const auto plugin = registry_.find_by_tool(task->tool_name);
    if (!plugin) {
        task->status = "failed";
        task->exit_code = 2;
        task->result = {{"error", "plugin not found for tool_name"}, {"tool_name", task->tool_name}};
        task->finished_at_utc = mp::utc_now_iso8601();
        store_.update(*task);
        publish_event({{"message_type", "task.status"}, {"task_id", task_id}, {"status", "failed"}});
        return;
    }

    task->status = "running";
    store_.update(*task);
    publish_event({{"message_type", "task.status"}, {"task_id", task_id}, {"status", "running"}});

    const auto exe = plugin_executable(*plugin);
    const auto request_path = task->work_dir / "request.json";
    std::vector<std::string> args = {
        "run",
        "--input",
        request_path.string(),
        "--work-dir",
        task->work_dir.string(),
        "--output",
        "json",
    };
    if (plugin_supports_dry_run(plugin->manifest) && request_wants_dry_run(task->request)) {
        args.push_back("--dry-run");
    }
    RunPluginOptions run_opts;
    run_opts.timeout_sec = plugin_timeout_sec(plugin->manifest);
    const auto result = run_plugin_command(exe, args, std::nullopt, run_opts);

    task = store_.get(task_id);
    if (!task) {
        return;
    }

    task->exit_code = result.exit_code;
    task->finished_at_utc = mp::utc_now_iso8601();
    try {
        task->result = nlohmann::json::parse(result.stdout_text);
    } catch (...) {
        task->result = {{"stdout", result.stdout_text}, {"stderr", result.stderr_text}};
    }

    if (result.exit_code == 0) {
        task->status = "succeeded";
    } else if (result.exit_code == mp::EXIT_NO_RESULT) {
        task->status = "no_result";
    } else {
        task->status = "failed";
    }

    write_json_file(task->work_dir / "task_result.json",
                    {
                        {"task_id", task->task_id},
                        {"status", task->status},
                        {"exit_code", task->exit_code},
                        {"result", task->result},
                    },
                    true);
    store_.update(*task);
    publish_event({{"message_type", "task.status"}, {"task_id", task_id}, {"status", task->status}});
}

void TaskManager::start() {
    running_ = true;
}

void TaskManager::stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TaskManager::worker_loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

}  // namespace mp::taskmgr
