#include <fstream>
#include <map>
#include <variant>
//#include <format>

#include "behaviortree_cpp/loggers/bt_file_logger_v2.h"
#include "behaviortree_cpp/bt_factory.h"
#include "behaviortree_cpp/loggers/ui_publisher.h"
#include "behaviortree_cpp/loggers/bt_sqlite_logger.h"
#include "behaviortree_cpp/xml_parsing.h"
#include "behaviortree_cpp/json_export.h"

using namespace std;

struct Demo {
  BT::BehaviorTreeFactory factory;
  BT::Tree tree;
  std::shared_ptr<BT::UIPublisher> pub;
  std::shared_ptr<BT::FileLogger2> logger1;
  std::shared_ptr<BT::SqliteLogger> logger2;

  Demo(): factory() {}

  void init() {
    std::ifstream file("demo.xml", std::ios::binary);
    if (!file) {
      throw std::runtime_error("open file failed");
    }

    std::string content(std::istreambuf_iterator<char>{file}, {});
    // cout << std::format("content: {}\n", content.data());

    // Groot2 editor requires a model of your registered Nodes.
    // You don't need to write that by hand, it can be automatically
    // generated using the following command.
    std::string xml_models = BT::writeTreeNodesModelXML(factory);

    factory.registerBehaviorTreeFromText(content.data());

    tree = factory.createTree("MainTree");

    auto xml_fulltree = BT::WriteTreeToXML(tree, true, true);
    std::ofstream file_o("demo_full.xml", std::ios::binary);
    if (!file_o) {
      throw std::runtime_error("create file failed");
    }
    file_o << xml_fulltree;

    // Connect the UIPublisher. This will allow Groot2 to
    // get the tree and poll status updates.
    const unsigned port = 1667;
    pub = std::make_shared<BT::UIPublisher>(tree, port);

    // Add two more loggers, to save the transitions into a file.
    // Both formats are compatible with Groot2

    // Lightweight serialization
    logger1 = std::make_shared<BT::FileLogger2>(tree, "demo.btlog");
    // SQLite logger can save multiple sessions into the same database
    bool append_to_database = true;
    logger2 = std::make_shared<BT::SqliteLogger>(tree, "demo.db3", append_to_database);
  }

  bool step() {
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

      return BT::isStatusCompleted(status);

    } catch (std::exception const& e) {
      cout << "exception: " << e.what() << "\n";
    }

    return true;
  }

};

int main()
{
  while (true) {
    Demo demo;
    try {
      demo.init();
    } catch (std::exception const& e) {
      cout << "init failed: " << e.what() << "\n";
      return 1;
    }

    std::cout << "Start" << std::endl;
    while(true)
    {
      if (demo.step())
        break;

      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
  }

  return 0;
}
