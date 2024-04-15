#include <fstream>
#include <map>
#include <variant>
#include <format>

#include "behaviortree_cpp/loggers/bt_file_logger_v2.h"
#include "behaviortree_cpp/bt_factory.h"
#include "behaviortree_cpp/loggers/groot2_publisher.h"
#include "behaviortree_cpp/loggers/bt_sqlite_logger.h"
#include "behaviortree_cpp/xml_parsing.h"
#include "behaviortree_cpp/json_export.h"

using namespace std;

struct Demo {
  BT::BehaviorTreeFactory factory;
  BT::Tree tree;
  std::shared_ptr<BT::Groot2Publisher> pub;
  std::shared_ptr<BT::FileLogger2> logger1;
  std::shared_ptr<BT::SqliteLogger> logger2;

  Demo(): factory() {
    cout << "Demo cons\n";

    std::ifstream file("demo.xml", std::ios::binary);
    if (!file) {
      cout << "open file failed\n";
      return;
    }

    std::string content(std::istreambuf_iterator<char>{file}, {});
    cout << std::format("content: {}\n", content.data());

    // Groot2 editor requires a model of your registered Nodes.
    // You don't need to write that by hand, it can be automatically
    // generated using the following command.
    std::string xml_models = BT::writeTreeNodesModelXML(factory);
    cout << "1\n";

    factory.registerBehaviorTreeFromText(content.data());
    cout << "2\n";

    try {
      tree = factory.createTree("MainTree");
    } catch (std::exception const& e) {
      cout << format("createTree failed: {}\n", e.what());
    }
    cout << "3\n";

    auto xml_fulltree = BT::WriteTreeToXML(tree, true, true);
    std::ofstream file_o("demo_full.xml", std::ios::binary);
    if (!file_o) {
      cout << "create file failed\n";
      return;
    }
    file_o << xml_fulltree;

    // Connect the Groot2Publisher. This will allow Groot2 to
    // get the tree and poll status updates.
    const unsigned port = 1667;
    pub = std::make_shared<BT::Groot2Publisher>(tree, port);

    // Add two more loggers, to save the transitions into a file.
    // Both formats are compatible with Groot2

    // Lightweight serialization
    logger1 = std::make_shared<BT::FileLogger2>(tree, "demo.btlog");
    // SQLite logger can save multiple sessions into the same database
    bool append_to_database = true;
    logger2 = std::make_shared<BT::SqliteLogger>(tree, "demo.db3", append_to_database);

    cout << "Demo cons leave\n";
  }

  void step() {
    try {
      tree.tickOnce();
    } catch (std::exception const& e) {
      cout << format("exception: {}\n", e.what());
    }
  }

};

int main()
{
  Demo demo;

  while(1)
  {
    std::cout << "Start" << std::endl;
    demo.step();

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  }

  return 0;
}
