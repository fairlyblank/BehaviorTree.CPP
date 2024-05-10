#include <fstream>
#include <map>
#include <variant>
// #include <format>

#include "behaviortree_cpp/bt_factory.h"
#include "behaviortree_cpp/json_export.h"
#include "behaviortree_cpp/loggers/bt_file_logger_v2.h"
#include "behaviortree_cpp/loggers/bt_sqlite_logger.h"
#include "behaviortree_cpp/loggers/groot2_protocol.h"
#include "behaviortree_cpp/loggers/ui_publisher.h"
#include "behaviortree_cpp/xml_parsing.h"

#include "cppzmq/zmq.hpp"
#include "cppzmq/zmq_addon.hpp"

using namespace std;
using namespace BT;

class BTManager {
    enum State {
        STOPPED = 0,
        RUNNING,
        SUSPENDED,
        WAIT_AUTH,
        SUCCESS,
    };

    enum Result {
        OK = 0,
        ERROR,
    };

   private:
    std::atomic<uint8_t> state = STOPPED;
    std::atomic_bool active_manager = false;

    uint16_t manager_port = 1666;
    zmq::context_t context;
    zmq::socket_t server;
    std::thread manager_thread;

    BT::BehaviorTreeFactory factory;
    BT::Tree tree;
    std::shared_ptr<BT::UIPublisher> pub;
    // std::shared_ptr<BT::FileLogger2> logger1;
    std::shared_ptr<BT::SqliteLogger> logger2;

   public:
    BTManager() : BTManager(1666) {}
    BTManager(uint16_t port) : manager_port(port), context(), server(context, ZMQ_REP), factory() {
        server.set(zmq::sockopt::linger, 0);
        int timeout_rcv = 100;
        server.set(zmq::sockopt::rcvtimeo, timeout_rcv);
        int timeout_ms = 1000;
        server.set(zmq::sockopt::sndtimeo, timeout_ms);

        auto server_address = "tcp://*:" + std::to_string(manager_port);
        server.bind(server_address.data());

        manager_thread = std::thread(&BTManager::managerLoop, this);
    }
    ~BTManager() {
        active_manager = false;
        if (manager_thread.joinable())
            manager_thread.join();
    }

    void managerLoop() {
        // auto serialized_uuid = CreateRandomUUID();
        active_manager = true;
        auto& socket = server;
        auto sendErrorReply = [&socket](const std::string& msg) {
            zmq::multipart_t error_msg;
            error_msg.addstr("error");
            error_msg.addstr(msg);
            error_msg.send(socket);
        };

        while (active_manager) {
            zmq::multipart_t requestMsg;
            if (!requestMsg.recv(socket) || requestMsg.size() == 0) {
                continue;
            }

            std::string const request_str = requestMsg[0].to_string();
            if (request_str.size() != Monitor::RequestHeader::size()) {
                sendErrorReply("wrong request header");
                continue;
            }

            auto request_header = Monitor::DeserializeRequestHeader(request_str);

            Monitor::ReplyHeader reply_header;
            reply_header.request = request_header;
            reply_header.request.protocol = Monitor::kProtocolID;
            // reply_header.tree_id = serialized_uuid;

            zmq::multipart_t reply_msg;
            reply_msg.addstr(Monitor::SerializeHeader(reply_header));
            auto sendResultReply = [&socket, &reply_msg](uint8_t result, std::string const& msg) {
                reply_msg.addtyp(result);
                reply_msg.addstr(msg);
                reply_msg.send(socket);
            };

            switch (request_header.type) {
                case Monitor::RequestType::UPDATE_MODE: {
                    if (requestMsg.size() != 2) {
                        sendResultReply(ERROR, "UPDATE_MODE: must be 2 parts message");
                        break;
                    }

                    if (state.load(std::memory_order_relaxed) != STOPPED) {
                        sendResultReply(ERROR, "UPDATE_MODE: not STOPPED");
                    } else {
                        auto content = requestMsg[1].to_string();
                        try {
                            BehaviorTreeFactory factory;
                            auto xml_models = BT::writeTreeNodesModelXML(factory);
                            factory.registerBehaviorTreeFromText(content.data());
                            Tree tree = factory.createTree("MainTree");

                            std::ofstream file_o("BlackStart.xml", std::ios::binary);
                            if (!file_o) {
                                throw std::runtime_error(
                                    "UPDATE_MODE: create file BlackStart.xml failed");
                            }
                            file_o << content;

                            sendResultReply(OK, "OK");
                        } catch (std::exception const& e) {
                            sendResultReply(ERROR, "UPDATE_MODE: mode xml invalid");
                        }
                    }
                } break;

                case Monitor::RequestType::START: {
                    auto cur = state.load(std::memory_order_relaxed);
                    if (cur == STOPPED) {
                        if (load()) {
                            state.store(RUNNING, std::memory_order_relaxed);
                            sendResultReply(OK, "OK");
                        } else {
                            sendResultReply(ERROR, "START: load failed");
                        }
                    } else if (cur == SUSPENDED) {
                        state.store(RUNNING, std::memory_order_relaxed);
                        sendResultReply(OK, "OK");
                    } else {
                        sendResultReply(ERROR, "START: state invalid");
                    }
                } break;

                case Monitor::RequestType::SUSPEND: {
                    auto cur = state.load(std::memory_order_relaxed);
                    if (cur == RUNNING) {
                        state.store(SUSPENDED, std::memory_order_relaxed);
                        sendResultReply(OK, "OK");
                    } else {
                        sendResultReply(ERROR, "SUSPEND: state invalid");
                    }
                } break;

                case Monitor::RequestType::STOP: {
                    state.store(STOPPED, std::memory_order_relaxed);
                    sendResultReply(OK, "OK");
                } break;

                case Monitor::RequestType::AUTH: {
                    auto cur = state.load(std::memory_order_relaxed);
                    if (cur == WAIT_AUTH) {
                        state.store(RUNNING, std::memory_order_relaxed);
                        sendResultReply(OK, "OK");
                    } else {
                        sendResultReply(ERROR, "AUTH: state invalid");
                    }
                } break;

                case Monitor::RequestType::STATE: {
                    auto cur = state.load(std::memory_order_relaxed);
                    sendResultReply(cur, "OK");
                } break;

                default: {
                    sendResultReply(ERROR, "Request not recognized");
                } break;
            }
        }
    }

    bool load() {
        if (auto res = state.load(std::memory_order_relaxed); res != STOPPED) {
            std::cerr << "load from invalid state: " << (int)res << "\n";
            return false;
        }

        std::ifstream file("BlackStart.xml", std::ios::binary);
        if (!file) {
            std::cerr << "init: open file BlackStart.xml failed\n";
            return false;
        }

        std::string content(std::istreambuf_iterator<char>{file}, {});
        // cout << std::format("content: {}\n", content.data());

        try {
            // Groot2 editor requires a model of your registered Nodes.
            // You don't need to write that by hand, it can be automatically
            // generated using the following command.
            std::string xml_models = BT::writeTreeNodesModelXML(factory);

            factory.registerBehaviorTreeFromText(content.data());

            tree = factory.createTree("MainTree");
        } catch (std::exception const& e) {
            std::cerr << "load failed: " << e.what() << '\n';
            return false;
        }

        auto xml_fulltree = BT::WriteTreeToXML(tree, true, true);
        std::ofstream file_o("BlackStart_full.xml", std::ios::binary);
        if (!file_o) {
            std::cerr << "init: create file BlackStart_full.xml failed\n";
            return false;
        }
        file_o << xml_fulltree;

        // Connect the UIPublisher. This will allow Groot2 to
        // get the tree and poll status updates.
        // const unsigned port = 1667;
        pub = std::make_shared<BT::UIPublisher>(tree, manager_port + 1);

        // Add two more loggers, to save the transitions into a file.
        // Both formats are compatible with Groot2

        // Lightweight serialization
        // logger1 = std::make_shared<BT::FileLogger2>(tree, "demo.btlog");
        // SQLite logger can save multiple sessions into the same database
        bool append_to_database = true;
        logger2 = std::make_shared<BT::SqliteLogger>(tree, "BlackStart.db3", append_to_database);

        return true;
    }

    void step() {
        auto cur = state.load(std::memory_order_relaxed);
        switch (cur) {
            case STOPPED: {
            } break;

            case RUNNING: {
                auto status = tick();
                if (status == NodeStatus::SUCCESS)
                    state.store(SUCCESS, std::memory_order_relaxed);
                else if (status == NodeStatus::FAILURE)
                    state.store(STOPPED, std::memory_order_relaxed);
                else {}
            } break;

            case SUSPENDED: {
            } break;

            case WAIT_AUTH: {
            } break;

            case SUCCESS: {

            } break;

            default: {
            } break;
        }
    }

    NodeStatus tick() {
        try {
            auto status = tree.tickOnce();
            switch (status) {
                case BT::NodeStatus::IDLE:
                    cout << "IDLE" << "\n";
                    break;
                case BT::NodeStatus::RUNNING:
                    cout << "RUNNING" << "\n";
                    break;
                case BT::NodeStatus::SUCCESS:
                    cout << "SUCCESS" << "\n";
                    break;
                case BT::NodeStatus::FAILURE:
                    cout << "FAILURE" << "\n";
                    break;
                default:
                    cout << (int)status << "\n";
                    break;
            }

            return status;
        } catch (std::exception const& e) {
            std::cerr << "tick exception: " << e.what() << "\n";
        }

        return NodeStatus::FAILURE;
    }

    bool succeeded() {
        return state.load(std::memory_order_relaxed) == SUCCESS;
    }
};

int main() {
    while (true) {
        BTManager manager;
        //

        std::cout << "Start" << std::endl;
        while (true) {
            manager.step();
            if (manager.succeeded())
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    }

    return 0;
}
