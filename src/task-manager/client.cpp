#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "mp/common/exit_codes.hpp"
#include "mp/common/json_io.hpp"
#include "mp/taskmgr/config_resolver.hpp"
#include "mp/taskmgr/task_manager.hpp"

namespace {

void usage() {
    std::cerr << "Usage: task-client [--root DIR] [--plugin-bin DIR] [--work-root DIR] [--plugin-index FILE] <envelope.json>\n"
              << "Env: SATELLITE_PLUGIN_INDEX, SATELLITE_PLUGIN_DIR, SATELLITE_PLUGIN_BIN, SATELLITE_TASK_WORK_ROOT\n"
              << "     (legacy: MISSION_PLANER_ROOT, MISSION_PLANER_BIN)\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::optional<std::filesystem::path> envelope_path;
    mp::taskmgr::TaskManagerConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--root" && i + 1 < argc) {
            const auto root = std::filesystem::path(argv[++i]);
            config.plugin_dir = root / "configs" / "plugins";
            config.work_root = root / "var" / "tasks";
        } else if (arg == "--plugin-bin" && i + 1 < argc) {
            config.plugin_bin_dir = argv[++i];
        } else if (arg == "--work-root" && i + 1 < argc) {
            config.work_root = argv[++i];
        } else if (arg == "--plugin-index" && i + 1 < argc) {
            config.plugin_index = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return mp::EXIT_OK;
        } else if (arg.rfind('-', 0) == 0) {
            std::cerr << "Unknown option: " << arg << '\n';
            usage();
            return mp::EXIT_USAGE;
        } else {
            envelope_path = arg;
        }
    }

    if (!envelope_path) {
        usage();
        return mp::EXIT_USAGE;
    }

    config = mp::taskmgr::resolve_config(config);
    mp::taskmgr::TaskManager manager(config);
    const auto envelope = mp::read_json_file(*envelope_path);
    const auto response = manager.handle_message(envelope);
    mp::write_json_stdout(response, true);
    const auto status = response.value("status", "");
    if (status == "failed") {
        return mp::EXIT_RETRYABLE;
    }
    if (status == "no_result") {
        return mp::EXIT_NO_RESULT;
    }
    return mp::EXIT_OK;
}
