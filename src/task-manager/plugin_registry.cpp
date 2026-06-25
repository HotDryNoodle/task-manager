#include "mp/taskmgr/plugin_registry.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <sys/wait.h>

#include "mp/common/json_io.hpp"

#include "mp/taskmgr/plugin_resolve.hpp"

namespace mp::taskmgr {

namespace {

std::string make_task_id() {
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream ss;
    ss << "task_" << now;
    return ss.str();
}

std::string shell_quote(const std::string& text) {
    std::string out = "'";
    for (const char ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

}  // namespace

void PluginRegistry::clear() {
    plugins_.clear();
}

void PluginRegistry::add_plugin(PluginInfo info) {
    for (const auto& existing : plugins_) {
        if (existing.tool_name == info.tool_name) {
            return;
        }
    }
    plugins_.push_back(std::move(info));
}

void PluginRegistry::load_index(const std::filesystem::path& index_path) {
    clear();
    if (!std::filesystem::exists(index_path)) {
        return;
    }
    const auto index = mp::read_json_file(index_path);
    if (!index.contains("plugins") || !index.at("plugins").is_array()) {
        return;
    }
    for (const auto& entry : index.at("plugins")) {
        PluginInfo info;
        info.tool_name = entry.at("tool_name").get<std::string>();
        info.manifest_path = entry.at("manifest_path").get<std::string>();
        info.manifest = mp::read_json_file(info.manifest_path);
        info.executable = info.manifest.at("executable").get<std::string>();
        if (entry.contains("executable_resolved")) {
            info.executable_resolved = entry.at("executable_resolved").get<std::string>();
        }
        add_plugin(std::move(info));
    }
}

void PluginRegistry::scan_directory(const std::filesystem::path& plugin_dir) {
    if (!std::filesystem::exists(plugin_dir)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(plugin_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().filename() == "plugins.index.json") {
            continue;
        }
        if (entry.path().extension() != ".json") {
            continue;
        }
        const auto manifest = mp::read_json_file(entry.path());
        PluginInfo info;
        info.manifest = manifest;
        info.manifest_path = entry.path();
        info.tool_name = manifest.at("name").get<std::string>();
        info.executable = manifest.at("executable").get<std::string>();
        add_plugin(std::move(info));
    }
}

void PluginRegistry::scan_directories(const std::vector<std::filesystem::path>& plugin_dirs) {
    for (const auto& dir : plugin_dirs) {
        scan_directory(dir);
    }
}

std::optional<PluginInfo> PluginRegistry::find_by_tool(const std::string& tool_name) const {
    for (const auto& p : plugins_) {
        if (p.tool_name == tool_name) {
            return p;
        }
    }
    return std::nullopt;
}

std::vector<PluginInfo> PluginRegistry::list() const {
    return plugins_;
}

ProcessResult run_plugin_command(const std::filesystem::path& executable,
                                 const std::vector<std::string>& args,
                                 const std::optional<std::string>& stdin_payload,
                                 const RunPluginOptions& options) {
    std::ostringstream cmd;
    if (options.timeout_sec && *options.timeout_sec > 0) {
        cmd << "timeout " << *options.timeout_sec << 's';
    }
    cmd << ' ' << shell_quote(executable.string());
    for (const auto& arg : args) {
        cmd << ' ' << shell_quote(arg);
    }

    ProcessResult result;
    std::array<char, 256> buffer{};
    std::unique_ptr<FILE, decltype(&pclose)> pipe(nullptr, pclose);
    if (stdin_payload) {
        cmd << " 2>&1";
        pipe.reset(popen(cmd.str().c_str(), "w"));
        if (!pipe) {
            throw std::runtime_error("Failed to start plugin process");
        }
        fwrite(stdin_payload->data(), 1, stdin_payload->size(), pipe.get());
        const int status = pclose(pipe.release());
        result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
        return result;
    }

    cmd << " 2>&1";
    pipe.reset(popen(cmd.str().c_str(), "r"));
    if (!pipe) {
        throw std::runtime_error("Failed to start plugin process: " + executable.string());
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get())) {
        result.stdout_text += buffer.data();
    }
    const int status = pclose(pipe.release());
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
    return result;
}

TaskRecord TaskStore::create_task(const nlohmann::json& envelope) {
    TaskRecord task;
    task.task_id = envelope.contains("task_id") ? envelope.at("task_id").get<std::string>() : make_task_id();
    task.request_id = envelope.at("request_id").get<std::string>();
    task.tool_name = envelope.at("tool_name").get<std::string>();
    task.status = "submitted";
    task.request = envelope.at("payload");
    task.request["request_id"] = task.request_id;
    if (envelope.contains("trace_id")) {
        task.request["trace_id"] = envelope.at("trace_id");
    }
    if (envelope.contains("idempotency_key")) {
        task.idempotency_key = envelope.at("idempotency_key").get<std::string>();
        task.request["idempotency_key"] = *task.idempotency_key;
    } else if (task.request.contains("idempotency_key")) {
        task.idempotency_key = task.request.at("idempotency_key").get<std::string>();
    }
    task.started_at_utc = mp::utc_now_iso8601();
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_[task.task_id] = task;
    if (task.idempotency_key) {
        idempotency_index_[*task.idempotency_key] = task.task_id;
    }
    return task;
}

std::optional<TaskRecord> TaskStore::find_by_idempotency_key(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = idempotency_index_.find(key);
    if (it == idempotency_index_.end()) {
        return std::nullopt;
    }
    const auto task_it = tasks_.find(it->second);
    if (task_it == tasks_.end()) {
        return std::nullopt;
    }
    return task_it->second;
}

std::optional<TaskRecord> TaskStore::get(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void TaskStore::update(const TaskRecord& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_[task.task_id] = task;
}

}  // namespace mp::taskmgr
