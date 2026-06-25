#ifdef MP_HAVE_ZMQ

#include <iostream>
#include <string>
#include <vector>

#include <zmq.h>

#include "mp/common/json_io.hpp"
#include "mp/taskmgr/config_resolver.hpp"
#include "mp/taskmgr/task_manager.hpp"

int main(int argc, char** argv) {
    mp::taskmgr::TaskManagerConfig config;
    if (argc > 1) {
        config.plugin_dir = argv[1];
    }
    if (argc > 2) {
        config.work_root = argv[2];
    }
    if (argc > 3) {
        config.plugin_bin_dir = argv[3];
    }
    config = mp::taskmgr::resolve_config(config);

    void* context = zmq_ctx_new();
    void* router = zmq_socket(context, ZMQ_ROUTER);
    void* pub = zmq_socket(context, ZMQ_PUB);

    if (zmq_bind(router, config.router_endpoint.c_str()) != 0 ||
        zmq_bind(pub, config.pub_endpoint.c_str()) != 0) {
        std::cerr << "Failed to bind ZeroMQ endpoints\n";
        return 1;
    }

    mp::taskmgr::TaskManager manager(config);
    manager.start();

    std::cerr << "TaskManager listening on " << config.router_endpoint << " pub " << config.pub_endpoint
              << " plugins=" << config.plugin_dir.string() << " bin=" << config.plugin_bin_dir.string() << '\n';

    while (true) {
        std::vector<std::string> frames;
        while (true) {
            zmq_msg_t msg;
            zmq_msg_init(&msg);
            const int rc = zmq_msg_recv(&msg, router, 0);
            if (rc < 0) {
                zmq_msg_close(&msg);
                break;
            }
            frames.emplace_back(static_cast<const char*>(zmq_msg_data(&msg)), zmq_msg_size(&msg));
            int more = 0;
            size_t more_size = sizeof(more);
            zmq_getsockopt(router, ZMQ_RCVMORE, &more, &more_size);
            zmq_msg_close(&msg);
            if (!more) {
                break;
            }
        }
        if (frames.size() < 2) {
            continue;
        }
        const auto identity = frames[0];
        const auto payload = frames.back();
        try {
            const auto request = nlohmann::json::parse(payload);
            const auto response = manager.handle_message(request);
            const auto response_text = response.dump();
            zmq_send(router, identity.data(), identity.size(), ZMQ_SNDMORE);
            zmq_send(router, "", 0, ZMQ_SNDMORE);
            zmq_send(router, response_text.data(), response_text.size(), 0);
            zmq_send(pub, "task.events", 11, ZMQ_SNDMORE);
            zmq_send(pub, response_text.data(), response_text.size(), 0);
        } catch (const std::exception& ex) {
            const auto err = nlohmann::json{{"message_type", "task.error"}, {"error", ex.what()}}.dump();
            zmq_send(router, identity.data(), identity.size(), ZMQ_SNDMORE);
            zmq_send(router, "", 0, ZMQ_SNDMORE);
            zmq_send(router, err.data(), err.size(), 0);
        }
    }

    zmq_close(router);
    zmq_close(pub);
    zmq_ctx_destroy(context);
    return 0;
}

#else

#include <iostream>

int main() {
    std::cerr << "task-manager was built without libzmq (meson -Dzmq=enabled)\n";
    return 1;
}

#endif
