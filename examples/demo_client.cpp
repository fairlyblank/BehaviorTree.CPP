#include <fstream>
#include <map>
#include <variant>
#include <format>

#include "behaviortree_cpp/json_export.h"
#include "behaviortree_cpp/loggers/groot2_publisher.h"
#include "behaviortree_cpp/loggers/groot2_protocol.h"
#include "behaviortree_cpp/xml_parsing.h"
#include "cppzmq/zmq.hpp"
#include "cppzmq/zmq_addon.hpp"

using namespace std;
using namespace BT;
using namespace BT::Monitor;

void action(zmq::socket_t& client) {

  // FULLTREE
  {
    cout << "\n===============FULLTREE\n";
    RequestHeader req_header(RequestType::FULLTREE);

    zmq::multipart_t req_msg;
    req_msg.addstr(SerializeHeader(req_header));
    if (!req_msg.send(client))
      throw std::runtime_error("FULLTREE: send failed");

    zmq::multipart_t rep_msg;
    if (!rep_msg.recv(client))
      throw std::runtime_error("FULLTREE: recv failed");

    assert(rep_msg.size() == 2);

    auto rep_header = DeserializeReplyHeader(rep_msg[0].to_string());
    assert(rep_header == req_header);
    cout << "unique_id: " << dec << rep_header.request.unique_id << "\n";
    cout << "protocol: " << (uint32_t)rep_header.request.protocol << "\n";
    cout << "type: " << (uint32_t)rep_header.request.type << "\n";
    cout << "tree_id: ";
    for (uint8_t c: rep_header.tree_id)
      cout << hex << (uint32_t)c;
    cout << "\n\n";

    cout << "fulltree:\n" << rep_msg[1].to_string() << endl;
  }

  // STATUS
  {
    cout << "\n===============STATUS\n";
    RequestHeader req_header(RequestType::STATUS);

    zmq::multipart_t req_msg;
    req_msg.addstr(SerializeHeader(req_header));
    if (!req_msg.send(client))
      throw std::runtime_error("STATUS: send failed");

    zmq::multipart_t rep_msg;
    if (!rep_msg.recv(client))
      throw std::runtime_error("STATUS: recv failed");

    assert(rep_msg.size() == 2);

    auto rep_header = DeserializeReplyHeader(rep_msg[0].to_string());
    assert(rep_header == req_header);
    cout << "unique_id: " << dec <<  rep_header.request.unique_id << "\n";
    cout << "protocol: " << (uint32_t)rep_header.request.protocol << "\n";
    cout << "type: " << (uint32_t)rep_header.request.type << "\n";
    cout << "tree_id: ";
    for (uint8_t c: rep_header.tree_id)
      cout << hex << (uint32_t)c;
    cout << "\n\n";

    cout << "all nodes status:\n";
    auto status_buffer = rep_msg[1].to_string();
    auto p = status_buffer.data();
    while (p < &status_buffer[status_buffer.size()]) {
      cout << "\tnode_uid: " << dec << *(uint16_t*)p << ", ";
      p += sizeof(uint16_t);
      cout << "status: " << (int)*(uint8_t*)p << "\n";
      ++p;
    }
  }

  // GET_TRANSITIONS
  {
    cout << "\n===============GET_TRANSITIONS\n";
    RequestHeader req_header(RequestType::GET_TRANSITIONS);

    zmq::multipart_t req_msg;
    req_msg.addstr(SerializeHeader(req_header));
    if (!req_msg.send(client))
      throw std::runtime_error("GET_TRANSITIONS: send failed");

    zmq::multipart_t rep_msg;
    if (!rep_msg.recv(client))
      throw std::runtime_error("GET_TRANSITIONS: recv failed");

    assert(rep_msg.size() == 3);

    auto rep_header = DeserializeReplyHeader(rep_msg[0].to_string());
    assert(rep_header == req_header);
    cout << "unique_id: " << dec << rep_header.request.unique_id << "\n";
    cout << "protocol: " << (uint32_t)rep_header.request.protocol << "\n";
    cout << "type: " << (uint32_t)rep_header.request.type << "\n";
    cout << "tree_id: ";
    for (uint8_t c: rep_header.tree_id)
      cout << hex << (uint32_t)c;
    cout << "\n\n";

    cout << "nodes transitions:\n";
    auto trans_buffer = rep_msg[1].to_string();
    auto p = trans_buffer.data();
    while (p < &trans_buffer[trans_buffer.size()]) {
      uint64_t usec = 0;
      memcpy(&usec, p, 6);
      cout << "\tusec: " << dec << usec << ", ";
      p += 6;
      cout << "node_uid: " << *(uint16_t*)p << ", ";
      p += sizeof(uint16_t);
      cout << "new status: " << (int)*(uint8_t*)p << "\n";
      ++p;
    }
    cout << "\n";

    auto start_time = rep_msg[2].to_string();
    chrono::microseconds usec{stoull(start_time)};
    chrono::time_point<chrono::high_resolution_clock> time_points{usec};
    std::time_t tt = std::chrono::high_resolution_clock::to_time_t(time_points);
    cout << "transition starting time: " << start_time << ", " << std::ctime(&tt) << "\n";
  }
}

int main(int argc, char* argv[]) {
  string ip;

  if (argc < 2)
    ip = "127.0.0.1";
  else
    ip = argv[1];

  zmq::context_t context;
  zmq::socket_t client(context, ZMQ_REQ);
  client.set(zmq::sockopt::linger, 0);
  client.set(zmq::sockopt::rcvtimeo, 100);
  client.set(zmq::sockopt::sndtimeo, 1000);

  try {
    client.connect("tcp://" + ip + ":1667");
    action(client);
  } catch (std::exception const& e) {
    cout << format("exception: {}\n", e.what());
  }

  return 0;
}
