#ifdef MP_HAVE_ZMQ

#include <iostream>
#include <string>
#include <vector>

#include <zmq.h>

#include "mp/common/exit_codes.hpp"
#include "mp/common/json_io.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: task-zmq-client <endpoint> <envelope.json>\n"
                  << "Example: task-zmq-client tcp://127.0.0.1:5555 samples/task_submit.json\n";
        return mp::EXIT_USAGE;
    }

    const std::string endpoint = argv[1];
    const auto envelope = mp::read_json_file(argv[2]);
    const std::string payload = envelope.dump();

    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_DEALER);
    zmq_setsockopt(socket, ZMQ_IDENTITY, "task-zmq-client", 15);

    if (zmq_connect(socket, endpoint.c_str()) != 0) {
        std::cerr << "Failed to connect: " << endpoint << '\n';
        return mp::EXIT_DEPENDENCY;
    }

    zmq_send(socket, "", 0, ZMQ_SNDMORE);
    zmq_send(socket, payload.data(), payload.size(), 0);

    int timeout_ms = 300000;
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

    std::vector<std::string> frames;
    while (true) {
        zmq_msg_t msg;
        zmq_msg_init(&msg);
        if (zmq_msg_recv(&msg, socket, 0) < 0) {
            zmq_msg_close(&msg);
            std::cerr << "Failed to receive response: " << zmq_strerror(zmq_errno()) << '\n';
            return mp::EXIT_RETRYABLE;
        }
        frames.emplace_back(static_cast<const char*>(zmq_msg_data(&msg)), zmq_msg_size(&msg));
        int more = 0;
        size_t more_size = sizeof(more);
        zmq_getsockopt(socket, ZMQ_RCVMORE, &more, &more_size);
        zmq_msg_close(&msg);
        if (!more) {
            break;
        }
    }
    if (frames.empty()) {
        std::cerr << "Empty response from TaskManager\n";
        return mp::EXIT_RETRYABLE;
    }

    std::string response = frames.back();
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
        if (!it->empty()) {
            response = *it;
            break;
        }
    }

    mp::write_json_stdout(nlohmann::json::parse(response), true);

    zmq_close(socket);
    zmq_ctx_destroy(context);
    return mp::EXIT_OK;
}

#else

#include <iostream>

#include "mp/common/exit_codes.hpp"

int main() {
    std::cerr << "task-zmq-client requires libzmq (build with -Dzmq=enabled)\n";
    return mp::EXIT_DEPENDENCY;
}

#endif
